// THE STAY OF THE CREATURE (docs/creature.md, "The stay").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/creature_stay_math.h"

namespace stay_checks {

// One grip on the far Creature at 90 frames a second: it closes at time 0 and
// opens after grip_ms. The hand is lost after lost_ms (less than 0: never).
// The answers: the number of leash clicks, of stay starts and of stay ends,
// and the time of each in ms from the close (-1 for none).
struct Run {
  int clicks = 0;
  int starts = 0;
  int ends = 0;
  double click_ms = -1.0;
  double start_ms = -1.0;
  double end_ms = -1.0;
};

inline Run Grip(int mode, double grip_ms, int hold_ms, double lost_ms = -1.0) {
  using namespace stay_math;
  Run run;
  Press press;
  const double dt = 1.0 / 90.0;
  auto count = [&run](int event, double ms) {
    if (event == kClick) {
      ++run.clicks;
      run.click_ms = ms;
    } else if (event == kStayStart) {
      ++run.starts;
      run.start_ms = ms;
    } else if (event == kStayEnd) {
      ++run.ends;
      run.end_ms = ms;
    }
  };
  count(Close(&press, mode, 100.0), 0.0);
  for (int i = 1; i < 2000; ++i) {
    const double ms = i * dt * 1000.0;
    const bool grip = ms < grip_ms;
    const bool hand = lost_ms < 0.0 || ms < lost_ms;
    count(Step(&press, grip, hand, 100.0 + i * dt, hold_ms), ms);
  }
  return run;
}

inline void StayChecks() {
  using namespace stay_math;
  printf("\nThe stay of the Creature\n");

  // THE KEYS.
  Check(ClampMode(1) == kModeHold && ClampMode(0) == kModeClose &&
            ClampMode(7) == kModeHold,
        "creature_hold_stay: 0 is the click at the close, and each other "
        "value is the hold", ClampMode(7), kModeHold);
  Check(ClampHoldMs(350) == 350 && ClampHoldMs(10) == kHoldMsLeast &&
            ClampHoldMs(90000) == kHoldMsMost,
        "creature_hold_stay_ms stays between 100 and 2000", ClampHoldMs(10),
        kHoldMsLeast);
  Check(kHoldMs > kGameHoldMs, "the default hold time is longer than the "
        "225 ms of the game, which are for a mouse button", kHoldMs,
        kGameHoldMs);

  // A SHORT GRIP is the leash click, at its open.
  {
    const Run r = Grip(kModeHold, 150.0, kHoldMs);
    Check(r.clicks == 1 && r.starts == 0 && r.ends == 0,
          "a grip of 150 ms is one leash click, and no stay", r.clicks, 1);
    Check(r.click_ms >= 150.0 && r.click_ms < 150.0 + 12.0,
          "the click goes out in the pump of the open, not at the close",
          r.click_ms, 150.0);
  }
  {
    const Run r = Grip(kModeHold, 330.0, kHoldMs);
    Check(r.clicks == 1 && r.starts == 0,
          "a grip of 330 ms is still a short grip", r.clicks, 1);
  }

  // A GRIP THAT STAYS CLOSED makes the Creature stay, and never changes the
  // leash.
  {
    const Run r = Grip(kModeHold, 3000.0, kHoldMs);
    Check(r.clicks == 0, "a grip of 3 s sends no leash click", r.clicks, 0);
    Check(r.starts == 1 && r.ends == 1,
          "it starts one stay and ends one stay", r.starts, 1);
    Check(r.start_ms >= 350.0 && r.start_ms < 350.0 + 12.0,
          "the stay starts in the first pump after 350 ms", r.start_ms,
          350.0);
    Check(r.end_ms >= 3000.0 && r.end_ms < 3000.0 + 12.0,
          "the stay ends in the pump of the open", r.end_ms, 3000.0);
  }
  {
    const Run r = Grip(kModeHold, 370.0, kHoldMs);
    Check(r.clicks == 0 && r.starts == 1 && r.ends == 1,
          "a grip of 370 ms is a hold: a stay that starts and ends, and no "
          "click", r.starts, 1);
  }
  {
    const Run r = Grip(kModeHold, 400.0, 225);
    Check(r.starts == 1 && r.start_ms >= 225.0 && r.start_ms < 225.0 + 12.0,
          "creature_hold_stay_ms=225 starts the stay at the time of the "
          "game", r.start_ms, 225.0);
  }
  {
    const Run r = Grip(kModeHold, 15000.0, kHoldMs);
    Check(r.starts == 1 && r.ends == 1 && r.clicks == 0,
          "a grip of 15 s starts one stay only", r.starts, 1);
  }

  // creature_hold_stay=0: the click at the close, and no stay.
  {
    const Run r = Grip(kModeClose, 150.0, kHoldMs);
    Check(r.clicks == 1 && r.click_ms == 0.0,
          "with creature_hold_stay=0 the click goes out at the close",
          r.click_ms, 0.0);
    const Run l = Grip(kModeClose, 3000.0, kHoldMs);
    Check(l.clicks == 1 && l.starts == 0 && l.ends == 0,
          "and a grip of 3 s is one click and no stay", l.starts, 0);
  }

  // THE HAND IS LOST while the grip is closed.
  {
    const Run r = Grip(kModeHold, 3000.0, kHoldMs, 200.0);
    Check(r.clicks == 0 && r.starts == 0 && r.ends == 0,
          "a hand that is lost 200 ms after the close sends no click and "
          "starts no stay", r.clicks, 0);
    const Run l = Grip(kModeHold, 3000.0, kHoldMs, 1000.0);
    Check(l.starts == 1 && l.ends == 1 && l.clicks == 0 &&
              l.end_ms >= 1000.0 && l.end_ms < 1000.0 + 12.0,
          "a hand that is lost in a stay ends the stay at that time",
          l.end_ms, 1000.0);
  }

  // THE KEY CHANGES while the grip is closed: the press keeps its rule.
  {
    Press press;
    Check(Close(&press, kModeClose, 5.0) == kClick,
          "a close with creature_hold_stay=0 is the click", 1, 1);
    // The test key sets 1 now. The open of this grip sends no second click.
    Check(Step(&press, true, true, 5.2, kHoldMs) == kNothing &&
              Step(&press, true, true, 5.5, kHoldMs) == kNothing &&
              Step(&press, false, true, 5.6, kHoldMs) == kNothing,
          "that grip sends no second click and starts no stay", 0, 0);
  }

  // A NEW GRIP after the open starts a new press.
  {
    Press press;
    Close(&press, kModeHold, 1.0);
    Check(Step(&press, false, true, 1.1, kHoldMs) == kClick,
          "a first short grip is a click", 1, 1);
    Check(Step(&press, false, true, 1.2, kHoldMs) == kNothing &&
              Step(&press, true, true, 1.3, kHoldMs) == kNothing,
          "a step with no close does nothing", 0, 0);
    Close(&press, kModeHold, 2.0);
    Check(Step(&press, true, true, 2.2, kHoldMs) == kNothing &&
              Step(&press, true, true, 2.36, kHoldMs) == kStayStart &&
              Step(&press, true, true, 2.5, kHoldMs) == kNothing &&
              Step(&press, false, true, 2.6, kHoldMs) == kStayEnd,
          "the next grip is a hold: the stay starts one time and ends at "
          "the open", 1, 1);
    Near("the time of a press is the time since its close, in ms",
         HeldMs(press, 2.6), 600.0, 1e-6);
  }

  // THE END waits for the Creature, for 5 s at most.
  Check(EndNow(true, 0.0), "a Creature that is ready gets the end at once",
        1, 1);
  Check(!EndNow(false, 0.5) && !EndNow(false, 4.9),
        "a Creature that is not ready makes the end wait", 0, 0);
  Check(EndNow(false, 5.0) && EndNow(false, 60.0),
        "after 5 s the end goes out for a Creature that is not ready", 1, 1);

  // THE CREATURE GOES ON, for the log.
  {
    const float a[2] = {1896.5f, 2520.0f};
    const float b[2] = {1899.5f, 2524.0f};
    Near("the distance of two places on the map", MapDistance(a, b), 5.0,
         1e-3);
    Near("a Creature that did not move has the distance 0", MapDistance(a, a),
         0.0, 1e-6);
    Check(GoesOn(MapDistance(a, b)) && GoesOn(0.5f),
          "a Creature that moved 5 units goes on, and so does one that "
          "moved 0.5 units", 1, 1);
    Check(!GoesOn(0.2f) && !GoesOn(MapDistance(a, a)),
          "a Creature that moved 0.2 units, or not at all, does not", 0, 0);
    Check(!WatchDone(0.2f, 3.0) && WatchDone(0.7f, 3.0) &&
              WatchDone(0.0f, 10.0),
          "the log waits for the Creature to go on, for 10 s at most", 1, 1);
  }
}

}  // namespace stay_checks
