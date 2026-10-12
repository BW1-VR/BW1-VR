// The glow on the mesh of a target of the Hand. See target_glow.cpp and
// docs/hand-model.md, "The glow on the mesh of a target".

#pragma once

#include <stdint.h>

struct TargetGlowSettings {
  int mode = 1;            // target_glow: 0 the halo, 1 the glow on the mesh
                           // with a halo only for a target whose mesh does
                           // not glow, 2 both
  int pct = 45;            // target_glow_pct: the share of the halo color
  int anim = 3;            // target_glow_anim: 0 steady, 1 the pulse, 2 the
                           // wave, 3 both
  bool test_keys = false;  // test_keys=1: Ctrl, Shift and the left square
                           // bracket key change target_glow
};

void TargetGlowSetOptions(const TargetGlowSettings& settings);
void TargetGlowInstall();

// stereo.cpp: once a frame, after the frame functions of the hands and
// before the eye passes.
void TargetGlowFrame();
// stereo.cpp: after the scene walk of each eye pass.
void TargetGlowPassDone();

// draw_hooks.cpp: a batch of the world goes to the draw. verts has the
// vertex format 0x1C4, and idx has the indices of a triangle list. clipped:
// the batch goes through the clip of the engine. caller is the code of the
// game that asked for the draw, for the log.
void TargetGlowBatch(void* verts, uint32_t count, const uint16_t* idx,
                     uint32_t indices, bool clipped, uint32_t caller);

// hand_glow.cpp: an object that has a halo now, and the color of its halo.
struct TargetGlowMark {
  void* object;
  uint8_t r, g, b;
  const char* name;
};
// hand_glow.cpp gives the marks of this frame. The answer is the count.
int HandGlowTargets(TargetGlowMark* out, int max);
// hand_glow.cpp asks in each eye pass, after the world is drawn: true when
// the halo of this object must be drawn.
bool TargetGlowHaloShows(const void* object);

// Once a report window.
void TargetGlowReport();
