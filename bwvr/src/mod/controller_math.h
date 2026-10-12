// The controller math, as pure functions with no game state.
//
// controllers.cpp calls these with the numbers it reads from the game, and
// test/controller_math calls them with numbers it makes up. Nothing here reads
// a global or touches the game.
//
// Every 3x3 matrix is m[r * 3 + c], and a vector is a column: out = m * in.
// This is the convention pointer_map.cpp and head_camera.cpp use for the room
// basis L and the head matrix H. See docs/controllers.md, "2.7 The controller in game world units" and
// "3.1 The mapping".

#pragma once

#include <math.h>
#include <stdint.h>

namespace controller_math {

// The screen mapping from bw::tech::InfoTransform, and the render size.
struct Screen {
  float hx, hy;  // half_res_x, half_res_y
  float fx, fy;  // inv_half_tan_fov_x, inv_half_tan_fov_y
};

// Rotate by m, or by its transpose.
inline void Apply(const float m[9], bool transpose, const float in[3],
                  float out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = 0.0f;
    for (int c = 0; c < 3; ++c) {
      out[r] += (transpose ? m[c * 3 + r] : m[r * 3 + c]) * in[c];
    }
  }
}

// out = a * b
inline void Multiply(const float a[9], const float b[9], float out[9]) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      float sum = 0.0f;
      for (int k = 0; k < 3; ++k) sum += a[r * 3 + k] * b[k * 3 + c];
      out[r * 3 + c] = sum;
    }
  }
}

// A hand in the world, from the room transform of head_pose.cpp.
//
//   position = E + L * (t_hand - t_head)
//   rotation = L * M_hand
//
// E is the rendered eye position, L the room basis, t_head the head center
// and t_hand the hand, both from HeadPoseHeadInRoom and HeadPoseToRoom, in
// game units. With full six degrees of freedom this is the same as
// game_position + L * t_hand. With rotation only mode it still keeps the hand
// at the right place relative to the eye.
inline void HandToWorld(const float eye[3], const float room[9],
                        const float t_head[3], const float t_hand[3],
                        const float m_hand[9], float position_out[3],
                        float rotation_out[9]) {
  const float delta[3] = {t_hand[0] - t_head[0], t_hand[1] - t_head[1],
                          t_hand[2] - t_head[2]};
  float moved[3];
  Apply(room, false, delta, moved);
  for (int i = 0; i < 3; ++i) position_out[i] = eye[i] + moved[i];
  Multiply(room, m_hand, rotation_out);
}

// A point of the room in the world: HandToWorld with no rotation.
//
//   position = E + L * (t - t_head)
inline void RoomToWorld(const float eye[3], const float room[9],
                        const float t_head[3], const float t[3],
                        float position_out[3]) {
  const float delta[3] = {t[0] - t_head[0], t[1] - t_head[1],
                          t[2] - t_head[2]};
  float moved[3];
  Apply(room, false, delta, moved);
  for (int i = 0; i < 3; ++i) position_out[i] = eye[i] + moved[i];
}

// The eye of one head sample: the game camera moved by the lean, in the
// room's axes.
//
//   E = game_position + L * lean
//
// head_camera.cpp renders from this same point. The lean, the head matrix H
// and t_head come from one HeadPoseUpdate, so E - L * t_head is the game
// camera, and a hand still in the room stays still in the world when the
// head moves. The render position of the frame that is drawn now has the
// lean of the last frame, so it does not fit t_head of this frame.
inline void EyeFromLean(const float game_position[3], const float room[9],
                        const float lean[3], float eye_out[3]) {
  float moved[3];
  Apply(room, false, lean, moved);
  for (int i = 0; i < 3; ++i) eye_out[i] = game_position[i] + moved[i];
}

// The aim direction of a hand: its rotation applied to (0, 0, 1). OpenXR aims
// along -Z, and the change of handedness in head_pose.cpp makes that +Z.
inline void AimDirection(const float rotation[9], float direction_out[3]) {
  const float forward[3] = {0.0f, 0.0f, 1.0f};
  Apply(rotation, false, forward, direction_out);
}

// The render pixel that an aim ray WOULD give. The exact inverse of
// ScreenRay in pointer_map.cpp.
//
//   T = A + s * D
//   c = transpose(H) * transpose(L) * (T - E)
//   x = (1 + c.x * fx / c.z) * hx
//   y = (1 - c.y * fy / c.z) * hy
//
// Returns false when the target is behind the head (c.z below 0.05), and
// then x and y are not written. off_view is true when the pixel falls outside
// the render, which is 0 to 2 * hx across and 0 to 2 * hy down.
inline bool RenderPixel(const float eye[3], const float room[9],
                        const float head[9], const float aim_origin[3],
                        const float aim_direction[3], float s,
                        const Screen& screen, float* x, float* y,
                        bool* off_view) {
  const float target[3] = {aim_origin[0] + s * aim_direction[0] - eye[0],
                           aim_origin[1] + s * aim_direction[1] - eye[1],
                           aim_origin[2] + s * aim_direction[2] - eye[2]};
  float in_room[3], c[3];
  Apply(room, true, target, in_room);
  Apply(head, true, in_room, c);
  if (c[2] < 0.05f) return false;
  *x = (1.0f + c[0] * screen.fx / c[2]) * screen.hx;
  *y = (1.0f - c[1] * screen.fy / c[2]) * screen.hy;
  *off_view = *x < 0.0f || *x >= 2.0f * screen.hx || *y < 0.0f ||
              *y >= 2.0f * screen.hy;
  return true;
}

// The orientation of a fake hand as x y z w, in the order the fake head uses
// (HeadPoseFakeEye): yaw about up, then pitch about right, then roll about
// forward. Positive yaw turns left and negative pitch points down. Forward is
// -Z in the runtime's right handed frame, so a roll is a rotation about -Z.
inline void FakeOrientation(float yaw_deg, float pitch_deg, float roll_deg,
                            float out[4]) {
  const float k = 0.5f / 57.29578f;
  const float cy = cosf(yaw_deg * k), sy = sinf(yaw_deg * k);
  const float cp = cosf(pitch_deg * k), sp = sinf(pitch_deg * k);
  const float cr = cosf(roll_deg * k), sr = -sinf(roll_deg * k);
  // yaw = (0, sy, 0, cy), pitch = (sp, 0, 0, cp), roll = (0, 0, sr, cr).
  // yaw * pitch first.
  const float ax = cy * sp, ay = sy * cp, az = -sy * sp, aw = cy * cp;
  // then (yaw * pitch) * roll, the Hamilton product.
  out[3] = aw * cr - az * sr;
  out[0] = ax * cr + ay * sr;
  out[1] = ay * cr - ax * sr;
  out[2] = aw * sr + az * cr;
}

// The yaw and the pitch of a fake hand (FakeOrientation, with the roll 0) that
// make its aim ray point along d, a direction in the room (x right, y up, z
// forward). Positive yaw turns left and positive pitch points up. False when
// d is zero.
inline bool FakeAimAngles(const float d[3], float* yaw_deg, float* pitch_deg) {
  const float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (!(length > 1e-6f)) return false;
  *yaw_deg = -atan2f(d[0], d[2]) * 57.29578f;
  float up = d[1] / length;
  if (up > 1.0f) up = 1.0f;
  if (up < -1.0f) up = -1.0f;
  *pitch_deg = asinf(up) * 57.29578f;
  return true;
}

inline float Distance(const float a[3], const float b[3]) {
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

// The render pixel of a point in the world: RenderPixel with a target
// distance of zero, so the target is the point itself. The direct hand
// uses this for the Hand's point.
inline bool PointPixel(const float eye[3], const float room[9],
                       const float head[9], const float point[3],
                       const Screen& screen, float* x, float* y,
                       bool* off_view) {
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  return RenderPixel(eye, room, head, point, zero, 0.0f, screen, x, y,
                     off_view);
}

// THE REACH (the Go-Go technique, Poupyrev and others, 1996).
//
// d is the real distance from the reach anchor to the hand, in meters. The
// anchor is a fixed point in the room at about the chest (ReachAnchor), not
// the eye, so a head that moves does not change d. Up to start the Hand
// follows the hand one to one. Past it the distance grows with the square of
// the distance past start:
//
//   D = d                          d <= start
//   D = d + gain * (d - start)^2   d > start
//
// The curve and its slope are continuous at start, so the Hand does not jump
// when the arm crosses it.
inline float ReachDistance(float d, float start, float gain) {
  if (!(d > start) || gain <= 0.0f) return d;
  const float past = d - start;
  return d + gain * past * past;
}

// The real distance that reaches the distance D, the inverse of
// ReachDistance. Past start it solves gain * x^2 + x + start - D = 0 for
// x = d - start.
inline float ReachInverse(float D, float start, float gain) {
  if (!(D > start) || gain <= 0.0f) return D;
  const float x = (-1.0f + sqrtf(1.0f + 4.0f * gain * (D - start))) /
                  (2.0f * gain);
  return start + x;
}

// Move a hand point out along the line from the anchor, by the reach curve.
// units_per_meter turns world units into meters and back. max_units is the
// far limit of the reach in world units, and 0 gives no limit. The limit
// never pulls the point in nearer than the hand itself. factor_out is D / d,
// the growth of the distance, and 1 inside the one to one zone.
inline void ReachPoint(const float anchor[3], const float hand[3],
                       float units_per_meter, float start, float gain,
                       float max_units, float out[3], float* factor_out) {
  const float d_units = Distance(hand, anchor);
  float factor = 1.0f;
  if (units_per_meter > 0.0f && d_units > 1e-5f) {
    const float d = d_units / units_per_meter;
    float reach = ReachDistance(d, start, gain);
    if (max_units > 0.0f && reach * units_per_meter > max_units) {
      reach = max_units / units_per_meter;
      if (reach < d) reach = d;
    }
    factor = reach / d;
  }
  for (int i = 0; i < 3; ++i) {
    out[i] = anchor[i] + (hand[i] - anchor[i]) * factor;
  }
  *factor_out = factor;
}

// THE REACH ANCHOR, in the room: the recenter origin, which is the head
// center at the last recenter, moved down by down_meters to about the chest.
// The room's up is +Y.
inline void ReachAnchor(float units_per_meter, float down_meters,
                        float out[3]) {
  out[0] = 0.0f;
  out[1] = -down_meters * units_per_meter;
  out[2] = 0.0f;
}

// The direct hand's point in the world, from one head sample (see
// EyeFromLean). The reach curve moves the grip out along the line from the
// anchor, in the room, where the head has no part in it. Then the anchor,
// the grip and the reach point go into the world with the same step. With
// full six degrees of freedom the reach point is
// game_position + L * (anchor + (t_grip - anchor) * factor), so the head
// does not move it.
inline void DirectReach(const float eye[3], const float room[9],
                        const float t_head[3], const float t_grip[3],
                        const float anchor_room[3], float units_per_meter,
                        float start, float gain, float max_units,
                        float anchor_out[3], float grip_out[3],
                        float reach_out[3], float* factor_out) {
  float reach_room[3];
  ReachPoint(anchor_room, t_grip, units_per_meter, start, gain, max_units,
             reach_room, factor_out);
  RoomToWorld(eye, room, t_head, anchor_room, anchor_out);
  RoomToWorld(eye, room, t_head, t_grip, grip_out);
  RoomToWorld(eye, room, t_head, reach_room, reach_out);
}

// THE DIRECT HAND ON THE TABLE (docs/tabletop.md, "The hand on the table").
//
// The table maps the room to the world from its own origin P0, the recenter
// pose over the table, with the table basis Lt. head_pose.cpp gives room
// points at the life size scale (7.5 units per meter), and the table is s
// units per meter, so every room point is multiplied by gain = s / 7.5 first.
// There is no reach curve: the whole land is within arm's reach.
//
//   E    = P0 + Lt * (gain * lean)
//   grip = E + Lt * (gain * (t_grip - t_head))
//
// With full six degrees of freedom the lean is t_head, so the grip is
// P0 + Lt * (gain * t_grip), which is C + s * Lt * (room - Tc) in meters. A
// real hand on the table top touches the land under it, and a head that
// moves with a still hand moves nothing.
inline void ScaleRoom(const float in[3], float gain, float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = in[i] * gain;
}

inline void TableHand(const float origin[3], const float basis[9], float gain,
                      const float lean[3], const float t_head[3],
                      const float t_grip[3], float eye_out[3],
                      float grip_out[3]) {
  float lean_g[3], head_g[3], grip_g[3];
  ScaleRoom(lean, gain, lean_g);
  ScaleRoom(t_head, gain, head_g);
  ScaleRoom(t_grip, gain, grip_g);
  EyeFromLean(origin, basis, lean_g, eye_out);
  RoomToWorld(eye_out, basis, head_g, grip_g, grip_out);
}

// The room point, at the life size scale, that the table puts on a world
// point: the inverse of P0 + Lt * (gain * t). The fake controller's word
// "seek" uses it on the table.
inline void TableRoomPoint(const float origin[3], const float basis[9],
                           float gain, const float world[3], float out[3]) {
  const float d[3] = {world[0] - origin[0], world[1] - origin[1],
                      world[2] - origin[2]};
  Apply(basis, true, d, out);
  if (gain > 0.0f) {
    for (int i = 0; i < 3; ++i) out[i] /= gain;
  }
}

// The unit direction from a point to a target. False when the two are the
// same point. The land drag casts its rays from the game camera along this
// direction to the reach point.
inline bool DirectionTo(const float from[3], const float to[3],
                        float direction_out[3]) {
  const float span = Distance(to, from);
  if (!(span > 1e-5f)) return false;
  for (int i = 0; i < 3; ++i) direction_out[i] = (to[i] - from[i]) / span;
  return true;
}


// THE MOTION OF A HAND.
//
// A sample is a point of the hand and the time it was there, in seconds. The
// direct hand keeps the last few of them, oldest first. The throw and the
// slap read the mean velocity over a short window of them.
struct MotionSample {
  double time;
  float point[3];
};

// The mean velocity over the last window seconds, in units of the point per
// second. It runs from the oldest sample inside the window to the newest one.
// When only the newest sample is inside the window (a slow frame rate), it
// runs from the sample just before it. False with fewer than two samples, or
// when the two ends are less than 4 ms apart.
inline bool MeanVelocity(const MotionSample* samples, int count,
                         double window, float out[3]) {
  if (count < 2) return false;
  const MotionSample& last = samples[count - 1];
  int first = count - 1;
  while (first > 0 &&
         last.time - samples[first - 1].time <= window + 1e-6) {
    --first;
  }
  if (first > count - 2) first = count - 2;
  const double span = last.time - samples[first].time;
  if (!(span >= 0.004)) return false;
  for (int i = 0; i < 3; ++i) {
    out[i] = static_cast<float>((last.point[i] - samples[first].point[i]) /
                                span);
  }
  return true;
}

inline float Length(const float v[3]) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// The velocity of a throw, as the game wants it, in world units per second.
//
// The DIRECTION is where the Hand moved: the velocity of the reach point in
// the room, turned into the world by the room basis. The SPEED is the real
// speed of the grip, times the gain. The reach curve multiplies the Hand's
// own speed by 10 to 40 for a Hand at 1 to 6 meters, and the game cuts a throw
// at cap (124 units per second, 16.5 m/s), so the Hand's speed would always
// hit the cap. The real speed does not depend on the reach.
//
// A speed above cap is cut to cap with the direction kept. The game does the
// same cut (0x005D4F75), so the mod cuts first and can say what it sent.
// speed_before_cut_out is the speed after the gain and before the cut. With
// no reach motion, the direction is the grip's own.
inline void ThrowVelocity(const float v_reach_room[3],
                          const float v_grip_room[3], const float room[9],
                          float gain, float cap, float out[3],
                          float* speed_before_cut_out) {
  float direction[3];
  Apply(room, false, v_reach_room, direction);
  float length = sqrtf(direction[0] * direction[0] +
                       direction[1] * direction[1] +
                       direction[2] * direction[2]);
  if (!(length > 1e-6f)) {
    Apply(room, false, v_grip_room, direction);
    length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] +
                   direction[2] * direction[2]);
  }
  const float grip_speed = sqrtf(v_grip_room[0] * v_grip_room[0] +
                                 v_grip_room[1] * v_grip_room[1] +
                                 v_grip_room[2] * v_grip_room[2]);
  float speed = grip_speed * gain;
  *speed_before_cut_out = speed;
  if (!(length > 1e-6f)) {
    out[0] = out[1] = out[2] = 0.0f;
    return;
  }
  if (cap > 0.0f && speed > cap) speed = cap;
  for (int i = 0; i < 3; ++i) out[i] = direction[i] / length * speed;
}

// THE THROW ON THE TABLE (docs/tabletop.md, "The throw from inside the
// influence").
//
// The room velocities are at the life size scale, 7.5 units per meter. On the
// table a meter of the room is s units, so the speed is multiplied by
// table_gain = s / 7.5 before the cut. At 2000 units per meter a hand at 5 cm
// per second already reaches the game's cap, so every real throw goes at the
// cap, in the direction of the hand.
//
// The game sets an object down when its horizontal speed is drop_speed (2
// units per second) or less (InitialisePhysicsFromHand). On the table the
// tremble of a still hand is more than that. So a hand that would set the
// object down at the life size scale sets it down on the table too: when the
// horizontal speed at the life size scale, times the gain, is drop_speed or
// less, the velocity is zero. drop_out says which it was.
inline void TableThrowVelocity(const float v_reach_room[3],
                               const float v_grip_room[3], const float room[9],
                               float gain, float table_gain, float cap,
                               float drop_speed, float out[3],
                               float* speed_before_cut_out, bool* drop_out) {
  const float flat = sqrtf(v_grip_room[0] * v_grip_room[0] +
                           v_grip_room[2] * v_grip_room[2]) * gain;
  *drop_out = !(flat > drop_speed);
  if (*drop_out) {
    out[0] = out[1] = out[2] = 0.0f;
    *speed_before_cut_out = Length(v_grip_room) * gain * table_gain;
    return;
  }
  ThrowVelocity(v_reach_room, v_grip_room, room, gain * table_gain, cap, out,
                speed_before_cut_out);
}

// Where the Hand was one game step ago, as the game should think it was, for
// the speed test of the Creature. The game reads the speed from the current
// and the previous screen point, so the previous point is the target moved
// back by the velocity times the game's step, with the gain on the velocity.
inline void PreviousPoint(const float target[3], const float v_world[3],
                          float gain, float dt, float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = target[i] - v_world[i] * gain * dt;
}

// THE HAND'S POINT ON THE LAND (docs/hand.md, "5. The touch of the land").
// A Hand pushed into the land stops on it. The game keeps its own Hand half
// its length over the land, which is 9.5 cm of the room for a hand of 19 cm
// at every scale, so the point could not touch the land or a villager on it.
// So the point comes down to clearance_m meters of the room over the land,
// at the scale now (units_per_meter). Returns the lowest height of the point.
inline float HandLandFloor(float land, float units_per_meter,
                           float clearance_m) {
  if (!(units_per_meter > 0.0f) || !(clearance_m > 0.0f)) return land;
  return land + clearance_m * units_per_meter;
}

// THE GRIP HOLDS (docs/grab.md, "The grip holds"). The game picks up with a
// click of the right button and lets go with a second click. A real hand
// takes, carries and lets go with one close and one open. This machine turns
// the grip into the game's right button:
//
//   Pass     The grip is the button, one to one, as in the plain game. A close on an
//            object starts the game's pick up, and a close on a tree pulls
//            it (the Tug). A Miracle, the leash and the Creature also stay
//            here.
//   Carry    An object came into the Hand while the grip is closed, and the
//            pick up is over. The button goes up, and the grip carries the
//            object with no button. So the trigger can still drag the land.
//   OpenHeld An object is in the Hand and the grip is open, for example
//            after the game refused a drop. A close carries it again.
//   LetGo    The grip opened while it carried. The machine clicks the button:
//            down, then up when the game shows the throw spring (hand mode
//            0x17) on a later frame, when the object left the Hand, or after
//            kGripSpringMs. The game then runs its release with the
//            velocity of the opening.
//
// The click waits while the game holds the other button (a land drag),
// because both buttons together are a camera mode and not a drop. A close
// before the click starts carries again. The press of the click comes at
// least one frame after the button went up.
//
// THE BULK TAKE (docs/grab.md, "The bulk take"). While the game takes from a
// pile, a field or a fish farm (GripFacts::take), the rule is:
//
//   The grip is closed   The game collects. The grip is the button, one to
//                        one, in the Pass phase.
//   The grip opens       The button goes up, and the game ends the take. The
//                        bundle stays in the Hand (OpenHeld). No throw and no
//                        click.
//   Close and open again The grip carries the bundle, and the opening lets it
//                        go, as for any object.
//
// Nothing collects after the grip opens. If the take still runs
// kGripTakeEndMs after the button went up, the step asks for the end of
// the take (end_take) on each frame until the take is over.
//
// THE TIMES OF THE GRIP are milliseconds of real time, so the grip does the
// same thing at each frame rate. A count of frames is only the order of two
// events: the game must read a release in one frame and a press in a later
// frame.
enum GripPhase { kGripPass = 0, kGripCarry, kGripOpenHeld, kGripLetGo };

// What the step reports, as bits, for the log and the report.
enum GripEvent {
  kGripCarried = 1,     // the take is over, the grip carries
  kGripOpenedHeld = 2,  // an object is in the Hand with the grip open
  kGripLettingGo = 4,   // the grip opened while it carried
  kGripLate = 8,        // the object came after the grip opened: let go
  kGripClickDown = 16,  // the press of the click
  kGripClickUp = 32,    // the release of the click
  kGripRegrip = 64,     // the grip closed again on the object
  kGripGone = 128,      // the object left the Hand another way
  kGripLost = 256,      // the controller does not place the Hand now
  kGripWaits = 512,     // the click waits for the other button
  kGripHeldOn = 1024,   // the grip opened, and the press of the game stays
  kGripHeldOff = 2048   // that press is over
};

// THE PRESS OF THE GAME (grip_press_hold, docs/grab.md, "The grip holds").
// The game has two ways to take an object.
//
// An object at rest is in the Hand of the game one or two game turns after
// the press, and the pick lock (GInterface+0x38) is clear some frames later.
//
// An object that the physics of the game has (it falls, flies or rolls) is in
// the Hand of the picture at once (fn_0046DC30 writes CHand+0x4904). The step
// of the pick up action (fn_005D5250) then waits for a button that stays
// down for 225 ms. Only then it sends the pick up (fn_005D2800, packet 0x13),
// and the game has the object in its Hand one game turn later. The step of
// the action after that (fn_005D4A90) then clears the pick lock, with the
// button down or up. A button that goes up before the 225 ms is a click: the
// Hand lets the object go (CHand::ThrowObject), and the game taps the object
// (fn_005D3930). The tap of a rock splits it (Rock::InterfaceTap).
//
// So the button of the game stays down while the grip is closed, until the
// pick up is over: the game has the object in its Hand (GripFacts::game_holds)
// and the pick lock is clear. No time ends the press of a closed grip while a
// release is a click for the game. A pick up that the game does not end in
// kGripSettleMs is a carry from then on. With the button up there is no
// press to end, so an object that is in the Hand with the grip open has that
// state after kGripSettleOpenMs.
//
// A grip that opens before that, on an object that moves, keeps the button
// down until the press is no click for the game (GripFacts::click), and for
// kGripHoldMostMs at most. The game then takes the object, and the machine
// lets it go with its click, as for each object that comes after the grip
// opened. That click waits until the game has the object in its Hand: before
// that it is a press on the object, and a new pick up. A short press on an
// object at rest is a click, as in the plain game.
//
// grip_press_hold=0 gives a carry when the pick lock is clear, or
// kGripSettleBeforeMs after the object came into the Hand of the picture,
// and an open grip never keeps the button down. That time is 15 frames at 90
// frames a second. It is less than the 225 ms of the game, so with this
// value a grip on an object that moves is a click.
const uint64_t kGripClickMs = 225;      // the game: a shorter press is a click
const uint64_t kGripHoldMs = 240;       // the mod: the least age of that press
const uint64_t kGripHoldMostMs = 1500;  // an open grip holds a press no longer
const uint64_t kGripSettleMs = 1000;
const uint64_t kGripSettleOpenMs = 150;
const uint64_t kGripSettleBeforeMs = 167;
// The click lets go of the button when the throw spring shows, or after
// this time. The desktop runs saw the spring 2 frames after a press.
const uint64_t kGripSpringMs = 130;
// An object that comes into the Hand this soon after the press ended on a
// pick up is let go at once. The player has already opened the hand.
const uint64_t kGripLateMs = 350;
// A bulk take that still runs this long after the button went up must be
// ended by the mod. The game ends a take one game turn after the release,
// and a game turn is 100 ms, so this is two game turns.
const uint64_t kGripTakeEndMs = 200;

// THE AGE OF A PRESS ON THE CLOCK OF THE GAME. The step of the pick up action
// (fn_005D5250, 0x005D5254 to 0x005D5293) measures a press two ways and takes
// the smaller number:
//   the real time since the press: the clock that LH3DRender::StartFrame
//     writes (0x00EA9EB0) less the clock at the press (GInterface+0x454),
//   100 ms for each game turn since the press, and 100 more: the game turn
//     now (GGame+0x205A40) and the game turn at the press (GInterface+0x458).
// So a press is 225 ms old only when that real time is over and the second
// game turn after the press has started. The numbers are unsigned, as in the
// game.
inline uint32_t GamePressAge(uint32_t clock_ms, uint32_t press_ms,
                             uint32_t turn, uint32_t press_turn) {
  const uint32_t real = clock_ms - press_ms;
  const uint32_t by_turn = (turn + 1u - press_turn) * 100u;
  return real < by_turn ? real : by_turn;
}

// True while a button that goes up now is a click for the game, with the
// margin of the mod (kGripHoldMs and not kGripClickMs): the pump reads the
// clock of the frame before, and the game compares with 225 two ways.
inline bool GameClick(uint32_t press_age_ms) {
  return press_age_ms < kGripHoldMs;
}

struct GripFacts {
  bool direct;      // the controller places the Hand and can act now
  bool holding;     // the Holding state, with an object that is no Miracle
  bool settled;     // the pick lock (GInterface+0x38) is clear
  bool spring;      // the interface hand mode is 0x17, the throw spring
  bool other_held;  // the game holds the other button, or its control is down
  bool take;        // the game's bulk take runs (GInterfaceStatus+0x3C)
  bool mod_take;    // the mod's own take from a forest runs (forest_take.cpp)
  bool game_holds;  // the game has an object in its Hand
                    // (GInterfaceStatus+0x90)
  bool moving;      // the Hand of the picture has an object that the physics
                    // of the game has
  bool click;       // a button that goes up now is a click for the game
                    // (GameClick of the age of the press)
  bool press_hold;  // the key grip_press_hold
};

struct GripHold {
  int phase;
  bool grip;              // the grip at the last step
  bool wait_up;           // Pass: the grip must open before it presses
  bool button;            // the last output
  uint64_t button_frame;  // the frame the output last changed
  uint64_t press_ms;      // the time the output last went down
  bool clicked;           // LetGo: the press of the click is out
  uint64_t click_frame;   // the frame of that press
  uint64_t click_ms;      // and its time
  bool take_opened;       // Pass: a press ended, maybe before its pick up
  uint64_t open_ms;       // the time it ended
  bool holding_seen;      // the object is in the Hand
  uint64_t holding_ms;    // since this time
  bool take_up;           // a bulk take runs and the button is up
  uint64_t take_up_ms;    // since this time
  bool held_on;           // Pass: the grip is open, and the press stays down
};

inline void GripReset(GripHold* s) {
  s->phase = kGripPass;
  s->grip = false;
  s->wait_up = false;
  s->button = false;
  s->button_frame = 0;
  s->press_ms = 0;
  s->clicked = false;
  s->click_frame = 0;
  s->click_ms = 0;
  s->take_opened = false;
  s->open_ms = 0;
  s->holding_seen = false;
  s->holding_ms = 0;
  s->take_up = false;
  s->take_up_ms = 0;
  s->held_on = false;
}

struct GripStepOut {
  bool button;  // the game's button
  bool click;   // the button is the machine's click: it needs no fresh press
                // and no pixel in view, because it lets go and picks nothing
  bool opened;  // the grip opened on this step: latch the throw now
  int events;   // GripEvent bits
  bool end_take;     // the grip is open and the bulk take still runs: end it
  bool take_opened;  // the grip opened on this step during a bulk take
};

// True when the pick up of the game is over, so the grip can carry. held_ms
// is the time since the object came into the Hand of the picture, and button
// is true while the button of the game is down.
inline bool GripPickUpOver(const GripFacts& f, uint64_t held_ms,
                           bool button) {
  if (!f.holding) return false;
  if (!f.press_hold) return f.settled || held_ms >= kGripSettleBeforeMs;
  if (f.settled && f.game_holds) return true;
  return !f.click &&
         held_ms >= (button ? kGripSettleMs : kGripSettleOpenMs);
}

// One step, once for each pass of the pump. grip is the grip with its
// hysteresis. frame counts the controller frames, and now_ms is the real
// time of that frame in milliseconds. A second step in one frame sees the
// same grip and the same time, and changes nothing.
inline GripStepOut GripStep(GripHold* s, bool grip, const GripFacts& f,
                            uint64_t frame, uint64_t now_ms) {
  GripStepOut out = {false, false, false, 0, false, false};
  const bool opened = s->grip && !grip;
  const bool closed = !s->grip && grip;
  s->grip = grip;
  if (!grip) s->wait_up = false;
  const bool take = f.direct && (f.take || f.mod_take);
  out.opened = opened && f.direct && !take;
  out.take_opened = opened && take;
  if (!take) s->take_up = false;

  if (!f.direct) {
    // The mouse places the Hand, or the hand cannot act: the grip is the
    // game's button, as in the plain game. A grip that carried must open first.
    if (s->phase != kGripPass) {
      out.events |= kGripLost;
      s->wait_up = grip;
      s->phase = kGripPass;
    }
    s->clicked = false;
    s->take_opened = false;
    s->holding_seen = false;
  } else if (take) {
    // THE BULK TAKE. The grip is the button, one to one: closed collects, and
    // the opening ends the take. The Pot in the Hand is no carry, and the
    // opening is no let go. A grip that carried must open first.
    if (s->phase != kGripPass) {
      s->wait_up = grip;
      s->phase = kGripPass;
    }
    s->clicked = false;
    s->take_opened = false;
    s->holding_seen = false;
    if (f.mod_take && !f.take) {
      // THE TAKE OF THE MOD from a forest needs no button of the game. The
      // grip only says go on or stop: a closed grip never presses, and the
      // mod ends its own take when the grip opens.
      s->wait_up = grip;
      s->take_up = false;
    } else if (grip && !s->wait_up) {
      s->take_up = false;
    } else if (!s->take_up) {
      s->take_up = true;
      s->take_up_ms = now_ms;
    } else if (now_ms - s->take_up_ms >= kGripTakeEndMs) {
      // The button is up, and the game still collects. Nothing may collect
      // after the grip opens, so the mod must end the take.
      out.end_take = true;
    }
  } else {
    if (!f.holding) {
      s->holding_seen = false;
    } else if (!s->holding_seen) {
      s->holding_seen = true;
      s->holding_ms = now_ms;
    }
    const bool done =
        GripPickUpOver(f, now_ms - s->holding_ms, s->button);
    switch (s->phase) {
      case kGripPass:
        if (opened && s->button) {
          s->take_opened = true;
          s->open_ms = now_ms;
        }
        if (done) {
          // A press ended a moment ago, and its object comes now.
          const bool late =
              s->take_opened && now_ms - s->open_ms <= kGripLateMs;
          if (grip) {
            s->phase = kGripCarry;
            out.events |= kGripCarried;
            s->take_opened = false;
          } else if (late && (f.game_holds || !f.press_hold)) {
            s->phase = kGripLetGo;
            s->clicked = false;
            out.events |= kGripLate;
            s->take_opened = false;
          } else if (!late) {
            s->phase = kGripOpenHeld;
            out.events |= kGripOpenedHeld;
            s->take_opened = false;
          }
          // Else the game does not have the object in its Hand yet. A click
          // now is a press on the object, so the machine waits for the game.
        }
        break;
      case kGripCarry:
        if (!f.holding) {
          s->phase = kGripPass;
          s->wait_up = grip;
          out.events |= kGripGone;
        } else if (opened) {
          s->phase = kGripLetGo;
          s->clicked = false;
          out.events |= kGripLettingGo;
        }
        break;
      case kGripOpenHeld:
        if (!f.holding) {
          s->phase = kGripPass;
          out.events |= kGripGone;
        } else if (closed) {
          s->phase = kGripCarry;
          out.events |= kGripRegrip;
        }
        break;
      case kGripLetGo:
        if (!s->clicked) {
          if (!f.holding) {
            s->phase = kGripPass;
            s->wait_up = grip;
            out.events |= kGripGone;
          } else if (grip) {
            s->phase = kGripCarry;
            out.events |= kGripRegrip;
          }
        } else if ((f.spring && frame > s->click_frame) || !f.holding ||
                   now_ms - s->click_ms >= kGripSpringMs) {
          // The spring shows, the time is up, or the object already left
          // the Hand: an empty Hand must not keep the press.
          s->clicked = false;
          s->phase = kGripPass;
          s->wait_up = grip;
          out.events |= kGripClickUp;
        }
        break;
      default:
        s->phase = kGripPass;
        break;
    }
  }

  bool button = false;
  bool held_on = false;
  if (s->phase == kGripPass) {
    button = grip && !s->wait_up;
    // THE PRESS ON AN OBJECT THAT MOVES. The grip is open, the button was
    // down, and a button that goes up now is a click for the game: the Hand
    // lets the object go, and a rock splits. So the button stays down until
    // the press is no click. The press ends later than the grip opened, so
    // the time of a late object counts from here.
    if (!button && s->button && f.direct && !take && f.press_hold &&
        f.moving && !f.settled && f.click &&
        now_ms - s->press_ms < kGripHoldMostMs) {
      button = true;
      held_on = true;
      s->take_opened = true;
      s->open_ms = now_ms;
    }
  } else if (s->phase == kGripLetGo) {
    out.click = true;
    if (s->clicked) {
      button = true;
    } else if (f.other_held) {
      out.events |= kGripWaits;
    } else if (!s->button && frame > s->button_frame) {
      // A fresh press: the button was up for a frame at least. A take press
      // that is still down goes up on this step first.
      button = true;
      s->clicked = true;
      s->click_frame = frame;
      s->click_ms = now_ms;
      out.events |= kGripClickDown;
    }
  }
  if (held_on != s->held_on) {
    s->held_on = held_on;
    out.events |= held_on ? kGripHeldOn : kGripHeldOff;
  }
  if (button != s->button) {
    s->button = button;
    s->button_frame = frame;
    if (button) s->press_ms = now_ms;
  }
  out.button = button;
  return out;
}

// THE GRIP AFTER A STATE OF THE FAR GRAB (far_grab.cpp, docs/grab.md, "A bulk
// source at a distance"). While the far grab has the grip (a lock, a pull, a
// fetch), the grip machine sees an open grip. When such a state ends with the
// grip still closed, the grip stays held back until it opens, so it is no new
// press for the machine.
//
// In the Take state the machine has the grip: it is the game's button for the
// take. A grip that is held back there looks like an opening to the machine.
// When that comes in the frame in which the game ends the take (the game
// deletes a pile that is used up in that game turn), the machine reads an
// opening of a press whose object came late, and it lets the bundle go with
// its click. So the end of the Take state does not hold the grip back
// (take_carry, the key far_take_carry): the machine sees the closed grip, and
// the grip carries the bundle.
//
// True when the far grab must hold the grip back until it opens.
inline bool FarHoldsBack(bool grip_closed, bool take_state, bool take_carry) {
  if (!grip_closed) return false;
  return !(take_state && take_carry);
}

// THE GRIP OPENS AT ONCE (docs/throw.md, "The release at once"). A grip or a
// trigger gives a value from 0 to 1. The control closes above
// kGripCloseAbove and opens below kGripOpenBelow, as in the plain game. A hand that
// opens for a throw takes some frames to come below kGripOpenBelow, so the
// Hand let go late. With fast_open, a closed control also opens when its
// value falls fast: by kGripFastFall or more in kGripFastFrames frames, to a
// value below kGripFastBelow. A slow change opens only below kGripOpenBelow.
// After a fast opening the control closes again only when its value rises:
// by kGripReclose from its lowest value, or from below kGripCloseAbove.
const float kGripCloseAbove = 0.55f;
const float kGripOpenBelow = 0.35f;
const float kGripFastFall = 0.30f;
const float kGripFastBelow = 0.80f;
const float kGripReclose = 0.10f;
const int kGripFastFrames = 3;
const int kGripTrace = 12;

struct GripLevel {
  bool down;
  bool fast;                // the last opening was a fast fall
  float low;                // the lowest value since the opening
  float trace[kGripTrace];  // the values of the last frames, the newest last
  int count;
};

inline void GripLevelReset(GripLevel* s) {
  s->down = false;
  s->fast = false;
  s->low = 0.0f;
  s->count = 0;
  for (int i = 0; i < kGripTrace; ++i) s->trace[i] = 0.0f;
}

// One value a frame. Returns true while the control is closed.
inline bool GripLevelStep(GripLevel* s, float value, bool fast_open) {
  if (!(value >= 0.0f)) value = 0.0f;
  if (s->count == kGripTrace) {
    for (int i = 1; i < kGripTrace; ++i) s->trace[i - 1] = s->trace[i];
    --s->count;
  }
  s->trace[s->count++] = value;
  if (s->down) {
    bool open = !(value > kGripOpenBelow);
    bool fast = false;
    if (!open && fast_open && value < kGripFastBelow) {
      float high = value;
      for (int k = 1; k <= kGripFastFrames; ++k) {
        const int i = s->count - 1 - k;
        if (i >= 0 && s->trace[i] > high) high = s->trace[i];
      }
      if (high - value >= kGripFastFall) open = fast = true;
    }
    if (open) {
      s->down = false;
      s->fast = fast;
      s->low = value;
    }
  } else {
    if (value < s->low) s->low = value;
    s->down = value > kGripCloseAbove &&
              (s->low <= kGripCloseAbove || value >= s->low + kGripReclose);
    if (s->down) s->fast = false;
  }
  return s->down;
}

// THE PRESS GUARD (docs/grab.md, "The grip holds"). A press of the Action
// button that ends in less than 225 ms is a click for the game: it taps the
// object in the pick record, and a rock splits. A hand that jerks away squeezes
// less for a frame or two, and that can look like a fast fall. The fast fall
// then opens a grip that the player still holds, and the short press breaks
// the rock. So from the close of the grip, and while the machine is still in
// its Pass phase (the pick up is not over), only the plain level (below
// kGripOpenBelow) opens the grip. kGripGuardMs is longer than the 225 ms of
// the game. A carry (the object is in the Hand) has the fast fall again, for
// the throw.
const uint64_t kGripGuardMs = 300;

// True when the fast fall must not open the grip now. guard_on is the ini key
// grip_press_guard. closed is the grip as the last step left it. phase is the
// GripPhase of the last step. since_close_ms is the time since the grip
// closed.
inline bool GripPressGuarded(bool guard_on, bool closed, int phase,
                             uint64_t since_close_ms) {
  return guard_on && closed && phase == kGripPass &&
         since_close_ms < kGripGuardMs;
}

// THE GRAB OF THE WORLD. See docs/god-view.md, "5.1 The grab of the land".
//
// A point a in the room is the world point C + L * a, with C the game camera
// and L the room basis. A grab keeps one world point in the hand. It solves
// the camera from the press every time, and it never adds steps. So the same
// hands always give the same camera, a second call in one frame changes
// nothing, and rounding cannot drift. Only the hand points go in. The head has
// no part in the solve, so a head that moves with still hands moves nothing.

// A turn about world up, in the matrix form of the room basis L. A positive
// angle turns forward (+Z) toward right (+X).
inline void TurnUp(float angle, const float in[3], float out[3]) {
  const float c = cosf(angle), s = sinf(angle);
  out[0] = c * in[0] + s * in[2];
  out[1] = in[1];
  out[2] = -s * in[0] + c * in[2];
}

// The heading of a vector about up, in the sense of TurnUp: TurnUp(angle)
// adds angle to it.
inline float HeadingOf(const float v[3]) { return atan2f(v[0], v[2]); }

inline float WrapAngle(float angle) {
  const float pi = 3.14159265f;
  while (angle > pi) angle -= 2.0f * pi;
  while (angle < -pi) angle += 2.0f * pi;
  return angle;
}

// The room basis of a camera: world up, and the heading from the camera to its
// focus, with columns right, up and forward. head_camera.cpp builds the same
// basis. False when the camera looks straight up or down.
inline bool LevelBasis(const float position[3], const float focus[3],
                       float out[9]) {
  float fx = focus[0] - position[0];
  float fz = focus[2] - position[2];
  const float length = sqrtf(fx * fx + fz * fz);
  if (!(length > 1e-3f)) return false;
  fx /= length;
  fz /= length;
  const float right[3] = {fz, 0.0f, -fx};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  const float forward[3] = {fx, 0.0f, fz};
  for (int i = 0; i < 3; ++i) {
    out[i * 3 + 0] = right[i];
    out[i * 3 + 1] = up[i];
    out[i * 3 + 2] = forward[i];
  }
  return true;
}

// The hand point the grab uses, scaled about the head at the press:
//
//   v = r0 + G * (a - r0),  G = diag(g, g_vertical, g)
//
// r0 is fixed at the press, so v does not move when the head moves. Between
// two moments of one grab, v changes by G times the change of a.
inline void GrabVirtual(const float r0[3], const float a[3], float g,
                        float g_vertical, float out[3]) {
  out[0] = r0[0] + g * (a[0] - r0[0]);
  out[1] = r0[1] + g_vertical * (a[1] - r0[1]);
  out[2] = r0[2] + g * (a[2] - r0[2]);
}

// The gain that makes the land look glued to the hand: the distance from the
// camera to its focus over the distance from the eye to the hand. The hand
// distance is kept between 0.25 and 0.70 m. percent multiplies the result,
// and the result stays between 1 and most. Both distances are in units.
inline float GrabGain(float camera_to_focus, float eye_to_hand,
                      float units_per_meter, float percent, float most) {
  float hand = eye_to_hand;
  if (hand < 0.25f * units_per_meter) hand = 0.25f * units_per_meter;
  if (hand > 0.70f * units_per_meter) hand = 0.70f * units_per_meter;
  if (!(hand > 0.0f)) return 1.0f;
  float g = camera_to_focus / hand * percent * 0.01f;
  if (g > most) g = most;
  if (!(g >= 1.0f)) g = 1.0f;
  return g;
}

// A camera of CameraModeNew3: the position, the focus and the pivot it turns
// about (mode+0x12C).
struct GrabPose {
  float position[3];
  float focus[3];
  float pivot[3];
};

// What a grab keeps from its press (or its last new anchor).
struct GrabAnchor {
  int hands;              // 1 or 2
  GrabPose pose;          // the camera at the press
  float room[9];          // L at the press
  float middle[3];        // the hand point v, or the middle of the two
  float spread[3];        // two hands: one real grip minus the other
  float least_distance;   // the zoom stops this near to the grabbed point
};

inline void AddScaled(const float base[3], const float step[3], float scale,
                      float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = base[i] + step[i] * scale;
}

// The world point under the middle of the hands at the press:
//
//   A = C_a + L_a * m_a
inline void GrabAnchorPoint(const GrabAnchor& anchor, float out[3]) {
  float offset[3];
  Apply(anchor.room, false, anchor.middle, offset);
  AddScaled(anchor.pose.position, offset, 1.0f, out);
}

// One solve of the camera from the hands now.
//
// middle is v now (one hand) or the middle of the two v points. spread is one
// real grip minus the other now, in room units, the same way round as at the
// press. With two hands:
//
//   theta = the change of the heading of the spread, about up
//   k     = |spread| / |spread at the press|, kept between 0.25 and 4
//   L     = TurnUp(-theta) * L_a              the world turns with the hands
//   C     = A - L * m / k                     the rigid solve, then the zoom
//   F     = C + TurnUp(-theta) * (F_a - C_a)
//   P     = C + TurnUp(-theta) * (P_a - C_a)
//
// With one hand, or theta 0 and k 1, this is C = C_a - L_a * (v - v_a). The
// zoom is a dolly toward A, and the distance to the focus stays. The turn is
// about world up only, so the horizon stays level.
//
// theta and k hold their last values (the ones passed in) when the turn or
// the zoom is off, or when the real spread is below min_spread, where its
// heading is noise. So they freeze, and they never jump.
inline void GrabSolve(const GrabAnchor& anchor, const float middle[3],
                      const float spread[3], bool turn, bool zoom,
                      float min_spread, float* theta_io, float* k_io,
                      GrabPose* out) {
  float theta = *theta_io;
  float k = *k_io;
  if (anchor.hands == 2) {
    const float flat_a = sqrtf(anchor.spread[0] * anchor.spread[0] +
                               anchor.spread[2] * anchor.spread[2]);
    const float flat = sqrtf(spread[0] * spread[0] + spread[2] * spread[2]);
    if (turn && flat_a >= min_spread && flat >= min_spread) {
      theta = WrapAngle(HeadingOf(spread) - HeadingOf(anchor.spread));
    }
    const float length_a = Length(anchor.spread);
    const float length = Length(spread);
    if (zoom && length_a >= min_spread && length >= min_spread) {
      k = length / length_a;
      if (k < 0.25f) k = 0.25f;
      if (k > 4.0f) k = 4.0f;
    }
  } else {
    theta = 0.0f;
    k = 1.0f;
  }
  *theta_io = theta;
  *k_io = k;

  float a_point[3];
  GrabAnchorPoint(anchor, a_point);
  float in_world[3], q[3];
  Apply(anchor.room, false, middle, in_world);
  TurnUp(-theta, in_world, q);
  const float span = Length(q);
  float distance = span / k;
  if (k > 1.0f && distance < anchor.least_distance) {
    distance = anchor.least_distance < span ? anchor.least_distance : span;
  }
  if (span > 1e-6f) {
    AddScaled(a_point, q, -distance / span, out->position);
  } else {
    for (int i = 0; i < 3; ++i) out->position[i] = a_point[i];
  }
  float offset[3], turned[3];
  for (int i = 0; i < 3; ++i) {
    offset[i] = anchor.pose.focus[i] - anchor.pose.position[i];
  }
  TurnUp(-theta, offset, turned);
  AddScaled(out->position, turned, 1.0f, out->focus);
  for (int i = 0; i < 3; ++i) {
    offset[i] = anchor.pose.pivot[i] - anchor.pose.position[i];
  }
  TurnUp(-theta, offset, turned);
  AddScaled(out->position, turned, 1.0f, out->pivot);
}

// Move a whole pose by a step: the camera, the focus and the pivot together.
inline void MovePose(const float step[3], GrabPose* pose) {
  for (int i = 0; i < 3; ++i) {
    pose->position[i] += step[i];
    pose->focus[i] += step[i];
    pose->pivot[i] += step[i];
  }
}

// THE WALL. Keep the camera and its focus both within radius of centre. The
// pose moves as a whole, to a place where both fit, near the place it was
// solved at. The two balls are convex, so moving to each in turn converges.
// True when the pose moved.
inline bool GrabKeepInside(const float centre[3], float radius,
                           GrabPose* pose) {
  bool moved = false;
  for (int round = 0; round < 24; ++round) {
    bool inside = true;
    float* points[2] = {pose->position, pose->focus};
    for (int p = 0; p < 2; ++p) {
      float offset[3];
      for (int i = 0; i < 3; ++i) offset[i] = points[p][i] - centre[i];
      const float distance = Length(offset);
      if (distance <= radius + 1e-4f || !(distance > 0.0f)) continue;
      inside = false;
      moved = true;
      float step[3];
      for (int i = 0; i < 3; ++i) {
        step[i] = offset[i] * (radius / distance - 1.0f);
      }
      MovePose(step, pose);
    }
    if (inside) break;
  }
  return moved;
}

// THE FLOOR. Keep the camera at floor_y or higher. The pose moves straight
// up as a whole. True when it moved.
inline bool GrabKeepAbove(float floor_y, GrabPose* pose) {
  if (!(pose->position[1] < floor_y)) return false;
  const float step[3] = {0.0f, floor_y - pose->position[1], 0.0f};
  MovePose(step, pose);
  return true;
}

// A GESTURE IN THE AIR. See docs/miracles.md.
//
// The game reads a gesture as a path of screen pixels, x to the right and y
// down. The hand draws in 3D. So the grip point goes on a plane: the head's
// right and up axes at the moment the draw started, through the grip point of
// that moment. The plane stays for the whole stroke, so a head that turns
// while the hand draws does not bend the shape. Only the motion along the
// plane counts. A hand that moves toward the face adds nothing.
//
// The plane is in the room, where the grip is. A snap turn or a camera move
// changes the room basis L, not the room, so it does not bend the shape
// either.
struct GesturePlane {
  float origin[3];  // the grip at the start, room units
  float right[3];   // the head's right axis at the start
  float up[3];      // the head's up axis at the start
};

// The head matrix H (HeadPoseCurrent) turns the head's own axes into the
// room, so its columns are the head's right, up and forward axes.
inline void GesturePlaneFromHead(const float head[9], const float origin[3],
                                 GesturePlane* out) {
  for (int i = 0; i < 3; ++i) {
    out->origin[i] = origin[i];
    out->right[i] = head[i * 3 + 0];
    out->up[i] = head[i * 3 + 1];
  }
}

// How far right and up of the plane's origin a point of the room is, in room
// units.
inline void GestureFlatten(const GesturePlane& plane, const float point[3],
                           float* right_out, float* up_out) {
  float right = 0.0f, up = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float offset = point[i] - plane.origin[i];
    right += offset * plane.right[i];
    up += offset * plane.up[i];
  }
  *right_out = right;
  *up_out = up;
}

// The pixels for one room unit. A hand motion of size_meters becomes a third
// of the render height, which is the size of a gesture drawn with the mouse.
// Zero when a number is missing.
inline float GesturePixelsPerUnit(float render_height, float size_meters,
                                  float units_per_meter) {
  if (!(render_height > 0.0f) || !(size_meters > 0.0f) ||
      !(units_per_meter > 0.0f)) {
    return 0.0f;
  }
  return render_height / 3.0f / (size_meters * units_per_meter);
}

// The pixel of a grip point: the start pixel moved by the flat offset. The
// screen's y goes down, so up on the plane is a smaller y.
inline void GesturePixel(const GesturePlane& plane, const float point[3],
                         float start_x, float start_y, float pixels_per_unit,
                         float* x_out, float* y_out) {
  float right, up;
  GestureFlatten(plane, point, &right, &up);
  *x_out = start_x + right * pixels_per_unit;
  *y_out = start_y - up * pixels_per_unit;
}

// Whether a new sample is far enough from the last one. The mouse gives the
// game a sample only when it moves, so a hand that holds still adds none.
inline bool GestureStepFar(float last_x, float last_y, float x, float y,
                           float spacing) {
  const float dx = x - last_x, dy = y - last_y;
  return dx * dx + dy * dy >= spacing * spacing;
}

// THE CORNERS. The game finds the corners of a path from the turns between
// its samples. A sample every spacing pixels cuts a sharp corner, and the
// game then reads two half turns where the template has one. The mouse does
// not cut it, because a hand slows down at a corner and the mouse samples by
// time. So the draw keeps the pixels of the frames since the last sample.
// When the next sample is due, the pixel farthest from the straight line
// from the last sample to now goes in first, if it is more than tolerance
// pixels off that line. A curve stays within the tolerance, a corner does
// not. Returns the index of that pixel, or -1.
inline int GestureCornerIndex(const float (*points)[2], int count,
                              float from_x, float from_y, float to_x,
                              float to_y, float tolerance) {
  const float dx = to_x - from_x, dy = to_y - from_y;
  const float length = sqrtf(dx * dx + dy * dy);
  int best = -1;
  float worst = tolerance;
  for (int i = 0; i < count; ++i) {
    const float px = points[i][0] - from_x, py = points[i][1] - from_y;
    const float off = length > 1e-6f ? fabsf(px * dy - py * dx) / length
                                     : sqrtf(px * px + py * py);
    if (off > worst) {
      worst = off;
      best = i;
    }
  }
  return best;
}

// THE INTERFACE PANELS (docs/ui-layer.md, "3.2 The quad layers" and
// "4.3 Normal placement").
//
// A quad layer in the LOCAL space of OpenXR, which is right handed with y up
// and -z ahead. The quad shows its front, the +z side of its own frame. These
// work on quaternions x y z w, the order OpenXR uses.

// out = a * b
inline void QuatMultiply(const float a[4], const float b[4], float out[4]) {
  const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
  out[0] = x;
  out[1] = y;
  out[2] = z;
  out[3] = w;
}

// v turned by the unit quaternion q.
inline void QuatRotate(const float q[4], const float v[3], float out[3]) {
  // t = 2 * cross(q.xyz, v), out = v + w * t + cross(q.xyz, t)
  const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
  const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
  const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
  out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
  out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

// The pose of a panel. anchor and yaw are the recenter pose: the head
// position and a turn about y only. The panel center is distance meters from
// the anchor, pitch radians above eye level (below is negative), straight
// ahead of the yaw. The panel turns about its own x axis by the same pitch,
// so its front faces the anchor.
inline void PanelPose(const float anchor[3], const float yaw[4],
                      float distance, float pitch, float position_out[3],
                      float orientation_out[4]) {
  const float ahead[3] = {0.0f, distance * sinf(pitch),
                          -distance * cosf(pitch)};
  float turned[3];
  QuatRotate(yaw, ahead, turned);
  for (int i = 0; i < 3; ++i) position_out[i] = anchor[i] + turned[i];
  const float tilt[4] = {sinf(pitch * 0.5f), 0.0f, 0.0f, cosf(pitch * 0.5f)};
  QuatMultiply(yaw, tilt, orientation_out);
}

// The width in meters of a panel of the given pixels, at pixels_per_degree,
// seen from distance meters. Zero when a number is missing.
inline float PanelWidth(float pixels, float pixels_per_degree,
                        float distance) {
  if (!(pixels > 0.0f) || !(pixels_per_degree > 0.0f) ||
      !(distance > 0.0f)) {
    return 0.0f;
  }
  const float half = pixels / pixels_per_degree * 0.5f * 0.01745329f;
  return 2.0f * distance * tanf(half);
}

// THE GLOW AT THE HAND'S POINT (docs/hand-model.md, "The glow at the
// Hand's point"). A disc that faces the eye, projected as LH3DTech::ProjectPoint
// projects a point (docs/software-tnl.md).
struct GlowDisc {
  float x, y;    // the center, in pixels of the render
  float rx, ry;  // the radius across and up, in pixels
  float rhw, z;  // what the game writes for a point at the depth of the test
  float depth;   // the camera space depth of the point, in world units
};

// m is the game's world to camera matrix (0x00EA9E40): 4 rows of 3, with the
// translation in the last row. Its columns are the camera axes, and the
// field of view terms scale columns 0 and 1:
//
//   cam.x = p.x * m[0] + p.y * m[3] + p.z * m[6] + m[9]
//   screen x = (1 + cam.x / cam.z) * hx,  screen y = (1 - cam.y / cam.z) * hy
//
// The radius is in world units. A world offset r along the camera's right
// axis adds r times the length of column 0 to cam.x, so the length of each
// column gives the radius in pixels, with or without a known field of view.
// The game writes rhw = near / cam.z and z = 1 - rhw. The depth test uses a
// point pull units nearer to the eye, but never nearer than the near plane.
// The disc stays at the true point, so both eyes see it at its real depth.
// False when the point is not in front of the near plane, or when the disc is
// wholly outside the render.
inline bool GlowProject(const float m[12], float hx, float hy,
                        float near_plane, const float point[3], float radius,
                        float pull, GlowDisc* out) {
  if (!(near_plane > 0.0f) || !(hx > 0.0f) || !(hy > 0.0f)) return false;
  const float cx = point[0] * m[0] + point[1] * m[3] + point[2] * m[6] + m[9];
  const float cy = point[0] * m[1] + point[1] * m[4] + point[2] * m[7] + m[10];
  const float cz = point[0] * m[2] + point[1] * m[5] + point[2] * m[8] + m[11];
  if (!(cz > near_plane)) return false;
  const float across = sqrtf(m[0] * m[0] + m[3] * m[3] + m[6] * m[6]);
  const float up = sqrtf(m[1] * m[1] + m[4] * m[4] + m[7] * m[7]);
  out->x = (1.0f + cx / cz) * hx;
  out->y = (1.0f - cy / cz) * hy;
  out->rx = radius * across / cz * hx;
  out->ry = radius * up / cz * hy;
  if (out->x + out->rx < 0.0f || out->x - out->rx > 2.0f * hx ||
      out->y + out->ry < 0.0f || out->y - out->ry > 2.0f * hy) {
    return false;
  }
  // A negative pull puts the test point farther from the eye (the halo of a
  // far grab, docs/grab.md, "The grab from a distance").
  float front = cz - pull;
  if (front < near_plane) front = near_plane;
  out->rhw = near_plane / front;
  out->z = 1.0f - out->rhw;
  out->depth = cz;
  return true;
}

// THE CREATURE BY TOUCH (docs/creature.md, "The Creature by touch").
//
// The body of the Creature is a vertical capsule: the points within radius
// of a segment that goes up height units from base. hand_direct.cpp puts
// base on the land under the body bone.
struct Capsule {
  float base[3];
  float height;
  float radius;
};

// The distance from the point p to the segment from a to b.
inline float SegmentPointDistance(const float a[3], const float b[3],
                                  const float p[3]) {
  const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const float w[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
  const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  float t = 0.0f;
  if (dd > 1e-12f) t = (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / dd;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float sum = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float e = w[i] - d[i] * t;
    sum += e * e;
  }
  return sqrtf(sum);
}

// The least distance between the segment p0 to p1 and the segment q0 to q1.
// The closest points of two segments, after Ericson, "Real-Time Collision
// Detection", 5.1.9.
inline float SegmentSegmentDistance(const float p0[3], const float p1[3],
                                    const float q0[3], const float q1[3]) {
  const float d1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
  const float d2[3] = {q1[0] - q0[0], q1[1] - q0[1], q1[2] - q0[2]};
  const float r[3] = {p0[0] - q0[0], p0[1] - q0[1], p0[2] - q0[2]};
  const float a = d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2];
  const float e = d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2];
  const float f = d2[0] * r[0] + d2[1] * r[1] + d2[2] * r[2];
  const float kTiny = 1e-12f;
  const auto clamp01 = [](float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  };
  float s = 0.0f, t = 0.0f;
  if (a <= kTiny && e <= kTiny) {
    s = t = 0.0f;
  } else if (a <= kTiny) {
    t = clamp01(f / e);
  } else {
    const float c = d1[0] * r[0] + d1[1] * r[1] + d1[2] * r[2];
    if (e <= kTiny) {
      s = clamp01(-c / a);
    } else {
      const float b = d1[0] * d2[0] + d1[1] * d2[1] + d1[2] * d2[2];
      const float denom = a * e - b * b;
      s = denom > kTiny ? clamp01((b * f - c * e) / denom) : 0.0f;
      t = (b * s + f) / e;
      if (t < 0.0f) {
        t = 0.0f;
        s = clamp01(-c / a);
      } else if (t > 1.0f) {
        t = 1.0f;
        s = clamp01((b - c) / a);
      }
    }
  }
  float sum = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float g = (p0[i] + d1[i] * s) - (q0[i] + d2[i] * t);
    sum += g * g;
  }
  return sqrtf(sum);
}

// How far the point p is outside the capsule. Below zero it is inside.
inline float CapsuleGap(const Capsule& c, const float p[3]) {
  const float top[3] = {c.base[0], c.base[1] + c.height, c.base[2]};
  return SegmentPointDistance(c.base, top, p) - c.radius;
}

// The swept test: the Hand was outside the capsule at from, on the last
// frame, and its path to to meets the capsule. So a fast hand that goes
// through the body between two frames counts, and a hand that stays inside
// does not.
inline bool CapsuleEntered(const Capsule& c, const float from[3],
                           const float to[3]) {
  if (!(CapsuleGap(c, from) > 0.0f)) return false;
  const float top[3] = {c.base[0], c.base[1] + c.height, c.base[2]};
  return SegmentSegmentDistance(from, to, c.base, top) <= c.radius;
}

// THE SPEED OF A SLAP. The game reads the speed of the Hand from two points
// on the sphere around the eye at the Hand's distance, the current one and
// the last one, and it uses only their difference across the land (x and z).
// So the mod puts the last point across the line of sight, level, at the
// distance the real hand moves in one step of the game, times k units per
// meter per second. The game then reads k times the real speed.
//
// The across axis: level, at right angles to the line from the eye to the
// target, to the right as the eye sees it. The game's world is left handed
// with y up, so for a line f the right is (f.z, 0, -f.x). False when the line
// is almost vertical, and then out does not change.
inline bool SlapAcross(const float eye[3], const float target[3],
                       float out[3]) {
  const float fx = target[0] - eye[0];
  const float fz = target[2] - eye[2];
  const float flat = sqrtf(fx * fx + fz * fz);
  const float fy = target[1] - eye[1];
  const float span = sqrtf(flat * flat + fy * fy);
  if (!(span > 1e-6f) || !(flat > 0.02f * span)) return false;
  out[0] = fz / flat;
  out[1] = 0.0f;
  out[2] = -fx / flat;
  return true;
}

// k: the game's units per second for one real meter per second. A slap needs
// 5 * S units per second in the game, with S = 15 * size. So with this k a
// slap needs slap_mps of real speed, at any size of the Creature and at any
// scale of the world. Zero when a number is missing.
inline float SlapUnitsPerMps(float creature_size, float slap_mps) {
  if (!(creature_size > 0.0f) || !(slap_mps > 0.0f)) return 0.0f;
  return 5.0f * 15.0f * creature_size / slap_mps;
}

// The last point of the Hand as the game should read it: target moved back
// along the across axis by k * real_mps * dt. sign is 1 for a hand that
// moves to the right as the eye sees it, and -1 for one that moves to the
// left. A still hand gives the target itself.
inline void SlapPreviousPoint(const float target[3], const float across[3],
                              float sign, float k, float real_mps, float dt,
                              float out[3]) {
  const float step = k * real_mps * dt * (sign < 0.0f ? -1.0f : 1.0f);
  for (int i = 0; i < 3; ++i) out[i] = target[i] - across[i] * step;
}

// THE CREATURE PANEL IN THE ROOM (docs/ui-layer.md, "The Creature panel").
//
// A world point in the room, at the life size scale: the inverse of
// RoomToWorld with the table gain, which puts the room point t at
// eye + L * gain * (t - t_head). Off the table the gain is 1.
inline void WorldToRoom(const float eye[3], const float room[9],
                        const float t_head[3], float gain,
                        const float world[3], float out[3]) {
  const float d[3] = {world[0] - eye[0], world[1] - eye[1],
                      world[2] - eye[2]};
  float back[3];
  Apply(room, true, d, back);
  const float g = gain > 0.0f ? gain : 1.0f;
  for (int i = 0; i < 3; ++i) out[i] = t_head[i] + back[i] / g;
}

// A room point in the LOCAL space of OpenXR, in meters: the inverse of
// RoomTranslation in head_pose.cpp. origin and yaw are the recenter pose,
// and the room is left handed, so z changes sign.
inline void RoomToLocal(const float origin[3], const float yaw[4],
                        float units_per_meter, const float t[3],
                        float out[3]) {
  const float u = units_per_meter > 0.0f ? units_per_meter : 1.0f;
  const float local[3] = {t[0] / u, t[1] / u, -t[2] / u};
  float turned[3];
  QuatRotate(yaw, local, turned);
  for (int i = 0; i < 3; ++i) out[i] = origin[i] + turned[i];
}

// The turn of a quad at position whose front (+z) faces target, with no
// roll: first a tilt about x, then a turn about y. The identity when the two
// points are the same.
inline void FacePose(const float position[3], const float target[3],
                     float out[4]) {
  const float dx = target[0] - position[0];
  const float dy = target[1] - position[1];
  const float dz = target[2] - position[2];
  const float flat = sqrtf(dx * dx + dz * dz);
  if (!(flat > 1e-6f) && !(fabsf(dy) > 1e-6f)) {
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    return;
  }
  const float yaw = flat > 1e-6f ? atan2f(dx, dz) : 0.0f;
  const float pitch = -atan2f(dy, flat);
  const float turn[4] = {0.0f, sinf(yaw * 0.5f), 0.0f, cosf(yaw * 0.5f)};
  const float tilt[4] = {sinf(pitch * 0.5f), 0.0f, 0.0f, cosf(pitch * 0.5f)};
  QuatMultiply(turn, tilt, out);
}

// The center of the Creature panel, in LOCAL meters: up_m above the
// Creature's head and side_m to the left of it as the eyes see it. Then it
// moves along the line from the eyes until it is near_m to far_m from them.
// False when the head is right above or below the eyes.
inline bool CreaturePanelPoint(const float eyes[3], const float head[3],
                               float up_m, float side_m, float near_m,
                               float far_m, float out[3]) {
  const float fx = head[0] - eyes[0];
  const float fz = head[2] - eyes[2];
  const float flat = sqrtf(fx * fx + fz * fz);
  if (!(flat > 1e-4f)) return false;
  // LOCAL is right handed with y up: the right of a level line f is
  // (-f.z, 0, f.x).
  const float left[3] = {fz / flat, 0.0f, -fx / flat};
  float p[3];
  for (int i = 0; i < 3; ++i) p[i] = head[i] + left[i] * side_m;
  p[1] += up_m;
  const float d[3] = {p[0] - eyes[0], p[1] - eyes[1], p[2] - eyes[2]};
  const float span = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (!(span > 1e-4f)) return false;
  float want = span;
  if (want < near_m) want = near_m;
  if (want > far_m) want = far_m;
  for (int i = 0; i < 3; ++i) out[i] = eyes[i] + d[i] / span * want;
  return true;
}

// THE TELEPORT ARC (docs/god-view.md, "The teleport").
//
// The arc is a parabola in meters of the room, from the aim pose of the
// pointing hand. The launch speed is set so that a 45 degree aim over flat
// ground at the launch height lands at the reach R:
//
//   v0 = sqrt(g * R)        p(t) = o + v0 * d * t + 0.5 * (0, -g, 0) * t^2
//
// The caller puts the points in the world with the map of the grip, and then
// finds the land on that polyline, so the drawn tube and the hit agree.
const float kArcGravity = 9.8f;
const int kArcSteps = 48;          // equal time steps, so 49 points
const float kArcMaxSeconds = 3.0f;
const float kArcSkyDegrees = 60.0f;
const int kArcBisections = 8;

inline float ArcLaunchSpeed(float gravity, float reach) {
  return sqrtf(gravity * reach);
}

inline void ArcPoint(const float o[3], const float d[3], float speed,
                     float gravity, float t, float out[3]) {
  out[0] = o[0] + speed * d[0] * t;
  out[1] = o[1] + speed * d[1] * t - 0.5f * gravity * t * t;
  out[2] = o[2] + speed * d[2] * t;
}

// The time at which the arc falls to floor_y, at most max_seconds. 0 when the
// arc starts at or under floor_y.
inline float ArcEndTime(const float o[3], const float d[3], float speed,
                        float gravity, float floor_y, float max_seconds) {
  const float drop = o[1] - floor_y;
  if (!(drop > 0.0f) || !(gravity > 0.0f)) return 0.0f;
  const float vy = speed * d[1];
  const float t = (vy + sqrtf(vy * vy + 2.0f * gravity * drop)) / gravity;
  return t < max_seconds ? t : max_seconds;
}

// steps + 1 points at equal time steps from 0 to t_end. Returns the count.
inline int ArcPoints(const float o[3], const float d[3], float speed,
                     float gravity, float t_end, int steps, float (*out)[3]) {
  for (int i = 0; i <= steps; ++i) {
    ArcPoint(o, d, speed, gravity, t_end * static_cast<float>(i) /
                                       static_cast<float>(steps), out[i]);
  }
  return steps + 1;
}

// An aim more than 60 degrees up is the sky. d is a unit vector.
inline bool ArcIsSky(const float d[3]) {
  return d[1] > sinf(kArcSkyDegrees * 3.14159265f / 180.0f);
}

// The first land on the polyline. ground(x, z) is the land height. The hit is
// on the first segment whose end is at or under the land, found by
// kArcBisections halvings of that segment, and its height is the land height
// there. A start that is already under the land is a hit at the start. False
// when no point is under the land.
template <typename Ground>
inline bool ArcLandHit(const float (*p)[3], int count, Ground ground,
                       float hit[3], int* segment_out) {
  if (count < 1) return false;
  if (p[0][1] <= ground(p[0][0], p[0][2])) {
    hit[0] = p[0][0];
    hit[1] = ground(p[0][0], p[0][2]);
    hit[2] = p[0][2];
    if (segment_out != nullptr) *segment_out = 0;
    return true;
  }
  for (int i = 0; i + 1 < count; ++i) {
    const float* a = p[i];
    const float* b = p[i + 1];
    if (!(b[1] <= ground(b[0], b[2]))) continue;
    float low = 0.0f, high = 1.0f;
    for (int n = 0; n < kArcBisections; ++n) {
      const float m = 0.5f * (low + high);
      const float x = a[0] + (b[0] - a[0]) * m;
      const float y = a[1] + (b[1] - a[1]) * m;
      const float z = a[2] + (b[2] - a[2]) * m;
      if (y > ground(x, z)) {
        low = m;
      } else {
        high = m;
      }
    }
    hit[0] = a[0] + (b[0] - a[0]) * high;
    hit[2] = a[2] + (b[2] - a[2]) * high;
    hit[1] = ground(hit[0], hit[2]);
    if (segment_out != nullptr) *segment_out = i;
    return true;
  }
  return false;
}

// THE CAMERA TELEPORT of the immersive view: the move that puts the eye over
// the target at the same height above the land. The caller adds it to the
// game camera.
inline void CameraTeleportDelta(const float eye[3], float eye_ground,
                                const float target[3], float target_ground,
                                float out[3]) {
  out[0] = target[0] - eye[0];
  out[1] = target_ground - eye_ground;
  out[2] = target[2] - eye[2];
}

// THE HINT AT THE HAND (docs/ui-layer.md, "15. The tooltip at the Hand"). The game draws a
// tooltip beside the pointer, in pixels, at one place in both eyes after the
// flat copy. With the direct hand the Hand is the real hand, 40 to 80 cm from
// the eyes, so a flat tooltip is double when the player looks at the hand.
// draw_hooks.cpp moves it to the Hand in each eye and gives it a size in the
// room.
//
// The scale: the box of box_px pixels that the game asked for becomes box_cm
// of the room at the Hand's depth. focal is the pixels of the render for one
// unit of tan(angle) up from the middle (half_res_y * inv_half_tan_fov_y). 0
// when a value is not usable, and then the tooltip stays at the pointer.
inline float HintAtHandScale(float focal, float depth_m, float box_px,
                             float box_cm) {
  if (!(focal > 0.0f) || !(depth_m > 0.05f) || !(box_px > 1.0f) ||
      !(box_cm > 0.0f)) {
    return 0.0f;
  }
  const float s = focal * box_cm * 0.01f / depth_m / box_px;
  if (s < 0.1f) return 0.1f;
  if (s > 3.0f) return 3.0f;
  return s;
}

// One point of the tooltip in this eye. hand is the Hand's point in this eye,
// first the top left corner of the first batch of the tooltip in this eye,
// and offset that corner less the Hand's point, from the left eye. So the left
// eye scales the tooltip about the Hand's point, and the right eye puts it at
// the same place from its own Hand's point: both eyes give it the disparity of
// the Hand, and it shows at the Hand's depth.
inline void HintAtHandPoint(const float hand[2], const float first[2],
                            const float offset[2], float s, float p[2]) {
  for (int i = 0; i < 2; ++i) p[i] = hand[i] + s * (p[i] - first[i] + offset[i]);
}

// THE GRAB FROM A DISTANCE (docs/grab.md, "The grab from a distance").
//
// THE CONE. The pointing hand's aim ray picks an object when the angle from
// the ray to the object, less the angle that the object's own radius covers,
// is small. te = max(0, t - atan(r / d)), with t the angle off the ray, r the
// 2D radius of the object and d its distance from the ray's origin (both in
// world units). A point on the ray, or inside the radius, gives 0. A point at
// or behind the ray's origin is out. The score adds 1 degree for each
// log2(1 + the distance from the Hand's point, in meters of the room), so of
// two equal targets the nearer one wins.
struct FarScore {
  bool valid;   // in front of the ray, and not nearer than the minimum
  float te;     // degrees
  float score;  // degrees
};

const float kFarSelectDeg = 8.0f;    // a new target needs te at or below this
const float kFarKeepDeg = 14.0f;     // the target stays while te is at or below
const float kFarSwitchDeg = 2.0f;    // another target must beat it by this
const float kFarMinHandM = 0.35f;    // an object nearer to the Hand is "near"
const float kFarDistanceDeg = 1.0f;  // degrees for each log2(1 + meters)

inline FarScore FarConeScore(const float origin[3], const float direction[3],
                             const float point[3], float radius,
                             float hand_distance_m) {
  FarScore out = {false, 0.0f, 0.0f};
  if (!(hand_distance_m >= kFarMinHandM)) return out;
  const float to[3] = {point[0] - origin[0], point[1] - origin[1],
                       point[2] - origin[2]};
  const float d = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
  const float along = to[0] * direction[0] + to[1] * direction[1] +
                      to[2] * direction[2];
  if (!(d > 1e-4f) || !(along > 0.0f)) return out;
  float c = along / d;
  if (c > 1.0f) c = 1.0f;
  const float t = acosf(c) * 57.29578f;
  const float covered =
      atanf((radius > 0.0f ? radius : 0.0f) / d) * 57.29578f;
  out.te = t > covered ? t - covered : 0.0f;
  out.score = out.te + kFarDistanceDeg * log2f(1.0f + hand_distance_m);
  out.valid = true;
  return out;
}

// The target among scores, with the hysteresis. current is the index of the
// target of the last frame, or -1. A new target needs te <= select_deg. The
// target stays while its te is at or below keep_deg, unless another one has a
// score switch_deg lower. Returns the index, or -1 for none.
inline int FarPickTarget(const FarScore* scores, int count, int current,
                         float select_deg, float keep_deg,
                         float switch_deg) {
  int best = -1;
  for (int i = 0; i < count; ++i) {
    if (!scores[i].valid || !(scores[i].te <= select_deg)) continue;
    if (best < 0 || scores[i].score < scores[best].score) best = i;
  }
  const bool keep = current >= 0 && current < count && scores[current].valid &&
                    scores[current].te <= keep_deg;
  if (!keep) return best;
  if (best >= 0 && best != current &&
      scores[best].score + switch_deg <= scores[current].score) {
    return best;
  }
  return current;
}

// The distance from the Hand's point that the score takes, in meters of the
// room: an object nearer than kFarMinHandM counts as that far. The grab from
// a distance has its own least distance (near_math.h, FarLeastMeters), and a
// Creature command has none.
inline float FarScoreMeters(float hand_distance_m) {
  return hand_distance_m < kFarMinHandM ? kFarMinHandM : hand_distance_m;
}

// THE SEA DOES NOT HIDE. One point of the line from the eye to an object:
// land is the height of the land under it, line_y the height of the line and
// slack the height that a hill must have over the line. The game gives 0 for
// the sea, and the sea is a plane at y 0 that the player sees through. So
// only land over 0 hides an object. An object that floats can have its point
// under that plane.
inline bool FarLandHides(float land, float line_y, float slack) {
  return land == land && land > 0.0f && land > line_y + slack;
}

// THE RANGE (far_grab_max_m). A far target must be near the player: at most
// max_m meters of the room from the head, along the floor. The height does
// not count, so an object in a valley is as near as one on a hill. head and
// point are in the world, where y is up, and units_per_meter is the world
// units in a meter of the room.
inline float FarFloorMeters(const float head[3], const float point[3],
                            float units_per_meter) {
  if (!(units_per_meter > 0.0f)) return 1e30f;
  const float dx = point[0] - head[0], dz = point[2] - head[2];
  return sqrtf(dx * dx + dz * dz) / units_per_meter;
}

// The target of the last frame stays to kFarRangeKeepM more, so a head that
// moves a little does not turn the target off and on. A max_m of 0 or less
// gives no limit.
const float kFarRangeKeepM = 0.15f;
inline bool FarInRange(float floor_m, float max_m, bool current) {
  if (!(max_m > 0.0f)) return true;
  return floor_m <= max_m + (current ? kFarRangeKeepM : 0.0f);
}

// THE PULL. p is the unit direction from the grip at the lock to the head.
// v is the velocity of the grip in m/s (the mean of the last 60 ms), and
// moved is the move of the grip along p since the lock, in meters. The pull
// counts at a speed along p of 0.8 m/s or more with an angle of 55 degrees or
// less between v and p, or a move of 12 cm along p, or a flick upward:
// v.y >= 1.0 m/s with a speed along p of 0.4 m/s or more.
const float kPullSpeedMps = 0.8f;
const float kPullCosAngle = 0.5735764f;  // cos 55 degrees
const float kPullMoveM = 0.12f;
const float kFlickUpMps = 1.0f;
const float kFlickAlongMps = 0.4f;

inline bool PullDetect(const float v[3], const float p[3], float moved,
                       float* along_out) {
  const float along = v[0] * p[0] + v[1] * p[1] + v[2] * p[2];
  const float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (along_out != nullptr) *along_out = along;
  if (moved >= kPullMoveM) return true;
  if (along >= kPullSpeedMps && along >= kPullCosAngle * speed) return true;
  if (v[1] >= kFlickUpMps && along >= kFlickAlongMps) return true;
  return false;
}

// THE GLIDE. The held object is drawn from where it was (p0) to the Hand
// (p1) in glide seconds. e(u) = 1 - (1 - u)^3 eases out, and the object rises
// by 15 percent of the distance at the middle, along world up. p1 is the
// game's value of each frame, so the glide follows a moving Hand.
inline float GlideEase(float u) {
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  const float r = 1.0f - u;
  return 1.0f - r * r * r;
}

inline void GlideArc(const float p0[3], const float p1[3], float u,
                     float out[3]) {
  if (u < 0.0f) u = 0.0f;
  if (u > 1.0f) u = 1.0f;
  const float e = GlideEase(u);
  const float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
  const float h = 0.15f * sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  out[0] = p0[0] + d[0] * e;
  out[1] = p0[1] + d[1] * e + h * 4.0f * u * (1.0f - u);
  out[2] = p0[2] + d[2] * e;
}

// The time of the glide, from the distance in meters of the room.
inline float GlideSeconds(float distance_m) {
  float t = 0.30f + 0.05f * distance_m;
  if (t < 0.30f) t = 0.30f;
  if (t > 0.50f) t = 0.50f;
  return t;
}

// THE SQUARE ROOT LAW. The game's gravity is 9.81 units per second squared,
// and a meter of the room is s units. A throw with the velocity sqrt(s) v
// lands where the real throw lands, times s, from any release height:
//
//   R_g = sqrt(s) v cos(a) (sqrt(s) v sin(a) + sqrt(s v^2 sin^2(a) + 2 g s h))
//         / g = s R_room
//
// The direction is that of ThrowVelocity. The speed is the real speed of the
// grip in meters per second (v_grip_room is in units at the life size scale,
// life units per meter), times sqrt(s). The cap of the game holds. The drop
// rule is that of TableThrowVelocity: a hand that the life size scale, with
// the gain, would set down, sets the object down.
inline void SqrtThrowVelocity(const float v_reach_room[3],
                              const float v_grip_room[3], const float room[9],
                              float gain, float life, float s, float cap,
                              float drop_speed, float out[3],
                              float* speed_before_cut_out, bool* drop_out) {
  const float flat = sqrtf(v_grip_room[0] * v_grip_room[0] +
                           v_grip_room[2] * v_grip_room[2]) * gain;
  const float real = life > 0.0f ? Length(v_grip_room) / life : 0.0f;
  const float want = s > 0.0f ? sqrtf(s) * real : 0.0f;
  *drop_out = !(flat > drop_speed);
  if (*drop_out) {
    out[0] = out[1] = out[2] = 0.0f;
    *speed_before_cut_out = want;
    return;
  }
  // ThrowVelocity gives grip_speed * gain, so a gain of want / grip_speed
  // gives the speed wanted, with the cut and the direction.
  const float grip_speed = Length(v_grip_room);
  const float g = grip_speed > 1e-6f ? want / grip_speed : 0.0f;
  ThrowVelocity(v_reach_room, v_grip_room, room, g, cap, out,
                speed_before_cut_out);
}

}  // namespace controller_math
