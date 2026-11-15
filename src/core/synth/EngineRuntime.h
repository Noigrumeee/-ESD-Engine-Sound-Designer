#pragma once
// Powertrain runtime: simulation + synthesis + mixing, driven sample-accurately. The same object
// is used by the real-time audio callback and by the offline renderers, so the live preview,
// the WAV renders and the loop sets are produced by identical code.
//
//   thermal  : EngineSim -> ThermalVoice
//   electric : ElectricSim -> ElectricVoice
//   hybrid   : both + HybridLogic (engine on/off, power split, battery)
//   vehicle  : Drivetrain couples the power source to the vehicle, TransmissionVoice
//   always   : SampleLayer (mapped samples), LayerMixer (FX, automation, reverb, master)
#include "../sim/Drivetrain.h"
#include "../sim/ElectricSim.h"
#include "../sim/EngineSim.h"
#include "ElectricVoice.h"
#include "LayerBus.h"
#include "SampleLayer.h"
#include "ThermalVoice.h"
#include "TransmissionVoice.h"

namespace esd
{
struct Telemetry
{
    float rpm = 0, brakeTorque = 0, powerKw = 0, airflow = 0, throttle = 0;
    float boostBar = 0, egtK = 0, coolantC = 0, turboShaft = 0;
    bool combustion = false, fuelCut = false, limiter = false, starter = false;
    float firingHz = 0;
    float layerRms[kNumLayers] {};
    float crankDeg = 0;
    // electric
    float motorRpm = 0, motorTorque = 0, motorCurrent = 0, motorTempC = 0, electricHz = 0, motorPowerKw = 0;
    bool regen = false;
    // vehicle
    float speedKmh = 0;
    int gear = 0, numGears = 0;
    float clutch = 0, converterSR = 1;
    bool shifting = false;
    // hybrid
    float soc = 0, batteryKw = 0;
    bool engineOn = false;
    int hybridMode = 0; ///< 0 EV, 1 engine + motor, 2 engine charging, 3 regeneration, 4 engine only
};

struct HybridState
{
    float soc = 0.6f;
    bool engineOn = false;
    bool coupled = false;
    double onTimer = 0.0, offTimer = 10.0;
    float batteryKw = 0.0f;
    int mode = 0;
};

class EngineRuntime
{
public:
    static constexpr int kSimBlock = 32; ///< simulation step = 32 samples (0.67 ms at 48 kHz)

    struct Loaders
    {
        SampleLoader samples;
        IrLoader ir;
    };

    EngineRuntime (const Project&, double sampleRate, const Loaders& = {}); ///< allocates: never on the audio thread
    void process (const EngineControls&, const VoiceMix&, float* outL, float* outR, int n,
                  float* const* layerL = nullptr, float* const* layerR = nullptr);

    /// Copies the mechanical state of another runtime (hot swap after an edit).
    void adoptFrom (const EngineRuntime&);
    void resetEngine (bool running);
    Telemetry telemetry() const;
    double sampleRate() const { return fs; }
    PowertrainKind kind() const { return k; }
    /// Angle (deg) of the primary source inside its cycle, and that cycle length: used to start
    /// loops on a cycle boundary.
    double primaryAngleDeg() const;
    double primaryCycleDeg() const;

    const EngineState& state() const { return sim.state(); }

    EngineSim sim;
    ThermalVoice voice;
    ElectricSim motor;
    ElectricVoice motorVoice;
    Drivetrain drivetrain;
    TransmissionVoice gearVoice;
    SampleLayer samples;
    LayerMixer mixer;
    HybridState hybrid;

private:
    void stepThermal (const EngineControls&, double dt);
    void stepElectric (const EngineControls&, double dt);
    void stepHybrid (const EngineControls&, double dt);
    ModInputs modInputs() const;

    double fs;
    PowertrainKind k;
    Project proj;
    double enginePeakKw = 100.0, motorAngle = 0.0;
    uint32_t seenUp = 0, seenDown = 0;
    float shiftEnv = 0.0f;
    uint32_t lastShiftCount = 0;
    bool vehicleActive = false;
    std::vector<float> busData;
    LayerBus bus;
    // voice inputs captured during the step
    double engW0 = 0, engW1 = 0, motW0 = 0, motW1 = 0, inW0 = 0, inW1 = 0;
};
} // namespace esd
