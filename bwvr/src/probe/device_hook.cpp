// Intercepts IDirect3DDevice7::SetTransform on the device vtable.
//
// This is the last point before the driver. Whatever the engine does
// internally, if it uses hardware transforms at all then the world, view and
// projection matrices pass through here, and changing them here changes what
// is drawn.
//
// Writing LH3DRender::g_d3d_world2view at StartFrame turned out not to affect
// rendering, so the engine must set its transforms later in the frame. This
// hook finds out what it actually sets, and lets us prove camera control with
// an unmistakable test: a 180 degree roll should turn the picture upside down.

#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

namespace {

// IDirect3DDevice7 vtable layout, verified against d3d.h:
//   0 QueryInterface, 1 AddRef, 2 Release, 3 GetCaps, 4 EnumTextureFormats,
//   5 BeginScene, 6 EndScene, 7 GetDirect3D, 8 SetRenderTarget,
//   9 GetRenderTarget, 10 Clear, 11 SetTransform
const int kSetTransformSlot = 11;

typedef HRESULT(__stdcall* SetTransformFn)(IDirect3DDevice7*,
                                           D3DTRANSFORMSTATETYPE, LPD3DMATRIX);
SetTransformFn o_SetTransform = nullptr;

void (*g_log)(const char*, ...) = nullptr;

float g_roll_degrees = 0.0f;
float g_eye_offset = 0.0f;

uint64_t g_counts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
bool g_logged[8] = {false, false, false, false, false, false, false, false};
D3DMATRIX g_first_view;
bool g_have_first_view = false;
uint64_t g_view_changed = 0;  // how often VIEW differed from the first one

const char* TransformName(D3DTRANSFORMSTATETYPE type) {
  switch (type) {
    case D3DTRANSFORMSTATE_WORLD: return "WORLD";
    case D3DTRANSFORMSTATE_VIEW: return "VIEW";
    case D3DTRANSFORMSTATE_PROJECTION: return "PROJECTION";
    case D3DTRANSFORMSTATE_WORLD1: return "WORLD1";
    case D3DTRANSFORMSTATE_WORLD2: return "WORLD2";
    case D3DTRANSFORMSTATE_WORLD3: return "WORLD3";
    default: return "other";
  }
}

void LogMatrix(const char* label, const D3DMATRIX* m) {
  if (g_log == nullptr || m == nullptr) return;
  g_log("  %s:", label);
  g_log("    [%9.4f %9.4f %9.4f %9.4f]", m->_11, m->_12, m->_13, m->_14);
  g_log("    [%9.4f %9.4f %9.4f %9.4f]", m->_21, m->_22, m->_23, m->_24);
  g_log("    [%9.4f %9.4f %9.4f %9.4f]", m->_31, m->_32, m->_33, m->_34);
  g_log("    [%9.4f %9.4f %9.4f %9.4f]", m->_41, m->_42, m->_43, m->_44);
}

// In Direct3D's row-vector convention the view matrix holds the camera basis
// in its columns: column 0 is right, column 1 is up, column 2 is forward.
// Rolling rotates right and up about forward.
void ApplyRoll(D3DMATRIX* m, float degrees) {
  const float rad = degrees * 3.14159265358979f / 180.0f;
  const float c = cosf(rad);
  const float s = sinf(rad);

  float* right[3] = {&m->_11, &m->_21, &m->_31};
  float* up[3] = {&m->_12, &m->_22, &m->_32};
  for (int i = 0; i < 3; ++i) {
    const float r = *right[i];
    const float u = *up[i];
    *right[i] = r * c + u * s;
    *up[i] = -r * s + u * c;
  }
}

// Shift the eye along its own right axis. This is exactly what a per-eye
// interpupillary offset is, expressed in the view matrix.
void ApplyEyeOffset(D3DMATRIX* m, float offset) {
  m->_41 += offset;
}

HRESULT __stdcall h_SetTransform(IDirect3DDevice7* self,
                                 D3DTRANSFORMSTATETYPE type, LPD3DMATRIX matrix) {
  const int slot = (type >= 0 && type < 8) ? static_cast<int>(type) : 7;
  ++g_counts[slot];

  if (!g_logged[slot] && g_log != nullptr) {
    g_logged[slot] = true;
    g_log("");
    g_log("SetTransform first call, type %d (%s)", static_cast<int>(type),
          TransformName(type));
    LogMatrix("matrix", matrix);
  }

  if (type == D3DTRANSFORMSTATE_VIEW && matrix != nullptr) {
    if (!g_have_first_view) {
      g_first_view = *matrix;
      g_have_first_view = true;
    } else if (memcmp(&g_first_view, matrix, sizeof(D3DMATRIX)) != 0) {
      ++g_view_changed;
    }

    if (g_roll_degrees != 0.0f) ApplyRoll(matrix, g_roll_degrees);
    if (g_eye_offset != 0.0f) ApplyEyeOffset(matrix, g_eye_offset);

    // Prove the write reached the matrix, once, so a silent no-op cannot
    // be mistaken for the engine ignoring the transform.
    if (g_counts[D3DTRANSFORMSTATE_VIEW] == 2000 && g_log != nullptr) {
      g_log("");
      g_log("VIEW call 2000, after our modification:");
      LogMatrix("modified", matrix);
    }
  }

  return o_SetTransform(self, type, matrix);
}

}  // namespace

void DeviceHookConfigure(float roll_degrees, float eye_offset) {
  g_roll_degrees = roll_degrees;
  g_eye_offset = eye_offset;
}

// Called after OpenD3D, when the device global is populated.
void DeviceHookInstall(void (*log)(const char*, ...)) {
  g_log = log;

  IDirect3DDevice7** slot =
      reinterpret_cast<IDirect3DDevice7**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(IDirect3DDevice7*)) || *slot == nullptr) {
    log("  device hook: no device, cannot hook SetTransform");
    return;
  }

  void** vtable = *reinterpret_cast<void***>(*slot);
  if (IsBadReadPtr(vtable, sizeof(void*) * 32)) {
    log("  device hook: device vtable unreadable");
    return;
  }

  void* target = vtable[kSetTransformSlot];
  log("  device hook: IDirect3DDevice7 vtable at 0x%08X, SetTransform at 0x%08X",
      reinterpret_cast<uintptr_t>(vtable), reinterpret_cast<uintptr_t>(target));

  MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void*>(&h_SetTransform),
                                   reinterpret_cast<void**>(&o_SetTransform));
  if (status != MH_OK) {
    log("  device hook: MH_CreateHook failed (%s)", MH_StatusToString(status));
    return;
  }
  status = MH_EnableHook(target);
  log("  device hook: SetTransform %s (roll %.1f deg, eye offset %.3f)",
      status == MH_OK ? "hooked" : "ENABLE FAILED", g_roll_degrees, g_eye_offset);
}

// Safe to call from the render thread. Reports every so often, because a
// force-killed process never reaches DLL_PROCESS_DETACH.
void DeviceHookTick(uint64_t frame) {
  if (g_log == nullptr) return;
  if (frame != 300 && frame != 1200 && (frame % 3000) != 0) return;
  g_log("");
  g_log("SetTransform counts at frame %llu:", frame);
  for (int i = 0; i < 8; ++i) {
    if (g_counts[i] == 0) continue;
    g_log("  type %d %-11s %llu", i,
          TransformName(static_cast<D3DTRANSFORMSTATETYPE>(i)), g_counts[i]);
  }
  g_log("  VIEW differed from the first value %llu times", g_view_changed);
}

void DeviceHookReport(void (*log)(const char*, ...)) {
  if (o_SetTransform == nullptr) {
    log("SetTransform was never hooked.");
    return;
  }
  log("");
  log("=== SetTransform call counts ===");
  bool any = false;
  for (int i = 0; i < 8; ++i) {
    if (g_counts[i] == 0) continue;
    any = true;
    log("  type %d %-11s %llu", i,
        TransformName(static_cast<D3DTRANSFORMSTATETYPE>(i)), g_counts[i]);
  }
  if (!any) {
    log("  NONE. The engine never set a Direct3D transform, so it transforms");
    log("  vertices itself and hardware transform hooks cannot steer the camera.");
  }
}
