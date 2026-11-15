#include "Tube.h"

#include <algorithm>

namespace esd::dsp
{
void FractionalDelay::prepare (int maxSamples)
{
    int size = 16;
    while (size < maxSamples + 8)
        size <<= 1;
    buf.assign (static_cast<std::size_t> (size), 0.0f);
    mask = size - 1;
    w = 0;
}

void FractionalDelay::reset()
{
    std::fill (buf.begin(), buf.end(), 0.0f);
    w = 0;
}

float FractionalDelay::read (float delay) const
{
    delay = clampv (delay, 2.0f, static_cast<float> (mask - 4));
    const int di = static_cast<int> (delay);
    const float f = delay - static_cast<float> (di);
    // samples around the read point: x0 = newer, x1.. older
    const int base = w - di;
    const float xm1 = buf[static_cast<std::size_t> ((base + 1) & mask)];
    const float x0 = buf[static_cast<std::size_t> ((base) & mask)];
    const float x1 = buf[static_cast<std::size_t> ((base - 1) & mask)];
    const float x2 = buf[static_cast<std::size_t> ((base - 2) & mask)];
    // Hermite (4-point, 3rd order) on the sequence xm1, x0, x1, x2 at position f between x0 and x1
    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Tube::prepare (double sampleRate, double maxLengthM, double minSoundSpeed)
{
    fs = sampleRate;
    maxDelay = static_cast<float> (2.0 * maxLengthM / minSoundSpeed * fs) + 4.0f;
    delay.prepare (static_cast<int> (maxDelay) + 8);
    reset();
}

void Tube::reset()
{
    delay.reset();
    loop.reset();
}

void Tube::set (double lengthM, double c, double reflection, double diameterM)
{
    delaySamples = clampv (static_cast<float> (2.0 * lengthM / std::max (c, 100.0) * fs), 1.0f, maxDelay);
    refl = static_cast<float> (clampv (reflection, -0.98, 0.98));
    // ka = 1 cutoff: above it the open end radiates instead of reflecting; also wall losses.
    const double radius = std::max (0.005, diameterM * 0.5);
    loop.setCutoff (fs, std::min (fs * 0.45, c / (kTwoPi * radius) * 0.6));
}
} // namespace esd::dsp
