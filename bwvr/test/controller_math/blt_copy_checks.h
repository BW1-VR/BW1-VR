// The copy of the mod between two surfaces, a test (blt_copy_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/blt_copy_math.h"

namespace blt_copy_checks {

inline void BltCopyChecks() {
  using namespace blt_copy;
  printf("\nThe copy of the mod between two surfaces\n");

  const Format r5g6b5 = {0xF800, 0x07E0, 0x001F, 0, 2};
  const Format a1r5g5b5 = {0x7C00, 0x03E0, 0x001F, 0x8000, 2};
  const Format a4r4g4b4 = {0x0F00, 0x00F0, 0x000F, 0xF000, 2};
  const Format x8r8g8b8 = {0xFF0000, 0x00FF00, 0x0000FF, 0, 4};
  const Format a8r8g8b8 = {0xFF0000, 0x00FF00, 0x0000FF, 0xFF000000u, 4};
  const Format r8g8b8 = {0xFF0000, 0x00FF00, 0x0000FF, 0, 3};

  Near("the shift of the green of 565 is 5", ShiftOf(0x07E0), 5, 0);
  Near("the top of the green of 565 is 63", TopOf(0x07E0), 63, 0);
  Near("the full red of 565 is 255", Get8(0xF800, 0xF800), 255, 0);
  Near("255 in the red of 565 is 0xF800", Put8(255, 0xF800), 0xF800, 0);
  Near("0 stays 0", Put8(0, 0xF800), 0, 0);

  Near("white 565 to A8R8G8B8 is opaque white",
       Convert(0xFFFF, r5g6b5, a8r8g8b8), 0xFFFFFFFFu, 0);
  Near("red 565 to A8R8G8B8 is opaque red", Convert(0xF800, r5g6b5, a8r8g8b8),
       0xFFFF0000u, 0);
  Near("pure blue X8R8G8B8 to 565 is 0x001F",
       Convert(0x000000FF, x8r8g8b8, r5g6b5), 0x001F, 0);
  Near("A1R5G5B5 with no alpha bit gives alpha 0 in A8R8G8B8",
       Convert(0x7C00, a1r5g5b5, a8r8g8b8) >> 24, 0, 0);
  Near("A1R5G5B5 with the alpha bit gives alpha 255",
       Convert(0xFC00, a1r5g5b5, a8r8g8b8) >> 24, 255, 0);
  Near("half alpha A8R8G8B8 to A4R4G4B4 is 8 of 15",
       Convert(0x80000000u, a8r8g8b8, a4r4g4b4) >> 12, 8, 0);
  Near("a gray of 128 in 565 and back is within one step of 5 bits",
       Get8(Convert(0x808080, x8r8g8b8, r5g6b5), 0xF800), 128, 5);

  Check(Usable(r8g8b8) && !Usable(Format{0, 0, 0, 0, 2}),
        "a format with no red is not usable", 0, 0);

  // A source of 4 by 3 pixels of 565 in rows of 10 bytes, and the copy of
  // its bottom right 2 by 2 into a destination of A8R8G8B8.
  {
    uint8_t in[3 * 10];
    memset(in, 0, sizeof(in));
    Write(in + 1 * 10 + 2 * 2, 2, 0xF800);  // (2, 1) red
    Write(in + 2 * 10 + 3 * 2, 2, 0x001F);  // (3, 2) blue
    uint8_t out[4 * 16];
    memset(out, 0xCD, sizeof(out));
    Check(CopyBlock(in, 10, 2, 1, r5g6b5, out, 16, 1, 2, a8r8g8b8, 2, 2),
          "a copy between two formats is made", 0, 0);
    Near("the red pixel is at (1, 2)", Read(out + 2 * 16 + 1 * 4, 4),
         0xFFFF0000u, 0);
    Near("the blue pixel is at (2, 3)", Read(out + 3 * 16 + 2 * 4, 4),
         0xFF0000FFu, 0);
    Near("a pixel outside the block keeps its value",
         Read(out + 2 * 16 + 0 * 4, 4), 0xCDCDCDCDu, 0);
  }

  // A copy with one format copies the bytes.
  {
    uint8_t in[2 * 6] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    uint8_t out[2 * 6];
    memset(out, 0, sizeof(out));
    Check(CopyBlock(in, 6, 0, 0, r8g8b8, out, 6, 0, 0, r8g8b8, 2, 2),
          "a copy of 24 bit pixels with one format is made", 0, 0);
    Check(memcmp(in, out, sizeof(in)) == 0, "and it has the same bytes", 0,
          0);
  }

  // Compressed blocks.
  const uint32_t dxt1 = 'D' | ('X' << 8) | ('T' << 16) | ('1' << 24);
  const uint32_t dxt5 = 'D' | ('X' << 8) | ('T' << 16) | ('5' << 24);
  Near("a block of DXT1 has 8 bytes", BlockBytes(dxt1), 8, 0);
  Near("a block of DXT5 has 16 bytes", BlockBytes(dxt5), 16, 0);
  Near("an other code has no blocks",
       BlockBytes('Y' | ('U' << 8) | ('Y' << 16) | ('2' << 24)), 0, 0);
  Near("a DXT1 surface 256 wide has rows of 512 bytes",
       BlockPitch(512, 256, 8), 512, 0);
  Near("a pitch with room at the end is kept", BlockPitch(1024, 256, 8), 1024,
       0);
  Near("a pitch that is the size of the surface is not a row",
       BlockPitch(32768, 256, 8), 512, 0);
  Near("a pitch of pixels that is too small is not a row",
       BlockPitch(256, 256, 8), 512, 0);
  Check(OnBlocks(0, 128, 128, 192, 256, 256),
        "the rect 0 128 128 192 of the save picture is on the blocks", 0, 0);
  Check(!OnBlocks(0, 130, 128, 192, 256, 256),
        "a rect from row 130 is not on the blocks", 0, 0);
  Check(OnBlocks(0, 0, 6, 6, 6, 6),
        "a rect to the edge of a surface of 6 by 6 is on the blocks", 0, 0);

  // A DXT1 surface of 16 by 16 pixels: 4 by 4 blocks, rows of 32 bytes.
  // The copy of the rect 0 8 8 16 puts the blocks (0, 2) to (1, 3) of the
  // source at the same blocks of the destination.
  {
    uint8_t in[4 * 32];
    for (int i = 0; i < 4 * 32; ++i) in[i] = static_cast<uint8_t>(i);
    uint8_t out[4 * 32];
    memset(out, 0, sizeof(out));
    Check(CopyBlocks(in, 32, 0, 8, out, 32, 0, 8, 8, 8, 8),
          "a copy of the blocks of a rect is made", 0, 0);
    Check(memcmp(out + 2 * 32, in + 2 * 32, 16) == 0 &&
              memcmp(out + 3 * 32, in + 3 * 32, 16) == 0,
          "the blocks of the rows 8 to 16 are copied", 0, 0);
    Check(out[0] == 0 && out[2 * 32 + 16] == 0,
          "a block outside the rect keeps its value", 0, 0);
    Check(!CopyBlocks(in, 32, 0, 6, out, 32, 0, 6, 8, 8, 8),
          "a rect from row 6 is refused", 0, 0);
    Check(!CopyBlocks(in, 16, 0, 0, out, 32, 0, 0, 16, 16, 8),
          "a row longer than the pitch of the source is refused", 0, 0);
  }

  // A row that is longer than the pitch is refused, and nothing is written.
  {
    uint8_t in[64];
    uint8_t out[64];
    memset(in, 7, sizeof(in));
    memset(out, 0, sizeof(out));
    Check(!CopyBlock(in, 8, 0, 0, r5g6b5, out, 8, 1, 0, r5g6b5, 4, 2),
          "a block past the end of a row of the destination is refused", 0,
          0);
    Check(out[0] == 0 && out[63] == 0, "and nothing is written", 0, 0);
  }
}

}  // namespace blt_copy_checks
