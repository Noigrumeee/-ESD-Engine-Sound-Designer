#pragma once
// Recorded / rendered samples mapped on (rpm, load), played with pitch correction and
// interpolated in two dimensions. This is the same algorithm as the game runtime exported in
// phase 8 (Godot / Unity scripts), so what you hear here is what the game will play:
//   * the mapped samples are grouped by load level (rows);
//   * the two rows around the current load are blended linearly;
//   * inside a row the two samples around the current rpm are crossfaded with equal power;
//   * each sample plays at rate = rpm / sample_rpm (pitch follows the engine between points);
//   * every voice keeps running (phase continuity) and its weight is smoothed (no clicks).
#include "LayerBus.h"

#include <functional>
#include <string>
#include <vector>

namespace esd
{
struct LoadedSample
{
    std::vector<float> L, R;
    double sampleRate = 48000.0;
};
/// Decodes a sample reference (path resolution is the caller's job).
using SampleLoader = std::function<bool (const SampleRef&, LoadedSample&)>;

struct GridWeight
{
    int index;
    float weight;
};
/// Interpolation weights over scattered (rpm, load) points grouped in load rows. Exposed for tests.
std::vector<GridWeight> gridWeights (const std::vector<std::pair<double, double>>& points, double rpm, double load);

class SampleLayer
{
public:
    void prepare (const std::vector<SampleRef>&, double fs, const SampleLoader&);
    bool empty() const { return voices.empty(); }
    void process (double rpm, double load, bool active, LayerBus& bus, int offset, int n);
    float rms() const { return level; }

private:
    struct Voice
    {
        LoadedSample data;
        double rpm = 1000.0, load = 1.0;
        double start = 0.0, end = 0.0, pos = 0.0;
        float gain = 1.0f, weight = 0.0f;
    };
    void computeWeights (double rpm, double load); // allocation free (audio thread)
    std::vector<Voice> voices;
    std::vector<std::pair<double, double>> points;
    std::vector<double> rowLevels;            // sorted load levels
    std::vector<std::vector<int>> rowIdx;     // voice indices per level, sorted by rpm
    std::vector<float> target;                // per voice weight of the current block
    double fs = 48000.0;
    float level = 0.0f;
};
} // namespace esd
