// The samples of the scene walk, a test (scene_sample_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/scene_sample_math.h"

namespace scene_sample_checks {

inline void SceneSampleChecks() {
  using namespace scene_sample;
  printf("\nThe samples of the scene walk\n");

  Module modules[3] = {{0x10000000u, 0x1000u},
                       {0x00400000u, 0x800000u},
                       {0x77000000u, 0x100000u}};
  int order[3];
  SortModules(modules, 3, order);
  Check(modules[0].base == 0x00400000u && modules[1].base == 0x10000000u &&
            modules[2].base == 0x77000000u,
        "the modules are sorted by base", 0, 0);
  Check(order[0] == 1 && order[1] == 0 && order[2] == 2,
        "the order gives the place of each module before the sort", 0, 0);
  Near("an address of the game is in its module",
       ModuleOf(0x0082F810u, modules, 3), 0, 0);
  Near("the first address of a module is in it",
       ModuleOf(0x10000000u, modules, 3), 1, 0);
  Near("the last address of a module is in it",
       ModuleOf(0x10000FFFu, modules, 3), 1, 0);
  Near("the address after a module is in no module",
       ModuleOf(0x10001000u, modules, 3), -1, 0);
  Near("an address under each module is in no module",
       ModuleOf(0x00001000u, modules, 3), -1, 0);
  Near("an address over each module is in no module",
       ModuleOf(0x7F000000u, modules, 3), -1, 0);

  // Ten samples: 5 in one bucket, 3 in a second, 2 in a third.
  uint32_t samples[10] = {0x00500010u, 0x00600000u, 0x00500020u, 0x00700004u,
                          0x00500030u, 0x00600030u, 0x0050003Fu, 0x00700008u,
                          0x00600010u, 0x00500000u};
  Bucket top[2];
  const int got = TopBuckets(samples, 10, top, 2);
  Near("two buckets are asked, and two come", got, 2, 0);
  Check(top[0].first == 0x00500000u && top[0].count == 5,
        "the bucket with the most samples is first", 0, 0);
  Check(top[1].first == 0x00600000u && top[1].count == 3,
        "the bucket with the second most is second", 0, 0);
  Bucket all[8];
  Near("each bucket comes when more are asked", TopBuckets(samples, 10, all, 8),
       3, 0);
  Near("no sample gives no bucket", TopBuckets(samples, 0, all, 8), 0, 0);
  uint32_t exact[5] = {0x00500010u, 0x00500011u, 0x00500010u, 0x00500012u,
                       0x00500010u};
  Bucket one[2];
  Near("a shift of 0 counts each address alone",
       TopBuckets(exact, 5, one, 2, 0), 2, 0);
  Check(one[0].first == 0x00500010u && one[0].count == 3,
        "the address with the most samples is first", 0, 0);

  // The bytes before a return address.
  const uint8_t near_call[7] = {0x90, 0x90, 0xE8, 0x10, 0x20, 0x30, 0x40};
  Check(EndsWithCall(near_call), "E8 rel32 is a call", 0, 0);
  const uint8_t reg_call[7] = {0x90, 0x90, 0x90, 0x90, 0x90, 0xFF, 0xD0};
  Check(EndsWithCall(reg_call), "FF D0 (call eax) is a call", 0, 0);
  const uint8_t disp8_call[7] = {0x90, 0x90, 0x90, 0x90, 0xFF, 0x50, 0x08};
  Check(EndsWithCall(disp8_call), "FF 50 08 (call [eax+8]) is a call", 0, 0);
  const uint8_t vtable_call[7] = {0x90, 0xFF, 0x90, 0x48, 0x01, 0x00, 0x00};
  Check(EndsWithCall(vtable_call), "FF 90 disp32 (call [eax+0x148]) is a call",
        0, 0);
  const uint8_t sib_call[7] = {0x90, 0x90, 0x90, 0xFF, 0x54, 0x24, 0x08};
  Check(EndsWithCall(sib_call), "FF 54 24 08 (call [esp+8]) is a call", 0, 0);
  const uint8_t abs_call[7] = {0x90, 0xFF, 0x15, 0x00, 0x10, 0x40, 0x00};
  Check(EndsWithCall(abs_call), "FF 15 abs32 (call [import]) is a call", 0, 0);
  const uint8_t jump[7] = {0x90, 0x90, 0x90, 0x90, 0x90, 0xFF, 0xE0};
  Check(!EndsWithCall(jump), "FF E0 (jmp eax) is not a call", 0, 0);
  const uint8_t short_disp[7] = {0x90, 0x90, 0x90, 0xFF, 0x50, 0x08, 0x90};
  Check(!EndsWithCall(short_disp),
        "a call that ends one byte early is not a call", 0, 0);
  const uint8_t data[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  Check(!EndsWithCall(data), "zeros are not a call", 0, 0);
}

}  // namespace scene_sample_checks
