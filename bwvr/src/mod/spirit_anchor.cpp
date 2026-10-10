// Keep the advisors in one place in the room when the player turns their
// head.
//
// The advisors are the two spirits that talk to the player, the good one and
// the evil one. Each is a HelpDude, and one HelpDudeControl runs both. They
// are real 3D meshes, but the game places them by screen coordinates. In each
// eye pass, HelpDudeControl::Update (0x005C3A30) puts each spirit at a screen
// point and a depth. The screen points are x = +0.66 and x = -0.66. Update
// uses the camera that the engine built, and it also copies the axes of that
// camera into the spirit's own matrix.
//
// WHY THEY WOULD STICK TO THE FACE. The engine's camera carries the head (see
// head_camera.cpp). So the game puts the spirits at the same place in the view
// in every pass. They would turn with the head, and roll with it too.
//
// THE ROOM CAMERA. This file gives the spirit code, and only the spirit code,
// a camera that the head does not move:
//
//   position   the game camera from the main UpdateCamera call
//   forward    a latched head yaw and pitch, in the room frame (world up and
//              the game camera heading), with the pitch in -35..+20 degrees
//   up         world up, so the camera has no roll
//
// Three hooks swap it in: HelpDudeControl::Update, and the two screen helpers
// that the spirit code and the script natives call, HelpDude::ScreenToWorld
// (0x005BD2A0) and HelpDude::WorldToScreen (0x005BD390). The swap writes
// three globals, and it puts back the saved bytes before the hook returns:
//
//   0x00EA1D28  the unscaled world to camera matrix, 48 bytes
//   0x00EA9E40  the scaled world to camera matrix, 48 bytes
//   0x00EA1DB8  the stored camera position, 12 bytes
//
// A walk of the call graph below the three functions found no draw and no
// write to these globals. The spirit mesh is still drawn through the real eye
// matrices. So both eyes see each spirit at one point in the world, with true
// depth.
//
// THE LIGHT. HelpDude::Draw (0x005C0700) places the spirits' light itself, at
// a point behind and beside the camera, with a direct call to
// UnprojectScreenPoint at 0x005C0747. That call is outside the three hooks,
// so the light would turn with the head while the spirits stay still, and
// their shading would change as the player looks around. The call is sent through
// SpiritLight, which makes the same call with the room camera swapped in.
//
// ON THE TABLE (docs/tabletop.md, "The spirits beside the table"). The world is s units per
// meter, not 7.5. The room camera then stands at the recenter pose over the
// table (P0), with the table's basis, and the latched pitch stays between 0
// and 15 degrees, so the room camera does not look down at the table. The
// spirits are at the screen points of the game in that camera: beside the
// table and below eye level. The spirits' depth range and their size are
// multiplied by s / 7.5 for as long as the table owns the view, so they
// float 1 to 1.5 m away in the room at the size they have off the table. The
// light's depth gets the same factor. A grab of the table moves the land,
// not the room, so it does not move the spirits.
//
// AT A PLACE OF THE WORLD. A script can send a spirit to a place of the
// world, for example to point at a scroll. HelpDude::Update1 (0x005BDDA0)
// then moves the spirit from its usual place to that place by an amount from
// 0 to 1 (+0x35DC), and it makes the mesh larger by 1 + 3 e, where e is the
// ease of the amount: 4 times the usual size at the place. The 3 is the
// constant at 0x008C2C50, in the fmul at 0x005BE947. With the size of the
// table (s / 7.5) that is a spirit of 73 cm in the room, on the land at the
// waist. So while the table owns the view, that fmul reads a number of the
// mod (spirit_world_size=1): the spirit at the place has the angle of size
// that it has at its near depth, seen from the room camera. It is not larger
// than the game makes it, and not smaller than a quarter of its usual size
// (spirit_size_math.h). The mod sets the number for each spirit in the hook
// of ScreenToWorld, at the call of Update1 that gives the usual place
// (return address 0x005BE6D7), a few instructions before the fmul. Off the
// table, and with spirit_world_size=0, the number is the 3 of the game. The
// distance from the place to the side and the height above it (+0x3674,
// +0x3678) stay in units of the world.
//
// NOT DONE. When a spirit looks at the Hand, the game unprojects the pointer
// through the room camera, but the pointer is a pixel of the live view. With
// the head turned away from the anchor, a spirit then looks a little beside
// the Hand.
//
// ONE DECISION A FRAME. Update runs once per eye pass. The anchor changes only
// at the first Update after a new main camera call (HeadCameraMainSerial). The
// room camera is built there, once, so both eyes get the same bytes.
//
// WHEN THE ANCHOR MOVES. The spirits land again around the current gaze after
// a cut: a snap turn, an instant jump, a recenter, or a cut of the script
// camera (see SpiritAnchorCameraCut). They also land again after they are
// away for more than 500 ms, and when the game camera jumps between two
// frames. Between those events, the head does not move them. While the
// spirits are away, the anchor follows the head, so they come into view where
// the player looks. Nobody sees that motion, because nothing is drawn.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "spirit_size_math.h"

extern void ModLog(const char* format, ...);
// head_camera.cpp: the room, the game camera and the main call count.
bool HeadCameraStageBasis(float basis_out[9]);
bool HeadCameraGameCamera(float position[3], float focus[3]);
uint64_t HeadCameraMainSerial();
bool HeadCameraEnabled();
bool HeadCameraRenderBasis(float basis_out[9], float position_out[3]);
// head_pose.cpp
bool HeadPoseCurrent(float matrix_out[9], float translation_out[3]);
// comfort_camera.cpp: where the game's own camera of the player goes.
bool ComfortCameraDestinations(float position[3], float focus[3]);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();
// stereo.cpp: true while the two eye passes run.
bool StereoInPass();
// tabletop.cpp: the room of the table in this frame.
bool TabletopRoom(float origin[3], float basis[9], float center[3],
                  float* gain);

namespace {

// HelpDudeControl::Update(float dt, float balance), thiscall, ret 8. It has
// one caller, HelpSystem::Draw3D at 0x005C5AEE, so it runs once per eye pass.
const uintptr_t kControlUpdate = 0x005C3A30;
// HelpDude::ScreenToWorld(float* out, float sx, float sy, float bias,
// char at_near), thiscall, ret 0x14, returns out. All eight callers are in
// the spirit code, and HelpDude::Draw is one of them.
const uintptr_t kScreenToWorld = 0x005BD2A0;
// HelpDude::WorldToScreen(const LHPoint* point, float* nx, float* ny,
// int allow), ret 0x10, returns 0 or 1. It gets ecx but does not use it. All
// seven callers are in the spirit code.
const uintptr_t kWorldToScreen = 0x005BD390;
// The light's call in HelpDude::Draw: E8 and a rel32 to UnprojectScreenPoint
// 0x0081B370, which is fastcall (the pixel in ecx, the output in edx, one
// stack float, ret 4).
const uintptr_t kLightCall = 0x005C0747;
const uintptr_t kUnproject = 0x0081B370;
const uint8_t kLightCallBytes[5] = {0xE8, 0x24, 0xAC, 0x25, 0x00};
// The scale of a spirit at a place of the world, in HelpDude::Update1: fmul
// dword ptr [0x008C2C50], which is the 3 of 1 + 3 e. Many other instructions
// read that constant, so the mod changes the address in this one fmul.
const uintptr_t kGrowSite = 0x005BE947;
const uintptr_t kGrowConstant = 0x008C2C50;
const uint8_t kGrowBytes[6] = {0xD8, 0x0D, 0x50, 0x2C, 0x8C, 0x00};
// The return address of the call of ScreenToWorld in Update1 that gives the
// usual place of the spirit. The code from there to the fmul has no jump in
// from a different place.
const uintptr_t kUsualPlaceReturn = 0x005BE6D7;

// HelpDudeControl fields. Spirit 0 comes first, and spirit 1 is four bytes
// after it.
const int kControlSpirit = 0x04;  // HelpDude*
const int kControlState = 0x0C;   // 0 means the spirit is not drawn
// What a script lets a spirit point at (fn_005C3960, from the script words
// SPIRIT_POINT_POS and SPIRIT_POINT_GAME_THING): 1 is a place of the world.
// The byte is the last argument of the script word, "in world": with 1 the
// spirit goes to the place, and with 0 it points from its place on the
// screen.
const int kControlInWorld = 0x2C;  // one byte for each spirit
const int kControlPoints = 0x4C;   // one int for each spirit
const size_t kControlPointBytes =
    kControlPoints + sizeof(int32_t) * 2 - kControlInWorld;
const int kSpiritCount = 2;
const size_t kControlBytes = kControlState + sizeof(int32_t) * kSpiritCount;

// HelpDude fields.
const int kSpiritModel = 0x10;       // a size of the model, from the data file
const int kSpiritMatrix = 0x3350;    // world matrix, 12 floats
const int kSpiritPosition = 0x3374;  // the translation of that matrix
const int kSpiritState = 0x3490;     // with 8 set, it points at a place
const int kSpiritSize = 0x35B4;      // the size of the mesh and the glow
const int kSpiritNear = 0x35B8;      // the depth range, from the data file
const int kSpiritFar = 0x35BC;
const size_t kSpiritBytes = kSpiritFar + sizeof(float) - kSpiritMatrix;
// The spirit at a place of the world. The amount goes to the goal while the
// state has 8 set, and to 0 without it, by the time of the frame.
const int kSpiritAmount = 0x35DC;   // 0 at the usual place, 1 at the place
const int kSpiritPlaceX = 0x35E0;   // the place: three Zoomers, 0x30 apart
const int kSpiritPlaceY = 0x3610;
const int kSpiritPlaceZ = 0x3640;
const int kSpiritGoal = 0x3670;     // 1 when the spirit goes to the place
const int kSpiritAside = 0x3674;    // the spirit stays this far to the left
const int kSpiritAbove = 0x3678;    // of the place, and this far above it
const int kSpiritBalance = 0x37E4;  // about 0.8, a factor on the size
const size_t kWorldBytes = kSpiritBalance + sizeof(float) - kSpiritMatrix;
const int32_t kStatePoints = 8;
// The three that the table scales: the size, then the depth range.
const int kScaledCount = 3;

const size_t kMatrixFloats = 12;
const size_t kMatrixBytes = sizeof(float) * kMatrixFloats;
const size_t kPointBytes = sizeof(float) * 3;

const float kDegrees = 57.29578f;
// A latch while the player looks far down would put the spirits near the
// land, and a latch while they look up would put them in the sky.
const float kPitchLowest = -35.0f / kDegrees;
const float kPitchHighest = 20.0f / kDegrees;
// On the table the player looks down at the table, and a latch there would
// stand the spirits on its edge. So the room camera looks level or up there.
const float kTablePitchLowest = 0.0f;
const float kTablePitchHighest = 15.0f / kDegrees;
// A gap between two lines of speech is shorter than this. A spirit that is
// away for longer has left.
const ULONGLONG kAwayMs = 500;
// A frame with no main camera call keeps the last room camera. After this
// long, the recorded camera is too old to use.
const ULONGLONG kStaleMs = 250;
const ULONGLONG kPositionLogMs = 1000;
// The safety net. A drag of the land moves the camera less than this in one
// frame, and a snap turn turns it more.
const float kJumpUnits = 8.0f;
const float kJumpShare = 0.10f;  // of the distance from camera to focus
const float kTurnLimit = 20.0f / kDegrees;

typedef void(__fastcall* ControlUpdateFn)(void* control, void* unused,
                                          float dt, float balance);
typedef float*(__fastcall* ScreenToWorldFn)(void* spirit, void* unused,
                                            float* out, float sx, float sy,
                                            float bias, int at_near);
typedef int(__fastcall* WorldToScreenFn)(void* self, void* unused,
                                         const float* point, float* nx,
                                         float* ny, int allow);

ControlUpdateFn o_ControlUpdate = nullptr;
ScreenToWorldFn o_ScreenToWorld = nullptr;
WorldToScreenFn o_WorldToScreen = nullptr;

bool g_installed = false;
bool g_hooks_ok = false;
bool g_globals_ok = false;
bool g_enabled = true;

// The decision of the frame, keyed on the main camera call.
bool g_have_decided = false;
uint64_t g_decided_serial = 0;
ULONGLONG g_decided_tick = 0;
int g_pass = 0;

// The anchor: a head yaw and pitch in the room frame. g_latched means it
// holds still. Without it, the anchor follows the head.
bool g_latched = false;
bool g_have_angles = false;
float g_yaw = 0.0f;
float g_pitch = 0.0f;
volatile LONG g_cut_serial = 0;
LONG g_latched_cut = 0;
ULONGLONG g_last_seen = 0;

// The game camera at the last decision, for the safety net.
bool g_have_previous = false;
float g_previous_position[3] = {0.0f, 0.0f, 0.0f};
float g_previous_focus[3] = {0.0f, 0.0f, 0.0f};
float g_previous_heading[2] = {0.0f, 1.0f};

// The room camera of the frame.
bool g_room_ok = false;
float g_room_unscaled[kMatrixFloats];
float g_room_scaled[kMatrixFloats];
float g_room_position[3];

// THE TABLE. Whether the table owns this frame, and s / 7.5, 1 off the
// table. The spirits' fields that the table scales keep the game's own
// values, as tabletop.cpp does with the values it changes: a value that the
// game writes while the table is on becomes the one to put back.
bool g_table = false;
float g_gain = 1.0f;
struct ScaledSpirit {
  uint8_t* spirit;
  float original[kScaledCount];
  float written[kScaledCount];
  bool have;
};
ScaledSpirit g_scaled[kSpiritCount] = {};
bool g_said_table = false;

// THE SIZE AT A PLACE OF THE WORLD. g_grow is the number that the game's
// fmul reads in place of its 3. The factor of a spirit is decided once for
// each frame, so both eyes get one size.
bool g_world_size = true;
bool g_world_patched = false;
bool g_world_keys = false;
int g_world_test_frame = 0;
bool g_world_test_done = false;
bool g_world_test_point = false;
bool g_world_test_point_said = false;
float g_grow = spirit_size_math::kGameGrow;
struct WorldSpirit {
  bool have;
  uint64_t serial;
  float factor;
  int phase;
};
WorldSpirit g_world[kSpiritCount] = {};
bool g_world_log_all = false;
int g_world_logged = 0;
const int kWorldLogMost = 200;

// The swap. The depth lets a helper hook run inside the Update hook without
// a second swap.
int g_swap_depth = 0;
int g_update_depth = 0;
bool g_update_swapped = false;
float g_saved_unscaled[kMatrixFloats];
float g_saved_scaled[kMatrixFloats];
float g_saved_position[3];

enum LatchReason { kLatchNone, kLatchArrived, kLatchCut, kLatchJumped,
                   kLatchTurned, kLatchReasons };
enum ReleaseReason { kReleaseAway, kReleaseIdle, kReleaseNoHead,
                     kReleaseReasons };
const char* const kLatchText[kLatchReasons] = {
    "", "the spirits are in view", "the camera cut", "the camera jumped",
    "the camera turned"};
const char* const kReleaseText[kReleaseReasons] = {
    "the spirits were away for more than 500 ms", "stereo is off",
    "the head camera stopped"};

// Counts for one report window.
uint64_t g_latches[kLatchReasons] = {0, 0, 0, 0, 0};
uint64_t g_releases[kReleaseReasons] = {0, 0, 0};
uint64_t g_swaps = 0;
bool g_seen = false;
int g_logged = 0;

// The world positions of the spirits, once a second, for each pass.
bool g_log_armed = false;
uint64_t g_log_serial = 0;
ULONGLONG g_last_position_log = 0;
bool g_have_depth = false;
float g_depth_near = 0.0f;
float g_depth_far = 0.0f;

float* Unscaled() {
  return reinterpret_cast<float*>(bw::tech::kWorldToCameraUnscaled);
}
float* Scaled() {
  return reinterpret_cast<float*>(bw::tech::kWorldToCamera);
}
float* StoredPosition() {
  return reinterpret_cast<float*>(bw::tech::kCameraPos);
}

bool Finite(float v) { return v == v && v < 1e30f && v > -1e30f; }

bool FinitePoint(const float p[3]) {
  return Finite(p[0]) && Finite(p[1]) && Finite(p[2]);
}

// THE SWAP.
//
// The outer call saves the three blocks and writes the room camera. A nested
// call only counts. The last call out puts back the exact saved bytes, so
// the eye pass continues with its own camera.
bool SwapIn() {
  if (g_swap_depth > 0) {
    ++g_swap_depth;
    return true;
  }
  if (!g_enabled || !g_room_ok || !g_hooks_ok || !g_globals_ok) return false;
  memcpy(g_saved_unscaled, Unscaled(), kMatrixBytes);
  memcpy(g_saved_scaled, Scaled(), kMatrixBytes);
  memcpy(g_saved_position, StoredPosition(), kPointBytes);
  memcpy(Unscaled(), g_room_unscaled, kMatrixBytes);
  memcpy(Scaled(), g_room_scaled, kMatrixBytes);
  memcpy(StoredPosition(), g_room_position, kPointBytes);
  g_swap_depth = 1;
  ++g_swaps;
  return true;
}

void SwapOut() {
  if (g_swap_depth <= 0) return;
  if (--g_swap_depth > 0) return;
  memcpy(Unscaled(), g_saved_unscaled, kMatrixBytes);
  memcpy(Scaled(), g_saved_scaled, kMatrixBytes);
  memcpy(StoredPosition(), g_saved_position, kPointBytes);
}

// A helper that runs inside Update follows the choice of Update. When Update
// did not swap, the helper must not swap alone, or one placement would mix
// two cameras.
bool HelperMaySwap() { return g_update_depth == 0 || g_update_swapped; }

// The head's forward, in the room frame, as a yaw and a pitch. H maps head
// space to room space, and its column 2 is the head's forward. A roll does
// not change the forward, so the yaw is exact at any roll.
bool HeadAngles(const float h[9], float* yaw, float* pitch) {
  const float fx = h[2], fy = h[5], fz = h[8];
  if (fx * fx + fz * fz > 0.0025f) {
    *yaw = atan2f(fx, fz);
  } else {
    // The head looks almost straight down or up, so the forward has no
    // useful heading. The head's up then points along the facing (looking
    // down) or against it (looking up).
    const float sign = fy >= 0.0f ? 1.0f : -1.0f;
    const float ux = -sign * h[1];
    const float uz = -sign * h[7];
    if (ux * ux + uz * uz < 1e-6f) return false;
    *yaw = atan2f(ux, uz);
  }
  float y = fy;
  if (y > 1.0f) y = 1.0f;
  if (y < -1.0f) y = -1.0f;
  *pitch = asinf(y);
  return Finite(*yaw) && Finite(*pitch);
}

float LimitPitch(float pitch) {
  const float lowest = g_table ? kTablePitchLowest : kPitchLowest;
  const float highest = g_table ? kTablePitchHighest : kPitchHighest;
  if (pitch < lowest) return lowest;
  if (pitch > highest) return highest;
  return pitch;
}

// THE SPIRITS AT THE TABLE SCALE. Once a frame, before the game places them.
// The size (+0x35B4) sizes the mesh in HelpDude::Update1 and the glow in
// HelpDude::Draw, and the depth range (+0x35B8, +0x35BC) places them in
// HelpDude::ScreenToWorld. The other readers use only the ratio of the size
// to the near depth, which the factor does not change. Off the table the
// game's values go back.
void ScaleSpirits(const uint8_t* control, bool table, float gain) {
  for (int i = 0; i < kSpiritCount; ++i) {
    ScaledSpirit& s = g_scaled[i];
    uint8_t* spirit = *reinterpret_cast<uint8_t* const*>(
        control + kControlSpirit + sizeof(void*) * i);
    if (spirit != s.spirit) {
      // Another spirit, or none. The old one may be gone, so it is left.
      s.spirit = spirit;
      s.have = false;
    }
    // Off the table with nothing to put back, the spirit is not touched.
    if (!table && !s.have) continue;
    if (spirit == nullptr ||
        IsBadWritePtr(spirit + kSpiritSize, sizeof(float) * kScaledCount)) {
      continue;
    }
    float* field = reinterpret_cast<float*>(spirit + kSpiritSize);
    if (table) {
      for (int k = 0; k < kScaledCount; ++k) {
        if (!s.have || field[k] != s.written[k]) s.original[k] = field[k];
        field[k] = s.original[k] * gain;
        s.written[k] = field[k];
      }
      s.have = true;
    } else if (s.have) {
      for (int k = 0; k < kScaledCount; ++k) {
        if (field[k] == s.written[k]) field[k] = s.original[k];
      }
      s.have = false;
    }
  }
}

// BUILD THE ROOM CAMERA, the way the engine builds its own.
//
// The unscaled matrix has the camera axes in its columns (right, up and
// forward, in world space), and the translation in row 3, so that
//     p_camera = p_world * R + t,  with t = -position * R.
// The scaled matrix is the same with column 0 times inv_half_tan_fov_x and
// column 1 times inv_half_tan_fov_y. LH3DTech::UpdateCamera does exactly this
// at 0x00819AF8 to 0x00819BAB, and EyeCameraSetPose does the same.
bool BuildRoomCamera(const float stage[9], const float position[3]) {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return false;
  const float factor[3] = {info->inv_half_tan_fov_x, info->inv_half_tan_fov_y,
                           1.0f};
  if (!(factor[0] > 0.0f) || !(factor[1] > 0.0f)) return false;
  if (!FinitePoint(position)) return false;

  // The latched forward in the room frame, then in the world.
  const float level = cosf(g_pitch);
  const float room[3] = {sinf(g_yaw) * level, sinf(g_pitch),
                         cosf(g_yaw) * level};
  float forward[3];
  for (int i = 0; i < 3; ++i) {
    forward[i] = stage[i * 3 + 0] * room[0] + stage[i * 3 + 1] * room[1] +
                 stage[i * 3 + 2] * room[2];
  }
  const float length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] +
                             forward[2] * forward[2]);
  if (!(length > 0.5f) || !Finite(length)) return false;
  for (int i = 0; i < 3; ++i) forward[i] /= length;

  // Right is world up crossed with forward, and up is forward crossed with
  // right. This is the engine's own handedness, and the result has no roll.
  float right[3] = {forward[2], 0.0f, -forward[0]};
  const float across = sqrtf(right[0] * right[0] + right[2] * right[2]);
  if (!(across > 0.1f)) return false;
  right[0] /= across;
  right[2] /= across;
  const float up[3] = {forward[1] * right[2] - forward[2] * right[1],
                       forward[2] * right[0] - forward[0] * right[2],
                       forward[0] * right[1] - forward[1] * right[0]};

  float* u = g_room_unscaled;
  for (int i = 0; i < 3; ++i) {
    u[i * 3 + 0] = right[i];
    u[i * 3 + 1] = up[i];
    u[i * 3 + 2] = forward[i];
  }
  for (int c = 0; c < 3; ++c) {
    u[9 + c] = -(position[0] * u[0 * 3 + c] + position[1] * u[1 * 3 + c] +
                 position[2] * u[2 * 3 + c]);
  }
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 3; ++col) {
      g_room_scaled[row * 3 + col] = u[row * 3 + col] * factor[col];
    }
  }
  for (int i = 0; i < 3; ++i) g_room_position[i] = position[i];
  for (size_t i = 0; i < kMatrixFloats; ++i) {
    if (!Finite(g_room_scaled[i])) return false;
  }
  return true;
}

void Latch(float yaw, float pitch, LatchReason reason, ULONGLONG now) {
  g_yaw = yaw;
  g_pitch = LimitPitch(pitch);
  g_have_angles = true;
  g_latched = true;
  g_latched_cut = g_cut_serial;
  g_last_seen = now;
  ++g_latches[reason];
  if (g_logged < 24) {
    ++g_logged;
    ModLog("advisors     : anchor set at yaw %.1f pitch %.1f degrees, because "
           "%s", g_yaw * kDegrees, g_pitch * kDegrees, kLatchText[reason]);
  }
}

void Release(ReleaseReason reason) {
  if (!g_latched) return;
  g_latched = false;
  ++g_releases[reason];
  if (g_logged < 24) {
    ++g_logged;
    ModLog("advisors     : anchor released, %s", kReleaseText[reason]);
  }
}

// Stop the swap until the next good decision.
void Drop(ReleaseReason reason) {
  Release(reason);
  g_have_angles = false;
  g_room_ok = false;
}

// THE SAFETY NET, for a cut that nobody reported. It compares the game camera
// with the camera at the last decision.
//
// A jump moves the focus, or moves the camera to the side of its view. A zoom
// step of comfort_camera.cpp moves the camera along its view in one frame and
// keeps the focus. That is not a cut, so motion along the view does not
// count. The spirits move with the camera position anyway.
void CheckCamera(const float stage[9], const float position[3],
                 const float focus[3], bool* jumped, bool* turned) {
  *jumped = false;
  *turned = false;
  const float heading[2] = {stage[2], stage[8]};
  if (g_have_previous) {
    float view[3], moved[3];
    float reach = 0.0f, focus_moved = 0.0f, along = 0.0f;
    for (int i = 0; i < 3; ++i) {
      view[i] = g_previous_focus[i] - g_previous_position[i];
      moved[i] = position[i] - g_previous_position[i];
      const float f = focus[i] - position[i];
      const float g = focus[i] - g_previous_focus[i];
      reach += f * f;
      focus_moved += g * g;
    }
    const float view_length =
        sqrtf(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
    if (view_length > 1e-3f) {
      for (int i = 0; i < 3; ++i) view[i] /= view_length;
      along = moved[0] * view[0] + moved[1] * view[1] + moved[2] * view[2];
    }
    float aside = 0.0f;
    for (int i = 0; i < 3; ++i) {
      const float s = moved[i] - view[i] * along;
      aside += s * s;
    }
    float limit = kJumpShare * sqrtf(reach);
    if (!(limit > kJumpUnits)) limit = kJumpUnits;
    *jumped = sqrtf(focus_moved) > limit || sqrtf(aside) > limit;
    const float cross = g_previous_heading[0] * heading[1] -
                        g_previous_heading[1] * heading[0];
    const float dot = g_previous_heading[0] * heading[0] +
                      g_previous_heading[1] * heading[1];
    *turned = fabsf(atan2f(cross, dot)) > kTurnLimit;
  }
  for (int i = 0; i < 3; ++i) {
    g_previous_position[i] = position[i];
    g_previous_focus[i] = focus[i];
  }
  g_previous_heading[0] = heading[0];
  g_previous_heading[1] = heading[1];
  g_have_previous = true;
}

// THE DECISION OF ONE FRAME. It runs at the first Update after a main camera
// call, before the game places the spirits for the first eye.
void DecideFrame(const uint8_t* control, bool present, ULONGLONG now) {
  g_room_ok = false;
  // THE TABLE comes first, so the spirits have the table scale even with the
  // anchor off. Then they follow the view at 1 to 1.5 m, not at 1 mm.
  float table_origin[3], table_basis[9], table_center[3], gain = 1.0f;
  const bool table =
      TabletopRoom(table_origin, table_basis, table_center, &gain);
  const bool table_changed = table != g_table;
  g_table = table;
  g_gain = table ? gain : 1.0f;
  ScaleSpirits(control, table, g_gain);
  if (table_changed && g_logged < 24) {
    ++g_logged;
    ModLog("advisors     : %s", table
               ? "on the table: the spirits float beside it in the room, at "
                 "the table scale"
               : "off the table: the spirits are at the life size scale");
  }
  if (table && !g_said_table) {
    g_said_table = true;
    ModLog("advisors     : on the table the spirits' size and depth are "
           "multiplied by %.1f", gain);
  }
  if (!g_enabled) {
    g_latched = false;
    g_have_angles = false;
    return;
  }
  // A frame drawn in mono has nothing live in the headset. A test of time
  // since the last stereo frame is wrong here: after one long frame it would
  // also drop the anchor in a frame that the headset shows, and the spirits
  // would jump to the gaze.
  if (!StereoInPass()) {
    Drop(kReleaseIdle);
    return;
  }
  if (!HeadCameraEnabled()) {
    Drop(kReleaseNoHead);
    return;
  }
  float stage[9], position[3], focus[3];
  if (!HeadCameraStageBasis(stage) || !HeadCameraGameCamera(position, focus) ||
      !FinitePoint(position) || !FinitePoint(focus)) {
    return;
  }
  // On the table the room camera stands at the recenter pose over the table.
  // The game camera is not in the picture, so its jumps and turns are not
  // cuts of the view.
  if (table) {
    for (int i = 0; i < 3; ++i) position[i] = table_origin[i];
    for (int i = 0; i < 9; ++i) stage[i] = table_basis[i];
  }

  if (!present && g_latched && now - g_last_seen > kAwayMs) {
    Release(kReleaseAway);
  }
  bool jumped = false, turned = false;
  if (table) {
    g_have_previous = false;
  } else {
    CheckCamera(stage, position, focus, &jumped, &turned);
  }

  float h[9], lean[3];
  float yaw = 0.0f, pitch = 0.0f;
  const bool have_head =
      HeadPoseCurrent(h, lean) && HeadAngles(h, &yaw, &pitch);

  // A cut comes first, because a snap turn or a jump also trips the safety
  // net in the same frame.
  LatchReason reason = kLatchNone;
  if (!g_latched) {
    if (present) reason = kLatchArrived;
  } else if (g_cut_serial != g_latched_cut || table_changed) {
    // The table going on or off is a cut: the room camera is a new one.
    reason = kLatchCut;
  } else if (jumped) {
    reason = kLatchJumped;
  } else if (turned) {
    reason = kLatchTurned;
  }
  // Without a head pose, a latch that is due waits for the next frame.
  if (have_head) {
    if (reason != kLatchNone) {
      Latch(yaw, pitch, reason, now);
    } else if (!g_latched) {
      g_yaw = yaw;
      g_pitch = LimitPitch(pitch);
      g_have_angles = true;
    }
  }
  if (!g_have_angles) return;
  g_room_ok = BuildRoomCamera(stage, position);
}

// Called by every Update. Only the first call after a main camera call
// decides. Later calls with the same serial are the second eye, or a frame
// without a main call.
void Decide(const uint8_t* control, bool present) {
  const uint64_t serial = HeadCameraMainSerial();
  const ULONGLONG now = GetTickCount64();
  if (!g_have_decided || serial != g_decided_serial) {
    g_have_decided = true;
    g_decided_serial = serial;
    g_decided_tick = now;
    g_pass = 0;
    if (present) {
      g_seen = true;
      g_last_seen = now;
      if (now - g_last_position_log >= kPositionLogMs) {
        g_last_position_log = now;
        g_log_armed = true;
        g_log_serial = serial;
      }
    }
    DecideFrame(control, present, now);
    return;
  }
  ++g_pass;
  if (present) g_last_seen = now;
  // An odd pass is the second eye of a frame, and it must use the same camera
  // as the first eye. So only the first eye of a frame with no main call can
  // stop the swap. The anchor works only in stereo, which always has two
  // passes.
  if (g_pass >= 2 && (g_pass % 2) == 0 && g_room_ok &&
      (!HeadCameraEnabled() || now - g_decided_tick > kStaleMs)) {
    Drop(kReleaseNoHead);
  }
}

bool ReadPresent(const uint8_t* control, bool* present) {
  if (control == nullptr || IsBadReadPtr(control, kControlBytes)) return false;
  const int32_t* state =
      reinterpret_cast<const int32_t*>(control + kControlState);
  *present = state[0] != 0 || state[1] != 0;
  return true;
}

// One line per pass, so the log shows whether both eyes put each spirit at
// the same world point.
void LogPositions(const uint8_t* control, int pass, bool room) {
  char text[kSpiritCount][64];
  for (int i = 0; i < kSpiritCount; ++i) {
    const int32_t state = *reinterpret_cast<const int32_t*>(
        control + kControlState + sizeof(int32_t) * i);
    const uint8_t* spirit = *reinterpret_cast<const uint8_t* const*>(
        control + kControlSpirit + sizeof(void*) * i);
    if (state == 0 || spirit == nullptr ||
        IsBadReadPtr(spirit + kSpiritMatrix, kSpiritBytes)) {
      sprintf_s(text[i], "not shown");
      continue;
    }
    const float* p = reinterpret_cast<const float*>(spirit + kSpiritPosition);
    sprintf_s(text[i], "at %.2f %.2f %.2f", p[0], p[1], p[2]);
    g_depth_near = *reinterpret_cast<const float*>(spirit + kSpiritNear);
    g_depth_far = *reinterpret_cast<const float*>(spirit + kSpiritFar);
    g_have_depth = true;
  }
  ModLog("advisors     : pass %d, %s camera, spirit 0 %s, spirit 1 %s", pass,
         room ? "room" : "live", text[0], text[1]);
}

// THE SIZE AT A PLACE OF THE WORLD, for the spirit that Update1 places now.
// It runs inside the swap, so the stored camera is the camera that the
// spirit code has: the room camera when the anchor is on. The near depth
// already has the factor of the table. The number stays the 3 of the game
// off the table, with the key at 0, and for a spirit at its usual place.
void WorldSize(const uint8_t* spirit) {
  g_grow = spirit_size_math::kGameGrow;
  int index = -1;
  for (int i = 0; i < kSpiritCount; ++i) {
    if (g_scaled[i].spirit == spirit) index = i;
  }
  if (index < 0 || spirit == nullptr) return;
  WorldSpirit& w = g_world[index];
  if (!g_world_size || !g_table ||
      IsBadReadPtr(spirit + kSpiritMatrix, kWorldBytes)) {
    w.have = false;
    return;
  }
  const float amount =
      *reinterpret_cast<const float*>(spirit + kSpiritAmount);
  if (amount == 0.0f) {
    w.have = false;
    return;
  }
  if (!w.have || w.serial != g_decided_serial) {
    const float place[3] = {
        *reinterpret_cast<const float*>(spirit + kSpiritPlaceX),
        *reinterpret_cast<const float*>(spirit + kSpiritPlaceY),
        *reinterpret_cast<const float*>(spirit + kSpiritPlaceZ)};
    const float* u = Unscaled();
    const float right[3] = {u[0], u[3], u[6]};
    float at[3];
    spirit_size_math::WorldPlace(
        place, right, *reinterpret_cast<const float*>(spirit + kSpiritAside),
        *reinterpret_cast<const float*>(spirit + kSpiritAbove), at);
    w.factor = spirit_size_math::WorldFactor(
        spirit_size_math::Distance(at, StoredPosition()),
        *reinterpret_cast<const float*>(spirit + kSpiritNear));
    w.serial = g_decided_serial;
    w.have = true;
  }
  g_grow = spirit_size_math::Grow(w.factor);
}

// THE LOG OF A SPIRIT AT A PLACE OF THE WORLD: one line for each change of
// what a spirit does, with the numbers of its size. The scale is the length
// of an axis of the mesh matrix, and the size is that scale times the size
// of the model. The distances are to the head as the frame was drawn, to the
// room camera and to the game's own camera of the player. A distance of -1
// has no camera.
void LogWorld(const uint8_t* control) {
  static const char* const kPhaseText[spirit_size_math::kPhases] = {
      "is at its usual place", "leaves for a place of the world",
      "is at the place of the world", "returns to its usual place"};
  const bool all = g_world_log_all;
  g_world_log_all = false;
  for (int i = 0; i < kSpiritCount; ++i) {
    WorldSpirit& w = g_world[i];
    const int32_t shown = *reinterpret_cast<const int32_t*>(
        control + kControlState + sizeof(int32_t) * i);
    const uint8_t* spirit = *reinterpret_cast<const uint8_t* const*>(
        control + kControlSpirit + sizeof(void*) * i);
    if (shown == 0 || spirit == nullptr ||
        IsBadReadPtr(spirit + kSpiritModel, sizeof(float)) ||
        IsBadReadPtr(spirit + kSpiritMatrix, kWorldBytes)) {
      if (w.phase != spirit_size_math::kUsual &&
          g_world_logged < kWorldLogMost) {
        ++g_world_logged;
        ModLog("advisors     : spirit %d is not shown now", i);
      }
      w.phase = spirit_size_math::kUsual;
      continue;
    }
    const float amount =
        *reinterpret_cast<const float*>(spirit + kSpiritAmount);
    const int32_t state =
        *reinterpret_cast<const int32_t*>(spirit + kSpiritState);
    const float goal =
        (state & kStatePoints) != 0
            ? *reinterpret_cast<const float*>(spirit + kSpiritGoal)
            : 0.0f;
    const int phase = spirit_size_math::PhaseOf(amount, goal);
    const bool changed = phase != w.phase;
    w.phase = phase;
    if (!changed && !(all && phase != spirit_size_math::kUsual)) continue;
    if (g_world_logged >= kWorldLogMost) continue;
    ++g_world_logged;
    float head[3], head_basis[9], game[3], game_focus[3];
    const bool have_head = HeadCameraRenderBasis(head_basis, head);
    const bool have_game = ComfortCameraDestinations(game, game_focus);
    const float* m = reinterpret_cast<const float*>(spirit + kSpiritMatrix);
    const float* at = m + 9;
    const float scale = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float usual =
        *reinterpret_cast<const float*>(spirit + kSpiritSize) *
        *reinterpret_cast<const float*>(spirit + kSpiritBalance);
    const float size =
        scale * *reinterpret_cast<const float*>(spirit + kSpiritModel);
    const float units_per_meter = g_gain * 7.5f;
    const float from_head =
        have_head ? spirit_size_math::Distance(at, head) : -1.0f;
    const float from_room =
        g_room_ok ? spirit_size_math::Distance(at, g_room_position) : -1.0f;
    const float from_game =
        have_game ? spirit_size_math::Distance(at, game) : -1.0f;
    ModLog("advisors     : spirit %d %s. It is at %.1f %.1f %.1f, amount "
           "%.2f. Scale %.4f, %.2f times the usual scale. Size %.2f units, "
           "%.1f cm of the room at %.1f units for a meter. From the head "
           "%.1f units, and the angle of the size from the head is %.1f "
           "degrees. From the room camera %.1f units, from the game "
           "camera %.1f units. spirit_world_size=%d%s",
           i, kPhaseText[phase], at[0], at[1], at[2], amount, scale,
           usual > 0.0f ? scale / usual : 0.0f, size,
           size / units_per_meter * 100.0f, units_per_meter, from_head,
           spirit_size_math::AngleDeg(size, from_head), from_room, from_game,
           g_world_size ? 1 : 0, g_table ? "" : ", off the table");
  }
}

// The test key spirit_world_size_test=N: the rule changes at frame N of the
// controllers, with no key press, for a desktop run.
void PollWorldTest() {
  if (g_world_test_frame <= 0 || g_world_test_done) return;
  if (ControllersFrameCount() < static_cast<uint64_t>(g_world_test_frame)) {
    return;
  }
  g_world_test_done = true;
  if (!g_world_patched) return;
  g_world_size = !g_world_size;
  g_world_log_all = true;
  ModLog("advisors     : test key spirit_world_size_test: "
         "spirit_world_size=%d from frame %llu", g_world_size ? 1 : 0,
         ControllersFrameCount());
}

// The test key spirit_point_world_test=1, for a desktop run: a spirit that a
// script lets point at a place of the world goes to that place, as with the
// last argument 1 of the script word. The start of a land has no script
// that gives 1.
void ForcePointInWorld(uint8_t* control) {
  if (!g_world_test_point ||
      IsBadWritePtr(control + kControlInWorld, kControlPointBytes)) {
    return;
  }
  for (int i = 0; i < kSpiritCount; ++i) {
    const int32_t points = *reinterpret_cast<const int32_t*>(
        control + kControlPoints + sizeof(int32_t) * i);
    if (points != 1 || control[kControlInWorld + i] != 0) continue;
    control[kControlInWorld + i] = 1;
    if (!g_world_test_point_said) {
      g_world_test_point_said = true;
      ModLog("advisors     : test key spirit_point_world_test=1: spirit %d "
             "points at a place from the screen, and the test sends it to "
             "that place of the world", i);
    }
  }
}

void __fastcall h_ControlUpdate(void* self, void* unused, float dt,
                                float balance) {
  const uint8_t* control = static_cast<const uint8_t*>(self);
  bool present = false;
  const bool readable = ReadPresent(control, &present);
  if (readable) Decide(control, present);
  const int pass = g_pass;
  if (pass == 0) PollWorldTest();
  if (readable) ForcePointInWorld(static_cast<uint8_t*>(self));

  const bool swapped = readable && SwapIn();
  const bool outer_swapped = g_update_swapped;
  ++g_update_depth;
  g_update_swapped = swapped;
  o_ControlUpdate(self, unused, dt, balance);
  g_update_swapped = outer_swapped;
  --g_update_depth;
  if (swapped) SwapOut();

  if (readable && g_log_armed && g_log_serial == g_decided_serial &&
      pass < 2) {
    LogPositions(control, pass, swapped);
  }
  if (readable && pass == 0) LogWorld(control);
}

float* __fastcall h_ScreenToWorld(void* spirit, void* unused, float* out,
                                  float sx, float sy, float bias,
                                  int at_near) {
  const bool swapped = HelperMaySwap() && SwapIn();
  float* result =
      o_ScreenToWorld(spirit, unused, out, sx, sy, bias, at_near);
  // The call of Update1 for the usual place: the scale of this spirit comes
  // next, so its number is set here, while the room camera is in.
  if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == kUsualPlaceReturn) {
    WorldSize(static_cast<const uint8_t*>(spirit));
  }
  if (swapped) SwapOut();
  return result;
}

int __fastcall h_WorldToScreen(void* self, void* unused, const float* point,
                               float* nx, float* ny, int allow) {
  const bool swapped = HelperMaySwap() && SwapIn();
  const int result = o_WorldToScreen(self, unused, point, nx, ny, allow);
  if (swapped) SwapOut();
  return result;
}

// The spirits' light, placed through the room camera. The call site in
// HelpDude::Draw calls this instead of UnprojectScreenPoint. The call goes on
// to the same address, so any hook on it still runs.
typedef void(__fastcall* UnprojectFn)(const int32_t* pixel, float* out,
                                      float depth);
void __fastcall SpiritLight(const int32_t* pixel, float* out, float depth) {
  const bool swapped = HelperMaySwap() && SwapIn();
  // On the table the light keeps its place to the spirits, at the table
  // scale. Off the table the factor is 1.
  if (swapped) depth *= g_gain;
  reinterpret_cast<UnprojectFn>(kUnproject)(pixel, out, depth);
  if (swapped) SwapOut();
}

// Point the light's call at SpiritLight. Only when the five bytes are the
// ones this was read from. False leaves the call as it is.
bool PatchLightCall() {
  uint8_t* site = reinterpret_cast<uint8_t*>(kLightCall);
  if (IsBadReadPtr(site, sizeof(kLightCallBytes)) ||
      memcmp(site, kLightCallBytes, sizeof(kLightCallBytes)) != 0) {
    return false;
  }
  DWORD old = 0;
  if (!VirtualProtect(site, sizeof(kLightCallBytes), PAGE_EXECUTE_READWRITE,
                      &old)) {
    return false;
  }
  const int32_t rel = static_cast<int32_t>(
      reinterpret_cast<uintptr_t>(&SpiritLight) -
      (kLightCall + sizeof(kLightCallBytes)));
  memcpy(site + 1, &rel, sizeof(rel));
  VirtualProtect(site, sizeof(kLightCallBytes), old, &old);
  FlushInstructionCache(GetCurrentProcess(), site, sizeof(kLightCallBytes));
  return true;
}

// Make the fmul of the scale at a place of the world read g_grow. Only when
// the six bytes and the constant are the ones this was read from. False
// leaves the code as it is.
bool PatchGrow() {
  uint8_t* site = reinterpret_cast<uint8_t*>(kGrowSite);
  const float* constant = reinterpret_cast<const float*>(kGrowConstant);
  if (IsBadReadPtr(site, sizeof(kGrowBytes)) ||
      memcmp(site, kGrowBytes, sizeof(kGrowBytes)) != 0 ||
      IsBadReadPtr(constant, sizeof(float)) ||
      *constant != spirit_size_math::kGameGrow) {
    return false;
  }
  DWORD old = 0;
  if (!VirtualProtect(site, sizeof(kGrowBytes), PAGE_EXECUTE_READWRITE,
                      &old)) {
    return false;
  }
  const float* number = &g_grow;
  memcpy(site + 2, &number, sizeof(number));
  VirtualProtect(site, sizeof(kGrowBytes), old, &old);
  FlushInstructionCache(GetCurrentProcess(), site, sizeof(kGrowBytes));
  return true;
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

void SpiritAnchorInstall() {
  if (g_installed) return;
  g_installed = true;
  // Fixed addresses in the game's own data, so one check is enough.
  g_globals_ok =
      !IsBadWritePtr(Unscaled(), kMatrixBytes) &&
      !IsBadWritePtr(Scaled(), kMatrixBytes) &&
      !IsBadWritePtr(StoredPosition(), kPointBytes);
  if (!g_globals_ok) {
    ModLog("advisors     : the camera globals are not writable. The anchor "
           "stays off.");
  }
  bool ok = Hook(kControlUpdate, reinterpret_cast<void*>(&h_ControlUpdate),
                 reinterpret_cast<void**>(&o_ControlUpdate),
                 "HelpDudeControl::Update");
  ok &= Hook(kScreenToWorld, reinterpret_cast<void*>(&h_ScreenToWorld),
             reinterpret_cast<void**>(&o_ScreenToWorld),
             "HelpDude::ScreenToWorld");
  ok &= Hook(kWorldToScreen, reinterpret_cast<void*>(&h_WorldToScreen),
             reinterpret_cast<void**>(&o_WorldToScreen),
             "HelpDude::WorldToScreen");
  // All three or none. A helper with no swap would place a trail or a point
  // through the head camera while Update placed the spirit through the room.
  g_hooks_ok = ok;
  // The light follows the swap. With the anchor off, SpiritLight makes the
  // game's own call, so the patch can stay.
  if (ok) {
    ModLog("advisors     : the spirits' light %s",
           PatchLightCall() ? "is placed through the room camera"
                            : "call is not the expected one, left alone");
    g_world_patched = PatchGrow();
  }
  if (!g_world_patched) {
    ModLog("hooks        : the scale of a spirit at a place of the world at "
           "0x%08X is not the expected code, or a hook of the spirits "
           "FAILED. spirit_world_size is 0", kGrowSite);
    g_world_size = false;
  } else {
    ModLog("advisors     : spirit_world_size=%d. On the table and in the God "
           "view a spirit at a place of the world %s%s",
           g_world_size ? 1 : 0,
           g_world_size ? "has the angle of size that it has at its near "
                          "depth"
                        : "is 4 times as large as at its usual place, as in "
                          "the game",
           g_world_keys ? ". Shift and Home change it" : "");
  }
}

// spirit_world_size, the test keys and the two tests of a desktop run,
// spirit_world_size_test and spirit_point_world_test. Call it before
// SpiritAnchorInstall.
void SpiritAnchorSetWorldSize(bool on, bool test_keys, int test_frame,
                              bool test_point) {
  g_world_size = on;
  g_world_keys = test_keys;
  g_world_test_frame = test_frame > 0 ? test_frame : 0;
  g_world_test_point = test_point;
}

// THE TEST KEY of spirit_world_size (Shift and Home, with test_keys=1). The
// pointing controller gives a long pulse for 1 and a short pulse for 0.
void SpiritAnchorWorldSizeToggle() {
  if (!g_installed || !g_world_keys || !g_world_patched) return;
  g_world_size = !g_world_size;
  g_world_log_all = true;
  ControllersGesturePulse(0.5f, g_world_size ? 0.30f : 0.08f);
  ModLog("test key     : spirit_world_size=%d. On the table and in the God "
         "view a spirit at a place of the world %s",
         g_world_size ? 1 : 0,
         g_world_size ? "has the angle of size that it has at its near depth"
                      : "is 4 times as large as at its usual place, as in "
                        "the game");
}

void SpiritAnchorSetEnabled(bool on) {
  g_enabled = on;
  if (!on) {
    g_latched = false;
    g_have_angles = false;
    g_room_ok = false;
  }
  ModLog("advisors     : the spirits %s",
         on ? "stay in place in the room when you turn your head"
            : "follow your view, as the game places them");
}

// A cut: the view changed at once. The spirits land again around the gaze at
// the next decision. comfort_camera.cpp (snap turns and instant jumps),
// script_camera.cpp (its cuts) and HeadPoseRecenter call this.
void SpiritAnchorCameraCut() { InterlockedIncrement(&g_cut_serial); }

void SpiritAnchorReport() {
  if (!g_installed) return;
  const uint64_t latches = g_latches[kLatchArrived] + g_latches[kLatchCut] +
                           g_latches[kLatchJumped] + g_latches[kLatchTurned];
  const uint64_t releases = g_releases[kReleaseAway] +
                            g_releases[kReleaseIdle] +
                            g_releases[kReleaseNoHead];
  if (latches != 0 || releases != 0) {
    ModLog("advisors     : %llu anchors set: %llu new, %llu at a cut, %llu at "
           "a camera jump, %llu at a camera turn",
           latches, g_latches[kLatchArrived], g_latches[kLatchCut],
           g_latches[kLatchJumped], g_latches[kLatchTurned]);
    ModLog("advisors     : %llu anchors released: %llu away, %llu stereo "
           "off, %llu no head camera",
           releases, g_releases[kReleaseAway], g_releases[kReleaseIdle],
           g_releases[kReleaseNoHead]);
  }
  if (g_seen) {
    ModLog("advisors     : anchor %s at yaw %.1f pitch %.1f degrees, %llu "
           "calls used the room camera",
           !g_enabled ? "off" : (g_latched ? "held" : "free"),
           g_yaw * kDegrees, g_pitch * kDegrees, g_swaps);
    if (g_have_depth) {
      ModLog("advisors     : spirit depth %.1f to %.1f units", g_depth_near,
             g_depth_far);
    }
  }
  for (int i = 0; i < kLatchReasons; ++i) g_latches[i] = 0;
  for (int i = 0; i < kReleaseReasons; ++i) g_releases[i] = 0;
  g_swaps = 0;
  g_seen = false;
}
