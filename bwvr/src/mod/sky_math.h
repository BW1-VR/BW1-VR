// THE SKY WITH NO HEAD IN IT (docs/backend.md, "Sky, clouds and detail").
// Pure math: no Windows, no game. The checks are in
// test/controller_math/sky_checks.h.
//
// THE CAMERA MATRIX OF THE ENGINE. LH3DTech::UpdateCamera writes the camera
// that it gets to 0x00EA1D28 as an LHMatrix of 12 floats. A world point w
// goes to camera space as
//
//     c[j] = w.x * m[j] + w.y * m[3 + j] + w.z * m[6 + j] + m[9 + j]
//
// So (m[0], m[3], m[6]) is the camera right in the world, (m[1], m[4], m[7])
// the camera up and (m[2], m[5], m[8]) the camera forward. The mod puts the
// head on that camera, with its roll.
//
// THE MOON. fn_0086AC60 turns the moon to the camera: it takes the direction
// d from the camera to the moon in camera space, and the right of the moon is
// the camera up crossed with d. So the moon turns about d with each turn of
// the head. LevelCamera gives a camera at the same place with world up. The
// moon then turns only with its own direction from the eye.
//
// THE CLOUDS. LH3DMist::Draw scales the rows 1 and 2 of the basis of a sky
// cloud by A = scale / ((flatten - 1) * (1 - |dy| / D) + 1), with D and dy
// from the stored camera to the cloud. MistScale gives A.

#pragma once

#include <math.h>

namespace sky_math {

inline float Dot3(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline void Cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

// Axis j of the camera in the world: 0 right, 1 up, 2 forward.
inline void CameraAxis(const float m[12], int j, float out[3]) {
  out[0] = m[j];
  out[1] = m[3 + j];
  out[2] = m[6 + j];
}

// The place of the camera in the world: the point that goes to camera space
// as (0, 0, 0). Only for a matrix that is a true rotation.
inline void CameraPlace(const float m[12], float out[3]) {
  for (int k = 0; k < 3; ++k) {
    out[k] = -(m[9] * m[3 * k] + m[10] * m[3 * k + 1] + m[11] * m[3 * k + 2]);
  }
}

// True when the three axes have a length of 1 and are square to each other.
inline bool IsRotation(const float m[12]) {
  float axis[3][3];
  for (int j = 0; j < 3; ++j) CameraAxis(m, j, axis[j]);
  const float tolerance = 0.01f;
  for (int j = 0; j < 3; ++j) {
    if (fabsf(Dot3(axis[j], axis[j]) - 1.0f) > tolerance) return false;
  }
  return fabsf(Dot3(axis[0], axis[1])) < tolerance &&
         fabsf(Dot3(axis[0], axis[2])) < tolerance &&
         fabsf(Dot3(axis[1], axis[2])) < tolerance;
}

// A camera at the place of m, with world up and the heading of m. Looking
// straight down or up, the heading comes from the camera up. The right has
// the handedness of m. False when m is not a true rotation: then out is not
// written.
inline bool LevelCamera(const float m[12], float out[12]) {
  if (!IsRotation(m)) return false;
  float right[3], up[3], forward[3];
  CameraAxis(m, 0, right);
  CameraAxis(m, 1, up);
  CameraAxis(m, 2, forward);
  float level[3] = {forward[0], 0.0f, forward[2]};
  float length = sqrtf(level[0] * level[0] + level[2] * level[2]);
  if (length < 1e-3f) {
    level[0] = up[0];
    level[2] = up[2];
    length = sqrtf(level[0] * level[0] + level[2] * level[2]);
  }
  if (length < 1e-3f) {
    level[0] = 0.0f;
    level[2] = 1.0f;
    length = 1.0f;
  }
  level[0] /= length;
  level[2] /= length;
  // Right is up crossed with forward when m is right handed in that sense.
  float up_x_forward[3];
  Cross3(up, forward, up_x_forward);
  const float hand = Dot3(right, up_x_forward) < 0.0f ? -1.0f : 1.0f;
  const float level_right[3] = {hand * level[2], 0.0f, -hand * level[0]};
  const float world_up[3] = {0.0f, 1.0f, 0.0f};
  float place[3];
  CameraPlace(m, place);
  for (int k = 0; k < 3; ++k) {
    out[3 * k + 0] = level_right[k];
    out[3 * k + 1] = world_up[k];
    out[3 * k + 2] = level[k];
  }
  for (int j = 0; j < 3; ++j) {
    out[9 + j] = -(place[0] * out[j] + place[1] * out[3 + j] +
                   place[2] * out[6 + j]);
  }
  return true;
}

// The turn that fn_0086AC60 gives the moon at `point`, before its scale, in
// the world: the rows are the right, the up and the direction from the
// camera. For the checks.
inline void MoonTurn(const float m[12], const float point[3], float out[9]) {
  float c[3];
  for (int j = 0; j < 3; ++j) {
    c[j] = point[0] * m[j] + point[1] * m[3 + j] + point[2] * m[6 + j] +
           m[9 + j];
  }
  const float cl = sqrtf(Dot3(c, c));
  float d[3] = {c[0], c[1], c[2]};
  if (cl > 0.0f) {
    for (int i = 0; i < 3; ++i) d[i] /= cl;
  }
  float r[3] = {d[2], 0.0f, -d[0]};
  const float rl = sqrtf(Dot3(r, r));
  if (rl > 0.0f) {
    for (int i = 0; i < 3; ++i) r[i] /= rl;
  }
  float u[3];
  Cross3(d, r, u);
  const float* rows[3] = {r, u, d};
  // Back to the world with the inverse of the rotation.
  for (int row = 0; row < 3; ++row) {
    for (int j = 0; j < 3; ++j) {
      out[3 * row + j] = rows[row][0] * m[3 * j] +
                         rows[row][1] * m[3 * j + 1] +
                         rows[row][2] * m[3 * j + 2];
    }
  }
}

// The scale that LH3DMist::Draw gives the rows 1 and 2 of the basis of a
// cloud seen from `eye` (0x007FA4B7 to 0x007FA539). A sky cloud is
// flattened when it is near the horizon of the eye. Zero when the engine
// would divide by about zero.
inline float MistScale(float scale, bool sky_cloud, float flatten,
                       const float cloud[3], const float eye[3]) {
  if (!sky_cloud) return scale;
  const float f[3] = {cloud[0] - eye[0], cloud[1] - eye[1], cloud[2] - eye[2]};
  const float distance = sqrtf(Dot3(f, f));
  if (!(distance > 1e-3f)) return 0.0f;
  const float divisor =
      (flatten - 1.0f) * (1.0f - fabsf(f[1]) / distance) + 1.0f;
  if (!(divisor > 1e-6f)) return 0.0f;
  return scale / divisor;
}

}  // namespace sky_math
