// THE LIGHTS ON THE LAND IN THE SECOND EYE PASS (land_light.cpp, and
// docs/backend.md, "Time, bars and the second eye pass"). Pure math: no
// Windows, no game. The checks are in
// test/controller_math/land_light_checks.h.
//
// The game has a list of the lights on the land of one scene walk: the white
// light of a lightning, the light of the Hand, the lights of a dance and the
// light maps of the particles. Code adds a light to the list, the draw of
// the land uses each light of the list, and the end of the scene walk empties
// the list.
//
// The mod walks the scene two times in a frame, one time for each eye. Some
// lights come one time in a frame: from code that runs before the two walks,
// and from a step of the world that the second walk skips. The first walk
// uses those lights and empties the list. So only the first eye has them.
//
// The mod holds a copy of each such light of the first walk. In the second
// walk it adds the copies to the list again, at the place in the walk where
// the first walk got them. The list of the game stays the store: the second
// walk draws from it and empties it as the game does.

#pragma once

#include <stdint.h>
#include <string.h>

namespace land_light {

// The list of the game has room for 200 lights of 0x34 bytes.
const int kCapacity = 200;
const int kLightBytes = 0x34;

// The code that adds a light writes these parts of it: the place, the map of
// the light and its size (0x00 to 0x13), and a flag, the strength and the
// kind (0x28 to 0x33). The draw of the land writes the parts between them.
const int kGivenFirstEnd = 0x14;
const int kGivenSecondStart = 0x28;

// True when two lights have the same given parts.
inline bool SameLight(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, kGivenFirstEnd) == 0 &&
         memcmp(a + kGivenSecondStart, b + kGivenSecondStart,
                kLightBytes - kGivenSecondStart) == 0;
}

// The copies of the lights of one source, with the place of each light in
// the list of the first walk.
struct Held {
  int count;
  int index[kCapacity];
  uint8_t lights[kCapacity * kLightBytes];
};

inline void Clear(Held* held) { held->count = 0; }

// Holds a copy of the lights that came to the list between two counts of the
// list: the list had `before` lights, and then `after`. A second call of one
// frame adds to the copies. Returns how many lights it took. A count that is
// not a count of the list gives 0.
inline int Hold(Held* held, const uint8_t* list, int before, int after) {
  if (held == nullptr || list == nullptr) return 0;
  if (before < 0 || after > kCapacity || after <= before) return 0;
  int taken = 0;
  for (int i = before; i < after && held->count < kCapacity; ++i) {
    memcpy(held->lights + held->count * kLightBytes, list + i * kLightBytes,
           kLightBytes);
    held->index[held->count] = i;
    ++held->count;
    ++taken;
  }
  return taken;
}

// True when the list of `count` lights still has each held light at its
// place: the first walk did not empty the list. That is a walk with no draw
// of the land. False for no held light.
inline bool StillInList(const Held& held, const uint8_t* list, int count) {
  if (held.count <= 0 || list == nullptr) return false;
  for (int i = 0; i < held.count; ++i) {
    if (held.index[i] >= count) return false;
    if (!SameLight(held.lights + i * kLightBytes,
                   list + held.index[i] * kLightBytes)) {
      return false;
    }
  }
  return true;
}

// Adds the held lights to the end of the list, as many as the list has room
// for, and moves `count`. Returns how many lights it added. It adds nothing
// when the list still has them.
inline int GiveBack(const Held& held, uint8_t* list, int* count) {
  if (list == nullptr || count == nullptr) return 0;
  if (*count < 0 || *count > kCapacity) return 0;
  if (StillInList(held, list, *count)) return 0;
  int given = 0;
  for (int i = 0; i < held.count && *count < kCapacity; ++i) {
    memcpy(list + *count * kLightBytes, held.lights + i * kLightBytes,
           kLightBytes);
    ++*count;
    ++given;
  }
  return given;
}

// THE LIGHT MAPS OF THE PARTICLES. One function of the game adds them in
// each scene walk, and it empties its own array. The call of the second eye
// pass adds the maps that came to the array after the call of the first eye
// pass. When it adds no light, the second eye gets the copies of the first
// eye. When it adds lights, the second eye has its own and gets no copy.
inline bool SecondPassTakesCopies(int added_by_second) {
  return added_by_second <= 0;
}

// THE TEST LIGHT (land_light_test). The map of a lightning of the game
// (fn_00837110): 64 by 64 points of 3 bytes, white, 255 in the middle and 0
// from 32 points out. The value falls by 9 for each point of distance and
// stops at 255.
const int kTestMapSize = 64;

inline uint8_t TestMapValue(int x, int y) {
  const int dx = x - kTestMapSize / 2;
  const int dy = y - kTestMapSize / 2;
  const int square = dx * dx + dy * dy;
  const int half = kTestMapSize / 2;
  if (square >= half * half) return 0;
  // The distance, without a root of the C library: the checks run with no
  // game. 0.01 of a point is enough for a value of 9 for each point.
  float low = 0.0f, high = static_cast<float>(half);
  for (int i = 0; i < 24; ++i) {
    const float mid = (low + high) * 0.5f;
    if (mid * mid < static_cast<float>(square)) low = mid; else high = mid;
  }
  const int value = static_cast<int>((static_cast<float>(half) - low) * 9.0f);
  return static_cast<uint8_t>(value > 255 ? 255 : value);
}

inline void FillTestMap(uint8_t* map) {
  for (int y = 0; y < kTestMapSize; ++y) {
    for (int x = 0; x < kTestMapSize; ++x) {
      const uint8_t value = TestMapValue(x, y);
      uint8_t* p = map + (y * kTestMapSize + x) * 3;
      p[0] = p[1] = p[2] = value;
    }
  }
}

// The test light is on for the last `on` frames of each `period` frames with
// two eye passes. So a run starts with frames that have no test light.
inline bool TestLightOn(uint64_t frame, int period, int on) {
  if (period <= 0 || on <= 0 || on > period) return false;
  return frame % static_cast<uint64_t>(period) >=
         static_cast<uint64_t>(period - on);
}

}  // namespace land_light
