#pragma once
// Game engine packages (phase 8): a loop set + runtime profile + the integration code for
// Godot (GDScript player + example, tested headless with Godot 4.5), Unity (C#) and Unreal (C++).
#include "LoopGenerator.h"

namespace esd
{
enum class GameTarget { Godot, Unity, Unreal, Generic };
const char* toString (GameTarget);

struct GameExportResult
{
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string folder;     ///< package root
    std::string soundsDir;  ///< folder holding engine_profile.json
    LoopSetResult loops;
};

/// Finds the templates folder next to the executable (or in the source tree when developing).
std::string findTemplatesDir (const std::string& exeDir);

/// Renders the loop set into the right place and writes the integration files.
/// Game packages always use 16-bit WAV (what the three engines import without conversion).
GameExportResult exportGamePackage (const Project&, GameTarget, const std::string& parentDir, const std::string& templatesDir, LoopJobOptions opt);
} // namespace esd
