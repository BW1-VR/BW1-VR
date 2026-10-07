# The Phase 0 probe. Builds a pass-through ddraw.dll that measures which draw
# path the game uses, then gets out of the way.
add_library(bwvr_probe SHARED
    ../proxy/ddraw_proxy.cpp
    probe.cpp
    camera_poke.cpp
    device_hook.cpp
    tech_camera.cpp
    frame_grab.cpp
    draw_phase.cpp
    ../proxy/ddraw.def
)

set_target_properties(bwvr_probe PROPERTIES
    OUTPUT_NAME "ddraw"
    PREFIX ""
)

target_link_libraries(bwvr_probe PRIVATE minhook)

if(MSVC)
  target_compile_options(bwvr_probe PRIVATE /W4 /permissive-)
  # The naked forwarding thunks in ddraw_proxy.cpp read a global through
  # inline assembly, which /GS instrumentation would disturb.
  set_source_files_properties(../proxy/ddraw_proxy.cpp PROPERTIES COMPILE_OPTIONS "/GS-")
endif()
