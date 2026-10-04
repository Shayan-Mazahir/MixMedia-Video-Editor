// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "timeline.h"
#include "stream_copy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ve {

namespace {

// Does this frame have see-through bits (like a title picture)?
bool hasAlpha(const AVFrame* f)
{
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(AVPixelFormat(f->format));
    return desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA) && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

// Paints src over dst, respecting src's own see-through-ness and an overall opacity
void blendRow(uint8_t* dst, const uint8_t* src, int pixels, double opacity)
{
    const int o = int(opacity * 256);
    for (int i = 0; i < pixels; ++i, src += 4, dst += 4) {
        int a = (src[3] * o) >> 8; // 0..255
        if (a == 0)
            continue;
        for (int c = 0; c < 3; ++c)
            dst[c] = uint8_t((src[c] * a + dst[c] * (255 - a)) / 255);
    }
}

} // namespace

Timeline::~Timeline()
{
    m_slots.clear(); // decoders first, they still use the card
    av_buffer_unref(&m_hwDevice);
}

void Timeline::setHardwareDevice(AVBufferRef* device)
{
    if (device == m_hwDevice)
        return;
    for (Slot& s : m_slots) { // reopen everything on the new device
        s.video.reset();
        s.videoFailed = false;
    }
    av_buffer_unref(&m_hwDevice);
    m_hwDevice = device ? av_buffer_ref(device) : nullptr;
}

void Timeline::setClips(const std::vector<Clip>& clips)
{
    // Opening files is slow, so hand existing decoders over to the new clips where we can
    std::vector<Slot> old = std::move(m_slots);
    m_slots.clear();
    m_slots.reserve(clips.size());

    for (const Clip& clip : clips) {
        Slot slot;
        slot.clip = clip;
        // Best match: the same clip as before (its decoder is already in the right spot),
        // otherwise any clip from the same file
        Slot* donor = nullptr;
        for (int pass = 0; pass < 2 && !donor; ++pass) {
            for (Slot& o : old) {
                bool hasSomething = o.video || o.audio || o.videoFailed || o.audioFailed;
                bool same = o.clip.start == clip.start && o.clip.in == clip.in;
                if (o.clip.path == clip.path && hasSomething && (pass == 1 || same)) {
                    donor = &o;
                    break;
                }
            }
        }
        if (donor) {
            slot.video = std::move(donor->video);
            slot.audio = std::move(donor->audio);
            slot.videoFailed = donor->videoFailed;
            slot.audioFailed = donor->audioFailed;
            slot.videoUsed = donor->videoUsed;
            slot.audioUsed = donor->audioUsed;
            slot.still = std::move(donor->still);
            slot.stillW = donor->stillW;
            slot.stillH = donor->stillH;
            slot.stillBgra = donor->stillBgra;
            donor->videoFailed = donor->audioFailed = false;
        }
        m_slots.push_back(std::move(slot));
    }
    linkTransitions();
}

double Timeline::duration() const
{
    double end = 0.0;
    for (const Slot& s : m_slots)
        end = std::max(end, s.clip.end());
    return end;
}

void Timeline::usePreviewSettings()
{
    m_threads = 4; // plenty for smooth playback, without one thread per core per clip
    m_fastScaling = true;
    // (Decoding on the graphics card was tried here, but copying frames back off the card
    // cost more CPU than decoding them on the CPU. The card earns its keep in export.)
}

namespace {
constexpr int MaxOpenDecoders = 3; // each holds frames and threads, so only keep a handful

// Closes the least recently used decoders (of one kind) until there are few enough
template <typename Slots, typename Get, typename Used>
void trimDecoders(Slots& slots, double t, Get get, Used used)
{
    while (true) {
        int open = 0;
        decltype(&slots[0]) oldest = nullptr;
        for (auto& s : slots) {
            if (!get(s))
                continue;
            ++open;
            if (!s.clip.inUse(t) && (!oldest || used(s) < used(*oldest)))
                oldest = &s;
        }
        if (open <= MaxOpenDecoders || !oldest)
            return;
        get(*oldest).reset();
    }
}
} // namespace

void Timeline::closeIdleVideo(double t)
{
    trimDecoders(m_slots, t, [](Slot& s) -> auto& { return s.video; }, [](Slot& s) { return s.videoUsed; });
}

void Timeline::closeIdleAudio(double t)
{
    trimDecoders(m_slots, t, [](Slot& s) -> auto& { return s.audio; }, [](Slot& s) { return s.audioUsed; });
}

namespace {
// A decoder for the same file that's sitting idle (its clip isn't playing right now)
template <typename Slots, typename Slot, typename Get, typename Used>
auto* idleDecoderFor(Slots& slots, const Slot& me, double t, Get get, Used used)
{
    decltype(&slots[0]) best = nullptr;
    for (auto& o : slots) {
        if (&o == &me || !get(o) || o.clip.path != me.clip.path || o.clip.inUse(t))
            continue;
        if (!best || used(o) > used(*best)) // the most recently used is probably nearest
            best = &o;
    }
    return best;
}
} // namespace

VideoReader* Timeline::videoFor(Slot& s, double t)
{
    s.videoUsed = ++m_videoTick;
    if (!s.video && !s.videoFailed) {
        // Cut a video into lots of pieces? They can all share one decoder as playback moves along.
        if (Slot* donor = idleDecoderFor(m_slots, s, t, [](Slot& o) -> auto& { return o.video; },
                                         [](Slot& o) { return o.videoUsed; })) {
            s.video = std::move(donor->video);
            return s.video.get();
        }
        s.video = std::make_unique<VideoReader>();
        if (!s.video->open(s.clip.path, m_hwDevice, m_threads)) {
            s.video.reset();
            s.videoFailed = true;
        } else {
            s.video->setFastScaling(m_fastScaling);
        }
    }
    return s.video.get();
}

AudioReader* Timeline::audioFor(Slot& s, double t)
{
    s.audioUsed = ++m_audioTick;
    if (!s.audio && !s.audioFailed) {
        if (Slot* donor = idleDecoderFor(m_slots, s, t, [](Slot& o) -> auto& { return o.audio; },
                                         [](Slot& o) { return o.audioUsed; })) {
            s.audio = std::move(donor->audio);
            return s.audio.get();
        }
        s.audio = std::make_unique<AudioReader>();
        if (!s.audio->open(s.clip.path)) {
            s.audio.reset();
            s.audioFailed = true;
        }
    }
    return s.audio.get();
}

bool Timeline::renderVideoDirect(double t, int w, int h, AVFrame* out)
{
    closeIdleVideo(t);
    Slot* only = nullptr;
    for (Slot& s : m_slots) {
        if (!s.clip.useVideo || !s.clip.inUse(t))
            continue;
        if (only)
            return false; // stacked clips need the full treatment
        only = &s;
    }
    if (!only || only->clip.envelope(t) < 1.0 || !only->clip.plain())
        return false; // fades, effects and resizing need the full treatment too

    VideoReader* reader = videoFor(*only, t);
    const AVFrame* frame = reader ? reader->frameAt(only->clip.sourceTime(t)) : nullptr;
    if (!frame || frame->width <= 0 || frame->height <= 0 || hasAlpha(frame))
        return false;

    // Different shape would need black bars, so leave that to the normal path
    double shapeDiff = std::abs(double(frame->width) / frame->height - double(w) / h);
    if (shapeDiff > 0.01)
        return false;

    // Best case: the decoded frame is already exactly what the encoder wants. Just pass it along!
    bool hd709 = frame->colorspace == AVCOL_SPC_BT709
                 || (frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->height >= 720);
    bool onCard = frame->hw_frames_ctx != nullptr; // decoded on the graphics card, encoder's on it too
    if (frame->width == w && frame->height == h && (onCard || frame->format == AV_PIX_FMT_YUV420P)
        && frame->color_range != AVCOL_RANGE_JPEG && hd709)
        return av_frame_ref(out, frame) == 0;

    return reader->toYuv420(frame, w, h, out);
}

namespace {

double smooth(double p) // eases in and out, so movement doesn't start or stop with a jolt
{
    p = std::clamp(p, 0.0, 1.0);
    return p * p * (3 - 2 * p);
}

double easeOut(double p) // fast start, gentle landing
{
    p = std::clamp(p, 0.0, 1.0);
    return 1 - (1 - p) * (1 - p) * (1 - p);
}

} // namespace

void Timeline::linkTransitions()
{
    for (Slot& s : m_slots) {
        s.prev = s.next = -1;
        s.clip.preRoll = s.clip.postRoll = 0.0;
        s.clip.blendInFrom = s.clip.blendInTo = s.clip.blendOutFrom = s.clip.blendOutTo = 0.0;
    }
    auto link = [&](size_t i, size_t j, double from, double to) {
        m_slots[j].prev = int(i);
        m_slots[i].next = int(j);
        m_slots[j].clip.blendInFrom = m_slots[i].clip.blendOutFrom = from;
        m_slots[j].clip.blendInTo = m_slots[i].clip.blendOutTo = to;
    };

    for (size_t j = 0; j < m_slots.size(); ++j) {
        Clip& b = m_slots[j].clip;
        if (b.kind != Clip::Kind::Media || b.transition == Transition::None)
            continue;

        // Filmora style: this clip overlaps the end of the one before, and they blend across the overlap.
        // (Works even when both bits come from one continuous recording: you see the end of the
        // first blending into the start of the second.)
        bool linked = false;
        for (size_t i = 0; i < m_slots.size() && !linked; ++i) {
            const Clip& a = m_slots[i].clip;
            if (i == j || a.kind != Clip::Kind::Media || a.layer != b.layer || a.start >= b.start - 0.001
                || a.end() <= b.start + 0.001)
                continue;
            link(i, j, b.start, std::min(a.end(), b.end()));
            linked = true;
        }

        // Otherwise, clips that just touch: borrow a little footage either side of the cut
        for (size_t i = 0; i < m_slots.size() && !linked; ++i) {
            Clip& a = m_slots[i].clip;
            if (i == j || a.kind != Clip::Kind::Media || a.layer != b.layer || std::abs(a.end() - b.start) > 0.01)
                continue;
            // Half the transition comes from each side, so it can't be longer than either clip
            double d = std::min({ b.transitionDuration, a.duration, b.duration });
            if (d <= 0.0)
                break;
            b.preRoll = d / 2;
            a.postRoll = d / 2;
            link(i, j, b.start - d / 2, b.start + d / 2);
            linked = true;
        }
    }
}

void Timeline::drawSlot(Slot& s, double t, int w, int h, uint8_t* canvas, bool bgra, DrawMods mods)
{
    VideoReader* reader = videoFor(s, t);
    if (!reader)
        return;
    const Clip& c = s.clip;
    reader->setBackwards(c.reverse); // (decoders get shared between clips, so say which way every time)
    const AVFrame* frame = reader->frameAt(std::max(0.0, c.sourceTime(t)));
    if (!frame || frame->width <= 0 || frame->height <= 0)
        return;

    // Arriving or leaving animations
    // (returns how much of the clip a wipe has uncovered, 1 for everything else)
    auto animate = [&](Anim anim, double e) {
        switch (anim) {
        case Anim::Fade: mods.opacity *= e; break;
        case Anim::SlideLeft: mods.dx -= (1 - e) * w; break;
        case Anim::SlideRight: mods.dx += (1 - e) * w; break;
        case Anim::SlideUp: mods.dy -= (1 - e) * h; break;
        case Anim::SlideDown: mods.dy += (1 - e) * h; break;
        case Anim::Zoom:
            mods.scale *= 0.5 + 0.5 * e;
            mods.opacity *= e;
            break;
        case Anim::Rise:
            mods.dy += (1 - e) * h * 0.06;
            mods.opacity *= e;
            break;
        case Anim::Wipe: return e; // handled once we know where the clip sits
        default: break;
        }
        return 1.0;
    };
    double wipeIn = 1.0, wipeOut = 1.0;
    if (c.animIn != Anim::None && c.animInDuration > 0)
        wipeIn = animate(c.animIn, easeOut((t - c.start) / c.animInDuration));
    if (c.animOut != Anim::None && c.animOutDuration > 0)
        wipeOut = animate(c.animOut, easeOut((c.end() - t) / c.animOutDuration));

    // What's left of the picture after cropping
    const double cw = std::max(2.0, frame->width * (1.0 - c.cropLeft - c.cropRight));
    const double ch = std::max(2.0, frame->height * (1.0 - c.cropTop - c.cropBottom));
    // Turned a quarter? Then it's the other way round when fitting it into the frame
    // Where it is right now (any of these can be keyframed)
    const double rotation = c.at(Param::Rotation, t, c.rotation);
    const double scaleNow = c.at(Param::Scale, t, c.scale);
    const double posX = c.at(Param::PosX, t, c.posX), posY = c.at(Param::PosY, t, c.posY);
    const double quarter = std::abs(std::remainder(rotation, 180.0));
    const bool sideways = std::abs(quarter - 90.0) < 0.01;
    const double boxW = sideways ? ch : cw, boxH = sideways ? cw : ch;

    // Fit inside the canvas keeping the shape (black bars if it doesn't match), or fill it
    // (cutting off what hangs over), then the clip's own size and position on top (picture-in-picture)
    double fit = c.fill ? std::max(w / boxW, h / boxH) : std::min(w / boxW, h / boxH);
    double size = std::clamp(scaleNow * mods.scale, 0.02, 10.0);
    int fw = std::max(1, int(std::lround(cw * fit * size)));
    int fh = std::max(1, int(std::lround(ch * fit * size)));
    int x = (w - fw) / 2 + int(std::lround(posX * w + mods.dx));
    int y = (h - fh) / 2 + int(std::lround(posY * h + mods.dy));

    // Solid and fully faded in? Paint it straight on. Otherwise mix it with what's underneath.
    double opacity = c.envelope(t) * std::clamp(c.at(Param::Opacity, t, c.opacity), 0.0f, 1.0f) * mods.opacity;
    if (opacity <= 0.0)
        return;
    // Where it may draw on the canvas: inside the frame, any transition's box, and the
    // part a wipe animation has uncovered (from left to right)
    int boxX0 = std::max(0, mods.clipX0), boxY0 = std::max(0, mods.clipY0);
    int boxX1 = std::min(w, mods.clipX1), boxY1 = std::min(h, mods.clipY1);
    boxX1 = std::min(boxX1, x + int(std::lround(fw * std::min(wipeIn, wipeOut))));
    int x0 = std::max(x, boxX0), x1 = std::min(x + fw, boxX1);
    int y0 = std::max(y, boxY0), y1 = std::min(y + fh, boxY1);
    if (!c.turned() && (x0 >= x1 || y0 >= y1))
        return;

    const FrameCrop crop { c.cropLeft, c.cropRight, c.cropTop, c.cropBottom };
    bool solid = opacity >= 1.0 && !hasAlpha(frame) && !c.key.on;
    // Green screen first (it needs the original colours), then the effects
    auto treat = [&](uint8_t* px, int pw, int ph) {
        if (c.key.on)
            applyChromaKey(px, pw, ph, c.key, bgra);
        if (c.effects.any())
            applyEffects(px, pw, ph, c.effects, bgra, m_fx);
    };

    if (!c.turned() && (fw > w + 2 || fh > h + 2)) {
        // Zoomed in past the edges: only scale the bit of the picture that actually shows,
        // instead of blowing up the whole thing and throwing most of it away.
        // Work in source pixels, trimmed to even numbers like the scaler does.
        const int W = frame->width, H = frame->height;
        auto even = [](double v) { return int(v) & ~1; };
        const int baseL = even(std::clamp(c.cropLeft, 0.0f, 0.95f) * W), baseR = even(std::clamp(c.cropRight, 0.0f, 0.95f) * W);
        const int baseT = even(std::clamp(c.cropTop, 0.0f, 0.95f) * H), baseB = even(std::clamp(c.cropBottom, 0.0f, 0.95f) * H);
        const int srcW = W - baseL - baseR, srcH = H - baseT - baseB;
        if (srcW < 4 || srcH < 4)
            return;
        // The showing part, in source pixels (rounded outwards)
        const int u0 = even(std::max(0.0, double(x0 - x) * srcW / fw));
        const int u1 = std::min(srcW, int(std::ceil(double(x1 - x) * srcW / fw)) + 2);
        const int v0 = even(std::max(0.0, double(y0 - y) * srcH / fh));
        const int v1 = std::min(srcH, int(std::ceil(double(y1 - y) * srcH / fh)) + 2);
        const int cutR = even(srcW - u1), cutB = even(srcH - v1);
        const int regionW = srcW - u0 - cutR, regionH = srcH - v0 - cutB;
        if (regionW < 2 || regionH < 2)
            return;
        // (the +0.5 makes the scaler's own rounding land on exactly these pixels)
        const FrameCrop region { float((baseL + u0 + 0.5) / W), float((baseR + cutR + 0.5) / W),
                                 float((baseT + v0 + 0.5) / H), float((baseB + cutB + 0.5) / H) };
        // Where that part lands on the canvas, and how big
        const int ox = x + int(std::lround(double(u0) * fw / srcW));
        const int oy = y + int(std::lround(double(v0) * fh / srcH));
        const int ow = std::max(1, int(std::lround(double(regionW) * fw / srcW)));
        const int oh = std::max(1, int(std::lround(double(regionH) * fh / srcH)));
        m_scratch.resize(size_t(ow) * oh * 4);
        if (!reader->scale(frame, ow, oh, m_scratch.data(), ow * 4, bgra, region))
            return;
        treat(m_scratch.data(), ow, oh);
        const int dx0 = std::max(x0, ox), dx1 = std::min(x1, ox + ow);
        for (int row = std::max(y0, oy); row < std::min(y1, oy + oh); ++row) {
            if (dx0 >= dx1)
                break;
            uint8_t* dst = canvas + (size_t(row) * w + dx0) * 4;
            const uint8_t* src = m_scratch.data() + (size_t(row - oy) * ow + (dx0 - ox)) * 4;
            if (solid)
                std::memcpy(dst, src, size_t(dx1 - dx0) * 4);
            else
                blendRow(dst, src, dx1 - dx0, opacity);
        }
        return;
    }
    bool wholeCanvas = fw == w && fh == h && x == 0 && y == 0 && x0 == 0 && y0 == 0 && x1 == w && y1 == h;
    bool effects = c.effects.any() || c.key.on;
    const uint8_t* pixels = nullptr;
    if (reader->isStill()) {
        // Pictures never change, so scale once and reuse it until the size (or crop) changes
        const float cropKey[4] = { c.cropLeft, c.cropRight, c.cropTop, c.cropBottom };
        if (s.still.empty() || s.stillW != fw || s.stillH != fh || s.stillBgra != bgra
            || !std::equal(cropKey, cropKey + 4, s.stillCrop)) {
            s.still.resize(size_t(fw) * fh * 4);
            if (!reader->scale(frame, fw, fh, s.still.data(), fw * 4, bgra, crop)) {
                s.still.clear();
                return;
            }
            s.stillW = fw;
            s.stillH = fh;
            s.stillBgra = bgra;
            std::copy(cropKey, cropKey + 4, s.stillCrop);
        }
        pixels = s.still.data();
    } else if (solid && wholeCanvas && !effects && !c.turned()) {
        reader->scale(frame, w, h, canvas, w * 4, bgra, crop);
        return;
    } else {
        m_scratch.resize(size_t(fw) * fh * 4);
        if (!reader->scale(frame, fw, fh, m_scratch.data(), fw * 4, bgra, crop))
            return;
        pixels = m_scratch.data();
    }

    if (effects) {
        // Work on a copy, so a cached still picture stays untouched
        m_layer.assign(pixels, pixels + size_t(fw) * fh * 4);
        treat(m_layer.data(), fw, fh);
        pixels = m_layer.data();
    }

    if (c.turned()) {
        drawTurned(canvas, w, h, pixels, fw, fh, x + fw / 2.0, y + fh / 2.0, rotation, c, opacity, boxX0, boxY0, boxX1, boxY1);
        return;
    }

    // Only the part that's actually showing
    for (int row = y0; row < y1; ++row) {
        uint8_t* dst = canvas + (size_t(row) * w + x0) * 4;
        const uint8_t* src = pixels + (size_t(row - y) * fw + (x0 - x)) * 4;
        if (solid)
            std::memcpy(dst, src, size_t(x1 - x0) * 4);
        else
            blendRow(dst, src, x1 - x0, opacity);
    }
}

void Timeline::drawTurned(uint8_t* canvas, int w, int /*h*/, const uint8_t* src, int fw, int fh, double cx, double cy,
                          double rotation, const Clip& c, double opacity, int boxX0, int boxY0, int boxX1, int boxY1)
{
    // For each pixel on the canvas, turn backwards to find which bit of the picture lands there,
    // and mix the four nearest picture pixels for smooth edges
    const double angle = rotation * M_PI / 180.0;
    const double cosA = std::cos(angle), sinA = std::sin(angle);
    const double mx = c.flipH ? -1.0 : 1.0, my = c.flipV ? -1.0 : 1.0;

    // Only bother with the area the turned picture covers
    const double hw = fw / 2.0, hh = fh / 2.0;
    const double reachX = std::abs(hw * cosA) + std::abs(hh * sinA);
    const double reachY = std::abs(hw * sinA) + std::abs(hh * cosA);
    const int x0 = std::max(boxX0, int(std::floor(cx - reachX)));
    const int x1 = std::min(boxX1, int(std::ceil(cx + reachX)));
    const int y0 = std::max(boxY0, int(std::floor(cy - reachY)));
    const int y1 = std::min(boxY1, int(std::ceil(cy + reachY)));
    if (x0 >= x1 || y0 >= y1)
        return;
    const int strength = int(std::clamp(opacity, 0.0, 1.0) * 256);

    parallelRows(y1 - y0, [&](int r0, int r1) {
        for (int py = y0 + r0; py < y0 + r1; ++py) {
            const double dy = py + 0.5 - cy;
            uint8_t* dst = canvas + (size_t(py) * w + x0) * 4;
            for (int px = x0; px < x1; ++px, dst += 4) {
                const double dx = px + 0.5 - cx;
                // Undo the turn (clockwise on screen), then the mirroring
                const double u = (cosA * dx + sinA * dy) * mx + hw - 0.5;
                const double v = (-sinA * dx + cosA * dy) * my + hh - 0.5;
                if (u < -0.5 || v < -0.5 || u > fw - 0.5 || v > fh - 0.5)
                    continue;
                const double uc = std::clamp(u, 0.0, fw - 1.0), vc = std::clamp(v, 0.0, fh - 1.0);
                const int ix = std::min(int(uc), fw - 2 < 0 ? 0 : fw - 2), iy = std::min(int(vc), fh - 2 < 0 ? 0 : fh - 2);
                const int ix1 = std::min(ix + 1, fw - 1), iy1 = std::min(iy + 1, fh - 1);
                const int fx = int((uc - ix) * 256), fy = int((vc - iy) * 256);
                const uint8_t* p00 = src + (size_t(iy) * fw + ix) * 4;
                const uint8_t* p10 = src + (size_t(iy) * fw + ix1) * 4;
                const uint8_t* p01 = src + (size_t(iy1) * fw + ix) * 4;
                const uint8_t* p11 = src + (size_t(iy1) * fw + ix1) * 4;
                int px4[4];
                for (int k = 0; k < 4; ++k) {
                    int top = p00[k] * (256 - fx) + p10[k] * fx;
                    int bottom = p01[k] * (256 - fx) + p11[k] * fx;
                    px4[k] = (top * (256 - fy) + bottom * fy) >> 16;
                }
                const int a = (px4[3] * strength) >> 8;
                if (a <= 0)
                    continue;
                for (int k = 0; k < 3; ++k)
                    dst[k] = uint8_t((px4[k] * a + dst[k] * (255 - a)) / 255);
            }
        }
    });
}

void Timeline::drawTransition(Slot& a, Slot& b, double t, int w, int h, uint8_t* canvas, bool bgra)
{
    // p goes 0 -> 1 across the transition, which is centred on the cut
    double p = std::clamp((t - b.clip.blendInFrom) / (b.clip.blendInTo - b.clip.blendInFrom), 0.0, 1.0);
    double e = smooth(p);
    DrawMods first, second;

    switch (b.clip.transition) {
    case Transition::Dissolve:
        second.opacity = p;
        break;
    case Transition::FadeBlack: // down to black, then back up
        if (p < 0.5) {
            first.opacity = 1 - 2 * p;
            drawSlot(a, t, w, h, canvas, bgra, first);
        } else {
            second.opacity = 2 * p - 1;
            drawSlot(b, t, w, h, canvas, bgra, second);
        }
        return;
    case Transition::WipeLeft: // the new clip sweeps in from the right
        second.clipX0 = int(std::lround(w * (1 - e)));
        break;
    case Transition::WipeRight:
        second.clipX1 = int(std::lround(w * e));
        break;
    case Transition::WipeUp:
        second.clipY0 = int(std::lround(h * (1 - e)));
        break;
    case Transition::WipeDown:
        second.clipY1 = int(std::lround(h * e));
        break;
    case Transition::SlideLeft: // the new clip pushes the old one out to the left
        first.dx = -e * w;
        second.dx = (1 - e) * w;
        break;
    case Transition::SlideRight:
        first.dx = e * w;
        second.dx = -(1 - e) * w;
        break;
    case Transition::Zoom:
        second.scale = 0.6 + 0.4 * e;
        second.opacity = p;
        break;
    default:
        break;
    }
    drawSlot(a, t, w, h, canvas, bgra, first);
    drawSlot(b, t, w, h, canvas, bgra, second);
}

void Timeline::applyAdjustment(const Clip& c, double t, int w, int h, uint8_t* canvas, bool bgra)
{
    // Effects on everything drawn so far. Fades and opacity ease the effect in and out.
    double strength = c.envelope(t) * c.opacity;
    if (strength <= 0.0 || !c.effects.any())
        return;
    m_layer.assign(canvas, canvas + size_t(w) * h * 4);
    applyEffects(m_layer.data(), w, h, c.effects, bgra, m_fx);
    if (strength >= 1.0) {
        std::memcpy(canvas, m_layer.data(), m_layer.size());
        return;
    }
    for (int row = 0; row < h; ++row)
        blendRow(canvas + size_t(row) * w * 4, m_layer.data() + size_t(row) * w * 4, w, strength);
}

void Timeline::applyTransitionBlock(const Clip& c, double t, int w, int h, uint8_t* canvas)
{
    // A transition with no cut: the picture so far is one side, black is the other.
    // In = black -> picture, Out = picture -> black, Through = out to black and straight back in.
    double p = std::clamp((t - c.start) / c.duration, 0.0, 1.0);
    bool arriving = true;
    switch (c.part) {
    case Clip::Part::In: break;
    case Clip::Part::Out: arriving = false; break;
    case Clip::Part::Through:
        arriving = p >= 0.5;
        p = arriving ? 2 * p - 1 : 2 * p;
        break;
    }
    double e = smooth(p);

    // Where the picture ends up: shifted, zoomed, cropped and/or faded over black
    double opacity = 1.0, dx = 0.0, dy = 0.0, scale = 1.0;
    int x0 = 0, y0 = 0, x1 = w, y1 = h;
    switch (c.transition) {
    case Transition::Dissolve:
    case Transition::FadeBlack:
        opacity = arriving ? p : 1 - p;
        break;
    case Transition::WipeLeft: // sweeps in from the right (or black sweeps over it from the right)
        if (arriving) x0 = int(std::lround(w * (1 - e)));
        else x1 = int(std::lround(w * (1 - e)));
        break;
    case Transition::WipeRight:
        if (arriving) x1 = int(std::lround(w * e));
        else x0 = int(std::lround(w * e));
        break;
    case Transition::WipeUp:
        if (arriving) y0 = int(std::lround(h * (1 - e)));
        else y1 = int(std::lround(h * (1 - e)));
        break;
    case Transition::WipeDown:
        if (arriving) y1 = int(std::lround(h * e));
        else y0 = int(std::lround(h * e));
        break;
    case Transition::SlideLeft: // leaves to the left, arrives from the right
        dx = arriving ? (1 - e) * w : -e * w;
        break;
    case Transition::SlideRight:
        dx = arriving ? -(1 - e) * w : e * w;
        break;
    case Transition::Zoom: // flies in from small, leaves by zooming right past you
        scale = arriving ? 0.6 + 0.4 * e : 1.0 + 0.5 * e;
        opacity = arriving ? p : 1 - p;
        break;
    default:
        return;
    }
    if (opacity >= 1.0 && dx == 0.0 && dy == 0.0 && scale == 1.0 && x0 == 0 && y0 == 0 && x1 == w && y1 == h)
        return; // nothing to do this frame

    m_layer.assign(canvas, canvas + size_t(w) * h * 4);
    const uint32_t black = 0xFF000000u;
    std::fill_n(reinterpret_cast<uint32_t*>(canvas), size_t(w) * h, black);
    if (opacity <= 0.0)
        return;

    const uint32_t* src = reinterpret_cast<const uint32_t*>(m_layer.data());
    std::vector<uint32_t>& row = m_rowScratch;
    row.resize(size_t(w));
    for (int y = y0; y < y1; ++y) {
        // Which row of the picture lands here (zooming around the middle)
        int sy = int(std::floor((y - dy - h / 2.0) / scale + h / 2.0));
        if (sy < 0 || sy >= h)
            continue;
        int from = w, to = 0;
        for (int x = x0; x < x1; ++x) {
            int sx = int(std::floor((x - dx - w / 2.0) / scale + w / 2.0));
            if (sx < 0 || sx >= w)
                continue;
            row[size_t(x)] = src[size_t(sy) * w + sx];
            from = std::min(from, x);
            to = x + 1;
        }
        if (from >= to)
            continue;
        uint8_t* dst = canvas + (size_t(y) * w + from) * 4;
        if (opacity >= 1.0)
            std::memcpy(dst, row.data() + from, size_t(to - from) * 4);
        else
            blendRow(dst, reinterpret_cast<const uint8_t*>(row.data() + from), to - from, opacity);
    }
}

void Timeline::renderVideo(double t, int w, int h, uint8_t* rgba, bool bgra)
{
    closeIdleVideo(t);

    // Opaque black, written 4 bytes at a time
    const uint32_t black = 0xFF000000u; // R G B A = 0 0 0 255 in memory
    std::fill_n(reinterpret_cast<uint32_t*>(rgba), size_t(w) * h, black);

    // What's on screen right now: single clips, and pairs that are mid-transition
    struct Item {
        Slot* slot;
        Slot* from; // set when this is a transition from `from` into `slot`
    };
    std::vector<Item> items;
    for (Slot& s : m_slots) {
        const Clip& c = s.clip;
        if (!c.useVideo)
            continue;
        if (s.prev >= 0 && t >= c.blendInFrom && t < c.blendInTo)
            items.push_back({ &s, &m_slots[size_t(s.prev)] });
        else if (c.activeAt(t) && !(s.next >= 0 && t >= c.blendOutFrom))
            items.push_back({ &s, nullptr }); // (the end of a clip leading into a transition gets drawn by that transition)
    }
    // Bottom layer first, so the top one ends up painted over everything else
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.slot->clip.layer < b.slot->clip.layer; });

    for (const Item& item : items) {
        if (item.slot->clip.kind == Clip::Kind::Adjustment)
            applyAdjustment(item.slot->clip, t, w, h, rgba, bgra);
        else if (item.slot->clip.kind == Clip::Kind::Transition)
            applyTransitionBlock(item.slot->clip, t, w, h, rgba);
        else if (item.from)
            drawTransition(*item.from, *item.slot, t, w, h, rgba, bgra);
        else
            drawSlot(*item.slot, t, w, h, rgba, bgra, {});
    }
}

const NoiseProfile& Timeline::noiseFor(const Clip& c)
{
    std::string key = c.path + "|" + std::to_string(std::llround(c.in * 10)) + "|"
                      + std::to_string(std::llround(c.duration * c.speed * 10));
    auto it = m_noise.find(key);
    if (it == m_noise.end())
        it = m_noise.emplace(key, learnNoise(c.path, c.in, c.duration * c.speed)).first;
    return it->second;
}

void Timeline::followTalking(double t, const float* mix, int frames)
{
    // How much talking (any sound at all from the clips that don't duck) there is, sample by sample.
    // Quick to notice it starting, slow to let go, so music doesn't bob up between words.
    if (std::abs(t - m_talkNext) > 1e-3)
        m_talkLevel = m_talkPresence = 0.0f; // jumped: start afresh
    const float attack = 1.0f - std::exp(-1.0f / (0.010f * AudioRate));
    const float release = 1.0f - std::exp(-1.0f / (0.250f * AudioRate));
    const float rise = 1.0f - std::exp(-1.0f / (0.080f * AudioRate));
    const float fall = 1.0f - std::exp(-1.0f / (0.600f * AudioRate));
    m_talking.resize(size_t(frames));
    for (int i = 0; i < frames; ++i) {
        float level = 0.5f * (std::abs(mix[i * 2]) + std::abs(mix[i * 2 + 1]));
        m_talkLevel += (level - m_talkLevel) * (level > m_talkLevel ? attack : release);
        float db = 20.0f * std::log10(std::max(m_talkLevel, 1e-6f));
        float target = std::clamp((db + 45.0f) / 15.0f, 0.0f, 1.0f); // -45dB = nothing, -30dB = definitely talking
        m_talkPresence += (target - m_talkPresence) * (target > m_talkPresence ? rise : fall);
        m_talking[size_t(i)] = m_talkPresence;
    }
    m_talkNext = t + double(frames) / AudioRate;
}

void Timeline::renderAudio(double t, int frames, float* out)
{
    closeIdleAudio(t);
    std::memset(out, 0, sizeof(float) * frames * AudioChannels);
    double windowEnd = t + double(frames) / AudioRate;

    // Clips set to duck (music) go in second, once we know how much talking there is
    bool anyDucking = false;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            if (!anyDucking)
                break;
            followTalking(t, out, frames);
        }
        for (Slot& s : m_slots) {
            const Clip& c = s.clip;
            if (c.duck && c.useAudio)
                anyDucking = true;
            if (c.duck != (pass == 1))
                continue;
            // (a transition borrows a little sound either side of the clip, for a crossfade)
            const double from0 = c.start - c.preRoll, to0 = c.end() + c.postRoll;
            if (!c.useAudio || to0 <= t || from0 >= windowEnd)
                continue;
            AudioReader* reader = audioFor(s, t);
            if (!reader)
                continue;

            // Which part of this chunk does the clip cover?
            // (worked out as doubles first so a really long clip can't overflow an int)
            int from = int(std::clamp(std::ceil((from0 - t) * AudioRate), 0.0, double(frames)));
            int to = int(std::clamp(std::floor((to0 - t) * AudioRate), 0.0, double(frames)));
            if (to <= from)
                continue;

            int n = to - from;
            m_mix.resize(size_t(n) * AudioChannels);
            const double when = t + double(from) / AudioRate;
            // (backwards: start at the matching spot near the end, exactly, no frame-edge nudge for sound)
            double sourceStart = c.reverse ? c.in + (c.end() - when) * c.speed : c.sourceTime(when);
            // The file's sound, cleaned of background noise if asked (forwards only)
            const bool clean = c.denoise > 0.0f && !c.reverse;
            if (clean && !s.denoiser)
                s.denoiser = std::make_unique<Denoiser>();
            auto source = [&](int64_t fromSample, int count, float* to) {
                if (clean) {
                    s.denoiser->read(*reader, noiseFor(c), c.denoise, fromSample, count, to);
                    return;
                }
                std::fill(to, to + size_t(count) * AudioChannels, 0.0f);
                int skip = fromSample < 0 ? int(std::min<int64_t>(-fromSample, count)) : 0;
                if (skip < count)
                    reader->read(double(fromSample + skip) / AudioRate, count - skip, to + size_t(skip) * AudioChannels);
            };
            // (backwards: sourceStart is where it starts from, walking back through the file)
            auto readSound = [&](int count, float* to) {
                if (c.reverse)
                    reader->readBackwards(sourceStart, count, to);
                else
                    source(std::llround(sourceStart * AudioRate), count, to);
            };
            if (c.speed != 1.0 && c.keepPitch && !c.reverse) {
                // Faster or slower, same pitch
                if (!s.stretcher)
                    s.stretcher = std::make_unique<TimeStretcher>();
                s.stretcher->render(source, c.in, c.speed, std::llround((when - c.start) * AudioRate), n, m_mix.data());
            } else if (c.speed == 1.0) {
                readSound(n, m_mix.data());
            } else {
                // Sped up or slowed down: read that much more (or less) sound and squash/stretch it.
                // (Like playing a record faster, so the pitch changes too.)
                int need = int(std::ceil(n * c.speed)) + 2;
                m_speedBuf.resize(size_t(need) * AudioChannels);
                readSound(need, m_speedBuf.data());
                for (int i = 0; i < n; ++i) {
                    double pos = i * c.speed;
                    int k = std::min(int(pos), need - 2);
                    float frac = float(pos - k);
                    for (int ch = 0; ch < AudioChannels; ++ch)
                        m_mix[size_t(i) * 2 + ch] = m_speedBuf[size_t(k) * 2 + ch] * (1 - frac)
                                                    + m_speedBuf[size_t(k + 1) * 2 + ch] * frac;
                }
            }
            // Volume, with the fades worked out sample by sample so they're smooth
            float* dst = out + size_t(from) * AudioChannels;
            bool blendsIn = c.blendInTo > c.blendInFrom, blendsOut = c.blendOutTo > c.blendOutFrom;
            bool fading = c.fadeIn > 0 || c.fadeOut > 0 || blendsIn || blendsOut;
            const bool volumeKeys = !c.keys[size_t(Param::Volume)].empty();
            float volume = c.volume;
            for (int i = 0; i < n; ++i) {
                if (volumeKeys && i % 64 == 0) // keyframed volume: a fresh value every ~1ms is plenty smooth
                    volume = std::max(0.0f, c.at(Param::Volume, t + double(from + i) / AudioRate, c.volume));
                float gain = volume;
                if (fading) {
                    double when = t + double(from + i) / AudioRate;
                    gain *= float(c.envelope(std::clamp(when, c.start, c.end())));
                    if (blendsIn && when < c.blendInTo) // crossfading in from the clip before
                        gain *= float(std::clamp((when - c.blendInFrom) / (c.blendInTo - c.blendInFrom), 0.0, 1.0));
                    if (blendsOut && when > c.blendOutFrom) // crossfading out into the clip after
                        gain *= float(std::clamp((c.blendOutTo - when) / (c.blendOutTo - c.blendOutFrom), 0.0, 1.0));
                }
                if (pass == 1) // music: down while there's talking
                    gain *= 1.0f - c.duckAmount * m_talking[size_t(from + i)];
                dst[i * 2] += m_mix[i * 2] * gain;
                dst[i * 2 + 1] += m_mix[i * 2 + 1] * gain;
            }
        }
    }

    for (int i = 0; i < frames * AudioChannels; ++i)
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

} // namespace ve

namespace ve {

bool Timeline::copyPlan(std::string& source, std::vector<CopySegment>& segments, std::string& why) const
{
    source.clear();
    segments.clear();
    if (m_slots.empty()) {
        why = "There's nothing on the timeline.";
        return false;
    }

    // Line clips up into pieces. A video and its detached sound count as one piece.
    struct Piece {
        double start, in, duration;
        bool video = false, audio = false;
    };
    std::vector<Piece> pieces;
    constexpr double eps = 0.002;
    for (const Slot& s : m_slots) {
        const Clip& c = s.clip;
        if (c.kind != Clip::Kind::Media) {
            why = "Effects and transitions need a normal export.";
            return false;
        }
        if (source.empty())
            source = c.path;
        else if (c.path != source) {
            why = "Titles, pictures and clips from different files need a normal export.";
            return false;
        }
        if (c.denoise > 0 || c.duck || (c.keepPitch && c.speed != 1.0)) {
            why = "Sound clean-up and ducking need a normal export.";
            return false;
        }
        if (c.fadeIn > 0 || c.fadeOut > 0 || std::abs(c.volume - 1.0f) > 0.001f) {
            why = "Fades and volume changes need a normal export.";
            return false;
        }
        if (c.speed != 1.0 || !c.plain()) {
            why = "Effects, speed changes and picture-in-picture need a normal export.";
            return false;
        }
        auto same = [&](const Piece& p) {
            return std::abs(p.start - c.start) < eps && std::abs(p.in - c.in) < eps && std::abs(p.duration - c.duration) < eps;
        };
        auto it = std::find_if(pieces.begin(), pieces.end(), same);
        if (it == pieces.end())
            it = pieces.insert(pieces.end(), Piece { c.start, c.in, c.duration });
        it->video |= c.useVideo;
        it->audio |= c.useAudio;
    }

    // Is it actually a video (not a picture), and does it have sound?
    FormatPtr fmt = openInput(source.c_str());
    if (!fmt || av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0) < 0
        || std::string(fmt->iformat->name).find("image") != std::string::npos
        || std::string(fmt->iformat->name).find("_pipe") != std::string::npos) {
        why = "Only video files can be exported instantly.";
        return false;
    }
    bool sourceHasAudio = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;

    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.start < b.start; });
    double expected = 0.0;
    for (const Piece& p : pieces) {
        if (p.start > expected + eps) {
            why = "Gaps between clips need a normal export.";
            return false;
        }
        if (p.start < expected - eps) {
            why = "Overlapping clips need a normal export.";
            return false;
        }
        if (!p.video || (sourceHasAudio && !p.audio)) {
            why = "Clips with their picture or sound removed need a normal export.";
            return false;
        }
        segments.push_back({ p.in, p.duration });
        expected = p.start + p.duration;
    }
    return true;
}

} // namespace ve
