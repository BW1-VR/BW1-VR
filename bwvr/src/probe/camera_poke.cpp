// Camera control experiment.
//
// The whole VR plan rests on one assumption: that writing
// LH3DRender::g_d3d_world2view moves the view. If the engine recomputes the
// matrix after we write it, or reads a cached copy during drawing, per-eye
// rendering cannot be driven this way and the architecture has to change.
//
// This applies a fixed offset to the view matrix every frame and lets us look
// at the result. It is the cheap rehearsal for a per-eye offset.
//
// Enabled by dropping a file named bwvr-camera-offset.txt beside
// runblack.exe, containing three floats: x y z. No file, no effect.

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

namespace {

typedef void(__cdecl* StartFrameFn)();
StartFrameFn o_StartFrame = nullptr;

bool g_enabled = false;
float g_offset[3] = {0.0f, 0.0f, 0.0f};
float g_roll_degrees = 0.0f;
uint64_t g_applied = 0;
uint64_t g_frames = 0;
bool g_armed_this_frame = false;
void (*g_log)(const char*, ...) = nullptr;

// Reading the matrix here, at the top of the frame, shows what the engine
// actually renders with. FinishFrame may be after the value has moved on.

// LHMatrix is 3 rows of 4, row-major, with the translation in the fourth
// column: m[3], m[7], m[11]. Measured, not assumed: only this grouping makes
// the three basis rows unit length and mutually orthogonal. Note that the
// decomp's LHMatrix::SetIdentityMatrix implies a 4x3 layout instead, which
// does not match what world2view actually contains.
//
// Writing m[3] shifts the eye along the view space X axis, which is exactly
// what a per-eye interpupillary offset is.
void ApplyOffset() {
  bw::LHMatrix* view = reinterpret_cast<bw::LHMatrix*>(bw::render::kWorldToView);
  if (IsBadWritePtr(view, sizeof(bw::LHMatrix))) return;
  view->m[3] += g_offset[0];
  view->m[7] += g_offset[1];
  view->m[11] += g_offset[2];

  // Roll about the view forward axis. Rows 0 and 1 are the camera right
  // and up vectors, so rolling rotates them into each other. A roll of 180
  // degrees turns the picture upside down, which is an unmistakable signal
  // that the engine really renders from this matrix.
  if (g_roll_degrees != 0.0f) {
    // Rows 0 and 1 of the 3x4 matrix are the camera right and up vectors.
    const float rad = g_roll_degrees * 3.14159265358979f / 180.0f;
    const float c = cosf(rad);
    const float s = sinf(rad);
    for (int i = 0; i < 3; ++i) {
      const float right = view->m[0 + i];
      const float up = view->m[4 + i];
      view->m[0 + i] = right * c + up * s;
      view->m[4 + i] = -right * s + up * c;
    }
  }

  ++g_applied;
}

void __cdecl h_StartFrame() {
  o_StartFrame();
  ++g_frames;

  const bool report = g_frames <= 3 || (g_frames % 600) == 0;
  if (report && g_log != nullptr) {
    const bw::LHMatrix* v =
        reinterpret_cast<const bw::LHMatrix*>(bw::render::kWorldToView);
    if (!IsBadReadPtr(v, sizeof(bw::LHMatrix))) {
      g_log("StartFrame %llu world2view (3 rows of 4, translation last):",
            g_frames);
      g_log("   [%10.4f %10.4f %10.4f | %10.4f]", v->m[0], v->m[1], v->m[2], v->m[3]);
      g_log("   [%10.4f %10.4f %10.4f | %10.4f]", v->m[4], v->m[5], v->m[6], v->m[7]);
      g_log("   [%10.4f %10.4f %10.4f | %10.4f]", v->m[8], v->m[9], v->m[10], v->m[11]);
    }
  }

  // Writing the matrix here does nothing, because the camera update runs
  // later in the game loop and overwrites it. Instead we arm a latch and
  // apply the change at the first draw of the frame, by which time the
  // engine has written its final value. See CameraPokeOnFirstDraw.
  g_armed_this_frame = true;
}

}  // namespace

// Reads the offset file. Returns true if the experiment is armed.
bool CameraPokeLoad(void (*log)(const char*, ...)) {
  char path[MAX_PATH];
  ZeroMemory(path, sizeof(path));
  GetModuleFileNameA(GetModuleHandleA(nullptr), path, MAX_PATH);
  char* slash = strrchr(path, '\\');
  if (slash != nullptr) *(slash + 1) = '\0';
  lstrcatA(path, "bwvr-camera-offset.txt");

  FILE* file = nullptr;
  if (fopen_s(&file, path, "r") != 0 || file == nullptr) {
    log("camera poke  : disabled (no bwvr-camera-offset.txt)");
    return false;
  }

  const int read = fscanf_s(file, "%f %f %f %f", &g_offset[0], &g_offset[1],
                            &g_offset[2], &g_roll_degrees);
  fclose(file);

  if (read < 3) {
    log("camera poke  : file found but needs at least three floats");
    return false;
  }
  if (read < 4) g_roll_degrees = 0.0f;

  g_enabled = true;
  log("camera poke  : ARMED, offset (%.3f, %.3f, %.3f), roll %.1f deg",
      g_offset[0], g_offset[1], g_offset[2], g_roll_degrees);
  return true;
}

void CameraPokeInstall(void (*log)(const char*, ...)) {
  g_log = log;
  void* target = reinterpret_cast<void*>(bw::render::kStartFrame);
  MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void*>(&h_StartFrame),
                                   reinterpret_cast<void**>(&o_StartFrame));
  if (status != MH_OK) {
    log("  hook FAILED   %-26s at 0x%08X (%s)", "LH3DRender::StartFrame",
        bw::render::kStartFrame, MH_StatusToString(status));
    return;
  }
  status = MH_EnableHook(target);
  log("  hook %-8s %-26s at 0x%08X", status == MH_OK ? "ok" : "ENABLE-FAIL",
      "LH3DRender::StartFrame", bw::render::kStartFrame);
}

// Called from the DrawTriangle hook. The engine has finished writing
// g_d3d_world2view by the time any geometry is submitted, so this is the
// first moment our change survives to be used.
void CameraPokeOnFirstDraw() {
  if (!g_enabled || !g_armed_this_frame) return;
  g_armed_this_frame = false;
  ApplyOffset();
}

float CameraPokeRoll() { return g_roll_degrees; }
float CameraPokeOffsetX() { return g_offset[0]; }

void CameraPokeReport(void (*log)(const char*, ...)) {
  if (!g_enabled) return;
  log("camera poke  : applied to %llu frames", g_applied);
}
