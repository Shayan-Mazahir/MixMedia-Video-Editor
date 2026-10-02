#pragma once

#include "ffmpeg_util.h"

#include <string>
#include <vector>

namespace ve {

constexpr int AudioRate = 48000;
constexpr int AudioChannels = 2;

// Pulls sound out of one file, always as 48kHz stereo floats (interleaved L R L R ...).
class AudioReader {
public:
    bool open(const std::string& path);
    bool isOpen() const { return m_fmt != nullptr; }

    // Fills `frames` stereo samples starting at `sec`. Anything we can't find is silence.
    // Returns false if there was nothing there at all (past the end of the file).
    bool read(double sec, int frames, float* out);

private:
    void seekTo(double sec);
    bool decodeMore();
    double bufferEnd() const { return m_bufStart + double(m_buf.size() / AudioChannels) / AudioRate; }

    std::string m_path;
    FormatPtr m_fmt;
    CodecPtr m_ctx;
    int m_stream = -1;
    AVRational m_timeBase { 1, 1 };
    int64_t m_startPts = 0;

    PacketPtr m_pkt;
    FramePtr m_frame;
    SwrPtr m_swr;
    bool m_eof = false;
    bool m_flushed = false;

    std::vector<float> m_buf; // decoded samples waiting to be used
    double m_bufStart = 0.0;  // time of the first sample in m_buf
    bool m_bufValid = false;
};

} // namespace ve
