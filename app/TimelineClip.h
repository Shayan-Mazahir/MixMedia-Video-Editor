#pragma once

#include <QDataStream>
#include <QPixmap>
#include <QString>

struct TimelineClip {
    QString path;
    QString name;
    double start = 0.0;          // where it sits on the timeline (seconds)
    double in = 0.0;             // where in the file it starts playing from
    double duration = 0.0;       // how long it plays for
    double sourceDuration = 0.0; // full length of the file, 0 = a picture (any length you like)
    int track = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    QPixmap thumb;

    double end() const { return start + duration; }
    bool audioOnly() const { return hasAudio && !hasVideo; }
    bool isStill() const { return sourceDuration <= 0.0; }
};

// So clips can be packed up for drag & drop
inline QDataStream& operator<<(QDataStream& out, const TimelineClip& c)
{
    return out << c.path << c.name << c.start << c.in << c.duration << c.sourceDuration << c.track
               << c.hasVideo << c.hasAudio << c.width << c.height << c.fps << c.thumb;
}

inline QDataStream& operator>>(QDataStream& in, TimelineClip& c)
{
    return in >> c.path >> c.name >> c.start >> c.in >> c.duration >> c.sourceDuration >> c.track
              >> c.hasVideo >> c.hasAudio >> c.width >> c.height >> c.fps >> c.thumb;
}
