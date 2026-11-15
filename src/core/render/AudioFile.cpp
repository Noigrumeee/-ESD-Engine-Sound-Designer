#include "AudioFile.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace esd
{
bool readWav (const std::string& path, AudioData& out, std::string* error)
{
    auto fail = [error] (const std::string& m) { if (error) *error = m; return false; };
    std::ifstream f (std::filesystem::path (reinterpret_cast<const char8_t*> (path.c_str())), std::ios::binary);
    if (! f) return fail ("cannot open " + path);
    std::vector<char> data ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::memcmp (data.data(), "RIFF", 4) != 0 || std::memcmp (data.data() + 8, "WAVE", 4) != 0) return fail ("not a RIFF/WAVE file");
    auto u16 = [&] (std::size_t o) { uint16_t v; std::memcpy (&v, data.data() + o, 2); return v; };
    auto u32 = [&] (std::size_t o) { uint32_t v; std::memcpy (&v, data.data() + o, 4); return v; };
    std::size_t pos = 12;
    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    const char* pcm = nullptr;
    std::size_t pcmBytes = 0;
    while (pos + 8 <= data.size())
    {
        const uint32_t size = u32 (pos + 4);
        const std::size_t body = pos + 8;
        if (body + size > data.size()) break;
        if (std::memcmp (data.data() + pos, "fmt ", 4) == 0 && size >= 16)
        {
            fmt = u16 (body);
            ch = u16 (body + 2);
            rate = u32 (body + 4);
            bits = u16 (body + 14);
            if (fmt == 0xFFFE && size >= 26) fmt = u16 (body + 24); // WAVE_FORMAT_EXTENSIBLE sub-format
        }
        else if (std::memcmp (data.data() + pos, "data", 4) == 0)
        {
            pcm = data.data() + body;
            pcmBytes = size;
        }
        pos = body + size + (size & 1u);
    }
    if (! pcm || ch == 0 || rate == 0) return fail ("missing fmt or data chunk");
    const std::size_t bytes = bits / 8u;
    if (bytes == 0) return fail ("unsupported bit depth");
    const std::size_t frames = pcmBytes / (bytes * ch);
    out.sampleRate = rate;
    out.channels.assign (ch, std::vector<float> (frames));
    for (std::size_t i = 0; i < frames; ++i)
        for (std::size_t c = 0; c < ch; ++c)
        {
            const char* p = pcm + (i * ch + c) * bytes;
            float v = 0.0f;
            if (fmt == 3 && bits == 32) std::memcpy (&v, p, 4);
            else if (fmt == 3 && bits == 64) { double d; std::memcpy (&d, p, 8); v = static_cast<float> (d); }
            else if (bits == 8) v = (static_cast<uint8_t> (*p) - 128) / 128.0f;
            else if (bits == 16) { int16_t s; std::memcpy (&s, p, 2); v = s / 32768.0f; }
            else if (bits == 24)
            {
                int32_t s = (static_cast<uint8_t> (p[0])) | (static_cast<uint8_t> (p[1]) << 8) | (static_cast<int8_t> (p[2]) << 16);
                v = s / 8388608.0f;
            }
            else if (bits == 32) { int32_t s; std::memcpy (&s, p, 4); v = static_cast<float> (s / 2147483648.0); }
            else return fail ("unsupported sample format");
            out.channels[c][i] = v;
        }
    return true;
}

std::vector<float> resampleLinear (const std::vector<float>& in, double fromRate, double toRate)
{
    if (in.empty() || fromRate <= 0 || toRate <= 0 || std::abs (fromRate - toRate) < 1e-6) return in;
    const double ratio = fromRate / toRate;
    const auto n = static_cast<std::size_t> (static_cast<double> (in.size()) / ratio);
    std::vector<float> out (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double p = static_cast<double> (i) * ratio;
        const auto k = static_cast<std::size_t> (p);
        const float f = static_cast<float> (p - static_cast<double> (k));
        const float a = in[std::min (k, in.size() - 1)], b = in[std::min (k + 1, in.size() - 1)];
        out[i] = a + (b - a) * f;
    }
    return out;
}
} // namespace esd
