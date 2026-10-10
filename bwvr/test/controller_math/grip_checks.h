// THE BULK TAKE RULE, THE GRIP THAT OPENS AT ONCE AND THE RANGE OF A FAR GRAB
// (docs/grab.md, "The bulk take" and "The grab from a
// distance"). main.cpp includes this file after Check, Near, GripRun, Facts,
// GripStart and GripFrames, just before main.
#pragma once

namespace grip_checks {

// The facts while the game takes from a bulk source: the controller places the
// Hand, the Pot is in it, and the take runs.
inline GripFacts TakeFacts() {
  return Facts(true, false, false, false, true, true);
}

// THE RULE: hold the grip on a bulk source to collect, open the grip to stop
// and keep the bundle, close and open again to drop. Nothing collects after
// the grip opens.
inline void TakeRuleChecks() {
  using namespace controller_math;
  printf("\nThe bulk take rule\n");
  GripRun run;
  const GripFacts empty = Facts(false, true);
  const GripFacts held = Facts(true, true);
  const GripFacts locked = Facts(true, false);
  const GripFacts take = TakeFacts();

  // Hold = collect.
  GripStart(&run);
  GripFrames(&run, 3, false, empty);
  GripFrames(&run, 1, true, empty);
  Check(run.out.button && !run.out.click,
        "a close on a bulk source presses the game's button", 1, 1);
  GripFrames(&run, 5, true, empty);
  int down = 0, ends = 0, e = 0;
  for (int i = 0; i < 300; ++i) {
    e |= GripFrames(&run, 1, true, take);
    if (run.out.button) ++down;
    if (run.out.end_take) ++ends;
  }
  Check(down == 300, "the button is down on all 300 frames of the take", down,
        300);
  Check(e == 0 && run.hold.phase == kGripPass,
        "the Pot in the Hand gives no carry and no other event", e, 0);
  Check(ends == 0, "a closed grip never asks for the end of the take", ends,
        0);

  // Open = stop, and the bundle stays.
  e = GripFrames(&run, 1, false, take);
  Check(!run.out.button, "the opening lets the button go up at once", 0, 0);
  Check(run.out.take_opened && !run.out.opened && !run.out.click && e == 0,
        "the opening of a take latches no throw and sends no click",
        run.out.take_opened, 1);
  // The game ends the take one game turn later, 9 frames at most.
  ends = 0;
  down = 0;
  for (int i = 0; i < 9; ++i) {
    GripFrames(&run, 1, false, take);
    if (run.out.end_take) ++ends;
    if (run.out.button) ++down;
  }
  Check(ends == 0 && down == 0,
        "the game has a game turn to end the take: no button, and the mod "
        "does not ask for the end", ends + down, 0);
  e = GripFrames(&run, 20, false, locked);
  Check((e & kGripOpenedHeld) != 0 && (e & (kGripLate | kGripClickDown)) == 0 &&
            run.hold.phase == kGripOpenHeld && !run.out.button,
        "the bundle stays in the Hand with the grip open: no let go", e,
        kGripOpenedHeld);
  Check(!run.out.end_take, "and nothing asks for the end of a take that is "
        "over", run.out.end_take, 0);

  // Close and open again = drop.
  e = GripFrames(&run, 1, true, held);
  Check((e & kGripRegrip) != 0 && !run.out.button && run.hold.phase == kGripCarry,
        "the next close carries the bundle with no button", e, kGripRegrip);
  e = GripFrames(&run, 30, true, held);
  Check(e == 0 && !run.out.button, "the carry sends no button", e, 0);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripClickDown) != 0 && run.out.button && run.out.click &&
            run.out.opened,
        "the next opening clicks to drop the bundle, and latches the throw", 1,
        1);

  // NOTHING COLLECTS AFTER THE GRIP OPENS. The game does not end the
  // take when the button goes up, and it collects to the limit of 20000. The
  // step asks for the end.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 100, true, take);
  GripFrames(&run, 1, false, take);
  const uint64_t opened = run.frame;
  int first_end = -1;
  down = 0;
  for (int i = 1; i <= 60; ++i) {
    GripFrames(&run, 1, false, take);
    if (run.out.button) ++down;
    if (run.out.end_take && first_end < 0) {
      first_end = static_cast<int>(run.frame - opened);
    }
  }
  Check(first_end == static_cast<int>(kGripTakeEndFrames),
        "a take that still runs 18 frames after the opening: the step asks "
        "for its end", first_end, static_cast<double>(kGripTakeEndFrames));
  Check(run.out.end_take, "and it asks on each frame until the take is over",
        run.out.end_take, 1);
  Check(down == 0, "the button stays up for all of these frames", down, 0);
  // A second step in one frame changes nothing.
  {
    const GripStepOut again = GripStep(&run.hold, false, take, run.frame);
    Check(again.end_take && !again.button && again.events == 0 &&
              !again.take_opened,
          "a second step in one frame gives the same answer", again.end_take,
          1);
  }
  // The take ends: the ask stops, and the bundle is in the Hand.
  e = GripFrames(&run, 16, false, locked);
  Check(!run.out.end_take && run.hold.phase == kGripOpenHeld &&
            (e & kGripClickDown) == 0,
        "after the forced end the bundle stays in the Hand, with no click",
        run.hold.phase, kGripOpenHeld);

  // The grip closes again while the game still collects: it collects on, and
  // the next opening starts the count again.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 50, true, take);
  GripFrames(&run, 10, false, take);
  GripFrames(&run, 1, true, take);
  Check(run.out.button && !run.out.end_take,
        "a close during a take that still runs presses again", 1, 1);
  GripFrames(&run, 40, true, take);
  Check(run.out.button && !run.out.end_take,
        "and a closed grip is never ended, 40 frames later", 1, 1);
  GripFrames(&run, static_cast<int>(kGripTakeEndFrames), false, take);
  Check(!run.out.end_take, "the count starts again at the next opening", 0, 0);
  GripFrames(&run, 1, false, take);
  Check(run.out.end_take, "and ends the take 18 frames after it", 1, 1);

  // A short press: the grip opened before the game started the take. The
  // take then starts with the button up.
  GripStart(&run);
  GripFrames(&run, 3, true, empty);
  GripFrames(&run, 4, false, empty);
  GripFrames(&run, static_cast<int>(kGripTakeEndFrames), false, take);
  Check(!run.out.button && !run.out.end_take,
        "a take that starts after the grip opened: no button", 0, 0);
  GripFrames(&run, 1, false, take);
  Check(run.out.end_take, "and the step asks for its end 18 frames later", 1,
        1);
  e = GripFrames(&run, 16, false, locked);
  Check(run.hold.phase == kGripOpenHeld && (e & (kGripLate | kGripClickDown)) == 0,
        "the first amount stays in the Hand: a late take is no late object "
        "to let go", run.hold.phase, kGripOpenHeld);

  // A take that starts while the grip carried another object: the grip must
  // open before it is the button, so nothing collects.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 2, true, held);
  Check(run.hold.phase == kGripCarry, "a carry", run.hold.phase, kGripCarry);
  GripFrames(&run, static_cast<int>(kGripTakeEndFrames) + 1, true, take);
  Check(!run.out.button && run.out.end_take,
        "a take during a carry gets no button, and it is ended", 1, 1);

  // The mouse places the Hand during a take: the grip is the button as
  // before, and the step never ends the take of the mouse.
  const GripFacts mouse = Facts(false, false, false, false, false, true);
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 20, true, mouse);
  Check(run.out.button, "with the mouse the closed grip is still the button",
        1, 1);
  GripFrames(&run, 60, false, mouse);
  Check(!run.out.button && !run.out.end_take && !run.out.take_opened,
        "and the step asks for no end of the mouse's take", 0, 0);
}

// THE GRIP OPENS AT ONCE: GripLevelStep.
inline void LevelChecks() {
  using namespace controller_math;
  printf("\nThe grip opens at once\n");
  GripLevel a;

  // The plain hysteresis, as before: closed above 0.55, open below 0.35.
  GripLevelReset(&a);
  Check(!GripLevelStep(&a, 0.50f, false), "0.50 does not close the grip", 0,
        0);
  Check(GripLevelStep(&a, 0.56f, false), "0.56 closes it", 1, 1);
  Check(GripLevelStep(&a, 0.36f, false), "0.36 keeps it closed", 1, 1);
  Check(!GripLevelStep(&a, 0.34f, false), "0.34 opens it", 0, 0);
  Check(!a.fast, "that opening is not a fast fall", a.fast, 0);
  Check(!GripLevelStep(&a, 0.50f, false) && GripLevelStep(&a, 0.60f, false),
        "and it closes again above 0.55", 1, 1);

  // With no fast opening the answer is the old rule on every step of a
  // sweep up and down.
  {
    GripLevelReset(&a);
    bool old_down = false;
    int differ = 0;
    for (int i = 0; i < 400; ++i) {
      const float v = 0.5f + 0.5f * sinf(i * 0.37f) * cosf(i * 0.011f);
      old_down = old_down ? v > 0.35f : v > 0.55f;
      if (GripLevelStep(&a, v, false) != old_down) ++differ;
    }
    Check(differ == 0, "without the fast opening the grip is the old "
          "hysteresis on 400 values", differ, 0);
  }

  // A hand that opens for a throw: the value goes from 1 to 0 in 8 frames.
  {
    GripLevel fast, slow;
    GripLevelReset(&fast);
    GripLevelReset(&slow);
    for (int i = 0; i < 5; ++i) {
      GripLevelStep(&fast, 1.0f, true);
      GripLevelStep(&slow, 1.0f, false);
    }
    int fast_frame = -1, slow_frame = -1;
    for (int i = 1; i <= 8; ++i) {
      const float v = 1.0f - 0.125f * i;
      if (!GripLevelStep(&fast, v, true) && fast_frame < 0) fast_frame = i;
      if (!GripLevelStep(&slow, v, false) && slow_frame < 0) slow_frame = i;
    }
    Check(fast_frame == 3, "a release in 8 frames opens the grip on frame 3, "
          "at 0.625", fast_frame, 3);
    Check(slow_frame == 6, "the old rule opened it on frame 6, at 0.25",
          slow_frame, 6);
    Check(fast.fast, "the opening is a fast fall", fast.fast, 1);
    Check(!fast.down && fast_frame > 0, "the grip stays open while the value "
          "falls on to 0", fast.down, 0);
  }

  // A slow change opens only below 0.35.
  {
    GripLevelReset(&a);
    for (int i = 0; i < 5; ++i) GripLevelStep(&a, 1.0f, true);
    int opened_at = -1;
    float v = 1.0f;
    for (int i = 1; i <= 40 && opened_at < 0; ++i) {
      v -= 0.02f;
      if (!GripLevelStep(&a, v, true)) opened_at = i;
    }
    Check(opened_at == 33 && !a.fast,
          "a grip that relaxes by 0.02 a frame opens only below 0.35, on "
          "frame 33", opened_at, 33);
  }

  // A small dip is no opening.
  {
    GripLevelReset(&a);
    for (int i = 0; i < 5; ++i) GripLevelStep(&a, 1.0f, true);
    Check(GripLevelStep(&a, 0.72f, true), "a fall of 0.28 in one frame keeps "
          "the grip closed", 1, 1);
    GripLevelReset(&a);
    for (int i = 0; i < 5; ++i) GripLevelStep(&a, 1.0f, true);
    GripLevelStep(&a, 0.95f, true);
    Check(GripLevelStep(&a, 0.82f, true), "a fall to 0.82 keeps it closed: "
          "the value is not below 0.80", 1, 1);
    Check(!GripLevelStep(&a, 0.69f, true), "a fall of 0.31 in 3 frames, to "
          "0.69, opens it", 0, 0);
  }

  // After a fast opening the grip closes again only on a rise.
  {
    GripLevelReset(&a);
    for (int i = 0; i < 5; ++i) GripLevelStep(&a, 1.0f, true);
    const bool closed = GripLevelStep(&a, 0.65f, true);
    Check(!closed && a.fast, "a fast opening at 0.65", a.fast, 1);
    Check(!GripLevelStep(&a, 0.65f, true) && !GripLevelStep(&a, 0.66f, true),
          "a value that stays at 0.65 does not close the grip again", 0, 0);
    Check(!GripLevelStep(&a, 0.62f, true) && !GripLevelStep(&a, 0.70f, true),
          "a rise of 0.08 from the lowest value does not close it", 0, 0);
    Check(GripLevelStep(&a, 0.73f, true), "a rise of 0.11 closes it again", 1,
          1);
    // A full opening and a new close.
    for (int i = 0; i < 5; ++i) GripLevelStep(&a, 1.0f, true);
    GripLevelStep(&a, 0.60f, true);
    GripLevelStep(&a, 0.20f, true);
    Check(!GripLevelStep(&a, 0.50f, true) && GripLevelStep(&a, 0.56f, true),
          "after a full opening the grip closes above 0.55, as before", 1, 1);
  }

  // The trace keeps the last 12 values, the newest last.
  {
    GripLevelReset(&a);
    for (int i = 0; i < 20; ++i) GripLevelStep(&a, i * 0.01f, true);
    Check(a.count == kGripTrace, "the trace holds 12 values", a.count,
          kGripTrace);
    Near("the newest value is the last one", a.trace[kGripTrace - 1], 0.19,
         1e-6);
    Near("and the oldest is 11 frames back", a.trace[0], 0.08, 1e-6);
    volatile float zero = 0.0f;
    const float nan_value = zero / zero;
    Check(!GripLevelStep(&a, nan_value, true), "a value that is no number "
          "is an open grip", 0, 0);
  }

  // THE PRESS GUARD: a dip of the squeeze in the first 300 ms of a press does
  // not open the grip. The values are the dip of a real hand that jerks away.
  {
    Check(GripPressGuarded(true, true, kGripPass, 0),
          "the guard holds at the close", 1, 1);
    Check(GripPressGuarded(true, true, kGripPass, 299),
          "and until 300 ms", 1, 1);
    Check(!GripPressGuarded(true, true, kGripPass, 300),
          "and not after 300 ms", 0, 0);
    Check(!GripPressGuarded(true, true, kGripCarry, 50),
          "a carry has the fast fall for the throw", 0, 0);
    Check(!GripPressGuarded(true, true, kGripLetGo, 50),
          "and so has the let go", 0, 0);
    Check(!GripPressGuarded(true, false, kGripPass, 50),
          "an open grip has nothing to guard", 0, 0);
    Check(!GripPressGuarded(false, true, kGripPass, 50),
          "with grip_press_guard=0 nothing is guarded", 0, 0);

    // The same 1.0, 0.92, 0.59 as in a log of a real hand, 6 frames after the
    // close. With the fast fall the grip opens. Under the guard it stays
    // closed, and it opens when the player really opens it (below 0.35).
    GripLevel plain, guarded;
    GripLevelReset(&plain);
    GripLevelReset(&guarded);
    const float close[6] = {0.30f, 0.70f, 1.0f, 1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 6; ++i) {
      GripLevelStep(&plain, close[i], true);
      GripLevelStep(&guarded, close[i], !GripPressGuarded(true, true,
                                                          kGripPass, 66));
    }
    GripLevelStep(&plain, 0.92f, true);
    GripLevelStep(&guarded, 0.92f, false);
    Check(!GripLevelStep(&plain, 0.59f, true),
          "without the guard the dip 1.00, 0.92, 0.59 opens the grip", 0, 0);
    Check(GripLevelStep(&guarded, 0.59f, false),
          "with the guard the same dip keeps it closed", 1, 1);
    Check(GripLevelStep(&guarded, 0.95f, false),
          "and the grip is still closed when the squeeze returns", 1, 1);
    Check(!GripLevelStep(&guarded, 0.30f, false),
          "a real opening, below 0.35, still opens it under the guard", 0, 0);
  }
}

// THE RANGE OF A FAR GRAB: FarFloorMeters and FarInRange.
inline void RangeChecks() {
  using namespace controller_math;
  printf("\nThe range of a far grab\n");
  const float head[3] = {1000.0f, 60.0f, 2000.0f};
  const float units = 100.0f;
  {
    const float at[3] = {1150.0f, 0.0f, 2000.0f};
    Near("an object 150 units across at 100 units a meter is 1.5 m from the "
         "head", FarFloorMeters(head, at, units), 1.5, 1e-5);
    const float low[3] = {1150.0f, -300.0f, 2000.0f};
    Near("the height does not count: 3 m lower is still 1.5 m",
         FarFloorMeters(head, low, units), 1.5, 1e-5);
    const float diagonal[3] = {1120.0f, 0.0f, 2160.0f};
    Near("120 and 160 units across are 2.0 m", FarFloorMeters(head, diagonal,
                                                               units),
         2.0, 1e-5);
    Near("at 66.7 units a meter the same object is 3.0 m away",
         FarFloorMeters(head, diagonal, 66.6667f), 3.0, 1e-3);
    Check(FarFloorMeters(head, at, 0.0f) > 1e20f,
          "with no scale the distance is not known, so it is out of range",
          1, 1);
  }
  Check(FarInRange(1.90f, 2.0f, false), "1.90 m is in a range of 2 m", 1, 1);
  Check(FarInRange(2.00f, 2.0f, false), "2.00 m is in it", 1, 1);
  Check(!FarInRange(2.05f, 2.0f, false), "2.05 m is out for a new target", 0,
        0);
  Check(FarInRange(2.05f, 2.0f, true),
        "2.05 m keeps the target of the last frame", 1, 1);
  Check(FarInRange(2.15f, 2.0f, true), "2.15 m keeps it too", 1, 1);
  Check(!FarInRange(2.20f, 2.0f, true), "2.20 m loses it", 0, 0);
  Check(FarInRange(50.0f, 0.0f, false), "a range of 0 is no limit", 1, 1);
  Check(!FarInRange(1e30f, 2.0f, true),
        "an object with no known distance is out", 0, 0);
}

inline void GripRuleChecks() {
  TakeRuleChecks();
  LevelChecks();
  RangeChecks();
}

}  // namespace grip_checks
