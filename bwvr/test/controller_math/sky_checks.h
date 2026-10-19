// THE SKY WITH NO HEAD IN IT (docs/backend.md, "Sky, clouds and detail").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/sky_math.h"

namespace sky_checks {

// A camera matrix of the engine for a head at `place` that looks along yaw
// and pitch, with roll about the forward. Degrees.
inline void HeadCamera(const float place[3], float yaw, float pitch,
                       float roll, float m[12]) {
  const float d = 3.14159265f / 180.0f;
  const float cy = cosf(yaw * d), sy = sinf(yaw * d);
  const float cp = cosf(pitch * d), sp = sinf(pitch * d);
  const float cr = cosf(roll * d), sr = sinf(roll * d);
  const float forward[3] = {sy * cp, sp, cy * cp};
  const float right0[3] = {cy, 0.0f, -sy};
  float up0[3];
  sky_math::Cross3(forward, right0, up0);
  float axes[3][3];
  for (int k = 0; k < 3; ++k) {
    axes[0][k] = cr * right0[k] + sr * up0[k];
    axes[1][k] = cr * up0[k] - sr * right0[k];
    axes[2][k] = forward[k];
  }
  for (int j = 0; j < 3; ++j) {
    for (int k = 0; k < 3; ++k) m[3 * k + j] = axes[j][k];
    m[9 + j] = -sky_math::Dot3(place, axes[j]);
  }
}

inline float Largest9(const float a[9], const float b[9]) {
  float most = 0.0f;
  for (int i = 0; i < 9; ++i) {
    const float d = fabsf(a[i] - b[i]);
    if (d > most) most = d;
  }
  return most;
}

inline void SkyChecks() {
  using namespace sky_math;
  printf("\nThe sky with no head in it\n");

  const float place[3] = {1200.0f, 340.0f, -560.0f};
  const float moon[3] = {place[0] + 30000.0f, place[1] + 18000.0f,
                         place[2] + 9000.0f};

  // The camera of a head: its place is the place that it was given.
  float head[12];
  HeadCamera(place, 35.0f, -50.0f, 20.0f, head);
  float at[3];
  CameraPlace(head, at);
  Near("the place of a head camera", at[0] - place[0] + at[1] - place[1] +
       at[2] - place[2], 0.0, 0.01);

  // The level camera keeps the place and has world up.
  float level[12];
  const bool made = LevelCamera(head, level);
  Check(made, "a head camera with roll gives a level camera", made, 1);
  CameraPlace(level, at);
  Near("the level camera keeps the place, x", at[0], place[0], 0.05);
  Near("the level camera keeps the place, y", at[1], place[1], 0.05);
  Near("the level camera keeps the place, z", at[2], place[2], 0.05);
  float up[3];
  CameraAxis(level, 1, up);
  Near("the up of the level camera is world up", up[1], 1.0, 1e-6);
  Check(IsRotation(level), "the level camera is a true rotation",
        IsRotation(level), 1);

  // With the head camera, the moon turns with the roll of the head. The head
  // looks at the moon (yaw 73.3 and pitch 29.9 degrees), with a roll of 20
  // degrees. The right of the moon is then 20 degrees from its right with no
  // roll.
  float rolled[9], straight[9], rolled_cam[12], straight_cam[12];
  HeadCamera(place, 73.3f, 29.9f, 20.0f, rolled_cam);
  HeadCamera(place, 73.3f, 29.9f, 0.0f, straight_cam);
  MoonTurn(rolled_cam, moon, rolled);
  MoonTurn(straight_cam, moon, straight);
  Near("with the head camera a roll of 20 degrees turns the moon 20 degrees",
       acosf(Dot3(rolled, straight)) * 57.29578f, 20.0, 0.5);

  // With the level camera, the turn of the moon is the same for each turn of
  // the head at that place, and its right is level.
  const float poses[5][3] = {{35.0f, -50.0f, 20.0f}, {35.0f, -50.0f, -25.0f},
                             {-80.0f, 10.0f, 0.0f}, {170.0f, -89.9f, 40.0f},
                             {0.0f, 0.0f, 0.0f}};
  float first[9];
  float most = 0.0f, tilt_most = 0.0f;
  bool all_made = true;
  for (int i = 0; i < 5; ++i) {
    float m[12], lv[12], turn[9];
    HeadCamera(place, poses[i][0], poses[i][1], poses[i][2], m);
    if (!LevelCamera(m, lv)) all_made = false;
    MoonTurn(lv, moon, turn);
    if (i == 0) {
      for (int k = 0; k < 9; ++k) first[k] = turn[k];
    } else {
      const float d = Largest9(first, turn);
      if (d > most) most = d;
    }
    if (fabsf(turn[1]) > tilt_most) tilt_most = fabsf(turn[1]);
  }
  Check(all_made, "each head pose gives a level camera, also straight down",
        all_made, 1);
  Near("the moon has one turn for five head poses", most, 0.0, 1e-4);
  Near("the right of the moon is level", tilt_most, 0.0, 1e-5);

  // With the head straight ahead, the level camera gives the game's own
  // turn.
  float game[12], game_level[12], game_turn[9], level_turn[9];
  HeadCamera(place, 35.0f, 0.0f, 0.0f, game);
  LevelCamera(game, game_level);
  MoonTurn(game, moon, game_turn);
  MoonTurn(game_level, moon, level_turn);
  Near("a level head gives the game's own turn of the moon",
       Largest9(game_turn, level_turn), 0.0, 1e-5);

  // A matrix with a scale is not a camera: it stays as it is.
  float scaled[12];
  for (int i = 0; i < 12; ++i) scaled[i] = head[i] * 1.5f;
  float unused[12];
  const bool refused = !LevelCamera(scaled, unused);
  Check(refused, "a matrix with a scale gives no level camera", refused, 1);

  // The scale of a cloud: the engine's formula, from two eyes.
  const float cloud[3] = {0.0f, 400.0f, 0.0f};
  const float below[3] = {0.0f, 0.0f, 0.0f};
  const float side[3] = {1000.0f, 400.0f, 0.0f};
  Near("a cloud straight above is not flattened",
       MistScale(30.0f, true, 4.0f, cloud, below), 30.0, 1e-4);
  Near("a cloud at the horizon is flattened by the flatten term",
       MistScale(30.0f, true, 4.0f, cloud, side), 7.5, 1e-4);
  Near("ground mist is not flattened",
       MistScale(30.0f, false, 4.0f, cloud, side), 30.0, 1e-6);
  Near("a cloud at the eye has no scale",
       MistScale(30.0f, true, 4.0f, cloud, cloud), 0.0, 1e-6);
  // The ratio that the mod gives rows 1 and 2: the engine's scale from the
  // head times the ratio is the scale from the table camera.
  const float a_head = MistScale(30.0f, true, 4.0f, cloud, side);
  const float a_table = MistScale(30.0f, true, 4.0f, cloud, below);
  Near("the ratio gives the scale from the table camera",
       a_head * (a_table / a_head), a_table, 1e-4);
}

}  // namespace sky_checks
