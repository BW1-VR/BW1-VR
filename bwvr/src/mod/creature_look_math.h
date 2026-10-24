// The Creature looks at the player: the pure math. See docs/creature.md, "The
// Creature looks at the player". creature_look.cpp uses it, and
// test/controller_math checks it (look_checks.h). It has no game and no
// headset in it.
//
// THE LOOK OF THE GAME [V, runblack.exe 1.42]. A step of a plan of the
// Creature can have a look function. fn_004D03B0 calls it on each update and
// gives the point that it writes to fn_00482C90, which keeps the point at
// LH3DCreature+0x48C0. LH3DCreature::UpdateLook (0x00482D90) then turns the
// head to the point: from the eyes (the base of the body at +0x78, 15 times
// the scale at +0x90 higher) it takes the yaw of the point less the heading
// of the body (+0x84) and the pitch of the point. fn_004EEBD0 moves the head
// yaw (+0x4864, at most pi) and the head pitch (+0x486C, at most pi / 2) to
// these with the speed of the Creature.
//
// THE POINT OF A LOOK FUNCTION. Creature::LookAtCamera (0x004D1A30) writes
// the place of the camera (GInterfaceStatus+0xB0) as three words: x and z in
// map units (6553.6 to a unit of the world, cut to an int by __ftol) and the
// height over the land as a float. fn_004D03B0 turns them back: x / 6553.6,
// the height of the land there plus the height, and z / 6553.6.

#pragma once

#include <math.h>
#include <stdint.h>

namespace look_math {

// creature_look_head.
const int kModeCamera = 0;  // the Creature looks at the game camera
const int kModeHead = 1;    // it looks at the player's head

inline int ClampMode(int mode) { return mode != 0 ? kModeHead : kModeCamera; }

// The map units of a unit of the world: 0x008AC400 in Creature::LookAtCamera,
// and 1 / 6553.6 (0x008AA3A4) back in fn_004D03B0.
const float kMapUnits = 6553.6f;

// __ftol cuts toward zero, as a cast does.
inline int32_t ToMap(float units) {
  return static_cast<int32_t>(units * kMapUnits);
}

inline float FromMap(int32_t map) {
  return static_cast<float>(map) * 0.00015258789f;
}

// The eyes of the Creature: LH3DCreature::UpdateLook (0x00482EA9) adds 15
// times the scale (0x008C2C40) to the height of the base of the body.
const float kEyeUp = 15.0f;

inline void EyePoint(const float base[3], float scale, float out[3]) {
  out[0] = base[0];
  out[1] = base[1] + kEyeUp * scale;
  out[2] = base[2];
}

// The yaw of a direction as the game gives it
// (GUtils::SetUnitDirectionVectorFromScreenPoint 0x007FAA50): atan2 of x and
// of minus z. So yaw 0 is minus z, and pi / 2 is plus x.
inline float YawOf(float x, float z) { return atan2f(x, -z); }

// The direction of the eyes: the heading of the body plus the yaw of the
// head, and the pitch of the head (the game takes the asin of the up part).
inline void EyeDirection(float heading, float head_yaw, float head_pitch,
                         float out[3]) {
  const float yaw = heading + head_yaw;
  const float flat = cosf(head_pitch);
  out[0] = sinf(yaw) * flat;
  out[1] = sinf(head_pitch);
  out[2] = -cosf(yaw) * flat;
}

inline float Length(const float v[3]) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

inline float Distance(const float a[3], const float b[3]) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return Length(d);
}

// The angle in degrees between a direction and the line from a point to a
// target. 0 when the line has no length.
inline float AngleToDeg(const float direction[3], const float from[3],
                        const float to[3]) {
  const float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
  const float a = Length(direction);
  const float b = Length(d);
  if (!(a > 1e-6f) || !(b > 1e-6f)) return 0.0f;
  float c = (direction[0] * d[0] + direction[1] * d[1] + direction[2] * d[2]) /
            (a * b);
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 57.29578f;
}

// The angle between the eyes of the Creature and a target, from the facts of
// the LH3DCreature.
inline float EyesToDeg(const float base[3], float scale, float heading,
                       float head_yaw, float head_pitch, const float to[3]) {
  float eye[3];
  float direction[3];
  EyePoint(base, scale, eye);
  EyeDirection(heading, head_yaw, head_pitch, direction);
  return AngleToDeg(direction, eye, to);
}

// THE POINT OF THE HEAD for a look function: x and z in map units, and the
// height over the land as the bits of a float. land is the height of the
// land under the head (LH3DIsland::GetAltitude).
inline void HeadWords(const float head[3], float land, int32_t out[3]) {
  out[0] = ToMap(head[0]);
  out[1] = ToMap(head[2]);
  const float over = head[1] - land;
  union {
    float f;
    int32_t i;
  } bits;
  bits.f = over;
  out[2] = bits.i;
}

// The point that fn_004D03B0 makes of the three words.
inline void PointOfWords(const int32_t words[3], float land, float out[3]) {
  union {
    int32_t i;
    float f;
  } bits;
  bits.i = words[2];
  out[0] = FromMap(words[0]);
  out[1] = land + bits.f;
  out[2] = FromMap(words[1]);
}

// THE LOG. A look ends when no look function asked for the camera for this
// many seconds: the look function runs once a game turn, about each 100 ms.
// The eyes are on the target when they are this near to it, and a move of
// the head of this many units gives one line more.
const double kEndSeconds = 0.5;
const float kOnDeg = 6.0f;
const float kMovedUnits = 10.0f;

}  // namespace look_math
