// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

/*
 * The engine's front door. Kept as plain C on purpose so Rust, C# or
 * whatever else we fancy later can talk to it too.
 */
#ifndef VE_ENGINE_H
#define VE_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VE_OK = 0,
    VE_ERR_ARG = -1,       /* bad argument passed in */
    VE_ERR_OPEN = -2,      /* file couldn't be opened or isn't media */
    VE_ERR_NO_STREAM = -3, /* no usable video/audio inside */
    VE_ERR_DECODE = -4,    /* couldn't decode a frame */
    VE_ERR_ENCODE = -5,    /* something went wrong while exporting */
    VE_ERR_CANCELLED = -6  /* export was cancelled */
};

/* All sound coming out of the engine is 48kHz stereo floats, interleaved (L R L R ...). */
#define VE_AUDIO_RATE 48000
#define VE_AUDIO_CHANNELS 2

const char* ve_version(void);
const char* ve_error_string(int code);

/* ---- Looking at files ---- */

typedef struct ve_media_info {
    double duration_sec; /* 0 if unknown (e.g. still images) */

    int has_video;
    int width;
    int height;
    double fps;
    char video_codec[32];

    int has_audio;
    int sample_rate;
    int channels;
    char audio_codec[32];
} ve_media_info;

/* Reads a file's details without decoding it. */
int ve_probe(const char* path, ve_media_info* out);

/*
 * Grabs a frame from a little way into the video, shrunk to fit inside
 * max_w x max_h. out_rgba needs room for max_w * max_h * 4 bytes, and
 * the size it actually ended up as goes in out_w / out_h.
 */
int ve_thumbnail(const char* path, int max_w, int max_h,
                 uint8_t* out_rgba, int* out_w, int* out_h);

/*
 * A loudness overview, for drawing waveforms: the loudest bit (0..1) of each little slice
 * of time, `per_second` slices per second. Writes up to max_peaks values and returns how
 * many it wrote (or a negative VE_ERR). Reads the whole file, so call it off the main thread.
 */
int ve_audio_peaks(const char* path, int per_second, float* out, int max_peaks);

/* A frame grabber that stays open, for when you need lots of frames from one file. */
typedef struct ve_reader ve_reader;

ve_reader* ve_reader_open(const char* path); /* NULL if it can't be opened */
void ve_reader_close(ve_reader* reader);

/* Same idea as ve_thumbnail, but at any time. fast = nearest keyframe is fine. */
int ve_reader_frame(ve_reader* reader, double sec, int fast, int max_w, int max_h,
                    uint8_t* out_rgba, int* out_w, int* out_h);

/* ---- Timelines ---- */

/* A setting's value at one moment of a clip. In between keyframes it glides smoothly. */
typedef struct ve_keyframe {
    int param;    /* one of VE_KEY_* */
    double time;  /* seconds from the start of the clip */
    float value;  /* size: 1 = normal; position: fraction of the frame; opacity: 0..1;
                     rotation: degrees clockwise; volume: 1 = as is */
} ve_keyframe;

enum {
    VE_KEY_SIZE = 0,
    VE_KEY_POS_X,
    VE_KEY_POS_Y,
    VE_KEY_OPACITY,
    VE_KEY_ROTATION,
    VE_KEY_VOLUME,
    VE_KEY_COUNT
};

typedef struct ve_clip {
    int kind;        /* VE_CLIP_MEDIA (a file), VE_CLIP_ADJUSTMENT (effects over everything below, no file)
                        or VE_CLIP_TRANSITION (a transition played on everything below, no file) */
    const char* path;
    int layer;       /* bigger = on top */
    double start;    /* where it sits on the timeline (seconds) */
    double in;       /* where in the file it starts playing from */
    double duration;
    int use_video;
    int use_audio;
    float volume;    /* 1.0 = as is */
    double fade_in;  /* seconds to fade up from black/silence, 0 = none */
    double fade_out; /* seconds to fade away at the end */

    /* Everything below is "0 = leave it alone", so a zeroed ve_clip is a plain clip. */
    double speed;       /* 2 = twice as fast, 0.5 = slow motion (0 counts as 1) */
    float transparency; /* 0 = solid, 1 = invisible */
    float size;         /* picture-in-picture size, 1 = normal (0 counts as 1) */
    float pos_x;        /* shift as a fraction of the frame, 0 = centred */
    float pos_y;

    int look;           /* one of VE_LOOK_* */
    float brightness;   /* -1..1 */
    float contrast;     /* -1..1 */
    float saturation;   /* -1..1, -1 = grey */
    float temperature;  /* -1..1, negative = cooler, positive = warmer */
    float blur;         /*  0..1 */
    float sharpen;      /*  0..1 */
    float vignette;     /*  0..1 */

    /* Media clips: transition from the clip before on the same layer. If this clip overlaps
       the end of that one they blend across the overlap, if they just touch it's centred on the cut.
       VE_CLIP_TRANSITION blocks: the transition they play (and transition_part says how). */
    int transition;             /* one of VE_TRANSITION_* */
    double transition_duration; /* seconds (0 counts as 1) */

    /* How the clip arrives and leaves */
    int anim_in;                /* one of VE_ANIM_* */
    double anim_in_duration;    /* seconds (0 counts as 0.5) */
    int anim_out;
    double anim_out_duration;

    /* VE_CLIP_TRANSITION blocks only: one of VE_PART_* */
    int transition_part;

    /* Crop (fraction cut off each edge, 0..0.95), turning and mirroring */
    float crop_left, crop_right, crop_top, crop_bottom;
    float rotation;      /* degrees, clockwise */
    int flip_h, flip_v;  /* 1 = mirrored left-right / upside down */
    int fill_frame;      /* 1 = cover the whole frame (cutting off what hangs over), 0 = fit inside it */

    int reverse;         /* 1 = plays backwards */
    int freeze;          /* 1 = holds the frame at `in` the whole time */

    /* Green screen: makes everything close to key_color see-through */
    int chroma_key;               /* 1 = on */
    unsigned int key_color;       /* 0xRRGGBB */
    float key_strength;           /* 0..1, how far from that colour still counts */
    float key_softness;           /* 0..1, how gradual the edge is */
    float key_spill;              /* 0..1, how much of its glow to take off the subject */

    /* Sound */
    int keep_pitch;      /* 1 = speed changes don't change the pitch */
    float denoise;       /* 0..1, how much steady background noise (hum, hiss) to take out */
    int duck;            /* 1 = gets quieter while other clips have talking in them (for music) */
    float duck_amount;   /* 0..1, how much quieter (0 counts as 0.7) */

    /* Settings that change over time (any order). Copied, so they only need to live during the call. */
    const ve_keyframe* keyframes;
    int keyframe_count;
} ve_clip;

enum {
    VE_TRANSITION_NONE = 0,
    VE_TRANSITION_DISSOLVE,
    VE_TRANSITION_FADE_BLACK,
    VE_TRANSITION_WIPE_LEFT,
    VE_TRANSITION_WIPE_RIGHT,
    VE_TRANSITION_WIPE_UP,
    VE_TRANSITION_WIPE_DOWN,
    VE_TRANSITION_SLIDE_LEFT,
    VE_TRANSITION_SLIDE_RIGHT,
    VE_TRANSITION_ZOOM,
    VE_TRANSITION_COUNT
};

enum {
    VE_ANIM_NONE = 0,
    VE_ANIM_FADE,
    VE_ANIM_SLIDE_LEFT,
    VE_ANIM_SLIDE_RIGHT,
    VE_ANIM_SLIDE_UP,
    VE_ANIM_SLIDE_DOWN,
    VE_ANIM_ZOOM,
    VE_ANIM_WIPE,
    VE_ANIM_RISE, /* floats up a little while fading in (drifts down going out) */
    VE_ANIM_COUNT
};

enum {
    VE_CLIP_MEDIA = 0,
    VE_CLIP_ADJUSTMENT = 1,
    VE_CLIP_TRANSITION = 2
};

/* How a transition block plays on the picture below it */
enum {
    VE_PART_THROUGH = 0, /* out and back in again, on the spot */
    VE_PART_IN,          /* in from black */
    VE_PART_OUT          /* out to black */
};

/* One-click looks */
enum {
    VE_LOOK_NONE = 0,
    VE_LOOK_BLACK_AND_WHITE,
    VE_LOOK_SEPIA,
    VE_LOOK_VINTAGE,
    VE_LOOK_VIVID,
    VE_LOOK_COOL,
    VE_LOOK_WARM,
    VE_LOOK_FADED,
    VE_LOOK_DRAMATIC,
    VE_LOOK_COUNT
};

/* A timeline isn't thread safe - give each thread its own. */
typedef struct ve_timeline ve_timeline;

ve_timeline* ve_timeline_create(void);
void ve_timeline_destroy(ve_timeline* tl);

/* For live preview: fewer decoder threads and quicker scaling. */
void ve_timeline_use_preview_settings(ve_timeline* tl);

void ve_timeline_set_clips(ve_timeline* tl, const ve_clip* clips, int count);
double ve_timeline_duration(const ve_timeline* tl);

/* Draws the frame at time t into a w x h RGBA buffer. */
int ve_timeline_render_video(ve_timeline* tl, double t, int w, int h, uint8_t* out_rgba);

/* Same, but B G R A byte order: what Qt's QImage::Format_RGB32 and most screens use. */
int ve_timeline_render_video_bgra(ve_timeline* tl, double t, int w, int h, uint8_t* out_bgra);

/* Mixes `frames` stereo samples starting at time t (out needs frames * 2 floats). */
int ve_timeline_render_audio(ve_timeline* tl, double t, int frames, float* out);

/* Applies a clip's effects (look, brightness, blur...) to an RGBA picture in place. Handy for previews. */
int ve_apply_effects(const ve_clip* settings, uint8_t* rgba, int w, int h);

/* ---- Export ---- */

/*
 * Can this timeline be exported instantly (copied without re-encoding)? That works for
 * straightforward cuts from a single video file. Returns 1 if so; otherwise 0, with the
 * reason written into `why` (if given).
 */
int ve_timeline_can_copy(ve_timeline* tl, char* why, int why_size);

/* One subtitle line, for exporting as a track people can switch on */
typedef struct ve_subtitle {
    double start, end;  /* seconds */
    const char* text;   /* UTF-8, \n for a new line */
} ve_subtitle;

typedef struct ve_export_settings {
    const char* path; /* e.g. "my video.mp4" */
    int width;
    int height;
    double fps;
    int crf;          /* quality, 18 = great, 23 = fine, 28 = small file */
    int force_software; /* 1 = skip the graphics card and use the CPU */
    int copy_only;      /* 1 = instant export: copy the video as-is, no re-encoding.
                           Only works if ve_timeline_can_copy says so. Size/fps/crf are ignored. */
    int format;         /* one of VE_FORMAT_* (0 = MP4) */
    const ve_subtitle* subtitles; /* MP4 only: added as a subtitle track players can switch on */
    int subtitle_count;
    const char* subtitle_language; /* e.g. "eng" (3 letters), or null */

    char encoder_used[32]; /* filled in by ve_export, e.g. "h264_vaapi" or "libx264" */
} ve_export_settings;

enum {
    VE_FORMAT_MP4 = 0, /* H.264 video + AAC sound */
    VE_FORMAT_GIF,     /* animated, no sound */
    VE_FORMAT_MP3,     /* sound only */
    VE_FORMAT_M4A      /* sound only (AAC) */
};

/* How loud the sound in [from, from + length) of a file is: average (RMS) and peak, in dB
   (0 = as loud as it can go). Listens to slices across it, so it's quick. Returns VE_OK or an error. */
int ve_measure_loudness(const char* path, double from, double length, float* rms_db, float* peak_db);

/* How many CPU threads the engine may use for decoding, effects and exporting.
   0 = decide automatically (the default). Applies to everything started afterwards. */
void ve_set_thread_limit(int threads);
/* How many CPU threads this computer has */
int ve_cpu_threads(void);

/* 1 if this build can export to that format (MP3 needs FFmpeg built with LAME) */
int ve_export_format_available(int format);

/* Gets called as the export goes (done goes 0 -> 1). Return non-zero to cancel. */
typedef int (*ve_progress_fn)(double done, void* user);

int ve_export(ve_timeline* tl, ve_export_settings* settings,
              ve_progress_fn progress, void* user);

/* ---- Auto-captions (whisper.cpp) ---- */

/* 1 if this build has them */
int ve_captions_available(void);

typedef struct ve_caption_settings {
    const char* model_path; /* a whisper ggml model file, e.g. ggml-base.bin */
    const char* language;   /* "en", "fr", ... or "auto" to work it out */
    int translate;          /* 1 = write it in English, whatever's being spoken */
    int threads;            /* CPU threads, 0 = the thread limit (or all of them) */
    int use_gpu;            /* 1 = use the graphics card if this build can */
} ve_caption_settings;

typedef struct ve_caption_word {
    double start, end;      /* seconds on the timeline */
    const char* text;       /* UTF-8, only valid during the call */
    int starts_sentence;    /* 1 = whisper started a new bit of speech here */
} ve_caption_word;
typedef void (*ve_caption_word_fn)(const ve_caption_word* word, void* user);

/* Listens to the timeline's sound and calls `word` for every word it hears, in order.
   Slow (it's doing a lot of maths), so run it off the main thread. `progress` can cancel it.
   Returns VE_OK, VE_ERR_OPEN (couldn't load the model), VE_ERR_CANCELLED, ... */
int ve_auto_captions(ve_timeline* tl, const ve_caption_settings* settings, ve_caption_word_fn word,
                     ve_progress_fn progress, void* user);


#ifdef __cplusplus
}
#endif

#endif /* VE_ENGINE_H */
