#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <string>

namespace ve {

// Small wrappers so FFmpeg objects clean themselves up automatically.
struct FormatCloser {
    void operator()(AVFormatContext* f) const { avformat_close_input(&f); }
};
struct CodecFreer {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct PacketFreer {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct FrameFreer {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct SwsFreer {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};
struct SwrFreer {
    void operator()(SwrContext* s) const { swr_free(&s); }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecFreer>;
using PacketPtr = std::unique_ptr<AVPacket, PacketFreer>;
using FramePtr = std::unique_ptr<AVFrame, FrameFreer>;
using SwsPtr = std::unique_ptr<SwsContext, SwsFreer>;
using SwrPtr = std::unique_ptr<SwrContext, SwrFreer>;

void quietLogs();

// Deletes a file. The path is UTF-8, which also works on Windows (unlike std::remove).
void deleteFile(const std::string& utf8Path);
FormatPtr openInput(const char* path);

// Opens a decoder for the best stream of the given type. Returns the stream index, or -1.
// Give it a graphics card (hwDevice) and video gets decoded there if the card can handle it.
int openDecoder(AVFormatContext* fmt, AVMediaType type, CodecPtr& out, AVBufferRef* hwDevice = nullptr);

} // namespace ve
