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

const SIZE_T kMinBytes = 256 * 1024;
const int kScanWords = 160;
const int kTopLines = 6;
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
SRWLOCK g_lock = SRWLOCK_INIT;
volatile LONG g_untracked = 0;  // blocks with no room in a table
uint64_t g_heap_live = 0;
uint64_t g_virtual_live = 0;
uint64_t g_live_at_line = 0;
// The bases of the modules whose frames are not a place.
uintptr_t g_system[3] = {0, 0, 0};
// The depth of the hooks of the heap on this thread: a region of the heap
// is not counted again. A slot of TlsAlloc and not __declspec(thread): ntdll
// makes blocks for a new thread before the thread has its static TLS. A slot
// under 64 is in the TEB itself (TlsSlots at fs:0xE10). The hooks read and
// write it there: TlsGetValue would set the last error of the thread to 0,
// and a free in a hook can come after the error of a call of the system.
const DWORD kTebTlsSlots = 0xE10;
DWORD g_depth_slot = TLS_OUT_OF_INDEXES;

int Depth() {
  return static_cast<int>(__readfsdword(kTebTlsSlots + g_depth_slot * 4));
}

void SetDepth(int depth) {
  __writefsdword(kTebTlsSlots + g_depth_slot * 4, static_cast<DWORD>(depth));
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

void Add(uint32_t key, SIZE_T size, int kind, const void* from) {
  uint32_t frames[kFrames];
  Frames(from, frames);
  AcquireSRWLockExclusive(&g_lock);
  // A key that is still in the table is of a block that went with no hook
  // (HeapDestroy frees its blocks so): its place loses it now.
  const int old = Find(g_blocks, key);
  if (old >= 0) {
    Site& t = g_sites.site[g_blocks.site[old]];
    Freed(&t, g_blocks.size[old]);
    uint64_t& live = t.kind == kHeap ? g_heap_live : g_virtual_live;
    live = live >= g_blocks.size[old] ? live - g_blocks.size[old] : 0;
    Erase(&g_blocks, old);
  }
  const int site = SiteOf(&g_sites, frames, kind);
  if (site < 0 || !Insert(&g_blocks, key, static_cast<uint32_t>(size),
                          static_cast<uint16_t>(site))) {
    InterlockedIncrement(&g_untracked);
  } else {
    Made(&g_sites.site[site], static_cast<uint32_t>(size));
    (kind == kHeap ? g_heap_live : g_virtual_live) += size;
  }
  ReleaseSRWLockExclusive(&g_lock);
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
    uint64_t& live = t.kind == kHeap ? g_heap_live : g_virtual_live;
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
        (g_sites.site[old_site].kind == kHeap ? g_heap_live : g_virtual_live) +=
            old_size;
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
  if (status >= 0 && Depth() == 0 && (type & MEM_RESERVE) != 0 &&
      size != nullptr && *size >= kMinBytes && base != nullptr &&
      process == GetCurrentProcess()) {
    Add(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*base)), *size,
        kVirtual, _AddressOfReturnAddress());
  }
  return status;
}

LONG NTAPI h_FreeVirtual(HANDLE process, PVOID* base, PSIZE_T size,
                         ULONG type) {
  if ((type & MEM_RELEASE) != 0 && base != nullptr && *base != nullptr &&
      process == GetCurrentProcess()) {
    uint32_t old_size = 0;
    uint16_t old_site = 0;
    Remove(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*base)),
           &old_size, &old_site);
  }
  return o_free_virtual(process, base, size, type);
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
  if (g_depth_slot == TLS_OUT_OF_INDEXES || g_depth_slot >= 64) {
    ModLog("memory       : memory_blocks=1, but there is no slot of TlsAlloc "
           "under 64 (%lu). No hook", g_depth_slot);
    return;
  }
  for (int i = 0; i < kSlots; ++i) g_blocks.key[i] = kEmpty;
  g_sites.count = 0;
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
         "VirtualAlloc (%s) count each block of %u KB or more with its place. "
         "Blocks that came before this line are not counted",
         allocate_ok && reallocate_ok ? "ok" : "FAILED",
         virtual_ok ? "ok" : "FAILED",
         static_cast<unsigned>(kMinBytes / 1024));
}

// memory_watch.cpp, on the memory thread, after each line of the address
// space.
void BlockWatchReport() {
  if (!g_on) return;
  static Sites copy;
  AcquireSRWLockExclusive(&g_lock);
  copy.count = g_sites.count;
  memcpy(copy.site, g_sites.site, sizeof(Site) * g_sites.count);
  for (int i = 0; i < g_sites.count; ++i) {
    g_sites.site[i].bytes_at_line = g_sites.site[i].live_bytes;
  }
  const uint64_t heap = g_heap_live, virt = g_virtual_live;
  const uint64_t before = g_live_at_line;
  g_live_at_line = heap + virt;
  ReleaseSRWLockExclusive(&g_lock);
  uint32_t blocks = 0;
  for (int i = 0; i < copy.count; ++i) blocks += copy.site[i].live_count;
  ModLog("memory       : the large blocks (%u KB or more) that live: %u "
         "blocks, %.1f MB (heap %.1f, VirtualAlloc %.1f MB), %+.1f MB since the "
         "line before, %d places, %ld blocks with no room in the table",
         static_cast<unsigned>(kMinBytes / 1024), blocks,
         (heap + virt) / kMiB, heap / kMiB, virt / kMiB,
         (static_cast<double>(heap + virt) - static_cast<double>(before)) /
             kMiB,
         copy.count, g_untracked);
  int top[kTopLines];
  const int n = Top(copy, top, kTopLines);
  for (int i = 0; i < n; ++i) {
    const Site& t = copy.site[top[i]];
    char frames[kFrames][64];
    for (int f = 0; f < kFrames; ++f) {
      if (t.frames[f] == 0) {
        strcpy_s(frames[f], "-");
      } else {
        Where(t.frames[f], frames[f], sizeof(frames[f]));
      }
    }
    ModLog("memory       :   place %d (%s): %u blocks live, %.1f MB, %+.1f MB "
           "since the line before, %u made in all. Its calls, the nearest "
           "first: %s < %s < %s < %s",
           i + 1, t.kind == kHeap ? "heap" : "VirtualAlloc", t.live_count,
           t.live_bytes / kMiB,
           (static_cast<double>(t.live_bytes) -
            static_cast<double>(t.bytes_at_line)) /
               kMiB,
           t.made_count, frames[0], frames[1], frames[2], frames[3]);
  }
}
