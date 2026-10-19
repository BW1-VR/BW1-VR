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

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "../common/exe_check.h"
#include "../common/version.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")

// The controls of Windows with its visual styles (version 6 of the common
// controls). Without this line Windows gives the launcher version 5, which
// draws in the old style and cannot show a picture beside the text of a
// button.
#pragma comment(linker, "\"/manifestdependency:type='win32' " \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' " \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
    "language='*'\"")

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
  kSlider,      // a slider with a value of its own: "low|high|step|unit"
  kRenderer,    // the renderer: "backend" in the ini, as text
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
    // Before the resolution: a save writes the rows in order, and the size of
    // the render reads this key.
    {kBool, "one_walk", "One scene walk for both eyes (prototype)", nullptr,
     1,
     "The game draws the scene one time, for the left eye, and the mod makes "
     "the right eye from the same drawing. This takes about 40 percent off "
     "the time of the scene. The render is a quarter wider, so that the "
     "right eye has a picture at its outer edge. A test build: report what "
     "looks wrong."},
    // Before the resolution too: the size of the render reads this key.
    {kValues, "fov_margin_pct", "Margin around the view",
     "None=0|5%=5|10%=10|15%=15|20%=20|25%=25|30%=30", 0,
     "If you see black at the edge of the view when you turn your head fast, "
     "set a margin. The game then draws that much more on each side, and the "
     "picture has that many more pixels each way, so it stays as sharp. A "
     "margin costs frame time: 20% is almost half as many pixels again."},
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

    {kGroup, nullptr, "Performance", nullptr, 0, nullptr},
    {kRenderer, "backend", "Renderer", "Vulkan|Direct3D 9", 0,
     "Vulkan (the default): the mod draws the game with a Vulkan renderer of "
     "its own, and the pictures go to the headset and to the window through "
     "Vulkan. The mod then uses no dxwrapper, no Direct3D 9 and no Direct3D "
     "11. Your GPU needs Vulkan 1.3: with no Vulkan 1.3 the launcher keeps "
     "Direct3D 9. "
     "Direct3D 9: the game draws through dxwrapper and Direct3D 9, as in "
     "the versions before. The change counts from the next start."},
    {kBool, "d3d9_backend", "Direct3D 9 through DXVK", nullptr, 0,
     "Only with the renderer Direct3D 9. The game draws through DXVK and "
     "Vulkan in place of the Direct3D 9 of Windows. This takes about half "
     "of the time of the driver off each frame. The mod uses it only when "
     "your GPU has Vulkan 1.3 and the file dxvk\\d3d9.dll is in the game "
     "folder. Else, and when a start with it fails, the game draws as "
     "before. The change counts from the next start."},
    {kBool, "xr_vulkan", "Headset session with Vulkan", nullptr, 0,
     "Only with the renderer Direct3D 9: the renderer Vulkan always has a "
     "session with Vulkan. The headset gets the pictures through Vulkan in "
     "place of Direct3D 11, and the mod draws the scene of each eye with its "
     "Vulkan renderer. Your GPU needs Vulkan 1.3. When the session with "
     "Vulkan does not start, the mod starts one with Direct3D 11. The change "
     "counts from the next start."},

    {kGroup, nullptr, "Foveated rendering", nullptr, 0, nullptr},
    {kBool, "foveation", "Fewer shades away from the middle", nullptr, 0,
     "The middle of each eye picture keeps all its shades of color, and the "
     "parts around it get one shade for 2 by 2 pixels (4 by 4 at the edge "
     "when the GPU has it). The edges of things stay sharp. This can take "
     "time off a GPU that is slow for each pixel. With an eye tracker (for "
     "example Quest Pro) the middle follows your gaze. With no eye tracker "
     "(for example Quest 3) it stays in the middle of the view. It needs "
     "the Vulkan renderer, or the headset session with Vulkan, and a GPU "
     "with variable rate shading."},
    {kSlider, "foveation_inner_deg", "All shades within", "10|45|1| degrees",
     22,
     "How far the part with all shades goes from the middle to each side. "
     "Smaller saves more."},
    {kSlider, "foveation_outer_deg", "One shade for 2 by 2 within",
     "15|70|1| degrees", 35,
     "Past this angle a shade is for 4 by 4 pixels, when the GPU has it. It "
     "is at least the angle above."},
    {kBool, "fovea_res", "Fewer pixels away from the middle", nullptr, 0,
     "The mod draws each eye picture from two pictures: one of the whole "
     "view with fewer pixels, and one of the middle with more. The middle "
     "follows your gaze with an eye tracker, and stays in the middle of the "
     "view with none. The three sliders below set the two resolutions and "
     "the size of the middle. It needs the Vulkan renderer, or the headset "
     "session with Vulkan."},
    {kSlider, "fovea_res_outer_pct", "Resolution of the whole view",
     "5|100|5|%", 50,
     "The pixels of the whole view, as a percentage of the eye picture on "
     "each side. 50% is a quarter of the pixels."},
    {kSlider, "fovea_res_center_pct", "Resolution of the middle", "50|200|5|%",
     100,
     "The pixels of the middle, as a percentage of the eye picture on each "
     "side. More than 100% makes the middle smoother than the eye picture "
     "alone."},
    {kSlider, "fovea_res_center_deg", "Size of the middle", "10|45|1| degrees",
     25,
     "How far the middle goes from its centre to each side. A larger middle "
     "is sharp further out and costs more frame time."},
    {kSlider, "fovea_res_center_down_deg", "Middle lower by", "0|15|1| degrees",
     0,
     "Moves the sharp middle down from straight ahead, for example to the "
     "land and the hands at your waist. Only with no eye tracker: a middle "
     "that follows your gaze stays where you look."},
    {kBool, "foveation_show", "Show the parts with fewer details", nullptr, 0,
     "A blue colour over the parts with fewer shades or fewer pixels, so "
     "you can find the settings that you like. Turn it off to play. With "
     "the test keys, Shift O and Shift P change the two kinds while you "
     "play."},

    {kGroup, nullptr, "View", nullptr, 0, nullptr},
    {kValues, "play_mode", "Play mode",
     "God view=4|Stereoscopic window=1|Tabletop (test)=3", 4,
     "God view: the land is at your waist, so your hands can reach it. It "
     "works with motion controllers and with the mouse. Controllers: push "
     "the stick of your Hand forward or back to zoom. Hold the grip of your "
     "other hand and pull to move the land, across and up and down. Your "
     "Hand is the right controller, or the left one with a left hand (see "
     "Hand). Mouse: drag the "
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
     "Shows the Lionhead logo at the start of the game in 3D around you. Your "
     "view moves through the orbs on the path of the game, and it does not "
     "turn: you look where your head looks. The menu button skips it. This "
     "setting is a test. Off shows the intro flat."},
    {kValues, "cutscene_immersive", "Cut scenes",
     "In a window=0|Around you=1", 0,
     "In a window: a cut scene of the game plays on a screen in front of "
     "you, and the land around you stays still. Around you: a cut scene "
     "plays all around you, and the camera of the game moves you with all "
     "its motion. Choose it only if a camera that moves is no problem for "
     "you."},

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
     "Push the stick of your Hand left or right, drag sideways with the middle "
     "mouse button, or press a turn key, to turn in one step. You stay "
     "where you are. The middle mouse button does not tilt the camera. Off "
     "gives the smooth turn and tilt of the game."},
    {kValues, "god_zoom_smooth", "Zoom with the stick", "In steps=0|Smooth=1",
     0,
     "In steps: one push of the stick of your Hand forward or back is one zoom "
     "step. Smooth: the zoom changes while you hold the stick. It stops when "
     "you let the stick go."},
    {kValues, "god_zoom_forward_in", "Stick forward",
     "Zooms out=0|Zooms in=1", 0,
     "The direction of the zoom on the stick of your Hand. Zooms out: push the "
     "stick forward and the world gets smaller, pull it back and the world "
     "gets larger. Zooms in is the other way."},
    {kValues, "god_zoom_about", "Center of the zoom",
     "The land you look at=1|The land under you=0|Your Hand=2", 1,
     "The point that stays where it is when you zoom with the stick. The "
     "land you look at: the thing in the middle of your view keeps its place "
     "and changes its size. The land under you: you keep your place on the "
     "land. Your Hand: the land at your Hand keeps its place."},
    {kValues, "move_stick", "Move with the other stick",
     "Head direction=1|Controller direction=2|Off=0", 1,
     "Push the stick of your other hand (the left stick, or the right stick "
     "with a left hand) to move over the "
     "land in the God view. You stop when you let the stick go. Head "
     "direction: forward is where your head faces. Controller direction: "
     "forward is where that controller points. Off: the stick does not move "
     "you. Some people feel ill when a stick moves them: use Off then, and "
     "move with the grip and with the teleport."},
    {kValues, "move_stick_follow_land", "Walk follows the land", "On=1|Off=0",
     1,
     "When you move with the stick, the height of your view follows "
     "the hills, so the land stays at the same height below your hands. The "
     "view goes up and down smoothly, and it does not follow each small "
     "bump. It moves only while you push the stick. Off: the view keeps its "
     "height, and a hill comes nearer or goes farther away."},
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
    {kValues, "left_hand", "Hand", "Follow the game=-1|Right=0|Left=1", -1,
     "Which controller is the Hand. Follow the game: the left handed option "
     "in the options of the game chooses it. Right or Left: the controller "
     "of that hand is the Hand. The other controller grabs the world, moves "
     "you with its stick and opens the ring."},
    {kBool, "hand_glow", "Glow at the Hand's point", nullptr, 0,
     "A small glow at the tip of the index finger shows the point of the "
     "Hand that picks up and clicks. It shows while a press of the grip "
     "acts on something."},
    {kValues, "grab_hand", "Grab the world",
     "Grip of the other hand=1|Either grip=2|Off=0", 1,
     "Hold the grip of your other hand. Move that hand to pull the world. "
     "Hold both grips to turn the world and to zoom. Either grip: the grip "
     "of your pointing hand also grabs the world when the Hand is empty and "
     "not over an object. Off: the grips do not move the world."},
    {kValues, "hint_learn", "Hints", "Until learned=1|Always=0", 1,
     "The cards beside your wrists show what each button does. Until "
     "learned: a line goes away after you do its action 3 times. It comes "
     "back after 10 minutes of play without that action. In a lesson of the "
     "tutorial, each line shows. Always: each line always shows. To show all "
     "lines again, delete bwvr-hint-counts.ini in the game folder."},

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
HWND g_heading[kSettingCount];
// The buttons below the table.
enum {
  kButtonPlay,
  kButtonFresh,
  kButtonSave,
  kButtonPlain,
  kButtonSupport,
  kButtonCount
};
HWND g_buttons[kButtonCount];
HWND g_window = nullptr;
HWND g_status = nullptr;
// The row "Renderer" shows Vulkan (UpdateRendererRows).
bool g_renderer_vulkan = false;
HWND g_tips = nullptr;
HFONT g_font = nullptr;
HFONT g_bold = nullptr;
char g_game_dir[MAX_PATH];
char g_ini_path[MAX_PATH];
int g_dpi = 96;
// The most rows of a column. A test sets a smaller number with "-rows" to get
// a picture of a table that has more columns.
int g_max_rows = 26;

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

void ReadIniText(const char* key, char* out, DWORD size) {
  GetPrivateProfileStringA("bwvr", key, "", out, size, g_ini_path);
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
  // THE MARGIN (stereo.cpp, PrepareHeadView). With fov_margin_pct the game
  // draws that many percent more on each side of the view. The render gets
  // the same percent more pixels each way, so a degree keeps its pixels.
  int margin = ReadIni("fov_margin_pct", 0);
  if (margin < 0) margin = 0;
  if (margin > 30) margin = 30;
  pixels = MulDiv(pixels, 100 + margin, 100);
  // Keep it even, and inside what the game and the driver will take.
  pixels = (pixels / 2) * 2;
  if (pixels < 640) pixels = 640;
  if (pixels > 4096) pixels = 4096;
  *width = pixels;
  *height = pixels;
  // ONE WALK (docs/backend.md, "One walk for both eyes"). The right eye comes
  // from the drawing of the left eye, and the left eye has nothing past its
  // own right edge. A wider render gives the game a wider horizontal angle at
  // the same pixels per degree (stereo.cpp, PrepareHeadView), so that edge is
  // outside what the right eye shows.
  if (ReadIni("one_walk", 1) != 0) {
    int guard = ReadIni("one_walk_guard_pct", 25);
    if (guard < 0) guard = 0;
    if (guard > 60) guard = 60;
    int wide = MulDiv(pixels, 100 + guard, 100);
    wide = (wide / 2) * 2;
    if (wide > 4096) wide = 4096;
    *width = wide;
  }
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

// THE SLIDERS WITH A VALUE OF THEIR OWN (kSlider). The spec is
// "low|high|step|unit". The value is on the step.
int SliderValue(int index) {
  int low = 0, high = 0, step = 0;
  ScaleRange(kSettings[index].choices, &low, &high, &step);
  int value = static_cast<int>(
      SendMessageA(g_control[index], TBM_GETPOS, 0, 0));
  if (step > 1) value = low + (value - low + step / 2) / step * step;
  if (value < low) value = low;
  if (value > high) value = high;
  return value;
}

void SaySlider(int index, int value) {
  if (g_label[index] == nullptr) return;
  // The unit is the text after the third '|'.
  const char* unit = kSettings[index].choices;
  for (int bars = 0; unit != nullptr && *unit != 0 && bars < 3; ++unit) {
    if (*unit == '|') ++bars;
    if (bars == 3) {
      ++unit;
      break;
    }
  }
  char text[128];
  _snprintf_s(text, sizeof(text), _TRUNCATE, "%s: %d%s",
              kSettings[index].label, value,
              unit != nullptr ? unit : "");
  SetWindowTextA(g_label[index], text);
}

// THE RENDERER VULKAN needs a GPU with Vulkan 1.3. The launcher asks the
// Vulkan of Windows, with no device: an instance, the list of the GPUs and
// their versions. It asks one time, because an instance takes time.
bool g_said_no_vulkan = false;

bool AskVulkan13() {
  HMODULE library = LoadLibraryA("vulkan-1.dll");
  if (library == nullptr) return false;
  bool found = false;
  PFN_vkGetInstanceProcAddr get_proc =
      reinterpret_cast<PFN_vkGetInstanceProcAddr>(
          GetProcAddress(library, "vkGetInstanceProcAddr"));
  PFN_vkCreateInstance create =
      get_proc != nullptr
          ? reinterpret_cast<PFN_vkCreateInstance>(
                get_proc(nullptr, "vkCreateInstance"))
          : nullptr;
  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Black & White VR Mod launcher";
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  info.pApplicationInfo = &app;
  VkInstance instance = VK_NULL_HANDLE;
  if (create != nullptr &&
      create(&info, nullptr, &instance) == VK_SUCCESS) {
    PFN_vkEnumeratePhysicalDevices enumerate =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
            get_proc(instance, "vkEnumeratePhysicalDevices"));
    PFN_vkGetPhysicalDeviceProperties properties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            get_proc(instance, "vkGetPhysicalDeviceProperties"));
    PFN_vkDestroyInstance destroy = reinterpret_cast<PFN_vkDestroyInstance>(
        get_proc(instance, "vkDestroyInstance"));
    if (enumerate != nullptr && properties != nullptr) {
      VkPhysicalDevice devices[16];
      uint32_t count = 16;
      if (enumerate(instance, &count, devices) >= 0) {
        for (uint32_t i = 0; i < count && i < 16; ++i) {
          VkPhysicalDeviceProperties p;
          properties(devices[i], &p);
          if (p.apiVersion >= VK_API_VERSION_1_3) found = true;
        }
      }
    }
    if (destroy != nullptr) destroy(instance, nullptr);
  }
  FreeLibrary(library);
  return found;
}

bool HasVulkan13() {
  static int s_answer = -1;
  if (s_answer < 0) s_answer = AskVulkan13() ? 1 : 0;
  return s_answer != 0;
}

// THE FALL BACK OF THE RENDERER VULKAN (docs/backend.md). The mod writes
// this file beside the game before it uses Vulkan, and the first picture
// deletes it. When it is there at a start, the game uses dxwrapper. A save
// deletes it, so the next start with Vulkan tries Vulkan again.
const char kVulkanPendingFile[] = "bwvr-vulkan-pending.txt";
// bwvr.ini has backend=vulkan and the file is there, so the row "Renderer"
// shows Direct3D 9 (LoadAll).
bool g_vulkan_fell_back = false;

bool VulkanPendingThere() { return FileThere(g_game_dir, kVulkanPendingFile); }

// --- the status line: the checks before a start ----------------------------

// The text is green with a tick, or red with a warning sign
// (WM_CTLCOLORSTATIC).
bool g_status_ok = true;
// The tooltip of the status line: each check and its result.
char g_status_tip[2048] = "";

void SetStatusTip(const char* text) {
  strcpy_s(g_status_tip, sizeof(g_status_tip), text);
  if (g_tips == nullptr || g_status == nullptr) return;
  TOOLINFOA tip = {0};
  tip.cbSize = sizeof(tip);
  tip.hwnd = g_window;
  tip.uId = reinterpret_cast<UINT_PTR>(g_status);
  tip.lpszText = g_status_tip;
  SendMessageA(g_tips, TTM_UPDATETIPTEXTA, 0, reinterpret_cast<LPARAM>(&tip));
}

// The font of the launcher has no tick and no warning sign. Windows takes
// them from a font that it links to it (Segoe UI Symbol).
void SayStatus(bool ok, const char* text) {
  g_status_ok = ok;
  wchar_t wide[1024];
  wide[0] = ok ? 0x2713 : 0x26A0;
  wide[1] = L' ';
  if (MultiByteToWideChar(CP_ACP, 0, text, -1, wide + 2, 1022) == 0) {
    wide[2] = 0;
  }
  SetWindowTextW(g_status, wide);
  InvalidateRect(g_status, nullptr, TRUE);
}

void SayStatus(const char* text) { SayStatus(false, text); }

// Adds one line to the text of a tooltip.
void TipLine(char* tip, size_t size, const char* line) {
  if (tip[0] != 0) strcat_s(tip, size, "\n");
  strcat_s(tip, size, line);
}

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

const char* FileName(const char* path) {
  const char* slash = strrchr(path, '\\');
  const char* other = strrchr(path, '/');
  if (other != nullptr && (slash == nullptr || other > slash)) slash = other;
  return slash != nullptr ? slash + 1 : path;
}

// The active OpenXR runtime of one view of the registry (KEY_WOW64_32KEY or
// KEY_WOW64_64KEY), with its variables expanded. It only reads.
bool ActiveRuntime(REGSAM view, char* out, size_t size) {
  out[0] = 0;
  HKEY key;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Khronos\\OpenXR\\1", 0,
                    KEY_READ | view, &key) != ERROR_SUCCESS) {
    return false;
  }
  char raw[MAX_PATH] = {0};
  DWORD bytes = sizeof(raw) - 1;
  DWORD type = 0;
  const LONG got = RegQueryValueExA(key, "ActiveRuntime", nullptr, &type,
                                    reinterpret_cast<BYTE*>(raw), &bytes);
  RegCloseKey(key);
  if (got != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
      raw[0] == 0) {
    return false;
  }
  if (ExpandEnvironmentStringsA(raw, out, static_cast<DWORD>(size)) == 0) {
    strcpy_s(out, size, raw);
  }
  return true;
}

// The machine of a DLL from its PE header, or 0 when it cannot be read.
WORD DllMachine(const char* path) {
  FILE* f = nullptr;
  if (fopen_s(&f, path, "rb") != 0 || f == nullptr) return 0;
  unsigned char head[1024] = {0};
  const size_t got = fread(head, 1, sizeof(head), f);
  fclose(f);
  if (got < 64 || head[0] != 'M' || head[1] != 'Z') return 0;
  DWORD pe = 0;
  memcpy(&pe, head + 0x3C, sizeof(pe));
  if (pe + 6 > got || memcmp(head + pe, "PE\0\0", 4) != 0) return 0;
  WORD machine = 0;
  memcpy(&machine, head + pe + 4, sizeof(machine));
  return machine;
}

// The value of "library_path" in the manifest of an API layer, as a full
// path. False when the manifest has none, or names a DLL with no folder
// (Windows then looks for it in its own folders).
bool LayerLibrary(const char* manifest, char* out, size_t size) {
  FILE* f = nullptr;
  if (fopen_s(&f, manifest, "rb") != 0 || f == nullptr) return false;
  static char text[65536];
  const size_t got = fread(text, 1, sizeof(text) - 1, f);
  fclose(f);
  text[got] = 0;
  const char* at = strstr(text, "\"library_path\"");
  if (at == nullptr) return false;
  at = strchr(at + 14, ':');
  if (at == nullptr) return false;
  at = strchr(at, '"');
  if (at == nullptr) return false;
  ++at;
  char path[MAX_PATH];
  size_t n = 0;
  for (; *at != 0 && *at != '"' && n + 1 < sizeof(path); ++at) {
    if (*at == '\\' && (at[1] == '\\' || at[1] == '/' || at[1] == '"')) ++at;
    path[n++] = *at == '/' ? '\\' : *at;
  }
  path[n] = 0;
  if (strchr(path, '\\') == nullptr) return false;
  const bool full = (path[0] != 0 && path[1] == ':') ||
                    (path[0] == '\\' && path[1] == '\\');
  if (full) {
    strcpy_s(out, size, path);
  } else {
    char folder[MAX_PATH];
    strcpy_s(folder, sizeof(folder), manifest);
    folder[FileName(folder) - folder] = 0;
    _snprintf_s(out, size, _TRUNCATE, "%s%s", folder, path);
  }
  return true;
}

// The first implicit OpenXR API layer for 32 bit programs that is on and
// broken: its manifest or its DLL is missing, or its DLL is for 64 bit
// programs only. The OpenXR loader of the game then stops each runtime
// (XR_ERROR_FILE_ACCESS_ERROR, xr_screen.cpp). It only reads the registry.
// `name`, `why` and `where` get the parts of the message.
bool BrokenApiLayer(char* name, char* why, char* where, size_t size,
                    int* on_count) {
  struct Place {
    HKEY root;
    const char* name;
  };
  const Place places[] = {{HKEY_LOCAL_MACHINE, "HKLM"},
                          {HKEY_CURRENT_USER, "HKCU"}};
  for (const Place& place : places) {
    HKEY key;
    if (RegOpenKeyExA(place.root,
                      "SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit", 0,
                      KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS) {
      continue;
    }
    for (DWORD i = 0;; ++i) {
      char manifest[MAX_PATH];
      DWORD name_size = sizeof(manifest);
      DWORD type = 0;
      DWORD value = 1;
      DWORD value_size = sizeof(value);
      const LONG got =
          RegEnumValueA(key, i, manifest, &name_size, nullptr, &type,
                        reinterpret_cast<BYTE*>(&value), &value_size);
      if (got == ERROR_NO_MORE_ITEMS) break;
      if (got != ERROR_SUCCESS || type != REG_DWORD || value != 0) continue;
      ++*on_count;
      const char* fault = nullptr;
      char library[MAX_PATH];
      if (GetFileAttributesA(manifest) == INVALID_FILE_ATTRIBUTES) {
        fault = "its file is missing";
      } else if (LayerLibrary(manifest, library, sizeof(library))) {
        if (GetFileAttributesA(library) == INVALID_FILE_ATTRIBUTES) {
          fault = "its DLL is missing";
        } else if (DllMachine(library) == IMAGE_FILE_MACHINE_AMD64) {
          fault = "its DLL is for 64 bit programs only";
        }
      }
      if (fault != nullptr) {
        strcpy_s(name, size, FileName(manifest));
        strcpy_s(why, size, fault);
        _snprintf_s(where, size, _TRUNCATE, "%s, %s", place.name, manifest);
        RegCloseKey(key);
        return true;
      }
    }
    RegCloseKey(key);
  }
  return false;
}

// The checks, in order: the files, the game, OpenXR and the renderer. The
// status line shows the first that fails, or "Ready". The tooltip shows
// each check that ran. `before` comes first in the line, for example
// "Saved.".
void CheckEverything(const char* before = nullptr) {
  char line[768] = "";
  char tip[2048] = "";
  char detail[1024];
  bool ok = false;
  if (before != nullptr && before[0] != 0) {
    strcpy_s(line, sizeof(line), before);
    strcat_s(line, sizeof(line), " ");
  }
  char runtime[MAX_PATH] = "";
  char layer[MAX_PATH] = "", fault[MAX_PATH] = "", where[MAX_PATH] = "";
  int layers_on = 0;
  const char* missing = nullptr;
  char from_env[MAX_PATH] = "";
  DWORD env = 0;
  const char* source = "the 32 bit registry";

  // Every file the mod needs at run time. A missing dxwrapper file makes the
  // mod fall back to a slow path in silence, so this check lists them.
  static const char* const kNeeded[] = {
      "ddraw.dll",
      "openxr_loader.dll",
      "dxwrapper\\ddraw.dll",
      "dxwrapper\\dxwrapper.dll",
      "dxwrapper\\dxwrapper.ini",
  };
  for (const char* name : kNeeded) {
    if (!FileThere(g_game_dir, name)) {
      missing = name;
      break;
    }
  }
  if (missing != nullptr) {
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "%s is missing from the game folder. Install the %s again.",
                missing, kProductName);
    strcat_s(line, sizeof(line), detail);
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "The files of the mod: %s is missing in %s.", missing,
                g_game_dir);
    TipLine(tip, sizeof(tip), detail);
    goto done;
  }
  TipLine(tip, sizeof(tip),
          "The files of the mod: ddraw.dll, openxr_loader.dll and the "
          "dxwrapper folder are there.");

  if (!GameBuildIsRight(detail, sizeof(detail))) {
    strcat_s(line, sizeof(line), detail);
    TipLine(tip, sizeof(tip), "The game: runblack.exe is not v1.42.");
    goto done;
  }
  TipLine(tip, sizeof(tip), "The game: runblack.exe is v1.42.");

  // OPENXR. The game is 32 bit, so its OpenXR loader reads the variable
  // XR_RUNTIME_JSON, or else the 32 bit view of the registry. A runtime that
  // is registered only for 64 bit cannot serve it. This is the most common
  // reason for a headset that shows nothing.
  env = GetEnvironmentVariableA("XR_RUNTIME_JSON", from_env, sizeof(from_env));
  if (env > 0 && env < sizeof(from_env)) {
    strcpy_s(runtime, sizeof(runtime), from_env);
    source = "the variable XR_RUNTIME_JSON";
  } else if (!ActiveRuntime(KEY_WOW64_32KEY, runtime, sizeof(runtime))) {
    char only64[MAX_PATH] = "";
    if (ActiveRuntime(KEY_WOW64_64KEY, only64, sizeof(only64))) {
      strcat_s(line, sizeof(line),
               "The OpenXR runtime is set for 64 bit programs only. The "
               "game is 32 bit. Use a runtime with 32 bit support.");
      _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                  "OpenXR: no runtime for 32 bit programs. The runtime for "
                  "64 bit programs is %s. Virtual Desktop has a runtime for "
                  "32 bit programs, and SteamVR has one from version 2.17.",
                  only64);
    } else {
      strcat_s(line, sizeof(line),
               "No OpenXR runtime is set. Start the VR app of your headset, "
               "and make it the OpenXR runtime.");
      strcpy_s(detail, sizeof(detail),
               "OpenXR: no active runtime in the registry "
               "(HKLM\\SOFTWARE\\Khronos\\OpenXR\\1, ActiveRuntime).");
    }
    TipLine(tip, sizeof(tip), detail);
    goto done;
  }
  if (GetFileAttributesA(runtime) == INVALID_FILE_ATTRIBUTES) {
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "The file of the OpenXR runtime is missing: %s. Install the "
                "VR app of your headset again.",
                FileName(runtime));
    strcat_s(line, sizeof(line), detail);
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "OpenXR: the runtime from %s is %s, and this file is not "
                "there.",
                source, runtime);
    TipLine(tip, sizeof(tip), detail);
    goto done;
  }
  _snprintf_s(detail, sizeof(detail), _TRUNCATE,
              "OpenXR: the runtime for 32 bit programs is %s (from %s).",
              runtime, source);
  TipLine(tip, sizeof(tip), detail);

  if (BrokenApiLayer(layer, fault, where, sizeof(layer), &layers_on)) {
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "The OpenXR API layer %s is broken: %s. Turn it off with the "
                "tool OpenXR API Layers GUI.",
                layer, fault);
    strcat_s(line, sizeof(line), detail);
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "OpenXR API layers for 32 bit programs: %s is on, and %s. "
                "It stops each runtime.",
                where, fault);
    TipLine(tip, sizeof(tip), detail);
    goto done;
  }
  _snprintf_s(detail, sizeof(detail), _TRUNCATE,
              "OpenXR API layers for 32 bit programs: %d on, and none is "
              "broken.",
              layers_on);
  TipLine(tip, sizeof(tip), detail);

  // THE RENDERER, as the row shows it.
  if (g_vulkan_fell_back || (g_renderer_vulkan && VulkanPendingThere())) {
    strcat_s(line, sizeof(line),
             "The last start with Vulkan failed, so the game uses Direct3D "
             "9. To try Vulkan again, set Renderer to Vulkan and save.");
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "Renderer: the file %s is in the game folder. The mod wrote "
                "it, because the last start with Vulkan stopped before its "
                "first picture, or Vulkan failed. bwvr.log of that start "
                "gives the cause.",
                kVulkanPendingFile);
    TipLine(tip, sizeof(tip), detail);
    goto done;
  }
  if (g_renderer_vulkan) {
    if (!HasVulkan13()) {
      strcat_s(line, sizeof(line),
               "Your GPU has no Vulkan 1.3. Set Renderer to Direct3D 9.");
      TipLine(tip, sizeof(tip),
              "Renderer: Vulkan, but no GPU has Vulkan 1.3.");
      goto done;
    }
    TipLine(tip, sizeof(tip), "Renderer: Vulkan. A GPU has Vulkan 1.3.");
  } else {
    TipLine(tip, sizeof(tip), "Renderer: Direct3D 9.");
  }

  _snprintf_s(detail, sizeof(detail), _TRUNCATE, "Ready. OpenXR runtime: %s",
              FileName(runtime));
  strcat_s(line, sizeof(line), detail);
  ok = true;

done:
  SayStatus(ok, line);
  SetStatusTip(tip);
}

// --- reading and writing the whole table -----------------------------------

int ChoiceValue(const char* choices, int index);
int ChoiceIndex(const char* choices, int value);

void LayOutControls(bool first);

// The rows that only the renderer Direct3D 9 uses are not in the table with
// the renderer Vulkan (RowShown). `first`: the window goes to the middle of
// the work area.
void UpdateRendererRows(bool first) {
  bool vulkan = false;
  for (int i = 0; i < kSettingCount; ++i) {
    if (kSettings[i].kind == kRenderer && g_control[i] != nullptr) {
      vulkan = SendMessageA(g_control[i], CB_GETCURSEL, 0, 0) == 0;
    }
  }
  if (vulkan == g_renderer_vulkan && !first) return;
  g_renderer_vulkan = vulkan;
  LayOutControls(first);
  // The check of the renderer follows the row.
  if (!first) CheckEverything();
}

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
      case kSlider: {
        int low = 0, high = 0, step = 0;
        ScaleRange(s.choices, &low, &high, &step);
        int value = ReadIni(s.key, s.fallback);
        if (value < low) value = low;
        if (value > high) value = high;
        SendMessageA(g_control[i], TBM_SETPOS, TRUE, value);
        SaySlider(i, value);
        break;
      }
      case kRenderer: {
        // No key: Vulkan, when the GPU has Vulkan 1.3. The save writes it.
        // With the file of the fall back the game uses Direct3D 9, and the
        // row shows it. A save with Vulkan tries Vulkan again.
        char text[MAX_PATH] = {0};
        ReadIniText(s.key, text, sizeof(text));
        bool vulkan = text[0] == 0 ? HasVulkan13()
                                   : _stricmp(text, "vulkan") == 0;
        g_vulkan_fell_back =
            _stricmp(text, "vulkan") == 0 && VulkanPendingThere();
        if (g_vulkan_fell_back) vulkan = false;
        SendMessageA(g_control[i], CB_SETCURSEL, vulkan ? 0 : 1, 0);
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
    "village_test_workshop", "pointer_leave_cursor", "cursor_trace",
    "exit_test_frame", "crash_test_frame", "target_check_test_frame",
    "registry_test_guard", "intro_skip_test_frame",
    "fake_controller_play_frame", "fake_controller_play_after",
    "menu_skip_test_frame", "intro_motion_test_seconds",
    "intro_push_test_px",
    // More keys for desktop tests: each key that the mod reads with _test in
    // its name, the turns and the places of the scripted controller, and the
    // keys of a self test, of a probe and of test pictures.
    "aim_test_keys", "aim_test_touch", "audio_stop_wait_test",
    "audio_test_bank", "audio_test_path", "audio_test_solo",
    "audio_test_step_ms", "audio_test_tag", "audio_test_tag_bank",
    "audio_test_voice", "bulk_test_influence", "bulk_test_no_keep",
    "command_test_class", "creature_command_test",
    "cutscene_depth_test_off", "cutscene_depth_test_on",
    "cutscene_window_test_off", "cutscene_window_test_on",
    "cutscene_window_capture", "far_test_aim_blend_pct",
    "far_test_aim_class", "far_test_aim_toward", "far_test_creature",
    "far_test_forest", "far_test_influence", "far_test_least_cm",
    "far_test_no_trees", "far_test_scroll", "far_test_thrown",
    "far_test_tree", "far_test_aim_blends", "far_test_aim_pair",
    "far_test_quest", "far_test_take_gone", "bubble_card_test",
    "bubble_card_test_frame", "one_walk_marks_test_frame",
    "eye_clean_test", "eye_clean_test_frame",
    "frame_time_test_frame", "frame_time_test_readers",
    "frame_time_test_readers_frames", "god_zoom_about_test",
    "god_zoom_test_toggle",
    "hand_feel_test_toggle", "hand_model_test_look", "hand_model_test_swap",
    "help_test", "hint_counts_test_file", "land_light_test",
    "near_test_flips", "spawn_test_flag",
    "spawn_test_temple",
    "spawn_test_toggle", "spawn_test_x", "spawn_test_z",
    "spirit_point_world_test", "spirit_world_size_test",
    "subtitle_card_test", "subtitle_selftest",
    "subtitle_read_ahead_test_seconds", "profile_sample",
    "profile_sample_thread", "profile_sample_dump", "profile_gpu",
    "hang_watch", "gpu_flush_ab", "ab_test", "d3d7_count", "fast_transform_check",
    "test_cores", "vk_scene", "vk_scene_test_frame", "vk_scene_follow", "vk_eyes", "xr_vulkan_upload", "vk_panel", "vk_only", "vk_only_test", "vk_upload_queue", "vk_scene_reread_test", "ddraw_census", "test_smt", "test_spin_walk_us", "test_spin_idle_us",
    "test_wake_records", "vulkan_fallback_test",
    "render_thread_test_frame",
    "temple_test_camera_trace",
    "temple_test_click_frame", "temple_test_door_on",
    "temple_test_enter_frame", "temple_test_flight_frame",
    "temple_test_follow_frame", "temple_test_scan", "temple_test_scan_frame",
    "temple_test_scan_hall", "temple_test_taps_frame", "temple_test_targets",
    "text_click_test", "text_click_test_text", "fake_controller_rots",
    "fake_controller_turns", "seek_plan", "seek_go", "seek_picture",
    "seek_class", "seek_bulk", "seek_forest", "seek_scroll", "seek_tap",
    "seek_thrown", "intro_probe",
    // Settings that the launcher offered before. Each has one right answer
    // in a headset, and the mod's default is that answer. The other
    // settings that are not offered now (the sizes and the depth of the
    // interface, the hint, the zoom step) stay in the ini as they are, so a
    // value that a person wrote by hand is kept.
    "controller_hand", "controller_left_handed", "script_camera",
    "instant_jumps",
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
      case kSlider:
        WriteIni(s.key, SliderValue(i));
        break;
      case kRenderer: {
        // Vulkan only with a GPU that has Vulkan 1.3: else the game would
        // have no picture.
        bool vulkan =
            SendMessageA(g_control[i], CB_GETCURSEL, 0, 0) == 0;
        if (vulkan && !HasVulkan13()) {
          vulkan = false;
          SendMessageA(g_control[i], CB_SETCURSEL, 1, 0);
          g_said_no_vulkan = true;
        }
        WriteIniText(s.key, vulkan ? "vulkan" : "dxwrapper\\ddraw.dll");
        // The renderer is the choice of the player now. With Vulkan the
        // next start tries Vulkan again, and with Direct3D 9 the file does
        // nothing.
        char pending[MAX_PATH];
        _snprintf_s(pending, sizeof(pending), _TRUNCATE, "%s\\%s",
                    g_game_dir, kVulkanPendingFile);
        DeleteFileA(pending);
        g_vulkan_fell_back = false;
        break;
      }
      default:
        break;
    }
  }
  // A PC with no Vulkan 1.3 has the renderer Direct3D 9 now, and its rows.
  UpdateRendererRows(false);
  // These are not offered as choices, because getting them wrong stops the mod
  // working at all and there is only one right answer. The renderer Vulkan
  // has no bridge of Direct3D 9.
  if (g_renderer_vulkan) {
    WritePrivateProfileStringA("bwvr", "gpu_bridge", nullptr, g_ini_path);
  } else {
    WriteIni("gpu_bridge", 1);
  }
  WriteIni("vr", 1);
  WriteIni("stereo", 1);
  WriteIni("head_camera", 1);
  // Settings that have one right answer and are not offered. Removing them
  // means an old ini cannot keep a wrong value. The mod's own default is the
  // right answer for each, and the keys still work for testing.
  for (const char* key : kRetiredKeys) {
    WritePrivateProfileStringA("bwvr", key, nullptr, g_ini_path);
  }
  // The renderer Vulkan takes the anisotropy of the game, so dxwrapper.ini
  // matters only with Direct3D 9.
  if (!g_renderer_vulkan) WriteBackendAnisotropy(ReadIni("anisotropy", 16));
  SaveGameDisplayOnce();
  // The game reads these from the registry. If they are missing, it starts
  // full screen at the size of the desktop, in 16 bit color. Another install
  // of the game can remove them. The mod needs a window at the size above.
  WriteRegistryInt("FullScreen", 0);
  WriteRegistryInt("ScreenD", 32);
}

// Starts runblack.exe. With `process`, the caller gets the handle of the game
// and closes it.
bool StartGame(const char* parameters, HANDLE* process) {
  char exe[MAX_PATH];
  _snprintf_s(exe, sizeof(exe), _TRUNCATE, "%s\\runblack.exe", g_game_dir);
  SHELLEXECUTEINFOA info = {sizeof(info)};
  info.lpFile = exe;
  info.lpDirectory = g_game_dir;
  info.lpParameters = parameters;
  info.nShow = SW_SHOWNORMAL;
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  if (!ShellExecuteExA(&info)) return false;
  if (process != nullptr) {
    *process = info.hProcess;
  } else if (info.hProcess != nullptr) {
    CloseHandle(info.hProcess);
  }
  return true;
}

void Play(bool new_game) {
  SaveAll();
  if (StartGame(new_game ? "-NEWGAME" : nullptr, nullptr)) {
    PostMessageA(g_window, WM_CLOSE, 0, 0);
  } else {
    SayStatus("Could not start runblack.exe.");
  }
}

// THE GAME WITHOUT VR.
//
// "Play, no VR" starts the game with vr=0, so the mod does nothing, full
// screen at the size of the main monitor. These values are for one game only:
// a start of runblack.exe without the launcher must find the values for the
// headset. So the launcher stays, with no window, until the game ends, and
// then it writes the values for the headset again (WinMain). It writes them
// after the end, because the game can write its display values when it ends.
HANDLE g_plain_game = nullptr;
int g_headset_screen_w = 0;
int g_headset_screen_h = 0;

// The renderer of the headset: the plain game has dxwrapper.
char g_headset_backend[MAX_PATH] = {0};

void WriteHeadsetStart() {
  WriteIni("vr", 1);
  if (g_headset_backend[0] != 0) WriteIniText("backend", g_headset_backend);
  WriteRegistryInt("ScreenW", g_headset_screen_w);
  WriteRegistryInt("ScreenH", g_headset_screen_h);
  WriteRegistryInt("FullScreen", 0);
}

void PlayNoVr() {
  SaveAll();
  ReadIniText("backend", g_headset_backend, sizeof(g_headset_backend));
  WriteIniText("backend", "dxwrapper\\ddraw.dll");
  g_headset_screen_w = ReadRegistryInt("ScreenW", 0);
  g_headset_screen_h = ReadRegistryInt("ScreenH", 0);
  WriteIni("vr", 0);
  WriteRegistryInt("ScreenW", GetSystemMetrics(SM_CXSCREEN));
  WriteRegistryInt("ScreenH", GetSystemMetrics(SM_CYSCREEN));
  WriteRegistryInt("FullScreen", 1);
  if (StartGame(nullptr, &g_plain_game)) {
    PostMessageA(g_window, WM_CLOSE, 0, 0);
  } else {
    WriteHeadsetStart();
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
// The rows that the renderer Vulkan does not use. With Vulkan they are not
// in the table.
bool RowShown(int index) {
  const char* key = kSettings[index].key;
  if (!g_renderer_vulkan || key == nullptr) return true;
  return strcmp(key, "d3d9_backend") != 0 && strcmp(key, "xr_vulkan") != 0;
}

int GroupRows(int group) {
  int rows = 1;
  for (int i = group + 1; i < kSettingCount; ++i) {
    if (kSettings[i].kind == kGroup) break;
    if (RowShown(i)) ++rows;
  }
  return rows;
}

// The heart on the "Support" button: a red heart with soft edges, as an icon
// of `size` pixels. The form is the curve (x^2 + y^2 - 1)^3 = x^2 y^3, and
// each pixel gets its alpha from 16 samples.
HICON MakeHeartIcon(int size) {
  if (size < 8) size = 8;
  if (size > 64) size = 64;
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP color =
      CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (color == nullptr) return nullptr;
  DWORD* pixel = static_cast<DWORD*>(bits);
  const int sub = 4;
  for (int py = 0; py < size; ++py) {
    for (int px = 0; px < size; ++px) {
      int inside = 0;
      for (int sy = 0; sy < sub; ++sy) {
        for (int sx = 0; sx < sub; ++sx) {
          // The curve is 2.3 wide and goes from -1.0 to 1.24 in y, so the
          // picture is 2.5 across and its middle is at y = 0.12.
          const double x = ((px + (sx + 0.5) / sub) / size - 0.5) * 2.5;
          const double y = (0.5 - (py + (sy + 0.5) / sub) / size) * 2.5 + 0.12;
          const double a = x * x + y * y - 1.0;
          if (a * a * a - x * x * y * y * y <= 0.0) ++inside;
        }
      }
      const DWORD alpha = static_cast<DWORD>(inside * 255 / (sub * sub));
      *pixel++ = (alpha << 24) | (224u << 16) | (49u << 8) | 72u;
    }
  }
  // An icon needs a mask. All of its bits are 0, so the alpha decides.
  static const unsigned char kNoMask[8 * 64] = {0};
  HBITMAP mask = CreateBitmap(size, size, 1, 1, kNoMask);
  ICONINFO parts = {TRUE, 0, 0, mask, color};
  HICON icon = CreateIconIndirect(&parts);
  DeleteObject(mask);
  DeleteObject(color);
  return icon;
}

// The sizes of the table.
struct TableSizes {
  int pad, row, label_w, field_w, col_w;
};

TableSizes Sizes() {
  TableSizes t;
  t.pad = Scaled(12);
  t.row = Scaled(26);
  t.label_w = Scaled(210);
  // Wide enough for the longest choice, "Grip of the other hand".
  t.field_w = Scaled(160);
  t.col_w = t.label_w + t.field_w + t.pad;
  return t;
}

// Puts each shown row, the buttons and the status line in place, and sizes
// the window to them. The table goes in columns, and a new column starts when
// the current gets tall. This is what lets the table grow without the window
// growing past the screen. The first call puts the window in the middle of
// the work area, and a later call keeps its place.
void LayOutControls(bool first) {
  const TableSizes t = Sizes();
  const int pad = t.pad;
  const int row = t.row;
  const int label_w = t.label_w;
  const int field_w = t.field_w;
  const int col_w = t.col_w;
  // The tallest a column may get. Rows and pixels are interchangeable
  // everywhere below, so this number also caps the height of the table.
  const int max_rows = g_max_rows;
  const UINT move = SWP_NOZORDER | SWP_NOACTIVATE;

  int column = 0;
  int used = 0;
  int tallest = 0;

  for (int i = 0; i < kSettingCount; ++i) {
    const Setting& s = kSettings[i];
    if (!RowShown(i)) {
      if (g_label[i] != nullptr) ShowWindow(g_label[i], SW_HIDE);
      if (g_control[i] != nullptr) ShowWindow(g_control[i], SW_HIDE);
      continue;
    }
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
    used += 1;
    if (used > tallest) tallest = used;

    if (s.kind == kGroup) {
      SetWindowPos(g_heading[i], nullptr, x, y + Scaled(6), col_w,
                   row - Scaled(4), move);
      continue;
    }
    if (g_label[i] != nullptr) {
      // A slider says its value in its own label, so it needs the whole width
      // rather than the label column.
      const int width =
          s.kind == kScale || s.kind == kSlider ? col_w : label_w;
      SetWindowPos(g_label[i], nullptr, x, y + Scaled(4), width,
                   row - Scaled(6), move);
      ShowWindow(g_label[i], SW_SHOW);
    }
    HWND control = g_control[i];
    if (control == nullptr) continue;
    switch (s.kind) {
      case kBool:
        SetWindowPos(control, nullptr, x, y + Scaled(2), col_w,
                     row - Scaled(4), move);
        break;
      case kInt:
      case kScale:
      case kSlider:
        SetWindowPos(control, nullptr, x + label_w, y, field_w,
                     row - Scaled(4), move);
        break;
      case kChoice:
      case kValues:
      case kRenderer:
        SetWindowPos(control, nullptr, x + label_w, y, field_w, row * 8,
                     move);
        break;
      default:
        break;
    }
    ShowWindow(control, SW_SHOW);
  }

  // The buttons are a block below the last column, as wide as its fields.
  // The two ways to play in VR are in the first row, "Save Config" and "Play,
  // no VR" in the second, and "Support" has the third row to itself. When a
  // column before the last is taller, the block is at the bottom of the
  // window.
  const int button_h = Scaled(28);
  const int button_gap = Scaled(8);
  const int columns = column + 1;
  const int width = columns * (col_w + pad);
  const int block_w = label_w + field_w;
  const int block_h = button_h * 3 + button_gap * 2;
  const int half_w = (block_w - button_gap) / 2;
  const int left_x = pad + column * (col_w + pad);
  const int right_x = left_x + block_w - half_w;
  const int table_bottom = pad + tallest * row;
  int row1_y = pad + used * row + Scaled(10);
  if (row1_y < table_bottom - block_h) row1_y = table_bottom - block_h;
  const int row2_y = row1_y + button_h + button_gap;
  const int row3_y = row2_y + button_h + button_gap;
  SetWindowPos(g_buttons[kButtonPlay], nullptr, left_x, row1_y, half_w,
               button_h, move);
  SetWindowPos(g_buttons[kButtonFresh], nullptr, right_x, row1_y, half_w,
               button_h, move);
  SetWindowPos(g_buttons[kButtonSave], nullptr, left_x, row2_y, half_w,
               button_h, move);
  SetWindowPos(g_buttons[kButtonPlain], nullptr, right_x, row2_y, half_w,
               button_h, move);
  SetWindowPos(g_buttons[kButtonSupport], nullptr, left_x, row3_y, block_w,
               button_h, move);
  // The status line is below the table and the buttons, over the whole
  // window, and has room for two lines of text.
  const int block_bottom = row3_y + button_h;
  const int status_y =
      (block_bottom > table_bottom ? block_bottom : table_bottom) + Scaled(8);
  const int status_h = Scaled(32);
  const int height = status_y + status_h + Scaled(6);
  SetWindowPos(g_status, nullptr, pad, status_y, width - pad * 2, status_h,
               move);

  RECT want = {0, 0, width, height};
  AdjustWindowRect(&want, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);
  const int frame_w = want.right - want.left;
  const int frame_h = want.bottom - want.top;
  if (first) {
    RECT work = {0, 0, 0, 0};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    SetWindowPos(g_window, nullptr,
                 work.left + ((work.right - work.left) - frame_w) / 2,
                 work.top + ((work.bottom - work.top) - frame_h) / 2, frame_w,
                 frame_h, SWP_NOZORDER);
  } else {
    SetWindowPos(g_window, nullptr, 0, 0, frame_w, frame_h,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    InvalidateRect(g_window, nullptr, TRUE);
  }
}

// Makes a control for each row, the buttons and the status line, and lays
// them out (LayOutControls).
void BuildControls() {
  const TableSizes t = Sizes();
  const int row = t.row;
  int id = 100;

  for (int i = 0; i < kSettingCount; ++i) {
    const Setting& s = kSettings[i];
    g_heading[i] = nullptr;
    g_label[i] = nullptr;
    g_control[i] = nullptr;
    if (s.kind == kGroup) {
      HWND heading = CreateWindowExA(0, "STATIC", s.label,
                                     WS_CHILD | WS_VISIBLE, 0, 0, t.col_w,
                                     row - Scaled(4), g_window, nullptr,
                                     nullptr, nullptr);
      SendMessageA(heading, WM_SETFONT, reinterpret_cast<WPARAM>(g_bold), TRUE);
      g_heading[i] = heading;
      continue;
    }

    if (s.kind != kBool) {
      HWND label = CreateWindowExA(0, "STATIC", s.label,
                                   WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0,
                                   t.label_w, row - Scaled(6), g_window,
                                   nullptr, nullptr, nullptr);
      SendMessageA(label, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
      AddTip(label, s.help);
      g_label[i] = label;
    }

    HWND control = nullptr;
    switch (s.kind) {
      case kBool:
        control = CreateWindowExA(
            0, "BUTTON", s.label, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,
            0, t.col_w, row - Scaled(4), g_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), nullptr,
            nullptr);
        break;
      case kInt:
        control = CreateWindowExA(
            WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_RIGHT, 0, 0, t.field_w,
            row - Scaled(4), g_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), nullptr,
            nullptr);
        break;
      case kScale:
      case kSlider: {
        // TBS_NOTICKS keeps the slider inside one ordinary row, which matters
        // because the whole layout counts rows rather than pixels. A taller
        // control would mean the column break and the window height stopped
        // agreeing with each other.
        int low = 0, high = 0, step = 0;
        ScaleRange(s.choices, &low, &high, &step);
        control = CreateWindowExA(
            0, TRACKBAR_CLASS, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0, t.field_w,
            row - Scaled(4), g_window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id++)), nullptr,
            nullptr);
        SendMessageA(control, TBM_SETRANGE, TRUE, MAKELONG(low, high));
        SendMessageA(control, TBM_SETPAGESIZE, 0, step);
        SendMessageA(control, TBM_SETLINESIZE, 0, step);
        break;
      }
      case kChoice:
      case kValues:
      case kRenderer:
        control = CreateWindowExA(
            0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
            t.field_w, row * 8, g_window,
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
  }

  // The buttons are made in the order of the Tab key. LayOutControls gives
  // them their places and sizes.
  g_buttons[kButtonPlay] = CreateWindowExA(
      0, "BUTTON", "Play in VR", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0,
      0, 0, 0, g_window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
  g_buttons[kButtonFresh] = CreateWindowExA(
      0, "BUTTON", "Play, new game", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
      g_window, reinterpret_cast<HMENU>(2), nullptr, nullptr);
  g_buttons[kButtonSave] = CreateWindowExA(
      0, "BUTTON", "Save Config", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_window,
      reinterpret_cast<HMENU>(3), nullptr, nullptr);
  g_buttons[kButtonPlain] = CreateWindowExA(
      0, "BUTTON", "Play, no VR", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_window,
      reinterpret_cast<HMENU>(5), nullptr, nullptr);
  // The space at the start of the text keeps the text off the heart.
  g_buttons[kButtonSupport] = CreateWindowExA(
      0, "BUTTON", " Support", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_window,
      reinterpret_cast<HMENU>(4), nullptr, nullptr);
  // The heart shows to the left of the text, at the height of a capital.
  HICON heart = MakeHeartIcon(Scaled(14));
  if (heart != nullptr) {
    SendMessageA(g_buttons[kButtonSupport], BM_SETIMAGE, IMAGE_ICON,
                 reinterpret_cast<LPARAM>(heart));
  }
  // SS_NOPREFIX, because the product name holds an ampersand and a static
  // control would otherwise read it as an access key. A window of Unicode,
  // for the tick and the warning sign (SayStatus). SS_NOTIFY gives the
  // mouse to the tooltip.
  g_status = CreateWindowExW(
      0, L"STATIC", L"",
      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_NOTIFY, 0, 0, 0, 0,
      g_window, nullptr, nullptr, nullptr);
  for (HWND button : g_buttons) {
    SendMessageA(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
  }
  SendMessageA(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
  AddTip(g_status, g_status_tip);
  AddTip(g_buttons[kButtonFresh],
         "Start a new game on the first island, skipping the menu.");
  AddTip(g_buttons[kButtonSave],
         "Write the settings without starting the game.");
  AddTip(g_buttons[kButtonPlain],
         "Start the game on the monitor, without the headset. The game "
         "is full screen, at the size of the desktop, and the mod does "
         "nothing in it.");
  AddTip(g_buttons[kButtonSupport],
         "Opens " BWVR_SUPPORT_URL " in your browser. The mod is "
         "free, and it stays free.");
  LayOutControls(true);
}

LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_COMMAND:
      if (HIWORD(w) == CBN_SELCHANGE) UpdateRendererRows(false);
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
          } else if (g_said_no_vulkan) {
            CheckEverything("Saved. Your GPU has no Vulkan 1.3, so the "
                            "renderer stays Direct3D 9.");
          } else {
            CheckEverything("Saved.");
          }
          return 0;
        case 4:
          // The support page. It opens in the player's own browser.
          ShellExecuteA(nullptr, "open", BWVR_SUPPORT_URL, nullptr, nullptr,
                        SW_SHOWNORMAL);
          return 0;
        case 5:
          PlayNoVr();
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
        if (g_control[i] != from) continue;
        if (kSettings[i].kind == kScale) {
          SayScale(i, static_cast<int>(SendMessageA(from, TBM_GETPOS, 0, 0)));
        } else if (kSettings[i].kind == kSlider) {
          SaySlider(i, SliderValue(i));
        }
        break;
      }
      return 0;
    }
    case WM_CTLCOLORSTATIC:
      SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
      // The status line: green when each check passes, else red.
      if (reinterpret_cast<HWND>(l) == g_status) {
        SetTextColor(reinterpret_cast<HDC>(w),
                     g_status_ok ? RGB(0, 128, 0) : RGB(200, 0, 0));
      }
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

// A PICTURE OF THE WINDOW FOR A TEST.
//
// "-shot <file>" at the end of the command line makes the launcher write its
// window to a BMP file and end. A person who uses the PC does not see the
// window: it is outside the screen, it has no button on the taskbar, and it
// does not take the focus. It must show there, because a list draws its text
// only in a window that shows. The picture is the client area, which each
// control draws into a bitmap (WM_PRINT). The launcher reads the settings
// for it and writes nothing but the file.
bool WriteShot(const char* path) {
  SetWindowLongA(g_window, GWL_EXSTYLE,
                 GetWindowLongA(g_window, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
  SetWindowPos(g_window, nullptr, -32000, -32000, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  ShowWindow(g_window, SW_SHOWNOACTIVATE);
  UpdateWindow(g_window);

  RECT client = {0, 0, 0, 0};
  GetClientRect(g_window, &client);
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = client.right;
  info.bmiHeader.biHeight = client.bottom;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap =
      CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bitmap == nullptr) {
    DeleteDC(dc);
    return false;
  }
  HGDIOBJ before = SelectObject(dc, bitmap);
  FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
  // WM_PRINT draws from the corner of the frame of the window. Move the
  // origin, so that the corner of the client area is the corner of the
  // picture.
  RECT frame = {0, 0, 0, 0};
  GetWindowRect(g_window, &frame);
  POINT corner = {0, 0};
  ClientToScreen(g_window, &corner);
  SetViewportOrgEx(dc, frame.left - corner.x, frame.top - corner.y, nullptr);
  SendMessageA(g_window, WM_PRINT, reinterpret_cast<WPARAM>(dc),
               PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
  GdiFlush();

  const DWORD size =
      static_cast<DWORD>(client.right) * static_cast<DWORD>(client.bottom) * 4;
  BITMAPFILEHEADER header = {};
  header.bfType = 0x4D42;
  header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
  header.bfSize = header.bfOffBits + size;
  bool written = false;
  FILE* file = nullptr;
  if (fopen_s(&file, path, "wb") == 0 && file != nullptr) {
    written = fwrite(&header, sizeof(header), 1, file) == 1 &&
              fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, file) == 1 &&
              fwrite(bits, 1, size, file) == size;
    fclose(file);
  }
  SelectObject(dc, before);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return written;
}

}  // namespace

// A TEST AID: "--picture <file>" saves a picture of the window as a BMP
// file and ends. "--game <folder>" names the folder of the game when the
// launcher is not in it. The window is outside the screen, at the bottom of the
// order of the windows, and is never active. Nothing is written to the ini.
bool SavePicture(const char* path) {
  RECT r;
  GetWindowRect(g_window, &r);
  const int w = r.right - r.left, h = r.bottom - r.top;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = h;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap =
      CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (bitmap == nullptr) {
    DeleteDC(dc);
    return false;
  }
  HGDIOBJ old = SelectObject(dc, bitmap);
  const BOOL printed = PrintWindow(g_window, dc, 2);  // PW_RENDERFULLCONTENT
  SelectObject(dc, old);
  bool ok = false;
  FILE* file = nullptr;
  if (printed && fopen_s(&file, path, "wb") == 0 && file != nullptr) {
    BITMAPFILEHEADER head = {};
    head.bfType = 0x4D42;
    head.bfOffBits = sizeof(head) + sizeof(info.bmiHeader);
    head.bfSize = head.bfOffBits + static_cast<DWORD>(w) * h * 4;
    fwrite(&head, sizeof(head), 1, file);
    fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, file);
    fwrite(bits, static_cast<size_t>(w) * h * 4, 1, file);
    fclose(file);
    ok = true;
  }
  DeleteObject(bitmap);
  DeleteDC(dc);
  return ok;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR command, int) {
  // The words of a test: "-rows <n>", then "-shot <file>" to the end of the
  // line.
  const char* shot = strstr(command, "-shot ");
  if (shot != nullptr) shot += 6;
  const char* rows = strstr(command, "-rows ");
  if (rows != nullptr && atoi(rows + 6) >= 4) g_max_rows = atoi(rows + 6);

  SetProcessDPIAware();
  HDC screen = GetDC(nullptr);
  if (screen != nullptr) {
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
  }

  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_STANDARD_CLASSES |
                                                         ICC_BAR_CLASSES};
  InitCommonControlsEx(&controls);

  const char* picture = nullptr;
  const char* game = nullptr;
  for (int i = 1; i + 1 < __argc; ++i) {
    if (strcmp(__argv[i], "--picture") == 0) picture = __argv[i + 1];
    if (strcmp(__argv[i], "--game") == 0) game = __argv[i + 1];
  }
  bool found = FindGame();
  if (!found && (picture != nullptr || shot != nullptr) && game != nullptr &&
      FileThere(game, "runblack.exe")) {
    TakeGameDir(game);
    found = true;
  }
  if (!found) {
    if (picture != nullptr) return 1;
    if (shot == nullptr) {
      MessageBoxA(nullptr,
                  "Cannot find runblack.exe. Install the " BWVR_PRODUCT_NAME
                  " in the folder that holds your Black & White game.",
                  kProductName, MB_ICONERROR | MB_OK);
      return 1;
    }
    // A picture of -shot needs no game. The table then shows its defaults,
    // and the status line names a file that is missing. "--game <folder>"
    // gives it a game.
    TakeGameDir(".");
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
      picture != nullptr ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0,
      kWindowClass, kTitle, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME &
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
  UpdateRendererRows(true);
  CheckEverything();
  if (picture != nullptr) {
    SetWindowPos(g_window, HWND_BOTTOM, -30000, -30000, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    // Each control paints itself now, and the messages of the paint go out
    // before the picture.
    RedrawWindow(g_window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    const DWORD until = GetTickCount() + 300;
    MSG pending;
    while (GetTickCount() < until) {
      while (PeekMessageA(&pending, nullptr, 0, 0, PM_REMOVE)) {
        DispatchMessageA(&pending);
      }
      Sleep(10);
    }
    const bool saved = SavePicture(picture);
    DestroyWindow(g_window);
    return saved ? 0 : 1;
  }
  if (shot != nullptr) return WriteShot(shot) ? 0 : 1;
  ShowWindow(g_window, SW_SHOW);
  UpdateWindow(g_window);

  MSG message;
  while (GetMessageA(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageA(g_window, &message)) {
      TranslateMessage(&message);
      DispatchMessageA(&message);
    }
  }
  // "Play, no VR" started the game. When that game ends, the values for the
  // headset come back.
  if (g_plain_game != nullptr) {
    WaitForSingleObject(g_plain_game, INFINITE);
    CloseHandle(g_plain_game);
    WriteHeadsetStart();
  }
  return 0;
}
