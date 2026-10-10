// The smooth zoom of the God view (docs/god-view.md, "The zoom"): the share
// of the top rate that a push of the stick asks for, the time of a frame that
// the zoom uses, and the ratio of the scale for one frame. Pure functions: no
// Windows and no game. The limits of the scale are in GodZoomAbout
// (table_math.h). The checks are in test/controller_math/zoom_checks.h.

#pragma once

#include <math.h>

namespace zoom_math {

// THE PUSH. The value of the stick along the zoom axis, 0 to 1, as a share of
// the top rate: 0 at the dead zone and below it, 1 at full and above it, and a
// straight line between the two. So a small push is a slow zoom.
inline float StickShare(float value, float dead, float full) {
  if (!(value > dead)) return 0.0f;
  if (!(full > dead) || value >= full) return 1.0f;
  return (value - dead) / (full - dead);
}

// THE TIME OF A FRAME that the zoom uses, in seconds: the real time since the
// frame before, and at most cap. So a frame that took long (a load, a stall)
// does not make a jump of the scale. A time that is not above 0 gives 0.
inline float FrameSeconds(double seconds, float cap) {
  if (!(seconds > 0.0)) return 0.0f;
  if (seconds > static_cast<double>(cap)) return cap;
  return static_cast<float>(seconds);
}

// THE RATIO OF ONE FRAME: the new scale over the old one.
//
//     ratio = step ^ (-push * seconds / step_seconds)
//
// push is the share of the top rate, -1 to 1. A push above 0 zooms in: the
// scale (units of the world for each meter of the room) goes down, and the
// world gets larger. step is the ratio of one zoom step (1.5), and
// step_seconds is the time in which a full push changes the scale by one
// step. The change is a ratio and not a difference, so the zoom feels the
// same at each scale. The ratio of a hold is the product of the ratios of its
// frames, so it follows the time held and not the frame rate. A push of 0 or
// a time of 0 gives exactly 1: the scale does not change.
inline float FrameRatio(float push, float step, float step_seconds,
                        float seconds) {
  if (push == 0.0f || !(seconds > 0.0f)) return 1.0f;
  if (!(step > 1.0f) || !(step_seconds > 0.0f)) return 1.0f;
  if (push > 1.0f) push = 1.0f;
  if (push < -1.0f) push = -1.0f;
  return powf(step, -push * seconds / step_seconds);
}

// How many zoom steps a change of the scale is, for the log: 1 for a change
// by the ratio of one step, in or out.
inline float Steps(float from, float to, float step) {
  if (!(from > 0.0f) || !(to > 0.0f) || !(step > 1.0f)) return 0.0f;
  return fabsf(logf(to / from) / logf(step));
}

}  // namespace zoom_math
