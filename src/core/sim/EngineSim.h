#pragma once
// Simplified mean-value engine model (crank speed dynamics). See docs/MODEL.md.
//
//  J dw/dt = T_indicated - T_friction - T_pumping - T_external (+ T_starter)
//  T_indicated = airflow * boostFactor * (T_wot_brake(rpm) + T_friction(rpm))   (so WOT steady state == torque curve)
//  T_friction  = FMEP(rpm) * Vd / (2 pi n_R)    FMEP = 0.97 + 0.15 N + 0.05 N^2 bar (N in 1000 rpm)
//  T_pumping   = PMEP * Vd / (2 pi n_R)         PMEP = 0.9 bar * (1 - airflow)
// airflow follows the throttle map through a first-order lag (manifold filling) and an idle PI
// controller. Turbo: shaft speed first order lag toward a target set by exhaust energy.
//
// Everything here is allocation free and deterministic: it runs on the audio thread.
#include "../base/Curve.h"
#include "../model/Project.h"

#include <cstdint>

namespace esd
{
enum class DriveMode
{
    FreeRev, ///< engine accelerates according to torque balance (external load torque applies)
    Dyno,    ///< engine speed is held at the target rpm, load is set directly (loop rendering)
    Vehicle  ///< connected to the transmission and the vehicle (phase 5)
};

struct EngineControls
{
    float throttle = 0.0f;       ///< pedal 0..1
    float externalLoad = 0.0f;   ///< FreeRev: external load as fraction of peak torque 0..1
    bool ignition = false;
    bool starter = false;
    bool limiterEnabled = true;
    DriveMode mode = DriveMode::FreeRev;
    float dynoRpm = 3000.0f;     ///< Dyno target
    float dynoLoad = 0.5f;       ///< Dyno: airflow fraction 0..1
    float dynoRampRpmPerSec = 4000.0f;
    // ---- vehicle driver inputs
    float brake = 0.0f;
    uint32_t shiftUpCount = 0, shiftDownCount = 0; ///< monotonically increasing request counters
    // ---- drivetrain coupling (vehicle mode)
    bool useLoadTorque = false;  ///< true: loadTorqueNm replaces externalLoad
    double loadTorqueNm = 0.0;   ///< signed load seen by the crank (negative = driven by the vehicle / motor)
    double extraInertia = 0.0;   ///< reflected vehicle inertia when the clutch is locked
    float torqueScale = 1.0f;    ///< combustion torque scaling (shift torque cut)
    bool forceFuelCut = false;   ///< dyno: overrun (deceleration) rendering
};

struct EngineState
{
    double omega = 0.0;          ///< rad/s
    double rpm = 0.0;
    float throttle = 0.0f;       ///< pedal after input
    float airflow = 0.0f;        ///< 0..1, cylinder filling (engine load)
    float idleAir = 0.0f;
    float idleIntegral = 0.0f;
    bool combustion = false;     ///< ignition on and engine turning fast enough
    bool fuelCut = false;        ///< limiter or overrun cut active
    bool limiterActive = false;
    bool overrunCut = false;     ///< deceleration fuel cut-off
    bool starterActive = false;
    float indicatedTorque = 0.0f;
    float frictionTorque = 0.0f;
    float pumpingTorque = 0.0f;
    float externalTorque = 0.0f;
    float brakeTorque = 0.0f;    ///< net torque at the crank (Nm)
    float powerKw = 0.0f;
    float turboShaft = 0.0f;     ///< 0..1 normalised shaft speed
    float wastegateOpen = 0.0f;  ///< 0..1
    float boostBar = 0.0f;       ///< relative manifold pressure (negative = vacuum)
    float egtK = 600.0f;         ///< exhaust gas temperature
    float coolantC = 20.0f;
    uint32_t blowOffCount = 0;   ///< increments on each blow-off/surge event
    float blowOffStrength = 0.0f;
    float throttleLiftTimer = 10.0f; ///< seconds since the last lift-off
    double timeSec = 0.0;
};

class EngineSim
{
public:
    void configure (const Project&);
    void reset (bool running = false);
    void step (const EngineControls&, double dt);

    const EngineState& state() const { return st; }
    void adoptState (const EngineState& s) { st = s; }
    void setOmega (double w) { st.omega = std::max (0.0, w); st.rpm = w * 60.0 / 6.283185307179586; }

    double inertia() const { return J; }
    double peakTorque() const { return peakT; }
    double frictionTorqueAt (double rpm) const;
    double wotTorqueAt (double rpm) const { return static_cast<double> (torqueWot.eval (rpm)); }
    /// Steady-state target turbo shaft speed for a given rpm/airflow.
    float turboTarget (double rpm, float airflow) const;

private:
    EngineState st;
    CurveTable torqueWot;
    CurveTable throttleMap;
    double J = 0.15;
    double vdM3 = 0.002;
    double mepToTorque = 0.0; ///< Nm per Pa of mean effective pressure
    double frictionScale = 1.0;
    double idleRpm = 800, redline = 6500, limiter = 6800, limiterHyst = 150;
    double peakT = 200.0;
    float throttleTau = 0.08f;
    BoostSpec boost;
    bool turbo = false, supercharger = false;
    bool diesel = false;
};
} // namespace esd
