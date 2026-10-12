// THE COPY OF THE MOD BETWEEN TWO SURFACES, the pixels (present_watch.cpp,
// blt_texture_copy, and docs/backend.md, "A copy between two textures").
// Pure math: no Windows, no game. The checks are in
// test/controller_math/blt_copy_checks.h.
//
// A pixel format is the bit masks of DirectDraw for red, green, blue and
// alpha, and 2, 3 or 4 bytes for each pixel. A copy with one format copies
// the bytes. A copy between two formats takes each channel to 8 bits and
// then to the bits of the other format. A source with no alpha gives full
// alpha.

#pragma once

#include <stdint.h>
#include <string.h>

namespace blt_copy {

struct Format {
  uint32_t red;
  uint32_t green;
  uint32_t blue;
  uint32_t alpha;  // 0 when the format has no alpha
  int bytes;       // 2, 3 or 4
};

inline bool Usable(const Format& f) {
  return (f.bytes == 2 || f.bytes == 3 || f.bytes == 4) && f.red != 0 &&
         f.green != 0 && f.blue != 0;
}

inline bool Same(const Format& a, const Format& b) {
  return a.red == b.red && a.green == b.green && a.blue == b.blue &&
         a.alpha == b.alpha && a.bytes == b.bytes;
}

inline int ShiftOf(uint32_t mask) {
  if (mask == 0) return 0;
  int shift = 0;
  while ((mask & 1u) == 0) {
    mask >>= 1;
    ++shift;
  }
  return shift;
}

// The largest value of the channel, for example 31 for 5 bits.
inline uint32_t TopOf(uint32_t mask) {
  return mask == 0 ? 0 : mask >> ShiftOf(mask);
}

// The channel of the mask in a pixel, as 0 to 255.
inline uint32_t Get8(uint32_t pixel, uint32_t mask) {
  const uint32_t top = TopOf(mask);
  if (top == 0) return 255;
  const uint32_t value = (pixel & mask) >> ShiftOf(mask);
  return (value * 255u + top / 2u) / top;
}

// A value of 0 to 255 in the bits of the mask.
inline uint32_t Put8(uint32_t value, uint32_t mask) {
  const uint32_t top = TopOf(mask);
  if (top == 0) return 0;
  return ((value * top + 127u) / 255u) << ShiftOf(mask);
}

inline uint32_t Convert(uint32_t pixel, const Format& from, const Format& to) {
  const uint32_t alpha = from.alpha != 0 ? Get8(pixel, from.alpha) : 255u;
  return Put8(Get8(pixel, from.red), to.red) |
         Put8(Get8(pixel, from.green), to.green) |
         Put8(Get8(pixel, from.blue), to.blue) | Put8(alpha, to.alpha);
}

inline uint32_t Read(const uint8_t* p, int bytes) {
  uint32_t v = 0;
  for (int i = 0; i < bytes; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
  return v;
}

inline void Write(uint8_t* p, int bytes, uint32_t v) {
  for (int i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// Copies a block of width by height pixels from (in_x, in_y) of the source to
// (out_x, out_y) of the destination. False, with no write, when a format is
// not usable or a row is longer than its pitch allows.
inline bool CopyBlock(const uint8_t* in, int in_pitch, int in_x, int in_y,
                      const Format& from, uint8_t* out, int out_pitch,
                      int out_x, int out_y, const Format& to, int width,
                      int height) {
  if (in == nullptr || out == nullptr || width <= 0 || height <= 0 ||
      in_x < 0 || in_y < 0 || out_x < 0 || out_y < 0 || !Usable(from) ||
      !Usable(to) || in_pitch < (in_x + width) * from.bytes ||
      out_pitch < (out_x + width) * to.bytes) {
    return false;
  }
  const bool same = Same(from, to);
  for (int y = 0; y < height; ++y) {
    const uint8_t* src = in + static_cast<size_t>(in_y + y) * in_pitch +
                         static_cast<size_t>(in_x) * from.bytes;
    uint8_t* dst = out + static_cast<size_t>(out_y + y) * out_pitch +
                   static_cast<size_t>(out_x) * to.bytes;
    if (same) {
      memcpy(dst, src, static_cast<size_t>(width) * to.bytes);
      continue;
    }
    for (int x = 0; x < width; ++x) {
      Write(dst + x * to.bytes, to.bytes,
            Convert(Read(src + x * from.bytes, from.bytes), from, to));
    }
  }
  return true;
}

// A compressed format of blocks of 4 by 4 pixels. The bytes of one block for
// the FourCC codes DXT1 to DXT5, and 0 for each other code.
inline int BlockBytes(uint32_t fourcc) {
  const uint32_t dxt = 'D' | ('X' << 8) | ('T' << 16);
  if ((fourcc & 0x00FFFFFFu) != dxt) return 0;
  const uint32_t n = (fourcc >> 24) - '0';
  if (n == 1) return 8;
  if (n >= 2 && n <= 5) return 16;
  return 0;
}

// The bytes of a row of blocks. The pitch of a lock of a compressed surface
// is that row, or the row with some room at its end. A pitch that is
// smaller, or more than 4 times the row, is not a row: the row is then used.
inline int BlockPitch(int locked_pitch, int width, int block_bytes) {
  const int row = ((width + 3) / 4) * block_bytes;
  return locked_pitch >= row && locked_pitch <= row * 4 ? locked_pitch : row;
}

// True when the rect from (left, top) to (right, bottom) is on the grid of
// the blocks: each edge on a multiple of 4, or the right or the bottom edge
// on the edge of the surface.
inline bool OnBlocks(int left, int top, int right, int bottom, int width,
                     int height) {
  return left % 4 == 0 && top % 4 == 0 &&
         (right % 4 == 0 || right == width) &&
         (bottom % 4 == 0 || bottom == height);
}

// Copies the blocks of a rect of pixels from (in_x, in_y) to (out_x, out_y).
// The rect is on the grid of the blocks. False, with no write, when it is
// not, or when a row is longer than its pitch allows.
inline bool CopyBlocks(const uint8_t* in, int in_pitch, int in_x, int in_y,
                       uint8_t* out, int out_pitch, int out_x, int out_y,
                       int width, int height, int block_bytes) {
  if (in == nullptr || out == nullptr || width <= 0 || height <= 0 ||
      block_bytes <= 0 || in_x < 0 || in_y < 0 || out_x < 0 || out_y < 0 ||
      in_x % 4 != 0 || in_y % 4 != 0 || out_x % 4 != 0 || out_y % 4 != 0) {
    return false;
  }
  const int blocks_wide = (width + 3) / 4;
  const int blocks_high = (height + 3) / 4;
  const int row = blocks_wide * block_bytes;
  if (in_pitch < (in_x / 4) * block_bytes + row ||
      out_pitch < (out_x / 4) * block_bytes + row) {
    return false;
  }
  for (int by = 0; by < blocks_high; ++by) {
    memcpy(out + static_cast<size_t>(out_y / 4 + by) * out_pitch +
               static_cast<size_t>(out_x / 4) * block_bytes,
           in + static_cast<size_t>(in_y / 4 + by) * in_pitch +
               static_cast<size_t>(in_x / 4) * block_bytes,
           static_cast<size_t>(row));
  }
  return true;
}

}  // namespace blt_copy
