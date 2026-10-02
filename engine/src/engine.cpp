// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

#include "ve/engine.h"

#include "exporter.h"
#include "stream_copy.h"
#include "ffmpeg_util.h"
#include "audio_reader.h"
#include "timeline.h"
#include "video_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

struct ve_reader {
    ve::VideoReader reader;
};

struct ve_timeline {
    ve::Timeline timeline;
};

namespace {

void copyName(char* dst, size_t size, const char* src)
{
    std::snprintf(dst, size, "%s", src ? src : "");
}

// Shrinks a frame to fit inside max_w x max_h and hands back the size it used.
int fitFrame(ve::VideoReader& reader, const AVFrame* frame, int max_w, int max_h,
             uint8_t* out, int* out_w, int* out_h)
{
    if (!frame || frame->width <= 0 || frame->height <= 0)
        return VE_ERR_DECODE;

    double scale = std::min(double(max_w) / frame->width, double(max_h) / frame->height);
    int w = std::clamp(int(std::lround(frame->width * scale)), 1, max_w);
    int h = std::clamp(int(std::lround(frame->height * scale)), 1, max_h);
    if (!reader.scale(frame, w, h, out, w * 4))
        return VE_ERR_DECODE;

    *out_w = w;
    *out_h = h;
    return VE_OK;
}

} // namespace

extern "C" {

const char* ve_version(void)
{
    return VE_VERSION;
}

const char* ve_error_string(int code)
{
    switch (code) {
    case VE_OK: return "ok";
    case VE_ERR_ARG: return "bad argument";
    case VE_ERR_OPEN: return "couldn't open file";
    case VE_ERR_NO_STREAM: return "no video or audio found";
    case VE_ERR_DECODE: return "couldn't decode frame";
    case VE_ERR_ENCODE: return "export failed";
    case VE_ERR_CANCELLED: return "cancelled";
    default: return "unknown error";
    }
}

int ve_probe(const char* path, ve_media_info* out)
{
    if (!path || !out)
        return VE_ERR_ARG;
    *out = {};

    ve::FormatPtr fmt = ve::openInput(path);
    if (!fmt)
        return VE_ERR_OPEN;

    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        out->duration_sec = fmt->duration / double(AV_TIME_BASE);

    int v = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (v >= 0) {
        AVStream* s = fmt->streams[v];
        out->has_video = 1;
        out->width = s->codecpar->width;
        out->height = s->codecpar->height;
        AVRational rate = av_guess_frame_rate(fmt.get(), s, nullptr);
        if (rate.num > 0 && rate.den > 0)
            out->fps = av_q2d(rate);
        copyName(out->video_codec, sizeof out->video_codec, avcodec_get_name(s->codecpar->codec_id));
    }

    int a = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (a >= 0) {
        AVStream* s = fmt->streams[a];
        out->has_audio = 1;
        out->sample_rate = s->codecpar->sample_rate;
        out->channels = s->codecpar->ch_layout.nb_channels;
        copyName(out->audio_codec, sizeof out->audio_codec, avcodec_get_name(s->codecpar->codec_id));
    }

    // A single picture says it's a "video" but its duration is basically nothing
    if (out->has_video && !out->has_audio && out->duration_sec < 0.1)
        out->duration_sec = 0;

    if (v < 0 && a < 0)
        return VE_ERR_NO_STREAM;
    return VE_OK;
}

int ve_thumbnail(const char* path, int max_w, int max_h,
                 uint8_t* out_rgba, int* out_w, int* out_h)
{
    if (!path || max_w <= 0 || max_h <= 0 || !out_rgba || !out_w || !out_h)
        return VE_ERR_ARG;

    ve_media_info info;
    if (ve_probe(path, &info) != VE_OK)
        return VE_ERR_OPEN;

    ve::VideoReader reader;
    if (!reader.open(path))
        return VE_ERR_NO_STREAM;

    // Skip a little way in so we don't grab a black intro frame
    double at = std::min(info.duration_sec / 10.0, 5.0);
    return fitFrame(reader, reader.frameAt(at, true), max_w, max_h, out_rgba, out_w, out_h);
}

int ve_audio_peaks(const char* path, int per_second, float* out, int max_peaks)
{
    if (!path || per_second <= 0 || !out || max_peaks <= 0)
        return VE_ERR_ARG;
    ve::AudioReader reader;
    if (!reader.open(path))
        return VE_ERR_NO_STREAM;

    // Read straight through in slices, keeping the loudest sample of each
    const int slice = std::max(1, VE_AUDIO_RATE / per_second);
    std::vector<float> buf(size_t(slice) * VE_AUDIO_CHANNELS);
    int count = 0;
    for (; count < max_peaks; ++count) {
        double t = double(count) * slice / VE_AUDIO_RATE;
        if (!reader.read(t, slice, buf.data()))
            break; // end of the file
        float peak = 0.0f;
        for (float s : buf)
            peak = std::max(peak, std::abs(s));
        out[count] = std::min(peak, 1.0f);
    }
    return count;
}

ve_reader* ve_reader_open(const char* path)
{
    if (!path)
        return nullptr;
    auto* r = new ve_reader;
    if (!r->reader.open(path)) {
        delete r;
        return nullptr;
    }
    return r;
}

void ve_reader_close(ve_reader* reader)
{
    delete reader;
}

int ve_reader_frame(ve_reader* reader, double sec, int fast, int max_w, int max_h,
                    uint8_t* out_rgba, int* out_w, int* out_h)
{
    if (!reader || max_w <= 0 || max_h <= 0 || !out_rgba || !out_w || !out_h)
        return VE_ERR_ARG;
    const AVFrame* frame = reader->reader.frameAt(std::max(0.0, sec), fast != 0);
    return fitFrame(reader->reader, frame, max_w, max_h, out_rgba, out_w, out_h);
}

ve_timeline* ve_timeline_create(void)
{
    return new ve_timeline;
}

void ve_timeline_destroy(ve_timeline* tl)
{
    delete tl;
}

void ve_timeline_set_clips(ve_timeline* tl, const ve_clip* clips, int count)
{
    if (!tl)
        return;
    std::vector<ve::Clip> list;
    for (int i = 0; i < count && clips; ++i) {
        const ve_clip& c = clips[i];
        if (!c.path || c.duration <= 0)
            continue;
        ve::Clip clip;
        clip.path = c.path;
        clip.layer = c.layer;
        clip.start = c.start;
        clip.in = c.in;
        clip.duration = c.duration;
        clip.useVideo = c.use_video != 0;
        clip.useAudio = c.use_audio != 0;
        clip.volume = c.volume;
        clip.fadeIn = std::max(0.0, c.fade_in);
        clip.fadeOut = std::max(0.0, c.fade_out);
        list.push_back(std::move(clip));
    }
    tl->timeline.setClips(list);
}

double ve_timeline_duration(const ve_timeline* tl)
{
    return tl ? tl->timeline.duration() : 0.0;
}

int ve_timeline_render_video(ve_timeline* tl, double t, int w, int h, uint8_t* out_rgba)
{
    if (!tl || w <= 0 || h <= 0 || !out_rgba)
        return VE_ERR_ARG;
    tl->timeline.renderVideo(t, w, h, out_rgba);
    return VE_OK;
}

int ve_timeline_render_audio(ve_timeline* tl, double t, int frames, float* out)
{
    if (!tl || frames < 0 || !out)
        return VE_ERR_ARG;
    tl->timeline.renderAudio(t, frames, out);
    return VE_OK;
}

int ve_timeline_can_copy(ve_timeline* tl, char* why, int why_size)
{
    if (!tl)
        return 0;
    std::string source, reason;
    std::vector<ve::CopySegment> segments;
    bool ok = tl->timeline.copyPlan(source, segments, reason);
    if (why && why_size > 0)
        copyName(why, size_t(why_size), reason.c_str());
    return ok ? 1 : 0;
}

int ve_export(ve_timeline* tl, ve_export_settings* settings,
              ve_progress_fn progress, void* user)
{
    if (!tl || !settings || !settings->path)
        return VE_ERR_ARG;

    if (settings->copy_only) {
        std::string source, why;
        std::vector<ve::CopySegment> segments;
        if (!tl->timeline.copyPlan(source, segments, why))
            return VE_ERR_ARG;
        copyName(settings->encoder_used, sizeof settings->encoder_used, "copy");
        return ve::copyStreams(source, segments, settings->path, progress, user);
    }
    ve::ExportSettings s;
    s.path = settings->path;
    s.width = settings->width;
    s.height = settings->height;
    s.fps = settings->fps;
    s.crf = settings->crf > 0 ? settings->crf : 20;
    s.hardware = settings->force_software == 0;

    std::string used;
    int rc = ve::exportTimeline(tl->timeline, s, progress, user, &used);
    copyName(settings->encoder_used, sizeof settings->encoder_used, used.c_str());
    return rc;
}

} // extern "C"
