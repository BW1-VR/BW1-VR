// Move the camera the way VR needs: in steps, not in slides.
//
// In a headset, any camera motion the player's own head did not make can make
// them ill. The game moves its camera in three ways that do exactly that:
//
//   The middle mouse turns and tilts the camera smoothly.
//   A double click on the land flies the camera there, for about 2.25 s.
//   After a drag of the land, the camera glides on for about 0.3 s.
//
// This file replaces all three:
//
//   SNAP TURNS IN PLACE. A drag with the middle mouse, or a turn key, turns
//   the player's heading by a fixed step in one frame. The position stays
//   where it is. The middle mouse does not tilt the camera.
//
//   INSTANT JUMPS. A flight that starts is finished in the same frame.
//
//   NO GLIDE. The camera follows a drag of the land exactly and stops when
//   the button is released.
//
//   ZOOM IN STEPS. One click of the wheel changes the distance by a fixed
//   ratio. The first click is used at once. Clicks that come in the next
//   150 ms are added to the next step, up to three clicks.
//
// It also writes the camera for the grab of the world, which
// world_grab.cpp solves from the player's hands. While a grab holds, the snap
// turns and the wheel are refused.
//
// IN THE GOD VIEW (play_mode=4, docs/god-view.md) the picture does not come
// from the game's camera. A snap turn and a wheel step then step the God
// view, and after the game's update the game camera goes over the player's
// place, the way the snap turn writes it (GodAfter). A tilt key moves the
// ground of the God view (GodTiltKeys). A land drag of the game does not
// move the game camera, and the mouse pans the God view in its place
// (GodDragAfter, and MousePan in tabletop.cpp).
//
// A hook on CameraHelpCallback logs the events that the tutorial counts
// (h_Help).
//
// Everything below was read from the binary, in CameraModeNew3, the camera
// mode used in play. See docs/backend.md, "Moving around".

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "MinHook.h"
#include "controllers.h"

extern void ModLog(const char* format, ...);
// spirit_anchor.cpp: the camera made a cut, so the spirits find their place
// again.
void SpiritAnchorCameraCut();
// controllers.cpp: a snap turn that a controller asked for has happened, and
// whether a grip holds the world.
void ControllersSnapTurned();
bool ControllersGrabHeld();
// world_grab.cpp: the grab of the world solves the camera, and this file
// writes it.
void WorldGrabCheckCamera(const float position[3], const float focus[3]);
bool WorldGrabSolve(const GrabCamera& camera, GrabCamera* out);
// tabletop.cpp: the God view. Its steps, and the game camera it wants.
// teleport.cpp: a double click of the mouse in the God view becomes a
// teleport. True when it took the click. The jump event of the help.
bool TeleportMouseDoubleClick(void* mode);
int TeleportJumpEvent();
bool TabletopGodOwnsView();
bool TabletopGodStep(int zoom, int ground);
bool TabletopGodTurn(int direction, float degrees);
bool TabletopGodGameCamera(float position[3], float focus[3],
                           uint64_t* version);
// The game put its own camera at a new place, and the camera is at rest
// there. The God view goes to that place at its next main camera call.
void TabletopGodGameMoved(const char* cause, const float position[3],
                          const float focus[3]);
// The game camera has the place that the God view wanted (GodAfter). When
// the game puts its camera there again, the view comes back where it is now.
void TabletopGodCameraWritten(const float position[3], const float focus[3]);
// The land point that a game camera looks at: where the line from the camera
// to its focus meets the land, or the focus put on the land.
void TabletopGodLookPoint(const float position[3], const float focus[3],
                          float out[3]);
// A test with no key press: the test keys of the place at a game camera.
void TabletopGodSpawnToggle();

namespace {

// ControlMap::IsActionPerformed, thiscall (int action), ret 4. Action 0x10 is
// the middle mouse, and CameraModeNew3::Update at 0x0045AC69 is its only
// reader.
const uintptr_t kIsActionPerformed = 0x00470AF0;
const int kActionMiddle = 0x10;
// CameraModeNew3::Update, thiscall with no arguments, ret.
const uintptr_t kModeUpdate = 0x0045A960;
// The CameraModeNew3 vtable. Update is its slot 2, and this vtable is the
// only reference to it.
const uintptr_t kModeVTable = 0x008C7BFC;
// The fields other files read, up to mode+0x2EC, fit in this.
const UINT_PTR kModeReadSize = 0x2F0;
// Zoomer::SetPosition, thiscall (float), ret 4. Sets the value and the
// destination, and zeroes the speeds and the duration, so the next
// Zoomer::Update keeps it.
const uintptr_t kZoomerSetPosition = 0x00441AC0;
// GCamera::GetHeadingAndPitchFromPoints, cdecl (from, to, float* heading,
// float* pitch). Used to measure a turn in the game's own units.
const uintptr_t kHeadingAndPitch = 0x004428D0;
// CameraHelp::CameraHelpCallback, cdecl (int event, LHPoint* point,
// unsigned inputs). The tutorial and the help hints count these events. Read
// from the calls in CameraModeNew3::Update and from the game's own names of
// the events (docs/god-view.md, "7. The game camera"):
//
//   0x300   the camera turned, then 0x301 for a turn key to the left (a key
//           turn of more than 0, 0x0045C772) or 0x302 for one to the right
//   0x303   the camera tilted
//   0x304   the camera zoomed (feature bit 4)
//   0x305, 0x306   a double click jump
//   0x308   a drag of the land, or a move by the keys (feature bit 8)
//
// The 0x100 and 0x200 events are errors and collisions, for the hints. Only
// the 0x300 events reach HelpProfile::Trigger. The point is not read for
// them.
const uintptr_t kCameraHelpCallback = 0x00449140;
const int kHelpTurn = 0x300;
const int kHelpTurnLeft = 0x301;
const int kHelpTurnRight = 0x302;
// Which input turned the camera, for that callback: 0x10 is the middle
// mouse, 8 is a key. Read from the mask the game builds at 0x0045C06A.
const unsigned kInputMiddle = 0x10;
const unsigned kInputKeys = 0x08;
const unsigned kInputMouse = 0x02;

// CameraHelp::EnabledFeatures. Scripts turn camera controls on and off with
// it, and the tutorial does so step by step. Bit 1 lets the camera tilt and
// bit 2 lets it turn.
const uintptr_t kEnabledFeatures = 0x009CDD6C;
const uint32_t kFeatureTilt = 0x1;
const uint32_t kFeatureTurn = 0x2;
// The time the camera takes to reach where it is sent, 0.3 s. One reader, at
// 0x004601BC, and no writers.
const uintptr_t kGlideTime = 0x009CE614;
// The turn and tilt the keys asked for last frame. ProcessKeyMovement fills
// them after the camera update, so at the start of the next update they hold
// exactly the keys. Left is positive.
const uintptr_t kKeyTurn = 0x00C5B0C8;
const uintptr_t kKeyTilt = 0x00C5B0C4;
// Set while the Hand drags the land.
const uintptr_t kHandDragA = 0x00C5B0B4;
const uintptr_t kHandDragB = 0x00C5B0F0;

// The wheel. CMouse::ProcessButtons adds the DirectInput wheel total, 120 for
// a click, to this int and keeps the sum until a reader clears it. Action 3
// is the wheel down and action 4 the wheel up. CameraModeNew3::Update reads
// it at 0x0045B704 only while [mode + 0x2EC] is zero and the follow blend
// Zoomer at 0x00C5B100 is below 0.01. Then it clears it. The zoom it gives is
//
//     acc   -= wheel * [0x009CE61C]        (0.5, one reader, no writers)
//     delta  = S * acc * 0.0015            (0x0045C0CC)
//
// where S is three times the height of the camera above the focus. S is
// between 60 and 2000 (60 and 240 when the camera is below the focus). delta
// is added to the distance at +0x1A8, and the camera moves along its view.
const uintptr_t kWheelCounts = 0x00C5E8D8;
const uintptr_t kWheelScale = 0x009CE61C;
const uintptr_t kFollowBlend = 0x00C5B100;
const int kActionWheelDown = 3;
const int kActionWheelUp = 4;
const float kZoomPerAcc = 0.0015f;
const int kCountsPerClick = 120;
const int kMaxStepCounts = 3 * kCountsPerClick;
const int kMaxPendingCounts = 2 * kMaxStepCounts;
const ULONGLONG kZoomGapMs = 150;

// CameraModeNew3 fields.
const int kModeCamera = 0x04;        // GCamera*
const int kModePositionCopy = 0x0C;  // the mode's copy of the position
const int kModeFocusCopy = 0x18;     // and of the focus
const int kModeFight = 0x44;         // nonzero during a fight
const int kModeYaw = 0x50;           // heading, radians
const int kModeElapsed = 0x78;       // time since a jump, 1.5 s means none
const int kModePivotForced = 0x88;   // byte: a script holds the pivot
const int kModeButtons = 0x8C;       // the buttons held, 1 is the left one
const int kModeHandStatus = 0x1AC;   // what the hold does, 3 is a land drag
const int32_t kButtonLeft = 1;
const int32_t kLandDrag = 3;
const int kModeMouseX = 0xA8;        // the mouse movement this frame
const int kModeFlying = 0x100;       // byte, and +0x101: a long flight
const int kModeFlightPosition = 0x110;
const int kModeFlightFocus = 0x11C;
const int kModePivot = 0x12C;        // the point the camera turns about
const int kModeDistance = 0x1A8;     // camera to focus, the wheel moves it
const int kModeInputOff = 0x2EC;     // nonzero: the wheel is not read
const int kModeLastFocus = 0x1D4;    // the focus to fall back to
// GCamera fields: three Zoomers each, 0x30 bytes apart. A Zoomer holds its
// value at +0 and its destination at +4.
const int kCameraFocus = 0x88;
const int kCameraPosition = 0x118;
const int kZoomerSize = 0x30;

typedef int(__fastcall* IsActionFn)(void* self, void* unused, int action);
typedef void(__fastcall* ModeUpdateFn)(void* self, void* unused);
typedef void(__fastcall* ZoomerSetFn)(void* self, void* unused, float value);
typedef void(__cdecl* HeadingFn)(const float* from, const float* to,
                                 float* heading, float* pitch);
typedef void(__cdecl* HelpFn)(int event, const void* unused, unsigned inputs);

IsActionFn o_IsActionPerformed = nullptr;
ModeUpdateFn o_ModeUpdate = nullptr;

bool g_installed = false;
float g_snap_degrees = 45.0f;  // 0 gives the game's own turning back
bool g_instant_jumps = true;
bool g_glide = false;
DWORD g_blink_ms = 0;

float g_saved_glide = 0.3f;
bool g_glide_changed = false;

// The zoom step: the ratio one wheel click changes the distance by. Zero
// leaves the game's own step.
const float kZoomRatios[4] = {0.0f, 1.12f, 1.20f, 1.30f};
const char* const kZoomNames[4] = {"the game's own step", "small", "medium",
                                   "large"};
int g_zoom_step = 2;
float g_saved_wheel_scale = 0.5f;
bool g_have_wheel_scale = false;
bool g_wheel_scale_changed = false;
// Wheel counts taken from the game and not yet used, and what the game is
// told about the wheel actions during this update.
int32_t g_wheel_pending = 0;
ULONGLONG g_last_zoom_tick = 0;
enum WheelAnswer { kWheelAsIs, kWheelHide, kWheelDown, kWheelUp };
WheelAnswer g_wheel_answer = kWheelAsIs;
bool g_in_mode_update = false;
// The mode whose update runs now, the last one seen, and a count of updates.
// pointer_map.cpp reads them. See ComfortCameraMode.
void* g_mode = nullptr;
void* g_last_mode = nullptr;
uint64_t g_mode_updates = 0;
uint64_t g_zoom_steps = 0;
uint64_t g_wheel_counts = 0;
int g_zoom_logged = 0;

// The middle mouse, as it really is. The game is told it is up. The time it
// was last read is kept, so a camera mode that stops running cannot leave the
// Hand held still.
bool g_middle = false;
ULONGLONG g_middle_tick = 0;
bool g_middle_before = false;
int g_middle_counts = 0;
int g_last_key = 0;
// A snap turn that a controller asked for (1 right, -1 left, 0 none), and when.
// It is used like a key press in the next camera update, and it goes stale
// after kTurnRequestMs, so a request made in a menu does not turn later.
int g_turn_request = 0;
ULONGLONG g_turn_request_tick = 0;
const ULONGLONG kTurnRequestMs = 300;
ULONGLONG g_last_snap_tick = 0;
ULONGLONG g_blink_until = 0;

// How far the mouse moves for one snap, in mouse counts, and the shortest
// time between two snaps from the mouse.
const int kCountsPerSnap = 150;
const ULONGLONG kSnapGapMs = 200;

uint64_t g_snaps = 0;
uint64_t g_refused = 0;
uint64_t g_jumps = 0;
int g_logged = 0;

// The grab of the world: the frames it wrote, the turns and the wheel
// clicks refused while it held, and the jump timer of the last update.
uint64_t g_grab_writes = 0;
uint64_t g_grab_turns_refused = 0;
uint64_t g_grab_wheel_refused = 0;
float g_grab_elapsed = 1.5f;

// The God view: the updates where it wrote the game camera, and the turns
// and the wheel steps it took.
uint64_t g_god_writes = 0;
uint64_t g_god_turns = 0;
uint64_t g_god_wheel = 0;
// The version of the God view that the game camera was last put over.
uint64_t g_god_written = 0;

// THE GAME'S LAND DRAG IN THE GOD VIEW. The camera before the game's update,
// to put back after a land drag (GodDragBefore and GodDragAfter), and the
// latch that tabletop.cpp reads at the main camera call of the same frame.
struct CameraHold {
  float position[3];
  float focus[3];
  float position_copy[3];
  float focus_copy[3];
  float last_focus[3];
  float pivot[3];
  float yaw;
  float distance;
  bool drag_before;
};
CameraHold g_hold;
bool g_have_hold = false;
bool g_god_drag = false;
uint64_t g_god_drag_frames = 0;

// The tilt keys move the ground of the God view, one step for each press.
int g_last_tilt_key = 0;
uint64_t g_god_ground_keys = 0;

// THE LOG OF THE CAMERA HELP EVENTS (h_Help). The 0x300 events by number,
// the others together, and the last event that was logged.
typedef void(__cdecl* HelpHookFn)(int event, const void* point,
                                  unsigned inputs);
HelpHookFn o_Help = nullptr;
uint64_t g_help_counts[16] = {0};
uint64_t g_help_other = 0;
int g_help_last = -1;
unsigned g_help_last_inputs = 0;
int g_help_logs = 0;
const int kMaxHelpLogs = 80;

bool Snapping() { return g_snap_degrees > 0.0f; }

float* Floats(uint8_t* base, int offset) {
  return reinterpret_cast<float*>(base + offset);
}

uint8_t* CameraOf(uint8_t* mode) {
  return *reinterpret_cast<uint8_t**>(mode + kModeCamera);
}

float* ZoomerAt(uint8_t* camera, int first, int axis) {
  return Floats(camera, first + axis * kZoomerSize);
}

void SetZoomer(float* zoomer, float value) {
  const ZoomerSetFn set = reinterpret_cast<ZoomerSetFn>(kZoomerSetPosition);
  set(zoomer, nullptr, value);
}

// Put all six zoomers at their destinations, so the camera is already where
// it was going.
void ArriveNow(uint8_t* camera, float position[3], float focus[3]) {
  for (int axis = 0; axis < 3; ++axis) {
    float* p = ZoomerAt(camera, kCameraPosition, axis);
    float* f = ZoomerAt(camera, kCameraFocus, axis);
    position[axis] = p[1];
    focus[axis] = f[1];
    SetZoomer(p, p[1]);
    SetZoomer(f, f[1]);
  }
}

void CopyPoint(float* to, const float* from) {
  to[0] = from[0];
  to[1] = from[1];
  to[2] = from[2];
}

void StartBlink() {
  if (g_blink_ms > 0) g_blink_until = GetTickCount64() + g_blink_ms;
}

// A JUMP THAT HAS STARTED IS FINISHED NOW.
//
// A double click starts one of two kinds of flight. A near one (150 units or
// less) sends the zoomers to the target and sets the elapsed time to 0.75 s,
// which makes the camera ease there over about 0.9 s. A far one sets the
// elapsed time to 0 and flies through a point above the land, in two legs,
// with the target kept at +0x110 and +0x11C and +0x100 set while it flies.
// Either way the elapsed time goes down, and nothing else makes it go down.
// True when a flight landed in this call.
bool FinishFlight(uint8_t* mode, float elapsed_before) {
  uint8_t* flying = mode + kModeFlying;
  const bool far_flight = flying[0] != 0;
  const float elapsed = *Floats(mode, kModeElapsed);
  if (!far_flight && !(elapsed < elapsed_before)) return false;

  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) return false;
  if (far_flight) {
    const float* to_position = Floats(mode, kModeFlightPosition);
    const float* to_focus = Floats(mode, kModeFlightFocus);
    for (int axis = 0; axis < 3; ++axis) {
      ZoomerAt(camera, kCameraPosition, axis)[1] = to_position[axis];
      ZoomerAt(camera, kCameraFocus, axis)[1] = to_focus[axis];
    }
    flying[0] = 0;
    flying[1] = 0;
  }
  float position[3], focus[3];
  ArriveNow(camera, position, focus);
  *Floats(mode, kModeElapsed) = 1.5f;
  CopyPoint(Floats(mode, kModePositionCopy), position);
  CopyPoint(Floats(mode, kModeFocusCopy), focus);
  ++g_jumps;
  StartBlink();
  SpiritAnchorCameraCut();
  if (g_logged < 8) {
    ++g_logged;
    ModLog("comfort      : %s jump landed at once, at %.1f %.1f %.1f",
           far_flight ? "a far" : "a near", position[0], position[1],
           position[2]);
  }
  return true;
}

// Whether the camera may turn now. The script's own switch comes first: the
// tutorial turns the controls on one at a time, and a turn it has not
// allowed yet must not happen.
bool MayTurn(uint8_t* mode, uint32_t features) {
  if ((features & kFeatureTurn) == 0) return false;
  if (mode[kModePivotForced] != 0) return false;
  if (*reinterpret_cast<int32_t*>(mode + kModeFight) != 0) return false;
  if (*reinterpret_cast<int32_t*>(kHandDragA) != 0) return false;
  if (*reinterpret_cast<int32_t*>(kHandDragB) != 0) return false;
  return true;
}

// Turn a point about the position, about world up. A positive angle turns
// the view to the right: forward goes toward the camera's right.
void TurnAbout(const float centre[3], float point[3], float c, float s) {
  const float dx = point[0] - centre[0];
  const float dz = point[2] - centre[2];
  point[0] = centre[0] + dx * c + dz * s;
  point[2] = centre[2] - dx * s + dz * c;
}

float Wrap(float angle) {
  const float pi = 3.14159265f;
  while (angle > pi) angle -= 2.0f * pi;
  while (angle < -pi) angle += 2.0f * pi;
  return angle;
}

// A SNAP TURN IN PLACE.
//
// The position stays. The focus and the pivot the camera turns about both
// turn about the position, so the next zoom or tilt starts from the new
// heading rather than swinging back. The mode's own copies and its heading
// are brought along, and the tutorial hears about it.
void SnapTurn(uint8_t* mode, int direction, unsigned input) {
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) return;
  float position[3], focus[3];
  ArriveNow(camera, position, focus);

  float* pivot = Floats(mode, kModePivot);
  const HeadingFn heading_of = reinterpret_cast<HeadingFn>(kHeadingAndPitch);
  float heading_before = 0.0f, pitch_before = 0.0f;
  heading_of(position, pivot, &heading_before, &pitch_before);

  const float angle = direction * g_snap_degrees * 3.14159265f / 180.0f;
  const float c = cosf(angle);
  const float s = sinf(angle);
  TurnAbout(position, focus, c, s);
  TurnAbout(position, pivot, c, s);

  float heading_after = 0.0f, pitch_after = 0.0f;
  heading_of(position, pivot, &heading_after, &pitch_after);
  *Floats(mode, kModeYaw) += Wrap(heading_after - heading_before);

  for (int axis = 0; axis < 3; ++axis) {
    SetZoomer(ZoomerAt(camera, kCameraFocus, axis), focus[axis]);
  }
  CopyPoint(Floats(mode, kModePositionCopy), position);
  CopyPoint(Floats(mode, kModeFocusCopy), focus);
  CopyPoint(Floats(mode, kModeLastFocus), focus);

  const HelpFn help = reinterpret_cast<HelpFn>(kCameraHelpCallback);
  help(kHelpTurn, position, input);
  help(direction > 0 ? kHelpTurnRight : kHelpTurnLeft, position, input);

  ++g_snaps;
  g_last_snap_tick = GetTickCount64();
  StartBlink();
  SpiritAnchorCameraCut();
  if (g_logged < 8) {
    ++g_logged;
    ModLog("comfort      : turned %s by %.0f degrees, heading %.3f to %.3f",
           direction > 0 ? "right" : "left", g_snap_degrees, heading_before,
           heading_after);
  }
}

// THE GRAB OF THE WORLD. world_grab.cpp solves the camera from the
// hands, and this writes it where the snap turn writes: the six Zoomers,
// through Zoomer::SetPosition, which also zeroes the speeds so the next
// Zoomer::Update keeps the value, and the mode's copies of the position and
// the focus, its fall back focus, its pivot and its heading. Then the next
// zoom or turn starts from where the grab left the camera, not from a stale
// field. See docs/god-view.md, "5.5 The grab on the game camera".
//
// Before the game's update: the grab checks that nothing else moved the
// camera since it last wrote.
void GrabBefore(uint8_t* mode) {
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) return;
  float position[3], focus[3];
  for (int axis = 0; axis < 3; ++axis) {
    position[axis] = ZoomerAt(camera, kCameraPosition, axis)[1];
    focus[axis] = ZoomerAt(camera, kCameraFocus, axis)[1];
  }
  WorldGrabCheckCamera(position, focus);
}

// After the game's update and any jump: the grab's camera, if a grip holds.
void GrabAfter(uint8_t* mode, uint32_t features) {
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) return;
  GrabCamera now;
  for (int axis = 0; axis < 3; ++axis) {
    now.position[axis] = ZoomerAt(camera, kCameraPosition, axis)[1];
    now.focus[axis] = ZoomerAt(camera, kCameraFocus, axis)[1];
  }
  CopyPoint(now.pivot, Floats(mode, kModePivot));
  now.yaw = *Floats(mode, kModeYaw);
  now.input_off = *reinterpret_cast<int32_t*>(mode + kModeInputOff) != 0;
  now.fight = *reinterpret_cast<int32_t*>(mode + kModeFight) != 0;
  // A jump is in flight while its timer counts up to 1.5 s, or while the far
  // flight flags are set. A timer that stands still is not a flight.
  const float elapsed = *Floats(mode, kModeElapsed);
  now.flying = mode[kModeFlying] != 0 || mode[kModeFlying + 1] != 0 ||
               (elapsed < 1.499f && elapsed != g_grab_elapsed);
  g_grab_elapsed = elapsed;
  now.may_turn = MayTurn(mode, features);
  GrabCamera out;
  if (!WorldGrabSolve(now, &out)) return;
  for (int axis = 0; axis < 3; ++axis) {
    SetZoomer(ZoomerAt(camera, kCameraPosition, axis), out.position[axis]);
    SetZoomer(ZoomerAt(camera, kCameraFocus, axis), out.focus[axis]);
  }
  CopyPoint(Floats(mode, kModePositionCopy), out.position);
  CopyPoint(Floats(mode, kModeFocusCopy), out.focus);
  CopyPoint(Floats(mode, kModeLastFocus), out.focus);
  CopyPoint(Floats(mode, kModePivot), out.pivot);
  *Floats(mode, kModeYaw) = out.yaw;
  ++g_grab_writes;
}

float PointDistance(const float a[3], const float b[3]) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// THE GOD VIEW, after the game's update: the game camera goes over the
// player's place, in the same fields the snap turn and the grab write. The
// heading of the mode turns by the game's own measure, as in SnapTurn. Only
// once for each move of the view by the player, so the game's own moves of
// its camera are not undone. Not while a script holds the input or the
// pivot, while a fight holds the camera, or while a jump flies: the write
// then waits for the next update that allows it.
void GodAfter(uint8_t* mode) {
  if (*reinterpret_cast<int32_t*>(mode + kModeInputOff) != 0) return;
  if (*reinterpret_cast<int32_t*>(mode + kModeFight) != 0) return;
  if (mode[kModePivotForced] != 0) return;
  if (mode[kModeFlying] != 0 || mode[kModeFlying + 1] != 0) return;
  float position[3], focus[3];
  uint64_t version = 0;
  if (!TabletopGodGameCamera(position, focus, &version)) return;
  if (version == g_god_written) return;
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) return;
  g_god_written = version;
  float now_position[3], now_focus[3];
  for (int axis = 0; axis < 3; ++axis) {
    now_position[axis] = ZoomerAt(camera, kCameraPosition, axis)[1];
    now_focus[axis] = ZoomerAt(camera, kCameraFocus, axis)[1];
  }
  if (PointDistance(now_position, position) < 0.01f &&
      PointDistance(now_focus, focus) < 0.01f) {
    TabletopGodCameraWritten(position, focus);
    return;
  }
  float* pivot = Floats(mode, kModePivot);
  const HeadingFn heading_of = reinterpret_cast<HeadingFn>(kHeadingAndPitch);
  float heading_before = 0.0f, heading_after = 0.0f, pitch = 0.0f;
  heading_of(now_position, pivot, &heading_before, &pitch);
  for (int axis = 0; axis < 3; ++axis) {
    SetZoomer(ZoomerAt(camera, kCameraPosition, axis), position[axis]);
    SetZoomer(ZoomerAt(camera, kCameraFocus, axis), focus[axis]);
  }
  CopyPoint(Floats(mode, kModePositionCopy), position);
  CopyPoint(Floats(mode, kModeFocusCopy), focus);
  CopyPoint(Floats(mode, kModeLastFocus), focus);
  CopyPoint(pivot, focus);
  heading_of(position, focus, &heading_after, &pitch);
  *Floats(mode, kModeYaw) += Wrap(heading_after - heading_before);
  ++g_god_writes;
  TabletopGodCameraWritten(position, focus);
  static int said = 0;
  if (said < 8) {
    ++said;
    ModLog("comfort      : the God view put the game camera at %.1f %.1f %.1f, "
           "looking at %.1f %.1f %.1f", position[0], position[1], position[2],
           focus[0], focus[1], focus[2]);
  }
}

// THE TELEPORT OF THE IMMERSIVE VIEW (docs/god-view.md, "The teleport"). The
// move is one cut, made after the game's update of the camera mode: delta is
// added to the six Zoomers, to the copies of the mode, to its fall back focus
// and to its pivot. The heading stays. Not while a script holds the input or
// the pivot, while a fight holds the camera, or while a jump flies: the move
// is then dropped and the log says so.
bool g_teleport_pending = false;
float g_teleport_delta[3] = {0.0f, 0.0f, 0.0f};
uint64_t g_teleport_moves = 0;
uint64_t g_teleport_dropped = 0;

void TeleportAfter(uint8_t* mode) {
  if (!g_teleport_pending) return;
  g_teleport_pending = false;
  const char* why = nullptr;
  if (*reinterpret_cast<int32_t*>(mode + kModeInputOff) != 0) {
    why = "a script holds the camera input";
  } else if (*reinterpret_cast<int32_t*>(mode + kModeFight) != 0) {
    why = "a fight holds the camera";
  } else if (mode[kModePivotForced] != 0) {
    why = "a script holds the pivot";
  } else if (mode[kModeFlying] != 0 || mode[kModeFlying + 1] != 0) {
    why = "a jump flies";
  }
  uint8_t* camera = CameraOf(mode);
  if (why == nullptr && camera == nullptr) why = "there is no camera";
  if (why != nullptr) {
    ++g_teleport_dropped;
    ModLog("teleport     : the camera did not move: %s", why);
    return;
  }
  float before[3];
  for (int axis = 0; axis < 3; ++axis) {
    float* p = ZoomerAt(camera, kCameraPosition, axis);
    float* f = ZoomerAt(camera, kCameraFocus, axis);
    before[axis] = p[1];
    SetZoomer(p, p[1] + g_teleport_delta[axis]);
    SetZoomer(f, f[1] + g_teleport_delta[axis]);
  }
  const int copies[4] = {kModePositionCopy, kModeFocusCopy, kModeLastFocus,
                         kModePivot};
  for (int c = 0; c < 4; ++c) {
    float* v = Floats(mode, copies[c]);
    for (int axis = 0; axis < 3; ++axis) v[axis] += g_teleport_delta[axis];
  }
  ++g_teleport_moves;
  StartBlink();
  SpiritAnchorCameraCut();
  const HelpFn help = reinterpret_cast<HelpFn>(kCameraHelpCallback);
  help(TeleportJumpEvent(), Floats(mode, kModePositionCopy), kInputMouse);
  ModLog("teleport     : the immersive camera moved by %.2f %.2f %.2f, from "
         "%.1f %.1f %.1f to %.1f %.1f %.1f, heading kept", g_teleport_delta[0],
         g_teleport_delta[1], g_teleport_delta[2], before[0], before[1],
         before[2], before[0] + g_teleport_delta[0],
         before[1] + g_teleport_delta[1], before[2] + g_teleport_delta[2]);
}

bool LandDragNow(const uint8_t* mode) {
  return *reinterpret_cast<const int32_t*>(mode + kModeButtons) ==
             kButtonLeft &&
         *reinterpret_cast<const int32_t*>(mode + kModeHandStatus) ==
             kLandDrag;
}

// THE GAME'S LAND DRAG IN THE GOD VIEW (docs/god-view.md, "5.4 What the game's
// land drag does"). The picture does not come from the game camera. So a
// land drag of the game would move a camera that the player does not see,
// away from the player's place. With the mouse the God view pans itself (tabletop.cpp, MousePan),
// and a controller moves the land with the grip. So after each update with
// a land drag, the camera goes back to where it was before the update. The
// drag code of the game still runs. So the game still knows the drag, and
// the tutorial still hears it (event 0x308). The drag starts from the press
// camera on each frame, so it does not add up while its moves are undone.
//
// Before the game's update: keep the camera.
void GodDragBefore(uint8_t* mode) {
  g_have_hold = false;
  if (!TabletopGodOwnsView()) return;
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) return;
  for (int axis = 0; axis < 3; ++axis) {
    g_hold.position[axis] = ZoomerAt(camera, kCameraPosition, axis)[1];
    g_hold.focus[axis] = ZoomerAt(camera, kCameraFocus, axis)[1];
  }
  CopyPoint(g_hold.position_copy, Floats(mode, kModePositionCopy));
  CopyPoint(g_hold.focus_copy, Floats(mode, kModeFocusCopy));
  CopyPoint(g_hold.last_focus, Floats(mode, kModeLastFocus));
  CopyPoint(g_hold.pivot, Floats(mode, kModePivot));
  g_hold.yaw = *Floats(mode, kModeYaw);
  g_hold.distance = *Floats(mode, kModeDistance);
  g_hold.drag_before = LandDragNow(mode);
  g_have_hold = true;
}

// After the game's update: a land drag before or after it puts the camera
// back. The latch tells tabletop.cpp that a land drag holds now.
void GodDragAfter(uint8_t* mode) {
  const bool drag_now = g_have_hold && LandDragNow(mode);
  g_god_drag = drag_now;
  if (!g_have_hold || !(drag_now || g_hold.drag_before)) return;
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) return;
  for (int axis = 0; axis < 3; ++axis) {
    SetZoomer(ZoomerAt(camera, kCameraPosition, axis), g_hold.position[axis]);
    SetZoomer(ZoomerAt(camera, kCameraFocus, axis), g_hold.focus[axis]);
  }
  CopyPoint(Floats(mode, kModePositionCopy), g_hold.position_copy);
  CopyPoint(Floats(mode, kModeFocusCopy), g_hold.focus_copy);
  CopyPoint(Floats(mode, kModeLastFocus), g_hold.last_focus);
  CopyPoint(Floats(mode, kModePivot), g_hold.pivot);
  *Floats(mode, kModeYaw) = g_hold.yaw;
  *Floats(mode, kModeDistance) = g_hold.distance;
  ++g_god_drag_frames;
}

// THE GROUND BY THE TILT KEYS (docs/god-view.md, "3. The ground height"). In the God view
// the game's tilt keys move the ground one step for each press: a tilt of
// more than 0 raises the ground, and a tilt of less than 0 lowers it. The
// game camera does not tilt, because the God view does not show it. This
// runs before the game's update, which uses the keys of the last frame.
void GodTiltKeys() {
  float* key_tilt = reinterpret_cast<float*>(kKeyTilt);
  const int held = *key_tilt > 0.0f ? 1 : (*key_tilt < 0.0f ? -1 : 0);
  const int last = g_last_tilt_key;
  g_last_tilt_key = held;
  if (!TabletopGodOwnsView()) return;
  *key_tilt = 0.0f;
  if (held == 0 || held == last) return;
  // While a grip holds the world, the hands move it. The key is refused.
  if (ControllersGrabHeld()) {
    ++g_grab_turns_refused;
    return;
  }
  if (TabletopGodStep(0, held)) {
    ++g_god_ground_keys;
    static int said = 0;
    if (said < 8) {
      ++said;
      ModLog("comfort      : a tilt key asks the God view to move the ground "
             "%s", held > 0 ? "up" : "down");
    }
  }
}

// THE GAME MOVES ITS OWN CAMERA (docs/god-view.md, "15. The place at a camera
// of the game").
//
// In the God view the picture does not come from the game camera, and the
// game camera stays where the view put it. When the game itself puts its
// camera at a new place (a new game, a load, a jump to the Temple, a script
// that sets the camera), the player must go to that place too. This watch
// finds those moves. It reads the destinations of the six Zoomers at three
// points of each update of the player's camera mode:
//
//   before the game's update   A change since the last update came from
//                              outside the update: a script, a load, a key.
//                              A new mode object is a change too.
//   after the game's update    A change is the game's own: a jump, or a
//                              camera that goes to its place.
//   after the writes of the mod  These are the base for the next update.
//
// A MOVE OF THE PLAYER IS NOT COUNTED. In an update with a camera event of
// the player (a turn, a tilt, a zoom, a drag or a move by the keys:
// CameraHelpCallback 0x300 to 0x304 and 0x308) the change is the player's.
// A write of the mod (a step of the God view, the grab, a teleport, a snap
// turn) is the player's too, and it ends a move of the game that is not at
// rest yet.
//
// THE GAME BRINGS ITS CAMERA TO REST AFTER A WRITE. After each write of the
// mod, and after the player's camera mode gets the camera back from a story,
// the game fits the camera to the land again in its own update: it puts the
// focus on the land along the camera line, and it pulls the camera in when a
// hill hides the focus. That moves the focus by up to about 26 units and
// the position by up to about 190 units, in about 0.25 to 1.3 s, and the
// heading stays. That is the end of the player's move, or of the story, and not a
// move of the game. So changes in the update that follow such a moment with
// no rest between them are not counted (settle). A change from outside the
// update, a new mode and a jump are always counted.
//
// ONE MOVE, ONE CUT. The changes of the game make one move. The move ends
// when the camera is at rest: no change for kWatchRestMs and no flight. A
// jump that landed at once (FinishFlight) needs kWatchLandedRestMs only: the
// game puts the focus on the land in its next update, and the look point is
// read after that (read at once, one press of the Temple key looks like a new place
// 6.9 units away). Then tabletop.cpp gets the camera, once.
// A camera that the game moves for many frames gives one cut at its end,
// never a glide.
//
// A SMALL MOVE IS NOT A NEW PLACE. The view goes to the land point that the
// camera looks at (TabletopGodLookPoint) and takes its heading, so only
// those two count. One press of the game's Temple key pulls the camera back
// at its place: the position moves 41.7 units, and the focus 2.6 units. So a
// new place needs one of these: the heading turned kWatchTurnDegrees, or the
// look point moved kWatchLookShare of the camera distance (and
// kWatchMoveUnits at the least).
//
// When the camera is at rest and the view stays, tabletop.cpp gets the
// camera too (TabletopGodCameraWritten): it is the game camera of the view
// of now.
const ULONGLONG kWatchRestMs = 250;
const ULONGLONG kWatchLandedRestMs = 100;
const float kWatchSame = 0.01f;
const float kWatchMoveUnits = 5.0f;
const float kWatchTurnDegrees = 5.0f;
const float kWatchLookShare = 0.05f;
enum WatchCause { kWatchInside, kWatchOutside, kWatchJump, kWatchMode,
                  kWatchCauses };
const char* const kWatchCauseNames[kWatchCauses] = {
    "the game moved its camera in its update",
    "the game set its camera between two updates",
    "a jump of the game camera",
    "a new camera mode of the player",
};
struct CameraWatch {
  const uint8_t* mode;     // the mode of the base, nullptr for no base
  float base_position[3];  // the destinations after the last update
  float base_focus[3];
  ULONGLONG base_ms;
  bool live;               // the God view owns the view in this update
  bool have_after;
  float before_position[3];  // before the game's update
  float before_focus[3];
  float after_position[3];   // after it, before the writes of the mod
  float after_focus[3];
  bool settle;             // the game brings its camera to rest after a write
  ULONGLONG settle_ms;     // the last change of that kind
  bool moving;             // a move of the game that is not at rest yet
  bool landed;             // a jump of this move landed at once
  int cause;               // its first cause
  float from_position[3];  // the destinations before the move
  float from_focus[3];
  ULONGLONG start_ms;
  ULONGLONG change_ms;
  uint64_t start_update;
  int changes[kWatchCauses];
  unsigned events;         // the game's camera events in the move
  bool input_off;          // a script held the camera input at a change
};
CameraWatch g_watch = {};
// The camera events that the game sent inside its update of this frame: bit n
// is the event 0x300 + n.
unsigned g_update_events = 0;
const unsigned kPlayerEvents = 0x11F;  // 0x300 to 0x304, and 0x308
uint64_t g_watch_moves = 0;
uint64_t g_watch_small = 0;
uint64_t g_watch_dropped = 0;
uint64_t g_watch_player = 0;
uint64_t g_watch_settle = 0;
int g_watch_logs = 0;
const int kMaxWatchLogs = 60;

void WatchRead(uint8_t* camera, float position[3], float focus[3]) {
  for (int axis = 0; axis < 3; ++axis) {
    position[axis] = ZoomerAt(camera, kCameraPosition, axis)[1];
    focus[axis] = ZoomerAt(camera, kCameraFocus, axis)[1];
  }
}

bool WatchSame(const float a_position[3], const float a_focus[3],
               const float b_position[3], const float b_focus[3]) {
  for (int axis = 0; axis < 3; ++axis) {
    if (fabsf(a_position[axis] - b_position[axis]) > kWatchSame) return false;
    if (fabsf(a_focus[axis] - b_focus[axis]) > kWatchSame) return false;
  }
  return true;
}

bool WatchFlying(const uint8_t* mode) {
  return mode[kModeFlying] != 0 || mode[kModeFlying + 1] != 0;
}

// One change that the game made. The first one starts a move.
void WatchChange(uint8_t* mode, int cause, const float from_position[3],
                 const float from_focus[3]) {
  const ULONGLONG now = GetTickCount64();
  const bool input_off =
      *reinterpret_cast<const int32_t*>(mode + kModeInputOff) != 0;
  if (!g_watch.moving) {
    g_watch.moving = true;
    g_watch.cause = cause;
    CopyPoint(g_watch.from_position, from_position);
    CopyPoint(g_watch.from_focus, from_focus);
    g_watch.start_ms = now;
    g_watch.start_update = g_mode_updates;
    for (int& n : g_watch.changes) n = 0;
    g_watch.events = 0;
    g_watch.input_off = false;
    g_watch.landed = false;
  }
  g_watch.settle = false;
  ++g_watch.changes[cause];
  g_watch.events |= g_update_events;
  g_watch.input_off = g_watch.input_off || input_off;
  g_watch.change_ms = now;
}

// Before the game's update. A camera that is not where the last update left
// it was set from outside the update.
void WatchBefore(uint8_t* mode) {
  g_update_events = 0;
  g_watch.have_after = false;
  uint8_t* camera = CameraOf(mode);
  const bool was_live = g_watch.live;
  g_watch.live = camera != nullptr && TabletopGodOwnsView();
  if (!g_watch.live) {
    g_watch.moving = false;
    g_watch.settle = false;
    return;
  }
  // The view is back after a story, a load or Ctrl F11: the player's camera
  // mode has the camera again and brings it to rest.
  if (!was_live) {
    g_watch.settle = true;
    g_watch.settle_ms = GetTickCount64();
  }
  WatchRead(camera, g_watch.before_position, g_watch.before_focus);
  if (g_watch.mode == nullptr) return;
  if (g_watch.mode != mode) {
    WatchChange(mode, kWatchMode, g_watch.base_position, g_watch.base_focus);
  } else if (!WatchSame(g_watch.before_position, g_watch.before_focus,
                        g_watch.base_position, g_watch.base_focus)) {
    WatchChange(mode, kWatchOutside, g_watch.base_position,
                g_watch.base_focus);
  }
}

// After the game's update, the land drag and a jump that landed at once.
void WatchAfterGame(uint8_t* mode, bool jumped) {
  if (!g_watch.live) return;
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) return;
  WatchRead(camera, g_watch.after_position, g_watch.after_focus);
  g_watch.have_after = true;
  if (!jumped && WatchSame(g_watch.after_position, g_watch.after_focus,
                           g_watch.before_position, g_watch.before_focus)) {
    return;
  }
  if (!jumped && (g_update_events & kPlayerEvents) != 0) {
    ++g_watch_player;
    return;
  }
  // The game brings its camera to rest after a write of the mod, or after it
  // got the camera back.
  if (!jumped && g_watch.settle && !g_watch.moving) {
    g_watch.settle_ms = GetTickCount64();
    ++g_watch_settle;
    return;
  }
  WatchChange(mode, jumped ? kWatchJump : kWatchInside,
              g_watch.before_position, g_watch.before_focus);
  // A jump that landed at once needs only a short rest.
  if (jumped) g_watch.landed = true;
}

// The turn of the camera between two poses, about up, in degrees. 0 when a
// pose looks straight down.
float WatchTurn(const float a_position[3], const float a_focus[3],
                const float b_position[3], const float b_focus[3]) {
  const float ax = a_focus[0] - a_position[0];
  const float az = a_focus[2] - a_position[2];
  const float bx = b_focus[0] - b_position[0];
  const float bz = b_focus[2] - b_position[2];
  if (ax * ax + az * az < 1e-6f || bx * bx + bz * bz < 1e-6f) return 0.0f;
  return fabsf(Wrap(atan2f(bx, bz) - atan2f(ax, az))) * 57.29578f;
}

// At the end of the hook, after the writes of the mod. The destinations are
// the base of the next update. A move of the game that is at rest goes to
// tabletop.cpp.
void WatchEnd(uint8_t* mode) {
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr) {
    g_watch.mode = nullptr;
    g_watch.moving = false;
    g_watch.settle = false;
    return;
  }
  float position[3], focus[3];
  WatchRead(camera, position, focus);
  const ULONGLONG now = GetTickCount64();
  // The mode of the base changes only while the view is up. So a mode
  // object that the game makes while the view stands aside (a load) is a
  // new mode at the first update with the view.
  if (g_watch.live || g_watch.mode == nullptr) g_watch.mode = mode;
  CopyPoint(g_watch.base_position, position);
  CopyPoint(g_watch.base_focus, focus);
  g_watch.base_ms = now;
  if (!g_watch.live) return;
  // The mod wrote the camera after the game's update: the player moved the
  // view, and the game camera went over the player's place. The game brings
  // it to rest in its next updates.
  if (g_watch.have_after &&
      !WatchSame(position, focus, g_watch.after_position,
                 g_watch.after_focus)) {
    if (g_watch.moving) {
      ++g_watch_dropped;
      if (g_watch_logs < kMaxWatchLogs) {
        ++g_watch_logs;
        ModLog("comfort      : the player moved the view, so the move of the "
               "game camera is dropped, update %llu", g_mode_updates);
      }
    }
    g_watch.moving = false;
    g_watch.settle = true;
    g_watch.settle_ms = now;
    return;
  }
  if (!g_watch.moving) {
    // At rest after a write: this is the game camera of the view of now.
    if (g_watch.settle && now - g_watch.settle_ms >= kWatchRestMs) {
      g_watch.settle = false;
      TabletopGodCameraWritten(position, focus);
    }
    return;
  }
  if (WatchFlying(mode)) {
    g_watch.change_ms = now;
    g_watch.landed = false;
  }
  if (now - g_watch.change_ms <
      (g_watch.landed ? kWatchLandedRestMs : kWatchRestMs)) {
    return;
  }
  g_watch.moving = false;
  g_watch.landed = false;
  float look_from[3], look_to[3];
  TabletopGodLookPoint(g_watch.from_position, g_watch.from_focus, look_from);
  TabletopGodLookPoint(position, focus, look_to);
  const float dx = look_to[0] - look_from[0], dz = look_to[2] - look_from[2];
  const float look_moved = sqrtf(dx * dx + dz * dz);
  const float turned = WatchTurn(g_watch.from_position, g_watch.from_focus,
                                 position, focus);
  float look_limit = kWatchLookShare * PointDistance(position, look_to);
  if (look_limit < kWatchMoveUnits) look_limit = kWatchMoveUnits;
  const bool place = turned >= kWatchTurnDegrees || look_moved >= look_limit;
  if (place) {
    ++g_watch_moves;
  } else {
    ++g_watch_small;
  }
  // One line for each new place. A small move has a line only the first
  // times, and the report counts all of them.
  static int small_logs = 0;
  if (place || small_logs < 4) {
    if (!place) ++small_logs;
    if (g_watch_logs < kMaxWatchLogs) {
      ++g_watch_logs;
      ModLog("comfort      : the game moved its own camera (%s). It was at "
             "%.1f %.1f %.1f and looked at %.1f %.1f %.1f. Now it is at rest "
             "at %.1f %.1f %.1f and looks at %.1f %.1f %.1f, update %llu: the "
             "land point that it looks at went %.1f units, from %.1f %.1f to "
             "%.1f %.1f, and the camera turned %.1f degrees. The move took "
             "%llu ms and %llu updates. Changes: %d in the update, %d between "
             "updates, %d jumps, %d new modes. Camera events 0x%03X, the "
             "camera input %s. %s", kWatchCauseNames[g_watch.cause],
             g_watch.from_position[0], g_watch.from_position[1],
             g_watch.from_position[2], g_watch.from_focus[0],
             g_watch.from_focus[1], g_watch.from_focus[2], position[0],
             position[1], position[2], focus[0], focus[1], focus[2],
             g_mode_updates, look_moved, look_from[0], look_from[2],
             look_to[0], look_to[2], turned, now - g_watch.start_ms,
             g_mode_updates - g_watch.start_update,
             g_watch.changes[kWatchInside], g_watch.changes[kWatchOutside],
             g_watch.changes[kWatchJump], g_watch.changes[kWatchMode],
             g_watch.events, g_watch.input_off ? "was held by a script"
                                                : "was the player's",
             place ? "This is a new place"
                   : "This is not a new place, so the view stays");
    }
  }
  if (place) {
    TabletopGodGameMoved(kWatchCauseNames[g_watch.cause], position, focus);
  } else {
    TabletopGodCameraWritten(position, focus);
  }
}

// THE LOG OF THE CAMERA HELP EVENTS. Every event that the game or the mod
// sends comes through here. A change of the event or of the inputs is
// logged, up to kMaxHelpLogs lines, and the report counts all of them. A
// test reads the events that a zoom, a drag or a turn sends from this log.
void __cdecl h_Help(int event, const void* point, unsigned inputs) {
  if ((event & 0xF00) == 0x300 && (event & 0xFF) < 16) {
    ++g_help_counts[event & 0xF];
    // For the watch of the game camera: the events of the game's own update.
    if (g_in_mode_update) g_update_events |= 1u << (event & 0xF);
  } else {
    ++g_help_other;
  }
  if ((event != g_help_last || inputs != g_help_last_inputs) &&
      g_help_logs < kMaxHelpLogs) {
    ++g_help_logs;
    ModLog("comfort      : camera help event 0x%03X, inputs 0x%02X", event,
           inputs);
  }
  g_help_last = event;
  g_help_last_inputs = inputs;
  o_Help(event, point, inputs);
}

// ZOOM IN STEPS.
//
// One click of the wheel moved the camera about 6 percent of its distance at
// the usual pitch. So it took 11 clicks to halve the distance, and a fast
// scroll felt like a slide. Both changes below are around the game's own
// zoom code. So its 3.1 unit floor, its script switch (feature bit 4) and
// the tutorial's "zoomed" event all stay.
//
//   A FIXED RATIO. For the frame of a step, the wheel scale is set so that
//   the step takes the distance to d * ratio^-clicks. A step is never
//   smaller than the game's own step, so the camera does not crawl when it
//   is near the land. The scale goes back to the game's value after the
//   update.
//
//   JOINED CLICKS. The wheel counts are taken from the game and kept here.
//   The game clears them at 0x0045B77E whether or not a wheel action is
//   performed, so they cannot be left with it. The first click is used at
//   once. After that, at most one step happens every kZoomGapMs, with at
//   most three clicks in a step. In between, the game is told the wheel did
//   not move. At most two steps of clicks are kept, and a turn of the wheel
//   the other way drops them.
//
// A key bound to a wheel action gives no counts, so it still works as the
// game has it.
float GameZoomScale(float height) {
  float scale = 3.0f * (height >= 0.0f ? height : -height);
  const float most = height >= 0.0f ? 2000.0f : 240.0f;
  if (scale < 60.0f) scale = 60.0f;
  if (scale > most) scale = most;
  return scale;
}

bool WheelGateOpen(uint8_t* mode) {
  return *reinterpret_cast<int32_t*>(mode + kModeInputOff) == 0 &&
         *reinterpret_cast<float*>(kFollowBlend) < 0.01f;
}

// Set the wheel scale so that this frame's counts make one step of the
// ratio. False leaves the game's own scale.
bool SetWheelScale(uint8_t* mode, int32_t counts) {
  uint8_t* camera = CameraOf(mode);
  if (camera == nullptr || counts == 0) return false;
  // The points the game uses: the Zoomer destinations, read at 0x0045AA45.
  // The distance field at +0x1A8 can be stale until a zoom starts. Then
  // UpdateClickParams sets it from these same two points (0x00459AC8). So
  // it is measured here the same way.
  float offset[3];
  for (int axis = 0; axis < 3; ++axis) {
    offset[axis] = ZoomerAt(camera, kCameraPosition, axis)[1] -
                   ZoomerAt(camera, kCameraFocus, axis)[1];
  }
  const float height = offset[1];
  const float distance = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] +
                               offset[2] * offset[2]);
  if (!(distance > 0.0f)) return false;
  const float clicks = static_cast<float>(counts) / kCountsPerClick;
  const float game_scale = GameZoomScale(height);
  float target = distance * powf(kZoomRatios[g_zoom_step], -clicks);
  // Near the land a ratio is a small distance. Never go slower than the
  // game's own step there.
  const float game_step = game_scale * kZoomPerAcc * g_saved_wheel_scale *
                          fabsf(static_cast<float>(counts));
  if (fabsf(target - distance) < game_step) {
    target = counts > 0 ? distance - game_step : distance + game_step;
  }
  const float acc = (target - distance) / (kZoomPerAcc * game_scale);
  float* scale = reinterpret_cast<float*>(kWheelScale);
  *scale = -acc / static_cast<float>(counts);
  g_wheel_scale_changed = true;
  if (g_zoom_logged < 18) {
    ++g_zoom_logged;
    ModLog("comfort      : zoom step of %d counts, distance %.1f to %.1f",
           counts, distance, target);
  }
  return true;
}

// Before the game's update: decide what it hears about the wheel.
void PrepareWheel(uint8_t* mode) {
  g_wheel_answer = kWheelAsIs;
  // While a grip holds the world, the two hands zoom. A wheel step would add
  // to the distance in the same frame, so the game hears no wheel, and the
  // clicks are dropped, not kept for later.
  if (ControllersGrabHeld()) {
    int32_t* counts = reinterpret_cast<int32_t*>(kWheelCounts);
    if (*counts != 0) ++g_grab_wheel_refused;
    *counts = 0;
    g_wheel_pending = 0;
    g_wheel_answer = kWheelHide;
    return;
  }
  // IN THE GOD VIEW a wheel click is one zoom step of the view, at most one
  // every kZoomGapMs. The game hears no wheel, so its camera does not zoom.
  if (TabletopGodOwnsView()) {
    int32_t* counts = reinterpret_cast<int32_t*>(kWheelCounts);
    const int32_t fresh = *counts;
    *counts = 0;
    g_wheel_pending = 0;
    g_wheel_answer = kWheelHide;
    const ULONGLONG now = GetTickCount64();
    if (fresh != 0 && now - g_last_zoom_tick >= kZoomGapMs &&
        TabletopGodStep(fresh > 0 ? 1 : -1, 0)) {
      g_last_zoom_tick = now;
      ++g_god_wheel;
    }
    return;
  }
  if (g_zoom_step <= 0 || !g_have_wheel_scale) {
    g_wheel_pending = 0;
    return;
  }
  // While the gate is closed, the game does not read or clear the wheel int,
  // but new clicks still add to it. When the gate opens, they come here as
  // fresh counts and make steps, as the game would use them. Only the clicks
  // held here are dropped, so they do not add to that zoom.
  if (!WheelGateOpen(mode)) {
    g_wheel_pending = 0;
    return;
  }
  int32_t* counts = reinterpret_cast<int32_t*>(kWheelCounts);
  const int32_t fresh = *counts;
  if (fresh == 0 && g_wheel_pending == 0) return;
  if (fresh != 0) {
    g_wheel_counts += static_cast<uint64_t>(fresh > 0 ? fresh : -fresh);
    if (g_zoom_logged < 18) {
      ++g_zoom_logged;
      ModLog("comfort      : the wheel gave %d counts", fresh);
    }
    // A turn of the wheel the other way replaces the clicks held here.
    if (g_wheel_pending != 0 && (fresh > 0) != (g_wheel_pending > 0)) {
      g_wheel_pending = 0;
    }
  }
  g_wheel_pending += fresh;
  if (g_wheel_pending > kMaxPendingCounts) g_wheel_pending = kMaxPendingCounts;
  if (g_wheel_pending < -kMaxPendingCounts) {
    g_wheel_pending = -kMaxPendingCounts;
  }
  *counts = 0;
  // Hide the wheel actions too: with the int at zero, a performed wheel
  // action would make the game zoom by its own 120 counts (0x0045B748).
  if (g_wheel_pending == 0) {
    g_wheel_answer = kWheelHide;
    return;
  }
  const ULONGLONG now = GetTickCount64();
  if (now - g_last_zoom_tick < kZoomGapMs) {
    g_wheel_answer = kWheelHide;
    return;
  }
  int32_t step = g_wheel_pending;
  if (step > kMaxStepCounts) step = kMaxStepCounts;
  if (step < -kMaxStepCounts) step = -kMaxStepCounts;
  g_wheel_pending -= step;
  SetWheelScale(mode, step);
  *counts = step;
  g_wheel_answer = step > 0 ? kWheelUp : kWheelDown;
  g_last_zoom_tick = now;
  ++g_zoom_steps;
}

// The game's update, with the wheel prepared around it.
void RunGameUpdate(uint8_t* mode, void* unused) {
  PrepareWheel(mode);
  const bool stepped =
      g_wheel_answer == kWheelUp || g_wheel_answer == kWheelDown;
  g_in_mode_update = true;
  void* const outer_mode = g_mode;
  g_mode = mode;
  o_ModeUpdate(mode, unused);
  g_mode = outer_mode;
  g_in_mode_update = false;
  // The update sets the distance from the new camera at 0x0046009E.
  if (stepped && g_zoom_logged < 18) {
    ++g_zoom_logged;
    ModLog("comfort      : zoom step done, distance now %.1f",
           *Floats(mode, kModeDistance));
  }
  g_wheel_answer = kWheelAsIs;
  if (g_wheel_scale_changed) {
    *reinterpret_cast<float*>(kWheelScale) = g_saved_wheel_scale;
    g_wheel_scale_changed = false;
  }
}

// The game never sees the middle mouse while snapping is on, so it never
// turns or tilts the camera with it, and never freezes the pointer for it.
// During the camera update, the wheel actions say what PrepareWheel decided.
int __fastcall h_IsActionPerformed(void* self, void* unused, int action) {
  const int real = o_IsActionPerformed(self, unused, action);
  if (action == kActionMiddle && Snapping()) {
    g_middle = real != 0;
    g_middle_tick = GetTickCount64();
    return 0;
  }
  if ((action == kActionWheelDown || action == kActionWheelUp) &&
      g_in_mode_update) {
    switch (g_wheel_answer) {
      case kWheelHide:
        return 0;
      case kWheelDown:
        return action == kActionWheelDown ? 1 : 0;
      case kWheelUp:
        return action == kActionWheelUp ? 1 : 0;
      default:
        break;
    }
  }
  return real;
}

// One update of the player's camera mode: the game's update, and the writes
// of the mod around it. h_ModeUpdate puts the watch of the game camera
// around all of it.
void ModeUpdate(uint8_t* mode, void* unused) {
  // A double click of the mouse in the God view teleports the view. It is
  // taken before the game reads it, so the game does not fly its camera.
  TeleportMouseDoubleClick(mode);
  GrabBefore(mode);
  GodTiltKeys();
  GodDragBefore(mode);
  if (!Snapping() && !g_instant_jumps) {
    const uint32_t features =
        *reinterpret_cast<const uint32_t*>(kEnabledFeatures);
    RunGameUpdate(mode, unused);
    GodDragAfter(mode);
    WatchAfterGame(mode, false);
    GrabAfter(mode, features);
    GodAfter(mode);
    TeleportAfter(mode);
    return;
  }

  uint32_t* features = reinterpret_cast<uint32_t*>(kEnabledFeatures);
  const uint32_t features_before = *features;
  int key = 0;
  if (Snapping()) {
    // A turn key counts once per press.
    float* key_turn = reinterpret_cast<float*>(kKeyTurn);
    const int held = *key_turn > 0.0f ? -1 : (*key_turn < 0.0f ? 1 : 0);
    if (held != 0 && held != g_last_key) key = held;
    g_last_key = held;
    *key_turn = 0.0f;
    *reinterpret_cast<float*>(kKeyTilt) = 0.0f;
    // No smooth turn and no tilt from any other path while the game runs
    // its update: the keys, grabbing the sky, or both buttons.
    *features = features_before & ~(kFeatureTilt | kFeatureTurn);
  }

  const float elapsed_before = *Floats(mode, kModeElapsed);
  RunGameUpdate(mode, unused);
  GodDragAfter(mode);

  if (Snapping()) {
    // Put back only the two bits, in case the update changed any other.
    *features = (*features & ~(kFeatureTilt | kFeatureTurn)) |
                (features_before & (kFeatureTilt | kFeatureTurn));
  }
  const bool jumped = g_instant_jumps && FinishFlight(mode, elapsed_before);
  WatchAfterGame(mode, jumped);
  GrabAfter(mode, features_before);
  GodAfter(mode);
  TeleportAfter(mode);
  if (!Snapping()) return;

  // The middle mouse turns once for every so many counts sideways.
  if (g_middle && !g_middle_before) g_middle_counts = 0;
  g_middle_before = g_middle;
  int direction = key;
  unsigned input = kInputKeys;
  bool from_controller = false;
  if (g_turn_request != 0) {
    if (direction == 0 &&
        GetTickCount64() - g_turn_request_tick <= kTurnRequestMs) {
      direction = g_turn_request;
      from_controller = true;
    }
    g_turn_request = 0;
  }
  if (g_middle) {
    g_middle_counts += *reinterpret_cast<int32_t*>(mode + kModeMouseX);
    if ((g_middle_counts >= kCountsPerSnap ||
         g_middle_counts <= -kCountsPerSnap) &&
        GetTickCount64() - g_last_snap_tick >= kSnapGapMs) {
      direction = g_middle_counts > 0 ? 1 : -1;
      input = kInputMiddle;
      g_middle_counts = 0;
    }
  }
  if (direction == 0) return;
  // While a grip holds the world, the two hands turn it. A snap turn from a
  // key, the middle mouse or the stick is refused and counted.
  if (ControllersGrabHeld()) {
    ++g_grab_turns_refused;
    return;
  }
  if (!MayTurn(mode, features_before)) {
    ++g_refused;
    return;
  }
  // In the God view the view turns about the head, at the next main camera
  // call, and the game camera follows it in the next update. The tutorial
  // still hears the turn.
  if (TabletopGodTurn(direction, g_snap_degrees)) {
    const HelpFn help = reinterpret_cast<HelpFn>(kCameraHelpCallback);
    const float* position = Floats(mode, kModePositionCopy);
    help(kHelpTurn, position, input);
    help(direction > 0 ? kHelpTurnRight : kHelpTurnLeft, position, input);
    ++g_god_turns;
    g_last_snap_tick = GetTickCount64();
    if (from_controller) ControllersSnapTurned();
    return;
  }
  SnapTurn(mode, direction, input);
  if (from_controller) ControllersSnapTurned();
}

// TESTS WITH NO KEY PRESS, for a desktop run (docs/god-view.md, "15.9 Tests").
//
// spawn_test_temple=N: at update N of the player's camera mode, the mod
// calls the game's own zoom to the Temple, CameraModeNew3::ZoomToCitadel
// 0x00457B60, as the key of the game does (GGame::DoAction, through
// fn_0064A7A0). It is thiscall (x, z, distance, pitch, flag) and ends in
// ret 0x14. The land point is spawn_test_x and spawn_test_z, and the
// distance and the pitch are the game's own (CameraModeNew3::CitadelDistance
// and CitadelPitch). The key passes the flag 1: its first press pulls the
// camera back at its place, and a second press in 500 ms goes to the Temple
// (the test at 0x00457BB9). The test passes spawn_test_flag: 0 goes at once.
//
// spawn_test_toggle=N: at update N the test key of god_spawn acts, and
// kTestToggleBack updates later it acts again.
const uint64_t kTestToggleBack = 300;
const uintptr_t kZoomToCitadel = 0x00457B60;
const uintptr_t kCitadelDistance = 0x009CE694;
const uintptr_t kCitadelPitch = 0x009CE698;
typedef void(__fastcall* ZoomToCitadelFn)(void* self, void* unused, float x,
                                          float z, float distance, float pitch,
                                          int flag);
uint64_t g_test_temple_update = 0;
float g_test_temple_x = 0.0f;
float g_test_temple_z = 0.0f;
uint64_t g_test_toggle_update = 0;
int g_test_temple_flag = 0;

void SpawnTests(uint8_t* mode) {
  if (g_test_temple_update != 0 && g_mode_updates == g_test_temple_update) {
    const float distance = *reinterpret_cast<const float*>(kCitadelDistance);
    const float pitch = *reinterpret_cast<const float*>(kCitadelPitch);
    ModLog("comfort      : a test calls the game's zoom to the Temple, to "
           "%.0f %.0f, %.0f units away and %.1f degrees down, with the flag "
           "%d, update %llu", g_test_temple_x, g_test_temple_z, distance,
           pitch * 57.29578f, g_test_temple_flag, g_mode_updates);
    reinterpret_cast<ZoomToCitadelFn>(kZoomToCitadel)(
        mode, nullptr, g_test_temple_x, g_test_temple_z, distance, pitch,
        g_test_temple_flag);
  }
  if (g_test_toggle_update != 0 &&
      (g_mode_updates == g_test_toggle_update ||
       g_mode_updates == g_test_toggle_update + kTestToggleBack)) {
    ModLog("comfort      : a test presses the test key of god_spawn, update "
           "%llu", g_mode_updates);
    TabletopGodSpawnToggle();
  }
}

void __fastcall h_ModeUpdate(void* self, void* unused) {
  uint8_t* mode = static_cast<uint8_t*>(self);
  if (mode == nullptr) {
    o_ModeUpdate(self, unused);
    return;
  }
  g_last_mode = mode;
  ++g_mode_updates;
  SpawnTests(mode);
  WatchBefore(mode);
  ModeUpdate(mode, unused);
  WatchEnd(mode);
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

// The glide is one number the game reads and never writes.
void ApplyGlide() {
  float* glide = reinterpret_cast<float*>(kGlideTime);
  if (!g_glide_changed) {
    g_saved_glide = *glide;
  }
  if (g_glide) {
    if (g_glide_changed) *glide = g_saved_glide;
    g_glide_changed = false;
  } else {
    *glide = 0.0f;
    g_glide_changed = true;
  }
}

}  // namespace

void ComfortCameraSetOptions(int snap_degrees, bool instant_jumps, bool glide,
                             int blink_ms) {
  g_snap_degrees = static_cast<float>(snap_degrees > 0 ? snap_degrees : 0);
  g_instant_jumps = instant_jumps;
  g_glide = glide;
  g_blink_ms = static_cast<DWORD>(blink_ms > 0 ? blink_ms : 0);
  if (g_installed) ApplyGlide();
  if (g_snap_degrees > 0.0f) {
    ModLog("comfort      : turn in steps of %.0f degrees, and no tilt from "
           "the middle mouse", g_snap_degrees);
  } else {
    ModLog("comfort      : the game's own smooth turning and tilting");
  }
  ModLog("comfort      : jumps %s, the camera %s, blink %u ms",
         instant_jumps ? "land at once" : "fly as the game flies them",
         glide ? "glides" : "stops with the mouse", g_blink_ms);
}

void ComfortCameraInstall() {
  if (g_installed) return;
  g_installed = true;
  Hook(kIsActionPerformed, reinterpret_cast<void*>(&h_IsActionPerformed),
       reinterpret_cast<void**>(&o_IsActionPerformed),
       "ControlMap::IsActionPerformed");
  Hook(kModeUpdate, reinterpret_cast<void*>(&h_ModeUpdate),
       reinterpret_cast<void**>(&o_ModeUpdate), "CameraModeNew3::Update");
  Hook(kCameraHelpCallback, reinterpret_cast<void*>(&h_Help),
       reinterpret_cast<void**>(&o_Help), "CameraHelp::CameraHelpCallback");
  ApplyGlide();
  g_saved_wheel_scale = *reinterpret_cast<float*>(kWheelScale);
  g_have_wheel_scale = g_saved_wheel_scale > 0.0f;
}

// The game's glide time goes back, so a game started without the mod after
// this one is not affected.
void ComfortCameraRelease() {
  if (g_glide_changed) {
    *reinterpret_cast<float*>(kGlideTime) = g_saved_glide;
    g_glide_changed = false;
  }
  if (g_wheel_scale_changed) {
    *reinterpret_cast<float*>(kWheelScale) = g_saved_wheel_scale;
    g_wheel_scale_changed = false;
  }
}

// 0 is the game's own zoom step, then small, medium and large.
void ComfortCameraSetZoomStep(int step) {
  g_zoom_step = (step >= 0 && step < 4) ? step : 2;
  g_wheel_pending = 0;
  if (g_zoom_step == 0) {
    ModLog("comfort      : zoom with %s", kZoomNames[0]);
  } else {
    ModLog("comfort      : zoom in %s steps, %.2f times the distance a click",
           kZoomNames[g_zoom_step], kZoomRatios[g_zoom_step]);
  }
}

// True while the middle mouse is held for turning, so the Hand stays put.
bool ComfortCameraHoldsPointer() {
  return Snapping() && g_middle && GetTickCount64() - g_middle_tick < 100;
}

// A snap turn from a controller. It takes the same path as a turn key: the
// next camera update reads it, so MayTurn, the tutorial's turn switch and the
// help callback all still apply. Without snap turns it does nothing, and the
// caller gets false.
bool ComfortCameraRequestTurn(int direction) {
  if (!Snapping() || direction == 0) return false;
  g_turn_request = direction > 0 ? 1 : -1;
  g_turn_request_tick = GetTickCount64();
  return true;
}

// True when snap turns replace the game's own turning and tilting.
bool ComfortCameraSnapping() { return Snapping(); }

// The angle of a snap turn, in degrees. 0 with the snap turns off.
float ComfortCameraSnapDegrees() { return g_snap_degrees; }

// The CameraModeNew3 whose update runs now, and null outside that update.
// pointer_map.cpp uses it to find the camera code's rays.
const void* ComfortCameraMode() { return g_mode; }

// The last CameraModeNew3 whose update ran, for code that runs before the
// update. The game can delete a camera mode, so this checks that the memory
// still holds one. Null when it does not.
const void* ComfortCameraLastMode() {
  if (g_last_mode == nullptr) return nullptr;
  if (IsBadReadPtr(g_last_mode, kModeReadSize)) return nullptr;
  if (*static_cast<const uintptr_t*>(g_last_mode) != kModeVTable) {
    return nullptr;
  }
  return g_last_mode;
}

// How many CameraModeNew3 updates ran. A caller compares two counts to see
// if an update ran between them.
uint64_t ComfortCameraUpdates() { return g_mode_updates; }

// True when the last update of the camera mode, in this frame, had a land
// drag in the God view. tabletop.cpp reads it once, at the main camera call,
// for the mouse pan. A frame with no update of the camera mode reads false.
bool ComfortCameraTakeGodDrag() {
  const bool drag = g_god_drag;
  g_god_drag = false;
  return drag;
}

// The tests of the place at a game camera (SpawnTests): the update of the
// zoom to the Temple with its land point and its flag, and the update of the
// test key. An update of 0 is no test.
void ComfortCameraSetSpawnTests(int temple_update, int x, int z, int flag,
                                int toggle_update) {
  g_test_temple_flag = flag != 0 ? 1 : 0;
  g_test_temple_update = temple_update > 0 ? temple_update : 0;
  g_test_temple_x = static_cast<float>(x);
  g_test_temple_z = static_cast<float>(z);
  g_test_toggle_update = toggle_update > 0 ? toggle_update : 0;
}

// Where the game camera goes: the destinations of its Zoomers after the last
// update of the player's camera mode. tabletop.cpp builds the God view from
// them, so a camera that still glides to its place gives the place at once.
// False when no update of that mode ran in the last 100 ms.
bool ComfortCameraDestinations(float position[3], float focus[3]) {
  if (g_watch.mode == nullptr) return false;
  if (GetTickCount64() - g_watch.base_ms > 100) return false;
  CopyPoint(position, g_watch.base_position);
  CopyPoint(focus, g_watch.base_focus);
  return true;
}

// True while the game moves its own camera and the camera is not at rest
// yet: a new camera mode of the player, a jump, or a camera that a script or
// a load set. The God view goes to that camera only when it is at rest, so
// in this time the view of the player is not at the place of the game camera
// (docs/god-view.md, "15.3 The watch" and "16. The player that a script
// sees").
bool ComfortCameraGameMoves() { return g_watch.live && g_watch.moving; }

// True for a moment after a snap or a jump, while the eyes are drawn black.
// A cut made somewhere else, for example by script_camera.cpp, blinks too.
void ComfortCameraBlink() { StartBlink(); }

// The teleport of the immersive view: add delta (world units) to the camera
// after the next update of the camera mode. A second request before then
// replaces the first.
void ComfortCameraTeleport(const float delta[3]) {
  g_teleport_delta[0] = delta[0];
  g_teleport_delta[1] = delta[1];
  g_teleport_delta[2] = delta[2];
  g_teleport_pending = true;
}

bool ComfortBlinkActive() {
  return g_blink_until != 0 && GetTickCount64() < g_blink_until;
}

void ComfortCameraReport() {
  if (!g_installed) return;
  if (g_grab_writes != 0 || g_grab_turns_refused != 0 ||
      g_grab_wheel_refused != 0) {
    ModLog("comfort      : the grab of the world wrote the camera on %llu "
           "updates. It refused %llu turns, and the wheel on %llu updates.",
           g_grab_writes, g_grab_turns_refused, g_grab_wheel_refused);
    g_grab_writes = g_grab_turns_refused = g_grab_wheel_refused = 0;
  }
  if (g_god_writes != 0 || g_god_turns != 0 || g_god_wheel != 0 ||
      g_god_drag_frames != 0 || g_god_ground_keys != 0) {
    ModLog("comfort      : the God view wrote the game camera on %llu updates. "
           "It took %llu snap turns, %llu wheel steps and %llu ground steps "
           "from the tilt keys. The camera went back after a land drag on "
           "%llu updates.", g_god_writes, g_god_turns, g_god_wheel,
           g_god_ground_keys, g_god_drag_frames);
    g_god_writes = g_god_turns = g_god_wheel = 0;
    g_god_drag_frames = g_god_ground_keys = 0;
  }
  if (g_watch_moves != 0 || g_watch_small != 0 || g_watch_dropped != 0 ||
      g_watch_player != 0 || g_watch_settle != 0) {
    ModLog("comfort      : the game put its camera at a new place %llu times. "
           "%llu of its moves were too small for a new place, and %llu ended "
           "when the player moved the view. %llu updates had a camera move "
           "of the player. In %llu updates the game brought its camera to "
           "rest after a write.", g_watch_moves, g_watch_small,
           g_watch_dropped, g_watch_player, g_watch_settle);
    g_watch_moves = g_watch_small = g_watch_dropped = g_watch_player = 0;
    g_watch_settle = 0;
  }
  {
    char text[256] = {0};
    int used = 0;
    for (int i = 0; i < 16 && used < 200; ++i) {
      if (g_help_counts[i] == 0) continue;
      used += sprintf_s(text + used, sizeof(text) - used, " 0x%03X x%llu,",
                        0x300 + i, g_help_counts[i]);
      g_help_counts[i] = 0;
    }
    if (used > 0 || g_help_other != 0) {
      ModLog("comfort      : camera help events:%s %llu others", text,
             g_help_other);
      g_help_other = 0;
    }
  }
  if (g_zoom_steps != 0 || g_wheel_counts != 0) {
    ModLog("comfort      : %.1f wheel clicks made %llu zoom steps",
           static_cast<double>(g_wheel_counts) / kCountsPerClick, g_zoom_steps);
    g_zoom_steps = 0;
    g_wheel_counts = 0;
  }
  if (g_teleport_moves != 0 || g_teleport_dropped != 0) {
    ModLog("teleport     : the immersive camera moved %llu times, and %llu "
           "moves were dropped", g_teleport_moves, g_teleport_dropped);
    g_teleport_moves = g_teleport_dropped = 0;
  }
  if (g_snaps == 0 && g_refused == 0 && g_jumps == 0) return;
  ModLog("comfort      : %llu snap turns, %llu refused, %llu jumps landed at "
         "once", g_snaps, g_refused, g_jumps);
  g_snaps = 0;
  g_refused = 0;
  g_jumps = 0;
}
