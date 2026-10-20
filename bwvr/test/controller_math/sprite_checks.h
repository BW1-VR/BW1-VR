// SPRITES WITH WORLD UP (docs/backend.md, "Sprites with world up").
// main.cpp includes this file after sky_checks.h, just before main.
#pragma once

#include "../../src/mod/sprite_math.h"

namespace sprite_checks {

// The up of a sprite with the angle a under the camera m, in the world.
inline void WorldUpOfSprite(const float m[12], float a, float out[3]) {
  float c[2];
  sprite_math::SpriteCorner(a, 0.0f, 1.0f, c);
  for (int k = 0; k < 3; ++k) out[k] = c[0] * m[3 * k] + c[1] * m[3 * k + 1];
}

inline void SpriteChecks() {
  using namespace sprite_math;
  printf("\nSprites with world up\n");

  const float place[3] = {1200.0f, 340.0f, -560.0f};

  // A level head: no turn, so the sprite has the turn of the game.
  float level[12];
  sky_checks::HeadCamera(place, 35.0f, -40.0f, 0.0f, level);
  Near("a head with no roll gives no turn", SpriteTurn(level), 0.0, 1e-6);

  // A roll of 20 degrees gives a turn of 20 degrees, at each pitch that is
  // more than 10 degrees from straight up or down.
  const float pitches[5] = {-75.0f, -40.0f, 0.0f, 30.0f, 70.0f};
  float most = 0.0f;
  for (int i = 0; i < 5; ++i) {
    float m[12];
    sky_checks::HeadCamera(place, 35.0f, pitches[i], 20.0f, m);
    const float d = fabsf(SpriteTurn(m) / kDeg - 20.0f);
    if (d > most) most = d;
  }
  Near("a roll of 20 degrees gives a turn of 20 degrees at five pitches",
       most, 0.0, 1e-3);
  float left[12];
  sky_checks::HeadCamera(place, 35.0f, -40.0f, -30.0f, left);
  Near("a roll of -30 degrees gives a turn of -30 degrees",
       SpriteTurn(left) / kDeg, -30.0, 1e-3);

  // The up of the sprite in the world does not change with the roll. Each
  // roll gives the up of the sprite with no roll.
  const float rolls[4] = {-35.0f, -10.0f, 20.0f, 45.0f};
  float straight[12];
  sky_checks::HeadCamera(place, -60.0f, -45.0f, 0.0f, straight);
  float want[3];
  WorldUpOfSprite(straight, 0.4f, want);
  float up_most = 0.0f, old_least = 10.0f;
  for (int i = 0; i < 4; ++i) {
    float m[12], got[3], old[3];
    sky_checks::HeadCamera(place, -60.0f, -45.0f, rolls[i], m);
    WorldUpOfSprite(m, 0.4f + SpriteTurn(m), got);
    WorldUpOfSprite(m, 0.4f, old);
    for (int k = 0; k < 3; ++k) {
      if (fabsf(got[k] - want[k]) > up_most) up_most = fabsf(got[k] - want[k]);
    }
    const float off = acosf(fminf(1.0f, sky_math::Dot3(old, want)));
    if (off < old_least) old_least = off;
  }
  Near("with the turn the up of a sprite keeps its place in the world",
       up_most, 0.0, 1e-5);
  Check(old_least > 9.0f * kDeg,
        "with no turn the up of a sprite rolls with the head",
        old_least / kDeg, 10.0);

  // A turn of the head with no roll and no move does not change the turn.
  float yaw_a[12], yaw_b[12];
  sky_checks::HeadCamera(place, 0.0f, -40.0f, 20.0f, yaw_a);
  sky_checks::HeadCamera(place, 70.0f, -40.0f, 20.0f, yaw_b);
  Near("a turn of the head about world up keeps the turn",
       SpriteTurn(yaw_a) - SpriteTurn(yaw_b), 0.0, 1e-5);

  // The two eyes differ in the camera x translation alone: one turn.
  float eye[12];
  for (int i = 0; i < 12; ++i) eye[i] = yaw_a[i];
  eye[9] -= 0.032f;
  Near("both eyes get one turn", SpriteTurn(eye) - SpriteTurn(yaw_a), 0.0,
       1e-7);

  // A matrix with a uniform scale (a table frame) gives the same turn.
  float scaled[12];
  for (int i = 0; i < 12; ++i) scaled[i] = yaw_a[i] * 0.004f;
  Near("a scaled camera gives the same turn",
       SpriteTurn(scaled) - SpriteTurn(yaw_a), 0.0, 1e-5);

  // Near a look straight down the turn fades to zero.
  float down[12], nearly[12];
  sky_checks::HeadCamera(place, 0.0f, -89.9f, 20.0f, down);
  sky_checks::HeadCamera(place, 0.0f, -85.0f, 20.0f, nearly);
  Check(fabsf(SpriteTurn(down)) < 0.5f * kDeg,
        "a look straight down gives almost no turn",
        SpriteTurn(down) / kDeg, 0.0);
  Check(SpriteTurn(nearly) > 5.0f * kDeg && SpriteTurn(nearly) < 15.0f * kDeg,
        "a look 5 degrees from straight down gives half the turn",
        SpriteTurn(nearly) / kDeg, 10.0);

  // The camera with no roll: the same place and forward, and no turn.
  float rolled[12], deroll[12];
  sky_checks::HeadCamera(place, 35.0f, -40.0f, 20.0f, rolled);
  const float t = SpriteTurn(rolled);
  DerollCamera(rolled, t, deroll);
  Near("the camera with no roll gives no turn", SpriteTurn(deroll), 0.0,
       1e-5);
  Near("the camera with no roll is the camera of the head with no roll",
       sky_checks::Largest9(deroll, level), 0.0, 1e-5);
  float at[3];
  sky_math::CameraPlace(deroll, at);
  Near("the camera with no roll keeps the place",
       fabsf(at[0] - place[0]) + fabsf(at[1] - place[1]) +
           fabsf(at[2] - place[2]),
       0.0, 0.05);

  // A sprite that follows its speed: the game measures its angle with the
  // camera matrix. Measured with the camera with no roll, the turn of the
  // draw gives the angle that the camera of the head gives.
  const float speeds[3][3] = {
      {1.0f, 0.5f, 0.0f}, {-0.3f, 2.0f, 0.7f}, {0.2f, -1.0f, -1.5f}};
  float speed_most = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float with_head = SpeedAngle(rolled, speeds[i]);
    const float no_roll = SpeedAngle(deroll, speeds[i]);
    const float d = fabsf(sprite_math::WrapAngle(no_roll + t - with_head));
    if (d > speed_most) speed_most = d;
  }
  Near("a speed sprite measured with no roll and turned at the draw",
       speed_most, 0.0, 1e-4);

  Near("an angle of 3.5 pi wraps to -0.5 pi",
       sprite_math::WrapAngle(3.5f * 3.14159265f), -0.5 * 3.14159265, 1e-5);
}

}  // namespace sprite_checks
