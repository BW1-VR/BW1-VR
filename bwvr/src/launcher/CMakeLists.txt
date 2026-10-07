# The launcher.
#
# A plain Win32 program with no dependencies beyond Windows, so it is one file
# to ship and nothing for anyone to install. It is built 32 bit like everything
# else here only because the whole project is configured that way. Nothing it
# does needs to be.
add_executable(bwvr_launcher WIN32 launcher.cpp launcher.rc)
set_target_properties(bwvr_launcher PROPERTIES
    OUTPUT_NAME "Black & White VR Mod")
