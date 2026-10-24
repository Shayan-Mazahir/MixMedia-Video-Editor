// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "lut.h"
#include "effects.h" // (parallelRows)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace ve {

bool parseCube(const std::string& text, Lut& out, std::string* why)
{
    auto fail = [&](const char* reason) {
        if (why)
            *why = reason;
        return false;
    };
    out = {};
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (const size_t hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        std::istringstream words(line);
        std::string first;
        if (!(words >> first))
            continue;
        if (first == "TITLE")
            continue;
        if (first == "LUT_1D_SIZE")
            return fail("it's a 1D look (only 3D ones work)");
        if (first == "LUT_3D_SIZE") {
            words >> out.size;
            if (out.size < 2 || out.size > 256)
                return fail("its size doesn't make sense");
            out.rgb.reserve(size_t(out.size) * out.size * out.size * 3);
            continue;
        }
        if (first == "DOMAIN_MIN" || first == "DOMAIN_MAX") {
            float* d = first == "DOMAIN_MIN" ? out.domainMin : out.domainMax;
            words >> d[0] >> d[1] >> d[2];
            continue;
        }
        // Anything else should be a row of three numbers
        char* end = nullptr;
        float r = std::strtof(first.c_str(), &end);
        if (end == first.c_str())
            continue; // (some other keyword we don't need)
        float g = 0, b = 0;
        if (!(words >> g >> b))
            return fail("one of its rows is broken");
        out.rgb.insert(out.rgb.end(), { r, g, b });
    }
    if (out.size == 0)
        return fail("it doesn't say how big it is (LUT_3D_SIZE)");
    if (out.rgb.size() != size_t(out.size) * out.size * out.size * 3)
        return fail("it's missing some of its colours");
    return true;
}

std::shared_ptr<const Lut> loadLut(const std::string& path)
{
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<const Lut>> kept;
    std::lock_guard guard(lock);
    if (auto it = kept.find(path); it != kept.end())
        return it->second;
    std::ifstream file(path, std::ios::binary);
    std::shared_ptr<Lut> lut;
    if (file) {
        std::stringstream all;
        all << file.rdbuf();
        lut = std::make_shared<Lut>();
        if (!parseCube(all.str(), *lut))
            lut.reset();
    }
    kept[path] = lut; // (remembered even if it didn't work, so it isn't tried every frame)
    return lut;
}

void applyLut(uint8_t* pixels, int w, int h, const Lut& lut, float amount, bool bgra)
{
    const int n = lut.size;
    if (n < 2 || amount <= 0)
        return;
    // Where each of the 256 levels of each channel lands in the table: which two entries, and
    // how far between them. Worked out once, so each pixel's just lookups and mixing.
    struct Step {
        int at;     // the lower entry
        float frac; // how far towards the next one
    };
    Step steps[3][256];
    for (int c = 0; c < 3; ++c) {
        const float lo = lut.domainMin[c], span = std::max(1e-6f, lut.domainMax[c] - lo);
        for (int v = 0; v < 256; ++v) {
            const float pos = std::clamp((v / 255.0f - lo) / span, 0.0f, 1.0f) * (n - 1);
            const int at = std::min(int(pos), n - 2);
            steps[c][v] = { at, pos - at };
        }
    }
    const float* t = lut.rgb.data();
    const int R = bgra ? 2 : 0, B = bgra ? 0 : 2;
    const float keep = 1.0f - std::clamp(amount, 0.0f, 1.0f), use = 1.0f - keep;
    parallelRows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            uint8_t* p = pixels + size_t(y) * w * 4;
            for (int x = 0; x < w; ++x, p += 4) {
                const Step sr = steps[0][p[R]], sg = steps[1][p[1]], sb = steps[2][p[B]];
                // The 8 entries around this colour, blended (trilinear)
                const size_t i000 = (size_t(sb.at) * n * n + size_t(sg.at) * n + size_t(sr.at)) * 3;
                const size_t dr = 3, dg = size_t(n) * 3, db = size_t(n) * n * 3;
                float out[3];
                for (int c = 0; c < 3; ++c) {
                    const float c00 = t[i000 + c] + (t[i000 + dr + c] - t[i000 + c]) * sr.frac;
                    const float c10 = t[i000 + dg + c] + (t[i000 + dg + dr + c] - t[i000 + dg + c]) * sr.frac;
                    const float c01 = t[i000 + db + c] + (t[i000 + db + dr + c] - t[i000 + db + c]) * sr.frac;
                    const float c11 = t[i000 + db + dg + c] + (t[i000 + db + dg + dr + c] - t[i000 + db + dg + c]) * sr.frac;
                    const float c0 = c00 + (c10 - c00) * sg.frac, c1 = c01 + (c11 - c01) * sg.frac;
                    out[c] = c0 + (c1 - c0) * sb.frac;
                }
                const int idx[3] = { R, 1, B };
                for (int c = 0; c < 3; ++c)
                    p[idx[c]] = uint8_t(std::clamp(p[idx[c]] * keep + out[c] * 255.0f * use + 0.5f, 0.0f, 255.0f));
            }
        }
    });
}

} // namespace ve
