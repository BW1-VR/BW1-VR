// SPRITES WITH WORLD UP (docs/backend.md, "Sprites with world up").
// Pure math: no Windows, no game. The checks are in
// test/controller_math/sprite_checks.h.
//
// THE CAMERA MATRIX OF THE ENGINE. The matrix at 0x00EA1D28 has 12 floats.
// A world point w goes to camera space as
//
//     c[j] = w.x * m[j] + w.y * m[3 + j] + w.z * m[6 + j] + m[9 + j]
//
// so (m[3], m[4], m[5]) is world up in camera space. Camera x is right, y is
// up and z is forward.
//
// THE SPRITE. LH3DSprite::Draw (0x00840530) puts the four corners of a
// billboard in the camera plane, turned by the angle a of the sprite
// (+0x14). A corner (x, y) goes to camera space as
//
//     (x * cos a + y * sin a,  y * cos a - x * sin a)
//
// so the up of the sprite (0, 1) goes to (sin a, cos a). With no roll of the
// camera, world up is straight up in the camera plane, and the angle a is the
// turn of the sprite from world up. When the head rolls, camera up is not
// world up, and the sprite rolls with the head.
//
// SpriteTurn gives the angle t that turns the up of a sprite onto world up as
// it shows in the camera plane. a + t then keeps the turn of the game
// against world up. The direction of world up in the camera plane is
// (m[3], m[4]), and its length is the cosine of the angle from the forward
// to the horizon. Near a look straight up or down that direction is not
// stable, so the turn fades to zero in the last kFadeShare of the length.
//
// DerollCamera turns a camera about its forward by -t: the same place and the
// same forward, with world up in its camera plane. The game measures the turn
// of a sprite that follows its speed on the screen with the camera matrix
// (UR_OrientSpriteWithVelocity, UR_Flocking). With this camera that measure
// has no roll, and the draw adds t one time only.

#pragma once

#include <math.h>

namespace sprite_math {

// The share of the length of world up (the cosine of the angle from the
// forward to the horizon) under which the turn fades. 0.17 is about 10
// degrees from a look straight up or down.
const float kFadeShare = 0.17f;

// The angle in radians that turns the up of a sprite onto world up in the
// camera plane. Zero when the matrix has no world up in its camera plane.
inline float SpriteTurn(const float m[12]) {
  const float x = m[3];
  const float y = m[4];
  const float z = m[5];
  const float all = sqrtf(x * x + y * y + z * z);
  if (!(all > 1e-6f)) return 0.0f;
  const float in_plane = sqrtf(x * x + y * y);
  if (!(in_plane > 1e-6f * all)) return 0.0f;
  float turn = atan2f(x, y);
  const float share = in_plane / all;
  if (share < kFadeShare) turn *= share / kFadeShare;
  return turn;
}

// The angle in (-pi, pi].
inline float WrapAngle(float a) {
  const float pi = 3.14159265f;
  while (a > pi) a -= 2.0f * pi;
  while (a <= -pi) a += 2.0f * pi;
  return a;
}

// The camera m turned about its forward by -turn. Each row (the camera x and
// y of a world axis, and of the translation) turns in the camera plane. The
// forward and the place of the camera do not change.
inline void DerollCamera(const float m[12], float turn, float out[12]) {
  const float c = cosf(turn);
  const float s = sinf(turn);
  for (int row = 0; row < 4; ++row) {
    const float x = m[3 * row];
    const float y = m[3 * row + 1];
    out[3 * row] = c * x - s * y;
    out[3 * row + 1] = s * x + c * y;
    out[3 * row + 2] = m[3 * row + 2];
  }
}

// The corner (x, y) of a sprite with the angle a, in the camera plane, as
// LH3DSprite::Draw makes it. For the checks.
inline void SpriteCorner(float a, float x, float y, float out[2]) {
  const float c = cosf(a);
  const float s = sinf(a);
  out[0] = x * c + y * s;
  out[1] = y * c - x * s;
}

// The angle that UR_OrientSpriteWithVelocity gives a sprite that moves along
// v (0x0069A790): the direction of v in the camera plane of m, plus a
// quarter turn. For the checks.
inline float SpeedAngle(const float m[12], const float v[3]) {
  const float x = m[0] * v[0] + m[3] * v[1] + m[6] * v[2];
  const float y = m[1] * v[0] + m[4] * v[1] + m[7] * v[2];
  return atan2f(-y, x) + 1.5707964f;
}

}  // namespace sprite_math
