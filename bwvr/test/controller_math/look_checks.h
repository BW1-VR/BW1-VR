// THE CREATURE LOOKS AT THE PLAYER (docs/creature.md, "The Creature looks at
// the player"). main.cpp includes this file after Check and Near, just before
// main.
#pragma once

#include "../../src/mod/creature_look_math.h"

namespace look_checks {

// The head yaw and the head pitch that LH3DCreature::UpdateLook gives for a
// point: the yaw of the line from the eyes less the heading, in -pi to pi
// (GVillagerStateTableInfo::GetInfo), and the asin of its up part.
inline void GameTurn(const float base[3], float scale, float heading,
                     const float to[3], float* yaw, float* pitch) {
  float eye[3];
  look_math::EyePoint(base, scale, eye);
  float d[3] = {to[0] - eye[0], to[1] - eye[1], to[2] - eye[2]};
  const float length = look_math::Length(d);
  for (int i = 0; i < 3; ++i) d[i] /= length;
  float y = look_math::YawOf(d[0], d[2]) - heading;
  if (y > 3.14159265f) y -= 6.2831853f;
  if (y < -3.14159265f) y += 6.2831853f;
  *yaw = y;
  *pitch = asinf(d[1]);
}

inline void LookChecks() {
  using namespace look_math;
  printf("\nThe Creature looks at the player\n");

  // THE KEY.
  Check(ClampMode(1) == kModeHead && ClampMode(0) == kModeCamera &&
            ClampMode(5) == kModeHead,
        "creature_look_head: 0 is the game camera, and each other value is "
        "the head", ClampMode(5), kModeHead);

  // THE WORDS OF A LOOK FUNCTION: x and z in map units, the height over the
  // land as a float. fn_004D03B0 makes the same point of them.
  {
    const float head[3] = {1923.37f, 61.25f, 2490.81f};
    const float land = 38.5f;
    int32_t words[3];
    HeadWords(head, land, words);
    float back[3];
    PointOfWords(words, land, back);
    Near("the point of the head comes back from the words, x", back[0],
         head[0], 1.0 / kMapUnits + 1e-3);
    Near("and y", back[1], head[1], 1e-4);
    Near("and z", back[2], head[2], 1.0 / kMapUnits + 1e-3);
    Check(words[0] == static_cast<int32_t>(1923.37f * 6553.6f),
          "x in map units is cut as __ftol cuts it", words[0],
          static_cast<int32_t>(1923.37f * 6553.6f));
    const float under[3] = {10.0f, 5.0f, 10.0f};
    HeadWords(under, 20.0f, words);
    PointOfWords(words, 20.0f, back);
    Near("a head under the land height keeps its height", back[1], 5.0,
         1e-4);
  }

  // THE YAW OF THE GAME: 0 is minus z, pi / 2 is plus x.
  Near("the yaw of minus z is 0", YawOf(0.0f, -1.0f), 0.0, 1e-6);
  Near("the yaw of plus x is pi / 2", YawOf(1.0f, 0.0f), 1.5707963, 1e-5);
  {
    float d[3];
    EyeDirection(0.3f, 0.4f, 0.0f, d);
    Near("the direction of the eyes has the yaw of heading and head yaw",
         YawOf(d[0], d[2]), 0.7, 1e-5);
    EyeDirection(0.0f, 0.0f, 0.5f, d);
    Near("and the pitch of the head", asinf(d[1]), 0.5, 1e-5);
  }

  // THE EYES. A Creature at rest looks along its heading at the height of
  // its eyes.
  {
    const float base[3] = {100.0f, 30.0f, 200.0f};
    const float ahead[3] = {100.0f, 30.0f + kEyeUp * 1.2f, 150.0f};
    Near("a point ahead at the height of the eyes is 0 degrees",
         EyesToDeg(base, 1.2f, 0.0f, 0.0f, 0.0f, ahead), 0.0, 1e-3);
    const float side[3] = {150.0f, 30.0f + kEyeUp * 1.2f, 200.0f};
    Near("a point at plus x is 90 degrees", EyesToDeg(base, 1.2f, 0.0f,
                                                       0.0f, 0.0f, side),
         90.0, 1e-3);
  }

  // THE TURN OF THE GAME PUTS THE EYES ON THE POINT. With the head yaw and
  // the head pitch that UpdateLook gives, the angle is 0 for each head pose.
  // So the angle of the log is 0 when the look of the game has come to the
  // head.
  {
    const float base[3] = {1900.0f, 40.0f, 2520.0f};
    const float scale = 1.4f;
    const float headings[4] = {0.0f, 1.0f, -2.5f, 3.0f};
    const float heads[4][3] = {{1950.0f, 110.0f, 2460.0f},
                               {1870.0f, 95.0f, 2560.0f},
                               {1905.0f, 160.0f, 2525.0f},
                               {1990.0f, 45.0f, 2500.0f}};
    float worst = 0.0f;
    for (int h = 0; h < 4; ++h) {
      for (int p = 0; p < 4; ++p) {
        float yaw = 0.0f, pitch = 0.0f;
        GameTurn(base, scale, headings[h], heads[p], &yaw, &pitch);
        const float deg =
            EyesToDeg(base, scale, headings[h], yaw, pitch, heads[p]);
        if (deg > worst) worst = deg;
      }
    }
    Near("the turn of the game puts the eyes on the head, degrees (worst)",
         worst, 0.0, 0.05);

    // A lean of 30 units to the side moves the head. The eyes that were on
    // the head are then off it by the angle of the lean, as seen from the
    // eyes.
    const float head[3] = {1950.0f, 110.0f, 2460.0f};
    const float leaned[3] = {1980.0f, 110.0f, 2460.0f};
    float yaw = 0.0f, pitch = 0.0f;
    GameTurn(base, scale, 0.0f, head, &yaw, &pitch);
    float eye[3];
    EyePoint(base, scale, eye);
    float dir[3];
    EyeDirection(0.0f, yaw, pitch, dir);
    Near("after a lean the eyes are off the head by the angle of the lean",
         EyesToDeg(base, scale, 0.0f, yaw, pitch, leaned),
         AngleToDeg(dir, eye, leaned), 1e-4);
    Check(EyesToDeg(base, scale, 0.0f, yaw, pitch, leaned) > kOnDeg,
          "a lean of 30 units at this distance is more than the angle of "
          "\"on the target\"", EyesToDeg(base, scale, 0.0f, yaw, pitch,
                                         leaned), kOnDeg);
  }

  {
    const float ahead[3] = {0.0f, 0.0f, -1.0f};
    const float at[3] = {1.0f, 2.0f, 3.0f};
    Near("a line of no length gives 0 degrees", AngleToDeg(ahead, at, at),
         0.0, 1e-6);
  }
}

}  // namespace look_checks
