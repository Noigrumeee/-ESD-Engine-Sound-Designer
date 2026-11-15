#pragma once
// Engine architectures and crank geometry.
//
// MODEL (mechanical, simplified but exact for the kinematics it covers):
//  * every cylinder has a bank axis angle (inline 0, V: 0 / bank angle, boxer 0 / 180,
//    radial k*360/N) and a crank pin angle;
//  * a cylinder is at top dead centre when  crank angle = axis - pin  (mod 360);
//  * a 4-stroke cylinder fires on one of its two TDCs per 720 deg cycle (cam phasing),
//    a 2-stroke / Wankel rotor fires on every revolution (360 deg cycle).
// Each crank variant is defined by its reference firing order and intervals at its nominal
// bank angle; the pin angles are derived from that (and match the real shared-pin cranks for
// the variants listed, see tests). Changing the bank angle then recomputes the real firing
// intervals, and any user firing order is validated against the geometry.
#include <string>
#include <vector>

namespace esd
{
enum class CycleType
{
    FourStroke,
    TwoStroke,
    Wankel
};

const char* toString (CycleType);
double cycleDegrees (CycleType);

/// Qualitative free forces / couples of the crank (0 = balanced, 1 = strong). Table based
/// approximation from textbook balance analysis; drives the vibration layer.
struct BalanceInfo
{
    float primary = 0.0f;
    float secondary = 0.0f;
    float couple = 0.0f;
};

struct CrankVariant
{
    std::string id;
    std::string name;
    std::vector<int> firingOrder;   ///< 1-based cylinder numbers, reference order
    std::vector<double> intervals;  ///< degrees between consecutive firings (repeated), at nominalBankAngle
    std::vector<int> bankOf;        ///< bank index of each cylinder (0-based cylinder index)
    double nominalBankAngle = 0.0;
    BalanceInfo balance;
    std::string note;
};

struct LayoutDef
{
    std::string id;
    std::string name;
    std::string family; ///< Inline, V, Boxer, Radial, Rotary, Motorcycle
    int cylinders = 4;  ///< rotors for a Wankel
    int banks = 1;
    CycleType cycle = CycleType::FourStroke;
    bool boxer = false;
    bool radial = false;
    bool bankAngleEditable = false;
    double bankAngleMin = 0.0, bankAngleMax = 0.0;
    std::vector<CrankVariant> cranks;
};

const std::vector<LayoutDef>& layoutLibrary();
const LayoutDef* findLayout (const std::string& id);
const CrankVariant* findCrank (const LayoutDef&, const std::string& crankId);

struct FiringEvent
{
    int cylinder = 0; ///< 0-based
    int bank = 0;
    double angleDeg = 0.0; ///< firing TDC angle inside the cycle, first event at 0
};

struct FiringPlan
{
    bool valid = false;
    std::string error;
    double cycleDeg = 720.0;
    std::vector<FiringEvent> events; ///< in firing order
    std::vector<int> order;          ///< 1-based
    std::vector<double> intervals;   ///< event k -> k+1 (last wraps)
    std::vector<double> pinDeg;      ///< per cylinder
    std::vector<double> axisDeg;     ///< per cylinder
    bool evenFiring() const;
    double maxInterval() const;
    double minInterval() const;
};

/// Computes the firing plan. An empty firingOrder means "reference order of the variant".
/// bankAngle <= 0 means "nominal bank angle of the variant".
FiringPlan computeFiringPlan (const LayoutDef&, const CrankVariant&, double bankAngle, const std::vector<int>& firingOrder);

/// Every distinct firing order the crank can mechanically produce (cam phasing choices),
/// most even first. Capped at maxCount.
std::vector<std::vector<int>> possibleFiringOrders (const LayoutDef&, const CrankVariant&, double bankAngle, std::size_t maxCount = 48);

std::string firingOrderToString (const std::vector<int>&);
/// Parses "1-3-4-2" (also accepts spaces / commas). Returns empty on syntax error.
std::vector<int> parseFiringOrder (const std::string&);
} // namespace esd
