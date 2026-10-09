// The screen interface on a surface of its own (docs/ui-layer.md).
//
// The menus, the subtitles and the help text draw into a panel target of the
// render size, in the first eye pass only. The panel reaches OpenXR like an
// eye: a shared texture, one copy, and a third swapchain. Two quad layers cut
// it into a menu panel ahead and a help panel lower down, fixed in the room.
// They hang from the recenter pose, so F11 and a recenter from the headset
// menu move them, and nothing else does.
//
// ui_panel in bwvr.ini:
//
//   0  the interface in the eye pictures
//   1  the interface on its own layers. The eye pictures lose it.
//   2  the test: the interface also drawn to the panel, which a capture
//      saves. Nothing changes in the eye pictures or in the headset.
//
// This file decides the route of each frame, saves the captures, and
// places the quads. draw_hooks.cpp picks the batches, and gpu_bridge.cpp
// switches the Direct3D 9 target.
//
// WHAT DOES NOT WORK YET. The pointer still points through the eye
// view. With ui_panel=1 a menu sits on its panel at the pixels its hit test
// reads, but the Hand shows where the pointer is in the world, not on the
// panel. So the player cannot see where a click on a menu lands.
//
// THE CREATURE PANEL (docs/ui-layer.md, "The Creature panel"). The game
// draws the Creature's stats in a box at the left of the screen, in the help
// scope. So it goes to the panel target, but the help quad shows only the
// bottom of the target. A third quad cuts the box out, and creature_panel
// puts it in the room: 1 fixed to the left, 2 near the Creature.
//
// THE SUBTITLE CARD (docs/ui-layer.md, "The subtitle card"). With ui_panel=0
// and a headset, the subtitles and the help text still leave the eye
// pictures. Flat text in two eye pictures needs a depth, and each depth is
// wrong for a part of the time: the text shows behind the land and the
// hands that it is drawn over, and the player sees it double. So the help
// text of the HelpText scope goes to the panel target in the first eye pass,
// and a copy of the part that drew, at half the size, goes to the headset as
// ONE quad layer. The quad is fixed in the room, ahead of the recenter pose
// and below eye level. It goes to a new place only when the head turns
// far away from it for a moment, and then it fades out and in. It never
// follows the head. If the runtime refuses the layer, the help text draws in
// the eye pictures at one fixed depth. With no headset, nothing changes.
//
// IN THE CUT SCENE WINDOW (docs/cutscene-window.md) the same card takes the
// help text, and cutscene_window.cpp gives its place: in the plane of the
// screen, under the scene. With no card there, the help text stays in the
// picture of the window.
//
//   subtitle_card=1               0: the subtitles stay in the eye pictures
//   subtitle_card_distance_cm     the distance from the head, 160
//   subtitle_card_down_deg        the angle below eye level, 17
//   subtitle_card_width_cm        the width of the whole render, 100
//   subtitle_fallback_cm          the fixed depth of the fallback, 75
//   subtitle_card_test=1          a test key: the route with no headset
//   subtitle_selftest=1           a test key: two test lines of help text

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../vr/xr_screen.h"
#include "MinHook.h"
#include "controller_math.h"
#include "cutscene_window.h"
#include "hint_card.h"
#include "subtitle_card_math.h"
#include "ui_panel.h"

extern void ModLog(const char* format, ...);

// stereo.cpp
bool StereoIdle();
bool StereoCaptureIntervalDue();
// head_pose.cpp
bool HeadPoseOrigin(float position_out[3], float rotation_out[4]);
uint64_t HeadPoseOriginSerial();
// eye_capture.cpp
void EyeCaptureSaveUi(const char* name, uint64_t frame, int style,
                      const void* pixels, int pitch, uint32_t width,
                      uint32_t height);
// frame_source.cpp
const void* FrameSourceGrab(int* pitch);
bool FrameSourceSize(uint32_t* width, uint32_t* height);
// The room and the head, for the Creature panel near the Creature.
bool HeadPoseCurrent(float matrix_out[9], float translation_out[3]);
bool HeadPoseHeadInRoom(float translation_out[3]);
float HeadPoseScale();
bool HeadCameraStageBasis(float basis_out[9]);
bool HeadCameraGameCamera(float position[3], float focus[3]);
bool TabletopRoom(float origin[3], float basis[9], float center[3],
                  float* gain);

namespace {

UiPanelSettings g_settings;
int g_mode = kUiPanelOff;  // what the test key has made of g_settings.mode
bool g_outside_capture = false;
uint64_t g_logged_serial = 0;
bool g_logged_quads = false;

// THE TWO RECTS (docs/ui-layer.md). Fixed values, so a panel never
// changes size while the player looks at it. 0 in the ini takes these.
//
// The Setup dialogs are drawn at a fixed size in pixels and near the middle
// of the render. The subtitle box and the help text sit in the lower part,
// and they scale with the render height.
void DefaultRect(int kind, uint32_t w, uint32_t h, int out[4]) {
  const int width = static_cast<int>(w);
  const int height = static_cast<int>(h);
  if (kind == kPanelMenu) {
    const int rw = width < 1280 ? width : 1280;
    const int rh = height < 960 ? height : 960;
    out[0] = (width - rw) / 2;
    out[1] = (height - rh) / 2;
    out[2] = rw;
    out[3] = rh;
    return;
  }
  out[0] = 0;
  out[1] = height * 3 / 5;
  out[2] = width;
  out[3] = height - out[1];
}

void Rect(int kind, uint32_t w, uint32_t h, int out[4]) {
  const int* set = kind == kPanelMenu ? g_settings.menu_rect
                                      : g_settings.help_rect;
  if (set[2] > 0 && set[3] > 0) {
    for (int i = 0; i < 4; ++i) out[i] = set[i];
    return;
  }
  DefaultRect(kind, w, h, out);
}

const char* ModeName(int mode) {
  if (mode == kUiPanelLayer) {
    return "the interface is on its own layers in the headset";
  }
  if (mode == kUiPanelTest) {
    return "the U1 test: the interface also draws to its own surface";
  }
  return "the interface is in the eye pictures";
}

// THE CREATURE PANEL. fn_005178D0, from the help text callback (0x005CD03C),
// draws the box with DrawCreatureStats (0x00517080) when the alpha at
// 0x00CC630C is above 0, and then clears the alpha and the layout. cdecl, no
// arguments, plain ret. Creature::Draw arms it with layout 1 while the Hand
// is over the player's Creature. CameraModeFollow::Update arms it with
// layout 0. The four values are a, b, c and d of DrawCreatureStats, at
// 0x00CC62EC down to 0x00CC62E0.
const uintptr_t kCreaturePanelDraw = 0x005178D0;
const uintptr_t kCreaturePanelAlpha = 0x00CC630C;
const uintptr_t kCreaturePanelLayout = 0x00CC6310;
const uintptr_t kCreaturePanelValues = 0x00CC62E0;
const uintptr_t kGame = 0x00D0195C;
const uintptr_t kMyInterface = 0x00555850;
const int kFaceNextObject = 0x400;  // GInterface: the object of the pick
typedef void(__cdecl* PanelDrawFn)();
typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);
PanelDrawFn o_CreaturePanelDraw = nullptr;
bool g_creature_hooked = false;

uint64_t g_frame = 0;           // the frame of UiPanelBeginFrame
uint64_t g_creature_frame = 0;  // the last frame the Creature panel drew
int32_t g_creature_layout = 1;
float g_creature_values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
// The Creature's head and the player's head center, in LOCAL meters, from
// the frame the panel drew.
bool g_creature_have_head = false;
float g_creature_head[3] = {0.0f, 0.0f, 0.0f};
float g_creature_eyes[3] = {0.0f, 0.0f, 0.0f};
// Near the Creature, the panel stays where it went until it is hidden for
// kCreatureHideSeconds.
const double kCreatureHideSeconds = 1.0;
bool g_creature_placed = false;
bool g_creature_near = false;
float g_creature_position[3] = {0.0f, 0.0f, 0.0f};
float g_creature_orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
float g_creature_distance = 1.0f;
double g_creature_seen = 0.0;
uint64_t g_creature_draws = 0;
uint64_t g_creature_shows = 0;
int g_creature_logs = 0;
const int kMaxCreatureLogs = 40;

double NowSeconds() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return static_cast<double>(count.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

// A pointer at base + offset, or null when it cannot be read.
const uint8_t* ReadPointer(const void* base, int offset) {
  if (base == nullptr) return nullptr;
  const uint8_t* const* at = reinterpret_cast<const uint8_t* const*>(
      static_cast<const uint8_t*>(base) + offset);
  if (IsBadReadPtr(at, sizeof(void*))) return nullptr;
  return *at;
}

// A world point and the head center in the LOCAL space of OpenXR, in
// meters. The inverse of the steps that put a controller in the world
// (controllers.cpp, ControllersOverlayHands), on the table and off it.
bool WorldToLocal(const float world[3], float local_out[3],
                  float eyes_out[3]) {
  float head[9], lean[3], room[9], game_position[3], focus[3], t_head[3];
  float origin[3], yaw[4];
  if (!HeadPoseCurrent(head, lean) || !HeadCameraStageBasis(room) ||
      !HeadCameraGameCamera(game_position, focus) ||
      !HeadPoseHeadInRoom(t_head) || !HeadPoseOrigin(origin, yaw)) {
    return false;
  }
  float table_origin[3], table_basis[9], table_center[3], gain = 1.0f;
  const bool table =
      TabletopRoom(table_origin, table_basis, table_center, &gain);
  if (table) {
    for (int i = 0; i < 9; ++i) room[i] = table_basis[i];
  } else {
    gain = 1.0f;
  }
  float lean_w[3], eye[3], t[3];
  controller_math::ScaleRoom(lean, gain, lean_w);
  controller_math::EyeFromLean(table ? table_origin : game_position, room,
                               lean_w, eye);
  controller_math::WorldToRoom(eye, room, t_head, gain, world, t);
  const float scale = HeadPoseScale();
  controller_math::RoomToLocal(origin, yaw, scale, t, local_out);
  controller_math::RoomToLocal(origin, yaw, scale, t_head, eyes_out);
  return true;
}

// The head of the Creature that armed the panel, in LOCAL meters. Layout 1
// only: Creature::Draw armed it for the object of the pick (the test of
// GetInteractObject, bit 0x10 of Object+0x24). The head is the base of the
// LH3DCreature (+0x78) raised by 15 * size (+0x90), the height of S.
bool CreatureHead() {
  if (g_creature_layout != 1) return false;
  void* game = *reinterpret_cast<void* const*>(kGame);
  if (game == nullptr) return false;
  void* face = reinterpret_cast<MyInterfaceFn>(kMyInterface)(game, nullptr);
  const uint8_t* object = ReadPointer(face, kFaceNextObject);
  if (object == nullptr || IsBadReadPtr(object, 0x168) ||
      (object[0x24] & 0x10) == 0) {
    return false;
  }
  const uint8_t* model = ReadPointer(ReadPointer(object, 0x160), 0x58);
  if (model == nullptr || IsBadReadPtr(model, 0x94)) return false;
  const float* base = reinterpret_cast<const float*>(model + 0x78);
  const float size = *reinterpret_cast<const float*>(model + 0x90);
  if (!(size > 0.0f && size < 1000.0f)) return false;
  const float head[3] = {base[0], base[1] + 15.0f * size, base[2]};
  return WorldToLocal(head, g_creature_head, g_creature_eyes);
}

void __cdecl h_CreaturePanelDraw() {
  const int32_t alpha = *reinterpret_cast<const int32_t*>(kCreaturePanelAlpha);
  // Once a frame: the first eye pass draws it and clears the alpha.
  if (alpha > 0 && g_creature_frame != g_frame) {
    g_creature_frame = g_frame;
    ++g_creature_draws;
    g_creature_layout =
        *reinterpret_cast<const int32_t*>(kCreaturePanelLayout);
    const float* v = reinterpret_cast<const float*>(kCreaturePanelValues);
    for (int i = 0; i < 4; ++i) g_creature_values[i] = v[3 - i];
    g_creature_have_head = CreatureHead();
  }
  o_CreaturePanelDraw();
}

// The box of DrawCreatureStats in render pixels, 8 px wider on each side:
// x from W/40, W/4 wide. Layout 1: y from H/4, H/3 high. Layout 0: y from
// H/40, H/4 high.
void CreatureRect(int32_t layout, uint32_t w, uint32_t h, int32_t out[4]) {
  const int32_t width = static_cast<int32_t>(w);
  const int32_t height = static_cast<int32_t>(h);
  const int32_t pad = 8;
  const int32_t x = width / 40;
  const int32_t y = layout != 0 ? height / 4 : height / 40;
  const int32_t box_h = layout != 0 ? height / 3 : height / 4;
  out[0] = x > pad ? x - pad : 0;
  out[1] = y > pad ? y - pad : 0;
  out[2] = width / 4 + 2 * pad;
  out[3] = box_h + 2 * pad;
}

// The Creature panel quad of this frame. Fixed: 0.9 m out, 35 degrees left
// of the recenter yaw and 15 degrees down, so a recenter moves it. Near the
// Creature: when it shows, 30 cm over the Creature's head and 15 cm to its
// left, 0.5 to 1.5 m from the eyes, facing them. It stays there while it
// shows, and until it is hidden for 1 s.
void CreatureQuad(uint32_t render_w, uint32_t render_h, const float anchor[3],
                  const float yaw[4], bwvr::PanelQuad* q) {
  q->visible = false;
  if (g_settings.creature_panel == kCreaturePanelOff) return;
  const double now = NowSeconds();
  if (g_creature_frame == 0 || g_creature_frame != g_frame) {
    if (g_creature_placed && now - g_creature_seen >= kCreatureHideSeconds) {
      g_creature_placed = false;
    }
    return;
  }
  g_creature_seen = now;
  const bool fresh = !g_creature_placed;
  if (g_settings.creature_panel == kCreaturePanelFixed || fresh) {
    g_creature_near = false;
    if (g_settings.creature_panel == kCreaturePanelNear &&
        g_creature_have_head &&
        controller_math::CreaturePanelPoint(g_creature_eyes, g_creature_head,
                                            0.30f, 0.15f, 0.5f, 1.5f,
                                            g_creature_position)) {
      controller_math::FacePose(g_creature_position, g_creature_eyes,
                                g_creature_orientation);
      g_creature_distance = controller_math::Distance(g_creature_position,
                                                      g_creature_eyes);
      g_creature_near = true;
    } else {
      const float left = 35.0f * 0.01745329f;
      const float turn[4] = {0.0f, sinf(left * 0.5f), 0.0f,
                             cosf(left * 0.5f)};
      float turned[4];
      controller_math::QuatMultiply(yaw, turn, turned);
      g_creature_distance = 0.9f;
      controller_math::PanelPose(anchor, turned, g_creature_distance,
                                 -15.0f * 0.01745329f, g_creature_position,
                                 g_creature_orientation);
    }
    g_creature_placed = true;
  }
  int32_t rect[4];
  CreatureRect(g_creature_layout, render_w, render_h, rect);
  const float density = g_settings.help_px_per_deg *
                        static_cast<float>(render_h) / 2880.0f;
  for (int i = 0; i < 4; ++i) q->rect[i] = rect[i];
  q->width = controller_math::PanelWidth(static_cast<float>(rect[2]), density,
                                         g_creature_distance);
  q->height = rect[2] > 0 ? q->width * rect[3] / rect[2] : 0.0f;
  for (int i = 0; i < 3; ++i) q->position[i] = g_creature_position[i];
  for (int i = 0; i < 4; ++i) q->orientation[i] = g_creature_orientation[i];
  q->visible = q->width > 0.0f;
  if (fresh && q->visible) {
    ++g_creature_shows;
    if (g_creature_logs < kMaxCreatureLogs) {
      ++g_creature_logs;
      ModLog("ui panel     : the Creature panel shows (layout %d, values "
             "%.2f %.2f %.2f %.2f), %s at %.2f %.2f %.2f m, %.2f m from the "
             "eyes, %.2f by %.2f m, rect %d %d %d %d", g_creature_layout,
             g_creature_values[0], g_creature_values[1], g_creature_values[2],
             g_creature_values[3],
             g_creature_near ? "near the Creature" : "fixed to the left",
             q->position[0], q->position[1], q->position[2],
             g_creature_distance, q->width, q->height, rect[0], rect[1],
             rect[2], rect[3]);
    }
  }
}

// --- the subtitle card ---------------------------------------------------------

struct SubtitleSettings {
  bool on = true;         // subtitle_card
  int distance_cm = 160;  // subtitle_card_distance_cm
  int down_deg = 17;      // subtitle_card_down_deg
  int width_cm = 100;     // subtitle_card_width_cm
  int fallback_cm = 75;   // subtitle_fallback_cm
  bool test = false;      // subtitle_card_test: the route with no headset
  bool selftest = false;  // subtitle_selftest
};

// What the help text does in a frame.
enum SubtitleMode {
  kSubtitlesOld,       // in the eye pictures, at the interface depth
  kSubtitlesCard,      // on the card
  kSubtitlesFallback,  // in the eye pictures, at a fixed depth
};

struct SubtitleState {
  bool read = false;
  SubtitleSettings set;
  bool toggled_off = false;  // the test key
  bool capture = false;
  int mode = kSubtitlesOld;  // of this frame
  int logged_mode = -1;
  // The card of this frame hangs under the cut scene window.
  bool window = false;
  int logged_window = -1;
  // The head of this frame in the LOCAL space.
  bool have_head = false;
  float head[3] = {0.0f, 0.0f, 0.0f};
  float head_yaw = 0.0f;
  subtitle_math::Anchor anchor;
  subtitle_math::Shown shown;
  bool showing = false;      // the card was in the view on the last frame
  bool window_showing = false;  // the same for the card under the window
  // The picture that goes to the headset: half the render size, BGRA.
  uint8_t* pixels = nullptr;
  uint32_t texture_w = 0;
  uint32_t texture_h = 0;
  int read_failures = 0;     // frames in a row with no copy of the panel
  bool read_failed = false;
  double last_time = 0.0;
  // For the log.
  int logs = 0;
  uint64_t frames_card = 0;
  uint64_t frames_shown = 0;
  uint64_t frames_fallback = 0;
  uint64_t frames_lost = 0;
  uint64_t moves = 0;
  double read_ms = 0.0;
  double worst_ms = 0.0;
  uint64_t bytes = 0;
};
SubtitleState g_sub;
const int kMaxSubtitleLogs = 80;
// The card fades out and in when it goes to a new place.
const float kSubtitleFadeSeconds = 0.2f;
// Pixels of the render around what drew, and the grid of the rect.
const float kSubtitlePad = 8.0f;
const int kSubtitleGrid = 16;

int SubtitleIniKey(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return fallback;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return fallback;
  slash[1] = 0;
  if (strlen(path) + 9 >= MAX_PATH) return fallback;
  strcat_s(path, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

int Clamp(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

void SubtitleReadSettings() {
  if (g_sub.read) return;
  g_sub.read = true;
  SubtitleSettings& s = g_sub.set;
  s.on = SubtitleIniKey("subtitle_card", 1) != 0;
  s.distance_cm = Clamp(SubtitleIniKey("subtitle_card_distance_cm", 160), 60,
                        400);
  s.down_deg = Clamp(SubtitleIniKey("subtitle_card_down_deg", 17), -20, 60);
  s.width_cm = Clamp(SubtitleIniKey("subtitle_card_width_cm", 100), 30, 300);
  s.fallback_cm = Clamp(SubtitleIniKey("subtitle_fallback_cm", 75), 30, 400);
  s.test = SubtitleIniKey("subtitle_card_test", 0) != 0;
  s.selftest = SubtitleIniKey("subtitle_selftest", 0) != 0;
}

subtitle_math::Layout SubtitleLayout() {
  subtitle_math::Layout layout;
  layout.distance = g_sub.set.distance_cm * 0.01f;
  layout.down = g_sub.set.down_deg * subtitle_math::kDegrees;
  layout.width = g_sub.set.width_cm * 0.01f;
  return layout;
}

// The route of the help text for this frame, latched before the eye passes.
void SubtitleBeginFrame(bool to_headset, bool projection, uint64_t frame,
                        bool capture) {
  SubtitleState& s = g_sub;
  SubtitleReadSettings();
  s.capture = capture;
  s.window = false;
  if (!s.set.on) {
    // subtitle_card=0: nothing of the card runs, and the subtitles stay in
    // the eye pictures.
    s.mode = kSubtitlesOld;
    return;
  }
  // A flat frame of the cut scene window has no projection layer. Its card
  // hangs under the scene. With no card there, the help text stays in the
  // picture of the window, and not at the fixed depth of the fallback.
  const bool flat = CutsceneWindowFlat();
  const bool window = CutsceneWindowSubtitles();
  const int no_card = flat ? kSubtitlesOld : kSubtitlesFallback;
  const bool wanted = !s.toggled_off;
  const char* why = "";
  int mode = kSubtitlesOld;
  if (s.toggled_off) {
    why = "the test key";
  } else if (flat && !window) {
    why = "cutscene_subtitles is 0, so the cut scene window has its "
          "subtitles in its picture";
  } else if (!to_headset) {
    // The mouse player with no headset keeps the old subtitles.
    if (s.set.test && GpuBridgePanelUsable() && !s.read_failed) {
      mode = kSubtitlesCard;
    } else {
      why = "no headset";
    }
  } else if (!projection && !window) {
    why = "the stereoscopic window has its interface in its picture";
  } else if (!GpuBridgePanelUsable()) {
    mode = no_card;
    why = "there is no target for the help text";
  } else if (bwvr::XrScreen::CardFailed(bwvr::kCardSubtitles)) {
    mode = no_card;
    why = "the runtime refused the layer of the card";
  } else if (s.read_failed) {
    mode = no_card;
    why = "the target or the copy of the help text failed 3 times";
  } else {
    mode = kSubtitlesCard;
  }
  s.mode = mode;
  s.window = window && mode == kSubtitlesCard;
  const int window_now = s.window ? 1 : 0;
  if (window_now != s.logged_window && (s.window || s.logged_window >= 0)) {
    s.logged_window = window_now;
    ModLog("subtitles    : frame %llu, %s", frame,
           s.window ? "the subtitle card hangs under the scene of the cut "
                      "scene window, and no subtitle draws in its picture"
                    : "the cut scene window is gone, so the subtitle card "
                      "has its place in the room again");
  }
  UiDepthSetHelpDepth(mode == kSubtitlesFallback ? s.set.fallback_cm * 0.01f
                                                 : 0.0f);
  if (mode == kSubtitlesFallback) ++s.frames_fallback;
  if (mode != s.logged_mode && (wanted || s.logged_mode >= 0)) {
    s.logged_mode = mode;
    if (mode == kSubtitlesCard) {
      ModLog("subtitles    : the subtitles and the help text now go to the "
             "subtitle card, and none draws in an eye picture, frame %llu",
             frame);
    } else if (mode == kSubtitlesFallback) {
      ModLog("subtitles    : no subtitle card, because %s. The subtitles and "
             "the help text draw in the eye pictures at a fixed depth of "
             "%.2f m (subtitle_fallback_cm), frame %llu", why,
             s.set.fallback_cm * 0.01f, frame);
    } else {
      ModLog("subtitles    : no subtitle card (%s). The subtitles draw in the "
             "eye pictures as before, frame %llu", why, frame);
    }
  }
  GpuBridgePanelNewFrame(false);
  UiPanelRouteFrame(kUiPanelSubtitles, mode == kSubtitlesCard, false);
}

// The half size copy of `rect` of the panel target, into s.pixels, with the
// color and the alpha multiplied by `fade`. False when the copy failed.
bool SubtitleCopy(const int rect[4], uint32_t render_w, uint32_t render_h,
                  float fade, uint32_t* w_out, uint32_t* h_out,
                  double* ms_out) {
  SubtitleState& s = g_sub;
  LARGE_INTEGER t0, t1, frequency;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&t0);
  int pitch = 0;
  uint32_t w = 0, h = 0;
  const uint8_t* source = static_cast<const uint8_t*>(
      GpuBridgePanelReadHalf(rect, &pitch, &w, &h));
  const uint32_t texture_w = render_w / 2, texture_h = render_h / 2;
  if (source == nullptr || w == 0 || h == 0 || w > texture_w ||
      h > texture_h) {
    GpuBridgePanelReadDone();
    ++s.frames_lost;
    s.showing = false;
    s.window_showing = false;
    if (++s.read_failures >= 3 && !s.read_failed) {
      s.read_failed = true;
      ModLog("subtitles    : the copy of the help text failed 3 times, so the "
             "subtitles go back to the eye pictures, frame %llu", g_frame);
    }
    return false;
  }
  s.read_failures = 0;
  if (s.pixels == nullptr || s.texture_w != texture_w ||
      s.texture_h != texture_h) {
    delete[] s.pixels;
    s.pixels = new uint8_t[static_cast<size_t>(texture_w) * texture_h * 4];
    s.texture_w = texture_w;
    s.texture_h = texture_h;
  }
  const int scale = static_cast<int>(fade * 256.0f + 0.5f);
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* in = source + static_cast<size_t>(y) * pitch;
    uint8_t* out = s.pixels + static_cast<size_t>(y) * texture_w * 4;
    if (scale >= 256) {
      memcpy(out, in, static_cast<size_t>(w) * 4);
    } else {
      for (uint32_t i = 0; i < w * 4; ++i) {
        out[i] = static_cast<uint8_t>((in[i] * scale) >> 8);
      }
    }
  }
  GpuBridgePanelReadDone();
  QueryPerformanceCounter(&t1);
  const double ms = 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) /
                    static_cast<double>(frequency.QuadPart);
  s.read_ms += ms;
  if (ms > s.worst_ms) s.worst_ms = ms;
  s.bytes += static_cast<uint64_t>(w) * h * 4;
  *w_out = w;
  *h_out = h;
  *ms_out = ms;
  return true;
}

// The card of a flat frame of the cut scene window: the part of the render
// that drew, in the plane of the screen, under the scene.
void SubtitleWindowCard(const int rect[4], uint32_t render_w,
                        uint32_t render_h) {
  SubtitleState& s = g_sub;
  bwvr::CardLayer layer = {};
  if (!CutsceneWindowSubtitleQuad(render_w, render_h, rect, layer.position,
                                  layer.orientation, &layer.width,
                                  &layer.height)) {
    ++s.frames_lost;
    s.window_showing = false;
    return;
  }
  uint32_t w = 0, h = 0;
  double ms = 0.0;
  if (!SubtitleCopy(rect, render_w, render_h, 1.0f, &w, &h, &ms)) return;
  layer.visible = true;
  layer.pixels = s.pixels;
  layer.texture_width = s.texture_w;
  layer.texture_height = s.texture_h;
  layer.rect[0] = 0;
  layer.rect[1] = 0;
  layer.rect[2] = static_cast<int32_t>(w);
  layer.rect[3] = static_cast<int32_t>(h);
  bwvr::XrScreen::StageCard(bwvr::kCardSubtitles, layer);
  ++s.frames_shown;
  const bool fresh = !s.window_showing;
  s.window_showing = true;
  if ((fresh && s.logs < kMaxSubtitleLogs) || s.capture) {
    if (!s.capture) ++s.logs;
    ModLog("subtitles    : frame %llu, the subtitle card under the cut scene "
           "window: the rect %d %d %d %d of the %u by %u render, %u by %u px "
           "on the card, at %.3f %.3f %.3f m, turn %.4f %.4f %.4f %.4f, %.2f "
           "by %.2f m. The copy took %.2f ms", g_frame, rect[0], rect[1],
           rect[2], rect[3], render_w, render_h, w, h, layer.position[0],
           layer.position[1], layer.position[2], layer.orientation[0],
           layer.orientation[1], layer.orientation[2], layer.orientation[3],
           layer.width, layer.height, ms);
  }
  if (s.capture) {
    const int out_pitch = static_cast<int>(s.texture_w) * 4;
    EyeCaptureSaveUi("subtitle-card", g_frame, 0, s.pixels, out_pitch, w, h);
    EyeCaptureSaveUi("subtitle-card-over", g_frame, 1, s.pixels, out_pitch, w,
                     h);
  }
}

// After both eye passes: the help text of this frame goes to the headset as
// one quad layer.
void SubtitleEndPasses() {
  SubtitleState& s = g_sub;
  const double now = NowSeconds();
  float dt = static_cast<float>(now - s.last_time);
  if (s.last_time == 0.0 || dt > 0.1f) dt = 0.1f;
  s.last_time = now;
  if (s.mode != kSubtitlesCard) {
    s.showing = false;
    s.window_showing = false;
    s.shown.fade = 0.0f;
    return;
  }
  ++s.frames_card;
  if (UiPanelFrameFailed()) {
    // The target of the help text could not be bound, so the help text of
    // this frame drew in the eye pictures.
    ++s.frames_lost;
    s.showing = false;
    if (++s.read_failures >= 3 && !s.read_failed) {
      s.read_failed = true;
      ModLog("subtitles    : the target of the help text failed 3 times, so "
             "the subtitles go back to the eye pictures, frame %llu", g_frame);
    }
    return;
  }

  // What drew, as a rect of the render.
  float bounds[4];
  uint32_t render_w = 0, render_h = 0;
  int rect[4] = {0, 0, 0, 0};
  bool content = UiPanelDrew(kPanelHelp) && !UiPanelFrameFailed() &&
                 UiPanelHelpBounds(bounds, &render_w, &render_h) &&
                 subtitle_math::CardRect(bounds, kSubtitlePad, kSubtitleGrid,
                                         static_cast<int>(render_w),
                                         static_cast<int>(render_h), rect);

  // IN THE CUT SCENE WINDOW the card has the place of the screen. It has no
  // anchor of its own, and it does not fade.
  if (s.window) {
    s.showing = false;
    s.shown.fade = 0.0f;
    if (content) {
      SubtitleWindowCard(rect, render_w, render_h);
    } else {
      s.window_showing = false;
    }
    return;
  }
  s.window_showing = false;

  // THE PLACE. The anchor is the recenter pose, until the head is far away
  // from it for a moment.
  float origin[3] = {0.0f, 0.0f, 0.0f}, origin_turn[4] = {0.0f, 0.0f, 0.0f,
                                                          1.0f};
  const bool have_origin = HeadPoseOrigin(origin, origin_turn);
  if (!s.have_head && !have_origin) {
    if (content) ++s.frames_lost;
    s.showing = false;
    return;
  }
  const float origin_yaw = subtitle_math::HeadYaw(origin_turn);
  const float* head = s.have_head ? s.head : origin;
  const float head_yaw = s.have_head ? s.head_yaw : origin_yaw;
  // The rule runs only while there is a card to place. A card that comes
  // into the view takes the place of that moment.
  const subtitle_math::AnchorRule rule;
  const int event =
      content || s.showing
          ? subtitle_math::AnchorStep(&s.anchor, have_origin, origin,
                                      origin_yaw, HeadPoseOriginSerial(), head,
                                      head_yaw, s.showing, now, rule)
          : subtitle_math::kAnchorKept;
  if (event != subtitle_math::kAnchorKept) {
    if (event == subtitle_math::kAnchorMoved) ++s.moves;
    if (s.logs < kMaxSubtitleLogs) {
      ++s.logs;
      ModLog("subtitles    : frame %llu, the subtitle card %s: ahead of %.2f "
             "%.2f %.2f m, at a yaw of %.0f degrees%s", g_frame,
             event == subtitle_math::kAnchorAtRecenter
                 ? "hangs from the recenter pose"
                 : "goes to a new place, because the head was far away from "
                   "the old one",
             s.anchor.head[0], s.anchor.head[1], s.anchor.head[2],
             s.anchor.yaw / subtitle_math::kDegrees,
             s.showing ? ". It fades out and in" : "");
    }
  }
  subtitle_math::ShownStep(&s.shown, s.anchor, content, dt,
                           kSubtitleFadeSeconds);
  if (!content) {
    s.showing = false;
    return;
  }

  // THE PICTURE: the part that drew, at half the size.
  uint32_t w = 0, h = 0;
  double ms = 0.0;
  if (!SubtitleCopy(rect, render_w, render_h, s.shown.fade, &w, &h, &ms)) {
    return;
  }
  const uint32_t texture_w = s.texture_w, texture_h = s.texture_h;

  // THE QUAD: the rect on the screen that hangs from the anchor.
  const float rect_f[4] = {static_cast<float>(rect[0]),
                           static_cast<float>(rect[1]),
                           static_cast<float>(rect[2]),
                           static_cast<float>(rect[3])};
  subtitle_math::Quad quad;
  subtitle_math::RectQuad(s.shown.head, s.shown.yaw, SubtitleLayout(),
                          static_cast<float>(render_w),
                          static_cast<float>(render_h), rect_f, &quad);
  bwvr::CardLayer layer = {};
  layer.visible = true;
  layer.pixels = s.pixels;
  layer.texture_width = texture_w;
  layer.texture_height = texture_h;
  layer.rect[0] = 0;
  layer.rect[1] = 0;
  layer.rect[2] = static_cast<int32_t>(w);
  layer.rect[3] = static_cast<int32_t>(h);
  for (int i = 0; i < 3; ++i) layer.position[i] = quad.position[i];
  for (int i = 0; i < 4; ++i) layer.orientation[i] = quad.orientation[i];
  layer.width = quad.width;
  layer.height = quad.height;
  if (s.shown.fade > 0.0f) {
    bwvr::XrScreen::StageCard(bwvr::kCardSubtitles, layer);
    ++s.frames_shown;
  }
  const bool fresh = !s.showing;
  s.showing = true;

  if ((fresh && s.logs < kMaxSubtitleLogs) || s.capture) {
    if (!s.capture) ++s.logs;
    const float away = controller_math::Distance(quad.position, head);
    const float below =
        away > 0.0f ? asinf((head[1] - quad.position[1]) / away) /
                          subtitle_math::kDegrees
                    : 0.0f;
    ModLog("subtitles    : frame %llu, the subtitle card: the rect %d %d %d %d "
           "of the %u by %u render, %u by %u px on the card, at %.3f %.3f "
           "%.3f m, turn %.4f %.4f %.4f %.4f, %.2f by %.2f m, %.2f m from "
           "the head and %.0f degrees below eye level. The head has a yaw of "
           "%.0f degrees and the card of %.0f. Fade %.2f, the copy took %.2f "
           "ms", g_frame, rect[0], rect[1], rect[2], rect[3], render_w,
           render_h, w, h, quad.position[0], quad.position[1],
           quad.position[2], quad.orientation[0], quad.orientation[1],
           quad.orientation[2], quad.orientation[3], quad.width, quad.height,
           away, below, head_yaw / subtitle_math::kDegrees,
           s.shown.yaw / subtitle_math::kDegrees, s.shown.fade, ms);
  }
  if (s.capture) {
    const int out_pitch = static_cast<int>(texture_w) * 4;
    EyeCaptureSaveUi("subtitle-card", g_frame, 0, s.pixels, out_pitch, w, h);
    EyeCaptureSaveUi("subtitle-card-over", g_frame, 1, s.pixels, out_pitch, w,
                     h);
  }
}

void SubtitleReport() {
  SubtitleState& s = g_sub;
  const uint64_t fixed = UiDepthTakeHelpFixed();
  if (s.frames_card == 0 && s.frames_fallback == 0 && fixed == 0) return;
  ModLog("subtitles    : %llu frames with the help text on the subtitle "
         "card, and the card showed on %llu. The card went to a new place "
         "%llu times. The copy took %.2f ms a frame that showed (the worst "
         "%.2f ms), %.0f KiB a frame. %llu frames had help text and no card",
         s.frames_card, s.frames_shown, s.moves,
         s.frames_shown ? s.read_ms / static_cast<double>(s.frames_shown) : 0.0,
         s.worst_ms,
         s.frames_shown
             ? static_cast<double>(s.bytes) / 1024.0 /
                   static_cast<double>(s.frames_shown)
             : 0.0,
         s.frames_lost);
  if (s.frames_fallback != 0 || fixed != 0) {
    ModLog("subtitles    : %llu frames of the fallback: %llu batches of help "
           "text drew in the eye pictures at the fixed depth of %.2f m",
           s.frames_fallback, fixed, s.set.fallback_cm * 0.01f);
  }
  s.frames_card = s.frames_shown = s.frames_fallback = s.frames_lost = 0;
  s.moves = 0;
  s.read_ms = s.worst_ms = 0.0;
  s.bytes = 0;
}

// Saves the panel, and in the check frame the two check pictures.
void SaveCaptures(uint64_t frame) {
  if (!UiPanelDrew(kPanelMenu) && !UiPanelDrew(kPanelHelp)) {
    ModLog("ui panel     : frame %llu drew nothing to the panel", frame);
    return;
  }
  int pitch = 0;
  uint32_t w = 0, h = 0;
  const void* pixels = GpuBridgePanelRead(0, &pitch, &w, &h);
  if (pixels != nullptr) {
    EyeCaptureSaveUi("ui", frame, 0, pixels, pitch, w, h);
    EyeCaptureSaveUi("ui-over", frame, 1, pixels, pitch, w, h);
  }
  GpuBridgePanelReadDone();
  const char* const names[2] = {"ui-check", "ui-spare"};
  for (int i = 0; i < 2; ++i) {
    pixels = GpuBridgePanelRead(1 + i, &pitch, &w, &h);
    if (pixels != nullptr) {
      EyeCaptureSaveUi(names[i], frame, 2, pixels, pitch, w, h);
    }
    GpuBridgePanelReadDone();
  }
}

}  // namespace

void UiPanelSetOptions(const UiPanelSettings& settings) {
  g_settings = settings;
  if (g_settings.mode < kUiPanelOff || g_settings.mode > kUiPanelTest) {
    g_settings.mode = kUiPanelOff;
  }
  g_mode = g_settings.mode;
  SubtitleReadSettings();
  if (g_settings.mode == kUiPanelOff) {
    // The subtitle card needs the panel target, and the hook that puts the
    // game's target back after the subtitle box. It needs no share: its
    // picture goes to the headset through the CPU, as the hint cards do.
    const SubtitleSettings& sub = g_sub.set;
    if (sub.on) {
      GpuBridgePanelWant(true, false);
      UiPanelInstallHooks();
    }
    UiDepthSetSubtitleSelfTest(sub.selftest);
    ModLog("subtitles    : subtitle_card=%d, %s. The card is %d cm from the "
           "head, %d degrees below eye level, and the width of the render is "
           "%d cm on it. The fallback depth is %d cm%s",
           sub.on ? 1 : 0,
           sub.on ? "in the headset the subtitles and the help text show on "
                    "one card that is fixed in the room"
                  : "the subtitles stay in the eye pictures",
           sub.distance_cm, sub.down_deg, sub.width_cm, sub.fallback_cm,
           sub.test ? ". subtitle_card_test=1: the route runs with no headset"
                    : "");
    return;
  }
  GpuBridgePanelWant(true, g_settings.mode == kUiPanelLayer);
  UiPanelInstallHooks();
  // The Creature panel: the hook only latches what the game draws.
  if (!g_creature_hooked) {
    void* target = reinterpret_cast<void*>(kCreaturePanelDraw);
    MH_STATUS status =
        MH_CreateHook(target, reinterpret_cast<void*>(&h_CreaturePanelDraw),
                      reinterpret_cast<void**>(&o_CreaturePanelDraw));
    if (status == MH_OK) status = MH_EnableHook(target);
    g_creature_hooked = status == MH_OK;
    ModLog("hooks        : the Creature panel draw at 0x%08X %s",
           static_cast<unsigned>(kCreaturePanelDraw),
           g_creature_hooked ? "ok" : "FAILED");
  }
  ModLog("ui panel     : ui_panel=%d, %s", g_settings.mode,
         ModeName(g_settings.mode));
  ModLog("ui panel     : the menu panel %d cm ahead at %d degrees, %d px a "
         "degree. The help panel %d cm ahead at %d degrees, %d px a degree "
         "at a 2880 px render.", g_settings.menu_distance_cm,
         g_settings.menu_pitch_deg, g_settings.menu_px_per_deg,
         g_settings.help_distance_cm, g_settings.help_pitch_deg,
         g_settings.help_px_per_deg);
}

const UiPanelSettings& UiPanelOptions() { return g_settings; }

int UiPanelMode() { return g_mode; }

// Ctrl Shift F11 with test_keys=1: the interface goes between its own layer
// and the eye pictures, for a live A/B. It takes effect at the next frame.
void UiPanelToggle() {
  if (g_settings.mode == kUiPanelOff) {
    // With no interface layer, the key switches the cards: the hint cards
    // beside the wrists and the subtitle card, or the hints and the subtitles
    // in the eye pictures as before.
    HintCardToggle();
    if (g_sub.set.on) {
      g_sub.toggled_off = !g_sub.toggled_off;
      ModLog("test key     : %s",
             g_sub.toggled_off
                 ? "the subtitles draw in the eye pictures, as before"
                 : "the subtitles show on the card in the room");
    }
    return;
  }
  g_mode = (g_mode == kUiPanelOff) ? g_settings.mode : kUiPanelOff;
  ModLog("test key     : %s", ModeName(g_mode));
}

void UiPanelBeginFrame(bool to_headset, bool projection, uint64_t frame,
                       bool capture) {
  g_frame = frame;
  // The hint card needs no panel, so it gets the frame with ui_panel=0 too.
  HintCardBeginFrame(to_headset, projection, frame, capture);
  if (g_settings.mode == kUiPanelOff) {
    // And the subtitle card takes the help text of the frame.
    SubtitleBeginFrame(to_headset, projection, frame, capture);
    return;
  }
  bool on = false;
  if (g_mode == kUiPanelTest) {
    on = GpuBridgePanelUsable();
  } else if (g_mode == kUiPanelLayer) {
    // Rule 8 of docs/ui-layer.md: in the headset only with the shared
    // panel and the projection layer. The stereoscopic window (play_mode 1)
    // has no projection layer, so its interface stays in its picture. With
    // no headset this is the desktop test.
    on = GpuBridgePanelUsable() &&
         (!to_headset || (GpuBridgePanelShared() && projection));
  }
  GpuBridgePanelNewFrame(on && capture);
  UiPanelRouteFrame(g_mode, on, capture);
}

void UiPanelAfterFirstEye(uint64_t frame, bool capture) {
  if (g_settings.mode == kUiPanelOff || !capture) return;
  SaveCaptures(frame);
}

// FinishFrame outside the eye passes: the front end, the loading screens,
// and a frame the runtime did not want drawn. Only the test (ui_panel=2) draws a panel
// there. A panel for screens with no scene is not built.
void UiPanelBeforeFinish(bool in_stereo_pass) {
  if (g_settings.mode == kUiPanelOff || in_stereo_pass) return;
  const bool on = g_mode == kUiPanelTest && GpuBridgePanelUsable();
  // The capture timer of the eye passes serves here while no scene draws.
  g_outside_capture = on && StereoIdle() && StereoCaptureIntervalDue();
  GpuBridgePanelNewFrame(g_outside_capture);
  UiPanelRouteOutside(g_mode, on, g_outside_capture);
}

void UiPanelAfterFinish(bool in_stereo_pass, uint64_t frame) {
  if (g_settings.mode == kUiPanelOff) {
    // The subtitle card: the scope hooks put the game's target back, so this
    // only counts a miss.
    if (g_sub.set.on) UiPanelRelease();
    return;
  }
  UiPanelRelease();
  if (in_stereo_pass) return;
  if (g_outside_capture) {
    g_outside_capture = false;
    SaveCaptures(frame);
    int pitch = 0;
    const void* pixels = FrameSourceGrab(&pitch);
    uint32_t w = 0, h = 0;
    if (pixels != nullptr && FrameSourceSize(&w, &h)) {
      EyeCaptureSaveUi("ui-frame", frame, 2, pixels, pitch, w, h);
    }
  }
  // With no scene the stereo report does not run, so the test reports here.
  if (g_mode == kUiPanelTest && StereoIdle() && (frame % 900) == 0) {
    UiPanelReport();
  }
}

// The quads of this frame (docs/ui-layer.md). Each hangs from the
// recenter pose and faces the head position of that pose. A quad shows only
// in a frame that drew its kind.
bool UiPanelQuads(uint32_t render_w, uint32_t render_h,
                  bwvr::PanelQuad out[2]) {
  out[0].visible = out[1].visible = false;
  if (g_mode != kUiPanelLayer || UiPanelFrameFailed()) return false;
  if (render_w == 0 || render_h == 0) return false;
  float anchor[3], yaw[4];
  if (!HeadPoseOrigin(anchor, yaw)) return false;

  bool any = false;
  for (int kind = 0; kind < 2; ++kind) {
    bwvr::PanelQuad& q = out[kind];
    const bool menu = kind == kPanelMenu;
    const float distance =
        (menu ? g_settings.menu_distance_cm : g_settings.help_distance_cm) /
        100.0f;
    const float pitch =
        (menu ? g_settings.menu_pitch_deg : g_settings.help_pitch_deg) *
        0.01745329f;
    // The menus draw at a fixed size in pixels. The help layer scales with
    // the render height, so its density does too, and its letters keep
    // their angle at any render size.
    float density = static_cast<float>(menu ? g_settings.menu_px_per_deg
                                            : g_settings.help_px_per_deg);
    if (!menu) density *= static_cast<float>(render_h) / 2880.0f;
    int rect[4];
    Rect(kind, render_w, render_h, rect);
    q.rect[0] = rect[0];
    q.rect[1] = rect[1];
    q.rect[2] = rect[2];
    q.rect[3] = rect[3];
    q.width = controller_math::PanelWidth(static_cast<float>(rect[2]), density,
                                           distance);
    q.height = rect[2] > 0 ? q.width * rect[3] / rect[2] : 0.0f;
    controller_math::PanelPose(anchor, yaw, distance, pitch, q.position,
                               q.orientation);
    q.visible = UiPanelDrew(kind) && q.width > 0.0f;
    if (q.visible) any = true;
  }
  // The third quad, the Creature panel. It shows only in a frame that drew
  // it.
  bwvr::PanelQuad creature = {};
  CreatureQuad(render_w, render_h, anchor, yaw, &creature);
  bwvr::XrScreen::StagePanelQuad(creature);
  if (creature.visible) any = true;

  // Once, and again after each recenter.
  const uint64_t serial = HeadPoseOriginSerial();
  if (any && (!g_logged_quads || serial != g_logged_serial)) {
    g_logged_quads = true;
    g_logged_serial = serial;
    for (int kind = 0; kind < 2; ++kind) {
      const bwvr::PanelQuad& q = out[kind];
      ModLog("ui panel     : the %s panel at %.2f %.2f %.2f m, %.2f by %.2f m, "
             "rect %d %d %d %d", kind == kPanelMenu ? "menu" : "help",
             q.position[0], q.position[1], q.position[2], q.width, q.height,
             q.rect[0], q.rect[1], q.rect[2], q.rect[3]);
    }
  }
  return any;
}

void UiPanelEndPasses() {
  if (g_settings.mode != kUiPanelOff) return;
  SubtitleEndPasses();
}

void UiPanelHead(const float center[3], const float orientation[4]) {
  for (int i = 0; i < 3; ++i) g_sub.head[i] = center[i];
  g_sub.head_yaw = subtitle_math::HeadYaw(orientation);
  g_sub.have_head = true;
}

void UiPanelReport() {
  HintCardReport();
  if (g_settings.mode == kUiPanelOff) {
    if (g_sub.set.on) {
      SubtitleReport();
      UiPanelReportRoutes();
      GpuBridgePanelReport();
    }
    return;
  }
  UiPanelReportRoutes();
  GpuBridgePanelReport();
  if (g_creature_draws != 0) {
    ModLog("ui panel     : the Creature panel drew on %llu frames and went to "
           "the room %llu times this window (creature_panel=%d)",
           g_creature_draws, g_creature_shows, g_settings.creature_panel);
    g_creature_draws = 0;
    g_creature_shows = 0;
  }
}
