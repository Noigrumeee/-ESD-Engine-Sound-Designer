#include "GameExport.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

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

std::string safeName (const std::string& n)
{
    std::string s;
    for (unsigned char c : n) s += std::isalnum (c) ? static_cast<char> (std::tolower (c)) : '_';
    while (s.find ("__") != std::string::npos) s.replace (s.find ("__"), 2, "_");
    if (! s.empty() && s.back() == '_') s.pop_back();
    return s.empty() ? std::string ("engine") : s;
}

bool copyTree (const fs::path& from, const fs::path& to, std::string& err)
{
    std::error_code ec;
    if (! fs::exists (from, ec)) { err = "template missing: " + str (from); return false; }
    fs::create_directories (to, ec);
    fs::copy (from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) { err = ec.message(); return false; }
    return true;
}

void writeText (const fs::path& p, const std::string& text)
{
    std::error_code ec;
    fs::create_directories (p.parent_path(), ec);
    std::ofstream f (p, std::ios::binary | std::ios::trunc);
    f << text;
}
} // namespace

const char* toString (GameTarget t)
{
    switch (t)
    {
        case GameTarget::Godot: return "godot";
        case GameTarget::Unity: return "unity";
        case GameTarget::Unreal: return "unreal";
        case GameTarget::Generic: return "generic";
    }
    return "generic";
}

std::string findTemplatesDir (const std::string& exeDir)
{
    fs::path p = u8p (exeDir);
    for (int up = 0; up < 7; ++up)
    {
        std::error_code ec;
        if (fs::exists (p / "templates" / "godot", ec)) return str (p / "templates");
        if (! p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

GameExportResult exportGamePackage (const Project& projIn, GameTarget target, const std::string& parentDir, const std::string& templatesDir, LoopJobOptions opt)
{
    GameExportResult r;
    Project p = projIn;
    p.loops.flac = false;
    p.loops.bitDepth = 16;
    const std::string name = safeName (p.name);
    const fs::path root = u8p (parentDir) / (name + "_" + toString (target));
    fs::path sounds;
    switch (target)
    {
        case GameTarget::Godot: sounds = root / "engine_sounds" / name; break;
        case GameTarget::Unity: sounds = root / "Assets" / "Resources" / "EngineSounds" / name; break;
        case GameTarget::Unreal: sounds = root / "Content" / "EngineSounds" / name; break;
        case GameTarget::Generic: sounds = root; break;
    }
    r.folder = str (root);
    r.soundsDir = str (sounds);
    opt.outDir = str (sounds);
    opt.writer = defaultAudioWriter();
    r.loops = generateLoops (p, opt);
    if (r.loops.cancelled) { r.cancelled = true; return r; }
    if (! r.loops.ok) { r.error = r.loops.error; return r; }

    const fs::path tpl = u8p (templatesDir);
    std::string err;
    bool ok = true;
    switch (target)
    {
        case GameTarget::Godot:
            ok = copyTree (tpl / "godot" / "addons", root / "addons", err) && copyTree (tpl / "godot" / "example", root / "example", err)
                 && copyTree (tpl / "godot" / "tests", root / "tests", err);
            if (ok) fs::copy_file (tpl / "godot" / "README_GODOT.md", root / "README_GODOT.md", fs::copy_options::overwrite_existing);
            if (ok)
            {
                // ready-to-run example scene + minimal project file so the folder opens directly in Godot
                std::ostringstream scn;
                scn << "[gd_scene load_steps=2 format=3]\n\n[ext_resource type=\"Script\" path=\"res://example/example_vehicle.gd\" id=\"1\"]\n\n"
                    << "[node name=\"Example\" type=\"Node\"]\nscript = ExtResource(\"1\")\nprofile_path = \"res://engine_sounds/" << name << "/engine_profile.json\"\n";
                writeText (root / "example" / "example.tscn", scn.str());
                writeText (root / "project.godot", "config_version=5\n\n[application]\n\nconfig/name=\"" + p.name + " engine sound\"\nrun/main_scene=\"res://example/example.tscn\"\n");
            }
            break;
        case GameTarget::Unity:
            ok = copyTree (tpl / "unity", root / "Assets" / "EngineSound", err);
            if (ok) fs::rename (root / "Assets" / "EngineSound" / "README_UNITY.md", root / "README_UNITY.md");
            break;
        case GameTarget::Unreal:
        {
            fs::create_directories (root / "Source" / "EngineSound");
            for (const char* f : { "EngineSoundComponent.h", "EngineSoundComponent.cpp" })
                fs::copy_file (tpl / "unreal" / f, root / "Source" / "EngineSound" / f, fs::copy_options::overwrite_existing);
            fs::copy_file (tpl / "unreal" / "README_UNREAL.md", root / "README_UNREAL.md", fs::copy_options::overwrite_existing);
            std::ostringstream csv;
            csv << "Name,Set,State,Load,Rpm,File,LoopSamples,Cycles\n";
            int i = 0;
            for (auto& pt : r.loops.plan.points)
                csv << "Row" << i++ << "," << pt.set << "," << pt.state << "," << pt.load << "," << pt.rpmExact << "," << pt.file << "," << pt.loopSamples << "," << pt.cycles << "\n";
            writeText (sounds / "EngineSoundGrid.csv", csv.str());
            break;
        }
        case GameTarget::Generic: break;
    }
    if (! ok) { r.error = err; return r; }
    std::ostringstream readme;
    readme << "# " << p.name << " - Engine Sound Designer export (" << toString (target) << ")\n\n"
           << "Loops: " << r.loops.plan.points.size() << " (" << r.loops.plan.totalSeconds << " s), sample rate " << p.loops.sampleRate
           << " Hz, 16-bit WAV, normalisation " << r.loops.normalizeGainDb << " dB.\n"
           << "Each loop holds a whole number of engine cycles and starts on a cycle boundary.\n"
           << "engine_profile.json describes the grid (sets / rows / points), the files, the response curves and the transmission.\n";
    writeText (root / "README.md", readme.str());
    r.ok = true;
    return r;
}
} // namespace esd
