// The pixels of a texture of the game as RGBA8, a test (vk_texture_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <stdlib.h>

#include "../../src/mod/vk_texture_math.h"

namespace vk_texture_checks {

inline void VkTextureChecks() {
  using namespace vk_texture_math;
  printf("\nThe pixels of a texture as RGBA8 for the Vulkan renderer\n");

  // The formats of the game and of DirectDraw with 16 bits: each value of
  // the table is the value of Expand.
  const Format formats16[] = {
      {0x0F00u, 0x00F0u, 0x000Fu, 0xF000u, true},   // A4R4G4B4
      {0x7C00u, 0x03E0u, 0x001Fu, 0x0000u, false},  // X1R5G5B5
      {0x7C00u, 0x03E0u, 0x001Fu, 0x8000u, true},   // A1R5G5B5
      {0xF800u, 0x07E0u, 0x001Fu, 0x0000u, false},  // R5G6B5
  };
  const char* const names16[] = {"A4R4G4B4", "X1R5G5B5", "A1R5G5B5",
                                 "R5G6B5"};
  uint32_t* lut = static_cast<uint32_t*>(malloc(65536 * sizeof(uint32_t)));
  for (int k = 0; k < 4; ++k) {
    BuildLut16(formats16[k], lut);
    int wrong = 0;
    for (uint32_t p = 0; p < 65536u; ++p) {
      if (lut[p] != ToRgba(p, formats16[k])) ++wrong;
    }
    char what[96];
    snprintf(what, sizeof(what),
             "%s: each of the 65536 values of the table is Expand",
             names16[k]);
    Check(wrong == 0, what, wrong, 0);
  }
  free(lut);

  // The values of the ends and of the middle.
  const Format a4 = formats16[0];
  Check(ToRgba(0xFFFFu, a4) == 0xFFFFFFFFu,
        "A4R4G4B4 0xFFFF is white with alpha 255", ToRgba(0xFFFFu, a4),
        0xFFFFFFFFu);
  Check(ToRgba(0x0000u, a4) == 0x00000000u,
        "A4R4G4B4 0x0000 is black with alpha 0", ToRgba(0x0000u, a4), 0);
  Check(ToRgba(0x8F00u, a4) == 0x880000FFu,
        "A4R4G4B4 0x8F00 is red with alpha 136 (8 * 17)", ToRgba(0x8F00u, a4),
        0x880000FFu);
  const Format x1 = formats16[1];
  Check((ToRgba(0x0000u, x1) >> 24) == 255u,
        "X1R5G5B5 has alpha 255 with no alpha bits", ToRgba(0x0000u, x1) >> 24,
        255);
  Check((ToRgba(0x7C00u, x1) & 0xFFu) == 255u,
        "X1R5G5B5 0x7C00 has red 255", ToRgba(0x7C00u, x1) & 0xFFu, 255);
  Check(((ToRgba(0x0200u, x1) >> 8) & 0xFFu) == 132u,
        "X1R5G5B5 green 16 of 31 is 132", (ToRgba(0x0200u, x1) >> 8) & 0xFFu,
        132);

  // Formats of 32 and 24 bits: the channels give Expand.
  struct Wide {
    Format f;
    const char* name;
  };
  const Wide wide[] = {
      {{0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u, true}, "A8R8G8B8"},
      {{0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0x00000000u, false},
       "X8R8G8B8"},
      {{0x3FF00000u, 0x000FFC00u, 0x000003FFu, 0xC0000000u, true},
       "A2R10G10B10"},
      {{0x00000F00u, 0x000000F0u, 0x0000000Fu, 0x0000F000u, true},
       "A4R4G4B4 in 32 bits"},
  };
  for (const Wide& w : wide) {
    Channel r, g, b, a;
    BuildChannel(w.f.r, 0, &r);
    BuildChannel(w.f.g, 0, &g);
    BuildChannel(w.f.b, 0, &b);
    BuildChannel(w.f.has_alpha ? w.f.a : 0u, 255, &a);
    int wrong = 0;
    uint32_t p = 0x12345678u;
    for (int i = 0; i < 200000; ++i) {
      p = p * 1664525u + 1013904223u;  // the values of a simple generator
      const uint32_t got =
          ChannelValue(p, r, w.f.r) | ChannelValue(p, g, w.f.g) << 8 |
          ChannelValue(p, b, w.f.b) << 16 |
          ChannelValue(p, a, w.f.has_alpha ? w.f.a : 0u) << 24;
      if (got != ToRgba(p, w.f)) ++wrong;
    }
    char what[96];
    snprintf(what, sizeof(what),
             "%s: 200000 pixels with the channels are Expand", w.name);
    Check(wrong == 0, what, wrong, 0);
  }
}

}  // namespace vk_texture_checks
