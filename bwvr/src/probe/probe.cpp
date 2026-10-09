// The probe: measurements of how the game draws.
//
// Answers the questions static analysis could not:
//   1. Which draw path does the world actually use, and with which FVF? If
//      geometry arrives as D3DFVF_XYZRHW it is already in screen space.
//   2. Does the engine get a hardware transform and lighting device on a
//      modern GPU?
//   3. What do the view and projection matrices actually contain during play?
//
// It only observes. Nothing here changes what the game draws.

#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>
#include <share.h>
#include <stdarg.h>
#include <stdint.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

bool ProxyLoadRealDDraw();

// Camera control experiment, in camera_poke.cpp.
bool CameraPokeLoad(void (*log)(const char*, ...));
void CameraPokeInstall(void (*log)(const char*, ...));
void CameraPokeReport(void (*log)(const char*, ...));

void CameraPokeOnFirstDraw();
float CameraPokeRoll();
float CameraPokeOffsetX();

// Device vtable hook, in device_hook.cpp.
void DeviceHookConfigure(float roll_degrees, float eye_offset);
void DeviceHookInstall(void (*log)(const char*, ...));
void DeviceHookReport(void (*log)(const char*, ...));
void DeviceHookTick(uint64_t frame);

// tech_camera.cpp: the real camera, LH3DTech::UpdateCamera.
void TechCameraConfigure(float roll_degrees, float eye_offset);
void TechCameraInstall();
void TechCameraReportInfoTransform();
void TechCameraReport();
void TechCameraTick(uint64_t frame);

// draw_phase.cpp: which part of the frame does the drawing.
void DrawPhaseNote(uint32_t vertices);
void DrawPhaseEnterFinish();
void DrawPhaseLeaveFinish();
void DrawPhaseInstall();
void DrawPhaseReport();
void DrawPhaseTick(uint64_t frame);

// frame_grab.cpp: save the render target to a BMP, no window needed.
void FrameGrabLoad();
void FrameGrabTick(uint64_t frame, uint64_t draw_calls);

void ProbeLog(const char* fmt, ...);

FILE* g_log = nullptr;
CRITICAL_SECTION g_lock;

// Shared with camera_poke.cpp, which is why this is not in the anonymous
// namespace below.
// Read bwvr-disable.txt once. Any hook whose name appears in the file is not
// installed. This makes it possible to bisect a bad hook in one run each,
// with no rebuild.
namespace {
char g_disable[512];
bool g_disable_read = false;
}  // namespace

bool ProbeDisabled(const char* name) {
  if (!g_disable_read) {
    g_disable_read = true;
    ZeroMemory(g_disable, sizeof(g_disable));
    char path[MAX_PATH];
    ZeroMemory(path, sizeof(path));
    GetModuleFileNameA(GetModuleHandleA(nullptr), path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash != nullptr) *(slash + 1) = '\0';
    lstrcatA(path, "bwvr-disable.txt");
    FILE* file = nullptr;
    if (fopen_s(&file, path, "r") == 0 && file != nullptr) {
      fread(g_disable, 1, sizeof(g_disable) - 1, file);
      fclose(file);
      CharLowerA(g_disable);
    }
  }
  if (g_disable[0] == '\0') return false;
  char lower[64];
  lstrcpynA(lower, name, sizeof(lower));
  CharLowerA(lower);
  return strstr(g_disable, lower) != nullptr;
}

void ProbeLog(const char* fmt, ...) {
  if (g_log == nullptr) return;
  EnterCriticalSection(&g_lock);
  va_list args;
  va_start(args, fmt);
  vfprintf(g_log, fmt, args);
  va_end(args);
  fputc('\n', g_log);
  fflush(g_log);
  LeaveCriticalSection(&g_lock);
}

namespace {

// Local alias so the rest of this file reads unchanged.
const auto Log = ProbeLog;

// ---------------------------------------------------------------- counters

struct PathCounters {
  uint64_t calls;
  uint64_t vertices;
};

PathCounters g_draw_triangle = {0, 0};
PathCounters g_draw_and_clip = {0, 0};
PathCounters g_draw_and_clip_2d = {0, 0};

// Distinct FVF values seen, with a call count each. Fixed table, so there is
// no allocation on the render thread.
struct FvfSeen {
  uint32_t fvf;
  uint64_t count;
  int path;  // 0 = DrawTriangle, 1 = DrawAndClip, 2 = DrawAndClip2D
};

const int kMaxFvf = 64;
FvfSeen g_fvf[kMaxFvf];
int g_fvf_count = 0;

void NoteFvf(uint32_t fvf, int path) {
  for (int i = 0; i < g_fvf_count; ++i) {
    if (g_fvf[i].fvf == fvf && g_fvf[i].path == path) {
      ++g_fvf[i].count;
      return;
    }
  }
  if (g_fvf_count < kMaxFvf) {
    g_fvf[g_fvf_count].fvf = fvf;
    g_fvf[g_fvf_count].count = 1;
    g_fvf[g_fvf_count].path = path;
    ++g_fvf_count;
  }
}

void DescribeFvf(uint32_t fvf, char* out, size_t n) {
  const uint32_t pos = fvf & D3DFVF_POSITION_MASK;
  const char* kind = "unknown position";
  if (pos == D3DFVF_XYZ) {
    kind = "XYZ (untransformed)";
  } else if (pos == D3DFVF_XYZRHW) {
    kind = "XYZRHW (PRE-TRANSFORMED)";
  } else if (pos == D3DFVF_XYZB1) {
    kind = "XYZB1";
  } else if (pos == D3DFVF_XYZB2) {
    kind = "XYZB2";
  } else if (pos == D3DFVF_XYZB3) {
    kind = "XYZB3";
  }

  const int tex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  _snprintf_s(out, n, _TRUNCATE, "%s%s%s%s tex=%d", kind,
              (fvf & D3DFVF_NORMAL) ? " +normal" : "",
              (fvf & D3DFVF_DIFFUSE) ? " +diffuse" : "",
              (fvf & D3DFVF_SPECULAR) ? " +specular" : "", tex);
}

// ------------------------------------------------------------------- hooks

// LH3DRender::DrawTriangle is __fastcall, not __cdecl. Proven at all 26 call
// sites and by its epilogue:
//
//   mov ecx, <vertex array>        ; arg 1 in ecx
//   movzx edx, word ptr [count]    ; arg 2 in edx
//   push <index count>             ; arg 4 on the stack
//   push <index array>             ; arg 3 on the stack
//   call 0x0082F810                ; ends in "ret 8", so the callee cleans
//
// Declaring it __cdecl unbalances the stack on the first call and the game
// dies a few seconds later. DrawAndClip and DrawAndClip2D really are __cdecl:
// they read every argument from the stack and end in a plain "ret".
typedef void(__fastcall* DrawTriangleFn)(void*, uint32_t, uint16_t*, uint32_t);
typedef void(__cdecl* DrawAndClipFn)(D3DPRIMITIVETYPE, uint32_t, void*, uint32_t,
                                     uint16_t*, uint32_t);
typedef void(__cdecl* FinishFrameFn)();
typedef int(__cdecl* OpenD3DFn)();

DrawTriangleFn o_DrawTriangle = nullptr;
DrawAndClipFn o_DrawAndClip = nullptr;
DrawAndClipFn o_DrawAndClip2D = nullptr;
FinishFrameFn o_FinishFrame = nullptr;
OpenD3DFn o_OpenD3D = nullptr;

// LH3DRender::DrawTriangle hardcodes FVF 0x1C4 (XYZRHW | DIFFUSE | SPECULAR |
// TEX1) in its DrawIndexedPrimitive call, so these vertices are already in
// screen space. Dump one to show that directly rather than by disassembly.
void DumpFirstVertex(const void* verts) {
  if (verts == nullptr || IsBadReadPtr(verts, 32)) return;
  const float* v = static_cast<const float*>(verts);
  const uint32_t* w = static_cast<const uint32_t*>(verts);
  Log("");
  Log("First vertex submitted by LH3DRender::DrawTriangle (FVF 0x1C4):");
  Log("  x %10.3f   y %10.3f   z %10.5f   rhw %12.8f", v[0], v[1], v[2], v[3]);
  Log("  diffuse 0x%08X  specular 0x%08X  u %8.4f  v %8.4f", w[4], w[5], v[6],
      v[7]);
  Log("  If x and y are inside the window in pixels, the engine already");
  Log("  projected this point on the CPU.");
}

void __fastcall h_DrawTriangle(void* verts, uint32_t count, uint16_t* idx,
                               uint32_t n) {
  CameraPokeOnFirstDraw();
  if (g_draw_triangle.calls == 0) DumpFirstVertex(verts);
  ++g_draw_triangle.calls;
  g_draw_triangle.vertices += count;
  NoteFvf(0x1C4, 0);
  DrawPhaseNote(count);
  o_DrawTriangle(verts, count, idx, n);
}

void __cdecl h_DrawAndClip(D3DPRIMITIVETYPE type, uint32_t fvf, void* verts,
                           uint32_t count, uint16_t* idx, uint32_t n) {
  ++g_draw_and_clip.calls;
  g_draw_and_clip.vertices += count;
  NoteFvf(fvf, 1);
  o_DrawAndClip(type, fvf, verts, count, idx, n);
}

void __cdecl h_DrawAndClip2D(D3DPRIMITIVETYPE type, uint32_t fvf, void* verts,
                             uint32_t count, uint16_t* idx, uint32_t n) {
  ++g_draw_and_clip_2d.calls;
  g_draw_and_clip_2d.vertices += count;
  NoteFvf(fvf, 2);
  o_DrawAndClip2D(type, fvf, verts, count, idx, n);
}

void DumpMatrix(const char* name, uintptr_t address) {
  const bw::LHMatrix* m = reinterpret_cast<const bw::LHMatrix*>(address);
  if (IsBadReadPtr(m, sizeof(bw::LHMatrix))) {
    Log("  %-12s <unreadable at 0x%08X>", name, address);
    return;
  }
  Log("  %-12s [%9.3f %9.3f %9.3f]", name, m->m[0], m->m[1], m->m[2]);
  Log("  %-12s [%9.3f %9.3f %9.3f]", "", m->m[3], m->m[4], m->m[5]);
  Log("  %-12s [%9.3f %9.3f %9.3f]", "", m->m[6], m->m[7], m->m[8]);
  Log("  %-12s [%9.3f %9.3f %9.3f]  translation", "", m->m[9], m->m[10], m->m[11]);
}

uint64_t g_frame = 0;

void ReportCounters(const char* heading) {
  Log("");
  Log("%s", heading);
  Log("  DrawTriangle   calls=%-8llu verts=%llu", g_draw_triangle.calls,
      g_draw_triangle.vertices);
  Log("  DrawAndClip    calls=%-8llu verts=%llu", g_draw_and_clip.calls,
      g_draw_and_clip.vertices);
  Log("  DrawAndClip2D  calls=%-8llu verts=%llu", g_draw_and_clip_2d.calls,
      g_draw_and_clip_2d.vertices);
  if (g_fvf_count > 0) {
    Log("  vertex formats seen:");
    for (int i = 0; i < g_fvf_count; ++i) {
      char desc[160];
      DescribeFvf(g_fvf[i].fvf, desc, sizeof(desc));
      Log("    %-13s FVF 0x%04X  x%-8llu  %s",
          g_fvf[i].path == 0 ? "DrawTriangle"
              : g_fvf[i].path == 1 ? "DrawAndClip" : "DrawAndClip2D", g_fvf[i].fvf,
          g_fvf[i].count, desc);
    }
  }
}

void __cdecl h_FinishFrame() {
  DrawPhaseEnterFinish();
  ++g_frame;

  // Report often at first, then rarely, so the log stays readable.
  const bool report =
      g_frame <= 3 || g_frame == 30 || g_frame == 120 || (g_frame % 600) == 0;
  if (report) {
    char heading[64];
    _snprintf_s(heading, sizeof(heading), _TRUNCATE, "=== frame %llu ===", g_frame);
    ReportCounters(heading);
    DumpMatrix("world2view", bw::render::kWorldToView);
    DumpMatrix("view2proj", bw::render::kViewToProj);
  }

  DeviceHookTick(g_frame);
  if (g_frame == 300) TechCameraReportInfoTransform();
  if (!ProbeDisabled("techcamera")) TechCameraTick(g_frame);
  FrameGrabTick(g_frame, g_draw_triangle.calls);
  DrawPhaseTick(g_frame);
  o_FinishFrame();
  DrawPhaseLeaveFinish();
}

void ReportDeviceCaps() {
  IDirect3DDevice7** slot =
      reinterpret_cast<IDirect3DDevice7**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(IDirect3DDevice7*)) || *slot == nullptr) {
    Log("  Direct3DDevice7 global is null, so no device exists yet.");
    return;
  }

  D3DDEVICEDESC7 desc;
  ZeroMemory(&desc, sizeof(desc));
  HRESULT hr = (*slot)->GetCaps(&desc);
  if (FAILED(hr)) {
    Log("  GetCaps failed, hr=0x%08X", hr);
    return;
  }

  const bool hw_tnl = (desc.dwDevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) != 0;
  const bool hw_raster = (desc.dwDevCaps & D3DDEVCAPS_HWRASTERIZATION) != 0;
  Log("  hardware transform and lighting : %s", hw_tnl ? "YES" : "no");
  Log("  hardware rasterization          : %s", hw_raster ? "YES" : "no");
  Log("  max vertex blend matrices       : %u", desc.wMaxVertexBlendMatrices);
  Log("  max active lights               : %lu", desc.dwMaxActiveLights);
  Log("  device GUID : %08lX-%04X-%04X", desc.deviceGUID.Data1,
      desc.deviceGUID.Data2, desc.deviceGUID.Data3);
}

int __cdecl h_OpenD3D() {
  Log("");
  Log("--- LH3DRender::OpenD3D() entered ---");
  const int result = o_OpenD3D();
  Log("--- OpenD3D returned %d ---", result);
  ReportDeviceCaps();
  // The device exists now, so its vtable can be hooked.
  if (!ProbeDisabled("devicehook")) {
    DeviceHookConfigure(CameraPokeRoll(), CameraPokeOffsetX());
    DeviceHookInstall(&ProbeLog);
  }
  return result;
}

// -------------------------------------------------------------- bootstrap

bool VerifyTarget() {
  HMODULE exe = GetModuleHandleA(nullptr);
  const uintptr_t base = reinterpret_cast<uintptr_t>(exe);
  if (base != bw::kImageBase) {
    Log("REFUSING TO HOOK: image base is 0x%08X, expected 0x%08X.", base,
        bw::kImageBase);
    return false;
  }

  char path[MAX_PATH];
  ZeroMemory(path, sizeof(path));
  GetModuleFileNameA(exe, path, MAX_PATH);
  Log("host executable : %s", path);
  Log("image base      : 0x%08X", base);

  const uint8_t* open_d3d = reinterpret_cast<const uint8_t*>(bw::render::kOpenD3D);
  if (IsBadReadPtr(open_d3d, 16)) {
    Log("REFUSING TO HOOK: OpenD3D address 0x%08X is not readable.",
        bw::render::kOpenD3D);
    return false;
  }
  Log("OpenD3D bytes   : %02X %02X %02X %02X %02X %02X", open_d3d[0], open_d3d[1],
      open_d3d[2], open_d3d[3], open_d3d[4], open_d3d[5]);
  return true;
}

struct HookSpec {
  const char* name;
  uintptr_t address;
  void* detour;
  void** original;
};

void InstallHooks() {
  if (MH_Initialize() != MH_OK) {
    Log("MH_Initialize failed. No hooks installed.");
    return;
  }

  const HookSpec specs[] = {
      {"LH3DRender::OpenD3D", bw::render::kOpenD3D,
       reinterpret_cast<void*>(&h_OpenD3D),
       reinterpret_cast<void**>(&o_OpenD3D)},
      {"LH3DRender::FinishFrame", bw::render::kFinishFrame,
       reinterpret_cast<void*>(&h_FinishFrame),
       reinterpret_cast<void**>(&o_FinishFrame)},
      {"LH3DRender::DrawTriangle", bw::render::kDrawTriangle,
       reinterpret_cast<void*>(&h_DrawTriangle),
       reinterpret_cast<void**>(&o_DrawTriangle)},
      {"DrawAndClip", bw::render::kDrawAndClip,
       reinterpret_cast<void*>(&h_DrawAndClip),
       reinterpret_cast<void**>(&o_DrawAndClip)},
      {"DrawAndClip2D", bw::render::kDrawAndClip2D,
       reinterpret_cast<void*>(&h_DrawAndClip2D),
       reinterpret_cast<void**>(&o_DrawAndClip2D)},
  };

  const int count = sizeof(specs) / sizeof(specs[0]);
  for (int i = 0; i < count; ++i) {
    const HookSpec& spec = specs[i];
    void* target = reinterpret_cast<void*>(spec.address);
    MH_STATUS status = MH_CreateHook(target, spec.detour, spec.original);
    if (status != MH_OK) {
      Log("  hook FAILED   %-26s at 0x%08X (%s)", spec.name, spec.address,
          MH_StatusToString(status));
      continue;
    }
    status = MH_EnableHook(target);
    Log("  hook %-8s %-26s at 0x%08X",
        status == MH_OK ? "ok" : "ENABLE-FAIL", spec.name, spec.address);
  }
}

}  // namespace

void ProbeStartup() {
  InitializeCriticalSection(&g_lock);

  char log_path[MAX_PATH];
  ZeroMemory(log_path, sizeof(log_path));
  GetModuleFileNameA(GetModuleHandleA(nullptr), log_path, MAX_PATH);
  char* slash = strrchr(log_path, '\\');
  if (slash != nullptr) *(slash + 1) = '\0';
  lstrcatA(log_path, "bwvr-probe.log");

  // Shared read, so the log can be followed while the game runs.
  g_log = _fsopen(log_path, "w", _SH_DENYWR);

  SYSTEMTIME now;
  GetLocalTime(&now);
  Log("Black & White VR, Phase 0 probe");
  Log("%04d-%02d-%02d %02d:%02d:%02d", now.wYear, now.wMonth, now.wDay, now.wHour,
      now.wMinute, now.wSecond);
  Log("");

  if (!VerifyTarget()) {
    Log("Probe disabled. The game runs normally.");
    return;
  }
  Log("");
  CameraPokeLoad(&ProbeLog);
  FrameGrabLoad();
  Log("");
  InstallHooks();
  // Installed even with no offset file, because it also reports the view
  // matrix at the top of the frame, which is the value that gets rendered.
  if (!ProbeDisabled("camerapoke")) CameraPokeInstall(&ProbeLog);
  // The scene is transformed on the CPU, so this is the hook that matters.
  if (!ProbeDisabled("drawphase")) DrawPhaseInstall();
  if (!ProbeDisabled("techcamera")) {
    TechCameraConfigure(CameraPokeRoll(), CameraPokeOffsetX());
    TechCameraInstall();
  } else {
    Log("  hook SKIPPED  LH3DTech::UpdateCamera     (bwvr-disable.txt)");
  }
}

void ProbeShutdown() {
  if (o_FinishFrame != nullptr) {
    char heading[80];
    _snprintf_s(heading, sizeof(heading), _TRUNCATE,
                "=== final totals after %llu frames ===", g_frame);
    ReportCounters(heading);
  }
  CameraPokeReport(&ProbeLog);
  TechCameraReport();
  DrawPhaseReport();
  TechCameraReportInfoTransform();
  DeviceHookReport(&ProbeLog);
  MH_Uninitialize();
  if (g_log != nullptr) {
    fclose(g_log);
    g_log = nullptr;
  }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      DisableThreadLibraryCalls(module);
      if (!ProxyLoadRealDDraw()) return FALSE;
      ProbeStartup();
      break;
    case DLL_PROCESS_DETACH:
      ProbeShutdown();
      break;
    default:
      break;
  }
  return TRUE;
}
