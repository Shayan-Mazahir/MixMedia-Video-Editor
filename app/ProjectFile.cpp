// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "ProjectFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace ProjectFile {

namespace {

constexpr int FormatVersion = 2; // 2 = tracks are saved, transitions are their own blocks

// We keep both the full path and one relative to the project, so moving the
// whole folder (or opening it on another computer) still finds everything.
QJsonObject fileRef(const QString& path, const QDir& projectDir)
{
    return { { "path", path }, { "relative", projectDir.relativeFilePath(path) } };
}

QString resolve(const QJsonObject& ref, const QDir& projectDir, QStringList* missing)
{
    QString full = ref["path"].toString();
    if (full.isEmpty() || QFileInfo::exists(full))
        return full;
    QString relative = projectDir.absoluteFilePath(ref["relative"].toString());
    if (QFileInfo::exists(relative))
        return QDir::cleanPath(relative);
    if (missing && !missing->contains(full))
        *missing << full;
    return full;
}

QString kindName(TimelineClip::Kind kind)
{
    switch (kind) {
    case TimelineClip::Kind::Title: return "title";
    case TimelineClip::Kind::Effect: return "effect";
    case TimelineClip::Kind::Transition: return "transition";
    default: return "media";
    }
}

TimelineClip::Kind kindFromName(const QString& name)
{
    if (name == "title")
        return TimelineClip::Kind::Title;
    if (name == "effect")
        return TimelineClip::Kind::Effect;
    if (name == "transition")
        return TimelineClip::Kind::Transition;
    return TimelineClip::Kind::Media;
}

QJsonObject clipToJson(const TimelineClip& c, const QDir& dir)
{
    QJsonObject o {
        { "kind", kindName(c.kind) },
        { "name", c.name },
        { "start", c.start },
        { "in", c.in },
        { "duration", c.duration },
        { "sourceDuration", c.sourceDuration },
        { "track", c.track },
        { "hasVideo", c.hasVideo },
        { "hasAudio", c.hasAudio },
        { "width", c.width },
        { "height", c.height },
        { "fps", c.fps },
        { "videoOn", c.videoOn },
        { "audioOn", c.audioOn },
        { "volume", c.volume },
        { "fadeIn", c.fadeIn },
        { "fadeOut", c.fadeOut },
        { "speed", c.speed },
        { "opacity", c.opacity },
        { "scale", c.scale },
        { "posX", c.posX },
        { "posY", c.posY },
        { "effects", QJsonObject {
            { "look", c.look },
            { "brightness", c.brightness },
            { "contrast", c.contrast },
            { "saturation", c.saturation },
            { "temperature", c.temperature },
            { "blur", c.blur },
            { "sharpen", c.sharpen },
            { "vignette", c.vignette },
        } },
        { "transition", QJsonObject { { "type", c.transition }, { "duration", c.transitionDuration } } },
        { "animation", QJsonObject {
            { "in", c.animIn },
            { "inDuration", c.animInDuration },
            { "out", c.animOut },
            { "outDuration", c.animOutDuration },
        } },
    };
    if (c.isTitle()) {
        o["title"] = QJsonObject {
            { "text", c.title.text },
            { "size", c.title.size },
            { "color", c.title.color.name(QColor::HexArgb) },
            { "y", c.title.y },
            { "box", c.title.box },
            { "bold", c.title.bold },
        };
    } else if (c.kind == TimelineClip::Kind::Media) {
        o["file"] = fileRef(c.path, dir);
    }
    return o;
}

TimelineClip clipFromJson(const QJsonObject& o, const QDir& dir, QStringList* missing)
{
    TimelineClip c;
    c.kind = kindFromName(o["kind"].toString());
    c.name = o["name"].toString();
    c.start = o["start"].toDouble();
    c.in = o["in"].toDouble();
    c.duration = o["duration"].toDouble();
    c.sourceDuration = o["sourceDuration"].toDouble();
    c.track = o["track"].toInt();
    c.hasVideo = o["hasVideo"].toBool();
    c.hasAudio = o["hasAudio"].toBool();
    c.width = o["width"].toInt();
    c.height = o["height"].toInt();
    c.fps = o["fps"].toDouble();
    c.videoOn = o["videoOn"].toBool(true);
    c.audioOn = o["audioOn"].toBool(true);
    c.volume = float(o["volume"].toDouble(1.0));
    c.fadeIn = o["fadeIn"].toDouble();
    c.fadeOut = o["fadeOut"].toDouble();
    c.speed = o["speed"].toDouble(1.0);
    c.opacity = float(o["opacity"].toDouble(1.0));
    c.scale = float(o["scale"].toDouble(1.0));
    c.posX = float(o["posX"].toDouble());
    c.posY = float(o["posY"].toDouble());
    QJsonObject fx = o["effects"].toObject();
    c.look = fx["look"].toInt();
    c.brightness = float(fx["brightness"].toDouble());
    c.contrast = float(fx["contrast"].toDouble());
    c.saturation = float(fx["saturation"].toDouble());
    c.temperature = float(fx["temperature"].toDouble());
    c.blur = float(fx["blur"].toDouble());
    c.sharpen = float(fx["sharpen"].toDouble());
    c.vignette = float(fx["vignette"].toDouble());
    QJsonObject tr = o["transition"].toObject();
    c.transition = tr["type"].toInt();
    c.transitionDuration = tr["duration"].toDouble(1.0);
    QJsonObject anim = o["animation"].toObject();
    c.animIn = anim["in"].toInt();
    c.animInDuration = anim["inDuration"].toDouble(0.5);
    c.animOut = anim["out"].toInt();
    c.animOutDuration = anim["outDuration"].toDouble(0.5);
    if (c.isTitle()) {
        QJsonObject t = o["title"].toObject();
        c.title.text = t["text"].toString();
        c.title.size = t["size"].toDouble(8.0);
        c.title.color = QColor(t["color"].toString("#ffffffff"));
        c.title.y = t["y"].toDouble(0.82);
        c.title.box = t["box"].toBool(true);
        c.title.bold = t["bold"].toBool(true);
    } else if (c.kind == TimelineClip::Kind::Media) {
        c.path = resolve(o["file"].toObject(), dir, missing);
    }
    return c;
}

} // namespace

bool save(const QString& path, const Data& data, QString* error)
{
    QDir dir = QFileInfo(path).absoluteDir();

    QJsonArray media;
    for (const QString& m : data.media)
        media << fileRef(m, dir);
    QJsonArray clips;
    for (const TimelineClip& c : data.clips)
        clips << clipToJson(c, dir);
    QJsonArray tracks;
    for (const TimelineTrack& t : data.tracks) {
        const char* kind = t.kind == TimelineTrack::Kind::Fx ? "fx" : t.kind == TimelineTrack::Kind::Audio ? "audio" : "video";
        tracks << QJsonObject { { "name", t.name }, { "kind", kind } };
    }

    QJsonObject root {
        { "app", "MixMedia Video Editor" },
        { "version", FormatVersion },
        { "media", media },
        { "timeline", QJsonObject { { "playhead", data.playhead }, { "tracks", tracks }, { "clips", clips } } },
    };

    // QSaveFile writes to a temp file first, so a crash mid-save can't wreck your project
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool load(const QString& path, Data* data, QStringList* missing, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    QJsonParseError parse;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (doc.isNull() || !doc.isObject()) {
        *error = "This doesn't look like a MixMedia project (" + parse.errorString() + ")";
        return false;
    }
    QJsonObject root = doc.object();
    if (root["version"].toInt() > FormatVersion) {
        *error = "This project was made by a newer version of MixMedia.";
        return false;
    }

    QDir dir = QFileInfo(path).absoluteDir();
    *data = {};
    for (const QJsonValue& m : root["media"].toArray())
        data->media << resolve(m.toObject(), dir, missing);
    QJsonObject timeline = root["timeline"].toObject();
    data->playhead = timeline["playhead"].toDouble();
    for (const QJsonValue& v : timeline["tracks"].toArray()) {
        QJsonObject t = v.toObject();
        QString kind = t["kind"].toString();
        data->tracks << TimelineTrack { t["name"].toString(),
                                        kind == "fx"      ? TimelineTrack::Kind::Fx
                                        : kind == "audio" ? TimelineTrack::Kind::Audio
                                                          : TimelineTrack::Kind::Video };
    }
    for (const QJsonValue& c : timeline["clips"].toArray())
        data->clips << clipFromJson(c.toObject(), dir, missing);
    return true;
}

} // namespace ProjectFile
