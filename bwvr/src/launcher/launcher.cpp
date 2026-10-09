// The launcher for Black & White VR.
//
// It edits two things, because the settings live in two places and neither is
// obvious: the mod's own options in bwvr.ini beside the game, and the render
// resolution in the registry, where the game keeps it. Then it starts the
// game.
//
// WHY IT IS BUILT FROM A TABLE. The project keeps adding settings. A hand
// laid out dialog would mean an edit in three places for each new one: the
// layout, the reader and the writer.
// Here a setting is one row in kSettings and nothing else. The window sizes
// itself around whatever the table holds, and starts a new column when one gets
// too tall, so the table can grow without the dialog becoming unusable.
//
// It has no dependencies beyond Windows itself, so it is one file to ship and
// nothing for anyone to install.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/exe_check.h"
#include "../common/version.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

const char* kWindowClass = "BwvrLauncher";
const char* kProductName = BWVR_PRODUCT_NAME;
const char* kTitle = BWVR_PRODUCT_NAME " " BWVR_VERSION_TEXT;
const char* kRegistryPath =
    "SOFTWARE\\Lionhead Studios Ltd\\Black & White\\BWSetup";

enum Kind {
  kGroup,       // a heading, not a setting
  kBool,        // a checkbox
  kInt,         // a number
  kChoice,      // one of a list, stored as the index
  kValues,      // one of a "label=value" list, stored as the value
  kScale,       // a slider, as a percentage of what the headset asks for
};

struct Setting {
  Kind kind;
  const char* key;    // the ini key, or null for a heading
  const char* label;
  const char* choices;  // "one|two|three" for kChoice
  int fallback;
  const char* help;
};

// THE WHOLE USER INTERFACE. Add a row, get a control.
//
// Only settings with more than one right answer are here. Settings that only
// break the game when changed are not offered, and SaveAll removes them from
// the ini (see kRetiredKeys).
//
// A row in a comment is a setting that the launcher does not offer now. The
// mod still reads its key, with the same default. To offer it again, take
// the comment marks away, and take its key out of kRetiredKeys if it is
// there.
const Setting kSettings[] = {
    {kGroup, nullptr, "Picture", nullptr, 0, nullptr},
    {kScale, "resolution_pct", "Resolution scale", "50|200|5", 100,
     "How big a picture the game draws for each eye, as a percentage of what "
     "your headset asks for. The headset's own figure is read when the game "
     "runs, and shown beside the slider. Note that the mod renders a wider "
     "view than the headset needs, so 100% is a little softer than native and "
     "about 115% matches it."},
    {kValues, "anisotropy", "Anisotropic filtering",
     "Off=1|2x=2|4x=4|8x=8|16x=16", 16,
     "Sharpens textures seen at a shallow angle, such as the ground. 16x is "
     "the sharpest and costs almost nothing here."},
    {kBool, "force_mips", "Enable texture mipmaps", nullptr, 1,
     "Uses smaller copies of each texture in the distance. This removes most "
     "of the shimmer on far land and water."},
    {kChoice, "detail_distance", "Model detail distance", "Game|2x|3x|5x", 3,
     "How far out trees, villagers and buildings keep their full models "
     "before they change to simpler ones or disappear. Further costs more "
     "frame time in busy places."},

    {kGroup, nullptr, "View", nullptr, 0, nullptr},
    {kValues, "play_mode", "Play mode",
     "God view=4|Stereoscopic window=1|Tabletop (test)=3", 4,
     "God view: the land is at your waist, so your hands can reach it. It "
     "works with motion controllers and with the mouse. Controllers: push "
     "the right stick forward or back to zoom one step. Hold the left grip "
     "and pull to move the land, across and up and down. Mouse: drag the "
     "land to move it. Turn the "
     "wheel to zoom one step. Press a tilt key to move the ground up or "
     "down. Stereoscopic window: the game is on a 3D screen in front of "
     "you, and your head does not move the camera. Tabletop: the island is "
     "a model on a table in front of you. Walk around it and lean over it. "
     "This mode is a test and is not complete."},
    // Not offered: 7.5 units for each meter is the life size scale, and a
    // different value makes the Hand and the world the wrong size.
    // {kInt, "head_units_per_meter_x10", "World scale, tenths per meter", nullptr,
    //  75,
    //  "How many game units a real meter is worth, in tenths. 75 means 7.5 "
    //  "units, which keeps what your eyes see and what your neck feels the same "
    //  "size."},
    {kBool, "intro_vr", "Intro in stereo", nullptr, 0,
     "Shows the Lionhead logo at the start of the game in 3D around you. The "
     "menu button skips it. It is not finished: some things show double, and "
     "the logo does not move. Off shows the intro flat."},

    // Not offered: the sizes and the depth of the interface in the eye
    // pictures have settled defaults. With controllers the hints and the
    // subtitles are on cards of the mod, which these keys do not change.
    // {kGroup, nullptr, "Comfort", nullptr, 0, nullptr},
    // {kValues, "ui_scale_pct", "Interface size",
    //  "25%=25|30%=30|40%=40|50%=50|75%=75|100%=100", 25,
    //  "The size of subtitles, help text, tooltips and the Creature panel. A "
    //  "smaller size also moves them nearer to the middle of your view. 100% "
    //  "is the size that the game uses. Menus keep their own size."},
    // {kInt, "hint_scale_pct", "Hint size, percent", nullptr, 20,
    //  "The game sizes its hints in pixels, which is small on a monitor and "
    //  "large in a headset. 20 is a fifth of the size the game asks for."},
    // {kBool, "hint_flat", "Draw the hint flat", nullptr, 1,
    //  "The hint stays on the Hand but loses the Hand's depth, so it does not "
    //  "sit a hand's breadth from your face. Turn it off to give it the Hand's "
    //  "own depth again."},
    // {kInt, "ui_shift_px", "Interface depth, pixels", nullptr, 8,
    //  "How far the interface sits in front of you. More pixels is nearer."},
    // {kBool, "ui_adaptive", "Bring the interface forward when needed", nullptr,
    //  1,
    //  "Moves the interface nearer when the world in front of it is nearer, so "
    //  "scenery does not poke through it."},

    {kGroup, nullptr, "Moving around", nullptr, 0, nullptr},
    {kChoice, "snap_turn", "Turn in steps", "Off|30 degrees|45 degrees|90 degrees",
     2,
     "Push the right stick left or right, drag sideways with the middle "
     "mouse button, or press a turn key, to turn in one step. You stay "
     "where you are. The middle mouse button does not tilt the camera. Off "
     "gives the smooth turn and tilt of the game."},
    // Not offered. These four keys act on the camera of the game. In the
    // God view the mod moves the view in cuts and in its own zoom steps,
    // and a story shows in a window in the room, so the defaults are the
    // only values that a headset needs. The zoom step still acts in the
    // stereoscopic window.
    // {kBool, "instant_jumps", "Jump at once", nullptr, 1,
    //  "A double click on the land takes you there in one frame. Off, the "
    //  "camera flies there as the game does it."},
    // {kBool, "camera_glide", "Let the camera glide", nullptr, 0,
    //  "Off, the camera stops when you release the land. On, it slides on for "
    //  "a moment, as the game does it."},
    // {kChoice, "zoom_step", "Zoom step", "Game|Small|Medium|Large", 2,
    //  "Sets how far one click of the mouse wheel zooms. Medium is about three "
    //  "times the step of the game. On Small, Medium and Large, clicks in the "
    //  "next 0.15 seconds join the next step."},
    // {kChoice, "script_camera", "Story camera",
    //  "Game|Cut at the start|Cut at the end", 2,
    //  "How the camera moves in cutscenes and tutorials. Game: it glides as the "
    //  "game moves it. Cut at the start: the view cuts to where each move goes. "
    //  "Cut at the end: the view holds until the camera stops, then it cuts. "
    //  "With both cut choices, a long move also cuts on the way, a close shot "
    //  "gets a little more room, and the camera does not shake."},
    {kChoice, "blink", "Blink when you jump or turn", "Off|Short|Long", 0,
     "Shows black for a moment when you jump or turn. Short is 0.1 seconds "
     "and long is 0.2 seconds. Some people find a jump easier with a blink."},

    {kGroup, nullptr, "Controllers", nullptr, 0, nullptr},
    // Not offered: the Hand is the real hand. The pointer stays in the mod
    // for a dialog box and for tests (controller_hand=0).
    // {kValues, "controller_hand", "Hand", "Direct hand=1|Pointer=0", 1,
    //  "Direct hand: the Hand is where your controller is. Stretch your arm to "
    //  "reach far into the world. Pointer: the controller points a ray, and the "
    //  "Hand goes where the ray points."},
    {kBool, "hand_glow", "Glow at the Hand's point", nullptr, 1,
     "A small glow at the tip of the index finger shows the point of the "
     "Hand that picks up and clicks. It shows while a press of the grip "
     "acts on something."},
    {kValues, "grab_hand", "Grab the world",
     "Grip of the other hand=1|Either grip=2|Off=0", 1,
     "Hold the grip of your other hand. Move that hand to pull the world. "
     "Hold both grips to turn the world and to zoom. Either grip: the grip "
     "of your pointing hand also grabs the world when the Hand is empty and "
     "not over an object. Off: the grips do not move the world."},

    {kGroup, nullptr, "Testing", nullptr, 0, nullptr},
    {kBool, "test_keys", "Test keys", nullptr, 0,
     "Turns on the keys for tests. Each key changes one rule of the mod "
     "while you play, so that you can compare, and the readme lists them. "
     "F12 saves a picture of each eye in the game folder. A player does "
     "not need this."},
};

const int kSettingCount = sizeof(kSettings) / sizeof(kSettings[0]);

// One control per setting, in the same order. The labels are kept too,
// because a slider has no text of its own and puts its value in its label.
HWND g_control[kSettingCount];
HWND g_label[kSettingCount];
HWND g_window = nullptr;
HWND g_status = nullptr;
HWND g_tips = nullptr;
HFONT g_font = nullptr;
HFONT g_bold = nullptr;
char g_game_dir[MAX_PATH];
char g_ini_path[MAX_PATH];
int g_dpi = 96;

int Scaled(int at96) { return MulDiv(at96, g_dpi, 96); }

// --- where the game is -----------------------------------------------------

void TakeGameDir(const char* dir) {
  strcpy_s(g_game_dir, sizeof(g_game_dir), dir);
  _snprintf_s(g_ini_path, sizeof(g_ini_path), _TRUNCATE, "%s\\bwvr.ini", dir);
}

bool FileThere(const char* dir, const char* name) {
  char path[MAX_PATH];
  _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%s", dir, name);
  return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// Where the game's own installer recorded the folder. The installer of the
// mod uses the same value, so this only matters when someone moves the
// launcher by hand.
bool GameDirFromRegistry(char* out, size_t size) {
  HKEY key;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return false;
  }
  DWORD type = 0;
  DWORD bytes = static_cast<DWORD>(size);
  const LONG got = RegQueryValueExA(key, "GameDir", nullptr, &type,
                                    reinterpret_cast<BYTE*>(out), &bytes);
  RegCloseKey(key);
  if (got != ERROR_SUCCESS || type != REG_SZ || bytes == 0) return false;
  out[size - 1] = 0;
  return true;
}

// The launcher sits beside the game after an install. The folders above it
// are where it ends up when it is built from the repository.
bool FindGame() {
  char exe[MAX_PATH];
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  char* slash = strrchr(exe, '\\');
  if (slash != nullptr) *slash = 0;

  const char* candidates[] = {
      "", "\\Black & White Game", "\\..\\Black & White Game",
      "\\..\\..\\Black & White Game", "\\..\\..\\..\\Black & White Game",
  };
  for (int i = 0; i < 5; ++i) {
    char probe[MAX_PATH];
    _snprintf_s(probe, sizeof(probe), _TRUNCATE, "%s%s", exe, candidates[i]);
    char full[MAX_PATH];
    if (GetFullPathNameA(probe, MAX_PATH, full, nullptr) == 0) continue;
    if (FileThere(full, "runblack.exe")) {
      TakeGameDir(full);
      return true;
    }
  }

  char recorded[MAX_PATH] = {0};
  if (GameDirFromRegistry(recorded, sizeof(recorded)) &&
      FileThere(recorded, "runblack.exe")) {
    TakeGameDir(recorded);
    return true;
  }
  return false;
}

// --- the registry, where the game keeps its resolution ---------------------

int ReadRegistryInt(const char* name, int fallback) {
  HKEY key;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return fallback;
  }
  DWORD value = 0;
  DWORD size = sizeof(value);
  DWORD type = 0;
  const LONG got =
      RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value),
                       &size);
  RegCloseKey(key);
  if (got != ERROR_SUCCESS || type != REG_DWORD) return fallback;
  return static_cast<int>(value);
}

void WriteRegistryInt(const char* name, int value) {
  HKEY key;
  if (RegCreateKeyExA(HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, 0,
                      KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
    return;
  }
  const DWORD v = static_cast<DWORD>(value);
  RegSetValueExA(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v),
                 sizeof(v));
  RegCloseKey(key);
}


// --- the ini ---------------------------------------------------------------

int ReadIni(const char* key, int fallback) {
  return GetPrivateProfileIntA("bwvr", key, fallback, g_ini_path);
}

// True when Windows refused a write to bwvr.ini. Under Program Files an
// unelevated write fails, and the launcher must not say "Saved." then.
bool g_write_failed = false;

void WriteIniText(const char* key, const char* text) {
  if (!WritePrivateProfileStringA("bwvr", key, text, g_ini_path)) {
    g_write_failed = true;
  }
}

void WriteIni(const char* key, int value) {
  char text[32];
  _snprintf_s(text, sizeof(text), _TRUNCATE, "%d", value);
  WriteIniText(key, text);
}

// WHAT THE HEADSET ASKS FOR, per eye.
//
// The game writes this into bwvr.ini every time it starts with a headset
// connected, because only a running OpenXR session can ask. Until then these
// are a Quest 3's figures, which are a reasonable guess and are replaced by
// the truth on the first run.
// A slider's range lives in the choices field as "low|high|step", so the
// Setting struct does not need to grow a pair of numbers that only one kind
// would ever use.
void ScaleRange(const char* spec, int* low, int* high, int* step) {
  *low = 50;
  *high = 200;
  *step = 5;
  if (spec == nullptr) return;
  int value[3] = {*low, *high, *step};
  int found = 0;
  const char* p = spec;
  while (found < 3 && *p != 0) {
    value[found++] = atoi(p);
    while (*p != 0 && *p != '|') ++p;
    if (*p == '|') ++p;
  }
  if (found >= 1) *low = value[0];
  if (found >= 2) *high = value[1];
  if (found >= 3) *step = value[2];
}

int HeadsetWidth() { return ReadIni("headset_width", 2064); }
int HeadsetHeight() { return ReadIni("headset_height", 2208); }

// A square render suits the game best: it derives its vertical angle from the
// render aspect, so a square one covers the headset while wasting the fewest
// pixels. So one number drives both axes, taken from the larger side.
void ScaleToPixels(int percent, int* width, int* height) {
  int side = HeadsetWidth();
  if (HeadsetHeight() > side) side = HeadsetHeight();
  int pixels = MulDiv(side, percent, 100);
  // Keep it even, and inside what the game and the driver will take.
  pixels = (pixels / 2) * 2;
  if (pixels < 640) pixels = 640;
  if (pixels > 4096) pixels = 4096;
  *width = pixels;
  *height = pixels;
}

void SayScale(int index, int percent) {
  if (g_label[index] == nullptr) return;
  int w = 0, h = 0;
  ScaleToPixels(percent, &w, &h);
  char text[128];
  _snprintf_s(text, sizeof(text), _TRUNCATE, "%s: %d%% (%d x %d)",
              kSettings[index].label, percent, w, h);
  SetWindowTextA(g_label[index], text);
}

// --- pre-flight, the same checks the script did ----------------------------

void SayStatus(const char* text) { SetWindowTextA(g_status, text); }

// The build of the game. The mod's addresses fit one file only, so a player
// with another build must be told, rather than shown a crash.
bool GameBuildIsRight(char* message, size_t size) {
  char exe[MAX_PATH];
  _snprintf_s(exe, sizeof(exe), _TRUNCATE, "%s\\runblack.exe", g_game_dir);
  wchar_t wide[MAX_PATH];
  if (MultiByteToWideChar(CP_ACP, 0, exe, -1, wide, MAX_PATH) == 0) return true;

  const bwvr::ExeReport report = bwvr::CheckGameExe(wide);
  if (report.verdict == bwvr::ExeVerdict::kSupported) return true;
  if (report.verdict == bwvr::ExeVerdict::kUnreadable) {
    _snprintf_s(message, size, _TRUNCATE,
                "Cannot read runblack.exe (error %lu).", report.error);
    return false;
  }
  _snprintf_s(message, size, _TRUNCATE,
              "This Black & White is %s. The mod needs the unofficial v1.42 "
              "patch.",
              report.build != nullptr ? report.build : "a build the mod does "
                                                       "not know");
  return false;
}

void CheckEverything() {
  char message[512];
  message[0] = 0;

  // Every file the mod needs at run time. A missing dxwrapper file makes the
  // mod fall back to a slow path in silence, so this check lists them.
  static const char* const kNeeded[] = {
      "ddraw.dll",
      "openxr_loader.dll",
      "dxwrapper\\ddraw.dll",
      "dxwrapper\\dxwrapper.dll",
      "dxwrapper\\dxwrapper.ini",
  };
  const char* missing = nullptr;
  for (const char* name : kNeeded) {
    if (!FileThere(g_game_dir, name)) {
      missing = name;
      break;
    }
  }
  if (missing != nullptr) {
    _snprintf_s(message, sizeof(message), _TRUNCATE,
                "%s is missing from the game folder. Install the %s again.",
                missing, kProductName);
    SayStatus(message);
    return;
  }

  if (!GameBuildIsRight(message, sizeof(message))) {
    SayStatus(message);
    return;
  }

  // The game is 32 bit, so a runtime registered only for 64 bit cannot serve
  // it. This is the single most common reason for a headset showing nothing.
  HKEY key;
  bool runtime = false;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "SOFTWARE\\WOW6432Node\\Khronos\\OpenXR\\1", 0, KEY_READ,
                    &key) == ERROR_SUCCESS) {
    char path[MAX_PATH] = {0};
    DWORD size = sizeof(path);
    DWORD type = 0;
    if (RegQueryValueExA(key, "ActiveRuntime", nullptr, &type,
                         reinterpret_cast<BYTE*>(path), &size) ==
        ERROR_SUCCESS) {
      const char* name = strrchr(path, '\\');
      _snprintf_s(message, sizeof(message), _TRUNCATE, "Ready. OpenXR runtime: %s",
                  name != nullptr ? name + 1 : path);
      runtime = true;
    }
    RegCloseKey(key);
  }
  if (!runtime) {
    strcpy_s(message, sizeof(message),
             "No 32 bit OpenXR runtime is registered. The game is 32 bit, so a "
             "64 bit only runtime cannot serve it.");
  }
  SayStatus(message);
}

// --- reading and writing the whole table -----------------------------------

int ChoiceValue(const char* choices, int index);
int ChoiceIndex(const char* choices, int value);

void LoadAll() {
  for (int i = 0; i < kSettingCount; ++i) {
    const Setting& s = kSettings[i];
    if (g_control[i] == nullptr) continue;
    switch (s.kind) {
      case kBool:
        SendMessageA(g_control[i], BM_SETCHECK,
                     ReadIni(s.key, s.fallback) != 0 ? BST_CHECKED
                                                     : BST_UNCHECKED,
                     0);
        break;
      case kInt: {
        char text[32];
        _snprintf_s(text, sizeof(text), _TRUNCATE, "%d",
                    ReadIni(s.key, s.fallback));
        SetWindowTextA(g_control[i], text);
        break;
      }
      case kChoice:
        SendMessageA(g_control[i], CB_SETCURSEL, ReadIni(s.key, s.fallback), 0);
        break;
      case kValues: {
        int value = ReadIni(s.key, s.fallback);
        // The God view replaced the immersive mode, play mode 0. An older
        // bwvr.ini can still hold 0, and it shows the God view.
        if (strcmp(s.key, "play_mode") == 0 && value == 0) value = 4;
        SendMessageA(g_control[i], CB_SETCURSEL, ChoiceIndex(s.choices, value),
                     0);
        break;
      }
      case kScale: {
        // The percentage is kept in the ini and the registry holds whatever it
        // came out as. Reading the percentage back is what makes the slider
        // land where the player left it, whatever the headset now asks for.
        int low = 0, high = 0, step = 0;
        ScaleRange(s.choices, &low, &high, &step);
        int percent = ReadIni(s.key, s.fallback);
        if (percent < low) percent = low;
        if (percent > high) percent = high;
        SendMessageA(g_control[i], TBM_SETPOS, TRUE, percent);
        SayScale(i, percent);
        break;
      }
      default:
        break;
    }
  }
}

// Keys the launcher no longer offers. See SaveAll.
const char* const kRetiredKeys[] = {
    "head_tracking",   "immersive",        "head_fov_deg",
    "eye_separation_milli", "hand_anchored", "head_roll",
    "ui_world_lock",   "pointer_clip",     "pointer_scale",
    "window_width",    "window_height",    "freeze_time",
    "skip_hand_pass2", "skip_physics_pass2", "skip_snapshot",
    "profile",         "capture_every_seconds", "subtitle_lift_pct",
    // Keys for desktop tests. stereo_headless=1 stops the headset session
    // with no message.
    "stereo_headless", "draw_probe",       "draw_probe_frames",
    "draw_probe_delay", "fov_sweep_after", "fov_absolute_deg",
    "fix_unscaled",    "fix_copy",         "fix_inverse",
    "fix_random",      "move_camera_point", "skip_hand_prepare",
    "skip_desktop_present", "hint_mode",   "ui_accounting",
    "filter_survey",   "gdi_watch",        "dump_camera",
    "headless_grab",   "fake_head",        "fake_head_yaw",
    "fake_head_pitch", "fake_head_roll",   "fake_head_sweep",
    "fake_controller", "fake_controller_right_cm", "fake_controller_up_cm",
    "fake_controller_forward_cm", "fake_controller_yaw",
    "fake_controller_pitch", "fake_controller_roll", "fake_controller_sweep",
    "fake_controller_sweep_yaw", "fake_controller_sweep_pitch",
    "fake_controller_script", "fake_controller_points",
    "fake_controller_left_points", "fake_head_right_cm", "fake_head_up_cm",
    "fake_head_forward_cm", "grab_probe", "fake_controller_gestures",
    "fake_controller_gesture_cm", "fake_controller_front", "hint_profile",
    "hint_selftest",   "miracle_test_seeds", "miracle_test_cancel",
    "fake_mouse_script", "fake_mouse_step_px", "fake_mouse_x_pct",
    "fake_mouse_y_pct", "fake_mouse_x2_pct", "fake_mouse_y2_pct",
    "radial_test_save", "village_test_chants", "village_test_census",
    "village_test_icon", "village_test_site", "seek_step_cm",
    "village_test_miracles", "village_test_scaffolds",
    "village_test_workshop",
    // Settings that the launcher offered before. Each has one right answer
    // in a headset, and the mod's default is that answer. The other
    // settings that are not offered now (the sizes and the depth of the
    // interface, the hint, the zoom step) stay in the ini as they are, so a
    // value that a person wrote by hand is kept.
    "controller_hand", "script_camera",    "instant_jumps",
    "camera_glide",    "head_units_per_meter_x10",
};

// dxwrapper sets its own anisotropic filtering on the Direct3D 9 device, from
// dxwrapper.ini. Left at 16, it would overrule "Off" in this launcher. So the
// launcher writes the same level there. dxwrapper reads 0 as "leave it to the
// game".
void WriteBackendAnisotropy(int level) {
  char path[MAX_PATH];
  _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\dxwrapper\\dxwrapper.ini",
              g_game_dir);
  if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
  char text[32];
  _snprintf_s(text, sizeof(text), _TRUNCATE, "%d", level > 1 ? level : 0);
  WritePrivateProfileStringA("d3d9", "AnisotropicFiltering", text, path);
}

// The game's own display settings, as they were before the mod first
// changed them. The mod runs the game in a window at a square size, which
// the plain game would then open too. So the uninstaller puts these back.
// They are written once, in their own section, which the retired key sweep
// never touches. -1 means the game had no value of its own.
void SaveGameDisplayOnce() {
  if (GetPrivateProfileIntA("game_before", "saved", 0, g_ini_path) != 0) return;
  static const char* const kNames[] = {"ScreenW", "ScreenH", "FullScreen",
                                       "ScreenD"};
  for (const char* name : kNames) {
    char text[32];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%d",
                ReadRegistryInt(name, -1));
    WritePrivateProfileStringA("game_before", name, text, g_ini_path);
  }
  WritePrivateProfileStringA("game_before", "saved", "1", g_ini_path);
}

void SaveAll() {
  for (int i = 0; i < kSettingCount; ++i) {
    const Setting& s = kSettings[i];
    if (g_control[i] == nullptr) continue;
    switch (s.kind) {
      case kBool:
        WriteIni(s.key, SendMessageA(g_control[i], BM_GETCHECK, 0, 0) ==
                                BST_CHECKED
                            ? 1
                            : 0);
        break;
      case kInt: {
        char text[32] = {0};
        GetWindowTextA(g_control[i], text, sizeof(text));
        WriteIni(s.key, atoi(text));
        break;
      }
      case kChoice:
        WriteIni(s.key,
                 static_cast<int>(SendMessageA(g_control[i], CB_GETCURSEL, 0, 0)));
        break;
      case kValues:
        WriteIni(s.key,
                 ChoiceValue(s.choices, static_cast<int>(SendMessageA(
                                            g_control[i], CB_GETCURSEL, 0, 0))));
        break;
      case kScale: {
        const int percent =
            static_cast<int>(SendMessageA(g_control[i], TBM_GETPOS, 0, 0));
        WriteIni(s.key, percent);
        // The game reads its own resolution from the registry, not from the
        // ini, so the percentage has to be turned into pixels here.
        int w = 0, h = 0;
        ScaleToPixels(percent, &w, &h);
        WriteRegistryInt("ScreenW", w);
        WriteRegistryInt("ScreenH", h);
        break;
      }
      default:
        break;
    }
  }
  // These are not offered as choices, because getting them wrong stops the mod
  // working at all and there is only one right answer.
  WriteIniText("backend", "dxwrapper\\ddraw.dll");
  WriteIni("gpu_bridge", 1);
  WriteIni("vr", 1);
  WriteIni("stereo", 1);
  WriteIni("head_camera", 1);
  // Settings that have one right answer and are not offered. Removing them
  // means an old ini cannot keep a wrong value. The mod's own default is the
  // right answer for each, and the keys still work for testing.
  for (const char* key : kRetiredKeys) {
    WritePrivateProfileStringA("bwvr", key, nullptr, g_ini_path);
  }
  WriteBackendAnisotropy(ReadIni("anisotropy", 16));
  SaveGameDisplayOnce();
  // The game reads these from the registry. If they are missing, it starts
  // full screen at the size of the desktop, in 16 bit color. Another install
  // of the game can remove them. The mod needs a window at the size above.
  WriteRegistryInt("FullScreen", 0);
  WriteRegistryInt("ScreenD", 32);
}

void Play(bool new_game) {
  SaveAll();
  char exe[MAX_PATH];
  _snprintf_s(exe, sizeof(exe), _TRUNCATE, "%s\\runblack.exe", g_game_dir);
  SHELLEXECUTEINFOA info = {sizeof(info)};
  info.lpFile = exe;
  info.lpDirectory = g_game_dir;
  info.lpParameters = new_game ? "-NEWGAME" : nullptr;
  info.nShow = SW_SHOWNORMAL;
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  if (ShellExecuteExA(&info)) {
    if (info.hProcess != nullptr) CloseHandle(info.hProcess);
    PostMessageA(g_window, WM_CLOSE, 0, 0);
  } else {
    SayStatus("Could not start runblack.exe.");
  }
}

// --- building the window ---------------------------------------------------

void AddTip(HWND control, const char* text) {
  if (g_tips == nullptr || text == nullptr) return;
  TOOLINFOA tip = {0};
  tip.cbSize = sizeof(tip);
  tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  tip.hwnd = g_window;
  tip.uId = reinterpret_cast<UINT_PTR>(control);
  tip.lpszText = const_cast<char*>(text);
  SendMessageA(g_tips, TTM_ADDTOOLA, 0, reinterpret_cast<LPARAM>(&tip));
}

void FillChoices(HWND combo, const char* choices) {
  char buffer[512];
  strcpy_s(buffer, sizeof(buffer), choices);
  char* start = buffer;
  for (char* p = buffer;; ++p) {
    if (*p == '|' || *p == 0) {
      const char end = *p;
      *p = 0;
      // A kValues item is "label=value". Only the label is shown.
      char* value = strchr(start, '=');
      if (value != nullptr) *value = 0;
      SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(start));
      start = p + 1;
      if (end == 0) break;
    }
  }
}

// The value of item `index` in a "label=value|label=value" list.
int ChoiceValue(const char* choices, int index) {
  const char* p = choices;
  for (int i = 0; i < index && p != nullptr; ++i) {
    p = strchr(p, '|');
    if (p != nullptr) ++p;
  }
  if (p == nullptr) return 0;
  const char* value = strchr(p, '=');
  return value != nullptr ? atoi(value + 1) : 0;
}

// The item whose value is `value`. A value that is not in the list selects
// the largest item below it, so a hand edited ini still shows something near.
int ChoiceIndex(const char* choices, int value) {
  int best = 0;
  int best_value = -1;
  int index = 0;
  for (const char* p = choices; p != nullptr; ++index) {
    const char* here = strchr(p, '=');
    const char* next = strchr(p, '|');
    if (here != nullptr && (next == nullptr || here < next)) {
      const int v = atoi(here + 1);
      if (v <= value && v > best_value) {
        best = index;
        best_value = v;
      }
    }
    p = next != nullptr ? next + 1 : nullptr;
  }
  return best;
}

// How many rows a group takes: its heading and every setting under it.
int GroupRows(int group) {
  int rows = 1;
  for (int i = group + 1; i < kSettingCount; ++i) {
    if (kSettings[i].kind == kGroup) break;
    ++rows;
  }
  return rows;
}

// Lay the table out in columns, starting a new one when the current gets tall.
// This is what lets the table grow without the window growing past the screen.
void BuildControls() {
  const int pad = Scaled(12);
  const int row = Scaled(26);
  const int label_w = Scaled(210);
  const int field_w = Scaled(120);
  const int col_w = label_w + field_w + pad;
  // The tallest a column may get. Rows and pixels are interchangeable
  // everywhere below, so this number also caps the window height.
  const int max_rows = 20;

  int column = 0;
  int used = 0;
  int tallest = 0;
  int id = 100;

  for (int i = 0; i < kSettingCount; ++i) {
    const Setting& s = kSettings[i];
    // Break between groups, so a heading always keeps all of its settings. A
    // group taller than a column is the one case that still breaks inside it.
    if (s.kind == kGroup) {
      if (used > 0 && used + GroupRows(i) > max_rows) {
        ++column;
        used = 0;
      }
    } else if (used >= max_rows) {
      ++column;
      used = 0;
    }
    const int x = pad + column * (col_w + pad);
    const int y = pad + used * row;

    if (s.kind == kGroup) {
      HWND heading = CreateWindowExA(
          0, "STATIC", s.label, WS_CHILD | WS_VISIBLE, x, y + Scaled(6),
          col_w, row - Scaled(4), g_window, nullptr, nullptr, nullptr);
      SendMessageA(heading, WM_SETFONT, reinterpret_cast<WPARAM>(g_bold), TRUE);
      g_control[i] = nullptr;
      used += 1;
      if (used > tallest) tallest = used;
      continue;
    }

    g_label[i] = nullptr;
    if (s.kind != kBool) {
      // A slider says its value in its own label, so it needs the whole width
      // rather than the label column.
      const int width = s.kind == kScale ? col_w : label_w;
      HWND label = CreateWindowExA(0, "STATIC", s.label,
                                   WS_CHILD | WS_VISIBLE | SS_LEFT, x,
                                   y + Scaled(4), width, row - Scaled(6),
                                   g_window, nullptr, nullptr, nullptr);
      SendMessageA(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
      AddTip(label, s.help);
      g_label[i] = label;
    }

    HWND control = nullptr;
    switch (s.kind) {
      case kBool:
        control = CreateWindowExA(
            0, "BUTTON", s.label, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x,
            y + Scaled(2), col_w, row - Scaled(4), g_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), nullptr,
            nullptr);
        break;
      case kInt:
        control = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT, x + label_w, y,
            field_w, row - Scaled(4), g_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), nullptr,
            nullptr);
        break;
      case kScale: {
        // TBS_NOTICKS keeps the slider inside one ordinary row, which matters
        // because the whole layout counts rows rather than pixels. A taller
        // control would mean the column break and the window height stopped
        // agreeing with each other.
        int low = 0, high = 0, step = 0;
        ScaleRange(s.choices, &low, &high, &step);
        control = CreateWindowExA(
            0, TRACKBAR_CLASS, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, x + label_w, y,
            field_w, row - Scaled(4), g_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), nullptr,
            nullptr);
        SendMessageA(control, TBM_SETRANGE, TRUE, MAKELONG(low, high));
        SendMessageA(control, TBM_SETPAGESIZE, 0, step);
        SendMessageA(control, TBM_SETLINESIZE, 0, step);
        break;
      }
      case kChoice:
      case kValues:
        control = CreateWindowExA(
            0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            x + label_w, y, field_w, row * 8, g_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), nullptr,
            nullptr);
        FillChoices(control, s.choices);
        break;
      default:
        break;
    }
    if (control != nullptr) {
      SendMessageA(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
      AddTip(control, s.help);
    }
    g_control[i] = control;
    used += 1;
    if (used > tallest) tallest = used;
  }

  const int columns = column + 1;
  const int width = pad + columns * (col_w + pad);
  const int height = pad + tallest * row + Scaled(76);

  // The buttons and the status line, along the bottom.
  const int button_w = Scaled(110);
  const int button_h = Scaled(28);
  const int button_y = height - Scaled(66);
  HWND play = CreateWindowExA(
      0, "BUTTON", "Play in VR", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
      width - pad - button_w * 2 - Scaled(8), button_y, button_w, button_h,
      g_window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
  HWND fresh = CreateWindowExA(0, "BUTTON", "Play, new game",
                               WS_CHILD | WS_VISIBLE, width - pad - button_w,
                               button_y, button_w, button_h, g_window,
                               reinterpret_cast<HMENU>(2), nullptr, nullptr);
  HWND support = CreateWindowExA(
      0, "BUTTON", "Support", WS_CHILD | WS_VISIBLE, pad + button_w + Scaled(8),
      button_y, button_w, button_h, g_window,
      reinterpret_cast<HMENU>(4), nullptr, nullptr);
  SendMessageA(support, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
  AddTip(support, "Opens " BWVR_SUPPORT_URL " in your browser. The mod is "
                  "free, and it stays free.");
  HWND save = CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE, pad,
                              button_y, button_w, button_h, g_window,
                              reinterpret_cast<HMENU>(3), nullptr, nullptr);
  // SS_NOPREFIX, because the product name holds an ampersand and a static
  // control would otherwise read it as an access key.
  g_status = CreateWindowExA(0, "STATIC", "",
                             WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                             pad, height - Scaled(32), width - pad * 2,
                             Scaled(24), g_window, nullptr, nullptr, nullptr);
  SendMessageA(play, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
  SendMessageA(fresh, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
  SendMessageA(save, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
  SendMessageA(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
  AddTip(save, "Write the settings without starting the game.");
  AddTip(fresh, "Start a new game on the first island, skipping the menu.");

  RECT want = {0, 0, width, height};
  AdjustWindowRect(&want, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
  RECT work = {0, 0, 0, 0};
  SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
  const int frame_w = want.right - want.left;
  const int frame_h = want.bottom - want.top;
  SetWindowPos(g_window, nullptr,
               work.left + ((work.right - work.left) - frame_w) / 2,
               work.top + ((work.bottom - work.top) - frame_h) / 2, frame_w,
               frame_h, SWP_NOZORDER);
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case 1:
          Play(false);
          return 0;
        case 2:
          Play(true);
          return 0;
        case 3:
          SaveAll();
          if (g_write_failed) {
            SayStatus("Windows did not let the launcher write bwvr.ini in the "
                      "game folder.");
          } else {
            SayStatus("Saved.");
          }
          return 0;
        case 4:
          // The support page. It opens in the player's own browser.
          ShellExecuteA(nullptr, "open", BWVR_SUPPORT_URL, nullptr, nullptr,
                        SW_SHOWNORMAL);
          return 0;
        default:
          break;
      }
      break;
    // A trackbar reports itself here, not through WM_COMMAND, so without this
    // the value beside the slider would never move.
    case WM_HSCROLL: {
      HWND from = reinterpret_cast<HWND>(l);
      for (int i = 0; i < kSettingCount; ++i) {
        if (g_control[i] != from || kSettings[i].kind != kScale) continue;
        SayScale(i, static_cast<int>(SendMessageA(from, TBM_GETPOS, 0, 0)));
        break;
      }
      return 0;
    }
    case WM_CTLCOLORSTATIC:
      SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcA(window, message, w, l);
}

void MakeFonts() {
  NONCLIENTMETRICSA metrics = {sizeof(metrics)};
  if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                            0)) {
    g_font = CreateFontIndirectA(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfWeight = FW_SEMIBOLD;
    g_bold = CreateFontIndirectA(&metrics.lfMessageFont);
  }
  if (g_font == nullptr) {
    g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    g_bold = g_font;
  }
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  SetProcessDPIAware();
  HDC screen = GetDC(nullptr);
  if (screen != nullptr) {
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
  }

  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES |
                                                         ICC_BAR_CLASSES};
  InitCommonControlsEx(&controls);

  if (!FindGame()) {
    MessageBoxA(nullptr,
                "Cannot find runblack.exe. Install the " BWVR_PRODUCT_NAME
                " in the folder that holds your Black & White game.",
                kProductName, MB_ICONERROR | MB_OK);
    return 1;
  }

  MakeFonts();

  WNDCLASSA wc = {0};
  wc.lpfnWndProc = Proc;
  wc.hInstance = instance;
  wc.lpszClassName = kWindowClass;
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconA(instance, MAKEINTRESOURCEA(1));
  if (wc.hIcon == nullptr) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  RegisterClassA(&wc);

  g_window = CreateWindowExA(
      0, kWindowClass, kTitle, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME &
                                   ~WS_MAXIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, Scaled(700), Scaled(560), nullptr, nullptr,
      instance, nullptr);
  if (g_window == nullptr) return 1;

  g_tips = CreateWindowExA(0, TOOLTIPS_CLASSA, nullptr,
                           WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                           g_window, nullptr, instance, nullptr);
  SendMessageA(g_tips, TTM_SETMAXTIPWIDTH, 0, Scaled(380));

  BuildControls();
  LoadAll();
  CheckEverything();
  ShowWindow(g_window, SW_SHOW);
  UpdateWindow(g_window);

  MSG message;
  while (GetMessageA(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageA(g_window, &message)) {
      TranslateMessage(&message);
      DispatchMessageA(&message);
    }
  }
  return 0;
}
