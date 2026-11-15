add_executable(esd_tests test_layout.cpp test_model.cpp test_sim_audio.cpp test_phases.cpp)
target_link_libraries(esd_tests PRIVATE esd_core Catch2::Catch2WithMain)
target_compile_definitions(esd_tests PRIVATE ESD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME esd_tests COMMAND esd_tests)
