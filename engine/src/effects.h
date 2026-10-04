// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include <algorithm>
#include <cstdint>
#include <thread>
#include <vector>

namespace ve {

// How many CPU threads we're allowed to use (0 = decide ourselves). Set from ve_set_thread_limit.
int threadLimit();
void setThreadLimit(int threads);
// Threads for splitting up picture work: half the cores (up to 8), or whatever the limit says
int workerThreads();

// Splits the rows of a picture between a few threads. Small jobs just run here.
template <typename Fn>
void parallelRows(int rows, Fn&& fn)
{
    const int threads = workerThreads();
    if (threads == 1 || rows < 128) {
        fn(0, rows);
        return;
    }
    std::vector<std::thread> pool;
    int chunk = (rows + threads - 1) / threads;
    for (int a = 0; a < rows; a += chunk)
        pool.emplace_back([&fn, a, b = std::min(rows, a + chunk)] { fn(a, b); });
    for (std::thread& t : pool)
        t.join();
}

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

// Green screen: makes everything close to one colour see-through
struct ChromaKey {
    bool on = false;
    uint8_t r = 0, g = 255, b = 0; // the colour to remove
    float strength = 0.4f;         // 0..1, how far from that colour still counts
    float softness = 0.2f;         // 0..1, how gradual the edge is
    float spill = 0.5f;            // 0..1, how much of the colour's glow to take off what's left
};
void applyChromaKey(uint8_t* pixels, int w, int h, const ChromaKey& key, bool bgra);

// Applies the effects to a w x h picture in place (4 bytes per pixel, RGBA or BGRA).
void applyEffects(uint8_t* pixels, int w, int h, const Effects& fx, bool bgra, EffectsScratch& scratch);

} // namespace ve
