#pragma once
#include "Project.h"

#include <string>
#include <vector>

namespace esd
{
struct PresetInfo
{
    std::string id;
    std::string name;
    std::string category;
    std::string description;
};

/// Built-in demo engines. Every preset is a complete, editable project.
const std::vector<PresetInfo>& builtinPresets();
/// Returns a sanitized project (fresh id). Unknown id -> Inline 4 NA.
Project makePreset (const std::string& id);
} // namespace esd
