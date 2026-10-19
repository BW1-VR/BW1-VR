// One walk of the scene for both eyes (docs/backend.md, "One walk for both
// eyes").
//
// WHAT IT REPLACES
//
// Stereo runs GGame::Process3dEngine two times a frame, one time for each
// eye. The engine transforms, lights, clips and sorts on the CPU, so the
// second walk costs as much game code as the first. The two walks give the
// same batches in the same order. Only the screen x of a vertex changes, by
// an amount that its own rhw gives (one_walk_math.h).
//
// HOW
//
// The game walks the scene one time, for the left eye. For that walk the
// render target is a target of the mod that is two eyes wide. The left half is
// the left eye. Each draw of the walk reaches Direct3D 9 as
// DrawIndexedPrimitiveUP or DrawPrimitiveUP from dxwrapper, with the game's
// own vertices. A hook here draws the batch as the game asked, into the left
// half, and then a copy of it into the right half, with the viewport moved to
// the right half and each x moved by the half and by the disparity of its
// rhw. Direct3D 9 takes XYZRHW vertices as pixels of the whole target and
// clips them to the viewport, so a triangle at the edge of one eye stays out
// of the other eye. A change of the viewport costs much less than a change of
// the render target, and nothing else changes between the two draws.
// With the render thread, a batch whose two copies are each in their own
// half is one draw for both eyes (one_walk_merge, ONE DRAW FOR BOTH EYES in
// h_DrawIndexedPrimitiveUP).
//
// THE INTERFACE
//
// The interface has no depth in the batch. Its depth comes from the chain of
// draw_hooks.cpp, which moves each batch for the eye of the pass. In one walk
// draw_hooks.cpp runs its chain a second time for the right eye and draws
// that batch into the right half only (kOneWalkRightOnly). The rule here
// moves no batch that has an rhw of exactly 1 in each vertex, or a z of 1 in
// each vertex (the sky), so a flat batch that no chain moves stays flat.
//
// FAIL SAFE
//
// The stereo loop asks for one walk frame by frame. If the target cannot be
// made, the hooks cannot go in, or the device is gone, the loop draws the two
// walks as before.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <string.h>

#include "../common/bw_addresses.h"
#include "one_walk.h"
#include "one_walk_math.h"
#include "d3d7_thread.h"
#include "render_thread_d3d9.h"
#include "vk_scene.h"

extern void ModLog(const char* format, ...);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);
// draw_merge.cpp: the batch of the merge goes to Direct3D now.
void DrawMergeFlush();
// gpu_bridge.cpp
void* GpuBridgeDeviceObject();
void GpuBridgeOwnDepth(bool on);

namespace {

bool g_wanted = false;     // one_walk in bwvr.ini, or the test key
bool g_check = false;      // one_walk_check in bwvr.ini
bool g_marks = true;       // one_walk_marks in bwvr.ini, or the test key
int g_marks_test_frame = 0;  // one_walk_marks_test_frame
bool g_hooked = false;
bool g_hook_failed = false;
bool g_make_failed = false;

IDirect3DDevice9* g_device = nullptr;  // the device the wide target is on
IDirect3DSurface9* g_wide = nullptr;
IDirect3DSurface9* g_wide_depth = nullptr;
uint32_t g_eye_width = 0;
uint32_t g_eye_height = 0;
bool g_fresh = false;  // the wide target was just made and holds no picture
D3DFORMAT g_format = D3DFMT_UNKNOWN;
D3DFORMAT g_depth_format = D3DFMT_UNKNOWN;
D3DMULTISAMPLE_TYPE g_samples = D3DMULTISAMPLE_NONE;
DWORD g_quality = 0;

// The game's target and depth of the pass. A reference is held only while the
// pass runs. The raw pointers stay after it, so that a late restore of the
// wide target can be turned back to the game's target.
IDirect3DSurface9* g_game_target = nullptr;
IDirect3DSurface9* g_game_depth = nullptr;
IDirect3DSurface9* g_last_game_target = nullptr;
IDirect3DSurface9* g_last_game_depth = nullptr;

bool g_active = false;
int g_mode = kOneWalkBoth;
IDirect3DSurface9* g_bound = nullptr;  // render target 0 now, in the pass
// The eye separation of the frame in world units. The move per rhw comes from
// it at each draw, with the field of view, the size and the near plane that
// the engine has at that draw: a room of the Temple builds its camera again
// inside the pass.
float g_separation = 0.0f;
float g_per_rhw = 0.0f;  // the move per rhw of the last draw
// A batch of draw_merge goes to Direct3D after the draw calls of the game that
// it holds, so the engine can have a new camera then. draw_merge keeps the
// move per rhw of its batch and holds it while the batch goes out.
bool g_shift_held = false;
float g_held_shift = 0.0f;

// The four inputs of the move per rhw: the scale of the field of view, the
// eye separation, half the render width and the near plane.
void RhwInputs(float out[4]) {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  out[0] = info->inv_half_tan_fov_x;
  out[1] = g_separation;
  out[2] = info->half_res_x;
  out[3] = *reinterpret_cast<const float*>(bw::tech::kNearPlane);
}

// The move per rhw from its inputs. The inputs change seldom in a walk, so
// the last result is kept with them.
struct RhwCache {
  float inputs[4];
  float result;
  float Get(const float in[4]) {
    if (memcmp(in, inputs, sizeof(inputs)) != 0) {
      memcpy(inputs, in, sizeof(inputs));
      result = one_walk_math::ShiftPerRhw(in[0], in[1], in[2], in[3]);
    }
    return result;
  }
};
RhwCache g_frame_rhw = {{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f};

// THE STATE ON THE D3D7 THREAD (d3d7_thread=1). The hooks of Direct3D 9 run
// on the D3D7 thread then, later than the calls of the game. So the D3D7
// thread has its own copy of the mode, of the held move and of the inputs of
// the move, and each change of them on the frame thread goes into the queue
// of the D3D7 thread in the order of the calls (D3D7ThreadPost).
struct WorkerState {
  int mode;
  bool shift_held;
  float held_shift;
  float inputs[4];
};
WorkerState g_worker = {kOneWalkBoth, false, 0.0f, {0.0f, 0.0f, 0.0f, 0.0f}};
RhwCache g_worker_rhw = {{0.0f, 0.0f, 0.0f, 0.0f}, 0.0f};
// The values that went into the queue last: a record only for a change.
float g_posted_inputs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
int g_posted_mode = kOneWalkBoth;
bool g_posted_held = false;
float g_posted_shift = 0.0f;

void ApplyState(const void* data) {
  memcpy(&g_worker, data, sizeof(g_worker));
}
void ApplyMode(const void* data) { memcpy(&g_worker.mode, data, sizeof(int)); }
void ApplyHold(const void* data) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  g_worker.shift_held = p[0] != 0;
  memcpy(&g_worker.held_shift, p + 4, sizeof(float));
}
void ApplyInputs(const void* data) {
  memcpy(g_worker.inputs, data, sizeof(g_worker.inputs));
}

// The mode for the hooks of Direct3D 9.
int Mode() { return D3D7ThreadOnWorker() ? g_worker.mode : g_mode; }

float PerRhwNow() {
  if (D3D7ThreadOnWorker()) {
    return g_worker.shift_held ? g_worker.held_shift
                               : g_worker_rhw.Get(g_worker.inputs);
  }
  if (g_shift_held) {
    g_per_rhw = g_held_shift;
    return g_per_rhw;
  }
  float inputs[4];
  RhwInputs(inputs);
  g_per_rhw = g_frame_rhw.Get(inputs);
  return g_per_rhw;
}

// THE EDGE OF THE SCREEN. The engine clamps each projected x into
// 0..g_MaxScreen.x (bw_addresses.h, kMaxScreen). A vertex past the right edge
// of the left eye lands on that edge, and the right eye moves it left by its
// disparity, so a strip at the right edge of the right eye had no geometry.
// In the pass the right limit is a guard band past the edge. The viewport
// still ends the left eye at its edge. The disparity is
// scale * separation * half_res_x / camera_z, which is about 390 px at one
// world unit from the eye for a render of 2208, so the guard is half a render.
float g_saved_max_x = 0.0f;
bool g_max_moved = false;

// The picture of one eye for a file (OneWalkReadEye): the half of the wide
// target resolved to a plain target, then copied to system memory.
IDirect3DSurface9* g_read_target = nullptr;
IDirect3DSurface9* g_read_memory = nullptr;
bool g_read_locked = false;

// The copy of a batch for the right eye.
uint8_t* g_scratch = nullptr;
size_t g_scratch_size = 0;

// The report, per window.
LARGE_INTEGER g_frequency = {0};
uint64_t g_passes = 0;
uint64_t g_world_batches = 0;
uint64_t g_flat_batches = 0;
uint64_t g_sky_batches = 0;
uint64_t g_right_only = 0;
uint64_t g_left_only = 0;
uint64_t g_not_screen_space = 0;
uint64_t g_buffer_draws = 0;  // draws from a vertex buffer: no right eye
uint64_t g_clears = 0;
uint64_t g_vertices = 0;
// The batches whose two copies are each in their own half (InOwnHalves),
// of the batches that one walk drew in both eyes.
uint64_t g_own_halves = 0;
uint64_t g_both_batches = 0;
uint64_t g_merged = 0;  // batches drawn in one draw for both eyes
bool g_merge = true;    // one_walk_merge
uint64_t g_right_ticks = 0;
uint64_t g_target_swaps = 0;
uint64_t g_late_swaps = 0;

// The time of the work of the right eye on the frame thread, measured for
// one draw in 16: two reads of the clock for each draw cost a part of what
// they measure.
uint32_t g_timed_draws = 0;
struct RightTime {
  LARGE_INTEGER start;
  bool on;
  RightTime() : on((++g_timed_draws & 15u) == 0) {
    if (on) QueryPerformanceCounter(&start);
  }
  ~RightTime() {
    if (!on) return;
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    g_right_ticks += 16 * (end.QuadPart - start.QuadPart);
  }
};

bool EnsureScratch(size_t bytes) {
  if (bytes <= g_scratch_size) return true;
  size_t size = g_scratch_size == 0 ? 65536 : g_scratch_size;
  while (size < bytes) size *= 2;
  uint8_t* fresh = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (fresh == nullptr) return false;
  if (g_scratch != nullptr) VirtualFree(g_scratch, 0, MEM_RELEASE);
  g_scratch = fresh;
  g_scratch_size = size;
  return true;
}

// True when the draw goes to the wide target in a pass.
bool InWide(IDirect3DDevice9* self) {
  return g_active && self == g_device && g_bound == g_wide;
}

bool ScreenSpace(IDirect3DDevice9* self, UINT stride) {
  DWORD fvf = 0;
  if (stride < 16 || FAILED(self->GetFVF(&fvf))) return false;
  return (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
}

// The viewport and the scissor rect of the right half, set. The caller puts
// the saved ones back with PutBack.
struct Saved {
  D3DVIEWPORT9 viewport;
  RECT scissor;
  bool scissor_on;
};

void MoveRight(IDirect3DDevice9* self, Saved* saved) {
  self->GetViewport(&saved->viewport);
  D3DVIEWPORT9 right = saved->viewport;
  right.X += g_eye_width;
  self->SetViewport(&right);
  DWORD on = FALSE;
  self->GetRenderState(D3DRS_SCISSORTESTENABLE, &on);
  saved->scissor_on = on != FALSE;
  if (saved->scissor_on) {
    self->GetScissorRect(&saved->scissor);
    RECT moved = saved->scissor;
    moved.left += static_cast<LONG>(g_eye_width);
    moved.right += static_cast<LONG>(g_eye_width);
    self->SetScissorRect(&moved);
  }
}

void PutBack(IDirect3DDevice9* self, const Saved& saved) {
  self->SetViewport(&saved.viewport);
  if (saved.scissor_on) self->SetScissorRect(&saved.scissor);
}

// Makes the copy of count vertices for the right eye in g_scratch. Returns
// false when there is no room.
bool CopyRight(const void* vertices, uint32_t count, UINT stride,
               bool as_world) {
  const size_t bytes = static_cast<size_t>(count) * stride;
  if (!EnsureScratch(bytes)) return false;
  const uint8_t* src = static_cast<const uint8_t*>(vertices);
  const float offset = static_cast<float>(g_eye_width);
  if (!as_world) {
    one_walk_math::CopyMoved(src, g_scratch, count, stride, offset);
    return true;
  }
  const one_walk_math::BatchKind kind = one_walk_math::CopyForRightEye(
      src, g_scratch, count, stride, offset, PerRhwNow());
  if (kind == one_walk_math::kWorld) {
    ++g_world_batches;
  } else if (kind == one_walk_math::kFlat) {
    ++g_flat_batches;
  } else {
    ++g_sky_batches;
  }
  g_vertices += count;
  return true;
}

// Counts a batch of both eyes, and if its copies are in their own halves.
// The right copy is in g_scratch.
void CountOwnHalves(IDirect3DDevice9* self, const void* left, uint32_t count,
                    UINT stride) {
  D3DVIEWPORT9 v;
  if (FAILED(self->GetViewport(&v))) return;
  ++g_both_batches;
  if (one_walk_math::InOwnHalves(static_cast<const uint8_t*>(left), g_scratch,
                                 count, stride,
                                 static_cast<float>(v.X + v.Width),
                                 static_cast<float>(v.X + g_eye_width))) {
    ++g_own_halves;
  }
}

HRESULT STDMETHODCALLTYPE h_DrawIndexedPrimitiveUP(
    IDirect3DDevice9* self, D3DPRIMITIVETYPE type, UINT min_index,
    UINT vertex_count, UINT primitive_count, const void* indices,
    D3DFORMAT index_format, const void* vertices, UINT stride) {
  if (!InWide(self)) {
    RenderThreadViewportPutBack(self);
    return RenderThreadOutDrawIndexedPrimitiveUP(
        self, type, min_index, vertex_count, primitive_count, indices,
        index_format, vertices, stride);
  }
  const int mode = Mode();
  // ONE DRAW FOR BOTH EYES (one_walk_merge=1). A triangle list whose left
  // copy ends at the right edge of the viewport of the left eye, and whose
  // right copy starts at the left edge of the right eye, needs no clip at the
  // edge between the eyes. Then one draw with one viewport over both eyes
  // gives the pixels of the two draws. The viewport over both eyes stays on
  // the device for the next such draw (render_thread.cpp, THE VIEWPORT OF
  // BOTH EYES). The scissor rect would need a move for the right eye, so a
  // draw with the scissor test is drawn one eye at a time.
  if (mode == kOneWalkBoth && g_merge && type == D3DPT_TRIANGLELIST &&
      vertices != nullptr && indices != nullptr && ScreenSpace(self, stride)) {
    RightTime timer;
    const uint32_t count = min_index + vertex_count;
    D3DVIEWPORT9 game;
    DWORD scissor = TRUE;
    D3DVIEWPORT9 both = {};
    if (SUCCEEDED(self->GetViewport(&game)) &&
        SUCCEEDED(self->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor)) &&
        scissor == FALSE) {
      both = game;
      both.Width = g_eye_width + game.Width;
    }
    // THE RIGHT EYE ON THE RENDER THREAD (render_thread_right_eye=1). The
    // queue gets the left copy, and the render thread makes the right copy
    // and finds if both copies are one draw. This thread then only copies the
    // vertices of the game one time.
    if (scissor == FALSE && RenderThreadViewportBoth(self, &both) &&
        RenderThreadOutDrawBothLater(self, type, min_index, vertex_count,
                                     primitive_count, indices, index_format,
                                     vertices, stride, game, g_eye_width,
                                     PerRhwNow())) {
      ++g_both_batches;
      return D3D_OK;
    }
    if (scissor == FALSE && CopyRight(vertices, count, stride, true)) {
      ++g_both_batches;
      const bool fits = one_walk_math::InOwnHalves(
          static_cast<const uint8_t*>(vertices), g_scratch, count, stride,
          static_cast<float>(game.X + game.Width),
          static_cast<float>(game.X + g_eye_width));
      if (fits) ++g_own_halves;
      if (fits && RenderThreadViewportBoth(self, &both) &&
          RenderThreadOutDrawIndexedBoth(self, type, min_index, vertex_count,
                                         primitive_count, indices,
                                         index_format, vertices, g_scratch,
                                         stride)) {
        ++g_merged;
        return D3D_OK;
      }
      // One eye at a time, with the copy of the right eye made.
      RenderThreadViewportPutBack(self);
      const HRESULT hr = RenderThreadOutDrawIndexedPrimitiveUP(
          self, type, min_index, vertex_count, primitive_count, indices,
          index_format, vertices, stride);
      if (SUCCEEDED(hr)) {
        Saved saved;
        MoveRight(self, &saved);
        RenderThreadOutDrawIndexedPrimitiveUP(
            self, type, min_index, vertex_count, primitive_count, indices,
            index_format, g_scratch, stride);
        PutBack(self, saved);
      }
      return hr;
    }
  }
  RenderThreadViewportPutBack(self);
  HRESULT hr = S_OK;
  if (mode != kOneWalkRightOnly) {
    hr = RenderThreadOutDrawIndexedPrimitiveUP(
        self, type, min_index, vertex_count, primitive_count, indices,
        index_format, vertices, stride);
  }
  if (mode == kOneWalkLeftOnly) {
    ++g_left_only;
    return hr;
  }
  if (FAILED(hr) || vertices == nullptr || !ScreenSpace(self, stride)) {
    ++g_not_screen_space;
    return hr;
  }
  RightTime timer;
  // The indices count from the start of the array, so the copy starts there.
  const uint32_t count = min_index + vertex_count;
  if (CopyRight(vertices, count, stride, mode == kOneWalkBoth)) {
    if (mode == kOneWalkBoth) CountOwnHalves(self, vertices, count, stride);
    if (mode == kOneWalkRightOnly) ++g_right_only;
    Saved saved;
    MoveRight(self, &saved);
    const HRESULT right = RenderThreadOutDrawIndexedPrimitiveUP(
        self, type, min_index, vertex_count, primitive_count, indices,
        index_format, g_scratch, stride);
    PutBack(self, saved);
    if (mode == kOneWalkRightOnly) hr = right;
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE h_DrawPrimitiveUP(IDirect3DDevice9* self,
                                            D3DPRIMITIVETYPE type,
                                            UINT primitive_count,
                                            const void* vertices, UINT stride) {
  RenderThreadViewportPutBack(self);
  if (!InWide(self)) {
    return RenderThreadOutDrawPrimitiveUP(self, type, primitive_count, vertices,
                                          stride);
  }
  const int mode = Mode();
  HRESULT hr = S_OK;
  if (mode != kOneWalkRightOnly) {
    hr = RenderThreadOutDrawPrimitiveUP(self, type, primitive_count, vertices,
                                        stride);
  }
  if (mode == kOneWalkLeftOnly) {
    ++g_left_only;
    return hr;
  }
  if (FAILED(hr) || vertices == nullptr || !ScreenSpace(self, stride)) {
    ++g_not_screen_space;
    return hr;
  }
  const uint32_t count =
      one_walk_math::VertexCount(static_cast<int>(type), primitive_count);
  if (count == 0) return hr;
  RightTime timer;
  if (CopyRight(vertices, count, stride, mode == kOneWalkBoth)) {
    if (mode == kOneWalkBoth) CountOwnHalves(self, vertices, count, stride);
    if (mode == kOneWalkRightOnly) ++g_right_only;
    Saved saved;
    MoveRight(self, &saved);
    const HRESULT right = RenderThreadOutDrawPrimitiveUP(
        self, type, primitive_count, g_scratch, stride);
    PutBack(self, saved);
    if (mode == kOneWalkRightOnly) hr = right;
  }
  return hr;
}

// A draw from a vertex buffer cannot be copied without a read of the buffer.
// The game and dxwrapper do not use one in the scene walk. It is counted, so
// the report says if that changes.
HRESULT STDMETHODCALLTYPE h_DrawPrimitive(IDirect3DDevice9* self,
                                          D3DPRIMITIVETYPE type, UINT start,
                                          UINT count) {
  RenderThreadViewportPutBack(self);
  if (InWide(self)) ++g_buffer_draws;
  return RenderThreadOutDrawPrimitive(self, type, start, count);
}

HRESULT STDMETHODCALLTYPE h_DrawIndexedPrimitive(IDirect3DDevice9* self,
                                                 D3DPRIMITIVETYPE type,
                                                 INT base, UINT min_index,
                                                 UINT vertices, UINT start,
                                                 UINT count) {
  RenderThreadViewportPutBack(self);
  if (InWide(self)) ++g_buffer_draws;
  return RenderThreadOutDrawIndexedPrimitive(self, type, base, min_index,
                                             vertices, start, count);
}

// A clear respects the viewport, which is the left half. The right half gets
// the same clear.
HRESULT STDMETHODCALLTYPE h_Clear(IDirect3DDevice9* self, DWORD count,
                                  const D3DRECT* rects, DWORD flags,
                                  D3DCOLOR color, float z, DWORD stencil) {
  RenderThreadViewportPutBack(self);
  // The Vulkan renderer makes the same clear when the target is the one of
  // its eye pictures (vk_scene.cpp).
  if (g_active && self == g_device) {
    VkSceneClear(count, rects, flags, color, z);
  }
  if (!InWide(self)) {
    return RenderThreadOutClear(self, count, rects, flags, color, z, stencil);
  }
  HRESULT hr = S_OK;
  const int mode = Mode();
  if (mode != kOneWalkRightOnly) {
    hr = RenderThreadOutClear(self, count, rects, flags, color, z, stencil);
  }
  if (mode == kOneWalkLeftOnly) return hr;
  ++g_clears;
  D3DRECT moved[16];
  const D3DRECT* use = nullptr;
  DWORD use_count = 0;
  if (count > 0 && rects != nullptr) {
    use_count = count < 16 ? count : 16;
    for (DWORD i = 0; i < use_count; ++i) {
      moved[i] = rects[i];
      moved[i].x1 += static_cast<LONG>(g_eye_width);
      moved[i].x2 += static_cast<LONG>(g_eye_width);
    }
    use = moved;
  }
  Saved saved;
  MoveRight(self, &saved);
  const HRESULT right =
      RenderThreadOutClear(self, use_count, use, flags, color, z, stencil);
  PutBack(self, saved);
  return mode == kOneWalkRightOnly ? right : hr;
}

// THE TARGET STAYS WIDE FOR THE WHOLE PASS. dxwrapper binds the game's target
// again when its own record of the target changes, and the interface panel
// (gpu_bridge.cpp) binds its target and then puts back the one it found. Both
// go through here. In a pass the game's target becomes the wide one. After
// the pass a late put back of the wide target becomes the game's target.
HRESULT STDMETHODCALLTYPE h_SetRenderTarget(IDirect3DDevice9* self,
                                            DWORD index,
                                            IDirect3DSurface9* surface) {
  if (index == 0 && self == g_device && surface != nullptr) {
    if (g_active && surface == g_game_target) {
      surface = g_wide;
      ++g_target_swaps;
    } else if (!g_active && surface == g_wide && g_last_game_target != nullptr) {
      surface = g_last_game_target;
      ++g_late_swaps;
    }
  }
  const HRESULT hr = RenderThreadOutSetRenderTarget(self, index, surface);
  if (index == 0 && self == g_device && SUCCEEDED(hr)) {
    g_bound = surface;
    // The draws that follow go to this target (vk_scene.cpp).
    if (g_active) VkSceneTarget(surface);
  }
  return hr;
}

HRESULT STDMETHODCALLTYPE h_SetDepthStencil(IDirect3DDevice9* self,
                                            IDirect3DSurface9* surface) {
  if (self == g_device && surface != nullptr) {
    if (g_active && surface == g_game_depth && g_wide_depth != nullptr) {
      surface = g_wide_depth;
    } else if (!g_active && surface == g_wide_depth &&
               g_last_game_depth != nullptr) {
      surface = g_last_game_depth;
    }
  }
  return RenderThreadOutSetDepthStencil(self, surface);
}

// The hooks of the seven calls are in render_thread.cpp. They call the
// OneWalkOn functions below.
bool Install(IDirect3DDevice9* device) {
  if (g_hooked) return true;
  if (g_hook_failed) return false;
  const bool ok = RenderThreadInstall(device, false);
  ModLog("one walk     : 7 Direct3D 9 calls hooked %s", ok ? "ok" : "FAILED");
  if (!ok) {
    g_hook_failed = true;
    return false;
  }
  g_hooked = true;
  QueryPerformanceFrequency(&g_frequency);
  return true;
}

void ReleaseWide() {
  if (g_read_locked && g_read_memory != nullptr) g_read_memory->UnlockRect();
  g_read_locked = false;
  if (g_read_target != nullptr) g_read_target->Release();
  if (g_read_memory != nullptr) g_read_memory->Release();
  g_read_target = nullptr;
  g_read_memory = nullptr;
  if (g_wide != nullptr) g_wide->Release();
  if (g_wide_depth != nullptr) g_wide_depth->Release();
  g_wide = nullptr;
  g_wide_depth = nullptr;
  g_eye_width = 0;
  g_eye_height = 0;
}

// The wide target and its depth, two eyes wide, in the format and the
// multisampling of the game's own target and depth.
bool EnsureWide(IDirect3DDevice9* device, const D3DSURFACE_DESC& target,
                const D3DSURFACE_DESC* depth) {
  const D3DFORMAT depth_format = depth != nullptr ? depth->Format
                                                  : D3DFMT_UNKNOWN;
  if (g_wide != nullptr && device == g_device &&
      target.Width == g_eye_width && target.Height == g_eye_height &&
      target.Format == g_format && target.MultiSampleType == g_samples &&
      target.MultiSampleQuality == g_quality &&
      depth_format == g_depth_format &&
      (depth == nullptr) == (g_wide_depth == nullptr)) {
    return true;
  }
  if (g_make_failed) return false;
  if (device == g_device) ReleaseWide();
  g_device = device;
  HRESULT hr = device->CreateRenderTarget(
      target.Width * 2, target.Height, target.Format, target.MultiSampleType,
      target.MultiSampleQuality, FALSE, &g_wide, nullptr);
  if (FAILED(hr) || g_wide == nullptr) {
    ModLog("one walk     : the wide target %ux%u could not be made "
           "(0x%08lX). Two walks stay", target.Width * 2, target.Height, hr);
    g_wide = nullptr;
    g_make_failed = true;
    return false;
  }
  if (depth != nullptr) {
    GpuBridgeOwnDepth(true);
    hr = device->CreateDepthStencilSurface(
        target.Width * 2, target.Height, depth->Format, depth->MultiSampleType,
        depth->MultiSampleQuality, FALSE, &g_wide_depth, nullptr);
    GpuBridgeOwnDepth(false);
    if (FAILED(hr) || g_wide_depth == nullptr) {
      ModLog("one walk     : the wide depth could not be made (0x%08lX). Two "
             "walks stay", hr);
      g_wide_depth = nullptr;
      ReleaseWide();
      g_make_failed = true;
      return false;
    }
  }
  g_eye_width = target.Width;
  g_eye_height = target.Height;
  g_fresh = true;
  g_format = target.Format;
  g_depth_format = depth_format;
  g_samples = target.MultiSampleType;
  g_quality = target.MultiSampleQuality;
  ModLog("one walk     : made the wide target %ux%u, format %d, multisample "
         "type %d, with %s depth", target.Width * 2, target.Height,
         static_cast<int>(target.Format),
         static_cast<int>(target.MultiSampleType),
         depth != nullptr ? "a" : "no");
  return true;
}

double Ms(uint64_t ticks) {
  if (g_frequency.QuadPart == 0) return 0.0;
  return static_cast<double>(ticks) * 1000.0 /
         static_cast<double>(g_frequency.QuadPart);
}

}  // namespace

// The seven calls, from the hooks of render_thread.cpp.
HRESULT OneWalkOnSetRenderTarget(IDirect3DDevice9* self, DWORD index,
                                 IDirect3DSurface9* surface) {
  return h_SetRenderTarget(self, index, surface);
}

HRESULT OneWalkOnSetDepthStencil(IDirect3DDevice9* self,
                                 IDirect3DSurface9* surface) {
  return h_SetDepthStencil(self, surface);
}

HRESULT OneWalkOnClear(IDirect3DDevice9* self, DWORD count,
                       const D3DRECT* rects, DWORD flags, D3DCOLOR color,
                       float z, DWORD stencil) {
  return h_Clear(self, count, rects, flags, color, z, stencil);
}

HRESULT OneWalkOnDrawPrimitive(IDirect3DDevice9* self, D3DPRIMITIVETYPE type,
                               UINT start, UINT count) {
  return h_DrawPrimitive(self, type, start, count);
}

HRESULT OneWalkOnDrawIndexedPrimitive(IDirect3DDevice9* self,
                                      D3DPRIMITIVETYPE type, INT base,
                                      UINT min_index, UINT vertices,
                                      UINT start, UINT count) {
  return h_DrawIndexedPrimitive(self, type, base, min_index, vertices, start,
                                count);
}

HRESULT OneWalkOnDrawPrimitiveUP(IDirect3DDevice9* self, D3DPRIMITIVETYPE type,
                                 UINT count, const void* vertices,
                                 UINT stride) {
  return h_DrawPrimitiveUP(self, type, count, vertices, stride);
}

HRESULT OneWalkOnDrawIndexedPrimitiveUP(IDirect3DDevice9* self,
                                        D3DPRIMITIVETYPE type, UINT min_index,
                                        UINT vertex_count,
                                        UINT primitive_count,
                                        const void* indices,
                                        D3DFORMAT index_format,
                                        const void* vertices, UINT stride) {
  return h_DrawIndexedPrimitiveUP(self, type, min_index, vertex_count,
                                  primitive_count, indices, index_format,
                                  vertices, stride);
}

void OneWalkSetWanted(bool on) {
  g_wanted = on;
  ModLog("one walk     : %s (one_walk=%d)", on
             ? "on. The scene is walked one time, and the right eye is drawn "
               "from the batches of the left eye"
             : "off. The scene is walked one time for each eye",
         on ? 1 : 0);
}

void OneWalkSetCheck(bool on) { g_check = on; }
bool OneWalkCheck() { return g_check; }

bool OneWalkWanted() { return g_wanted && !g_hook_failed && !g_make_failed; }

void OneWalkToggle() { OneWalkSetWanted(!g_wanted); }

namespace {

const char* MarksText(bool on) {
  return on ? "in one walk a speech bubble or the name of a villager as a "
              "card, the glow at the Hand's point and a halo are drawn for "
              "each eye, and the right eye has them at the place of its own "
              "walk"
            : "in one walk the right eye gets a speech bubble or the name of a "
              "villager as a card, the glow at the Hand's point and a halo "
              "from the rhw of their batches, as each other batch";
}

}  // namespace

void OneWalkSetMarks(bool on, int test_frame) {
  g_marks = on;
  g_marks_test_frame = test_frame;
  ModLog("one walk     : one_walk_marks=%d, %s", on ? 1 : 0, MarksText(on));
}

bool OneWalkMarks() { return g_marks; }

// A long pulse for 1 and a short pulse for 0.
void OneWalkMarksToggle(const char* cause) {
  g_marks = !g_marks;
  ControllersGesturePulse(0.5f, g_marks ? 0.30f : 0.08f);
  ModLog("one walk     : %s: one_walk_marks is %d now (%s), frame %llu", cause,
         g_marks ? 1 : 0, MarksText(g_marks), ControllersFrameCount());
}

bool OneWalkBeginPass(float separation) {
  if (g_active) return true;
  // A test with no key press: the test key of one_walk_marks acts at one
  // frame.
  if (g_marks_test_frame > 0) {
    const uint64_t frame = ControllersFrameCount();
    const uint64_t at = static_cast<uint64_t>(g_marks_test_frame);
    static uint64_t done_frame = 0;
    if (frame != done_frame && ControllersTestFrame(at, frame) == at) {
      done_frame = frame;
      OneWalkMarksToggle("test key one_walk_marks_test_frame");
    }
  }
  // backend=vulkan: the Vulkan renderer draws each batch of the walk into
  // both layers of its target, and moves the vertices of the right layer by
  // the move per rhw (vk_scene.cpp, MULTIVIEW). So there is no wide target
  // and no hook of Direct3D 9.
  if (VkSceneDirect()) {
    g_separation = separation;
    g_mode = kOneWalkBoth;
    g_active = true;
    g_bound = nullptr;
    float* max_screen = reinterpret_cast<float*>(bw::tech::kMaxScreen);
    g_saved_max_x = max_screen[0];
    g_max_moved = g_saved_max_x > 0.0f;
    // As with the wide target: the game keeps half an eye more on the
    // right, which the right eye sees.
    const bw::tech::InfoTransform* info =
        reinterpret_cast<const bw::tech::InfoTransform*>(
            bw::tech::kInfoTransform);
    if (g_max_moved) max_screen[0] = g_saved_max_x + info->half_res_x;
    ++g_passes;
    return true;
  }
  IDirect3DDevice9* device =
      static_cast<IDirect3DDevice9*>(GpuBridgeDeviceObject());
  if (device == nullptr || !Install(device)) return false;
  IDirect3DSurface9* target = nullptr;
  if (FAILED(device->GetRenderTarget(0, &target)) || target == nullptr) {
    return false;
  }
  IDirect3DSurface9* depth = nullptr;
  if (FAILED(device->GetDepthStencilSurface(&depth))) depth = nullptr;
  D3DSURFACE_DESC target_desc, depth_desc;
  bool ok = SUCCEEDED(target->GetDesc(&target_desc));
  if (ok && depth != nullptr) ok = SUCCEEDED(depth->GetDesc(&depth_desc));
  // A wide target bound already means a pass that did not end. Do not nest.
  if (ok && target == g_wide) ok = false;
  if (ok) ok = EnsureWide(device, target_desc,
                          depth != nullptr ? &depth_desc : nullptr);
  if (!ok) {
    target->Release();
    if (depth != nullptr) depth->Release();
    return false;
  }
  g_game_target = target;
  g_game_depth = depth;
  g_last_game_target = target;
  g_last_game_depth = depth;
  g_separation = separation;
  g_mode = kOneWalkBoth;
  g_active = true;
  float* max_screen = reinterpret_cast<float*>(bw::tech::kMaxScreen);
  g_saved_max_x = max_screen[0];
  g_max_moved = g_saved_max_x > 0.0f;
  if (g_max_moved) max_screen[0] = g_saved_max_x + 0.5f * g_eye_width;
  RenderThreadOutSetRenderTarget(device, 0, g_wide);
  g_bound = g_wide;
  if (g_wide_depth != nullptr) {
    RenderThreadOutSetDepthStencil(device, g_wide_depth);
  }
  // THE FRAME BEFORE. The game draws each frame over the frame before
  // (eye_clean.cpp). The wide target keeps the picture of each eye in its
  // half from one frame to the next, so each eye has its own frame before.
  // The cleans of the game and of eye_clean.cpp in the pass reach both halves
  // through h_Clear. Only a target that was just made is cleaned here.
  if (g_fresh) {
    g_fresh = false;
    const DWORD clear =
        D3DCLEAR_TARGET | (g_wide_depth != nullptr ? D3DCLEAR_ZBUFFER : 0);
    D3DVIEWPORT9 all = {0, 0, g_eye_width * 2, g_eye_height, 0.0f, 1.0f};
    device->SetViewport(&all);
    RenderThreadOutClear(device, 0, nullptr, clear, D3DCOLOR_ARGB(255, 0, 0, 0),
                         1.0f, 0);
  }
  ++g_passes;
  return true;
}

void OneWalkEndPass() {
  if (!g_active) return;
  g_active = false;
  g_mode = kOneWalkBoth;
  if (g_max_moved) {
    reinterpret_cast<float*>(bw::tech::kMaxScreen)[0] = g_saved_max_x;
    g_max_moved = false;
  }
  if (g_device != nullptr) {
    RenderThreadOutSetRenderTarget(g_device, 0, g_game_target);
    RenderThreadOutSetDepthStencil(g_device, g_game_depth);
    g_bound = g_game_target;
  }
  if (g_game_target != nullptr) g_game_target->Release();
  if (g_game_depth != nullptr) g_game_depth->Release();
  g_game_target = nullptr;
  g_game_depth = nullptr;
}

bool OneWalkActive() { return g_active; }

int OneWalkModeNow() { return Mode(); }

float OneWalkShiftPerRhw() { return g_active ? PerRhwNow() : g_per_rhw; }

void OneWalkHoldShift(bool hold, float shift) {
  g_shift_held = hold;
  g_held_shift = shift;
  if (!D3D7ThreadActive() ||
      (hold == g_posted_held && (!hold || shift == g_posted_shift))) {
    return;
  }
  uint8_t data[8] = {static_cast<uint8_t>(hold ? 1 : 0), 0, 0, 0};
  memcpy(data + 4, &shift, sizeof(float));
  if (D3D7ThreadPost(&ApplyHold, data, sizeof(data))) {
    g_posted_held = hold;
    g_posted_shift = shift;
  }
}

void OneWalkSetMode(int mode) {
  // A batch of draw_merge goes out with the mode of the draw calls in it.
  if (mode != g_mode) DrawMergeFlush();
  g_mode = mode;
  if (D3D7ThreadActive() && mode != g_posted_mode &&
      D3D7ThreadPost(&ApplyMode, &mode, sizeof(mode))) {
    g_posted_mode = mode;
  }
}

void OneWalkPostState() {
  WorkerState state = {g_mode, g_shift_held, g_held_shift, {}};
  RhwInputs(state.inputs);
  memcpy(g_posted_inputs, state.inputs, sizeof(g_posted_inputs));
  g_posted_mode = g_mode;
  g_posted_held = g_shift_held;
  g_posted_shift = g_held_shift;
  if (!D3D7ThreadPost(&ApplyState, &state, sizeof(state))) {
    memcpy(&g_worker, &state, sizeof(state));
  }
}

void OneWalkBeforeQueuedDraw() {
  float inputs[4];
  RhwInputs(inputs);
  if (memcmp(inputs, g_posted_inputs, sizeof(inputs)) == 0) return;
  memcpy(g_posted_inputs, inputs, sizeof(inputs));
  D3D7ThreadPost(&ApplyInputs, inputs, sizeof(inputs));
}

void* OneWalkTarget() { return g_wide; }

void* OneWalkBoundTarget() { return g_active ? g_bound : nullptr; }

bool OneWalkEyeRect(int eye, long rect[4]) {
  if (g_wide == nullptr || eye < 0 || eye > 1) return false;
  rect[0] = static_cast<long>(eye * g_eye_width);
  rect[1] = 0;
  rect[2] = static_cast<long>((eye + 1) * g_eye_width);
  rect[3] = static_cast<long>(g_eye_height);
  return true;
}

void* OneWalkGameSurface(void* surface) {
  if (surface != nullptr && surface == g_wide && g_last_game_target != nullptr) {
    return g_last_game_target;
  }
  return surface;
}

// The picture of one eye of the last pass, for a file. Slow (a copy to
// system memory), so only for a frame that saves pictures. Call
// OneWalkReadDone after each eye.
const void* OneWalkReadEye(int eye, int* pitch, uint32_t* width,
                           uint32_t* height) {
  if (g_device == nullptr || g_wide == nullptr || eye < 0 || eye > 1 ||
      g_read_locked) {
    return nullptr;
  }
  if (g_read_target == nullptr &&
      FAILED(g_device->CreateRenderTarget(g_eye_width, g_eye_height, g_format,
                                          D3DMULTISAMPLE_NONE, 0, FALSE,
                                          &g_read_target, nullptr))) {
    g_read_target = nullptr;
    return nullptr;
  }
  if (g_read_memory == nullptr &&
      FAILED(g_device->CreateOffscreenPlainSurface(
          g_eye_width, g_eye_height, g_format, D3DPOOL_SYSTEMMEM,
          &g_read_memory, nullptr))) {
    g_read_memory = nullptr;
    return nullptr;
  }
  RECT half = {static_cast<LONG>(eye * g_eye_width), 0,
               static_cast<LONG>((eye + 1) * g_eye_width),
               static_cast<LONG>(g_eye_height)};
  if (FAILED(g_device->StretchRect(g_wide, &half, g_read_target, nullptr,
                                   D3DTEXF_NONE)) ||
      FAILED(g_device->GetRenderTargetData(g_read_target, g_read_memory))) {
    return nullptr;
  }
  D3DLOCKED_RECT locked;
  if (FAILED(g_read_memory->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
    return nullptr;
  }
  g_read_locked = true;
  *pitch = locked.Pitch;
  *width = g_eye_width;
  *height = g_eye_height;
  return locked.pBits;
}

void OneWalkReadDone() {
  if (g_read_locked && g_read_memory != nullptr) g_read_memory->UnlockRect();
  g_read_locked = false;
}

// The device is gone, and the objects on it with it. Drop, do not release.
void OneWalkForgetDevice() {
  g_read_target = nullptr;
  g_read_memory = nullptr;
  g_read_locked = false;
  g_wide = nullptr;
  g_wide_depth = nullptr;
  g_device = nullptr;
  g_game_target = nullptr;
  g_game_depth = nullptr;
  g_last_game_target = nullptr;
  g_last_game_depth = nullptr;
  g_bound = nullptr;
  g_active = false;
  g_eye_width = 0;
  g_eye_height = 0;
  g_make_failed = false;
}

void OneWalkReport(uint64_t frames) {
  if (g_passes == 0) return;
  const double per = static_cast<double>(g_passes);
  // backend=vulkan: the renderer draws the right eye. Its report has the
  // draws (vk_scene.cpp), and the counts of the hooks here stay 0.
  if (VkSceneDirect()) {
    ModLog("one walk     : %llu of %llu frames in one walk. The Vulkan "
           "renderer draws each batch into both eyes (backend=vulkan)",
           g_passes, frames);
    g_passes = 0;
    return;
  }
  // The right copies that the render thread made (render_thread_right_eye).
  RenderThreadRightEyeCounts later;
  RenderThreadTakeRightEyeCounts(&later);
  g_world_batches += later.world;
  g_flat_batches += later.flat;
  g_sky_batches += later.sky;
  g_vertices += later.vertices;
  g_own_halves += later.own_halves;
  g_merged += later.own_halves;
  ModLog("one walk     : %llu of %llu frames in one walk. A frame: %.0f world "
         "batches, %.0f flat, %.0f sky, %.0f interface for the right eye only, "
         "%.0f vertices, the right eye %.2f ms of the frame thread",
         g_passes, frames, g_world_batches / per, g_flat_batches / per,
         g_sky_batches / per, g_right_only / per, g_vertices / per,
         Ms(g_right_ticks) / per);
  if (g_not_screen_space > 0 || g_buffer_draws > 0 || g_late_swaps > 0) {
    ModLog("one walk     : not in the right eye: %llu draws that are not "
           "screen space, %llu draws from a vertex buffer. %llu late puts "
           "back of the wide target were turned to the game's target",
           g_not_screen_space, g_buffer_draws, g_late_swaps);
  }
  ModLog("one walk     : %.1f clears and %.1f binds of the game's target "
         "turned wide in a frame, %.0f interface batches for the left eye only",
         g_clears / per, g_target_swaps / per, g_left_only / per);
  ModLog("one walk     : one_walk_merge=%d. %.0f batches of a frame in both "
         "eyes, %.0f of them with each copy in its own half, %.0f of them in "
         "one draw for both eyes",
         g_merge ? 1 : 0, g_both_batches / per, g_own_halves / per,
         g_merged / per);
  g_both_batches = g_own_halves = g_merged = 0;
  g_passes = 0;
  g_world_batches = g_flat_batches = g_sky_batches = 0;
  g_right_only = g_left_only = g_not_screen_space = g_buffer_draws = 0;
  g_clears = g_vertices = g_right_ticks = g_target_swaps = g_late_swaps = 0;
}

void OneWalkSetMerge(bool on) {
  g_merge = on;
  ModLog("one walk     : one_walk_merge=%d. %s", on ? 1 : 0,
         on ? "A batch whose two copies are in their own halves is one draw "
              "for both eyes, with the render thread"
            : "Each batch is one draw for each eye");
}

void OneWalkMergeToggle(const char* keys) {
  g_merge = !g_merge;
  ControllersGesturePulse(0.5f, g_merge ? 0.30f : 0.08f);
  ModLog("test key     : %s, one_walk_merge is %d from the next draw", keys,
         g_merge ? 1 : 0);
}
