// The queue of the render thread, a test (render_thread_ring.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/render_thread_ring.h"

namespace render_thread_checks {

inline void RenderThreadChecks() {
  using namespace render_ring;
  printf("\nThe queue of the render thread\n");

  Near("a record of 0 bytes is its header of 8 bytes", RecordBytes(0), 8, 0);
  Near("a record of 1 byte is 16 bytes", RecordBytes(1), 16, 0);
  Near("a record of 8 bytes is 16 bytes", RecordBytes(8), 16, 0);
  Near("a record of 9 bytes is 24 bytes", RecordBytes(9), 24, 0);

  const uint32_t size = 1024;
  Near("an empty queue has all its bytes free", Free(0, 0, size), 1024, 0);
  Near("a queue with 1000 bytes written and 600 read has 624 free",
       Free(1000, 600, size), 624, 0);
  Near("the counts can pass the size: 5000 written and 4500 read",
       Free(5000, 4500, size), 524, 0);
  Near("the count of the writer can pass 2^32 before the count of the reader",
       Used(0x00000100u, 0xFFFFFF00u), 512, 0);
  Near("and the free bytes are right then", Free(0x00000100u, 0xFFFFFF00u, size),
       512, 0);
  Check(HasRoom(1000, 600, size, 624) && !HasRoom(1000, 600, size, 625),
        "624 free bytes are room for 624 and not for 625", 0, 0);
  Check(!HasRoom(5000, 100, size, 8),
        "an old count of the reader that seems to leave less than nothing "
        "gives no room",
        0, 0);
  Near("at 1000 there are 24 bytes to the end", ToEnd(1000, size), 24, 0);
  Near("at 1024 the place is the start again", ToEnd(1024, size), 1024, 0);

  Plan plan;
  Check(PlanRecord(1000, size, 16, &plan) && !plan.wrap && plan.need == 16,
        "a record that fits before the end needs no wrap", 0, 0);
  Check(PlanRecord(1000, size, 32, &plan) && plan.wrap &&
            plan.wrap_bytes == 24 && plan.need == 56,
        "a record that does not fit wraps, and needs the rest of the array too",
        0, 0);
  Check(PlanRecord(1000, size, 24, &plan) && !plan.wrap,
        "a record that ends at the end of the array needs no wrap", 0, 0);
  Check(!PlanRecord(0, size, 600, &plan),
        "a record of more than half the array is refused", 0, 0);
}

}  // namespace render_thread_checks
