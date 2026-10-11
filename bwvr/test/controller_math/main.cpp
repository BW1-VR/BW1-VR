// A math test for the motion controllers and the table, with no game and no
// headset.
//
// It compiles the real head_pose.cpp, the math of controllers.cpp
// (controller_math.h) and the table math (table_math.h). It checks the
// numbers of docs/controllers.md, "Tests", and a few more.
// The overlay checks follow docs/hand-model.md, "Overlay renderer".
// The table checks follow docs/tabletop.md, "The table camera". The head is a
// fake head, so the origin is the identity at zero, and a fake hand in LOCAL space
// is a hand in the room. The direct hand checks move the head and keep the
// hand still: the reach point, its factor and the drag ray must not change.
//
// The test needs no game: ModLog and SpiritAnchorCameraCut are stubs.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "../../src/mod/controller_math.h"
#include "../../src/mod/table_math.h"
#include "../../src/mod/overlay_math.h"

// Stubs for what head_pose.cpp expects from the mod.
void ModLog(const char* format, ...) {
  (void)format;
}
void SpiritAnchorCameraCut() {}

// head_pose.cpp
bool HeadPoseCurrent(float matrix_out[9], float translation_out[3]);
bool HeadPoseToRoom(const float position[3], const float orientation[4],
                    float rotation_out[9], float translation_out[3]);
bool HeadPoseHeadInRoom(float translation_out[3]);
bool HeadPoseUpdate(const float position[3], const float orientation[4]);
void HeadPoseSetFake(bool on, float yaw_degrees, float pitch_degrees,
                     float roll_degrees);
void HeadPoseFakeEye(int eye, float position[3], float orientation[4]);
void HeadPoseSetScale(float units_per_meter);
float HeadPoseScale();
void HeadPoseSetRotationOnly(bool on);
void HeadPoseSetFakeLean(int right_cm, int up_cm, int forward_cm);
void HeadPoseFakeCenter(float out[3]);
bool HeadPoseOrigin(float position_out[3], float rotation_out[4]);

namespace {

using namespace controller_math;

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what, double got, double want) {
  ++g_checks;
  printf("  %s  %s (got %.4f, want %.4f)\n", ok ? "pass" : "FAIL", what, got,
         want);
  if (!ok) ++g_failures;
}

void Near(const char* what, double got, double want, double tolerance) {
  Check(fabs(got - want) <= tolerance, what, got, want);
}

const float kDeg = 3.14159265f / 180.0f;

// The numbers of a 1280 by 720 render, as measured in the game.
const Screen kScreen = {640.0f, 360.0f, 1.4281f, 2.5389f};

// The room basis L for a game camera heading, columns right, up and forward.
void RoomBasis(float yaw_degrees, float room[9]) {
  const float c = cosf(yaw_degrees * kDeg), s = sinf(yaw_degrees * kDeg);
  const float right[3] = {c, 0.0f, -s};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  const float forward[3] = {s, 0.0f, c};
  for (int i = 0; i < 3; ++i) {
    room[i * 3 + 0] = right[i];
    room[i * 3 + 1] = up[i];
    room[i * 3 + 2] = forward[i];
  }
}

// One frame of the head: an invented head, then its pose.
void SetHead(float yaw, float pitch, float roll, float head_out[9]) {
  HeadPoseSetFake(true, yaw, pitch, roll);
  float position[3], orientation[4];
  HeadPoseFakeEye(0, position, orientation);
  const float center[3] = {0.0f, 0.0f, 0.0f};
  HeadPoseUpdate(center, orientation);
  float lean[3];
  HeadPoseCurrent(head_out, lean);
}

// What controllers.cpp does for a hand, from the head and the room.
struct Result {
  float origin[3];
  float direction[3];
  float distance;  // origin to the eye
  float x, y;
  bool have_pixel;
  bool off_view;
};

// The hand is given as a fake hand: offsets in centimeters, angles in degrees.
Result Hand(const float eye[3], const float room[9], const float head[9],
            float right_cm, float up_cm, float forward_cm, float yaw,
            float pitch, float roll, float target) {
  const float position[3] = {right_cm * 0.01f, up_cm * 0.01f,
                             -forward_cm * 0.01f};
  float orientation[4];
  FakeOrientation(yaw, pitch, roll, orientation);
  float m_hand[9], t_hand[3], t_head[3];
  HeadPoseToRoom(position, orientation, m_hand, t_hand);
  HeadPoseHeadInRoom(t_head);

  Result r = {};
  float rotation[9];
  HandToWorld(eye, room, t_head, t_hand, m_hand, r.origin, rotation);
  AimDirection(rotation, r.direction);
  r.distance = Distance(r.origin, eye);
  r.have_pixel = RenderPixel(eye, room, head, r.origin, r.direction, target,
                             kScreen, &r.x, &r.y, &r.off_view);
  return r;
}

// One head sample with the head center moved in LOCAL space, in meters: the
// head matrix, the lean and the head center, as head_pose.cpp gives them.
void SetHeadAt(float yaw, const float center[3], float head_out[9],
               float lean_out[3], float t_head_out[3]) {
  HeadPoseSetFake(true, yaw, -15.0f, 0.0f);
  float position[3], orientation[4];
  HeadPoseFakeEye(0, position, orientation);
  HeadPoseUpdate(center, orientation);
  HeadPoseCurrent(head_out, lean_out);
  HeadPoseHeadInRoom(t_head_out);
}

// What controllers.cpp does for the direct hand, from one head sample.
struct Direct {
  float eye[3];
  float anchor[3];
  float grip[3];
  float reach[3];
  float factor;
  float camera_dir[3];
  float old_reach[3];  // the reach from the render eye
};

const float kStart = 0.35f;
const float kGain = 215.0f;
const float kAnchorDown = 0.20f;
const float kFarLimit = 1800.0f;

Direct DirectHand(const float game_position[3], const float room[9],
                  float yaw, const float head_center[3], float right_cm,
                  float up_cm, float forward_cm) {
  float head[9], lean[3], t_head[3];
  SetHeadAt(yaw, head_center, head, lean, t_head);
  const float position[3] = {right_cm * 0.01f, up_cm * 0.01f,
                             -forward_cm * 0.01f};
  const float orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float m_grip[9], t_grip[3];
  HeadPoseToRoom(position, orientation, m_grip, t_grip);
  const float scale = HeadPoseScale();
  Direct r = {};
  EyeFromLean(game_position, room, lean, r.eye);
  float anchor_room[3];
  ReachAnchor(scale, kAnchorDown, anchor_room);
  DirectReach(r.eye, room, t_head, t_grip, anchor_room, scale, kStart, kGain,
              kFarLimit, r.anchor, r.grip, r.reach, &r.factor);
  DirectionTo(game_position, r.reach, r.camera_dir);
  float old_factor = 0.0f;
  ReachPoint(r.eye, r.grip, scale, kStart, kGain, 0.0f, r.old_reach,
             &old_factor);
  return r;
}

// THE GRAB OF THE WORLD.
//
// A camera over the island, a head and hands in the room, and the solve of
// controller_math.h. The world point of a room point a is C + L * a, with L
// the level basis of the camera now.

const float kUnits = 7.5f;  // units per meter

// A room point from cm: right, up, forward.
void RoomCm(float right, float up, float forward, float out[3]) {
  out[0] = right * 0.01f * kUnits;
  out[1] = up * 0.01f * kUnits;
  out[2] = forward * 0.01f * kUnits;
}

// The world point of a v point for a pose, through the level basis of the
// pose.
void WorldOf(const GrabPose& pose, const float v[3], float out[3]) {
  float room[9], offset[3];
  LevelBasis(pose.position, pose.focus, room);
  Apply(room, false, v, offset);
  for (int i = 0; i < 3; ++i) out[i] = pose.position[i] + offset[i];
}

GrabAnchor MakeAnchor(const GrabPose& pose, int hands, const float middle[3],
                      const float spread[3]) {
  GrabAnchor a = {};
  a.hands = hands;
  a.pose = pose;
  LevelBasis(pose.position, pose.focus, a.room);
  for (int i = 0; i < 3; ++i) {
    a.middle[i] = middle[i];
    a.spread[i] = spread[i];
  }
  float offset[3];
  Apply(a.room, false, a.middle, offset);
  const float span = Length(offset);
  a.least_distance = span < 5.0f ? span : 5.0f;
  return a;
}

float Heading2(const float from[3], const float to[3]) {
  const float d[3] = {to[0] - from[0], 0.0f, to[2] - from[2]};
  return HeadingOf(d);
}

void GrabChecks() {
  printf("\nThe grab of the world (M7)\n");
  // A camera in the pen, looking down at the land ahead and to the side.
  GrabPose start = {{2222.0f, 21.0f, 3144.0f},
                    {2250.0f, -10.0f, 3060.0f},
                    {2248.0f, -12.0f, 3070.0f}};
  const float head[3] = {0.4f, 0.1f, -0.2f};  // r0, the head at the press
  const float gains[2] = {1.0f, 44.0f};
  for (int gi = 0; gi < 2; ++gi) {
    const float g = gains[gi];
    printf("One hand, gain %.0f\n", g);
    float a_press[3];
    RoomCm(-20.0f, -30.0f, 40.0f, a_press);
    float v_press[3];
    GrabVirtual(head, a_press, g, g, v_press);
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const GrabAnchor anchor = MakeAnchor(start, 1, v_press, zero);
    float grabbed[3];
    WorldOf(start, v_press, grabbed);

    float theta = 0.0f, k = 1.0f;
    GrabPose pose;
    GrabSolve(anchor, v_press, zero, true, true, 0.6f, &theta, &k, &pose);
    Near("a still hand gives the camera of the press, units",
         Distance(pose.position, start.position), 0.0, 1e-4);

    // Hand steps in cm: right, up, toward the head, a mix, a large one.
    const float steps[5][3] = {{10, 0, 0}, {10, 10, 0}, {10, 10, -10},
                               {-7, 23, 15}, {60, -40, 30}};
    const char* const names[5] = {"10 cm right", "then 10 cm up",
                                  "then 10 cm toward the head", "a mixed step",
                                  "a large step"};
    for (int s = 0; s < 5; ++s) {
      float a[3], v[3], step[3];
      RoomCm(steps[s][0], steps[s][1], steps[s][2], step);
      for (int i = 0; i < 3; ++i) a[i] = a_press[i] + step[i];
      GrabVirtual(head, a, g, g, v);
      GrabSolve(anchor, v, zero, true, true, 0.6f, &theta, &k, &pose);
      float in_hand[3];
      WorldOf(pose, v, in_hand);
      char what[160];
      sprintf_s(what, "%s: the grabbed point stays in the hand, units",
                names[s]);
      Near(what, Distance(in_hand, grabbed), 0.0, 0.01);
      sprintf_s(what, "%s: the camera moves g times the hand, units",
                names[s]);
      float moved[3];
      for (int i = 0; i < 3; ++i) moved[i] = pose.position[i] - start.position[i];
      Near(what, Length(moved), g * Length(step), 0.01);
      sprintf_s(what, "%s: the focus keeps its distance, units", names[s]);
      Near(what, Distance(pose.position, pose.focus),
           Distance(start.position, start.focus), 1e-3);
      if (s == 0) {
        float along[3];
        Apply(anchor.room, true, moved, along);
        Near("10 cm right: the camera goes left along the room right, units",
             along[0], -0.75 * g, 1e-3);
        Near("10 cm right: and not up or forward, units",
             fabs(along[1]) + fabs(along[2]), 0.0, 1e-3);
      }
      if (s == 1) {
        float along[3];
        Apply(anchor.room, true, moved, along);
        Near("10 cm up: the camera goes down, units", along[1], -0.75 * g,
             1e-3);
      }
      if (s == 2) {
        float along[3];
        Apply(anchor.room, true, moved, along);
        Near("10 cm toward the head: the camera goes forward, units",
             along[2], 0.75 * g, 1e-3);
      }
    }
    // The same hands twice give the same camera: the solve is absolute.
    {
      float a[3], v[3];
      RoomCm(-3.0f, -21.0f, 44.0f, a);
      GrabVirtual(head, a, g, g, v);
      GrabPose first, second;
      GrabSolve(anchor, v, zero, true, true, 0.6f, &theta, &k, &first);
      GrabSolve(anchor, v, zero, true, true, 0.6f, &theta, &k, &second);
      Near("a second solve in one frame changes nothing, units",
           Distance(first.position, second.position) +
               Distance(first.focus, second.focus),
           0.0, 0.0);
      // Back to the press point: back to the press camera, with no drift.
      GrabSolve(anchor, v_press, zero, true, true, 0.6f, &theta, &k, &pose);
      Near("back at the press point, the camera is back, units",
           Distance(pose.position, start.position), 0.0, 1e-3);
    }
  }

  printf("A head that moves, with a still hand\n");
  {
    // The hand's room point comes from head_pose.cpp. The head moves 5 cm
    // and turns 20 degrees. The room point, and so the camera, do not move.
    float head_m[9], lean[3], t_head[3];
    const float center0[3] = {0.0f, 0.0f, 0.0f};
    SetHeadAt(0.0f, center0, head_m, lean, t_head);
    const float hand_position[3] = {-0.2f, -0.3f, -0.4f};
    const float hand_orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float m[9], a0[3];
    HeadPoseToRoom(hand_position, hand_orientation, m, a0);
    float v0[3];
    GrabVirtual(t_head, a0, 44.0f, 44.0f, v0);
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const GrabAnchor anchor = MakeAnchor(start, 1, v0, zero);
    const float moves[3][3] = {{0.05f, 0.0f, 0.0f},
                               {0.0f, 0.03f, -0.05f},
                               {-0.03f, -0.03f, -0.03f}};
    for (int mi = 0; mi < 3; ++mi) {
      SetHeadAt(20.0f, moves[mi], head_m, lean, t_head);
      float a1[3];
      HeadPoseToRoom(hand_position, hand_orientation, m, a1);
      Near("the head moves: the hand's room point does not, units",
           Distance(a1, a0), 0.0, 1e-5);
      // r0 is the head at the press, not the head now.
      float v1[3];
      GrabVirtual(center0, a1, 44.0f, 44.0f, v1);
      float theta = 0.0f, k = 1.0f;
      GrabPose pose;
      GrabSolve(anchor, v1, zero, true, true, 0.6f, &theta, &k, &pose);
      Near("the head moves: the camera does not, units",
           Distance(pose.position, start.position) +
               Distance(pose.focus, start.focus),
           0.0, 1e-4);
    }
    const float centre_again[3] = {0.0f, 0.0f, 0.0f};
    SetHeadAt(0.0f, centre_again, head_m, lean, t_head);
  }

  printf("Two hands\n");
  {
    const float r0[3] = {0.0f, 0.0f, 0.0f};
    const float g = 30.0f;
    float left[3], right[3];
    RoomCm(-20.0f, -30.0f, 40.0f, left);
    RoomCm(20.0f, -30.0f, 40.0f, right);
    float vl[3], vr[3], middle[3], spread[3];
    GrabVirtual(r0, left, g, g, vl);
    GrabVirtual(r0, right, g, g, vr);
    for (int i = 0; i < 3; ++i) {
      middle[i] = 0.5f * (vl[i] + vr[i]);
      spread[i] = right[i] - left[i];
    }
    const GrabAnchor anchor = MakeAnchor(start, 2, middle, spread);
    float grab_l[3], grab_r[3], grab_a[3];
    WorldOf(start, vl, grab_l);
    WorldOf(start, vr, grab_r);
    GrabAnchorPoint(anchor, grab_a);
    const float min_spread = 0.08f * kUnits;

    // A turn of 30 degrees about up, about the middle of the hands.
    const float turns[2] = {30.0f, -45.0f};
    for (int t = 0; t < 2; ++t) {
      const float angle = turns[t] * kDeg;
      float centre[3], l2[3], r2[3];
      for (int i = 0; i < 3; ++i) centre[i] = 0.5f * (left[i] + right[i]);
      float half[3], turned[3];
      for (int i = 0; i < 3; ++i) half[i] = right[i] - centre[i];
      TurnUp(angle, half, turned);
      for (int i = 0; i < 3; ++i) {
        r2[i] = centre[i] + turned[i];
        l2[i] = centre[i] - turned[i];
      }
      float v2l[3], v2r[3], m2[3], s2[3];
      GrabVirtual(r0, l2, g, g, v2l);
      GrabVirtual(r0, r2, g, g, v2r);
      for (int i = 0; i < 3; ++i) {
        m2[i] = 0.5f * (v2l[i] + v2r[i]);
        s2[i] = r2[i] - l2[i];
      }
      float theta = 0.0f, k = 1.0f;
      GrabPose pose;
      GrabSolve(anchor, m2, s2, true, true, min_spread, &theta, &k, &pose);
      char what[160];
      sprintf_s(what, "turn %.0f: theta is the turn of the hands, degrees",
                turns[t]);
      Near(what, theta / kDeg, turns[t], 1e-3);
      Near("and the zoom stays 1", k, 1.0, 1e-5);
      float in_l[3], in_r[3];
      WorldOf(pose, v2l, in_l);
      WorldOf(pose, v2r, in_r);
      Near("the left hand keeps its world point, units",
           Distance(in_l, grab_l), 0.0, 0.01);
      Near("the right hand keeps its world point, units",
           Distance(in_r, grab_r), 0.0, 0.01);
      Near("the camera height does not change, units",
           pose.position[1], start.position[1], 1e-3);
      Near("the drop to the focus does not change (no tilt), units",
           pose.focus[1] - pose.position[1],
           start.focus[1] - start.position[1], 1e-3);
      Near("the camera heading turns by -theta, degrees",
           WrapAngle(Heading2(pose.position, pose.focus) -
                     Heading2(start.position, start.focus)) / kDeg,
           -turns[t], 1e-2);
      Near("the pivot keeps its place to the camera, turned, units",
           Distance(pose.pivot, pose.position),
           Distance(start.pivot, start.position), 1e-3);
    }

    // The hands spread from 40 to 60 cm, about their middle.
    {
      float l2[3], r2[3], v2l[3], v2r[3], m2[3], s2[3];
      RoomCm(-30.0f, -30.0f, 40.0f, l2);
      RoomCm(30.0f, -30.0f, 40.0f, r2);
      GrabVirtual(r0, l2, g, g, v2l);
      GrabVirtual(r0, r2, g, g, v2r);
      for (int i = 0; i < 3; ++i) {
        m2[i] = 0.5f * (v2l[i] + v2r[i]);
        s2[i] = r2[i] - l2[i];
      }
      float theta = 0.0f, k = 1.0f;
      GrabPose pose;
      GrabSolve(anchor, m2, s2, true, true, min_spread, &theta, &k, &pose);
      Near("a spread of 40 to 60 cm gives k 1.5", k, 1.5, 1e-4);
      Near("the camera is 1/1.5 as far from the grabbed point",
           Distance(pose.position, grab_a) /
               Distance(start.position, grab_a),
           1.0 / 1.5, 1e-4);
      Near("and the heading does not change, degrees",
           WrapAngle(Heading2(pose.position, pose.focus) -
                     Heading2(start.position, start.focus)) / kDeg,
           0.0, 1e-3);
      Near("the grabbed point stays in the direction of the hands (cosine)",
           [&]() {
             float to_a[3], to_m[3], room[9], offset[3];
             LevelBasis(pose.position, pose.focus, room);
             Apply(room, false, m2, offset);
             for (int i = 0; i < 3; ++i) {
               to_a[i] = grab_a[i] - pose.position[i];
               to_m[i] = offset[i];
             }
             return (to_a[0] * to_m[0] + to_a[1] * to_m[1] +
                     to_a[2] * to_m[2]) / (Length(to_a) * Length(to_m));
           }(), 1.0, 1e-5);
    }

    // One hand up and one down: the horizontal spread does not turn, and
    // nothing tilts.
    {
      float l2[3], r2[3], v2l[3], v2r[3], m2[3], s2[3];
      RoomCm(-20.0f, -40.0f, 40.0f, l2);
      RoomCm(20.0f, -20.0f, 40.0f, r2);
      GrabVirtual(r0, l2, g, g, v2l);
      GrabVirtual(r0, r2, g, g, v2r);
      for (int i = 0; i < 3; ++i) {
        m2[i] = 0.5f * (v2l[i] + v2r[i]);
        s2[i] = r2[i] - l2[i];
      }
      float theta = 0.0f, k = 1.0f;
      GrabPose pose;
      GrabSolve(anchor, m2, s2, true, true, min_spread, &theta, &k, &pose);
      Near("hands up and down: no turn, degrees", theta / kDeg, 0.0, 1e-4);
      Near("hands up and down: the drop to the focus stays (no tilt), units",
           pose.focus[1] - pose.position[1],
           start.focus[1] - start.position[1], 1e-3);
    }

    // Hands closer than 8 cm: the turn and the zoom hold their last values.
    {
      float l2[3], r2[3], v2l[3], v2r[3], m2[3], s2[3];
      RoomCm(-2.0f, -30.0f, 40.0f, l2);
      RoomCm(1.0f, -30.0f, 43.0f, r2);
      GrabVirtual(r0, l2, g, g, v2l);
      GrabVirtual(r0, r2, g, g, v2r);
      for (int i = 0; i < 3; ++i) {
        m2[i] = 0.5f * (v2l[i] + v2r[i]);
        s2[i] = r2[i] - l2[i];
      }
      float theta = 0.2f, k = 1.3f;
      GrabPose pose;
      GrabSolve(anchor, m2, s2, true, true, min_spread, &theta, &k, &pose);
      Near("hands 4 cm apart: the turn holds, radians", theta, 0.2, 1e-6);
      Near("hands 4 cm apart: the zoom holds", k, 1.3, 1e-6);
      theta = 0.0f;
      k = 1.0f;
      GrabSolve(anchor, m2, s2, false, false, min_spread, &theta, &k, &pose);
      Near("with the turn and the zoom off, they stay 0 and 1", theta + k, 1.0,
           1e-6);
    }
  }

  printf("The limits\n");
  {
    const float centre[3] = {2560.0f, 0.0f, 2560.0f};
    GrabPose pose = {{2560.0f + 3500.0f, 50.0f, 2560.0f},
                     {2560.0f + 3450.0f, 10.0f, 2600.0f},
                     {2560.0f + 3450.0f, 10.0f, 2600.0f}};
    const float view[3] = {pose.focus[0] - pose.position[0],
                           pose.focus[1] - pose.position[1],
                           pose.focus[2] - pose.position[2]};
    Check(GrabKeepInside(centre, 3400.0f, &pose), "a camera past the wall "
          "moves", 1, 1);
    Check(Distance(pose.position, centre) <= 3400.01f,
          "the camera is inside the wall", Distance(pose.position, centre),
          3400.0);
    Check(Distance(pose.focus, centre) <= 3400.01f,
          "and so is the focus", Distance(pose.focus, centre), 3400.0);
    Near("the pose moved as a whole (the view is the same), units",
         fabs(pose.focus[0] - pose.position[0] - view[0]) +
             fabs(pose.focus[1] - pose.position[1] - view[1]) +
             fabs(pose.focus[2] - pose.position[2] - view[2]),
         0.0, 1e-3);
    Near("a camera on the wall stays on it, units",
         Distance(pose.position, centre) > Distance(pose.focus, centre)
             ? Distance(pose.position, centre)
             : Distance(pose.focus, centre),
         3400.0, 0.02);
    // The focus alone past the wall.
    GrabPose inner = {{2560.0f + 3300.0f, 50.0f, 2560.0f},
                      {2560.0f + 3480.0f, 10.0f, 2560.0f},
                      {2560.0f + 3480.0f, 10.0f, 2560.0f}};
    GrabKeepInside(centre, 3400.0f, &inner);
    Check(Distance(inner.focus, centre) <= 3400.01f,
          "a focus past the wall is brought inside",
          Distance(inner.focus, centre), 3400.0);
    GrabPose near_centre = {{2600.0f, 50.0f, 2600.0f},
                            {2650.0f, 10.0f, 2700.0f},
                            {2650.0f, 10.0f, 2700.0f}};
    Check(!GrabKeepInside(centre, 3400.0f, &near_centre),
          "a camera inside the wall does not move", 0, 0);
    // The floor.
    GrabPose low = {{2600.0f, 3.0f, 2600.0f},
                    {2650.0f, -20.0f, 2700.0f},
                    {2650.0f, -20.0f, 2700.0f}};
    Check(GrabKeepAbove(12.0f, &low), "a camera under the floor moves", 1, 1);
    Near("it stands on the floor, units", low.position[1], 12.0, 1e-5);
    Near("its focus rose by the same, units", low.focus[1], -11.0, 1e-5);
    Check(!GrabKeepAbove(12.0f, &low), "a camera on the floor stays", 0, 0);
    // The gain.
    Near("the gain uses a hand at 25 cm at least",
         GrabGain(90.0f, 0.10f * kUnits, kUnits, 100.0f, 60.0f),
         90.0 / (0.25 * kUnits), 1e-3);
    Near("the gain stops at its most",
         GrabGain(2000.0f, 0.4f * kUnits, kUnits, 100.0f, 60.0f), 60.0, 1e-5);
    Near("the gain is never below 1",
         GrabGain(1.0f, 0.4f * kUnits, kUnits, 100.0f, 60.0f), 1.0, 1e-5);
    Near("150 units out and a hand at 45 cm give 44.4",
         GrabGain(150.0f, 0.45f * kUnits, kUnits, 100.0f, 60.0f), 44.44, 0.01);
    // The zoom stops near the grabbed point.
    {
      const float middle[3] = {0.0f, -1.0f, 6.0f};
      const float spread[3] = {3.0f, 0.0f, 0.0f};
      GrabAnchor a = MakeAnchor(start, 2, middle, spread);
      const float wide[3] = {30.0f, 0.0f, 0.0f};
      float theta = 0.0f, k = 1.0f;
      GrabPose pose2;
      GrabSolve(a, middle, wide, true, true, 0.6f, &theta, &k, &pose2);
      Near("a spread 10 times wider stops the zoom at 4", k, 4.0, 1e-6);
      float grabbed[3];
      GrabAnchorPoint(a, grabbed);
      Near("the camera stops 5 units from the grabbed point",
           Distance(pose2.position, grabbed), 5.0, 1e-3);
      const float narrow[3] = {0.61f, 0.0f, 0.0f};
      GrabSolve(a, middle, narrow, true, true, 0.6f, &theta, &k, &pose2);
      Near("a spread 5 times narrower stops the zoom at 0.25", k, 0.25, 1e-6);
    }
  }
}

// A point in front of a head: the origin moved right, up and forward along
// the head's own axes, in meters. The axes are the columns of the head matrix.
void PointOnHead(const float head[9], const float origin[3], float right,
                 float up, float forward, float units, float out[3]) {
  for (int i = 0; i < 3; ++i) {
    out[i] = origin[i] + (head[i * 3 + 0] * right + head[i * 3 + 1] * up +
                          head[i * 3 + 2] * forward) * units;
  }
}

// THE GESTURE PLANE. A square drawn in front of any head gives the
// same pixels. A head that turns during the draw does not bend it. The size
// follows gesture_size_cm.
void GestureChecks() {
  printf("\nA gesture in the air (M6)\n");
  const float units = 7.5f;
  const float render_h = 1440.0f;
  const float ppu = GesturePixelsPerUnit(render_h, 0.30f, units);
  Near("30 cm of hand is a third of a render 1440 high", 0.30 * units * ppu,
       480.0, 1e-3);
  Near("60 cm with a size of 60 cm is a third too",
       0.60 * units * GesturePixelsPerUnit(render_h, 0.60f, units), 480.0,
       1e-3);
  Check(GesturePixelsPerUnit(0.0f, 0.30f, units) == 0.0f,
        "no render height gives no scale", 0, 0);

  // A square of 20 cm, drawn with the depth wandering by 5 cm.
  const float square[5][3] = {{0.0f, 0.0f, 0.0f},
                              {0.2f, 0.0f, 0.03f},
                              {0.2f, -0.2f, -0.05f},
                              {0.0f, -0.2f, 0.02f},
                              {0.0f, 0.0f, 0.0f}};
  const float start_x = 1000.0f, start_y = 700.0f;
  float straight[9], turned[9], later[9];
  SetHead(0.0f, 0.0f, 0.0f, straight);
  SetHead(40.0f, -25.0f, 10.0f, turned);
  SetHead(-35.0f, 10.0f, -5.0f, later);
  const float origin_a[3] = {1.0f, -2.0f, 3.0f};
  const float origin_b[3] = {-2.5f, -1.0f, 2.0f};
  GesturePlane plane_a, plane_b;
  GesturePlaneFromHead(straight, origin_a, &plane_a);
  GesturePlaneFromHead(turned, origin_b, &plane_b);
  float worst_turned = 0.0f, worst_latched = 0.0f, worst_remade = 0.0f;
  float xs[5], ys[5];
  for (int i = 0; i < 5; ++i) {
    float p_a[3], p_b[3];
    PointOnHead(straight, origin_a, square[i][0], square[i][1], square[i][2],
                units, p_a);
    PointOnHead(turned, origin_b, square[i][0], square[i][1], square[i][2],
                units, p_b);
    float xa, ya, xb, yb;
    GesturePixel(plane_a, p_a, start_x, start_y, ppu, &xa, &ya);
    GesturePixel(plane_b, p_b, start_x, start_y, ppu, &xb, &yb);
    xs[i] = xa;
    ys[i] = ya;
    const float off = sqrtf((xa - xb) * (xa - xb) + (ya - yb) * (ya - yb));
    if (off > worst_turned) worst_turned = off;
    // The head turns to "later" after the second corner. The plane of the
    // start stays. Made again from the head of now, it would bend the path.
    if (i >= 2) {
      const float off_latched = off;
      if (off_latched > worst_latched) worst_latched = off_latched;
      GesturePlane remade;
      GesturePlaneFromHead(later, origin_b, &remade);
      float xr, yr;
      GesturePixel(remade, p_b, start_x, start_y, ppu, &xr, &yr);
      const float off_remade =
          sqrtf((xa - xr) * (xa - xr) + (ya - yr) * (ya - yr));
      if (off_remade > worst_remade) worst_remade = off_remade;
    }
  }
  Near("a square in front of a turned head gives the same pixels", worst_turned,
       0.0, 1e-2);
  Near("a head turn during the draw does not bend it (plane of the start)",
       worst_latched, 0.0, 1e-2);
  Check(worst_remade > 50.0f,
        "a plane made again from the turned head would bend it", worst_remade,
        50.0);
  Near("the path starts at the start pixel (x)", xs[0], start_x, 1e-3);
  Near("the path starts at the start pixel (y)", ys[0], start_y, 1e-3);
  Near("20 cm to the right is 320 pixels at a size of 30 cm", xs[1] - xs[0],
       320.0, 1e-2);
  Near("the depth adds nothing (y of the second corner)", ys[1], start_y,
       1e-2);
  Near("20 cm down is 320 pixels down the screen", ys[2] - ys[1], 320.0, 1e-2);
  Near("the square closes", sqrtf((xs[4] - xs[0]) * (xs[4] - xs[0]) +
                                  (ys[4] - ys[0]) * (ys[4] - ys[0])),
       0.0, 1e-3);
  // The spacing of the samples.
  Check(!GestureStepFar(100.0f, 100.0f, 120.0f, 100.0f, 36.0f),
        "a step of 20 pixels adds no sample", 0, 0);
  Check(GestureStepFar(100.0f, 100.0f, 130.0f, 120.0f, 36.0f),
        "a step of 36 pixels adds one", 1, 1);
  Check(!GestureStepFar(100.0f, 100.0f, 100.0f, 100.0f, 36.0f),
        "a still hand adds none", 0, 0);
  // The corner between two samples.
  const float l_shape[3][2] = {{20.0f, 0.0f}, {40.0f, 0.0f}, {40.0f, 20.0f}};
  Check(GestureCornerIndex(l_shape, 3, 0.0f, 0.0f, 40.0f, 40.0f, 9.0f) == 1,
        "the corner of an L goes in first", GestureCornerIndex(
            l_shape, 3, 0.0f, 0.0f, 40.0f, 40.0f, 9.0f), 1);
  const float straight_line[3][2] = {{10.0f, 1.0f}, {20.0f, -1.0f},
                                     {30.0f, 2.0f}};
  Check(GestureCornerIndex(straight_line, 3, 0.0f, 0.0f, 40.0f, 0.0f,
                           9.0f) == -1,
        "a line with a 2 pixel tremor has no corner", 0, 0);
  // An arc of a circle 480 pixels across, a chord of 36 pixels.
  float arc[5][2];
  const float radius = 240.0f, span = 36.0f / radius;
  for (int i = 0; i < 5; ++i) {
    const float a = span * (i + 1) / 6.0f;
    arc[i][0] = radius * sinf(a);
    arc[i][1] = radius * (1.0f - cosf(a));
  }
  Check(GestureCornerIndex(arc, 5, 0.0f, 0.0f, radius * sinf(span),
                           radius * (1.0f - cosf(span)), 9.0f) == -1,
        "a curve stays within the tolerance", 0, 0);
}

// THE TABLE, docs/tabletop.md, "The table camera". The real head_pose.cpp gives the
// lean at the life size scale, and table_math.h puts it on the table the way
// head_camera.cpp does: E = P0 + Lt * (lean * gain).
void TableEye(const float origin[3], const float basis[9], int right_cm,
              int up_cm, int forward_cm, float gain, float eye[3]) {
  HeadPoseSetFake(true, 0.0f, 0.0f, 0.0f);
  HeadPoseSetFakeLean(right_cm, up_cm, forward_cm);
  float center[3], position[3], orientation[4], head[9], lean[3];
  HeadPoseFakeCenter(center);
  HeadPoseFakeEye(0, position, orientation);
  HeadPoseUpdate(center, orientation);
  HeadPoseCurrent(head, lean);
  for (int i = 0; i < 3; ++i) lean[i] *= gain;
  table_math::Eye(origin, basis, lean, eye);
  HeadPoseSetFakeLean(0, 0, 0);
}

void TableChecks() {
  using namespace table_math;
  printf("\nThe table\n");
  // Land 1: its middle, and its farthest land cell.
  const float center[3] = {2235.0f, 0.0f, 2615.0f};
  const float table_room[3] = {0.0f, -0.40f, 0.80f};
  const float scale = FitScale(1345.0f, 1.5f, 0.10f);
  Near("Land 1 on a 1.5 m table, units per meter", scale, 1972.67, 0.05);
  Near("the edge is the farthest land plus 10 percent, units",
       CutRadius(scale, 1.5f), 1345.0 * 1.1, 0.01);
  const float gain = LeanGain(scale, HeadPoseScale());
  Near("the lean gain is s / 7.5", gain, scale / 7.5, 1e-3);

  const float headings[2] = {0.0f, 37.0f};
  for (int h = 0; h < 2; ++h) {
    printf("Table heading %.0f degrees\n", headings[h]);
    float basis[9];
    Basis(headings[h] * kDeg, basis);
    float at[3];
    RoomToWorld(center, basis, scale, table_room, table_room, at);
    Near("the table center in the room is the land point (x)", at[0],
         center[0], 1e-3);
    Near("the table center in the room is the land point (y)", at[1],
         center[1], 1e-3);
    Near("the table center in the room is the land point (z)", at[2],
         center[2], 1e-3);
    float origin[3];
    Origin(center, basis, scale, table_room, origin);
    const float back[2] = {center[0] - origin[0], center[2] - origin[2]};
    Near("the recenter pose is 80 cm of table back from the center, units",
         sqrtf(back[0] * back[0] + back[1] * back[1]), 0.80 * scale, 0.05);
    Near("and 40 cm of table above it, units", origin[1] - center[1],
         0.40 * scale, 0.01);
    Near("the recenter pose looks along the table heading (cosine)",
         (back[0] * basis[2] + back[1] * basis[8]) /
             sqrtf(back[0] * back[0] + back[1] * back[1]),
         1.0, 1e-5);

    float still[3], right[3], forward[3], down[3];
    TableEye(origin, basis, 0, 0, 0, gain, still);
    Near("a still head is at the recenter pose, units", Distance(still, origin),
         0.0, 1e-3);
    TableEye(origin, basis, 1, 0, 0, gain, right);
    Near("a head step of 1 cm moves the eye s / 100 units",
         Distance(right, still), 0.01 * scale, 0.01);
    Near("a step to the right goes along the table right (cosine)",
         ((right[0] - still[0]) * basis[0] + (right[1] - still[1]) * basis[3] +
          (right[2] - still[2]) * basis[6]) / Distance(right, still),
         1.0, 1e-5);
    TableEye(origin, basis, 0, 0, 1, gain, forward);
    Near("a step of 1 cm forward also moves it s / 100 units",
         Distance(forward, still), 0.01 * scale, 0.01);
    Near("toward the table center (cosine)",
         ((forward[0] - still[0]) * basis[2] +
          (forward[2] - still[2]) * basis[8]) / Distance(forward, still),
         1.0, 1e-5);
    TableEye(origin, basis, 0, -40, 80, gain, down);
    Near("a head moved to the table center is at the land point, units",
         Distance(down, center), 0.0, 0.05);
    const float room_point[3] = {0.01f, 0.0f, 0.0f};
    float mapped[3];
    RoomToWorld(center, basis, scale, table_room, room_point, mapped);
    Near("the head and the room map agree, units", Distance(mapped, right),
         0.0, 0.01);
  }

  printf("The edge of the table\n");
  const float radius = CutRadius(scale, 1.5f);
  // Blocks across the edge, and blocks all inside or all outside.
  Check(!BlockCut(center[0] + radius - 100.0f, center[2] - 80.0f, 160.0f,
                  center, radius, false),
        "a sea block across the edge, with its middle inside, stays", 0, 0);
  Check(BlockCut(center[0] + radius - 20.0f, center[2] - 80.0f, 160.0f,
                 center, radius, false),
        "a sea block with its middle outside goes", 1, 1);
  Check(!BlockCut(center[0] + radius - 20.0f, center[2] - 80.0f, 160.0f,
                  center, radius, true),
        "a land block in the same place stays", 0, 0);
  Check(BlockCut(center[0] + radius + 1.0f, center[2], 160.0f, center, radius,
                 true),
        "a land block all outside goes", 1, 1);
  Check(!BlockCut(center[0] - 80.0f, center[2] - 80.0f, 160.0f, center,
                  radius, false),
        "the block under the center stays", 0, 0);

  printf("The sea as a disc\n");
  float u, v;
  SeaUv(-kSeaHalf, kSeaHalf, 0.25f, 0.5f, &u, &v);
  Near("the game's first corner keeps its texture point (u)", u, 0.25, 1e-5);
  Near("the game's first corner keeps its texture point (v)", v, 0.5, 1e-5);
  SeaUv(kSeaHalf, -kSeaHalf, 0.25f, 0.5f, &u, &v);
  Near("the far corner is 50 repeats away (u)", u, 50.25, 1e-3);
  Near("the far corner is 50 repeats away (v)", v, 50.5, 1e-3);
  float positions[kFanVertices][3];
  unsigned indices[kFanSegments][3];
  SeaFan(center, radius, positions, indices);
  float worst = 0.0f;
  for (int i = 1; i < kFanVertices; ++i) {
    const float off = fabsf(Distance(positions[i], positions[0]) - radius);
    if (off > worst) worst = off;
  }
  Near("every rim point is at the edge, units", worst, 0.0, 0.01);
  const float quad[4][3] = {{-kSeaHalf, 0.0f, kSeaHalf},
                            {kSeaHalf, 0.0f, kSeaHalf},
                            {kSeaHalf, 0.0f, -kSeaHalf},
                            {-kSeaHalf, 0.0f, -kSeaHalf}};
  const bool quad_up = Facing(quad[0], quad[1], quad[2]) > 0.0f &&
                       Facing(quad[0], quad[2], quad[3]) > 0.0f;
  bool fan_up = true;
  for (int i = 0; i < kFanSegments; ++i) {
    if (!(Facing(positions[indices[i][0]], positions[indices[i][1]],
                 positions[indices[i][2]]) > 0.0f)) {
      fan_up = false;
    }
  }
  Check(quad_up && fan_up, "the disc faces the same way as the game's quad",
        fan_up ? 1 : 0, 1);
  Check(indices[kFanSegments - 1][2] == 1, "the last triangle closes the disc",
        indices[kFanSegments - 1][2], 1);

  printf("The key color outside the disc\n");
  // The eye camera at the recenter pose, pitched 40 degrees down, as the
  // engine builds the scaled matrix: columns right, up and forward, scaled by
  // the field of view terms, and t = -E * R. A 2560 by 1440 render at 110
  // degrees.
  const float hx = 1280.0f, hy = 720.0f;
  const float fx = 1.0f / tanf(55.0f * kDeg), fy = fx * 2560.0f / 1440.0f;
  float basis[9];
  Basis(-22.5f * kDeg, basis);
  float origin[3];
  Origin(center, basis, scale, table_room, origin);
  const float pitch = -40.0f * kDeg;
  float axes[3][3];
  for (int i = 0; i < 3; ++i) {
    axes[0][i] = basis[i * 3 + 0];
    axes[1][i] = basis[i * 3 + 1] * cosf(pitch) - basis[i * 3 + 2] * sinf(pitch);
    axes[2][i] = basis[i * 3 + 2] * cosf(pitch) + basis[i * 3 + 1] * sinf(pitch);
  }
  const float factor[3] = {fx, fy, 1.0f};
  float m[12];
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) m[r * 3 + c] = axes[c][r] * factor[c];
  }
  for (int c = 0; c < 3; ++c) {
    m[9 + c] = -(origin[0] * axes[c][0] + origin[1] * axes[c][1] +
                 origin[2] * axes[c][2]) * factor[c];
  }
  float at_center[3];
  ToCamera(m, center, at_center);
  Check(at_center[2] > 0.0f, "the table center is in front of the eye",
        at_center[2], 0);
  float camera[kFanSegments][3];
  for (int i = 0; i < kFanSegments; ++i) ToCamera(m, positions[i + 1], camera[i]);
  float clipped[kFanSegments + 1][3];
  const int n = ClipNear(camera, kFanSegments, 3.5f, clipped);
  Check(n == kFanSegments, "the whole rim is in front of the eye", n,
        kFanSegments);
  float points[kFanSegments + 1][2];
  float top = 1e30f, bottom = -1e30f;
  for (int i = 0; i < n; ++i) {
    ToScreen(clipped[i], hx, hy, points[i]);
    if (points[i][1] < top) top = points[i][1];
    if (points[i][1] > bottom) bottom = points[i][1];
  }
  const int first = top > 0.0f ? static_cast<int>(floorf(top)) : 0;
  const int last = bottom < 1440.0f ? static_cast<int>(ceilf(bottom)) : 1440;
  static float left[1440], right[1440];
  RowSpans(points, n, first, last - first, left, right);
  // The key and the sea split each row: a pixel whose middle is inside the
  // outline stays out of the key, and a pixel whose middle is outside takes
  // it. Middles within a thousandth of a pixel of an edge do not count.
  int covered = 0, left_out = 0, sea = 0;
  for (int y = first; y < last; ++y) {
    const float cy = y + 0.5f;
    const float key_before = KeyBefore(left[y - first]);
    const float key_from = KeyFrom(right[y - first]);
    const bool crossed = left[y - first] <= right[y - first];
    for (int x = 0; x < 2560; ++x) {
      const float cx = x + 0.5f;
      float nearest = 1e30f;
      for (int i = 0; i < n; ++i) {
        const float* a = points[i];
        const float* b = points[(i + 1) % n];
        const float ex = b[0] - a[0], ey = b[1] - a[1];
        const float side =
            (ex * (cy - a[1]) - ey * (cx - a[0])) / sqrtf(ex * ex + ey * ey);
        if (side < nearest) nearest = side;
      }
      if (fabsf(nearest) < 1e-3f) continue;
      const bool keyed = !crossed || x < key_before || x >= key_from;
      if (nearest > 0.0f) ++sea;
      if (nearest > 0.0f && keyed) ++covered;
      if (nearest < 0.0f && !keyed) ++left_out;
    }
  }
  Check(sea > 500000, "the disc covers a large part of the screen (pixels)",
        sea, 500000);
  Check(covered == 0,
        "no pixel with its middle inside the outline takes the key", covered,
        0);
  Check(left_out == 0, "every pixel with its middle outside takes the key",
        left_out, 0);
  // A polygon across the near plane keeps only the part in front.
  const float across[4][3] = {{-5.0f, 0.0f, -2.0f}, {5.0f, 0.0f, -2.0f},
                              {5.0f, 0.0f, 10.0f}, {-5.0f, 0.0f, 10.0f}};
  float front[5][3];
  const int kept = ClipNear(across, 4, 1.0f, front);
  float nearest = 1e30f;
  for (int i = 0; i < kept; ++i) if (front[i][2] < nearest) nearest = front[i][2];
  Check(kept == 4, "a square across the near plane keeps 4 points", kept, 4);
  Near("and none of them is behind it", nearest, 1.0, 1e-5);
}

// THE CONTROLLERS ON THE TABLE, docs/tabletop.md, "The hand on the table",
// "Moving the table by the grips" and "The ears on the table".
//
// One head sample of the fake head with a lean in cm, as head_pose.cpp gives
// it, and a fake hand at a point in cm from the recenter pose. The grip goes
// on the table the way controllers.cpp puts it there (TableHand).
struct TableSample {
  float eye[3];
  float grip[3];
  float t_grip[3];
};

TableSample TableGrip(const float origin[3], const float basis[9], float gain,
                      float yaw, const float lean_cm[3], float right_cm,
                      float up_cm, float forward_cm) {
  HeadPoseSetFake(true, yaw, -30.0f, 0.0f);
  HeadPoseSetFakeLean(static_cast<int>(lean_cm[0]),
                      static_cast<int>(lean_cm[1]),
                      static_cast<int>(lean_cm[2]));
  float center[3], position[3], orientation[4], head[9], lean[3], t_head[3];
  HeadPoseFakeCenter(center);
  HeadPoseFakeEye(0, position, orientation);
  HeadPoseUpdate(center, orientation);
  HeadPoseCurrent(head, lean);
  HeadPoseHeadInRoom(t_head);
  HeadPoseSetFakeLean(0, 0, 0);
  const float hand[3] = {right_cm * 0.01f, up_cm * 0.01f,
                         -forward_cm * 0.01f};
  const float still[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float m_grip[9];
  TableSample r = {};
  HeadPoseToRoom(hand, still, m_grip, r.t_grip);
  TableHand(origin, basis, gain, lean, t_head, r.t_grip, r.eye, r.grip);
  return r;
}

void TableHandChecks() {
  using table_math::TablePose;
  using table_math::TableGrab;
  printf("\nThe controllers on the table\n");
  const float center[3] = {2235.0f, 0.0f, 2615.0f};
  const float table_room[3] = {0.0f, -0.40f, 0.80f};
  const float scale = table_math::FitScale(1345.0f, 1.5f, 0.10f);
  const float gain = table_math::LeanGain(scale, HeadPoseScale());
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  const float headings[2] = {0.0f, 37.0f};
  for (int h = 0; h < 2; ++h) {
    printf("Table heading %.0f degrees\n", headings[h]);
    float basis[9], origin[3];
    table_math::Basis(headings[h] * kDeg, basis);
    table_math::Origin(center, basis, scale, table_room, origin);

    // A real hand on the table top, 20 cm right of the table center and
    // 30 cm past it, touches the land point the table map gives.
    const TableSample on = TableGrip(origin, basis, gain, 0.0f, zero, 20.0f,
                                     -40.0f, 110.0f);
    const float room_point[3] = {0.20f, -0.40f, 1.10f};
    float want[3];
    table_math::RoomToWorld(center, basis, scale, table_room, room_point, want);
    Near("a hand on the table top is at sea level, units", on.grip[1], 0.0,
         0.01);
    Near("and on the land point of the table map, units",
         Distance(on.grip, want), 0.0, 0.02);
    const float along_right = (on.grip[0] - center[0]) * basis[0] +
                              (on.grip[2] - center[2]) * basis[6];
    const float along_forward = (on.grip[0] - center[0]) * basis[2] +
                                (on.grip[2] - center[2]) * basis[8];
    Near("20 cm right of the table center is 0.2 s units right of C",
         along_right, 0.20 * scale, 0.02);
    Near("30 cm past it is 0.3 s units forward of C", along_forward,
         0.30 * scale, 0.02);
    const TableSample at_center = TableGrip(origin, basis, gain, 0.0f, zero,
                                            0.0f, -40.0f, 80.0f);
    Near("a hand at the table center touches C, units",
         Distance(at_center.grip, center), 0.0, 0.02);
    const TableSample above = TableGrip(origin, basis, gain, 0.0f, zero,
                                        20.0f, -30.0f, 110.0f);
    Near("a hand 10 cm over the table top is 0.1 s units over the land",
         above.grip[1], 0.10 * scale, 0.02);

    // A head that moves with a still hand moves nothing.
    const float leans[3][3] = {{1.0f, 0.0f, 0.0f},
                               {0.0f, 0.0f, 5.0f},
                               {-3.0f, -3.0f, 3.0f}};
    const char* const names[3] = {"1 cm right", "5 cm forward",
                                  "3 cm left, down and forward"};
    for (int m = 0; m < 3; ++m) {
      char what[160];
      const TableSample moved = TableGrip(origin, basis, gain, 20.0f,
                                          leans[m], 20.0f, -40.0f, 110.0f);
      const float lean_m = sqrtf(leans[m][0] * leans[m][0] +
                                 leans[m][1] * leans[m][1] +
                                 leans[m][2] * leans[m][2]) * 0.01f;
      sprintf_s(what, "head %s and turned: the eye moves s times it, units",
                names[m]);
      Near(what, Distance(moved.eye, on.eye), lean_m * scale, 0.05);
      sprintf_s(what, "head %s and turned: the Hand does not move, units",
                names[m]);
      Near(what, Distance(moved.grip, on.grip), 0.0, 0.02);
    }

    // The word "seek" on the table: the room point back from the world.
    float back[3];
    TableRoomPoint(origin, basis, gain, on.grip, back);
    Near("the table map backward gives the hand again, room units",
         Distance(back, on.t_grip), 0.0, 1e-4);
  }

  printf("The Hand's size on the table\n");
  const float size = table_math::HandSize(0.10f, scale);
  Near("a Hand of 10 cm at the Land 1 scale, size", size,
       0.10 * scale / 3.2, 1e-3);
  Near("is 10 cm long in the room, cm",
       table_math::kHandLength * size / scale * 100.0f, 10.0, 1e-3);
  Near("its grab radius, 0.6 of its length, is 6 cm, cm",
       0.6f * table_math::kHandLength * size / scale * 100.0f, 6.0, 1e-3);

  printf("The throw on the table\n");
  {
    float room[9];
    RoomBasis(90.0f, room);
    const float life = HeadPoseScale();
    const float table_gain = scale / life;
    // A still hand trembles 1 mm in 80 ms: 1.25 cm/s.
    const float tremble[3] = {0.0125f * life, 0.0f, 0.0f};
    float out[3], before = 0.0f;
    bool drop = false;
    TableThrowVelocity(tremble, tremble, room, 1.5f, table_gain, 124.0f, 2.0f,
                       out, &before, &drop);
    Check(drop, "a trembling hand sets the object down", drop ? 1 : 0, 1);
    Near("and sends zero", Length(out), 0.0, 1e-6);
    Check(before > 2.0f, "at the table scale that tremble would be a throw "
          "(units/s)", before, 2.0);
    // A hand at 1 m/s forward in the room.
    const float swing[3] = {0.0f, 0.2f * life, 1.0f * life};
    TableThrowVelocity(swing, swing, room, 1.5f, table_gain, 124.0f, 2.0f, out,
                       &before, &drop);
    Check(!drop, "a hand at 1 m/s throws", drop ? 1 : 0, 0);
    Near("the speed before the cut is the real speed at the table scale",
         before, Length(swing) * 1.5 * table_gain, 0.5);
    Near("and the game's cap still holds, units/s", Length(out), 124.0, 1e-3);
    Near("in the direction of the hand: room forward turned 90 degrees is "
         "world x (share)", out[0] / Length(out), 1.0 / sqrt(1.04), 1e-4);
    // At the life size scale the same tremble is also a drop by the game's
    // own rule, so the table adds no new rule.
    float life_out[3], life_before = 0.0f;
    ThrowVelocity(tremble, tremble, room, 1.5f, 124.0f, life_out,
                  &life_before);
    Check(life_before <= 2.0f, "off the table the tremble is under the game's "
          "set down speed (units/s)", life_before, 2.0);
  }

  printf("The throw from inside the influence\n");
  {
    // The player's influence is a disc of 100 units. A swing of 30 cm across
    // the land in 0.3 s starts 20 units inside its edge. One point a frame
    // at 60 frames a second, oldest first.
    const float edge = 100.0f;
    const int kPoints = 19;
    double times[kPoints];
    float along[kPoints];
    for (int i = 0; i < kPoints; ++i) times[i] = 10.0 + i / 60.0;
    const double now = times[kPoints - 1];
    const auto swing = [&](float units_per_meter) {
      for (int i = 0; i < kPoints; ++i) {
        along[i] = edge - 20.0f + 0.30f * units_per_meter * i / (kPoints - 1);
      }
    };
    const auto inside = [&](int i) { return along[i] < edge; };
    Near("a step of 2 cm moves the Hand over the land at the table scale "
         "(units)", 0.02f * scale, 39.5, 0.1);
    swing(scale);
    const int from = table_math::ThrowFromIndex(times, kPoints, now, 0.5,
                                                inside);
    Check(!inside(kPoints - 1), "the swing at the table scale ends outside",
          along[kPoints - 1], edge);
    // The Hand moves 32.9 units a frame, so only the first point is inside.
    Check(from == 0, "the throw goes from the last point inside", from, 0);
    Near("which is 20 units inside the edge (units)",
         from >= 0 ? edge - along[from] : 0.0f, 20.0, 1e-3);
    swing(7.5f);
    Check(inside(kPoints - 1), "at the life size scale the swing stays inside "
          "(units)", along[kPoints - 1], edge);
    Check(table_math::ThrowFromIndex(times, kPoints, now, 0.5, inside) ==
              kPoints - 1,
          "so the throw goes from the release point", kPoints - 1,
          kPoints - 1);
    for (int i = 0; i < kPoints; ++i) along[i] = edge + 50.0f;
    Check(table_math::ThrowFromIndex(times, kPoints, now, 0.5, inside) == -1,
          "a hand outside for the whole window keeps the game's rule", -1, -1);
    // Inside 0.6 s ago only: out of the window of half a second.
    double old_times[3] = {now - 0.6, now - 0.2, now};
    float old_along[3] = {edge - 10.0f, edge + 10.0f, edge + 20.0f};
    Check(table_math::ThrowFromIndex(old_times, 3, now, 0.5,
                                     [&](int i) {
                                       return old_along[i] < edge;
                                     }) == -1,
          "a point inside older than the window does not count", -1, -1);
    // Out, in, and out again: the newest point inside wins.
    float wave[3] = {edge + 10.0f, edge - 5.0f, edge + 10.0f};
    const double wave_times[3] = {now - 0.2, now - 0.1, now};
    Check(table_math::ThrowFromIndex(wave_times, 3, now, 0.5,
                                     [&](int i) { return wave[i] < edge; }) ==
              1,
          "the newest point inside wins", 1, 1);
  }

  printf("The grab of the table\n");
  {
    TablePose press = {{2235.0f, 0.0f, 2615.0f}, 25.0f * kDeg, scale};
    const float low = 40.0f, high = scale;
    // One hand, 30 cm right, 20 cm over the table top, 50 cm ahead.
    TableGrab one = {};
    one.hands = 1;
    one.pose = press;
    const float grip_a[3] = {0.30f, -0.20f, 0.50f};
    for (int i = 0; i < 3; ++i) one.middle[i] = grip_a[i];
    float land_a[3];
    table_math::FlatWorld(press, table_room, grip_a, land_a);
    float theta = 0.0f, k = 1.0f;
    TablePose pose;
    table_math::TableGrabSolve(one, table_room, grip_a, zero, true, true,
                               0.08f, low, high, &theta, &k, &pose);
    Near("a still hand leaves the center where it was, units",
         Distance(pose.center, press.center), 0.0, 1e-3);
    Near("and the heading, degrees", (pose.heading - press.heading) / kDeg,
         0.0, 1e-4);
    Near("and the scale", pose.scale, press.scale, 1e-3);
    const float right_10[3] = {0.40f, -0.20f, 0.50f};
    table_math::TableGrabSolve(one, table_room, right_10, zero, true, true,
                               0.08f, low, high, &theta, &k, &pose);
    float land_now[3];
    table_math::FlatWorld(pose, table_room, right_10, land_now);
    Near("a hand 10 cm right keeps its land point under it, units",
         Distance(land_now, land_a), 0.0, 0.02);
    Near("the land slid 0.1 s units", Distance(pose.center, press.center),
         0.10 * scale, 0.02);
    Near("the table center stays at sea level, units", pose.center[1], 0.0,
         1e-6);
    TablePose again;
    table_math::TableGrabSolve(one, table_room, right_10, zero, true, true,
                               0.08f, low, high, &theta, &k, &again);
    Near("a second solve with the same hand gives the same table, units",
         Distance(again.center, pose.center), 0.0, 0.0);
    const float up_10[3] = {0.30f, -0.10f, 0.50f};
    table_math::TableGrabSolve(one, table_room, up_10, zero, true, true, 0.08f,
                               low, high, &theta, &k, &pose);
    Near("a hand 10 cm up moves no land, units",
         Distance(pose.center, press.center), 0.0, 1e-3);

    // A head that moves with still hands: the room point of a hand does not
    // depend on the head.
    {
      const float lean_a[3] = {0.0f, 0.0f, 0.0f};
      const float lean_b[3] = {4.0f, -2.0f, 6.0f};
      float basis[9], origin[3];
      table_math::Basis(press.heading, basis);
      table_math::Origin(press.center, basis, press.scale, table_room, origin);
      const TableSample a = TableGrip(origin, basis, gain, 0.0f, lean_a, 30.0f,
                                      -20.0f, 50.0f);
      const TableSample b = TableGrip(origin, basis, gain, 25.0f, lean_b, 30.0f,
                                      -20.0f, 50.0f);
      Near("a head lean and turn with a still hand: the same room point",
           Distance(a.t_grip, b.t_grip), 0.0, 1e-5);
    }

    // Two hands, 40 cm apart, 20 cm over the table top.
    TableGrab two = {};
    two.hands = 2;
    two.pose = press;
    const float left_a[3] = {-0.20f, -0.20f, 0.60f};
    const float right_a[3] = {0.20f, -0.20f, 0.60f};
    for (int i = 0; i < 3; ++i) {
      two.middle[i] = 0.5f * (left_a[i] + right_a[i]);
      two.spread[i] = right_a[i] - left_a[i];
    }
    float mid_land[3], right_land[3];
    table_math::FlatWorld(press, table_room, two.middle, mid_land);
    table_math::FlatWorld(press, table_room, right_a, right_land);
    // Turn both hands 30 degrees clockwise about their middle.
    const float turn = 30.0f * kDeg;
    float left_t[3], right_t[3], middle[3], spread[3];
    for (int i = 0; i < 3; ++i) middle[i] = two.middle[i];
    const float half = 0.20f;
    right_t[0] = middle[0] + half * cosf(turn);
    right_t[2] = middle[2] - half * sinf(turn);
    left_t[0] = middle[0] - half * cosf(turn);
    left_t[2] = middle[2] + half * sinf(turn);
    right_t[1] = left_t[1] = middle[1];
    for (int i = 0; i < 3; ++i) spread[i] = right_t[i] - left_t[i];
    theta = 0.0f;
    k = 1.0f;
    table_math::TableGrabSolve(two, table_room, middle, spread, true, true,
                               0.08f, low, high, &theta, &k, &pose);
    Near("two hands turned 30 degrees turn the table heading back 30, "
         "degrees", table_math::Wrap(press.heading - pose.heading) / kDeg,
         30.0, 1e-3);
    float now_mid[3], now_right[3];
    table_math::FlatWorld(pose, table_room, middle, now_mid);
    table_math::FlatWorld(pose, table_room, right_t, now_right);
    Near("the land under the middle stays there, units",
         Distance(now_mid, mid_land), 0.0, 0.05);
    Near("and the land under the right hand turns with it, units",
         Distance(now_right, right_land), 0.0, 0.05);
    Near("the scale does not change", pose.scale, press.scale, 1e-2);

    // Spread the hands to twice the distance: the land grows two times.
    const float left_z[3] = {-0.40f, -0.20f, 0.60f};
    const float right_z[3] = {0.40f, -0.20f, 0.60f};
    for (int i = 0; i < 3; ++i) spread[i] = right_z[i] - left_z[i];
    theta = 0.0f;
    k = 1.0f;
    table_math::TableGrabSolve(two, table_room, two.middle, spread, true, true,
                               0.08f, low, high, &theta, &k, &pose);
    Near("hands twice as far apart halve the scale", pose.scale,
         press.scale * 0.5, 1e-2);
    table_math::FlatWorld(pose, table_room, two.middle, now_mid);
    table_math::FlatWorld(pose, table_room, right_z, now_right);
    Near("the land under the middle stays there, units",
         Distance(now_mid, mid_land), 0.0, 0.05);
    Near("and each hand keeps its own land point, units",
         Distance(now_right, right_land), 0.0, 0.05);

    // Far apart: the zoom stops at the lowest scale, and the land under the
    // middle still stays there.
    TableGrab near_scale = two;
    near_scale.pose.scale = 100.0f;
    float near_mid[3];
    table_math::FlatWorld(near_scale.pose, table_room, two.middle, near_mid);
    for (int i = 0; i < 3; ++i) spread[i] = 4.0f * (right_z[i] - left_z[i]);
    theta = 0.0f;
    k = 1.0f;
    table_math::TableGrabSolve(near_scale, table_room, two.middle, spread,
                               true, true, 0.08f, low, high, &theta, &k,
                               &pose);
    Near("the zoom stops at 40 units per meter", pose.scale, 40.0, 1e-4);
    table_math::FlatWorld(pose, table_room, two.middle, now_mid);
    Near("and the land under the middle stays there, units",
         Distance(now_mid, near_mid), 0.0, 0.02);
    // Hands together: the land shrinks no further than the start.
    for (int i = 0; i < 3; ++i) spread[i] = 0.5f * (right_a[i] - left_a[i]);
    theta = 0.0f;
    k = 1.0f;
    table_math::TableGrabSolve(two, table_room, two.middle, spread, true, true,
                               0.08f, low, high, &theta, &k, &pose);
    Near("hands together stop at the scale that fits the land", pose.scale,
         high, 1e-3);
  }

  printf("The ears on the table\n");
  {
    const float radius = table_math::CutRadius(scale, 1.5f);
    float at[3];
    const float over[3] = {center[0] + 100.0f, 800.0f, center[2] - 50.0f};
    table_math::InsideEdge(over, center, radius, at);
    Near("a head over the table keeps its place across (x)", at[0], over[0],
         1e-3);
    Near("a head over the table keeps its place across (z)", at[2], over[2],
         1e-3);
    Near("a head 800 units over land at 30 comes down to 430",
         table_math::EarsHeight(800.0f, 30.0f, 400.0f), 430.0, 1e-3);
    Near("a head 100 units over the land stays",
         table_math::EarsHeight(130.0f, 30.0f, 400.0f), 130.0, 1e-3);
    const float outside[3] = {center[0], 900.0f, center[2] - 2.0f * radius};
    table_math::InsideEdge(outside, center, radius, at);
    const float dx = at[0] - center[0], dz = at[2] - center[2];
    Near("a head past the edge gives ears on the edge, units",
         sqrtf(dx * dx + dz * dz), radius, 0.01);
    Near("on the side of the head (z)", at[2], center[2] - radius, 0.01);
  }
}

// THE GOD VIEW, docs/god-view.md. The table map with the table center right
// under the recenter pose, 60 cm down, and no edge. The waist point is on
// land 45.5 units high, and the land under the head is 40 units high.
void GodChecks() {
  using table_math::TablePose;
  using table_math::TableGrab;
  printf("\nThe God view\n");
  // A float step at x 2300 is 0.00024 units, which is 0.003 cm at the life
  // size scale. So a place in the room is checked to 0.01 cm, a tenth of a
  // millimeter.
  const double kGodCm = 0.01;
  float room[3];
  table_math::GodRoom(0.60f, room);
  const float life = HeadPoseScale();
  const float highest = 4000.0f;
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  const float scales[3] = {life, 100.0f, 2000.0f};
  for (int n = 0; n < 3; ++n) {
    const float s = scales[n];
    printf("At %.1f units per meter\n", s);
    const TablePose pose = {{2300.0f, 45.5f, 2600.0f}, 30.0f * kDeg, s};
    float basis[9], origin[3];
    table_math::Basis(pose.heading, basis);
    table_math::Origin(pose.center, basis, s, room, origin);
    Near("the recenter pose is 60 cm of the room over the waist point, cm",
         (origin[1] - pose.center[1]) / s * 100.0f, 60.0, 1e-3);
    Near("and straight over it, units",
         sqrtf((origin[0] - pose.center[0]) * (origin[0] - pose.center[0]) +
               (origin[2] - pose.center[2]) * (origin[2] - pose.center[2])),
         0.0, 1e-3);

    // The Hand is the real hand: 19 cm at every scale.
    const float size = table_math::HandSize(0.19f, s);
    Near("the Hand is as long as the real hand, cm",
         table_math::kHandLength * size / s * 100.0f, 19.0, 1e-3);

    // A real hand at the waist touches the waist point's height, and a head
    // that moves with a still hand moves nothing.
    const float gain = table_math::LeanGain(s, life);
    const TableSample on = TableGrip(origin, basis, gain, 0.0f, zero, 20.0f,
                                     -60.0f, 30.0f);
    Near("a hand 60 cm under the recenter pose is at the waist height, cm",
         (on.grip[1] - pose.center[1]) / s * 100.0f, 0.0, 1e-3);
    const float leans[3][3] = {{2.0f, 0.0f, 0.0f},
                               {0.0f, -3.0f, 5.0f},
                               {-4.0f, 2.0f, -3.0f}};
    for (int m = 0; m < 3; ++m) {
      const TableSample moved = TableGrip(origin, basis, gain, 25.0f,
                                          leans[m], 20.0f, -60.0f, 30.0f);
      Near("a head lean and turn with a still hand: how far the Hand moves, "
           "cm",
           Distance(moved.grip, on.grip) / s * 100.0f, 0.0, 1e-3);
    }

    // A zoom step keeps the land under the head where it is: in the world,
    // in the room, and at its height in the room.
    const float head[3] = {0.12f, 0.03f, 0.08f};
    const float ground_y = 40.0f;
    float at[3];
    table_math::PoseWorld(pose, room, head, at);
    const float pivot[3] = {at[0], ground_y, at[2]};
    float pivot_room[3];
    table_math::PoseRoom(pose, room, pivot, pivot_room);
    Near("the land under the head is straight under it (right, cm)",
         (pivot_room[0] - head[0]) * 100.0f, 0.0, kGodCm);
    Near("the land under the head is straight under it (forward, cm)",
         (pivot_room[2] - head[2]) * 100.0f, 0.0, kGodCm);
    const float ratios[2] = {1.0f / 1.5f, 1.5f};
    const char* const names[2] = {"in", "out"};
    for (int r = 0; r < 2; ++r) {
      char what[160];
      TablePose next;
      const float used = table_math::GodZoom(pose, room, head, ground_y,
                                             ratios[r], life, highest, &next);
      float want = s * ratios[r];
      if (want < life) want = life;
      if (want > highest) want = highest;
      sprintf_s(what, "a zoom step %s: the scale", names[r]);
      Near(what, next.scale, want, 1e-3 * want);
      float again[3];
      table_math::PoseWorld(next, room, pivot_room, again);
      sprintf_s(what, "a zoom step %s keeps the land under the head at its "
                "room point, cm", names[r]);
      Near(what, Distance(again, pivot) / next.scale * 100.0f, 0.0, kGodCm);
      sprintf_s(what, "a zoom step %s keeps the height of that land in the "
                "room, cm", names[r]);
      Near(what,
           table_math::GroundInRoom(next, room, ground_y) * 100.0f,
           table_math::GroundInRoom(pose, room, ground_y) * 100.0f, 1e-3);
      sprintf_s(what, "a zoom step %s keeps the heading, degrees", names[r]);
      Near(what, (next.heading - pose.heading) / kDeg, 0.0, 1e-5);
      if (want == s) {
        Near("at the limit the zoom does nothing (ratio)", used, 1.0, 1e-6);
        Near("and the waist point stays, units",
             Distance(next.center, pose.center), 0.0, 1e-4);
      }
    }

    // A step of the ground: the land goes up 5 cm in the room, and back.
    TablePose up;
    table_math::GodRaise(pose, 0.05f, &up);
    Near("a ground step up raises the land under the head 5 cm, cm",
         (table_math::GroundInRoom(up, room, ground_y) -
          table_math::GroundInRoom(pose, room, ground_y)) * 100.0f, 5.0,
         1e-3);
    float point_before[3], point_after[3];
    table_math::PoseRoom(pose, room, pivot, point_before);
    table_math::PoseRoom(up, room, pivot, point_after);
    Near("every land point goes 5 cm up in the room, cm",
         (point_after[1] - point_before[1]) * 100.0f, 5.0, 1e-3);
    Near("and not across, cm",
         (fabsf(point_after[0] - point_before[0]) +
          fabsf(point_after[2] - point_before[2])) * 100.0f, 0.0, 1e-3);
    Near("a ground step keeps the scale", up.scale, s, 0.0);
    TablePose back;
    table_math::GodRaise(up, -0.05f, &back);
    Near("a step down after a step up puts the ground back, cm",
         (back.center[1] - pose.center[1]) / s * 100.0f, 0.0, 1e-3);

    // A snap turn keeps the head where it is in the world.
    TablePose turned;
    table_math::GodTurn(pose, room, head, 90.0f * kDeg, &turned);
    float head_before[3], head_after[3];
    table_math::PoseWorld(pose, room, head, head_before);
    table_math::PoseWorld(turned, room, head, head_after);
    Near("a snap turn keeps the head's world point, cm",
         Distance(head_after, head_before) / s * 100.0f, 0.0, 1e-3);
    const float ahead[3] = {head[0], head[1], head[2] + 1.0f};
    const float right[3] = {head[0] + 1.0f, head[1], head[2]};
    float ahead_after[3], right_before[3];
    table_math::PoseWorld(turned, room, ahead, ahead_after);
    table_math::PoseWorld(pose, room, right, right_before);
    Near("a turn right of 90 degrees puts ahead where the right was, cm",
         Distance(ahead_after, right_before) / s * 100.0f, 0.0, kGodCm);
    Near("a snap turn keeps the height of the waist point, units",
         turned.center[1], pose.center[1], 1e-3);

    // A pan by one hand at the waist keeps the grabbed land in the hand.
    TableGrab one = {};
    one.hands = 1;
    one.pose = pose;
    const float grip_a[3] = {0.25f, -0.55f, 0.35f};
    for (int i = 0; i < 3; ++i) one.middle[i] = grip_a[i];
    float land_a[3];
    table_math::FlatWorld(pose, room, grip_a, land_a);
    float theta = 0.0f, k = 1.0f;
    TablePose panned;
    const float grip_b[3] = {0.10f, -0.55f, 0.50f};
    table_math::TableGrabSolve(one, room, grip_b, zero, true, true, 0.08f,
                               life, highest, &theta, &k, &panned);
    float land_b[3];
    table_math::FlatWorld(panned, room, grip_b, land_b);
    Near("a pan keeps the grabbed land in the hand, cm",
         Distance(land_b, land_a) / s * 100.0f, 0.0, 1e-3);
    Near("the pan moves the land as far as the hand, cm",
         Distance(panned.center, pose.center) / s * 100.0f,
         sqrtf(0.15f * 0.15f + 0.15f * 0.15f) * 100.0f, kGodCm);
    Near("a pan keeps the height of the waist point, units", panned.center[1],
         pose.center[1], 0.0);
    const float grip_up[3] = {0.25f, -0.35f, 0.35f};
    table_math::TableGrabSolve(one, room, grip_up, zero, true, true, 0.08f,
                               life, highest, &theta, &k, &panned);
    Near("a hand that moves up moves no land across, units",
         Distance(panned.center, pose.center), 0.0, 1e-3);

    // THE VERTICAL GRAB of the God view (docs/god-view.md, "The grab of the land"): one
    // hand also moves the land up and down, one to one, from the press.
    {
      TablePose raised;
      table_math::TableGrabSolve(one, room, grip_up, zero, true, true, 0.08f,
                                 life, highest, &theta, &k, &raised, true);
      Near("a hand 20 cm up raises the land 20 cm in the room, cm",
           (table_math::GroundInRoom(raised, room, 0.0f) -
            table_math::GroundInRoom(pose, room, 0.0f)) * 100.0f, 20.0, 1e-3);
      Near("and moves no land across, units",
           fabsf(raised.center[0] - pose.center[0]) +
               fabsf(raised.center[2] - pose.center[2]), 0.0, 1e-3);
      Near("and keeps the scale and the heading",
           fabsf(raised.scale - pose.scale) +
               fabsf(raised.heading - pose.heading), 0.0, 1e-6);
      const float grip_down[3] = {0.25f, -0.75f, 0.35f};
      TablePose lowered;
      table_math::TableGrabSolve(one, room, grip_down, zero, true, true, 0.08f,
                                 life, highest, &theta, &k, &lowered, true);
      Near("a hand 20 cm down lowers the land 20 cm in the room, cm",
           (table_math::GroundInRoom(lowered, room, 0.0f) -
            table_math::GroundInRoom(pose, room, 0.0f)) * 100.0f, -20.0, 1e-3);
      TablePose still;
      table_math::TableGrabSolve(one, room, grip_a, zero, true, true, 0.08f,
                                 life, highest, &theta, &k, &still, true);
      Near("a still hand moves nothing, units",
           Distance(still.center, pose.center), 0.0, 0.0);
      // A move across and up at once: the move across is the same as
      // without the vertical grab.
      const float grip_diag[3] = {0.10f, -0.35f, 0.50f};
      TablePose flat, both;
      table_math::TableGrabSolve(one, room, grip_diag, zero, true, true, 0.08f,
                                 life, highest, &theta, &k, &flat, false);
      table_math::TableGrabSolve(one, room, grip_diag, zero, true, true, 0.08f,
                                 life, highest, &theta, &k, &both, true);
      Near("a move across and up keeps the same move across, units",
           fabsf(both.center[0] - flat.center[0]) +
               fabsf(both.center[2] - flat.center[2]), 0.0, 1e-4);
      Near("and the land goes up by the hand's rise, cm",
           (table_math::GroundInRoom(both, room, 0.0f) -
            table_math::GroundInRoom(pose, room, 0.0f)) * 100.0f, 20.0, 1e-3);
      // The mouse pan passes a hand on the level plane of the press, so the
      // vertical grab changes nothing for it.
      const float level[3] = {0.05f, -0.55f, 0.80f};
      TablePose mouse_a, mouse_b;
      table_math::TableGrabSolve(one, room, level, zero, false, false, 0.08f,
                                 life, highest, &theta, &k, &mouse_a, false);
      table_math::TableGrabSolve(one, room, level, zero, false, false, 0.08f,
                                 life, highest, &theta, &k, &mouse_b, true);
      Near("a hand on the level plane gives the same height, units",
           mouse_a.center[1], mouse_b.center[1], 0.0);
    }

    // Two hands that spread to twice the distance: the land grows two times,
    // down to the life size scale, and the land under the middle stays.
    TableGrab two = {};
    two.hands = 2;
    two.pose = pose;
    const float left_a[3] = {-0.15f, -0.55f, 0.40f};
    const float right_a[3] = {0.15f, -0.55f, 0.40f};
    float spread[3];
    for (int i = 0; i < 3; ++i) {
      two.middle[i] = 0.5f * (left_a[i] + right_a[i]);
      two.spread[i] = right_a[i] - left_a[i];
      spread[i] = 2.0f * two.spread[i];
    }
    float mid_before[3], mid_after[3];
    table_math::FlatWorld(pose, room, two.middle, mid_before);
    theta = 0.0f;
    k = 1.0f;
    TablePose spread_pose;
    table_math::TableGrabSolve(two, room, two.middle, spread, true, true,
                               0.08f, life, highest, &theta, &k,
                               &spread_pose);
    const float half = s * 0.5f < life ? life : s * 0.5f;
    Near("hands twice as far apart halve the scale, down to life size",
         spread_pose.scale, half, 1e-3 * s);
    table_math::FlatWorld(spread_pose, room, two.middle, mid_after);
    Near("and the land under the middle of the hands stays, cm",
         Distance(mid_after, mid_before) / spread_pose.scale * 100.0f, 0.0,
         1e-3);
    Near("and the waist point keeps its height, units", spread_pose.center[1],
         pose.center[1], 0.0);
    // Two hands keep the height of the press, also with the vertical grab.
    {
      float up_spread[3] = {spread[0], spread[1], spread[2]};
      float up_middle[3] = {two.middle[0], two.middle[1] + 0.30f,
                            two.middle[2]};
      TablePose two_up;
      theta = 0.0f;
      k = 1.0f;
      table_math::TableGrabSolve(two, room, up_middle, up_spread, true, true,
                                 0.08f, life, highest, &theta, &k, &two_up,
                                 true);
      Near("two hands that rise keep the height of the press, units",
           two_up.center[1], pose.center[1], 0.0);
    }
  }

  // THE GROUND LIMITS OF THE GRAB (GodGroundHeight). The land under the head
  // stays 10 cm to 2.5 m below the head. At a limit the land stops, and the
  // result never jumps and never goes farther out than it is.
  {
    const float room_y = -0.55f, head_y = 0.0f, land = 0.0f;
    const float scale = 100.0f;
    const float near_m = 0.10f, far_m = 2.50f;
    // C.y that puts the land at d meters below the head.
    auto height_for = [&](float d) {
      return land - ((head_y - d) - room_y) * scale;
    };
    auto under_of = [&](float c) {
      return room_y + (land - c) / scale;
    };
    const float inside = height_for(1.0f);
    Near("a request inside the range is used, units",
         table_math::GodGroundHeight(inside - 3.0f, inside, land, room_y,
                                     scale, head_y, near_m, far_m),
         inside - 3.0f, 0.0);
    const float too_high = table_math::GodGroundHeight(
        height_for(0.0f), inside, land, room_y, scale, head_y, near_m, far_m);
    Near("a land that would pass 10 cm below the head stops there, cm",
         (head_y - under_of(too_high)) * 100.0f, 10.0, 1e-3);
    const float too_low = table_math::GodGroundHeight(
        height_for(4.0f), inside, land, room_y, scale, head_y, near_m, far_m);
    Near("a land that would pass 2.5 m below the head stops there, cm",
         (head_y - under_of(too_low)) * 100.0f, 250.0, 1e-3);
    // A sweep of requests through both limits: no jump, and in the range.
    float last = inside;
    float worst_jump = 0.0f, worst_over = 0.0f, worst_request = 0.0f;
    float last_wanted = inside;
    for (int i = 0; i <= 400; ++i) {
      const float d = 1.0f + 2.0f * sinf(i * 0.05f);  // -1 to 3 m
      const float wanted = height_for(d);
      const float got = table_math::GodGroundHeight(
          wanted, last, land, room_y, scale, head_y, near_m, far_m);
      const float under = head_y - under_of(got);
      if (under < near_m - 1e-4f) worst_over = near_m - under;
      if (under > far_m + 1e-4f) worst_over = under - far_m;
      const float step = fabsf(got - last);
      if (step > worst_jump) worst_jump = step;
      const float request = fabsf(wanted - last_wanted);
      if (request > worst_request) worst_request = request;
      last = got;
      last_wanted = wanted;
    }
    Near("a sweep through both limits stays in the range, cm",
         worst_over * 100.0f, 0.0, 1e-3);
    Check(worst_jump <= worst_request + 1e-3f,
          "and never moves more than the request does", worst_jump,
          worst_request);
    // From a land that is already too near the head (a hill), the height may
    // only move toward the range.
    const float hill = height_for(0.02f);
    Near("a land outside the range does not go farther out, units",
         table_math::GodGroundHeight(height_for(-0.5f), hill, land, room_y,
                                     scale, head_y, near_m, far_m),
         hill, 0.0);
    Near("but it may move toward the range, units",
         table_math::GodGroundHeight(height_for(0.05f), hill, land, room_y,
                                     scale, head_y, near_m, far_m),
         height_for(0.05f), 0.0);
    Near("and it stops at the edge of the range, cm",
         (head_y - under_of(table_math::GodGroundHeight(
              height_for(4.0f), hill, land, room_y, scale, head_y, near_m,
              far_m))) * 100.0f, 250.0, 1e-3);
    // A hand that went past the limit and comes back: the land follows the
    // hand again as soon as the request is inside, with no jump.
    Near("a request that comes back into the range is used at once, units",
         table_math::GodGroundHeight(inside, too_high, land, room_y, scale,
                                     head_y, near_m, far_m),
         inside, 0.0);
  }

  printf("A hill over the head\n");
  {
    // At the life size scale a hill of 100 units over the waist point is
    // 13 m over the head. The zoom turns about a point at most 10 cm under
    // the head, so zoomed out the hill top comes down.
    const TablePose pose = {{2300.0f, 20.0f, 2600.0f}, 0.0f, life};
    const float head[3] = {0.0f, 0.0f, 0.0f};
    const float hill = 120.0f;
    const float top_before = table_math::GroundInRoom(pose, room, hill);
    Check(top_before > 0.0f, "the hill top is over the head (m)", top_before,
          0.0);
    // Each step turns about the point 10 cm under the head, as
    // tabletop.cpp does for land that is higher than that. Each step out
    // brings the hill top 1.5 times nearer to that point.
    const float nearest = head[1] - 0.10f;
    TablePose next = pose;
    for (int i = 0; i < 14; ++i) {
      const float pivot_y = next.center[1] + (nearest - room[1]) * next.scale;
      TablePose step;
      table_math::GodZoom(next, room, head, pivot_y, 1.5f, life, highest,
                          &step);
      next = step;
    }
    const float top_after = table_math::GroundInRoom(next, room, hill);
    Near("after 14 steps out the hill top is 1.5^14 times nearer, m",
         top_after - nearest, (top_before - nearest) / pow(1.5, 14), 1e-4);
    Check(top_after < head[1], "and it is under the head (m)", top_after,
          head[1]);
  }

  printf("The game camera over the player\n");
  {
    const float ground[3] = {2300.0f, 40.0f, 2600.0f};
    float position[3];
    const float heading = 30.0f * kDeg;
    table_math::GameCameraOver(ground, heading, 400.0f, 50.0f * kDeg,
                               position);
    Near("the game camera is 400 units from the ground point",
         Distance(position, ground), 400.0, 1e-2);
    Near("50 degrees over it, units", position[1] - ground[1],
         400.0 * sin(50.0 * kDeg), 1e-2);
    float psi = 0.0f;
    table_math::Heading(position, ground, &psi);
    Near("and it looks along the heading of the view, degrees",
         table_math::Wrap(psi - heading) / kDeg, 0.0, 1e-3);
  }
}

// A unit vector from right, up and forward parts.
void UnitRoom(float right, float up, float forward, float out[3]) {
  const float length = sqrtf(right * right + up * up + forward * forward);
  out[0] = right / length;
  out[1] = up / length;
  out[2] = forward / length;
}

// The distance of a point from a ray, in the units of the point.
float FromRay(const float origin[3], const float dir[3], const float p[3]) {
  const float d[3] = {p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
  const float along = d[0] * dir[0] + d[1] * dir[1] + d[2] * dir[2];
  float off = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float part = d[i] - along * dir[i];
    off += part * part;
  }
  return sqrtf(off);
}

// THE MOUSE PAN OF THE GOD VIEW, docs/god-view.md, "The mouse pan". The land
// is level at the height of the waist point, 45.5 units, with a hill of 60
// units in one test. The mouse ray starts at the head of the press.
void GodPanChecks() {
  using table_math::TablePose;
  using table_math::TableGrab;
  printf("\nThe mouse pan of the God view\n");
  float room[3];
  table_math::GodRoom(0.60f, room);
  const float life = HeadPoseScale();
  const float highest = 4000.0f;
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  const float flat_y = 45.5f;
  const auto flat = [&](float, float) { return flat_y; };
  const float head[3] = {0.12f, 0.03f, 0.08f};
  const float scales[3] = {life, 100.0f, 2000.0f};
  for (int n = 0; n < 3; ++n) {
    const float s = scales[n];
    printf("At %.1f units per meter\n", s);
    const TablePose pose = {{2300.0f, flat_y, 2600.0f}, 30.0f * kDeg, s};
    float press_dir[3];
    UnitRoom(0.2f, -0.6f, 0.77f, press_dir);
    float hit[3];
    const bool found = table_math::RayLand(pose, room, head, press_dir, 2.0f,
                                           5600.0f, flat, hit);
    Check(found, "the press ray finds the land", found, 1);
    Near("the land point is at the height of the waist point in the room, cm",
         hit[1] * 100.0f, room[1] * 100.0f, 0.01);
    Near("and on the ray, cm", FromRay(head, press_dir, hit) * 100.0f, 0.0,
         0.01);

    // The pan: the pointer turns to the right and down, and the grabbed land
    // point goes onto the new ray.
    TableGrab one = {};
    one.hands = 1;
    one.pose = pose;
    for (int i = 0; i < 3; ++i) one.middle[i] = hit[i];
    float grabbed[3];
    table_math::PoseWorld(pose, room, hit, grabbed);
    float now_dir[3];
    UnitRoom(0.35f, -0.7f, 0.6f, now_dir);
    float hand[3];
    const bool level = table_math::RayLevel(head, now_dir, hit[1],
                                            5600.0f / s, hand);
    Check(level, "the new ray meets the level plane of the press", level, 1);
    float theta = 0.0f, k = 1.0f;
    TablePose panned;
    table_math::TableGrabSolve(one, room, hand, zero, false, false, 0.08f,
                               life, highest, &theta, &k, &panned);
    float grabbed_room[3];
    table_math::PoseRoom(panned, room, grabbed, grabbed_room);
    Near("the grabbed land is on the new ray, cm",
         FromRay(head, now_dir, grabbed_room) * 100.0f, 0.0, 0.05);
    Near("at the point where the ray meets the plane, cm",
         Distance(grabbed_room, hand) * 100.0f, 0.0, 0.05);
    Near("the pan keeps the scale", panned.scale, s, 0.0);
    Near("the pan keeps the heading, degrees",
         (panned.heading - pose.heading) / kDeg, 0.0, 1e-5);
    Near("the pan keeps the height of the waist point, units",
         panned.center[1], pose.center[1], 0.0);

    // A still mouse: the ray of the press again moves nothing.
    float still[3];
    table_math::RayLevel(head, press_dir, hit[1], 5600.0f / s, still);
    TablePose same;
    table_math::TableGrabSolve(one, room, still, zero, false, false, 0.08f,
                               life, highest, &theta, &k, &same);
    Near("a still mouse moves no land, units",
         Distance(same.center, pose.center), 0.0, 1e-3);
  }

  printf("A hill in front of the flat land\n");
  {
    const float s = 100.0f;
    const TablePose pose = {{2300.0f, flat_y, 2600.0f}, 0.0f, s};
    // At heading 0 the room forward is world +z. The hill is 60 units high
    // and 40 units across, 50 units ahead of the waist point.
    const auto hill = [&](float x, float z) {
      const float dx = x - 2300.0f, dz = z - 2650.0f;
      const float d = sqrtf(dx * dx + dz * dz);
      return d < 40.0f ? flat_y + 60.0f * (1.0f - d / 40.0f) : flat_y;
    };
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float dir[3];
    UnitRoom(0.0f, -0.4f, 1.0f, dir);
    float on_hill[3], on_flat[3];
    const bool found_hill = table_math::RayLand(pose, room, origin, dir, 2.0f,
                                                5600.0f, hill, on_hill);
    table_math::RayLand(pose, room, origin, dir, 2.0f, 5600.0f, flat,
                        on_flat);
    Check(found_hill, "the ray finds the hill", found_hill, 1);
    Check(Distance(on_hill, origin) < Distance(on_flat, origin),
          "the hill is nearer than the flat land behind it (m)",
          Distance(on_hill, origin), Distance(on_flat, origin));
    float world[3];
    table_math::PoseWorld(pose, room, on_hill, world);
    Near("the point is on the hill, units", world[1],
         hill(world[0], world[2]), 0.01);
  }

  printf("Rays that find no land\n");
  {
    const TablePose pose = {{2300.0f, flat_y, 2600.0f}, 0.0f, 100.0f};
    float up[3], out[3];
    UnitRoom(0.0f, 0.3f, 1.0f, up);
    Check(!table_math::RayLand(pose, room, head, up, 2.0f, 5600.0f, flat,
                               out),
          "a ray that goes up finds no land", 0, 0);
    Check(!table_math::RayLevel(head, up, room[1], 56.0f, out),
          "and it does not meet the level plane under the head", 0, 0);
    float graze[3];
    UnitRoom(0.0f, -0.001f, 1.0f, graze);
    Check(!table_math::RayLevel(head, graze, room[1], 56.0f, out),
          "a ray that meets the plane past the reach of the Hand is refused",
          0, 0);
    float level_dir[3];
    UnitRoom(0.0f, 0.0f, 1.0f, level_dir);
    Check(!table_math::RayLevel(head, level_dir, room[1], 56.0f, out),
          "a level ray does not meet a level plane", 0, 0);
  }
}

// The interface panels of docs/ui-layer.md, "Normal placement".
// THE TELEPORT (docs/god-view.md, "The teleport"): the arc, the hit, the move
// of the God view and of the immersive camera, the tube and the ring.
double Dist3(const float a[3], const float b[3]) {
  const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return sqrt(dx * dx + dy * dy + dz * dz);
}

// The distance from p to the segment a b.
double SegmentDistance(const float p[3], const float a[3], const float b[3]) {
  const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const double ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
  const double length2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
  double t = length2 > 0.0
                 ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / length2
                 : 0.0;
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  const double q[3] = {a[0] + t * ab[0] - p[0], a[1] + t * ab[1] - p[1],
                       a[2] + t * ab[2] - p[2]};
  return sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
}

void TeleportChecks() {
  using table_math::TablePose;
  printf("\nThe teleport arc\n");
  const float g = kArcGravity;
  const auto flat = [](float, float) { return 0.0f; };

  // The first point is the origin, and the first segment goes along the aim.
  {
    const float o[3] = {0.3f, 1.2f, -0.4f};
    const float yaw = 25.0f * kDeg, pitch = 20.0f * kDeg;
    const float d[3] = {sinf(yaw) * cosf(pitch), sinf(pitch),
                        cosf(yaw) * cosf(pitch)};
    const float speed = ArcLaunchSpeed(g, 10.0f);
    Near("the launch speed is sqrt(g R), m/s", speed, sqrt(9.8 * 10.0), 1e-4);
    const float t_end = ArcEndTime(o, d, speed, g, -0.5f, kArcMaxSeconds);
    float p[kArcSteps + 1][3];
    const int n = ArcPoints(o, d, speed, g, t_end, kArcSteps, p);
    Near("the arc has 49 points", n, 49.0, 0.0);
    Near("the first point is the origin, m", Dist3(p[0], o), 0.0, 1e-6);
    const float seg[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1],
                          p[1][2] - p[0][2]};
    const double length =
        sqrt(seg[0] * seg[0] + seg[1] * seg[1] + seg[2] * seg[2]);
    const double cosine =
        (seg[0] * d[0] + seg[1] * d[1] + seg[2] * d[2]) / length;
    Near("the first segment goes along the aim, degrees",
         acos(cosine > 1.0 ? 1.0 : cosine) / kDeg, 0.0, 1.5);
    double low = 1e9, high = -1e9;
    for (int i = 0; i + 1 < n; ++i) {
      const double step = sqrt(
          (p[i + 1][0] - p[i][0]) * (p[i + 1][0] - p[i][0]) +
          (p[i + 1][2] - p[i][2]) * (p[i + 1][2] - p[i][2]));
      if (step < low) low = step;
      if (step > high) high = step;
    }
    Near("equal time steps: the flat step is the same, m", high - low, 0.0,
         1e-5);
    Near("the last point is 0.5 m under the floor, m", p[n - 1][1], -0.5, 1e-3);
  }

  // A 45 degree aim over flat ground at the launch height lands at R.
  const float reaches[3] = {10.0f, 20.0f, 4.0f};
  for (int k = 0; k < 3; ++k) {
    const float reach = reaches[k];
    const float o[3] = {0.0f, 0.002f, 0.0f};
    const float d[3] = {0.0f, 0.70710678f, 0.70710678f};
    const float speed = ArcLaunchSpeed(g, reach);
    const float t_end = ArcEndTime(o, d, speed, g, -0.5f, kArcMaxSeconds);
    float p[kArcSteps + 1][3];
    const int n = ArcPoints(o, d, speed, g, t_end, kArcSteps, p);
    float hit[3];
    const bool got = ArcLandHit(p, n, flat, hit, nullptr);
    Check(got, "a 45 degree aim over flat ground has a hit", got ? 1 : 0, 1);
    Near("and it lands at the reach R within 1 percent, m", hit[2], reach,
         reach * 0.01);
    Near("on the line of the aim, m", hit[0], 0.0, 1e-5);
    Near("at the land height, m", hit[1], 0.0, 1e-6);
  }

  // A level aim from the height h lands at v0 * sqrt(2 h / g).
  {
    const float h = 1.5f;
    const float o[3] = {0.0f, h, 0.0f};
    const float d[3] = {0.0f, 0.0f, 1.0f};
    const float speed = ArcLaunchSpeed(g, 10.0f);
    const float t_end = ArcEndTime(o, d, speed, g, -0.5f, kArcMaxSeconds);
    float p[kArcSteps + 1][3];
    const int n = ArcPoints(o, d, speed, g, t_end, kArcSteps, p);
    float hit[3];
    ArcLandHit(p, n, flat, hit, nullptr);
    Near("a level aim from 1.5 m lands at v0 sqrt(2h/g), m", hit[2],
         speed * sqrt(2.0 * h / g), speed * sqrt(2.0 * h / g) * 0.01);
  }

  // The heading of the landing point is the heading of the aim.
  {
    const float o[3] = {0.0f, 1.4f, 0.0f};
    const float yaw = 37.0f * kDeg, pitch = 15.0f * kDeg;
    const float d[3] = {sinf(yaw) * cosf(pitch), sinf(pitch),
                        cosf(yaw) * cosf(pitch)};
    const float speed = ArcLaunchSpeed(g, 10.0f);
    const float t_end = ArcEndTime(o, d, speed, g, -0.5f, kArcMaxSeconds);
    float p[kArcSteps + 1][3];
    const int n = ArcPoints(o, d, speed, g, t_end, kArcSteps, p);
    float hit[3];
    ArcLandHit(p, n, flat, hit, nullptr);
    Near("the heading of the landing point is the heading of the aim, degrees",
         atan2(hit[0], hit[2]) / kDeg, 37.0, 0.01);
  }

  // A wall gives a hit at the wall, and no crossing gives false.
  {
    const auto wall = [](float x, float) { return x > 4.0f ? 100.0f : 0.0f; };
    const float o[3] = {0.0f, 1.5f, 0.0f};
    const float d[3] = {1.0f, 0.0f, 0.0f};
    const float speed = ArcLaunchSpeed(g, 10.0f);
    const float t_end = ArcEndTime(o, d, speed, g, -0.5f, kArcMaxSeconds);
    float p[kArcSteps + 1][3];
    const int n = ArcPoints(o, d, speed, g, t_end, kArcSteps, p);
    float hit[3];
    int segment = -1;
    const bool got = ArcLandHit(p, n, wall, hit, &segment);
    Check(got, "an arc into a wall has a hit", got ? 1 : 0, 1);
    Near("the hit is at the wall, m", hit[0], 4.0, 0.002);
    Near("at the height of the wall top, m", hit[1], 100.0, 1e-4);
    Check(segment >= 0 && segment < n - 1, "in a segment of the arc", segment,
          0);
    const auto deep = [](float, float) { return -50.0f; };
    const bool none = ArcLandHit(p, n, deep, hit, nullptr);
    Check(!none, "an arc over land that is far below has no hit", none ? 1 : 0,
          0);
    // A start under the land is a hit at the start.
    const auto high = [](float, float) { return 5.0f; };
    const bool under = ArcLandHit(p, n, high, hit, &segment);
    const float start[3] = {0.0f, 5.0f, 0.0f};
    Check(under && segment == 0 && Dist3(hit, start) < 1e-6,
          "a start under the land is a hit at the start", under ? 1 : 0, 1);
  }

  // The sky, the cap and the floor of the end time.
  {
    const float up70[3] = {0.0f, sinf(70.0f * kDeg), cosf(70.0f * kDeg)};
    const float up59[3] = {0.0f, sinf(59.0f * kDeg), cosf(59.0f * kDeg)};
    const float up61[3] = {0.0f, sinf(61.0f * kDeg), cosf(61.0f * kDeg)};
    Check(ArcIsSky(up70), "a 70 degree aim is the sky", 70, 60);
    Check(ArcIsSky(up61), "a 61 degree aim is the sky", 61, 60);
    Check(!ArcIsSky(up59), "a 59 degree aim is not the sky", 59, 60);
    const float high[3] = {0.0f, 1000.0f, 0.0f};
    const float level[3] = {0.0f, 0.0f, 1.0f};
    Near("the end time is 3 s at most",
         ArcEndTime(high, level, 9.9f, g, 0.0f, kArcMaxSeconds), 3.0, 1e-6);
    const float low[3] = {0.0f, -2.0f, 0.0f};
    Near("an arc that starts under the floor has no time",
         ArcEndTime(low, level, 9.9f, g, -0.5f, kArcMaxSeconds), 0.0, 0.0);
  }

  // The immersive camera: the eye goes over the target at the same height
  // over the land.
  {
    const float eye[3] = {10.0f, 5.0f, 20.0f};
    const float target[3] = {100.0f, 0.0f, 200.0f};
    float delta[3];
    CameraTeleportDelta(eye, 2.0f, target, 30.0f, delta);
    Near("the camera move: x", delta[0], 90.0, 1e-5);
    Near("the camera move: y, the change of the land height", delta[1], 28.0,
         1e-5);
    Near("the camera move: z", delta[2], 180.0, 1e-5);
    Near("the eye ends over the target, units", eye[0] + delta[0] - target[0],
         0.0, 1e-5);
    Near("at the same height over the land, units",
         eye[1] + delta[1] - 30.0, 5.0 - 2.0, 1e-5);
  }

  printf("\nThe teleport of the God view\n");
  {
    float room[3];
    table_math::GodRoom(0.60f, room);
    const float life = HeadPoseScale();
    const float scales[3] = {life, 100.0f, 2000.0f};
    for (int n = 0; n < 3; ++n) {
      const float s = scales[n];
      printf("At %.1f units per meter\n", s);
      const TablePose pose = {{2300.0f, 45.5f, 2600.0f}, 30.0f * kDeg, s};
      const float head_room[3] = {0.12f, 0.03f, 0.08f};
      float head[3];
      table_math::PoseWorld(pose, room, head_room, head);
      const float target[3] = {2500.0f, 0.0f, 2400.0f};
      const float ground_head = 40.0f, ground_target = 80.0f;
      TablePose to;
      table_math::GodTeleport(pose, room, head_room, ground_head, target,
                              ground_target, 0.10f, 2.50f, &to);
      float head_after[3];
      table_math::PoseWorld(to, room, head_room, head_after);
      Near("the new head point is over the target (x), units", head_after[0],
           target[0], 0.01);
      Near("the new head point is over the target (z), units", head_after[2],
           target[2], 0.01);
      const double before = table_math::GroundInRoom(pose, room, ground_head);
      const double after = table_math::GroundInRoom(to, room, ground_target);
      Near("the land under the head keeps its room height, mm", after * 1000.0,
           before * 1000.0, 1.0);
      Check(to.scale == pose.scale, "the scale does not change", to.scale,
            pose.scale);
      Check(to.heading == pose.heading, "the heading does not change",
            to.heading, pose.heading);
      // A target at the head gives the same pose.
      TablePose same;
      const float here[3] = {head[0], 0.0f, head[2]};
      table_math::GodTeleport(pose, room, head_room, ground_head, here,
                              ground_head, 0.10f, 2.50f, &same);
      Near("a target at the head gives the same waist point (x), units",
           same.center[0], pose.center[0], 0.01);
      Near("(y)", same.center[1], pose.center[1], 0.01);
      Near("(z)", same.center[2], pose.center[2], 0.01);
      // A hill over the head goes down to 10 cm under it.
      const float hill = pose.center[1] + (0.30f - room[1]) * s;
      TablePose clamped;
      table_math::GodTeleport(pose, room, head_room, hill, target,
                              ground_target, 0.10f, 2.50f, &clamped);
      Near("a hill over the head is kept 10 cm under it, mm",
           (head_room[1] -
            table_math::GroundInRoom(clamped, room, ground_target)) * 1000.0,
           100.0, 1.0);
    }
  }

  printf("\nThe tube and the ring of the teleport\n");
  {
    using overlay_math::Triangle;
    static Triangle tube[overlay_math::kTubeMaxTriangles];
    const float line[3][3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                              {2.0f, 0.0f, 0.0f}};
    int count = overlay_math::BuildTube(line, 3, 0.01f, tube,
                                        overlay_math::kTubeMaxTriangles);
    Near("a tube of 3 points has 24 triangles", count, 24.0, 0.0);
    double worst = 0.0;
    for (int t = 0; t < count; ++t) {
      for (int v = 0; v < 3; ++v) {
        const double r = hypot(tube[t].p[v][1], tube[t].p[v][2]);
        if (fabs(r - 0.01) > worst) worst = fabs(r - 0.01);
      }
    }
    Near("every corner of a straight tube is at the radius from the line, m",
         worst, 0.0, 1e-6);
    const float rise[2][3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    count = overlay_math::BuildTube(rise, 2, 0.01f, tube,
                                    overlay_math::kTubeMaxTriangles);
    worst = 0.0;
    for (int t = 0; t < count; ++t) {
      for (int v = 0; v < 3; ++v) {
        const double r = hypot(tube[t].p[v][0], tube[t].p[v][2]);
        if (fabs(r - 0.01) > worst) worst = fabs(r - 0.01);
      }
    }
    Near("a vertical tube has 12 triangles", count, 12.0, 0.0);
    Near("and its corners are at the radius, m", worst, 0.0, 1e-6);

    // A whole arc: 48 segments.
    const float o[3] = {0.0f, 1.4f, 0.0f};
    const float d[3] = {0.0f, 0.5f, 0.8660254f};
    const float speed = ArcLaunchSpeed(g, 10.0f);
    float p[kArcSteps + 1][3];
    const float t_end = ArcEndTime(o, d, speed, g, -0.5f, kArcMaxSeconds);
    const int n = ArcPoints(o, d, speed, g, t_end, kArcSteps, p);
    count = overlay_math::BuildTube(p, n, 0.006f, tube,
                                    overlay_math::kTubeMaxTriangles);
    Near("a tube of the whole arc has 576 triangles", count, 576.0, 0.0);
    double near_min = 1e9, near_max = 0.0, normal_error = 0.0;
    for (int t = 0; t < count; ++t) {
      for (int v = 0; v < 3; ++v) {
        double best = 1e9;
        for (int i = 0; i + 1 < n; ++i) {
          const double dist = SegmentDistance(tube[t].p[v], p[i], p[i + 1]);
          if (dist < best) best = dist;
        }
        if (best < near_min) near_min = best;
        if (best > near_max) near_max = best;
        const double length = sqrt(tube[t].n[v][0] * tube[t].n[v][0] +
                                   tube[t].n[v][1] * tube[t].n[v][1] +
                                   tube[t].n[v][2] * tube[t].n[v][2]);
        if (fabs(length - 1.0) > normal_error) {
          normal_error = fabs(length - 1.0);
        }
      }
    }
    Near("a corner of the arc tube is at most the radius from the line, mm",
         near_max * 1000.0, 6.0, 0.01);
    Check(near_min > 0.006 * 0.98,
          "and at least 98 percent of the radius from it", near_min * 1000.0,
          6.0);
    Near("the normals of the tube are unit vectors", normal_error, 0.0, 1e-5);
    Check(overlay_math::kTeleportMaxTriangles ==
              overlay_math::kTubeMaxTriangles + overlay_math::kRingMaxTriangles,
          "the teleport triangle budget", overlay_math::kTeleportMaxTriangles,
          overlay_math::kTubeMaxTriangles + overlay_math::kRingMaxTriangles);

    // The ring follows the land at the height of the land plus the lift.
    const auto slope = [](float x, float z) {
      return 10.0f + 0.1f * x - 0.05f * z;
    };
    static Triangle ring[overlay_math::kRingMaxTriangles];
    for (int valid = 0; valid < 2; ++valid) {
      const int c = overlay_math::BuildLandRing(
          100.0f, 200.0f, 15.0f, 20.0f, 2.0f, valid != 0, slope, ring,
          overlay_math::kRingMaxTriangles);
      Near(valid != 0 ? "a valid ring has a band and a disc: 72 triangles"
                      : "an invalid ring has a band and a cross: 52 triangles",
           c, valid != 0 ? 72.0 : 52.0, 0.0);
      double height_error = 0.0, band_low = 1e9, band_high = 0.0;
      double cross_far = 0.0;
      for (int t = 0; t < c; ++t) {
        for (int v = 0; v < 3; ++v) {
          const float* q = ring[t].p[v];
          const double e = fabs(q[1] - (slope(q[0], q[2]) + 2.0f));
          if (e > height_error) height_error = e;
          const double r = hypot(q[0] - 100.0, q[2] - 200.0);
          if (t < 48) {
            if (r < band_low) band_low = r;
            if (r > band_high) band_high = r;
          } else if (r > cross_far) {
            cross_far = r;
          }
        }
      }
      Near("every ring point is at the land height plus the lift, units",
           height_error, 0.0, 1e-3);
      Near("the band starts at the inner radius, units", band_low, 15.0, 1e-3);
      Near("and ends at the outer radius, units", band_high, 20.0, 1e-3);
      Check(cross_far <= 15.0 + 1e-3,
            "the disc or the cross is inside the inner radius", cross_far,
            15.0);
    }
  }
}

void PanelChecks() {
  printf("\nThe interface panels\n");
  const float pi = 3.14159265f;
  const float at[3] = {0.1f, 1.6f, -0.2f};
  const float straight[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float position[3], orientation[4];

  // Straight ahead at eye level: 1.4 m down -z, facing +z.
  PanelPose(at, straight, 1.4f, 0.0f, position, orientation);
  Near("a level panel is 1.4 m ahead (z)", position[2], -0.2 - 1.4, 1e-5);
  Near("at eye level (y)", position[1], 1.6, 1e-5);
  Near("and not turned (w)", orientation[3], 1.0, 1e-6);

  // 10 degrees below eye level: lower, nearer in z, and tilted to face the
  // head. The front of the quad, +z of its frame, points back at the anchor.
  const float down = -10.0f * pi / 180.0f;
  PanelPose(at, straight, 1.4f, down, position, orientation);
  Near("10 degrees down puts it 0.243 m low", position[1], 1.6 - 0.2431,
       1e-3);
  const float front[3] = {0.0f, 0.0f, 1.0f};
  float normal[3];
  QuatRotate(orientation, front, normal);
  const float back[3] = {at[0] - position[0], at[1] - position[1],
                         at[2] - position[2]};
  const float length = Length(back);
  Near("its front points at the head (x)", normal[0], back[0] / length, 1e-4);
  Near("its front points at the head (y)", normal[1], back[1] / length, 1e-4);
  Near("its front points at the head (z)", normal[2], back[2] / length, 1e-4);

  // A head turned 90 degrees to the left looks down -x.
  const float left[4] = {0.0f, sinf(pi / 4.0f), 0.0f, cosf(pi / 4.0f)};
  PanelPose(at, left, 1.2f, down, position, orientation);
  Near("turned left, the panel is ahead on -x", position[0],
       0.1 - 1.2 * cosf(down), 1e-4);
  Near("and not to the side (z)", position[2], -0.2, 1e-4);
  QuatRotate(orientation, front, normal);
  Near("its front points back along +x", normal[0], cosf(down), 1e-4);

  // The width of a panel: 16 px a degree, 960 px is 60 degrees.
  Near("960 px at 16 px a degree, 1.4 m away, is 1.617 m wide",
       PanelWidth(960.0f, 16.0f, 1.4f), 2.0 * 1.4 * tan(30.0 * pi / 180.0),
       1e-4);
  Near("no pixels give no width", PanelWidth(0.0f, 16.0f, 1.4f), 0.0, 0.0);
  Near("no density gives no width", PanelWidth(960.0f, 0.0f, 1.4f), 0.0, 0.0);
}

// A world to camera matrix as the game keeps it at 0x00EA9E40, for an eye at
// eye with the camera axes right, up and forward in world space. The field
// of view terms scale columns 0 and 1.
void GameCamera(const float eye[3], const float right[3], const float up[3],
                const float forward[3], float m[12]) {
  const float* axis[3] = {right, up, forward};
  const float factor[3] = {kScreen.fx, kScreen.fy, 1.0f};
  for (int c = 0; c < 3; ++c) {
    for (int r = 0; r < 3; ++r) m[r * 3 + c] = axis[c][r] * factor[c];
    m[9 + c] = -(eye[0] * axis[c][0] + eye[1] * axis[c][1] +
                 eye[2] * axis[c][2]) * factor[c];
  }
}

// The camera axes for a heading and a pitch, in degrees. Negative pitch
// looks down.
void CameraAxes(float yaw, float pitch, float right[3], float up[3],
                float forward[3]) {
  const float cy = cosf(yaw * kDeg), sy = sinf(yaw * kDeg);
  const float cp = cosf(pitch * kDeg), sp = sinf(pitch * kDeg);
  right[0] = cy;
  right[1] = 0.0f;
  right[2] = -sy;
  forward[0] = sy * cp;
  forward[1] = sp;
  forward[2] = cy * cp;
  // up = forward x right
  up[0] = forward[1] * right[2] - forward[2] * right[1];
  up[1] = forward[2] * right[0] - forward[0] * right[2];
  up[2] = forward[0] * right[1] - forward[1] * right[0];
}

void GlowChecks() {
  printf("\nThe glow at the Hand's point\n");
  const float near_plane = 3.5f;
  GlowDisc disc = {};

  // A camera at the origin that looks down +z.
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float x_axis[3] = {1.0f, 0.0f, 0.0f};
  const float y_axis[3] = {0.0f, 1.0f, 0.0f};
  const float z_axis[3] = {0.0f, 0.0f, 1.0f};
  float m[12];
  GameCamera(origin, x_axis, y_axis, z_axis, m);
  const float ahead[3] = {0.0f, 0.0f, 10.0f};
  Check(GlowProject(m, kScreen.hx, kScreen.hy, near_plane, ahead, 0.5f, 0.0f,
                    &disc),
        "a point 10 units ahead is drawn", 0, 0);
  Near("it is in the middle (x)", disc.x, 640.0, 1e-3);
  Near("it is in the middle (y)", disc.y, 360.0, 1e-3);
  Near("rhw is near / depth", disc.rhw, 0.35, 1e-6);
  Near("z is 1 - rhw", disc.z, 0.65, 1e-6);
  Near("the depth is 10 units", disc.depth, 10.0, 1e-5);
  Near("a radius of 0.5 is 45.7 px across", disc.rx, 0.5 * 1.4281 / 10 * 640,
       1e-3);
  Near("and 45.7 px up, so the disc is round", disc.ry,
       0.5 * 2.5389 / 10 * 360, 1e-3);

  const float aside[3] = {1.0f, 0.5f, 10.0f};
  GlowProject(m, kScreen.hx, kScreen.hy, near_plane, aside, 0.5f, 0.0f, &disc);
  Near("1 unit right is ProjectPoint's x", disc.x, (1 + 1.4281 * 0.1) * 640,
       1e-3);
  Near("0.5 units up is ProjectPoint's y", disc.y, (1 - 2.5389 * 0.05) * 360,
       1e-3);

  // The depth test uses a nearer point, and stops at the near plane.
  GlowProject(m, kScreen.hx, kScreen.hy, near_plane, ahead, 0.5f, 4.0f, &disc);
  Near("a pull of 4 tests at depth 6", disc.rhw, 3.5 / 6.0, 1e-6);
  Near("and the disc stays in the middle", disc.x, 640.0, 1e-3);
  Near("with the same size", disc.rx, 0.5 * 1.4281 / 10 * 640, 1e-3);
  GlowProject(m, kScreen.hx, kScreen.hy, near_plane, ahead, 0.5f, 20.0f,
              &disc);
  Near("a pull past the near plane stops there (rhw)", disc.rhw, 1.0, 1e-6);
  Near("and z is 0", disc.z, 0.0, 1e-6);

  const float close[3] = {0.0f, 0.0f, 3.0f};
  Check(!GlowProject(m, kScreen.hx, kScreen.hy, near_plane, close, 0.5f, 0.0f,
                     &disc),
        "a point nearer than the near plane is not drawn", 0, 0);
  const float behind[3] = {0.0f, 0.0f, -10.0f};
  Check(!GlowProject(m, kScreen.hx, kScreen.hy, near_plane, behind, 0.5f,
                     0.0f, &disc),
        "a point behind the eye is not drawn", 0, 0);
  const float far_right[3] = {100.0f, 0.0f, 10.0f};
  Check(!GlowProject(m, kScreen.hx, kScreen.hy, near_plane, far_right, 0.5f,
                     0.0f, &disc),
        "a point far outside the view is not drawn", 0, 0);

  // A turned camera: 30 degrees of heading, 40 degrees down, away from the
  // origin. A point a right, b up and d ahead of the eye.
  float right[3], up[3], forward[3];
  CameraAxes(30.0f, -40.0f, right, up, forward);
  const float eye[3] = {120.0f, 45.0f, -300.0f};
  GameCamera(eye, right, up, forward, m);
  const float a = 1.5f, b = -2.0f, d = 25.0f;
  float point[3];
  for (int i = 0; i < 3; ++i) {
    point[i] = eye[i] + a * right[i] + b * up[i] + d * forward[i];
  }
  Check(GlowProject(m, kScreen.hx, kScreen.hy, near_plane, point, 0.3f, 0.0f,
                    &disc),
        "a turned camera draws the point", 0, 0);
  Near("its x is (1 + fx * a / d) * hx", disc.x,
       (1 + 1.4281 * a / d) * 640, 1e-2);
  Near("its y is (1 - fy * b / d) * hy", disc.y,
       (1 - 2.5389 * b / d) * 360, 1e-2);
  Near("the depth is d", disc.depth, d, 1e-3);
  Near("the radius does not change with the turn", disc.rx,
       0.3 * 1.4281 / d * 640, 1e-3);

  // Two eyes. The God view at 100 units a meter, eyes 6.68 cm apart, a hand
  // 50 cm ahead. Each eye projects with its own camera, so the two discs are
  // apart by fx * e / d * hx and level with each other, and each one is on
  // the line from its eye through the point.
  const float e = 6.68f;
  const float hand_d = 50.0f;
  float center[3], left_eye[3], right_eye[3], hand[3];
  for (int i = 0; i < 3; ++i) {
    center[i] = eye[i];
    left_eye[i] = center[i] - 0.5f * e * right[i];
    right_eye[i] = center[i] + 0.5f * e * right[i];
    hand[i] = center[i] + 3.0f * right[i] - 10.0f * up[i] +
              hand_d * forward[i];
  }
  float m_left[12], m_right[12];
  GameCamera(left_eye, right, up, forward, m_left);
  GameCamera(right_eye, right, up, forward, m_right);
  GlowDisc left = {}, right_disc = {};
  Check(GlowProject(m_left, kScreen.hx, kScreen.hy, near_plane, hand, 1.6f,
                    0.0f, &left) &&
            GlowProject(m_right, kScreen.hx, kScreen.hy, near_plane, hand,
                        1.6f, 0.0f, &right_disc),
        "both eyes draw the hand's point", 0, 0);
  Near("the left disc is right of the right disc by fx * e / d * hx",
       left.x - right_disc.x, 1.4281 * e / hand_d * 640, 1e-2);
  Near("the two discs are level", left.y - right_disc.y, 0.0, 1e-3);
  Near("the left disc is on the left eye's line to the point", left.x,
       (1 + 1.4281 * (3.0 + 0.5 * e) / hand_d) * 640, 1e-2);
  Near("the right disc is on the right eye's line to the point",
       right_disc.x, (1 + 1.4281 * (3.0 - 0.5 * e) / hand_d) * 640, 1e-2);
  Near("both have the same depth", left.depth - right_disc.depth, 0.0, 1e-3);
}


// The corner of a mesh nearest to a point, as a distance.
float NearestCorner(const overlay_math::Triangle* mesh, int count,
                    const float point[3]) {
  float best = 1e30f;
  for (int t = 0; t < count; ++t) {
    for (int v = 0; v < 3; ++v) {
      const float d = Distance(mesh[t].p[v], point);
      if (d < best) best = d;
    }
  }
  return best;
}

// The extent of a mesh along a unit axis: the highest and the lowest of
// (corner - origin) . axis.
void Extent(const overlay_math::Triangle* mesh, int count,
            const float origin[3], const float axis[3], float* low,
            float* high) {
  *low = 1e30f;
  *high = -1e30f;
  for (int t = 0; t < count; ++t) {
    for (int v = 0; v < 3; ++v) {
      float along = 0.0f;
      for (int k = 0; k < 3; ++k) {
        along += (mesh[t].p[v][k] - origin[k]) * axis[k];
      }
      if (along < *low) *low = along;
      if (along > *high) *high = along;
    }
  }
}

void OverlayChecks() {
  printf("\nThe overlay renderer and its test hand\n");
  namespace om = overlay_math;
  static om::Triangle mesh[om::kHandTriangles];
  static om::Triangle other[om::kHandTriangles];
  static om::Triangle placed[om::kHandTriangles];

  // The curl from the controller values.
  Near("a trigger of 0 gives the rest curl", om::CurlOf(0.0f), 0.12, 1e-6);
  Near("a trigger of 1 gives a fist", om::CurlOf(1.0f), 1.0, 1e-6);
  Near("a value past 1 stops at a fist", om::CurlOf(1.7f), 1.0, 1e-6);
  Near("a value below 0 stops at the rest curl", om::CurlOf(-0.5f), 0.12,
       1e-6);

  // The size of the mesh.
  int count = om::BuildHand(0.0f, 0.0f, mesh);
  Check(count == om::kHandTriangles, "the hand has 330 triangles", count, 330);

  // A fingertip from an independent chain: the index finger at half curl.
  // Its bends add up at each joint, and a segment after a sum of bends A
  // points along (-sin A, -cos A, 0) from its knuckle at (0.13, -0.02, 0.155).
  {
    const float half = 0.5f;
    const float a1 = half * 75.0f * kDeg;
    const float a2 = a1 + half * 95.0f * kDeg;
    const float a3 = a2 + half * 55.0f * kDeg;
    const float want[3] = {
        0.13f - (0.200f * sinf(a1) + 0.135f * sinf(a2) + 0.105f * sinf(a3)),
        -0.02f - (0.200f * cosf(a1) + 0.135f * cosf(a2) + 0.105f * cosf(a3)),
        0.155f};
    float tip[3];
    om::FingerTip(1, half, tip);
    Near("the index tip at half curl (x)", tip[0], want[0], 1e-5);
    Near("the index tip at half curl (y)", tip[1], want[1], 1e-5);
    Near("the index tip stays in its own plane (z)", tip[2], want[2], 1e-6);
    count = om::BuildHand(half, 0.0f, mesh);
    Near("the index tip is a corner of the mesh",
         NearestCorner(mesh, count, tip), 0.0, 1e-6);
  }

  // The hand at a pose: a grip turned 30 degrees and pitched 40 degrees down,
  // in the God view at 100 units a meter. The hand is 19 cm, 19 units. The
  // grip axes are the columns of the rotation.
  {
    float right[3], up[3], forward[3];
    CameraAxes(30.0f, -40.0f, right, up, forward);
    float rotation[9];
    for (int r = 0; r < 3; ++r) {
      rotation[r * 3 + 0] = right[r];
      rotation[r * 3 + 1] = up[r];
      rotation[r * 3 + 2] = forward[r];
    }
    const float position[3] = {1400.0f, 60.0f, 2200.0f};
    const float length = 0.19f * 100.0f;
    const float curl = om::CurlOf(0.0f);
    count = om::BuildHand(curl, curl, mesh);
    float tip[3];
    om::FingerTip(1, curl, tip);
    float want[3];
    for (int k = 0; k < 3; ++k) {
      want[k] = position[k] + length * (tip[0] * right[k] + tip[1] * up[k] +
                                        tip[2] * forward[k]);
    }
    om::PlaceTriangles(mesh, count, position, rotation, length, false, placed);
    Near("the right index tip lands at grip + R * tip * length (units)",
         NearestCorner(placed, count, want), 0.0, 1e-3);
    float placed_tip[3];
    om::PlacePoint(position, rotation, length, false, tip, placed_tip);
    Near("and PlacePoint puts it there too (units)",
         Distance(placed_tip, want), 0.0, 1e-3);
    // The left hand is the mirror image in x of the grip frame.
    for (int k = 0; k < 3; ++k) {
      want[k] = position[k] + length * (-tip[0] * right[k] + tip[1] * up[k] +
                                        tip[2] * forward[k]);
    }
    om::PlaceTriangles(mesh, count, position, rotation, length, true, placed);
    Near("the left index tip is the mirror in x (units)",
         NearestCorner(placed, count, want), 0.0, 1e-3);
    // A normal turns with the grip and does not scale.
    const float n[3] = {1.0f, 0.0f, 0.0f};
    float turned[3];
    om::PlaceNormal(rotation, false, n, turned);
    Near("the back of the right hand faces the grip's +x",
         Distance(turned, right), 0.0, 1e-5);
    om::PlaceNormal(rotation, true, n, turned);
    Near("the back of the left hand faces the grip's -x",
         turned[0] * right[0] + turned[1] * right[1] + turned[2] * right[2],
         -1.0, 1e-5);

    // THE SIZE. The wrist to the tip of the middle finger, along the grip's
    // -y, is the real hand's 19 cm at every scale, with the fingers straight.
    count = om::BuildHand(0.0f, 0.0f, mesh);
    const float scales[3] = {7.5f, 100.0f, 2000.0f};
    for (int i = 0; i < 3; ++i) {
      const float l = 0.19f * scales[i];
      om::PlaceTriangles(mesh, count, position, rotation, l, false, placed);
      float low = 0.0f, high = 0.0f;
      Extent(placed, count, position, up, &low, &high);
      char what[128];
      sprintf_s(what, "at %.1f units a meter the hand is 19 cm long (cm)",
                scales[i]);
      Near(what, (high - low) / scales[i] * 100.0f, 19.0, 1e-2);
      Extent(placed, count, position, forward, &low, &high);
      sprintf_s(what, "and 6 to 12 cm across the knuckles (cm, %.1f units a "
                "meter)", scales[i]);
      const float wide = (high - low) / scales[i] * 100.0f;
      Check(wide > 6.0f && wide < 12.0f, what, wide, 9.0);
    }
  }

  // THE CURL. The trigger closes the index finger and nothing else, and the
  // grip closes the other three and not the index.
  {
    float open_tip[3], closed_tip[3], middle_closed[3];
    om::FingerTip(1, om::CurlOf(0.0f), open_tip);
    om::FingerTip(1, om::CurlOf(1.0f), closed_tip);
    om::FingerTip(2, om::CurlOf(1.0f), middle_closed);
    const float grip[3] = {0.0f, 0.0f, 0.0f};
    Check(Distance(open_tip, grip) > 0.4f,
          "an open index tip is far from the grip (hand lengths)",
          Distance(open_tip, grip), 0.45);
    // The handle's axis is the grip's z. A closed finger goes around the
    // front of the handle and comes back on the wrist side (+y).
    const float from_axis =
        sqrtf(closed_tip[0] * closed_tip[0] + closed_tip[1] * closed_tip[1]);
    Check(from_axis < 0.16f && closed_tip[1] > 0.0f,
          "a closed index tip wraps the handle (hand lengths from its axis)",
          from_axis, 0.14);
    count = om::BuildHand(om::CurlOf(1.0f), om::CurlOf(0.0f), mesh);
    Near("the trigger closes the index in the mesh",
         NearestCorner(mesh, count, closed_tip), 0.0, 1e-6);
    Check(NearestCorner(mesh, count, middle_closed) > 0.05f,
          "and leaves the middle finger open (hand lengths)",
          NearestCorner(mesh, count, middle_closed), 0.1);
    const int count2 =
        om::BuildHand(om::CurlOf(0.0f), om::CurlOf(1.0f), other);
    Near("the grip closes the middle finger in the mesh",
         NearestCorner(other, count2, middle_closed), 0.0, 1e-6);
    Near("and leaves the index open", NearestCorner(other, count2, open_tip),
         0.0, 1e-6);
    float thumb_a[3], thumb_b[3];
    om::FingerTip(0, 0.0f, thumb_a);
    om::FingerTip(0, 1.0f, thumb_b);
    Check(Distance(thumb_a, thumb_b) > 0.25, "the thumb moves with its curl",
          Distance(thumb_a, thumb_b), 0.25);
  }

  // THE LIGHT: from straight above, with an ambient of 0.35.
  {
    const float top[3] = {0.0f, 1.0f, 0.0f};
    const float under[3] = {0.0f, -1.0f, 0.0f};
    const float side[3] = {1.0f, 0.0f, 0.0f};
    Near("a face that looks up gets the full light",
         om::LightLevel(top, 0.35f), 1.0, 1e-6);
    Near("a face that looks down gets the ambient",
         om::LightLevel(under, 0.35f), 0.35, 1e-6);
    Near("a face that looks across gets the ambient",
         om::LightLevel(side, 0.35f), 0.35, 1e-6);
  }

  // THE NEAR PLANE, with a camera at the origin that looks down +z.
  {
    const float near_plane = 3.5f;
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const float x_axis[3] = {1.0f, 0.0f, 0.0f};
    const float y_axis[3] = {0.0f, 1.0f, 0.0f};
    const float z_axis[3] = {0.0f, 0.0f, 1.0f};
    float m[12];
    GameCamera(origin, x_axis, y_axis, z_axis, m);
    const float colors[3][3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                                {0.5f, 0.5f, 0.5f}};
    om::ScreenVertex out[6];
    const float hx = kScreen.hx, hy = kScreen.hy;

    // All in front: one triangle, at ProjectPoint's pixels and the game's
    // depth.
    const float front[3][3] = {{1.0f, 0.5f, 10.0f}, {-1.0f, 0.5f, 10.0f},
                               {0.0f, -1.0f, 20.0f}};
    float c[3][3];
    for (int v = 0; v < 3; ++v) om::ToCamera(m, front[v], c[v]);
    Check(om::ClipTriangle(c, colors, near_plane, hx, hy, out) == 1,
          "a triangle in front stays one triangle", 1, 1);
    Near("its corner is at ProjectPoint's x", out[0].x,
         (1 + 1.4281 * 0.1) * 640, 1e-2);
    Near("and y", out[0].y, (1 - 2.5389 * 0.05) * 360, 1e-2);
    Near("rhw is near / depth", out[2].rhw, 3.5 / 20.0, 1e-6);
    Near("z is 1 - rhw", out[2].z, 1.0 - 3.5 / 20.0, 1e-6);

    // All behind the near plane: nothing.
    const float behind[3][3] = {{1.0f, 0.5f, 3.0f}, {-1.0f, 0.5f, 1.0f},
                                {0.0f, -1.0f, -4.0f}};
    for (int v = 0; v < 3; ++v) om::ToCamera(m, behind[v], c[v]);
    Check(om::ClipTriangle(c, colors, near_plane, hx, hy, out) == 0,
          "a triangle behind the near plane draws nothing", 0, 0);

    // One corner behind: a quad of two triangles. The corner at depth 1.5
    // is behind, so the edge from depth 7.5 is cut at 4 / 6 of its length.
    const float one[3][3] = {{0.0f, 0.0f, 7.5f}, {0.0f, 1.0f, 1.5f},
                             {1.0f, 0.0f, 9.5f}};
    for (int v = 0; v < 3; ++v) om::ToCamera(m, one[v], c[v]);
    const int pieces = om::ClipTriangle(c, colors, near_plane, hx, hy, out);
    Check(pieces == 2, "one corner behind gives two triangles", pieces, 2);
    float most_rhw = 0.0f, least_z = 1.0f;
    int on_plane = 0;
    for (int v = 0; v < pieces * 3; ++v) {
      if (out[v].rhw > most_rhw) most_rhw = out[v].rhw;
      if (out[v].z < least_z) least_z = out[v].z;
      if (fabsf(out[v].rhw - 1.0f) < 1e-6f) ++on_plane;
    }
    Near("no corner is nearer than the near plane (rhw at most 1)", most_rhw,
         1.0, 1e-6);
    Near("so no z is below 0", least_z, 0.0, 1e-6);
    Check(on_plane >= 2, "the cut corners lie on the near plane (rhw 1)",
          on_plane, 2);
    // out[1] is the cut of the edge from corner 0 (color 0) to corner 1
    // (color 1), at t = (3.5 - 7.5) / (1.5 - 7.5).
    Near("the cut takes the color at the same share of the edge",
         out[1].color[0], 4.0 / 6.0, 1e-5);
    Near("and the position (y on the screen)", out[1].y,
         (1 - 2.5389 * (4.0 / 6.0) / 3.5) * 360, 1e-2);

    // Two corners behind: one smaller triangle.
    const float two[3][3] = {{0.0f, 0.0f, 7.5f}, {0.0f, 1.0f, 1.5f},
                             {1.0f, 0.0f, 2.5f}};
    for (int v = 0; v < 3; ++v) om::ToCamera(m, two[v], c[v]);
    Check(om::ClipTriangle(c, colors, near_plane, hx, hy, out) == 1,
          "two corners behind give one triangle", 1, 1);
    Near("with two corners on the near plane",
         (fabsf(out[1].rhw - 1.0f) < 1e-6f ? 1 : 0) +
             (fabsf(out[2].rhw - 1.0f) < 1e-6f ? 1 : 0),
         2.0, 0.0);

    // Off the screen: three corners past the right edge.
    const float right_out[3][3] = {{100.0f, 0.0f, 10.0f},
                                   {120.0f, 1.0f, 10.0f},
                                   {110.0f, -1.0f, 12.0f}};
    for (int v = 0; v < 3; ++v) om::ToCamera(m, right_out[v], c[v]);
    om::ClipTriangle(c, colors, near_plane, hx, hy, out);
    Check(om::OffScreen(out, hx, hy), "a triangle past the right edge is off",
          1, 1);
    const float across[3][3] = {{100.0f, 0.0f, 10.0f}, {0.0f, 1.0f, 10.0f},
                                {110.0f, -1.0f, 12.0f}};
    for (int v = 0; v < 3; ++v) om::ToCamera(m, across[v], c[v]);
    om::ClipTriangle(c, colors, near_plane, hx, hy, out);
    Check(!om::OffScreen(out, hx, hy), "a triangle across the edge is drawn",
          0, 0);
  }

  // TWO EYES. A corner of the hand 50 units ahead, seen from two eyes 6.68
  // units apart, is fx * e / d * hx pixels apart, and level.
  {
    float right[3], up[3], forward[3];
    CameraAxes(30.0f, -40.0f, right, up, forward);
    const float center[3] = {120.0f, 45.0f, -300.0f};
    const float e = 6.68f, d = 50.0f;
    float left_eye[3], right_eye[3], point[3];
    for (int i = 0; i < 3; ++i) {
      left_eye[i] = center[i] - 0.5f * e * right[i];
      right_eye[i] = center[i] + 0.5f * e * right[i];
      point[i] = center[i] + 3.0f * right[i] - 10.0f * up[i] + d * forward[i];
    }
    float m_left[12], m_right[12];
    GameCamera(left_eye, right, up, forward, m_left);
    GameCamera(right_eye, right, up, forward, m_right);
    const float white[3] = {1.0f, 1.0f, 1.0f};
    float c[3];
    om::ScreenVertex l = {}, r = {};
    om::ToCamera(m_left, point, c);
    om::Project(c, white, 3.5f, kScreen.hx, kScreen.hy, &l);
    om::ToCamera(m_right, point, c);
    om::Project(c, white, 3.5f, kScreen.hx, kScreen.hy, &r);
    Near("the two eyes put a hand corner fx * e / d * hx apart", l.x - r.x,
         1.4281 * e / d * 640, 1e-2);
    Near("level with each other", l.y - r.y, 0.0, 1e-3);
    Near("at the same depth", l.rhw - r.rhw, 0.0, 1e-6);
  }
}

}  // namespace

// THE GRIP HOLDS (docs/grab.md, "The grip holds"). A small driver: one step a
// frame, with the facts of that frame. A frame of the driver is 10 ms, so the
// times of the grip are whole frames.
const uint64_t kGripTestFrameMs = 10;

struct GripRun {
  GripHold hold;
  uint64_t frame;
  uint64_t ms;
  GripStepOut out;
};

// The frames of the driver in a time of the grip.
int GripTestFrames(uint64_t ms) {
  return static_cast<int>(ms / kGripTestFrameMs);
}

// The object in the Hand is in the Hand of the game too, and no press is a
// click: the facts of an object at rest. The checks of an object that moves
// set the other facts themselves (grip_checks.h, PressHoldChecks).
GripFacts Facts(bool holding, bool settled, bool spring = false,
                bool other = false, bool direct = true, bool take = false) {
  GripFacts f;
  f.direct = direct;
  f.holding = holding;
  f.settled = settled;
  f.spring = spring;
  f.other_held = other;
  f.take = take;
  f.mod_take = false;
  f.game_holds = holding;
  f.moving = false;
  f.click = false;
  f.press_hold = true;
  return f;
}

void GripStart(GripRun* run) {
  GripReset(&run->hold);
  run->frame = 0;
  run->ms = 0;
  run->out = GripStepOut{false, false, false, 0, false, false};
}

// Step frames frames with the same grip and facts. Returns the events of all
// the steps.
int GripFrames(GripRun* run, int frames, bool grip, const GripFacts& f) {
  int events = 0;
  for (int i = 0; i < frames; ++i) {
    ++run->frame;
    run->ms += kGripTestFrameMs;
    run->out = GripStep(&run->hold, grip, f, run->frame, run->ms);
    events |= run->out.events;
  }
  return events;
}

void GripChecks() {
  printf("\nThe grip holds\n");
  GripRun run;
  const GripFacts empty = Facts(false, true);
  const GripFacts taking = Facts(true, false);
  const GripFacts held = Facts(true, true);
  const GripFacts spring = Facts(true, true, true);

  // A take, a carry and a let go: the object comes 9 frames
  // after the press, the pick lock clears 5 frames later, and the throw
  // spring shows 2 frames after the click's press.
  GripStart(&run);
  GripFrames(&run, 3, false, empty);
  Check(!run.out.button, "an open grip on nothing sends no button", 0, 0);
  GripFrames(&run, 1, true, empty);
  Check(run.out.button && !run.out.click,
        "a close sends the game's button at once, as before", 1, 1);
  GripFrames(&run, 8, true, empty);
  int e = GripFrames(&run, 4, true, taking);
  Check(run.out.button && (e & kGripCarried) == 0,
        "the button stays down while the pick lock is set", 1, 1);
  e = GripFrames(&run, 1, true, held);
  Check((e & kGripCarried) != 0 && !run.out.button,
        "the pick up is over: the grip carries, and the button goes up", 0,
        0);
  Check(run.hold.phase == kGripCarry, "the phase is Carry",
        run.hold.phase, kGripCarry);
  e = GripFrames(&run, 30, true, held);
  Check(!run.out.button && e == 0,
        "a carry of 30 frames sends no button and changes nothing", e, 0);
  const uint64_t open_frame = run.frame + 1;
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripLettingGo) != 0 && (e & kGripClickDown) != 0,
        "the grip opens: the click presses on the same frame", e, 20);
  Check(run.out.button && run.out.click && run.out.opened,
        "the press is a click, and the step says to latch the throw now", 1,
        1);
  // A second step in the same frame sees the same grip.
  run.out = GripStep(&run.hold, false, held, run.frame, run.ms);
  Check(run.out.button && run.out.events == 0 && !run.out.opened,
        "a second step in one frame changes nothing", run.out.events, 0);
  e = GripFrames(&run, 1, false, held);
  Check(run.out.button, "the press holds until the throw spring shows", 1, 1);
  e = GripFrames(&run, 1, false, spring);
  Check((e & kGripClickUp) != 0 && !run.out.button && !run.out.click,
        "the spring shows: the click releases", 0, 0);
  Check(run.frame - open_frame == 2, "two frames from the opening to the "
        "release of the click", static_cast<double>(run.frame - open_frame),
        2);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripOpenedHeld) != 0 && !run.out.button,
        "the object is still in the Hand on the next frame: no button", 0, 0);
  e = GripFrames(&run, 1, false, empty);
  Check((e & kGripGone) != 0 && run.hold.phase == kGripPass,
        "the game let go: the grip is the button again", run.hold.phase,
        kGripPass);
  GripFrames(&run, 1, true, empty);
  Check(run.out.button, "the next close is a new press", 1, 1);

  // The click needs no spring: it lets go after kGripSpringMs.
  GripStart(&run);
  GripFrames(&run, 2, true, empty);
  GripFrames(&run, 2, true, held);
  e = GripFrames(&run, 1, false, held);
  const uint64_t pressed = run.ms;
  while (run.out.button && run.ms < pressed + 1000) {
    GripFrames(&run, 1, false, held);
  }
  Check(run.ms - pressed == kGripSpringMs,
        "with no spring the click lets go after kGripSpringMs",
        static_cast<double>(run.ms - pressed),
        static_cast<double>(kGripSpringMs));

  // The press itself gives the object away (for example to the Creature):
  // the empty Hand does not keep the press.
  GripStart(&run);
  GripFrames(&run, 2, true, empty);
  GripFrames(&run, 2, true, held);
  GripFrames(&run, 1, false, held);
  e = GripFrames(&run, 1, false, empty);
  Check((e & kGripClickUp) != 0 && !run.out.button,
        "the object left during the click: the button goes up at once", 0, 0);

  // The game refuses the drop (outside the influence): the object stays. A
  // close carries it again, and the next opening clicks again.
  GripStart(&run);
  GripFrames(&run, 2, true, empty);
  GripFrames(&run, 2, true, held);
  GripFrames(&run, 1, false, held);
  GripFrames(&run, 2, false, spring);
  e = GripFrames(&run, 5, false, held);
  Check(run.hold.phase == kGripOpenHeld && !run.out.button,
        "a refused drop leaves the object in the Hand with no button",
        run.hold.phase, kGripOpenHeld);
  e = GripFrames(&run, 1, true, held);
  Check((e & kGripRegrip) != 0 && !run.out.button,
        "a close takes it again with no button", 0, 0);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripClickDown) != 0 && run.out.button,
        "and the next opening clicks again", 1, 1);

  // A land drag: the trigger holds the other button. The click waits, so
  // the game never sees both buttons. A close while it waits carries again.
  const GripFacts dragging = Facts(true, true, false, true);
  GripStart(&run);
  GripFrames(&run, 2, true, empty);
  GripFrames(&run, 2, true, held);
  e = GripFrames(&run, 1, false, dragging);
  Check((e & kGripWaits) != 0 && !run.out.button,
        "with the other button held the click waits", 0, 0);
  GripFrames(&run, 10, false, dragging);
  Check(!run.out.button && run.hold.phase == kGripLetGo,
        "and keeps waiting while it is held", run.hold.phase, kGripLetGo);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripClickDown) != 0 && run.out.button,
        "the other button is up: the click presses", 1, 1);
  GripStart(&run);
  GripFrames(&run, 2, true, empty);
  GripFrames(&run, 2, true, held);
  GripFrames(&run, 3, false, dragging);
  e = GripFrames(&run, 1, true, dragging);
  Check((e & kGripRegrip) != 0 && run.hold.phase == kGripCarry &&
            !run.out.button,
        "a close while the click waits carries again", run.hold.phase,
        kGripCarry);

  // A Miracle, the leash and the Creature are never "holding": the grip is
  // the button, one to one, and the opening still latches the throw.
  GripStart(&run);
  e = GripFrames(&run, 20, true, empty);
  Check(run.out.button && e == 0, "a Miracle: the close is the button", 1, 1);
  e = GripFrames(&run, 1, false, empty);
  Check(!run.out.button && run.out.opened && e == 0,
        "the opening is the release, and it latches the throw", 0, 0);

  // The Tug of a tree: the button stays down while the grip pulls. When the
  // tree comes free into the Hand, the grip carries it.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 60, true, empty);
  Check(run.out.button, "the Tug: 60 frames with the button down", 1, 1);
  e = GripFrames(&run, 1, true, held);
  Check((e & kGripCarried) != 0 && !run.out.button,
        "the tree is free: the grip carries it", 0, 0);

  // The pick lock never clears: the carry starts kGripSettleMs after the
  // object came, and the button stays down until then.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 1, true, taking);
  const uint64_t came = run.ms;
  int up = 0;
  while (run.hold.phase == kGripPass && run.ms < came + 5000) {
    GripFrames(&run, 1, true, taking);
    if (run.hold.phase == kGripPass && !run.out.button) ++up;
  }
  Check(run.ms - came == kGripSettleMs,
        "a lock that stays set: the carry starts after kGripSettleMs",
        static_cast<double>(run.ms - came),
        static_cast<double>(kGripSettleMs));
  Check(up == 0, "and the button is down on each frame before the carry", up,
        0);
  // While a release is a click for the game, no time ends the press.
  {
    GripFacts click = taking;
    click.click = true;
    GripStart(&run);
    GripFrames(&run, 1, true, empty);
    e = GripFrames(&run, GripTestFrames(3 * kGripSettleMs), true, click);
    Check(run.hold.phase == kGripPass && run.out.button && e == 0,
          "a press that is still a click for the game stays down, 3 s later",
          run.hold.phase, kGripPass);
  }
  // The pick lock is clear, and the game does not have the object in its
  // Hand yet: the pick up is not over.
  {
    GripFacts early = Facts(true, true);
    early.game_holds = false;
    GripStart(&run);
    GripFrames(&run, 1, true, empty);
    e = GripFrames(&run, 20, true, early);
    Check(run.hold.phase == kGripPass && run.out.button && e == 0,
          "an open pick with no object in the Hand of the game is no carry",
          run.hold.phase, kGripPass);
    e = GripFrames(&run, 1, true, held);
    Check((e & kGripCarried) != 0 && !run.out.button,
          "the game has the object: the grip carries", e, kGripCarried);
  }

  // The grip opens before the object comes. The take press ends at once. An
  // object that comes within kGripLateMs is let go with a fresh press.
  GripStart(&run);
  GripFrames(&run, 6, true, empty);
  GripFrames(&run, 1, false, empty);
  Check(!run.out.button, "an opening before the object ends the press", 0, 0);
  GripFrames(&run, 2, false, empty);
  GripFrames(&run, 3, false, taking);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripLate) != 0 && (e & kGripClickDown) != 0 && run.out.button,
        "the object came after the opening: a click lets it go", 1, 1);
  GripStart(&run);
  GripFrames(&run, 6, true, empty);
  GripFrames(&run, 1, false, empty);
  GripFrames(&run, GripTestFrames(kGripLateMs) + 5, false, empty);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripLate) == 0 && run.hold.phase == kGripOpenHeld &&
            !run.out.button,
        "an object that comes much later is not let go", run.hold.phase,
        kGripOpenHeld);
  // The grip opens on the same frame the pick up settles. The take press
  // goes up first, and the click presses on the next frame.
  GripStart(&run);
  GripFrames(&run, 5, true, empty);
  GripFrames(&run, 2, true, taking);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripLate) != 0 && !run.out.button && run.out.click,
        "an opening as the pick up ends: the take press goes up first", 0, 0);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripClickDown) != 0 && run.out.button,
        "and the click presses on the next frame", 1, 1);

  // The mouse takes the Hand during a carry. The grip is the button again,
  // but a grip that is still closed must open before it presses.
  const GripFacts mouse = Facts(true, true, false, false, false);
  GripStart(&run);
  GripFrames(&run, 2, true, empty);
  GripFrames(&run, 2, true, held);
  e = GripFrames(&run, 1, true, mouse);
  Check((e & kGripLost) != 0 && !run.out.button,
        "the mouse takes the Hand: no button from a closed grip", 0, 0);
  e = GripFrames(&run, 1, true, held);
  Check((e & kGripCarried) != 0 && !run.out.button,
        "the controller takes it back: the grip carries again", 0, 0);
  // And during the click's press, the button goes up at once.
  GripFrames(&run, 1, false, held);
  Check(run.out.button, "the click presses", 1, 1);
  GripFrames(&run, 1, false, mouse);
  Check(!run.out.button && !run.out.click && run.hold.phase == kGripPass,
        "the mouse takes the Hand during the click: the button goes up", 0,
        0);

  // The object leaves the Hand another way during a carry: the closed grip
  // presses nothing until it opens.
  GripStart(&run);
  GripFrames(&run, 2, true, empty);
  GripFrames(&run, 2, true, held);
  e = GripFrames(&run, 5, true, empty);
  Check((e & kGripGone) != 0 && !run.out.button,
        "the object is gone: a closed grip presses nothing", 0, 0);
  GripFrames(&run, 1, false, empty);
  GripFrames(&run, 1, true, empty);
  Check(run.out.button, "after an opening the next close presses", 1, 1);

  // THE THROW OF THE OPENING. A swing at 4 m/s (30 units/s at 7.5 units a
  // meter) to the opening, then a stop of the follow through. 90 frames a
  // second. The mean over the 80 ms before the opening is the swing. At the
  // release, 4 frames later, the same window holds half the stop.
  MotionSample samples[32];
  int count = 0;
  for (int i = 0; i < 21; ++i, ++count) {
    samples[count].time = i / 90.0;
    samples[count].point[0] = 30.0f * static_cast<float>(i) / 90.0f;
    samples[count].point[1] = 0.0f;
    samples[count].point[2] = 0.0f;
  }
  const int at_opening = count;
  for (int i = 21; i < 25; ++i, ++count) {
    samples[count] = samples[at_opening - 1];
    samples[count].time = i / 90.0;
  }
  float v_open[3], v_late[3];
  MeanVelocity(samples, at_opening, 0.080, v_open);
  MeanVelocity(samples, count, 0.080, v_late);
  Near("the motion latched at the opening is the swing, 30 units/s",
       v_open[0], 30.0, 1e-3);
  Check(v_late[0] < 0.6f * v_open[0],
        "4 frames later the window has lost most of it", v_late[0],
        0.6 * v_open[0]);
  const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  float sent[3], before = 0.0f;
  ThrowVelocity(v_open, v_open, identity, 1.5f, 124.0f, sent, &before);
  Near("the latched swing throws at 4 m/s * 7.5 * 1.5 = 45 units/s",
       Length(sent), 45.0, 1e-2);

  // THE POINT TOUCHES THE LAND. The old rule held the point half the Hand's
  // length over the land: a hand of 19 cm is size 0.19 * s / 3.2, and half
  // of 3.2 times that is 9.5 cm of the room at every scale. The point now
  // comes to 5 mm.
  const float scales[3] = {7.5f, 100.0f, 1000.0f};
  for (int i = 0; i < 3; ++i) {
    const float s = scales[i];
    const float size = 0.19f * s / 3.2f;
    const float old_gap = 0.5f * 3.2f * size / s * 100.0f;
    char what[96];
    sprintf_s(what, "at %.1f units a meter the old rule held the point "
              "9.5 cm over the land", s);
    Near(what, old_gap, 9.5, 1e-3);
    const float land = 12.5f;
    const float gap = (HandLandFloor(land, s, 0.005f) - land) / s * 100.0f;
    sprintf_s(what, "at %.1f units a meter the point now comes to 0.5 cm",
              s);
    Near(what, gap, 0.5, 1e-3);
  }
  Near("no scale known: the floor is the land", HandLandFloor(3.0f, 0.0f,
       0.005f), 3.0, 0.0);
}

// THE BULK TAKE (docs/grab.md, "The bulk take"). The game takes from a pile,
// a field or a fish farm only while the right button is down. The Pot is in
// the Hand from the first turn of the take.
void BulkTakeChecks() {
  printf("\nThe bulk take\n");
  GripRun run;
  const GripFacts empty = Facts(false, true);
  const GripFacts held = Facts(true, true);
  const GripFacts locked = Facts(true, false);
  // What hand_direct.cpp gives while a take runs: no direct hand, so the
  // grip is the game's button.
  const GripFacts take = Facts(false, false, false, false, false);

  // With the facts of a held object, the machine carries kGripSettleMs after
  // the Pot comes, and the button goes up. That would end the take.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  int e = GripFrames(&run, GripTestFrames(kGripSettleMs) + 1, true, locked);
  Check((e & kGripCarried) != 0 && !run.out.button,
        "with the facts of a held object the Pot in the Hand gives a carry, "
        "and the button goes up", run.out.button, 0);

  // A close on a pile, the take for 200 frames, and an opening.
  GripStart(&run);
  GripFrames(&run, 3, false, empty);
  GripFrames(&run, 1, true, empty);
  Check(run.out.button, "a close on a pile presses the game's button", 1, 1);
  GripFrames(&run, 5, true, empty);
  int down = 0;
  e = 0;
  for (int i = 0; i < 200; ++i) {
    e |= GripFrames(&run, 1, true, take);
    if (run.out.button) ++down;
  }
  Check(down == 200 && (e & kGripCarried) == 0,
        "the button stays down for all 200 frames of the take", down, 200);
  e = GripFrames(&run, 1, false, take);
  Check(!run.out.button && !run.out.opened && !run.out.click,
        "the opening lets the button go up, with no throw and no click", 0,
        0);
  // The end packet of the take comes some frames later.
  e = GripFrames(&run, 3, false, take);
  Check(!run.out.button && e == 0, "no button while the take ends", e, 0);
  // Then the Pot stays in the Hand, and the pick lock is set for a while.
  e = GripFrames(&run, 20, false, locked);
  Check((e & kGripOpenedHeld) != 0 && (e & kGripLate) == 0 &&
            (e & kGripClickDown) == 0 && !run.out.button,
        "the Hand keeps the Pot with the grip open: no let go and no click",
        e, kGripOpenedHeld);
  Check(run.hold.phase == kGripOpenHeld, "the phase is OpenHeld",
        run.hold.phase, kGripOpenHeld);
  e = GripFrames(&run, 1, true, held);
  Check((e & kGripRegrip) != 0 && !run.out.button,
        "the next close carries the Pot with no button", 0, 0);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripClickDown) != 0 && run.out.button && run.out.opened,
        "the next opening clicks to let go, as for any object", 1, 1);

  // The pile runs out while the grip is still closed: the game ends the
  // take, and the grip carries the Pot.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 50, true, take);
  e = GripFrames(&run, 1, true, held);
  Check((e & kGripCarried) != 0 && !run.out.button,
        "a take that ends with the grip closed: the grip carries the Pot", 0,
        0);
  e = GripFrames(&run, 1, false, held);
  Check((e & kGripClickDown) != 0 && run.out.button,
        "and the opening lets go", 1, 1);

  // A short close: the take ends at once with a small amount, and the Pot
  // stays in the Hand.
  GripStart(&run);
  GripFrames(&run, 1, true, empty);
  GripFrames(&run, 3, true, take);
  GripFrames(&run, 1, false, take);
  e = GripFrames(&run, 16, false, locked);
  Check(run.hold.phase == kGripOpenHeld && (e & kGripLate) == 0,
        "a short take keeps the Pot in the Hand, with no let go",
        run.hold.phase, kGripOpenHeld);
}

// THE HINT AT THE HAND (docs/ui-layer.md, "The tooltip at the Hand"): the size of a tooltip
// at the Hand's depth, and the same place from the Hand in both eyes.
void HintHandChecks() {
  printf("\nThe hint at the Hand\n");
  // The render of the desktop runs: 2880 high at 110 degrees.
  const float focal = 1440.0f / tanf(55.0f * kDeg);
  const float box = 115.0f;  // the size of the game's call at that render
  const float s1 = HintAtHandScale(focal, 1.0f, box, 2.5f);
  Near("at 1 m a box of 2.5 cm is 2.5 cm in pixels", s1 * box,
       focal * 0.025f, 1e-3);
  Near("the scale is the same as in run bk3", s1, 0.219, 0.002);
  const float s2 = HintAtHandScale(focal, 0.5f, box, 2.5f);
  Near("at half the distance the box is twice the size", s2 / s1, 2.0,
       1e-4);
  Check(HintAtHandScale(focal, 0.0f, box, 2.5f) == 0.0f,
        "no depth: no change", 0, 0);
  Check(HintAtHandScale(0.0f, 1.0f, box, 2.5f) == 0.0f,
        "no focal: no change", 0, 0);
  Near("a hand at the face is cut to 3 times", HintAtHandScale(focal, 0.06f,
       box, 2.5f), 3.0, 0.0);

  // The game drew the tooltip at the same pixels in both eyes. The Hand's
  // point is 68 px apart in the two eyes at 1 m.
  const float hand_l[2] = {1242.0f, 1309.0f};
  const float hand_r[2] = {1174.0f, 1309.0f};
  const float first_l[2] = {1448.0f, 1252.0f};
  const float first_r[2] = {1366.0f, 1252.0f};
  const float offset[2] = {first_l[0] - hand_l[0], first_l[1] - hand_l[1]};
  float corner_l[2] = {first_l[0], first_l[1]};
  HintAtHandPoint(hand_l, first_l, offset, s1, corner_l);
  Near("left eye: the corner goes to the Hand plus the scaled offset (x)",
       corner_l[0], hand_l[0] + s1 * offset[0], 1e-3);
  Near("left eye: the same in y", corner_l[1], hand_l[1] + s1 * offset[1],
       1e-3);
  float far_l[2] = {first_l[0] + 100.0f, first_l[1] + 40.0f};
  float far_r[2] = {first_r[0] + 100.0f, first_r[1] + 40.0f};
  HintAtHandPoint(hand_l, first_l, offset, s1, far_l);
  HintAtHandPoint(hand_r, first_r, offset, s1, far_r);
  Near("a point of the tooltip has the Hand's disparity, 68 px",
       far_l[0] - far_r[0], hand_l[0] - hand_r[0], 1e-3);
  Near("and no vertical disparity", far_l[1] - far_r[1], 0.0, 1e-3);
  Near("the size between two points is scaled",
       (far_l[0] - corner_l[0]), 100.0 * s1, 1e-3);
}

// THE CREATURE BY TOUCH (docs/creature.md, "The Creature by touch"): the body
// capsule, the swept test, the speed of a slap as the game reads it, and the
// place of the Creature panel.
// The game's speed test: both points go onto the sphere around the eye at
// the Hand's distance, and the speed is their difference across the land,
// over dt (0x005B1B32 to 0x005B1BAA).
float GameSlapSpeed(const float eye[3], const float target[3],
                    const float last[3], float dt) {
  const float depth = Distance(target, eye);
  const float d[3] = {last[0] - eye[0], last[1] - eye[1], last[2] - eye[2]};
  const float span = Length(d);
  float p[3];
  for (int i = 0; i < 3; ++i) p[i] = eye[i] + d[i] / span * depth;
  const float dx = target[0] - p[0], dz = target[2] - p[2];
  return sqrtf(dx * dx + dz * dz) / dt;
}

void CreatureChecks() {
  printf("\nThe Creature by touch\n");
  // A Creature of size 0.22: S = 3.3 units, so the body is 3.63 units high.
  const float size = 0.22f;
  const float S = 15.0f * size;
  const Capsule body = {{100.0f, 20.0f, 200.0f}, 1.1f * S, 1.2f};
  const float top = 20.0f + 1.1f * S;
  const float inside[3] = {100.5f, 21.0f, 200.0f};
  Near("a point 0.5 units from the axis is 0.7 inside", CapsuleGap(body,
       inside), -0.7, 1e-4);
  const float over[3] = {100.0f, top + 2.0f, 200.0f};
  Near("a point 2 units over the top is 0.8 outside", CapsuleGap(body, over),
       0.8, 1e-4);
  const float front[3] = {100.0f, 21.5f, 197.0f};
  Near("a point 3 units in front is 1.8 outside", CapsuleGap(body, front),
       1.8, 1e-4);
  const float base[3] = {100.0f, 19.5f, 200.0f};
  Check(CapsuleGap(body, base) < 0.0f, "a point at the land under it is in "
        "it", CapsuleGap(body, base), -0.7);

  // Two segments.
  const float a0[3] = {0.0f, 0.0f, 0.0f}, a1[3] = {1.0f, 0.0f, 0.0f};
  const float b0[3] = {0.0f, 1.0f, 0.0f}, b1[3] = {1.0f, 1.0f, 0.0f};
  Near("two parallel segments 1 apart", SegmentSegmentDistance(a0, a1, b0,
       b1), 1.0, 1e-5);
  const float c0[3] = {0.5f, -1.0f, 2.0f}, c1[3] = {0.5f, 1.0f, 2.0f};
  Near("two crossed segments 2 apart", SegmentSegmentDistance(a0, a1, c0,
       c1), 2.0, 1e-5);
  const float d0[3] = {3.0f, 0.0f, 0.0f}, d1[3] = {4.0f, 0.0f, 0.0f};
  Near("two segments in a line, 2 apart at the ends",
       SegmentSegmentDistance(a0, a1, d0, d1), 2.0, 1e-5);
  Near("a segment of one point", SegmentSegmentDistance(b0, b0, a0, a1), 1.0,
       1e-5);

  // The swept test.
  const float left[3] = {98.0f, 21.5f, 200.0f};
  const float right[3] = {102.0f, 21.5f, 200.0f};
  Check(CapsuleEntered(body, left, right),
        "a hand that goes through the body between two frames enters it", 1,
        1);
  Check(CapsuleEntered(body, left, inside),
        "a hand that stops in the body enters it", 1, 1);
  const float beside[3] = {98.0f, 21.5f, 203.0f};
  Check(!CapsuleEntered(body, left, beside),
        "a hand that goes past 0.8 units from it does not", 0, 0);
  const float inside2[3] = {99.7f, 22.0f, 200.3f};
  Check(!CapsuleEntered(body, inside, inside2),
        "a hand that stays in the body does not enter it again", 0, 0);
  Check(!CapsuleEntered(body, inside, right),
        "a hand that comes out of the body does not enter it", 0, 0);
  const float over_l[3] = {98.0f, top + 2.0f, 200.0f};
  const float over_r[3] = {102.0f, top + 2.0f, 200.0f};
  Check(!CapsuleEntered(body, over_l, over_r),
        "a hand that goes 2 units over the top does not", 0, 0);
  const float graze_l[3] = {98.0f, 21.5f, 201.19f};
  const float graze_r[3] = {102.0f, 21.5f, 201.19f};
  Check(CapsuleEntered(body, graze_l, graze_r),
        "a hand that goes past 1.19 units from the axis enters it", 1, 1);

  // The across axis: level, at right angles to the line of sight, to the
  // right of the eye.
  const float eye0[3] = {0.0f, 0.0f, 0.0f};
  const float ahead[3] = {0.0f, 0.0f, 10.0f};
  float across[3] = {0.0f, 0.0f, 0.0f};
  Check(SlapAcross(eye0, ahead, across), "the across axis of a line ahead",
        1, 1);
  Near("for a line along +z it is +x (the right)", across[0], 1.0, 1e-6);
  const float eye[3] = {10.0f, 50.0f, -20.0f};
  const float target[3] = {25.0f, 22.0f, 14.0f};
  Check(SlapAcross(eye, target, across), "the across axis of a line down and "
        "to the side", 1, 1);
  const float sight[3] = {target[0] - eye[0], target[1] - eye[1],
                          target[2] - eye[2]};
  Near("it is at right angles to the line of sight",
       across[0] * sight[0] + across[1] * sight[1] + across[2] * sight[2],
       0.0, 1e-4);
  Near("it is level", across[1], 0.0, 0.0);
  Near("it is a unit", Length(across), 1.0, 1e-5);
  float kept[3] = {0.3f, 0.0f, 0.4f};
  const float below[3] = {10.2f, 0.0f, -20.1f};
  Check(!SlapAcross(eye, below, kept) && kept[0] == 0.3f,
        "a line straight down keeps the axis of the last frame", kept[0],
        0.3);

  // The speed the game reads. k makes a slap need 1.5 m/s at any size.
  const float slap_mps = 1.5f;
  const float k = SlapUnitsPerMps(size, slap_mps);
  Near("k for size 0.22 and 1.5 m/s: 5 S / 1.5", k, 5.0 * S / 1.5, 1e-4);
  const float dt = 1.0f / 60.0f;
  const float speeds[4] = {0.0f, 1.4f, 1.6f, 2.75f};
  const char* const names[4] = {"a still hand", "1.4 m/s", "1.6 m/s",
                                "2.75 m/s"};
  for (int n = 0; n < 4; ++n) {
    float last[3];
    SlapPreviousPoint(target, across, 1.0f, k, speeds[n], dt, last);
    const float game = speeds[n] > 0.0f
                           ? GameSlapSpeed(eye, target, last, dt)
                           : 0.0f;
    char what[160];
    sprintf_s(what, "%s: the game reads k times the real speed, units/s",
              names[n]);
    Near(what, game, k * speeds[n], 0.005 * k * speeds[n] + 1e-4);
    const bool slap = game > 5.0f * S;
    const bool hard = game >= 9.0f * S;
    sprintf_s(what, "%s: %s", names[n],
              n < 2 ? "no slap" : (n == 2 ? "a slap, not hard" : "a hard "
                                                               "slap"));
    Check(n < 2 ? !slap : (n == 2 ? slap && !hard : slap && hard), what,
          game, n < 2 ? 0.0 : 5.0 * S);
  }
  {
    float last[3];
    SlapPreviousPoint(target, across, 1.0f, k, 0.0f, dt, last);
    Near("a still hand: the last point is the target", Distance(last,
         target), 0.0, 0.0);
    SlapPreviousPoint(target, across, -1.0f, k, 1.6f, dt, last);
    const float back = (last[0] - target[0]) * across[0] +
                       (last[2] - target[2]) * across[2];
    Check(back > 0.0f, "a hand that moves left: the last point is on the "
          "right", back, k * 1.6 * dt);
  }
  // The same real speed at a larger Creature and the God view scale.
  {
    const float big = 1.8f;
    const float k_big = SlapUnitsPerMps(big, slap_mps);
    const float far_eye[3] = {0.0f, 400.0f, 0.0f};
    const float far_target[3] = {30.0f, 340.0f, 45.0f};
    float axis[3] = {1.0f, 0.0f, 0.0f};
    SlapAcross(far_eye, far_target, axis);
    float last[3];
    SlapPreviousPoint(far_target, axis, 1.0f, k_big, 1.6f, dt, last);
    const float game = GameSlapSpeed(far_eye, far_target, last, dt);
    Check(game > 5.0f * 15.0f * big && game < 9.0f * 15.0f * big,
          "size 1.8, 72 units from the eye: 1.6 m/s is a slap, not hard",
          game, 5.0 * 15.0 * big);
  }
  Near("no size: k is 0", SlapUnitsPerMps(0.0f, 1.5f), 0.0, 0.0);

  // The Creature panel. A room point goes to the world and back.
  float room[9];
  RoomBasis(30.0f, room);
  const float e[3] = {500.0f, 80.0f, 900.0f};
  const float t_head[3] = {0.2f, 12.0f, -0.5f};
  const float t[3] = {1.0f, 9.0f, 3.0f};
  const float gain = 13.3f;
  float t_head_g[3], t_g[3], world[3], back[3];
  ScaleRoom(t_head, gain, t_head_g);
  ScaleRoom(t, gain, t_g);
  RoomToWorld(e, room, t_head_g, t_g, world);
  WorldToRoom(e, room, t_head, gain, world, back);
  Near("a room point to the world and back, on the table", Distance(back, t),
       0.0, 1e-4);
  RoomToWorld(e, room, t_head, t, world);
  WorldToRoom(e, room, t_head, 1.0f, world, back);
  Near("and off the table", Distance(back, t), 0.0, 1e-4);

  // The room to LOCAL, against the real head_pose.cpp.
  float origin[3], yaw[4];
  if (HeadPoseOrigin(origin, yaw)) {
    const float local[3] = {0.4f, 1.1f, -0.7f};
    const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float m[9], in_room[3], again[3];
    HeadPoseToRoom(local, identity, m, in_room);
    RoomToLocal(origin, yaw, HeadPoseScale(), in_room, again);
    Near("a LOCAL point to the room and back", Distance(again, local), 0.0,
         1e-5);
  } else {
    Check(false, "the head has an origin", 0, 1);
  }
  // And with a turned origin, against the formula of RoomTranslation.
  {
    const float o[3] = {0.3f, 1.5f, -0.2f};
    const float half = 40.0f * kDeg * 0.5f;
    const float q[4] = {0.0f, sinf(half), 0.0f, cosf(half)};
    const float q_back[4] = {0.0f, -sinf(half), 0.0f, cosf(half)};
    const float p[3] = {0.5f, 1.2f, -0.8f};
    const float off[3] = {p[0] - o[0], p[1] - o[1], p[2] - o[2]};
    float l[3];
    QuatRotate(q_back, off, l);
    const float u = 7.5f;
    const float in_room[3] = {l[0] * u, l[1] * u, -l[2] * u};
    float again[3];
    RoomToLocal(o, q, u, in_room, again);
    Near("a turned origin: the room to LOCAL undoes RoomTranslation",
         Distance(again, p), 0.0, 1e-5);
  }

  // The panel faces the eyes, with no roll.
  {
    const float at[3] = {0.2f, 1.0f, -0.6f};
    const float eyes[3] = {-0.1f, 1.6f, 0.0f};
    float q[4], normal[3], side[3];
    FacePose(at, eyes, q);
    const float z[3] = {0.0f, 0.0f, 1.0f};
    const float x[3] = {1.0f, 0.0f, 0.0f};
    QuatRotate(q, z, normal);
    QuatRotate(q, x, side);
    const float to[3] = {eyes[0] - at[0], eyes[1] - at[1], eyes[2] - at[2]};
    const float span = Length(to);
    Near("the front of the panel points at the eyes (x)", normal[0],
         to[0] / span, 1e-5);
    Near("the front of the panel points at the eyes (y)", normal[1],
         to[1] / span, 1e-5);
    Near("the front of the panel points at the eyes (z)", normal[2],
         to[2] / span, 1e-5);
    Near("and it does not roll", side[1], 0.0, 1e-5);
  }

  // The place near the Creature: 30 cm over its head, 15 cm to its left,
  // 0.5 to 1.5 m from the eyes.
  {
    const float eyes[3] = {0.0f, 1.6f, 0.0f};
    const float head[3] = {0.1f, 0.9f, -0.6f};
    float at[3];
    Check(CreaturePanelPoint(eyes, head, 0.30f, 0.15f, 0.5f, 1.5f, at),
          "a place near a Creature at the waist", 1, 1);
    const float flat = sqrtf(0.1f * 0.1f + 0.6f * 0.6f);
    const float right_dir[3] = {0.6f / flat, 0.0f, 0.1f / flat};
    const float dx = at[0] - head[0], dz = at[2] - head[2];
    Check(dx * right_dir[0] + dz * right_dir[2] < 0.0f,
          "it is to the left of the Creature as the eyes see it",
          dx * right_dir[0] + dz * right_dir[2], -0.15);
    Check(at[1] > head[1], "it is over the Creature's head", at[1], head[1]);
    const float d = Distance(at, eyes);
    Check(d >= 0.5f - 1e-5f && d <= 1.5f + 1e-5f, "it is 0.5 to 1.5 m from "
          "the eyes", d, 1.0);
    const float far_head[3] = {2.0f, 1.0f, -9.0f};
    CreaturePanelPoint(eyes, far_head, 0.30f, 0.15f, 0.5f, 1.5f, at);
    Near("a Creature 9 m away: the panel comes to 1.5 m",
         Distance(at, eyes), 1.5, 1e-4);
    const float near_head[3] = {0.05f, 1.4f, -0.1f};
    CreaturePanelPoint(eyes, near_head, 0.30f, 0.15f, 0.5f, 1.5f, at);
    Near("a Creature at the face: the panel goes out to 0.5 m",
         Distance(at, eyes), 0.5, 1e-4);
    const float above[3] = {0.0f, 3.0f, 0.0f};
    Check(!CreaturePanelPoint(eyes, above, 0.30f, 0.15f, 0.5f, 1.5f, at),
          "a head right over the eyes has no place", 0, 0);
  }
}

#include "aim_checks.h"
#include "miracle_checks.h"
#include "audio_checks.h"
#include "command_checks.h"
#include "exact_checks.h"
#include "village_checks.h"
#include "far_checks.h"
#include "far_choice_checks.h"
#include "forest_checks.h"
#include "grip_checks.h"
#include "hand_pose_checks.h"
#include "hand_model_checks.h"
#include "hint_checks.h"
#include "near_checks.h"
#include "haptics_checks.h"
#include "polish_checks.h"
#include "throw_checks.h"
#include "window_checks.h"
#include "intro_skip_checks.h"
#include "intro_motion_checks.h"
#include "spawn_checks.h"
#include "spirit_size_checks.h"
#include "stay_checks.h"
#include "ring_checks.h"
#include "teleport_checks.h"
#include "feel_checks.h"
#include "pinch_checks.h"
#include "temple_checks.h"
#include "eye_luma_checks.h"
#include "tree_plant_checks.h"
#include "text_click_checks.h"
#include "script_frame_checks.h"
#include "frame_stats_checks.h"
#include "frame_time_checks.h"
#include "zoom_checks.h"

int main() {
  const float eye[3] = {120.0f, 45.0f, -300.0f};
  float head[9];

  printf("Before the head has an origin\n");
  {
    float position[3] = {0.1f, 0.2f, 0.3f}, orientation[4] = {0, 0, 0, 1};
    float m[9], t[3];
    // The origin is not captured by a hand, in either call.
    Check(!HeadPoseToRoom(position, orientation, m, t),
          "HeadPoseToRoom is false with no origin", 0, 0);
    Check(!HeadPoseToRoom(position, orientation, m, t),
          "and it is still false, so it did not capture one", 0, 0);
    Check(!HeadPoseHeadInRoom(t), "HeadPoseHeadInRoom is false too", 0, 0);
  }

  // Two room headings, so a wrong order of L and H cannot pass by luck.
  const float headings[2] = {0.0f, 30.0f};
  for (int h = 0; h < 2; ++h) {
    float room[9];
    RoomBasis(headings[h], room);
    printf("\nRoom heading %.0f degrees\n", headings[h]);

    printf("Test 1: a hand at the head center\n");
    SetHead(0.0f, -15.0f, 0.0f, head);
    Result r = Hand(eye, room, head, 0, 0, 0, 0.0f, -15.0f, 0.0f, 150.0f);
    Near("distance from the eye, units", r.distance, 0.0, 0.005);
    Check(r.have_pixel, "a pixel exists", 0, 0);
    Near("pixel x, the middle", r.x, kScreen.hx, 0.5);
    Near("pixel y, the middle", r.y, kScreen.hy, 0.5);

    printf("Test 2: a hand 100 cm forward\n");
    SetHead(0.0f, 0.0f, 0.0f, head);
    r = Hand(eye, room, head, 0, 0, 100, 0.0f, 0.0f, 0.0f, 150.0f);
    float render_forward[3];
    const float forward[3] = {0.0f, 0.0f, 1.0f};
    float in_room[3];
    Apply(head, false, forward, in_room);
    Apply(room, false, in_room, render_forward);
    const float from_eye[3] = {r.origin[0] - eye[0], r.origin[1] - eye[1],
                               r.origin[2] - eye[2]};
    Near("distance from the eye, units", r.distance, 7.5, 0.005);
    Near("distance along the render forward, units",
         from_eye[0] * render_forward[0] + from_eye[1] * render_forward[1] +
             from_eye[2] * render_forward[2],
         7.5, 0.005);

    printf("Test 3: the hand pitched 20 degrees down\n");
    r = Hand(eye, room, head, 0, 0, 0, 0.0f, -20.0f, 0.0f, 150.0f);
    Check(r.have_pixel, "a pixel exists", 0, 0);
    const double drop = kScreen.hy * kScreen.fy * tan(20.0 * kDeg);
    Near("pixel x does not move", r.x, kScreen.hx, 0.5);
    Near("pixel moves down by hy * fy * tan(20)", r.y - kScreen.hy, drop, 0.5);
    r = Hand(eye, room, head, 0, 0, 0, 0.0f, 20.0f, 0.0f, 150.0f);
    Near("pitched up, it moves up by the same", kScreen.hy - r.y, drop, 0.5);

    printf("Test 4: the head turns, the hand does not\n");
    SetHead(0.0f, 0.0f, 0.0f, head);
    const Result before = Hand(eye, room, head, 0, 0, 0, 0.0f, 0.0f, 0.0f,
                               150.0f);
    SetHead(20.0f, 0.0f, 0.0f, head);
    const Result after = Hand(eye, room, head, 0, 0, 0, 0.0f, 0.0f, 0.0f,
                              150.0f);
    Near("world direction x", after.direction[0], before.direction[0], 1e-4);
    Near("world direction y", after.direction[1], before.direction[1], 1e-4);
    Near("world direction z", after.direction[2], before.direction[2], 1e-4);
    Near("pixel moves by hx * fx * tan(20)", fabs(after.x - before.x),
         kScreen.hx * kScreen.fx * tan(20.0 * kDeg), 0.5);
    Near("pixel y does not move", after.y, before.y, 0.5);

    printf("Test 5: a hand 20 cm to the right of the head center\n");
    SetHead(0.0f, 0.0f, 0.0f, head);
    r = Hand(eye, room, head, 20, 0, 0, 0.0f, 0.0f, 0.0f, 150.0f);
    float right_world[3];
    const float right[3] = {1.0f, 0.0f, 0.0f};
    Apply(room, false, right, right_world);
    Near("origin x offset along the room right, units",
         (r.origin[0] - eye[0]) * right_world[0] +
             (r.origin[1] - eye[1]) * right_world[1] +
             (r.origin[2] - eye[2]) * right_world[2],
         1.5, 0.005);
    Near("distance from the eye, units", r.distance, 1.5, 0.005);

    printf("Test 6: the pixel of a point is the pixel of the ray to it\n");
    SetHead(10.0f, -20.0f, 5.0f, head);
    r = Hand(eye, room, head, 15, -25, 40, 5.0f, -30.0f, 0.0f, 6.0f);
    {
      const float point[3] = {r.origin[0] + 6.0f * r.direction[0],
                              r.origin[1] + 6.0f * r.direction[1],
                              r.origin[2] + 6.0f * r.direction[2]};
      float px = 0.0f, py = 0.0f;
      bool off = false;
      const bool have = PointPixel(eye, room, head, point, kScreen, &px, &py,
                                   &off);
      Check(have && r.have_pixel, "both pixels exist", 0, 0);
      Near("point pixel x", px, r.x, 0.01);
      Near("point pixel y", py, r.y, 0.01);
      // Back through the screen: the ray of the pixel, L * H * ray, points at
      // the point.
      const float ray[3] = {(px / kScreen.hx - 1.0f) / kScreen.fx,
                            (1.0f - py / kScreen.hy) / kScreen.fy, 1.0f};
      float a[3], world[3];
      Apply(head, false, ray, a);
      Apply(room, false, a, world);
      const float length = sqrtf(world[0] * world[0] + world[1] * world[1] +
                                 world[2] * world[2]);
      const float d = Distance(point, eye);
      float along = 0.0f;
      for (int i = 0; i < 3; ++i) {
        along += (world[i] / length) * (point[i] - eye[i]) / d;
      }
      Near("the pixel's ray points at the point (cosine)", along, 1.0, 1e-5);
    }
  }

  printf("\nThe reach curve\n");
  {
    const float start = 0.35f, gain = 215.0f;
    Near("inside the start it is one to one", ReachDistance(0.30f, start, gain),
         0.30, 1e-6);
    Near("at the start it is one to one", ReachDistance(0.35f, start, gain),
         0.35, 1e-6);
    Near("a stretched arm, 65 cm, reaches 20 m (150 units)",
         ReachDistance(0.65f, start, gain) * 7.5f, 150.0, 0.5);
    Near("the slope just past the start is 1",
         (ReachDistance(0.3501f, start, gain) - ReachDistance(0.35f, start,
                                                               gain)) / 0.0001f,
         1.0, 0.05);
    bool rising = true;
    float last = 0.0f;
    for (int cm = 1; cm <= 90; ++cm) {
      const float value = ReachDistance(cm * 0.01f, start, gain);
      if (!(value > last)) rising = false;
      last = value;
    }
    Check(rising, "it always grows with the real distance", 0, 0);
    Near("a gain of 0 is one to one", ReachDistance(0.65f, start, 0.0f), 0.65,
         1e-6);
    Near("the inverse of 20 m is 65 cm", ReachInverse(20.0f, start, gain),
         0.65, 1e-4);
    Near("the inverse inside the start is one to one",
         ReachInverse(0.2f, start, gain), 0.2, 1e-6);
    Near("the inverse of 3 m, then forward again",
         ReachDistance(ReachInverse(3.0f, start, gain), start, gain), 3.0,
         1e-3);

    // ReachPoint keeps the direction from the eye.
    const float from[3] = {10.0f, 20.0f, 30.0f};
    const float hand[3] = {10.0f + 3.0f, 20.0f - 2.0f, 30.0f + 3.0f};
    float out[3], factor = 0.0f;
    ReachPoint(from, hand, 7.5f, start, gain, 0.0f, out, &factor);
    const float d = Distance(hand, from) / 7.5f;
    Near("ReachPoint distance, units", Distance(out, from),
         ReachDistance(d, start, gain) * 7.5f, 0.01);
    Near("ReachPoint factor", factor, ReachDistance(d, start, gain) / d, 1e-4);
    float along = 0.0f;
    for (int i = 0; i < 3; ++i) {
      along += (out[i] - from[i]) * (hand[i] - from[i]);
    }
    Near("ReachPoint keeps the direction (cosine)",
         along / (Distance(out, from) * Distance(hand, from)), 1.0, 1e-5);
    const float near_hand[3] = {10.0f + 1.0f, 20.0f, 30.0f + 1.0f};
    ReachPoint(from, near_hand, 7.5f, start, gain, 0.0f, out, &factor);
    Near("a hand 19 cm away does not move", Distance(out, near_hand), 0.0,
         1e-5);

    // The far limit.
    const float far_hand[3] = {10.0f, 20.0f, 30.0f + 1.5f * 7.5f};
    ReachPoint(from, far_hand, 7.5f, start, gain, kFarLimit, out, &factor);
    Near("a hand 1.5 m away stops at the far limit, units",
         Distance(out, from), kFarLimit, 0.05);
    ReachPoint(from, far_hand, 7.5f, start, gain, 5.0f, out, &factor);
    Near("a limit nearer than the hand does not pull it in",
         Distance(out, far_hand), 0.0, 1e-4);
  }

  printf("\nThe direct hand: a still hand, a head that moves\n");
  {
    const float game_position[3] = {120.0f, 45.0f, -300.0f};
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const float right_1cm[3] = {0.01f, 0.0f, 0.0f};
    const float forward_5cm[3] = {0.0f, 0.0f, -0.05f};
    const float mixed_5cm[3] = {-0.03f, -0.03f, -0.03f};
    const float* moves[3] = {right_1cm, forward_5cm, mixed_5cm};
    const char* const names[3] = {"1 cm right", "5 cm forward",
                                  "5 cm down, left and forward"};
    for (int h = 0; h < 2; ++h) {
      float room[9];
      RoomBasis(headings[h], room);
      printf("Room heading %.0f degrees\n", headings[h]);
      // The hand at 20 cm right, 30 cm down and 50 cm forward of the head
      // center: 55 cm from the anchor, past the reach start.
      const Direct still = DirectHand(game_position, room, 0.0f, zero, 20.0f,
                                      -30.0f, 50.0f);
      Check(still.factor > 2.0f, "the hand is past the reach start (factor)",
            still.factor, 2.0);
      Near("the anchor is 20 cm below the head center, units",
           Distance(still.anchor, still.eye), kAnchorDown * 7.5f, 1e-4);
      for (int m = 0; m < 3; ++m) {
        char what[160];
        // The head turns 20 degrees too, which must not matter either.
        const Direct moved = DirectHand(game_position, room, 20.0f, moves[m],
                                        20.0f, -30.0f, 50.0f);
        sprintf_s(what, "head %s: the eye moves with it, units", names[m]);
        Near(what, Distance(moved.eye, still.eye),
             Distance(moves[m], zero) * 7.5, 1e-4);
        sprintf_s(what, "head %s: the reach point does not move, units",
                  names[m]);
        Near(what, Distance(moved.reach, still.reach), 0.0, 1e-3);
        sprintf_s(what, "head %s: the reach factor does not change",
                  names[m]);
        Near(what, moved.factor, still.factor, 1e-4);
        float along = 0.0f;
        for (int i = 0; i < 3; ++i) {
          along += moved.camera_dir[i] * still.camera_dir[i];
        }
        sprintf_s(what, "head %s: the drag ray does not turn (cosine)",
                  names[m]);
        Near(what, along, 1.0, 1e-6);
        sprintf_s(what, "head %s: the old reach from the eye moved, units",
                  names[m]);
        Check(Distance(moved.old_reach, still.old_reach) > 1.0f, what,
              Distance(moved.old_reach, still.old_reach), 1.0);
      }
    }

    printf("Rotation only mode\n");
    HeadPoseSetRotationOnly(true);
    {
      float room[9];
      RoomBasis(30.0f, room);
      const Direct still = DirectHand(game_position, room, 0.0f, zero, 20.0f,
                                      -30.0f, 50.0f);
      const Direct moved = DirectHand(game_position, room, 0.0f, forward_5cm,
                                      20.0f, -30.0f, 50.0f);
      Near("the eye stays on the game camera, units",
           Distance(moved.eye, game_position), 0.0, 1e-4);
      Near("the reach factor does not change with a 5 cm move", moved.factor,
           still.factor, 1e-4);
      Near("the Hand keeps its place to the eye: it moves 5 cm, units",
           Distance(moved.reach, still.reach), 0.05 * 7.5, 1e-3);
    }
    HeadPoseSetRotationOnly(false);
  }

  printf("\nThe motion of a hand (M4 and M5)\n");
  {
    // A hand that moves 1 unit each 10 ms along +x: 100 units per second.
    MotionSample s[12];
    for (int i = 0; i < 12; ++i) {
      s[i].time = 5.0 + i * 0.010;
      s[i].point[0] = static_cast<float>(i) * 1.0f;
      s[i].point[1] = 2.0f;
      s[i].point[2] = -static_cast<float>(i) * 0.5f;
    }
    float v[3];
    Check(MeanVelocity(s, 12, 0.08, v), "a steady hand has a velocity", 1, 1);
    Near("steady hand, x per second", v[0], 100.0, 0.5);
    Near("steady hand, y per second", v[1], 0.0, 0.01);
    Near("steady hand, z per second", v[2], -50.0, 0.5);
    Check(!MeanVelocity(s, 1, 0.08, v), "one sample gives no velocity", 0, 0);

    // A step: still for 9 samples, then a jump of 8 units in one frame. The
    // mean over the window takes the step over the window, not one frame.
    MotionSample step[10];
    for (int i = 0; i < 10; ++i) {
      step[i].time = 1.0 + i * 0.010;
      step[i].point[0] = i < 9 ? 0.0f : 8.0f;
      step[i].point[1] = step[i].point[2] = 0.0f;
    }
    MeanVelocity(step, 10, 0.08, v);
    Near("a step of 8 units in a window of 80 ms, per second", v[0], 8.0 / 0.08,
         0.5);
    MeanVelocity(step, 10, 0.03, v);
    Near("the same step in a window of 30 ms, per second", v[0], 8.0 / 0.03,
         0.5);

    // A slow frame rate: the window holds one sample, so the sample before
    // it is used.
    MotionSample slow[3] = {{1.0, {0.0f, 0.0f, 0.0f}},
                            {1.5, {5.0f, 0.0f, 0.0f}},
                            {2.0, {10.0f, 0.0f, 0.0f}}};
    Check(MeanVelocity(slow, 3, 0.08, v), "a slow frame rate has a velocity",
          1, 1);
    Near("a slow frame rate, x per second", v[0], 10.0, 1e-3);

    // The same time twice gives none.
    MotionSample twice[2] = {{1.0, {0, 0, 0}}, {1.001, {1, 0, 0}}};
    Check(!MeanVelocity(twice, 2, 0.08, v), "samples 1 ms apart give none", 0,
          0);

    // The throw: the room turns 90 degrees. The reach point moves along the
    // room's forward at 300 units per second (a far Hand), and the grip
    // moves at 20 units per second. The speed is the grip's.
    float room[9];
    RoomBasis(90.0f, room);
    const float reach_room[3] = {0.0f, 0.0f, 300.0f};
    const float grip_room[3] = {0.0f, 0.0f, 20.0f};
    float out[3], before = 0.0f;
    ThrowVelocity(reach_room, grip_room, room, 1.0f, 124.0f, out, &before);
    Near("throw speed before the cut is the grip's", before, 20.0, 1e-3);
    Near("throw speed is not the Hand's 300", Length(out), 20.0, 1e-3);
    Near("a room forward turned 90 degrees goes along world x", out[0], 20.0,
         1e-3);
    Near("and not along world z", out[2], 0.0, 1e-3);
    ThrowVelocity(reach_room, grip_room, room, 1.5f, 124.0f, out, &before);
    Near("a gain of 1.5 gives 30 units per second", Length(out), 30.0, 1e-3);
    const float fast_reach[3] = {60.0f, 80.0f, 0.0f};
    const float fast_grip[3] = {30.0f, 40.0f, 0.0f};
    const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    ThrowVelocity(fast_reach, fast_grip, ident, 3.0f, 124.0f, out, &before);
    Near("a speed of 150 is reported before the cut", before, 150.0, 1e-2);
    Near("and cut to the cap of 124", Length(out), 124.0, 1e-3);
    Near("the cut keeps the direction (x share)", out[0] / Length(out), 0.6,
         1e-4);
    ThrowVelocity(reach_room, grip_room, room, 0.0f, 124.0f, out, &before);
    Near("a gain of zero gives a drop", Length(out), 0.0, 1e-6);
    const float no_reach[3] = {0.0f, 0.0f, 0.0f};
    ThrowVelocity(no_reach, grip_room, room, 1.0f, 124.0f, out, &before);
    Near("with no reach motion the grip gives the direction", out[0], 20.0,
         1e-3);
    ThrowVelocity(no_reach, no_reach, room, 1.0f, 124.0f, out, &before);
    Near("with no motion at all the throw is zero", Length(out), 0.0, 1e-6);

    // The previous point of the Creature: it goes back by v * gain * dt.
    const float target[3] = {10.0f, 5.0f, 20.0f};
    const float v_world[3] = {40.0f, 0.0f, -20.0f};
    float prev[3];
    PreviousPoint(target, v_world, 1.5f, 0.02f, prev);
    Near("previous point x", prev[0], 10.0 - 40.0 * 1.5 * 0.02, 1e-4);
    Near("previous point y", prev[1], 5.0, 1e-4);
    Near("previous point z", prev[2], 20.0 + 20.0 * 1.5 * 0.02, 1e-4);
    Near("the step back has the length of speed * gain * dt",
         Distance(prev, target), Length(v_world) * 1.5 * 0.02, 1e-4);
  }

  GrabChecks();
  GestureChecks();
  TableChecks();
  TableHandChecks();
  GodChecks();
  GodPanChecks();
  TeleportChecks();
  PanelChecks();
  GlowChecks();
  OverlayChecks();
  GripChecks();
  CreatureChecks();
  BulkTakeChecks();
  HintHandChecks();
  hint_checks::HintCardChecks();
  AudioChecks();
  ExactChecks();
  far_checks::FarChecks();
  far_choice_checks::FarChoiceChecks();
  near_checks::NearChecks();
  near_checks::OneTargetChecks();
  near_checks::StrokeChecks();
  haptics_checks::HapticsChecks();
  haptics_checks::LeashChecks();
  grip_checks::GripRuleChecks();
  hand_pose_checks::HandPoseChecks();
  hand_model_checks::HandModelChecks();
  polish_checks::PolishChecks();
  throw_checks::ThrowChecks();
  command_checks::CommandChecks();
  command_checks::LiveChecks();
  window_checks::WindowChecks();
  intro_skip_checks::IntroSkipChecks();
  intro_motion_checks::IntroMotionChecks();
  spawn_checks::SpawnChecks();
  spirit_size_checks::SpiritSizeChecks();
  village_checks::VillageChecks();
  aim_checks::AimChecks();
  miracle_checks::MiracleChecks();
  ring_checks::RingChecks();
  teleport_checks::TeleportChecks();
  forest_checks::ForestChecks();
  forest_checks::ForestGripChecks();
  feel_checks::FeelChecks();
  pinch_checks::PinchChecks();
  temple_checks::TempleChecks();
  eye_luma_checks::EyeLumaChecks();
  tree_plant_checks::TreePlantChecks();
  zoom_checks::ZoomChecks();
  stay_checks::StayChecks();
  text_click_checks::TextClickChecks();
  script_frame_checks::ScriptFrameChecks();
  frame_stats_checks::FrameStatsChecks();
  frame_time_checks::FrameTimeChecks();

  printf("\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
