#include "audio_reader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ve {

bool AudioReader::open(const std::string& path)
{
    m_path = path;
    m_fmt.reset();
    m_ctx.reset();
    m_swr.reset();
    m_buf.clear();
    m_bufValid = m_eof = m_flushed = false;

    FormatPtr fmt = openInput(path.c_str());
    if (!fmt)
        return false;
    CodecPtr ctx;
    int idx = openDecoder(fmt.get(), AVMEDIA_TYPE_AUDIO, ctx);
    if (idx < 0)
        return false;

    AVStream* st = fmt->streams[idx];
    m_timeBase = st->time_base;
    m_startPts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    m_fmt = std::move(fmt);
    m_ctx = std::move(ctx);
    m_stream = idx;
    if (!m_pkt) {
        m_pkt.reset(av_packet_alloc());
        m_frame.reset(av_frame_alloc());
    }
    return true;
}

void AudioReader::seekTo(double sec)
{
    int64_t ts = m_startPts + static_cast<int64_t>(std::max(0.0, sec) / av_q2d(m_timeBase));
    if (av_seek_frame(m_fmt.get(), m_stream, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        open(m_path);
        return;
    }
    avcodec_flush_buffers(m_ctx.get());
    m_swr.reset(); // throw away anything the resampler was holding
    m_buf.clear();
    m_bufValid = m_eof = m_flushed = false;
}

bool AudioReader::decodeMore()
{
    while (true) {
        int r = avcodec_receive_frame(m_ctx.get(), m_frame.get());
        if (r == AVERROR_EOF || (r == AVERROR(EAGAIN) && m_flushed)) {
            m_eof = true;
            return false;
        }
        if (r == AVERROR(EAGAIN)) {
            if (av_read_frame(m_fmt.get(), m_pkt.get()) < 0) {
                avcodec_send_packet(m_ctx.get(), nullptr);
                m_flushed = true;
                continue;
            }
            if (m_pkt->stream_index == m_stream)
                avcodec_send_packet(m_ctx.get(), m_pkt.get());
            av_packet_unref(m_pkt.get());
            continue;
        }
        if (r < 0)
            return false;

        if (!m_swr) {
            SwrContext* swr = nullptr;
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, AudioRate,
                                    &m_frame->ch_layout, static_cast<AVSampleFormat>(m_frame->format),
                                    m_frame->sample_rate, 0, nullptr) < 0
                || swr_init(swr) < 0) {
                swr_free(&swr);
                av_frame_unref(m_frame.get());
                return false;
            }
            m_swr.reset(swr);
        }

        if (!m_bufValid) {
            int64_t pts = m_frame->best_effort_timestamp != AV_NOPTS_VALUE ? m_frame->best_effort_timestamp : m_startPts;
            m_bufStart = (pts - m_startPts) * av_q2d(m_timeBase);
            m_bufValid = true;
        }

        int maxOut = swr_get_out_samples(m_swr.get(), m_frame->nb_samples);
        size_t old = m_buf.size();
        m_buf.resize(old + size_t(maxOut) * AudioChannels);
        uint8_t* dst = reinterpret_cast<uint8_t*>(m_buf.data() + old);
        int got = swr_convert(m_swr.get(), &dst, maxOut,
                              const_cast<const uint8_t**>(m_frame->extended_data), m_frame->nb_samples);
        m_buf.resize(old + size_t(std::max(got, 0)) * AudioChannels);
        av_frame_unref(m_frame.get());
        return true;
    }
}

void AudioReader::read(double sec, int frames, float* out)
{
    std::memset(out, 0, sizeof(float) * frames * AudioChannels);
    if (!isOpen() || frames <= 0)
        return;

    // Jumped somewhere else? Seek. Otherwise keep reading where we left off.
    if (!m_bufValid || sec < m_bufStart - 0.01 || sec > bufferEnd() + 0.25)
        seekTo(sec);

    double wantEnd = sec + double(frames) / AudioRate;
    while ((!m_bufValid || bufferEnd() < wantEnd) && !m_eof) {
        if (!decodeMore())
            break;
    }
    if (!m_bufValid)
        return;

    int64_t offset = std::llround((sec - m_bufStart) * AudioRate);
    int64_t available = int64_t(m_buf.size() / AudioChannels);
    for (int i = 0; i < frames; ++i) {
        int64_t idx = offset + i;
        if (idx >= 0 && idx < available) {
            out[i * 2] = m_buf[size_t(idx) * 2];
            out[i * 2 + 1] = m_buf[size_t(idx) * 2 + 1];
        }
    }

    // Toss what we've used so the buffer doesn't keep growing
    int64_t used = std::clamp<int64_t>(offset + frames, 0, available);
    if (used > 0) {
        m_buf.erase(m_buf.begin(), m_buf.begin() + used * AudioChannels);
        m_bufStart += double(used) / AudioRate;
    }
}

} // namespace ve
