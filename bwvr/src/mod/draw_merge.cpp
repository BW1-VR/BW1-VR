// One draw call for a row of draw calls with one state: draw_merge.
//
// WHY. A frame of the scene has about 2800 draw calls of about 37 vertices
// each, and the cost of a frame follows the count of the draw calls. The
// game changes a state of the device before about two draw calls of three.
// The other draw calls have the state of the draw call before, so Direct3D
// can get them as one draw call (docs/backend.md, "Draw calls with one
// state").
//
// HOW. In an eye pass of the scene, the hook of a draw call does not give
// the call to Direct3D. It copies the vertices and the indices into a batch
// and returns. The batch goes to Direct3D as one draw call before each call
// that changes what a draw call does, or that needs the picture so far:
//
//   - a call of the device that sets a state, clears, or starts or ends a
//     scene, and a draw call that the merge does not take (draw_probe.cpp
//     and texture_filter.cpp have these hooks),
//   - a call of a DirectDraw surface that reads or writes pixels, or changes
//     a color key or a palette: the game writes a texture in the middle of a
//     scene walk (this file, frame_profile.cpp and present_watch.cpp have
//     these hooks),
//   - a call of the mod on the Direct3D 9 device of the backend
//     (gpu_bridge.cpp),
//   - a new move of the right eye in one walk (one_walk.cpp): a room of the
//     Temple builds its camera again in the pass, and one walk draws the
//     batch again for the right eye with the move of its draw calls,
//   - the end of the eye pass.
//
// So the device has the same state for each triangle as with no merge, the
// triangles have the same order, and the picture is the same.
//
// WHAT THE MERGE TAKES. A triangle list with vertices that have their place
// on the screen (the game transforms on the CPU), from the code of the game.
// A draw call of the mod itself goes to Direct3D as it comes, because the
// mod reads the result of such a call.
//
// draw_merge=0 gives each draw call to Direct3D as it comes. Then the mod
// puts in no hook for the merge.
//
// THE TESTS, for a desktop run:
//   draw_merge_test=1        A checksum of the triangles that the game gave
//                            in an eye pass, and of the triangles that went
//                            out. The report says how many eye passes had a
//                            different checksum.
//   draw_merge_test_frame=N  The frames N and N+1 of the controllers have
//                            the merge, the frames N+2 and N+3 have not, and
//                            each of the four gets a picture of each eye.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d.h>
#include <stdint.h>
#include <stdlib.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "draw_merge_math.h"
#include "d3d7_thread.h"
#include "vk_scene.h"
#include "render_thread.h"
#include "thread_id.h"

extern void ModLog(const char* format, ...);
// draw_probe.cpp: the hooks of the device, and the draw call of a batch.
// DrawProbeMergeHooks gives 1 when each hook is in, 0 when the device is not
// there yet, and -1 when a hook failed.
int DrawProbeMergeHooks();
HRESULT DrawProbeSendMerged(void* self, DWORD fvf, void* vertices,
                            DWORD vertex_count, WORD* indices,
                            DWORD index_count, DWORD flags);
// one_walk.cpp: the move per rhw of the right eye now, and the hold of the move
// of a batch while it goes out.
float OneWalkShiftPerRhw();
void OneWalkHoldShift(bool hold, float shift);

namespace {

using draw_merge::Batch;

int g_key = 1;         // draw_merge
int g_test = 0;        // draw_merge_test
int g_test_frame = 0;  // draw_merge_test_frame
bool g_installed = false;
bool g_failed = false;
int g_tries = 0;
// True in an eye pass of the scene, when the merge takes draw calls.
bool g_inside = false;
// The test of the pictures has frames with no merge.
bool g_frame_on = true;
DWORD g_thread = 0;
// The code of the game in memory.
uintptr_t g_game_first = 0;
uintptr_t g_game_last = 0;

Batch g_batch = {};
// The device, the vertex format and the flags of the draw calls in the batch.
void* g_self = nullptr;
DWORD g_fvf = 0;
DWORD g_flags = 0;
// The move per rhw of the right eye in one walk at the draw calls in the batch.
float g_shift = 0.0f;

// For the report.
uint64_t g_passes = 0;
uint64_t g_taken = 0;   // draw calls of the game that went into a batch
uint64_t g_sent = 0;    // draw calls that went out for them
uint64_t g_passed = 0;  // draw calls in an eye pass that the merge did not take
uint64_t g_to_d3d7 = 0;  // draw calls that the D3D7 thread merges
// The check of the triangles.
uint64_t g_hash_in = draw_merge::kHashStart;
uint64_t g_hash_out = draw_merge::kHashStart;
uint64_t g_triangles_in = 0;
uint64_t g_triangles_out = 0;
uint64_t g_checked = 0;
uint64_t g_different = 0;
uint64_t g_checked_triangles = 0;

typedef HRESULT(WINAPI* BltFn)(IDirectDrawSurface7*, LPRECT,
                               IDirectDrawSurface7*, LPRECT, DWORD, LPDDBLTFX);
typedef HRESULT(WINAPI* BltFastFn)(IDirectDrawSurface7*, DWORD, DWORD,
                                   IDirectDrawSurface7*, LPRECT, DWORD);
typedef HRESULT(WINAPI* GetDCFn)(IDirectDrawSurface7*, HDC*);
typedef HRESULT(WINAPI* LockFn)(IDirectDrawSurface7*, LPRECT,
                                LPDDSURFACEDESC2, DWORD, HANDLE);
typedef HRESULT(WINAPI* SetColorKeyFn)(IDirectDrawSurface7*, DWORD,
                                       LPDDCOLORKEY);
typedef HRESULT(WINAPI* SetPaletteFn)(IDirectDrawSurface7*,
                                      LPDIRECTDRAWPALETTE);

// IDirectDrawSurface7 vtable slots.
const int kSlotBlt = 5;
const int kSlotBltFast = 7;
const int kSlotGetDC = 17;
const int kSlotLock = 25;
const int kSlotSetColorKey = 29;
const int kSlotSetPalette = 31;

BltFn o_Blt = nullptr;
BltFastFn o_BltFast = nullptr;
GetDCFn o_GetDC = nullptr;
LockFn o_Lock = nullptr;
SetColorKeyFn o_SetColorKey = nullptr;
SetPaletteFn o_SetPalette = nullptr;

}  // namespace

void DrawMergeFlush();

namespace {

// The calls of a surface that read or write pixels, or that change how a
// texture draws. The batch goes out before each of them, and the render
// thread makes each call of the walk before them (render_thread.cpp).
HRESULT WINAPI h_Blt(IDirectDrawSurface7* self, LPRECT destination,
                     IDirectDrawSurface7* source, LPRECT source_rect,
                     DWORD flags, LPDDBLTFX fx) {
  DrawMergeFlush();
  RenderThreadSync();
  VkSceneSurfaceWritten(self);
  return o_Blt(self, destination, source, source_rect, flags, fx);
}

HRESULT WINAPI h_BltFast(IDirectDrawSurface7* self, DWORD x, DWORD y,
                         IDirectDrawSurface7* source, LPRECT source_rect,
                         DWORD flags) {
  DrawMergeFlush();
  RenderThreadSync();
  VkSceneSurfaceWritten(self);
  return o_BltFast(self, x, y, source, source_rect, flags);
}

HRESULT WINAPI h_GetDC(IDirectDrawSurface7* self, HDC* dc) {
  DrawMergeFlush();
  RenderThreadSync();
  return o_GetDC(self, dc);
}

HRESULT WINAPI h_Lock(IDirectDrawSurface7* self, LPRECT rect,
                      LPDDSURFACEDESC2 desc, DWORD flags, HANDLE event) {
  DrawMergeFlush();
  RenderThreadSync();
  if ((flags & DDLOCK_READONLY) == 0) VkSceneSurfaceWritten(self);
  return o_Lock(self, rect, desc, flags, event);
}

HRESULT WINAPI h_SetColorKey(IDirectDrawSurface7* self, DWORD flags,
                             LPDDCOLORKEY key) {
  DrawMergeFlush();
  RenderThreadSync();
  return o_SetColorKey(self, flags, key);
}

HRESULT WINAPI h_SetPalette(IDirectDrawSurface7* self,
                            LPDIRECTDRAWPALETTE palette) {
  DrawMergeFlush();
  RenderThreadSync();
  return o_SetPalette(self, palette);
}

// One hook on a call of a surface. True when the hook is in. Also true when
// a different file of the mod has the hook of that call: frame_profile.cpp
// and present_watch.cpp call DrawMergeFlush in their hooks.
bool HookSurface(void* target, void* hook, void** original,
                 const char* name) {
  MH_STATUS status = MH_CreateHook(target, hook, original);
  if (status == MH_ERROR_ALREADY_CREATED) return true;
  if (status == MH_OK) status = MH_EnableHook(target);
  if (status != MH_OK) {
    ModLog("draw merge   : the hook of %s at 0x%08X FAILED", name,
           reinterpret_cast<uintptr_t>(target));
  }
  return status == MH_OK;
}

// The hooks of the surface calls, on the class of the render target. Each
// surface of the backend has that class. 1: each hook is in. 0: the device
// is not there yet. -1: a hook failed.
int InstallSurfaceHooks() {
  IDirect3DDevice7** slot =
      reinterpret_cast<IDirect3DDevice7**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return 0;
  IDirectDrawSurface7* target = nullptr;
  if (FAILED((*slot)->GetRenderTarget(&target)) || target == nullptr) return 0;
  void** vtable = *reinterpret_cast<void***>(target);
  bool all = !IsBadReadPtr(vtable, sizeof(void*) * (kSlotSetPalette + 1));
  if (all) {
    all &= HookSurface(vtable[kSlotBlt], reinterpret_cast<void*>(&h_Blt),
                       reinterpret_cast<void**>(&o_Blt), "Blt");
    all &= HookSurface(vtable[kSlotBltFast],
                       reinterpret_cast<void*>(&h_BltFast),
                       reinterpret_cast<void**>(&o_BltFast), "BltFast");
    all &= HookSurface(vtable[kSlotGetDC], reinterpret_cast<void*>(&h_GetDC),
                       reinterpret_cast<void**>(&o_GetDC), "GetDC");
    all &= HookSurface(vtable[kSlotLock], reinterpret_cast<void*>(&h_Lock),
                       reinterpret_cast<void**>(&o_Lock), "Lock");
    all &= HookSurface(vtable[kSlotSetColorKey],
                       reinterpret_cast<void*>(&h_SetColorKey),
                       reinterpret_cast<void**>(&o_SetColorKey),
                       "SetColorKey");
    all &= HookSurface(vtable[kSlotSetPalette],
                       reinterpret_cast<void*>(&h_SetPalette),
                       reinterpret_cast<void**>(&o_SetPalette), "SetPalette");
  }
  target->Release();
  return all ? 1 : -1;
}

// The first and the last address of the code of runblack.exe in memory.
void FindGame() {
  const uint8_t* base =
      reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
  if (base == nullptr) return;
  const IMAGE_DOS_HEADER* dos =
      reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const IMAGE_NT_HEADERS* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  g_game_first = reinterpret_cast<uintptr_t>(base);
  g_game_last = g_game_first + nt->OptionalHeader.SizeOfImage;
}

void GiveUp(const char* why) {
  g_failed = true;
  ModLog("draw merge   : OFF, %s. Each draw call goes to Direct3D as it "
         "comes", why);
}

// The hooks and the arrays of the batch, at the first frame of the scene.
void Install() {
  // The device is there at the first frame of the scene. 600 frames with no
  // device is a fault.
  if (++g_tries > 600) {
    GiveUp("the device of the game was not there in 600 frames");
    return;
  }
  const int device = DrawProbeMergeHooks();
  if (device == 0) return;
  if (device < 0) {
    GiveUp("a hook on a call of the device failed");
    return;
  }
  const int surface = InstallSurfaceHooks();
  if (surface == 0) return;
  if (surface < 0) {
    GiveUp("a hook on a call of a surface failed");
    return;
  }
  FindGame();
  g_batch.vertices = static_cast<uint8_t*>(
      malloc(static_cast<size_t>(draw_merge::kMaxVertices) *
             draw_merge::kMaxStride));
  g_batch.indices = static_cast<uint16_t*>(
      malloc(static_cast<size_t>(draw_merge::kMaxIndices) * sizeof(uint16_t)));
  if (g_batch.vertices == nullptr || g_batch.indices == nullptr ||
      g_game_last == 0) {
    GiveUp("the arrays of the batch could not be made");
    return;
  }
  draw_merge::Clear(&g_batch);
  g_installed = true;
  ModLog("draw merge   : draw_merge=1. In an eye pass of the scene, a row of "
         "draw calls of the game with one state goes to Direct3D as one draw "
         "call");
}

}  // namespace

void DrawMergeSetOptions(int key, int test, int test_frame) {
  g_key = key != 0 ? 1 : 0;
  g_test = test != 0 ? 1 : 0;
  g_test_frame = test_frame > 0 ? test_frame : 0;
  if (g_key == 0) {
    ModLog("draw merge   : draw_merge=0. Each draw call goes to Direct3D as "
           "it comes");
  }
}

// Each frame of the scene, before the eye passes. `frame` is the frame of
// the controllers. True when the test of the pictures wants a picture of
// each eye of this frame.
bool DrawMergeFrame(uint64_t frame) {
  g_frame_on = true;
  if (g_key == 0) return false;
  if (!g_installed && !g_failed) Install();
  if (g_test_frame <= 0) return false;
  const uint64_t first = static_cast<uint64_t>(g_test_frame);
  if (frame < first || frame >= first + 4) return false;
  g_frame_on = frame < first + 2;
  ModLog("draw merge   : test key draw_merge_test_frame: frame %llu has %s, "
         "and a picture of each eye", frame,
         g_frame_on ? "the merge" : "no merge");
  return true;
}

// The start and the end of an eye pass of the scene.
void DrawMergePass(bool inside) {
  if (!g_installed) return;
  if (inside) {
    g_inside = g_key != 0 && g_frame_on;
    g_thread = GetCurrentThreadId();
    if (g_inside) ++g_passes;
    return;
  }
  DrawMergeFlush();
  if (g_inside && g_test != 0) {
    ++g_checked;
    g_checked_triangles += g_triangles_in;
    if (g_hash_in != g_hash_out || g_triangles_in != g_triangles_out) {
      if (++g_different <= 5) {
        ModLog("draw merge   : THE CHECK FAILED in an eye pass: the game gave "
               "%llu triangles and %llu went out, and the checksums are "
               "%016llX and %016llX", g_triangles_in, g_triangles_out,
               g_hash_in, g_hash_out);
      }
    }
    g_hash_in = draw_merge::kHashStart;
    g_hash_out = draw_merge::kHashStart;
    g_triangles_in = 0;
    g_triangles_out = 0;
  }
  g_inside = false;
}

// The hook of a draw call asks: does the merge take this call? True: the
// vertices are in the batch, and the hook returns with no call of Direct3D.
// False: the batch went out, and the hook gives the call to Direct3D.
// `indices` is null for a draw call with no indices. `caller` is the address
// that the draw call returns to.
bool DrawMergeTake(void* self, D3DPRIMITIVETYPE type, DWORD fvf,
                   const void* vertices, DWORD vertex_count,
                   const WORD* indices, DWORD index_count, DWORD flags,
                   const void* caller) {
  if (!g_inside) return false;
  // The D3D7 thread merges the draws of this walk (d3d7_thread.cpp, THE
  // MERGE).
  if (D3D7ThreadMerging()) {
    DrawMergeFlush();
    ++g_to_d3d7;
    return false;
  }
  const uintptr_t from = reinterpret_cast<uintptr_t>(caller);
  const uint32_t stride = draw_merge::Stride(fvf);
  const uint32_t corners = draw_merge::WholeTriangles(
      indices != nullptr ? index_count : vertex_count);
  if (ThisThreadId() != g_thread || from < g_game_first ||
      from >= g_game_last || type != D3DPT_TRIANGLELIST ||
      vertices == nullptr || stride == 0 || corners == 0) {
    DrawMergeFlush();
    ++g_passed;
    return false;
  }
  const float shift = OneWalkShiftPerRhw();
  if (g_batch.draws > 0 &&
      (self != g_self || fvf != g_fvf || flags != g_flags ||
       shift != g_shift ||
       !draw_merge::Fits(g_batch, stride, vertex_count, corners))) {
    DrawMergeFlush();
  }
  const bool added =
      indices != nullptr
          ? draw_merge::AddIndexed(&g_batch, stride, vertices, vertex_count,
                                   indices, corners)
          : draw_merge::AddList(&g_batch, stride, vertices, corners);
  if (!added) {
    DrawMergeFlush();
    ++g_passed;
    return false;
  }
  g_self = self;
  g_fvf = fvf;
  g_flags = flags;
  g_shift = shift;
  ++g_taken;
  if (g_test != 0) {
    g_hash_in = draw_merge::HashTriangles(g_hash_in, vertices, stride, indices,
                                          corners);
    g_triangles_in += corners / 3;
  }
  return true;
}

// True when the hooks of the surface calls are in (render_thread.cpp needs
// their stops).
bool DrawMergeSurfaceHooked() { return g_installed; }

// Gives the batch to Direct3D as one draw call. Each hook of a call that
// changes what a draw call does calls this first.
void DrawMergeFlush() {
  if (g_batch.draws == 0) return;
  if (g_test != 0) {
    g_hash_out = draw_merge::HashTriangles(g_hash_out, g_batch.vertices,
                                           g_batch.stride, g_batch.indices,
                                           g_batch.index_count);
    g_triangles_out += g_batch.index_count / 3;
  }
  OneWalkHoldShift(true, g_shift);
  DrawProbeSendMerged(g_self, g_fvf, g_batch.vertices, g_batch.vertex_count,
                      g_batch.indices, g_batch.index_count, g_flags);
  OneWalkHoldShift(false, 0.0f);
  ++g_sent;
  draw_merge::Clear(&g_batch);
}

void DrawMergeReport() {
  if (!g_installed || g_passes == 0) return;
  ModLog("draw merge   : draw_merge=%d. In %llu eye passes the merge took "
         "%llu draw calls of the game, and they went to Direct3D as %llu "
         "draw calls (%.1f percent fewer). %llu draw calls went as they came, "
         "%llu went to the merge of the D3D7 thread",
         g_key, g_passes, g_taken, g_sent,
         g_taken > 0 ? 100.0 * (1.0 - static_cast<double>(g_sent) /
                                          static_cast<double>(g_taken))
                     : 0.0,
         g_passed, g_to_d3d7);
  if (g_test != 0) {
    ModLog("draw merge   : the check of the triangles (draw_merge_test=1): "
           "%llu eye passes with %llu triangles, and %llu of the eye passes "
           "had a different checksum", g_checked, g_checked_triangles,
           g_different);
    g_checked = 0;
    g_checked_triangles = 0;
    g_different = 0;
  }
  g_passes = 0;
  g_taken = 0;
  g_sent = 0;
  g_passed = 0;
  g_to_d3d7 = 0;
}
