// The camera the story moves, made comfortable in a headset.
//
// WHAT MOVES THE CAMERA WHEN THE PLAYER DOES NOT
//
// GCamera keeps a stack of camera modes, and the mode on top moves the six
// GCamera Zoomers on every frame. CameraModeNew3 is the player's camera. The
// story uses the other modes. The campaign script
// (Scripts\Quests\challenge.chl) pushes CameraModeScript with
// START_CAMERA_CONTROL. In that mode MOVE_CAMERA_POSITION and
// MOVE_CAMERA_FOCUS ease over 2 to 8 seconds, and the follow natives aim the
// Zoomers again on every frame. A dual camera (CameraModeTwoObjects) and a
// creature follow (CameraModeFollow) also aim them on every frame.
// RUN_CAMERA_PATH writes a new pose on every frame. PLAY_HAND_DEMO writes
// recorded poses, and draws them with its own UpdateCamera call.
// SHAKE_CAMERA shakes the view.
//
// A turn that the player's head did not make is what makes a headset sick.
//
// WHY THE SCRIPT MOVES ARE NOT JUDGED ONE BY ONE
//
// A hook on the script moves that judges each move once (play it or cut it)
// sees only a few moves. Most story motion never goes through them. The
// follow and dual cameras aim again on every frame, so they would glide. A
// clamp that keeps the camera back from its subject would make a glide of its
// own: it would aim the Zoomers at the clamped point with the time the move
// has left. Float rounding moves the clamped point, so it would aim again on
// about half of all frames.
//
// THE DRAWN CAMERA HOLDS ITS SHOT, AND CUTS
//
// This file does not change the game camera. The Zoomers move as the game
// moves them, so HAS_CAMERA_ARRIVED, script timing and save games do not
// change. Only the camera that is DRAWN changes. head_camera.cpp asks
// ScriptCameraRenderPose at the top of its UpdateCamera hook, before anything
// reads the pose. The pose that comes back is then the camera the game asked
// for, for all that follows: the head, the stored camera at 0x00EA1DB8, the
// Hand's ray and the billboard heading.
//
// 1. WHO OWNS THE CAMERA. The main call at the end of GCamera::Update
//    (return 0x00442627) reads the mode on top of the stack. The player's
//    modes are not held. CameraModeFollow is held when the follow option is
//    on. Every other mode is held. The hand demo's own call (return
//    0x005DB125) is always held. No other call is touched.
//
// 2. WHAT THE SHOT IS. Variant 2, the default, shows the camera where it
//    is, so the view keeps the old shot and cuts when the move ends. Variant 1
//    shows where the move ends: the Zoomer
//    destinations. The view then cuts at the start of a move. Variant 0 is
//    the game's own camera.
//
// 3. A LITTLE MORE ROOM FOR A CLOSE SHOT. A shot nearer to its focus than
//    60 units moves back along its own line by 25 percent, but not past 60
//    units. A shot further out does not change. So each shot keeps its
//    framing. The hard limits only keep the camera out of its subject (5
//    units) and out of the ground (5 units). A rule that keeps every shot 60
//    units back and 15 above the ground puts most shots too far, and some in
//    places that make no sense. All of this
//    happens to the target BEFORE the comparisons below. A clamp after them
//    would find a difference, and cut, on every frame.
//
// 4. WHEN IT CUTS. A new owner cuts at once. A target that drifts 90 degrees,
//    or half the view distance, from the shot cuts. A target that stays still
//    for 300 ms, and is 5 degrees or 5 percent from the shot, cuts. Nothing
//    else moves the drawn camera, so the view never glides.
//
// 5. THE GAME SEES A STILL CAMERA. GCamera::Update sets its moving flag when
//    the stored camera is not the new pose. While a shot is held, that is
//    true on every frame. The interface then clears a buffer that looks like
//    the gesture trail. ScriptCameraAfterGameCamera sets the flag from the
//    drawn camera instead.
//
// 6. NO SHAKES.
//
// A cut blinks when the player asked for a blink (comfort_camera.cpp). Each
// cut and each owner change also tells spirit_anchor.cpp to place the spirits
// again.
//
// 7. NOT IN THE CUT SCENE WINDOW. While the cinema bars are up in the God
//    view, the picture is on a screen in the room (cutscene_window.cpp). In
//    those frames this file acts as variant 0: the game moves the camera,
//    and a shake plays. The owner is still read, so ScriptCameraStoryOwns
//    and the log stay true.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"

extern void ModLog(const char* format, ...);
// comfort_camera.cpp: a moment of black, when the player asked for one.
void ComfortCameraBlink();
// spirit_anchor.cpp: the view jumped, so place the spirits again.
void SpiritAnchorCameraCut();
// cutscene_window.cpp: this frame draws on the screen of the cut scene
// window. A picture that moves on a screen in the room is comfortable, so
// the game moves the camera there, and a shake is not stopped.
bool CutsceneWindowFlat();

namespace {

const uintptr_t kGame = 0x00D0195C;
// GGame + this is the GCamera. GGame::GetCamera 0x0054C180 reads only this.
const int kGameCamera = 0x2502C0;
// Who called LH3DTech::UpdateCamera, by return address.
const uintptr_t kFromGameCamera = 0x00442627;  // GCamera::Update, main call
const uintptr_t kFromHandDemo = 0x005DB125;    // the hand demo's own call
const uintptr_t kStartCameraShake = 0x0068F400;
// LH3DMapCoords from a world x and z, then the ground height there.
const uintptr_t kMakeMapCoords = 0x004427B0;
const uintptr_t kGetAltitude = 0x00803090;

// GCamera: the mode stack, the index of its top (-1 when empty), and the
// flag that GCamera::IsMoving 0x00441B00 returns.
const int kModes = 0x28;
const int kModeSlots = 12;
const int kTop = 0x58;
const int kMoving = 0x74;
// Three Zoomers for the focus and three for the position, 0x30 bytes each.
// The second float of a Zoomer is its destination.
const int kCameraFocus = 0x88;
const int kCameraPosition = 0x118;
const int kZoomerSize = 0x30;
const int kDestination = 1;

// Every camera mode, by the vtable its RTTI names. The player steers the
// first group, and those are never held. New, New1, New2 and FlyAndClick are
// not seen in normal play, but their names say the player steers them.
enum ModeOwner { kPlayerMode, kStoryMode, kFollowMode };
struct ModeInfo {
  uintptr_t vtable;
  const char* name;
  ModeOwner owner;
};
const ModeInfo kModeInfo[] = {
    {0x008C7BFC, "CameraModeNew3", kPlayerMode},
    {0x008C7958, "CameraModeFree", kPlayerMode},
    {0x008C7AB4, "CameraModeNew", kPlayerMode},
    {0x008C7A58, "CameraModeNew1", kPlayerMode},
    {0x008C7B58, "CameraModeNew2", kPlayerMode},
    {0x008C7814, "CameraModeFlyAndClick", kPlayerMode},
    {0x008C7D5C, "CameraModeScript", kStoryMode},
    {0x008C7DD0, "CameraModeTwoObjects", kStoryMode},
    {0x008C7740, "CameraModeCtrInteract", kStoryMode},
    {0x008C7D10, "CameraModePath", kStoryMode},
    {0x008C77A0, "CameraModeDance", kStoryMode},
    {0x008C7694, "CameraModeCitadel", kStoryMode},
    {0x008C7884, "CameraModeFollow", kFollowMode},
    {0x008C7904, "CameraModeFollowHeading", kFollowMode},
};
const int kKnownModes = sizeof(kModeInfo) / sizeof(kModeInfo[0]);
// The kinds after the table, for the log and the counts.
const int kKindHandDemo = kKnownModes;
const int kKindOther = kKnownModes + 1;  // a mode that is not in the table
const int kKindNone = kKnownModes + 2;   // no mode, or no camera to read
const int kKinds = kKnownModes + 3;
// The hand demo has no mode of its own, so its return address is its id.
const void* const kHandDemoId = reinterpret_cast<const void*>(kFromHandDemo);

// The variants. The ini key script_camera chooses one.
const int kGameMoves = 0;  // the game's own camera
const int kCutAtStart = 1;
const int kCutAtEnd = 2;

// A held shot cuts when its target drifts this far from it. The move limit
// is a share of the view distance, and never less than kCutLeast.
const float kCutLeast = 40.0f;
// A target at rest cuts when it is this far from the shot.
const float kSettleDegrees = 5.0f;
const float kSettleShare = 0.05f;
const float kSettleLeast = 2.0f;
// A target that moves less than this in kRestMs is at rest. These limits are
// small, so an ease is at rest only at its end. The last 300 ms of a 7 s, 63
// unit move still move the camera about 0.35 units.
const float kRestDegrees = 0.3f;
const float kRestShare = 0.003f;
const float kRestLeast = 0.1f;
const ULONGLONG kRestMs = 300;
// A hold with no call for this long starts again.
const ULONGLONG kForgetMs = 500;
// A main call this soon after the hand demo's own call keeps the demo shot.
const ULONGLONG kDemoHandoverMs = 250;
const float kPi = 3.14159265f;

typedef void(__cdecl* StartShakeFn)(uint32_t, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t);
struct MapCoords {
  int32_t x;
  int32_t z;
  int32_t unused;
};
typedef MapCoords*(__fastcall* MakeMapCoordsFn)(MapCoords* out, void* unused,
                                                float x, float z);
typedef float(__fastcall* GetAltitudeFn)(const MapCoords* at, void* unused);

StartShakeFn o_StartShake = nullptr;

bool g_installed = false;
int g_variant = kCutAtEnd;
bool g_hold_follow = true;
// The hard limits: out of the subject, and out of the ground.
float g_min_distance = 5.0f;
float g_min_height = 5.0f;
// A close shot moves back by this factor, but not past this distance.
float g_close_distance = 60.0f;
float g_close_zoom = 1.25f;
float g_cut_degrees = 90.0f;
float g_cut_share = 0.5f;

struct Pose {
  float position[3];
  float focus[3];
};

// Who has the camera on one call.
struct Owner {
  const void* id;   // the top mode, or kHandDemoId. nullptr for no mode.
  int kind;         // an index into kModeInfo, or a kind after the table
  bool held;
  bool clamp;
  uint8_t* camera;  // the GCamera, on the main call only
};

// The shot on screen, and what decides the next cut.
struct Hold {
  bool valid;
  Pose shown;
  const void* owner;
  ULONGLONG last_ms;
  // The target when it last moved more than the rest limits, and when.
  Pose anchor;
  ULONGLONG anchor_ms;
};
Hold g_hold = {};

// The pose the last owned call drew, held or not.
Pose g_drawn = {};
bool g_have_drawn = false;

// The owner the last owned call saw, for the log and the spirits.
const void* g_seen_owner = nullptr;
int g_seen_kind = kKindNone;
bool g_have_seen = false;
// The last main call belonged to a story or a follow mode. The God view
// stands aside then (tabletop.cpp).
bool g_story_owns = false;

// What the main call leaves for ScriptCameraAfterGameCamera.
struct AfterCall {
  bool pending;
  bool moved;
  void* camera;
};
AfterCall g_after = {};

// Why a cut happened.
enum CutReason {
  kCutStart,
  kCutNewOwner,
  kCutPause,
  kCutDrift,
  kCutRest,
  kCutRelease,
  kReasons
};
const char* const kReasonNames[kReasons] = {
    "a hold starts", "new owner", "after a pause",
    "drifted",       "at rest",   "back to the player"};

// Per report window.
uint64_t g_held_frames[kKinds] = {};
uint64_t g_cuts[kReasons] = {};
uint64_t g_quiet_changes = 0;
uint64_t g_owner_changes = 0;
uint64_t g_clamped_frames = 0;
uint64_t g_clamped_cuts = 0;
uint64_t g_moving_set = 0;
uint64_t g_shakes = 0;
// For the whole session.
int g_owner_logs = 0;
const int kMaxOwnerLogs = 40;
int g_cut_logs = 0;
const int kMaxCutLogs = 24;

const char* KindName(int kind) {
  if (kind >= 0 && kind < kKnownModes) return kModeInfo[kind].name;
  if (kind == kKindHandDemo) return "the hand demo";
  if (kind == kKindOther) return "another camera mode";
  return "no camera mode";
}

int KindOf(uintptr_t vtable) {
  for (int i = 0; i < kKnownModes; ++i) {
    if (kModeInfo[i].vtable == vtable) return i;
  }
  return kKindOther;
}

const char* VariantText(int variant) {
  if (variant == kCutAtStart) return "cuts at once to where each move ends";
  if (variant == kCutAtEnd) {
    return "holds each shot until the camera stops, then cuts";
  }
  return "moves as the game moves it";
}

// Who has the camera. False for every call other than the main call and the
// hand demo's own call. A main call with no mode to read belongs to the
// player, so a hold stops.
bool ReadOwner(uintptr_t caller, Owner* out) {
  out->id = nullptr;
  out->kind = kKindNone;
  out->held = false;
  out->clamp = false;
  out->camera = nullptr;
  if (caller == kFromHandDemo) {
    // Recorded poses that the demo Hand is drawn through. A clamp would move
    // the demo Hand away from what it points at.
    out->id = kHandDemoId;
    out->kind = kKindHandDemo;
    out->held = true;
    return true;
  }
  if (caller != kFromGameCamera) return false;
  uint8_t* const game = *reinterpret_cast<uint8_t* const*>(kGame);
  if (game == nullptr) return true;
  uint8_t* const camera =
      *reinterpret_cast<uint8_t* const*>(game + kGameCamera);
  if (camera == nullptr ||
      IsBadReadPtr(camera, kCameraPosition + 3 * kZoomerSize)) {
    return true;
  }
  const int32_t top = *reinterpret_cast<const int32_t*>(camera + kTop);
  if (top < 0 || top >= kModeSlots) return true;
  const uintptr_t* const mode =
      *reinterpret_cast<const uintptr_t* const*>(camera + kModes + 4 * top);
  if (mode == nullptr || IsBadReadPtr(mode, sizeof(uintptr_t))) return true;
  out->id = mode;
  out->kind = KindOf(*mode);
  out->camera = camera;
  out->clamp = true;
  const ModeOwner owner =
      out->kind < kKnownModes ? kModeInfo[out->kind].owner : kStoryMode;
  out->held = owner == kStoryMode || (owner == kFollowMode && g_hold_follow);
  return true;
}

// The heading of the flattened view, in radians. False when the view looks
// straight down and has no heading.
bool Heading(const float* position, const float* focus, float* heading) {
  const float dx = focus[0] - position[0];
  const float dz = focus[2] - position[2];
  if (dx * dx + dz * dz < 1e-4f) return false;
  *heading = atan2f(dx, dz);
  return true;
}

// Under R = L * H (head_camera.cpp) the game's pitch never reaches the view.
// So the turn the player sees is the turn of the flattened heading.
float TurnDegrees(const Pose& from, const Pose& to) {
  float a = 0.0f, b = 0.0f;
  if (!Heading(from.position, from.focus, &a) ||
      !Heading(to.position, to.focus, &b)) {
    return 0.0f;
  }
  float d = b - a;
  while (d > kPi) d -= 2.0f * kPi;
  while (d < -kPi) d += 2.0f * kPi;
  return fabsf(d) * 180.0f / kPi;
}

float Distance(const float* a, const float* b) {
  const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
  return sqrtf(x * x + y * y + z * z);
}

// True when b has turned or moved at least a limit away from a. The move
// limit is a share of the view distance of a, and never less than least.
// A move of the focus counts as well as a move of the position.
bool Differs(const Pose& a, const Pose& b, float degrees, float share,
             float least) {
  if (TurnDegrees(a, b) >= degrees) return true;
  float limit = share * Distance(a.position, a.focus);
  if (limit < least) limit = least;
  return Distance(a.position, b.position) >= limit ||
         Distance(a.focus, b.focus) >= limit;
}

// False for a pose with a NaN or a value far outside any island.
bool Usable(const Pose& pose) {
  for (int i = 0; i < 3; ++i) {
    if (!(fabsf(pose.position[i]) < 1e6f) || !(fabsf(pose.focus[i]) < 1e6f)) {
      return false;
    }
  }
  return true;
}

// Where the move under way ends: the six Zoomer destinations. Every camera
// mode sets these, through Zoomer::SetDestinationWithSpeedAndTime or
// Zoomer::SetPosition, so they are never stale.
Pose Destinations(const uint8_t* camera) {
  Pose pose;
  for (int axis = 0; axis < 3; ++axis) {
    pose.position[axis] = reinterpret_cast<const float*>(
        camera + kCameraPosition + axis * kZoomerSize)[kDestination];
    pose.focus[axis] = reinterpret_cast<const float*>(
        camera + kCameraFocus + axis * kZoomerSize)[kDestination];
  }
  return pose;
}

// GCamera::Update calls LH3DIsland::GetAltitude itself before the main call,
// for its ground lift, so the ground can be read here too.
float GroundAt(float x, float z) {
  MapCoords at = {0, 0, 0};
  reinterpret_cast<MakeMapCoordsFn>(kMakeMapCoords)(&at, nullptr, x, z);
  return reinterpret_cast<GetAltitudeFn>(kGetAltitude)(&at, nullptr);
}

// The distance a shot at `d` from its focus is drawn at. A close shot gets
// g_close_zoom more room, but never more than g_close_distance, and a shot
// that is further out keeps its own distance. So the result grows with d and
// has no jump. The hard limit comes last.
float RoomyDistance(float d) {
  float out = d * g_close_zoom;
  if (out > g_close_distance) out = g_close_distance;
  if (out < d) out = d;
  if (out < g_min_distance) out = g_min_distance;
  return out;
}

// Give a close shot more room, then keep the camera a minimum height above
// the ground. True when the camera moved. The same target always gives the
// same result, so a still target stays still after the clamp.
bool Clamp(Pose* pose) {
  float* p = pose->position;
  const float* f = pose->focus;
  bool moved = false;
  const float d = Distance(p, f);
  const float wanted = RoomyDistance(d);
  if (wanted > d) {
    if (d > 1e-3f) {
      const float k = wanted / d;
      for (int i = 0; i < 3; ++i) p[i] = f[i] + (p[i] - f[i]) * k;
    } else {
      // A camera on its focus has no direction to move back along. Straight
      // up would look straight down, which head_camera.cpp cannot use. So
      // the camera goes back and up along the heading of the drawn view, at
      // 45 degrees. The next frame then has the same heading, and the same
      // result.
      float heading = 0.0f;
      if (g_have_drawn) Heading(g_drawn.position, g_drawn.focus, &heading);
      const float k = wanted * 0.7071f;
      p[0] = f[0] - sinf(heading) * k;
      p[1] = f[1] + k;
      p[2] = f[2] - cosf(heading) * k;
    }
    moved = true;
  }
  const float floor = GroundAt(p[0], p[2]) + g_min_height;
  if (p[1] < floor) {
    p[1] = floor;
    moved = true;
  }
  return moved;
}

void LogOwner(const Owner& owner) {
  ++g_owner_changes;
  if (g_owner_logs >= kMaxOwnerLogs) return;
  ++g_owner_logs;
  const char* how = CutsceneWindowFlat()    ? "The window shows the game camera."
                    : g_variant == kGameMoves ? "The game moves the view."
                    : owner.held              ? "The view holds its shot."
                                              : "The player moves the view.";
  if (g_have_seen) {
    ModLog("script camera: the camera belongs to %s, after %s. %s",
           KindName(owner.kind), KindName(g_seen_kind), how);
  } else {
    ModLog("script camera: the camera belongs to %s. %s",
           KindName(owner.kind), how);
  }
}

// The drawn view changes from g_drawn to `to`. When the change shows, count
// it as a cut, blink, and log it. True when the change shows. A hold that
// starts where the view already is does not show, so it does not blink.
bool Jump(const Pose& to, int reason, int kind, bool clamped) {
  const bool shows =
      g_have_drawn &&
      Differs(g_drawn, to, kSettleDegrees, kSettleShare, kSettleLeast);
  if (!shows) {
    ++g_quiet_changes;
    return false;
  }
  ++g_cuts[reason];
  if (clamped) ++g_clamped_cuts;
  ComfortCameraBlink();
  if (g_cut_logs < kMaxCutLogs) {
    ++g_cut_logs;
    ModLog("script camera: %s cut (%s), %.0f units and %.0f degrees from the "
           "last view%s", KindName(kind), kReasonNames[reason],
           Distance(g_drawn.position, to.position), TurnDegrees(g_drawn, to),
           clamped ? ", kept back from its subject" : "");
  }
  return true;
}

void Show(const Pose& pose, float* out_position, float* out_focus) {
  for (int i = 0; i < 3; ++i) {
    out_position[i] = pose.position[i];
    out_focus[i] = pose.focus[i];
  }
}

void __cdecl h_StartShake(uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                          uint32_t e, uint32_t f) {
  if (g_variant != kGameMoves && !CutsceneWindowFlat()) {
    ++g_shakes;
    return;
  }
  o_StartShake(a, b, c, d, e, f);
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

int Limit(int value, int least, int most) {
  return value < least ? least : (value > most ? most : value);
}

}  // namespace

void ScriptCameraSetOptions(int variant, int min_distance, int min_height,
                            int close_distance, int close_zoom_pct,
                            int cut_degrees, int cut_percent,
                            bool hold_follow) {
  if (variant >= kGameMoves && variant <= kCutAtEnd) g_variant = variant;
  if (min_distance >= 0) {
    g_min_distance = static_cast<float>(Limit(min_distance, 0, 1000));
  }
  if (min_height >= 0) {
    g_min_height = static_cast<float>(Limit(min_height, 0, 500));
  }
  if (close_distance >= 0) {
    g_close_distance = static_cast<float>(Limit(close_distance, 0, 1000));
  }
  // 100 leaves close shots where the game puts them.
  if (close_zoom_pct >= 0) {
    g_close_zoom = static_cast<float>(Limit(close_zoom_pct, 100, 300)) / 100.0f;
  }
  // A limit near 0 would cut on almost every frame.
  if (cut_degrees >= 0) {
    g_cut_degrees = static_cast<float>(Limit(cut_degrees, 10, 180));
  }
  if (cut_percent >= 0) {
    g_cut_share = static_cast<float>(Limit(cut_percent, 10, 400)) / 100.0f;
  }
  g_hold_follow = hold_follow;
  ModLog("script camera: the story camera %s", VariantText(g_variant));
  ModLog("script camera: a shot nearer than %.0f units moves back %.0f "
         "percent, to %.0f units at most", g_close_distance,
         (g_close_zoom - 1.0f) * 100.0f, g_close_distance);
  ModLog("script camera: at least %.0f units from its subject and %.0f above "
         "the ground", g_min_distance, g_min_height);
  ModLog("script camera: a held shot cuts at %.0f degrees or %.0f percent of "
         "the view distance. Follow cameras %s.", g_cut_degrees,
         g_cut_share * 100.0f,
         g_hold_follow ? "are held" : "move as the game moves them");
}

void ScriptCameraInstall() {
  if (g_installed) return;
  g_installed = true;
  Hook(kStartCameraShake, reinterpret_cast<void*>(&h_StartShake),
       reinterpret_cast<void**>(&o_StartShake),
       "PSysGlobal::StartCameraShake");
}

// head_camera.cpp calls this at the top of its UpdateCamera hook. True when
// the pose to draw is not the one asked for, and then out_position and
// out_focus hold it.
bool ScriptCameraRenderPose(uintptr_t caller, const float* position,
                            const float* focus, float* out_position,
                            float* out_focus) {
  Owner owner;
  if (!ReadOwner(caller, &owner)) return false;
  if (position == nullptr || focus == nullptr || out_position == nullptr ||
      out_focus == nullptr) {
    return false;
  }
  const bool main_call = caller == kFromGameCamera;
  if (main_call) g_after.pending = false;
  if (main_call) g_story_owns = owner.held;
  const ULONGLONG now = GetTickCount64();
  Pose asked;
  for (int i = 0; i < 3; ++i) {
    asked.position[i] = position[i];
    asked.focus[i] = focus[i];
  }

  // GCamera::Update makes no main call while a hand demo plays, and the demo
  // draws with its own call. A main call at the edge of a demo would make
  // two owners take turns, and cut on every frame. So it keeps the demo shot
  // until the demo has stopped for kDemoHandoverMs.
  // The cut scene window shows the game camera, so a flat frame holds nothing.
  const bool game_moves = g_variant == kGameMoves || CutsceneWindowFlat();
  if (main_call && !game_moves && g_hold.valid &&
      g_hold.owner == kHandDemoId && now - g_hold.last_ms <= kDemoHandoverMs) {
    Show(g_hold.shown, out_position, out_focus);
    g_after.pending = true;
    g_after.moved = false;
    g_after.camera = owner.camera;
    return true;
  }

  // A new owner, in every variant, so the log shows which mode drove a scene.
  bool relatch = false;
  if (!g_have_seen || owner.id != g_seen_owner) {
    LogOwner(owner);
    g_seen_owner = owner.id;
    g_seen_kind = owner.kind;
    g_have_seen = true;
    relatch = true;
  }

  // The game's own camera, or the player's. A shot that was held lets go,
  // which is a cut back to the camera the game has.
  if (game_moves || !owner.held) {
    if (g_hold.valid) {
      g_hold.valid = false;
      // In a flat frame the picture is on the screen of the cut scene window,
      // so a shot that lets go there is not a cut of the view.
      if (!CutsceneWindowFlat() &&
          Jump(asked, kCutRelease, owner.kind, false)) {
        relatch = true;
      }
    }
    g_drawn = asked;
    g_have_drawn = true;
    if (relatch) SpiritAnchorCameraCut();
    return false;
  }

  Pose target = asked;
  if (g_variant == kCutAtStart && owner.camera != nullptr) {
    const Pose ahead = Destinations(owner.camera);
    if (Usable(ahead)) target = ahead;
  }
  const bool clamped = owner.clamp && Clamp(&target);
  if (clamped) ++g_clamped_frames;

  int reason = -1;
  if (!g_hold.valid) {
    reason = kCutStart;
  } else if (owner.id != g_hold.owner) {
    reason = kCutNewOwner;
  } else if (now - g_hold.last_ms > kForgetMs) {
    reason = kCutPause;
  }
  const bool fresh = reason >= 0;
  if (fresh ||
      Differs(g_hold.anchor, target, kRestDegrees, kRestShare, kRestLeast)) {
    g_hold.anchor = target;
    g_hold.anchor_ms = now;
  }
  if (!fresh) {
    if (Differs(g_hold.shown, target, g_cut_degrees, g_cut_share,
                kCutLeast)) {
      reason = kCutDrift;
    } else if (now - g_hold.anchor_ms >= kRestMs &&
               Differs(g_hold.shown, target, kSettleDegrees, kSettleShare,
                       kSettleLeast)) {
      reason = kCutRest;
    }
  }
  if (reason >= 0) {
    if (Jump(target, reason, owner.kind, clamped)) relatch = true;
    g_hold.shown = target;
    g_hold.valid = true;
  }
  g_hold.owner = owner.id;
  g_hold.last_ms = now;
  ++g_held_frames[owner.kind];

  // The game's own rule for its moving flag, applied to the drawn camera.
  const bool moved =
      !g_have_drawn || memcmp(g_drawn.position, g_hold.shown.position,
                              sizeof(g_drawn.position)) != 0;
  g_drawn = g_hold.shown;
  g_have_drawn = true;
  if (relatch) SpiritAnchorCameraCut();
  if (main_call) {
    g_after.pending = true;
    g_after.moved = moved;
    g_after.camera = owner.camera;
  }
  Show(g_hold.shown, out_position, out_focus);
  return true;
}

// True when the last main camera call belonged to a story or a follow mode,
// whichever variant is set. tabletop.cpp asks it on the main call, after
// ScriptCameraRenderPose has read the owner of that call.
bool ScriptCameraStoryOwns() { return g_story_owns; }

// Who has the game camera at this moment, read from the mode stack and not
// from the last main call: 0 a camera mode of the player, 1 a story or a
// follow mode, and -1 when no mode can be read. A script of the game can put
// a story mode on the stack in a game turn, between two main calls.
// hand_direct.cpp asks this when a script measures from the camera (docs/
// god-view.md, "The player that a script sees").
int ScriptCameraOwnerNow() {
  Owner owner;
  ReadOwner(kFromGameCamera, &owner);
  if (owner.kind == kKindNone) return -1;
  if (owner.kind < kKnownModes && kModeInfo[owner.kind].owner == kPlayerMode) {
    return 0;
  }
  return 1;
}

// pointer_map.cpp calls this right after GCamera::Update. GCamera::Update
// set the moving flag from the stored camera, which is the held shot, and
// the pose the game has, which is not. So a held shot would be moving on
// every frame. Set the flag as the game would for the drawn camera. Only a
// held main call in this update leaves anything to set.
void ScriptCameraAfterGameCamera(void* camera) {
  if (!g_after.pending) return;
  g_after.pending = false;
  if (camera == nullptr || camera != g_after.camera) return;
  int32_t* moving =
      reinterpret_cast<int32_t*>(static_cast<uint8_t*>(camera) + kMoving);
  const int32_t want = g_after.moved ? 1 : 0;
  if (*moving != want) {
    *moving = want;
    ++g_moving_set;
  }
}

void ScriptCameraReport() {
  uint64_t held = 0;
  for (int k = 0; k < kKinds; ++k) held += g_held_frames[k];
  uint64_t cuts = 0;
  for (int r = 0; r < kReasons; ++r) cuts += g_cuts[r];
  if (held + cuts + g_quiet_changes + g_owner_changes + g_clamped_frames +
          g_moving_set + g_shakes ==
      0) {
    return;
  }
  char kinds[512] = "none";
  size_t used = 0;
  for (int k = 0; k < kKinds && used < sizeof(kinds); ++k) {
    if (g_held_frames[k] == 0) continue;
    const int wrote = _snprintf_s(
        kinds + used, sizeof(kinds) - used, _TRUNCATE, "%s%s %llu",
        used == 0 ? "" : ", ", KindName(k), g_held_frames[k]);
    if (wrote < 0) break;
    used += static_cast<size_t>(wrote);
  }
  ModLog("script camera: %llu frames held (%s). %llu owner changes.", held,
         kinds, g_owner_changes);
  ModLog("script camera: %llu cuts: %llu as a hold starts, %llu new owner, "
         "%llu after a pause, %llu drifted, %llu at rest, %llu back to the "
         "player.", cuts, g_cuts[kCutStart], g_cuts[kCutNewOwner],
         g_cuts[kCutPause], g_cuts[kCutDrift], g_cuts[kCutRest],
         g_cuts[kCutRelease]);
  ModLog("script camera: %llu new shots too small to see. The clamp moved "
         "the target on %llu frames and in %llu cuts.", g_quiet_changes,
         g_clamped_frames, g_clamped_cuts);
  ModLog("script camera: %llu moving flags set from the drawn camera, %llu "
         "shakes stopped.", g_moving_set, g_shakes);
  for (int k = 0; k < kKinds; ++k) g_held_frames[k] = 0;
  for (int r = 0; r < kReasons; ++r) g_cuts[r] = 0;
  g_quiet_changes = 0;
  g_owner_changes = 0;
  g_clamped_frames = 0;
  g_clamped_cuts = 0;
  g_moving_set = 0;
  g_shakes = 0;
}
