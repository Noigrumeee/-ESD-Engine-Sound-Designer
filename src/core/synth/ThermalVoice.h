#pragma once
// Thermal engine sound synthesis (procedural). Turns the crank angle + engine state into
// pressure pulses and pushes them through simplified acoustic systems. See docs/MODEL.md:
//
//  combustion events (from the firing plan, exact crank angles)
//    -> per-cylinder exhaust blowdown pulse (crank-angle domain: it shortens in time with rpm)
//    -> per-cylinder primary delay (unequal headers) -> bank collector
//    -> routing (single / dual / X / H)  -> per line: header tube, mid pipe tube, turbine
//       damping, catalyst, resonator notch, muffler transmission loss, tailpipe tube, radiation
//  intake suction pulses -> runner tube -> plenum / airbox Helmholtz -> filter, throttle hiss
//  block-radiated combustion knock, valvetrain ticks, chain whine, starter
//  turbo whistle / spool / blow-off / wastegate, supercharger whine
//  vibration (free forces from the balance table)
//
// prepare() allocates; process() never allocates, locks or does I/O.
#include "../base/Random.h"
#include "../dsp/Filters.h"
#include "../dsp/Tube.h"
#include "../model/Project.h"
#include "../sim/EngineSim.h"
#include "LayerBus.h"

#include <array>
#include <cstdint>

namespace esd
{
inline constexpr int kMaxCylinders = 16;

/// Per-block inputs from the simulation.
struct VoiceInput
{
    double omegaStart = 0.0, omegaEnd = 0.0; ///< rad/s at block start/end (interpolated per sample)
    float airflow = 0.0f;
    float throttle = 0.0f;
    bool combustion = false;
    bool fuelCut = false;
    bool limiterActive = false;
    bool overrunCut = false;
    bool starterActive = false;
    float turboShaft = 0.0f;
    float wastegateOpen = 0.0f;
    float boostBar = 0.0f;
    float egtK = 600.0f;
    uint32_t blowOffCount = 0;
    float blowOffStrength = 0.0f;
};

class ThermalVoice
{
public:
    static constexpr int kShapeSize = 1024;
    void prepare (const Project&, double sampleRate);
    void reset();

    /// Adds its layers (natural placement, before mix gains / FX) into bus[offset .. offset+n).
    void process (const VoiceInput&, LayerBus& bus, int offset, int n);

    double crankAngleDeg() const { return crankDeg; }
    double cycleDeg() const { return cycle; }
    /// Firing frequency at the given rpm (events per second).
    double firingFrequency (double rpm) const { return rpm / 60.0 * numEvents * (360.0 / cycle); }
    /// RMS per layer over the last processed block (meters).
    const std::array<float, kNumLayers>& layerRms() const { return rms; }

private:
    struct Event
    {
        int cyl = 0;
        int bank = 0;
        double angle = 0.0;
    };
    struct CylState
    {
        double lastPhase = 0.0;   ///< for edge detection of the pulse start
        double lastIntake = 0.0;
        float amp = 0.0f;
        float intakeAmp = 0.0f;
        float pop = 0.0f;         ///< afterfire amplitude of the current cycle
        float trim = 1.0f;        ///< cylinder-to-cylinder static variation
        float headerDelayS = 0.0f;///< primary length difference (seconds)
        bool fired = false;
    };
    struct ExhaustLine
    {
        dsp::Tube header, midpipe, tailpipe;
        dsp::OnePoleLP turbine, catalyst;
        dsp::Biquad resonator1, resonator2;
        dsp::Biquad muffler[4];
        dsp::OnePoleLP mufflerLP;
        dsp::DcBlocker dc;
        dsp::OnePoleLP radiationLP;
        float lastX = 0.0f;   ///< for the radiation differentiator
        float mufflerGain = 1.0f;
        float catGain = 1.0f;
        float turbineGain = 1.0f;
    };

    void updateAcoustics (float egtK, float turboShaft);
    float pulseShape (double x) const;

    // configuration
    double fs = 48000.0;
    double cycle = 720.0;
    int numCyl = 4;
    int numEvents = 4;
    int numLines = 1;
    Event events[kMaxCylinders] {};
    CylState cyl[kMaxCylinders] {};
    double evoDeg = 130.0, pulseWidthDeg = 200.0;
    double intakeOffsetDeg = 360.0, intakeWidthDeg = 230.0;
    float pulseA = 1.6f, pulseB = 3.0f, pulseNorm = 1.0f;
    float harshness = 0.4f, variability = 0.3f, overlap = 0.3f, crackle = 0.1f;
    bool ignitionCutLimiter = false, softCutLimiter = false, diesel = false, rotary = false, twoStroke = false;
    float bankCross = 0.0f;  ///< routing cross mix (X/H pipe)
    float linePanL[2] {}, linePanR[2] {};
    bool turbo = false, twinTurbo = false, superchargerRoots = false, superchargerScrew = false, superchargerCentrifugal = false;
    BoostSpec boost;
    ExhaustSpec exhaust;
    IntakeSpec intake;
    ThermalEngineSpec engine;
    BalanceInfo balance;
    double redline = 6500.0;
    float sizeScale = 1.0f; ///< engine size factor for modal frequencies
    float exhaustTrim = 1.0f, intakeTrim = 1.0f;

    float exhaustShape[kShapeSize + 1] {};
    float intakeShape[kShapeSize + 1] {};
    float popShape[kShapeSize + 1] {};

    // state
    double crankDeg = 0.0;
    Rng rng;
    ExhaustLine lines[2];
    dsp::Tube intakeRunner;
    dsp::Biquad airboxRes, plenumRes, intakeFilter, intakeResonator, hissBand;
    dsp::Biquad blockModes[3];
    dsp::Biquad valveModes[3];
    dsp::Biquad whistleNoiseBand, bovBand, bovHP, wastegateBand, starterBand;
    dsp::OnePoleLP bovLP;
    float whistlePhase[3] {};
    float scPhase = 0.0f, chainPhase = 0.0f, altPhase = 0.0f, vibPhase1 = 0.0f, vibPhase2 = 0.0f, starterPhase = 0.0f;
    float bovEnv = 0.0f, bovStrength = 0.0f, surgeEnv = 0.0f, surgePhase = 0.0f;
    uint32_t lastBlowOff = 0;
    float lastEgt = -1.0f, lastShaft = -1.0f;
    int acousticCounter = 0;
    float rippleDepth = 0.0f;
    float levelSmooth = 0.0f;
    std::array<float, kNumLayers> rms {};
    dsp::Smoother airflowSm, throttleSm, shaftSm;
};
} // namespace esd
