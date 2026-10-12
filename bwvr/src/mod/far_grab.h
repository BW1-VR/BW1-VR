// The grab from a distance. See far_grab.cpp and docs/grab.md, "The grab from
// a distance".

#pragma once

#include <stdint.h>

struct FarGrabSettings {
  bool on = true;          // far_grab: 1 shows and fetches far objects
  int cone_deg = 8;        // far_grab_cone_deg: a new target needs this te
  int pull_cmps = 80;      // far_grab_pull: the speed of the pull, in cm/s
  bool throw_law = true;   // throw_law: 1 is the square root law of the throw
  float max_m = 2.0f;      // far_grab_max_m: a target is at most this far from
                           // the head, in meters of the room along the floor.
                           // 0 gives no limit
  int choice = 2;          // far_grab_choice: 0 one point of the object, 1 its
                           // body with a bias for a living thing, 2 its body
                           // with ranks, 3 balls, 4 balls with ranks
                           // (far_choice_math.h)
  float living_deg = 4.0f;       // far_grab_living_deg: the cone of a living
                                 // thing, for the ranks
  float ball_living_deg = 3.0f;  // far_grab_ball_living_deg: the ball of a
                                 // living thing, in degrees
  float ball_other_deg = 1.5f;   // far_grab_ball_other_deg: the ball of each
                                 // other thing
  int stay = 1;            // far_grab_stay: 1 lets a living target keep the
                           // halo past its cone. 0 has the margin of the
                           // scores only
  float stay_deg = 3.0f;   // far_grab_stay_deg: how far past its cone
  int take_carry = 1;      // far_take_carry: 1 leaves the grip with the grip
                           // machine when the source of a far take goes
                           // away, so the grip carries the bundle. 0 holds
                           // the grip back until it opens, and the machine
                           // lets the bundle go
};

void FarGrabSetOptions(const FarGrabSettings& settings);

// far_grab=1.
bool FarGrabOn();
// throw_law=1: the throw sends sqrt(s) v, not the old gain.
bool FarGrabThrowLaw();
// The cone of the selection, in degrees.
float FarGrabSelectDeg();
float FarGrabKeepDeg();
// far_grab_max_m: the range from the head, in meters of the room. 0 for no
// limit.
float FarGrabMaxMeters();
// far_grab_choice: 0 to 4. The test key changes it while the game runs.
int FarGrabChoice();
void FarGrabSetChoice(int choice);
// What a value of far_grab_choice does, for the log.
const char* FarGrabChoiceWords(int choice);
// far_grab_living_deg, far_grab_ball_living_deg and far_grab_ball_other_deg.
float FarGrabLivingDeg();
float FarGrabBallLivingDeg();
float FarGrabBallOtherDeg();
// far_grab_stay: 1 or 0, and far_grab_stay_deg. The test key changes the
// first one while the game runs.
int FarGrabStay();
void FarGrabSetStay(int stay);
float FarGrabStayDeg();
const char* FarGrabStayWords(int stay);
// far_take_carry: 1 or 0. The test key changes it while the game runs.
int FarGrabTakeCarry();
void FarGrabSetTakeCarry(int carry);
const char* FarGrabTakeCarryWords(int carry);

// What hand_direct.cpp gives once a frame, after the history of the hand.
struct FarFrame {
  uint64_t frame;
  double now;               // seconds, one value for the whole frame
  bool usable;              // the feature may run: on, God view or table, the
                            // controller places the Hand, not multiplayer
  bool ready;               // usable, and the Hand is empty in its Normal state
  const char* why;          // the first reason for a no
  float units_per_meter;    // world units in a meter of the room
  bool have_ray;
  float ray_origin[3];      // the aim ray, in the world
  float ray_direction[3];   // a unit vector
  float hand_point[3];      // the Hand's point, in the world
  bool have_motion;
  float grip_velocity[3];   // the mean of the last 60 ms, in m/s of the room
  float grip_room[3];       // the grip in the room, in meters
  float head_room[3];       // the head in the room, in meters
  void* held;               // CHand+0x4904
  int32_t hand_state;       // CHand+0x4878
  void* take;               // the source of the game's bulk take now, or null
};
void FarGrabFrame(const FarFrame& frame);

// What the selection gives, from the first eye pass. hand_direct.cpp scores the
// candidates and calls one of these once a frame.
struct FarPick {
  void* object;
  const char* name;         // the class, for the log
  float position[3];        // the mesh translation
  float radius;             // Get2DRadius, in world units
  bool bulk;                // a bulk source (a pile, a field, a fish farm):
                            // the grip takes from it, with no pull
  bool touch;               // a scroll: the grip activates it, with no pull
                            // (the far touch)
  bool forest;              // a forest: the fetch puts a new tree in the Hand
  int leash;                // the player's own Creature: a short grip changes
                            // the leash, with no pull and no press of the
                            // game (near_math::LeashClick: 1 puts the leash
                            // on, 2 takes it off, 3 takes it back to the
                            // Hand). A grip that stays closed makes the
                            // Creature stay (creature_stay.h). 0 for each
                            // other target
  bool inside;              // the Hand can take it: in the influence, or it
                            // does not need the influence
  float te;                 // degrees
  float hand_m;             // meters of the room from the Hand's point
  float head_m;             // meters of the room from the head, along the floor
  float land[2];            // the land under it: x and z
  const char* runner;       // the text for the log about the second best
                            // candidate, or null
  const char* why;          // why this target won, for the log
  void* vtable;             // the first word of the object at the selection,
                            // for the test that the object still lives
  const void* model;        // the LH3DObject of the draw at the selection
};
void FarGrabSelect(const FarPick& pick);
void FarGrabNoTarget();
// The target is locked, pulled or fetched, or the game takes from it, so the
// selection does not change.
bool FarGrabLocked();
// The game takes from the target at a distance (the Take state).
bool FarGrabTaking();
// The object of the target now, or null.
void* FarGrabTargetObject();
// The target now with the facts of its selection, for the test that the
// object still lives. False with no target.
bool FarGrabTargetFacts(void** object, void** vtable, const void** model);
// The locked object moved: its mesh translation from the last draw, the land
// under it, and whether the Hand can take it there.
void FarGrabTrack(const float position[3], const float land[2], bool inside);
// The object of the target is gone: the game deleted it. The lock, the pull
// or the fetch ends, and nothing reads the object again.
void FarGrabGone();

// For the pick of the game: while a pull or a fetch runs, the object to put in
// the next record, its position and the land under it. Pull and Fetch only.
bool FarGrabFetching(void** object, float position[3], float land[2]);

// The object that glides to the Hand now and the place that it came from (the
// mesh translation at the lock). False when no glide is armed.
bool FarGrabGlideOrigin(void** object, float origin[3]);

// For controllers.cpp, once a pump, before the grip goes to the grip machine.
// grip is the grip button, and ok is true when the hand may act now. hand is
// true while the controller can hold the grip: a grip that is closed on the
// Creature needs only that, so a different control in use does not end it.
// The answer is true while the grip must be held back from the machine.
bool FarGrabPumpGrip(bool grip, bool ok, bool hand);
// True while the press of the fetch goes to the game, so it needs no pixel in
// view: the hand is near the head after a pull.
bool FarGrabPressing();

// For overlay.cpp and hand_glow.cpp: what to draw on this frame. hand_glow.cpp
// draws the halo around the target, and overlay.cpp draws the tether. Nothing
// is drawn on the land under the target.
struct FarDraw {
  bool active;            // a target is selected
  bool valid;             // gold, not red
  bool locked;            // the lock holds, or the game takes from the
                          // target: a tether from the Hand
  bool bulk;              // the target is a bulk source: the grip collects
  bool touch;             // the target is a scroll: the grip activates it.
                          // For the hint card: the prompt of the grip is
                          // then "Activate", and not "Pull to fetch"
  int leash;              // the target is the player's Creature: what a
                          // short grip does to the leash (FarPick::leash),
                          // or 0
  bool hold;              // and a grip that stays closed makes it stay
                          // (creature_hold_stay=1)
  bool stay;              // the grip holds the Creature in a stay now
  float object[3];        // the mesh translation, in the world
  float radius;           // in world units
  float land[2];
  float hand[3];          // the Hand's point
  float units_per_meter;
};
bool FarGrabDrawState(FarDraw* out);

// Once a report window.
void FarGrabReport();
void FarGrabInstall();
