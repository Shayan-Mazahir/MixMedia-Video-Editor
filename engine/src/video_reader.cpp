// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "video_reader.h"

#include <algorithm>

namespace ve {

namespace {
// If we need a frame less than this far ahead, decoding forward beats seeking.
constexpr double ForwardDecodeLimit = 2.0;
}

VideoReader::~VideoReader()
{
    m_ctx.reset(); // the decoder lets go of the card before we do
    av_buffer_unref(&m_hwDevice);
}

bool VideoReader::open(const std::string& path, AVBufferRef* hwDevice, int threads)
{
    m_path = path;
    m_threads = threads;
    if (hwDevice != m_hwDevice) {
        av_buffer_unref(&m_hwDevice);
        m_hwDevice = hwDevice ? av_buffer_ref(hwDevice) : nullptr;
    }
    m_fmt.reset();
    m_ctx.reset();
    m_haveFrame = m_eof = m_flushed = false;

    FormatPtr fmt = openInput(path.c_str());
    if (!fmt)
        return false;
    CodecPtr ctx;
    int idx = openDecoder(fmt.get(), AVMEDIA_TYPE_VIDEO, ctx, m_hwDevice, m_threads);
    if (idx < 0)
        return false;

    AVStream* st = fmt->streams[idx];
    m_timeBase = st->time_base;
    m_startPts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    AVRational rate = av_guess_frame_rate(fmt.get(), st, nullptr);
    if (rate.num > 0 && rate.den > 0)
        m_frameDuration = av_q2d(av_inv_q(rate));

    m_still = isStillImage(fmt.get());
    m_fmt = std::move(fmt);
    m_ctx = std::move(ctx);
    m_stream = idx;
    if (!m_pkt) {
        m_pkt.reset(av_packet_alloc());
        m_frame.reset(av_frame_alloc());
        m_next.reset(av_frame_alloc());
        m_inMemory.reset(av_frame_alloc());
    }
    return true;
}

double VideoReader::frameStart(const AVFrame* f) const
{
    int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    if (pts == AV_NOPTS_VALUE)
        return 0.0;
    return (pts - m_startPts) * av_q2d(m_timeBase);
}

double VideoReader::frameEnd(const AVFrame* f) const
{
    double d = f->duration > 0 ? f->duration * av_q2d(m_timeBase) : m_frameDuration;
    return frameStart(f) + d;
}

void VideoReader::seekTo(double sec)
{
    int64_t ts = m_startPts + static_cast<int64_t>(sec / av_q2d(m_timeBase));
    if (av_seek_frame(m_fmt.get(), m_stream, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        // Some files (like still images) can't seek, so just start over
        open(m_path, m_hwDevice, m_threads);
        return;
    }
    avcodec_flush_buffers(m_ctx.get());
    m_haveFrame = m_eof = m_flushed = false;
}

bool VideoReader::decodeNext()
{
    while (true) {
        int r = avcodec_receive_frame(m_ctx.get(), m_next.get());
        if (r == 0) {
            av_frame_unref(m_frame.get());
            av_frame_move_ref(m_frame.get(), m_next.get());
            m_haveFrame = true;
            return true;
        }
        if (r == AVERROR_EOF) {
            m_eof = true;
            return false;
        }
        if (r != AVERROR(EAGAIN))
            return false;

        // The decoder is hungry, feed it another packet
        if (m_flushed) {
            m_eof = true;
            return false;
        }
        if (av_read_frame(m_fmt.get(), m_pkt.get()) < 0) {
            avcodec_send_packet(m_ctx.get(), nullptr); // end of file, squeeze out what's left
            m_flushed = true;
            continue;
        }
        if (m_pkt->stream_index == m_stream)
            avcodec_send_packet(m_ctx.get(), m_pkt.get());
        av_packet_unref(m_pkt.get());
    }
}

void VideoReader::setBackwards(bool backwards)
{
    if (backwards == m_backwards)
        return;
    m_backwards = backwards;
    m_window.clear();
}

const AVFrame* VideoReader::backwardsFrameAt(double sec)
{
    // Already got it? (the newest frame that starts at or before `sec`)
    auto lookup = [&]() -> const AVFrame* {
        if (m_window.empty() || sec < frameStart(m_window.front().get()) - 1e-6
            || sec >= frameEnd(m_window.back().get()) + 1e-6)
            return nullptr;
        for (auto it = m_window.rbegin(); it != m_window.rend(); ++it)
            if (frameStart(it->get()) <= sec + 1e-6)
                return it->get();
        return m_window.front().get();
    };
    if (const AVFrame* f = lookup())
        return f;

    // Grab the second leading up to it, and keep every frame
    constexpr double Window = 1.0;
    m_window.clear();
    const double from = std::max(0.0, sec - Window);
    seekTo(from);
    while (decodeNext()) {
        const AVFrame* f = m_frame.get();
        if (frameEnd(f) <= from + 1e-6)
            continue; // still before the window
        // Card frames come back to normal memory (the card only has a few slots to hold frames in)
        const AVFrame* mem = toMemory(f);
        if (!mem)
            break;
        FramePtr copy(av_frame_alloc());
        if (av_frame_ref(copy.get(), mem) < 0)
            break;
        m_window.push_back(std::move(copy));
        if (frameEnd(f) > sec + 1e-6)
            break;
    }
    m_haveFrame = false; // (the decoder's moved on, so don't trust m_frame for forwards reading)
    return lookup() ? lookup() : (m_window.empty() ? nullptr : m_window.back().get());
}

const AVFrame* VideoReader::frameAt(double sec, bool fast)
{
    if (!isOpen())
        return nullptr;
    if (m_backwards && !fast && !m_still)
        return backwardsFrameAt(sec);

    if (m_haveFrame) {
        double start = frameStart(m_frame.get());
        double end = frameEnd(m_frame.get());
        if (sec >= start && (sec < end || m_eof))
            return m_frame.get(); // already showing the right one

        bool justAhead = sec >= start && sec - start < ForwardDecodeLimit;
        if (!justAhead)
            seekTo(sec);
    } else {
        seekTo(sec);
    }

    if (fast && !m_haveFrame) {
        decodeNext();
        return m_haveFrame ? m_frame.get() : nullptr;
    }

    while (!m_haveFrame || frameEnd(m_frame.get()) <= sec) {
        if (!decodeNext())
            break; // ran out of video, the last frame will have to do
    }
    return m_haveFrame ? m_frame.get() : nullptr;
}

namespace {

// HD video uses slightly different colour maths than old SD video
const int* sourceCoefficients(const AVFrame* f)
{
    int space = f->colorspace != AVCOL_SPC_UNSPECIFIED ? f->colorspace
                                                       : (f->height >= 720 ? AVCOL_SPC_BT709 : AVCOL_SPC_BT470BG);
    return sws_getCoefficients(space);
}

// Makes (or reuses) a converter from this frame to w x h in the given format
SwsContext* converterFor(SwsPtr& sws, int (&cachedKey)[6], const AVFrame* f, int w, int h, AVPixelFormat to,
                         bool fast = false)
{
    int key[6] = { f->width, f->height, f->format, w, h, to };
    if (sws && std::equal(key, key + 6, cachedKey))
        return sws.get();

    sws.reset(sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format),
                             w, h, to, fast ? SWS_FAST_BILINEAR : SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!sws)
        return nullptr;
    std::copy(key, key + 6, cachedKey);

    int srcFull = f->color_range == AVCOL_RANGE_JPEG;
    if (to == AV_PIX_FMT_RGBA || to == AV_PIX_FMT_BGRA)
        sws_setColorspaceDetails(sws.get(), sourceCoefficients(f), srcFull, sourceCoefficients(f), 1, 0, 1 << 16, 1 << 16);
    else // video out: HD colours, normal (limited) range
        sws_setColorspaceDetails(sws.get(), sourceCoefficients(f), srcFull, sws_getCoefficients(AVCOL_SPC_BT709), 0, 0, 1 << 16, 1 << 16);
    return sws.get();
}

} // namespace

const AVFrame* VideoReader::toMemory(const AVFrame* f)
{
    if (!f || !f->hw_frames_ctx)
        return f;
    av_frame_unref(m_inMemory.get());
    if (av_hwframe_transfer_data(m_inMemory.get(), f, 0) < 0)
        return nullptr;
    av_frame_copy_props(m_inMemory.get(), f);
    return m_inMemory.get();
}

bool VideoReader::scale(const AVFrame* f, int w, int h, uint8_t* dst, int dstStride, bool bgra, const FrameCrop& crop)
{
    f = toMemory(f);
    if (!f)
        return false;
    if (crop.any()) {
        // A second look at the same pixels with the edges trimmed off (nothing gets copied)
        if (!m_cropped)
            m_cropped.reset(av_frame_alloc());
        av_frame_unref(m_cropped.get());
        if (av_frame_ref(m_cropped.get(), f) < 0)
            return false;
        auto px = [](float part, int size) { return size_t(std::clamp(part, 0.0f, 0.95f) * size) & ~size_t(1); };
        m_cropped->crop_left = px(crop.left, f->width);
        m_cropped->crop_right = px(crop.right, f->width);
        m_cropped->crop_top = px(crop.top, f->height);
        m_cropped->crop_bottom = px(crop.bottom, f->height);
        if (int(m_cropped->crop_left + m_cropped->crop_right) > f->width - 2
            || int(m_cropped->crop_top + m_cropped->crop_bottom) > f->height - 2
            || av_frame_apply_cropping(m_cropped.get(), AV_FRAME_CROP_UNALIGNED) < 0)
            return false;
        f = m_cropped.get();
    }
    SwsContext* sws = converterFor(m_sws, m_swsKey, f, w, h, bgra ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA, m_fastScaling);
    if (!sws)
        return false;
    uint8_t* planes[4] = { dst, nullptr, nullptr, nullptr };
    int strides[4] = { dstStride, 0, 0, 0 };
    sws_scale(sws, f->data, f->linesize, 0, f->height, planes, strides);
    return true;
}

bool VideoReader::toYuv420(const AVFrame* f, int w, int h, AVFrame* out)
{
    f = toMemory(f);
    if (!f)
        return false;
    SwsContext* sws = converterFor(m_yuvSws, m_yuvSwsKey, f, w, h, AV_PIX_FMT_YUV420P);
    if (!sws)
        return false;
    out->format = AV_PIX_FMT_YUV420P;
    out->width = w;
    out->height = h;
    if (av_frame_get_buffer(out, 0) < 0)
        return false;
    sws_scale(sws, f->data, f->linesize, 0, f->height, out->data, out->linesize);
    return true;
}

} // namespace ve
