#pragma once
// Deterministic pseudo random generator (xorshift64*). Every random process in the
// synthesis is seeded from the project seed, so the same project renders the same audio.
#include <cstdint>

namespace esd
{
class Rng
{
public:
    explicit Rng (uint64_t seed = 0x9E3779B97F4A7C15ull) { reseed (seed); }

    void reseed (uint64_t seed)
    {
        s = seed != 0 ? seed : 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < 4; ++i)
            next();
    }

    uint64_t next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ull;
    }

    /// Uniform in [0,1).
    float uniform() { return static_cast<float> (next() >> 40) * (1.0f / 16777216.0f); }
    /// Uniform in [-1,1).
    float bipolar() { return uniform() * 2.0f - 1.0f; }
    /// Approximately normal (Irwin-Hall n=4), mean 0, std 1, bounded to +-3.46.
    float gauss()
    {
        const float a = uniform() + uniform() + uniform() + uniform();
        return (a - 2.0f) * 1.7320508f;
    }

    /// Derives an independent stream from this one (used to give each layer its own stream).
    uint64_t fork (uint64_t salt) { return next() ^ (salt * 0xD6E8FEB86659FD93ull); }

private:
    uint64_t s = 1;
};
} // namespace esd
