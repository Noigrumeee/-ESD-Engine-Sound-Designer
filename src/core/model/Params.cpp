#include "Params.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace esd
{
const char* toString (ParamTag t)
{
    switch (t)
    {
        case ParamTag::Mechanical: return "Mechanical";
        case ParamTag::Approximation: return "Approximation";
        case ParamTag::Acoustic: return "Acoustic";
    }
    return "";
}

namespace
{
template <class T> void assignNum (T& dst, double v)
{
    if constexpr (std::is_same_v<T, bool>)
        dst = v >= 0.5;
    else if constexpr (std::is_enum_v<T>)
        dst = static_cast<T> (static_cast<int> (std::lround (v)));
    else if constexpr (std::is_integral_v<T>)
        dst = static_cast<T> (std::lround (v));
    else
        dst = static_cast<T> (v);
}

template <class T> double readNum (const T& v)
{
    if constexpr (std::is_enum_v<T>)
        return static_cast<double> (static_cast<int> (v));
    else
        return static_cast<double> (v);
}

using Tag = ParamTag;
using K = ParamDef::Kind;

#define ESD_FIELD(field)                                                                              \
    d.get = [] (const Project& p) { return readNum (p.field); };                                      \
    d.set = [] (Project& p, double v) { assignNum (p.field, v); };

ParamDef make (const char* id, const char* group, const char* label, const char* unit, double lo, double hi, int decimals, Tag tag,
               const char* help, K kind = K::Number)
{
    ParamDef d;
    d.id = id;
    d.group = group;
    d.label = label;
    d.unit = unit;
    d.min = lo;
    d.max = hi;
    d.decimals = decimals;
    d.tag = tag;
    d.help = help;
    d.kind = kind;
    return d;
}

bool isBoosted (const Project& p) { return p.boost.type != BoostType::None; }
bool isTurbo (const Project& p)
{
    return p.boost.type == BoostType::Turbo || p.boost.type == BoostType::TwinTurbo || p.boost.type == BoostType::Twincharged;
}
bool isSupercharged (const Project& p)
{
    return p.boost.type == BoostType::RootsSupercharger || p.boost.type == BoostType::TwinScrewSupercharger
           || p.boost.type == BoostType::CentrifugalSupercharger || p.boost.type == BoostType::Twincharged;
}

std::vector<ParamDef> build()
{
    std::vector<ParamDef> r;
    auto add = [&r] (ParamDef d) { r.push_back (std::move (d)); };

    // ---------------- engine geometry
    { auto d = make ("engine.bore", "engine.geometry", "Bore", "mm", 30, 160, 1, Tag::Mechanical, "Cylinder bore diameter."); ESD_FIELD (engine.boreMm) add (d); }
    { auto d = make ("engine.stroke", "engine.geometry", "Stroke", "mm", 30, 160, 1, Tag::Mechanical, "Piston stroke (2 x crank throw)."); ESD_FIELD (engine.strokeMm) add (d); }
    {
        auto d = make ("engine.displacement", "engine.geometry", "Displacement", "cc", 50, 10000, 0, Tag::Mechanical,
                       "Total swept volume. Editing it rescales bore and stroke keeping their ratio.");
        d.get = [] (const Project& p) { return p.engine.displacementCc(); };
        d.set = [] (Project& p, double v) { p.engine.setDisplacementCc (v); };
        add (d);
    }
    { auto d = make ("engine.compression", "engine.geometry", "Compression ratio", ":1", 6, 24, 1, Tag::Approximation,
                     "Raises cylinder pressure at exhaust opening: sharper, louder pulses (approximation)."); ESD_FIELD (engine.compressionRatio) add (d); }
    { auto d = make ("engine.bankAngle", "engine.geometry", "Bank angle", "deg", 0, 180, 1, Tag::Mechanical,
                     "Angle between banks. Changes the real firing intervals of V engines (geometry is recomputed).");
      ESD_FIELD (engine.bankAngleDeg)
      d.visible = [] (const Project& p) { auto* l = findLayout (p.engine.layoutId); return l && l->bankAngleEditable; };
      add (d); }

    // ---------------- operating range
    { auto d = make ("engine.idle", "engine.operation", "Idle speed", "rpm", 300, 3000, 0, Tag::Mechanical, "Target of the idle speed controller."); ESD_FIELD (engine.idleRpm) add (d); }
    { auto d = make ("engine.redline", "engine.operation", "Redline", "rpm", 1500, 22000, 0, Tag::Mechanical, "End of the torque curve range."); ESD_FIELD (engine.redlineRpm) add (d); }
    { auto d = make ("engine.limiter", "engine.operation", "Rev limiter", "rpm", 1500, 23000, 0, Tag::Mechanical, "Fuel/ignition is cut above this speed."); ESD_FIELD (engine.limiterRpm) add (d); }
    { auto d = make ("engine.limiterHyst", "engine.operation", "Limiter hysteresis", "rpm", 20, 1500, 0, Tag::Mechanical, "Cut is released this many rpm below the limit (bounce rate)."); ESD_FIELD (engine.limiterHysteresisRpm) add (d); }
    { auto d = make ("engine.limiterType", "engine.operation", "Limiter type", "", 0, 2, 0, Tag::Mechanical, "Ignition cut lets unburnt fuel ignite in the exhaust (pops).", K::Choice);
      d.choices = { "Fuel cut", "Ignition cut (pops)", "Soft cut (alternate cylinders)" }; ESD_FIELD (engine.limiterType) add (d); }
    { auto d = make ("engine.throttleResponse", "engine.operation", "Throttle response", "ms", 10, 600, 0, Tag::Approximation,
                     "Time constant of the intake manifold filling (load follows the throttle with this lag)."); ESD_FIELD (engine.throttleResponseMs) add (d); }
    { auto d = make ("engine.inertia", "engine.operation", "Flywheel inertia", "kg m2", 0, 2, 3, Tag::Mechanical,
                     "Rotating inertia of crank + flywheel. 0 = estimated from displacement."); ESD_FIELD (engine.flywheelInertia) add (d); }
    { auto d = make ("engine.friction", "engine.operation", "Friction scale", "x", 0.3, 3, 2, Tag::Approximation,
                     "Multiplier of the friction mean effective pressure model (FMEP = 0.97 + 0.15 N + 0.05 N^2 bar, N in krpm)."); ESD_FIELD (engine.frictionScale) add (d); }

    // ---------------- valvetrain
    { auto d = make ("engine.cam", "engine.valvetrain", "Valvetrain", "", 0, 4, 0, Tag::Approximation, "Changes the mechanical noise layer (pushrod clatter vs DOHC chain whine).", K::Choice);
      d.choices = { "OHV pushrod", "SOHC", "DOHC", "2-stroke ports", "Rotary ports" }; ESD_FIELD (engine.cam) add (d); }
    { auto d = make ("engine.valves", "engine.valvetrain", "Valves per cylinder", "", 2, 5, 0, Tag::Mechanical, "Number of valve events per cycle (mechanical ticks, intake flow)."); ESD_FIELD (engine.valvesPerCylinder) add (d); }
    { auto d = make ("engine.exhaustDuration", "engine.valvetrain", "Exhaust duration", "deg", 160, 320, 0, Tag::Approximation,
                     "Exhaust valve open duration: width of the exhaust pulse in crank degrees."); ESD_FIELD (engine.exhaustDurationDeg) add (d); }
    { auto d = make ("engine.overlap", "engine.valvetrain", "Valve overlap", "deg", 0, 110, 0, Tag::Approximation,
                     "Large overlap = unstable, lumpy idle (reversion). Strongest at low load."); ESD_FIELD (engine.camOverlapDeg) add (d); }

    // ---------------- combustion
    { auto d = make ("engine.fuel", "engine.combustion", "Fuel", "", 0, 2, 0, Tag::Approximation, "Diesel = compression ignition: harsher combustion knock.", K::Choice);
      d.choices = { "Gasoline", "Diesel", "Ethanol" }; ESD_FIELD (engine.fuel) add (d); }
    { auto d = make ("engine.harshness", "engine.combustion", "Combustion harshness", "", 0, 1, 2, Tag::Approximation,
                     "Pressure rise rate: sharpness of the pulses and level of the block knock."); ESD_FIELD (engine.combustionHarshness) add (d); }
    { auto d = make ("engine.variability", "engine.combustion", "Cycle variability", "", 0, 1, 2, Tag::Approximation,
                     "Cycle-to-cycle combustion variation (largest at idle and low load)."); ESD_FIELD (engine.combustionVariability) add (d); }
    { auto d = make ("engine.crackle", "engine.combustion", "Overrun crackle", "", 0, 1, 2, Tag::Acoustic,
                     "Probability of afterfire pops when lifting off at high rpm."); ESD_FIELD (engine.overrunCrackle) add (d); }

    // ---------------- intake
    { auto d = make ("intake.filter", "intake", "Air filter", "", 0, 3, 0, Tag::Approximation, "Stock airbox muffles intake roar; open cone/stacks let it through.", K::Choice);
      d.choices = { "Stock airbox", "Panel filter", "Open cone", "Velocity stacks" }; ESD_FIELD (intake.filter) add (d); }
    { auto d = make ("intake.manifold", "intake", "Manifold", "", 0, 2, 0, Tag::Approximation, "Individual throttles give a sharper, louder induction sound.", K::Choice);
      d.choices = { "Single plenum", "Dual plenum", "Individual throttle bodies" }; ESD_FIELD (intake.manifold) add (d); }
    { auto d = make ("intake.airbox", "intake", "Airbox volume", "L", 0.5, 40, 1, Tag::Approximation, "Helmholtz resonance of the airbox with its snorkel."); ESD_FIELD (intake.airboxVolumeL) add (d); }
    { auto d = make ("intake.snorkelLength", "intake", "Snorkel length", "mm", 30, 1000, 0, Tag::Approximation, "Neck length of the airbox Helmholtz resonator."); ESD_FIELD (intake.snorkelLengthMm) add (d); }
    { auto d = make ("intake.snorkelDiameter", "intake", "Snorkel diameter", "mm", 20, 150, 0, Tag::Approximation, "Neck diameter of the airbox Helmholtz resonator."); ESD_FIELD (intake.snorkelDiameterMm) add (d); }
    { auto d = make ("intake.runnerLength", "intake", "Runner length", "mm", 50, 900, 0, Tag::Approximation, "Quarter-wave tuning of the intake runners."); ESD_FIELD (intake.runnerLengthMm) add (d); }
    { auto d = make ("intake.runnerDiameter", "intake", "Runner diameter", "mm", 20, 90, 0, Tag::Approximation, "Larger runners: less loss, deeper sound."); ESD_FIELD (intake.runnerDiameterMm) add (d); }
    { auto d = make ("intake.plenum", "intake", "Plenum volume", "L", 0.5, 20, 1, Tag::Approximation, "Plenum smooths the individual pulses."); ESD_FIELD (intake.plenumVolumeL) add (d); }
    { auto d = make ("intake.throttleDiameter", "intake", "Throttle diameter", "mm", 20, 120, 0, Tag::Approximation, "Smaller throttle: more hiss at part throttle."); ESD_FIELD (intake.throttleDiameterMm) add (d); }
    { auto d = make ("intake.resonator", "intake", "Intake resonator", "", 0, 1, 0, Tag::Approximation, "Side-branch resonator cancelling one frequency.", K::Toggle); ESD_FIELD (intake.resonatorEnabled) add (d); }
    { auto d = make ("intake.resonatorFreq", "intake", "Resonator frequency", "Hz", 40, 1000, 0, Tag::Approximation, "Frequency cancelled by the intake resonator.");
      ESD_FIELD (intake.resonatorFreqHz) d.visible = [] (const Project& p) { return p.intake.resonatorEnabled; }; add (d); }

    // ---------------- exhaust
    { auto d = make ("exhaust.header", "exhaust.header", "Header type", "", 0, 3, 0, Tag::Approximation, "Cast log manifolds smear the pulses, long tubes keep them separated.", K::Choice);
      d.choices = { "Cast manifold", "Short tube headers", "Long tube headers", "Tri-Y" }; ESD_FIELD (exhaust.header) add (d); }
    { auto d = make ("exhaust.primaryLength", "exhaust.header", "Primary length", "mm", 100, 1500, 0, Tag::Approximation, "Length of each primary pipe (waveguide)."); ESD_FIELD (exhaust.primaryLengthMm) add (d); }
    { auto d = make ("exhaust.primaryDiameter", "exhaust.header", "Primary diameter", "mm", 20, 70, 0, Tag::Approximation, "Larger diameter = less damping, lower radiation cutoff."); ESD_FIELD (exhaust.primaryDiameterMm) add (d); }
    { auto d = make ("exhaust.unequal", "exhaust.header", "Unequal length", "", 0, 1, 2, Tag::Approximation,
                     "Length differences between primaries: pulses arrive unevenly at the collector (boxer rumble)."); ESD_FIELD (exhaust.unequalLength) add (d); }
    { auto d = make ("exhaust.routing", "exhaust.line", "Routing", "", 0, 3, 0, Tag::Approximation,
                     "Dual keeps the banks apart (V8 cross-plane burble). X/H pipes mix the banks.", K::Choice);
      d.choices = { "Single line", "Dual lines", "Dual with X-pipe", "Dual with H-pipe" }; ESD_FIELD (exhaust.routing) add (d); }
    { auto d = make ("exhaust.midpipeLength", "exhaust.line", "Mid pipe length", "mm", 200, 4000, 0, Tag::Approximation, "Main pipe resonances (round trip delay)."); ESD_FIELD (exhaust.midpipeLengthMm) add (d); }
    { auto d = make ("exhaust.pipeDiameter", "exhaust.line", "Pipe diameter", "mm", 30, 110, 0, Tag::Approximation, "Main pipe diameter."); ESD_FIELD (exhaust.pipeDiameterMm) add (d); }
    { auto d = make ("exhaust.catalyst", "exhaust.silencing", "Catalyst", "", 0, 2, 0, Tag::Approximation, "Catalyst damps high frequencies a little.", K::Choice);
      d.choices = { "None", "Standard", "High flow" }; ESD_FIELD (exhaust.catalyst) add (d); }
    { auto d = make ("exhaust.resonator", "exhaust.silencing", "Resonator", "", 0, 1, 0, Tag::Approximation, "Quarter-wave side branch: notches c/(4L) and its odd harmonics.", K::Toggle); ESD_FIELD (exhaust.resonatorEnabled) add (d); }
    { auto d = make ("exhaust.resonatorLength", "exhaust.silencing", "Resonator length", "mm", 50, 2000, 0, Tag::Approximation, "Side-branch length (sets the cancelled frequency).");
      ESD_FIELD (exhaust.resonatorLengthMm) d.visible = [] (const Project& p) { return p.exhaust.resonatorEnabled; }; add (d); }
    { auto d = make ("exhaust.muffler", "exhaust.silencing", "Muffler", "", 0, 3, 0, Tag::Approximation,
                     "Straight-through = absorptive, chambered = expansion chamber transmission loss, turbo = reversing chambers.", K::Choice);
      d.choices = { "None (straight pipe)", "Straight-through", "Chambered", "Turbo / reverse flow" }; ESD_FIELD (exhaust.muffler) add (d); }
    { auto d = make ("exhaust.mufflerVolume", "exhaust.silencing", "Muffler volume", "L", 1, 60, 1, Tag::Approximation, "Bigger muffler: more attenuation, lower chamber frequencies.");
      ESD_FIELD (exhaust.mufflerVolumeL) d.visible = [] (const Project& p) { return p.exhaust.muffler != MufflerType::None; }; add (d); }
    { auto d = make ("exhaust.tailpipeLength", "exhaust.outlet", "Tailpipe length", "mm", 50, 2000, 0, Tag::Approximation, "Last waveguide before radiation."); ESD_FIELD (exhaust.tailpipeLengthMm) add (d); }
    { auto d = make ("exhaust.tailpipeDiameter", "exhaust.outlet", "Tailpipe diameter", "mm", 30, 130, 0, Tag::Approximation, "Radiation: bigger outlets radiate low frequencies better."); ESD_FIELD (exhaust.tailpipeDiameterMm) add (d); }
    { auto d = make ("exhaust.outlets", "exhaust.outlet", "Outlets", "", 1, 4, 0, Tag::Approximation, "Number of tips."); ESD_FIELD (exhaust.outlets) add (d); }
    { auto d = make ("exhaust.outletPosition", "exhaust.outlet", "Outlet position", "", 0, 3, 0, Tag::Acoustic, "Stereo placement of the outlets.", K::Choice);
      d.choices = { "Center", "Left", "Right", "Split left/right" }; ESD_FIELD (exhaust.outletPosition) add (d); }

    // ---------------- boost
    { auto d = make ("boost.type", "boost", "Induction", "", 0, 6, 0, Tag::Mechanical, "Forced induction type.", K::Choice);
      d.choices = { "Naturally aspirated", "Turbo", "Twin turbo", "Roots supercharger", "Twin-screw supercharger", "Centrifugal supercharger", "Twincharged" };
      ESD_FIELD (boost.type) add (d); }
    { auto d = make ("boost.maxBoost", "boost", "Max boost", "bar", 0.1, 3.5, 2, Tag::Mechanical, "Relative manifold pressure at full boost."); ESD_FIELD (boost.maxBoostBar) d.visible = isBoosted; add (d); }
    { auto d = make ("boost.size", "boost", "Turbo size", "", 0, 1, 2, Tag::Approximation, "Small = quick spool, big = lag and more damping of exhaust pulses."); ESD_FIELD (boost.turboSize) d.visible = isTurbo; add (d); }
    { auto d = make ("boost.spoolRpm", "boost", "Full boost at", "rpm", 1000, 12000, 0, Tag::Approximation, "Engine speed where the turbo reaches full boost at wide open throttle."); ESD_FIELD (boost.spoolRpm) d.visible = isTurbo; add (d); }
    { auto d = make ("boost.response", "boost", "Shaft response", "ms", 50, 3000, 0, Tag::Approximation, "Time constant of the turbo shaft (inertia)."); ESD_FIELD (boost.responseMs) d.visible = isTurbo; add (d); }
    { auto d = make ("boost.externalWg", "boost", "External wastegate", "", 0, 1, 0, Tag::Acoustic, "Open screamer pipe: wastegate hiss/flutter when boost is capped.", K::Toggle); ESD_FIELD (boost.externalWastegate) d.visible = isTurbo; add (d); }
    { auto d = make ("boost.blowOff", "boost", "Blow-off valve", "", 0, 2, 0, Tag::Acoustic, "None = compressor surge flutter on lift-off.", K::Choice);
      d.choices = { "None (flutter)", "Recirculating", "Atmospheric" }; ESD_FIELD (boost.blowOff) d.visible = isTurbo; add (d); }
    { auto d = make ("boost.whistleMin", "boost.acoustic", "Whistle min", "Hz", 200, 8000, 0, Tag::Acoustic,
                     "Audible whistle frequency at low shaft speed. The real blade-pass frequency is ultrasonic: this is a sound-design mapping.");
      ESD_FIELD (boost.whistleMinHz) d.visible = isTurbo; add (d); }
    { auto d = make ("boost.whistleMax", "boost.acoustic", "Whistle max", "Hz", 500, 12000, 0, Tag::Acoustic, "Audible whistle frequency at full shaft speed."); ESD_FIELD (boost.whistleMaxHz) d.visible = isTurbo; add (d); }
    { auto d = make ("boost.scRatio", "boost", "Supercharger drive ratio", "x", 0.5, 5, 2, Tag::Mechanical, "Supercharger speed / crank speed (sets the whine pitch)."); ESD_FIELD (boost.superchargerDriveRatio) d.visible = isSupercharged; add (d); }
    { auto d = make ("boost.scLobes", "boost", "Rotor lobes", "", 2, 6, 0, Tag::Mechanical, "Lobes per rotor of a Roots / twin-screw blower."); ESD_FIELD (boost.superchargerLobes) d.visible = isSupercharged; add (d); }

    // ---------------- electric machine (phase 4)
    { auto d = make ("electric.type", "electric.machine", "Machine type", "", 0, 4, 0, Tag::Mechanical, "Each type has its own sound model (harmonics, inverter, slip...).", K::Choice);
      d.choices = { "PMSM (permanent magnet synchronous)", "BLDC (trapezoidal)", "Induction (asynchronous)", "Axial flux", "Outer rotor (hub)" }; ESD_FIELD (electric.type) add (d); }
    { auto d = make ("electric.poles", "electric.machine", "Poles", "", 2, 60, 0, Tag::Mechanical, "Magnetic poles: electrical frequency = rpm / 60 x poles / 2."); ESD_FIELD (electric.poles) add (d); }
    { auto d = make ("electric.slots", "electric.machine", "Stator slots", "", 6, 120, 0, Tag::Mechanical, "Slot passing order = slots (whine)."); ESD_FIELD (electric.slots) add (d); }
    { auto d = make ("electric.phases", "electric.machine", "Phases", "", 3, 9, 0, Tag::Mechanical, "Torque ripple order = 2 x phases (x pole pairs) per revolution."); ESD_FIELD (electric.phases) add (d); }
    { auto d = make ("electric.rotor", "electric.machine", "Rotor", "", 0, 3, 0, Tag::Approximation, "Interior magnets add reluctance harmonics; cages add rotor bar passing.", K::Choice);
      d.choices = { "Surface magnets", "Interior magnets", "Squirrel cage", "Wound rotor" }; ESD_FIELD (electric.rotor) add (d); }
    { auto d = make ("electric.stator", "electric.machine", "Stator winding", "", 0, 2, 0, Tag::Approximation, "Concentrated windings: stronger space harmonics (louder, rougher whine).", K::Choice);
      d.choices = { "Distributed", "Concentrated", "Hairpin" }; ESD_FIELD (electric.stator) add (d); }
    { auto d = make ("electric.rotorBars", "electric.machine", "Rotor bars", "", 10, 120, 0, Tag::Mechanical, "Induction machines: rotor slot harmonics.");
      ESD_FIELD (electric.rotorBars) d.visible = [] (const Project& p) { return p.electric.type == ElectricMotorType::Induction; }; add (d); }
    { auto d = make ("electric.maxRpm", "electric.ratings", "Max speed", "rpm", 1000, 30000, 0, Tag::Mechanical, "Torque falls to zero at this speed."); ESD_FIELD (electric.maxRpm) add (d); }
    { auto d = make ("electric.torque", "electric.ratings", "Peak torque", "Nm", 5, 2000, 0, Tag::Mechanical, "Constant-torque region below the base speed."); ESD_FIELD (electric.peakTorqueNm) add (d); }
    { auto d = make ("electric.power", "electric.ratings", "Peak power", "kW", 1, 1000, 0, Tag::Mechanical, "Constant-power (field weakening) region above the base speed."); ESD_FIELD (electric.peakPowerKw) add (d); }
    { auto d = make ("electric.voltage", "electric.ratings", "DC voltage", "V", 12, 1000, 0, Tag::Mechanical, "Battery / DC link voltage."); ESD_FIELD (electric.voltage) add (d); }
    { auto d = make ("electric.current", "electric.ratings", "Max current", "A", 10, 2000, 0, Tag::Mechanical, "Phase current limit (sets the inverter noise level)."); ESD_FIELD (electric.maxCurrentA) add (d); }
    { auto d = make ("electric.slip", "electric.ratings", "Rated slip", "", 0.005, 0.1, 3, Tag::Mechanical, "Induction: rotor lags the field by this fraction at full torque.");
      ESD_FIELD (electric.ratedSlip) d.visible = [] (const Project& p) { return p.electric.type == ElectricMotorType::Induction; }; add (d); }
    { auto d = make ("electric.inertia", "electric.ratings", "Rotor inertia", "kg m2", 0.001, 1, 3, Tag::Mechanical, "Rotating inertia of the rotor."); ESD_FIELD (electric.rotorInertia) add (d); }
    { auto d = make ("electric.resistance", "electric.ratings", "Phase resistance", "ohm", 0.001, 1, 3, Tag::Approximation, "Copper losses I^2 R heat the machine (temperature input)."); ESD_FIELD (electric.resistanceOhm) add (d); }
    { auto d = make ("electric.controller", "electric.drive", "Controller", "", 0, 2, 0, Tag::Mechanical, "FOC = sinusoidal currents; six-step = trapezoidal (6k harmonics, commutation ticks).", K::Choice);
      d.choices = { "Field oriented (sine)", "Six-step (trapezoidal)", "Scalar V/f" }; ESD_FIELD (electric.controller) add (d); }
    { auto d = make ("electric.pwm", "electric.drive", "PWM frequency", "Hz", 1000, 40000, 0, Tag::Mechanical, "Inverter switching frequency: carrier and sidebands fpwm +- 2 fe."); ESD_FIELD (electric.pwmHz) add (d); }
    { auto d = make ("electric.randomPwm", "electric.drive", "Random PWM", "", 0, 1, 0, Tag::Mechanical, "Spread-spectrum PWM: the carrier tone becomes a noise band.", K::Toggle); ESD_FIELD (electric.randomPwm) add (d); }
    { auto d = make ("electric.semi", "electric.drive", "Semiconductors", "", 0, 2, 0, Tag::Approximation, "Faster switching (SiC, GaN): brighter inverter noise.", K::Choice);
      d.choices = { "Si IGBT", "SiC MOSFET", "GaN" }; ESD_FIELD (electric.semiconductor) add (d); }
    { auto d = make ("electric.regen", "electric.drive", "Lift-off regen", "", 0, 1, 2, Tag::Mechanical, "Braking torque fraction applied when the pedal is released."); ESD_FIELD (electric.regenStrength) add (d); }
    { auto d = make ("electric.cooling", "electric.acoustic", "Cooling", "", 0, 1, 0, Tag::Approximation, "Air: fan noise growing with temperature; liquid: pump hum.", K::Choice);
      d.choices = { "Air (fan)", "Liquid (pump)" }; ESD_FIELD (electric.cooling) add (d); }
    { auto d = make ("electric.balls", "electric.acoustic", "Bearing balls", "", 5, 20, 0, Tag::Mechanical, "Bearing pass frequencies = f(ball count) x rotation."); ESD_FIELD (electric.bearingBalls) add (d); }
    { auto d = make ("electric.res1", "electric.acoustic", "Housing mode 1", "Hz", 200, 8000, 0, Tag::Approximation, "Structural resonance excited by the magnetic forces."); ESD_FIELD (electric.resonance1Hz) add (d); }
    { auto d = make ("electric.res2", "electric.acoustic", "Housing mode 2", "Hz", 200, 12000, 0, Tag::Approximation, "Second structural resonance."); ESD_FIELD (electric.resonance2Hz) add (d); }

    // ---------------- hybrid (phase 6)
    { auto d = make ("hybrid.arch", "hybrid", "Architecture", "", 0, 5, 0, Tag::Mechanical, "How engine, motor and wheels are connected.", K::Choice);
      d.choices = { "Mild (belt starter-generator)", "Full", "Plug-in", "Series (engine = generator)", "Parallel", "Series-parallel (power split)" }; ESD_FIELD (hybrid.arch) add (d); }
    { auto d = make ("hybrid.battery", "hybrid", "Battery", "kWh", 0.2, 120, 1, Tag::Mechanical, "Usable energy."); ESD_FIELD (hybrid.batteryKWh) add (d); }
    { auto d = make ("hybrid.socInit", "hybrid", "Initial charge", "", 0, 1, 2, Tag::Mechanical, "State of charge at start."); ESD_FIELD (hybrid.socInitial) add (d); }
    { auto d = make ("hybrid.socMin", "hybrid", "Min charge", "", 0, 1, 2, Tag::Mechanical, "Below this the engine runs and recharges."); ESD_FIELD (hybrid.socMin) add (d); }
    { auto d = make ("hybrid.socMax", "hybrid", "Max charge", "", 0, 1, 2, Tag::Mechanical, "Regeneration stops above this."); ESD_FIELD (hybrid.socMax) add (d); }
    { auto d = make ("hybrid.batteryKw", "hybrid", "Battery power", "kW", 1, 500, 0, Tag::Mechanical, "Max charge / discharge power."); ESD_FIELD (hybrid.maxBatteryKw) add (d); }
    { auto d = make ("hybrid.evSpeed", "hybrid", "EV-only up to", "km/h", 0, 200, 0, Tag::Mechanical, "Engine starts above this speed."); ESD_FIELD (hybrid.evMaxSpeedKmh) add (d); }
    { auto d = make ("hybrid.evPower", "hybrid", "EV-only up to", "kW", 0, 300, 0, Tag::Mechanical, "Engine starts above this power demand."); ESD_FIELD (hybrid.evMaxPowerKw) add (d); }
    { auto d = make ("hybrid.minOn", "hybrid", "Engine min on time", "s", 0, 30, 1, Tag::Mechanical, "Avoids rapid start/stop cycling."); ESD_FIELD (hybrid.engineMinOnSeconds) add (d); }
    { auto d = make ("hybrid.chargeKw", "hybrid", "Charge power", "kW", 0, 100, 0, Tag::Mechanical, "Extra engine load used to recharge the battery."); ESD_FIELD (hybrid.chargePowerKw) add (d); }
    { auto d = make ("hybrid.regenKw", "hybrid", "Max regeneration", "kW", 0, 300, 0, Tag::Mechanical, "Braking power recovered by the motor."); ESD_FIELD (hybrid.regenMaxKw) add (d); }
    { auto d = make ("hybrid.assist", "hybrid", "Motor assist", "", 0, 1, 2, Tag::Mechanical, "Parallel / mild: fraction of motor torque added under load."); ESD_FIELD (hybrid.motorAssist) add (d); }

    // ---------------- transmission (phase 5)
    { auto d = make ("transmission.type", "transmission", "Gearbox", "", 0, 6, 0, Tag::Mechanical, "Transmission type (independent of the engine).", K::Choice);
      d.choices = { "Manual", "Automatic (torque converter)", "Dual clutch (DCT)", "CVT", "Automated manual (AMT)", "EV fixed reduction", "EV multi-speed" };
      ESD_FIELD (transmission.type) add (d); }
    {
        auto d = make ("transmission.gears", "transmission", "Number of gears", "", 1, 10, 0, Tag::Mechanical, "Adding a gear extends the ratio ladder geometrically.");
        d.get = [] (const Project& p) { return static_cast<double> (p.transmission.ratios.size()); };
        d.set = [] (Project& p, double v)
        {
            auto& r = p.transmission.ratios;
            const auto n = static_cast<std::size_t> (std::clamp (static_cast<int> (std::lround (v)), 1, 10));
            while (r.size() < n) r.push_back (r.empty() ? 3.5 : (r.size() >= 2 ? r.back() * (r.back() / r[r.size() - 2]) : r.back() * 0.7));
            r.resize (n);
        };
        d.visible = [] (const Project& p) { return p.transmission.type != TransmissionType::CVT && p.transmission.type != TransmissionType::EVFixed; };
        add (d);
    }
    for (int g = 0; g < 10; ++g)
    {
        auto d = make ("", "transmission", "", "", 0.2, 8.0, 3, Tag::Mechanical, "Gear ratio (input / output).");
        d.id = "transmission.gear" + std::to_string (g + 1);
        d.label = "Gear " + std::to_string (g + 1);
        d.get = [g] (const Project& p) { return g < static_cast<int> (p.transmission.ratios.size()) ? p.transmission.ratios[static_cast<std::size_t> (g)] : 1.0; };
        d.set = [g] (Project& p, double v) { if (g < static_cast<int> (p.transmission.ratios.size())) p.transmission.ratios[static_cast<std::size_t> (g)] = v; };
        d.visible = [g] (const Project& p)
        {
            return g < static_cast<int> (p.transmission.ratios.size()) && p.transmission.type != TransmissionType::CVT && p.transmission.type != TransmissionType::EVFixed;
        };
        add (d);
    }
    { auto d = make ("transmission.final", "transmission", "Final drive", "", 1, 15, 2, Tag::Mechanical, "Differential (or EV reduction) ratio."); ESD_FIELD (transmission.finalDrive) add (d); }
    { auto d = make ("transmission.eff", "transmission", "Efficiency", "", 0.6, 1, 2, Tag::Approximation, "Mechanical efficiency of the driveline."); ESD_FIELD (transmission.efficiency) add (d); }
    { auto d = make ("transmission.shiftTime", "transmission", "Shift time", "ms", 20, 2000, 0, Tag::Mechanical, "Clutch open / torque interruption time."); ESD_FIELD (transmission.shiftTimeMs) add (d); }
    { auto d = make ("transmission.clutch", "transmission", "Clutch capacity", "Nm", 0, 3000, 0, Tag::Mechanical, "0 = 1.6 x peak torque."); ESD_FIELD (transmission.clutchCapacityNm) add (d); }
    { auto d = make ("transmission.stall", "transmission", "Converter stall speed", "rpm", 1200, 4000, 0, Tag::Mechanical, "Torque converter K-factor: engine speed at full throttle with the output held.");
      ESD_FIELD (transmission.converterStallRpm) d.visible = [] (const Project& p) { return p.transmission.type == TransmissionType::TorqueConverter; }; add (d); }
    { auto d = make ("transmission.lockup", "transmission", "Lock-up from gear", "", 1, 10, 0, Tag::Mechanical, "Converter lock-up clutch engages from this gear.");
      ESD_FIELD (transmission.lockupFromGear) d.visible = [] (const Project& p) { return p.transmission.type == TransmissionType::TorqueConverter; }; add (d); }
    { auto d = make ("transmission.upLight", "transmission.logic", "Upshift (light)", "rpm", 1000, 15000, 0, Tag::Mechanical, "Automatic upshift speed at light throttle."); ESD_FIELD (transmission.upshiftRpmLight) add (d); }
    { auto d = make ("transmission.upFull", "transmission.logic", "Upshift (full)", "rpm", 1000, 20000, 0, Tag::Mechanical, "Automatic upshift speed at full throttle."); ESD_FIELD (transmission.upshiftRpmFull) add (d); }
    { auto d = make ("transmission.down", "transmission.logic", "Downshift", "rpm", 500, 8000, 0, Tag::Mechanical, "Automatic downshift speed."); ESD_FIELD (transmission.downshiftRpm) add (d); }
    { auto d = make ("transmission.kickdown", "transmission.logic", "Kickdown pedal", "", 0.5, 1, 2, Tag::Mechanical, "Pedal position that forces a downshift."); ESD_FIELD (transmission.kickdownThrottle) add (d); }
    { auto d = make ("transmission.cvtMin", "transmission.logic", "CVT min ratio", "", 0.2, 2, 2, Tag::Mechanical, "Overdrive end of the variator.");
      ESD_FIELD (transmission.cvtRatioMin) d.visible = [] (const Project& p) { return p.transmission.type == TransmissionType::CVT; }; add (d); }
    { auto d = make ("transmission.cvtMax", "transmission.logic", "CVT max ratio", "", 1, 6, 2, Tag::Mechanical, "Launch end of the variator.");
      ESD_FIELD (transmission.cvtRatioMax) d.visible = [] (const Project& p) { return p.transmission.type == TransmissionType::CVT; }; add (d); }
    { auto d = make ("transmission.cvtLight", "transmission.logic", "CVT rpm (light)", "rpm", 800, 8000, 0, Tag::Mechanical, "Engine speed held at light throttle.");
      ESD_FIELD (transmission.cvtRpmLight) d.visible = [] (const Project& p) { return p.transmission.type == TransmissionType::CVT; }; add (d); }
    { auto d = make ("transmission.cvtFull", "transmission.logic", "CVT rpm (full)", "rpm", 1000, 15000, 0, Tag::Mechanical, "Engine speed held at full throttle (the CVT drone).");
      ESD_FIELD (transmission.cvtRpmFull) d.visible = [] (const Project& p) { return p.transmission.type == TransmissionType::CVT; }; add (d); }
    { auto d = make ("transmission.fakeShifts", "transmission.logic", "Artificial shifts", "", 0, 1, 0, Tag::Acoustic, "EV: simulate gear changes (sound + torque dip) on a fixed reduction.", K::Toggle);
      ESD_FIELD (transmission.artificialShifts) d.visible = [] (const Project& p) { return p.transmission.type == TransmissionType::EVFixed; }; add (d); }
    { auto d = make ("transmission.teeth", "transmission.logic", "Input gear teeth", "", 10, 60, 0, Tag::Mechanical, "Gear mesh order of the whine."); ESD_FIELD (transmission.pinionTeeth) add (d); }

    // ---------------- vehicle
    { auto d = make ("vehicle.mass", "vehicle", "Mass", "kg", 50, 40000, 0, Tag::Mechanical, "Vehicle mass with driver."); ESD_FIELD (vehicle.massKg) add (d); }
    { auto d = make ("vehicle.crr", "vehicle", "Rolling resistance", "", 0.005, 0.05, 3, Tag::Mechanical, "Rolling resistance coefficient."); ESD_FIELD (vehicle.rollingResistance) add (d); }
    { auto d = make ("vehicle.cda", "vehicle", "Drag area Cd x A", "m2", 0.1, 10, 2, Tag::Mechanical, "Aerodynamic drag area."); ESD_FIELD (vehicle.dragArea) add (d); }
    { auto d = make ("vehicle.wheel", "vehicle", "Wheel radius", "m", 0.15, 0.8, 3, Tag::Mechanical, "Rolling radius."); ESD_FIELD (vehicle.wheelRadiusM) add (d); }
    { auto d = make ("vehicle.brake", "vehicle", "Brake force", "N", 500, 60000, 0, Tag::Mechanical, "Force at full brake pedal."); ESD_FIELD (vehicle.brakeForceN) add (d); }
    { auto d = make ("vehicle.grade", "vehicle", "Road grade", "%", -25, 25, 1, Tag::Mechanical, "Uphill positive, downhill negative."); ESD_FIELD (vehicle.gradePercent) add (d); }

    // ---------------- loop generator (phase 7)
    { auto d = make ("loops.rpmMin", "loops", "Rpm min", "rpm", 0, 20000, 0, Tag::Acoustic, "0 = idle (thermal) / 300 rpm (electric)."); ESD_FIELD (loops.rpmMin) add (d); }
    { auto d = make ("loops.rpmMax", "loops", "Rpm max", "rpm", 0, 30000, 0, Tag::Acoustic, "0 = limiter (thermal) / max speed (electric)."); ESD_FIELD (loops.rpmMax) add (d); }
    { auto d = make ("loops.rpmPoints", "loops", "Rpm points", "", 2, 64, 0, Tag::Acoustic, "Exported rpm resolution (the runtime interpolates between them)."); ESD_FIELD (loops.rpmPoints) add (d); }
    { auto d = make ("loops.loadPoints", "loops", "Load points", "", 1, 9, 0, Tag::Acoustic, "Exported load (or torque) resolution; 1 = full load only."); ESD_FIELD (loops.loadPoints) add (d); }
    { auto d = make ("loops.overrun", "loops", "Deceleration loops", "", 0, 1, 0, Tag::Acoustic, "Thermal: extra fuel-cut (overrun) row.", K::Toggle); ESD_FIELD (loops.overrunState) add (d); }
    { auto d = make ("loops.regen", "loops", "Regeneration loops", "", 0, 1, 0, Tag::Acoustic, "Electric / hybrid: negative torque rows.", K::Toggle); ESD_FIELD (loops.regenState) add (d); }
    { auto d = make ("loops.seconds", "loops", "Loop length", "s", 0.25, 20, 2, Tag::Acoustic, "Target length; rounded to whole engine cycles."); ESD_FIELD (loops.loopSeconds) add (d); }
    { auto d = make ("loops.xfade", "loops", "Loop crossfade", "ms", 0, 500, 0, Tag::Acoustic, "Seam crossfade for the non-periodic parts (noise, cycle variation)."); ESD_FIELD (loops.crossfadeMs) add (d); }
    { auto d = make ("loops.sr", "loops.format", "Sample rate", "Hz", 44100, 192000, 0, Tag::Acoustic, "Output sample rate (48 kHz recommended)."); ESD_FIELD (loops.sampleRate) add (d); }
    { auto d = make ("loops.bits", "loops.format", "Bit depth", "", 16, 32, 0, Tag::Acoustic, "16, 24 or 32 (float)."); ESD_FIELD (loops.bitDepth) add (d); }
    { auto d = make ("loops.flac", "loops.format", "FLAC instead of WAV", "", 0, 1, 0, Tag::Acoustic, "Lossless compression (needs the application, not the CLI).", K::Toggle); ESD_FIELD (loops.flac) add (d); }
    { auto d = make ("loops.stems", "loops.format", "Layer stems", "", 0, 1, 0, Tag::Acoustic, "Also export each layer of each loop (multi-layer runtime).", K::Toggle); ESD_FIELD (loops.stems) add (d); }
    { auto d = make ("loops.normalize", "loops.format", "Normalize set", "", 0, 1, 0, Tag::Acoustic, "One common gain for the whole set (keeps relative levels).", K::Toggle); ESD_FIELD (loops.normalize) add (d); }
    { auto d = make ("loops.startStop", "loops.format", "Start / stop one-shots", "", 0, 1, 0, Tag::Acoustic, "Render start-up and shut-down sounds.", K::Toggle); ESD_FIELD (loops.startStop) add (d); }
    { auto d = make ("loops.threads", "loops.format", "Threads", "", 0, 64, 0, Tag::Acoustic, "0 = all cores."); ESD_FIELD (loops.threads) add (d); }

    // ---------------- per-layer effects (phase 3)
    for (int i = 0; i < kNumLayers; ++i)
    {
        const std::string ln = layerName (static_cast<LayerId> (i));
        const std::string grp = "fx." + ln;
        const auto idx = static_cast<std::size_t> (i);
        auto fxParam = [&] (const char* key, const char* label, const char* unit, double lo, double hi, int dec, const char* help, float LayerFx::* member)
        {
            auto d = make ("", "", label, unit, lo, hi, dec, Tag::Acoustic, help);
            d.id = grp + "." + key;
            d.group = grp;
            d.get = [idx, member] (const Project& p) { return static_cast<double> (p.mix.fx[idx].*member); };
            d.set = [idx, member] (Project& p, double v) { p.mix.fx[idx].*member = static_cast<float> (v); };
            add (d);
        };
        fxParam ("hp", "High-pass", "Hz", 0, 2000, 0, "0 = off.", &LayerFx::highpassHz);
        fxParam ("low", "Low shelf (200 Hz)", "dB", -24, 24, 1, "Three-band EQ.", &LayerFx::lowDb);
        fxParam ("mid", "Mid peak", "dB", -24, 24, 1, "Three-band EQ.", &LayerFx::midDb);
        fxParam ("midHz", "Mid frequency", "Hz", 100, 10000, 0, "Centre of the mid band.", &LayerFx::midHz);
        fxParam ("high", "High shelf (4 kHz)", "dB", -24, 24, 1, "Three-band EQ.", &LayerFx::highDb);
        fxParam ("lp", "Low-pass", "Hz", 0, 20000, 0, "0 = off.", &LayerFx::lowpassHz);
        fxParam ("drive", "Saturation", "", 0, 1, 2, "tanh waveshaper with gain compensation.", &LayerFx::drive);
        fxParam ("comp", "Compressor threshold", "dB", -60, 0, 1, "0 = off.", &LayerFx::compThresholdDb);
        fxParam ("ratio", "Compressor ratio", ":1", 1, 20, 1, "Above the threshold.", &LayerFx::compRatio);
        fxParam ("conv", "Convolution mix", "", 0, 1, 2, "Impulse response mix (load an IR file in the Sound Lab).", &LayerFx::convMix);
        fxParam ("reverb", "Reverb send", "", 0, 1, 2, "Send to the shared reverb.", &LayerFx::reverbSend);
    }
    { auto d = make ("mix.reverbSize", "mix.reverb", "Reverb size", "", 0, 1, 2, Tag::Acoustic, "Room size of the shared reverb."); ESD_FIELD (mix.reverbSize) add (d); }
    { auto d = make ("mix.reverbDamp", "mix.reverb", "Reverb damping", "", 0, 1, 2, Tag::Acoustic, "High frequency damping."); ESD_FIELD (mix.reverbDamping) add (d); }
    { auto d = make ("mix.reverbReturn", "mix.reverb", "Reverb return", "dB", -60, 6, 1, Tag::Acoustic, "Reverb level in the mix."); ESD_FIELD (mix.reverbReturnDb) add (d); }

    // ---------------- mix
    for (int i = 0; i < kNumLayers; ++i)
    {
        const std::string base = std::string ("mix.") + layerName (static_cast<LayerId> (i));
        ParamDef g = make ("", "mix", "", "dB", -60, 12, 1, Tag::Acoustic, "Layer gain.");
        g.id = base + ".gain";
        g.label = std::string (layerName (static_cast<LayerId> (i))) + " gain";
        g.get = [i] (const Project& p) { return static_cast<double> (p.mix.layers[static_cast<std::size_t> (i)].gainDb); };
        g.set = [i] (Project& p, double v) { p.mix.layers[static_cast<std::size_t> (i)].gainDb = static_cast<float> (v); };
        add (g);
        ParamDef pan = make ("", "mix", "", "", -1, 1, 2, Tag::Acoustic, "Layer pan offset.");
        pan.id = base + ".pan";
        pan.label = std::string (layerName (static_cast<LayerId> (i))) + " pan";
        pan.get = [i] (const Project& p) { return static_cast<double> (p.mix.layers[static_cast<std::size_t> (i)].pan); };
        pan.set = [i] (Project& p, double v) { p.mix.layers[static_cast<std::size_t> (i)].pan = static_cast<float> (v); };
        add (pan);
    }
    { auto d = make ("mix.master", "mix", "Master gain", "dB", -48, 12, 1, Tag::Acoustic, "Output gain before the safety limiter."); ESD_FIELD (mix.masterGainDb) add (d); }
    { auto d = make ("mix.width", "mix", "Stereo width", "", 0, 2, 2, Tag::Acoustic, "0 = mono."); ESD_FIELD (mix.stereoWidth) add (d); }
    return r;
}
#undef ESD_FIELD
} // namespace

const std::vector<ParamDef>& paramRegistry()
{
    static const std::vector<ParamDef> reg = build();
    return reg;
}

const ParamDef* findParam (const std::string& id)
{
    for (auto& p : paramRegistry())
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<const ParamDef*> paramsInGroup (const std::string& prefix, bool includeSubgroups)
{
    std::vector<const ParamDef*> out;
    for (auto& p : paramRegistry())
        if (p.group == prefix || (includeSubgroups && p.group.rfind (prefix + ".", 0) == 0))
            out.push_back (&p);
    return out;
}
} // namespace esd
