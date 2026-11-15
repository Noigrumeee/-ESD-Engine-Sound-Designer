#pragma once
// Parameter registry: one description per editable parameter (range, unit, help, and the
// modelling level it belongs to). The UI builds its editors from it and sanitize() clamps
// with it, so ranges are defined in exactly one place.
#include "Project.h"

#include <functional>
#include <string>
#include <vector>

namespace esd
{
/// Which of the three modelling levels a parameter belongs to (see docs/MODEL.md).
enum class ParamTag
{
    Mechanical,    ///< enters the simplified mechanical model (kinematics, torque, timing)
    Approximation, ///< parameter of a simplified physical approximation (acoustics of pipes...)
    Acoustic       ///< pure sound-design control, no physical meaning claimed
};
const char* toString (ParamTag);

struct ParamDef
{
    enum class Kind { Number, Choice, Toggle };

    std::string id;
    std::string group;
    std::string label;
    std::string unit;
    std::string help;
    ParamTag tag = ParamTag::Mechanical;
    Kind kind = Kind::Number;
    double min = 0.0, max = 1.0, step = 0.0;
    bool logScale = false;
    int decimals = 0;
    std::vector<std::string> choices;
    std::function<double (const Project&)> get;
    std::function<void (Project&, double)> set;
    std::function<bool (const Project&)> visible; ///< optional
};

const std::vector<ParamDef>& paramRegistry();
const ParamDef* findParam (const std::string& id);
/// Parameters of a group; includeSubgroups also returns "group.sub" groups.
std::vector<const ParamDef*> paramsInGroup (const std::string& groupPrefix, bool includeSubgroups = true);
} // namespace esd
