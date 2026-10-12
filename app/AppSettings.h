// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "AppPaths.h"

#include <ve/engine.h>

#include <QSettings>

#include <algorithm>

// App-wide settings (not saved in projects): how much of the computer MixMedia may use
namespace AppSettings {

// Off = MixMedia decides everything itself (the default)
inline bool advancedPerformance() { return AppPaths::settings().value("performance/advanced", false).toBool(); }

// CPU threads to use when advanced is on
inline int threads()
{
    return std::clamp(AppPaths::settings().value("performance/threads", ve_cpu_threads()).toInt(), 1, ve_cpu_threads());
}

inline bool gpuForExport() { return !advancedPerformance() || AppPaths::settings().value("performance/gpuExport", true).toBool(); }
inline bool gpuForCaptions() { return !advancedPerformance() || AppPaths::settings().value("performance/gpuCaptions", true).toBool(); }

// Threads for jobs like auto-captions (the limit, or 0 = let the engine pick)
inline int jobThreads() { return advancedPerformance() ? threads() : 0; }

// Tells the engine (call at startup and whenever the settings change)
inline void apply() { ve_set_thread_limit(advancedPerformance() ? threads() : 0); }

} // namespace AppSettings
