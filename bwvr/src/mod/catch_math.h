// A catch at once (catch_at_once, docs/grab.md, "A catch at once"): the math
// of the press on an object that moves, with no game and no Windows. The
// checks are in test/controller_math/catch_checks.h.
#pragma once

#include <math.h>
#include <stdint.h>

namespace catch_math {

// The values of catch_at_once.
const int kCatchWaits = 0;   // the pick up of the game, and its picture
const int kCatchAtOnce = 1;  // the pick up and the picture at the press

// THE PRESS IS OLD ENOUGH. The step of the pick up action of the game
// (fn_005D5250) sends the pick up of an object that its physics has only when
// the press is 225 ms old. It takes the smaller of two ages: the clock now
// less the clock at the press, and 100 ms for each game turn since the press
// and 100 more (controller_math::GamePressAge). This gives the clock and the
// game turn of a press that is age_ms old or more on both, for the clock and
// the game turn of now. The numbers are unsigned and can wrap, as in the game.
inline void OldPress(uint32_t clock_ms, uint32_t turn, uint32_t age_ms,
                     uint32_t* press_ms, uint32_t* press_turn) {
  *press_ms = clock_ms - age_ms;
  *press_turn = turn - (age_ms + 99u) / 100u;
}

// THE PICTURE OF A CATCH. From the press, the mod draws the object in the
// Hand: it starts where the picture was before the press, and it goes to its
// place in the Hand with the settle of each pick up. share is the part of the
// way that is left (hand_exact_math::SettleShare: 1 at the press, 0 after the
// settle).
inline void Picture(const float from[3], const float place[3], float share,
                    float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = place[i] + (from[i] - place[i]) * share;
}

// THE NUMBERS OF ONE CATCH, for the log. One call for each frame from the
// press, with the place of the picture of that frame, whether the mod drew
// the object in the Hand in it (hold), and the real time since the press.
//
//   first_hold_ms  the time of the first frame with the object in the Hand
//   own            how far the picture went before that, by the motion of the
//                  object itself
//   fell           how far it came down in that time, from its height at the
//                  press
//   jump           the largest move of the picture from one frame to the next
struct Track {
  int frames;
  float from[3];
  float last[3];
  bool held;
  double first_hold_ms;
  int first_hold_frame;
  float own;
  float fell;
  float jump;
  double jump_ms;
  int jump_frame;
};

inline void TrackReset(Track* t, const float from[3]) {
  t->frames = 0;
  for (int i = 0; i < 3; ++i) t->from[i] = t->last[i] = from[i];
  t->held = false;
  t->first_hold_ms = -1.0;
  t->first_hold_frame = -1;
  t->own = t->fell = t->jump = 0.0f;
  t->jump_ms = 0.0;
  t->jump_frame = -1;
}

inline void TrackAdd(Track* t, const float place[3], bool hold, double ms) {
  const float d[3] = {place[0] - t->last[0], place[1] - t->last[1],
                      place[2] - t->last[2]};
  const float move = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (hold && !t->held) {
    t->held = true;
    t->first_hold_ms = ms;
    t->first_hold_frame = t->frames;
  }
  if (!t->held) {
    t->own += move;
    const float down = t->from[1] - place[1];
    if (down > t->fell) t->fell = down;
  }
  if (move > t->jump) {
    t->jump = move;
    t->jump_ms = ms;
    t->jump_frame = t->frames;
  }
  for (int i = 0; i < 3; ++i) t->last[i] = place[i];
  ++t->frames;
}

}  // namespace catch_math
