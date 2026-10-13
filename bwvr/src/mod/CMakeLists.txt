# bwvr, the mod itself. Builds the ddraw.dll the game loads.
add_library(bwvr_mod SHARED
    ../proxy/ddraw_proxy.cpp
    mod_main.cpp
    frame_source.cpp
    stereo.cpp
    one_walk.cpp
    intro_vr.cpp
    intro_skip.cpp
    eye_clean.cpp
    frame_send.cpp
    draw_merge.cpp
    bubble_card.cpp
    draw_hooks.cpp
    ui_panel.cpp
    eye_camera.cpp
    texture_filter.cpp
    eye_capture.cpp
    eye_luma.cpp
    draw_probe.cpp
    gdi_watch.cpp
    gpu_bridge.cpp
    interop_probe.cpp
    present_watch.cpp
    pass_guard.cpp
    land_light.cpp
    frame_timers.cpp
    tick_direct.cpp
    frame_profile.cpp
    scene_sample.cpp
    render_thread.cpp
    frame_time.cpp
    fov.cpp
    input_watch.cpp
    head_pose.cpp
    head_camera.cpp
    snapshot_guard.cpp
    water_mode.cpp
    ui_worldlock.cpp
    pointer_map.cpp
    cursor_trace.cpp
    exit_test.cpp
    game_start.cpp
    comfort_camera.cpp
    script_camera.cpp
    cutscene_window.cpp
    sky_detail.cpp
    spirit_anchor.cpp
    controllers.cpp
    controller_hints.cpp
    hint_card.cpp
    text_click.cpp
    hand_direct.cpp
    hand_glow.cpp
    target_glow.cpp
    haptics.cpp
    hand_exact.cpp
    hand_model.cpp
    hand_model_file.cpp
    hand_feel.cpp
    far_grab.cpp
    take_stream.cpp
    forest_take.cpp
    creature_command.cpp
    creature_stay.cpp
    miracle.cpp
    village.cpp
    radial_menu.cpp
    temple.cpp
    throw_physics.cpp
    overlay.cpp
    teleport.cpp
    world_grab.cpp
    tabletop.cpp
    near_plane.cpp
    net_check.cpp
    dx_check.cpp
    hang_watch.cpp
    gesture_air.cpp
    audio_listener.cpp
    audio_rolloff.cpp
    audio_range.cpp
    audio_capture.cpp
    audio_voices.cpp
    audio_cues.cpp
    audio_sources.cpp
    audio_trace.cpp
    mod.rc
    ../proxy/ddraw.def
)

set_target_properties(bwvr_mod PROPERTIES
    OUTPUT_NAME "ddraw"
    PREFIX ""
)

target_link_libraries(bwvr_mod PRIVATE minhook bwvr_vr d3d9 d3d11)

# The reader of a hand that the player edited (hand_model_file.cpp) uses
# cgltf, one header with the MIT license. With no clone of it the mod builds
# with no reader, and the log of the mod says so.
set(CGLTF_DIR "${BWVR_THIRD_PARTY}/cgltf")
if(EXISTS "${CGLTF_DIR}/cgltf.h")
  target_include_directories(bwvr_mod PRIVATE "${CGLTF_DIR}")
  target_compile_definitions(bwvr_mod PRIVATE BWVR_HAVE_CGLTF)
else()
  message(WARNING
      "cgltf not found at ${CGLTF_DIR}, so the mod cannot read bwvr-hand.glb. "
      "It is part of the repo, in the folder third_party. Check that the "
      "clone is complete.")
endif()

if(MSVC)
  target_compile_options(bwvr_mod PRIVATE /W4 /permissive-)
  # The naked forwarding thunks read a global through inline assembly, which
  # /GS instrumentation would disturb.
  set_source_files_properties(../proxy/ddraw_proxy.cpp PROPERTIES COMPILE_OPTIONS "/GS-")
endif()

# build_id.h, which CMake writes from the git commit.
target_include_directories(bwvr_mod PRIVATE "${CMAKE_BINARY_DIR}/generated")

# The mod needs the OpenXR loader beside it in the game folder.
add_custom_command(TARGET bwvr_mod POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENXR_DLL}" "$<TARGET_FILE_DIR:bwvr_mod>"
    COMMENT "Copying the 32 bit OpenXR loader beside the mod"
)
