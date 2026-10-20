// The sums of the address space of the process (memory_watch.cpp,
// docs/backend.md, "The address space"). Pure: the caller gives the regions
// of VirtualQuery in the order of their addresses.
#pragma once

#include <stdint.h>

namespace address_space {

// The values of MEMORY_BASIC_INFORMATION.
const uint32_t kCommit = 0x1000;
const uint32_t kReserve = 0x2000;
const uint32_t kFree = 0x10000;
const uint32_t kTypeImage = 0x1000000;
const uint32_t kTypeMapped = 0x40000;
const uint32_t kTypePrivate = 0x20000;
// VirtualAlloc gives memory in blocks of 64 KiB, at addresses that are a
// multiple of 64 KiB. A free part outside such a block is of no use.
const uint64_t kGranularity = 0x10000;
const int kTop = 4;  // the largest private allocations that the line names

enum Kind { kImage = 0, kMapped, kPrivate, kKinds };

struct Region {
  uint64_t base;
  uint64_t size;
  uint64_t allocation_base;
  uint32_t state;
  uint32_t type;
};

struct Summary {
  uint64_t committed[kKinds];
  uint64_t reserved[kKinds];
  uint64_t free_usable;   // the free blocks of 64 KiB
  uint64_t free_largest;  // the largest free run of whole blocks
  uint32_t free_runs;     // free runs with one block or more
  uint32_t private_allocations;
  uint64_t top[kTop];     // the largest private allocations, largest first
  // The private allocation that the walk is in now.
  uint64_t open_base;
  uint64_t open_size;
};

inline void Start(Summary* s) {
  for (int k = 0; k < kKinds; ++k) s->committed[k] = s->reserved[k] = 0;
  s->free_usable = s->free_largest = 0;
  s->free_runs = 0;
  s->private_allocations = 0;
  for (int i = 0; i < kTop; ++i) s->top[i] = 0;
  s->open_base = 0;
  s->open_size = 0;
}

inline int KindOf(uint32_t type) {
  if (type == kTypeImage) return kImage;
  if (type == kTypeMapped) return kMapped;
  return kPrivate;
}

// The whole blocks of 64 KiB in [base, base + size).
inline uint64_t UsableBlocks(uint64_t base, uint64_t size) {
  const uint64_t first = (base + kGranularity - 1) / kGranularity;
  const uint64_t end = (base + size) / kGranularity;
  return end > first ? (end - first) * kGranularity : 0;
}

inline void PutInTop(Summary* s, uint64_t size) {
  for (int i = 0; i < kTop; ++i) {
    if (size > s->top[i]) {
      for (int j = kTop - 1; j > i; --j) s->top[j] = s->top[j - 1];
      s->top[i] = size;
      return;
    }
  }
}

inline void CloseAllocation(Summary* s) {
  if (s->open_size == 0) return;
  ++s->private_allocations;
  PutInTop(s, s->open_size);
  s->open_size = 0;
}

// One region. Regions of one private allocation follow each other and have
// the same allocation base.
inline void Add(Summary* s, const Region& r) {
  const bool private_part =
      r.state != kFree && KindOf(r.type) == kPrivate;
  if (!private_part || r.allocation_base != s->open_base) CloseAllocation(s);
  if (r.state == kFree) {
    const uint64_t usable = UsableBlocks(r.base, r.size);
    if (usable > 0) {
      s->free_usable += usable;
      ++s->free_runs;
      if (usable > s->free_largest) s->free_largest = usable;
    }
    return;
  }
  const int kind = KindOf(r.type);
  if (r.state == kCommit) {
    s->committed[kind] += r.size;
  } else if (r.state == kReserve) {
    s->reserved[kind] += r.size;
  }
  if (private_part) {
    if (s->open_size == 0) s->open_base = r.allocation_base;
    s->open_size += r.size;
  }
}

inline void Finish(Summary* s) { CloseAllocation(s); }

// True when `value` is in the array `a` of `n` values in rising order.
inline bool InSorted(const uint32_t* a, int n, uint32_t value) {
  int lo = 0, hi = n;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (a[mid] < value) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo < n && a[lo] == value;
}

inline uint64_t InUse(const Summary& s) {
  uint64_t sum = 0;
  for (int k = 0; k < kKinds; ++k) sum += s.committed[k] + s.reserved[k];
  return sum;
}

}  // namespace address_space
