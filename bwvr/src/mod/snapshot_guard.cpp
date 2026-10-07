// Stop the save game thumbnail from reading off the end of the screen.
//
// Entering the world with a square render killed the game with an access
// violation. It was blamed on the wide field of view, because the two arrived
// together. The view was innocent: a run at 2560x1440 swept the view to 105
// degrees for 450 frames and ended clean.
//
// The real fault is in ScriptedScreenShot::Process, which makes the small
// picture a save game shows. It locks the primary surface and then:
//
//     vstep = LHScreen::height * (1/128)
//     hstep = LHScreen::width  * (1/256)
//     px = ((uint32*)desc.lpSurface)[ ftol(r*vstep) * (desc.lPitch/4)
//                                     + ftol(c*hstep) ]
//
// The base pointer, the pitch and the pixel format come from the locked
// descriptor. The EXTENT comes from two globals. It never reads dwWidth or
// dwHeight out of the descriptor and it clamps nothing, so if the configured
// mode is wider than the surface it walks off the end of the row.
//
// LHScreen::ChangeMode writes the requested width and height into the object
// before it knows whether the mode was accepted, and in windowed mode it
// creates the primary surface with DDSCAPS_PRIMARYSURFACE and no dimensions,
// so the surface is whatever the wrapper gives it while width and height keep
// the configured values. A square render in a window is exactly that case.
//
// The evidence that it is a row overrun and not a column one: all four crashes
// landed on the inner column read and never on the row base read one loop out.
// A height overrun would fault at the row base.
//
// Nothing in VR needs this picture. The mod already stops the copy to the
// monitor, and a save thumbnail is not worth a crash, so the call is skipped
// while VR is running. The request flag is cleared as the original does, so
// the game does not think a snapshot is still pending.
//
// 0x00821750 takes no arguments and ends C3, so it is safe either way, and it
// is named in the decompilation as ScriptedScreenShot::Process. The 400x300
// sibling at 0x00821370 has the same defect but is only reached from here, so
// one hook covers both.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "MinHook.h"

extern void ModLog(const char* format, ...);

namespace {

// BW1W120 00821750 ScriptedScreenShot::Process(void)
const uintptr_t kScriptedScreenShotProcess = 0x00821750;
// The flag the original clears on entry, so a skipped request does not stay
// pending for ever.
const uintptr_t kSnapshotRequested = 0x00EB99B4;

typedef void(__cdecl* ProcessFn)();
ProcessFn o_Process = nullptr;

bool g_installed = false;
bool g_skip = false;
uint64_t g_skipped = 0;

void __cdecl h_Process() {
  if (!g_skip) {
    o_Process();
    return;
  }
  uint32_t* requested = reinterpret_cast<uint32_t*>(kSnapshotRequested);
  if (!IsBadWritePtr(requested, sizeof(uint32_t)) && *requested != 0) {
    *requested = 0;
    ++g_skipped;
  }
}

}  // namespace

void SnapshotGuardSetSkip(bool on) {
  g_skip = on;
  ModLog("snapshot     : the save game thumbnail is %s",
         on ? "skipped, because it reads past the end of a square window"
            : "taken as usual");
}

void SnapshotGuardInstall() {
  if (g_installed) return;
  void* target = reinterpret_cast<void*>(kScriptedScreenShotProcess);
  MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void*>(&h_Process),
                                   reinterpret_cast<void**>(&o_Process));
  if (status == MH_OK) status = MH_EnableHook(target);
  g_installed = status == MH_OK;
  ModLog("hooks        : ScriptedScreenShot::Process at 0x%08X %s",
         kScriptedScreenShotProcess, g_installed ? "ok" : "FAILED");
}

void SnapshotGuardReport() {
  if (!g_installed || g_skipped == 0) return;
  ModLog("snapshot     : %llu thumbnail requests skipped", g_skipped);
  g_skipped = 0;
}
