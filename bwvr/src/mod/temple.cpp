// THE TEMPLE WITH THE CONTROLLERS (docs/controllers.md, "The Temple").
//
// In the Temple the game draws a room from a camera of its own, and its Hand
// has the state of the Temple (HandStateCitadel): the Hand is at the pixel of
// the pointer, in front of the thing that the game finds under that pixel.
// The real hand cannot be that Hand. So in a room the pointing hand points
// with a ray, as it does in a dialog box, and the direct hand steps aside.
//
// THE DEPTH. A dialog box is flat, at one depth. A room has depth. The game
// gives the point of the room under its pointer (TempleRoom::Draw writes it
// at 0x00E360C8). The ray ends on the plane through that point
// (temple_math.h, RayToHitPlane), and the pixel of the pointer is that end,
// seen from the eye. At rest the ray, the Hand of the game and the thing
// under it are then on one line from the eye, in both eyes.
//
// THE BUTTONS. The game takes a press of each of its two buttons on a control
// of a room. The Action button opens a door, and the Move button on empty
// space turns the camera of the room about the player. With temple_click=1
// the trigger and the grip are both the Action button.
//
// This file reads the game, keeps the depth, writes the log and holds the
// test keys. controllers.cpp asks it for the mode and the depth.

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/bw_addresses.h"
#include "MinHook.h"
#include "controller_hints.h"
#include "miracle.h"
#include "temple.h"
#include "temple_math.h"

extern void ModLog(const char* format, ...);

// head_camera.cpp
bool HeadCameraGameCamera(float position[3], float focus[3]);
// head_pose.cpp: one turn of the room about the head, and a number that
// changes each time the origin of the room changes.
bool HeadPoseTurn(float degrees);
uint64_t HeadPoseOriginSerial();
// comfort_camera.cpp: the angle of a snap turn on the land.
float ComfortCameraSnapDegrees();
// controllers.cpp
unsigned long long ControllersFrameCount();
bool ControllersOwnPointer();
// hand_direct.cpp: an object that the game offers to its pick as a ball with
// no mesh becomes a candidate of the choice. The object at the Hand.
bool HandDirectOffer(void* object, const float position[3], float radius,
                     const char** words);
bool HandDirectNearObject(void** object, float position[3]);
void ControllersGrabPulse(int hands, float amplitude, float seconds);

namespace {

// --- the game (runblack.exe v1.42) -------------------------------------------

const uintptr_t kGame = 0x00D0195C;       // the pointer to GGame
const int kGameTemple = 0x59B4;           // the Temple object
const int kGameInTemple = 0x205A28;       // 1 while the player is in the Temple
const int kGameHelp = 0x25005C;           // the HelpSystem
const int kHelpCinemaA = 0x45E8;          // both are not 0 while a guide plays
const int kHelpCinemaB = 0x45EC;
const int kTempleRoomNow = 0x1C;          // the room, after the 7 rooms
// TempleRoom.
const int kRoomEntries = 0x24;            // the array of the controls
const int kRoomEntryCount = 0x2C;
const int kRoomDrag = 0x7C;               // 0 no drag, 1 a control, 2 the camera
const int kRoomUnder = 0xC8;              // the sub mesh under the pointer
const int kRoomPicked = 0xCC;             // the same, of the last draw
// SubOptionEntry: the vtable, and the sub mesh of the control at +4.
const int kEntrySubMesh = 0x04;
// The point of the room under the pointer, 3 floats (TempleRoom::Draw).
const uintptr_t kHit = 0x00E360C8;
// The pointer of the game in render pixels, 2 ints.
const uintptr_t kPointer = 0x00E852C0;
// The buttons that the mouse object holds: bit 0 Move, bit 1 Action.
const uintptr_t kMouseHeld = 0x00E85304;
// A dialog box (SetupBox::GetCurrentActiveBox).
const uintptr_t kActiveBox = 0x00C4CD20;
// Temple::ProcessMouse, thiscall (x, y, message), ret 0xC.
const uintptr_t kProcessMouse = 0x007947C0;
// InnerCamera::Update, thiscall (room, seconds, held, action, pixel, drag),
// ret 0x18. Each camera of a room calls it.
const uintptr_t kCameraUpdate = 0x00796B60;

const char* const kRoomName[7] = {
    "the hall", "the Creature Room", "the Challenge Room",
    "the room of the multiplayer game", "the Options Room",
    "the Save Game Room", "the Library"};

// --- the settings -------------------------------------------------------------

bool g_loaded = false;
int g_pointer = 1;        // temple_pointer
int g_click = 1;          // temple_click
int g_camera_drag = 0;    // temple_camera_drag
int g_camera_follow = 0;  // temple_camera_follow
int g_turn = 1;           // temple_turn
int g_depth_cm = 300;     // temple_pointer_depth_cm
bool g_test_keys = false;
// Test keys.
int g_test_click_frame = 0;  // before this frame temple_click is 0
int g_scan_grid = 0;         // temple_test_scan: the cells in a row, 0 is off
bool g_scan_hall = false;    // temple_test_scan_hall: the scan runs in the hall too
int g_test_follow_frame = 0; // from this frame temple_camera_follow is 1
int g_scan_frame = 0;        // temple_test_scan_frame: no scan before it
// The taps on the land.
int g_taps = 1;              // temple_taps
int g_test_taps_frame = 0;   // before this frame temple_taps is 0
bool g_test_door_on = false; // temple_test_door_on: the story flag of the door
int g_test_enter_frame = 0;  // at this frame the Enter key goes to a dialog box
char g_targets[256] = "";    // temple_test_targets

bool IniPath(char path[MAX_PATH]) {
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return false;
  slash[1] = 0;
  strcat_s(path, MAX_PATH, "bwvr.ini");
  return true;
}

int IniInt(const char* key, int fallback) {
  char path[MAX_PATH];
  if (!IniPath(path)) return fallback;
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

void Load() {
  if (g_loaded) return;
  g_loaded = true;
  g_pointer = IniInt("temple_pointer", 1) != 0 ? 1 : 0;
  g_click = IniInt("temple_click", 1) != 0 ? 1 : 0;
  g_camera_drag = IniInt("temple_camera_drag", 0) != 0 ? 1 : 0;
  g_camera_follow = IniInt("temple_camera_follow", 0) != 0 ? 1 : 0;
  g_turn = IniInt("temple_turn", 1) != 0 ? 1 : 0;
  g_depth_cm = IniInt("temple_pointer_depth_cm", 300);
  if (g_depth_cm < 50) g_depth_cm = 50;
  if (g_depth_cm > 2000) g_depth_cm = 2000;
  g_test_keys = IniInt("test_keys", 0) != 0;
  g_test_click_frame = IniInt("temple_test_click_frame", 0);
  g_scan_grid = IniInt("temple_test_scan", 0);
  g_scan_hall = IniInt("temple_test_scan_hall", 0) != 0;
  g_test_follow_frame = IniInt("temple_test_follow_frame", 0);
  g_scan_frame = IniInt("temple_test_scan_frame", 0);
  g_taps = IniInt("temple_taps", 1) != 0 ? 1 : 0;
  g_test_taps_frame = IniInt("temple_test_taps_frame", 0);
  g_test_door_on = IniInt("temple_test_door_on", 0) != 0;
  g_test_enter_frame = IniInt("temple_test_enter_frame", 0);
  ModLog("temple       : temple_taps=%d. %s", g_taps,
         g_taps != 0
             ? "On the land the three leash posts at the Temple are "
               "targets of the grip of the real hand"
             : "On the land the leash posts at the Temple are no targets of "
               "the real hand");
  if (g_scan_grid < 0) g_scan_grid = 0;
  if (g_scan_grid > 40) g_scan_grid = 40;
  char path[MAX_PATH];
  if (IniPath(path)) {
    GetPrivateProfileStringA("bwvr", "temple_test_targets", "", g_targets,
                             sizeof(g_targets), path);
  }
  ModLog("temple       : temple_pointer=%d. %s", g_pointer,
         g_pointer != 0
             ? "In a room of the Temple the pointing hand points with a ray "
               "that ends on the thing under it"
             : "In a room of the Temple the pointer is the place where you "
               "see the point of the real hand");
  ModLog("temple       : temple_click=%d. %s", g_click,
         g_click != 0
             ? "In a room the trigger and the grip are both the Action "
               "button of the game: each one clicks a door and a control, "
               "and holds a slider"
             : "In a room the trigger is the Move button of the game and "
               "the grip is its Action button");
  ModLog("temple       : temple_camera_drag=%d. %s", g_camera_drag,
         g_camera_drag != 0
             ? "A press on empty space turns the camera of the room, as in "
               "the plain game"
             : "A press on empty space does not turn the camera of the "
               "room");
  ModLog("temple       : temple_turn=%d. %s", g_turn,
         g_turn != 0
             ? "In the Temple the stick of the pointing hand turns you in "
               "the room by one step, to the left or to the right"
             : "In the Temple the stick of the pointing hand does what it "
               "does on the land");
  ModLog("temple       : temple_camera_follow=%d. %s", g_camera_follow,
         g_camera_follow != 0
             ? "The camera of a room turns to the place of the pointer, as "
               "in the plain game"
             : "The camera of a room does not turn to the place of the "
               "pointer. It gets the pointer only for a press on empty "
               "space, and a door is such a place");
}

// --- what the game says on this frame -----------------------------------------

struct Facts {
  bool inside;       // the player is in the Temple
  int room;          // 0 to 6, -1 not known
  uint8_t* object;   // the TempleRoom
  bool guide;        // a guide of the game plays
  bool dialog;       // a dialog box is up
  int drag;          // the drag of the room
  int under;         // the sub mesh under the pointer
  int kind;          // the kind of the control under the pointer
  uint8_t* entry;    // that control
  bool have_hit;
  float hit[3];      // the point of the room under the pointer
  int pixel[2];      // the pointer of the game
  int held;          // the buttons that the mouse object holds
};

Facts g_facts = {};
bool g_ray_on = false;
uint64_t g_frame = 0;

uint8_t* Game() {
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return nullptr;
  return static_cast<uint8_t*>(*slot);
}

// The control of the room that has this sub mesh.
uint8_t* EntryOf(uint8_t* room, int sub_mesh, int* kind) {
  *kind = temple_math::kControlNone;
  uint8_t** const entries =
      *reinterpret_cast<uint8_t** const*>(room + kRoomEntries);
  const uint32_t count =
      *reinterpret_cast<const uint32_t*>(room + kRoomEntryCount);
  if (entries == nullptr || count == 0 || count > 512 ||
      IsBadReadPtr(entries, count * sizeof(void*))) {
    return nullptr;
  }
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t* entry = entries[i];
    if (entry == nullptr || IsBadReadPtr(entry, 0x30)) continue;
    if (entry[kEntrySubMesh] != sub_mesh) continue;
    const uint32_t* vtable = *reinterpret_cast<const uint32_t* const*>(entry);
    if (vtable == nullptr || IsBadReadPtr(vtable, 8)) continue;
    *kind = temple_math::ControlKindOf(vtable[1]);
    return entry;
  }
  return nullptr;
}

void ReadFacts(Facts* f) {
  memset(f, 0, sizeof(*f));
  f->room = -1;
  f->under = -1;
  uint8_t* game = Game();
  if (game == nullptr || IsBadReadPtr(game + kGameInTemple, 4)) return;
  f->inside = *reinterpret_cast<const int32_t*>(game + kGameInTemple) == 1;
  f->dialog = *reinterpret_cast<const uintptr_t*>(kActiveBox) != 0;
  f->held = *reinterpret_cast<const uint8_t*>(kMouseHeld) & 3;
  f->pixel[0] = reinterpret_cast<const int32_t*>(kPointer)[0];
  f->pixel[1] = reinterpret_cast<const int32_t*>(kPointer)[1];
  const uint8_t* help = *reinterpret_cast<uint8_t* const*>(game + kGameHelp);
  if (help != nullptr && !IsBadReadPtr(help + kHelpCinemaA, 8)) {
    f->guide = *reinterpret_cast<const int32_t*>(help + kHelpCinemaA) != 0 &&
               *reinterpret_cast<const int32_t*>(help + kHelpCinemaB) != 0;
  }
  if (!f->inside) return;
  uint8_t* temple = *reinterpret_cast<uint8_t* const*>(game + kGameTemple);
  if (temple == nullptr || IsBadReadPtr(temple, kTempleRoomNow + 4)) return;
  uint8_t* now = *reinterpret_cast<uint8_t* const*>(temple + kTempleRoomNow);
  if (now == nullptr || IsBadReadPtr(now, 0xE0)) return;
  for (int i = 0; i < 7; ++i) {
    if (*reinterpret_cast<uint8_t* const*>(temple + 4 * i) == now) f->room = i;
  }
  f->object = now;
  f->drag = *reinterpret_cast<const int32_t*>(now + kRoomDrag);
  f->under = *reinterpret_cast<const int32_t*>(now + kRoomUnder);
  f->entry = EntryOf(now, f->under, &f->kind);
  const float* hit = reinterpret_cast<const float*>(kHit);
  for (int i = 0; i < 3; ++i) f->hit[i] = hit[i];
  f->have_hit = hit[0] != 0.0f || hit[1] != 0.0f || hit[2] != 0.0f;
}

const char* RoomName(int room) {
  return room >= 0 && room < 7 ? kRoomName[room] : "a room with no name";
}

const char* HeldName(int held) {
  static const char* const kNames[4] = {
      "no button", "the Move button", "the Action button",
      "the Move button and the Action button"};
  return kNames[held & 3];
}

// --- the depth of the ray --------------------------------------------------------

temple_math::Depth g_depth = {};
bool g_have_asked = false;
float g_asked[3] = {0.0f, 0.0f, 0.0f};  // the end of the ray, last frame
// A hit of the game is from the pixel of a frame before. It counts when the
// eye sees it within this angle of the end of the ray of the last frame.
const float kStaleDegrees = 6.0f;
// The ray of this frame, for the log.
bool g_have_ray = false;
float g_ray_origin[3], g_ray_direction[3], g_ray_eye[3];
float g_ray_units = 0.0f;
// The ray is at rest: its direction did not change for this many frames.
int g_rest_frames = 0;
float g_rest_direction[3] = {0.0f, 0.0f, 0.0f};
int g_rest_logs = 0;
const int kMaxRestLogs = 60;
// For the report.
uint64_t g_ray_frames = 0;
uint64_t g_hit_frames = 0;
uint64_t g_kept_frames = 0;
uint64_t g_default_frames = 0;

// How far the hit of the game is from the ray, in cm of the room. -1 when
// one of them is not known.
float HitFromRayCm() {
  if (!g_have_ray || !g_facts.have_hit || !(g_ray_units > 0.0f)) return -1.0f;
  return temple_math::PointToRay(g_ray_origin, g_ray_direction, g_facts.hit) /
         g_ray_units * 100.0f;
}

const char* SourceName(int source) {
  switch (source) {
    case temple_math::kDepthHit: return "the hit of the game";
    case temple_math::kDepthKept: return "the depth of before";
    default: return "the depth of the settings";
  }
}

// What is under the pointer, for a log line.
void UnderText(const Facts& f, char* out, size_t size) {
  if (f.kind != temple_math::kControlNone) {
    sprintf_s(out, size, "%s (sub mesh %d)",
              temple_math::ControlKindName(f.kind), f.under);
  } else {
    sprintf_s(out, size, "no control (sub mesh %d)", f.under);
  }
}

// --- the saved games ------------------------------------------------------------
//
// THE SAVE GAME ROOM [V]. Each picture of the room is a slot. The function
// of picture N is the stub at 0x00791360 + 0x10 * N (63 stubs), and each
// one calls fn_00790B30 (the picture, N):
//  - The first click turns the camera to the picture
//    (Temple::SetCameraToLookAtSubMesh) and makes it the picture of now.
//  - A click on the picture of now, with a saved game of that number in the
//    list: the box that asks before a load (SaveGameRoom::ComfirmForLoadGame).
//  - With no saved game there, and N less than 14: fn_00790F80 saves the
//    game into slot N at once (the function at 0x00792C90). It asks first
//    only when a saved game has that slot.
// The list of the saved games is at 0x00E05F18 (a node has the next node
// and the saved game), with the count at 0x00E05F1C. A saved game has its
// number at +0x100. The slots 14 and more are the saves that the game makes
// by itself, so the slots of a player are 0 to 13.
const uintptr_t kSaveList = 0x00E05F18;
const uintptr_t kSaveCount = 0x00E05F1C;
const int kSaveNumber = 0x100;
const uint32_t kPictureFirst = 0x00791360;
const int kPictureCount = 63;
// GKeyBuffer::AddKey, for the test key temple_test_enter_frame.
const int kGameNoKeys = 0x0A;
const int kGameKeyBuffer = 0x2502A8;
const uintptr_t kAddKey = 0x005E1BB0;
const uintptr_t kKeyboardLock = 0x00E8C118;
typedef int(__fastcall* AddKeyFn)(void* self, void* unused, int key, int mods);

// The numbers of the saved games of the list. Returns the count.
int SaveNumbers(int* out, int most, uint8_t** saves) {
  int count = 0;
  const uint8_t* node = *reinterpret_cast<uint8_t* const*>(kSaveList);
  while (node != nullptr && count < most && !IsBadReadPtr(node, 8)) {
    uint8_t* save = *reinterpret_cast<uint8_t* const*>(node + 4);
    if (save != nullptr && !IsBadReadPtr(save, kSaveNumber + 4)) {
      out[count] = *reinterpret_cast<const int32_t*>(save + kSaveNumber);
      if (saves != nullptr) saves[count] = save;
      ++count;
    }
    node = *reinterpret_cast<uint8_t* const*>(node);
  }
  return count;
}

bool SlotHasSave(int slot) {
  int numbers[64];
  const int count = SaveNumbers(numbers, 64, nullptr);
  for (int i = 0; i < count; ++i) {
    if (numbers[i] % 100 == slot) return true;
  }
  return false;
}

// The slot of a picture, from its function. -1 for no picture of a slot.
int SlotOfFunction(uint32_t function) {
  if (function < kPictureFirst || ((function - kPictureFirst) & 0xF) != 0) {
    return -1;
  }
  const int slot = static_cast<int>((function - kPictureFirst) >> 4);
  return slot < kPictureCount ? slot : -1;
}

// One line with the saved games, when the room shows them and when the list
// changes. The name of a saved game is the first text in it.
int g_saves_before = -1;
void WatchSaves(const Facts& f) {
  if (!f.inside || f.room != 5) {
    g_saves_before = -1;
    return;
  }
  int numbers[32];
  uint8_t* saves[32];
  const int count = SaveNumbers(numbers, 32, saves);
  int sum = count;
  for (int i = 0; i < count; ++i) sum = sum * 131 + numbers[i];
  if (sum == g_saves_before) return;
  g_saves_before = sum;
  char text[900] = "";
  size_t used = 0;
  for (int i = 0; i < count && used + 90 < sizeof(text); ++i) {
    // The name: wide text at the start of the saved game.
    char name[48] = "";
    const wchar_t* wide = reinterpret_cast<const wchar_t*>(saves[i]);
    int n = 0;
    for (; n < 40 && wide[n] >= 0x20 && wide[n] < 0x7F; ++n) {
      name[n] = static_cast<char>(wide[n]);
    }
    name[n] = 0;
    used += static_cast<size_t>(sprintf_s(
        text + used, sizeof(text) - used, "%s%d \"%s\"", i > 0 ? ", " : "",
        numbers[i], name));
  }
  ModLog("temple       : the Save Game Room has %d saved games (the number "
         "and the name of each one): %s, frame %llu", count, text, g_frame);
}

// A test key: the Enter key into the key buffer of the game while a dialog
// box is up, and the Y key 40 frames later if the box is still up.
void TestEnter(const Facts& f) {
  if (g_test_enter_frame <= 0) return;
  const uint64_t first = static_cast<uint64_t>(g_test_enter_frame);
  if (g_frame != first && g_frame != first + 40) return;
  uint8_t* game = Game();
  if (game == nullptr || !f.dialog) {
    ModLog("temple test  : no dialog box is up, so no key goes to the game, "
           "frame %llu", g_frame);
    return;
  }
  const int key = g_frame == first ? 0x1C : 0x15;
  if ((game[kGameNoKeys] & 1) != 0) {
    ModLog("temple test  : the game takes no keys now, frame %llu", g_frame);
    return;
  }
  CRITICAL_SECTION* lock = reinterpret_cast<CRITICAL_SECTION*>(kKeyboardLock);
  EnterCriticalSection(lock);
  const int added = reinterpret_cast<AddKeyFn>(kAddKey)(
      game + kGameKeyBuffer, nullptr, key, 0);
  LeaveCriticalSection(lock);
  ModLog("temple test  : the key %s %s the key buffer of the game for the "
         "dialog box, frame %llu", key == 0x1C ? "Enter" : "Y",
         added != 0 ? "goes into" : "does not fit in", g_frame);
}

// --- the scan (a test key) --------------------------------------------------------
//
// temple_test_scan=N: when the ray first comes on in a room, the pointer of
// the game goes through N by N pixels of the middle 80 percent of the
// render, 3 frames on each one. The log then has each control that the game
// found, with its place. The word "aimtemple" of the fake controller aims
// the ray at the next control of temple_test_targets.

const int kScanFrames = 3;
const int kMaxGrid = 40;
bool g_scanning = false;
int g_scan_room = -2;       // the room of the last scan
uint64_t g_scan_start = 0;
float g_scan_width = 0.0f, g_scan_height = 0.0f;
int16_t g_scan_id[kMaxGrid * kMaxGrid];
float g_scan_hit[kMaxGrid * kMaxGrid][3];
// What a scan found: one entry for each sub mesh that is a control.
struct Found {
  int id;
  int kind;
  int cells;
  float pixel[2];
  float world[3];
  int slot;          // the slot of a picture of the Save Game Room, or -1
};
const int kMaxFound = 64;
Found g_found[kMaxFound];
int g_found_count = 0;
int g_targets_used = 0;

void ScanPixel(int cell, float* x, float* y) {
  const int n = g_scan_grid;
  const int cx = cell % n, cy = cell / n;
  *x = g_scan_width * (0.1f + 0.8f * (cx + 0.5f) / n);
  *y = g_scan_height * (0.1f + 0.8f * (cy + 0.5f) / n);
}

void ScanEnd(const Facts& f) {
  g_scanning = false;
  g_found_count = 0;
  const int n = g_scan_grid;
  int none = 0;
  for (int id = 0; id < 256; ++id) {
    int cells = 0;
    float sx = 0.0f, sy = 0.0f;
    for (int c = 0; c < n * n; ++c) {
      if (g_scan_id[c] != id) continue;
      float x, y;
      ScanPixel(c, &x, &y);
      sx += x;
      sy += y;
      ++cells;
    }
    if (cells == 0) continue;
    int kind = temple_math::kControlNone;
    uint8_t* entry =
        f.object != nullptr ? EntryOf(f.object, id, &kind) : nullptr;
    if (kind == temple_math::kControlNone || entry == nullptr) {
      none += cells;
      continue;
    }
    const uint32_t function = *reinterpret_cast<const uint32_t*>(entry + 0x10);
    const int slot = f.room == 5 && kind == temple_math::kControlPicture
                         ? SlotOfFunction(function)
                         : -1;
    sx /= cells;
    sy /= cells;
    // The cell of this control that is nearest to its middle.
    int best = -1;
    float best_d = 0.0f;
    for (int c = 0; c < n * n; ++c) {
      if (g_scan_id[c] != id) continue;
      float x, y;
      ScanPixel(c, &x, &y);
      const float d = (x - sx) * (x - sx) + (y - sy) * (y - sy);
      if (best < 0 || d < best_d) {
        best = c;
        best_d = d;
      }
    }
    float bx, by;
    ScanPixel(best, &bx, &by);
    char slot_text[80] = "";
    if (slot >= 0) {
      sprintf_s(slot_text, ". It is the picture of slot %d, which has %s",
                slot, SlotHasSave(slot) ? "a saved game" : "no saved game");
    }
    ModLog("temple test  : the scan found %s, sub mesh %d, in %d cells. Its "
           "middle cell is at the pixel %.0f %.0f, and at %.2f %.2f %.2f in "
           "the world. Its function is 0x%08X%s",
           temple_math::ControlKindName(kind), id, cells, bx, by,
           g_scan_hit[best][0], g_scan_hit[best][1], g_scan_hit[best][2],
           function, slot_text);
    if (g_found_count < kMaxFound) {
      Found& found = g_found[g_found_count++];
      found.id = id;
      found.kind = kind;
      found.cells = cells;
      found.pixel[0] = bx;
      found.pixel[1] = by;
      found.slot = slot;
      for (int i = 0; i < 3; ++i) found.world[i] = g_scan_hit[best][i];
    }
  }
  ModLog("temple test  : the scan of %s ends at frame %llu: %d controls in "
         "%d by %d cells, and %d cells with no control", RoomName(f.room),
         g_frame, g_found_count, n, n, none);
}

void ScanFrame(const Facts& f) {
  if (g_scan_grid <= 0) return;
  if (!g_scanning) {
    if (!g_ray_on || f.dialog || f.room < 0 || f.room == g_scan_room) return;
    // In the hall the camera turns to a pointer that is low in the view, so
    // a scan there changes the view for the rest of the run.
    if (f.room == 0 && !g_scan_hall) return;
    if (g_frame < static_cast<uint64_t>(g_scan_frame)) return;
    const bw::tech::InfoTransform* info =
        reinterpret_cast<const bw::tech::InfoTransform*>(
            bw::tech::kInfoTransform);
    if (IsBadReadPtr(info, sizeof(*info)) || !(info->half_res_x > 0.0f) ||
        !(info->half_res_y > 0.0f)) {
      return;
    }
    g_scan_width = 2.0f * info->half_res_x;
    g_scan_height = 2.0f * info->half_res_y;
    g_scan_room = f.room;
    g_scanning = true;
    g_scan_start = g_frame;
    for (int c = 0; c < kMaxGrid * kMaxGrid; ++c) g_scan_id[c] = -1;
    ModLog("temple test  : a scan of %s starts at frame %llu, %d by %d "
           "pixels, %d frames on each one", RoomName(f.room), g_frame,
           g_scan_grid, g_scan_grid, kScanFrames);
    return;
  }
  if (!g_ray_on || f.room != g_scan_room) {
    g_scanning = false;
    g_scan_room = -2;
    ModLog("temple test  : the scan stops at frame %llu: the ray is off or "
           "the room changed", g_frame);
    return;
  }
  // The pixel of a cell goes out on its frames 0 to 2. The draw of frame 1
  // has the pick of that pixel, and this frame reads it at the start of the
  // next cell.
  const uint64_t since = g_frame - g_scan_start;
  const int cell = static_cast<int>(since / kScanFrames);
  if (since > 0 && since % kScanFrames == 0 && cell - 1 >= 0 &&
      cell - 1 < g_scan_grid * g_scan_grid && f.object != nullptr) {
    g_scan_id[cell - 1] = static_cast<int16_t>(
        *reinterpret_cast<const int32_t*>(f.object + kRoomPicked) & 0xFF);
    for (int i = 0; i < 3; ++i) g_scan_hit[cell - 1][i] = f.hit[i];
  }
  if (cell >= g_scan_grid * g_scan_grid) ScanEnd(f);
}

// --- the hooks ----------------------------------------------------------------------

typedef int(__fastcall* ProcessMouseFn)(void* self, void* unused, int x, int y,
                                        int message);
typedef void(__fastcall* CameraUpdateFn)(void* self, void* unused, void* room,
                                         float seconds, int held, int action,
                                         const int* pixel, int drag);
ProcessMouseFn o_ProcessMouse = nullptr;
CameraUpdateFn o_CameraUpdate = nullptr;
bool g_hooked = false;
int g_message_logs = 0;
const int kMaxMessageLogs = 120;
uint64_t g_messages = 0;
uint64_t g_drags_stopped = 0;
// A press came on this frame: TempleFrame writes the values of the control
// some frames later, so a log shows what the press changed.
uint64_t g_entry_log_frame = 0;
int g_entry_sub_mesh = -1;

void LogEntry(const char* when, uint8_t* entry) {
  if (entry == nullptr || IsBadReadPtr(entry, 0x30)) return;
  const uint32_t* w = reinterpret_cast<const uint32_t*>(entry);
  const float* v = reinterpret_cast<const float*>(entry);
  ModLog("temple       : the control %s: words %08X %08X %08X %08X %08X %08X "
         "%08X %08X %08X %08X, floats at +0x1C %.3f, +0x20 %.3f, +0x24 %.3f, "
         "+0x28 %.3f, frame %llu", when, w[1], w[2], w[3], w[4], w[5], w[6],
         w[7], w[8], w[9], w[10], v[7], v[8], v[9], v[10], g_frame);
}

// The messages of the mouse that the Temple gets: 1 and 3 are a button that
// goes down, 2 and 4 a button that goes up, 5 a double click, 7 a move.
int __fastcall h_ProcessMouse(void* self, void* unused, int x, int y,
                              int message) {
  if (message != 7) {
    ++g_messages;
    if (g_message_logs < kMaxMessageLogs) {
      ++g_message_logs;
      char under[96];
      UnderText(g_facts, under, sizeof(under));
      const float off = HitFromRayCm();
      ModLog("temple       : the game gets the mouse message %d at the pixel "
             "%d %d in %s, with the pointer on %s. The mouse object holds "
             "%s. The thing under the pointer is %.1f cm from the ray, "
             "frame %llu", message, x, y, RoomName(g_facts.room), under,
             HeldName(*reinterpret_cast<const uint8_t*>(kMouseHeld)), off,
             g_frame);
      if (g_facts.entry != nullptr && (message == 1 || message == 3)) {
        LogEntry("at the press", g_facts.entry);
        g_entry_sub_mesh = g_facts.under;
        g_entry_log_frame = g_frame + 40;
      }
    }
  }
  return o_ProcessMouse(self, unused, x, y, message);
}

// temple_camera_follow now. A test key makes it 1 from a frame on.
bool CameraFollows() {
  if (g_test_follow_frame > 0 &&
      g_frame >= static_cast<uint64_t>(g_test_follow_frame)) {
    return true;
  }
  return g_camera_follow != 0;
}

// held is 1 while a button is down after a press on empty space, and 2 after
// a double click. drag says that the room drags the camera. action is not
// read: the function takes the Action button from the mouse object.
//
// THE CAMERA DOES NOT FOLLOW THE POINTER. The function turns the place of
// the pointer into a number from -1 to 1 for each axis (+0x3FC, +0x400), and
// it finds what the pointer is on (+0x3F8). The camera of the hall
// (WorldRoomCamera::UpdateMain) turns up or down when the pointer is in the
// top or the bottom tenth of the view, and it turns down to the pool when
// the pointer is on the pool. With a mouse the player does that on purpose.
// A hand that points low must not turn the room about the head. So the
// camera gets the middle of the view as the pointer.
//
// A PRESS ON EMPTY SPACE DOES NOT TURN THE CAMERA. A click on a door is a
// press on empty space: the room gives the camera the held flag and the drag
// flag, and the camera finds the door at the pixel of that call
// (CalcDoorHit). So the camera must get the pointer for a press. But the
// same press turns the camera when the pointer moves with the button down.
// So for a press on empty space the camera gets the pixel of the press, and
// that pixel stays until the button goes up.
bool g_press_latched = false;
int g_press_pixel[2] = {0, 0};
uint64_t g_neutral_frames = 0;

void __fastcall h_CameraUpdate(void* self, void* unused, void* room,
                               float seconds, int held, int action,
                               const int* pixel, int drag) {
  static int held_before = -1, drag_before = -1;
  static int said = 0;
  const int drag_now = drag & 0xFF;
  int given[2] = {0, 0};
  if (g_ray_on && ControllersOwnPointer() && pixel != nullptr) {
    const bool down =
        (*reinterpret_cast<const uint8_t*>(kMouseHeld) & 3) != 0;
    if (drag_now != 0 && down) {
      if (!g_press_latched) {
        g_press_latched = true;
        g_press_pixel[0] = pixel[0];
        g_press_pixel[1] = pixel[1];
      }
      if (g_camera_drag == 0) {
        given[0] = g_press_pixel[0];
        given[1] = g_press_pixel[1];
        if (given[0] != pixel[0] || given[1] != pixel[1]) ++g_drags_stopped;
        pixel = given;
      }
    } else {
      g_press_latched = false;
      const bw::tech::InfoTransform* info =
          reinterpret_cast<const bw::tech::InfoTransform*>(
              bw::tech::kInfoTransform);
      if (!CameraFollows() && !IsBadReadPtr(info, sizeof(*info)) &&
          info->half_res_x > 0.0f && info->half_res_y > 0.0f) {
        given[0] = static_cast<int>(info->half_res_x);
        given[1] = static_cast<int>(info->half_res_y);
        pixel = given;
        ++g_neutral_frames;
      }
    }
  } else {
    g_press_latched = false;
  }
  if ((held != held_before || drag_now != drag_before) && said < 60) {
    ++said;
    ModLog("temple       : the camera of the room gets held %d and drag %d "
           "(the mouse object holds %s), frame %llu", held, drag_now,
           HeldName(*reinterpret_cast<const uint8_t*>(kMouseHeld)), g_frame);
  }
  held_before = held;
  drag_before = drag_now;
  o_CameraUpdate(self, unused, room, seconds, held, action, pixel, drag);
}

// --- the taps on the land -------------------------------------------------------
//
// THE THREE LEASH POSTS at the Temple are objects with no mesh. The game
// gives each one to its pick as a ball of 1 unit
// (GInterface::SendInvisibleDrawCollision, from fn_00466730 in
// CitadelHeart::DrawNow). The pick of the real hand takes its candidates
// from GInterface::SendObjectDrawCollision, and a post does not come there.
// So a press of the grip at a post has no target in the plain game.
//
// The mod takes the three posts from the draw of the Temple of the player
// and gives them to the choice of the real hand as balls (HandDirectOffer).
// The game then taps the chosen one as it taps a house: the tests and the
// tap are its own (InterfaceValidToTap, InterfaceTap).
//
// THE DOOR OF THE TEMPLE (CitadelEntrance) has a mesh for the collision, and
// the draw offers it as each object. So it is a target of the grip with no
// help from the mod. The mod only reads it here, for the card and for the
// log.
const uintptr_t kHeartOffers = 0x00466730;   // thiscall on CitadelHeart
const uintptr_t kInvisibleCollision = 0x00519960;  // cdecl (object, point, radius)
const int kHeartPlayer = 0x1C;       // the vtable place of GetPlayer
const int kHeartEntrance = 0xDC;     // the CitadelEntrance
const int kHeartLeash = 0xE4;        // the TempleLeash
const int kLeashPosts = 0x04;        // 3 LeashObj pointers
const int kPostChosen = 0x58;        // a pointer to the number of the chosen post
const int kGamePlayers = 0x18;       // the first GPlayer, 0xA60 bytes each
const int kGamePlayerSize = 0xA60;
const int kGameLocalPlayer = 0x205A59;
const int kGameStory = 0x250090;     // its +0xA0 turns the door on
const int kStoryDoorOn = 0xA0;
const int kObjectMesh = 0x40;        // the LH3DObject of an object
const int kMeshTranslation = 0x38;

typedef void(__fastcall* HeartOffersFn)(void* self, void* unused);
typedef int(__cdecl* InvisibleFn)(void* object, const float* point,
                                  float radius);
typedef void*(__fastcall* GetPlayerFn)(void* self, void* unused);
HeartOffersFn o_HeartOffers = nullptr;
InvisibleFn o_Invisible = nullptr;

struct LandThing {
  void* object;
  float position[3];
  float radius;
  uint64_t frame;       // the frame of the last draw
  bool offered;         // it is a candidate of the choice in this frame
  const char* words;    // what the game says that a press does
};
LandThing g_door = {};
LandThing g_post[3] = {};
uint8_t* g_offer_heart = nullptr;  // the Temple of the player, in its draw
int g_chosen_before = -2;
bool g_door_valid_before = false;
bool g_land_said = false;
int g_land_logs = 0;
const int kMaxLandLogs = 120;
void* g_land_at_before = nullptr;
int g_test_post = 0;
uint64_t g_land_offers = 0;

const char* const kLeashName[3] = {"the Leash of Aggression",
                                   "the Leash of Learning",
                                   "the Leash of Compassion"};

bool TapsOn() {
  if (g_test_taps_frame > 0 &&
      g_frame < static_cast<uint64_t>(g_test_taps_frame)) {
    return false;
  }
  return g_taps != 0;
}

bool Finite3(const float* v) {
  for (int i = 0; i < 3; ++i) {
    if (!(v[i] == v[i]) || v[i] > 1e7f || v[i] < -1e7f) return false;
  }
  return true;
}

// The number of the chosen post, 0 to 2, and -1 for none or not known.
int ChosenPost() {
  for (int i = 0; i < 3; ++i) {
    uint8_t* post = static_cast<uint8_t*>(g_post[i].object);
    if (post == nullptr || IsBadReadPtr(post + kPostChosen, 4)) continue;
    const int32_t* chosen =
        *reinterpret_cast<const int32_t* const*>(post + kPostChosen);
    if (chosen == nullptr || IsBadReadPtr(chosen, 4)) continue;
    return *chosen >= 0 && *chosen <= 2 ? *chosen : -1;
  }
  return -1;
}

void OfferThing(LandThing* thing) {
  if (!TapsOn()) {
    thing->offered = false;
    thing->words = "";
    return;
  }
  // The offer counts in the first eye pass only. The call of the second pass
  // gives no words, and it must not take the words of the first one away.
  const char* words = "";
  const bool offered =
      HandDirectOffer(thing->object, thing->position, thing->radius, &words);
  if (words[0] != 0) {
    thing->words = words;
    thing->offered = offered;
  }
  if (offered) ++g_land_offers;
}

int __cdecl h_Invisible(void* object, const float* point, float radius) {
  if (g_offer_heart != nullptr && object != nullptr && point != nullptr &&
      !IsBadReadPtr(point, 12) && Finite3(point)) {
    uint8_t* leash =
        *reinterpret_cast<uint8_t* const*>(g_offer_heart + kHeartLeash);
    if (leash != nullptr && !IsBadReadPtr(leash, 0x10)) {
      for (int i = 0; i < 3; ++i) {
        if (*reinterpret_cast<void* const*>(leash + kLeashPosts + 4 * i) !=
            object) {
          continue;
        }
        LandThing* post = &g_post[i];
        post->object = object;
        for (int k = 0; k < 3; ++k) post->position[k] = point[k];
        post->radius = radius > 0.1f && radius < 50.0f ? radius : 1.0f;
        post->frame = ControllersFrameCount();
        OfferThing(post);
      }
    }
  }
  return o_Invisible(object, point, radius);
}

// The draw of a Temple: the posts come in the call, through h_Invisible. The
// door comes after it.
void __fastcall h_HeartOffers(void* self, void* unused) {
  uint8_t* heart = static_cast<uint8_t*>(self);
  bool mine = false;
  uint8_t* game = Game();
  if (heart != nullptr && game != nullptr && !IsBadReadPtr(heart, 0xE8)) {
    void** table = *reinterpret_cast<void** const*>(heart);
    void* player = reinterpret_cast<GetPlayerFn>(table[kHeartPlayer / 4])(
        heart, nullptr);
    const int local = game[kGameLocalPlayer];
    mine = player != nullptr &&
           player == game + kGamePlayers + kGamePlayerSize * local;
  }
  // The frame of the controllers, because this runs in the draw.
  const uint64_t frame = ControllersFrameCount();
  g_offer_heart = mine ? heart : nullptr;
  o_HeartOffers(self, unused);
  g_offer_heart = nullptr;
  if (!mine) return;

  // A test key: the story of a new game turns the door on late.
  uint8_t* story = *reinterpret_cast<uint8_t* const*>(game + kGameStory);
  if (g_test_door_on && story != nullptr &&
      !IsBadWritePtr(story + kStoryDoorOn, 4) &&
      *reinterpret_cast<const int32_t*>(story + kStoryDoorOn) == 0) {
    *reinterpret_cast<int32_t*>(story + kStoryDoorOn) = 1;
    ModLog("temple test  : temple_test_door_on=1: the mod turns the door of "
           "the Temple on, as the story of the game does later, frame %llu",
           frame);
  }
  const bool door_valid =
      story != nullptr && !IsBadReadPtr(story + kStoryDoorOn, 4) &&
      *reinterpret_cast<const int32_t*>(story + kStoryDoorOn) != 0;

  uint8_t* entrance = *reinterpret_cast<uint8_t* const*>(heart + kHeartEntrance);
  if (entrance != nullptr && !IsBadReadPtr(entrance, 0x60)) {
    const uint8_t* mesh =
        *reinterpret_cast<uint8_t* const*>(entrance + kObjectMesh);
    if (mesh != nullptr && !IsBadReadPtr(mesh + kMeshTranslation, 12) &&
        Finite3(reinterpret_cast<const float*>(mesh + kMeshTranslation))) {
      const float* at = reinterpret_cast<const float*>(mesh + kMeshTranslation);
      g_door.object = entrance;
      for (int k = 0; k < 3; ++k) g_door.position[k] = at[k];
      g_door.frame = frame;
    }
  }

  // One line for the first draw, and one for each change.
  const int chosen = ChosenPost();
  if (!g_land_said && g_door.object != nullptr && g_post[0].object != nullptr &&
      g_post[1].object != nullptr && g_post[2].object != nullptr) {
    g_land_said = true;
    ModLog("temple land  : the Temple of the player is in the draw. The door "
           "is at %.2f %.2f %.2f. %s. The leash "
           "posts are at %.2f %.2f %.2f, %.2f %.2f %.2f and %.2f %.2f %.2f, "
           "each with a ball of %.1f units. The chosen post is %d. "
           "temple_taps is %s, frame %llu", g_door.position[0],
           g_door.position[1], g_door.position[2],
           door_valid ? "The story has the door on"
                      : "The story has the door off: a tap does nothing yet",
           g_post[0].position[0], g_post[0].position[1], g_post[0].position[2],
           g_post[1].position[0], g_post[1].position[1], g_post[1].position[2],
           g_post[2].position[0], g_post[2].position[1], g_post[2].position[2],
           g_post[0].radius, chosen, TapsOn() ? "on" : "off", frame);
    g_chosen_before = chosen;
    g_door_valid_before = door_valid;
  }
  if (g_land_said && g_land_logs < kMaxLandLogs) {
    if (chosen != g_chosen_before) {
      ++g_land_logs;
      ModLog("temple land  : the chosen leash post is %d now (%s). It was %d, "
             "frame %llu", chosen,
             chosen >= 0 ? kLeashName[chosen] : "no leash", g_chosen_before,
             frame);
      g_chosen_before = chosen;
    }
    if (door_valid != g_door_valid_before) {
      ++g_land_logs;
      ModLog("temple land  : the story turns the door of the Temple %s, frame "
             "%llu", door_valid ? "on: a tap goes into the Temple" : "off",
             frame);
      g_door_valid_before = door_valid;
    }
  }
}

// Which of the four things an object is: 0 the door, 1 to 3 a post, -1 none.
// Only a thing of the draw of the last frames counts.
int LandThingOf(void* object) {
  if (object == nullptr) return -1;
  const uint64_t now = ControllersFrameCount();
  if (g_door.object == object && now - g_door.frame < 4) return 0;
  for (int i = 0; i < 3; ++i) {
    if (g_post[i].object == object && now - g_post[i].frame < 4) return 1 + i;
  }
  return -1;
}

// Once a frame: one line when the Hand's point comes to one of the things,
// and one when it goes away.
void WatchLand() {
  void* object = nullptr;
  float position[3];
  if (!HandDirectNearObject(&object, position)) object = nullptr;
  const int thing = LandThingOf(object);
  if (thing < 0) object = nullptr;
  if (object == g_land_at_before) return;
  g_land_at_before = object;
  if (g_land_logs >= kMaxLandLogs) return;
  ++g_land_logs;
  if (object == nullptr) {
    ModLog("temple land  : the Hand's point is not at the door or at a post "
           "now, frame %llu", g_frame);
  } else if (thing == 0) {
    ModLog("temple land  : the Hand's point is at the door of the Temple "
           "(%.2f %.2f %.2f): the grip taps it, frame %llu", position[0],
           position[1], position[2], g_frame);
  } else {
    ModLog("temple land  : the Hand's point is at the leash post %d, %s "
           "(%.2f %.2f %.2f). The game says that a press %s. The chosen post "
           "is %d, frame %llu", thing - 1, kLeashName[thing - 1], position[0],
           position[1], position[2], g_post[thing - 1].words, ChosenPost(),
           g_frame);
  }
}

void HookOne(uintptr_t address, void* replacement, void** original,
             const char* name) {
  void* target = reinterpret_cast<void*>(address);
  MH_STATUS status = MH_CreateHook(target, replacement, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : %s at 0x%08X %s", name,
         static_cast<unsigned>(address), status == MH_OK ? "ok" : "FAILED");
}

void Install() {
  if (g_hooked) return;
  g_hooked = true;
  HookOne(kProcessMouse, &h_ProcessMouse,
          reinterpret_cast<void**>(&o_ProcessMouse),
          "Temple::ProcessMouse (the log of the Temple)");
  HookOne(kCameraUpdate, &h_CameraUpdate,
          reinterpret_cast<void**>(&o_CameraUpdate),
          "InnerCamera::Update (the drag of the camera of a room)");
  HookOne(kHeartOffers, &h_HeartOffers,
          reinterpret_cast<void**>(&o_HeartOffers),
          "the leash posts of a Temple (the taps on the land)");
  HookOne(kInvisibleCollision, &h_Invisible,
          reinterpret_cast<void**>(&o_Invisible),
          "GInterface::SendInvisibleDrawCollision (the leash posts)");
}

// --- the watch of the camera of the room ------------------------------------------

bool g_have_camera = false;
float g_camera_position[3], g_camera_yaw = 0.0f, g_camera_pitch = 0.0f;
bool g_camera_moving = false;
int g_camera_still = 0;
uint64_t g_move_start = 0;
float g_move_position[3], g_move_yaw = 0.0f, g_move_pitch = 0.0f;
int g_move_held = 0, g_move_drag = 0;
int g_camera_logs = 0;
const int kMaxCameraLogs = 60;
uint64_t g_camera_moves = 0;

float WrapDegrees(float d) {
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

void WatchCamera(const Facts& f) {
  float position[3], focus[3];
  // A different room has a different camera: that is a cut, and not a move.
  static int room_before = -2;
  const bool new_room = f.room != room_before;
  room_before = f.room;
  if (!f.inside || f.guide || new_room ||
      !HeadCameraGameCamera(position, focus)) {
    g_have_camera = false;
    g_camera_moving = false;
    return;
  }
  const float d[3] = {focus[0] - position[0], focus[1] - position[1],
                      focus[2] - position[2]};
  const float flat = sqrtf(d[0] * d[0] + d[2] * d[2]);
  const float yaw = atan2f(d[0], d[2]) * 57.29578f;
  const float pitch = atan2f(d[1], flat) * 57.29578f;
  if (g_have_camera) {
    const bool moved =
        fabsf(WrapDegrees(yaw - g_camera_yaw)) > 0.02f ||
        fabsf(pitch - g_camera_pitch) > 0.02f ||
        temple_math::Distance(position, g_camera_position) > 0.01f;
    if (moved && !g_camera_moving) {
      g_camera_moving = true;
      g_move_start = g_frame;
      g_move_yaw = g_camera_yaw;
      g_move_pitch = g_camera_pitch;
      for (int i = 0; i < 3; ++i) g_move_position[i] = g_camera_position[i];
      g_move_held = f.held;
      g_move_drag = f.drag;
    }
    if (moved) g_camera_still = 0;
    if (!moved && g_camera_moving && ++g_camera_still >= 10) {
      g_camera_moving = false;
      ++g_camera_moves;
      if (g_camera_logs < kMaxCameraLogs) {
        ++g_camera_logs;
        ModLog("temple       : the camera of %s moved from frame %llu to "
               "frame %llu: it turned %.1f degrees to the side and %.1f "
               "degrees up, and it went %.2f units. At the start the mouse "
               "object held %s, and the drag of the room was %d",
               RoomName(f.room), g_move_start, g_frame - 10,
               WrapDegrees(yaw - g_move_yaw), pitch - g_move_pitch,
               temple_math::Distance(position, g_move_position),
               HeldName(g_move_held), g_move_drag);
      }
    }
  }
  g_have_camera = true;
  g_camera_yaw = yaw;
  g_camera_pitch = pitch;
  for (int i = 0; i < 3; ++i) g_camera_position[i] = position[i];
}

// --- the test key -------------------------------------------------------------------

// Ctrl and the 6 key of the number pad: the ray with the trigger and the
// grip as the click, the ray with the plain buttons, and no ray (the pointer
// is the place of the real hand in the view). A longer pulse for each next
// one.
void PollTestKey() {
  if (!g_test_keys) return;
  static bool before = false;
  const bool down = (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool pressed = down && !before;
  before = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  int way = g_pointer == 0 ? 2 : (g_click != 0 ? 0 : 1);
  way = (way + 1) % 3;
  g_pointer = way == 2 ? 0 : 1;
  g_click = way == 0 ? 1 : 0;
  static const char* const kWays[3] = {
      "the ray, and the trigger and the grip are both the click "
      "(temple_pointer=1, temple_click=1)",
      "the ray, and the plain buttons of the game: the grip is the Action "
      "button and the trigger the Move button (temple_pointer=1, "
      "temple_click=0)",
      "no ray: the pointer is the place of the real hand in the view "
      "(temple_pointer=0)"};
  ControllersGrabPulse(1, 0.5f, 0.08f + 0.11f * way);
  ModLog("temple       : Ctrl and numpad 6: %s, frame %llu", kWays[way],
         g_frame);
}

// --- one frame --------------------------------------------------------------------------

// The drag of a control: the control, and how far the pointer went while
// the room held it. A scroll and a list take the change of the pixel row of
// each mouse message (SubOptionEntryScroll::UpdateMouse, +0x1C).
uint8_t* g_drag_entry = nullptr;
int g_drag_sub_mesh = -1;
int g_drag_start_row = 0;
int g_drag_most = 0;      // the most rows from the start, with its sign
int g_drag_steps = 0;     // frames with a step that is not 0
uint64_t g_drag_start_frame = 0;

// THE TURN IN A ROOM. A player who sits cannot turn the body to a door
// behind the chair. On the land the stick turns the view in steps. In a room
// the same push turns the player about the head by the same step: the origin
// of the room of the headset turns (HeadPoseTurn), and the camera of the
// game does not move. It is a move that the player makes, in one step, with
// no glide. The sum of the turns goes back when the player leaves the
// Temple, so the land is where it was.
float g_turned = 0.0f;         // degrees to the right, in this visit
uint64_t g_turn_serial = 0;    // the origin of the room after the last turn
uint64_t g_turns = 0;

enum Mode { kModeOut, kModeOld, kModeGuide, kModeRay };
int g_mode = kModeOut;
int g_room_before = -2;
int g_under_before = -2, g_kind_before = -1, g_drag_before = 0;
int g_under_logs = 0;
const int kMaxUnderLogs = 300;
uint64_t g_under_changes = 0;

}  // namespace

bool TempleInside() { return g_facts.inside; }

bool TempleRayOn() { return g_ray_on; }

int TempleClick() {
  if (g_test_click_frame > 0 &&
      g_frame < static_cast<uint64_t>(g_test_click_frame)) {
    return 0;
  }
  return g_click;
}

bool TempleTurnOn() { return g_turn != 0 && g_facts.inside; }

bool TempleTurn(int direction) {
  const Facts& f = g_facts;
  if (!f.inside || direction == 0) return false;
  const char* why = f.guide    ? "a guide of the game plays"
                    : f.dialog ? "a dialog box is up"
                               : nullptr;
  float step = ComfortCameraSnapDegrees();
  if (!(step > 0.0f)) step = 45.0f;
  const float degrees = direction > 0 ? step : -step;
  if (why == nullptr && !HeadPoseTurn(degrees)) why = "the head has no pose";
  if (why != nullptr) {
    ModLog("temple       : the stick does not turn the player in %s: %s, "
           "frame %llu", RoomName(f.room), why, g_frame);
    return false;
  }
  g_turned += degrees;
  while (g_turned > 180.0f) g_turned -= 360.0f;
  while (g_turned <= -180.0f) g_turned += 360.0f;
  g_turn_serial = HeadPoseOriginSerial();
  ++g_turns;
  // The ray is at a new place, so its depth starts again.
  g_depth.have = false;
  g_have_asked = false;
  g_rest_frames = 0;
  ModLog("temple       : the stick turns the player %.0f degrees to the %s "
         "in %s, %.0f degrees to the right in all in this visit, frame %llu",
         step, direction > 0 ? "right" : "left", RoomName(f.room), g_turned,
         g_frame);
  return true;
}

int TempleCardRoom() {
  return g_ray_on && !g_facts.dialog ? g_facts.room : -1;
}

void TempleFrame(unsigned long long frame) {
  Load();
  Install();
  g_frame = frame;
  PollTestKey();
  ReadFacts(&g_facts);
  const Facts& f = g_facts;

  const int mode = !f.inside ? kModeOut
                   : g_pointer == 0 ? kModeOld
                   : f.guide ? kModeGuide
                             : kModeRay;
  if (mode != g_mode || (f.inside && f.room != g_room_before)) {
    switch (mode) {
      case kModeOut:
        ModLog("temple       : the player is out of the Temple, and the "
               "direct hand is back, frame %llu", frame);
        // The turns of this visit go back.
        if (g_turned != 0.0f) {
          const bool same = HeadPoseOriginSerial() == g_turn_serial;
          const bool back = same && HeadPoseTurn(-g_turned);
          ModLog("temple       : the turns in the Temple were %.0f degrees to "
                 "the right in all. %s, frame %llu", g_turned,
                 back ? "The room of the headset turns back, so the land is "
                        "where it was"
                      : "The origin of the room changed in the Temple (a "
                        "recenter), so nothing turns back",
                 frame);
          g_turned = 0.0f;
        }
        break;
      case kModeOld:
        ModLog("temple       : in %s the pointer is the place of the real "
               "hand in the view (temple_pointer=0), frame %llu",
               RoomName(f.room), frame);
        break;
      case kModeGuide:
        ModLog("temple       : in %s a guide of the game plays, so the ray "
               "is off and the room takes no click, frame %llu",
               RoomName(f.room), frame);
        break;
      default:
        ModLog("temple       : in %s the pointing hand points with a ray. "
               "%s, frame %llu", RoomName(f.room),
               TempleClick() != 0
                   ? "The trigger and the grip are both the click"
                   : "The grip is the Action button and the trigger the "
                     "Move button",
               frame);
        break;
    }
    // A new room, or a new mode: the depth starts again.
    g_depth.have = false;
    g_have_asked = false;
    g_rest_frames = 0;
  }
  g_mode = mode;
  g_room_before = f.inside ? f.room : -2;
  g_ray_on = mode == kModeRay;
  if (!g_ray_on) g_have_ray = false;

  // What is under the pointer, and the drag of the room: one line for each
  // change.
  if (f.inside && !f.guide && !g_scanning) {
    // A wall has many sub meshes. Only a change of the control counts.
    const int under = f.kind != temple_math::kControlNone ? f.under : -1;
    if (under != g_under_before || f.kind != g_kind_before) {
      ++g_under_changes;
      if (g_under_logs < kMaxUnderLogs) {
        ++g_under_logs;
        char text[96];
        UnderText(f, text, sizeof(text));
        ModLog("temple       : the pointer is on %s at the pixel %d %d, at "
               "%.2f %.2f %.2f in the world, frame %llu", text, f.pixel[0],
               f.pixel[1], f.hit[0], f.hit[1], f.hit[2], frame);
      }
    }
    if (f.drag == 1 && g_drag_before != 1) {
      g_drag_entry = f.entry;
      g_drag_sub_mesh = f.under;
      g_drag_start_row = f.pixel[1];
      g_drag_most = 0;
      g_drag_steps = 0;
      g_drag_start_frame = frame;
    }
    if (f.drag == 1 && g_drag_entry != nullptr &&
        !IsBadReadPtr(g_drag_entry, 0x24)) {
      const int rows = f.pixel[1] - g_drag_start_row;
      if (abs(rows) > abs(g_drag_most)) g_drag_most = rows;
      if (*reinterpret_cast<const int32_t*>(g_drag_entry + 0x1C) != 0) {
        ++g_drag_steps;
      }
    }
    if (f.drag != 1 && g_drag_before == 1 && g_under_logs < kMaxUnderLogs) {
      ++g_under_logs;
      ModLog("temple       : the drag of the control of sub mesh %d ends "
             "after %llu frames. The pointer went %d pixel rows from the "
             "press at the most, it is %d rows from it now, and the control "
             "got a step on %d frames, frame %llu", g_drag_sub_mesh,
             frame - g_drag_start_frame, g_drag_most,
             f.pixel[1] - g_drag_start_row, g_drag_steps, frame);
      g_drag_entry = nullptr;
    }
    if (f.drag != g_drag_before && g_under_logs < kMaxUnderLogs) {
      ++g_under_logs;
      ModLog("temple       : the room %s, frame %llu",
             f.drag == 1   ? "starts the drag of a control"
             : f.drag == 2 ? "starts a drag of its camera"
                           : "ends its drag",
             frame);
    }
    g_under_before = under;
    g_kind_before = f.kind;
    g_drag_before = f.drag;
  } else if (!g_scanning) {
    g_under_before = -2;
    g_kind_before = -1;
    g_drag_before = 0;
  }
  if (g_entry_log_frame != 0 && frame >= g_entry_log_frame) {
    g_entry_log_frame = 0;
    if (f.inside && f.object != nullptr) {
      // The control can be a new object after a click: a check box that is
      // on has a different class. So find the control of the sub mesh again.
      int kind = temple_math::kControlNone;
      uint8_t* now = EntryOf(f.object, g_entry_sub_mesh, &kind);
      if (now != nullptr) {
        ModLog("temple       : 40 frames after the press the control of sub "
               "mesh %d is %s", g_entry_sub_mesh,
               temple_math::ControlKindName(kind));
        LogEntry("40 frames after the press", now);
      }
    }
  }
  WatchCamera(f);
  ScanFrame(f);
  WatchLand();
  WatchSaves(f);
  TestEnter(f);
}

bool TempleRayDistance(const float origin[3], const float direction[3],
                       const float eye[3], float units, float* along) {
  if (!g_ray_on || !(units > 0.0f)) return false;
  const Facts& f = g_facts;
  const float least = 0.3f * units, most = 60.0f * units;
  const float fallback = g_depth_cm * 0.01f * units;
  bool ok = false;
  float hit_along = 0.0f;
  if (f.have_hit && !g_scanning) {
    const float from_eye = temple_math::Distance(f.hit, eye);
    const bool near_ray =
        !g_have_asked ||
        temple_math::AngleAtEye(eye, f.hit, g_asked) < kStaleDegrees;
    ok = from_eye >= least && from_eye <= most && near_ray &&
         temple_math::RayToHitPlane(origin, direction, eye, f.hit, &hit_along);
  }
  const float s =
      temple_math::DepthStep(&g_depth, ok, hit_along, fallback, least, most);
  ++g_ray_frames;
  if (g_depth.source == temple_math::kDepthHit) ++g_hit_frames;
  else if (g_depth.source == temple_math::kDepthKept) ++g_kept_frames;
  else ++g_default_frames;
  for (int i = 0; i < 3; ++i) {
    g_asked[i] = origin[i] + direction[i] * s;
    g_ray_origin[i] = origin[i];
    g_ray_direction[i] = direction[i];
    g_ray_eye[i] = eye[i];
  }
  g_have_asked = true;
  g_have_ray = true;
  g_ray_units = units;

  // The ray at rest: one line after 20 frames with the same direction. It
  // says how far the thing under the pointer is from the ray.
  const float same = temple_math::Dot(direction, g_rest_direction);
  if (same > 0.9999999f) {
    ++g_rest_frames;
  } else {
    g_rest_frames = 0;
    for (int i = 0; i < 3; ++i) g_rest_direction[i] = direction[i];
  }
  if (g_rest_frames == 20 && g_rest_logs < kMaxRestLogs && !g_scanning) {
    ++g_rest_logs;
    char under[96];
    UnderText(f, under, sizeof(under));
    ModLog("temple       : the ray is at rest in %s. It ends %.2f m from "
           "the hand (%s), at %.2f %.2f %.2f in the world. The pointer of "
           "the game is at the pixel %d %d, on %s. The point under it is "
           "%.2f %.2f %.2f, which is %.1f cm from the ray and %.1f cm from "
           "its end, frame %llu", RoomName(f.room), s / units,
           SourceName(g_depth.source), g_asked[0], g_asked[1], g_asked[2],
           f.pixel[0], f.pixel[1], under, f.hit[0], f.hit[1], f.hit[2],
           HitFromRayCm(),
           temple_math::Distance(f.hit, g_asked) / units * 100.0f, g_frame);
  }
  *along = s;
  return true;
}

void TempleReport() {
  if (g_ray_frames == 0 && g_messages == 0 && g_camera_moves == 0) {
    g_neutral_frames = 0;
    return;
  }
  ModLog("temple       : the ray was on for %llu frames this window: %llu "
         "with the hit of the game, %llu with the depth of before, %llu with "
         "the depth of the settings. The Temple got %llu button messages, "
         "the control under the pointer changed %llu times, the camera of a "
         "room moved %llu times, the camera got the pixel of the press in "
         "the place of a pointer that moved on %llu frames, and it got the "
         "middle of the view as the pointer on %llu frames", g_ray_frames, g_hit_frames, g_kept_frames,
         g_default_frames, g_messages, g_under_changes, g_camera_moves,
         g_drags_stopped, g_neutral_frames);
  g_ray_frames = g_hit_frames = g_kept_frames = g_default_frames = 0;
  g_messages = g_under_changes = g_camera_moves = g_drags_stopped = 0;
  g_neutral_frames = 0;
}

int TempleLandHint(MiracleLine* lines) {
  void* object = nullptr;
  float position[3];
  if (!HandDirectNearObject(&object, position)) return 0;
  const int thing = LandThingOf(object);
  if (thing < 0) return 0;
  lines[0].word = kWordGrab;
  if (thing == 0) {
    lines[0].text = L"Tap to go into the Temple";
    return 1;
  }
  static const wchar_t* const kTap[3] = {L"Tap for the Leash of Aggression",
                                         L"Tap for the Leash of Learning",
                                         L"Tap for the Leash of Compassion"};
  static const wchar_t* const kChosen[3] = {
      L"The Leash of Aggression is chosen", L"The Leash of Learning is chosen",
      L"The Leash of Compassion is chosen"};
  if (ChosenPost() == thing - 1) {
    lines[0].word = -1;
    lines[0].text = kChosen[thing - 1];
  } else {
    lines[0].text = kTap[thing - 1];
  }
  return 1;
}

bool TempleTestPlace(int place, float out[3]) {
  const uint64_t now = ControllersFrameCount();
  const LandThing* thing = &g_door;
  if (place != 0) thing = &g_post[g_test_post++ % 3];
  if (thing->object == nullptr || now - thing->frame > 4) return false;
  for (int k = 0; k < 3; ++k) out[k] = thing->position[k];
  return true;
}

bool TempleTestPixel(float* x, float* y) {
  if (!g_scanning) return false;
  const uint64_t since = g_frame - g_scan_start;
  int cell = static_cast<int>(since / kScanFrames);
  if (cell >= g_scan_grid * g_scan_grid) cell = g_scan_grid * g_scan_grid - 1;
  ScanPixel(cell, x, y);
  return true;
}

// temple_test_targets: words with a space between them. A word is a letter
// and a number: b button, c check box, s scroll, p picture, l list, t text
// line, and the number counts the controls of that kind that the scan found,
// from 0. "i12" is the control with the sub mesh 12.
bool TempleTestTarget(float world[3]) {
  if (g_found_count == 0 || g_targets[0] == 0) return false;
  // The word of this call.
  const char* p = g_targets;
  int index = 0;
  char word[16] = "";
  for (;;) {
    while (*p == ' ') ++p;
    if (*p == 0) return false;
    const char* end = p;
    while (*end != 0 && *end != ' ') ++end;
    if (index == g_targets_used) {
      const size_t length = static_cast<size_t>(end - p);
      if (length == 0 || length >= sizeof(word)) return false;
      memcpy(word, p, length);
      word[length] = 0;
      break;
    }
    ++index;
    p = end;
  }
  ++g_targets_used;
  const int number = atoi(word + 1);
  int seen = 0;
  // "e0": a picture of the Save Game Room with a slot of the player (0 to
  // 13) that had no saved game when the test first aimed at it. The test
  // keeps that slot, so the same word finds the picture again after a save.
  // A slot with a saved game from before the run is never a target.
  static int kept_slot = -1;
  for (int i = 0; i < g_found_count; ++i) {
    const Found& found = g_found[i];
    bool match = false;
    switch (word[0]) {
      case 'e':
        if (found.kind != temple_math::kControlPicture || found.slot < 0 ||
            found.slot > 13) {
          break;
        }
        if (kept_slot >= 0) {
          match = found.slot == kept_slot;
        } else if (!SlotHasSave(found.slot)) {
          if (seen++ == number) {
            kept_slot = found.slot;
            ModLog("temple test  : the test takes slot %d: it has no saved "
                   "game now, frame %llu", kept_slot, g_frame);
            match = true;
          }
        }
        break;
      case 'i': match = found.id == number; break;
      case 'b': match = found.kind == temple_math::kControlButton; break;
      case 'c':
        match = found.kind == temple_math::kControlCheckOff ||
                found.kind == temple_math::kControlCheckOn;
        break;
      case 's':
        match = found.kind == temple_math::kControlScroll ||
                found.kind == temple_math::kControlSlider;
        break;
      case 'p': match = found.kind == temple_math::kControlPicture; break;
      case 'l': match = found.kind == temple_math::kControlList; break;
      case 't': match = found.kind == temple_math::kControlText; break;
      default: break;
    }
    if (!match) continue;
    if (word[0] != 'i' && word[0] != 'e' && seen++ != number) continue;
    for (int k = 0; k < 3; ++k) world[k] = found.world[k];
    ModLog("temple test  : the target \"%s\" is %s, sub mesh %d, at %.2f "
           "%.2f %.2f in the world, frame %llu", word,
           temple_math::ControlKindName(found.kind), found.id, world[0],
           world[1], world[2], g_frame);
    return true;
  }
  ModLog("temple test  : the scan found no control for the target \"%s\", "
         "frame %llu", word, g_frame);
  return false;
}
