// A MIRACLE IN THE HAND (docs/miracles.md, "A miracle in the Hand").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/creature_command_math.h"
#include "../../src/mod/miracle_math.h"

namespace miracle_checks {

inline void MiracleChecks() {
  using namespace miracle_math;
  printf("\nA miracle in the Hand\n");

  // The seeds of the game's table (Scripts\info.dat, DETAIL_SPELL_SEEDS).
  const Seed strong = {16, kCastPlace, true, true, 0};
  const Seed heal = {7, kCastPlace, false, false, 0};
  const Seed water = {9, kCastPour, true, false, 0};
  const Seed fire = {2, kCastThrow, false, false, 0};
  const Seed shield = {4, kCastThrow, false, false, 4};

  // WHAT THE CARD SAYS.
  Check(PromptOf(true, false, strong, false, false, false) == kPromptSeedHeld,
        "a seed that the Hand carries has its own lines", 1, 1);
  Check(PromptOf(false, false, strong, false, false, false) == kPromptNone,
        "no miracle and no seed: no lines", 1, 1);
  Check(PromptOf(false, true, strong, false, false, false) ==
            kPromptNeedTarget,
        "a Creature miracle with no target asks for a target", 1, 1);
  Check(PromptOf(false, true, strong, true, false, false) == kPromptTarget,
        "a Creature miracle with a target: the grip casts on it", 1, 1);
  Check(PromptOf(false, true, strong, true, false, true) == kPromptRuns,
        "a Creature miracle whose spell runs says so", 1, 1);
  Check(PromptOf(false, true, heal, false, false, false) == kPromptPlace,
        "heal is cast at the place of the Hand", 1, 1);
  Check(PromptOf(false, true, water, false, false, false) == kPromptPour,
        "water: hold the grip to pour", 1, 1);
  Check(PromptOf(false, true, water, false, true, true) == kPromptPouring,
        "water with the grip closed: open to stop, also while its spell runs",
        1, 1);
  Check(PromptOf(false, true, fire, false, false, false) == kPromptThrow,
        "a fireball: hold, swing and open", 1, 1);
  Check(PromptOf(false, true, fire, false, true, false) == kPromptHeld,
        "a fireball with the grip closed: open to throw", 1, 1);
  Check(PromptOf(false, true, shield, false, false, false) == kPromptCircle,
        "a shield needs a circle", 1, 1);
  Check(PromptOf(false, true, water, true, false, false) == kPromptTarget,
        "water with its Worship Site as the target: a press gives it back", 1,
        1);
  Check(PromptOf(false, true, water, true, true, true) == kPromptPouring,
        "water that pours keeps the line of the pour", 1, 1);

  // WHY THE GAME REFUSED A CAST.
  Check(RefusalOf(false, true, true, true, 0, strong, true, 0) ==
            kRefuseNotReady,
        "a seed that is not ready is the first reason", 1, 1);
  Check(RefusalOf(true, false, false, false, 0, heal, false, 0) ==
            kRefuseOffMap,
        "a point off the map", 1, 1);
  Check(RefusalOf(true, true, false, true, 1, heal, false, 0) ==
            kRefuseNeedsLand,
        "the cast rule 1 needs land", 1, 1);
  Check(RefusalOf(true, true, false, true, 3, heal, false, 0) ==
            kRefuseNeedsLand,
        "the cast rule 3 needs land first", 1, 1);
  Check(RefusalOf(true, true, true, false, 3, heal, false, 0) ==
            kRefuseInfluence,
        "the cast rule 3 needs the influence", 1, 1);
  Check(RefusalOf(true, true, true, false, 2, strong, true, 0) ==
            kRefuseInfluence,
        "a Creature outside the influence: the influence is the reason", 1, 1);
  Check(RefusalOf(true, true, true, true, 0, strong, false, 0) ==
            kRefuseNeedsTarget,
        "the presses of the log of 2026-10-04: a Creature miracle with no "
        "object in the pick record needs a target", 1, 1);
  Check(RefusalOf(true, true, true, true, 3, shield, false, 0) ==
            kRefuseNeedsCircle,
        "a shield with no circle needs the circle", 1, 1);
  Check(RefusalOf(true, true, true, true, 3, shield, false, 4) ==
            kRefuseOther,
        "a shield with its circle has no reason that the mod knows", 1, 1);
  Check(RefusalOf(true, true, true, false, 0, fire, false, 0) ==
            kRefuseInfluence,
        "a throw from outside the influence, with the cast rule 0", 1, 1);
  for (int r = kRefuseNotReady; r <= kRefuseOther; ++r) {
    Check(RefusalWords(r)[0] != 0, "each reason has words for the log", r, r);
  }

  // THE TARGET. The Hand is at 0 0 0, and the aim goes along +z from it.
  {
    const float hand[3] = {0.0f, 0.0f, 0.0f};
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const float forward[3] = {0.0f, 0.0f, 1.0f};
    const float touch = 1.0f, cone = 6.0f, most = 100.0f;
    // A body of radius 2 with its middle 2.5 from the point: the gap is 0.5.
    const float near_body[3] = {2.5f, 0.0f, 0.0f};
    const Aim at_hand =
        AimAt(hand, origin, forward, true, near_body, 2.0f, touch, cone, most);
    Check(at_hand.valid && at_hand.at_hand, "a body 0.5 units from the point "
          "is a target at the Hand", at_hand.gap, 0.5);
    Near("its gap is the distance to its surface, units", at_hand.gap, 0.5,
         1e-5);
    // The point is inside the body.
    const float inside[3] = {0.5f, 0.0f, 0.0f};
    const Aim in_body =
        AimAt(hand, origin, forward, true, inside, 2.0f, touch, cone, most);
    Check(in_body.valid && in_body.at_hand && in_body.gap == 0.0f,
          "the point inside the body: a gap of 0", in_body.gap, 0.0);
    // A body 40 units along the aim and 2 to the side, radius 2: on the ray.
    const float ahead[3] = {2.0f, 0.0f, 40.0f};
    const Aim in_aim =
        AimAt(hand, origin, forward, true, ahead, 2.0f, touch, cone, most);
    Check(in_aim.valid && !in_aim.at_hand, "a body on the aim ray, 40 units "
          "away, is a target in the aim", in_aim.degrees, 0.0);
    Near("the ray touches it: 0 degrees", in_aim.degrees, 0.0, 1e-4);
    // 10 units to the side at 40 units: atan(8/40) = 11.3 degrees.
    const float beside[3] = {10.0f, 0.0f, 40.0f};
    const Aim off =
        AimAt(hand, origin, forward, true, beside, 2.0f, touch, cone, most);
    Check(!off.valid, "a body 11.3 degrees off the aim is no target",
          off.degrees, 11.3);
    Near("its angle is from its surface, degrees", off.degrees, 11.31, 0.01);
    // Behind the hand.
    const float behind[3] = {0.0f, 0.0f, -40.0f};
    Check(!AimAt(hand, origin, forward, true, behind, 2.0f, touch, cone, most)
               .valid,
          "a body behind the hand is no target", 0, 0);
    // Past the range.
    const float far_body[3] = {0.0f, 0.0f, 150.0f};
    Check(!AimAt(hand, origin, forward, true, far_body, 2.0f, touch, cone,
                 most).valid,
          "a body past miracle_max_m is no target", 150, 100);
    Check(AimAt(hand, origin, forward, true, far_body, 2.0f, touch, cone,
                0.0f).valid,
          "with no limit it is a target", 150, 0);
    // No aim ray: only the Hand counts.
    Check(!AimAt(hand, origin, forward, false, ahead, 2.0f, touch, cone, most)
               .valid,
          "with no aim ray a far body is no target", 0, 0);
    Check(AimAt(hand, origin, forward, false, near_body, 2.0f, touch, cone,
                most).valid,
          "with no aim ray the body at the Hand is still the target", 1, 1);
    // The order.
    Check(Better(at_hand, in_aim), "the body at the Hand wins against the "
          "body in the aim", 1, 1);
    Check(!Better(in_aim, at_hand), "and not the other way round", 0, 0);
    Check(Better(in_body, at_hand), "of two bodies at the Hand the smaller "
          "gap wins", 1, 1);
    const float ahead2[3] = {4.0f, 0.0f, 40.0f};
    const Aim in_aim2 =
        AimAt(hand, origin, forward, true, ahead2, 2.0f, touch, cone, most);
    Check(in_aim2.valid && Better(in_aim, in_aim2),
          "of two bodies in the aim the smaller angle wins", in_aim2.degrees,
          2.86);
    Check(Better(in_aim, off) && !Better(off, in_aim),
          "a target wins against no target", 1, 1);
    const Aim none = {false, false, 0.0f, 0.0f};
    Check(!Better(none, none), "no target is not better than no target", 0, 0);
  }

  // THE TARGET STAYS.
  {
    const Aim none = {false, false, 0.0f, 0.0f};
    const Aim kept_aim = {true, false, 40.0f, 8.0f};   // in the keep cone
    const Aim near_ray = {true, false, 60.0f, 5.0f};   // 3 degrees nearer
    const Aim a_bit = {true, false, 60.0f, 7.0f};      // 1 degree nearer
    const Aim at_hand = {true, true, 0.5f, 0.0f};
    const Aim nearer_hand = {true, true, 0.2f, 0.0f};
    Check(Takes(near_ray, none, 2.0f), "with no target a new body takes it",
          1, 1);
    Check(!Takes(none, kept_aim, 2.0f), "no body does not take the target", 0,
          0);
    Check(Takes(near_ray, kept_aim, 2.0f),
          "a body 3 degrees nearer to the ray takes the target", 1, 1);
    Check(!Takes(a_bit, kept_aim, 2.0f),
          "a body 1 degree nearer does not: the target stays", 0, 0);
    Check(Takes(at_hand, kept_aim, 2.0f),
          "a body at the Hand takes the target from a body in the aim", 1, 1);
    Check(!Takes(near_ray, at_hand, 2.0f),
          "a body in the aim does not take it from a body at the Hand", 0, 0);
    Check(Takes(nearer_hand, at_hand, 2.0f) && !Takes(at_hand, nearer_hand,
                                                      2.0f),
          "of two bodies at the Hand the nearer one takes it", 1, 1);
    // A Creature that walks: 6 degrees take it, and it stays to 10.
    const float hand[3] = {0.0f, 0.0f, 0.0f};
    const float forward[3] = {0.0f, 0.0f, 1.0f};
    const float walked[3] = {6.0f, 0.0f, 40.0f};  // 5.7 degrees off its skin
    const Aim as_new =
        AimAt(hand, hand, forward, true, walked, 2.0f, 1.0f, 6.0f, 100.0f);
    const float more[3] = {8.0f, 0.0f, 40.0f};    // 8.5 degrees
    const Aim new_more =
        AimAt(hand, hand, forward, true, more, 2.0f, 1.0f, 6.0f, 100.0f);
    const Aim keep_more =
        AimAt(hand, hand, forward, true, more, 2.0f, 2.0f, 10.0f, 125.0f);
    Check(as_new.valid, "a body 5.7 degrees off the aim is a new target",
          as_new.degrees, 5.7);
    Check(!new_more.valid && keep_more.valid,
          "at 8.5 degrees it is no new target, and the target stays",
          keep_more.degrees, 8.5);
  }

  // THE CANCEL BUTTON.
  Check(CancelTap(0.12f, 0.004f, 0.4f, 0.05f),
        "a press of 120 ms with a still hand is the cancel", 1, 1);
  Check(!CancelTap(0.8f, 0.004f, 0.4f, 0.05f),
        "a press of 800 ms is a draw of a gesture", 0, 0);
  Check(!CancelTap(0.3f, 0.12f, 0.4f, 0.05f),
        "a press of 300 ms with 12 cm of motion is a draw of a gesture", 0, 0);

  // THE USE OF A SEED THAT THE HAND CARRIES.
  {
    Use u = {kUseLetGo, 0};
    bool tap = false;
    // The seed is in the Hand for 6 frames after the trigger.
    for (int i = 0; i < 6; ++i) {
      Check(UseStep(&u, false, true, false, 120, &tap) && !tap,
            "while the Hand holds the seed, no tap", i, i);
    }
    Check(UseStep(&u, true, true, false, 120, &tap) && !tap &&
              u.stage == kUseTap,
          "the Hand is empty: the tap is due", u.stage, kUseTap);
    Check(UseStep(&u, true, true, false, 120, &tap) && tap &&
              u.stage == kUseWait,
          "one frame later the mod sends the tap, once", u.stage, kUseWait);
    Check(UseStep(&u, true, false, false, 120, &tap) && !tap,
          "the seed is gone after the tap: the mod waits for the miracle", 1,
          1);
    Check(UseStep(&u, false, false, true, 120, &tap) && u.stage == kUseIdle,
          "the miracle is in the Hand: the use is over", u.stage, kUseIdle);
    Check(UseStep(&u, false, false, true, 120, &tap) && !tap,
          "an idle use does nothing", 1, 1);
    // The game keeps the seed, for example outside the influence.
    Use kept = {kUseLetGo, 0};
    bool ended = false;
    for (int i = 0; i < 130 && !ended; ++i) {
      ended = !UseStep(&kept, false, true, false, 120, &tap);
    }
    Check(ended && kept.stage == kUseIdle && kept.frames == 121,
          "a seed that stays in the Hand ends the use after 120 frames",
          kept.frames, 121);
    // The seed dies before the Hand is empty.
    Use dead = {kUseLetGo, 0};
    Check(!UseStep(&dead, false, false, false, 120, &tap) &&
              dead.stage == kUseIdle,
          "a seed that is gone before the tap ends the use", 1, 1);
    // No miracle comes after the tap.
    Use silent = {kUseWait, 0};
    ended = false;
    for (int i = 0; i < 130 && !ended; ++i) {
      ended = !UseStep(&silent, true, false, false, 120, &tap);
    }
    Check(ended, "a tap with no answer ends the use after 120 frames",
          silent.frames, 121);
  }

  // THE SHAKE of the desktop runs: the word "pull" steps the hand 15 cm and
  // back, 6 frames apart at 45 frames a second. 5 steps are 4 strokes.
  {
    command_math::ShakeState s = {};
    command_math::ShakeReset(&s);
    command_math::ShakeResult result = {};
    command_math::ShakeResult first = {};
    bool fired = false;
    int fired_frame = -1;
    for (int frame = 0; frame < 80; ++frame) {
      const int steps = frame < 20 ? 0 : (frame - 20) / 6 + 1;
      const bool out = steps > 0 && steps <= 6 && (steps % 2) == 1;
      const float p[3] = {0.0f, 0.0f, out ? 0.15f : 0.0f};
      if (command_math::ShakeStep(&s, 0.6f, p, frame / 45.0, &result) &&
          !fired) {
        fired = true;
        fired_frame = frame;
        first = result;
      }
    }
    Check(fired, "5 steps of 15 cm, 6 frames apart, are a shake", fired, 1);
    Check(fired_frame == 20 + 4 * 6, "it fires at the fifth step",
          fired_frame, 44);
    Check(first.strokes == 4, "with 4 strokes", first.strokes, 4);
  }
}

}  // namespace miracle_checks
