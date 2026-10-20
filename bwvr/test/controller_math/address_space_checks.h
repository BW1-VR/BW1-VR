// The sums of the address space (address_space_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/address_space_math.h"

namespace address_space_checks {

inline void AddressSpaceChecks() {
  using namespace address_space;
  printf("\nThe sums of the address space\n");
  const uint64_t kKiB = 1024;
  const uint64_t kMiB = 1024 * 1024;

  Near("a free run on block bounds is usable in full",
       static_cast<double>(UsableBlocks(0x10000, 0x30000)), 0x30000, 0);
  Near("a free run that starts inside a block loses the start",
       static_cast<double>(UsableBlocks(0x11000, 0x30000)), 0x20000, 0);
  Near("a free run of less than one whole block is of no use",
       static_cast<double>(UsableBlocks(0x11000, 0xF000)), 0, 0);

  Summary s;
  Start(&s);
  // An image, a private allocation of three regions, a gap, a mapped view,
  // a second private allocation, and the free end.
  const Region regions[] = {
      {0x00400000, 8 * kMiB, 0x00400000, kCommit, kTypeImage},
      {0x00C00000, 4 * kMiB, 0x00C00000, kCommit, kTypePrivate},
      {0x01000000, 12 * kMiB, 0x00C00000, kReserve, kTypePrivate},
      {0x01C00000, 1 * kMiB, 0x00C00000, kCommit, kTypePrivate},
      {0x01D00000, 3 * kMiB + 4 * kKiB, 0, kFree, 0},
      {0x02001000, 2 * kMiB, 0x02001000, kCommit, kTypeMapped},
      {0x02201000, 6 * kMiB, 0x02201000, kCommit, kTypePrivate},
      {0x02801000, 100 * kMiB, 0, kFree, 0},
  };
  for (const Region& r : regions) Add(&s, r);
  Finish(&s);
  Near("committed image, MiB", static_cast<double>(s.committed[kImage] / kMiB),
       8, 0);
  Near("committed mapped, MiB",
       static_cast<double>(s.committed[kMapped] / kMiB), 2, 0);
  Near("committed private, MiB",
       static_cast<double>(s.committed[kPrivate] / kMiB), 11, 0);
  Near("reserved private, MiB",
       static_cast<double>(s.reserved[kPrivate] / kMiB), 12, 0);
  Near("in use is committed and reserved, MiB",
       static_cast<double>(InUse(s) / kMiB), 33, 0);
  Near("two private allocations", s.private_allocations, 2, 0);
  Near("the largest private allocation has its three regions, MiB",
       static_cast<double>(s.top[0] / kMiB), 17, 0);
  Near("the second private allocation, MiB",
       static_cast<double>(s.top[1] / kMiB), 6, 0);
  Near("no third private allocation", static_cast<double>(s.top[2]), 0, 0);
  Near("two free runs", s.free_runs, 2, 0);
  // The gap ends at 0x02001000: its whole blocks end at 0x02000000.
  Near("the free blocks, KiB",
       static_cast<double>(s.free_usable / kKiB),
       static_cast<double>((3 * kMiB + 100 * kMiB - 64 * kKiB) / kKiB), 0);
  Near("the largest free run, MiB, less the block cut at its start",
       static_cast<double>(s.free_largest / kKiB),
       static_cast<double>((100 * kMiB - 64 * kKiB) / kKiB), 0);

  // Two private allocations that touch each other stay two.
  Start(&s);
  const Region touching[] = {
      {0x10000000, 1 * kMiB, 0x10000000, kCommit, kTypePrivate},
      {0x10100000, 2 * kMiB, 0x10100000, kCommit, kTypePrivate},
  };
  for (const Region& r : touching) Add(&s, r);
  Finish(&s);
  Near("two allocations that touch are two", s.private_allocations, 2, 0);
  Near("the larger one is first, MiB", static_cast<double>(s.top[0] / kMiB), 2,
       0);

  // The search in a rising array.
  const uint32_t sorted[5] = {0x10000, 0x20000, 0x50000, 0x90000, 0xA0000};
  Check(InSorted(sorted, 5, 0x50000), "a value in the middle is found", 0, 0);
  Check(InSorted(sorted, 5, 0x10000) && InSorted(sorted, 5, 0xA0000),
        "the first and the last value are found", 0, 0);
  Check(!InSorted(sorted, 5, 0x30000) && !InSorted(sorted, 5, 0xB0000) &&
            !InSorted(sorted, 0, 0x10000),
        "a value that is not there is not found", 0, 0);

  // The top list keeps the largest four, largest first.
  Start(&s);
  const uint64_t sizes[] = {3, 9, 1, 7, 5, 8};
  for (int i = 0; i < 6; ++i) PutInTop(&s, sizes[i] * kMiB);
  Check(s.top[0] == 9 * kMiB && s.top[1] == 8 * kMiB && s.top[2] == 7 * kMiB &&
            s.top[3] == 5 * kMiB,
        "the top list has 9, 8, 7 and 5 MiB", 0, 0);
}

}  // namespace address_space_checks
