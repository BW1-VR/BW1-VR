# The launcher.
#
# A plain Win32 program with no dependencies beyond Windows, so it is one file
# to ship and nothing for anyone to install. It is built 32 bit like everything
# else here only because the whole project is configured that way. Nothing it
# does needs to be.
add_executable(bwvr_launcher WIN32 launcher.cpp launcher.rc)
set_target_properties(bwvr_launcher PROPERTIES
    OUTPUT_NAME "Black & White VR Mod")
# The Vulkan headers alone: the launcher asks the Vulkan of Windows if the GPU
# has Vulkan 1.3 (the renderer Vulkan). It links no Vulkan library.
target_include_directories(bwvr_launcher PRIVATE
    "${BWVR_THIRD_PARTY}/Vulkan-Headers/include")
