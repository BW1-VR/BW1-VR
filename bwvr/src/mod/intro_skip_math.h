// The math of the intro skip (docs/intro.md, "The skip"). It has no game and
// no headset in it. intro_skip.cpp uses it, and test/controller_math checks
// it.
#pragma once

#include <stdint.h>

namespace intro_skip {

// THE PARTS OF THE INTRO at the start of the game. Each part draws its own
// frames and calls LH3DRender::FinishFrame itself, so the address that
// called FinishFrame says which part plays.
//
//   PlayLogoScreens    0x00642950, 0x430 bytes: three logo screens
//   PlayPreIntroVideo  0x006426F0, 0x23B bytes: a video, on the first start
//   RunAlexFX, DoLogo  0x005F89F0 to 0x005FA120: the Lionhead logo
enum Part { kNone, kLogoScreens, kVideo, kLionheadLogo };

inline int PartOf(uintptr_t caller) {
  if (caller >= 0x00642950 && caller < 0x00642D80) return kLogoScreens;
  if (caller >= 0x006426F0 && caller < 0x0064292B) return kVideo;
  if (caller >= 0x005F89F0 && caller < 0x005FA120) return kLionheadLogo;
  return kNone;
}

// THE BUTTONS: one bit for each button that is down. Bit 0 is the left
// trigger, bit 1 is X and bit 2 is Y. Bits 3 to 5 are the right trigger, A
// and B. `awake` has bit 0 for a left hand that the runtime reports and bit
// 1 for the right hand.
const unsigned kLeftButtons = 0x07;
const unsigned kRightButtons = 0x38;

// intro_skip
const int kOff = 0;
const int kSkipAll = 1;   // one press skips all that is left of the intro
const int kSkipPart = 2;  // one press skips one part, as a key on a monitor

struct State {
  bool have = false;      // a frame before this one gave its buttons
  unsigned down = 0;
  unsigned awake = 0;
  bool latched = false;   // a press asked for all that is left
  bool logo_done = false; // the Lionhead logo got the skip
  float seconds = 0.0f;   // since the latch
};

// The longest time that one press skips for. The parts of the intro are all
// in the first minute of a start.
const float kLatchSeconds = 120.0f;

// One frame that ended with FinishFrame. `part` is the part that drew it.
// True when the game must get the skip key for this frame. `pressed` gets
// the buttons that went down in this frame.
//
// A new press is a button that is down now, was up in the frame before, and
// is on a hand that was awake in the frame before. So a button that is down
// when the intro starts, or when a controller wakes, does not skip.
inline bool Step(State* s, int part, unsigned down, unsigned awake, float dt,
                 int mode, unsigned* pressed) {
  unsigned fresh = 0;
  if (s->have) {
    const unsigned hands = ((s->awake & 1u) ? kLeftButtons : 0u) |
                           ((s->awake & 2u) ? kRightButtons : 0u);
    fresh = down & ~s->down & hands;
  }
  s->have = true;
  s->down = down;
  s->awake = awake;
  if (pressed != nullptr) *pressed = 0;
  if (s->latched) {
    if (dt > 0.0f) s->seconds += dt;
    // The intro is over: a frame that is not of a part comes after the skip
    // of the Lionhead logo, which is the last part.
    if ((part == kNone && s->logo_done) || s->seconds > kLatchSeconds) {
      s->latched = false;
      s->logo_done = false;
    }
  }
  if (part == kNone || mode == kOff) return false;
  bool skip = s->latched;
  if (fresh != 0) {
    if (pressed != nullptr) *pressed = fresh;
    skip = true;
    if (mode == kSkipAll && !s->latched) {
      s->latched = true;
      s->logo_done = false;
      s->seconds = 0.0f;
    }
  }
  if (skip && s->latched && part == kLionheadLogo) s->logo_done = true;
  return skip;
}

}  // namespace intro_skip
