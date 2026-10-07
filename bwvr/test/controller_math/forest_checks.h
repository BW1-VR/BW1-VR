// MANY TREES FROM A FOREST (docs/hand.md, "Many trees from a forest").
// main.cpp includes this file after Check, Near, GripRun, Facts, GripStart and
// GripFrames, just before main.
#pragma once

#include "../../src/mod/forest_take_math.h"

namespace forest_checks {

// One step of the rule a frame, 11 ms apart, with the same facts. Returns the
// acts of all the steps as bits: 1 for a start, 2 for an add.
struct TakeRun {
  forest_take::State state;
  double now;
  forest_take::Out out;
  int adds;
};

inline void TakeStart(TakeRun* run) {
  forest_take::Reset(&run->state);
  run->now = 1000.0;
  run->out = forest_take::Out{forest_take::kActNone, false, false};
  run->adds = 0;
}

inline int TakeFrames(TakeRun* run, int frames, bool grip, int held, bool next,
                      bool on = true) {
  int acts = 0;
  for (int i = 0; i < frames; ++i) {
    run->now += 11.0;
    forest_take::Facts f;
    f.on = on;
    f.grip = grip;
    f.held = held;
    f.next = next;
    f.now_ms = run->now;
    run->out = forest_take::Step(&run->state, f, 600, 250, 300);
    if (run->out.act == forest_take::kActStart) acts |= 1;
    if (run->out.act == forest_take::kActAdd) {
      acts |= 2;
      ++run->adds;
    }
    // The caller of the rule holds the bundle after a start.
    if (run->out.act == forest_take::kActStart) held = forest_take::kHeldBundle;
  }
  return acts;
}

inline void ForestChecks() {
  using namespace forest_take;
  printf("\nMany trees from a forest\n");
  TakeRun run;

  // THE RULE: hold the grip in a forest to collect.
  TakeStart(&run);
  TakeFrames(&run, 5, false, kHeldNothing, false);
  Check(run.state.phase == kIdle, "an empty Hand does nothing", run.state.phase,
        kIdle);
  TakeFrames(&run, 1, true, kHeldTree, true);
  Check(run.state.phase == kArmed && run.state.trees == 1,
        "a tree in the closed grip starts the wait", run.state.phase, kArmed);
  int acts = TakeFrames(&run, 50, true, kHeldTree, true);
  Check(acts == 0 && !run.out.runs,
        "no take in the first 550 ms: the tree is one tree", acts, 0);
  acts = TakeFrames(&run, 6, true, kHeldTree, true);
  Check(acts == 1 && run.state.phase == kTaking && run.state.trees == 2 &&
            run.out.runs,
        "after 600 ms with a second tree in reach the take starts with two "
        "trees", run.state.trees, 2);
  // 250 ms a tree: 23 frames of 11 ms. One of them is behind already.
  acts = TakeFrames(&run, 21, true, kHeldBundle, true);
  Check(acts == 0, "the next tree does not come before 250 ms", acts, 0);
  acts = TakeFrames(&run, 1, true, kHeldBundle, true);
  Check(acts == 2 && run.state.trees == 3, "the third tree comes at 250 ms",
        run.state.trees, 3);
  run.adds = 0;
  TakeFrames(&run, 230, true, kHeldBundle, true);
  Check(run.adds == 10 && run.state.trees == 13,
        "ten more trees in the next 2.5 s", run.adds, 10);
  // No tree in reach: the take waits, and it still runs.
  acts = TakeFrames(&run, 300, true, kHeldBundle, false);
  Check(acts == 0 && run.out.runs && run.state.phase == kTaking,
        "with no tree in reach the take gets nothing and still runs", acts, 0);
  acts = TakeFrames(&run, 1, true, kHeldBundle, true);
  Check(acts == 2, "a tree that comes in reach again goes in at once", acts,
        2);
  // Open = stop and keep.
  TakeFrames(&run, 1, false, kHeldBundle, true);
  Check(run.out.ended && !run.out.runs && run.state.phase == kIdle,
        "the opening of the grip ends the take", run.out.ended, 1);
  acts = TakeFrames(&run, 100, false, kHeldBundle, true);
  Check(acts == 0 && run.state.phase == kIdle,
        "nothing collects with the grip open", acts, 0);
  // The next close does not collect: it carries the bundle for the let go.
  acts = TakeFrames(&run, 200, true, kHeldBundle, true);
  Check(acts == 0 && run.state.phase == kIdle && !run.out.runs,
        "a close on the bundle starts no new take", acts, 0);

  // ONE TREE STAYS ONE TREE.
  TakeStart(&run);
  TakeFrames(&run, 30, true, kHeldTree, true);
  TakeFrames(&run, 1, false, kHeldTree, true);
  Check(run.state.phase == kIdle && !run.out.ended,
        "a grip that opens before the wait is over starts no take",
        run.state.phase, kIdle);
  TakeStart(&run);
  TakeFrames(&run, 10, true, kHeldTree, true);
  acts = TakeFrames(&run, 27, true, kHeldTree, false);
  Check(run.state.phase == kArmed,
        "297 ms with no second tree in reach: the wait still runs",
        run.state.phase, kArmed);
  TakeFrames(&run, 2, true, kHeldTree, false);
  Check(run.state.phase == kCarried,
        "past 300 ms with no second tree in reach the tree left the forest",
        run.state.phase, kCarried);
  acts = TakeFrames(&run, 400, true, kHeldTree, true);
  Check(acts == 0 && run.state.phase == kCarried,
        "a tree that the player carries into a forest starts no take", acts,
        0);
  TakeFrames(&run, 1, false, kHeldNothing, true);
  Check(run.state.phase == kIdle, "the let go of the tree ends that state",
        run.state.phase, kIdle);
  // A short gap in the reach does not end the wait.
  TakeStart(&run);
  TakeFrames(&run, 20, true, kHeldTree, true);
  TakeFrames(&run, 20, true, kHeldTree, false);
  acts = TakeFrames(&run, 20, true, kHeldTree, true);
  Check(acts == 1, "a gap of 220 ms in the reach does not stop the start",
        acts, 1);
  // A tree in the closed grip with no forest at all.
  TakeStart(&run);
  acts = TakeFrames(&run, 300, true, kHeldTree, false);
  Check(acts == 0 && run.state.phase == kCarried,
        "a tree with no second tree near it is carried", acts, 0);
  // Another object.
  TakeStart(&run);
  acts = TakeFrames(&run, 300, true, kHeldOther, true);
  Check(acts == 0 && run.state.phase == kIdle,
        "an object that is no tree starts no wait", acts, 0);

  // THE WAYS OUT.
  TakeStart(&run);
  TakeFrames(&run, 60, true, kHeldTree, true);
  TakeFrames(&run, 30, true, kHeldBundle, true);
  TakeFrames(&run, 1, true, kHeldNothing, true);
  Check(run.out.ended && run.state.phase == kIdle,
        "a bundle that leaves the Hand ends the take", run.out.ended, 1);
  TakeStart(&run);
  TakeFrames(&run, 60, true, kHeldTree, true);
  TakeFrames(&run, 1, true, kHeldBundle, true, false);
  Check(run.out.ended && !run.out.runs,
        "the take ends when the controller does not place the Hand",
        run.out.ended, 1);
  // The game refused the start.
  {
    State s;
    Reset(&s);
    forest_take::Facts f = {true, true, kHeldTree, true, 0.0};
    Step(&s, f, 600, 250, 300);
    f.now_ms = 700.0;
    const Out o = Step(&s, f, 600, 250, 300);
    Check(o.act == kActStart, "the start comes", o.act, kActStart);
    Failed(&s);
    f.now_ms = 2000.0;
    const Out again = Step(&s, f, 600, 250, 300);
    Check(again.act == kActNone && s.phase == kCarried && !again.runs,
          "a start that the game refused is not tried again for that tree",
          again.act, kActNone);
  }

  // THE NEXT TREE. A tree of radius 4 and height 10 with its base at the
  // origin: the crown is a spheroid about the point 0 5 0.
  const Tree tree = {{0.0f, 0.0f, 0.0f}, 4.0f, 10.0f};
  const float inside[3] = {1.0f, 5.0f, 0.0f};
  const float beside[3] = {7.0f, 5.0f, 0.0f};
  const float above[3] = {0.0f, 14.0f, 0.0f};
  Check(HandGap(inside, tree) == 0.0f, "a point in the crown has no gap",
        HandGap(inside, tree), 0.0);
  Near("a point 7 units from the axis at half the height is 3 units from the "
       "crown", HandGap(beside, tree), 3.0, 0.01);
  Near("a point 14 units up is 4 units over the top", HandGap(above, tree),
       4.0, 0.01);
  const float origin[3] = {0.0f, 5.0f, -40.0f};
  const float ahead[3] = {0.0f, 0.0f, 1.0f};
  Check(AimAngle(origin, ahead, tree) == 0.0f,
        "a ray through the middle of the tree has the angle 0",
        AimAngle(origin, ahead, tree), 0.0);
  const float aside[3] = {0.1736482f, 0.0f, 0.9848078f};  // 10 degrees
  const float want = 10.0f - asinf(4.0f / 40.0f) * 57.29578f;
  Near("a ray 10 degrees to the side: 10 degrees less the angle of the radius",
       AimAngle(origin, aside, tree), want, 0.02);
  const float back[3] = {0.0f, 0.0f, -1.0f};
  Check(AimAngle(origin, back, tree) == 180.0f,
        "a tree behind the origin of the ray is out", AimAngle(origin, back,
                                                              tree), 180.0);
}

// THE GRIP during the take of the mod: the closed grip never presses, the
// opening keeps the bundle, and the next close and opening let it go.
inline void ForestGripChecks() {
  using namespace controller_math;
  printf("\nThe grip during a take from a forest\n");
  GripRun run;
  const GripFacts empty = Facts(false, true);
  const GripFacts locked = Facts(true, false);
  const GripFacts held = Facts(true, true);
  GripFacts take = Facts(true, true);
  take.mod_take = true;

  // The tree comes into the closed grip, and the grip carries it.
  GripStart(&run);
  GripFrames(&run, 3, false, empty);
  GripFrames(&run, 10, true, empty);
  GripFrames(&run, 5, true, locked);
  int e = GripFrames(&run, 2, true, held);
  Check((e & kGripCarried) != 0 && run.hold.phase == kGripCarry &&
            !run.out.button,
        "the tree is in the Hand, and the grip carries it with no button", e,
        kGripCarried);
  // The take starts: 300 frames with the grip closed.
  int down = 0, ends = 0;
  e = 0;
  for (int i = 0; i < 300; ++i) {
    e |= GripFrames(&run, 1, true, take);
    if (run.out.button) ++down;
    if (run.out.end_take) ++ends;
  }
  Check(down == 0, "the closed grip never presses the game's button in the "
        "take of the mod", down, 0);
  Check(ends == 0, "and the grip machine never asks for the end of a take of "
        "the game", ends, 0);
  Check(e == 0 && run.hold.phase == kGripPass,
        "the bundle in the Hand gives no event", e, 0);
  // The grip opens: the take is still on for this step.
  e = GripFrames(&run, 1, false, take);
  Check(run.out.take_opened && !run.out.opened && !run.out.click &&
            !run.out.button && e == 0,
        "the opening latches no throw and sends no click", run.out.take_opened,
        1);
  // The take is over, and the bundle stays.
  e = GripFrames(&run, 10, false, held);
  Check((e & kGripOpenedHeld) != 0 && run.hold.phase == kGripOpenHeld &&
            (e & (kGripLate | kGripClickDown | kGripLettingGo)) == 0,
        "the bundle stays in the Hand with the grip open", e, kGripOpenedHeld);
  // The next close carries, and the next opening lets go.
  e = GripFrames(&run, 5, true, held);
  Check((e & kGripRegrip) != 0 && run.hold.phase == kGripCarry &&
            !run.out.button,
        "the next close carries the bundle", e, kGripRegrip);
  e = GripFrames(&run, 2, false, held);
  Check((e & kGripLettingGo) != 0 && (e & kGripClickDown) != 0 &&
            run.out.click,
        "and the next opening lets the bundle go with the click", e,
        kGripLettingGo | kGripClickDown);
  // A take that ends with the grip still closed: the grip carries the bundle.
  GripStart(&run);
  GripFrames(&run, 3, false, empty);
  GripFrames(&run, 10, true, empty);
  GripFrames(&run, 7, true, held);
  GripFrames(&run, 50, true, take);
  e = GripFrames(&run, 3, true, held);
  Check((e & kGripCarried) != 0 && run.hold.phase == kGripCarry &&
            !run.out.button,
        "a take that ends with the grip closed gives a carry and no press", e,
        kGripCarried);
}

}  // namespace forest_checks
