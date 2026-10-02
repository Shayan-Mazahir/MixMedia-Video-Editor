#include "timeline.h"
#include "stream_copy.h"

#include <algorithm>
#include <cmath>
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
        for (Slot& o : old) {
            bool hasSomething = o.video || o.audio || o.videoFailed || o.audioFailed;
            if (o.clip.path != clip.path || !hasSomething)
                continue;
            slot.video = std::move(o.video);
            slot.audio = std::move(o.audio);
            slot.videoFailed = o.videoFailed;
            slot.audioFailed = o.audioFailed;
            o.videoFailed = o.audioFailed = false;
            break;
        }
        m_slots.push_back(std::move(slot));
    }
}

double Timeline::duration() const
{
    double end = 0.0;
    for (const Slot& s : m_slots)
        end = std::max(end, s.clip.end());
    return end;
}

VideoReader* Timeline::videoFor(Slot& s)
{
    if (!s.video && !s.videoFailed) {
        s.video = std::make_unique<VideoReader>();
        if (!s.video->open(s.clip.path, m_hwDevice)) {
            s.video.reset();
            s.videoFailed = true;
        }
    }
    return s.video.get();
}

AudioReader* Timeline::audioFor(Slot& s)
{
    if (!s.audio && !s.audioFailed) {
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
    Slot* only = nullptr;
    for (Slot& s : m_slots) {
        if (!s.clip.useVideo || !s.clip.activeAt(t))
            continue;
        if (only)
            return false; // stacked clips need the full treatment
        only = &s;
    }
    if (!only || only->clip.envelope(t) < 1.0)
        return false; // fading needs blending, that's the full treatment too

    VideoReader* reader = videoFor(*only);
    const AVFrame* frame = reader ? reader->frameAt(only->clip.in + (t - only->clip.start)) : nullptr;
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

void Timeline::renderVideo(double t, int w, int h, uint8_t* rgba)
{
    // Opaque black, written 4 bytes at a time
    const uint32_t black = 0xFF000000u; // R G B A = 0 0 0 255 in memory
    std::fill_n(reinterpret_cast<uint32_t*>(rgba), size_t(w) * h, black);

    // Bottom layer first, so the top one ends up painted over everything else
    std::vector<Slot*> active;
    for (Slot& s : m_slots)
        if (s.clip.useVideo && s.clip.activeAt(t))
            active.push_back(&s);
    std::stable_sort(active.begin(), active.end(),
                     [](const Slot* a, const Slot* b) { return a->clip.layer < b->clip.layer; });

    for (Slot* s : active) {
        VideoReader* reader = videoFor(*s);
        if (!reader)
            continue;
        const AVFrame* frame = reader->frameAt(s->clip.in + (t - s->clip.start));
        if (!frame || frame->width <= 0 || frame->height <= 0)
            continue;

        // Fit inside the canvas, keeping the shape (black bars if it doesn't match)
        double fit = std::min(double(w) / frame->width, double(h) / frame->height);
        int fw = std::clamp(int(std::lround(frame->width * fit)), 1, w);
        int fh = std::clamp(int(std::lround(frame->height * fit)), 1, h);
        int x = (w - fw) / 2;
        int y = (h - fh) / 2;

        // Solid and fully faded in? Paint it straight on. Otherwise mix it with what's underneath.
        double opacity = s->clip.envelope(t);
        bool solid = opacity >= 1.0 && !hasAlpha(frame);
        if (solid && fw == w && fh == h) {
            reader->scale(frame, w, h, rgba, w * 4);
            continue;
        }
        m_scratch.resize(size_t(fw) * fh * 4);
        if (!reader->scale(frame, fw, fh, m_scratch.data(), fw * 4))
            continue;
        for (int row = 0; row < fh; ++row) {
            uint8_t* dst = rgba + (size_t(y + row) * w + x) * 4;
            const uint8_t* src = m_scratch.data() + size_t(row) * fw * 4;
            if (solid)
                std::memcpy(dst, src, size_t(fw) * 4);
            else
                blendRow(dst, src, fw, opacity);
        }
    }
}

void Timeline::renderAudio(double t, int frames, float* out)
{
    std::memset(out, 0, sizeof(float) * frames * AudioChannels);
    double windowEnd = t + double(frames) / AudioRate;

    for (Slot& s : m_slots) {
        const Clip& c = s.clip;
        if (!c.useAudio || c.end() <= t || c.start >= windowEnd)
            continue;
        AudioReader* reader = audioFor(s);
        if (!reader)
            continue;

        // Which part of this chunk does the clip cover?
        // (worked out as doubles first so a really long clip can't overflow an int)
        int from = int(std::clamp(std::ceil((c.start - t) * AudioRate), 0.0, double(frames)));
        int to = int(std::clamp(std::floor((c.end() - t) * AudioRate), 0.0, double(frames)));
        if (to <= from)
            continue;

        int n = to - from;
        m_mix.resize(size_t(n) * AudioChannels);
        reader->read(c.in + (t + double(from) / AudioRate - c.start), n, m_mix.data());
        // Volume, with the fades worked out sample by sample so they're smooth
        float* dst = out + size_t(from) * AudioChannels;
        bool fading = c.fadeIn > 0 || c.fadeOut > 0;
        for (int i = 0; i < n; ++i) {
            float gain = c.volume;
            if (fading)
                gain *= float(c.envelope(t + double(from + i) / AudioRate));
            dst[i * 2] += m_mix[i * 2] * gain;
            dst[i * 2 + 1] += m_mix[i * 2 + 1] * gain;
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
        if (source.empty())
            source = c.path;
        else if (c.path != source) {
            why = "Titles, pictures and clips from different files need a normal export.";
            return false;
        }
        if (c.fadeIn > 0 || c.fadeOut > 0 || std::abs(c.volume - 1.0f) > 0.001f) {
            why = "Fades and volume changes need a normal export.";
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
