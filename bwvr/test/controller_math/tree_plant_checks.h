// The release of a tree (tree_plant_math.h). main.cpp includes this file after
// Check and Near, just before main.
#pragma once

#include "../../src/mod/tree_plant_math.h"

namespace tree_plant_checks {

inline void TreePlantChecks() {
  using namespace tree_plant_math;
  auto verdict = [](float height, float limit, float x, float z, bool dry,
                    bool burning) {
    return ReleaseVerdict(height, limit, x, z, dry, burning);
  };
  Check(verdict(0.05f, 0.15f, 0, 0, true, false) == kPlants,
        "a still hand near the land plants an upright tree", 0, 0);
  Check(verdict(0.15f, 0.15f, 0, 0, true, false) == kPlants,
        "the limit itself still plants", 0, 0);
  Check(verdict(0.45f, 0.15f, 0, 0, true, false) == kTooHigh,
        "a hand over the limit lets the tree fall", 0, 0);
  Check(verdict(2.0f, 0.0f, 0, 0, true, false) == kPlants,
        "the limit 0 gives no height rule", 0, 0);
  Check(verdict(0.0f, 0.15f, 0.2f, -0.2f, true, false) == kPlants,
        "a lean of 0.2 rad still plants", 0, 0);
  Check(verdict(0.0f, 0.15f, 0.21f, 0, true, false) == kLeans,
        "a lean of 0.21 rad about x does not plant", 0, 0);
  Check(verdict(0.0f, 0.15f, 0, -0.25f, true, false) == kLeans,
        "a lean of 0.25 rad about z does not plant", 0, 0);
  Check(verdict(0.0f, 0.15f, 0, 0, false, false) == kNoLand,
        "water does not plant", 0, 0);
  Check(verdict(0.0f, 0.15f, 0, 0, true, true) == kBurning,
        "a burning tree does not plant", 0, 0);
  Check(verdict(1.0f, 0.15f, 0.5f, 0, true, false) == kTooHigh,
        "the height is named before the lean", 0, 0);
  Check(VerdictText(kPlants) == nullptr, "a tree that plants has no text", 0,
        0);
  for (int v = 1; v < kVerdictCount; ++v) {
    Check(VerdictText(v) != nullptr, "each cause has a text", v, 1);
  }
}

}  // namespace tree_plant_checks
