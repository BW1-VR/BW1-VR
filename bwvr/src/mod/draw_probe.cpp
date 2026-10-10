// Find which draw calls break the stereo invariant.
//
// THE INVARIANT
//
// The two eye passes change exactly one number: m[9] of the world to camera
// matrix. LH3DTech::ProjectPoint computes
//
//     cam.x = p.x*m[0] + p.y*m[3] + p.z*m[6] + m[9]
//     cam.y = p.x*m[1] + p.y*m[4] + p.z*m[7] + m[10]
//     cam.z = p.x*m[2] + p.y*m[5] + p.z*m[8] + m[11]
//
// so m[9] can only move cam.x. For a point at a FIXED world position, cam.y
// and cam.z are therefore identical in both passes, and so are the projected
// screen y and the rhw that comes from cam.z. Only screen x may differ.
//
// That gives an exact test, with no guessing and no tuning:
//
//     screen y or rhw differs between the passes
//         -> the object had a DIFFERENT WORLD POSITION in the second pass.
//            It was rebuilt from the camera, and that shows as ghosting.
//
//     screen x is identical between the passes
//         -> the object has no disparity at all. It is screen space, which
//            means it is the flat interface, and it needs shifting.
//
//     the vertices hold still but the TEXTURE COORDINATES move
//         -> the geometry is right and what is painted on it is not. A
//            projected shadow works this way: the shadow is a texture on the
//            ground, and a wrong shadow matrix slides that texture across
//            ground that never moved. Comparing positions alone cannot see
//            this.
//
//     the vertex COLOURS move
//         -> per vertex lighting was computed from the camera.
//
// Everything else is ordinary stereo and is already correct.
//
// WHERE IT MEASURES
//
// At the Direct3D device draw methods, not at the engine draw functions.
// LH3DRender has several entry points and the interface uses more than one of
// them, so a hook on one of them can split a checkbox in half. Every triangle
// in the frame reaches the device, so a hook there cannot miss a path. The
// probe records which methods carry traffic, so the list of paths stops being
// an assumption.
//
// The device vtable gives the addresses, but the hook goes on the FUNCTIONS,
// through MinHook, not on the vtable entries. Swapping vtable entries catches
// almost no calls. The game reaches the same implementation through an
// interface pointer this code never sees, and `ddraw.dll` and `d3dim700.dll`
// are both already proxied here, so there is more than one vtable in play.
// Patching the function itself catches every caller whatever vtable they hold.
//
// The caller return address goes into each record. That is what turns "some
// shadows are wrong" into "this function drew them", because the address
// resolves against the symbol file:
//
//     python bwvr/tools/disasm.py <address>
//
// Armed from bwvr.ini, so it costs nothing when it is off:
//
//     [bwvr]
//     draw_probe=1
//     draw_probe_frames=120
//
// It writes bwvr-draw-probe.txt beside runblack.exe.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <share.h>
#include <intrin.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "frame_profile.h"

extern void ModLog(const char* format, ...);

// draw_hooks.cpp: the address inside the subsystem that asked for this draw.
// The four call sites into Direct3D all live inside LH3DRender, so they name
// the renderer rather than the thing that went wrong. One level up does.
uint32_t DrawHooksCaller();

namespace {

// IDirect3DDevice7 vtable slots. These are every method that can put triangles
// on the screen, so nothing gets through unrecorded.
const int kSlotDrawPrimitive = 25;
const int kSlotDrawIndexedPrimitive = 26;
const int kSlotDrawPrimitiveStrided = 29;
const int kSlotDrawIndexedPrimitiveStrided = 30;
const int kSlotDrawPrimitiveVB = 31;
const int kSlotDrawIndexedPrimitiveVB = 32;
const int kVtableSlots = 49;

const int kMaxBatches = 8192;
const int kMaxCallers = 512;

// One draw call, reduced to the few numbers the test needs.
struct Batch {
  uint32_t caller;
  uint32_t fvf;
  uint32_t count;
  uint32_t slot;
  // Sums are enough. A change of world position moves the sum, and summing
  // instead of copying whole vertex arrays keeps the probe cheap enough to
  // leave running for a hundred frames.
  double x_sum, y_sum, z_sum, rhw_sum;
  double u_sum, v_sum;
  double colour_sum;
  float z_min, z_max, rhw_min, rhw_max;
};

// What the probe learned about one calling function, across every frame
// measured.
struct Caller {
  uint32_t address;
  uint64_t slots_used;      // one bit per vtable slot
  uint64_t batches;
  uint64_t rebuilt;         // y or rhw moved: the geometry was rebuilt
  uint64_t retextured;      // vertices held, texture coordinates moved
  uint64_t relit;           // vertices held, vertex colours moved
  uint64_t flat;            // x identical: screen space interface
  uint64_t stereo;          // x moved, y and rhw held: correct
  uint64_t count_mismatch;  // a different vertex count in the two passes
  double worst_dy;
  double worst_drhw;
  double worst_duv;
  // What z and rhw the flat batches carried. This is how a per batch interface
  // test gets found. A list of interface functions cannot work when one
  // function draws both the world and the interface, but a per batch test can.
  float flat_z_min, flat_z_max, flat_rhw_min, flat_rhw_max;
  float solid_z_min, solid_z_max, solid_rhw_min, solid_rhw_max;
};

Batch g_pass[2][kMaxBatches];
int g_pass_count[2] = {0, 0};
Caller g_callers[kMaxCallers];
int g_caller_count = 0;

bool g_armed = false;
bool g_installed = false;

// PROFILING, which answers a different question from the probe.
//
// "Why does a 2001 game only reach 176 frames per second on an RTX 3090" has
// two possible answers and they point opposite ways:
//
//   the game's own code   LH3DTech transforms every vertex on the CPU, in
//                         scalar x87 written for a Pentium III. Nothing about
//                         a modern graphics API touches that.
//
//   the layer underneath  DirectDraw and Direct3D 7 do not exist on Windows
//                         11. They are emulated over something newer, and
//                         every draw call pays for the translation. A modern
//                         backend would remove exactly this.
//
// So this times the inside of the Direct3D draw calls and compares it with the
// whole scene walk. A large share means the emulation is the problem and the
// pivot is worth it. A small share means the bottleneck is 2001 CPU code that
// no API swap can reach.
bool g_profiling = false;
LARGE_INTEGER g_profile_frequency = {0};
uint64_t g_d3d_ticks = 0;
uint64_t g_d3d_calls = 0;
uint64_t g_d3d_vertices = 0;
uint64_t g_d3d_indices = 0;
int g_frames_left = 0;
int g_recording_pass = -1;
uint64_t g_frames_done = 0;
uint64_t g_slots_seen = 0;
uint64_t g_non_xyzrhw = 0;
uint64_t g_overflow = 0;
uint64_t g_calls_seen = 0;
uint64_t g_recorded = 0;
uint64_t g_armed_frames = 0;
int g_delay_left = 0;

void** g_vtable = nullptr;
void* g_original[kVtableSlots] = {nullptr};
void* g_target[kVtableSlots] = {nullptr};

// Bytes per vertex for a fixed function FVF.
uint32_t FvfStride(uint32_t fvf) {
  uint32_t size = 0;
  switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ: size += 12; break;
    case D3DFVF_XYZRHW: size += 16; break;
    case D3DFVF_XYZB1: size += 16; break;
    case D3DFVF_XYZB2: size += 20; break;
    case D3DFVF_XYZB3: size += 24; break;
    case D3DFVF_XYZB4: size += 28; break;
    case D3DFVF_XYZB5: size += 32; break;
    default: return 0;
  }
  if (fvf & D3DFVF_NORMAL) size += 12;
  if (fvf & D3DFVF_DIFFUSE) size += 4;
  if (fvf & D3DFVF_SPECULAR) size += 4;
  const uint32_t textures =
      (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  size += textures * 8;
  return size;
}

Caller* FindCaller(uint32_t address) {
  for (int i = 0; i < g_caller_count; ++i) {
    if (g_callers[i].address == address) return &g_callers[i];
  }
  if (g_caller_count >= kMaxCallers) return nullptr;
  Caller* fresh = &g_callers[g_caller_count++];
  memset(fresh, 0, sizeof(*fresh));
  fresh->address = address;
  fresh->flat_z_min = 1e30f;
  fresh->flat_rhw_min = 1e30f;
  fresh->flat_z_max = -1e30f;
  fresh->flat_rhw_max = -1e30f;
  fresh->solid_z_min = 1e30f;
  fresh->solid_rhw_min = 1e30f;
  fresh->solid_z_max = -1e30f;
  fresh->solid_rhw_max = -1e30f;
  return fresh;
}

void Record(uint32_t caller, uint32_t slot, uint32_t fvf, const void* verts,
            uint32_t count) {
  ++g_calls_seen;
  g_slots_seen |= (1ull << slot);
  if (g_recording_pass < 0 || verts == nullptr || count == 0) return;
  const int pass = g_recording_pass;
  if (g_pass_count[pass] >= kMaxBatches) {
    ++g_overflow;
    return;
  }

  // Only XYZRHW carries a screen position to compare. The engine transforms on
  // the CPU, so everything should be XYZRHW. A count here would say otherwise,
  // and that would be worth knowing on its own.
  if ((fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW) {
    ++g_non_xyzrhw;
    return;
  }
  const uint32_t stride = FvfStride(fvf);
  if (stride == 0) return;
  if (IsBadReadPtr(verts, static_cast<size_t>(stride) * count)) return;

  // Where the first texture coordinate pair and the diffuse colour sit inside
  // this vertex. For FVF 0x1C4 that is 24 and 16, but computing it keeps the
  // probe honest if another FVF turns up.
  uint32_t colour_offset = 16;
  uint32_t uv_offset = 16;
  if (fvf & D3DFVF_NORMAL) {
    colour_offset += 12;
    uv_offset += 12;
  }
  const bool has_diffuse = (fvf & D3DFVF_DIFFUSE) != 0;
  if (has_diffuse) uv_offset += 4;
  if (fvf & D3DFVF_SPECULAR) uv_offset += 4;
  const bool has_uv =
      ((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT) > 0;

  ++g_recorded;
  Batch* batch = &g_pass[pass][g_pass_count[pass]++];
  batch->caller = caller;
  batch->fvf = fvf;
  batch->count = count;
  batch->slot = slot;
  batch->x_sum = 0.0;
  batch->y_sum = 0.0;
  batch->z_sum = 0.0;
  batch->rhw_sum = 0.0;
  batch->u_sum = 0.0;
  batch->v_sum = 0.0;
  batch->colour_sum = 0.0;
  batch->z_min = 1e30f;
  batch->rhw_min = 1e30f;
  batch->z_max = -1e30f;
  batch->rhw_max = -1e30f;

  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    const float* p = reinterpret_cast<const float*>(vertex);
    batch->x_sum += p[0];
    batch->y_sum += p[1];
    batch->z_sum += p[2];
    batch->rhw_sum += p[3];
    if (p[2] < batch->z_min) batch->z_min = p[2];
    if (p[2] > batch->z_max) batch->z_max = p[2];
    if (p[3] < batch->rhw_min) batch->rhw_min = p[3];
    if (p[3] > batch->rhw_max) batch->rhw_max = p[3];
    if (has_uv) {
      const float* uv = reinterpret_cast<const float*>(vertex + uv_offset);
      batch->u_sum += uv[0];
      batch->v_sum += uv[1];
    }
    if (has_diffuse) {
      batch->colour_sum += static_cast<double>(
          *reinterpret_cast<const uint32_t*>(vertex + colour_offset));
    }
    vertex += stride;
  }
}

// The vtable thunks. Each records, then chains to the real method.
typedef HRESULT(WINAPI* DrawPrimitiveFn)(void*, D3DPRIMITIVETYPE, DWORD,
                                         LPVOID, DWORD, DWORD);
typedef HRESULT(WINAPI* DrawIndexedPrimitiveFn)(void*, D3DPRIMITIVETYPE, DWORD,
                                                LPVOID, DWORD, LPWORD, DWORD,
                                                DWORD);
typedef HRESULT(WINAPI* DrawPrimitiveVBFn)(void*, D3DPRIMITIVETYPE, void*,
                                           DWORD, DWORD, DWORD);
typedef HRESULT(WINAPI* DrawPrimitiveStridedFn)(void*, D3DPRIMITIVETYPE, DWORD,
                                                void*, DWORD, DWORD);
typedef HRESULT(WINAPI* DrawIndexedPrimitiveStridedFn)(void*, D3DPRIMITIVETYPE,
                                                       DWORD, void*, DWORD,
                                                       LPWORD, DWORD, DWORD);
typedef HRESULT(WINAPI* DrawIndexedPrimitiveVBFn)(void*, D3DPRIMITIVETYPE,
                                                  void*, DWORD, DWORD, LPWORD,
                                                  DWORD, DWORD);

// Prefer the subsystem that asked for the draw. Fall back to the call site
// inside LH3DRender when there is no hook above us, which happens for the
// full screen overlay quad that FinishFrame submits directly.
uint32_t CallerAddress() {
  const uint32_t engine = DrawHooksCaller();
  return engine != 0 ? engine : reinterpret_cast<uint32_t>(_ReturnAddress());
}

HRESULT WINAPI h_DrawPrimitive(void* self, D3DPRIMITIVETYPE type, DWORD fvf,
                               LPVOID verts, DWORD count, DWORD flags) {
  if (g_armed) Record(CallerAddress(), kSlotDrawPrimitive, fvf, verts, count);
  if (!g_profiling) {
    return reinterpret_cast<DrawPrimitiveFn>(g_original[kSlotDrawPrimitive])(
        self, type, fvf, verts, count, flags);
  }
  LARGE_INTEGER a, b;
  QueryPerformanceCounter(&a);
  const HRESULT hr =
      reinterpret_cast<DrawPrimitiveFn>(g_original[kSlotDrawPrimitive])(
          self, type, fvf, verts, count, flags);
  QueryPerformanceCounter(&b);
  g_d3d_ticks += b.QuadPart - a.QuadPart;
  ++g_d3d_calls;
  g_d3d_vertices += count;
  FrameProfileAdd(kFpD3d, b.QuadPart - a.QuadPart);
  FrameProfileCount(kFpcDraws, 1);
  FrameProfileCount(kFpcVertices, count);
  return hr;
}

HRESULT WINAPI h_DrawIndexedPrimitive(void* self, D3DPRIMITIVETYPE type,
                                      DWORD fvf, LPVOID verts, DWORD count,
                                      LPWORD indices, DWORD index_count,
                                      DWORD flags) {
  if (g_armed) {
    Record(CallerAddress(), kSlotDrawIndexedPrimitive, fvf, verts, count);
  }
  if (!g_profiling) {
    return reinterpret_cast<DrawIndexedPrimitiveFn>(
        g_original[kSlotDrawIndexedPrimitive])(self, type, fvf, verts, count,
                                               indices, index_count, flags);
  }
  LARGE_INTEGER a, b;
  QueryPerformanceCounter(&a);
  const HRESULT hr = reinterpret_cast<DrawIndexedPrimitiveFn>(
      g_original[kSlotDrawIndexedPrimitive])(self, type, fvf, verts, count,
                                             indices, index_count, flags);
  QueryPerformanceCounter(&b);
  g_d3d_ticks += b.QuadPart - a.QuadPart;
  ++g_d3d_calls;
  g_d3d_vertices += count;
  g_d3d_indices += index_count;
  FrameProfileAdd(kFpD3d, b.QuadPart - a.QuadPart);
  FrameProfileCount(kFpcDraws, 1);
  FrameProfileCount(kFpcVertices, count);
  return hr;
}

// The vertex buffer paths keep their vertices inside a Direct3D buffer, so the
// probe cannot read them without a lock. It still counts them, because the one
// thing that must not happen is a path carrying traffic unnoticed.
HRESULT WINAPI h_DrawPrimitiveVB(void* self, D3DPRIMITIVETYPE type, void* vb,
                                 DWORD start, DWORD count, DWORD flags) {
  g_slots_seen |= (1ull << kSlotDrawPrimitiveVB);
  return reinterpret_cast<DrawPrimitiveVBFn>(g_original[kSlotDrawPrimitiveVB])(
      self, type, vb, start, count, flags);
}

HRESULT WINAPI h_DrawIndexedPrimitiveVB(void* self, D3DPRIMITIVETYPE type,
                                        void* vb, DWORD start, DWORD count,
                                        LPWORD indices, DWORD index_count,
                                        DWORD flags) {
  g_slots_seen |= (1ull << kSlotDrawIndexedPrimitiveVB);
  return reinterpret_cast<DrawIndexedPrimitiveVBFn>(
      g_original[kSlotDrawIndexedPrimitiveVB])(self, type, vb, start, count,
                                               indices, index_count, flags);
}

HRESULT WINAPI h_DrawPrimitiveStrided(void* self, D3DPRIMITIVETYPE type,
                                      DWORD fvf, void* data, DWORD count,
                                      DWORD flags) {
  g_slots_seen |= (1ull << kSlotDrawPrimitiveStrided);
  return reinterpret_cast<DrawPrimitiveStridedFn>(
      g_original[kSlotDrawPrimitiveStrided])(self, type, fvf, data, count,
                                             flags);
}

HRESULT WINAPI h_DrawIndexedPrimitiveStrided(void* self, D3DPRIMITIVETYPE type,
                                             DWORD fvf, void* data,
                                             DWORD count, LPWORD indices,
                                             DWORD index_count, DWORD flags) {
  g_slots_seen |= (1ull << kSlotDrawIndexedPrimitiveStrided);
  return reinterpret_cast<DrawIndexedPrimitiveStridedFn>(
      g_original[kSlotDrawIndexedPrimitiveStrided])(
      self, type, fvf, data, count, indices, index_count, flags);
}

// Put a MinHook on one vtable slot, by its function address.
bool HookSlot(int slot, void* replacement, const char* name) {
  void* target = g_vtable[slot];
  if (target == nullptr) return false;
  g_target[slot] = target;
  MH_STATUS status = MH_CreateHook(target, replacement, &g_original[slot]);
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("draw probe   : %s at 0x%08X %s", name,
         reinterpret_cast<uintptr_t>(target),
         status == MH_OK ? "ok" : "FAILED");
  return status == MH_OK;
}

bool Install() {
  void** device = reinterpret_cast<void**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(device, sizeof(void*)) || *device == nullptr) return false;
  void* object = *device;
  if (IsBadReadPtr(object, sizeof(void*))) return false;
  g_vtable = *reinterpret_cast<void***>(object);
  if (IsBadReadPtr(g_vtable, sizeof(void*) * kVtableSlots)) return false;

  ModLog("draw probe   : device 0x%08X, vtable 0x%08X",
         reinterpret_cast<uintptr_t>(object),
         reinterpret_cast<uintptr_t>(g_vtable));

  bool any = false;
  any |= HookSlot(kSlotDrawPrimitive, &h_DrawPrimitive, "DrawPrimitive");
  any |= HookSlot(kSlotDrawIndexedPrimitive, &h_DrawIndexedPrimitive,
                  "DrawIndexedPrimitive");
  any |= HookSlot(kSlotDrawPrimitiveStrided, &h_DrawPrimitiveStrided,
                  "DrawPrimitiveStrided");
  any |= HookSlot(kSlotDrawIndexedPrimitiveStrided,
                  &h_DrawIndexedPrimitiveStrided,
                  "DrawIndexedPrimitiveStrided");
  any |= HookSlot(kSlotDrawPrimitiveVB, &h_DrawPrimitiveVB, "DrawPrimitiveVB");
  any |= HookSlot(kSlotDrawIndexedPrimitiveVB, &h_DrawIndexedPrimitiveVB,
                  "DrawIndexedPrimitiveVB");
  return any;
}

// Compare the two passes, caller by caller, and add the verdict to the totals.
void Compare() {
  // Walk pass 0 and pass 1 together, per caller, in call order. Matching on
  // the caller rather than on a global index survives a small difference in
  // what the two eyes cull.
  int cursor[kMaxCallers];
  memset(cursor, 0, sizeof(cursor));

  for (int a = 0; a < g_pass_count[0]; ++a) {
    const Batch& left = g_pass[0][a];
    Caller* record = FindCaller(left.caller);
    if (record == nullptr) continue;
    record->slots_used |= (1ull << left.slot);
    const int index = static_cast<int>(record - g_callers);

    // The next batch in pass 1 from the same caller that is not yet matched.
    const Batch* right = nullptr;
    for (int b = cursor[index]; b < g_pass_count[1]; ++b) {
      if (g_pass[1][b].caller == left.caller) {
        right = &g_pass[1][b];
        cursor[index] = b + 1;
        break;
      }
    }
    ++record->batches;
    if (right == nullptr || right->count != left.count) {
      ++record->count_mismatch;
      continue;
    }

    const double n = static_cast<double>(left.count);
    const double dy = fabs(right->y_sum - left.y_sum) / n;
    const double dx = fabs(right->x_sum - left.x_sum) / n;

    // rhw has to be compared in proportion, not in absolute terms. It is
    // near/camera_z, so it runs from about 0.0001 far away to over 6000 for a
    // vertex almost touching the near plane. A fixed epsilon calls float
    // rounding on the large values a rebuild.
    const double scale = fabs(left.rhw_sum) / n;
    const double drhw = fabs(right->rhw_sum - left.rhw_sum) / n;
    const double drhw_rel = drhw / (scale > 1e-9 ? scale : 1e-9);

    // A quarter of a pixel of average movement in y is far more than float
    // rounding and far less than any real rebuild, so it separates the two
    // cleanly.
    const bool moved_in_world = (dy > 0.25) || (drhw_rel > 1e-4);
    if (dy > record->worst_dy) record->worst_dy = dy;
    if (drhw_rel > record->worst_drhw) record->worst_drhw = drhw_rel;

    // Texture coordinates are in the 0 to 1 range for ordinary textures and
    // can run outside it for projected ones, so a thousandth is small enough
    // to be rounding and large enough to be real.
    const double du = fabs(right->u_sum - left.u_sum) / n;
    const double dv = fabs(right->v_sum - left.v_sum) / n;
    const double duv = du > dv ? du : dv;
    if (duv > record->worst_duv) record->worst_duv = duv;
    const double dcolour = fabs(right->colour_sum - left.colour_sum) / n;

    if (moved_in_world) {
      ++record->rebuilt;
    } else if (duv > 1e-3) {
      // The geometry held still and the picture painted on it did not. This is
      // the projected shadow case.
      ++record->retextured;
    } else if (dcolour > 0.5) {
      ++record->relit;
    } else if (dx < 1e-4) {
      ++record->flat;
      if (left.z_min < record->flat_z_min) record->flat_z_min = left.z_min;
      if (left.z_max > record->flat_z_max) record->flat_z_max = left.z_max;
      if (left.rhw_min < record->flat_rhw_min) {
        record->flat_rhw_min = left.rhw_min;
      }
      if (left.rhw_max > record->flat_rhw_max) {
        record->flat_rhw_max = left.rhw_max;
      }
    } else {
      ++record->stereo;
      if (left.z_min < record->solid_z_min) record->solid_z_min = left.z_min;
      if (left.z_max > record->solid_z_max) record->solid_z_max = left.z_max;
      if (left.rhw_min < record->solid_rhw_min) {
        record->solid_rhw_min = left.rhw_min;
      }
      if (left.rhw_max > record->solid_rhw_max) {
        record->solid_rhw_max = left.rhw_max;
      }
    }
  }
}

void WriteReport() {
  char path[MAX_PATH];
  GetModuleFileNameA(GetModuleHandleA(nullptr), path, MAX_PATH);
  char* slash = strrchr(path, '\\');
  if (slash != nullptr) *(slash + 1) = '\0';
  lstrcatA(path, "bwvr-draw-probe.txt");

  FILE* out = _fsopen(path, "w", _SH_DENYWR);
  if (out == nullptr) {
    ModLog("draw probe   : cannot write %s", path);
    return;
  }

  fprintf(out, "Draw probe, %llu stereo frames measured.\n\n", g_frames_done);
  fprintf(out,
          "The two passes change only m[9] of the camera matrix, so for a "
          "point at a\nfixed world position the projected y and rhw MUST match "
          "between them.\n"
          "  rebuilt : y or rhw moved. The object had a different world "
          "position in\n"
          "            the second pass, so the geometry was rebuilt from the "
          "camera.\n"
          "  retex   : vertices held, texture coordinates moved. A projected "
          "shadow\n"
          "            sliding across ground that never moved looks like "
          "this.\n"
          "  relit   : vertices held, vertex colours moved. Lighting was "
          "computed\n"
          "            from the camera.\n"
          "  flat    : x did not move either. No disparity at all, so it is "
          "screen\n"
          "            space, and that is the interface.\n"
          "  stereo  : x moved, y and rhw held. Correct.\n\n");

  fprintf(out, "Device draw methods that carried traffic:");
  for (int slot = 0; slot < 40; ++slot) {
    if (g_slots_seen & (1ull << slot)) fprintf(out, " %d", slot);
  }
  fprintf(out, "\n  (25 DrawPrimitive, 26 DrawIndexedPrimitive,"
               " 29 and 30 strided,\n"
               "   31 DrawPrimitiveVB, 32 DrawIndexedPrimitiveVB)\n");
  fprintf(out, "Draw calls seen: %llu, recorded: %llu\n", g_calls_seen,
          g_recorded);
  fprintf(out, "Draw calls whose FVF was not XYZRHW: %llu\n", g_non_xyzrhw);
  fprintf(out, "Batches dropped because the buffer filled: %llu\n\n",
          g_overflow);

  fprintf(out, "%-10s %8s %8s %7s %6s %7s %7s %7s %9s %9s\n", "caller",
          "batches", "rebuilt", "retex", "relit", "flat", "stereo", "nomatch",
          "worst dy", "worst duv");
  fprintf(out,
          "----------------------------------------------------------------"
          "--------------------------------\n");

  // Worst first, because the report gets read from the top.
  for (int pick = 0; pick < g_caller_count; ++pick) {
    int best = -1;
    uint64_t best_score = 0;
    for (int i = 0; i < g_caller_count; ++i) {
      if (g_callers[i].batches == 0) continue;
      const uint64_t score = g_callers[i].rebuilt * 1000000000ull +
                             g_callers[i].retextured * 1000000ull +
                             g_callers[i].relit * 100000ull +
                             g_callers[i].flat * 1000ull +
                             g_callers[i].batches;
      if (score > best_score) {
        best_score = score;
        best = i;
      }
    }
    if (best < 0) break;
    Caller& c = g_callers[best];
    fprintf(out, "0x%08X %8llu %8llu %7llu %6llu %7llu %7llu %7llu %9.3f "
                 "%9.3e\n",
            c.address, c.batches, c.rebuilt, c.retextured, c.relit, c.flat,
            c.stereo, c.count_mismatch, c.worst_dy, c.worst_duv);
    c.batches = 0;  // consumed, so the next pick finds the one after it
  }

  fprintf(out,
          "\n\nScreen space ranges, for finding a per batch interface test\n");
  fprintf(out, "%-10s %-36s %-36s\n", "caller", "flat: z and rhw",
          "stereo: z and rhw");
  for (int i = 0; i < g_caller_count; ++i) {
    const Caller& c = g_callers[i];
    if (c.flat == 0 && c.stereo == 0) continue;
    char flat_text[48] = "-";
    char solid_text[48] = "-";
    if (c.flat > 0) {
      _snprintf_s(flat_text, sizeof(flat_text), _TRUNCATE,
                  "z %.4f..%.4f rhw %.5f..%.5f", c.flat_z_min, c.flat_z_max,
                  c.flat_rhw_min, c.flat_rhw_max);
    }
    if (c.stereo > 0) {
      _snprintf_s(solid_text, sizeof(solid_text), _TRUNCATE,
                  "z %.4f..%.4f rhw %.5f..%.5f", c.solid_z_min, c.solid_z_max,
                  c.solid_rhw_min, c.solid_rhw_max);
    }
    fprintf(out, "0x%08X %-36s %-36s\n", c.address, flat_text, solid_text);
  }

  fprintf(out, "\nResolve a caller with:\n"
               "  python bwvr/tools/disasm.py 0x????????\n");
  fclose(out);
  ModLog("draw probe   : %llu calls seen, %llu recorded, %llu frames compared",
         g_calls_seen, g_recorded, g_frames_done);
  ModLog("draw probe   : report written to %s", path);
}

}  // namespace

// Turn on profiling and make sure the device hooks exist. Returns false if
// the device is not ready yet, so the caller can try again.
bool DrawProbeProfile(bool on) {
  if (on && !g_installed) {
    g_installed = Install();
    if (!g_installed) return false;
  }
  g_profiling = on;
  if (on && g_profile_frequency.QuadPart == 0) {
    QueryPerformanceFrequency(&g_profile_frequency);
  }
  return true;
}

// Milliseconds spent inside Direct3D draw calls since the last report, plus
// the geometry that went through them. Resets the counters.
void DrawProbeProfileTake(double* d3d_ms, uint64_t* calls, uint64_t* vertices,
                          uint64_t* indices, uint64_t frames) {
  *d3d_ms = 0.0;
  *calls = g_d3d_calls;
  *vertices = g_d3d_vertices;
  *indices = g_d3d_indices;
  if (g_profile_frequency.QuadPart != 0 && frames > 0) {
    *d3d_ms = (static_cast<double>(g_d3d_ticks) /
               static_cast<double>(g_profile_frequency.QuadPart)) *
              1000.0 / static_cast<double>(frames);
  }
  if (frames > 0) {
    *calls /= frames;
    *vertices /= frames;
    *indices /= frames;
  }
  g_d3d_ticks = 0;
  g_d3d_calls = 0;
  g_d3d_vertices = 0;
  g_d3d_indices = 0;
}

void DrawProbeArm(bool on, int frames) {
  g_armed = on;
  g_frames_left = frames;
  if (on) ModLog("draw probe   : armed for %d stereo frames", frames);
}

// Wait this many drawing frames before recording anything. The mod starts
// before the island does, and measuring the loading screen and the opening
// camera move says nothing about villagers, shadows or smoke.
void DrawProbeDelay(int frames) {
  g_delay_left = frames;
  if (frames > 0) {
    ModLog("draw probe   : waiting %d frames for the scene to settle", frames);
  }
}

// stereo.cpp calls this at the top of each eye pass, and with -1 once the two
// passes are over.
void DrawProbeBeginPass(int eye) {
  if (!g_armed) return;
  if (!g_installed) {
    g_installed = Install();
    if (!g_installed) return;
  }
  if (eye < 0) {
    g_recording_pass = -1;
    ++g_armed_frames;
    if (g_delay_left > 0 && g_pass_count[0] > 0 && g_pass_count[1] > 0) {
      --g_delay_left;
      g_pass_count[0] = 0;
      g_pass_count[1] = 0;
      if (g_delay_left == 0) ModLog("draw probe   : recording now");
      return;
    }
    // Only a frame that drew something counts. The probe arms as the mod
    // loads, and the loading screen draws almost nothing, so counting every
    // frame would spend the whole measurement before the island appears.
    if (g_pass_count[0] > 0 && g_pass_count[1] > 0) {
      Compare();
      ++g_frames_done;
      if (--g_frames_left <= 0) {
        g_armed = false;
        WriteReport();
      }
    } else if (g_armed_frames > 20000) {
      // Still nothing after a long wait. Report anyway. A probe that stays
      // silent looks exactly like a probe that found nothing wrong.
      g_armed = false;
      WriteReport();
    }
    g_pass_count[0] = 0;
    g_pass_count[1] = 0;
    return;
  }
  g_recording_pass = (eye == 0) ? 0 : 1;
  g_pass_count[g_recording_pass] = 0;
}
