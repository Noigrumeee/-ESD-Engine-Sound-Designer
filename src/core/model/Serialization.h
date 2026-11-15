#pragma once
// JSON (de)serialization of projects. Unknown keys are ignored and missing keys keep their
// defaults, so older/newer files load; sanitize() is applied after loading.
#include "Project.h"

#include <string>
#include <vector>

namespace esd
{
std::string projectToJson (const Project&, int indent = 2);

struct LoadResult
{
    bool ok = false;
    std::string error;
    Project project;
    std::vector<ValidationIssue> fixes;
};

LoadResult projectFromJson (const std::string& json);

/// File helpers (UTF-8 paths). Saving is atomic: write to a temp file then rename.
bool saveProjectFile (const Project&, const std::string& path, std::string* error = nullptr);
LoadResult loadProjectFile (const std::string& path);

std::string curveToJson (const Curve&);
} // namespace esd
