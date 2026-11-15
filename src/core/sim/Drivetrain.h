#pragma once
// Transmission + simplified vehicle. A separate system from the engine / motor model: each
// simulation step it tells the power source what load torque and extra inertia it sees, then
// advances the vehicle from the source speed (see docs/MODEL.md, section drivetrain).
//
//  locked clutch / lock-up : one inertia  J_source + m r^2 / R^2, road force reflected F r / (R eta)
//  slipping clutch         : clutch torque = capacity x engagement x sign(slip) on both sides
//  torque converter        : pump torque (w_e / K)^2 (1 - SR^2), torque ratio 2 -> 1 at SR 0.85
//  CVT                     : ratio chosen to hold a target engine speed, rate limited
//  road                    : m g (Crr cos a + sin a) + 0.5 rho CdA v^2 + brake
#include "../model/Project.h"

#include <cstdint>

namespace esd
{
struct DriverInput
{
    float throttle = 0.0f;
    float brake = 0.0f;
    int shiftRequest = 0; ///< +1 up / -1 down, consumed once
};

struct DrivetrainState
{
    int gear = 1;              ///< 0 = neutral
    int numGears = 1;
    double gearRatio = 1.0;    ///< gearbox (or CVT) ratio
    double totalRatio = 1.0;   ///< x final drive
    double speedMs = 0.0;
    double inputOmega = 0.0;   ///< gearbox input shaft
    float clutch = 1.0f;       ///< 0 open .. 1 engaged
    bool locked = false;
    bool shifting = false;
    float converterSR = 1.0f;  ///< speed ratio turbine / pump
    bool lockup = false;
    float torqueScale = 1.0f;  ///< torque request scaling during shifts
    uint32_t shiftCount = 0;
    int shiftDir = 0;
    float shiftStrength = 0.0f;
    float inputTorque = 0.0f;  ///< torque through the gearbox input (Nm)
    float slipRpm = 0.0f;
    int virtualGear = 1;       ///< EV artificial shifts
    double timeSinceShift = 10.0;
};

struct Coupling
{
    double loadTorque = 0.0;
    double extraInertia = 0.0;
    float torqueScale = 1.0f;
    double snapOmega = -1.0; ///< >= 0: the source speed must be set to this (clutch / lock-up just closed)
};

class Drivetrain
{
public:
    void configure (const Project&, double sourceIdleRpm, double sourcePeakTorque, double sourceRedline, bool electricSource);
    void reset();
    Coupling couple (double sourceOmega, const DriverInput&, double dt);
    void advance (double sourceOmega, double dt);
    const DrivetrainState& state() const { return st; }
    void adoptState (const DrivetrainState& s) { st = s; }
    double roadForce (double v, float brake) const;

private:
    void startShift (int dir);
    void autoShiftLogic (double sourceOmega, float throttle);
    double ratioForGear (int g) const;

    TransmissionSpec t;
    VehicleSpec veh;
    DrivetrainState st;
    double idleOmega = 80.0, peakTorque = 200.0, redlineOmega = 700.0;
    bool electric = false;
    double clutchCap = 300.0, K = 10.0;
    double shiftTimer = 0.0, shiftTime = 0.35;
    int pendingGear = 1;
    float throttle = 0.0f, brake = 0.0f;
    double lastClutchTorque = 0.0;
    double lastTurbineTorque = 0.0;
    bool converter = false;
};
} // namespace esd
