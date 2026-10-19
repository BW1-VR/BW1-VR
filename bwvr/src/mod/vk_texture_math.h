// The pixels of a texture of the game as RGBA8, for the Vulkan renderer
// (vk_scene.cpp, docs/backend.md, "The Vulkan renderer", "The textures").
// Pure: no Windows, no Vulkan. Checks in test/controller_math/
// vk_texture_checks.h.
//
// A DirectDraw surface gives its pixels with a mask for each channel. A
// channel of b bits with the value v becomes (v * 255 + (2^b - 1) / 2) /
// (2^b - 1), and a channel with no mask is the fallback (0 for a color, 255
// for the alpha). The textures of the game are 16 bits (A4R4G4B4,
// X1R5G5B5), so one table of 65536 RGBA values for a format converts a pixel
// with one read. A format of 24 or 32 bits takes the shift of each channel,
// and a table of the values for a channel of fewer than 8 bits.
#pragma once

#include <stdint.h>

namespace vk_texture_math {

// The place of the lowest bit of a mask and its count of bits. 0 and 0 for
// no mask.
inline void MaskShape(uint32_t mask, int* shift, int* bits) {
  *shift = 0;
  *bits = 0;
  if (mask == 0) return;
  while (((mask >> *shift) & 1u) == 0) ++*shift;
  while (*shift + *bits < 32 && ((mask >> (*shift + *bits)) & 1u) != 0) {
    ++*bits;
  }
}

// One channel of a pixel as 0 to 255. The reference for the tables.
inline uint32_t Expand(uint32_t pixel, uint32_t mask, uint32_t fallback) {
  if (mask == 0) return fallback;
  int shift, bits;
  MaskShape(mask, &shift, &bits);
  const uint32_t v = (pixel & mask) >> shift;
  const uint32_t max = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
  return static_cast<uint32_t>(
      (static_cast<uint64_t>(v) * 255u + max / 2u) / max);
}

// The masks of a format. alpha_mask counts only when has_alpha.
struct Format {
  uint32_t r, g, b, a;
  bool has_alpha;
};

// A pixel as RGBA8 in the order of the bytes R, G, B, A (a little endian
// uint32_t), as Expand makes it.
inline uint32_t ToRgba(uint32_t pixel, const Format& f) {
  return Expand(pixel, f.r, 0) | Expand(pixel, f.g, 0) << 8 |
         Expand(pixel, f.b, 0) << 16 |
         (f.has_alpha ? Expand(pixel, f.a, 255) : 255u) << 24;
}

// The table of a format of 16 bits: lut[p] is ToRgba(p) for each p.
inline void BuildLut16(const Format& f, uint32_t lut[65536]) {
  for (uint32_t p = 0; p < 65536u; ++p) lut[p] = ToRgba(p, f);
}

// One channel of a format of 24 or 32 bits: the shift, the mask after the
// shift, and for fewer than 8 bits the 0 to 255 value of each v.
struct Channel {
  int shift;
  uint32_t mask;      // after the shift
  uint32_t fallback;  // with no mask
  bool direct;        // 8 bits: the value is v
  uint8_t value[256];
};

inline void BuildChannel(uint32_t mask, uint32_t fallback, Channel* c) {
  int shift, bits;
  MaskShape(mask, &shift, &bits);
  c->shift = shift;
  c->mask = mask == 0 ? 0u : mask >> shift;
  c->fallback = fallback;
  c->direct = bits == 8;
  for (uint32_t v = 0; v < 256u; ++v) {
    c->value[v] = static_cast<uint8_t>(
        bits > 0 && bits < 8 && v <= c->mask ? Expand(v << shift, mask, 0)
                                             : 0);
  }
}

// One channel of a pixel of 24 or 32 bits with its Channel. Expand for a
// channel of 1 to 8 bits. A channel of more than 8 bits goes to Expand.
inline uint32_t ChannelValue(uint32_t pixel, const Channel& c,
                             uint32_t mask) {
  if (c.mask == 0) return c.fallback;
  const uint32_t v = (pixel >> c.shift) & c.mask;
  if (c.direct) return v;
  if (c.mask <= 0xFFu) return c.value[v];
  return Expand(pixel, mask, c.fallback);
}

}  // namespace vk_texture_math
