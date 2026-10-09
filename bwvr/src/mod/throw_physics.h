// The real flight of a thrown object. See throw_physics.cpp and docs/throw.md,
// "The throw physics".

#pragma once

#include <stdint.h>

#include "controller_math.h"

// throw_law: 0 is the plain game's gain, 1 is the square root law, 2 is the real
// flight. throw_log=1 logs each flight in full, for the desktop runs, and
// throw_log=2 also logs each substep and each frame after the first contact.
// With test_keys=1, Scroll Lock goes between the real flight and the square
// root law while the game runs, for an A/B in the headset.
void ThrowPhysicsSetOptions(int law, int log, bool test_keys);
void ThrowPhysicsInstall();
// The time factor k times 10 (10 to 100, 20 is 2.0) and whether a tree flies
// as a rock does. See throw_math.h. With test_keys=1, Numpad Plus and Numpad
// Divide change k by 0.25 while the game runs.
void ThrowPhysicsSetSlow(int slow_x10, bool trees);

// THE LANDING: what a thrown object does after its first contact. land 1 is
// the time of the land with the low bounce and the brake, 2 is the low bounce
// and the brake in the time of the flight, 0 is the landing of the plain game.
// rate_x10 is the rate of the land times 10 (10 to 100, 10 is the game's own
// time). bounce_pct is the share of its speed that a bounce keeps, of the
// speed that the game gives it. brake_pct is the friction of the brake of the
// land, in percent of the friction that the game has for the object. With
// test_keys=1, Pause goes through the three landings,
// and Ctrl with Numpad Plus or Numpad Divide changes the rate of the land by
// 0.25.
void ThrowPhysicsSetLanding(int land, int rate_x10, int bounce_pct,
                            int brake_pct);
// release_fall_cm: a hand that is still at the opening of the grip sets the
// object down only when its point is this many cm of the room over the land
// or less. Higher up the object falls. 0 sets it down at each height. With
// test_keys=1, Shift with Scroll Lock goes between this limit and 0.
void ThrowPhysicsSetFall(int limit_cm);
// Before FarGrabInstall: the call that reads the place of the held object in
// the Hand must run before the glide of a far grab.
void ThrowPhysicsInstallEarly();

// The law of a release: the key. It is 1 in place of 2 when the hooks of
// the real flight are not there, and in a multiplayer game.
int ThrowPhysicsLaw();

// What hand_direct.cpp gives at a release in the God view or on the table,
// from its release hook.
struct ThrowPlanIn {
  uint64_t frame;
  void* held;                 // CHand+0x4904
  // The history of the grip in the room, oldest first, in units at the life
  // size scale.
  const controller_math::MotionSample* grip;
  int count;
  double now;                 // the time of the release action
  bool latched;               // the grip opened before this release
  double opened;              // the time of the opening, or `now`
  float v_end_room[3];        // the mean of the 80 ms before the opening
  float room[9];              // the room basis L
  float gain;                 // throw_gain_pct / 100: the set down rule
  float life;                 // units in a meter at the life size scale
  float units_per_meter;      // world units in a meter of the room now
  float cap;                  // the game's cap of the speed, 124
  float drop_speed;           // the game sets down at this speed or less
  float height_m;             // the Hand's point over the land at the opening
                              // of the grip, meters of the room now
};

struct ThrowPlanOut {
  bool drop;                  // the game sets the object down
  bool fall;                  // a still hand high up: the object falls
  float sent[3];              // what the release action sends
  float real_mps;             // the top speed of the hand
};

// Works out the release of this frame and logs it. False when the real
// flight cannot run now. Then the caller uses the square root law.
bool ThrowPhysicsPlan(const ThrowPlanIn& in, ThrowPlanOut* out);

// After the game's release action ran with the plan of this frame. The mod
// flies the object from this moment.
void ThrowPhysicsReleased(void* held);

// pass_guard.cpp, around PhysicsObject::GameTurnUpdate.
void ThrowPhysicsTurnBegin();
void ThrowPhysicsTurnEnd();
