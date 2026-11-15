#pragma once
// Transmission layer: gear whine (mesh frequencies from tooth counts and shaft speeds),
// shift clunks, torque converter slip noise, CVT chain / belt whine, EV reducer whine.
#include "../base/Random.h"
#include "../dsp/Filters.h"
#include "LayerBus.h"

#include <cstdint>

namespace esd
{
struct TransmissionVoiceInput
{
    double inputOmegaStart = 0.0, inputOmegaEnd = 0.0; ///< gearbox input shaft
    float torque = 0.0f;    ///< Nm through the input (negative = coast)
    float peakTorque = 300.0f;
    double gearRatio = 1.0;
    bool engaged = false;   ///< a gear is engaged and transmitting
    bool cvt = false;
    float converterSlip = 0.0f; ///< 0..1
    uint32_t shiftCount = 0;
    float shiftStrength = 0.0f;
};

class TransmissionVoice
{
public:
    void prepare (const TransmissionSpec&, double fs, uint64_t seed);
    void process (const TransmissionVoiceInput&, LayerBus& bus, int offset, int n);
    float rms() const { return level; }

private:
    double fs = 48000.0;
    int teeth = 23;
    Rng rng;
    double ph[4] {};
    dsp::Biquad clunkLp, slipBand, chainBand;
    dsp::Smoother torqueSm, engageSm;
    float clunkEnv = 0.0f, clunkStrength = 0.0f, level = 0.0f;
    double thumpPhase = 0.0;
    uint32_t lastShift = 0;
};
} // namespace esd
