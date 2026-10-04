// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#pragma once

#include "audio_reader.h"

#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace ve {

// Positions here are in samples (48kHz) from the start of the file, and sound is stereo,
// interleaved L R L R. Everything works best read forwards, a bit at a time, like playback does.

// How loud the noise is in each frequency band, learnt from the quiet bits of a file
using NoiseProfile = std::vector<float>;
NoiseProfile learnNoise(const std::string& path, double from, double length);

// Takes steady background noise (hum, hiss, fans) out of a file's sound, as it's read
class Denoiser {
public:
    void read(AudioReader& reader, const NoiseProfile& noise, float strength, int64_t from, int count, float* out);

private:
    void reset(int64_t at);
    void processFrame(AudioReader& reader, const NoiseProfile& noise, float strength);

    std::vector<float> m_in;   // the last frame's worth of input
    std::vector<float> m_out;  // finished (and half-finished) output, starting at m_outStart
    int64_t m_outStart = 0;
    int64_t m_outReady = 0;    // everything before this is finished
    int64_t m_frameStart = 0;  // where the next frame starts
    std::vector<float> m_gain; // last frame's gains, for smoothing
    bool m_valid = false;
    std::vector<std::complex<float>> m_left, m_right;
};

// Plays sound faster or slower without changing its pitch (so voices don't go chipmunk).
// Overlapping slices of the file are laid end to end, each one nudged a little so it lines
// up with the last (that's the "WSOLA" trick), and blended into each other.
class TimeStretcher {
public:
    // `source(from, count, out)` reads the file's sound (plain or cleaned up).
    // Fills `frames` samples of the clip's output, starting `outPos` samples into the clip.
    template <typename Source>
    void render(Source&& source, double inSeconds, double speed, int64_t outPos, int frames, float* out);

private:
    static constexpr int N = 1920;  // slice length (40ms)
    static constexpr int Hop = 960; // half of it: each slice overlaps the next by half
    static constexpr int Tol = 480; // how far a slice can be nudged to line up (10ms)

    template <typename Source>
    void load(Source& source, int64_t from, int64_t to);
    float at(int64_t index, int channel) const;
    int64_t bestStart(int64_t wanted, int64_t natural) const;

    std::vector<float> m_src; // the file's sound from m_srcStart on
    int64_t m_srcStart = 0;
    std::vector<float> m_acc; // output being blended together, from m_accStart on
    int64_t m_accStart = 0;
    int64_t m_nextSlice = 0;
    int64_t m_prevStart = 0;
    bool m_havePrev = false;
    int64_t m_expectOut = -1;
    double m_in = -1, m_speed = -1;
};

// Loudness of the bit of a file that a clip uses: average (RMS) and peak, in dB (0 = as loud as it gets).
// Listens to slices spread across it, so it's quick even for long clips.
bool measureLoudness(const std::string& path, double from, double length, float* rmsDb, float* peakDb);

// ---- TimeStretcher's templates ----

template <typename Source>
void TimeStretcher::load(Source& source, int64_t from, int64_t to)
{
    from = std::max<int64_t>(0, from);
    int64_t have = m_srcStart + int64_t(m_src.size() / AudioChannels);
    if (m_src.empty() || from < m_srcStart || from > have + AudioRate) {
        m_src.clear(); // jumped somewhere else: start afresh
        m_srcStart = from;
        have = from;
    }
    if (to > have) {
        int64_t count = std::max<int64_t>(to - have, 4096);
        size_t old = m_src.size();
        m_src.resize(old + size_t(count) * AudioChannels);
        source(have, int(count), m_src.data() + old);
    }
}

template <typename Source>
void TimeStretcher::render(Source&& source, double inSeconds, double speed, int64_t outPos, int frames, float* out)
{
    auto floorDiv = [](int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    static const std::vector<float> window = [] {
        std::vector<float> w(N);
        for (int i = 0; i < N; ++i)
            w[size_t(i)] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / N); // two halves overlap to exactly 1
        return w;
    }();

    if (outPos != m_expectOut || inSeconds != m_in || speed != m_speed) {
        // Started somewhere new: begin with the slice before, so the first samples are covered twice too
        m_nextSlice = floorDiv(outPos, Hop) - 1;
        m_accStart = m_nextSlice * Hop;
        m_acc.clear();
        m_havePrev = false;
        m_in = inSeconds;
        m_speed = speed;
    }

    const int64_t lastSlice = floorDiv(outPos + frames - 1, Hop);
    for (; m_nextSlice <= lastSlice; ++m_nextSlice) {
        const int64_t outStart = m_nextSlice * Hop;
        const int64_t wanted = std::llround(inSeconds * AudioRate + double(outStart) * speed);
        const int64_t natural = m_prevStart + Hop; // where the last slice would carry on
        load(source, std::min(wanted - Tol, m_havePrev ? natural : wanted), std::max(wanted + Tol, natural) + N);
        const int64_t start = m_havePrev ? bestStart(wanted, natural) : wanted;

        size_t need = size_t(outStart + N - m_accStart) * AudioChannels;
        if (m_acc.size() < need)
            m_acc.resize(need, 0.0f);
        for (int i = 0; i < N; ++i) {
            size_t o = size_t(outStart - m_accStart + i) * AudioChannels;
            for (int ch = 0; ch < AudioChannels; ++ch)
                m_acc[o + size_t(ch)] += window[size_t(i)] * at(start + i, ch);
        }
        m_prevStart = start;
        m_havePrev = true;
    }

    const size_t offset = size_t(outPos - m_accStart) * AudioChannels;
    std::copy(m_acc.begin() + long(offset), m_acc.begin() + long(offset + size_t(frames) * AudioChannels), out);
    // Done with everything up to here
    m_acc.erase(m_acc.begin(), m_acc.begin() + long(offset + size_t(frames) * AudioChannels));
    m_accStart = outPos + frames;
    m_expectOut = outPos + frames;
    // ...and the file's sound from well before the next slice
    int64_t keepFrom = std::max(m_srcStart, std::min(m_prevStart, int64_t(std::llround(inSeconds * AudioRate + double(m_nextSlice * Hop) * speed))) - Tol - N);
    if (keepFrom > m_srcStart) {
        m_src.erase(m_src.begin(), m_src.begin() + long(size_t(keepFrom - m_srcStart) * AudioChannels));
        m_srcStart = keepFrom;
    }
}

} // namespace ve
