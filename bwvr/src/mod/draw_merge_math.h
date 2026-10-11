// The merge of draw calls (draw_merge.cpp): the batch that takes a row of
// draw calls and gives them back as one. No game, no Direct3D: the math
// checks run this file (test/controller_math/draw_merge_checks.h).

#pragma once

#include <stdint.h>
#include <string.h>

namespace draw_merge {

// The limits of one draw call of Direct3D 7. An index has 16 bits, and a
// draw call has 65,535 triangles at the most.
const uint32_t kMaxVertices = 65535;
const uint32_t kMaxIndices = 3 * 65535;
// The largest vertex that the merge takes: a place on the screen, two colors
// and 8 texture coordinate sets.
const uint32_t kMaxStride = 16 + 4 + 4 + 8 * 8;

// The bytes of one vertex with a place on the screen (D3DFVF_XYZRHW). 0 for a
// vertex format that the merge does not take: Direct3D transforms such a
// vertex, or the format has a part that this file does not know.
inline uint32_t Stride(uint32_t fvf) {
  const uint32_t kPosition = 0x00E;
  const uint32_t kXyzRhw = 0x004;
  const uint32_t kDiffuse = 0x040;
  const uint32_t kSpecular = 0x080;
  const uint32_t kTextures = 0xF00;
  if ((fvf & kPosition) != kXyzRhw) return 0;
  // A normal, a reserved part, or a texture coordinate set that has not two
  // numbers.
  if ((fvf & ~(kPosition | kDiffuse | kSpecular | kTextures)) != 0) return 0;
  uint32_t size = 16;
  if ((fvf & kDiffuse) != 0) size += 4;
  if ((fvf & kSpecular) != 0) size += 4;
  size += 8 * ((fvf & kTextures) >> 8);
  return size;
}

// The draw calls that wait. The caller gives the two arrays: `vertices` has
// room for kMaxVertices vertices of kMaxStride bytes, and `indices` has room
// for kMaxIndices values.
struct Batch {
  uint8_t* vertices;
  uint16_t* indices;
  uint32_t stride;  // the bytes of one vertex of the batch
  uint32_t vertex_count;
  uint32_t index_count;
  uint32_t draws;   // the draw calls in the batch
};

inline void Clear(Batch* batch) {
  batch->stride = 0;
  batch->vertex_count = 0;
  batch->index_count = 0;
  batch->draws = 0;
}

// The indices of a triangle list that Direct3D uses: it does not draw a last
// triangle with one or two corners. The merge must drop them too, because
// the next draw call of the batch starts at a new triangle.
inline uint32_t WholeTriangles(uint32_t count) { return count - count % 3; }

// True when a draw call with these counts can go into the batch after the
// draw calls that it holds.
inline bool Fits(const Batch& batch, uint32_t stride, uint32_t vertices,
                 uint32_t indices) {
  if (stride == 0 || stride > kMaxStride) return false;
  if (batch.draws > 0 && stride != batch.stride) return false;
  return batch.vertex_count + vertices <= kMaxVertices &&
         batch.index_count + indices <= kMaxIndices;
}

// Adds a draw call with indices. It copies the vertices, and each index gets
// the place of the first new vertex added to it. `index_count` is a count of
// whole triangles. False, and the batch does not change, when the draw call
// does not fit or when an index is not the index of one of its vertices.
inline bool AddIndexed(Batch* batch, uint32_t stride, const void* vertices,
                       uint32_t vertex_count, const uint16_t* indices,
                       uint32_t index_count) {
  if (!Fits(*batch, stride, vertex_count, index_count)) return false;
  const uint32_t base = batch->vertex_count;
  uint16_t* out = batch->indices + batch->index_count;
  for (uint32_t i = 0; i < index_count; ++i) {
    if (indices[i] >= vertex_count) return false;
    out[i] = static_cast<uint16_t>(base + indices[i]);
  }
  memcpy(batch->vertices + static_cast<size_t>(base) * stride, vertices,
         static_cast<size_t>(vertex_count) * stride);
  batch->stride = stride;
  batch->vertex_count += vertex_count;
  batch->index_count += index_count;
  ++batch->draws;
  return true;
}

// Adds a draw call with no indices: a triangle list of `vertex_count`
// vertices in their order, a count of whole triangles. False, and the batch
// does not change, when the draw call does not fit.
inline bool AddList(Batch* batch, uint32_t stride, const void* vertices,
                    uint32_t vertex_count) {
  if (!Fits(*batch, stride, vertex_count, vertex_count)) return false;
  const uint32_t base = batch->vertex_count;
  uint16_t* out = batch->indices + batch->index_count;
  for (uint32_t i = 0; i < vertex_count; ++i) {
    out[i] = static_cast<uint16_t>(base + i);
  }
  memcpy(batch->vertices + static_cast<size_t>(base) * stride, vertices,
         static_cast<size_t>(vertex_count) * stride);
  batch->stride = stride;
  batch->vertex_count += vertex_count;
  batch->index_count += vertex_count;
  ++batch->draws;
  return true;
}

// THE CHECK OF THE TRIANGLES. A checksum of the corners of a row of
// triangles, in their order: each corner is the bytes of its vertex. The
// triangles that the game gave and the triangles that went out have the same
// checksum when the merge changed no triangle and no order. `hash` is the
// checksum so far. `indices` is null for vertices in their order. A stride
// is a count of whole words of 4 bytes.
inline uint64_t HashTriangles(uint64_t hash, const void* vertices,
                              uint32_t stride, const uint16_t* indices,
                              uint32_t count) {
  const uint64_t kPrime = 0x100000001B3ull;
  const uint8_t* bytes = static_cast<const uint8_t*>(vertices);
  const uint32_t words = stride / 4;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t vertex = indices != nullptr ? indices[i] : i;
    const uint8_t* at = bytes + static_cast<size_t>(vertex) * stride;
    for (uint32_t w = 0; w < words; ++w) {
      uint32_t word;
      memcpy(&word, at + w * 4, sizeof(word));
      hash = (hash ^ word) * kPrime;
    }
  }
  return hash;
}

const uint64_t kHashStart = 0xCBF29CE484222325ull;

}  // namespace draw_merge
