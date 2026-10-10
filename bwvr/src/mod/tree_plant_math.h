// WHETHER THE OPENING OF THE GRIP PLANTS A TREE (docs/grab.md, "A tree in the
// Hand"). Pure math: no Windows and no game.
//
// The game decides at the release (Object::InitialisePhysicsFromHand
// 0x00636F00, then Tree::EndPhysics 0x0074B830). It plants the tree on the
// spot when the object is set down on dry land, with no other body in the
// way, no fire on the tree and a mesh that is at most 0.2 rad from upright
// about x and z. Else the tree goes into the physics and falls. The mod adds
// the height rule: a still hand that is higher than release_fall_cm over the
// land lets the tree fall (throw.md, "The height of a release").
#pragma once

#include <math.h>

namespace tree_plant_math {

// The most that the game allows the mesh to lean, in radians, about x and z.
const float kMaxLean = 0.2f;

enum Verdict {
  kPlants = 0,    // the opening of the grip plants the tree
  kTooHigh = 1,   // the hand is higher than the limit: the tree falls
  kLeans = 2,     // the tree leans more than kMaxLean: the game lets it fall
  kNoLand = 3,    // the land under the Hand is not dry land
  kBurning = 4,   // a tree on fire is not planted
  kVerdictCount = 5
};

// height_m: the Hand's point over the land, in meters of the room. limit_m:
// release_fall_cm in meters, 0 for no limit. lean_x, lean_z: the angles of the
// mesh in radians. The first cause in the list is the one that the card names.
inline int ReleaseVerdict(float height_m, float limit_m, float lean_x,
                          float lean_z, bool dry_land, bool burning) {
  if (burning) return kBurning;
  if (!dry_land) return kNoLand;
  if (limit_m > 0.0f && height_m > limit_m) return kTooHigh;
  if (fabsf(lean_x) > kMaxLean || fabsf(lean_z) > kMaxLean) return kLeans;
  return kPlants;
}

// The words of the card for a release that does not plant. They say what the
// opening does and what the player can do.
inline const wchar_t* VerdictText(int verdict) {
  switch (verdict) {
    case kTooHigh: return L"Lower to plant. Open to drop";
    case kLeans: return L"Hold upright to plant. Open to drop";
    case kNoLand: return L"Needs dry land. Open to drop";
    case kBurning: return L"On fire. Open to drop";
    default: return nullptr;
  }
}

}  // namespace tree_plant_math
