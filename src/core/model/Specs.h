#pragma once
// Plain data describing a thermal powertrain. No behaviour beyond derived quantities:
// the simulation (sim/) and the synthesis (synth/) read these structures.
#include "../base/Curve.h"
#include "EngineLayout.h"

#include <array>
#include <string>
#include <vector>

namespace esd
{
enum class CamType { OHV, SOHC, DOHC, PortTwoStroke, PortRotary };
enum class LimiterType { FuelCut, IgnitionCut, SoftCut };
enum class FuelType { Gasoline, Diesel, Ethanol };

struct ThermalEngineSpec
{
    std::string layoutId = "i4";
    std::string crankId = "flat";
    std::vector<int> firingOrder; ///< empty = reference order of the crank variant
    double bankAngleDeg = 0.0;    ///< 0 = nominal angle of the crank variant

    double boreMm = 86.0;
    double strokeMm = 86.0;
    double compressionRatio = 10.5;

    double idleRpm = 850.0;
    double redlineRpm = 6800.0;
    double limiterRpm = 7000.0;
    double limiterHysteresisRpm = 180.0;
    LimiterType limiterType = LimiterType::FuelCut;

    int valvesPerCylinder = 4;
    CamType cam = CamType::DOHC;
    double exhaustDurationDeg = 240.0; ///< exhaust valve open duration (pulse width)
    double camOverlapDeg = 30.0;       ///< overlap: idle lope / reversion
    FuelType fuel = FuelType::Gasoline;
    double combustionHarshness = 0.4;  ///< 0 smooth .. 1 harsh (pressure rise rate) - approximation
    double combustionVariability = 0.3;///< cycle-to-cycle variation 0..1 - approximation
    double overrunCrackle = 0.15;      ///< afterfire probability on overrun 0..1 - acoustic

    double flywheelInertia = 0.0; ///< kg m^2, 0 = estimated from displacement
    double frictionScale = 1.0;
    double throttleResponseMs = 80.0; ///< intake manifold filling time constant

    Curve torqueCurve;  ///< brake torque at wide-open throttle [Nm] vs rpm (full boost if boosted)
    Curve throttleMap;  ///< pedal 0..1 -> airflow fraction 0..1

    // ---- derived (computed, exact for the given geometry)
    double displacementCc() const;
    double cylinderDisplacementCc() const;
    double boreStrokeRatio() const { return strokeMm > 0 ? boreMm / strokeMm : 0.0; }
    double meanPistonSpeed (double rpm) const { return 2.0 * strokeMm * 1e-3 * rpm / 60.0; }
    /// Sets bore/stroke from a total displacement keeping the bore/stroke ratio.
    void setDisplacementCc (double cc);

    bool operator== (const ThermalEngineSpec&) const = default;
};

enum class AirFilterType { StockAirbox, PanelFilter, OpenCone, VelocityStacks };
enum class IntakeManifoldType { SinglePlenum, DualPlenum, IndividualThrottles };

struct IntakeSpec
{
    AirFilterType filter = AirFilterType::StockAirbox;
    IntakeManifoldType manifold = IntakeManifoldType::SinglePlenum;
    double airboxVolumeL = 8.0;
    double snorkelLengthMm = 250.0;
    double snorkelDiameterMm = 70.0;
    double runnerLengthMm = 300.0;
    double runnerDiameterMm = 42.0;
    double plenumVolumeL = 4.0;
    double throttleDiameterMm = 65.0;
    bool resonatorEnabled = false;
    double resonatorFreqHz = 180.0;
    bool operator== (const IntakeSpec&) const = default;
};

enum class HeaderType { CastManifold, ShortTube, LongTube, TriY };
enum class ExhaustRouting { Single, Dual, DualXPipe, DualHPipe };
enum class CatalystType { None, Standard, HighFlow };
enum class MufflerType { None, StraightThrough, Chambered, Turbo };
enum class OutletPosition { Center, Left, Right, Split };

struct ExhaustSpec
{
    HeaderType header = HeaderType::CastManifold;
    double primaryLengthMm = 450.0;
    double primaryDiameterMm = 38.0;
    double unequalLength = 0.0; ///< 0 = equal-length primaries, 1 = strongly unequal (boxer rumble)
    ExhaustRouting routing = ExhaustRouting::Single;
    double midpipeLengthMm = 1600.0;
    double pipeDiameterMm = 55.0;
    CatalystType catalyst = CatalystType::Standard;
    bool resonatorEnabled = true;
    double resonatorLengthMm = 400.0; ///< quarter-wave side branch length
    MufflerType muffler = MufflerType::Chambered;
    double mufflerVolumeL = 12.0;
    double tailpipeLengthMm = 500.0;
    double tailpipeDiameterMm = 55.0;
    int outlets = 1;
    OutletPosition outletPosition = OutletPosition::Right;
    bool operator== (const ExhaustSpec&) const = default;
};

enum class BoostType { None, Turbo, TwinTurbo, RootsSupercharger, TwinScrewSupercharger, CentrifugalSupercharger, Twincharged };
enum class BlowOffType { None, Recirculating, Atmospheric };

struct BoostSpec
{
    BoostType type = BoostType::None;
    double maxBoostBar = 1.0;     ///< relative pressure at full boost
    double turboSize = 0.5;       ///< 0 small/fast .. 1 big/laggy
    double spoolRpm = 3000.0;     ///< rpm at which full boost is reached at wide-open throttle
    double responseMs = 600.0;    ///< shaft time constant (inertia)
    bool externalWastegate = false;
    BlowOffType blowOff = BlowOffType::Recirculating;
    double whistleMinHz = 900.0;  ///< acoustic mapping of shaft speed -> audible whistle
    double whistleMaxHz = 4200.0;
    double superchargerDriveRatio = 2.0;
    int superchargerLobes = 3;
    bool operator== (const BoostSpec&) const = default;
};

// ---------------------------------------------------------------- electric machines (phase 4)
enum class ElectricMotorType { PMSM, BLDC, Induction, AxialFlux, OuterRotor };
enum class MotorController { FOC, SixStep, ScalarVf };
enum class SemiconductorType { SiIGBT, SiCMOSFET, GaN };
enum class CoolingType { Air, Liquid };
enum class RotorType { SurfacePM, InteriorPM, SquirrelCage, WoundRotor };
enum class StatorWinding { Distributed, Concentrated, Hairpin };

struct ElectricSpec
{
    ElectricMotorType type = ElectricMotorType::PMSM;
    int poles = 8;             ///< magnetic poles (pole pairs = poles / 2)
    int slots = 48;            ///< stator slots
    int phases = 3;
    RotorType rotor = RotorType::InteriorPM;
    StatorWinding stator = StatorWinding::Distributed;
    int rotorBars = 58;        ///< induction rotor bars
    double maxRpm = 16000.0;
    double peakTorqueNm = 350.0;
    double peakPowerKw = 150.0;
    double voltage = 400.0;
    double maxCurrentA = 500.0;
    double ratedSlip = 0.03;   ///< induction: slip at rated torque
    MotorController controller = MotorController::FOC;
    double pwmHz = 10000.0;
    bool randomPwm = false;
    SemiconductorType semiconductor = SemiconductorType::SiIGBT;
    CoolingType cooling = CoolingType::Liquid;
    double rotorInertia = 0.06;  ///< kg m^2
    double regenStrength = 0.25; ///< torque fraction applied on lift-off (one pedal feel)
    int bearingBalls = 9;
    double resonance1Hz = 1900.0, resonance2Hz = 3600.0; ///< housing modes
    double resistanceOhm = 0.02; ///< phase resistance (copper losses -> temperature)

    double polePairs() const { return poles * 0.5; }
    /// Corner speed: constant torque below, constant power above (field weakening).
    double baseRpm() const;
    /// Available torque magnitude at a speed (motoring and regen are symmetric here).
    double torqueAt (double rpm) const;
    bool operator== (const ElectricSpec&) const = default;
};

// ---------------------------------------------------------------- drivetrain (phase 5)
enum class TransmissionType { Manual, TorqueConverter, DCT, CVT, AMT, EVFixed, EVMultiSpeed };

struct TransmissionSpec
{
    TransmissionType type = TransmissionType::Manual;
    std::vector<double> ratios { 3.6, 2.13, 1.39, 1.03, 0.84, 0.69 };
    double finalDrive = 3.9;
    double efficiency = 0.92;
    double shiftTimeMs = 350.0;
    double clutchCapacityNm = 0.0;     ///< 0 = 1.6 x peak torque
    double converterStallRpm = 2300.0;
    int lockupFromGear = 3;
    double upshiftRpmLight = 2300.0, upshiftRpmFull = 6200.0, downshiftRpm = 1300.0;
    double kickdownThrottle = 0.92;
    double cvtRatioMin = 0.45, cvtRatioMax = 2.6;
    double cvtRpmLight = 1600.0, cvtRpmFull = 5600.0;
    bool artificialShifts = false;     ///< EV: simulated gear changes (sound + torque dip)
    int pinionTeeth = 23;              ///< gear mesh order of the input stage
    bool operator== (const TransmissionSpec&) const = default;
};

struct VehicleSpec
{
    double massKg = 1450.0;
    double rollingResistance = 0.012;
    double dragArea = 0.68;      ///< Cd x A [m^2]
    double wheelRadiusM = 0.32;
    double brakeForceN = 14000.0;
    double gradePercent = 0.0;
    bool operator== (const VehicleSpec&) const = default;
};

// ---------------------------------------------------------------- hybrid (phase 6)
enum class HybridArch { Mild, Full, PlugIn, Series, Parallel, SeriesParallel };

struct HybridSpec
{
    HybridArch arch = HybridArch::SeriesParallel;
    double batteryKWh = 1.6;
    double socInitial = 0.6, socMin = 0.3, socMax = 0.8;
    double maxBatteryKw = 60.0;
    double evMaxSpeedKmh = 55.0;    ///< above this the engine runs
    double evMaxPowerKw = 25.0;     ///< above this demand the engine starts
    double engineMinOnSeconds = 4.0;
    double chargePowerKw = 8.0;     ///< extra engine load used to recharge when the SOC is low
    double regenMaxKw = 40.0;
    double motorAssist = 1.0;       ///< parallel / mild: fraction of motor torque added under load
    bool operator== (const HybridSpec&) const = default;
};

// ---------------------------------------------------------------- loop generator (phase 7)
struct LoopSpec
{
    double rpmMin = 0.0;   ///< 0 = idle (thermal) / 300 rpm (electric)
    double rpmMax = 0.0;   ///< 0 = redline (thermal) / max rpm (electric)
    int rpmPoints = 12;
    int loadPoints = 3;    ///< thermal: load 0..1 ; electric: torque 0..1 (and -1..0 with regen)
    bool overrunState = true; ///< thermal: extra fuel-cut (deceleration) loops
    bool regenState = true;   ///< electric: negative torque loops
    double loopSeconds = 2.0;
    double crossfadeMs = 30.0;
    int sampleRate = 48000;
    int bitDepth = 24;        ///< 16, 24, 32 (float)
    bool flac = false;
    bool stems = false;
    bool normalize = true;
    bool startStop = true;    ///< also render start-up / shut-down one-shots
    int threads = 0;          ///< 0 = hardware concurrency
    bool operator== (const LoopSpec&) const = default;
};

// ---------------------------------------------------------------- layers and mix (phase 3)
enum class LayerId : int
{
    Combustion = 0,
    Intake,
    Exhaust,
    Mechanical,
    Boost,
    Vibration,
    Electromagnetic,
    Whine,
    Rotor,
    Bearings,
    Inverter,
    Cooling,
    Resonance,
    Transmission,
    Samples,
    Count
};
inline constexpr int kNumLayers = static_cast<int> (LayerId::Count);
const char* layerName (LayerId);
/// powertrainKind: 0 thermal, 1 electric, 2 hybrid
bool layerUsedBy (LayerId, int powertrainKind);

struct LayerMix
{
    float gainDb = 0.0f;
    float pan = 0.0f; ///< -1 .. 1, added to the layer's natural placement
    bool mute = false;
    bool solo = false;
    bool operator== (const LayerMix&) const = default;
};

/// Per-layer effect chain: HP -> 3-band EQ -> LP -> saturation -> compressor -> convolution -> reverb send.
struct LayerFx
{
    float highpassHz = 0.0f;  ///< 0 = off
    float lowpassHz = 0.0f;   ///< 0 = off
    float lowDb = 0.0f, midDb = 0.0f, midHz = 1000.0f, highDb = 0.0f;
    float drive = 0.0f;       ///< saturation 0..1
    float compThresholdDb = 0.0f; ///< 0 = off
    float compRatio = 3.0f;
    std::string irPath;       ///< impulse response for convolution (WAV), empty = off
    float convMix = 0.0f;
    float reverbSend = 0.0f;
    bool operator== (const LayerFx&) const = default;
};

enum class ModSource { Rpm, Torque, Load, Throttle, Speed, Gear, Temperature, Boost, ElectricFreq, Battery, Regen, ShiftEvent, Count };
enum class ModTarget { GainDb, Pan, LowpassHz, Drive, Count };
const char* toString (ModSource);
const char* toString (ModTarget);

/// One automation: target(layer) += curve(source normalised 0..1). Several can stack on one target.
struct Automation
{
    bool enabled = true;
    int layer = 0;
    ModSource source = ModSource::Rpm;
    ModTarget target = ModTarget::GainDb;
    Curve curve; ///< x: source 0..1 ; y: dB / pan -1..1 / lowpass 0..1 (log 100 Hz..20 kHz) / drive 0..1
    bool operator== (const Automation&) const = default;
};

struct SoundMix
{
    std::array<LayerMix, kNumLayers> layers {};
    std::array<LayerFx, kNumLayers> fx {};
    std::vector<Automation> automations;
    float masterGainDb = -6.0f;
    float stereoWidth = 1.0f;
    float reverbSize = 0.5f, reverbDamping = 0.4f, reverbReturnDb = -6.0f;
    bool operator== (const SoundMix&) const = default;
};
} // namespace esd
