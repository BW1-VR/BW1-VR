// THE SAMPLES OF THE SCENE WALK, the pure part (scene_sample.cpp, and
// docs/backend.md, "Where the time of the scene walk goes"). No Windows, no
// game. The checks are in test/controller_math/scene_sample_checks.h.
//
// A sample is an address of code. A module is a range of addresses with a
// name. The summary gives the share of each module, and the buckets of 64
// bytes with the most samples.

#pragma once

#include <stdint.h>
#include <stdlib.h>

namespace scene_sample {

struct Module {
  uint32_t base;
  uint32_t size;
};

// The index of the module that holds the address, in modules sorted by base,
// or -1.
inline int ModuleOf(uint32_t address, const Module* modules, int count) {
  int low = 0, high = count - 1;
  while (low <= high) {
    const int mid = (low + high) / 2;
    if (address < modules[mid].base) {
      high = mid - 1;
    } else if (address - modules[mid].base >= modules[mid].size) {
      low = mid + 1;
    } else {
      return mid;
    }
  }
  return -1;
}

// Sorts the modules by base (insertion sort: a process has a few hundred).
inline void SortModules(Module* modules, int count, int* order) {
  for (int i = 0; i < count; ++i) order[i] = i;
  for (int i = 1; i < count; ++i) {
    const Module m = modules[i];
    const int o = order[i];
    int j = i - 1;
    while (j >= 0 && modules[j].base > m.base) {
      modules[j + 1] = modules[j];
      order[j + 1] = order[j];
      --j;
    }
    modules[j + 1] = m;
    order[j + 1] = o;
  }
}

const uint32_t kBucketShift = 6;  // buckets of 64 bytes

struct Bucket {
  uint32_t first;  // the first address of the bucket
  uint32_t count;
};

inline int CompareAddress(const void* a, const void* b) {
  const uint32_t x = *static_cast<const uint32_t*>(a);
  const uint32_t y = *static_cast<const uint32_t*>(b);
  return x < y ? -1 : (x > y ? 1 : 0);
}

// The `want` buckets with the most samples, the most first. `addresses` is
// sorted here. Gives the count of buckets in `out`.
inline int TopBuckets(uint32_t* addresses, int count, Bucket* out, int want) {
  if (count <= 0 || want <= 0) return 0;
  qsort(addresses, static_cast<size_t>(count), sizeof(uint32_t),
        CompareAddress);
  int have = 0;
  int i = 0;
  while (i < count) {
    const uint32_t first = addresses[i] >> kBucketShift << kBucketShift;
    uint32_t n = 0;
    while (i < count &&
           (addresses[i] >> kBucketShift << kBucketShift) == first) {
      ++n;
      ++i;
    }
    // Keep the list sorted by count, the most first.
    int place = have;
    while (place > 0 && out[place - 1].count < n) --place;
    if (place >= want) continue;
    const int last = have < want ? have : want - 1;
    for (int k = last; k > place; --k) out[k] = out[k - 1];
    out[place].first = first;
    out[place].count = n;
    if (have < want) ++have;
  }
  return have;
}

}  // namespace scene_sample
