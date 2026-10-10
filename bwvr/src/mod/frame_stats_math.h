// THE NUMBERS OF THE FRAME COST MEASURE (profile=1, frame_profile.cpp, and
// docs/backend.md, "Frame cost"). Pure math: no Windows, no game. The checks
// are in test/controller_math/frame_stats_checks.h.
//
// The measure keeps the time of each frame of a scene, and the time of each
// part of the frame. These functions make the numbers of the summary: the
// mean, the median, the 99th percentile and the worst frame. They also say
// which frames are slow, and which part of a slow frame is the cause.

#pragma once

#include <algorithm>

namespace frame_stats_math {

// The value at a percent of a list that is sorted from small to large, by
// the nearest rank. It is the smallest value that has at least `percent` of
// the list at or below it. 50 is the median, and 100 is the largest value.
inline float Percentile(const float* sorted, int count, float percent) {
  if (count <= 0) return 0.0f;
  int rank = static_cast<int>(percent * 0.01f * static_cast<float>(count));
  if (static_cast<float>(rank) < percent * 0.01f * static_cast<float>(count)) {
    ++rank;
  }
  if (rank < 1) rank = 1;
  if (rank > count) rank = count;
  return sorted[rank - 1];
}

struct Summary {
  float mean;
  float median;
  float p99;
  float worst;
};

// The summary of `count` values. `scratch` has room for `count` values: the
// function sorts a copy there, and the values stay in their order.
inline Summary Summarize(const float* values, int count, float* scratch) {
  Summary s = {0.0f, 0.0f, 0.0f, 0.0f};
  if (count <= 0) return s;
  double sum = 0.0;
  for (int i = 0; i < count; ++i) {
    scratch[i] = values[i];
    sum += values[i];
  }
  std::sort(scratch, scratch + count);
  s.mean = static_cast<float>(sum / count);
  s.median = Percentile(scratch, count, 50.0f);
  s.p99 = Percentile(scratch, count, 99.0f);
  s.worst = scratch[count - 1];
  return s;
}

// A frame is slow when its time is over this limit: one and a half times
// the median frame, and 4 ms over the median at least. The second rule
// keeps the usual spread of a fast frame out of the count.
const float kSlowRatio = 1.5f;
const float kSlowLeastMs = 4.0f;

inline float SlowLimit(float median) {
  const float by_ratio = median * kSlowRatio;
  const float by_ms = median + kSlowLeastMs;
  return by_ratio > by_ms ? by_ratio : by_ms;
}

// The part of one frame that is the most over its median, and how many ms it
// is over (`over`). The parts of a frame do not overlap, so this part is the
// cause of a slow frame. -1 when no part is over its median.
inline int MostOver(const float* parts, const float* medians, int count,
                    float* over) {
  int best = -1;
  float most = 0.0f;
  for (int i = 0; i < count; ++i) {
    const float d = parts[i] - medians[i];
    if (d > most) {
      most = d;
      best = i;
    }
  }
  if (over != nullptr) *over = most;
  return best;
}

}  // namespace frame_stats_math
