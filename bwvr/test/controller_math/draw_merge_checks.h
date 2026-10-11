// The batch of the merge of draw calls (draw_merge_math.h). main.cpp
// includes this file after Check and Near, just before main.
#pragma once

#include <stdlib.h>

#include "../../src/mod/draw_merge_math.h"

namespace draw_merge_checks {

// A vertex of the game: a place on the screen, two colors and one texture
// coordinate set. 32 bytes.
struct Vertex {
  float x, y, z, rhw;
  uint32_t diffuse, specular;
  float u, v;
};

inline Vertex Corner(float x, float y) {
  Vertex vertex = {x, y, 0.5f, 1.0f, 0xFF102030u, 0xFF000000u, x * 0.01f,
                   y * 0.01f};
  return vertex;
}

inline void DrawMergeChecks() {
  using namespace draw_merge;
  printf("The batch of the merge of draw calls\n");

  // The vertex formats.
  Check(Stride(0x1C4) == 32, "the vertex of the game has 32 bytes",
        Stride(0x1C4), 32);
  Check(Stride(0x144) == 28, "a vertex with one color has 28 bytes",
        Stride(0x144), 28);
  Check(Stride(0x2C4) == 40, "two texture coordinate sets make 40 bytes",
        Stride(0x2C4), 40);
  Check(Stride(0x142) == 0, "a vertex that Direct3D transforms is not taken",
        Stride(0x142), 0);
  Check(Stride(0x1D4) == 0, "a vertex with a normal is not taken",
        Stride(0x1D4), 0);
  Check(Stride(0x101C4) == 0,
        "a texture coordinate set with one number is not taken",
        Stride(0x101C4), 0);

  Batch batch = {};
  batch.vertices = static_cast<uint8_t*>(malloc(kMaxVertices * kMaxStride));
  batch.indices =
      static_cast<uint16_t*>(malloc(kMaxIndices * sizeof(uint16_t)));
  if (batch.vertices == nullptr || batch.indices == nullptr) {
    Check(false, "the arrays of the batch", 0, 1);
    return;
  }
  Clear(&batch);

  // Two draw calls with indices: a square of two triangles, then a triangle.
  const Vertex square[4] = {Corner(0, 0), Corner(10, 0), Corner(10, 10),
                            Corner(0, 10)};
  const uint16_t square_indices[6] = {0, 1, 2, 0, 2, 3};
  const Vertex triangle[3] = {Corner(20, 0), Corner(30, 0), Corner(20, 10)};
  const uint16_t triangle_indices[3] = {2, 1, 0};
  Check(AddIndexed(&batch, 32, square, 4, square_indices, 6),
        "the first draw call goes into an empty batch", 0, 0);
  Check(AddIndexed(&batch, 32, triangle, 3, triangle_indices, 3),
        "the second draw call goes in after it", 0, 0);
  Check(batch.draws == 2 && batch.vertex_count == 7 && batch.index_count == 9,
        "the batch has 2 draw calls, 7 vertices and 9 indices",
        batch.vertex_count, 7);
  Check(batch.indices[0] == 0 && batch.indices[5] == 3,
        "the indices of the first draw call stay", batch.indices[5], 3);
  Check(batch.indices[6] == 6 && batch.indices[7] == 5 &&
            batch.indices[8] == 4,
        "each index of the second draw call gets the 4 vertices before it",
        batch.indices[6], 6);
  Check(memcmp(batch.vertices, square, sizeof(square)) == 0 &&
            memcmp(batch.vertices + sizeof(square), triangle,
                   sizeof(triangle)) == 0,
        "the vertices are the vertices of both draw calls, in their order", 0,
        0);

  // The triangles in and the triangles out are the same triangles.
  uint64_t in = HashTriangles(kHashStart, square, 32, square_indices, 6);
  in = HashTriangles(in, triangle, 32, triangle_indices, 3);
  const uint64_t out = HashTriangles(kHashStart, batch.vertices, batch.stride,
                                     batch.indices, batch.index_count);
  Check(in == out, "the checksum of the batch is that of the two draw calls",
        static_cast<double>(out & 0xFFFF), static_cast<double>(in & 0xFFFF));
  uint64_t swapped = HashTriangles(kHashStart, triangle, 32, triangle_indices,
                                   3);
  swapped = HashTriangles(swapped, square, 32, square_indices, 6);
  Check(swapped != in, "a different order gives a different checksum", 0, 0);
  Vertex moved[3] = {triangle[0], triangle[1], triangle[2]};
  moved[1].x += 1.0f;
  uint64_t other = HashTriangles(kHashStart, square, 32, square_indices, 6);
  other = HashTriangles(other, moved, 32, triangle_indices, 3);
  Check(other != in, "a vertex that moves gives a different checksum", 0, 0);

  // A draw call with no indices: its vertices in their order.
  Clear(&batch);
  Check(AddList(&batch, 32, triangle, 3) && AddList(&batch, 32, triangle, 3),
        "two draw calls with no indices go into the batch", 0, 0);
  Check(batch.index_count == 6 && batch.indices[3] == 3 &&
            batch.indices[5] == 5,
        "the second one gets the indices 3, 4 and 5", batch.indices[5], 5);
  Check(WholeTriangles(7) == 6 && WholeTriangles(6) == 6 &&
            WholeTriangles(2) == 0,
        "a last triangle with one or two corners is dropped",
        WholeTriangles(7), 6);
  uint64_t list_in = HashTriangles(kHashStart, triangle, 32, nullptr, 3);
  list_in = HashTriangles(list_in, triangle, 32, nullptr, 3);
  Check(list_in == HashTriangles(kHashStart, batch.vertices, 32,
                                 batch.indices, 6),
        "the checksum of the batch is that of the two lists", 0, 0);

  // What the batch refuses.
  Clear(&batch);
  AddIndexed(&batch, 32, square, 4, square_indices, 6);
  const uint16_t bad_indices[3] = {0, 1, 3};
  Check(!AddIndexed(&batch, 32, triangle, 3, bad_indices, 3),
        "an index past the vertices of its draw call is refused", 0, 0);
  Check(batch.draws == 1 && batch.vertex_count == 4 && batch.index_count == 6,
        "and the batch does not change", batch.vertex_count, 4);
  Check(!Fits(batch, 28, 3, 3), "a different vertex size does not fit", 0, 0);
  Check(Fits(batch, 32, kMaxVertices - 4, 3) &&
            !Fits(batch, 32, kMaxVertices - 3, 3),
        "the batch has 65,535 vertices at the most", 0, 0);
  Check(Fits(batch, 32, 3, kMaxIndices - 6) &&
            !Fits(batch, 32, 3, kMaxIndices - 5),
        "and 196,605 indices at the most", 0, 0);
  Clear(&batch);
  Check(!Fits(batch, 0, 3, 3) && !Fits(batch, kMaxStride + 4, 3, 3),
        "a vertex size of 0 or over the limit does not fit", 0, 0);
  Check(Fits(batch, 28, 3, 3), "an empty batch takes each vertex size", 0, 0);

  free(batch.vertices);
  free(batch.indices);
}

}  // namespace draw_merge_checks
