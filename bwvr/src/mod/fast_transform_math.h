// The two vertex loops of the engine with SSE (docs/backend.md, "The vertex
// loops with SSE"). Pure math with no game, so that the checks in
// bwvr/test/controller_math/ can compare it with the x87 code of the game.
//
// THE LOOPS. fn_00856D40 (TransformClip) and fn_0085AB10 (TransformLit)
// transform the vertices of a mesh by the 3x4 matrix of the camera, and
// project each one to a D3DTLVERTEX of LH3DP3::Table1. TransformClip also
// writes the clip flags of each vertex to LH3DP3::Table0, and projects only a
// vertex with no flag. TransformLit projects each vertex, and lights it from
// its normal. The game makes one vertex at a time on the x87 unit. These
// functions make four at a time.
//
// THE SAME BITS. Direct3D sets the x87 unit to a precision of 24 bits, so
// the game rounds the result of each operation to a float. An SSE operation
// on floats rounds each result to a float too. So each function here makes
// the operations of the game in the order of the game, and gets the same
// bits. The order of an addition matters, the order of the two values of
// one addition or multiplication does not. The comparisons of the game are
// fcomp with fnstsw: a test of C0 is true for a NaN, a test of C0 and C3 is
// false for a NaN. The caller checks the precision and the rounding of the
// x87 unit and of SSE first (Exact). The bits differ only where a value
// between two operations is out of the range of a float, which the x87
// unit keeps.
#pragma once

#include <emmintrin.h>
#include <stdint.h>
#include <string.h>

namespace fast_transform_math {

// The values that the loops read from the globals of the game.
struct Consts {
  float m[12];          // the camera matrix, rows of 3 (*0x00EA9EA0)
  float near_plane;     // 0x00E839E0
  float half_x;         // 0x00E839F0
  float half_y;         // 0x00E839F4
  float max_x;          // g_MaxScreen.x, 0x00C2AB00
  float max_y;          // g_MaxScreen.y, 0x00C2AB04
  uint32_t color;       // the diffuse color of TransformClip, 0x00C37D8C
  uint32_t specular;    // 0x00E9FE2C
};

// The light of TransformLit.
struct Light {
  float direction[3];   // 0x00F03140
  int32_t ambient;      // 0x00C39264
  uint32_t color;       // the color of the mesh
};

// True when the x87 control word and MXCSR give the bits of the game: a
// precision of 24 bits and rounding to the nearest, no flush of small
// values to zero.
inline bool Exact(uint16_t x87_control, uint32_t mxcsr) {
  const bool x87 = (x87_control & 0x0F00u) == 0x0000u;  // PC 24 bits, RC near
  const bool sse = (mxcsr & 0xE040u) == 0;  // RC near, no FTZ, no DAZ
  return x87 && sse;
}

namespace detail {

const int kStride = 32;  // a vertex in and a D3DTLVERTEX out

// Four vertices from `in`, each 32 bytes: x, y, z, u and v, nx, ny, nz as
// rows, turned into columns.
struct Columns {
  __m128 x, y, z, u;
  __m128 v, nx, ny, nz;
};

inline Columns Load4(const uint8_t* in, bool normals) {
  const float* f = reinterpret_cast<const float*>(in);
  Columns c;
  c.x = _mm_loadu_ps(f);
  c.y = _mm_loadu_ps(f + 8);
  c.z = _mm_loadu_ps(f + 16);
  c.u = _mm_loadu_ps(f + 24);
  _MM_TRANSPOSE4_PS(c.x, c.y, c.z, c.u);
  if (normals) {
    c.v = _mm_loadu_ps(f + 4);
    c.nx = _mm_loadu_ps(f + 12);
    c.ny = _mm_loadu_ps(f + 20);
    c.nz = _mm_loadu_ps(f + 28);
    _MM_TRANSPOSE4_PS(c.v, c.nx, c.ny, c.nz);
  }
  return c;
}

// The second half of a D3DTLVERTEX: the color, the specular color, and the u
// and v of the input vertex (its bytes 12 to 19).
inline void StoreTail(uint8_t* out, uint32_t color, uint32_t specular,
                      const uint8_t* in) {
  const __m128i colors = _mm_unpacklo_epi32(
      _mm_cvtsi32_si128(static_cast<int>(color)),
      _mm_cvtsi32_si128(static_cast<int>(specular)));
  const __m128i uv = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(in + 12));
  _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 16),
                   _mm_unpacklo_epi64(colors, uv));
}

// The vertices from `base` to `count` (1 to 3) through `block`, by way of a
// copy of 4 vertices. The copy of the output starts as the output, so that
// the bytes that the block does not write stay.
template <typename Block>
inline void Rest(const uint8_t* in, uint32_t count, uint32_t base,
                 uint8_t* out, uint32_t* flags, Block block) {
  const uint32_t n = count - base;
  if (n == 0) return;
  uint8_t in4[4 * kStride];
  uint8_t out4[4 * kStride];
  uint32_t flags4[4];
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t from = base + (i < n ? i : n - 1);
    memcpy(in4 + i * kStride, in + from * kStride, kStride);
    memcpy(out4 + i * kStride, out + from * kStride, kStride);
  }
  block(in4, out4, flags4);
  memcpy(out + base * kStride, out4, n * kStride);
  if (flags != nullptr) memcpy(flags + base, flags4, n * 4);
}

struct Camera {
  __m128 x, y, z;  // camera space
};

// The values of Consts in each lane, made one time for a mesh. In 32-bit
// code there are 8 SSE registers, so the loop reads them from memory.
struct Wide {
  __m128 m[12];
  __m128 near_plane, half_x, half_y, max_x, max_y, one;
  explicit Wide(const Consts& c) {
    for (int i = 0; i < 12; ++i) m[i] = _mm_set1_ps(c.m[i]);
    near_plane = _mm_set1_ps(c.near_plane);
    half_x = _mm_set1_ps(c.half_x);
    half_y = _mm_set1_ps(c.half_y);
    max_x = _mm_set1_ps(c.max_x);
    max_y = _mm_set1_ps(c.max_y);
    one = _mm_set1_ps(1.0f);
  }
};

// The order of the additions of the matrix.
enum Order {
  // x = ((m6 z + m3 y) + m0 x) + m9
  // y = ((m1 x + m7 z) + m4 y) + m10
  // z = ((m2 x + m8 z) + m5 y) + m11
  kMeshOrder,
  // The loop of fn_0084BA90 with a matrix for each group of vertices:
  // x = ((m6 z + m0 x) + m3 y) + m9
  // y = ((m4 y + m1 x) + m7 z) + m10
  // z = ((m5 y + m2 x) + m8 z) + m11
  kGroupOrder,
};

inline Camera Transform(const Wide& w, __m128 x, __m128 y, __m128 z,
                        Order order = kMeshOrder) {
  Camera out;
  if (order == kGroupOrder) {
    out.x = _mm_add_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(w.m[6], z), _mm_mul_ps(x, w.m[0])),
                   _mm_mul_ps(w.m[3], y)),
        w.m[9]);
    out.y = _mm_add_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(w.m[4], y), _mm_mul_ps(x, w.m[1])),
                   _mm_mul_ps(w.m[7], z)),
        w.m[10]);
    out.z = _mm_add_ps(
        _mm_add_ps(_mm_add_ps(_mm_mul_ps(w.m[5], y), _mm_mul_ps(w.m[2], x)),
                   _mm_mul_ps(w.m[8], z)),
        w.m[11]);
    return out;
  }
  out.x = _mm_add_ps(
      _mm_add_ps(_mm_add_ps(_mm_mul_ps(w.m[6], z), _mm_mul_ps(y, w.m[3])),
                 _mm_mul_ps(x, w.m[0])),
      w.m[9]);
  out.y = _mm_add_ps(
      _mm_add_ps(_mm_add_ps(_mm_mul_ps(w.m[1], x), _mm_mul_ps(w.m[7], z)),
                 _mm_mul_ps(y, w.m[4])),
      w.m[10]);
  out.z = _mm_add_ps(
      _mm_add_ps(_mm_add_ps(_mm_mul_ps(w.m[2], x), _mm_mul_ps(w.m[8], z)),
                 _mm_mul_ps(y, w.m[5])),
      w.m[11]);
  return out;
}

// A screen value of the game: 0 when it is below 0 or a NaN, the limit when
// it is above the limit.
inline __m128 Clamp(__m128 v, __m128 top) {
  const __m128 low = _mm_cmpnge_ps(v, _mm_setzero_ps());  // C0: below, NaN
  const __m128 high = _mm_cmpgt_ps(v, top);  // not C0 and not C3
  v = _mm_or_ps(_mm_andnot_ps(high, v), _mm_and_ps(high, top));
  return _mm_andnot_ps(low, v);
}

struct Screen {
  __m128 x, y, z, rhw;
};

// The order of the product of the y of the screen.
enum YOrder {
  kHalfYThenR,  // (half_y y) r, fn_00856D40
  kYRThenHalf,  // (y r) half_y, fn_0085AB10
  kHalfRThenY,  // (half_y r) y, fn_0084BA90
};

// r = 1 / z, x = (r x + 1) half_x, w = near r, z = 1 - w, and the y of the
// screen is half_y minus the product of half_y, y and r.
inline Screen Project(const Wide& w, const Camera& v, YOrder y_order) {
  const __m128 r = _mm_div_ps(w.one, v.z);
  Screen s;
  s.x = Clamp(_mm_mul_ps(_mm_add_ps(_mm_mul_ps(r, v.x), w.one), w.half_x),
              w.max_x);
  const __m128 scaled =
      y_order == kYRThenHalf  ? _mm_mul_ps(_mm_mul_ps(v.y, r), w.half_y)
      : y_order == kHalfRThenY ? _mm_mul_ps(_mm_mul_ps(w.half_y, r), v.y)
                               : _mm_mul_ps(_mm_mul_ps(w.half_y, v.y), r);
  s.y = Clamp(_mm_sub_ps(w.half_y, scaled), w.max_y);
  s.rhw = _mm_mul_ps(w.near_plane, r);
  s.z = _mm_sub_ps(w.one, s.rhw);
  return s;
}

}  // namespace detail

// The brightness of a lit vertex from the light value i (the dot product
// of the normal and the light, times 255, rounded to the nearest whole
// number), and the color of the vertex from the color of the mesh.
inline uint32_t LitColor(int32_t i, const Light& l) {
  // The game multiplies with imul and shifts with sar: a product of 32 bits
  // that wraps, and a shift that keeps the sign.
  int32_t level = l.ambient;
  if (i >= 0) {
    const int32_t product = static_cast<int32_t>(
        static_cast<uint32_t>(255 - l.ambient) * static_cast<uint32_t>(i));
    level = (product >> 8) + l.ambient;
  }
  const uint32_t a = static_cast<uint32_t>(level);
  const uint32_t rb = ((l.color & 0x00FF0000u) * a) & 0xFF0000FFu;
  const uint32_t g = ((l.color & 0x0000FF00u) * a) & 0x00FF0000u;
  const uint32_t b = ((l.color & 0x000000FFu) * a) & 0x0000FF00u;
  return ((rb | g | b) >> 8) | (l.color & 0xFF000000u);
}

namespace detail {

// One loop of the engine. kClip: the clip flags, and a vertex with a flag
// keeps its camera x, y and z at offsets 0, 4 and 12, and the value at
// offset 8 does not change. Else each vertex is projected. kLit: the color is
// lit by the normal (offsets 20, 24 and 28 of the input) with
// i = ((l2 nz + l0 nx) + l1 ny) 255, else it is the color of Consts.
template <bool kClip, bool kLit>
void TransformMesh(const Consts& c, const Light* l, Order order,
                   YOrder y_order, const uint8_t* in, uint32_t count,
                   uint8_t* out, uint32_t* flags) {
  const Wide w(c);
  const __m128 sign = _mm_set1_ps(-0.0f);
  const __m128i bit20 = _mm_set1_epi32(0x20), bit10 = _mm_set1_epi32(0x10);
  const __m128i bit08 = _mm_set1_epi32(0x08), bit04 = _mm_set1_epi32(0x04);
  const __m128i bit02 = _mm_set1_epi32(0x02);
  // The lane of z in a row, for the value at offset 8 that stays.
  const __m128 lane2 = _mm_castsi128_ps(_mm_set_epi32(0, -1, 0, 0));
  const __m128 l0 = _mm_set1_ps(kLit ? l->direction[0] : 0.0f);
  const __m128 l1 = _mm_set1_ps(kLit ? l->direction[1] : 0.0f);
  const __m128 l2 = _mm_set1_ps(kLit ? l->direction[2] : 0.0f);
  const __m128 full = _mm_set1_ps(255.0f);
  auto block = [&](const uint8_t* src, uint8_t* dst, uint32_t* f4) {
    (void)f4;  // no flags with kClip false
    const Columns in4 = Load4(src, kLit);
    const Camera v = Transform(w, in4.x, in4.y, in4.z, order);
    int32_t level[4] = {0, 0, 0, 0};
    if constexpr (kLit) {
      const __m128 dot = _mm_mul_ps(
          _mm_add_ps(
              _mm_add_ps(_mm_mul_ps(l2, in4.nz), _mm_mul_ps(l0, in4.nx)),
              _mm_mul_ps(l1, in4.ny)),
          full);
      _mm_storeu_si128(reinterpret_cast<__m128i*>(level),
                       _mm_cvtps_epi32(dot));
    }
    int clipped = 0;
    if constexpr (kClip) {
      const __m128 neg_z = _mm_xor_ps(v.z, sign);
      const __m128 nearer = _mm_cmpnge_ps(v.z, w.near_plane);
      const __m128 right = _mm_cmpgt_ps(v.x, v.z);
      const __m128 left = _mm_andnot_ps(right, _mm_cmpgt_ps(neg_z, v.x));
      const __m128 below = _mm_cmpgt_ps(v.y, v.z);
      const __m128 above = _mm_andnot_ps(below, _mm_cmpgt_ps(neg_z, v.y));
      const __m128i f = _mm_or_si128(
          _mm_or_si128(_mm_and_si128(_mm_castps_si128(nearer), bit20),
                       _mm_and_si128(_mm_castps_si128(right), bit10)),
          _mm_or_si128(
              _mm_and_si128(_mm_castps_si128(left), bit08),
              _mm_or_si128(_mm_and_si128(_mm_castps_si128(below), bit04),
                           _mm_and_si128(_mm_castps_si128(above), bit02))));
      _mm_storeu_si128(reinterpret_cast<__m128i*>(f4), f);
      clipped = ~_mm_movemask_ps(
          _mm_castsi128_ps(_mm_cmpeq_epi32(f, _mm_setzero_si128())));
    }
    Screen s = Project(w, v, y_order);
    _MM_TRANSPOSE4_PS(s.x, s.y, s.z, s.rhw);
    const __m128 projected[4] = {s.x, s.y, s.z, s.rhw};
    __m128 cx = v.x, cy = v.y, cz = v.z, cw = v.z;
    if constexpr (kClip) {
      _MM_TRANSPOSE4_PS(cx, cy, cz, cw);
    }
    const __m128 camera[4] = {cx, cy, cz, cw};
    for (int i = 0; i < 4; ++i) {
      float* row = reinterpret_cast<float*>(dst + i * kStride);
      if ((clipped & (1 << i)) != 0) {
        // x, y, the value at offset 8 as it was, z.
        const __m128 old = _mm_loadu_ps(row);
        _mm_storeu_ps(row, _mm_or_ps(_mm_andnot_ps(lane2, camera[i]),
                                     _mm_and_ps(lane2, old)));
      } else {
        _mm_storeu_ps(row, projected[i]);
      }
      StoreTail(dst + i * kStride, kLit ? LitColor(level[i], *l) : c.color,
                c.specular, src + i * kStride);
    }
  };
  uint32_t base = 0;
  for (; base + 4 <= count; base += 4) {
    block(in + base * kStride, out + base * kStride,
          kClip ? flags + base : nullptr);
  }
  Rest(in, count, base, out, kClip ? flags : nullptr, block);
}

}  // namespace detail

// fn_00856D40, the path with one matrix: the clip flags (0x20 nearer than
// the near plane, 0x10 right of the view and else 8 left of it, 4 below and
// else 2 above), the point on the screen of a vertex with no flag, and the
// color of the mesh.
inline void TransformClip(const Consts& c, const uint8_t* in, uint32_t count,
                          uint8_t* out, uint32_t* flags) {
  detail::TransformMesh<true, false>(c, nullptr, detail::kMeshOrder,
                                     detail::kHalfYThenR, in, count, out,
                                     flags);
}

// fn_0085AB10, the path with one matrix: no clip flags, each vertex is
// projected and lit.
inline void TransformLit(const Consts& c, const Light& l, const uint8_t* in,
                         uint32_t count, uint8_t* out) {
  detail::TransformMesh<false, true>(c, &l, detail::kMeshOrder,
                                     detail::kYRThenHalf, in, count, out,
                                     nullptr);
}

// fn_0084BA90: the clip flags and the light. `group` is the loop with a
// matrix for each group of vertices (Consts::m is the matrix of the group,
// and Light::direction the light of the group), else the loop with one
// matrix.
inline void TransformLitClip(const Consts& c, const Light& l, bool group,
                             const uint8_t* in, uint32_t count, uint8_t* out,
                             uint32_t* flags) {
  detail::TransformMesh<true, true>(
      c, &l, group ? detail::kGroupOrder : detail::kMeshOrder,
      detail::kHalfRThenY, in, count, out, flags);
}

}  // namespace fast_transform_math
