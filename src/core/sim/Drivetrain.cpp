#include "Drivetrain.h"
#include "../base/Math.h"

#include <cmath>

namespace esd
{
namespace
{
constexpr double g = 9.81;
constexpr double rho = 1.2;
} // namespace

void Drivetrain::configure (const Project& p, double idleRpm, double peakT, double redlineRpm, bool electricSource)
{
    t = p.transmission;
    veh = p.vehicle;
    electric = electricSource;
    idleOmega = rpmToRadPerSec (idleRpm);
    peakTorque = std::max (1.0, peakT);
    redlineOmega = rpmToRadPerSec (redlineRpm);
    clutchCap = t.clutchCapacityNm > 0.0 ? t.clutchCapacityNm : 1.6 * peakTorque;
    converter = t.type == TransmissionType::TorqueConverter;
    K = rpmToRadPerSec (t.converterStallRpm) / std::sqrt (peakTorque);
    shiftTime = t.shiftTimeMs * 0.001;
    if (t.type == TransmissionType::DCT) shiftTime = std::min (shiftTime, 0.12);
    reset();
}

void Drivetrain::reset()
{
    st = DrivetrainState {};
    st.numGears = (t.type == TransmissionType::CVT || t.type == TransmissionType::EVFixed) ? 1 : static_cast<int> (t.ratios.size());
    st.gear = 1;
    st.gearRatio = t.type == TransmissionType::CVT ? t.cvtRatioMax : ratioForGear (1);
    st.totalRatio = st.gearRatio * t.finalDrive;
    st.clutch = electric ? 1.0f : 0.0f;
    pendingGear = 1;
}

double Drivetrain::ratioForGear (int gear) const
{
    if (t.type == TransmissionType::EVFixed) return t.ratios.empty() ? 1.0 : t.ratios.front();
    if (t.ratios.empty()) return 1.0;
    return t.ratios[static_cast<std::size_t> (clampv (gear, 1, static_cast<int> (t.ratios.size())) - 1)];
}

double Drivetrain::roadForce (double v, float brakePedal) const
{
    const double a = std::atan (veh.gradePercent / 100.0);
    const double roll = veh.massKg * g * veh.rollingResistance * std::cos (a) * std::tanh (v / 0.5);
    const double grade = veh.massKg * g * std::sin (a);
    const double aero = 0.5 * rho * veh.dragArea * v * v;
    const double br = brakePedal * veh.brakeForceN * std::tanh (v / 0.3);
    return roll + grade + aero + br;
}

void Drivetrain::startShift (int dir)
{
    const int target = st.gear + dir;
    if (target < 1 || target > st.numGears || st.shifting) return;
    pendingGear = target;
    st.shifting = true;
    shiftTimer = 0.0;
    st.shiftDir = dir;
    ++st.shiftCount;
    st.shiftStrength = clampv (0.3f + throttle * 0.7f, 0.0f, 1.0f);
    st.timeSinceShift = 0.0;
}

void Drivetrain::autoShiftLogic (double sourceOmega, float thr)
{
    const bool automatic = t.type == TransmissionType::TorqueConverter || t.type == TransmissionType::DCT || t.type == TransmissionType::AMT
                           || t.type == TransmissionType::EVMultiSpeed;
    if (! automatic || st.shifting || st.timeSinceShift < 0.8) return;
    // shift points on the output speed (input speed in the current gear), not on a slipping converter
    const double inRpm = radPerSecToRpm (st.speedMs / veh.wheelRadiusM * st.totalRatio);
    const double up = t.upshiftRpmLight + (t.upshiftRpmFull - t.upshiftRpmLight) * thr;
    if (inRpm > up && st.gear < st.numGears) { startShift (+1); return; }
    if (inRpm < t.downshiftRpm && st.gear > 1) { startShift (-1); return; }
    if (thr >= t.kickdownThrottle && st.gear > 1)
    {
        const double after = inRpm * ratioForGear (st.gear - 1) / ratioForGear (st.gear);
        if (after < t.upshiftRpmFull * 0.9) startShift (-1);
    }
    (void) sourceOmega;
}

Coupling Drivetrain::couple (double sourceOmega, const DriverInput& in, double dt)
{
    throttle = clampv (in.throttle, 0.0f, 1.0f);
    brake = clampv (in.brake, 0.0f, 1.0f);
    st.timeSinceShift += dt;
    if (in.shiftRequest != 0 && t.type != TransmissionType::CVT && t.type != TransmissionType::EVFixed) startShift (in.shiftRequest > 0 ? 1 : -1);
    autoShiftLogic (sourceOmega, throttle);

    // ---- EV artificial shifts: sound + torque dip, the reduction does not change
    if (t.type == TransmissionType::EVFixed && t.artificialShifts)
    {
        const int vg = 1 + std::min (5, static_cast<int> (st.speedMs * 3.6 / 35.0));
        if (vg != st.virtualGear)
        {
            st.shiftDir = vg > st.virtualGear ? 1 : -1;
            st.virtualGear = vg;
            ++st.shiftCount;
            st.shiftStrength = 0.4f + 0.6f * throttle;
            st.timeSinceShift = 0.0;
        }
    }

    // ---- shift sequence
    st.torqueScale = 1.0f;
    float clutchTarget = 1.0f;
    if (st.shifting)
    {
        shiftTimer += dt;
        const double ph = shiftTimer / std::max (0.02, shiftTime);
        const bool dct = t.type == TransmissionType::DCT;
        auto switchGear = [this]
        {
            if (st.gear != pendingGear) { st.gear = pendingGear; st.locked = false; } // the clutch re-synchronises the speeds
        };
        if (converter)
        {
            st.torqueScale = 0.7f; // torque management during a powershift
            if (ph > 0.5) switchGear();
        }
        else if (dct)
        {
            st.torqueScale = 0.6f; // clutch to clutch handover, no interruption
            if (ph > 0.5) switchGear();
        }
        else
        {
            st.torqueScale = t.type == TransmissionType::AMT ? 0.1f : 0.0f;
            clutchTarget = ph < 0.35 ? static_cast<float> (1.0 - ph / 0.35) : ph < 0.45 ? 0.0f : static_cast<float> ((ph - 0.45) / 0.55);
            if (ph >= 0.4) switchGear();
        }
        if (ph >= 1.0) st.shifting = false;
    }
    if (t.type == TransmissionType::EVFixed && st.timeSinceShift < 0.15) st.torqueScale = 0.55f;

    // ---- ratio
    if (t.type == TransmissionType::CVT)
    {
        const double targetRpm = t.cvtRpmLight + (t.cvtRpmFull - t.cvtRpmLight) * throttle;
        const double wheel = st.speedMs / veh.wheelRadiusM;
        double target = wheel > 0.5 ? rpmToRadPerSec (targetRpm) / (wheel * t.finalDrive) : t.cvtRatioMax;
        target = clampv (target, t.cvtRatioMin, t.cvtRatioMax);
        const double maxStep = 1.2 * dt;
        st.gearRatio += clampv (target - st.gearRatio, -maxStep, maxStep);
    }
    else
        st.gearRatio = ratioForGear (st.gear);
    st.totalRatio = st.gearRatio * t.finalDrive;

    const double R = st.totalRatio;
    const double r = veh.wheelRadiusM;
    const double inOmega = st.speedMs / r * R;
    st.inputOmega = inOmega;
    const double F = roadForce (st.speedMs, brake);
    const double eta = t.efficiency;

    Coupling c;
    c.torqueScale = st.torqueScale;

    if (converter)
    {
        const bool wasLocked = st.lockup;
        st.lockup = st.gear >= t.lockupFromGear && st.converterSR > 0.85f && throttle < 0.75f && ! st.shifting;
        if (st.lockup)
        {
            if (! wasLocked) c.snapOmega = inOmega; // lock-up clutch closes: engine pulled to the turbine speed
            st.locked = true;
            c.loadTorque = F * r / R / (F > 0 ? eta : 1.0 / eta);
            c.extraInertia = veh.massKg * r * r / (R * R);
            st.converterSR = 1.0f;
            return c;
        }
        st.locked = false;
        const double we = std::max (0.0, sourceOmega);
        const double sr = we > 1.0 ? inOmega / we : (inOmega > 0.1 ? 2.0 : 1.0);
        st.converterSR = static_cast<float> (sr);
        double tp;
        if (sr < 1.0) tp = (we / K) * (we / K) * (1.0 - sr * sr);
        else tp = -(inOmega / K) * (inOmega / K) * (sr * sr - 1.0) * 0.5;
        const double tr = sr < 0.85 ? 2.0 - sr / 0.85 : 1.0;
        lastTurbineTorque = tp > 0 ? tr * tp : tp;
        c.loadTorque = tp;
        st.inputTorque = static_cast<float> (lastTurbineTorque);
        st.slipRpm = static_cast<float> (radPerSecToRpm (we - inOmega));
        return c;
    }

    // ---- launch clutch (automated) for clutch-type gearboxes; electric motors start in gear
    if (! st.shifting)
    {
        if (electric) clutchTarget = 1.0f;
        else if (st.speedMs < 3.0 && ! st.locked)
            clutchTarget = static_cast<float> (clampv ((sourceOmega - idleOmega * 1.05) / rpmToRadPerSec (900.0), 0.0, 1.0)) * (throttle > 0.01f || st.speedMs > 0.3 ? 1.0f : 0.0f);
        else if (st.speedMs < 3.0 && st.locked && sourceOmega < idleOmega * 0.95 && throttle < 0.05f)
            clutchTarget = 0.0f; // stopping: open the clutch before the engine stalls
    }
    st.clutch += static_cast<float> ((clutchTarget - st.clutch) * std::min (1.0, dt / 0.06));

    const double slip = sourceOmega - inOmega;
    st.slipRpm = static_cast<float> (radPerSecToRpm (slip));
    if (st.locked && st.clutch < 0.98f)
        st.locked = false;
    if (! st.locked && st.clutch >= 0.98f && std::abs (slip) < rpmToRadPerSec (40.0))
    {
        st.locked = true;
        c.snapOmega = inOmega;
    }

    if (st.locked)
    {
        c.loadTorque = F * r / R / (F > 0 ? eta : 1.0 / eta);
        c.extraInertia = veh.massKg * r * r / (R * R);
        st.inputTorque = 0.0f; // filled by advance() from the source torque
        return c;
    }
    const double cap = clutchCap * st.clutch;
    lastClutchTorque = cap * clampv (slip / rpmToRadPerSec (60.0), -1.0, 1.0);
    c.loadTorque = lastClutchTorque;
    st.inputTorque = static_cast<float> (lastClutchTorque);
    return c;
}

void Drivetrain::advance (double sourceOmega, double dt)
{
    const double R = st.totalRatio;
    const double r = veh.wheelRadiusM;
    const double F = roadForce (st.speedMs, brake);
    if (st.locked)
    {
        const double vPrev = st.speedMs;
        st.speedMs = std::max (0.0, sourceOmega * r / R);
        // torque actually transmitted (for the gear whine): what accelerates the car + road load
        const double a = (st.speedMs - vPrev) / std::max (1e-6, dt);
        st.inputTorque = static_cast<float> ((veh.massKg * a + F) * r / R);
        st.inputOmega = sourceOmega;
        st.converterSR = 1.0f;
        return;
    }
    const double drive = converter ? lastTurbineTorque : lastClutchTorque;
    const double wheelForce = drive * R * t.efficiency / r;
    st.speedMs += (wheelForce - F) / veh.massKg * dt;
    if (st.speedMs < 0.0) st.speedMs = 0.0;
    st.inputOmega = st.speedMs / r * R;
}
} // namespace esd
