// bwvr, the Black & White VR mod. The game in VR.
//
// The game loads this as ddraw.dll, and it chains every DirectDraw export to
// the real system DLL. It hooks the engine, and it presents the game in the
// headset with OpenXR.
//
// Settings live in bwvr.ini beside runblack.exe. With no file the defaults
// apply, and with vr=0 the mod does nothing and the game runs normally.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <share.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "one_walk.h"
#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "../common/exe_check.h"
#include "../common/version.h"
#include "build_id.h"
#include "../vr/xr_screen.h"
#include "controllers.h"
#include "frame_profile.h"
#include "d3d7_count.h"
#include "d3d7_thread.h"
#include "fast_transform.h"
#include "thread_wait.h"
#include "ddraw_census.h"
#include "vk_core.h"
#include "vk_scene.h"
#include "vk_ddraw.h"
#include "vk_window.h"
#include "vk_fallback.h"
#include "../proxy/ddraw_proxy.h"
#include "render_thread.h"
#include "tabletop.h"
#include "far_grab.h"
#include "take_stream.h"
#include "target_glow.h"
#include "throw_physics.h"
#include "teleport.h"
#include "ui_panel.h"
#include "vr_start.h"
#include "vr_start_math.h"

bool ProxyLoadRealDDraw();
const char* ProxyBackendNote();

bool FrameSourceSize(uint32_t* width, uint32_t* height);
const void* FrameSourceGrab(int* pitch);
void FrameSourceReport();
void FrameSourceShutdown();

// stereo.cpp: render the scene twice, once per eye.
bool StereoInstall(bwvr::XrScreen* screen, float separation,
                   float ui_shift_pixels, uint32_t height);
bool StereoInPass();
bool StereoIdle();
void StereoSetSeparation(float world_units);
void StereoSetFixes(bool freeze_time, bool move_camera_point);
// frame_time.cpp: the frame time of the game after the two eye passes.
void FrameTimeSetOptions(int real, int test_frame, int readers_frame,
                         int readers_frames);
void DrawProbeArm(bool on, int frames);
void DrawProbeDelay(int frames);
void StereoSetHeadless(bool on);
void StereoSetProfile(int level);
void StereoSetTestKeys(bool on);
void StereoSetCaptureInterval(int seconds);
void StereoSetHeadlessGrab(bool on);
void StereoSetUiDepth(bool adaptive, float ceiling_pixels);
void StereoSetScreen(bwvr::XrScreen* screen);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
// frame_send.cpp: the way of the eye pictures to the headset.
void FrameSendSetOptions(int key);
void FrameSendSetScreen(bwvr::XrScreen* screen);
// draw_merge.cpp: the merge of draw calls with one state.
void DrawMergeSetOptions(int key, int test, int test_frame);
void DrawMergeOff();
// A line of the log with the DLLs of Direct3D in the process.
void LogDirect3DModules(const char* when);
// gpu_bridge.cpp: with frame_send=2 a second thread makes calls on the
// device of the backend.
void GpuBridgeWantTwoThreads(bool on);
// intro_vr.cpp: the Lionhead intro in stereo.
void IntroSetOptions(bool on, int motion, int speed_pct, int distance_cm,
                     int model_distance_cm, bool eye_trails, bool text_place,
                     bool push_once, bool controller, bool probe,
                     bool test_keys);
void IntroSetTests(int motion_seconds, int push_pixels);
void IntroSetHandPush(int percent, bool hand_point);
void IntroSetHands(bool push_3d, int radius_cm, int hand_show,
                   bool hand_depth);
void IntroSetScreen(bwvr::XrScreen* screen);
void IntroInstall();
bool IntroFinishFrame();
// eye_clean.cpp: the frame before of each eye.
void EyeCleanSetOptions(int key, int test, int test_frame);
void EyeCleanInstall();
// bubble_card.cpp: a speech bubble of the game as one card.
void BubbleCardSetOptions(int on, int name_on, int test, int test_frame);
void BubbleCardInstall();
// intro_skip.cpp: a press of a controller button skips the logos at the
// start of the game. `caller` is the address that called FinishFrame.
void IntroSkipFrame(uintptr_t caller, bwvr::XrScreen* screen);
// hang_watch.cpp: the watch of the frame thread (hang_watch, a test key).
void HangWatchFrame();
// controllers.cpp
void ControllerHintsSetOptions(bool on, const char* forced_profile);
bool ControllerHintsInstall();
void ControllersSetOptions(bool on, int left_hand, bool haptics,
                           bool swap_buttons);
void ControllersSetFake(const FakeControllerSettings& fake);
void ControllersSetScreen(bwvr::XrScreen* screen);
void ControllersInstall();
void ControllersSetDirect(const DirectHandSettings& direct);
void ControllersSetGrab(const GrabSettings& grab);
void ControllersSetGesture(const GestureSettings& gesture);
// gesture_air.cpp: gestures drawn in the air.
void GestureAirInstall();
// world_grab.cpp: moving around by grabbing the world.
void WorldGrabSetOptions(const GrabSettings& settings);
// pointer_map.cpp: the land drag probe.
void PointerMapSetGrabProbe(bool on);
// head_pose.cpp: a still lean of the invented head.
void HeadPoseSetFakeLean(int right_cm, int up_cm, int forward_cm);
// tabletop.cpp: the two lists that stop without a message.
void TabletopCountLists();
// hand_direct.cpp
void HandDirectInstall();
void MiracleInstall();
void VillageInstall();
void HandDirectInstallTable();
void HandDirectInstallCreatureCamera(int mode);
// hand_glow.cpp: the glow at the Hand's point.
void HandGlowSetEnabled(bool on);
// overlay.cpp: the mod's own meshes, a test hand at each controller.
void OverlaySetEnabled(bool on);
void PresentWatchSetEnabled(bool on);
void PresentWatchInstall();
void PresentWatchSkipDesktop(bool on);
void PassGuardSetOptions(bool skip_hand, bool skip_physics);
void PassGuardSetSkipPrepare(bool on);
void PassGuardInstall();
void FrameTimersInstall();
// tick_direct.cpp: the clock of the game with no check of busy waiting.
void TickDirectInstall(int mode);
void FovInstall();
void InputWatchInstall();
void HeadCameraInstall();
void SnapshotGuardInstall();
void SnapshotGuardSetSkip(bool on);
void WaterModeSetSimple(bool on);
void HeadCameraSetRoll(bool on);
void StereoSetImmersive(bool on);
void UiDepthSetHintFlat(bool on);
void UiDepthSetSubtitleLift(int percent);
void UiDepthSetUiScale(int percent);
void UiDepthSetSubtitleDegrees(int degrees);
void PointerMapSetOptions(bool scale, bool clip, bool leave_cursor);
void PointerMapSetRenderSize(int width, int height);
void PointerMapSetWindowSize(int width, int height);
void PointerMapRelease();
void PointerMapSetRebuild(bool on);
// cursor_trace.cpp
void CursorTraceInstall();
void CursorTraceFrame(uint64_t frame);
// comfort_camera.cpp
void ComfortCameraSetOptions(int snap_degrees, bool instant_jumps, bool glide,
                             int blink_ms);
void ComfortCameraSetZoomStep(int step);
void ComfortCameraSetTurnHelp(int degrees_per_event, bool keeps_focus,
                              int tilt_degrees);
void ComfortCameraSetSpawnTests(int temple_update, int x, int z, int flag,
                                int toggle_update);
void ComfortCameraInstall();
void ScriptCameraSetOptions(int variant, int min_distance, int min_height,
                            int close_distance, int close_zoom_pct,
                            int cut_degrees, int cut_percent,
                            bool hold_follow);
void ScriptCameraInstall();
void SkyDetailInstall();
void SkyDetailRelease();
void SkySetGlareMode(int mode);
void SkySetCloudPoleShare(int percent);
void SkySetHeadFree(bool clouds, bool moon, int test_frame, bool trace,
                    int test_hour);
void PointerMapSetDragSmoothing(int ms);
void PointerMapSetFakeMouse(const char* script, int step_px,
                            const int x_pct[2], const int y_pct[2]);
void UiDepthSetCinemaBars(bool show);
float UiBarsBegin();
void UiBarsEnd(float saved);
// spirit_anchor.cpp
void SpiritAnchorInstall();
// fight_marks.cpp: the marks of a fight of Creatures at their depth.
void FightMarksInstall();
void AudioSetRoom(bool room, int units_per_meter, bool places);
void AudioSetRange(bool by_ears, bool keep_loops, int test_tag,
                   int test_tag_bank);
void AudioInstall(bool head, bool advisors, bool trace);
void AudioFrame();
void SpiritAnchorSetEnabled(bool on);
void SpiritAnchorSetWorldSize(bool on, bool test_keys, int test_frame,
                              bool test_point);
void DetailDistanceSet(float factor);
void ComfortCameraRelease();
void PointerMapInstall();
void HeadCameraSetHandAnchored(bool on);
void HeadCameraSetEnabled(bool on);
void StereoSetHeadTracking(bool on);
void StereoSetHeadFov(float degrees);
void StereoSetFovMargin(int percent);
void HeadPoseSetScale(float units_per_meter);
void HeadPoseSetEnabled(bool on);
void HeadPoseSetFake(bool on, float yaw_degrees, float pitch_degrees,
                     float roll_degrees);
void HeadPoseFakeSweep(bool on);
void FovSweepAfter(int windows);
void FovSetAbsolute(float radians);
void EyeCameraDump(bool on);
void EyeCameraSetFixes(bool unscaled, bool copy, bool inverse, bool random);
void EyeCameraReport();
void StereoReport();

// cutscene_window.cpp: the draw of the advisors.
void CutsceneWindowInstall();
// ui_depth.cpp
bool UiDepthInstall(bool with_attribution);
void UiDepthSetAccounting(bool on);
void UiDepthSetHintMode(int mode);
void UiDepthSetHintSelfTest(bool on);
void UiDepthSetHintScale(float scale);
DWORD TextureFilterInstall(int requested_anisotropy);
void TextureFilterReport();
void TextureFilterSetOptions(bool survey, bool force_mips);
void GdiWatchSetEnabled(bool on);
void GpuBridgeSetEnabled(bool on);
void Depth24SetWanted(bool on);
void Depth24Install();
// net_check.cpp: the game's check for an internet connection.
void NetCheckInstall(bool own_answer);
// dx_check.cpp: the game's check for DirectX 7.
void DxCheckInstall(bool own_answer);
// file_read_ahead.cpp: the read ahead of the files of LHFile.
void FileReadAheadInstall(bool on, bool test_keys);
// near_plane.cpp: the near plane of the game camera.
void NearPlaneSetOptions(int cm_24, int cm_16);
void NearPlaneInstall();
// proxy/ddraw_proxy.cpp
void ProxyForceSystemDDraw();
void ProxyCreateStarts(unsigned char ex[2], unsigned char old[2]);
void GpuBridgeInstall();
// d3d9_backend.cpp
void D3D9BackendLoad(const char* dir, const char* ini);
void GpuBridgeProbeInterop();
bool GpuBridgeHasDevice();
bool GpuBridgeFullScreen();
bool GpuBridgeReady();
void* GpuBridgeCaptureEye(int eye);
bool GpuBridgeStart(void* d3d11_device, uint32_t width, uint32_t height,
                    uint32_t swapchain_format);
void GpuBridgeReport(uint64_t frames);
void GpuBridgeResume();
void GpuBridgeShutdown();
void GdiWatchInstall();
void GdiWatchReport();
void UiDepthReport();
// exit_test.cpp: the game ends by itself at one frame, for a desktop run.
void ExitTestSetFrame(int frame);
void ExitTestFrame(uint64_t frame);
void ExitTestGuardFromStart();
// crash_report.cpp: the report and the dump of a fault (crash_report).
void CrashReportInstall(const char* ini);
void CrashReportFrame(uint64_t frame);

void ModLog(const char* format, ...);

namespace {

CRITICAL_SECTION g_lock;
FILE* g_log = nullptr;
char g_dir[MAX_PATH];

bool g_vr_enabled = true;
bool g_stereo = true;
float g_distance = 2.0f;
float g_width = 2.6f;
// How far apart the eyes sit, in the game's own world units. The scale of a
// Black & White land unit is not established yet, so this is a tunable and
// the right value is whatever looks correct in the headset.
// 0 means ask the headset, which knows the player's own eyes.
float g_separation = 0.0f;
// How far the flat interface sits in front of the screen plane, in pixels of
// horizontal shift. Zero leaves it where the engine puts it.
//
// Default 8, which puts the interface about 1.3 m away against a virtual
// screen at 2 m.
//
// A shift of DrawAndClip2D alone would split the interface: a checkbox would
// get its border at one depth and its fill at another. DrawAndClip draws flat
// interface too (3000 batches in 150 frames). draw_hooks.cpp moves both, and
// refuses anything that is not flat.
float g_ui_shift = 8.0f;
bool g_freeze_time = true;
// One walk of the scene for both eyes (one_walk.cpp). 0 walks the scene one
// time for each eye.
bool g_one_walk = true;
bool g_one_walk_check = false;
bool g_one_walk_merge = true;
// In one walk a speech bubble or the name of a villager as a card, the glow
// at the Hand's point and a halo are drawn for each eye by itself. 0 gives them to the right eye as
// each other batch. The test key one_walk_marks_test_frame changes the value
// at one frame.
bool g_one_walk_marks = true;
int g_one_walk_marks_test_frame = 0;
bool g_move_camera_point = false;
// After the two eye passes the game has the real time of the frame as its
// frame time. 0 gives the number from before the passes. The three test keys
// are for desktop runs. See frame_time.cpp.
int g_frame_time_real = 1;
// tick_direct: 0 GetTickCount through dxwrapper, 1 GetTickCount64, 2 the
// milliseconds of the performance counter (tick_direct.cpp).
int g_tick_direct = 2;
int g_frame_time_test_frame = 0;
int g_frame_time_test_readers = 0;
int g_frame_time_test_readers_frames = 600;

// The draw probe measures which draw calls break the stereo invariant.
// It is off by default. draw_probe=1 arms it at the start, and
// draw_probe_delay holds it back for that many stereo frames, which is how
// the broken thing gets on screen first.
// Name every function that submits a screen space looking batch, so a path
// that should be moved with the interface cannot stay hidden.
bool g_ui_accounting = false;

// The interface follows the world when the camera zooms in, because a
// fixed shift cannot stay in front of ground that comes to the near
// plane. ui_shift_px is the resting depth and ui_shift_max_px is the
// nearest it is allowed to come.
bool g_ui_adaptive = true;
float g_ui_shift_max = 24.0f;

// Anisotropic filtering, which the game could not afford in 2001 and a
// modern GPU gives away. 0 or 1 leaves the filtering alone.
int g_anisotropy = 16;
// profile of bwvr.ini, a test key. 0 is off. 1 is the frame cost measure
// with a timer on each Direct3D draw call. 2 is the measure with no such
// timer. See frame_profile.cpp.
int g_profile = 0;
bool g_headless_grab = false;
bool g_skip_desktop = true;
int g_capture_seconds = 0;
// Things inside Process3dEngine that step state rather than draw. They
// must run once a frame, not once an eye. See pass_guard.cpp.
bool g_skip_hand_pass2 = true;
bool g_skip_physics_pass2 = true;
int g_hint_mode = 1;
float g_hint_scale = 0.2f;
bool g_head_tracking = true;
// Play mode 3: the island on a table in the room, and play mode 4: the God
// view, the same camera with the land at the waist. See tabletop.cpp.
bool g_tabletop = false;
TabletopSettings g_table;
float g_head_fov_deg = 0.0f;
// The margin of the render past the view of the headset, in percent.
int g_fov_margin_pct = 0;
float g_fov_absolute_deg = 0.0f;
bool g_skip_snapshot = true;
bool g_water_simple = true;
bool g_head_roll = true;

bool g_immersive = true;
bool g_hint_flat = true;
int g_subtitle_lift = 0;
int g_ui_scale_pct = 25;
// How far below the middle of the view the subtitles sit, in degrees.
int g_subtitle_degrees = 35;
// The interface on its own layer (docs/ui-layer.md). Off by default.
UiPanelSettings g_ui_panel;
// F12 and Shift F12, for tests. See stereo.cpp, PollHotkeys.
bool g_test_keys = false;
bool g_pointer_scale = true;
bool g_pointer_clip = true;
// Test keys of the desktop runs. See pointer_map.cpp and cursor_trace.cpp.
bool g_pointer_leave_cursor = false;
bool g_cursor_trace = false;
// A test key of the desktop runs. See exit_test.cpp.
bool g_registry_test_guard = false;
int g_window_w = 0;
int g_window_h = 0;
bool g_hand_anchored = true;
// Moving around. The launcher stores the two choices as an index.
int g_snap_turn_deg = 45;
bool g_instant_jumps = true;
bool g_camera_glide = false;
int g_blink_ms = 0;
int g_zoom_step = 2;  // 0 the game's own, 1 small, 2 medium, 3 large
// The turn events of a snap turn for the tutorial, and the turn of the game
// camera about its own focus (comfort_camera.cpp).
int g_turn_help_degrees = 5;
int g_tilt_help_degrees = 3;
bool g_god_turn_keeps_focus = true;
// The camera the story moves, see script_camera.cpp. 0 as the game moves it,
// 1 a cut at the start of each move, 2 a cut at the end of each move.
int g_script_camera = 2;
float g_detail_distance = 5.0f;  // how far out models keep their detail
// Below 0 keeps the default in script_camera.cpp.
int g_script_min_distance = -1;
int g_script_min_height = -1;
int g_script_close_distance = -1;
int g_script_close_zoom = -1;
int g_script_cut_degrees = -1;
int g_script_cut_percent = -1;
bool g_script_follow = true;
// The mouse part of a land drag is smoothed over this many milliseconds.
int g_drag_smoothing_ms = 35;
// The sun glare: 0 through the eye camera, 1 as the game draws it, 2 hidden.
int g_sun_glare = 0;
// How long a cloud may reach toward the eye, as a percent of its distance.
int g_cloud_pole_pct = 25;
// In the table and the God view the clouds face the table camera, not the
// render head. The moon turns with world up, not with the head.
bool g_clouds_head_free = true;
bool g_moon_world_up = true;
// Test keys of a desktop run: both keys above change at this frame, and the
// report has the moves of the clouds and the moon from frame to frame.
int g_sky_test_frame = 0;
bool g_sky_test_trace = false;
int g_sky_test_hour = -1;
// The black bars of the story scenes. Off by default in a headset.
bool g_cinema_bars = false;
// The advisors stay where the player looked when they came.
bool g_spirit_anchor = true;
// An advisor at a place of the world has the size for the eye that it has
// at its usual place. 0: the size of the game, 4 times the usual size.
bool g_spirit_world_size = true;
// Test only: the rule above changes at this frame of the controllers.
int g_spirit_world_size_test = 0;
// Test only: an advisor that points at a place of the world goes to it.
bool g_spirit_point_world_test = false;
// Sound in the room. See docs/audio.md.
bool g_audio_head = true;
bool g_audio_advisors = true;
bool g_audio_trace = false;
bool g_audio_room = true;
int g_audio_units_per_meter = 40;
bool g_audio_flat_places = true;
bool g_audio_range = true;
bool g_audio_loops_keep = true;
int g_audio_test_tag = 0;
int g_audio_test_tag_bank = 1;
bool g_game_camera_rebuild = true;
bool g_fake_head = false;
bool g_head_camera = true;
float g_fake_yaw = 0.0f;
float g_fake_pitch = 0.0f;
float g_fake_roll = 0.0f;
bool g_fake_sweep = false;
// The player key "controllers" turns the whole controller path off.
bool g_controllers = true;
// The player key "left_hand": -1 follows the game's own option, 0 makes the
// right controller the Hand, 1 the left controller.
int g_left_hand = -1;
bool g_controller_swap_buttons = false;
bool g_controller_hints = true;
bool g_hint_selftest = false;
char g_hint_profile[64] = {0};
bool g_controller_haptics = true;
FakeControllerSettings g_fake_controller;
DirectHandSettings g_direct_hand;
// A glow marks the point the Hand interacts from (hand_glow.cpp). Off by
// default: the mesh of a target glows (target_glow.cpp).
bool g_hand_glow = false;
// The mesh of a target of the Hand glows (target_glow.cpp).
TargetGlowSettings g_target_glow;
// The mod draws its own test hand at each controller (overlay.cpp).
bool g_overlay = false;
// The teleport with a visible arc (teleport.cpp). See docs/god-view.md.
TeleportSettings g_teleport;
// The grab from a distance (far_grab.cpp), and the square root law of the
// throw. See docs/grab.md, "The grab from a distance".
FarGrabSettings g_far_grab;
// take_stream: the stream of a take at a distance goes on a curve from the
// source to the Hand (take_stream.cpp).
int g_take_stream = 1;
// The law of a throw and its log (throw_physics.cpp).
int g_throw_law = 2;
int g_throw_slow_x10 = 20;
bool g_throw_trees = true;
int g_release_fall_cm = 15;
int g_throw_log = 0;
// The landing of a thrown object (throw_physics.cpp).
int g_throw_land = 1;
int g_throw_land_rate_x10 = 10;
int g_throw_bounce_pct = 50;
int g_throw_land_brake_pct = 60;
int g_throw_tree_settle = 1;
int g_throw_land_rest = 1;
int g_throw_draw = 1;
// The take of an object that comes to rest (throw_physics.cpp).
int g_catch_rest_guard = 1;
int g_catch_rest_test = 0;
int g_catch_at_once = 1;
GrabSettings g_grab;
GestureSettings g_gesture;
int g_fake_lean_cm[3] = {0, 0, 0};
// The test mouse of a desktop run (pointer_map.cpp, PointerMapSetFakeMouse).
char g_fake_mouse_script[512] = {0};
int g_fake_mouse_step_px = 8;
int g_fake_mouse_x_pct[2] = {50, 50};
int g_fake_mouse_y_pct[2] = {50, 70};
float g_head_units_per_meter = 7.5f;
// The near plane in the room, in play modes 0, 3 and 4 (near_plane.cpp).
int g_near_plane_cm = 5;
// The mod answers the game's check for an internet connection (net_check.cpp).
bool g_net_check_own = true;
// The mod answers the game's check for DirectX 7 (dx_check.cpp).
bool g_dx_check_own = true;
int g_near_plane_cm_16 = 10;
bool g_skip_prepare = false;
bool g_filter_survey = false;
bool g_force_mips = true;
bool g_draw_probe = false;
int g_draw_probe_frames = 120;
int g_draw_probe_delay = 0;

// Run both passes with no headset, so the draw probe can be used from a
// plain desktop session.
bool g_stereo_headless = false;

// The Lionhead intro in stereo. Off by default while it is tested. See
// intro_vr.cpp and docs/intro.md.
bool g_intro_vr = false;
// 0 the camera stays at the rest pose, 1 it goes along the path of the game
// and does not turn, 2 the orbs are a model in front of the player, 3 the
// camera of the game with its turns.
int g_intro_motion = 1;
int g_intro_speed_pct = 100;
int g_intro_distance_cm = 300;
int g_intro_model_distance_cm = 200;
bool g_intro_eye_trails = true;
// The frame before of each eye in the scene (eye_clean.cpp), and its test
// keys for desktop runs.
int g_eye_clean = 1;
int g_eye_clean_test = 0;
int g_eye_clean_test_frame = 0;
// The way of the eye pictures to the headset (frame_send.cpp).
int g_frame_send = 0;
// The merge of draw calls with one state (draw_merge.cpp), and its test keys
// for desktop runs.
int g_draw_merge = 1;
int g_draw_merge_test = 0;
int g_draw_merge_test_frame = 0;
// The render thread (render_thread.cpp), and its test key for desktop runs.
int g_render_thread = 0;
int g_render_thread_test_frame = 0;
int g_render_thread_lock_free = 1;
int g_render_thread_filter = 1;
int g_render_thread_buffers = 1;
int g_render_thread_right_eye = 1;
int g_fast_transform = 1;
int g_fast_transform_check = 0;
int g_d3d7_count = 0;
int g_d3d7_thread = 1;
int g_d3d7_thread_merge = 1;
int g_vk_scene = 0;
int g_vk_scene_test_frame = 0;
int g_vk_scene_follow = 1;
int g_vk_panel = 1;
int g_vk_only = 1;
int g_vk_only_test = 0;
int g_vk_upload_queue = 1;
int g_vk_present_late = 1;
int g_vk_texture_reuse = 1;
int g_vk_keep_picture = 1;
int g_vk_black_check = 1;
int g_vk_untextured = 1;
int g_vk_stage1 = 1;
// Foveated rendering (docs/backend.md, "Foveated rendering").
int g_foveation = 0;
int g_foveation_inner_deg = 0;
int g_foveation_outer_deg = 0;
int g_foveation_gaze = 1;
int g_foveation_show = 0;
// Foveated resolution (docs/backend.md, "Foveated resolution").
int g_fovea_res = 0;
int g_fovea_res_outer_pct = 50;
int g_fovea_res_center_pct = 100;
int g_fovea_res_center_deg = 25;
int g_fovea_res_center_down_deg = 0;
int g_vk_scene_reread_test = 0;
// The OpenXR session with Vulkan, and the eye pictures of the Vulkan
// renderer in it (docs/backend.md, "The session with Vulkan").
int g_xr_vulkan = 0;
int g_vk_eyes = 1;
// The pixels of the CPU in a session with Vulkan (XrScreen::SetVulkanUpload).
int g_xr_vulkan_upload = 1;
// backend=vulkan with vulkan_only=1 (docs/backend.md, "Only Vulkan"). Set in
// EarlyHooks, before each other part reads it.
bool g_vulkan_only = false;

// xr_hold_ms: the hold of the last frame (XrScreen::SetHoldMs).
int g_xr_hold_ms = 30;
int g_render_thread_late_end = 1;
int g_thread_wait = 1;
int g_test_cores = 0;
int g_test_smt = 0;
int g_test_spin_walk_us = -1;
int g_test_spin_idle_us = -1;
int g_test_wake_records = -1;
int g_gpu_flush = 128;
char g_ab_test[64] = "";
int g_profile_gpu = 0;
// A speech bubble of the game as one card (bubble_card.cpp), and its test
// keys for desktop runs. The name of a villager as one card (name_card).
int g_bubble_card = 1;
int g_name_card = 1;
int g_bubble_card_test = 0;
int g_bubble_card_test_frame = 0;
bool g_intro_text_place = true;
bool g_intro_push_once = true;
bool g_intro_controller = true;
// The motion of the hand that goes to the motion of the mouse in the intro,
// in percent. 0: only a mouse pushes the orbs.
int g_intro_hand_push_pct = 100;
// In the intro the pointer is at the Hand's point, not where the aim ray
// points.
bool g_intro_hand_point = true;
// In the intro each hand takes the orbs in its reach in 3D, the reach in cm
// of the room, when the hand models show (0 never, 1 while a hand moves, 2
// while it is tracked), and whether they draw before the orbs.
bool g_intro_push_3d = true;
int g_intro_push_radius_cm = 30;
int g_intro_hand_show = 1;
bool g_intro_hand_depth = true;
bool g_intro_probe = false;
// Tests of the desktop runs.
int g_intro_motion_test_seconds = 0;
int g_intro_push_test_px = 0;

// The camera is a family of matrices, not one matrix. Each of these
// keeps one more member of that family in step with the eye. They are
// switches so the draw probe can measure one at a time. See
// eye_camera.cpp for what each one is and why it matters.
bool g_fix_unscaled = true;
bool g_fix_copy = true;
bool g_fix_inverse = true;
bool g_fix_random = false;  // measured: it makes things worse, see eye_camera.cpp

// The end of the mod (docs/backend.md, "The end of the mod"). 1: the mod
// shuts down on the exit path of the game. 0: it shuts down when Windows
// unloads the DLL.
bool g_exit_shutdown = true;
// The shutdown ran. No hook of this file does work after it.
bool g_shut_down = false;
// The thread that draws the frames of the game, from FinishFrame.
DWORD g_frame_thread = 0;
// Windows ends the process. Each other thread is stopped then.
bool g_process_ends = false;

bwvr::XrScreen* g_screen = nullptr;
bool g_hooks_installed = false;
bool g_vr_gave_up = false;
// 1 starts the headset session in a full screen game too.
bool g_vr_full_screen = false;
int g_vr_tries = 0;
DWORD g_next_vr_try = 0;
// Where the tries to start the headset session run (docs/backend.md, "The
// start of the headset session"). 0: each try on the frame thread, with a
// new instance. 1: on a thread of the mod, with one instance for all tries.
// 2: on that thread, with a new instance for each try.
int g_vr_start = 1;
// Test keys: in this try the thread of the tries waits this long, as a call
// into the runtime that does not come back. 0 is off.
int g_vr_start_test_hang_try = 0;
int g_vr_start_test_hang_ms = 0;
// Test key: the session part of a headset that the thread found fails one
// time, before StartSession, so the fall back runs. 0 is off.
int g_vr_start_test_fail_session = 0;
// The thread of the tries runs, and the frame thread only looks at it.
bool g_vr_thread = false;
// The session part failed after the thread found the headset. When the
// thread has ended its instance, the tries of vr_start=0 go on.
bool g_vr_fell_back = false;
// The first try of vr_start=0 after the fall back writes its lines.
bool g_vr_fallback_try = false;
// The frame watch runs, and the time of the tries in this frame.
bool g_vr_watch = false;
double g_vr_code_ms = 0.0;

// Try for two minutes, which is long enough to put a headset on and
// connect Virtual Desktop after the game has already started.
const int kVrRetryMs = 3000;
const int kMaxVrTries = 40;
uint64_t g_frame = 0;

typedef void(__cdecl* FinishFrameFn)();
FinishFrameFn o_FinishFrame = nullptr;

void ReadSettings() {
  char path[MAX_PATH];
  lstrcpynA(path, g_dir, MAX_PATH);
  lstrcatA(path, "bwvr.ini");

  // No early return when the file is missing. GetPrivateProfileIntA gives
  // the fallback for a missing file as well as for a missing key, so the
  // reads below are the one place a default lives.
  if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
    ModLog("settings     : no bwvr.ini yet, so every setting is a default");
  }
  g_vr_enabled = GetPrivateProfileIntA("bwvr", "vr", 1, path) != 0;
  g_stereo = GetPrivateProfileIntA("bwvr", "stereo", 1, path) != 0;
  g_distance = GetPrivateProfileIntA("bwvr", "screen_distance_cm", 200, path) /
               100.0f;
  g_width = GetPrivateProfileIntA("bwvr", "screen_width_cm", 260, path) / 100.0f;
  // Zero means take it from the headset, which knows the player's own eyes.
  // Anything else is a deliberate choice and stands.
  g_separation =
      GetPrivateProfileIntA("bwvr", "eye_separation_milli", 0, path) / 1000.0f;
  g_ui_shift =
      static_cast<float>(GetPrivateProfileIntA("bwvr", "ui_shift_px", 8, path));
  g_freeze_time = GetPrivateProfileIntA("bwvr", "freeze_time", 1, path) != 0;
  g_frame_time_real =
      GetPrivateProfileIntA("bwvr", "frame_time_real", 1, path) != 0 ? 1 : 0;
  g_tick_direct =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "tick_direct", 2, path));
  g_frame_time_test_frame = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "frame_time_test_frame", 0, path));
  g_frame_time_test_readers = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "frame_time_test_readers", 0, path));
  g_frame_time_test_readers_frames = static_cast<int>(GetPrivateProfileIntA(
      "bwvr", "frame_time_test_readers_frames", 600, path));
  g_one_walk = GetPrivateProfileIntA("bwvr", "one_walk", 1, path) != 0;
  g_one_walk_check =
      GetPrivateProfileIntA("bwvr", "one_walk_check", 0, path) != 0;
  g_one_walk_merge =
      GetPrivateProfileIntA("bwvr", "one_walk_merge", 1, path) != 0;
  g_one_walk_marks =
      GetPrivateProfileIntA("bwvr", "one_walk_marks", 1, path) != 0;
  g_one_walk_marks_test_frame = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "one_walk_marks_test_frame", 0, path));
  g_draw_probe = GetPrivateProfileIntA("bwvr", "draw_probe", 0, path) != 0;
  g_ui_accounting =
      GetPrivateProfileIntA("bwvr", "ui_accounting", 0, path) != 0;
  g_ui_adaptive = GetPrivateProfileIntA("bwvr", "ui_adaptive", 1, path) != 0;
  g_ui_shift_max = static_cast<float>(
      GetPrivateProfileIntA("bwvr", "ui_shift_max_px", 24, path));
  g_anisotropy = GetPrivateProfileIntA("bwvr", "anisotropy", 16, path);
  g_filter_survey =
      GetPrivateProfileIntA("bwvr", "filter_survey", 0, path) != 0;
  g_profile =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "profile", 0, path));
  if (g_profile < 0 || g_profile > 3) g_profile = 1;
  g_skip_desktop =
      GetPrivateProfileIntA("bwvr", "skip_desktop_present", 1, path) != 0;
  g_capture_seconds =
      GetPrivateProfileIntA("bwvr", "capture_every_seconds", 0, path);
  g_skip_hand_pass2 =
      GetPrivateProfileIntA("bwvr", "skip_hand_pass2", 1, path) != 0;
  g_skip_physics_pass2 =
      GetPrivateProfileIntA("bwvr", "skip_physics_pass2", 1, path) != 0;
  // 0 leaves the hand hint alone, 1 keeps the copy at the Hand, and 2 keeps
  // the copy at the pointer.
  // PLAY MODE. One key picks how the game reaches the headset, because head
  // tracking and filling the view only make sense in these combinations.
  //   0  Immersive: the world is all around the player, and the head moves
  //      the camera. Not a play mode: play_mode=0 gives the God view.
  //      This view stays for a story, while the God view stands aside, and
  //      for Ctrl F11, which turns the God view off and on.
  //   1  Stereoscopic window: a 3D screen in front of the player. The head
  //      does not move the camera.
  //   2  A window that follows the head. Not offered in the launcher yet.
  //   3  Tabletop: the island is a model on a table in the room, and the
  //      head moves the camera. See tabletop.cpp.
  //   4  God view, the main mode and the default: the land at the player's
  //      waist, zoomed from the life size scale to the table scales. The
  //      same camera as the table, with no edge. See tabletop.cpp and
  //      docs/god-view.md.
  {
    struct PlayMode {
      bool head_tracking;
      bool immersive;
      bool tabletop;
      bool god;
    };
    static const PlayMode kPlayModes[] = {{true, true, false, false},
                                          {false, false, false, false},
                                          {true, false, false, false},
                                          {true, true, true, false},
                                          {true, true, true, true}};
    int mode =
        static_cast<int>(GetPrivateProfileIntA("bwvr", "play_mode", 4, path));
    // play_mode=0 is the retired immersive mode. An older bwvr.ini can hold
    // it, and it gets the God view.
    if (mode == 0) {
      ModLog("settings     : play_mode=0, the immersive mode, is now the God "
             "view, play mode 4");
      mode = 4;
    }
    const PlayMode& chosen = kPlayModes[(mode >= 0 && mode < 5) ? mode : 4];
    g_head_tracking = chosen.head_tracking;
    g_immersive = chosen.immersive;
    g_tabletop = chosen.tabletop;
    g_table.god = chosen.god;
  }
  // THE TABLE. Not in the launcher, except the play mode. All sizes are in
  // the room. A scale of 0 fits the land to the table, and a center of -1 is
  // the middle of the land. table_key_color is a hex color such as FF00FF,
  // and empty or 0 leaves the sky as it is.
  {
    TabletopSettings& t = g_table;
    const auto number = [&](const char* key, int fallback) {
      return static_cast<int>(GetPrivateProfileIntA(
          "bwvr", key, static_cast<UINT>(fallback), path));
    };
    t.diameter_cm = number("table_diameter_cm", 150);
    t.forward_cm = number("table_forward_cm", 80);
    t.down_cm = number("table_down_cm", 40);
    t.units_per_meter = number("table_units_per_meter", 0);
    t.margin_pct = number("table_margin_pct", 10);
    t.center_x = number("table_center_x", -1);
    t.center_z = number("table_center_z", -1);
    t.full_textures = number("table_textures", 1) != 0;
    // The length of a controller's Hand on the table, in the room.
    t.hand_cm = number("table_hand_cm", 10);
    // A test, like fake_head_sweep: the table goes off and on in steps, once
    // a report window, so one desktop run measures both.
    t.sweep = number("table_sweep", 0) != 0;
    char key[32] = {0};
    GetPrivateProfileStringA("bwvr", "table_key_color", "", key, sizeof(key),
                             path);
    const unsigned long rgb = strtoul(key, nullptr, 16) & 0xFFFFFFul;
    t.key = key[0] != 0 && rgb != 0;
    t.key_color = 0xFF000000u | static_cast<uint32_t>(rgb);
    // THE GOD VIEW (play_mode=4). Not in the launcher, except the play mode.
    // The ground is god_ground_down_cm below the head at the last recenter.
    // The view starts at god_units_per_meter, and a zoom step changes the
    // scale by god_zoom_step_pct, from the life size scale up to
    // god_far_units_per_meter. A ground step is god_ground_step_cm.
    t.god_down_cm = number("god_ground_down_cm", 60);
    t.god_units_per_meter = number("god_units_per_meter", 100);
    t.god_zoom_pct = number("god_zoom_step_pct", 50);
    // The zoom of the stick: 0 is one zoom step for each push. 1 is the
    // smooth zoom: the scale changes while the stick is held, and a full push
    // changes it by one zoom step in god_zoom_smooth_ms. With test_keys=1,
    // Ctrl, Shift and Insert change it, and the test god_zoom_test_toggle
    // does the same at one frame of the controllers (docs/god-view.md, "The
    // zoom").
    t.god_zoom_smooth = number("god_zoom_smooth", 0);
    t.god_zoom_smooth_ms = number("god_zoom_smooth_ms", 500);
    t.god_zoom_test_toggle = number("god_zoom_test_toggle", 0);
    // The direction of the stick: 0 zooms out with the stick forward and in
    // with the stick back. 1 zooms in with the stick forward.
    t.god_zoom_forward_in = number("god_zoom_forward_in", 0);
    // The point that a zoom keeps in the room: 1 is the land point that the
    // head looks at, at most god_zoom_about_far_cm from the head. 0 is the
    // land point under the head, and 2 is the Hand's point. With test_keys=1,
    // Ctrl, Shift and Delete change it, and the test god_zoom_about_test does
    // the same at one frame of the controllers (docs/god-view.md, "The
    // zoom").
    t.god_zoom_about = number("god_zoom_about", 1);
    t.god_zoom_about_far_cm = number("god_zoom_about_far_cm", 300);
    t.god_zoom_about_test = number("god_zoom_about_test", 0);
    t.god_step_cm = number("god_ground_step_cm", 5);
    t.god_most_units = number("god_far_units_per_meter", 4000);
    // Where the player goes when the game puts its own camera at a place:
    // 1 follows the game, with the land point that the camera looks at
    // god_spawn_ahead_cm in front of the head. 0 builds the view only at the
    // start, at a load and at the end of a story. With test_keys=1, Ctrl and
    // Numpad 0 change it (docs/god-view.md).
    t.god_spawn = number("god_spawn", 1);
    t.god_spawn_ahead_cm = number("god_spawn_ahead_cm", 50);
    // The place of the game camera: 1 over the land point under the head,
    // 0 over the recenter place. With test_keys=1, Ctrl and Numpad 1 change
    // it.
    t.god_camera_at = number("god_camera_at", 1);
    // The walk with the stick of the hand that does not point: 1 moves the
    // player over the land along the direction of the head, 2 along the
    // direction in which that controller points, and 0 is off. A full push
    // is move_stick_speed_cm of the room for each second. With test_keys=1,
    // Shift and the Menu key change the rule (docs/god-view.md, "The walk
    // with the stick").
    t.move_stick = number("move_stick", 1);
    t.move_stick_speed_cm = number("move_stick_speed_cm", 150);
    // The time of a frame for the walk and the smooth zoom: the time between
    // the display times of two frames of the headset (1), or the time of
    // the CPU between two main camera calls (0). docs/god-view.md, "The time
    // of a frame".
    t.god_display_time = number("god_display_time", 1);
    // While the stick walks, 1: the height of the view follows the land
    // under the head through a smooth filter, and 0: the view keeps its
    // height. move_stick_trace=N logs the first N frames of each walk, for
    // a desktop run.
    t.move_stick_follow_land = number("move_stick_follow_land", 1);
    t.move_stick_trace = number("move_stick_trace", 0);
    t.test_keys = number("test_keys", 0) != 0;
    // Tests with no key press, for a desktop run: the game's zoom to the
    // Temple at an update of the camera mode, and the test key of god_spawn.
    ComfortCameraSetSpawnTests(number("spawn_test_temple", 0),
                               number("spawn_test_x", 2238),
                               number("spawn_test_z", 3156),
                               number("spawn_test_flag", 0),
                               number("spawn_test_toggle", 0));
  }
  // WHICH COPY OF THE HINT TO KEEP DEPENDS ON THE HEAD.
  //
  // The game draws every hint twice: once projected at the Hand, and once
  // pinned to the mouse cursor's position on screen. On a monitor those are
  // the same point, which is why it reads as one hint.
  //
  // Without head tracking the pinned copy is the right one to keep, because it
  // sits in the same place in both eyes and reads flat, like a label.
  //
  // With head tracking it is the worst one to keep. A screen position is fixed
  // relative to the camera, and the camera turns with the head, so the
  // pinned copy rides the player's look direction while the Hand stays where
  // it was put. So the default follows the head: mode 1 keeps the copy at the
  // Hand, which is the one that belongs to the world.
  const int hint_default = g_head_tracking ? 1 : 2;
  g_hint_mode =
      GetPrivateProfileIntA("bwvr", "hint_mode", hint_default, path);
  g_hint_scale = static_cast<float>(
      GetPrivateProfileIntA("bwvr", "hint_scale_pct", 20, path)) / 100.0f;
  EyeCameraDump(GetPrivateProfileIntA("bwvr", "dump_camera", 0, path) != 0);
  FovSweepAfter(GetPrivateProfileIntA("bwvr", "fov_sweep_after", 0, path));
  // A fixed view angle, so a wide view can be tested on the desktop. The
  // headset path sets its own from what the runtime asks for.
  g_fov_absolute_deg = static_cast<float>(
      GetPrivateProfileIntA("bwvr", "fov_absolute_deg", 0, path));
  g_skip_snapshot =
      GetPrivateProfileIntA("bwvr", "skip_snapshot", 1, path) != 0;
  // HEAD ROLL IS ON, AND IT DECIDES THE SEA.
  //
  // Without roll the world tilts with the player's head instead of staying
  // level, which is the worse of the two faults by a long way. The scanline
  // sea is drawn as full width horizontal strips and cannot tilt, so roll
  // forces the plainer sea. See water_mode.cpp and head_camera.cpp.
  g_head_roll = GetPrivateProfileIntA("bwvr", "head_roll", 1, path) != 0;
  g_water_simple =
      GetPrivateProfileIntA("bwvr", "water_simple", 1, path) != 0;
  if (g_head_roll) g_water_simple = true;
  // The table cuts the plain sea at its edge. The scanline sea cannot be cut.
  // The God view keeps the plain sea too: the scanline sea is a band of the
  // screen from the horizon down, and it cannot follow the scale.
  if (g_tabletop) g_water_simple = true;
  // The hint stays on the Hand but loses the Hand's depth, which is what makes
  // it readable. Off puts the depth back.
  g_hint_flat = GetPrivateProfileIntA("bwvr", "hint_flat", 1, path) != 0;
  // Not in the launcher. The subtitles stay where the game puts them. The
  // interface size scales them toward the middle of the view, and at 25
  // percent that is where they read best. This moves them up further.
  g_subtitle_lift =
      GetPrivateProfileIntA("bwvr", "subtitle_lift_pct", 0, path);
  // Not in the launcher. After the interface size, the subtitles move down
  // to this angle below the middle of the view. Shift F12 steps it, with
  // test keys on.
  g_subtitle_degrees = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "subtitle_deg", 35, path));
  // How big the help layer is drawn. The game sizes it from the render
  // height, which is far too big in a headset. 25 read best in the headset.
  g_ui_scale_pct = GetPrivateProfileIntA("bwvr", "ui_scale_pct", 25, path);
  // Not in the launcher yet. The interface on its own surface: 1 shows it as
  // quad layers in the room, 2 is the desktop test. See
  // docs/ui-layer.md, "Ini keys and the test key". A negative angle needs the cast.
  {
    UiPanelSettings& u = g_ui_panel;
    u.mode = static_cast<int>(GetPrivateProfileIntA("bwvr", "ui_panel", 0, path));
    u.menu_distance_cm = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "ui_menu_distance_cm", 140, path));
    u.menu_pitch_deg = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "ui_menu_pitch_deg", -10, path));
    u.menu_px_per_deg = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "ui_menu_px_per_deg", 16, path));
    u.help_distance_cm = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "ui_help_distance_cm", 120, path));
    u.help_pitch_deg = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "ui_help_pitch_deg", -30, path));
    u.help_px_per_deg = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "ui_help_px_per_deg", 44, path));
    // The Creature panel: 0 off, 1 fixed in the room, 2 near the Creature.
    u.creature_panel = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "creature_panel", 2, path));
    // A rect is "x y w h" in render pixels. 0 or a bad value takes the
    // default of ui_panel.cpp.
    const char* const rect_keys[2] = {"ui_menu_rect", "ui_help_rect"};
    int* const rects[2] = {u.menu_rect, u.help_rect};
    for (int i = 0; i < 2; ++i) {
      char text[64];
      GetPrivateProfileStringA("bwvr", rect_keys[i], "0", text, sizeof(text),
                               path);
      int r[4] = {0, 0, 0, 0};
      if (sscanf_s(text, "%d %d %d %d", &r[0], &r[1], &r[2], &r[3]) == 4 &&
          r[0] >= 0 && r[1] >= 0 && r[2] > 0 && r[3] > 0) {
        for (int k = 0; k < 4; ++k) rects[i][k] = r[k];
      }
    }
    if (u.menu_distance_cm < 30) u.menu_distance_cm = 30;
    if (u.help_distance_cm < 30) u.help_distance_cm = 30;
    if (u.menu_px_per_deg < 4) u.menu_px_per_deg = 4;
    if (u.help_px_per_deg < 4) u.help_px_per_deg = 4;
  }
  // The desktop window stops deciding what the pointer can reach, and
  // stops being able to hand a click to another program.
  g_pointer_scale =
      GetPrivateProfileIntA("bwvr", "pointer_scale", 1, path) != 0;
  g_pointer_clip =
      GetPrivateProfileIntA("bwvr", "pointer_clip", 1, path) != 0;
  // Not in the launcher. Test keys: a desktop test run must not hold or move
  // the real cursor, and its log names each caller that does.
  g_pointer_leave_cursor =
      GetPrivateProfileIntA("bwvr", "pointer_leave_cursor", 0, path) != 0;
  g_cursor_trace = GetPrivateProfileIntA("bwvr", "cursor_trace", 0, path) != 0;
  g_window_w = GetPrivateProfileIntA("bwvr", "window_width", 0, path);
  g_window_h = GetPrivateProfileIntA("bwvr", "window_height", 0, path);
  // Off is the control for the anchor test: the Hand should then move
  // when the head turns, which is what makes the test mean something.
  g_hand_anchored =
      GetPrivateProfileIntA("bwvr", "hand_anchored", 1, path) != 0;
  // MOVING AROUND. Camera motion that the player's head did not make is what
  // makes a headset uncomfortable, so the camera turns in steps, jumps land
  // at once, and nothing glides. See comfort_camera.cpp.
  {
    static const int kSnapSteps[] = {0, 30, 45, 90};
    static const int kBlinkSteps[] = {0, 100, 200};
    const int snap = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "snap_turn", 2, path));
    g_snap_turn_deg = (snap >= 0 && snap < 4) ? kSnapSteps[snap] : 45;
    const int blink =
        static_cast<int>(GetPrivateProfileIntA("bwvr", "blink", 0, path));
    g_blink_ms = (blink >= 0 && blink < 3) ? kBlinkSteps[blink] : 0;
  }
  g_instant_jumps =
      GetPrivateProfileIntA("bwvr", "instant_jumps", 1, path) != 0;
  g_camera_glide =
      GetPrivateProfileIntA("bwvr", "camera_glide", 0, path) != 0;
  {
    const int zoom =
        static_cast<int>(GetPrivateProfileIntA("bwvr", "zoom_step", 2, path));
    g_zoom_step = (zoom >= 0 && zoom < 4) ? zoom : 2;
    g_turn_help_degrees = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "turn_help_degrees", 5, path));
    g_god_turn_keeps_focus =
        GetPrivateProfileIntA("bwvr", "god_turn_keeps_focus", 1, path) != 0;
    g_tilt_help_degrees = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "tilt_help_degrees", 3, path));
  }
  // The camera the story moves: 0 as the game does it, 1 a cut at the start
  // of each move, 2 a cut at the end of each move.
  {
    const int story =
        static_cast<int>(GetPrivateProfileIntA("bwvr", "script_camera", 2, path));
    g_script_camera = (story >= 0 && story < 3) ? story : 2;
    // A cut scene around the player (cutscene_immersive=1, the launcher row
    // "Cut scenes") has the camera of the game with all its motion, and no
    // cut scene window (cutscene_window.cpp).
    if (GetPrivateProfileIntA("bwvr", "cutscene_immersive", 0, path) != 0) {
      g_script_camera = 0;
    }
  }
  // How far out models keep their detail: the game's own, 2, 3 or 5 times.
  {
    static const float kDetailFactors[] = {1.0f, 2.0f, 3.0f, 5.0f};
    const int detail = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "detail_distance", 3, path));
    g_detail_distance =
        (detail >= 0 && detail < 4) ? kDetailFactors[detail] : 5.0f;
  }
  // Not in the launcher. How near the story may take the camera to what it
  // looks at and to the ground, in world units, and how far a held shot may
  // drift before it cuts. A value below 0 keeps the default.
  g_script_min_distance = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "script_camera_min_distance", -1, path));
  g_script_min_height = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "script_camera_min_height", -1, path));
  g_script_close_distance = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "script_camera_close_distance", -1, path));
  g_script_close_zoom = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "script_camera_close_zoom_pct", -1, path));
  g_script_cut_degrees = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "script_camera_cut_degrees", -1, path));
  g_script_cut_percent = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "script_camera_cut_percent", -1, path));
  g_script_follow =
      GetPrivateProfileIntA("bwvr", "script_camera_follow", 1, path) != 0;
  // Not in the launcher. Tests in the headset chose these values: the drag
  // smoothing, the sun glare, the cloud pole, the cinema bars and the
  // advisors.
  g_drag_smoothing_ms = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "drag_smoothing_ms", 35, path));
  g_sun_glare = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "sun_glare", 0, path));
  g_cloud_pole_pct = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "cloud_pole_pct", 25, path));
  // Not in the launcher. See docs/backend.md, "Sky, clouds and detail".
  g_clouds_head_free =
      GetPrivateProfileIntA("bwvr", "clouds_head_free", 1, path) != 0;
  g_moon_world_up =
      GetPrivateProfileIntA("bwvr", "moon_world_up", 1, path) != 0;
  g_sky_test_frame = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "sky_test_frame", 0, path));
  g_sky_test_trace =
      GetPrivateProfileIntA("bwvr", "sky_test_trace", 0, path) != 0;
  g_sky_test_hour = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "sky_test_hour", -1, path));
  g_cinema_bars = GetPrivateProfileIntA("bwvr", "cinema_bars", 0, path) != 0;
  g_spirit_anchor =
      GetPrivateProfileIntA("bwvr", "spirit_anchor", 1, path) != 0;
  // Not in the launcher. See docs/tabletop.md, "The spirits beside the
  // table". With test_keys=1, Shift and Home change it.
  g_spirit_world_size =
      GetPrivateProfileIntA("bwvr", "spirit_world_size", 1, path) != 0;
  g_spirit_world_size_test = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "spirit_world_size_test", 0, path));
  g_spirit_point_world_test =
      GetPrivateProfileIntA("bwvr", "spirit_point_world_test", 0, path) != 0;
  // The ears follow the head, and the advisors speak from where they float.
  g_audio_head = GetPrivateProfileIntA("bwvr", "audio_head", 1, path) != 0;
  g_audio_advisors =
      GetPrivateProfileIntA("bwvr", "audio_advisors", 1, path) != 0;
  // Not in the launcher. A log of the calls into QMixer, for tests.
  g_audio_trace = GetPrivateProfileIntA("bwvr", "audio_trace", 0, path) != 0;
  // Not in the launcher. Room distances for the positional sounds, and how
  // many world units one meter of the room sounds as. See docs/audio.md.
  g_audio_room = GetPrivateProfileIntA("bwvr", "audio_room", 1, path) != 0;
  g_audio_units_per_meter = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "audio_units_per_meter", 40, path));
  // Not in the launcher. Flat sounds with a place in the world come from it.
  g_audio_flat_places =
      GetPrivateProfileIntA("bwvr", "audio_flat_places", 1, path) != 0;
  // Not in the launcher. The head and the room distances decide which
  // positional sounds start and stop. 0: the game camera decides.
  g_audio_range = GetPrivateProfileIntA("bwvr", "audio_range", 1, path) != 0;
  // Not in the launcher. 1: a loop out of range keeps its sample slot and
  // is silent. 0: it stops, as in the game.
  g_audio_loops_keep =
      GetPrivateProfileIntA("bwvr", "audio_loops_keep", 1, path) != 0;
  // Test only: a sound tag with this sample on the Creature. 0 is off.
  g_audio_test_tag = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "audio_test_tag", 0, path));
  g_audio_test_tag_bank = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "audio_test_tag_bank", 1, path));
  // The launcher's "Test keys" box. F12 saves both eye pictures and
  // Shift F12 steps the subtitles. Off for players.
  g_test_keys = GetPrivateProfileIntA("bwvr", "test_keys", 0, path) != 0;
  const bool xr_bridge_key =
      GetPrivateProfileIntA("bwvr", "xr_bridge", 0, path) == 1;
  // The bridge to the x64 host gives the frames of Direct3D 11 to the host.
  // backend=vulkan and xr_vulkan=1 have a session with Vulkan, so they use
  // the local runtime (docs/backend.md, "Only Vulkan").
  const bool vulkan_session =
      ProxyBackendVulkan() ||
      GetPrivateProfileIntA("bwvr", "xr_vulkan", 0, path) != 0;
  const bool xr_bridge = xr_bridge_key && !vulkan_session;
  bwvr::XrScreen::ConfigureBridge(xr_bridge);
  ModLog("settings     : xr_bridge=%d, %s", xr_bridge_key ? 1 : 0,
         xr_bridge ? "experimental x64 OpenXR host"
         : xr_bridge_key
             ? "but the session has Vulkan (backend=vulkan or xr_vulkan=1), "
               "and the bridge needs Direct3D 11. The local OpenXR runtime"
             : "local OpenXR runtime");
  // Not in the launcher. Off only to test the fault it fixes.
  g_game_camera_rebuild =
      GetPrivateProfileIntA("bwvr", "game_camera_rebuild", 1, path) != 0;
  g_head_fov_deg = static_cast<float>(
      GetPrivateProfileIntA("bwvr", "head_fov_deg", 0, path));
  g_fov_margin_pct = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "fov_margin_pct", 0, path));
  // An invented head, for testing on the desktop with no headset.
  g_fake_head = GetPrivateProfileIntA("bwvr", "fake_head", 0, path) != 0;
  g_head_camera =
      GetPrivateProfileIntA("bwvr", "head_camera", 1, path) != 0;
  // GetPrivateProfileIntA returns an unsigned value, so a negative angle
  // must go through int first or it arrives as about four billion degrees.
  g_fake_yaw = static_cast<float>(static_cast<int>(
      GetPrivateProfileIntA("bwvr", "fake_head_yaw", 0, path)));
  g_fake_pitch = static_cast<float>(static_cast<int>(
      GetPrivateProfileIntA("bwvr", "fake_head_pitch", 0, path)));
  g_fake_roll = static_cast<float>(static_cast<int>(
      GetPrivateProfileIntA("bwvr", "fake_head_roll", 0, path)));
  g_fake_sweep =
      GetPrivateProfileIntA("bwvr", "fake_head_sweep", 0, path) != 0;
  // A fake controller, for the same reason. Every offset and angle goes
  // through int first, for the same reason as above.
  g_controllers = GetPrivateProfileIntA("bwvr", "controllers", 1, path) != 0;
  // Player keys: which hand points, which trigger is which button, pulses.
  g_left_hand = GetPrivateProfileIntA("bwvr", "left_hand", -1, path);
  if (g_left_hand < -1 || g_left_hand > 1) g_left_hand = -1;
  g_controller_swap_buttons =
      GetPrivateProfileIntA("bwvr", "controller_swap_buttons", 0, path) != 0;
  g_controller_haptics =
      GetPrivateProfileIntA("bwvr", "controller_haptics", 1, path) != 0;
  // With a controller in use, the hints show its buttons. The profile
  // key is a test key. It forces the words of one profile, for example
  // "valve/index_controller", instead of the profile the runtime reports.
  g_controller_hints =
      GetPrivateProfileIntA("bwvr", "controller_hints", 1, path) != 0;
  GetPrivateProfileStringA("bwvr", "hint_profile", "", g_hint_profile,
                           sizeof(g_hint_profile), path);
  g_hint_selftest = GetPrivateProfileIntA("bwvr", "hint_selftest", 0, path) != 0;
  {
    FakeControllerSettings& f = g_fake_controller;
    const auto number = [&](const char* key, int fallback) {
      return static_cast<int>(GetPrivateProfileIntA(
          "bwvr", key, static_cast<UINT>(fallback), path));
    };
    f.mode = number("fake_controller", 0);
    f.right_cm = number("fake_controller_right_cm", 20);
    f.up_cm = number("fake_controller_up_cm", -30);
    f.forward_cm = number("fake_controller_forward_cm", 35);
    f.yaw = number("fake_controller_yaw", 0);
    f.pitch = number("fake_controller_pitch", -30);
    f.roll = number("fake_controller_roll", 0);
    f.sweep = number("fake_controller_sweep", 0) != 0;
    f.sweep_yaw = number("fake_controller_sweep_yaw", 20);
    f.sweep_pitch = number("fake_controller_sweep_pitch", -30);
    GetPrivateProfileStringA("bwvr", "fake_controller_script", "", f.script,
                             sizeof(f.script), path);
    GetPrivateProfileStringA("bwvr", "fake_controller_points", "", f.points,
                             sizeof(f.points), path);
    GetPrivateProfileStringA("bwvr", "fake_controller_left_points", "",
                             f.left_points, sizeof(f.left_points), path);
    // The templates the word "trace" steps through.
    GetPrivateProfileStringA("bwvr", "fake_controller_gestures", "",
                             f.gestures, sizeof(f.gestures), path);
    f.gesture_cm = number("fake_controller_gesture_cm", 30);
    // The test mouse: words that move the pointer and press the left button
    // inside the mod, so a desktop run does not touch the real mouse.
    GetPrivateProfileStringA("bwvr", "fake_mouse_script", "",
                             g_fake_mouse_script, sizeof(g_fake_mouse_script),
                             path);
    g_fake_mouse_step_px = number("fake_mouse_step_px", 8);
    g_fake_mouse_x_pct[0] = number("fake_mouse_x_pct", 50);
    g_fake_mouse_y_pct[0] = number("fake_mouse_y_pct", 50);
    g_fake_mouse_x_pct[1] = number("fake_mouse_x2_pct", 50);
    g_fake_mouse_y_pct[1] = number("fake_mouse_y2_pct", 70);
    // A still lean of the invented head, for the grab tests.
    g_fake_lean_cm[0] = number("fake_head_right_cm", 0);
    g_fake_lean_cm[1] = number("fake_head_up_cm", 0);
    g_fake_lean_cm[2] = number("fake_head_forward_cm", 0);
    // The direct hand: 1 puts the Hand at the controller, 0 keeps the
    // pointer.
    DirectHandSettings& d = g_direct_hand;
    d.mode = number("controller_hand", 1) != 0 ? 1 : 0;
    // The Hand is the real hand: one to one, the size of the real hand. The
    // reach keys stay for tests, and a gain of 0 turns the reach curve off.
    d.reach_start_cm = number("hand_reach_start_cm", 35);
    d.reach_gain = number("hand_reach_gain", 0);
    d.hand_cm = number("hand_real_cm", 19);
    d.scale_pct = number("hand_scale_pct", 100);
    d.reach_anchor_down_cm = number("hand_reach_anchor_down_cm", 20);
    // The throw.
    d.throw_gain_pct = number("throw_gain_pct", 150);
    // The Creature by touch (docs/creature.md, "The Creature by touch"): the camera, the real speed of a
    // slap in m/s, the reach of the body past its radius, and the wait.
    d.creature_camera = number("creature_camera", 2);
    {
      char text[32];
      GetPrivateProfileStringA("bwvr", "creature_slap_mps", "1.0", text,
                               sizeof(text), path);
      const double mps = atof(text);
      d.creature_slap_mps = mps > 0.0 ? static_cast<float>(mps) : 1.0f;
    }
    d.creature_touch_cm = number("creature_touch_cm", 5);
    d.creature_slap_wait = number("creature_slap_wait", 0) != 0;
    // A glow at the point the Hand interacts from. See docs/hand-model.md, "The glow at the Hand's point".
    g_hand_glow = number("hand_glow", 0) != 0;
    // The glow on the mesh of a target of the Hand. See docs/hand-model.md,
    // "The glow on the mesh of a target". target_glow=0 gives the halo.
    g_target_glow.mode = number("target_glow", 1);
    g_target_glow.pct = number("target_glow_pct", 45);
    g_target_glow.anim = number("target_glow_anim", 3);
    // The overlay renderer's test hand. See docs/hand-model.md, "Overlay renderer".
    g_overlay = number("overlay", 1) != 0;
    // The teleport: hold A to aim an arc, let go to go there. See
    // docs/god-view.md, "The teleport". teleport=0 keeps the jump of the A button.
    g_teleport.on = number("teleport", 1) != 0;
    g_teleport.reach_m = number("teleport_reach_m", 10);
    g_teleport.ring_cm = number("teleport_ring_cm", 11);
    g_teleport.ring_mode = number("teleport_ring_mode", 1);
    g_teleport.ring_units = number("teleport_ring_units", 11);
    g_teleport.ring_min_cm = number("teleport_ring_min_cm", 4);
    g_teleport.ring_max_cm = number("teleport_ring_max_cm", 40);
    // The switch of the game that the press of A keeps to: 1 the drag of the
    // land, as the walk with the stick, 0 the double click jump. With
    // test_keys=1, Ctrl and Shift with the comma key change it.
    g_teleport.move_switch = number("teleport_switch", 1);
    g_teleport.test_keys = number("test_keys", 0) != 0;
    // The grab from a distance: point at an object that is out of reach,
    // close the grip and pull toward the head. throw_law=1 sends sqrt(s) v in
    // the God view and on the table, so a throw lands where the real throw
    // lands, times the scale.
    g_far_grab.on = number("far_grab", 1) != 0;
    g_far_grab.cone_deg = number("far_grab_cone_deg", 8);
    g_far_grab.pull_cmps = number("far_grab_pull", 80);
    g_far_grab.choice = number("far_grab_choice", 2);
    // throw_law: 2 is the real flight of a thrown object (the default), 1
    // is the square root law, and 0 is the plain gain. See docs/throw.md, "The
    // throw physics". throw_log=1 logs each flight in full, and 2 also logs
    // each substep and each frame after the first contact.
    g_throw_law = number("throw_law", 2);
    g_throw_log = number("throw_log", 0);
    // throw_slow_x10: the time factor k times 10 (10 to 100). The thrown
    // object flies the real arc, k times slower. throw_trees: 1 lets a tree
    // fly as a rock does.
    g_throw_slow_x10 = number("throw_slow_x10", 20);
    g_throw_trees = number("throw_trees", 1) != 0;
    // release_fall_cm: a hand that is still at the opening of the grip sets
    // the object down only when its point is this many cm of the room over
    // the land or less. Higher up the object falls. 0 sets it down at each
    // height. See docs/throw.md, "The height of a release".
    g_release_fall_cm = number("release_fall_cm", 15);
    // throw_land: what a thrown object does after its first contact. 1 is
    // the time of the land with the low bounce and the brake, 2 is the low
    // bounce and the brake in the time of the flight, 0 is the landing of
    // the game. throw_land_rate_x10: the rate of the land times 10 (10 is the
    // game's own time). throw_bounce_pct: the share of its speed that a
    // bounce keeps, of the speed that the game gives it.
    // throw_land_brake_pct: the friction of the brake of the land, in
    // percent of the friction that the game has for the object.
    g_throw_land = number("throw_land", 1);
    g_throw_land_rate_x10 = number("throw_land_rate_x10", 10);
    g_throw_bounce_pct = number("throw_bounce_pct", 50);
    g_throw_land_brake_pct = number("throw_land_brake_pct", 60);
    // throw_tree_settle: 1 lets a tree that falls turn down fast while it
    // touches the land and is slow. 0 keeps the game's turn.
    g_throw_tree_settle = number("throw_tree_settle", 1);
    // throw_land_rest: 1 lets a turn in place of a thrown object wear off
    // fast after its landing, turns the brake of the land off on land that
    // is steeper than the brake holds, and gives an object that lies still
    // for 0.5 s its rest. 2, 3 and 4 are one of the three, and 0 is none.
    g_throw_land_rest = number("throw_land_rest", 1);
    // catch_rest_guard: 1 holds the end of the physics of an object back
    // while the Hand takes it. 0 lets the game end the physics, and a tree
    // that comes to rest in that time is then a dead tree and not in the
    // Hand. catch_rest_test is a test: 1 makes one step of the object answer
    // "at rest" while the Hand has it.
    g_catch_rest_guard = number("catch_rest_guard", 1);
    g_catch_rest_test = number("catch_rest_test", 0);
    // catch_at_once: 1 puts an object that moves in the hand at the press of
    // the grip: the game sends its pick up at once, the body of the object
    // stays where it is, and the mod draws the object in the Hand. 0 is the
    // pick up of the game, which comes 225 ms and some game turns after the
    // press, and the object moves on until then.
    g_catch_at_once = number("catch_at_once", 1);
    // throw_draw: 1 gives a substep with a touch of another object 5 ms of
    // the picture of a thrown object, and the picture does not go back to a
    // place behind a guess. 0 gives each substep its time of the flight.
    g_throw_draw = number("throw_draw", 1);
    g_far_grab.throw_law = g_throw_law != 0;
    // far_grab_max_m: a far target is at most this many meters of the room
    // from the head, along the floor. 0 gives no limit.
    {
      char text[32];
      GetPrivateProfileStringA("bwvr", "far_grab_max_m", "2.0", text,
                               sizeof(text), path);
      const double meters = atof(text);
      g_far_grab.max_m = meters > 0.0 ? static_cast<float>(meters) : 0.0f;
    }
    // The choice of a far target (docs/grab.md, "The cone and the choice").
    // far_grab_living_deg: the cone of a living thing, for the ranks of
    // far_grab_choice 2 and 4. far_grab_ball_living_deg and
    // far_grab_ball_other_deg: the balls of far_grab_choice 3 and 4.
    // far_grab_stay: 1 lets a living target keep the halo past its cone, by
    // far_grab_stay_deg. FarGrabSetOptions has the limits.
    {
      const auto degrees = [&](const char* key, float fallback) {
        char text[32] = "";
        GetPrivateProfileStringA("bwvr", key, "", text, sizeof(text), path);
        if (text[0] == 0) return fallback;
        char* end = nullptr;
        const double value = strtod(text, &end);
        return end != text ? static_cast<float>(value) : fallback;
      };
      g_far_grab.living_deg =
          degrees("far_grab_living_deg", g_far_grab.living_deg);
      g_far_grab.ball_living_deg =
          degrees("far_grab_ball_living_deg", g_far_grab.ball_living_deg);
      g_far_grab.ball_other_deg =
          degrees("far_grab_ball_other_deg", g_far_grab.ball_other_deg);
      g_far_grab.stay = number("far_grab_stay", 1);
      g_far_grab.stay_deg = degrees("far_grab_stay_deg", g_far_grab.stay_deg);
    }
    // far_take_carry: 1 leaves the grip with the grip machine when the source
    // of a take at a distance goes away while the grip is closed, so the grip
    // carries the bundle. 0 holds the grip back until it opens, and the
    // machine lets the bundle go (docs/grab.md, "A bulk source at a
    // distance").
    g_far_grab.take_carry = number("far_take_carry", 1);
    // take_stream: 1 puts the pieces of the stream of a take at a distance on
    // a curve from the top of the source to the Hand. 0 leaves the line of
    // the game, from the land under the Hand (docs/grab.md, "The stream of a
    // take at a distance").
    g_take_stream = number("take_stream", 1);

    // Moving around by grabbing the world. See docs/god-view.md, "Moving
    // around: the grab and the mouse".
    GrabSettings& g = g_grab;
    // The player's choices: which grips grab, and what two hands do.
    g.on = number("grab_world", 1) != 0;
    g.hand = number("grab_hand", 1);
    g.two_hands = number("grab_two_hands", 1) != 0;
    g.turn = number("grab_turn", 1) != 0;
    g.zoom = number("grab_zoom", 1) != 0;
    // How far the world moves for the hand.
    g.gain_mode = number("grab_gain_mode", 1);
    g.gain_pct = number("grab_gain_pct", 100);
    g.gain_max = number("grab_gain_max", 60);
    g.vertical_pct = number("grab_vertical_pct", 100);
    g.smoothing_ms = number("grab_smoothing_ms", 35);
    g.max_speed = number("grab_max_speed", 0);
    // The grip levels and the limits.
    g.press_pct = number("grab_press_pct", 70);
    g.release_pct = number("grab_release_pct", 50);
    g.limit = number("grab_limit", 3400);
    g.min_height = number("grab_min_height", 6);
    // A test: the gain of the game's own land drag (the land drag probe).
    g.probe = number("grab_probe", 0) != 0;

    // Drawing a gesture in the air. See docs/miracles.md.
    g_gesture.button = number("gesture_button", 1);
    g_gesture.size_cm = number("gesture_size_cm", 30);
  }
  // How many game units a metre is worth. The default keeps the neck and
  // the eyes agreeing: the mod renders with eye_separation world units for
  // a human eye separation of about 66.8 mm, so a metre is that ratio.
  g_head_units_per_meter = static_cast<float>(GetPrivateProfileIntA(
      "bwvr", "head_units_per_meter_x10", 75, path)) / 10.0f;
  // The near plane is at most this far in the room: near_plane_cm with a
  // 24 bit Z buffer, near_plane_cm_16 with a 16 bit one. 0 keeps the game's.
  g_near_plane_cm = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "near_plane_cm", 5, path));
  g_near_plane_cm_16 = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "near_plane_cm_16", 10, path));
  g_skip_prepare =
      GetPrivateProfileIntA("bwvr", "skip_hand_prepare", 0, path) != 0;
  GdiWatchSetEnabled(
      GetPrivateProfileIntA("bwvr", "gdi_watch", 0, path) != 0);
  g_headless_grab =
      GetPrivateProfileIntA("bwvr", "headless_grab", 0, path) != 0;
  g_force_mips = GetPrivateProfileIntA("bwvr", "force_mips", 1, path) != 0;
  g_draw_probe_frames =
      GetPrivateProfileIntA("bwvr", "draw_probe_frames", 120, path);
  g_draw_probe_delay =
      GetPrivateProfileIntA("bwvr", "draw_probe_delay", 0, path);
  g_stereo_headless =
      GetPrivateProfileIntA("bwvr", "stereo_headless", 0, path) != 0;
  g_intro_vr = GetPrivateProfileIntA("bwvr", "intro_vr", 0, path) != 0;
  g_intro_motion = GetPrivateProfileIntA("bwvr", "intro_motion", 1, path);
  g_intro_speed_pct =
      GetPrivateProfileIntA("bwvr", "intro_speed_pct", 100, path);
  g_intro_distance_cm =
      GetPrivateProfileIntA("bwvr", "intro_distance_cm", 300, path);
  g_intro_model_distance_cm =
      GetPrivateProfileIntA("bwvr", "intro_model_distance_cm", 200, path);
  g_intro_eye_trails =
      GetPrivateProfileIntA("bwvr", "intro_eye_trails", 1, path) != 0;
  g_eye_clean = GetPrivateProfileIntA("bwvr", "eye_clean", 1, path) != 0 ? 1 : 0;
  g_eye_clean_test =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "eye_clean_test", 0, path));
  g_eye_clean_test_frame = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "eye_clean_test_frame", 0, path));
  g_frame_send =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "frame_send", 0, path));
  g_draw_merge =
      GetPrivateProfileIntA("bwvr", "draw_merge", 1, path) != 0 ? 1 : 0;
  g_draw_merge_test = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "draw_merge_test", 0, path));
  g_draw_merge_test_frame = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "draw_merge_test_frame", 0, path));
  g_render_thread = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "render_thread", 0, path));
  g_render_thread_test_frame = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "render_thread_test_frame", 0, path));
  g_render_thread_lock_free =
      GetPrivateProfileIntA("bwvr", "render_thread_lock_free", 1, path) != 0
          ? 1
          : 0;
  g_render_thread_filter =
      GetPrivateProfileIntA("bwvr", "render_thread_filter", 1, path) != 0
          ? 1
          : 0;
  g_render_thread_buffers =
      GetPrivateProfileIntA("bwvr", "render_thread_buffers", 1, path) != 0
          ? 1
          : 0;
  g_gpu_flush = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "gpu_flush", 128, path));
  g_render_thread_right_eye =
      GetPrivateProfileIntA("bwvr", "render_thread_right_eye", 1, path) != 0
          ? 1
          : 0;
  g_fast_transform =
      GetPrivateProfileIntA("bwvr", "fast_transform", 1, path) != 0 ? 1 : 0;
  g_fast_transform_check =
      GetPrivateProfileIntA("bwvr", "fast_transform_check", 0, path) != 0 ? 1
                                                                        : 0;
  g_d3d7_count =
      GetPrivateProfileIntA("bwvr", "d3d7_count", 0, path) != 0 ? 1 : 0;
  g_d3d7_thread =
      GetPrivateProfileIntA("bwvr", "d3d7_thread", 1, path) != 0 ? 1 : 0;
  g_d3d7_thread_merge =
      GetPrivateProfileIntA("bwvr", "d3d7_thread_merge", 1, path) != 0 ? 1
                                                                       : 0;
  g_vk_scene =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "vk_scene", 0, path));
  g_vk_scene_test_frame = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "vk_scene_test_frame", 0, path));
  g_vk_scene_follow =
      GetPrivateProfileIntA("bwvr", "vk_scene_follow", 1, path) != 0 ? 1 : 0;
  g_vk_panel = GetPrivateProfileIntA("bwvr", "vk_panel", 1, path) != 0 ? 1 : 0;
  g_vk_only = GetPrivateProfileIntA("bwvr", "vk_only", 1, path) != 0 ? 1 : 0;
  g_vk_only_test =
      GetPrivateProfileIntA("bwvr", "vk_only_test", 0, path) != 0 ? 1 : 0;
  g_vk_upload_queue =
      GetPrivateProfileIntA("bwvr", "vk_upload_queue", 1, path) != 0 ? 1 : 0;
  g_vk_present_late =
      GetPrivateProfileIntA("bwvr", "vk_present_late", 1, path) != 0 ? 1 : 0;
  g_vk_texture_reuse =
      GetPrivateProfileIntA("bwvr", "vk_texture_reuse", 1, path) != 0 ? 1 : 0;
  g_vk_keep_picture =
      GetPrivateProfileIntA("bwvr", "vk_keep_picture", 1, path) != 0 ? 1 : 0;
  g_vk_black_check =
      GetPrivateProfileIntA("bwvr", "vk_black_check", 1, path) != 0 ? 1 : 0;
  g_vk_untextured =
      GetPrivateProfileIntA("bwvr", "vk_untextured", 1, path) != 0 ? 1 : 0;
  g_vk_stage1 =
      GetPrivateProfileIntA("bwvr", "vk_stage1", 1, path) != 0 ? 1 : 0;
  g_foveation =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "foveation", 0, path));
  g_foveation_inner_deg = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "foveation_inner_deg", 0, path));
  g_foveation_outer_deg = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "foveation_outer_deg", 0, path));
  g_foveation_gaze =
      GetPrivateProfileIntA("bwvr", "foveation_gaze", 1, path) != 0 ? 1 : 0;
  g_foveation_show =
      GetPrivateProfileIntA("bwvr", "foveation_show", 0, path) != 0 ? 1 : 0;
  g_fovea_res =
      GetPrivateProfileIntA("bwvr", "fovea_res", 0, path) != 0 ? 1 : 0;
  g_fovea_res_outer_pct = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "fovea_res_outer_pct", 50, path));
  g_fovea_res_center_pct = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "fovea_res_center_pct", 100, path));
  g_fovea_res_center_deg = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "fovea_res_center_deg", 25, path));
  g_fovea_res_center_down_deg = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "fovea_res_center_down_deg", 0, path));
  g_vk_scene_reread_test =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "vk_scene_reread_test", 0,
                                             path));
  g_xr_vulkan =
      GetPrivateProfileIntA("bwvr", "xr_vulkan", 0, path) != 0 ? 1 : 0;
  g_vk_eyes = GetPrivateProfileIntA("bwvr", "vk_eyes", 1, path) != 0 ? 1 : 0;
  g_xr_vulkan_upload =
      GetPrivateProfileIntA("bwvr", "xr_vulkan_upload", 1, path) != 0 ? 1 : 0;
  g_xr_hold_ms = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "xr_hold_ms", 30, path));
  g_render_thread_late_end =
      GetPrivateProfileIntA("bwvr", "render_thread_late_end", 1, path) != 0
          ? 1
          : 0;
  g_thread_wait =
      GetPrivateProfileIntA("bwvr", "thread_wait", 1, path) != 0 ? 1 : 0;
  g_test_cores =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "test_cores", 0, path));
  g_test_smt =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "test_smt", 0, path));
  g_test_spin_walk_us = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "test_spin_walk_us", -1, path));
  g_test_spin_idle_us = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "test_spin_idle_us", -1, path));
  g_test_wake_records = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "test_wake_records", -1, path));
  GetPrivateProfileStringA("bwvr", "ab_test", "", g_ab_test,
                           sizeof(g_ab_test), path);
  g_profile_gpu =
      static_cast<int>(GetPrivateProfileIntA("bwvr", "profile_gpu", 0, path));
  g_bubble_card =
      GetPrivateProfileIntA("bwvr", "bubble_card", 1, path) != 0 ? 1 : 0;
  g_name_card =
      GetPrivateProfileIntA("bwvr", "name_card", 1, path) != 0 ? 1 : 0;
  g_bubble_card_test =
      GetPrivateProfileIntA("bwvr", "bubble_card_test", 0, path) != 0 ? 1 : 0;
  g_bubble_card_test_frame = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "bubble_card_test_frame", 0, path));
  g_intro_text_place =
      GetPrivateProfileIntA("bwvr", "intro_text_place", 1, path) != 0;
  g_intro_push_once =
      GetPrivateProfileIntA("bwvr", "intro_push_once", 1, path) != 0;
  g_intro_motion_test_seconds =
      GetPrivateProfileIntA("bwvr", "intro_motion_test_seconds", 0, path);
  g_intro_push_test_px =
      GetPrivateProfileIntA("bwvr", "intro_push_test_px", 0, path);
  g_intro_controller =
      GetPrivateProfileIntA("bwvr", "intro_controller", 1, path) != 0;
  g_intro_hand_push_pct = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "intro_hand_push_pct", 100, path));
  g_intro_hand_point =
      GetPrivateProfileIntA("bwvr", "intro_hand_point", 1, path) != 0;
  g_intro_push_3d =
      GetPrivateProfileIntA("bwvr", "intro_push_3d", 1, path) != 0;
  g_intro_push_radius_cm = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "intro_push_radius_cm", 30, path));
  g_intro_hand_show = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "intro_hand_show", 1, path));
  g_intro_hand_depth =
      GetPrivateProfileIntA("bwvr", "intro_hand_depth", 1, path) != 0;
  g_intro_probe = GetPrivateProfileIntA("bwvr", "intro_probe", 0, path) != 0;
  g_fix_unscaled =
      GetPrivateProfileIntA("bwvr", "fix_unscaled", 1, path) != 0;
  g_fix_copy = GetPrivateProfileIntA("bwvr", "fix_copy", 1, path) != 0;
  g_fix_inverse =
      GetPrivateProfileIntA("bwvr", "fix_inverse", 1, path) != 0;
  g_fix_random =
      GetPrivateProfileIntA("bwvr", "fix_random", 0, path) != 0;
  g_move_camera_point =
      GetPrivateProfileIntA("bwvr", "move_camera_point", 0, path) != 0;
  // Not in the launcher. 1: the mod shuts down on the exit path of the game,
  // while each thread runs and each DLL is loaded. 0: it shuts down when
  // Windows unloads the DLL. See docs/backend.md, "The end of the mod".
  g_exit_shutdown =
      GetPrivateProfileIntA("bwvr", "exit_shutdown", 1, path) != 0;
  // Test only: the frame at which the mod asks the game to end by itself.
  ExitTestSetFrame(static_cast<int>(
      GetPrivateProfileIntA("bwvr", "exit_test_frame", 0, path)));
  // Test only: the mod drops each write of the game to the registry, from
  // the start of the mod.
  g_registry_test_guard =
      GetPrivateProfileIntA("bwvr", "registry_test_guard", 0, path) != 0;
  g_vr_full_screen =
      GetPrivateProfileIntA("bwvr", "vr_full_screen", 0, path) != 0;
  g_vr_start = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "vr_start", 1, path));
  if (g_vr_start < 0 || g_vr_start > 2) g_vr_start = 1;
  g_vr_start_test_hang_try = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "vr_start_test_hang_try", 0, path));
  g_vr_start_test_hang_ms = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "vr_start_test_hang_ms", 0, path));
  g_vr_start_test_fail_session = static_cast<int>(
      GetPrivateProfileIntA("bwvr", "vr_start_test_fail_session", 0, path));
  ModLog("settings     : vr=%d, stereo=%d, screen %.2f m wide at %.2f m",
         g_vr_enabled ? 1 : 0, g_stereo ? 1 : 0, g_width, g_distance);
  ModLog("settings     : eye separation %.3f world units, interface shift "
         "%.0f px", g_separation, g_ui_shift);
}

void ModLogNowhere(const char* format, ...);
double MsSince(const LARGE_INTEGER& start);
void StartVrFromThread();
void VrSessionRuns(bwvr::XrScreen* screen, uint32_t width, uint32_t height);

// Starting the VR session needs the Direct3D device, which does not exist
// until the game has opened it. So this runs on a frame, not in DllMain, where
// almost nothing is safe to do anyway.
//
// It keeps trying rather than giving up after one go. The runtime is
// registered as soon as Virtual Desktop is installed, but it reports no head
// mounted display until the headset actually connects, so a single attempt at
// startup forces the player to connect first and launch second. Getting that
// order wrong looks exactly like the mod is broken. Retrying lets the headset
// arrive whenever it likes.
//
// With no headset, a try can only fail or wait for the runtime. So with
// vr_start=1 and 2 the tries run on a thread of the mod (vr_start.cpp), and
// the frame thread only looks at that thread once a frame.
void StartVrStep() {
  if (g_vr_thread) {
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    StartVrFromThread();
    g_vr_code_ms += MsSince(start);
    return;
  }

  const DWORD now = GetTickCount();
  if (g_next_vr_try != 0 && now < g_next_vr_try) return;
  g_next_vr_try = now + kVrRetryMs;

  uint32_t width = 0, height = 0;
  if (!FrameSourceSize(&width, &height)) return;

  // The hooks go in once, on the first frame that has a render target, so a
  // headless run and a retrying VR run both get them.
  if (!g_hooks_installed && g_stereo) {
    g_hooks_installed = true;
    TextureFilterSetOptions(g_filter_survey, g_force_mips);
    TextureFilterInstall(g_anisotropy);
    GdiWatchInstall();
    PassGuardSetOptions(g_skip_hand_pass2, g_skip_physics_pass2);
    PassGuardInstall();
    FrameTimersInstall();
    TickDirectInstall(g_tick_direct);
    FovInstall();
    InputWatchInstall();
    PointerMapInstall();
    HeadCameraInstall();
    PointerMapSetRebuild(g_game_camera_rebuild);
    ComfortCameraSetOptions(g_snap_turn_deg, g_instant_jumps, g_camera_glide,
                            g_blink_ms);
    ComfortCameraSetZoomStep(g_zoom_step);
    ComfortCameraSetTurnHelp(g_turn_help_degrees, g_god_turn_keeps_focus,
                             g_tilt_help_degrees);
    ComfortCameraInstall();
    ScriptCameraSetOptions(g_script_camera, g_script_min_distance,
                           g_script_min_height, g_script_close_distance,
                           g_script_close_zoom, g_script_cut_degrees,
                           g_script_cut_percent, g_script_follow);
    ScriptCameraInstall();
    PointerMapSetDragSmoothing(g_drag_smoothing_ms);
    PointerMapSetFakeMouse(g_fake_mouse_script, g_fake_mouse_step_px,
                           g_fake_mouse_x_pct, g_fake_mouse_y_pct);
    SkySetCloudPoleShare(g_cloud_pole_pct);
    SkySetHeadFree(g_clouds_head_free, g_moon_world_up, g_sky_test_frame,
                   g_sky_test_trace, g_sky_test_hour);
    SkySetGlareMode(g_sun_glare);
    SkyDetailInstall();
    SpiritAnchorSetEnabled(g_spirit_anchor);
    SpiritAnchorSetWorldSize(g_spirit_world_size, g_test_keys,
                             g_spirit_world_size_test,
                             g_spirit_point_world_test);
    SpiritAnchorInstall();
    FightMarksInstall();
    AudioSetRoom(g_audio_room, g_audio_units_per_meter,
                 g_audio_flat_places);
    AudioSetRange(g_audio_range, g_audio_loops_keep, g_audio_test_tag,
                  g_audio_test_tag_bank);
    AudioInstall(g_audio_head, g_audio_advisors, g_audio_trace);
    DetailDistanceSet(g_detail_distance);
    SnapshotGuardInstall();
    PresentWatchSetEnabled(true);
    PresentWatchInstall();
    FrameProfileSetLevel(g_profile);
    FrameProfileInstall();
    if (!g_vulkan_only) GpuBridgeProbeInterop();
    ModLog("vr           : game renders at %ux%u", width, height);
    // The pointer is scaled onto this, so the window may be any size.
    PointerMapSetRenderSize(static_cast<int>(width),
                            static_cast<int>(height));
    UiDepthInstall(g_draw_probe);
    // The draw of the advisors, for the cut scene window.
    CutsceneWindowInstall();
    UiDepthSetAccounting(g_ui_accounting);
    UiDepthSetHintMode(g_hint_mode);
    UiDepthSetHintSelfTest(g_hint_selftest);
    UiDepthSetHintScale(g_hint_scale);
    UiDepthSetHintFlat(g_hint_flat);
    UiDepthSetSubtitleLift(g_subtitle_lift);
    UiDepthSetSubtitleDegrees(g_subtitle_degrees);
    // After UiDepthInstall, and before GpuBridgeStart below.
    UiPanelSetOptions(g_ui_panel);
    PassGuardSetSkipPrepare(g_skip_prepare);
    HeadPoseSetScale(g_head_units_per_meter);
    StereoSetHeadFov(g_head_fov_deg);
    StereoSetFovMargin(g_fov_margin_pct);
    HeadPoseSetFake(g_fake_head, g_fake_yaw, g_fake_pitch, g_fake_roll);
    HeadPoseFakeSweep(g_fake_sweep);
    if (g_fake_head) {
      HeadPoseSetFakeLean(g_fake_lean_cm[0], g_fake_lean_cm[1],
                          g_fake_lean_cm[2]);
    }
    ControllersSetOptions(g_controllers, g_left_hand,
                          g_controller_haptics, g_controller_swap_buttons);
    ControllersSetFake(g_fake_controller);
    ControllersInstall();
    ControllerHintsSetOptions(g_controller_hints, g_hint_profile);
    ControllerHintsInstall();
    if (g_controllers) {
      ControllersSetDirect(g_direct_hand);
      if (g_direct_hand.mode == 1) {
        FarGrabSetOptions(g_far_grab);
        HandDirectInstall();
        MiracleInstall();
        VillageInstall();
        ThrowPhysicsSetOptions(g_throw_law, g_throw_log, g_test_keys);
        ThrowPhysicsSetSlow(g_throw_slow_x10, g_throw_trees);
        ThrowPhysicsSetFall(g_release_fall_cm);
        ThrowPhysicsSetLanding(g_throw_land, g_throw_land_rate_x10,
                               g_throw_bounce_pct, g_throw_land_brake_pct);
        ThrowPhysicsSetSettle(g_throw_tree_settle != 0);
        ThrowPhysicsSetRest(g_throw_land_rest);
        ThrowPhysicsSetTake(g_catch_rest_guard != 0, g_catch_rest_test);
        ThrowPhysicsSetCatch(g_catch_at_once);
        ThrowPhysicsSetDraw(g_throw_draw);
        ThrowPhysicsInstallEarly();
        FarGrabInstall();
        TakeStreamSetOptions(g_take_stream, g_test_keys);
        TakeStreamInstall();
        ThrowPhysicsInstall();
        HandGlowSetEnabled(g_hand_glow);
        g_target_glow.test_keys = g_test_keys;
        TargetGlowSetOptions(g_target_glow);
        TargetGlowInstall();
      }
      ControllersSetGrab(g_grab);
      WorldGrabSetOptions(g_grab);
      PointerMapSetGrabProbe(g_grab.probe);
      ControllersSetGesture(g_gesture);
      if (g_gesture.button != 0) GestureAirInstall();
      if (g_overlay) OverlaySetEnabled(true);
    }
    // The Creature without camera moves. With or without the controllers,
    // because a mouse in the God view needs it too.
    HandDirectInstallCreatureCamera(g_direct_hand.creature_camera);
    // The teleport also takes the mouse double click of the God view, so it
    // is set with or without the controllers.
    TeleportSetOptions(g_teleport);
    if (g_fov_absolute_deg > 0.0f) {
      FovSetAbsolute(g_fov_absolute_deg / 57.29578f);
    }
    // The engine's own camera carries the head by default, because a
    // poke inside the scene walk arrives too late for anything the engine
    // built earlier in the frame, and the sea is one of those things.
    HeadCameraSetEnabled(g_head_tracking && g_head_camera);
    // Only with play_mode=3 and 4. With any other mode the table hooks
    // nothing.
    if (g_tabletop) {
      TabletopSetOptions(g_table);
      TabletopInstall();
      // The pick of the direct hand on the table (docs/tabletop.md, "The hand on the table").
      // The God view needs it too: zoomed out, a villager is a few pixels.
      if (g_controllers && g_direct_hand.mode == 1) HandDirectInstallTable();
    }
    // The near plane of the game camera. Only where the head moves the
    // camera (play modes 0, 3 and 4), because there the real hand is in the
    // world.
    if (g_head_tracking && g_immersive) {
      NearPlaneSetOptions(g_near_plane_cm, g_near_plane_cm_16);
      NearPlaneInstall();
    }
    // The thumbnail reads past the end of a square window, so it goes.
    SnapshotGuardSetSkip(g_skip_snapshot);
    WaterModeSetSimple(g_water_simple);
    HeadCameraSetRoll(g_head_roll);
    StereoSetImmersive(g_immersive);
    UiDepthSetUiScale(g_ui_scale_pct);
    UiDepthSetCinemaBars(g_cinema_bars);
    PointerMapSetWindowSize(g_window_w, g_window_h);
    PointerMapSetOptions(g_pointer_scale, g_pointer_clip,
                         g_pointer_leave_cursor);
    HeadCameraSetHandAnchored(g_hand_anchored);
    StereoSetHeadTracking(g_head_tracking);
    StereoSetFixes(g_freeze_time, g_move_camera_point);
    FrameTimeSetOptions(g_frame_time_real, g_frame_time_test_frame,
                        g_frame_time_test_readers,
                        g_frame_time_test_readers_frames);
    OneWalkSetCheck(g_one_walk_check);
    // one_walk_merge is a part of the render thread.
    if (!g_vulkan_only) OneWalkSetMerge(g_one_walk_merge);
    OneWalkSetMarks(g_one_walk_marks, g_one_walk_marks_test_frame);
    if (g_one_walk) OneWalkSetWanted(true);
    StereoSetHeadless(g_stereo_headless);
    StereoSetProfile(g_profile);
    StereoSetTestKeys(g_test_keys);
    StereoSetCaptureInterval(g_capture_seconds);
    StereoSetHeadlessGrab(g_headless_grab);
    StereoSetUiDepth(g_ui_adaptive, g_ui_shift_max);
    EyeCameraSetFixes(g_fix_unscaled, g_fix_copy, g_fix_inverse, g_fix_random);
    EyeCameraReport();
    if (g_draw_probe) {
      DrawProbeArm(true, g_draw_probe_frames);
      DrawProbeDelay(g_draw_probe_delay);
    }
    StereoInstall(nullptr, g_separation, g_ui_shift, height);
    IntroSetOptions(g_intro_vr, g_intro_motion, g_intro_speed_pct,
                    g_intro_distance_cm, g_intro_model_distance_cm,
                    g_intro_eye_trails, g_intro_text_place, g_intro_push_once,
                    g_intro_controller, g_intro_probe, g_test_keys);
    IntroSetTests(g_intro_motion_test_seconds, g_intro_push_test_px);
    IntroSetHandPush(g_intro_hand_push_pct, g_intro_hand_point);
    IntroSetHands(g_intro_push_3d, g_intro_push_radius_cm, g_intro_hand_show,
                  g_intro_hand_depth);
    IntroInstall();
    EyeCleanSetOptions(g_eye_clean, g_eye_clean_test, g_eye_clean_test_frame);
    EyeCleanInstall();
    FrameSendSetOptions(g_frame_send);
    ThreadWaitLimitCores(g_test_cores, g_test_smt);
    ThreadWaitSetOptions(g_thread_wait, g_test_spin_walk_us,
                         g_test_spin_idle_us, g_test_wake_records);
    // The merge of draw calls, the render thread, gpu_flush and the D3D7
    // thread give the calls of a walk to Direct3D 9. With vulkan_only=1 they
    // do not start, and their keys and test keys do nothing.
    if (g_vulkan_only) {
      DrawMergeOff();
    } else {
      DrawMergeSetOptions(g_draw_merge, g_draw_merge_test,
                          g_draw_merge_test_frame);
      RenderThreadSetOptions(g_render_thread, g_render_thread_test_frame,
                             g_render_thread_lock_free, g_render_thread_filter,
                             g_render_thread_buffers, g_gpu_flush,
                             g_profile_gpu);
      RenderThreadSetRightEye(g_render_thread_right_eye != 0);
      RenderThreadSetAbTest(g_ab_test);
    }
    FastTransformSetOptions(g_fast_transform);
    FastTransformSetCheck(g_fast_transform_check);
    FastTransformInstall();
    D3D7CountSetOptions(g_d3d7_count);
    if (!g_vulkan_only) {
      D3D7ThreadSetOptions(g_d3d7_thread);
      D3D7ThreadSetMerge(g_d3d7_thread_merge);
    }
    // backend=vulkan has no Direct3D 9 and no Direct3D 11, so the session
    // is a session with Vulkan, and the eye pictures come from the renderer.
    const bool direct = VkSceneDirect();
    if (direct && (g_xr_vulkan == 0 || g_vk_eyes == 0)) {
      g_xr_vulkan = 1;
      g_vk_eyes = 1;
      ModLog("vr           : backend=vulkan, so xr_vulkan=1 and vk_eyes=1");
    }
    // With no Direct3D 11 there is no relay, so the pixels of the CPU go
    // into the swapchains with Vulkan.
    if (g_vulkan_only && g_xr_vulkan_upload == 0) {
      g_xr_vulkan_upload = 1;
      ModLog("vr           : vulkan_only=1, so xr_vulkan_upload=1");
    }
    // The eye pictures of the Vulkan renderer need the renderer.
    const bool vk_eyes = g_xr_vulkan != 0 && g_vk_eyes != 0;
    VkSceneSetOptions(g_vk_scene != 0 || vk_eyes || direct ? 1 : 0,
                      g_vk_scene_test_frame);
    VkSceneSetEyes(vk_eyes);
    VkSceneSetFollow(g_vk_scene_follow);
    VkSceneSetPanel(g_vk_panel);
    VkSceneSetOnly(g_vk_only, g_vk_only_test);
    VkSceneSetUploads(g_vk_upload_queue, g_vk_scene_reread_test);
    VkSceneSetPresentLate(g_vk_present_late);
    VkSceneSetTextureReuse(g_vk_texture_reuse);
    VkSceneSetKeepPicture(g_vk_keep_picture);
    VkSceneSetBlackCheck(g_vk_black_check);
    VkSceneSetUntextured(g_vk_untextured);
    VkSceneSetStage1(g_vk_stage1);
    VkSceneSetFoveation(g_foveation, g_foveation_inner_deg,
                        g_foveation_outer_deg, g_foveation_show);
    VkSceneSetFoveaRes(g_fovea_res, g_fovea_res_outer_pct,
                       g_fovea_res_center_pct, g_fovea_res_center_deg,
                       g_fovea_res_center_down_deg);
    if ((g_foveation != 0 || g_fovea_res != 0) && g_vk_scene == 0 &&
        !vk_eyes && !direct) {
      ModLog("vk scene     : foveation=%d and fovea_res=%d, but the eye "
             "pictures come from Direct3D 9. Both need backend=vulkan, or "
             "xr_vulkan=1 with vk_eyes=1",
             g_foveation, g_fovea_res);
    }
    // With a session with Vulkan the renderer takes its device, so it waits
    // for the session.
    if (g_xr_vulkan != 0 && g_vr_enabled && !g_stereo_headless) {
      vk::WaitForExternal(true);
    }
    if (!g_vulkan_only) {
      RenderThreadSetLateEnd(g_render_thread_late_end != 0);
    }
    BubbleCardSetOptions(g_bubble_card, g_name_card, g_bubble_card_test,
                         g_bubble_card_test_frame);
    BubbleCardInstall();
  }

  // Headless measuring runs the two passes and throws the pictures away, so
  // it needs no runtime, no headset and no Virtual Desktop. Skipping the
  // session keeps a measuring run to one command on the desktop.
  if (g_stereo_headless) {
    g_vr_gave_up = true;
    vk::WaitForExternal(false);
    return;
  }

  // A full screen game and the VR compositor both want the display, and the
  // launcher always sets a window. So in a full screen game the mod does not
  // start a session, and the game stays on the monitor. vr_full_screen=1
  // starts one.
  if (!g_vr_full_screen && GpuBridgeFullScreen()) {
    g_vr_gave_up = true;
    vk::WaitForExternal(false);
    ModLog("vr           : vr_full_screen=0. The game is full screen, so the "
           "mod does not use the headset. Start the game from the launcher");
    return;
  }

  g_vr_watch = true;
  if (g_vr_start != 0) {
    VrStartOptions options = {};
    options.mode = g_vr_start;
    options.retry_ms = kVrRetryMs;
    options.max_tries = kMaxVrTries;
    options.test_hang_try = g_vr_start_test_hang_try;
    options.test_hang_ms = g_vr_start_test_hang_ms;
    options.vulkan = g_xr_vulkan != 0;
    options.d3d11 = !g_vulkan_only;
    options.eye_gaze = g_foveation != 0 && g_foveation_gaze != 0;
    if (VrStartBegin(options)) {
      g_vr_thread = true;
      return;
    }
    // VrStartBegin wrote why. The tries run here, as with vr_start=0.
    g_vr_start = 0;
  }

  LARGE_INTEGER try_start;
  QueryPerformanceCounter(&try_start);
  bwvr::XrScreen* screen = new bwvr::XrScreen();
  screen->UseVulkan(g_xr_vulkan != 0);
  screen->UseD3D11(!g_vulkan_only);
  screen->WantEyeGaze(g_foveation != 0 && g_foveation_gaze != 0);
  screen->SetVulkanUpload(g_xr_vulkan_upload != 0);
  screen->SetHoldMs(g_xr_hold_ms);
  screen->SetPlacement(g_distance, g_width);
  screen->EnablePanel(g_stereo && g_ui_panel.mode == kUiPanelLayer);
  // Only the first failure and every tenth after it reach the log. A message
  // every three seconds for two minutes buries everything else.
  const bool quiet =
      !g_vr_fallback_try && (g_vr_tries > 0) && (g_vr_tries % 10 != 0);
  if (!screen->Start(width, height, g_stereo ? 2 : 1,
                     quiet ? &ModLogNowhere : &ModLog)) {
    // A session with Vulkan that the GPU or the runtime cannot have. With
    // vulkan_only=1 there is no session with Direct3D 11, so the game stays
    // on the monitor. With backend=vulkan the next start uses dxwrapper and
    // a session with Direct3D 11 (VkFallbackFault).
    if (screen->VulkanFailed()) {
      VkFallbackFault("the session with Vulkan did not start (the openxr "
                      "lines before name the call)");
    }
    if (screen->VulkanFailed() && g_vulkan_only) {
      delete screen;
      g_vr_gave_up = true;
      vk::WaitForExternal(false);
      ModLog("vr           : the session with Vulkan did not start, and "
             "vulkan_only=1 has no session with Direct3D 11. The game stays "
             "on the monitor");
      return;
    }
    // Else the next try is a session with Direct3D 11, with the eye pictures
    // of Direct3D 9.
    if (screen->VulkanFailed()) {
      g_xr_vulkan = 0;
      vk::WaitForExternal(false);
      VkSceneSetEyes(false);
      ModLog("vr           : xr_vulkan=1, but the session with Vulkan did "
             "not start. The next try is a session with Direct3D 11");
    }
    delete screen;
    g_vr_code_ms += MsSince(try_start);
    ++g_vr_tries;
    if (g_vr_fallback_try) {
      g_vr_fallback_try = false;
      ModLog("vr           : the start on the frame thread failed too, in try "
             "%d. The tries go on to the end of the 2 minutes (vr_start=0)",
             g_vr_tries);
    }
    // backend=vulkan: the window of the game needs the renderer. After three
    // tries (about 9 s) it draws with a device of its own, and a session
    // that starts later gets no image of it (VkSceneEyesWanted).
    if (g_vr_tries >= 3 && VkSceneDirect() && vk::Pending()) {
      vk::WaitForExternal(false);
      ModLog("vr           : no session after 3 tries, so the Vulkan renderer "
             "draws with a device of its own. A session that starts later "
             "gets no picture of it: start the game with the headset ready");
    }
    if (g_vr_tries == 1) {
      ModLog("vr           : waiting for the headset, will keep trying for "
             "%d seconds. The tries run on the frame thread, with a new "
             "instance for each try (vr_start=0)",
             (kMaxVrTries * kVrRetryMs) / 1000);
    }
    if (g_vr_tries >= kMaxVrTries) {
      g_vr_gave_up = true;
      vk::WaitForExternal(false);
      ModLog("vr           : no headset arrived, the game stays on the "
             "monitor");
    }
    return;
  }
  g_vr_code_ms += MsSince(try_start);
  VrSessionRuns(screen, width, height);
}

// THE TRIES ON THE THREAD OF THE MOD (vr_start=1 and 2). Each frame, with no
// wait for that thread.
void StartVrFromThread() {
  // The same rule as for each try on the frame thread.
  if (!g_vr_full_screen && GpuBridgeFullScreen()) {
    VrStartCancel();
    g_vr_gave_up = true;
    vk::WaitForExternal(false);
    ModLog("vr           : vr_full_screen=0. The game is full screen, so the "
           "mod does not use the headset. Start the game from the launcher");
    return;
  }
  bool over = false;
  bwvr::XrScreen* screen = VrStartTake(&over);
  if (over) {
    if (g_vr_fell_back) {
      // The thread ended its instance, so the loader allows a new one. The
      // next try is the try of vr_start=0, and the tries of the thread
      // count, so the tries keep their 2 minutes.
      g_vr_thread = false;
      g_vr_fallback_try = true;
      g_vr_tries =
          vr_start_math::TriesAfterFallBack(VrStartTries(), kMaxVrTries);
      g_next_vr_try = 0;
      return;
    }
    g_vr_gave_up = true;
    vk::WaitForExternal(false);
    return;
  }
  // backend=vulkan: the window of the game needs the renderer, as with the
  // tries on the frame thread.
  if (screen == nullptr && VrStartTries() >= 3 && VkSceneDirect() &&
      vk::Pending()) {
    vk::WaitForExternal(false);
    ModLog("vr           : no session after 3 tries, so the Vulkan renderer "
           "draws with a device of its own. A session that starts later "
           "gets no picture of it: start the game with the headset ready");
  }
  if (screen == nullptr) return;

  // THE SESSION RUNS ON THE FRAME THREAD. The thread found the headset, so
  // this part runs one time, as the last part of a try on the frame thread
  // does. The frame thread uses the session and its swapchains from the
  // next frame on, and GpuBridgeStart opens the shared textures of the game
  // on the Direct3D 11 device of the session at once, on this thread.
  const char* failed = nullptr;
  uint32_t width = 0, height = 0;
  if (vr_start_math::TestFailSession(g_vr_start_test_fail_session, true)) {
    failed = "the test key vr_start_test_fail_session, before StartSession";
  } else if (!FrameSourceSize(&width, &height)) {
    failed = "the size of the render target, before StartSession";
  } else {
    screen->SetVulkanUpload(g_xr_vulkan_upload != 0);
    screen->SetHoldMs(g_xr_hold_ms);
    screen->SetPlacement(g_distance, g_width);
    screen->EnablePanel(g_stereo && g_ui_panel.mode == kUiPanelLayer);
    screen->SetLog(&ModLog);
    if (!screen->StartSession(width, height, g_stereo ? 2 : 1)) {
      failed = "StartSession (the openxr line before names the call)";
    }
  }
  if (!vr_start_math::FallBack(g_vr_start, failed == nullptr)) {
    VrStartSessionRuns();
    VrSessionRuns(screen, width, height);
    return;
  }
  // THE FALL BACK. A session part that the thread of the tries handed over
  // failed, so the mod uses the tries of vr_start=0 for the rest of the run.
  // The thread ends this instance and its tries, and VrStartTake then says
  // that it is over.
  ModLog("vr           : the start through the thread of the tries failed at "
         "%s, so the mod starts the session on the frame thread as before "
         "(vr_start=0)", failed);
  g_vr_fell_back = true;
  g_vr_start = vr_start_math::kOnFrameThread;
  VrStartGiveBack(screen);
}

// A screen with a session that runs, from either way of the tries.
void VrSessionRuns(bwvr::XrScreen* screen, uint32_t width, uint32_t height) {
  // The attempt that succeeded may have been a quiet one, and the sink Start
  // was given lasts for the whole session. Put the real log back, or every
  // openxr line after this point goes nowhere: the eye angles, the measured
  // separation, and the size the headset asks for.
  screen->SetLog(&ModLog);

  // The Vulkan renderer draws with the device of a session with Vulkan.
  bwvr::XrVulkan session_vulkan;
  if (screen->VulkanInfo(&session_vulkan)) {
    vk::External external = {};
    external.get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        session_vulkan.get_instance_proc_addr);
    external.instance = static_cast<VkInstance>(session_vulkan.instance);
    external.physical = static_cast<VkPhysicalDevice>(session_vulkan.physical);
    external.device = static_cast<VkDevice>(session_vulkan.device);
    external.queue_family = session_vulkan.queue_family;
    external.queue_index = session_vulkan.queue_index;
    external.max_anisotropy = session_vulkan.max_anisotropy;
    external.color_format = static_cast<VkFormat>(session_vulkan.eye_format);
    external.shading_rate = session_vulkan.shading_rate;
    external.window_present = session_vulkan.window_present;
    external.lock = &bwvr::XrScreen::LockQueue;
    external.unlock = &bwvr::XrScreen::UnlockQueue;
    vk::UseExternal(external);
    ModLog("vr           : xr_vulkan=1. A session with Vulkan. The Vulkan "
           "renderer takes its device. xr_vulkan_upload=%d: %s",
           g_xr_vulkan_upload,
           g_xr_vulkan_upload != 0
               ? "the pixels of the CPU go into the swapchains with Vulkan"
               : "the pixels of the CPU go through the relays of Direct3D 11");
  }

  // WRITE THE HEADSET'S OWN SIZE BACK TO THE SETTINGS.
  //
  // The launcher runs before the game and cannot ask the headset without
  // starting a session of its own. So the game leaves the answer where the
  // launcher already reads, and the launcher offers a percentage of it.
  {
    uint32_t want_w = 0, want_h = 0;
    if (screen->RecommendedSize(&want_w, &want_h)) {
      char path[MAX_PATH];
      char text[32];
      lstrcpynA(path, g_dir, MAX_PATH);
      lstrcatA(path, "bwvr.ini");
      _snprintf_s(text, sizeof(text), _TRUNCATE, "%u", want_w);
      WritePrivateProfileStringA("bwvr", "headset_width", text, path);
      _snprintf_s(text, sizeof(text), _TRUNCATE, "%u", want_h);
      WritePrivateProfileStringA("bwvr", "headset_height", text, path);
    }
  }

  g_screen = screen;
  StereoSetScreen(g_screen);
  FrameSendSetScreen(g_screen);
  IntroSetScreen(g_screen);
  ControllersSetScreen(g_screen);

  if (g_vulkan_only) {
    ModLog("vr           : running, the frame reaches the headset with Vulkan, "
           "with no readback");
    LogDirect3DModules("the session runs");
    return;
  }
  // With a backend that keeps the frame on the GPU, the readback can go.
  // This is allowed to fail: the CPU path stays available underneath it.
  if (GpuBridgeHasDevice()) {
    GpuBridgeStart(g_screen->D3D11Device(), width, height,
                   g_screen->SwapchainFormat());
  }
  ModLog("vr           : running, frame reaches the headset %s",
         GpuBridgeReady() ? "without a readback" : "through a readback");
}

// Each frame until a session runs or the tries are over. The frame watch
// says how long the frames are while the tries run, and how much of each
// frame the tries take on the frame thread.
void StartVrOnce() {
  if (g_screen != nullptr || g_vr_gave_up) return;
  g_vr_code_ms = 0.0;
  StartVrStep();
  if (!g_vr_watch) return;
  VrWatchFrame(g_vr_code_ms, g_vr_start);
  if (g_screen != nullptr) {
    VrWatchEnd("a session runs", g_vr_start);
  } else if (g_vr_gave_up) {
    VrWatchEnd("no session", g_vr_start);
  }
}

// A log sink that throws the line away, for the quiet retries.
void ModLogNowhere(const char*, ...) {}

// A TEST AID for a frame with no eye pass with backend=vulkan (a menu, a
// load): one line for each such frame after the first 600 frames, at most
// 400 lines. It gives the time from the frame of this kind before, the
// time of the submit, and what the walks since then did.
void FlatFrameLine(uint64_t image, LARGE_INTEGER t0, LARGE_INTEGER t1) {
  static int s_lines = 0;
  static LARGE_INTEGER s_last = {};
  static uint64_t s_count[5] = {};
  static uint64_t s_image = 0;
  uint64_t now[5] = {};
  VkSceneCounts(&now[0], &now[1], &now[2], &now[3], &now[4]);
  uint64_t delta[5] = {};
  for (int i = 0; i < 5; ++i) {
    // The report of 450 frames sets the counts back to 0.
    delta[i] = now[i] >= s_count[i] ? now[i] - s_count[i] : now[i];
    s_count[i] = now[i];
  }
  LARGE_INTEGER hz;
  QueryPerformanceFrequency(&hz);
  const double ms_hz = 1000.0 / static_cast<double>(hz.QuadPart);
  const double gap =
      s_last.QuadPart != 0 ? (t0.QuadPart - s_last.QuadPart) * ms_hz : 0.0;
  s_last = t0;
  const bool same = image == s_image;
  s_image = image;
  if (g_frame < 600 || s_lines >= 400) return;
  ++s_lines;
  ModLog("vr flat      : frame %llu, %.1f ms after the flat frame before, the "
         "submit %.1f ms. Since then %llu walks, %llu draws, %llu clears, "
         "%llu walks with the picture before, %llu walks with a clear of the "
         "whole target. The image is %s",
         g_frame, gap, (t1.QuadPart - t0.QuadPart) * ms_hz, delta[0],
         delta[1], delta[2], delta[3], delta[4],
         same ? "the same as before" : "the other target");
}

void __cdecl h_FinishFrame() {
  // After the shutdown the game draws for itself.
  if (g_shut_down) {
    o_FinishFrame();
    return;
  }
  g_frame_thread = GetCurrentThreadId();
  // Only reads, and only with the table. See tabletop.cpp.
  TabletopCountLists();
  // The bars of a story scene are hidden for the draw only. The game's own
  // widescreen state and its timing do not change. See draw_hooks.cpp.
  const float bars = UiBarsBegin();
  // The interface panel of a FinishFrame outside the eye passes. Inside them
  // the stereo loop latched the route already.
  const bool in_pass = StereoInPass();
  UiPanelBeforeFinish(in_pass);
  o_FinishFrame();
  UiBarsEnd(bars);
  ++g_frame;
  ExitTestFrame(g_frame);
  CrashReportFrame(g_frame);
  if (g_cursor_trace) CursorTraceFrame(g_frame);
  // The rules for the switch (docs/ui-layer.md, "Rules for the switch"):
  // the game's target is back before anything else runs.
  UiPanelAfterFinish(in_pass, g_frame);
  // The ears and the advisors. It latches itself to one run per frame.
  AudioFrame();

  // Shutdown never runs when the game is force killed, and it usually is
  // during a test. So the accounting report goes out while the game runs.
  if (g_ui_accounting && (g_frame % 1800) == 0) UiDepthReport();
  if (g_filter_survey && (g_frame % 1800) == 0) TextureFilterReport();
  if ((g_frame % 1800) == 0) GdiWatchReport();

  if (!g_vr_enabled && !g_stereo_headless) {
    vk::WaitForExternal(false);
    return;
  }
  StartVrOnce();
  // The backend can replace its device while the game runs, for example when
  // the game goes full screen. This starts the GPU path again on the new one.
  GpuBridgeResume();
  // Measured at 1920x1080: the copy onto the primary surface costs 7.90 ms of
  // a 19 ms frame. With the headset on, nobody sees it.
  //
  // This is decided every frame, and it must be decided BEFORE the return
  // below. If the headset sleeps or the runtime goes away, the monitor is the
  // only output there is. Leaving the skip latched on would freeze the window
  // and make the game look hung with no way back.
  PresentWatchSkipDesktop(g_skip_desktop && g_screen != nullptr &&
                          g_screen->running());
  // backend=vulkan: the first frame that the session with Vulkan presented
  // deletes the file of the fall back (vk_fallback.h).
  if (VkFallbackWaiting() && g_screen != nullptr && g_screen->vulkan() &&
      vk::Started() && g_screen->presented() > 0) {
    VkFallbackPicture("the headset");
  }

  // The watch of the frame thread counts each frame, also of the logos
  // (hang_watch, a test key).
  HangWatchFrame();

  // The logos at the start of the game end on a press of a controller
  // button. The caller of FinishFrame says which logo plays.
  IntroSkipFrame(reinterpret_cast<uintptr_t>(_ReturnAddress()), g_screen);

  // The intro draws its own eye passes and sends them itself. Its
  // FinishFrame must not also go out as a flat picture.
  if (IntroFinishFrame()) return;

  if (g_screen == nullptr || !g_screen->running()) return;

  // Process3dEngine runs the whole scene twice for stereo, and this hook is
  // inside it, so the mono path has to stand aside while that happens.
  if (StereoInPass()) return;
  // Once stereo is driving, it owns the frame. This path takes over again
  // whenever stereo goes quiet, which is what happens in menus, loading
  // screens and videos: they never reach Process3dEngine, so without this the
  // headset would hold the last scene frame and show nothing else.
  if (g_stereo && g_screen->eyes() > 1 && !StereoIdle()) return;

  // The same GPU path the stereo passes use. Without it a menu at 2560x1440
  // costs 18.98 ms a frame just to read the picture back, and the readback is
  // worse on this backend than on system DirectDraw, because the render
  // target lives in graphics memory.
  // backend=vulkan: the picture of the last walk goes to the session with a
  // copy of the GPU (the logos and the loading screens are walks too). A
  // picture that the game wrote with the CPU keeps the path of the CPU.
  {
    uint64_t image = 0;
    uint32_t layer = 0, width = 0, height = 0;
    if (g_screen->vulkan() &&
        VkSceneLastPicture(&image, &layer, &width, &height) &&
        width >= g_screen->width() && height >= g_screen->height()) {
      LARGE_INTEGER t0, t1;
      QueryPerformanceCounter(&t0);
      if (!g_screen->SubmitVulkan(image, layer)) {
        ModLog("vr           : the session ended at frame %llu", g_frame);
      }
      QueryPerformanceCounter(&t1);
      FlatFrameLine(image, t0, t1);
      return;
    }
  }
  if (GpuBridgeReady()) {
    void* texture = GpuBridgeCaptureEye(0);
    if (texture != nullptr) {
      if (!g_screen->SubmitTexture(texture)) {
        ModLog("vr           : the session ended at frame %llu", g_frame);
      }
      if ((g_frame % 900) == 0) {
        ModLog("frame %llu: %llu presented, %llu skipped", g_frame,
               g_screen->presented(), g_screen->skipped());
      }
      return;
    }
  }

  // backend=vulkan: the pixels of the CPU are the picture only where the
  // game wrote them. The first frames of this kind go into the log.
  if (VkSceneDirect()) {
    static int s_said = 0;
    if (s_said < 24) {
      ++s_said;
      ModLog("vr           : frame %llu sends the pixels of the CPU of the "
             "render target, with no eye pass and no picture of a walk after "
             "the last write of the CPU",
             g_frame);
    }
  }
  int pitch = 0;
  const void* pixels = FrameSourceGrab(&pitch);
  if (pixels == nullptr) return;

  if (!g_screen->SubmitBgra(pixels, pitch)) {
    ModLog("vr           : the session ended at frame %llu", g_frame);
  }

  if ((g_frame % 900) == 0) {
    ModLog("frame %llu: %llu presented, %llu skipped", g_frame,
           g_screen->presented(), g_screen->skipped());
    FrameSourceReport();
  }
}

// THE GAME BUILD.
//
// Every address in bw_addresses.h belongs to one file: the runblack.exe of
// the unofficial v1.42 patch. The image base is 0x00400000 in every build of
// the game, so it cannot tell them apart. A 1.10 or 1.30 file with the copy
// protection removed would take the hooks into unrelated code, which crashes
// or, worse, writes rubbish into a saved game.
//
// So the file on the disk is hashed, which costs about 20 ms once per start.
// On any other build the mod installs nothing at all, loads system
// DirectDraw, and the game runs as it did before the mod arrived.
bool CheckGameBuild() {
  LARGE_INTEGER hz, start, end;
  QueryPerformanceFrequency(&hz);
  QueryPerformanceCounter(&start);
  wchar_t path[MAX_PATH];
  if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
    ModLog("game         : cannot find the path of the game (error %lu). The "
           "mod is off.", GetLastError());
    return false;
  }
  const bwvr::ExeReport report = bwvr::CheckGameExe(path);
  QueryPerformanceCounter(&end);
  const double ms =
      hz.QuadPart > 0
          ? (end.QuadPart - start.QuadPart) * 1000.0 / hz.QuadPart
          : 0.0;

  if (report.verdict == bwvr::ExeVerdict::kSupported) {
    ModLog("game         : runblack.exe is the v1.42 build, checked in %.0f ms",
           ms);
    return true;
  }
  if (report.verdict == bwvr::ExeVerdict::kUnreadable) {
    ModLog("game         : runblack.exe could not be read (error %lu)",
           report.error);
  } else {
    ModLog("game         : this runblack.exe is %s, %llu bytes",
           report.build != nullptr ? report.build : "a build the mod does not "
                                                    "know",
           report.size);
    ModLog("game         : sha1 %s", report.sha1);
    ModLog("game         : the mod fits the unofficial v1.42 patch only, "
           "sha1 %s", bwvr::kGameExeSha1);
  }
  ModLog("game         : no hooks are installed, and the game runs as it did "
         "before.");
  return false;
}

bool VerifyTarget() {
  const uintptr_t base =
      reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
  if (base != bw::kImageBase) {
    ModLog("REFUSING TO HOOK: image base is 0x%08X, expected 0x%08X.", base,
           bw::kImageBase);
    return false;
  }
  return true;
}

// The number of old logs that stay beside the game.
const int kOldLogs = 20;

// Keeps the log of the run before this one. Each run starts a new bwvr.log,
// and a report of a problem often comes after the player started the game
// again. The old log gets the time of its last write in its name, for example
// bwvr-old-20261003-185400.log, and only the newest kOldLogs stay. name_out
// gets the new name. False when there was no old log to keep.
bool KeepOldLog(const char* log_path, char* name_out, size_t name_size) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExA(log_path, GetFileExInfoStandard, &data)) {
    return false;
  }
  if (data.nFileSizeLow == 0 && data.nFileSizeHigh == 0) return false;
  FILETIME local;
  SYSTEMTIME t;
  if (!FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) ||
      !FileTimeToSystemTime(&local, &t)) {
    return false;
  }
  _snprintf_s(name_out, name_size, _TRUNCATE,
              "bwvr-old-%04d%02d%02d-%02d%02d%02d.log", t.wYear, t.wMonth,
              t.wDay, t.wHour, t.wMinute, t.wSecond);
  char path[MAX_PATH];
  _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s", g_dir, name_out);
  if (!MoveFileExA(log_path, path, MOVEFILE_REPLACE_EXISTING)) return false;

  // The names sort by time, so the smallest name is the oldest log.
  char pattern[MAX_PATH];
  _snprintf_s(pattern, MAX_PATH, _TRUNCATE, "%sbwvr-old-*.log", g_dir);
  for (;;) {
    WIN32_FIND_DATAA found;
    const HANDLE find = FindFirstFileA(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) break;
    int count = 0;
    char oldest[MAX_PATH] = "";
    do {
      if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
      ++count;
      if (oldest[0] == '\0' || strcmp(found.cFileName, oldest) < 0) {
        lstrcpynA(oldest, found.cFileName, MAX_PATH);
      }
    } while (FindNextFileA(find, &found));
    FindClose(find);
    if (count <= kOldLogs) break;
    _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s", g_dir, oldest);
    if (!DeleteFileA(path)) break;
  }
  return true;
}

// Opens the log and works out where the game lives. This has to run before
// the backend loads, so the backend's own first moments can be logged.
void OpenLog() {
  InitializeCriticalSection(&g_lock);

  ZeroMemory(g_dir, sizeof(g_dir));
  GetModuleFileNameA(GetModuleHandleA(nullptr), g_dir, MAX_PATH);
  char* slash = strrchr(g_dir, '\\');
  if (slash != nullptr) *(slash + 1) = '\0';

  char log_path[MAX_PATH];
  lstrcpynA(log_path, g_dir, MAX_PATH);
  lstrcatA(log_path, "bwvr.log");
  char old_name[64] = "";
  const bool kept = KeepOldLog(log_path, old_name, sizeof(old_name));
  // Shared read, so the log can be followed while the game runs.
  g_log = _fsopen(log_path, "w", _SH_DENYWR);

  SYSTEMTIME now;
  GetLocalTime(&now);
  ModLog("%s %s (build %s)", BWVR_PRODUCT_NAME, BWVR_VERSION_TEXT,
         BWVR_BUILD_ID);
  ModLog("%04d-%02d-%02d %02d:%02d:%02d", now.wYear, now.wMonth, now.wDay,
         now.wHour, now.wMinute, now.wSecond);
  if (kept) {
    ModLog("log          : the log of the run before this one is now %s. The "
           "newest %d old logs stay.", old_name, kOldLogs);
  }
}

// The hooks that must be in place before the graphics backend loads.
//
// The bridge watches for the backend creating its Direct3D 9 device. A
// backend creates that device the first time the game asks for DirectDraw,
// which is after this DLL is loaded but not by much, so the watch goes in
// first and waits.
void EarlyHooks() {
  char path[MAX_PATH];
  lstrcpynA(path, g_dir, MAX_PATH);
  lstrcatA(path, "bwvr.ini");
  // The report of a fault goes in first, so it also covers the start.
  CrashReportInstall(path);
  // The game's check for an internet connection can wait forever when a
  // service of Windows does not answer. The mod answers it by default. It
  // goes in here, before each other thing, because it must work with vr
  // turned off too.
  g_net_check_own = GetPrivateProfileIntA("bwvr", "net_check", 0, path) == 0;
  NetCheckInstall(g_net_check_own);
  // The check of the game for DirectX 7 stops the game with no administrator
  // rights. It comes before the backend loads.
  g_dx_check_own = GetPrivateProfileIntA("bwvr", "dx_check", 0, path) == 0;
  DxCheckInstall(g_dx_check_own);
  // Before the game opens its first file with LHFile. It works with vr
  // turned off too.
  FileReadAheadInstall(
      GetPrivateProfileIntA("bwvr", "file_read_ahead", 1, path) != 0,
      GetPrivateProfileIntA("bwvr", "test_keys", 0, path) != 0);
  // On by default from version 1.1.0. Read here because the device hooks of
  // the bridge need it.
  Depth24SetWanted(GetPrivateProfileIntA("bwvr", "depth24", 1, path) != 0);
  // backend=vulkan: the line of a fall back to dxwrapper, or the file that
  // the first picture deletes (vk_fallback.h).
  VkFallbackStart(g_dir, path);
  // backend=vulkan with vulkan_only=1: no DXVK, no hook on Direct3D 9 and no
  // load of d3d9.dll.
  g_vulkan_only = ProxyBackendVulkan() &&
                  GetPrivateProfileIntA("bwvr", "vulkan_only", 1, path) != 0;
  if (ProxyBackendVulkan()) {
    ModLog("vulkan only  : vulkan_only=%d. %s", g_vulkan_only ? 1 : 0,
           g_vulkan_only
               ? "With backend=vulkan the mod uses only Vulkan: no "
                 "dxwrapper, no Direct3D 9, no DXVK and no Direct3D 11. The "
                 "parts for Direct3D 9 do not start"
               : "The parts for Direct3D 9 and the Direct3D 11 device of the "
                 "session start as with dxwrapper, with no Direct3D 9 to "
                 "use");
  }
  if (g_vulkan_only) return;
  // Before anything loads d3d9.dll: DXVK in place of the Direct3D 9 of
  // Windows, when it is wanted and the PC can run it.
  D3D9BackendLoad(g_dir, path);
  // On by default: it is the tested path, and it falls back to the readback
  // on its own if the device cannot share a texture.
  const bool wanted =
      GetPrivateProfileIntA("bwvr", "gpu_bridge", 1, path) != 0;
  if (!wanted) return;
  // With vr=0 the mod does nothing, so the bridge stays out of the backend. A
  // headless run has vr=0 too, and it needs the bridge.
  if (GetPrivateProfileIntA("bwvr", "vr", 1, path) == 0 &&
      GetPrivateProfileIntA("bwvr", "stereo_headless", 0, path) == 0) {
    return;
  }

  const MH_STATUS status = MH_Initialize();
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
    ModLog("gpu bridge   : MinHook would not start, staying on the CPU path");
    return;
  }

  // With frame_send=2 a second thread makes calls on the device of the
  // backend, and the render thread is a second thread too. A test key can
  // choose each of them in a session.
  GpuBridgeWantTwoThreads(
      GetPrivateProfileIntA("bwvr", "frame_send", 0, path) == 2 ||
      GetPrivateProfileIntA("bwvr", "render_thread", 0, path) != 0 ||
      GetPrivateProfileIntA("bwvr", "test_keys", 0, path) != 0);
  GpuBridgeSetEnabled(true);
  GpuBridgeInstall();
}

void ExitPathInstall();

void Startup() {
  ModLog("%s", ProxyBackendNote());
  ModLog("");
  // A second try, if LHMultiplayerR.dll was not loaded at the first one.
  NetCheckInstall(g_net_check_own);
  DxCheckInstall(g_dx_check_own);

  ReadSettings();
  // Headless measuring still needs the hooks, even with vr turned off.
  if (!g_vr_enabled && !g_stereo_headless) {
    ModLog("vr           : turned off in bwvr.ini, the game runs normally");
    return;
  }
  if (!VerifyTarget()) return;

  const MH_STATUS started = MH_Initialize();
  if (started != MH_OK && started != MH_ERROR_ALREADY_INITIALIZED) {
    ModLog("hooks        : MinHook would not start, VR disabled");
    g_vr_enabled = false;
    return;
  }
  // Before the game makes its window, so the log has each call from the start.
  if (g_cursor_trace) CursorTraceInstall();
  // Before the game reads its profile, so the guard has each write.
  if (g_registry_test_guard) ExitTestGuardFromStart();

  void* target = reinterpret_cast<void*>(bw::render::kFinishFrame);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_FinishFrame),
                    reinterpret_cast<void**>(&o_FinishFrame));
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : LH3DRender::FinishFrame at 0x%08X %s",
         bw::render::kFinishFrame, status == MH_OK ? "ok" : "FAILED");
  if (status != MH_OK) g_vr_enabled = false;
  ExitPathInstall();
  Depth24Install();
}

// THE END OF THE MOD.
//
// When a process ends, Windows first stops each thread but the one that
// ends it, and then tells each DLL in an order that the mod does not
// control. A call into OpenXR or Direct3D from DllMain then goes to a
// runtime whose threads are stopped and to objects whose device is gone.
//
// The game also ends its own Direct3D before that, in exit: the exit list
// of the C runtime holds the destructor of LHScreen (0x007DD850), which
// calls IDirectDraw7::RestoreDisplayMode, and the backend destroys its
// Direct3D 9 device there, with each object that the bridge made on it. If
// the shared textures are open on a Direct3D 11 device at that time, the
// process stops with an access violation.
//
// So the mod shuts down on the exit path of the game, before the game frees
// anything: each thread runs, each DLL is loaded, and the device of the
// backend lives. free_system is the last call of pc_main (0x00641C60) when
// the game ends in its usual way. exit is where WinMainCRTStartup
// (0x007CA49F) takes the result of WinMain, and where the game goes when it
// stops for an error. The first of the two that runs does the shutdown, one
// time. See docs/backend.md, "The end of the mod".
const uintptr_t kFreeSystem = 0x00643420;  // void free_system()
const uintptr_t kExit = 0x007C9EE1;        // void exit(int), of the C runtime

typedef void(__cdecl* FreeSystemFn)();
typedef void(__cdecl* ExitFn)(int);
FreeSystemFn o_free_system = nullptr;
ExitFn o_exit = nullptr;
// One of the two hooks is there.
bool g_exit_hooked = false;

double MsSince(const LARGE_INTEGER& start) {
  LARGE_INTEGER hz, now;
  QueryPerformanceFrequency(&hz);
  QueryPerformanceCounter(&now);
  return hz.QuadPart > 0
             ? (now.QuadPart - start.QuadPart) * 1000.0 / hz.QuadPart
             : 0.0;
}

// One part of the shutdown. Its line comes after it, with its time, so the
// last line of a log that stops here names the part before the one that
// did not come back.
template <typename Part>
void ShutdownPart(const char* name, Part part) {
  LARGE_INTEGER start;
  QueryPerformanceCounter(&start);
  part();
  ModLog("shutdown     : %s, %.1f ms", name, MsSince(start));
}

// `path` says who called. The hooks stay: on the exit path the threads of
// the game run, and one of them can be in a hook.
void Shutdown(const char* path) {
  if (g_shut_down) return;
  g_shut_down = true;
  LARGE_INTEGER start;
  QueryPerformanceCounter(&start);
  ModLog("");
  ModLog("shutdown     : the shutdown starts, %s, frame %llu", path, g_frame);
  LogDirect3DModules("the shutdown starts");
  // Let the cursor go before anything else. A clipped cursor outlives the
  // process that clipped it, so a crash here would leave the pointer trapped
  // in a rectangle on a dead window.
  ShutdownPart("the cursor, the camera values and the detail distance", [] {
    PointerMapRelease();
    ComfortCameraRelease();
    SkyDetailRelease();
  });
  if (g_screen != nullptr) {
    ModLog("final: %llu frames, %llu presented, %llu skipped", g_frame,
           g_screen->presented(), g_screen->skipped());
    FrameSourceReport();
    StereoReport();
    UiDepthReport();
    TextureFilterReport();
    ShutdownPart(g_vulkan_only ? "the OpenXR session"
                               : "the OpenXR session and its Direct3D 11 device",
                 [] {
      // Each file that holds the screen lets it go first: the game can call
      // a hook after this.
      StereoSetScreen(nullptr);
      FrameSendSetScreen(nullptr);
      IntroSetScreen(nullptr);
      ControllersSetScreen(nullptr);
      bwvr::XrScreen* screen = g_screen;
      g_screen = nullptr;
      screen->Stop();
      delete screen;
    });
  }
  // After the session: the thread of the tries made its instance, and it
  // stays while that instance lives. When Windows ends the process, each
  // other thread is stopped already, maybe with the lock of vr_start.cpp
  // held, so then the shutdown does not touch that thread.
  if (g_vr_thread && !g_process_ends) {
    ShutdownPart("the thread of the tries to start the headset session",
                 [] { VrStartShutdown(500); });
  }
  if (!g_vulkan_only) {
    ShutdownPart("the GPU bridge", [] { GpuBridgeShutdown(); });
  }
  ShutdownPart("the frame source", [] { FrameSourceShutdown(); });
  ModLog("shutdown     : all parts are done, %.1f ms", MsSince(start));
}

void __cdecl h_free_system() {
  // The game loop ended as it does for Quit. A fault comes to exit or to
  // DllMain, not here.
  VkFallbackEnd();
  Shutdown("the game frees its system (free_system)");
  o_free_system();
}

// The game calls exit from 27 places, and most of them stop the game for an
// error. Only the thread that draws the frames can shut the mod down: on a
// different thread a frame can be in the middle of a call into OpenXR.
void __cdecl h_exit(int code) {
  if (g_frame_thread == 0 || GetCurrentThreadId() == g_frame_thread) {
    char path[64];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "the game calls exit(%d)",
                code);
    Shutdown(path);
  } else if (!g_shut_down) {
    ModLog("shutdown     : the game calls exit(%d) on a thread that does not "
           "draw the frames, so the shutdown does not run", code);
  }
  o_exit(code);
}

// The two hooks of the exit path. With exit_shutdown=0 they are not there.
void ExitPathInstall() {
  if (!g_exit_shutdown) {
    ModLog("shutdown     : exit_shutdown=0, the mod shuts down when Windows "
           "unloads the DLL");
    return;
  }
  void* free_system = reinterpret_cast<void*>(kFreeSystem);
  void* exit_fn = reinterpret_cast<void*>(kExit);
  const bool free_ok =
      MH_CreateHook(free_system, reinterpret_cast<void*>(&h_free_system),
                    reinterpret_cast<void**>(&o_free_system)) == MH_OK &&
      MH_EnableHook(free_system) == MH_OK;
  const bool exit_ok =
      MH_CreateHook(exit_fn, reinterpret_cast<void*>(&h_exit),
                    reinterpret_cast<void**>(&o_exit)) == MH_OK &&
      MH_EnableHook(exit_fn) == MH_OK;
  g_exit_hooked = free_ok || exit_ok;
  ModLog("hooks        : the exit path of the game, free_system at 0x%08X %s "
         "and exit at 0x%08X %s (exit_shutdown=1)",
         kFreeSystem, free_ok ? "ok" : "FAILED", kExit,
         exit_ok ? "ok" : "FAILED");
}

// From DllMain, when Windows unloads the DLL. `process_ends` is true when
// the process ends, which is the usual case: the game never unloads the DLL.
void EndOfDll(bool process_ends) {
  g_process_ends = process_ends;
  if (!g_exit_shutdown || !process_ends) {
    Shutdown(process_ends
                 ? "Windows unloads the DLL at the end of the process"
                 : "the DLL is unloaded and the process continues");
    ShutdownPart("the hooks", [] { MH_Uninitialize(); });
  } else {
    // No OpenXR, no Direct3D and no MinHook here. The cursor is free again
    // in each case: that call is safe, and it does nothing a second time.
    PointerMapRelease();
    if (!g_shut_down && g_exit_hooked) {
      ModLog("");
      ModLog("shutdown     : the process ends and the shutdown did not run "
             "on the exit path of the game. The mod frees the cursor and "
             "closes the log");
    }
  }
  ModLog("shutdown     : the mod is shut down");
  if (g_log != nullptr) {
    fclose(g_log);
    g_log = nullptr;
  }
}

}  // namespace

// The test key of xr_vulkan_upload (stereo.cpp, Shift Z). It changes the
// key for the next session too.
void XrVulkanUploadToggle(const char* keys) {
  g_xr_vulkan_upload = g_xr_vulkan_upload != 0 ? 0 : 1;
  if (g_screen != nullptr) g_screen->SetVulkanUpload(g_xr_vulkan_upload != 0);
  ControllersGesturePulse(0.5f, g_xr_vulkan_upload != 0 ? 0.30f : 0.08f);
  ModLog("test key     : %s, xr_vulkan_upload is %d now: the pixels of the "
         "CPU go %s from the next write",
         keys, g_xr_vulkan_upload,
         g_xr_vulkan_upload != 0 ? "into the swapchains with Vulkan"
                                 : "through the relays of Direct3D 11");
}

// backend=vulkan with vulkan_only=1 (docs/backend.md, "Only Vulkan").
bool VulkanOnly() { return g_vulkan_only; }

// A TEST AID for vulkan_only: which DLLs of Direct3D and of dxwrapper are in
// the process. The OpenXR runtime and the driver can load some of them
// themselves.
void LogDirect3DModules(const char* when) {
  static const char* const kNames[] = {"dxwrapper.dll", "d3d9.dll",
                                       "d3d11.dll", "dxgi.dll", "d3d12.dll"};
  char loaded[160] = "";
  char absent[160] = "";
  for (const char* name : kNames) {
    char* list = GetModuleHandleA(name) != nullptr ? loaded : absent;
    if (list[0] != 0) strcat_s(list, 160, ", ");
    strcat_s(list, 160, name);
  }
  ModLog("modules      : %s. In the process: %s. Not in the process: %s", when,
         loaded[0] != 0 ? loaded : "none", absent[0] != 0 ? absent : "none");
}

// For tabletop.cpp: the predicted display time of the frame of the headset
// that is open, in seconds, or 0 with no session.
double ModDisplaySeconds() {
  if (g_screen == nullptr || !g_screen->running()) return 0.0;
  return g_screen->DisplaySeconds();
}

// The test key of xr_hold_ms (stereo.cpp, Shift W): the hold goes off, and
// on again with the value of the ini (30 ms when the ini has 0).
void XrHoldToggle(const char* keys) {
  if (g_screen == nullptr) return;
  const int now =
      g_screen->HoldMs() > 0 ? 0 : (g_xr_hold_ms > 0 ? g_xr_hold_ms : 30);
  g_screen->SetHoldMs(now);
  ControllersGesturePulse(0.5f, now > 0 ? 0.30f : 0.08f);
  ModLog("test key     : %s, xr_hold_ms is %d now", keys, now);
}

void ModLog(const char* format, ...) {
  if (g_log == nullptr) return;
  // The frame cost measure counts the time of a line, with the wait for the
  // lock.
  const uint64_t profile_start = FrameProfileOn() ? FrameProfileTicks() : 0;
  // At the end of the process a stopped thread can hold the lock for ever.
  // No other thread runs then, so the line goes out without the lock.
  bool locked = true;
  if (g_process_ends) {
    locked = TryEnterCriticalSection(&g_lock) != 0;
  } else {
    EnterCriticalSection(&g_lock);
  }
  va_list args;
  va_start(args, format);
  vfprintf(g_log, format, args);
  va_end(args);
  fputc('\n', g_log);
  fflush(g_log);
  if (locked) LeaveCriticalSection(&g_lock);
  if (profile_start != 0) {
    FrameProfileAdd(kFpLog, FrameProfileTicks() - profile_start);
    FrameProfileCount(kFpcLogLines, 1);
  }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    OpenLog();
    // Before any hook goes in, and before a backend loads.
    const bool game_ok = CheckGameBuild();
    if (game_ok) {
      EarlyHooks();
    } else {
      ProxyForceSystemDDraw();
    }
    // The first bytes of the two exports that make a DirectDraw object,
    // before the backend can put its hot patch on them (ProxyCreateStarts).
    unsigned char start_ex[2], start_old[2];
    ProxyCreateStarts(start_ex, start_old);
    if (!ProxyLoadRealDDraw()) return FALSE;
    // A test key: the count of each call of DirectDraw and Direct3D 7. After
    // the backend loads, and before the game makes its DirectDraw object.
    if (game_ok) {
      char ini[MAX_PATH];
      lstrcpynA(ini, g_dir, MAX_PATH);
      lstrcatA(ini, "bwvr.ini");
      // backend=vulkan: the DirectDraw of the mod. The count needs the
      // objects of another backend, so it does not run then.
      const bool vulkan = ProxyBackendVulkan();
      if (vulkan) {
        VkDdrawInstall(static_cast<int>(
            GetPrivateProfileIntA("bwvr", "vk_lines", 1, ini)));
        // Before the first present of the game.
        VkWindowSetOptions(static_cast<int>(
            GetPrivateProfileIntA("bwvr", "vk_window", 1, ini)));
        // With a headset the renderer draws with the Vulkan device of the
        // session, so its images can go into the swapchains. It waits for
        // the session from the first walk (StartVrOnce lets it go when no
        // session comes).
        if (GetPrivateProfileIntA("bwvr", "vr", 1, ini) != 0 &&
            GetPrivateProfileIntA("bwvr", "stereo_headless", 0, ini) == 0) {
          vk::WaitForExternal(true);
        }
      }
      const int census =
          vulkan ? 0
                 : static_cast<int>(
                       GetPrivateProfileIntA("bwvr", "ddraw_census", 0, ini));
      DdrawCensusInstall(census);
      // dxwrapper puts a hot patch on the two exports of the mod that make a
      // DirectDraw object, so the count sees the calls of the start only
      // with another backend (backend=system).
      if (census != 0) {
        unsigned char now_ex[2], now_old[2];
        ProxyCreateStarts(now_ex, now_old);
        const bool patched = now_ex[0] != start_ex[0] ||
                             now_ex[1] != start_ex[1] ||
                             now_old[0] != start_old[0] ||
                             now_old[1] != start_old[1];
        ModLog("ddraw census : %s",
               patched ? "the backend put a hot patch on DirectDrawCreateEx "
                         "and DirectDrawCreate of the mod, so the count starts "
                         "at the first walk. backend=system counts the start"
                       : "the exports of the mod have no hot patch of the "
                         "backend, so the count starts with the first "
                         "DirectDraw object");
      }
    }
    if (game_ok) Startup();
  } else if (reason == DLL_PROCESS_DETACH) {
    // The third parameter is not null when the process ends.
    EndOfDll(reserved != nullptr);
  }
  return TRUE;
}
