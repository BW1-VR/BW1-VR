// Stereo: render the scene twice, once per eye.
//
// The engine projects every vertex on the CPU, so there is no way to get a
// second eye except to submit the geometry again against a second camera.
// 84% of vertices are drawn while GGame::Process3dEngine walks the scene, and
// only 3% inside LH3DRender::FinishFrame. So the walk is what has to run
// twice, not a deferred draw list.
//
// GGame::ProcessGraphicsEngine does this, in order:
//
//   LHMouse::UpdateDeltaPos     consume the mouse
//   GCamera::Update             which ends in LH3DTech::UpdateCamera
//   GGame::Process3dEngine      the scene, StartFrame to FinishFrame
//
// Hooking Process3dEngine rather than its caller is deliberate. The mouse is
// read once and the camera logic steps once, both outside our loop, so the
// only thing that repeats is the drawing. We take the matrix the camera just
// wrote, offset it left and right, and run the walk against each.
//
// The camera matrix at 0x00EA9E40 holds its basis in the COLUMNS, scaled by
// the field of view terms. Camera space X is therefore pre-multiplied by
// inv_half_tan_fov_x, so moving the eye sideways by d world units means
//
//     m[9] -= inv_half_tan_fov_x * d
//
// and not simply m[9] -= d. Getting that wrong gives an eye separation that
// changes with the field of view.
//
// Three things have to be handled for the second pass:
//
// 1. Animations advance per call, not per frame. Windmill::PreDraw reads
//    LH3DTech::g_game_time_inc, steps its own rotation by it and stores the
//    result, so a second call spins it twice as fast. Scrolls, the portal,
//    swaying trees and the interface all do the same. Freezing the time step
//    for the second pass leaves the state where the first pass put it, so
//    both eyes show the same moment. The second time global, the delta time,
//    cannot be held from here: LH3DRender::StartFrame writes it at the start
//    of each walk (frame_time.cpp).
//
// 2. LH3DTech::g_camera, the camera position in world space, must NOT move
//    with the eye. The Hand is
//    placed relative to that point, so moving it per eye moves the Hand with
//    the eye, and an object that moves with the eye has no disparity and
//    reads as infinitely far away. The right model is the one real eyes use:
//    the head stays put, the eyes move around it. So the matrices move, and
//    everything positioned relative to the camera stays where the mono camera
//    put it.
//
// 2b. The MATRICES, plural. There is more than one, and moving only the one
//    ProjectPoint reads left shadows, sprites and the landscape reading a
//    mono camera while the rest of the world moved. eye_camera.cpp owns that
//    and explains which matrices and why.
//
// 3. The camera matrix must go back exactly as it was, because the Hand and
//    the interface read it after the scene walk.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "one_walk.h"
#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "../vr/xr_screen.h"
#include "bubble_card.h"
#include "cutscene_window.h"
#include "frame_profile.h"
#include "d3d7_count.h"
#include "d3d7_thread.h"
#include "fast_transform.h"
#include "thread_wait.h"
#include "render_thread.h"
#include "scene_sample.h"
#include "ui_panel.h"

// d3d9_backend.cpp: the start with DXVK did not fail.
void D3D9BackendConfirm();

extern void ModLog(const char* format, ...);

const void* FrameSourceGrab(int* pitch);
const void* FrameSourceBegin(int* pitch);
void FrameSourceEnd();
void FrameSourceReport();
bool FrameSourceSize(uint32_t* width, uint32_t* height);
bool GpuBridgeReady();
bool GpuBridgeHasDevice();
void PresentWatchReport(uint64_t frames);
void* GpuBridgeCaptureEye(int eye);
void* GpuBridgeCaptureEyeFrom(int eye, void* source, const long rect[4],
                              bool flush);
const void* GpuBridgeSelfTestReadFrom(int eye, void* source,
                                      const long rect[4], int* pitch);
void GpuBridgeReport(uint64_t frames);
bool GpuBridgeSelfTestStart(uint32_t width, uint32_t height);
const void* GpuBridgeSelfTestRead(int eye, int* pitch);
void GpuBridgeSelfTestDone();
void EyeCaptureArmNow(uint64_t stereo_frame);
void EyeCaptureSaveAs(const char* suffix, uint64_t stereo_frame, int eye,
                      const void* pixels, int pitch, uint32_t width,
                      uint32_t height);

// ui_depth.cpp: move the flat interface off the screen plane.
void UiDepthSetEye(int eye, float shift_pixels);
// head_pose.cpp: the player's head, in the game's camera space.
bool HeadPoseForEye(const float position[3], const float orientation[4],
                    float matrix_out[9], float translation_out[3]);
void HeadPoseSetEnabled(bool on);
bool HeadPoseEnabled();
void HeadPoseRecenter();
void HeadPoseSetScale(float units_per_meter);
float HeadPoseScale();
// eye_camera.cpp: move the whole camera by a rigid transform.
void EyeCameraSetPose(const float h[9], const float d[3], bool log = true);
void EyeCameraLogPoses(int count);
void HeadPoseSetHead(const float center[3]);
void HeadPoseFakeSweep(bool on);
void HeadPoseFakeStep();
bool HeadPoseFakeSweeping();
void HeadPoseSetFake(bool on, float yaw_degrees, float pitch_degrees,
                     float roll_degrees);
bool HeadPoseUpdate(const float position[3], const float orientation[4]);
bool HeadCameraEnabled();
void HeadCameraReport();
void HeadCameraWorldIsUp();
void SnapshotGuardReport();
void WaterModeApply();
void UiDepthCycleSubtitleDegrees();
void HeadCameraSetHandAnchored(bool on);
bool HeadCameraHandAnchored();
float HeadCameraRoll();
// Bracketed around the whole scene walk, so the sub cameras the engine builds
// inside it never take the frame's head state. See head_camera.cpp.
void HeadCameraBeginScene();
void HeadCameraEndScene();
bool HeadCameraRollEnabled();
// head_pose.cpp: take the roll out of a declared orientation.
void HeadPoseStripRoll(const float in[4], float out[4]);
bool HeadPoseFakeOn();
void HeadPoseFakeEye(int eye, float position[3], float orientation[4]);
void HeadPoseFakeCenter(float out[3]);
void PassGuardLogHand(int count);
void UiDepthDumpNextFrame();
void PassGuardSetEye(int eye);
void PassGuardReport();
void FrameTimersReport(uint64_t frames);
void FovReport();
void FovSetAbsolute(float radians);
float FovAbsolute();
void InputWatchReport();
void PointerMapReport();
// comfort_camera.cpp
void ComfortCameraReport();
void ScriptCameraReport();
void UiDepthReportHelp();
void SkyDetailReport();
// spirit_anchor.cpp
void SpiritAnchorReport();
void SpiritAnchorWorldSizeToggle();
// controllers.cpp
void ControllersFrame();
void ControllersReport();
uint64_t ControllersFrameCount();
// game_start.cpp: the line of the start of the game, and the clock of the log.
void GameStartFrame();
double SecondsSinceProcessStart();
// hand_direct.cpp: the direct hand.
void HandDirectFrame();
void HandDirectPassBegin(int eye);
void HandDirectPassDone(int eye);
void HandDirectReport();
// hand_glow.cpp: the glow at the Hand's point.
void HandGlowReport();
// target_glow.cpp: the glow on the mesh of a target of the Hand.
void TargetGlowFrame();
void TargetGlowPassDone();
void TargetGlowReport();
// overlay.cpp: the mod's own meshes. See docs/hand-model.md, "Overlay
// renderer".
void OverlayFrame();
void OverlayPassEye(int eye);
void OverlayReport();
// teleport.cpp: the teleport with a visible arc. See docs/god-view.md.
void TeleportFrame();
void TeleportReport();
// creature_command.cpp: the trigger commands the leashed Creature, and a
// shake takes the leash off. See docs/creature.md, "The commands".
void CreatureCommandFrame();
void CreatureCommandReport();
void MiracleFrame();
void MiracleReport();
// village.cpp
void VillageFrame();
void VillageReport();
// gesture_air.cpp: gestures drawn in the air.
void GestureAirReport();
// tabletop.cpp: the island on a table in the room. See docs/tabletop.md.
float TabletopScale();
void TabletopApplyFrame();
void TabletopToggle();
void TabletopGodSpawnToggle();
void TabletopGodCameraAtToggle();
void TabletopGodZoomSmoothToggle();
void TabletopGodZoomAboutCycle();
void TabletopReport();
bool TabletopSweeping();
// world_grab.cpp: moving around by grabbing the world.
void WorldGrabReport();
void WorldGrabCycleTest();
void ControllersCycleOwnerMode();
void AudioCycleMode();
// audio_voices.cpp: the A/B of audio_stop_wait.
void AudioStopWaitToggle();
// frame_time.cpp: the frame time of the game after the two eye passes.
void FrameTimeHook(bool inside);
void FrameTimeBeforePasses();
void FrameTimePass(int eye);
void FrameTimePassDone(int eye);
uint32_t FrameTimeAfterPasses(uint32_t before_passes);
void FrameTimeToggle(const char* cause);
void FrameTimeReport();
void TickDirectReport();
// eye_clean.cpp: the frame before of each eye.
void EyeCleanBeginPair();
void EyeCleanBeforePass(int eye);
void EyeCleanAfterPass(int eye);
void EyeCleanOneWalkBefore();
void EyeCleanOneWalkAfter();
void EyeCleanEndPair();
void EyeCleanToggle(const char* cause);
void EyeCleanReport();
// frame_send.cpp: the way of the eye pictures to the headset.
int FrameSendMode();
uint64_t FrameSendEnd(const bwvr::LateFrame& pictures, int mode);
void FrameSendCycle(const char* cause);
// draw_merge.cpp: the merge of draw calls with one state.
bool DrawMergeFrame(uint64_t frame);
void DrawMergePass(bool inside);
void DrawMergeReport();
// gpu_bridge.cpp: the copy of an eye picture with no wait for the GPU.
void* GpuBridgeCopyEye(int eye);
void* GpuBridgeCopyEyeFrom(int eye, void* source, const long rect[4]);
void InputWatchAskPointer(int count);
void InputWatchFrame();
void FovSetScale(float scale);
float FovScale();
float UiDepthWorldInverseZ();
// near_plane.cpp
void NearPlaneReport();
void UiDepthNewFrame();
void UiDepthTimeSubmit(bool on);
void UiDepthTakeSubmit(double* ms, uint64_t* calls, uint64_t frames);

// eye_capture.cpp: write the two eye images to disk for inspection.
bool EyeCaptureWanted(uint64_t stereo_frame);
void EyeCaptureSave(uint64_t stereo_frame, int eye, const void* pixels,
                    int pitch, uint32_t width, uint32_t height);

// draw_probe.cpp: measure which draw calls break the stereo invariant.
void DrawProbeBeginPass(int eye);
bool DrawProbeProfile(bool on);
bool DrawProbeStates(bool on);
void DrawProbeProfileTake(double* d3d_ms, uint64_t* calls, uint64_t* vertices,
                          uint64_t* indices, uint64_t frames);

// eye_camera.cpp: move the whole camera family, not just one matrix.
bool EyeCameraCapture();
void EyeCameraSetEye(float d);
void EyeCameraRestore();

// eye_luma.cpp: the mean brightness of each eye picture, a test.
bool EyeLumaOn();
void EyeLumaEye(int eye, const void* pixels, int pitch, uint32_t width,
                uint32_t height);
void EyeLumaFrame(uint64_t frame);
void EyeLumaReport();
// temple.cpp: before the scene walk of a frame in the Temple.
void TempleBeforeScene();

namespace {

// GGame::Process3dEngine is __thiscall with no arguments, so the object
// arrives in ecx.
typedef void(__fastcall* Process3dEngineFn)(void* self, void* unused);
Process3dEngineFn o_Process3dEngine = nullptr;

bwvr::XrScreen* g_screen = nullptr;
float g_eye_separation = 0.5f;  // world units between the eyes
bool g_stereo_on = true;
float g_ui_shift = 0.0f;        // pixels, see draw_hooks.cpp
bool g_ui_adaptive = true;
float g_ui_shift_max = 24.0f;
float g_ui_shift_used = 0.0f;
bool g_freeze_time = true;      // stop the second pass advancing animations
bool g_move_camera_point = false;  // see note 2 above: this made the Hand worse
// GGame's own two copies of the frame time, in milliseconds, at g_game plus
// these offsets. GGame::Loop writes both from the same value as kGameTimeInc
// (0x0054D366 and 0x0054D380). See the freeze in the eye loop.
const uintptr_t kGameFrameTimeOffset = 0x205D48;
const uintptr_t kGameRawFrameTimeOffset = 0x250540;
bool g_game_time_logged = false;

// THE CAMERA OF THE EYE PASS THAT IS RUNNING.
//
// Code inside the scene walk can give the engine a camera of its own and then
// put the stored camera back through UpdateCamera (fn_0068B740 and
// fn_0068B7D0). That restore goes through head_camera.cpp. It rebuilds the
// camera matrices with the head, but with no eye offset and no roll. Anything
// drawn after it is drawn from a different camera than the rest of the eye.
// The sun glare is one. StereoReapplyEye makes the same call the eye loop
// made, with the same values, so the pass is on its own camera again.
enum EyePlacement { kEyeNotPlaced, kEyeSlid, kEyePosed };
EyePlacement g_eye_placement = kEyeNotPlaced;
// The eye of the pass that runs: 0 or 1, and -1 outside a pass.
int g_pass_eye = -1;
float g_eye_slide = 0.0f;  // for EyeCameraSetEye
float g_eye_pose[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
float g_eye_move[3] = {0.0f, 0.0f, 0.0f};  // for EyeCameraSetPose
// HEAD TRACKING. With this on the mod submits a projection layer instead of a flat
// quad, renders the headset's own frustum, and puts the player's head on the
// camera. All three go together: a projection layer promises that the image
// was rendered from the pose and frustum it reports, and a broken promise
// shears the world when the head moves.
bool g_head_tracking = false;
bool g_head_ready = false;
// A WINDOW TO LOOK THROUGH, for anyone the full view makes ill. With this off
// the picture stays on a quad at a fixed distance: the head still tracks, but the screen stays put in front of the
// player and nothing fills the periphery.
bool g_immersive = true;
// Take the eye separation from the headset rather than from the settings.
bool g_eye_separation_auto = true;
// The separation last taken, in meters. 0 until the headset has measured.
float g_taken_ipd = 0.0f;
// The separation of this frame, in world units. It is g_eye_separation,
// except when the table owns the frame. See FrameSeparation.
float g_frame_separation = 0.5f;
float g_said_table_separation = 0.0f;
// A human eye separation in meters, for a desktop run with no headset. The
// invented head of head_pose.cpp uses the same.
const float kDefaultIpd = 0.0668f;
// The pose the engine's camera used this frame, kept so the layer can
// declare it rather than the newer one this frame just fetched.
float g_rendered_position[2][3] = {{0, 0, 0}, {0, 0, 0}};
float g_rendered_orientation[2][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}};
bool g_head_camera_pose_ready = false;
float g_head_fov_deg = 0.0f;  // 0 means ask the headset
// How far the render goes past what the headset asks for, in percent of the
// tangent of the half angle. 0 to 30.
int g_fov_margin_pct = 0;
// Take a capture every so many milliseconds. Zero turns it off.
DWORD g_capture_interval_ms = 0;
DWORD g_next_capture_tick = 0;

// F12 and Shift F12, for tests. Off for players. See PollHotkeys.
bool g_test_keys = false;
bool g_enabled = false;
// profile=1 and profile=3: a timer on each Direct3D draw call. profile=3:
// also the state count of the draw probe. Each value of profile from 1 is
// the frame cost measure (frame_profile.cpp).
bool g_profile_draws = false;
bool g_profile_states = false;
bool g_profile_ready = false;

// Read the frame back even with no headset, so the cost of the readback can be
// measured at any resolution from a desktop session. It is the readback, not
// the game, that decides what resolution is affordable, so it needs measuring
// on its own.
bool g_headless_grab = false;

// Run the two passes with no headset and no OpenXR at all.
//
// Measuring stereo with a headset needs Virtual Desktop running and the
// headset on, which makes every measurement a round trip through the one
// person who can wear it. The draw probe does not need either. It compares what the two
// passes submit, and the passes happen inside the game. So this mode runs them
// on the desktop, throws the pictures away, and lets the probe do its work.
// The window shows the second pass, which looks like the game with the camera
// moved half an eye to the right.
bool g_headless = false;
bool g_in_pass = false;
uint64_t g_frames = 0;
uint32_t g_height = 0;
DWORD g_last_present_tick = 0;

// "Noticeably slower but playable" is a feeling. These make it a number, so
// the next change can be aimed rather than guessed.
LARGE_INTEGER g_frequency = {0};
LARGE_INTEGER g_last_frame = {0};
uint64_t g_scene_ticks = 0;    // the two scene walks
uint64_t g_grab_ticks = 0;     // reading the render target, twice
uint64_t g_wait_ticks = 0;     // blocked in BeginFrame waiting for the runtime
uint64_t g_total_ticks = 0;    // wall clock between frames

// THE WORST FRAME, NOT THE AVERAGE.
//
// A mean of 11.11 ms can hide a 40 ms frame every hundred milliseconds, and in
// a headset a single long frame is felt, not averaged. So the report carries
// the worst frame in the window and a count of the frames that missed, and the
// reports from the runtime say how many frames it actually showed.
//
// A hitch of this shape is expected: the simulation, the script machine and
// audio run on a 10 Hz game turn rather than per frame, so at 90 fps roughly
// one frame in nine carries a whole turn and the other eight carry none.
uint64_t g_frame_worst = 0;
uint64_t g_frames_missed = 0;
// What the worst frame was actually made of. A total on its own says a frame
// was late. These say which part of it was late, which is the difference
// between a fix and another guess.
uint64_t g_worst_wait = 0;
uint64_t g_worst_scene = 0;
uint64_t g_worst_grab = 0;
uint64_t g_worst_rest = 0;
uint64_t g_last_wait = 0;
uint64_t g_last_scene = 0;
uint64_t g_last_grab = 0;
double g_budget_ms = 11.6;  // a little over a 90 Hz frame

// THE REST OF THE FRAME.
//
// Everything above times what happens inside this hook. At 1080p that is
// about 3.4 ms of an 11.3 ms frame, so roughly 8 ms of every frame happens
// somewhere this file cannot see. That block barely moves when the frame rate
// moves, so it is a fixed cost and it is the largest one.
//
// The game gives us two marks per frame to split it with. This hook is
// GGame::Process3dEngine, which walks the scene. LH3DRender::FinishFrame runs
// later and ends the render. So one frame divides into:
//
//   in this hook        both eye passes, and the handoff to the headset
//   after this hook     whatever the render does between the two marks
//   FinishFrame         ending the render, which is where a present would sit
//   after FinishFrame   the simulation, the script machine, audio and input
//
// Naming those four says which of them is worth attacking.
LARGE_INTEGER g_mark_hook_end = {0};
LARGE_INTEGER g_mark_finish_start = {0};
LARGE_INTEGER g_mark_finish_end = {0};
uint64_t g_after_hook_ticks = 0;
uint64_t g_finish_ticks = 0;
uint64_t g_after_finish_ticks = 0;
uint64_t g_timed = 0;
uint64_t g_timed_now = 0;
uint64_t g_scene_ticks_now = 0;

// One frame's worth of ticks, in milliseconds. Ms() below divides by the
// number of frames in the reporting window, which is right for a total and
// wrong for a single frame.
double Ms1(uint64_t ticks) {
  if (g_frequency.QuadPart == 0) return 0.0;
  return (static_cast<double>(ticks) * 1000.0) /
         static_cast<double>(g_frequency.QuadPart);
}

double Ms(uint64_t ticks) {
  if (g_frequency.QuadPart == 0 || g_timed == 0) return 0.0;
  return (static_cast<double>(ticks) / static_cast<double>(g_timed)) * 1000.0 /
         static_cast<double>(g_frequency.QuadPart);
}

float* Matrix() { return reinterpret_cast<float*>(bw::tech::kWorldToCamera); }

float* CameraPos() { return reinterpret_cast<float*>(bw::tech::kCameraPos); }

uint32_t* GameTimeInc() {
  return reinterpret_cast<uint32_t*>(bw::tech::kGameTimeInc);
}

uint32_t* DeltaTime() {
  return reinterpret_cast<uint32_t*>(bw::tech::kDeltaTime);
}

// A uint32 field of GGame, reached through g_game. Null when there is no game
// object yet.
uint32_t* GameField(uintptr_t offset) {
  const uintptr_t* game = reinterpret_cast<const uintptr_t*>(bw::game::kGGame);
  if (IsBadReadPtr(game, sizeof(uintptr_t)) || *game == 0) return nullptr;
  uint32_t* field = reinterpret_cast<uint32_t*>(*game + offset);
  if (IsBadWritePtr(field, sizeof(uint32_t))) return nullptr;
  return field;
}

// Keep the values the eye loop gave the camera, for StereoReapplyEye.
void KeepEyePose(const float h[9], const float d[3]) {
  memcpy(g_eye_pose, h, sizeof(g_eye_pose));
  memcpy(g_eye_move, d, sizeof(g_eye_move));
  g_eye_placement = kEyePosed;
}

void KeepEyeSlide(float d) {
  g_eye_slide = d;
  g_eye_placement = kEyeSlid;
}

const bw::tech::InfoTransform* Info() {
  return reinterpret_cast<const bw::tech::InfoTransform*>(
      bw::tech::kInfoTransform);
}

// Column 0 of the matrix is the camera right axis in world space, scaled by
// the horizontal field of view term. Dividing by its own length gives the
// unit right axis, which is what an offset in world units needs.
void RightAxis(const float* m, float* out) {
  float x = m[0], y = m[3], z = m[6];
  const float length = sqrtf(x * x + y * y + z * z);
  if (length > 1e-6f) {
    x /= length;
    y /= length;
    z /= length;
  } else {
    x = 1.0f;
    y = 0.0f;
    z = 0.0f;
  }
  out[0] = x;
  out[1] = y;
  out[2] = z;
}

// How far the flat interface has to be shifted to sit in front of the world.
//
// A fixed number cannot work, and the reason is worth writing down. The engine
// projects with
//
//     screen_x = (1 + cam.x / cam.z) * half_res_x        rhw = near / cam.z
//
// and the two passes differ by scale * separation in cam.x, so the disparity
// of a world vertex, in pixels per eye, is
//
//     scale * separation * half_res_x * rhw / (2 * near)
//   = scale * separation * half_res_x / (2 * cam.z)
//
// which is proportional to rhw, and rhw is how near the vertex is. The
// second form does not use the near plane, which near_plane.cpp can lower.
// So draw_hooks.cpp gives the nearest world as 1 / cam.z. Zoomed out,
// the nearest ground has a small rhw and eight pixels puts the interface well
// in front of it. Zoom in and the ground comes to within a few units of the
// near plane, its rhw approaches 1, and it needs sixty or more pixels to stay
// behind the interface. At that point the interface is drawn OVER the terrain,
// so occlusion says it is in front, while the stereo says it is behind. The
// two cues disagree and the result is unreadable.
//
// So the shift follows the world. It is never less than the setting, which is
// the comfortable resting depth, and never more than the ceiling, because an
// interface pushed too near is a strain to read. Between those it tracks
// whatever the nearest geometry needs, with a small margin.
float AdaptiveUiShift() {
  if (!g_ui_adaptive || g_ui_shift <= 0.0f) return g_ui_shift;

  const bw::tech::InfoTransform* info = Info();
  if (IsBadReadPtr(info, sizeof(*info))) return g_ui_shift;

  const float inverse_z = UiDepthWorldInverseZ();
  if (inverse_z <= 0.0f) return g_ui_shift;

  const float needed = info->inv_half_tan_fov_x * g_frame_separation *
                       info->half_res_x * inverse_z / 2.0f;
  // A tenth over, so the interface is clearly in front rather than fighting
  // for the same depth.
  float shift = needed * 1.1f;
  if (shift < g_ui_shift) shift = g_ui_shift;
  if (shift > g_ui_shift_max) shift = g_ui_shift_max;
  return shift;
}

// THE EYES OF THE TABLE.
//
// On the table the world is s units per meter, not the life size scale. So
// the eyes are the player's own separation times s, about 130 units at 2000
// units per meter. A separation set by hand is scaled by the same ratio.
// Without the table this is g_eye_separation, bit for bit.
float FrameSeparation() {
  const float table = TabletopScale();
  if (!(table > 0.0f)) return g_eye_separation;
  float separation;
  if (g_eye_separation_auto) {
    separation = (g_taken_ipd > 0.0f ? g_taken_ipd : kDefaultIpd) * table;
  } else {
    separation = g_eye_separation * table / HeadPoseScale();
  }
  // The zoom of the table grab changes s on every frame of a grab, so a new
  // separation is logged only after a change of a tenth.
  if (fabsf(separation - g_said_table_separation) >
      0.1f * g_said_table_separation + 0.5f) {
    g_said_table_separation = separation;
    ModLog("stereo       : on the table the eyes are %.1f world units apart, "
           "at %.0f units per meter", separation, table);
  }
  return separation;
}

// THE KEYS OF A VIRTUAL KEYBOARD. The keyboard of Virtual Desktop can send a
// key down and up at once, between two frames, so GetAsyncKeyState in a
// frame does not see it. The game reads its keys from the window messages of
// its thread. A hook of the messages of that thread sees each key as the game
// sees it, and keeps the press for the next frame. The state of Shift and
// Ctrl comes from GetKeyState, which is the state of that message.
//
// A virtual keyboard can also send a capital letter as the character alone
// (VK_PACKET), with no Shift. Then only a WM_CHAR of the capital letter
// comes. So a WM_CHAR of F, G, H, J, K, L, M, N, B or V is the same key, unless the WM_KEYDOWN
// of that key came less than 200 ms before it: a real keyboard sends both.
// The digits have no such second way, because the character of Shift 8 and
// Shift 9 is not the same on each keyboard layout.
// Shift 8, Shift 9, Shift F, Shift G, Shift H, Shift J, Shift K, Shift L,
// Shift M, Shift N, Shift B, Shift V
static volatile LONG g_shift_key[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static DWORD g_shift_key_at[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static HHOOK g_message_hook = nullptr;
static bool g_message_hook_tried = false;

static int ShiftKeyIndex(WPARAM key) {
  return key == '8'   ? 0
         : key == '9' ? 1
         : key == 'F' ? 2
         : key == 'G' ? 3
         : key == 'H' ? 4
         : key == 'J' ? 5
         : key == 'K' ? 6
         : key == 'L' ? 7
         : key == 'M' ? 8
         : key == 'N' ? 9
         : key == 'B' ? 10
         : key == 'V' ? 11
                      : -1;
}

static LRESULT CALLBACK KeyMessageHook(int code, WPARAM remove, LPARAM message) {
  if (code == HC_ACTION && remove == PM_REMOVE) {
    const MSG* m = reinterpret_cast<const MSG*>(message);
    const bool first = (m->lParam & (1 << 30)) == 0;  // not a repeat
    const int index = ShiftKeyIndex(m->wParam);
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (m->message == WM_KEYDOWN && first && index >= 0 &&
        (GetKeyState(VK_SHIFT) & 0x8000) != 0 && !ctrl) {
      InterlockedExchange(&g_shift_key[index], 1);
      g_shift_key_at[index] = GetTickCount();
    } else if (m->message == WM_CHAR && index >= 2 && !ctrl &&
               GetTickCount() - g_shift_key_at[index] > 200) {
      InterlockedExchange(&g_shift_key[index], 1);
      g_shift_key_at[index] = GetTickCount();
    }
  }
  return CallNextHookEx(g_message_hook, code, remove, message);
}

// A key pressed in another program is not for the game. GetAsyncKeyState
// reads the keyboard of the whole system, so the mod reads its keys only
// while a window of this process is in front.
bool GameHasFocus() {
  HWND window = GetForegroundWindow();
  if (window == nullptr) return false;
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  return process == GetCurrentProcessId();
}

// THE MOD'S KEYS.
//
//   F11         the way the head faces now becomes straight ahead
//
// With test_keys=1 (the launcher's "Test keys" box), two more:
//
//   F12         save both eye pictures in the game folder (eye_capture.cpp)
//   Shift F12   the subtitles step through 25, 30, 35, 40 and 45 degrees
//               below the middle of the view
//   Ctrl F12    the pointer owner cycles: automatic, controller only, mouse
//               only (controllers.cpp)
//   Shift F11   the grab of the world cycles: the land glued to the hand, one
//               to one, and off (world_grab.cpp)
//   Ctrl Shift F11  the interface goes between its own layer and the eye
//               pictures, when ui_panel is not 0 (ui_panel.cpp)
//   Numpad 0    the cut scene window goes off and on, with Num Lock on
//               (cutscene_window.cpp)
//   Numpad 1    the depth limit of the cut scene window goes off and on
//   Ctrl Numpad 0  the God view follows the game camera or not, and goes to
//               the game camera again (god_spawn, tabletop.cpp)
//   Ctrl Shift Insert  the stick of the God view zooms in steps, or while it
//               is held (god_zoom_smooth, tabletop.cpp)
//   Ctrl Shift Delete  the point that a zoom of the God view keeps in the
//               room: the land that the head looks at, the land under the
//               head, or the Hand's point (god_zoom_about, tabletop.cpp)
//   Shift Home  an advisor at a place of the world has the size for the eye
//               that it has at its usual place, or the size of the game
//               (spirit_world_size, spirit_anchor.cpp)
//   Ctrl Pause  the wait of the audio DLL at the stop of a sound goes off and
//               on (audio_stop_wait, audio_voices.cpp). Windows gives the key
//               VK_CANCEL for Pause with Ctrl down, so the keys of Pause
//               alone (throw_physics.cpp) do not see it
//   Ctrl Shift Numpad Star  after the two eye passes the game has the real
//               time of the frame, or the number from before the passes
//               (frame_time_real, frame_time.cpp)
//   Shift Page Down  each eye pass of the scene starts from its own frame
//               before, or the two passes share the render target
//               (eye_clean, eye_clean.cpp)
//   Shift Delete  a speech bubble of the game is one card at the depth of its
//               sign, or it is drawn as before (bubble_card, bubble_card.cpp)
//   Shift 8     the calls of each scene walk go to the render thread, or to
//               Direct3D 9 as they come (render_thread, render_thread.cpp)
//   Shift 9     the answers of some calls of a texture with no lock of the
//               device go on and off (render_thread_lock_free,
//               render_thread.cpp)
//   Shift F     a call of a walk that sets the value that the device has
//               already goes to Direct3D 9 or not (render_thread_filter,
//               render_thread.cpp)
//   Shift G     the render thread draws from buffers of its own, or gives
//               each DrawIndexedPrimitiveUP to Direct3D 9
//               (render_thread_buffers, render_thread.cpp)
//   Shift H     a batch of one walk is one draw for both eyes, or one draw
//               for each eye (one_walk_merge, one_walk.cpp)
//   Shift J     the driver gives the calls of a walk to the GPU each N
//               draws, or when it wants (gpu_flush,
//               render_thread.cpp)
//   Shift K     the render thread or the frame thread makes the copy of a
//               batch of one walk for the right eye
//               (render_thread_right_eye, render_thread.cpp)
//   Shift L     the two vertex loops of the engine run with SSE or on the
//               x87 unit (fast_transform, fast_transform.cpp)
//   Shift M     the calls of Direct3D 7 of a walk go to the D3D7 thread or
//               to dxwrapper on the frame thread (d3d7_thread,
//               d3d7_thread.cpp)
//   Shift N     the end of a walk does not wait for the render thread, or
//               it waits (render_thread_late_end, render_thread.cpp)
//   Ctrl Shift Numpad Slash  the mod waits for the GPU after each eye pass,
//               one time a frame, or one time a frame on a second thread
//               that also gives the frame to the runtime (frame_send,
//               frame_send.cpp)
//
// With play_mode=3, the tabletop mode, or play_mode=4, the God view, one
// more for everyone:
//
//   Ctrl F11    the table or the God view goes off and on (tabletop.cpp)
//
// WHY THESE KEYS. The game binds F1 to F9: F6 to F9 go to the Challenge
// Room, the Save Game Room, the Options Room and the Library. F10 reaches
// Windows as a system key and can open the window menu. F11 and F12 have no
// game action. The game also reads a held Ctrl as its zoom and a held Shift
// as its tilt, and both together start its zoom to the Hand. Those act on a
// mouse move, so no key a player needs holds one. Shift F12 holds one, and
// it is a test key with the mouse at rest. The mod reads the keys and never
// takes them from the game.
//
// The choices that these keys switched are settled, and the defaults hold the
// values chosen. The ini keys stay, except the ones the launcher rewrites on
// every save. See launcher.cpp, kRetiredKeys.
void PollHotkeys() {
  const int kKeys = 11;
  static bool down[kKeys] = {false, false, false, false, false, false,
                             false, false, false, false, false};
  const int keys[kKeys] = {VK_F11,    VK_F12,    VK_NUMPAD0, VK_NUMPAD1,
                           VK_INSERT, VK_DELETE, VK_HOME,    VK_CANCEL,
                           VK_MULTIPLY, VK_NEXT, VK_DIVIDE};
  bool now[kKeys];
  for (int i = 0; i < kKeys; ++i) {
    now[i] = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
  }
  const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool plain = !shift && !ctrl;

  if (GameHasFocus()) {
    if (now[0] && !down[0] && plain) HeadPoseRecenter();
    // An A/B of the table. It does nothing unless play_mode picks the table.
    if (now[0] && !down[0] && ctrl && !shift) TabletopToggle();
    // An A/B of the grab in the headset. Shift is the game's tilt, but only
    // with a mouse move, and a test key is pressed with the mouse at rest.
    if (g_test_keys && now[0] && !down[0] && shift && !ctrl) {
      WorldGrabCycleTest();
    }
    // An A/B of the interface layer (docs/ui-layer.md).
    if (g_test_keys && now[0] && !down[0] && shift && ctrl) UiPanelToggle();
    if (g_test_keys && now[1] && !down[1]) {
      if (plain) {
        EyeCaptureArmNow(g_frames + 1);
      } else if (shift && !ctrl) {
        UiDepthCycleSubtitleDegrees();
      } else if (ctrl && !shift) {
        // Who owns the pointer: automatic, controller only, mouse only.
        ControllersCycleOwnerMode();
      } else if (shift && ctrl) {
        // The ears: the head, or the game camera. See docs/audio.md.
        AudioCycleMode();
      }
    }
    // An A/B of the cut scene window (docs/cutscene-window.md).
    if (g_test_keys && now[2] && !down[2] && plain) CutsceneWindowToggle();
    // An A/B of the place at a camera of the game (docs/god-view.md, "The
    // place at a camera of the game").
    if (g_test_keys && now[2] && !down[2] && ctrl && !shift) {
      TabletopGodSpawnToggle();
    }
    // An A/B of the place of the game camera (docs/god-view.md, "The game
    // camera").
    if (g_test_keys && now[3] && !down[3] && ctrl && !shift) {
      TabletopGodCameraAtToggle();
    }
    // An A/B of its depth limit.
    if (g_test_keys && now[3] && !down[3] && plain) CutsceneWindowDepthToggle();
    // An A/B of the zoom of the stick in the God view (docs/god-view.md,
    // "The zoom"). Ctrl and Insert with no Shift is the key of the ring of
    // the teleport (teleport.cpp).
    if (g_test_keys && now[4] && !down[4] && ctrl && shift) {
      TabletopGodZoomSmoothToggle();
    }
    // An A/B of the point of a zoom in the God view (docs/god-view.md, "The
    // zoom"). Ctrl and Delete with no Shift is the key of the choice of a far
    // target (hand_direct.cpp).
    if (g_test_keys && now[5] && !down[5] && ctrl && shift) {
      TabletopGodZoomAboutCycle();
    }
    // An A/B of the size of an advisor at a place of the world
    // (docs/tabletop.md, "The spirits beside the table"). Home with Ctrl is
    // a key of the hint card (hint_card.cpp) and of the grip
    // (controllers.cpp).
    if (g_test_keys && now[6] && !down[6] && shift && !ctrl) {
      SpiritAnchorWorldSizeToggle();
    }
    // An A/B of the wait at the stop of a sound (docs/audio.md, "7.6 The
    // wait of a stop").
    if (g_test_keys && now[7] && !down[7] && !shift) AudioStopWaitToggle();
    // An A/B of the frame time of the game after the two eye passes
    // (docs/backend.md, "Time, bars and the second eye pass"). The star key
    // with Ctrl and no Shift is the key of the camera flight of the Temple
    // (temple.cpp), and with no Ctrl it is the key of the release at once
    // (hand_direct.cpp).
    if (g_test_keys && now[8] && !down[8] && ctrl && shift) {
      FrameTimeToggle("Ctrl, Shift and the star key of the number pad");
    }
    // An A/B of the frame before of each eye (docs/stereo-artifacts.md, "The
    // frame before of each eye"). While the Lionhead logo plays, the same
    // key changes the trails of the logo (intro_vr.cpp). Page Down with Ctrl
    // is a key of the hint card (hint_card.cpp).
    if (g_test_keys && now[9] && !down[9] && shift && !ctrl) {
      EyeCleanToggle("Shift and Page Down");
    }
    // An A/B of the speech bubble as one card (docs/ui-layer.md, "A speech
    // bubble as one card"). While the Lionhead logo plays, the same key
    // changes the place of its flat text (intro_vr.cpp). Delete with Ctrl
    // is the key of the choice of a far target (hand_direct.cpp).
    if (g_test_keys && now[5] && !down[5] && shift && !ctrl) {
      BubbleCardToggle("Shift and Delete");
    }
    // An A/B of the way of the eye pictures to the headset (docs/backend.md,
    // "Keeping the two devices in step"). The slash key with Shift and no
    // Ctrl is a key of the Temple (temple.cpp), and with no Shift it is a
    // key of the throw (throw_physics.cpp).
    if (g_test_keys && now[10] && !down[10] && ctrl && shift) {
      FrameSendCycle("Ctrl, Shift and the slash key of the number pad");
    }
  }
  // Kept up to date without focus too, so a key that is already down when
  // the game comes to the front does not count as a new press.
  for (int i = 0; i < kKeys; ++i) down[i] = now[i];
  // An A/B of the render thread (docs/backend.md, "The render thread"), and
  // of the answers with no lock of the device ("The lock of the device"):
  // Shift 8 and Shift 9, from the hook of the messages. The game uses Shift
  // with 1 to 7 for its bookmarks, and no key with 8 or 9.
  if (g_test_keys && !g_message_hook_tried) {
    g_message_hook_tried = true;
    g_message_hook =SetWindowsHookExA(WH_GETMESSAGE, KeyMessageHook, nullptr,
                                       GetCurrentThreadId());
    ModLog("test key     : the hook of the messages for Shift 8 and Shift 9 "
           "%s", g_message_hook != nullptr ? "is in" : "FAILED");
  }
  if (InterlockedExchange(&g_shift_key[0], 0) != 0) {
    RenderThreadToggle("Shift and 8");
  }
  if (InterlockedExchange(&g_shift_key[1], 0) != 0) {
    RenderThreadLockFreeToggle("Shift and 9");
  }
  if (InterlockedExchange(&g_shift_key[2], 0) != 0) {
    RenderThreadFilterToggle("Shift and F");
  }
  if (InterlockedExchange(&g_shift_key[3], 0) != 0) {
    RenderThreadBuffersToggle("Shift and G");
  }
  if (InterlockedExchange(&g_shift_key[4], 0) != 0) {
    OneWalkMergeToggle("Shift and H");
  }
  if (InterlockedExchange(&g_shift_key[5], 0) != 0) {
    RenderThreadFlushToggle("Shift and J");
  }
  if (InterlockedExchange(&g_shift_key[6], 0) != 0) {
    RenderThreadRightEyeToggle("Shift and K");
  }
  if (InterlockedExchange(&g_shift_key[7], 0) != 0) {
    FastTransformToggle("Shift and L");
  }
  if (InterlockedExchange(&g_shift_key[8], 0) != 0) {
    D3D7ThreadToggle("Shift and M");
  }
  if (InterlockedExchange(&g_shift_key[9], 0) != 0) {
    RenderThreadLateEndToggle("Shift and N");
  }
  if (InterlockedExchange(&g_shift_key[10], 0) != 0) {
    ThreadWaitToggle("Shift and B");
  }
  if (InterlockedExchange(&g_shift_key[11], 0) != 0) {
    D3D7ThreadMergeToggle("Shift and V");
  }
  // An A/B of one walk (docs/backend.md, "One walk for both eyes"). The
  // menu key (VK_APPS) has no action in the game and no other test key.
  static bool menu_down = false;
  const bool menu_now = (GetAsyncKeyState(VK_APPS) & 0x8000) != 0;
  if (g_test_keys && menu_now && !menu_down && plain && GameHasFocus()) {
    OneWalkToggle();
  }
  menu_down = menu_now;
}

// THE HEAD TRACKING SETUP, once. The render has to cover what the headset asks for,
// and the game renders a symmetric frustum whose vertical angle comes from
// the render aspect. So take the widest half angle of either eye in either
// direction and render that both ways, which needs a roughly square render.
// The intro (intro_vr.cpp) runs the same setup before its first frame.
void PrepareHeadView(bool to_headset) {
  if (g_head_tracking && to_headset && g_immersive && !g_head_ready &&
      g_screen->eye(0).valid && g_screen->eye(1).valid) {
    // What the headset asks for, horizontally and vertically, taking the
    // widest of either eye in each direction.
    float need_h = 0.0f;
    float need_v = 0.0f;
    for (int e = 0; e < 2; ++e) {
      const bwvr::EyeView& v = g_screen->eye(e);
      const float across[2] = {-v.fov_left, v.fov_right};
      const float down[2] = {v.fov_up, -v.fov_down};
      for (int i = 0; i < 2; ++i) {
        if (across[i] > need_h) need_h = across[i];
        if (down[i] > need_v) need_v = down[i];
      }
    }

    // THE RENDER SIZE MUST NOT DECIDE HOW MUCH OF THE VIEW IS FILLED.
    //
    // The game renders a symmetric frustum and takes its vertical angle from
    // the render aspect, so a horizontal angle alone only covers the view
    // when the render is square. Solve for the horizontal angle that covers
    // both, at whatever aspect the player is actually rendering at.
    float aspect = 1.0f;
    const bw::tech::InfoTransform* info =
        reinterpret_cast<const bw::tech::InfoTransform*>(
            bw::tech::kInfoTransform);
    if (!IsBadReadPtr(info, sizeof(*info)) && info->res_y > 0 &&
        info->res_x > 0) {
      aspect = static_cast<float>(info->res_x) / static_cast<float>(info->res_y);
    }
    const float wanted_h = tanf(need_h);
    const float wanted_v = tanf(need_v) * aspect;
    float half = atanf(wanted_h > wanted_v ? wanted_h : wanted_v);
    // THE MARGIN. The runtime shows a frame at the pose that the head has
    // when the frame comes to the lenses. When the head turns fast or a
    // frame is late, that pose looks past an edge of the render, and the
    // runtime has no picture there. With fov_margin_pct the render goes that
    // many percent past the view on each side: the vertical angle follows
    // the horizontal one. The launcher makes the render larger by the same
    // percent, so a degree keeps its pixels.
    if (g_fov_margin_pct > 0) {
      half = atanf(tanf(half) *
                   static_cast<float>(100 + g_fov_margin_pct) / 100.0f);
    }
    // head_fov_deg overrides what the headset asks for. The wide view and the
    // head pose arrived together, so they have to be separable: the crash on
    // entering the world is at 0x00821AEB, in the culling code, which suspects
    // the view rather than the pose.
    if (g_head_fov_deg > 0.0f) half = g_head_fov_deg * 0.5f / 57.29578f;
    if (half > 0.0f) {
      FovSetAbsolute(half * 2.0f);
      g_screen->SetRenderHalfAngle(half);
      g_screen->SetProjection(true);
      g_head_ready = true;
      const bw::tech::InfoTransform* info =
          reinterpret_cast<const bw::tech::InfoTransform*>(
              bw::tech::kInfoTransform);
      float aspect = 0.0f;
      if (!IsBadReadPtr(info, sizeof(*info)) && info->res_y != 0) {
        aspect = static_cast<float>(info->res_x) /
                 static_cast<float>(info->res_y);
      }
      const float vertical = atanf(tanf(half) / aspect) * 2.0f * 57.29578f;
      ModLog("head         : rendering %.1f by %.1f degrees at %d by %d, "
             "covering the %.1f by %.1f the headset asks for",
             half * 2.0f * 57.29578f, vertical, info->res_x, info->res_y,
             need_h * 2.0f * 57.29578f, need_v * 2.0f * 57.29578f);
      if (g_fov_margin_pct > 0) {
        ModLog("head         : fov_margin_pct=%d. The render goes %d percent "
               "past the view of the headset on each side",
               g_fov_margin_pct, g_fov_margin_pct);
      }
      // A render far from square wastes a lot of what it draws, but it still
      // COVERS, which is the part that matters. Say so rather than warn.
      if (aspect < 0.85f || aspect > 1.2f) {
        ModLog("head         : a %.2f to 1 render covers the view by being "
               "wider than it needs. A squarer one wastes less.", aspect);
      }
    }
  }
}

// The scene walk, with every early return still inside the bracket that tells
// head_camera.cpp which cameras are sub cameras. Wrapping it is what makes
// that true on the paths that draw mono as well as the one that draws two
// eyes: three of the returns below happen before the eye loop is reached.
void __fastcall Process3dEngineBody(void* self, void* unused);

void __fastcall h_Process3dEngine(void* self, void* unused) {
  FrameProfileHook(true);
  FrameTimeHook(true);
  // A frame in the Temple with no main camera call gets one here, so its
  // eye passes have the head of the frame (temple.cpp).
  TempleBeforeScene();
  HeadCameraBeginScene();
  Process3dEngineBody(self, unused);
  HeadCameraEndScene();
  FrameTimeHook(false);
  FrameProfileHook(false);
}

// One frame function of the mod. With profile=1 or 2 its time goes to the
// frame cost measure.
void FrameFunction(int index, void (*function)()) {
  if (!FrameProfileOn()) {
    function();
    return;
  }
  const uint64_t start = FrameProfileTicks();
  function();
  FrameProfileFunction(index, FrameProfileTicks() - start);
}

void __fastcall Process3dEngineBody(void* self, void* unused) {
  PollHotkeys();
  const bool to_headset = g_screen != nullptr && g_screen->running() &&
                          g_screen->eyes() > 1;
  if (!g_enabled || !g_stereo_on || (!to_headset && !g_headless)) {
    o_Process3dEngine(self, unused);
    return;
  }

  float* m = Matrix();
  if (IsBadWritePtr(m, sizeof(float) * 12)) {
    o_Process3dEngine(self, unused);
    return;
  }

  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  if (g_profile_draws && !g_profile_ready) {
    g_profile_ready = DrawProbeProfile(true);
    if (g_profile_ready && g_profile_states) DrawProbeStates(true);
    UiDepthTimeSubmit(true);
  }
  // Catching something that only appears when the player holds the hand over
  // an object is awkward with a keypress. F12 is claimed by debuggers and by
  // overlays, and a headset makes the timing hard to coordinate. So a capture
  // can also be taken on a timer: keep the thing on screen for a while and
  // one of the frames will have it.
  if (g_capture_interval_ms > 0) {
    const DWORD now_ms = GetTickCount();
    if (now_ms >= g_next_capture_tick) {
      g_next_capture_tick = now_ms + g_capture_interval_ms;
      EyeCaptureArmNow(g_frames + 1);
    }
  }

  LARGE_INTEGER t0, t1, t2, t3;
  QueryPerformanceCounter(&t0);

  // On an armed frame, save the render target BEFORE either eye
  // pass touches it.
  //
  // This is the decisive test for the doubled hand tooltip. The per pass
  // counters proved that both passes draw the same batches, so nothing is
  // accumulating and nothing is drawn once per eye. The remaining
  // explanation is that the tooltip is not drawn in this hook at all:
  // HelpSystem::PostDrawProcess at 0x005C5EB0 runs AFTER Process3dEngine, so
  // whatever it draws lands in the render target after both eye pictures have
  // already been taken, and is still sitting there when the next frame starts.
  // If this picture already has a tooltip in it, that is the answer.
  if (EyeCaptureWanted(g_frames + 1)) {
    UiDepthDumpNextFrame();
    int before_pitch = 0;
    const void* before = FrameSourceBegin(&before_pitch);
    if (before != nullptr) {
      uint32_t w = 0, h = 0;
      if (FrameSourceSize(&w, &h)) {
        EyeCaptureSaveAs("before", g_frames + 1, 0, before, before_pitch, w, h);
      }
    }
    FrameSourceEnd();
  }

  if (to_headset) {
    if (!g_screen->BeginFrame()) {
      o_Process3dEngine(self, unused);
      return;
    }
    QueryPerformanceCounter(&t1);
    // The headset recentered from its own menu. See XrScreen::TakeRecenter.
    if (g_screen->TakeRecenter()) HeadPoseRecenter();

    if (!g_screen->should_render()) {
      o_Process3dEngine(self, unused);
      g_screen->EndFrame();
      return;
    }
  } else {
    t1 = t0;
  }

  float base[12];
  memcpy(base, m, sizeof(base));

  // THE EYE SEPARATION THE HEADSET MEASURED.
  //
  // A fixed number in world units has to be right for one world scale and one
  // pair of eyes. The runtime already knows the player's own, so take the
  // distance between the two eye positions and scale it into the world. A
  // separation set by hand still wins, because tuning it by feel is how the
  // world scale gets judged.
  //
  // It is taken again whenever it changes by more than half a millimeter, so
  // an IPD wheel turned during play (Quest 3) is followed.
  if (g_eye_separation_auto && to_headset && g_screen->eye(0).valid &&
      g_screen->eye(1).valid) {
    const float ex = g_screen->eye(1).position[0] - g_screen->eye(0).position[0];
    const float ey = g_screen->eye(1).position[1] - g_screen->eye(0).position[1];
    const float ez = g_screen->eye(1).position[2] - g_screen->eye(0).position[2];
    const float ipd = sqrtf(ex * ex + ey * ey + ez * ez);
    if (ipd > 0.03f && ipd < 0.12f && fabsf(ipd - g_taken_ipd) > 0.0005f) {
      g_taken_ipd = ipd;
      g_eye_separation = ipd * HeadPoseScale();
      ModLog("head         : the headset measures %.1f mm between the eyes, "
             "which is %.3f world units at %.1f units per metre",
             ipd * 1000.0f, g_eye_separation, HeadPoseScale());
    }
  }
  g_frame_separation = FrameSeparation();

  // THE CUT SCENE WINDOW (cutscene_window.cpp). A flat frame is a frame of
  // the stereoscopic window: the camera has no head on it, the eyes are two
  // cameras side by side, and the pictures go out on a screen in the room.
  // The state changed last at the end of the frame before, so the main
  // camera call of this frame read the same value.
  const bool flat = CutsceneWindowFlat();

  // Not in a flat frame: the setup fixes the view angle of the headset, and
  // a flat frame has the game's own.
  if (!flat) PrepareHeadView(to_headset);

  // An invented head sits at the origin by construction, or at its still
  // lean, so the centre is known without asking the runtime.
  if (g_head_tracking && !to_headset && HeadPoseFakeOn()) {
    float centre[3];
    HeadPoseFakeCenter(centre);
    HeadPoseSetHead(centre);
    float fake_position[3];
    float fake_orientation[4];
    HeadPoseFakeEye(0, fake_position, fake_orientation);
    HeadPoseUpdate(centre, fake_orientation);
    // The subtitle card hangs in the room, so it needs the head of the frame.
    UiPanelHead(centre, fake_orientation);
    CutsceneWindowHead(centre, fake_orientation);
  }
  if (g_head_tracking && to_headset && g_screen->eye(0).valid &&
      g_screen->eye(1).valid) {
    // The middle of the head, so recentring does not land on one eye and put
    // the whole view half a separation off to one side. Taken as the midpoint
    // of the two eyes, which stays right however the head is turned.
    const float center[3] = {
        (g_screen->eye(0).position[0] + g_screen->eye(1).position[0]) * 0.5f,
        (g_screen->eye(0).position[1] + g_screen->eye(1).position[1]) * 0.5f,
        (g_screen->eye(0).position[2] + g_screen->eye(1).position[2]) * 0.5f};
    HeadPoseSetHead(center);
    // The frame's head pose, with no eye in it, for the camera hook. It runs
    // before this point in the next frame, so what it reads is one frame old.
    // The runtime reprojects late head movement anyway, and the alternative is
    // moving the whole frame begin earlier, which is a larger change than the
    // sea is worth tonight.
    // Declare the pose the scene was actually drawn from. The engine's camera
    // update runs earlier in the frame than this hook, so it used the pose
    // worked out on the previous frame, and that is what the layer has to
    // report.
    if (g_head_camera_pose_ready) {
      for (int e = 0; e < 2; ++e) {
        g_screen->SetRenderPose(e, g_rendered_position[e],
                                g_rendered_orientation[e]);
      }
    }
    // DECLARE A POSE THE PICTURE COULD HAVE COME FROM.
    //
    // The runtime's own pose is the right thing to declare, because the layer
    // says "this image is what a head at this pose sees" and that is exactly
    // what the render is built to be. The one place it lies is roll. With roll
    // switched off the picture is drawn level while the runtime's pose still
    // carries the player's tilt, so the runtime shows a level picture inside a
    // tilted frustum and the whole world tilts with the player's head. Take
    // the roll out of the claim whenever the render leaves it out.
    const bool declare_roll = HeadCameraRollEnabled() || !HeadCameraEnabled();
    for (int e = 0; e < 2; ++e) {
      for (int i = 0; i < 3; ++i) {
        g_rendered_position[e][i] = g_screen->eye(e).position[i];
      }
      if (declare_roll) {
        for (int i = 0; i < 4; ++i) {
          g_rendered_orientation[e][i] = g_screen->eye(e).orientation[i];
        }
      } else {
        HeadPoseStripRoll(g_screen->eye(e).orientation,
                          g_rendered_orientation[e]);
      }
    }
    g_head_camera_pose_ready = true;
    HeadPoseUpdate(center, g_screen->eye(0).orientation);
    UiPanelHead(center, g_screen->eye(0).orientation);
    CutsceneWindowHead(center, g_screen->eye(0).orientation);
  }

  UiDepthNewFrame();
  // The route of the interface for both eye passes, latched here. A flat
  // frame has no projection layer, so its interface stays in its picture.
  // The merge of draw calls: its hooks at the first frame of the scene, and
  // the pictures of its test.
  if (DrawMergeFrame(ControllersFrameCount())) EyeCaptureArmNow(g_frames + 1);
  if (RenderThreadFrame(ControllersFrameCount())) {
    EyeCaptureArmNow(g_frames + 1);
  }
  const bool capture_frame = EyeCaptureWanted(g_frames + 1);
  UiPanelBeginFrame(to_headset, to_headset && g_screen->projection() && !flat,
                    g_frames + 1, capture_frame);
  InputWatchFrame();
  g_ui_shift_used = AdaptiveUiShift();

  // THE DEPTH LIMIT OF THE CUT SCENE WINDOW. In a flat frame the eye cameras
  // are as far apart as the nearest thing in view allows, and the flat
  // interface is on the screen. Both are set here, once, so the two eye
  // passes agree.
  const float eyes_apart = g_taken_ipd > 0.0f ? g_taken_ipd : kDefaultIpd;
  if (flat) {
    float separation = 0.0f, shift = 0.0f;
    if (CutsceneWindowBeginFlat(eyes_apart, g_frames + 1, &separation,
                                &shift)) {
      g_frame_separation = separation;
      g_ui_shift_used = shift;
    }
  }

  float right[3];
  RightAxis(base, right);

  // Save the whole camera family, not just the one matrix.
  if (!EyeCameraCapture()) {
    o_Process3dEngine(self, unused);
    if (to_headset) g_screen->EndFrame();
    return;
  }

  float* camera = CameraPos();
  float base_camera[3] = {0.0f, 0.0f, 0.0f};
  const bool camera_ok = !IsBadWritePtr(camera, sizeof(float) * 3);
  if (camera_ok) memcpy(base_camera, camera, sizeof(base_camera));

  uint32_t* time_inc = GameTimeInc();
  uint32_t* delta = DeltaTime();
  const bool time_ok = !IsBadWritePtr(time_inc, sizeof(uint32_t)) &&
                       !IsBadWritePtr(delta, sizeof(uint32_t));
  const uint32_t saved_inc = time_ok ? *time_inc : 0;
  const uint32_t saved_delta = time_ok ? *delta : 0;

  // THE GAME'S OWN COPIES OF THE FRAME TIME.
  //
  // GGame::Loop also copies the frame time into the game object twice, and
  // the two globals above do not cover those copies. HelpSystem::Draw3D steps
  // the widescreen transition by +0x205D48 in each pass. So the bars came in
  // at two times the speed, and a script saw the transition end at half the
  // time. Tree::Draw, Abode::Draw and LH3DCreature::PrepareForDrawing read it
  // too. The walk cycle of animals (sub_0051AF00, from Animal::Draw,
  // Dove::Draw and SpellWolf::Draw) and a 50 ms emitter reached from
  // GLandscape::Draw read +0x250540. So animals walked at two times the speed,
  // and the right eye drew them one step ahead. The second pass gets zero in
  // both. The game writes zero to them itself on a paused frame. Only the
  // loading, saving and plasma screens write them outside GGame::Loop, and
  // none of them runs inside the scene walk.
  uint32_t* game_time =
      g_freeze_time ? GameField(kGameFrameTimeOffset) : nullptr;
  const uint32_t saved_game_time = game_time != nullptr ? *game_time : 0;
  uint32_t* raw_time =
      g_freeze_time ? GameField(kGameRawFrameTimeOffset) : nullptr;
  const uint32_t saved_raw_time = raw_time != nullptr ? *raw_time : 0;
  if (g_freeze_time && !g_game_time_logged) {
    g_game_time_logged = true;
    if (game_time != nullptr) {
      ModLog("stereo       : freeze time on second pass also holds the game "
             "frame times at 0x%08X and 0x%08X",
             reinterpret_cast<uintptr_t>(game_time),
             reinterpret_cast<uintptr_t>(raw_time));
    } else {
      ModLog("stereo       : freeze time on second pass cannot find the game "
             "frame time, left alone");
    }
  }

  HeadCameraWorldIsUp();
  GameStartFrame();
  // The hands of this frame, after the head pose of this frame.
  FrameFunction(kFpFnControllers, ControllersFrame);
  FrameFunction(kFpFnHand, HandDirectFrame);
  FrameFunction(kFpFnTeleport, TeleportFrame);
  FrameFunction(kFpFnCreatureCommand, CreatureCommandFrame);
  FrameFunction(kFpFnMiracle, MiracleFrame);
  FrameFunction(kFpFnVillage, VillageFrame);
  // The targets whose mesh glows, after each frame function that has one.
  TargetGlowFrame();
  FrameFunction(kFpFnOverlay, OverlayFrame);
  FrameFunction(kFpFnWater, WaterModeApply);
  FrameFunction(kFpFnTable, TabletopApplyFrame);
  FrameTimeBeforePasses();
  EyeCleanBeginPair();
  g_in_pass = true;
  // ONE ROLL FOR BOTH EYES.
  //
  // It is read here, not inside the loop. The scene walk can rewrite it
  // between the two passes, and the eyes would then draw degrees apart in one
  // frame, with the right eye inverted. head_camera.cpp records it once a
  // frame, and reading it once here keeps it so.
  const float frame_roll = HeadCameraRoll();

  // ONE WALK (one_walk.cpp). The left eye walks the scene, and the right eye
  // is drawn from its batches. It needs the two eyes to differ in the
  // sideways offset alone, so not with a pose for each eye. A flat frame of
  // the cut scene window has two eye cameras side by side, which is that
  // case. With a headset it needs the GPU path, because the backend's own
  // target does not hold the eyes.
  const bool pose_per_eye = g_head_tracking && !flat && !HeadCameraEnabled() &&
                            (to_headset || HeadPoseFakeOn());
  const bool one_walk = OneWalkWanted() && !pose_per_eye &&
                        !g_headless_grab &&
                        (!to_headset || GpuBridgeReady());
  bool one_walk_done = false;
  // one_walk_check=1: on a frame that saves pictures, the right eye also gets
  // its own walk, as without one walk, and its picture is saved as "walk2"
  // beside the right eye of one walk. The two pictures are of one moment.
  const bool one_walk_check = one_walk && OneWalkCheck() &&
                              EyeCaptureWanted(g_frames + 1);

  uint64_t scene = 0, grab = 0;
  uint64_t eye_scene[2] = {0, 0};
  // The way of the eye pictures to the headset, the same for both eye passes
  // (frame_send.cpp). A frame with one wait for the GPU keeps its textures
  // here until the second eye pass is over.
  const int send_mode = FrameSendMode();
  bwvr::LateFrame late = {};
  const bool profile = FrameProfileOn();
  for (int eye = 0; eye < 2; ++eye) {
    // In one walk the right eye was drawn in the pass of the left eye.
    if (eye == 1 && one_walk_done && !one_walk_check) break;
    // Left eye is negative, right eye is positive, both about the camera the
    // game chose, so the mono view stays the average of the two.
    // With the head on the camera, the runtime's own per eye pose carries
    // both the turn of the head and the separation of the eyes, so the fixed
    // sideways offset is not used at all. Without it, or with tracking
    // switched off, the fixed offset is used.
    const float d = (eye == 0 ? -0.5f : 0.5f) * g_frame_separation;
    bool placed = false;
    g_eye_placement = kEyeNotPlaced;
    g_pass_eye = eye;
    const bool real_head = g_head_tracking && !flat && to_headset &&
                           g_screen->eye(eye).valid;
    const bool fake_head =
        g_head_tracking && !flat && !to_headset && HeadPoseFakeOn();
    if ((real_head || fake_head) && !HeadCameraEnabled()) {
      float h[9];
      float offset[3];
      float fake_position[3];
      float fake_orientation[4];
      const float* position;
      const float* orientation;
      if (real_head) {
        position = g_screen->eye(eye).position;
        orientation = g_screen->eye(eye).orientation;
      } else {
        HeadPoseFakeEye(eye, fake_position, fake_orientation);
        position = fake_position;
        orientation = fake_orientation;
      }
      if (HeadPoseForEye(position, orientation, h, offset)) {
        EyeCameraSetPose(h, offset);
        KeepEyePose(h, offset);
        placed = true;
      }
    }
    if (!placed) {
      // With the head on the engine's camera, the yaw and the pitch are
      // already in every matrix the engine built. What is left is the eye
      // offset and the roll, and both go in here.
      const float roll = frame_roll;
      if (roll > 0.0005f || roll < -0.0005f) {
        // A rotation about the camera's forward axis, in the row vector
        // convention the matrix uses.
        const float c = cosf(roll), s = sinf(roll);
        const float rz[9] = {c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f};
        const float offset[3] = {d, 0.0f, 0.0f};
        EyeCameraSetPose(rz, offset);
        KeepEyePose(rz, offset);
      } else {
        EyeCameraSetEye(d);
        KeepEyeSlide(d);
      }
    }
    UiDepthSetEye(eye, g_ui_shift_used);
    BubbleCardPass(eye);
    PassGuardSetEye(eye);
    DrawProbeBeginPass(eye);

    // Move the camera point with the eye, so billboards and particles face
    // the eye that is looking at them.
    if (camera_ok && g_move_camera_point) {
      camera[0] = base_camera[0] + right[0] * d;
      camera[1] = base_camera[1] + right[1] * d;
      camera[2] = base_camera[2] + right[2] * d;
    }

    // The first pass advances the world. The second draws the same moment
    // again, so it must not step any animation on. LH3DRender::StartFrame
    // writes the delta time again at the start of each scene walk, so the
    // number that is set here lasts only to that call (frame_time.cpp).
    if (time_ok && g_freeze_time) {
      *time_inc = (eye == 0) ? saved_inc : 0;
      *delta = (eye == 0) ? saved_delta : 0;
    }
    if (game_time != nullptr) *game_time = (eye == 0) ? saved_game_time : 0;
    if (raw_time != nullptr) *raw_time = (eye == 0) ? saved_raw_time : 0;

    QueryPerformanceCounter(&t2);
    SceneSampleWalk(true);
    HandDirectPassBegin(eye);
    OverlayPassEye(eye);
    const uint64_t walk_start = profile ? FrameProfileTicks() : 0;
    FrameTimePass(eye);
    if (one_walk && eye == 0) {
      one_walk_done = OneWalkBeginPass(g_frame_separation);
    }
    const bool walk_wide = one_walk_done && eye == 0;
    // The render target of this eye pass: its clean, or the keep of the eye.
    // The wide target of one walk keeps the picture of each eye itself.
    if (walk_wide) {
      EyeCleanOneWalkBefore();
    } else {
      EyeCleanBeforePass(eye);
    }
    // The merge of draw calls takes the draw calls of the scene walk only.
    DrawMergePass(true);
    // The calls of the walk to Direct3D 9 can go to the render thread. The
    // end waits until each of them went to Direct3D 9.
    RenderThreadWalk(true);
    D3D7ThreadWalk(true, RenderThreadQueueing());
    static bool s_frame_thread_added = false;
    if (!s_frame_thread_added) {
      s_frame_thread_added = true;
      ThreadLoadAddThis("frame thread");
      // The start with DXVK did not fail (d3d9_backend.cpp).
      D3D9BackendConfirm();
    }
    D3D7CountWalk(true);
    o_Process3dEngine(self, unused);
    // The targets get the color of the game back (target_glow.cpp).
    TargetGlowPassDone();
    DrawMergePass(false);
    D3D7CountWalk(false);
    D3D7ThreadWalk(false, false);
    RenderThreadWalk(false);
    if (walk_wide) {
      EyeCleanOneWalkAfter();
      OneWalkEndPass();
    } else {
      EyeCleanAfterPass(eye);
    }
    FrameTimePassDone(eye);
    const uint64_t walk_end = profile ? FrameProfileTicks() : 0;
    OverlayPassEye(-1);
    HandDirectPassDone(eye);
    SceneSampleWalk(false);
    QueryPerformanceCounter(&t3);
    // This eye pass is over. Nothing after it may put its camera back.
    g_eye_placement = kEyeNotPlaced;
    g_pass_eye = -1;
    scene += t3.QuadPart - t2.QuadPart;
    eye_scene[eye] = t3.QuadPart - t2.QuadPart;
    // The pass functions of the mod, before and after the walk of the game.
    if (profile) {
      FrameProfileAdd(kFpModPass, (walk_start - t2.QuadPart) +
                                      (t3.QuadPart - walk_end));
    }

    // With no headset there is nowhere to send the picture, and reading the
    // render target back would cost a millisecond for nothing.
    // The fast path. With a backend that keeps the frame in graphics memory,
    // the picture reaches the headset through two GPU copies and the CPU
    // never sees a pixel. Its cost does not grow with resolution.
    //
    // The interface panel of eye pass 0 goes first, with no flush of its own.
    // The next wait for the GPU covers its copy too: the wait of the eye 0
    // capture, or the one wait of the frame (frame_send.cpp).
    if (eye == 0) UiPanelAfterFirstEye(g_frames + 1, capture_frame);
    void* ui_texture = nullptr;
    if (eye == 0 && to_headset && GpuBridgeReady() &&
        UiPanelMode() == kUiPanelLayer) {
      ui_texture = GpuBridgeCaptureUi();
    }
    bool sent = false;
    long eye_rect[2][4];
    const bool walked_once = one_walk_done && eye == 0;
    const bool wide = walked_once && OneWalkEyeRect(0, eye_rect[0]) &&
                      OneWalkEyeRect(1, eye_rect[1]);
    if (to_headset && GpuBridgeReady() && wide) {
      // Both eyes from the halves of the wide target, in this pass.
      if (send_mode == 0) {
        // The first copy goes with no wait and the second with the wait, so
        // the two cost one wait for the GPU.
        void* left_texture =
            GpuBridgeCaptureEyeFrom(0, OneWalkTarget(), eye_rect[0], false);
        void* right_texture =
            GpuBridgeCaptureEyeFrom(1, OneWalkTarget(), eye_rect[1], true);
        if (left_texture != nullptr && right_texture != nullptr) {
          g_screen->SetEyeTexture(0, left_texture);
          g_screen->SetEyeTexture(1, right_texture);
          sent = true;
        }
      } else {
        // The copies alone. The wait for the GPU and the copy to the headset
        // come one time, after the pass (frame_send.cpp).
        for (int e = 0; e < 2; ++e) {
          late.eye_texture[e] =
              GpuBridgeCopyEyeFrom(e, OneWalkTarget(), eye_rect[e]);
        }
        sent = late.eye_texture[0] != nullptr && late.eye_texture[1] != nullptr;
      }
    } else if (to_headset && GpuBridgeReady()) {
      if (send_mode == 0) {
        void* texture = GpuBridgeCaptureEye(eye);
        if (texture != nullptr) {
          g_screen->SetEyeTexture(eye, texture);
          sent = true;
        }
      } else {
        // The copy alone. The wait for the GPU and the copy to the headset
        // come one time, after the second eye pass.
        late.eye_texture[eye] = GpuBridgeCopyEye(eye);
        sent = late.eye_texture[eye] != nullptr;
      }
    }
    if (ui_texture != nullptr && sent) {
      bwvr::PanelQuad quads[2];
      const bw::tech::InfoTransform* info = Info();
      if (!IsBadReadPtr(info, sizeof(*info)) &&
          UiPanelQuads(static_cast<uint32_t>(info->res_x),
                       static_cast<uint32_t>(info->res_y), quads)) {
        if (send_mode == 0) {
          g_screen->SetPanelTexture(ui_texture);
        } else {
          late.panel_texture = ui_texture;
        }
        g_screen->SetPanelQuads(quads);
      }
    }

    // The slow path, still here for system DirectDraw, for a backend that
    // refuses to share, and for saving a frame to disk. Reading the render
    // target back costs 0.41 ms per eye at 720p and 1.34 ms at 1440p.
    const bool want_capture = EyeCaptureWanted(g_frames + 1);

    // With no headset the shared path has no Direct3D 11 device to copy into,
    // so on a capture frame the self test makes one and saves what comes
    // back. Comparing that file with the readback file proves the path.
    if (want_capture && !sent && GpuBridgeHasDevice() && wide) {
      uint32_t shared_w = 0, shared_h = 0;
      if (FrameSourceSize(&shared_w, &shared_h) &&
          GpuBridgeSelfTestStart(shared_w, shared_h)) {
        for (int e = 0; e < 2; ++e) {
          int shared_pitch = 0;
          const void* shared = GpuBridgeSelfTestReadFrom(
              e, OneWalkTarget(), eye_rect[e], &shared_pitch);
          if (shared != nullptr) {
            EyeCaptureSaveAs("shared", g_frames + 1, e, shared, shared_pitch,
                             shared_w, shared_h);
          }
          GpuBridgeSelfTestDone();
        }
      }
    } else if (want_capture && !sent && GpuBridgeHasDevice() && one_walk_done) {
      // The check walk of the right eye, from the backend's own target.
      uint32_t shared_w = 0, shared_h = 0;
      if (FrameSourceSize(&shared_w, &shared_h) &&
          GpuBridgeSelfTestStart(shared_w, shared_h)) {
        int shared_pitch = 0;
        const void* shared = GpuBridgeSelfTestRead(eye, &shared_pitch);
        if (shared != nullptr) {
          EyeCaptureSaveAs("walk2", g_frames + 1, eye, shared, shared_pitch,
                           shared_w, shared_h);
        }
        GpuBridgeSelfTestDone();
      }
    } else if (want_capture && !sent && GpuBridgeHasDevice()) {
      uint32_t shared_w = 0, shared_h = 0;
      if (FrameSourceSize(&shared_w, &shared_h) &&
          GpuBridgeSelfTestStart(shared_w, shared_h)) {
        int shared_pitch = 0;
        const void* shared = GpuBridgeSelfTestRead(eye, &shared_pitch);
        if (shared != nullptr) {
          EyeCaptureSaveAs("shared", g_frames + 1, eye, shared, shared_pitch,
                           shared_w, shared_h);
        }
        GpuBridgeSelfTestDone();
      }
    }
    // In one walk with a headset, a frame that saves pictures reads both eyes
    // from the halves of the wide target.
    if (wide && sent && want_capture) {
      for (int e = 0; e < 2; ++e) {
        int read_pitch = 0;
        uint32_t read_w = 0, read_h = 0;
        const void* read = OneWalkReadEye(e, &read_pitch, &read_w, &read_h);
        if (read != nullptr) {
          EyeCaptureSave(g_frames + 1, e, read, read_pitch, read_w, read_h);
        }
        OneWalkReadDone();
      }
    }
    // In one walk the backend's own target holds neither eye.
    const bool luma = EyeLumaOn();
    if (!one_walk_done &&
        ((to_headset && !sent) || g_headless_grab || want_capture || luma)) {
      // The surface stays locked while the VR layer reads it, so the frame is
      // copied once instead of twice.
      int pitch = 0;
      const void* pixels = FrameSourceBegin(&pitch);
      if (pixels != nullptr) {
        if (to_headset && !sent) g_screen->SetEyeImage(eye, pixels, pitch);
        if (luma) {
          EyeLumaEye(eye, pixels, pitch, static_cast<uint32_t>(pitch / 4),
                     g_height);
        }
        if (want_capture) {
          EyeCaptureSave(g_frames + 1, eye, pixels, pitch,
                         static_cast<uint32_t>(pitch / 4), g_height);
        }
      }
      FrameSourceEnd();
    }
    QueryPerformanceCounter(&t2);
    grab += t2.QuadPart - t3.QuadPart;
  }
  UiDepthSetEye(-1, 0.0f);
  BubbleCardPass(-1);
  PassGuardSetEye(-1);
  DrawProbeBeginPass(-1);
  EyeCleanEndPair();
  g_in_pass = false;
  if (EyeLumaOn()) EyeLumaFrame(ControllersFrameCount());

  // Put everything back, because the Hand, the interface and the next game
  // tick all read these. GGame::Loop wrote the time step and the two frame
  // times of the game object for this frame, so those go back as they were.
  // The delta time is not from this frame: only LH3DRender::StartFrame writes
  // it, inside the passes. The cameras of the game read it before the next
  // scene walk, so it gets the time of the whole frame (frame_time.cpp).
  EyeCameraRestore();
  if (camera_ok) memcpy(camera, base_camera, sizeof(base_camera));
  if (time_ok) {
    *time_inc = saved_inc;
    *delta = FrameTimeAfterPasses(saved_delta);
  }
  if (game_time != nullptr) *game_time = saved_game_time;
  if (raw_time != nullptr) *raw_time = saved_raw_time;

  // The screen or the walls of the cut scene window go out with this frame.
  {
    const uint64_t end_start = profile ? FrameProfileTicks() : 0;
    CutsceneWindowStage(to_headset, g_frames + 1, eyes_apart);
    // A wait for the GPU on this thread is time of the eye pictures, as the
    // wait after each eye pass is.
    uint64_t gpu_wait = 0;
    if (to_headset) {
      if (late.eye_texture[0] != nullptr || late.eye_texture[1] != nullptr) {
        gpu_wait = FrameSendEnd(late, send_mode);
      } else {
        g_screen->EndFrame();
      }
    }
    grab += gpu_wait;
    if (profile) {
      FrameProfileAdd(kFpEndFrame,
                      FrameProfileTicks() - end_start - gpu_wait);
    }
  }
  ++g_frames;
  g_last_present_tick = GetTickCount();
  // The state of the next frame, set here and nowhere else.
  {
    const uint64_t save = CutsceneWindowEndFrame(g_frames);
    if (save != 0) EyeCaptureArmNow(save);
  }

  if (profile) {
    FrameProfileStereoFrame(ControllersFrameCount());
    FrameProfileAdd(kFpWait, t1.QuadPart - t0.QuadPart);
    FrameProfileAdd(kFpEye0, eye_scene[0]);
    FrameProfileAdd(kFpEye1, eye_scene[1]);
    FrameProfileAdd(kFpGrab, grab);
  }
  g_wait_ticks += t1.QuadPart - t0.QuadPart;
  g_scene_ticks += scene;
  g_grab_ticks += grab;
  g_last_wait = t1.QuadPart - t0.QuadPart;
  g_last_scene = scene;
  g_last_grab = grab;
  if (g_last_frame.QuadPart != 0) {
    const uint64_t this_frame = t0.QuadPart - g_last_frame.QuadPart;
    g_total_ticks += this_frame;
    if (this_frame > g_frame_worst) {
      g_frame_worst = this_frame;
      // The span just measured ended where this invocation began, so the
      // parts that belong to it are the ones the PREVIOUS invocation
      // recorded.
      g_worst_wait = g_last_wait;
      g_worst_scene = g_last_scene;
      g_worst_grab = g_last_grab;
      const uint64_t parts = g_last_wait + g_last_scene + g_last_grab;
      g_worst_rest = this_frame > parts ? this_frame - parts : 0;
    }
    if (g_frequency.QuadPart != 0) {
      const double ms = (static_cast<double>(this_frame) /
                         static_cast<double>(g_frequency.QuadPart)) * 1000.0;
      if (ms > g_budget_ms) ++g_frames_missed;
    }
    ++g_timed;
  }
  g_last_frame = t0;

  // Everything the game does between this hook returning and being called
  // again. LH3DRender::FinishFrame runs INSIDE the scene walk,
  // so this span holds no rendering at all: it is the simulation, the script
  // machine, audio, input, and the present.
  if (g_mark_hook_end.QuadPart != 0 &&
      t0.QuadPart >= g_mark_hook_end.QuadPart) {
    g_after_hook_ticks += t0.QuadPart - g_mark_hook_end.QuadPart;
  }
  QueryPerformanceCounter(&g_mark_hook_end);

  if ((g_frames % 450) == 0 && g_timed > 0) {
    const uint64_t report_start = profile ? FrameProfileTicks() : 0;
    g_timed_now = g_timed;
    g_scene_ticks_now = g_scene_ticks;
    const double total = Ms(g_total_ticks);
    // The frame of the controllers and the time: the clock of the log. A
    // run script reads from it at what time a frame of a script came.
    ModLog("stereo       : %.1f fps  (frame %.2f ms = wait %.2f + scene x2 "
           "%.2f + grab x2 %.2f), frame %llu, %.1f s after the start of the "
           "game process",
           total > 0.0 ? 1000.0 / total : 0.0, total, Ms(g_wait_ticks),
           Ms(g_scene_ticks), Ms(g_grab_ticks), ControllersFrameCount(),
           SecondsSinceProcessStart());
    ModLog("worst frame  : %.2f ms, and %llu of %llu frames missed %.1f ms",
           (static_cast<double>(g_frame_worst) /
            static_cast<double>(g_frequency.QuadPart)) * 1000.0,
           g_frames_missed, g_timed_now, g_budget_ms);
    ModLog("worst frame  : made of wait %.2f + scene x2 %.2f + grab x2 %.2f + "
           "everything else %.2f ms",
           Ms1(g_worst_wait), Ms1(g_worst_scene), Ms1(g_worst_grab),
           Ms1(g_worst_rest));
    if (g_screen != nullptr) {
      ModLog("runtime      : %llu presented, %llu skipped in total",
             g_screen->presented(), g_screen->skipped());
    }
    ModLog("rest of frame: %.2f ms outside the scene walk entirely",
           Ms(g_after_hook_ticks));
    SceneSampleReport(g_timed_now, Ms(g_scene_ticks));
    // Before the report of the render thread, which changes the key of
    // ab_test for the next window.
    FastTransformReport(g_timed_now);
    D3D7CountReport(g_timed_now);
    D3D7ThreadReport(g_timed_now);
    ThreadLoadReport(g_timed_now);
    RenderThreadReport(g_timed_now);
    PresentWatchReport(g_timed_now);
    {
      const float inverse_z = UiDepthWorldInverseZ();
      ModLog("interface    : nearest world %.0f units, shift in use %.1f px",
             inverse_z > 0.0f ? 1.0f / inverse_z : 0.0f, g_ui_shift_used);
    }
    FrameSourceReport();
    GpuBridgeReport(g_timed_now);
    OneWalkReport(g_timed_now);
    PassGuardReport();
    FrameTimersReport(g_frames);
    FrameTimeReport();
    TickDirectReport();
    EyeLumaReport();
    EyeCleanReport();
    DrawMergeReport();
    BubbleCardReport();
    FovReport();
    // Step the invented head, and photograph the result straight away. A
    // capture on a timer lands on whatever phase the timer happens to hit:
    // the first attempt captured every 5 seconds against a 2.4 second window
    // and got the same yaw every time, so two pictures that should have
    // differed came back identical.
    HeadPoseFakeStep();
    if (HeadPoseFakeSweeping() || TabletopSweeping()) {
      EyeCaptureArmNow(g_frames + 2);
    }
    PassGuardLogHand(2);
    HeadCameraReport();
    SnapshotGuardReport();
    if (g_head_tracking) EyeCameraLogPoses(2);
    InputWatchAskPointer(1);
    InputWatchReport();
    PointerMapReport();
    ComfortCameraReport();
    ScriptCameraReport();
    CutsceneWindowReport();
    UiDepthReportHelp();
    UiPanelReport();
    SkyDetailReport();
    SpiritAnchorReport();
    ControllersReport();
    HandDirectReport();
    HandGlowReport();
    TargetGlowReport();
    OverlayReport();
    TeleportReport();
    CreatureCommandReport();
    MiracleReport();
    VillageReport();
    GestureAirReport();
    WorldGrabReport();
    TabletopReport();
    NearPlaneReport();
    if (g_screen != nullptr) g_screen->ReportUpload(g_timed_now);
    if (g_profile_ready) {
      double d3d_ms = 0.0;
      uint64_t calls = 0, vertices = 0, indices = 0;
      DrawProbeProfileTake(&d3d_ms, &calls, &vertices, &indices, g_timed_now);
      const double walk_ms = Ms(g_scene_ticks_now);
      ModLog("profile      : scene x2 %.2f ms = direct3d %.2f ms + game code "
             "%.2f ms  (%.0f%% in the driver)",
             walk_ms, d3d_ms, walk_ms - d3d_ms,
             walk_ms > 0.0 ? (d3d_ms / walk_ms) * 100.0 : 0.0);
      ModLog("profile      : %llu draw calls, %llu vertices, %llu indices per "
             "frame, both passes", calls, vertices, indices);
      double submit_ms = 0.0;
      uint64_t submit_calls = 0;
      UiDepthTakeSubmit(&submit_ms, &submit_calls, g_timed_now);
      ModLog("profile      : of which DrawTriangle %.2f ms over %llu calls, "
             "so traversal and transform %.2f ms",
             submit_ms, submit_calls, walk_ms - submit_ms);
    }
    g_scene_ticks = g_grab_ticks = g_wait_ticks = g_total_ticks = 0;
    g_after_hook_ticks = g_finish_ticks = g_after_finish_ticks = 0;
    g_frame_worst = 0;
    g_frames_missed = 0;
    g_worst_wait = g_worst_scene = g_worst_grab = g_worst_rest = 0;
    g_timed = 0;
    if (profile) {
      FrameProfileAdd(kFpReport, FrameProfileTicks() - report_start);
    }
  }
}

}  // namespace

// True while the two eye passes are running, so the mono path stands aside.
bool StereoInPass() { return g_in_pass; }

// Gives the engine the camera of the eye pass that is running again, after
// something inside the walk put another camera on it. It makes the call the
// eye loop made, with the same values. Outside an eye pass, and when the
// pass placed no eye, it does nothing. See g_eye_placement.
void StereoReapplyEye() {
  if (!g_in_pass || g_eye_placement == kEyeNotPlaced) return;
  // The eye camera calls can also put the random seed back, when fix_random
  // is on. This call must not do that halfway through a pass, so the seed
  // keeps the value it has now.
  uint32_t* seed = GameField(bw::game::kRandomSeedOffset);
  const uint32_t saved_seed = seed != nullptr ? *seed : 0;
  if (g_eye_placement == kEyePosed) {
    // Quiet, so the log still gets one pose line for each eye.
    EyeCameraSetPose(g_eye_pose, g_eye_move, false);
  } else {
    EyeCameraSetEye(g_eye_slide);
  }
  if (seed != nullptr) *seed = saved_seed;
}

// The eye of the pass that runs: 0 or 1, and -1 outside a pass.
int StereoPassEye() { return g_in_pass ? g_pass_eye : -1; }

// THE CAMERA OF A ROOM OF THE TEMPLE IN AN EYE PASS. Temple::Draw gives the
// camera of the room to the engine again inside each pass, and that takes
// the camera of the eye away: the roll and the place of the eye. So
// head_camera.cpp calls this after that build. The mod keeps the camera that
// the engine built as the camera of the middle of the head, and then makes
// the call that the eye loop made, with the same values. The camera that
// goes back after the passes is then the one of the last build.
void StereoRoomCamera() {
  if (!g_in_pass || g_eye_placement == kEyeNotPlaced) return;
  if (!EyeCameraCapture()) return;
  StereoReapplyEye();
}

// Menus, loading screens and videos never reach Process3dEngine, so stereo
// stops presenting and the headset would show a frozen picture. The mono path
// takes over again when stereo has been quiet for a moment.
bool StereoIdle() {
  if (g_last_present_tick == 0) return true;
  return (GetTickCount() - g_last_present_tick) > 150;
}

void StereoSetSeparation(float world_units) {
  // Zero means "ask the headset". Anything else is the player's own choice and
  // stands.
  g_eye_separation_auto = world_units <= 0.0f;
  if (!g_eye_separation_auto) g_eye_separation = world_units;
}

// With this off the picture stays on a quad in front of the player instead of
// filling the view. Head tracking still works. It is the comfort setting for
// anyone the full view makes ill.
void StereoSetImmersive(bool on) {
  g_immersive = on;
  ModLog("head         : %s",
         on ? "the picture fills the view"
            : "the picture stays on a screen in front of you");
}

// Both default to on. They exist as switches so a report of "this looks
// wrong" can be narrowed to one change without a rebuild.
void StereoSetFixes(bool freeze_time, bool move_camera_point) {
  g_freeze_time = freeze_time;
  g_move_camera_point = move_camera_point;
}

void StereoSetHeadless(bool on) { g_headless = on; }

void StereoSetProfile(int level) {
  g_profile_draws = level == 1 || level == 3;
  g_profile_states = level == 3;
}

// The two test keys, F12 and Shift F12. See PollHotkeys.
void StereoSetTestKeys(bool on) { g_test_keys = on; }

// The world around you instead of a screen in front of you.
void StereoSetHeadFov(float degrees) { g_head_fov_deg = degrees; }
void StereoSetFovMargin(int percent) {
  g_fov_margin_pct = percent < 0 ? 0 : (percent > 30 ? 30 : percent);
}

void StereoSetHeadTracking(bool on) {
  g_head_tracking = on;
  g_head_ready = false;
  ModLog("head         : tracking is %s at startup", on ? "on" : "off");
}

void StereoSetHeadlessGrab(bool on) { g_headless_grab = on; }

void StereoSetUiDepth(bool adaptive, float ceiling_pixels) {
  g_ui_adaptive = adaptive;
  g_ui_shift_max = ceiling_pixels;
}

// For controllers.cpp, the ray of the menu: the shift that the interface got
// in the last frame (pixels, to each eye), and the eye separation of that
// frame in world units. False before the first frame and with no shift.
bool StereoUiDepthNow(float* shift_pixels, float* separation) {
  if (!(g_ui_shift_used > 0.0f) || !(g_frame_separation > 0.0f)) return false;
  *shift_pixels = g_ui_shift_used;
  *separation = g_frame_separation;
  return true;
}

// The hooks go in before the headset is found, so the screen arrives later.
// LH3DRender::FinishFrame ends the render, after the scene walk this file
// hooks. mod_main calls these around it so the time in it can be named.
void StereoMarkFinishStart() {
  if (g_mark_hook_end.QuadPart == 0) return;
  QueryPerformanceCounter(&g_mark_finish_start);
}

void StereoMarkFinishEnd() {
  if (g_mark_finish_start.QuadPart == 0) return;
  QueryPerformanceCounter(&g_mark_finish_end);
}

void StereoSetCaptureInterval(int seconds) {
  g_capture_interval_ms = static_cast<DWORD>(seconds > 0 ? seconds * 1000 : 0);
  g_next_capture_tick = GetTickCount() + g_capture_interval_ms;
  if (seconds > 0) {
    ModLog("eye capture  : saving a frame every %d seconds", seconds);
  }
}

void StereoSetScreen(bwvr::XrScreen* screen) { g_screen = screen; }

// What the intro (intro_vr.cpp) needs from this file. It draws its own frame
// pair, so it shares the head view setup, the capture timer and the mark that
// tells the mono path that stereo is driving.
void StereoIntroPrepareView(bool to_headset) { PrepareHeadView(to_headset); }

bool StereoIntroAllowed() { return g_enabled && g_stereo_on; }

bool StereoHeadTrackingOn() { return g_head_tracking; }

bool StereoIsHeadless() { return g_headless; }

// True once for each capture interval, so the intro saves a pair on the same
// timer that the scene uses.
bool StereoCaptureIntervalDue() {
  if (g_capture_interval_ms == 0) return false;
  const DWORD now = GetTickCount();
  if (now < g_next_capture_tick) return false;
  g_next_capture_tick = now + g_capture_interval_ms;
  return true;
}

void StereoMarkPresented() { g_last_present_tick = GetTickCount(); }

bool StereoInstall(bwvr::XrScreen* screen, float separation,
                   float ui_shift_pixels, uint32_t height) {
  g_screen = screen;
  g_height = height;
  // Zero asks the headset. Until the headset has measured, use the default,
  // which is 66.7 mm at 7.5 units per metre. Writing zero here would give a
  // world with no depth at all whenever there was no headset to ask, or if the
  // headset reported a bad eye position.
  g_eye_separation_auto = separation <= 0.0f;
  if (g_eye_separation_auto) separation = 0.5f;
  g_eye_separation = separation;
  g_ui_shift = ui_shift_pixels;

  void* target = reinterpret_cast<void*>(bw::game::kProcess3dEngine);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_Process3dEngine),
                    reinterpret_cast<void**>(&o_Process3dEngine));
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : GGame::Process3dEngine at 0x%08X %s",
         bw::game::kProcess3dEngine, status == MH_OK ? "ok" : "FAILED");
  if (status != MH_OK) return false;

  g_enabled = true;
  if (g_headless) {
    ModLog("stereo       : HEADLESS. Both passes run, nothing is sent to a "
           "headset. For measuring only.");
  }
  ModLog("stereo       : on, eye separation %.3f world units, interface shift "
         "%.1f px", g_eye_separation, g_ui_shift);
  ModLog("stereo       : freeze time on second pass=%d, move camera point=%d",
         g_freeze_time ? 1 : 0, g_move_camera_point ? 1 : 0);
  ModLog("interface    : resting shift %.0f px, adaptive=%d, ceiling %.0f px",
         g_ui_shift, g_ui_adaptive ? 1 : 0, g_ui_shift_max);
  ModLog("keys         : F11 makes the direction of your head straight ahead");
  if (g_test_keys) {
    ModLog("keys         : test keys are on. F12 saves both eye pictures. "
           "Shift F12 moves the subtitles. Numpad 0 turns the cut scene "
           "window off and on, and numpad 1 its depth limit.");
  }
  return true;
}

void StereoReport() {
  if (!g_enabled) return;
  ModLog("stereo       : %llu frames rendered twice", g_frames);
}
