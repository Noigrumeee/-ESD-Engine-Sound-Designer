# esd_core: everything that does not depend on JUCE (model, simulation, DSP, synthesis,
# rendering, library). Reusable as the base of a future native runtime library.
add_library(esd_core STATIC
    base/Curve.cpp
    model/EngineLayout.cpp
    model/Params.cpp
    model/Project.cpp
    model/Specs.cpp
    model/Presets.cpp
    model/Serialization.cpp
    sim/EngineSim.cpp
    sim/ElectricSim.cpp
    sim/Drivetrain.cpp
    dsp/Filters.cpp
    dsp/Tube.cpp
    dsp/Effects.cpp
    synth/LayerBus.cpp
    synth/ThermalVoice.cpp
    synth/ElectricVoice.cpp
    synth/TransmissionVoice.cpp
    synth/SampleLayer.cpp
    synth/EngineRuntime.cpp
    render/Render.cpp
    render/AudioFile.cpp
    render/LoopGenerator.cpp
    render/GameExport.cpp
    library/ProjectLibrary.cpp
)
target_include_directories(esd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(esd_core PUBLIC nlohmann_json::nlohmann_json PRIVATE esd_sqlite)
target_compile_features(esd_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(esd_core PRIVATE /W4 /permissive- /wd4100)
endif()
