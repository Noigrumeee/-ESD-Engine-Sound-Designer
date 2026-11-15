#include "dsp/Effects.h"
#include "model/Presets.h"
#include "model/Serialization.h"
#include "render/AudioFile.h"
#include "render/GameExport.h"
#include "render/LoopGenerator.h"
#include "render/Render.h"
#include "synth/SampleLayer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace esd;
using Catch::Approx;
namespace fs = std::filesystem;

// ------------------------------------------------------------------ phase 4: electric

TEST_CASE ("electric torque envelope: constant torque then constant power", "[electric]")
{
    const auto p = makePreset ("ev_pmsm");
    const auto& m = p.electric;
    const double base = m.baseRpm();
    CHECK (m.torqueAt (base * 0.5) == Approx (m.peakTorqueNm));
    const double rpm = base * 2.0;
    CHECK (m.torqueAt (rpm) * rpm * 2 * 3.14159265 / 60 / 1000 == Approx (m.peakPowerKw).epsilon (0.01));
    CHECK (m.torqueAt (m.maxRpm) == Approx (0.0));
}

static RenderResult dyno (const Project& p, double rpm, double load, double seconds = 1.0)
{
    RenderOptions o;
    o.seconds = seconds;
    o.preRollSeconds = 0.5;
    return renderDyno (p, rpm, load, o);
}

TEST_CASE ("PMSM: slot-passing whine and 2 fe magnetic tone are where the machine data puts them", "[electric]")
{
    auto p = makePreset ("ev_pmsm");
    const double rpm = 6000, rev = rpm / 60.0, fe = rev * p.electric.polePairs();
    auto r = dyno (p, rpm, 0.8);
    const double slot = toneMagnitude (r.mix.left, 48000, p.electric.slots * rev);
    const double twoFe = toneMagnitude (r.mix.left, 48000, 2.0 * fe);
    const double off = toneMagnitude (r.mix.left, 48000, p.electric.slots * rev * 1.13); // not a machine frequency
    CHECK (slot > 5.0 * off);
    CHECK (twoFe > 5.0 * toneMagnitude (r.mix.left, 48000, 2.0 * fe * 1.21));
}

TEST_CASE ("BLDC six-step has a strong 6 fe harmonic, FOC PMSM much weaker relative to 2 fe", "[electric]")
{
    auto ratio = [] (const char* id)
    {
        auto p = makePreset (id);
        const double rpm = 2400, fe = rpm / 60.0 * p.electric.polePairs();
        // only the electromagnetic layer
        for (auto& l : p.mix.layers) l.mute = true;
        p.mix.layers[static_cast<std::size_t> (LayerId::Electromagnetic)].mute = false;
        auto r = dyno (p, rpm, 0.8);
        return toneMagnitude (r.mix.left, 48000, 6.0 * fe) / toneMagnitude (r.mix.left, 48000, 2.0 * fe);
    };
    CHECK (ratio ("ev_bldc") > 2.0 * ratio ("ev_pmsm"));
}

TEST_CASE ("electric motor regenerates on lift-off in vehicle mode", "[electric][vehicle]")
{
    auto p = makePreset ("ev_pmsm");
    EngineRuntime rt (p, 48000);
    VoiceMix mix = mixFromProject (p.mix);
    std::vector<float> l (480), r (480);
    EngineControls c;
    c.mode = DriveMode::Vehicle;
    c.ignition = true;
    c.throttle = 1.0f;
    for (int i = 0; i < 500; ++i) rt.process (c, mix, l.data(), r.data(), 480); // 5 s
    const auto t1 = rt.telemetry();
    CHECK (t1.speedKmh > 60.0f);
    c.throttle = 0.0f;
    for (int i = 0; i < 50; ++i) rt.process (c, mix, l.data(), r.data(), 480);
    const auto t2 = rt.telemetry();
    CHECK (t2.regen);
    CHECK (t2.motorTorque < 0.0f);
    CHECK (t2.speedKmh < t1.speedKmh);
}

// ------------------------------------------------------------------ phase 5: drivetrain

static Telemetry drive (const Project& p, float throttle, double seconds, int upshiftEvery = 0)
{
    EngineRuntime rt (p, 48000);
    rt.resetEngine (true);
    VoiceMix mix = mixFromProject (p.mix);
    std::vector<float> l (480), r (480);
    EngineControls c;
    c.mode = DriveMode::Vehicle;
    c.ignition = true;
    c.throttle = throttle;
    const int blocks = static_cast<int> (seconds * 100);
    for (int i = 0; i < blocks; ++i)
    {
        if (upshiftEvery > 0 && i > 0 && i % upshiftEvery == 0) ++c.shiftUpCount;
        rt.process (c, mix, l.data(), r.data(), 480);
    }
    return rt.telemetry();
}

TEST_CASE ("automatic with torque converter launches and shifts up by itself", "[vehicle]")
{
    const auto t = drive (makePreset ("v8_cross"), 1.0f, 10.0);
    CHECK (t.speedKmh > 90.0f);
    CHECK (t.gear >= 3);
}

TEST_CASE ("manual gearbox: launch clutch, shifts on request", "[vehicle]")
{
    const auto p = makePreset ("i4_na");
    const auto first = drive (p, 1.0f, 3.0);
    CHECK (first.gear == 1);
    CHECK (first.speedKmh > 15.0f);
    const auto shifted = drive (p, 1.0f, 9.0, 250); // shift every 2.5 s
    CHECK (shifted.gear >= 3);
    CHECK (shifted.speedKmh > first.speedKmh);
}

TEST_CASE ("CVT holds the engine near its target speed at full throttle", "[vehicle]")
{
    const auto p = makePreset ("v6_60");
    const auto t = drive (p, 1.0f, 8.0);
    CHECK (t.speedKmh > 60.0f);
    CHECK (t.rpm == Approx (p.transmission.cvtRpmFull).margin (900.0));
}

TEST_CASE ("vehicle top speed is limited by aerodynamic drag", "[vehicle]")
{
    auto p = makePreset ("ev_pmsm");
    p.vehicle.dragArea = 3.0; // a brick
    const auto brick = drive (p, 1.0f, 40.0);
    // power-limited top speed (aero only): P = 0.5 rho CdA v^3
    const double vmax = std::cbrt (p.electric.peakPowerKw * 1000.0 / (0.5 * 1.2 * p.vehicle.dragArea)) * 3.6;
    CHECK (brick.speedKmh < vmax);
    CHECK (brick.speedKmh > vmax * 0.8);
    p.vehicle.dragArea = 0.5;
    CHECK (drive (p, 1.0f, 40.0).speedKmh > brick.speedKmh + 30.0f);
}

// ------------------------------------------------------------------ phase 6: hybrid

TEST_CASE ("series-parallel hybrid: EV at low demand, engine at high demand", "[hybrid]")
{
    const auto p = makePreset ("hybrid_sp");
    const auto gentle = drive (p, 0.12f, 4.0);
    CHECK_FALSE (gentle.engineOn);
    CHECK (gentle.soc < static_cast<float> (p.hybrid.socInitial));
    const auto hard = drive (p, 1.0f, 4.0);
    CHECK (hard.engineOn);
    CHECK (hard.rpm > p.engine.idleRpm);
}

TEST_CASE ("parallel plug-in hybrid drives electrically then couples the engine", "[hybrid]")
{
    auto p = makePreset ("hybrid_p2");
    const auto t = drive (p, 0.15f, 5.0); // demand below the 55 kW EV threshold
    CHECK (t.speedKmh > 10.0f);
    CHECK_FALSE (t.engineOn); // within the EV envelope
    p.hybrid.evMaxSpeedKmh = 20;
    const auto t2 = drive (p, 0.6f, 8.0);
    CHECK (t2.engineOn);
}

// ------------------------------------------------------------------ phase 3: mixer, FX, automation, samples

TEST_CASE ("automation on rpm lowers a layer at high rpm only", "[soundlab]")
{
    auto p = makePreset ("i4_na");
    for (auto& l : p.mix.layers) l.solo = false;
    p.mix.layers[static_cast<std::size_t> (LayerId::Exhaust)].solo = true;
    Automation a;
    a.layer = static_cast<int> (LayerId::Exhaust);
    a.source = ModSource::Rpm;
    a.target = ModTarget::GainDb;
    a.curve = Curve ({ { 0.0, 0.0 }, { 0.5, 0.0 }, { 0.6, -40.0 }, { 1.0, -40.0 } }, CurveInterp::Linear);
    auto withAuto = p;
    withAuto.mix.automations.push_back (a);
    const double lowA = rms (dyno (withAuto, 1500, 0.7, 0.4).mix.left), lowB = rms (dyno (p, 1500, 0.7, 0.4).mix.left);
    const double hiA = rms (dyno (withAuto, 6000, 0.7, 0.4).mix.left), hiB = rms (dyno (p, 6000, 0.7, 0.4).mix.left);
    CHECK (lowA == Approx (lowB).epsilon (0.05));
    CHECK (hiA < hiB * 0.05);
}

TEST_CASE ("FX chain: low-pass removes the highs, saturation adds harmonics", "[soundlab]")
{
    auto p = makePreset ("i4_na");
    auto lp = p;
    for (auto& f : lp.mix.fx) f.lowpassHz = 300.0f;
    const auto a = dyno (p, 4000, 0.8, 0.5), b = dyno (lp, 4000, 0.8, 0.5);
    const double firing = 4000.0 / 60.0 * 2.0;
    double hiA = 0, hiB = 0, loA = 0, loB = 0;
    for (int k = 10; k <= 20; ++k) { hiA += toneMagnitude (a.mix.left, 48000, firing * k); hiB += toneMagnitude (b.mix.left, 48000, firing * k); }
    loA = toneMagnitude (a.mix.left, 48000, firing);
    loB = toneMagnitude (b.mix.left, 48000, firing);
    CHECK (hiB < 0.1 * hiA);       // harmonics above 1.3 kHz removed
    CHECK (loB > 0.5 * loA);       // fundamental kept
}

TEST_CASE ("convolver with a unit impulse delays by one block and keeps the signal", "[dsp]")
{
    dsp::Convolver cv;
    std::vector<float> ir (300, 0.0f);
    ir[0] = 1.0f;
    cv.prepare (ir, 48000.0);
    std::vector<float> out;
    for (int i = 0; i < 2048; ++i)
    {
        float l = std::sin (i * 0.05f), r = l;
        cv.process (l, r);
        out.push_back (l);
    }
    for (int i = 600; i < 2000; ++i)
        CHECK (out[static_cast<std::size_t> (i)] == Approx (std::sin ((i - dsp::Convolver::kBlock) * 0.05f)).margin (1e-3));
}

TEST_CASE ("sample grid weights: equal power in rpm, linear in load", "[samples]")
{
    const std::vector<std::pair<double, double>> pts { { 1000, 0.0 }, { 3000, 0.0 }, { 1000, 1.0 }, { 3000, 1.0 } };
    auto w = gridWeights (pts, 2000, 0.5);
    double sumSq = 0.0;
    for (auto& g : w) sumSq += g.weight * g.weight;
    REQUIRE (w.size() == 4);
    CHECK (sumSq == Approx (0.5).margin (1e-6)); // (cos^2 + sin^2) x (0.5^2 + 0.5^2)
    auto edge = gridWeights (pts, 500, 1.0);
    REQUIRE (edge.size() == 1);
    CHECK (edge[0].index == 2);
}

TEST_CASE ("WAV write / read round trip", "[render]")
{
    const auto dir = fs::temp_directory_path() / "esd_wav";
    fs::create_directories (dir);
    StereoBuffer b;
    for (int i = 0; i < 1000; ++i) { b.left.push_back (std::sin (i * 0.1f) * 0.5f); b.right.push_back (-b.left.back()); }
    for (auto fmt : { WavFormat::Pcm16, WavFormat::Pcm24, WavFormat::Float32 })
    {
        const auto path = (dir / "t.wav").string();
        REQUIRE (writeWav (path, b, fmt));
        AudioData a;
        REQUIRE (readWav (path, a));
        REQUIRE (a.channels.size() == 2);
        REQUIRE (a.frames() == 1000);
        CHECK (a.channels[0][123] == Approx (b.left[123]).margin (1e-3));
        CHECK (a.channels[1][456] == Approx (b.right[456]).margin (1e-3));
    }
}

TEST_CASE ("project with every new section survives a JSON round trip", "[json]")
{
    for (const char* id : { "ev_bldc", "hybrid_p2" })
    {
        auto p = makePreset (id);
        Automation a;
        a.layer = static_cast<int> (LayerId::Whine);
        a.source = ModSource::Torque;
        a.target = ModTarget::LowpassHz;
        a.curve = Curve ({ { 0, 0.3 }, { 1, 1.0 } });
        p.mix.automations.push_back (a);
        p.mix.fx[3].drive = 0.4f;
        p.mix.fx[3].irPath = "C:/irs/hall.wav";
        auto r = projectFromJson (projectToJson (p));
        REQUIRE (r.ok);
        CHECK (r.project == p);
    }
}

// ------------------------------------------------------------------ phase 7 / 8: loops and export

static Project smallLoopProject (const char* id)
{
    auto p = makePreset (id);
    p.loops.rpmPoints = 3;
    p.loops.loadPoints = 2;
    p.loops.loopSeconds = 0.4;
    p.loops.threads = 3;
    return p;
}

TEST_CASE ("loop generator: whole cycles, seamless seams, profile", "[loops]")
{
    const auto dir = fs::temp_directory_path() / "esd_loops";
    fs::remove_all (dir);
    const auto p = smallLoopProject ("v8_cross");
    LoopJobOptions o;
    o.outDir = dir.string();
    o.resume = false;
    const auto r = generateLoops (p, o);
    REQUIRE (r.ok);
    // 3 rpm x 2 loads + 2 overrun (no overrun at idle)
    CHECK (r.plan.points.size() == 8);
    for (auto& pt : r.plan.points)
    {
        INFO (pt.file);
        AudioData a;
        REQUIRE (readWav ((dir / pt.file).string(), a));
        CHECK (static_cast<int> (a.frames()) == pt.loopSamples);
        // the rendered rpm makes the loop hold exactly 'cycles' engine cycles
        const double cyclesInLoop = pt.loopSamples / 48000.0 * pt.rpmExact / 60.0 / 2.0;
        CHECK (cyclesInLoop == Approx (pt.cycles).margin (1e-6));
        CHECK (pt.seamJumpDb < 12.0);
    }
    std::ifstream f (r.profilePath);
    const auto j = nlohmann::json::parse (f);
    CHECK (j["format"] == "EngineSoundDesigner.runtime_profile");
    CHECK (j["sets"]["engine"]["rows"].size() == 3);
    CHECK (j["one_shots"].contains ("start"));
    // resume keeps existing files
    LoopJobOptions again = o;
    again.resume = true;
    const auto r2 = generateLoops (p, again);
    REQUIRE (r2.ok);
    for (auto& pt : r2.plan.points) CHECK (pt.reused);
}

TEST_CASE ("electric and hybrid loop plans", "[loops]")
{
    const auto e = planLoops (smallLoopProject ("ev_pmsm"));
    bool regen = false;
    for (auto& pt : e.points) regen = regen || pt.state == "regen";
    CHECK (regen);
    const auto h = planLoops (smallLoopProject ("hybrid_sp"));
    bool engine = false, motor = false;
    for (auto& pt : h.points) { engine = engine || pt.set == "engine"; motor = motor || pt.set == "motor"; }
    CHECK ((engine && motor));
    CHECK (h.estimatedBytes > 0);
}

TEST_CASE ("Godot package export", "[export]")
{
    const auto tpl = findTemplatesDir (fs::current_path().string());
    const auto tpl2 = tpl.empty() ? findTemplatesDir (ESD_SOURCE_DIR) : tpl;
    REQUIRE_FALSE (tpl2.empty());
    const auto dir = fs::temp_directory_path() / "esd_export";
    fs::remove_all (dir);
    LoopJobOptions o;
    const auto r = exportGamePackage (smallLoopProject ("i4_na"), GameTarget::Godot, dir.string(), tpl2, o);
    REQUIRE (r.ok);
    CHECK (fs::exists (fs::path (r.folder) / "addons" / "engine_sound" / "engine_sound_player.gd"));
    CHECK (fs::exists (fs::path (r.folder) / "project.godot"));
    CHECK (fs::exists (fs::path (r.soundsDir) / "engine_profile.json"));
}
