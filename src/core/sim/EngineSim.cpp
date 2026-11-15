#include "EngineSim.h"
#include "../base/Math.h"

#include <cmath>

namespace esd
{
namespace
{
double fmepBar (double rpm)
{
    const double n = rpm / 1000.0;
    return 0.97 + 0.15 * n + 0.05 * n * n;
}
} // namespace

void EngineSim::configure (const Project& p)
{
    const auto& e = p.engine;
    const auto* l = findLayout (e.layoutId);
    const CycleType cycle = l ? l->cycle : CycleType::FourStroke;
    const double revsPerCycle = cycle == CycleType::FourStroke ? 2.0 : 1.0;

    vdM3 = e.displacementCc() * 1e-6;
    mepToTorque = vdM3 / (kTwoPi * revsPerCycle);
    frictionScale = e.frictionScale;
    idleRpm = e.idleRpm;
    redline = e.redlineRpm;
    limiter = e.limiterRpm;
    limiterHyst = e.limiterHysteresisRpm;
    throttleTau = static_cast<float> (std::max (0.005, e.throttleResponseMs * 0.001));
    diesel = e.fuel == FuelType::Diesel;

    const double liters = vdM3 * 1000.0;
    J = e.flywheelInertia > 0.0 ? e.flywheelInertia : 0.02 + 0.07 * std::pow (std::max (liters, 0.05), 0.9);

    torqueWot.build (e.torqueCurve, 0.0, std::max (limiter, redline) * 1.2);
    throttleMap.build (e.throttleMap, 0.0, 1.0);
    peakT = std::max (1.0, e.torqueCurve.maxY());

    boost = p.boost;
    turbo = boost.type == BoostType::Turbo || boost.type == BoostType::TwinTurbo || boost.type == BoostType::Twincharged;
    supercharger = boost.type == BoostType::RootsSupercharger || boost.type == BoostType::TwinScrewSupercharger
                   || boost.type == BoostType::CentrifugalSupercharger || boost.type == BoostType::Twincharged;
}

void EngineSim::reset (bool running)
{
    const double t = st.timeSec;
    st = EngineState {};
    st.timeSec = t;
    st.coolantC = 85.0f; // warm engine by default (the warm-up is still simulated)
    if (running)
    {
        st.omega = rpmToRadPerSec (idleRpm);
        st.rpm = idleRpm;
        st.combustion = true;
        st.airflow = 0.15f;
        st.idleIntegral = 0.0f;
        st.egtK = 650.0f;
    }
}

double EngineSim::frictionTorqueAt (double rpm) const
{
    return frictionScale * fmepBar (rpm) * 1e5 * mepToTorque;
}

float EngineSim::turboTarget (double rpm, float airflow) const
{
    if (! turbo) return 0.0f;
    const double spool = std::max (500.0, boost.spoolRpm);
    const double energy = (rpm / spool) * (0.12 + 0.88 * airflow);
    return static_cast<float> (clampv (energy, 0.0, 1.3));
}

void EngineSim::step (const EngineControls& c, double dt)
{
    if (dt <= 0.0) return;
    st.timeSec += dt;
    const float prevThrottle = st.throttle;
    st.throttle = clampv (c.throttle, 0.0f, 1.0f);
    st.rpm = radPerSecToRpm (st.omega);
    const double rpm = st.rpm;

    // ---- combustion state machine
    if (c.mode == DriveMode::Dyno)
        st.combustion = true;
    else if (! c.ignition)
        st.combustion = false;
    else if (! st.combustion && rpm > 150.0)
        st.combustion = true;
    else if (st.combustion && rpm < 80.0 && ! c.starter)
        st.combustion = false; // stalled

    // ---- limiter and decel fuel cut
    if (c.mode == DriveMode::Dyno)
    {
        st.limiterActive = false;
        st.fuelCut = c.forceFuelCut;
        st.overrunCut = c.forceFuelCut;
    }
    else
    {
        if (c.limiterEnabled && rpm > limiter) st.limiterActive = true;
        if (st.limiterActive && (rpm < limiter - limiterHyst || ! c.limiterEnabled)) st.limiterActive = false;
        // deceleration fuel cut-off (DFCO) with hysteresis, as real ECUs do
        if (! st.overrunCut && st.throttle < 0.02f && rpm > idleRpm + 1100.0) st.overrunCut = true;
        if (st.overrunCut && (st.throttle >= 0.02f || rpm < idleRpm + 450.0)) st.overrunCut = false;
        st.fuelCut = st.limiterActive || st.overrunCut;
    }

    // ---- friction & idle feed-forward
    const double tf = frictionTorqueAt (rpm) * std::tanh (st.omega / 5.0);
    const double pmepFull = (diesel ? 0.2 : 0.9) * 1e5 * mepToTorque;
    const double twot = std::max (0.0, static_cast<double> (torqueWot.eval (rpm)));

    // ---- boost
    double prNow = 1.0, prTarget = 1.0;
    if (turbo)
    {
        const float target = turboTarget (rpm, st.airflow);
        const float tau = static_cast<float> (std::max (0.03, boost.responseMs * 0.001 * (0.6 + 0.8 * boost.turboSize)));
        const float tauEff = target > st.turboShaft ? tau : tau * 0.7f;
        st.turboShaft += (std::min (target, 1.0f) - st.turboShaft) * static_cast<float> (dt) / tauEff;
        st.turboShaft = clampv (st.turboShaft, 0.0f, 1.0f);
        st.wastegateOpen = clampv ((target - 1.0f) * 4.0f, 0.0f, 1.0f) * smooth01 ((st.turboShaft - 0.85f) * 6.7f);
        prNow = 1.0 + boost.maxBoostBar * st.turboShaft * st.turboShaft;
        const float ss = std::min (turboTarget (rpm, 1.0f), 1.0f);
        prTarget = 1.0 + boost.maxBoostBar * ss * ss;
    }
    if (supercharger)
    {
        double f = 0.0;
        if (boost.type == BoostType::CentrifugalSupercharger)
            f = std::pow (clampv (rpm / redline, 0.0, 1.1), 2.0);
        else
            f = std::pow (clampv (rpm / (0.35 * redline), 0.0, 1.0), 0.7);
        const double pr = 1.0 + boost.maxBoostBar * f;
        prNow = std::max (prNow, pr);
        prTarget = std::max (prTarget, pr);
    }
    const double boostFactor = clampv (prNow / prTarget, 0.3, 1.1);
    st.boostBar = static_cast<float> ((0.25 + 0.75 * st.airflow) * prNow - 1.0);

    // blow-off / surge event on a quick lift with boost built
    st.throttleLiftTimer += static_cast<float> (dt);
    if (turbo && (prevThrottle - st.throttle) / dt > 2.0 && st.throttle < 0.35f)
    {
        const float compressorBoost = static_cast<float> (boost.maxBoostBar) * st.turboShaft * st.turboShaft;
        if (compressorBoost > 0.2f && st.throttleLiftTimer > 0.25f)
        {
            ++st.blowOffCount;
            st.blowOffStrength = clampv (compressorBoost / static_cast<float> (std::max (0.1, boost.maxBoostBar)), 0.0f, 1.0f);
            st.throttleLiftTimer = 0.0f;
        }
    }

    // ---- airflow (cylinder filling) and idle control
    if (c.mode == DriveMode::Dyno)
    {
        st.airflow += (clampv (c.dynoLoad, 0.0f, 1.0f) - st.airflow) * static_cast<float> (std::min (1.0, dt / 0.02));
        st.idleAir = 0.0f;
    }
    else
    {
        float demand = clampv (throttleMap.eval (st.throttle), 0.0f, 1.0f);
        if (st.combustion)
        {
            const double denom = twot + tf + pmepFull;
            const float ff = denom > 0.0 ? static_cast<float> ((frictionTorqueAt (idleRpm) + pmepFull) / (std::max (0.0, static_cast<double> (torqueWot.eval (idleRpm))) + frictionTorqueAt (idleRpm) + pmepFull)) : 0.1f;
            const float err = static_cast<float> ((idleRpm - rpm) / idleRpm);
            st.idleIntegral = clampv (st.idleIntegral + 0.8f * err * static_cast<float> (dt), -0.2f, 0.3f);
            st.idleAir = clampv (ff + 1.2f * err + st.idleIntegral, 0.0f, 0.45f);
        }
        else
        {
            st.idleAir = 0.0f;
            st.idleIntegral = 0.0f;
        }
        const float target = std::max (demand, st.idleAir);
        st.airflow += (target - st.airflow) * static_cast<float> (std::min (1.0, dt / throttleTau));
    }
    st.airflow = clampv (st.airflow, 0.0f, 1.0f);

    // ---- torques
    double tind = 0.0;
    if (st.combustion)
    {
        tind = st.airflow * boostFactor * (twot + frictionTorqueAt (rpm)) * c.torqueScale;
        if (st.fuelCut) tind = 0.0;
    }
    const double tp = pmepFull * (1.0 - st.airflow) * std::tanh (st.omega / 20.0);
    double text = 0.0;
    double tstart = 0.0;
    st.starterActive = c.starter && c.mode == DriveMode::FreeRev && rpm < 450.0 && c.ignition;
    if (st.starterActive)
        tstart = 2.5 * frictionTorqueAt (200.0) * std::max (0.0, 1.0 - rpm / 420.0) + 0.6 * pmepFull;

    if (c.mode == DriveMode::Dyno)
    {
        // hold speed: ramp toward the target, the dyno absorbs the difference
        const double target = rpmToRadPerSec (clampv (static_cast<double> (c.dynoRpm), 0.0, limiter * 1.1));
        const double maxStep = rpmToRadPerSec (c.dynoRampRpmPerSec) * dt;
        st.omega += clampv (target - st.omega, -maxStep, maxStep);
        text = tind - tf - tp;
    }
    else
    {
        text = c.useLoadTorque ? c.loadTorqueNm : clampv (static_cast<double> (c.externalLoad), 0.0, 1.0) * peakT * std::tanh (st.omega / 30.0);
        const double net = tind - tf - tp - text + tstart;
        st.omega += net / (J + std::max (0.0, c.extraInertia)) * dt;
        if (st.omega < 0.0) st.omega = 0.0;
    }
    st.rpm = radPerSecToRpm (st.omega);

    st.indicatedTorque = static_cast<float> (tind);
    st.frictionTorque = static_cast<float> (tf);
    st.pumpingTorque = static_cast<float> (tp);
    st.externalTorque = static_cast<float> (text);
    st.brakeTorque = static_cast<float> (tind - tf - tp);
    st.powerKw = static_cast<float> (st.brakeTorque * st.omega / 1000.0);

    // ---- temperatures (first order, illustrative)
    const bool burning = st.combustion && ! st.fuelCut;
    const float egtTarget = burning ? 520.0f + 480.0f * st.airflow * static_cast<float> (0.4 + 0.6 * clampv (rpm / redline, 0.0, 1.2)) : 420.0f;
    st.egtK += (egtTarget - st.egtK) * static_cast<float> (dt / 1.2);
    const float coolTarget = burning ? 90.0f : 20.0f;
    st.coolantC += (coolTarget - st.coolantC) * static_cast<float> (dt / (burning ? 240.0 : 1800.0));
}
} // namespace esd
