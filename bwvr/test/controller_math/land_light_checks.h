// The lights on the land in the second eye pass (land_light_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <string.h>

#include "../../src/mod/land_light_math.h"

namespace land_light_checks {

// A light with the given parts from `seed`, and the parts that the draw of
// the land writes from `drawn`.
inline void MakeLight(uint8_t* light, uint8_t seed, uint8_t drawn) {
  for (int i = 0; i < land_light::kLightBytes; ++i) {
    const bool given = i < land_light::kGivenFirstEnd ||
                       i >= land_light::kGivenSecondStart;
    light[i] = static_cast<uint8_t>(given ? seed + i : drawn);
  }
}

inline void LandLightChecks() {
  using namespace land_light;
  printf("\nThe lights on the land in the second eye pass\n");

  static uint8_t list[kCapacity * kLightBytes];
  static Held step;
  static Held before;

  // Two lights are the same light when the given parts are the same.
  {
    uint8_t a[kLightBytes], b[kLightBytes];
    MakeLight(a, 10, 0);
    MakeLight(b, 10, 99);
    Check(SameLight(a, b), "a light is the same light after the draw of the "
          "land wrote its parts", 0, 0);
    b[0x2C] ^= 1;
    Check(!SameLight(a, b), "a different strength is a different light", 0, 0);
    MakeLight(b, 10, 0);
    b[0] ^= 1;
    Check(!SameLight(a, b), "a different place is a different light", 0, 0);
  }

  // A frame with a lightning. Before the passes the list has 1 light of a
  // dance. In the first pass the particles add 2 lights, the step of the
  // atmosphere adds 1 light, and the draw of the land adds 1 light.
  memset(list, 0, sizeof(list));
  Clear(&step);
  Clear(&before);
  MakeLight(list + 0 * kLightBytes, 1, 0);   // the dance
  Near("the light from before the passes is held", Hold(&before, list, 0, 1),
       1, 0);
  MakeLight(list + 1 * kLightBytes, 2, 0);   // the particles
  MakeLight(list + 2 * kLightBytes, 3, 0);
  MakeLight(list + 3 * kLightBytes, 4, 0);   // the lightning
  Near("the light of the step is held", Hold(&step, list, 3, 4), 1, 0);
  Near("it was light 3 of the list", step.index[0], 3, 0);
  MakeLight(list + 4 * kLightBytes, 5, 0);   // the draw of the land
  Near("a step that adds no light holds nothing", Hold(&step, list, 5, 5), 0,
       0);
  Near("a count that went down holds nothing", Hold(&step, list, 5, 2), 0, 0);
  Near("a count above the room of the list holds nothing",
       Hold(&step, list, 199, 201), 0, 0);
  Near("the step still holds 1 light", step.count, 1, 0);

  // The first pass did not empty the list (no draw of the land): the second
  // pass adds nothing.
  {
    int count = 5;
    Check(StillInList(step, list, count), "the list still has the light of "
          "the step when no draw of the land emptied it", 0, 0);
    Near("so the second pass adds no light of the step",
         GiveBack(step, list, &count), 0, 0);
    Near("and no light from before the passes", GiveBack(before, list, &count),
         0, 0);
    Near("the list has its 5 lights", count, 5, 0);
    // The draw of the land wrote its parts into the lights. They are still
    // the same lights.
    for (int i = 0; i < 5; ++i) {
      memset(list + i * kLightBytes + kGivenFirstEnd, 77,
             kGivenSecondStart - kGivenFirstEnd);
    }
    Check(StillInList(step, list, count), "the list still has the light "
          "after a draw of the land with no clear", 0, 0);
  }

  // The first pass emptied the list. The second pass: the light from before
  // the passes at its start, the 2 lights of the particles, the light of the
  // step where the pass skips the step, and the light of the draw of the
  // land.
  {
    static uint8_t first[kCapacity * kLightBytes];
    for (int i = 0; i < 5; ++i) {
      MakeLight(first + i * kLightBytes, static_cast<uint8_t>(1 + i), 0);
    }
    memset(list, 0xEE, sizeof(list));
    int count = 0;
    Check(!StillInList(before, list, count), "an empty list does not have "
          "the held lights", 0, 0);
    Near("the second pass gets the light from before the passes",
         GiveBack(before, list, &count), 1, 0);
    MakeLight(list + count++ * kLightBytes, 2, 0);
    MakeLight(list + count++ * kLightBytes, 3, 0);
    Near("and the light of the step", GiveBack(step, list, &count), 1, 0);
    MakeLight(list + count++ * kLightBytes, 5, 0);
    Near("the list of the second pass has 5 lights, as in the first pass",
         count, 5, 0);
    bool same = true;
    for (int i = 0; i < 5; ++i) {
      same = same && SameLight(list + i * kLightBytes, first + i * kLightBytes);
    }
    Check(same, "each light of the second pass is the light of the first "
          "pass, with the same strength and at the same place in the list", 0,
          0);
    Near("the strength of the lightning is the strength of the first pass",
         list[3 * kLightBytes + 0x2C], first[3 * kLightBytes + 0x2C], 0);
  }

  // A light that the list has at its place does not go back a second time.
  {
    int count = 0;
    GiveBack(step, list, &count);
    Near("one light went back", count, 1, 0);
    Held again;
    Clear(&again);
    Hold(&again, list, 0, 1);
    Near("the light that is in the list does not go back a second time",
         GiveBack(again, list, &count), 0, 0);
  }

  // A full list takes only the lights that it has room for.
  {
    Held many;
    Clear(&many);
    static uint8_t source[kCapacity * kLightBytes];
    for (int i = 0; i < 10; ++i) MakeLight(source + i * kLightBytes, 100, 0);
    Near("10 lights are held", Hold(&many, source, 0, 10), 10, 0);
    memset(list, 0, sizeof(list));
    int count = kCapacity - 4;
    Near("a list with room for 4 lights gets 4", GiveBack(many, list, &count),
         4, 0);
    Near("and it is full then", count, kCapacity, 0);
    Near("a full list gets none", GiveBack(many, list, &count), 0, 0);
    int bad = kCapacity + 1;
    Near("a count that is not a count of the list gets none",
         GiveBack(many, list, &bad), 0, 0);
    Held none;
    Clear(&none);
    int empty = 0;
    Near("no held light gives no light", GiveBack(none, list, &empty), 0, 0);
    Check(!StillInList(none, list, 3), "and the list does not have it", 0, 0);
  }

  // Two calls of the step in one pass: the copies are of both.
  {
    Clear(&step);
    for (int i = 0; i < 6; ++i) {
      MakeLight(list + i * kLightBytes, static_cast<uint8_t>(20 + i), 0);
    }
    Hold(&step, list, 1, 2);
    Hold(&step, list, 4, 6);
    Near("two calls of the step hold 3 lights", step.count, 3, 0);
    Near("the last one was light 5 of the list", step.index[2], 5, 0);
    Check(StillInList(step, list, 6), "the list has all 3", 0, 0);
    Check(!StillInList(step, list, 5), "a list of 5 lights does not have the "
          "light at place 5", 0, 0);
  }

  // The light maps of the particles.
  Check(SecondPassTakesCopies(0), "a second pass that adds no light map of a "
        "particle gets the copies of the first pass", 0, 0);
  Check(!SecondPassTakesCopies(2), "a second pass that adds 2 light maps "
        "itself gets no copy: it has the lights one time", 0, 0);

  // The map of the test light is the map of a lightning of the game.
  Near("the middle of the map is white", TestMapValue(32, 32), 255, 0);
  Near("3 points from the middle it is still 255", TestMapValue(35, 32), 255,
       0);
  Near("4 points from the middle it is 252", TestMapValue(36, 32), 252, 1);
  Near("16 points from the middle it is 144", TestMapValue(48, 32), 144, 1);
  Near("31 points from the middle it is 9", TestMapValue(1, 32), 9, 1);
  Near("32 points from the middle it is 0", TestMapValue(0, 32), 0, 0);
  Near("a corner is 0", TestMapValue(0, 0), 0, 0);
  {
    static uint8_t map[kTestMapSize * kTestMapSize * 3];
    FillTestMap(map);
    const uint8_t* p = map + (32 * kTestMapSize + 48) * 3;
    Check(p[0] == p[1] && p[1] == p[2] && p[0] == TestMapValue(48, 32),
          "each point of the map has three bytes of one value: white", p[0],
          144);
  }

  // The test light: the last 150 frames of each 500 frames.
  Check(!TestLightOn(0, 500, 150), "the test light is off at frame 0", 0, 0);
  Check(!TestLightOn(349, 500, 150), "off at frame 349", 0, 0);
  Check(TestLightOn(350, 500, 150), "on at frame 350", 0, 0);
  Check(TestLightOn(499, 500, 150), "on at frame 499", 0, 0);
  Check(!TestLightOn(500, 500, 150), "off at frame 500", 0, 0);
  Check(TestLightOn(850, 500, 150), "on again at frame 850", 0, 0);
  Check(!TestLightOn(10, 0, 150), "no period gives no light", 0, 0);
}

}  // namespace land_light_checks
