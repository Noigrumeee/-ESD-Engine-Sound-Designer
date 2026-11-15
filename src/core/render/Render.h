#pragma once
// Offline rendering (deterministic: same project + same request = same samples) and WAV output.
#include "../synth/EngineRuntime.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace esd
{
struct StereoBuffer
{
    double sampleRate = 48000.0;
    std::vector<float> left, right;
    std::size_t frames() const { return left.size(); }
};

/// Called once per simulation block with the time in seconds; fills the controls.
using ControlScript = std::function<void (double timeSec, EngineControls&)>;

struct RenderOptions
{
    double sampleRate = 48000.0;
    double seconds = 4.0;
    double preRollSeconds = 0.0;   ///< rendered and discarded (lets the dyno settle)
    bool startRunning = true;
    std::atomic<bool>* cancel = nullptr;
    std::function<void (float progress)> onProgress;
    bool renderLayers = false;     ///< also fill StereoBuffer per layer
    bool alignToCycle = false;     ///< start recording exactly at a cycle boundary of the primary source
    EngineRuntime::Loaders loaders;
};

/// Loaders using the core WAV reader (absolute paths, or relative to baseDir).
EngineRuntime::Loaders defaultLoaders (const std::string& baseDir = {});

struct RenderResult
{
    bool cancelled = false;
    StereoBuffer mix;
    std::vector<StereoBuffer> layers; ///< kNumLayers entries when requested
};

RenderResult renderScript (const Project&, const ControlScript&, const RenderOptions&);
/// Engine held at a fixed rpm and load (dyno mode).
RenderResult renderDyno (const Project&, double rpm, double load, const RenderOptions&, bool overrun = false);
/// Standard demo: idle, blip, full throttle pull to the limiter, lift off, back to idle.
RenderResult renderDemoPull (const Project&, const RenderOptions&);

enum class WavFormat { Pcm16, Pcm24, Float32 };
bool writeWav (const std::string& path, const StereoBuffer&, WavFormat, bool mono = false, std::string* error = nullptr);

/// Simple analysis helpers (used by tests and the CLI)
double rms (const std::vector<float>&, std::size_t from = 0, std::size_t to = 0);
/// Goertzel magnitude of frequency f over the buffer (normalised by length).
double toneMagnitude (const std::vector<float>&, double sampleRate, double f);
} // namespace esd
