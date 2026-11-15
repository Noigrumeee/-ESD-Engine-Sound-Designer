#include "Project.h"
#include "../base/Math.h"
#include "Params.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace esd
{
const char* toString (PowertrainKind k)
{
    switch (k)
    {
        case PowertrainKind::Thermal: return "thermal";
        case PowertrainKind::Electric: return "electric";
        case PowertrainKind::Hybrid: return "hybrid";
    }
    return "thermal";
}


double ThermalEngineSpec::cylinderDisplacementCc() const
{
    const double b = boreMm * 0.1, s = strokeMm * 0.1; // cm
    return kPi * 0.25 * b * b * s;
}

double ThermalEngineSpec::displacementCc() const
{
    const auto* l = findLayout (layoutId);
    // A Wankel "displacement" is quoted per chamber x rotors; we use the same swept volume
    // formula on equivalent bore/stroke so the user can still dial a cc value.
    return cylinderDisplacementCc() * (l ? l->cylinders : 4);
}

void ThermalEngineSpec::setDisplacementCc (double cc)
{
    const double cur = displacementCc();
    if (cur <= 0.0 || cc <= 0.0) return;
    const double k = std::cbrt (cc / cur);
    boreMm *= k;
    strokeMm *= k;
}

Curve defaultThrottleMap()
{
    return Curve ({ { 0.0, 0.0 }, { 0.1, 0.05 }, { 0.25, 0.18 }, { 0.5, 0.45 }, { 0.75, 0.78 }, { 1.0, 1.0 } }, CurveInterp::Smooth);
}

Curve estimateTorqueCurve (const ThermalEngineSpec& e, const BoostSpec& b)
{
    const auto* l = findLayout (e.layoutId);
    const CycleType cycle = l ? l->cycle : CycleType::FourStroke;
    const double vdM3 = e.displacementCc() * 1e-6;
    const bool boosted = b.type != BoostType::None;
    const bool diesel = e.fuel == FuelType::Diesel;

    double bmepBar = diesel ? 9.0 : 12.0 + 0.3 * (e.compressionRatio - 10.0);
    if (cycle == CycleType::TwoStroke) bmepBar = 6.5;
    if (cycle == CycleType::Wankel) bmepBar = 10.5;
    if (boosted) bmepBar *= (1.0 + b.maxBoostBar) * (diesel ? 0.9 : 0.82);
    const double revsPerCycle = cycle == CycleType::FourStroke ? 2.0 : 1.0;
    const double peak = bmepBar * 1e5 * vdM3 / (kTwoPi * revsPerCycle);

    const double idle = e.idleRpm, red = std::max (e.redlineRpm, idle + 1000.0), lim = std::max (e.limiterRpm, red + 1.0);
    std::vector<CurvePoint> pts;
    const bool turbo = b.type == BoostType::Turbo || b.type == BoostType::TwinTurbo || b.type == BoostType::Twincharged;
    if (turbo)
    {
        const double spool = clampv (b.spoolRpm, idle + 400.0, red * 0.75);
        pts = { { idle * 0.5, 0.4 }, { idle, 0.45 }, { idle + (spool - idle) * 0.5, 0.68 }, { spool, 1.0 },
                { std::max (spool + 100.0, red * 0.75), 1.0 }, { red, 0.8 }, { lim, 0.74 } };
    }
    else if (diesel)
        pts = { { idle * 0.5, 0.55 }, { idle, 0.7 }, { red * 0.35, 0.92 }, { red * 0.45, 1.0 }, { red * 0.7, 0.9 }, { red, 0.7 }, { lim, 0.6 } };
    else
        pts = { { idle * 0.5, 0.55 }, { idle, 0.65 }, { red * 0.3, 0.82 }, { red * 0.5, 0.95 }, { red * 0.65, 1.0 }, { red * 0.85, 0.93 }, { red, 0.82 }, { lim, 0.75 } };

    // keep strictly increasing x
    std::vector<CurvePoint> clean;
    for (auto& p : pts)
        if (clean.empty() || p.x > clean.back().x + 1.0)
            clean.push_back ({ std::round (p.x), std::round (p.y * peak * 10.0) / 10.0 });
    return Curve (clean, CurveInterp::Smooth);
}

std::string newUuid()
{
    std::random_device rd;
    std::mt19937_64 g ((static_cast<uint64_t> (rd()) << 32) ^ rd());
    const uint64_t a = g(), b = g();
    char buf[40];
    std::snprintf (buf, sizeof (buf), "%08x-%04x-4%03x-%04x-%012llx", static_cast<unsigned> (a >> 32), static_cast<unsigned> ((a >> 16) & 0xffff),
                   static_cast<unsigned> (a & 0xfff), static_cast<unsigned> (((b >> 48) & 0x3fff) | 0x8000),
                   static_cast<unsigned long long> (b & 0xffffffffffffull));
    return buf;
}

std::vector<ValidationIssue> validate (const Project& p)
{
    std::vector<ValidationIssue> issues;
    const auto& e = p.engine;
    const auto* l = findLayout (e.layoutId);
    if (! l)
    {
        issues.push_back ({ Severity::Error, "engine.layout", "Unknown layout '" + e.layoutId + "'." });
        return issues;
    }
    const auto* crank = findCrank (*l, e.crankId);
    if (crank)
    {
        const auto plan = computeFiringPlan (*l, *crank, e.bankAngleDeg, e.firingOrder);
        if (! plan.valid)
            issues.push_back ({ Severity::Error, "engine.firingOrder", plan.error });
    }
    if (e.limiterRpm < e.redlineRpm)
        issues.push_back ({ Severity::Warning, "engine.limiter", "The rev limiter is below the redline." });
    const double ps = e.meanPistonSpeed (e.limiterRpm);
    if (ps > 26.0)
        issues.push_back ({ Severity::Warning, "engine.stroke",
                            "Mean piston speed at the limiter is " + std::to_string (static_cast<int> (ps)) + " m/s (> 25 m/s is beyond racing engines)." });
    const double cylCc = e.cylinderDisplacementCc();
    if (cylCc > 1500.0)
        issues.push_back ({ Severity::Warning, "engine.bore", "More than 1.5 L per cylinder: unusual outside marine/industrial engines." });
    if (e.fuel == FuelType::Diesel && e.compressionRatio < 14.0)
        issues.push_back ({ Severity::Warning, "engine.compression", "A diesel needs a compression ratio of about 14:1 or more to self-ignite." });
    if (e.fuel != FuelType::Diesel && e.compressionRatio > 14.5 && p.boost.type == BoostType::None)
        issues.push_back ({ Severity::Info, "engine.compression", "Very high compression for a spark ignition engine (knock limited in reality)." });
    if (p.boost.type != BoostType::None && e.fuel != FuelType::Diesel && e.compressionRatio > 11.5)
        issues.push_back ({ Severity::Info, "engine.compression", "Boosted gasoline engines usually run 8.5-10.5:1." });
    if (p.exhaust.outlets > 1 && p.exhaust.routing == ExhaustRouting::Single && p.exhaust.outletPosition == OutletPosition::Split)
        issues.push_back ({ Severity::Info, "exhaust.outletPosition", "Split outlets on a single line: the line is split after the muffler." });
    if (e.torqueCurve.points().size() < 2)
        issues.push_back ({ Severity::Error, "engine.torqueCurve", "The torque curve needs at least two points." });
    const auto& t = p.transmission;
    const bool ev = t.type == TransmissionType::EVFixed;
    if (! ev && t.type != TransmissionType::CVT && t.ratios.empty())
        issues.push_back ({ Severity::Error, "transmission.ratios", "The gearbox needs at least one ratio." });
    for (std::size_t i = 1; i < t.ratios.size(); ++i)
        if (t.ratios[i] >= t.ratios[i - 1])
            issues.push_back ({ Severity::Warning, "transmission.ratios", "Gear ratios should decrease from 1st to top gear." });
    if (p.kind != PowertrainKind::Thermal)
    {
        const auto& m = p.electric;
        const double ef = m.maxRpm / 60.0 * m.polePairs();
        if (m.pwmHz < 6.0 * ef)
            issues.push_back ({ Severity::Warning, "electric.pwm", "PWM frequency below 6x the max electrical frequency: very coarse current waveform." });
        if (m.peakPowerKw * 1000.0 > m.voltage * m.maxCurrentA * 1.73)
            issues.push_back ({ Severity::Warning, "electric.power", "Peak power above what voltage x current can deliver (V x I x sqrt 3)." });
    }
    if (p.kind == PowertrainKind::Hybrid && p.hybrid.socMin >= p.hybrid.socMax)
        issues.push_back ({ Severity::Error, "hybrid.soc", "Minimum state of charge must be below the maximum." });
    return issues;
}

std::vector<ValidationIssue> sanitize (Project& p)
{
    std::vector<ValidationIssue> fixes;
    auto& e = p.engine;
    const auto* l = findLayout (e.layoutId);
    if (! l)
    {
        fixes.push_back ({ Severity::Error, "engine.layout", "Unknown layout '" + e.layoutId + "', reset to Inline 4." });
        e.layoutId = "i4";
        l = findLayout (e.layoutId);
    }
    const auto* crank = findCrank (*l, e.crankId);
    if (crank && crank->id != e.crankId)
    {
        fixes.push_back ({ Severity::Warning, "engine.crank", "Unknown crank '" + e.crankId + "', using " + crank->name + "." });
        e.crankId = crank->id;
    }

    for (auto& def : paramRegistry())
    {
        if (def.id == "engine.displacement" || ! def.get || ! def.set) continue;
        if (def.id == "engine.bankAngle" && ! l->bankAngleEditable) continue;
        const double v = def.get (p);
        double c = v;
        if (! std::isfinite (c)) c = def.min;
        if (def.id == "engine.bankAngle" && c > 0.0) c = clampv (c, l->bankAngleMin, l->bankAngleMax);
        else if (def.id != "engine.bankAngle") c = clampv (c, def.min, def.max);
        if (c != v)
        {
            def.set (p, c);
            fixes.push_back ({ Severity::Warning, def.id, def.label + " clamped to its valid range." });
        }
    }

    if (e.idleRpm > e.redlineRpm - 500.0) { e.idleRpm = std::max (300.0, e.redlineRpm - 500.0); fixes.push_back ({ Severity::Warning, "engine.idle", "Idle must be at least 500 rpm below the redline." }); }
    if (e.limiterRpm < e.idleRpm + 500.0) { e.limiterRpm = e.redlineRpm; fixes.push_back ({ Severity::Warning, "engine.limiter", "Limiter moved to the redline." }); }
    if (p.boost.whistleMaxHz < p.boost.whistleMinHz) std::swap (p.boost.whistleMaxHz, p.boost.whistleMinHz);

    if (crank)
    {
        const auto plan = computeFiringPlan (*l, *crank, e.bankAngleDeg, e.firingOrder);
        if (! plan.valid)
        {
            fixes.push_back ({ Severity::Error, "engine.firingOrder", plan.error + " Reset to " + firingOrderToString (crank->firingOrder) + "." });
            e.firingOrder.clear();
        }
    }
    if (e.torqueCurve.points().size() < 2)
    {
        e.torqueCurve = estimateTorqueCurve (e, p.boost);
        fixes.push_back ({ Severity::Info, "engine.torqueCurve", "Torque curve estimated from displacement." });
    }
    if (e.throttleMap.points().size() < 2)
        e.throttleMap = defaultThrottleMap();
    auto& t = p.transmission;
    for (auto& r : t.ratios) r = clampv (r, 0.2, 8.0);
    if (t.ratios.size() > 10) t.ratios.resize (10);
    if (t.ratios.empty() && t.type != TransmissionType::EVFixed && t.type != TransmissionType::CVT)
    {
        t.ratios = { 3.6, 2.1, 1.4, 1.0, 0.8 };
        fixes.push_back ({ Severity::Warning, "transmission.ratios", "Empty gearbox: default ratios restored." });
    }
    if (p.hybrid.socMin >= p.hybrid.socMax) { p.hybrid.socMin = 0.3; p.hybrid.socMax = 0.8; }
    p.hybrid.socInitial = clampv (p.hybrid.socInitial, 0.0, 1.0);
    for (auto& a : p.mix.automations)
    {
        a.layer = clampv (a.layer, 0, kNumLayers - 1);
        if (a.curve.points().size() < 2) a.curve = Curve ({ { 0.0, 0.0 }, { 1.0, 0.0 } }, CurveInterp::Smooth);
    }
    if (p.id.empty()) p.id = newUuid();
    return fixes;
}
} // namespace esd
