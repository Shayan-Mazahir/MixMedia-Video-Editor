// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "timeline.h"

// What the C API's ve_timeline handle really is
struct ve_timeline {
    ve::Timeline timeline;
};
