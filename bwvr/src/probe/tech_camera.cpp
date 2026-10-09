// The real camera hook: LH3DTech::UpdateCamera.
//
// Black & White transforms the scene on the CPU. LH3DTech::ProjectPoint
// multiplies every point by the matrix at 0x00EA9E40 and divides by the
// result, and LH3DRender::DrawTriangle then hands Direct3D the finished
// screen space vertices as FVF 0x1C4 (XYZRHW). The Direct3D VIEW transform
// the engine also sets is a leftover that nothing in the scene reads. So
// steering the camera through Direct3D or through
// LH3DRender::g_d3d_world2view has no effect.
//
// LH3DTech::UpdateCamera(position, focus) builds that matrix. Hook it, let
// it run, then change the matrix it just wrote. This module proves the idea
// with a 180 degree roll, which turns the picture upside down if, and only
// if, the mod controls the camera.
//
// Matrix layout, read out of ProjectPoint rather than guessed. 4 rows of 3,
// translation in the last row, camera basis in the COLUMNS:
//
//   cam.x = p.x*m[0] + p.y*m[3] + p.z*m[6] + m[9]
//   cam.y = p.x*m[1] + p.y*m[4] + p.z*m[7] + m[10]
//   cam.z = p.x*m[2] + p.y*m[5] + p.z*m[8] + m[11]
//
// So column 0 is the camera right axis, column 1 is up, column 2 is forward,
// and m[9..11] is the camera space origin. Adding to m[9] slides the eye
// along its own right axis, which is exactly an interpupillary offset.

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

extern void ProbeLog(const char* format, ...);

namespace {

// The Intel compiler passes the first two pointer arguments in ecx and edx.
// Verified in the prologue at 0x00819920: mov esi, edx / mov edi, ecx.
typedef void(__fastcall* UpdateCameraFn)(const bw::LHPoint* position,
                                         const bw::LHPoint* focus);
UpdateCameraFn o_UpdateCamera = nullptr;
UpdateCameraFn o_UpdateCamera2 = nullptr;
uint64_t g_calls2 = 0;

bool g_enabled = false;
float g_roll_degrees = 0.0f;
float g_eye_offset = 0.0f;
uint64_t g_calls = 0;

float* Matrix() { return reinterpret_cast<float*>(bw::tech::kWorldToCamera); }

// Rotate the image about the view forward axis. Column 0 and column 1 are the
// right and up axes, and the translation pair m[9], m[10] must turn with them
// so that the camera stays where it is.
void ApplyRoll(float* m, float degrees) {
  const float rad = degrees * 3.14159265358979f / 180.0f;
  const float c = cosf(rad);
  const float s = sinf(rad);
  for (int row = 0; row < 4; ++row) {
    const float x = m[row * 3 + 0];
    const float y = m[row * 3 + 1];
    m[row * 3 + 0] = x * c + y * s;
    m[row * 3 + 1] = -x * s + y * c;
  }
}

void LogMatrix(const char* label, const float* m) {
  ProbeLog("  %s (4 rows of 3, translation last):", label);
  for (int row = 0; row < 4; ++row) {
    ProbeLog("    [%10.4f %10.4f %10.4f ]%s", m[row * 3 + 0], m[row * 3 + 1],
             m[row * 3 + 2], row == 3 ? "  <- translation" : "");
  }
}

void __fastcall h_UpdateCamera(const bw::LHPoint* position,
                               const bw::LHPoint* focus) {
  o_UpdateCamera(position, focus);
  ++g_calls;

  float* m = Matrix();
  if (IsBadWritePtr(m, sizeof(float) * 12)) return;

  const bool report = g_calls <= 2 || (g_calls % 900) == 0;
  if (report) {
    ProbeLog("");
    ProbeLog("UpdateCamera call %llu", g_calls);
    if (position != nullptr && !IsBadReadPtr(position, sizeof(bw::LHPoint))) {
      ProbeLog("  position (%.2f, %.2f, %.2f)", position->x, position->y,
               position->z);
    }
    if (focus != nullptr && !IsBadReadPtr(focus, sizeof(bw::LHPoint))) {
      ProbeLog("  focus    (%.2f, %.2f, %.2f)", focus->x, focus->y, focus->z);
    }
    LogMatrix("world2camera as written by the engine", m);
  }

  if (!g_enabled) return;

  if (g_roll_degrees != 0.0f) ApplyRoll(m, g_roll_degrees);
  // Column 0 is the camera right axis, so m[9] is the camera space X origin.
  if (g_eye_offset != 0.0f) m[9] += g_eye_offset;

  if (report) LogMatrix("world2camera after our change", m);
}

// The unnamed twin at 0x00819F50 writes the same 12 floats. Hook it too, or a
// change made in UpdateCamera can be overwritten before anything is drawn.
void __fastcall h_UpdateCamera2(const bw::LHPoint* position,
                                const bw::LHPoint* focus) {
  o_UpdateCamera2(position, focus);
  ++g_calls2;

  float* m = Matrix();
  if (IsBadWritePtr(m, sizeof(float) * 12)) return;

  if (g_calls2 <= 2) {
    ProbeLog("");
    ProbeLog("second camera writer (0x00819F50) call %llu", g_calls2);
    LogMatrix("world2camera as written", m);
  }

  if (!g_enabled) return;
  if (g_roll_degrees != 0.0f) ApplyRoll(m, g_roll_degrees);
  if (g_eye_offset != 0.0f) m[9] += g_eye_offset;
}

}  // namespace

void TechCameraConfigure(float roll_degrees, float eye_offset) {
  g_roll_degrees = roll_degrees;
  g_eye_offset = eye_offset;
  g_enabled = (roll_degrees != 0.0f) || (eye_offset != 0.0f);
}

void TechCameraInstall() {
  void* target = reinterpret_cast<void*>(bw::tech::kUpdateCamera);
  MH_STATUS status = MH_CreateHook(target,
                                   reinterpret_cast<void*>(&h_UpdateCamera),
                                   reinterpret_cast<void**>(&o_UpdateCamera));
  if (status != MH_OK) {
    ProbeLog("  hook FAILED   %-26s at 0x%08X (%s)", "LH3DTech::UpdateCamera",
             bw::tech::kUpdateCamera, MH_StatusToString(status));
    return;
  }
  status = MH_EnableHook(target);
  ProbeLog("  hook %-8s %-26s at 0x%08X  (roll %.1f deg, eye %.3f)",
           status == MH_OK ? "ok" : "ENABLE-FAIL", "LH3DTech::UpdateCamera",
           bw::tech::kUpdateCamera, g_roll_degrees, g_eye_offset);

  void* target2 = reinterpret_cast<void*>(bw::tech::kUpdateCamera2);
  status = MH_CreateHook(target2, reinterpret_cast<void*>(&h_UpdateCamera2),
                         reinterpret_cast<void**>(&o_UpdateCamera2));
  if (status == MH_OK) status = MH_EnableHook(target2);
  ProbeLog("  hook %-8s %-26s at 0x%08X", status == MH_OK ? "ok" : "FAILED",
           "second camera writer", bw::tech::kUpdateCamera2);
}

// The screen mapping and the culling cone, read once the game is running.
void TechCameraReportInfoTransform() {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) {
    ProbeLog("InfoTransform: unreadable");
    return;
  }
  ProbeLog("");
  ProbeLog("=== LH3DTech::g_info_transform ===");
  ProbeLog("  resolution        %d x %d", info->res_x, info->res_y);
  ProbeLog("  aspect x/y        %.4f", info->aspect_x_over_y);
  ProbeLog("  half res          %.2f, %.2f", info->half_res_x, info->half_res_y);
  ProbeLog("  inv half tan fov  x %.4f  y %.4f", info->inv_half_tan_fov_x,
           info->inv_half_tan_fov_y);
  ProbeLog("  cos half fov      %.4f  (squared %.4f)", info->cos_half_fov,
           info->cos_half_fov_sqr);

  // The projection is screen_x = (1 + cam.x/cam.z) * half_res_x, so the field
  // of view is carried in the matrix basis, not here. These terms scale it.
  if (info->inv_half_tan_fov_x > 0.0f && info->inv_half_tan_fov_y > 0.0f) {
    const float deg = 2.0f * 180.0f / 3.14159265358979f;
    ProbeLog("  implied fov       %.1f deg horizontal, %.1f deg vertical",
             atanf(1.0f / info->inv_half_tan_fov_x) * deg,
             atanf(1.0f / info->inv_half_tan_fov_y) * deg);
  }
  if (info->cos_half_fov > -1.0f && info->cos_half_fov < 1.0f) {
    ProbeLog("  cull cone half    %.1f deg", acosf(info->cos_half_fov) * 180.0f /
                                                 3.14159265358979f);
  }
}

// Is the matrix rewritten every frame, and by whom? UpdateCamera is called
// only a handful of times per session, yet the camera clearly moves, so
// something else writes 0x00EA9E40. Count the frames where it changed.
namespace {
float g_last[12];
bool g_have_last = false;
uint64_t g_frames_seen = 0;
uint64_t g_frames_changed = 0;
uint64_t g_changed_without_update = 0;
uint64_t g_calls_at_last_change = 0;
}  // namespace

void TechCameraTick(uint64_t frame) {
  const float* m = Matrix();
  if (IsBadReadPtr(m, sizeof(float) * 12)) return;
  ++g_frames_seen;

  if (g_have_last) {
    if (memcmp(g_last, m, sizeof(g_last)) != 0) {
      ++g_frames_changed;
      if (g_calls_at_last_change == g_calls + g_calls2) {
        ++g_changed_without_update;
      }
      g_calls_at_last_change = g_calls + g_calls2;
    }
  }
  memcpy(g_last, m, sizeof(g_last));
  g_have_last = true;

  if (frame == 600 || (frame % 3000) == 0) {
    ProbeLog("");
    ProbeLog("world2camera at frame %llu: changed on %llu of %llu frames, "
             "%llu of those with no UpdateCamera call",
             frame, g_frames_changed, g_frames_seen, g_changed_without_update);
    LogMatrix("current", m);
  }
}

void TechCameraReport() {
  ProbeLog("");
  ProbeLog("world2camera changed on %llu of %llu frames, %llu without an "
           "UpdateCamera call in between",
           g_frames_changed, g_frames_seen, g_changed_without_update);
  ProbeLog("");
  ProbeLog("LH3DTech::UpdateCamera calls: %llu, second writer calls: %llu",
           g_calls, g_calls2);
}
