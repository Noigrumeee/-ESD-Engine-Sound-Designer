#include "ElectricSim.h"
#include "../base/Math.h"

#include <cmath>

namespace esd
{
void ElectricSim::configure (const ElectricSpec& s)
{
    m = s;
    kt = m.peakTorqueNm / std::max (1.0, m.maxCurrentA);
}

void ElectricSim::reset()
{
    st = MotorState {};
    st.tempC = 30.0f;
}

double ElectricSim::pedalTorque (float throttle, float brake) const
{
    const double avail = m.torqueAt (st.rpm);
    if (throttle > 0.02f) return throttle * avail;
    // lift-off regeneration fades out at very low speed (no creep backwards)
    const double fade = clampv (st.rpm / 800.0, 0.0, 1.0);
    return -(m.regenStrength + 0.6 * brake) * avail * fade;
}

void ElectricSim::updateElectrical (double dt)
{
    // current loop: 5 ms
    const double avail = m.torqueAt (st.rpm);
    const double cmd = clampv (static_cast<double> (st.torqueCmd), -avail, avail);
    st.torque += static_cast<float> ((cmd - st.torque) * std::min (1.0, dt / 0.005));
    const double p = st.torque * st.omega;
    st.powerKw = static_cast<float> (p / 1000.0);
    const double iTorque = std::abs (st.torque) / std::max (0.01, kt);
    const double iPower = std::abs (p) / (0.95 * std::max (1.0, m.voltage));
    st.currentA = static_cast<float> (std::min (m.maxCurrentA * 1.2, std::max (iTorque, iPower) + 0.03 * m.maxCurrentA));
    st.loadFraction = avail > 0.0 ? static_cast<float> (std::abs (st.torque) / avail) : 0.0f;
    st.regen = st.torque < -0.5f && st.omega > 1.0;
    // electrical frequency (induction: the field runs ahead of the rotor by the slip)
    st.slip = m.type == ElectricMotorType::Induction ? static_cast<float> (m.ratedSlip * st.loadFraction) * (st.regen ? -1.0f : 1.0f) : 0.0f;
    st.electricalHz = static_cast<float> (st.rpm / 60.0 * m.polePairs() / std::max (0.5, 1.0 - st.slip));
    // thermal: 3 I^2 R + iron losses ~ f, lumped capacity 9 kJ/K
    const double losses = 3.0 * st.currentA * st.currentA * m.resistanceOhm + 0.002 * m.peakPowerKw * 1000.0 * (st.rpm / std::max (1.0, m.maxRpm));
    const double h = m.cooling == CoolingType::Liquid ? 60.0 : 25.0;
    st.tempC += static_cast<float> ((losses - h * (st.tempC - 30.0)) / 9000.0 * dt);
}

void ElectricSim::step (double torqueCmd, double loadTorque, double extraInertia, double dt)
{
    st.torqueCmd = static_cast<float> (torqueCmd);
    updateElectrical (dt);
    const double friction = (0.004 * m.peakTorqueNm + 2e-6 * m.peakTorqueNm * st.omega) * std::tanh (st.omega / 2.0);
    const double J = m.rotorInertia + std::max (0.0, extraInertia);
    st.omega += (st.torque - friction - loadTorque) / J * dt;
    st.omega = clampv (st.omega, 0.0, rpmToRadPerSec (m.maxRpm * 1.02));
    st.rpm = radPerSecToRpm (st.omega);
}

void ElectricSim::stepImposedSpeed (double omega, double torqueCmd, double dt)
{
    st.omega = std::max (0.0, omega);
    st.rpm = radPerSecToRpm (st.omega);
    st.torqueCmd = static_cast<float> (torqueCmd);
    updateElectrical (dt);
}
} // namespace esd
