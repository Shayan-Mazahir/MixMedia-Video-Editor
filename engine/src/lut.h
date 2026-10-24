// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

// Looks from .cube files ("LUTs"): a table saying what every colour becomes, the way colour
// grading programs share their looks.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ve {

struct Lut {
    int size = 0;           // the table is size x size x size
    std::vector<float> rgb; // red changes fastest, then green, then blue; 0..1
    float domainMin[3] = { 0, 0, 0 }, domainMax[3] = { 1, 1, 1 };
};

// Reads a .cube file (kept, so each one's only read once). Null if it can't be read.
std::shared_ptr<const Lut> loadLut(const std::string& path);
// Parses .cube text. False (with why) if it isn't one.
bool parseCube(const std::string& text, Lut& out, std::string* why = nullptr);

// Changes every pixel through the table; amount 0..1 mixes it with the original
void applyLut(uint8_t* pixels, int w, int h, const Lut& lut, float amount, bool bgra);

} // namespace ve
