#include "ThermalVoice.h"
#include "../base/Math.h"

#include <algorithm>
#include <cmath>

namespace esd
{
namespace
{
constexpr int kShapeSize = ThermalVoice::kShapeSize;

inline float lut (const float* t, double x)
{
    if (x <= 0.0 || x >= 1.0) return 0.0f;
    const double p = x * kShapeSize;
    const int i = static_cast<int> (p);
    const float f = static_cast<float> (p - i);
    return t[i] + (t[i + 1] - t[i]) * f;
}

void buildShape (float* t, double a, double b)
{
    const double xm = a / (a + b);
    const double norm = 1.0 / (std::pow (xm, a) * std::pow (1.0 - xm, b));
    for (int i = 0; i <= kShapeSize; ++i)
    {
        const double x = static_cast<double> (i) / kShapeSize;
        t[i] = static_cast<float> (norm * std::pow (x, a) * std::pow (1.0 - x, b));
    }
}

inline void equalPower (float pan, float& l, float& r)
{
    const float th = (clampv (pan, -1.0f, 1.0f) + 1.0f) * static_cast<float> (kPi) * 0.25f;
    l = std::cos (th);
    r = std::sin (th);
}

// Calibration constants: bring every layer to a comparable loudness at 0 dB mix gain.
// Measured with tools/esd_render --calibrate on the built-in presets (see docs/MODEL.md).
constexpr float kExhaustLevel = 8.0f;
constexpr float kIntakeLevel = 0.1f;
constexpr float kCombustionLevel = 0.2f;
constexpr float kMechanicalLevel = 0.14f;
constexpr float kBoostLevel = 0.3f;
constexpr float kVibrationLevel = 0.04f;
} // namespace

void ThermalVoice::prepare (const Project& p, double sampleRate)
{
    fs = sampleRate;
    engine = p.engine;
    exhaust = p.exhaust;
    intake = p.intake;
    boost = p.boost;
    redline = engine.redlineRpm;

    const LayoutDef* layout = findLayout (engine.layoutId);
    if (! layout) layout = findLayout ("i4");
    const CrankVariant* crank = findCrank (*layout, engine.crankId);
    FiringPlan plan = computeFiringPlan (*layout, *crank, engine.bankAngleDeg, engine.firingOrder);
    if (! plan.valid) plan = computeFiringPlan (*layout, *crank, 0.0, {});
    balance = crank->balance;

    cycle = plan.cycleDeg;
    numCyl = std::min (layout->cylinders, kMaxCylinders);
    numEvents = std::min (static_cast<int> (plan.events.size()), kMaxCylinders);
    twoStroke = layout->cycle == CycleType::TwoStroke;
    rotary = layout->cycle == CycleType::Wankel;
    diesel = engine.fuel == FuelType::Diesel;

    const bool dual = exhaust.routing != ExhaustRouting::Single;
    numLines = dual ? 2 : 1;
    for (int i = 0; i < numEvents; ++i)
    {
        const auto& fe = plan.events[static_cast<std::size_t> (i)];
        events[i].cyl = fe.cylinder;
        events[i].angle = fe.angleDeg;
        int bank = fe.bank;
        if (dual && layout->banks == 1)
            bank = fe.cylinder < (numCyl + 1) / 2 ? 0 : 1; // split collector: front / rear half
        events[i].bank = dual ? std::min (bank, 1) : 0;
    }
    bankCross = exhaust.routing == ExhaustRouting::DualXPipe ? 0.35f : exhaust.routing == ExhaustRouting::DualHPipe ? 0.18f : 0.0f;

    // outlet placement
    auto setPan = [this] (int line, float pan) { equalPower (pan, linePanL[line], linePanR[line]); };
    const float side = exhaust.outletPosition == OutletPosition::Left ? -0.6f : exhaust.outletPosition == OutletPosition::Right ? 0.6f : 0.0f;
    if (numLines == 1)
        setPan (0, side);
    else if (exhaust.outletPosition == OutletPosition::Split || exhaust.outletPosition == OutletPosition::Center)
    {
        setPan (0, -0.75f);
        setPan (1, 0.75f);
    }
    else
    {
        setPan (0, side - 0.2f);
        setPan (1, side + 0.2f);
    }

    // valve timing -> pulse windows (crank degrees)
    const double dur = engine.exhaustDurationDeg;
    if (twoStroke)
    {
        evoDeg = 95.0;
        pulseWidthDeg = std::min (dur, 190.0) * 0.8;
        intakeOffsetDeg = 230.0;
        intakeWidthDeg = 130.0;
    }
    else if (rotary)
    {
        evoDeg = 180.0;
        pulseWidthDeg = dur * 0.7;
        intakeOffsetDeg = 90.0;
        intakeWidthDeg = 240.0;
    }
    else
    {
        evoDeg = 180.0 - (dur - 180.0) * 0.6;
        pulseWidthDeg = dur * 0.6;
        intakeOffsetDeg = 360.0 - engine.camOverlapDeg * 0.5;
        intakeWidthDeg = dur * 0.95;
    }

    harshness = static_cast<float> (engine.combustionHarshness);
    variability = static_cast<float> (engine.combustionVariability);
    overlap = static_cast<float> (clampv (engine.camOverlapDeg / 100.0, 0.0, 1.1));
    crackle = static_cast<float> (engine.overrunCrackle);
    ignitionCutLimiter = engine.limiterType == LimiterType::IgnitionCut;
    softCutLimiter = engine.limiterType == LimiterType::SoftCut;

    // pulse shape: faster pressure rise (harsh, high compression, rotary peripheral port) -> smaller exponent
    double a = lerp (2.2, 0.9, static_cast<double> (harshness)) * clampv (10.0 / engine.compressionRatio, 0.7, 1.25);
    if (rotary) a *= 0.7;
    if (diesel) a *= 0.85;
    buildShape (exhaustShape, std::max (0.6, a), 2.6);
    buildShape (intakeShape, 1.6, 1.6);
    buildShape (popShape, 0.5, 5.0);

    // static per-cylinder variation (deterministic from the seed)
    rng.reseed (p.seed);
    const double cHot = speedOfSound (800.0);
    for (int c = 0; c < numCyl; ++c)
    {
        cyl[c] = CylState {};
        cyl[c].trim = 1.0f + 0.04f * rng.bipolar();
        // unequal-length primaries: spread of up to 45 % of the primary length
        const double spread = numCyl > 1 ? (static_cast<double> ((c * 5 + 3) % numCyl) / (numCyl - 1) - 0.5) : 0.0;
        const double extraM = exhaust.unequalLength * 0.9 * spread * exhaust.primaryLengthMm * 1e-3;
        cyl[c].headerDelayS = static_cast<float> (extraM / cHot);
    }

    const double cylL = engine.cylinderDisplacementCc() / 500.0;
    sizeScale = static_cast<float> (std::cbrt (std::max (0.05, cylL)));
    rippleDepth = static_cast<float> (clampv (0.22 / std::pow (static_cast<double> (numEvents), 1.2), 0.004, 0.12));

    turbo = boost.type == BoostType::Turbo || boost.type == BoostType::TwinTurbo || boost.type == BoostType::Twincharged;
    twinTurbo = boost.type == BoostType::TwinTurbo;
    superchargerRoots = boost.type == BoostType::RootsSupercharger || boost.type == BoostType::Twincharged;
    superchargerScrew = boost.type == BoostType::TwinScrewSupercharger;
    superchargerCentrifugal = boost.type == BoostType::CentrifugalSupercharger;

    // level trims so that small and big engines are both usable at 0 dB
    exhaustTrim = static_cast<float> (1.6 / std::pow (static_cast<double> (std::max (1, numEvents)), 0.75));
    intakeTrim = static_cast<float> (1.6 / std::sqrt (static_cast<double> (std::max (1, numEvents))));

    for (auto& line : lines)
    {
        line.header.prepare (fs, 1.6);
        line.midpipe.prepare (fs, 4.2);
        line.tailpipe.prepare (fs, 2.1);
    }
    intakeRunner.prepare (fs, 1.0, 300.0);

    airflowSm.setTime (fs, 0.01);
    throttleSm.setTime (fs, 0.02);
    shaftSm.setTime (fs, 0.03);
    reset();
}

void ThermalVoice::reset()
{
    crankDeg = 0.0;
    for (int c = 0; c < kMaxCylinders; ++c)
    {
        cyl[c].lastPhase = 1e9;
        cyl[c].lastIntake = 1e9;
        cyl[c].amp = cyl[c].intakeAmp = cyl[c].pop = 0.0f;
    }
    for (auto& line : lines)
    {
        line.header.reset();
        line.midpipe.reset();
        line.tailpipe.reset();
        line.turbine.reset();
        line.catalyst.reset();
        line.resonator1.reset();
        line.resonator2.reset();
        for (auto& m : line.muffler) m.reset();
        line.mufflerLP.reset();
        line.dc.reset();
        line.radiationLP.reset();
        line.lastX = 0.0f;
    }
    intakeRunner.reset();
    airboxRes.reset(); plenumRes.reset(); intakeFilter.reset(); intakeResonator.reset(); hissBand.reset();
    for (auto& b : blockModes) b.reset();
    for (auto& b : valveModes) b.reset();
    whistleNoiseBand.reset(); bovBand.reset(); bovHP.reset(); wastegateBand.reset(); starterBand.reset(); bovLP.reset();
    for (auto& ph : whistlePhase) ph = 0.0f;
    scPhase = chainPhase = altPhase = vibPhase1 = vibPhase2 = starterPhase = 0.0f;
    bovEnv = bovStrength = surgeEnv = surgePhase = 0.0f;
    lastEgt = lastShaft = -1.0f;
    acousticCounter = 0;
    airflowSm.snap (0.0f);
    throttleSm.snap (0.0f);
    shaftSm.snap (0.0f);
    rms.fill (0.0f);
    updateAcoustics (650.0f, 0.0f);
}

float ThermalVoice::pulseShape (double x) const { return lut (exhaustShape, x); }

void ThermalVoice::updateAcoustics (float egtK, float turboShaft)
{
    lastEgt = egtK;
    lastShaft = turboShaft;
    const double cEx = speedOfSound (egtK);
    // the gas cools along the line: header hottest, tailpipe ~60 % of the absolute temperature
    const double cMid = speedOfSound (egtK * 0.8), cTail = speedOfSound (std::max (330.0f, egtK * 0.6f));
    const double cAir = 343.0;

    double headerR = -0.25;
    switch (exhaust.header)
    {
        case HeaderType::CastManifold: headerR = -0.18; break;
        case HeaderType::ShortTube: headerR = -0.3; break;
        case HeaderType::LongTube: headerR = -0.42; break;
        case HeaderType::TriY: headerR = -0.36; break;
    }

    const double pipeD = exhaust.pipeDiameterMm * 1e-3;
    const double pipeArea = kPi * 0.25 * pipeD * pipeD;
    const double vol = exhaust.mufflerVolumeL * 1e-3;
    const double chamberLen = 0.3 + 0.012 * exhaust.mufflerVolumeL;
    const double m = std::max (1.01, (vol / chamberLen) / pipeArea);
    const double tlMaxDb = std::min (25.0, 10.0 * std::log10 (1.0 + 0.25 * (m - 1.0 / m) * (m - 1.0 / m)));

    for (int li = 0; li < 2; ++li)
    {
        auto& line = lines[li];
        line.header.set (exhaust.primaryLengthMm * 1e-3, cEx, headerR, exhaust.primaryDiameterMm * 1e-3);
        line.midpipe.set (exhaust.midpipeLengthMm * 1e-3, cMid, -0.35, pipeD);
        line.tailpipe.set (exhaust.tailpipeLengthMm * 1e-3, cTail, -0.45, exhaust.tailpipeDiameterMm * 1e-3);

        if (turbo && (li == 0 || twinTurbo || numLines == 1))
        {
            const double size = boost.turboSize;
            line.turbine.setCutoff (fs, 700.0 + 2600.0 * size + 600.0 * (1.0 - turboShaft));
            line.turbineGain = static_cast<float> (0.42 + 0.35 * size);
        }
        else if (turbo)
        {
            line.turbine.setCutoff (fs, 700.0 + 2600.0 * boost.turboSize);
            line.turbineGain = static_cast<float> (0.42 + 0.35 * boost.turboSize);
        }
        else
        {
            line.turbine.setCutoff (fs, fs * 0.45);
            line.turbineGain = 1.0f;
        }

        switch (exhaust.catalyst)
        {
            case CatalystType::None: line.catalyst.setCutoff (fs, fs * 0.45); line.catGain = 1.0f; break;
            case CatalystType::Standard: line.catalyst.setCutoff (fs, 2600.0); line.catGain = 0.78f; break;
            case CatalystType::HighFlow: line.catalyst.setCutoff (fs, 5200.0); line.catGain = 0.92f; break;
        }

        if (exhaust.resonatorEnabled)
        {
            const double f = cTail / (4.0 * std::max (0.05, exhaust.resonatorLengthMm * 1e-3));
            line.resonator1.setPeak (fs, f, 2.5, -14.0);
            line.resonator2.setPeak (fs, std::min (fs * 0.45, 3.0 * f), 3.0, -9.0);
        }
        else
        {
            line.resonator1.setIdentity();
            line.resonator2.setIdentity();
        }

        for (auto& b : line.muffler) b.setIdentity();
        const double V = exhaust.mufflerVolumeL;
        switch (exhaust.muffler)
        {
            case MufflerType::None:
                line.mufflerLP.setCutoff (fs, fs * 0.45);
                line.mufflerGain = 1.0f;
                break;
            case MufflerType::StraightThrough:
                line.mufflerLP.setCutoff (fs, 600.0 + 2600.0 / (1.0 + V / 15.0));
                line.muffler[0].setHighShelf (fs, 1200.0, -(2.0 + 0.2 * V));
                line.mufflerGain = static_cast<float> (1.0 / (1.0 + V / 45.0));
                break;
            case MufflerType::Chambered:
            case MufflerType::Turbo:
            {
                // expansion chamber transmission loss maxima at odd multiples of c / 4 L
                const double f1 = cTail / (4.0 * chamberLen);
                for (int k = 0; k < 3; ++k)
                    line.muffler[k].setPeak (fs, std::min (fs * 0.45, f1 * (2 * k + 1)), 1.6, -0.6 * tlMaxDb);
                // Helmholtz-like low frequency bump (drone)
                const double neckL = 0.15, fh = cTail / kTwoPi * std::sqrt (pipeArea / (vol * neckL));
                line.muffler[3].setPeak (fs, clampv (fh, 30.0, 400.0), 2.0, 4.0);
                const bool reverse = exhaust.muffler == MufflerType::Turbo;
                line.mufflerLP.setCutoff (fs, (reverse ? 450.0 + 1800.0 / (1.0 + V / 25.0) : 700.0 + 2500.0 / (1.0 + V / 25.0)));
                line.mufflerGain = static_cast<float> ((reverse ? 0.7 : 0.85) / (1.0 + V / 60.0));
                break;
            }
        }

        line.dc.setCutoff (fs, 130.0); // radiation corner (one-pole high-pass)
        const double a = std::max (0.008, exhaust.tailpipeDiameterMm * 1e-3 * 0.5);
        line.radiationLP.setCutoff (fs, std::min (fs * 0.45, cAir / (kTwoPi * a) * 1.5));
    }

    // ---- intake
    const double runnerD = intake.runnerDiameterMm * 1e-3;
    const bool itb = intake.manifold == IntakeManifoldType::IndividualThrottles;
    intakeRunner.set (intake.runnerLengthMm * 1e-3, cAir, itb ? -0.6 : -0.42, runnerD);
    {
        const double d = intake.snorkelDiameterMm * 1e-3, A = kPi * 0.25 * d * d;
        const double Leff = intake.snorkelLengthMm * 1e-3 + 0.85 * d;
        const double fH = cAir / kTwoPi * std::sqrt (A / (intake.airboxVolumeL * 1e-3 * Leff));
        airboxRes.setBandpass (fs, clampv (fH, 20.0, 2000.0), 4.0);
        const double Ar = kPi * 0.25 * runnerD * runnerD * std::max (1, numCyl);
        const double fP = cAir / kTwoPi * std::sqrt (Ar / (intake.plenumVolumeL * 1e-3 * intake.runnerLengthMm * 1e-3));
        plenumRes.setBandpass (fs, clampv (fP, 30.0, 3000.0), 3.0);
    }
    switch (intake.filter)
    {
        case AirFilterType::StockAirbox: intakeFilter.setLowpass (fs, 650.0, 0.7); break;
        case AirFilterType::PanelFilter: intakeFilter.setLowpass (fs, 1500.0, 0.7); break;
        case AirFilterType::OpenCone: intakeFilter.setLowpass (fs, 4500.0, 0.7); break;
        case AirFilterType::VelocityStacks: intakeFilter.setLowpass (fs, 8000.0, 0.7); break;
    }
    if (intake.resonatorEnabled) intakeResonator.setNotch (fs, intake.resonatorFreqHz, 3.0);
    else intakeResonator.setIdentity();
    hissBand.setBandpass (fs, 2500.0 * std::sqrt (65.0 / std::max (20.0, intake.throttleDiameterMm)), 1.2);

    // ---- structure modes (scaled by engine size: bigger parts ring lower)
    const double s = sizeScale;
    blockModes[0].setBandpass (fs, 900.0 / s, 7.0);
    blockModes[1].setBandpass (fs, 1750.0 / s, 6.0);
    blockModes[2].setBandpass (fs, 3200.0 / s, 5.0);
    const double vs = std::sqrt (s);
    valveModes[0].setBandpass (fs, 2400.0 / vs, 14.0);
    valveModes[1].setBandpass (fs, 4100.0 / vs, 12.0);
    valveModes[2].setBandpass (fs, 6500.0 / vs, 10.0);

    bovHP.setHighpass (fs, 600.0, 0.7);
    bovBand.setBandpass (fs, 2300.0, 0.8);
    bovLP.setCutoff (fs, 1400.0);
    wastegateBand.setBandpass (fs, 1800.0, 0.7);
    starterBand.setBandpass (fs, 900.0, 2.0);
}

void ThermalVoice::process (const VoiceInput& in, LayerBus& bus, int offset, int n)
{
    if (n <= 0) return;

    // slow acoustic updates (speed of sound follows exhaust temperature)
    acousticCounter += n;
    if (acousticCounter >= 256)
    {
        acousticCounter = 0;
        if (std::abs (in.egtK - lastEgt) > 12.0f || std::abs (in.turboShaft - lastShaft) > 0.05f)
            updateAcoustics (in.egtK, in.turboShaft);
    }

    const double rpm0 = radPerSecToRpm (in.omegaStart);
    // block constant helpers
    const float idleRatio = static_cast<float> (engine.idleRpm / std::max (100.0, rpm0));
    const float rpmNorm = static_cast<float> (clampv (rpm0 / redline, 0.0, 1.3));
    const float camTick = engine.cam == CamType::OHV ? 1.0f : engine.cam == CamType::SOHC ? 0.6f : engine.cam == CamType::DOHC ? 0.4f : 0.15f;
    const float chainOrder = engine.cam == CamType::DOHC ? 21.0f : engine.cam == CamType::SOHC ? 19.0f : 0.0f;
    const float valveTickAmp = camTick * std::pow (rpmNorm + 0.05f, 0.6f) * static_cast<float> (engine.valvesPerCylinder) / 4.0f;

    // turbo whistle band follows the shaft
    float whistleHz = 0.0f;
    if (turbo)
    {
        whistleHz = static_cast<float> (boost.whistleMinHz + (boost.whistleMaxHz - boost.whistleMinHz) * in.turboShaft);
        whistleNoiseBand.setBandpass (fs, std::max (100.0f, whistleHz * 0.5f), 3.0);
    }
    if (in.blowOffCount != lastBlowOff)
    {
        lastBlowOff = in.blowOffCount;
        if (boost.blowOff == BlowOffType::None) surgeEnv = std::max (surgeEnv, in.blowOffStrength);
        else
        {
            bovEnv = 1.0f;
            bovStrength = in.blowOffStrength;
        }
    }
    const float bovDecay = static_cast<float> (std::exp (-1.0 / ((boost.blowOff == BlowOffType::Atmospheric ? 0.32 : 0.22) * fs)));
    const float surgeDecay = static_cast<float> (std::exp (-1.0 / (0.55 * fs)));

    std::array<double, kNumLayers> sq {};
    const double degPerRad = 180.0 / kPi;
    const float radiationGain = static_cast<float> (fs / (kTwoPi * 200.0));
    const float invN = 1.0f / static_cast<float> (n);

    for (int i = 0; i < n; ++i)
    {
        const double t = (i + 0.5) / n;
        const double omega = in.omegaStart + (in.omegaEnd - in.omegaStart) * t;
        const double rpm = radPerSecToRpm (omega);
        const float af = airflowSm.next (in.airflow);
        const float thr = throttleSm.next (in.throttle);
        const float shaft = shaftSm.next (in.turboShaft);

        // ---- crank angle integration with combustion torque ripple (speed fluctuation)
        double dDeg = omega * degPerRad / fs;
        if (in.combustion && ! in.fuelCut)
        {
            const double r = std::sin (kTwoPi * crankDeg * numEvents / cycle);
            const float depth = rippleDepth * clampv (idleRatio * idleRatio, 0.0f, 1.5f) * (0.4f + 0.6f * af);
            dDeg *= 1.0 + depth * r;
        }
        crankDeg += dDeg;
        if (crankDeg >= cycle) crankDeg -= cycle;
        const double degPerSec = omega * degPerRad;

        float bank[2] = { 0.0f, 0.0f };
        float intakeSum = 0.0f;
        float knockImpulse = 0.0f;
        float tickImpulse = 0.0f;

        for (int e = 0; e < numEvents; ++e)
        {
            const Event& ev = events[e];
            CylState& cs = cyl[ev.cyl];

            // ----- exhaust blowdown pulse
            const double delayDeg = degPerSec * cs.headerDelayS;
            const double ph = wrapDeg (crankDeg - ev.angle - evoDeg - delayDeg, cycle);
            if (ph < cs.lastPhase) // a new cycle started for this cylinder: latch combustion
            {
                cs.pop = 0.0f;
                if (! in.combustion)
                    cs.amp = rpm > 30.0 ? 0.06f : 0.0f; // motoring: compression / pumping pulse only
                else if (in.fuelCut)
                {
                    const bool softFire = softCutLimiter && in.limiterActive && rng.uniform() < 0.5f;
                    cs.amp = softFire ? (0.25f + 0.75f * std::pow (af, 0.8f)) * cs.trim : 0.07f;
                    if (in.limiterActive && ignitionCutLimiter && rng.uniform() < 0.35f)
                        cs.pop = 1.4f + 1.6f * rng.uniform();
                    else if (in.overrunCut && rpm > 2200.0 && rng.uniform() < crackle * 0.3f)
                        cs.pop = 0.7f + 1.5f * rng.uniform();
                }
                else
                {
                    const float lowLoad = 1.0f - af;
                    const float sigma = variability * (0.03f + 0.22f * lowLoad * lowLoad) + overlap * 0.3f * lowLoad * lowLoad * lowLoad * clampv (idleRatio, 0.0f, 1.5f);
                    float a = (0.25f + 0.75f * std::pow (af, 0.8f)) * cs.trim * (1.0f + sigma * rng.gauss());
                    if (rng.uniform() < overlap * 0.05f * lowLoad * lowLoad * clampv (idleRatio, 0.0f, 1.0f))
                        a *= 0.3f; // partial misfire at idle with a big cam
                    cs.amp = std::max (0.0f, a);
                }
                tickImpulse += valveTickAmp * (0.7f + 0.6f * rng.uniform()); // exhaust valve opens
            }
            if (cs.lastPhase < pulseWidthDeg && ph >= pulseWidthDeg)
                tickImpulse += valveTickAmp * 0.8f; // exhaust valve seats
            cs.lastPhase = ph;

            float s = 0.0f;
            if (ph < pulseWidthDeg) s = cs.amp * lut (exhaustShape, ph / pulseWidthDeg);
            if (cs.pop > 0.0f && ph < 45.0) s += cs.pop * lut (popShape, ph / 45.0);
            bank[ev.bank] += s;

            // ----- combustion knock radiated by the block (at firing TDC)
            const double tdcPh = wrapDeg (crankDeg - ev.angle - 8.0, cycle);
            // detect TDC crossing using a tiny window after the event (robust at any rpm)
            if (tdcPh < dDeg + 1e-9 && in.combustion && ! in.fuelCut)
                knockImpulse += (0.15f + 0.85f * af) * cs.amp * (0.4f + harshness) * (diesel ? 2.5f : 1.0f);

            // ----- intake suction pulse
            const double iph = wrapDeg (crankDeg - ev.angle - intakeOffsetDeg, cycle);
            if (iph < cs.lastIntake)
            {
                cs.intakeAmp = (0.15f + 0.85f * af) * cs.trim * (in.combustion || rpm > 30.0 ? 1.0f : 0.0f);
                tickImpulse += valveTickAmp * (0.7f + 0.6f * rng.uniform());
            }
            cs.lastIntake = iph;
            if (iph < intakeWidthDeg)
            {
                intakeSum -= cs.intakeAmp * lut (intakeShape, iph / intakeWidthDeg);
                // reversion with overlap: exhaust gas pushed back into the intake at low load
                if (iph < 40.0) intakeSum += overlap * 0.5f * (1.0f - af) * cs.amp * lut (popShape, iph / 40.0);
            }
        }

        // ================= EXHAUST =================
        float exL = 0.0f, exR = 0.0f;
        {
            float lineIn[2];
            if (numLines == 1)
                lineIn[0] = bank[0] + bank[1];
            else
            {
                lineIn[0] = (1.0f - bankCross) * bank[0] + bankCross * bank[1];
                lineIn[1] = (1.0f - bankCross) * bank[1] + bankCross * bank[0];
            }
            for (int li = 0; li < numLines; ++li)
            {
                auto& L = lines[li];
                float x = L.header.process (lineIn[li]);
                x = L.midpipe.process (x * 0.5f);
                x = L.turbine.process (x) * L.turbineGain;
                x = L.catalyst.process (x) * L.catGain;
                x = L.resonator1.process (x);
                x = L.resonator2.process (x);
                for (auto& m : L.muffler) x = m.process (x);
                x = L.mufflerLP.process (x) * L.mufflerGain;
                x = L.tailpipe.process (x * 0.5f);
                // radiation from the outlet: ~ d/dt (6 dB/oct) below the corner, flat above it
                // (a pure differentiator would make idle 25-30 dB quieter than full load)
                const float d = L.dc.process (x);
                const float y = L.radiationLP.process (d) * kExhaustLevel * exhaustTrim;
                exL += y * linePanL[li];
                exR += y * linePanR[li];
            }
        }

        // ================= INTAKE =================
        float intakeOut = 0.0f;
        {
            const float runner = intakeRunner.process (intakeSum);
            float res = runner;
            if (intake.filter == AirFilterType::StockAirbox || intake.filter == AirFilterType::PanelFilter) res += 0.8f * airboxRes.process (runner);
            if (intake.manifold != IntakeManifoldType::IndividualThrottles) res += 0.5f * plenumRes.process (runner);
            float f = intakeResonator.process (intakeFilter.process (res));
            const float plate = 0.2f + 0.8f * thr; // the throttle plate blocks the pulses at part throttle
            // throttle hiss: flow through a small opening (mass flow / open area)
            const float flow = static_cast<float> (clampv (rpm / redline, 0.0, 1.2)) * (0.1f + af);
            const float hissAmp = in.combustion ? flow * (1.0f - thr) * (1.0f - thr) * 0.6f : 0.0f;
            const float hiss = hissBand.process (rng.bipolar()) * hissAmp;
            intakeOut = (f * plate * radiationGain * 0.02f + hiss) * kIntakeLevel * intakeTrim;
        }

        // ================= COMBUSTION (block) =================
        float combOut = 0.0f;
        {
            const float exc = knockImpulse * 40.0f + knockImpulse * rng.bipolar() * 20.0f;
            combOut = (blockModes[0].process (exc) + 0.8f * blockModes[1].process (exc) + 0.5f * blockModes[2].process (exc)) * kCombustionLevel * exhaustTrim;
        }

        // ================= MECHANICAL =================
        float mechOut = 0.0f;
        {
            const float exc = tickImpulse * 30.0f;
            mechOut = valveModes[0].process (exc) + 0.7f * valveModes[1].process (exc) + 0.5f * valveModes[2].process (exc);
            const float revHz = static_cast<float> (rpm / 60.0);
            if (chainOrder > 0.0f)
            {
                chainPhase += revHz * chainOrder / static_cast<float> (fs);
                chainPhase -= std::floor (chainPhase);
                mechOut += std::sin (static_cast<float> (kTwoPi) * chainPhase) * 0.04f * rpmNorm * rpmNorm;
            }
            altPhase += revHz * 2.7f * 12.0f / static_cast<float> (fs); // alternator: pulley 2.7 x 12 poles
            altPhase -= std::floor (altPhase);
            mechOut += std::sin (static_cast<float> (kTwoPi) * altPhase) * 0.008f * std::min (1.0f, rpmNorm * 4.0f);
            if (in.starterActive)
            {
                const float sHz = std::max (35.0f, revHz) * 11.0f; // pinion/ring gear ratio ~11
                starterPhase += sHz / static_cast<float> (fs);
                starterPhase -= std::floor (starterPhase);
                const float saw = 2.0f * starterPhase - 1.0f;
                mechOut += starterBand.process (saw) * 0.6f + saw * 0.12f;
            }
            mechOut *= kMechanicalLevel;
        }

        // ================= BOOST =================
        float boostOut = 0.0f;
        {
            if (turbo)
            {
                const float amp = shaft * shaft * (0.35f + 0.65f * af);
                const float partial[3] = { 1.0f, 2.03f, 3.1f };
                const float pamp[3] = { 1.0f, 0.3f, 0.1f };
                for (int k = 0; k < 3; ++k)
                {
                    whistlePhase[k] += whistleHz * partial[k] / static_cast<float> (fs);
                    whistlePhase[k] -= std::floor (whistlePhase[k]);
                    boostOut += std::sin (static_cast<float> (kTwoPi) * whistlePhase[k]) * pamp[k] * amp * 0.25f;
                }
                boostOut += whistleNoiseBand.process (rng.bipolar()) * shaft * 0.35f;
                if (bovEnv > 1e-4f)
                {
                    const float nz = rng.bipolar();
                    const float b = boost.blowOff == BlowOffType::Atmospheric ? bovBand.process (bovHP.process (nz)) * 2.2f : bovLP.process (nz) * 0.6f;
                    boostOut += b * bovEnv * bovStrength;
                    bovEnv *= bovDecay;
                }
                if (surgeEnv > 1e-4f)
                {
                    surgePhase += 17.0f / static_cast<float> (fs);
                    surgePhase -= std::floor (surgePhase);
                    const float am = 0.5f + 0.5f * std::sin (static_cast<float> (kTwoPi) * surgePhase);
                    boostOut += whistleNoiseBand.process (rng.bipolar()) * am * am * surgeEnv * 1.5f;
                    surgeEnv *= surgeDecay;
                }
                const float wg = in.wastegateOpen * static_cast<float> (clampv (rpm / redline, 0.0, 1.0)) * (boost.externalWastegate ? 1.0f : 0.25f);
                if (wg > 0.0f) boostOut += wastegateBand.process (rng.bipolar()) * wg * 1.2f;
            }
            if (superchargerRoots || superchargerScrew || superchargerCentrifugal)
            {
                const float revHz = static_cast<float> (rpm / 60.0);
                float hz;
                if (superchargerCentrifugal)
                    hz = static_cast<float> (boost.whistleMinHz + (boost.whistleMaxHz - boost.whistleMinHz) * clampv (rpm / redline, 0.0, 1.0) * clampv (rpm / redline, 0.0, 1.0));
                else
                    hz = revHz * static_cast<float> (boost.superchargerDriveRatio) * static_cast<float> (boost.superchargerLobes) * (superchargerScrew ? 1.5f : 2.0f);
                scPhase += std::min (hz, static_cast<float> (fs) * 0.2f) / static_cast<float> (fs);
                scPhase -= std::floor (scPhase);
                const float ph2 = static_cast<float> (kTwoPi) * scPhase;
                const float amp = rpmNorm * (0.35f + 0.65f * af) * (superchargerCentrifugal ? rpmNorm : 1.0f);
                boostOut += (std::sin (ph2) + 0.5f * std::sin (2.0f * ph2) + 0.25f * std::sin (3.0f * ph2)) * amp * 0.3f;
            }
            boostOut *= kBoostLevel;
        }

        // ================= VIBRATION =================
        float vibOut = 0.0f;
        {
            const float revAngle = static_cast<float> (wrapDeg (crankDeg, 360.0) * kPi / 180.0);
            const float level = (0.15f + rpmNorm) * (in.combustion || rpm > 30.0 ? 1.0f : 0.0f) * static_cast<float> (clampv (rpm / 300.0, 0.0, 1.0));
            vibOut = (std::sin (revAngle) * (balance.primary + 0.5f * balance.couple) + std::sin (2.0f * revAngle) * balance.secondary * 0.8f
                      + std::sin (static_cast<float> (kTwoPi * crankDeg * numEvents / cycle)) * 0.15f)
                     * level * kVibrationLevel;
        }

        // ================= OUTPUT TO THE LAYER BUS =================
        // natural placement only; gains, FX, automation and master are applied by the LayerMixer
        static constexpr float naturalPan[6] = { 0.0f, -0.15f, 0.0f, 0.05f, 0.2f, 0.0f };
        const float mono[6] = { combOut, intakeOut, 0.0f, mechOut, boostOut, vibOut };
        for (int l = 0; l < 6; ++l)
        {
            float lL, lR;
            if (l == static_cast<int> (LayerId::Exhaust))
            {
                lL = exL * 1.41421356f * 0.7071f;
                lR = exR * 1.41421356f * 0.7071f;
                sq[static_cast<std::size_t> (l)] += 0.5 * (static_cast<double> (exL) * exL + static_cast<double> (exR) * exR);
            }
            else
            {
                float pl, pr;
                equalPower (naturalPan[l], pl, pr);
                lL = mono[l] * pl;
                lR = mono[l] * pr;
                sq[static_cast<std::size_t> (l)] += static_cast<double> (mono[l]) * mono[l];
            }
            if (bus.L[l]) bus.L[l][offset + i] += lL;
            if (bus.R[l]) bus.R[l][offset + i] += lR;
        }
    }
    for (int l = 0; l < 6; ++l)
        rms[static_cast<std::size_t> (l)] = static_cast<float> (std::sqrt (sq[static_cast<std::size_t> (l)] * invN));
}
} // namespace esd
