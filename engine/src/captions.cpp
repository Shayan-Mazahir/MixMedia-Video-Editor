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
    std::vector<float> chunk(size_t(ve::AudioRate) * ve::AudioChannels);
    for (int64_t at = 0; at < total;) {
        int frames = int(std::min<int64_t>(ve::AudioRate, (total - at) * 3));
        tl->timeline.renderAudio(double(at) / Rate, frames, chunk.data());
        // 48kHz -> 16kHz: average each three (a gentle low-pass, enough for speech)
        for (int i = 0; i + 2 < frames && at < total; i += 3, ++at) {
            float sum = 0;
            for (int k = 0; k < 3; ++k)
                sum += chunk[size_t(i + k) * 2] + chunk[size_t(i + k) * 2 + 1];
            samples[size_t(at)] = sum / 6.0f;
        }
        if (!job.report(0.1 * double(at) / double(total)))
            return VE_ERR_CANCELLED;
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

    // 2. Whisper
    whisper_log_set([](enum ggml_log_level, const char*, void*) {}, nullptr); // (it's chatty)
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

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    const int cores = std::max(1, int(std::thread::hardware_concurrency()));
    p.n_threads = settings->threads > 0 ? settings->threads : (ve::threadLimit() > 0 ? ve::threadLimit() : cores);
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
        static_cast<Job*>(j)->report(0.1 + 0.9 * percent / 100.0);
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

    // 3. Words: tokens starting with a space start a new word
    const whisper_token eot = whisper_token_eot(ctx);
    const int segments = whisper_full_n_segments(ctx);
    for (int s = 0; s < segments; ++s) {
        std::string text = whisper_full_get_segment_text(ctx, s);
        double t0 = whisper_full_get_segment_t0(ctx, s) / 100.0, t1 = whisper_full_get_segment_t1(ctx, s) / 100.0;
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
            double a = whisper_full_get_token_t0(ctx, s, k) / 100.0, b = whisper_full_get_token_t1(ctx, s, k) / 100.0;
            if (cparams.dtw_token_timestamps) {
                int64_t aligned = whisper_full_get_token_data(ctx, s, k).t_dtw;
                if (aligned >= 0)
                    a = aligned / 100.0; // (much closer to when it's really said)
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
