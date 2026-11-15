#pragma once
// Small math helpers shared by every module. No allocation, no I/O: safe on the audio thread.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace esd
{
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

template <class T> constexpr T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <class T> constexpr T lerp (T a, T b, T t) { return a + (b - a) * t; }

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

/// Wraps an angle into [0, cycle).
inline double wrapDeg (double d, double cycle)
{
    d = std::fmod (d, cycle);
    return d < 0.0 ? d + cycle : d;
}

inline double rpmToRadPerSec (double rpm) { return rpm * kTwoPi / 60.0; }
inline double radPerSecToRpm (double w) { return w * 60.0 / kTwoPi; }

/// Speed of sound in a hot air-like gas [m/s]: c = sqrt(gamma R T), gamma 1.4, R 287 J/(kg K).
/// (Exhaust gas has gamma ~1.33 - the difference is ~3 %, documented as an approximation.)
inline double speedOfSound (double tempK) { return 20.05 * std::sqrt (std::max (tempK, 200.0)); }

/// Smoothstep on [0,1].
inline float smooth01 (float x)
{
    x = clampv (x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

/// Fast soft clipper used only as an output safety stage.
inline float softClip (float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
} // namespace esd
