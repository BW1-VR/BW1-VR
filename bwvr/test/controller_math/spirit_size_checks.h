// THE SIZE OF AN ADVISOR AT A PLACE OF THE WORLD (spirit_size_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/spirit_size_math.h"

namespace spirit_size_checks {

inline void SpiritSizeChecks() {
  using namespace spirit_size_math;
  printf("\nThe size of an advisor at a place of the world\n");

  // The numbers of the game: the near depth of the data file, and the size
  // of the good spirit at its usual place (its scale times its model size).
  const float near_depth = 8.733f;
  const float usual_size = 1.361f;

  // The ease of the game.
  Near("the ease is 0 at the usual place", Ease(0.0f), 0.0, 1e-6);
  Near("the ease is 0.5 at half of the way", Ease(0.5f), 0.5, 1e-5);
  Near("the ease is 1 at the place", Ease(1.0f), 1.0, 1e-6);
  Near("an amount below 0 gives 0", Ease(-0.3f), 0.0, 1e-6);
  Near("an amount above 1 gives 1", Ease(1.7f), 1.0, 1e-6);

  // The game's own scale.
  Near("the game makes the spirit 4 times as large at the place",
       Share(kGameGrow, Ease(1.0f)), 4.0, 1e-5);
  Near("and leaves it as it is at the usual place",
       Share(kGameGrow, Ease(0.0f)), 1.0, 1e-6);
  Near("the factor of the game gives the game's 3", Grow(kGameFactor), 3.0,
       1e-6);

  // THE FAULT IN NUMBERS, at the start scale of the God view: 100 units for
  // a meter, a place of the world 80 cm from the camera.
  {
    const float units_per_meter = 100.0f;
    const float gain = units_per_meter / 7.5f;
    const float size = usual_size * gain;
    const float place = 0.8f * units_per_meter;
    const float old_size = size * Share(kGameGrow, 1.0f);
    Near("with the game's factor the spirit is 73 cm in the room",
         old_size / units_per_meter * 100.0f, 72.59, 0.01);
    const float old_angle = AngleDeg(old_size, place);
    Check(old_angle > 45.0f, "and it has an angle of more than 45 degrees",
          old_angle, 48.8);
    const float factor = WorldFactor(place, near_depth * gain);
    const float new_angle = AngleDeg(size * Share(Grow(factor), 1.0f), place);
    const float usual_angle = AngleDeg(size, near_depth * gain);
    Near("with the rule it has the angle of its near depth", new_angle,
         usual_angle, 0.05);
    Check(new_angle < 9.0f, "which is less than 9 degrees", new_angle, 8.91);
  }

  // The same angle at each scale of the God view, for a place 80 cm of the
  // room from the camera.
  {
    const float scales[6] = {7.5f, 29.6f, 100.0f, 400.0f, 1973.0f, 4000.0f};
    bool same = true, room = true;
    float first = 0.0f;
    for (int i = 0; i < 6; ++i) {
      const float gain = scales[i] / 7.5f;
      const float place = 0.8f * scales[i];
      const float factor = WorldFactor(place, near_depth * gain);
      const float size = usual_size * gain * Share(Grow(factor), 1.0f);
      const float angle = AngleDeg(size, place);
      if (i == 0) first = angle;
      if (fabsf(angle - first) > 0.01f) same = false;
      // The size in the room, in cm.
      const float cm = size / scales[i] * 100.0f;
      if (fabsf(cm - 12.47f) > 0.05f) room = false;
    }
    Check(same, "the angle is the same at each scale", same, 1);
    Check(room, "and the spirit is 12.5 cm in the room at each scale", room,
          1);
  }

  // The limits.
  Near("a place at the near depth keeps the usual size",
       WorldFactor(near_depth, near_depth), 1.0, 1e-5);
  Near("a place at half of the near depth gives half of the size",
       WorldFactor(near_depth * 0.5f, near_depth), 0.5, 1e-5);
  Near("a far place gives the 4 of the game, and not more",
       WorldFactor(near_depth * 30.0f, near_depth), 4.0, 1e-6);
  Near("a place at the camera gives a quarter, and not less",
       WorldFactor(0.0f, near_depth), 0.25, 1e-6);
  {
    bool within = true, grows = true;
    float last = 0.0f;
    for (float d = 0.0f; d < 200.0f; d += 0.37f) {
      const float f = WorldFactor(d, near_depth);
      if (f < kLeastFactor || f > kMostFactor) within = false;
      if (f < last) grows = false;
      last = f;
    }
    Check(within, "the factor is always from 1/4 to 4", within, 1);
    Check(grows, "a farther place never gives a smaller spirit", grows, 1);
  }
  // In the game without the table a place is far, and the rule gives the
  // game's own size there: 60 units is more than 4 near depths.
  Near("at 60 units with no table the rule gives the size of the game",
       WorldFactor(60.0f, near_depth), kGameFactor, 1e-6);

  // Numbers that are not usable give the factor of the game.
  const float bad = sqrtf(-1.0f);
  Near("no near depth gives the factor of the game",
       WorldFactor(50.0f, 0.0f), kGameFactor, 1e-6);
  Near("a distance that is not a number gives the factor of the game",
       WorldFactor(bad, near_depth), kGameFactor, 1e-6);
  Near("a near depth that is not a number gives the factor of the game",
       WorldFactor(50.0f, bad), kGameFactor, 1e-6);

  // On the way to the place the scale goes from 1 to the factor, and never
  // past it.
  {
    const float factor = WorldFactor(near_depth * 0.57f, near_depth);
    bool between = true;
    for (float t = 0.0f; t <= 1.0f; t += 0.05f) {
      const float share = Share(Grow(factor), Ease(t));
      if (share > 1.0f + 1e-5f || share < factor - 1e-5f) between = false;
    }
    Check(between, "on the way the scale stays between the two sizes",
          between, 1);
    Near("and it ends at the factor", Share(Grow(factor), Ease(1.0f)), factor,
         1e-5);
  }

  // Where the spirit stays at the place.
  {
    const float place[3] = {1755.0f, 40.0f, 2692.0f};
    const float right[3] = {0.6f, 0.0f, -0.8f};
    float at[3];
    WorldPlace(place, right, 5.0f, 2.0f, at);
    Near("the spirit stays to the left of the place, x", at[0], 1752.0, 1e-3);
    Near("above it", at[1], 42.0, 1e-3);
    Near("the spirit stays to the left of the place, z", at[2], 2696.0, 1e-3);
    const float camera[3] = {1752.0f, 42.0f, 2796.0f};
    Near("the distance of that point", spirit_size_math::Distance(at, camera),
         100.0, 1e-3);
  }

  // The angle.
  Near("1 unit at 10 units is 5.7 degrees", AngleDeg(1.0f, 10.0f), 5.725,
       0.01);
  Near("no distance gives no angle", AngleDeg(1.0f, 0.0f), 0.0, 1e-6);

  // What the spirit does.
  Check(PhaseOf(0.0f, 0.0f) == kUsual, "amount 0 with no goal: the usual "
        "place", PhaseOf(0.0f, 0.0f), kUsual);
  Check(PhaseOf(0.0f, 1.0f) == kLeaves, "a goal above the amount: the spirit "
        "leaves", PhaseOf(0.0f, 1.0f), kLeaves);
  Check(PhaseOf(0.4f, 1.0f) == kLeaves, "also on the way",
        PhaseOf(0.4f, 1.0f), kLeaves);
  Check(PhaseOf(1.0f, 1.0f) == kAtPlace, "the amount at its goal: at the "
        "place", PhaseOf(1.0f, 1.0f), kAtPlace);
  Check(PhaseOf(1.0f, 0.0f) == kReturns, "a goal below the amount: the "
        "spirit returns", PhaseOf(1.0f, 0.0f), kReturns);
  Check(PhaseOf(0.2f, 0.0f) == kReturns, "also on the way back",
        PhaseOf(0.2f, 0.0f), kReturns);
}

}  // namespace spirit_size_checks
