// THE QUEUE OF THE RENDER THREAD, the pure part (render_thread.cpp, and
// docs/backend.md, "The render thread"). No Windows, no game. The checks are
// in test/controller_math/render_thread_checks.h.
//
// One thread writes records, one thread reads them, in the order of the
// writes. The queue is an array of bytes of a size that is a power of two.
// `write` and `read` count bytes from the start. They are 32 bit counts that
// go past 2^32 and start at 0 again, so each difference is taken modulo 2^32.
// The place in the array is the count modulo the size. A record never wraps:
// when it does not fit before the end of the array, the writer puts a wrap
// record there and the record starts at the start of the array. A record is a
// multiple of 8 bytes.

#pragma once

#include <stdint.h>

namespace render_ring {

const uint32_t kAlign = 8;

// The bytes of a record of `payload` bytes with its header of 8 bytes.
inline uint32_t RecordBytes(uint32_t payload) {
  return (payload + 8 + kAlign - 1) & ~(kAlign - 1);
}

// The bytes in the queue that the reader has not read.
inline uint32_t Used(uint32_t write, uint32_t read) { return write - read; }

// The bytes that the writer can use now.
inline uint32_t Free(uint32_t write, uint32_t read, uint32_t size) {
  return size - Used(write, read);
}

// True when the writer has `need` bytes free. `read` can be an old count of
// the reader: then the free bytes are fewer than now, never more. A count so
// old that more than the whole array seems used gives false too.
inline bool HasRoom(uint32_t write, uint32_t read, uint32_t size,
                    uint32_t need) {
  const uint32_t used = Used(write, read);
  return used <= size && size - used >= need;
}

// The bytes from the place of `write` to the end of the array.
inline uint32_t ToEnd(uint32_t write, uint32_t size) {
  return size - (write & (size - 1));
}

// The plan for a record of `bytes` bytes: whether it needs a wrap record
// first, and the bytes that the writer needs free for both. False when the
// record can never fit.
struct Plan {
  bool wrap;
  uint32_t wrap_bytes;  // the bytes of the wrap record, or 0
  uint32_t need;        // the free bytes needed for the wrap and the record
};

inline bool PlanRecord(uint32_t write, uint32_t size, uint32_t bytes,
                       Plan* plan) {
  if (bytes > size / 2) return false;
  const uint32_t to_end = ToEnd(write, size);
  plan->wrap = bytes > to_end;
  plan->wrap_bytes = plan->wrap ? to_end : 0;
  plan->need = plan->wrap_bytes + bytes;
  return true;
}

}  // namespace render_ring
