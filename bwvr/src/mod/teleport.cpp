// The teleport with a visible arc. See docs/god-view.md, "The teleport", and
// docs/controllers.md.
//
// THE PLAYER'S RULE. Hold A to aim and release A to go. While A is down, an
// arc leaves the pointing hand and falls on the land, and a ring shows the
// target. The arc is cyan when the target is valid and red when it is not.
// Release A on a valid target and the view goes there as one cut, with a
// short black blink. Nothing moves after the cut. Release on a red target and
// nothing happens. B while A is down cancels. Look at the arc and the ring to
// know where you will go before you let go.
//
// THE MATH is in controller_math.h (the arc and the hit), table_math.h
// (GodTeleport) and overlay_math.h (the tube and the ring), and test/
// controller_math checks it. This file reads the game and keeps the states.
//
// ONE FRAME. TeleportFrame runs once a frame (stereo.cpp), after the hands of
// the frame are known. It reads A and B as edges that controllers.cpp latched
// once, finds the arc and the hit on the CPU, and keeps the result for the
// overlay (TeleportDrawState). A release goes to the target of the last frame
// that was drawn, so the player gets what they saw.
//
// THE SMOOTH AIM. A hand in the air shakes a little, and the shake grows with
// the length of the arc. So the aim point and the aim direction go through a
// one euro filter (aim_smooth.h) before the arc is found. The filter is slow
// when the hand is still, so the ring does not shake. It is fast when the hand
// moves, so a real move of the aim shows at once. The filter starts again at
// each press of A. The land is read with at
// most 56 GetAltitude calls for the arc and 72 for the ring, on the game
// thread, before the eye passes.
//
// WHERE IT GOES. The God view: TabletopGodTeleport puts the head over the
// target. The table: TabletopTableTeleport puts the target at the table
// center. The immersive view: ComfortCameraTeleport adds the move of the eye
// to the game camera. In each case it is one cut at the next camera update.
//
// THE MOUSE. A double click of the mouse in the God view teleports too
// (TeleportMouseDoubleClick). The game's own jump would fly a camera that the
// God view does not show.
//
// THE REFUSALS. The press of A is refused, with one short buzz, when a grip
// holds the world, a gesture is drawn, a story owns the camera, a dialog is
// up, the camera input is off, there is a fight, the game's switch for a move
// is off, or the hand cannot act. A refusal that starts while the player aims
// cancels the aim.
//
// THE SWITCH OF THE GAME (teleport_switch). A script of the game turns the
// camera controls on and off with the bits of CameraHelp::EnabledFeatures.
// The teleport moves the player over the land, as the walk with the stick
// does, so the press of A keeps to the switch of the walk: the drag of the
// land (bit 8). The first step of the tutorial has only that bit, and the
// player goes to the village with the teleport or with the stick. With
// teleport_switch=0 the press keeps to the switch of the double click jump
// (bit 0x10). The mouse double click is that jump, so it always keeps to bit
// 0x10.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "aim_smooth.h"
#include "controller_math.h"
#include "controllers.h"
#include "haptics.h"
#include "hint_card.h"
#include "hint_learn_math.h"
#include "overlay_math.h"
#include "table_math.h"
#include "teleport_math.h"
#include "tabletop.h"
#include "teleport.h"

extern void ModLog(const char* format, ...);
// head_camera.cpp: the head is on the camera.
bool HeadCameraEnabled();
// head_pose.cpp: the life size scale, and the last head sample.
float HeadPoseScale();
bool HeadPoseCurrent(float matrix_out[9], float translation_out[3]);
// script_camera.cpp: a story or a follow camera holds the view.
bool ScriptCameraStoryOwns();
// comfort_camera.cpp: the camera mode, and the move of the immersive view.
const void* ComfortCameraLastMode();
void ComfortCameraTeleport(const float delta[3]);
// pointer_map.cpp: the ray of the mouse, while the mouse owns the pointer.
bool PointerMapMouseRay(float dir_room[3]);
// controllers.cpp: a pulse on the pointing hand.
void ControllersGesturePulse(float amplitude, float seconds);

namespace {

using controller_math::kArcGravity;
using controller_math::kArcMaxSeconds;
using controller_math::kArcSteps;
using table_math::TablePose;

// LH3DIsland::GetAltitude, and the map coordinates it takes.
const uintptr_t kMakeMapCoords = 0x004427B0;
const uintptr_t kGetAltitude = 0x00803090;
struct MapCoords {
  int32_t x, z, unused;
};
typedef MapCoords*(__fastcall* MakeMapCoordsFn)(MapCoords* out, void* unused,
                                                float x, float z);
typedef float(__fastcall* GetAltitudeFn)(const MapCoords* at, void* unused);
const float kMapSize = 5120.0f;
const float kMapCenter = 2560.0f;
// The God view keeps the player's place within 3000 units of the map center
// (tabletop.cpp, kGodGameRadius). The immersive view keeps the camera within
// 3400 units, inside the pull back of GCamera::Update at 3500.
const float kGodRadius = 3000.0f;
const float kImmersiveRadius = 3400.0f;

// The latch of the mouse events, and its double click bit.
const uintptr_t kLatch = 0x00E85350;
const uint8_t kLatchDouble = 0x10;
// CameraHelp::EnabledFeatures, and its bit for the jump.
const uintptr_t kEnabledFeatures = 0x009CDD6C;
const uint32_t kFeatureJump = 0x10;
// SetupBox::GetCurrentActiveBox reads this: a dialog box is up when it is not
// null.
const uintptr_t kActiveBox = 0x00C4CD20;
// CameraModeNew3: the camera input switch and the fight.
const int kModeFight = 0x44;
const int kModeInputOff = 0x2EC;
const UINT_PTR kModeReadSize = 0x2F0;
// GGame::MyInterface, and the object under the Hand in its result.
const uintptr_t kGame = 0x00D0195C;
const uintptr_t kMyInterface = 0x00555850;
const int kFaceObject = 0x3C8;
typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);

// A target this close to the point under the head is nothing to do.
const float kNearM = 0.30f;
const int kMaxAimLogs = 90;
const int kMaxMiscLogs = 60;
const ULONGLONG kSecondTickMs = 120;

enum Mode { kNone, kGod, kTable, kImmersive };
const char* const kModeName[4] = {"none", "God view", "table", "immersive"};

TeleportSettings g_settings;
bool g_installed = false;

// THE STATE. aiming is true from a valid press of A until its release or a
// cancel. need_up is a cancel that waits for A to come up.
bool g_aiming = false;
Mode g_mode = kNone;
uint64_t g_frame = 0;

// The result of the last aim frame.
struct Aim {
  bool have;          // the arc meets the ground at target
  bool valid;
  const char* why;    // why it is valid or not, always set while aiming
  float target[3];    // in the world, y is the land height
  float ground;
  float origin_m[3];
  float direction[3];
  float eye[3];
  float units_per_meter;
  float eye_ground;
};
Aim g_aim = {};
TeleportDraw g_draw = {};
float g_arc_room[overlay_math::kArcMaxPoints][3];

// For the log: what the last logged aim line said.
bool g_logged_have = false;
bool g_logged_valid = false;
const char* g_logged_why = "";
uint64_t g_logged_frame = 0;
int g_aim_logs = 0;
int g_misc_logs = 0;
ULONGLONG g_second_tick_at = 0;
bool g_refused_buzz = false;

// THE PRESS IS POSSIBLE: what the last TeleportFrame found, for the hint
// card. And the reason of the game that the log last gave, with its count.
bool g_possible = false;
const char* g_logged_game = "";
int g_game_logs = 0;
const int kMaxGameLogs = 40;

// THE SMOOTH AIM: one filter for the aim point and one for the direction, and
// the time of the last aim frame.
aim_smooth::OneEuroVec3 g_filter_point;
aim_smooth::OneEuroVec3 g_filter_direction;
LONGLONG g_filter_tick = 0;
float g_smooth_most_deg = 0.0f;  // the largest gap of raw and smoothed aim

// THE COUNTS of a report window.
uint64_t g_aims = 0;
uint64_t g_aim_frames = 0;
uint64_t g_valid_frames = 0;
uint64_t g_invalid_frames = 0;
uint64_t g_sky_frames = 0;
uint64_t g_nohit_frames = 0;
uint64_t g_refused = 0;
uint64_t g_moves = 0;
uint64_t g_released_invalid = 0;
uint64_t g_cancels = 0;
uint64_t g_mouse_moves = 0;
uint64_t g_mouse_refused = 0;
uint64_t g_ground_calls = 0;
int g_frame_calls = 0;
int g_most_calls = 0;
uint64_t g_draw_passes = 0;
uint64_t g_draw_triangles = 0;

bool MiscLog() {
  if (g_misc_logs >= kMaxMiscLogs) return false;
  ++g_misc_logs;
  return true;
}

// The land height, with the sea and the places off the map at 0. A point on
// the map has the height of GetAltitude, or 0 when that is not a land height.
float Ground(float x, float z) {
  ++g_ground_calls;
  ++g_frame_calls;
  if (!(x >= 0.0f && x < kMapSize && z >= 0.0f && z < kMapSize)) return 0.0f;
  MapCoords at = {0, 0, 0};
  reinterpret_cast<MakeMapCoordsFn>(kMakeMapCoords)(&at, nullptr, x, z);
  const float y =
      reinterpret_cast<GetAltitudeFn>(kGetAltitude)(&at, nullptr);
  if (!(y > 0.0f && y < 5000.0f)) return 0.0f;
  return y;
}

bool OnMap(float x, float z) {
  return x >= 0.0f && x < kMapSize && z >= 0.0f && z < kMapSize;
}

// A point of the room, in meters, in the world.
void RoomToWorld(const TeleportInput& in, const float r[3], float out[3]) {
  for (int i = 0; i < 3; ++i) {
    out[i] = in.base[i] + in.map[i * 3 + 0] * r[0] +
             in.map[i * 3 + 1] * r[1] + in.map[i * 3 + 2] * r[2];
  }
}

const uint8_t* CameraMode() {
  return static_cast<const uint8_t*>(ComfortCameraLastMode());
}

// Which view the teleport goes through, and why there is none.
Mode CurrentMode(const char** why) {
  if (TabletopGodOwnsView()) return kGod;
  if (TabletopOwnsView()) return kTable;
  if (TabletopIsOn()) {
    *why = "the view does not belong to the player now";
    return kNone;
  }
  if (!HeadCameraEnabled()) {
    *why = "the head is not on the camera";
    return kNone;
  }
  return kImmersive;
}

bool JumpSwitchOn() {
  return (*reinterpret_cast<const uint32_t*>(kEnabledFeatures) &
          kFeatureJump) != 0;
}

// The first reason that the teleport cannot start or go on, or null. The
// reasons that need the hand are in Refuse, and the ones of the game are
// here, so the mouse double click uses them too. jump is true for a move that
// keeps to the game's jump switch, and false for one that keeps to its switch
// for the drag of the land.
const char* GameRefuses(bool jump) {
  if (ScriptCameraStoryOwns()) return "a story owns the camera";
  if (*reinterpret_cast<const uintptr_t*>(kActiveBox) != 0) {
    return "a dialog is up";
  }
  const uint8_t* mode = CameraMode();
  if (mode == nullptr) return "the camera mode is not known";
  if (*reinterpret_cast<const int32_t*>(mode + kModeInputOff) != 0) {
    return "the camera input is off";
  }
  if (*reinterpret_cast<const int32_t*>(mode + kModeFight) != 0) {
    return "there is a fight";
  }
  if (jump) {
    if (!JumpSwitchOn()) {
      return "the game's jump switch is off (camera switch 0x10)";
    }
  } else if (!TabletopMoveAllowed()) {
    return "the game's switch for the drag of the land is off (camera switch "
           "8)";
  }
  return nullptr;
}

// The press of A keeps to the jump switch (teleport_switch=0).
bool PressNeedsJump() { return g_settings.move_switch == 0; }

// The first reason that refuses the press of A, or null. game gets the
// reason of the view or of the game alone, with no reason of the hand.
const char* Refuse(const TeleportInput& in, Mode* mode, const char** game) {
  const char* why = nullptr;
  *mode = CurrentMode(&why);
  *game = why != nullptr ? why : GameRefuses(PressNeedsJump());
  if (why != nullptr) return why;
  if (!in.hand) return "the pointing hand is not active";
  if (!in.usable) return "the hand cannot act now, for example in a menu";
  if (in.grab_held) return "a grip holds the world";
  if (in.gesture_held) return "a gesture is being drawn";
  if (*game != nullptr) return *game;
  if (!in.have_aim) return "the aim pose is not tracked";
  return nullptr;
}

// One line of the log for each change of what the view and the game say to a
// press of A.
void LogGame(const char* game) {
  if (game == g_logged_game) return;
  g_logged_game = game;
  if (g_game_logs >= kMaxGameLogs) return;
  ++g_game_logs;
  if (game == nullptr) {
    ModLog("teleport     : the view and the game let A teleport now "
           "(teleport_switch=%d), frame %llu", g_settings.move_switch, g_frame);
  } else {
    ModLog("teleport     : A does not teleport now: %s (teleport_switch=%d), "
           "frame %llu", game, g_settings.move_switch, g_frame);
  }
}

// Is the land point a valid target? The reason is always set.
bool ValidTarget(Mode mode, float x, float z, float ground, const char** why) {
  if (!OnMap(x, z)) {
    *why = "the target is off the map";
    return false;
  }
  if (!(ground > 0.0f)) {
    *why = "the target is the sea";
    return false;
  }
  const float dx = x - kMapCenter, dz = z - kMapCenter;
  const float from_center = sqrtf(dx * dx + dz * dz);
  if (mode == kGod && from_center > kGodRadius) {
    *why = "the target is outside the 3000 unit limit of the God view";
    return false;
  }
  if (mode == kImmersive && from_center > kImmersiveRadius) {
    *why = "the target is outside the 3400 unit limit of the camera";
    return false;
  }
  *why = "the target is land";
  return true;
}

void BuzzRefused() { HapticsPulse(haptics_math::kRefused); }

void TwoTicks() {
  HapticsPulse(haptics_math::kTick);
  g_second_tick_at = GetTickCount64() + kSecondTickMs;
}

// Smooth the aim of this frame. reset is true on the first frame of an aim.
// The direction is made a unit vector again after the filter.
void SmoothAim(TeleportInput* in, bool reset) {
  LARGE_INTEGER now, frequency;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&frequency);
  float dt = 1.0f / 90.0f;
  if (!reset && g_filter_tick != 0 && frequency.QuadPart > 0) {
    dt = static_cast<float>(now.QuadPart - g_filter_tick) /
         static_cast<float>(frequency.QuadPart);
  }
  g_filter_tick = now.QuadPart;
  if (reset) {
    aim_smooth::Reset(&g_filter_point);
    aim_smooth::Reset(&g_filter_direction);
  }
  const float raw[3] = {in->direction[0], in->direction[1], in->direction[2]};
  float point[3], direction[3];
  aim_smooth::Step(&g_filter_point, in->origin_m, dt,
                   aim_smooth::kPointMinCutoff, aim_smooth::kPointBeta, point);
  aim_smooth::Step(&g_filter_direction, in->direction, dt,
                   aim_smooth::kDirectionMinCutoff,
                   aim_smooth::kDirectionBeta, direction);
  const float length = sqrtf(direction[0] * direction[0] +
                             direction[1] * direction[1] +
                             direction[2] * direction[2]);
  if (!(length > 1e-3f)) return;
  for (int i = 0; i < 3; ++i) {
    in->origin_m[i] = point[i];
    in->direction[i] = direction[i] / length;
  }
  float along = raw[0] * in->direction[0] + raw[1] * in->direction[1] +
                raw[2] * in->direction[2];
  if (along > 1.0f) along = 1.0f;
  if (along < -1.0f) along = -1.0f;
  const float gap = acosf(along) * 57.29578f;
  if (gap > g_smooth_most_deg) g_smooth_most_deg = gap;
}

// THE AIM of one frame: the arc, the hit and the validity. Fills g_aim and
// g_draw.
void Evaluate(const TeleportInput& in, Mode mode) {
  g_frame_calls = 0;
  g_aim.have = false;
  g_aim.valid = false;
  g_aim.why = "no aim";
  g_draw.active = true;
  g_draw.valid = false;
  g_draw.count = 0;
  g_draw.ring = false;
  g_draw.units_per_meter = in.units_per_meter;
  g_draw.ring_cm = teleport_math::RingRadiusCm(
      g_settings.ring_mode, static_cast<float>(g_settings.ring_cm),
      static_cast<float>(g_settings.ring_units),
      static_cast<float>(g_settings.ring_min_cm),
      static_cast<float>(g_settings.ring_max_cm), in.units_per_meter);
  for (int i = 0; i < 3; ++i) {
    g_aim.origin_m[i] = in.origin_m[i];
    g_aim.direction[i] = in.direction[i];
    g_aim.eye[i] = in.eye[i];
  }
  g_aim.units_per_meter = in.units_per_meter;
  g_aim.eye_ground = 0.0f;
  const float* o = in.origin_m;
  const float* d = in.direction;

  if (controller_math::ArcIsSky(d)) {
    g_aim.why = "the aim is the sky, so there is no arc";
    ++g_sky_frames;
    return;
  }
  const auto ground = [](float x, float z) { return Ground(x, z); };
  float hit[3] = {0.0f, 0.0f, 0.0f};
  bool got = false;
  TablePose pose = {};
  float table_room[3] = {0.0f, 0.0f, 0.0f};
  float low = 0.0f, high = 0.0f;
  uint64_t serial = 0;
  const bool have_pose = (mode == kGod || mode == kTable) &&
                         TabletopPose(&pose, table_room, &low, &high, &serial);

  if (mode == kTable) {
    // A straight ray: the table is small, and a ray shows where it points.
    if (!have_pose) {
      g_aim.why = "the table is not known";
      return;
    }
    const float s = in.units_per_meter;
    float room_hit[3];
    got = table_math::RayLand(pose, table_room, o, d,
                              s * 0.02f > 2.0f ? s * 0.02f : 2.0f, 3.0f * s,
                              ground, room_hit);
    float end_room[3];
    for (int i = 0; i < 3; ++i) {
      end_room[i] = got ? room_hit[i] : o[i] + d[i] * 3.0f;
    }
    RoomToWorld(in, o, g_draw.points[0]);
    RoomToWorld(in, end_room, g_draw.points[1]);
    g_draw.count = 2;
    if (got) {
      RoomToWorld(in, room_hit, hit);
      hit[1] = Ground(hit[0], hit[2]);
    }
  } else {
    if (mode == kGod && !have_pose) {
      g_aim.why = "the God view is not known";
      return;
    }
    // The room height of the sea, and the arc ends 0.5 m under it.
    float sea_room;
    if (mode == kGod) {
      sea_room = table_math::GroundInRoom(pose, table_room, 0.0f);
    } else {
      sea_room = in.map[4] != 0.0f ? (0.0f - in.base[1]) / in.map[4] : -2.0f;
    }
    const float speed = controller_math::ArcLaunchSpeed(
        kArcGravity, static_cast<float>(g_settings.reach_m));
    const float t_end = controller_math::ArcEndTime(
        o, d, speed, kArcGravity, sea_room - 0.5f, kArcMaxSeconds);
    if (!(t_end > 0.02f)) {
      g_aim.why = "the hand is below the sea level, so there is no arc";
      ++g_nohit_frames;
      return;
    }
    const int n = controller_math::ArcPoints(o, d, speed, kArcGravity, t_end,
                                             kArcSteps, g_arc_room);
    float world[overlay_math::kArcMaxPoints][3];
    for (int i = 0; i < n; ++i) RoomToWorld(in, g_arc_room[i], world[i]);
    int segment = 0;
    got = controller_math::ArcLandHit(world, n, ground, hit, &segment);
    if (got) {
      const int keep = segment + 1;
      for (int i = 0; i < keep; ++i) {
        for (int k = 0; k < 3; ++k) g_draw.points[i][k] = world[i][k];
      }
      for (int k = 0; k < 3; ++k) g_draw.points[keep][k] = hit[k];
      g_draw.count = keep + 1;
      // A hit at the start, from a hand under the land, has no length.
      if (segment == 0 && world[0][1] <= hit[1]) g_draw.count = 0;
    } else {
      for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 3; ++k) g_draw.points[i][k] = world[i][k];
      }
      g_draw.count = n;
    }
  }

  if (!got) {
    g_aim.why = "the arc meets no land";
    ++g_nohit_frames;
    return;
  }
  g_aim.have = true;
  for (int k = 0; k < 3; ++k) g_aim.target[k] = hit[k];
  g_aim.ground = hit[1];
  g_aim.eye_ground = Ground(in.eye[0], in.eye[2]);
  g_draw.ring = true;
  for (int k = 0; k < 3; ++k) g_draw.target[k] = hit[k];
  const char* why = "";
  bool valid = ValidTarget(mode, hit[0], hit[2], hit[1], &why);
  if (valid) {
    const float dx = hit[0] - in.eye[0], dz = hit[2] - in.eye[2];
    if (sqrtf(dx * dx + dz * dz) < kNearM * in.units_per_meter) {
      valid = false;
      why = "the target is within 30 cm of the point under your head, so "
            "there is nothing to do";
    }
  }
  g_aim.valid = valid;
  g_aim.why = why;
  g_draw.valid = valid;
  if (valid) {
    ++g_valid_frames;
  } else {
    ++g_invalid_frames;
  }
  if (g_frame_calls > g_most_calls) g_most_calls = g_frame_calls;
}

// Log the aim: on the first frame, on a change of the hit or its reason, and
// once a second.
void LogAim(const TeleportInput& in, Mode mode, bool first) {
  const bool changed = first || g_aim.have != g_logged_have ||
                       g_aim.valid != g_logged_valid ||
                       g_aim.why != g_logged_why;
  const bool sample = g_frame >= g_logged_frame + 90;
  if (!changed && !sample) return;
  if (g_aim_logs >= kMaxAimLogs) return;
  ++g_aim_logs;
  g_logged_have = g_aim.have;
  g_logged_valid = g_aim.valid;
  g_logged_why = g_aim.why;
  g_logged_frame = g_frame;
  const float up = asinf(in.direction[1]) * 57.29578f;
  ModLog("teleport     : frame %llu, %s, aim from %.3f %.3f %.3f m in the room, "
         "%.1f degrees up, %d arc points, %.1f units a meter",
         g_frame, kModeName[mode], in.origin_m[0], in.origin_m[1],
         in.origin_m[2], up, g_draw.count, in.units_per_meter);
  if (g_aim.have) {
    ModLog("teleport     : hit land at %.1f %.1f %.1f, %s. %s, %d land "
           "lookups this frame", g_aim.target[0], g_aim.target[1],
           g_aim.target[2], g_aim.why, g_aim.valid ? "Valid (cyan)"
                                                   : "Not valid (red)",
           g_frame_calls);
  } else {
    ModLog("teleport     : no hit: %s", g_aim.why);
  }
}

void CancelAim(const char* why, bool tick) {
  if (!g_aiming) return;
  g_aiming = false;
  g_draw.active = false;
  ++g_cancels;
  if (MiscLog()) ModLog("teleport     : the aim is cancelled: %s, frame %llu",
                        why, g_frame);
  if (tick) TwoTicks();
}

// A on a valid target went up: go there.
void Commit(const TeleportInput& in, Mode mode) {
  const float* t = g_aim.target;
  bool ok = false;
  if (mode == kGod) {
    ok = TabletopGodTeleport(t);
  } else if (mode == kTable) {
    ok = TabletopTableTeleport(t);
  } else if (mode == kImmersive) {
    float delta[3];
    controller_math::CameraTeleportDelta(in.eye, Ground(in.eye[0], in.eye[2]),
                                         t, g_aim.ground, delta);
    ComfortCameraTeleport(delta);
    ok = true;
  }
  if (!ok) {
    if (MiscLog()) {
      ModLog("teleport     : the %s did not take the move", kModeName[mode]);
    }
    return;
  }
  ++g_moves;
  HapticsPulse(haptics_math::kTeleport);
  HintCardLearnDone(hint_learn_math::kSkillTeleport,
                    "A went up, and the teleport moved the player");
  ModLog("teleport     : A is up, so go to %.1f %.1f %.1f (%s), frame %llu. "
         "The eye is at %.1f %.1f %.1f, %.1f units from the target on the "
         "ground", t[0], t[1], t[2], kModeName[mode], g_frame, in.eye[0],
         in.eye[1], in.eye[2],
         sqrtf((t[0] - in.eye[0]) * (t[0] - in.eye[0]) +
               (t[2] - in.eye[2]) * (t[2] - in.eye[2])));
}

// What the size rule of the ring is now, in the log.
void LogRuleMode(const char* why) {
  if (g_settings.ring_mode != 0) {
    ModLog("teleport     : ring size rule 1 (%s): one size in the world, %d "
           "units, between %d and %d cm of the room", why,
           g_settings.ring_units, g_settings.ring_min_cm,
           g_settings.ring_max_cm);
  } else {
    ModLog("teleport     : ring size rule 0 (%s): one size in the room, %d cm",
           why, g_settings.ring_cm);
  }
}

// THE A/B KEY (test_keys=1): Ctrl with Insert changes the size rule of the
// ring between 1 (one size in the world) and 0 (one size in the room). The
// pointing controller gives a long pulse for 1 and a short pulse for 0. The
// key is read once a frame, and only while a window of the game is in front.
void PollRingKey() {
  static bool was = false;
  if (!g_settings.test_keys) return;
  // Shift must be up: Ctrl, Shift and Insert is the key of god_zoom_smooth.
  const bool down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0;
  const bool pressed = down && !was;
  was = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_settings.ring_mode = g_settings.ring_mode != 0 ? 0 : 1;
  ControllersGesturePulse(0.5f, g_settings.ring_mode != 0 ? 0.24f : 0.08f);
  LogRuleMode("Ctrl and Insert");
}

// What the switch rule of the press is now, in the log.
void LogSwitch(const char* why) {
  ModLog("teleport     : teleport_switch=%d (%s). %s", g_settings.move_switch,
         why,
         g_settings.move_switch != 0
             ? "The press of A keeps to the game's switch for the drag of the "
               "land (bit 8), as the walk with the stick does"
             : "The press of A keeps to the game's switch for the double "
               "click jump (bit 0x10)");
}

// THE A/B KEY of teleport_switch (test_keys=1): Ctrl and Shift with the comma
// key change the switch between 1 and 0. The pointing controller gives a long
// pulse for 1 and a short pulse for 0. The key is read once a frame, and only
// while a window of the game is in front.
void PollSwitchKey() {
  static bool was = false;
  if (!g_settings.test_keys) return;
  const bool down = (GetAsyncKeyState(VK_OEM_COMMA) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool pressed = down && !was;
  was = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  TeleportSwitchToggle("Ctrl, Shift and the comma key");
}

}  // namespace

void TeleportSwitchToggle(const char* by) {
  if (!g_installed || !g_settings.test_keys) return;
  g_settings.move_switch = g_settings.move_switch != 0 ? 0 : 1;
  ControllersGesturePulse(0.5f, g_settings.move_switch != 0 ? 0.30f : 0.08f);
  LogSwitch(by);
}

void TeleportSetOptions(const TeleportSettings& settings) {
  g_settings = settings;
  if (g_settings.reach_m < 2) g_settings.reach_m = 2;
  if (g_settings.reach_m > 100) g_settings.reach_m = 100;
  if (g_settings.ring_cm < 5) g_settings.ring_cm = 5;
  if (g_settings.ring_cm > 60) g_settings.ring_cm = 60;
  g_settings.ring_mode = g_settings.ring_mode != 0 ? 1 : 0;
  if (g_settings.ring_units < 1) g_settings.ring_units = 1;
  if (g_settings.ring_units > 1000) g_settings.ring_units = 1000;
  if (g_settings.ring_min_cm < 1) g_settings.ring_min_cm = 1;
  if (g_settings.ring_max_cm < g_settings.ring_min_cm) {
    g_settings.ring_max_cm = g_settings.ring_min_cm;
  }
  g_settings.move_switch = g_settings.move_switch != 0 ? 1 : 0;
  g_installed = true;
  if (g_settings.on) {
    ModLog("teleport     : on. Hold A to aim an arc with a reach of %d m, and "
           "let go of A to go to the ring. B cancels. The mouse double "
           "click in the God view goes too.", g_settings.reach_m);
    LogRuleMode("start");
    LogSwitch("start");
    if (g_settings.test_keys) {
      ModLog("keys         : Ctrl with Insert changes the size rule of the "
             "ring between the world and the room (test_keys=1)");
      ModLog("keys         : Ctrl and Shift with the comma key change the "
             "switch of the game that the teleport keeps to (test_keys=1)");
    }
  } else {
    ModLog("teleport     : off. A makes the game's jump. The mouse double "
           "click in the God view still teleports.");
  }
}

bool TeleportOwnsA() { return g_installed && g_settings.on &&
                              HeadCameraEnabled(); }

float TeleportGround(float x, float z) { return Ground(x, z); }

int TeleportJumpEvent() {
  void* const* game = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(game, sizeof(void*)) || *game == nullptr) return 0x305;
  const MyInterfaceFn my_interface =
      reinterpret_cast<MyInterfaceFn>(kMyInterface);
  const uint8_t* face = static_cast<const uint8_t*>(my_interface(*game, nullptr));
  if (face == nullptr || IsBadReadPtr(face + kFaceObject, sizeof(void*))) {
    return 0x305;
  }
  return *reinterpret_cast<void* const*>(face + kFaceObject) != nullptr
             ? 0x306
             : 0x305;
}

// For the grab from a distance: the player aims the teleport now.
bool TeleportAiming() { return g_installed && g_aiming; }

bool TeleportPossible() {
  if (!g_installed) return false;
  // When A is not the teleport, it is the game's jump, and the game refuses
  // that while its jump switch is off.
  if (!TeleportOwnsA()) return JumpSwitchOn();
  return g_possible;
}

bool TeleportDrawState(TeleportDraw* out) {
  if (!g_installed || !g_draw.active || !g_aiming) return false;
  *out = g_draw;
  return true;
}

void TeleportCountDraw(int eye_passes, int triangles) {
  g_draw_passes += static_cast<uint64_t>(eye_passes);
  g_draw_triangles += static_cast<uint64_t>(triangles);
}

void TeleportFrame() {
  g_draw.active = false;
  if (!g_installed || !g_settings.on) return;
  ++g_frame;
  if (g_second_tick_at != 0 && GetTickCount64() >= g_second_tick_at) {
    g_second_tick_at = 0;
    HapticsPulse(haptics_math::kTick);
  }
  PollRingKey();
  PollSwitchKey();
  TeleportInput in;
  if (!ControllersTeleportInput(&in)) {
    g_possible = false;
    if (g_aiming) CancelAim("the controllers are off", false);
    return;
  }
  Mode mode = kNone;
  const char* game = nullptr;
  const char* refused = Refuse(in, &mode, &game);
  g_possible = refused == nullptr;
  LogGame(game);

  if (g_aiming) {
    if (!in.a_down) {
      // The release. The target is the one of the last frame that was drawn.
      g_aiming = false;
      g_draw.active = false;
      if (refused != nullptr) {
        ++g_cancels;
        ModLog("teleport     : A is up, but the teleport stops: %s, frame "
               "%llu", refused, g_frame);
        TwoTicks();
      } else if (mode != g_mode) {
        ++g_cancels;
        ModLog("teleport     : A is up, but the view changed from %s to %s, "
               "so nothing happens, frame %llu", kModeName[g_mode],
               kModeName[mode], g_frame);
        TwoTicks();
      } else if (g_aim.have && g_aim.valid) {
        Commit(in, mode);
      } else {
        ++g_released_invalid;
        ModLog("teleport     : A is up on %s: %s, so nothing happens, frame "
               "%llu", g_aim.have ? "a red target" : "no target", g_aim.why,
               g_frame);
        TwoTicks();
      }
      return;
    }
    if (in.b_pressed) {
      CancelAim("B was pressed, and it starts no gesture", true);
      return;
    }
    if (refused != nullptr) {
      CancelAim(refused, true);
      return;
    }
    ++g_aim_frames;
    SmoothAim(&in, false);
    Evaluate(in, mode);
    LogAim(in, mode, false);
    return;
  }

  if (!in.a_pressed) return;
  if (refused != nullptr) {
    ++g_refused;
    if (MiscLog()) {
      ModLog("teleport     : A is refused: %s, frame %llu", refused, g_frame);
    }
    BuzzRefused();
    return;
  }
  g_aiming = true;
  g_mode = mode;
  ++g_aims;
  ++g_aim_frames;
  g_logged_frame = 0;
  SmoothAim(&in, true);
  Evaluate(in, mode);
  LogAim(in, mode, true);
  ModLog("teleport     : the ring has a radius of %.1f cm of the room and %.1f "
         "units of the world, at %.1f units a meter, rule %d",
         g_draw.ring_cm,
         teleport_math::RingRadiusUnits(g_draw.ring_cm, in.units_per_meter),
         in.units_per_meter, g_settings.ring_mode);
}

bool TeleportMouseDoubleClick(void* mode_ptr) {
  if (!g_installed) return false;
  uint8_t* latch = reinterpret_cast<uint8_t*>(kLatch);
  if ((*latch & kLatchDouble) == 0) return false;
  if (!TabletopGodOwnsView()) return false;
  if (mode_ptr == nullptr ||
      IsBadReadPtr(mode_ptr, kModeReadSize)) {
    return false;
  }
  // The game decides with the same switches, and its jump switch is one of
  // them. When one is off the game keeps the click, so it does not move the
  // view.
  const char* why = GameRefuses(true);
  if (why != nullptr) {
    ++g_mouse_refused;
    if (MiscLog()) {
      ModLog("teleport     : the mouse double click is left to the game: %s",
             why);
    }
    return false;
  }
  float dir[3];
  if (!PointerMapMouseRay(dir)) return false;
  // The double click is ours now, so the game does not fly its camera.
  *latch = static_cast<uint8_t>(*latch & ~kLatchDouble);

  TablePose pose = {};
  float table_room[3], low, high;
  uint64_t serial;
  float head[9], lean[3];
  const float life = HeadPoseScale();
  if (!TabletopPose(&pose, table_room, &low, &high, &serial) ||
      !HeadPoseCurrent(head, lean) || !(life > 0.0f)) {
    return true;
  }
  const float head_room[3] = {lean[0] / life, lean[1] / life, lean[2] / life};
  g_frame_calls = 0;
  const auto ground = [](float x, float z) { return Ground(x, z); };
  float room_hit[3];
  if (!table_math::RayLand(pose, table_room, head_room, dir, 2.0f, 5600.0f,
                           ground, room_hit)) {
    ++g_mouse_refused;
    ModLog("teleport     : the mouse double click meets no land, so nothing "
           "happens");
    return true;
  }
  float target[3];
  table_math::PoseWorld(pose, table_room, room_hit, target);
  target[1] = Ground(target[0], target[2]);
  const char* reason = "";
  if (!ValidTarget(kGod, target[0], target[2], target[1], &reason)) {
    ++g_mouse_refused;
    ModLog("teleport     : the mouse double click is not valid: %s, at %.1f "
           "%.1f", reason, target[0], target[2]);
    return true;
  }
  if (!TabletopGodTeleport(target)) return true;
  ++g_mouse_moves;
  ModLog("teleport     : the mouse double click goes to %.1f %.1f %.1f, %s",
         target[0], target[1], target[2], reason);
  return true;
}

void TeleportReport() {
  if (!g_installed) return;
  const uint64_t applied = TabletopTakeTeleportMoves();
  if (g_aims == 0 && g_refused == 0 && g_mouse_moves == 0 &&
      g_mouse_refused == 0 && g_moves == 0 && applied == 0 &&
      g_draw_passes == 0) {
    return;
  }
  ModLog("teleport     : %llu aims over %llu frames (%llu on a valid target, "
         "%llu on a red one, %llu with no hit, %llu in the sky). %llu "
         "releases went, %llu were red or without a target, %llu cancels, %llu "
         "presses refused. The view applied %llu moves. The mouse double click "
         "went %llu times and was refused %llu times.", g_aims, g_aim_frames,
         g_valid_frames, g_invalid_frames, g_nohit_frames, g_sky_frames,
         g_moves, g_released_invalid, g_cancels, g_refused, applied,
         g_mouse_moves, g_mouse_refused);
  ModLog("teleport     : %llu land lookups, at most %d in one frame. The arc "
         "was drawn in %llu eye passes with %llu triangles. The smooth aim "
         "was at most %.2f degrees from the raw aim.", g_ground_calls,
         g_most_calls, g_draw_passes, g_draw_triangles, g_smooth_most_deg);
  g_smooth_most_deg = 0.0f;
  g_aims = g_aim_frames = g_valid_frames = g_invalid_frames = 0;
  g_sky_frames = g_nohit_frames = g_refused = g_moves = 0;
  g_released_invalid = g_cancels = g_mouse_moves = g_mouse_refused = 0;
  g_ground_calls = 0;
  g_most_calls = 0;
  g_draw_passes = g_draw_triangles = 0;
}
