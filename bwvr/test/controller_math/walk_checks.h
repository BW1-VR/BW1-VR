// THE WALK WITH THE STICK (docs/god-view.md, "The walk with the stick"): the
// key, the push, the forward direction of the head and of the controller,
// the speed in the room, the step of a frame and the view after a step
// (walk_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/table_math.h"
#include "../../src/mod/walk_math.h"
#include "../../src/mod/zoom_math.h"

namespace walk_checks {

// The matrix of a head in the room (the columns are its right, its up and its
// forward): yaw degrees to the right, then pitch degrees down, then roll
// degrees to the right.
inline void Head(float yaw_right, float pitch_down, float roll_right,
                 float out[9]) {
  const float k = 3.14159265f / 180.0f;
  const float cy = cosf(yaw_right * k), sy = sinf(yaw_right * k);
  const float cp = cosf(pitch_down * k), sp = sinf(pitch_down * k);
  const float cr = cosf(roll_right * k), sr = sinf(roll_right * k);
  const float right0[3] = {cy, 0.0f, -sy};
  const float up0[3] = {sy * sp, cp, cy * sp};
  const float forward[3] = {sy * cp, -sp, cy * cp};
  for (int i = 0; i < 3; ++i) {
    // A roll to the right puts the right axis down.
    out[i * 3 + 0] = cr * right0[i] - sr * up0[i];
    out[i * 3 + 1] = sr * right0[i] + cr * up0[i];
    out[i * 3 + 2] = forward[i];
  }
}

inline void WalkChecks() {
  using namespace table_math;
  using namespace walk_math;
  printf("\nThe walk with the stick\n");

  // THE KEY.
  Check(Rule(-2) == kRuleOff && Rule(0) == kRuleOff && Rule(1) == kRuleHead &&
            Rule(2) == kRuleController && Rule(9) == kRuleController,
        "move_stick is 0, 1 or 2", Rule(9), kRuleController);
  Check(NextRule(1) == 2 && NextRule(2) == 0 && NextRule(0) == 1,
        "the test key goes from 1 to 2 to 0 and back to 1", NextRule(2), 0);

  // THE PUSH: the dead zone of the smooth zoom (0.3) and its full push
  // (0.95), along the length of the stick.
  const float dead = 0.3f, full = 0.95f;
  float push[2];
  Near("a stick at rest gives no push", Push(0.0f, 0.0f, dead, full, push),
       0.0, 1e-6);
  Near("a push of 0.3 forward is in the dead zone",
       Push(0.0f, 0.3f, dead, full, push), 0.0, 1e-6);
  Near("and it gives no vector", fabsf(push[0]) + fabsf(push[1]), 0.0, 1e-6);
  Near("a push of 0.625 forward is half of the top speed",
       Push(0.0f, 0.625f, dead, full, push), 0.5, 1e-5);
  Near("and it points forward", push[1], 0.5, 1e-5);
  Near("a full push forward is the top speed",
       Push(0.0f, 1.0f, dead, full, push), 1.0, 1e-6);
  Push(0.0f, -1.0f, dead, full, push);
  Near("a full push back points back", push[1], -1.0, 1e-6);
  Push(-1.0f, 0.0f, dead, full, push);
  Near("a full push to the left points left", push[0], -1.0, 1e-6);
  // A diagonal of the scripted stick: both axes at full.
  Near("a diagonal is not faster than a straight push",
       Push(1.0f, 1.0f, dead, full, push), 1.0, 1e-6);
  Near("a diagonal keeps its direction, x", push[0], 0.70711, 1e-4);
  Near("a diagonal keeps its direction, y", push[1], 0.70711, 1e-4);
  // A push of 0.2 on each axis is 0.283 long: in the dead zone, although no
  // axis is at rest.
  Near("a small diagonal is in the dead zone",
       Push(0.2f, 0.2f, dead, full, push), 0.0, 1e-6);
  const float nan = nanf("");
  Near("a value that is no number gives no push",
       Push(nan, 1.0f, dead, full, push), 0.0, 1e-6);

  // FORWARD OF THE HEAD.
  float head[9], forward[2];
  Head(0.0f, 0.0f, 0.0f, head);
  Check(HeadForward(head, forward), "a level head has a direction", 1, 1);
  Near("a head that looks straight ahead: x", forward[0], 0.0, 1e-5);
  Near("a head that looks straight ahead: z", forward[1], 1.0, 1e-5);
  Head(-30.0f, 0.0f, 0.0f, head);
  HeadForward(head, forward);
  Near("a head 30 degrees to the left: degrees", DegreesRight(forward), -30.0,
       1e-3);
  // The word "look" of the scripted controller: 30 degrees left, 20 down.
  Head(-30.0f, 20.0f, 0.0f, head);
  HeadForward(head, forward);
  Near("a look down does not change the direction", DegreesRight(forward),
       -30.0, 1e-3);
  Near("the direction is a unit vector",
       sqrtf(forward[0] * forward[0] + forward[1] * forward[1]), 1.0, 1e-5);
  Head(40.0f, 90.0f, 0.0f, head);
  Check(HeadForward(head, forward),
        "a head that looks straight down has a direction", 1, 1);
  Near("it is the direction of the top of the head", DegreesRight(forward),
       40.0, 1e-3);
  Head(0.0f, 0.0f, 90.0f, head);
  Check(!HeadForward(head, forward), "a head on its side has no direction", 0,
        0);

  // FORWARD OF A CONTROLLER.
  const float k = 3.14159265f / 180.0f;
  const float ahead[3] = {0.0f, 0.0f, 1.0f};
  Check(AimForward(ahead, 0.2f, forward), "a level controller has a direction",
        1, 1);
  Near("a controller that points ahead", DegreesRight(forward), 0.0, 1e-4);
  // 30 degrees to the right and 30 degrees down, as the left hand of the
  // scripted controller with fake_controller_yaw=30.
  const float aim[3] = {sinf(30.0f * k) * cosf(30.0f * k), -sinf(30.0f * k),
                        cosf(30.0f * k) * cosf(30.0f * k)};
  Check(AimForward(aim, 0.2f, forward), "a controller 30 degrees down", 1, 1);
  Near("points 30 degrees to the right on the land", DegreesRight(forward),
       30.0, 1e-3);
  Near("with a unit direction",
       sqrtf(forward[0] * forward[0] + forward[1] * forward[1]), 1.0, 1e-5);
  const float down[3] = {0.0f, -1.0f, 0.0f};
  forward[0] = forward[1] = 7.0f;
  Check(!AimForward(down, 0.2f, forward),
        "a controller that points straight down has no direction", 0, 0);
  Check(forward[0] == 7.0f && forward[1] == 7.0f,
        "and the direction of before is not written", 1, 1);
  // 80 degrees down: the flat part is 0.17 long.
  const float steep[3] = {0.0f, -sinf(80.0f * k), cosf(80.0f * k)};
  Check(!AimForward(steep, 0.2f, forward),
        "a controller 80 degrees down has no direction", 0, 0);
  const float nearly[3] = {0.0f, -sinf(75.0f * k), cosf(75.0f * k)};
  Check(AimForward(nearly, 0.2f, forward),
        "a controller 75 degrees down has a direction", 1, 1);

  // THE SPEED IN THE ROOM. The top speed is 1.5 m for each second.
  const float top = 1.5f;
  float speed[2];
  const float straight[2] = {0.0f, 1.0f};
  const float stick_forward[2] = {0.0f, 1.0f};
  const float stick_back[2] = {0.0f, -1.0f};
  const float stick_right[2] = {1.0f, 0.0f};
  const float stick_half[2] = {0.0f, 0.5f};
  Speed(straight, stick_forward, top, speed);
  Near("forward on the stick moves forward", speed[1], 1.5, 1e-6);
  Near("and not to the side", speed[0], 0.0, 1e-6);
  Speed(straight, stick_back, top, speed);
  Near("back on the stick moves back", speed[1], -1.5, 1e-6);
  Speed(straight, stick_right, top, speed);
  Near("right on the stick moves to the right", speed[0], 1.5, 1e-6);
  Near("and not forward", speed[1], 0.0, 1e-6);
  Speed(straight, stick_half, top, speed);
  Near("half of a push is half of the speed", speed[1], 0.75, 1e-6);
  const float left30[2] = {-sinf(30.0f * k), cosf(30.0f * k)};
  Speed(left30, stick_forward, top, speed);
  Near("with forward 30 degrees to the left, the move is 30 degrees left",
       DegreesRight(speed), -30.0, 1e-3);
  Near("at the top speed", sqrtf(speed[0] * speed[0] + speed[1] * speed[1]),
       1.5, 1e-5);
  Speed(left30, stick_right, top, speed);
  Near("and the stick to the right moves 90 degrees to the right of that",
       DegreesRight(speed), 60.0, 1e-3);
  const float right30[2] = {sinf(30.0f * k), cosf(30.0f * k)};
  Speed(right30, stick_forward, top, speed);
  Near("with forward 30 degrees to the right, the move is 30 degrees right",
       DegreesRight(speed), 30.0, 1e-3);

  // THE STEP OF A FRAME, and a hold of one second at 90 and at 45 frames a
  // second: the distance follows the time and not the frame rate.
  float step[2];
  Speed(straight, stick_forward, top, speed);
  Step(speed, 0.0f, step);
  Near("a frame with no time gives no step", fabsf(step[0]) + fabsf(step[1]),
       0.0, 1e-9);
  Step(speed, 1.0f / 90.0f, step);
  Near("a frame of 11 ms at 1.5 m/s is 1.67 cm", step[1] * 100.0, 1.6667,
       1e-3);
  float far90 = 0.0f, far45 = 0.0f;
  for (int i = 0; i < 90; ++i) {
    Step(speed, zoom_math::FrameSeconds(1.0 / 90.0, 0.05f), step);
    far90 += step[1];
  }
  for (int i = 0; i < 45; ++i) {
    Step(speed, zoom_math::FrameSeconds(1.0 / 45.0, 0.05f), step);
    far45 += step[1];
  }
  Near("one second at 90 frames a second is 1.5 m", far90, 1.5, 1e-4);
  Near("one second at 45 frames a second is 1.5 m", far45, 1.5, 1e-4);
  Step(speed, zoom_math::FrameSeconds(0.5, 0.05f), step);
  Near("a frame of 0.5 s moves as a frame of 50 ms: 7.5 cm", step[1] * 100.0,
       7.5, 1e-4);
  // The stick comes back: the frame has no push, so it has no step.
  const float rest[2] = {0.0f, 0.0f};
  Speed(straight, rest, top, speed);
  Step(speed, 1.0f / 90.0f, step);
  Near("a stick that is back gives no step in that frame",
       fabsf(step[0]) + fabsf(step[1]), 0.0, 1e-9);

  // THE VIEW AFTER A STEP. The God view at 100 units for each meter, the
  // waist point 60 cm below the recenter pose.
  float room[3];
  GodRoom(0.6f, room);
  TablePose pose;
  pose.center[0] = 2000.0f;
  pose.center[1] = 30.0f;
  pose.center[2] = 2500.0f;
  pose.heading = 0.0f;
  pose.scale = 100.0f;
  const float ten_cm[2] = {0.0f, 0.1f};
  TablePose next;
  Walk(pose, ten_cm, &next);
  Near("10 cm forward at 100 units a meter is 10 units: z", next.center[2],
       2510.0, 1e-3);
  Near("and x stays", next.center[0], 2000.0, 1e-3);
  Check(next.center[1] == pose.center[1] && next.heading == pose.heading &&
            next.scale == pose.scale,
        "the height, the heading and the scale stay", 1, 1);
  // The land point that was 10 cm in front of the head is under the head.
  const float head_room[3] = {0.05f, 0.0f, -0.02f};
  const float ahead_room[3] = {0.05f, 0.0f, 0.08f};
  float was[3], is[3];
  PoseWorld(pose, room, ahead_room, was);
  PoseWorld(next, room, head_room, is);
  Near("the land that was 10 cm ahead is under the head: x", is[0], was[0],
       1e-3);
  Near("the land that was 10 cm ahead is under the head: z", is[2], was[2],
       1e-3);
  // A heading of 90 degrees: the room forward is world +x.
  pose.heading = 90.0f * k;
  Walk(pose, ten_cm, &next);
  Near("with a heading of 90 degrees, forward is world +x", next.center[0],
       2010.0, 1e-3);
  Near("and z stays", next.center[2], 2500.0, 1e-3);
  // The speed is a speed of the room: the same step is more land far out.
  pose.heading = 0.0f;
  pose.scale = 4000.0f;
  Walk(pose, ten_cm, &next);
  Near("10 cm at 4000 units a meter is 400 units", next.center[2] - 2500.0f,
       400.0, 1e-2);
  pose.scale = 7.5f;
  Walk(pose, ten_cm, &next);
  Near("10 cm at the life size scale is 0.75 units", next.center[2] - 2500.0f,
       0.75, 1e-4);

  // THE SAME MOTION AS A PULL OF THE WORLD: a grab of one hand that pulls
  // the land 10 cm back and 4 cm to the left gives the same view as a step
  // of 10 cm forward and 4 cm to the right.
  pose.heading = 35.0f * k;
  pose.scale = 66.667f;
  TableGrab grab;
  memset(&grab, 0, sizeof(grab));
  grab.hands = 1;
  grab.pose = pose;
  grab.middle[0] = -0.2f;
  grab.middle[1] = -0.3f;
  grab.middle[2] = 0.35f;
  const float hand_now[3] = {-0.2f - 0.04f, -0.3f, 0.35f - 0.1f};
  const float no_spread[3] = {0.0f, 0.0f, 0.0f};
  float theta = 0.0f, zoom = 1.0f;
  TablePose pulled;
  TableGrabSolve(grab, room, hand_now, no_spread, false, false, 0.08f, 7.5f,
                 4000.0f, &theta, &zoom, &pulled);
  const float walk_step[2] = {0.04f, 0.1f};
  Walk(pose, walk_step, &next);
  Near("a step is the pan of a grab: x", next.center[0], pulled.center[0],
       1e-3);
  Near("a step is the pan of a grab: z", next.center[2], pulled.center[2],
       1e-3);
  float back[2];
  InRoom(pose, next.center[0] - pose.center[0],
         next.center[2] - pose.center[2], back);
  Near("the move in the room from the move in the world: x", back[0], 0.04,
       1e-5);
  Near("the move in the room from the move in the world: z", back[1], 0.1,
       1e-5);

  // A HILL AT THE NEW PLACE (zoom_math::ClearHead). The head is 5 cm above
  // the recenter pose, and the land at the new place is 20 units higher than
  // the waist point: 30 cm at this scale, so 25 cm below the head.
  const float head_y = 0.05f;
  Near("a low hill at the new place leaves the land where it is",
       zoom_math::ClearHead(next.center[1], next.center[1] + 20.0f, room[1],
                            next.scale, head_y, 0.10f),
       next.center[1], 1e-4);
  // A hill of 40 units is 60 cm: 5 cm above the head. The land goes 15 cm
  // down, so the hill is 10 cm below the head.
  const float cleared = zoom_math::ClearHead(
      next.center[1], next.center[1] + 40.0f, room[1], next.scale, head_y,
      0.10f);
  Near("a hill that would be above the head goes 10 cm below it",
       head_y - (room[1] + (next.center[1] + 40.0f - cleared) / next.scale),
       0.10, 1e-4);

  // THE LAND THAT THE VIEW FOLLOWS (FollowStep). Heights in world units, the
  // way in meters of the room, 25 cm for each stage.
  const float length = kFollowLength;
  LandFollow follow;
  FollowStart(30.0f, &follow);
  Near("the start keeps the land where it is: a step over flat land moves "
       "nothing", FollowStep(&follow, 30.0f, 0.02f, length), 0.0, 1e-6);
  Near("a frame with no step moves nothing, also over a hill",
       FollowStep(&follow, 80.0f, 0.0f, length), 0.0, 1e-6);
  Near("and the hill of that frame is not kept", follow.stage[0], 30.0, 1e-5);
  // One step of 10 cm onto land 10 units higher: the exact answer of two
  // stages, 1 - e^-x (1 + x) with x = 0.4.
  const float one = FollowStep(&follow, 40.0f, 0.10f, length);
  const double x = 0.4;
  Near("a step onto higher land starts slowly (two stages)", one,
       10.0 * (1.0 - exp(-x) * (1.0 + x)), 1e-4);
  LandFollow split;
  FollowStart(30.0f, &split);
  float sum = 0.0f;
  for (int i = 0; i < 10; ++i) sum += FollowStep(&split, 40.0f, 0.01f, length);
  Near("ten steps of 1 cm give what one step of 10 cm gives", sum, one, 1e-4);
  // A long climb: the land rises 20 units for each meter of the room. After
  // 3 m the view rises with the land, 0.2 units for each step of 1 cm, and
  // it is 2 * 0.25 m of the climb behind it: 10 units.
  LandFollow climb;
  FollowStart(0.0f, &climb);
  float rise = 0.0f, land = 0.0f;
  for (int i = 0; i < 300; ++i) {
    land += 0.2f;
    rise = FollowStep(&climb, land, 0.01f, length);
  }
  Near("on a long hill the view rises with the land", rise, 0.2, 2e-3);
  Near("and it is the way of two stages behind it", land - climb.stage[1],
       20.0 * 2.0 * 0.25, 0.3);
  // A bump: 20 units for 10 cm of the way, then the land of before. The
  // view rises a little and comes back down: the bump does not shake it.
  LandFollow bump;
  FollowStart(0.0f, &bump);
  float peak = 0.0f, at = 0.0f;
  for (int i = 0; i < 100; ++i) {
    const float under = (i >= 10 && i < 20) ? 20.0f : 0.0f;
    at += FollowStep(&bump, under, 0.01f, length);
    if (at > peak) peak = at;
  }
  Check(peak < 20.0f * 0.15f, "a bump of 10 cm moves the view less than 15 "
        "percent of its height", peak, 20.0f * 0.15f);
  Near("and 80 cm after the bump the view is near its height of before", at,
       0.0, 20.0 * 0.05);
  Near("a step with a height that is not a number moves nothing",
       FollowStep(&bump, sqrtf(-1.0f), 0.01f, length), 0.0, 1e-6);
}

}  // namespace walk_checks
