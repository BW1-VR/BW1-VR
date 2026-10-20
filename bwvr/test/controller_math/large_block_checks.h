// The table of the large blocks (large_block_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/large_block_math.h"

namespace large_block_checks {

inline void LargeBlockChecks() {
  using namespace large_block;
  printf("\nThe table of the large blocks\n");
  static Blocks b;
  for (int i = 0; i < kSlots; ++i) b.key[i] = kEmpty;

  Check(Insert(&b, 0x10000020u, 300000u, 3), "a block goes in", 0, 0);
  const int slot = Find(b, 0x10000020u);
  Check(slot >= 0, "the block is found", slot, 0);
  Near("its size", b.size[slot], 300000, 0);
  Near("its place", b.site[slot], 3, 0);
  Near("an address that is not in the table is not found",
       Find(b, 0x20000020u), -1, 0);
  Near("the null address is never found", Find(b, 0), -1, 0);

  // Keys with the same slot follow each other, and a free in the middle
  // does not hide the one after it.
  const uint32_t a1 = 0x00100000u;
  uint32_t a2 = 0, a3 = 0;
  for (uint32_t k = a1 + 0x10; a3 == 0; k += 0x10) {
    if (SlotOf(k) != SlotOf(a1)) continue;
    if (a2 == 0) {
      a2 = k;
    } else {
      a3 = k;
    }
  }
  Check(Insert(&b, a1, 1, 1) && Insert(&b, a2, 2, 2) && Insert(&b, a3, 3, 3),
        "three blocks with one slot go in", 0, 0);
  Erase(&b, Find(b, a2));
  Near("the freed block is not found", Find(b, a2), -1, 0);
  Check(Find(b, a3) >= 0, "the block after the freed one is still found", 0,
        0);
  const int before = Find(b, a3);
  Check(Insert(&b, a2, 9, 4), "a new block takes the freed slot", 0, 0);
  Check(Find(b, a2) >= 0 && Find(b, a2) < before,
        "the new block is in the freed slot, before the third", 0, 0);
  Near("an insert of a key that is there changes its size",
       (Insert(&b, a2, 11, 4), b.size[Find(b, a2)]), 11, 0);

  // The places.
  static Sites s;
  s.count = 0;
  const uint32_t f1[kFrames] = {0x5D700100u, 0x5D700200u, 0x00500000u, 0};
  const uint32_t f2[kFrames] = {0x5D700100u, 0x5D700300u, 0x00500000u, 0};
  const int p1 = SiteOf(&s, f1, kHeap);
  const int p2 = SiteOf(&s, f2, kHeap);
  const int p3 = SiteOf(&s, f1, kVirtual);
  Check(p1 == 0 && p2 == 1 && p3 == 2,
        "other frames or another kind give a new place", 0, 0);
  Near("the same frames and kind give the same place", SiteOf(&s, f1, kHeap),
       0, 0);
  Made(&s.site[p1], 2000000u);
  Made(&s.site[p1], 2000000u);
  Made(&s.site[p2], 5000000u);
  Made(&s.site[p3], 1000000u);
  Freed(&s.site[p1], 2000000u);
  Near("a place counts its live bytes",
       static_cast<double>(s.site[p1].live_bytes), 2000000, 0);
  Near("a place counts its live blocks", s.site[p1].live_count, 1, 0);
  Near("a place counts each block that it made", s.site[p1].made_count, 2, 0);
  Freed(&s.site[p3], 3000000u);
  Near("a free of more than the live bytes gives zero",
       static_cast<double>(s.site[p3].live_bytes), 0, 0);

  int top[2];
  Near("a place with no live bytes is not in the top", Top(s, top, 2), 2, 0);
  Check(top[0] == p2 && top[1] == p1, "the top has the most bytes first", 0,
        0);
  int one[1];
  Near("a top of one", Top(s, one, 1), 1, 0);
  Near("a top of one has the largest", one[0], p2, 0);

  // The places of a heap: another heap gives another place.
  const uint32_t none[kFrames] = {0, 0, 0, 0};
  const int h1 = SiteOf(&s, none, kSegment, 0x00A40000u);
  const int h2 = SiteOf(&s, none, kSegment, 0x00B50000u);
  Check(h1 != h2, "another heap gives another place", h1, h2);
  Near("the same heap gives the same place",
       SiteOf(&s, none, kSegment, 0x00A40000u), h1, 0);

  // The growth since the line before.
  const int g1 = SiteOf(&s, f1, kGrowth, 0x00A40000u);
  const int g2 = SiteOf(&s, f2, kGrowth, 0x00A40000u);
  const int g3 = SiteOf(&s, f2, kGrowth, 0x00B50000u);
  s.site[g1].live_bytes = 10;
  s.site[g1].bytes_at_line = 8;
  s.site[g2].live_bytes = 30;
  s.site[g2].bytes_at_line = 5;
  s.site[g3].live_bytes = 4;
  s.site[g3].bytes_at_line = 4;
  int grew[4];
  Near("only places that grew are in the top of the growth",
       TopGrowth(s, kGrowth, grew, 4), 2, 0);
  Check(grew[0] == g2 && grew[1] == g1, "the top of the growth has the most "
        "growth first", 0, 0);
  Near("a top of the growth of one has the most",
       (TopGrowth(s, kGrowth, grew, 1), grew[0]), g2, 0);
  Near("the blocks of another kind are not in the top of the growth",
       TopGrowth(s, kSegment, grew, 4), 0, 0);

  // The heaps.
  static Heaps heaps;
  heaps.count = 0;
  const int a = HeapOf(&heaps, 0x00A40000u);
  const int b2 = HeapOf(&heaps, 0x00B50000u);
  Check(a == 0 && b2 == 1, "each heap has its index", a, b2);
  Near("a heap that is there keeps its index", HeapOf(&heaps, 0x00A40000u), 0,
       0);
}

}  // namespace large_block_checks
