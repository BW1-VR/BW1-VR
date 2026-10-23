// THE REFRESH RATE OF THE HEADSET (refresh_rate_hz, xr_screen.cpp, and
// docs/backend.md, "The refresh rate of the headset"). Pure math: no
// Windows, no OpenXR. The checks are in
// test/controller_math/refresh_rate_checks.h.
//
// With XR_FB_display_refresh_rate the runtime gives the rates that the
// headset offers, and the mod can ask for one of them. Choose says which
// rate the value of the ini asks for. The report of each window of frames
// gives the display period that the runtime predicts, the time between the
// display times of two frames, and the time in xrWaitFrame (Window).

#pragma once

#include <stdint.h>

namespace refresh_rate_math {

// The values of refresh_rate_hz.
const int kAskNone = 0;      // the mod asks for no rate
const int kAskHighest = -1;  // the highest rate that the headset offers
const int kAskMost = 1000;

// The ini value in its range: -1, 0, or 1 to kAskMost. A value below -1 is
// 0, so a wrong value asks for no rate.
inline int ClampAsk(int value) {
  if (value < kAskHighest) return kAskNone;
  if (value > kAskMost) return kAskMost;
  return value;
}

// Two rates closer than this are the same rate. A runtime can give 119.99
// for 120.
const float kSameHz = 0.5f;

inline bool SameRate(float a, float b) {
  const float d = a - b;
  return d < kSameHz && d > -kSameHz;
}

// How Choose found its rate.
enum How {
  kNone = 0,    // no rate: the value is 0, or the headset offers no rate
  kExact,       // the headset offers the rate of the value
  kHigher,      // the nearest rate above the value
  kHighest,     // the value is -1
  kBelow,       // no rate is at or above the value, so the highest one
};

struct Choice {
  float hz;  // 0 with kNone
  int how;
};

// The rate that `ask` (a value of ClampAsk) asks for, from `count` rates in
// any order.
inline Choice Choose(const float* rates, int count, int ask) {
  Choice c = {0.0f, kNone};
  if (ask == kAskNone || rates == nullptr || count <= 0) return c;
  float highest = 0.0f;
  float above = 0.0f;  // the lowest rate above the value
  for (int i = 0; i < count; ++i) {
    const float r = rates[i];
    if (r <= 0.0f) continue;
    if (r > highest) highest = r;
    if (ask > 0) {
      const float want = static_cast<float>(ask);
      if (SameRate(r, want)) {
        c.hz = r;
        c.how = kExact;
      } else if (r > want && (above == 0.0f || r < above)) {
        above = r;
      }
    }
  }
  if (highest == 0.0f) return Choice{0.0f, kNone};
  if (c.how == kExact) return c;
  if (ask == kAskHighest) return Choice{highest, kHighest};
  if (above > 0.0f) return Choice{above, kHigher};
  return Choice{highest, kBelow};
}

// THE FRAMES OF A REPORT WINDOW. Add gives one frame of the game: its
// predicted display time and display period (nanoseconds, from
// xrWaitFrame), and the time of its xrWaitFrame in milliseconds.
//
// The step is the time from the display time of the frame before. A step
// of more than kGapNs (a load, a pause, the hold of the last frame) is not
// in the mean. A step of more than one and a half periods is late: the
// headset showed the frame before again for one refresh or more.
const int64_t kGapNs = 250000000;  // 250 ms

struct Window {
  int64_t last_time = 0;  // stays from one window to the next
  uint64_t frames = 0;
  double period_sum_ms = 0.0;
  double period_least_ms = 0.0;
  double period_most_ms = 0.0;
  uint64_t steps = 0;
  double step_sum_ms = 0.0;
  uint64_t late = 0;
  double wait_sum_ms = 0.0;
};

inline void Add(Window* w, int64_t display_time, int64_t period,
                double wait_ms) {
  if (display_time <= 0) return;
  const double period_ms = static_cast<double>(period) / 1e6;
  if (period > 0) {
    if (w->frames == 0 || period_ms < w->period_least_ms) {
      w->period_least_ms = period_ms;
    }
    if (w->frames == 0 || period_ms > w->period_most_ms) {
      w->period_most_ms = period_ms;
    }
    w->period_sum_ms += period_ms;
    ++w->frames;
    w->wait_sum_ms += wait_ms;
  }
  if (w->last_time > 0 && display_time > w->last_time &&
      display_time - w->last_time <= kGapNs) {
    const int64_t step = display_time - w->last_time;
    w->step_sum_ms += static_cast<double>(step) / 1e6;
    ++w->steps;
    if (period > 0 && step > period + period / 2) ++w->late;
  }
  w->last_time = display_time;
}

inline double MeanPeriodMs(const Window& w) {
  return w.frames > 0 ? w.period_sum_ms / static_cast<double>(w.frames) : 0.0;
}

inline double MeanStepMs(const Window& w) {
  return w.steps > 0 ? w.step_sum_ms / static_cast<double>(w.steps) : 0.0;
}

inline double MeanWaitMs(const Window& w) {
  return w.frames > 0 ? w.wait_sum_ms / static_cast<double>(w.frames) : 0.0;
}

// The rate of a time in milliseconds, 0 for no time.
inline double HzOfMs(double ms) { return ms > 0.0 ? 1000.0 / ms : 0.0; }

// A new window. The display time of the last frame stays, so the first
// step of the new window is counted.
inline void Clear(Window* w) {
  const int64_t last = w->last_time;
  *w = Window();
  w->last_time = last;
}

}  // namespace refresh_rate_math
