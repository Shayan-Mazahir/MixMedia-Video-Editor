#pragma once

#include <ve/engine.h>

#include <QByteArray>
#include <QList>
#include <QString>

#include <vector>

// A clip described just enough for the engine to play it. Safe to copy between threads.
struct RenderClip {
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
        list.push_back(v);
    }
    ve_timeline_set_clips(tl, list.data(), int(list.size()));
}
