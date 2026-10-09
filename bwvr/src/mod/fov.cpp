// The field of view of the game.
//
// The mod submits a projection layer per eye, and a projection layer is a
// promise: this image was rendered from this pose, with this frustum. The
// game must then render the headset's frustum rather than its own, or the
// world shears when the head moves.
//
// A headset wants about 100 degrees per eye. The game renders far less. So
// the mod widens the game's view, and that costs geometry.
//
// THIS IS WHY THAT MATTERS. The scene walk was profiled at the heaviest scene
// found so far, a glowing portal with particles and moving trees:
//
//     1839 draw calls, 126,565 vertices, scene x2 9.79 ms of an 11.1 ms frame
//     38 percent driver, 62 percent game code
//     the wait for the headset was 0.02 ms, so there is no room left
//
// The cost tracks draw calls, not pixels, and a wider view means more of the
// island is in front of the camera. If a headset field of view doubles the
// draw calls, the scene must be walked more cheaply.
//
// LH3DTech::ChangeFov is the right lever rather than the projection matrix,
// because the struct it writes holds both:
//
//     InvHalfTanFovY, InvHalfTanFovX      the projection
//     CosHalfFovSqr, CosHalfFov           the culling cone
//
// So widening the view widens the culling with it. That matters: the project
// plan lists culling against a narrow frustum as a certainty, because a
// wrapper cannot recover geometry the engine already threw away. Here the
// engine throws away less, which is the honest fix rather than a patch over
// the symptom.
//
// The decompilation names the function but has not recovered any caller, so
// how often the game sets the view, and to what, is measured here rather than
// assumed.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "MinHook.h"

extern void ModLog(const char* format, ...);

namespace {

// BW1W120 008195b0 LH3DTech::ChangeFov(float).
//
// __stdcall, NOT __cdecl. The decompilation calls it a static, which says
// nothing about who cleans the stack, and this binary uses __stdcall for free
// functions. The function ends with:
//
//     C2 04 00     ret 4
//
// A __cdecl replacement returns without popping the argument, so every call
// leaves the caller's stack 4 bytes out and it returns into garbage. That
// crashed the game after the loading screen, as an access violation at an
// address in no loaded module.
//
// The rule this leaves behind: before hooking anything that takes a stack
// argument, find its ret. C3 is caller cleans, C2 n is callee cleans. Member
// functions with no arguments are safe either way, which is why every other
// hook in this mod survived without the check.
const uintptr_t kChangeFov = 0x008195B0;

typedef void(__stdcall* ChangeFovFn)(float fov);
ChangeFovFn o_ChangeFov = nullptr;

bool g_installed = false;

// What the mod multiplies the game's own value by. 1.0 changes nothing.
float g_scale = 1.0f;
// A fixed horizontal angle in radians, which overrides the multiplier. Zero
// means leave the game's own value alone.
float g_absolute = 0.0f;

// The last value the game asked for, and the last one actually used.
float g_asked = 0.0f;
float g_used = 0.0f;

uint64_t g_calls = 0;
int g_logged = 0;
const int kMaxLogged = 12;

void __stdcall h_ChangeFov(float fov) {
  ++g_calls;
  g_asked = fov;
  // An absolute target wins over the multiplier. The mod needs the game to
  // render the headset's frustum, not a proportion of its own.
  const float wanted = g_absolute > 0.0f ? g_absolute : fov * g_scale;
  g_used = wanted;
  // Measured: 1.2217 radians, which is 70.0 degrees exactly, asked for once as
  // the world loads. Logging stops after the first few calls, because this
  // runs inside the engine's own floating point work and formatting a float is
  // not free.
  if (g_logged < kMaxLogged) {
    ++g_logged;
    ModLog("fov          : the game asked for %.4f rad (%.1f deg), using %.4f",
           fov, fov * 57.29578f, wanted);
  }
  o_ChangeFov(wanted);
}

}  // namespace

void FovSetScale(float scale) {
  if (scale < 0.25f) scale = 0.25f;
  if (scale > 4.0f) scale = 4.0f;
  g_scale = scale;
  // Ask for the last value again, so the change takes effect at once even if
  // the game only sets the view when something happens.
  if (o_ChangeFov != nullptr && g_asked > 0.0f) {
    g_used = g_asked * g_scale;
    o_ChangeFov(g_used);
  }
  ModLog("fov          : scale %.2f, so %.4f becomes %.4f", g_scale, g_asked,
         g_used);
}

float FovScale() { return g_scale; }

// Render exactly this horizontal angle, whatever the game asks for. The
// vertical follows from the render aspect, so a square render gives the same
// angle both ways, which is what a headset wants.
void FovSetAbsolute(float radians) {
  g_absolute = radians;
  if (o_ChangeFov != nullptr && radians > 0.0f) {
    g_used = radians;
    o_ChangeFov(radians);
  }
  ModLog("fov          : absolute %.1f degrees", radians * 57.29578f);
}

float FovAbsolute() { return g_absolute; }

void FovInstall() {
  if (g_installed) return;
  void* target = reinterpret_cast<void*>(kChangeFov);
  MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void*>(&h_ChangeFov),
                                   reinterpret_cast<void**>(&o_ChangeFov));
  if (status == MH_OK) status = MH_EnableHook(target);
  g_installed = status == MH_OK;
  ModLog("hooks        : LH3DTech::ChangeFov at 0x%08X %s", kChangeFov,
         g_installed ? "ok" : "FAILED");
}

// THE SWEEP.
//
// A measurement by hand does not answer the question. At one fixed scale the
// draw calls ranged from 336 to 585, because the camera had moved between
// windows. The scene was drowning the thing being measured.
//
// So the mod runs the sweep itself. One step per report window, which is the
// same 450 frames the profile counts over, so each scale gets exactly one
// clean window and the two lines always describe the same frames.
//
// The last step returns to 1.0 on purpose. If that window matches the first
// one, the scene did not change during the sweep and the middle windows can
// be compared. If it does not match, the run is thrown away rather than
// argued about.
const float kSweep[] = {1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f, 1.0f};
const int kSweepSteps = sizeof(kSweep) / sizeof(kSweep[0]);
int g_sweep_step = -1;  // -1 is not sweeping

void FovSweepStart() {
  if (!g_installed) {
    ModLog("fov          : no hook, so no sweep");
    return;
  }
  g_sweep_step = 0;
  FovSetScale(kSweep[0]);
  ModLog("fov          : SWEEP started, %d steps of 450 frames. Hold the view "
         "still and do not move the camera.", kSweepSteps);
}

bool FovSweeping() { return g_sweep_step >= 0; }

// Windows to wait before starting on its own. A headless run has nobody to
// press a key, and the first window covers the loading screen.
int g_sweep_after = 0;

void FovSweepAfter(int windows) { g_sweep_after = windows; }

// How often the game sets the view, which says whether the mod can set it once
// or has to keep asking. Also drives the sweep, so a step lands exactly on a
// window boundary.
void FovReport() {
  if (!g_installed) return;
  ModLog("fov          : %llu calls, asked %.4f, used %.4f, scale %.2f, "
         "%.1f degrees", g_calls, g_asked, g_used, g_scale,
         g_used * 57.29578f);
  g_calls = 0;

  if (g_sweep_step < 0) {
    if (g_sweep_after > 0 && --g_sweep_after == 0) FovSweepStart();
    return;
  }
  ++g_sweep_step;
  if (g_sweep_step >= kSweepSteps) {
    g_sweep_step = -1;
    ModLog("fov          : SWEEP done. Compare the first and last windows: if "
           "they differ, the scene moved and the run is void.");
    return;
  }
  FovSetScale(kSweep[g_sweep_step]);
}
