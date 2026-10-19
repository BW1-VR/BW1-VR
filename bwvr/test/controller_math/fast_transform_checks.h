// THE VERTEX LOOPS WITH SSE (docs/backend.md, "The vertex loops with SSE").
// main.cpp includes this file after Check and Near, just before main.
//
// The references are the x87 code of the game for one vertex, instruction
// by instruction, with the globals of the game in a Consts. Each check
// compares the bits of the SSE functions with them, with the x87 unit at the
// precision of 24 bits that Direct3D sets.
#pragma once

#include <float.h>
#include <stdint.h>
#include <string.h>

#include "../../src/mod/fast_transform_math.h"

namespace fast_transform_checks {

using fast_transform_math::Consts;
using fast_transform_math::Light;

// fn_00856D40 from 0x00856D86 to 0x00856F0D, one vertex. The game has the
// output at edx + 4. Here edx is the output. The names `in` and `out` are
// words of the assembler.
inline void RefClip(const Consts* c, const uint8_t* vertex, uint8_t* result,
                    uint32_t* flags) {
  const float one = 1.0f, zero = 0.0f;
  __asm {
    mov esi, c
    mov ecx, vertex
    mov edx, result
    mov edi, flags
    fld dword ptr [esi + 0x18]
    fmul dword ptr [ecx + 8]
    fld dword ptr [ecx + 4]
    fmul dword ptr [esi + 0xc]
    faddp st(1), st
    fld dword ptr [ecx]
    fmul dword ptr [esi]
    faddp st(1), st
    fadd dword ptr [esi + 0x24]
    fstp dword ptr [edx]
    fld dword ptr [esi + 4]
    fmul dword ptr [ecx]
    fld dword ptr [esi + 0x1c]
    fmul dword ptr [ecx + 8]
    faddp st(1), st
    fld dword ptr [ecx + 4]
    fmul dword ptr [esi + 0x10]
    faddp st(1), st
    fadd dword ptr [esi + 0x28]
    fstp dword ptr [edx + 4]
    fld dword ptr [esi + 8]
    fmul dword ptr [ecx]
    fld dword ptr [esi + 0x20]
    fmul dword ptr [ecx + 8]
    faddp st(1), st
    fld dword ptr [ecx + 4]
    fmul dword ptr [esi + 0x14]
    faddp st(1), st
    fadd dword ptr [esi + 0x2c]
    fstp dword ptr [edx + 0xc]
    mov eax, [esi + 0x44]
    mov [edx + 0x10], eax
    mov eax, [esi + 0x48]
    mov [edx + 0x14], eax
    mov eax, [ecx + 0xc]
    mov [edx + 0x18], eax
    mov eax, [ecx + 0x10]
    mov [edx + 0x1c], eax
    fld dword ptr [edx + 0xc]
    fcomp dword ptr [esi + 0x30]
    fnstsw ax
    test ah, 1
    je c_far
    mov dword ptr [edi], 0x20
    jmp c_x
  c_far:
    mov dword ptr [edi], 0
  c_x:
    fld dword ptr [edx]
    fcomp dword ptr [edx + 0xc]
    fnstsw ax
    test ah, 0x41
    jne c_x_left
    mov eax, [edi]
    or eax, 0x10
    jmp c_x_store
  c_x_left:
    fld dword ptr [edx + 0xc]
    fchs
    fcomp dword ptr [edx]
    fnstsw ax
    test ah, 0x41
    jne c_y
    mov eax, [edi]
    or eax, 8
  c_x_store:
    mov [edi], eax
  c_y:
    fld dword ptr [edx + 4]
    fcomp dword ptr [edx + 0xc]
    fnstsw ax
    test ah, 0x41
    jne c_y_up
    mov eax, [edi]
    or eax, 4
    jmp c_y_store
  c_y_up:
    fld dword ptr [edx + 0xc]
    fchs
    fcomp dword ptr [edx + 4]
    fnstsw ax
    test ah, 0x41
    jne c_flags
    mov eax, [edi]
    or eax, 2
  c_y_store:
    mov [edi], eax
  c_flags:
    cmp dword ptr [edi], 0
    jne c_done
    fld one
    fdiv dword ptr [edx + 0xc]
    fst dword ptr [edx + 0xc]
    fmul dword ptr [edx]
    fadd one
    fmul dword ptr [esi + 0x34]
    fst dword ptr [edx]
    fcomp zero
    fnstsw ax
    test ah, 1
    je c_sx_top
    mov dword ptr [edx], 0
    jmp c_sy
  c_sx_top:
    fld dword ptr [edx]
    fcomp dword ptr [esi + 0x3c]
    fnstsw ax
    test ah, 0x41
    jne c_sy
    mov eax, [esi + 0x3c]
    mov [edx], eax
  c_sy:
    fld dword ptr [esi + 0x38]
    fmul dword ptr [edx + 4]
    fmul dword ptr [edx + 0xc]
    fsubr dword ptr [esi + 0x38]
    fst dword ptr [edx + 4]
    fcomp zero
    fnstsw ax
    test ah, 1
    je c_sy_top
    mov dword ptr [edx + 4], 0
    jmp c_depth
  c_sy_top:
    fld dword ptr [edx + 4]
    fcomp dword ptr [esi + 0x40]
    fnstsw ax
    test ah, 0x41
    jne c_depth
    mov eax, [esi + 0x40]
    mov [edx + 4], eax
  c_depth:
    fld dword ptr [esi + 0x30]
    fmul dword ptr [edx + 0xc]
    fst dword ptr [edx + 0xc]
    fld one
    fsub st, st(1)
    fstp dword ptr [edx + 8]
    fstp st(0)
  c_done:
  }
}

// 0x0085AC3B to 0x0085AC95 (and 0x0084BBC0 to 0x0084BC1D): the color from
// the level of the light.
inline uint32_t RefDiffuse(int32_t level, const Light* l) {
  const int32_t ambient = l->ambient;
  const uint32_t color = l->color;
  uint32_t diffuse = 0;
  __asm {
    mov eax, level
    test eax, eax
    jge d_lit
    mov eax, ambient
    jmp d_color
  d_lit:
    mov edx, ambient
    mov eax, 0xff
    sub eax, edx
    imul eax, level
    sar eax, 8
    add eax, edx
  d_color:
    mov esi, color
    and esi, 0xff0000
    imul esi, eax
    and esi, 0xff0000ff
    mov edx, color
    and edx, 0xff00
    imul edx, eax
    and edx, 0xff0000
    or edx, esi
    mov esi, color
    and esi, 0xff
    imul esi, eax
    and esi, 0xff00
    or edx, esi
    shr edx, 8
    mov eax, color
    and eax, 0xff000000
    or edx, eax
    mov diffuse, edx
  }
  return diffuse;
}

// fn_0085AB10 from 0x0085ABAF to 0x0085AD4F, one vertex. The game has the
// output at ecx + 4. Here ecx is the output.
inline void RefLit(const Consts* c, const Light* l, const uint8_t* vertex,
                   uint8_t* result) {
  const float one = 1.0f, zero = 0.0f, full = 255.0f;
  const float* dir = l->direction;
  float rounded = 0.0f;
  int32_t level = 0;
  __asm {
    mov esi, c
    mov edi, vertex
    mov ecx, result
    mov edx, dir
    fld dword ptr [esi + 0x18]
    fmul dword ptr [edi + 8]
    fld dword ptr [esi + 0xc]
    fmul dword ptr [edi + 4]
    faddp st(1), st
    fld dword ptr [edi]
    fmul dword ptr [esi]
    faddp st(1), st
    fadd dword ptr [esi + 0x24]
    fstp dword ptr [ecx]
    fld dword ptr [esi + 0x1c]
    fmul dword ptr [edi + 8]
    fld dword ptr [esi + 4]
    fmul dword ptr [edi]
    faddp st(1), st
    fld dword ptr [esi + 0x10]
    fmul dword ptr [edi + 4]
    faddp st(1), st
    fadd dword ptr [esi + 0x28]
    fstp dword ptr [ecx + 4]
    fld dword ptr [esi + 0x20]
    fmul dword ptr [edi + 8]
    fld dword ptr [esi + 8]
    fmul dword ptr [edi]
    faddp st(1), st
    fld dword ptr [esi + 0x14]
    fmul dword ptr [edi + 4]
    faddp st(1), st
    fadd dword ptr [esi + 0x2c]
    mov eax, [esi + 0x48]
    fstp dword ptr [ecx + 0xc]
    mov [ecx + 0x14], eax
    fld dword ptr [edx + 8]
    fmul dword ptr [edi + 0x1c]
    fld dword ptr [edx]
    fmul dword ptr [edi + 0x14]
    faddp st(1), st
    fld dword ptr [edx + 4]
    fmul dword ptr [edi + 0x18]
    faddp st(1), st
    fmul full
    fstp rounded
    fld rounded
    fistp level
    mov eax, [edi + 0xc]
    mov [ecx + 0x18], eax
    mov eax, [edi + 0x10]
    mov [ecx + 0x1c], eax
    fld one
    fdiv dword ptr [ecx + 0xc]
    fst dword ptr [ecx + 0xc]
    fmul dword ptr [ecx]
    fadd one
    fmul dword ptr [esi + 0x34]
    fst dword ptr [ecx]
    fcomp zero
    fnstsw ax
    test ah, 1
    je l_sx_top
    mov dword ptr [ecx], 0
    jmp l_sy
  l_sx_top:
    fld dword ptr [ecx]
    fcomp dword ptr [esi + 0x3c]
    fnstsw ax
    test ah, 0x41
    jne l_sy
    mov eax, [esi + 0x3c]
    mov [ecx], eax
  l_sy:
    fld dword ptr [ecx + 4]
    fmul dword ptr [ecx + 0xc]
    fmul dword ptr [esi + 0x38]
    fsubr dword ptr [esi + 0x38]
    fst dword ptr [ecx + 4]
    fcomp zero
    fnstsw ax
    test ah, 1
    je l_sy_top
    mov dword ptr [ecx + 4], 0
    jmp l_depth
  l_sy_top:
    fld dword ptr [ecx + 4]
    fcomp dword ptr [esi + 0x40]
    fnstsw ax
    test ah, 0x41
    jne l_depth
    mov eax, [esi + 0x40]
    mov [ecx + 4], eax
  l_depth:
    fld dword ptr [esi + 0x30]
    fmul dword ptr [ecx + 0xc]
    fst dword ptr [ecx + 0xc]
    fld one
    fsub st, st(1)
    fstp dword ptr [ecx + 8]
    fstp st(0)
  }
  const uint32_t diffuse = RefDiffuse(level, l);
  memcpy(result + 16, &diffuse, 4);
}

// fn_0084BA90, one vertex: the loop with one matrix from 0x0084BB33 to
// 0x0084BD40 (group false), or the loop with a matrix for each group from
// 0x0084BE97 to 0x0084C099 (group true: Consts::m is the matrix of the
// group). The game has the output at ecx + 4 or at edi. Here ecx is the
// output.
inline void RefLitClip(const Consts* c, const Light* l, bool group,
                       const uint8_t* vertex, uint8_t* result,
                       uint32_t* flags) {
  const float one = 1.0f, zero = 0.0f, full = 255.0f;
  const float* dir = l->direction;
  float rounded = 0.0f;
  int32_t level = 0;
  if (!group) {
    __asm {
      mov ebx, c
      mov esi, vertex
      mov ecx, result
      fld dword ptr [ebx + 0x18]
      fmul dword ptr [esi + 8]
      fld dword ptr [ebx + 0xc]
      fmul dword ptr [esi + 4]
      faddp st(1), st
      fld dword ptr [esi]
      fmul dword ptr [ebx]
      faddp st(1), st
      fadd dword ptr [ebx + 0x24]
      fstp dword ptr [ecx]
      fld dword ptr [ebx + 0x1c]
      fmul dword ptr [esi + 8]
      fld dword ptr [ebx + 4]
      fmul dword ptr [esi]
      faddp st(1), st
      fld dword ptr [ebx + 0x10]
      fmul dword ptr [esi + 4]
      faddp st(1), st
      fadd dword ptr [ebx + 0x28]
      fstp dword ptr [ecx + 4]
      fld dword ptr [ebx + 0x20]
      fmul dword ptr [esi + 8]
      fld dword ptr [ebx + 8]
      fmul dword ptr [esi]
      faddp st(1), st
      fld dword ptr [ebx + 0x14]
      fmul dword ptr [esi + 4]
      faddp st(1), st
      fadd dword ptr [ebx + 0x2c]
      fstp dword ptr [ecx + 0xc]
    }
  } else {
    __asm {
      mov ebx, c
      mov esi, vertex
      mov ecx, result
      fld dword ptr [ebx + 0x18]
      fmul dword ptr [esi + 8]
      fld dword ptr [esi]
      fmul dword ptr [ebx]
      faddp st(1), st
      fld dword ptr [ebx + 0xc]
      fmul dword ptr [esi + 4]
      faddp st(1), st
      fadd dword ptr [ebx + 0x24]
      fstp dword ptr [ecx]
      fld dword ptr [ebx + 0x10]
      fmul dword ptr [esi + 4]
      fld dword ptr [esi]
      fmul dword ptr [ebx + 4]
      faddp st(1), st
      fld dword ptr [ebx + 0x1c]
      fmul dword ptr [esi + 8]
      faddp st(1), st
      fadd dword ptr [ebx + 0x28]
      fstp dword ptr [ecx + 4]
      fld dword ptr [ebx + 0x14]
      fmul dword ptr [esi + 4]
      fld dword ptr [ebx + 8]
      fmul dword ptr [esi]
      faddp st(1), st
      fld dword ptr [ebx + 0x20]
      fmul dword ptr [esi + 8]
      faddp st(1), st
      fadd dword ptr [ebx + 0x2c]
      fstp dword ptr [ecx + 0xc]
    }
  }
  __asm {
    mov ebx, c
    mov esi, vertex
    mov ecx, result
    mov edi, flags
    mov edx, [ebx + 0x48]
    mov [ecx + 0x14], edx
    mov edx, dir
    fld dword ptr [edx + 8]
    fmul dword ptr [esi + 0x1c]
    fld dword ptr [edx]
    fmul dword ptr [esi + 0x14]
    faddp st(1), st
    fld dword ptr [edx + 4]
    fmul dword ptr [esi + 0x18]
    faddp st(1), st
    fmul full
    fstp rounded
    fld rounded
    fistp level
    mov edx, [esi + 0xc]
    mov [ecx + 0x18], edx
    mov edx, [esi + 0x10]
    mov [ecx + 0x1c], edx
    fld dword ptr [ecx + 0xc]
    fcomp dword ptr [ebx + 0x30]
    fnstsw ax
    test ah, 1
    je k_far
    mov dword ptr [edi], 0x20
    jmp k_x
  k_far:
    mov dword ptr [edi], 0
  k_x:
    fld dword ptr [ecx]
    fcomp dword ptr [ecx + 0xc]
    fnstsw ax
    test ah, 0x41
    jne k_x_left
    mov eax, [edi]
    or eax, 0x10
    jmp k_x_store
  k_x_left:
    fld dword ptr [ecx + 0xc]
    fchs
    fcomp dword ptr [ecx]
    fnstsw ax
    test ah, 0x41
    jne k_y
    mov eax, [edi]
    or eax, 8
  k_x_store:
    mov [edi], eax
  k_y:
    fld dword ptr [ecx + 4]
    fcomp dword ptr [ecx + 0xc]
    fnstsw ax
    test ah, 0x41
    jne k_y_up
    mov eax, [edi]
    or eax, 4
    jmp k_y_store
  k_y_up:
    fld dword ptr [ecx + 0xc]
    fchs
    fcomp dword ptr [ecx + 4]
    fnstsw ax
    test ah, 0x41
    jne k_flags
    mov eax, [edi]
    or eax, 2
  k_y_store:
    mov [edi], eax
  k_flags:
    cmp dword ptr [edi], 0
    jne k_done
    fld one
    fdiv dword ptr [ecx + 0xc]
    fst dword ptr [ecx + 0xc]
    fmul dword ptr [ecx]
    fadd one
    fmul dword ptr [ebx + 0x34]
    fst dword ptr [ecx]
    fcomp zero
    fnstsw ax
    test ah, 1
    je k_sx_top
    mov dword ptr [ecx], 0
    jmp k_sy
  k_sx_top:
    fld dword ptr [ecx]
    fcomp dword ptr [ebx + 0x3c]
    fnstsw ax
    test ah, 0x41
    jne k_sy
    mov eax, [ebx + 0x3c]
    mov [ecx], eax
  k_sy:
    fld dword ptr [ebx + 0x38]
    fmul dword ptr [ecx + 0xc]
    fmul dword ptr [ecx + 4]
    fsubr dword ptr [ebx + 0x38]
    fst dword ptr [ecx + 4]
    fcomp zero
    fnstsw ax
    test ah, 1
    je k_sy_top
    mov dword ptr [ecx + 4], 0
    jmp k_depth
  k_sy_top:
    fld dword ptr [ecx + 4]
    fcomp dword ptr [ebx + 0x40]
    fnstsw ax
    test ah, 0x41
    jne k_depth
    mov eax, [ebx + 0x40]
    mov [ecx + 4], eax
  k_depth:
    fld dword ptr [ebx + 0x30]
    fmul dword ptr [ecx + 0xc]
    fst dword ptr [ecx + 0xc]
    fld one
    fsub st, st(1)
    fstp dword ptr [ecx + 8]
    fstp st(0)
  k_done:
  }
  const uint32_t diffuse = RefDiffuse(level, l);
  memcpy(result + 16, &diffuse, 4);
}

// A small random generator, so the checks are the same in each run.
struct Random {
  uint32_t state;
  uint32_t Next() {
    state = state * 1664525u + 1013904223u;
    return state;
  }
  float Range(float lo, float hi) {
    return lo + (hi - lo) * static_cast<float>(Next() >> 8) / 16777216.0f;
  }
};

inline void FillVertex(Random* r, uint8_t* v, int kind) {
  float f[8];
  // Most points are in front of the camera, some are behind it, near the
  // near plane, on the edges of the view or at a z of 0.
  f[0] = r->Range(-3000.0f, 3000.0f);
  f[1] = r->Range(-500.0f, 1500.0f);
  f[2] = r->Range(-3000.0f, 3000.0f);
  if (kind == 1) f[0] = f[1] = f[2] = 0.0f;
  f[3] = r->Range(0.0f, 1.0f);
  f[4] = r->Range(0.0f, 1.0f);
  // A normal of length about 1, and some of length 0 or more.
  f[5] = r->Range(-1.0f, 1.0f);
  f[6] = r->Range(-1.0f, 1.0f);
  f[7] = r->Range(-1.0f, 1.0f);
  if (kind == 2) f[5] = f[6] = f[7] = 0.0f;
  memcpy(v, f, 32);
}

inline void MakeConsts(Random* r, Consts* c) {
  // A camera matrix of the game: a rotation scaled by the field of view,
  // and a translation.
  for (int i = 0; i < 9; ++i) c->m[i] = r->Range(-1.3f, 1.3f);
  c->m[9] = r->Range(-2000.0f, 2000.0f);
  c->m[10] = r->Range(-2000.0f, 2000.0f);
  c->m[11] = r->Range(-500.0f, 3000.0f);
  c->near_plane = r->Range(0.5f, 20.0f);
  c->half_x = r->Range(300.0f, 1500.0f);
  c->half_y = r->Range(200.0f, 1300.0f);
  c->max_x = c->half_x * 2.0f - 1.0f;
  c->max_y = c->half_y * 2.0f - 1.0f;
  c->color = r->Next();
  c->specular = r->Next();
}

inline void FastTransformChecks() {
  printf("\nThe vertex loops with SSE\n");
  using fast_transform_math::Exact;
  Check(Exact(0x027F, 0x1F80) == false,
        "a precision of 53 bits is not exact (0x027F)", 0, 0);
  Check(Exact(0x007F, 0x1F80), "24 bits and rounding to the nearest is exact",
        0, 0);
  Check(!Exact(0x047F, 0x1F80), "rounding down on the x87 unit is not exact",
        0, 0);
  Check(!Exact(0x007F, 0x9F80), "a flush to zero of SSE is not exact", 0, 0);

  unsigned int saved = 0;
  _controlfp_s(&saved, 0, 0);
  unsigned int now = 0;
  _controlfp_s(&now, _PC_24 | _RC_NEAR, _MCW_PC | _MCW_RC);

  Random r = {12345u};
  const uint32_t kCount = 4099;  // not a multiple of 4
  static uint8_t in[kCount * 32];
  static uint8_t want[kCount * 32];
  static uint8_t got[kCount * 32];
  static uint32_t want_flags[kCount];
  static uint32_t got_flags[kCount];
  int clip_diff = 0, lit_diff = 0, flag_counts[6] = {0, 0, 0, 0, 0, 0};
  int lit_clip_diff[2] = {0, 0};
  int projected = 0;
  for (int round = 0; round < 8; ++round) {
    Consts c;
    MakeConsts(&r, &c);
    for (uint32_t i = 0; i < kCount; ++i) {
      FillVertex(&r, in + i * 32, i % 97 == 5 ? 1 : (i % 89 == 7 ? 2 : 0));
    }
    // A z of exactly the near plane and of exactly the edges.
    float* v = reinterpret_cast<float*>(in);
    v[0] = v[1] = v[2] = 0.0f;
    // The bytes that a vertex with a flag does not write must stay.
    memset(want, 0xA5, sizeof(want));
    memset(got, 0xA5, sizeof(got));
    for (uint32_t i = 0; i < kCount; ++i) {
      RefClip(&c, in + i * 32, want + i * 32, &want_flags[i]);
    }
    fast_transform_math::TransformClip(c, in, kCount, got, got_flags);
    for (uint32_t i = 0; i < kCount; ++i) {
      if (memcmp(want + i * 32, got + i * 32, 32) != 0 ||
          want_flags[i] != got_flags[i]) {
        ++clip_diff;
      }
      for (int b = 0; b < 6; ++b) {
        if (want_flags[i] & (0x01u << b)) ++flag_counts[b];
      }
      if (want_flags[i] == 0) ++projected;
    }
    Light l;
    l.direction[0] = r.Range(-1.0f, 1.0f);
    l.direction[1] = r.Range(-1.0f, 1.0f);
    l.direction[2] = r.Range(-1.0f, 1.0f);
    l.ambient = static_cast<int32_t>(r.Next() % 256);
    l.color = r.Next();
    memset(want, 0x5A, sizeof(want));
    memset(got, 0x5A, sizeof(got));
    for (uint32_t i = 0; i < kCount; ++i) {
      RefLit(&c, &l, in + i * 32, want + i * 32);
    }
    fast_transform_math::TransformLit(c, l, in, kCount, got);
    for (uint32_t i = 0; i < kCount; ++i) {
      if (memcmp(want + i * 32, got + i * 32, 32) != 0) ++lit_diff;
    }
    for (int group = 0; group < 2; ++group) {
      memset(want, 0xC3, sizeof(want));
      memset(got, 0xC3, sizeof(got));
      for (uint32_t i = 0; i < kCount; ++i) {
        RefLitClip(&c, &l, group != 0, in + i * 32, want + i * 32,
                   &want_flags[i]);
      }
      fast_transform_math::TransformLitClip(c, l, group != 0, in, kCount, got,
                                            got_flags);
      for (uint32_t i = 0; i < kCount; ++i) {
        if (memcmp(want + i * 32, got + i * 32, 32) != 0 ||
            want_flags[i] != got_flags[i]) {
          ++lit_clip_diff[group];
        }
      }
    }
  }
  // Meshes of 1 to 7 vertices: each count of vertices after the blocks of 4.
  int small_diff = 0;
  for (uint32_t small = 1; small <= 7; ++small) {
    Consts c;
    MakeConsts(&r, &c);
    Light l = {{0.3f, -0.8f, 0.5f}, 90, 0xFF80C040u};
    for (uint32_t i = 0; i < small; ++i) FillVertex(&r, in + i * 32, 0);
    memset(want, 0x33, 8 * 32);
    memset(got, 0x33, 8 * 32);
    for (uint32_t i = 0; i < small; ++i) {
      RefClip(&c, in + i * 32, want + i * 32, &want_flags[i]);
    }
    fast_transform_math::TransformClip(c, in, small, got, got_flags);
    if (memcmp(want, got, 8 * 32) != 0 ||
        memcmp(want_flags, got_flags, small * 4) != 0) {
      ++small_diff;
    }
    for (uint32_t i = 0; i < small; ++i) {
      RefLit(&c, &l, in + i * 32, want + i * 32);
    }
    fast_transform_math::TransformLit(c, l, in, small, got);
    if (memcmp(want, got, 8 * 32) != 0) ++small_diff;
    for (uint32_t i = 0; i < small; ++i) {
      RefLitClip(&c, &l, true, in + i * 32, want + i * 32, &want_flags[i]);
    }
    fast_transform_math::TransformLitClip(c, l, true, in, small, got,
                                          got_flags);
    if (memcmp(want, got, 8 * 32) != 0 ||
        memcmp(want_flags, got_flags, small * 4) != 0) {
      ++small_diff;
    }
  }
  _controlfp_s(&now, saved, _MCW_PC | _MCW_RC);
  Near("meshes of 1 to 7 vertices with other bits, and other bytes past "
       "the mesh",
       small_diff, 0, 0);

  Check(projected > 1000 && flag_counts[5] > 1000 && flag_counts[4] > 1000 &&
            flag_counts[3] > 1000 && flag_counts[2] > 100 &&
            flag_counts[1] > 100,
        "the vertices have each clip flag and many have none", projected,
        flag_counts[5]);
  Near("TransformClip: vertices with other bits than the x87 code of the "
       "game, of 8 times 4099",
       clip_diff, 0, 0);
  Near("TransformLit: vertices with other bits than the x87 code of the "
       "game, of 8 times 4099",
       lit_diff, 0, 0);
  Near("TransformLitClip with one matrix: vertices with other bits than the "
       "x87 code of the game, of 8 times 4099",
       lit_clip_diff[0], 0, 0);
  Near("TransformLitClip with a matrix for each group: vertices with other "
       "bits than the x87 code of the game, of 8 times 4099",
       lit_clip_diff[1], 0, 0);
}

}  // namespace fast_transform_checks
