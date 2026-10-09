// PARKED. Nothing calls this file.
//
// Subtitles that hang in the world read worse than subtitles on the view, so
// no setting, hotkey or draw hook uses this math. It stays because a later
// interface in the world will need the same camera difference. See
// backend.md, "Subtitles in the world (parked)".
//
// Stop the interface riding the player's head.
//
// The world stays put when the head turns because the camera turns with it and
// the image compensates. The interface is drawn in screen space, so nothing
// compensates for it and it rides along, which is what "the menus are stuck to
// my head" means.
//
// The usual answer is to render the interface into its own layer and hang it in
// the world. That needs a second render target and control of the render target
// switch, which is behind the backend and not reachable from here.
//
// But it is not needed. The interface is drawn at a screen position, and a
// screen position is a direction: the projection is known, and both cameras are
// known. So the vertices can simply be moved.
//
//     screen  ->  direction, through the camera the GAME chose
//     direction  ->  screen, through the camera the HEAD chose
//
// THE ROTATION BETWEEN THEM, which the first version got wrong twice over.
//
// It used the head's ABSOLUTE pose, and it applied that pose in the camera's
// own frame. Neither is the rotation wanted. The head is applied to the
// engine's camera in a LEVELLED frame, and the engine's camera already carries
// it by the time anything is drawn, so rotating a screen ray by the absolute
// head pose applies the head a second time, about the wrong axis.
//
// The rotation wanted is the difference between the camera that drew the
// frame and the ROOM the player is standing in:
//
//     M = transpose(render_basis) * stage_basis
//
// Both bases have their right, up and forward down their columns, in world
// space. The room is world up and the game camera's heading, and the head
// turns inside it, so M is simply the inverse of the head's rotation. With the
// head straight ahead it is the identity and nothing moves, which is the check
// that matters.
//
// It is the room and not the game's own camera, because the game's camera
// looks down at the island and the view does not. Text hung in the game's
// camera would take the game's whole downward pitch, which puts a subtitle at
// the bottom of the screen 68 degrees below the horizon and off the view.
//
// The interface then sits at a fixed direction in the world and the player can
// look around it, which is what a menu hanging in a room does.
//
// THE PROJECTION, measured and recorded in bw_addresses.h:
//
//     screen_x = (1 + cx/cz) * half_res_x
//
// where cx is camera x already multiplied by inv_half_tan_fov_x. Screen y grows
// downward while camera y grows upward, so the y form carries a minus.
//
// WHAT IS LEFT ALONE. The hand hint is not screen space: the copy the mod keeps
// is projected through the camera and already lands in the right place, so
// moving it would correct it twice. The band the hint occupies is already
// known, so it is skipped.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);

// head_camera.cpp: the two cameras this works between. Both are latched once
// a frame, from the arguments the engine was given, so both eyes see the same
// pair and neither can drift inside the scene walk.
bool HeadCameraEnabled();
bool HeadCameraRenderBasis(float basis_out[9], float position_out[3]);
bool HeadCameraStageBasis(float basis_out[9]);

namespace {

bool g_enabled = false;
uint64_t g_moved = 0;
uint64_t g_refused = 0;

}  // namespace

void UiWorldLockSetEnabled(bool on) {
  g_enabled = on;
  ModLog("interface    : %s",
         on ? "hangs in the world, so the head can look around it"
            : "rides with the head, as the game drew it");
}

bool UiWorldLockEnabled() { return g_enabled; }


// Move one screen position from the camera the game chose to the camera the
// head chose. Returns false when the point ends up behind the viewer, and then
// the caller must leave the whole batch alone rather than draw half of it.
bool UiWorldLockPoint(float* x, float* y) {
  if (!g_enabled || !HeadCameraEnabled()) return false;

  float render[9];
  float stage[9];
  float where[3];
  if (!HeadCameraRenderBasis(render, where)) return false;
  if (!HeadCameraStageBasis(stage)) return false;

  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return false;
  const float hx = info->half_res_x;
  const float hy = info->half_res_y;
  const float fx = info->inv_half_tan_fov_x;
  const float fy = info->inv_half_tan_fov_y;
  if (hx <= 0.0f || hy <= 0.0f || fx <= 0.0f || fy <= 0.0f) return false;

  // Screen to a direction, in the frame of the camera the game chose.
  const float ray[3] = {(*x / hx - 1.0f) / fx, (1.0f - *y / hy) / fy, 1.0f};

  // Into world space through the room, then back out through the camera that
  // actually drew this frame. Written as one matrix product so the identity
  // case is obvious.
  float world[3];
  for (int i = 0; i < 3; ++i) {
    world[i] = stage[i * 3 + 0] * ray[0] + stage[i * 3 + 1] * ray[1] +
               stage[i * 3 + 2] * ray[2];
  }
  float turned[3];
  for (int c = 0; c < 3; ++c) {
    turned[c] = render[0 * 3 + c] * world[0] + render[1 * 3 + c] * world[1] +
                render[2 * 3 + c] * world[2];
  }

  // Anything at or behind the eye has no screen position at all.
  if (turned[2] < 0.05f) {
    ++g_refused;
    return false;
  }

  *x = (1.0f + (turned[0] * fx) / turned[2]) * hx;
  *y = (1.0f - (turned[1] * fy) / turned[2]) * hy;
  ++g_moved;
  return true;
}

void UiWorldLockReport() {
  if (!g_enabled || g_moved == 0) return;
  ModLog("interface    : %llu vertices moved into the world, %llu left alone "
         "for being behind the viewer", g_moved, g_refused);
  g_moved = 0;
  g_refused = 0;
}
