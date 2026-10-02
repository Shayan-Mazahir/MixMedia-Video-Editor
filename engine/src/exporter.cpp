#include "exporter.h"

#include "video_encoder.h"
#include "ve/engine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace ve {

namespace {

// Owns the output file and closes it properly however we leave.
struct Output {
    AVFormatContext* fmt = nullptr;

    void close()
    {
        if (!fmt)
            return;
        if (!(fmt->oformat->flags & AVFMT_NOFILE) && fmt->pb)
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
        fmt = nullptr;
    }
    ~Output() { close(); }
};

bool drainPackets(AVCodecContext* enc, AVStream* st, AVFormatContext* fmt, AVPacket* pkt)
{
    while (true) {
        int r = avcodec_receive_packet(enc, pkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
            return true;
        if (r < 0)
            return false;
        av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
        pkt->stream_index = st->index;
        if (av_interleaved_write_frame(fmt, pkt) < 0)
            return false;
    }
}

bool encode(AVCodecContext* enc, const AVFrame* frame, AVStream* st, AVFormatContext* fmt, AVPacket* pkt)
{
    if (avcodec_send_frame(enc, frame) < 0)
        return false;
    return drainPackets(enc, st, fmt, pkt);
}

// A short line of finished frames between the drawing thread and the encoding thread.
class FrameQueue {
public:
    explicit FrameQueue(size_t capacity)
        : m_capacity(capacity)
    {
    }
    ~FrameQueue()
    {
        for (AVFrame* f : m_frames)
            av_frame_free(&f);
    }

    // Waits for room. Returns false (and frees the frame) if the encoder side has given up.
    bool push(AVFrame* frame)
    {
        std::unique_lock lock(m_mutex);
        m_hasRoom.wait(lock, [this] { return m_frames.size() < m_capacity || m_stopped; });
        if (m_stopped) {
            av_frame_free(&frame);
            return false;
        }
        m_frames.push_back(frame);
        m_hasFrame.notify_one();
        return true;
    }

    // Waits for the next frame. nullptr = that's all of them.
    AVFrame* pop()
    {
        std::unique_lock lock(m_mutex);
        m_hasFrame.wait(lock, [this] { return !m_frames.empty() || m_finished; });
        if (m_frames.empty())
            return nullptr;
        AVFrame* frame = m_frames.front();
        m_frames.pop_front();
        m_hasRoom.notify_one();
        return frame;
    }

    void finish() // the drawer has no more frames
    {
        std::lock_guard lock(m_mutex);
        m_finished = true;
        m_hasFrame.notify_all();
    }

    void stop() // the encoder is quitting early
    {
        std::lock_guard lock(m_mutex);
        m_stopped = true;
        m_hasRoom.notify_all();
    }

private:
    size_t m_capacity;
    std::mutex m_mutex;
    std::condition_variable m_hasRoom;
    std::condition_variable m_hasFrame;
    std::deque<AVFrame*> m_frames;
    bool m_finished = false;
    bool m_stopped = false;
};

} // namespace

int exportTimeline(Timeline& timeline, const ExportSettings& s, ProgressFn progress, void* user,
                   std::string* encoderUsed)
{
    // Video encoders want even sizes
    const int w = s.width & ~1;
    const int h = s.height & ~1;
    const double duration = timeline.duration();
    if (w < 2 || h < 2 || s.fps <= 0 || duration <= 0 || s.path.empty())
        return VE_ERR_ARG;

    quietLogs();
    Output out;
    if (avformat_alloc_output_context2(&out.fmt, nullptr, nullptr, s.path.c_str()) < 0 || !out.fmt)
        return VE_ERR_ENCODE;
    const bool globalHeader = out.fmt->oformat->flags & AVFMT_GLOBALHEADER;

    // ---- Video: H.264, on the graphics card if we can ----
    AVRational frameRate = av_d2q(s.fps, 100000);
    VideoEncoder encoder;
    if (!encoder.open(w, h, frameRate, s.crf, globalHeader, s.hardware))
        return VE_ERR_ENCODE;
    AVCodecContext* venc = encoder.context();

    // Encoding on the card? Then decode there too, so frames never have to leave it
    struct CardGuard {
        Timeline& tl;
        ~CardGuard() { tl.setHardwareDevice(nullptr); }
    } cardGuard { timeline };
    timeline.setHardwareDevice(encoder.device());
    if (encoderUsed)
        *encoderUsed = encoder.name();

    AVStream* vst = avformat_new_stream(out.fmt, nullptr);
    if (!vst || avcodec_parameters_from_context(vst->codecpar, venc) < 0)
        return VE_ERR_ENCODE;
    vst->time_base = venc->time_base;
    vst->avg_frame_rate = frameRate;

    // ---- Audio: AAC ----
    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!acodec)
        return VE_ERR_ENCODE;

    CodecPtr aenc(avcodec_alloc_context3(acodec));
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    aenc->sample_rate = AudioRate;
    av_channel_layout_copy(&aenc->ch_layout, &stereo);
    aenc->bit_rate = 192000;
    aenc->time_base = { 1, AudioRate };
    if (globalHeader)
        aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(aenc.get(), acodec, nullptr) < 0)
        return VE_ERR_ENCODE;

    AVStream* ast = avformat_new_stream(out.fmt, nullptr);
    if (!ast || avcodec_parameters_from_context(ast->codecpar, aenc.get()) < 0)
        return VE_ERR_ENCODE;
    ast->time_base = aenc->time_base;

    // ---- Open the file ----
    if (!(out.fmt->oformat->flags & AVFMT_NOFILE) && avio_open(&out.fmt->pb, s.path.c_str(), AVIO_FLAG_WRITE) < 0)
        return VE_ERR_ENCODE;
    if (avformat_write_header(out.fmt, nullptr) < 0)
        return VE_ERR_ENCODE;


    // ---- Two cooks in the kitchen ----
    // A drawing thread decodes and builds frames while this thread encodes them,
    // with a small queue in between so neither has to wait on the other.
    const int64_t totalFrames = std::max<int64_t>(1, int64_t(std::ceil(duration * s.fps)));
    const int64_t totalSamples = std::llround(duration * AudioRate);

    using Clock = std::chrono::steady_clock;
    auto secondsSince = [](Clock::time_point from) {
        return std::chrono::duration<double>(Clock::now() - from).count();
    };

    FrameQueue queue(8);
    std::atomic<bool> drawFailed { false };
    int64_t fastFrames = 0; // only touched by the drawer until it's joined
    double tDraw = 0;

    std::thread drawer([&] {
        SwsPtr toYuv(sws_getContext(w, h, AV_PIX_FMT_RGBA, w, h, AV_PIX_FMT_YUV420P,
                                    SWS_BICUBIC, nullptr, nullptr, nullptr));
        const int* bt709 = sws_getCoefficients(AVCOL_SPC_BT709);
        if (toYuv)
            sws_setColorspaceDetails(toYuv.get(), bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
        std::vector<uint8_t> canvas;

        for (int64_t i = 0; i < totalFrames; ++i) {
            auto started = Clock::now();
            FramePtr frame(av_frame_alloc());
            double t = i / s.fps;

            if (timeline.renderVideoDirect(t, w, h, frame.get())) {
                ++fastFrames;
            } else {
                // The long way round: draw everything in RGBA, then convert
                canvas.resize(size_t(w) * h * 4);
                timeline.renderVideo(t, w, h, canvas.data());
                frame->format = AV_PIX_FMT_YUV420P;
                frame->width = w;
                frame->height = h;
                if (!toYuv || av_frame_get_buffer(frame.get(), 0) < 0) {
                    drawFailed = true;
                    break;
                }
                const uint8_t* src[1] = { canvas.data() };
                const int srcStride[1] = { w * 4 };
                sws_scale(toYuv.get(), src, srcStride, 0, h, frame->data, frame->linesize);
            }

            frame->pts = i;
            frame->duration = 0;
            frame->pict_type = AV_PICTURE_TYPE_NONE; // let the encoder pick its own keyframes
            frame->colorspace = AVCOL_SPC_BT709;
            frame->color_range = AVCOL_RANGE_MPEG;
            tDraw += secondsSince(started);
            if (!queue.push(frame.release()))
                break; // the encoder side gave up
        }
        queue.finish();
    });

    // However we leave this function, make sure the drawer stops and is joined first
    struct StopDrawer {
        std::thread& thread;
        FrameQueue& queue;
        ~StopDrawer()
        {
            queue.stop();
            if (thread.joinable())
                thread.join();
        }
    } stopDrawer { drawer, queue };

    const int samplesPerFrame = aenc->frame_size > 0 ? aenc->frame_size : 1024;
    FramePtr aframe(av_frame_alloc());
    aframe->format = AV_SAMPLE_FMT_FLTP;
    aframe->sample_rate = AudioRate;
    aframe->nb_samples = samplesPerFrame;
    av_channel_layout_copy(&aframe->ch_layout, &stereo);
    if (av_frame_get_buffer(aframe.get(), 0) < 0)
        return VE_ERR_ENCODE;
    std::vector<float> mix(size_t(samplesPerFrame) * AudioChannels);
    PacketPtr pkt(av_packet_alloc());

    // Set VE_EXPORT_STATS=1 to see where the time goes
    double tWait = 0, tEncode = 0, tAudio = 0;
    int64_t audioPos = 0;

    for (int64_t i = 0;; ++i) {
        auto started = Clock::now();
        FramePtr frame(queue.pop());
        tWait += secondsSince(started);
        if (!frame)
            break;

        started = Clock::now();
        AVFrame* ready = encoder.prepare(frame.get());
        if (!ready || !encode(venc, ready, vst, out.fmt, pkt.get()))
            return VE_ERR_ENCODE;
        tEncode += secondsSince(started);

        // Then enough sound to keep up with the picture
        started = Clock::now();
        int64_t audioTarget = std::min<int64_t>(totalSamples, std::llround((i + 1) / s.fps * AudioRate));
        while (audioPos < audioTarget) {
            int n = int(std::min<int64_t>(samplesPerFrame, totalSamples - audioPos));
            timeline.renderAudio(double(audioPos) / AudioRate, n, mix.data());

            if (av_frame_make_writable(aframe.get()) < 0)
                return VE_ERR_ENCODE;
            aframe->nb_samples = n;
            auto* left = reinterpret_cast<float*>(aframe->data[0]);
            auto* right = reinterpret_cast<float*>(aframe->data[1]);
            for (int k = 0; k < n; ++k) {
                left[k] = mix[k * 2];
                right[k] = mix[k * 2 + 1];
            }
            aframe->pts = audioPos;
            if (!encode(aenc.get(), aframe.get(), ast, out.fmt, pkt.get()))
                return VE_ERR_ENCODE;
            audioPos += n;
        }
        tAudio += secondsSince(started);

        if (progress && progress(double(i + 1) / totalFrames, user)) {
            queue.stop();
            drawer.join();
            out.close();
            deleteFile(s.path); // don't leave half a video lying around
            return VE_ERR_CANCELLED;
        }
    }

    drawer.join();
    if (drawFailed)
        return VE_ERR_ENCODE;

    if (std::getenv("VE_EXPORT_STATS"))
        std::fprintf(stderr, "%s: drawing %.2fs (%lld/%lld fast lane) | encoding %.2fs  audio %.2fs  waiting for frames %.2fs\n",
                     encoder.name().c_str(), tDraw, (long long)fastFrames, (long long)totalFrames,
                     tEncode, tAudio, tWait);

    // Squeeze out anything the encoders are still holding on to
    if (!encode(venc, nullptr, vst, out.fmt, pkt.get())
        || !encode(aenc.get(), nullptr, ast, out.fmt, pkt.get()))
        return VE_ERR_ENCODE;
    if (av_write_trailer(out.fmt) < 0)
        return VE_ERR_ENCODE;
    return VE_OK;
}

} // namespace ve
