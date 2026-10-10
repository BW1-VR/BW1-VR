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

// The direction of a look in the room: yaw degrees to the left and pitch
// degrees up.
inline void Look(float yaw_left, float pitch_up, float out[3]) {
  const float k = 3.14159265f / 180.0f;
  out[0] = -sinf(yaw_left * k) * cosf(pitch_up * k);
  out[1] = sinf(pitch_up * k);
  out[2] = cosf(yaw_left * k) * cosf(pitch_up * k);
}

// THE POINT OF A ZOOM (god_zoom_about, zoom_math.h): the key, the direction
// of the head, the level, the place of the point across the room for each
// value, and the zoom about that point.
inline void ZoomPointChecks() {
  using namespace table_math;
  using namespace zoom_math;
  printf("\nThe point of a zoom of the God view\n");

  // THE KEY.
  Check(About(-3) == 0 && About(0) == 0 && About(1) == 1 && About(2) == 2 &&
            About(9) == 2,
        "a value of the key outside 0 to 2 goes to the nearest value",
        About(9), 2);
  Check(NextAbout(1) == 0 && NextAbout(0) == 2 && NextAbout(2) == 1,
        "the test key goes from 1 to 0 to 2 and back to 1", NextAbout(1), 0);

  // THE DIRECTION OF THE HEAD, with the real head_pose.cpp: a level head, and
  // the look of the fake head, 30 degrees left and 20 degrees down.
  {
    float position[3], orientation[4], matrix[9], lean[3], forward[3];
    HeadPoseSetFake(true, 0.0f, 0.0f, 0.0f);
    HeadPoseFakeEye(0, position, orientation);
    const float middle[3] = {0.0f, 0.0f, 0.0f};
    HeadPoseUpdate(middle, orientation);
    bool ok = HeadPoseCurrent(matrix, lean) && HeadForward(matrix, forward);
    Check(ok && fabsf(forward[0]) < 1e-5f && fabsf(forward[1]) < 1e-5f &&
              fabsf(forward[2] - 1.0f) < 1e-5f,
          "a level head looks along the forward axis of the room",
          forward[2], 1);
    HeadPoseSetFake(true, 30.0f, -20.0f, 0.0f);
    HeadPoseFakeEye(0, position, orientation);
    HeadPoseUpdate(middle, orientation);
    float want[3];
    Look(30.0f, -20.0f, want);
    ok = HeadPoseCurrent(matrix, lean) && HeadForward(matrix, forward);
    Check(ok, "a head that looks left and down has a direction", ok, 1);
    Near("a head 30 degrees left looks to the left", forward[0], want[0],
         1e-5);
    Near("a head 20 degrees down looks down", forward[1], want[1], 1e-5);
    Near("and it still looks forward", forward[2], want[2], 1e-5);
    const float none[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    Check(!HeadForward(none, forward), "a matrix of zeros has no direction",
          HeadForward(none, forward), 0);
    HeadPoseSetFake(true, 0.0f, 0.0f, 0.0f);
    HeadPoseFakeEye(0, position, orientation);
    HeadPoseUpdate(middle, orientation);
  }

  // THE LEVEL: the land under the head, in the range of a ground step.
  Near("the level of a zoom is the height of the land under the head",
       ZoomLevel(-0.60f, -0.05f, 0.10f, 2.50f), -0.60, 1e-6);
  Near("a hill over the head gives a level 10 cm below the head",
       ZoomLevel(1.20f, -0.05f, 0.10f, 2.50f), -0.15, 1e-6);
  Near("a deep valley gives a level 2.5 m below the head",
       ZoomLevel(-9.0f, -0.05f, 0.10f, 2.50f), -2.55, 1e-6);

  // A HILL UNDER THE HEAD AFTER A ZOOM, at 100 units a meter. The waist point
  // has a height of 40 and is 60 cm under the recenter pose, and the head is
  // 5 cm under that pose. So land of a height of 40 is 55 cm below the head.
  Near("land 55 cm below the head keeps the height of the waist point",
       ClearHead(40.0f, 40.0f, -0.60f, 100.0f, -0.05f, 0.10f), 40.0, 1e-4);
  Near("land 10 cm below the head keeps it too",
       ClearHead(40.0f, 85.0f, -0.60f, 100.0f, -0.05f, 0.10f), 40.0, 1e-3);
  Near("land 5 cm below the head goes 5 cm down",
       ClearHead(40.0f, 90.0f, -0.60f, 100.0f, -0.05f, 0.10f), 45.0, 1e-3);
  Near("a hill 25 cm over the head goes 35 cm down",
       ClearHead(40.0f, 120.0f, -0.60f, 100.0f, -0.05f, 0.10f), 75.0, 1e-3);
  Near("and that hill is then 10 cm below the head",
       -0.60 + (120.0 - ClearHead(40.0f, 120.0f, -0.60f, 100.0f, -0.05f,
                                  0.10f)) / 100.0,
       -0.15, 1e-5);

  // THE PLACE ACROSS. The God view of the tests: the ground 60 cm under the
  // recenter pose, the head 30 cm right and 40 cm forward of it and 5 cm
  // lower, a heading of 30 degrees, and flat land at the height of the waist
  // point. The land under the head is 55 cm below it.
  float room[3];
  GodRoom(0.60f, room);
  const float head[3] = {0.30f, -0.05f, 0.40f};
  const float hand[3] = {0.55f, -0.35f, 1.00f};
  const float far_m = 3.0f;
  const float level = -0.60f;
  const float low = 7.5f, high = 4000.0f;
  const float scales[3] = {7.5f, 100.0f, 2000.0f};
  for (int s = 0; s < 3; ++s) {
    TablePose pose;
    pose.center[0] = 2000.0f;
    pose.center[1] = 40.0f;
    pose.center[2] = 2400.0f;
    pose.heading = 30.0f * 3.14159265f / 180.0f;
    pose.scale = scales[s];
    const float scale = scales[s];
    float step = 0.01f * scale;
    if (step < 2.0f) step = 2.0f;
    const auto flat = [](float, float) { return 40.0f; };
    char what[200];
    float down[3], up[3], ahead[3], top[3], out[3];
    Look(30.0f, -20.0f, down);
    Look(30.0f, 20.0f, up);
    Look(0.0f, 0.0f, ahead);
    Look(0.0f, 90.0f, top);

    // 0: the head, whatever the head and the Hand do.
    int kind = PointAcross(kAboutHead, pose, room, head, down, hand, far_m,
                           step, flat, out);
    sprintf_s(what, "at %.1f units a meter, god_zoom_about=0 gives the point "
              "under the head", scale);
    Check(kind == kPointHead && out[0] == head[0] && out[2] == head[2], what,
          Across(head, out), 0);

    // 1, a look down at flat land: the point where the line of the look
    // meets the land, 0.55 / tan(20 degrees) = 1.511 m across from the head.
    kind = PointAcross(kAboutLook, pose, room, head, down, hand, far_m, step,
                       flat, out);
    float plane[3];
    const bool met = RayLevel(head, down, level, far_m, plane);
    sprintf_s(what, "at %.1f a look 20 degrees down finds the land on the "
              "line of the look", scale);
    Check(kind == kPointLook && met, what, kind, kPointLook);
    sprintf_s(what, "at %.1f that land point is 1.511 m across from the "
              "head", scale);
    Near(what, Across(head, out), 0.55 / tan(20.0 * 3.14159265 / 180.0),
         0.002);
    sprintf_s(what, "at %.1f it is the point where the line meets the level "
              "of the land, in cm", scale);
    Near(what, sqrtf((out[0] - plane[0]) * (out[0] - plane[0]) +
                     (out[1] - plane[1]) * (out[1] - plane[1]) +
                     (out[2] - plane[2]) * (out[2] - plane[2])) * 100.0f,
         0.0, 0.2);
    float look_point[3];
    for (int i = 0; i < 3; ++i) look_point[i] = out[i];

    // 1, a step of the land 30 cm high that starts 0.8 m in front of the
    // head: the look finds the step, not the flat land behind it.
    {
      float head_world[3], ahead_room[3], ahead_world[3];
      PoseWorld(pose, room, head, head_world);
      for (int i = 0; i < 3; ++i) ahead_room[i] = head[i];
      ahead_room[0] += down[0] / cosf(20.0f * 3.14159265f / 180.0f);
      ahead_room[2] += down[2] / cosf(20.0f * 3.14159265f / 180.0f);
      PoseWorld(pose, room, ahead_room, ahead_world);
      const float fx = (ahead_world[0] - head_world[0]) / scale;
      const float fz = (ahead_world[2] - head_world[2]) / scale;
      const auto hill = [&](float x, float z) {
        const float along =
            (x - head_world[0]) * fx + (z - head_world[2]) * fz;
        return along >= 0.8f * scale ? 40.0f + 0.3f * scale : 40.0f;
      };
      kind = PointAcross(kAboutLook, pose, room, head, down, hand, far_m,
                         step, hill, out);
      sprintf_s(what, "at %.1f a hill in front of the flat land is the point "
                "of the look, 0.8 m across", scale);
      Check(kind == kPointLook && fabsf(Across(head, out) - 0.8f) < 0.01f,
            what, Across(head, out), 0.8);
    }

    // 1, a look at the sky, at the horizon and straight up: no land on the
    // line, so the point of the line at 3 m.
    kind = PointAcross(kAboutLook, pose, room, head, up, hand, far_m, step,
                       flat, out);
    sprintf_s(what, "at %.1f a look 20 degrees up meets no land, and the "
              "point is 3 m along the line, 2.819 m across", scale);
    Check(kind == kPointLookFar &&
              fabsf(Across(head, out) - 3.0f * cosf(20.0f * 3.14159265f /
                                                    180.0f)) < 1e-4f,
          what, Across(head, out), 2.819);
    kind = PointAcross(kAboutLook, pose, room, head, ahead, hand, far_m, step,
                       flat, out);
    sprintf_s(what, "at %.1f a level look gives the point 3 m in front of "
              "the head", scale);
    Check(kind == kPointLookFar && fabsf(out[0] - head[0]) < 1e-5f &&
              fabsf(out[2] - head[2] - 3.0f) < 1e-5f,
          what, Across(head, out), 3);
    kind = PointAcross(kAboutLook, pose, room, head, top, hand, far_m, step,
                       flat, out);
    sprintf_s(what, "at %.1f a look straight up gives the point under the "
              "head", scale);
    Check(kind == kPointLookFar && Across(head, out) < 1e-5f, what,
          Across(head, out), 0);

    // 1, at the limit: a line that meets the land at 2.99 m and a line that
    // meets it at 3.01 m give points 1 cm apart, so nothing jumps.
    {
      float in[3], past[3], a[3], b[3];
      Look(30.0f, -asinf(0.55f / 2.99f) * 180.0f / 3.14159265f, in);
      Look(30.0f, -asinf(0.55f / 3.01f) * 180.0f / 3.14159265f, past);
      const int k1 = PointAcross(kAboutLook, pose, room, head, in, hand,
                                 far_m, step, flat, a);
      const int k2 = PointAcross(kAboutLook, pose, room, head, past, hand,
                                 far_m, step, flat, b);
      sprintf_s(what, "at %.1f the land at 2.99 m is the point, and the land "
                "at 3.01 m is past the limit", scale);
      Check(k1 == kPointLook && k2 == kPointLookFar, what, k1, kPointLook);
      sprintf_s(what, "at %.1f the two points are 1 cm apart across the "
                "room, in cm", scale);
      Near(what, sqrtf((a[0] - b[0]) * (a[0] - b[0]) +
                       (a[2] - b[2]) * (a[2] - b[2])) * 100.0f, 1.05, 0.3);
    }

    // 1, a head with no direction, and 2 with and without the Hand.
    kind = PointAcross(kAboutLook, pose, room, head, nullptr, hand, far_m,
                       step, flat, out);
    sprintf_s(what, "at %.1f a head with no direction gives the point under "
              "the head", scale);
    Check(kind == kPointLookNone && out[0] == head[0] && out[2] == head[2],
          what, kind, kPointLookNone);
    kind = PointAcross(kAboutHand, pose, room, head, down, hand, far_m, step,
                       flat, out);
    sprintf_s(what, "at %.1f god_zoom_about=2 gives the Hand's point", scale);
    Check(kind == kPointHand && out[0] == hand[0] && out[2] == hand[2], what,
          kind, kPointHand);
    kind = PointAcross(kAboutHand, pose, room, head, down, nullptr, far_m,
                       step, flat, out);
    sprintf_s(what, "at %.1f with no Hand it gives the point under the head",
              scale);
    Check(kind == kPointHandNone && out[0] == head[0] && out[2] == head[2],
          what, kind, kPointHandNone);

    // THE ZOOM ABOUT THE POINT, for the land that the head looks at and for
    // the Hand's point. The point keeps its place in the room through a step
    // in and a step out, the land of the level keeps its height, and the land
    // under the head moves by the share of the distance that the ratio
    // gives: half of it for a step in, a third of it for a step out.
    const float* places[2] = {look_point, hand};
    const char* names[2] = {"the land that the head looks at",
                            "the Hand's point"};
    for (int p = 0; p < 2; ++p) {
      float pivot[3], under[3], at_start[3];
      PivotAt(pose, room, places[p], level, pivot);
      PivotAt(pose, room, head, level, under);
      PoseRoom(pose, room, pivot, at_start);
      const float d = Across(head, places[p]);
      sprintf_s(what, "at %.1f %s is at the level of the zoom, in the room",
                scale, names[p]);
      Near(what, at_start[1], level, 1e-4);
      for (int direction = 0; direction < 2; ++direction) {
        const float ratio = direction == 0 ? 1.0f / 1.5f : 1.5f;
        TablePose next;
        const float used = GodZoomAbout(pose, pivot, ratio, low, high, &next);
        if (used == 1.0f) continue;  // at a limit of the scale
        float at_end[3], under_end[3];
        PoseRoom(next, room, pivot, at_end);
        PoseRoom(next, room, under, under_end);
        float off = 0.0f;
        for (int i = 0; i < 3; ++i) {
          off += (at_end[i] - at_start[i]) * (at_end[i] - at_start[i]);
        }
        sprintf_s(what, "at %.1f a step %s keeps %s at its place in the "
                  "room, in cm", scale, direction == 0 ? "in" : "out",
                  names[p]);
        Near(what, sqrtf(off) * 100.0f, 0.0, 0.01);
        sprintf_s(what, "at %.1f the land under the head moves %s of the "
                  "distance to the point", scale,
                  direction == 0 ? "a half" : "a third");
        Near(what, Across(head, under_end), d * fabsf(1.0f - 1.0f / used),
             0.001);
        sprintf_s(what, "at %.1f the land of the level keeps its height in "
                  "the room", scale);
        Near(what, under_end[1], level, 1e-4);
        sprintf_s(what, "at %.1f the heading stays", scale);
        Check(next.heading == pose.heading, what, next.heading, pose.heading);
        // The step back about the same point gives the view of the start.
        TablePose back;
        GodZoomAbout(next, pivot, 1.0f / used, low, high, &back);
        float back_under[3];
        PoseRoom(back, room, under, back_under);
        sprintf_s(what, "at %.1f the step back about the same point puts the "
                  "land under the head again, in cm", scale);
        Near(what, Across(head, back_under) * 100.0f, 0.0, 0.01);
      }
    }

    // A HOLD KEEPS ITS POINT. The head turns 0.2 degrees on each of 45
    // frames. The point of the start keeps its place in the room. A point
    // that each frame takes again from the head moves with the look.
    {
      float pivot[3], at_start[3], at_end[3];
      PivotAt(pose, room, look_point, level, pivot);
      PoseRoom(pose, room, pivot, at_start);
      TablePose kept = pose;
      float again[3] = {look_point[0], look_point[1], look_point[2]};
      for (int i = 0; i < 45; ++i) {
        const float ratio = FrameRatio(-1.0f, 1.5f, 0.5f, 1.0f / 90.0f);
        TablePose next;
        GodZoomAbout(kept, pivot, ratio, low, high, &next);
        kept = next;
        float turned[3];
        Look(30.0f + 0.2f * (i + 1), -20.0f, turned);
        PointAcross(kAboutLook, kept, room, head, turned, hand, far_m, step,
                    flat, again);
      }
      PoseRoom(kept, room, pivot, at_end);
      float off = 0.0f;
      for (int i = 0; i < 3; ++i) {
        off += (at_end[i] - at_start[i]) * (at_end[i] - at_start[i]);
      }
      sprintf_s(what, "at %.1f the point of a hold keeps its place in the "
                "room while the head turns 9 degrees, in cm", scale);
      Near(what, sqrtf(off) * 100.0f, 0.0, 0.01);
      sprintf_s(what, "at %.1f a point that each frame takes again moves "
                "more than 20 cm with that turn", scale);
      const float moved = sqrtf(
          (again[0] - look_point[0]) * (again[0] - look_point[0]) +
          (again[2] - look_point[2]) * (again[2] - look_point[2]));
      Check(moved > 0.20f, what, moved, 0.24);
    }
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
  ZoomPointChecks();
}

}  // namespace zoom_checks
