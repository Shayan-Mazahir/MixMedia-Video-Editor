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

// How one clip hands over to the next on the same track (centred on the cut)
enum class Transition { None, Dissolve, FadeBlack, WipeLeft, WipeRight, WipeUp, WipeDown, SlideLeft, SlideRight, Zoom, Count };

// How a clip arrives or leaves
enum class Anim { None, Fade, SlideLeft, SlideRight, SlideUp, SlideDown, Zoom, Wipe, Count };

struct Clip {
    // Media = a video/picture/sound file. Adjustment = no file, its effects apply to
    // everything underneath it (like a filter laid over the picture). Transition = no file,
    // plays its transition on everything underneath.
    enum class Kind { Media, Adjustment, Transition };
    Kind kind = Kind::Media;
    // For Transition blocks: out and back in on the spot, in from black, or out to black
    enum class Part { Through, In, Out };
    Part part = Part::Through;

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

    // Transition from the clip that ends right where this one starts (same layer)
    Transition transition = Transition::None;
    double transitionDuration = 1.0;

    Anim animIn = Anim::None, animOut = Anim::None;
    double animInDuration = 0.5, animOutDuration = 0.5;

    // Worked out by the timeline: extra time either side that a transition borrows
    double preRoll = 0.0, postRoll = 0.0;
    // ...and when it blends in from the clip before, or out into the clip after (0 width = it doesn't)
    double blendInFrom = 0.0, blendInTo = 0.0;
    double blendOutFrom = 0.0, blendOutTo = 0.0;
    // Needed at time t (playing, or taking part in a transition)
    bool inUse(double t) const { return t >= start - preRoll && t < end() + postRoll; }

    double end() const { return start + duration; }
    bool activeAt(double t) const { return t >= start && t < end(); }

    // Which moment of the source file is on screen at timeline time t
    double sourceTime(double t) const { return in + (t - start) * speed; }

    // Nothing fancy going on (so export can take its fast lane)
    bool plain() const
    {
        return kind == Kind::Media && !effects.any() && opacity >= 1.0f && scale == 1.0f && posX == 0.0f && posY == 0.0f
               && transition == Transition::None && animIn == Anim::None && animOut == Anim::None
               && preRoll == 0.0 && postRoll == 0.0 && blendInTo <= blendInFrom && blendOutTo <= blendOutFrom;
    }

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
        // The clips either side of a transition (indexes into m_slots, -1 = none)
        int prev = -1;
        int next = -1;
        // A still picture (like a title) scaled once and kept, instead of every frame
        std::vector<uint8_t> still;
        int stillW = 0, stillH = 0;
        bool stillBgra = false;
    };

    VideoReader* videoFor(Slot& s, double t);
    AudioReader* audioFor(Slot& s, double t);
    // Tweaks for drawing one clip: transitions fade, shift, zoom or crop it
    struct DrawMods {
        double opacity = 1.0;
        double dx = 0.0, dy = 0.0; // pixels
        double scale = 1.0;
        int clipX0 = 0, clipY0 = 0, clipX1 = 1 << 30, clipY1 = 1 << 30; // only draw inside this box
    };
    void drawSlot(Slot& s, double t, int w, int h, uint8_t* canvas, bool bgra, DrawMods mods);
    void drawTransition(Slot& a, Slot& b, double t, int w, int h, uint8_t* canvas, bool bgra);
    void applyAdjustment(const Clip& c, double t, int w, int h, uint8_t* canvas, bool bgra);
    void applyTransitionBlock(const Clip& c, double t, int w, int h, uint8_t* canvas);
    void linkTransitions();

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
    std::vector<uint32_t> m_rowScratch; // one row of a moving/zooming picture
    EffectsScratch m_fx;
    std::vector<float> m_mix;
    std::vector<float> m_speedBuf; // sound for sped-up/slowed-down clips
};

} // namespace ve
