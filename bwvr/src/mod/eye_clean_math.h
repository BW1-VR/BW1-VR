// The math of the clean of each eye pass (docs/stereo-artifacts.md, "The
// frame before of each eye"). It has no game and no Direct3D in it.
// eye_clean.cpp uses it, and test/controller_math checks it.
#pragma once

#include <stdint.h>

namespace eye_clean {

// LH3DRender::g_mode_cleaning (0x00C3871C): what the game does to its render
// target after each present.
const int kModeNone = 0;   // nothing: the sky is the background of a frame
const int kModeAll = 1;    // it cleans the picture and the Z buffer
const int kModeDepth = 2;  // it cleans the Z buffer: the frame before stays

// What one call of the clean function of the game did.
enum Cleaned { kCleanedNothing = 0, kCleanedDepth = 1, kCleanedAll = 2 };

// The clean function of the game (fn_0082EE70). Mode 1 cleans the picture
// and the Z buffer, and mode 2 the Z buffer. Mode 0 cleans nothing, with one
// exception: while the callbacks of the frame end are on, a timer makes the
// clean of mode 1 for two calls in each 2000 ms. The function keeps a stamp
// of the time and a flag for the second call. `stamp` and `flag` are those
// two values before and after the call.
inline Cleaned GameCleaned(int mode, bool callbacks, uint32_t stamp_before,
                           uint32_t stamp_after, uint32_t flag_before,
                           uint32_t flag_after) {
  if (mode == kModeAll) return kCleanedAll;
  if (mode == kModeDepth) return kCleanedDepth;
  if (mode != kModeNone || !callbacks) return kCleanedNothing;
  // The first call of the two writes a new stamp. The second call puts the
  // flag down.
  if (stamp_after != stamp_before) return kCleanedAll;
  if (flag_before != 0 && flag_after == 0) return kCleanedAll;
  return kCleanedNothing;
}

// The larger of two cleans.
inline Cleaned More(Cleaned a, Cleaned b) { return a > b ? a : b; }

// eye_clean
const int kShared = 0;  // the two eye passes share the render target
const int kOwn = 1;     // each eye pass starts from its own frame before

// What the mod does for one eye pass of the scene.
struct PassPlan {
  // Before the pass: the clean that the mod makes itself.
  Cleaned clear = kCleanedNothing;
  // Before the pass: the keep of this eye goes into the render target.
  bool give = false;
  // After the pass: the picture goes into the keep of this eye.
  bool take = false;
  // In the pass: a clean of the picture and the Z buffer before the sky.
  bool clean_at_sky = false;
};

// True when the picture of the frame before stays in view in this frame: the
// game cleans only the Z buffer, or it cleans nothing and the passes of the
// frame before drew no sky.
inline bool FrameBeforeStays(int mode, bool covered) {
  return mode == kModeDepth || (mode == kModeNone && !covered);
}

// The plan of one eye pass.
//
// `key` is eye_clean. `mode` is the cleaning mode of the last clean of the
// game, and `cleaned` is what that clean did: it acted on the render target
// one time, before eye pass 0. `covered` is true when each eye pass of the
// frame before drew the sky.
//
// Eye pass 0 has the clean of the game. Eye pass 1 gets the same clean from
// the mod. When the frame before stays, each eye has a keep of its picture.
// After a clean of the picture the frame before is gone for both eyes, so no
// keep goes into the render target. In mode 0 the sky is the clean of the
// game, and the mod makes that clean whole.
inline PassPlan Plan(int key, int mode, Cleaned cleaned, bool covered,
                     int eye) {
  PassPlan plan;
  if (key != kOwn || (eye != 0 && eye != 1)) return plan;
  if (eye == 1) plan.clear = cleaned;
  if (FrameBeforeStays(mode, covered)) {
    plan.take = true;
    plan.give = cleaned != kCleanedAll;
  }
  plan.clean_at_sky = mode == kModeNone;
  return plan;
}

// The first frame in which the frame before stays has no keep. Eye pass 0
// then draws over the render target as it is, and eye pass 1 must start
// from that same picture. True when the keep of eye 1 must take the render
// target before eye pass 0 draws over it.
inline bool SeedEyeOne(int key, int mode, Cleaned cleaned, bool covered,
                       bool has_keep_one) {
  return key == kOwn && FrameBeforeStays(mode, covered) &&
         cleaned != kCleanedAll && !has_keep_one;
}

}  // namespace eye_clean
