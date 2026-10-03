// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <ve/engine.h>

#include <QString>

// Friendly names for the engine's transitions and animations (same order as VE_TRANSITION_* / VE_ANIM_*)
inline QString transitionName(int type)
{
    static const char* names[VE_TRANSITION_COUNT] = {
        "None", "Dissolve", "Fade through black", "Wipe left", "Wipe right",
        "Wipe up", "Wipe down", "Slide left", "Slide right", "Zoom",
    };
    return (type >= 0 && type < VE_TRANSITION_COUNT) ? QString(names[type]) : QString();
}

inline QString animationName(int type)
{
    static const char* names[VE_ANIM_COUNT] = {
        "None", "Fade", "Slide from the left", "Slide from the right",
        "Slide from the top", "Slide from the bottom", "Zoom (pop)", "Wipe (reveal)",
    };
    return (type >= 0 && type < VE_ANIM_COUNT) ? QString(names[type]) : QString();
}
