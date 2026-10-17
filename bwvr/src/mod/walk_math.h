// THE WALK WITH THE STICK (docs/god-view.md, "The walk with the stick"). The
// stick of the hand that does not point moves the player over the land of
// the God view while it is pushed. Pure functions: no Windows and no game.
// The checks are in test/controller_math/walk_checks.h.
//
// A direction on the land is a unit vector of the room with two parts: x to
// the right and z forward. A speed is in meters of the room for each second,
// so the walk feels the same at each scale of the view.

#pragma once

#include <math.h>

#include "table_math.h"
#include "zoom_math.h"

namespace walk_math {

// move_stick: which direction is forward for the stick.
const int kRuleOff = 0;         // the stick does not move the player
const int kRuleHead = 1;        // the direction of the head
const int kRuleController = 2;  // the direction in which the controller points

// The value of the key from a number: 0, 1 or 2.
inline int Rule(int value) {
  if (value < kRuleOff) return kRuleOff;
  return value > kRuleController ? kRuleController : value;
}

// The next value for the test key: from 1 to 2 to 0 and back to 1.
inline int NextRule(int value) {
  return value >= kRuleController ? kRuleOff : value + 1;
}

// THE PUSH. The stick as a vector, x to the right and y forward, with the
// dead zone and the curve of the smooth zoom (zoom_math::StickShare) along
// its length. So a push in each direction has the same dead zone, a small
// push is a slow walk, and a diagonal is not faster than a straight push.
// out keeps the direction of the stick, and its length is the share of the
// top speed, 0 to 1. Returns that share.
inline float Push(float x, float y, float dead, float full, float out[2]) {
  out[0] = out[1] = 0.0f;
  if (!(x == x) || !(y == y)) return 0.0f;
  const float length = sqrtf(x * x + y * y);
  const float share = zoom_math::StickShare(length, dead, full);
  if (!(share > 0.0f)) return 0.0f;
  out[0] = x / length * share;
  out[1] = y / length * share;
  return share;
}

// FORWARD OF THE HEAD, flat on the land, from the matrix of the head in the
// room (the columns are its right, its up and its forward). The direction
// comes from the right axis of the head (table_math::HeadYaw), which stays
// level when the head looks down at the land. So a look straight down still
// has a direction. False for a head that lies on its side.
inline bool HeadForward(const float head[9], float out[2]) {
  float yaw = 0.0f;
  if (!table_math::HeadYaw(head, &yaw)) return false;
  out[0] = sinf(yaw);
  out[1] = cosf(yaw);
  return true;
}

// A controller has a direction on the land while the flat part of its unit
// direction is this long or more: it points more than 11.5 degrees away from
// straight up and from straight down.
const float kAimLeast = 0.2f;

// FORWARD OF A CONTROLLER, flat on the land, from the direction in which it
// points in the room. A controller that points almost straight up or down
// has no direction on the land: the flat part of a unit direction must be
// `least` long or more. False then, and out is not written.
inline bool AimForward(const float direction[3], float least, float out[2]) {
  const float flat =
      sqrtf(direction[0] * direction[0] + direction[2] * direction[2]);
  if (!(flat >= least) || !(flat > 0.0f)) return false;
  out[0] = direction[0] / flat;
  out[1] = direction[2] / flat;
  return true;
}

// THE SPEED IN THE ROOM, in meters for each second. forward is the forward
// direction on the land, push is the stick (Push), and top is the speed of a
// full push. The stick forward moves along forward, and the stick to the
// right moves along the direction 90 degrees to the right of it.
inline void Speed(const float forward[2], const float push[2], float top,
                  float out[2]) {
  const float right[2] = {forward[1], -forward[0]};
  out[0] = top * (push[1] * forward[0] + push[0] * right[0]);
  out[1] = top * (push[1] * forward[1] + push[0] * right[1]);
}

// THE STEP OF ONE FRAME, in meters of the room: the speed times the time of
// the frame (zoom_math::FrameSeconds gives that time, with its cap). A time
// that is not above 0 gives no step.
inline void Step(const float speed[2], float seconds, float out[2]) {
  out[0] = out[1] = 0.0f;
  if (!(seconds > 0.0f)) return;
  out[0] = speed[0] * seconds;
  out[1] = speed[1] * seconds;
}

// THE VIEW AFTER A STEP. The player moves `step` over the land, so the waist
// point G goes to the land point that was `step` from it in the room:
//
//     G' = G + s * Lt * (step.x, 0, step.z)
//
// The height of G, the heading and the scale stay. It is the pan of a grab
// with one hand whose hand moved by minus `step` (TableGrabSolve).
inline void Walk(const table_math::TablePose& pose, const float step[2],
                 table_math::TablePose* out) {
  float basis[9];
  table_math::Basis(pose.heading, basis);
  const float across[3] = {step[0] * pose.scale, 0.0f, step[1] * pose.scale};
  *out = pose;
  table_math::Along(pose.center, basis, across, out->center);
  out->center[1] = pose.center[1];
}

// THE LAND THAT THE VIEW FOLLOWS (move_stick_follow_land=1). While the player
// walks, the height of the view follows the land under the head, so the land
// stays at the same height below the hands. The land height goes through two
// stages of a first order filter. The filter counts the way that the head
// went in the room, not the time: a frame with no step changes nothing, so
// the view cannot move while the stick is let go. The land height of the new
// place is the input for the whole step, and the stages take the exact
// answer of the filter for that input:
//
//     x = way / length, e = exp(-x)
//     stage0' = land + (stage0 - land) * e
//     stage1' = land + (stage1 - land) * e + (stage0 - land) * x * e
//
// So one long step and many short steps over the same land give the same
// result, and a low frame rate gives the same heights as a high one.
//
// The length of a stage is 25 cm of the room. At a full push of 150 cm each
// second that is 0.17 s for each stage. A swell of the land with a
// wavelength of 40 cm of the room comes through at 6 percent of its height,
// one of 1 m at 29 percent, and a hill of 3 m at 78 percent. The second
// stage starts from rest, so the view does not start to rise with a jolt
// when the land under the head changes in a step.
const float kFollowLength = 0.25f;  // meters of the room, for each stage

struct LandFollow {
  float stage[2] = {0.0f, 0.0f};  // land heights in world units
};

// The start of a follow: both stages at the land height now, so the land
// keeps the height below the head that it has now.
inline void FollowStart(float land, LandFollow* follow) {
  follow->stage[0] = follow->stage[1] = land;
}

// One step of the walk. land is the land height under the head at the new
// place, in world units, way is the length of the step in meters of the
// room, and length is the length of a stage. Returns how far the filtered
// land went up, in world units (down is less than 0). The view goes up by
// the same height, so the filtered land keeps its height in the room.
inline float FollowStep(LandFollow* follow, float land, float way,
                        float length) {
  if (!(way > 0.0f) || !(length > 0.0f) || !(land == land)) return 0.0f;
  const float x = way / length;
  const float e = expf(-x);
  const float before = follow->stage[1];
  const float gap0 = follow->stage[0] - land;
  const float gap1 = follow->stage[1] - land;
  follow->stage[0] = land + gap0 * e;
  follow->stage[1] = land + gap1 * e + gap0 * x * e;
  return follow->stage[1] - before;
}

// A direction on the land in degrees, for the log: 0 is straight ahead in the
// room, and a direction to the right is more than 0.
inline float DegreesRight(const float direction[2]) {
  return atan2f(direction[0], direction[1]) * 57.29578f;
}

// A move over the land in the world as a move in the room, in meters: the
// map of Walk backward. For the log.
inline void InRoom(const table_math::TablePose& pose, float world_dx,
                   float world_dz, float out[2]) {
  float basis[9];
  table_math::Basis(pose.heading, basis);
  out[0] = out[1] = 0.0f;
  if (!(pose.scale > 0.0f)) return;
  // The transpose of the basis, then the scale.
  out[0] = (basis[0] * world_dx + basis[6] * world_dz) / pose.scale;
  out[1] = (basis[2] * world_dx + basis[8] * world_dz) / pose.scale;
}

}  // namespace walk_math
