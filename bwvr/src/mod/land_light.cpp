// THE LIGHTS ON THE LAND IN THE SECOND EYE PASS (land_light_eyes,
// land_light_math.h, and docs/backend.md, "The lights on the land in the
// second eye pass").
//
// The game has a list of the lights on the land of one scene walk: 200
// lights of 0x34 bytes at 0x00FA2920, and their count at 0x00FA51C0.
// fn_0086CFF0 adds one light. GLandscape::Draw uses each light of the list
// (fn_005E5830, fn_0086D360), and ClearLight 0x005E57B0 empties the list at
// the end of a scene walk that drew the land (fn_0086D460). The code that
// adds a light:
//
//     fn_00837200        the white light of a lightning of a storm, from
//                        LH3DAtmos::Update3D (0x008357C9). Its map is 64 by
//                        64 points of 10 units: the land is white for 320
//                        units round the lightning.
//     PSysLightMaps::AddDrawing   the light maps of the particles, from
//                        GGame::Process3dEngine (0x0054DEAD), in each scene
//                        walk. A particle gives its map to an array when the
//                        game draws it (ParticleLightMap::DrawAt). The
//                        function adds the maps of the array and empties the
//                        array.
//     DanceLight::Process    the lights of a dance, from
//                        GroupBehaviour::Process and Dance::Process, not in
//                        the scene walk
//     GWeather::DrawClouds   a map of kind 2 at a storm cloud (0x008400C6)
//     fn_005E25C0        from GLandscape::Draw (fn_005E5830), in each scene
//                        walk
//
// The second eye pass skips LH3DAtmos::Update3D, because it is a step of the
// world (pass_guard.cpp). So the light of a lightning comes in the first eye
// pass only: the land is white in the left eye and not in the right eye. A
// light from code before the scene walks has the same fault, and so has a
// light map of a particle when the array is empty in the second eye pass.
//
// With land_light_eyes=1 the mod holds a copy of each such light of the
// first eye pass, and it adds the copies to the list again in the second eye
// pass:
//  - the lights from before the passes, at the start of the pass,
//  - the light maps of the particles, after PSysLightMaps::AddDrawing, when
//    that call of the second pass added no light,
//  - the lights of the step of the atmosphere, where the pass skips the step.
// No step runs a second time, so nothing moves two times. With
// land_light_eyes=0 the second eye pass does not get those lights.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "debug_menu.h"
#include "land_light_math.h"

extern void ModLog(const char* format, ...);
// controllers.cpp: a pulse on the hands of the mask (bit 0 is the hand that
// does not point), and the frame count.
void ControllersGrabPulse(int hands, float amplitude, float seconds);
uint64_t ControllersFrameCount();

namespace {

const uintptr_t kLightList = 0x00FA2920;
const uintptr_t kLightCount = 0x00FA51C0;
// Adds one light to the list. The place is in ecx and the map is in edx, and
// the function removes its five other arguments from the stack (ret 0x14):
// the size of the map in points, 1 for a place that is the middle of the
// map, the strength from 0 to 1, the kind (1 for a light, 2 for the storm
// cloud), and 0.
const uintptr_t kAddLight = 0x0086CFF0;
typedef void(__fastcall* AddLightFn)(const float* place, const uint8_t* map,
                                     int size, int middle, float strength,
                                     int kind, int zero);
// PSysLightMaps::AddDrawing: no arguments and a plain return.
const uintptr_t kParticleMaps = 0x006CA6E0;
typedef void(__cdecl* VoidFn)();
VoidFn o_ParticleMaps = nullptr;
// The place of the Hand in the world, three floats.
const uintptr_t kHandWorldPosition = 0x00C5E680;

// The test light: 150 frames of each 500 frames with two eye passes.
const int kTestPeriod = 500;
const int kTestOn = 150;
const int kMaxLogs = 200;

bool g_loaded = false;
bool g_ready = false;       // the list of the game can be read and written
int g_both_eyes = 1;        // land_light_eyes
bool g_test_keys = false;
bool g_test = false;        // land_light_test
int g_eye = -1;

land_light::Held g_before;     // the lights from before the two passes
land_light::Held g_step;       // the lights of the step of the atmosphere
land_light::Held g_particles;  // the light maps of the particles
int g_step_before = -1;     // the count of the list before that step
int g_after_passes = -1;    // the count of the list after the last pass

// The lights of this frame: the copies of each group, and the lights that
// the second pass got again.
int g_frame_step = 0, g_frame_before = 0, g_frame_particles = 0;
int g_frame_given = 0;
// False in a frame with one scene walk for both eyes: no second pass.
bool g_frame_second = false;
// A run of frames in which the second pass needs copies, for one line in
// the log.
bool g_run = false;
uint64_t g_run_first = 0, g_run_last = 0, g_run_frames = 0;
int g_run_most_step = 0, g_run_most_before = 0, g_run_most_particles = 0;
uint64_t g_run_given = 0;
int g_logs = 0;

// The sums of a window, for the report.
uint64_t g_frames_with = 0, g_held_step = 0, g_held_before = 0, g_given = 0;
uint64_t g_missed_particles = 0;
// The lights that PSysLightMaps::AddDrawing added itself in each pass.
uint64_t g_particles_first = 0, g_particles_second = 0;

uint8_t g_test_map[land_light::kTestMapSize * land_light::kTestMapSize * 3];
uint64_t g_test_frame = 0;
bool g_test_on = false;

int* Count() { return reinterpret_cast<int*>(kLightCount); }
uint8_t* List() { return reinterpret_cast<uint8_t*>(kLightList); }

void Load() {
  if (g_loaded) return;
  g_loaded = true;
  // Fixed addresses of the game, so one test is enough.
  g_ready = !IsBadWritePtr(Count(), sizeof(int)) &&
            !IsBadWritePtr(List(), land_light::kCapacity *
                                       land_light::kLightBytes);
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash != nullptr && strlen(path) + 9 < MAX_PATH) {
    slash[1] = 0;
    strcat_s(path, MAX_PATH, "bwvr.ini");
    g_both_eyes =
        GetPrivateProfileIntA("bwvr", "land_light_eyes", 1, path) != 0 ? 1 : 0;
    g_test_keys = GetPrivateProfileIntA("bwvr", "test_keys", 0, path) != 0;
    g_test = GetPrivateProfileIntA("bwvr", "land_light_test", 0, path) != 0;
  }
  ModLog("land light   : land_light_eyes=%d. A light on the land that comes "
         "one time in a frame (the white light of a lightning, a light from "
         "before the eye passes) shows in %s%s", g_both_eyes,
         g_both_eyes != 0 ? "both eyes" : "the left eye only",
         g_ready ? "" : ". The list of the lights of the game cannot be "
                        "read, so the mod does nothing");
  if (g_test) {
    land_light::FillTestMap(g_test_map);
    ModLog("land light   : land_light_test=1. The step of the atmosphere of "
           "the first eye pass adds the white light of a lightning at the "
           "Hand, at full strength, for %d frames of each %d frames",
           kTestOn, kTestPeriod);
  }
}

// One line for a run of frames that ended.
void EndRun() {
  if (!g_run) return;
  g_run = false;
  if (g_logs >= kMaxLogs) return;
  ++g_logs;
  ModLog("land light   : from frame %llu to frame %llu (%llu frames) the "
         "first eye pass had lights on the land that the second eye pass "
         "does not add itself: at most %d from the step of the atmosphere, "
         "%d from before the eye passes and %d light maps of particles. The "
         "second eye pass got %llu lights again (land_light_eyes=%d)",
         g_run_first, g_run_last, g_run_frames, g_run_most_step,
         g_run_most_before, g_run_most_particles, g_run_given, g_both_eyes);
}

// The end of the two passes of a frame: the run of frames with such lights.
void CloseFrame() {
  if (!g_frame_second ||
      (g_frame_step == 0 && g_frame_before == 0 && g_frame_particles == 0)) {
    EndRun();
    return;
  }
  const uint64_t frame = ControllersFrameCount();
  if (!g_run) {
    g_run = true;
    g_run_first = frame;
    g_run_frames = 0;
    g_run_most_step = g_run_most_before = g_run_most_particles = 0;
    g_run_given = 0;
  }
  g_run_last = frame;
  ++g_run_frames;
  if (g_frame_step > g_run_most_step) g_run_most_step = g_frame_step;
  if (g_frame_before > g_run_most_before) g_run_most_before = g_frame_before;
  if (g_frame_particles > g_run_most_particles) {
    g_run_most_particles = g_frame_particles;
  }
  g_run_given += static_cast<uint64_t>(g_frame_given);
  ++g_frames_with;
  g_held_step += static_cast<uint64_t>(g_frame_step);
  g_held_before += static_cast<uint64_t>(g_frame_before);
  g_missed_particles += static_cast<uint64_t>(g_frame_particles);
  g_given += static_cast<uint64_t>(g_frame_given);
}

}  // namespace

// The switch of the debug page (debug_menu.h). The list of the lights of the
// game must be ready, as for the key.
void LandLightEyesChange(const char* by) {
  Load();
  if (!g_ready) return;
  EndRun();
  g_both_eyes = g_both_eyes != 0 ? 0 : 1;
  ControllersGrabPulse(1, 0.5f, g_both_eyes != 0 ? 0.30f : 0.08f);
  ModLog("land light   : %s: land_light_eyes is %d now. A light on the land "
         "that comes one time in a frame shows in %s, frame %llu", by,
         g_both_eyes, g_both_eyes != 0 ? "both eyes" : "the left eye only",
         ControllersFrameCount());
}

int LandLightEyesValue() {
  Load();
  if (!g_ready) return kDebugCannot;
  return g_both_eyes;
}

namespace {

// Ctrl, Shift and the Menu key: the lights show in both eyes, or in the left
// eye only (land_light_eyes). A long pulse for both eyes and a short pulse
// for the left eye.
void PollKey() {
  if (!g_test_keys) return;
  static bool before = false;
  const bool down = (GetAsyncKeyState(VK_APPS) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool pressed = down && !before;
  before = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  LandLightEyesChange("Ctrl, Shift and the Menu key");
}

// land_light_test: the light of a lightning at its start, as fn_00837200
// adds it, at the place of the Hand.
void TestLight() {
  if (!g_test) return;
  const bool on =
      land_light::TestLightOn(g_test_frame, kTestPeriod, kTestOn);
  ++g_test_frame;
  const float* hand = reinterpret_cast<const float*>(kHandWorldPosition);
  static const bool readable = !IsBadReadPtr(hand, sizeof(float) * 3);
  if (!readable) return;
  if (on != g_test_on) {
    g_test_on = on;
    if (g_logs < kMaxLogs) {
      ++g_logs;
      ModLog("land light   : the test light is %s at %.1f %.1f %.1f, frame "
             "%llu", on ? "on" : "off", hand[0], hand[1], hand[2],
             ControllersFrameCount());
    }
  }
  if (!on) return;
  const float place[3] = {hand[0], hand[1], hand[2]};
  reinterpret_cast<AddLightFn>(kAddLight)(place, g_test_map,
                                          land_light::kTestMapSize, 1, 1.0f,
                                          1, 0);
}

// THE LIGHT MAPS OF THE PARTICLES. The call of the first eye pass adds the
// maps of the array and empties the array. The call of the second eye pass
// adds the maps that came to the array after that. When it adds none, the
// second eye gets the copies of the first eye.
void __cdecl h_ParticleMaps() {
  if (!g_ready || g_eye < 0) {
    o_ParticleMaps();
    return;
  }
  const int before = *Count();
  o_ParticleMaps();
  const int own = *Count() - before;
  if (g_eye == 0) {
    land_light::Hold(&g_particles, List(), before, *Count());
    if (own > 0) g_particles_first += static_cast<uint64_t>(own);
    return;
  }
  if (own > 0) g_particles_second += static_cast<uint64_t>(own);
  if (land_light::SecondPassTakesCopies(own)) {
    g_frame_particles += g_particles.count;
    if (g_both_eyes != 0) {
      g_frame_given += land_light::GiveBack(g_particles, List(), Count());
    }
  }
  land_light::Clear(&g_particles);
}

}  // namespace

// From PassGuardInstall.
void LandLightInstall() {
  Load();
  if (!g_ready) return;
  void* target = reinterpret_cast<void*>(kParticleMaps);
  const bool ok =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_ParticleMaps),
                    reinterpret_cast<void**>(&o_ParticleMaps)) == MH_OK &&
      MH_EnableHook(target) == MH_OK;
  if (!ok) {
    ModLog("land light   : PSysLightMaps::AddDrawing at 0x%08X FAILED to "
           "hook. The light maps of the particles stay as the game adds "
           "them", static_cast<unsigned>(kParticleMaps));
  }
}

// From PassGuardSetEye: the start of each eye pass, and -1 after the two
// passes.
void LandLightEye(int eye) {
  Load();
  g_eye = eye;
  if (!g_ready) return;
  const int count = *Count();
  if (eye == 0) {
    PollKey();
    land_light::Clear(&g_before);
    land_light::Clear(&g_step);
    land_light::Clear(&g_particles);
    g_step_before = -1;
    g_frame_step = g_frame_before = g_frame_particles = g_frame_given = 0;
    g_frame_second = false;
    // The lights that came after the last pass are from code before the
    // passes. The lights that were there after the last pass are not.
    if (g_after_passes >= 0 && count >= g_after_passes) {
      g_frame_before =
          land_light::Hold(&g_before, List(), g_after_passes, count);
    }
    return;
  }
  if (eye == 1) {
    g_frame_second = true;
    if (g_both_eyes != 0) {
      g_frame_given += land_light::GiveBack(g_before, List(), Count());
    }
    return;
  }
  g_after_passes = count >= 0 && count <= land_light::kCapacity ? count : -1;
  CloseFrame();
}

// Round the step of the atmosphere, which the second eye pass skips, in the
// first eye pass.
void LandLightStepBegin() {
  g_step_before = g_ready && g_eye == 0 ? *Count() : -1;
}

void LandLightStepEnd() {
  if (g_step_before < 0 || g_eye != 0) return;
  TestLight();
  g_frame_step +=
      land_light::Hold(&g_step, List(), g_step_before, *Count());
  g_step_before = -1;
}

// Where the second eye pass skips that step.
void LandLightStepSkipped() {
  if (!g_ready || g_eye != 1 || g_both_eyes == 0) return;
  g_frame_given += land_light::GiveBack(g_step, List(), Count());
  // A second call of the step in this pass adds nothing.
  land_light::Clear(&g_step);
}

void LandLightReport() {
  if (g_particles_first != 0 || g_particles_second != 0) {
    ModLog("land light   : the particles added %llu light maps in first eye "
           "passes and %llu in second eye passes this window",
           g_particles_first, g_particles_second);
  }
  g_particles_first = g_particles_second = 0;
  if (g_frames_with == 0) return;
  ModLog("land light   : %llu frames this window had lights on the land that "
         "the second eye pass does not add itself: %llu lights from the step "
         "of the atmosphere, %llu from before the eye passes and %llu light "
         "maps of particles. The second eye pass got %llu lights again "
         "(land_light_eyes=%d)", g_frames_with, g_held_step, g_held_before,
         g_missed_particles, g_given, g_both_eyes);
  g_frames_with = g_held_step = g_held_before = g_given = 0;
  g_missed_particles = 0;
}
