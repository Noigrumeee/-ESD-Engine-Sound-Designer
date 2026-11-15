#pragma once
// Editable 1D curve (control points + interpolation). Used for torque curves, throttle maps
// and, later, for every automation curve of the Sound Lab.
#include <cstddef>
#include <string>
#include <vector>

namespace esd
{
struct CurvePoint
{
    double x = 0.0;
    double y = 0.0;
    bool operator== (const CurvePoint&) const = default;
};

enum class CurveInterp
{
    Linear,
    Smooth, ///< monotone cubic (Fritsch-Carlson): smooth, never overshoots the control points
    Step
};

const char* toString (CurveInterp);
CurveInterp curveInterpFromString (const std::string&);

class Curve
{
public:
    Curve() = default;
    explicit Curve (std::vector<CurvePoint> pts, CurveInterp interp = CurveInterp::Smooth);

    double eval (double x) const;

    const std::vector<CurvePoint>& points() const { return pts; }
    void setPoints (std::vector<CurvePoint> newPoints);
    /// Moves one point; keeps the order (x clamped between its neighbours). Returns the index.
    std::size_t movePoint (std::size_t index, CurvePoint p);
    std::size_t addPoint (CurvePoint p);
    void removePoint (std::size_t index);

    CurveInterp interpolation() const { return interp; }
    void setInterpolation (CurveInterp i);

    bool empty() const { return pts.empty(); }
    double minX() const { return pts.empty() ? 0.0 : pts.front().x; }
    double maxX() const { return pts.empty() ? 0.0 : pts.back().x; }
    double maxY() const;
    /// x of the maximum of the evaluated curve (sampled), useful for "peak torque at".
    double argMax (int samples = 512) const;

    bool operator== (const Curve& o) const { return pts == o.pts && interp == o.interp; }

private:
    void rebuildTangents();
    std::vector<CurvePoint> pts;
    std::vector<double> tangents;
    CurveInterp interp = CurveInterp::Smooth;
};

/// Fixed-size lookup table of a curve over [x0, x1]: allocation-free evaluation for the audio thread.
class CurveTable
{
public:
    static constexpr int kSize = 256;
    void build (const Curve& c, double x0, double x1);
    float eval (double x) const;
    double x0() const { return lo; }
    double x1() const { return hi; }

private:
    float table[kSize + 1] {};
    double lo = 0.0, hi = 1.0, scale = 1.0;
};
} // namespace esd
