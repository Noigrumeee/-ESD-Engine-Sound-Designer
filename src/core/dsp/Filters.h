#pragma once
// Basic real-time safe filters. Coefficient updates use only arithmetic and trig: allowed on
// the audio thread (bounded cost, no allocation).
#include "../base/Math.h"

#include <cmath>

namespace esd::dsp
{
/// RBJ cookbook biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0f; }

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowpass (double fs, double f, double q) { design (0, fs, f, q, 0.0); }
    void setHighpass (double fs, double f, double q) { design (1, fs, f, q, 0.0); }
    /// Band-pass with 0 dB peak gain.
    void setBandpass (double fs, double f, double q) { design (2, fs, f, q, 0.0); }
    void setNotch (double fs, double f, double q) { design (3, fs, f, q, 0.0); }
    void setPeak (double fs, double f, double q, double gainDb) { design (4, fs, f, q, gainDb); }
    void setLowShelf (double fs, double f, double gainDb) { design (5, fs, f, 0.707, gainDb); }
    void setHighShelf (double fs, double f, double gainDb) { design (6, fs, f, 0.707, gainDb); }
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    /// Magnitude response at frequency f (for plots and tests).
    double magnitude (double fs, double f) const;

private:
    void design (int type, double fs, double f, double q, double gainDb);
};

/// One-pole low-pass: y += a (x - y).
struct OnePoleLP
{
    float a = 1.0f, y = 0.0f;
    void setCutoff (double fs, double fc) { a = static_cast<float> (1.0 - std::exp (-kTwoPi * clampv (fc, 1.0, fs * 0.49) / fs)); }
    void reset (float v = 0.0f) { y = v; }
    inline float process (float x) { return y += a * (x - y); }
};

/// DC blocker (one-zero one-pole high-pass).
struct DcBlocker
{
    float r = 0.995f, x1 = 0.0f, y1 = 0.0f;
    void setCutoff (double fs, double fc) { r = static_cast<float> (std::exp (-kTwoPi * fc / fs)); }
    void reset() { x1 = y1 = 0.0f; }
    inline float process (float x)
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

/// Exponential parameter smoother (per-sample).
struct Smoother
{
    float coeff = 0.001f, value = 0.0f;
    void setTime (double fs, double seconds) { coeff = static_cast<float> (1.0 - std::exp (-1.0 / (std::max (seconds, 1e-4) * fs))); }
    void snap (float v) { value = v; }
    inline float next (float target) { return value += coeff * (target - value); }
};
} // namespace esd::dsp
