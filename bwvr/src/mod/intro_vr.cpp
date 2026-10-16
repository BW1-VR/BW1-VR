// The Lionhead intro in stereo. Design and evidence: docs/intro.md.
//
// The intro is one function, RunAlexFX at 0x005F89F0. DoLogo calls it with
// mode 0 to build, mode 1 for each frame, and mode 2 to free. Mode 1 does its
// own StartFrame, LH3DTech::UpdateCamera, draw, FinishFrame and Flip, and it
// never calls GGame::Process3dEngine. So the scene hook of stereo.cpp never
// runs, and without this file the intro shows flat.
//
// This file runs mode 1 twice for each frame, once for each eye:
//
//   1. Before the pair: begin the headset frame, work out the head, and set
//      the scale of the room (a logo is not life size).
//   2. Eye 0: run RunAlexFX. Its camera call goes through head_camera.cpp,
//      which asks IntroCameraPose for the camera and IntroCameraBuilt to move
//      it half an eye. Its FinishFrame goes through IntroFinishFrame, which
//      copies the picture to the headset.
//   3. Eye 1: the same, with the simulation stopped and the random numbers put
//      back, so both eyes show the same moment.
//   4. After the pair: end the headset frame.
//
// THE MOTION (intro_motion, docs/intro.md "The motion"). In the effect the
// orbs stay at their places, and the camera of the game moves: it goes
// around the logo between the orbs, then back to a rest pose in front of the
// logo, then through the logo. The mod takes the place of that camera and
// gives it one fixed heading, so the path does not turn the view: the head
// of the player has the only turn. intro_motion_math.h has the cameras of
// each value of the key.
//
// With intro_motion=2 a frame has two cameras. The call at 0x005F934B gets
// the camera of the room, which stays at the rest pose and draws the sky.
// RunAlexFX calls LHMouse::UpdateDeltaPos at 0x005F95DC, after the sky and
// before it moves and draws the orbs. The hook of that function
// (input_watch.cpp) calls IntroMouseCall, and it places the camera of the
// orbs there.
//
// WHAT "STOPPED" MEANS. RunAlexFX reads LH3DTech::g_delta_time twice: at
// 0x005F9050 for the effect clock, and at 0x005F96F0 for the spring and the
// damping. LH3DRender::StartFrame writes g_delta_time in between, from the
// tick count, so setting it before the call is not enough. h_StartFrame
// writes it again when StartFrame returns, for the call at 0x005F933E only.
// The first eye gets the real frame time in both places. The second eye gets
// zero in both places, so the clock, the spring and the damping stop.
//
// THE PUSH DOES NOT STOP WITH THE TIME. While the smoothed mouse motion at
// 0x00D37CB0 and 0x00D37CB4 is not zero, the effect puts each orb near the
// pointer at a new place (0x005F991A), with no frame time in it. So a second
// eye pass with a smoothed motion moves those orbs after the first eye drew
// them. For the second eye the mod sets the smoothed motion to zero, and its
// call of LHMouse::UpdateDeltaPos does not read the mouse. The push code
// then does not run, and the motion that the mouse made in that time stays
// in the device for the next frame (intro_push_once).
//
// THE PUSH OF THE HAND. The push needs a motion of the mouse device: the
// effect reads the two motion sums of LHMouse after LHMouse::UpdateDeltaPos.
// With the pointer owned by the controller, the first eye pass adds the
// motion of the pixel of the hand to those sums (intro_hand_push_pct). So the
// hand pushes the orbs as a mouse does, through the code of the game.
//
// Three more values change on each call and are not scaled by time: the
// smoothed mouse motion, the speed of the effect clock at 0x00D34A8C (the
// mouse wheel changes it), and the C runtime random seed (each sprite gets a
// random angle from rand). The second eye starts from the values the first
// eye ended with, and they go back after it, so the game sees one step for
// each frame.
//
// THE TRAILS. The effect does not clear its picture. It sets
// LH3DRender::g_mode_cleaning to 2, and it draws the sky with an alpha of
// 160 of 255 over the picture of the frame before. So each thing that moves
// in the picture has a trail. The two eye passes share one render target, so
// with no help an eye pass draws over the picture of the other eye, and each
// eye sees each orb two times: at its own place, and weaker at the place
// that the orb has for the other eye. gpu_bridge.cpp keeps the last picture
// of each eye. Before an eye pass the mod puts the keep of that eye in the
// render target, and after the pass it takes the new picture
// (intro_eye_trails).
//
// THE FLAT TEXT AND THE BOXES. After the orbs the effect draws a text and
// boxes at a place of the picture (the mark "TM" beside the logo, a name
// under it, the logos of two makers of hardware). They have no place in the
// scene, so each eye gets them at the same pixel: no depth, and they go with
// the head. IntroFlatVertex gives each corner the pixel of its place on the
// plane of the logo (intro_text_place). draw_hooks.cpp calls it.
//
// EVERY DOUBT FALLS BACK TO THE FLAT INTRO. With intro_vr off nothing is
// hooked. With it on, the code checks the bytes at every address it uses. A
// frame that takes too long, a camera call that does not arrive, or a headset
// frame that does not start send the rest of the run back to the flat path.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "../vr/xr_screen.h"
#include "vk_scene.h"
#include "intro_motion_math.h"

extern void ModLog(const char* format, ...);

// stereo.cpp
void StereoIntroPrepareView(bool to_headset);
bool StereoIntroAllowed();
bool StereoHeadTrackingOn();
bool StereoIsHeadless();
bool StereoCaptureIntervalDue();
void StereoMarkPresented();
// head_pose.cpp
void HeadPoseSetScale(float units_per_meter);
float HeadPoseScale();
void HeadPoseSetHead(const float center[3]);
bool HeadPoseUpdate(const float position[3], const float orientation[4]);
bool HeadPoseForEye(const float position[3], const float orientation[4],
                    float matrix_out[9], float translation_out[3]);
bool HeadPoseFakeOn();
void HeadPoseFakeCenter(float out[3]);
void HeadPoseFakeEye(int eye, float position[3], float orientation[4]);
void HeadPoseStripRoll(const float in[4], float out[4]);
void HeadPoseRecenter();
// head_camera.cpp
void HeadCameraIntroFrame();
void HeadCameraIntroPlace(const float* position, const float* focus);
float HeadCameraRoll();
bool HeadCameraRollEnabled();
bool HeadCameraEnabled();
// eye_camera.cpp
bool EyeCameraCapture();
void EyeCameraSetEye(float d);
void EyeCameraSetPose(const float h[9], const float d[3], bool log);
void EyeCameraRestore();
// frame_source.cpp, gpu_bridge.cpp, eye_capture.cpp
const void* FrameSourceBegin(int* pitch);
void FrameSourceEnd();
bool FrameSourceSize(uint32_t* width, uint32_t* height);
bool GpuBridgeReady();
void* GpuBridgeCaptureEye(int eye);
bool GpuBridgeKeepTake(int eye);
bool GpuBridgeKeepGive(int eye);
void GpuBridgeKeepFree();
uint64_t GpuBridgeKeepCopies();
bool EyeCaptureWanted(uint64_t stereo_frame);
void EyeCaptureArmNow(uint64_t stereo_frame);
void EyeCaptureSave(uint64_t stereo_frame, int eye, const void* pixels,
                    int pitch, uint32_t width, uint32_t height);
// controllers.cpp
void ControllersFrame();
void ControllersGesturePulse(float amplitude, float seconds);
bool ControllersOwnPointer();
bool ControllerRenderPixel(float* x, float* y, bool* off_view);
bool ControllerHandPixel(float* x, float* y, bool* off_view);

namespace {

const uintptr_t kRunAlexFx = 0x005F89F0;      // __cdecl void (int mode)
const uintptr_t kUpdateCamera = 0x00819920;   // LH3DTech::UpdateCamera
const uintptr_t kStartFrameCall = 0x005F933E; // RunAlexFX calls StartFrame
const uintptr_t kFrameCameraCall = 0x005F934B;  // and UpdateCamera, every frame
// The intro has two other camera calls. One builds the effect (0x005F8A84).
// The head does not go on it.
const uintptr_t kBuildCameraCall = 0x005F8A84;
// RunAlexFX calls LHMouse::UpdateDeltaPos, after the sky and before the orbs.
const uintptr_t kMouseCall = 0x005F95DC;
const uintptr_t kMouseUpdateDeltaPos = 0x007E4810;
const uintptr_t kGetPtd = 0x007D232B;         // __cdecl, the C runtime data

// The effect's own state, in the game's data section.
const uintptr_t kDoneFlag = 0x00D37CC8;       // byte, the effect has ended
const uintptr_t kClock = 0x00D37CCC;          // float, the effect clock
const uintptr_t kMouseLagX = 0x00D37CB0;      // int, smoothed mouse motion
const uintptr_t kMouseLagY = 0x00D37CB4;
// float, 0.1 to 1: the effect clock gets the frame time times this. The
// effect sets it to 1 when it builds, and the mouse wheel changes it.
const uintptr_t kClockSpeed = 0x00D34A8C;
const uintptr_t kParticles = 0x00D34A98;      // pointer to 0x34 byte records
const uintptr_t kHomes = 0x00D34A94;          // pointer, 3 floats for each orb
const int kOrbs = 0x1000;
const int kSeedOffset = 0x14;                 // _holdrand in the runtime data
// LHMouse: the motion that LHMouse::UpdateDeltaPos sums, and the pointer.
const uintptr_t kMouseMotionX = 0x00E852E4;
const uintptr_t kPointerX = 0x00E852C0;
// LHKeyboard + 0x108: the scan code of the last key that went down. DoLogo
// ends the logo when it is not 0 after a frame.
const uintptr_t kLastKey = 0x00E85474;

// The pictures of the intro are saved under numbers that the scene never
// uses, so a run that shows both does not overwrite one with the other.
const uint64_t kCaptureBase = 100000;

typedef void(__cdecl* RunAlexFxFn)(int mode);
RunAlexFxFn o_RunAlexFx = nullptr;
typedef void(__cdecl* StartFrameFn)();
StartFrameFn o_StartFrame = nullptr;
typedef void*(__cdecl* GetPtdFn)();

// The settings.
bool g_on = false;             // intro_vr
int g_motion = intro_motion::kPath;  // intro_motion
int g_speed_pct = 100;         // intro_speed_pct: the speed of the effect clock
float g_distance_m = 3.0f;     // intro_distance_cm: the logo plane, from the head
float g_model_distance_m = 2.0f;  // intro_model_distance_cm, intro_motion=2
bool g_eye_trails = true;      // intro_eye_trails: each eye has its trails
bool g_text_place = true;      // intro_text_place: the flat text has a place
bool g_push_once = true;       // intro_push_once: one push for each frame
bool g_controller = true;      // intro_controller: the aim moves the pointer
int g_hand_push_pct = 100;     // intro_hand_push_pct: the hand pushes the orbs
int g_hand_push_set_pct = 100; // the value of the ini, for the test key
bool g_hand_point = true;      // intro_hand_point: the pointer is at the hand
bool g_probe = false;          // intro_probe: log the cameras each second
bool g_test_keys = false;      // test_keys
int g_motion_test_ms = 0;      // intro_motion_test_seconds, a test
int g_push_test_px = 0;        // intro_push_test_px, a test

bool g_installed = false;
bool g_gave_up = false;
bwvr::XrScreen* g_screen = nullptr;

// The state of the pair that is running.
int g_eye = -1;                // the eye pass that is running, or -1
bool g_to_headset = false;
uint32_t g_pass_delta = 0;     // what StartFrame must leave in g_delta_time
bool g_pass_delta_valid = false;
bool g_camera_seen = false;    // IntroCameraPose ran in this eye pass
bool g_camera_built = false;   // and IntroCameraBuilt did
bool g_eye_sent = false;       // the picture reached the headset
float g_eye_position[2][3] = {{0, 0, 0}, {0, 0, 0}};
float g_eye_orientation[2][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}};
bool g_have_eyes = false;
float g_separation_m = 0.0668f;  // the eyes, in meters
float g_scale = 0.0f;          // units per meter in the intro, 0 until known
float g_saved_scale = 0.0f;    // the scale of the world, put back at the end
bool g_scale_swapped = false;
bool g_view_ready = false;

// THE CAMERAS OF THE PAIR. The first eye pass works them out from the camera
// that the game asks for, and the second eye pass uses them as they are.
bool g_have_cameras = false;
bool g_two_cameras = false;    // the orbs have a camera of their own
float g_game_position[3] = {0, 0, 0};
float g_game_focus[3] = {0, 0, 0};
float g_room_position[3] = {0, 0, 0};
float g_room_focus[3] = {0, 0, 0};
float g_orb_position[3] = {0, 0, 0};
float g_orb_focus[3] = {0, 0, 0};
int g_part_logged = -1;        // the part of the path that the log has
DWORD g_motion_test_tick = 0;
int g_key_pulses = 0;          // pulses of a test key that are not given yet
DWORD g_key_pulse_tick = 0;

// The flat batches of the effect.
intro_motion::View g_flat_view = {0.0f, 0.0f, 0.0f, 0.0f};
uint64_t g_flat_placed = 0;
uint64_t g_flat_dropped = 0;
bool g_flat_said = false;
bool g_no_keep_said = false;

// Bookkeeping.
uint64_t g_pairs = 0;
uint64_t g_flat_frames = 0;
DWORD g_last_pair_tick = 0;
DWORD g_active_tick = 0;
int g_slow_run = 0;
int g_missed_run = 0;
int g_unsent_run = 0;
LARGE_INTEGER g_frequency = {0};
double g_call_ms_total = 0.0;
double g_call_ms_worst = 0.0;
double g_pair_ms_total = 0.0;
double g_pair_ms_worst = 0.0;
uint64_t g_timed = 0;
int g_clock_logged = -1;
int g_probe_lines = 0;
bool g_menu_before = false;

// The push of the hand.
intro_motion::HandPush g_hand = {false, 0.0f, 0.0f, 0.0f, 0.0f};
uint64_t g_hand_pair = 0;        // the pair of the last step
int32_t g_hand_point_x = 0;      // the last pixel of the Hand's point
int32_t g_hand_point_y = 0;
bool g_have_hand_point = false;
int32_t g_push_pointer_x = 0;    // the pointer that the push of the effect read
int32_t g_push_pointer_y = 0;
uint64_t g_hand_push_frames = 0; // frames in which the hand gave a motion
uint64_t g_hand_push_counts = 0; // the sum of that motion, across and down
bool g_hand_push_said = false;

uint32_t* DeltaTime() {
  return reinterpret_cast<uint32_t*>(bw::tech::kDeltaTime);
}
uint32_t* GameTimeInc() {
  return reinterpret_cast<uint32_t*>(bw::tech::kGameTimeInc);
}
uint8_t* DoneFlag() { return reinterpret_cast<uint8_t*>(kDoneFlag); }
float* Clock() { return reinterpret_cast<float*>(kClock); }

// A rand seed, from the runtime data of this thread.
uint32_t* Seed() {
  uint8_t* data = static_cast<uint8_t*>(reinterpret_cast<GetPtdFn>(kGetPtd)());
  if (data == nullptr) return nullptr;
  uint32_t* seed = reinterpret_cast<uint32_t*>(data + kSeedOffset);
  if (IsBadWritePtr(seed, sizeof(uint32_t))) return nullptr;
  return seed;
}

double MsBetween(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
  if (g_frequency.QuadPart == 0) return 0.0;
  return static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 /
         static_cast<double>(g_frequency.QuadPart);
}

// True when the bytes at the call site are a near call to the target.
bool CallsTarget(uintptr_t site, uintptr_t target) {
  const uint8_t* code = reinterpret_cast<const uint8_t*>(site);
  if (IsBadReadPtr(code, 5) || code[0] != 0xE8) return false;
  int32_t relative = 0;
  memcpy(&relative, code + 1, sizeof(relative));
  return site + 5 + static_cast<uintptr_t>(relative) == target;
}

bool CodeIsWhatWeExpect() {
  static const uint8_t prologue[6] = {0x81, 0xEC, 0xA0, 0x01, 0x00, 0x00};
  const uint8_t* start = reinterpret_cast<const uint8_t*>(kRunAlexFx);
  if (IsBadReadPtr(start, sizeof(prologue)) ||
      memcmp(start, prologue, sizeof(prologue)) != 0) {
    ModLog("intro        : RunAlexFX does not start as expected, no hook");
    return false;
  }
  if (!CallsTarget(kFrameCameraCall, kUpdateCamera) ||
      !CallsTarget(kBuildCameraCall, kUpdateCamera) ||
      !CallsTarget(kMouseCall, kMouseUpdateDeltaPos) ||
      !CallsTarget(kStartFrameCall, bw::render::kStartFrame)) {
    ModLog("intro        : the calls inside RunAlexFX are not where expected, "
           "no hook");
    return false;
  }
  if (IsBadReadPtr(reinterpret_cast<void*>(kGetPtd), 1) ||
      IsBadWritePtr(reinterpret_cast<void*>(kClock), sizeof(float)) ||
      IsBadWritePtr(reinterpret_cast<void*>(kClockSpeed), sizeof(float)) ||
      IsBadWritePtr(reinterpret_cast<void*>(kMouseLagX), sizeof(int32_t) * 2) ||
      IsBadWritePtr(reinterpret_cast<void*>(kMouseMotionX),
                    sizeof(int32_t) * 2) ||
      IsBadWritePtr(reinterpret_cast<void*>(kPointerX), sizeof(int32_t) * 2) ||
      IsBadWritePtr(reinterpret_cast<void*>(kLastKey), sizeof(uint32_t)) ||
      IsBadWritePtr(reinterpret_cast<void*>(kDoneFlag), 1) ||
      IsBadWritePtr(DeltaTime(), sizeof(uint32_t)) ||
      IsBadWritePtr(GameTimeInc(), sizeof(uint32_t))) {
    ModLog("intro        : a game address is not readable, no hook");
    return false;
  }
  return true;
}

void RestoreScale() {
  if (!g_scale_swapped) return;
  g_scale_swapped = false;
  HeadPoseSetScale(g_saved_scale);
  ModLog("intro        : the room is back at %.1f units per meter",
         g_saved_scale);
}

void GiveUp(const char* why) {
  if (g_gave_up) return;
  g_gave_up = true;
  GpuBridgeKeepFree();
  g_eye = -1;
  g_pass_delta_valid = false;
  g_active_tick = 0;
  RestoreScale();
  ModLog("intro        : %s, so the rest of the intro is flat", why);
}

// LH3DRender::StartFrame writes g_delta_time from the tick count. The call at
// 0x005F933E in RunAlexFX must see the time this pass wants instead. See the
// top of the file.
void __cdecl h_StartFrame() {
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  o_StartFrame();
  if (g_pass_delta_valid && caller == kStartFrameCall + 5) {
    *DeltaTime() = g_pass_delta;
  }
}

// Where the head is this frame, in the game's room, and what the runtime
// should be told the picture was drawn from. Mirrors the head block of
// Process3dEngineBody in stereo.cpp.
void UpdateHead() {
  g_have_eyes = false;
  if (!StereoHeadTrackingOn()) return;
  if (g_to_headset) {
    const bwvr::EyeView& left = g_screen->eye(0);
    const bwvr::EyeView& right = g_screen->eye(1);
    if (!left.valid || !right.valid) return;
    float center[3];
    for (int i = 0; i < 3; ++i) {
      center[i] = (left.position[i] + right.position[i]) * 0.5f;
      g_eye_position[0][i] = left.position[i];
      g_eye_position[1][i] = right.position[i];
    }
    for (int i = 0; i < 4; ++i) {
      g_eye_orientation[0][i] = left.orientation[i];
      g_eye_orientation[1][i] = right.orientation[i];
    }
    const float dx = right.position[0] - left.position[0];
    const float dy = right.position[1] - left.position[1];
    const float dz = right.position[2] - left.position[2];
    const float ipd = sqrtf(dx * dx + dy * dy + dz * dz);
    if (ipd > 0.03f && ipd < 0.12f) g_separation_m = ipd;
    HeadPoseSetHead(center);
    HeadPoseUpdate(center, left.orientation);
    // This frame draws from this frame's pose, so that is the pose to
    // declare. The scene of stereo.cpp draws from the pose of the frame
    // before, because its camera call runs before the frame begins.
    const bool declare_roll = HeadCameraRollEnabled() || !HeadCameraEnabled();
    for (int e = 0; e < 2; ++e) {
      float orientation[4];
      if (declare_roll) {
        memcpy(orientation, g_eye_orientation[e], sizeof(orientation));
      } else {
        HeadPoseStripRoll(g_eye_orientation[e], orientation);
      }
      g_screen->SetRenderPose(e, g_eye_position[e], orientation);
    }
    g_have_eyes = true;
  } else if (HeadPoseFakeOn()) {
    float center[3];
    HeadPoseFakeCenter(center);
    HeadPoseSetHead(center);
    for (int e = 0; e < 2; ++e) {
      HeadPoseFakeEye(e, g_eye_position[e], g_eye_orientation[e]);
    }
    HeadPoseUpdate(center, g_eye_orientation[0]);
    g_have_eyes = true;
  }
}

const char* MotionText(int mode) {
  switch (mode) {
    case intro_motion::kStill:
      return "the camera stays at the rest pose";
    case intro_motion::kPath:
      return "the camera goes along the path of the game and does not turn";
    case intro_motion::kModel:
      return "the orbs are a model in front of the player, and the room "
             "stays still";
    default:
      return "the camera of the game, with its turns";
  }
}

// The scale of the room for the intro, from the distance in meters between
// the head and the plane of the logo at the rest pose. The model has a
// distance of its own. A logo is not life size, so the scale of the world
// goes back when the intro ends.
void ApplyScale() {
  const float want = intro_motion::UnitsPerMeter(
      g_motion == intro_motion::kModel ? g_model_distance_m : g_distance_m);
  if (!(want > 0.0f)) return;
  if (g_scale_swapped && want == g_scale) return;
  const float was = HeadPoseScale();
  if (!g_scale_swapped) g_saved_scale = was;
  g_scale_swapped = true;
  g_scale = want;
  HeadPoseSetScale(want);
  ModLog("intro        : the room is %.1f units per meter here, was %.1f. The "
         "plane of the logo is %.2f m from the head at the rest pose",
         want, was, intro_motion::kRestDistance / want);
}

// The first stereo frame of the intro: the speed of the effect clock, and
// the size of the logo for the log.
void FirstPair() {
  if (g_speed_pct != 100) {
    const float speed = intro_motion::ClockSpeed(g_speed_pct);
    *reinterpret_cast<float*>(kClockSpeed) = speed;
    ModLog("intro        : intro_speed_pct=%d: the clock of the effect gets "
           "%.2f of the frame time, so the path and the fades take %.0f s "
           "and not %.0f s", g_speed_pct, speed,
           intro_motion::kEndClock / 1.1f / speed,
           intro_motion::kEndClock / 1.1f);
  }
  const float* homes = *reinterpret_cast<const float* const*>(kHomes);
  if (homes != nullptr && !IsBadReadPtr(homes, sizeof(float) * 3 * kOrbs)) {
    const float width = intro_motion::LogoWidth(homes, kOrbs);
    const float scale = g_scale > 0.0f ? g_scale : HeadPoseScale();
    ModLog("intro        : the logo is %.1f units wide on its plane, and "
           "that plane is %.0f units from the rest pose: %.2f m wide at "
           "%.2f m", width, intro_motion::kRestDistance, width / scale,
           intro_motion::kRestDistance / scale);
  }
}

// One step of intro_motion, for the test key and for the test of a desktop
// run. The pointing controller gives one short pulse for each unit of the
// new value, and one long pulse for 0.
void StepMotion(const char* who) {
  g_motion = intro_motion::NextMode(g_motion);
  g_key_pulses = g_motion;
  g_key_pulse_tick = GetTickCount();
  if (g_motion == 0) ControllersGesturePulse(0.5f, 0.40f);
  ModLog("test key     : %s: intro_motion is %d now (%s), clock %.2f", who,
         g_motion, MotionText(g_motion), *Clock());
  // The log gets the cameras of the new value at once.
  g_part_logged = -1;
}

// One switch of intro_eye_trails, for its test key. The keeps start again,
// so a keep from before the switch does not come back.
void SwitchTrails(const char* who) {
  g_eye_trails = !g_eye_trails;
  GpuBridgeKeepFree();
  ModLog("test key     : %s: intro_eye_trails is %d now (%s), clock %.2f", who,
         g_eye_trails ? 1 : 0,
         g_eye_trails ? "each eye pass draws over the last picture of the "
                        "same eye"
                      : "each eye pass draws over the last picture in the "
                        "render target, which is the picture of the other "
                        "eye",
         *Clock());
}

// THE A/B KEYS (test_keys=1), read one time for each stereo frame of the
// intro, and only while a window of the game is in front. Shift with Page Up
// steps intro_motion. Shift with Page Down switches intro_eye_trails, and
// Shift with Delete switches intro_text_place: a long pulse for 1 and a
// short pulse for 0. Shift with Home switches the push of the hand between
// off and on, and Shift with End switches intro_hand_point: a long pulse
// for on and a short pulse for off. Ctrl must be up for each of them.
void PollKeys() {
  static bool was_motion = false;
  static bool was_trails = false;
  static bool was_text = false;
  static bool was_hand = false;
  static bool was_point = false;
  if (g_key_pulses > 0 && GetTickCount() >= g_key_pulse_tick) {
    --g_key_pulses;
    g_key_pulse_tick = GetTickCount() + 250;
    ControllersGesturePulse(0.5f, 0.08f);
  }
  if (!g_test_keys) return;
  const bool lone_shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 &&
                          (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0;
  const bool motion = lone_shift && (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
  const bool trails = lone_shift && (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
  const bool text = lone_shift && (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
  const bool hand = lone_shift && (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
  const bool motion_pressed = motion && !was_motion;
  const bool trails_pressed = trails && !was_trails;
  const bool text_pressed = text && !was_text;
  const bool point = lone_shift && (GetAsyncKeyState(VK_END) & 0x8000) != 0;
  const bool hand_pressed = hand && !was_hand;
  const bool point_pressed = point && !was_point;
  was_motion = motion;
  was_trails = trails;
  was_text = text;
  was_hand = hand;
  was_point = point;
  if (!motion_pressed && !trails_pressed && !text_pressed && !hand_pressed &&
      !point_pressed) {
    return;
  }
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  if (motion_pressed) StepMotion("Shift and Page Up");
  if (trails_pressed) {
    SwitchTrails("Shift and Page Down");
    ControllersGesturePulse(0.5f, g_eye_trails ? 0.24f : 0.08f);
  }
  if (text_pressed) {
    g_text_place = !g_text_place;
    ControllersGesturePulse(0.5f, g_text_place ? 0.24f : 0.08f);
    ModLog("test key     : Shift and Delete: intro_text_place is %d now "
           "(%s), clock %.2f", g_text_place ? 1 : 0,
           g_text_place ? "the flat text and the boxes of the effect are on "
                          "the plane of the logo"
                        : "the flat text and the boxes have the same pixel "
                          "in each eye",
           *Clock());
  }
  if (hand_pressed) {
    g_hand_push_pct = g_hand_push_pct > 0
                          ? 0
                          : (g_hand_push_set_pct > 0 ? g_hand_push_set_pct
                                                     : 100);
    ControllersGesturePulse(0.5f, g_hand_push_pct > 0 ? 0.24f : 0.08f);
    ModLog("test key     : Shift and Home: intro_hand_push_pct is %d now "
           "(%s), clock %.2f", g_hand_push_pct,
           g_hand_push_pct > 0 ? "a motion of the hand pushes the orbs"
                               : "only a motion of the mouse pushes the orbs",
           *Clock());
  }
  if (point_pressed) {
    g_hand_point = !g_hand_point;
    ControllersGesturePulse(0.5f, g_hand_point ? 0.24f : 0.08f);
    ModLog("test key     : Shift and End: intro_hand_point is %d now (%s), "
           "clock %.2f", g_hand_point ? 1 : 0,
           g_hand_point ? "the pointer is at the Hand's point"
                        : "the pointer is where the aim ray points",
           *Clock());
  }
}

// A key that goes down ends the logo: DoLogo tests the last key after each
// frame. While a key of the A/B keys is down, the mod clears the last key,
// so a press of a test key does not end the logo.
void KeepLogoForTestKey() {
  if (!g_test_keys) return;
  if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0 &&
      (GetAsyncKeyState(VK_PRIOR) & 0x8000) == 0 &&
      (GetAsyncKeyState(VK_NEXT) & 0x8000) == 0 &&
      (GetAsyncKeyState(VK_DELETE) & 0x8000) == 0 &&
      (GetAsyncKeyState(VK_HOME) & 0x8000) == 0 &&
      (GetAsyncKeyState(VK_END) & 0x8000) == 0) {
    return;
  }
  *reinterpret_cast<uint32_t*>(kLastKey) = 0;
}

// A test of a desktop run (intro_motion_test_seconds): each N seconds of the
// stereo intro the mod does what the test key of intro_motion does.
void MotionTest() {
  if (g_motion_test_ms <= 0) return;
  const DWORD now = GetTickCount();
  if (g_motion_test_tick == 0) {
    g_motion_test_tick = now + g_motion_test_ms;
  } else if (now >= g_motion_test_tick) {
    g_motion_test_tick = now + g_motion_test_ms;
    StepMotion("intro_motion_test_seconds (a test, no key went down)");
  }
}

float Distance(const float a[3], const float b[3]) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

// One line with the cameras of the pair: where the mod draws from, the
// direction, and where the logo is from there.
void LogCameras(const char* what) {
  float look[3];
  const float reach = Distance(g_room_focus, g_room_position);
  for (int i = 0; i < 3; ++i) {
    look[i] = reach > 1e-4f ? (g_room_focus[i] - g_room_position[i]) / reach
                            : 0.0f;
  }
  char orbs[160] = "";
  if (g_two_cameras) {
    _snprintf_s(orbs, sizeof(orbs), _TRUNCATE, " The orbs have a camera of "
                "their own, at %.3f %.3f %.3f, with the same direction.",
                g_orb_position[0], g_orb_position[1], g_orb_position[2]);
  }
  ModLog("intro        : clock %.2f, %s. The game asks for the camera at "
         "%.3f %.3f %.3f, looking at %.3f %.3f %.3f. With intro_motion=%d "
         "the mod draws from %.3f %.3f %.3f, looking along %.3f %.3f %.3f.%s "
         "From the camera of the orbs the logo is %.1f degrees from straight "
         "ahead and %.1f units away", *Clock(), what, g_game_position[0],
         g_game_position[1], g_game_position[2], g_game_focus[0],
         g_game_focus[1], g_game_focus[2], g_motion, g_room_position[0],
         g_room_position[1], g_room_position[2], look[0], look[1], look[2],
         orbs, intro_motion::LogoAngleDeg(g_orb_position, g_orb_focus),
         Distance(g_orb_position, intro_motion::kLogo));
}

// The log of the path: one line when a new part of the path starts, and with
// intro_probe one line for each second of the effect clock.
void LogPath() {
  static const char* const kPartText[4] = {
      "the camera of the game goes around the logo, between the orbs",
      "the camera of the game leaves the orbit and goes to the rest pose",
      "the camera of the game is at the rest pose",
      "the camera of the game goes through the logo"};
  const int part = intro_motion::Part(*Clock());
  const int second = static_cast<int>(*Clock());
  if (part != g_part_logged) {
    g_part_logged = part;
    g_clock_logged = second;
    LogCameras(kPartText[part]);
  } else if (g_probe && second != g_clock_logged && g_probe_lines < 120) {
    g_clock_logged = second;
    ++g_probe_lines;
    LogCameras("one second of the clock later");
  }
}

// One eye pass: run the effect once with the state of that eye.
void RunEye(int eye, uint32_t delta, uint32_t game_time_inc) {
  g_eye = eye;
  g_camera_seen = false;
  g_camera_built = false;
  g_eye_sent = false;
  // The effect draws over the picture that is in the render target. That
  // must be the last picture of this eye. See "THE TRAILS" at the top.
  if (g_eye_trails) GpuBridgeKeepGive(eye);
  // backend=vulkan: the pixels of the CPU are not the picture, so a frame
  // that saves the eyes also asks the renderer for the picture of the walk
  // (bwvr-vk-left-M.bmp, M = 10 times the number of the capture plus the
  // eye).
  if (VkSceneDirect() && EyeCaptureWanted(kCaptureBase + g_pairs)) {
    VkSceneFrame(0, (kCaptureBase + g_pairs) * 10 + eye, true);
  }
  *DeltaTime() = delta;
  *GameTimeInc() = game_time_inc;
  g_pass_delta = delta;
  g_pass_delta_valid = true;
  o_RunAlexFx(1);
  g_pass_delta_valid = false;
  g_eye = -1;
  EyeCameraRestore();
}

// A sample of one particle, to show that the second eye does not move it.
void ProbeParticle(const char* when, float clock) {
  const uint8_t* records = *reinterpret_cast<uint8_t* const*>(kParticles);
  if (records == nullptr || IsBadReadPtr(records + 0x34 * 100, 12)) return;
  const float* p = reinterpret_cast<const float*>(records + 0x34 * 100);
  ModLog("intro        : pair %llu %s clock %.4f particle 100 at %.3f %.3f "
         "%.3f", g_pairs, when, clock, p[0], p[1], p[2]);
}

// With intro_probe, for a pair that has a picture of each eye: the pixel of
// some orbs for the camera of this eye pass, as LH3DTech::ProjectPoint gives
// it. The pictures must have an orb at each of these pixels.
void ProbeOrbs() {
  const uint8_t* records = *reinterpret_cast<uint8_t* const*>(kParticles);
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  if (records == nullptr || IsBadReadPtr(records, 0x34 * kOrbs) ||
      IsBadReadPtr(info, sizeof(*info))) {
    return;
  }
  const intro_motion::View view = {info->half_res_x, info->half_res_y,
                                   info->inv_half_tan_fov_x,
                                   info->inv_half_tan_fov_y};
  const float* m = reinterpret_cast<const float*>(bw::tech::kWorldToCamera);
  const float near_plane =
      *reinterpret_cast<const float*>(bw::tech::kNearPlane);
  // The first 8 of each 97th orb that have a pixel in the picture.
  int lines = 0;
  for (int orb = 100; orb < kOrbs && lines < 8; orb += 97) {
    const float* p = reinterpret_cast<const float*>(records + 0x34 * orb);
    const float depth = p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11];
    float x = 0.0f, y = 0.0f;
    if (!intro_motion::Project(m, view, p, near_plane, &x, &y)) continue;
    if (x < 0.0f || y < 0.0f || x > view.half_res_x * 2.0f ||
        y > view.half_res_y * 2.0f) {
      continue;
    }
    ++lines;
    ModLog("intro        : pair %llu %s eye, orb %d at %.3f %.3f %.3f is "
           "%.2f units in front of the eye, at the pixel %.1f %.1f",
           g_pairs, g_eye == 0 ? "left" : "right", orb, p[0], p[1], p[2],
           depth, x, y);
  }
}

// With intro_probe: how many orbs are farther than `distance` from their
// home. Only a push moves an orb from its home, and the spring pulls it back.
int OrbsAway(float distance) {
  const uint8_t* records = *reinterpret_cast<uint8_t* const*>(kParticles);
  const float* homes = *reinterpret_cast<const float* const*>(kHomes);
  if (records == nullptr || homes == nullptr ||
      IsBadReadPtr(records, 0x34 * kOrbs) ||
      IsBadReadPtr(homes, sizeof(float) * 3 * kOrbs)) {
    return -1;
  }
  int away = 0;
  for (int orb = 0; orb < kOrbs; ++orb) {
    const float* p = reinterpret_cast<const float*>(records + 0x34 * orb);
    if (Distance(p, homes + orb * 3) > distance) ++away;
  }
  return away;
}

void LogPair() {
  if (g_timed == 0) return;
  ModLog("intro        : %llu pairs, %.2f ms for the two effect calls (worst "
         "%.2f), %.2f ms for the whole pair (worst %.2f)", g_pairs,
         g_call_ms_total / g_timed, g_call_ms_worst, g_pair_ms_total / g_timed,
         g_pair_ms_worst);
  g_call_ms_total = g_pair_ms_total = 0.0;
  g_call_ms_worst = g_pair_ms_worst = 0.0;
  g_timed = 0;
}

// THE PUSH OF THE HAND, in the first eye pass, after LHMouse::UpdateDeltaPos
// has added the motion of the mouse device and pointer_map.cpp has put the
// pixel of the controller in the pointer. The value of the active box is not
// 0 while the logo plays, so that pixel is the pixel of the aim ray. With
// intro_hand_point=1 the pointer gets the pixel of the Hand's point, so the
// push is at the hand. The motion of the pixel goes into the motion sums,
// which the effect then reads. A pair that did not run in stereo before this
// one is a gap, so the pixel of the hand starts again. A frame with no pixel
// of the Hand keeps the pointer at the last one: the pixel of the aim ray is
// at a different place, and the smoothed motion of the frames before still
// pushes there.
void HandPush() {
  float x = 0.0f, y = 0.0f;
  bool off_view = true;
  bool has_pixel = false;
  if (ControllersOwnPointer()) {
    if (g_hand_point && ControllerHandPixel(&x, &y, &off_view)) {
      has_pixel = !off_view;
      if (has_pixel) {
        g_hand_point_x = static_cast<int32_t>(x);
        g_hand_point_y = static_cast<int32_t>(y);
        g_have_hand_point = true;
      }
      if (g_have_hand_point) {
        int32_t* pointer = reinterpret_cast<int32_t*>(kPointerX);
        pointer[0] = g_hand_point_x;
        pointer[1] = g_hand_point_y;
      }
    } else {
      has_pixel = ControllerRenderPixel(&x, &y, &off_view) && !off_view;
    }
  }
  if (g_hand_pair + 1 != g_pairs) g_hand.have = false;
  g_hand_pair = g_pairs;
  int dx = 0, dy = 0;
  intro_motion::HandPushStep(&g_hand, has_pixel, x, y, g_hand_push_pct, &dx,
                             &dy);
  if (dx == 0 && dy == 0) return;
  int32_t* motion = reinterpret_cast<int32_t*>(kMouseMotionX);
  motion[0] += dx;
  motion[1] += dy;
  ++g_hand_push_frames;
  g_hand_push_counts += static_cast<uint64_t>((dx < 0 ? -dx : dx) +
                                              (dy < 0 ? -dy : dy));
  if (!g_hand_push_said) {
    g_hand_push_said = true;
    ModLog("intro        : the hand moved the pointer to %.1f %.1f and gives "
           "the mouse a motion of %d %d. The effect pushes the orbs near the "
           "pointer, clock %.2f", x, y, dx, dy, *Clock());
  }
}

void RunPair() {
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);

  g_to_headset = g_screen != nullptr && g_screen->running() &&
                 g_screen->eyes() > 1;
  LARGE_INTEGER t_begin, t_calls1, t_end;
  QueryPerformanceCounter(&t_begin);

  // THE HEADSET FRAME. It blocks until the runtime is ready. If the runtime
  // does not want this frame drawn, or the session has ended, the effect runs
  // once, flat, and the frame ends.
  if (g_to_headset) {
    if (!g_screen->BeginFrame()) {
      o_RunAlexFx(1);
      ++g_flat_frames;
      return;
    }
    if (g_screen->TakeRecenter()) HeadPoseRecenter();
    if (!g_screen->should_render()) {
      o_RunAlexFx(1);
      g_screen->EndFrame();
      ++g_flat_frames;
      return;
    }
  }

  ++g_pairs;
  g_active_tick = GetTickCount();

  if (!g_view_ready) {
    g_view_ready = true;
    // The view of the headset, once, before the camera is built. The scene
    // does the same at its first frame.
    StereoIntroPrepareView(g_to_headset);
  }
  // A change of intro_motion counts from this frame on, for both eyes.
  PollKeys();
  MotionTest();
  ApplyScale();
  UpdateHead();
  HeadCameraIntroFrame();
  // The hands of this frame, for the pointer. Buttons stay off. See
  // IntroBlocksButtons.
  if (g_controller) ControllersFrame();
  if (g_pairs == 1) FirstPair();

  // The menu button on either controller ends the intro on purpose.
  if (g_to_headset) {
    const bool menu = g_screen->hand(0).menu || g_screen->hand(1).menu;
    if (menu && !g_menu_before) {
      *DoneFlag() = 1;
      ModLog("intro        : the menu button ends the intro");
    }
    g_menu_before = menu;
  }

  if (StereoCaptureIntervalDue()) EyeCaptureArmNow(kCaptureBase + g_pairs);

  // The real time of this frame, for the first eye.
  DWORD now = GetTickCount();
  uint32_t delta = g_last_pair_tick == 0 ? 16 : now - g_last_pair_tick;
  if (delta < 1) delta = 1;
  if (delta > 100) delta = 100;
  g_last_pair_tick = now;

  uint32_t* seed = Seed();
  const uint32_t seed_before = seed != nullptr ? *seed : 0;
  const uint32_t saved_inc = *GameTimeInc();
  const bool log_particles = g_probe && (g_pairs <= 4 || g_pairs % 600 == 0);
  const float clock_before = *Clock();

  g_have_cameras = false;
  RunEye(0, delta, saved_inc);
  if (log_particles) ProbeParticle("after the left eye", *Clock());
  const bool missed0 = !g_camera_seen || !g_camera_built;

  bool missed1 = false;
  // A finished effect sleeps at the top of the call. There is nothing to draw
  // for the second eye then.
  if (*DoneFlag() == 0) {
    const uint32_t seed_after = seed != nullptr ? *seed : 0;
    const int32_t lag_x = *reinterpret_cast<int32_t*>(kMouseLagX);
    const int32_t lag_y = *reinterpret_cast<int32_t*>(kMouseLagY);
    const float speed = *reinterpret_cast<float*>(kClockSpeed);
    if (seed != nullptr) *seed = seed_before;
    // No smoothed motion and no new motion (IntroMouseCall): the second eye
    // pass does not push the orbs. See the top of the file.
    if (g_push_once) {
      *reinterpret_cast<int32_t*>(kMouseLagX) = 0;
      *reinterpret_cast<int32_t*>(kMouseLagY) = 0;
    }
    RunEye(1, 0, 0);
    if (seed != nullptr) *seed = seed_after;
    *reinterpret_cast<int32_t*>(kMouseLagX) = lag_x;
    *reinterpret_cast<int32_t*>(kMouseLagY) = lag_y;
    *reinterpret_cast<float*>(kClockSpeed) = speed;
    missed1 = !g_camera_seen || !g_camera_built;
    if (log_particles) ProbeParticle("after the right eye", *Clock());
  }
  KeepLogoForTestKey();
  *GameTimeInc() = saved_inc;
  QueryPerformanceCounter(&t_calls1);

  if (g_probe && (g_pairs % 100) == 0) {
    const int32_t* pointer = reinterpret_cast<const int32_t*>(0x00E852C0);
    ModLog("intro        : pair %llu pointer at %d %d, at the push %d %d, "
           "buttons 0x%02X, the pointer is the %s, the hand pushed in %llu "
           "frames, %d orbs are more than 1 unit from their home", g_pairs,
           pointer[0], pointer[1], g_push_pointer_x, g_push_pointer_y,
           *reinterpret_cast<const uint8_t*>(0x00E85304),
           ControllersOwnPointer() ? "controller" : "mouse",
           g_hand_push_frames, OrbsAway(1.0f));
  }
  if (log_particles) {
    ModLog("intro        : pair %llu clock %.4f before, %.4f after, frame "
           "time %u ms", g_pairs, clock_before, *Clock(), delta);
  }

  if (g_to_headset) g_screen->EndFrame();
  StereoMarkPresented();
  QueryPerformanceCounter(&t_end);

  const double call_ms = MsBetween(t_begin, t_calls1);
  const double pair_ms = MsBetween(t_begin, t_end);
  if (g_pairs > 2) {
    g_call_ms_total += call_ms;
    g_pair_ms_total += pair_ms;
    if (call_ms > g_call_ms_worst) g_call_ms_worst = call_ms;
    if (pair_ms > g_pair_ms_worst) g_pair_ms_worst = pair_ms;
    ++g_timed;
    if ((g_pairs % 300) == 0) LogPair();
  }

  // THE WAYS BACK TO THE FLAT INTRO.
  g_missed_run = (missed0 || missed1) ? g_missed_run + 1 : 0;
  g_slow_run = (g_pairs > 2 && call_ms > 250.0) ? g_slow_run + 1 : 0;
  if (g_to_headset && !g_eye_sent) {
    ++g_unsent_run;
  } else {
    g_unsent_run = 0;
  }
  if (g_missed_run >= 3) {
    GiveUp("the camera of the intro was not found for 3 frames");
  } else if (g_slow_run >= 4) {
    GiveUp("4 frames in a row took more than 250 ms");
  } else if (g_unsent_run >= 3) {
    GiveUp("the picture did not reach the headset for 3 frames");
  }
}

void __cdecl h_RunAlexFx(int mode) {
  if (mode == 2) {
    // The end. Everything the intro changed goes back, and the head starts
    // again from where the player looks when the world begins.
    const bool ran = g_pairs > 0;
    g_eye = -1;
    g_active_tick = 0;
    GpuBridgeKeepFree();
    if (ran) LogPair();
    RestoreScale();
    o_RunAlexFx(mode);
    if (ran) {
      HeadPoseRecenter();
      ModLog("intro        : ended after %llu stereo frames, %llu flat "
             "frames, with intro_motion=%d. %llu flat batches (a text or a "
             "box, one for each eye pass) got a place on the plane of the "
             "logo, and %llu were not drawn: their place was behind the "
             "camera. The keeps of the eye pictures made %llu copies",
             g_pairs, g_flat_frames, g_motion, g_flat_placed, g_flat_dropped,
             GpuBridgeKeepCopies());
      ModLog("intro        : the hand gave the mouse a motion in %llu frames, "
             "%llu counts in all (intro_hand_push_pct=%d)", g_hand_push_frames,
             g_hand_push_counts, g_hand_push_pct);
    }
    g_hand.have = false;
    g_have_hand_point = false;
    return;
  }
  const bool ready = mode == 1 && g_on && !g_gave_up && g_installed &&
                     StereoIntroAllowed() && *DoneFlag() == 0 &&
                     (StereoIsHeadless() ||
                      (g_screen != nullptr && g_screen->running() &&
                       g_screen->eyes() > 1));
  if (!ready) {
    o_RunAlexFx(mode);
    if (mode == 1) ++g_flat_frames;
    return;
  }
  RunPair();
}

}  // namespace

void IntroCameraBuilt();

// For head_camera.cpp: is this call the camera call of an eye pass of the
// intro? Then out_position and out_focus get the camera of the room for
// intro_motion, in place of the camera that the game asks for.
bool IntroCameraPose(uintptr_t caller, const float* position,
                     const float* focus, float* out_position,
                     float* out_focus) {
  if (g_eye < 0 || caller != kFrameCameraCall + 5 || position == nullptr ||
      focus == nullptr) {
    return false;
  }
  if (IsBadReadPtr(position, sizeof(float) * 3) ||
      IsBadReadPtr(focus, sizeof(float) * 3)) {
    return false;
  }
  g_camera_seen = true;
  if (g_eye == 0 || !g_have_cameras) {
    // THE CAMERAS OF THIS PAIR, worked out one time. The game asks for the
    // same camera in the second eye pass, because its clock stands still
    // there.
    memcpy(g_game_position, position, sizeof(g_game_position));
    memcpy(g_game_focus, focus, sizeof(g_game_focus));
    g_two_cameras = intro_motion::Cameras(
        g_motion, g_game_position, g_game_focus, g_room_position,
        g_room_focus, g_orb_position, g_orb_focus);
    g_have_cameras = true;
    LogPath();
  }
  // This call gets the camera of the room. It draws the sky.
  memcpy(out_position, g_room_position, sizeof(float) * 3);
  memcpy(out_focus, g_room_focus, sizeof(float) * 3);
  return true;
}

// For input_watch.cpp: a call of LHMouse::UpdateDeltaPos. In an eye pass of
// the intro the effect makes one, after the sky and before it moves and
// draws the orbs. So this is the place for the camera of the orbs, when they
// have one of their own. True when the call must not read the mouse: the
// second eye pass moves no orb (see the top of the file).
bool IntroMouseCall(uintptr_t caller) {
  if (g_eye < 0 || caller != kMouseCall + 5) return false;
  if (g_two_cameras && g_have_cameras) {
    HeadCameraIntroPlace(g_orb_position, g_orb_focus);
    IntroCameraBuilt();
  }
  return g_eye == 1 && g_push_once;
}

// For input_watch.cpp: the same call has read the mouse, and the pointer has
// its place. The first eye pass adds the motion of the hand (HandPush).
// A test of a desktop run (intro_push_test_px): the first eye pass gets a
// motion of the mouse that no hand made, and the pointer is in the middle of
// the picture, on the logo. The second eye pass gets the same pointer and no
// motion, as from a real mouse.
void IntroMouseRead(uintptr_t caller) {
  if (g_eye < 0 || caller != kMouseCall + 5) return;
  if (g_eye == 0) {
    HandPush();
    // The push of the effect reads the pointer next. Other code can write
    // it later in the frame, so the probe logs this value.
    const int32_t* pointer = reinterpret_cast<const int32_t*>(kPointerX);
    g_push_pointer_x = pointer[0];
    g_push_pointer_y = pointer[1];
  }
  if (g_push_test_px == 0) return;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return;
  int32_t* pointer = reinterpret_cast<int32_t*>(kPointerX);
  pointer[0] = static_cast<int32_t>(info->half_res_x);
  pointer[1] = static_cast<int32_t>(info->half_res_y);
  if (g_eye == 0) *reinterpret_cast<int32_t*>(kMouseMotionX) += g_push_test_px;
}

// For draw_hooks.cpp: a flat batch of the interface code comes in an eye
// pass of the intro. True when each of its corners must get its place on the
// plane of the logo (IntroFlatVertex). `caller` is the code that draws it.
bool IntroFlatOn(uint32_t caller) {
  if (g_eye < 0 || !g_text_place) return false;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return false;
  g_flat_view.half_res_x = info->half_res_x;
  g_flat_view.half_res_y = info->half_res_y;
  g_flat_view.inv_half_tan_x = info->inv_half_tan_fov_x;
  g_flat_view.inv_half_tan_y = info->inv_half_tan_fov_y;
  if (!g_flat_said) {
    g_flat_said = true;
    ModLog("intro        : the effect draws a flat batch (a text or a box) "
           "from 0x%08X, clock %.2f. Each corner gets its place on the plane "
           "of the logo (intro_text_place=1)", caller, *Clock());
  }
  return true;
}

// One corner of that batch: from its pixel in the picture of the rest camera
// to its pixel for the camera of this eye pass. False when its place is not
// in front of that camera: then the batch is not drawn.
bool IntroFlatVertex(float* x, float* y) {
  const float* m = reinterpret_cast<const float*>(bw::tech::kWorldToCamera);
  const float near_plane =
      *reinterpret_cast<const float*>(bw::tech::kNearPlane);
  return intro_motion::PlaceFlat(m, g_flat_view, near_plane, x, y);
}

// For draw_hooks.cpp: the batch was drawn at its place, or it was not drawn.
void IntroFlatDone(bool drawn) {
  if (drawn) {
    ++g_flat_placed;
  } else {
    ++g_flat_dropped;
  }
}

// For head_camera.cpp: the engine has built the camera of this eye pass, with
// the head on it. Move it half an eye, the way the scene does.
void IntroCameraBuilt() {
  if (g_eye < 0) return;
  if (!EyeCameraCapture()) return;
  const float d = (g_eye == 0 ? -0.5f : 0.5f) * g_separation_m *
                  (g_scale > 0.0f ? g_scale : HeadPoseScale());
  if (g_have_eyes && !HeadCameraEnabled()) {
    // The engine's camera does not carry the head, so the pose of this eye
    // carries all of it.
    float h[9];
    float offset[3];
    if (HeadPoseForEye(g_eye_position[g_eye], g_eye_orientation[g_eye], h,
                       offset)) {
      EyeCameraSetPose(h, offset, false);
      g_camera_built = true;
      return;
    }
  }
  const float roll = HeadCameraRoll();
  if (roll > 0.0005f || roll < -0.0005f) {
    const float c = cosf(roll), s = sinf(roll);
    const float rz[9] = {c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f};
    const float offset[3] = {d, 0.0f, 0.0f};
    EyeCameraSetPose(rz, offset, false);
  } else {
    EyeCameraSetEye(d);
  }
  g_camera_built = true;
}

// For mod_main.cpp: the picture of an eye pass is finished. True when the
// intro took it, and then the mono path must not send it as well.
bool IntroFinishFrame() {
  if (g_eye < 0) return false;
  bool sent = false;
  // backend=vulkan: the picture of the walk of this eye pass, with a copy
  // of the GPU.
  {
    uint64_t image = 0;
    uint32_t layer = 0, width = 0, height = 0;
    if (g_to_headset && g_screen->vulkan() &&
        VkSceneLastPicture(&image, &layer, &width, &height) &&
        width >= g_screen->width() && height >= g_screen->height()) {
      g_screen->SetEyeVulkan(g_eye, image, layer);
      sent = true;
    }
  }
  if (!sent && g_to_headset && GpuBridgeReady()) {
    void* texture = GpuBridgeCaptureEye(g_eye);
    if (texture != nullptr) {
      g_screen->SetEyeTexture(g_eye, texture);
      sent = true;
    }
  }
  const bool want = EyeCaptureWanted(kCaptureBase + g_pairs);
  if (want && g_probe) ProbeOrbs();
  // The picture of this eye is finished. Its keep gets it: the next pass of
  // this eye draws over it. With backend=vulkan the target of the eye in the
  // renderer keeps it (IntroPassEye).
  if (g_eye_trails && !VkSceneDirect() && !GpuBridgeKeepTake(g_eye) &&
      !g_no_keep_said) {
    g_no_keep_said = true;
    ModLog("intro        : there is no keep for the picture of each eye (the "
           "mod does not have the Direct3D 9 device of the backend, or a "
           "copy failed). So each eye pass draws over the picture of the "
           "other eye, and each orb shows two times in each eye");
  }
  if ((g_to_headset && !sent) || want) {
    int pitch = 0;
    uint32_t width = 0, height = 0;
    const void* pixels = FrameSourceBegin(&pitch);
    if (pixels != nullptr) {
      if (g_to_headset && !sent) {
        g_screen->SetEyeImage(g_eye, pixels, pitch);
        sent = true;
      }
      if (want && FrameSourceSize(&width, &height)) {
        EyeCaptureSave(kCaptureBase + g_pairs, g_eye, pixels, pitch, width,
                       height);
      }
    }
    FrameSourceEnd();
  }
  if (sent) g_eye_sent = true;
  return true;
}

// For vk_scene.cpp: the eye pass of the intro that runs, when each eye keeps
// its own picture (intro_eye_trails). Else -1.
int IntroPassEye() { return g_eye_trails ? g_eye : -1; }

// For controllers.cpp: while the intro draws in stereo, a controller must not
// press a mouse button. Any click ends the logo (DoLogo tests both main
// buttons), and the trigger would end it at the first push.
bool IntroBlocksButtons() {
  return g_active_tick != 0 && GetTickCount() - g_active_tick < 400;
}

void IntroSetScreen(bwvr::XrScreen* screen) { g_screen = screen; }

void IntroSetOptions(bool on, int motion, int speed_pct, int distance_cm,
                     int model_distance_cm, bool eye_trails, bool text_place,
                     bool push_once, bool controller, bool probe,
                     bool test_keys) {
  g_on = on;
  g_motion = intro_motion::ClampMode(motion);
  g_speed_pct = speed_pct < 10 ? 10 : speed_pct > 100 ? 100 : speed_pct;
  g_distance_m = distance_cm > 10 ? distance_cm * 0.01f : 3.0f;
  g_model_distance_m =
      model_distance_cm > 10 ? model_distance_cm * 0.01f : 2.0f;
  g_eye_trails = eye_trails;
  g_text_place = text_place;
  g_push_once = push_once;
  g_controller = controller;
  g_probe = probe;
  g_test_keys = test_keys;
}

// The tests of a desktop run. `motion_seconds`: each N seconds of the intro
// in stereo, the mod steps intro_motion as its test key does.
// `push_pixels`: each frame gets that motion of the mouse to the right, with
// the pointer in the middle of the picture.
void IntroSetTests(int motion_seconds, int push_pixels) {
  g_motion_test_ms = motion_seconds > 0 ? motion_seconds * 1000 : 0;
  g_push_test_px = push_pixels;
}

// intro_hand_push_pct: the motion of the hand, in percent of its pixels, that
// goes to the motion of the mouse. 0 is off: only a mouse pushes the orbs.
// intro_hand_point: the pointer is at the Hand's point, not where the aim
// ray points.
void IntroSetHandPush(int percent, bool hand_point) {
  g_hand_push_pct = percent < 0 ? 0 : percent > 400 ? 400 : percent;
  g_hand_push_set_pct = g_hand_push_pct;
  g_hand_point = hand_point;
}

void IntroInstall() {
  if (!g_on || g_installed) return;
  if (!CodeIsWhatWeExpect()) return;
  void* run = reinterpret_cast<void*>(kRunAlexFx);
  void* start = reinterpret_cast<void*>(bw::render::kStartFrame);
  MH_STATUS status = MH_CreateHook(start, reinterpret_cast<void*>(&h_StartFrame),
                                   reinterpret_cast<void**>(&o_StartFrame));
  if (status == MH_OK) status = MH_EnableHook(start);
  if (status != MH_OK) {
    ModLog("hooks        : LH3DRender::StartFrame for the intro FAILED");
    return;
  }
  status = MH_CreateHook(run, reinterpret_cast<void*>(&h_RunAlexFx),
                         reinterpret_cast<void**>(&o_RunAlexFx));
  if (status == MH_OK) status = MH_EnableHook(run);
  ModLog("hooks        : RunAlexFX at 0x%08X %s", kRunAlexFx,
         status == MH_OK ? "ok" : "FAILED");
  if (status != MH_OK) {
    // The StartFrame hook does nothing without a pass, so it can stay.
    return;
  }
  g_installed = true;
  ModLog("intro        : on. intro_motion=%d: %s. The plane of the logo is "
         "%.2f m from the head at the rest pose (%.2f m for the model of "
         "intro_motion=2), the clock of the effect runs at %d percent, "
         "intro_eye_trails=%d, intro_text_place=%d, intro_push_once=%d, "
         "controller %s, intro_hand_push_pct=%d, intro_hand_point=%d",
         g_motion, MotionText(g_motion), g_distance_m, g_model_distance_m,
         g_speed_pct, g_eye_trails ? 1 : 0, g_text_place ? 1 : 0,
         g_push_once ? 1 : 0, g_controller ? "on" : "off", g_hand_push_pct,
         g_hand_point ? 1 : 0);
  if (g_test_keys) {
    ModLog("intro        : test_keys=1: while the logo plays in stereo, Shift "
           "and Page Up step intro_motion, Shift and Page Down switch "
           "intro_eye_trails, Shift and Delete switch intro_text_place, Shift "
           "and Home switch the push of the hand, and Shift and End switch "
           "intro_hand_point. These keys do not end the logo");
  }
  if (g_motion_test_ms > 0) {
    ModLog("intro        : test key intro_motion_test_seconds: each %d s of "
           "the logo in stereo the mod steps intro_motion, as its test key "
           "does", g_motion_test_ms / 1000);
  }
  if (g_push_test_px != 0) {
    ModLog("intro        : test key intro_push_test_px: each frame of the "
           "logo in stereo gets a motion of the mouse of %d counts to the "
           "right that no hand made, with the pointer in the middle of the "
           "picture", g_push_test_px);
  }
}
