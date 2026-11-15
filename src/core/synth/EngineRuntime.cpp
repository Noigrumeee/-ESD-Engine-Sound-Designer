#include "EngineRuntime.h"
#include "../base/Math.h"

#include <algorithm>
#include <cmath>

namespace esd
{
EngineRuntime::EngineRuntime (const Project& p, double sampleRate, const Loaders& loaders) : fs (sampleRate), k (p.kind), proj (p)
{
    sim.configure (p);
    sim.reset (false);
    voice.prepare (p, sampleRate);
    motor.configure (p.electric);
    motor.reset();
    motorVoice.prepare (p.electric, sampleRate, p.seed);
    gearVoice.prepare (p.transmission, sampleRate, p.seed);
    samples.prepare (p.samples, sampleRate, loaders.samples);
    mixer.prepare (p.mix, sampleRate, loaders.ir);

    enginePeakKw = 1.0;
    for (double rpm = p.engine.idleRpm; rpm <= p.engine.limiterRpm; rpm += 50.0)
        enginePeakKw = std::max (enginePeakKw, p.engine.torqueCurve.eval (rpm) * rpmToRadPerSec (rpm) / 1000.0);

    if (k == PowertrainKind::Thermal)
        drivetrain.configure (p, p.engine.idleRpm, std::max (1.0, p.engine.torqueCurve.maxY()), p.engine.redlineRpm, false);
    else
        drivetrain.configure (p, 0.0, p.electric.peakTorqueNm, p.electric.maxRpm, true);

    hybrid = HybridState {};
    hybrid.soc = static_cast<float> (p.hybrid.socInitial);

    busData.assign (static_cast<std::size_t> (kNumLayers * 2 * kSimBlock), 0.0f);
    for (int l = 0; l < kNumLayers; ++l)
    {
        bus.L[l] = busData.data() + (2 * l) * kSimBlock;
        bus.R[l] = busData.data() + (2 * l + 1) * kSimBlock;
    }
}

void EngineRuntime::adoptFrom (const EngineRuntime& o)
{
    sim.adoptState (o.sim.state());
    motor.adoptState (o.motor.state());
    if (o.proj.transmission == proj.transmission && o.k == k) drivetrain.adoptState (o.drivetrain.state());
    hybrid = o.hybrid;
    seenUp = o.seenUp;
    seenDown = o.seenDown;
    motorAngle = o.motorAngle;
}

void EngineRuntime::resetEngine (bool running)
{
    sim.reset (running && k != PowertrainKind::Electric);
    voice.reset();
    motor.reset();
    drivetrain.reset();
    hybrid = HybridState {};
    hybrid.soc = static_cast<float> (proj.hybrid.socInitial);
    hybrid.engineOn = running && k == PowertrainKind::Thermal;
}

double EngineRuntime::primaryAngleDeg() const { return k == PowertrainKind::Electric ? motorAngle : voice.crankAngleDeg(); }
double EngineRuntime::primaryCycleDeg() const { return k == PowertrainKind::Electric ? 360.0 : voice.cycleDeg(); }

namespace
{
DriverInput driverFrom (const EngineControls& c, uint32_t& seenUp, uint32_t& seenDown)
{
    DriverInput d;
    d.throttle = c.throttle;
    d.brake = c.brake;
    if (c.shiftUpCount != seenUp) { d.shiftRequest = 1; seenUp = c.shiftUpCount; }
    else if (c.shiftDownCount != seenDown) { d.shiftRequest = -1; seenDown = c.shiftDownCount; }
    return d;
}
} // namespace

void EngineRuntime::stepThermal (const EngineControls& c, double dt)
{
    vehicleActive = c.mode == DriveMode::Vehicle;
    engW0 = sim.state().omega;
    if (vehicleActive)
    {
        const auto d = driverFrom (c, seenUp, seenDown);
        inW0 = drivetrain.state().inputOmega;
        const auto cp = drivetrain.couple (sim.state().omega, d, dt);
        if (cp.snapOmega >= 0.0) { sim.setOmega (cp.snapOmega); engW0 = cp.snapOmega; }
        EngineControls ec = c;
        ec.mode = DriveMode::FreeRev;
        ec.useLoadTorque = true;
        ec.loadTorqueNm = cp.loadTorque;
        ec.extraInertia = cp.extraInertia;
        ec.torqueScale = cp.torqueScale;
        sim.step (ec, dt);
        drivetrain.advance (sim.state().omega, dt);
        inW1 = drivetrain.state().inputOmega;
    }
    else
        sim.step (c, dt);
    engW1 = sim.state().omega;
    hybrid.engineOn = sim.state().combustion;
}

void EngineRuntime::stepElectric (const EngineControls& c, double dt)
{
    motW0 = motor.state().omega;
    vehicleActive = c.mode == DriveMode::Vehicle;
    const bool on = c.ignition || c.mode == DriveMode::Dyno;
    if (c.mode == DriveMode::Dyno)
    {
        const double target = rpmToRadPerSec (clampv (static_cast<double> (c.dynoRpm), 0.0, proj.electric.maxRpm));
        const double step = rpmToRadPerSec (c.dynoRampRpmPerSec) * dt;
        const double w = motor.state().omega + clampv (target - motor.state().omega, -step, step);
        const double cmd = clampv (static_cast<double> (c.dynoLoad), -1.0, 1.0) * motor.availableTorque (radPerSecToRpm (w));
        motor.stepImposedSpeed (w, cmd, dt);
    }
    else if (vehicleActive)
    {
        const auto d = driverFrom (c, seenUp, seenDown);
        inW0 = drivetrain.state().inputOmega;
        const auto cp = drivetrain.couple (motor.state().omega, d, dt);
        if (cp.snapOmega >= 0.0) { MotorState s = motor.state(); s.omega = cp.snapOmega; s.rpm = radPerSecToRpm (cp.snapOmega); motor.adoptState (s); }
        const double cmd = on ? motor.pedalTorque (c.throttle, c.brake) * cp.torqueScale : 0.0;
        motor.step (cmd, cp.loadTorque, cp.extraInertia, dt);
        drivetrain.advance (motor.state().omega, dt);
        inW1 = drivetrain.state().inputOmega;
    }
    else
    {
        const double cmd = on ? motor.pedalTorque (c.throttle, 0.0f) : 0.0;
        motor.step (cmd, clampv (static_cast<double> (c.externalLoad), 0.0, 1.0) * proj.electric.peakTorqueNm, 0.0, dt);
    }
    motW1 = motor.state().omega;
    motorAngle = wrapDeg (motorAngle + radPerSecToRpm (motW1) * 6.0 * dt, 360.0);
}

void EngineRuntime::stepHybrid (const EngineControls& c, double dt)
{
    const auto& h = proj.hybrid;
    engW0 = sim.state().omega;
    motW0 = motor.state().omega;
    if (c.mode == DriveMode::Dyno)
    {
        // dyno: the engine alone (hybrid loop sets are rendered as an engine set + a motor set)
        vehicleActive = false;
        sim.step (c, dt);
        motor.stepImposedSpeed (0.0, 0.0, dt);
        hybrid.engineOn = true;
        engW1 = sim.state().omega;
        motW1 = 0.0;
        return;
    }
    vehicleActive = true;
    const auto d = driverFrom (c, seenUp, seenDown);
    const double v = drivetrain.state().speedMs;
    const bool series = h.arch == HybridArch::Series || h.arch == HybridArch::SeriesParallel;
    const double motorKw = proj.electric.peakPowerKw;
    const double demandKw = c.throttle * (series ? motorKw : motorKw * h.motorAssist + enginePeakKw);
    const double idleW = rpmToRadPerSec (proj.engine.idleRpm);

    // ---- engine on / off strategy
    bool want;
    if (h.arch == HybridArch::Mild) want = ! (v < 0.3 && c.throttle < 0.02f); // stop-start
    else want = hybrid.soc < h.socMin || v * 3.6 > h.evMaxSpeedKmh || demandKw > h.evMaxPowerKw;
    if (hybrid.engineOn && ! want && (hybrid.onTimer < h.engineMinOnSeconds || hybrid.soc < h.socMin + 0.03)) want = true;
    if (! c.ignition) want = false;
    if (want != hybrid.engineOn)
    {
        hybrid.engineOn = want;
        hybrid.onTimer = 0.0;
        hybrid.offTimer = 0.0;
    }
    hybrid.onTimer += dt;
    hybrid.offTimer += dt;

    auto limitRegen = [&] (double cmd, double w)
    {
        if (cmd >= 0.0) return cmd;
        if (hybrid.soc >= h.socMax) return 0.0;
        const double lim = h.regenMaxKw * 1000.0 / std::max (5.0, w);
        return std::max (cmd, -lim);
    };

    double genKw = 0.0;
    inW0 = drivetrain.state().inputOmega;
    if (series)
    {
        // wheels: motor only ; engine: generator at a speed set by the power demand
        auto cp = drivetrain.couple (motor.state().omega, d, dt);
        if (cp.snapOmega >= 0.0) { MotorState s = motor.state(); s.omega = cp.snapOmega; s.rpm = radPerSecToRpm (cp.snapOmega); motor.adoptState (s); }
        const double cmd = c.ignition ? limitRegen (motor.pedalTorque (c.throttle, c.brake), motor.state().omega) * cp.torqueScale : 0.0;
        motor.step (cmd, cp.loadTorque, cp.extraInertia, dt);
        drivetrain.advance (motor.state().omega, dt);
        hybrid.coupled = false;
        EngineControls ec = c;
        if (hybrid.engineOn)
        {
            const double motorElecKw = std::max (0.0, static_cast<double> (motor.state().powerKw)) / 0.9;
            const double charge = hybrid.soc < 0.5 * (h.socMin + h.socMax) ? h.chargePowerKw : 0.0;
            const double pg = clampv (motorElecKw + charge, h.chargePowerKw * 0.5, enginePeakKw * 0.9);
            const double rpm = proj.engine.idleRpm + (proj.engine.redlineRpm * 0.75 - proj.engine.idleRpm) * std::sqrt (pg / enginePeakKw);
            const double tw = std::max (1.0, sim.wotTorqueAt (rpm));
            ec.mode = DriveMode::Dyno;
            ec.dynoRpm = static_cast<float> (rpm);
            ec.dynoLoad = static_cast<float> (clampv (pg * 1000.0 / (tw * rpmToRadPerSec (rpm)), 0.05, 1.0));
            ec.dynoRampRpmPerSec = 1500.0f;
            ec.ignition = true;
        }
        else
        {
            ec.mode = DriveMode::FreeRev;
            ec.ignition = false;
            ec.throttle = 0.0f;
        }
        sim.step (ec, dt);
        if (hybrid.engineOn) genKw = std::max (0.0f, sim.state().powerKw);
        const double mk = motor.state().powerKw;
        hybrid.batteryKw = static_cast<float> ((mk > 0 ? mk / 0.9 : mk * 0.9) - genKw * 0.92);
        hybrid.mode = ! hybrid.engineOn ? (motor.state().regen ? 3 : 0) : 2;
    }
    else
    {
        // parallel family (P2): engine and motor on the gearbox input shaft when the K0 clutch is closed
        const double shaft = hybrid.coupled ? sim.state().omega : motor.state().omega;
        hybrid.coupled = hybrid.engineOn && motor.state().omega >= idleW * 0.95 && sim.state().combustion;
        auto cp = drivetrain.couple (shaft, d, dt);
        if (hybrid.coupled)
        {
            if (cp.snapOmega >= 0.0) sim.setOmega (cp.snapOmega);
            const double w = sim.state().omega;
            const double tm = motor.availableTorque (radPerSecToRpm (w));
            double cmd;
            if (c.throttle > 0.02f)
                cmd = hybrid.soc > h.socMin ? h.motorAssist * c.throttle * tm : -std::min (tm, h.chargePowerKw * 1000.0 / std::max (5.0, w));
            else
                cmd = limitRegen (-(proj.electric.regenStrength + 0.6 * c.brake) * tm, w);
            EngineControls ec = c;
            ec.mode = DriveMode::FreeRev;
            ec.ignition = true;
            ec.useLoadTorque = true;
            ec.loadTorqueNm = cp.loadTorque - motor.state().torque;
            ec.extraInertia = cp.extraInertia + proj.electric.rotorInertia;
            ec.torqueScale = cp.torqueScale;
            sim.step (ec, dt);
            motor.stepImposedSpeed (sim.state().omega, cmd, dt);
            drivetrain.advance (sim.state().omega, dt);
        }
        else
        {
            if (cp.snapOmega >= 0.0) { MotorState s = motor.state(); s.omega = cp.snapOmega; s.rpm = radPerSecToRpm (cp.snapOmega); motor.adoptState (s); }
            const double cmd = c.ignition ? limitRegen (motor.pedalTorque (c.throttle, c.brake), motor.state().omega) * cp.torqueScale : 0.0;
            motor.step (cmd, cp.loadTorque, cp.extraInertia, dt);
            drivetrain.advance (motor.state().omega, dt);
            EngineControls ec = c;
            ec.mode = DriveMode::FreeRev;
            ec.throttle = 0.0f;
            ec.useLoadTorque = false;
            ec.externalLoad = 0.0f;
            ec.ignition = hybrid.engineOn;
            if (hybrid.engineOn && sim.state().rpm < 150.0)
                sim.setOmega (std::max (idleW, motor.state().omega)); // started by the motor / starter-generator
            sim.step (ec, dt);
        }
        const double mk = motor.state().powerKw;
        hybrid.batteryKw = static_cast<float> (mk > 0 ? mk / 0.9 : mk * 0.9);
        const float mt = motor.state().torque;
        hybrid.mode = ! hybrid.engineOn ? (motor.state().regen ? 3 : 0) : ! hybrid.coupled ? 2 : (mt > 1.0f ? 1 : mt < -1.0f ? (c.throttle > 0.02f ? 2 : 3) : 4);
    }
    hybrid.soc = static_cast<float> (clampv (hybrid.soc - hybrid.batteryKw * dt / (std::max (0.05, h.batteryKWh) * 3600.0), 0.0, 1.0));
    inW1 = drivetrain.state().inputOmega;
    engW1 = sim.state().omega;
    motW1 = motor.state().omega;
}

ModInputs EngineRuntime::modInputs() const
{
    ModInputs m;
    const auto& e = sim.state();
    const auto& ms = motor.state();
    const bool electricPrimary = k == PowertrainKind::Electric || (k == PowertrainKind::Hybrid && ! hybrid.engineOn);
    const double maxFe = proj.electric.maxRpm / 60.0 * proj.electric.polePairs();
    m[ModSource::Rpm] = static_cast<float> (electricPrimary ? ms.rpm / std::max (1.0, proj.electric.maxRpm) : e.rpm / std::max (1.0, proj.engine.limiterRpm));
    m[ModSource::Torque] = electricPrimary ? static_cast<float> (std::abs (ms.torque) / std::max (1.0, proj.electric.peakTorqueNm))
                                           : static_cast<float> (std::max (0.0f, e.brakeTorque) / std::max (1.0, sim.peakTorque()));
    m[ModSource::Load] = electricPrimary ? ms.loadFraction : e.airflow;
    m[ModSource::Throttle] = e.throttle;
    m[ModSource::Speed] = static_cast<float> (drivetrain.state().speedMs * 3.6 / 250.0);
    m[ModSource::Gear] = static_cast<float> (drivetrain.state().gear) / static_cast<float> (std::max (1, drivetrain.state().numGears));
    m[ModSource::Temperature] = electricPrimary ? (ms.tempC - 20.0f) / 100.0f : (e.coolantC - 20.0f) / 100.0f;
    m[ModSource::Boost] = static_cast<float> (e.boostBar / std::max (0.1, proj.boost.maxBoostBar));
    m[ModSource::ElectricFreq] = static_cast<float> (ms.electricalHz / std::max (1.0, maxFe));
    m[ModSource::Battery] = hybrid.soc;
    m[ModSource::Regen] = ms.regen ? static_cast<float> (std::abs (ms.torque) / std::max (1.0, proj.electric.peakTorqueNm)) : 0.0f;
    m[ModSource::ShiftEvent] = shiftEnv;
    return m;
}

void EngineRuntime::process (const EngineControls& c, const VoiceMix& mix, float* outL, float* outR, int n, float* const* layerL, float* const* layerR)
{
    int done = 0;
    float* lL[kNumLayers];
    float* lR[kNumLayers];
    while (done < n)
    {
        const int chunk = std::min (kSimBlock, n - done);
        const double dt = chunk / fs;
        std::fill (busData.begin(), busData.end(), 0.0f);
        vehicleActive = false;
        switch (k)
        {
            case PowertrainKind::Thermal: stepThermal (c, dt); break;
            case PowertrainKind::Electric: stepElectric (c, dt); break;
            case PowertrainKind::Hybrid: stepHybrid (c, dt); break;
        }

        // ---- shift event envelope (automation source)
        const auto& ds = drivetrain.state();
        if (ds.shiftCount != lastShiftCount)
        {
            lastShiftCount = ds.shiftCount;
            shiftEnv = 1.0f;
        }
        shiftEnv *= static_cast<float> (std::exp (-dt / 0.3));

        // ---- sources -> layer bus
        if (k != PowertrainKind::Electric)
        {
            const auto& s = sim.state();
            VoiceInput in;
            in.omegaStart = engW0;
            in.omegaEnd = engW1;
            in.airflow = s.airflow;
            in.throttle = s.throttle;
            in.combustion = s.combustion;
            in.fuelCut = s.fuelCut;
            in.limiterActive = s.limiterActive;
            in.overrunCut = s.overrunCut;
            in.starterActive = s.starterActive;
            in.turboShaft = s.turboShaft;
            in.wastegateOpen = s.wastegateOpen;
            in.boostBar = s.boostBar;
            in.egtK = s.egtK;
            in.blowOffCount = s.blowOffCount;
            in.blowOffStrength = s.blowOffStrength;
            voice.process (in, bus, 0, chunk);
        }
        if (k != PowertrainKind::Thermal)
        {
            const auto& ms = motor.state();
            MotorVoiceInput mi;
            mi.omegaStart = motW0;
            mi.omegaEnd = motW1;
            mi.torque = ms.torque;
            mi.currentA = ms.currentA;
            mi.tempC = ms.tempC;
            mi.slip = ms.slip;
            mi.loadFraction = ms.loadFraction;
            mi.regen = ms.regen;
            mi.enabled = c.ignition || c.mode == DriveMode::Dyno;
            motorVoice.process (mi, bus, 0, chunk);
        }
        {
            TransmissionVoiceInput ti;
            if (vehicleActive)
            {
                ti.inputOmegaStart = inW0;
                ti.inputOmegaEnd = inW1;
                ti.torque = ds.inputTorque;
                ti.gearRatio = ds.gearRatio;
                ti.engaged = ds.gear > 0 && (ds.locked || ds.clutch > 0.3f || proj.transmission.type == TransmissionType::TorqueConverter);
                ti.cvt = proj.transmission.type == TransmissionType::CVT;
                ti.converterSlip = proj.transmission.type == TransmissionType::TorqueConverter && ! ds.lockup ? clampv (1.0f - ds.converterSR, 0.0f, 1.0f) : 0.0f;
            }
            else if (k == PowertrainKind::Electric)
            {
                // the e-axle reduction is always meshed with the motor
                ti.inputOmegaStart = motW0;
                ti.inputOmegaEnd = motW1;
                ti.torque = motor.state().torque;
                ti.gearRatio = proj.transmission.ratios.empty() ? 1.0 : proj.transmission.ratios.front();
                ti.engaged = true;
            }
            ti.peakTorque = static_cast<float> (k == PowertrainKind::Thermal ? sim.peakTorque() : proj.electric.peakTorqueNm);
            ti.shiftCount = ds.shiftCount;
            ti.shiftStrength = ds.shiftStrength;
            gearVoice.process (ti, bus, 0, chunk);
        }
        if (! samples.empty())
        {
            const bool electricPrimary = k == PowertrainKind::Electric || (k == PowertrainKind::Hybrid && ! hybrid.engineOn);
            const double rpm = electricPrimary ? motor.state().rpm : sim.state().rpm;
            const double load = electricPrimary ? motor.state().loadFraction : sim.state().airflow;
            samples.process (rpm, load, rpm > 50.0, bus, 0, chunk);
        }

        for (int l = 0; l < kNumLayers; ++l)
        {
            lL[l] = (layerL && layerL[l]) ? layerL[l] + done : nullptr;
            lR[l] = (layerR && layerR[l]) ? layerR[l] + done : nullptr;
        }
        mixer.process (bus, mix, modInputs(), outL + done, outR + done, chunk, layerL ? lL : nullptr, layerR ? lR : nullptr);
        done += chunk;
    }
}

Telemetry EngineRuntime::telemetry() const
{
    const auto& s = sim.state();
    const auto& ms = motor.state();
    const auto& ds = drivetrain.state();
    Telemetry t;
    t.rpm = static_cast<float> (s.rpm);
    t.brakeTorque = s.brakeTorque;
    t.powerKw = s.powerKw;
    t.airflow = s.airflow;
    t.throttle = s.throttle;
    t.boostBar = s.boostBar;
    t.egtK = s.egtK;
    t.coolantC = s.coolantC;
    t.turboShaft = s.turboShaft;
    t.combustion = s.combustion;
    t.fuelCut = s.fuelCut;
    t.limiter = s.limiterActive;
    t.starter = s.starterActive;
    t.firingHz = static_cast<float> (voice.firingFrequency (s.rpm));
    for (int l = 0; l < kNumLayers; ++l) t.layerRms[l] = mixer.layerRms()[static_cast<std::size_t> (l)];
    t.crankDeg = static_cast<float> (voice.crankAngleDeg());
    t.motorRpm = static_cast<float> (ms.rpm);
    t.motorTorque = ms.torque;
    t.motorCurrent = ms.currentA;
    t.motorTempC = ms.tempC;
    t.electricHz = ms.electricalHz;
    t.motorPowerKw = ms.powerKw;
    t.regen = ms.regen;
    t.speedKmh = static_cast<float> (ds.speedMs * 3.6);
    t.gear = ds.gear;
    t.numGears = ds.numGears;
    t.clutch = ds.clutch;
    t.converterSR = ds.converterSR;
    t.shifting = ds.shifting;
    t.soc = hybrid.soc;
    t.batteryKw = hybrid.batteryKw;
    t.engineOn = k == PowertrainKind::Hybrid ? hybrid.engineOn : s.combustion;
    t.hybridMode = hybrid.mode;
    if (k == PowertrainKind::Electric) t.firingHz = ms.electricalHz;
    return t;
}
} // namespace esd
