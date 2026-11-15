#pragma once
// Electric machine sound model (procedural, its own model - not a thermal engine without
// combustion). Frequencies come from the machine data (see docs/MODEL.md):
//   fe   = rpm/60 x pole pairs (/ (1 - slip) for induction)
//   radial magnetic force harmonics at 2 fe x k (strong 6k fe with six-step commutation)
//   slot passing: slots x rev (+- 2 fe), rotor bar passing (induction): bars x rev
//   cogging: lcm(slots, poles) x rev (PM machines)
//   PWM: carrier fpwm and sidebands fpwm +- 2 fe, 2 fpwm +- fe ; random PWM -> noise band
//   bearings: outer / inner race pass frequencies 0.4 / 0.6 x balls x rev
//   cooling: fan blade pass (air) or pump hum (liquid), driven by temperature
//   structure: two housing modes excited by the magnetic forces
#include "../base/Random.h"
#include "../dsp/Filters.h"
#include "../sim/ElectricSim.h"
#include "LayerBus.h"

namespace esd
{
struct MotorVoiceInput
{
    double omegaStart = 0.0, omegaEnd = 0.0;
    float torque = 0.0f, currentA = 0.0f, tempC = 30.0f, slip = 0.0f, loadFraction = 0.0f;
    bool regen = false;
    bool enabled = true; ///< inverter on
};

class ElectricVoice
{
public:
    void prepare (const ElectricSpec&, double fs, uint64_t seed);
    void process (const MotorVoiceInput&, LayerBus& bus, int offset, int n);
    const std::array<float, kNumLayers>& layerRms() const { return rms; }

private:
    struct Osc
    {
        double phase = 0.0;
        inline float next (double hz, double fs)
        {
            phase += hz / fs;
            phase -= std::floor (phase);
            return static_cast<float> (std::sin (kTwoPi * phase));
        }
    };
    ElectricSpec m;
    double fs = 48000.0;
    Rng rng;
    Osc em[6], whine[4], rotor[3], pwm[5], fan, pump, pump2;
    double bearingPhaseO = 0.0, bearingPhaseI = 0.0;
    dsp::Biquad bearingBand, randomPwmBand, fanNoise, mode1, mode2, tickHp;
    dsp::Smoother torqueSm, currentSm, tempSm;
    float tickEnv = 0.0f;
    double lastSector = 0.0;
    int cogOrder = 0;
    std::array<float, kNumLayers> rms {};
};
} // namespace esd
