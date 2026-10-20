// The table of the large blocks and of their places (block_watch.cpp,
// docs/backend.md, "The large blocks"). Pure: the caller holds the lock for
// each change. Find only reads, so a free can look without the lock.
#pragma once

#include <stdint.h>

namespace large_block {

const int kSlots = 65536;  // blocks that the table can hold
const int kProbes = 64;    // the longest walk from the slot of a key
const int kSites = 1024;   // places
const int kFrames = 4;     // return addresses that make a place
const uint32_t kEmpty = 0;
const uint32_t kGone = 1;  // a slot of a block that was freed

// kHeap: a block of a heap. kVirtual: a region of VirtualAlloc. kSegment: a
// region that a heap makes for itself, with the heap in Site::heap. kGrowth:
// the pages that a heap commits in a call of its caller (in all, not live).
enum Kind { kHeap = 0, kVirtual = 1, kSegment = 2, kGrowth = 3 };

struct Blocks {
  volatile uint32_t key[kSlots];  // the address of the block
  uint32_t size[kSlots];
  uint16_t site[kSlots];
};

struct Site {
  uint32_t frames[kFrames];
  int kind;
  uint32_t heap;  // kSegment and kGrowth: the heap, else 0
  uint64_t live_bytes;
  uint32_t live_count;
  uint32_t made_count;
  uint64_t bytes_at_line;  // live_bytes at the line before
};

struct Sites {
  Site site[kSites];
  int count;
};

inline uint32_t SlotOf(uint32_t key) {
  return ((key >> 4) * 2654435761u) & (kSlots - 1);
}

// The slot of `key`, or -1.
inline int Find(const Blocks& b, uint32_t key) {
  if (key == kEmpty || key == kGone) return -1;
  uint32_t at = SlotOf(key);
  for (int i = 0; i < kProbes; ++i) {
    const uint32_t k = b.key[at];
    if (k == key) return static_cast<int>(at);
    if (k == kEmpty) return -1;
    at = (at + 1) & (kSlots - 1);
  }
  return -1;
}

// False when the walk finds no free slot.
inline bool Insert(Blocks* b, uint32_t key, uint32_t size, uint16_t site) {
  if (key == kEmpty || key == kGone) return false;
  uint32_t at = SlotOf(key);
  int reuse = -1;
  for (int i = 0; i < kProbes; ++i) {
    const uint32_t k = b->key[at];
    if (k == key) {
      reuse = static_cast<int>(at);
      break;
    }
    if (k == kGone && reuse < 0) reuse = static_cast<int>(at);
    if (k == kEmpty) {
      if (reuse < 0) reuse = static_cast<int>(at);
      break;
    }
    at = (at + 1) & (kSlots - 1);
  }
  if (reuse < 0) return false;
  b->size[reuse] = size;
  b->site[reuse] = site;
  b->key[reuse] = key;
  return true;
}

inline void Erase(Blocks* b, int slot) { b->key[slot] = kGone; }

// The index of the place with these frames, this kind and this heap. -1 when
// the table is full.
inline int SiteOf(Sites* s, const uint32_t frames[kFrames], int kind,
                  uint32_t heap = 0) {
  for (int i = 0; i < s->count; ++i) {
    const Site& t = s->site[i];
    if (t.kind != kind || t.heap != heap) continue;
    bool same = true;
    for (int f = 0; f < kFrames && same; ++f) same = t.frames[f] == frames[f];
    if (same) return i;
  }
  if (s->count >= kSites) return -1;
  Site& t = s->site[s->count];
  for (int f = 0; f < kFrames; ++f) t.frames[f] = frames[f];
  t.kind = kind;
  t.heap = heap;
  t.live_bytes = 0;
  t.live_count = 0;
  t.made_count = 0;
  t.bytes_at_line = 0;
  return s->count++;
}

inline void Made(Site* t, uint32_t size) {
  t->live_bytes += size;
  ++t->live_count;
  ++t->made_count;
}

inline void Freed(Site* t, uint32_t size) {
  t->live_bytes = t->live_bytes >= size ? t->live_bytes - size : 0;
  if (t->live_count > 0) --t->live_count;
}

// The `n` places with the most live bytes, the most first. A place of
// kGrowth counts bytes in all and not live bytes, so it is not in this top.
// Gives the count.
inline int Top(const Sites& s, int* out, int n) {
  int got = 0;
  for (int i = 0; i < s.count; ++i) {
    if (s.site[i].live_bytes == 0 || s.site[i].kind == kGrowth) continue;
    int at = got < n ? got++ : n;
    if (at == n) {
      if (s.site[i].live_bytes <= s.site[out[n - 1]].live_bytes) continue;
      at = n - 1;
    }
    out[at] = i;
    while (at > 0 && s.site[out[at]].live_bytes > s.site[out[at - 1]].live_bytes) {
      const int swap = out[at];
      out[at] = out[at - 1];
      out[at - 1] = swap;
      --at;
    }
  }
  return got;
}

// The `n` places of `kind` with the most bytes since the line before
// (live_bytes - bytes_at_line), the most first. Gives the count.
inline int TopGrowth(const Sites& s, int kind, int* out, int n) {
  int got = 0;
  for (int i = 0; i < s.count; ++i) {
    const Site& t = s.site[i];
    if (t.kind != kind || t.live_bytes <= t.bytes_at_line) continue;
    const uint64_t grew = t.live_bytes - t.bytes_at_line;
    int at = got < n ? got++ : n;
    if (at == n) {
      const Site& last = s.site[out[n - 1]];
      if (grew <= last.live_bytes - last.bytes_at_line) continue;
      at = n - 1;
    }
    out[at] = i;
    while (at > 0) {
      const Site& a = s.site[out[at]];
      const Site& b = s.site[out[at - 1]];
      if (a.live_bytes - a.bytes_at_line <= b.live_bytes - b.bytes_at_line) break;
      const int swap = out[at];
      out[at] = out[at - 1];
      out[at - 1] = swap;
      --at;
    }
  }
  return got;
}

// The heaps that grow inside the hooks: the pages that each commits less
// the pages that it gives back, and its segments.
const int kHeaps = 64;

struct Heap {
  uint32_t handle;
  int64_t committed;  // net, since the hooks came in
  int64_t committed_at_line;
};

struct Heaps {
  Heap heap[kHeaps];
  int count;
};

// The index of a heap, -1 when the table is full.
inline int HeapOf(Heaps* h, uint32_t handle) {
  for (int i = 0; i < h->count; ++i) {
    if (h->heap[i].handle == handle) return i;
  }
  if (h->count >= kHeaps) return -1;
  Heap& t = h->heap[h->count];
  t.handle = handle;
  t.committed = 0;
  t.committed_at_line = 0;
  return h->count++;
}

}  // namespace large_block
