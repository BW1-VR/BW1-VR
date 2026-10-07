# A math test for the motion controllers and the table. It runs with no game
# and no headset. It compiles the real head_pose.cpp, the math of
# controllers.cpp (controller_math.h) and the table math (table_math.h). Run
# it with test/controller_math/run.ps1.
add_executable(bwvr_controller_math_test
    main.cpp
    ../../src/mod/head_pose.cpp
)

if(MSVC)
  target_compile_options(bwvr_controller_math_test PRIVATE /W4 /permissive-)
endif()
