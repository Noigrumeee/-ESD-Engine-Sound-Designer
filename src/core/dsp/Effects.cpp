#include "Effects.h"

#include <algorithm>

namespace esd::dsp
{
void Compressor::prepare (double fs)
{
    att = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * fs)));
    rel = static_cast<float> (1.0 - std::exp (-1.0 / (0.12 * fs)));
    env = 0.0f;
}

void Compressor::process (float& l, float& r)
{
    if (! active()) return;
    const float peak = std::max (std::abs (l), std::abs (r));
    env += (peak > env ? att : rel) * (peak - env);
    const float lvl = gainToDb (env);
    if (lvl > thr)
    {
        const float g = dbToGain ((thr - lvl) * (1.0f - 1.0f / rat));
        l *= g;
        r *= g;
    }
}

void Reverb::prepare (double fs)
{
    static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int apT[4] = { 556, 441, 341, 225 };
    const double k = fs / 44100.0;
    for (int i = 0; i < 8; ++i)
    {
        combL[i].buf.assign (static_cast<std::size_t> (combT[i] * k), 0.0f);
        combR[i].buf.assign (static_cast<std::size_t> ((combT[i] + 23) * k), 0.0f);
        combL[i].pos = combR[i].pos = 0;
        combL[i].store = combR[i].store = 0.0f;
    }
    for (int i = 0; i < 4; ++i)
    {
        apL[i].buf.assign (static_cast<std::size_t> (apT[i] * k), 0.0f);
        apR[i].buf.assign (static_cast<std::size_t> ((apT[i] + 23) * k), 0.0f);
        apL[i].pos = apR[i].pos = 0;
    }
}

void Reverb::set (float size, float damping)
{
    feedback = 0.7f + 0.28f * clampv (size, 0.0f, 1.0f);
    damp = 0.4f * clampv (damping, 0.0f, 1.0f);
}

void Reverb::process (float inL, float inR, float& outL, float& outR)
{
    const float in = (inL + inR) * 0.015f;
    float l = 0.0f, r = 0.0f;
    auto comb = [this] (Comb& c, float x)
    {
        float& y = c.buf[static_cast<std::size_t> (c.pos)];
        const float o = y;
        c.store = o * (1.0f - damp) + c.store * damp;
        y = x + c.store * feedback;
        if (++c.pos >= static_cast<int> (c.buf.size())) c.pos = 0;
        return o;
    };
    auto ap = [] (Allpass& a, float x)
    {
        float& b = a.buf[static_cast<std::size_t> (a.pos)];
        const float o = -x + b;
        b = x + b * 0.5f;
        if (++a.pos >= static_cast<int> (a.buf.size())) a.pos = 0;
        return o;
    };
    for (int i = 0; i < 8; ++i)
    {
        l += comb (combL[i], in);
        r += comb (combR[i], in);
    }
    for (int i = 0; i < 4; ++i)
    {
        l = ap (apL[i], l);
        r = ap (apR[i], r);
    }
    outL = l;
    outR = r;
}

void fft (std::vector<std::complex<float>>& a, bool inverse)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = (inverse ? 2.0 : -2.0) * kPi / static_cast<double> (len);
        const std::complex<float> wl (static_cast<float> (std::cos (ang)), static_cast<float> (std::sin (ang)));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= static_cast<float> (n);
}

void Convolver::prepare (const std::vector<float>& ir, double fs, double maxSeconds)
{
    parts.clear();
    if (ir.empty()) return;
    const std::size_t len = std::min (ir.size(), static_cast<std::size_t> (maxSeconds * fs));
    const std::size_t nParts = (len + kBlock - 1) / kBlock;
    // energy normalisation so the wet signal has a comparable level
    double e = 0.0;
    for (std::size_t i = 0; i < len; ++i) e += static_cast<double> (ir[i]) * ir[i];
    const float norm = e > 0.0 ? static_cast<float> (1.0 / std::sqrt (e)) : 1.0f;
    for (std::size_t p = 0; p < nParts; ++p)
    {
        std::vector<std::complex<float>> spec (2 * kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            const std::size_t idx = p * kBlock + static_cast<std::size_t> (i);
            spec[static_cast<std::size_t> (i)] = idx < len ? ir[idx] * norm : 0.0f;
        }
        fft (spec, false);
        parts.push_back (std::move (spec));
    }
    for (auto& c : ch)
    {
        c.history.assign (nParts, std::vector<std::complex<float>> (2 * kBlock));
        c.histPos = 0;
        c.inBlock.assign (2 * kBlock, 0.0f);
        c.overlap.assign (kBlock, 0.0f);
        c.outBlock.assign (kBlock, 0.0f);
    }
    work.assign (2 * kBlock, {});
    acc.assign (2 * kBlock, {});
    pos = 0;
}

void Convolver::runBlock (Channel& c)
{
    // input spectrum of [previous block | current block]
    for (int i = 0; i < 2 * kBlock; ++i) work[static_cast<std::size_t> (i)] = c.inBlock[static_cast<std::size_t> (i)];
    fft (work, false);
    c.history[static_cast<std::size_t> (c.histPos)] = work;
    std::fill (acc.begin(), acc.end(), std::complex<float> {});
    const std::size_t n = parts.size();
    for (std::size_t p = 0; p < n; ++p)
    {
        const auto& h = c.history[(static_cast<std::size_t> (c.histPos) + n - p) % n];
        const auto& pr = parts[p];
        for (std::size_t k = 0; k < acc.size(); ++k) acc[k] += h[k] * pr[k];
    }
    c.histPos = static_cast<int> ((static_cast<std::size_t> (c.histPos) + 1) % n);
    fft (acc, true);
    // overlap-save: keep the second half
    for (int i = 0; i < kBlock; ++i) c.outBlock[static_cast<std::size_t> (i)] = acc[static_cast<std::size_t> (kBlock + i)].real();
    // shift input
    std::copy (c.inBlock.begin() + kBlock, c.inBlock.end(), c.inBlock.begin());
}

void Convolver::process (float& l, float& r)
{
    if (parts.empty()) return;
    ch[0].inBlock[static_cast<std::size_t> (kBlock + pos)] = l;
    ch[1].inBlock[static_cast<std::size_t> (kBlock + pos)] = r;
    l = ch[0].outBlock[static_cast<std::size_t> (pos)];
    r = ch[1].outBlock[static_cast<std::size_t> (pos)];
    if (++pos == kBlock)
    {
        pos = 0;
        runBlock (ch[0]);
        runBlock (ch[1]);
    }
}
} // namespace esd::dsp
