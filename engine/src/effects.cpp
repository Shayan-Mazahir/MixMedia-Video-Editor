// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "effects.h"

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

void applyChromaKey(uint8_t* pixels, int w, int h, const ChromaKey& key, bool bgra)
{
    // Compare colours by their tint only (blue-ness and red-ness, ignoring brightness), so
    // shadows and bright patches on the screen still count as the screen
    auto tint = [](float r, float g, float b, float& cb, float& cr) {
        cb = -0.1146f * r - 0.3854f * g + 0.5f * b;
        cr = 0.5f * r - 0.4542f * g - 0.0458f * b;
    };
    float keyCb, keyCr;
    tint(key.r, key.g, key.b, keyCb, keyCr);
    const float reach = std::max(1.0f, std::hypot(keyCb, keyCr)); // how far the key is from grey
    const float inner = std::clamp(key.strength, 0.0f, 1.0f) * reach;
    const float edge = std::max(1.0f, std::clamp(key.softness, 0.0f, 1.0f) * reach);
    // Spill: green (or blue) light bouncing onto the subject. Pull that channel down to the others.
    const int spillChannel = key.g >= key.r && key.g >= key.b ? 1 : (key.b >= key.r ? 2 : -1);
    const float spill = std::clamp(key.spill, 0.0f, 1.0f);
    const int R = bgra ? 2 : 0, B = bgra ? 0 : 2;

    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            uint8_t* p = pixels + size_t(y) * w * 4;
            for (int x = 0; x < w; ++x, p += 4) {
                float cb, cr;
                tint(p[R], p[1], p[B], cb, cr);
                float d = std::hypot(cb - keyCb, cr - keyCr);
                float keep = std::clamp((d - inner) / edge, 0.0f, 1.0f);
                p[3] = uint8_t(p[3] * keep);
                if (spillChannel >= 0 && spill > 0 && keep > 0) {
                    int c = spillChannel == 1 ? 1 : B;
                    int others = spillChannel == 1 ? std::max(p[R], p[B]) : std::max(p[R], p[1]);
                    if (p[c] > others)
                        p[c] = uint8_t(p[c] - (p[c] - others) * spill);
                }
            }
        }
    });
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

bool Effects::any() const
{
    return look != Look::None || brightness != 0 || contrast != 0 || saturation != 0 || temperature != 0
           || blur != 0 || sharpen != 0 || vignette != 0;
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
            blur(pixels, w, h, radius, 3, scratch);
    }
    if (fx.sharpen > 0)
        applySharpen(pixels, w, h, fx.sharpen, scratch);

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
