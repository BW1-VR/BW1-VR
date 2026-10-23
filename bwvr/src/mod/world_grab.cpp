// Move around by grabbing the world with a motion controller, as in Google
// Earth VR. See docs/god-view.md, "5.1 The grab of the land".
//
// THE RULE. While a grab holds, one point of the world stays in the hand, and
// nothing else moves. A still hand gives a still camera, whatever the head
// does. The release stops the camera at once, with no glide.
//
// THE SOLVE. The camera is solved from the press every time
// (controller_math.h, GrabSolve), and never by adding steps. Only the grip
// points in the room go in. The head has no part in it. So a head that moves
// with still hands moves nothing, a second call in one frame gives the same
// camera, and rounding cannot drift.
//
// THE GAIN. A grip point a is scaled about the head center at the press:
// v = r0 + G * (a - r0). With grab_gain_mode=1, g is the distance from the
// camera to its focus over the distance from the eye to the hand, fixed for
// the grab, so the land looks glued to the hand. With 0 it is one to one.
//
// WHERE IT WRITES. The camera is the game's own: the six GCamera Zoomers and
// the fields of CameraModeNew3 that the snap turn also writes, in
// comfort_camera.cpp after the game's update. So the sound, the picking, the
// Hand's ray, the save game and the 3500 unit pull back all see one camera.
//
// SMOOTHING. The grip points, which are the player's own motion, ease in by a
// share of 1 - exp(-dt / grab_smoothing_ms) for each new sample. The release
// drops the rest, and the camera stays where it is.
//
// THE LIMITS. The camera and its focus stay within grab_limit of the map
// center, and the camera stays grab_min_height above the land. A grab that
// starts outside a limit keeps its start as the limit, so a press never moves
// the camera.
//
// ON THE TABLE (docs/tabletop.md, "Moving the table by the grips"). While
// the table owns the view, the same grips move the land on the table, not
// the game camera: one hand slides it, two hands turn it about up and zoom
// it. The table top stays where it is in the room. The table is solved from the
// press (table_math.h, TableGrabSolve), one to one, so the land point under
// the hand stays under the hand. The game camera stays the game's. See
// TableAnchor and TableSolve.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "controller_math.h"
#include "controllers.h"
#include "debug_menu.h"
#include "hint_card.h"
#include "hint_learn_math.h"
#include "tabletop.h"

extern void ModLog(const char* format, ...);

// controllers.cpp
bool ControllersGrabInput(GrabInput* out);
void ControllersGrabEnd(const char* why);
void ControllersGrabPulse(int hands, float amplitude, float seconds);
uint64_t ControllersFrameCount();
// head_pose.cpp
uint64_t HeadPoseOriginSerial();

namespace {

using controller_math::GrabAnchor;
using controller_math::GrabPose;

// LH3DMapCoords from a world x and z, then the land height there.
// GCamera::Update calls GetAltitude itself, so the camera mode's update can
// call it too (script_camera.cpp does the same).
const uintptr_t kMakeMapCoords = 0x004427B0;
const uintptr_t kGetAltitude = 0x00803090;
// GCamera::GetHeadingAndPitchFromPoints, cdecl (from, to, float* heading,
// float* pitch). The snap turn measures its turn with it too.
const uintptr_t kHeadingAndPitch = 0x004428D0;
struct MapCoords {
  int32_t x;
  int32_t z;
  int32_t unused;
};
typedef MapCoords*(__fastcall* MakeMapCoordsFn)(MapCoords* out, void* unused,
                                                float x, float z);
typedef float(__fastcall* GetAltitudeFn)(const MapCoords* at, void* unused);
typedef void(__cdecl* HeadingFn)(const float* from, const float* to,
                                 float* heading, float* pitch);

// The middle of the map. GCamera::Update pulls a camera that is more than
// 3500 from it back over 3 s (docs/tabletop.md). The land is 5120 across.
const float kCentre[3] = {2560.0f, 0.0f, 2560.0f};
const float kMapSize = 5120.0f;
// Two hands turn and zoom only with a real spread of 8 cm or more.
const float kMinSpreadMeters = 0.08f;
// A grip that jumps 50 cm between two samples lost its tracking. The grab
// starts a new anchor there, so the camera does not jump with it.
const float kJumpMeters = 0.5f;
// The zoom stops this far from the grabbed point, in units.
const float kLeastDistance = 5.0f;
// A hand step is logged once the camera has had this many new samples.
const int kSettleFrames = 12;
// The camera is watched this many camera updates after a release.
const int kWatchUpdates = 60;

GrabSettings g_set;
// The Shift F11 test key: 0 as set, 1 one to one, 2 off.
int g_test = 0;
const char* const kTestName[3] = {"as set in the ini", "one to one",
                                  "off"};

// THE ANCHOR of the grab now.
bool g_anchored = false;
int g_mask = 0;  // the hands of the anchor: bit 0 the other, bit 1 pointing
GrabAnchor g_anchor;
float g_r0[3] = {0.0f, 0.0f, 0.0f};  // the head center at the anchor
float g_gain = 1.0f;
float g_gain_v = 1.0f;
float g_radius = 3400.0f;  // the wall for this anchor
float g_allowance = 0.0f;  // how far the start was below the floor
float g_heading_a = 0.0f;  // the game's heading, camera to pivot
float g_yaw_a = 0.0f;      // mode+0x50
float g_theta = 0.0f;
float g_k = 1.0f;
float g_hand_a[2][3];      // the smoothed grips at the anchor
float g_anchor_distance = 0.0f;  // the camera to the grabbed point

// The grip points: smoothed, and raw from the last sample.
float g_smooth[2][3];
float g_raw[2][3];
bool g_have_raw[2] = {false, false};
uint64_t g_sample = ~0ull;
double g_sample_time = 0.0;
bool g_new_sample = false;
bool g_raw_moved = false;
bool g_settled = false;
bool g_jumped = false;
uint64_t g_origin_serial = 0;
bool g_have_origin_serial = false;

// A grab that holds, from its first anchor to its release.
bool g_holding = false;
uint64_t g_hold_frames = 0;

// What the grab wrote last. The camera is checked against it before the
// next game update.
bool g_have_written = false;
GrabCamera g_written;
bool g_external = false;
double g_last_solve = 0.0;
uint64_t g_solve_frame = 0;  // the controllers frame of the last solve

// The player's camera mode ran with input on, on this controllers frame.
uint64_t g_ready_frame = 0;
bool g_ready_seen = false;

// THE GRAB OF THE TABLE. g_on_table says which kind the grab that holds is.
bool g_on_table = false;
table_math::TableGrab g_table_anchor;
float g_table_room[3] = {0.0f, 0.0f, 0.0f};  // the table center, room meters
float g_table_low = 40.0f;
float g_table_high = 40.0f;
uint64_t g_table_serial = 0;
table_math::TablePose g_table_written;
bool g_have_table_written = false;
// The table at the press of the grab, for the hint cards: a grab that moved
// the land is a use of the grip that moves the land (hint_learn).
table_math::TablePose g_table_at_press;

// The watch after a release: 60 camera updates with no input must leave the
// camera where the grab left it. Then, for a while, the next moves of the
// camera are logged with their distance to where the grab started, so a
// turn or a zoom that swings the camera back shows.
int g_watch = 0;
float g_released[3] = {0.0f, 0.0f, 0.0f};
float g_watch_most = 0.0f;
int g_after = 0;
int g_after_logs = 0;
float g_after_last[3] = {0.0f, 0.0f, 0.0f};
float g_hold_start[3] = {0.0f, 0.0f, 0.0f};  // the camera at the first press
const int kAfterUpdates = 900;

// The log, and the counts for the report.
int g_logs = 0;
const int kMaxLogs = 160;
int g_settle = -1;
bool g_at_wall = false;
bool g_on_floor = false;
uint64_t g_held = 0;
uint64_t g_two = 0;
uint64_t g_still = 0;
uint64_t g_still_moved = 0;
float g_still_most = 0.0f;
uint64_t g_wall = 0;
uint64_t g_floor = 0;
uint64_t g_anchors = 0;
uint64_t g_game_moved = 0;
float g_game_moved_most = 0.0f;
uint64_t g_ends = 0;
// The camera step of each held frame. A real hand is never quite still, so in
// the headset this shows the tracking noise times the gain.
float g_step_most = 0.0f;
double g_step_sum = 0.0;
uint64_t g_steps = 0;

// The ground limit held the height of the last one hand solve back.
bool g_height_cut = false;

bool LogRoom() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

double Now() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return frequency.QuadPart > 0
             ? static_cast<double>(count.QuadPart) / frequency.QuadPart
             : GetTickCount64() / 1000.0;
}

void Copy3(float* to, const float* from) {
  to[0] = from[0];
  to[1] = from[1];
  to[2] = from[2];
}

// The land height at x and z. Off the map, and for a result that is not a
// height, the land is at 0.
float Ground(float x, float z) {
  if (!(x >= 0.0f && x < kMapSize && z >= 0.0f && z < kMapSize)) return 0.0f;
  MapCoords at = {0, 0, 0};
  reinterpret_cast<MakeMapCoordsFn>(kMakeMapCoords)(&at, nullptr, x, z);
  const float y = reinterpret_cast<GetAltitudeFn>(kGetAltitude)(&at, nullptr);
  if (!(y > -1000.0f && y < 5000.0f)) return 0.0f;
  return y;
}

float Heading(const float from[3], const float to[3]) {
  float heading = 0.0f, pitch = 0.0f;
  reinterpret_cast<HeadingFn>(kHeadingAndPitch)(from, to, &heading, &pitch);
  return heading;
}

bool On() { return g_set.on && g_set.hand > 0 && g_test != 2; }
int GainMode() { return g_test == 1 ? 0 : g_set.gain_mode; }

// The v point of a hand, from its smoothed grip.
void Virtual(int slot, float out[3]) {
  controller_math::GrabVirtual(g_r0, g_smooth[slot], g_gain, g_gain_v, out);
}

// The middle of the v points of the held hands, and the real spread (the
// pointing hand minus the other hand). One hand has no spread.
void Hands(int mask, float middle[3], float spread[3]) {
  if (mask == 3) {
    float v0[3], v1[3];
    Virtual(0, v0);
    Virtual(1, v1);
    for (int i = 0; i < 3; ++i) {
      middle[i] = 0.5f * (v0[i] + v1[i]);
      spread[i] = g_smooth[1][i] - g_smooth[0][i];
    }
    return;
  }
  Virtual(mask == 1 ? 0 : 1, middle);
  spread[0] = spread[1] = spread[2] = 0.0f;
}

// A new sample of the grips: the smoothing takes its share, once for each
// sample. A second camera update on the same sample changes nothing.
void Sample(const GrabInput& in, int mask) {
  g_new_sample = in.frame != g_sample;
  if (!g_new_sample) return;
  double dt = g_sample_time > 0.0 ? in.time - g_sample_time : 0.0;
  if (dt < 0.0) dt = 0.0;
  if (dt > 0.1) dt = 0.1;
  g_sample = in.frame;
  g_sample_time = in.time;
  const float jump = kJumpMeters * in.scale;
  const float share =
      g_set.smoothing_ms > 0
          ? 1.0f - expf(-static_cast<float>(dt * 1000.0) / g_set.smoothing_ms)
          : 1.0f;
  g_raw_moved = false;
  g_settled = true;
  for (int slot = 0; slot < 2; ++slot) {
    const bool held = ((mask >> slot) & 1) != 0;
    if (!held) {
      g_have_raw[slot] = false;
      continue;
    }
    const float* raw = in.hand[slot].room;
    // Settled: the eased grip was already on the real one, so this sample
    // changes nothing.
    if (!g_have_raw[slot] ||
        controller_math::Distance(raw, g_smooth[slot]) > 0.0f) {
      g_settled = false;
    }
    if (!g_have_raw[slot]) {
      // A hand that joins starts where it is, with nothing to ease.
      Copy3(g_smooth[slot], raw);
    } else {
      if (controller_math::Distance(raw, g_raw[slot]) > jump) {
        g_jumped = true;
        Copy3(g_smooth[slot], raw);
      }
      if (controller_math::Distance(raw, g_raw[slot]) > 1e-5f) {
        g_raw_moved = true;
      }
      for (int i = 0; i < 3; ++i) {
        g_smooth[slot][i] += (raw[i] - g_smooth[slot][i]) * share;
      }
      if (controller_math::Distance(raw, g_smooth[slot]) < 1e-4f) {
        Copy3(g_smooth[slot], raw);
      }
    }
    Copy3(g_raw[slot], raw);
    g_have_raw[slot] = true;
  }
}

const char* HandsName(int mask) {
  switch (mask) {
    case 1: return "the other hand";
    case 2: return "the pointing hand";
    default: return "both hands";
  }
}

// A new anchor, from the camera now and the smoothed grips. The camera does
// not move at a new anchor: its first solve gives the camera back.
bool Anchor(const GrabCamera& camera, const GrabInput& in, int mask,
            const char* why) {
  GrabAnchor a;
  memset(&a, 0, sizeof(a));
  a.hands = mask == 3 ? 2 : 1;
  Copy3(a.pose.position, camera.position);
  Copy3(a.pose.focus, camera.focus);
  Copy3(a.pose.pivot, camera.pivot);
  if (!controller_math::LevelBasis(camera.position, camera.focus, a.room)) {
    return false;
  }
  const float scale = in.scale > 0.0f ? in.scale : 7.5f;

  // The gain, from the camera and the hands of this moment.
  Copy3(g_r0, in.head);
  float hand_distance = 0.0f;
  int count = 0;
  for (int slot = 0; slot < 2; ++slot) {
    if (((mask >> slot) & 1) == 0) continue;
    hand_distance += controller_math::Distance(g_smooth[slot], in.head);
    ++count;
  }
  if (count > 0) hand_distance /= count;
  const float to_focus =
      controller_math::Distance(camera.position, camera.focus);
  const float most = static_cast<float>(g_set.gain_max);
  if (GainMode() == 1) {
    g_gain = controller_math::GrabGain(to_focus, hand_distance, scale,
                                       static_cast<float>(g_set.gain_pct),
                                       most);
  } else {
    g_gain = g_set.gain_pct * 0.01f;
    if (g_gain > most) g_gain = most;
    if (!(g_gain >= 0.1f)) g_gain = 0.1f;
  }
  g_gain_v = g_gain * g_set.vertical_pct * 0.01f;

  Hands(mask, a.middle, a.spread);
  float offset[3];
  controller_math::Apply(a.room, false, a.middle, offset);
  g_anchor_distance = controller_math::Length(offset);
  a.least_distance =
      g_anchor_distance < kLeastDistance ? g_anchor_distance : kLeastDistance;
  g_anchor = a;
  g_theta = 0.0f;
  g_k = 1.0f;

  // A limit the start already passes becomes the start.
  g_radius = static_cast<float>(g_set.limit);
  const float from_centre[2] = {
      controller_math::Distance(camera.position, kCentre),
      controller_math::Distance(camera.focus, kCentre)};
  for (int i = 0; i < 2; ++i) {
    if (from_centre[i] > g_radius) g_radius = from_centre[i];
  }
  const float floor_now =
      Ground(camera.position[0], camera.position[2]) + g_set.min_height;
  g_allowance =
      camera.position[1] < floor_now ? floor_now - camera.position[1] : 0.0f;

  g_heading_a = Heading(camera.position, camera.pivot);
  g_yaw_a = camera.yaw;
  for (int slot = 0; slot < 2; ++slot) Copy3(g_hand_a[slot], g_smooth[slot]);
  g_mask = mask;
  g_anchored = true;
  g_height_cut = false;
  g_at_wall = false;
  g_on_floor = false;
  ++g_anchors;
  if (LogRoom()) {
    ModLog("grab         : an anchor for %s (%s), frame %llu. Camera at "
           "%.2f %.2f %.2f, %.1f units from its focus, %.1f from the map "
           "center, wall at %.1f. Hand %.1f cm from the head, gain %.2f "
           "(%s), %.2f up and down.",
           HandsName(mask), why, in.frame, camera.position[0],
           camera.position[1], camera.position[2], to_focus, from_centre[0],
           g_radius, hand_distance / scale * 100.0f, g_gain,
           GainMode() == 1 ? "the land glued to the hand" : "one to one",
           g_gain_v);
    if (mask == 3) {
      ModLog("grab         : two hands %.1f cm apart. The grabbed point is "
             "%.2f units from the camera.",
             controller_math::Length(a.spread) / scale * 100.0f,
             g_anchor_distance);
    }
  }
  return true;
}

// The grab has ended. The camera stays where the grab left it, and the rest of
// the smoothing is dropped.
void Release(const char* why) {
  if (g_holding && g_on_table && g_have_table_written &&
      g_table_at_press.scale > 0.0f) {
    // In the God view one hand also moves the land up and down (center y).
    const float moved[3] = {
        g_table_written.center[0] - g_table_at_press.center[0],
        g_table_written.center[1] - g_table_at_press.center[1],
        g_table_written.center[2] - g_table_at_press.center[2]};
    if (hint_learn_math::GrabMovedLand(
            controller_math::Length(moved) / g_table_at_press.scale,
            table_math::Wrap(g_table_written.heading -
                             g_table_at_press.heading) * 57.29578f,
            g_table_written.scale / g_table_at_press.scale)) {
      HintCardLearnDone(hint_learn_math::kSkillWorld,
                        "the grip moved the land");
    }
  }
  if (g_holding && g_on_table && LogRoom()) {
    ModLog("grab         : released the table (%s) after %llu frames, frame "
           "%llu. The table stays: center %.3f %.3f, heading %.3f degrees, "
           "%.2f units per meter.", why, g_hold_frames,
           ControllersFrameCount(), g_table_written.center[0],
           g_table_written.center[2], g_table_written.heading * 57.29578f,
           g_table_written.scale);
  } else if (g_holding && LogRoom()) {
    ModLog("grab         : released (%s) after %llu frames, frame %llu. The "
           "camera stays at %.3f %.3f %.3f.", why, g_hold_frames,
           ControllersFrameCount(), g_written.position[0],
           g_written.position[1], g_written.position[2]);
  }
  g_on_table = false;
  g_have_table_written = false;
  if (g_holding && g_have_written) {
    Copy3(g_released, g_written.position);
    Copy3(g_after_last, g_written.position);
    g_watch = kWatchUpdates;
    g_watch_most = 0.0f;
    g_after = kAfterUpdates;
    g_after_logs = 0;
  }
  g_holding = false;
  g_anchored = false;
  g_mask = 0;
  g_have_raw[0] = g_have_raw[1] = false;
  g_have_written = false;
  g_settle = -1;
  g_hold_frames = 0;
}

// The player's camera mode stopped running during a grab, for example when a
// follow camera took over. Its camera is not the grab's any more, so the grab
// ends with no watch.
void DropStale() {
  if (!g_holding || ControllersFrameCount() - g_solve_frame <= 3) return;
  Release("the player's camera stopped running");
  g_watch = 0;
  g_after = 0;
  g_external = false;
}

// The log after a hand step, once the camera has settled: how far the hands
// and the camera moved since the anchor, and how far the grabbed point is
// from the hand.
void LogSettled(const GrabInput& in, const GrabCamera& out) {
  if (!LogRoom()) return;
  const float scale = in.scale > 0.0f ? in.scale : 7.5f;
  const float* start = g_anchor.pose.position;
  float moved[3], along[3];
  for (int i = 0; i < 3; ++i) moved[i] = out.position[i] - start[i];
  controller_math::Apply(g_anchor.room, true, moved, along);
  float room_now[9];
  const bool have_room =
      controller_math::LevelBasis(out.position, out.focus, room_now);
  // The anchor error: the world point that was in the hand at the anchor,
  // minus the world point in the raw hand now.
  float error = 0.0f;
  for (int slot = 0; slot < 2; ++slot) {
    if (((g_mask >> slot) & 1) == 0 || !have_room) continue;
    float v_a[3], v_now[3], at[3], was[3], now_offset[3], now[3];
    controller_math::GrabVirtual(g_r0, g_hand_a[slot], g_gain, g_gain_v, v_a);
    controller_math::GrabVirtual(g_r0, g_raw[slot], g_gain, g_gain_v, v_now);
    controller_math::Apply(g_anchor.room, false, v_a, at);
    controller_math::AddScaled(start, at, 1.0f, was);
    controller_math::Apply(room_now, false, v_now, now_offset);
    controller_math::AddScaled(out.position, now_offset, 1.0f, now);
    const float e = controller_math::Distance(was, now);
    if (e > error) error = e;
  }
  const int slot = g_mask == 1 ? 0 : 1;
  float hand_step[3];
  for (int i = 0; i < 3; ++i) {
    hand_step[i] = (g_raw[slot][i] - g_hand_a[slot][i]) / scale * 100.0f;
  }
  ModLog("grab         : settled at frame %llu. %s moved %.2f %.2f %.2f cm "
         "(right, up, forward) since the anchor. The camera moved %.4f %.4f "
         "%.4f units (right, up, forward of the anchor room), %.4f in all. "
         "Gain %.2f. %s %.4f units.",
         in.frame, g_mask == 3 ? "The pointing hand" : "The hand",
         hand_step[0], hand_step[1], hand_step[2], along[0], along[1],
         along[2], controller_math::Length(moved), g_gain,
         g_mask == 3 ? "The larger hand error (the zoom moves the hands' "
                       "points by design)"
                     : "Anchor error",
         error);
  if (g_mask == 3) {
    float a_point[3];
    controller_math::GrabAnchorPoint(g_anchor, a_point);
    const float distance = controller_math::Distance(out.position, a_point);
    // The grabbed point A is under the middle of the hands, k times nearer.
    float middle[3], spread[3], offset[3], at[3];
    Hands(3, middle, spread);
    float a_error = 0.0f;
    if (have_room) {
      controller_math::Apply(room_now, false, middle, offset);
      controller_math::AddScaled(out.position, offset, 1.0f / g_k, at);
      a_error = controller_math::Distance(at, a_point);
    }
    const float heading_now = Heading(out.position, out.pivot);
    ModLog("grab         : two hands: spread %.1f cm (%.1f at the anchor), "
           "zoom k %.4f, turn %.3f degrees, the game's heading turned %.3f "
           "degrees, the camera is %.3f units from the grabbed point (%.3f at "
           "the anchor, ratio %.4f), its error %.4f units, the camera height "
           "%.3f to %.3f.",
           controller_math::Length(spread) / scale * 100.0f,
           controller_math::Length(g_anchor.spread) / scale * 100.0f, g_k,
           g_theta * 57.29578f,
           controller_math::WrapAngle(heading_now - g_heading_a) * 57.29578f,
           distance, g_anchor_distance,
           distance > 0.0f ? g_anchor_distance / distance : 0.0f, a_error,
           start[1], out.position[1]);
  }
}

// THE GRAB OF THE TABLE.
//
// The hands in room meters, from the smoothed grips. The gain is 1: on the
// table the land under the hand stays under the hand.
void TableHands(const GrabInput& in, int mask, float middle[3],
                float spread[3]) {
  const float scale = in.scale > 0.0f ? in.scale : 7.5f;
  Hands(mask, middle, spread);
  for (int i = 0; i < 3; ++i) {
    middle[i] /= scale;
    spread[i] /= scale;
  }
}

// A new anchor on the table, from the table now and the smoothed grips. The
// table does not move at a new anchor: its first solve gives it back.
bool TableAnchor(const GrabInput& in, int mask, const char* why) {
  table_math::TablePose pose;
  uint64_t serial = 0;
  if (!TabletopPose(&pose, g_table_room, &g_table_low, &g_table_high,
                    &serial)) {
    return false;
  }
  g_gain = 1.0f;
  g_gain_v = 1.0f;
  Copy3(g_r0, in.head);
  table_math::TableGrab a;
  memset(&a, 0, sizeof(a));
  a.hands = mask == 3 ? 2 : 1;
  a.pose = pose;
  TableHands(in, mask, a.middle, a.spread);
  g_table_anchor = a;
  g_table_serial = serial;
  g_theta = 0.0f;
  g_k = 1.0f;
  for (int slot = 0; slot < 2; ++slot) Copy3(g_hand_a[slot], g_smooth[slot]);
  g_mask = mask;
  g_anchored = true;
  g_height_cut = false;
  g_at_wall = false;
  g_on_floor = false;
  ++g_anchors;
  if (LogRoom()) {
    float land[3];
    table_math::FlatWorld(pose, g_table_room, a.middle, land);
    ModLog("grab         : an anchor on the table for %s (%s), frame %llu. The "
           "table center is at %.2f %.2f, heading %.2f degrees, %.1f units "
           "per meter. The land point under the hands is %.2f %.2f.",
           HandsName(mask), why, in.frame, pose.center[0], pose.center[2],
           pose.heading * 57.29578f, pose.scale, land[0], land[2]);
    if (mask == 3) {
      ModLog("grab         : two hands %.1f cm apart on the table.",
             controller_math::Length(a.spread) * 100.0f);
    }
  }
  return true;
}

// The log after a hand step on the table, once the grips have settled: how
// far the hands and the land moved, and how far the land point that was
// under the hands at the anchor is from the land point under them now.
void LogTableSettled(const GrabInput& in, const table_math::TablePose& now) {
  if (!LogRoom()) return;
  const float scale = in.scale > 0.0f ? in.scale : 7.5f;
  float middle[3], spread[3];
  TableHands(in, g_mask, middle, spread);
  float was[3], under[3];
  table_math::FlatWorld(g_table_anchor.pose, g_table_room,
                        g_table_anchor.middle, was);
  table_math::FlatWorld(now, g_table_room, middle, under);
  const int slot = g_mask == 1 ? 0 : 1;
  float hand_step[3];
  for (int i = 0; i < 3; ++i) {
    hand_step[i] = (g_raw[slot][i] - g_hand_a[slot][i]) / scale * 100.0f;
  }
  const float moved[3] = {now.center[0] - g_table_anchor.pose.center[0], 0.0f,
                          now.center[2] - g_table_anchor.pose.center[2]};
  ModLog("grab         : the table settled at frame %llu. %s moved %.2f %.2f "
         "%.2f cm (right, up, forward) since the anchor. The table center "
         "moved %.3f units, the heading %.3f degrees, the scale %.2f to %.2f "
         "units per meter (zoom k %.4f). The land point of the anchor is "
         "%.4f units from the land under the hands.",
         in.frame, g_mask == 3 ? "The pointing hand" : "The hand",
         hand_step[0], hand_step[1], hand_step[2],
         controller_math::Length(moved),
         table_math::Wrap(now.heading - g_table_anchor.pose.heading) *
             57.29578f,
         g_table_anchor.pose.scale, now.scale, g_k,
         controller_math::Distance(was, under));
  // In the God view, one hand also moves the ground up and down.
  if (g_mask != 3 && TabletopGodOwnsView()) {
    ModLog("grab         : the ground went %.2f cm up in the room since the "
           "anchor, and the hand went %.2f cm up, frame %llu",
           -(now.center[1] - g_table_anchor.pose.center[1]) / now.scale *
               100.0f,
           hand_step[1], in.frame);
  }
}

// One solve of the table, from the anchor and the smoothed grips. It moves
// the table and writes no camera. The game camera stays the game's.
void TableSolve(const GrabInput& in, int mask, const char* why) {
  float middle[3], spread[3];
  TableHands(in, mask, middle, spread);
  // The game's camera switches: the tutorial turns the turn and the zoom on
  // at its own steps (docs/god-view.md, "7. The game camera").
  bool turn = g_set.turn, zoom = g_set.zoom;
  TabletopGrabAllows(&turn, &zoom);
  table_math::TablePose pose;
  // In the God view one hand also moves the land up and down, inside the
  // ground limits. Two hands keep the height of the press.
  const bool god = TabletopGodOwnsView();
  table_math::TableGrabSolve(g_table_anchor, g_table_room, middle, spread,
                             turn, zoom, kMinSpreadMeters, g_table_low,
                             g_table_high, &g_theta, &g_k, &pose, god);
  if (god && mask != 3) {
    const float asked = pose.center[1];
    bool cut = false;
    TabletopGodLimitHeight(&pose, &cut);
    if (cut != g_height_cut && LogRoom()) {
      const float over = (asked - pose.center[1]) / pose.scale;
      if (cut) {
        ModLog("grab         : the ground stops at its limit, %.2f m past it "
               "(%s), frame %llu", fabsf(over),
               over > 0.0f ? "too far below the head" : "too near the head",
               in.frame);
      } else {
        ModLog("grab         : the ground moves again, frame %llu", in.frame);
      }
    }
    g_height_cut = cut;
  }
  TabletopMove(pose);
  // What the table took: the center stays on the map.
  float room[3], low = 0.0f, high = 0.0f;
  uint64_t serial = 0;
  TabletopPose(&pose, room, &low, &high, &serial);

  if (g_new_sample) {
    ++g_held;
    ++g_hold_frames;
    if (mask == 3) ++g_two;
    if (g_have_table_written && why == nullptr) {
      const float step[3] = {pose.center[0] - g_table_written.center[0], 0.0f,
                             pose.center[2] - g_table_written.center[2]};
      const float frame_step = controller_math::Length(step);
      if (frame_step > g_step_most) g_step_most = frame_step;
      g_step_sum += frame_step;
      ++g_steps;
      // With the hands still and the smoothing done, the table must not
      // move at all: not its center, its heading or its scale.
      if (!g_raw_moved && g_settled) {
        const float change =
            frame_step +
            fabsf(table_math::Wrap(pose.heading - g_table_written.heading)) +
            fabsf(pose.scale - g_table_written.scale);
        ++g_still;
        if (change > 1e-3f) ++g_still_moved;
        if (change > g_still_most) g_still_most = change;
      }
    }
    if (g_raw_moved) g_settle = kSettleFrames;
    if (g_settle > 0 && g_settled) {
      --g_settle;
      if (g_settle == 0) LogTableSettled(in, pose);
    }
  }
  g_table_written = pose;
  g_have_table_written = true;
}

}  // namespace

void WorldGrabSetOptions(const GrabSettings& settings) {
  g_set = settings;
  if (g_set.gain_pct < 1) g_set.gain_pct = 1;
  if (g_set.gain_max < 1) g_set.gain_max = 1;
  if (g_set.vertical_pct < 0) g_set.vertical_pct = 0;
  if (g_set.smoothing_ms < 0) g_set.smoothing_ms = 0;
  if (g_set.smoothing_ms > 500) g_set.smoothing_ms = 500;
  if (g_set.limit < 100) g_set.limit = 100;
  if (g_set.min_height < 0) g_set.min_height = 0;
  if (g_set.max_speed < 0) g_set.max_speed = 0;
  if (!On()) return;
  ModLog("grab         : gain %s, %d%%, at most %d, %d%% up and down, "
         "smoothing %d ms, two hands %s, turn %s, zoom %s, wall %d units from "
         "the map center, %d units above the land, speed cap %d units/s",
         g_set.gain_mode == 1 ? "glued to the land" : "one to one",
         g_set.gain_pct, g_set.gain_max, g_set.vertical_pct,
         g_set.smoothing_ms, g_set.two_hands ? "on" : "off",
         g_set.turn ? "on" : "off", g_set.zoom ? "on" : "off", g_set.limit,
         g_set.min_height, g_set.max_speed);
}

// True when the player's camera mode ran with its input on, on one of the
// last few frames. A grip only grabs then.
bool WorldGrabCameraReady() {
  return g_ready_seen && ControllersFrameCount() - g_ready_frame <= 3;
}

// comfort_camera.cpp, before the game's update of CameraModeNew3: the Zoomer
// destinations. Between two frames of a grab, only the grab moves the camera.
// Any other change is another part of the game, and the grab ends.
void WorldGrabCheckCamera(const float position[3], const float focus[3]) {
  DropStale();
  if (g_holding && g_have_written) {
    const float moved_position =
        controller_math::Distance(position, g_written.position);
    const float moved_focus = controller_math::Distance(focus, g_written.focus);
    const float moved =
        moved_position > moved_focus ? moved_position : moved_focus;
    if (moved > 0.01f) {
      g_external = true;
      if (LogRoom()) {
        ModLog("grab         : the camera moved %.3f units between two frames "
               "of the grab, and the grab did not move it, frame %llu", moved,
               ControllersFrameCount());
      }
    }
  }
  if (g_watch > 0) {
    const float d = controller_math::Distance(position, g_released);
    if (d > g_watch_most) g_watch_most = d;
    --g_watch;
    if (g_watch == 0 && LogRoom()) {
      ModLog("grab         : %d camera updates after the release, the camera "
             "is %.4f units from where the grab left it, %.4f at most, frame "
             "%llu", kWatchUpdates, d, g_watch_most, ControllersFrameCount());
    }
  }
  if (g_after > 0 && !g_holding) {
    --g_after;
    const float step = controller_math::Distance(position, g_after_last);
    if (step > 0.01f && g_after_logs < 4 && LogRoom()) {
      ++g_after_logs;
      ModLog("grab         : after the release the camera moved %.3f units in "
             "one update, to %.3f %.3f %.3f. That is %.3f from where the grab "
             "left it and %.3f from where the grab started, frame %llu", step,
             position[0], position[1], position[2],
             controller_math::Distance(position, g_released),
             controller_math::Distance(position, g_hold_start),
             ControllersFrameCount());
    }
    Copy3(g_after_last, position);
  }
}

// comfort_camera.cpp, after the game's update of CameraModeNew3: the camera
// the grab wants, from the hands of the last frame. False when no grab holds,
// and then nothing is written.
bool WorldGrabSolve(const GrabCamera& camera, GrabCamera* out) {
  DropStale();
  g_solve_frame = ControllersFrameCount();
  if (!camera.input_off && !camera.fight) {
    g_ready_frame = g_solve_frame;
    g_ready_seen = true;
  }
  GrabInput in;
  int mask = 0;
  if (ControllersGrabInput(&in) && On()) {
    mask = (in.hand[0].held ? 1 : 0) | (in.hand[1].held ? 2 : 0);
    if (!g_set.two_hands && mask == 3) mask = 1;
  }
  if (mask == 0) {
    if (g_holding) Release("the grips opened");
    g_external = false;
    return false;
  }
  // The table owns the view: the grab moves the land on the table.
  const bool table = TabletopOwnsView();
  const char* end = nullptr;
  if (g_holding && table != g_on_table) {
    end = "the table went on or off";
  } else if (g_external) {
    end = "another part of the game moved the camera";
  } else if (camera.input_off) {
    end = "the camera takes no input now";
  } else if (camera.fight) {
    end = "a fight holds the camera";
  }
  g_external = false;
  if (end != nullptr) {
    ++g_ends;
    ControllersGrabEnd(end);
    Release(end);
    return false;
  }
  // A jump in flight: wait, and anchor where it lands, not where the grab
  // wrote last. A jump moves the game camera, not the table.
  if (camera.flying && !table) {
    g_anchored = false;
    g_have_written = false;
    return false;
  }

  Sample(in, mask);
  const uint64_t origin = HeadPoseOriginSerial();
  uint64_t table_serial = g_table_serial;
  if (table) {
    table_math::TablePose now_pose;
    float room[3], low = 0.0f, high = 0.0f;
    TabletopPose(&now_pose, room, &low, &high, &table_serial);
  }
  const char* why = nullptr;
  if (!g_anchored) {
    why = g_holding ? "a jump landed" : "a new press";
  } else if (mask != g_mask) {
    why = "the hands changed";
  } else if (g_jumped) {
    why = "a grip jumped 50 cm";
  } else if (g_have_origin_serial && origin != g_origin_serial) {
    why = "the room was recentered";
  } else if (table && table_serial != g_table_serial) {
    why = "the table was built again";
  }
  g_origin_serial = origin;
  g_have_origin_serial = true;
  g_jumped = false;
  if (why != nullptr) {
    if (table) {
      if (!TableAnchor(in, mask, why)) return false;
      if (!g_holding) {
        g_holding = true;
        g_on_table = true;
        g_hold_frames = 0;
        g_table_at_press = g_table_anchor.pose;
        g_watch = 0;
        g_after = 0;
      }
    } else {
      // While a grab holds, a new anchor starts from what it wrote last, so
      // the game's own update of this frame cannot slip in.
      const GrabCamera start = g_holding && g_have_written ? g_written : camera;
      if (!Anchor(start, in, mask, why)) return false;
      if (!g_holding) {
        g_holding = true;
        g_on_table = false;
        g_hold_frames = 0;
        g_watch = 0;
        g_after = 0;
        Copy3(g_hold_start, start.position);
      }
    }
    g_settle = -1;
  }
  if (table) {
    // The land moves on the table. The game camera is not written.
    TableSolve(in, mask, why);
    return false;
  }

  // THE SOLVE, from the anchor and the smoothed grips.
  float middle[3], spread[3];
  Hands(mask, middle, spread);
  GrabPose pose;
  controller_math::GrabSolve(g_anchor, middle, spread,
                             g_set.turn && camera.may_turn, g_set.zoom,
                             kMinSpreadMeters * in.scale, &g_theta, &g_k,
                             &pose);

  // The speed cap, when one is set. The camera catches up while the grab
  // holds, and never after it.
  const double now = Now();
  if (g_set.max_speed > 0 && g_have_written) {
    double dt = now - g_last_solve;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.1) dt = 0.1;
    const float most = static_cast<float>(g_set.max_speed * dt);
    float step[3];
    for (int i = 0; i < 3; ++i) {
      step[i] = pose.position[i] - g_written.position[i];
    }
    const float length = controller_math::Length(step);
    if (length > most && length > 0.0f) {
      float back[3];
      for (int i = 0; i < 3; ++i) back[i] = -step[i] * (1.0f - most / length);
      controller_math::MovePose(back, &pose);
    }
  }
  g_last_solve = now;

  // THE LIMITS: the wall, then the floor. The hand keeps moving and the
  // world slides through the wall. The solve is absolute, so the camera
  // leaves the wall where the solved pose crosses it.
  const bool wall = controller_math::GrabKeepInside(kCentre, g_radius, &pose);
  const float floor_y = Ground(pose.position[0], pose.position[2]) +
                        g_set.min_height - g_allowance;
  const bool floor = controller_math::GrabKeepAbove(floor_y, &pose);
  if ((wall && !g_at_wall) || (floor && !g_on_floor)) {
    ControllersGrabPulse(mask, 0.30f, 0.020f);
    if (LogRoom()) {
      ModLog("grab         : the camera stops at the %s, frame %llu. Distance "
             "from the map center %.3f (wall %.3f), height %.3f (floor "
             "%.3f).", wall && !g_at_wall ? "wall" : "floor", in.frame,
             controller_math::Distance(pose.position, kCentre), g_radius,
             pose.position[1], floor_y);
    }
  }
  g_at_wall = wall;
  g_on_floor = floor;

  // The heading: by the game's own measure, as the snap turn does it.
  float yaw = g_yaw_a;
  if (g_theta != 0.0f) {
    yaw = g_yaw_a + controller_math::WrapAngle(
                        Heading(pose.position, pose.pivot) - g_heading_a);
  }

  *out = camera;
  Copy3(out->position, pose.position);
  Copy3(out->focus, pose.focus);
  Copy3(out->pivot, pose.pivot);
  out->yaw = yaw;

  // The counts, once for each new sample of the grips.
  if (g_new_sample) {
    ++g_held;
    ++g_hold_frames;
    if (mask == 3) ++g_two;
    if (wall) ++g_wall;
    if (floor) ++g_floor;
    if (g_have_written && why == nullptr) {
      const float frame_step =
          controller_math::Distance(out->position, g_written.position);
      if (frame_step > g_step_most) g_step_most = frame_step;
      g_step_sum += frame_step;
      ++g_steps;
      // The game's own update of this frame, before the grab wrote.
      const float game =
          controller_math::Distance(camera.position, g_written.position);
      if (game > 1e-3f) {
        ++g_game_moved;
        if (game > g_game_moved_most) g_game_moved_most = game;
      }
      // With the hands still and the smoothing done, the camera must not
      // move at all.
      if (!g_raw_moved && g_settled) {
        ++g_still;
        const float step =
            controller_math::Distance(out->position, g_written.position);
        if (step > 1e-3f) ++g_still_moved;
        if (step > g_still_most) g_still_most = step;
      }
    }
    if (g_raw_moved) g_settle = kSettleFrames;
    if (g_settle > 0 && g_settled) {
      --g_settle;
      if (g_settle == 0) LogSettled(in, *out);
    }
  }
  g_written = *out;
  g_have_written = true;
  return true;
}

// The Shift F11 test key: the grab as set, one to one, or off. A grab that
// holds ends, and the next press uses the new choice.
void WorldGrabCycleTest() {
  g_test = (g_test + 1) % 3;
  if (g_holding) {
    ControllersGrabEnd("the test key");
    Release("the test key");
  }
  ModLog("grab         : the test key sets the grab of the world to %s",
         kTestName[g_test]);
}

// The step of the Shift F11 test key for the debug page: 0, 1 or 2. With no
// grab in the ini, the step changes nothing.
int WorldGrabTestValue() {
  if (!g_set.on || g_set.hand <= 0) return kDebugCannot;
  return g_test;
}

// Once a report window.
void WorldGrabReport() {
  if (g_held == 0 && g_anchors == 0 && g_ends == 0) return;
  ModLog("grab         : %llu frames held (%llu with two hands), %llu anchors, "
         "%llu ends by the camera. With the hands still, the camera moved on "
         "%llu of %llu frames, %.4f units at most. %llu frames at the wall, "
         "%llu on the floor. The game's own update moved the held camera on "
         "%llu frames, %.3f units at most.",
         g_held, g_two, g_anchors, g_ends, g_still_moved, g_still,
         g_still_most, g_wall, g_floor, g_game_moved, g_game_moved_most);
  if (g_steps != 0) {
    ModLog("grab         : the held camera moved %.4f units a frame on "
           "average, %.4f at most, over %llu frames",
           g_step_sum / static_cast<double>(g_steps), g_step_most, g_steps);
  }
  g_step_most = 0.0f;
  g_step_sum = 0.0;
  g_steps = 0;
  g_held = g_two = g_anchors = g_ends = 0;
  g_still = g_still_moved = g_wall = g_floor = g_game_moved = 0;
  g_still_most = g_game_moved_most = 0.0f;
}
