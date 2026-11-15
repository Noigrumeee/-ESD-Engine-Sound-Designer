#include "model/EngineLayout.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>

using namespace esd;
using Catch::Approx;

static FiringPlan plan (const char* layout, const char* crank, double bank = 0.0, std::vector<int> order = {})
{
    const auto* l = findLayout (layout);
    REQUIRE (l != nullptr);
    const auto* c = findCrank (*l, crank);
    REQUIRE (c != nullptr);
    return computeFiringPlan (*l, *c, bank, order);
}

/// Intervals between consecutive firings of one bank only.
static std::vector<double> bankIntervals (const FiringPlan& p, int bank)
{
    std::vector<double> a;
    for (auto& e : p.events)
        if (e.bank == bank) a.push_back (e.angleDeg);
    std::vector<double> out;
    for (std::size_t i = 0; i < a.size(); ++i)
        out.push_back ((i + 1 < a.size() ? a[i + 1] : a[0] + p.cycleDeg) - a[i]);
    return out;
}

TEST_CASE ("every built-in crank produces its reference order and intervals", "[layout]")
{
    for (auto& l : layoutLibrary())
        for (auto& c : l.cranks)
        {
            INFO (l.id << " / " << c.id);
            const auto p = computeFiringPlan (l, c, 0.0, {});
            REQUIRE (p.valid);
            CHECK (p.order == c.firingOrder);
            CHECK (std::accumulate (p.intervals.begin(), p.intervals.end(), 0.0) == Approx (p.cycleDeg));
            for (std::size_t i = 0; i < p.intervals.size(); ++i)
                CHECK (p.intervals[i] == Approx (c.intervals[i % c.intervals.size()]).margin (1e-6));
        }
}

TEST_CASE ("inline 4 flat crank fires every 180 deg and rejects impossible orders", "[layout]")
{
    auto p = plan ("i4", "flat");
    CHECK (p.evenFiring());
    CHECK (p.maxInterval() == Approx (180.0));
    auto bad = plan ("i4", "flat", 0.0, { 1, 2, 3, 4 });
    CHECK_FALSE (bad.valid);
    CHECK_FALSE (bad.error.empty());
    auto alt = plan ("i4", "flat", 0.0, { 1, 2, 4, 3 }); // the other valid I4 order
    CHECK (alt.valid);
    CHECK (alt.evenFiring());
}

TEST_CASE ("V8 cross-plane is even overall but uneven per bank; flat-plane is even per bank", "[layout]")
{
    auto cross = plan ("v8", "crossplane");
    REQUIRE (cross.valid);
    CHECK (cross.evenFiring());
    auto left = bankIntervals (cross, 0);
    auto sorted = left;
    std::sort (sorted.begin(), sorted.end());
    CHECK (sorted.front() == Approx (90.0));   // two cylinders of the same bank 90 deg apart
    CHECK (sorted.back() == Approx (270.0));   // ... and a 270 deg gap: the burble

    auto flat = plan ("v8", "flatplane");
    REQUIRE (flat.valid);
    for (double iv : bankIntervals (flat, 0)) CHECK (iv == Approx (180.0));
    for (double iv : bankIntervals (flat, 1)) CHECK (iv == Approx (180.0));
}

TEST_CASE ("shared crank pins are reproduced by the derived geometry", "[layout]")
{
    auto cross = plan ("v8", "crossplane");
    for (int pair = 0; pair < 4; ++pair) // GM numbering: (1,2) (3,4) (5,6) (7,8) share a throw
        CHECK (cross.pinDeg[static_cast<std::size_t> (2 * pair)] == Approx (cross.pinDeg[static_cast<std::size_t> (2 * pair + 1)]));
    auto v12 = plan ("v12", "60_even");
    for (int k = 0; k < 6; ++k)
        CHECK (v12.pinDeg[static_cast<std::size_t> (k)] == Approx (v12.pinDeg[static_cast<std::size_t> (k + 6)]));
}

TEST_CASE ("changing the bank angle changes the real firing intervals", "[layout]")
{
    auto v90 = plan ("twin_v", "common_pin", 90.0);
    REQUIRE (v90.valid);
    CHECK (v90.intervals[0] == Approx (270.0));
    auto v45 = plan ("twin_v", "common_pin", 45.0);
    REQUIRE (v45.valid);
    CHECK (v45.intervals[0] == Approx (315.0)); // the 45 deg V-twin rhythm comes out of the geometry
    CHECK (v45.intervals[1] == Approx (405.0));

    auto v8at60 = plan ("v8", "crossplane", 60.0);
    REQUIRE (v8at60.valid);
    CHECK_FALSE (v8at60.evenFiring()); // a cross-plane crank in a 60 deg block is uneven
}

TEST_CASE ("possible firing orders are enumerated from the geometry", "[layout]")
{
    const auto* l = findLayout ("v8");
    const auto* c = findCrank (*l, "crossplane");
    auto orders = possibleFiringOrders (*l, *c, 90.0);
    REQUIRE_FALSE (orders.empty());
    CHECK (orders.front() == c->firingOrder);
    const std::vector<int> ls { 1, 8, 7, 2, 6, 5, 4, 3 }; // GM LS order, same crank, different cam
    CHECK (std::find (orders.begin(), orders.end(), ls) != orders.end());
    for (auto& o : orders)
        CHECK (computeFiringPlan (*l, *c, 90.0, o).valid);
}

TEST_CASE ("radial and rotary layouts", "[layout]")
{
    auto r9 = plan ("radial9", "single_pin");
    REQUIRE (r9.valid);
    CHECK (r9.evenFiring());
    CHECK (r9.intervals[0] == Approx (80.0));
    auto w2 = plan ("wankel2", "std");
    REQUIRE (w2.valid);
    CHECK (w2.cycleDeg == Approx (360.0));
    CHECK (w2.intervals[0] == Approx (180.0));
}

TEST_CASE ("firing order text parsing", "[layout]")
{
    CHECK (parseFiringOrder ("1-3-4-2") == std::vector<int> { 1, 3, 4, 2 });
    CHECK (parseFiringOrder ("1, 8 4 3") == std::vector<int> { 1, 8, 4, 3 });
    CHECK (parseFiringOrder ("1-x-2").empty());
    CHECK (firingOrderToString ({ 1, 5, 3, 6, 2, 4 }) == "1-5-3-6-2-4");
}
