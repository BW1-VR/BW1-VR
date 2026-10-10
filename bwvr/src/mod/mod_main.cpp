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

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "../common/exe_check.h"
#include "../common/version.h"
#include "build_id.h"
#include "../vr/xr_screen.h"
#include "controllers.h"
#include "tabletop.h"
#include "far_grab.h"
#include "throw_physics.h"
#include "teleport.h"
#include "ui_panel.h"

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
void DrawProbeArm(bool on, int frames);
void DrawProbeDelay(int frames);
void StereoSetHeadless(bool on);
void StereoSetProfile(bool on);
void StereoSetTestKeys(bool on);
void StereoSetCaptureInterval(int seconds);
void StereoSetHeadlessGrab(bool on);
void StereoSetUiDepth(bool adaptive, float ceiling_pixels);
void StereoSetScreen(bwvr::XrScreen* screen);
// intro_vr.cpp: the Lionhead intro in stereo.
void IntroSetOptions(bool on, bool still, int distance_cm, bool controller,
                     bool probe);
void IntroSetScreen(bwvr::XrScreen* screen);
void IntroInstall();
bool IntroFinishFrame();
// intro_skip.cpp: a press of a controller button skips the logos at the
// start of the game. `caller` is the address that called FinishFrame.
void IntroSkipFrame(uintptr_t caller, bwvr::XrScreen* screen);
// controllers.cpp
void ControllerHintsSetOptions(bool on, const char* forced_profile);
bool ControllerHintsInstall();
void ControllersSetOptions(bool on, bool left_handed, bool haptics,
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
void PointerMapSetOptions(bool scale, bool clip);
void PointerMapSetRenderSize(int width, int height);
void PointerMapSetWindowSize(int width, int height);
void PointerMapRelease();
void PointerMapSetRebuild(bool on);
// comfort_camera.cpp
void ComfortCameraSetOptions(int snap_degrees, bool instant_jumps, bool glide,
                             int blink_ms);
void ComfortCameraSetZoomStep(int step);
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
void PointerMapSetDragSmoothing(int ms);
void PointerMapSetFakeMouse(const char* script, int step_px,
                            const int x_pct[2], const int y_pct[2]);
void UiDepthSetCinemaBars(bool show);
float UiBarsBegin();
void UiBarsEnd(float saved);
// spirit_anchor.cpp
void SpiritAnchorInstall();
void AudioSetRoom(bool room, int units_per_meter, bool places);
void AudioSetRange(bool by_ears, bool keep_loops, int test_tag,
                   int test_tag_bank);
void AudioInstall(bool head, bool advisors, bool trace);
void AudioFrame();
void SpiritAnchorSetEnabled(bool on);
void DetailDistanceSet(float factor);
void ComfortCameraRelease();
void PointerMapInstall();
void HeadCameraSetHandAnchored(bool on);
void HeadCameraSetEnabled(bool on);
void StereoSetHeadTracking(bool on);
void StereoSetHeadFov(float degrees);
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
// near_plane.cpp: the near plane of the game camera.
void NearPlaneSetOptions(int cm_24, int cm_16);
void NearPlaneInstall();
// proxy/ddraw_proxy.cpp
void ProxyForceSystemDDraw();
void GpuBridgeInstall();
void GpuBridgeProbeInterop();
bool GpuBridgeHasDevice();
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
bool g_move_camera_point = false;

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
bool g_profile = false;
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
int g_window_w = 0;
int g_window_h = 0;
bool g_hand_anchored = true;
// Moving around. The launcher stores the two choices as an index.
int g_snap_turn_deg = 45;
bool g_instant_jumps = true;
bool g_camera_glide = false;
int g_blink_ms = 0;
int g_zoom_step = 2;  // 0 the game's own, 1 small, 2 medium, 3 large
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
// The black bars of the story scenes. Off by default in a headset.
bool g_cinema_bars = false;
// The advisors stay where the player looked when they came.
bool g_spirit_anchor = true;
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
bool g_controller_left_handed = false;
bool g_controller_swap_buttons = false;
bool g_controller_hints = true;
bool g_hint_selftest = false;
char g_hint_profile[64] = {0};
bool g_controller_haptics = true;
FakeControllerSettings g_fake_controller;
DirectHandSettings g_direct_hand;
// A glow marks the point the Hand interacts from (hand_glow.cpp).
bool g_hand_glow = true;
// The mod draws its own test hand at each controller (overlay.cpp).
bool g_overlay = false;
// The teleport with a visible arc (teleport.cpp). See docs/god-view.md.
TeleportSettings g_teleport;
// The grab from a distance (far_grab.cpp), and the square root law of the
// throw. See docs/grab.md, "The grab from a distance".
FarGrabSettings g_far_grab;
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
bool g_intro_still = true;
int g_intro_distance_cm = 300;
bool g_intro_controller = true;
bool g_intro_probe = false;

// The camera is a family of matrices, not one matrix. Each of these
// keeps one more member of that family in step with the eye. They are
// switches so the draw probe can measure one at a time. See
// eye_camera.cpp for what each one is and why it matters.
bool g_fix_unscaled = true;
bool g_fix_copy = true;
bool g_fix_inverse = true;
bool g_fix_random = false;  // measured: it makes things worse, see eye_camera.cpp

bwvr::XrScreen* g_screen = nullptr;
bool g_hooks_installed = false;
bool g_vr_gave_up = false;
int g_vr_tries = 0;
DWORD g_next_vr_try = 0;

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
  g_draw_probe = GetPrivateProfileIntA("bwvr", "draw_probe", 0, path) != 0;
  g_ui_accounting =
      GetPrivateProfileIntA("bwvr", "ui_accounting", 0, path) != 0;
  g_ui_adaptive = GetPrivateProfileIntA("bwvr", "ui_adaptive", 1, path) != 0;
  g_ui_shift_max = static_cast<float>(
      GetPrivateProfileIntA("bwvr", "ui_shift_max_px", 24, path));
  g_anisotropy = GetPrivateProfileIntA("bwvr", "anisotropy", 16, path);
  g_filter_survey =
      GetPrivateProfileIntA("bwvr", "filter_survey", 0, path) != 0;
  g_profile = GetPrivateProfileIntA("bwvr", "profile", 0, path) != 0;
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
  }
  // The camera the story moves: 0 as the game does it, 1 a cut at the start
  // of each move, 2 a cut at the end of each move.
  {
    const int story =
        static_cast<int>(GetPrivateProfileIntA("bwvr", "script_camera", 2, path));
    g_script_camera = (story >= 0 && story < 3) ? story : 2;
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
  g_cinema_bars = GetPrivateProfileIntA("bwvr", "cinema_bars", 0, path) != 0;
  g_spirit_anchor =
      GetPrivateProfileIntA("bwvr", "spirit_anchor", 1, path) != 0;
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
  // Not in the launcher. Off only to test the fault it fixes.
  g_game_camera_rebuild =
      GetPrivateProfileIntA("bwvr", "game_camera_rebuild", 1, path) != 0;
  g_head_fov_deg = static_cast<float>(
      GetPrivateProfileIntA("bwvr", "head_fov_deg", 0, path));
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
  g_controller_left_handed =
      GetPrivateProfileIntA("bwvr", "controller_left_handed", 0, path) != 0;
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
    g_hand_glow = number("hand_glow", 1) != 0;
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
    g_teleport.test_keys = number("test_keys", 0) != 0;
    // The grab from a distance: point at an object that is out of reach,
    // close the grip and pull toward the head. throw_law=1 sends sqrt(s) v in
    // the God view and on the table, so a throw lands where the real throw
    // lands, times the scale.
    g_far_grab.on = number("far_grab", 1) != 0;
    g_far_grab.cone_deg = number("far_grab_cone_deg", 8);
    g_far_grab.pull_cmps = number("far_grab_pull", 80);
    g_far_grab.choice = number("far_grab_choice", 1);
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
  g_intro_still = GetPrivateProfileIntA("bwvr", "intro_still", 1, path) != 0;
  g_intro_distance_cm =
      GetPrivateProfileIntA("bwvr", "intro_distance_cm", 300, path);
  g_intro_controller =
      GetPrivateProfileIntA("bwvr", "intro_controller", 1, path) != 0;
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
  ModLog("settings     : vr=%d, stereo=%d, screen %.2f m wide at %.2f m",
         g_vr_enabled ? 1 : 0, g_stereo ? 1 : 0, g_width, g_distance);
  ModLog("settings     : eye separation %.3f world units, interface shift "
         "%.0f px", g_separation, g_ui_shift);
}

void ModLogNowhere(const char* format, ...);

// Starting the VR session needs the Direct3D device, which does not exist
// until the game has opened it. So this runs on a frame, not in DllMain, where
// almost nothing is safe to do anyway.
//
// It keeps trying rather than giving up after one go. The runtime is
// registered as soon as Virtual Desktop is installed, but it reports no head
// mounted display until the headset actually connects, so a single attempt at
// startup forces the player to connect first and launch second. Getting that
// order wrong looks exactly like the mod is broken. Retrying costs one failed
// call every few seconds and lets the headset arrive whenever it likes.
void StartVrOnce() {
  if (g_screen != nullptr || g_vr_gave_up) return;

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
    FovInstall();
    InputWatchInstall();
    PointerMapInstall();
    HeadCameraInstall();
    PointerMapSetRebuild(g_game_camera_rebuild);
    ComfortCameraSetOptions(g_snap_turn_deg, g_instant_jumps, g_camera_glide,
                            g_blink_ms);
    ComfortCameraSetZoomStep(g_zoom_step);
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
    SkySetGlareMode(g_sun_glare);
    SkyDetailInstall();
    SpiritAnchorSetEnabled(g_spirit_anchor);
    SpiritAnchorInstall();
    AudioSetRoom(g_audio_room, g_audio_units_per_meter,
                 g_audio_flat_places);
    AudioSetRange(g_audio_range, g_audio_loops_keep, g_audio_test_tag,
                  g_audio_test_tag_bank);
    AudioInstall(g_audio_head, g_audio_advisors, g_audio_trace);
    DetailDistanceSet(g_detail_distance);
    SnapshotGuardInstall();
    PresentWatchSetEnabled(true);
    PresentWatchInstall();
    GpuBridgeProbeInterop();
    ModLog("vr           : game renders at %ux%u", width, height);
    // The pointer is scaled onto this, so the window may be any size.
    PointerMapSetRenderSize(static_cast<int>(width),
                            static_cast<int>(height));
    UiDepthInstall(g_draw_probe);
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
    HeadPoseSetFake(g_fake_head, g_fake_yaw, g_fake_pitch, g_fake_roll);
    HeadPoseFakeSweep(g_fake_sweep);
    if (g_fake_head) {
      HeadPoseSetFakeLean(g_fake_lean_cm[0], g_fake_lean_cm[1],
                          g_fake_lean_cm[2]);
    }
    ControllersSetOptions(g_controllers, g_controller_left_handed,
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
        ThrowPhysicsInstallEarly();
        FarGrabInstall();
        ThrowPhysicsInstall();
        HandGlowSetEnabled(g_hand_glow);
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
    PointerMapSetOptions(g_pointer_scale, g_pointer_clip);
    HeadCameraSetHandAnchored(g_hand_anchored);
    StereoSetHeadTracking(g_head_tracking);
    StereoSetFixes(g_freeze_time, g_move_camera_point);
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
    IntroSetOptions(g_intro_vr, g_intro_still, g_intro_distance_cm,
                    g_intro_controller, g_intro_probe);
    IntroInstall();
  }

  // Headless measuring runs the two passes and throws the pictures away, so
  // it needs no runtime, no headset and no Virtual Desktop. Skipping the
  // session keeps a measuring run to one command on the desktop.
  if (g_stereo_headless) {
    g_vr_gave_up = true;
    return;
  }

  bwvr::XrScreen* screen = new bwvr::XrScreen();
  screen->SetPlacement(g_distance, g_width);
  screen->EnablePanel(g_stereo && g_ui_panel.mode == kUiPanelLayer);
  // Only the first failure and every tenth after it reach the log. A message
  // every three seconds for two minutes buries everything else.
  const bool quiet = (g_vr_tries > 0) && (g_vr_tries % 10 != 0);
  if (!screen->Start(width, height, g_stereo ? 2 : 1,
                     quiet ? &ModLogNowhere : &ModLog)) {
    delete screen;
    ++g_vr_tries;
    if (g_vr_tries == 1) {
      ModLog("vr           : waiting for the headset, will keep trying for "
             "%d seconds", (kMaxVrTries * kVrRetryMs) / 1000);
    }
    if (g_vr_tries >= kMaxVrTries) {
      g_vr_gave_up = true;
      ModLog("vr           : no headset arrived, the game stays on the "
             "monitor");
    }
    return;
  }

  // The attempt that succeeded may have been a quiet one, and the sink Start
  // was given lasts for the whole session. Put the real log back, or every
  // openxr line after this point goes nowhere: the eye angles, the measured
  // separation, and the size the headset asks for.
  screen->SetLog(&ModLog);

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
  IntroSetScreen(g_screen);
  ControllersSetScreen(g_screen);

  // With a backend that keeps the frame on the GPU, the readback can go.
  // This is allowed to fail: the CPU path stays available underneath it.
  if (GpuBridgeHasDevice()) {
    GpuBridgeStart(g_screen->D3D11Device(), width, height,
                   g_screen->SwapchainFormat());
  }
  ModLog("vr           : running, frame reaches the headset %s",
         GpuBridgeReady() ? "without a readback" : "through a readback");
}

// A log sink that throws the line away, for the quiet retries.
void ModLogNowhere(const char*, ...) {}

void __cdecl h_FinishFrame() {
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

  if (!g_vr_enabled && !g_stereo_headless) return;
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
  // The game's check for an internet connection can wait forever when a
  // service of Windows does not answer. The mod answers it by default. It
  // goes in here, before each other thing, because it must work with vr
  // turned off too.
  g_net_check_own = GetPrivateProfileIntA("bwvr", "net_check", 0, path) == 0;
  NetCheckInstall(g_net_check_own);
  // On by default: it is the tested path, and it falls back to the readback
  // on its own if the device cannot share a texture.
  const bool wanted =
      GetPrivateProfileIntA("bwvr", "gpu_bridge", 1, path) != 0;
  // On by default from version 1.1.0. Read here because the device hooks of
  // the bridge need it.
  Depth24SetWanted(GetPrivateProfileIntA("bwvr", "depth24", 1, path) != 0);
  if (!wanted) return;

  const MH_STATUS status = MH_Initialize();
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
    ModLog("gpu bridge   : MinHook would not start, staying on the CPU path");
    return;
  }
  GpuBridgeSetEnabled(true);
  GpuBridgeInstall();
}

void Startup() {
  ModLog("%s", ProxyBackendNote());
  ModLog("");
  // A second try, if LHMultiplayerR.dll was not loaded at the first one.
  NetCheckInstall(g_net_check_own);

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

  void* target = reinterpret_cast<void*>(bw::render::kFinishFrame);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_FinishFrame),
                    reinterpret_cast<void**>(&o_FinishFrame));
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : LH3DRender::FinishFrame at 0x%08X %s",
         bw::render::kFinishFrame, status == MH_OK ? "ok" : "FAILED");
  if (status != MH_OK) g_vr_enabled = false;
  Depth24Install();
}

void Shutdown() {
  if (g_screen != nullptr) {
    ModLog("");
    ModLog("final: %llu frames, %llu presented, %llu skipped", g_frame,
           g_screen->presented(), g_screen->skipped());
    FrameSourceReport();
    StereoReport();
    UiDepthReport();
    TextureFilterReport();
    g_screen->Stop();
    delete g_screen;
    g_screen = nullptr;
  }
  // Let the cursor go before anything else. A clipped cursor outlives the
  // process that clipped it, so a crash here would leave the pointer trapped
  // in a rectangle on a dead window.
  PointerMapRelease();
  ComfortCameraRelease();
  SkyDetailRelease();
  GpuBridgeShutdown();
  FrameSourceShutdown();
  MH_Uninitialize();
  if (g_log != nullptr) {
    fclose(g_log);
    g_log = nullptr;
  }
}

}  // namespace

void ModLog(const char* format, ...) {
  if (g_log == nullptr) return;
  EnterCriticalSection(&g_lock);
  va_list args;
  va_start(args, format);
  vfprintf(g_log, format, args);
  va_end(args);
  fputc('\n', g_log);
  fflush(g_log);
  LeaveCriticalSection(&g_lock);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
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
    if (!ProxyLoadRealDDraw()) return FALSE;
    if (game_ok) Startup();
  } else if (reason == DLL_PROCESS_DETACH) {
    Shutdown();
  }
  return TRUE;
}
