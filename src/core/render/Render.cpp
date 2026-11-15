#include "Render.h"
#include "../base/Math.h"
#include "../base/Random.h"
#include "AudioFile.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace esd
{
RenderResult renderScript (const Project& p, const ControlScript& script, const RenderOptions& o)
{
    RenderResult r;
    EngineRuntime rt (p, o.sampleRate, o.loaders);
    if (o.startRunning) rt.resetEngine (true);
    const VoiceMix mix = mixFromProject (p.mix);

    const auto pre = static_cast<std::size_t> (std::max (0.0, o.preRollSeconds) * o.sampleRate);
    const auto frames = static_cast<std::size_t> (std::max (0.0, o.seconds) * o.sampleRate);
    r.mix.sampleRate = o.sampleRate;
    r.mix.left.assign (frames, 0.0f);
    r.mix.right.assign (frames, 0.0f);
    if (o.renderLayers)
    {
        r.layers.resize (kNumLayers);
        for (auto& l : r.layers)
        {
            l.sampleRate = o.sampleRate;
            l.left.assign (frames, 0.0f);
            l.right.assign (frames, 0.0f);
        }
    }

    constexpr int kBlock = 256;
    std::vector<float> tmpL (kBlock), tmpR (kBlock);
    std::vector<std::vector<float>> layL (kNumLayers, std::vector<float> (kBlock)), layR (kNumLayers, std::vector<float> (kBlock));
    float* lp[kNumLayers];
    float* rp[kNumLayers];
    for (int l = 0; l < kNumLayers; ++l)
    {
        lp[l] = layL[static_cast<std::size_t> (l)].data();
        rp[l] = layR[static_cast<std::size_t> (l)].data();
    }

    EngineControls c;
    c.ignition = true;
    std::size_t total = pre + frames;
    std::size_t pos = 0;
    int progressTick = 0;
    while (pos < total)
    {
        if (o.cancel && o.cancel->load())
        {
            r.cancelled = true;
            return r;
        }
        const int n = static_cast<int> (std::min<std::size_t> ({ static_cast<std::size_t> (kBlock), total - pos, pos < pre ? pre - pos : static_cast<std::size_t> (kBlock) }));
        const double t = (static_cast<double> (pos) - static_cast<double> (pre)) / o.sampleRate;
        script (t, c);
        rt.process (c, mix, tmpL.data(), tmpR.data(), n, o.renderLayers ? lp : nullptr, o.renderLayers ? rp : nullptr);
        for (int i = 0; i < n; ++i)
        {
            const std::size_t at = pos + static_cast<std::size_t> (i);
            if (at < pre) continue;
            const std::size_t k = at - pre;
            r.mix.left[k] = tmpL[static_cast<std::size_t> (i)];
            r.mix.right[k] = tmpR[static_cast<std::size_t> (i)];
            if (o.renderLayers)
                for (int l = 0; l < kNumLayers; ++l)
                {
                    r.layers[static_cast<std::size_t> (l)].left[k] = lp[l][i];
                    r.layers[static_cast<std::size_t> (l)].right[k] = rp[l][i];
                }
        }
        pos += static_cast<std::size_t> (n);
        if (o.alignToCycle && pos == pre && pre > 0)
        {
            // run sample by sample until the primary source crosses a cycle boundary (max 1 cycle)
            float dl, dr;
            double prev = rt.primaryAngleDeg();
            const auto maxSteps = static_cast<std::size_t> (o.sampleRate * 0.5);
            for (std::size_t s = 0; s < maxSteps; ++s)
            {
                script (0.0, c);
                rt.process (c, mix, &dl, &dr, 1);
                const double a = rt.primaryAngleDeg();
                if (a < prev) break;
                prev = a;
            }
        }
        if (o.onProgress && ++progressTick % 64 == 0)
            o.onProgress (static_cast<float> (pos) / static_cast<float> (total));
    }
    if (o.onProgress) o.onProgress (1.0f);
    return r;
}

RenderResult renderDyno (const Project& p, double rpm, double load, const RenderOptions& o, bool overrun)
{
    // The pre-roll uses an unlimited ramp so the engine jumps to the operating point and the
    // slow states (exhaust temperature, turbo shaft) settle before the recorded part.
    auto script = [rpm, load, overrun] (double t, EngineControls& c)
    {
        c.mode = DriveMode::Dyno;
        c.ignition = true;
        c.dynoRpm = static_cast<float> (rpm);
        c.dynoLoad = static_cast<float> (load);
        c.throttle = static_cast<float> (load);
        c.dynoRampRpmPerSec = t < 0.0 ? 1.0e7f : 4000.0f;
        c.forceFuelCut = overrun;
    };
    return renderScript (p, script, o);
}

RenderResult renderDemoPull (const Project& p, const RenderOptions& o)
{
    auto script = [] (double t, EngineControls& c)
    {
        c.mode = DriveMode::FreeRev;
        c.ignition = true;
        c.limiterEnabled = true;
        float thr = 0.0f;
        if (t > 1.5 && t < 1.8) thr = 0.6f;          // blip
        else if (t > 3.0 && t < 6.5) thr = 1.0f;     // pull to the limiter
        else if (t > 6.5 && t < 6.7) thr = 0.0f;     // lift
        else if (t > 8.5 && t < 8.9) thr = 0.35f;    // small blip
        c.throttle = thr;
        c.externalLoad = (t > 3.0 && t < 6.5) ? 0.3f : 0.0f; // load during the pull (slower rev rise)
    };
    return renderScript (p, script, o);
}

EngineRuntime::Loaders defaultLoaders (const std::string& baseDir)
{
    namespace fs = std::filesystem;
    auto resolve = [baseDir] (const std::string& p)
    {
        fs::path path (reinterpret_cast<const char8_t*> (p.c_str()));
        if (path.is_relative() && ! baseDir.empty()) path = fs::path (reinterpret_cast<const char8_t*> (baseDir.c_str())) / path;
        const auto u = path.u8string();
        return std::string (u.begin(), u.end());
    };
    EngineRuntime::Loaders l;
    l.samples = [resolve] (const SampleRef& r, LoadedSample& out)
    {
        AudioData a;
        if (! readWav (resolve (r.path), a) || a.channels.empty()) return false;
        out.sampleRate = a.sampleRate;
        out.L = a.channels[0];
        if (a.channels.size() > 1) out.R = a.channels[1];
        return true;
    };
    l.ir = [resolve] (const std::string& path, double rate)
    {
        AudioData a;
        if (! readWav (resolve (path), a) || a.channels.empty()) return std::vector<float> {};
        return resampleLinear (a.channels[0], a.sampleRate, rate);
    };
    return l;
}

namespace
{
void put16 (std::ofstream& f, uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); }
void put32 (std::ofstream& f, uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); }
} // namespace

bool writeWav (const std::string& path, const StereoBuffer& b, WavFormat fmt, bool mono, std::string* error)
{
    namespace fs = std::filesystem;
    const fs::path target (reinterpret_cast<const char8_t*> (path.c_str()));
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories (target.parent_path(), ec);
    std::ofstream f (target, std::ios::binary | std::ios::trunc);
    if (! f)
    {
        if (error) *error = "Cannot write " + path;
        return false;
    }
    const uint16_t channels = mono ? 1 : 2;
    const uint16_t bits = fmt == WavFormat::Pcm16 ? 16 : fmt == WavFormat::Pcm24 ? 24 : 32;
    const uint16_t formatTag = fmt == WavFormat::Float32 ? 3 : 1;
    const uint32_t frames = static_cast<uint32_t> (b.frames());
    const uint32_t blockAlign = channels * bits / 8;
    const uint32_t dataBytes = frames * blockAlign;
    const bool isFloat = fmt == WavFormat::Float32;
    const uint32_t fmtSize = isFloat ? 18 : 16;
    const uint32_t riffSize = 4 + (8 + fmtSize) + (isFloat ? 12 : 0) + (8 + dataBytes);

    f.write ("RIFF", 4); put32 (f, riffSize); f.write ("WAVE", 4);
    f.write ("fmt ", 4); put32 (f, fmtSize); put16 (f, formatTag); put16 (f, channels);
    put32 (f, static_cast<uint32_t> (b.sampleRate)); put32 (f, static_cast<uint32_t> (b.sampleRate) * blockAlign);
    put16 (f, static_cast<uint16_t> (blockAlign)); put16 (f, bits);
    if (isFloat)
    {
        put16 (f, 0);
        f.write ("fact", 4); put32 (f, 4); put32 (f, frames);
    }
    f.write ("data", 4); put32 (f, dataBytes);

    Rng dither (0xD17E);
    std::vector<char> row (blockAlign);
    for (uint32_t i = 0; i < frames; ++i)
    {
        float s[2] = { b.left[i], b.right[i] };
        if (mono) s[0] = 0.5f * (s[0] + s[1]);
        for (uint16_t ch = 0; ch < channels; ++ch)
        {
            const float v = clampv (s[ch], -1.0f, 1.0f);
            char* out = row.data() + ch * (bits / 8);
            if (fmt == WavFormat::Float32)
                std::memcpy (out, &v, 4);
            else if (fmt == WavFormat::Pcm16)
            {
                const float d = (dither.uniform() - dither.uniform()) / 32768.0f; // TPDF dither
                const auto q = static_cast<int16_t> (std::lround (clampv (v + d, -1.0f, 1.0f) * 32767.0f));
                std::memcpy (out, &q, 2);
            }
            else
            {
                const auto q = static_cast<int32_t> (std::lround (v * 8388607.0f));
                out[0] = static_cast<char> (q & 0xff);
                out[1] = static_cast<char> ((q >> 8) & 0xff);
                out[2] = static_cast<char> ((q >> 16) & 0xff);
            }
        }
        f.write (row.data(), static_cast<std::streamsize> (row.size()));
    }
    if (! f)
    {
        if (error) *error = "Write error on " + path;
        return false;
    }
    return true;
}

double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    if (to == 0 || to > x.size()) to = x.size();
    if (from >= to) return 0.0;
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (to - from));
}

double toneMagnitude (const std::vector<float>& x, double fs, double f)
{
    const double w = kTwoPi * f / fs, cw = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (float v : x)
    {
        const double s0 = v + cw * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return std::sqrt (re * re + im * im) / static_cast<double> (std::max<std::size_t> (1, x.size()));
}
} // namespace esd
