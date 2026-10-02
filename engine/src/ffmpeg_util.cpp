#include "ffmpeg_util.h"

#include <filesystem>

namespace ve {

void quietLogs()
{
    // FFmpeg loves to chat in the terminal; only let it speak up for real errors.
    av_log_set_level(AV_LOG_ERROR);
}

void deleteFile(const std::string& utf8Path)
{
    std::error_code ignored;
    std::filesystem::remove(std::filesystem::path(reinterpret_cast<const char8_t*>(utf8Path.c_str())), ignored);
}

FormatPtr openInput(const char* path)
{
    quietLogs();

    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, path, nullptr, nullptr) < 0)
        return nullptr;
    FormatPtr fmt(raw);
    if (avformat_find_stream_info(raw, nullptr) < 0)
        return nullptr;
    return fmt;
}

namespace {

// FFmpeg asks which format we'd like the frames in. Pick the graphics card one if it's on offer.
AVPixelFormat preferCard(AVCodecContext* ctx, const AVPixelFormat* offered)
{
    const auto* device = reinterpret_cast<AVHWDeviceContext*>(ctx->hw_device_ctx->data);
    for (const AVPixelFormat* f = offered; *f != AV_PIX_FMT_NONE; ++f) {
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(*f);
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            continue;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(ctx->codec, i);
            if (!cfg)
                break;
            if (cfg->pix_fmt == *f && cfg->device_type == device->type
                && (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                return *f;
        }
    }
    return avcodec_default_get_format(ctx, offered); // the card can't do this one, CPU it is
}

} // namespace

int openDecoder(AVFormatContext* fmt, AVMediaType type, CodecPtr& out, AVBufferRef* hwDevice)
{
    const AVCodec* decoder = nullptr;
    int idx = av_find_best_stream(fmt, type, -1, -1, &decoder, 0);
    if (idx < 0 || !decoder)
        return -1;

    CodecPtr ctx(avcodec_alloc_context3(decoder));
    if (!ctx || avcodec_parameters_to_context(ctx.get(), fmt->streams[idx]->codecpar) < 0)
        return -1;

    ctx->thread_count = 0; // let FFmpeg pick, uses all cores
    ctx->pkt_timebase = fmt->streams[idx]->time_base;
    if (hwDevice && type == AVMEDIA_TYPE_VIDEO) {
        ctx->hw_device_ctx = av_buffer_ref(hwDevice);
        ctx->get_format = preferCard;
        ctx->extra_hw_frames = 24; // the encoder hangs on to a few, so keep spares around
    }
    if (avcodec_open2(ctx.get(), decoder, nullptr) < 0)
        return -1;

    out = std::move(ctx);
    return idx;
}

} // namespace ve
