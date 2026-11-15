#include "EngineLayout.h"
#include "../base/Math.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace esd
{
const char* toString (CycleType c)
{
    switch (c)
    {
        case CycleType::FourStroke: return "4-stroke";
        case CycleType::TwoStroke: return "2-stroke";
        case CycleType::Wankel: return "Wankel";
    }
    return "4-stroke";
}

double cycleDegrees (CycleType c) { return c == CycleType::FourStroke ? 720.0 : 360.0; }

bool FiringPlan::evenFiring() const { return valid && (maxInterval() - minInterval()) < 0.5; }

double FiringPlan::maxInterval() const
{
    return intervals.empty() ? 0.0 : *std::max_element (intervals.begin(), intervals.end());
}

double FiringPlan::minInterval() const
{
    return intervals.empty() ? 0.0 : *std::min_element (intervals.begin(), intervals.end());
}

namespace
{
std::vector<int> repeatBanks (std::initializer_list<int> pattern, int n)
{
    std::vector<int> p (pattern), out;
    for (int i = 0; i < n; ++i)
        out.push_back (p[static_cast<std::size_t> (i) % p.size()]);
    return out;
}

CrankVariant variant (std::string id, std::string name, std::vector<int> order, std::vector<double> intervals,
                      std::vector<int> bankOf, double nominalBank, BalanceInfo bal, std::string note = {})
{
    CrankVariant v;
    v.id = std::move (id);
    v.name = std::move (name);
    v.firingOrder = std::move (order);
    v.intervals = std::move (intervals);
    v.bankOf = std::move (bankOf);
    v.nominalBankAngle = nominalBank;
    v.balance = bal;
    v.note = std::move (note);
    return v;
}

LayoutDef layout (std::string id, std::string name, std::string family, int cyl, int banks, CycleType cycle)
{
    LayoutDef l;
    l.id = std::move (id);
    l.name = std::move (name);
    l.family = std::move (family);
    l.cylinders = cyl;
    l.banks = banks;
    l.cycle = cycle;
    return l;
}

LayoutDef radialLayout (int n)
{
    auto l = layout ("radial" + std::to_string (n), "Radial " + std::to_string (n), "Radial", n, 1, CycleType::FourStroke);
    l.radial = true;
    std::vector<int> order;
    for (int c = 1; c <= n; c += 2) order.push_back (c);
    for (int c = 2; c <= n; c += 2) order.push_back (c);
    l.cranks.push_back (variant ("single_pin", "Single crank pin, master rod", order, { 720.0 / n }, std::vector<int> (static_cast<std::size_t> (n), 0), 0.0,
                                 { 0.25f, 0.35f, 0.0f }, "Odd cylinder count fires every other cylinder: even firing."));
    return l;
}

std::vector<LayoutDef> buildLibrary()
{
    std::vector<LayoutDef> lib;
    using CT = CycleType;

    { // Motorcycle singles / twins
        auto l = layout ("single", "Single cylinder (4T)", "Motorcycle", 1, 1, CT::FourStroke);
        l.cranks.push_back (variant ("std", "Single", { 1 }, { 720 }, { 0 }, 0, { 1.0f, 0.35f, 0.0f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("single_2t", "Single cylinder (2T)", "Motorcycle", 1, 1, CT::TwoStroke);
        l.cranks.push_back (variant ("std", "Single", { 1 }, { 360 }, { 0 }, 0, { 1.0f, 0.35f, 0.0f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("twin_parallel", "Parallel twin", "Motorcycle", 2, 1, CT::FourStroke);
        l.cranks.push_back (variant ("360", "360 deg crank", { 1, 2 }, { 360, 360 }, { 0, 0 }, 0, { 1.0f, 0.4f, 0.0f }, "Fires like a single, even 360/360."));
        l.cranks.push_back (variant ("270", "270 deg crank", { 1, 2 }, { 270, 450 }, { 0, 0 }, 0, { 0.5f, 0.2f, 0.5f }, "V-twin like 270/450 rhythm."));
        l.cranks.push_back (variant ("180", "180 deg crank", { 1, 2 }, { 180, 540 }, { 0, 0 }, 0, { 0.0f, 0.5f, 0.8f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("twin_parallel_2t", "Parallel twin (2T)", "Motorcycle", 2, 1, CT::TwoStroke);
        l.cranks.push_back (variant ("180", "180 deg crank", { 1, 2 }, { 180, 180 }, { 0, 0 }, 0, { 0.0f, 0.5f, 0.8f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("twin_v", "V-twin", "Motorcycle", 2, 2, CT::FourStroke);
        l.bankAngleEditable = true;
        l.bankAngleMin = 30;
        l.bankAngleMax = 120;
        l.cranks.push_back (variant ("common_pin", "Common crank pin", { 1, 2 }, { 270, 450 }, { 1, 0 }, 90, { 0.2f, 0.3f, 0.1f },
                                     "Shared pin: at 90 deg it fires 270/450, at 45 deg 315/405 (geometry, not a preset)."));
        lib.push_back (l);
    }
    {
        auto l = layout ("twin_boxer", "Boxer twin", "Motorcycle", 2, 2, CT::FourStroke);
        l.boxer = true;
        l.cranks.push_back (variant ("std", "Opposed throws", { 1, 2 }, { 360, 360 }, { 0, 1 }, 0, { 0.0f, 0.0f, 0.5f }));
        lib.push_back (l);
    }
    { // Inline
        auto l = layout ("i3", "Inline 3", "Inline", 3, 1, CT::FourStroke);
        l.cranks.push_back (variant ("120", "120 deg crank", { 1, 2, 3 }, { 240 }, { 0, 0, 0 }, 0, { 0.0f, 0.0f, 0.7f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("i4", "Inline 4", "Inline", 4, 1, CT::FourStroke);
        l.cranks.push_back (variant ("flat", "Flat-plane (180 deg)", { 1, 3, 4, 2 }, { 180 }, { 0, 0, 0, 0 }, 0, { 0.0f, 0.8f, 0.0f }));
        l.cranks.push_back (variant ("crossplane", "Crossplane (90 deg)", { 1, 3, 2, 4 }, { 270, 180, 90, 180 }, { 0, 0, 0, 0 }, 0,
                                     { 0.0f, 0.0f, 0.6f }, "Uneven 270-180-90-180 rhythm (superbike style)."));
        lib.push_back (l);
    }
    {
        auto l = layout ("i5", "Inline 5", "Inline", 5, 1, CT::FourStroke);
        l.cranks.push_back (variant ("72", "72 deg crank", { 1, 2, 4, 5, 3 }, { 144 }, { 0, 0, 0, 0, 0 }, 0, { 0.0f, 0.0f, 0.4f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("i6", "Inline 6", "Inline", 6, 1, CT::FourStroke);
        l.cranks.push_back (variant ("120", "120 deg crank", { 1, 5, 3, 6, 2, 4 }, { 120 }, std::vector<int> (6, 0), 0, { 0.0f, 0.0f, 0.0f }, "Perfect primary and secondary balance."));
        lib.push_back (l);
    }
    { // V
        auto l = layout ("v6", "V6", "V", 6, 2, CT::FourStroke);
        l.bankAngleEditable = true;
        l.bankAngleMin = 45;
        l.bankAngleMax = 120;
        l.cranks.push_back (variant ("60_even", "60 deg V, 6 throws (even)", { 1, 2, 3, 4, 5, 6 }, { 120 }, repeatBanks ({ 0, 1 }, 6), 60, { 0.0f, 0.0f, 0.3f }));
        l.cranks.push_back (variant ("90_splitpin", "90 deg V, split pins (even)", { 1, 6, 5, 4, 3, 2 }, { 120 }, repeatBanks ({ 0, 1 }, 6), 90, { 0.0f, 0.1f, 0.4f }));
        l.cranks.push_back (variant ("90_oddfire", "90 deg V, common pins (odd-fire)", { 1, 6, 5, 4, 3, 2 }, { 90, 150 }, repeatBanks ({ 0, 1 }, 6), 90,
                                     { 0.0f, 0.1f, 0.5f }, "Classic 90/150 odd-fire rhythm."));
        lib.push_back (l);
    }
    {
        auto l = layout ("v8", "V8", "V", 8, 2, CT::FourStroke);
        l.bankAngleEditable = true;
        l.bankAngleMin = 45;
        l.bankAngleMax = 120;
        l.cranks.push_back (variant ("crossplane", "Cross-plane", { 1, 8, 4, 3, 6, 5, 7, 2 }, { 90 }, repeatBanks ({ 1, 0 }, 8), 90, { 0.0f, 0.0f, 0.0f },
                                     "Even overall, but each bank fires unevenly (the burble). Odd cylinders left bank."));
        l.cranks.push_back (variant ("flatplane", "Flat-plane", { 1, 5, 3, 7, 4, 8, 2, 6 }, { 90 }, { 0, 0, 0, 0, 1, 1, 1, 1 }, 90, { 0.0f, 0.55f, 0.0f },
                                     "Banks alternate: each bank fires evenly every 180 deg (scream)."));
        lib.push_back (l);
    }
    {
        auto l = layout ("v10", "V10", "V", 10, 2, CT::FourStroke);
        l.bankAngleEditable = true;
        l.bankAngleMin = 60;
        l.bankAngleMax = 120;
        l.cranks.push_back (variant ("72_even", "72 deg V, split pins (even)", { 1, 6, 5, 10, 2, 7, 3, 8, 4, 9 }, { 72 }, { 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 }, 72,
                                     { 0.0f, 0.0f, 0.2f }));
        l.cranks.push_back (variant ("90_oddfire", "90 deg V, common pins (odd-fire)", { 1, 10, 9, 4, 3, 6, 5, 8, 7, 2 }, { 54, 90 }, repeatBanks ({ 1, 0 }, 10), 90,
                                     { 0.0f, 0.1f, 0.3f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("v12", "V12", "V", 12, 2, CT::FourStroke);
        l.bankAngleEditable = true;
        l.bankAngleMin = 45;
        l.bankAngleMax = 120;
        l.cranks.push_back (variant ("60_even", "60 deg V, common pins", { 1, 7, 5, 11, 3, 9, 6, 12, 2, 8, 4, 10 }, { 60 },
                                     { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1 }, 60, { 0.0f, 0.0f, 0.0f }, "Inherently balanced."));
        lib.push_back (l);
    }
    { // Boxer
        auto l = layout ("boxer4", "Boxer 4 (flat-4)", "Boxer", 4, 2, CT::FourStroke);
        l.boxer = true;
        l.cranks.push_back (variant ("std", "Flat-4 crank", { 1, 3, 2, 4 }, { 180 }, { 0, 1, 0, 1 }, 0, { 0.0f, 0.0f, 0.4f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("boxer6", "Boxer 6 (flat-6)", "Boxer", 6, 2, CT::FourStroke);
        l.boxer = true;
        l.cranks.push_back (variant ("std", "Flat-6 crank", { 1, 6, 2, 4, 3, 5 }, { 120 }, { 0, 0, 0, 1, 1, 1 }, 0, { 0.0f, 0.0f, 0.0f }));
        lib.push_back (l);
    }
    // Radial
    lib.push_back (radialLayout (5));
    lib.push_back (radialLayout (7));
    lib.push_back (radialLayout (9));
    { // Wankel (rotors fire once per eccentric shaft revolution)
        auto l = layout ("wankel1", "Wankel 1-rotor", "Rotary", 1, 1, CT::Wankel);
        l.cranks.push_back (variant ("std", "Eccentric shaft", { 1 }, { 360 }, { 0 }, 0, { 0.0f, 0.0f, 0.0f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("wankel2", "Wankel 2-rotor", "Rotary", 2, 1, CT::Wankel);
        l.cranks.push_back (variant ("std", "Eccentric shaft 180 deg", { 1, 2 }, { 180 }, { 0, 0 }, 0, { 0.0f, 0.0f, 0.0f }));
        lib.push_back (l);
    }
    {
        auto l = layout ("wankel3", "Wankel 3-rotor", "Rotary", 3, 1, CT::Wankel);
        l.cranks.push_back (variant ("std", "Eccentric shaft 120 deg", { 1, 2, 3 }, { 120 }, { 0, 0, 0 }, 0, { 0.0f, 0.0f, 0.0f }));
        lib.push_back (l);
    }
    return lib;
}

std::vector<double> axesFor (const LayoutDef& l, const CrankVariant& v, double bankAngle)
{
    std::vector<double> axes (static_cast<std::size_t> (l.cylinders), 0.0);
    for (int c = 0; c < l.cylinders; ++c)
    {
        const int bank = c < static_cast<int> (v.bankOf.size()) ? v.bankOf[static_cast<std::size_t> (c)] : 0;
        if (l.radial)
            axes[static_cast<std::size_t> (c)] = c * 360.0 / l.cylinders;
        else if (l.boxer)
            axes[static_cast<std::size_t> (c)] = bank * 180.0;
        else
            axes[static_cast<std::size_t> (c)] = bank * bankAngle;
    }
    return axes;
}

std::vector<double> derivePins (const LayoutDef& l, const CrankVariant& v)
{
    const auto axes = axesFor (l, v, v.nominalBankAngle);
    std::vector<double> pins (static_cast<std::size_t> (l.cylinders), 0.0);
    double a = 0.0;
    for (std::size_t i = 0; i < v.firingOrder.size(); ++i)
    {
        const auto c = static_cast<std::size_t> (v.firingOrder[i] - 1);
        pins[c] = wrapDeg (axes[c] - a, 360.0);
        a += v.intervals[i % v.intervals.size()];
    }
    return pins;
}

bool isPermutation (const std::vector<int>& order, int n)
{
    if (static_cast<int> (order.size()) != n) return false;
    std::vector<bool> seen (static_cast<std::size_t> (n), false);
    for (int c : order)
    {
        if (c < 1 || c > n || seen[static_cast<std::size_t> (c - 1)]) return false;
        seen[static_cast<std::size_t> (c - 1)] = true;
    }
    return true;
}
} // namespace

const std::vector<LayoutDef>& layoutLibrary()
{
    static const std::vector<LayoutDef> lib = buildLibrary();
    return lib;
}

const LayoutDef* findLayout (const std::string& id)
{
    for (auto& l : layoutLibrary())
        if (l.id == id) return &l;
    return nullptr;
}

const CrankVariant* findCrank (const LayoutDef& l, const std::string& crankId)
{
    for (auto& c : l.cranks)
        if (c.id == crankId) return &c;
    return l.cranks.empty() ? nullptr : &l.cranks.front();
}

FiringPlan computeFiringPlan (const LayoutDef& l, const CrankVariant& v, double bankAngle, const std::vector<int>& firingOrderIn)
{
    FiringPlan plan;
    plan.cycleDeg = cycleDegrees (l.cycle);
    if (bankAngle <= 0.0 || ! l.bankAngleEditable) bankAngle = v.nominalBankAngle;

    const auto& order = firingOrderIn.empty() ? v.firingOrder : firingOrderIn;
    if (! isPermutation (order, l.cylinders))
    {
        plan.error = "The firing order must list every cylinder 1.." + std::to_string (l.cylinders) + " exactly once.";
        return plan;
    }

    plan.pinDeg = derivePins (l, v);
    plan.axisDeg = axesFor (l, v, bankAngle);
    std::vector<double> tdc (static_cast<std::size_t> (l.cylinders));
    for (std::size_t c = 0; c < tdc.size(); ++c)
        tdc[c] = wrapDeg (plan.axisDeg[c] - plan.pinDeg[c], 360.0);

    constexpr double eps = 1e-6;
    const auto first = static_cast<std::size_t> (order[0] - 1);
    const double a0 = tdc[first];
    double prev = a0;
    std::vector<double> angles { a0 };
    for (std::size_t i = 1; i < order.size(); ++i)
    {
        double cand = tdc[static_cast<std::size_t> (order[i] - 1)];
        while (cand <= prev + eps) cand += 360.0;
        angles.push_back (cand);
        prev = cand;
    }
    // Earliest-possible placement: every cylinder must fire once inside [a0, a0 + cycle).
    // If even the earliest placement overflows the cycle, no cam phasing can produce this order.
    const double close = a0 + plan.cycleDeg;
    if (prev >= close - eps)
    {
        double needed = prev - a0;
        while (needed < prev - a0 + eps) needed += 360.0;
        plan.error = "Firing order " + firingOrderToString (order) + " is not possible with this crank: it would need more than "
                     + std::to_string (static_cast<int> (plan.cycleDeg)) + " deg (a cylinder would have to skip a cycle).";
        return plan;
    }

    for (std::size_t i = 0; i < order.size(); ++i)
    {
        FiringEvent e;
        e.cylinder = order[i] - 1;
        e.bank = static_cast<std::size_t> (e.cylinder) < v.bankOf.size() ? v.bankOf[static_cast<std::size_t> (e.cylinder)] : 0;
        e.angleDeg = angles[i] - a0;
        plan.events.push_back (e);
        const double next = (i + 1 < order.size() ? angles[i + 1] : close);
        plan.intervals.push_back (next - angles[i]);
    }
    plan.order = order;
    plan.valid = true;
    return plan;
}

std::vector<std::vector<int>> possibleFiringOrders (const LayoutDef& l, const CrankVariant& v, double bankAngle, std::size_t maxCount)
{
    std::vector<std::vector<int>> result;
    const int n = l.cylinders;
    if (n <= 0) return result;
    if (bankAngle <= 0.0 || ! l.bankAngleEditable) bankAngle = v.nominalBankAngle;

    const auto pins = derivePins (l, v);
    const auto axes = axesFor (l, v, bankAngle);
    std::vector<double> tdc (static_cast<std::size_t> (n));
    for (int c = 0; c < n; ++c)
        tdc[static_cast<std::size_t> (c)] = wrapDeg (axes[static_cast<std::size_t> (c)] - pins[static_cast<std::size_t> (c)] - (axes[0] - pins[0]), 360.0);

    const bool fourStroke = l.cycle == CycleType::FourStroke;
    const int bits = fourStroke ? n - 1 : 0;
    if (bits > 16) return { v.firingOrder };

    struct Cand { std::vector<int> order; double maxGap; };
    std::vector<Cand> cands;
    std::set<std::vector<int>> seen;
    for (uint32_t mask = 0; mask < (1u << bits); ++mask)
    {
        std::vector<std::pair<double, int>> fa;
        fa.emplace_back (0.0, 1);
        for (int c = 1; c < n; ++c)
        {
            const bool shift = fourStroke && ((mask >> (c - 1)) & 1u);
            fa.emplace_back (tdc[static_cast<std::size_t> (c)] + (shift ? 360.0 : 0.0), c + 1);
        }
        std::sort (fa.begin(), fa.end());
        bool collision = false;
        for (std::size_t i = 1; i < fa.size(); ++i)
            if (std::abs (fa[i].first - fa[i - 1].first) < 1e-6) collision = true;
        if (collision) continue;
        std::vector<int> order;
        double maxGap = 0.0;
        const double cyc = cycleDegrees (l.cycle);
        for (std::size_t i = 0; i < fa.size(); ++i)
        {
            order.push_back (fa[i].second);
            const double next = i + 1 < fa.size() ? fa[i + 1].first : fa[0].first + cyc;
            maxGap = std::max (maxGap, next - fa[i].first);
        }
        if (seen.insert (order).second)
            cands.push_back ({ order, maxGap });
    }
    std::stable_sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.maxGap < b.maxGap - 1e-6; });

    // reference order first when it is valid at this bank angle
    if (computeFiringPlan (l, v, bankAngle, v.firingOrder).valid)
        result.push_back (v.firingOrder);
    for (auto& c : cands)
    {
        if (result.size() >= maxCount) break;
        if (std::find (result.begin(), result.end(), c.order) == result.end())
            result.push_back (c.order);
    }
    return result;
}

std::string firingOrderToString (const std::vector<int>& order)
{
    std::ostringstream s;
    for (std::size_t i = 0; i < order.size(); ++i)
        s << (i ? "-" : "") << order[i];
    return s.str();
}

std::vector<int> parseFiringOrder (const std::string& text)
{
    std::vector<int> out;
    int cur = -1;
    for (char ch : text)
    {
        if (ch >= '0' && ch <= '9')
            cur = (cur < 0 ? 0 : cur * 10) + (ch - '0');
        else if (ch == '-' || ch == ' ' || ch == ',' || ch == '\t')
        {
            if (cur >= 0) out.push_back (cur);
            cur = -1;
        }
        else
            return {};
    }
    if (cur >= 0) out.push_back (cur);
    return out;
}
} // namespace esd
