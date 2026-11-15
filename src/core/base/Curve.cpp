#include "Curve.h"
#include "Math.h"

#include <algorithm>
#include <cmath>

namespace esd
{
const char* toString (CurveInterp i)
{
    switch (i)
    {
        case CurveInterp::Linear: return "linear";
        case CurveInterp::Smooth: return "smooth";
        case CurveInterp::Step: return "step";
    }
    return "smooth";
}

CurveInterp curveInterpFromString (const std::string& s)
{
    if (s == "linear") return CurveInterp::Linear;
    if (s == "step") return CurveInterp::Step;
    return CurveInterp::Smooth;
}

Curve::Curve (std::vector<CurvePoint> p, CurveInterp i) : interp (i) { setPoints (std::move (p)); }

void Curve::setPoints (std::vector<CurvePoint> newPoints)
{
    pts = std::move (newPoints);
    std::stable_sort (pts.begin(), pts.end(), [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    rebuildTangents();
}

void Curve::setInterpolation (CurveInterp i)
{
    interp = i;
    rebuildTangents();
}

std::size_t Curve::movePoint (std::size_t index, CurvePoint p)
{
    if (index >= pts.size())
        return index;
    const double lo = index > 0 ? pts[index - 1].x + 1e-9 : -1e300;
    const double hi = index + 1 < pts.size() ? pts[index + 1].x - 1e-9 : 1e300;
    p.x = clampv (p.x, lo, hi);
    pts[index] = p;
    rebuildTangents();
    return index;
}

std::size_t Curve::addPoint (CurvePoint p)
{
    auto it = std::upper_bound (pts.begin(), pts.end(), p, [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    const auto idx = static_cast<std::size_t> (it - pts.begin());
    pts.insert (it, p);
    rebuildTangents();
    return idx;
}

void Curve::removePoint (std::size_t index)
{
    if (index < pts.size())
    {
        pts.erase (pts.begin() + static_cast<std::ptrdiff_t> (index));
        rebuildTangents();
    }
}

double Curve::maxY() const
{
    double m = pts.empty() ? 0.0 : pts.front().y;
    for (auto& p : pts)
        m = std::max (m, p.y);
    return m;
}

double Curve::argMax (int samples) const
{
    if (pts.empty())
        return 0.0;
    double bestX = pts.front().x, bestY = eval (bestX);
    for (int i = 1; i <= samples; ++i)
    {
        const double x = minX() + (maxX() - minX()) * i / samples;
        const double y = eval (x);
        if (y > bestY)
        {
            bestY = y;
            bestX = x;
        }
    }
    return bestX;
}

void Curve::rebuildTangents()
{
    const std::size_t n = pts.size();
    tangents.assign (n, 0.0);
    if (n < 2 || interp != CurveInterp::Smooth)
        return;

    std::vector<double> d (n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        const double dx = pts[i + 1].x - pts[i].x;
        d[i] = dx > 0.0 ? (pts[i + 1].y - pts[i].y) / dx : 0.0;
    }
    tangents[0] = d[0];
    tangents[n - 1] = d[n - 2];
    for (std::size_t i = 1; i + 1 < n; ++i)
        tangents[i] = (d[i - 1] * d[i] <= 0.0) ? 0.0 : 0.5 * (d[i - 1] + d[i]);

    // Fritsch-Carlson limiter -> monotone, no overshoot
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        if (d[i] == 0.0)
        {
            tangents[i] = tangents[i + 1] = 0.0;
            continue;
        }
        const double a = tangents[i] / d[i], b = tangents[i + 1] / d[i];
        const double s = a * a + b * b;
        if (s > 9.0)
        {
            const double t = 3.0 / std::sqrt (s);
            tangents[i] = t * a * d[i];
            tangents[i + 1] = t * b * d[i];
        }
    }
}

double Curve::eval (double x) const
{
    const std::size_t n = pts.size();
    if (n == 0) return 0.0;
    if (n == 1 || x <= pts.front().x) return pts.front().y;
    if (x >= pts.back().x) return pts.back().y;

    auto it = std::upper_bound (pts.begin(), pts.end(), x, [] (double v, const CurvePoint& p) { return v < p.x; });
    const std::size_t i1 = static_cast<std::size_t> (it - pts.begin());
    const std::size_t i0 = i1 - 1;
    const auto& p0 = pts[i0];
    const auto& p1 = pts[i1];
    const double h = p1.x - p0.x;
    if (h <= 0.0) return p1.y;
    const double t = (x - p0.x) / h;

    switch (interp)
    {
        case CurveInterp::Step: return p0.y;
        case CurveInterp::Linear: return p0.y + (p1.y - p0.y) * t;
        case CurveInterp::Smooth:
        {
            const double t2 = t * t, t3 = t2 * t;
            const double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t;
            const double h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
            return h00 * p0.y + h10 * h * tangents[i0] + h01 * p1.y + h11 * h * tangents[i1];
        }
    }
    return p0.y;
}

void CurveTable::build (const Curve& c, double x0, double x1)
{
    lo = x0;
    hi = x1 > x0 ? x1 : x0 + 1.0;
    scale = kSize / (hi - lo);
    for (int i = 0; i <= kSize; ++i)
        table[i] = static_cast<float> (c.eval (lo + (hi - lo) * i / kSize));
}

float CurveTable::eval (double x) const
{
    double pos = (x - lo) * scale;
    if (pos <= 0.0) return table[0];
    if (pos >= kSize) return table[kSize];
    const int i = static_cast<int> (pos);
    const float f = static_cast<float> (pos - i);
    return table[i] + (table[i + 1] - table[i]) * f;
}
} // namespace esd
