// The refresh rate of the headset (refresh_rate_math.h). main.cpp includes
// this file after Check and Near, just before main.
#pragma once

#include "../../src/vr/refresh_rate_math.h"

namespace refresh_rate_checks {

inline void RefreshRateChecks() {
  using namespace refresh_rate_math;
  printf("The refresh rate of the headset\n");

  // The values of the ini.
  Check(ClampAsk(0) == 0, "0 asks for no rate", ClampAsk(0), 0);
  Check(ClampAsk(-1) == -1, "-1 asks for the highest rate", ClampAsk(-1), -1);
  Check(ClampAsk(-5) == 0, "a value below -1 asks for no rate", ClampAsk(-5),
        0);
  Check(ClampAsk(120) == 120, "120 stays 120", ClampAsk(120), 120);
  Check(ClampAsk(5000) == kAskMost, "a large value stops at the most",
        ClampAsk(5000), kAskMost);

  // The rates of a Quest 3, in the order of a runtime.
  const float quest[4] = {72.0f, 90.0f, 120.0f, 80.0f};
  Choice c = Choose(quest, 4, 0);
  Check(c.how == kNone && c.hz == 0.0f, "0 chooses no rate", c.how, kNone);
  c = Choose(quest, 4, 120);
  Check(c.how == kExact, "120 is a rate of the headset", c.how, kExact);
  Near("so 120 Hz", c.hz, 120.0, 0.001);
  c = Choose(quest, 4, 100);
  Check(c.how == kHigher, "100 is not a rate, so the nearest higher one",
        c.how, kHigher);
  Near("100 gives 120 Hz", c.hz, 120.0, 0.001);
  c = Choose(quest, 4, 85);
  Near("85 gives 90 Hz, not 120", c.hz, 90.0, 0.001);
  c = Choose(quest, 4, 60);
  Near("60 gives 72 Hz", c.hz, 72.0, 0.001);
  c = Choose(quest, 4, -1);
  Check(c.how == kHighest, "-1 chooses the highest rate", c.how, kHighest);
  Near("-1 gives 120 Hz", c.hz, 120.0, 0.001);
  c = Choose(quest, 4, 144);
  Check(c.how == kBelow, "no rate is at or above 144", c.how, kBelow);
  Near("so the highest rate, 120 Hz", c.hz, 120.0, 0.001);

  // A runtime that gives a rate that is near a round number.
  const float near_rates[2] = {89.9f, 119.99f};
  c = Choose(near_rates, 2, 120);
  Check(c.how == kExact, "119.99 is the rate 120", c.how, kExact);
  Near("and the rate is the number of the runtime", c.hz, 119.99, 0.001);
  c = Choose(near_rates, 2, 90);
  Check(c.how == kExact, "89.9 is the rate 90", c.how, kExact);

  // No rates.
  c = Choose(quest, 0, 120);
  Check(c.how == kNone, "no rates choose no rate", c.how, kNone);
  c = Choose(nullptr, 4, -1);
  Check(c.how == kNone, "no list chooses no rate", c.how, kNone);
  const float zero[2] = {0.0f, 0.0f};
  c = Choose(zero, 2, -1);
  Check(c.how == kNone, "rates of 0 choose no rate", c.how, kNone);

  // A window of frames: a period of 8.33 ms, and display times 10 ms apart.
  // That is a runtime that gives a frame each 10 ms on a display of 120 Hz.
  Window w;
  const int64_t period = 8333333;
  int64_t t = 1000000000;
  for (int i = 0; i < 100; ++i) {
    Add(&w, t, period, 4.0);
    t += 10000000;
  }
  Check(w.frames == 100, "100 frames", static_cast<double>(w.frames), 100);
  Check(w.steps == 99, "99 steps between them", static_cast<double>(w.steps),
        99);
  Near("the mean period is 8.33 ms", MeanPeriodMs(w), 8.333, 0.001);
  Near("the mean step is 10 ms", MeanStepMs(w), 10.0, 0.001);
  Near("the frames came at 100 Hz", HzOfMs(MeanStepMs(w)), 100.0, 0.01);
  Near("the display has 120 Hz", HzOfMs(MeanPeriodMs(w)), 120.0, 0.01);
  Near("the mean wait is 4 ms", MeanWaitMs(w), 4.0, 0.001);
  Check(w.late == 0, "a step of 10 ms is not late (1.5 periods is 12.5 ms)",
        static_cast<double>(w.late), 0);

  // A new window keeps the last display time, so its first step counts.
  Clear(&w);
  Check(w.frames == 0 && w.steps == 0, "a new window has no frames", 0, 0);
  Add(&w, t, period, 2.0);
  Check(w.steps == 1, "the first frame of a new window has a step",
        static_cast<double>(w.steps), 1);
  Near("of 10 ms", MeanStepMs(w), 10.0, 0.001);

  // A step of two periods is late. A step of more than 250 ms is a gap.
  t += 2 * period;
  Add(&w, t, period, 2.0);
  Check(w.late == 1, "a step of two periods is late",
        static_cast<double>(w.late), 1);
  t += 400000000;
  Add(&w, t, period, 2.0);
  Check(w.steps == 2, "a gap of 400 ms is not a step",
        static_cast<double>(w.steps), 2);
  Check(w.frames == 3, "but its frame counts", static_cast<double>(w.frames),
        3);

  // The least and the most period of a window.
  Window v;
  Add(&v, 100, 11111111, 1.0);
  Add(&v, 200, 8333333, 1.0);
  Near("the least period is 8.33 ms", v.period_least_ms, 8.333, 0.001);
  Near("the most period is 11.11 ms", v.period_most_ms, 11.111, 0.001);

  // A frame with no display time does not count.
  Window none;
  Add(&none, 0, period, 3.0);
  Check(none.frames == 0, "a frame with no display time does not count",
        static_cast<double>(none.frames), 0);
  Near("no frames give a mean of 0", MeanPeriodMs(none), 0.0, 0.001);
  Near("no time gives 0 Hz", HzOfMs(0.0), 0.0, 0.001);
}

}  // namespace refresh_rate_checks
