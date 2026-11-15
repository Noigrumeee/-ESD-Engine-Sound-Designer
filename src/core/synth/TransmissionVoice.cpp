#include "TransmissionVoice.h"
#include "../base/Math.h"

#include <cmath>

namespace esd
{
namespace
{
constexpr float kTransmissionLevel = 0.05f;
}

void TransmissionVoice::prepare (const TransmissionSpec& t, double sampleRate, uint64_t seed)
{
    fs = sampleRate;
    teeth = std::max (8, t.pinionTeeth);
    rng.reseed (seed ^ 0x6EA2u);
    clunkLp.setLowpass (fs, 380.0, 0.9);
    slipBand.setBandpass (fs, 650.0, 0.8);
    chainBand.setBandpass (fs, 2400.0, 2.0);
    torqueSm.setTime (fs, 0.02);
    engageSm.setTime (fs, 0.02);
}

void TransmissionVoice::process (const TransmissionVoiceInput& in, LayerBus& bus, int offset, int n)
{
    const int l = static_cast<int> (LayerId::Transmission);
    if (in.shiftCount != lastShift)
    {
        lastShift = in.shiftCount;
        clunkEnv = 1.0f;
        clunkStrength = 0.3f + 0.7f * in.shiftStrength;
        thumpPhase = 0.0;
    }
    const float decay = static_cast<float> (std::exp (-1.0 / (0.07 * fs)));
    const double nyq = fs * 0.45;
    double sq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (i + 0.5) / n;
        const double w = in.inputOmegaStart + (in.inputOmegaEnd - in.inputOmegaStart) * t;
        const double rev = std::max (0.0, w) / kTwoPi;
        const float tq = torqueSm.next (in.torque);
        const float eng = engageSm.next (in.engaged ? 1.0f : 0.0f);
        float out = 0.0f;
        if (rev > 0.2 && eng > 0.001f)
        {
            // drive and coast flanks sound different: coast is quieter and slightly brighter
            const float load = std::pow (std::min (1.0f, std::abs (tq) / std::max (1.0f, in.peakTorque)), 0.8f) * (tq < 0.0f ? 0.6f : 1.0f);
            const float spd = static_cast<float> (std::min (1.0, rev / 100.0));
            const double f[4] = {
                teeth * rev,                                              // input constant mesh
                teeth * 0.8 * rev / std::max (0.3, in.gearRatio) * 1.7,   // selected gear pair
                11.0 * rev / std::max (0.3, in.gearRatio),                // final drive pinion
                in.cvt ? 30.0 * rev : 0.0                                 // CVT chain pitch
            };
            const float a[4] = { 1.0f, 0.6f, 0.4f, in.cvt ? 0.5f : 0.0f };
            for (int k = 0; k < 4; ++k)
            {
                ph[k] += f[k] / fs;
                ph[k] -= std::floor (ph[k]);
                if (f[k] > 0.0 && f[k] < nyq) out += static_cast<float> (std::sin (kTwoPi * ph[k])) * a[k];
            }
            out *= (0.15f + 0.85f * load) * (0.2f + 0.8f * spd) * eng;
            if (in.cvt) out += chainBand.process (rng.bipolar()) * 0.15f * spd;
        }
        if (in.converterSlip > 0.02f && rev > 2.0)
            out += slipBand.process (rng.bipolar()) * in.converterSlip * static_cast<float> (std::min (1.0, rev / 60.0)) * 0.8f;
        if (clunkEnv > 1e-4f)
        {
            thumpPhase += 95.0 / fs;
            out += (clunkLp.process (rng.bipolar()) * 2.5f + static_cast<float> (std::sin (kTwoPi * thumpPhase)) * 0.8f) * clunkEnv * clunkStrength;
            clunkEnv *= decay;
        }
        out *= kTransmissionLevel;
        sq += static_cast<double> (out) * out;
        if (bus.L[l]) bus.L[l][offset + i] += out * 0.7071f;
        if (bus.R[l]) bus.R[l][offset + i] += out * 0.7071f;
    }
    level = static_cast<float> (std::sqrt (sq / std::max (1, n)));
}
} // namespace esd
