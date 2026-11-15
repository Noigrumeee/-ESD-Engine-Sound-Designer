#pragma once
// Effects used by the per-layer chains of the Sound Lab. All allocation happens in prepare().
#include "Filters.h"

#include <complex>
#include <vector>

namespace esd::dsp
{
/// tanh waveshaper with approximate loudness compensation.
struct Saturator
{
    float drive = 0.0f;
    inline float process (float x) const
    {
        if (drive <= 1e-4f) return x;
        const float g = 1.0f + drive * 9.0f;
        return std::tanh (x * g) / std::tanh (g * 0.5f) * 0.5f;
    }
};

/// Feed-forward compressor, stereo linked, peak detector with attack / release.
class Compressor
{
public:
    void prepare (double fs);
    void set (float thresholdDb, float ratio) { thr = thresholdDb; rat = std::max (1.0f, ratio); }
    void process (float& l, float& r);
    bool active() const { return thr < -0.05f; }

private:
    float thr = 0.0f, rat = 1.0f, env = 0.0f, att = 0.0f, rel = 0.0f;
};

/// Freeverb-style stereo reverb (8 lowpass-feedback combs + 4 allpasses per channel).
class Reverb
{
public:
    void prepare (double fs);
    void set (float size, float damping);
    void process (float inL, float inR, float& outL, float& outR);

private:
    struct Comb { std::vector<float> buf; int pos = 0; float store = 0; };
    struct Allpass { std::vector<float> buf; int pos = 0; };
    Comb combL[8], combR[8];
    Allpass apL[4], apR[4];
    float feedback = 0.8f, damp = 0.3f;
};

/// In-place radix-2 complex FFT (size power of two).
void fft (std::vector<std::complex<float>>& a, bool inverse);

/// Uniformly partitioned FFT convolution (block 256), mono IR applied to each channel.
class Convolver
{
public:
    static constexpr int kBlock = 256;
    /// ir: impulse response at the processing sample rate (truncated to maxSeconds).
    void prepare (const std::vector<float>& ir, double fs, double maxSeconds = 2.0);
    bool ready() const { return ! parts.empty(); }
    /// Processes one sample per channel; latency = kBlock samples.
    void process (float& l, float& r);

private:
    struct Channel
    {
        std::vector<std::vector<std::complex<float>>> history; // spectra of past input blocks
        int histPos = 0;
        std::vector<float> inBlock, overlap, outBlock;
    };
    void runBlock (Channel&);
    std::vector<std::vector<std::complex<float>>> parts; // IR partition spectra (size 2*kBlock)
    Channel ch[2];
    int pos = 0;
    std::vector<std::complex<float>> work, acc;
};
} // namespace esd::dsp
