// THE PICK AT THE GLOW (docs/grab.md, "One point for each interaction").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <algorithm>

#include "../../src/mod/near_math.h"

namespace near_checks {

inline void NearChecks() {
  using namespace near_math;
  printf("\nThe pick at the glow\n");

  // AN OBJECT THAT MOVES. 100 units a second is 1 m/s at 100 units a meter.
  {
    const float a[3] = {10.0f, 5.0f, 20.0f}, b[3] = {10.0f, 5.0f, 21.0f};
    Near("a move of 1 unit in 0.01 s is 100 units a second",
         ObjectSpeed(a, b, 0.01f), 100.0, 1e-3);
    Near("no time gives no speed", ObjectSpeed(a, b, 0.0f), 0.0, 1e-9);
    Near("a still object gives no extra reach",
         MovingExtra(10.0f, 15.0f, 0.1f, 8.0f), 0.0, 1e-9);
    Near("a speed at the still limit gives no extra reach",
         MovingExtra(15.0f, 15.0f, 0.1f, 8.0f), 0.0, 1e-9);
    Near("a moving object gets (speed - still) x lag",
         MovingExtra(75.0f, 15.0f, 0.1f, 8.0f), 6.0, 1e-4);
    Near("a fast object gets the cap", MovingExtra(900.0f, 15.0f, 0.1f, 8.0f),
         8.0, 1e-9);
    Near("a lag of 0 gives no extra reach",
         MovingExtra(75.0f, 15.0f, 0.0f, 8.0f), 0.0, 1e-9);
    Near("a cap of 0 gives no extra reach",
         MovingExtra(75.0f, 15.0f, 0.1f, 0.0f), 0.0, 1e-9);
    Check(JustLetGo(0.2, 0.6), "the object let go 0.2 s ago is guarded", 0.2,
          0.6);
    Check(!JustLetGo(0.7, 0.6), "the object let go 0.7 s ago is not guarded",
          0.7, 0.6);
    Check(!JustLetGo(-1.0, 0.6), "a release in the future is no guard", -1.0,
          0.6);
    // THE RULE OF catch_moving. A rock that falls at 75 cm a second has 6 cm
    // of extra reach. With the rule 2 the wide reach of 15 cm is 21 cm for
    // it, so a gap of 15.8 cm is in the reach.
    const float extra = MovingExtra(75.0f, 15.0f, 0.1f, 8.0f);
    Near("rule 2: the extra reach is on the wide reach",
         WideMoving(15.0f, extra, kCatchRest), 21.0, 1e-4);
    Near("rule 1: the wide reach is the same for an object that moves",
         WideMoving(15.0f, extra, kCatchPick), 15.0, 1e-9);
    Near("rule 0: the wide reach is the same for an object that moves",
         WideMoving(15.0f, extra, kCatchOff), 15.0, 1e-9);
    Near("rule 2: a wide reach that is off stays off",
         WideMoving(0.0f, extra, kCatchRest), 0.0, 1e-9);
    Near("rule 2: an object at rest has the wide reach",
         WideMoving(15.0f, 0.0f, kCatchRest), 15.0, 1e-9);
    Check(WidePick(15.8f, 4.0f, WideMoving(15.0f, extra, kCatchRest), 15.8f,
                   0.1f),
          "rule 2: the rock that falls 15.8 cm away is the target", 15.8, 21.0);
    Check(!WidePick(15.8f, 4.0f, WideMoving(15.0f, extra, kCatchPick), 15.8f,
                    0.1f),
          "rule 1: the rock that falls 15.8 cm away is no target", 15.8, 15.0);
    Check(!WidePick(21.5f, 4.0f, WideMoving(15.0f, extra, kCatchRest), 21.5f,
                    0.1f),
          "rule 2: past the wide reach and the extra reach is no target", 21.5,
          21.0);
    Near("rule 3: the extra reach is on the wide reach, as in the rule 2",
         WideMoving(15.0f, extra, kCatchOpen), 21.0, 1e-4);
    // THE GUARD. A throw has it in each rule. An object that fell from a hand
    // that was still has it in the rule 1 only.
    Check(GuardHolds(0.3, 0.6, false, kCatchRest, false),
          "rule 2: a throw of 0.3 s ago is guarded", 0.3, 0.6);
    Check(!GuardHolds(0.3, 0.6, true, kCatchRest, false),
          "rule 2: an object that fell 0.3 s ago is not guarded", 0.3, 0.6);
    Check(GuardHolds(0.3, 0.6, true, kCatchPick, false),
          "rule 1: an object that fell 0.3 s ago is guarded", 0.3, 0.6);
    Check(!GuardHolds(0.7, 0.6, false, kCatchRest, false),
          "rule 2: a throw of 0.7 s ago is not guarded", 0.7, 0.6);
    // THE GUARD ENDS AT THE FULL OPENING (the rule 3). A grip that was fully
    // open closes only when the player closes the hand, and that is a catch.
    Check(GuardHolds(0.05, 0.6, false, kCatchOpen, false),
          "rule 3: a throw of 0.05 s ago is guarded while the grip was not "
          "fully open", 0.05, 0.6);
    Check(!GuardHolds(0.05, 0.6, false, kCatchOpen, true),
          "rule 3: the guard ends when the grip was fully open", 0.05, 0.6);
    Check(GuardHolds(0.05, 0.6, false, kCatchRest, true),
          "rule 2: a throw has the guard for the whole grace time, also "
          "after the grip was fully open", 0.05, 0.6);
    Check(GuardHolds(0.3, 0.6, true, kCatchPick, true),
          "rule 1: the full opening does not end the guard", 0.3, 0.6);
    Check(!GuardHolds(0.3, 0.6, true, kCatchOpen, false),
          "rule 3: an object that fell is not guarded, as in the rule 2", 0.3,
          0.6);
    Check(!GuardHolds(0.7, 0.6, false, kCatchOpen, false),
          "rule 3: the guard ends with the grace time when the grip stays "
          "at a dip", 0.7, 0.6);
    // A throw with a real hand: the fast fall opens the grip at 0.65, the
    // value is below 0.35 two frames later, and the hand closes again 0.3 s
    // after the release. The squeeze of a dip (0.65, then 0.80) never comes
    // to 0.35.
    {
      const float open[4] = {1.0f, 0.65f, 0.40f, 0.10f};
      const float dip[4] = {1.0f, 0.65f, 0.72f, 0.80f};
      bool reopened_open = false, reopened_dip = false;
      for (int i = 1; i < 4; ++i) {
        if (!(open[i] > controller_math::kGripOpenBelow)) reopened_open = true;
        if (!(dip[i] > controller_math::kGripOpenBelow)) reopened_dip = true;
      }
      Check(!GuardHolds(0.3, 0.6, false, kCatchOpen, reopened_open),
            "rule 3: a hand that opened fully and closes 0.3 s after its "
            "throw has the extra reach: a catch", 0.3, 0.6);
      Check(GuardHolds(0.03, 0.6, false, kCatchOpen, reopened_dip),
            "rule 3: a grip that closes again by a dip has no extra reach",
            0.03, 0.6);
    }
  }

  // THE BODY. A tree: its base at the origin, radius 1.5, height 8. All in
  // world units.
  const float base[3] = {100.0f, 20.0f, 300.0f};
  {
    const float inside[3] = {100.5f, 23.0f, 300.0f};
    Gap g = BodyGap(inside, base, 1.5f, 8.0f);
    Near("a point in the body has a gap of 0", g.gap, 0.0, 1e-5);
    Near("its middle is the middle of the height", g.middle,
         sqrt(0.5 * 0.5 + 1.0 * 1.0), 1e-4);
    const float beside[3] = {104.5f, 22.0f, 300.0f};
    g = BodyGap(beside, base, 1.5f, 8.0f);
    Near("a point beside the body: the gap is to the radius", g.gap, 3.0,
         1e-4);
    const float over[3] = {100.0f, 30.0f, 300.0f};
    g = BodyGap(over, base, 1.5f, 8.0f);
    Near("a point over the body: the gap is to the top", g.gap, 2.0, 1e-4);
    const float under[3] = {100.0f, 19.0f, 300.0f};
    g = BodyGap(under, base, 1.5f, 8.0f);
    Near("a point under the base: the gap is to the base", g.gap, 1.0, 1e-4);
    const float corner[3] = {104.5f, 32.0f, 300.0f};
    g = BodyGap(corner, base, 1.5f, 8.0f);
    Near("a point past the top edge: across and up together", g.gap, 5.0,
         1e-4);
    // The crown of a tree. With the old top of two radii the gap was 3.
    const float crown[3] = {100.0f, 26.0f, 300.0f};
    g = BodyGap(crown, base, 1.5f, 8.0f);
    Near("the crown of a tree of height 8 is in the body", g.gap, 0.0, 1e-5);
    g = BodyGap(crown, base, 1.5f, 3.0f);
    Near("with a height of two radii the crown was 3 units out", g.gap, 3.0,
         1e-4);
    g = BodyGap(beside, base, -1.0f, -2.0f);
    Near("a body with no radius and no height is its point", g.gap,
         sqrt(4.5 * 4.5 + 2.0 * 2.0), 1e-4);
  }

  // THE REACH. hand_pick_cm of the room at the scale now.
  {
    Near("4 cm of the room at 100 units a meter is 4 units",
         CmToUnits(4.0f, 100.0f), 4.0, 1e-5);
    Near("4 cm of the room at 44.5 units a meter is 1.78 units",
         CmToUnits(4.0f, 44.5f), 1.78, 1e-4);
    Near("no scale gives no reach", CmToUnits(4.0f, 0.0f), 0.0, 1e-6);
    // A tree 11.38 units from the point, at 100 units a meter and a reach of
    // 11.40.
    Check(11.38f < 0.6f * 3.2f * 5.9375f,
          "the old reach of 60 percent of the Hand took a tree 11.38 cm away",
          11.38, 11.40);
    Check(!(11.38f < CmToUnits(static_cast<float>(kPickCm), 100.0f)),
          "the reach of hand_pick_cm does not", 11.38,
          CmToUnits(static_cast<float>(kPickCm), 100.0f));
  }

  // THE CHOICE.
  {
    const float eps = 0.1f;
    const Gap near_one = {1.0f, 5.0f};
    const Gap far_one = {3.0f, 0.5f};
    Check(Beats(near_one, kTap, far_one, kHold, eps),
          "the nearer body wins, a scroll over a tree too", 1, 1);
    Check(!Beats(far_one, kHold, near_one, kTap, eps),
          "the tree that is farther does not win", 0, 0);
    // The point in both bodies: a villager who stands in a forest.
    const Gap villager = {0.0f, 0.4f};
    const Gap forest = {0.0f, 9.0f};
    Check(Beats(villager, kHold, forest, kHold, eps),
          "in two bodies, the nearer middle wins", 1, 1);
    Check(!Beats(forest, kHold, villager, kHold, eps),
          "and the farther middle does not", 0, 0);
    // A scroll, and a tree that the player set down beside it. The point is
    // in both. The tree is tall, so its middle is far.
    const Gap scroll = {0.0f, 1.2f};
    const Gap tree = {0.0f, 4.1f};
    Check(Beats(scroll, kTap, tree, kHold, eps),
          "the scroll wins over the tree beside it by its middle", 1, 1);
    // The same gap and the same middle: the kind decides.
    const Gap same = {0.5f, 2.0f};
    Check(Beats(same, kHold, same, kTake, eps),
          "at the same place, an object to hold wins over a bulk source", 1,
          1);
    Check(!Beats(same, kTake, same, kHold, eps),
          "and the bulk source does not win over it", 0, 0);
    Check(Beats(same, kTake, same, kTap, eps),
          "a bulk source wins over an object that a press taps", 1, 1);
    // A difference below eps is a tie.
    const Gap a = {0.50f, 2.0f};
    const Gap b = {0.55f, 1.0f};
    Check(Beats(b, kHold, a, kHold, eps),
          "gaps that agree to eps are a tie, and the middle decides", 1, 1);
  }

  // THE WIDE PICK: every gap from 0 to 30 cm has a target or a far grab.
  {
    const float reach = CmToUnits(static_cast<float>(kPickCm), 100.0f);
    const float wide = CmToUnits(static_cast<float>(kWideCm), 100.0f);
    const float eps = CmToUnits(0.1f, 100.0f);
    Check(!WidePick(2.0f, reach, wide, 2.0f, eps),
          "inside the reach of the pick the plain rules choose", 0, 0);
    Check(WidePick(4.0f, reach, wide, 4.0f, eps),
          "at the reach of the pick the wide pick takes it", 1, 1);
    Check(WidePick(9.0f, reach, wide, 9.0f, eps), "9 cm is the target", 1, 1);
    Check(WidePick(14.9f, reach, wide, 14.9f, eps), "14.9 cm is the target", 1,
          1);
    Check(!WidePick(15.0f, reach, wide, 15.0f, eps),
          "at the wide reach the far grab takes over", 0, 0);
    Check(!WidePick(9.0f, reach, wide, 6.0f, eps),
          "a nearer object that a press acts on keeps it", 0, 0);
    Check(WidePick(9.0f, reach, wide, 8.95f, eps),
          "a nearer object inside eps is a tie, the held object stays", 1, 1);
    Check(!WidePick(9.0f, reach, 0.0f, 9.0f, eps), "0 is the wide pick off", 0,
          0);
    Check(kWideCm >= kNearOutCm, "the wide reach reaches the far grab", 1, 1);
  }

  // NEAR OBJECTS WIN. The gaps are in units at 100 units a meter, so they
  // read as centimeters: off at 10, on again past 15.
  {
    const float in_gap = CmToUnits(static_cast<float>(kNearInCm), 100.0f);
    const float out_gap = CmToUnits(static_cast<float>(kNearOutCm), 100.0f);
    Near("the far grab goes off at 10 cm", in_gap, 10.0, 1e-4);
    Near("and comes back past 15 cm", out_gap, 15.0, 1e-4);
    Check(!NearBlocks(30.0f, false, in_gap, out_gap),
          "an object 30 cm away does not turn the far grab off", 0, 0);
    Check(!NearBlocks(12.0f, false, in_gap, out_gap),
          "an object 12 cm away does not turn it off by itself", 0, 0);
    Check(NearBlocks(10.0f, false, in_gap, out_gap),
          "an object 10 cm away turns it off", 1, 1);
    Check(NearBlocks(0.0f, false, in_gap, out_gap),
          "the point in an object turns it off", 1, 1);
    Check(NearBlocks(12.0f, true, in_gap, out_gap),
          "it stays off at 12 cm", 1, 1);
    Check(NearBlocks(15.0f, true, in_gap, out_gap),
          "it stays off at 15 cm", 1, 1);
    Check(!NearBlocks(15.1f, true, in_gap, out_gap),
          "it comes back at 15.1 cm", 0, 0);
    Check(!NearBlocks(1e30f, true, in_gap, out_gap),
          "with no object it comes back", 0, 0);
    // A hand that rests at the border: 20 frames between 11 and 14 cm.
    bool blocked = NearBlocks(9.0f, false, in_gap, out_gap);
    int changes = 0;
    for (int i = 0; i < 20; ++i) {
      const float gap = (i % 2) == 0 ? 11.0f : 14.0f;
      const bool next = NearBlocks(gap, blocked, in_gap, out_gap);
      if (next != blocked) ++changes;
      blocked = next;
    }
    Check(changes == 0 && blocked,
          "a hand that moves between 11 and 14 cm does not change it",
          changes, 0);
    blocked = false;
    changes = 0;
    for (int i = 0; i < 20; ++i) {
      const float gap = (i % 2) == 0 ? 11.0f : 14.0f;
      const bool next = NearBlocks(gap, blocked, in_gap, out_gap);
      if (next != blocked) ++changes;
      blocked = next;
    }
    Check(changes == 0 && !blocked,
          "and it does not turn it off when it was on", changes, 0);
    Check(NearBlocks(5.0f, false, 10.0f, 4.0f),
          "an out gap below the in gap counts as the in gap", 1, 1);
    Check(!NearBlocks(10.5f, true, 10.0f, 4.0f),
          "so there is no band then", 0, 0);
    const float nan = sqrtf(-1.0f);
    Check(!NearBlocks(nan, true, in_gap, out_gap),
          "a gap that is not a number does not turn it off", 0, 0);
  }

  // NO BAND WITH NO TARGET.
  {
    using controller_math::kFarMinHandM;
    Near("with the rule off a far target is 35 cm from the Hand or more",
         FarLeastMeters(false, 15.0f, kFarMinHandM), 0.35, 1e-6);
    Near("with the rule on it is 15 cm or more, where the far grab comes back",
         FarLeastMeters(true, 15.0f, kFarMinHandM), 0.15, 1e-6);
    Near("an out distance of 50 cm does not move it past the cone's 35 cm",
         FarLeastMeters(true, 50.0f, kFarMinHandM), 0.35, 1e-6);
    Near("an out distance of 0 gives no least distance",
         FarLeastMeters(true, 0.0f, kFarMinHandM), 0.0, 1e-6);
    // An object 20 cm from the Hand's point, on the aim ray: the cone itself
    // leaves it out, and with the least distance of the score it is in.
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const float ray[3] = {0.0f, 0.0f, 1.0f};
    const float at[3] = {0.0f, 0.0f, 40.0f};
    controller_math::FarScore s =
        controller_math::FarConeScore(origin, ray, at, 1.0f, 0.20f);
    Check(!s.valid, "the cone leaves out an object 20 cm from the Hand",
          s.valid, 0);
    s = controller_math::FarConeScore(origin, ray, at, 1.0f, kFarMinHandM);
    Check(s.valid && 0.20f >= FarLeastMeters(true, 15.0f, kFarMinHandM),
          "with the rule on it is a far target, scored at 35 cm", s.valid, 1);
    Near("its score is the distance term of 35 cm", s.score,
         log2(1.0 + 0.35), 1e-4);
  }

  // THE WAITS.
  {
    // The game's pick up of a forest: 225 ms with the button down, on the
    // clock of the game turns (3 turns of 100 ms), and one turn more for the
    // packet.
    Check(kForestFetchMs > 2 * 400,
          "a fetch from a forest waits more than twice the game's 400 ms",
          kForestFetchMs, 800);
    Check(kFetchMs > 400 && kFetchMs < kForestFetchMs,
          "a plain fetch waits less than a fetch from a forest", kFetchMs,
          kForestFetchMs);
    Check(kTouchMs < 200, "the press of a far touch is down for less than "
          "0.2 s", kTouchMs, 200);
    // The same press of a far touch at 60, 72 and 90 frames a second: it is
    // over at the first frame at or after kTouchMs.
    const double rates[3] = {60.0, 72.0, 90.0};
    for (int r = 0; r < 3; ++r) {
      const double frame_ms = 1000.0 / rates[r];
      unsigned long long frames = 0;
      while (!TouchOver(frames * frame_ms, frames) && frames < 1000) ++frames;
      const double ms = frames * frame_ms;
      char what[120];
      sprintf_s(what, "at %.0f frames a second the press of a far touch is "
                "down for 130 ms, to one frame", rates[r]);
      Check(ms >= kTouchMs && ms < kTouchMs + frame_ms, what, ms, kTouchMs);
    }
    Check(!TouchOver(500.0, 1) && TouchOver(500.0, 2),
          "one long frame is not enough: the press is down for two frames",
          1, 1);
  }
}

// ONE TARGET (docs/grab.md, "One target"): the bodies, the rule of the
// choice, the distance in the pick record and the halo.
inline void OneTargetChecks() {
  using namespace near_math;
  printf("\nOne target\n");

  // THE BALL OF A SCROLL.
  {
    const float center[3] = {50.0f, 30.0f, 70.0f};
    const float in[3] = {51.0f, 30.5f, 70.0f};
    Gap g = BallGap(in, center, 2.0f);
    Near("a point in the ball of a scroll has a gap of 0", g.gap, 0.0, 1e-6);
    Near("its middle is the distance to the middle of the ball", g.middle,
         sqrt(1.25), 1e-5);
    const float out[3] = {50.0f, 35.0f, 70.0f};
    g = BallGap(out, center, 2.0f);
    Near("a point 5 units over the middle is 3 units from a ball of 2", g.gap,
         3.0, 1e-5);
    g = BallGap(out, center, -1.0f);
    Near("a ball with no radius is its middle", g.gap, 5.0, 1e-5);
  }

  // THE SPHEROID. The nearest point of an ellipse, against a search along
  // the ellipse in 20000 steps.
  {
    const float a = 4.0f, b = 10.0f;
    const float points[6][2] = {{6.0f, 0.0f}, {0.0f, 12.0f}, {6.0f, 8.0f},
                                {9.0f, 3.0f}, {2.0f, 14.0f}, {4.5f, 9.5f}};
    double worst = 0.0;
    for (int i = 0; i < 6; ++i) {
      double best = 1e30;
      for (int k = 0; k <= 20000; ++k) {
        const double t = 1.5707963267948966 * k / 20000.0;
        const double dx = points[i][0] - a * cos(t);
        const double dy = points[i][1] - b * sin(t);
        const double d = sqrt(dx * dx + dy * dy);
        if (d < best) best = d;
      }
      const double got = EllipseGap(points[i][0], points[i][1], a, b);
      if (fabs(got - best) > worst) worst = fabs(got - best);
    }
    Check(worst < 0.02, "the gap to an ellipse of 4 by 10 agrees with a search "
                        "to 0.02 units at 6 points", worst, 0.0);
    Near("a point beside the ellipse at its middle: the gap is to the radius",
         EllipseGap(6.0f, 0.0f, a, b), 2.0, 1e-3);
    Near("a point over its top: the gap is to the top",
         EllipseGap(0.0f, 12.0f, a, b), 2.0, 1e-3);
    Near("a point in the ellipse has a gap of 0",
         EllipseGap(2.0f, 5.0f, a, b), 0.0, 1e-6);
    Near("the sign of the point does not count",
         EllipseGap(-6.0f, -8.0f, a, b), EllipseGap(6.0f, 8.0f, a, b), 1e-5);
    Near("an ellipse with no width is a line", EllipseGap(3.0f, 4.0f, 0.0f, b),
         3.0, 1e-5);
  }

  // THE BODY OF A TREE: radius 4, height 20, its base at 100 20 300.
  {
    const float base[3] = {100.0f, 20.0f, 300.0f};
    const float r = 4.0f, h = 20.0f;
    // A corner of the cylinder: beside the trunk, near the land. The cylinder
    // of a plain cylinder would hold it inside.
    const float corner[3] = {103.5f, 21.0f, 300.0f};
    Near("the cylinder of the build before holds a point 3.5 units from the "
         "trunk at the land", BodyGap(corner, base, r, h).gap, 0.0, 1e-6);
    Gap g = TreeGap(corner, base, r, h);
    Check(g.gap > 1.4f && g.gap < 1.5f,
          "the tree is 1.45 units from that point: the crown is narrow near "
          "the land", g.gap, 1.45);
    Near("the trunk is 1 unit thick, so it is 2.5 units from that point",
         BodyGap(corner, base, kTrunkShare * r, 0.5f * h).gap, 2.5, 1e-4);
    const float trunk[3] = {100.5f, 22.0f, 300.0f};
    Near("a point on the trunk is in the tree", TreeGap(trunk, base, r, h).gap,
         0.0, 1e-6);
    const float crown[3] = {102.0f, 32.0f, 300.0f};
    Near("a point in the crown is in the tree", TreeGap(crown, base, r, h).gap,
         0.0, 1e-6);
    const float top_edge[3] = {103.5f, 39.5f, 300.0f};
    Check(TreeGap(top_edge, base, r, h).gap > 1.5f,
          "the top edge of the cylinder is more than 1.5 units from the tree",
          TreeGap(top_edge, base, r, h).gap, 1.5);
    const float over[3] = {100.0f, 43.0f, 300.0f};
    Near("a point 3 units over the top is 3 units from the tree",
         TreeGap(over, base, r, h).gap, 3.0, 1e-3);
    Near("the middle of a tree is half its height over its base",
         TreeGap(base, base, r, h).middle, 10.0, 1e-5);
    // The volume of the body, by a count of points: about a third of the
    // cylinder.
    int in_tree = 0, in_cylinder = 0;
    for (int i = 0; i < 20; ++i) {
      for (int j = 0; j < 40; ++j) {
        for (int k = 0; k < 20; ++k) {
          const float p[3] = {base[0] - r + (i + 0.5f) * 0.4f,
                              base[1] + (j + 0.5f) * 0.5f,
                              base[2] - r + (k + 0.5f) * 0.4f};
          if (BodyGap(p, base, r, h).gap <= 0.0f) ++in_cylinder;
          if (TreeGap(p, base, r, h).gap <= 0.0f) ++in_tree;
        }
      }
    }
    const double share = static_cast<double>(in_tree) / in_cylinder;
    Check(share > 0.60 && share < 0.72,
          "the body of a tree is two thirds of the cylinder of the build "
          "before", share, 0.667);
  }

  // THE RULE OF THE CHOICE. 100 units a meter: the reach is 4 units, a touch
  // is 1 unit, and two gaps agree to 0.1 unit.
  {
    const float touch = 1.0f, eps = 0.1f;
    const Target tree = {{0.0f, 9.0f}, kHold, 10.0f, false, true};
    const Target scroll_away = {{3.0f, 5.5f}, kTap, 2.5f, true, false};
    Check(BeatsTarget(scroll_away, tree, touch, eps) &&
              !BeatsTarget(tree, scroll_away, touch, eps),
          "a scroll 3 cm from the point wins over a tree that the point is in",
          1, 1);
    const Target scroll_near = {{1.5f, 4.0f}, kTap, 2.5f, true, false};
    Check(BeatsTarget(scroll_near, scroll_away, touch, eps) &&
              !BeatsTarget(scroll_away, scroll_near, touch, eps),
          "of two scrolls the nearer one wins", 1, 1);
    const Target villager_in = {{0.0f, 0.8f}, kHold, 1.0f, false, false};
    Check(BeatsTarget(villager_in, tree, touch, eps) &&
              !BeatsTarget(tree, villager_in, touch, eps),
          "a villager that the point touches wins over the tree about him", 1,
          1);
    const Target villager_away = {{3.0f, 3.8f}, kHold, 1.0f, false, false};
    Check(BeatsTarget(tree, villager_away, touch, eps) &&
              !BeatsTarget(villager_away, tree, touch, eps),
          "a tree that the point touches wins over a villager 3 cm away", 1,
          1);
    const Target rock = {{2.0f, 2.6f}, kHold, 0.6f, false, false};
    const Target tree_away = {{3.0f, 12.0f}, kHold, 10.0f, false, true};
    Check(BeatsTarget(rock, tree_away, touch, eps) &&
              !BeatsTarget(tree_away, rock, touch, eps),
          "with no touch the nearer body wins", 1, 1);
    // Two trees with crowns that meet, the point in both: the nearer middle.
    const Target tree_a = {{0.0f, 4.0f}, kHold, 10.0f, false, true};
    const Target tree_b = {{0.0f, 7.0f}, kHold, 9.0f, false, true};
    Check(BeatsTarget(tree_a, tree_b, touch, eps) &&
              !BeatsTarget(tree_b, tree_a, touch, eps),
          "of two trees of one size step the nearer middle wins", 1, 1);
    // The house and the rock at its wall, the point touches both.
    const Target house = {{0.5f, 6.0f}, kTap, 7.0f, false, true};
    const Target rock_in = {{0.2f, 6.5f}, kHold, 0.6f, false, false};
    Check(BeatsTarget(rock_in, house, touch, eps) &&
              !BeatsTarget(house, rock_in, touch, eps),
          "a rock at the wall of a house wins over the house", 1, 1);
    // The same body in each respect: the kind decides, hold over tap.
    const Target hold = {{0.0f, 2.0f}, kHold, 3.0f, false, false};
    const Target tap = {{0.0f, 2.0f}, kTap, 3.0f, false, false};
    Check(BeatsTarget(hold, tap, touch, eps) &&
              !BeatsTarget(tap, hold, touch, eps),
          "at the same place and size the order is hold, take, tap", 1, 1);
    Check(SizeClass(1.0f) == 0 && SizeClass(1.9f) == 0 && SizeClass(2.0f) == 1 &&
              SizeClass(10.0f) == 3 && SizeClass(0.6f) == -1,
          "the size counts in steps of a factor of 2", SizeClass(10.0f), 3);
    // A SIGNPOST COMES LAST (hand_sign_last, rule 6). A signpost 3 cm from
    // the point, and a miracle, the Creature and a tree in the reach.
    {
      Check(SignOut(true, true, 3.0f, touch) && !SignOut(true, true, 0.5f, touch),
            "a signpost comes last while the point is not in it", 1, 1);
      Check(!SignOut(false, true, 3.0f, touch) &&
                !SignOut(true, false, 3.0f, touch),
            "hand_sign_last=0 and a scroll of a quest keep the rule of a "
            "scroll", 0, 0);
      Target sign = {{3.0f, 6.0f}, kTap, 3.0f, true, false};
      const Target miracle = {{3.5f, 5.5f}, kHold, 2.3f, false, false};
      const Target creature = {{3.8f, 5.0f}, kCreature, 1.8f, false, false};
      Check(TargetRule(sign, touch) == 2 &&
                BeatsTarget(sign, miracle, touch, eps) &&
                BeatsTarget(sign, creature, touch, eps),
            "as a scroll, a signpost 3 cm away wins over a miracle and over "
            "the Creature in the reach", TargetRule(sign, touch), 2);
      sign.last = SignOut(true, true, sign.gap.gap, touch);
      Check(TargetRule(sign, touch) == 6, "the rule of such a signpost is 6",
            TargetRule(sign, touch), 6);
      Check(BeatsTarget(miracle, sign, touch, eps) &&
                !BeatsTarget(sign, miracle, touch, eps),
            "with the rule, a miracle 3.5 cm away wins over the signpost", 1,
            1);
      Check(BeatsTarget(creature, sign, touch, eps) &&
                !BeatsTarget(sign, creature, touch, eps),
            "and the Creature 3.8 cm away wins over it", 1, 1);
      Check(BeatsTarget(tree, sign, touch, eps) &&
                BeatsTarget(scroll_away, sign, touch, eps),
            "a tree that the point is in and a scroll of a quest win over it "
            "too", 1, 1);
      // A scroll of a quest in its longer reach (rule 5) comes before it.
      Target far_scroll = {{8.0f, 10.0f}, kTap, 2.5f, true, false};
      far_scroll.outer = true;
      Check(BeatsTarget(far_scroll, sign, touch, eps),
            "a scroll of a quest 8 cm away comes before a signpost 3 cm away",
            1, 1);
      // A LOOSE BODY THAT THE POINT IS NOT IN (far_grab_beside_loose). A
      // tree 7 cm from the point and a house 3 cm from it do not turn the
      // grab from a distance off, and they give way to a far target. A tree
      // that the point is in, a rock 7 cm away and the Creature stay near
      // objects.
      {
        const float in_gap = 10.0f, out_gap = 15.0f;
        const Target tree_near = {{7.0f, 20.0f}, kTap, 12.0f, false, true};
        const Target house_near = {{3.0f, 9.0f}, kTap, 7.0f, false, true};
        const Target tree_in = {{0.5f, 15.0f}, kTap, 12.0f, false, true};
        const Target rock_near = {{7.0f, 8.0f}, kHold, 0.6f, false, false};
        const bool tree_out = LooseOut(true, tree_near.loose,
                                       tree_near.gap.gap, touch);
        const bool house_out = LooseOut(true, house_near.loose,
                                        house_near.gap.gap, touch);
        Check(tree_out && house_out &&
                  !StopsFar(false, tree_out) && !StopsFar(false, house_out),
              "a tree 7 cm away and a house 3 cm away do not turn the far "
              "grab off", tree_out ? 1 : 0, 1);
        Check(SignGivesWay(house_out, true) && !SignGivesWay(house_out, false),
              "the house 3 cm away is the target at the Hand only while the "
              "aim has no far target", 1, 1);
        Check(TargetRule(house_near, touch) == 4,
              "its rule in the choice stays 4", TargetRule(house_near, touch),
              4);
        const bool tree_in_out =
            LooseOut(true, tree_in.loose, tree_in.gap.gap, touch);
        Check(!tree_in_out && StopsFar(false, tree_in_out) &&
                  NearBlocks(tree_in.gap.gap, false, in_gap, out_gap) &&
                  !SignGivesWay(tree_in_out, true),
              "a tree that the point is in turns the far grab off, and it "
              "stays the target", tree_in_out ? 1 : 0, 0);
        const bool rock_out =
            LooseOut(true, rock_near.loose, rock_near.gap.gap, touch);
        Check(!rock_out && StopsFar(false, rock_out) &&
                  NearBlocks(rock_near.gap.gap, false, in_gap, out_gap),
              "a rock 7 cm away turns the far grab off", rock_out ? 1 : 0, 0);
        Check(!LooseOut(true, false, 7.0f, touch) &&
                  !LooseOut(true, scroll_away.loose, scroll_away.gap.gap,
                            touch),
              "the Creature and a scroll of a quest are no loose bodies", 0,
              0);
        Check(!LooseOut(false, true, 7.0f, touch) && StopsFar(false, false),
              "far_grab_beside_loose=0: a tree 7 cm away turns the far grab "
              "off, as before", 0, 0);
        Check(!StopsFar(true, false),
              "a signpost that the point is not in does not turn it off", 0,
              0);
      }
      // The point in the signpost: a body that the point touches (rule 1).
      Target sign_in = {{0.5f, 3.0f}, kTap, 3.0f, true, false};
      sign_in.last = SignOut(true, true, sign_in.gap.gap, touch);
      Check(TargetRule(sign_in, touch) == 1 &&
                BeatsTarget(sign_in, tree, touch, eps) &&
                BeatsTarget(sign_in, miracle, touch, eps),
            "a signpost that the point is in wins over a tree that the point "
            "is in and over a miracle 3.5 cm away", TargetRule(sign_in, touch),
            1);
      // The wide pick: 9 cm from a signpost and 6 cm from a miracle. The
      // nearest object that counts is the miracle, at each gap of the
      // signpost.
      const float reach = 4.0f, wide = 15.0f;
      Check(WidePick(6.0f, reach, wide, 6.0f, eps),
            "9 cm from a signpost and 6 cm from a miracle: the miracle is "
            "the wide target, because the signpost does not count as the "
            "nearest object", 1, 1);
      Check(!WidePick(6.0f, reach, wide, 2.0f, eps),
            "as a near object, a signpost 2 cm away stops that wide pick", 0,
            0);
      // The grab from a distance: only an object that counts turns it off.
      Check(!NearBlocks(1e30f, false, 10.0f, 15.0f),
            "with a signpost of rule 6 as the only near object, the far grab "
            "stays on", 0, 0);
      Check(NearBlocks(0.5f, false, 10.0f, 15.0f),
            "a signpost that the point is in turns it off", 1, 1);
      Check(SignGivesWay(true, true) && !SignGivesWay(true, false) &&
                !SignGivesWay(false, true),
            "a signpost of rule 6 is no target while the aim has a target, "
            "and it is the target when the aim has none", 1, 1);
      // Each order of the candidates gives the same target.
      const Target with_sign[5] = {sign, miracle, creature, tree, far_scroll};
      int order[5] = {0, 1, 2, 3, 4};
      int first = -1;
      bool same = true;
      do {
        int best = order[0];
        for (int i = 1; i < 5; ++i) {
          if (BeatsTarget(with_sign[order[i]], with_sign[best], touch, eps)) {
            best = order[i];
          }
        }
        if (first < 0) first = best;
        if (best != first) same = false;
      } while (std::next_permutation(order, order + 5));
      Check(same && first == 3,
            "each order of a signpost, a miracle, the Creature, a tree that "
            "the point is in and a far scroll gives the tree", first, 3);
    }
    // The order is the same for each order of the candidates: with the best
    // of each of the 720 orders of 6 bodies.
    const Target set[6] = {tree, scroll_away, villager_in, villager_away,
                           tree_a, house};
    int order[6] = {0, 1, 2, 3, 4, 5};
    int winners = 0, first = -1;
    bool same = true;
    do {
      int best = order[0];
      for (int i = 1; i < 6; ++i) {
        if (BeatsTarget(set[order[i]], set[best], touch, eps)) best = order[i];
      }
      if (first < 0) first = best;
      if (best != first) same = false;
      ++winners;
    } while (std::next_permutation(order, order + 6));
    Check(same && winners == 720 && first == 2,
          "each of the 720 orders of 6 bodies gives the villager that the "
          "point touches", first, 2);
    const Target none_scroll[5] = {tree, villager_in, villager_away, tree_a,
                                   house};
    int order5[5] = {0, 1, 2, 3, 4};
    first = -1;
    same = true;
    do {
      int best = order5[0];
      for (int i = 1; i < 5; ++i) {
        if (BeatsTarget(none_scroll[order5[i]], none_scroll[best], touch,
                        eps)) {
          best = order5[i];
        }
      }
      if (first < 0) first = best;
      if (best != first) same = false;
    } while (std::next_permutation(order5, order5 + 5));
    Check(same && first == 1,
          "and with no scroll each order gives the villager that the point "
          "touches", first, 1);
  }

  // THE DISTANCE OF THE CHOICE IN THE PICK RECORD. The tree at
  // 1859.63 32.78 2621.32, the camera at 1897.31 85.03 2615.55, and the land
  // under the pointer at 53.04 units.
  {
    const double dx = 1897.31 - 1859.63, dy = 85.03 - 32.78,
                 dz = 2615.55 - 2621.32;
    const float tree = static_cast<float>(sqrt(dx * dx + dy * dy + dz * dz));
    Check(tree > 53.04f,
          "the tree of the log was farther from the camera than the land "
          "under the pointer, so the game took it out of the record", tree,
          53.04);
    Near("its distance in the record is half the land distance now",
         RecordDistance(tree, 53.04f), 26.52, 1e-4);
    Check(RecordDistance(tree, 53.04f) < 53.04f,
          "and that is less than the land distance, so the game keeps it", 1,
          1);
    Near("an object nearer than the land keeps its distance",
         RecordDistance(40.0f, 53.04f), 40.0, 1e-6);
    Near("with no land under the pointer the distance stays",
         RecordDistance(40.0f, 3.4e38f), 40.0, 1e-6);
    Near("an object as far as the land gets the cut",
         RecordDistance(53.04f, 53.04f), 26.52, 1e-4);
  }

  // THE HALO OF THE TARGET AT THE HAND.
  {
    HaloShape s = HaloEllipse(0.0f, 0.0f, 30.0f, 1.35f);
    Check(s.across == 30.0f && s.along == 30.0f && s.tilt_y == 1.0f,
          "from above the halo of a tree is round", s.along, 30.0);
    s = HaloEllipse(0.0f, -100.0f, 30.0f, 1.35f);
    Near("from the side it is as long as the body and a share more", s.along,
         sqrt(135.0 * 135.0 + 30.0 * 30.0), 1e-3);
    Near("and as wide as the halo of a ball", s.across, 30.0, 1e-6);
    Check(s.tilt_x == 0.0f && s.tilt_y == -1.0f,
          "its long axis is the image of the axis of the body", s.tilt_y,
          -1.0);
    s = HaloEllipse(60.0f, -80.0f, 30.0f, 1.35f);
    Near("with a head that leans the axis leans with the tree", s.tilt_x, 0.6,
         1e-6);
    Near("and the long axis is a unit", s.tilt_x * s.tilt_x +
                                             s.tilt_y * s.tilt_y, 1.0, 1e-6);
  }
}

// THE OPEN HAND ON THE CREATURE (docs/creature.md, "The open hand"): the hand of a stroke,
// the tickle and the speed of a slap.
inline void StrokeChecks() {
  using namespace near_math;
  printf("\nThe open hand on the Creature\n");

  // THE HAND OF A STROKE. A hand of 19 cm. At 100 units a meter a Creature of
  // size 0.22 has a body of 3.6 cm, and at 7.5 units a meter one of 48 cm.
  {
    float lo = 0.0f, hi = 0.0f;
    StrokeLimits(kStrokeFingerBelowPct, kStrokeFlatAbovePct, &lo, &hi);
    Near("the finger limit is 0.6 of the hand's length", lo, 0.6, 1e-6);
    Near("and the flat limit is 0.9", hi, 0.9, 1e-6);
    Check(StrokeFinger(3.63f / 19.0f, false, lo, hi),
          "a Creature of 3.6 cm against a hand of 19 cm: the index finger", 1,
          1);
    Check(!StrokeFinger(48.4f / 19.0f, true, lo, hi),
          "a Creature of 48 cm against the same hand: the flat hand", 0, 0);
    Check(StrokeFinger(0.75f, true, lo, hi) &&
              !StrokeFinger(0.75f, false, lo, hi),
          "a Creature of three quarters of the hand keeps the hand that it "
          "had", 1, 1);
    bool finger = true;
    int changes = 0;
    for (int i = 0; i < 40; ++i) {
      const float ratio = (i % 2) == 0 ? 0.65f : 0.85f;
      const bool next = StrokeFinger(ratio, finger, lo, hi);
      if (next != finger) ++changes;
      finger = next;
    }
    Check(changes == 0,
          "a zoom that goes between 0.65 and 0.85 for 40 frames changes "
          "nothing", changes, 0);
    Check(!StrokeFinger(0.91f, true, lo, hi) &&
              StrokeFinger(0.59f, false, lo, hi),
          "past each limit the hand changes", 1, 1);
    const float nan = sqrtf(-1.0f);
    Check(StrokeFinger(nan, true, lo, hi) && !StrokeFinger(nan, false, lo, hi),
          "a ratio that is not a number keeps the hand", 1, 1);

    // THE START OF A STROKE: a Creature of size 0.22 (a body of 3.63 units)
    // and a hand of 19 cm.
    Check(!StrokeFingerAtStart(3.63f / 13.2f / 0.19f, lo, hi),
          "at 13.2 units a meter the body is 1.45 of the hand: the flat hand",
          0, 0);
    Check(!StrokeFingerAtStart(3.63f / 19.8f / 0.19f, lo, hi),
          "at 19.8 units a meter it is 0.97 of the hand, as large as the "
          "hand: the flat hand now", 0, 0);
    Check(StrokeFingerAtStart(3.63f / 29.6f / 0.19f, lo, hi),
          "at 29.6 units a meter it is 0.65 of the hand: the finger", 1, 1);
    float lo_before = 0.0f, hi_before = 0.0f;
    StrokeLimits(kStrokeFingerBelowPctBefore, kStrokeFlatAbovePctBefore,
                 &lo_before, &hi_before);
    Check(StrokeFingerAtStart(3.63f / 19.8f / 0.19f, lo_before, hi_before),
          "with the limits of the builds before (0.8 and 1.2) a Creature as "
          "large as the hand got the finger", 1, 1);
    Near("the start changes at 0.75 of the hand, and it was 1.0",
         0.5f * (lo + hi), 0.75, 1e-6);
    Check(StrokeFingerAtStart(0.74f, lo, hi) &&
              !StrokeFingerAtStart(0.76f, lo, hi),
          "a stroke starts with the finger below 0.75 and with the flat hand "
          "above", 1, 1);
    StrokeLimits(200, 50, &lo, &hi);
    Check(hi >= lo, "a flat limit below the finger limit is made equal to "
          "it", hi, lo);
    StrokeLimits(0, 9000, &lo, &hi);
    Near("a limit that is not sane is held at 10 percent", lo, 0.1, 1e-6);
    Near("or at 500 percent", hi, 5.0, 1e-6);
  }

  // THE DISTANCE FROM THE PLAYER, for a script that measures from the camera.
  {
    // The guide of Land 1 walks, and his script asks if the camera is less
    // than 10 or 25 units from him. The game's camera of the God view is 400
    // units from the player.
    const float guide[3] = {1000.0f, 30.0f, 2000.0f};
    const float camera[3] = {1000.0f - 257.0f, 30.0f + 306.0f, 2000.0f};
    Near("the game measures on the map: the height does not count",
         MapDistance(camera, guide), 257.0, 1e-3);
    PlayerPoints p = {};
    float d = -1.0f;
    Check(PlayerDistance(p, guide, &d) == -1 && d == -1.0f,
          "with no point of the player the distance is not changed", 0, 0);
    p.head = true;
    p.head_at[0] = 1012.0f;
    p.head_at[1] = 48.0f;  // 60 cm over the land at 29.6 units a meter
    p.head_at[2] = 2016.0f;
    Check(PlayerDistance(p, guide, &d) == 0, "the head alone is the player",
          0, 0);
    Near("the head 20 units away on the map is 20 units, at each height", d,
         20.0, 1e-4);
    Check(AnswerChanges(MapDistance(camera, guide), d, 25.0f) &&
              !AnswerChanges(MapDistance(camera, guide), d, 10.0f),
          "the test of 25 units is true now, and the test of 10 is not", 1, 1);
    p.hand[0] = true;
    p.hand_at[0][0] = 1003.0f;
    p.hand_at[0][1] = 31.0f;
    p.hand_at[0][2] = 2004.0f;
    Check(PlayerDistance(p, guide, &d) == 1,
          "a hand at the guide is nearer than the head", 1, 1);
    Near("the hand is 5 units from him", d, 5.0, 1e-4);
    Check(AnswerChanges(MapDistance(camera, guide), d, 10.0f),
          "so the test of 10 units is true: the player came close", 1, 1);
    // The other hand rests in the lap, and it is not the nearest.
    p.hand[2] = true;
    p.hand_at[2][0] = 1015.0f;
    p.hand_at[2][1] = 40.0f;
    p.hand_at[2][2] = 2020.0f;
    Check(PlayerDistance(p, guide, &d) == 1, "the nearest point wins", 1, 1);
    // A test that asks if the player went away: each point must be away.
    const float far_thing[3] = {1300.0f, 30.0f, 2000.0f};
    PlayerDistance(p, far_thing, &d);
    Check(d > 250.0f, "a thing 300 units away is away from the head and "
          "from each hand", d, 285.0);
    Check(!AnswerChanges(MapDistance(camera, far_thing), d, 250.0f),
          "and the answer of that test does not change", 0, 0);
    volatile float zero = 0.0f;
    p.hand_at[0][0] = zero / zero;
    Check(PlayerDistance(p, guide, &d) == 0 && d == d,
          "a hand with a place that is not a number does not count", 0, 0);
    // The tests that ask if the player went away, with NOT. The player is
    // away only when the head and each hand are away.
    PlayerPoints q = {};
    q.head = true;
    q.head_at[0] = 1120.0f;
    q.head_at[2] = 2000.0f;
    PlayerDistance(q, guide, &d);
    Check(!(d < 100.0f), "the head 120 units away: NOT less than 100 is true, "
          "the player went away", d, 120.0);
    q.hand[1] = true;
    q.hand_at[1][0] = 1060.0f;
    q.hand_at[1][2] = 2000.0f;
    PlayerDistance(q, guide, &d);
    Check(d < 100.0f, "a hand 60 units away: the player did not go away", d,
          60.0);
    // The limit of 1 unit of the hermit: the game gives 0 below 0.5 units,
    // and no point of the player is that near in play.
    PlayerDistance(q, guide, &d);
    Check(!(d < 1.0f), "NOT less than 1 is true, as with the game's camera",
          d, 60.0);
    // The largest limit of the scripts, 250 units: the game's camera of the
    // God view is 257 units away, and the head over the thing is not.
    q.hand[1] = false;
    q.head_at[0] = 1000.0f;
    PlayerDistance(q, guide, &d);
    Check(AnswerChanges(MapDistance(camera, guide), d, 250.0f),
          "the test of 250 units is true with the head over the thing", 1, 1);
  }

  // WHEN THE PLAYER STANDS FOR THE CAMERA OF A SCRIPT.
  {
    Check(PlayerIsCamera(true, true, 0, false, false),
          "the God view has the view and the player has the camera: the "
          "player", 1, 1);
    Check(!PlayerIsCamera(false, true, 0, false, false),
          "the God view did not own the last frame (the menu, the intro, the "
          "first frames): the game's camera", 0, 0);
    Check(!PlayerIsCamera(true, false, 0, false, false),
          "the God view does not own the view now (a load): the game's "
          "camera", 0, 0);
    Check(!PlayerIsCamera(true, true, 1, false, false),
          "the first game turn of a story, before a frame shows it: the "
          "story's camera", 0, 0);
    Check(!PlayerIsCamera(true, true, -1, false, false),
          "no camera mode can be read: the game's camera", 0, 0);
    Check(!PlayerIsCamera(true, true, 0, true, false),
          "the cinema bars are up: the story's camera", 0, 0);
    Check(!PlayerIsCamera(true, true, 0, false, true),
          "the game moves its own camera and the view is not there yet: the "
          "game's camera", 0, 0);
  }

  // THE VIEW OF THE HEAD, for a script that asks if the camera sees a point.
  {
    // The head looks along +z, level. The columns are right, up, forward.
    const float level[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float head[3] = {1000.0f, 90.0f, 2000.0f};
    const float k = kHeadViewInvTan;  // 100 degrees
    const float ahead[3] = {1000.0f, 90.0f, 2050.0f};
    Check(HeadSees(head, level, ahead, k, k), "a point ahead is in the view",
          1, 1);
    const float behind[3] = {1000.0f, 90.0f, 1950.0f};
    Check(!HeadSees(head, level, behind, k, k),
          "a point behind the head is not", 0, 0);
    // 49 and 51 degrees to the right, at 50 units.
    const float in[3] = {1000.0f + 50.0f * 0.75471f, 90.0f,
                         2000.0f + 50.0f * 0.65606f};
    const float out[3] = {1000.0f + 50.0f * 0.77715f, 90.0f,
                          2000.0f + 50.0f * 0.62932f};
    Check(HeadSees(head, level, in, k, k) && !HeadSees(head, level, out, k, k),
          "with a view of 100 degrees a point 49 degrees to the side is in "
          "it, and a point 51 degrees to the side is not", 1, 1);
    // The land 60 cm below the head at 100 units a meter, 50 cm ahead: 50
    // degrees down, at the edge. 60 cm ahead is in the view.
    const float land[3] = {1000.0f, 30.0f, 2060.0f};
    Check(HeadSees(head, level, land, k, k),
          "the land 60 cm ahead and 60 cm below a level head is in the view",
          1, 1);
    const float feet[3] = {1000.0f, 30.0f, 2010.0f};
    Check(!HeadSees(head, level, feet, k, k),
          "the land at the feet is not, with a level head", 0, 0);
    // The head turns 90 degrees to the right: forward is +x, right is -z.
    const float turned[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    const float right[3] = {1050.0f, 90.0f, 2000.0f};
    Check(HeadSees(head, turned, right, k, k) &&
              !HeadSees(head, turned, ahead, k, k),
          "the view follows the head: after a turn of 90 degrees the point "
          "at the right is in it, and the point of before is not", 1, 1);
    Check(!HeadSees(head, level, head, k, k), "the place of the head is not "
          "in the view", 0, 0);
    volatile float zero = 0.0f;
    const float bad[3] = {zero / zero, 90.0f, 2050.0f};
    Check(!HeadSees(head, level, bad, k, k),
          "a place that is not a number is not in the view", 0, 0);
    // The view angle of the game: 110 degrees in a desktop run.
    Near("a view angle of the game that is sane is used",
         HeadViewInvTan(0.70021f), 0.70021, 1e-6);
    Near("a value that is not sane gives 100 degrees", HeadViewInvTan(0.0f),
         kHeadViewInvTan, 1e-6);
    Check(HeadSees(head, level, out, 0.70021f, 0.70021f),
          "with 110 degrees the point 51 degrees to the side is in the view",
          1, 1);
  }

  // THE POINT OF THE FLAT HAND: the palm.
  {
    const float tip[3] = {10.0f, 5.0f, 20.0f};
    // A palm offset: 12.1 cm from the tip at 13.2 units a meter.
    const float offset[3] = {-1.2f, -0.4f, 0.9f};
    float out[3];
    Check(PalmPoint(tip, offset, 1, true, true, out),
          "with the flat hand the point is the palm", 1, 1);
    Near("the palm is the tip and the offset, x", out[0], 8.8, 1e-5);
    Near("y", out[1], 4.6, 1e-5);
    Near("z", out[2], 20.9, 1e-5);
    Check(!PalmPoint(tip, offset, 1, false, true, out) && out[0] == tip[0] &&
              out[1] == tip[1] && out[2] == tip[2],
          "with the index finger the point is the tip", 0, 0);
    Check(!PalmPoint(tip, offset, 1, true, false, out) && out[0] == tip[0],
          "with creature_palm_point=0 the point is the tip, as in the builds "
          "before", 0, 0);
    Check(PalmPoint(tip, offset, kPalmOffsetFrames, true, true, out) &&
              !PalmPoint(tip, offset, kPalmOffsetFrames + 1, true, true, out) &&
              out[2] == tip[2],
          "an offset of more than 8 frames ago is not used", 0, 0);
    Check(!PalmPoint(tip, offset, -1, true, true, out) && out[1] == tip[1],
          "with no offset yet the point is the tip", 0, 0);
    volatile float zero = 0.0f;
    const float bad[3] = {0.0f, zero / zero, 0.0f};
    Check(!PalmPoint(tip, bad, 1, true, true, out) && out[1] == tip[1],
          "an offset that is not a number is not used", 0, 0);
    // A hand that moves 1 unit a frame: the point moves with the tip of this
    // frame, with no lag.
    const float tip2[3] = {11.0f, 5.0f, 20.0f};
    PalmPoint(tip2, offset, 1, true, true, out);
    Near("the palm follows the tip of this frame", out[0], 9.8, 1e-5);
  }

  // THE PALM DOES NOT FOLLOW THE POINT. The palm of the mod's hand is the
  // point that the hand stands on and the offset c. The numbers are
  // 11.4 cm of the room at 19.8 units a meter, 2.2569 units.
  {
    const float c[3] = {-0.38f, 2.2161f, -0.19f};
    const float length = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    Near("the offset of run hs1 is 2.2569 units", length, 2.2569, 1e-3);
    const float tip[3] = {1894.68f, 29.81f, 2520.36f};
    // Now: the mod's hand stands on the tip, and the offset is from the tip.
    float stood[3] = {tip[0], tip[1], tip[2]};
    float point[3] = {tip[0], tip[1], tip[2]};
    for (int frame = 0; frame < 30; ++frame) {
      const float palm[3] = {stood[0] + c[0], stood[1] + c[1],
                             stood[2] + c[2]};
      float offset[3];
      PalmOffset(palm, stood, offset);
      PalmPoint(tip, offset, 1, true, true, point);
      for (int i = 0; i < 3; ++i) stood[i] = tip[i];
    }
    float d[3] = {point[0] - tip[0], point[1] - tip[1], point[2] - tip[2]};
    Near("after 30 frames with a still hand the point is one offset from "
         "the tip", sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), length,
         1e-3);
    // A model in which the mod's hand stands on the Hand's point, and the
    // offset is from the tip of the controller.
    for (int i = 0; i < 3; ++i) stood[i] = point[i] = tip[i];
    for (int frame = 0; frame < 27; ++frame) {
      const float palm[3] = {stood[0] + c[0], stood[1] + c[1],
                             stood[2] + c[2]};
      float offset[3];
      PalmOffset(palm, tip, offset);
      PalmPoint(tip, offset, 1, true, true, point);
      for (int i = 0; i < 3; ++i) stood[i] = point[i];
    }
    for (int i = 0; i < 3; ++i) d[i] = point[i] - tip[i];
    Near("that point ran away one offset a frame: 60.9 units after 27 "
         "frames (run hs1: 60.45 units outside the body)",
         sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 27.0 * length, 0.05);
  }

  // THE TICKLE.
  {
    float phase = 1.0f;
    for (int i = 0; i < 100; ++i) phase = TicklePhase(phase, 0.0f);
    Near("a hand that is still for 100 frames has the same wave", phase, 1.0,
         1e-6);
    Near("and so each finger has the same curl", TickleCurl(phase, 2),
         TickleCurl(1.0f, 2), 1e-6);
    Near("5 cm of path is one wave", TicklePhase(0.5f, kTickleCm), 0.5, 1e-4);
    Near("2.5 cm of path is half a wave", TicklePhase(0.0f, 2.5f),
         3.14159265, 1e-4);
    float least = 1e30f, most = -1e30f, worst_step = 0.0f;
    phase = 0.0f;
    float last[5] = {0, 0, 0, 0, 0};
    for (int f = 0; f < 5; ++f) last[f] = TickleCurl(phase, f);
    // A stroke at 0.3 m/s and 90 frames a second: 0.33 cm a frame.
    for (int i = 0; i < 400; ++i) {
      phase = TicklePhase(phase, 0.3333f);
      for (int f = 0; f < 5; ++f) {
        const float c = TickleCurl(phase, f);
        if (c < least) least = c;
        if (c > most) most = c;
        if (fabsf(c - last[f]) > worst_step) worst_step = fabsf(c - last[f]);
        last[f] = c;
      }
    }
    Check(least >= 0.0f && most <= kTickleCurl + 1e-6f,
          "the wave only closes a finger, by 0.12 of its curl at most", most,
          kTickleCurl);
    Check(worst_step < 0.03f,
          "a stroke at 0.3 m/s moves no finger more than 0.03 of its curl in "
          "a frame", worst_step, 0.03);
    Check(fabsf(TickleCurl(1.0f, 1) - TickleCurl(1.0f, 2)) > 0.01f,
          "two fingers are at two places of the wave", 1, 1);
    Near("a jump of the tracking of 2 m counts as 10 cm of path",
         TicklePhase(0.0f, 200.0f), TicklePhase(0.0f, 10.0f), 1e-5);
  }

  // THE SPEED OF A SLAP.
  {
    // A slap with the wrist: the hand turns 300 degrees a second about the
    // wrist. The grip is 7 cm from the wrist and the index tip is 17 cm.
    const float turn = 300.0f * 3.14159265f / 180.0f;
    const float grip = turn * 0.07f, tip = turn * 0.17f;
    Check(grip < 1.0f && tip < 1.0f && SlapSpeed(grip, tip) == tip,
          "a turn of the wrist of 300 degrees a second: the tip is the "
          "faster part", SlapSpeed(grip, tip), tip);
    const float fast = 420.0f * 3.14159265f / 180.0f;
    Check(fast * 0.07f < 1.0f && SlapSpeed(fast * 0.07f, fast * 0.17f) > 1.0f,
          "a backhand of 420 degrees a second: the grip moves at 0.5 m/s, "
          "which was no slap, and the tip at 1.25 m/s, which is one",
          SlapSpeed(fast * 0.07f, fast * 0.17f), 1.25);
    Near("a slap with the arm: the grip is as fast as the tip",
         SlapSpeed(1.4f, 1.3f), 1.4, 1e-6);
    Near("no motion gives no speed", SlapSpeed(-1.0f, sqrtf(-1.0f)), 0.0,
         1e-6);
    // The game reads k times the speed. At the new speed of 1 m/s a slap
    // needs 5 S units a second, as in the game.
    const float k = controller_math::SlapUnitsPerMps(0.22f, 1.0f);
    Near("at creature_slap_mps=1 the game reads 16.5 units a second for 1 m/s",
         k, 16.5, 1e-4);
    Check(k * 0.9f < 5.0f * 15.0f * 0.22f && k * 1.1f > 5.0f * 15.0f * 0.22f,
          "0.9 m/s is a stroke and 1.1 m/s is a slap", k, 16.5);
    Check(k * 1.9f > 9.0f * 15.0f * 0.22f && k * 1.7f < 9.0f * 15.0f * 0.22f,
          "a hard slap needs 1.8 m/s", k * 1.8f, 29.7);
  }
}

}  // namespace near_checks
