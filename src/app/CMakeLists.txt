juce_add_gui_app(EngineSoundDesignerApp
    PRODUCT_NAME "Engine Sound Designer"
    COMPANY_NAME "EngineSoundDesigner"
    VERSION ${PROJECT_VERSION}
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

juce_generate_juce_header(EngineSoundDesignerApp)

target_sources(EngineSoundDesignerApp PRIVATE
    Main.cpp
    Theme.cpp
    ProjectDocument.cpp
    AppContext.cpp
    audio/AudioHost.cpp
    widgets/ParamEditors.cpp
    widgets/CurveEditor.cpp
    widgets/Visualizers.cpp
    pages/HomePage.cpp
    pages/EngineBuilderPage.cpp
    pages/ExhaustPage.cpp
    pages/SimulatorPage.cpp
    pages/SoundLabPage.cpp
    pages/SampleEditorPage.cpp
    pages/ExportPage.cpp
    pages/PlannedPage.cpp
    pages/ElectricPage.cpp
    pages/LoopGeneratorPage.cpp)

target_include_directories(EngineSoundDesignerApp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(EngineSoundDesignerApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:EngineSoundDesignerApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:EngineSoundDesignerApp,JUCE_VERSION>"
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=0
    JUCE_ASIO=$<BOOL:${ESD_ENABLE_ASIO}>)

if(ESD_ENABLE_ASIO)
    if(NOT EXISTS "${ESD_ASIO_SDK_DIR}/common/iasiodrv.h")
        message(FATAL_ERROR "ESD_ENABLE_ASIO is ON but ESD_ASIO_SDK_DIR does not contain common/iasiodrv.h")
    endif()
    target_include_directories(EngineSoundDesignerApp PRIVATE "${ESD_ASIO_SDK_DIR}/common")
endif()

target_link_libraries(EngineSoundDesignerApp PRIVATE
    esd_core
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# integration templates (Godot / Unity / Unreal) next to the executable
add_custom_command(TARGET EngineSoundDesignerApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/templates $<TARGET_FILE_DIR:EngineSoundDesignerApp>/templates)
