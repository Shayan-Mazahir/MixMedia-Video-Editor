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

bool VideoReader::open(const std::string& path, AVBufferRef* hwDevice)
{
    m_path = path;
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
    int idx = openDecoder(fmt.get(), AVMEDIA_TYPE_VIDEO, ctx, m_hwDevice);
    if (idx < 0)
        return false;

    AVStream* st = fmt->streams[idx];
    m_timeBase = st->time_base;
    m_startPts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    AVRational rate = av_guess_frame_rate(fmt.get(), st, nullptr);
    if (rate.num > 0 && rate.den > 0)
        m_frameDuration = av_q2d(av_inv_q(rate));

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
        open(m_path, m_hwDevice);
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

const AVFrame* VideoReader::frameAt(double sec, bool fast)
{
    if (!isOpen())
        return nullptr;

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
SwsContext* converterFor(SwsPtr& sws, int (&cachedKey)[5], const AVFrame* f, int w, int h, AVPixelFormat to)
{
    int key[5] = { f->width, f->height, f->format, w, h };
    if (sws && std::equal(key, key + 5, cachedKey))
        return sws.get();

    sws.reset(sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format),
                             w, h, to, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!sws)
        return nullptr;
    std::copy(key, key + 5, cachedKey);

    int srcFull = f->color_range == AVCOL_RANGE_JPEG;
    if (to == AV_PIX_FMT_RGBA)
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

bool VideoReader::scale(const AVFrame* f, int w, int h, uint8_t* dst, int dstStride)
{
    f = toMemory(f);
    if (!f)
        return false;
    SwsContext* sws = converterFor(m_sws, m_swsKey, f, w, h, AV_PIX_FMT_RGBA);
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
