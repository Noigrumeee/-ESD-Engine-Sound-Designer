#include "model/Presets.h"
#include "render/Render.h"
#include "sim/EngineSim.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace esd;
using Catch::Approx;

static void run (EngineSim& sim, const EngineControls& c, double seconds)
{
    const double dt = 32.0 / 48000.0;
    for (double t = 0; t < seconds; t += dt) sim.step (c, dt);
}

TEST_CASE ("idle controller holds the idle speed", "[sim]")
{
    for (const char* id : { "i4_na", "v8_cross", "single_moto", "wankel2" })
    {
        INFO (id);
        Project p = makePreset (id);
        EngineSim sim;
        sim.configure (p);
        sim.reset (true);
        EngineControls c;
        c.ignition = true;
        run (sim, c, 4.0);
        CHECK (sim.state().rpm == Approx (p.engine.idleRpm).margin (80.0));
    }
}

TEST_CASE ("starter cranks and the engine catches", "[sim]")
{
    Project p = makePreset ("i4_na");
    EngineSim sim;
    sim.configure (p);
    sim.reset (false);
    EngineControls c;
    c.ignition = true;
    c.starter = true;
    run (sim, c, 1.0);
    CHECK (sim.state().combustion);
    c.starter = false;
    run (sim, c, 3.0);
    CHECK (sim.state().rpm == Approx (p.engine.idleRpm).margin (100.0));
    c.ignition = false;
    run (sim, c, 6.0);
    CHECK (sim.state().rpm < 5.0); // switched off: spins down
}

TEST_CASE ("full throttle reaches the limiter and the limiter holds", "[sim]")
{
    Project p = makePreset ("i4_na");
    EngineSim sim;
    sim.configure (p);
    sim.reset (true);
    EngineControls c;
    c.ignition = true;
    c.throttle = 1.0f;
    double maxRpm = 0;
    bool sawCut = false;
    const double dt = 32.0 / 48000.0;
    for (double t = 0; t < 4.0; t += dt)
    {
        sim.step (c, dt);
        maxRpm = std::max (maxRpm, sim.state().rpm);
        sawCut = sawCut || sim.state().limiterActive;
    }
    CHECK (sawCut);
    CHECK (maxRpm < p.engine.limiterRpm + 300.0);
    CHECK (maxRpm > p.engine.limiterRpm - 10.0);
}

TEST_CASE ("steady wide-open-throttle torque equals the torque curve", "[sim]")
{
    Project p = makePreset ("v8_cross");
    EngineSim sim;
    sim.configure (p);
    sim.reset (true);
    EngineControls c;
    c.mode = DriveMode::Dyno;
    c.dynoRpm = 4000;
    c.dynoLoad = 1.0f;
    c.dynoRampRpmPerSec = 1e7f;
    run (sim, c, 2.0);
    CHECK (sim.state().brakeTorque == Approx (p.engine.torqueCurve.eval (4000)).epsilon (0.02));
    CHECK (sim.state().powerKw == Approx (sim.state().brakeTorque * 4000 * 2 * 3.14159265 / 60 / 1000).epsilon (0.01));
}

TEST_CASE ("turbo boost builds with lag", "[sim]")
{
    Project p = makePreset ("i4_turbo");
    EngineSim sim;
    sim.configure (p);
    sim.reset (true);
    EngineControls c;
    c.mode = DriveMode::Dyno;
    c.dynoRpm = 4500;
    c.dynoLoad = 1.0f;
    c.dynoRampRpmPerSec = 1e7f;
    run (sim, c, 0.15);
    const float early = sim.state().boostBar;
    run (sim, c, 3.0);
    const float late = sim.state().boostBar;
    CHECK (late > early + 0.3f);
    CHECK (late == Approx (p.boost.maxBoostBar).margin (0.15));
}

// ---------------------------------------------------------------- audio

static RenderResult dyno (const char* id, double rpm, double load, double seconds = 1.0)
{
    RenderOptions o;
    o.seconds = seconds;
    o.preRollSeconds = 1.0;
    return renderDyno (makePreset (id), rpm, load, o);
}

TEST_CASE ("rendering is deterministic and finite", "[audio]")
{
    auto a = dyno ("v8_cross", 2500, 0.6, 0.5);
    auto b = dyno ("v8_cross", 2500, 0.6, 0.5);
    REQUIRE (a.mix.left.size() == b.mix.left.size());
    CHECK (a.mix.left == b.mix.left);
    CHECK (a.mix.right == b.mix.right);
    for (float v : a.mix.left)
    {
        REQUIRE (std::isfinite (v));
        REQUIRE (std::abs (v) <= 1.0f);
    }
    CHECK (rms (a.mix.left) > 1e-3);
}

TEST_CASE ("every preset renders sound at idle, mid and high rpm", "[audio]")
{
    for (auto& info : builtinPresets())
    {
        INFO (info.id);
        const Project p = makePreset (info.id);
        for (double frac : { 0.15, 0.5, 0.95 })
        {
            RenderOptions o;
            o.seconds = 0.3;
            o.preRollSeconds = 0.5;
            const auto r = renderDyno (p, std::max (p.engine.idleRpm, p.engine.redlineRpm * frac), 0.6, o);
            const double level = rms (r.mix.left) + rms (r.mix.right);
            CHECK (level > 3e-3);
            CHECK (level < 1.2);
        }
    }
}

TEST_CASE ("inline 4: the 2nd engine order (firing frequency) dominates the low orders", "[audio]")
{
    const double rpm = 3000, rev = rpm / 60.0;
    auto r = dyno ("i4_na", rpm, 0.7, 1.0);
    const double o2 = toneMagnitude (r.mix.left, 48000, 2 * rev);
    CHECK (o2 > 3.0 * toneMagnitude (r.mix.left, 48000, 0.5 * rev));
    CHECK (o2 > 3.0 * toneMagnitude (r.mix.left, 48000, 1.0 * rev));
    CHECK (o2 > 3.0 * toneMagnitude (r.mix.left, 48000, 1.5 * rev));
}

TEST_CASE ("cross-plane vs flat-plane V8: per-bank uneven firing shows in each exhaust line", "[audio]")
{
    // Same rpm, both on true dual exhausts. Each bank of a cross-plane V8 fires unevenly ->
    // strong half orders (2.5, 3.5: the burble) on each side; a flat-plane bank fires every
    // 180 deg -> its energy concentrates at orders 2, 4, 6... and the half orders vanish.
    const double rpm = 3000, rev = rpm / 60.0;
    auto halfOrderRatio = [&] (const char* id)
    {
        auto r = dyno (id, rpm, 0.7, 1.0);
        double half = 0;
        for (double ord : { 2.5, 3.5 }) half += toneMagnitude (r.mix.left, 48000, ord * rev);
        const double even = toneMagnitude (r.mix.left, 48000, 2.0 * rev) + toneMagnitude (r.mix.left, 48000, 4.0 * rev);
        return half / even;
    };
    const double cross = halfOrderRatio ("v8_cross");
    const double flat = halfOrderRatio ("v8_flat");
    INFO ("cross " << cross << " flat " << flat);
    CHECK (cross > 0.05);
    CHECK (cross > 20.0 * flat);
}

TEST_CASE ("louder at high load than at low load", "[audio]")
{
    auto lo = dyno ("i6_na", 3500, 0.15, 0.5);
    auto hi = dyno ("i6_na", 3500, 0.95, 0.5);
    CHECK (rms (hi.mix.left) > 1.5 * rms (lo.mix.left));
}

TEST_CASE ("muting layers removes them", "[audio]")
{
    Project p = makePreset ("i4_turbo");
    for (auto& l : p.mix.layers) l.mute = true;
    RenderOptions o;
    o.seconds = 0.3;
    o.preRollSeconds = 0.3;
    auto r = renderDyno (p, 3000, 0.5, o);
    CHECK (rms (r.mix.left) < 1e-6);
}

TEST_CASE ("96 kHz rendering gives the same pitch content", "[audio]")
{
    Project p = makePreset ("i4_na");
    RenderOptions o;
    o.seconds = 1.0;
    o.preRollSeconds = 1.0;
    o.sampleRate = 96000;
    auto r = renderDyno (p, 3000, 0.7, o);
    const double rev = 50.0;
    CHECK (toneMagnitude (r.mix.left, 96000, 2 * rev) > 3.0 * toneMagnitude (r.mix.left, 96000, 1.0 * rev));
}
