// Foveated resolution: an outer image with fewer pixels and a centre image
// with more, for one eye picture (vk_scene.cpp, docs/backend.md, "Foveated
// resolution"). Pure: no Windows, no Vulkan. Checks in
// test/controller_math/fovea_res_checks.h.
//
// The game gives each vertex in pixels of its render, width by height (the
// eye picture). The outer image has the whole view at outer_pct percent of
// the pixels on each side. The centre image has a rect of the view around
// the centre of the fovea at center_pct percent: its half width and half
// height are inset_deg degrees from the centre. A pass maps the pixels of
// the game into its image with a scale and an origin, so the same draws go
// into both images.
#pragma once

#include <math.h>
#include <stdint.h>

namespace fovea_res_math {

struct Layout {
  uint32_t outer_w, outer_h;    // the outer image
  uint32_t center_w, center_h;  // the centre image
  // The rect of the centre in pixels of the game.
  float inset_x0, inset_y0, inset_w, inset_h;
  // pixel of an image = (pixel of the game - origin) * scale
  float outer_scale_x, outer_scale_y;
  float center_scale_x, center_scale_y;
};

inline uint32_t Scaled(float pixels, float pct) {
  const float v = pixels * pct / 100.0f + 0.5f;
  return v < 16.0f ? 16u : static_cast<uint32_t>(v);
}

// The layout for a picture of width by height with the view tan_h by tan_v
// (tan of the half angles) and the centre of the fovea at the tangents gx,
// gy (x right, y up). outer_pct 5 to 100, center_pct 25 to 300, inset_deg
// 5 to 80.
inline Layout MakeLayout(uint32_t width, uint32_t height, float tan_h,
                         float tan_v, float gx, float gy, float outer_pct,
                         float center_pct, float inset_deg) {
  Layout l;
  const float w = static_cast<float>(width), h = static_cast<float>(height);
  l.outer_w = Scaled(w, outer_pct);
  l.outer_h = Scaled(h, outer_pct);
  l.outer_scale_x = static_cast<float>(l.outer_w) / w;
  l.outer_scale_y = static_cast<float>(l.outer_h) / h;
  // The half size of the rect: inset_deg on each side of the centre.
  const float t = tanf(inset_deg / 57.2957795f);
  float half_w = t / tan_h * 0.5f * w;
  float half_h = t / tan_v * 0.5f * h;
  if (half_w > 0.5f * w) half_w = 0.5f * w;
  if (half_h > 0.5f * h) half_h = 0.5f * h;
  if (half_w < 8.0f) half_w = 8.0f;
  if (half_h < 8.0f) half_h = 8.0f;
  // The centre in pixels, and the rect kept in the picture.
  const float cx = (gx / tan_h + 1.0f) * 0.5f * w;
  const float cy = (1.0f - gy / tan_v) * 0.5f * h;
  float x0 = cx - half_w, y0 = cy - half_h;
  if (x0 < 0.0f) x0 = 0.0f;
  if (y0 < 0.0f) y0 = 0.0f;
  if (x0 + 2.0f * half_w > w) x0 = w - 2.0f * half_w;
  if (y0 + 2.0f * half_h > h) y0 = h - 2.0f * half_h;
  l.inset_x0 = floorf(x0);
  l.inset_y0 = floorf(y0);
  l.inset_w = floorf(2.0f * half_w);
  l.inset_h = floorf(2.0f * half_h);
  l.center_w = Scaled(l.inset_w, center_pct);
  l.center_h = Scaled(l.inset_h, center_pct);
  l.center_scale_x = static_cast<float>(l.center_w) / l.inset_w;
  l.center_scale_y = static_cast<float>(l.center_h) / l.inset_h;
  return l;
}

// The tangent y (up) of a centre down_deg below straight ahead
// (fovea_res_center_down_deg). 0 to 60 degrees: a value outside goes to the
// nearest end.
inline float DownGaze(float down_deg) {
  if (!(down_deg > 0.0f)) return 0.0f;
  if (down_deg > 60.0f) down_deg = 60.0f;
  return -tanf(down_deg / 57.2957795f);
}

// The share of the pixels of the two images against the picture.
inline float PixelShare(const Layout& l, uint32_t width, uint32_t height) {
  return (static_cast<float>(l.outer_w) * l.outer_h +
          static_cast<float>(l.center_w) * l.center_h) /
         (static_cast<float>(width) * height);
}

// A rect of pixels of the game in an image with an origin and a scale: the
// rect of the image, x0, y0, x1, y1, not cut to the image.
inline void MapRect(float x0, float y0, float x1, float y1, float origin_x,
                    float origin_y, float scale_x, float scale_y, float out[4]) {
  out[0] = (x0 - origin_x) * scale_x;
  out[1] = (y0 - origin_y) * scale_y;
  out[2] = (x1 - origin_x) * scale_x;
  out[3] = (y1 - origin_y) * scale_y;
}

}  // namespace fovea_res_math
