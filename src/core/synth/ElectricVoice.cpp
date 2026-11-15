#include "ElectricVoice.h"
#include "../base/Math.h"

#include <cmath>
#include <numeric>

namespace esd
{
namespace
{
// Layer calibration (esd_render --calibrate, PMSM preset at 50 % speed, 70 % torque)
constexpr float kEm = 0.05f, kWhine = 0.05f, kRotor = 0.04f, kBearing = 0.04f, kInverter = 0.02f, kCooling = 0.03f, kRes = 0.4f;

inline void put (LayerBus& bus, LayerId id, int at, float v, float pan, std::array<double, kNumLayers>& sq)
{
    const int l = static_cast<int> (id);
    const float th = (pan + 1.0f) * static_cast<float> (kPi) * 0.25f;
    if (bus.L[l]) bus.L[l][at] += v * std::cos (th);
    if (bus.R[l]) bus.R[l][at] += v * std::sin (th);
    sq[static_cast<std::size_t> (l)] += static_cast<double> (v) * v;
}
} // namespace

void ElectricVoice::prepare (const ElectricSpec& spec, double sampleRate, uint64_t seed)
{
    m = spec;
    fs = sampleRate;
    rng.reseed (seed ^ 0xE1EC7u);
    bearingBand.setBandpass (fs, 4500.0, 1.5);
    randomPwmBand.setBandpass (fs, std::min (m.pwmHz, fs * 0.45), 3.0);
    fanNoise.setLowpass (fs, 1800.0, 0.7);
    mode1.setBandpass (fs, m.resonance1Hz, 18.0);
    mode2.setBandpass (fs, m.resonance2Hz, 22.0);
    tickHp.setHighpass (fs, 2500.0, 0.7);
    torqueSm.setTime (fs, 0.01);
    currentSm.setTime (fs, 0.01);
    tempSm.setTime (fs, 0.5);
    tempSm.snap (30.0f);
    cogOrder = std::lcm (std::max (1, m.slots), std::max (2, m.poles));
    rms.fill (0.0f);
}

void ElectricVoice::process (const MotorVoiceInput& in, LayerBus& bus, int offset, int n)
{
    std::array<double, kNumLayers> sq {};
    const double nyq = fs * 0.45;
    const double pp = m.polePairs();
    const bool sixStep = m.controller == MotorController::SixStep || m.type == ElectricMotorType::BLDC;
    const bool induction = m.type == ElectricMotorType::Induction;
    const float statorK = m.stator == StatorWinding::Concentrated ? 1.5f : m.stator == StatorWinding::Hairpin ? 1.2f : 1.0f;
    const float emK[5] = { 2.0f, 4.0f, 6.0f, 12.0f, 18.0f };
    float emA[5] = { 1.0f, 0.2f, 0.35f, 0.15f, 0.05f };
    if (sixStep) { emA[2] = 0.9f; emA[3] = 0.6f; emA[4] = 0.4f; }
    if (m.controller == MotorController::ScalarVf) { emA[1] = 0.3f; emA[2] = 0.45f; }
    const float axialK = m.type == ElectricMotorType::AxialFlux ? 1.3f : 1.0f;
    const float semiK = m.semiconductor == SemiconductorType::SiIGBT ? 1.0f : 1.6f;

    for (int i = 0; i < n; ++i)
    {
        const double t = (i + 0.5) / n;
        const double omega = in.omegaStart + (in.omegaEnd - in.omegaStart) * t;
        const double rev = omega / kTwoPi;
        const double rpmNorm = clampv (omega * 60.0 / kTwoPi / m.maxRpm, 0.0, 1.1);
        const double fe = rev * pp / std::max (0.5, 1.0 - static_cast<double> (in.slip));
        const float tn = torqueSm.next (static_cast<float> (std::min (1.0, std::abs (in.torque) / std::max (1.0, m.peakTorqueNm))));
        const float cn = currentSm.next (in.enabled ? static_cast<float> (in.currentA / std::max (1.0, m.maxCurrentA)) : 0.0f);
        const float temp = tempSm.next (in.tempC);
        const bool turning = rev > 0.05;

        // ---- 1. electromagnetic (radial force harmonics of the electrical frequency)
        float emOut = 0.0f;
        if (turning)
        {
            const float amp = (0.12f + 0.88f * tn) * statorK;
            for (int k = 0; k < 5; ++k)
            {
                const double f = fe * emK[k];
                const float s = em[k].next (f, fs);
                if (f < nyq) emOut += s * emA[k] * (k == 0 ? axialK : 1.0f);
            }
            if (induction) emOut += em[5].next (2.0 * fe, fs) * 0.35f; // magnetising hum even without load
            emOut *= amp;
        }

        // ---- 2. whine: slot (or rotor bar) passing with +-2fe sidebands
        float whineOut = 0.0f;
        if (turning)
        {
            const double f1 = (induction ? m.rotorBars : m.slots) * rev;
            const double fr[4] = { f1, f1 + 2.0 * fe, std::abs (f1 - 2.0 * fe), 2.0 * f1 };
            const float ar[4] = { 1.0f, 0.4f, 0.4f, 0.3f };
            for (int k = 0; k < 4; ++k)
            {
                const float s = whine[k].next (fr[k], fs);
                if (fr[k] < nyq) whineOut += s * ar[k];
            }
            const float rotorK = m.rotor == RotorType::InteriorPM ? 1.25f : 1.0f;
            whineOut *= (0.1f + 0.9f * std::pow (tn, 0.7f)) * static_cast<float> (std::sqrt (rpmNorm)) * statorK * rotorK
                        * (m.type == ElectricMotorType::AxialFlux ? 0.6f : 1.0f);
        }

        // ---- 3. rotor orders: imbalance (order 1), cogging lcm(slots,poles), torque ripple 2 pp
        float rotorOut = 0.0f;
        if (turning)
        {
            const double fc = cogOrder * rev;
            const float cogA = (m.rotor == RotorType::SurfacePM ? 0.35f : induction ? 0.0f : 0.12f) * (1.0f - 0.7f * tn);
            const float s = rotor[1].next (fc, fs);
            if (fc < nyq) rotorOut += s * cogA;
            rotorOut += rotor[2].next (2.0 * pp * rev, fs) * 0.2f * tn;
        }

        // ---- 4. mechanical vibrations: residual imbalance (order 1) and misalignment (order 2)
        float vibOut = 0.0f;
        if (turning)
            vibOut = (rotor[0].next (rev, fs) + 0.4f * em[5].next (2.0 * rev, fs) * (induction ? 0.0f : 1.0f)) * static_cast<float> (0.1 + rpmNorm * rpmNorm) * 0.6f;

        // ---- 5. bearings (vibration layer of the electric family)
        float bearingOut = 0.0f;
        if (turning)
        {
            bearingPhaseO += 0.4 * m.bearingBalls * rev / fs;
            bearingPhaseI += 0.6 * m.bearingBalls * rev / fs;
            bearingPhaseO -= std::floor (bearingPhaseO);
            bearingPhaseI -= std::floor (bearingPhaseI);
            const float env = 0.5f + 0.25f * static_cast<float> (std::sin (kTwoPi * bearingPhaseO)) + 0.25f * static_cast<float> (std::sin (kTwoPi * bearingPhaseI));
            bearingOut = bearingBand.process (rng.bipolar()) * env * static_cast<float> (std::pow (rpmNorm, 1.3)) * 2.0f;
        }

        // ---- 7. inverter: PWM carrier + sidebands, or random PWM noise; six-step commutation ticks
        float invOut = 0.0f;
        if (in.enabled && (turning || std::abs (in.torque) > 0.5f))
        {
            const float amp = 0.05f + 0.95f * cn;
            if (m.randomPwm)
                invOut = randomPwmBand.process (rng.bipolar()) * amp * 2.5f;
            else
            {
                const double fc = m.pwmHz;
                const double f[5] = { fc, fc + 2.0 * fe, std::abs (fc - 2.0 * fe), 2.0 * fc + fe, std::abs (2.0 * fc - fe) };
                const float a[5] = { 0.3f, 1.0f, 1.0f, 0.5f * semiK, 0.5f * semiK };
                for (int k = 0; k < 5; ++k)
                {
                    const float s = pwm[k].next (f[k], fs);
                    if (f[k] < nyq) invOut += s * a[k];
                }
                invOut *= amp;
            }
            if (sixStep && turning)
            {
                // commutation: 6 sectors per electrical period
                const double sector = std::floor (em[0].phase * 3.0); // em[0] runs at 2 fe -> 6 sectors per fe period
                if (sector != lastSector) tickEnv = 1.0f;
                lastSector = sector;
                invOut += tickHp.process (rng.bipolar()) * tickEnv * (0.3f + cn) * 1.5f;
                tickEnv *= 0.97f;
            }
        }

        // ---- 8. cooling
        float coolOut = 0.0f;
        if (m.cooling == CoolingType::Air)
        {
            const double fanRpm = 1200.0 + 2800.0 * clampv ((temp - 40.0) / 60.0, 0.0, 1.0);
            const float fa = static_cast<float> (fanRpm / 4000.0);
            coolOut = fan.next (fanRpm / 60.0 * 7.0, fs) * 0.3f * fa + fanNoise.process (rng.bipolar()) * fa * fa * 1.2f;
        }
        else
            coolOut = (pump.next (49.0, fs) + 0.5f * pump2.next (98.0, fs)) * 0.12f + fanNoise.process (rng.bipolar()) * 0.05f;

        // ---- 9. structural resonances excited by the magnetic forces
        const float exc = emOut + 0.5f * whineOut;
        const float resOut = mode1.process (exc) + 0.7f * mode2.process (exc);

        const int at = offset + i;
        put (bus, LayerId::Electromagnetic, at, emOut * kEm, 0.0f, sq);
        put (bus, LayerId::Whine, at, whineOut * kWhine, 0.05f, sq);
        put (bus, LayerId::Rotor, at, rotorOut * kRotor, 0.0f, sq);
        put (bus, LayerId::Vibration, at, vibOut * kRotor, 0.0f, sq);
        put (bus, LayerId::Bearings, at, bearingOut * kBearing, -0.1f, sq);
        put (bus, LayerId::Inverter, at, invOut * kInverter, 0.15f, sq);
        put (bus, LayerId::Cooling, at, coolOut * kCooling, -0.3f, sq);
        put (bus, LayerId::Resonance, at, resOut * kRes, 0.0f, sq);
    }
    rms[static_cast<std::size_t> (LayerId::Vibration)] = static_cast<float> (std::sqrt (sq[static_cast<std::size_t> (LayerId::Vibration)] / std::max (1, n)));
    for (int l = static_cast<int> (LayerId::Electromagnetic); l <= static_cast<int> (LayerId::Resonance); ++l)
        rms[static_cast<std::size_t> (l)] = static_cast<float> (std::sqrt (sq[static_cast<std::size_t> (l)] / std::max (1, n)));
}
} // namespace esd
