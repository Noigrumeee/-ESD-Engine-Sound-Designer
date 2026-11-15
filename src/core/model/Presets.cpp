#include "Presets.h"

namespace esd
{
const std::vector<PresetInfo>& builtinPresets()
{
    static const std::vector<PresetInfo> list = {
        { "i4_na", "Inline 4 - 2.0 NA", "Inline", "2.0 L DOHC 16v, cast manifold, single exhaust with resonator and chambered muffler." },
        { "i4_turbo", "Inline 4 - 2.0 Turbo", "Inline", "2.0 L turbo 1.2 bar, atmospheric blow-off valve." },
        { "i6_na", "Inline 6 - 3.0 NA", "Inline", "3.0 L straight six, tri-Y headers, smooth and balanced." },
        { "v6_60", "V6 - 3.5 60 deg", "V", "3.5 L 60 deg V6, dual exhaust with X-pipe." },
        { "v8_cross", "V8 - 6.2 cross-plane", "V", "6.2 L pushrod cross-plane V8, long tubes, true dual exhaust: the burble." },
        { "v8_flat", "V8 - 4.5 flat-plane", "V", "4.5 L high revving flat-plane V8, each bank fires evenly: the scream." },
        { "v10_72", "V10 - 5.2 72 deg", "V", "5.2 L even-fire V10, 8700 rpm." },
        { "v12_60", "V12 - 6.5 60 deg", "V", "6.5 L naturally aspirated V12, 8900 rpm." },
        { "boxer4_turbo", "Boxer 4 - 2.5 Turbo", "Boxer", "2.5 L flat-4 turbo with unequal-length headers (rumble)." },
        { "wankel2", "Wankel - 2 rotor", "Rotary", "1.3 L two-rotor, peripheral exhaust ports." },
        { "single_moto", "Single - 450 moto", "Motorcycle", "450 cc 4-stroke single, short exhaust." },
        { "vtwin45", "V-twin - 1200 45 deg", "Motorcycle", "1200 cc 45 deg common-pin V-twin (potato-potato idle)." },
        { "i4_crossplane_moto", "Inline 4 - 1000 crossplane moto", "Motorcycle", "Superbike inline 4 with crossplane crank, 14 000 rpm." },
        { "ev_pmsm", "Electric - PMSM 220 kW", "Electric", "Interior permanent magnet synchronous motor, 6 poles / 54 slots, SiC inverter 8 kHz, fixed 9:1 reduction." },
        { "ev_bldc", "Electric - BLDC 15 kW kart", "Electric", "Surface magnet BLDC, 10 poles / 12 slots concentrated, six-step commutation, air cooled." },
        { "ev_induction", "Electric - Induction 310 kW", "Electric", "Squirrel cage induction motor, 4 poles, 74 rotor bars, IGBT inverter 6 kHz." },
        { "hybrid_sp", "Hybrid - 1.8 series-parallel", "Hybrid", "Atkinson inline 4 + 53 kW PMSM, power split: the engine speed follows the power demand." },
        { "hybrid_p2", "Hybrid - 2.0 turbo plug-in P2", "Hybrid", "2.0 turbo + 80 kW motor between engine and an 8-speed automatic, 13 kWh battery." },
    };
    return list;
}

namespace
{
void base (Project& p, const std::string& name, const std::string& cat, const std::string& layout, const std::string& crank, double bore, double stroke,
           double cr, double idle, double red, double lim)
{
    p.name = name;
    p.category = cat;
    p.engine.layoutId = layout;
    p.engine.crankId = crank;
    p.engine.boreMm = bore;
    p.engine.strokeMm = stroke;
    p.engine.compressionRatio = cr;
    p.engine.idleRpm = idle;
    p.engine.redlineRpm = red;
    p.engine.limiterRpm = lim;
}

void finish (Project& p)
{
    p.engine.torqueCurve = estimateTorqueCurve (p.engine, p.boost);
    p.engine.throttleMap = defaultThrottleMap();
    p.id = newUuid();
    sanitize (p);
}
} // namespace

Project makePreset (const std::string& id)
{
    Project p;
    auto& e = p.engine;
    auto& x = p.exhaust;
    auto& in = p.intake;
    auto& b = p.boost;

    for (auto& info : builtinPresets())
        if (info.id == id) p.description = info.description;

    if (id == "i4_turbo")
    {
        base (p, "Inline 4 - 2.0 Turbo", "Inline", "i4", "flat", 83, 92, 9.5, 800, 6500, 6700);
        b.type = BoostType::Turbo; b.maxBoostBar = 1.2; b.spoolRpm = 2800; b.turboSize = 0.45; b.responseMs = 550; b.blowOff = BlowOffType::Atmospheric;
        x.header = HeaderType::CastManifold; x.muffler = MufflerType::Chambered; x.mufflerVolumeL = 11; x.outlets = 2; x.outletPosition = OutletPosition::Split;
        x.pipeDiameterMm = 65; x.tailpipeDiameterMm = 70;
        in.filter = AirFilterType::PanelFilter;
        e.combustionHarshness = 0.35;
        p.seed = 2002;
        p.mix.masterGainDb = 1.0f;
    }
    else if (id == "i6_na")
    {
        base (p, "Inline 6 - 3.0 NA", "Inline", "i6", "120", 84, 89.6, 11.0, 750, 7500, 7800);
        x.header = HeaderType::TriY; x.primaryLengthMm = 550; x.muffler = MufflerType::Chambered; x.mufflerVolumeL = 14; x.outlets = 2; x.outletPosition = OutletPosition::Split;
        x.pipeDiameterMm = 60; x.tailpipeDiameterMm = 65;
        in.manifold = IntakeManifoldType::DualPlenum; in.runnerLengthMm = 380;
        e.combustionHarshness = 0.35; e.combustionVariability = 0.2;
        p.seed = 3003;
        p.mix.masterGainDb = 5.0f;
    }
    else if (id == "v6_60")
    {
        base (p, "V6 - 3.5 60 deg", "V", "v6", "60_even", 94, 83, 10.8, 700, 6600, 6800);
        x.routing = ExhaustRouting::DualXPipe; x.header = HeaderType::ShortTube; x.primaryLengthMm = 400; x.muffler = MufflerType::Chambered; x.mufflerVolumeL = 10;
        x.outlets = 2; x.outletPosition = OutletPosition::Split; x.pipeDiameterMm = 55; x.tailpipeDiameterMm = 63;
        p.seed = 6006;
        p.mix.masterGainDb = 6.0f;
    }
    else if (id == "v8_cross")
    {
        base (p, "V8 - 6.2 cross-plane", "V", "v8", "crossplane", 103.25, 92, 10.5, 650, 6400, 6600);
        e.cam = CamType::OHV; e.valvesPerCylinder = 2; e.camOverlapDeg = 48; e.combustionHarshness = 0.5; e.combustionVariability = 0.45;
        e.exhaustDurationDeg = 250; e.overrunCrackle = 0.2;
        x.header = HeaderType::LongTube; x.primaryLengthMm = 850; x.primaryDiameterMm = 48; x.routing = ExhaustRouting::Dual;
        x.midpipeLengthMm = 1900; x.pipeDiameterMm = 76; x.catalyst = CatalystType::HighFlow; x.resonatorEnabled = false;
        x.muffler = MufflerType::StraightThrough; x.mufflerVolumeL = 10; x.tailpipeDiameterMm = 89; x.outlets = 2; x.outletPosition = OutletPosition::Split;
        in.filter = AirFilterType::OpenCone; in.runnerLengthMm = 320; in.throttleDiameterMm = 90; in.plenumVolumeL = 7;
        p.seed = 8008;
        p.mix.masterGainDb = -4.0f;
    }
    else if (id == "v8_flat")
    {
        base (p, "V8 - 4.5 flat-plane", "V", "v8", "flatplane", 94, 81, 12.5, 1000, 9000, 9200);
        e.combustionHarshness = 0.45; e.combustionVariability = 0.25; e.camOverlapDeg = 40; e.overrunCrackle = 0.3;
        x.header = HeaderType::LongTube; x.primaryLengthMm = 600; x.primaryDiameterMm = 42; x.routing = ExhaustRouting::Dual;
        x.midpipeLengthMm = 1400; x.pipeDiameterMm = 65; x.catalyst = CatalystType::HighFlow; x.resonatorEnabled = false;
        x.muffler = MufflerType::StraightThrough; x.mufflerVolumeL = 8; x.tailpipeDiameterMm = 80; x.outlets = 4; x.outletPosition = OutletPosition::Split;
        in.manifold = IntakeManifoldType::IndividualThrottles; in.filter = AirFilterType::PanelFilter; in.runnerLengthMm = 220;
        p.seed = 8118;
        p.mix.masterGainDb = -5.5f;
    }
    else if (id == "v10_72")
    {
        base (p, "V10 - 5.2 72 deg", "V", "v10", "72_even", 84.5, 92.8, 12.5, 1000, 8500, 8700);
        e.combustionHarshness = 0.45;
        x.header = HeaderType::LongTube; x.primaryLengthMm = 620; x.routing = ExhaustRouting::DualXPipe; x.pipeDiameterMm = 70;
        x.muffler = MufflerType::StraightThrough; x.mufflerVolumeL = 10; x.tailpipeDiameterMm = 85; x.outlets = 2; x.outletPosition = OutletPosition::Split;
        x.resonatorEnabled = false;
        in.manifold = IntakeManifoldType::DualPlenum;
        p.seed = 1010;
        p.mix.masterGainDb = 4.0f;
    }
    else if (id == "v12_60")
    {
        base (p, "V12 - 6.5 60 deg", "V", "v12", "60_even", 95, 76.4, 13.5, 950, 8700, 8900);
        e.combustionHarshness = 0.35; e.combustionVariability = 0.2;
        x.header = HeaderType::LongTube; x.primaryLengthMm = 650; x.routing = ExhaustRouting::Dual; x.pipeDiameterMm = 70;
        x.muffler = MufflerType::StraightThrough; x.mufflerVolumeL = 12; x.tailpipeDiameterMm = 85; x.outlets = 2; x.outletPosition = OutletPosition::Split;
        x.resonatorEnabled = false;
        in.manifold = IntakeManifoldType::IndividualThrottles; in.filter = AirFilterType::PanelFilter;
        p.seed = 1212;
        p.mix.masterGainDb = -8.0f;
    }
    else if (id == "boxer4_turbo")
    {
        base (p, "Boxer 4 - 2.5 Turbo", "Boxer", "boxer4", "std", 99.5, 79, 8.2, 800, 6700, 7000);
        b.type = BoostType::Turbo; b.maxBoostBar = 1.1; b.spoolRpm = 3300; b.turboSize = 0.6; b.responseMs = 700; b.blowOff = BlowOffType::Recirculating;
        x.header = HeaderType::ShortTube; x.unequalLength = 0.85; x.primaryLengthMm = 500; x.muffler = MufflerType::Chambered; x.mufflerVolumeL = 10;
        x.outlets = 4; x.outletPosition = OutletPosition::Split; x.pipeDiameterMm = 76; x.tailpipeDiameterMm = 80;
        e.combustionVariability = 0.35;
        p.seed = 4444;
        p.mix.masterGainDb = 1.5f;
    }
    else if (id == "wankel2")
    {
        base (p, "Wankel - 2 rotor", "Rotary", "wankel2", "std", 94, 94, 9.4, 900, 9000, 9300);
        e.cam = CamType::PortRotary; e.exhaustDurationDeg = 260; e.camOverlapDeg = 55; e.combustionHarshness = 0.7; e.combustionVariability = 0.4;
        e.overrunCrackle = 0.35;
        x.header = HeaderType::ShortTube; x.primaryLengthMm = 350; x.muffler = MufflerType::StraightThrough; x.mufflerVolumeL = 9;
        x.pipeDiameterMm = 65; x.tailpipeDiameterMm = 76; x.outlets = 2; x.outletPosition = OutletPosition::Right;
        p.seed = 1313;
        p.mix.masterGainDb = 1.8f;
    }
    else if (id == "single_moto")
    {
        base (p, "Single - 450 moto", "Motorcycle", "single", "std", 97, 60.8, 12.5, 1600, 11000, 11500);
        e.valvesPerCylinder = 4; e.combustionHarshness = 0.55; e.combustionVariability = 0.3;
        x.header = HeaderType::ShortTube; x.primaryLengthMm = 700; x.primaryDiameterMm = 38; x.midpipeLengthMm = 400; x.pipeDiameterMm = 45;
        x.catalyst = CatalystType::None; x.resonatorEnabled = false; x.muffler = MufflerType::StraightThrough; x.mufflerVolumeL = 3;
        x.tailpipeLengthMm = 150; x.tailpipeDiameterMm = 45; x.outletPosition = OutletPosition::Right;
        in.filter = AirFilterType::PanelFilter; in.airboxVolumeL = 4; in.runnerLengthMm = 250;
        p.seed = 450;
        p.mix.masterGainDb = -5.5f;
    }
    else if (id == "vtwin45")
    {
        base (p, "V-twin - 1200 45 deg", "Motorcycle", "twin_v", "common_pin", 88.9, 96.8, 9.7, 900, 5800, 6000);
        e.bankAngleDeg = 45; e.cam = CamType::OHV; e.valvesPerCylinder = 2; e.combustionHarshness = 0.55; e.combustionVariability = 0.45; e.camOverlapDeg = 40;
        x.header = HeaderType::ShortTube; x.primaryLengthMm = 700; x.primaryDiameterMm = 45; x.routing = ExhaustRouting::Dual;
        x.midpipeLengthMm = 500; x.pipeDiameterMm = 50; x.catalyst = CatalystType::None; x.resonatorEnabled = false;
        x.muffler = MufflerType::StraightThrough; x.mufflerVolumeL = 4; x.tailpipeLengthMm = 300; x.tailpipeDiameterMm = 55; x.outlets = 2;
        x.outletPosition = OutletPosition::Right;
        in.filter = AirFilterType::OpenCone; in.airboxVolumeL = 2;
        p.seed = 1245;
        p.mix.masterGainDb = 2.0f;
    }
    else if (id == "i4_crossplane_moto")
    {
        base (p, "Inline 4 - 1000 crossplane moto", "Motorcycle", "i4", "crossplane", 79, 50.9, 13.0, 1300, 14000, 14300);
        e.combustionHarshness = 0.5;
        x.header = HeaderType::LongTube; x.primaryLengthMm = 600; x.primaryDiameterMm = 35; x.midpipeLengthMm = 500; x.pipeDiameterMm = 50;
        x.catalyst = CatalystType::HighFlow; x.resonatorEnabled = false; x.muffler = MufflerType::StraightThrough; x.mufflerVolumeL = 5;
        x.tailpipeLengthMm = 200; x.tailpipeDiameterMm = 55; x.outletPosition = OutletPosition::Right;
        in.manifold = IntakeManifoldType::IndividualThrottles; in.airboxVolumeL = 9; in.runnerLengthMm = 160;
        p.seed = 1000;
        p.mix.masterGainDb = -5.5f;
    }
    else if (id == "ev_pmsm" || id == "ev_bldc" || id == "ev_induction")
    {
        base (p, "", "Electric", "i4", "flat", 86, 86, 10.5, 850, 6800, 7000);
        p.kind = PowertrainKind::Electric;
        auto& m = p.electric;
        auto& tr = p.transmission;
        tr.type = TransmissionType::EVFixed;
        tr.ratios = { 1.0 };
        if (id == "ev_pmsm")
        {
            p.name = "Electric - PMSM 220 kW";
            m.type = ElectricMotorType::PMSM; m.poles = 6; m.slots = 54; m.rotor = RotorType::InteriorPM; m.stator = StatorWinding::Hairpin;
            m.maxRpm = 17900; m.peakTorqueNm = 420; m.peakPowerKw = 220; m.voltage = 350; m.maxCurrentA = 1000;
            m.controller = MotorController::FOC; m.pwmHz = 8000; m.semiconductor = SemiconductorType::SiCMOSFET; m.cooling = CoolingType::Liquid;
            tr.finalDrive = 9.04; tr.pinionTeeth = 25; p.vehicle.massKg = 1850; p.vehicle.dragArea = 0.5; p.vehicle.wheelRadiusM = 0.34;
            p.seed = 7001;
            p.mix.masterGainDb = 5.0f;
        }
        else if (id == "ev_bldc")
        {
            p.name = "Electric - BLDC 15 kW kart";
            m.type = ElectricMotorType::BLDC; m.poles = 10; m.slots = 12; m.rotor = RotorType::SurfacePM; m.stator = StatorWinding::Concentrated;
            m.maxRpm = 6000; m.peakTorqueNm = 80; m.peakPowerKw = 15; m.voltage = 96; m.maxCurrentA = 300; m.rotorInertia = 0.01;
            m.controller = MotorController::SixStep; m.pwmHz = 16000; m.semiconductor = SemiconductorType::SiIGBT; m.cooling = CoolingType::Air;
            m.resonance1Hz = 2600; m.resonance2Hz = 5200;
            tr.finalDrive = 4.5; tr.pinionTeeth = 13; p.vehicle.massKg = 260; p.vehicle.dragArea = 0.5; p.vehicle.wheelRadiusM = 0.14; p.vehicle.brakeForceN = 3000;
            p.seed = 7002;
            p.mix.masterGainDb = 3.0f;
        }
        else
        {
            p.name = "Electric - Induction 310 kW";
            m.type = ElectricMotorType::Induction; m.poles = 4; m.slots = 60; m.rotorBars = 74; m.rotor = RotorType::SquirrelCage; m.stator = StatorWinding::Distributed;
            m.maxRpm = 16000; m.peakTorqueNm = 600; m.peakPowerKw = 310; m.voltage = 375; m.maxCurrentA = 1300; m.ratedSlip = 0.025;
            m.controller = MotorController::FOC; m.pwmHz = 6000; m.semiconductor = SemiconductorType::SiIGBT; m.cooling = CoolingType::Liquid;
            m.resonance1Hz = 1500; m.resonance2Hz = 3100;
            tr.finalDrive = 9.73; tr.pinionTeeth = 21; p.vehicle.massKg = 2100; p.vehicle.dragArea = 0.58; p.vehicle.wheelRadiusM = 0.35;
            p.seed = 7003;
            p.mix.masterGainDb = 6.0f;
        }
    }
    else if (id == "hybrid_sp" || id == "hybrid_p2")
    {
        auto& m = p.electric;
        auto& tr = p.transmission;
        auto& h = p.hybrid;
        if (id == "hybrid_sp")
        {
            base (p, "Hybrid - 1.8 series-parallel", "Hybrid", "i4", "flat", 80.5, 88.3, 13.0, 1000, 5200, 5400);
            e.combustionHarshness = 0.3; e.camOverlapDeg = 15;
            m.type = ElectricMotorType::PMSM; m.poles = 8; m.slots = 48; m.maxRpm = 13500; m.peakTorqueNm = 163; m.peakPowerKw = 53; m.voltage = 600; m.maxCurrentA = 230;
            m.pwmHz = 5000; m.semiconductor = SemiconductorType::SiIGBT;
            h.arch = HybridArch::SeriesParallel; h.batteryKWh = 1.3; h.evMaxSpeedKmh = 50; h.evMaxPowerKw = 18;
            tr.type = TransmissionType::EVFixed; tr.ratios = { 1.0 }; tr.finalDrive = 8.7;
            p.vehicle.massKg = 1400; p.vehicle.dragArea = 0.56; p.vehicle.wheelRadiusM = 0.31;
            p.seed = 9001;
        }
        else
        {
            base (p, "Hybrid - 2.0 turbo plug-in P2", "Hybrid", "i4", "flat", 83, 92, 9.6, 800, 6200, 6500);
            b.type = BoostType::Turbo; b.maxBoostBar = 1.1; b.spoolRpm = 2600; b.turboSize = 0.4; b.responseMs = 500; b.blowOff = BlowOffType::Recirculating;
            m.type = ElectricMotorType::PMSM; m.poles = 12; m.slots = 72; m.maxRpm = 7000; m.peakTorqueNm = 330; m.peakPowerKw = 80; m.voltage = 400; m.maxCurrentA = 450;
            m.pwmHz = 9000; m.semiconductor = SemiconductorType::SiIGBT;
            h.arch = HybridArch::PlugIn; h.batteryKWh = 13.0; h.evMaxSpeedKmh = 120; h.evMaxPowerKw = 55; h.socInitial = 0.7; h.socMin = 0.15; h.socMax = 0.95;
            tr.type = TransmissionType::TorqueConverter; tr.ratios = { 4.71, 3.14, 2.11, 1.67, 1.29, 1.0, 0.84, 0.67 }; tr.finalDrive = 3.15;
            p.vehicle.massKg = 2050; p.vehicle.dragArea = 0.72; p.vehicle.wheelRadiusM = 0.35;
            p.seed = 9002;
        }
        p.kind = PowertrainKind::Hybrid;
        x.muffler = MufflerType::Chambered; x.mufflerVolumeL = 14;
        p.mix.masterGainDb = 2.0f;
    }
    else // i4_na and fallback
    {
        base (p, "Inline 4 - 2.0 NA", "Inline", "i4", "flat", 86, 86, 10.5, 850, 6800, 7000);
        p.seed = 2000;
        p.mix.masterGainDb = 3.5f;
    }
    // ---- drivetrains (phase 5)
    auto& t = p.transmission;
    auto& veh = p.vehicle;
    if (id == "v8_cross")
    {
        t.type = TransmissionType::TorqueConverter; t.ratios = { 4.56, 2.97, 2.08, 1.69, 1.27, 1.0, 0.85, 0.65 }; t.finalDrive = 2.85; t.shiftTimeMs = 300;
        t.upshiftRpmFull = 6000; veh.massKg = 1720; veh.dragArea = 0.75; veh.wheelRadiusM = 0.34;
    }
    else if (id == "v8_flat" || id == "v10_72")
    {
        t.type = TransmissionType::DCT; t.ratios = { 3.08, 2.19, 1.63, 1.29, 1.03, 0.84, 0.69 }; t.finalDrive = 4.44; t.shiftTimeMs = 90;
        t.upshiftRpmFull = p.engine.redlineRpm * 0.97; veh.massKg = 1520; veh.dragArea = 0.62; veh.wheelRadiusM = 0.34;
    }
    else if (id == "v12_60")
    {
        t.type = TransmissionType::AMT; t.ratios = { 3.91, 2.44, 1.81, 1.46, 1.19, 0.97, 0.89 }; t.finalDrive = 3.4; t.shiftTimeMs = 250;
        t.upshiftRpmFull = 8500; veh.massKg = 1650; veh.dragArea = 0.66; veh.wheelRadiusM = 0.35;
    }
    else if (id == "v6_60")
    {
        t.type = TransmissionType::CVT; t.cvtRatioMin = 0.38; t.cvtRatioMax = 2.4; t.finalDrive = 5.5; t.cvtRpmLight = 1500; t.cvtRpmFull = 6000;
        veh.massKg = 1600;
    }
    else if (id == "single_moto" || id == "vtwin45" || id == "i4_crossplane_moto")
    {
        t.type = TransmissionType::Manual;
        t.ratios = id == "single_moto" ? std::vector<double> { 2.8, 1.9, 1.45, 1.17, 0.96 } : std::vector<double> { 2.6, 1.95, 1.6, 1.38, 1.22, 1.1 };
        t.finalDrive = id == "single_moto" ? 7.5 : 5.2; t.shiftTimeMs = 120; t.pinionTeeth = 17;
        veh.massKg = id == "single_moto" ? 190 : id == "vtwin45" ? 330 : 220; veh.dragArea = 0.45; veh.wheelRadiusM = 0.31; veh.brakeForceN = 4500;
    }
    else if (id == "i4_na" || id == "wankel2")
    {
        t.type = TransmissionType::Manual; t.ratios = { 3.6, 2.1, 1.39, 1.03, 0.82 }; t.finalDrive = 4.1;
    }
    else if (p.kind == PowertrainKind::Thermal)
    {
        t.type = TransmissionType::Manual; t.ratios = { 3.5, 2.06, 1.4, 1.03, 0.84, 0.69 }; t.finalDrive = 3.9;
    }
    finish (p);
    return p;
}
} // namespace esd
