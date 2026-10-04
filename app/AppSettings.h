// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <ve/engine.h>

#include <QSettings>

#include <algorithm>

// App-wide settings (not saved in projects): how much of the computer MixMedia may use
namespace AppSettings {

// Off = MixMedia decides everything itself (the default)
inline bool advancedPerformance() { return QSettings().value("performance/advanced", false).toBool(); }

// CPU threads to use when advanced is on
inline int threads()
{
    return std::clamp(QSettings().value("performance/threads", ve_cpu_threads()).toInt(), 1, ve_cpu_threads());
}

inline bool gpuForExport() { return !advancedPerformance() || QSettings().value("performance/gpuExport", true).toBool(); }
inline bool gpuForCaptions() { return !advancedPerformance() || QSettings().value("performance/gpuCaptions", true).toBool(); }

// Threads for jobs like auto-captions (all of them, or the limit)
inline int jobThreads() { return advancedPerformance() ? threads() : ve_cpu_threads(); }

// Tells the engine (call at startup and whenever the settings change)
inline void apply() { ve_set_thread_limit(advancedPerformance() ? threads() : 0); }

} // namespace AppSettings
