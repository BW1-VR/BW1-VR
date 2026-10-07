// The cut scene window: in the God view, a cut scene plays on a screen in the
// room (docs/cutscene-window.md).
//
// WHY. A cut scene moves the camera, and camera motion that the head did not
// make is not comfortable when the picture fills the view. Before this file
// the mod held each shot and cut between the shots (script_camera.cpp), and
// it hid the cinema bars. The player could not tell a cut scene from play,
// and the shots lost the motion and the frame that the game gave them. A
// picture that moves on a screen 2 m away is comfortable. So for the length
// of a cut scene the view becomes the stereoscopic window of play mode 1.
//
// THE SIGNAL. HelpSystem::SetWideScreen 0x005C6AD0 puts the cinema bars up
// and takes them down. It keeps the state at HelpSystem + 0x45E8 (1 while
// the bars are up), and GGame + 0x25005C is the pointer to the HelpSystem
// (GScript::SetWideScreen 0x006F7BF0 reads both). The script command
// SET_WIDESCREEN and the hand demo (GInterface::StartPlayBack 0x005DAD60)
// put the bars up. This file only reads the state. It hooks nothing.
//
// THE RULE is window_math::Wanted: the window is wanted while the bars are
// up, and after that while a story camera still owns the view, for at most
// cutscene_window_linger_ms. A story camera with no bars (a follow camera,
// for example) keeps the held shots of script_camera.cpp in the full view.
//
// THE SEQUENCE.
//
// 1. The bars go up. Black walls close around an opening in the plane of
//    the screen, in cutscene_window_ms (500). The picture inside is still
//    the full view.
// 2. The opening has the size of the screen. From the next frame the frame
//    is flat (CutsceneWindowFlat): head_camera.cpp puts no head on the
//    camera, the God view stands aside, script_camera.cpp holds no shot and
//    stops no shake, the view angle is the game's own, draw_hooks.cpp shows
//    the bars and the help text as the game made them, and the eye pictures
//    go out as two quads at the screen (xr_screen.cpp). Each module that asks
//    HeadCameraEnabled gets a no, so it does what it does in play mode 1.
// 3. The window is not wanted now. The next frame is a full view frame, and
//    the God view is built where the story left the camera. The walls open
//    in the same time, and nothing in the view moves while they open.
//
// ONE STATE FOR A FRAME. The state changes only in CutsceneWindowEndFrame,
// after a frame went out. The main camera call, both eye passes and the
// layers of the next frame read the same state.
//
// THE PLACE (cutscene_face_head). When a cut scene starts, the screen goes
// ahead of the head of that moment: at its yaw, level, at the height of the
// recenter pose. It stays there in the room until the next cut scene. A
// recenter puts it at the recenter pose.
//
// THE DEPTH LIMIT (cutscene_depth). The window is a window: nothing in the
// picture is in front of the screen, and the eyes are parallel for a point
// far away. The two eye pictures are the distance between the eyes apart on
// the screen, and the eye cameras are as far apart as the nearest thing in
// view allows (window_math::PlaneStep). CutsceneWindowBeginFlat sets that for
// a frame, once, before its eye passes. The opening is the part of the
// screen that has both pictures, so it is one rectangle for both eyes.
//
// THE NEAREST THING IN VIEW. draw_hooks.cpp gives each world batch of both
// eye passes to CutsceneWindowProbeBatch. The measure of a frame sets the
// depth of the next frame.
//
// THE SUBTITLES (cutscene_subtitles). The subtitle card of ui_panel.cpp
// takes the help text of a flat frame too, and CutsceneWindowSubtitleQuad
// puts the card in the plane of the screen, under the scene.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../common/bw_addresses.h"
#include "../vr/xr_screen.h"
#include "cutscene_window.h"
#include "cutscene_window_math.h"

extern void ModLog(const char* format, ...);
// script_camera.cpp: the last main camera call was a story or a follow mode.
bool ScriptCameraStoryOwns();
// head_pose.cpp: the recenter pose in the LOCAL space of the runtime, the
// count of the recenters, and the life size scale in units per meter.
bool HeadPoseOrigin(float position_out[3], float rotation_out[4]);
uint64_t HeadPoseOriginSerial();
float HeadPoseScale();
// head_camera.cpp: the head goes on the camera in the full view.
bool HeadCameraOn();
// frame_source.cpp: the size of the render.
bool FrameSourceSize(uint32_t* width, uint32_t* height);
// fov.cpp: the fixed view angle of the full view, and the game's own.
void FovSetAbsolute(float radians);
float FovAbsolute();
void FovSetScale(float scale);
float FovScale();
// spirit_anchor.cpp: the view jumped, so place the spirits again.
void SpiritAnchorCameraCut();

namespace {

const uintptr_t kGame = 0x00D0195C;    // GGame::g_game
const int kGameHelpSystem = 0x25005C;  // GGame + this: the HelpSystem pointer
const int kHelpWideScreen = 0x45E8;    // HelpSystem + this: 1 with the bars up
// The share of the cinema bars that shows now, 0 to 1 (draw_hooks.cpp).
const uintptr_t kWideScreenShown = 0x00EB9950;
// The shape of the scene between the bars, height over width: 0.5625. The
// bar height function 0x005C5780 reads it.
const uintptr_t kSceneShape = 0x008C7A50;

// cutscene_depth
const int kDepthOff = 0;      // the stereo as before, with no limit
const int kDepthNearest = 1;  // from the nearest thing in view
const int kDepthFixed = 2;    // the worst case: the near plane of the game

struct Settings {
  bool read;
  bool on;            // cutscene_window
  bool god;           // play mode 4, or 0, which is the God view too
  float distance;     // screen_distance_cm, in meters
  float width;        // screen_width_cm, in meters
  float seconds;      // cutscene_window_ms
  float linger;       // cutscene_window_linger_ms
  bool capture;       // cutscene_window_capture, a test
  // A test with no key press: the window goes off at the first frame and on
  // again at the second, as numpad 0 does. 0 is no change.
  uint64_t test_off;  // cutscene_window_test_off
  uint64_t test_on;   // cutscene_window_test_on
  bool face_head;     // cutscene_face_head
  int depth;          // cutscene_depth
  float units;        // cutscene_units_per_meter_x10, 0 is the life size scale
  // The same test for the depth limit, as numpad 1 does.
  uint64_t depth_test_off;  // cutscene_depth_test_off
  uint64_t depth_test_on;   // cutscene_depth_test_on
  bool subtitles;     // cutscene_subtitles
  float text_width;   // cutscene_subtitle_width_cm, in meters
  int text_place;     // cutscene_subtitle_place
};
Settings g_set = {false, true, true, 2.0f, 2.6f,          0.5f, 3.0f,
                  false, 0,    0,    true, kDepthNearest, 0.0f, 0,
                  0,     true, 1.4f, 0};

window_math::State g_state = {false, 1.0f};
bool g_wanted = false;
// The fixed view angle that the full view had when the frame went flat.
float g_saved_fov = 0.0f;

// THE PLACE. The head of the last frame in the LOCAL space, and the anchor
// that the screen hangs from. With no anchor the screen hangs from the
// recenter pose.
bool g_have_head = false;
float g_head[3] = {0.0f, 0.0f, 0.0f};
float g_head_turn[4] = {0.0f, 0.0f, 0.0f, 1.0f};
bool g_have_anchor = false;
window_math::Anchor g_anchor = {};
uint64_t g_anchor_serial = 0;  // the recenter count when the anchor was set

// THE DEPTH LIMIT of the frame that draws now. CutsceneWindowBeginFlat sets
// these before the eye passes, and nothing changes them in the frame.
int g_crop = 0;              // columns that each eye picture loses
float g_plane = 0.0f;        // the depth on the screen, in units
float g_separation = 0.0f;   // between the eye cameras, in units
window_math::DepthState g_depth;
LARGE_INTEGER g_depth_last = {0};
// The camera of the last flat frame, for the test of a new shot.
bool g_have_camera = false;
float g_camera_position[3] = {0.0f, 0.0f, 0.0f};
float g_camera_forward[3] = {0.0f, 0.0f, 1.0f};
// A shot started, and no frame of it gave a measure yet. The next measure
// is the first of the shot.
bool g_shot_fresh = false;
// The measure of the last flat frame.
window_math::DepthGrid g_grid;
bool g_grid_filled = false;  // the eye passes gave their batches to the grid
float g_grid_near = 0.0f;    // the near plane of that frame
uint32_t g_grid_caller = 0;  // the caller of the batch with the nearest vertex
// For the log: the plane of the last line, and the range of a report.
float g_said_plane = 0.0f;
int g_depth_logs = 0;
const int kMaxDepthLogs = 400;
float g_report_plane[2] = {0.0f, 0.0f};
float g_report_nearest[2] = {0.0f, 0.0f};  // the least and the last measure
uint64_t g_report_shots = 0;
uint64_t g_report_measures = 0;
bool g_said_no_depth = false;

LARGE_INTEGER g_frequency = {0};
LARGE_INTEGER g_last = {0};
// Seconds since the bars were up.
float g_since_bars = 1000.0f;

// The last values, so each change gives one log line.
int g_said_bars = -2;
bool g_said_story = false;
bool g_was_flat = false;

// The frames to save the eye pictures of, with cutscene_window_capture=1.
uint64_t g_capture_at[3] = {0, 0, 0};

// For the report and the log.
uint64_t g_flat_frames = 0;
uint64_t g_change_frames = 0;
uint64_t g_scenes = 0;
int g_logs = 0;
const int kMaxLogs = 600;
// The lines for each frame of a change have a limit of their own, so they
// cannot use up the lines for the state.
int g_frame_logs = 0;
const int kMaxFrameLogs = 540;
const float kWallBorder = 40.0f;  // meters
// The longest frame that the change counts in full, in seconds.
const float kLongestStep = 0.035f;

bool Say() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

bool SayFrame() {
  if (g_frame_logs >= kMaxFrameLogs) return false;
  ++g_frame_logs;
  return true;
}

void ReadSettings() {
  if (g_set.read) return;
  g_set.read = true;
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return;
  slash[1] = 0;
  strcat_s(path, MAX_PATH, "bwvr.ini");
  const auto number = [&path](const char* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntA(
        "bwvr", key, static_cast<UINT>(fallback), path));
  };
  const auto limit = [](int value, int least, int most) {
    return value < least ? least : (value > most ? most : value);
  };
  g_set.on = number("cutscene_window", 1) != 0;
  const int mode = number("play_mode", 4);
  g_set.god = mode == 4 || mode == 0;
  g_set.distance = limit(number("screen_distance_cm", 200), 50, 1000) / 100.0f;
  g_set.width = limit(number("screen_width_cm", 260), 50, 1000) / 100.0f;
  g_set.seconds = limit(number("cutscene_window_ms", 500), 0, 3000) / 1000.0f;
  g_set.linger =
      limit(number("cutscene_window_linger_ms", 3000), 0, 20000) / 1000.0f;
  g_set.capture = number("cutscene_window_capture", 0) != 0;
  g_set.test_off =
      static_cast<uint64_t>(limit(number("cutscene_window_test_off", 0), 0,
                                  100000000));
  g_set.test_on =
      static_cast<uint64_t>(limit(number("cutscene_window_test_on", 0), 0,
                                  100000000));
  g_set.face_head = number("cutscene_face_head", 1) != 0;
  g_set.depth = limit(number("cutscene_depth", kDepthNearest), kDepthOff,
                      kDepthFixed);
  g_set.units =
      limit(number("cutscene_units_per_meter_x10", 0), 0, 10000) / 10.0f;
  g_set.depth_test_off =
      static_cast<uint64_t>(limit(number("cutscene_depth_test_off", 0), 0,
                                  100000000));
  g_set.depth_test_on =
      static_cast<uint64_t>(limit(number("cutscene_depth_test_on", 0), 0,
                                  100000000));
  g_set.subtitles = number("cutscene_subtitles", 1) != 0;
  g_set.text_width =
      limit(number("cutscene_subtitle_width_cm", 140), 30, 400) / 100.0f;
  g_set.text_place = limit(number("cutscene_subtitle_place", 0), 0, 1);
  if (!g_set.god) return;
  ModLog("cut scene    : cutscene_window=%d. %s", g_set.on ? 1 : 0,
         g_set.on ? "In the God view a cut scene plays on a screen in the room"
                  : "A cut scene plays in the full view, with held shots");
  ModLog("cut scene    : the screen is %.2f m wide, %.2f m ahead of %s. The "
         "view closes and opens in %.0f ms.", g_set.width, g_set.distance,
         g_set.face_head ? "the head at the start of the cut scene "
                           "(cutscene_face_head=1)"
                         : "the recenter pose (cutscene_face_head=0)",
         g_set.seconds * 1000.0f);
  ModLog("cut scene    : cutscene_depth=%d. %s", g_set.depth,
         g_set.depth == kDepthNearest
             ? "The nearest thing in view is on the screen or behind it"
             : g_set.depth == kDepthFixed
                   ? "The near plane of the game is on the screen, so all of "
                     "the scene is behind it"
                   : "The stereo has no depth limit, as before");
  ModLog("cut scene    : cutscene_subtitles=%d. %s", g_set.subtitles ? 1 : 0,
         g_set.subtitles
             ? (g_set.text_place == 1
                    ? "The subtitles show on a card under the opening"
                    : "The subtitles show on a card under the scene")
             : "The subtitles stay in the picture");
}

// 1 while the cinema bars are up, 0 while they are not, -1 with no game.
int ReadBars() {
  const uint8_t* const* game_at = reinterpret_cast<const uint8_t* const*>(kGame);
  const uint8_t* const game = *game_at;
  if (game == nullptr ||
      IsBadReadPtr(game + kGameHelpSystem, sizeof(uintptr_t))) {
    return -1;
  }
  const uint8_t* const help =
      *reinterpret_cast<const uint8_t* const*>(game + kGameHelpSystem);
  if (help == nullptr ||
      IsBadReadPtr(help + kHelpWideScreen, sizeof(int32_t))) {
    return -1;
  }
  return *reinterpret_cast<const int32_t*>(help + kHelpWideScreen) != 0 ? 1 : 0;
}

// The transform of the game for this frame, or null.
const bw::tech::InfoTransform* Info() {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info)) || info->res_x <= 0 ||
      info->res_y <= 0 || !(info->inv_half_tan_fov_x > 0.0f)) {
    return nullptr;
  }
  return info;
}

// The columns that each eye picture loses for eyes `ipd` meters apart. 0
// with no depth limit.
int CropFor(float ipd) {
  if (g_set.depth == kDepthOff) return 0;
  const bw::tech::InfoTransform* info = Info();
  if (info == nullptr) return 0;
  return window_math::Crop(ipd, g_set.width, info->res_x);
}

// The height of a cinema bar now, in pixels of the render. The game draws
// its bars with this height (0x005C5780).
float BarPixels(const bw::tech::InfoTransform& info) {
  const float* shown = reinterpret_cast<const float*>(kWideScreenShown);
  const float* shape = reinterpret_cast<const float*>(kSceneShape);
  if (IsBadReadPtr(shown, sizeof(float)) ||
      IsBadReadPtr(shape, sizeof(float)) || !(*shown > 0.0f) ||
      !(*shape > 0.0f)) {
    return 0.0f;
  }
  const float share = *shown > 1.0f ? 1.0f : *shown;
  const float bars = (info.res_y - info.res_x * *shape) * share * 0.5f;
  return bars > 0.0f ? bars : 0.0f;
}

// A cut scene starts with the view open, so the screen takes its place: ahead
// of the head of now, level, at the height of the recenter pose.
void PlaceScreen(uint64_t frame) {
  float origin[3] = {0.0f, 0.0f, 0.0f}, turn[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  const bool have_origin = HeadPoseOrigin(origin, turn);
  g_anchor_serial = HeadPoseOriginSerial();
  g_have_anchor = g_set.face_head && g_have_head;
  if (!g_have_anchor) {
    if (Say()) {
      ModLog("cut scene    : frame %llu: the screen goes ahead of the "
             "recenter pose, because %s", frame,
             g_set.face_head ? "the mod has no head pose"
                             : "cutscene_face_head is 0");
    }
    return;
  }
  window_math::AnchorAtHead(g_head, g_head_turn,
                            have_origin ? origin[1] : g_head[1], &g_anchor);
  if (Say()) {
    ModLog("cut scene    : frame %llu: the screen goes ahead of the head: the "
           "head is at %.2f %.2f %.2f m with a yaw of %.1f degrees, and the "
           "screen is level at a height of %.2f m (the recenter pose has a "
           "yaw of %.1f degrees)", frame, g_head[0], g_head[1], g_head[2],
           window_math::YawOf(g_head_turn) * 57.29578f, g_anchor.position[1],
           have_origin ? window_math::YawOf(turn) * 57.29578f : 0.0f);
  }
}

// The opening of this frame: the part of the screen that both eye pictures
// have. `crop` is the columns that each eye picture loses. With no anchor
// the screen hangs from the recenter pose, and with no recenter pose yet it
// is straight ahead of the origin of the LOCAL space, where play mode 1 puts
// its screen.
void OpeningNow(int crop, window_math::Screen* opening) {
  float anchor[3] = {0.0f, 0.0f, 0.0f};
  float yaw[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  // A recenter while the screen is up: the screen goes to the recenter pose,
  // which is the head of that moment.
  if (g_have_anchor && HeadPoseOriginSerial() != g_anchor_serial) {
    g_have_anchor = false;
    if (Say()) {
      ModLog("cut scene    : a recenter, so the screen goes ahead of the "
             "recenter pose");
    }
  }
  float origin[3], turn[4];
  if (g_have_anchor) {
    for (int i = 0; i < 3; ++i) anchor[i] = g_anchor.position[i];
    for (int i = 0; i < 4; ++i) yaw[i] = g_anchor.turn[i];
  } else if (HeadPoseOrigin(origin, turn)) {
    for (int i = 0; i < 3; ++i) anchor[i] = origin[i];
    for (int i = 0; i < 4; ++i) yaw[i] = turn[i];
  }
  uint32_t w = 0, h = 0;
  float shape = 1.0f;
  float share = 1.0f;
  if (FrameSourceSize(&w, &h) && w > 0 && h > 0) {
    shape = static_cast<float>(h) / static_cast<float>(w);
    if (crop > 0 && static_cast<uint32_t>(crop) < w) {
      share = static_cast<float>(w - crop) / static_cast<float>(w);
    }
  }
  window_math::ScreenPose(anchor, yaw, g_set.distance, g_set.width, shape,
                          opening);
  // The eye pictures keep their size on the screen. The opening is narrower
  // by the distance between the two pictures.
  opening->width *= share;
}

// One line for a change of the depth of the picture.
void SayDepth(uint64_t frame, const char* why, float nearest, float strict,
              float rule_units) {
  g_said_plane = g_plane;
  if (g_depth_logs >= kMaxDepthLogs) return;
  ++g_depth_logs;
  char measure[160];
  if (nearest > 0.0f) {
    _snprintf_s(measure, sizeof(measure), _TRUNCATE,
                "The nearest thing in view is %.1f units away (the nearest "
                "vertex %.1f, from 0x%08X)", nearest, strict, g_grid_caller);
  } else {
    _snprintf_s(measure, sizeof(measure), _TRUNCATE,
                "No measure of the nearest thing");
  }
  ModLog("cut scene    : frame %llu: %s. %s. The screen plane is at %.1f "
         "units: %.2f units for each meter of depth (at most %.1f), and the "
         "eye cameras are %.3f units apart", frame, why, measure, g_plane,
         g_set.distance > 0.0f ? g_plane / g_set.distance : 0.0f, rule_units,
         g_separation);
}

// The frame after this one is the first flat frame. The game's own view
// angle goes on now, so the main camera call of that frame builds with it.
void EnterFlat(uint64_t frame) {
  g_saved_fov = FovAbsolute();
  if (g_saved_fov > 0.0f) {
    FovSetAbsolute(0.0f);
    FovSetScale(FovScale());
  }
  // The depth limit starts again. The first flat frame has no measure of its
  // scene, so it gets the worst case.
  g_depth = window_math::DepthState();
  g_depth_last.QuadPart = 0;
  g_have_camera = false;
  g_shot_fresh = false;
  g_grid_filled = false;
  g_said_plane = 0.0f;
  ++g_scenes;
  if (g_set.capture) {
    g_capture_at[0] = frame + 2;
    g_capture_at[1] = frame + 90;
  }
  if (Say()) {
    ModLog("cut scene    : frame %llu: the view is the window now. The game "
           "camera moves as the game made it, with the game's view angle "
           "(the full view had %.1f degrees)", frame + 1,
           g_saved_fov * 57.29578f);
  }
}

// The frame after this one is a full view frame again.
void LeaveFlat(uint64_t frame) {
  if (g_saved_fov > 0.0f) FovSetAbsolute(g_saved_fov);
  g_saved_fov = 0.0f;
  SpiritAnchorCameraCut();
  if (g_set.capture) g_capture_at[2] = frame + 2;
  if (Say()) {
    ModLog("cut scene    : frame %llu: the window ends. The full view is "
           "built again, and the walls open", frame + 1);
  }
}

}  // namespace

bool CutsceneWindowFlat() { return g_state.flat; }

// True while the cinema bars of the game are up at this moment, read from
// the game and not from the last frame. A script puts the bars up in a game
// turn, before the frame that closes the view to the window.
bool CutsceneWindowBarsUp() { return ReadBars() == 1; }

void CutsceneWindowHead(const float center[3], const float orientation[4]) {
  for (int i = 0; i < 3; ++i) g_head[i] = center[i];
  for (int i = 0; i < 4; ++i) g_head_turn[i] = orientation[i];
  g_have_head = true;
}

bool CutsceneWindowBeginFlat(float ipd, uint64_t frame, float* separation,
                             float* ui_shift) {
  g_crop = 0;
  if (!g_state.flat || g_set.depth == kDepthOff) return false;
  const bw::tech::InfoTransform* info = Info();
  const float* near_at = reinterpret_cast<const float*>(bw::tech::kNearPlane);
  const float* camera_at =
      reinterpret_cast<const float*>(bw::tech::kCameraPos);
  const float* matrix =
      reinterpret_cast<const float*>(bw::tech::kWorldToCameraUnscaled);
  const int crop =
      info != nullptr ? window_math::Crop(ipd, g_set.width, info->res_x) : 0;
  if (info == nullptr || crop <= 0 || IsBadReadPtr(near_at, sizeof(float)) ||
      !(*near_at > 0.0f) || IsBadReadPtr(camera_at, sizeof(float) * 3) ||
      IsBadReadPtr(matrix, sizeof(float) * 12)) {
    if (!g_said_no_depth) {
      g_said_no_depth = true;
      ModLog("cut scene    : frame %llu: no depth limit, because the mod "
             "cannot read the camera of the game. The stereo is as before",
             frame);
    }
    return false;
  }
  g_said_no_depth = false;

  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  float dt = 0.0f;
  if (g_depth_last.QuadPart != 0 && g_frequency.QuadPart != 0) {
    dt = static_cast<float>(now.QuadPart - g_depth_last.QuadPart) /
         static_cast<float>(g_frequency.QuadPart);
    if (dt > 0.1f) dt = 0.1f;
  }
  g_depth_last = now;

  window_math::DepthRule rule;
  rule.screen_distance = g_set.distance;
  rule.units_per_meter = g_set.units > 0.0f ? g_set.units : HeadPoseScale();
  rule.near_plane = *near_at;

  // The measure of the frame before. Its rhw has the near plane of that
  // frame in it.
  float nearest = 0.0f, strict = 0.0f;
  if (g_set.depth == kDepthNearest && g_grid_filled) {
    const float rhw = window_math::GridNearest(g_grid, window_math::kGridRank);
    if (rhw > 0.0f) nearest = g_grid_near / rhw;
    if (g_grid.strict > 0.0f) strict = g_grid_near / g_grid.strict;
  }
  g_grid_filled = false;
  if (nearest > 0.0f) {
    ++g_report_measures;
    if (g_report_nearest[0] == 0.0f || nearest < g_report_nearest[0]) {
      g_report_nearest[0] = nearest;
    }
    g_report_nearest[1] = nearest;
  }

  // The forward axis is the third column of the camera matrix.
  float forward[3] = {matrix[2], matrix[5], matrix[8]};
  const float length = sqrtf(forward[0] * forward[0] +
                             forward[1] * forward[1] +
                             forward[2] * forward[2]);
  if (length > 1e-6f) {
    for (int i = 0; i < 3; ++i) forward[i] /= length;
  }
  const bool cut = g_have_camera &&
                   window_math::ShotCut(g_camera_position, g_camera_forward,
                                        camera_at, forward, nearest);
  for (int i = 0; i < 3; ++i) {
    g_camera_position[i] = camera_at[i];
    g_camera_forward[i] = forward[i];
  }
  g_have_camera = true;
  if (cut) ++g_report_shots;

  const bool had = g_depth.have;
  bool first_of_shot = false;
  if (g_set.depth == kDepthFixed) {
    g_depth = window_math::DepthState();
    g_plane = window_math::PlaneStep(&g_depth, 0.0f, false, dt, rule);
  } else {
    // The measure is of the frame before. In the frame of a cut it is of
    // the old shot, so that frame keeps the rule of a shot. The next frame
    // that has a measure and is not a cut has the first measure of the new
    // shot. A story can cut while the picture is black, so that frame is not
    // always the next one. A camera that moves fast enough to be a cut in
    // each frame keeps the rule of a shot, so a nearer thing still takes the
    // plane with it.
    first_of_shot = g_shot_fresh && !cut && nearest > 0.0f;
    g_plane = window_math::PlaneStep(&g_depth, nearest, first_of_shot, dt,
                                     rule);
    if (cut) {
      g_shot_fresh = true;
    } else if (nearest > 0.0f) {
      g_shot_fresh = false;
    }
  }
  const float shift = window_math::Shift(crop, g_set.width, info->res_x);
  g_crop = crop;
  g_separation = window_math::SeparationFor(
      shift, g_set.width, 1.0f / info->inv_half_tan_fov_x, g_plane);
  *separation = g_separation;
  // The flat interface is on the screen: each eye moves it half of the
  // distance between the two pictures.
  *ui_shift = 0.5f * static_cast<float>(crop);

  if (g_report_plane[0] == 0.0f || g_plane < g_report_plane[0]) {
    g_report_plane[0] = g_plane;
  }
  if (g_plane > g_report_plane[1]) g_report_plane[1] = g_plane;

  const bool first_measure = g_depth.have && (!had || first_of_shot);
  const float change =
      g_said_plane > 0.0f ? fabsf(g_plane - g_said_plane) / g_said_plane
                          : 1.0f;
  if (g_said_plane == 0.0f) {
    SayDepth(frame, g_set.depth == kDepthFixed
                        ? "the depth limit is the near plane of the game"
                        : "the first frame in the window has the worst case",
             nearest, strict, rule.units_per_meter);
  } else if (first_measure && change > 0.05f) {
    SayDepth(frame, "a new shot", nearest, strict, rule.units_per_meter);
  } else if (change > 0.15f) {
    SayDepth(frame,
             g_plane < g_said_plane ? "a thing came nearer in the shot"
                                    : "the nearest thing went away in the shot",
             nearest, strict, rule.units_per_meter);
  }
  return true;
}

bool CutsceneWindowProbeStart(int eye) {
  if (eye == 0) g_grid_filled = false;
  if (!g_state.flat || g_set.depth != kDepthNearest || g_crop <= 0) {
    return false;
  }
  const bw::tech::InfoTransform* info = Info();
  const float* near_at = reinterpret_cast<const float*>(bw::tech::kNearPlane);
  if (info == nullptr || IsBadReadPtr(near_at, sizeof(float)) ||
      !(*near_at > 0.0f)) {
    return false;
  }
  const float crop = static_cast<float>(g_crop);
  const float res_x = static_cast<float>(info->res_x);
  if (eye == 0) {
    // The left eye shows its picture less the columns at its left side. The
    // cinema bars hide the top and the bottom of the render, so a thing
    // there is not in view.
    const float bar = BarPixels(*info);
    window_math::GridStart(&g_grid, crop, bar, res_x,
                           static_cast<float>(info->res_y) - bar);
    g_grid_near = *near_at;
    g_grid_caller = 0;
    g_grid_filled = true;
    return true;
  }
  // The right eye shows its picture less the columns at its right side.
  if (!g_grid_filled) return false;
  window_math::GridEye(&g_grid, 0.0f, res_x - crop);
  return true;
}

void CutsceneWindowProbeBatch(const void* verts, uint32_t count,
                              const uint16_t* idx, uint32_t n,
                              uint32_t caller) {
  const float before = g_grid.strict;
  window_math::GridBatch(&g_grid, verts, count, idx, n, 32);
  if (g_grid.strict > before) g_grid_caller = caller;
}

bool CutsceneWindowSubtitles() { return g_state.flat && g_set.subtitles; }

bool CutsceneWindowSubtitleQuad(uint32_t render_w, uint32_t render_h,
                                const int rect[4], float position[3],
                                float orientation[4], float* width,
                                float* height) {
  if (!g_state.flat || render_w == 0 || render_h == 0) return false;
  window_math::Screen opening;
  OpeningNow(g_crop, &opening);
  // The eye picture is g_set.width wide on the screen, and the scene is the
  // part of it between the cinema bars.
  const float picture_height = opening.height;
  const float* shape = reinterpret_cast<const float*>(kSceneShape);
  const float scene_shape =
      IsBadReadPtr(shape, sizeof(float)) ? 0.5625f : *shape;
  const float kGap = 0.04f;  // meters between the scene and the card
  const float top =
      (g_set.text_place == 1
           ? 0.5f * picture_height
           : window_math::SceneHalfHeight(g_set.width, picture_height,
                                          scene_shape)) +
      kGap;
  const float rect_f[4] = {static_cast<float>(rect[0]),
                           static_cast<float>(rect[1]),
                           static_cast<float>(rect[2]),
                           static_cast<float>(rect[3])};
  window_math::TextQuad quad;
  window_math::SubtitleQuad(opening, top, g_set.text_width,
                            static_cast<float>(render_w), rect_f, &quad);
  for (int i = 0; i < 3; ++i) position[i] = quad.position[i];
  for (int i = 0; i < 4; ++i) orientation[i] = quad.orientation[i];
  *width = quad.width;
  *height = quad.height;
  return quad.width > 0.0f && quad.height > 0.0f;
}

void CutsceneWindowStage(bool to_headset, uint64_t frame, float ipd) {
  if (g_state.flat) ++g_flat_frames;
  const bool changing = !g_state.flat && g_state.open < 1.0f;
  if (changing) ++g_change_frames;
  // The first flat frame gets a log line too.
  const bool first_flat = g_state.flat && !g_was_flat;
  g_was_flat = g_state.flat;
  if (!g_state.flat && !changing) return;

  // A flat frame has the crop of its eye passes. The walls close to the
  // opening that the first flat frame has.
  const int crop = g_state.flat ? g_crop : CropFor(ipd);
  window_math::Screen screen;
  OpeningNow(crop, &screen);
  bwvr::WindowLayer layer = {};
  layer.screen = g_state.flat;
  layer.mask = changing;
  layer.crop = g_state.flat ? crop : 0;
  for (int i = 0; i < 3; ++i) layer.position[i] = screen.position[i];
  for (int i = 0; i < 4; ++i) layer.orientation[i] = screen.orientation[i];
  layer.width = screen.width;
  layer.height = screen.height;
  float open_w = screen.width, open_h = screen.height;
  if (changing) {
    const float scale = window_math::OpenScale(g_state.open);
    open_w *= scale;
    open_h *= scale;
    layer.alpha = window_math::MaskAlpha(g_state.open);
    window_math::Wall walls[4];
    window_math::Walls(screen, open_w, open_h, kWallBorder, walls);
    for (int k = 0; k < 4; ++k) {
      for (int i = 0; i < 3; ++i) {
        layer.wall_position[k][i] = walls[k].position[i];
      }
      layer.wall_size[k][0] = walls[k].width;
      layer.wall_size[k][1] = walls[k].height;
    }
  }
  if (to_headset) bwvr::XrScreen::StageWindow(layer);

  if (changing && SayFrame()) {
    ModLog("cut scene    : frame %llu: open %.3f, the opening is %.2f by "
           "%.2f m and the walls are %.0f%% black, at %.2f %.2f %.2f m%s",
           frame, g_state.open, open_w, open_h, layer.alpha * 100.0f,
           screen.position[0], screen.position[1], screen.position[2],
           to_headset ? "" : " (no headset, so no layer)");
  } else if (first_flat && Say()) {
    ModLog("cut scene    : frame %llu: the eye pictures are on the screen, "
           "%.2f by %.2f m at %.2f %.2f %.2f m, turn %.3f %.3f %.3f %.3f%s",
           frame, screen.width, screen.height, screen.position[0],
           screen.position[1], screen.position[2], screen.orientation[0],
           screen.orientation[1], screen.orientation[2],
           screen.orientation[3],
           to_headset ? "" : " (no headset, so no layer)");
    if (crop > 0 && Say()) {
      const bw::tech::InfoTransform* info = Info();
      const int res_x = info != nullptr ? info->res_x : 0;
      ModLog("cut scene    : frame %llu: each eye picture loses %d of its %d "
             "columns, so the two pictures are %.1f mm apart on the screen, "
             "for eyes %.1f mm apart. The opening is the same %.2f m for "
             "both eyes", frame, crop, res_x,
             window_math::Shift(crop, g_set.width, res_x) * 1000.0f,
             ipd * 1000.0f, screen.width);
    }
  }
}

uint64_t CutsceneWindowEndFrame(uint64_t frame) {
  ReadSettings();
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  float dt = 0.0f;
  if (g_last.QuadPart != 0 && g_frequency.QuadPart != 0) {
    dt = static_cast<float>(now.QuadPart - g_last.QuadPart) /
         static_cast<float>(g_frequency.QuadPart);
  }
  g_last = now;
  if (frame != 0 && (frame == g_set.test_off || frame == g_set.test_on)) {
    g_set.on = frame == g_set.test_on;
    ModLog("cut scene    : frame %llu: a test sets cutscene_window=%d", frame,
           g_set.on ? 1 : 0);
  }
  if (frame != 0 &&
      (frame == g_set.depth_test_off || frame == g_set.depth_test_on)) {
    g_set.depth = frame == g_set.depth_test_on ? kDepthNearest : kDepthOff;
    g_depth = window_math::DepthState();
    g_said_plane = 0.0f;
    ModLog("cut scene    : frame %llu: a test sets cutscene_depth=%d", frame,
           g_set.depth);
  }
  // A long frame must not jump over the change. A cut scene often starts
  // with one: the game loads its script and its sounds then.
  const float step = dt > kLongestStep ? kLongestStep : dt;

  const int bars = ReadBars();
  const bool story = ScriptCameraStoryOwns();
  if (bars == 1) {
    g_since_bars = 0.0f;
  } else if (g_since_bars < 1000.0f) {
    g_since_bars += dt;
  }
  // The window needs the God view and a head on the camera. A flat frame
  // has no head on the camera, and it is still the window.
  const bool usable =
      g_set.on && g_set.god && (g_state.flat || HeadCameraOn());
  const bool wanted =
      usable && window_math::Wanted(bars == 1, g_wanted, story, g_since_bars,
                                    g_set.linger);

  if (g_set.god && (bars != g_said_bars || story != g_said_story)) {
    g_said_bars = bars;
    g_said_story = story;
    if (Say()) {
      ModLog("cut scene    : frame %llu: the cinema bars are %s, and %s",
             frame, bars == 1 ? "up" : bars == 0 ? "down" : "not known",
             story ? "a story camera owns the view"
                   : "the player's camera owns the view");
    }
  }
  // The screen takes its place when the walls start to close. A cut scene
  // that starts while the walls are in the view keeps the place that they
  // have.
  if (wanted && !g_wanted) {
    if (!g_state.flat && g_state.open >= 1.0f) {
      PlaceScreen(frame);
    } else if (Say()) {
      ModLog("cut scene    : frame %llu: the view was not open, so the screen "
             "keeps its place", frame);
    }
  }
  if (wanted != g_wanted && Say()) {
    if (wanted) {
      ModLog("cut scene    : frame %llu: a cut scene starts, so the view "
             "closes to the window", frame);
    } else {
      ModLog("cut scene    : frame %llu: the cut scene is over (%s)", frame,
             !usable       ? "the window is off"
             : story       ? "a story camera stayed too long with no bars"
                           : "the bars are down and the camera is back");
    }
  }
  g_wanted = wanted;

  const bool was_open = !g_state.flat && g_state.open >= 1.0f;
  if (window_math::Step(&g_state, wanted, step, g_set.seconds)) {
    if (g_state.flat) {
      EnterFlat(frame);
    } else {
      LeaveFlat(frame);
    }
  }
  if (!was_open && !g_state.flat && g_state.open >= 1.0f && Say()) {
    ModLog("cut scene    : frame %llu: the full view is open again",
           frame + 1);
  }

  for (uint64_t& at : g_capture_at) {
    if (at != 0 && at == frame + 1) {
      at = 0;
      return frame + 1;
    }
  }
  return 0;
}

void CutsceneWindowToggle() {
  ReadSettings();
  g_set.on = !g_set.on;
  ModLog("test key     : cutscene_window=%d. %s", g_set.on ? 1 : 0,
         g_set.on ? "A cut scene plays on a screen in the room"
                  : "A cut scene plays in the full view, with held shots");
}

void CutsceneWindowDepthToggle() {
  ReadSettings();
  g_set.depth = g_set.depth == kDepthOff ? kDepthNearest : kDepthOff;
  g_depth = window_math::DepthState();
  g_said_plane = 0.0f;
  ModLog("test key     : cutscene_depth=%d. %s", g_set.depth,
         g_set.depth == kDepthNearest
             ? "The nearest thing in view is on the screen or behind it"
             : "The stereo of the cut scene window has no depth limit, as "
               "before");
}

void CutsceneWindowReport() {
  if (g_flat_frames == 0 && g_change_frames == 0) return;
  ModLog("cut scene    : %llu frames in the window and %llu frames of a "
         "change this report. %llu cut scenes had a window this session.",
         g_flat_frames, g_change_frames, g_scenes);
  if (g_flat_frames != 0 && g_report_plane[1] > 0.0f) {
    ModLog("cut scene    : the screen plane was at %.1f to %.1f units this "
           "report (%.2f to %.2f units for each meter of depth). %llu frames "
           "had a measure of the nearest thing, and %llu shots started. The "
           "nearest thing was %.1f units away at the least and %.1f units in "
           "the last measure.", g_report_plane[0], g_report_plane[1],
           g_report_plane[0] / g_set.distance,
           g_report_plane[1] / g_set.distance, g_report_measures,
           g_report_shots, g_report_nearest[0], g_report_nearest[1]);
  }
  g_report_plane[0] = g_report_plane[1] = 0.0f;
  g_report_nearest[0] = g_report_nearest[1] = 0.0f;
  g_report_measures = 0;
  g_report_shots = 0;
  g_flat_frames = 0;
  g_change_frames = 0;
}
