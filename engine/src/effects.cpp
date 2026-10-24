// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "effects.h"
#include "lut.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

namespace ve {

namespace {
std::atomic<int> g_threadLimit { 0 };
}

void setThreadLimit(int threads) { g_threadLimit = std::max(0, threads); }
int threadLimit() { return g_threadLimit; }

int workerThreads()
{
    int limit = g_threadLimit;
    return limit > 0 ? std::clamp(limit, 1, 32) : std::clamp(int(std::thread::hardware_concurrency()) / 2, 1, 8);
}

namespace {

// A colour recipe: new RGB = M * old RGB + offset. Every colour effect is one of these,
// and we squash them all into a single one so each pixel only gets touched once.
struct ColorMatrix {
    float m[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };

    // `this` happens after `first`
    ColorMatrix after(const ColorMatrix& first) const
    {
        ColorMatrix out;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                out.m[r][c] = m[r][0] * first.m[0][c] + m[r][1] * first.m[1][c] + m[r][2] * first.m[2][c];
            out.m[r][3] = m[r][0] * first.m[0][3] + m[r][1] * first.m[1][3] + m[r][2] * first.m[2][3] + m[r][3];
        }
        return out;
    }

    bool isIdentity() const
    {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                if (std::abs(m[r][c] - (r == c ? 1.0f : 0.0f)) > 1e-4f)
                    return false;
        return true;
    }
};

ColorMatrix saturationMatrix(float s) // 0 = grey, 1 = as is
{
    const float lr = 0.2126f, lg = 0.7152f, lb = 0.0722f; // how bright each colour looks to us
    ColorMatrix c;
    float rows[3][3] = {
        { lr + (1 - lr) * s, lg - lg * s, lb - lb * s },
        { lr - lr * s, lg + (1 - lg) * s, lb - lb * s },
        { lr - lr * s, lg - lg * s, lb + (1 - lb) * s },
    };
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            c.m[r][k] = rows[r][k];
    return c;
}

ColorMatrix contrastMatrix(float c, float brightness) // stretches around the middle grey
{
    ColorMatrix out;
    for (int r = 0; r < 3; ++r) {
        out.m[r][r] = c;
        out.m[r][3] = 0.5f * (1 - c) + brightness;
    }
    return out;
}

ColorMatrix temperatureMatrix(float t) // warm = more red, less blue
{
    ColorMatrix out;
    out.m[0][0] = 1 + 0.18f * t;
    out.m[1][1] = 1 + 0.04f * t;
    out.m[2][2] = 1 - 0.18f * t;
    return out;
}

ColorMatrix sepiaMatrix(float amount)
{
    const float s[3][3] = { { 0.393f, 0.769f, 0.189f }, { 0.349f, 0.686f, 0.168f }, { 0.272f, 0.534f, 0.131f } };
    ColorMatrix out;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out.m[r][c] = (1 - amount) * (r == c ? 1.0f : 0.0f) + amount * s[r][c];
    return out;
}

// Each look is really just some slider settings (plus sepia for the old-timey ones)
struct LookRecipe {
    float sepia = 0, brightness = 0, contrast = 0, saturation = 0, temperature = 0, vignette = 0;
};

LookRecipe recipeFor(Look look)
{
    switch (look) {
    case Look::BlackAndWhite: return { .contrast = 0.1f, .saturation = -1 };
    case Look::Sepia: return { .sepia = 1 };
    case Look::Vintage: return { .sepia = 0.5f, .brightness = 0.03f, .contrast = -0.1f, .vignette = 0.35f };
    case Look::Vivid: return { .contrast = 0.12f, .saturation = 0.45f };
    case Look::Cool: return { .temperature = -0.5f };
    case Look::Warm: return { .temperature = 0.5f };
    case Look::Faded: return { .brightness = 0.06f, .contrast = -0.25f, .saturation = -0.3f };
    case Look::Dramatic: return { .contrast = 0.35f, .saturation = -0.15f, .vignette = 0.3f };
    case Look::None: break;
    }
    return {};
}

// ---- Green screen ----

} // namespace

namespace {

// The edge clean-up: pulls the kept part in by `in` pixels (so the fringe of screen colour round
// it goes), then softens it by `soft` pixels (so it isn't jagged). Works on just the alpha.
void cleanEdges(uint8_t* pixels, int w, int h, int in, int soft, EffectsScratch& scratch)
{
    const size_t n = size_t(w) * h;
    scratch.alpha.resize(n);
    scratch.alpha2.resize(n);
    uint8_t* a = scratch.alpha.data();
    uint8_t* b = scratch.alpha2.data();
    for (size_t i = 0; i < n; ++i)
        a[i] = pixels[i * 4 + 3];
    // Pull in: each pixel takes the smallest alpha around it (across, then down)
    if (in > 0) {
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x) {
                    uint8_t m = 255;
                    for (int k = std::max(0, x - in); k <= std::min(w - 1, x + in); ++k)
                        m = std::min(m, a[size_t(y) * w + k]);
                    b[size_t(y) * w + x] = m;
                }
        });
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x) {
                    uint8_t m = 255;
                    for (int k = std::max(0, y - in); k <= std::min(h - 1, y + in); ++k)
                        m = std::min(m, b[size_t(k) * w + x]);
                    a[size_t(y) * w + x] = m;
                }
        });
    }
    // Soften: a little blur on the alpha only (a box blur across and down, twice)
    for (int pass = 0; pass < 2 && soft > 0; ++pass) {
        const int span = 2 * soft + 1;
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const uint8_t* row = a + size_t(y) * w;
                int sum = 0;
                for (int k = -soft; k <= soft; ++k)
                    sum += row[std::clamp(k, 0, w - 1)];
                for (int x = 0; x < w; ++x) {
                    b[size_t(y) * w + x] = uint8_t(sum / span);
                    sum += row[std::min(x + soft + 1, w - 1)] - row[std::max(x - soft, 0)];
                }
            }
        });
        parallelRows(w, [&](int x0, int x1) {
            for (int x = x0; x < x1; ++x) {
                int sum = 0;
                for (int k = -soft; k <= soft; ++k)
                    sum += b[size_t(std::clamp(k, 0, h - 1)) * w + x];
                for (int y = 0; y < h; ++y) {
                    a[size_t(y) * w + x] = uint8_t(sum / span);
                    sum += b[size_t(std::min(y + soft + 1, h - 1)) * w + x] - b[size_t(std::max(y - soft, 0)) * w + x];
                }
            }
        });
    }
    // (never more solid than it was: softening only ever takes away)
    for (size_t i = 0; i < n; ++i)
        pixels[i * 4 + 3] = std::min(pixels[i * 4 + 3], a[i]);
}

} // namespace

void applyChromaKey(uint8_t* pixels, int w, int h, const ChromaKey& key, bool bgra, EffectsScratch& scratch)
{
    const int R = bgra ? 2 : 0, B = bgra ? 0 : 2;
    // A colour's "tint": which way it leans away from grey, and how far, compared to how bright it
    // is. The same in a screen's shadows as in its bright patches, so uneven lighting doesn't matter.
    auto tint = [](float r, float g, float b, float out[3]) {
        const float mean = (r + g + b) / 3.0f, top = std::max({ r, g, b, 24.0f });
        out[0] = (r - mean) / top;
        out[1] = (g - mean) / top;
        out[2] = (b - mean) / top;
    };
    float keyTint[3];
    tint(key.r, key.g, key.b, keyTint);
    const float keyStrength2 = keyTint[0] * keyTint[0] + keyTint[1] * keyTint[1] + keyTint[2] * keyTint[2];
    const float keyLength = std::sqrt(keyStrength2);
    // Starts going see-through this much of the way towards the screen, completely gone a bit after
    const float from = 1.0f - std::clamp(key.strength, 0.0f, 1.0f) * 0.9f;
    const float span = 0.02f + std::clamp(key.softness, 0.0f, 1.0f) * 0.6f;
    // (50% on the slider already takes most of the glow off: edges are where it shows most)
    const float spill = std::min(1.0f, std::clamp(key.spill, 0.0f, 1.0f) * 1.6f);

    if (keyLength > 0.12f) {
        // A colourful screen (green, blue, red, purple, yellow...): how far each pixel leans the
        // same way as the screen, minus how much it leans some other way (so skin on a red screen,
        // or a red shirt on a purple one, doesn't go with it). 1 = as much as the screen itself.
        const float dir[3] = { keyTint[0] / keyLength, keyTint[1] / keyLength, keyTint[2] / keyLength };
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                uint8_t* p = pixels + size_t(y) * w * 4;
                for (int x = 0; x < w; ++x, p += 4) {
                    float c[3];
                    tint(p[R], p[1], p[B], c);
                    const float along = c[0] * dir[0] + c[1] * dir[1] + c[2] * dir[2];
                    const float off[3] = { c[0] - along * dir[0], c[1] - along * dir[1], c[2] - along * dir[2] };
                    const float sideways = std::sqrt(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
                    const float t = (along - 0.8f * sideways) / keyLength;
                    const float gone = std::clamp((t - from) / span, 0.0f, 1.0f);
                    p[3] = uint8_t(p[3] * (1.0f - gone));
                    // The screen's glow on what's left: take that tint back out (keeping its brightness).
                    // Only near the screen's colour (edges, hair): further in, a colour that just
                    // happens to lean the same way (skin on a red or yellow screen) is left alone.
                    // (eases in from a quarter of the way to the screen's colour, full strength by the edge)
                    const float lean = std::clamp((t - 0.25f * from) / (0.75f * from), 0.0f, 1.0f);
                    const float near = lean * lean * (3 - 2 * lean);
                    if (spill > 0 && p[3] > 0 && near > 0) {
                        const float mean = (p[R] + p[1] + p[B]) / 3.0f;
                        const float cast = (p[R] - mean) * dir[0] + (p[1] - mean) * dir[1] + (p[B] - mean) * dir[2];
                        if (cast > 0) {
                            const int idx[3] = { R, 1, B };
                            for (int k = 0; k < 3; ++k)
                                p[idx[k]] = uint8_t(std::clamp(p[idx[k]] - cast * spill * near * dir[k], 0.0f, 255.0f));
                        }
                    }
                }
            }
        });
    } else {
        // A white, black or grey screen: no tint to go by, so by how close the colour (and brightness) is
        const float reach = 0.02f + std::clamp(key.strength, 0.0f, 1.0f) * 0.5f;
        const float soft = 0.02f + std::clamp(key.softness, 0.0f, 1.0f) * 0.3f;
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                uint8_t* p = pixels + size_t(y) * w * 4;
                for (int x = 0; x < w; ++x, p += 4) {
                    const float dr = p[R] - key.r, dg = p[1] - key.g, db = p[B] - key.b;
                    const float d = std::sqrt(dr * dr + dg * dg + db * db) / 441.7f; // (0..1, 441.7 = all the way)
                    const float gone = std::clamp((reach - d) / soft, 0.0f, 1.0f);
                    p[3] = uint8_t(p[3] * (1.0f - gone));
                }
            }
        });
    }
    const int size = std::max(w, h);
    const float edges = std::clamp(key.edges, 0.0f, 1.0f);
    if (edges > 0)
        // Pulls in by about 1-2 pixels at the usual 30% (where the person and screen blend: the
        // outline), more when turned up. Grows faster than the slider, so low settings keep fine hair.
        cleanEdges(pixels, w, h, int(std::lround(edges * edges * 0.012 * size)), std::max(1, int(std::lround(edges * 0.003 * size))), scratch);
    // Anything partly see-through now is right on the edge, where the person and the screen
    // blend: take the screen's tint right out of those, so no coloured outline's left
    if (keyLength > 0.12f && spill > 0) {
        const float dir[3] = { keyTint[0] / keyLength, keyTint[1] / keyLength, keyTint[2] / keyLength };
        const int idx[3] = { R, 1, B };
        parallelRows(h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                uint8_t* p = pixels + size_t(y) * w * 4;
                for (int x = 0; x < w; ++x, p += 4) {
                    if (p[3] == 0 || p[3] >= 250)
                        continue;
                    const float mean = (p[R] + p[1] + p[B]) / 3.0f;
                    const float cast = (p[R] - mean) * dir[0] + (p[1] - mean) * dir[1] + (p[B] - mean) * dir[2];
                    if (cast > 0)
                        for (int k = 0; k < 3; ++k)
                            p[idx[k]] = uint8_t(std::clamp(p[idx[k]] - cast * dir[k], 0.0f, 255.0f));
                }
            }
        });
    }
    if (key.matte) {
        // Black = gone, white = kept
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            uint8_t* p = pixels + i * 4;
            p[0] = p[1] = p[2] = p[3];
            p[3] = 255;
        }
    }
}

namespace {

// ---- Blur: three quick box blurs in a row look just like a proper (slow) Gaussian blur ----

void boxBlurRows(const uint8_t* src, uint8_t* dst, int w, int y0, int y1, int r)
{
    // Dividing is slow, so multiply by 1/(2r+1) in fixed point instead
    const int inv = (1 << 16) / (2 * r + 1);
    for (int y = y0; y < y1; ++y) {
        const uint8_t* s = src + size_t(y) * w * 4;
        uint8_t* d = dst + size_t(y) * w * 4;
        int sum[4] = {};
        for (int k = -r; k <= r; ++k) {
            int x = std::clamp(k, 0, w - 1);
            for (int c = 0; c < 4; ++c)
                sum[c] += s[x * 4 + c];
        }
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c)
                d[x * 4 + c] = uint8_t((sum[c] * inv + 32768) >> 16);
            int add = std::min(x + r + 1, w - 1), sub = std::max(x - r, 0);
            for (int c = 0; c < 4; ++c)
                sum[c] += s[add * 4 + c] - s[sub * 4 + c];
        }
    }
}

void boxBlurColumns(const uint8_t* src, uint8_t* dst, int w, int h, int x0, int x1, int r)
{
    // Walks down row by row with a running total per column, which keeps the memory access tidy.
    // Each thread takes its own band of columns [x0, x1).
    const int inv = (1 << 16) / (2 * r + 1);
    const size_t stride = size_t(w) * 4;
    const size_t from = size_t(x0) * 4, to = size_t(x1) * 4;
    std::vector<int> sum(to - from, 0);
    for (int k = -r; k <= r; ++k) {
        const uint8_t* row = src + size_t(std::clamp(k, 0, h - 1)) * stride;
        for (size_t i = from; i < to; ++i)
            sum[i - from] += row[i];
    }
    for (int y = 0; y < h; ++y) {
        uint8_t* d = dst + size_t(y) * stride;
        for (size_t i = from; i < to; ++i)
            d[i] = uint8_t((sum[i - from] * inv + 32768) >> 16);
        const uint8_t* add = src + size_t(std::min(y + r + 1, h - 1)) * stride;
        const uint8_t* sub = src + size_t(std::max(y - r, 0)) * stride;
        for (size_t i = from; i < to; ++i)
            sum[i - from] += add[i] - sub[i];
    }
}

void blur(uint8_t* pixels, int w, int h, int radius, int passes, EffectsScratch& scratch)
{
    scratch.temp.resize(size_t(w) * h * 4);
    for (int i = 0; i < passes; ++i) {
        uint8_t* temp = scratch.temp.data();
        parallelRows(h, [&](int a, int b) { boxBlurRows(pixels, temp, w, a, b, radius); });
        parallelRows(w, [&](int a, int b) { boxBlurColumns(temp, pixels, w, h, a, b, radius); });
    }
}

// A big blur looks just the same done on a shrunk copy and scaled back up (there's no fine
// detail left to lose), and that's a fraction of the work
void bigBlur(uint8_t* pixels, int w, int h, int radius, int passes, EffectsScratch& scratch)
{
    const int f = radius >= 10 ? 4 : radius >= 4 ? 2 : 1;
    if (f == 1 || w < f * 8 || h < f * 8) {
        blur(pixels, w, h, radius, passes, scratch);
        return;
    }
    const int sw = (w + f - 1) / f, sh = (h + f - 1) / f;
    scratch.small.resize(size_t(sw) * sh * 4);
    uint8_t* small = scratch.small.data();
    // Shrink: each small pixel is the average of an f x f block (the ragged edge, if the size
    // doesn't divide evenly, just repeats the last row and column)
    const int shift = f == 4 ? 4 : 2; // (dividing by f*f)
    parallelRows(sh, [&](int y0, int y1) {
        for (int sy = y0; sy < y1; ++sy) {
            const uint8_t* rows[4];
            for (int k = 0; k < f; ++k)
                rows[k] = pixels + size_t(std::min(h - 1, sy * f + k)) * w * 4;
            uint8_t* d = small + size_t(sy) * sw * 4;
            for (int sx = 0; sx < sw; ++sx, d += 4) {
                int sum[4] = {};
                for (int k = 0; k < f; ++k)
                    for (int j = 0; j < f; ++j) {
                        const uint8_t* p = rows[k] + size_t(std::min(w - 1, sx * f + j)) * 4;
                        sum[0] += p[0], sum[1] += p[1], sum[2] += p[2], sum[3] += p[3];
                    }
                for (int c = 0; c < 4; ++c)
                    d[c] = uint8_t(sum[c] >> shift);
            }
        }
    });
    blur(small, sw, sh, std::max(1, radius / f), passes, scratch);
    // Back up to full size, smoothly (bilinear, in fixed point). Which two small pixels each
    // column sits between is the same on every row, so that's worked out once.
    std::vector<int> left(static_cast<size_t>(w)), right(static_cast<size_t>(w)), across(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) {
        const double fx = std::clamp((x + 0.5) / f - 0.5, 0.0, sw - 1.0);
        left[size_t(x)] = int(fx) * 4;
        right[size_t(x)] = std::min(int(fx) + 1, sw - 1) * 4;
        across[size_t(x)] = int((fx - int(fx)) * 256);
    }
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const double fy = std::clamp((y + 0.5) / f - 0.5, 0.0, sh - 1.0);
            const int ty = int(fy), by = std::min(ty + 1, sh - 1), wy = int((fy - ty) * 256);
            const uint8_t* top = small + size_t(ty) * sw * 4;
            const uint8_t* bottom = small + size_t(by) * sw * 4;
            uint8_t* d = pixels + size_t(y) * w * 4;
            for (int x = 0; x < w; ++x, d += 4) {
                const int l = left[size_t(x)], r = right[size_t(x)], wx = across[size_t(x)];
                for (int c = 0; c < 4; ++c) {
                    const int t = top[l + c] * (256 - wx) + top[r + c] * wx;
                    const int b = bottom[l + c] * (256 - wx) + bottom[r + c] * wx;
                    d[c] = uint8_t((t * (256 - wy) + b * wy + 32768) >> 16);
                }
            }
        }
    });
}

void applyColor(uint8_t* pixels, int w, int h, const ColorMatrix& cm, bool bgra)
{
    // Fixed-point maths (x4096) is a lot quicker than floats per pixel
    int m[3][4];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            m[r][c] = int(std::lround(cm.m[r][c] * 4096));
        m[r][3] = int(std::lround(cm.m[r][3] * 255 * 4096));
    }
    const int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;
    parallelRows(h, [&](int y0, int y1) {
        uint8_t* p = pixels + size_t(y0) * w * 4;
        uint8_t* end = pixels + size_t(y1) * w * 4;
        for (; p < end; p += 4) {
            int r = p[ri], g = p[1], b = p[bi];
            int nr = (m[0][0] * r + m[0][1] * g + m[0][2] * b + m[0][3]) >> 12;
            int ng = (m[1][0] * r + m[1][1] * g + m[1][2] * b + m[1][3]) >> 12;
            int nb = (m[2][0] * r + m[2][1] * g + m[2][2] * b + m[2][3]) >> 12;
            p[ri] = uint8_t(std::clamp(nr, 0, 255));
            p[1] = uint8_t(std::clamp(ng, 0, 255));
            p[bi] = uint8_t(std::clamp(nb, 0, 255));
        }
    });
}

void applyVignette(uint8_t* pixels, int w, int h, float amount, EffectsScratch& scratch)
{
    // The darkening pattern only depends on the size and strength, so work it out once
    if (scratch.maskW != w || scratch.maskH != h || scratch.maskAmount != amount) {
        scratch.vignetteMask.resize(size_t(w) * h);
        const float cx = w / 2.0f, cy = h / 2.0f;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float nx = (x - cx) / cx, ny = (y - cy) / cy;
                float d = std::sqrt(nx * nx + ny * ny) / 1.41421f; // 0 in the middle, 1 in the corners
                float t = std::clamp((d - 0.35f) / 0.65f, 0.0f, 1.0f);
                float smooth = t * t * (3 - 2 * t);
                scratch.vignetteMask[size_t(y) * w + x] = uint8_t(255 * (1 - amount * smooth));
            }
        }
        scratch.maskW = w;
        scratch.maskH = h;
        scratch.maskAmount = amount;
    }
    const uint8_t* mask = scratch.vignetteMask.data();
    parallelRows(h, [&](int y0, int y1) {
        for (size_t i = size_t(y0) * w; i < size_t(y1) * w; ++i) {
            int f = mask[i] + 1; // (x * (f+1)) >> 8 is a quick way to do x * f / 255
            uint8_t* p = pixels + i * 4;
            p[0] = uint8_t((p[0] * f) >> 8);
            p[1] = uint8_t((p[1] * f) >> 8);
            p[2] = uint8_t((p[2] * f) >> 8);
        }
    });
}

void applySharpen(uint8_t* pixels, int w, int h, float amount, EffectsScratch& scratch)
{
    // Sharpening = push each pixel away from its blurry neighbourhood ("unsharp mask")
    int radius = std::max(1, int(std::lround(0.0015 * std::max(w, h))));
    scratch.blurred.assign(pixels, pixels + size_t(w) * h * 4);
    blur(scratch.blurred.data(), w, h, radius, 1, scratch);
    const int k = int(amount * 2.0f * 256);
    const uint8_t* blurred = scratch.blurred.data();
    parallelRows(h, [&](int y0, int y1) {
        for (size_t i = size_t(y0) * w; i < size_t(y1) * w; ++i) {
            uint8_t* p = pixels + i * 4;
            const uint8_t* b = blurred + i * 4;
            for (int c = 0; c < 3; ++c)
                p[c] = uint8_t(std::clamp(p[c] + ((p[c] - b[c]) * k >> 8), 0, 255));
        }
    });
}

} // namespace

void blurPicture(uint8_t* pixels, int w, int h, int radius, EffectsScratch& scratch)
{
    if (radius >= 1 && w > 0 && h > 0)
        bigBlur(pixels, w, h, radius, 3, scratch);
}

bool Effects::any() const
{
    return look != Look::None || brightness != 0 || contrast != 0 || saturation != 0 || temperature != 0
           || blur != 0 || sharpen != 0 || vignette != 0 || (lut && lutAmount > 0);
}

void applyEffects(uint8_t* pixels, int w, int h, const Effects& fx, bool bgra, EffectsScratch& scratch)
{
    if (!fx.any() || w <= 0 || h <= 0)
        return;

    // The look sets a starting point, and the sliders go on top
    LookRecipe look = recipeFor(fx.look);
    float contrast = std::max(0.0f, 1 + look.contrast + fx.contrast);
    float saturation = std::max(0.0f, 1 + look.saturation + fx.saturation);
    float temperature = look.temperature + fx.temperature;
    float brightness = look.brightness + fx.brightness * 0.5f;
    float vignette = std::clamp(look.vignette + fx.vignette, 0.0f, 1.0f);

    // Blur first, so the colour changes apply to the softened picture
    if (fx.blur > 0) {
        int radius = int(std::lround(fx.blur * 0.012 * std::max(w, h)));
        if (radius >= 1)
            bigBlur(pixels, w, h, radius, 3, scratch);
    }
    if (fx.sharpen > 0)
        applySharpen(pixels, w, h, fx.sharpen, scratch);
    // A look from a file goes on before the sliders, so they can tweak it
    if (fx.lut && fx.lutAmount > 0)
        applyLut(pixels, w, h, *fx.lut, fx.lutAmount, bgra);

    ColorMatrix color = contrastMatrix(contrast, brightness)
                            .after(temperatureMatrix(temperature))
                            .after(saturationMatrix(saturation))
                            .after(sepiaMatrix(look.sepia));
    if (!color.isIdentity())
        applyColor(pixels, w, h, color, bgra);

    if (vignette > 0)
        applyVignette(pixels, w, h, vignette, scratch);
}

} // namespace ve
