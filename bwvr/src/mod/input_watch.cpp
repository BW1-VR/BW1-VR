// What the camera is actually being driven with.
//
// The drag goes heavy on some runs and not others, at a steady 89.8 fps with
// 3.17 ms of idle time and nothing skipped by the runtime. So it is not frame
// pacing, and the explanation has to be in what the camera is fed rather than
// in how often it is fed.
//
// A camera drag is fed two things: how far the mouse moved, and how much time
// the engine thinks passed. This watches both, because "heavy, then floats"
// is what either one looks like when it arrives in lumps.
//
//   LHMouse::UpdateDeltaPos   0x007E4810, called once per frame from
//                             GGame::ProcessGraphicsEngine, right before
//                             ControlMap::MouseDelta is taken from it.
//
//   LH3DTech::g_game_time_inc 0x00EA9EC0, the per frame time step, 157
//                             readers. GGame::Loop builds it from GetTickCount,
//                             which steps in 15 to 16 ms whatever the system
//                             timer is set to. At 11.1 ms per frame that means
//                             many frames get a step of zero and then one gets
//                             15 or 16. Measured, not assumed: this prints the
//                             distribution.
//
//   LH3DTech::g_delta_time    0x00C38134, the other time global, 80 readers.
//
// The point is comparison. Run it while the drag feels wrong, run it while it
// feels right, and the difference between the two reports is the fault.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdint.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);
// pointer_map.cpp: rewrite the pointer once the game has settled it.
void PointerMapApply();
// intro_vr.cpp: the Lionhead logo in stereo calls this function one time in
// each eye pass, between the sky and the orbs. IntroMouseCall places the
// camera of the orbs there. It is true when the call must not read the
// mouse: the second eye pass must not move an orb. IntroMouseRead is a test
// of a desktop run.
bool IntroMouseCall(uintptr_t caller);
void IntroMouseRead(uintptr_t caller);

namespace {

// A member function with no arguments, so this in ecx and ret 0. Safe as
// __fastcall either way.
const uintptr_t kMouseUpdateDeltaPos = 0x007E4810;

typedef void(__fastcall* MouseUpdateFn)(void* self, void* unused);
MouseUpdateFn o_MouseUpdateDeltaPos = nullptr;

bool g_installed = false;

// The mouse is watched through the object it updates. LHMouse::AccumDelta is
// read straight after the call, so the two fields that move are found by
// watching the object rather than by guessing the layout: any field that
// changes across the call and is small and signed is a candidate. To keep
// this honest the raw first 16 dwords are dumped once.
bool g_dumped_layout = false;
uint32_t g_before[16];

// Distribution of the frame time step.
uint64_t g_frames = 0;
uint64_t g_zero_steps = 0;
uint32_t g_step_min = 0xFFFFFFFF;
uint32_t g_step_max = 0;
uint64_t g_step_total = 0;

// How lumpy the mouse is. Counted rather than averaged, because an average
// hides exactly the thing being looked for.
uint64_t g_mouse_calls = 0;
uint64_t g_mouse_zero = 0;
int32_t g_mouse_worst = 0;
int64_t g_mouse_total = 0;

int32_t g_last_x = 0;
int32_t g_last_y = 0;
uintptr_t g_mouse_object = 0;
int g_x_offset = -1;
int g_y_offset = -1;

void __fastcall h_MouseUpdateDeltaPos(void* self, void* unused) {
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  if (IntroMouseCall(caller)) return;
  const uint32_t* words = reinterpret_cast<const uint32_t*>(self);
  const bool readable =
      self != nullptr && !IsBadReadPtr(self, sizeof(uint32_t) * 16);
  if (readable) {
    for (int i = 0; i < 16; ++i) g_before[i] = words[i];
  }

  o_MouseUpdateDeltaPos(self, unused);

  // The one point in the frame where the pointer is settled and nothing has
  // read it yet. ControlMap::MouseDelta is taken from the object on the very
  // next line of the game's own code.
  PointerMapApply();
  IntroMouseRead(caller);

  if (!readable) return;
  g_mouse_object = reinterpret_cast<uintptr_t>(self);

  // Find the two fields that carry the accumulated movement, once. They are
  // the ones that change across the call and stay small.
  if (g_x_offset < 0) {
    for (int i = 0; i < 16; ++i) {
      if (words[i] == g_before[i]) continue;
      const int32_t v = static_cast<int32_t>(words[i]);
      if (v > 2000 || v < -2000) continue;
      if (g_x_offset < 0) {
        g_x_offset = i;
      } else if (g_y_offset < 0) {
        g_y_offset = i;
      }
    }
    if (g_x_offset >= 0 && !g_dumped_layout) {
      g_dumped_layout = true;
      ModLog("input watch  : LHMouse at 0x%08X, moving fields at +0x%02X and "
             "+0x%02X", g_mouse_object, g_x_offset * 4,
             g_y_offset >= 0 ? g_y_offset * 4 : 0);
    }
  }

  if (g_x_offset >= 0) {
    const int32_t x = static_cast<int32_t>(words[g_x_offset]);
    const int32_t y =
        g_y_offset >= 0 ? static_cast<int32_t>(words[g_y_offset]) : 0;
    const int32_t dx = x - g_last_x;
    const int32_t dy = y - g_last_y;
    g_last_x = x;
    g_last_y = y;
    const int32_t moved =
        (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    ++g_mouse_calls;
    if (moved == 0) ++g_mouse_zero;
    if (moved > g_mouse_worst) g_mouse_worst = moved;
    g_mouse_total += moved;
  }
}

}  // namespace

// WHERE THE GAME THINKS THE POINTER IS, AND WHERE WINDOWS THINKS IT IS.
//
// This decides how the desktop window can be treated. If the game's pointer is
// mapped from the window's client area, then the window has to be reachable
// and the mapping scales with its size. If the game accumulates raw movement
// instead, the window's size and position do not matter at all and it can be
// made small or moved out of the way.
//
// 0x00E852C0 and 0x00E852C4 are the pointer globals, read by
// CHand::PrepareForDrawing at 0x0046C6C5.
const uintptr_t kMouseX = 0x00E852C0;
const uintptr_t kMouseY = 0x00E852C4;
int g_pointer_log = 0;

void PointerReport() {
  if (g_pointer_log <= 0) return;
  --g_pointer_log;
  const int32_t* gx = reinterpret_cast<const int32_t*>(kMouseX);
  const int32_t* gy = reinterpret_cast<const int32_t*>(kMouseY);
  if (IsBadReadPtr(gx, sizeof(int32_t)) || IsBadReadPtr(gy, sizeof(int32_t))) {
    return;
  }
  POINT screen = {0, 0};
  GetCursorPos(&screen);
  POINT client = screen;
  HWND window = GetActiveWindow();
  RECT rect = {0, 0, 0, 0};
  if (window != nullptr) {
    ScreenToClient(window, &client);
    GetClientRect(window, &rect);
  }
  ModLog("pointer      : game %5d %5d   windows client %5d %5d of %d x %d",
         *gx, *gy, client.x, client.y, rect.right, rect.bottom);
}

// Called once a frame, from the stereo hook, before the passes.
void InputWatchFrame() {
  if (!g_installed) return;
  const uint32_t* step =
      reinterpret_cast<const uint32_t*>(bw::tech::kGameTimeInc);
  if (IsBadReadPtr(step, sizeof(uint32_t))) return;
  const uint32_t value = *step;
  ++g_frames;
  if (value == 0) ++g_zero_steps;
  if (value < g_step_min) g_step_min = value;
  if (value > g_step_max) g_step_max = value;
  g_step_total += value;
}

void InputWatchInstall() {
  if (g_installed) return;
  void* target = reinterpret_cast<void*>(kMouseUpdateDeltaPos);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_MouseUpdateDeltaPos),
                    reinterpret_cast<void**>(&o_MouseUpdateDeltaPos));
  if (status == MH_OK) status = MH_EnableHook(target);
  g_installed = status == MH_OK;
  ModLog("hooks        : LHMouse::UpdateDeltaPos at 0x%08X %s",
         kMouseUpdateDeltaPos, g_installed ? "ok" : "FAILED");
}

void InputWatchAskPointer(int count) { g_pointer_log = count; }

void InputWatchReport() {
  PointerReport();
  if (!g_installed) return;
  if (g_frames > 0) {
    ModLog("time step    : mean %.2f ms, min %u, max %u, %llu of %llu frames "
           "got zero",
           static_cast<double>(g_step_total) / static_cast<double>(g_frames),
           g_step_min == 0xFFFFFFFF ? 0 : g_step_min, g_step_max, g_zero_steps,
           g_frames);
  }
  if (g_mouse_calls > 0) {
    ModLog("mouse        : mean %.2f px per frame, worst %d, %llu of %llu "
           "frames got nothing",
           static_cast<double>(g_mouse_total) /
               static_cast<double>(g_mouse_calls),
           g_mouse_worst, g_mouse_zero, g_mouse_calls);
  }
  g_frames = 0;
  g_zero_steps = 0;
  g_step_min = 0xFFFFFFFF;
  g_step_max = 0;
  g_step_total = 0;
  g_mouse_calls = 0;
  g_mouse_zero = 0;
  g_mouse_worst = 0;
  g_mouse_total = 0;
}
