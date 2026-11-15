add_executable(esd_render esd_render.cpp)
target_link_libraries(esd_render PRIVATE esd_core)
add_custom_command(TARGET esd_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/templates $<TARGET_FILE_DIR:esd_render>/templates)
