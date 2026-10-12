// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

// Auto-captions: listens to the timeline's sound with whisper.cpp and hands back every word
// it hears, with when it was said.

#include "handles.h"
#include "ve/engine.h"
#include "effects.h" // (threadLimit)

#include <whisper.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int Rate = 16000; // what whisper listens at

struct Job {
    ve_progress_fn progress;
    void* user;
    std::atomic<bool> cancelled { false };
    bool report(double done)
    {
        if (progress && progress(std::clamp(done, 0.0, 1.0), user))
            cancelled = true;
        return !cancelled;
    }
};

bool bracketed(const std::string& text)
{
    // "[Music]", "(applause)", "[BLANK_AUDIO]": whisper describing sounds, not words
    size_t a = text.find_first_not_of(' '), b = text.find_last_not_of(' ');
    if (a == std::string::npos)
        return true;
    char first = text[a], last = text[b];
    return (first == '[' && last == ']') || (first == '(' && last == ')') || (first == '*' && last == '*');
}

// Which of whisper's word-timing presets fits this model (worked out from its file name)
whisper_alignment_heads_preset alignmentFor(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    name = name.substr(name.find_last_of("/\\") + 1);
    const bool en = name.find(".en") != std::string::npos;
    // Distilled models keep the big encoder but only a couple of decoder layers, so none of the
    // presets fit (and using one would point at layers that aren't there)
    if (name.find("distil") != std::string::npos)
        return WHISPER_AHEADS_NONE;
    if (name.find("large-v3-turbo") != std::string::npos) return WHISPER_AHEADS_LARGE_V3_TURBO;
    if (name.find("large-v3") != std::string::npos) return WHISPER_AHEADS_LARGE_V3;
    if (name.find("large-v2") != std::string::npos) return WHISPER_AHEADS_LARGE_V2;
    if (name.find("large") != std::string::npos) return WHISPER_AHEADS_LARGE_V1;
    if (name.find("medium") != std::string::npos) return en ? WHISPER_AHEADS_MEDIUM_EN : WHISPER_AHEADS_MEDIUM;
    if (name.find("small") != std::string::npos) return en ? WHISPER_AHEADS_SMALL_EN : WHISPER_AHEADS_SMALL;
    if (name.find("base") != std::string::npos) return en ? WHISPER_AHEADS_BASE_EN : WHISPER_AHEADS_BASE;
    if (name.find("tiny") != std::string::npos) return en ? WHISPER_AHEADS_TINY_EN : WHISPER_AHEADS_TINY;
    return WHISPER_AHEADS_NONE;
}

// A low-pass filter for going from 48kHz down to 16kHz: keeps speech, stops the high end
// folding back down as hiss (which whisper would hear)
std::vector<float> lowPass(int taps, double cutoff)
{
    std::vector<float> h(static_cast<size_t>(taps));
    const double pi = 3.14159265358979323846, middle = (taps - 1) / 2.0;
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        double x = i - middle;
        double sinc = x == 0 ? 2 * cutoff : std::sin(2 * pi * cutoff * x) / (pi * x);
        double blackman = 0.42 - 0.5 * std::cos(2 * pi * i / (taps - 1)) + 0.08 * std::cos(4 * pi * i / (taps - 1));
        h[size_t(i)] = float(sinc * blackman);
        sum += h[size_t(i)];
    }
    for (float& v : h)
        v = float(v / sum);
    return h;
}

// When only the talking gets listened to, the bits of talking are glued together. This keeps
// track of where each bit really was, to put the words back on the timeline.
struct Piece {
    double from;   // where it starts in the glued-together sound (seconds)
    double at;     // where it really is
    double length;
};

double toTimeline(const std::vector<Piece>& pieces, double t)
{
    if (pieces.empty())
        return t;
    auto it = std::upper_bound(pieces.begin(), pieces.end(), t, [](double v, const Piece& p) { return v < p.from; });
    const Piece& p = it == pieces.begin() ? pieces.front() : *std::prev(it);
    return p.at + std::clamp(t - p.from, 0.0, p.length);
}

// Finds the talking with the speech detector and glues it together (with a little gap between).
// Empty pieces = no detector, so use the whole thing.
std::vector<Piece> keepTalking(const char* detector, int threads, std::vector<float>& samples, Job& job)
{
    if (!detector || !*detector)
        return {};
    whisper_vad_context_params cp = whisper_vad_default_context_params();
    cp.n_threads = threads;
    cp.use_gpu = false; // (it's tiny, the CPU's quicker than getting the GPU going)
    whisper_vad_context* vad = whisper_vad_init_from_file_with_params(detector, cp);
    if (!vad)
        return {}; // (no harm done, just listen to all of it)
    whisper_vad_params vp = whisper_vad_default_params();
    vp.threshold = 0.4f;            // (a bit keen: missing a quiet word is worse than listening to a pause)
    vp.min_speech_duration_ms = 200;
    vp.min_silence_duration_ms = 400;
    vp.speech_pad_ms = 250;         // room either side so the first and last sounds don't get clipped
    vp.max_speech_duration_s = 25;  // (fits in one of whisper's 30-second windows)
    whisper_vad_segments* found = whisper_vad_segments_from_samples(vad, vp, samples.data(), int(samples.size()));
    std::vector<Piece> pieces;
    if (found) {
        const int n = whisper_vad_segments_n_segments(found);
        const double total = double(samples.size()) / Rate;
        for (int i = 0; i < n; ++i) {
            double a = std::clamp(whisper_vad_segments_get_segment_t0(found, i) / 100.0, 0.0, total);
            double b = std::clamp(whisper_vad_segments_get_segment_t1(found, i) / 100.0, 0.0, total);
            if (b <= a)
                continue;
            // Short pauses stay in (they help whisper tell where sentences end)
            if (!pieces.empty() && a - (pieces.back().at + pieces.back().length) < 0.6)
                pieces.back().length = std::max(pieces.back().length, b - pieces.back().at);
            else
                pieces.push_back({ 0, a, b - a });
        }
        whisper_vad_free_segments(found);
    }
    whisper_vad_free(vad);
    if (pieces.empty()) {
        samples.clear(); // nobody talking
        return { { 0, 0, 0 } };
    }
    // Glue: each bit of talking, then a fifth of a second of silence
    std::vector<float> glued;
    const size_t gap = Rate / 5;
    for (Piece& p : pieces) {
        p.from = double(glued.size()) / Rate;
        size_t a = size_t(p.at * Rate), b = std::min(samples.size(), size_t((p.at + p.length) * Rate));
        glued.insert(glued.end(), samples.begin() + std::ptrdiff_t(a), samples.begin() + std::ptrdiff_t(b));
        glued.insert(glued.end(), gap, 0.0f);
    }
    samples.swap(glued);
    job.report(0.12);
    return pieces;
}

} // namespace

extern "C" {

int ve_captions_available(void)
{
    return 1;
}

int ve_auto_captions(ve_timeline* tl, const ve_caption_settings* settings, ve_caption_word_fn word,
                     ve_progress_fn progress, void* user)
{
    if (!tl || !settings || !settings->model_path || !word)
        return VE_ERR_ARG;
    Job job { progress, user };

    // 1. The finished sound, mixed down to one channel at 16kHz (the first 10% of the bar)
    const double duration = tl->timeline.duration();
    if (duration <= 0)
        return VE_ERR_ARG;
    const int64_t total = int64_t(duration * Rate);
    std::vector<float> samples(static_cast<size_t>(total));
    {
        static_assert(ve::AudioRate == Rate * 3, "48kHz in, 16kHz out");
        const std::vector<float> filter = lowPass(63, 7400.0 / ve::AudioRate);
        const size_t half = filter.size() / 2;
        std::vector<float> chunk(size_t(ve::AudioRate) * ve::AudioChannels);
        std::vector<float> mono(half, 0.0f); // (starts with a bit of silence, so it lines up)
        const int64_t totalIn = total * 3;
        size_t read = 0; // where the next output's window starts in `mono`
        int64_t out = 0;
        for (int64_t in = 0; out < total;) {
            const int frames = int(std::min<int64_t>(ve::AudioRate, totalIn - in));
            if (frames > 0) {
                tl->timeline.renderAudio(double(in) / ve::AudioRate, frames, chunk.data());
                for (int i = 0; i < frames; ++i)
                    mono.push_back(0.5f * (chunk[size_t(i) * 2] + chunk[size_t(i) * 2 + 1]));
                in += frames;
            } else {
                mono.insert(mono.end(), filter.size(), 0.0f); // (the very end)
            }
            for (; read + filter.size() <= mono.size() && out < total; read += 3, ++out) {
                float sum = 0;
                for (size_t k = 0; k < filter.size(); ++k)
                    sum += filter[k] * mono[read + k];
                samples[size_t(out)] = sum;
            }
            mono.erase(mono.begin(), mono.begin() + std::ptrdiff_t(read));
            read = 0;
            if (!job.report(0.1 * double(out) / double(total)))
                return VE_ERR_CANCELLED;
        }
    }

    // How loud each 10ms is, to spot made-up words in silence later
    std::vector<float> loudness(size_t(total / 160 + 1));
    for (size_t i = 0; i < loudness.size(); ++i) {
        double sum = 0;
        int n = 0;
        for (int64_t k = int64_t(i) * 160; k < std::min<int64_t>(total, int64_t(i + 1) * 160); ++k, ++n)
            sum += double(samples[size_t(k)]) * samples[size_t(k)];
        loudness[i] = n ? float(std::sqrt(sum / n)) : 0.0f;
    }
    auto averageLoudness = [&](double from, double to) {
        size_t a = size_t(std::max(0.0, from * 100)), b = std::min(loudness.size(), size_t(std::max(0.0, to * 100)) + 1);
        double sum = 0;
        for (size_t i = a; i < b; ++i)
            sum += loudness[i];
        return b > a ? sum / double(b - a) : 0.0;
    };

    // 2. Only the talking (if there's a speech detector): quicker, and whisper can't imagine
    // words in the quiet bits
    whisper_log_set([](enum ggml_log_level, const char*, void*) {}, nullptr); // (it's chatty)
    // Every thread waits for the slowest one at each step, so using all of them (when the rest of
    // the computer wants some too, or half are slow "efficiency" cores) is slower than leaving two free
    const int cores = std::max(1, int(std::thread::hardware_concurrency()));
    const int threads = settings->threads > 0 ? settings->threads
                      : ve::threadLimit() > 0 ? ve::threadLimit()
                      : cores > 4 ? cores - 2 : cores;
    const std::vector<Piece> pieces = keepTalking(settings->vad_model_path, threads, samples, job);
    if (samples.empty()) {
        job.report(1.0);
        return VE_OK; // (nothing to hear)
    }
    auto when = [&pieces](int64_t centiseconds) { return toTimeline(pieces, centiseconds / 100.0); };

    // 3. Whisper
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = settings->use_gpu != 0;
    // Finer word timings (lines each word up with the sound), when we know the model
    cparams.dtw_aheads_preset = alignmentFor(settings->model_path);
    cparams.dtw_token_timestamps = cparams.dtw_aheads_preset != WHISPER_AHEADS_NONE;
    if (cparams.dtw_token_timestamps)
        cparams.flash_attn = false; // (the two don't work together)
    whisper_context* ctx = whisper_init_from_file_with_params(settings->model_path, cparams);
    if (!ctx)
        return VE_ERR_OPEN;

    const bool careful = settings->careful != 0;
    whisper_full_params p = whisper_full_default_params(careful ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    if (careful)
        p.beam_search.beam_size = 5;
    p.n_threads = threads;
    // Names and unusual words: whisper reads these first, so it knows to expect them
    const std::string hint = settings->hint ? settings->hint : "";
    if (!hint.empty()) {
        p.initial_prompt = hint.c_str();
        p.carry_initial_prompt = true;
    }
    std::string language = settings->language && *settings->language ? settings->language : "en";
    p.language = language == "auto" ? "auto" : language.c_str();
    p.detect_language = false;
    p.translate = settings->translate != 0;
    p.no_context = true;         // stops it getting stuck repeating itself on long videos
    p.token_timestamps = true;   // when each word is said
    p.suppress_blank = true;
    p.suppress_nst = true;       // no "[Music]" and friends (mostly)
    p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
    p.progress_callback = [](whisper_context*, whisper_state*, int percent, void* j) {
        static_cast<Job*>(j)->report(0.12 + 0.88 * percent / 100.0);
    };
    p.progress_callback_user_data = &job;
    p.abort_callback = [](void* j) { return static_cast<Job*>(j)->cancelled.load(); };
    p.abort_callback_user_data = &job;

    int rc = whisper_full(ctx, p, samples.data(), int(samples.size()));
    if (job.cancelled) {
        whisper_free(ctx);
        return VE_ERR_CANCELLED;
    }
    if (rc != 0) {
        whisper_free(ctx);
        return VE_ERR_DECODE;
    }

    // 4. Words: tokens starting with a space start a new word
    const whisper_token eot = whisper_token_eot(ctx);
    const int segments = whisper_full_n_segments(ctx);
    for (int s = 0; s < segments; ++s) {
        std::string text = whisper_full_get_segment_text(ctx, s);
        double t0 = when(whisper_full_get_segment_t0(ctx, s)), t1 = when(whisper_full_get_segment_t1(ctx, s));
        // Sound descriptions, or "words" heard in near silence (whisper sometimes imagines those)
        if (bracketed(text) || averageLoudness(t0, t1) < 0.003)
            continue;

        struct Word {
            std::string text;
            double start, end;
        };
        std::vector<Word> words;
        const int tokens = whisper_full_n_tokens(ctx, s);
        for (int k = 0; k < tokens; ++k) {
            if (whisper_full_get_token_id(ctx, s, k) >= eot)
                continue; // timestamps and other special markers
            std::string piece = whisper_full_get_token_text(ctx, s, k);
            double a = when(whisper_full_get_token_t0(ctx, s, k)), b = when(whisper_full_get_token_t1(ctx, s, k));
            if (cparams.dtw_token_timestamps) {
                int64_t aligned = whisper_full_get_token_data(ctx, s, k).t_dtw;
                if (aligned >= 0)
                    a = when(aligned); // (much closer to when it's really said)
            }
            bool fresh = words.empty() || (!piece.empty() && piece[0] == ' ');
            size_t from = piece.find_first_not_of(' ');
            if (from == std::string::npos)
                continue;
            piece = piece.substr(from);
            if (fresh)
                words.push_back({ piece, a, b });
            else {
                words.back().text += piece;
                words.back().end = std::max(words.back().end, b);
            }
        }
        // Keep the times sensible: in order, inside the segment, each word ending where the next begins
        double last = t0;
        for (Word& w : words) {
            w.start = std::clamp(std::max(w.start, last), t0, t1);
            last = w.start;
        }
        for (size_t k = 0; k < words.size(); ++k)
            words[k].end = k + 1 < words.size() ? std::max(words[k + 1].start, words[k].start + 0.05) : std::max(t1, words[k].start + 0.05);
        // A word that "starts" in silence really starts where the sound does
        const float quiet = 0.004f;
        for (Word& w : words) {
            double at = w.start;
            while (at < w.end - 0.05 && loudness[std::min(loudness.size() - 1, size_t(at * 100))] < quiet)
                at += 0.01;
            w.start = at;
        }
        for (size_t k = 0; k < words.size(); ++k) {
            Word& w = words[k];
            if (bracketed(w.text))
                continue;
            ve_caption_word out { w.start, w.end, w.text.c_str(), k == 0 ? 1 : 0 };
            word(&out, user);
        }
    }
    whisper_free(ctx);
    job.report(1.0);
    return VE_OK;
}

} // extern "C"
