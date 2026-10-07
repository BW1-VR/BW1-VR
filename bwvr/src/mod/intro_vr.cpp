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
// WHAT "STOPPED" MEANS. RunAlexFX reads LH3DTech::g_delta_time twice: at
// 0x005F9050 for the effect clock, and at 0x005F96F0 for the spring, the
// damping and the push. LH3DRender::StartFrame writes g_delta_time in
// between, from the tick count, so setting it before the call is not enough.
// h_StartFrame writes it again when StartFrame returns, for the call at
// 0x005F933E only. The first eye gets the real frame time in both places. The
// second eye gets zero in both places, so nothing moves.
//
// Three more values change on each call and are not scaled by time: the
// smoothed mouse motion at 0x00D37CB0 and 0x00D37CB4, the push strength at
// 0x00D34A8C, and the C runtime random seed (each sprite gets a random angle
// from rand). The second eye starts from the values the first eye ended with,
// and they go back after it, so the game sees one step for each frame.
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
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "../vr/xr_screen.h"

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
bool EyeCaptureWanted(uint64_t stereo_frame);
void EyeCaptureArmNow(uint64_t stereo_frame);
void EyeCaptureSave(uint64_t stereo_frame, int eye, const void* pixels,
                    int pitch, uint32_t width, uint32_t height);
// controllers.cpp
void ControllersFrame();

namespace {

const uintptr_t kRunAlexFx = 0x005F89F0;      // __cdecl void (int mode)
const uintptr_t kUpdateCamera = 0x00819920;   // LH3DTech::UpdateCamera
const uintptr_t kStartFrameCall = 0x005F933E; // RunAlexFX calls StartFrame
const uintptr_t kFrameCameraCall = 0x005F934B;  // and UpdateCamera, every frame
// The intro has two other camera calls. One builds the effect (0x005F8A84).
// The head does not go on it.
const uintptr_t kBuildCameraCall = 0x005F8A84;
const uintptr_t kGetPtd = 0x007D232B;         // __cdecl, the C runtime data

// The effect's own state, in the game's data section.
const uintptr_t kDoneFlag = 0x00D37CC8;       // byte, the effect has ended
const uintptr_t kClock = 0x00D37CCC;          // float, the effect clock
const uintptr_t kMouseLagX = 0x00D37CB0;      // int, smoothed mouse motion
const uintptr_t kMouseLagY = 0x00D37CB4;
const uintptr_t kPushStrength = 0x00D34A8C;   // float
const uintptr_t kParticles = 0x00D34A98;      // pointer to 0x34 byte records
const int kSeedOffset = 0x14;                 // _holdrand in the runtime data

// Numbers for the still camera, in the units of the logo. Measured with
// intro_probe. See docs/intro.md.
const float kStillPosition[3] = {0.0f, 0.0f, 100.0f};
const float kStillFocus[3] = {0.0f, 0.0f, 0.0f};

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
bool g_still = true;           // intro_still: hold the camera still
float g_distance_m = 3.0f;     // intro_distance_cm: the logo plane, from the head
bool g_controller = true;      // intro_controller: the aim moves the pointer
bool g_probe = false;          // intro_probe: log the game's camera

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
      !CallsTarget(kStartFrameCall, bw::render::kStartFrame)) {
    ModLog("intro        : the calls inside RunAlexFX are not where expected, "
           "no hook");
    return false;
  }
  if (IsBadReadPtr(reinterpret_cast<void*>(kGetPtd), 1) ||
      IsBadWritePtr(reinterpret_cast<void*>(kClock), sizeof(float)) ||
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

// The scale of the room for the intro, from the distance of the camera to the
// logo. A logo of about 2.5 m at g_distance_m from the head.
void ChooseScale(const float position[3], const float focus[3]) {
  const float dx = focus[0] - position[0];
  const float dy = focus[1] - position[1];
  const float dz = focus[2] - position[2];
  const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
  if (!(distance > 1.0f) || !(g_distance_m > 0.1f)) return;
  g_scale = distance / g_distance_m;
}

void SwapScale() {
  if (g_scale_swapped || !(g_scale > 0.0f)) return;
  g_saved_scale = HeadPoseScale();
  HeadPoseSetScale(g_scale);
  g_scale_swapped = true;
  ModLog("intro        : the room is %.1f units per meter here, was %.1f",
         g_scale, g_saved_scale);
}

// One eye pass: run the effect once with the state of that eye.
void RunEye(int eye, uint32_t delta, uint32_t game_time_inc) {
  g_eye = eye;
  g_camera_seen = false;
  g_camera_built = false;
  g_eye_sent = false;
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
  if (g_scale > 0.0f) SwapScale();
  UpdateHead();
  HeadCameraIntroFrame();
  // The hands of this frame, for the pointer. Buttons stay off. See
  // IntroBlocksButtons.
  if (g_controller) ControllersFrame();

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
    const float strength = *reinterpret_cast<float*>(kPushStrength);
    if (seed != nullptr) *seed = seed_before;
    RunEye(1, 0, 0);
    if (seed != nullptr) *seed = seed_after;
    *reinterpret_cast<int32_t*>(kMouseLagX) = lag_x;
    *reinterpret_cast<int32_t*>(kMouseLagY) = lag_y;
    *reinterpret_cast<float*>(kPushStrength) = strength;
    missed1 = !g_camera_seen || !g_camera_built;
    if (log_particles) ProbeParticle("after the right eye", *Clock());
  }
  *GameTimeInc() = saved_inc;
  QueryPerformanceCounter(&t_calls1);

  if (g_probe && (g_pairs % 100) == 0) {
    const int32_t* pointer = reinterpret_cast<const int32_t*>(0x00E852C0);
    ModLog("intro        : pair %llu pointer at %d %d, buttons 0x%02X", g_pairs,
           pointer[0], pointer[1], *reinterpret_cast<const uint8_t*>(0x00E85304));
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
    if (ran) LogPair();
    RestoreScale();
    o_RunAlexFx(mode);
    if (ran) {
      HeadPoseRecenter();
      ModLog("intro        : ended after %llu stereo frames, %llu flat frames",
             g_pairs, g_flat_frames);
    }
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

// For head_camera.cpp: is this call the still camera of an eye pass?
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
  if (g_probe && g_eye == 0) {
    const int second = static_cast<int>(*Clock());
    if (second != g_clock_logged && g_probe_lines < 120) {
      g_clock_logged = second;
      ++g_probe_lines;
      ModLog("intro        : clock %.2f, the game asks for the camera at "
             "%.3f %.3f %.3f, looking at %.3f %.3f %.3f", *Clock(),
             position[0], position[1], position[2], focus[0], focus[1],
             focus[2]);
    }
  }
  if (g_still) {
    memcpy(out_position, kStillPosition, sizeof(float) * 3);
    memcpy(out_focus, kStillFocus, sizeof(float) * 3);
  } else {
    // The game's own camera. The scale of the room still comes from it.
    memcpy(out_position, position, sizeof(float) * 3);
    memcpy(out_focus, focus, sizeof(float) * 3);
    if (!(g_scale > 0.0f)) ChooseScale(position, focus);
  }
  return true;
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
  if (g_to_headset && GpuBridgeReady()) {
    void* texture = GpuBridgeCaptureEye(g_eye);
    if (texture != nullptr) {
      g_screen->SetEyeTexture(g_eye, texture);
      sent = true;
    }
  }
  const bool want = EyeCaptureWanted(kCaptureBase + g_pairs);
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

// For controllers.cpp: while the intro draws in stereo, a controller must not
// press a mouse button. Any click ends the logo (DoLogo tests both main
// buttons), and the trigger would end it at the first push.
bool IntroBlocksButtons() {
  return g_active_tick != 0 && GetTickCount() - g_active_tick < 400;
}

void IntroSetScreen(bwvr::XrScreen* screen) { g_screen = screen; }

void IntroSetOptions(bool on, bool still, int distance_cm, bool controller,
                     bool probe) {
  g_on = on;
  g_still = still;
  g_distance_m = distance_cm > 0 ? distance_cm * 0.01f : 3.0f;
  g_controller = controller;
  g_probe = probe;
  if (still) {
    ChooseScale(kStillPosition, kStillFocus);
  }
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
  ModLog("intro        : on, %s camera, logo plane %.2f m away, controller %s",
         g_still ? "still" : "the game's", g_distance_m,
         g_controller ? "on" : "off");
}
