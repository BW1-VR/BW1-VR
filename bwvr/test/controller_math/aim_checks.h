// THE AIM RAY (docs/controllers.md, section 8).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/aim_math.h"

namespace aim_checks {

// The direction of an aim orientation in the room, as controllers.cpp gets
// it: HeadPoseToRoom, then AimDirection. The room has x right, y up and z
// forward.
inline void RoomDirection(const float orientation[4], float out[3]) {
  const float position[3] = {0.0f, 0.0f, 0.0f};
  float m[9], t[3];
  HeadPoseToRoom(position, orientation, m, t);
  controller_math::AimDirection(m, out);
}

// The forward axis of Unity's Quaternion.Euler(x, y, z), in degrees. Unity
// has x right, y up and z forward, and it turns about z, then x, then y. The
// turn about z does not move the forward axis.
inline void UnityForward(double x_deg, double y_deg, double out[3]) {
  const double k = 3.14159265358979 / 180.0;
  out[0] = sin(y_deg * k) * cos(x_deg * k);
  out[1] = -sin(x_deg * k);
  out[2] = cos(y_deg * k) * cos(x_deg * k);
}

// Mirror a pose from one hand to the other, as Virtual Desktop does it.
inline void MirrorPose(float position[3], float orientation[4]) {
  position[0] = -position[0];
  orientation[1] = -orientation[1];
  orientation[2] = -orientation[2];
}

inline void AimChecks() {
  using namespace aim_math;
  printf("\nThe aim ray\n");
  const double k = 3.14159265358979 / 180.0;
  const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float d[3];

  // THE CHAIN: a known aim orientation gives a known direction in the room.
  RoomDirection(identity, d);
  Near("an aim pose with no turn aims along +z of the room: x", d[0], 0.0,
       1e-5);
  Near("y", d[1], 0.0, 1e-5);
  Near("z", d[2], 1.0, 1e-5);
  {
    // 90 degrees about +y of the runtime's frame is a turn to the left.
    const float s = static_cast<float>(sin(45.0 * k));
    const float left[4] = {0.0f, s, 0.0f, s};
    RoomDirection(left, d);
    Near("an aim pose turned 90 degrees about +y aims to the left: x", d[0],
         -1.0, 1e-5);
    Near("z", d[2], 0.0, 1e-5);
    // 30 degrees about +x is up.
    const float up[4] = {static_cast<float>(sin(15.0 * k)), 0.0f, 0.0f,
                         static_cast<float>(cos(15.0 * k))};
    RoomDirection(up, d);
    Near("an aim pose turned 30 degrees about +x aims up: y", d[1], 0.5,
         1e-5);
    Near("z", d[2], cos(30.0 * k), 1e-5);
    // Yaw 20 to the left, pitch 30 down, with a roll: the roll does not
    // move the ray.
    float q[4];
    controller_math::FakeOrientation(20.0f, -30.0f, 40.0f, q);
    RoomDirection(q, d);
    Near("yaw 20, pitch -30, roll 40: x", d[0],
         -sin(20.0 * k) * cos(30.0 * k), 1e-5);
    Near("y", d[1], -0.5, 1e-5);
    Near("z", d[2], cos(20.0 * k) * cos(30.0 * k), 1e-5);
  }

  // THE META POINTER: the turn of Meta's Interaction SDK. The room has the
  // axes of Unity, so the two directions must be equal.
  {
    float q[4];
    double want[3];
    Turned(identity, kTouchYawInDeg, kTouchPitchUpDeg, false, q);
    RoomDirection(q, d);
    UnityForward(359.209534, 353.548035, want);
    Near("the right hand, against Euler(359.209534, 353.548035, z): x", d[0],
         want[0], 1e-5);
    Near("y", d[1], want[1], 1e-5);
    Near("z", d[2], want[2], 1e-5);
    Near("it is 6.45 degrees to the left", atan2(-d[0], d[2]) / k, 6.452,
         0.01);
    Near("and 0.79 degrees up", asin(d[1]) / k, 0.790, 0.01);
    Turned(identity, kTouchYawInDeg, kTouchPitchUpDeg, true, q);
    RoomDirection(q, d);
    UnityForward(359.209534, 6.45196056, want);
    Near("the left hand is the mirror image: x", d[0], want[0], 1e-5);
    Near("y", d[1], want[1], 1e-5);
    Near("z", d[2], want[2], 1e-5);
  }

  // THE TURN IS IN THE FRAME OF THE CONTROLLER. A right hand with its top
  // leaned to the left: a turn toward the other hand also goes down.
  {
    float rolled[4], q[4];
    controller_math::FakeOrientation(0.0f, 0.0f, -90.0f, rolled);
    Turned(rolled, 10.0f, 0.0f, false, q);
    RoomDirection(q, d);
    Near("top to the left by 90 degrees: the turn is all down, x", d[0], 0.0,
         1e-5);
    Near("y", d[1], -sin(10.0 * k), 1e-5);
    controller_math::FakeOrientation(0.0f, 0.0f, -30.0f, rolled);
    Turned(rolled, 10.0f, 0.0f, false, q);
    RoomDirection(q, d);
    Near("top to the left by 30 degrees: to the left, x", d[0],
         -sin(10.0 * k) * cos(30.0 * k), 1e-5);
    Near("and down, y", d[1], -sin(10.0 * k) * sin(30.0 * k), 1e-5);
    // The Meta pointer for a relaxed right hand, whose top leans 30 degrees
    // to the left: to the left and down from the aim pose.
    Turned(rolled, kTouchYawInDeg, kTouchPitchUpDeg, false, q);
    RoomDirection(q, d);
    Near("the Meta pointer with a lean of 30 degrees: 6.0 degrees left",
         atan2(-d[0], d[2]) / k, 5.98, 0.02);
    Near("and 2.5 degrees down", -asin(d[1]) / k, 2.54, 0.02);
    // A trim of the pitch only is the same for both hands.
    float a[3], b[3];
    Turned(identity, 0.0f, -4.0f, false, q);
    RoomDirection(q, a);
    Turned(identity, 0.0f, -4.0f, true, q);
    RoomDirection(q, b);
    Near("a pitch trim of -4 aims 4 degrees down", asin(a[1]) / k, -4.0,
         1e-3);
    Near("for both hands", b[1], a[1], 1e-6);
    Near("with no turn to a side", a[0] - b[0], 0.0, 1e-6);
  }

  // THE HALF-LIFE: ALYX POINTER. The game turns the raw pose of SteamVR 45
  // degrees down and 10 degrees toward the other hand, and the aim pose of
  // SteamVR is the raw pose turned 39.4 degrees down. The raw frame has x
  // right, y up and -z forward.
  {
    const double p = 45.0 * k, y = 10.0 * k, a = 39.4 * k;
    // The pointer of the right hand in the raw frame, and the axes of the
    // aim pose in it.
    const double ray[3] = {-sin(y) * cos(p), -sin(p), -cos(y) * cos(p)};
    const double ahead[3] = {0.0, -sin(a), -cos(a)};
    const double up[3] = {0.0, cos(a), -sin(a)};
    const double back[3] = {0.0, sin(a), cos(a)};
    const double along = ray[1] * ahead[1] + ray[2] * ahead[2];
    const double rise = ray[1] * up[1] + ray[2] * up[2];
    Near("the Alyx pointer against the aim pose: yaw toward the other hand",
         atan2(-ray[0], along) / k, kAlyxYawInDeg, 0.005);
    Near("and pitch, which is down", asin(rise) / k, kAlyxPitchUpDeg, 0.005);
    // Its point: 2.5 inches under the raw origin, then 1 inch back along the
    // pointer. The aim point of SteamVR is at -0.007 -0.03894766 0.00949694.
    const double inch = 0.0254;
    const double point[3] = {-inch * ray[0], -2.5 * inch - inch * ray[1],
                             -inch * ray[2]};
    const double d2[3] = {point[0] + 0.007, point[1] + 0.03894766,
                          point[2] - 0.00949694};
    const PresetPose& alyx = kPresets[kPresetAlyx];
    Near("its point from the aim point: to the outside", d2[0], alyx.out_m,
         2e-4);
    Near("up", d2[1] * up[1] + d2[2] * up[2], alyx.up_m, 2e-4);
    Near("back", d2[1] * back[1] + d2[2] * back[2], alyx.back_m, 2e-4);
    // The ray of the mod with this preset.
    float q[4], room[3];
    const Turn t = TotalTurn(kPresetAlyx, true, 0.0f, 0.0f);
    Turned(identity, t.yaw_in_deg, t.pitch_up_deg, false, q);
    RoomDirection(q, room);
    Near("the right hand with the Alyx preset: 7.09 degrees to the left",
         atan2(-room[0], room[2]) / k, 7.092, 0.01);
    Near("and 5.99 degrees down", -asin(room[1]) / k, 5.993, 0.01);
    Turned(identity, t.yaw_in_deg, t.pitch_up_deg, true, q);
    RoomDirection(q, room);
    Near("the left hand: 7.09 degrees to the right",
         atan2(room[0], room[2]) / k, 7.092, 0.01);
  }

  // THE AIM POINT OF A PRESET, in the frame of the aim pose of the runtime.
  {
    const float at[3] = {0.2f, 1.0f, -0.3f};
    float out[3];
    PresetPoint(kPresetAlyx, true, false, at, identity, out);
    Near("the right hand: 1.01 cm to the right", out[0] - at[0], 0.0101,
         1e-6);
    Near("1.03 cm down", out[1] - at[1], -0.0103, 1e-6);
    Near("0.21 cm back", out[2] - at[2], 0.0021, 1e-6);
    PresetPoint(kPresetAlyx, true, true, at, identity, out);
    Near("the left hand: 1.01 cm to the left", out[0] - at[0], -0.0101, 1e-6);
    PresetPoint(kPresetAlyx, false, false, at, identity, out);
    Near("a controller that is not a Touch keeps its aim point",
         controller_math::Distance(out, at), 0.0, 1e-7);
    PresetPoint(kPresetRuntime, true, false, at, identity, out);
    Near("the ray of the runtime keeps the aim point",
         controller_math::Distance(out, at), 0.0, 1e-7);
    // The move is in the frame of the aim pose: with the pose turned 90
    // degrees to the left, "to the outside" is forward in the room.
    const float s = static_cast<float>(sin(45.0 * k));
    const float left[4] = {0.0f, s, 0.0f, s};
    PresetPoint(kPresetAlyx, true, false, at, left, out);
    Near("the point goes with the pose: -z of the runtime", out[2] - at[2],
         -0.0101, 1e-6);
    PresetPoint(kPresetMeta, true, false, at, identity, out);
    Near("the Meta pointer starts 1.75 cm behind the aim point",
         out[2] - at[2], 0.0175, 1e-6);
  }

  // THE TURN IN USE.
  {
    Turn t = TotalTurn(kPresetMeta, true, 1.0f, -3.0f);
    Near("the Meta pointer plus a trim: yaw", t.yaw_in_deg,
         kTouchYawInDeg + 1.0, 1e-5);
    Near("pitch", t.pitch_up_deg, kTouchPitchUpDeg - 3.0, 1e-5);
    t = TotalTurn(kPresetAlyx, true, 1.0f, -3.0f);
    Near("the Alyx pointer plus a trim: yaw", t.yaw_in_deg,
         kAlyxYawInDeg + 1.0, 1e-5);
    Near("pitch", t.pitch_up_deg, kAlyxPitchUpDeg - 3.0, 1e-5);
    t = TotalTurn(kPresetAlyx, false, 1.0f, -3.0f);
    Near("a controller that is not a Touch has the trim only: yaw",
         t.yaw_in_deg, 1.0, 1e-6);
    Near("pitch", t.pitch_up_deg, -3.0, 1e-6);
    t = TotalTurn(kPresetRuntime, true, 0.0f, 0.0f);
    Near("aim_preset=0 with no trim is the aim pose of the runtime: yaw",
         t.yaw_in_deg, 0.0, 1e-6);
    Near("pitch", t.pitch_up_deg, 0.0, 1e-6);
    Check(NextPreset(kPresetAlyx) == kPresetRuntime &&
              NextPreset(kPresetRuntime) == kPresetMeta &&
              NextPreset(kPresetMeta) == kPresetAlyx,
          "the key goes from Alyx to the runtime, to Meta and back to Alyx",
          1, 1);
    Check(PresetOf(-1) == kPresetRuntime && PresetOf(7) == kPresetAlyx &&
              PresetOf(1) == kPresetMeta,
          "a preset from the ini stays in the range", 1, 1);
    float q[4];
    Turned(identity, 0.0f, 0.0f, false, q);
    Near("and its turn is the identity", q[3], 1.0, 1e-6);
    Check(TouchProfile("oculus/touch_controller"),
          "the Touch profile is a Touch controller", 1, 1);
    Check(!TouchProfile("fake") && !TouchProfile("valve/index_controller") &&
              !TouchProfile("") && !TouchProfile(nullptr),
          "the fake hand, an Index controller and no profile are not", 0, 0);
    Near("a trim step of 1 degree", TrimStep(2.0f, 1.0f), 3.0, 1e-6);
    Near("the trim stops at 30 degrees", TrimStep(29.5f, 1.0f), 30.0, 1e-6);
    Near("and at -30 degrees", TrimStep(-30.0f, -1.0f), -30.0, 1e-6);
  }

  // WHAT THE RUNTIME GIVES. The poses of the left hand that Virtual Desktop
  // took from the Oculus runtime (rebindControllerActions in action.cpp of
  // VirtualDesktop-OpenXR).
  {
    float grip_p[3] = {0.755659044f, 0.849373817f, 0.065275185f};
    float grip_q[4] = {-0.406630576f, -0.480359077f, -0.434706628f,
                       0.644155979f};
    float aim_p[3] = {0.711199582f, 0.760591149f, 0.057151683f};
    float aim_q[4] = {-0.674230397f, -0.198649853f, -0.616646528f,
                      0.354540110f};
    GripToAim g;
    GripToAimOf(grip_p, grip_q, aim_p, aim_q, &g);
    Near("Virtual Desktop: the aim pose is 60 degrees down from the grip",
         g.pitch_up_deg, -60.0, 0.01);
    Near("with no yaw", g.yaw_left_deg, 0.0, 0.01);
    Near("and no roll", g.roll_deg, 0.0, 0.01);
    Near("the aim point in the grip frame, x in m", g.offset_m[0], 0.0, 2e-4);
    Near("y", g.offset_m[1], -0.0673, 2e-4);
    Near("z", g.offset_m[2], -0.0735, 2e-4);
    // The right hand is the mirror image.
    MirrorPose(grip_p, grip_q);
    MirrorPose(aim_p, aim_q);
    GripToAim r;
    GripToAimOf(grip_p, grip_q, aim_p, aim_q, &r);
    Near("the right hand: 60 degrees down", r.pitch_up_deg, -60.0, 0.01);
    Near("no yaw", r.yaw_left_deg, 0.0, 0.01);
    Near("the same point, y", r.offset_m[1], g.offset_m[1], 1e-5);
    Near("z", r.offset_m[2], g.offset_m[2], 1e-5);
    Check(!GripToAimMoved(g, r, 1.0f, 0.005f),
          "the two hands of Virtual Desktop are not a change", 0, 0);
  }
  {
    // A made up runtime: yaw 20, pitch -35, roll 15, and an offset.
    float grip_q[4], relative[4], aim_q[4];
    controller_math::FakeOrientation(-70.0f, 25.0f, 10.0f, grip_q);
    controller_math::FakeOrientation(20.0f, -35.0f, 15.0f, relative);
    controller_math::QuatMultiply(grip_q, relative, aim_q);
    const float grip_p[3] = {0.3f, 1.1f, -0.4f};
    const float offset[3] = {0.01f, -0.02f, -0.05f};
    float moved[3];
    controller_math::QuatRotate(grip_q, offset, moved);
    const float aim_p[3] = {grip_p[0] + moved[0], grip_p[1] + moved[1],
                            grip_p[2] + moved[2]};
    GripToAim g;
    GripToAimOf(grip_p, grip_q, aim_p, aim_q, &g);
    Near("a made up runtime: yaw 20 to the left", g.yaw_left_deg, 20.0, 1e-3);
    Near("pitch 35 down", g.pitch_up_deg, -35.0, 1e-3);
    Near("roll 15", g.roll_deg, 15.0, 1e-3);
    Near("offset x", g.offset_m[0], 0.01, 1e-5);
    Near("offset y", g.offset_m[1], -0.02, 1e-5);
    Near("offset z", g.offset_m[2], -0.05, 1e-5);
    // The same pose with no change is the same answer for the fake hand.
    GripToAimOf(grip_p, grip_q, grip_p, grip_q, &g);
    Near("the fake hand, whose aim pose is its grip pose: pitch",
         g.pitch_up_deg, 0.0, 1e-3);
    Near("offset", controller_math::Length(g.offset_m), 0.0, 1e-6);

    // WHEN THE LOG SAYS IT AGAIN.
    GripToAim a = {-60.0f, 0.0f, 0.0f, {0.0f, -0.0673f, -0.0735f}};
    GripToAim b = a;
    b.pitch_up_deg = -60.5f;
    Check(!GripToAimMoved(a, b, 1.0f, 0.005f),
          "half a degree is not a change", 0, 0);
    b.pitch_up_deg = -61.5f;
    Check(GripToAimMoved(a, b, 1.0f, 0.005f), "1.5 degrees is a change", 1,
          1);
    b = a;
    b.offset_m[2] = -0.0775f;
    Check(!GripToAimMoved(a, b, 1.0f, 0.005f), "4 mm is not a change", 0, 0);
    b.offset_m[2] = -0.0795f;
    Check(GripToAimMoved(a, b, 1.0f, 0.005f), "6 mm is a change", 1, 1);
    Near("179 and -179 degrees are 2 degrees apart", AngleApart(179.0f,
                                                                -179.0f),
         2.0, 1e-4);
  }
}

}  // namespace aim_checks
