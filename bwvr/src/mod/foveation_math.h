// Foveated rendering: the shading rate of each part of an eye picture
// (vk_scene.cpp, docs/backend.md, "Foveated rendering"). Pure: no Windows,
// no Vulkan. Checks in test/controller_math/foveation_checks.h.
//
// An eye picture is the symmetric view of the game: x of a pixel goes from
// -tan_h at the left edge to +tan_h at the right edge, and y from +tan_v at
// the top to -tan_v at the bottom. The centre of the fovea is a direction in
// the space of the eye, as tangents (x to the right, y up): the gaze of the
// eye tracker, or 0, 0 (straight ahead, the middle of the picture) with no
// eye tracker. A part of the picture within inner_deg of that direction has
// the full rate, a part within outer_deg 2 by 2 pixels for one shade, and
// the rest 4 by 4 (or 2 by 2 when the GPU has no 4 by 4).
#pragma once

#include <math.h>
#include <stdint.h>

namespace foveation_math {

// The rates of VK_KHR_fragment_shading_rate: (log2 width << 2) | log2 height.
const uint8_t kRateFull = 0;     // 1 by 1
const uint8_t kRateHalf = 5;     // 2 by 2
const uint8_t kRateQuarter = 10; // 4 by 4

struct Level {
  float inner_deg;
  float outer_deg;
};

// The levels of the key foveation: 1 light, 2 medium, 3 strong. 0 and other
// values have no foveation (inner_deg 0).
inline Level LevelOf(int foveation) {
  switch (foveation) {
    case 1: return {30.0f, 45.0f};
    case 2: return {22.0f, 35.0f};
    case 3: return {15.0f, 25.0f};
    default: return {0.0f, 0.0f};
  }
}

struct Params {
  float tan_h;  // tan of half the horizontal angle of the picture
  float tan_v;  // tan of half the vertical angle
  float inner_deg;
  float outer_deg;
  bool quarter;  // the GPU has the rate 4 by 4
};

// The tangents of a direction of the space of the eye (x right, y up, -z
// forward). False for a direction that does not point forward.
inline bool GazeTangent(const float dir[3], float* gx, float* gy) {
  if (!(dir[2] < -1e-3f)) return false;
  *gx = dir[0] / -dir[2];
  *gy = dir[1] / -dir[2];
  return true;
}

// The tangents of a point of the picture (pixels, y down) of width by height.
inline void PixelTangent(float px, float py, uint32_t width, uint32_t height,
                         const Params& p, float* tx, float* ty) {
  *tx = (2.0f * px / static_cast<float>(width) - 1.0f) * p.tan_h;
  *ty = (1.0f - 2.0f * py / static_cast<float>(height)) * p.tan_v;
}

// The point of the picture of a direction, in pixels (y down).
inline void TangentPixel(float tx, float ty, uint32_t width, uint32_t height,
                         const Params& p, float* px, float* py) {
  *px = (tx / p.tan_h + 1.0f) * 0.5f * static_cast<float>(width);
  *py = (1.0f - ty / p.tan_v) * 0.5f * static_cast<float>(height);
}

// The angle in degrees between two directions given as tangents.
inline float AngleDeg(float ax, float ay, float bx, float by) {
  const float dot = ax * bx + ay * by + 1.0f;
  const float la = sqrtf(ax * ax + ay * ay + 1.0f);
  const float lb = sqrtf(bx * bx + by * by + 1.0f);
  float c = dot / (la * lb);
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 57.2957795f;
}

// The rate for an angle from the centre of the fovea.
inline uint8_t RateForAngle(float deg, const Params& p) {
  if (p.inner_deg <= 0.0f || deg < p.inner_deg) return kRateFull;
  if (deg < p.outer_deg || !p.quarter) return kRateHalf;
  return kRateQuarter;
}

// The map of the rates: one byte for each texel of texel_w by texel_h
// pixels of a picture of width by height, map_w by map_h texels, row after
// row. A texel takes the rate of its point that is nearest to the centre of
// the fovea, so no part of the fovea gets a lower rate.
inline void BuildMap(uint8_t* out, uint32_t map_w, uint32_t map_h,
                     uint32_t texel_w, uint32_t texel_h, uint32_t width,
                     uint32_t height, float gx, float gy, const Params& p) {
  float cx = 0.0f, cy = 0.0f;
  TangentPixel(gx, gy, width, height, p, &cx, &cy);
  for (uint32_t ty = 0; ty < map_h; ++ty) {
    const float y0 = static_cast<float>(ty * texel_h);
    const float y1 = static_cast<float>((ty + 1) * texel_h);
    const float py = cy < y0 ? y0 : (cy > y1 ? y1 : cy);
    for (uint32_t tx = 0; tx < map_w; ++tx) {
      const float x0 = static_cast<float>(tx * texel_w);
      const float x1 = static_cast<float>((tx + 1) * texel_w);
      const float px = cx < x0 ? x0 : (cx > x1 ? x1 : cx);
      float ax = 0.0f, ay = 0.0f;
      PixelTangent(px, py, width, height, p, &ax, &ay);
      out[ty * map_w + tx] = RateForAngle(AngleDeg(ax, ay, gx, gy), p);
    }
  }
}

// THE MARKS of the test (foveation_show): point k of n on the edge of the
// directions within deg degrees of the centre of the fovea, as tangents.
// False when that point does not point forward.
inline bool ConePoint(float gx, float gy, float deg, int k, int n, float* tx,
                      float* ty) {
  // The centre as a direction with z forward, and two directions at a right
  // angle to it.
  float d[3] = {gx, gy, 1.0f};
  const float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  for (int i = 0; i < 3; ++i) d[i] /= dl;
  // u = up x d, normalized. Up is never parallel to a forward direction.
  float u[3] = {d[2], 0.0f, -d[0]};
  const float ul = sqrtf(u[0] * u[0] + u[2] * u[2]);
  u[0] /= ul;
  u[2] /= ul;
  const float v[3] = {d[1] * u[2], d[2] * u[0] - d[0] * u[2], -d[1] * u[0]};
  const float a = deg / 57.2957795f;
  const float phi = 6.28318531f * static_cast<float>(k) / static_cast<float>(n);
  const float c = cosf(a), s = sinf(a);
  float p[3];
  for (int i = 0; i < 3; ++i) {
    p[i] = c * d[i] + s * (cosf(phi) * u[i] + sinf(phi) * v[i]);
  }
  if (p[2] < 0.05f) return false;
  *tx = p[0] / p[2];
  *ty = p[1] / p[2];
  return true;
}

// The count of texels of a picture size, rounded up.
inline uint32_t TexelCount(uint32_t pixels, uint32_t texel) {
  return texel == 0 ? 0 : (pixels + texel - 1) / texel;
}

}  // namespace foveation_math
