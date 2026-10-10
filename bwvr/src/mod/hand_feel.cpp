// Hands that react to the world. See docs/hand-model.md, "Hands that react to the
// world", and hand_feel_math.h for the rules and the math.
//
// WHY. Hands that react to the world feel real: the fingers flex over a
// shelf, and start to grasp an object or a door handle when the hand gets
// close. The pose of a hand comes from the trigger, the grip and the state of
// the game's Hand. This file adds the world near the hand.
//
// WHAT. overlay.cpp makes the curl of each finger from the pose. Then this
// file changes the curls, in four parts, each with its own key:
//
//   the land      A finger that is in the land takes the nearest curl at
//                 which it is not (hand_feel_land).
//   the approach  While the grip has a target at the Hand's point, the
//                 pointing hand takes the form of the target: a share of
//                 the curls that hold it, and the game's own pose
//                 `can_pickup` at least (hand_feel_approach).
//   the hold      The fingers close on the body of the object in the Hand,
//                 and a finger that cannot touch it takes the game's own hold
//                 pose for that object (hand_feel_hold).
//   the bodies    A finger that is in the body of an object near the hand
//                 takes the nearest curl at which it is not
//                 (hand_feel_bodies).
//
// The place and the turn of a hand do not change. The index finger of the
// pointing hand keeps its curl in a body and toward a target: its tip is the
// Hand's point, where the glow is, and that point must go into a body to
// choose it. On the land it bends as each other finger does. The land test of
// hand_direct.cpp keeps the Hand's point over the land, so the glow and the
// tip of the bent finger are both on the land then.
//
// WHEN. Only while the God view or the table owns the view. In a menu, in a
// Temple room and in a cut scene the hands keep the curls of the pose.
//
// THE DATA. The land is LH3DIsland::GetAltitude on a grid of 8 by 8 points
// under a hand, read only while the hand is near the land. The bodies are the
// objects that the game drew in the first eye pass of the frame before
// (hand_direct.cpp, the candidates of the pick), with the body that the
// choice of the grip uses. The object in the Hand is at the place where
// hand_exact.cpp put it in the frame before.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hand_feel.h"
#include "hand_feel_math.h"
#include "hand_model.h"
#include "haptics.h"
#include "pinch_math.h"

extern void ModLog(const char* format, ...);

// hand_direct.cpp: the CHand, the target of the grip at the Hand's point, the
// kind of a press there, the Hand's point, the candidates of the last first
// eye pass, the body of a candidate and the class of an object.
void* HandDirectHand(bool* active);
bool HandDirectNearTarget(float center[3], float* radius, float* half_height,
                          bool* acts);
int HandDirectPressKind();
bool HandDirectRealPoint(float out[3]);
int HandDirectConeCount();
bool HandDirectConeAt(int index, void** object, float position[3]);
bool HandDirectFeelBody(int index, void** object, float base[3], float* radius,
                        float* height, int* kind, float axis[9],
                        float half[3]);
const char* HandDirectClass(void* object);
// hand_exact.cpp: the object in the Hand as the mod placed it last.
bool HandExactHeld(void** object, float origin[3], float up[3]);
// overlay.cpp: the pixel of the index tip of a hand in the left eye.
bool OverlayTipPixel(int hand, float out[2]);
// overlay.cpp: a request for the curl and the spread of one finger.
void OverlaySetFinger(int hand, int finger, float curl, float spread,
                      float weight);
// tabletop.cpp: the God view or the table owns the view.
bool TabletopOwnsView();
// controllers.cpp: a pulse on the pointing hand, and the frame count that the
// script of the fake controller uses.
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();

namespace {

namespace hf = hand_feel;

const int kHands = 2;

struct Settings {
  bool on = true;
  bool land = true;
  bool approach = true;
  bool hold = true;
  bool bodies = false;
  bool pulse = true;
  bool idle = false;
  bool pinch = true;
  bool test_keys = false;
  int pick_cm = near_math::kPickCm;
  uint64_t toggle_frames[16];
  int toggle_count = 0;
  // Test key hand_feel_log: one line for each hand each so many frames.
  int log_every = 0;
};
Settings g_set;
bool g_loaded = false;
// The live switch: the test key and the test frames turn it.
bool g_live = true;
bool g_key_down = false;

// THE GAME [V]. LH3DIsland::GetAltitude, fastcall on the map coordinates.
// The held object of the Hand, and the slots of Object for the hold (see
// hand_feel_math.h, THE HOLD OF THE GAME).
const uintptr_t kGetAltitude = 0x00803090;
const float kMapScale = 6553.6f;
const float kMapSize = 5120.0f;
const int kHandHeld = 0x4904;
const int kGet2DRadius = 0x64;
const int kGetHeight = 0x42C;
const int kGetHoldType = 0x58C;
const int kGetHoldRadius = 0x590;
typedef float(__fastcall* GetAltitudeFn)(const int32_t* coords);
typedef int(__fastcall* VirtualIntFn)(void* self, void* unused);
typedef float(__fastcall* VirtualFloatFn)(void* self, void* unused);

// THE REACH OF A HAND. Each point of each finger at each curl is within
// kReach hand lengths of this point of the hand frame. The land and the
// bodies inside that ball are the world of the hand.
const float kCenter[3] = {0.0f, -0.12f, 0.05f};
const float kReach = 0.60f;
// The middle of the palm, in the hand frame.
const float kPalm[3] = {overlay_math::kPalmX, overlay_math::kKnuckleY + 0.20f,
                        0.0f};
// The land is near a hand when the lowest point of the ball is less than
// kLandSlack hand lengths over the highest of five land heights under it.
const float kLandSlack = 0.25f;
// A body counts from this gap to the ball on, in hand lengths.
const float kBodySlack = 0.30f;
// No object of the game has a radius of more than this, in world units. It is
// the first cut of the candidates, before the game gives the body.
const float kBodyFar = 60.0f;
// Past this length of the hand, in world units, a finger is wider than a
// house (the table). The bodies then do not count.
const float kBodiesMaxLength = 60.0f;
// The point touches a body from this gap on, in cm of the room: the weight of
// the approach is 1 there.
const float kTouchCm = 1.0f;

// The finger tables of the hand frame, for the hand model in use.
hf::FingerTable g_table[hf::kFingers];
bool g_have_table = false;
bool g_table_mesh = false;

struct HandState {
  // The place of the hand when its curls were made last.
  bool have_place = false;
  uint64_t frame = 0;
  float position[3];
  float rotation[9];
  float length = 0.0f;
  bool left = false;
  // What the world adds to the curl of each finger, after the filter, and
  // the side of the free curl of the last frame.
  float add[hf::kFingers] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  int side[hf::kFingers] = {0, 0, 0, 0, 0};
  // The bodies near the hand, from the last first eye pass.
  int bodies = 0;
  hf::Body body[hf::kMaxBodies];
  float body_gap[hf::kMaxBodies];
  // For the pulse and the log.
  float quiet = 10.0f;
  int in = hf::kInNothing;
  uint64_t in_frame = 0;
  bool approach = false;
  float idle_phase = 0.0f;
};
HandState g_hand[kHands];

// The curls at which the hand holds a ball of a radius, for the shape of the
// approach. Made again when the radius changes.
struct ShapeCache {
  bool have = false;
  bool mesh = false;
  float radius = 0.0f;
  float hold[hf::kFingers];
};
ShapeCache g_shape;

// The hold pose of the last frame, and what it was made from.
struct HoldCache {
  bool have = false;
  void* object = nullptr;
  bool mesh = false;
  bool capsule = false;
  hf::HeldBody body;
  overlay_math::PoseRange range;
};
HoldCache g_hold;

// THE PINCH of the object in the Hand (HandFeelPinch). The tips of the thumb
// and of the index finger for the hand model in use, the pose that brings
// them to the object, and the weight of the pinch against the fist.
struct PinchState {
  bool have_table = false;
  bool mesh = false;
  pinch_math::Table table;
  pinch_math::Pose pose;
  float solved_gap = -1.0f;
  void* object = nullptr;
  uint64_t frame = 0;         // of hand_exact.cpp, for the latch
  uint64_t feel_frame = 0;    // of this file, for the age
  int64_t ticks = 0;
  bool want = false;
  float weight = 0.0f;        // 0 to 1 in time, before the ease
  bool live = true;           // the test key
  bool key_down = false;
};
PinchState g_pinch;
int g_pinch_logs = 0;
const int kMaxPinchLogs = 40;

// For the log and the report.
uint64_t g_frame = 0;
int g_meet_logs = 0;
const int kMaxMeetLogs = 60;
const uint64_t kMeetLogGap = 45;
int g_approach_logs = 0;
const int kMaxApproachLogs = 30;
int g_hold_logs = 0;
const int kMaxHoldLogs = 40;
int g_fall_logs = 0;
uint64_t g_frames = 0;
uint64_t g_land_near = 0;
uint64_t g_body_near = 0;
uint64_t g_in_land = 0;
uint64_t g_in_body = 0;
uint64_t g_stuck = 0;
uint64_t g_approach_frames = 0;
uint64_t g_hold_made = 0;
uint64_t g_pulses = 0;
uint64_t g_land_calls = 0;
uint64_t g_candidates_seen = 0;
uint64_t g_view_off = 0;
LARGE_INTEGER g_frequency = {};
int64_t g_ticks = 0;
int64_t g_frame_ticks = 0;
int64_t g_worst_ticks = 0;
uint64_t g_ticks_frame = 0;

int64_t Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

double Ms(int64_t ticks) {
  if (g_frequency.QuadPart == 0) return 0.0;
  return static_cast<double>(ticks) * 1000.0 /
         static_cast<double>(g_frequency.QuadPart);
}

// The time of one call, in the sum of its frame.
void CountTicks(int64_t ticks) {
  if (g_ticks_frame != g_frame) {
    if (g_frame_ticks > g_worst_ticks) g_worst_ticks = g_frame_ticks;
    g_frame_ticks = 0;
    g_ticks_frame = g_frame;
  }
  g_frame_ticks += ticks;
  g_ticks += ticks;
}

void LoadSettings() {
  if (g_loaded) return;
  g_loaded = true;
  QueryPerformanceFrequency(&g_frequency);
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash = length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || (slash - path) + 10 >= MAX_PATH) {
    ModLog("hand feel    : the mod did not find bwvr.ini, so each key has its "
           "default");
  } else {
    slash[1] = 0;
    strcat_s(path, "bwvr.ini");
    const auto key = [&](const char* name, int fallback) {
      return static_cast<int>(
          GetPrivateProfileIntA("bwvr", name, fallback, path));
    };
    g_set.on = key("hand_feel", 1) != 0;
    g_set.land = key("hand_feel_land", 1) != 0;
    g_set.approach = key("hand_feel_approach", 1) != 0;
    g_set.hold = key("hand_feel_hold", 1) != 0;
    g_set.bodies = key("hand_feel_bodies", 0) != 0;
    g_set.pulse = key("hand_feel_pulse", 1) != 0;
    g_set.idle = key("hand_feel_idle", 0) != 0;
    g_set.pinch = key("hand_pinch", 1) != 0;
    g_set.test_keys = key("test_keys", 0) != 0;
    g_set.pick_cm = key("hand_pick_cm", near_math::kPickCm);
    if (g_set.pick_cm < 1) g_set.pick_cm = 1;
    if (g_set.pick_cm > 30) g_set.pick_cm = 30;
    g_set.log_every = key("hand_feel_log", 0);
    char text[256];
    GetPrivateProfileStringA("bwvr", "hand_feel_test_toggle", "", text,
                             sizeof(text), path);
    const char* p = text;
    while (*p != 0 && g_set.toggle_count < 16) {
      while (*p == ' ' || *p == ',') ++p;
      if (*p == 0) break;
      char* end = nullptr;
      const unsigned long value = strtoul(p, &end, 10);
      if (end == p) break;
      if (value > 0) g_set.toggle_frames[g_set.toggle_count++] = value;
      p = end;
    }
  }
  g_live = g_set.on;
  if (!g_set.on) {
    ModLog("hand feel    : hand_feel=0, so the fingers have the curls of the "
           "pose only, as in the builds before");
    return;
  }
  ModLog("hand feel    : on. The fingers bend on the land: %s. The hand takes "
         "the form of a target of the grip: %s. The fingers close on the "
         "object in the Hand with the game's hold pose: %s. The fingers bend "
         "against a body: %s. A pulse when a hand first meets a surface: %s. "
         "The idle wave: %s. A small object in the Hand is held in a pinch: "
         "%s. The filter is %.0f ms%s",
         g_set.land ? "yes" : "no (hand_feel_land=0)",
         g_set.approach ? "yes" : "no (hand_feel_approach=0)",
         g_set.hold ? "yes" : "no (hand_feel_hold=0)",
         g_set.bodies ? "yes (hand_feel_bodies=1)" : "no",
         g_set.pulse ? "yes" : "no (hand_feel_pulse=0)",
         g_set.idle ? "yes (hand_feel_idle=1)" : "no",
         g_set.pinch ? "yes" : "no (hand_pinch=0)",
         hf::kFilterSeconds * 1000.0f,
         g_set.test_keys ? ". Ctrl, Shift and the minus key of the number pad "
                           "turn it off and on, and Ctrl, Shift and End "
                           "turn the pinch off and on"
                         : "");
}

void Toggle(const char* how) {
  g_live = !g_live;
  ControllersGesturePulse(0.5f, g_live ? 0.30f : 0.08f);
  ModLog("hand feel    : %s: the fingers %s, frame %llu", how,
         g_live ? "react to the world" : "have the curls of the pose only",
         g_frame);
}

// THE LAND HEIGHT, with the sea and the places off the map at 0, as the land
// test of the Hand's point has it.
float Ground(float x, float z) {
  ++g_land_calls;
  if (!(x >= 0.0f && x < kMapSize && z >= 0.0f && z < kMapSize)) return 0.0f;
  const int32_t coords[3] = {static_cast<int32_t>(x * kMapScale),
                             static_cast<int32_t>(z * kMapScale), 0};
  const float y = reinterpret_cast<GetAltitudeFn>(kGetAltitude)(coords);
  if (!(y > 0.0f && y < 5000.0f)) return 0.0f;
  return y;
}

void EnsureTables(bool mesh) {
  if (g_have_table && g_table_mesh == mesh) return;
  for (int f = 0; f < hf::kFingers; ++f) {
    hf::FillTable(
        f,
        [&](int finger, float curl, float out[hf::kPoints][3]) {
          if (!mesh || !HandModelFingerJoints(finger, curl, 0.0f, out)) {
            overlay_math::FingerJoints(finger, curl, out);
          }
        },
        &g_table[f]);
  }
  g_have_table = true;
  g_table_mesh = mesh;
  g_hold.have = false;
}

// The object in the Hand, or null.
void* HeldObject() {
  bool active = false;
  const uint8_t* hand = static_cast<const uint8_t*>(HandDirectHand(&active));
  if (hand == nullptr) return nullptr;
  void* held = *reinterpret_cast<void* const*>(hand + kHandHeld);
  if (held == nullptr || IsBadReadPtr(held, sizeof(void*))) return nullptr;
  void** table = *static_cast<void***>(held);
  if (IsBadReadPtr(table, kGetHoldRadius + sizeof(void*))) return nullptr;
  return held;
}

float VirtualFloat(void* object, int slot) {
  void** table = *static_cast<void***>(object);
  const float value =
      reinterpret_cast<VirtualFloatFn>(table[slot / 4])(object, nullptr);
  return value > 0.0f && value < 2000.0f ? value : 0.0f;
}

// The hold types that a pinch can hold: an object with no rule of its own
// (1), and a mobile object, a crop or a rock (6).
bool PinchType(int type) {
  return type == hf::kHoldAbove || type == hf::kHoldMobile;
}

// The pinch of this frame, or false when there is none: the weight with its
// ease, and the curls of the thumb and the index finger at which the tips meet.
bool PinchNow(float* weight, float* thumb_curl, float* thumb_spread,
              float* index_curl) {
  if (g_pinch.object == nullptr || g_frame > g_pinch.feel_frame + 2 ||
      !(g_pinch.weight > 0.0f) || !g_pinch.pose.ok) {
    return false;
  }
  *weight = pinch_math::Ease(g_pinch.weight);
  *thumb_curl = g_pinch.pose.thumb_curl;
  *thumb_spread = g_pinch.pose.thumb_spread;
  *index_curl = g_pinch.pose.index_curl;
  return true;
}

const char* HoldName(int type) {
  if (hf::HoldFromSide(type)) return "hold_side";
  if (type == hf::kHoldSeed || type == hf::kHoldHorn) return "wiggle";
  return "hold_above";
}

void LogCurls(const float curls[hf::kFingers], char* out, size_t size) {
  sprintf_s(out, size, "%d %d %d %d %d",
            static_cast<int>(curls[0] * 100.0f + 0.5f),
            static_cast<int>(curls[1] * 100.0f + 0.5f),
            static_cast<int>(curls[2] * 100.0f + 0.5f),
            static_cast<int>(curls[3] * 100.0f + 0.5f),
            static_cast<int>(curls[4] * 100.0f + 0.5f));
}

// THE APPROACH: the curls of the pointing hand while the grip has a target at
// the Hand's point. `want` has the curls of the pose and gets the new ones.
void Approach(const HandFeelHand& in, const float values[hf::kFingers],
              HandState* state, float want[hf::kFingers]) {
  bool on = false;
  float center[3], radius = 0.0f, half = 0.0f, point[3];
  bool acts = false;
  const int kind = HandDirectPressKind();
  if (HandDirectNearTarget(center, &radius, &half, &acts) && acts &&
      (kind == near_math::kHold || kind == near_math::kTake) &&
      HandDirectRealPoint(point) && in.units_per_meter > 0.0f) {
    const float base[3] = {center[0], center[1] - half, center[2]};
    const float gap = near_math::BodyGap(point, base, radius, 2.0f * half).gap;
    const float cm = in.units_per_meter * 0.01f;
    const float weight =
        hf::ApproachWeight(gap, g_set.pick_cm * cm, kTouchCm * cm);
    if (weight > 0.0f) {
      on = true;
      ++g_approach_frames;
      const float size = (radius > half ? radius : half) / in.length;
      const float frame = hf::PickupFrame(size);
      float pickup[hf::kFingers];
      hf::FrameCurls(hf::kPickupCurls, 3, frame, pickup);
      // The shape: a share of the curls at which the hand holds a ball of the
      // size of the target at the palm.
      float ball_radius = size;
      if (ball_radius < overlay_math::kHoldMinRadius) {
        ball_radius = overlay_math::kHoldMinRadius;
      }
      if (!g_shape.have || g_shape.mesh != in.mesh ||
          fabsf(g_shape.radius - ball_radius) > 0.004f) {
        float at[3], finger_radius[hf::kFingers];
        overlay_math::HoldCenter(ball_radius, at);
        hf::HeldBody ball;
        hf::MakeHeldBody(at, at, ball_radius, &ball);
        for (int f = 0; f < hf::kFingers; ++f) {
          finger_radius[f] = overlay_math::kShape[f].radius;
        }
        overlay_math::PoseRange range;
        hf::HoldRange(g_table, finger_radius, nullptr, ball, pickup, false,
                      &range, nullptr);
        for (int f = 0; f < hf::kFingers; ++f) g_shape.hold[f] = range.hi[f];
        g_shape.have = true;
        g_shape.mesh = in.mesh;
        g_shape.radius = ball_radius;
      }
      for (int f = 0; f < hf::kFingers; ++f) {
        pickup[f] = hf::PreShapeCurl(g_shape.hold[f], pickup[f]);
      }
      // The index finger keeps its curl: its tip is the Hand's point.
      for (int f = 0; f < hf::kFingers; ++f) {
        if (f == 1) continue;
        want[f] = hf::ApproachCurl(want[f], pickup[f], weight, values[f]);
      }
      if (!state->approach && g_approach_logs < kMaxApproachLogs) {
        ++g_approach_logs;
        char to[40];
        LogCurls(pickup, to, sizeof(to));
        ModLog("hand feel    : the pointing hand takes the form of a target "
               "of the grip, frame %llu. The target is %.2f hand lengths in "
               "size. The form is %.2f of the curls that hold a ball of that "
               "size, and not less than frame %.2f of the game's can_pickup "
               "(curls in percent, thumb to little finger: %s, and the index "
               "finger keeps its curl). The gap from the Hand's point is "
               "%.1f cm of the room, and the weight is %.2f",
               g_frame, size, hf::kPreShape, frame, to, gap / cm, weight);
      }
    }
  }
  state->approach = on;
}

// THE WORLD of a hand in this frame. False when no land and no body is near.
bool MakeWorld(const HandFeelHand& in, const HandState& state, bool left,
               hf::World* world, float* land_under, float* slope) {
  world->land = false;
  world->bodies = 0;
  float center[3];
  overlay_math::PlacePoint(in.position, in.rotation, in.length, left, kCenter,
                           center);
  const float reach = kReach * in.length;
  *land_under = 0.0f;
  *slope = 0.0f;
  if (g_set.land) {
    float highest = Ground(center[0], center[2]);
    *land_under = center[1] - highest;
    const float side[4][2] = {{reach, 0.0f}, {-reach, 0.0f}, {0.0f, reach},
                              {0.0f, -reach}};
    float at[4];
    for (int i = 0; i < 4; ++i) {
      at[i] = Ground(center[0] + side[i][0], center[2] + side[i][1]);
      if (at[i] > highest) highest = at[i];
    }
    // The slope of the land under the hand, for the log: the rise over the
    // run, across x and across z.
    const float dx = (at[0] - at[1]) / (2.0f * reach);
    const float dz = (at[2] - at[3]) / (2.0f * reach);
    *slope = sqrtf(dx * dx + dz * dz);
    if (center[1] - reach < highest + kLandSlack * in.length) {
      hf::FillPatch(center[0], center[2], reach,
                    [](float x, float z) { return Ground(x, z); },
                    &world->patch);
      world->land = true;
      ++g_land_near;
    }
  }
  // The pointing hand strokes the Creature with the flat hand or the index
  // finger, in its body. The fingers do not bend away from it then.
  if (g_set.bodies && !(in.pointing && in.stroke)) {
    // A hand with its palm in a body is in that body, and no curl takes its
    // fingers out. So that body does not count.
    float palm[3];
    overlay_math::PlacePoint(in.position, in.rotation, in.length, left, kPalm,
                             palm);
    for (int b = 0; b < state.bodies; ++b) {
      if (hf::BodyGapAt(state.body[b], center) > reach) continue;
      if (!(hf::BodyGapAt(state.body[b], palm) > 0.0f)) continue;
      world->body[world->bodies++] = state.body[b];
    }
    if (world->bodies > 0) ++g_body_near;
  }
  return world->land || world->bodies > 0;
}

}  // namespace

void HandFeelFrame() {
  LoadSettings();
  g_frame = ControllersFrameCount();
  if (!g_set.on) return;
  ++g_frames;
  for (int i = 0; i < g_set.toggle_count; ++i) {
    if (g_set.toggle_frames[i] == g_frame) Toggle("hand_feel_test_toggle");
  }
  if (!g_set.test_keys) return;
  // Ctrl, Shift and End: the pinch for a small object goes off and on.
  {
    const bool pinch_down = (GetAsyncKeyState(VK_END) & 0x8000) != 0 &&
                            (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                            (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool pinch_pressed = pinch_down && !g_pinch.key_down;
    g_pinch.key_down = pinch_down;
    if (pinch_pressed) {
      HWND front = GetForegroundWindow();
      DWORD owner = 0;
      if (front != nullptr) GetWindowThreadProcessId(front, &owner);
      if (owner == GetCurrentProcessId()) {
        g_pinch.live = !g_pinch.live;
        ControllersGesturePulse(0.5f, g_pinch.live ? 0.30f : 0.08f);
        ModLog("hand feel    : Ctrl, Shift and End: a small object in the Hand "
               "is held %s, frame %llu",
               g_pinch.live ? "in a pinch" : "in the fist", g_frame);
      }
    }
  }
  const bool down = (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool pressed = down && !g_key_down;
  g_key_down = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  Toggle("Ctrl, Shift and the minus key");
}

void HandFeelCurls(const HandFeelHand& in,
                   float curls[overlay_math::kFingers]) {
  LoadSettings();
  if (in.hand < 0 || in.hand >= kHands) return;
  HandState& state = g_hand[in.hand];
  const bool left = in.hand == 0;
  // The place of this frame, for the bodies and the object in the Hand.
  state.have_place = true;
  state.frame = g_frame;
  state.length = in.length;
  state.left = left;
  memcpy(state.position, in.position, sizeof(state.position));
  memcpy(state.rotation, in.rotation, sizeof(state.rotation));
  if (!g_set.on) return;
  const int64_t start = Now();
  float pose[hf::kFingers], want[hf::kFingers], values[hf::kFingers];
  for (int f = 0; f < hf::kFingers; ++f) pose[f] = want[f] = curls[f];
  overlay_math::FingerValues(in.trigger, in.squeeze, values);
  const bool view = TabletopOwnsView();
  int in_what = hf::kInNothing;
  bool stuck = false;
  float land_under = 0.0f, slope = 0.0f;
  if (g_live && view && in.length > 0.0f) {
    EnsureTables(in.mesh);
    if (g_set.approach && in.pointing && in.placed && !in.holding &&
        !in.stroke) {
      Approach(in, values, &state, want);
    } else {
      state.approach = false;
    }
    if (g_set.idle && in.relaxed && !state.approach) {
      state.idle_phase = hf::IdlePhase(state.idle_phase, in.dt);
      for (int f = 0; f < hf::kFingers; ++f) {
        if (f == 1 && in.pointing) continue;
        want[f] += hf::ApproachShare(values[f]) *
                   hf::IdleCurl(state.idle_phase, f);
      }
    }
    hf::World world;
    if (MakeWorld(in, state, left, &world, &land_under, &slope)) {
      const float tip = hf::kTipCm * 0.01f * in.units_per_meter;
      // The tip of the index finger of the pointing hand is the Hand's point,
      // which goes into a body to choose it. So that finger bends on the
      // land only.
      hf::World land_only = world;
      land_only.bodies = 0;
      for (int f = 0; f < hf::kFingers; ++f) {
        const bool point_finger = f == 1 && in.pointing;
        if (point_finger && !world.land) continue;
        const hf::World& around = point_finger ? land_only : world;
        hf::FingerTable table;
        hf::PlaceTable(g_table[f], in.position, in.rotation, in.length, left,
                       &table);
        float radius[hf::kSamples];
        hf::SampleRadii(overlay_math::kShape[f].radius * in.length, tip,
                        radius);
        const auto depth_at = [&](float curl, int* what) {
          float points[hf::kPoints][3], samples[hf::kSamples][3];
          hf::TableAt(table, curl, points);
          hf::Samples(points, samples);
          return hf::Depth(around, samples, radius, what);
        };
        const hf::Free free = hf::FreeCurl(
            want[f], hf::MayOpen(values[f]), state.side[f],
            [&](float curl) { return depth_at(curl, nullptr); });
        state.side[f] = free.side;
        if (!free.blocked) continue;
        int what = hf::kInNothing;
        depth_at(want[f], &what);
        if (what > in_what) in_what = what;
        if (!free.free) stuck = true;
        want[f] = free.curl;
      }
    } else {
      for (int f = 0; f < hf::kFingers; ++f) state.side[f] = 0;
    }
  } else {
    state.approach = false;
    if (g_live && !view) ++g_view_off;
  }
  // THE FILTER, on what the world adds to the curl of the pose.
  for (int f = 0; f < hf::kFingers; ++f) {
    state.add[f] = hf::FilterStep(state.add[f], want[f] - pose[f], in.dt,
                                  hf::kFilterSeconds);
    if (fabsf(state.add[f]) < 1e-4f && want[f] == pose[f]) state.add[f] = 0.0f;
    curls[f] = hf::Clamp01(pose[f] + state.add[f]);
  }
  // THE PINCH. The thumb and the index finger go to the curls at which their
  // tips meet at the object, and the thumb turns at its base.
  if (in.holding && in.mesh) {
    float weight, thumb_curl, thumb_spread, index_curl;
    if (PinchNow(&weight, &thumb_curl, &thumb_spread, &index_curl)) {
      curls[0] = pinch_math::Mix(curls[0], thumb_curl, weight);
      curls[1] = pinch_math::Mix(curls[1], index_curl, weight);
      OverlaySetFinger(in.hand, 0, curls[0], thumb_spread * weight, 1.0f);
    }
  }
  if (in_what == hf::kInLand) ++g_in_land;
  if (in_what == hf::kInBody) ++g_in_body;
  if (stuck) ++g_stuck;
  // THE PULSE, when a finger first meets the land or a body.
  if (hf::MeetPulse(in_what != hf::kInNothing, in.dt, &state.quiet) &&
      g_set.pulse) {
    ++g_pulses;
    HapticsPulseHands(in.pointing ? 2 : 1, haptics_math::kSurface);
  }
  // One line for each change of what the hand is in.
  if (in_what != state.in) {
    const bool due = g_frame >= state.in_frame + kMeetLogGap;
    if (due && g_meet_logs < kMaxMeetLogs) {
      ++g_meet_logs;
      state.in_frame = g_frame;
      char from[40], to[40];
      LogCurls(pose, from, sizeof(from));
      LogCurls(want, to, sizeof(to));
      const float cm = in.units_per_meter > 0.0f
                           ? in.units_per_meter * 0.01f
                           : 1.0f;
      ModLog("hand feel    : the fingers of the %s hand %s, frame %llu. The "
             "curls of the pose are %s and the fingers go to %s (percent, "
             "thumb to little finger)%s. The middle of the hand is %.1f cm of "
             "the room over the land, and %d bodies are near. Trigger %.2f, "
             "grip %.2f",
             left ? "left" : "right",
             in_what == hf::kInLand
                 ? "are in the land"
                 : in_what == hf::kInBody ? "are in a body" : "are free again",
             g_frame, from, to,
             stuck ? ". A finger has no free curl, so it takes the curl with "
                     "the least depth"
                   : "",
             land_under / cm, state.bodies, in.trigger, in.squeeze);
    }
    state.in = in_what;
  }
  if (g_set.log_every > 0 && g_frame % g_set.log_every == 0) {
    char from[40], to[40], now[40];
    LogCurls(pose, from, sizeof(from));
    LogCurls(want, to, sizeof(to));
    LogCurls(curls, now, sizeof(now));
    const float cm = in.units_per_meter > 0.0f ? in.units_per_meter * 0.01f
                                               : 1.0f;
    float nearest = 1e30f;
    float center[3];
    float pixel[2] = {-1.0f, -1.0f};
    OverlayTipPixel(in.hand, pixel);
    overlay_math::PlacePoint(in.position, in.rotation, in.length, left,
                             kCenter, center);
    // Each body near the hand: its kind (0 a cylinder, 1 a tree, 2 a
    // capsule, 4 a box), its radius and its gap from the middle of the hand
    // and from the palm, in units.
    char list[200] = "";
    float palm[3];
    overlay_math::PlacePoint(in.position, in.rotation, in.length, left, kPalm,
                             palm);
    for (int b = 0; b < state.bodies; ++b) {
      const float gap = hf::BodyGapAt(state.body[b], center);
      if (gap < nearest) nearest = gap;
      const size_t used = strlen(list);
      if (used + 40 < sizeof(list)) {
        sprintf_s(list + used, sizeof(list) - used, " [%d r %.1f gap %.1f palm %.1f]",
                  state.body[b].kind, state.body[b].radius, gap,
                  hf::BodyGapAt(state.body[b], palm));
      }
    }
    ModLog("hand feel    : test log, frame %llu, the %s hand%s. Pose %s, the "
           "world asks for %s, drawn %s. The middle of the hand is at %.1f "
           "%.1f %.1f, %.1f cm over the land, which has a slope of %.2f. %d "
           "bodies near, the nearest %.1f cm from the middle. In %d, the "
           "approach %s, the view %s. The index tip was at the pixel %.0f "
           "%.0f of the left eye. Bodies:%s",
           g_frame, left ? "left" : "right", in.pointing ? " (pointing)" : "",
           from, to, now, center[0], center[1], center[2], land_under / cm,
           slope, state.bodies, state.bodies > 0 ? nearest / cm : 0.0f, in_what,
           state.approach ? "is on" : "is off", view ? "is ours" : "is not ours",
           pixel[0], pixel[1], list);
  }
  CountTicks(Now() - start);
}

bool HandFeelHoldRange(int hand, bool mesh, float length, float hold_radius,
                       overlay_math::PoseRange* out) {
  LoadSettings();
  if (!g_set.on || !g_set.hold || !g_live || hand < 0 || hand >= kHands ||
      !(length > 0.0f)) {
    return false;
  }
  void* held = HeldObject();
  if (held == nullptr) {
    if (g_fall_logs < 5) {
      ++g_fall_logs;
      ModLog("hand feel    : the mod cannot read the object in the Hand, so "
             "the hold pose is the pose of the builds before, frame %llu",
             g_frame);
    }
    return false;
  }
  const int64_t start = Now();
  EnsureTables(mesh);
  void** table = *static_cast<void***>(held);
  const int type =
      reinterpret_cast<VirtualIntFn>(table[kGetHoldType / 4])(held, nullptr);
  const float game_radius = VirtualFloat(held, kGetHoldRadius);
  const float height = VirtualFloat(held, kGetHeight);
  // The body in the hand frame. The ball of hand_exact.cpp at the palm, and
  // for an object that the game holds from the side, the axis of the object
  // at the place where hand_exact.cpp put it in the frame before.
  hf::HeldBody body, ball_body;
  float ball[3];
  float ball_radius = hold_radius > 0.0f ? hold_radius / length : 0.0f;
  // The least ball: a loose cup round a small object.
  if (ball_radius < overlay_math::kHoldMinRadius) {
    ball_radius = overlay_math::kHoldMinRadius;
  }
  overlay_math::HoldCenter(ball_radius, ball);
  hf::MakeHeldBody(ball, ball, ball_radius, &ball_body);
  body = ball_body;
  bool capsule = false;
  void* placed = nullptr;
  float origin[3], up[3];
  const HandState& state = g_hand[hand];
  if (hf::HoldFromSide(type) && height > 0.0f && state.have_place &&
      HandExactHeld(&placed, origin, up) && placed == held) {
    const float top[3] = {origin[0] + up[0] * height,
                          origin[1] + up[1] * height,
                          origin[2] + up[2] * height};
    float a[3], b[3];
    hf::ToHandFrame(state.position, state.rotation, state.length, state.left,
                    origin, a);
    hf::ToHandFrame(state.position, state.rotation, state.length, state.left,
                    top, b);
    hf::MakeHeldBody(a, b, game_radius / length, &body);
    capsule = true;
  }
  // The same body as in the last frame: the same pose.
  bool same = g_hold.have && g_hold.object == held && g_hold.mesh == mesh &&
              fabsf(g_hold.body.radius - body.radius) < 0.002f;
  for (int k = 0; same && k < 3; ++k) {
    same = fabsf(g_hold.body.a[k] - body.a[k]) < 0.004f &&
           fabsf(g_hold.body.b[k] - body.b[k]) < 0.004f;
  }
  if (!same) {
    float game[hf::kFingers], finger_radius[hf::kFingers];
    hf::GameHoldCurls(type, game_radius, length, game);
    for (int f = 0; f < hf::kFingers; ++f) {
      finger_radius[f] = overlay_math::kShape[f].radius;
    }
    int touches[hf::kFingers];
    hf::HoldRange(g_table, finger_radius, capsule ? &body : nullptr, ball_body,
                  game, hf::HoldFromSide(type), &g_hold.range, touches);
    ++g_hold_made;
    // One line for a new object, and one more when its body becomes the
    // capsule: hand_exact.cpp puts the object in the palm some frames after
    // the game gives it to the Hand.
    if ((g_hold.object != held || g_hold.capsule != capsule) &&
        g_hold_logs < kMaxHoldLogs) {
      ++g_hold_logs;
      char game_text[40], hold_text[40];
      LogCurls(game, game_text, sizeof(game_text));
      LogCurls(g_hold.range.hi, hold_text, sizeof(hold_text));
      ModLog("hand feel    : the fingers close on %s (%p), frame %llu. The "
             "hold type of the game is %d (%s), the hold radius %.2f units "
             "and the height %.2f units, on a hand of %.2f units. The game's "
             "own curls are %s. The body in the hand is %s with a radius of "
             "%.3f hand lengths, from %.2f %.2f %.2f to %.2f %.2f %.2f of the "
             "hand frame, and the ball at the palm has a radius of %.3f. The "
             "hold curls are %s (percent, thumb to little finger). What each "
             "finger stops on, thumb to little finger (1 the capsule, 2 the "
             "ball, 0 the game's curl): %d %d %d %d %d",
             HandDirectClass(held), held, g_frame, type, HoldName(type),
             game_radius, height, length, game_text,
             capsule ? "a capsule on the axis of the object" : "the ball",
             body.radius, body.a[0], body.a[1], body.a[2], body.b[0],
             body.b[1], body.b[2], ball_body.radius, hold_text, touches[0],
             touches[1], touches[2], touches[3], touches[4]);
    }
    g_hold.have = true;
    g_hold.object = held;
    g_hold.mesh = mesh;
    g_hold.capsule = capsule;
    g_hold.body = body;
  }
  *out = g_hold.range;
  // The pinch takes the thumb and the index finger to the curls at which the
  // tips meet, and the other three fingers curl a little.
  float weight, thumb_curl, thumb_spread, index_curl;
  if (mesh && PinchNow(&weight, &thumb_curl, &thumb_spread, &index_curl)) {
    out->hi[0] = out->lo[0] = pinch_math::Mix(out->hi[0], thumb_curl, weight);
    out->hi[1] = out->lo[1] = pinch_math::Mix(out->hi[1], index_curl, weight);
    for (int f = 2; f < hf::kFingers; ++f) {
      out->hi[f] = pinch_math::OtherHold(out->hi[f], weight);
      if (out->lo[f] > out->hi[f]) out->lo[f] = out->hi[f];
    }
  }
  CountTicks(Now() - start);
  return true;
}

// THE PINCH (hand_pinch, docs/hand-model.md, "The pinch"). hand_exact.cpp asks
// once a frame, when it places the object in the Hand. The answer is the weight
// of the pinch against the fist, 0 to 1, and the point between the tips of
// the thumb and of the index finger in the hand frame, in hand lengths. The
// weight goes up and down in kBlendSeconds, so a change at a zoom step moves
// the object in the frame of the hand and does not jump. Only an object of the
// hold type 1 (an object with no rule of its own) and 6 (a mobile object, a
// crop, some rocks) is pinched. A tree, a villager, an animal and a miracle
// keep the pose of their hold.
float HandFeelPinch(void* held, uint64_t frame, float body_radius,
                    float hand_length, float units_per_cm, float point[3]) {
  LoadSettings();
  PinchState& p = g_pinch;
  if (held == nullptr || IsBadReadPtr(held, sizeof(void*))) return 0.0f;
  if (p.object == held && p.frame == frame) {
    for (int k = 0; k < 3; ++k) point[k] = p.pose.point[k];
    return pinch_math::Ease(p.weight);
  }
  const int64_t now = Now();
  float dt = 0.0f;
  // A new object, or the same address after a pause: a new hold.
  const bool fresh = p.object != held || g_frame > p.feel_frame + 5;
  if (!fresh && p.ticks != 0) {
    dt = static_cast<float>(Ms(now - p.ticks)) * 0.001f;
    if (dt > 0.05f) dt = 0.05f;
  }
  if (fresh) {
    p.object = held;
    p.want = false;
    p.weight = 0.0f;
    p.solved_gap = -1.0f;
    p.pose = pinch_math::Pose();
  }
  p.frame = frame;
  p.feel_frame = g_frame;
  p.ticks = now;
  bool want = false;
  float size = 0.0f;
  int type = 0;
  void** vtable = *static_cast<void***>(held);
  if (!IsBadReadPtr(vtable, kGetHoldRadius + sizeof(void*))) {
    type = reinterpret_cast<VirtualIntFn>(vtable[kGetHoldType / 4])(held,
                                                                    nullptr);
  }
  const float height = VirtualFloat(held, kGetHeight);
  size = 2.0f * body_radius > height ? 2.0f * body_radius : height;
  const bool active = g_set.on && g_set.pinch && g_live && p.live &&
                      hand_length > 0.0f && PinchType(type) &&
                      HandModelActive();
  if (active) {
    if (!p.have_table) {
      pinch_math::Fill(
          [](float curl, float spread, float out[3]) {
            float joints[hf::kPoints][3];
            HandModelFingerJoints(0, curl, spread, joints);
            for (int k = 0; k < 3; ++k) out[k] = joints[hf::kPoints - 1][k];
          },
          [](float curl, float out[3]) {
            float joints[hf::kPoints][3];
            HandModelFingerJoints(1, curl, 0.0f, joints);
            for (int k = 0; k < 3; ++k) out[k] = joints[hf::kPoints - 1][k];
          },
          &p.table);
      p.have_table = true;
      p.solved_gap = -1.0f;
    }
    const float gap = 2.0f * body_radius / hand_length;
    if (fabsf(gap - p.solved_gap) > 0.002f) {
      pinch_math::Solve(p.table, gap, &p.pose);
      p.solved_gap = gap;
    }
    want = p.pose.ok && pinch_math::Wanted(size, hand_length, p.want);
  }
  if ((fresh || want != p.want) && g_pinch_logs < kMaxPinchLogs) {
    ++g_pinch_logs;
    const float per_cm = units_per_cm > 0.0f ? 1.0f / units_per_cm : 0.0f;
    ModLog("hand feel    : the Hand holds %p (%s), frame %llu. The object is "
           "%.1f cm of the room across (the body %.1f cm, the height %.1f cm) "
           "and the hand is %.1f cm long, so the size is %.2f of the hand. "
           "The pinch starts below %.2f and ends above %.2f. The pose is %s%s. "
           "Thumb curl %.2f, thumb spread %.2f rad, index curl %.2f, the tips "
           "%.2f hand lengths apart, %.2f %.2f %.2f of the hand frame "
           "between them. The tips %s",
           held, HandDirectClass(held), g_frame, size * per_cm,
           2.0f * body_radius * per_cm, height * per_cm,
           hand_length * per_cm, hand_length > 0.0f ? size / hand_length : 0.0f,
           pinch_math::kEnterRatio, pinch_math::kLeaveRatio,
           want ? "the pinch" : "the fist",
           active ? ""
                  : (!PinchType(type)
                         ? " (this hold type has its own pose)"
                         : " (the pinch is off)"),
           p.pose.thumb_curl, p.pose.thumb_spread, p.pose.index_curl,
           p.pose.gap, p.pose.point[0], p.pose.point[1], p.pose.point[2],
           p.pose.ok ? "can meet" : "cannot meet");
  }
  p.want = want;
  p.weight = pinch_math::Step(p.weight, want, dt);
  for (int k = 0; k < 3; ++k) point[k] = p.pose.point[k];
  return pinch_math::Ease(p.weight);
}

void HandFeelSnapshot() {
  LoadSettings();
  if (!g_set.on || !g_set.bodies || !g_live) {
    for (int h = 0; h < kHands; ++h) g_hand[h].bodies = 0;
    return;
  }
  const int64_t start = Now();
  float center[kHands][3], reach[kHands];
  bool use[kHands];
  bool any = false;
  for (int h = 0; h < kHands; ++h) {
    HandState& state = g_hand[h];
    state.bodies = 0;
    use[h] = state.have_place && g_frame <= state.frame + 1 &&
             state.length > 0.0f && state.length <= kBodiesMaxLength;
    if (!use[h]) continue;
    any = true;
    overlay_math::PlacePoint(state.position, state.rotation, state.length,
                             state.left, kCenter, center[h]);
    // A hand moves between two frames, so the reach has some slack.
    reach[h] = (kReach + kBodySlack) * state.length;
  }
  if (any) {
    void* held = HeldObject();
    const int count = HandDirectConeCount();
    g_candidates_seen += static_cast<uint64_t>(count);
    for (int i = 0; i < count; ++i) {
      void* object = nullptr;
      float position[3];
      if (!HandDirectConeAt(i, &object, position) || object == held) continue;
      bool near_one = false;
      for (int h = 0; h < kHands; ++h) {
        if (!use[h]) continue;
        const float dx = position[0] - center[h][0];
        const float dz = position[2] - center[h][2];
        const float far_gap = reach[h] + kBodyFar;
        if (dx * dx + dz * dz < far_gap * far_gap) near_one = true;
      }
      if (!near_one) continue;
      hf::Body body;
      if (!HandDirectFeelBody(i, &object, body.base, &body.radius,
                              &body.height, &body.kind, body.axis,
                              body.half)) {
        continue;
      }
      // The hand goes into a scroll to tap it, and a scroll floats and turns.
      if (body.kind == hf::kBall) continue;
      for (int h = 0; h < kHands; ++h) {
        if (!use[h]) continue;
        HandState& state = g_hand[h];
        if (!hf::BodyCounts(body, state.length)) continue;
        const float gap = hf::BodyGapAt(body, center[h]);
        if (gap > reach[h]) continue;
        int slot = state.bodies;
        if (slot >= hf::kMaxBodies) {
          // The list is full: the body takes the place of the farthest one.
          slot = 0;
          for (int b = 1; b < hf::kMaxBodies; ++b) {
            if (state.body_gap[b] > state.body_gap[slot]) slot = b;
          }
          if (!(gap < state.body_gap[slot])) continue;
        } else {
          ++state.bodies;
        }
        state.body[slot] = body;
        state.body_gap[slot] = gap;
      }
    }
  }
  CountTicks(Now() - start);
}

void HandFeelReport() {
  if (!g_set.on || g_frames == 0) return;
  if (g_frame_ticks > g_worst_ticks) g_worst_ticks = g_frame_ticks;
  const double frames = static_cast<double>(g_frames);
  ModLog("hand feel    : %llu frames, %s. The land was near a hand %llu times "
         "and a body %llu times. A finger was in the land in %llu hand frames "
         "and in a body in %llu, and %llu had a finger with no free curl. The "
         "pointing hand opened toward a target in %llu frames. %llu hold "
         "poses made, %llu pulses. %llu hand frames had no view of the God "
         "view or the table. %.4f ms a frame on the CPU (the worst frame "
         "%.3f ms), %.1f land heights and %.0f candidates a frame",
         g_frames, g_live ? "on" : "off by the test key", g_land_near,
         g_body_near, g_in_land, g_in_body, g_stuck, g_approach_frames,
         g_hold_made, g_pulses, g_view_off, Ms(g_ticks) / frames,
         Ms(g_worst_ticks), static_cast<double>(g_land_calls) / frames,
         static_cast<double>(g_candidates_seen) / frames);
  g_frames = g_land_near = g_body_near = g_in_land = g_in_body = 0;
  g_stuck = g_approach_frames = g_hold_made = g_pulses = g_land_calls = 0;
  g_candidates_seen = g_view_off = 0;
  g_ticks = g_worst_ticks = g_frame_ticks = 0;
}
