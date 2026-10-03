// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Shayan Mazahir. Part of MixMedia Video Editor, see NOTICE.

// The exports that aren't an MP4: animated GIFs, and sound on its own (MP3 or M4A).

#include "exporter.h"

#include "ve/engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ve {

namespace {

struct Output {
    AVFormatContext* fmt = nullptr;
    ~Output()
    {
        if (!fmt)
            return;
        if (!(fmt->oformat->flags & AVFMT_NOFILE) && fmt->pb)
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
    }
};

bool writePackets(AVCodecContext* enc, const AVFrame* frame, AVStream* st, AVFormatContext* fmt, AVPacket* pkt)
{
    if (avcodec_send_frame(enc, frame) < 0)
        return false;
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

bool openFile(Output& out, const std::string& path)
{
    if (avformat_alloc_output_context2(&out.fmt, nullptr, nullptr, path.c_str()) < 0 || !out.fmt)
        return false;
    return true;
}

bool startWriting(Output& out, const std::string& path)
{
    if (!(out.fmt->oformat->flags & AVFMT_NOFILE) && avio_open(&out.fmt->pb, path.c_str(), AVIO_FLAG_WRITE) < 0)
        return false;
    return avformat_write_header(out.fmt, nullptr) >= 0;
}

// ---- GIF colours ----
// A GIF only gets 256 colours. We look at a handful of frames, pick the 256 that matter most
// (median cut), then a lookup table turns any colour into its closest pick in one step.

constexpr int Bits = 5; // colours get bucketed into 32 x 32 x 32 boxes
constexpr int Side = 1 << Bits;

struct Bucket {
    uint64_t count = 0;
    uint64_t r = 0, g = 0, b = 0;
};

int bucketOf(int r, int g, int b) { return ((r >> 3) << (2 * Bits)) | ((g >> 3) << Bits) | (b >> 3); }

std::vector<uint32_t> medianCut(const std::vector<Bucket>& buckets, int colours)
{
    struct Box {
        std::vector<int> ids; // which buckets are in it
        uint64_t weight = 0;
    };
    auto channel = [](int id, int c) { return c == 0 ? id >> (2 * Bits) : c == 1 ? (id >> Bits) & (Side - 1) : id & (Side - 1); };

    std::vector<Box> boxes(1);
    for (int id = 0; id < Side * Side * Side; ++id)
        if (buckets[id].count) {
            boxes[0].ids.push_back(id);
            boxes[0].weight += buckets[id].count;
        }
    if (boxes[0].ids.empty())
        return { 0xFF000000u };

    while (int(boxes.size()) < colours) {
        // Split the busiest box that still can be split, along its widest colour
        int pick = -1, axis = 0;
        double best = -1;
        for (int i = 0; i < int(boxes.size()); ++i) {
            if (boxes[i].ids.size() < 2)
                continue;
            for (int c = 0; c < 3; ++c) {
                int lo = Side, hi = -1;
                for (int id : boxes[i].ids) {
                    lo = std::min(lo, channel(id, c));
                    hi = std::max(hi, channel(id, c));
                }
                double score = double(hi - lo) * std::sqrt(double(boxes[i].weight));
                if (hi > lo && score > best) {
                    best = score;
                    pick = i;
                    axis = c;
                }
            }
        }
        if (pick < 0)
            break; // every colour has its own box already

        Box& box = boxes[pick];
        std::sort(box.ids.begin(), box.ids.end(), [&](int a, int b) { return channel(a, axis) < channel(b, axis); });
        uint64_t half = box.weight / 2, seen = 0;
        size_t cut = 1;
        for (; cut < box.ids.size(); ++cut) {
            seen += buckets[box.ids[cut - 1]].count;
            if (seen >= half)
                break;
        }
        cut = std::clamp<size_t>(cut, 1, box.ids.size() - 1);
        Box other;
        other.ids.assign(box.ids.begin() + long(cut), box.ids.end());
        box.ids.resize(cut);
        box.weight = 0;
        for (int id : box.ids)
            box.weight += buckets[id].count;
        for (int id : other.ids)
            other.weight += buckets[id].count;
        boxes.push_back(std::move(other));
    }

    std::vector<uint32_t> palette;
    for (const Box& box : boxes) {
        uint64_t n = 0, r = 0, g = 0, b = 0;
        for (int id : box.ids) {
            n += buckets[id].count;
            r += buckets[id].r;
            g += buckets[id].g;
            b += buckets[id].b;
        }
        n = std::max<uint64_t>(n, 1);
        palette.push_back(0xFF000000u | uint32_t(r / n) << 16 | uint32_t(g / n) << 8 | uint32_t(b / n));
    }
    return palette;
}

} // namespace

int exportGif(Timeline& timeline, const ExportSettings& s, ProgressFn progress, void* user)
{
    const int w = s.width, h = s.height;
    const double duration = timeline.duration();
    if (w < 2 || h < 2 || s.fps <= 0 || duration <= 0 || s.path.empty())
        return VE_ERR_ARG;
    quietLogs();

    const int64_t totalFrames = std::max<int64_t>(1, int64_t(std::ceil(duration * s.fps)));
    std::vector<uint8_t> canvas(size_t(w) * h * 4);

    // 1. Peek at some frames to see which colours the video uses
    std::vector<Bucket> buckets(Side * Side * Side);
    const int samples = int(std::min<int64_t>(totalFrames, 24));
    for (int i = 0; i < samples; ++i) {
        double t = (i + 0.5) * duration / samples;
        timeline.renderVideo(t, w, h, canvas.data());
        for (size_t p = 0; p < size_t(w) * h; ++p) {
            const uint8_t* px = &canvas[p * 4];
            Bucket& bk = buckets[size_t(bucketOf(px[0], px[1], px[2]))];
            ++bk.count;
            bk.r += px[0];
            bk.g += px[1];
            bk.b += px[2];
        }
        if (progress && progress(0.1 * (i + 1) / samples, user))
            return VE_ERR_CANCELLED;
    }
    std::vector<uint32_t> palette = medianCut(buckets, 256);

    // Closest palette colour for every bucket, worked out once
    std::vector<uint8_t> nearest(Side * Side * Side);
    for (int id = 0; id < Side * Side * Side; ++id) {
        int r = ((id >> (2 * Bits)) << 3) + 4, g = (((id >> Bits) & (Side - 1)) << 3) + 4, b = ((id & (Side - 1)) << 3) + 4;
        int best = 0, bestDist = 1 << 30;
        for (int k = 0; k < int(palette.size()); ++k) {
            int dr = r - int(palette[k] >> 16 & 255), dg = g - int(palette[k] >> 8 & 255), db = b - int(palette[k] & 255);
            int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db; // eyes notice green most, then blue, then red
            if (d < bestDist) {
                bestDist = d;
                best = k;
            }
        }
        nearest[size_t(id)] = uint8_t(best);
    }

    // 2. The encoder and the file
    Output out;
    if (!openFile(out, s.path))
        return VE_ERR_ENCODE;
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_GIF);
    if (!codec)
        return VE_ERR_ENCODE;
    CodecPtr enc(avcodec_alloc_context3(codec));
    enc->width = w;
    enc->height = h;
    enc->pix_fmt = AV_PIX_FMT_PAL8;
    enc->time_base = av_inv_q(av_d2q(s.fps, 1000));
    if (avcodec_open2(enc.get(), codec, nullptr) < 0)
        return VE_ERR_ENCODE;
    AVStream* st = avformat_new_stream(out.fmt, nullptr);
    if (!st || avcodec_parameters_from_context(st->codecpar, enc.get()) < 0)
        return VE_ERR_ENCODE;
    st->time_base = enc->time_base;
    if (!startWriting(out, s.path))
        return VE_ERR_ENCODE;

    // 3. Every frame: a little ordered dithering (steady from frame to frame, so it doesn't
    // crawl), then the lookup table
    static const int bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    FramePtr frame(av_frame_alloc());
    frame->format = AV_PIX_FMT_PAL8;
    frame->width = w;
    frame->height = h;
    if (av_frame_get_buffer(frame.get(), 0) < 0)
        return VE_ERR_ENCODE;
    PacketPtr pkt(av_packet_alloc());

    for (int64_t i = 0; i < totalFrames; ++i) {
        timeline.renderVideo(i / s.fps, w, h, canvas.data());
        if (av_frame_make_writable(frame.get()) < 0)
            return VE_ERR_ENCODE;
        auto* pal = reinterpret_cast<uint32_t*>(frame->data[1]);
        for (int k = 0; k < 256; ++k)
            pal[k] = k < int(palette.size()) ? palette[size_t(k)] : 0xFF000000u;
        for (int y = 0; y < h; ++y) {
            const uint8_t* src = &canvas[size_t(y) * w * 4];
            uint8_t* dst = frame->data[0] + size_t(y) * frame->linesize[0];
            for (int x = 0; x < w; ++x, src += 4) {
                int nudge = bayer[y & 3][x & 3] - 8; // -8..7, about one bucket's worth
                int r = std::clamp(src[0] + nudge, 0, 255);
                int g = std::clamp(src[1] + nudge, 0, 255);
                int b = std::clamp(src[2] + nudge, 0, 255);
                dst[x] = nearest[size_t(bucketOf(r, g, b))];
            }
        }
        frame->pts = i;
        if (!writePackets(enc.get(), frame.get(), st, out.fmt, pkt.get()))
            return VE_ERR_ENCODE;
        if (progress && progress(0.1 + 0.9 * double(i + 1) / totalFrames, user)) {
            avio_closep(&out.fmt->pb);
            deleteFile(s.path);
            return VE_ERR_CANCELLED;
        }
    }
    if (!writePackets(enc.get(), nullptr, st, out.fmt, pkt.get()) || av_write_trailer(out.fmt) < 0)
        return VE_ERR_ENCODE;
    return VE_OK;
}

int exportSound(Timeline& timeline, const ExportSettings& s, ProgressFn progress, void* user)
{
    const double duration = timeline.duration();
    if (duration <= 0 || s.path.empty())
        return VE_ERR_ARG;
    quietLogs();

    Output out;
    if (!openFile(out, s.path))
        return VE_ERR_ENCODE;
    const AVCodec* codec = avcodec_find_encoder(s.format == ExportFormat::Mp3 ? AV_CODEC_ID_MP3 : AV_CODEC_ID_AAC);
    if (!codec)
        return VE_ERR_ENCODE;

    CodecPtr enc(avcodec_alloc_context3(codec));
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    enc->sample_fmt = AV_SAMPLE_FMT_FLTP; // both MP3 (LAME) and AAC take this
    enc->sample_rate = AudioRate;
    av_channel_layout_copy(&enc->ch_layout, &stereo);
    enc->bit_rate = 192000;
    enc->time_base = { 1, AudioRate };
    if (out.fmt->oformat->flags & AVFMT_GLOBALHEADER)
        enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(enc.get(), codec, nullptr) < 0)
        return VE_ERR_ENCODE;
    AVStream* st = avformat_new_stream(out.fmt, nullptr);
    if (!st || avcodec_parameters_from_context(st->codecpar, enc.get()) < 0)
        return VE_ERR_ENCODE;
    st->time_base = enc->time_base;
    if (!startWriting(out, s.path))
        return VE_ERR_ENCODE;

    const int chunk = enc->frame_size > 0 ? enc->frame_size : 1024;
    const int64_t total = std::llround(duration * AudioRate);
    FramePtr frame(av_frame_alloc());
    frame->format = AV_SAMPLE_FMT_FLTP;
    frame->sample_rate = AudioRate;
    frame->nb_samples = chunk;
    av_channel_layout_copy(&frame->ch_layout, &stereo);
    if (av_frame_get_buffer(frame.get(), 0) < 0)
        return VE_ERR_ENCODE;
    std::vector<float> mix(size_t(chunk) * AudioChannels);
    PacketPtr pkt(av_packet_alloc());

    for (int64_t pos = 0; pos < total; pos += chunk) {
        int n = int(std::min<int64_t>(chunk, total - pos));
        timeline.renderAudio(double(pos) / AudioRate, n, mix.data());
        if (av_frame_make_writable(frame.get()) < 0)
            return VE_ERR_ENCODE;
        frame->nb_samples = n;
        auto* left = reinterpret_cast<float*>(frame->data[0]);
        auto* right = reinterpret_cast<float*>(frame->data[1]);
        for (int k = 0; k < n; ++k) {
            left[k] = mix[size_t(k) * 2];
            right[k] = mix[size_t(k) * 2 + 1];
        }
        frame->pts = pos;
        if (!writePackets(enc.get(), frame.get(), st, out.fmt, pkt.get()))
            return VE_ERR_ENCODE;
        if (progress && progress(double(pos + n) / total, user)) {
            avio_closep(&out.fmt->pb);
            deleteFile(s.path);
            return VE_ERR_CANCELLED;
        }
    }
    if (!writePackets(enc.get(), nullptr, st, out.fmt, pkt.get()) || av_write_trailer(out.fmt) < 0)
        return VE_ERR_ENCODE;
    return VE_OK;
}

bool formatAvailable(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Mp3: return avcodec_find_encoder(AV_CODEC_ID_MP3) != nullptr;
    case ExportFormat::Gif: return avcodec_find_encoder(AV_CODEC_ID_GIF) != nullptr;
    default: return true;
    }
}

} // namespace ve
