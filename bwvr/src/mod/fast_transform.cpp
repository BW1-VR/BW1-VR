// The two vertex loops of the engine with SSE (docs/backend.md, "The vertex
// loops with SSE"). The math is in fast_transform_math.h.
//
// WHERE. fn_00856D40 and fn_0085AB10 each transform the vertices of a mesh
// in a loop on the x87 unit, then build the triangles. Each has a second loop
// for a mesh with a matrix for each group of vertices (bit 0 of *0x00FA93BC),
// which is rare. A jump at the start of the loop with one matrix goes to a
// stub here. The stub keeps each register, and calls the SSE loop. When the
// SSE loop did the work, the stub goes on at the code after the loop. Else it
// goes back into the loop of the game, through the trampoline of MinHook.
//
//   fn_00856D40  0x00856D6E  mov eax,[ebp+10h]; xor ebx,ebx   after: 0x0085713C
//   fn_0085AB10  0x0085AB64  mov ecx,[ebx+10h]; test ecx,ecx  after: 0x0085B05B
//
// The code after each loop reads only the object (ebp, or ebx) and sets each
// other register and local that it uses. At the start of the loop of
// fn_0085AB10, eax has the color of the mesh.
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
uint32_t g_vertices = 0;
uint32_t g_game_loops = 0;
uint16_t g_last_control = 0;
uint32_t g_inexact = 0;
uint32_t g_game_mxcsr = 0;  // the last MXCSR of the game that the loop changed
uint32_t g_mxcsr_sets = 0;

// The targets of the stubs.
void* g_clip_back = nullptr;
void* g_lit_back = nullptr;
uintptr_t g_clip_after = kClipAfter;
uintptr_t g_lit_after = kLitAfter;

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
  fast_transform_math::TransformLit(c, l, in, static_cast<uint32_t>(count),
                                    reinterpret_cast<uint8_t*>(kTable1));
  units.End();
  ++g_lit_calls;
  g_vertices += static_cast<uint32_t>(count);
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

bool Expect(uintptr_t address, const uint8_t* bytes, size_t size) {
  const void* site = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(site, size) && memcmp(site, bytes, size) == 0;
}

}  // namespace

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
      !Expect(kLitLoop, kLitBytes, sizeof(kLitBytes))) {
    ModLog("fast transform: the code at 0x%08X or 0x%08X is not the code of "
           "the loops, so the loops of the game run",
           static_cast<unsigned>(kClipLoop), static_cast<unsigned>(kLitLoop));
    return;
  }
  void* clip = reinterpret_cast<void*>(kClipLoop);
  void* lit = reinterpret_cast<void*>(kLitLoop);
  MH_STATUS status = MH_CreateHook(clip, reinterpret_cast<void*>(&ClipStub),
                                   &g_clip_back);
  if (status == MH_OK) {
    status = MH_CreateHook(lit, reinterpret_cast<void*>(&LitStub), &g_lit_back);
    if (status != MH_OK) MH_RemoveHook(clip);
  }
  if (status == MH_OK) status = MH_QueueEnableHook(clip);
  if (status == MH_OK) status = MH_QueueEnableHook(lit);
  if (status == MH_OK) status = MH_ApplyQueued();
  g_in = status == MH_OK;
  ModLog("hooks        : the vertex loops at 0x%08X and 0x%08X %s",
         static_cast<unsigned>(kClipLoop), static_cast<unsigned>(kLitLoop),
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

void FastTransformReport(uint64_t frames) {
  if (!g_in || frames == 0) return;
  const double per = static_cast<double>(frames);
  ModLog("fast transform: fast_transform=%d. A frame: %.0f meshes of "
         "fn_00856D40 and %.0f of fn_0085AB10 with SSE, %.0f vertices, %.0f "
         "loops of the game%s",
         g_on ? 1 : 0, g_clip_calls / per, g_lit_calls / per,
         g_vertices / per, g_game_loops / per,
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
  g_mxcsr_sets = 0;
}
