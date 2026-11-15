#pragma once
#include "Specs.h"

#include <cstdint>
#include <string>
#include <vector>

namespace esd
{
inline constexpr int kProjectSchemaVersion = 2;

enum class PowertrainKind { Thermal, Electric, Hybrid };
const char* toString (PowertrainKind);

/// A sample imported into the project pool (audio stays on disk, the project stores a reference).
struct SampleRef
{
    std::string id;
    std::string name;
    std::string path;      ///< absolute, or relative to the project file
    std::string category;  ///< e.g. "exhaust", "intake", "idle"
    double rpm = 0.0;      ///< mapping (0 = not mapped)
    double load = 0.0;
    double startSec = 0.0, endSec = 0.0; ///< trimmed region (end 0 = whole file)
    bool loop = false;
    float gainDb = 0.0f;
    bool operator== (const SampleRef&) const = default;
};

struct Project
{
    int schemaVersion = kProjectSchemaVersion;
    std::string id;
    std::string name = "Untitled engine";
    std::string category = "Custom";
    std::string description;
    PowertrainKind kind = PowertrainKind::Thermal;
    uint64_t seed = 12345;

    ThermalEngineSpec engine;
    IntakeSpec intake;
    ExhaustSpec exhaust;
    BoostSpec boost;
    ElectricSpec electric;
    HybridSpec hybrid;
    TransmissionSpec transmission;
    VehicleSpec vehicle;
    LoopSpec loops;
    SoundMix mix;
    std::vector<SampleRef> samples;

    bool operator== (const Project&) const = default;
};

enum class Severity { Info, Warning, Error };
struct ValidationIssue
{
    Severity severity = Severity::Warning;
    std::string field;
    std::string message;
};

/// Clamps every numeric value into its registered range and fixes inconsistent values
/// (limiter above redline, missing curves, invalid firing order...). Returns what was changed.
std::vector<ValidationIssue> sanitize (Project&);
/// Non-modifying check (warnings about plausibility, invalid firing order...).
std::vector<ValidationIssue> validate (const Project&);

/// Default WOT brake torque curve estimated from displacement and induction:
/// BMEP ~ 12 bar NA gasoline, ~20 bar boosted, 9 bar diesel NA; shaped by rpm range.
/// This is an APPROXIMATION used only as a starting point - the user edits the curve.
Curve estimateTorqueCurve (const ThermalEngineSpec&, const BoostSpec&);
Curve defaultThrottleMap();

std::string newUuid();
} // namespace esd
