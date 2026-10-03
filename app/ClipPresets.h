// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "EffectNames.h"
#include "TimelineClip.h"

#include <QList>

// Ready-made titles, effects and transitions for the library panels to hand out.

inline TimelineClip makeTransitionClip(int type, double duration = 1.0)
{
    TimelineClip t;
    t.kind = TimelineClip::Kind::Transition;
    t.transition = type;
    t.name = transitionName(type);
    t.duration = duration;
    return t;
}

inline QList<TimelineClip> transitionPresets()
{
    QList<TimelineClip> out;
    for (int type = 1; type < VE_TRANSITION_COUNT; ++type)
        out << makeTransitionClip(type);
    return out;
}

inline QList<TimelineClip> effectPresets()
{
    auto make = [](const QString& name) {
        TimelineClip e;
        e.kind = TimelineClip::Kind::Effect;
        e.name = name;
        e.duration = 5.0;
        return e;
    };
    QList<TimelineClip> out;
    // The one-click looks
    const char* looks[VE_LOOK_COUNT] = { "", "Black & white", "Sepia", "Vintage", "Vivid", "Cool", "Warm", "Faded", "Dramatic" };
    for (int look = 1; look < VE_LOOK_COUNT; ++look) {
        TimelineClip e = make(looks[look]);
        e.look = look;
        out << e;
    }
    // Single effects, set to a nice middle amount
    TimelineClip blur = make("Blur");
    blur.blur = 0.4f;
    TimelineClip sharpen = make("Sharpen");
    sharpen.sharpen = 0.5f;
    TimelineClip vignette = make("Vignette");
    vignette.vignette = 0.6f;
    TimelineClip brighter = make("Brighter");
    brighter.brightness = 0.3f;
    TimelineClip darker = make("Darker");
    darker.brightness = -0.3f;
    TimelineClip adjust = make("Colour adjust"); // blank: dial it in yourself
    out << blur << sharpen << vignette << brighter << darker << adjust;
    return out;
}

inline QList<TimelineClip> titlePresets()
{
    auto make = [](const QString& name, const QString& text) {
        TimelineClip t;
        t.kind = TimelineClip::Kind::Title;
        t.name = name;
        t.title.text = text;
        t.duration = 5.0;
        t.hasVideo = true;
        return t;
    };
    TimelineClip title = make("Title", "Your title");
    title.title.size = 12;
    title.title.y = 0.5;
    title.title.box = false;
    title.animIn = VE_ANIM_FADE;
    title.animOut = VE_ANIM_FADE;

    TimelineClip lower = make("Lower third", "Name\nWhat they do");
    lower.title.size = 5;
    lower.title.y = 0.82;
    lower.animIn = VE_ANIM_SLIDE_LEFT;
    lower.animOut = VE_ANIM_SLIDE_LEFT;

    TimelineClip subtitle = make("Subtitle", "What's being said");
    subtitle.title.size = 4.5;
    subtitle.title.y = 0.9;
    subtitle.title.bold = false;

    TimelineClip big = make("Big & bold", "WOW");
    big.title.size = 22;
    big.title.y = 0.5;
    big.title.box = false;
    big.title.color = QColor(0xff, 0xd2, 0x3f);
    big.animIn = VE_ANIM_ZOOM;

    TimelineClip reveal = make("Reveal", "Chapter one");
    reveal.title.size = 9;
    reveal.title.y = 0.5;
    reveal.animIn = VE_ANIM_WIPE;
    reveal.animInDuration = 1.0;
    reveal.animOut = VE_ANIM_FADE;

    return { title, lower, subtitle, big, reveal };
}
