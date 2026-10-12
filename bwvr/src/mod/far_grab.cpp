// The grab from a distance. See docs/grab.md, "The grab from a distance".
//
// WHAT THE PLAYER DOES. The player points the hand at an object that is
// out of reach. A gold halo shows around it (red when the land under it is
// outside the player's influence, and then the object cannot be taken). A
// short tick in the controller tells that the target is new. The player
// closes the grip, and a line shows from the Hand to the object (the lock).
// The player then pulls the hand toward the head. The object comes to the
// Hand and the grip carries it, as with an object that the Hand touches. A
// grip that opens before the pull ends the lock.
//
// THE RANGE. A target is at most far_grab_max_m meters of the room from the
// head, along the floor (2 m). hand_direct.cpp applies it in the selection.
//
// A BULK SOURCE (a pile of a store, a field, a fish farm) is a target too.
// It needs no pull: the grip that closes on it starts the game's bulk take at
// a distance, the take runs while the grip is closed, and the opening ends
// it. The bundle is then in the Hand.
//
// A FOREST is a target as a single object is. The game puts a new tree in
// the Hand 225 ms after the press (hand_direct.cpp, IsForest), so the fetch is
// done when an object is in the Hand, and the glide draws that tree from the
// forest's point.
//
// A SCROLL is a target too: the far touch. It needs no pull: the grip that
// closes on it goes to the game as a press with the scroll in the pick
// record, and the game taps the scroll, as it does for a press at the Hand's
// point. After near_math::kTouchMs the far grab lets the press go.
//
// THE PLAYER'S OWN CREATURE is a target of its own kind (docs/creature.md,
// "The leash from a distance" and "The stay"). It needs no pull and it does
// not come to the Hand. A short grip is the leash click of the game. A grip
// that stays closed makes the Creature stay until the grip opens
// (creature_stay.cpp). No press goes to the game.
//
// NEAR OBJECTS WIN. hand_direct.cpp gives no target while an object that a
// press acts on is near the Hand's point (docs/grab.md, "One point for each
// interaction").
//
// WHAT THE GAME DOES. The mod does not move the object. After the pull it
// puts the object in the next pick record, and it writes the land under the
// object to GInterface+0x3F0, so the game takes it with its own pick up
// (fn_005D1740, packet 0x13) and its own rules. The pick up has no distance
// test [V]. The mod then draws the object gliding to the Hand (GlidePrepare).
//
// THE STATES. Idle: no target. Aim: a target, chosen by hand_direct.cpp from
// the cone of the aim ray. Lock: the grip closed on the target, and the grip
// is held back from the grip machine (FarGrabPumpGrip). Pull: the pull was
// seen, and the object is in the pick record for kPullFrames frames. Fetch:
// the grip goes to the game as a press, and the mod waits for the Hand to hold
// the object. A fetch that the game does not finish in near_math::kFetchMs is
// cancelled. Take: the game takes from a far bulk source. The grip is not held
// back: the grip machine makes it the game's button (GripStep), and the state
// ends when the game ends the take. Hold: the grip is closed on the player's
// Creature, and the time decides between the leash click and the stay
// (stay_math::Step). The target does not change in that time.
//
// A grip that is closed when a lock or a fetch ends stays held back until it
// opens, so it never reaches the grip machine as a new press on the land. The
// end of the Take state does not hold the grip back: the grip machine has the
// grip there, and it carries the bundle (controller_math::FarHoldsBack).
//
// A TARGET THAT MOVES OR DIES. The lock keeps the pointer of the object, and
// the game can delete the object before the pull: a rock that sinks in the
// sea, a villager that dies. So hand_direct.cpp tests once a frame that the
// object still lives (FarGrabTargetFacts), and FarGrabGone ends the lock when
// it does not. While the lock holds, the draw gives the place of the object
// on each frame (FarGrabTrack), so an object that drifts on the sea or rolls
// down a hill is fetched from where it is at the pull.
//
// ONCE A FRAME. FarGrabFrame runs once a frame. The pump (FarGrabPumpGrip)
// runs once a frame too. The hooks (the glide) run for each eye, so they read
// only the values that FarGrabFrame latched.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "controller_math.h"
#include "creature_stay.h"
#include "creature_stay_math.h"
#include "far_choice_math.h"
#include "far_grab.h"
#include "haptics.h"
#include "near_math.h"

extern void ModLog(const char* format, ...);

// hand_direct.cpp: the leash click on the player's own Creature (packet
// 0x5F), with no press of the game. False when the game would not take it.
bool HandDirectLeashClick(void* creature);
// hand_exact.cpp: a call after CHand::PrepareForDrawing.
bool HandExactAddPrepareHook(void (*hook)(void* hand));
// hand_direct.cpp: the CHand, and whether the controller places it now.
void* HandDirectHand(bool* active);
// hand_direct.cpp: the Hand's point after the land test, and the size.
bool HandDirectExactFacts(float target[3], float* size, uint64_t* frame);
// hand_direct.cpp: the mesh translation that the draw saw for an object.
bool HandDirectCandidatePosition(void* object, float out[3]);

namespace {

enum State { kIdle, kAim, kLock, kPull, kFetch, kTake, kHold };
const char* const kStateName[7] = {"idle",  "aim",  "lock", "pull",
                                   "fetch", "take", "hold"};

// The frames the object stays in the record before the press goes out: the
// interface of the game reads the record one time a frame. The time that the
// game has to take the object is near_math::kFetchMs.
const int kPullFrames = 2;
// The pull counts only after the lock has stood this long, so the motion that
// closes the grip is not a pull.
const double kLockSeconds = 0.06;

// CHand fields. See docs/hand.md, "CHand fields".
const int kHandHeld = 0x4904;
const int kHandState = 0x4878;
const int32_t kStateHolding = 4;
// Object: the LH3DObject at +0x40, its translation at +0x38.
const int kObjectMesh = 0x40;
const int kMeshTranslation = 0x38;

FarGrabSettings g_set;
bool g_installed = false;
bool g_hooked = false;

int g_state = kIdle;
FarPick g_pick = {};
bool g_have_pick = false;
// The class of the target. The caller writes a new name to its own buffer at
// each selection, so the far grab keeps its copy.
char g_pick_name[48] = "";
FarFrame g_f = {};
bool g_have_frame = false;

// The lock.
float g_lock_grip[3] = {0.0f, 0.0f, 0.0f};  // the grip at the lock, meters
float g_lock_p[3] = {0.0f, 0.0f, 0.0f};     // to the head, a unit vector
double g_lock_time = 0.0;
uint64_t g_lock_frame = 0;
int g_pull_wait = 0;
uint64_t g_pull_frame = 0;
uint64_t g_fetch_frame = 0;
double g_fetch_time = 0.0;  // the time of the press of a fetch, in seconds

// The grip, as the pump saw it. A grip that is still closed when a lock ends
// stays held back until it opens.
bool g_pump_grip = false;
bool g_hold_open = false;

// THE GRIP ON THE CREATURE (the Hold state): the press, and whether the frame
// said that the far grab cannot run (FarGrabFrame). The pump then ends the
// press as for a hand that it lost.
stay_math::Press g_press;
bool g_press_lost = false;

// THE GLIDE.
struct Glide {
  bool armed;        // an object was fetched, and the glide waits or runs
  bool started;
  void* object;
  float p0[3];
  float seconds;
  double armed_at;
  double start;
  double last_pulse;
  int logs;
};
Glide g_glide = {};
const int kMaxGlideLogs = 24;
const double kGlideWaitSeconds = 4.0;

// For the log and the report.
int g_logs = 0;
const int kMaxLogs = 200;
uint64_t g_targets = 0;
uint64_t g_locks = 0;
uint64_t g_pulls = 0;
uint64_t g_fetches = 0;
uint64_t g_cancels = 0;
uint64_t g_refused = 0;
uint64_t g_glides = 0;
uint64_t g_press_frames = 0;
uint64_t g_far_takes = 0;
uint64_t g_far_touches = 0;
uint64_t g_far_leashes = 0;
uint64_t g_far_stays = 0;
uint64_t g_forest_trees = 0;
uint64_t g_gone = 0;
uint64_t g_gone_takes = 0;  // of them, in the Take state with the grip closed
// Where the target was at the lock, for the log of how far it moved.
float g_lock_at[3] = {0.0f, 0.0f, 0.0f};

bool LogRoom() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}



double Meters(const float a[3], const float b[3]) {
  const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return sqrt(dx * dx + dy * dy + dz * dz);
}

// True when the state that ends now must hold a closed grip back until it
// opens (controller_math::FarHoldsBack, the key far_take_carry).
bool HoldsBackNow() {
  return controller_math::FarHoldsBack(g_pump_grip, g_state == kTake,
                                       g_set.take_carry != 0);
}

// Cancel a lock, a pull or a fetch. A grip that is still closed stays held
// back until it opens.
void Cancel(const char* why) {
  if (g_state == kIdle) return;
  if (g_state == kHold) {
    if (g_press.stay) CreatureStayEnd(why, g_f.now, g_f.frame);
    g_press = stay_math::Press();
  }
  ++g_cancels;
  if (LogRoom()) {
    ModLog("far grab    : the %s ends: %s, frame %llu", kStateName[g_state], why,
           g_f.frame);
  }
  if (HoldsBackNow()) g_hold_open = true;
  g_state = kIdle;
  g_have_pick = false;
}

void ArmGlide() {
  g_glide.armed = true;
  g_glide.started = false;
  g_glide.object = g_pick.object;
  for (int i = 0; i < 3; ++i) g_glide.p0[i] = g_pick.position[i];
  g_glide.seconds = 0.0f;
  g_glide.armed_at = g_f.now;
  g_glide.start = 0.0;
  g_glide.last_pulse = 0.0;
  g_glide.logs = 0;
}

void EnterFetch() {
  // The object drifted out of the influence while the lock held: the game
  // would refuse the press.
  if (!g_pick.inside && !g_pick.bulk && !g_pick.touch) {
    Cancel("the object left your influence");
    return;
  }
  g_state = kFetch;
  g_fetch_frame = g_f.frame;
  g_fetch_time = g_f.now;
  // A bulk source and a scroll do not come to the Hand, so they have no pull
  // and no glide.
  if (!g_pick.bulk && !g_pick.touch) {
    ++g_pulls;
    ArmGlide();
  }
  if (LogRoom()) {
    ModLog("far grab    : the grip goes to the game as a press, the next "
           "record holds %p and the land point is %.1f %.1f. The object moved "
           "%.1f units while the lock held, frame %llu",
           g_pick.object, g_pick.land[0], g_pick.land[1],
           Meters(g_pick.position, g_lock_at), g_f.frame);
  }
}

void TestPull() {
  if (!g_f.have_motion) return;
  if (g_f.now - g_lock_time < kLockSeconds) return;
  float scaled[3];
  const float want = g_set.pull_cmps * 0.01f;
  const float k = want > 0.05f ? controller_math::kPullSpeedMps / want : 1.0f;
  for (int i = 0; i < 3; ++i) scaled[i] = g_f.grip_velocity[i] * k;
  float moved = 0.0f;
  for (int i = 0; i < 3; ++i) {
    moved += (g_f.grip_room[i] - g_lock_grip[i]) * g_lock_p[i];
  }
  float along = 0.0f;
  if (!controller_math::PullDetect(scaled, g_lock_p, moved, &along)) return;
  g_state = kPull;
  g_pull_wait = kPullFrames;
  g_pull_frame = g_f.frame;
  if (LogRoom()) {
    ModLog("far grab    : pull, %.2f m/s along the line to the head, the "
           "grip moved %.1f cm along it, %.0f ms after the lock, frame %llu",
           along / k, moved * 100.0f, (g_f.now - g_lock_time) * 1000.0,
           g_f.frame);
  }
}

// THE GLIDE. After CHand::PrepareForDrawing, in each eye pass. In the Holding
// state the game writes the held object's mesh translation to the Hand's
// position plus the hang offset (0x0046CA60 to 0x0046CA97) [V]. This writes
// over it, so the object is drawn on a curve from where it was to that point.
// The time comes from FarGrabFrame, so both eyes draw the same point.
void GlidePrepare(void* hand) {
  if (!g_glide.armed || hand == nullptr) return;
  bool active = false;
  void* ours = HandDirectHand(&active);
  if (!active || hand != ours) return;
  const uint8_t* h = static_cast<const uint8_t*>(hand);
  const int32_t state = *reinterpret_cast<const int32_t*>(h + kHandState);
  void* held = *reinterpret_cast<void* const*>(h + kHandHeld);
  if (held != g_glide.object) return;
  // Until the Holding state, the game does not write the object's mesh
  // translation. The glide then goes to the Hand's point, and in the Holding
  // state it goes to the point that the game writes (the Hand's point and the
  // hang offset, about 1 cm of the room lower).
  float hand_point[3], hand_size = 0.0f;
  uint64_t hand_frame = 0;
  if (state != kStateHolding &&
      !HandDirectExactFacts(hand_point, &hand_size, &hand_frame)) {
    return;
  }
  uint8_t* const* link =
      reinterpret_cast<uint8_t* const*>(static_cast<uint8_t*>(held) +
                                        kObjectMesh);
  if (IsBadReadPtr(link, sizeof(void*)) || *link == nullptr) return;
  float* translation = reinterpret_cast<float*>(*link + kMeshTranslation);
  if (IsBadWritePtr(translation, 3 * sizeof(float))) return;
  float p1[3] = {translation[0], translation[1], translation[2]};
  if (state != kStateHolding) {
    for (int i = 0; i < 3; ++i) p1[i] = hand_point[i];
  }
  if (!g_glide.started) {
    g_glide.started = true;
    g_glide.start = g_f.now;
    const float upm = g_f.units_per_meter > 0.1f ? g_f.units_per_meter : 7.5f;
    g_glide.seconds = controller_math::GlideSeconds(
        static_cast<float>(Meters(g_glide.p0, p1) / upm));
    ++g_glides;
    if (LogRoom()) {
      ModLog("far grab    : the glide starts, %.1f m of the room, %.2f s, from "
             "%.1f %.1f %.1f to %.1f %.1f %.1f, frame %llu",
             Meters(g_glide.p0, p1) / upm, g_glide.seconds, g_glide.p0[0],
             g_glide.p0[1], g_glide.p0[2], p1[0], p1[1], p1[2], g_f.frame);
    }
  }
  const float u = static_cast<float>((g_f.now - g_glide.start) /
                                     g_glide.seconds);
  if (u >= 1.0f) {
    g_glide.armed = false;
    if (LogRoom()) {
      ModLog("far grab    : the glide ends, frame %llu", g_f.frame);
    }
    return;
  }
  float p[3];
  controller_math::GlideArc(g_glide.p0, p1, u, p);
  for (int i = 0; i < 3; ++i) translation[i] = p[i];
  if (g_glide.logs < kMaxGlideLogs) {
    ++g_glide.logs;
    float seen[3] = {0.0f, 0.0f, 0.0f};
    const bool saw = HandDirectCandidatePosition(held, seen);
    ModLog("far grab    : glide u %.2f, state %d, the object is drawn at %.1f "
           "%.1f %.1f toward %.1f %.1f %.1f, the draw of this frame saw %.1f "
           "%.1f %.1f%s, frame %llu", u, state, p[0], p[1], p[2], p1[0], p1[1],
           p1[2], seen[0], seen[1], seen[2], saw ? "" : " (not offered)",
           g_f.frame);
  }
}

}  // namespace

void FarGrabSetOptions(const FarGrabSettings& settings) {
  g_set = settings;
  if (g_set.cone_deg < 2) g_set.cone_deg = 2;
  if (g_set.cone_deg > 20) g_set.cone_deg = 20;
  if (g_set.pull_cmps < 20) g_set.pull_cmps = 20;
  if (g_set.pull_cmps > 400) g_set.pull_cmps = 400;
  // 0 or less is no limit. A range below 0.5 m leaves no room past the near
  // objects of the Hand.
  if (!(g_set.max_m > 0.0f)) g_set.max_m = 0.0f;
  if (g_set.max_m > 0.0f && g_set.max_m < 0.5f) g_set.max_m = 0.5f;
  if (g_set.max_m > 100.0f) g_set.max_m = 100.0f;
  g_set.choice = far_choice_math::ClampChoice(g_set.choice);
  if (!(g_set.living_deg >= 0.5f)) g_set.living_deg = 0.5f;
  if (g_set.living_deg > 20.0f) g_set.living_deg = 20.0f;
  if (!(g_set.ball_living_deg >= 0.0f)) g_set.ball_living_deg = 0.0f;
  if (g_set.ball_living_deg > 20.0f) g_set.ball_living_deg = 20.0f;
  if (!(g_set.ball_other_deg >= 0.0f)) g_set.ball_other_deg = 0.0f;
  if (g_set.ball_other_deg > 20.0f) g_set.ball_other_deg = 20.0f;
  g_set.stay = g_set.stay != 0 ? 1 : 0;
  if (!(g_set.stay_deg >= 0.0f)) g_set.stay_deg = 0.0f;
  if (g_set.stay_deg > 20.0f) g_set.stay_deg = 20.0f;
  g_set.take_carry = g_set.take_carry != 0 ? 1 : 0;
  if (!g_set.on) {
    ModLog("far grab    : far_grab=0, the Hand takes only what it touches");
  } else {
    ModLog("far grab    : on. Point the hand at an object, close the grip and "
           "pull toward the head. A new target needs te %d degrees and stays "
           "to %d, the pull is %d cm/s or a move of 12 cm. throw_law=%d",
           g_set.cone_deg, g_set.cone_deg + 6, g_set.pull_cmps,
           g_set.throw_law ? 1 : 0);
    ModLog("far grab    : far_grab_choice=%d. %s", g_set.choice,
           FarGrabChoiceWords(g_set.choice));
    ModLog("far grab    : the cone of a living thing is %.1f degrees "
           "(far_grab_living_deg). The ball of a living thing is %.1f degrees "
           "and the ball of each other thing %.1f (far_grab_ball_living_deg, "
           "far_grab_ball_other_deg, for far_grab_choice 3 and 4)",
           g_set.living_deg, g_set.ball_living_deg, g_set.ball_other_deg);
    ModLog("far grab    : far_grab_stay=%d. %s. The margin is %.1f degrees "
           "(far_grab_stay_deg)", g_set.stay, FarGrabStayWords(g_set.stay),
           g_set.stay_deg);
    ModLog("far grab    : far_take_carry=%d. %s", g_set.take_carry,
           FarGrabTakeCarryWords(g_set.take_carry));
    if (g_set.max_m > 0.0f) {
      ModLog("far grab    : a target is at most %.2f m of the room from the "
             "head, along the floor (far_grab_max_m). A pile of a store, a "
             "field and a fish farm are targets: the closed grip takes from "
             "them at a distance. A forest is a target: the pull gives one "
             "new tree", g_set.max_m);
    } else {
      ModLog("far grab    : far_grab_max_m=0, so a target can be at any "
             "distance");
    }
  }
}

bool FarGrabOn() { return g_set.on; }
bool FarGrabThrowLaw() { return g_set.throw_law; }
float FarGrabSelectDeg() { return static_cast<float>(g_set.cone_deg); }
float FarGrabKeepDeg() { return static_cast<float>(g_set.cone_deg + 6); }
float FarGrabMaxMeters() { return g_set.max_m; }
int FarGrabChoice() { return g_set.choice; }
void FarGrabSetChoice(int choice) {
  g_set.choice = far_choice_math::ClampChoice(choice);
}
float FarGrabLivingDeg() { return g_set.living_deg; }
float FarGrabBallLivingDeg() { return g_set.ball_living_deg; }
float FarGrabBallOtherDeg() { return g_set.ball_other_deg; }
int FarGrabStay() { return g_set.stay; }
void FarGrabSetStay(int stay) { g_set.stay = stay != 0 ? 1 : 0; }
float FarGrabStayDeg() { return g_set.stay_deg; }
int FarGrabTakeCarry() { return g_set.take_carry; }
void FarGrabSetTakeCarry(int carry) { g_set.take_carry = carry != 0 ? 1 : 0; }

const char* FarGrabChoiceWords(int choice) {
  switch (choice) {
    case far_choice_math::kChoicePoint:
      return "The angle is measured to one point of the object, its base, "
             "less the angle of its radius. No class counts. The Creature "
             "command reaches 4 m of the room";
    case far_choice_math::kChoiceBias:
      return "The angle is measured to the body of the object (its base, its "
             "height and its radius, and at least 2 degrees wide). A "
             "villager, an animal and your Creature score 3 degrees better "
             "than a tree or a rock, and a small body scores better than a "
             "large one. The Creature command reaches 10 m of the room";
    case far_choice_math::kChoiceRanks:
      return "The angle is measured to the body of the object. A living "
             "thing in its cone comes before each thing that is not living, "
             "and of two living things the nearest to the ray wins. A "
             "villager of each kind, an animal and a Creature are living "
             "things. The Creature command reaches 10 m of the room";
    case far_choice_math::kChoiceBalls:
      return "Each object is a ball at its origin, and a living thing a "
             "ball at the middle of its body. The size of the ball comes "
             "from the class. No class comes first. The Creature command "
             "reaches 10 m of the room";
    default:
      return "Each object is a ball at its origin, and a living thing a "
             "ball at the middle of its body. A living thing in its cone "
             "comes before each thing that is not living. The Creature "
             "command reaches 10 m of the room";
  }
}

const char* FarGrabStayWords(int stay) {
  return stay != 0
             ? "A living thing that has the halo keeps it against each thing "
               "that is not living while the aim is within its cone and the "
               "margin. With ranks, a different living thing takes it when it "
               "is 1 degree nearer to the ray"
             : "The halo goes to a different thing when its score is 2 "
               "degrees lower, and a living thing has its rank only in its "
               "cone";
}

const char* FarGrabTakeCarryWords(int carry) {
  return carry != 0
             ? "When the source of a take at a distance goes away while the "
               "grip is closed, the grip machine keeps the grip, and the grip "
               "carries the bundle"
             : "When the source of a take at a distance goes away while the "
               "grip is closed, the far grab holds the grip back until it "
               "opens. The grip machine then sees an open grip and lets the "
               "bundle go";
}

void FarGrabInstall() {
  if (g_hooked) return;
  g_hooked = true;
  const bool ok = HandExactAddPrepareHook(&GlidePrepare);
  ModLog("hooks        : the glide of a far grab, after "
         "CHand::PrepareForDrawing, %s", ok ? "ok" : "FAILED");
}

void FarGrabFrame(const FarFrame& f) {
  g_f = f;
  g_have_frame = true;
  CreatureStayFrame(f.now, f.frame);
  // A glide that never started: the Hand did not hold the object.
  if (g_glide.armed && !g_glide.started &&
      f.now - g_glide.armed_at > kGlideWaitSeconds &&
      g_state != kFetch) {
    g_glide.armed = false;
  }
  if (g_glide.armed && g_glide.started && f.held != g_glide.object) {
    g_glide.armed = false;
  }
  if (g_glide.armed && g_glide.started && f.now - g_glide.start > 3.0) {
    g_glide.armed = false;
  }
  // THE OBJECT ARRIVES: a pulse on each frame of the last 0.125 s of the
  // glide, and none before it (haptics_math::ArriveNow).
  if (g_glide.armed && g_glide.started &&
      haptics_math::ArriveNow(g_glide.seconds,
                              static_cast<float>(f.now - g_glide.start))) {
    g_glide.last_pulse = f.now;
    HapticsPulse(haptics_math::kArrive);
  }
  if (!g_set.on) {
    if (g_state != kIdle) Cancel("far_grab is off");
    return;
  }
  switch (g_state) {
    case kIdle:
      break;
    case kAim:
      if (!f.ready) {
        g_state = kIdle;
        g_have_pick = false;
      }
      break;
    case kLock:
      if (!f.ready) {
        Cancel(f.why != nullptr ? f.why : "the Hand cannot take it now");
      } else {
        TestPull();
      }
      break;
    case kPull:
      if (!f.ready) {
        Cancel(f.why != nullptr ? f.why : "the Hand cannot take it now");
      } else if (--g_pull_wait <= 0) {
        EnterFetch();
      }
      break;
    case kFetch: {
      ++g_press_frames;
      // The time since the press went out, in ms of real time.
      const double fetch_ms = (f.now - g_fetch_time) * 1000.0;
      if (!f.usable) {
        Cancel("the controller does not place the Hand");
      } else if (g_pick.touch) {
        // THE FAR TOUCH: the press is out, and the game tapped the scroll on
        // its first step. The press goes up now, and the grip stays held back
        // until it opens.
        if (near_math::TouchOver(fetch_ms, f.frame - g_fetch_frame)) {
          ++g_far_touches;
          if (LogRoom()) {
            ModLog("far grab    : the press on the scroll %p was down for "
                   "%.0f ms (%llu frames), so the game had it. The far touch "
                   "is over, frame %llu", g_pick.object, fetch_ms,
                   f.frame - g_fetch_frame, f.frame);
          }
          if (g_pump_grip) g_hold_open = true;
          g_state = kIdle;
          g_have_pick = false;
        }
      } else if (g_pick.bulk) {
        // A BULK SOURCE: the game takes from it now, or it refused the press.
        if (f.take == g_pick.object) {
          ++g_far_takes;
          g_state = kTake;
          if (LogRoom()) {
            ModLog("far grab    : the game takes from %p at a distance, %llu "
                   "frames after the press. The take runs while the grip is "
                   "closed, frame %llu", g_pick.object,
                   f.frame - g_fetch_frame, f.frame);
          }
        } else if (fetch_ms > near_math::kFetchMs) {
          Cancel("the game did not start the take");
        }
      } else if (f.held == g_pick.object ||
                 (g_pick.forest && f.held != nullptr)) {
        ++g_fetches;
        if (g_pick.forest && f.held != g_pick.object) {
          // A FOREST gave a new tree. The glide draws that tree from the
          // forest's point, where the game made it.
          ++g_forest_trees;
          if (g_glide.armed && !g_glide.started) g_glide.object = f.held;
          if (LogRoom()) {
            ModLog("far grab    : the forest %p gave the new object %p, %llu "
                   "frames after the press, state %d, frame %llu",
                   g_pick.object, f.held, f.frame - g_fetch_frame,
                   f.hand_state, f.frame);
          }
        } else if (LogRoom()) {
          ModLog("far grab    : the Hand holds %p, %llu frames after the "
                 "press, state %d, frame %llu", g_pick.object,
                 f.frame - g_fetch_frame, f.hand_state, f.frame);
        }
        g_state = kIdle;
        g_have_pick = false;
      } else if (fetch_ms > (g_pick.forest ? near_math::kForestFetchMs
                                           : near_math::kFetchMs)) {
        g_glide.armed = false;
        Cancel(g_pick.forest ? "the forest gave no tree"
                             : "the game did not take the object");
      }
      break;
    }
    case kTake:
      // The grip machine owns the grip now. The state only keeps the target
      // and the tether until the game ends the take.
      if (f.take != g_pick.object) {
        if (LogRoom()) {
          ModLog("far grab    : the take at a distance is over, frame %llu",
                 f.frame);
        }
        g_state = kIdle;
        g_have_pick = false;
      }
      break;
    case kHold:
      // The pump has the rule of the press (PumpHold). A different control
      // in use (the trigger, a grip on the world) does not end it. A view
      // that the far grab cannot run in does.
      g_press_lost = !f.usable;
      break;
  }
}

void FarGrabSelect(const FarPick& pick) {
  if (!g_set.on || pick.object == nullptr) return;
  if (g_state == kLock || g_state == kPull || g_state == kFetch ||
      g_state == kTake || g_state == kHold) {
    if (pick.object == g_pick.object) {
      for (int i = 0; i < 3; ++i) g_pick.position[i] = pick.position[i];
      g_pick.inside = pick.inside;
      g_pick.te = pick.te;
      g_pick.hand_m = pick.hand_m;
      g_pick.head_m = pick.head_m;
      g_pick.land[0] = pick.land[0];
      g_pick.land[1] = pick.land[1];
    }
    return;
  }
  const bool changed = !g_have_pick || pick.object != g_pick.object;
  const bool was_inside = g_have_pick && g_pick.inside;
  g_pick = pick;
  strcpy_s(g_pick_name, pick.name != nullptr ? pick.name : "unknown");
  g_pick.name = g_pick_name;
  g_have_pick = true;
  g_state = kAim;
  if (changed) {
    ++g_targets;
    HapticsPulse(haptics_math::kTarget);
    if (LogRoom()) {
      ModLog("far grab    : target %p (%s%s) at %.1f degrees from the ray, "
             "%.2f m of the room from the Hand and %.2f m from the head, %s "
             "your influence. Why: %s%s, frame %llu", pick.object,
             pick.name != nullptr ? pick.name : "unknown",
             pick.leash != 0 ? ", your Creature: the grip changes the leash"
             : pick.bulk ? ", a bulk source"
             : pick.touch ? ", a scroll: the grip activates it"
             : pick.forest ? ", a forest: the fetch gives a tree" : "",
             pick.te, pick.hand_m,
             pick.head_m, pick.inside ? "inside" : "outside",
             pick.why != nullptr ? pick.why : "not known",
             pick.runner != nullptr ? pick.runner : "", g_f.frame);
    }
  } else if (was_inside != pick.inside && LogRoom()) {
    ModLog("far grab    : the target %p is now %s your influence, frame %llu",
           pick.object, pick.inside ? "inside" : "outside", g_f.frame);
  }
}

void FarGrabNoTarget() {
  if (g_state != kAim) return;
  if (LogRoom()) {
    ModLog("far grab    : no target now, frame %llu", g_f.frame);
  }
  g_state = kIdle;
  g_have_pick = false;
}

bool FarGrabLocked() {
  return g_state == kLock || g_state == kPull || g_state == kFetch ||
         g_state == kTake || g_state == kHold;
}

bool FarGrabTaking() { return g_state == kTake; }

void* FarGrabTargetObject() { return g_have_pick ? g_pick.object : nullptr; }

bool FarGrabTakeSource(FarTakeSource* out) {
  if (!g_set.on || !g_have_frame || !g_have_pick || !g_pick.bulk) return false;
  // The press goes to the game in Pull and Fetch, and the game starts the
  // stream in the game turn of the press, before FarGrabFrame reads the take.
  if (g_state != kPull && g_state != kFetch && g_state != kTake) return false;
  out->object = g_pick.object;
  out->vtable = g_pick.vtable;
  out->name = g_pick.name;
  for (int i = 0; i < 3; ++i) out->position[i] = g_pick.position[i];
  out->units_per_meter = g_f.units_per_meter;
  return true;
}

bool FarGrabTargetFacts(void** object, void** vtable, const void** model) {
  if (!g_have_pick) return false;
  *object = g_pick.object;
  *vtable = g_pick.vtable;
  *model = g_pick.model;
  return true;
}

void FarGrabTrack(const float position[3], const float land[2], bool inside) {
  if (!g_have_pick) return;
  for (int i = 0; i < 3; ++i) g_pick.position[i] = position[i];
  g_pick.land[0] = land[0];
  g_pick.land[1] = land[1];
  // A bulk source and a scroll do not move, and their test has more parts.
  // The Creature needs no influence (FarSelect).
  if (!g_pick.bulk && !g_pick.touch && g_pick.leash == 0) {
    if (inside != g_pick.inside && LogRoom()) {
      ModLog("far grab    : the locked target %p is now %s your influence, "
             "frame %llu", g_pick.object, inside ? "inside" : "outside",
             g_f.frame);
    }
    g_pick.inside = inside;
  }
  // The object moved before the glide started: it starts from here.
  if (g_glide.armed && !g_glide.started && g_glide.object == g_pick.object) {
    for (int i = 0; i < 3; ++i) g_glide.p0[i] = position[i];
  }
}

void FarGrabGone() {
  if (!g_have_pick) return;
  ++g_gone;
  const bool locked = g_state == kLock || g_state == kPull ||
                      g_state == kFetch || g_state == kTake ||
                      g_state == kHold;
  const bool holds_back = locked && HoldsBackNow();
  // THE SOURCE OF A TAKE IS GONE WITH THE GRIP CLOSED: the game deletes a
  // pile that is used up, in the game turn in which it ends the take.
  const bool take_closed = g_state == kTake && g_pump_grip;
  if (take_closed) ++g_gone_takes;
  if (LogRoom()) {
    ModLog("far grab    : the target %p is gone: the game deleted the object "
           "in the %s state. The mod lets it go and does not read it again. "
           "%s, frame %llu", g_pick.object, kStateName[g_state],
           !g_pump_grip ? "The grip is open"
           : holds_back && take_closed
                 ? "The grip is closed, and the far grab holds it back until "
                   "it opens, so the grip machine sees an open grip "
                   "(far_take_carry=0)"
           : holds_back ? "The grip is closed, and the far grab holds it back "
                          "until it opens"
           : take_closed ? "The grip is closed and stays with the grip "
                           "machine, which carries the bundle "
                           "(far_take_carry=1)"
                         : "The grip is closed and stays with the grip machine",
           g_f.frame);
  }
  if (g_glide.object == g_pick.object) g_glide.armed = false;
  if (holds_back) g_hold_open = true;
  if (g_state == kHold) {
    CreatureStayGone();
    g_press = stay_math::Press();
  }
  g_state = kIdle;
  g_have_pick = false;
}

bool FarGrabFetching(void** object, float position[3], float land[2]) {
  if (!g_have_pick || (g_state != kPull && g_state != kFetch)) return false;
  *object = g_pick.object;
  for (int i = 0; i < 3; ++i) position[i] = g_pick.position[i];
  land[0] = g_pick.land[0];
  land[1] = g_pick.land[1];
  return true;
}

bool FarGrabGlideOrigin(void** object, float origin[3]) {
  if (!g_glide.armed || g_glide.object == nullptr) return false;
  *object = g_glide.object;
  for (int i = 0; i < 3; ++i) origin[i] = g_glide.p0[i];
  return true;
}

namespace {

// What a leash click on the Creature does, for the log. kind is
// near_math::LeashClick at the selection.
const char* LeashWords(int kind, bool sent) {
  return !sent ? "The game would not take a leash click now, so the mod sent "
                 "none"
         : kind == 1 ? "The leash click (packet 0x5F): the leash goes on"
         : kind == 2 ? "The leash click (packet 0x5F): the leash comes off"
                     : "The leash click (packet 0x5F): the leash comes back "
                       "to the Hand";
}

// THE GRIP ON THE CREATURE, one step for each pump (stay_math::Step). hand is
// true while the controller can hold the grip. The answer is true while the
// grip must be held back from the grip machine.
bool PumpHold(bool grip, bool hand) {
  const double held_ms = stay_math::HeldMs(g_press, g_f.now);
  const bool can = hand && !g_press_lost;
  const int event =
      stay_math::Step(&g_press, grip, can, g_f.now, CreatureStayHoldMs());
  if (event == stay_math::kStayStart) {
    // The pulse of a lock: the grip holds the Creature now.
    if (CreatureStayStart(g_pick.object, g_pick.vtable, g_f.now, g_f.frame)) {
      ++g_far_stays;
      HapticsPulse(haptics_math::kLock);
    } else {
      HapticsPulse(haptics_math::kRefused);
    }
    return true;
  }
  if (g_press.down) return true;
  // THE PRESS IS OVER.
  if (event == stay_math::kStayEnd) {
    CreatureStayEnd(can ? "the grip opened"
                        : "the hand cannot hold the grip now",
                    g_f.now, g_f.frame);
  } else if (event == stay_math::kClick) {
    // A SHORT GRIP: the leash click of the game, and no press. The pulse
    // comes when the game changed the leash (creature_command.cpp,
    // LeashFrame).
    const bool sent = HandDirectLeashClick(g_pick.object);
    if (sent) ++g_far_leashes;
    if (LogRoom()) {
      ModLog("far grab    : the grip opened after %.0f ms, a short grip on "
             "your Creature %p. %s. No press goes to the game, so the "
             "Creature state does not start, frame %llu", held_ms,
             g_pick.object, LeashWords(g_pick.leash, sent), g_f.frame);
    }
  } else if (LogRoom()) {
    ModLog("far grab    : the hand cannot hold the grip now, %.0f ms after "
           "it closed on your Creature %p. The player did not open the grip, "
           "so the mod sends no leash click, frame %llu", held_ms,
           g_pick.object, g_f.frame);
  }
  g_press_lost = false;
  g_state = kAim;
  if (grip) g_hold_open = true;
  return g_hold_open && grip;
}

}  // namespace

bool FarGrabPumpGrip(bool grip, bool ok, bool hand) {
  const bool edge = grip && !g_pump_grip;
  g_pump_grip = grip;
  if (!grip) g_hold_open = false;
  if (!g_set.on) return false;
  if (g_state == kHold) return PumpHold(grip, hand);
  if (!ok) {
    if (g_state == kLock || g_state == kPull) {
      Cancel("the hand cannot act now");
    } else if (g_state == kAim) {
      g_state = kIdle;
      g_have_pick = false;
    }
    return g_hold_open && grip;
  }
  if (g_state == kAim && edge && g_have_pick) {
    if (!g_pick.inside) {
      ++g_refused;
      if (LogRoom()) {
        ModLog("far grab    : the grip closed on %p, which is outside your "
               "influence, so the game would refuse it and the mod does "
               "nothing, frame %llu", g_pick.object, g_f.frame);
      }
      return g_hold_open && grip;
    }
    if (g_pick.leash != 0) {
      // THE GRIP ON THE CREATURE. The target is the player's own Creature. No
      // press goes to the game, so the Creature state does not start, however
      // long the grip stays closed: no stroke, no slap and no camera from a
      // distance. The grip stays held back until it opens.
      const int event =
          stay_math::Close(&g_press, CreatureStayMode(), g_f.now);
      if (event != stay_math::kClick) {
        // creature_hold_stay=1: the time decides (PumpHold). A short grip is
        // the leash click, and a grip that stays closed makes the Creature
        // stay.
        g_state = kHold;
        g_press_lost = false;
        CreatureStayPressed(g_pick.object, g_f.now);
        if (LogRoom()) {
          ModLog("far grab    : the grip closed on your Creature %p, %.2f m "
                 "of the room from the Hand. A grip that opens in less than "
                 "%d ms is the leash click. A grip that stays closed makes "
                 "the Creature stay (creature_hold_stay=1). No press goes to "
                 "the game, frame %llu", g_pick.object, g_pick.hand_m,
                 CreatureStayHoldMs(), g_f.frame);
        }
        return true;
      }
      // creature_hold_stay=0: the closed grip sends the leash click of the
      // game (packet 0x5F). The pulse comes when the game changed the leash
      // (creature_command.cpp, LeashFrame).
      g_press = stay_math::Press();
      const bool sent = HandDirectLeashClick(g_pick.object);
      if (sent) ++g_far_leashes;
      g_hold_open = true;
      if (LogRoom()) {
        ModLog("far grab    : the grip closed on your Creature %p, %.2f m of "
               "the room from the Hand. %s. No press goes to the game, so the "
               "Creature state does not start, frame %llu", g_pick.object,
               g_pick.hand_m, LeashWords(g_pick.leash, sent), g_f.frame);
      }
      return true;
    }
    if (g_pick.bulk || g_pick.touch) {
      // A BULK SOURCE needs no pull: the closed grip collects. A SCROLL needs
      // none: the closed grip activates it. The object goes in the pick
      // record for kPullFrames frames, and then the grip goes to the game as
      // a press.
      g_state = kPull;
      g_pull_wait = kPullFrames;
      g_pull_frame = g_f.frame;
      for (int i = 0; i < 3; ++i) g_lock_at[i] = g_pick.position[i];
      HapticsPulse(g_pick.touch ? haptics_math::kActivate
                                : haptics_math::kTake);
      if (LogRoom()) {
        ModLog("far grab    : the grip closed on the %s %p (%s), %.2f m of "
               "the room from the Hand. The %s starts with no pull, frame "
               "%llu", g_pick.touch ? "scroll" : "bulk source", g_pick.object,
               g_pick.name != nullptr ? g_pick.name : "unknown", g_pick.hand_m,
               g_pick.touch ? "far touch" : "take", g_f.frame);
      }
      return true;
    }
    g_state = kLock;
    ++g_locks;
    g_lock_time = g_f.now;
    g_lock_frame = g_f.frame;
    for (int i = 0; i < 3; ++i) g_lock_at[i] = g_pick.position[i];
    for (int i = 0; i < 3; ++i) g_lock_grip[i] = g_f.grip_room[i];
    float p[3];
    for (int i = 0; i < 3; ++i) p[i] = g_f.head_room[i] - g_f.grip_room[i];
    const float length = controller_math::Length(p);
    if (length > 1e-3f) {
      for (int i = 0; i < 3; ++i) g_lock_p[i] = p[i] / length;
    } else {
      g_lock_p[0] = g_lock_p[1] = 0.0f;
      g_lock_p[2] = -1.0f;
    }
    HapticsPulse(haptics_math::kLock);
    if (LogRoom()) {
      ModLog("far grab    : lock on %p, the pull goes to the head along "
             "%.2f %.2f %.2f, frame %llu", g_pick.object, g_lock_p[0],
             g_lock_p[1], g_lock_p[2], g_f.frame);
    }
  }
  if ((g_state == kLock || g_state == kPull) && !grip) {
    Cancel(g_pick.bulk ? "the grip opened before the take started"
           : g_pick.touch ? "the grip opened before the press went out"
                          : "the grip opened before a pull");
    return false;
  }
  return g_state == kLock || g_state == kPull || (g_hold_open && grip);
}

bool FarGrabPressing() { return g_state == kFetch; }

bool FarGrabDrawState(FarDraw* out) {
  if (!g_set.on || !g_have_frame || !g_have_pick || g_state == kIdle) {
    return false;
  }
  out->active = true;
  out->valid = g_pick.inside;
  // The grip on the Creature has no tether: the Creature does not come to the
  // Hand.
  out->locked = g_state != kAim && g_state != kHold;
  out->bulk = g_pick.bulk;
  out->touch = g_pick.touch;
  out->leash = g_pick.leash;
  out->hold = g_pick.leash != 0 &&
              CreatureStayMode() == stay_math::kModeHold;
  out->stay = g_state == kHold && g_press.stay && CreatureStayHolds();
  for (int i = 0; i < 3; ++i) {
    out->object[i] = g_pick.position[i];
    out->hand[i] = g_f.hand_point[i];
  }
  out->radius = g_pick.radius;
  out->land[0] = g_pick.land[0];
  out->land[1] = g_pick.land[1];
  out->units_per_meter = g_f.units_per_meter;
  out->target = g_pick.object;
  return out->units_per_meter > 0.0f;
}

void FarGrabReport() {
  CreatureStayReport();
  if (g_gone != 0) {
    ModLog("far grab    : %llu targets were deleted by the game while the mod "
           "held them, %llu of them the source of a take with the grip closed "
           "(far_take_carry=%d), this window", g_gone, g_gone_takes,
           g_set.take_carry);
    g_gone = g_gone_takes = 0;
  }
  if (g_targets == 0 && g_locks == 0 && g_cancels == 0 && g_refused == 0) {
    return;
  }
  ModLog("far grab    : %llu targets, %llu locks, %llu pulls, %llu fetches "
         "done (%llu trees from a forest), %llu glides, %llu takes from a "
         "bulk source, %llu far touches of a scroll, %llu leash clicks on "
         "the Creature, %llu stays of the Creature, %llu cancelled, %llu "
         "grips refused outside the influence, %llu frames of fetch",
         g_targets, g_locks, g_pulls, g_fetches, g_forest_trees, g_glides,
         g_far_takes, g_far_touches, g_far_leashes, g_far_stays, g_cancels,
         g_refused, g_press_frames);
  g_far_leashes = g_far_stays = 0;
  g_targets = g_locks = g_pulls = g_fetches = g_glides = g_cancels = 0;
  g_refused = g_press_frames = g_far_takes = 0;
  g_far_touches = g_forest_trees = 0;
}
