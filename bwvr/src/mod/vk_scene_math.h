// THE TARGET, THE VIEWPORT AND THE CLEARS of the Vulkan renderer
// (vk_scene.cpp, docs/backend.md, "The Vulkan renderer"). Pure math: no
// Windows, no Vulkan, no game. The checks are in
// test/controller_math/vk_scene_checks.h.

#pragma once

#include <stdint.h>

namespace vk_scene_math {

// A rect of pixels: x0 and y0 are in it, x1 and y1 are not.
struct Rect {
  int64_t x0, y0, x1, y1;
};

inline Rect Intersect(const Rect& a, const Rect& b) {
  Rect r;
  r.x0 = a.x0 > b.x0 ? a.x0 : b.x0;
  r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
  r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
  r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
  return r;
}

// The part of a rect that is in a target of width by height pixels. False
// when no pixel is.
inline bool InTarget(const Rect& r, int64_t width, int64_t height,
                     Rect* out) {
  const Rect all = {0, 0, width, height};
  *out = Intersect(r, all);
  return out->x1 > out->x0 && out->y1 > out->y0;
}

// The rect of a viewport of Direct3D: its corner and its size.
inline Rect ViewportRect(uint32_t x, uint32_t y, uint32_t width,
                         uint32_t height) {
  const Rect r = {x, y, static_cast<int64_t>(x) + width,
                  static_cast<int64_t>(y) + height};
  return r;
}

// The color of a clear of Direct3D (A8R8G8B8) as red, green, blue and alpha
// from 0 to 1.
inline void ClearColor(uint32_t argb, float out[4]) {
  out[0] = static_cast<float>((argb >> 16) & 255u) / 255.0f;
  out[1] = static_cast<float>((argb >> 8) & 255u) / 255.0f;
  out[2] = static_cast<float>(argb & 255u) / 255.0f;
  out[3] = static_cast<float>((argb >> 24) & 255u) / 255.0f;
}

// The depth of a clear, in 0 to 1.
inline float ClearDepth(float z) {
  if (!(z >= 0.0f)) return 0.0f;
  return z > 1.0f ? 1.0f : z;
}

// The layers of a draw or a clear: bit 0 the left eye, bit 1 the right eye.
// In one walk the mode names them (one_walk.h: 0 both, 1 the left eye
// only, 2 the right eye only). With no one walk the walk is the left eye.
inline uint32_t Eyes(bool one_walk, int mode) {
  if (!one_walk) return 1u;
  return mode == 1 ? 1u : mode == 2 ? 2u : 3u;
}

// True when a draw or a clear goes to another target than the image of the
// eyes: the renderer follows the target, it knows the target of the eyes,
// and render target 0 now is a different one.
inline bool OffTarget(bool follow, const void* eyes, const void* now) {
  return follow && eyes != nullptr && now != eyes;
}

}  // namespace vk_scene_math
