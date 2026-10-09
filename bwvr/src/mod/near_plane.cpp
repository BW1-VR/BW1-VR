// The near plane of the game camera (docs/god-view.md, "Fog, detail and the near
// plane", and docs/backend.md, "The depth buffer").
//
// LandFeature::GetNearClipping 0x005E2F30 has no arguments, gives the near
// plane in st0 and ends with a plain ret. Its only caller is GCamera::Update
// at 0x004424AF, which stores the result at 0x00E839E0 every frame. The game
// gives 0.3 + 0.16 * h for a camera h units over the land, and 3.5 units at
// 20 units and higher (0.1 when 0x00D1A2F8 is set).
//
// At the life size scale (7.5 units per meter) 3.5 units is 47 cm of the
// room. That cuts away a real hand in front of the eyes. So in the play modes
// where the head moves the camera (0, 3 and 4), the near plane is at most a
// fixed distance of the room:
//
//     near_plane_cm      5 cm, with a Z buffer of 24 bit or more
//     near_plane_cm_16  10 cm, with a 16 bit Z buffer, or while the depth of
//                          the Z buffer is not known
//
// The distance in game units is that distance times the scale of the view.
// While the table or the God view owns the view, the scale is the table
// scale. At other times (play mode 0, and a story in play mode 3 or 4) it is
// the life size scale. When the game's value is nearer, the game's value
// stays. So this never moves the near plane away from the eye.
//
// WHY 10 CM AT 16 BIT. The game writes z = 1 - near / cz, so a depth step is
// cz^2 / (near * 2^bits). A smaller near plane makes every step larger. 0.75
// units (10 cm at the life size scale) is the value that the game itself uses
// with its camera 2.8 units over the land, so the game accepts that
// precision. 5 cm at 16 bit doubles the steps again. The table of
// docs/backend.md gives the numbers.
//
// Everything that reads the near plane reads 0x00E839E0 after this hook:
// the overlay hands (overlay.cpp), the glow (hand_glow.cpp) and the sea disc
// of the table (tabletop.cpp). The interface depth of draw_hooks.cpp and
// stereo.cpp measures the world as 1 / cz, so it does not change with the
// near plane.
//
// Play modes 1 and 2 show the game in a window, so the hands are not in the
// world. They keep the game's near plane, and this file hooks nothing.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "MinHook.h"

extern void ModLog(const char* format, ...);

// gpu_bridge.cpp: the depth of the game's Z buffer in bits, 0 when not known.
int GpuBridgeDepthBits();
// tabletop.cpp: the table scale while the table owns the view, else 0.
float TabletopViewScale();
// head_pose.cpp: the life size scale, in game units per meter.
float HeadPoseScale();
// cutscene_window.cpp: this frame draws on the screen of the cut scene
// window, as play mode 1 does.
bool CutsceneWindowFlat();

namespace {

const uintptr_t kGetNearClipping = 0x005E2F30;

typedef float(__fastcall* NearClippingFn)(void* ecx, void* edx);
NearClippingFn o_NearClipping = nullptr;

bool g_installed = false;
float g_meters = 0.05f;     // near_plane_cm
float g_meters_16 = 0.10f;  // near_plane_cm_16

// For the report.
uint64_t g_calls = 0;
uint64_t g_lowered = 0;
float g_last_game = 0.0f;
float g_last_used = 0.0f;
float g_last_scale = 0.0f;
int g_last_bits = 0;
// A log line comes when the depth of the Z buffer changes, at most a few.
int g_said_bits = -1;
int g_said_count = 0;

// The hook has the register arguments of a fastcall, so ecx and edx reach
// the original as they came.
float __fastcall h_NearClipping(void* ecx, void* edx) {
  const float game = o_NearClipping(ecx, edx);
  // The cut scene window shows the game in a window, so no hand is in the
  // world. As in play modes 1 and 2, the game's near plane stays.
  if (CutsceneWindowFlat()) return game;
  ++g_calls;
  const int bits = GpuBridgeDepthBits();
  const float meters = bits >= 24 ? g_meters : g_meters_16;
  float scale = TabletopViewScale();
  if (!(scale > 0.0f)) scale = HeadPoseScale();
  g_last_game = game;
  g_last_scale = scale;
  g_last_bits = bits;
  g_last_used = game;
  if (!(meters > 0.0f) || !(scale > 0.0f)) return game;
  const float room = meters * scale;
  if (bits != g_said_bits && g_said_count < 4) {
    g_said_bits = bits;
    ++g_said_count;
    if (bits > 0) {
      ModLog("near plane   : the Z buffer is %d bit, so the near plane is at "
             "most %.0f cm of the room", bits, meters * 100.0f);
    } else {
      ModLog("near plane   : the depth of the Z buffer is not known yet, so "
             "the near plane is at most %.0f cm of the room", meters * 100.0f);
    }
  }
  if (room < game) {
    ++g_lowered;
    g_last_used = room;
    return room;
  }
  return game;
}

}  // namespace

// near_plane_cm and near_plane_cm_16 in bwvr.ini. 0 keeps the game's near
// plane for that depth of Z buffer.
void NearPlaneSetOptions(int cm_24, int cm_16) {
  if (cm_24 < 0) cm_24 = 0;
  if (cm_16 < 0) cm_16 = 0;
  if (cm_24 > 50) cm_24 = 50;
  if (cm_16 > 50) cm_16 = 50;
  g_meters = static_cast<float>(cm_24) * 0.01f;
  g_meters_16 = static_cast<float>(cm_16) * 0.01f;
}

// Only in the play modes where the head moves the camera (0, 3 and 4).
void NearPlaneInstall() {
  if (g_installed) return;
  void* target = reinterpret_cast<void*>(kGetNearClipping);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_NearClipping),
                    reinterpret_cast<void**>(&o_NearClipping));
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : LandFeature::GetNearClipping at 0x%08X %s",
         static_cast<unsigned>(kGetNearClipping),
         status == MH_OK ? "ok" : "FAILED");
  if (status != MH_OK) return;
  g_installed = true;
  ModLog("near plane   : at most %.0f cm of the room with a 24 bit Z buffer, "
         "and %.0f cm with a 16 bit one. 0 keeps the game's value",
         g_meters * 100.0f, g_meters_16 * 100.0f);
}

void NearPlaneReport() {
  if (!g_installed || g_calls == 0) return;
  ModLog("near plane   : lowered on %llu of %llu calls. Last: the game gave "
         "%.2f units, %.3f used (%.1f cm at %.1f units per meter), Z buffer "
         "%d bit", g_lowered, g_calls, g_last_game, g_last_used,
         g_last_scale > 0.0f ? g_last_used / g_last_scale * 100.0f : 0.0f,
         g_last_scale, g_last_bits);
  g_calls = 0;
  g_lowered = 0;
}
