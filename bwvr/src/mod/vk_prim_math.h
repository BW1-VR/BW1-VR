// The draws of points and lines of Direct3D 7 as triangles, for the backend
// of the mod on Vulkan (vk_ddraw.cpp, docs/backend.md, "The backend of the
// mod on Vulkan"). Pure: no Windows, no Vulkan. Checks in
// test/controller_math/vk_prim_checks.h.
//
// The Vulkan renderer draws triangles of vertices with XYZRHW, so x and y
// are pixels of the render target. Direct3D 7 draws a point as one pixel
// and a line as a line of one pixel. So a point becomes a square of 1 by 1
// pixels with its centre at the point, and a line becomes a rectangle of
// one pixel across, from its first vertex to its second vertex. Each corner
// is a copy of the vertex of its end, so the color and the texture
// coordinates are the values of the vertex. Only x and y change.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace vk_prim_math {

// The types of Direct3D 7 (D3DPRIMITIVETYPE).
const uint32_t kPointList = 1;
const uint32_t kLineList = 2;
const uint32_t kLineStrip = 3;

// The bytes of one vertex of a flexible vertex format, 0 for a format that
// Direct3D 7 does not know.
inline uint32_t FvfStride(uint32_t fvf) {
  uint32_t bytes = 0;
  switch (fvf & 0x00E) {  // D3DFVF_POSITION_MASK
    case 0x002: bytes = 12; break;  // XYZ
    case 0x004: bytes = 16; break;  // XYZRHW
    case 0x006: bytes = 16; break;  // XYZB1
    case 0x008: bytes = 20; break;  // XYZB2
    case 0x00A: bytes = 24; break;  // XYZB3
    case 0x00C: bytes = 28; break;  // XYZB4
    case 0x00E: bytes = 32; break;  // XYZB5
    default: return 0;
  }
  if (fvf & 0x010) bytes += 12;  // NORMAL
  if (fvf & 0x020) bytes += 4;   // RESERVED1
  if (fvf & 0x040) bytes += 4;   // DIFFUSE
  if (fvf & 0x080) bytes += 4;   // SPECULAR
  const uint32_t sets = (fvf >> 8) & 0xF;
  if (sets > 8) return 0;
  for (uint32_t i = 0; i < sets; ++i) {
    static const uint32_t kFloats[4] = {2, 3, 4, 1};
    bytes += 4 * kFloats[(fvf >> (16 + 2 * i)) & 3];
  }
  return bytes;
}

inline bool IsPoints(uint32_t type) { return type == kPointList; }
inline bool IsLines(uint32_t type) {
  return type == kLineList || type == kLineStrip;
}

// The count of points or lines of a draw of count elements (vertices, or
// indices when the draw has indices).
inline uint32_t PrimCount(uint32_t type, uint32_t count) {
  if (type == kPointList) return count;
  if (type == kLineList) return count / 2;
  if (type == kLineStrip) return count >= 2 ? count - 1 : 0;
  return 0;
}

// The two elements of line i: a and b.
inline void LineEnds(uint32_t type, uint32_t i, uint32_t* a, uint32_t* b) {
  if (type == kLineList) {
    *a = 2 * i;
    *b = 2 * i + 1;
  } else {
    *a = i;
    *b = i + 1;
  }
}

// The x and y of the 4 corners of a point at (x, y), in the order 0 1 2 3
// around the square: (x - 0.5, y - 0.5), (x + 0.5, y - 0.5),
// (x + 0.5, y + 0.5), (x - 0.5, y + 0.5).
inline void PointCorners(float x, float y, float corners[8]) {
  corners[0] = x - 0.5f;
  corners[1] = y - 0.5f;
  corners[2] = x + 0.5f;
  corners[3] = y - 0.5f;
  corners[4] = x + 0.5f;
  corners[5] = y + 0.5f;
  corners[6] = x - 0.5f;
  corners[7] = y + 0.5f;
}

// The x and y of the 4 corners of a line from (ax, ay) to (bx, by), one
// pixel across: corner 0 and 1 at a, corner 2 and 3 at b, in the order
// around the rectangle. A line of length 0 becomes a point at a.
inline void LineCorners(float ax, float ay, float bx, float by,
                        float corners[8]) {
  const float dx = bx - ax;
  const float dy = by - ay;
  const float length_sq = dx * dx + dy * dy;
  if (length_sq < 1e-12f) {
    PointCorners(ax, ay, corners);
    return;
  }
  const float length = sqrtf(length_sq);
  const float nx = -dy / length * 0.5f;
  const float ny = dx / length * 0.5f;
  corners[0] = ax + nx;
  corners[1] = ay + ny;
  corners[2] = ax - nx;
  corners[3] = ay - ny;
  corners[4] = bx - nx;
  corners[5] = by - ny;
  corners[6] = bx + nx;
  corners[7] = by + ny;
}

// The vertices of a list of triangles for a draw of points or lines of
// XYZRHW vertices with stride bytes each. indices can be null. The out
// buffer takes 6 vertices for each point or line, of stride bytes each.
// The return is the count of vertices in out, 0 for a draw that is not of
// points or lines.
inline uint32_t Expand(uint32_t type, const uint8_t* vertices,
                       uint32_t vertex_count, const uint16_t* indices,
                       uint32_t index_count, uint32_t stride, uint8_t* out,
                       uint32_t out_room) {
  if (stride < 16 || vertices == nullptr) return 0;
  const uint32_t elements = indices != nullptr ? index_count : vertex_count;
  const uint32_t prims = PrimCount(type, elements);
  uint32_t made = 0;
  for (uint32_t i = 0; i < prims; ++i) {
    if (made + 6 > out_room) break;
    uint32_t ea = i;
    uint32_t eb = i;
    if (IsLines(type)) LineEnds(type, i, &ea, &eb);
    const uint32_t va = indices != nullptr ? indices[ea] : ea;
    const uint32_t vb = indices != nullptr ? indices[eb] : eb;
    if (va >= vertex_count || vb >= vertex_count) continue;
    const uint8_t* a = vertices + va * stride;
    const uint8_t* b = vertices + vb * stride;
    float pa[2];
    float pb[2];
    memcpy(pa, a, 8);
    memcpy(pb, b, 8);
    float corners[8];
    if (IsPoints(type)) {
      PointCorners(pa[0], pa[1], corners);
    } else {
      LineCorners(pa[0], pa[1], pb[0], pb[1], corners);
    }
    // Corner 0 and 1 take the vertex a, corner 2 and 3 the vertex b (the
    // same vertex for a point). Triangles 0 1 2 and 0 2 3.
    static const int kCorner[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; ++k) {
      const int c = kCorner[k];
      uint8_t* v = out + (made + k) * stride;
      memcpy(v, c < 2 ? a : b, stride);
      memcpy(v, &corners[2 * c], 8);
    }
    made += 6;
  }
  return made;
}

}  // namespace vk_prim_math
