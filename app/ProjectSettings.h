// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <QList>
#include <QSize>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>

// The shape, size and frame rate of the finished video
struct ProjectSettings {
    QString shape = "auto"; // "auto" = same as the first video, otherwise "16:9", "9:16", ...
    int resolution = 1080;  // the short side in pixels (1080 = 1920×1080 when it's 16:9)
    double fps = 0.0;       // 0 = same as the first video

    struct Shape {
        QString id;
        QString name;      // in the settings window
        QString shortName; // under the preview, where there's not much room
    };
    static QList<Shape> shapes()
    {
        return {
            { "auto", "Match the first video", "Auto" },
            { "16:9", "16:9 widescreen (YouTube)", "16:9 wide" },
            { "9:16", "9:16 tall (Shorts, TikTok, Reels)", "9:16 tall" },
            { "1:1", "1:1 square", "1:1 square" },
            { "4:5", "4:5 portrait (Instagram)", "4:5 portrait" },
            { "4:3", "4:3 classic", "4:3" },
            { "21:9", "21:9 cinema", "21:9 cinema" },
        };
    }
    static QList<int> resolutions() { return { 720, 1080, 1440, 2160 }; }

    // The real size, given how big the first video is (for "auto")
    QSize sizeFor(QSize firstVideo) const
    {
        int a = 0, b = 0;
        if (shape == "auto" || std::sscanf(shape.toUtf8().constData(), "%d:%d", &a, &b) != 2 || a <= 0 || b <= 0)
            return firstVideo;
        auto even = [](double v) { return std::max(2, int(std::lround(v)) & ~1); };
        return a >= b ? QSize(even(resolution * double(a) / b), even(resolution))
                      : QSize(even(resolution), even(resolution * double(b) / a));
    }

    bool operator==(const ProjectSettings&) const = default;
};
