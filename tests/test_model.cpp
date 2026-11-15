#include "base/Curve.h"
#include "library/ProjectLibrary.h"
#include "model/Params.h"
#include "model/Presets.h"
#include "model/Serialization.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace esd;
using Catch::Approx;

TEST_CASE ("smooth curves never overshoot their control points", "[curve]")
{
    Curve c ({ { 0, 0 }, { 1, 1 }, { 2, 1 }, { 3, 0.2 }, { 4, 0.2 } }, CurveInterp::Smooth);
    for (int i = 0; i <= 400; ++i)
    {
        const double y = c.eval (i * 0.01);
        CHECK (y >= -1e-9);
        CHECK (y <= 1.0 + 1e-9);
    }
    CHECK (c.eval (1.0) == Approx (1.0));
    CHECK (c.eval (-5) == Approx (0.0));
    CHECK (c.eval (10) == Approx (0.2));
    Curve lin ({ { 0, 0 }, { 10, 20 } }, CurveInterp::Linear);
    CHECK (lin.eval (2.5) == Approx (5.0));
    CurveTable t;
    t.build (lin, 0, 10);
    CHECK (t.eval (2.5) == Approx (5.0).margin (1e-4));
}

TEST_CASE ("curve editing keeps points sorted", "[curve]")
{
    Curve c ({ { 0, 0 }, { 5, 5 }, { 10, 0 } });
    const auto idx = c.addPoint ({ 7, 3 });
    CHECK (idx == 2);
    c.movePoint (1, { 20, 1 }); // cannot cross its neighbour
    CHECK (c.points()[1].x < c.points()[2].x);
    c.removePoint (0);
    CHECK (c.points().size() == 3);
}

TEST_CASE ("displacement is computed from bore, stroke and cylinders", "[model]")
{
    ThermalEngineSpec e;
    e.layoutId = "i4";
    e.boreMm = 86;
    e.strokeMm = 86;
    CHECK (e.displacementCc() == Approx (1998.2).margin (0.5));
    e.setDisplacementCc (2500);
    CHECK (e.displacementCc() == Approx (2500).margin (0.1));
    CHECK (e.boreStrokeRatio() == Approx (1.0));
}

TEST_CASE ("every preset is valid and survives a JSON round trip", "[model][json]")
{
    for (auto& info : builtinPresets())
    {
        INFO (info.id);
        Project p = makePreset (info.id);
        for (auto& issue : validate (p))
            CHECK (issue.severity != Severity::Error);
        const auto text = projectToJson (p);
        auto r = projectFromJson (text);
        REQUIRE (r.ok);
        CHECK (r.project == p);
    }
}

TEST_CASE ("loading is tolerant: garbage, missing keys, invalid values", "[json]")
{
    CHECK_FALSE (projectFromJson ("{not json").ok);
    CHECK_FALSE (projectFromJson ("{\"format\":\"other\"}").ok);

    auto r = projectFromJson (R"({"format":"EngineSoundDesigner.project","engine":{"layout":"v8","crank":"crossplane",
        "firing_order":[1,2,3,4,5,6,7,8],"bore_mm":9999,"idle_rpm":"oops"}})");
    REQUIRE (r.ok);
    CHECK (r.project.engine.layoutId == "v8");
    CHECK (r.project.engine.firingOrder.empty());        // impossible order reset
    CHECK (r.project.engine.boreMm == Approx (160.0));   // clamped to the registered range
    CHECK (r.project.engine.idleRpm == Approx (850.0));  // wrong type -> default kept
    CHECK (r.project.engine.torqueCurve.points().size() >= 2); // estimated
    CHECK_FALSE (r.fixes.empty());
}

TEST_CASE ("project files are written atomically and read back", "[json]")
{
    const auto dir = std::filesystem::temp_directory_path() / "esd_test_files";
    std::filesystem::create_directories (dir);
    const auto path = (dir / "v8.esdproj").string();
    Project p = makePreset ("v8_cross");
    REQUIRE (saveProjectFile (p, path));
    auto r = loadProjectFile (path);
    REQUIRE (r.ok);
    CHECK (r.project == p);
    CHECK_FALSE (loadProjectFile ((dir / "missing.esdproj").string()).ok);
}

TEST_CASE ("parameter registry covers ranges used by sanitize", "[params]")
{
    REQUIRE (findParam ("engine.bore") != nullptr);
    Project p = makePreset ("i4_na");
    const auto* d = findParam ("exhaust.midpipeLength");
    REQUIRE (d);
    d->set (p, 1234.0);
    CHECK (p.exhaust.midpipeLengthMm == Approx (1234.0));
    const auto* ch = findParam ("exhaust.muffler");
    ch->set (p, 3.0);
    CHECK (p.exhaust.muffler == MufflerType::Turbo);
    CHECK_FALSE (paramsInGroup ("exhaust").empty());
}

TEST_CASE ("SQLite library indexes projects", "[library]")
{
    const auto dir = std::filesystem::temp_directory_path() / "esd_test_lib";
    std::filesystem::remove_all (dir);
    ProjectLibrary lib;
    REQUIRE (lib.open ((dir / "library.db").string()));
    LibraryEntry e { "id1", "My V8", "V", "thermal", (dir / "a.esdproj").string(), "", 10, 0, false };
    REQUIRE (lib.upsert (e));
    e.name = "My V8 renamed";
    REQUIRE (lib.upsert (e));
    CHECK (lib.all().size() == 1);
    CHECK (lib.all()[0].name == "My V8 renamed");
    CHECK (lib.recent (5).empty());
    lib.markOpened ("id1", 100);
    CHECK (lib.recent (5).size() == 1);
    CHECK (lib.pruneMissingFiles() == 1); // file does not exist
    CHECK (lib.all().empty());
}
