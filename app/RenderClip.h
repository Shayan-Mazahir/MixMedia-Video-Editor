// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <ve/engine.h>

#include <QByteArray>
#include <QList>
#include <QString>

#include <vector>

// A clip described just enough for the engine to play it. Safe to copy between threads.
struct RenderClip {
    int kind = VE_CLIP_MEDIA; // or VE_CLIP_ADJUSTMENT / VE_CLIP_TRANSITION, which work on everything below
    QString path;
    int layer = 0;
    double start = 0.0;
    double in = 0.0;
    double duration = 0.0;
    bool video = true;
    bool audio = true;
    float volume = 1.0f;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    double speed = 1.0;
    float opacity = 1.0f, scale = 1.0f, posX = 0.0f, posY = 0.0f;
    int look = 0;
    float brightness = 0, contrast = 0, saturation = 0, temperature = 0, blur = 0, sharpen = 0, vignette = 0;
    int transition = 0;
    double transitionDuration = 1.0;
    int animIn = 0, animOut = 0;
    double animInDuration = 0.5, animOutDuration = 0.5;
    int part = VE_PART_THROUGH; // transition blocks: on the spot, in from black or out to black
};

// Hands the clips to an engine timeline (the engine wants plain C strings, so we keep them alive here).
inline void applyClips(ve_timeline* tl, const QList<RenderClip>& clips)
{
    std::vector<QByteArray> paths;
    std::vector<ve_clip> list;
    paths.reserve(clips.size());
    for (const RenderClip& c : clips) {
        paths.push_back(c.path.toUtf8());
        ve_clip v {};
        v.kind = c.kind;
        v.path = paths.back().constData();
        v.layer = c.layer;
        v.start = c.start;
        v.in = c.in;
        v.duration = c.duration;
        v.use_video = c.video;
        v.use_audio = c.audio;
        v.volume = c.volume;
        v.fade_in = c.fadeIn;
        v.fade_out = c.fadeOut;
        v.speed = c.speed;
        v.transparency = 1.0f - c.opacity;
        v.size = c.scale;
        v.pos_x = c.posX;
        v.pos_y = c.posY;
        v.look = c.look;
        v.brightness = c.brightness;
        v.contrast = c.contrast;
        v.saturation = c.saturation;
        v.temperature = c.temperature;
        v.blur = c.blur;
        v.sharpen = c.sharpen;
        v.vignette = c.vignette;
        v.transition = c.transition;
        v.transition_duration = c.transitionDuration;
        v.anim_in = c.animIn;
        v.anim_in_duration = c.animInDuration;
        v.anim_out = c.animOut;
        v.anim_out_duration = c.animOutDuration;
        v.transition_part = c.part;
        list.push_back(v);
    }
    ve_timeline_set_clips(tl, list.data(), int(list.size()));
}
