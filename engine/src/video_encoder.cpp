// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "video_encoder.h"
#include "effects.h" // (threadLimit)

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace ve {

namespace {

using Options = std::vector<std::pair<const char*, std::string>>;

// How to ask each encoder for "this quality, please". Some drivers don't support every
// mode, so there can be a few attempts per encoder, best first.
std::vector<Options> qualityOptions(const std::string& name, int quality)
{
    std::string q = std::to_string(quality);
    std::string hwq = std::to_string(quality + 2); // hardware encoders look a bit worse at the same number

    if (name == "libx264")
        return { { { "preset", "veryfast" }, { "crf", q } } };
    if (name == "h264_vaapi")
        // low_power = Intel's speedy fixed-function encoder (only does constant quality there)
        return { { { "low_power", "1" }, { "async_depth", "8" }, { "rc_mode", "CQP" }, { "qp", hwq } },
                 { { "async_depth", "8" }, { "rc_mode", "ICQ" }, { "global_quality", hwq } },
                 { { "rc_mode", "CQP" }, { "qp", hwq } } };
    if (name == "h264_nvenc")
        return { { { "preset", "p4" }, { "rc", "vbr" }, { "cq", hwq }, { "b", "0" } } };
    if (name == "h264_qsv")
        return { { { "preset", "faster" }, { "global_quality", hwq } } };
    if (name == "h264_amf")
        return { { { "rc", "cqp" }, { "qp_i", hwq }, { "qp_p", hwq }, { "quality", "speed" } } };
    return { {} };
}

bool takesYuv420p(const AVCodec* codec)
{
    const void* list = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &list, &count) < 0 || !list)
        return true; // doesn't say, assume the common one
    auto* fmts = static_cast<const AVPixelFormat*>(list);
    for (int i = 0; i < count; ++i)
        if (fmts[i] == AV_PIX_FMT_YUV420P)
            return true;
    return false;
}

AVPixelFormat cardFormat(AVHWDeviceType device)
{
    switch (device) {
    case AV_HWDEVICE_TYPE_VAAPI: return AV_PIX_FMT_VAAPI;
    case AV_HWDEVICE_TYPE_CUDA: return AV_PIX_FMT_CUDA;
    case AV_HWDEVICE_TYPE_QSV: return AV_PIX_FMT_QSV;
    default: return AV_PIX_FMT_NONE;
    }
}

} // namespace

VideoEncoder::~VideoEncoder()
{
    reset();
}

void VideoEncoder::reset()
{
    m_ctx.reset();
    av_buffer_unref(&m_frames);
    av_buffer_unref(&m_device);
    m_upload = false;
    m_input = AV_PIX_FMT_YUV420P;
}

bool VideoEncoder::open(int w, int h, AVRational frameRate, int quality, bool globalHeader, bool tryHardware)
{
    static const Candidate hardware[] = {
#if defined(_WIN32)
        { "h264_nvenc", AV_HWDEVICE_TYPE_NONE },
        { "h264_qsv", AV_HWDEVICE_TYPE_NONE },
        { "h264_amf", AV_HWDEVICE_TYPE_NONE },
#elif defined(__APPLE__)
        { "h264_videotoolbox", AV_HWDEVICE_TYPE_NONE },
#else
        { "h264_vaapi", AV_HWDEVICE_TYPE_VAAPI }, // Intel and AMD
        { "h264_nvenc", AV_HWDEVICE_TYPE_NONE },
#endif
    };

    if (tryHardware)
        for (const Candidate& c : hardware)
            if (tryOpen(c, w, h, frameRate, quality, globalHeader))
                return true;

    // Good old CPU encoding works everywhere
    return tryOpen({ "libx264", AV_HWDEVICE_TYPE_NONE }, w, h, frameRate, quality, globalHeader);
}

bool VideoEncoder::tryOpen(const Candidate& c, int w, int h, AVRational frameRate, int quality, bool globalHeader)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(c.name);
    if (!codec)
        return false;

    for (const Options& opts : qualityOptions(c.name, quality)) {
        reset();

        if (c.device != AV_HWDEVICE_TYPE_NONE) {
            // Set up the graphics card and a pool of frames that live on it
            if (av_hwdevice_ctx_create(&m_device, c.device, nullptr, nullptr, 0) < 0)
                return false;
            m_frames = av_hwframe_ctx_alloc(m_device);
            if (!m_frames)
                return false;
            auto* pool = reinterpret_cast<AVHWFramesContext*>(m_frames->data);
            pool->format = cardFormat(c.device);
            pool->sw_format = AV_PIX_FMT_NV12;
            pool->width = w;
            pool->height = h;
            pool->initial_pool_size = 16;
            if (av_hwframe_ctx_init(m_frames) < 0)
                return false;
            m_input = AV_PIX_FMT_NV12;
            m_upload = true;
        } else {
            m_input = takesYuv420p(codec) ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
        }

        CodecPtr ctx(avcodec_alloc_context3(codec));
        ctx->width = w;
        ctx->height = h;
        ctx->time_base = av_inv_q(frameRate);
        ctx->framerate = frameRate;
        ctx->pix_fmt = m_upload ? cardFormat(c.device) : m_input;
        ctx->colorspace = AVCOL_SPC_BT709;
        ctx->color_primaries = AVCOL_PRI_BT709;
        ctx->color_trc = AVCOL_TRC_BT709;
        ctx->color_range = AVCOL_RANGE_MPEG;
        ctx->gop_size = int(av_q2d(frameRate) * 4); // a keyframe every few seconds keeps seeking snappy
        if (globalHeader)
            ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        ctx->thread_count = threadLimit(); // 0 = one per core
        if (m_upload)
            ctx->hw_frames_ctx = av_buffer_ref(m_frames);

        AVDictionary* dict = nullptr;
        for (const auto& [key, value] : opts)
            av_dict_set(&dict, key, value.c_str(), 0);
        int r = avcodec_open2(ctx.get(), codec, &dict);
        av_dict_free(&dict);

        if (r >= 0) {
            if (std::getenv("VE_EXPORT_STATS")) {
                std::string list;
                for (const auto& [key, value] : opts)
                    list += std::string(" ") + key + "=" + value;
                std::fprintf(stderr, "encoder %s opened with%s\n", c.name, list.c_str());
            }
            m_ctx = std::move(ctx);
            m_name = c.name;
            return true;
        }
    }
    reset();
    return false;
}

AVFrame* VideoEncoder::prepare(AVFrame* frame)
{
    // Already on the graphics card (decoded there)? Perfect, nothing to do.
    if (frame->hw_frames_ctx)
        return m_upload ? frame : nullptr;

    AVFrame* out = frame;

    if (m_input != AV_PIX_FMT_YUV420P) {
        // Same picture, just with the colour planes packed the way this encoder likes
        if (!m_repacked)
            m_repacked.reset(av_frame_alloc());
        av_frame_unref(m_repacked.get());
        m_repacked->format = m_input;
        m_repacked->width = frame->width;
        m_repacked->height = frame->height;
        if (av_frame_get_buffer(m_repacked.get(), 0) < 0)
            return nullptr;
        if (!m_repack)
            m_repack.reset(sws_getContext(frame->width, frame->height, AV_PIX_FMT_YUV420P,
                                          frame->width, frame->height, m_input, SWS_POINT,
                                          nullptr, nullptr, nullptr));
        if (!m_repack)
            return nullptr;
        sws_scale(m_repack.get(), frame->data, frame->linesize, 0, frame->height,
                  m_repacked->data, m_repacked->linesize);
        av_frame_copy_props(m_repacked.get(), frame);
        out = m_repacked.get();
    }

    if (m_upload) {
        if (!m_onCard)
            m_onCard.reset(av_frame_alloc());
        av_frame_unref(m_onCard.get());
        if (av_hwframe_get_buffer(m_frames, m_onCard.get(), 0) < 0
            || av_hwframe_transfer_data(m_onCard.get(), out, 0) < 0)
            return nullptr;
        av_frame_copy_props(m_onCard.get(), out);
        out = m_onCard.get();
    }
    return out;
}

} // namespace ve
