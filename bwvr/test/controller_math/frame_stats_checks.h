// The numbers of the frame cost measure (frame_stats_math.h). main.cpp
// includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/frame_stats_math.h"

namespace frame_stats_checks {

inline void FrameStatsChecks() {
  using namespace frame_stats_math;
  printf("The numbers of the frame cost measure\n");

  // The nearest rank of a sorted list of 1 to 100.
  float hundred[100];
  for (int i = 0; i < 100; ++i) hundred[i] = static_cast<float>(i + 1);
  Near("the median of 1 to 100 is 50", Percentile(hundred, 100, 50.0f), 50,
       0.001);
  Near("the 99th percentile of 1 to 100 is 99",
       Percentile(hundred, 100, 99.0f), 99, 0.001);
  Near("the 100th percentile is the largest value",
       Percentile(hundred, 100, 100.0f), 100, 0.001);
  Near("a percent of 0 gives the smallest value",
       Percentile(hundred, 100, 0.0f), 1, 0.001);
  // 600 frames: the 99th percentile has 6 frames above it.
  float six[600];
  for (int i = 0; i < 600; ++i) six[i] = static_cast<float>(i + 1);
  Near("the 99th percentile of 600 frames is frame 594",
       Percentile(six, 600, 99.0f), 594, 0.001);
  // An odd count, and a list of one value.
  const float five[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  Near("the median of five values is the third", Percentile(five, 5, 50.0f),
       3, 0.001);
  const float one[1] = {7.5f};
  Near("one value is each percentile", Percentile(one, 1, 99.0f), 7.5, 0.001);
  Near("no values give 0", Percentile(one, 0, 50.0f), 0, 0.001);

  // The summary sorts a copy. The values keep their order.
  float frames[10] = {16.0f, 17.0f, 16.5f, 41.0f, 16.2f,
                      16.8f, 16.1f, 16.9f, 16.4f, 16.6f};
  float scratch[10];
  const Summary s = Summarize(frames, 10, scratch);
  Near("the mean of ten frames", s.mean, 18.95, 0.001);
  Near("the median of ten frames", s.median, 16.5, 0.001);
  Near("the 99th percentile of ten frames is the worst one", s.p99, 41, 0.001);
  Near("the worst of ten frames", s.worst, 41, 0.001);
  Near("the values keep their order", frames[3], 41, 0.001);
  const Summary none = Summarize(frames, 0, scratch);
  Near("no frames give a mean of 0", none.mean, 0, 0.001);

  // A slow frame: over 1.5 times the median, and 4 ms over it at least.
  Near("at a median of 16.5 ms the limit is 24.75 ms", SlowLimit(16.5f), 24.75,
       0.001);
  Near("at a median of 5 ms the limit is 9 ms", SlowLimit(5.0f), 9, 0.001);
  Near("at a median of 8 ms the two rules meet", SlowLimit(8.0f), 12, 0.001);

  // The cause of a slow frame is the part that is the most over its median.
  const float medians[4] = {0.1f, 4.0f, 4.0f, 8.0f};
  const float turn[4] = {21.0f, 4.5f, 4.1f, 8.2f};
  float over = 0.0f;
  Check(MostOver(turn, medians, 4, &over) == 0,
        "a frame with a long game turn names the game turn", 0, 0);
  Near("the game turn is 20.9 ms over its median", over, 20.9, 0.001);
  const float walk[4] = {0.1f, 9.0f, 4.2f, 8.1f};
  Check(MostOver(walk, medians, 4, &over) == 1,
        "a frame with a long eye pass names that pass", 0, 0);
  Near("the eye pass is 5 ms over its median", over, 5, 0.001);
  const float usual[4] = {0.1f, 3.9f, 4.0f, 7.5f};
  Check(MostOver(usual, medians, 4, &over) == -1,
        "a frame with no part over its median names no part", 0, 0);
  Near("and it is 0 ms over", over, 0, 0.001);
}

}  // namespace frame_stats_checks
