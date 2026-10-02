#include "timeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ve {

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
    if (!only)
        return false;

    VideoReader* reader = videoFor(*only);
    const AVFrame* frame = reader ? reader->frameAt(only->clip.in + (t - only->clip.start)) : nullptr;
    if (!frame || frame->width <= 0 || frame->height <= 0)
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

        if (fw == w && fh == h) {
            reader->scale(frame, w, h, rgba, w * 4);
            continue;
        }
        m_scratch.resize(size_t(fw) * fh * 4);
        if (!reader->scale(frame, fw, fh, m_scratch.data(), fw * 4))
            continue;
        for (int row = 0; row < fh; ++row)
            std::memcpy(rgba + (size_t(y + row) * w + x) * 4, m_scratch.data() + size_t(row) * fw * 4, size_t(fw) * 4);
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
        float* dst = out + size_t(from) * AudioChannels;
        for (size_t i = 0; i < m_mix.size(); ++i)
            dst[i] += m_mix[i] * c.volume;
    }

    for (int i = 0; i < frames * AudioChannels; ++i)
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

} // namespace ve
