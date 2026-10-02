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

/* A frame grabber that stays open, for when you need lots of frames from one file. */
typedef struct ve_reader ve_reader;

ve_reader* ve_reader_open(const char* path); /* NULL if it can't be opened */
void ve_reader_close(ve_reader* reader);

/* Same idea as ve_thumbnail, but at any time. fast = nearest keyframe is fine. */
int ve_reader_frame(ve_reader* reader, double sec, int fast, int max_w, int max_h,
                    uint8_t* out_rgba, int* out_w, int* out_h);

/* ---- Timelines ---- */

typedef struct ve_clip {
    const char* path;
    int layer;       /* bigger = on top */
    double start;    /* where it sits on the timeline (seconds) */
    double in;       /* where in the file it starts playing from */
    double duration;
    int use_video;
    int use_audio;
    float volume;    /* 1.0 = as is */
} ve_clip;

/* A timeline isn't thread safe - give each thread its own. */
typedef struct ve_timeline ve_timeline;

ve_timeline* ve_timeline_create(void);
void ve_timeline_destroy(ve_timeline* tl);

void ve_timeline_set_clips(ve_timeline* tl, const ve_clip* clips, int count);
double ve_timeline_duration(const ve_timeline* tl);

/* Draws the frame at time t into a w x h RGBA buffer. */
int ve_timeline_render_video(ve_timeline* tl, double t, int w, int h, uint8_t* out_rgba);

/* Mixes `frames` stereo samples starting at time t (out needs frames * 2 floats). */
int ve_timeline_render_audio(ve_timeline* tl, double t, int frames, float* out);

/* ---- Export ---- */

typedef struct ve_export_settings {
    const char* path; /* e.g. "my video.mp4" */
    int width;
    int height;
    double fps;
    int crf;          /* quality, 18 = great, 23 = fine, 28 = small file */
    int force_software; /* 1 = skip the graphics card and use the CPU */

    char encoder_used[32]; /* filled in by ve_export, e.g. "h264_vaapi" or "libx264" */
} ve_export_settings;

/* Gets called as the export goes (done goes 0 -> 1). Return non-zero to cancel. */
typedef int (*ve_progress_fn)(double done, void* user);

int ve_export(ve_timeline* tl, ve_export_settings* settings,
              ve_progress_fn progress, void* user);

#ifdef __cplusplus
}
#endif

#endif /* VE_ENGINE_H */
