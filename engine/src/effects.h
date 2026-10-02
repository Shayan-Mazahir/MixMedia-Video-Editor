// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <cstdint>
#include <vector>

namespace ve {

// One-click looks, like Filmora's filters
enum class Look {
    None,
    BlackAndWhite,
    Sepia,
    Vintage,
    Vivid,
    Cool,
    Warm,
    Faded,
    Dramatic,
};

// Everything that changes how a clip's picture looks. All zeros = untouched.
struct Effects {
    Look look = Look::None;
    float brightness = 0;  // -1..1
    float contrast = 0;    // -1..1  (0 = normal)
    float saturation = 0;  // -1..1  (-1 = grey, 1 = twice as colourful)
    float temperature = 0; // -1..1  (negative = cooler/bluer, positive = warmer/orange)
    float blur = 0;        //  0..1
    float sharpen = 0;     //  0..1
    float vignette = 0;    //  0..1  (darker corners)

    bool any() const;
};

// Scratch space for the effects, so we don't allocate every frame
struct EffectsScratch {
    std::vector<uint8_t> temp;
    std::vector<uint8_t> blurred;
    std::vector<uint8_t> vignetteMask;
    int maskW = 0, maskH = 0;
    float maskAmount = -1;
};

// Applies the effects to a w x h picture in place (4 bytes per pixel, RGBA or BGRA).
void applyEffects(uint8_t* pixels, int w, int h, const Effects& fx, bool bgra, EffectsScratch& scratch);

} // namespace ve
