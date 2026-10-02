// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#if defined(__linux__)
#include <pthread.h>
#endif

// Names the current thread, so it shows up nicely in system monitors and debuggers
inline void nameThisThread(const char* name)
{
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name); // max 15 characters
#else
    (void)name;
#endif
}
