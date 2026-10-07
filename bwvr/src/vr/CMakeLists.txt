# The VR output layer. Shared by the standalone test and, later, the mod.
add_library(bwvr_vr STATIC
    xr_screen.cpp
)

set(OPENXR_SDK "${BWVR_THIRD_PARTY}/OpenXR-SDK")
set(OPENXR_LIB "${OPENXR_SDK}/build32/src/loader/Release/openxr_loader.lib")
set(OPENXR_DLL "${OPENXR_SDK}/build32/src/loader/Release/openxr_loader.dll"
    CACHE FILEPATH "The 32 bit OpenXR loader to ship beside the game")

if(NOT EXISTS "${OPENXR_LIB}")
  message(FATAL_ERROR
      "32 bit OpenXR loader not found at ${OPENXR_LIB}.\n"
      "It is part of the repo, in the folder third_party. Check that the "
      "clone is complete. third_party/README.md says how it was built.")
endif()

target_include_directories(bwvr_vr PUBLIC "${OPENXR_SDK}/include")
# The loader is a DLL. The OpenXR SDK forces a dynamic CRT on its static
# library build, which clashes with the static CRT the mod needs, and the DLL
# build carries its own static CRT instead. One extra file to ship.
target_link_libraries(bwvr_vr PUBLIC
    "${OPENXR_LIB}"
    d3d11 dxgi
)

if(MSVC)
  target_compile_options(bwvr_vr PRIVATE /W4 /permissive-)
endif()
