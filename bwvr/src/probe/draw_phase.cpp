// Where in the frame does the drawing actually happen?
//
// This decides how stereo has to work, and it is worth one measurement rather
// than an argument.
//
// The engine projects every vertex on the CPU, so a second eye needs the
// geometry submitted a second time against a second camera matrix. There are
// two shapes that could take:
//
//   - If the scene draws while GGame::Process3dEngine walks it, then stereo
//     means running that walk twice. That function also updates physics,
//     particles and the atmosphere, so a second pass would double step the
//     simulation unless every update is neutralized.
//
//   - If the scene is registered with LH3DZSorter during the walk and only
//     drawn when LH3DRender::FinishFrame flushes the list, then stereo means
//     flushing the list twice. That is far cheaper and touches no simulation.
//
// LH3DZSorter::NewZObject takes an object, a draw callback and a depth, which
// looks like the second shape. Looks are not evidence. This counts
// LH3DRender::DrawTriangle calls in each phase of the frame and settles it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

extern void ProbeLog(const char* format, ...);

namespace {

// GGame::Process3dEngine is __thiscall with no arguments, so the object is in
// ecx. Declaring it __fastcall with an unused second argument is the standard
// way to reach that from C++.
typedef void(__fastcall* Process3dEngineFn)(void* self, void* unused);
Process3dEngineFn o_Process3dEngine = nullptr;

bool g_in_scene_walk = false;
bool g_in_finish_frame = false;

uint64_t g_calls_in_walk = 0;
uint64_t g_calls_in_finish = 0;
uint64_t g_calls_elsewhere = 0;
uint64_t g_verts_in_walk = 0;
uint64_t g_verts_in_finish = 0;
uint64_t g_verts_elsewhere = 0;
uint64_t g_walks = 0;

void __fastcall h_Process3dEngine(void* self, void* unused) {
  g_in_scene_walk = true;
  o_Process3dEngine(self, unused);
  g_in_scene_walk = false;
  ++g_walks;
}

}  // namespace

// Called from the DrawTriangle hook, before the original runs.
void DrawPhaseNote(uint32_t vertices) {
  if (g_in_finish_frame) {
    ++g_calls_in_finish;
    g_verts_in_finish += vertices;
  } else if (g_in_scene_walk) {
    ++g_calls_in_walk;
    g_verts_in_walk += vertices;
  } else {
    ++g_calls_elsewhere;
    g_verts_elsewhere += vertices;
  }
}

// FinishFrame runs inside the scene walk, so this bracket has to be the inner
// one and it is checked first above.
void DrawPhaseEnterFinish() { g_in_finish_frame = true; }
void DrawPhaseLeaveFinish() { g_in_finish_frame = false; }

void DrawPhaseReport();

void DrawPhaseInstall() {
  void* target = reinterpret_cast<void*>(bw::game::kProcess3dEngine);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_Process3dEngine),
                    reinterpret_cast<void**>(&o_Process3dEngine));
  if (status == MH_OK) status = MH_EnableHook(target);
  ProbeLog("  hook %-8s %-26s at 0x%08X", status == MH_OK ? "ok" : "FAILED",
           "GGame::Process3dEngine", bw::game::kProcess3dEngine);
}

// A force killed process never reaches DLL_PROCESS_DETACH, so the report has
// to happen while the game is still running.
void DrawPhaseTick(uint64_t frame) {
  if (frame != 1200 && (frame % 6000) != 0) return;
  DrawPhaseReport();
}

void DrawPhaseReport() {
  const uint64_t calls =
      g_calls_in_walk + g_calls_in_finish + g_calls_elsewhere;
  const uint64_t verts =
      g_verts_in_walk + g_verts_in_finish + g_verts_elsewhere;
  if (calls == 0) return;

  ProbeLog("");
  ProbeLog("=== where DrawTriangle is called, over %llu scene walks ===",
           g_walks);
  ProbeLog("  during the scene walk : %12llu calls  %14llu verts  %5.1f%%",
           g_calls_in_walk, g_verts_in_walk,
           100.0 * static_cast<double>(g_verts_in_walk) /
               static_cast<double>(verts ? verts : 1));
  ProbeLog("  inside FinishFrame    : %12llu calls  %14llu verts  %5.1f%%",
           g_calls_in_finish, g_verts_in_finish,
           100.0 * static_cast<double>(g_verts_in_finish) /
               static_cast<double>(verts ? verts : 1));
  ProbeLog("  outside both          : %12llu calls  %14llu verts  %5.1f%%",
           g_calls_elsewhere, g_verts_elsewhere,
           100.0 * static_cast<double>(g_verts_elsewhere) /
               static_cast<double>(verts ? verts : 1));
  ProbeLog("");
  ProbeLog("  A high FinishFrame share means the scene is a deferred list and");
  ProbeLog("  stereo can flush it twice. A high scene walk share means the");
  ProbeLog("  walk itself has to run twice.");
}
