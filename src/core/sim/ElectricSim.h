#pragma once
// Simplified electric machine model (see docs/MODEL.md, section electric):
//  * torque envelope: constant torque below the base speed, constant power above (field weakening);
//  * torque follows the command through a 1st-order current loop (5 ms);
//  * current = max(|T| / kt, |P| / (0.95 V)), kt = T_peak / I_max ; copper losses 3 I^2 R heat a
//    lumped thermal mass (illustrative temperature);
//  * rotor speed: J dw/dt = T - T_friction - T_load (or imposed by the dyno / the drivetrain).
#include "../model/Project.h"

namespace esd
{
struct MotorState
{
    double omega = 0.0, rpm = 0.0;
    float torque = 0.0f;       ///< Nm, negative = regeneration
    float torqueCmd = 0.0f;
    float currentA = 0.0f;
    float powerKw = 0.0f;      ///< mechanical, negative when regenerating
    float tempC = 30.0f;
    float electricalHz = 0.0f;
    float slip = 0.0f;         ///< induction
    float loadFraction = 0.0f; ///< |T| / T_available
    bool regen = false;
};

class ElectricSim
{
public:
    void configure (const ElectricSpec&);
    void reset();
    /// Torque command from the pedals (signed): throttle -> motoring, lift-off -> regen.
    double pedalTorque (float throttle, float brake) const;
    /// Integrates the rotor (free, or with the drivetrain load / inertia).
    void step (double torqueCmd, double loadTorque, double extraInertia, double dt);
    /// Dyno / imposed speed: the rotor follows the given speed, the torque follows the command.
    void stepImposedSpeed (double omega, double torqueCmd, double dt);
    const MotorState& state() const { return st; }
    void adoptState (const MotorState& s) { st = s; }
    const ElectricSpec& spec() const { return m; }
    double availableTorque (double rpm) const { return m.torqueAt (rpm); }

private:
    void updateElectrical (double dt);
    ElectricSpec m;
    MotorState st;
    double kt = 0.7;
};
} // namespace esd
