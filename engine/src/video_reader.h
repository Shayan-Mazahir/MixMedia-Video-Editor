// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "ffmpeg_util.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ve {

// Bits to cut off each edge of a frame, as fractions of the picture (0.1 = a tenth)
struct FrameCrop {
    float left = 0, right = 0, top = 0, bottom = 0;
    bool any() const { return left > 0 || right > 0 || top > 0 || bottom > 0; }
};

// Pulls frames out of one video file. Times are in seconds from the start of the file.
class VideoReader {
public:
    VideoReader() = default;
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;
    ~VideoReader();

    // hwDevice (optional): decode on this graphics card. Frames then live on the card,
    // which is perfect for handing to a hardware encoder.
    // threads: decoder threads, 0 = one per CPU core
    bool open(const std::string& path, AVBufferRef* hwDevice = nullptr, int threads = 0);
    bool isOpen() const { return m_fmt != nullptr; }
    bool isStill() const { return m_still; } // a picture, not a video

    int width() const { return m_ctx ? m_ctx->width : 0; }
    int height() const { return m_ctx ? m_ctx->height : 0; }

    // The frame on screen at `sec`. With fast=true the nearest keyframe is good enough
    // (great for thumbnails). Returns nullptr if nothing could be decoded.
    const AVFrame* frameAt(double sec, bool fast = false);

    // Bits to cut off each edge, as fractions of the picture (0.1 = a tenth)
    struct Crop {
        float left = 0, right = 0, top = 0, bottom = 0;
        bool any() const { return left > 0 || right > 0 || top > 0 || bottom > 0; }
    };

    // Scales a frame to exactly w x h RGBA (or BGRA, the order most screens use),
    // cropping it first if asked.
    bool scale(const AVFrame* frame, int w, int h, uint8_t* dst, int dstStride, bool bgra = false,
               const FrameCrop& crop = FrameCrop());

    // Converts a frame into a new w x h YUV 4:2:0 frame (HD colours), allocating `out`'s buffers.
    bool toYuv420(const AVFrame* frame, int w, int h, AVFrame* out);

    // Playing backwards: decoders only go forwards, so this grabs about a second of frames at a
    // time and hands them out from the end. Much quicker than seeking for every single frame.
    void setBackwards(bool backwards);

    // Quicker, slightly rougher scaling. Fine for a small preview, not for export.
    void setFastScaling(bool fast) { m_fastScaling = fast; }

    // Brings a frame that lives on the graphics card back into normal memory (no-op otherwise)
    const AVFrame* toMemory(const AVFrame* frame);

private:
    void seekTo(double sec);
    const AVFrame* backwardsFrameAt(double sec);
    bool decodeNext();
    double frameStart(const AVFrame* f) const;
    double frameEnd(const AVFrame* f) const;

    std::string m_path;
    AVBufferRef* m_hwDevice = nullptr;
    int m_threads = 0;
    bool m_fastScaling = false;
    bool m_still = false;
    FormatPtr m_fmt;
    CodecPtr m_ctx;
    int m_stream = -1;
    AVRational m_timeBase { 1, 1 };
    int64_t m_startPts = 0;
    double m_frameDuration = 1.0 / 30.0;

    PacketPtr m_pkt;
    FramePtr m_frame; // the one we're currently showing
    FramePtr m_next;
    FramePtr m_inMemory; // copy of a card frame, for when the CPU needs to look at it
    FramePtr m_cropped;  // the same frame with its edges trimmed (shares the pixels)
    bool m_backwards = false;
    std::vector<FramePtr> m_window; // a second or so of frames, in order (when going backwards)
    bool m_haveFrame = false;
    bool m_eof = false;
    bool m_flushed = false;

    SwsPtr m_sws;
    int m_swsKey[6] = {};
    SwsPtr m_yuvSws;
    int m_yuvSwsKey[6] = {};
};

} // namespace ve
