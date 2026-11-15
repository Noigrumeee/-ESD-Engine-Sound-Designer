// esd_render - command line renderer / analyser for Engine Sound Designer projects.
// Uses exactly the same core as the application (deterministic offline rendering).
#include "model/Presets.h"
#include "model/Serialization.h"
#include "render/GameExport.h"
#include "render/LoopGenerator.h"
#include "render/Render.h"

#include <chrono>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace esd;

namespace
{
void usage()
{
    std::puts (
        "esd_render - Engine Sound Designer offline renderer\n"
        "  --list                         list built-in presets\n"
        "  --preset ID | --project FILE   engine to render (default i4_na)\n"
        "  --rpm R --load L               dyno render at fixed rpm / load 0..1 (default 3000 / 0.5)\n"
        "  --demo                         scripted idle / blip / full pull / lift-off instead of dyno\n"
        "  --drive                        vehicle demo: launch, full throttle through the gears, lift-off, braking (12 s)\n"
        "  --seconds S                    length (default 4, demo 10)\n"
        "  --sr HZ                        sample rate (default 48000)\n"
        "  --format 16|24|f32             WAV format (default 24)\n"
        "  --out FILE.wav                 output file\n"
        "  --layers                       also write one WAV per layer next to --out\n"
        "  --analyze                      print engine orders / spectrum facts instead of writing\n"
        "  --calibrate                    per-layer levels of every preset\n"
        "  --save-project FILE            write the selected engine as a .esdproj file\n"
        "  --loops DIR                    generate the loop set (Loop Generator settings of the project)\n"
        "  --export godot|unity|unreal|generic DIR   game engine package\n");
}

const char* arg (int& i, int argc, char** argv)
{
    if (i + 1 >= argc) return "";
    return argv[++i];
}

double dbfs (double v) { return 20.0 * std::log10 (std::max (v, 1e-9)); }
} // namespace

int main (int argc, char** argv)
{
    std::string presetId = "i4_na", projectFile, out, saveProject, loopsDir, exportTarget, exportDir;
    double rpm = 3000, load = 0.5, seconds = -1, sr = 48000;
    std::string format = "24";
    bool demo = false, drive = false, analyze = false, calibrate = false, layers = false, list = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--preset") presetId = arg (i, argc, argv);
        else if (a == "--project") projectFile = arg (i, argc, argv);
        else if (a == "--rpm") rpm = std::atof (arg (i, argc, argv));
        else if (a == "--load") load = std::atof (arg (i, argc, argv));
        else if (a == "--seconds") seconds = std::atof (arg (i, argc, argv));
        else if (a == "--sr") sr = std::atof (arg (i, argc, argv));
        else if (a == "--format") format = arg (i, argc, argv);
        else if (a == "--out") out = arg (i, argc, argv);
        else if (a == "--save-project") saveProject = arg (i, argc, argv);
        else if (a == "--demo") demo = true;
        else if (a == "--drive") drive = true;
        else if (a == "--loops") loopsDir = arg (i, argc, argv);
        else if (a == "--export") { exportTarget = arg (i, argc, argv); exportDir = arg (i, argc, argv); }
        else if (a == "--analyze") analyze = true;
        else if (a == "--calibrate") calibrate = true;
        else if (a == "--layers") layers = true;
        else if (a == "--list") list = true;
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf (stderr, "unknown argument %s\n", a.c_str()); usage(); return 2; }
    }

    if (list)
    {
        for (auto& p : builtinPresets())
            std::printf ("%-20s %-36s %s\n", p.id.c_str(), p.name.c_str(), p.description.c_str());
        return 0;
    }

    if (calibrate)
    {
        std::printf ("%-20s %8s %8s | layers (dBFS RMS, dyno 0.45 x redline or 0.5 x max speed, load 0.7)\n", "preset", "mix", "peak");
        for (auto& info : builtinPresets())
        {
            Project p = makePreset (info.id);
            RenderOptions o;
            o.sampleRate = sr;
            o.seconds = 2.0;
            o.preRollSeconds = 1.5;
            o.renderLayers = true;
            const double rpmCal = p.kind == PowertrainKind::Electric ? p.electric.maxRpm * 0.5 : p.engine.redlineRpm * 0.45;
            const auto r = renderDyno (p, rpmCal, 0.7, o);
            float peak = 0.0f;
            for (float v : r.mix.left) peak = std::max (peak, std::abs (v));
            std::printf ("%-20s %8.1f %8.1f |", info.id.c_str(), dbfs (rms (r.mix.left)), dbfs (peak));
            for (int li = 0; li < kNumLayers; ++li)
                if (layerUsedBy (static_cast<LayerId> (li), static_cast<int> (p.kind)) && li != static_cast<int> (LayerId::Samples))
                    std::printf (" %s %.1f", layerName (static_cast<LayerId> (li)), dbfs (rms (r.layers[static_cast<std::size_t> (li)].left)));
            std::printf ("\n");
        }
        return 0;
    }

    Project project;
    if (! projectFile.empty())
    {
        auto lr = loadProjectFile (projectFile);
        if (! lr.ok) { std::fprintf (stderr, "%s\n", lr.error.c_str()); return 1; }
        for (auto& f : lr.fixes) std::fprintf (stderr, "note: %s\n", f.message.c_str());
        project = lr.project;
    }
    else
        project = makePreset (presetId);

    if (! saveProject.empty())
    {
        std::string err;
        if (! saveProjectFile (project, saveProject, &err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
        std::printf ("saved %s\n", saveProject.c_str());
        if (out.empty() && ! analyze) return 0;
    }

    auto progress = [] (float f, const std::string& m)
    {
        std::printf ("\r%3d %%  %-60s", static_cast<int> (f * 100.0f), m.substr (0, 60).c_str());
        std::fflush (stdout);
    };
    if (! loopsDir.empty())
    {
        LoopJobOptions o;
        o.outDir = loopsDir;
        o.progress = progress;
        o.loaders = defaultLoaders();
        const auto r = generateLoops (project, o);
        std::printf ("\n");
        if (! r.ok) { std::fprintf (stderr, "loop generation failed: %s\n", r.error.c_str()); return 1; }
        for (auto& pt : r.plan.points)
            std::printf ("%-40s %8.1f rpm  load %5.2f  %3d cycles  %7d smp  seam %5.1f dB  %6.1f dBFS%s\n", pt.file.c_str(), pt.rpmExact, pt.load, pt.cycles,
                         pt.loopSamples, pt.seamJumpDb, pt.rmsDb, pt.reused ? "  (kept)" : "");
        std::printf ("%zu loops, normalisation %.1f dB, %.1f s, profile %s\n", r.plan.points.size(), r.normalizeGainDb, r.renderSeconds, r.profilePath.c_str());
        return 0;
    }
    if (! exportTarget.empty())
    {
        const GameTarget t = exportTarget == "unity" ? GameTarget::Unity : exportTarget == "unreal" ? GameTarget::Unreal : exportTarget == "generic" ? GameTarget::Generic : GameTarget::Godot;
        const auto exeDir = std::filesystem::absolute (std::filesystem::path (argv[0])).parent_path().string();
        const auto tpl = findTemplatesDir (exeDir);
        if (tpl.empty()) { std::fprintf (stderr, "templates folder not found\n"); return 1; }
        LoopJobOptions o;
        o.progress = progress;
        o.resume = false;
        o.loaders = defaultLoaders();
        const auto r = exportGamePackage (project, t, exportDir, tpl, o);
        std::printf ("\n");
        if (! r.ok) { std::fprintf (stderr, "export failed: %s\n", r.error.c_str()); return 1; }
        std::printf ("package: %s\n", r.folder.c_str());
        return 0;
    }

    const auto* layout = findLayout (project.engine.layoutId);
    const auto* crank = findCrank (*layout, project.engine.crankId);
    const auto plan = computeFiringPlan (*layout, *crank, project.engine.bankAngleDeg, project.engine.firingOrder);

    RenderOptions o;
    o.sampleRate = sr;
    o.seconds = seconds > 0 ? seconds : (demo ? 10.0 : drive ? 12.0 : 4.0);
    o.preRollSeconds = (demo || drive) ? 0.0 : 1.5;
    o.renderLayers = layers;

    const auto t0 = std::chrono::steady_clock::now();
    RenderResult r;
    if (drive)
    {
        const bool manual = project.transmission.type == TransmissionType::Manual;
        const double shiftRpm = project.engine.redlineRpm * 0.95;
        auto script = [manual, shiftRpm] (double t, EngineControls& c)
        {
            c.mode = DriveMode::Vehicle;
            c.ignition = true;
            c.throttle = t < 0.8 ? 0.0f : t < 8.0 ? 1.0f : 0.0f;
            c.brake = t > 9.5 ? 0.5f : 0.0f;
            (void) manual;
            (void) shiftRpm;
        };
        // manual gearboxes: the script needs the engine speed -> shift every 1.9 s of full throttle
        if (manual)
        {
            auto manualScript = [] (double t, EngineControls& c)
            {
                c.mode = DriveMode::Vehicle;
                c.ignition = true;
                c.throttle = t < 0.8 ? 0.0f : t < 8.0 ? 1.0f : 0.0f;
                c.brake = t > 9.5 ? 0.5f : 0.0f;
                const int shifts = t < 0.8 ? 0 : std::min (5, static_cast<int> ((t - 0.8) / 1.9));
                c.shiftUpCount = static_cast<uint32_t> (t < 8.0 ? shifts : 5);
            };
            r = renderScript (project, manualScript, o);
        }
        else
            r = renderScript (project, script, o);
    }
    else
        r = demo ? renderDemoPull (project, o) : renderDyno (project, rpm, load, o);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();

    if (analyze)
    {
        std::printf ("%s  [%s, %s]  %.0f cc\n", project.name.c_str(), layout->name.c_str(), crank->name.c_str(), project.engine.displacementCc());
        std::printf ("firing order %s, intervals:", firingOrderToString (plan.order).c_str());
        for (double iv : plan.intervals) std::printf (" %.0f", iv);
        std::printf ("  (cycle %.0f deg)\n", plan.cycleDeg);
        const double rev = rpm / 60.0;
        const double firing = rev * plan.events.size() * 360.0 / plan.cycleDeg;
        std::printf ("rpm %.0f: rev %.2f Hz, firing %.2f Hz\n", rpm, rev, firing);
        std::printf ("order  freq(Hz)   L(dB)   R(dB)\n");
        for (double ord = 0.5; ord <= 8.0; ord += 0.5)
        {
            const double f = rev * ord;
            std::printf ("%5.1f %9.1f %7.1f %7.1f\n", ord, f, dbfs (toneMagnitude (r.mix.left, sr, f)), dbfs (toneMagnitude (r.mix.right, sr, f)));
        }
        std::printf ("RMS L %.1f dBFS, R %.1f dBFS; render %.0f ms for %.1f s audio (%.1fx real time)\n", dbfs (rms (r.mix.left)), dbfs (rms (r.mix.right)), ms,
                     o.seconds, o.seconds * 1000.0 / ms);
    }

    if (! out.empty())
    {
        const WavFormat fmt = format == "16" ? WavFormat::Pcm16 : format == "f32" ? WavFormat::Float32 : WavFormat::Pcm24;
        std::string err;
        if (! writeWav (out, r.mix, fmt, false, &err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
        std::printf ("wrote %s (%.1f s, %.0f Hz) in %.0f ms\n", out.c_str(), o.seconds, sr, ms);
        if (layers)
        {
            const std::string base = out.size() > 4 ? out.substr (0, out.size() - 4) : out;
            for (int l = 0; l < kNumLayers; ++l)
            {
                const std::string path = base + "_" + layerName (static_cast<LayerId> (l)) + ".wav";
                writeWav (path, r.layers[static_cast<std::size_t> (l)], fmt, false, &err);
                std::printf ("  layer %s\n", path.c_str());
            }
        }
    }
    else if (! analyze)
        usage();
    return 0;
}
