#include "SampleLayer.h"
#include "../base/Math.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace esd
{
std::vector<GridWeight> gridWeights (const std::vector<std::pair<double, double>>& pts, double rpm, double load)
{
    std::vector<GridWeight> out;
    if (pts.empty()) return out;
    // rows by load level
    std::map<double, std::vector<int>> rows;
    for (int i = 0; i < static_cast<int> (pts.size()); ++i)
        rows[std::round (pts[static_cast<std::size_t> (i)].second * 1000.0) / 1000.0].push_back (i);
    std::vector<double> levels;
    for (auto& [l, v] : rows) levels.push_back (l);

    // bracketing rows
    double lo = levels.front(), hi = levels.front(), u = 0.0;
    if (load <= levels.front()) lo = hi = levels.front();
    else if (load >= levels.back()) lo = hi = levels.back();
    else
        for (std::size_t k = 0; k + 1 < levels.size(); ++k)
            if (load >= levels[k] && load <= levels[k + 1])
            {
                lo = levels[k];
                hi = levels[k + 1];
                u = (load - lo) / (hi - lo);
                break;
            }

    auto rowWeights = [&] (double level, float rowW)
    {
        auto idx = rows[level];
        std::sort (idx.begin(), idx.end(), [&] (int a, int b) { return pts[static_cast<std::size_t> (a)].first < pts[static_cast<std::size_t> (b)].first; });
        const double r0 = pts[static_cast<std::size_t> (idx.front())].first, r1 = pts[static_cast<std::size_t> (idx.back())].first;
        if (rpm <= r0) { out.push_back ({ idx.front(), rowW }); return; }
        if (rpm >= r1) { out.push_back ({ idx.back(), rowW }); return; }
        for (std::size_t k = 0; k + 1 < idx.size(); ++k)
        {
            const double a = pts[static_cast<std::size_t> (idx[k])].first, b = pts[static_cast<std::size_t> (idx[k + 1])].first;
            if (rpm >= a && rpm <= b)
            {
                const double t = b > a ? (rpm - a) / (b - a) : 0.0;
                out.push_back ({ idx[k], rowW * static_cast<float> (std::cos (t * kPi * 0.5)) });   // equal power
                out.push_back ({ idx[k + 1], rowW * static_cast<float> (std::sin (t * kPi * 0.5)) });
                return;
            }
        }
    };
    if (lo == hi) rowWeights (lo, 1.0f);
    else
    {
        rowWeights (lo, static_cast<float> (1.0 - u));
        rowWeights (hi, static_cast<float> (u));
    }
    return out;
}

void SampleLayer::prepare (const std::vector<SampleRef>& refs, double sampleRate, const SampleLoader& loader)
{
    fs = sampleRate;
    voices.clear();
    points.clear();
    if (! loader) return;
    for (const auto& r : refs)
    {
        if (r.rpm <= 0.0) continue; // only mapped samples take part in the engine layer
        Voice v;
        if (! loader (r, v.data) || v.data.L.size() < 16) continue;
        const double total = static_cast<double> (v.data.L.size());
        v.start = clampv (r.startSec * v.data.sampleRate, 0.0, total - 8.0);
        v.end = r.endSec > 0.0 ? clampv (r.endSec * v.data.sampleRate, v.start + 8.0, total - 1.0) : total - 1.0;
        v.pos = v.start;
        v.rpm = r.rpm;
        v.load = r.load;
        v.gain = dbToGain (r.gainDb);
        points.emplace_back (v.rpm, v.load);
        voices.push_back (std::move (v));
    }
    std::map<double, std::vector<int>> rows;
    for (int i = 0; i < static_cast<int> (points.size()); ++i)
        rows[std::round (points[static_cast<std::size_t> (i)].second * 1000.0) / 1000.0].push_back (i);
    rowLevels.clear();
    rowIdx.clear();
    for (auto& [lvl, idx] : rows)
    {
        std::sort (idx.begin(), idx.end(), [this] (int a, int b) { return points[static_cast<std::size_t> (a)].first < points[static_cast<std::size_t> (b)].first; });
        rowLevels.push_back (lvl);
        rowIdx.push_back (idx);
    }
    target.assign (voices.size(), 0.0f);
}

void SampleLayer::computeWeights (double rpm, double load)
{
    std::fill (target.begin(), target.end(), 0.0f);
    if (rowLevels.empty()) return;
    std::size_t r0 = 0, r1 = 0;
    double u = 0.0;
    if (load >= rowLevels.back()) r0 = r1 = rowLevels.size() - 1;
    else if (load > rowLevels.front())
        for (std::size_t k = 0; k + 1 < rowLevels.size(); ++k)
            if (load >= rowLevels[k] && load <= rowLevels[k + 1])
            {
                r0 = k;
                r1 = k + 1;
                u = (load - rowLevels[k]) / (rowLevels[k + 1] - rowLevels[k]);
                break;
            }
    auto row = [this, rpm] (std::size_t r, float w)
    {
        const auto& idx = rowIdx[r];
        const auto& p = points;
        if (rpm <= p[static_cast<std::size_t> (idx.front())].first) { target[static_cast<std::size_t> (idx.front())] += w; return; }
        if (rpm >= p[static_cast<std::size_t> (idx.back())].first) { target[static_cast<std::size_t> (idx.back())] += w; return; }
        for (std::size_t k = 0; k + 1 < idx.size(); ++k)
        {
            const double a = p[static_cast<std::size_t> (idx[k])].first, b = p[static_cast<std::size_t> (idx[k + 1])].first;
            if (rpm >= a && rpm <= b)
            {
                const double t = b > a ? (rpm - a) / (b - a) : 0.0;
                target[static_cast<std::size_t> (idx[k])] += w * static_cast<float> (std::cos (t * kPi * 0.5));
                target[static_cast<std::size_t> (idx[k + 1])] += w * static_cast<float> (std::sin (t * kPi * 0.5));
                return;
            }
        }
    };
    if (r0 == r1) row (r0, 1.0f);
    else
    {
        row (r0, static_cast<float> (1.0 - u));
        row (r1, static_cast<float> (u));
    }
}

void SampleLayer::process (double rpm, double load, bool active, LayerBus& bus, int offset, int n)
{
    if (voices.empty()) return;
    const int l = static_cast<int> (LayerId::Samples);
    if (active) computeWeights (rpm, load);
    else std::fill (target.begin(), target.end(), 0.0f);
    double sq = 0.0;
    for (std::size_t k = 0; k < voices.size(); ++k)
    {
        auto& v = voices[k];
        const double rate = (rpm > 1.0 ? rpm / v.rpm : 1.0) * v.data.sampleRate / fs;
        const float w0 = v.weight, w1 = target[k];
        v.weight = w1;
        const double len = v.end - v.start;
        if (w0 < 1e-5f && w1 < 1e-5f)
        {
            v.pos = v.start + std::fmod (v.pos - v.start + rate * n, len); // keep running (phase continuity)
            continue;
        }
        for (int i = 0; i < n; ++i)
        {
            const float w = w0 + (w1 - w0) * static_cast<float> (i) / static_cast<float> (n);
            const auto idx = static_cast<std::size_t> (v.pos);
            const float f = static_cast<float> (v.pos - static_cast<double> (idx));
            const std::size_t j = std::min (idx + 1, v.data.L.size() - 1);
            const float sl = v.data.L[idx] + (v.data.L[j] - v.data.L[idx]) * f;
            const float sr = v.data.R.empty() ? sl : v.data.R[idx] + (v.data.R[j] - v.data.R[idx]) * f;
            const float g = w * v.gain;
            if (bus.L[l]) bus.L[l][offset + i] += sl * g;
            if (bus.R[l]) bus.R[l][offset + i] += sr * g;
            sq += 0.5 * (static_cast<double> (sl * g) * sl * g + static_cast<double> (sr * g) * sr * g);
            v.pos += rate;
            if (v.pos >= v.end) v.pos -= len;
        }
    }
    level = static_cast<float> (std::sqrt (sq / std::max (1, n)));
}
} // namespace esd
