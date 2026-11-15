#include "LoopGenerator.h"
#include "../base/Math.h"
#include "../synth/TransmissionVoice.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>

using nlohmann::json;

namespace esd
{
namespace fs = std::filesystem;

namespace
{
fs::path u8p (const std::string& s) { return fs::path (reinterpret_cast<const char8_t*> (s.c_str())); }
std::string str (const fs::path& p)
{
    const auto u = p.u8string();
    return std::string (u.begin(), u.end());
}

std::vector<double> linspace (double a, double b, int n)
{
    std::vector<double> v;
    if (n <= 1) { v.push_back (b); return v; }
    for (int i = 0; i < n; ++i) v.push_back (a + (b - a) * i / (n - 1));
    return v;
}

double cycleDegFor (const Project& p, const std::string& set)
{
    if (set == "motor") return 360.0;
    const auto* l = findLayout (p.engine.layoutId);
    return l ? cycleDegrees (l->cycle) : 720.0;
}

std::string fileName (const LoopPoint& pt)
{
    std::ostringstream s;
    s << pt.set << "/" << pt.state << "_r" << static_cast<int> (std::lround (pt.rpm)) << "_l" << static_cast<int> (std::lround (pt.load * 100.0));
    return s.str();
}

Project projectForSet (const Project& p, const std::string& set)
{
    Project q = p;
    if (p.kind == PowertrainKind::Hybrid) q.kind = set == "motor" ? PowertrainKind::Electric : PowertrainKind::Thermal;
    return q;
}

WavFormat wavFormat (int bits) { return bits <= 16 ? WavFormat::Pcm16 : bits >= 32 ? WavFormat::Float32 : WavFormat::Pcm24; }

/// Seam discontinuity relative to the typical step between consecutive samples.
double seamJumpDb (const StereoBuffer& b)
{
    const std::size_t n = b.frames();
    if (n < 4) return 0.0;
    double acc = 0.0;
    for (std::size_t i = 1; i < n; ++i)
    {
        const double d = b.left[i] - b.left[i - 1];
        acc += d * d;
    }
    const double typical = std::sqrt (acc / static_cast<double> (n - 1));
    const double seam = std::abs (b.left[0] - b.left[n - 1]);
    return 20.0 * std::log10 (std::max (1e-12, seam) / std::max (1e-12, typical));
}

/// Builds the loop: the head is crossfaded (equal power) with the samples that follow the tail.
StereoBuffer makeLoop (const StereoBuffer& src, int loopLen, int xfade)
{
    StereoBuffer out;
    out.sampleRate = src.sampleRate;
    const auto L = static_cast<std::size_t> (loopLen);
    out.left.assign (src.left.begin(), src.left.begin() + static_cast<std::ptrdiff_t> (L));
    out.right.assign (src.right.begin(), src.right.begin() + static_cast<std::ptrdiff_t> (L));
    const int x = std::min (xfade, static_cast<int> (src.frames()) - loopLen);
    for (int i = 0; i < x; ++i)
    {
        const double t = (i + 0.5) / x;
        const float wIn = static_cast<float> (std::sin (t * kPi * 0.5)), wOut = static_cast<float> (std::cos (t * kPi * 0.5));
        const auto a = static_cast<std::size_t> (i), b = L + static_cast<std::size_t> (i);
        out.left[a] = src.left[a] * wIn + src.left[b] * wOut;
        out.right[a] = src.right[a] * wIn + src.right[b] * wOut;
    }
    return out;
}

void applyGain (StereoBuffer& b, float g)
{
    for (auto& v : b.left) v = clampv (v * g, -1.0f, 1.0f);
    for (auto& v : b.right) v = clampv (v * g, -1.0f, 1.0f);
}
} // namespace

AudioWriter defaultAudioWriter()
{
    return [] (const std::string& pathNoExt, const StereoBuffer& b, const LoopSpec& spec, std::string& written, std::string* error)
    {
        written = pathNoExt + ".wav";
        return writeWav (written, b, wavFormat (spec.bitDepth), false, error);
    };
}

LoopPlan planLoops (const Project& p)
{
    LoopPlan plan;
    const auto& ls = p.loops;
    std::vector<std::string> sets;
    if (p.kind != PowertrainKind::Electric) sets.push_back ("engine");
    if (p.kind != PowertrainKind::Thermal) sets.push_back ("motor");
    const double sr = ls.sampleRate;

    for (const auto& set : sets)
    {
        const bool motor = set == "motor";
        const double lo = ls.rpmMin > 0.0 ? ls.rpmMin : (motor ? 300.0 : p.engine.idleRpm);
        const double hi = ls.rpmMax > 0.0 ? ls.rpmMax : (motor ? p.electric.maxRpm * 0.98 : p.engine.limiterRpm);
        const auto rpms = linspace (lo, std::max (lo + 1.0, hi), std::max (2, ls.rpmPoints));
        std::vector<std::pair<std::string, double>> rows;
        for (double l : linspace (motor ? 0.05 : 0.1, 1.0, std::max (1, ls.loadPoints))) rows.emplace_back ("on_load", l);
        if (! motor && ls.overrunState) rows.emplace_back ("overrun", 0.0);
        if (motor && ls.regenState)
            for (double l : linspace (0.3, 1.0, std::max (1, (ls.loadPoints + 1) / 2))) rows.emplace_back ("regen", -l);
        const double cyc = cycleDegFor (p, set);
        for (auto& [state, load] : rows)
            for (std::size_t i = 0; i < rpms.size(); ++i)
            {
                const double rpm = rpms[i];
                if (state == "overrun" && i == 0) continue; // no fuel cut at idle
                LoopPoint pt;
                pt.set = set;
                pt.state = state;
                pt.rpm = std::round (rpm);
                pt.load = load;
                const double tcyc = 60.0 / pt.rpm * cyc / 360.0;
                pt.cycles = std::max (1, static_cast<int> (std::lround (ls.loopSeconds / tcyc)));
                pt.loopSamples = static_cast<int> (std::lround (pt.cycles * tcyc * sr));
                pt.rpmExact = pt.cycles * (cyc / 360.0) * 60.0 * sr / pt.loopSamples;
                pt.file = fileName (pt);
                plan.points.push_back (pt);
                plan.totalSeconds += pt.loopSamples / sr;
            }
    }
    int layersUsed = 0;
    for (int l = 0; l < kNumLayers; ++l)
        if (layerUsedBy (static_cast<LayerId> (l), static_cast<int> (p.kind)) && l != static_cast<int> (LayerId::Samples)) ++layersUsed;
    const double bytesPerSec = sr * 2.0 * (ls.bitDepth / 8.0) * (ls.flac ? 0.55 : 1.0);
    plan.estimatedBytes = static_cast<std::size_t> (plan.totalSeconds * bytesPerSec * (1.0 + (ls.stems ? layersUsed : 0)));
    return plan;
}

LoopSetResult generateLoops (const Project& p, const LoopJobOptions& opt)
{
    const auto t0 = std::chrono::steady_clock::now();
    LoopSetResult res;
    res.plan = planLoops (p);
    const auto& ls = p.loops;
    const double sr = ls.sampleRate;
    const auto writer = opt.writer ? opt.writer : defaultAudioWriter();
    std::error_code ec;
    fs::create_directories (u8p (opt.outDir), ec);
    for (const char* sub : { "engine", "motor", "oneshots" })
        fs::create_directories (u8p (opt.outDir) / sub, ec);
    if (ec) { res.error = "Cannot create " + opt.outDir; return res; }

    std::mutex progressLock;
    auto report = [&] (float f, const std::string& m)
    {
        if (! opt.progress) return;
        const std::lock_guard<std::mutex> g (progressLock);
        opt.progress (f, m);
    };

    // ---- normalisation probe: the loudest corners of each set (short renders)
    float gain = 1.0f;
    if (ls.normalize)
    {
        float peak = 1e-6f;
        double maxRpm = 0.0;
        for (const auto& pt : res.plan.points) maxRpm = std::max (maxRpm, pt.rpm);
        for (const auto& pt : res.plan.points)
        {
            const bool corner = pt.rpm >= 0.55 * maxRpm && (pt.load >= 0.99 || pt.load <= -0.99);
            if (! corner) continue;
            RenderOptions o;
            o.sampleRate = sr;
            o.seconds = 0.4;
            o.preRollSeconds = 0.8;
            o.loaders = opt.loaders;
            auto r = renderDyno (projectForSet (p, pt.set), pt.rpmExact, pt.load, o, pt.state == "overrun");
            for (float v : r.mix.left) peak = std::max (peak, std::abs (v));
            for (float v : r.mix.right) peak = std::max (peak, std::abs (v));
        }
        gain = clampv (0.89f / peak, 0.05f, 4.0f);
    }
    res.normalizeGainDb = gainToDb (gain);

    // ---- loops (thread pool)
    std::atomic<std::size_t> next { 0 }, done { 0 };
    std::atomic<bool> failed { false };
    std::string firstError;
    std::mutex errLock;
    const int threads = ls.threads > 0 ? ls.threads : std::max (1, static_cast<int> (std::thread::hardware_concurrency()));
    auto worker = [&]
    {
        for (;;)
        {
            if (opt.cancel && opt.cancel->load()) return;
            const std::size_t i = next++;
            if (i >= res.plan.points.size()) return;
            auto& pt = res.plan.points[i];
            const auto base = str (u8p (opt.outDir) / u8p (pt.file));
            bool exists = false;
            for (const char* ext : { ".wav", ".flac" })
                if (fs::exists (u8p (base + ext), ec)) { exists = true; pt.file += ext; }
            if (opt.resume && exists)
            {
                pt.reused = true;
                report (static_cast<float> (++done) / res.plan.points.size(), "kept " + pt.file);
                continue;
            }
            const int xfade = static_cast<int> (ls.crossfadeMs * 0.001 * sr);
            RenderOptions o;
            o.sampleRate = sr;
            o.seconds = (pt.loopSamples + xfade + 2) / sr;
            o.preRollSeconds = 1.0;
            o.alignToCycle = true;
            o.renderLayers = ls.stems;
            o.cancel = opt.cancel;
            o.loaders = opt.loaders;
            auto r = renderDyno (projectForSet (p, pt.set), pt.rpmExact, pt.load, o, pt.state == "overrun");
            if (r.cancelled) return;
            auto loop = makeLoop (r.mix, pt.loopSamples, xfade);
            applyGain (loop, gain);
            pt.seamJumpDb = seamJumpDb (loop);
            pt.rmsDb = 20.0 * std::log10 (std::max (1e-9, rms (loop.left)));
            std::string written, err;
            if (! writer (base, loop, ls, written, &err))
            {
                failed.store (true);
                const std::lock_guard<std::mutex> g (errLock);
                if (firstError.empty()) firstError = err;
                return;
            }
            pt.file = str (fs::relative (u8p (written), u8p (opt.outDir), ec));
            std::replace (pt.file.begin(), pt.file.end(), '\\', '/');
            if (ls.stems)
                for (int l = 0; l < kNumLayers; ++l)
                {
                    if (! layerUsedBy (static_cast<LayerId> (l), static_cast<int> (projectForSet (p, pt.set).kind)) || l == static_cast<int> (LayerId::Samples)) continue;
                    auto stem = makeLoop (r.layers[static_cast<std::size_t> (l)], pt.loopSamples, xfade);
                    applyGain (stem, gain);
                    std::string sw;
                    if (writer (base + "_" + layerName (static_cast<LayerId> (l)), stem, ls, sw, &err))
                    {
                        auto rel = str (fs::relative (u8p (sw), u8p (opt.outDir), ec));
                        std::replace (rel.begin(), rel.end(), '\\', '/');
                        pt.stems.push_back (rel);
                    }
                }
            report (static_cast<float> (++done) / res.plan.points.size(), "rendered " + pt.file);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back (worker);
    for (auto& t : pool) t.join();
    if (opt.cancel && opt.cancel->load()) { res.cancelled = true; return res; }
    if (failed.load()) { res.error = firstError.empty() ? "write error" : firstError; return res; }

    // ---- one-shots
    if (ls.startStop && p.kind != PowertrainKind::Electric)
    {
        Project q = projectForSet (p, "engine");
        auto oneShot = [&] (const std::string& name, const ControlScript& script, bool running, double seconds)
        {
            RenderOptions o;
            o.sampleRate = sr;
            o.seconds = seconds;
            o.startRunning = running;
            o.loaders = opt.loaders;
            auto r = renderScript (q, script, o);
            applyGain (r.mix, gain);
            std::string written, err;
            if (writer (str (u8p (opt.outDir) / "oneshots" / name), r.mix, ls, written, &err))
            {
                auto rel = str (fs::relative (u8p (written), u8p (opt.outDir), ec));
                std::replace (rel.begin(), rel.end(), '\\', '/');
                res.oneShots.emplace_back (name, rel);
            }
        };
        oneShot ("start", [] (double t, EngineControls& c) { c.mode = DriveMode::FreeRev; c.ignition = true; c.starter = t < 0.9; c.throttle = 0.0f; }, false, 3.0);
        oneShot ("stop", [] (double t, EngineControls& c) { c.mode = DriveMode::FreeRev; c.ignition = t < 0.6; c.throttle = 0.0f; }, true, 3.0);
    }
    {
        // gear change one-shot from the transmission model (clunk only)
        TransmissionVoice tv;
        tv.prepare (p.transmission, sr, p.seed);
        const int n = static_cast<int> (sr * 0.5);
        std::vector<float> l (static_cast<std::size_t> (n)), rr (static_cast<std::size_t> (n));
        LayerBus b;
        b.L[static_cast<int> (LayerId::Transmission)] = l.data();
        b.R[static_cast<int> (LayerId::Transmission)] = rr.data();
        TransmissionVoiceInput ti;
        ti.shiftCount = 1;
        ti.shiftStrength = 0.8f;
        tv.process (ti, b, 0, n);
        StereoBuffer sb;
        sb.sampleRate = sr;
        const float g = gain * dbToGain (p.mix.masterGainDb) * 3.0f;
        for (int i = 0; i < n; ++i)
        {
            sb.left.push_back (l[static_cast<std::size_t> (i)] * g);
            sb.right.push_back (rr[static_cast<std::size_t> (i)] * g);
        }
        std::string written, err;
        if (writer (str (u8p (opt.outDir) / "oneshots" / "shift"), sb, ls, written, &err))
        {
            auto rel = str (fs::relative (u8p (written), u8p (opt.outDir), ec));
            std::replace (rel.begin(), rel.end(), '\\', '/');
            res.oneShots.emplace_back ("shift", rel);
        }
    }

    res.renderSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    res.ok = true;
    const auto profile = runtimeProfileJson (p, res);
    res.profilePath = str (u8p (opt.outDir) / "engine_profile.json");
    std::ofstream f (u8p (res.profilePath), std::ios::binary | std::ios::trunc);
    f << profile;
    report (1.0f, "done");
    return res;
}

std::string runtimeProfileJson (const Project& p, const LoopSetResult& r)
{
    json j;
    j["format"] = "EngineSoundDesigner.runtime_profile";
    j["version"] = 1;
    j["name"] = p.name;
    j["kind"] = toString (p.kind);
    j["sample_rate"] = p.loops.sampleRate;
    j["loop_crossfade_ms"] = p.loops.crossfadeMs;
    j["normalize_gain_db"] = r.normalizeGainDb;
    if (p.kind != PowertrainKind::Electric)
    {
        const auto* l = findLayout (p.engine.layoutId);
        const auto* c = l ? findCrank (*l, p.engine.crankId) : nullptr;
        const auto plan = (l && c) ? computeFiringPlan (*l, *c, p.engine.bankAngleDeg, p.engine.firingOrder) : FiringPlan {};
        json curve = json::array();
        for (auto& pt : p.engine.torqueCurve.points()) curve.push_back ({ pt.x, pt.y });
        json tmap = json::array();
        for (auto& pt : p.engine.throttleMap.points()) tmap.push_back ({ pt.x, pt.y });
        j["engine"] = { { "layout", p.engine.layoutId }, { "cylinders", l ? l->cylinders : 0 }, { "idle_rpm", p.engine.idleRpm },
                        { "redline_rpm", p.engine.redlineRpm }, { "limiter_rpm", p.engine.limiterRpm }, { "cycle_deg", plan.cycleDeg },
                        { "events_per_rev", plan.valid ? plan.events.size() * 360.0 / plan.cycleDeg : 0.0 }, { "firing_order", plan.order },
                        { "torque_curve_nm", curve }, { "throttle_map", tmap } };
    }
    if (p.kind != PowertrainKind::Thermal)
        j["motor"] = { { "max_rpm", p.electric.maxRpm }, { "poles", p.electric.poles }, { "peak_torque_nm", p.electric.peakTorqueNm },
                       { "peak_power_kw", p.electric.peakPowerKw }, { "base_rpm", p.electric.baseRpm() } };
    if (p.kind == PowertrainKind::Hybrid)
        j["hybrid"] = { { "arch", static_cast<int> (p.hybrid.arch) }, { "ev_max_speed_kmh", p.hybrid.evMaxSpeedKmh } };
    j["transmission"] = { { "type", static_cast<int> (p.transmission.type) }, { "ratios", p.transmission.ratios }, { "final_drive", p.transmission.finalDrive },
                          { "shift_time_ms", p.transmission.shiftTimeMs } };
    j["vehicle"] = { { "mass_kg", p.vehicle.massKg }, { "wheel_radius_m", p.vehicle.wheelRadiusM } };
    j["interpolation"] = { { "rpm", "equal_power_crossfade" }, { "load", "linear_between_rows" }, { "pitch", "rpm / point_rpm_exact" },
                           { "pitch_range", { 0.5, 2.0 } }, { "weight_smoothing_s", 0.05 } };
    j["sync"] = { { "loops_start_on_cycle_boundary", true }, { "loops_hold_whole_cycles", true } };
    j["events"] = { { "shift", { { "duck_db", -6.0 }, { "duck_s", 0.15 } } } };

    json sets = json::object();
    for (const auto& pt : r.plan.points)
    {
        auto& set = sets[pt.set];
        if (! set.contains ("rows")) set["rows"] = json::array();
        json* row = nullptr;
        for (auto& rw : set["rows"])
            if (rw["state"] == pt.state && std::abs (rw["load"].get<double>() - pt.load) < 1e-6) row = &rw;
        if (! row)
        {
            set["rows"].push_back ({ { "state", pt.state }, { "load", pt.load }, { "points", json::array() } });
            row = &set["rows"].back();
        }
        (*row)["points"].push_back ({ { "rpm", pt.rpmExact }, { "file", pt.file }, { "loop_samples", pt.loopSamples }, { "cycles", pt.cycles },
                                      { "seam_jump_db", pt.seamJumpDb }, { "rms_db", pt.rmsDb }, { "stems", pt.stems } });
    }
    j["sets"] = sets;
    json shots = json::object();
    for (auto& [name, file] : r.oneShots) shots[name] = file;
    j["one_shots"] = shots;
    json mix = json::object();
    for (int l = 0; l < kNumLayers; ++l)
        mix[layerName (static_cast<LayerId> (l))] = p.mix.layers[static_cast<std::size_t> (l)].gainDb;
    j["layer_gains_db"] = mix;
    return j.dump (2);
}
} // namespace esd
