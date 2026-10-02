// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "ffmpeg_util.h"

#include <string>

namespace ve {

// An H.264 encoder that tries the graphics card first and falls back to x264 on the CPU.
class VideoEncoder {
public:
    ~VideoEncoder();

    // quality works like x264's crf: lower = better looking, bigger file
    bool open(int w, int h, AVRational frameRate, int quality, bool globalHeader, bool tryHardware);

    AVCodecContext* context() const { return m_ctx.get(); }
    const std::string& name() const { return m_name; }

    // The graphics card it encodes on, if it wants frames to already be there (VA-API). nullptr otherwise.
    AVBufferRef* device() const { return m_upload ? m_device : nullptr; }

    // Takes a YUV 4:2:0 frame and hands back whatever this encoder actually wants
    // (repacked and/or copied onto the graphics card). nullptr if something broke.
    AVFrame* prepare(AVFrame* yuv420);

private:
    struct Candidate {
        const char* name;
        AVHWDeviceType device; // NONE = it takes normal frames from memory
    };

    bool tryOpen(const Candidate& c, int w, int h, AVRational frameRate, int quality, bool globalHeader);
    void reset();

    CodecPtr m_ctx;
    std::string m_name;
    AVBufferRef* m_device = nullptr;
    AVBufferRef* m_frames = nullptr;
    AVPixelFormat m_input = AV_PIX_FMT_YUV420P; // the format it wants in memory
    bool m_upload = false;                      // does it need frames copied to the graphics card?

    FramePtr m_repacked;
    FramePtr m_onCard;
    SwsPtr m_repack;
};

} // namespace ve
