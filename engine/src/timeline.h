// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "audio_reader.h"
#include "effects.h"
#include "video_reader.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace ve {

struct Clip {
    std::string path;
    int layer = 0;         // bigger number = drawn on top
    double start = 0.0;    // where it sits on the timeline
    double in = 0.0;       // where in the source file it starts playing from
    double duration = 0.0;
    bool useVideo = true;
    bool useAudio = true;
    float volume = 1.0f;
    double fadeIn = 0.0;  // seconds to fade up from nothing
    double fadeOut = 0.0; // seconds to fade away at the end
    double speed = 1.0;   // 2 = twice as fast, 0.5 = slow motion

    // Picture-in-picture: size and position on screen, and how see-through
    float opacity = 1.0f;
    float scale = 1.0f;   // 1 = fills the frame like normal
    float posX = 0.0f;    // shift, as a fraction of the frame width (0 = centred)
    float posY = 0.0f;

    Effects effects;

    double end() const { return start + duration; }
    bool activeAt(double t) const { return t >= start && t < end(); }

    // Which moment of the source file is on screen at timeline time t
    double sourceTime(double t) const { return in + (t - start) * speed; }

    // Nothing fancy going on (so export can take its fast lane)
    bool plain() const { return !effects.any() && opacity >= 1.0f && scale == 1.0f && posX == 0.0f && posY == 0.0f; }

    // How "there" the clip is at time t: 0 = faded out completely, 1 = fully visible/audible
    double envelope(double t) const
    {
        double e = 1.0;
        if (fadeIn > 0)
            e = std::min(e, (t - start) / fadeIn);
        if (fadeOut > 0)
            e = std::min(e, (end() - t) / fadeOut);
        return std::clamp(e, 0.0, 1.0);
    }
};

// Turns a list of clips into finished pictures and sound for any moment in time.
// One Timeline per thread - they keep their own decoders.
class Timeline {
public:
    Timeline() = default;
    Timeline(const Timeline&) = delete;
    Timeline& operator=(const Timeline&) = delete;
    ~Timeline();

    // Decode video on this graphics card from now on (nullptr = back to the CPU)
    void setHardwareDevice(AVBufferRef* device);

    // For the live preview: a few decoder threads instead of one per core, and quicker scaling.
    void usePreviewSettings();

    void setClips(const std::vector<Clip>& clips);
    double duration() const;

    // Draws the frame at time t into a w x h RGBA canvas (black where nothing covers it).
    // bgra = write the pixels in B G R A order instead (what screens want, saves a conversion)
    void renderVideo(double t, int w, int h, uint8_t* rgba, bool bgra = false);

    // Export's fast lane: if one clip fills the whole picture, fill `out` (an empty frame)
    // straight in the encoder's YUV 4:2:0 format and skip the RGBA detour.
    // Returns false when it can't, and you should use renderVideo instead.
    bool renderVideoDirect(double t, int w, int h, AVFrame* out);

    // Can this be exported instantly, by copying pieces of one file without re-encoding?
    // If so fills in the file and the pieces, otherwise `why` says what's stopping it.
    bool copyPlan(std::string& source, std::vector<struct CopySegment>& segments, std::string& why) const;

    // Mixes `frames` stereo samples starting at time t.
    void renderAudio(double t, int frames, float* out);

private:
    struct Slot {
        Clip clip;
        std::unique_ptr<VideoReader> video;
        std::unique_ptr<AudioReader> audio;
        bool videoFailed = false;
        bool audioFailed = false;
        // For closing decoders nobody's used in a while. Separate for picture and sound,
        // because export works on those from two different threads.
        uint64_t videoUsed = 0;
        uint64_t audioUsed = 0;
        // A still picture (like a title) scaled once and kept, instead of every frame
        std::vector<uint8_t> still;
        int stillW = 0, stillH = 0;
        bool stillBgra = false;
    };

    VideoReader* videoFor(Slot& s, double t);
    AudioReader* audioFor(Slot& s, double t);
    void closeIdleVideo(double t);
    void closeIdleAudio(double t);

    std::vector<Slot> m_slots;
    AVBufferRef* m_hwDevice = nullptr;
    int m_threads = 0; // decoder threads, 0 = one per CPU core
    bool m_fastScaling = false;
    uint64_t m_videoTick = 0;
    uint64_t m_audioTick = 0;
    std::vector<uint8_t> m_scratch;
    std::vector<uint8_t> m_layer; // one clip's picture, while its effects get applied
    EffectsScratch m_fx;
    std::vector<float> m_mix;
    std::vector<float> m_speedBuf; // sound for sped-up/slowed-down clips
};

} // namespace ve
