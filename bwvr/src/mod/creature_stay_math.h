// The stay of the Creature: the pure rules. See docs/creature.md, "The stay".
// far_grab.cpp and creature_stay.cpp use them, and test/controller_math
// checks them (stay_checks.h). It has no game and no headset in it.
//
// THE GRIP ON THE FAR CREATURE HAS TWO JOBS. A short grip changes the leash.
// A grip that stays closed makes the Creature stay. The plain game has a
// rule for a press that has two jobs, for example with the leash on the
// Hand. The step of its interface action 0x13 (0x005D53D0) sends the click
// when the button comes up. It starts the locked select of the object when
// the button stays down for more than 225 ms [V]. The mod has the same rule:
// the leash click of a short grip goes out when the grip opens, and a hold
// never changes the leash.

#pragma once

#include <math.h>

namespace stay_math {

// creature_hold_stay.
const int kModeClose = 0;  // the leash click at the close of the grip, and no
                           // stay
const int kModeHold = 1;   // a short grip is the leash click, at its open. A
                           // grip that stays closed makes the Creature stay

inline int ClampMode(int mode) { return mode != 0 ? kModeHold : kModeClose; }

// creature_hold_stay_ms: a grip that is closed for this long is a hold. The
// game's own time is 225 ms, for a mouse button. The grip of a controller is
// slower than a mouse button, so the default is longer.
const int kHoldMs = 350;
const int kGameHoldMs = 225;
const int kHoldMsLeast = 100;
const int kHoldMsMost = 2000;

inline int ClampHoldMs(int ms) {
  if (ms < kHoldMsLeast) return kHoldMsLeast;
  if (ms > kHoldMsMost) return kHoldMsMost;
  return ms;
}

// What the caller does now.
enum Event {
  kNothing,
  kClick,      // send the leash click
  kStayStart,  // ask the game to make the Creature stay
  kStayEnd,    // let the Creature go
};

// One grip on the far Creature, from its close to its open.
struct Press {
  bool down = false;       // the grip is closed on the Creature
  bool stay = false;       // the hold time is over: the stay was asked for
  int mode = kModeClose;   // creature_hold_stay at the close
  double since = 0.0;      // the time of the close, in seconds
};

// THE CLOSE of the grip with the Creature as the far target.
inline int Close(Press* p, int mode, double now) {
  p->down = true;
  p->stay = false;
  p->mode = ClampMode(mode);
  p->since = now;
  return p->mode == kModeHold ? kNothing : kClick;
}

// The time since the close, in ms.
inline double HeldMs(const Press& p, double now) {
  const double ms = (now - p.since) * 1000.0;
  return ms > 0.0 ? ms : 0.0;
}

// ONE STEP for each pump after the close. grip is the grip now. hand is true
// while the controller can hold the grip: a hand that the mod lost (a menu, a
// controller that sleeps) is false, and its grip counts as open.
//
// - The grip stays closed for hold_ms: the stay starts, one time.
// - The grip opens before that: the leash click (kModeHold only: with
//   kModeClose the click went out at the close).
// - The grip opens after that: the stay ends.
// - The hand is lost: a stay ends, and a short grip sends no click, because
//   the player did not open the grip.
inline int Step(Press* p, bool grip, bool hand, double now, int hold_ms) {
  if (!p->down) return kNothing;
  if (!grip || !hand) {
    const bool stay = p->stay;
    const bool click = !stay && hand && p->mode == kModeHold;
    p->down = false;
    p->stay = false;
    return stay ? kStayEnd : (click ? kClick : kNothing);
  }
  if (p->mode == kModeHold && !p->stay && HeldMs(*p, now) >= hold_ms) {
    p->stay = true;
    return kStayStart;
  }
  return kNothing;
}

// THE END OF A STAY. The game does not end a press on the Creature at the
// release. It first asks the Creature (slot 0x6E4,
// IsReadyForNetworkUnfriendlyEndLockedSelect): the look of the Creature is at
// its place, and no action of its body or of its face runs. The step of the
// game's interface action 5 waits for that with no limit (0x005D49B0) [V].
// The mod waits too, for kEndWaitS at most, so a Creature that does not
// become ready is not held for ever. The exit of the game's own actions sends
// the end with no wait (0x005D4870) [V].
const double kEndWaitS = 5.0;

inline bool EndNow(bool ready, double waited_s) {
  return ready || !(waited_s < kEndWaitS);
}

// THE CREATURE GOES ON, for the log: after the end it moved this many units
// on the map. The mod watches for kWatchS after the end, and then the log
// says that the Creature did not leave its place.
const float kGoesOnUnits = 0.5f;
const double kWatchS = 10.0;

inline bool GoesOn(float moved_units) { return moved_units >= kGoesOnUnits; }

// The watch is over: the Creature goes on, or the time is over.
inline bool WatchDone(float moved_units, double watched_s) {
  return GoesOn(moved_units) || !(watched_s < kWatchS);
}

// The distance of two places on the map, in x and z.
inline float MapDistance(const float a[2], const float b[2]) {
  const float dx = a[0] - b[0], dz = a[1] - b[1];
  const float d2 = dx * dx + dz * dz;
  return d2 > 0.0f ? sqrtf(d2) : 0.0f;
}

}  // namespace stay_math
