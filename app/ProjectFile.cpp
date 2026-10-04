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

// Keyframed settings, by name in the file (same order as VE_KEY_*)
const char* const KeyNames[VE_KEY_COUNT] = { "size", "posX", "posY", "opacity", "rotation", "volume" };

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

QJsonObject styleToJson(const TitleStyle& s)
{
    return QJsonObject {
        { "text", s.text },
        { "size", s.size },
        { "color", s.color.name(QColor::HexArgb) },
        { "y", s.y },
        { "box", s.box },
        { "bold", s.bold },
        { "font", s.font },
        { "italic", s.italic },
        { "align", s.align },
        { "x", s.x },
        { "spacing", s.spacing },
        { "outline", s.outline },
        { "outlineColor", s.outlineColor.name(QColor::HexArgb) },
        { "shadow", s.shadow },
        { "shadowColor", s.shadowColor.name(QColor::HexArgb) },
        { "shadowDistance", s.shadowDistance },
        { "shadowSoftness", s.shadowSoftness },
        { "boxColor", s.boxColor.name(QColor::HexArgb) },
        { "wordByWord", s.wordByWord },
        { "highlight", s.highlight.name(QColor::HexArgb) },
        { "wordsAtOnce", s.wordsAtOnce },
    };
}

TitleStyle styleFromJson(const QJsonObject& t, const TitleStyle& defaults = {})
{
    TitleStyle s;
    s.text = t["text"].toString();
    s.size = t["size"].toDouble(defaults.size);
    s.color = QColor(t["color"].toString(defaults.color.name(QColor::HexArgb)));
    s.y = t["y"].toDouble(defaults.y);
    s.box = t["box"].toBool(defaults.box);
    s.bold = t["bold"].toBool(defaults.bold);
    s.font = t["font"].toString();
    s.italic = t["italic"].toBool();
    s.align = std::clamp(t["align"].toInt(1), 0, 2);
    s.x = t["x"].toDouble(0.5);
    s.spacing = t["spacing"].toDouble();
    s.outline = t["outline"].toDouble();
    s.outlineColor = QColor(t["outlineColor"].toString("#ff000000"));
    // Older projects: titles without a box always had a shadow
    s.shadow = t.contains("shadow") ? t["shadow"].toBool() : !s.box;
    s.shadowColor = QColor(t["shadowColor"].toString("#aa000000"));
    s.shadowDistance = t["shadowDistance"].toDouble(6.0);
    s.shadowSoftness = t["shadowSoftness"].toDouble(20.0);
    s.boxColor = QColor(t["boxColor"].toString(defaults.boxColor.name(QColor::HexArgb)));
    s.wordByWord = t["wordByWord"].toBool();
    s.highlight = QColor(t["highlight"].toString(defaults.highlight.name(QColor::HexArgb)));
    s.wordsAtOnce = std::clamp(t["wordsAtOnce"].toInt(defaults.wordsAtOnce), 1, 12);
    return s;
}

QString kindName(TimelineClip::Kind kind)
{
    switch (kind) {
    case TimelineClip::Kind::Subtitle: return "subtitle";
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
    if (name == "subtitle")
        return TimelineClip::Kind::Subtitle;
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
        { "crop", QJsonArray { c.cropLeft, c.cropRight, c.cropTop, c.cropBottom } },
        { "rotation", c.rotation },
        { "flipH", c.flipH },
        { "flipV", c.flipV },
        { "fill", c.fill },
        { "reverse", c.reverse },
        { "sound", QJsonObject {
            { "keepPitch", c.keepPitch },
            { "denoise", c.denoise },
            { "duck", c.duck },
            { "duckAmount", c.duckAmount },
        } },
        { "freeze", c.freeze },
        { "greenScreen", QJsonObject {
            { "on", c.chromaKey },
            { "color", c.keyColor.name() },
            { "strength", c.keyStrength },
            { "softness", c.keySoftness },
            { "spill", c.keySpill },
        } },
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
        o["title"] = styleToJson(c.title);
    } else if (c.isSubtitle()) {
        o["text"] = c.title.text;
        if (c.ownStyle)
            o["style"] = styleToJson(c.title);
        if (!c.words.isEmpty()) {
            QJsonArray words; // [word, start, end]
            for (const WordTime& w : c.words)
                words << QJsonArray { w.word, w.start, w.end };
            o["words"] = words;
        }
    } else if (c.kind == TimelineClip::Kind::Media) {
        o["file"] = fileRef(c.path, dir);
    }
    if (c.hasKeys()) {
        // "keyframes": { "size": [[time, value], ...], ... }
        QJsonObject keys;
        for (int p = 0; p < VE_KEY_COUNT; ++p) {
            QJsonArray list;
            for (const Keyframe& f : c.keys[size_t(p)])
                list << QJsonArray { f.time, f.value };
            if (!list.isEmpty())
                keys[KeyNames[p]] = list;
        }
        o["keyframes"] = keys;
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
    QJsonArray crop = o["crop"].toArray();
    if (crop.size() == 4) {
        c.cropLeft = float(crop[0].toDouble());
        c.cropRight = float(crop[1].toDouble());
        c.cropTop = float(crop[2].toDouble());
        c.cropBottom = float(crop[3].toDouble());
    }
    c.rotation = float(o["rotation"].toDouble());
    c.flipH = o["flipH"].toBool();
    c.flipV = o["flipV"].toBool();
    c.fill = o["fill"].toBool();
    c.reverse = o["reverse"].toBool();
    QJsonObject sound = o["sound"].toObject();
    c.keepPitch = sound["keepPitch"].toBool(true);
    c.denoise = float(sound["denoise"].toDouble());
    c.duck = sound["duck"].toBool();
    c.duckAmount = float(sound["duckAmount"].toDouble(0.7));
    c.freeze = o["freeze"].toBool();
    QJsonObject key = o["greenScreen"].toObject();
    c.chromaKey = key["on"].toBool();
    c.keyColor = QColor(key["color"].toString("#00c83c"));
    c.keyStrength = float(key["strength"].toDouble(0.4));
    c.keySoftness = float(key["softness"].toDouble(0.2));
    c.keySpill = float(key["spill"].toDouble(0.5));
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
        c.title = styleFromJson(o["title"].toObject());
    } else if (c.isSubtitle()) {
        c.ownStyle = o.contains("style");
        c.title = c.ownStyle ? styleFromJson(o["style"].toObject(), TitleStyle::subtitles()) : TitleStyle::subtitles();
        c.title.text = o["text"].toString();
        for (const QJsonValue& v : o["words"].toArray()) {
            QJsonArray w = v.toArray();
            if (w.size() == 3)
                c.words << WordTime { w[0].toString(), w[1].toDouble(), w[2].toDouble() };
        }
    } else if (c.kind == TimelineClip::Kind::Media) {
        c.path = resolve(o["file"].toObject(), dir, missing);
    }
    QJsonObject keys = o["keyframes"].toObject();
    for (int p = 0; p < VE_KEY_COUNT; ++p) {
        for (const QJsonValue& v : keys[KeyNames[p]].toArray()) {
            QJsonArray pair = v.toArray();
            if (pair.size() == 2)
                c.setKey(p, pair[0].toDouble(), float(pair[1].toDouble()));
        }
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
        using K = TimelineTrack::Kind;
        const char* kind = t.kind == K::Fx ? "fx" : t.kind == K::Audio ? "audio" : t.kind == K::Subtitles ? "subtitles" : "video";
        QJsonObject track { { "name", t.name }, { "kind", kind } };
        if (t.kind == K::Subtitles)
            track["style"] = styleToJson(t.style);
        tracks << track;
    }

    QJsonObject root {
        { "app", "MixMedia Video Editor" },
        { "version", FormatVersion },
        { "media", media },
        { "project", QJsonObject { { "shape", data.settings.shape },
                                   { "resolution", data.settings.resolution },
                                   { "fps", data.settings.fps } } },
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
    QJsonObject project = root["project"].toObject();
    data->settings.shape = project["shape"].toString("auto");
    data->settings.resolution = project["resolution"].toInt(1080);
    data->settings.fps = project["fps"].toDouble();
    QJsonObject timeline = root["timeline"].toObject();
    data->playhead = timeline["playhead"].toDouble();
    for (const QJsonValue& v : timeline["tracks"].toArray()) {
        QJsonObject t = v.toObject();
        QString kind = t["kind"].toString();
        using K = TimelineTrack::Kind;
        TimelineTrack track { t["name"].toString(),
                              kind == "fx"          ? K::Fx
                              : kind == "audio"     ? K::Audio
                              : kind == "subtitles" ? K::Subtitles
                                                    : K::Video };
        if (track.kind == K::Subtitles)
            track.style = styleFromJson(t["style"].toObject(), TitleStyle::subtitles());
        data->tracks << track;
    }
    for (const QJsonValue& c : timeline["clips"].toArray())
        data->clips << clipFromJson(c.toObject(), dir, missing);
    return true;
}

} // namespace ProjectFile
