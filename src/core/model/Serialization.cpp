#include "Serialization.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace esd
{
NLOHMANN_JSON_SERIALIZE_ENUM (CurveInterp, { { CurveInterp::Linear, "linear" }, { CurveInterp::Smooth, "smooth" }, { CurveInterp::Step, "step" } })
NLOHMANN_JSON_SERIALIZE_ENUM (CamType, { { CamType::OHV, "ohv" }, { CamType::SOHC, "sohc" }, { CamType::DOHC, "dohc" }, { CamType::PortTwoStroke, "port_2t" }, { CamType::PortRotary, "port_rotary" } })
NLOHMANN_JSON_SERIALIZE_ENUM (LimiterType, { { LimiterType::FuelCut, "fuel_cut" }, { LimiterType::IgnitionCut, "ignition_cut" }, { LimiterType::SoftCut, "soft_cut" } })
NLOHMANN_JSON_SERIALIZE_ENUM (FuelType, { { FuelType::Gasoline, "gasoline" }, { FuelType::Diesel, "diesel" }, { FuelType::Ethanol, "ethanol" } })
NLOHMANN_JSON_SERIALIZE_ENUM (AirFilterType, { { AirFilterType::StockAirbox, "stock_airbox" }, { AirFilterType::PanelFilter, "panel" }, { AirFilterType::OpenCone, "open_cone" }, { AirFilterType::VelocityStacks, "velocity_stacks" } })
NLOHMANN_JSON_SERIALIZE_ENUM (IntakeManifoldType, { { IntakeManifoldType::SinglePlenum, "single_plenum" }, { IntakeManifoldType::DualPlenum, "dual_plenum" }, { IntakeManifoldType::IndividualThrottles, "itb" } })
NLOHMANN_JSON_SERIALIZE_ENUM (HeaderType, { { HeaderType::CastManifold, "cast_manifold" }, { HeaderType::ShortTube, "short_tube" }, { HeaderType::LongTube, "long_tube" }, { HeaderType::TriY, "tri_y" } })
NLOHMANN_JSON_SERIALIZE_ENUM (ExhaustRouting, { { ExhaustRouting::Single, "single" }, { ExhaustRouting::Dual, "dual" }, { ExhaustRouting::DualXPipe, "dual_x" }, { ExhaustRouting::DualHPipe, "dual_h" } })
NLOHMANN_JSON_SERIALIZE_ENUM (CatalystType, { { CatalystType::None, "none" }, { CatalystType::Standard, "standard" }, { CatalystType::HighFlow, "high_flow" } })
NLOHMANN_JSON_SERIALIZE_ENUM (MufflerType, { { MufflerType::None, "none" }, { MufflerType::StraightThrough, "straight_through" }, { MufflerType::Chambered, "chambered" }, { MufflerType::Turbo, "turbo" } })
NLOHMANN_JSON_SERIALIZE_ENUM (OutletPosition, { { OutletPosition::Center, "center" }, { OutletPosition::Left, "left" }, { OutletPosition::Right, "right" }, { OutletPosition::Split, "split" } })
NLOHMANN_JSON_SERIALIZE_ENUM (BoostType, { { BoostType::None, "none" }, { BoostType::Turbo, "turbo" }, { BoostType::TwinTurbo, "twin_turbo" }, { BoostType::RootsSupercharger, "roots" }, { BoostType::TwinScrewSupercharger, "twin_screw" }, { BoostType::CentrifugalSupercharger, "centrifugal" }, { BoostType::Twincharged, "twincharged" } })
NLOHMANN_JSON_SERIALIZE_ENUM (BlowOffType, { { BlowOffType::None, "none" }, { BlowOffType::Recirculating, "recirculating" }, { BlowOffType::Atmospheric, "atmospheric" } })
NLOHMANN_JSON_SERIALIZE_ENUM (PowertrainKind, { { PowertrainKind::Thermal, "thermal" }, { PowertrainKind::Electric, "electric" }, { PowertrainKind::Hybrid, "hybrid" } })

NLOHMANN_JSON_SERIALIZE_ENUM (ElectricMotorType, { { ElectricMotorType::PMSM, "pmsm" }, { ElectricMotorType::BLDC, "bldc" }, { ElectricMotorType::Induction, "induction" }, { ElectricMotorType::AxialFlux, "axial_flux" }, { ElectricMotorType::OuterRotor, "outer_rotor" } })
NLOHMANN_JSON_SERIALIZE_ENUM (MotorController, { { MotorController::FOC, "foc" }, { MotorController::SixStep, "six_step" }, { MotorController::ScalarVf, "scalar_vf" } })
NLOHMANN_JSON_SERIALIZE_ENUM (SemiconductorType, { { SemiconductorType::SiIGBT, "si_igbt" }, { SemiconductorType::SiCMOSFET, "sic_mosfet" }, { SemiconductorType::GaN, "gan" } })
NLOHMANN_JSON_SERIALIZE_ENUM (CoolingType, { { CoolingType::Air, "air" }, { CoolingType::Liquid, "liquid" } })
NLOHMANN_JSON_SERIALIZE_ENUM (RotorType, { { RotorType::SurfacePM, "surface_pm" }, { RotorType::InteriorPM, "interior_pm" }, { RotorType::SquirrelCage, "squirrel_cage" }, { RotorType::WoundRotor, "wound" } })
NLOHMANN_JSON_SERIALIZE_ENUM (StatorWinding, { { StatorWinding::Distributed, "distributed" }, { StatorWinding::Concentrated, "concentrated" }, { StatorWinding::Hairpin, "hairpin" } })
NLOHMANN_JSON_SERIALIZE_ENUM (TransmissionType, { { TransmissionType::Manual, "manual" }, { TransmissionType::TorqueConverter, "torque_converter" }, { TransmissionType::DCT, "dct" }, { TransmissionType::CVT, "cvt" }, { TransmissionType::AMT, "amt" }, { TransmissionType::EVFixed, "ev_fixed" }, { TransmissionType::EVMultiSpeed, "ev_multi" } })
NLOHMANN_JSON_SERIALIZE_ENUM (HybridArch, { { HybridArch::Mild, "mild" }, { HybridArch::Full, "full" }, { HybridArch::PlugIn, "plug_in" }, { HybridArch::Series, "series" }, { HybridArch::Parallel, "parallel" }, { HybridArch::SeriesParallel, "series_parallel" } })
NLOHMANN_JSON_SERIALIZE_ENUM (ModSource, { { ModSource::Rpm, "rpm" }, { ModSource::Torque, "torque" }, { ModSource::Load, "load" }, { ModSource::Throttle, "throttle" }, { ModSource::Speed, "speed" }, { ModSource::Gear, "gear" }, { ModSource::Temperature, "temperature" }, { ModSource::Boost, "boost" }, { ModSource::ElectricFreq, "electric_freq" }, { ModSource::Battery, "battery" }, { ModSource::Regen, "regen" }, { ModSource::ShiftEvent, "shift_event" } })
NLOHMANN_JSON_SERIALIZE_ENUM (ModTarget, { { ModTarget::GainDb, "gain_db" }, { ModTarget::Pan, "pan" }, { ModTarget::LowpassHz, "lowpass" }, { ModTarget::Drive, "drive" } })

namespace
{
template <class T> void rd (const json& j, const char* key, T& v)
{
    if (! j.is_object()) return;
    auto it = j.find (key);
    if (it == j.end() || it->is_null()) return;
    try
    {
        v = it->get<T>();
    }
    catch (const std::exception&)
    {
        // wrong type: keep default, sanitize() reports ranges later
    }
}

json curveJ (const Curve& c)
{
    json pts = json::array();
    for (auto& p : c.points())
        pts.push_back ({ p.x, p.y });
    return { { "interp", c.interpolation() }, { "points", pts } };
}

Curve curveFromJ (const json& j)
{
    Curve c;
    if (! j.is_object()) return c;
    CurveInterp interp = CurveInterp::Smooth;
    rd (j, "interp", interp);
    std::vector<CurvePoint> pts;
    if (j.contains ("points") && j["points"].is_array())
        for (auto& p : j["points"])
            if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number())
                pts.push_back ({ p[0].get<double>(), p[1].get<double>() });
    c = Curve (pts, interp);
    return c;
}

json engineJ (const ThermalEngineSpec& e)
{
    return { { "layout", e.layoutId }, { "crank", e.crankId }, { "firing_order", e.firingOrder }, { "bank_angle_deg", e.bankAngleDeg },
             { "bore_mm", e.boreMm }, { "stroke_mm", e.strokeMm }, { "compression_ratio", e.compressionRatio },
             { "idle_rpm", e.idleRpm }, { "redline_rpm", e.redlineRpm }, { "limiter_rpm", e.limiterRpm }, { "limiter_hysteresis_rpm", e.limiterHysteresisRpm },
             { "limiter_type", e.limiterType }, { "valves_per_cylinder", e.valvesPerCylinder }, { "cam", e.cam },
             { "exhaust_duration_deg", e.exhaustDurationDeg }, { "cam_overlap_deg", e.camOverlapDeg }, { "fuel", e.fuel },
             { "combustion_harshness", e.combustionHarshness }, { "combustion_variability", e.combustionVariability }, { "overrun_crackle", e.overrunCrackle },
             { "flywheel_inertia", e.flywheelInertia }, { "friction_scale", e.frictionScale }, { "throttle_response_ms", e.throttleResponseMs },
             { "torque_curve", curveJ (e.torqueCurve) }, { "throttle_map", curveJ (e.throttleMap) } };
}

void engineFromJ (const json& j, ThermalEngineSpec& e)
{
    rd (j, "layout", e.layoutId); rd (j, "crank", e.crankId); rd (j, "firing_order", e.firingOrder); rd (j, "bank_angle_deg", e.bankAngleDeg);
    rd (j, "bore_mm", e.boreMm); rd (j, "stroke_mm", e.strokeMm); rd (j, "compression_ratio", e.compressionRatio);
    rd (j, "idle_rpm", e.idleRpm); rd (j, "redline_rpm", e.redlineRpm); rd (j, "limiter_rpm", e.limiterRpm); rd (j, "limiter_hysteresis_rpm", e.limiterHysteresisRpm);
    rd (j, "limiter_type", e.limiterType); rd (j, "valves_per_cylinder", e.valvesPerCylinder); rd (j, "cam", e.cam);
    rd (j, "exhaust_duration_deg", e.exhaustDurationDeg); rd (j, "cam_overlap_deg", e.camOverlapDeg); rd (j, "fuel", e.fuel);
    rd (j, "combustion_harshness", e.combustionHarshness); rd (j, "combustion_variability", e.combustionVariability); rd (j, "overrun_crackle", e.overrunCrackle);
    rd (j, "flywheel_inertia", e.flywheelInertia); rd (j, "friction_scale", e.frictionScale); rd (j, "throttle_response_ms", e.throttleResponseMs);
    if (j.contains ("torque_curve")) e.torqueCurve = curveFromJ (j["torque_curve"]);
    if (j.contains ("throttle_map")) e.throttleMap = curveFromJ (j["throttle_map"]);
}

json intakeJ (const IntakeSpec& i)
{
    return { { "filter", i.filter }, { "manifold", i.manifold }, { "airbox_volume_l", i.airboxVolumeL }, { "snorkel_length_mm", i.snorkelLengthMm },
             { "snorkel_diameter_mm", i.snorkelDiameterMm }, { "runner_length_mm", i.runnerLengthMm }, { "runner_diameter_mm", i.runnerDiameterMm },
             { "plenum_volume_l", i.plenumVolumeL }, { "throttle_diameter_mm", i.throttleDiameterMm }, { "resonator", i.resonatorEnabled },
             { "resonator_hz", i.resonatorFreqHz } };
}

void intakeFromJ (const json& j, IntakeSpec& i)
{
    rd (j, "filter", i.filter); rd (j, "manifold", i.manifold); rd (j, "airbox_volume_l", i.airboxVolumeL); rd (j, "snorkel_length_mm", i.snorkelLengthMm);
    rd (j, "snorkel_diameter_mm", i.snorkelDiameterMm); rd (j, "runner_length_mm", i.runnerLengthMm); rd (j, "runner_diameter_mm", i.runnerDiameterMm);
    rd (j, "plenum_volume_l", i.plenumVolumeL); rd (j, "throttle_diameter_mm", i.throttleDiameterMm); rd (j, "resonator", i.resonatorEnabled);
    rd (j, "resonator_hz", i.resonatorFreqHz);
}

json exhaustJ (const ExhaustSpec& x)
{
    return { { "header", x.header }, { "primary_length_mm", x.primaryLengthMm }, { "primary_diameter_mm", x.primaryDiameterMm },
             { "unequal_length", x.unequalLength }, { "routing", x.routing }, { "midpipe_length_mm", x.midpipeLengthMm },
             { "pipe_diameter_mm", x.pipeDiameterMm }, { "catalyst", x.catalyst }, { "resonator", x.resonatorEnabled },
             { "resonator_length_mm", x.resonatorLengthMm }, { "muffler", x.muffler }, { "muffler_volume_l", x.mufflerVolumeL },
             { "tailpipe_length_mm", x.tailpipeLengthMm }, { "tailpipe_diameter_mm", x.tailpipeDiameterMm }, { "outlets", x.outlets },
             { "outlet_position", x.outletPosition } };
}

void exhaustFromJ (const json& j, ExhaustSpec& x)
{
    rd (j, "header", x.header); rd (j, "primary_length_mm", x.primaryLengthMm); rd (j, "primary_diameter_mm", x.primaryDiameterMm);
    rd (j, "unequal_length", x.unequalLength); rd (j, "routing", x.routing); rd (j, "midpipe_length_mm", x.midpipeLengthMm);
    rd (j, "pipe_diameter_mm", x.pipeDiameterMm); rd (j, "catalyst", x.catalyst); rd (j, "resonator", x.resonatorEnabled);
    rd (j, "resonator_length_mm", x.resonatorLengthMm); rd (j, "muffler", x.muffler); rd (j, "muffler_volume_l", x.mufflerVolumeL);
    rd (j, "tailpipe_length_mm", x.tailpipeLengthMm); rd (j, "tailpipe_diameter_mm", x.tailpipeDiameterMm); rd (j, "outlets", x.outlets);
    rd (j, "outlet_position", x.outletPosition);
}

json boostJ (const BoostSpec& b)
{
    return { { "type", b.type }, { "max_boost_bar", b.maxBoostBar }, { "turbo_size", b.turboSize }, { "spool_rpm", b.spoolRpm },
             { "response_ms", b.responseMs }, { "external_wastegate", b.externalWastegate }, { "blow_off", b.blowOff },
             { "whistle_min_hz", b.whistleMinHz }, { "whistle_max_hz", b.whistleMaxHz }, { "sc_drive_ratio", b.superchargerDriveRatio },
             { "sc_lobes", b.superchargerLobes } };
}

void boostFromJ (const json& j, BoostSpec& b)
{
    rd (j, "type", b.type); rd (j, "max_boost_bar", b.maxBoostBar); rd (j, "turbo_size", b.turboSize); rd (j, "spool_rpm", b.spoolRpm);
    rd (j, "response_ms", b.responseMs); rd (j, "external_wastegate", b.externalWastegate); rd (j, "blow_off", b.blowOff);
    rd (j, "whistle_min_hz", b.whistleMinHz); rd (j, "whistle_max_hz", b.whistleMaxHz); rd (j, "sc_drive_ratio", b.superchargerDriveRatio);
    rd (j, "sc_lobes", b.superchargerLobes);
}

json electricJ (const ElectricSpec& m)
{
    return { { "type", m.type }, { "poles", m.poles }, { "slots", m.slots }, { "phases", m.phases }, { "rotor", m.rotor }, { "stator", m.stator },
             { "rotor_bars", m.rotorBars }, { "max_rpm", m.maxRpm }, { "peak_torque_nm", m.peakTorqueNm }, { "peak_power_kw", m.peakPowerKw },
             { "voltage", m.voltage }, { "max_current_a", m.maxCurrentA }, { "rated_slip", m.ratedSlip }, { "controller", m.controller },
             { "pwm_hz", m.pwmHz }, { "random_pwm", m.randomPwm }, { "semiconductor", m.semiconductor }, { "cooling", m.cooling },
             { "rotor_inertia", m.rotorInertia }, { "regen_strength", m.regenStrength }, { "bearing_balls", m.bearingBalls },
             { "resonance1_hz", m.resonance1Hz }, { "resonance2_hz", m.resonance2Hz }, { "resistance_ohm", m.resistanceOhm } };
}

void electricFromJ (const json& j, ElectricSpec& m)
{
    rd (j, "type", m.type); rd (j, "poles", m.poles); rd (j, "slots", m.slots); rd (j, "phases", m.phases); rd (j, "rotor", m.rotor); rd (j, "stator", m.stator);
    rd (j, "rotor_bars", m.rotorBars); rd (j, "max_rpm", m.maxRpm); rd (j, "peak_torque_nm", m.peakTorqueNm); rd (j, "peak_power_kw", m.peakPowerKw);
    rd (j, "voltage", m.voltage); rd (j, "max_current_a", m.maxCurrentA); rd (j, "rated_slip", m.ratedSlip); rd (j, "controller", m.controller);
    rd (j, "pwm_hz", m.pwmHz); rd (j, "random_pwm", m.randomPwm); rd (j, "semiconductor", m.semiconductor); rd (j, "cooling", m.cooling);
    rd (j, "rotor_inertia", m.rotorInertia); rd (j, "regen_strength", m.regenStrength); rd (j, "bearing_balls", m.bearingBalls);
    rd (j, "resonance1_hz", m.resonance1Hz); rd (j, "resonance2_hz", m.resonance2Hz); rd (j, "resistance_ohm", m.resistanceOhm);
}

json hybridJ (const HybridSpec& h)
{
    return { { "arch", h.arch }, { "battery_kwh", h.batteryKWh }, { "soc_initial", h.socInitial }, { "soc_min", h.socMin }, { "soc_max", h.socMax },
             { "max_battery_kw", h.maxBatteryKw }, { "ev_max_speed_kmh", h.evMaxSpeedKmh }, { "ev_max_power_kw", h.evMaxPowerKw },
             { "engine_min_on_s", h.engineMinOnSeconds }, { "charge_power_kw", h.chargePowerKw }, { "regen_max_kw", h.regenMaxKw }, { "motor_assist", h.motorAssist } };
}

void hybridFromJ (const json& j, HybridSpec& h)
{
    rd (j, "arch", h.arch); rd (j, "battery_kwh", h.batteryKWh); rd (j, "soc_initial", h.socInitial); rd (j, "soc_min", h.socMin); rd (j, "soc_max", h.socMax);
    rd (j, "max_battery_kw", h.maxBatteryKw); rd (j, "ev_max_speed_kmh", h.evMaxSpeedKmh); rd (j, "ev_max_power_kw", h.evMaxPowerKw);
    rd (j, "engine_min_on_s", h.engineMinOnSeconds); rd (j, "charge_power_kw", h.chargePowerKw); rd (j, "regen_max_kw", h.regenMaxKw); rd (j, "motor_assist", h.motorAssist);
}

json transmissionJ (const TransmissionSpec& t)
{
    return { { "type", t.type }, { "ratios", t.ratios }, { "final_drive", t.finalDrive }, { "efficiency", t.efficiency }, { "shift_time_ms", t.shiftTimeMs },
             { "clutch_capacity_nm", t.clutchCapacityNm }, { "converter_stall_rpm", t.converterStallRpm }, { "lockup_from_gear", t.lockupFromGear },
             { "upshift_rpm_light", t.upshiftRpmLight }, { "upshift_rpm_full", t.upshiftRpmFull }, { "downshift_rpm", t.downshiftRpm },
             { "kickdown_throttle", t.kickdownThrottle }, { "cvt_ratio_min", t.cvtRatioMin }, { "cvt_ratio_max", t.cvtRatioMax },
             { "cvt_rpm_light", t.cvtRpmLight }, { "cvt_rpm_full", t.cvtRpmFull }, { "artificial_shifts", t.artificialShifts }, { "pinion_teeth", t.pinionTeeth } };
}

void transmissionFromJ (const json& j, TransmissionSpec& t)
{
    rd (j, "type", t.type); rd (j, "ratios", t.ratios); rd (j, "final_drive", t.finalDrive); rd (j, "efficiency", t.efficiency); rd (j, "shift_time_ms", t.shiftTimeMs);
    rd (j, "clutch_capacity_nm", t.clutchCapacityNm); rd (j, "converter_stall_rpm", t.converterStallRpm); rd (j, "lockup_from_gear", t.lockupFromGear);
    rd (j, "upshift_rpm_light", t.upshiftRpmLight); rd (j, "upshift_rpm_full", t.upshiftRpmFull); rd (j, "downshift_rpm", t.downshiftRpm);
    rd (j, "kickdown_throttle", t.kickdownThrottle); rd (j, "cvt_ratio_min", t.cvtRatioMin); rd (j, "cvt_ratio_max", t.cvtRatioMax);
    rd (j, "cvt_rpm_light", t.cvtRpmLight); rd (j, "cvt_rpm_full", t.cvtRpmFull); rd (j, "artificial_shifts", t.artificialShifts); rd (j, "pinion_teeth", t.pinionTeeth);
}

json vehicleJ (const VehicleSpec& v)
{
    return { { "mass_kg", v.massKg }, { "rolling_resistance", v.rollingResistance }, { "drag_area", v.dragArea }, { "wheel_radius_m", v.wheelRadiusM },
             { "brake_force_n", v.brakeForceN }, { "grade_percent", v.gradePercent } };
}

void vehicleFromJ (const json& j, VehicleSpec& v)
{
    rd (j, "mass_kg", v.massKg); rd (j, "rolling_resistance", v.rollingResistance); rd (j, "drag_area", v.dragArea); rd (j, "wheel_radius_m", v.wheelRadiusM);
    rd (j, "brake_force_n", v.brakeForceN); rd (j, "grade_percent", v.gradePercent);
}

json loopsJ (const LoopSpec& l)
{
    return { { "rpm_min", l.rpmMin }, { "rpm_max", l.rpmMax }, { "rpm_points", l.rpmPoints }, { "load_points", l.loadPoints }, { "overrun_state", l.overrunState },
             { "regen_state", l.regenState }, { "loop_seconds", l.loopSeconds }, { "crossfade_ms", l.crossfadeMs }, { "sample_rate", l.sampleRate },
             { "bit_depth", l.bitDepth }, { "flac", l.flac }, { "stems", l.stems }, { "normalize", l.normalize }, { "start_stop", l.startStop }, { "threads", l.threads } };
}

void loopsFromJ (const json& j, LoopSpec& l)
{
    rd (j, "rpm_min", l.rpmMin); rd (j, "rpm_max", l.rpmMax); rd (j, "rpm_points", l.rpmPoints); rd (j, "load_points", l.loadPoints); rd (j, "overrun_state", l.overrunState);
    rd (j, "regen_state", l.regenState); rd (j, "loop_seconds", l.loopSeconds); rd (j, "crossfade_ms", l.crossfadeMs); rd (j, "sample_rate", l.sampleRate);
    rd (j, "bit_depth", l.bitDepth); rd (j, "flac", l.flac); rd (j, "stems", l.stems); rd (j, "normalize", l.normalize); rd (j, "start_stop", l.startStop); rd (j, "threads", l.threads);
}

json fxJ (const LayerFx& f)
{
    return { { "highpass_hz", f.highpassHz }, { "lowpass_hz", f.lowpassHz }, { "low_db", f.lowDb }, { "mid_db", f.midDb }, { "mid_hz", f.midHz },
             { "high_db", f.highDb }, { "drive", f.drive }, { "comp_threshold_db", f.compThresholdDb }, { "comp_ratio", f.compRatio },
             { "ir_path", f.irPath }, { "conv_mix", f.convMix }, { "reverb_send", f.reverbSend } };
}

void fxFromJ (const json& j, LayerFx& f)
{
    rd (j, "highpass_hz", f.highpassHz); rd (j, "lowpass_hz", f.lowpassHz); rd (j, "low_db", f.lowDb); rd (j, "mid_db", f.midDb); rd (j, "mid_hz", f.midHz);
    rd (j, "high_db", f.highDb); rd (j, "drive", f.drive); rd (j, "comp_threshold_db", f.compThresholdDb); rd (j, "comp_ratio", f.compRatio);
    rd (j, "ir_path", f.irPath); rd (j, "conv_mix", f.convMix); rd (j, "reverb_send", f.reverbSend);
}

json mixJ (const SoundMix& m)
{
    json layers = json::object();
    for (int i = 0; i < kNumLayers; ++i)
    {
        const auto& l = m.layers[static_cast<std::size_t> (i)];
        layers[layerName (static_cast<LayerId> (i))] = { { "gain_db", l.gainDb }, { "pan", l.pan }, { "mute", l.mute }, { "solo", l.solo } };
    }
    json fx = json::object();
    for (int i = 0; i < kNumLayers; ++i)
        if (! (m.fx[static_cast<std::size_t> (i)] == LayerFx {}))
            fx[layerName (static_cast<LayerId> (i))] = fxJ (m.fx[static_cast<std::size_t> (i)]);
    json autos = json::array();
    for (auto& a : m.automations)
        autos.push_back ({ { "enabled", a.enabled }, { "layer", layerName (static_cast<LayerId> (a.layer)) }, { "source", a.source }, { "target", a.target },
                           { "curve", curveJ (a.curve) } });
    return { { "master_gain_db", m.masterGainDb }, { "stereo_width", m.stereoWidth }, { "layers", layers }, { "fx", fx }, { "automations", autos },
             { "reverb_size", m.reverbSize }, { "reverb_damping", m.reverbDamping }, { "reverb_return_db", m.reverbReturnDb } };
}

void mixFromJ (const json& j, SoundMix& m)
{
    rd (j, "master_gain_db", m.masterGainDb);
    rd (j, "stereo_width", m.stereoWidth);
    if (j.contains ("layers") && j["layers"].is_object())
        for (int i = 0; i < kNumLayers; ++i)
        {
            const char* name = layerName (static_cast<LayerId> (i));
            if (! j["layers"].contains (name)) continue;
            const auto& lj = j["layers"][name];
            auto& l = m.layers[static_cast<std::size_t> (i)];
            rd (lj, "gain_db", l.gainDb); rd (lj, "pan", l.pan); rd (lj, "mute", l.mute); rd (lj, "solo", l.solo);
        }
    rd (j, "reverb_size", m.reverbSize); rd (j, "reverb_damping", m.reverbDamping); rd (j, "reverb_return_db", m.reverbReturnDb);
    if (j.contains ("fx") && j["fx"].is_object())
        for (int i = 0; i < kNumLayers; ++i)
        {
            const char* name = layerName (static_cast<LayerId> (i));
            if (j["fx"].contains (name)) fxFromJ (j["fx"][name], m.fx[static_cast<std::size_t> (i)]);
        }
    m.automations.clear();
    if (j.contains ("automations") && j["automations"].is_array())
        for (auto& aj : j["automations"])
        {
            Automation a;
            rd (aj, "enabled", a.enabled); rd (aj, "source", a.source); rd (aj, "target", a.target);
            std::string layer;
            rd (aj, "layer", layer);
            for (int i = 0; i < kNumLayers; ++i)
                if (layer == layerName (static_cast<LayerId> (i))) a.layer = i;
            if (aj.contains ("curve")) a.curve = curveFromJ (aj["curve"]);
            m.automations.push_back (a);
        }
}

json samplesJ (const std::vector<SampleRef>& s)
{
    json a = json::array();
    for (auto& r : s)
        a.push_back ({ { "id", r.id }, { "name", r.name }, { "path", r.path }, { "category", r.category }, { "rpm", r.rpm }, { "load", r.load },
                       { "start_sec", r.startSec }, { "end_sec", r.endSec }, { "loop", r.loop }, { "gain_db", r.gainDb } });
    return a;
}

void samplesFromJ (const json& j, std::vector<SampleRef>& s)
{
    s.clear();
    if (! j.is_array()) return;
    for (auto& rj : j)
    {
        SampleRef r;
        rd (rj, "id", r.id); rd (rj, "name", r.name); rd (rj, "path", r.path); rd (rj, "category", r.category); rd (rj, "rpm", r.rpm);
        rd (rj, "load", r.load); rd (rj, "start_sec", r.startSec); rd (rj, "end_sec", r.endSec); rd (rj, "loop", r.loop); rd (rj, "gain_db", r.gainDb);
        s.push_back (r);
    }
}

std::filesystem::path u8 (const std::string& s) { return std::filesystem::path (reinterpret_cast<const char8_t*> (s.c_str())); }
} // namespace

std::string curveToJson (const Curve& c) { return curveJ (c).dump(); }

std::string projectToJson (const Project& p, int indent)
{
    json j = { { "format", "EngineSoundDesigner.project" },
               { "schema_version", kProjectSchemaVersion },
               { "id", p.id },
               { "name", p.name },
               { "category", p.category },
               { "description", p.description },
               { "kind", p.kind },
               { "seed", p.seed },
               { "engine", engineJ (p.engine) },
               { "intake", intakeJ (p.intake) },
               { "exhaust", exhaustJ (p.exhaust) },
               { "boost", boostJ (p.boost) },
               { "electric", electricJ (p.electric) },
               { "hybrid", hybridJ (p.hybrid) },
               { "transmission", transmissionJ (p.transmission) },
               { "vehicle", vehicleJ (p.vehicle) },
               { "loops", loopsJ (p.loops) },
               { "mix", mixJ (p.mix) },
               { "samples", samplesJ (p.samples) } };
    return j.dump (indent);
}

LoadResult projectFromJson (const std::string& text)
{
    LoadResult r;
    json j;
    try
    {
        j = json::parse (text);
    }
    catch (const std::exception& ex)
    {
        r.error = std::string ("Invalid JSON: ") + ex.what();
        return r;
    }
    if (! j.is_object() || j.value ("format", std::string()) != "EngineSoundDesigner.project")
    {
        r.error = "Not an Engine Sound Designer project file.";
        return r;
    }
    int version = 0;
    rd (j, "schema_version", version);
    if (version > kProjectSchemaVersion)
        r.fixes.push_back ({ Severity::Warning, "schema_version", "File written by a newer version: unknown fields are ignored." });

    Project& p = r.project;
    p.engine.torqueCurve = {};
    p.engine.throttleMap = {};
    rd (j, "id", p.id); rd (j, "name", p.name); rd (j, "category", p.category); rd (j, "description", p.description);
    rd (j, "kind", p.kind); rd (j, "seed", p.seed);
    if (j.contains ("engine")) engineFromJ (j["engine"], p.engine);
    if (j.contains ("intake")) intakeFromJ (j["intake"], p.intake);
    if (j.contains ("exhaust")) exhaustFromJ (j["exhaust"], p.exhaust);
    if (j.contains ("boost")) boostFromJ (j["boost"], p.boost);
    if (j.contains ("electric")) electricFromJ (j["electric"], p.electric);
    if (j.contains ("hybrid")) hybridFromJ (j["hybrid"], p.hybrid);
    if (j.contains ("transmission")) transmissionFromJ (j["transmission"], p.transmission);
    if (j.contains ("vehicle")) vehicleFromJ (j["vehicle"], p.vehicle);
    if (j.contains ("loops")) loopsFromJ (j["loops"], p.loops);
    if (j.contains ("mix")) mixFromJ (j["mix"], p.mix);
    if (j.contains ("samples")) samplesFromJ (j["samples"], p.samples);
    p.schemaVersion = kProjectSchemaVersion;

    auto fixes = sanitize (p);
    r.fixes.insert (r.fixes.end(), fixes.begin(), fixes.end());
    r.ok = true;
    return r;
}

bool saveProjectFile (const Project& p, const std::string& path, std::string* error)
{
    namespace fs = std::filesystem;
    const fs::path target = u8 (path);
    fs::path tmp = target;
    tmp += ".tmp";
    try
    {
        if (target.has_parent_path()) fs::create_directories (target.parent_path());
        {
            std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
            if (! f)
            {
                if (error) *error = "Cannot write " + path;
                return false;
            }
            const auto text = projectToJson (p);
            f.write (text.data(), static_cast<std::streamsize> (text.size()));
            f.flush();
            if (! f)
            {
                if (error) *error = "Write error on " + path;
                return false;
            }
        }
        fs::rename (tmp, target); // atomic replace on the same volume
        return true;
    }
    catch (const std::exception& ex)
    {
        if (error) *error = ex.what();
        std::error_code ec;
        fs::remove (tmp, ec);
        return false;
    }
}

LoadResult loadProjectFile (const std::string& path)
{
    std::ifstream f (u8 (path), std::ios::binary);
    if (! f)
    {
        LoadResult r;
        r.error = "Cannot open " + path;
        return r;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return projectFromJson (ss.str());
}
} // namespace esd
