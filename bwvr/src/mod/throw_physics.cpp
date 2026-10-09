// The real flight of a thrown object (throw_law=2). See docs/throw.md, "The
// throw physics", and throw_math.h for the math.
//
// WHY. The game's physics is made for a mouse. Its gravity is 9.81 units per
// second squared, which is 1.3 m/s^2 in the room at the life size scale and
// 0.1 m/s^2 at 100 units per meter. It cuts each speed to 124 units per
// second. A throw with the real hand then looks slow: the object leaves the
// hand much slower than the hand moved, and it floats.
//
// WHAT. The player's own throw flies in the room as a real object flies: the
// same arc, the same time, at each scale of the God view and of the table.
// Only that one object changes. Each other object keeps the game's physics.
//
// HOW. A flight with the velocity sqrt(s) v under the game's gravity is the
// real flight, in a time that is sqrt(s) times longer (throw_math.h). So:
//
//   THE STATE. The release sends a velocity that makes the game throw the
//   object. When the game's physics has the object, the mod writes the
//   position of the real arc and the velocity sqrt(s) v to it, with no cap.
//
//   THE STEPS. PhysicsObject::GameTurnUpdate 0x00644FC0 runs 20 substeps of
//   5 ms each game turn. In a substep it calls, for each object, fn_007FD200
//   (the points of the object), fn_007FD4D0 (the weight, the drag and the
//   land), fn_007FDE40 (the contact forces) and fn_007FE260 (the velocity
//   and the position). A hook on fn_007FE260 runs these four again for the
//   thrown object, sqrt(s) times a substep in all. So the game's own rules
//   make the bounce, the roll, the water and the damage, in the real time.
//
//   THE CAP. fn_007FE260 cuts the speed to the float at 0x00C371FC (124).
//   The hook puts a large value there for the steps of the thrown object
//   only, and puts 124 back.
//
//   THE DRAG. The drag is F = -k |v| v with k at PhysOb+0x14C. It is 0 for
//   the thrown object until its first contact, so the arc is the real arc.
//   From the first contact the game's drag is back, because the water needs
//   it.
//
//   OTHER OBJECTS. In a substep where the thrown object touches another
//   physics object, the game's one step runs and no more. The game finds
//   the objects near a physics object once a turn, from its speed. For that
//   search only, the speed of the thrown object is the speed of the real
//   flight, up to 300 units per second.
//
//   THE DAMAGE. In a turn the game adds the force of each of its 20 steps
//   that has a contact to PhysicsObject+0x0C. The length of that sum, as a
//   mean force, is what Living::ReactToPhysicsImpact 0x005ED3E0 reads: more
//   than 2 times the weight hurts. With the mod a turn has more steps. So
//   the hook makes the same sum for each 20 steps in a row, and it gives the
//   game the largest of them. A landing then hurts as it does in the game at
//   the same speed of the game's time, and a slide along the land does not
//   hurt more.
//
//   THE LANDING. The rate of the flight is wrong for the time after the
//   first contact: the game makes its contact for one step a substep, and at
//   5 steps a rock turns 7.5 times a second and shakes. So from the first
//   contact the rate goes to the rate of the land (the game's own time),
//   while the object touches. A hook on fn_007FDE40 cuts the force of the
//   contact on its way back, for a low bounce, and it adds a brake against
//   the velocity while the object touches the land, so the object comes to
//   rest soon. See throw_math.h, LandRate, ReboundFactor and LandBrake.
//
//   THE DRAW. The game draws a physics object where the last turn left it,
//   10 times a second. The mod keeps the object's matrix after each substep
//   with its time in the flight, and a hook on PhysicsObject::DrawAll
//   0x00646DE0 puts the object where the flight is at the time of the frame.
//   Between the release action and the game's first turn with the object,
//   the game still holds it in the Hand. Then the mod draws it on the real
//   arc from the release.
//
// ONCE A FRAME. The draw hooks run once for each eye. The matrix of a frame
// is latched on the first one. The physics hooks run in the game turn, and
// they keep no state of a frame.
//
// The mouse player's throw does not pass through here: hand_direct.cpp calls
// ThrowPhysicsPlan only while the controller places the Hand. A multiplayer
// game keeps the square root law, because all players must run the same
// physics.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "controller_math.h"
#include "throw_math.h"
#include "throw_physics.h"

extern void ModLog(const char* format, ...);
// controllers.cpp: the count of the frames.
uint64_t ControllersFrameCount();
// hand_exact.cpp: a call after CHand::PrepareForDrawing, in each eye pass.
bool HandExactAddPrepareHook(void (*hook)(void* hand));
// controllers.cpp: a short pulse of the hand that points.
void ControllersGesturePulse(float amplitude, float seconds);
// hand_direct.cpp: the CHand that the controller places.
void* HandDirectHand(bool* active);
// hand_direct.cpp: the Hand's point after the land test, and the size.
bool HandDirectExactFacts(float target[3], float* size, uint64_t* frame);

namespace {

// THE PHYSICS OBJECTS [V, PhysicsObject::GameTurnUpdate 0x00644FE0 to
// 0x00645026]. An array of 0x1DC bytes an entry at *0x00D47814, with the
// count at 0x00D47818. The game takes an entry out by a copy of the last
// entry over it, so an entry can move. The Object of the entry is the key.
const uintptr_t kPhysicsArray = 0x00D47814;
const uintptr_t kPhysicsCount = 0x00D47818;
const int kEntrySize = 0x1DC;
const int kEntryImpulse = 0x0C;  // the sum of the contact forces of a turn
const int kEntryObject = 0x18;
const int kEntryPhysOb = 0x28;
// PhysOb fields [V, fn_007FD4D0, fn_007FE260, fn_007FD140, fn_007FDA60].
const int kObScale = 0x04;
const int kObPoints = 0x154;     // the count of the points of the object
// The points [V, fn_007FD4D0, fn_007FDE40]: an array of 0x50 bytes a point at
// PhysOb+0x15C, with the depth in the land or in an object at +0x24 and the
// PhysOb that the point touches at +0x4C (null for the land).
const int kObPointList = 0x15C;
const int kPointSize = 0x50;
const int kPointDepth = 0x24;
const int kPointOther = 0x4C;
const int kObHitBy = 0x78;       // the PhysOb that touched this one
const int kObPosition = 0xA0;    // the center, after the 9 floats of the axes
const int kObVelocity = 0xDC;
const int kObForce = 0x104;
const int kObTorque = 0x110;
const int kObMass = 0x134;
// The constants of the contact, from a row of PhysicsConstants.txt
// (PhysOb::SetUpConstants 0x007FB810): the spring and the second spring are
// times the mass, the turn factor is the row's value to the power 0.005.
const int kObSpring = 0x13C;
const int kObSpring2 = 0x140;
const int kObFriction = 0x144;
const int kObTurnKeep = 0x148;
const int kObDrag = 0x14C;
const int kObRadius = 0x150;
const int kObContacts = 0x158;   // the points in contact in this step
const int kObResting = 0x174;    // a byte: the object is at rest
const int kObContact = 0x176;    // a byte: a contact in this step
// The four parts of a substep, thiscall on a PhysOb, no arguments, plain ret
// [V]. fn_007FE260 gives 1 for a step of a moving object, 2 when the object
// came to rest, 4 when it fell under the sea, and 0 or 3 for an object at
// rest.
const uintptr_t kPrepare = 0x007FD200;
const uintptr_t kForces = 0x007FD4D0;
const uintptr_t kContact = 0x007FDE40;
const uintptr_t kIntegrate = 0x007FE260;
// fn_007FD140, thiscall (LHMatrix* out), ret 4: the matrix of the mesh from
// the PhysOb, 9 floats of the axes times the scale, then the position.
const uintptr_t kWorldMatrix = 0x007FD140;
// PhysicsObject::DrawAll, cdecl, no arguments.
const uintptr_t kDrawAll = 0x00646DE0;
// The game's cap of the speed, a float in .data (124).
const uintptr_t kCap = 0x00C371FC;
// The release action 0x005D4DB0 sets the int at 0x00D47824 to 1 and the
// object at 0x00D47828 when it lets go [V, 0x005D4E78].
const uintptr_t kLocalThrow = 0x00D47824;
const uintptr_t kLocalObject = 0x00D47828;
// LH3DIsland::GetAltitude, fastcall (const MapCoords*), the height in st0.
const uintptr_t kGetAltitude = 0x00803090;
const float kMapScale = 6553.6f;
const float kMapEdge = 5119.0f;
// Object and LH3DObject: the mesh at +0x40, and its matrix at +0x14 (the
// position is at +0x38).
const int kObjectMesh = 0x40;
const int kMeshMatrix = 0x14;
const int kHandHeld = 0x4904;
const int kHandState = 0x4878;
const int32_t kStateHolding = 4;
// Vtable slots of Object: 1 for a tree, and 1 for a Miracle in the Hand.
const int kIsAnyKindOfTree = 0x478;
const int kIsSpellSeed = 0x4C4;
// -MULTIPLAYER, and the game mode 3 of GGame.
const uintptr_t kGame = 0x00D0195C;
const uintptr_t kMultiplayerFlag = 0x00D46ABA;
const int kGameMode = 0x25017C;

typedef void(__fastcall* ObFn)(void* ob, void* unused);
typedef int(__fastcall* IntegrateFn)(void* ob, void* unused);
typedef void(__fastcall* WorldMatrixFn)(void* ob, void* unused, float* out);
typedef void(__cdecl* DrawAllFn)();
typedef float(__fastcall* GetAltitudeFn)(const int32_t* coords);
typedef int(__fastcall* VirtualIntFn)(void* self, void* unused);

ObFn o_Prepare = nullptr;
ObFn o_Contact = nullptr;
IntegrateFn o_Integrate = nullptr;
DrawAllFn o_DrawAll = nullptr;

int g_law = 2;
// throw_log: 1 logs each turn and each third frame, 2 also logs each substep
// and each frame after the first contact.
int g_log = 0;
bool g_test_keys = false;
// The time factor k (throw_slow_x10 / 10) and the trees (throw_trees).
float g_slow = 2.0f;
bool g_trees = true;
// THE HEIGHT RULE (release_fall_cm): the limit in meters of the room, 0 for
// a set down at each height. g_fall_off is the A/B key (Shift Scroll Lock).
float g_fall_m = 0.15f;
bool g_fall_off = false;
// THE LANDING (throw_land): 1 is the time of the land with the low bounce and
// the brake, 2 is the low bounce and the brake in the time of the flight, 0
// is the game's own landing. g_land_rate is the rate of the land, the steps in
// a substep (1 is the game's own time). g_bounce is the share of its speed
// that a bounce keeps, of the speed that the game gives it. g_brake is the
// friction of the brake of the land, as a part of the friction that the game
// has for the object (PhysOb+0x144, 1.31 for a rock).
int g_land = 1;
float g_land_rate = 1.0f;
float g_bounce = 0.5f;
float g_brake = 0.6f;
bool g_contact_hook = false;
bool g_installed = false;
bool g_said_multiplayer = false;

// The speed that the search of the near objects sees, at most.
const float kSearchSpeed = 300.0f;
// The game must have the object this long after the release, or the flight
// ends: the game kept the object in the Hand.
const double kHeldLimit = 0.6;
// A flight ends after this time in any case.
const double kFlightLimit = 60.0;
const int kMaxSamples = 128;
const int kMaxFlights = 4;

enum Phase {
  kHeld = 1,     // the game still holds the object: the mod draws the arc
  kPhysics = 2,  // the game's physics has the object
};

struct Flight {
  bool active;
  int phase;
  void* object;
  uint64_t frame0;
  double t0;             // the time of the release
  float p0[3];           // the mesh position at the release
  float v0[3];           // the real flight, world units per second
  float matrix0[12];     // the mesh matrix at the release
  float s;               // world units in a meter of the room
  float gs;              // s / k^2: the gravity of the flight, in s g units
  float scale;           // the steps in a substep: sqrt(s) / k
  float slow;            // the time factor k that this flight uses
  float real_mps;
  // Where the real arc meets the land, or leaves the map.
  bool lands;
  bool leaves;
  double land_t;
  float land[3];
  // The physics.
  double time;           // the seconds of the flight that the state is at
  float remainder;
  float drag;            // the game's drag of the object
  bool drag_off;
  bool search;           // the velocity is scaled for the search of a turn
  float search_velocity[3];
  void* hit_before;      // PhysOb+0x78 at the start of the substep
  bool substep_open;
  bool contact;          // the first contact came
  // The sum of the contact forces of the 20 steps that are open now, and
  // the largest sum of 20 steps of this turn.
  float window[3];
  int window_steps;
  float best[3];
  int last_code;
  int turns;
  int substeps;
  int steps;
  int hit_substeps;
  int turn_steps;
  double step_seconds;
  double turn_seconds;
  bool matrix_checked;
  bool axes_ok;          // the kept axes are those that the game draws
  // The draw.
  throw_math::FlightSample samples[kMaxSamples];
  int count;
  uint64_t draw_frame;
  bool draw_valid;
  float draw_matrix[12];
  int draw_logs;
  // The numbers of the time after the first contact, for the log.
  throw_math::Landing landing;
  throw_math::DrawTrack track;
  int trace_logs;
  // The landing. The flight latches it at the first contact.
  int land_mode;         // throw_land of this flight
  float rate_flight;     // the steps in a substep of the flight
  float rate_land;       // the steps in a substep that the object goes to
  double touch;          // the time of the picture with a point in contact
  float keep;            // the part of the contact force on its way back
  float brake;           // the friction of the brake of the land
  float cut[3];          // the game's force minus the force of this step
  int cut_steps;
  float turn_keep;       // the game's turn factor of the object
  bool turn_changed;
  // A release in the glide of a far fetch: the place of the picture at the
  // release, from the place in the Hand.
  bool has_lead;
  float lead[3];
};

Flight g_flights[kMaxFlights];
int g_active = 0;
int g_flight_logs = 0;
const int kMaxFlightLogs = 600;
// throw_log=2: the lines of one flight, at most.
const int kMaxTraceLogs = 6000;

// The plan of the release of this frame, from ThrowPhysicsPlan.
struct Plan {
  bool valid;
  uint64_t frame;
  void* held;
  throw_math::RealThrow t;
  float s;
};
Plan g_plan = {};
int g_plan_logs = 0;
const int kMaxPlanLogs = 200;

double NowSeconds() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return static_cast<double>(count.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

bool Multiplayer() {
  bool on = *reinterpret_cast<const uint8_t*>(kMultiplayerFlag) != 0;
  void* const* game = reinterpret_cast<void* const*>(kGame);
  if (!IsBadReadPtr(game, sizeof(void*)) && *game != nullptr) {
    on |= *reinterpret_cast<const int32_t*>(
              static_cast<const uint8_t*>(*game) + kGameMode) == 3;
  }
  if (on && !g_said_multiplayer) {
    g_said_multiplayer = true;
    ModLog("hand throw   : this is a multiplayer game, so a throw keeps the "
           "game's physics and the square root law");
  }
  return on;
}

bool OnMap(float x, float z) {
  return x >= 0.0f && x <= kMapEdge && z >= 0.0f && z <= kMapEdge;
}

float Altitude(float x, float z) {
  if (!(x >= 0.0f)) x = 0.0f;
  if (x > kMapEdge) x = kMapEdge;
  if (!(z >= 0.0f)) z = 0.0f;
  if (z > kMapEdge) z = kMapEdge;
  const int32_t coords[3] = {static_cast<int32_t>(x * kMapScale),
                             static_cast<int32_t>(z * kMapScale), 0};
  return reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords);
}

// The entries of the game's physics, or null.
uint8_t* Entries(int* count_out) {
  uint8_t* base = *reinterpret_cast<uint8_t* const*>(kPhysicsArray);
  const int32_t count = *reinterpret_cast<const int32_t*>(kPhysicsCount);
  if (base == nullptr || count <= 0 || count > 4096) return nullptr;
  *count_out = count;
  return base;
}

// The entry of an object, by a compare of pointers only. The object can be
// gone while the entry is not.
uint8_t* FindEntry(void* object) {
  int count = 0;
  uint8_t* base = Entries(&count);
  if (base == nullptr || object == nullptr) return nullptr;
  for (int i = 0; i < count; ++i) {
    uint8_t* entry = base + i * kEntrySize;
    if (*reinterpret_cast<void* const*>(entry + kEntryObject) == object) {
      return entry;
    }
  }
  return nullptr;
}

// The flight of the PhysOb, when the PhysOb is in the game's array and the
// game's physics has the object.
Flight* FlightOfOb(void* ob) {
  if (g_active == 0) return nullptr;
  int count = 0;
  uint8_t* base = Entries(&count);
  if (base == nullptr) return nullptr;
  uint8_t* entry = static_cast<uint8_t*>(ob) - kEntryPhysOb;
  if (entry < base || entry >= base + count * kEntrySize ||
      (entry - base) % kEntrySize != 0) {
    return nullptr;
  }
  void* object = *reinterpret_cast<void* const*>(entry + kEntryObject);
  for (int i = 0; i < kMaxFlights; ++i) {
    Flight& f = g_flights[i];
    if (f.active && f.phase == kPhysics && f.object == object) return &f;
  }
  return nullptr;
}

// A virtual call of an Object with no arguments that gives an int.
bool VirtualTrue(void* object, int slot) {
  void* const* const* link = static_cast<void* const* const*>(object);
  if (IsBadReadPtr(link, sizeof(void*)) || *link == nullptr) return false;
  void* const* vtable = *link;
  if (IsBadReadPtr(vtable + slot / 4, sizeof(void*))) return false;
  return reinterpret_cast<VirtualIntFn>(vtable[slot / 4])(object, nullptr) !=
         0;
}

float* MeshMatrix(void* object) {
  uint8_t* const* link = reinterpret_cast<uint8_t* const*>(
      static_cast<uint8_t*>(object) + kObjectMesh);
  if (IsBadReadPtr(link, sizeof(void*)) || *link == nullptr) return nullptr;
  float* matrix = reinterpret_cast<float*>(*link + kMeshMatrix);
  if (IsBadWritePtr(matrix, 12 * sizeof(float))) return nullptr;
  return matrix;
}

void WorldMatrix(void* ob, float out[12]) {
  reinterpret_cast<WorldMatrixFn>(kWorldMatrix)(ob, nullptr, out);
}

float Flat(const float a[3], const float b[3]) {
  return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2]));
}

float Gap(const float a[3], const float b[3]) {
  return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
               (a[2] - b[2]) * (a[2] - b[2]));
}

// Where the real arc meets the land. It goes along the arc in steps of 2 ms,
// for 6 s at most.
void FindLanding(Flight* f) {
  f->lands = f->leaves = false;
  f->land_t = 0.0;
  for (int k = 1; k <= 3000; ++k) {
    const double t = k * 0.002;
    float p[3], v[3];
    throw_math::RealArc(f->p0, f->v0, f->gs, t, p, v);
    if (!OnMap(p[0], p[2])) {
      f->leaves = true;
      f->land_t = t;
      for (int i = 0; i < 3; ++i) f->land[i] = p[i];
      return;
    }
    const float land = Altitude(p[0], p[2]);
    if (p[1] <= land) {
      f->lands = true;
      f->land_t = t;
      f->land[0] = p[0];
      f->land[1] = land;
      f->land[2] = p[2];
      return;
    }
  }
}

// The real arc at a time of the flight, stopped at the land or at the edge
// of the map.
void ArcAt(const Flight& f, double t, float p[3], float v[3]) {
  if (t < 0.0) t = 0.0;
  if ((f.lands || f.leaves) && t > f.land_t) t = f.land_t;
  throw_math::RealArc(f.p0, f.v0, f.gs, t, p, v);
  if (f.lands && t >= f.land_t) p[1] = f.land[1];
}

// THE PLACE IN THE HAND. The mesh position of the held object on the last
// frame, as the game and hand_exact.cpp wrote it, before the glide of a far
// grab wrote over it. Before the Holding state nothing writes that position,
// and the place is the Hand's point.
struct HandPlace {
  void* object;
  uint64_t frame;
  float p[3];
};
HandPlace g_hand_place = {};

// After CHand::PrepareForDrawing, in each eye pass, before the glide.
void ThrowCapture(void* hand) {
  bool active = false;
  if (hand == nullptr || hand != HandDirectHand(&active) || !active) return;
  const uint8_t* h = static_cast<const uint8_t*>(hand);
  void* held = *reinterpret_cast<void* const*>(h + kHandHeld);
  const uint64_t frame = ControllersFrameCount();
  // Once a frame: the second eye pass has the same place.
  if (held != nullptr && g_hand_place.object == held &&
      g_hand_place.frame == frame) {
    return;
  }
  g_hand_place.object = nullptr;
  if (held == nullptr) return;
  if (*reinterpret_cast<const int32_t*>(h + kHandState) == kStateHolding) {
    const float* matrix = MeshMatrix(held);
    if (matrix == nullptr) return;
    for (int i = 0; i < 3; ++i) g_hand_place.p[i] = matrix[9 + i];
  } else {
    float size = 0.0f;
    uint64_t hand_frame = 0;
    if (!HandDirectExactFacts(g_hand_place.p, &size, &hand_frame)) return;
  }
  g_hand_place.object = held;
  g_hand_place.frame = frame;
}

void PushSample(Flight* f, void* ob) {
  if (f->count == kMaxSamples) {
    memmove(f->samples, f->samples + 1,
            sizeof(f->samples[0]) * (kMaxSamples - 1));
    --f->count;
  }
  throw_math::FlightSample& sample = f->samples[f->count++];
  sample.time = f->time;
  WorldMatrix(ob, sample.matrix);
  const uint8_t* o = static_cast<const uint8_t*>(ob);
  const float* v = reinterpret_cast<const float*>(o + kObVelocity);
  const float* center = reinterpret_cast<const float*>(o + kObPosition);
  for (int i = 0; i < 3; ++i) {
    sample.velocity[i] = v[i] * f->scale;
    sample.center[i] = center[i];
  }
  sample.contact = *reinterpret_cast<const int32_t*>(o + kObContacts) > 0 ||
                   o[kObContact] != 0;
}

// The numbers of the time after the first contact: one sample a substep.
void TrackLanding(Flight* f, const uint8_t* ob) {
  if (!f->contact || f->count < 1) return;
  const throw_math::FlightSample& now = f->samples[f->count - 1];
  const float over = now.center[1] - Altitude(now.center[0], now.center[2]);
  const float turn =
      f->count > 1 && f->landing.samples > 0
          ? throw_math::TurnBetween(f->samples[f->count - 2].matrix,
                                    now.matrix)
          : 0.0f;
  // A bob is a move of 0.5 mm of the room or more.
  throw_math::LandingAdd(&f->landing, now.time, now.center, over, now.contact,
                         turn, f->s * 0.0005f);
  if (g_log >= 2 && f->trace_logs < kMaxTraceLogs) {
    ++f->trace_logs;
    const float* v = reinterpret_cast<const float*>(ob + kObVelocity);
    ModLog("hand throw   : s %.4f c %.2f %.2f %.2f o %.3f n %d k %d v %.2f "
           "%.2f %.2f a %.4f r %.2f", now.time, now.center[0], now.center[1],
           now.center[2], over,
           *reinterpret_cast<const int32_t*>(ob + kObContacts),
           ob[kObContact] != 0 ? 1 : 0, v[0], v[1], v[2], turn, f->scale);
  }
}

// What the object did after its first contact, as one line of the log.
void LogLanding(Flight* f) {
  throw_math::Landing& l = f->landing;
  if (l.samples < 2 || g_flight_logs >= kMaxFlightLogs) return;
  ++g_flight_logs;
  throw_math::LandingEndHop(&l, l.last);
  const double span = l.last - l.first;
  const float cm = 100.0f / f->s;
  const float turns = l.turned / 6.2831853f;
  ModLog("hand throw   : after the first contact, %p moved for %.2f s and "
         "%.2f m of the room across the land, at %.2f m/s at most. It left "
         "the land %d times: the highest hop was %.1f cm, the longest was "
         "%.2f s, and it was in the air for %.0f percent of the time. Its "
         "height over the land changed direction %d times (%.1f a second, "
         "%.1f cm as a mean). It turned %.1f times, at %.1f turns a second "
         "at most. The game's step ran %.2f times a substep at the first "
         "contact and %.2f at the end, and the mod cut the force of the "
         "contact in %d steps. Of %d frames, %d were drawn after "
         "the newest step (%.0f ms at most), and the picture turned back %d "
         "times (%.1f cm at most)",
         f->object, span, l.path / f->s, l.top_speed / f->s, l.hops,
         l.top_hop * cm, l.long_hop,
         span > 1e-6 ? l.air_time / span * 100.0 : 0.0, l.bobs,
         span > 1e-6 ? l.bobs / span : 0.0,
         l.bobs > 0 ? l.bob_sum / l.bobs * cm : 0.0f, turns,
         l.top_turn / 6.2831853f, f->rate_flight, f->scale, f->cut_steps,
         f->track.frames,
         f->track.late, f->track.top_late * 1000.0, f->track.back,
         f->track.top_back * cm);
}

// The end of a flight. The game's drag goes back to the object, when the
// game's physics still has it.
void EndFlight(Flight* f, const char* why) {
  if (!f->active) return;
  uint8_t* entry = f->phase == kPhysics ? FindEntry(f->object) : nullptr;
  if (entry != nullptr) {
    uint8_t* ob = entry + kEntryPhysOb;
    if (f->search) {
      float* v = reinterpret_cast<float*>(ob + kObVelocity);
      for (int i = 0; i < 3; ++i) v[i] = f->search_velocity[i];
    }
    if (f->drag_off) *reinterpret_cast<float*>(ob + kObDrag) = f->drag;
    if (f->turn_changed) {
      *reinterpret_cast<float*>(ob + kObTurnKeep) = f->turn_keep;
    }
  }
  f->search = false;
  f->drag_off = false;
  f->turn_changed = false;
  LogLanding(f);
  if (g_flight_logs < kMaxFlightLogs) {
    ++g_flight_logs;
    float end[3] = {f->p0[0], f->p0[1], f->p0[2]};
    if (f->count > 0) {
      for (int i = 0; i < 3; ++i) {
        end[i] = f->samples[f->count - 1].matrix[9 + i];
      }
    }
    ModLog("hand throw   : the flight of %p is over after %.2f s (%s). It "
           "ended %.1f units (%.2f m of the room) from the release, across "
           "the land. The game ran %d turns, %d substeps and %d steps for "
           "it, and %d substeps touched an object. The steps took %.2f ms",
           f->object, f->time, why, Flat(end, f->p0),
           Flat(end, f->p0) / f->s, f->turns, f->substeps, f->steps,
           f->hit_substeps, f->step_seconds * 1000.0);
  }
  f->active = false;
  if (g_active > 0) --g_active;
}

Flight* FlightOfObject(void* object) {
  for (int i = 0; i < kMaxFlights; ++i) {
    if (g_flights[i].active && g_flights[i].object == object) {
      return &g_flights[i];
    }
  }
  return nullptr;
}

// A free flight, or the oldest one when all are in use.
Flight* NewFlight() {
  Flight* oldest = &g_flights[0];
  for (int i = 0; i < kMaxFlights; ++i) {
    if (!g_flights[i].active) return &g_flights[i];
    if (g_flights[i].t0 < oldest->t0) oldest = &g_flights[i];
  }
  EndFlight(oldest, "a new throw needs its place");
  return oldest;
}

// THE GAME'S PHYSICS HAS THE OBJECT. The game put it at the Hand's point of
// the release, with the velocity that the release sent. The flight is
// further on by now. So the position and the velocity of the real arc at
// this time go to the PhysOb, with the velocity in the game's time.
void StartPhysics(Flight* f, uint8_t* entry, double now) {
  uint8_t* ob = entry + kEntryPhysOb;
  float matrix[12];
  WorldMatrix(ob, matrix);
  const double t = now - f->t0;
  float p[3], v[3];
  ArcAt(*f, t, p, v);
  float* position = reinterpret_cast<float*>(ob + kObPosition);
  float* velocity = reinterpret_cast<float*>(ob + kObVelocity);
  const float radius = *reinterpret_cast<const float*>(ob + kObRadius);
  const float before[3] = {matrix[9], matrix[10], matrix[11]};
  for (int i = 0; i < 3; ++i) {
    position[i] += p[i] - matrix[9 + i];
    velocity[i] = v[i] / f->scale;
  }
  // Not in the land: the center stays one radius or more over it.
  if (radius > 0.0f && radius < 1000.0f && OnMap(position[0], position[2])) {
    const float floor = Altitude(position[0], position[2]) + radius;
    if (position[1] < floor) position[1] = floor;
  }
  f->drag = *reinterpret_cast<const float*>(ob + kObDrag);
  *reinterpret_cast<float*>(ob + kObDrag) = 0.0f;
  f->drag_off = true;
  f->phase = kPhysics;
  f->time = t;
  f->remainder = 0.0f;
  f->count = 0;
  PushSample(f, ob);
  if (g_flight_logs < kMaxFlightLogs) {
    ++g_flight_logs;
    ModLog("hand throw   : the game's physics has %p, %.0f ms after the "
           "release. The game put it at %.1f %.1f %.1f, and the flight is at "
           "%.1f %.1f %.1f. Mass %.2f, radius %.2f, drag %.3f (0 until the "
           "first contact). The velocity in the game's time is %.1f %.1f "
           "%.1f", f->object, t * 1000.0, before[0], before[1], before[2],
           p[0], p[1], p[2], *reinterpret_cast<const float*>(ob + kObMass),
           radius, f->drag, velocity[0], velocity[1], velocity[2]);
    const float mass = *reinterpret_cast<const float*>(ob + kObMass);
    if (mass > 0.0f) {
      ++g_flight_logs;
      ModLog("hand throw   : the contact of %p: %d points, a spring of %.2f "
             "and a second spring of %.3f for each unit of mass, a friction "
             "of %.3f. The turn keeps %.3f of itself in a second of the "
             "game's time", f->object,
             *reinterpret_cast<const int32_t*>(ob + kObPoints),
             *reinterpret_cast<const float*>(ob + kObSpring) / mass,
             *reinterpret_cast<const float*>(ob + kObSpring2) / mass,
             *reinterpret_cast<const float*>(ob + kObFriction),
             powf(*reinterpret_cast<const float*>(ob + kObTurnKeep), 200.0f));
    }
  }
}

// The first contact of the flight: the game's drag is back, and the log
// compares the place with the real arc.
void FirstContact(Flight* f, uint8_t* ob, int step) {
  f->contact = true;
  // THE LANDING of this flight. A change of the key in the flight does not
  // change an object that touched the land before it.
  f->land_mode = g_land;
  f->rate_flight = f->scale;
  f->rate_land = f->scale;
  if (f->land_mode == 1 && g_land_rate < f->scale) f->rate_land = g_land_rate;
  f->keep = f->land_mode != 0 && g_contact_hook
                ? throw_math::ReboundKeep(g_bounce)
                : 1.0f;
  f->brake = f->land_mode != 0 && g_contact_hook
                 ? g_brake * *reinterpret_cast<const float*>(ob + kObFriction)
                 : 0.0f;
  if (!(f->brake >= 0.0f) || f->brake > 10.0f) f->brake = 0.0f;
  f->touch = 0.0;
  // The brake of the turn: the game's own factor of each step, made smaller.
  if (f->brake > 0.0f) {
    float* keep = reinterpret_cast<float*>(ob + kObTurnKeep);
    const float most = powf(throw_math::kLandTurnKeep, throw_math::kSubstep);
    if (*keep > most) {
      f->turn_keep = *keep;
      f->turn_changed = true;
      *keep = most;
    }
  }
  if (f->drag_off) {
    *reinterpret_cast<float*>(ob + kObDrag) = f->drag;
    f->drag_off = false;
  }
  if (g_flight_logs >= kMaxFlightLogs) return;
  ++g_flight_logs;
  const double t = f->time + step * throw_math::kSubstep / f->scale;
  float matrix[12], p[3], v[3];
  WorldMatrix(ob, matrix);
  throw_math::RealArc(f->p0, f->v0, f->gs, t, p, v);
  const float* velocity = reinterpret_cast<const float*>(ob + kObVelocity);
  const float speed = controller_math::Length(velocity);
  const void* other = *reinterpret_cast<void* const*>(ob + kObHitBy);
  ModLog("hand throw   : first contact of %p after %.3f s of the flight, at "
         "%.1f %.1f %.1f, %.1f units over the land (%s). The real arc is at "
         "%.1f %.1f %.1f at that time, %.2f units (%.1f cm of the room) "
         "away. The speed is %.1f units/s in the game's time (%.2f m/s of "
         "the room)", f->object, t, matrix[9], matrix[10], matrix[11],
         matrix[10] - Altitude(matrix[9], matrix[11]),
         other != nullptr ? "an object" : "the land or the water", p[0],
         p[1], p[2], Gap(&matrix[9], p), Gap(&matrix[9], p) / f->s * 100.0f,
         speed, speed * f->scale / f->s);
  ++g_flight_logs;
  if (f->land_mode == 0) {
    ModLog("hand throw   : the landing of %p is as before (throw_land=0): "
           "the game's step stays at %.2f times a substep, with the game's "
           "bounce", f->object, f->rate_flight);
    return;
  }
  char rate[160];
  if (f->rate_land < f->rate_flight) {
    sprintf_s(rate, "the game's step goes from %.2f to %.2f times a substep "
              "while the object touches", f->rate_flight, f->rate_land);
  } else {
    sprintf_s(rate, "the game's step stays at %.2f times a substep (%s)",
              f->rate_flight,
              f->land_mode == 2 ? "throw_land=2"
                           : "the rate of the land is not below it");
  }
  ModLog("hand throw   : the landing of %p (throw_land=%d): %s. A bounce "
         "keeps %.0f percent of the speed that the game gives it, and the "
         "land brakes the object with a friction of %.2f (%.0f percent of "
         "the friction of the object)%s",
         f->object, f->land_mode, rate, sqrtf(f->keep) * 100.0f, f->brake,
         g_brake * 100.0f,
         f->land_mode != 0 && !g_contact_hook
             ? " (the hook of the contact is not there, so the bounce is the "
               "game's and there is no brake)"
             : "");
}

// True while a point of the object is in another physics object.
bool TouchesObject(const uint8_t* ob) {
  const int32_t count = *reinterpret_cast<const int32_t*>(ob + kObPoints);
  const uint8_t* point = *reinterpret_cast<const uint8_t* const*>(
      ob + kObPointList);
  if (point == nullptr || count <= 0 || count > 1024) return false;
  for (int i = 0; i < count; ++i, point += kPointSize) {
    if (*reinterpret_cast<const float*>(point + kPointDepth) > 0.0f &&
        *reinterpret_cast<void* const*>(point + kPointOther) != nullptr) {
      return true;
    }
  }
  return false;
}

// THE LOW BOUNCE AND THE BRAKE. fn_007FDE40 adds the spring force and the
// friction of each point in contact to the force and to the torque of the
// object. For a thrown object after its first contact, the mod takes a part
// of that force off again while the force pushes the object along its
// motion. Then it adds the brake of the land. The force of a contact with
// another physics object stays as it is: the other object got the same
// force, and the two must agree.
void __fastcall h_Contact(void* ob, void* unused) {
  Flight* f = FlightOfOb(ob);
  if (f == nullptr || !f->contact ||
      (!(f->keep < 1.0f) && !(f->brake > 0.0f))) {
    o_Contact(ob, unused);
    return;
  }
  uint8_t* o = static_cast<uint8_t*>(ob);
  float* force = reinterpret_cast<float*>(o + kObForce);
  float* torque = reinterpret_cast<float*>(o + kObTorque);
  const float before[6] = {force[0],  force[1],  force[2],
                           torque[0], torque[1], torque[2]};
  o_Contact(ob, unused);
  f->cut[0] = f->cut[1] = f->cut[2] = 0.0f;
  if (TouchesObject(o)) return;
  float push[3] = {force[0] - before[0], force[1] - before[1],
                   force[2] - before[2]};
  const float* velocity = reinterpret_cast<const float*>(o + kObVelocity);
  const float factor = throw_math::ReboundFactor(push, velocity, f->keep,
                                                 throw_math::kReboundSoft);
  if (factor < 1.0f) {
    for (int i = 0; i < 3; ++i) {
      f->cut[i] = push[i] * (1.0f - factor);
      push[i] -= f->cut[i];
      force[i] -= f->cut[i];
      torque[i] -= (torque[i] - before[3 + i]) * (1.0f - factor);
    }
    ++f->cut_steps;
  }
  float brake[3];
  throw_math::LandBrake(push, velocity,
                        *reinterpret_cast<const float*>(o + kObMass),
                        f->brake, brake);
  for (int i = 0; i < 3; ++i) {
    force[i] += brake[i];
    f->cut[i] -= brake[i];
  }
}

// ONE SUBSTEP OF THE THROWN OBJECT. The game ran fn_007FD200, fn_007FD4D0,
// the test against the other objects and fn_007FDE40 for this substep, and
// it calls fn_007FE260 now. The mod runs the four parts sqrt(s) times in
// all. The answer is the code of the last step, for the game.
int FlySubstep(Flight* f, uint8_t* ob) {
  LARGE_INTEGER start;
  QueryPerformanceCounter(&start);
  uint8_t* entry = ob - kEntryPhysOb;
  // Another object touched this one in this substep (fn_007FDA60 wrote
  // PhysOb+0x78). Then the game's one step runs.
  const bool hit = *reinterpret_cast<void* const*>(ob + kObHitBy) != nullptr;
  if (!hit && f->substep_open) {
    *reinterpret_cast<void**>(ob + kObHitBy) = f->hit_before;
  }
  f->substep_open = false;
  // The rate of this substep. After the first contact it goes to the rate of
  // the land, by the time that the object touched.
  if (f->contact && f->land_mode == 1) {
    f->scale = throw_math::LandRate(f->rate_flight, f->rate_land, f->touch,
                                    throw_math::kLandEase);
  }
  int steps = throw_math::MiniSteps(f->scale, &f->remainder);
  if (hit) {
    steps = 1;
    ++f->hit_substeps;
  }
  float* cap = reinterpret_cast<float*>(kCap);
  const float cap_before = *cap;
  *cap = 1.0e9f;
  const ObFn forces = reinterpret_cast<ObFn>(kForces);
  const ObFn contact = reinterpret_cast<ObFn>(kContact);
  int code = 0, done = 0;
  for (int j = 0; j < steps; ++j) {
    if (j > 0) {
      o_Prepare(ob, nullptr);
      forces(ob, nullptr);
      contact(ob, nullptr);
    }
    if (!f->contact &&
        (*reinterpret_cast<const int32_t*>(ob + kObContacts) > 0 ||
         ob[kObContact] != 0)) {
      FirstContact(f, ob, j);
    }
    code = o_Integrate(ob, nullptr);
    ++done;
    // The force of a contact step goes to the sum of its 20 steps, as the
    // game made it, with no low bounce and no brake: a landing hurts as it
    // does in the game.
    if (ob[kObContact] != 0) {
      const float* force = reinterpret_cast<const float*>(ob + kObForce);
      for (int i = 0; i < 3; ++i) f->window[i] += force[i] + f->cut[i];
    }
    if (++f->window_steps >= throw_math::kSubstepsInTurn) {
      if (controller_math::Length(f->window) >
          controller_math::Length(f->best)) {
        for (int i = 0; i < 3; ++i) f->best[i] = f->window[i];
      }
      f->window[0] = f->window[1] = f->window[2] = 0.0f;
      f->window_steps = 0;
    }
    if (j + 1 >= steps || code != 1) break;
  }
  *cap = cap_before;
  // THE SUM OF THE TURN. After this call the game adds the force of this
  // last step to PhysicsObject+0x0C when the step had a contact
  // (0x00645FEE). The sum must then be the largest sum of 20 steps so far.
  {
    const float* largest =
        controller_math::Length(f->window) > controller_math::Length(f->best)
            ? f->window
            : f->best;
    const bool game_adds = ob[kObContact] != 0;
    float* sum = reinterpret_cast<float*>(entry + kEntryImpulse);
    const float* force = reinterpret_cast<const float*>(ob + kObForce);
    for (int i = 0; i < 3; ++i) {
      sum[i] = largest[i] - (game_adds ? force[i] : 0.0f);
    }
  }
  const double span =
      done * static_cast<double>(throw_math::kSubstep) / f->scale;
  f->time += span;
  f->last_code = code;
  ++f->substeps;
  f->steps += done;
  f->turn_steps += done;
  PushSample(f, ob);
  if (f->contact && f->samples[f->count - 1].contact) f->touch += span;
  TrackLanding(f, ob);
  LARGE_INTEGER end, frequency;
  QueryPerformanceCounter(&end);
  QueryPerformanceFrequency(&frequency);
  const double took = static_cast<double>(end.QuadPart - start.QuadPart) /
                      static_cast<double>(frequency.QuadPart);
  f->step_seconds += took;
  f->turn_seconds += took;
  return code;
}

void __fastcall h_Prepare(void* ob, void* unused) {
  Flight* f = FlightOfOb(ob);
  if (f != nullptr) {
    uint8_t* o = static_cast<uint8_t*>(ob);
    // The search of the near objects is over. The velocity of the game's
    // time is back before the first part of the first substep reads it.
    if (f->search) {
      float* v = reinterpret_cast<float*>(o + kObVelocity);
      for (int i = 0; i < 3; ++i) v[i] = f->search_velocity[i];
      f->search = false;
    }
    // fn_007FDA60 writes the other PhysOb here when one touches this one.
    // The game clears it once a turn only, so the hook clears it for the
    // substep and puts it back when no object came.
    f->hit_before = *reinterpret_cast<void* const*>(o + kObHitBy);
    *reinterpret_cast<void**>(o + kObHitBy) = nullptr;
    f->substep_open = true;
  }
  o_Prepare(ob, unused);
}

int __fastcall h_Integrate(void* ob, void* unused) {
  Flight* f = FlightOfOb(ob);
  if (f == nullptr) return o_Integrate(ob, unused);
  return FlySubstep(f, static_cast<uint8_t*>(ob));
}

// The matrix that the object is drawn with on this frame. Once a frame: the
// second eye pass gets the same matrix.
void LatchDraw(Flight* f) {
  const uint64_t frame = ControllersFrameCount();
  if (f->draw_frame == frame) return;
  f->draw_frame = frame;
  f->draw_valid = false;
  const double now = NowSeconds();
  double clock = now - f->t0;
  float arc[3], arc_v[3];
  ArcAt(*f, clock, arc, arc_v);
  const char* from = "the real arc";
  double late = 0.0;
  if (f->phase == kHeld || f->count == 0 || clock < f->samples[0].time) {
    for (int i = 0; i < 9; ++i) f->draw_matrix[i] = f->matrix0[i];
    if (f->phase == kPhysics && f->count > 0) {
      for (int i = 0; i < 9; ++i) f->draw_matrix[i] = f->samples[0].matrix[i];
    }
    for (int i = 0; i < 3; ++i) f->draw_matrix[9 + i] = arc[i];
    f->draw_valid = true;
  } else {
    // After the newest step the object falls with the gravity of the
    // picture: the square of the rate, which is s / k^2 in the flight.
    f->draw_valid = throw_math::FlightAt(
        f->samples, f->count, clock, f->contact ? f->scale * f->scale : f->gs,
        f->draw_matrix, &late);
    from = late > 0.0 ? "after the newest step" : "the game's steps";
    // The game ran no turn for some time (a pause, a slow turn). The clock
    // of the flight waits, so the flight goes on from where it stopped.
    if (late > throw_math::kFlightAhead) {
      f->t0 += late - throw_math::kFlightAhead;
    }
  }
  // After a release in the glide of a far fetch the picture comes from the
  // point of the glide to the flight.
  if (f->has_lead && f->draw_valid) {
    const float left = throw_math::CatchUpLeft(clock, throw_math::kCatchUp);
    if (left > 0.0f) {
      for (int i = 0; i < 3; ++i) f->draw_matrix[9 + i] += f->lead[i] * left;
    } else {
      f->has_lead = false;
    }
  }
  if (f->contact && f->draw_valid) {
    // The picture turns back with a move of 0.5 mm of the room or more.
    throw_math::DrawTrackAdd(&f->track, &f->draw_matrix[9], late,
                             f->s * 0.0005f);
    if (g_log >= 2 && f->trace_logs < kMaxTraceLogs) {
      ++f->trace_logs;
      ModLog("hand throw   : d %llu %.4f p %.2f %.2f %.2f l %.1f", frame,
             clock, f->draw_matrix[9], f->draw_matrix[10], f->draw_matrix[11],
             late * 1000.0);
    }
  }
  if (g_log && f->draw_valid && f->draw_logs < 90 && frame % 3 == 0) {
    ++f->draw_logs;
    const float* p = &f->draw_matrix[9];
    ModLog("hand throw   : frame %llu, %.3f s of the flight: %p is drawn at "
           "%.1f %.1f %.1f (%s), %.1f units over the land. The real arc is "
           "at %.1f %.1f %.1f, %.2f units away%s", frame, clock, f->object,
           p[0], p[1], p[2], from, p[1] - Altitude(p[0], p[2]), arc[0],
           arc[1], arc[2], Gap(p, arc),
           f->contact ? " (after the first contact)" : "");
  }
}

void WriteDraw(Flight* f) {
  LatchDraw(f);
  if (!f->draw_valid) return;
  float* matrix = MeshMatrix(f->object);
  if (matrix == nullptr) return;
  // The axes only when the check of the first turn passed. Without it the
  // object keeps the axes that the game wrote, and the mod moves it only.
  const int first = f->phase == kPhysics && !f->axes_ok ? 9 : 0;
  for (int i = first; i < 12; ++i) matrix[i] = f->draw_matrix[i];
}

// Before the game draws its physics objects, in each eye pass.
void DrawFlights() {
  if (g_active == 0) return;
  bool hand_active = false;
  const uint8_t* hand =
      static_cast<const uint8_t*>(HandDirectHand(&hand_active));
  for (int i = 0; i < kMaxFlights; ++i) {
    Flight& f = g_flights[i];
    if (!f.active) continue;
    // The object is there while the Hand holds it, or while the game's
    // physics has it. In each other case the mod does not touch it.
    const bool held = hand != nullptr &&
                      *reinterpret_cast<void* const*>(hand + kHandHeld) ==
                          f.object;
    if (held || FindEntry(f.object) != nullptr) WriteDraw(&f);
  }
}

// AN A/B IN THE HEADSET. With test_keys=1, Scroll Lock goes between the real
// flight and the square root law. The game has no action on that key. A
// flight that is in the air goes on. Read once a frame, and only while a
// window of the game is in front.
void PollLawKey() {
  static uint64_t last_frame = 0;
  static bool down = false;
  if (!g_test_keys) return;
  const uint64_t frame = ControllersFrameCount();
  if (frame == last_frame) return;
  last_frame = frame;
  const bool now = (GetAsyncKeyState(VK_SCROLL) & 0x8000) != 0 &&
                   (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0;
  if (now && !down) {
    DWORD process = 0;
    const HWND window = GetForegroundWindow();
    if (window != nullptr) GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId()) {
      g_law = g_law == 2 ? 1 : 2;
      ModLog("hand throw   : Scroll Lock: a throw now uses %s",
             g_law == 2 ? "the real flight (throw_law=2)"
                        : "the square root law with the game's physics "
                          "(throw_law=1)");
    }
  }
  down = now;
}

// THE HEIGHT RULE IN THE HEADSET. With test_keys=1, Shift with Scroll Lock
// goes between the height rule (release_fall_cm) and the set down at each
// height. A long pulse means the rule is on, a short pulse means it is off.
// Read once a frame, and only while a window of the game is in front.
void PollFallKey() {
  static uint64_t last_frame = 0;
  static bool down = false;
  if (!g_test_keys) return;
  const uint64_t frame = ControllersFrameCount();
  if (frame == last_frame) return;
  last_frame = frame;
  const bool now = (GetAsyncKeyState(VK_SCROLL) & 0x8000) != 0 &&
                   (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  if (now && !down) {
    DWORD process = 0;
    const HWND window = GetForegroundWindow();
    if (window != nullptr) GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId()) {
      g_fall_off = !g_fall_off;
      ControllersGesturePulse(0.5f, g_fall_off ? 0.06f : 0.30f);
      if (g_fall_off || g_fall_m > 0.0f) {
        ModLog("hand throw   : Shift Scroll Lock: a still hand %s",
               g_fall_off ? "sets the object down at each height"
                          : "lets the object fall when its point is higher "
                            "than release_fall_cm over the land");
      } else {
        ModLog("hand throw   : Shift Scroll Lock: release_fall_cm is 0, so a "
               "still hand sets the object down at each height");
      }
    }
  }
  down = now;
}

// THE LANDING IN THE HEADSET. With test_keys=1, the Pause key goes through
// the three landings: the time of the land with the low bounce and the brake
// (a long pulse), the low bounce and the brake in the time of the flight (a
// pulse of middle length), and the game's own landing (a short pulse). An
// object that touched the land before the press keeps its landing. Read once
// a frame, and only while a window of the game is in front.
void PollLandKey() {
  static uint64_t last_frame = 0;
  static bool down = false;
  if (!g_test_keys) return;
  const uint64_t frame = ControllersFrameCount();
  if (frame == last_frame) return;
  last_frame = frame;
  const bool now = (GetAsyncKeyState(VK_PAUSE) & 0x8000) != 0;
  if (now && !down) {
    DWORD process = 0;
    const HWND window = GetForegroundWindow();
    if (window != nullptr) GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId()) {
      g_land = g_land == 1 ? 2 : (g_land == 2 ? 0 : 1);
      ControllersGesturePulse(
          0.5f, g_land == 1 ? 0.30f : (g_land == 2 ? 0.15f : 0.06f));
      ModLog("hand throw   : Pause: after its first contact a thrown object "
             "now %s (throw_land=%d)",
             g_land == 1 ? "goes to the time of the land, with the low bounce "
                           "and the brake"
             : g_land == 2
                   ? "keeps the time of the flight, with the low bounce and "
                     "the brake"
                   : "keeps the time of the flight, with the game's bounce "
                     "and no brake",
             g_land);
    }
  }
  down = now;
}

// THE TIME FACTOR IN THE HEADSET. With test_keys=1, Numpad Plus makes the
// flight slower and Numpad Divide makes it faster, 0.25 a press, from 1.0
// (the real flight) to 10.0. With the Ctrl key down the same keys change the
// rate of the land, from 1.0 (the game's own time) to 10.0: Plus is slower
// there too. A short pulse of the hand and a log line answer
// each press. The game and the mod use neither key. A flight that is in the
// air keeps its factor. Read once a frame, and only while a window of the
// game is in front.
void PollSlowKeys() {
  static uint64_t last_frame = 0;
  static bool down[2] = {false, false};
  if (!g_test_keys) return;
  const uint64_t frame = ControllersFrameCount();
  if (frame == last_frame) return;
  last_frame = frame;
  const int keys[2] = {VK_ADD, VK_DIVIDE};
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  const bool focus = process == GetCurrentProcessId();
  const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  for (int i = 0; i < 2; ++i) {
    const bool now = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
    if (now && !down[i] && focus && ctrl) {
      float rate = g_land_rate + (i == 0 ? -0.25f : 0.25f);
      if (rate < 1.0f) rate = 1.0f;
      if (rate > 10.0f) rate = 10.0f;
      const bool moved = rate != g_land_rate;
      g_land_rate = rate;
      ControllersGesturePulse(moved ? 0.3f : 0.1f, moved ? 0.03f : 0.01f);
      ModLog("hand throw   : the rate of the land is %.2f now (%s). After "
             "its first contact the next thrown object moves %.2f times as "
             "fast as in the game's own time",
             g_land_rate, i == 0 ? "Ctrl and Numpad Plus: slower"
                                 : "Ctrl and Numpad Divide: faster",
             g_land_rate);
    } else if (now && !down[i] && focus) {
      float k = g_slow + (i == 0 ? 0.25f : -0.25f);
      if (k < 1.0f) k = 1.0f;
      if (k > 10.0f) k = 10.0f;
      const bool moved = k != g_slow;
      g_slow = k;
      ControllersGesturePulse(moved ? 0.3f : 0.1f, moved ? 0.03f : 0.01f);
      ModLog("hand throw   : the time factor is %.2f now (%s). The next "
             "throw flies the same arc, %.2f times slower than real",
             g_slow, i == 0 ? "Numpad Plus: slower" : "Numpad Divide: faster",
             g_slow);
    }
    down[i] = now;
  }
}

void __cdecl h_DrawAll() {
  PollLawKey();
  PollFallKey();
  PollLandKey();
  PollSlowKeys();
  DrawFlights();
  o_DrawAll();
}

// After CHand::PrepareForDrawing, in each eye pass. In the Holding state the
// game writes the held object's mesh position at the Hand there. From the
// release action to the game's first turn with the object, the object is
// still in the Hand, and this writes the real arc over that position.
void ThrowPrepare(void* hand) {
  if (g_active == 0 || hand == nullptr) return;
  void* held = *reinterpret_cast<void* const*>(
      static_cast<const uint8_t*>(hand) + kHandHeld);
  if (held == nullptr) return;
  Flight* f = FlightOfObject(held);
  if (f != nullptr && f->phase == kHeld) WriteDraw(f);
}

bool SameBytes(uintptr_t address, const uint8_t* want, int count) {
  const void* at = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(at, count) && memcmp(at, want, count) == 0;
}

bool Hook(uintptr_t address, void* replacement, void** original,
          const char* name) {
  void* target = reinterpret_cast<void*>(address);
  MH_STATUS status = MH_CreateHook(target, replacement, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : %s at 0x%08X %s", name, address,
         status == MH_OK ? "ok" : "FAILED");
  return status == MH_OK;
}

}  // namespace

void ThrowPhysicsSetOptions(int law, int log, bool test_keys) {
  g_law = law < 0 ? 0 : (law > 2 ? 2 : law);
  g_log = log;
  g_test_keys = test_keys;
}

void ThrowPhysicsSetSlow(int slow_x10, bool trees) {
  if (slow_x10 < 10) slow_x10 = 10;
  if (slow_x10 > 100) slow_x10 = 100;
  g_slow = static_cast<float>(slow_x10) / 10.0f;
  g_trees = trees;
}

void ThrowPhysicsSetFall(int limit_cm) {
  if (limit_cm < 0) limit_cm = 0;
  if (limit_cm > 1000) limit_cm = 1000;
  g_fall_m = static_cast<float>(limit_cm) * 0.01f;
}

void ThrowPhysicsSetLanding(int land, int rate_x10, int bounce_pct,
                            int brake_pct) {
  g_land = land < 0 ? 0 : (land > 2 ? 2 : land);
  if (rate_x10 < 10) rate_x10 = 10;
  if (rate_x10 > 100) rate_x10 = 100;
  g_land_rate = static_cast<float>(rate_x10) / 10.0f;
  if (bounce_pct < 0) bounce_pct = 0;
  if (bounce_pct > 100) bounce_pct = 100;
  g_bounce = static_cast<float>(bounce_pct) / 100.0f;
  if (brake_pct < 0) brake_pct = 0;
  if (brake_pct > 300) brake_pct = 300;
  g_brake = static_cast<float>(brake_pct) / 100.0f;
}

void ThrowPhysicsInstallEarly() {
  if (g_law != 2) return;
  if (!HandExactAddPrepareHook(&ThrowCapture)) {
    ModLog("hand throw   : the list of calls after the Hand's draw is full, "
           "so a release in the glide of a far fetch starts its flight at "
           "the point of the glide");
  }
}

int ThrowPhysicsLaw() {
  if (g_law != 2) return g_law;
  if (!g_installed || Multiplayer()) return 1;
  return 2;
}

void ThrowPhysicsInstall() {
  if (g_installed) return;
  if (g_law != 2) {
    ModLog("hand throw   : throw_law=%d, %s", g_law,
           g_law == 1 ? "the square root law with the game's physics"
                      : "the old gain with the game's physics");
    return;
  }
  // The first bytes of each function that the mod hooks or calls, as the
  // disassembly of runblack.exe gave them.
  const uint8_t prepare[] = {0x8A, 0x81, 0x74, 0x01, 0x00, 0x00, 0x83, 0xEC};
  const uint8_t forces[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x53, 0x56};
  const uint8_t contact[] = {0x83, 0xEC, 0x5C, 0x53, 0x55, 0x57, 0x8B, 0xF9};
  const uint8_t integrate[] = {0x83, 0xEC, 0x5C, 0x53, 0x56, 0x8B, 0xF1};
  const uint8_t matrix[] = {0x8B, 0x54, 0x24, 0x04, 0x83, 0xEC, 0x0C, 0x56};
  const uint8_t draw[] = {0x83, 0xEC, 0x0C, 0x8B, 0x0D, 0x18, 0x78, 0xD4};
  const float* cap = reinterpret_cast<const float*>(kCap);
  const bool same =
      SameBytes(kPrepare, prepare, sizeof(prepare)) &&
      SameBytes(kForces, forces, sizeof(forces)) &&
      SameBytes(kContact, contact, sizeof(contact)) &&
      SameBytes(kIntegrate, integrate, sizeof(integrate)) &&
      SameBytes(kWorldMatrix, matrix, sizeof(matrix)) &&
      SameBytes(kDrawAll, draw, sizeof(draw)) &&
      !IsBadWritePtr(const_cast<float*>(cap), sizeof(float)) &&
      *cap > 100.0f && *cap < 200.0f;
  if (!same) {
    ModLog("hand throw   : this is not the game that the real flight knows "
           "(the physics code is different). A throw uses the square root "
           "law");
    return;
  }
  const bool ok =
      Hook(kPrepare, reinterpret_cast<void*>(&h_Prepare),
           reinterpret_cast<void**>(&o_Prepare),
           "the points of a physics object (the real flight)") &&
      Hook(kIntegrate, reinterpret_cast<void*>(&h_Integrate),
           reinterpret_cast<void**>(&o_Integrate),
           "the step of a physics object (the real flight)") &&
      Hook(kDrawAll, reinterpret_cast<void*>(&h_DrawAll),
           reinterpret_cast<void**>(&o_DrawAll),
           "PhysicsObject::DrawAll (the real flight)");
  // The low bounce needs this hook only. Without it a thrown object has the
  // game's bounce.
  g_contact_hook =
      ok && Hook(kContact, reinterpret_cast<void*>(&h_Contact),
                 reinterpret_cast<void**>(&o_Contact),
                 "the contact of a physics object (the landing)");
  const bool prepare_hook = HandExactAddPrepareHook(&ThrowPrepare);
  g_installed = ok;
  ModLog("hand throw   : throw_law=2, the real flight is %s. A thrown object "
         "flies in the room as a real object: the same arc and the same "
         "time. The draw of the first frames of a flight is %s%s",
         ok ? "on" : "OFF (a hook failed), and a throw uses the square root "
                     "law",
         prepare_hook ? "in the Hand's draw" : "only in the physics draw",
         g_log ? ". throw_log logs each flight in full" : "");
  ModLog("hand throw   : the time factor is %.2f: a throw flies the same "
         "arc as the real throw, %.2f times slower. Trees %s", g_slow,
         g_slow, g_trees ? "fly the same way (throw_trees=1)"
                         : "keep the game's physics (throw_trees=0)");
  if (g_fall_m > 0.0f) {
    ModLog("hand throw   : release_fall_cm=%.0f: a hand that is still at the "
           "opening of the grip sets the object down when its point is %.0f "
           "cm of the room over the land or less. Higher up the object "
           "falls. A Miracle always sets down", g_fall_m * 100.0f,
           g_fall_m * 100.0f);
  } else {
    ModLog("hand throw   : release_fall_cm=0: a hand that is still at the "
           "opening of the grip sets the object down at each height");
  }
  if (g_land == 0) {
    ModLog("hand throw   : throw_land=0: after its first contact a thrown "
           "object keeps the time of the flight, with the game's bounce");
  } else {
    ModLog("hand throw   : throw_land=%d: after its first contact a thrown "
           "object %s. A bounce keeps %.0f percent of the speed that the "
           "game gives it (throw_bounce_pct), and the land brakes the object "
           "with %.0f percent of the friction that the game has for it "
           "(throw_land_brake_pct)%s", g_land,
           g_land == 1 ? "goes to the rate of the land while it touches"
                       : "keeps the time of the flight",
           g_bounce * 100.0f, g_brake * 100.0f,
           g_contact_hook ? "" : ". The hook of the contact FAILED, so the "
                                 "bounce is the game's and there is no "
                                 "brake");
    if (g_land == 1) {
      ModLog("hand throw   : the rate of the land is %.2f steps a substep "
             "(throw_land_rate_x10), where 1.00 is the game's own time",
             g_land_rate);
    }
  }
  if (ok && g_test_keys) {
    ModLog("hand throw   : test_keys=1: Scroll Lock goes between the real "
           "flight and the square root law. Numpad Plus and Numpad Divide "
           "change the time factor by 0.25, and with Ctrl the rate of the "
           "land. Pause goes through the three landings");
  }
}

bool ThrowPhysicsPlan(const ThrowPlanIn& in, ThrowPlanOut* out) {
  if (ThrowPhysicsLaw() != 2 || !(in.units_per_meter > 0.0f) ||
      !(in.life > 0.0f)) {
    return false;
  }
  // The top speed of the swing, from the peak window before the opening to
  // the release. With no pair of samples there, the mean of the opening.
  float peak[3];
  double at = in.now;
  if (!throw_math::PeakVelocity(in.grip, in.count,
                                in.opened - throw_math::kPeakWindow, in.now,
                                throw_math::kPeakHalfSpan, peak, &at)) {
    for (int i = 0; i < 3; ++i) peak[i] = in.v_end_room[i];
  }
  Plan plan = {};
  plan.valid = true;
  plan.frame = in.frame;
  plan.held = in.held;
  plan.s = in.units_per_meter;
  // The height rule. A Miracle keeps the set down: its release is its own.
  const float limit = g_fall_off ? 0.0f : g_fall_m;
  const bool high = throw_math::FallsFromHeight(in.height_m, limit) &&
                    !VirtualTrue(in.held, kIsSpellSeed);
  throw_math::RealThrowPlan(in.v_end_room, peak, in.room, in.gain, in.life,
                            in.units_per_meter, in.cap, in.drop_speed,
                            &plan.t, g_slow, high);
  g_plan = plan;
  out->drop = plan.t.drop;
  out->fall = plan.t.fall;
  out->real_mps = plan.t.real_mps;
  for (int i = 0; i < 3; ++i) out->sent[i] = plan.t.sent[i];
  if (g_plan_logs >= kMaxPlanLogs) return true;
  ++g_plan_logs;
  const float end_mps = controller_math::Length(in.v_end_room) / in.life;
  const float end_flat = sqrtf(in.v_end_room[0] * in.v_end_room[0] +
                               in.v_end_room[2] * in.v_end_room[2]) / in.life;
  char opened[96] = "";
  if (in.latched) {
    sprintf_s(opened, " The grip opened %.0f ms before this release.",
              (in.now - in.opened) * 1000.0);
  }
  if (plan.t.fall) {
    ModLog("hand throw   : release at frame %llu with the real flight.%s The "
           "hand was still, but its point was %.1f cm of the room over the "
           "land, and the limit is %.0f cm (release_fall_cm). The object "
           "falls with the velocity of the hand at the opening: %.2f m/s "
           "across the land and %.2f m/s up. Sent %.1f %.1f %.1f to start the "
           "physics. Holding %p", in.frame, opened, in.height_m * 100.0f,
           limit * 100.0f, end_flat, plan.t.world[1] / in.units_per_meter,
           plan.t.sent[0], plan.t.sent[1], plan.t.sent[2], in.held);
    return true;
  }
  if (plan.t.drop) {
    if (in.height_m > 0.0f && limit > 0.0f && in.height_m > limit) {
      ModLog("hand throw   : the release keeps the set down at %.1f cm of "
             "the room over the land, because %p is a Miracle",
             in.height_m * 100.0f, in.held);
    }
    ModLog("hand throw   : release at frame %llu with the real flight.%s The "
           "hand was still: %.2f m/s across the land in the 80 ms before the "
           "opening, and a top speed of %.2f m/s. The game sets the object "
           "down at %.1f cm of the room over the land, sent zero. Holding %p",
           in.frame, opened, end_flat, controller_math::Length(peak) / in.life,
           in.height_m * 100.0f, in.held);
    return true;
  }
  ModLog("hand throw   : release at frame %llu with the real flight.%s The "
         "top speed of the hand was %.2f m/s, %.0f ms before the release. "
         "The mean of the 80 ms before the opening was %.2f m/s. At %.1f "
         "units per meter the real flight is %.0f units/s under a gravity "
         "of %.0f units/s^2. The time factor is %.2f (%.2f asked), so the "
         "object flies at %.0f units/s under %.0f units/s^2, on the same "
         "arc. In the game's time that is %.1f units/s, and the game's "
         "step runs %.2f times as often. Sent %.1f %.1f %.1f to "
         "start the physics. Holding %p", in.frame, opened, plan.t.real_mps,
         (in.now - at) * 1000.0, end_mps, in.units_per_meter,
         controller_math::Length(plan.t.world),
         in.units_per_meter * throw_math::kGravity, plan.t.slow, g_slow,
         controller_math::Length(plan.t.flight),
         plan.t.gravity * throw_math::kGravity,
         controller_math::Length(plan.t.game), plan.t.rate, plan.t.sent[0],
         plan.t.sent[1], plan.t.sent[2], in.held);
  return true;
}

void ThrowPhysicsReleased(void* held) {
  if (!g_plan.valid || g_plan.held != held || held == nullptr) return;
  const Plan plan = g_plan;
  g_plan.valid = false;
  if (plan.t.drop || ThrowPhysicsLaw() != 2) return;
  // The release action marks the object when it lets go. With no mark the
  // game kept the object, for example outside the player's influence.
  if (*reinterpret_cast<const int32_t*>(kLocalThrow) == 0 ||
      *reinterpret_cast<void* const*>(kLocalObject) != held) {
    if (g_flight_logs < kMaxFlightLogs) {
      ++g_flight_logs;
      ModLog("hand throw   : the game did not let go of %p, so no flight "
             "starts", held);
    }
    return;
  }
  // A Miracle has its own rules in the air, and it keeps the game's time. A
  // tree is a physics object like a rock (see the doc), so it flies the same
  // way, unless throw_trees=0. The velocity that the release sent is the
  // velocity of the square root law.
  const bool tree = VirtualTrue(held, kIsAnyKindOfTree);
  if ((tree && !g_trees) || VirtualTrue(held, kIsSpellSeed)) {
    if (g_flight_logs < kMaxFlightLogs) {
      ++g_flight_logs;
      ModLog("hand throw   : %p is a %s, so it keeps the game's physics",
             held, tree ? "tree (throw_trees=0)" : "Miracle");
    }
    return;
  }
  const uint64_t frame = ControllersFrameCount();
  Flight* old = FlightOfObject(held);
  if (old != nullptr) {
    // A second run of the release action in one frame.
    if (old->phase == kHeld && old->frame0 == frame) return;
    EndFlight(old, "the object is thrown again");
  }
  const float* matrix = MeshMatrix(held);
  if (matrix == nullptr) return;
  Flight* f = NewFlight();
  memset(f, 0, sizeof(*f));
  f->active = true;
  ++g_active;
  f->phase = kHeld;
  f->object = held;
  f->frame0 = frame;
  f->t0 = NowSeconds();
  f->s = plan.s;
  f->gs = plan.t.gravity;
  f->scale = plan.t.rate;
  f->rate_flight = plan.t.rate;
  f->rate_land = plan.t.rate;
  f->keep = 1.0f;
  f->slow = plan.t.slow;
  f->real_mps = plan.t.real_mps;
  for (int i = 0; i < 12; ++i) f->matrix0[i] = matrix[i];
  for (int i = 0; i < 3; ++i) {
    f->p0[i] = matrix[9 + i];
    f->v0[i] = plan.t.flight[i];
  }
  // A RELEASE IN THE GLIDE OF A FAR FETCH. The glide draws the object on its
  // way to the Hand, and the mesh position is the point of the glide. The
  // throw is the throw of the real hand, so the flight starts at the place
  // of the object in the Hand. The picture comes from the point of the glide
  // to the flight in a short time (throw_math.h, CatchUpLeft).
  if (g_hand_place.object == held && frame - g_hand_place.frame <= 2 &&
      Gap(g_hand_place.p, f->p0) > f->s * 0.01f) {
    for (int i = 0; i < 3; ++i) {
      f->lead[i] = f->p0[i] - g_hand_place.p[i];
      f->p0[i] = g_hand_place.p[i];
    }
    f->has_lead = true;
    if (g_flight_logs < kMaxFlightLogs) {
      ++g_flight_logs;
      ModLog("hand throw   : the release came while %p was drawn %.1f cm of "
             "the room from its place in the Hand (the glide of a far "
             "fetch). The flight starts at the Hand, at %.1f %.1f %.1f, and "
             "the picture comes to the flight in %.0f ms", held,
             controller_math::Length(f->lead) / f->s * 100.0f, f->p0[0],
             f->p0[1], f->p0[2], throw_math::kCatchUp * 1000.0);
    }
  }
  FindLanding(f);
  if (g_flight_logs >= kMaxFlightLogs) return;
  ++g_flight_logs;
  char where[200];
  if (f->lands) {
    sprintf_s(where, "The real arc meets the land after %.2f s at %.1f %.1f "
              "%.1f, %.1f units (%.2f m of the room) away",
              f->land_t, f->land[0], f->land[1], f->land[2],
              Flat(f->land, f->p0), Flat(f->land, f->p0) / f->s);
  } else if (f->leaves) {
    sprintf_s(where, "The real arc leaves the map after %.2f s", f->land_t);
  } else {
    sprintf_s(where, "The real arc does not meet the land in 6 s");
  }
  ModLog("hand throw   : the flight of %p starts at frame %llu from %.1f "
         "%.1f %.1f, %.1f units over the land, at %.2f m/s of the room. %s",
         held, frame, f->p0[0], f->p0[1], f->p0[2],
         f->p0[1] - Altitude(f->p0[0], f->p0[2]), f->real_mps, where);
}

void ThrowPhysicsTurnBegin() {
  if (g_active == 0) return;
  const double now = NowSeconds();
  bool hand_active = false;
  const uint8_t* hand =
      static_cast<const uint8_t*>(HandDirectHand(&hand_active));
  for (int k = 0; k < kMaxFlights; ++k) {
    Flight& f = g_flights[k];
    if (!f.active) continue;
    uint8_t* entry = FindEntry(f.object);
    if (f.phase == kHeld) {
      if (entry == nullptr) {
        // The object left the Hand and the physics does not have it: the
        // game did a different thing with it, for example gave it to a
        // building.
        const bool held = hand != nullptr &&
                          *reinterpret_cast<void* const*>(hand + kHandHeld) ==
                              f.object;
        if (!held) {
          EndFlight(&f, "the object left the Hand and the game did not throw "
                        "it");
        } else if (now - f.t0 > kHeldLimit) {
          EndFlight(&f, "the game did not throw the object");
        }
        continue;
      }
      StartPhysics(&f, entry, now);
    } else if (entry == nullptr) {
      EndFlight(&f, "the game took the object out of its physics");
      continue;
    }
    uint8_t* ob = entry + kEntryPhysOb;
    if (ob[kObResting] != 0) {
      EndFlight(&f, "the object is at rest");
      continue;
    }
    if (now - f.t0 > kFlightLimit) {
      EndFlight(&f, "the time limit of a flight");
      continue;
    }
    ++f.turns;
    f.turn_steps = 0;
    f.turn_seconds = 0.0;
    for (int i = 0; i < 3; ++i) f.window[i] = f.best[i] = 0.0f;
    f.window_steps = 0;
    // THE SEARCH OF THE NEAR OBJECTS. At the start of a turn the game puts
    // the objects in the map cells near each physics object into its
    // physics, out to the speed times 0.1 s and the radius (0x0064528A).
    // The thrown object goes sqrt(s) times as far in a turn. So for the
    // search only, its velocity is that of the real flight, up to
    // kSearchSpeed. h_Prepare puts the velocity back.
    float* v = reinterpret_cast<float*>(ob + kObVelocity);
    const float speed = controller_math::Length(v);
    float factor = f.scale;
    if (speed * factor > kSearchSpeed) factor = kSearchSpeed / speed;
    if (speed > 1e-3f && factor > 1.0f) {
      for (int i = 0; i < 3; ++i) {
        f.search_velocity[i] = v[i];
        v[i] *= factor;
      }
      f.search = true;
    }
  }
}

void ThrowPhysicsTurnEnd() {
  if (g_active == 0) return;
  const double now = NowSeconds();
  for (int k = 0; k < kMaxFlights; ++k) {
    Flight& f = g_flights[k];
    if (!f.active || f.phase != kPhysics) continue;
    uint8_t* entry = FindEntry(f.object);
    if (entry == nullptr) {
      EndFlight(&f, f.last_code == 4 ? "the object fell under the sea"
                                     : "the game took the object out of its "
                                       "physics");
      continue;
    }
    uint8_t* ob = entry + kEntryPhysOb;
    // The search velocity is still there when the game ran no substep.
    if (f.search) {
      float* v = reinterpret_cast<float*>(ob + kObVelocity);
      for (int i = 0; i < 3; ++i) v[i] = f.search_velocity[i];
      f.search = false;
    }
    if (g_log && g_flight_logs < kMaxFlightLogs) {
      ++g_flight_logs;
      const float* p = reinterpret_cast<const float*>(ob + kObPosition);
      const float* v = reinterpret_cast<const float*>(ob + kObVelocity);
      ModLog("hand throw   : turn %d of the flight of %p: %d steps in %.2f "
             "ms. The physics is at %.3f s of the flight, and the real time "
             "is %.3f s. The center is at %.1f %.1f %.1f, the speed is %.1f "
             "units/s in the game's time, %d points touch, the last step "
             "gave %d", f.turns, f.object, f.turn_steps,
             f.turn_seconds * 1000.0, f.time, now - f.t0, p[0], p[1], p[2],
             controller_math::Length(v),
             *reinterpret_cast<const int32_t*>(ob + kObContacts),
             f.last_code);
    }
    // THE CHECK OF THE AXES. The game wrote the mesh matrix from the last
    // substep (fn_007FD140, the angles, then SetXYZAngles). It must be the
    // matrix that the mod keeps for that substep. Only then the mod draws
    // the axes of each substep. The position is the game's own in each case.
    if (!f.matrix_checked && f.count > 0 && f.turn_steps > 0 &&
        f.last_code == 1 && ob[kObResting] == 0) {
      f.matrix_checked = true;
      const float* mesh = MeshMatrix(f.object);
      if (mesh != nullptr) {
        const float* kept = f.samples[f.count - 1].matrix;
        float axes = 0.0f;
        for (int i = 0; i < 9; ++i) {
          const float d = fabsf(mesh[i] - kept[i]);
          if (d > axes) axes = d;
        }
        const float size = sqrtf(kept[0] * kept[0] + kept[1] * kept[1] +
                                 kept[2] * kept[2]);
        f.axes_ok = size > 1e-6f && axes <= 0.03f * size;
        if (g_flight_logs < kMaxFlightLogs && (g_log || !f.axes_ok)) {
          ++g_flight_logs;
          ModLog("hand throw   : the mesh matrix of the game and the matrix "
                 "that the mod keeps differ by %.4f at most in the axes "
                 "(their size is %.3f) and by %.3f units in the position. "
                 "The mod draws %s", axes, size, Gap(&mesh[9], &kept[9]),
                 f.axes_ok ? "the axes of each substep"
                           : "the position only, with the game's axes");
        }
      }
    }
    // The object came to rest (2), or it fell under the sea and the game
    // takes it out (4). Then the mod does not touch it more.
    if (ob[kObResting] != 0 || (f.turn_steps > 0 && f.last_code == 2)) {
      EndFlight(&f, "the object is at rest");
    } else if (f.turn_steps > 0 && f.last_code == 4) {
      EndFlight(&f, "the object fell under the sea");
    }
  }
}
