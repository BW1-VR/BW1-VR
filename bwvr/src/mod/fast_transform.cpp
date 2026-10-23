// The two vertex loops of the engine with SSE (docs/backend.md, "The vertex
// loops with SSE"). The math is in fast_transform_math.h.
//
// WHERE. fn_00856D40, fn_0085AB10 and fn_0084BA90 each transform the
// vertices of a mesh in a loop on the x87 unit, then build the triangles.
// Each has a second loop for a mesh with a matrix for each group of vertices
// (bit 0 of *0x00FA93BC). That loop is rare in the first two, and common in
// fn_0084BA90 (the meshes of people and of the Creature). A jump at the start
// of a loop goes to a stub here. The stub keeps each register, and calls the
// SSE loop. When the SSE loop did the work, the stub goes on at the code
// after the loop. Else it goes back into the loop of the game, through the
// trampoline of MinHook.
//
//   fn_00856D40  0x00856D6E  mov eax,[ebp+10h]; xor ebx,ebx   after: 0x0085713C
//   fn_0085AB10  0x0085AB64  mov ecx,[ebx+10h]; test ecx,ecx  after: 0x0085B05B
//   fn_0084BA90  0x0084BAE5  mov ecx,[ebx+10h]; test ecx,ecx  after: 0x0084C0E6
//   fn_0084BA90  0x0084BE60  lea eax,[ebp-18h]; mov [ebp-34h],eax
//                                                     (a group) after: 0x0084C0C1
//
// The code after each loop with one matrix reads only the object (ebp, or
// ebx) and sets each other register and local that it uses. At the start of
// the loops of fn_0085AB10 and fn_0084BA90 with one matrix, eax has the color
// of the mesh. The loop of a group of fn_0084BA90 has the input in esi, the
// output in edi, the matrix of the group in ebx, the place of the clip flags
// in [ebp-10h], the light of the group in [ebp-44h], [ebp-40h] and
// [ebp-3Ch], the color in [ebp-30h], the group in [ebp-28h] (its first word
// is its count of vertices) and the index of its first vertex in [ebp-8]. The
// code after it reads [ebp-8], and the next group goes on from esi, edi and
// [ebp-10h], so the stub moves them past the group.
//
// THE SAME BITS. The SSE loop runs only while the x87 unit has the precision
// of 24 bits and rounds to the nearest (fast_transform_math::Exact). Else the
// loop of the game runs, and the report counts it. In the game MXCSR has the
// flush to zero on, which the x87 unit does not do. So the SSE loop sets
// MXCSR to round to the nearest with no flush to zero while it runs, and
// puts the value of the game back after it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <emmintrin.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "debug_menu.h"
#include "fast_transform.h"
#include "fast_transform_math.h"

void ModLog(const char* format, ...);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);

namespace {

const uintptr_t kClipLoop = 0x00856D6E;
const uintptr_t kClipAfter = 0x0085713C;
const uintptr_t kLitLoop = 0x0085AB64;
const uintptr_t kLitAfter = 0x0085B05B;
const uint8_t kClipBytes[5] = {0x8B, 0x45, 0x10, 0x33, 0xDB};
const uint8_t kLitBytes[5] = {0x8B, 0x4B, 0x10, 0x85, 0xC9};
const uintptr_t kLitClipLoop = 0x0084BAE5;
const uintptr_t kLitClipAfter = 0x0084C0E6;
const uintptr_t kGroupLoop = 0x0084BE60;
const uintptr_t kGroupAfter = 0x0084C0C1;
const uint8_t kGroupBytes[6] = {0x8D, 0x45, 0xE8, 0x89, 0x45, 0xCC};

const uintptr_t kMatrix = 0x00EA9EA0;     // a pointer to 12 floats
const uintptr_t kNearPlane = 0x00E839E0;
const uintptr_t kHalfX = 0x00E839F0;
const uintptr_t kHalfY = 0x00E839F4;
const uintptr_t kMaxScreen = 0x00C2AB00;  // x, y
const uintptr_t kColor = 0x00C37D8C;
const uintptr_t kSpecular = 0x00E9FE2C;
const uintptr_t kLightDirection = 0x00F03140;
const uintptr_t kAmbient = 0x00C39264;
const uintptr_t kTable0 = 0x00E3B5E0;     // LH3DP3::Table0, the clip flags
const uintptr_t kTable1 = 0x00E437E0;     // LH3DP3::Table1, the vertices
// The room of Table0 up to Table1, and the same count of vertices.
const int32_t kMaxVertices = (kTable1 - kTable0) / 4;

bool g_on = true;  // fast_transform
bool g_in = false;
// For the report, on the frame thread.
uint32_t g_clip_calls = 0;
uint32_t g_lit_calls = 0;
uint32_t g_lit_clip_calls = 0;
uint32_t g_group_calls = 0;
uint32_t g_vertices = 0;
uint32_t g_game_loops = 0;
uint16_t g_last_control = 0;
uint32_t g_inexact = 0;
uint32_t g_game_mxcsr = 0;  // the last MXCSR of the game that the loop changed
uint32_t g_mxcsr_sets = 0;

// THE CHECK (fast_transform_check=1, a test key). Each hook makes the SSE
// loop into a copy of the output, and the loop of the game runs. A hook at
// the start of the code after each loop compares the copy with what the game
// wrote, before other code of the game writes the tables again.
bool g_check = false;
uint8_t* g_check_out = nullptr;      // kMaxVertices vertices of 32 bytes
uint32_t* g_check_flags = nullptr;   // kMaxVertices clip flags
uint32_t g_check_count = 0;          // the vertices of the pending copy
int g_check_kind = 0;                // 1 clip, 2 lit, 3 lit clip, 4 group
uint8_t* g_check_dest = nullptr;     // where the game wrote the vertices
uint32_t* g_check_dest_flags = nullptr;
uint32_t g_checked_meshes = 0;
uint32_t g_checked_vertices = 0;
uint32_t g_check_diff_meshes[5] = {0, 0, 0, 0, 0};
uint32_t g_check_diff_vertices = 0;
int g_check_lines = 0;

// Compares the pending copy with what the game wrote.
void ComparePending() {
  if (g_check_count == 0) return;
  const uint32_t n = g_check_count;
  g_check_count = 0;
  ++g_checked_meshes;
  g_checked_vertices += n;
  uint32_t differ = 0;
  for (uint32_t i = 0; i < n; ++i) {
    const uint8_t* want = g_check_dest + i * 32;
    const uint8_t* got = g_check_out + i * 32;
    const bool flags_differ = g_check_dest_flags != nullptr &&
                              g_check_dest_flags[i] != g_check_flags[i];
    if (memcmp(want, got, 32) == 0 && !flags_differ) continue;
    ++differ;
    if (g_check_lines < 12) {
      ++g_check_lines;
      uint32_t a[8], b[8];
      memcpy(a, want, 32);
      memcpy(b, got, 32);
      ModLog("fast transform: check, loop %d, vertex %u of %u: the game "
             "%08X %08X %08X %08X %08X %08X %08X %08X flags %X, SSE %08X "
             "%08X %08X %08X %08X %08X %08X %08X flags %X",
             g_check_kind, i, n, a[0], a[1], a[2], a[3], a[4], a[5], a[6],
             a[7], g_check_dest_flags != nullptr ? g_check_dest_flags[i] : 0,
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             g_check_dest_flags != nullptr ? g_check_flags[i] : 0);
    }
  }
  if (differ > 0) {
    ++g_check_diff_meshes[g_check_kind];
    g_check_diff_vertices += differ;
  }
}

// Gets the copy ready: the bytes that a loop does not write are as in the
// output now.
bool BeginCheck(int kind, uint8_t* dest, uint32_t* dest_flags, uint32_t count) {
  if (g_check_out == nullptr) {
    g_check_out = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, kMaxVertices * 32 + kMaxVertices * 4, MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE));
    if (g_check_out == nullptr) return false;
    g_check_flags = reinterpret_cast<uint32_t*>(g_check_out + kMaxVertices * 32);
  }
  if (count == 0 || count > static_cast<uint32_t>(kMaxVertices)) return false;
  memcpy(g_check_out, dest, count * 32);
  g_check_kind = kind;
  g_check_dest = dest;
  g_check_dest_flags = dest_flags;
  return true;
}

extern "C" void __cdecl FastTransformCompare() { ComparePending(); }

// The hooks of the check at the code after each loop: pushad; call
// FastTransformCompare; popad; jmp dword ptr [original].
const uintptr_t kAfterPlaces[4] = {0x0085713C, 0x0085B05B, 0x0084C0E6,
                                   0x0084C0C1};
void* g_after_original[4] = {nullptr, nullptr, nullptr, nullptr};

bool InstallCheck() {
  uint8_t* thunks = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, 4 * 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (thunks == nullptr) return false;
  for (int i = 0; i < 4; ++i) {
    uint8_t* at = thunks + i * 16;
    at[0] = 0x60;
    at[1] = 0xE8;
    const int32_t rel = static_cast<int32_t>(
        reinterpret_cast<intptr_t>(&FastTransformCompare) -
        reinterpret_cast<intptr_t>(at + 6));
    memcpy(at + 2, &rel, 4);
    at[6] = 0x61;
    at[7] = 0xFF;
    at[8] = 0x25;
    const uint32_t o = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&g_after_original[i]));
    memcpy(at + 9, &o, 4);
    void* target = reinterpret_cast<void*>(kAfterPlaces[i]);
    if (MH_CreateHook(target, at, &g_after_original[i]) != MH_OK ||
        MH_QueueEnableHook(target) != MH_OK) {
      return false;
    }
  }
  FlushInstructionCache(GetCurrentProcess(), thunks, 4 * 16);
  return true;
}

// The targets of the stubs.
void* g_clip_back = nullptr;
void* g_lit_back = nullptr;
uintptr_t g_clip_after = kClipAfter;
uintptr_t g_lit_after = kLitAfter;
void* g_lit_clip_back = nullptr;
void* g_group_back = nullptr;
uintptr_t g_lit_clip_after = kLitClipAfter;
uintptr_t g_group_after = kGroupAfter;
// The bytes that the stub of a group moves esi and edi.
uint32_t g_group_bytes = 0;

template <typename T>
T Read(uintptr_t address) {
  T v;
  memcpy(&v, reinterpret_cast<const void*>(address), sizeof(T));
  return v;
}

// The state of the units for an SSE loop. Begin is true when the SSE loop
// gives the bits of the game: then it sets MXCSR for the loop, and End puts
// the MXCSR of the game back.
struct Units {
  uint32_t game_mxcsr;
  uint32_t loop_mxcsr;

  bool Begin() {
    uint16_t control = 0;
    __asm fnstcw control
    game_mxcsr = _mm_getcsr();
    // Round to the nearest, no flush to zero, no denormals as zero. The
    // masks of the exceptions stay.
    loop_mxcsr = game_mxcsr & ~0xE040u;
    if (!fast_transform_math::Exact(control, loop_mxcsr)) {
      ++g_inexact;
      g_last_control = control;
      return false;
    }
    if (loop_mxcsr != game_mxcsr) {
      _mm_setcsr(loop_mxcsr);
      ++g_mxcsr_sets;
      g_game_mxcsr = game_mxcsr;
    }
    return true;
  }

  void End() const {
    if (loop_mxcsr != game_mxcsr) _mm_setcsr(game_mxcsr);
  }
};

bool Fill(fast_transform_math::Consts* c) {
  const float* m = Read<const float*>(kMatrix);
  if (m == nullptr) return false;
  memcpy(c->m, m, sizeof(c->m));
  c->near_plane = Read<float>(kNearPlane);
  c->half_x = Read<float>(kHalfX);
  c->half_y = Read<float>(kHalfY);
  c->max_x = Read<float>(kMaxScreen);
  c->max_y = Read<float>(kMaxScreen + 4);
  c->color = Read<uint32_t>(kColor);
  c->specular = Read<uint32_t>(kSpecular);
  return true;
}

// The loop of fn_00856D40. Gives 1 when it did the work.
int __cdecl FastClip(const uint8_t* object) {
  const int32_t count = Read<int32_t>(reinterpret_cast<uintptr_t>(object) + 0x10);
  fast_transform_math::Consts c;
  Units units;
  if (!g_on || count > kMaxVertices || !Fill(&c) || !units.Begin()) {
    ++g_game_loops;
    return 0;
  }
  if (count <= 0) {
    units.End();
    return 1;
  }
  const uint8_t* in =
      Read<const uint8_t*>(reinterpret_cast<uintptr_t>(object) + 0x14);
  if (g_check) {
    if (BeginCheck(1, reinterpret_cast<uint8_t*>(kTable1),
                   reinterpret_cast<uint32_t*>(kTable0),
                   static_cast<uint32_t>(count))) {
      fast_transform_math::TransformClip(c, in, static_cast<uint32_t>(count),
                                         g_check_out, g_check_flags);
      g_check_count = static_cast<uint32_t>(count);
    }
    units.End();
    return 0;
  }
  fast_transform_math::TransformClip(
      c, in, static_cast<uint32_t>(count), reinterpret_cast<uint8_t*>(kTable1),
      reinterpret_cast<uint32_t*>(kTable0));
  units.End();
  ++g_clip_calls;
  g_vertices += static_cast<uint32_t>(count);
  return 1;
}

// The loop of fn_0085AB10. Gives 1 when it did the work.
int __cdecl FastLit(const uint8_t* object, uint32_t color) {
  const int32_t count = Read<int32_t>(reinterpret_cast<uintptr_t>(object) + 0x10);
  fast_transform_math::Consts c;
  Units units;
  if (!g_on || count > kMaxVertices || !Fill(&c) || !units.Begin()) {
    ++g_game_loops;
    return 0;
  }
  if (count <= 0) {
    units.End();
    return 1;
  }
  fast_transform_math::Light l;
  memcpy(l.direction, reinterpret_cast<const void*>(kLightDirection),
         sizeof(l.direction));
  l.ambient = Read<int32_t>(kAmbient);
  l.color = color;
  const uint8_t* in =
      Read<const uint8_t*>(reinterpret_cast<uintptr_t>(object) + 0x14);
  if (g_check) {
    if (BeginCheck(2, reinterpret_cast<uint8_t*>(kTable1), nullptr,
                   static_cast<uint32_t>(count))) {
      fast_transform_math::TransformLit(c, l, in, static_cast<uint32_t>(count),
                                        g_check_out);
      g_check_count = static_cast<uint32_t>(count);
    }
    units.End();
    return 0;
  }
  fast_transform_math::TransformLit(c, l, in, static_cast<uint32_t>(count),
                                    reinterpret_cast<uint8_t*>(kTable1));
  units.End();
  ++g_lit_calls;
  g_vertices += static_cast<uint32_t>(count);
  return 1;
}

// The loop of fn_0084BA90 with one matrix. Gives 1 when it did the work.
int __cdecl FastLitClip(const uint8_t* object, uint32_t color) {
  const int32_t count = Read<int32_t>(reinterpret_cast<uintptr_t>(object) + 0x10);
  fast_transform_math::Consts c;
  Units units;
  if (!g_on || count > kMaxVertices || !Fill(&c) || !units.Begin()) {
    ++g_game_loops;
    return 0;
  }
  if (count <= 0) {
    units.End();
    return 1;
  }
  fast_transform_math::Light l;
  memcpy(l.direction, reinterpret_cast<const void*>(kLightDirection),
         sizeof(l.direction));
  l.ambient = Read<int32_t>(kAmbient);
  l.color = color;
  const uint8_t* in =
      Read<const uint8_t*>(reinterpret_cast<uintptr_t>(object) + 0x14);
  if (g_check) {
    if (BeginCheck(3, reinterpret_cast<uint8_t*>(kTable1),
                   reinterpret_cast<uint32_t*>(kTable0),
                   static_cast<uint32_t>(count))) {
      fast_transform_math::TransformLitClip(c, l, false, in,
                                            static_cast<uint32_t>(count),
                                            g_check_out, g_check_flags);
      g_check_count = static_cast<uint32_t>(count);
    }
    units.End();
    return 0;
  }
  fast_transform_math::TransformLitClip(
      c, l, false, in, static_cast<uint32_t>(count),
      reinterpret_cast<uint8_t*>(kTable1), reinterpret_cast<uint32_t*>(kTable0));
  units.End();
  ++g_lit_clip_calls;
  g_vertices += static_cast<uint32_t>(count);
  return 1;
}

// One group of the loop of fn_0084BA90 with a matrix for each group. `frame`
// is ebp of fn_0084BA90. Gives 1 when it did the work, and then
// g_group_bytes is the move of esi and edi.
int __cdecl FastGroup(const uint8_t* in, uint8_t* out, const float* matrix,
                      uint8_t* frame) {
  const uint8_t* group = *reinterpret_cast<uint8_t**>(frame - 0x28);
  const uint32_t count = *reinterpret_cast<const uint16_t*>(group);
  uint32_t* flags = *reinterpret_cast<uint32_t**>(frame - 0x10);
  int32_t* index = reinterpret_cast<int32_t*>(frame - 8);
  fast_transform_math::Consts c;
  Units units;
  if (!g_on || count == 0 ||
      *index + static_cast<int32_t>(count) > kMaxVertices || !Fill(&c) ||
      !units.Begin()) {
    ++g_game_loops;
    return 0;
  }
  memcpy(c.m, matrix, sizeof(c.m));
  fast_transform_math::Light l;
  memcpy(l.direction, frame - 0x44, sizeof(l.direction));
  l.ambient = Read<int32_t>(kAmbient);
  l.color = *reinterpret_cast<const uint32_t*>(frame - 0x30);
  if (g_check) {
    if (BeginCheck(4, out, flags, count)) {
      fast_transform_math::TransformLitClip(c, l, true, in, count, g_check_out,
                                            g_check_flags);
      g_check_count = count;
    }
    units.End();
    return 0;
  }
  fast_transform_math::TransformLitClip(c, l, true, in, count, out, flags);
  units.End();
  *reinterpret_cast<uint32_t**>(frame - 0x10) = flags + count;
  *index += static_cast<int32_t>(count);
  g_group_bytes = 32u * count;
  ++g_group_calls;
  g_vertices += count;
  return 1;
}

// The stubs. popad does not change the flags, so the test of the result
// decides the jump after it.
__declspec(naked) void ClipStub() {
  __asm {
    pushad
    push ebp
    call FastClip
    add esp, 4
    test eax, eax
    popad
    jz clip_game
    jmp dword ptr [g_clip_after]
  clip_game:
    jmp dword ptr [g_clip_back]
  }
}

__declspec(naked) void LitStub() {
  __asm {
    pushad
    push eax
    push ebx
    call FastLit
    add esp, 8
    test eax, eax
    popad
    jz lit_game
    jmp dword ptr [g_lit_after]
  lit_game:
    jmp dword ptr [g_lit_back]
  }
}

__declspec(naked) void LitClipStub() {
  __asm {
    pushad
    push eax
    push ebx
    call FastLitClip
    add esp, 8
    test eax, eax
    popad
    jz lit_clip_game
    jmp dword ptr [g_lit_clip_after]
  lit_clip_game:
    jmp dword ptr [g_lit_clip_back]
  }
}

__declspec(naked) void GroupStub() {
  __asm {
    pushad
    push ebp
    push ebx
    push edi
    push esi
    call FastGroup
    add esp, 16
    test eax, eax
    popad
    jz group_game
    add esi, dword ptr [g_group_bytes]
    add edi, dword ptr [g_group_bytes]
    jmp dword ptr [g_group_after]
  group_game:
    jmp dword ptr [g_group_back]
  }
}

bool Expect(uintptr_t address, const uint8_t* bytes, size_t size) {
  const void* site = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(site, size) && memcmp(site, bytes, size) == 0;
}

}  // namespace

void FastTransformSetCheck(int on) {
  g_check = on != 0;
  if (g_check) {
    ModLog("fast transform: fast_transform_check=1. Each loop runs on the x87 "
           "unit, and the SSE loop makes a copy that the mod compares with it");
  }
}

void FastTransformSetOptions(int on) {
  g_on = on != 0;
  ModLog("fast transform: fast_transform=%d. %s", g_on ? 1 : 0,
         g_on ? "The two vertex loops of the engine run with SSE, four "
                "vertices at a time, with the bits of the game"
              : "The two vertex loops of the engine run on the x87 unit");
}

void FastTransformInstall() {
  if (g_in) return;
  if (!Expect(kClipLoop, kClipBytes, sizeof(kClipBytes)) ||
      !Expect(kLitLoop, kLitBytes, sizeof(kLitBytes)) ||
      !Expect(kLitClipLoop, kLitBytes, sizeof(kLitBytes)) ||
      !Expect(kGroupLoop, kGroupBytes, sizeof(kGroupBytes))) {
    ModLog("fast transform: the code at a loop is not the code of the loops, "
           "so the loops of the game run");
    return;
  }
  void* clip = reinterpret_cast<void*>(kClipLoop);
  void* lit = reinterpret_cast<void*>(kLitLoop);
  MH_STATUS status = MH_CreateHook(clip, reinterpret_cast<void*>(&ClipStub),
                                   &g_clip_back);
  void* lit_clip = reinterpret_cast<void*>(kLitClipLoop);
  void* group = reinterpret_cast<void*>(kGroupLoop);
  if (status == MH_OK) {
    status = MH_CreateHook(lit, reinterpret_cast<void*>(&LitStub), &g_lit_back);
  }
  if (status == MH_OK) {
    status = MH_CreateHook(lit_clip, reinterpret_cast<void*>(&LitClipStub),
                           &g_lit_clip_back);
  }
  if (status == MH_OK) {
    status = MH_CreateHook(group, reinterpret_cast<void*>(&GroupStub),
                           &g_group_back);
  }
  if (status != MH_OK) {
    MH_RemoveHook(clip);
    MH_RemoveHook(lit);
    MH_RemoveHook(lit_clip);
    MH_RemoveHook(group);
  }
  if (status == MH_OK) status = MH_QueueEnableHook(clip);
  if (status == MH_OK) status = MH_QueueEnableHook(lit);
  if (status == MH_OK) status = MH_QueueEnableHook(lit_clip);
  if (status == MH_OK) status = MH_QueueEnableHook(group);
  if (status == MH_OK && g_check && !InstallCheck()) status = MH_UNKNOWN;
  if (status == MH_OK) status = MH_ApplyQueued();
  g_in = status == MH_OK;
  ModLog("hooks        : the vertex loops at 0x%08X, 0x%08X, 0x%08X and "
         "0x%08X %s",
         static_cast<unsigned>(kClipLoop), static_cast<unsigned>(kLitLoop),
         static_cast<unsigned>(kLitClipLoop), static_cast<unsigned>(kGroupLoop),
         g_in ? "ok" : "FAILED");
}

bool FastTransformOn() { return g_on; }

void FastTransformSet(bool on) { g_on = on; }

void FastTransformToggle(const char* keys) {
  g_on = !g_on;
  ControllersGesturePulse(0.5f, g_on ? 0.30f : 0.08f);
  ModLog("test key     : %s, fast_transform is %d from the next mesh", keys,
         g_on ? 1 : 0);
}

// The value of fast_transform for the debug page. With no hooks of the loops
// the switch changes nothing.
int FastTransformValue() {
  if (!g_in) return kDebugCannot;
  return g_on ? 1 : 0;
}

void FastTransformReport(uint64_t frames) {
  if (!g_in || frames == 0) return;
  if (g_check) {
    ComparePending();
    ModLog("fast transform: check: %u meshes and %u vertices compared. "
           "Meshes with a vertex that differs: %u of fn_00856D40, %u of "
           "fn_0085AB10, %u of fn_0084BA90 and %u groups of fn_0084BA90, "
           "%u vertices",
           g_checked_meshes, g_checked_vertices, g_check_diff_meshes[1],
           g_check_diff_meshes[2], g_check_diff_meshes[3],
           g_check_diff_meshes[4], g_check_diff_vertices);
    g_checked_meshes = g_checked_vertices = g_check_diff_vertices = 0;
    memset(g_check_diff_meshes, 0, sizeof(g_check_diff_meshes));
  }
  const double per = static_cast<double>(frames);
  ModLog("fast transform: fast_transform=%d. A frame with SSE: %.0f meshes "
         "of fn_00856D40, %.0f of fn_0085AB10, %.0f of fn_0084BA90 and %.0f "
         "groups of fn_0084BA90, %.0f vertices. %.0f loops of the game%s",
         g_on ? 1 : 0, g_clip_calls / per, g_lit_calls / per,
         g_lit_clip_calls / per, g_group_calls / per, g_vertices / per,
         g_game_loops / per,
         g_on && g_inexact == 0
             ? ". The x87 unit and SSE had the bits of the game"
             : "");
  if (g_inexact > 0) {
    ModLog("fast transform: %u meshes went to the loop of the game, because "
           "the x87 control word was 0x%04X (the SSE loop needs a precision "
           "of 24 bits and rounding to the nearest)",
           g_inexact, g_last_control);
  }
  if (g_mxcsr_sets > 0) {
    ModLog("fast transform: MXCSR of the game was 0x%08X. For %.0f SSE loops "
           "of a frame the mod set it to round to the nearest with no flush "
           "to zero, and put it back after each loop",
           g_game_mxcsr, g_mxcsr_sets / per);
  }
  g_clip_calls = g_lit_calls = g_vertices = g_game_loops = g_inexact = 0;
  g_lit_clip_calls = g_group_calls = 0;
  g_mxcsr_sets = 0;
}
