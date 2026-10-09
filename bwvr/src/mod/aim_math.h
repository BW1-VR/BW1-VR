// The aim of a controller, as pure functions with no game state.
//
// controllers.cpp calls these with the poses of the runtime, and
// test/controller_math calls them with poses it makes up. See
// docs/controllers.md, "8. The aim ray".
//
// A pose is in the frame OpenXR uses: x right, y up and -z forward. A
// quaternion is x y z w.

#pragma once

#include <math.h>
#include <string.h>

#include "controller_math.h"

namespace aim_math {

const float kRadians = 3.14159265f / 180.0f;

// THE PRESETS of the aim ray of a Touch controller. Each is a turn of the aim
// pose of the runtime and a move of the aim point, in the frame of that pose.
// The Oculus runtime, SteamVR and Virtual Desktop give the same aim pose for
// a Touch controller: 60 degrees down from the grip pose, 9.96 cm from the
// grip point.
//
// kPresetRuntime: the aim pose of the runtime, with no change.
//
// kPresetMeta: the pointer of Meta's Interaction SDK (OVRPointerPoseSelector
// in FromOVRControllerDataSource.cs). It turns the controller pose by
// Quaternion.Euler(359.209534, 353.548035, 353.044556) for the right hand,
// and it mirrors the turn for the left hand. That is 6.45 degrees toward the
// other hand and 0.79 degrees up. The roll of that turn is about the ray, so
// it does not move the ray. Its point is 0.8 cm to the outside, 0.4 cm lower
// and 1.8 cm behind the aim point (the values for each headset but the
// Quest 2).
//
// kPresetAlyx: the menu pointer of Half-Life: Alyx on a Touch controller. The
// game turns the raw pose of SteamVR 45 degrees down and 10 degrees toward
// the other hand (hlvr_client_ui_hand_pointer_rot_x and _rot_y), and the aim
// pose of SteamVR is that raw pose turned 39.4 degrees down. So the pointer
// is 7.09 degrees toward the other hand and 5.99 degrees down from the aim
// pose. Its point is 1.0 cm to the outside, 1.0 cm lower and 0.2 cm behind
// the aim point (hlvr_client_ui_hand_pointer_pos_z -2.5 and _forward_offset
// -1, in inches). This is the default: the ray of the mod was too high and to
// the right of the ray of the Quest menu.
enum Preset { kPresetRuntime = 0, kPresetMeta = 1, kPresetAlyx = 2 };
const int kPresetCount = 3;

struct PresetPose {
  const char* name;
  float yaw_in_deg;     // toward the other hand
  float pitch_up_deg;
  float out_m;          // the aim point moves away from the other hand,
  float up_m;           // up,
  float back_m;         // and back, in the frame of the aim pose
};

const float kTouchYawInDeg = 6.451965f;
const float kTouchPitchUpDeg = 0.790466f;
const float kAlyxYawInDeg = 7.092f;
const float kAlyxPitchUpDeg = -5.993f;

const PresetPose kPresets[kPresetCount] = {
    {"the ray of the runtime", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {"the Meta pointer", kTouchYawInDeg, kTouchPitchUpDeg, 0.0078f, -0.0041f,
     0.0175f},
    {"the Half-Life: Alyx pointer", kAlyxYawInDeg, kAlyxPitchUpDeg, 0.0101f,
     -0.0103f, 0.0021f},
};

// A preset number from the ini, inside the range.
inline int PresetOf(int value) {
  return value < 0 ? 0 : (value >= kPresetCount ? kPresetCount - 1 : value);
}

// The preset after this one, for the test key.
inline int NextPreset(int preset) { return (PresetOf(preset) + 1) % kPresetCount; }

// The largest trim, each way.
const float kTrimLimitDeg = 30.0f;

// The turn of the aim, in the frame of the aim pose: yaw_in degrees about y
// toward the other hand, then pitch_up degrees about the turned x. A right
// hand turns to the left, and a left hand turns to the right.
inline void TurnQuat(float yaw_in_deg, float pitch_up_deg, bool left,
                     float out[4]) {
  // A positive turn about +y takes -z to -x, which is to the left.
  const float yaw = (left ? -yaw_in_deg : yaw_in_deg) * kRadians * 0.5f;
  const float pitch = pitch_up_deg * kRadians * 0.5f;
  const float about_y[4] = {0.0f, sinf(yaw), 0.0f, cosf(yaw)};
  const float about_x[4] = {sinf(pitch), 0.0f, 0.0f, cosf(pitch)};
  controller_math::QuatMultiply(about_y, about_x, out);
}

// The aim orientation with the turn: orientation * turn. The turn is in the
// frame of the controller, so it goes with a controller that rolls.
inline void Turned(const float orientation[4], float yaw_in_deg,
                   float pitch_up_deg, bool left, float out[4]) {
  float turn[4];
  TurnQuat(yaw_in_deg, pitch_up_deg, left, turn);
  controller_math::QuatMultiply(orientation, turn, out);
}

// The direction of an aim orientation: -z turned by it.
inline void Direction(const float orientation[4], float out[3]) {
  const float ahead[3] = {0.0f, 0.0f, -1.0f};
  controller_math::QuatRotate(orientation, ahead, out);
}

// Whether a profile is the Touch controller of a Quest. The presets are for
// that controller only.
inline bool TouchProfile(const char* profile) {
  return profile != nullptr &&
         strstr(profile, "oculus/touch_controller") != nullptr;
}

// The turn in use: the preset when the controller is a Touch, plus the trim.
struct Turn {
  float yaw_in_deg;
  float pitch_up_deg;
};

inline Turn TotalTurn(int preset, bool touch, float trim_yaw_in_deg,
                      float trim_pitch_up_deg) {
  Turn t = {trim_yaw_in_deg, trim_pitch_up_deg};
  if (touch) {
    const PresetPose& pose = kPresets[PresetOf(preset)];
    t.yaw_in_deg += pose.yaw_in_deg;
    t.pitch_up_deg += pose.pitch_up_deg;
  }
  return t;
}

// The aim point in use: the aim point of the runtime, moved by the preset in
// the frame of the aim pose of the runtime. The left hand has the mirror
// image. A controller that is not a Touch keeps its aim point.
inline void PresetPoint(int preset, bool touch, bool left,
                        const float position[3], const float orientation[4],
                        float out[3]) {
  const PresetPose& pose = kPresets[touch ? PresetOf(preset) : 0];
  const float local[3] = {left ? -pose.out_m : pose.out_m, pose.up_m,
                          pose.back_m};
  float moved[3];
  controller_math::QuatRotate(orientation, local, moved);
  for (int i = 0; i < 3; ++i) out[i] = position[i] + moved[i];
}

// One step of a trim, kept inside the limit.
inline float TrimStep(float value, float step) {
  float next = value + step;
  if (next > kTrimLimitDeg) next = kTrimLimitDeg;
  if (next < -kTrimLimitDeg) next = -kTrimLimitDeg;
  return next;
}

// THE AIM POSE AS THE RUNTIME GIVES IT, seen from the grip pose. The angles
// are of the aim direction in the frame of the grip. The roll is about the
// aim direction, and a positive roll leans the top of the aim pose to the
// right. The offset is the aim point in the frame of the grip, in meters.
struct GripToAim {
  float pitch_up_deg;
  float yaw_left_deg;
  float roll_deg;
  float offset_m[3];
};

inline void GripToAimOf(const float grip_position[3],
                        const float grip_orientation[4],
                        const float aim_position[3],
                        const float aim_orientation[4], GripToAim* out) {
  const float back[4] = {-grip_orientation[0], -grip_orientation[1],
                         -grip_orientation[2], grip_orientation[3]};
  float relative[4];
  controller_math::QuatMultiply(back, aim_orientation, relative);
  const float delta[3] = {aim_position[0] - grip_position[0],
                          aim_position[1] - grip_position[1],
                          aim_position[2] - grip_position[2]};
  controller_math::QuatRotate(back, delta, out->offset_m);

  float ahead[3];
  Direction(relative, ahead);
  float up = ahead[1];
  if (up > 1.0f) up = 1.0f;
  if (up < -1.0f) up = -1.0f;
  out->pitch_up_deg = asinf(up) / kRadians;
  out->yaw_left_deg = atan2f(-ahead[0], -ahead[2]) / kRadians;
  // The roll: the top of the aim pose against the top and the right side of
  // the same yaw and pitch with no roll.
  float level[4];
  TurnQuat(out->yaw_left_deg, out->pitch_up_deg, false, level);
  const float y[3] = {0.0f, 1.0f, 0.0f}, x[3] = {1.0f, 0.0f, 0.0f};
  float top[3], level_top[3], level_right[3];
  controller_math::QuatRotate(relative, y, top);
  controller_math::QuatRotate(level, y, level_top);
  controller_math::QuatRotate(level, x, level_right);
  const float to_right = top[0] * level_right[0] + top[1] * level_right[1] +
                         top[2] * level_right[2];
  const float to_top = top[0] * level_top[0] + top[1] * level_top[1] +
                       top[2] * level_top[2];
  out->roll_deg = atan2f(to_right, to_top) / kRadians;
}

// The difference of two angles in degrees, from 0 to 180.
inline float AngleApart(float a, float b) {
  float d = fabsf(a - b);
  while (d > 360.0f) d -= 360.0f;
  return d > 180.0f ? 360.0f - d : d;
}

// Whether the runtime moved the aim pose against the grip pose: an angle
// changed by more than degrees, or the offset by more than meters.
inline bool GripToAimMoved(const GripToAim& a, const GripToAim& b,
                           float degrees, float meters) {
  if (AngleApart(a.pitch_up_deg, b.pitch_up_deg) > degrees ||
      AngleApart(a.yaw_left_deg, b.yaw_left_deg) > degrees ||
      AngleApart(a.roll_deg, b.roll_deg) > degrees) {
    return true;
  }
  return controller_math::Distance(a.offset_m, b.offset_m) > meters;
}

}  // namespace aim_math
