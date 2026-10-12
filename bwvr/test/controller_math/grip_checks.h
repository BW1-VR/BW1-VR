// THE BULK TAKE RULE, THE GRIP THAT OPENS AT ONCE, THE PRESS OF THE GAME AND
// THE RANGE OF A FAR GRAB (docs/grab.md, "The bulk take", "The grip holds"
// and "The grab from a distance"). main.cpp includes this file after Check,
// Near, GripRun, Facts, GripStart, GripFrames and GripTestFrames, just before
// main.
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
  // The game ends the take one game turn later, 100 ms at most.
  ends = 0;
  down = 0;
  for (int i = 0; i < GripTestFrames(100) - 1; ++i) {
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
  const uint64_t opened = run.ms;
  int first_end = -1;
  down = 0;
  for (int i = 1; i <= 60; ++i) {
    GripFrames(&run, 1, false, take);
    if (run.out.button) ++down;
    if (run.out.end_take && first_end < 0) {
      first_end = static_cast<int>(run.ms - opened);
    }
  }
  Check(first_end == static_cast<int>(kGripTakeEndMs),
        "a take that still runs 200 ms after the opening: the step asks "
        "for its end", first_end, static_cast<double>(kGripTakeEndMs));
  Check(run.out.end_take, "and it asks on each frame until the take is over",
        run.out.end_take, 1);
  Check(down == 0, "the button stays up for all of these frames", down, 0);
  // A second step in one frame changes nothing.
  {
    const GripStepOut again =
        GripStep(&run.hold, false, take, run.frame, run.ms);
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
  GripFrames(&run, GripTestFrames(kGripTakeEndMs), false, take);
  Check(!run.out.end_take, "the time starts again at the next opening", 0, 0);
  GripFrames(&run, 1, false, take);
  Check(run.out.end_take, "and ends the take 200 ms after it", 1, 1);

  // A short press: the grip opened before the game started the take. The
  // take then starts with the button up.
  GripStart(&run);
  GripFrames(&run, 3, true, empty);
  GripFrames(&run, 4, false, empty);
  GripFrames(&run, GripTestFrames(kGripTakeEndMs), false, take);
  Check(!run.out.button && !run.out.end_take,
        "a take that starts after the grip opened: no button", 0, 0);
  GripFrames(&run, 1, false, take);
  Check(run.out.end_take, "and the step asks for its end 200 ms later", 1,
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
  GripFrames(&run, GripTestFrames(kGripTakeEndMs) + 1, true, take);
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

// A TAKE AT A DISTANCE WHOSE SOURCE GOES AWAY (docs/grab.md, "A bulk source at
// a distance"). The game ends the take and deletes a pile that is used up in
// one game turn, with the grip closed. FarHoldsBack says whether the far grab
// then holds the grip back, and the grip machine shows what each answer does.
inline void FarTakeChecks() {
  using namespace controller_math;
  printf("\nA take at a distance whose source goes away\n");
  Check(!FarHoldsBack(true, true, true),
        "the Take state ends with the grip closed: the grip stays with the "
        "grip machine (far_take_carry=1)", 0, 0);
  Check(FarHoldsBack(true, true, false),
        "with far_take_carry=0 the far grab holds the grip back", 1, 1);
  Check(FarHoldsBack(true, false, true) && FarHoldsBack(true, false, false),
        "a lock, a pull or a fetch that ends with the grip closed holds it "
        "back with each value of the key", 1, 1);
  Check(!FarHoldsBack(false, true, true) && !FarHoldsBack(false, true, false) &&
            !FarHoldsBack(false, false, true),
        "an open grip is never held back", 0, 0);

  GripRun run;
  const GripFacts empty = Facts(false, true);
  const GripFacts held = Facts(true, true);
  const GripFacts take = TakeFacts();

  // The grip stays with the machine: the grip is closed in the frame after
  // the take, and it carries the bundle.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 300, true, take);
  int e = GripFrames(&run, 1, true, held);
  Check((e & kGripCarried) != 0 && (e & (kGripLate | kGripClickDown)) == 0 &&
            run.hold.phase == kGripCarry && !run.out.button,
        "the machine sees the closed grip after the take: the grip carries "
        "the bundle, with no click", e, kGripCarried);
  e = GripFrames(&run, GripTestFrames(15000), true, held);
  Check(e == 0 && run.hold.phase == kGripCarry && !run.out.button &&
            !run.out.click,
        "the grip carries the bundle for 15 s with no button and no click",
        e, 0);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripLettingGo) != 0 && (e & kGripClickDown) != 0,
        "and the opening of the grip lets the bundle go, as for each object",
        e, kGripLettingGo | kGripClickDown);

  // The far grab holds the grip back: the machine sees an open grip in the
  // frame after the take. That is the opening of a press whose object came
  // late, so the machine lets the bundle go with its click.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 300, true, take);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripLate) != 0 && run.hold.phase == kGripLetGo,
        "the machine sees an open grip in the frame after the take: the "
        "bundle is a late object to let go", e, kGripLate);
  e |= GripFrames(&run, 2, false, held);
  Check((e & kGripClickDown) != 0,
        "and the click of the machine drops the bundle, with the real grip "
        "closed", e & kGripClickDown, kGripClickDown);
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

// THE PRESS OF THE GAME (grip_press_hold). A model of what the game does with
// a press on an object that its physics has (fn_005D1740, the step of the
// pick up action fn_005D5250, and fn_005D2800), and the grip machine on top
// of it.
//
// The model, one step a frame, after the step of the machine:
//   A new press puts the object in the Hand of the picture, sets the pick
//   lock and keeps the clock and the game turn.
//   While the age of the press is less than 225 ms on the clock of the game
//   (GamePressAge), a button that is up is a click: the Hand lets the object
//   go, and the game taps it.
//   From 225 ms on the game sends the pick up, and it has the object in its
//   Hand at the start of the next game turn. The pick lock is clear from
//   then on (the step of action 7, fn_005D4A90), with the button down or up.
struct CatchGame {
  bool pressed;     // the pick up action runs
  bool sent;        // the pick up went out
  bool picture;     // the Hand of the picture has the object
  bool in_hand;     // the game has the object in its Hand
  bool locked;
  bool clicked;     // the game took the press as a click
  bool button;      // the button at the last step
  uint32_t press_ms;
  uint32_t press_turn;
  uint32_t sent_turn;
  uint32_t age;     // the age of the press at the last step
  double down_ms;   // how long the button was down for the game
};

inline void CatchGameStep(CatchGame* g, bool button, double now_ms,
                          double turn_ms, double turn_phase_ms) {
  using namespace controller_math;
  const uint32_t clock = static_cast<uint32_t>(now_ms);
  const uint32_t turn =
      static_cast<uint32_t>((now_ms + turn_phase_ms) / turn_ms);
  if (button && !g->button && !g->pressed && !g->in_hand && !g->clicked) {
    g->pressed = true;
    g->picture = true;
    g->locked = true;
    g->press_ms = clock;
    g->press_turn = turn;
    g->down_ms = now_ms;
  }
  if (g->pressed && !g->sent) {
    g->age = GamePressAge(clock, g->press_ms, turn, g->press_turn);
    if (g->age >= kGripClickMs) {
      g->sent = true;
      g->sent_turn = turn;
    } else if (!button) {
      g->clicked = true;
      g->pressed = false;
      g->picture = false;
      g->locked = false;
    }
  } else if (g->pressed) {
    g->age = GamePressAge(clock, g->press_ms, turn, g->press_turn);
  }
  if (g->sent && !g->in_hand && turn > g->sent_turn) g->in_hand = true;
  if (g->sent && g->in_hand) g->locked = false;
  if (!button && g->button && g->down_ms >= 0.0) {
    g->down_ms = now_ms - g->down_ms;
  }
  g->button = button;
}

// What one press ends as.
struct CatchResult {
  bool clicked;     // the game took the press as a click: the object is lost
  bool carried;     // the machine carries the object
  bool let_go;      // the machine let the object go with its click
  bool held_on;     // the mod kept the button down after the grip opened
  double down_ms;   // how long the first press was for the game
  int phase;
};

// One press on an object that the physics of the game has. The grip closes
// at 100 ms and opens grip_ms later. frame_ms is the time of a frame, turn_ms
// the time of a game turn, and turn_phase_ms the time from the start of a
// game turn to the start of the run. moving is false for an object at rest
// with the same press: the model of the game is the same (a tree in the
// ground has the 225 ms too), and only the fact of the machine differs.
inline CatchResult CatchRun(double frame_ms, double grip_ms, bool press_hold,
                            double turn_ms, double turn_phase_ms,
                            bool moving = true) {
  using namespace controller_math;
  CatchGame game = {};
  game.down_ms = -1.0;
  GripHold hold;
  GripReset(&hold);
  CatchResult result = {};
  double first_down = -1.0;
  bool first_up = false;
  uint64_t frame = 0;
  const double close_ms = 100.0;
  for (double now = 0.0; now < 4000.0; now += frame_ms) {
    ++frame;
    // The pump reads the facts that the step of the frame before left.
    GripFacts f = {};
    f.direct = true;
    f.holding = game.picture;
    f.settled = !game.locked;
    f.game_holds = game.in_hand;
    f.moving = moving && game.picture && !game.in_hand;
    f.click = game.pressed && GameClick(game.age);
    f.press_hold = press_hold;
    f.spring = false;
    const bool grip = now >= close_ms && now < close_ms + grip_ms;
    const GripStepOut out =
        GripStep(&hold, grip, f, frame, static_cast<uint64_t>(now));
    if ((out.events & kGripHeldOn) != 0) result.held_on = true;
    if ((out.events & kGripCarried) != 0) result.carried = true;
    if ((out.events & kGripClickDown) != 0) result.let_go = true;
    if (out.button && !out.click && first_down < 0.0) first_down = now;
    if (!out.button && first_down >= 0.0 && !first_up) {
      first_up = true;
      result.down_ms = now - first_down;
    }
    // The click of the machine is the press of a release: the model has no
    // release, so it gets only the press of the take.
    CatchGameStep(&game, out.button && !out.click, now, turn_ms,
                  turn_phase_ms);
  }
  result.clicked = game.clicked;
  result.phase = hold.phase;
  return result;
}

inline void PressHoldChecks() {
  using namespace controller_math;
  printf("\nThe press of the game on an object that moves\n");

  // THE AGE OF A PRESS, as the game counts it.
  Check(GamePressAge(1000, 800, 12, 10) == 200,
        "200 ms of real time and three game turns: the age is 200 ms",
        GamePressAge(1000, 800, 12, 10), 200);
  Check(GamePressAge(1300, 800, 11, 10) == 200,
        "500 ms of real time in the turn after the press: the age is 200 ms",
        GamePressAge(1300, 800, 11, 10), 200);
  Check(GamePressAge(1300, 800, 10, 10) == 100,
        "in the game turn of the press the age is 100 ms at most",
        GamePressAge(1300, 800, 10, 10), 100);
  Check(GamePressAge(1050, 800, 12, 10) == 250,
        "250 ms of real time and two game turns: the age is 250 ms",
        GamePressAge(1050, 800, 12, 10), 250);
  Check(GamePressAge(5, 0xFFFFFFF0u, 12, 10) == 21,
        "the clock of the game can wrap: the numbers are unsigned",
        GamePressAge(5, 0xFFFFFFF0u, 12, 10), 21);
  Check(GameClick(224) && GameClick(239) && !GameClick(240),
        "the mod takes a press as a click for the game to 240 ms", 1, 1);
  Check(kGripHoldMs > kGripClickMs && kGripSettleBeforeMs < kGripClickMs,
        "240 ms is past the 225 ms of the game, and 167 ms is not",
        static_cast<double>(kGripHoldMs), static_cast<double>(kGripClickMs));

  // WHY THE FRAME RATE COUNTED. A carry 15 frames after the object came, and
  // one frame for the object: the length of the press in ms.
  {
    const double at60 = 16.0 * 1000.0 / 60.0;
    const double at72 = 16.0 * 1000.0 / 72.0;
    const double at90 = 16.0 * 1000.0 / 90.0;
    Check(at60 > 225.0, "16 frames are 267 ms at 60 frames a second: no click",
          at60, 225.0);
    Check(at72 < 225.0, "222 ms at 72 frames a second: a click", at72, 225.0);
    Check(at90 < 225.0, "178 ms at 90 frames a second: a click", at90, 225.0);
  }

  // THE SAME PRESS AT 60, 72 AND 90 FRAMES A SECOND. The grip closes on an
  // object that moves and stays closed for 1.5 s. At each frame rate, and at
  // each time of the press in a game turn, the machine carries the object,
  // and the game sees no click.
  const double rates[4] = {60.0, 72.0, 90.0, 120.0};
  for (int r = 0; r < 4; ++r) {
    const double frame_ms = 1000.0 / rates[r];
    int clicks = 0, carries = 0, old_clicks = 0, old_carries = 0;
    double least = 1e9, most = 0.0;
    for (int phase = 0; phase < 100; phase += 5) {
      const CatchResult now = CatchRun(frame_ms, 1500.0, true, 100.0, phase);
      if (now.clicked) ++clicks;
      if (now.carried) ++carries;
      if (now.down_ms < least) least = now.down_ms;
      if (now.down_ms > most) most = now.down_ms;
      const CatchResult old = CatchRun(frame_ms, 1500.0, false, 100.0, phase);
      if (old.clicked) ++old_clicks;
      if (old.carried && !old.clicked) ++old_carries;
    }
    char what[160];
    sprintf_s(what, "at %.0f frames a second a closed grip on an object that "
              "moves is no click, at 20 times in a game turn", rates[r]);
    Check(clicks == 0, what, clicks, 0);
    sprintf_s(what, "at %.0f frames a second the grip carries the object "
              "each time", rates[r]);
    Check(carries == 20, what, carries, 20);
    sprintf_s(what, "at %.0f frames a second the button is down for 225 ms "
              "or more, and for less than 500 ms", rates[r]);
    Check(least >= 225.0 && most < 500.0, what, least, 225.0);
    sprintf_s(what, "at %.0f frames a second grip_press_hold=0 is a click "
              "each time: the button goes up after 167 ms", rates[r]);
    Check(old_clicks == 20 && old_carries == 0, what, old_clicks, 20);
  }

  // A SHORT PRESS ON AN OBJECT THAT MOVES. The grip opens after 50 ms. The
  // button stays down until the press is no click, the game takes the
  // object, and the machine lets it go with its click.
  for (int r = 0; r < 4; ++r) {
    const double frame_ms = 1000.0 / rates[r];
    int clicks = 0, held = 0, let_go = 0, old_clicks = 0;
    double least = 1e9;
    for (int phase = 0; phase < 100; phase += 5) {
      const CatchResult now = CatchRun(frame_ms, 50.0, true, 100.0, phase);
      if (now.clicked) ++clicks;
      if (now.held_on) ++held;
      if (now.let_go && !now.carried) ++let_go;
      if (now.down_ms < least) least = now.down_ms;
      if (CatchRun(frame_ms, 50.0, false, 100.0, phase).clicked) ++old_clicks;
    }
    char what[160];
    sprintf_s(what, "at %.0f frames a second a press of 50 ms on an object "
              "that moves is no click for the game", rates[r]);
    Check(clicks == 0 && held == 20, what, clicks, 0);
    sprintf_s(what, "at %.0f frames a second the button stays down for 225 "
              "ms or more", rates[r]);
    Check(least >= 225.0, what, least, 225.0);
    sprintf_s(what, "at %.0f frames a second the object comes into the Hand "
              "and the machine lets it go", rates[r]);
    Check(let_go == 20, what, let_go, 20);
    sprintf_s(what, "at %.0f frames a second grip_press_hold=0 makes the "
              "press of 50 ms a click", rates[r]);
    Check(old_clicks == 20, what, old_clicks, 20);
  }

  // SLOW GAME TURNS. With a game turn of 180 ms, 240 ms of real time are not
  // enough: the game counts 100 ms for each game turn. The machine reads the
  // age of the press from the game, so the press is still no click.
  {
    int clicks = 0, carries = 0;
    double least = 1e9;
    for (int phase = 0; phase < 180; phase += 9) {
      const CatchResult now = CatchRun(1000.0 / 90.0, 50.0, true, 180.0, phase);
      if (now.clicked) ++clicks;
      if (now.down_ms < least) least = now.down_ms;
      const CatchResult closed =
          CatchRun(1000.0 / 90.0, 1500.0, true, 180.0, phase);
      if (closed.clicked) ++clicks;
      if (closed.carried) ++carries;
    }
    Check(clicks == 0 && carries == 20,
          "with a game turn of 180 ms a press is no click, short or long",
          clicks, 0);
    Check(least >= 180.0, "and a short press stays down past the second "
          "game turn after it", least, 180.0);
  }

  // AN OBJECT AT REST keeps the click of the plain game: the button goes up
  // with the grip.
  {
    const CatchResult rest =
        CatchRun(1000.0 / 90.0, 50.0, true, 100.0, 0.0, false);
    Check(!rest.held_on && rest.down_ms < 80.0,
          "a press of 50 ms on an object at rest is 50 ms for the game",
          rest.down_ms, 50.0);
  }

  // THE HOLD ENDS. A game that never counts the press as old (its turns
  // stand still) does not keep the button down for ever.
  {
    GripRun run;
    GripFacts moving = Facts(true, false);
    moving.game_holds = false;
    moving.moving = true;
    moving.click = true;
    GripStart(&run);
    GripFrames(&run, 2, false, Facts(false, true));
    GripFrames(&run, 3, true, moving);
    const uint64_t pressed = run.ms - 2 * kGripTestFrameMs;
    int e = GripFrames(&run, 1, false, moving);
    Check((e & kGripHeldOn) != 0 && run.out.button && !run.out.click,
          "the grip opens while a release is a click: the button stays down",
          e, kGripHeldOn);
    Check(run.out.opened, "and the step still says that the grip opened",
          run.out.opened, 1);
    while (run.out.button && run.ms < pressed + 5000) {
      GripFrames(&run, 1, false, moving);
    }
    Check(run.ms - pressed == kGripHoldMostMs,
          "an open grip keeps the button down for kGripHoldMostMs at most",
          static_cast<double>(run.ms - pressed),
          static_cast<double>(kGripHoldMostMs));
    // The mouse takes the Hand: the hold ends at once.
    GripStart(&run);
    GripFrames(&run, 3, true, moving);
    GripFrames(&run, 1, false, moving);
    GripFacts mouse = moving;
    mouse.direct = false;
    GripFrames(&run, 1, false, mouse);
    Check(!run.out.button, "the mouse takes the Hand: the held press ends",
          0, 0);
    // The pick up of the game is over while the mod holds the press: the
    // object is let go with the click of the machine.
    GripStart(&run);
    GripFrames(&run, 3, true, moving);
    GripFrames(&run, 5, false, moving);
    e = GripFrames(&run, 1, false, Facts(true, true));
    Check((e & kGripLate) != 0 && (e & kGripHeldOff) != 0 && !run.out.button,
          "the object is in the Hand of the game: the held press ends, and "
          "the object is a late object", e, kGripLate | kGripHeldOff);
    e = GripFrames(&run, 1, false, Facts(true, true));
    Check((e & kGripClickDown) != 0 && run.out.button && run.out.click,
          "and the click presses on the next frame", 1, 1);
  }
}

inline void GripRuleChecks() {
  TakeRuleChecks();
  FarTakeChecks();
  LevelChecks();
  PressHoldChecks();
  RangeChecks();
}

}  // namespace grip_checks
