// The math of the Lionhead logo in stereo (docs/intro.md, "The motion" and
// "The flat text and the boxes"). It has no game and no headset in it.
// intro_vr.cpp uses it, and test/controller_math checks it.
#pragma once

#include <math.h>

namespace intro_motion {

// intro_motion
const int kStill = 0;  // the camera stays at the rest pose
const int kPath = 1;   // the camera goes along the path of the game, no turn
const int kModel = 2;  // the orbs are a model in front of the player
const int kGame = 3;   // the camera of the game, with its turns
const int kModes = 4;

inline int ClampMode(int mode) {
  return mode < 0 || mode >= kModes ? kPath : mode;
}

// The value after a press of the test key: 1, 2, 3, 0, and 1 again.
inline int NextMode(int mode) { return (ClampMode(mode) + 1) % kModes; }

// THE CAMERA OF THE GAME (RunAlexFX, 0x005F90E7 to 0x005F933A). The effect
// has a clock that starts at 0 and gets 1.1 for each second. The logo is at
// the origin, on the plane z = 0.
//
//   clock 0 to 15    The camera goes around the origin, between the orbs. Its
//                    distance from the origin goes from 25 units to 10.
//   clock 15 to 26   The camera leaves the orbit and goes to the rest pose.
//                    The game eases the start and the end of this move.
//   clock 26 to 30   The rest pose: 100 units in front of the logo.
//   clock 30 to 36   The camera goes through the logo, 50 units for each unit
//                    of the clock. This move starts at once, with no ease.
//   clock 36         The effect sets its done flag.
//
// Until clock 30 the camera looks at the origin. Then it looks along -z.
const float kRest[3] = {0.0f, 0.0f, 100.0f};
const float kLogo[3] = {0.0f, 0.0f, 0.0f};
const float kRestDistance = 100.0f;
const float kOrbitEnd = 15.0f;
const float kRestStart = 26.0f;
const float kThroughStart = 30.0f;
const float kEndClock = 36.0f;

// fn_005F8960: 0 at 0, 1 at 1, and no speed at each end.
inline float Ease(float a) {
  if (a < 0.0f) return 0.0f;
  if (a >= 1.0f) return 1.0f;
  return (1.0f - cosf(a * 3.14159265f)) * 0.5f;
}

inline float Clamp01(float a) { return a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a; }

inline void GameCamera(float clock, float position[3], float focus[3]) {
  const float blend = Ease(Clamp01((clock - kOrbitEnd) / 11.0f));
  const float near_radius = Clamp01((kOrbitEnd - clock) / 15.0f) * 15.0f + 10.0f;
  const float radius = (kRestDistance - near_radius) * blend + near_radius;
  const float orbit[3] = {sinf(clock * 0.352532f + 2.0f) * radius,
                          cosf(3.0f - clock * 0.26793f) * radius,
                          sinf(clock * 0.15161f + 7.0f) * radius};
  float place[3];
  for (int i = 0; i < 3; ++i) {
    place[i] = orbit[i] + (kRest[i] - orbit[i]) * blend;
  }
  float through = 1.0f - (clock - kThroughStart) * 0.5f;
  if (through < -2.0f) through = -2.0f;
  if (through > 1.0f) through = 1.0f;
  for (int i = 0; i < 3; ++i) {
    position[i] = place[i] * through;
    focus[i] = kLogo[i] - place[i] * (1.0f - through);
  }
}

// The part of the path at a clock: 0 the orbit, 1 the move to the rest pose,
// 2 the rest pose, 3 the move through the logo.
inline int Part(float clock) {
  if (clock < kOrbitEnd) return 0;
  if (clock < kRestStart) return 1;
  if (clock < kThroughStart) return 2;
  return 3;
}

// THE FIXED HEADING. It is the direction of the rest pose: from the place of
// the camera at rest to the logo, level. A camera that has this heading for
// the whole intro has the logo in front of it at the rest pose.
inline void RestHeading(float heading[3]) {
  const float dx = kLogo[0] - kRest[0];
  const float dz = kLogo[2] - kRest[2];
  const float length = sqrtf(dx * dx + dz * dz);
  heading[0] = dx / length;
  heading[1] = 0.0f;
  heading[2] = dz / length;
}

// A camera at a place that looks along the fixed heading.
inline void LookAhead(const float place[3], float position[3],
                      float focus[3]) {
  float heading[3];
  RestHeading(heading);
  for (int i = 0; i < 3; ++i) {
    position[i] = place[i];
    focus[i] = place[i] + heading[i] * kRestDistance;
  }
}

// THE MODEL (intro_motion=2). The orbs are a model in front of the player.
// The camera of the model has the rest pose of the camera of the game as its
// rest place. From there it makes this part of each move of the camera of
// the game. In the orbit it is then 50 units from the plane of the logo at
// the least, and the nearest orbs are 30 units in front of that plane. So
// the head does not come between the orbs. The model passes the head only in
// the move through the logo, after clock 32.8, when the fade of the effect
// is 90 percent black.
const float kModelGain = 0.5f;

inline void ModelPlace(const float game_position[3], float place[3]) {
  for (int i = 0; i < 3; ++i) {
    place[i] = kRest[i] + (game_position[i] - kRest[i]) * kModelGain;
  }
}

// THE CAMERAS OF A FRAME. `game_position` and `game_focus` are what the game
// asks for. The room camera draws the sky. The orb camera draws the orbs,
// the flat text and the boxes. Only the model has two different cameras:
// its room stays still. Returns true when the two cameras are different.
inline bool Cameras(int mode, const float game_position[3],
                    const float game_focus[3], float room_position[3],
                    float room_focus[3], float orb_position[3],
                    float orb_focus[3]) {
  switch (ClampMode(mode)) {
    case kStill:
      LookAhead(kRest, room_position, room_focus);
      break;
    case kPath:
      LookAhead(game_position, room_position, room_focus);
      break;
    case kModel: {
      float place[3];
      ModelPlace(game_position, place);
      LookAhead(kRest, room_position, room_focus);
      LookAhead(place, orb_position, orb_focus);
      return true;
    }
    default:
      for (int i = 0; i < 3; ++i) {
        room_position[i] = game_position[i];
        room_focus[i] = game_focus[i];
      }
      break;
  }
  for (int i = 0; i < 3; ++i) {
    orb_position[i] = room_position[i];
    orb_focus[i] = room_focus[i];
  }
  return false;
}

// The game units for one meter, from the distance in meters between the head
// and the plane of the logo at the rest pose. 0 when the distance is not
// usable.
inline float UnitsPerMeter(float rest_distance_m) {
  return rest_distance_m > 0.1f ? kRestDistance / rest_distance_m : 0.0f;
}

// The angle in degrees between the direction of a camera and the logo. 0 is
// straight ahead, 180 is behind. -1 when the camera is at the logo.
inline float LogoAngleDeg(const float position[3], const float focus[3]) {
  float look[3], to_logo[3];
  float look_length = 0.0f, logo_length = 0.0f, dot = 0.0f;
  for (int i = 0; i < 3; ++i) {
    look[i] = focus[i] - position[i];
    to_logo[i] = kLogo[i] - position[i];
    look_length += look[i] * look[i];
    logo_length += to_logo[i] * to_logo[i];
    dot += look[i] * to_logo[i];
  }
  look_length = sqrtf(look_length);
  logo_length = sqrtf(logo_length);
  if (look_length < 1e-4f || logo_length < 1e-4f) return -1.0f;
  float c = dot / (look_length * logo_length);
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 57.29578f;
}

// The speed of the clock of the effect for a percent: the effect keeps its
// own speed factor between 0.1 and 1.
inline float ClockSpeed(int percent) {
  if (percent < 10) percent = 10;
  if (percent > 100) percent = 100;
  return static_cast<float>(percent) * 0.01f;
}

// THE FLAT TEXT AND THE BOXES. The effect draws them at a place of the
// picture, with no place in the scene. The place in the scene is the point
// of the plane of the logo that the rest camera shows at that pixel. The
// mod draws each corner of a flat batch where the camera of an eye sees that
// point, so the text has the depth of the logo and stays beside it.
struct View {
  float half_res_x;
  float half_res_y;
  float inv_half_tan_x;  // 1 / tan of half the view angle, across
  float inv_half_tan_y;  // and up and down
};

// The rest camera looks along -z. The engine builds its right axis from the
// world up and the forward axis, so right is -x and up is +y.
// LH3DTech::ProjectPoint gives x = (1 + cx / cz) * half_res_x and
// y = (1 - cy / cz) * half_res_y.
inline bool PlanePoint(const View& view, float x, float y, float point[3]) {
  if (!(view.half_res_x > 0.0f) || !(view.half_res_y > 0.0f) ||
      !(view.inv_half_tan_x > 0.0f) || !(view.inv_half_tan_y > 0.0f)) {
    return false;
  }
  const float right = (x / view.half_res_x - 1.0f) * kRestDistance /
                      view.inv_half_tan_x;
  const float up = (1.0f - y / view.half_res_y) * kRestDistance /
                   view.inv_half_tan_y;
  point[0] = kRest[0] - right;
  point[1] = kRest[1] + up;
  point[2] = kRest[2] - kRestDistance;
  return true;
}

// A point of the scene to a pixel, as LH3DTech::ProjectPoint does it with
// the matrix at 0x00EA9E40: 12 floats, three for each row, and the columns
// have the view terms in them. False when the point is not in front of the
// near plane.
inline bool Project(const float m[12], const View& view, const float point[3],
                    float near_plane, float* x, float* y) {
  const float cx = point[0] * m[0] + point[1] * m[3] + point[2] * m[6] + m[9];
  const float cy = point[0] * m[1] + point[1] * m[4] + point[2] * m[7] + m[10];
  const float cz = point[0] * m[2] + point[1] * m[5] + point[2] * m[8] + m[11];
  if (!(cz > near_plane) || !(cz > 1e-4f)) return false;
  *x = (1.0f + cx / cz) * view.half_res_x;
  *y = (1.0f - cy / cz) * view.half_res_y;
  return true;
}

// One corner of a flat batch: from its pixel in the picture of the rest
// camera to its pixel in the picture of the camera with the matrix `m`.
inline bool PlaceFlat(const float m[12], const View& view, float near_plane,
                      float* x, float* y) {
  float point[3];
  if (!PlanePoint(view, *x, *y, point)) return false;
  return Project(m, view, point, near_plane, x, y);
}

// The width of the logo on its plane, from the home of each orb. The effect
// puts each orb on a ray of the rest camera, so the place on the plane is
// the home times 100 over its distance from that camera. `homes` has three
// floats for each orb.
inline float LogoWidth(const float* homes, int count) {
  float low = 1e30f, high = -1e30f;
  for (int i = 0; i < count; ++i) {
    const float depth = kRest[2] - homes[i * 3 + 2];
    if (!(depth > 1.0f)) continue;
    const float on_plane = homes[i * 3] * kRestDistance / depth;
    if (on_plane < low) low = on_plane;
    if (on_plane > high) high = on_plane;
  }
  return high > low ? high - low : 0.0f;
}

// THE PUSH OF THE HAND (intro_hand_push_pct). The effect pushes the orbs
// near the pointer only when the mouse device moved: it reads the two motion
// sums of LHMouse. The controller gives the pointer a pixel. The motion of
// that pixel since the frame before, in render pixels and times the percent,
// goes into the two sums as whole counts. The fraction stays for the next
// frame, so a slow hand also pushes. A frame with no pixel gives no motion
// and forgets the pixel, so a pixel that comes back after a gap is not a
// motion of the hand.
struct HandPush {
  bool have;
  float last_x, last_y;
  float carry_x, carry_y;
};

inline void HandPushStep(HandPush* s, bool has_pixel, float x, float y,
                         int percent, int* dx, int* dy) {
  *dx = 0;
  *dy = 0;
  if (!has_pixel || percent <= 0) {
    s->have = false;
    s->carry_x = 0.0f;
    s->carry_y = 0.0f;
    return;
  }
  if (s->have) {
    const float gain = static_cast<float>(percent) * 0.01f;
    s->carry_x += (x - s->last_x) * gain;
    s->carry_y += (y - s->last_y) * gain;
    *dx = static_cast<int>(s->carry_x);
    *dy = static_cast<int>(s->carry_y);
    s->carry_x -= static_cast<float>(*dx);
    s->carry_y -= static_cast<float>(*dy);
  }
  s->have = true;
  s->last_x = x;
  s->last_y = y;
}

// THE PUSH IN 3D (intro_push_3d). Each hand takes the orbs near it with its
// own motion, one to one, in place and in turn. A hand pose is the grip point
// and the grip axes in the world: world = position + rotation * local, with
// the rotation stored row by row (as overlay_math.h, PlacePoint). An orb at
// `point` near the pose of the frame before keeps its place in the hand
// frame, so it goes where the hand takes that place now. The weight is 1 to
// half the radius, and falls to 0 at the radius, so the edge of the push is
// soft.
struct HandPose {
  float position[3];
  float rotation[9];
};

// The inverse of a 3 by 3 matrix, row by row. False when it has none.
inline bool Invert3(const float m[9], float out[9]) {
  const float c00 = m[4] * m[8] - m[5] * m[7];
  const float c01 = m[5] * m[6] - m[3] * m[8];
  const float c02 = m[3] * m[7] - m[4] * m[6];
  const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
  if (!(det > 1e-12f || det < -1e-12f)) return false;
  const float k = 1.0f / det;
  out[0] = c00 * k;
  out[1] = (m[2] * m[7] - m[1] * m[8]) * k;
  out[2] = (m[1] * m[5] - m[2] * m[4]) * k;
  out[3] = c01 * k;
  out[4] = (m[0] * m[8] - m[2] * m[6]) * k;
  out[5] = (m[2] * m[3] - m[0] * m[5]) * k;
  out[6] = c02 * k;
  out[7] = (m[1] * m[6] - m[0] * m[7]) * k;
  out[8] = (m[0] * m[4] - m[1] * m[3]) * k;
  return true;
}

inline float DragWeight(float distance, float radius) {
  if (!(radius > 0.0f) || !(distance < radius)) return 0.0f;
  const float inner = 0.5f * radius;
  if (distance <= inner) return 1.0f;
  return (radius - distance) / (radius - inner);
}

// The move of one orb by a hand from `before` to `now`. False when the orb is
// not in the reach of the hand of the frame before.
inline bool DragPoint(const HandPose& before, const HandPose& now,
                      float radius, const float point[3], float out[3]) {
  const float d[3] = {point[0] - before.position[0],
                      point[1] - before.position[1],
                      point[2] - before.position[2]};
  const float distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  const float weight = DragWeight(distance, radius);
  if (!(weight > 0.0f)) return false;
  float inverse[9];
  if (!Invert3(before.rotation, inverse)) return false;
  float local[3];
  for (int r = 0; r < 3; ++r) {
    local[r] = inverse[r * 3 + 0] * d[0] + inverse[r * 3 + 1] * d[1] +
               inverse[r * 3 + 2] * d[2];
  }
  for (int r = 0; r < 3; ++r) {
    const float there = now.position[r] + now.rotation[r * 3 + 0] * local[0] +
                        now.rotation[r * 3 + 1] * local[1] +
                        now.rotation[r * 3 + 2] * local[2];
    out[r] = (there - point[r]) * weight;
  }
  return true;
}

// THE SHOW OF THE HANDS (intro_hand_show=1). A hand shows while it moves,
// and it fades when it rests. The speed is the motion of the grip point in
// the room, smoothed over a short time, so the shake of the tracking of a
// hand at rest does not show it. The hand comes in a short time and goes in
// a longer time, after a short wait.
const float kShowMetersPerSecond = 0.12f;
const float kShowSmoothSeconds = 0.08f;
const float kShowWaitSeconds = 0.4f;
const float kShowInSeconds = 0.15f;
const float kShowOutSeconds = 0.5f;

struct HandShow {
  bool have;
  float last[3];   // the grip point in the room, meters
  float speed;     // smoothed, meters a second
  float still;     // seconds below the speed of a motion
  float opacity;   // 0 to 1
};

// One frame of a hand. `valid` is false when the hand is not tracked: then
// the hand fades and its speed starts again. Returns the opacity.
inline float HandShowStep(HandShow* s, bool valid, const float room_m[3],
                          float dt) {
  if (!(dt > 0.0f)) dt = 0.0f;
  if (dt > 0.1f) dt = 0.1f;
  bool moving = false;
  if (valid) {
    if (s->have && dt > 0.0f) {
      const float dx = room_m[0] - s->last[0];
      const float dy = room_m[1] - s->last[1];
      const float dz = room_m[2] - s->last[2];
      const float now = sqrtf(dx * dx + dy * dy + dz * dz) / dt;
      float share = dt / kShowSmoothSeconds;
      if (share > 1.0f) share = 1.0f;
      s->speed += (now - s->speed) * share;
    }
    s->have = true;
    for (int k = 0; k < 3; ++k) s->last[k] = room_m[k];
    moving = s->speed > kShowMetersPerSecond;
  } else {
    s->have = false;
    s->speed = 0.0f;
  }
  if (moving) {
    s->still = 0.0f;
    s->opacity += dt / kShowInSeconds;
  } else {
    s->still += dt;
    if (s->still > kShowWaitSeconds || !valid) {
      s->opacity -= dt / kShowOutSeconds;
    }
  }
  if (s->opacity < 0.0f) s->opacity = 0.0f;
  if (s->opacity > 1.0f) s->opacity = 1.0f;
  return s->opacity;
}

}  // namespace intro_motion
