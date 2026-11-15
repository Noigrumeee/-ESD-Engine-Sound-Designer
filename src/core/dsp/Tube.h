#pragma once
// 1D digital waveguide approximation of a pipe (APPROXIMATION, documented in docs/MODEL.md).
//
// A pressure wave entering the pipe travels to the far end, part of it is reflected (open end:
// negative reflection, frequency dependent because short wavelengths radiate), travels back,
// reflects again at the source end, and so on. Lumping both ends into the feedback path gives
//      y[n] = x[n] + R * LP( y[n - D] ),   D = 2 L / c * fs  (round trip)
// whose peaks sit at the pipe resonances (odd harmonics of c/4L for R < 0).
// The delay buffer is allocated in prepare() only.
#include "Filters.h"

#include <vector>

namespace esd::dsp
{
class FractionalDelay
{
public:
    void prepare (int maxSamples);
    void reset();
    inline void push (float x)
    {
        buf[static_cast<std::size_t> (w)] = x;
        w = (w + 1) & mask;
    }
    /// Reads 'delay' samples back (>= 1), cubic Hermite interpolation.
    float read (float delay) const;

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

class Tube
{
public:
    void prepare (double sampleRate, double maxLengthM, double minSoundSpeed = 300.0);
    void reset();
    /// lengthM: pipe length; c: speed of sound; reflection: -1..1 at low frequency;
    /// diameterM: sets the radiation/loss cutoff of the loop filter.
    void set (double lengthM, double c, double reflection, double diameterM);
    inline float process (float x)
    {
        const float fb = delay.read (delaySamples);
        const float y = x + refl * loop.process (fb);
        delay.push (y);
        return y;
    }
    float delaySamplesNow() const { return delaySamples; }

private:
    double fs = 48000.0;
    FractionalDelay delay;
    OnePoleLP loop;
    float delaySamples = 10.0f;
    float refl = 0.0f;
    float maxDelay = 10.0f;
};
} // namespace esd::dsp
