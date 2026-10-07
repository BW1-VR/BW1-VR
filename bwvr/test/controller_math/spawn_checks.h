// THE PLACE OF THE GOD VIEW AT A CAMERA OF THE GAME (docs/god-view.md,
// section 15): GodSpawn and HeadYaw of table_math.h.
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/table_math.h"

namespace spawn_checks {

// A head in the room: a yaw to the right and a pitch down, in degrees. The
// columns are its right, its up and its forward.
inline void HeadMatrix(float yaw_degrees, float down_degrees, float out[9]) {
  const float k = 3.14159265f / 180.0f;
  const float c = cosf(yaw_degrees * k), s = sinf(yaw_degrees * k);
  const float cp = cosf(down_degrees * k), sp = sinf(down_degrees * k);
  const float right[3] = {c, 0.0f, -s};
  const float forward[3] = {s * cp, -sp, c * cp};
  const float up[3] = {s * sp, cp, c * sp};
  for (int i = 0; i < 3; ++i) {
    out[i * 3 + 0] = right[i];
    out[i * 3 + 1] = up[i];
    out[i * 3 + 2] = forward[i];
  }
}

inline void SpawnChecks() {
  using namespace table_math;
  printf("\nThe place of the God view at a camera of the game\n");
  const float k = 3.14159265f / 180.0f;

  // THE YAW OF THE HEAD.
  {
    float head[9], yaw = 9.0f;
    HeadMatrix(0.0f, 0.0f, head);
    Check(HeadYaw(head, &yaw), "a level head has a yaw", 1, 1);
    Near("a head that looks along the room forward has a yaw of 0",
         yaw / k, 0.0, 0.001);
    HeadMatrix(30.0f, 0.0f, head);
    HeadYaw(head, &yaw);
    Near("a head that turned 30 degrees right has a yaw of 30", yaw / k, 30.0,
         0.001);
    HeadMatrix(-50.0f, 60.0f, head);
    HeadYaw(head, &yaw);
    Near("a head that looks 60 degrees down keeps its yaw of -50", yaw / k,
         -50.0, 0.001);
    HeadMatrix(170.0f, 20.0f, head);
    HeadYaw(head, &yaw);
    Near("a head that looks back has a yaw of 170", yaw / k, 170.0, 0.001);
    // A head that lies on its side: its right axis points up.
    const float side[9] = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    Check(!HeadYaw(side, &yaw), "a head on its side has no yaw", 0, 0);
  }

  // THE LAND POINT THAT A CAMERA LOOKS AT.
  {
    const auto flat = [](float, float) { return 10.0f; };
    const float camera[3] = {0.0f, 60.0f, 0.0f};
    float look[3];
    const float on_land[3] = {100.0f, 10.0f, 0.0f};
    CameraLookPoint(camera, on_land, 1.0f, flat, look);
    Near("a focus on the land is the look point (x)", look[0], 100.0, 0.01);
    Near("a focus on the land is the look point (y)", look[1], 10.0, 0.01);
    const float under[3] = {200.0f, -40.0f, 0.0f};
    CameraLookPoint(camera, under, 1.0f, flat, look);
    Near("a focus under the land gives the point where the line meets the "
         "land (x)", look[0], 100.0, 0.01);
    Near("and that point is on the land (y)", look[1], 10.0, 0.01);
    const float in_air[3] = {50.0f, 35.0f, 0.0f};
    CameraLookPoint(camera, in_air, 1.0f, flat, look);
    Near("a focus in the air gives the land under the focus (x)", look[0],
         50.0, 0.01);
    Near("and its land height (y)", look[1], 10.0, 0.01);
    // A wall of land between the camera and its focus.
    const auto wall = [](float x, float) {
      return x > 40.0f && x < 60.0f ? 90.0f : 10.0f;
    };
    CameraLookPoint(camera, on_land, 1.0f, wall, look);
    Near("a hill before the focus gives the point on the hill (x)", look[0],
         40.0, 0.01);
    Near("with the height of the hill (y)", look[1], 90.0, 0.01);
    // A camera under the land has no line.
    const float buried[3] = {0.0f, 5.0f, 0.0f};
    CameraLookPoint(buried, on_land, 1.0f, flat, look);
    Near("a camera under the land gives the land under the focus", look[0],
         100.0, 0.01);
    // The two poses of a story camera that the player's mode brings to rest:
    // the focus under a slope, and the focus on the slope, on one line.
    const auto slope = [](float x, float) { return 0.5f * x; };
    const float eye[3] = {0.0f, 30.0f, 0.0f};
    const float deep[3] = {100.0f, -10.0f, 0.0f};
    const float hit[3] = {100.0f / 3.0f, 50.0f / 3.0f, 0.0f};
    float look_deep[3], look_hit[3];
    CameraLookPoint(eye, deep, 1.0f, slope, look_deep);
    CameraLookPoint(eye, hit, 1.0f, slope, look_hit);
    Near("a focus under a slope gives the point on the slope (x)",
         look_deep[0], 100.0 / 3.0, 0.01);
    Near("the focus on the slope gives the same look point (x)",
         look_hit[0] - look_deep[0], 0.0, 0.01);
    Near("the focus on the slope gives the same look point (y)",
         look_hit[1] - look_deep[1], 0.0, 0.01);
    // A camera that looks at its own position has no line.
    CameraLookPoint(eye, eye, 1.0f, slope, look);
    Near("a camera with no line gives the land under it", look[1], 0.0, 0.01);
  }

  const float scales[3] = {7.5f, 100.0f, 2000.0f};
  for (int n = 0; n < 3; ++n) {
    const float s = scales[n];
    printf("  at %.1f units per meter\n", s);
    float room[3];
    GodRoom(0.60f, room);
    const float look[3] = {2238.0f, 16.0f, 3156.0f};
    const float camera_heading = 54.8f * k;
    const float ahead = 0.50f;

    // A head at the recenter pose that looks along the room forward.
    {
      const float head[3] = {0.0f, 0.0f, 0.0f};
      TablePose to;
      GodSpawn(look, camera_heading, s, room, head, 0.0f, ahead, room[1], &to);
      float at[3];
      PoseRoom(to, room, look, at);
      Near("the look point is on the middle line of the room", at[0], 0.0,
           0.0005);
      Near("the look point is 50 cm in front of the head", at[2], ahead,
           0.0005);
      Near("the land at the look point is at the waist", at[1], room[1],
           0.0005);
      Near("the room forward has the heading of the camera",
           to.heading / k, 54.8, 0.001);
      Near("the scale does not change", to.scale, s, 0.0);
      float head_world[3];
      PoseWorld(to, room, head, head_world);
      Near("the head is behind the look point, along the camera (x)",
           head_world[0], look[0] - ahead * s * sinf(camera_heading),
           0.002 * s + 0.001);
      Near("the head is behind the look point, along the camera (z)",
           head_world[2], look[2] - ahead * s * cosf(camera_heading),
           0.002 * s + 0.001);
    }

    // A head that stands off the recenter pose and looks to the side.
    {
      const float head[3] = {0.30f, 0.10f, -0.20f};
      const float yaw = 40.0f * k;
      const float look_y = head[1] - 0.45f;
      TablePose to;
      GodSpawn(look, camera_heading, s, room, head, yaw, ahead, look_y, &to);
      float at[3];
      PoseRoom(to, room, look, at);
      Near("the look point is in front of the head that turned (x)", at[0],
           head[0] + ahead * sinf(yaw), 0.0005);
      Near("the look point is in front of the head that turned (z)", at[2],
           head[2] + ahead * cosf(yaw), 0.0005);
      Near("the land at the look point has the height that was asked",
           at[1], look_y, 0.0005);
      // The flat forward of the head in the world.
      const float tip[3] = {head[0] + sinf(yaw), head[1], head[2] + cosf(yaw)};
      float from[3], to_world[3], heading = 0.0f;
      PoseWorld(to, room, head, from);
      PoseWorld(to, room, tip, to_world);
      Check(Heading(from, to_world, &heading), "the head has a heading", 1, 1);
      Near("the forward of the head has the heading of the camera",
           Wrap(heading - camera_heading) / k, 0.0, 0.01);
      Near("the room forward is 40 degrees to the left of the camera",
           Wrap(to.heading - camera_heading) / k, -40.0, 0.001);
      Near("the scale does not change", to.scale, s, 0.0);
    }

    // The same camera two times gives the same pose.
    {
      const float head[3] = {-0.15f, 0.05f, 0.25f};
      TablePose a, b;
      GodSpawn(look, camera_heading, s, room, head, -25.0f * k, ahead, -0.5f,
               &a);
      GodSpawn(look, camera_heading, s, room, head, -25.0f * k, ahead, -0.5f,
               &b);
      Near("a second place at the same camera moves nothing",
           fabs(a.center[0] - b.center[0]) + fabs(a.center[1] - b.center[1]) +
               fabs(a.center[2] - b.center[2]) + fabs(a.heading - b.heading),
           0.0, 0.0);
    }

    // The rule before: the look point under the recenter pose.
    {
      const float head[3] = {0.0f, 0.0f, 0.0f};
      const float lift = 0.10f;
      TablePose to;
      GodSpawn(look, camera_heading, s, room, head, 0.0f, 0.0f, room[1] + lift,
               &to);
      Near("the rule before puts the look point at the waist point (x)",
           to.center[0], look[0], 0.0005);
      Near("the rule before puts the look point at the waist point (z)",
           to.center[2], look[2], 0.0005);
      Near("and the land there keeps its lift over the waist", to.center[1],
           look[1] - lift * s, 0.0005 * s + 0.0005);
      Near("with the heading of the camera", to.heading / k, 54.8, 0.001);
    }

    // A camera that looks the other way puts the player on the other side.
    {
      const float head[3] = {0.0f, 0.0f, 0.0f};
      TablePose a, b;
      GodSpawn(look, 0.0f, s, room, head, 0.0f, ahead, room[1], &a);
      GodSpawn(look, 180.0f * k, s, room, head, 0.0f, ahead, room[1], &b);
      float head_a[3], head_b[3];
      PoseWorld(a, room, head, head_a);
      PoseWorld(b, room, head, head_b);
      Near("a camera that looks north puts the head south of the look point",
           head_a[2], look[2] - ahead * s, 0.002 * s + 0.001);
      Near("a camera that looks south puts the head north of it", head_b[2],
           look[2] + ahead * s, 0.002 * s + 0.001);
    }
  }
}

}  // namespace spawn_checks
