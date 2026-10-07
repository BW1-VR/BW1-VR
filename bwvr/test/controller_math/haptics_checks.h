// HAPTICS (docs/hand.md, "Haptics"), and the leash from a distance.
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/haptics_math.h"
#include "../../src/mod/near_math.h"

namespace haptics_checks {

inline void HapticsChecks() {
  using namespace haptics_math;
  printf("\nHaptics\n");

  // THE SET. Each kind is short and none is a buzz.
  {
    float most = 0.0f, longest = 0.0f, least = 1.0f;
    bool names = true;
    for (int k = 0; k < kKinds; ++k) {
      const Pulse p = Scaled(k, 1.0f, 100);
      if (p.amplitude > most) most = p.amplitude;
      if (p.amplitude < least) least = p.amplitude;
      if (p.seconds > longest) longest = p.seconds;
      if (kNames[k] == nullptr || kNames[k][0] == 0) names = false;
    }
    Check(most <= kMostAmplitude + 1e-6f,
          "no pulse is stronger than 0.75 at haptics_pct=100", most, 0.75);
    Check(longest <= 0.040f + 1e-6f, "no pulse is longer than 40 ms", longest,
          0.040);
    Check(least > 0.04f, "the faintest pulse is more than 0.04", least, 0.04);
    Check(names, "each kind has a name for the report", names, 1);
    Near("the arrive pulse is the 0.40 of Half-Life: Alyx",
         Scaled(kArrive, 1.0f, 100).amplitude, 0.40, 1e-6);
    Near("and it is 11 ms long", Scaled(kArrive, 1.0f, 100).seconds, 0.011,
         1e-6);
    Check(Scaled(kLock, 1.0f, 100).amplitude <
              Scaled(kArrive, 1.0f, 100).amplitude &&
              Scaled(kTarget, 1.0f, 100).amplitude <
                  Scaled(kLock, 1.0f, 100).amplitude,
          "a new target is fainter than a lock, and a lock than an arrival",
          1, 1);
    Check(Scaled(kNoTarget, 1.0f, 100).amplitude <
              Scaled(kTake, 1.0f, 100).amplitude,
          "a press with no target is fainter than a take", 1, 1);
  }

  // haptics_pct.
  {
    Near("haptics_pct=50 gives half the amplitude",
         Scaled(kTake, 1.0f, 50).amplitude,
         0.5 * Scaled(kTake, 1.0f, 100).amplitude, 1e-6);
    Near("and the same time", Scaled(kTake, 1.0f, 50).seconds,
         Scaled(kTake, 1.0f, 100).seconds, 1e-9);
    Near("haptics_pct=0 gives no pulse", Scaled(kSlap, 1.0f, 0).amplitude,
         0.0, 1e-9);
    Near("haptics_pct=200 does not pass 0.75",
         Scaled(kSlap, 1.0f, 200).amplitude, 0.75, 1e-6);
    Near("a kind that is not in the table gives no pulse",
         Scaled(kKinds, 1.0f, 100).amplitude, 0.0, 1e-9);
    Near("a strength of 0 gives no pulse", Scaled(kSlap, 0.0f, 100).amplitude,
         0.0, 1e-9);
  }

  // THE SLAP, at creature_slap_mps=1.
  {
    Near("a slap that just counts has a strength of 0.55",
         SlapStrength(1.0f, 1.0f), 0.55, 1e-6);
    Near("which is 0.41 of the full amplitude",
         Scaled(kSlap, SlapStrength(1.0f, 1.0f), 100).amplitude, 0.4125, 1e-4);
    Near("a hard slap at 1.8 m/s has the full strength",
         SlapStrength(1.8f, 1.0f), 1.0, 1e-6);
    Near("and a slap at 3 m/s has no more", SlapStrength(3.0f, 1.0f), 1.0,
         1e-6);
    Check(SlapStrength(1.4f, 1.0f) > SlapStrength(1.2f, 1.0f),
          "a faster slap is a stronger pulse", 1, 1);
    Near("with no speed the strength is the least one",
         SlapStrength(0.0f, 0.0f), 0.55, 1e-6);
  }

  // THE TEXTURE OF A STROKE.
  {
    float path = 0.0f;
    int ticks = 0;
    for (int i = 0; i < 300; ++i) {
      if (StrokeTick(&path, 0.0f)) ++ticks;
    }
    Check(ticks == 0, "a hand that is still for 300 frames gives no tick",
          ticks, 0);
    // A stroke at 0.3 m/s and 90 frames a second: 0.333 cm a frame, 30 cm in
    // a second.
    path = 0.0f;
    ticks = 0;
    for (int i = 0; i < 90; ++i) {
      if (StrokeTick(&path, 30.0f / 90.0f)) ++ticks;
    }
    Check(ticks >= 19 && ticks <= 20,
          "a stroke of 30 cm gives a tick for each 1.5 cm: 20 ticks", ticks,
          20);
    path = 0.0f;
    ticks = 0;
    for (int i = 0; i < 90; ++i) {
      if (StrokeTick(&path, 3.2f / 90.0f)) ++ticks;
    }
    Check(ticks == 2, "a slow stroke of 3.2 cm in a second gives 2 ticks",
          ticks, 2);
    path = 0.0f;
    Check(StrokeTick(&path, 500.0f) && path < kStrokeStepCm,
          "a jump of the tracking gives one tick, and no row of ticks after "
          "it", path, 0.0);
  }

  // A FETCHED OBJECT ARRIVES: a glide of 0.4 s.
  {
    Check(!ArriveNow(0.4f, 0.0f) && !ArriveNow(0.4f, 0.27f),
          "no pulse in the first 0.27 s of a glide of 0.4 s", 0, 0);
    Check(ArriveNow(0.4f, 0.28f) && ArriveNow(0.4f, 0.4f),
          "a pulse on each frame of the last 0.125 s", 1, 1);
    Check(!ArriveNow(0.4f, 0.41f), "and none after the object is in the hand",
          0, 0);
    Check(ArriveNow(0.1f, 0.0f),
          "a glide shorter than 0.125 s pulses from its start", 1, 1);
    Check(!ArriveNow(0.0f, 0.0f), "no glide gives no pulse", 0, 0);
    int pulses = 0;
    for (int i = 0; i < 36; ++i) {
      if (ArriveNow(0.4f, i / 90.0f)) ++pulses;
    }
    Check(pulses >= 11 && pulses <= 12,
          "at 90 frames a second that is 11 or 12 pulses", pulses, 11);
  }
}

// THE LEASH FROM A DISTANCE, and the Creature in the rule of the choice.
inline void LeashChecks() {
  using namespace near_math;
  printf("\nThe leash from a distance\n");

  // WHAT THE CLICK DOES (mine, ready, other, on, tied).
  Check(LeashClick(true, true, false, false, false) == kLeashPut,
        "no leash on the player's own Creature: the click puts it on",
        LeashClick(true, true, false, false, false), kLeashPut);
  Check(LeashClick(true, true, false, true, false) == kLeashOff,
        "the leash on the Hand: the click takes it off",
        LeashClick(true, true, false, true, false), kLeashOff);
  Check(LeashClick(true, true, false, true, true) == kLeashBack,
        "the leash tied to an object: the click takes it back to the Hand",
        LeashClick(true, true, false, true, true), kLeashBack);
  Check(LeashClick(false, true, false, false, false) == kLeashNone,
        "the Creature of another player is no target of the leash", 0, 0);
  Check(LeashClick(true, false, false, false, false) == kLeashNone,
        "a Creature that can have no leash now is no target", 0, 0);
  Check(LeashClick(true, true, true, false, false) == kLeashNone,
        "a Creature on the leash of another player is no target", 0, 0);

  // THE BODY BONE BEFORE A PRESS: a Creature of size 0.22 is 3.3 units high.
  Check(BoneSane(0.6f, -0.4f, 0.22f),
        "a body bone 0.7 units from the place of a small Creature counts", 1,
        1);
  Check(BoneSane(5.2f, 0.0f, 0.22f) && !BoneSane(5.4f, 0.0f, 0.22f),
        "it counts to 5.3 units: the height of the Creature and 2 units", 1,
        1);
  Check(!BoneSane(300.0f, 40.0f, 0.22f),
        "an old bone from a different place does not count", 0, 0);
  Check(BoneSane(12.0f, 9.0f, 1.0f) && !BoneSane(12.0f, 13.0f, 1.0f),
        "a grown Creature of size 1 has a reach of 17 units", 1, 1);
  {
    volatile float zero = 0.0f;
    const float no_number = zero / zero;
    Check(!BoneSane(no_number, 0.0f, 0.22f) && !BoneSane(0.0f, 0.0f, 0.0f),
          "a bone that is no number, or a Creature with no size, does not "
          "count", 0, 0);
  }

  // THE REACH OF A SCROLL.
  Near("the reach of a scroll is scroll_reach_cm when that is more than the "
       "reach of the pick", ScrollReach(1.18f, 2.37f), 2.37, 1e-6);
  Near("and never less than the reach of the pick", ScrollReach(1.18f, 0.0f),
       1.18, 1e-6);
  Check(kScrollReachCm > kPickCm, "the default reach of a scroll is more "
        "than the reach of the pick", kScrollReachCm, kPickCm);
  Check(kScrollReachCm == kNearInCm, "and it is the gap at which the grab "
        "from a distance goes off, so no gap is between the two",
        kScrollReachCm, kNearInCm);
  // A press on a scroll: a tap, a click for the scripts, or nothing.
  Check(ScrollPress(true, true, true) == kScrollPressTap &&
            ScrollPress(true, false, false) == kScrollPressTap,
        "a press on a scroll that the game taps is a tap, with each key",
        ScrollPress(true, true, true), kScrollPressTap);
  Check(ScrollPress(false, true, true) == kScrollPressClick,
        "a press on a scroll that is drawn and not active is a click for "
        "the scripts", ScrollPress(false, true, true), kScrollPressClick);
  Check(ScrollPress(false, true, false) == kScrollPressNone,
        "with scroll_click=0 such a scroll is no target, as in the builds "
        "before", ScrollPress(false, true, false), kScrollPressNone);
  Check(ScrollPress(false, false, true) == kScrollPressNone,
        "a scroll that the game does not draw is no target of a click",
        ScrollPress(false, false, true), kScrollPressNone);
  {
    // 100 units a meter: the reach of the pick is 4 units, of a scroll 10.
    const float touch = 1.0f, eps = 0.1f;
    const Target outer = {{7.0f, 10.1f}, kTap, 3.08f, true, false, true};
    const Target inner = {{2.0f, 5.1f}, kTap, 3.08f, true, false, false};
    const Target tree_in = {{0.0f, 2.0f}, kHold, 6.0f, false, true, false};
    const Target villager = {{3.0f, 3.5f}, kHold, 1.0f, false, false, false};
    Check(TargetRule(outer, touch) == 5 && TargetRule(inner, touch) == 2,
          "a scroll past the reach of the pick is rule 5, and a scroll in it "
          "is rule 2", TargetRule(outer, touch), 5);
    Check(BeatsTarget(tree_in, outer, touch, eps) &&
              !BeatsTarget(outer, tree_in, touch, eps),
          "a tree that the point is in wins over a scroll 7 cm away", 1, 1);
    Check(BeatsTarget(villager, outer, touch, eps),
          "a villager 3 cm away wins over a scroll 7 cm away", 1, 1);
    Check(BeatsTarget(inner, tree_in, touch, eps) &&
              BeatsTarget(inner, villager, touch, eps),
          "a scroll 2 cm away wins over the two, as before", 1, 1);
    const Target outer_near = {{5.0f, 8.1f}, kTap, 3.08f, true, false, true};
    Check(BeatsTarget(outer_near, outer, touch, eps),
          "of two scrolls past the reach of the pick the nearer one wins", 1,
          1);
  }

  // THE PLACE OF A POINT OF THE PICTURE. Two cameras 6.4 units apart look
  // along +z, with a field of view term of 0.8 across and 1.4 up.
  {
    // cam = p * rows + translation, with the camera at (-3.2, 10, -5) and at
    // (3.2, 10, -5).
    const float left[12] = {0.8f, 0, 0, 0, 1.4f, 0, 0, 0, 1,
                            0.8f * 3.2f, -1.4f * 10.0f, 5.0f};
    const float right[12] = {0.8f, 0, 0, 0, 1.4f, 0, 0, 0, 1,
                             -0.8f * 3.2f, -1.4f * 10.0f, 5.0f};
    const float hx = 1280.0f, hy = 720.0f;
    const float point[3] = {4.0f, 3.5f, 37.0f};
    float px[2][2];
    const float* m[2] = {left, right};
    for (int e = 0; e < 2; ++e) {
      const float cx = point[0] * m[e][0] + point[1] * m[e][3] +
                       point[2] * m[e][6] + m[e][9];
      const float cy = point[0] * m[e][1] + point[1] * m[e][4] +
                       point[2] * m[e][7] + m[e][10];
      const float cz = point[0] * m[e][2] + point[1] * m[e][5] +
                       point[2] * m[e][8] + m[e][11];
      px[e][0] = (1.0f + cx / cz) * hx;
      px[e][1] = (1.0f - cy / cz) * hy;
    }
    float out[3] = {0, 0, 0};
    const bool ok = PicturePoint(left, right, hx, hy, px[0][0], px[0][1],
                                 px[1][0], px[1][1], out);
    Check(ok, "a pixel pair of the two eye pictures gives a place", ok, 1);
    Near("which is the point that was drawn: x", out[0], 4.0, 0.01);
    Near("y", out[1], 3.5, 0.01);
    Near("and z", out[2], 37.0, 0.05);
    Check(px[0][0] > px[1][0], "the left eye sees a near point more to the "
          "right than the right eye does", px[0][0], px[1][0]);
    float none[3];
    Check(!PicturePoint(left, right, hx, hy, 900.0f, 500.0f, 900.0f, 500.0f,
                        none),
          "the same pixel in both eyes is a point at no distance, so it "
          "gives no place", 0, 0);
    Check(!PicturePoint(left, right, hx, hy, 800.0f, 500.0f, 900.0f, 500.0f,
                        none),
          "a pixel pair that crosses behind the cameras gives no place", 0,
          0);
    // One pixel of error in one eye moves the place along the line of sight.
    float off[3];
    PicturePoint(left, right, hx, hy, px[0][0] + 1.0f, px[0][1], px[1][0],
                 px[1][1], off);
    Check(fabsf(off[2] - 37.0f) < 2.0f && fabsf(off[0] - 4.0f) < 0.2f,
          "one pixel of error at 37 units is less than 2 units of depth",
          off[2], 37.0);
  }

  // THE CREATURE AND A SCROLL (run hk4): the Creature stands 4 cm of the room
  // from a scroll, and the Hand's point is in its body.
  {
    const float touch = 1.0f, eps = 0.1f;
    const Target creature = {{0.0f, 1.2f}, kCreature, 1.65f, false, false};
    const Target scroll = {{3.4f, 6.5f}, kTap, 3.08f, true, false};
    Check(BeatsTarget(creature, scroll, touch, eps) &&
              !BeatsTarget(scroll, creature, touch, eps),
          "the Creature that the point touches wins over a scroll 3.4 cm "
          "away", 1, 1);
    const Target creature_away = {{2.0f, 3.2f}, kCreature, 1.65f, false,
                                  false};
    Check(BeatsTarget(scroll, creature_away, touch, eps),
          "a scroll in the reach wins over the Creature 2 cm away", 1, 1);
    const Target small_tree = {{0.0f, 1.0f}, kHold, 1.84f, false, true};
    Check(BeatsTarget(scroll, small_tree, touch, eps) &&
              !BeatsTarget(small_tree, scroll, touch, eps),
          "the scroll wins over a small tree that the point is in", 1, 1);
    const Target house = {{0.0f, 4.0f}, kTap, 7.0f, false, true};
    Check(BeatsTarget(scroll, house, touch, eps),
          "and over a house that the point touches", 1, 1);
    const Target scroll_in = {{0.0f, 1.0f}, kTap, 3.08f, true, false};
    Check(BeatsTarget(creature, scroll_in, touch, eps),
          "the point in the scroll and in a small Creature: the smaller body",
          1, 1);
    const Target grown = {{0.0f, 9.0f}, kCreature, 12.0f, false, false};
    Check(BeatsTarget(scroll_in, grown, touch, eps),
          "the point in the scroll and in a grown Creature: the scroll", 1, 1);
    Check(TargetRule(creature, touch) == 1 && TargetRule(scroll, touch) == 2 &&
              TargetRule(small_tree, touch) == 3 &&
              TargetRule(creature_away, touch) == 4,
          "the four rules, in their order", TargetRule(small_tree, touch), 3);
  }
}

}  // namespace haptics_checks
