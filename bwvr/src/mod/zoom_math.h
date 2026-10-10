// The zoom of the God view (docs/god-view.md, "The zoom"). For the smooth
// zoom: the share of the top rate that a push of the stick asks for, the time
// of a frame that the zoom uses, and the ratio of the scale for one frame.
// For each zoom: the place of the point that the zoom keeps in the room
// (god_zoom_about). Pure functions: no Windows and no game. The limits of the
// scale are in GodZoomAbout (table_math.h). The checks are in
// test/controller_math/zoom_checks.h.

#pragma once

#include <math.h>

#include "table_math.h"

namespace zoom_math {

// THE POINT OF A ZOOM (god_zoom_about). A zoom keeps one point of the world
// at its place in the room. The key says where that point is across the
// room: under the head, where the head looks, or at the Hand. The height of
// the point is the level of the zoom for each value (ZoomLevel), so the
// ground keeps the height that the player gave it.
const int kAboutHead = 0;  // the land point under the head
const int kAboutLook = 1;  // the land point that the head looks at
const int kAboutHand = 2;  // the Hand's point

// The value of the key from a number: 0, 1 or 2.
inline int About(int value) {
  if (value < kAboutHead) return kAboutHead;
  return value > kAboutHand ? kAboutHand : value;
}

// The next value for the test key: from 1 to 0 to 2 and back to 1.
inline int NextAbout(int value) {
  return value <= kAboutHead ? kAboutHand : value - 1;
}

// Which point a zoom got, for the log.
enum PointKind {
  kPointHead = 0,   // under the head, as the key asks
  kPointLook,       // the first land on the line of the look
  kPointLookFar,    // no land on that line: the point of the line at the limit
  kPointLookNone,   // the head has no direction: under the head
  kPointHand,       // the Hand's point
  kPointHandNone,   // no controller places the Hand: under the head
};

// The direction that a head looks in, from its matrix in the room (the
// columns are its right, its up and its forward). False when the column is
// not a unit vector.
inline bool HeadForward(const float head[9], float out[3]) {
  out[0] = head[2];
  out[1] = head[5];
  out[2] = head[8];
  const float length =
      sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
  if (!(length > 0.99f && length < 1.01f)) return false;
  for (int i = 0; i < 3; ++i) out[i] /= length;
  return true;
}

// THE LEVEL OF A ZOOM, as a height in the room: the height of the land under
// the head, kept between farthest and nearest meters below the head (the
// range of a ground step). So a hill that stands over the head goes down as
// the view zooms out.
inline float ZoomLevel(float land_room_y, float head_y, float nearest_m,
                       float farthest_m) {
  const float top = head_y - nearest_m;
  const float bottom = head_y - farthest_m;
  if (land_room_y > top) return top;
  if (land_room_y < bottom) return bottom;
  return land_room_y;
}

// WHERE THE POINT OF A ZOOM IS ACROSS THE ROOM. out is a room point, in
// meters, and the zoom uses its x and its z. Returns the kind of the point.
//
//   kAboutHead  the head.
//   kAboutLook  the first land point on the line from the head along
//               forward, within far_m meters of the head (RayLand, with the
//               land height of the caller). When the line meets no land
//               there, the point of the line at far_m. So a look at the sky
//               or past the horizon gives a point that is at most far_m
//               across from the head, and a look straight up gives the head.
//               The two cases meet at far_m, so a small turn of the head
//               never makes a jump of the point.
//   kAboutHand  the Hand's point.
//
// forward is a unit vector of the room, or null when the head has no
// direction. hand is the Hand's point in the room, or null when no
// controller places the Hand. Each of these two cases gives the head. The
// line goes in steps of step_units of the world.
template <typename Height>
inline int PointAcross(int about, const table_math::TablePose& pose,
                       const float table_room[3], const float head[3],
                       const float* forward, const float* hand, float far_m,
                       float step_units, Height height, float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = head[i];
  if (about == kAboutHand) {
    if (hand == nullptr) return kPointHandNone;
    for (int i = 0; i < 3; ++i) out[i] = hand[i];
    return kPointHand;
  }
  if (about != kAboutLook) return kPointHead;
  if (forward == nullptr || !(far_m > 0.0f)) return kPointLookNone;
  // The last step of the land test can end past the limit.
  if (table_math::RayLand(pose, table_room, head, forward, step_units,
                          far_m * pose.scale, height, out)) {
    const float d[3] = {out[0] - head[0], out[1] - head[1], out[2] - head[2]};
    if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= far_m * far_m) {
      return kPointLook;
    }
  }
  for (int i = 0; i < 3; ++i) out[i] = head[i] + far_m * forward[i];
  return kPointLookFar;
}

// THE POINT OF A ZOOM IN THE WORLD, from its place across the room (the x and
// the z of across) and the level of the zoom, a height in the room.
inline void PivotAt(const table_math::TablePose& pose,
                    const float table_room[3], const float across[3],
                    float level_room_y, float pivot[3]) {
  const float at[3] = {across[0], level_room_y, across[2]};
  table_math::PoseWorld(pose, table_room, at, pivot);
}

// A HILL UNDER THE HEAD AFTER A ZOOM. A zoom about a point that is not under
// the head moves the head over the land. When the land at the new place of
// the head is nearer than nearest_m below the head, all of the land goes down
// until it is nearest_m below, so the head does not go into a hill. Gives the
// height of the waist point to use. center_y is the height that the zoom
// gave, land_y is the land height under the head in that view, and room_y is
// the height of the waist point in the room.
inline float ClearHead(float center_y, float land_y, float room_y, float scale,
                       float head_y, float nearest_m) {
  if (!(scale > 0.0f)) return center_y;
  const float under = room_y + (land_y - center_y) / scale;
  const float top = head_y - nearest_m;
  return under > top ? center_y + (under - top) * scale : center_y;
}

// How far a room point is from the head across the room, in meters.
inline float Across(const float head[3], const float point[3]) {
  const float dx = point[0] - head[0], dz = point[2] - head[2];
  return sqrtf(dx * dx + dz * dz);
}

// THE PUSH. The value of the stick along the zoom axis, 0 to 1, as a share of
// the top rate: 0 at the dead zone and below it, 1 at full and above it, and a
// straight line between the two. So a small push is a slow zoom.
inline float StickShare(float value, float dead, float full) {
  if (!(value > dead)) return 0.0f;
  if (!(full > dead) || value >= full) return 1.0f;
  return (value - dead) / (full - dead);
}

// THE TIME OF A FRAME that the zoom uses, in seconds: the real time since the
// frame before, and at most cap. So a frame that took long (a load, a stall)
// does not make a jump of the scale. A time that is not above 0 gives 0.
inline float FrameSeconds(double seconds, float cap) {
  if (!(seconds > 0.0)) return 0.0f;
  if (seconds > static_cast<double>(cap)) return cap;
  return static_cast<float>(seconds);
}

// THE RATIO OF ONE FRAME: the new scale over the old one.
//
//     ratio = step ^ (-push * seconds / step_seconds)
//
// push is the share of the top rate, -1 to 1. A push above 0 zooms in: the
// scale (units of the world for each meter of the room) goes down, and the
// world gets larger. step is the ratio of one zoom step (1.5), and
// step_seconds is the time in which a full push changes the scale by one
// step. The change is a ratio and not a difference, so the zoom feels the
// same at each scale. The ratio of a hold is the product of the ratios of its
// frames, so it follows the time held and not the frame rate. A push of 0 or
// a time of 0 gives exactly 1: the scale does not change.
inline float FrameRatio(float push, float step, float step_seconds,
                        float seconds) {
  if (push == 0.0f || !(seconds > 0.0f)) return 1.0f;
  if (!(step > 1.0f) || !(step_seconds > 0.0f)) return 1.0f;
  if (push > 1.0f) push = 1.0f;
  if (push < -1.0f) push = -1.0f;
  return powf(step, -push * seconds / step_seconds);
}

// How many zoom steps a change of the scale is, for the log: 1 for a change
// by the ratio of one step, in or out.
inline float Steps(float from, float to, float step) {
  if (!(from > 0.0f) || !(to > 0.0f) || !(step > 1.0f)) return 0.0f;
  return fabsf(logf(to / from) / logf(step));
}

}  // namespace zoom_math
