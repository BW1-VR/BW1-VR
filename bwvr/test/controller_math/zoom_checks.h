// THE SMOOTH ZOOM OF THE GOD VIEW (docs/god-view.md, "The zoom"): the push,
// the time of a frame and the ratio of a frame (zoom_math.h), and the zoom
// about a point with its limits (GodZoomAbout in table_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/table_math.h"
#include "../../src/mod/zoom_math.h"

namespace zoom_checks {

// A hold of the stick: frames of one length at one push, from a pose, about
// the land point under the head. Each frame takes the point again, as the
// mod does. worst_cm gets the largest move of that land point in the room
// over one frame, in cm.
inline void Hold(table_math::TablePose* pose, const float room[3],
                 const float head[3], float land, float push, float step,
                 float step_seconds, float frame, int frames, float low,
                 float high, float* worst_cm) {
  using namespace table_math;
  for (int i = 0; i < frames; ++i) {
    float at[3];
    PoseWorld(*pose, room, head, at);
    const float pivot[3] = {at[0], land, at[2]};
    float before[3], after[3];
    PoseRoom(*pose, room, pivot, before);
    TablePose next;
    GodZoomAbout(*pose, pivot,
                 zoom_math::FrameRatio(push, step, step_seconds, frame), low,
                 high, &next);
    PoseRoom(next, room, pivot, after);
    float off = 0.0f;
    for (int k = 0; k < 3; ++k) {
      off += (after[k] - before[k]) * (after[k] - before[k]);
    }
    const float cm = sqrtf(off) * 100.0f;
    if (worst_cm != nullptr && cm > *worst_cm) *worst_cm = cm;
    *pose = next;
  }
}

inline void ZoomChecks() {
  using namespace table_math;
  using namespace zoom_math;
  printf("\nThe smooth zoom of the God view\n");
  const float dead = 0.3f, full = 0.95f;
  const float step = 1.5f, step_seconds = 0.5f;

  // THE PUSH.
  Check(StickShare(0.0f, dead, full) == 0.0f, "a stick at rest asks for no "
        "zoom", StickShare(0.0f, dead, full), 0);
  Check(StickShare(0.3f, dead, full) == 0.0f, "a stick at the dead zone asks "
        "for no zoom", StickShare(0.3f, dead, full), 0);
  Check(StickShare(0.95f, dead, full) == 1.0f, "a stick at full asks for the "
        "top rate", StickShare(0.95f, dead, full), 1);
  Check(StickShare(1.0f, dead, full) == 1.0f, "a stick past full asks for the "
        "top rate and not more", StickShare(1.0f, dead, full), 1);
  Near("a stick halfway between the dead zone and full asks for half the "
       "rate", StickShare(0.625f, dead, full), 0.5, 1e-5);
  Check(StickShare(0.35f, dead, full) < 0.1f, "a small push is a slow zoom",
        StickShare(0.35f, dead, full), 0.077);
  bool rises = true;
  for (int i = 0; i < 100; ++i) {
    if (StickShare((i + 1) * 0.01f, dead, full) <
        StickShare(i * 0.01f, dead, full)) {
      rises = false;
    }
  }
  Check(rises, "a larger push never asks for a slower zoom", rises, 1);

  // THE TIME OF A FRAME.
  Near("a frame of 11 ms counts 11 ms", FrameSeconds(0.011, 0.05f), 0.011,
       1e-6);
  Near("a frame of 1 s counts as the cap of 50 ms",
       FrameSeconds(1.0, 0.05f), 0.05, 1e-6);
  Check(FrameSeconds(0.0, 0.05f) == 0.0f, "a frame of no time counts 0",
        FrameSeconds(0.0, 0.05f), 0);
  Check(FrameSeconds(-0.2, 0.05f) == 0.0f, "a time that goes back counts 0",
        FrameSeconds(-0.2, 0.05f), 0);

  // THE RATIO OF A FRAME.
  Check(FrameRatio(0.0f, step, step_seconds, 0.011f) == 1.0f,
        "a stick at rest gives a ratio of exactly 1",
        FrameRatio(0.0f, step, step_seconds, 0.011f), 1);
  Check(FrameRatio(1.0f, step, step_seconds, 0.0f) == 1.0f,
        "a frame of no time gives a ratio of exactly 1",
        FrameRatio(1.0f, step, step_seconds, 0.0f), 1);
  Near("a full push forward for 0.5 s divides the scale by 1.5",
       FrameRatio(1.0f, step, step_seconds, 0.5f), 1.0 / 1.5, 1e-5);
  Near("a full push back for 0.5 s multiplies the scale by 1.5",
       FrameRatio(-1.0f, step, step_seconds, 0.5f), 1.5, 1e-5);
  Near("a push past full counts as a full push",
       FrameRatio(3.0f, step, step_seconds, 0.5f), 1.0 / 1.5, 1e-5);
  Near("a half push for 1 s is one step too",
       FrameRatio(-0.5f, step, step_seconds, 1.0f), 1.5, 1e-5);
  // The ratio of a hold is the product of its frames.
  float at_90 = 1.0f, at_30 = 1.0f, back = 1.0f;
  for (int i = 0; i < 45; ++i) {
    at_90 *= FrameRatio(-1.0f, step, step_seconds, 1.0f / 90.0f);
  }
  for (int i = 0; i < 15; ++i) {
    at_30 *= FrameRatio(-1.0f, step, step_seconds, 1.0f / 30.0f);
  }
  Near("45 frames at 90 a second, 0.5 s, are one step", at_90, 1.5, 1e-4);
  Near("15 frames at 30 a second, 0.5 s, are the same step", at_30, at_90,
       1e-4);
  for (int i = 0; i < 45; ++i) {
    back *= FrameRatio(-1.0f, step, step_seconds, 1.0f / 90.0f);
    back *= FrameRatio(1.0f, step, step_seconds, 1.0f / 90.0f);
  }
  Near("a zoom out and a zoom in of the same time give the scale of the "
       "start", back, 1.0, 1e-4);
  // A long frame: the cap of 50 ms holds the change of one frame to 4.2%.
  Near("a frame of 1 s changes the scale by the ratio of 50 ms only",
       FrameRatio(-1.0f, step, step_seconds, FrameSeconds(1.0, 0.05f)),
       pow(1.5, 0.1), 1e-5);
  Near("a change from 100 to 150 units a meter is 1 zoom step",
       Steps(100.0f, 150.0f, step), 1.0, 1e-4);
  Near("a change from 100 to 44.44 units a meter is 2 zoom steps",
       Steps(100.0f, 44.4444f, step), 2.0, 1e-3);

  // THE ZOOM ABOUT A POINT. The God view of the tests: the ground 60 cm
  // under the recenter pose, the head 30 cm right and 20 cm forward of it and
  // 5 cm lower, a heading of 30 degrees, and land at the height of the waist
  // point.
  float room[3];
  GodRoom(0.60f, room);
  const float head[3] = {0.30f, -0.05f, 0.20f};
  const float low = 7.5f, high = 4000.0f;
  const float frame = 1.0f / 90.0f;
  const float scales[3] = {7.5f, 100.0f, 2000.0f};
  for (int s = 0; s < 3; ++s) {
    TablePose start;
    start.center[0] = 2000.0f;
    start.center[1] = 40.0f;
    start.center[2] = 2400.0f;
    start.heading = 30.0f * 3.14159265f / 180.0f;
    start.scale = scales[s];
    const float land = start.center[1];
    float under_start[3];
    PoseWorld(start, room, head, under_start);

    // A zoom step is the zoom about the land point under the head.
    {
      const float pivot[3] = {under_start[0], land, under_start[2]};
      TablePose by_head, by_point;
      const float a = GodZoom(start, room, head, land, 1.5f, low, high,
                              &by_head);
      const float b = GodZoomAbout(start, pivot, 1.5f, low, high, &by_point);
      const bool same = a == b && by_head.scale == by_point.scale &&
                        by_head.center[0] == by_point.center[0] &&
                        by_head.center[1] == by_point.center[1] &&
                        by_head.center[2] == by_point.center[2] &&
                        by_head.heading == by_point.heading;
      char what[128];
      sprintf_s(what, "at %.1f units a meter a zoom step is the zoom about "
                "the land point under the head", scales[s]);
      Check(same, what, same, 1);
    }

    // A hold of 45 frames out, at a full push: one step in 0.5 s, and the
    // land point under the head stays where it is on each frame.
    TablePose pose = start;
    float worst = 0.0f;
    Hold(&pose, room, head, land, -1.0f, step, step_seconds, frame, 45, low,
         high, &worst);
    float under_end[3];
    PoseWorld(pose, room, head, under_end);
    const float want = scales[s] * 1.5f > high ? high : scales[s] * 1.5f;
    char what[160];
    sprintf_s(what, "from %.1f units a meter a full push back for 0.5 s gives "
              "1.5 times the scale", scales[s]);
    Near(what, pose.scale / want, 1.0, 2e-4);
    sprintf_s(what, "from %.1f the land point under the head keeps its room "
              "point on each frame, in cm", scales[s]);
    Near(what, worst, 0.0, 0.01);
    sprintf_s(what, "from %.1f the head stays over the same land, in cm of "
              "the room", scales[s]);
    const float dx = under_end[0] - under_start[0];
    const float dz = under_end[2] - under_start[2];
    Near(what, sqrtf(dx * dx + dz * dz) / pose.scale * 100.0f, 0.0, 0.02);
    sprintf_s(what, "from %.1f the heading stays", scales[s]);
    Check(pose.heading == start.heading, what, pose.heading, start.heading);

    // The stick is back: a frame with no push changes nothing at all.
    const TablePose stopped = pose;
    Hold(&pose, room, head, land, 0.0f, step, step_seconds, frame, 30, low,
         high, nullptr);
    const bool still = pose.scale == stopped.scale &&
                       pose.center[0] == stopped.center[0] &&
                       pose.center[1] == stopped.center[1] &&
                       pose.center[2] == stopped.center[2];
    sprintf_s(what, "from %.1f the view does not change in 30 frames after "
              "the stick is back", scales[s]);
    Check(still, what, still, 1);
  }

  // THE LIMITS. A hold in stops at the life size scale, and a hold out stops
  // at the farthest zoom. The scale never passes a limit, and at the limit
  // the view does not change.
  {
    TablePose pose;
    pose.center[0] = 2000.0f;
    pose.center[1] = 40.0f;
    pose.center[2] = 2400.0f;
    pose.heading = 0.0f;
    pose.scale = 12.0f;
    bool inside = true;
    for (int i = 0; i < 200; ++i) {
      Hold(&pose, room, head, 40.0f, 1.0f, step, step_seconds, frame, 1, low,
           high, nullptr);
      if (pose.scale < low) inside = false;
    }
    Check(inside, "a hold in never goes below the life size scale", inside, 1);
    Check(pose.scale == low, "a hold in stops at the life size scale",
          pose.scale, low);
    const TablePose at_low = pose;
    float at[3];
    PoseWorld(pose, room, head, at);
    const float pivot[3] = {at[0], 40.0f, at[2]};
    TablePose next;
    const float used = GodZoomAbout(
        pose, pivot, FrameRatio(1.0f, step, step_seconds, frame), low, high,
        &next);
    Check(used == 1.0f && next.center[0] == at_low.center[0] &&
              next.center[1] == at_low.center[1] &&
              next.center[2] == at_low.center[2],
          "at the limit a frame of the hold changes nothing", used, 1);

    pose.scale = 3000.0f;
    inside = true;
    for (int i = 0; i < 200; ++i) {
      Hold(&pose, room, head, 40.0f, -1.0f, step, step_seconds, frame, 1, low,
           high, nullptr);
      if (pose.scale > high) inside = false;
    }
    Check(inside, "a hold out never goes past the farthest zoom", inside, 1);
    Check(pose.scale == high, "a hold out stops at the farthest zoom",
          pose.scale, high);
    // Away from the limit the stick zooms again at once.
    Hold(&pose, room, head, 40.0f, 1.0f, step, step_seconds, frame, 1, low,
         high, nullptr);
    Check(pose.scale < high, "a push in at the farthest zoom zooms in at once",
          pose.scale, high);
  }
}

}  // namespace zoom_checks
