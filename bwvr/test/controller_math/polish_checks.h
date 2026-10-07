// THE SMOOTH AIM and THE DEPTH OF THE MENU (aim_smooth.h). main.cpp includes
// this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/aim_smooth.h"

namespace polish_checks {

// A unit direction at a yaw to the right and a pitch up, in degrees.
inline void Aim(double yaw_deg, double pitch_deg, float out[3]) {
  const double y = yaw_deg / 57.29578, p = pitch_deg / 57.29578;
  out[0] = static_cast<float>(sin(y) * cos(p));
  out[1] = static_cast<float>(sin(p));
  out[2] = static_cast<float>(cos(y) * cos(p));
}

inline double AngleBetween(const float a[3], const float b[3]) {
  double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const double la = sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  const double lb = sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
  dot /= la * lb;
  if (dot > 1.0) dot = 1.0;
  return acos(dot) * 57.29578;
}

inline void Unit(float v[3]) {
  const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  for (int i = 0; i < 3; ++i) v[i] /= l;
}

// One step of the filter on a direction, as teleport.cpp does it.
inline void StepDirection(aim_smooth::OneEuroVec3* f, const float in[3],
                          float dt, float out[3]) {
  aim_smooth::Step(f, in, dt, aim_smooth::kDirectionMinCutoff,
                   aim_smooth::kDirectionBeta, out);
  Unit(out);
}

inline void PolishChecks() {
  printf("\nThe smooth aim and the depth of the menu\n");
  const float dt = 1.0f / 90.0f;

  // A STILL HAND WITH TREMOR. 0.4 degrees of shake at 10 Hz and a second shake
  // at 7 Hz on both axes, around 12 degrees right.
  {
    aim_smooth::OneEuroVec3 f;
    aim_smooth::Reset(&f);
    float truth[3];
    Aim(12.0, -20.0, truth);
    double raw_most = 0.0, smooth_most = 0.0;
    for (int i = 0; i < 270; ++i) {
      const double t = i * dt;
      const double yaw = 12.0 + 0.4 * sin(6.2831853 * 10.0 * t) +
                         0.25 * sin(6.2831853 * 7.0 * t + 1.0);
      const double pitch = -20.0 + 0.4 * cos(6.2831853 * 9.0 * t) +
                           0.25 * sin(6.2831853 * 13.0 * t);
      float in[3], out[3];
      Aim(yaw, pitch, in);
      StepDirection(&f, in, dt, out);
      if (i >= 45) {
        const double raw = AngleBetween(in, truth);
        const double smooth = AngleBetween(out, truth);
        if (raw > raw_most) raw_most = raw;
        if (smooth > smooth_most) smooth_most = smooth;
      }
    }
    Check(raw_most > 0.5, "the tremor is at least 0.5 degrees of the raw aim",
          raw_most, 0.5);
    Check(smooth_most < 0.12,
          "a still hand with tremor stays within 0.12 degrees after smoothing",
          smooth_most, 0.12);
    Check(smooth_most < raw_most * 0.25,
          "the smooth aim shakes at most a quarter of the raw aim",
          smooth_most, raw_most * 0.25);
  }

  // A STEP OF THE AIM. From still, the aim turns 20 degrees in one frame and
  // stays. The filter follows within a few frames.
  {
    aim_smooth::OneEuroVec3 f;
    aim_smooth::Reset(&f);
    float a[3], b[3], out[3];
    Aim(0.0, 0.0, a);
    Aim(20.0, 0.0, b);
    for (int i = 0; i < 90; ++i) StepDirection(&f, a, dt, out);
    double gap[8] = {0};
    for (int i = 0; i < 8; ++i) {
      StepDirection(&f, b, dt, out);
      gap[i] = AngleBetween(out, b);
    }
    Check(gap[0] < 19.0, "the first frame of a step moves toward the new aim",
          gap[0], 19.0);
    Check(gap[4] < 5.0, "5 frames after a step of 20 degrees, within 5 degrees",
          gap[4], 5.0);
    Check(gap[7] < 1.5, "8 frames after a step of 20 degrees, within 1.5",
          gap[7], 1.5);
  }

  // A SWEEP. The aim turns at 60 degrees a second for a second. The smooth
  // aim must not be late by more than 1.5 degrees.
  {
    aim_smooth::OneEuroVec3 f;
    aim_smooth::Reset(&f);
    double late_most = 0.0;
    for (int i = 0; i < 135; ++i) {
      const double yaw = 60.0 * i * dt;
      float in[3], out[3];
      Aim(yaw, 0.0, in);
      StepDirection(&f, in, dt, out);
      if (i >= 30) {
        const double late = AngleBetween(in, out);
        if (late > late_most) late_most = late;
      }
    }
    Check(late_most < 1.5, "a sweep at 60 degrees a second is late by < 1.5",
          late_most, 1.5);
  }

  // A SLOW DRIFT of 4 degrees a second is still a move. The filter does not
  // hold it back by more than half a degree.
  {
    aim_smooth::OneEuroVec3 f;
    aim_smooth::Reset(&f);
    double late_most = 0.0;
    for (int i = 0; i < 180; ++i) {
      const double yaw = 4.0 * i * dt;
      float in[3], out[3];
      Aim(yaw, 0.0, in);
      StepDirection(&f, in, dt, out);
      if (i >= 45) {
        const double late = AngleBetween(in, out);
        if (late > late_most) late_most = late;
      }
    }
    Check(late_most < 0.5, "a slow drift of 4 degrees a second is late < 0.5",
          late_most, 0.5);
  }

  // THE FIRST FRAME gives the input as it is, and a Reset starts again.
  {
    aim_smooth::OneEuroVec3 f;
    aim_smooth::Reset(&f);
    const float in[3] = {1.0f, 2.0f, 3.0f};
    float out[3];
    aim_smooth::Step(&f, in, dt, 1.0f, 1.0f, out);
    Near("the first output is the first input", out[0] + out[1] + out[2], 6.0,
         1e-6);
    const float far_in[3] = {10.0f, 0.0f, 0.0f};
    aim_smooth::Reset(&f);
    aim_smooth::Step(&f, far_in, dt, 1.0f, 1.0f, out);
    Near("after a Reset the output is the new input", out[0], 10.0, 1e-6);
  }

  // THE POINT of the hand, in meters: 2 mm of shake at 10 Hz is quiet.
  {
    aim_smooth::OneEuroVec3 f;
    aim_smooth::Reset(&f);
    double most = 0.0;
    for (int i = 0; i < 270; ++i) {
      const float in[3] = {
          0.3f + 0.002f * sinf(6.2831853f * 10.0f * i * dt), 1.0f, 0.4f};
      float out[3];
      aim_smooth::Step(&f, in, dt, aim_smooth::kPointMinCutoff,
                       aim_smooth::kPointBeta, out);
      if (i >= 45) {
        const double e = fabs(out[0] - 0.3);
        if (e > most) most = e;
      }
    }
    Check(most < 0.0007, "2 mm of shake of the aim point leaves < 0.7 mm",
          most * 1000.0, 0.7);
  }

  // THE DEPTH OF THE MENU. Eyes 0.064 m apart, render 2880 wide (hx 1440),
  // fx 1 (a 90 degree view), a shift of 8 pixels to each eye.
  {
    const float fx = 1.0f, hx = 1440.0f, sep = 0.064f, shift = 8.0f;
    const float z = aim_smooth::MenuDepth(fx, hx, sep, shift);
    Near("the menu depth is fx * hx * sep / (2 * shift)", z,
         1.0 * 1440 * 0.064 / 16.0, 1e-4);
    Near("a shift of 0 has no depth", aim_smooth::MenuDepth(fx, hx, sep, 0.0f),
         0.0, 1e-9);
    Check(aim_smooth::MenuDepth(fx, hx, sep, 16.0f) < z * 0.51,
          "twice the shift is half the depth",
          aim_smooth::MenuDepth(fx, hx, sep, 16.0f), z * 0.5);

    // The cursor, as the eyes see it: a point of the ray at the menu depth.
    // The hand is 0.25 m right, 0.30 m down and 0.10 m ahead of the eye
    // (identity head and room). The ray points up and to the left.
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float origin[3] = {0.25f, -0.30f, 0.10f};
    float dir[3] = {-0.20f, 0.25f, 1.0f};
    Unit(dir);
    float along = 0.0f;
    const bool ok = aim_smooth::RayToDepth(origin, dir, z, &along);
    Check(ok, "a ray that goes forward meets the menu plane", ok, 1);
    const controller_math::Screen screen = {hx, hx, fx, fx};
    float px = 0, py = 0;
    bool off = false;
    controller_math::RenderPixel(eye, ident, ident, origin, dir, along,
                                 screen, &px, &py, &off);
    // The point that the ray meets, seen from each eye.
    float p3[3];
    for (int i = 0; i < 3; ++i) p3[i] = origin[i] + dir[i] * along;
    Near("the meeting point is at the menu depth", p3[2], z, 1e-3);
    float lx = 0, ly = 0, rx = 0, ry = 0;
    const float left_eye[3] = {-sep / 2, 0.0f, 0.0f};
    const float right_eye[3] = {sep / 2, 0.0f, 0.0f};
    const float zero[3] = {0, 0, 0};
    controller_math::RenderPixel(left_eye, ident, ident, p3, zero, 0.0f,
                                 screen, &lx, &ly, &off);
    controller_math::RenderPixel(right_eye, ident, ident, p3, zero, 0.0f,
                                 screen, &rx, &ry, &off);
    Near("the left eye sees the meeting point shift pixels right of the pixel",
         lx - px, shift, 0.02);
    Near("the right eye sees it shift pixels left of the pixel", rx - px,
         -shift, 0.02);
    Near("both eyes see it at the row of the pixel", ly - py, 0.0, 1e-3);
    Near("the right eye too", ry - py, 0.0, 1e-3);

    // With the old distance (a far focus) the pixel and the drawn end differ.
    float far_x = 0, far_y = 0;
    controller_math::RenderPixel(eye, ident, ident, origin, dir, 40.0f, screen,
                                 &far_x, &far_y, &off);
    Check(fabsf(far_x - px) > 20.0f || fabsf(far_y - py) > 20.0f,
          "a far focus distance gives a pixel more than 20 px from the cursor",
          fabsf(far_x - px) + fabsf(far_y - py), 20.0);

    const float back[3] = {0.0f, 0.0f, -1.0f};
    Check(!aim_smooth::RayToDepth(origin, back, z, &along),
          "a ray that goes back never meets the menu", 0, 0);
  }
}

}  // namespace polish_checks
