// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "audio_fx.h"

#include <algorithm>
#include <cmath>

namespace ve {

namespace {

constexpr int FrameSize = 1024; // ~21ms: short enough to follow speech, long enough to tell hum from voice
constexpr int FrameHop = FrameSize / 2;
constexpr int Bins = FrameSize / 2 + 1;
constexpr float Pi = 3.14159265f;

// A plain radix-2 FFT. `inverse` goes back the other way (without the 1/n, that's done by the caller).
void fft(std::vector<std::complex<float>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = 2 * Pi / float(len) * (inverse ? 1 : -1);
        std::complex<float> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

// Square root of a Hann window: used going in and coming out, the two multiply back to a
// plain Hann, and those overlap-add to exactly 1
const std::vector<float>& sqrtHann()
{
    static const std::vector<float> w = [] {
        std::vector<float> v(FrameSize);
        for (int i = 0; i < FrameSize; ++i)
            v[size_t(i)] = std::sqrt(0.5f - 0.5f * std::cos(2 * Pi * i / FrameSize));
        return v;
    }();
    return w;
}

// Reads `count` samples from `from` (before the start of the file = silence)
void readAt(AudioReader& reader, int64_t from, int count, float* out)
{
    std::fill(out, out + size_t(count) * AudioChannels, 0.0f);
    if (from + count <= 0)
        return;
    int skip = from < 0 ? int(-from) : 0;
    reader.read(double(from + skip) / AudioRate, count - skip, out + size_t(skip) * AudioChannels);
}

} // namespace

// ---- Noise ----

NoiseProfile learnNoise(const std::string& path, double from, double length)
{
    // Listen to slices spread across the clip and, for each frequency, note how loud it is in
    // the quieter moments. Steady noise is there even then; voices and music mostly aren't.
    AudioReader reader;
    NoiseProfile profile(Bins, 0.0f);
    if (!reader.open(path) || length <= 0)
        return profile;
    const int slices = int(std::clamp(length / 2.0, 1.0, 24.0));
    const int framesPerSlice = 24;
    std::vector<std::vector<float>> mags(Bins);
    std::vector<float> buf(size_t(FrameSize + FrameHop * framesPerSlice) * AudioChannels);
    std::vector<std::complex<float>> spec(FrameSize);
    for (int s = 0; s < slices; ++s) {
        double at = from + length * (s + 0.5) / slices - 0.25;
        reader.read(std::max(0.0, at), int(buf.size() / AudioChannels), buf.data());
        for (int f = 0; f < framesPerSlice; ++f) {
            for (int i = 0; i < FrameSize; ++i) {
                size_t k = size_t(f * FrameHop + i) * AudioChannels;
                spec[size_t(i)] = 0.5f * (buf[k] + buf[k + 1]) * sqrtHann()[size_t(i)];
            }
            fft(spec, false);
            for (int b = 0; b < Bins; ++b)
                mags[size_t(b)].push_back(std::abs(spec[size_t(b)]));
        }
    }
    for (int b = 0; b < Bins; ++b) {
        std::vector<float>& m = mags[size_t(b)];
        if (m.empty())
            continue;
        auto quiet = m.begin() + long(m.size() / 5); // the quietest 20%
        std::nth_element(m.begin(), quiet, m.end());
        profile[size_t(b)] = *quiet;
    }
    return profile;
}

void Denoiser::reset(int64_t at)
{
    // Start a frame early so the first samples asked for come out fully finished
    m_frameStart = at - FrameSize;
    m_outStart = m_frameStart;
    m_outReady = m_frameStart + FrameHop;
    m_out.assign(size_t(FrameSize) * AudioChannels, 0.0f);
    m_in.assign(size_t(FrameSize) * AudioChannels, 0.0f);
    m_gain.assign(Bins, 1.0f);
    m_left.assign(FrameSize, {});
    m_right.assign(FrameSize, {});
    m_valid = true;
}

void Denoiser::processFrame(AudioReader& reader, const NoiseProfile& noise, float strength)
{
    // Slide the input along by half a frame, reading the new half
    std::copy(m_in.begin() + FrameHop * AudioChannels, m_in.end(), m_in.begin());
    readAt(reader, m_frameStart + FrameHop, FrameHop, m_in.data() + size_t(FrameHop) * AudioChannels);

    const std::vector<float>& w = sqrtHann();
    for (int i = 0; i < FrameSize; ++i) {
        m_left[size_t(i)] = m_in[size_t(i) * 2] * w[size_t(i)];
        m_right[size_t(i)] = m_in[size_t(i) * 2 + 1] * w[size_t(i)];
    }
    fft(m_left, false);
    fft(m_right, false);

    // Each frequency: how far above the noise is it? Well above = keep, near it = turn down.
    // (Smoothed over time, or the leftover noise "twinkles".)
    const float floor = 1.0f - 0.94f * strength; // never all the way to silence, that sounds odd
    for (int b = 0; b < Bins; ++b) {
        float mag = 0.5f * (std::abs(m_left[size_t(b)]) + std::abs(m_right[size_t(b)]));
        // (the noise was measured in its quiet moments, so it's usually around twice that)
        float g = mag > 1e-9f ? 1.0f - 2.5f * strength * noise[size_t(b)] / mag : floor;
        g = std::clamp(g, floor, 1.0f);
        g = std::max(g, 0.5f * m_gain[size_t(b)] + 0.5f * g); // quick to open, slower to close
        m_gain[size_t(b)] = g;
        m_left[size_t(b)] *= g;
        m_right[size_t(b)] *= g;
        if (b > 0 && b < Bins - 1) { // the mirror-image half of the spectrum
            m_left[size_t(FrameSize - b)] *= g;
            m_right[size_t(FrameSize - b)] *= g;
        }
    }
    fft(m_left, true);
    fft(m_right, true);

    // Add it into the output, and the first half of this frame is now finished
    const size_t offset = size_t(m_frameStart - m_outStart) * AudioChannels;
    if (m_out.size() < offset + size_t(FrameSize) * AudioChannels)
        m_out.resize(offset + size_t(FrameSize) * AudioChannels, 0.0f);
    for (int i = 0; i < FrameSize; ++i) {
        m_out[offset + size_t(i) * 2] += m_left[size_t(i)].real() * w[size_t(i)] / FrameSize;
        m_out[offset + size_t(i) * 2 + 1] += m_right[size_t(i)].real() * w[size_t(i)] / FrameSize;
    }
    m_frameStart += FrameHop;
    m_outReady = m_frameStart;
}

void Denoiser::read(AudioReader& reader, const NoiseProfile& noise, float strength, int64_t from, int count, float* out)
{
    if (!m_valid || from < m_outStart || from > m_outReady + AudioRate)
        reset(from);
    while (m_outReady < from + count)
        processFrame(reader, noise, strength);
    const size_t offset = size_t(from - m_outStart) * AudioChannels;
    std::copy(m_out.begin() + long(offset), m_out.begin() + long(offset + size_t(count) * AudioChannels), out);
    // Toss what's been handed out
    const size_t used = offset + size_t(count) * AudioChannels;
    m_out.erase(m_out.begin(), m_out.begin() + long(used));
    m_outStart = from + count;
}

// ---- Stretching ----

float TimeStretcher::at(int64_t index, int channel) const
{
    if (index < m_srcStart)
        return 0.0f;
    size_t i = size_t(index - m_srcStart) * AudioChannels + size_t(channel);
    return i < m_src.size() ? m_src[i] : 0.0f;
}

int64_t TimeStretcher::bestStart(int64_t wanted, int64_t natural) const
{
    // Which nudge makes the new slice look most like how the last one would have carried on?
    // Rough search first, then a fine one around the best.
    auto similarity = [&](int64_t start) {
        float sum = 0;
        for (int i = 0; i < Hop; i += 2)
            sum += (at(start + i, 0) + at(start + i, 1)) * (at(natural + i, 0) + at(natural + i, 1));
        return sum;
    };
    int64_t best = wanted;
    float bestScore = -1e30f;
    for (int d = -Tol; d <= Tol; d += 8) {
        float score = similarity(wanted + d);
        if (score > bestScore) {
            bestScore = score;
            best = wanted + d;
        }
    }
    int64_t around = best;
    for (int d = -7; d <= 7; ++d) {
        if (std::abs(around + d - wanted) > Tol)
            continue;
        float score = similarity(around + d);
        if (score > bestScore) {
            bestScore = score;
            best = around + d;
        }
    }
    return best;
}

// ---- Measuring ----

bool measureLoudness(const std::string& path, double from, double length, float* rmsDb, float* peakDb)
{
    AudioReader reader;
    if (!reader.open(path) || length <= 0)
        return false;
    const double slice = std::min(length, 0.5);
    const int slices = int(std::clamp(length / slice, 1.0, 120.0));
    std::vector<float> buf(size_t(slice * AudioRate) * AudioChannels);
    double sumSquares = 0, count = 0;
    float peak = 0;
    for (int s = 0; s < slices; ++s) {
        double at = from + (length - slice) * (slices > 1 ? double(s) / (slices - 1) : 0.0);
        reader.read(at, int(buf.size() / AudioChannels), buf.data());
        for (float v : buf) {
            sumSquares += double(v) * v;
            peak = std::max(peak, std::abs(v));
        }
        count += double(buf.size());
    }
    if (count <= 0)
        return false;
    auto db = [](double v) { return float(20.0 * std::log10(std::max(v, 1e-9))); };
    *rmsDb = db(std::sqrt(sumSquares / count));
    *peakDb = db(peak);
    return true;
}

} // namespace ve
