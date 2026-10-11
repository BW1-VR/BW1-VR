// ONE WALK FOR BOTH EYES (docs/backend.md, "One walk for both eyes").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <math.h>
#include <stdint.h>

#include "../../src/mod/one_walk_math.h"

namespace one_walk_checks {

struct Projected {
  float x, y, z, rhw;
  bool visible;
};

// LH3DTech::ProjectPoint with the scaled matrix m (4 rows of 3, row vector
// convention), and the depth that the engine writes: rhw = near / cz,
// z = 1 - rhw.
inline Projected Project(const float m[12], const float p[3], float half_x,
                         float half_y, float near_plane) {
  const float cx = p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9];
  const float cy = p[0] * m[1] + p[1] * m[4] + p[2] * m[7] + m[10];
  const float cz = p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11];
  Projected out = {0.0f, 0.0f, 0.0f, 0.0f, cz >= near_plane};
  if (!out.visible) return out;
  out.x = (1.0f + cx / cz) * half_x;
  out.y = (1.0f - cy / cz) * half_y;
  out.rhw = near_plane / cz;
  out.z = 1.0f - out.rhw;
  return out;
}

// The eye camera of eye_camera.cpp, EyeCameraSetPose: R' = R * H and
// t' = t * H - d on the unscaled matrix, then the columns scaled by the field
// of view terms.
inline void EyeMatrix(const float unscaled[12], const float h[9], float d,
                      float scale_x, float scale_y, float out[12]) {
  float u[12];
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      u[row * 3 + col] = unscaled[row * 3 + 0] * h[0 * 3 + col] +
                         unscaled[row * 3 + 1] * h[1 * 3 + col] +
                         unscaled[row * 3 + 2] * h[2 * 3 + col];
    }
  }
  const float offset[3] = {d, 0.0f, 0.0f};
  for (int col = 0; col < 3; ++col) {
    u[9 + col] = unscaled[9] * h[0 * 3 + col] + unscaled[10] * h[1 * 3 + col] +
                 unscaled[11] * h[2 * 3 + col] - offset[col];
  }
  const float factor[3] = {scale_x, scale_y, 1.0f};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 3; ++col) {
      out[row * 3 + col] = u[row * 3 + col] * factor[col];
    }
  }
}

inline void OneWalkChecks() {
  using namespace one_walk_math;
  printf("\nOne walk for both eyes\n");

  // A camera that looks down at the land at 35 degrees, turned 30 degrees,
  // at a place far from the origin. Rigid, so the columns are the axes.
  const float pitch = 0.6109f, yaw = 0.5236f;
  const float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw),
              sy = sinf(yaw);
  // Columns: right, up, forward, in world space.
  const float right[3] = {cy, 0.0f, -sy};
  const float forward[3] = {sy * cp, -sp, cy * cp};
  const float up[3] = {right[1] * forward[2] - right[2] * forward[1],
                       right[2] * forward[0] - right[0] * forward[2],
                       right[0] * forward[1] - right[1] * forward[0]};
  const float eye_at[3] = {1880.0f, 40.0f, 2540.0f};
  float unscaled[12];
  for (int row = 0; row < 3; ++row) {
    unscaled[row * 3 + 0] = right[row];
    unscaled[row * 3 + 1] = up[row];
    unscaled[row * 3 + 2] = forward[row];
  }
  for (int col = 0; col < 3; ++col) {
    const float* axis = col == 0 ? right : (col == 1 ? up : forward);
    unscaled[9 + col] =
        -(eye_at[0] * axis[0] + eye_at[1] * axis[1] + eye_at[2] * axis[2]);
  }

  // A square render of 2208 at 110 degrees, the near plane of 5 cm at
  // 100 units a metre, and the eyes 0.475 units apart (63 mm at 7.5).
  const float half = 1104.0f;
  const float scale = 1.0f / tanf(0.9599f);
  const float near_plane = 5.0f;
  const float separation = 0.475f;
  const float per_rhw = ShiftPerRhw(scale, separation, half, near_plane);
  Check(per_rhw < 0.0f, "the right eye sees a near thing further to the left",
        per_rhw, -1.0);

  const float points[5][3] = {{1890.0f, 0.0f, 2560.0f},
                              {1900.0f, 12.0f, 2600.0f},
                              {1885.0f, 30.0f, 2548.0f},
                              {2400.0f, 0.0f, 3200.0f},
                              {1700.0f, -5.0f, 2900.0f}};
  const float rolls[3] = {0.0f, 0.35f, -0.6f};
  for (int r = 0; r < 3; ++r) {
    const float c = cosf(rolls[r]), s = sinf(rolls[r]);
    const float h[9] = {c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f};
    float left[12], right_eye[12];
    EyeMatrix(unscaled, h, -0.5f * separation, scale, scale, left);
    EyeMatrix(unscaled, h, 0.5f * separation, scale, scale, right_eye);
    double worst_x = 0.0, worst_yz = 0.0;
    int seen = 0;
    for (int i = 0; i < 5; ++i) {
      const Projected a = Project(left, points[i], half, half, near_plane);
      const Projected b = Project(right_eye, points[i], half, half, near_plane);
      if (!a.visible || !b.visible) continue;
      ++seen;
      const double derived = a.x + per_rhw * a.rhw;
      const double ex = fabs(derived - b.x) / (fabs(b.x) + 1.0);
      if (ex > worst_x) worst_x = ex;
      const double eyz = fabs(a.y - b.y) + fabs(a.rhw - b.rhw) + fabs(a.z - b.z);
      if (eyz > worst_yz) worst_yz = eyz;
    }
    char what[96];
    sprintf_s(what, sizeof(what),
              "roll %.2f: the derived x is the x of the right eye", rolls[r]);
    Check(seen == 5 && worst_x < 1e-5, what, worst_x, 0.0);
    sprintf_s(what, sizeof(what),
              "roll %.2f: y, z and rhw are the same in both eyes", rolls[r]);
    Check(worst_yz < 1e-4, what, worst_yz, 0.0);
  }

  // The copy. 32 byte vertices: x, y, z, rhw, diffuse, specular, u, v.
  struct V {
    float x, y, z, rhw;
    uint32_t diffuse, specular;
    float u, v;
  };
  V world[3] = {{100.0f, 50.0f, 0.9f, 0.1f, 1, 2, 0.5f, 0.5f},
                {200.0f, 60.0f, 0.5f, 0.5f, 3, 4, 0.1f, 0.2f},
                {300.0f, 70.0f, 0.99f, 0.01f, 5, 6, 0.9f, 0.8f}};
  V out[3];
  BatchKind kind = CopyForRightEye(reinterpret_cast<const uint8_t*>(world),
                                   reinterpret_cast<uint8_t*>(out), 3,
                                   sizeof(V), 2208.0f, -40.0f);
  Check(kind == kWorld, "a batch with depth is world", kind, kWorld);
  Near("each world vertex moves by the half and its own rhw", out[1].x,
       200.0 + 2208.0 - 40.0 * 0.5, 1e-3);
  Near("a far vertex moves by little more than the half", out[2].x,
       300.0 + 2208.0 - 40.0 * 0.01, 1e-3);
  Check(out[1].y == world[1].y && out[1].rhw == world[1].rhw &&
            out[1].u == world[1].u && out[1].diffuse == world[1].diffuse,
        "y, rhw, colour and texture are copied as they are", 1, 1);
  Check(world[1].x == 200.0f, "the batch of the game is not changed",
        world[1].x, 200.0);

  V flat[2] = {{10.0f, 10.0f, 0.0f, 1.0f, 0, 0, 0, 0},
               {20.0f, 10.0f, 0.0f, 1.0f, 0, 0, 0, 0}};
  kind = CopyForRightEye(reinterpret_cast<const uint8_t*>(flat),
                         reinterpret_cast<uint8_t*>(out), 2, sizeof(V),
                         2208.0f, -40.0f);
  Check(kind == kFlat, "every rhw is 1: a flat batch", kind, kFlat);
  Near("a flat batch moves by the half only", out[1].x, 20.0 + 2208.0, 1e-4);

  V sky[2] = {{0.0f, 0.0f, 1.0f, 0.2f, 0, 0, 0, 0},
              {500.0f, 0.0f, 1.0f, 0.3f, 0, 0, 0, 0}};
  kind = CopyForRightEye(reinterpret_cast<const uint8_t*>(sky),
                         reinterpret_cast<uint8_t*>(out), 2, sizeof(V),
                         2208.0f, -40.0f);
  Check(kind == kSky, "every z is 1: the sky", kind, kSky);
  Near("the sky moves by the half only", out[1].x, 500.0 + 2208.0, 1e-4);

  Check(VertexCount(4, 10) == 30, "a list of 10 triangles reads 30 vertices",
        VertexCount(4, 10), 30);
  Check(VertexCount(5, 10) == 12, "a strip of 10 triangles reads 12 vertices",
        VertexCount(5, 10), 12);
  Check(ShiftPerRhw(scale, separation, half, 0.0f) == 0.0f,
        "no near plane gives no move", 0, 0);
}

}  // namespace one_walk_checks
