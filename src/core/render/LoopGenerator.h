#pragma once
// Loop Generator (phase 7) and runtime profile (phase 8).
//
// Each loop is rendered on the dyno at an rpm adjusted so that the loop holds an exact whole
// number of engine cycles (thermal: 720 or 360 crank degrees; electric: one rotor revolution):
// the deterministic part of the sound is then periodic over the loop. Recording starts on a
// cycle boundary (all loops share the same phase reference). The non periodic parts (cycle to
// cycle variation, noise) are made seamless with an equal-power crossfade of the tail into the
// head, and the seam is measured (continuity check).
#include "Render.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace esd
{
struct LoopPoint
{
    std::string set;       ///< "engine" or "motor" (hybrids have both)
    std::string state;     ///< "on_load", "overrun", "regen"
    double rpm = 0.0;      ///< requested
    double rpmExact = 0.0; ///< rendered (whole cycles)
    double load = 0.0;     ///< thermal: 0..1, electric: torque fraction -1..1
    int cycles = 0;
    int loopSamples = 0;
    std::string file;      ///< relative to the output folder
    std::vector<std::string> stems;
    double seamJumpDb = 0.0;   ///< seam discontinuity relative to the typical sample step (< 6 dB = inaudible)
    double rmsDb = -120.0;
    bool reused = false;
};

struct LoopPlan
{
    std::vector<LoopPoint> points;
    double totalSeconds = 0.0;
    std::size_t estimatedBytes = 0;
};

/// Writes a buffer (pathNoExt without extension). Returns the written file name (with extension).
using AudioWriter = std::function<bool (const std::string& pathNoExt, const StereoBuffer&, const LoopSpec&, std::string& written, std::string* error)>;
/// Default writer: WAV through the core (FLAC requires the application writer).
AudioWriter defaultAudioWriter();

struct LoopJobOptions
{
    std::string outDir;
    std::atomic<bool>* cancel = nullptr;
    std::function<void (float progress, const std::string& message)> progress; ///< called from worker threads (serialised)
    AudioWriter writer;
    bool resume = true;    ///< keep loops already present (same file name)
    EngineRuntime::Loaders loaders;
};

struct LoopSetResult
{
    bool ok = false;
    bool cancelled = false;
    std::string error;
    LoopPlan plan;
    double normalizeGainDb = 0.0;
    std::vector<std::pair<std::string, std::string>> oneShots; ///< name -> file
    std::string profilePath;
    double renderSeconds = 0.0;
};

LoopPlan planLoops (const Project&);
LoopSetResult generateLoops (const Project&, const LoopJobOptions&);

/// Runtime profile JSON describing a generated set (consumed by the game scripts).
std::string runtimeProfileJson (const Project&, const LoopSetResult&);
} // namespace esd
