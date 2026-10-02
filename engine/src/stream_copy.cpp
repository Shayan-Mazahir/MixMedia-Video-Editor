#include "stream_copy.h"

#include "ffmpeg_util.h"
#include "ve/engine.h"

#include <cmath>
#include <cstdio>

namespace ve {

namespace {

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

// What we're copying from one input stream into one output stream
struct Track {
    int in = -1;
    AVStream* out = nullptr;
    AVRational tb { 1, 1 };
    int64_t lastDts = AV_NOPTS_VALUE;
};

} // namespace

int copyStreams(const std::string& source, const std::vector<CopySegment>& segments,
                const std::string& outPath, ProgressFn progress, void* user)
{
    if (segments.empty())
        return VE_ERR_ARG;

    FormatPtr in = openInput(source.c_str());
    if (!in)
        return VE_ERR_OPEN;

    Track video, audio;
    video.in = av_find_best_stream(in.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio.in = av_find_best_stream(in.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (video.in < 0)
        return VE_ERR_NO_STREAM;

    Output out;
    if (avformat_alloc_output_context2(&out.fmt, nullptr, nullptr, outPath.c_str()) < 0 || !out.fmt)
        return VE_ERR_ENCODE;

    for (Track* t : { &video, &audio }) {
        if (t->in < 0)
            continue;
        AVStream* src = in->streams[t->in];
        t->out = avformat_new_stream(out.fmt, nullptr);
        if (!t->out || avcodec_parameters_copy(t->out->codecpar, src->codecpar) < 0)
            return VE_ERR_ENCODE;
        t->out->codecpar->codec_tag = 0; // let the MP4 muxer pick the right tag
        t->out->time_base = src->time_base;
        t->tb = src->time_base;
    }
    out.fmt->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_ZERO;

    if (!(out.fmt->oformat->flags & AVFMT_NOFILE) && avio_open(&out.fmt->pb, outPath.c_str(), AVIO_FLAG_WRITE) < 0)
        return VE_ERR_ENCODE;
    if (avformat_write_header(out.fmt, nullptr) < 0)
        return VE_ERR_ENCODE;

    double total = 0;
    for (const CopySegment& s : segments)
        total += s.duration;

    const double videoStart = in->streams[video.in]->start_time != AV_NOPTS_VALUE
                                  ? in->streams[video.in]->start_time * av_q2d(video.tb) : 0.0;
    PacketPtr pkt(av_packet_alloc());
    double outputAt = 0.0; // where the next piece starts in the new file (seconds)
    double copiedSoFar = 0.0;

    for (const CopySegment& seg : segments) {
        // Jump to the keyframe at or before where this piece starts. That's where the cut really happens.
        int64_t want = int64_t((videoStart + seg.in) / av_q2d(video.tb));
        if (av_seek_frame(in.get(), video.in, want, AVSEEK_FLAG_BACKWARD) < 0)
            return VE_ERR_DECODE;

        double cutStart = -1; // learnt from the first video keyframe we see
        const double cutEnd = videoStart + seg.in + seg.duration;
        bool videoDone = false;
        bool audioDone = audio.in < 0;

        while (!(videoDone && audioDone) && av_read_frame(in.get(), pkt.get()) >= 0) {
            Track* t = pkt->stream_index == video.in ? &video : pkt->stream_index == audio.in ? &audio : nullptr;
            if (!t || pkt->pts == AV_NOPTS_VALUE) {
                av_packet_unref(pkt.get());
                continue;
            }
            double pts = pkt->pts * av_q2d(t->tb);

            if (cutStart < 0) {
                // Wait for the keyframe so the piece can actually be decoded
                if (t != &video || !(pkt->flags & AV_PKT_FLAG_KEY)) {
                    av_packet_unref(pkt.get());
                    continue;
                }
                cutStart = pts;
            }
            if (pts < cutStart - 0.001) { // sound from just before the keyframe
                av_packet_unref(pkt.get());
                continue;
            }
            // Sound and picture are stored a little out of step, so keep going until both pass the end
            double order = (t == &video && pkt->dts != AV_NOPTS_VALUE) ? pkt->dts * av_q2d(t->tb) : pts;
            if (order >= cutEnd)
                (t == &video ? videoDone : audioDone) = true;
            if (pts >= cutEnd) {
                av_packet_unref(pkt.get());
                continue;
            }

            // Move it to its new spot in the output
            int64_t shift = int64_t(std::llround((outputAt - cutStart) / av_q2d(t->tb)));
            pkt->pts += shift;
            if (pkt->dts != AV_NOPTS_VALUE)
                pkt->dts += shift;
            // Timestamps must always go forwards, even across a cut
            if (pkt->dts != AV_NOPTS_VALUE && t->lastDts != AV_NOPTS_VALUE && pkt->dts <= t->lastDts) {
                pkt->dts = t->lastDts + 1;
                pkt->pts = std::max(pkt->pts, pkt->dts);
            }
            t->lastDts = pkt->dts;

            pkt->stream_index = t->out->index;
            av_packet_rescale_ts(pkt.get(), t->tb, t->out->time_base);
            pkt->pos = -1;
            if (av_interleaved_write_frame(out.fmt, pkt.get()) < 0)
                return VE_ERR_ENCODE;

            if (progress && t == &video && progress(std::min(1.0, (copiedSoFar + pts - cutStart) / total), user)) {
                out.close();
                deleteFile(outPath);
                return VE_ERR_CANCELLED;
            }
        }

        if (cutStart < 0)
            return VE_ERR_DECODE; // never found a keyframe
        double length = cutEnd - cutStart; // a touch longer than asked, because of the keyframe
        outputAt += length;
        copiedSoFar += seg.duration;
    }

    if (av_write_trailer(out.fmt) < 0)
        return VE_ERR_ENCODE;
    if (progress)
        progress(1.0, user);
    return VE_OK;
}

} // namespace ve
