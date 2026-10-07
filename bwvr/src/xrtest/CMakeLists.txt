# Standalone check that the VR output chain works, with no game involved.
add_executable(bwvr_xrtest main.cpp)
set_target_properties(bwvr_xrtest PROPERTIES OUTPUT_NAME "bwvr-xrtest")
target_link_libraries(bwvr_xrtest PRIVATE bwvr_vr)
if(MSVC)
  target_compile_options(bwvr_xrtest PRIVATE /W4 /permissive-)
endif()

# The test has to find the loader, so put it next to the executable.
add_custom_command(TARGET bwvr_xrtest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENXR_DLL}" "$<TARGET_FILE_DIR:bwvr_xrtest>"
    COMMENT "Copying the 32 bit OpenXR loader beside the test"
)
