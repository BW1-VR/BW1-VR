// The draws of points and lines as triangles, a test (vk_prim_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/vk_prim_math.h"

namespace vk_prim_checks {

inline void VkPrimChecks() {
  using namespace vk_prim_math;
  printf("\nThe draws of points and lines as triangles for the Vulkan "
         "renderer\n");

  // The strides of the formats of the game and of some others.
  Check(FvfStride(0x1C4) == 32, "XYZRHW DIFFUSE SPECULAR TEX1 is 32 bytes",
        FvfStride(0x1C4), 32);
  Check(FvfStride(0x044) == 20, "XYZRHW DIFFUSE is 20 bytes",
        FvfStride(0x044), 20);
  Check(FvfStride(0x112) == 32, "XYZ NORMAL TEX1 is 32 bytes",
        FvfStride(0x112), 32);
  Check(FvfStride(0x10144) == 32,
        "XYZRHW DIFFUSE TEX1 with 3 coordinates is 32 bytes",
        FvfStride(0x10144), 32);
  Check(FvfStride(0x001) == 0, "a format with no position is 0",
        FvfStride(0x001), 0);

  // The counts.
  Check(PrimCount(kPointList, 7) == 7, "7 points", PrimCount(kPointList, 7),
        7);
  Check(PrimCount(kLineList, 7) == 3, "a line list of 7 has 3 lines",
        PrimCount(kLineList, 7), 3);
  Check(PrimCount(kLineStrip, 7) == 6, "a line strip of 7 has 6 lines",
        PrimCount(kLineStrip, 7), 6);
  Check(PrimCount(kLineStrip, 1) == 0, "a line strip of 1 has no line",
        PrimCount(kLineStrip, 1), 0);
  Check(PrimCount(4, 9) == 0, "a list of triangles is not points or lines",
        PrimCount(4, 9), 0);

  // A point is a square of 1 by 1 with its centre at the point.
  float c[8];
  PointCorners(10.0f, 20.0f, c);
  Near("point: corner 0 x", c[0], 9.5, 1e-6);
  Near("point: corner 0 y", c[1], 19.5, 1e-6);
  Near("point: corner 2 x", c[4], 10.5, 1e-6);
  Near("point: corner 2 y", c[5], 20.5, 1e-6);

  // A line across is one pixel across, at a right angle to the line.
  LineCorners(0.0f, 5.0f, 100.0f, 5.0f, c);
  Near("line across: corner 0 y is half a pixel down", c[1], 5.5, 1e-5);
  Near("line across: corner 1 y is half a pixel up", c[3], 4.5, 1e-5);
  Near("line across: corner 0 x is at a", c[0], 0.0, 1e-5);
  Near("line across: corner 2 x is at b", c[4], 100.0, 1e-5);
  LineCorners(3.0f, 3.0f, 3003.0f, 4003.0f, c);
  {
    const double dx = c[0] - c[2];
    const double dy = c[1] - c[3];
    Near("a long slant line is one pixel across", sqrt(dx * dx + dy * dy),
         1.0, 1e-4);
    Near("its width is at a right angle to it", dx * 3000.0 + dy * 4000.0,
         0.0, 1e-2);
  }
  LineCorners(7.0f, 8.0f, 7.0f, 8.0f, c);
  Near("a line of length 0 is a point: corner 0 x", c[0], 6.5, 1e-6);

  // Expand: the corners take the vertex of their end, and only x and y
  // change.
  struct Vertex {
    float x, y, z, rhw;
    uint32_t diffuse, specular;
    float u, v;
  };
  const Vertex in[3] = {{0, 0, 0.25f, 1, 0xFF0000FFu, 0, 0.1f, 0.2f},
                        {50, 0, 0.5f, 1, 0xFF00FF00u, 0, 0.3f, 0.4f},
                        {50, 50, 0.75f, 1, 0xFFFF0000u, 0, 0.5f, 0.6f}};
  Vertex out[24];
  uint32_t made = Expand(kLineStrip, reinterpret_cast<const uint8_t*>(in), 3,
                         nullptr, 0, 32, reinterpret_cast<uint8_t*>(out), 24);
  Check(made == 12, "a line strip of 3 vertices gives 12 vertices", made, 12);
  Check(out[0].diffuse == in[0].diffuse && out[2].diffuse == in[1].diffuse,
        "line 0: corner 0 has the color of a, corner 2 the color of b", 0, 0);
  Near("line 0: corner 2 has the z of b", out[2].z, 0.5, 1e-6);
  Near("line 0: corner 2 has the u of b", out[2].u, 0.3, 1e-6);
  Check(out[6].diffuse == in[1].diffuse && out[8].diffuse == in[2].diffuse,
        "line 1 goes from vertex 1 to vertex 2", 0, 0);
  const uint16_t indices[4] = {2, 0, 1, 9};
  made = Expand(kPointList, reinterpret_cast<const uint8_t*>(in), 3, indices,
                4, 32, reinterpret_cast<uint8_t*>(out), 24);
  Check(made == 18, "3 points with indices give 18 vertices, and the index "
        "past the vertices is left out", made, 18);
  Near("point 0 is vertex 2: x", out[0].x, 49.5, 1e-6);
  made = Expand(kPointList, reinterpret_cast<const uint8_t*>(in), 3, nullptr,
                0, 32, reinterpret_cast<uint8_t*>(out), 12);
  Check(made == 12, "the room of the out buffer is kept", made, 12);
}

}  // namespace vk_prim_checks
