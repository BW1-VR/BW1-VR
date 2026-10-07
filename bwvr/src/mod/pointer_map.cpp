// Let the desktop window be any size, stop it stealing clicks, and keep the
// Hand still in the room when the player turns their head.
//
// THE MEASUREMENT THIS IS BUILT ON. The game's pointer globals and the Windows
// cursor, logged side by side:
//
//     pointer : game 576 795   windows client 576 795 of 1280 x 1280
//
// Identical. The game's pointer is the Windows cursor in client pixels, one to
// one and unscaled. Three things follow, and all three were reported from the
// headset:
//
//   A render bigger than the monitor makes part of the game unreachable. The
//   window is as big as the render, so the bottom of it is off the screen and
//   the pointer can never get there.
//
//   The cursor can leave the window entirely. A click outside brings another
//   program to the front and the game is lost behind it.
//
//   The cursor is fixed to the SCREEN, and with head tracking the screen turns
//   with the head. So the Hand swept across the world whenever the player
//   looked around.
//
// None of them needs to be true. The one to one mapping is a choice the game
// makes, and the mod can make a different one.
//
//   SCALE. Read the cursor in client pixels, scale it onto the render size,
//   and write it back. A small window then reaches the whole game screen, so
//   the render resolution stops being limited by the monitor.
//
//   CLIP. Hold the cursor inside the client area while the game has focus, so
//   a click cannot land on anything else. Released the moment focus is lost,
//   so alt and tab still work, and released again on shutdown.
//
//   LOCK TO THE ROOM. Turn the cursor by the inverse of the head's rotation
//   each frame. The Hand then stays where it was in the room, and only the
//   mouse moves it. See LockToRoom for why this is done to the cursor and not
//   to the camera.
//
// The pointer is rewritten after LHMouse::UpdateDeltaPos has run and before
// anything reads it, which is the one point in the frame where it is settled.
//
// That is not the only place the game takes the pointer, and three other
// paths must agree with it or the game works with two different pointers:
//
//   MouseMoveHandler 0x007DC920 runs from the window procedure on every
//   WM_MOUSEMOVE. It writes the pointer globals from the message, in WINDOW
//   pixels, and GGame::MouseHandler then sends that position straight to the
//   interface through CMouse::ProcessPosition 0x0061A110. The interface keeps
//   it at +0x428 and asks the landscape what is under it. So the land under
//   the Hand was looked up at a point 2.16 times nearer the top left corner.
//   With the head level that point is sky, so the game found no land, showed
//   a red cross, and refused to drop anything. With the head down far enough,
//   the wrong point happened to be land, which is why looking down helped.
//
//   LHMouse::SetPosition 0x007E4E40 is how the game moves the cursor itself.
//   It passes render pixels to SetCursorPos, which takes window pixels.
//
//   GCamera::Update 0x00441F80 reads the pointer as a point on the game's
//   own screen, not the rendered one. See h_GCameraUpdate.
//
// While the camera code runs, the rays it casts through the pointer also
// change. UnprojectScreenPoint 0x0081B370 turns a pointer into a ray, and
// the camera code gets the Hand's own ray from it. See h_Unproject.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../common/bw_addresses.h"
#include "MinHook.h"

extern void ModLog(const char* format, ...);

// head_pose.cpp: the head rotation the camera uses this frame.
bool HeadPoseCurrent(float matrix_out[9], float translation_out[3]);
// head_camera.cpp
bool HeadCameraEnabled();
bool HeadCameraHandAnchored();
bool HeadCameraStageBasis(float basis_out[9]);
bool HeadCameraGameBasis(float basis_out[9]);
bool HeadCameraUseGameCamera();
void HeadCameraReapply();
uint64_t HeadCameraMainSerial();
// comfort_camera.cpp: true while a snap turn button is held.
bool ComfortCameraHoldsPointer();
// comfort_camera.cpp: the CameraModeNew3 while its update runs (else null),
// the last one seen, how many updates ran, and whether snap turns are on.
const void* ComfortCameraMode();
const void* ComfortCameraLastMode();
uint64_t ComfortCameraUpdates();
bool ComfortCameraSnapping();
// script_camera.cpp: runs right after each GCamera::Update.
void ScriptCameraAfterGameCamera(void* camera);
// stereo.cpp: true when the scene has not been drawn for a moment, which is
// what menus, loading screens and videos look like from here.
bool StereoIdle();
// controllers.cpp: the motion controller as a second owner of the pointer.
bool ControllerRenderPixel(float* x, float* y, bool* off_view);
bool ControllerPointing();
bool ControllerMoved();
void ControllerRestMotion();
bool ControllersButtonEvent();
bool ControllersMouseEvent();
int ControllersOwnerMode();
bool ControllersHoldButtons();
bool ControllersGripCarries();
void ControllersSetPointerOwned(bool owned);
bool ControllersActBehind();
bool ControllersLeaveCursor();
bool ControllerCameraRay(float direction_out[3]);
bool ControllersProbePoints(float grip_room[3], float head_room[3]);
// hand_direct.cpp: the direct hand's own ray.
void HandDirectUnproject(const int32_t* screen, float* out);
// head_pose.cpp: game units per meter.
float HeadPoseScale();

namespace {

// Written by the game, read by CHand::PrepareForDrawing at 0x0046C6C5.
const uintptr_t kPointerX = 0x00E852C0;
const uintptr_t kPointerY = 0x00E852C4;

bool g_scale = true;
bool g_clip = true;
bool g_clipped = false;
HWND g_window = nullptr;
int g_render_w = 0;
int g_render_h = 0;
bool g_said = false;

// Where the cursor really is, in render pixels, kept as a float. The Windows
// cursor only moves in whole client pixels, and at a 2.4 to 1 scale that is a
// 2.4 pixel step on the render. Turning the head moves the cursor by fractions
// of that every frame, so rounding each frame and reading the rounded value
// back would drift. The float carries the fraction instead.
float g_true_x = 0.0f;
float g_true_y = 0.0f;
bool g_have_true = false;
// The client position this file last put the Windows cursor at. Any difference
// from it next frame is the player moving the mouse.
int g_written_x = 0;
int g_written_y = 0;

// The game's camera code, which works on the game's own screen. See
// h_GCameraUpdate.
const uintptr_t kGCameraUpdate = 0x00441F80;  // thiscall, no arguments
typedef void(__fastcall* CameraUpdateFn)(void* self, void* unused);
CameraUpdateFn o_GCameraUpdate = nullptr;
bool g_in_game_camera = false;
bool g_warped_in_camera = false;
uint64_t g_game_screen_frames = 0;
// Whether the camera code also gets the game's own camera. See
// h_GCameraUpdate. Off only for testing.
bool g_rebuild = true;
uint64_t g_rebuilt_frames = 0;
uint64_t g_reapplied_frames = 0;
// Which camera the game is drawing from. The main camera call at the end of
// GCamera::Update only happens when this is zero.
const uintptr_t kGame = 0x00D0195C;
const uintptr_t kGameCameraSource = 0x00205A28;

// The whole pixel the camera code got last on the game's own screen. See
// GamePixel. Off (g_game_from_float false) gives the old conversion from the
// whole render pixel.
int32_t g_game_x = 0;
int32_t g_game_y = 0;
bool g_have_game_pixel = false;
bool g_game_from_float = true;
// How far the exact point may move from the last pixel before the pixel
// changes. This is half a pixel for rounding, and 0.3 of a pixel more.
const float kGameStick = 0.8f;

// The proof, for the periodic report. It counts the frames where the land
// was held and the mouse did not move. It also counts how often the camera
// code's pointer and the camera still moved on those frames.
const uintptr_t kHandDragA = 0x00C5B0B4;  // set while the Hand drags the land
const uintptr_t kHandDragB = 0x00C5B0F0;
const uintptr_t kMouseDelta = 0x00C5E8D0;  // ControlMap::MouseDelta, 2 ints
const int kCameraPositionX = 0x118;  // GCamera position Zoomers, 0x30 apart
const int kZoomerSize = 0x30;
bool g_cursor_still = false;  // PointerMapApply saw no mouse movement
uint64_t g_hold_frames = 0;
uint64_t g_hold_pointer_moves = 0;
uint64_t g_hold_camera_moves = 0;
float g_hold_largest = 0.0f;

// The window procedure's mouse move, and the game's own cursor move.
const uintptr_t kMouseMoveHandler = 0x007DC920;  // __cdecl (wparam, lparam, time)
const uintptr_t kMouseSetPosition = 0x007E4E40;  // thiscall (const int[2]), ret 4
// Nonzero when LHMouse draws the cursor itself and never calls SetCursorPos.
const uintptr_t kMouseOwnsCursor = 0x00E8C0FA;
typedef void(__cdecl* MouseMoveFn)(int wparam, int lparam, int time);
typedef int(__fastcall* SetPositionFn)(void* self, void* unused,
                                        const int32_t* point);
MouseMoveFn o_MouseMoveHandler = nullptr;
SetPositionFn o_MouseSetPosition = nullptr;
bool g_hooks_installed = false;
uint64_t g_moves_mapped = 0;
uint64_t g_warps_mapped = 0;

// The head rotation the cursor was last turned for.
float g_last_head[9];
bool g_have_last_head = false;
uint64_t g_locked_frames = 0;
uint64_t g_skipped_jumps = 0;

// LH3DTech::UnprojectScreenPoint. __fastcall: the screen point in ecx, the
// output in edx, the depth on the stack, ret 4. The screen point is a pair
// of ints: the function starts with fild [ecx]. The output is the stored
// camera position 0x00EA1DB8 plus the camera rotation times the ray.
const uintptr_t kUnproject = 0x0081B370;
typedef void(__fastcall* UnprojectFn)(const int32_t* screen, float* out,
                                      float depth);
UnprojectFn o_Unproject = nullptr;
// CameraModeNew3::UpdateClickParams. thiscall (const LHPoint* position,
// const LHPoint* focus, bool keep), ret 0xC.
const uintptr_t kClickParams = 0x00459610;
typedef void(__fastcall* ClickParamsFn)(void* mode, void* unused,
                                        const float* position,
                                        const float* focus, int keep);
ClickParamsFn o_ClickParams = nullptr;
// The land drag casts two rays, at 0x0045D444 (the press pointer) and at
// 0x0045D456 (the pointer now). These are the return addresses of the calls.
const uintptr_t kDragRayPress = 0x0045D449;
const uintptr_t kDragRayNow = 0x0045D45B;

// CameraModeNew3 fields.
const int kModeButtons = 0x8C;         // the buttons held, 1 is the left one
const int kModePointer = 0xB0;         // the pointer, two ints
const int kModeGrabPoint = 0xCC;       // the land under the pointer
const int kModePlaneNormal = 0x14C;    // the drag plane, three floats
const int kModePlaneDistance = 0x158;  // and its distance, a double
const int kModeHandStatus = 0x1AC;     // what the hold does, 3 is a land drag
const int kModePressPointer = 0x2D4;   // the pointer at UpdateClickParams
const int32_t kButtonLeft = 1;
const int32_t kLandDrag = 3;
// The buttons the game holds, from LHMouse::SetButtons: bit 1 left, 2 right and
// 0x40 middle. With controllers on, this is the union of the real mouse and
// the controller. See controllers.cpp.
const uintptr_t kHeldBits = 0x00E85304;
const uint32_t kHeldAny = 0x43;

// Whether the camera code gets the Hand's exact ray. Off gives the pointer
// clamped to the game's screen, as before. See h_Unproject.
bool g_exact_ray = true;
// The Hand's ray in the world this frame, and the whole pixel on the game's
// screen that the camera code got with it.
float g_hand_dir[3] = {0.0f, 0.0f, 1.0f};
int32_t g_hand_px = 0;
int32_t g_hand_py = 0;
bool g_have_hand_dir = false;
// The same at the last UpdateClickParams, for the drag's first ray.
float g_press_dir[3] = {0.0f, 0.0f, 1.0f};
int32_t g_press_px = 0;
int32_t g_press_py = 0;
const void* g_press_mode = nullptr;
bool g_have_press_dir = false;
// Whether the land drag used the exact ray for its two rays this frame.
bool g_ray_press_used = false;
bool g_ray_now_used = false;
// The frames where the camera code got the direct hand's ray from the game
// camera, for the periodic report.
uint64_t g_direct_ray_frames = 0;
// Whether the last GCamera::Update ran the CameraModeNew3 update.
bool g_mode_ran = false;

// The safe box, as parts of the game's screen. The camera code turns a press
// in the side bands (more than 0.45 from the middle) into a turn. It can
// turn a press in the bottom 7 percent or the top 10 percent into a tilt.
// The box stays clear of all of them. See PlaceGamePoint.
const float kBoxLeft = 0.06f;
const float kBoxRight = 0.94f;
const float kBoxTop = 0.11f;
const float kBoxBottom = 0.92f;
// Where a Hand behind the game camera goes for a press. Any point in the box
// does, because the ray is exact.
const float kBehindX = 0.5f;
const float kBehindY = 0.9f;
// During a hold the point is not clamped to the screen. The camera code
// squares the pixel distance as an int at 0x0045C438, so it must not reach
// the overflow.
const float kHoldLimit = 12000.0f;

// The mouse movement the Hand has not taken yet, in render pixels. See
// DragShare.
int g_drag_smooth_ms = 35;
float g_pend_x = 0.0f;
float g_pend_y = 0.0f;
double g_last_apply_ms = 0.0;
double g_ticks_per_ms = 0.0;
// The Hand never lags more than this behind the mouse, in render pixels.
const float kRubberBand = 300.0f;
// Less than this is taken at once, so the ease ends.
const float kPendingDone = 0.05f;

// The proof for the drag, for the periodic report. See ReportDrag.
uint64_t g_presses = 0;
uint64_t g_land_drags = 0;
uint64_t g_box_presses = 0;
uint64_t g_behind_presses = 0;
uint64_t g_exact_drags = 0;
uint64_t g_smoothed_frames = 0;
int32_t g_last_buttons = 0;
bool g_dragging = false;
bool g_drag_counted_exact = false;
int g_plane_logged = 0;

// The window that the game made: it keeps the handle at 0x00E8C0F4
// (0x007DBB61).
const uintptr_t kGameWindowMade = 0x00E8C0F4;
// gWindowActive: 0 while a different program is the active one. The mouse
// thread of the game sleeps then.
const uintptr_t kWindowActive = 0x00C311A8;

HWND GameWindow() {
  if (g_window != nullptr && IsWindow(g_window)) return g_window;
  // The game says which window is its own. A game that starts behind a
  // different program has no active window, and the window in front is then
  // not the game's.
  g_window = *reinterpret_cast<const HWND*>(kGameWindowMade);
  if (g_window != nullptr && IsWindow(g_window)) return g_window;
  // The game has one top level window, and it is the foreground one while it
  // is being played. GetActiveWindow is thread affine and this runs on the
  // game's own thread, so it finds the right one.
  g_window = GetActiveWindow();
  if (g_window == nullptr) g_window = GetForegroundWindow();
  DWORD process = 0;
  if (g_window != nullptr) GetWindowThreadProcessId(g_window, &process);
  if (process != GetCurrentProcessId()) g_window = nullptr;
  return g_window;
}

// THE FOCUS HAS TWO SIDES (docs/controllers.md, "The game window behind").
//
// The real focus: the game window is the window in front. Each call that
// touches the cursor of Windows asks for it: ClipCursor and SetCursorPos. So
// the mod never moves or holds the real cursor for a window that is behind.
// The real mouse also asks for it: a mouse that moves, or a button that is
// down, while a different window is in front is not for the game.
//
// The logical focus: the real focus, or the controllers act while the window
// is behind (the headset session stands for the front, or the test key
// fake_controller_front is on). The owner of the pointer and the game's own
// pointer ask for it.
bool RealFocus(HWND window) { return GetForegroundWindow() == window; }

bool LogicalFocus(HWND window) {
  return RealFocus(window) || ControllersActBehind();
}

// The render size, from whoever knows it. The VR start says so directly, and
// without a headset the game's own projection block still knows.
bool RenderSize(int* width, int* height) {
  if (g_render_w > 0 && g_render_h > 0) {
    *width = g_render_w;
    *height = g_render_h;
    return true;
  }
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return false;
  if (info->res_x <= 0 || info->res_y <= 0) return false;
  *width = info->res_x;
  *height = info->res_y;
  return true;
}

// KEEP THE HAND STILL IN THE ROOM WHEN THE HEAD TURNS.
//
// The first way this was done swapped the game's own camera in around the
// code that places the Hand, so the cursor's ray came from where the GAME was
// looking rather than where the player was. That placed the Hand through one
// camera while everything else the game does with the cursor used another:
// what the Hand is over, whether a drop is allowed, where a held object goes.
// Once the view stopped following the game's downward pitch, the two cameras
// were 12.9 degrees apart all the time, and the game showed a red cross beside
// the Hand, let ground spin under it instead of dragging, and refused to drop
// objects anywhere but the bottom edge of the screen.
//
// So the camera is left alone and the CURSOR is turned instead. The view is
// the room turned by the head, R = L * H. For a direction on screen to stay
// fixed in the room while the head goes from H_prev to H_now, the ray in
// camera space has to go from r to
//
//     r' = transpose(H_now) * H_prev * r
//
// Every part of the game then sees one camera and one cursor, and they agree.
// The one exception is the game's camera code, which gets its own screen for
// the length of GCamera::Update.
//
// Part of the red cross turned out to have a second cause, the window pixel
// pointer described at the top of this file.
//
// Only the head is compensated. When the game moves its own camera, by the
// player dragging the land or by a cutscene, the room moves and the cursor
// moves with it, exactly as it does on a monitor.
void LockToRoom(float* x, float* y, bool apply) {
  const bool active = HeadCameraEnabled() && HeadCameraHandAnchored() &&
                      !StereoIdle();
  float head[9];
  float lean[3];
  if (!active || !HeadPoseCurrent(head, lean)) {
    // Forget the last head, so coming back from a menu does not turn the
    // cursor by everything the head did while the menu was up.
    g_have_last_head = false;
    return;
  }
  if (!g_have_last_head) {
    for (int i = 0; i < 9; ++i) g_last_head[i] = head[i];
    g_have_last_head = true;
    return;
  }

  // delta = transpose(H_now) * H_prev, in the column convention head_pose.cpp
  // builds its matrices in: m[r * 3 + c] is row r, column c.
  float delta[9];
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      delta[r * 3 + c] = head[0 * 3 + r] * g_last_head[0 * 3 + c] +
                         head[1 * 3 + r] * g_last_head[1 * 3 + c] +
                         head[2 * 3 + r] * g_last_head[2 * 3 + c];
    }
  }
  for (int i = 0; i < 9; ++i) g_last_head[i] = head[i];
  // A controller that owns the pointer keeps its own place in the room. The
  // head is still recorded, so the mouse does not jump when it takes over.
  if (!apply) return;

  // A head does not turn 30 degrees in one frame. When the rotation jumps that
  // far it is a recentre or a tracking glitch, and following it would fling
  // the Hand across the island.
  const float trace = delta[0] + delta[4] + delta[8];
  if (trace < 1.0f + 2.0f * 0.866f) {
    ++g_skipped_jumps;
    return;
  }

  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return;
  const float hx = info->half_res_x;
  const float hy = info->half_res_y;
  const float fx = info->inv_half_tan_fov_x;
  const float fy = info->inv_half_tan_fov_y;
  if (hx <= 0.0f || hy <= 0.0f || fx <= 0.0f || fy <= 0.0f) return;

  // Screen to a ray in camera space: x right, y up, z forward.
  const float ray[3] = {(*x / hx - 1.0f) / fx, (1.0f - *y / hy) / fy, 1.0f};
  const float turned[3] = {
      delta[0] * ray[0] + delta[1] * ray[1] + delta[2] * ray[2],
      delta[3] * ray[0] + delta[4] * ray[1] + delta[5] * ray[2],
      delta[6] * ray[0] + delta[7] * ray[1] + delta[8] * ray[2]};
  // A direction that is now behind the player has no place on the screen.
  // Leave the cursor where it was, which keeps it at the edge they turned from.
  if (turned[2] < 0.05f) return;

  *x = (1.0f + (turned[0] * fx) / turned[2]) * hx;
  *y = (1.0f - (turned[1] * fy) / turned[2]) * hy;
  ++g_locked_frames;
}

// TWO SCREENS.
//
// The picture is drawn from the player's head: R = L * H, the room turned by
// the head. The game's camera code still thinks in its own camera, E, which
// looks where the game points it and knows nothing of the head. A point on
// the rendered screen is a ray in the world, and that ray falls somewhere on
// the game's screen too. These two move a pointer between the two.
//
//     game ray   = transpose(E) * L * H * rendered ray
//     render ray = transpose(H) * transpose(L) * E * game ray
bool ScreenScale(float* hx, float* hy, float* fx, float* fy) {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return false;
  *hx = info->half_res_x;
  *hy = info->half_res_y;
  *fx = info->inv_half_tan_fov_x;
  *fy = info->inv_half_tan_fov_y;
  return *hx > 0.0f && *hy > 0.0f && *fx > 0.0f && *fy > 0.0f;
}

bool ScreenRay(float x, float y, float ray[3], float* hx, float* hy, float* fx,
               float* fy) {
  if (!ScreenScale(hx, hy, fx, fy)) return false;
  ray[0] = (x / *hx - 1.0f) / *fx;
  ray[1] = (1.0f - y / *hy) / *fy;
  ray[2] = 1.0f;
  return true;
}

// Rotate by m, or by its transpose, in the column convention: m[r * 3 + c].
void Apply(const float m[9], bool transpose, const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = 0.0f;
    for (int c = 0; c < 3; ++c) {
      out[r] += (transpose ? m[c * 3 + r] : m[r * 3 + c]) * in[c];
    }
  }
}

bool MoveBetweenScreens(bool to_game, float* x, float* y) {
  float head[9], lean[3], stage[9], game[9];
  if (!HeadPoseCurrent(head, lean) || !HeadCameraStageBasis(stage) ||
      !HeadCameraGameBasis(game)) {
    return false;
  }
  float ray[3], hx, hy, fx, fy;
  if (!ScreenRay(*x, *y, ray, &hx, &hy, &fx, &fy)) return false;
  float a[3], b[3], turned[3];
  if (to_game) {
    Apply(head, false, ray, a);
    Apply(stage, false, a, b);
    Apply(game, true, b, turned);
  } else {
    Apply(game, false, ray, a);
    Apply(stage, true, a, b);
    Apply(head, true, b, turned);
  }
  // Behind the camera, so there is no place on that screen for it.
  if (turned[2] < 0.05f) return false;
  *x = (1.0f + (turned[0] * fx) / turned[2]) * hx;
  *y = (1.0f - (turned[1] * fy) / turned[2]) * hy;
  return true;
}

// Where a unit direction in the world falls on the game's screen, with no
// clamp. in_front is false when the direction is 87 degrees or more from the
// game camera's forward, and then the screen point is not written.
bool OnGameScreen(const float world[3], float* game_x, float* game_y,
                  bool* in_front) {
  float game[9], hx, hy, fx, fy;
  if (!HeadCameraGameBasis(game) || !ScreenScale(&hx, &hy, &fx, &fy)) {
    return false;
  }
  float turned[3];
  Apply(game, true, world, turned);
  *in_front = turned[2] >= 0.05f;
  if (*in_front) {
    *game_x = (1.0f + (turned[0] * fx) / turned[2]) * hx;
    *game_y = (1.0f - (turned[1] * fy) / turned[2]) * hy;
  }
  return true;
}

// The Hand's ray in the world, L * H * ray, as a unit vector. Also where it
// falls on the game's screen (OnGameScreen). The cursor is locked to the
// room in this same L * H, so the ray only changes when the mouse moves.
bool HandRay(float x, float y, float world[3], float* game_x, float* game_y,
             bool* in_front) {
  float head[9], lean[3], stage[9];
  if (!HeadPoseCurrent(head, lean) || !HeadCameraStageBasis(stage)) {
    return false;
  }
  float ray[3], hx, hy, fx, fy;
  if (!ScreenRay(x, y, ray, &hx, &hy, &fx, &fy)) return false;
  float a[3];
  Apply(head, false, ray, a);
  Apply(stage, false, a, world);
  const float length = sqrtf(world[0] * world[0] + world[1] * world[1] +
                             world[2] * world[2]);
  if (!(length > 1e-6f)) return false;
  for (int i = 0; i < 3; ++i) world[i] /= length;
  return OnGameScreen(world, game_x, game_y, in_front);
}

// Whether the cursor is being held still in the room, which is the only time
// the two screens differ in a way the game can notice.
bool Locking() {
  return g_scale && g_have_true && HeadCameraEnabled() &&
         HeadCameraHandAnchored() && !StereoIdle();
}

// GIVE THE GAME'S CAMERA CODE ITS OWN SCREEN.
//
// Holding the ground and turning the head dragged the land. The camera code
// in CameraModeNew3 builds its own world to camera matrix from the game's
// camera, then reads the pointer globals as a point on THAT screen. The head
// lock moves the pointer on the rendered screen to keep the Hand still in the
// room, and on the game's screen that looks exactly like the player moving the
// mouse. Measured with an invented head stepping 20 degrees and the button
// held: the camera sat 52 units further back with the head down, and came
// back when the head came up.
//
// So for the length of GCamera::Update the pointer is where the Hand's own
// ray falls on the game's screen. That point only moves when the Hand's ray
// moves, which is when the player moves the mouse. The Hand, the land under
// it and everything drawn still use the rendered screen, before and after.
//
// THE CAMERA CODE ALSO GETS THE GAME'S OWN CAMERA.
//
// The pointer is not the only thing the camera code reads. It casts rays
// through the pointer with fn_00800C30, for example the grab point at
// 0x0045BA22, and those rays use the matrices in the engine. GCamera::Update
// makes the main camera call at its END, at 0x00442622, so while the camera
// code runs the engine still holds the previous frame's camera, and that one
// carries the head. A game screen pointer through a head camera gives a ray
// that lands in the wrong place, and a wrong grab distance over 3000 stops
// the zoom and locks the camera's input. That matches the report: after a
// turn with the middle mouse, the zoom got stuck or the Hand stopped.
//
// So the engine is first pointed at the game's own camera from the last main
// call, with no head. The main call at the end puts the head back on. When
// the game draws from another camera ([game + 0x205A28] is not zero), the
// main call is skipped, so nothing is changed. If the main call still does not
// happen, the head is put back here, so a frame is never drawn without it.
bool GameCameraIsPlain() {
  const uint8_t* const* game = reinterpret_cast<const uint8_t* const*>(kGame);
  if (IsBadReadPtr(game, sizeof(void*)) || *game == nullptr) return false;
  const int32_t* source =
      reinterpret_cast<const int32_t*>(*game + kGameCameraSource);
  if (IsBadReadPtr(source, sizeof(int32_t))) return false;
  return *source == 0;
}

// THE EXACT POINT, NOT THE WHOLE PIXEL.
//
// Holding the land shook the camera in the headset, with the mouse still.
// The camera code got its pointer from the Hand's WHOLE render pixel. That
// pixel was turned through the head onto the game's screen and cut to a
// whole pixel again. The head cancels in that turn, but the first cut does
// not. Where the Hand falls inside its render pixel changes as the head
// moves. So the game's pixel flipped between two neighbors on about 20 to 70
// percent of frames. The land drag places the camera straight from that
// pixel, through the camera and the plane it fixed at the press
// (UpdateClickParams, 0x00459610). So every flip moved the camera. The
// game's 0.3 s glide hid this until the glide was turned off.
//
// So the exact float the Hand is kept in is turned instead, and rounded. It
// then only moves when the mouse moves. The pixel also stays where it was
// until the exact point is kGameStick away from it. So float noise in the
// turn cannot flip it either, and neither can the small pitch changes a
// drag down a slope makes.
int32_t GamePixel(float exact, int32_t last) {
  if (g_have_game_pixel &&
      fabsf(exact - static_cast<float>(last)) <= kGameStick) {
    return last;
  }
  return static_cast<int32_t>(floorf(exact + 0.5f));
}

bool LandHeld() {
  return *reinterpret_cast<const int32_t*>(kHandDragA) != 0 ||
         *reinterpret_cast<const int32_t*>(kHandDragB) != 0;
}

void CameraPosition(void* camera, float out[3]) {
  const uint8_t* base = static_cast<const uint8_t*>(camera);
  for (int axis = 0; axis < 3; ++axis) {
    out[axis] = *reinterpret_cast<const float*>(
        base + kCameraPositionX + axis * kZoomerSize);
  }
}

// The CameraModeNew3 as the last GCamera::Update left it. Null when that
// update ran another camera mode, so its fields tell nothing about now.
const uint8_t* ActiveMode() {
  if (!g_mode_ran) return nullptr;
  return static_cast<const uint8_t*>(ComfortCameraLastMode());
}

int32_t ModeInt(const uint8_t* mode, int offset) {
  return *reinterpret_cast<const int32_t*>(mode + offset);
}

float Clamp(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

void ClampToScreen(int width, int height, float* x, float* y) {
  *x = Clamp(*x, 0.0f, static_cast<float>(width - 1));
  *y = Clamp(*y, 0.0f, static_cast<float>(height - 1));
}

// A Hand behind the game camera has no point on its screen. Keep the last
// pixel, or use the middle when there is none.
void HoldLastPixel(int width, int height, float* x, float* y) {
  if (g_have_game_pixel) {
    *x = static_cast<float>(g_game_x);
    *y = static_cast<float>(g_game_y);
  } else {
    *x = width * 0.5f;
    *y = height * 0.5f;
  }
}

// Whether the pointer at mode+0x2D4 is the one UpdateClickParams stored
// with the press ray.
bool PressRayFits(const uint8_t* mode) {
  const int32_t* press =
      reinterpret_cast<const int32_t*>(mode + kModePressPointer);
  return g_have_press_dir && g_press_mode == mode && press[0] == g_press_px &&
         press[1] == g_press_py;
}

// The drag limits its move by the pixel distance from the press. That
// distance must mean the same thing wherever the press was made. A pixel held
// still behind the game camera would hold the limit at its floor of 50 units.
// A press that the safe box moved would start with the box offset in it, with
// the mouse still, and the limit would be far too loose. So during a land
// drag the distance is SET, not floored: the pixel sits exactly as far from
// the press pixel as the angle the Hand turned since the press, in pixels at
// the middle of the screen. That is what a press at the middle of the screen
// gives in the game. Only the direction comes from the projection.
void KeepStepLimit(const float world[3], float* x, float* y) {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return;
  const float per_radian = info->half_res_x * info->inv_half_tan_fov_x;
  if (!(per_radian > 0.0f)) return;
  float along = world[0] * g_press_dir[0] + world[1] * g_press_dir[1] +
                world[2] * g_press_dir[2];
  along = Clamp(along, -1.0f, 1.0f);
  const float want = acosf(along) * per_radian;
  float dx = *x - static_cast<float>(g_press_px);
  float dy = *y - static_cast<float>(g_press_py);
  const float have = sqrtf(dx * dx + dy * dy);
  if (have < 1.0f) {
    dx = 1.0f;
    dy = 0.0f;
  } else {
    dx /= have;
    dy /= have;
  }
  *x = static_cast<float>(g_press_px) + dx * want;
  *y = static_cast<float>(g_press_py) + dy * want;
}

// WHERE THE CAMERA CODE SEES THE HAND ON ITS SCREEN.
//
// A land drag failed when the player looked down or behind. The camera code
// sorts a press by where the pointer falls on the GAME's screen. It looks
// for the land, a side band to turn, or the bottom or the sky to tilt. The
// player cannot see that screen. With the head far enough down, the Hand
// fell in the bottom band or past the frame, so the press became a tilt.
// Comfort mode turns tilt off, so nothing moved for the whole hold. Behind
// the game camera, the Hand had no point on that screen at all.
//
// So while no button is held, the point stays inside a safe box, where a
// press can only become a land drag. A Hand behind the game camera goes to a
// point in the box. The box is only used with snap turns on. Without them,
// the side bands and the sky still turn and tilt the camera, as the game
// does it, so the point is clamped to the frame as before.
//
// While a button is held, the point is not clamped to the frame. The drag
// limits its move to the pixel distance from the press times the distance
// times 0.011, so that limit must still grow with the motion. When the Hand
// goes behind the game camera during a hold, the pixel stays where it was.
// During a land drag, KeepStepLimit then sets the distance from the press.
//
// The rays themselves do not come from this point. See h_Unproject. True
// when the box moved the point.
bool PlaceGamePoint(const float world[3], bool in_front, bool held,
                    bool land_drag, int width, int height, float* x,
                    float* y) {
  if (held) {
    if (!in_front) HoldLastPixel(width, height, x, y);
    if (land_drag) KeepStepLimit(world, x, y);
    *x = Clamp(*x, -kHoldLimit, kHoldLimit);
    *y = Clamp(*y, -kHoldLimit, kHoldLimit);
    return false;
  }
  if (!ComfortCameraSnapping()) {
    if (!in_front) HoldLastPixel(width, height, x, y);
    ClampToScreen(width, height, x, y);
    return false;
  }
  const float from_x = *x;
  const float from_y = *y;
  if (!in_front) {
    *x = width * kBehindX;
    *y = height * kBehindY;
  }
  *x = Clamp(*x, width * kBoxLeft, width * kBoxRight);
  *y = Clamp(*y, height * kBoxTop, height * kBoxBottom);
  return !in_front || *x != from_x || *y != from_y;
}

// Log a steep drag plane at a press. The camera code builds the plane from
// the line between the grab point and the land under the camera. When the
// grab point is almost under the camera, that line can be steep, and a steep
// plane moves the camera up and down instead of along the land.
void LogPlane(const uint8_t* mode) {
  if (g_plane_logged >= 8) return;
  const float* normal =
      reinterpret_cast<const float*>(mode + kModePlaneNormal);
  const float up = fabsf(normal[1]);
  if (up >= 0.5f) return;
  ++g_plane_logged;
  ModLog("drag         : the drag plane of this press is %.0f degrees from "
         "level", acosf(up < 1.0f ? up : 1.0f) * 57.29578f);
}

// Count presses and land drags, after the camera code has run. boxed and
// behind say what happened to the point this frame.
void CountDrag(bool boxed, bool behind) {
  const uint8_t* mode = ActiveMode();
  const int32_t buttons = mode != nullptr ? ModeInt(mode, kModeButtons) : 0;
  const int32_t status = mode != nullptr ? ModeInt(mode, kModeHandStatus) : 0;
  if (buttons == kButtonLeft && g_last_buttons == 0) {
    ++g_presses;
    if (boxed) ++g_box_presses;
    // Reported as a part of the boxed presses, so only those count.
    if (boxed && behind) ++g_behind_presses;
    LogPlane(mode);
  }
  const bool dragging = buttons == kButtonLeft && status == kLandDrag;
  if (dragging && !g_dragging) {
    ++g_land_drags;
    g_drag_counted_exact = false;
  }
  if (dragging && g_ray_press_used && g_ray_now_used &&
      !g_drag_counted_exact) {
    ++g_exact_drags;
    g_drag_counted_exact = true;
  }
  g_dragging = dragging;
  g_last_buttons = buttons;
}

// THE LAND DRAG PROBE, stage G0 of docs/grab-world.md. With grab_probe=1,
// each land drag logs how far the controller's grip, the head and the camera
// moved from the press to the release. The ratio is the gain of the game's
// own drag, which follows the angle of the Hand and not its motion. Nothing
// changes in the game.
bool g_probe = false;
bool g_probe_have = false;
float g_probe_grip[2][3];    // at the press, and on the last drag frame
float g_probe_head[2][3];
float g_probe_camera[2][3];
uint64_t g_probe_frames = 0;
int g_probe_logs = 0;

void ProbeDrag(void* camera, bool was_dragging) {
  if (!g_probe || camera == nullptr || g_probe_logs >= 40) return;
  float position[3], grip[3], head[3];
  CameraPosition(camera, position);
  const bool have = ControllersProbePoints(grip, head);
  if (g_dragging) {
    const int slot = was_dragging ? 1 : 0;
    if (!was_dragging) {
      g_probe_have = have;
      g_probe_frames = 0;
    }
    if (!have) g_probe_have = false;
    if (!g_probe_have) return;
    ++g_probe_frames;
    for (int i = 0; i < 3; ++i) {
      g_probe_grip[slot][i] = grip[i];
      g_probe_head[slot][i] = head[i];
      g_probe_camera[slot][i] = position[i];
      if (slot == 0) {
        g_probe_grip[1][i] = grip[i];
        g_probe_head[1][i] = head[i];
        g_probe_camera[1][i] = position[i];
      }
    }
    return;
  }
  if (!was_dragging || !g_probe_have) return;
  g_probe_have = false;
  const float scale = HeadPoseScale() > 0.0f ? HeadPoseScale() : 7.5f;
  float hand[3], lean[3], moved[3];
  for (int i = 0; i < 3; ++i) {
    hand[i] = (g_probe_grip[1][i] - g_probe_grip[0][i]) / scale * 100.0f;
    lean[i] = (g_probe_head[1][i] - g_probe_head[0][i]) / scale * 100.0f;
    moved[i] = g_probe_camera[1][i] - g_probe_camera[0][i];
  }
  const float hand_cm = sqrtf(hand[0] * hand[0] + hand[1] * hand[1] +
                              hand[2] * hand[2]);
  const float lean_cm = sqrtf(lean[0] * lean[0] + lean[1] * lean[1] +
                              lean[2] * lean[2]);
  const float camera_units = sqrtf(moved[0] * moved[0] + moved[1] * moved[1] +
                                   moved[2] * moved[2]);
  const float hand_units = hand_cm * 0.01f * scale;
  ++g_probe_logs;
  ModLog("drag probe   : a land drag of %llu frames. The grip moved %.1f cm "
         "(%.1f %.1f %.1f, right, up, forward in the room), the head %.1f cm, "
         "the camera %.2f units (%.2f %.2f %.2f). %s %.1f.",
         g_probe_frames, hand_cm, hand[0], hand[1], hand[2], lean_cm,
         camera_units, moved[0], moved[1], moved[2],
         hand_units > 0.01f ? "Camera units for each unit of the grip:"
                            : "The grip did not move. Camera units:",
         hand_units > 0.01f ? camera_units / hand_units : camera_units);
}

void __fastcall h_GCameraUpdate(void* self, void* unused) {
  bool rebuilt = false;
  uint64_t serial = 0;
  if (g_rebuild && GameCameraIsPlain()) {
    serial = HeadCameraMainSerial();
    rebuilt = HeadCameraUseGameCamera();
    if (rebuilt) ++g_rebuilt_frames;
  }

  // Read before the camera code, which zeroes the mouse movement.
  const int32_t* mouse = reinterpret_cast<const int32_t*>(kMouseDelta);
  const bool measure =
      self != nullptr && LandHeld() && g_cursor_still && mouse[0] == 0 &&
      mouse[1] == 0;
  float camera_before[3] = {0.0f, 0.0f, 0.0f};
  if (measure) CameraPosition(self, camera_before);

  // Whether a button was held when the camera mode last ran. The frame of a
  // press still reads no button here, so the safe box applies to the press.
  const uint8_t* mode = ActiveMode();
  const bool held = mode != nullptr && ModeInt(mode, kModeButtons) != 0;
  // Only a land drag has the step limit, and only its press ray is known.
  const bool land_drag = held && ModeInt(mode, kModeButtons) == kButtonLeft &&
                         ModeInt(mode, kModeHandStatus) == kLandDrag &&
                         PressRayFits(mode);

  int32_t* px = reinterpret_cast<int32_t*>(kPointerX);
  int32_t* py = reinterpret_cast<int32_t*>(kPointerY);
  int render_w = 0, render_h = 0;
  bool moved = false;
  bool pointer_moved = false;
  bool boxed = false;
  bool behind = false;
  int32_t rendered_x = 0, rendered_y = 0;
  g_have_hand_dir = false;
  g_ray_press_used = false;
  g_ray_now_used = false;
  if (Locking() && RenderSize(&render_w, &render_h) &&
      !IsBadWritePtr(px, sizeof(int32_t)) && !IsBadWritePtr(py, sizeof(int32_t))) {
    rendered_x = *px;
    rendered_y = *py;
    // Start from the exact point when the pointer is still the one
    // PointerMapApply wrote. Otherwise something else moved it, and its
    // whole pixel is all there is.
    const bool from_float = g_game_from_float &&
                            static_cast<int32_t>(g_true_x) == rendered_x &&
                            static_cast<int32_t>(g_true_y) == rendered_y;
    float x = from_float ? g_true_x : static_cast<float>(rendered_x);
    float y = from_float ? g_true_y : static_cast<float>(rendered_y);
    float world[3] = {0.0f, 0.0f, 1.0f};
    bool have_world = false;
    bool placed = false;
    if (g_exact_ray) {
      bool in_front = false;
      // The direct hand gives the ray from the game camera to its reach
      // point. The point is fixed in the room, so a lean of the head does
      // not turn this ray, and the land does not slide. The ray from the
      // eye through the pixel would turn with every lean.
      if (ControllerCameraRay(world)) {
        have_world = OnGameScreen(world, &x, &y, &in_front);
        if (have_world) ++g_direct_ray_frames;
      } else {
        have_world = HandRay(x, y, world, &x, &y, &in_front);
      }
      if (have_world) {
        behind = !in_front;
        boxed = PlaceGamePoint(world, in_front, held, land_drag, render_w,
                               render_h, &x, &y);
        placed = true;
      }
    } else if (MoveBetweenScreens(true, &x, &y)) {
      ClampToScreen(render_w, render_h, &x, &y);
      placed = true;
    }
    if (placed) {
      const int32_t game_x = from_float
                                 ? GamePixel(x, g_game_x)
                                 : static_cast<int32_t>(floorf(x));
      const int32_t game_y = from_float
                                 ? GamePixel(y, g_game_y)
                                 : static_cast<int32_t>(floorf(y));
      pointer_moved = g_have_game_pixel &&
                      (game_x != g_game_x || game_y != g_game_y);
      g_game_x = game_x;
      g_game_y = game_y;
      g_have_game_pixel = true;
      *px = game_x;
      *py = game_y;
      moved = true;
      // The ray goes with the pixel it was written with. See h_Unproject.
      if (have_world) {
        for (int i = 0; i < 3; ++i) g_hand_dir[i] = world[i];
        g_hand_px = game_x;
        g_hand_py = game_y;
        g_have_hand_dir = true;
      }
    }
  }
  if (!moved) g_have_game_pixel = false;

  const uint64_t mode_updates = ComfortCameraUpdates();
  g_in_game_camera = moved;
  g_warped_in_camera = false;
  o_GCameraUpdate(self, unused);
  g_in_game_camera = false;
  ScriptCameraAfterGameCamera(self);
  g_mode_ran = ComfortCameraUpdates() != mode_updates;
  const bool was_dragging = g_dragging;
  CountDrag(boxed, behind);
  ProbeDrag(self, was_dragging);

  if (measure && moved) {
    float camera_after[3];
    CameraPosition(self, camera_after);
    const float dx = camera_after[0] - camera_before[0];
    const float dy = camera_after[1] - camera_before[1];
    const float dz = camera_after[2] - camera_before[2];
    const float step = sqrtf(dx * dx + dy * dy + dz * dz);
    ++g_hold_frames;
    if (pointer_moved) ++g_hold_pointer_moves;
    if (step > 1e-4f) ++g_hold_camera_moves;
    if (step > g_hold_largest) g_hold_largest = step;
  }

  if (moved) {
    // A cursor move made by the camera code has already put the rendered
    // position in place. Otherwise put back the one the Hand uses.
    if (!g_warped_in_camera) {
      *px = rendered_x;
      *py = rendered_y;
    }
    ++g_game_screen_frames;
  }
  if (rebuilt && HeadCameraMainSerial() == serial) {
    HeadCameraReapply();
    ++g_reapplied_frames;
  }
}

// THE CAMERA CODE GETS THE HAND'S OWN RAY.
//
// The point on the game's screen is not the Hand. Near the frame, past it,
// or behind the game camera, a ray through that point missed the land under
// the Hand. So the grab point was not under the Hand, and the drag did not
// follow it.
//
// Every ray that CameraModeNew3::Update casts through the pointer goes
// through UnprojectScreenPoint with the pointer at mode+0xB0: the grab point
// at 0x0045BA22, the drag's second ray and the double click. The drag's
// first ray uses the press pointer at mode+0x2D4. This hook gives those
// calls the Hand's own ray in the world. The origin and the length stay as
// the game made them, so only the direction changes.
//
// The drag then keeps the grabbed land on the Hand's ray, at any head angle:
// in front, below or behind. The ray only changes when the mouse moves, so
// the camera stays still under a held Hand while the head moves. With the
// direct hand the ray goes from the game camera to the reach point
// (ControllerCameraRay), and it only changes when the controller moves.
//
// A ray is only changed when its pixel is the one this file wrote with it.
// So after a cursor move by the camera code, or with a stale press, the
// game's own ray stays. The two rays of a drag change together or not at
// all, so the drag never mixes the two kinds.
//
// The function runs many times a frame, for the landscape draw too. The
// first test keeps that cheap.
void __fastcall h_Unproject(const int32_t* screen, float* out, float depth) {
  o_Unproject(screen, out, depth);
  if (screen == nullptr || out == nullptr) return;
  // Outside the camera code, the direct hand turns the ray of the Hand's own
  // screen point to its exact target. See hand_direct.cpp.
  if (!g_in_game_camera) {
    HandDirectUnproject(screen, out);
    return;
  }
  if (!g_have_hand_dir) return;
  const uint8_t* mode = static_cast<const uint8_t*>(ComfortCameraMode());
  if (mode == nullptr) return;
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  const uint8_t* at = reinterpret_cast<const uint8_t*>(screen);
  const float* dir = nullptr;
  if (at == mode + kModePointer) {
    const bool fits = screen[0] == g_hand_px && screen[1] == g_hand_py;
    if (fits && (caller != kDragRayNow || PressRayFits(mode))) {
      dir = g_hand_dir;
    }
  } else if (at == mode + kModePressPointer) {
    const int32_t* now = reinterpret_cast<const int32_t*>(mode + kModePointer);
    const bool now_fits = now[0] == g_hand_px && now[1] == g_hand_py;
    if (PressRayFits(mode) && (caller != kDragRayPress || now_fits)) {
      dir = g_press_dir;
    }
  }
  if (dir == nullptr) return;

  // The caller put the camera it casts from in the stored camera: the grab
  // camera for the drag, the game's own camera for the rest.
  const float* camera = reinterpret_cast<const float*>(bw::tech::kCameraPos);
  float offset[3];
  for (int i = 0; i < 3; ++i) offset[i] = out[i] - camera[i];
  const float length = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] +
                             offset[2] * offset[2]);
  if (!(length > 1e-4f)) return;
  for (int i = 0; i < 3; ++i) out[i] = camera[i] + dir[i] * length;
  if (caller == kDragRayPress) g_ray_press_used = true;
  if (caller == kDragRayNow) g_ray_now_used = true;
}

// THE PRESS RAY, AND A DRAG PLANE THAT IS NEVER ZERO.
//
// UpdateClickParams runs at each press and release. It copies the pointer to
// mode+0x2D4, and it keeps the grab camera and the drag plane. The Hand's ray
// is kept here with that pointer, for the drag's first ray.
//
// The plane's normal comes from the line between the grab point and the land
// under the camera. With the exact ray, the player can grab the land right
// under the camera. That line then has no length, the normal is zero, and
// the drag stops at once. So a zero normal becomes a level plane through the
// grab point. Any other plane stays as the game made it.
void __fastcall h_ClickParams(void* self, void* unused, const float* position,
                              const float* focus, int keep) {
  uint8_t* mode = static_cast<uint8_t*>(self);
  if (mode != nullptr) {
    const int32_t* pointer =
        reinterpret_cast<const int32_t*>(mode + kModePointer);
    if (g_have_hand_dir && pointer[0] == g_hand_px &&
        pointer[1] == g_hand_py) {
      for (int i = 0; i < 3; ++i) g_press_dir[i] = g_hand_dir[i];
      g_press_px = pointer[0];
      g_press_py = pointer[1];
      g_press_mode = mode;
      g_have_press_dir = true;
    } else {
      g_have_press_dir = false;
    }
  }
  o_ClickParams(self, unused, position, focus, keep);
  if (mode == nullptr) return;

  float* normal = reinterpret_cast<float*>(mode + kModePlaneNormal);
  const float size = normal[0] * normal[0] + normal[1] * normal[1] +
                     normal[2] * normal[2];
  if (size >= 0.25f) return;
  const float* grab = reinterpret_cast<const float*>(mode + kModeGrabPoint);
  normal[0] = 0.0f;
  normal[1] = 1.0f;
  normal[2] = 0.0f;
  *reinterpret_cast<double*>(mode + kModePlaneDistance) = grab[1];
  if (g_plane_logged < 8) {
    ++g_plane_logged;
    ModLog("drag         : the grab point was under the camera, so the drag "
           "plane is level");
  }
}

// Whether the Hand's pointer is held still. The game holds it while the
// middle mouse turns the camera (SetTurnOffMouseMove, 0x00E8C0FA), and
// comfort_camera.cpp holds it while a snap turn button is down, so moving the
// mouse to turn does not also move the Hand. A button must be down: playback
// also sets the game's flag, and GInterface::Process resets it every turn, so
// the flag alone is not a safe reason to stop the Hand.
bool PointerHeld() {
  if (ComfortCameraHoldsPointer()) return true;
  const uint8_t* frozen = reinterpret_cast<const uint8_t*>(kMouseOwnsCursor);
  if (*frozen == 0) return false;
  // A controller button is held: the game's held bits, which hold both
  // sources. Otherwise the real mouse, read now, exactly as before.
  if (ControllersHoldButtons()) {
    return (*reinterpret_cast<const uint32_t*>(kHeldBits) & kHeldAny) != 0;
  }
  return ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) |
           GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
}

// Whether the left button holds the land in a land drag. The camera mode's
// fields are from the last frame, because the camera code runs after this.
// The button is read now, so the release is seen on the frame it happens.
bool LandDragHeld() {
  const uint8_t* mode = ActiveMode();
  if (mode == nullptr) return false;
  if (ModeInt(mode, kModeButtons) != kButtonLeft) return false;
  if (ModeInt(mode, kModeHandStatus) != kLandDrag) return false;
  if (ControllersHoldButtons()) {
    return (*reinterpret_cast<const uint32_t*>(kHeldBits) & 1) != 0;
  }
  return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

double NowMs() {
  if (g_ticks_per_ms <= 0.0) {
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
      return static_cast<double>(GetTickCount64());
    }
    g_ticks_per_ms = static_cast<double>(frequency.QuadPart) / 1000.0;
  }
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart) / g_ticks_per_ms;
}

// SMOOTH THE MOUSE, NOT THE CAMERA, DURING A LAND DRAG.
//
// A drag of far land was choppy. A ray that hits the land at range R, from a
// camera h above it, moves the grab point about R * R / h per radian. So one
// window pixel of mouse motion can move the camera some meters at range.
// The cursor also moves in whole window pixels, once a frame. So a slow drag
// moves the camera on some frames and not on others. The game's 0.3 s glide
// hid this, and comfort mode turns the glide off.
//
// So the player's own mouse motion goes into a pending amount, and the Hand
// takes a share of it each frame: 1 - exp(-dt / time). The Hand and the
// camera code both read the smoothed point, so the grabbed land stays under
// the Hand while it moves. A camera that eases after the Hand would let the
// land slide under it. The sum of all the steps is exactly the mouse motion.
//
// This only happens while the left button holds the land in a land drag. At
// the release the rest arrives at once, and the drag has ended, so the
// camera does not move after the release. Head motion is never smoothed.
// The Hand never lags more than kRubberBand pixels behind the mouse.
float DragShare() {
  const double now = NowMs();
  double dt = g_last_apply_ms > 0.0 ? now - g_last_apply_ms : 11.1;
  g_last_apply_ms = now;
  if (dt < 1.0) dt = 1.0;
  if (dt > 100.0) dt = 100.0;
  if (g_drag_smooth_ms <= 0 || StereoIdle() || !LandDragHeld()) return 1.0f;
  return 1.0f - expf(-static_cast<float>(dt) /
                     static_cast<float>(g_drag_smooth_ms));
}

// Move the Hand by its share of the pending mouse motion. True when it moved.
bool TakePending() {
  const float share = DragShare();
  float rest_x = g_pend_x - g_pend_x * share;
  float rest_y = g_pend_y - g_pend_y * share;
  const float rest = fabsf(rest_x) + fabsf(rest_y);
  if (rest > kRubberBand) {
    rest_x *= kRubberBand / rest;
    rest_y *= kRubberBand / rest;
  }
  if (fabsf(rest_x) + fabsf(rest_y) < kPendingDone) {
    rest_x = 0.0f;
    rest_y = 0.0f;
  }
  if (rest_x != 0.0f || rest_y != 0.0f) ++g_smoothed_frames;
  const float step_x = g_pend_x - rest_x;
  const float step_y = g_pend_y - rest_y;
  g_true_x += step_x;
  g_true_y += step_y;
  g_pend_x = rest_x;
  g_pend_y = rest_y;
  return step_x != 0.0f || step_y != 0.0f;
}

// The window and render sizes, and the scale between them. False when the
// pointer is not scaled, and then the game keeps its own mapping.
bool Scales(HWND* window, int* render_w, int* render_h, float* sx, float* sy) {
  if (!g_scale) return false;
  *window = GameWindow();
  if (*window == nullptr) return false;
  RECT client;
  if (!GetClientRect(*window, &client)) return false;
  const int client_w = client.right - client.left;
  const int client_h = client.bottom - client.top;
  if (client_w <= 0 || client_h <= 0) return false;
  if (!RenderSize(render_w, render_h)) return false;
  *sx = static_cast<float>(*render_w) / client_w;
  *sy = static_cast<float>(*render_h) / client_h;
  return true;
}

// A window position in render pixels, worked out the same way
// PointerMapApply does it, but with nothing moved or stored.
bool ClientToRender(int client_x, int client_y, int* render_x, int* render_y) {
  HWND window;
  int render_w, render_h;
  float sx, sy;
  if (!Scales(&window, &render_w, &render_h, &sx, &sy)) return false;
  // With the window behind, the point of a message is the real cursor in a
  // different program. The game then gets the pointer that it has.
  const bool real_focus = RealFocus(window);
  const bool focused = LogicalFocus(window);
  float x, y;
  if (g_have_true && focused && (PointerHeld() || !real_focus)) {
    x = g_true_x;
    y = g_true_y;
  } else if (g_have_true && focused) {
    x = g_true_x + (client_x - g_written_x) * sx;
    y = g_true_y + (client_y - g_written_y) * sy;
  } else {
    x = client_x * sx;
    y = client_y * sy;
  }
  if (x < 0.0f) x = 0.0f;
  if (y < 0.0f) y = 0.0f;
  if (x > render_w - 1) x = static_cast<float>(render_w - 1);
  if (y > render_h - 1) y = static_cast<float>(render_h - 1);
  *render_x = static_cast<int>(x);
  *render_y = static_cast<int>(y);
  return true;
}

// The window procedure hands the game a WM_MOUSEMOVE position in window
// pixels. Give it render pixels, so the pointer globals and the interface's
// copy of them agree with the Hand from the moment the message arrives.
void __cdecl h_MouseMoveHandler(int wparam, int lparam, int time) {
  const int client_x = static_cast<short>(lparam & 0xFFFF);
  const int client_y = static_cast<short>((lparam >> 16) & 0xFFFF);
  int render_x, render_y;
  if (ClientToRender(client_x, client_y, &render_x, &render_y)) {
    lparam = (render_x & 0xFFFF) | ((render_y & 0xFFFF) << 16);
    ++g_moves_mapped;
  }
  o_MouseMoveHandler(wparam, lparam, time);
}

// The game moves the cursor to a point in render pixels. Put the Windows
// cursor at the matching window pixel, and start the Hand's own position
// from the exact point, so the next frame does not read the rounding back as
// a mouse movement.
int __fastcall h_MouseSetPosition(void* self, void* unused,
                                  const int32_t* point) {
  const uint8_t* owns = reinterpret_cast<const uint8_t*>(kMouseOwnsCursor);
  HWND window;
  int render_w, render_h;
  float sx, sy;
  if (point == nullptr || IsBadReadPtr(point, sizeof(int32_t) * 2) ||
      *owns != 0 || !Scales(&window, &render_w, &render_h, &sx, &sy)) {
    return o_MouseSetPosition(self, unused, point);
  }
  float x = static_cast<float>(point[0]);
  float y = static_cast<float>(point[1]);
  // The camera code works on the game's own screen, so a move it asks for is
  // a point on that screen. See h_GCameraUpdate. Keep the pixel it asked for
  // as the last one, so the next frame gives exactly that pixel back.
  if (g_in_game_camera) {
    // At the end of its update the camera code can ask for the pointer it
    // was given (mode+0xB0). That pixel may be one this file placed for it:
    // the safe box, the point for a Hand behind the camera, or a held pixel.
    // None of those is the Hand, so the Hand stays where it is. The render
    // pixel goes back after the update, in h_GCameraUpdate.
    if (g_have_game_pixel && point[0] == g_game_x && point[1] == g_game_y) {
      return 0;
    }
    g_game_x = point[0];
    g_game_y = point[1];
    g_have_game_pixel = true;
    MoveBetweenScreens(false, &x, &y);
    g_warped_in_camera = true;
  }
  if (x < 0.0f) x = 0.0f;
  if (y < 0.0f) y = 0.0f;
  if (x > render_w - 1) x = static_cast<float>(render_w - 1);
  if (y > render_h - 1) y = static_cast<float>(render_h - 1);
  int client_x = static_cast<int>(x / sx + 0.5f);
  int client_y = static_cast<int>(y / sy + 0.5f);
  // The clip rectangle leaves out the right and bottom edges. A cursor put
  // there comes back one pixel in, which would read as a mouse movement.
  RECT client;
  if (GetClientRect(window, &client)) {
    if (client_x > client.right - 1) client_x = client.right - 1;
    if (client_y > client.bottom - 1) client_y = client.bottom - 1;
  }
  // With the window behind, the real cursor stays where it is: it is in a
  // different program (controller_window_behind=1, or the test key
  // fake_controller_front). The written point is then the cursor itself, so
  // the next frame reads no mouse movement.
  POINT real_cursor = {0, 0};
  const bool leave_cursor = ControllersLeaveCursor() && !RealFocus(window) &&
                            GetCursorPos(&real_cursor) &&
                            ScreenToClient(window, &real_cursor);
  if (!leave_cursor) {
    POINT screen = {client_x, client_y};
    ClientToScreen(window, &screen);
    SetCursorPos(screen.x, screen.y);
  }
  g_true_x = x;
  g_true_y = y;
  g_have_true = true;
  // The game put the cursor here, so mouse motion from before does not count.
  g_pend_x = 0.0f;
  g_pend_y = 0.0f;
  g_written_x = leave_cursor ? real_cursor.x : client_x;
  g_written_y = leave_cursor ? real_cursor.y : client_y;
  int32_t* px = reinterpret_cast<int32_t*>(kPointerX);
  int32_t* py = reinterpret_cast<int32_t*>(kPointerY);
  if (!IsBadWritePtr(px, sizeof(int32_t)) &&
      !IsBadWritePtr(py, sizeof(int32_t))) {
    *px = static_cast<int32_t>(x);
    *py = static_cast<int32_t>(y);
  }
  ++g_warps_mapped;
  return 0;
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

void PointerMapInstall() {
  if (g_hooks_installed) return;
  g_hooks_installed = true;
  Hook(kMouseMoveHandler, reinterpret_cast<void*>(&h_MouseMoveHandler),
       reinterpret_cast<void**>(&o_MouseMoveHandler), "MouseMoveHandler");
  Hook(kMouseSetPosition, reinterpret_cast<void*>(&h_MouseSetPosition),
       reinterpret_cast<void**>(&o_MouseSetPosition), "LHMouse::SetPosition");
  Hook(kGCameraUpdate, reinterpret_cast<void*>(&h_GCameraUpdate),
       reinterpret_cast<void**>(&o_GCameraUpdate), "GCamera::Update");
  Hook(kUnproject, reinterpret_cast<void*>(&h_Unproject),
       reinterpret_cast<void**>(&o_Unproject), "UnprojectScreenPoint");
  Hook(kClickParams, reinterpret_cast<void*>(&h_ClickParams),
       reinterpret_cast<void**>(&o_ClickParams),
       "CameraModeNew3::UpdateClickParams");
}

// The land hold counts, printed under the conversion they were measured with.
void ReportHold() {
  if (g_hold_frames != 0) {
    ModLog("pointer      : land held with the mouse still on %llu frames. The "
           "game's pointer moved on %llu, the camera on %llu.", g_hold_frames,
           g_hold_pointer_moves, g_hold_camera_moves);
    ModLog("pointer      : the camera moved %.3f units at most. Its pointer "
           "came from the %s.", g_hold_largest,
           g_game_from_float ? "exact point" : "whole render pixel");
  }
  g_hold_frames = 0;
  g_hold_pointer_moves = 0;
  g_hold_camera_moves = 0;
  g_hold_largest = 0.0f;
}

void PointerMapSetRebuild(bool on) {
  g_rebuild = on;
  ModLog("pointer      : the camera code %s the game's own camera",
         on ? "sees" : "does not see");
}

// The drag counts for the periodic report. A headset log proves the two
// drag fixes with them: presses that the safe box moved still become land
// drags, those drags use the Hand's exact ray, and the ease runs.
void ReportDrag() {
  if (g_presses != 0 || g_land_drags != 0 || g_smoothed_frames != 0) {
    ModLog("drag         : %llu presses, %llu land drags, %llu moved into the "
           "safe box (%llu from behind), %llu used the exact ray, %llu frames "
           "smoothed", g_presses, g_land_drags, g_box_presses,
           g_behind_presses, g_exact_drags, g_smoothed_frames);
  }
  g_presses = 0;
  g_land_drags = 0;
  g_box_presses = 0;
  g_behind_presses = 0;
  g_exact_drags = 0;
  g_smoothed_frames = 0;
}

// How long the Hand takes to follow the mouse in a land drag. 0 turns the
// ease off. See DragShare.
// grab_probe=1: log the gain of the game's own land drag (stage G0 of M7).
void PointerMapSetGrabProbe(bool on) {
  g_probe = on;
  if (on) {
    ModLog("drag probe   : each land drag logs the motion of the grip, the "
           "head and the camera");
  }
}

void PointerMapSetDragSmoothing(int ms) {
  if (ms < 0) ms = 0;
  if (ms > 200) ms = 200;
  g_drag_smooth_ms = ms;
  if (ms > 0) {
    ModLog("pointer      : a land drag smooths the mouse over %d ms, and the "
           "release ends it at once", ms);
  } else {
    ModLog("pointer      : a land drag follows the mouse with no smoothing");
  }
}

void PointerMapSetRenderSize(int width, int height) {
  g_render_w = width;
  g_render_h = height;
}

void PointerMapSetOptions(bool scale, bool clip) {
  g_scale = scale;
  g_clip = clip;
  ModLog("pointer      : %s, and the cursor %s",
         scale ? "scaled onto the render, so any window size reaches it all"
               : "left as the game maps it",
         clip ? "is held inside the window" : "may leave the window");
}

// MAKE THE WINDOW FIT THE MONITOR, IN THE SHAPE OF THE RENDER.
//
// The window is created the size of the render, so a render bigger than the
// desktop leaves part of the window off the screen. With the cursor scaled
// that no longer costs any reach, so the window can simply be shrunk to fit
// and centred, and the render left alone at whatever size was asked for.
//
// It is shrunk to the RENDER's shape, not the window's. The game creates its
// window as wide as the render and only as tall as the desktop allows, so a
// 2880 by 2880 render came up in a 2544 by 1217 window. The cursor is scaled
// by the ratio of the two, which was 1.13 across and 2.37 down, so moving the
// mouse down moved the Hand more than twice as far as moving it sideways.
// A window the shape of the render scales the cursor the same way both ways.
//
// Done once, when the render size is known and the window is not already that
// shape and inside the desktop.
bool g_fitted = false;
// The client size the fit asked for, and how many times it has been put
// right since. The game's Options menu can come and go after the fit, and a
// menu counted but not there makes the client taller than asked.
int g_fit_w = 0;
int g_fit_h = 0;
int g_fit_corrections = 0;
const int kMaxFitCorrections = 3;

// Put the client back to the size the fit asked for, by the difference.
void CorrectFit(HWND window, int have_w, int have_h) {
  if (g_fit_w <= 0 || g_fit_h <= 0) return;
  if (have_w == g_fit_w && have_h == g_fit_h) return;
  if (g_fit_corrections >= kMaxFitCorrections) return;
  RECT outer;
  if (!GetWindowRect(window, &outer)) return;
  ++g_fit_corrections;
  const int outer_w = outer.right - outer.left + (g_fit_w - have_w);
  const int outer_h = outer.bottom - outer.top + (g_fit_h - have_h);
  SetWindowPos(window, nullptr, 0, 0, outer_w, outer_h,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
  ModLog("pointer      : window client was %d x %d, not %d x %d, so it was "
         "corrected", have_w, have_h, g_fit_w, g_fit_h);
}
int g_want_w = 0;
int g_want_h = 0;

void PointerMapSetWindowSize(int width, int height) {
  g_want_w = width;
  g_want_h = height;
}

void FitWindowOnce(HWND window) {
  RECT client;
  if (!GetClientRect(window, &client)) return;
  const int have_w = client.right - client.left;
  const int have_h = client.bottom - client.top;
  if (have_w <= 0 || have_h <= 0) return;
  if (g_fitted) {
    CorrectFit(window, have_w, have_h);
    return;
  }

  int render_w = 0, render_h = 0;
  if (!RenderSize(&render_w, &render_h)) return;
  g_fitted = true;

  RECT work = {0, 0, 0, 0};
  if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) return;
  const int room_w = work.right - work.left;
  const int room_h = work.bottom - work.top;
  if (room_w <= 0 || room_h <= 0) return;

  int want_w = g_want_w > 0 ? g_want_w : render_w;
  int want_h = g_want_h > 0 ? g_want_h : render_h;

  // Leave room for the frame, the title bar, and the game's own Options menu.
  // Leaving the menu out made the client 20 pixels shorter than asked for, so
  // the cursor still scaled differently down than across.
  RECT frame = {0, 0, want_w, want_h};
  const LONG style = GetWindowLongA(window, GWL_STYLE);
  const LONG ex_style = GetWindowLongA(window, GWL_EXSTYLE);
  const BOOL has_menu = GetMenu(window) != nullptr;
  AdjustWindowRectEx(&frame, style, has_menu, ex_style);
  int frame_w = frame.right - frame.left;
  int frame_h = frame.bottom - frame.top;

  if (frame_w > room_w || frame_h > room_h) {
    const int extra_w = frame_w - want_w;
    const int extra_h = frame_h - want_h;
    const int fit_w = room_w - extra_w;
    const int fit_h = room_h - extra_h;
    // Keep the render's shape, so the cursor scales the same way both ways.
    const double by_width = static_cast<double>(fit_w) / want_w;
    const double by_height = static_cast<double>(fit_h) / want_h;
    const double shrink = by_width < by_height ? by_width : by_height;
    want_w = static_cast<int>(want_w * shrink);
    want_h = static_cast<int>(want_h * shrink);
    frame = {0, 0, want_w, want_h};
    AdjustWindowRectEx(&frame, style, has_menu, ex_style);
    frame_w = frame.right - frame.left;
    frame_h = frame.bottom - frame.top;
  } else if (have_w == want_w && have_h == want_h) {
    // It already fits and is already the right shape, so leave it alone.
    g_fit_w = want_w;
    g_fit_h = want_h;
    return;
  }
  g_fit_w = want_w;
  g_fit_h = want_h;

  const int x = work.left + (room_w - frame_w) / 2;
  const int y = work.top + (room_h - frame_h) / 2;
  SetWindowPos(window, nullptr, x, y, frame_w, frame_h,
               SWP_NOZORDER | SWP_NOACTIVATE);
  ModLog("pointer      : window %d x %d set to %d x %d, the shape of the %d x "
         "%d render, so the mouse moves the Hand at one speed both ways",
         have_w, have_h, want_w, want_h, render_w, render_h);
}

// THE TEST MOUSE (fake_mouse_script). A desktop test must not move the real
// mouse or press its buttons. So these words move the pointer and press the
// left button inside the mod. The form is input@frame+frames, as in
// fake_controller_script:
//
//   home         the pointer goes to fake_mouse_x_pct and fake_mouse_y_pct of
//                the render, on the first frame of the word
//   home2        the same, to fake_mouse_x2_pct and fake_mouse_y2_pct
//   button       the left button is down for the frames
//   move_right, move_left, move_up, move_down
//                the pointer moves fake_mouse_step_px render pixels on each
//                frame
//   wheel_up, wheel_down
//                one click of the wheel, on the first frame
//   tilt_up, tilt_down, turn_left, turn_right
//                the game's tilt key or turn key is held for the frames
//   god          Ctrl F11: the God view goes off or on, on the first frame
//
// The frames count the frames with a drawn scene, from 0. A move goes into
// the pending motion, as a move of the real mouse does, and into the game's
// mouse motion (ControlMap::MouseDelta), one count for each pixel. The
// camera code reads that motion, for example for the drag event of the
// tutorial. The button goes through LHMouse::SetButtons, as a real button
// does. A test run does not touch the real mouse.
void TabletopToggle();

namespace {

enum FakeMouseWord {
  kFmHome,
  kFmHome2,
  kFmButton,
  kFmRight,
  kFmLeft,
  kFmUp,
  kFmDown,
  kFmWheelUp,
  kFmWheelDown,
  kFmTiltUp,
  kFmTiltDown,
  kFmTurnLeft,
  kFmTurnRight,
  kFmGod,
  kFmDouble,
  kFmCount
};
const char* const kFakeMouseName[kFmCount] = {
    "home",      "home2",     "button",     "move_right", "move_left",
    "move_up",   "move_down", "wheel_up",   "wheel_down", "tilt_up",
    "tilt_down", "turn_left", "turn_right", "god",      "double"};
struct FakeMouseStep {
  int word;
  uint64_t start;
  uint64_t frames;
};
const int kMaxFakeMouse = 48;
FakeMouseStep g_fm[kMaxFakeMouse];
int g_fm_count = 0;
float g_fm_step_px = 8.0f;
float g_fm_x[2] = {0.5f, 0.5f};
float g_fm_y[2] = {0.5f, 0.7f};
uint64_t g_fm_frame = 0;
bool g_fm_button = false;
// LHMouse::SetButtons, thiscall (uint8 flag), ret 4, on the LHMouse object.
// 0x01 presses the left button and 0x04 releases it.
const uintptr_t kFmSetButtons = 0x007E4B80;
const uintptr_t kFmMouseObject = 0x00E85204;
typedef void(__fastcall* FmSetButtonsFn)(void* self, void* unused, int flag);
const int kFmLeftPress = 0x01;
const int kFmLeftRelease = 0x04;
// A double click as Windows sends it: down, up, double click, up, one step a
// frame (the word "double", which takes 4 frames).
const int kFmLeftDouble = 0x10;
// The wheel sum (120 for a click, up is more than 0), and the turn and tilt
// that the keys asked for. Left is a turn of more than 0.
const uintptr_t kFmWheelCounts = 0x00C5E8D8;
const uintptr_t kFmMouseDelta = 0x00C5E8D0;
const uintptr_t kFmKeyTurn = 0x00C5B0C8;
const uintptr_t kFmKeyTilt = 0x00C5B0C4;

// Once a frame, from PointerMapApply, while the mouse owns the pointer.
void FakeMouseFrame(int render_w, int render_h) {
  if (g_fm_count == 0 || StereoIdle()) return;
  const uint64_t frame = g_fm_frame++;
  bool button = false;
  for (int i = 0; i < g_fm_count; ++i) {
    const FakeMouseStep& step = g_fm[i];
    const uint64_t frames = step.frames > 0 ? step.frames : 1;
    if (frame < step.start || frame >= step.start + frames) continue;
    const bool first = frame == step.start;
    switch (step.word) {
      case kFmHome:
      case kFmHome2:
        if (first) {
          const int n = step.word == kFmHome ? 0 : 1;
          g_true_x = g_fm_x[n] * static_cast<float>(render_w);
          g_true_y = g_fm_y[n] * static_cast<float>(render_h);
          g_pend_x = 0.0f;
          g_pend_y = 0.0f;
        }
        break;
      case kFmButton:
        button = true;
        break;
      case kFmRight:
      case kFmLeft:
      case kFmUp:
      case kFmDown: {
        const float dx = step.word == kFmRight  ? g_fm_step_px
                         : step.word == kFmLeft ? -g_fm_step_px
                                                : 0.0f;
        const float dy = step.word == kFmDown ? g_fm_step_px
                         : step.word == kFmUp ? -g_fm_step_px
                                              : 0.0f;
        g_pend_x += dx;
        g_pend_y += dy;
        int32_t* delta = reinterpret_cast<int32_t*>(kFmMouseDelta);
        delta[0] += static_cast<int32_t>(dx);
        delta[1] += static_cast<int32_t>(dy);
        break;
      }
      case kFmWheelUp:
      case kFmWheelDown:
        if (first) {
          *reinterpret_cast<int32_t*>(kFmWheelCounts) +=
              step.word == kFmWheelUp ? 120 : -120;
        }
        break;
      case kFmTiltUp:
      case kFmTiltDown:
        *reinterpret_cast<float*>(kFmKeyTilt) =
            step.word == kFmTiltUp ? 1.0f : -1.0f;
        break;
      case kFmTurnLeft:
      case kFmTurnRight:
        *reinterpret_cast<float*>(kFmKeyTurn) =
            step.word == kFmTurnLeft ? 1.0f : -1.0f;
        break;
      case kFmGod:
        if (first) TabletopToggle();
        break;
      case kFmDouble: {
        const int flags[4] = {kFmLeftPress, kFmLeftRelease, kFmLeftDouble,
                              kFmLeftRelease};
        const uint64_t at = frame - step.start;
        if (at < 4) {
          reinterpret_cast<FmSetButtonsFn>(kFmSetButtons)(
              reinterpret_cast<void*>(kFmMouseObject), nullptr,
              flags[at]);
        }
        break;
      }
      default:
        break;
    }
    if (first && step.word != kFmButton) {
      ModLog("pointer      : the test mouse word %s, frame %llu",
             kFakeMouseName[step.word], frame);
    }
  }
  if (button != g_fm_button) {
    g_fm_button = button;
    reinterpret_cast<FmSetButtonsFn>(kFmSetButtons)(
        reinterpret_cast<void*>(kFmMouseObject), nullptr,
        button ? kFmLeftPress : kFmLeftRelease);
    ModLog("pointer      : the test mouse %s the left button, frame %llu",
           button ? "presses" : "releases", frame);
  }
}

// The mouse owns the pointer in this frame, and the pointer is the mapped
// one. Set by PointerMapApply. See PointerMapMouseRay.
bool g_mouse_points = false;

}  // namespace

// WHO OWNS THE POINTER.
//
// The mouse and a motion controller both point. One of them owns the pointer,
// and it stays where the owner left it. The mouse takes it when the Windows
// cursor moved, or when a real button event arrived (controllers.cpp sees
// those). The controller takes it when its aim turned more than 1 degree, or
// when one of its buttons changed. The test key Ctrl+F12 can pin the choice
// to one of them. The mouse always works: when the controller is not usable,
// the pointer falls back to the mouse whatever the owner is.
//
// While a button is held, only the source that pressed it can own the
// pointer. Otherwise a still mouse in a land drag and a turn of the other
// hand's controller would move the pointer with the button down, and the
// drag would put the grab point under the new ray. A grip that carries an
// object counts as a held controller button here (docs/hand.md, "The grip
// holds"), so a bump of the mouse does not take the object away.
enum PointerOwner { kOwnerMouse, kOwnerController };
PointerOwner g_owner = kOwnerMouse;
int g_owner_logs = 0;
const int kMaxOwnerLogs = 60;

// Whether a real mouse button is down, read now, as PointerHeld reads it.
bool MouseHoldsButton() {
  return ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) |
           GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
}

// THE MOUSE THREAD SLEEPS while a different program is the active one
// (MouseInputThreadProc 0x007DBD10 tests gWindowActive). With the game
// active, that thread posts the message 0x8009 to the game window each 40 ms
// in which no mouse move came (0x007DBE88), and the window procedure gives
// it to MouseMoveHandler. That is how the interface gets the pointer of a
// controller: GGame::MouseHandler sends it with CMouse::ProcessPosition. So
// while the controllers act with the window behind and the thread sleeps,
// the mod posts the same message at the same rate. h_MouseMoveHandler then
// gives the game the pointer that it has, so the point of the message does
// not count.
namespace {
const UINT kMouseThreadMessage = 0x8009;
ULONGLONG g_fed_at = 0;
bool g_feed_said = false;
}  // namespace

void FeedPointerBehind(HWND window) {
  if (*reinterpret_cast<const int32_t*>(kWindowActive) != 0) return;
  const ULONGLONG now = GetTickCount64();
  if (now - g_fed_at < 40) return;
  g_fed_at = now;
  PostMessageA(window, kMouseThreadMessage, 0,
               MAKELPARAM(g_written_x, g_written_y));
  if (!g_feed_said) {
    g_feed_said = true;
    ModLog("pointer      : a different program is the active one, so the "
           "mouse thread of the game sleeps. The mod now posts the pointer "
           "message of that thread each 40 ms, while the controllers act");
  }
}

void SetOwner(PointerOwner owner, const char* why) {
  if (owner == g_owner) return;
  g_owner = owner;
  if (g_owner_logs < kMaxOwnerLogs) {
    ++g_owner_logs;
    ModLog("pointer      : the %s owns the pointer (%s)",
           owner == kOwnerController ? "controller" : "mouse", why);
  }
}

// Decide the owner for this frame, and say whether the controller sets the
// pointer. mouse_moved says the Windows cursor moved on its own. The two
// latches are read every frame, so an old event cannot act later. A mouse
// button that is down while a different window is in front does not hold the
// pointer.
bool ChooseOwner(bool focused, bool real_focus, bool mouse_moved) {
  const bool mouse_event = ControllersMouseEvent();
  const bool button_event = ControllersButtonEvent();
  const bool turned = ControllerMoved();
  const int mode = ControllersOwnerMode();
  const bool usable = mode != 2 && focused && !StereoIdle() &&
                      HeadCameraEnabled() && ControllerPointing();
  // A source that holds a button keeps the pointer (see above).
  if (mode == 2) {
    SetOwner(kOwnerMouse, "mouse only");
  } else if (mode == 1) {
    if (usable) SetOwner(kOwnerController, "controller only");
  } else if ((mouse_moved || mouse_event) && !ControllersHoldButtons() &&
             !ControllersGripCarries()) {
    SetOwner(kOwnerMouse, mouse_moved ? "the cursor moved" : "a mouse button");
    ControllerRestMotion();
  } else if (usable && (turned || button_event) &&
             !(real_focus && MouseHoldsButton())) {
    SetOwner(kOwnerController, button_event ? "a button" : "the aim turned");
  }
  return usable && g_owner == kOwnerController;
}

// Called once a frame, straight after the game has updated its pointer.
void PointerMapApply() {
  g_cursor_still = false;
  g_mouse_points = false;
  ControllersSetPointerOwned(false);
  HWND window = GameWindow();
  if (window == nullptr) return;

  FitWindowOnce(window);

  RECT client;
  if (!GetClientRect(window, &client)) return;
  const int client_w = client.right - client.left;
  const int client_h = client.bottom - client.top;
  if (client_w <= 0 || client_h <= 0) return;

  // The two sides of the focus (see RealFocus). The cursor of Windows is
  // held and moved only with the real one.
  const bool real_focus = RealFocus(window);
  const bool focused = real_focus || ControllersActBehind();

  // Only hold the cursor while the game is actually the window in front, or
  // alt and tab would become a trap.
  if (g_clip) {
    if (real_focus) {
      POINT top_left = {client.left, client.top};
      POINT bottom_right = {client.right, client.bottom};
      ClientToScreen(window, &top_left);
      ClientToScreen(window, &bottom_right);
      RECT screen = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
      ClipCursor(&screen);
      g_clipped = true;
    } else if (g_clipped) {
      ClipCursor(nullptr);
      g_clipped = false;
    }
  }

  if (!g_scale) return;
  int render_w = 0, render_h = 0;
  if (!RenderSize(&render_w, &render_h)) return;

  POINT cursor;
  if (!GetCursorPos(&cursor)) return;
  if (!ScreenToClient(window, &cursor)) return;
  g_cursor_still = g_have_true && focused && cursor.x == g_written_x &&
                   cursor.y == g_written_y;

  const float sx = static_cast<float>(render_w) / client_w;
  const float sy = static_cast<float>(render_h) / client_h;

  // The owner of the pointer. The cursor moving on its own is the mouse. A
  // held button freezes the mouse's own motion, so it does not count then.
  // A cursor that moves while a different window is in front is not the
  // game's mouse.
  const bool mouse_moved = g_have_true && real_focus && !PointerHeld() &&
                           (cursor.x != g_written_x || cursor.y != g_written_y);
  const bool use_controller = ChooseOwner(focused, real_focus, mouse_moved);
  ControllersSetPointerOwned(use_controller);
  if (focused && !real_focus) FeedPointerBehind(window);

  // Follow the player's own mouse movement into the float position, and leave
  // the fraction that the head compensation carries untouched.
  // While the pointer is held, the mouse movement is not the Hand's: the
  // cursor goes back to where it was, below.
  // During a land drag the movement arrives over a few frames. See DragShare.
  if (!g_have_true || !focused) {
    g_true_x = cursor.x * sx;
    g_true_y = cursor.y * sy;
    g_have_true = true;
    g_pend_x = 0.0f;
    g_pend_y = 0.0f;
  } else if (use_controller) {
    // The controller sets the pointer below. Mouse motion is not taken.
    g_pend_x = 0.0f;
    g_pend_y = 0.0f;
  } else if (real_focus && !PointerHeld()) {
    g_pend_x += (cursor.x - g_written_x) * sx;
    g_pend_y += (cursor.y - g_written_y) * sy;
  } else {
    g_pend_x = 0.0f;
    g_pend_y = 0.0f;
  }
  if (g_have_true && focused && !use_controller) {
    FakeMouseFrame(render_w, render_h);
  }
  // The land hold proof wants frames where nothing moved the Hand but the
  // head. A step of the ease is the mouse, so it does not count as still.
  if (TakePending()) g_cursor_still = false;

  if (focused) LockToRoom(&g_true_x, &g_true_y, !use_controller);

  // The controller's aim, as a render pixel. A ray behind the head has no
  // pixel, and the pointer keeps its last one. The clamp below cuts a pixel
  // past the edge of the render, so the Hand stops at the edge of the view.
  if (use_controller && g_have_true && focused) {
    float cx, cy;
    bool off_view = false;
    if (ControllerRenderPixel(&cx, &cy, &off_view)) {
      g_true_x = cx;
      g_true_y = cy;
    }
  }

  if (g_true_x < 0.0f) g_true_x = 0.0f;
  if (g_true_y < 0.0f) g_true_y = 0.0f;
  if (g_true_x > render_w - 1) g_true_x = static_cast<float>(render_w - 1);
  if (g_true_y > render_h - 1) g_true_y = static_cast<float>(render_h - 1);

  // Put the Windows cursor where the Hand now is, so the next frame starts
  // from there and a mouse movement is measured against the right place.
  int want_x = static_cast<int>(g_true_x / sx + 0.5f);
  int want_y = static_cast<int>(g_true_y / sy + 0.5f);
  if (want_x > client_w - 1) want_x = client_w - 1;
  if (want_y > client_h - 1) want_y = client_h - 1;
  if (real_focus && (want_x != cursor.x || want_y != cursor.y)) {
    POINT screen = {want_x, want_y};
    ClientToScreen(window, &screen);
    SetCursorPos(screen.x, screen.y);
  }
  g_written_x = real_focus ? want_x : cursor.x;
  g_written_y = real_focus ? want_y : cursor.y;

  int32_t* px = reinterpret_cast<int32_t*>(kPointerX);
  int32_t* py = reinterpret_cast<int32_t*>(kPointerY);
  if (IsBadWritePtr(px, sizeof(int32_t)) || IsBadWritePtr(py, sizeof(int32_t))) {
    return;
  }
  *px = static_cast<int32_t>(g_true_x);
  *py = static_cast<int32_t>(g_true_y);
  g_mouse_points = focused && !use_controller;

  if (!g_said) {
    g_said = true;
    ModLog("pointer      : window %d x %d carries a %d x %d render, so the "
           "cursor is scaled by %.2f across and %.2f down", client_w, client_h,
           render_w, render_h, sx, sy);
  }
}

// How often the head moved the cursor, for the periodic report.
void PointerMapReport() {
  if (g_locked_frames != 0 || g_skipped_jumps != 0) {
    ModLog("pointer      : the head turned the cursor on %llu frames, %llu "
           "jumps ignored", g_locked_frames, g_skipped_jumps);
  }
  if (g_moves_mapped != 0 || g_warps_mapped != 0) {
    ModLog("pointer      : %llu mouse moves and %llu game cursor moves put "
           "onto the render", g_moves_mapped, g_warps_mapped);
  }
  if (g_game_screen_frames != 0) {
    ModLog("pointer      : the camera code saw the game's own screen on %llu "
           "frames", g_game_screen_frames);
  }
  if (g_rebuilt_frames != 0 || g_reapplied_frames != 0) {
    ModLog("pointer      : the camera code saw the game's own camera on %llu "
           "frames, and the head was put back by hand on %llu",
           g_rebuilt_frames, g_reapplied_frames);
  }
  if (g_direct_ray_frames != 0) {
    ModLog("pointer      : the camera code got the direct hand's ray from the "
           "game camera on %llu frames", g_direct_ray_frames);
  }
  ReportHold();
  ReportDrag();
  g_direct_ray_frames = 0;
  g_game_screen_frames = 0;
  g_rebuilt_frames = 0;
  g_reapplied_frames = 0;
  g_locked_frames = 0;
  g_skipped_jumps = 0;
  g_moves_mapped = 0;
  g_warps_mapped = 0;
}

// Let the cursor go, whatever happens next.
void PointerMapRelease() {
  if (g_clipped) {
    ClipCursor(nullptr);
    g_clipped = false;
  }
}

// THE MOUSE RAY IN THE ROOM, for the mouse pan of the God view (tabletop.cpp,
// docs/god-view.md, section 5). The ray of the exact pointer through the head
// of this frame, H * ray, as a unit vector in the room. The cursor is locked
// to the room in this same H, so the ray changes only when the mouse moves.
// The Hand is on the same ray, because the game places it through the same
// pixel and the same head. False when a controller owns the pointer, or when
// the game window does not have the focus.
bool PointerMapMouseRay(float dir_room[3]) {
  if (!g_mouse_points || !g_scale || !g_have_true) return false;
  float head[9], lean[3];
  if (!HeadPoseCurrent(head, lean)) return false;
  float ray[3], hx, hy, fx, fy;
  if (!ScreenRay(g_true_x, g_true_y, ray, &hx, &hy, &fx, &fy)) return false;
  Apply(head, false, ray, dir_room);
  const float length = sqrtf(dir_room[0] * dir_room[0] +
                             dir_room[1] * dir_room[1] +
                             dir_room[2] * dir_room[2]);
  if (!(length > 1e-6f)) return false;
  for (int i = 0; i < 3; ++i) dir_room[i] /= length;
  return true;
}

// The test mouse of a desktop run. See FakeMouseFrame.
void PointerMapSetFakeMouse(const char* script, int step_px,
                            const int x_pct[2], const int y_pct[2]) {
  g_fm_count = 0;
  g_fm_frame = 0;
  g_fm_step_px = static_cast<float>(step_px > 0 ? step_px : 8);
  for (int n = 0; n < 2; ++n) {
    g_fm_x[n] = Clamp(x_pct[n] * 0.01f, 0.0f, 1.0f);
    g_fm_y[n] = Clamp(y_pct[n] * 0.01f, 0.0f, 1.0f);
  }
  if (script == nullptr || script[0] == 0) return;
  const char* p = script;
  while (*p != 0) {
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 0) break;
    const char* end = p;
    while (*end != 0 && *end != ' ' && *end != '\t') ++end;
    char word[64];
    const size_t length = static_cast<size_t>(end - p);
    p = end;
    if (length >= sizeof(word)) {
      ModLog("pointer      : a test mouse word is too long, skipped");
      continue;
    }
    memcpy(word, end - length, length);
    word[length] = 0;
    char* at = strchr(word, '@');
    char* plus = at != nullptr ? strchr(at, '+') : nullptr;
    int input = -1;
    if (at != nullptr && plus != nullptr) {
      *at = 0;
      *plus = 0;
      for (int i = 0; i < kFmCount; ++i) {
        if (strcmp(word, kFakeMouseName[i]) == 0) input = i;
      }
    }
    if (input < 0 || g_fm_count >= kMaxFakeMouse) {
      ModLog("pointer      : the test mouse word \"%.40s\" is not "
             "input@frame+frames, skipped", word);
      continue;
    }
    FakeMouseStep& step = g_fm[g_fm_count++];
    step.word = input;
    step.start = static_cast<uint64_t>(strtoul(at + 1, nullptr, 10));
    step.frames = static_cast<uint64_t>(strtoul(plus + 1, nullptr, 10));
  }
  ModLog("pointer      : the test mouse has %d words, a step of %.0f render "
         "pixels, its home at %.0f%% and %.0f%% of the render, and its second "
         "home at %.0f%% and %.0f%%", g_fm_count, g_fm_step_px,
         g_fm_x[0] * 100.0f, g_fm_y[0] * 100.0f, g_fm_x[1] * 100.0f,
         g_fm_y[1] * 100.0f);
}
