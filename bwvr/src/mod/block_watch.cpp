// THE LARGE BLOCKS AND THEIR PLACES (memory_blocks, docs/backend.md, "The
// large blocks").
//
// The line of the address space (memory_watch.cpp) gives the use by kind,
// and not who makes it. With memory_blocks=1 hooks of ntdll count each
// large block (kMinBytes or more) while it lives:
//   - RtlAllocateHeap, RtlReAllocateHeap and RtlFreeHeap: a block of a heap.
//     HeapAlloc, malloc and new of each C runtime come here.
//   - NtAllocateVirtualMemory with MEM_RESERVE and NtFreeVirtualMemory with
//     MEM_RELEASE: a region of VirtualAlloc, also of a driver. A heap makes
//     such a region for a large block of its own. A call inside a hook of the
//     heap is not counted again.
// A place is the first kFrames return addresses on the stack of the call
// outside ntdll, KernelBase and kernel32. The stack is read from the return
// address of the call up: a word counts when it is in an image that can run
// code and the bytes before it end with a call (scene_sample_math.h). A word
// that only looks like a return address can pass. The memory thread writes
// the places with the most live bytes after each line of the address space.
//
// The hooks run for each block of each heap. A small block costs a compare.
// A large block costs a walk of the stack, and a free costs a look in the
// table with no lock.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <psapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "large_block_math.h"
#include "scene_sample_math.h"

extern void ModLog(const char* format, ...);

// The walk of the stack starts at the address of the return address of a
// hook, so each hook keeps its frame.
#pragma optimize("y", off)

namespace {

using namespace large_block;

const SIZE_T kMinBytes = 256 * 1024;        // a block of a heap
const SIZE_T kMinVirtualBytes = 64 * 1024;  // a region of VirtualAlloc
const uintptr_t kGameCrtHeap = 0x00FACCA8;  // __crtheap of the game
const int kScanWords = 160;
const int kTopLines = 4;
const int kTopGrowth = 6;
const int kTopHeaps = 4;
const double kMiB = 1024.0 * 1024.0;

typedef PVOID(NTAPI* AllocateHeapFn)(PVOID heap, ULONG flags, SIZE_T size);
typedef PVOID(NTAPI* ReAllocateHeapFn)(PVOID heap, ULONG flags, PVOID p,
                                       SIZE_T size);
typedef BOOLEAN(NTAPI* FreeHeapFn)(PVOID heap, ULONG flags, PVOID p);
typedef LONG(NTAPI* AllocateVirtualFn)(HANDLE process, PVOID* base,
                                       ULONG_PTR zero_bits, PSIZE_T size,
                                       ULONG type, ULONG protect);
typedef LONG(NTAPI* FreeVirtualFn)(HANDLE process, PVOID* base, PSIZE_T size,
                                   ULONG type);

AllocateHeapFn o_allocate = nullptr;
ReAllocateHeapFn o_reallocate = nullptr;
FreeHeapFn o_free = nullptr;
AllocateVirtualFn o_allocate_virtual = nullptr;
FreeVirtualFn o_free_virtual = nullptr;

bool g_on = false;
Blocks g_blocks;
Sites g_sites;
Sites g_growth;  // kGrowth: the pages that the heaps commit, by place
Heaps g_heaps;
SRWLOCK g_lock = SRWLOCK_INIT;
volatile LONG g_untracked = 0;  // blocks with no room in a table
uint64_t g_live[3] = {0, 0, 0};  // kHeap, kVirtual, kSegment
uint64_t g_live_at_line = 0;
// The bases of the modules whose frames are not a place.
uintptr_t g_system[3] = {0, 0, 0};
// The depth of the hooks of the heap on this thread: a region of the heap
// is not counted again. A slot of TlsAlloc and not __declspec(thread): ntdll
// makes blocks for a new thread before the thread has its static TLS. A slot
// under 64 is in the TEB itself (TlsSlots at fs:0xE10). The hooks read and
// write it there: TlsGetValue would set the last error of the thread to 0,
// and a free in a hook can come after the error of a call of the system.
// Two more slots hold the heap of the outer call and the address of the
// return address of the outer call, for the pages that the heap commits in
// it.
const DWORD kTebTlsSlots = 0xE10;
DWORD g_depth_slot = TLS_OUT_OF_INDEXES;
DWORD g_heap_slot = TLS_OUT_OF_INDEXES;
DWORD g_from_slot = TLS_OUT_OF_INDEXES;

DWORD ReadSlot(DWORD slot) { return __readfsdword(kTebTlsSlots + slot * 4); }

void WriteSlot(DWORD slot, DWORD value) {
  __writefsdword(kTebTlsSlots + slot * 4, value);
}

int Depth() { return static_cast<int>(ReadSlot(g_depth_slot)); }

void SetDepth(int depth) {
  WriteSlot(g_depth_slot, static_cast<DWORD>(depth));
}

// The start of an outer call of the heap.
void EnterHeap(PVOID heap, const void* from) {
  WriteSlot(g_heap_slot,
            static_cast<DWORD>(reinterpret_cast<uintptr_t>(heap)));
  WriteSlot(g_from_slot,
            static_cast<DWORD>(reinterpret_cast<uintptr_t>(from)));
}

uint64_t& LiveOf(int kind) {
  return g_live[kind == kVirtual ? 1 : kind == kSegment ? 2 : 0];
}

bool IsSystem(uintptr_t base) {
  return base == g_system[0] || base == g_system[1] || base == g_system[2];
}

// The place of a call: the first kFrames return addresses from `from` up.
void Frames(const void* from, uint32_t frames[kFrames]) {
  for (int f = 0; f < kFrames; ++f) frames[f] = 0;
  const NT_TIB* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
  const uint32_t* word = static_cast<const uint32_t*>(from);
  const uint32_t* top = static_cast<const uint32_t*>(tib->StackBase);
  uintptr_t region_lo = 0, region_hi = 0, region_base = 0;
  bool region_code = false;
  int got = 0;
  for (int w = 0; w < kScanWords && word + w < top && got < kFrames; ++w) {
    const uintptr_t value = word[w];
    if (value < 0x10000) continue;
    if (value < region_lo || value >= region_hi) {
      MEMORY_BASIC_INFORMATION m;
      if (VirtualQuery(reinterpret_cast<LPCVOID>(value - 7), &m, sizeof(m)) ==
          0) {
        continue;
      }
      region_lo = reinterpret_cast<uintptr_t>(m.BaseAddress);
      region_hi = region_lo + m.RegionSize;
      region_base = reinterpret_cast<uintptr_t>(m.AllocationBase);
      region_code = m.Type == MEM_IMAGE && m.State == MEM_COMMIT &&
                    (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                  PAGE_EXECUTE_READWRITE |
                                  PAGE_EXECUTE_WRITECOPY)) != 0;
      if (value - 7 < region_lo) region_code = false;
    }
    if (!region_code || IsSystem(region_base)) continue;
    if (!scene_sample::EndsWithCall(reinterpret_cast<const uint8_t*>(value - 7))) {
      continue;
    }
    frames[got++] = static_cast<uint32_t>(value);
  }
}

// A block or a region with its place. A segment of a heap has no frames.
void Add(uint32_t key, SIZE_T size, int kind, const void* from,
         uint32_t heap = 0) {
  uint32_t frames[kFrames];
  if (from != nullptr) {
    Frames(from, frames);
  } else {
    for (int f = 0; f < kFrames; ++f) frames[f] = 0;
  }
  AcquireSRWLockExclusive(&g_lock);
  // A key that is still in the table is of a block that went with no hook
  // (HeapDestroy frees its blocks so): its place loses it now.
  const int old = Find(g_blocks, key);
  if (old >= 0) {
    Site& t = g_sites.site[g_blocks.site[old]];
    Freed(&t, g_blocks.size[old]);
    uint64_t& live = LiveOf(t.kind);
    live = live >= g_blocks.size[old] ? live - g_blocks.size[old] : 0;
    Erase(&g_blocks, old);
  }
  const int site = SiteOf(&g_sites, frames, kind, heap);
  if (site < 0 || !Insert(&g_blocks, key, static_cast<uint32_t>(size),
                          static_cast<uint16_t>(site))) {
    InterlockedIncrement(&g_untracked);
  } else {
    Made(&g_sites.site[site], static_cast<uint32_t>(size));
    LiveOf(kind) += size;
  }
  ReleaseSRWLockExclusive(&g_lock);
}

// Pages that a heap commits (bytes > 0) or gives back (bytes < 0) in a call
// of its caller. A commit gets the place of the outer call.
void Grow(uint32_t heap, int64_t bytes, const void* from) {
  uint32_t frames[kFrames];
  if (bytes > 0 && from != nullptr) {
    Frames(from, frames);
  } else {
    for (int f = 0; f < kFrames; ++f) frames[f] = 0;
  }
  AcquireSRWLockExclusive(&g_lock);
  const int h = HeapOf(&g_heaps, heap);
  if (h >= 0) g_heaps.heap[h].committed += bytes;
  if (bytes > 0) {
    const int site = SiteOf(&g_growth, frames, kGrowth, heap);
    if (site >= 0) Made(&g_growth.site[site], static_cast<uint32_t>(bytes));
  }
  ReleaseSRWLockExclusive(&g_lock);
}

// The committed bytes of [base, base + size), before a release.
uint64_t CommittedIn(uintptr_t base, SIZE_T size) {
  uint64_t sum = 0;
  uintptr_t at = base;
  while (at < base + size) {
    MEMORY_BASIC_INFORMATION m;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(at), &m, sizeof(m)) == 0) break;
    const uintptr_t end = reinterpret_cast<uintptr_t>(m.BaseAddress) +
                          m.RegionSize;
    if (end <= at) break;
    if (m.State == MEM_COMMIT) sum += end - at;
    at = end;
  }
  return sum;
}

// Takes a block out of the table. False when it was not in it.
bool Remove(uint32_t key, uint32_t* size, uint16_t* site) {
  if (Find(g_blocks, key) < 0) return false;  // with no lock: the most frees
  AcquireSRWLockExclusive(&g_lock);
  const int slot = Find(g_blocks, key);
  const bool found = slot >= 0;
  if (found) {
    *size = g_blocks.size[slot];
    *site = g_blocks.site[slot];
    Site& t = g_sites.site[*site];
    Freed(&t, *size);
    uint64_t& live = LiveOf(t.kind);
    live = live >= *size ? live - *size : 0;
    Erase(&g_blocks, slot);
  }
  ReleaseSRWLockExclusive(&g_lock);
  return found;
}

// A call of the heap inside a hook of the heap (for example in a
// reallocation) is part of the outer call, and only the outer call counts.
PVOID NTAPI h_Allocate(PVOID heap, ULONG flags, SIZE_T size) {
  const int depth = Depth();
  const bool outer = depth == 0;
  if (outer) EnterHeap(heap, _AddressOfReturnAddress());
  SetDepth(depth + 1);
  PVOID p = o_allocate(heap, flags, size);
  SetDepth(depth);
  if (outer && p != nullptr && size >= kMinBytes) {
    Add(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)), size, kHeap,
        _AddressOfReturnAddress());
  }
  return p;
}

BOOLEAN NTAPI h_Free(PVOID heap, ULONG flags, PVOID p) {
  const int depth = Depth();
  if (depth == 0 && p != nullptr) {
    uint32_t size = 0;
    uint16_t site = 0;
    Remove(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)), &size,
           &site);
  }
  if (depth == 0) EnterHeap(heap, nullptr);
  SetDepth(depth + 1);
  const BOOLEAN ok = o_free(heap, flags, p);
  SetDepth(depth);
  return ok;
}

PVOID NTAPI h_ReAllocate(PVOID heap, ULONG flags, PVOID p, SIZE_T size) {
  const int depth = Depth();
  const bool outer = depth == 0;
  uint32_t old_size = 0;
  uint16_t old_site = 0;
  const bool had =
      outer && p != nullptr &&
      Remove(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)), &old_size,
             &old_site);
  if (outer) EnterHeap(heap, _AddressOfReturnAddress());
  SetDepth(depth + 1);
  PVOID q = o_reallocate(heap, flags, p, size);
  SetDepth(depth);
  if (q == nullptr) {
    // The old block lives on.
    if (had) {
      AcquireSRWLockExclusive(&g_lock);
      if (Insert(&g_blocks, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)),
                 old_size, old_site)) {
        Made(&g_sites.site[old_site], old_size);
        LiveOf(g_sites.site[old_site].kind) += old_size;
      }
      ReleaseSRWLockExclusive(&g_lock);
    }
    return q;
  }
  if (outer && size >= kMinBytes) {
    Add(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(q)), size, kHeap,
        _AddressOfReturnAddress());
  }
  return q;
}

LONG NTAPI h_AllocateVirtual(HANDLE process, PVOID* base, ULONG_PTR zero_bits,
                             PSIZE_T size, ULONG type, ULONG protect) {
  const LONG status =
      o_allocate_virtual(process, base, zero_bits, size, type, protect);
  if (status < 0 || size == nullptr || base == nullptr || *base == nullptr ||
      process != GetCurrentProcess()) {
    return status;
  }
  const uint32_t at = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*base));
  if (Depth() == 0) {
    if ((type & MEM_RESERVE) != 0 && *size >= kMinVirtualBytes) {
      Add(at, *size, kVirtual, _AddressOfReturnAddress());
    }
    return status;
  }
  // Inside a call of the heap: a segment of the heap, or its pages.
  const uint32_t heap = ReadSlot(g_heap_slot);
  if ((type & MEM_RESERVE) != 0) Add(at, *size, kSegment, nullptr, heap);
  if ((type & MEM_COMMIT) != 0) {
    Grow(heap, static_cast<int64_t>(*size),
         reinterpret_cast<const void*>(
             static_cast<uintptr_t>(ReadSlot(g_from_slot))));
  }
  return status;
}

LONG NTAPI h_FreeVirtual(HANDLE process, PVOID* base, PSIZE_T size,
                         ULONG type) {
  const bool here = base != nullptr && *base != nullptr &&
                    process == GetCurrentProcess();
  const bool in_heap = here && Depth() > 0;
  uint64_t released = 0;
  if ((type & MEM_RELEASE) != 0 && here) {
    uint32_t old_size = 0;
    uint16_t old_site = 0;
    const uintptr_t at = reinterpret_cast<uintptr_t>(*base);
    if (Remove(static_cast<uint32_t>(at), &old_size, &old_site) && in_heap) {
      released = CommittedIn(at, old_size);
    }
  }
  const LONG status = o_free_virtual(process, base, size, type);
  if (status >= 0 && in_heap) {
    const uint32_t heap = ReadSlot(g_heap_slot);
    if ((type & MEM_DECOMMIT) != 0 && size != nullptr) {
      Grow(heap, -static_cast<int64_t>(*size), nullptr);
    } else if (released > 0) {
      Grow(heap, -static_cast<int64_t>(released), nullptr);
    }
  }
  return status;
}

bool HookExport(HMODULE module, const char* name, void* hook, void** original) {
  void* target = module != nullptr ? GetProcAddress(module, name) : nullptr;
  if (target == nullptr) return false;
  MH_STATUS status = MH_CreateHook(target, hook, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  return status == MH_OK;
}

// A frame as "module+offset". On the memory thread, so the lock of the
// loader is free to take.
void Where(uint32_t address, char* out, size_t size) {
  HMODULE module = nullptr;
  char path[MAX_PATH];
  if (address != 0 &&
      GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(
                             static_cast<uintptr_t>(address)),
                         &module) &&
      GetModuleFileNameA(module, path, MAX_PATH) != 0) {
    const char* slash = strrchr(path, '\\');
    sprintf_s(out, size, "%s+0x%X", slash != nullptr ? slash + 1 : path,
              address - static_cast<uint32_t>(
                            reinterpret_cast<uintptr_t>(module)));
  } else {
    sprintf_s(out, size, "0x%08X", address);
  }
}

}  // namespace

#pragma optimize("", on)

// mod_main.cpp, in Startup, after MH_Initialize.
void BlockWatchInstall() {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash = length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  int key = 1;
  if (slash != nullptr && strlen(path) + 9 < MAX_PATH) {
    slash[1] = 0;
    strcat_s(path, "bwvr.ini");
    key = static_cast<int>(GetPrivateProfileIntA("bwvr", "memory_blocks", 1,
                                                 path));
  }
  if (key == 0) {
    ModLog("memory       : memory_blocks=0: the line of the address space has "
           "no places of the large blocks");
    return;
  }
  g_depth_slot = TlsAlloc();
  g_heap_slot = TlsAlloc();
  g_from_slot = TlsAlloc();
  if (g_depth_slot >= 64 || g_heap_slot >= 64 || g_from_slot >= 64) {
    ModLog("memory       : memory_blocks=1, but there are no 3 slots of "
           "TlsAlloc under 64 (%lu, %lu, %lu). No hook", g_depth_slot,
           g_heap_slot, g_from_slot);
    return;
  }
  for (int i = 0; i < kSlots; ++i) g_blocks.key[i] = kEmpty;
  g_sites.count = 0;
  g_growth.count = 0;
  g_heaps.count = 0;
  HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  g_system[0] = reinterpret_cast<uintptr_t>(ntdll);
  g_system[1] = reinterpret_cast<uintptr_t>(GetModuleHandleA("kernelbase.dll"));
  g_system[2] = reinterpret_cast<uintptr_t>(GetModuleHandleA("kernel32.dll"));
  // The free first, so no block that a hook counts is freed with no hook.
  const bool free_ok = HookExport(ntdll, "RtlFreeHeap",
                                  reinterpret_cast<void*>(&h_Free),
                                  reinterpret_cast<void**>(&o_free));
  const bool virtual_free_ok =
      HookExport(ntdll, "NtFreeVirtualMemory",
                 reinterpret_cast<void*>(&h_FreeVirtual),
                 reinterpret_cast<void**>(&o_free_virtual));
  const bool reallocate_ok =
      free_ok && HookExport(ntdll, "RtlReAllocateHeap",
                            reinterpret_cast<void*>(&h_ReAllocate),
                            reinterpret_cast<void**>(&o_reallocate));
  const bool allocate_ok =
      free_ok && HookExport(ntdll, "RtlAllocateHeap",
                            reinterpret_cast<void*>(&h_Allocate),
                            reinterpret_cast<void**>(&o_allocate));
  const bool virtual_ok =
      virtual_free_ok &&
      HookExport(ntdll, "NtAllocateVirtualMemory",
                 reinterpret_cast<void*>(&h_AllocateVirtual),
                 reinterpret_cast<void**>(&o_allocate_virtual));
  g_on = allocate_ok || virtual_ok;
  ModLog("memory       : memory_blocks=1. The hooks of the heap (%s) and of "
         "VirtualAlloc (%s) count each block of a heap of %u KB or more and "
         "each region of VirtualAlloc of %u KB or more with its place, and "
         "the pages that each heap commits, by the place of the call. Blocks "
         "that came before this line are not counted",
         allocate_ok && reallocate_ok ? "ok" : "FAILED",
         virtual_ok ? "ok" : "FAILED",
         static_cast<unsigned>(kMinBytes / 1024),
         static_cast<unsigned>(kMinVirtualBytes / 1024));
}

namespace {

// The name of a heap for the log.
void HeapName(uint32_t handle, char* out, size_t size) {
  const uint32_t process = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(GetProcessHeap()));
  uint32_t game = 0;
  if (!IsBadReadPtr(reinterpret_cast<const void*>(kGameCrtHeap), 4)) {
    game = *reinterpret_cast<const uint32_t*>(kGameCrtHeap);
  }
  if (handle == process) {
    sprintf_s(out, size, "the process heap 0x%08X (the C runtime of the mod)",
              handle);
  } else if (handle == game) {
    sprintf_s(out, size, "the heap of the C runtime of the game 0x%08X",
              handle);
  } else {
    sprintf_s(out, size, "the heap 0x%08X", handle);
  }
}

void FramesText(const Site& t, char frames[kFrames][64]) {
  for (int f = 0; f < kFrames; ++f) {
    if (t.frames[f] == 0) {
      strcpy_s(frames[f], "-");
    } else {
      Where(t.frames[f], frames[f], sizeof(frames[f]));
    }
  }
}

}  // namespace

// memory_watch.cpp: true when a hook saw the region at `base` (a region of
// VirtualAlloc or a segment of a heap) and it lives. With no lock.
bool BlockWatchKnows(uint32_t base) {
  return g_on && Find(g_blocks, base) >= 0;
}

// memory_watch.cpp: the hooks are in.
bool BlockWatchOn() { return g_on; }

// memory_watch.cpp, on the memory thread, after each line of the address
// space.
void BlockWatchReport() {
  if (!g_on) return;
  static Sites copy;
  static Sites growth;
  static Heaps heaps;
  AcquireSRWLockExclusive(&g_lock);
  copy.count = g_sites.count;
  memcpy(copy.site, g_sites.site, sizeof(Site) * g_sites.count);
  for (int i = 0; i < g_sites.count; ++i) {
    g_sites.site[i].bytes_at_line = g_sites.site[i].live_bytes;
  }
  growth.count = g_growth.count;
  memcpy(growth.site, g_growth.site, sizeof(Site) * g_growth.count);
  for (int i = 0; i < g_growth.count; ++i) {
    g_growth.site[i].bytes_at_line = g_growth.site[i].live_bytes;
  }
  heaps.count = g_heaps.count;
  memcpy(heaps.heap, g_heaps.heap, sizeof(Heap) * g_heaps.count);
  for (int i = 0; i < g_heaps.count; ++i) {
    g_heaps.heap[i].committed_at_line = g_heaps.heap[i].committed;
  }
  const uint64_t heap = g_live[0], virt = g_live[1], segments = g_live[2];
  const uint64_t before = g_live_at_line;
  g_live_at_line = heap + virt;
  ReleaseSRWLockExclusive(&g_lock);
  uint32_t blocks = 0;
  for (int i = 0; i < copy.count; ++i) {
    if (copy.site[i].kind == kHeap || copy.site[i].kind == kVirtual) {
      blocks += copy.site[i].live_count;
    }
  }
  ModLog("memory       : the large blocks that live: %u blocks, %.1f MB (blocks "
         "of a heap of %u KB or more %.1f, regions of VirtualAlloc of %u KB or "
         "more %.1f MB), %+.1f MB since the line before. The segments of the "
         "heaps %.1f MB. %d places, %ld blocks with no room in the table",
         blocks, (heap + virt) / kMiB, static_cast<unsigned>(kMinBytes / 1024),
         heap / kMiB, static_cast<unsigned>(kMinVirtualBytes / 1024),
         virt / kMiB,
         (static_cast<double>(heap + virt) - static_cast<double>(before)) /
             kMiB,
         segments / kMiB, copy.count, g_untracked);
  // The heaps that grew most since the line before.
  int order[kHeaps];
  for (int i = 0; i < heaps.count; ++i) order[i] = i;
  for (int i = 1; i < heaps.count; ++i) {
    for (int j = i; j > 0; --j) {
      const Heap& a = heaps.heap[order[j]];
      const Heap& b = heaps.heap[order[j - 1]];
      if (a.committed - a.committed_at_line <= b.committed - b.committed_at_line) {
        break;
      }
      const int swap = order[j];
      order[j] = order[j - 1];
      order[j - 1] = swap;
    }
  }
  for (int i = 0; i < heaps.count && i < kTopHeaps; ++i) {
    const Heap& h = heaps.heap[order[i]];
    char name[96];
    HeapName(h.handle, name, sizeof(name));
    ModLog("memory       :   %s: %+.1f MB of pages since the line before, "
           "%+.1f MB since the hooks came in",
           name, (h.committed - h.committed_at_line) / kMiB,
           h.committed / kMiB);
  }
  // The calls in which the heaps committed the most pages since the line
  // before. A heap commits pages when its free room is used up, so the call
  // that does that is often the call of a place that makes many blocks.
  int top_growth[kTopGrowth];
  const int g = TopGrowth(growth, kGrowth, top_growth, kTopGrowth);
  for (int i = 0; i < g; ++i) {
    const Site& t = growth.site[top_growth[i]];
    char frames[kFrames][64];
    FramesText(t, frames);
    char name[96];
    HeapName(t.heap, name, sizeof(name));
    ModLog("memory       :   growth %d: %+.1f MB of pages since the line "
           "before, %.1f MB in all in %u commits, in %s. Its calls, the "
           "nearest first: %s < %s < %s < %s",
           i + 1,
           (static_cast<double>(t.live_bytes) -
            static_cast<double>(t.bytes_at_line)) /
               kMiB,
           t.live_bytes / kMiB, t.made_count, name, frames[0], frames[1],
           frames[2], frames[3]);
  }
  int top[kTopLines];
  const int n = Top(copy, top, kTopLines);
  for (int i = 0; i < n; ++i) {
    const Site& t = copy.site[top[i]];
    char frames[kFrames][64];
    FramesText(t, frames);
    char name[96] = "";
    if (t.kind == kSegment) HeapName(t.heap, name, sizeof(name));
    ModLog("memory       :   place %d (%s%s%s): %u blocks live, %.1f MB, %+.1f "
           "MB since the line before, %u made in all. Its calls, the nearest "
           "first: %s < %s < %s < %s",
           i + 1,
           t.kind == kHeap      ? "heap"
           : t.kind == kVirtual ? "VirtualAlloc"
                                : "segments of ",
           name, "", t.live_count, t.live_bytes / kMiB,
           (static_cast<double>(t.live_bytes) -
            static_cast<double>(t.bytes_at_line)) /
               kMiB,
           t.made_count, frames[0], frames[1], frames[2], frames[3]);
  }
}
