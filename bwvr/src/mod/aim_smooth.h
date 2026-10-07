// The smoothing of the aim, and the depth of the menu. Pure functions with no
// game state. teleport.cpp and controllers.cpp call them, and test/
// controller_math checks them (polish_checks.h).
//
// THE ONE EURO FILTER (Casiez, Roussel and Vogel, 2012). A hand in the air
// shakes a little, 8 to 12 times a second. A fixed low pass filter removes the
// shake, but it also makes a real move late. This filter changes its cutoff
// with the speed. When the value is still, the cutoff is low, so the shake is
// gone. When the value moves fast, the cutoff is high, so the filter follows
// at once.
//
//   cutoff = min_cutoff + beta * speed
//   alpha  = 1 / (1 + tau / dt), with tau = 1 / (2 pi cutoff)
//   out    = out_before + alpha * (in - out_before)
//
// The speed is the filtered change of the value in one second. A vector has
// one speed (its length), so all three parts use one cutoff and the shape of
// the move stays.

#pragma once

#include <math.h>

namespace aim_smooth {

struct OneEuroVec3 {
  bool have;
  float value[3];  // the last output
  float slope[3];  // the filtered change in one second
};

// The values for the aim of the teleport. The direction is a unit vector, so
// its speed is in radians a second. The point is in meters of the room.
const float kDirectionMinCutoff = 0.9f;  // Hz, when the aim is still
const float kDirectionBeta = 6.0f;       // Hz for each radian a second
const float kPointMinCutoff = 1.5f;
const float kPointBeta = 8.0f;           // Hz for each meter a second
const float kSlopeCutoff = 1.0f;         // Hz, for the speed itself

inline float Alpha(float cutoff, float dt) {
  const float tau = 1.0f / (6.2831853f * cutoff);
  return 1.0f / (1.0f + tau / dt);
}

inline void Reset(OneEuroVec3* f) { f->have = false; }

// One step. dt is the time since the last step, in seconds. The first step
// after a Reset gives the input as it is.
inline void Step(OneEuroVec3* f, const float in[3], float dt, float min_cutoff,
                 float beta, float out[3]) {
  if (!(dt > 1e-4f)) dt = 1e-4f;
  if (dt > 0.25f) dt = 0.25f;
  if (!f->have) {
    f->have = true;
    for (int i = 0; i < 3; ++i) {
      f->value[i] = in[i];
      f->slope[i] = 0.0f;
      out[i] = in[i];
    }
    return;
  }
  const float a_slope = Alpha(kSlopeCutoff, dt);
  float speed2 = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float raw = (in[i] - f->value[i]) / dt;
    f->slope[i] += a_slope * (raw - f->slope[i]);
    speed2 += f->slope[i] * f->slope[i];
  }
  const float cutoff = min_cutoff + beta * sqrtf(speed2);
  const float a = Alpha(cutoff, dt);
  for (int i = 0; i < 3; ++i) {
    f->value[i] += a * (in[i] - f->value[i]);
    out[i] = f->value[i];
  }
}

// THE DEPTH OF THE MENU. The game draws the menu in screen space. Each eye
// gets the menu moved by shift pixels, to the right in the left eye and to the
// left in the right eye (draw_hooks.cpp, UiDepthSetEye). The two images fuse
// at the distance z where the eyes, sep apart, see a point with the same
// disparity of 2 * shift pixels:
//
//   2 * shift = fx * hx * sep / z    so    z = fx * hx * sep / (2 * shift)
//
// z is in the units of sep, along the forward axis of the head. fx is
// inv_half_tan_fov_x and hx is half_res_x. 0 when the shift is 0 (the menu is
// at the screen, so it has no depth that the eyes can see).
inline float MenuDepth(float fx, float hx, float sep, float shift) {
  if (!(shift > 0.0f) || !(fx > 0.0f) || !(hx > 0.0f) || !(sep > 0.0f)) {
    return 0.0f;
  }
  return fx * hx * sep / (2.0f * shift);
}

// How far along a ray (a unit direction d, from the point o) a camera plane
// at the forward depth z is. cam_o and cam_d are the point and the direction
// in the camera (the forward axis is the third part). False when the ray does
// not go to the plane.
inline bool RayToDepth(const float cam_o[3], const float cam_d[3], float z,
                       float* along) {
  if (!(cam_d[2] > 0.05f)) return false;
  const float t = (z - cam_o[2]) / cam_d[2];
  if (!(t > 0.0f)) return false;
  *along = t;
  return true;
}

}  // namespace aim_smooth
