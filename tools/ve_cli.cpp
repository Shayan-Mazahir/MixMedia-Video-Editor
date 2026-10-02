// ve-cli: poke at the engine from the terminal.
//
//   ve-cli probe  <file>
//   ve-cli frame  <file> <sec> <out.png>            one frame straight from the file
//   ve-cli render <out.png> <w> <h> <sec> <clips...> a frame from a timeline
//   ve-cli audio  <file> <sec> <len>                 how loud is it there?
//   ve-cli bench  <file> <sec> <len>                 how fast can we play it back?
//   ve-cli peaks  <file>                             waveform overview, timed
//   ve-cli export <out.mp4> <w> <h> <fps> <clips...>
//
// <clips...> is groups of five: path start in duration layer

#include <ve/engine.h>

#include <QImage>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int fail(const char* what, int code)
{
    std::fprintf(stderr, "%s: %s\n", what, ve_error_string(code));
    return 1;
}

std::vector<ve_clip> parseClips(int argc, char** argv, int first)
{
    std::vector<ve_clip> clips;
    for (int i = first; i + 4 < argc; i += 5) {
        ve_clip c {};
        c.path = argv[i];
        c.start = std::atof(argv[i + 1]);
        c.in = std::atof(argv[i + 2]);
        c.duration = std::atof(argv[i + 3]);
        c.layer = std::atoi(argv[i + 4]);
        c.use_video = c.use_audio = 1;
        c.volume = 1.0f;
        clips.push_back(c);
    }
    return clips;
}

int probe(const char* path)
{
    ve_media_info info;
    if (int rc = ve_probe(path, &info))
        return fail("probe", rc);
    std::printf("duration: %.3f s\n", info.duration_sec);
    if (info.has_video)
        std::printf("video:    %dx%d @ %.3f fps (%s)\n", info.width, info.height, info.fps, info.video_codec);
    if (info.has_audio)
        std::printf("audio:    %d Hz, %d ch (%s)\n", info.sample_rate, info.channels, info.audio_codec);
    return 0;
}

int frame(const char* path, double sec, const char* out)
{
    ve_reader* r = ve_reader_open(path);
    if (!r)
        return fail("open", VE_ERR_OPEN);
    const int maxW = 960, maxH = 540;
    QImage img(maxW, maxH, QImage::Format_RGBA8888);
    int w = 0, h = 0;
    int rc = ve_reader_frame(r, sec, 0, maxW, maxH, img.bits(), &w, &h);
    ve_reader_close(r);
    if (rc)
        return fail("frame", rc);
    // bits() is packed at w*4 per row, so rebuild the image at its real size
    QImage real(img.bits(), w, h, w * 4, QImage::Format_RGBA8888);
    return real.save(out) ? 0 : fail("save", VE_ERR_ARG);
}

int render(const char* out, int w, int h, double sec, std::vector<ve_clip> clips)
{
    ve_timeline* tl = ve_timeline_create();
    ve_timeline_set_clips(tl, clips.data(), int(clips.size()));
    QImage img(w, h, QImage::Format_RGBA8888);
    ve_timeline_render_video(tl, sec, w, h, img.bits());
    ve_timeline_destroy(tl);
    return img.save(out) ? 0 : fail("save", VE_ERR_ARG);
}

int audio(const char* path, double sec, double len)
{
    ve_media_info info;
    if (int rc = ve_probe(path, &info))
        return fail("probe", rc);
    ve_clip c {};
    c.path = path;
    c.duration = info.duration_sec;
    c.use_audio = 1;
    c.volume = 1.0f;
    ve_timeline* tl = ve_timeline_create();
    ve_timeline_set_clips(tl, &c, 1);

    // Read in small chunks like a speaker would, and report the loudness of each second
    const int chunk = 1024;
    std::vector<float> buf(chunk * 2);
    double t = sec, sum = 0;
    long n = 0;
    while (t < sec + len) {
        ve_timeline_render_audio(tl, t, chunk, buf.data());
        for (float s : buf) {
            sum += s * s;
            ++n;
        }
        t += double(chunk) / VE_AUDIO_RATE;
        if (n >= VE_AUDIO_RATE * 2) {
            std::printf("t=%7.2f  rms=%.4f\n", t, std::sqrt(sum / n));
            sum = 0;
            n = 0;
        }
    }
    ve_timeline_destroy(tl);
    return 0;
}

int bench(const char* path, double sec, double len)
{
    ve_media_info info;
    if (int rc = ve_probe(path, &info))
        return fail("probe", rc);
    ve_clip c {};
    c.path = path;
    c.duration = info.duration_sec;
    c.use_video = 1;
    c.volume = 1.0f;
    ve_timeline* tl = ve_timeline_create();
    ve_timeline_set_clips(tl, &c, 1);

    const int w = 960, h = 540;
    std::vector<uint8_t> buf(size_t(w) * h * 4);
    double fps = info.fps > 0 ? info.fps : 30;
    int frames = int(len * fps);

    auto t0 = std::chrono::steady_clock::now();
    ve_timeline_render_video(tl, sec, w, h, buf.data()); // the first one includes the seek
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 1; i < frames; ++i)
        ve_timeline_render_video(tl, sec + i / fps, w, h, buf.data());
    auto t2 = std::chrono::steady_clock::now();
    ve_timeline_destroy(tl);

    double seekMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double perFrame = std::chrono::duration<double, std::milli>(t2 - t1).count() / std::max(1, frames - 1);
    std::printf("first frame (seek): %.1f ms\nthen per frame:     %.2f ms (need < %.2f for %.0f fps)\n",
                seekMs, perFrame, 1000.0 / fps, fps);
    return 0;
}

int peaks(const char* path)
{
    ve_media_info info;
    if (int rc = ve_probe(path, &info))
        return fail("probe", rc);
    const int perSecond = 50;
    std::vector<float> out(size_t(info.duration_sec * perSecond) + 16);
    auto t0 = std::chrono::steady_clock::now();
    int n = ve_audio_peaks(path, perSecond, out.data(), int(out.size()));
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (n < 0)
        return fail("peaks", n);
    float loudest = 0;
    for (int i = 0; i < n; ++i)
        loudest = std::max(loudest, out[size_t(i)]);
    std::printf("%d peaks (%.1f s of sound) in %.2f s, loudest %.3f\n", n, double(n) / perSecond, secs, loudest);
    return 0;
}

int exportVideo(const char* out, int w, int h, double fps, std::vector<ve_clip> clips)
{
    ve_timeline* tl = ve_timeline_create();
    ve_timeline_set_clips(tl, clips.data(), int(clips.size()));

    ve_export_settings s {};
    s.path = out;
    s.width = w;
    s.height = h;
    s.fps = fps;
    s.crf = 20;
    s.force_software = std::getenv("VE_SOFTWARE") != nullptr; // VE_SOFTWARE=1 to skip the graphics card
    s.copy_only = std::getenv("VE_COPY") != nullptr;             // VE_COPY=1 for an instant export
    if (s.copy_only) {
        char why[256];
        if (!ve_timeline_can_copy(tl, why, sizeof why)) {
            std::fprintf(stderr, "can't do an instant export: %s\n", why);
            return 1;
        }
    }

    auto t0 = std::chrono::steady_clock::now();
    int rc = ve_export(tl, &s, [](double done, void*) {
        static int last = -1;
        int pct = int(done * 100);
        if (pct / 10 != last / 10) {
            std::printf("  %d%%\n", pct);
            std::fflush(stdout);
        }
        last = pct;
        return 0;
    }, nullptr);
    auto secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ve_timeline_destroy(tl);
    if (rc)
        return fail("export", rc);
    std::printf("done in %.1f s with %s\n", secs, s.encoder_used);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "probe" && argc == 3)
        return probe(argv[2]);
    if (cmd == "frame" && argc == 5)
        return frame(argv[2], std::atof(argv[3]), argv[4]);
    if (cmd == "render" && argc >= 11)
        return render(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atof(argv[5]), parseClips(argc, argv, 6));
    if (cmd == "audio" && argc == 5)
        return audio(argv[2], std::atof(argv[3]), std::atof(argv[4]));
    if (cmd == "peaks" && argc == 3)
        return peaks(argv[2]);
    if (cmd == "bench" && argc == 5)
        return bench(argv[2], std::atof(argv[3]), std::atof(argv[4]));
    if (cmd == "export" && argc >= 11)
        return exportVideo(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atof(argv[5]), parseClips(argc, argv, 6));

    std::fprintf(stderr, "usage: see the top of tools/ve_cli.cpp\n");
    return 1;
}
