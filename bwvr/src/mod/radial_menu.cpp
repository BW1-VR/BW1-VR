// The ring. See docs/controllers.md, "The ring", and docs/controls-audit.md.
//
// WHY. The plain game needs the keyboard for the Temple and its rooms, the
// Creature, the kinds of the leash, the bookmarks, the pause and more
// (docs/controls-audit.md, section 7). A player in a headset has no keyboard.
// The controller of the hand that does not point had three inputs with no
// job: its first face button (X), its stick and its trigger.
//
// WHAT. A ring of 8 items opens at that hand. It is one picture in the room
// (a quad layer, as the hint cards), so it is sharp and it is never double.
// It stays where it opened, and the hand moves over it. There are two ways
// to use it (radial_menu_math.h):
//
//   Hold the button, move the hand toward an item, let go of the button.
//   Push the stick toward an item, pull the trigger of the same hand.
//
// A release in the middle, and a stick that rests in the middle for half a
// second, choose nothing.
//
// HOW AN ITEM ACTS. Each item is a key of the plain game. The mod writes the
// key into the key buffer of the game (GKeyBuffer::AddKey 0x005E1BB0), and
// the game reads it at its own point of the frame with GGame::ProcessKey
// 0x0063EF20. So each gate of the game holds: a dialog box, a cut scene, the
// Temple, the tutorial. The key needs no window in front, and it does not
// touch the key table, so a Ctrl of the ring does not start the zoom of the
// camera. For an action of the control map the mod reads the key that the
// player gave it in the Options Room.
//
// THE KEYS of bwvr.ini:
//
//   radial_menu=1              0 turns the ring off
//   radial_menu_button=1       what opens the ring with a hold: 0 nothing,
//                              1 the first face button (X), 2 the stick click,
//                              3 the trigger
//   radial_menu_stick=1        1 the stick opens the ring and points, and the
//                              trigger chooses
//   radial_menu_items          the 8 items of the first page, from the top
//                              and clockwise, with commas
//   radial_menu_items2         the second page. "more" changes the page
//   radial_menu_items_temple   the ring in the Temple
//   radial_menu_reach_cm=5     how far the hand moves to pick an item
//   radial_menu_size_pct=100   the size of the ring, 50 to 200
//   radial_menu_creature=1     the item "creature" in the God view: 1 one
//                              teleport to the Creature, 0 the key of the
//                              game, which starts its follow camera
//
// With test_keys=1, Ctrl and the 1 key of the number pad go through the two
// ways: both, the button only, the stick only. radial_test_save=N (a test
// key) saves the first N pictures of the ring as bwvr-ring-NNNNN.bmp.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../vr/xr_screen.h"
#include "audio.h"
#include "controllers.h"
#include "cutscene_window.h"
#include "haptics.h"
#include "hint_card_math.h"
#include "radial_menu.h"
#include "radial_menu_math.h"
#include "tabletop.h"

extern void ModLog(const char* format, ...);

// controllers.cpp: the frame count, and a pulse on the hands of the mask (bit
// 0 is the hand that does not point).
uint64_t ControllersFrameCount();
void ControllersGrabPulse(int hands, float amplitude, float seconds);
// eye_capture.cpp
void EyeCaptureSaveUi(const char* name, uint64_t frame, int style,
                      const void* pixels, int pitch, uint32_t width,
                      uint32_t height);

namespace {

using namespace radial_math;

// --- the settings ----------------------------------------------------------------

enum Button { kButtonNone, kButtonFace, kButtonStickClick, kButtonTrigger };

bool g_read = false;
bool g_on = true;
int g_button = kButtonFace;
bool g_stick = true;
int g_reach_cm = 5;
int g_size_pct = 100;
bool g_creature_teleport = true;
bool g_test_keys = false;
int g_save = 0;
int g_items[kPages][kSlots];
int g_pages = 1;
int g_temple_items[kPages][kSlots];
Settings g_settings = DefaultSettings();
// The test key: 0 both ways, 1 the button only, 2 the stick only.
int g_way = 0;

bool IniPath(char path[MAX_PATH]) {
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return false;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return false;
  slash[1] = 0;
  if (strlen(path) + 9 >= MAX_PATH) return false;
  strcat_s(path, MAX_PATH, "bwvr.ini");
  return true;
}

int IniKey(const char* key, int fallback) {
  char path[MAX_PATH];
  if (!IniPath(path)) return fallback;
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

void IniText(const char* key, const char* fallback, char* out, DWORD size) {
  char path[MAX_PATH];
  if (!IniPath(path)) {
    strcpy_s(out, size, fallback);
    return;
  }
  GetPrivateProfileStringA("bwvr", key, fallback, out, size, path);
  if (out[0] == 0) strcpy_s(out, size, fallback);
}

void Narrow(const wchar_t* in, char* out, size_t size) {
  size_t n = 0;
  for (; n + 1 < size && in[n] != 0; ++n) {
    out[n] = (in[n] >= 32 && in[n] < 127) ? static_cast<char>(in[n]) : '?';
  }
  out[n] = 0;
}

// The words of a page, for the log.
void PageWords(const int items[kSlots], char* out, size_t size) {
  out[0] = 0;
  for (int i = 0; i < kSlots; ++i) {
    if (i > 0) strncat_s(out, size, ", ", _TRUNCATE);
    strncat_s(out, size, kItemInfo[items[i]].id, _TRUNCATE);
  }
}

void ReadPage(const char* key, const char* fallback, int out[kSlots]) {
  char text[256];
  IniText(key, fallback, text, sizeof(text));
  char unknown[32];
  const int bad = ParseItems(text, out, unknown, sizeof(unknown));
  if (bad > 0) {
    ModLog("ring         : %s has %d word%s that the ring does not know (the "
           "first one is \"%s\"). Each such slot is empty", key, bad,
           bad == 1 ? "" : "s", unknown);
  }
}

void ApplyWay() {
  g_settings.button = g_button != kButtonNone && g_way != 2;
  // The trigger cannot open the ring and choose with the stick too.
  g_settings.stick = g_stick && g_button != kButtonTrigger && g_way != 1;
}

void ReadSettings() {
  if (g_read) return;
  g_read = true;
  g_on = IniKey("radial_menu", 1) != 0;
  g_button = IniKey("radial_menu_button", kButtonFace);
  if (g_button < kButtonNone || g_button > kButtonTrigger) {
    g_button = kButtonFace;
  }
  g_stick = IniKey("radial_menu_stick", 1) != 0;
  g_reach_cm = IniKey("radial_menu_reach_cm", 5);
  if (g_reach_cm < 2) g_reach_cm = 2;
  if (g_reach_cm > 15) g_reach_cm = 15;
  g_size_pct = IniKey("radial_menu_size_pct", 100);
  if (g_size_pct < 50) g_size_pct = 50;
  if (g_size_pct > 200) g_size_pct = 200;
  g_creature_teleport = IniKey("radial_menu_creature", 1) != 0;
  g_test_keys = IniKey("test_keys", 0) != 0;
  g_save = IniKey("radial_test_save", 0);
  g_settings.hand_in = g_reach_cm * 0.01f;
  g_settings.hand_out = g_reach_cm * 0.007f;
  ApplyWay();
  if (!g_on) {
    ModLog("ring         : radial_menu=0, the ring is off");
    return;
  }
  ReadPage("radial_menu_items", kDefaultPage1, g_items[0]);
  ReadPage("radial_menu_items2", kDefaultPage2, g_items[1]);
  ReadPage("radial_menu_items_temple", kDefaultTemple, g_temple_items[0]);
  for (int i = 0; i < kSlots; ++i) g_temple_items[1][i] = kItemNone;
  g_pages = PageHasItems(g_items[1]) ? 2 : 1;
  static const char* const kButtonWords[4] = {
      "no button opens it", "the first face button of the hand that does not "
      "point opens it (X)", "the stick click of that hand opens it",
      "the trigger of that hand opens it"};
  char a[200], b[200], c[200];
  PageWords(g_items[0], a, sizeof(a));
  PageWords(g_items[1], b, sizeof(b));
  PageWords(g_temple_items[0], c, sizeof(c));
  ModLog("ring         : radial_menu=1. radial_menu_button=%d: %s, the hand "
         "picks an item after %d cm, and the release chooses. "
         "radial_menu_stick=%d: %s. The size is %d percent",
         g_button, kButtonWords[g_button], g_reach_cm, g_stick ? 1 : 0,
         g_settings.stick ? "the stick of that hand opens the ring and points, "
                            "and its trigger chooses"
                          : "the stick does not open the ring",
         g_size_pct);
  ModLog("ring         : page 1: %s", a);
  if (g_pages > 1) ModLog("ring         : page 2: %s", b);
  ModLog("ring         : in the Temple: %s", c);
}

// --- the game --------------------------------------------------------------------

const uintptr_t kGame = 0x00D0195C;
// GGame.
const int kGameNoKeys = 0x0A;          // bit 0: the game takes no keys now
const int kGameFlags = 0x14;           // bit 2 pause, 0x20000 the names
const int kGamePlayers = 0x18;
const int kGamePlayerSize = 0xA60;
const int kGameTemple = 0x59B4;        // the Temple object
const int kGameInTemple = 0x205A28;    // 1 while the player is in the Temple
const int kGameLocalPlayer = 0x205A59;
const int kGameSpeed = 0x205B74;       // a float, 0.5 to 2
const int kGameHelp = 0x25005C;        // the HelpSystem
const int kGameKeyBuffer = 0x2502A8;   // the GKeyBuffer
const int kGameControlMap = 0x250300;  // the ControlMap
const int kPlayerCreature = 0xA4C;
// HelpSystem: both are not 0 while a cut scene plays, and GGame::DoAction
// then does nothing.
const int kHelpCinemaA = 0x45E8;
const int kHelpCinemaB = 0x45EC;
// ControlMap: one record of 0x30C bytes for each action.
const int kMapRecord = 0x30C;
const int kMapKey = 0x100;
const int kMapKeyMod = 0x184;
const int kMapMoves = 0x652C;          // 0: the tutorial has the moves off
const int kMapJumps = 0x6530;          // 0: and the keys of the Temple
// GKeyBuffer: the size and the count, as words.
const int kBufferSize = 0x0C;
const int kBufferCount = 0x0E;
// GKeyBuffer::AddKey, thiscall (key, modifiers), ret 8.
const uintptr_t kAddKey = 0x005E1BB0;
// LHKeyboard::CriticalSection: GGame::ProcessGameInputs holds it while it
// reads the buffer.
const uintptr_t kKeyboardLock = 0x00E8C118;
// GGame::MyInterface, thiscall, no arguments.
const uintptr_t kMyInterface = 0x00555850;
const int kFaceStatus = 0x39C;         // the local GInterfaceStatus
const int kStatusLeash = 0x12C;        // its GLeashStatus
const int kLeashOn = 0x14;
const int kLeashKind = 0x1C;           // 1 to 3
const int kCreatureLeashes = 0x1108;   // + 4 * kind: 1 when it has that kind
// A GameThingWithPos: its map point, in 1/6553.6 of a unit.
const int kThingX = 0x14;
const int kThingZ = 0x18;
const float kMapUnit = 0.00015258789f;
// The bookmarks: a record of 0x24 bytes for each, with bit 0 of the byte at
// +4 set while it is set. The keys act only while kBookmarksOn is not 0.
const uintptr_t kBookmarks = 0x00C58B30;
const int kBookmarkSize = 0x24;
const uintptr_t kBookmarksOn = 0x009CD384;
// The Temple object: its 7 rooms, and the room now.
const int kTempleRoomNow = 0x1C;
const char* const kRoomName[7] = {
    "the hall", "the Creature Room", "the Challenge Room",
    "the room of the multiplayer game", "the Options Room",
    "the Save Game Room", "the Library"};

typedef void*(__fastcall* MyInterfaceFn)(void* self, void* unused);
typedef int(__fastcall* AddKeyFn)(void* self, void* unused, int key,
                                  int modifiers);

struct Facts {
  uint8_t* game;
  bool temple;
  int room;            // 0 to 6, or -1
  bool paused;
  float speed;
  bool names;
  bool details;
  bool cutscene;
  bool keys_off;
  bool moves_on;
  bool jumps_on;
  void* creature;
  bool leash_known;
  bool leash_on;
  int leash_kind;
  bool leash_has[4];
  bool marks_on;
  bool mark[kMarks];
};

bool ReadFacts(Facts* f) {
  memset(f, 0, sizeof(*f));
  f->room = -1;
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return false;
  uint8_t* game = static_cast<uint8_t*>(*slot);
  if (IsBadReadPtr(game + kGameControlMap, 8)) return false;
  f->game = game;
  f->keys_off = (game[kGameNoKeys] & 1) != 0;
  const uint32_t flags = *reinterpret_cast<const uint32_t*>(game + kGameFlags);
  f->paused = (flags & 4) != 0;
  f->names = (flags & 0x20000) != 0;
  f->details = (flags & 0x200000) != 0;
  f->speed = *reinterpret_cast<const float*>(game + kGameSpeed);
  f->temple = *reinterpret_cast<const int32_t*>(game + kGameInTemple) == 1;
  const uint8_t* temple =
      *reinterpret_cast<uint8_t* const*>(game + kGameTemple);
  if (f->temple && temple != nullptr &&
      !IsBadReadPtr(temple, kTempleRoomNow + 4)) {
    const void* now = *reinterpret_cast<void* const*>(temple + kTempleRoomNow);
    for (int i = 0; i < 7; ++i) {
      if (now != nullptr &&
          *reinterpret_cast<void* const*>(temple + 4 * i) == now) {
        f->room = i;
      }
    }
  }
  const uint8_t* help = *reinterpret_cast<uint8_t* const*>(game + kGameHelp);
  if (help != nullptr && !IsBadReadPtr(help + kHelpCinemaA, 8)) {
    f->cutscene =
        *reinterpret_cast<const int32_t*>(help + kHelpCinemaA) != 0 &&
        *reinterpret_cast<const int32_t*>(help + kHelpCinemaB) != 0;
  }
  const uint8_t* map =
      *reinterpret_cast<uint8_t* const*>(game + kGameControlMap);
  f->moves_on = f->jumps_on = true;
  if (map != nullptr && !IsBadReadPtr(map + kMapMoves, 8)) {
    f->moves_on = *reinterpret_cast<const int32_t*>(map + kMapMoves) != 0;
    f->jumps_on = *reinterpret_cast<const int32_t*>(map + kMapJumps) != 0;
  }
  const uint8_t* player =
      game + kGamePlayers + kGamePlayerSize * game[kGameLocalPlayer];
  f->creature = *reinterpret_cast<void* const*>(player + kPlayerCreature);
  if (f->creature != nullptr &&
      IsBadReadPtr(f->creature, kCreatureLeashes + 16)) {
    f->creature = nullptr;
  }
  if (f->creature != nullptr) {
    const uint8_t* creature = static_cast<const uint8_t*>(f->creature);
    for (int kind = 1; kind <= 3; ++kind) {
      f->leash_has[kind] = *reinterpret_cast<const int32_t*>(
                               creature + kCreatureLeashes + 4 * kind) == 1;
    }
  }
  uint8_t* face = static_cast<uint8_t*>(
      reinterpret_cast<MyInterfaceFn>(kMyInterface)(game, nullptr));
  if (face != nullptr && !IsBadReadPtr(face + kFaceStatus, 4)) {
    const uint8_t* status =
        *reinterpret_cast<uint8_t* const*>(face + kFaceStatus);
    if (status != nullptr && !IsBadReadPtr(status + kStatusLeash, 4)) {
      const uint8_t* leash =
          *reinterpret_cast<uint8_t* const*>(status + kStatusLeash);
      if (leash != nullptr && !IsBadReadPtr(leash, kLeashKind + 4)) {
        f->leash_known = true;
        f->leash_on = *reinterpret_cast<const int32_t*>(leash + kLeashOn) != 0;
        f->leash_kind = *reinterpret_cast<const int32_t*>(leash + kLeashKind);
      }
    }
  }
  f->marks_on = *reinterpret_cast<const int32_t*>(kBookmarksOn) != 0;
  for (int i = 0; i < kMarks; ++i) {
    f->mark[i] = (*reinterpret_cast<const uint8_t*>(
                      kBookmarks + kBookmarkSize * i + 4) & 1) != 0;
  }
  return true;
}

const char* KeyName(int key) {
  switch (key) {
    case 0x01: return "Esc";
    case 0x0E: return "Backspace";
    case 0x0F: return "Tab";
    case 0x19: return "P";
    case 0x39: return "Space";
    case 0x3C: return "F2";
    default: break;
  }
  static char text[16];
  if (key >= 0x02 && key <= 0x0A) {
    sprintf_s(text, "%d", key - 1);
  } else if (key >= 0x3B && key <= 0x44) {
    sprintf_s(text, "F%d", key - 0x3A);
  } else {
    sprintf_s(text, "scan code 0x%02X", key);
  }
  return text;
}

const char* ModsName(int mods) {
  if (mods == kModCtrl) return "Ctrl and ";
  if (mods == kModAlt) return "Alt and ";
  if (mods == kModShift) return "Shift and ";
  if (mods == (kModCtrl | kModShift)) return "Ctrl, Shift and ";
  return "";
}

// --- the log ---------------------------------------------------------------------

int g_logs = 0;
const int kMaxLogs = 400;
bool LogRoom() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

uint64_t g_opens = 0, g_choices = 0, g_refusals = 0, g_keys_sent = 0;
uint64_t g_blocked = 0, g_no_choice = 0;
// The cost of the picture, for the report: how often it was drawn, and the
// longest draw and the longest fade step in milliseconds.
uint64_t g_rasters = 0;
double g_raster_ms = 0.0;
double g_stage_ms = 0.0;

double NowSeconds() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return static_cast<double>(count.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

// One line for each change of what the ring can change in the game. A log
// from a headset session then says what a choice did.
bool g_have_watch = false;
Facts g_watch;

void Watch(const Facts& f) {
  const uint64_t frame = ControllersFrameCount();
  if (!g_have_watch) {
    g_have_watch = true;
    g_watch = f;
    return;
  }
  const Facts& w = g_watch;
  if (f.temple != w.temple || f.room != w.room) {
    if (f.temple) {
      ModLog("ring         : the game: the player is in the Temple, in %s, "
             "frame %llu", f.room >= 0 ? kRoomName[f.room] : "no room yet",
             frame);
    } else {
      ModLog("ring         : the game: the player is out of the Temple, frame "
             "%llu", frame);
    }
  }
  if (f.paused != w.paused) {
    ModLog("ring         : the game: the pause is %s, frame %llu",
           f.paused ? "on" : "off", frame);
  }
  if (f.speed != w.speed) {
    ModLog("ring         : the game: the speed is %.2f (it was %.2f), frame "
           "%llu", f.speed, w.speed, frame);
  }
  if (f.names != w.names) {
    ModLog("ring         : the game: the villager names are %s, frame %llu",
           f.names ? "on" : "off", frame);
  }
  if (f.details != w.details) {
    ModLog("ring         : the game: the villager details are %s, frame %llu",
           f.details ? "on" : "off", frame);
  }
  if (f.leash_known && w.leash_known &&
      (f.leash_kind != w.leash_kind || f.leash_on != w.leash_on)) {
    ModLog("ring         : the game: the leash is kind %d and %s (it was kind "
           "%d and %s), frame %llu", f.leash_kind, f.leash_on ? "on" : "off",
           w.leash_kind, w.leash_on ? "on" : "off", frame);
  }
  for (int i = 0; i < kMarks; ++i) {
    if (f.mark[i] != w.mark[i]) {
      ModLog("ring         : the game: bookmark %d is %s, frame %llu", i + 1,
             f.mark[i] ? "set" : "not set", frame);
    }
  }
  if (f.cutscene != w.cutscene && LogRoom()) {
    ModLog("ring         : the game: a cut scene %s, frame %llu",
           f.cutscene ? "starts" : "ends", frame);
  }
  g_watch = f;
}

// --- the keys --------------------------------------------------------------------

struct KeyToSend {
  int key;
  int mods;
};
const int kMaxKeys = 8;
KeyToSend g_queue[kMaxKeys];
int g_queued = 0;

void QueueKey(int key, int mods) {
  if (g_queued >= kMaxKeys) return;
  g_queue[g_queued].key = key;
  g_queue[g_queued].mods = mods;
  ++g_queued;
}

// One key into the key buffer of the game. The game takes it from there at
// its next GGame::ProcessBufferedKeys, as it takes a key of the keyboard.
void SendKey(int key, int mods) {
  const uint64_t frame = ControllersFrameCount();
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) {
    ModLog("ring         : the key %s%s is dropped, there is no game, frame "
           "%llu", ModsName(mods), KeyName(key), frame);
    return;
  }
  uint8_t* game = static_cast<uint8_t*>(*slot);
  if ((game[kGameNoKeys] & 1) != 0) {
    ModLog("ring         : the key %s%s is dropped, the game takes no keys "
           "now, frame %llu", ModsName(mods), KeyName(key), frame);
    return;
  }
  uint8_t* buffer = game + kGameKeyBuffer;
  CRITICAL_SECTION* lock = reinterpret_cast<CRITICAL_SECTION*>(kKeyboardLock);
  EnterCriticalSection(lock);
  const int added =
      reinterpret_cast<AddKeyFn>(kAddKey)(buffer, nullptr, key, mods);
  const int count = *reinterpret_cast<const uint16_t*>(buffer + kBufferCount);
  const int size = *reinterpret_cast<const uint16_t*>(buffer + kBufferSize);
  LeaveCriticalSection(lock);
  if (added != 0) ++g_keys_sent;
  ModLog("ring         : the key %s%s %s the key buffer of the game (%d of %d "
         "keys), frame %llu", ModsName(mods), KeyName(key),
         added != 0 ? "goes into" : "does not fit in", count, size, frame);
}

// The key that the player gave an action in the Options Room. False when the
// action has no key.
bool ActionKey(const Facts& f, int action, int* key, int* mods) {
  const uint8_t* map =
      *reinterpret_cast<uint8_t* const*>(f.game + kGameControlMap);
  if (map == nullptr || action < 0 || action > 0x20) return false;
  const uint8_t* record = map + kMapRecord * action;
  if (IsBadReadPtr(record + kMapKeyMod, 4)) return false;
  *key = *reinterpret_cast<const int32_t*>(record + kMapKey);
  *mods = ModsOfKey(*reinterpret_cast<const int32_t*>(record + kMapKeyMod));
  return *key != 0;
}

// --- an item now -------------------------------------------------------------------

// What the card says under the name of an item, and whether the item can act
// now. An item that cannot act is dim on the ring.
struct ItemNow {
  bool can;
  const wchar_t* name;
  const wchar_t* note;   // empty for none
  const char* why;       // for the log, when it cannot act
};

int g_last_mark = -1;

ItemNow StateOf(int item, const Facts& f) {
  ItemNow now = {true, kItemInfo[item].name, L"", ""};
  const ItemInfo& info = kItemInfo[item];
  const auto no = [&now](const wchar_t* note, const char* why) {
    now.can = false;
    now.note = note;
    now.why = why;
  };
  if (item == kItemNone) {
    now.can = false;
    return now;
  }
  if (item == kItemMore) return now;
  if (f.game == nullptr) {
    no(L"No game runs now", "no game runs");
    return now;
  }
  if (f.keys_off) {
    no(L"The game takes no keys now", "the game takes no keys now");
    return now;
  }
  if (f.cutscene) {
    no(L"Not in a cut scene", "a cut scene plays, and the game does no "
                              "action then");
    return now;
  }
  if (info.how == kHowAction) {
    if (ActionIsOff(info.code, f.moves_on, f.jumps_on)) {
      no(L"The tutorial has this off", "the tutorial has this control off");
      return now;
    }
    int key = 0, mods = 0;
    if (!ActionKey(f, info.code, &key, &mods)) {
      no(L"No key in the game options",
         "the action has no key in the control map of the game");
      return now;
    }
  }
  switch (item) {
    case kItemCreature:
      if (f.creature == nullptr) {
        no(L"You have no Creature", "the player has no Creature");
      }
      break;
    case kItemLeashAggression:
    case kItemLeashLearning:
    case kItemLeashCompassion:
    case kItemLeashNext:
    case kItemLeashPrevious:
      if (f.creature == nullptr) {
        no(L"You have no Creature", "the player has no Creature");
      } else if (info.how == kHowLeash) {
        if (!f.leash_has[info.code]) {
          no(L"Your Creature does not have it",
             "the Creature does not have this kind of leash");
        } else if (f.leash_known && f.leash_kind == info.code) {
          now.note = f.leash_on ? L"It is on now" : L"It is the leash now";
        }
      }
      break;
    case kItemMarkSet:
    case kItemMarkGo:
    case kItemMarkBack:
    case kItemTown:
      if (!f.marks_on || !f.moves_on || !f.jumps_on) {
        no(L"The game has this off now", "the game has the bookmarks off now");
      } else if (item == kItemMarkGo) {
        bool any = false;
        for (int i = 0; i < kMarks; ++i) any = any || f.mark[i];
        if (!any) no(L"No bookmark is set", "no bookmark is set");
      }
      break;
    case kItemPause:
      if (f.paused) now.name = L"Go on with the game";
      break;
    case kItemSlower:
      if (f.speed <= 0.5f) no(L"The game is at its slowest",
                              "the speed is 0.5, the least");
      break;
    case kItemFaster:
      if (f.speed >= 2.0f) no(L"The game is at its fastest",
                              "the speed is 2.0, the most");
      break;
    case kItemNames:
      now.note = f.names ? L"They show now" : L"";
      break;
    case kItemDetails:
      now.note = f.details ? L"They show now" : L"";
      break;
    case kItemLeave:
      if (f.temple && f.room > 0) now.name = L"Back to the hall";
      break;
    case kItemHall:
    case kItemRoomCreature:
    case kItemRoomChallenge:
    case kItemRoomSave:
    case kItemRoomOptions:
    case kItemRoomLibrary: {
      // The room of each item (TempleRoomsEnum).
      const int room = item == kItemHall            ? 0
                       : item == kItemRoomCreature  ? 1
                       : item == kItemRoomChallenge ? 2
                       : item == kItemRoomSave      ? 5
                       : item == kItemRoomOptions   ? 4
                                                    : 6;
      if (f.temple && f.room == room) {
        no(L"You are here", "the player is in this room");
      }
      break;
    }
    default:
      break;
  }
  return now;
}

// The player chose an item. True when its keys are queued.
bool Choose(int item, const Facts& f, const ItemNow& now) {
  const uint64_t frame = ControllersFrameCount();
  const ItemInfo& info = kItemInfo[item];
  if (!now.can) {
    ++g_refusals;
    ModLog("ring         : the player chose \"%s\", and it does not act: %s, "
           "frame %llu", info.id, now.why, frame);
    return false;
  }
  int key = 0, mods = 0;
  switch (info.how) {
    case kHowAction:
      // THE CREATURE IN THE GOD VIEW. The key of the game starts its follow
      // camera, and the God view stands aside for that camera. One teleport
      // to the Creature keeps the God view, and nothing moves after it.
      if (item == kItemCreature && g_creature_teleport &&
          TabletopGodOwnsView() && f.creature != nullptr) {
        const uint8_t* creature = static_cast<const uint8_t*>(f.creature);
        const float target[3] = {
            *reinterpret_cast<const int32_t*>(creature + kThingX) * kMapUnit,
            0.0f,
            *reinterpret_cast<const int32_t*>(creature + kThingZ) * kMapUnit};
        if (TabletopGodTeleport(target)) {
          ++g_choices;
          ModLog("ring         : the player chose \"creature\": one teleport "
                 "of the God view to the Creature at %.1f %.1f "
                 "(radial_menu_creature=1), frame %llu", target[0], target[2],
                 frame);
          return true;
        }
      }
      ActionKey(f, info.code, &key, &mods);
      break;
    case kHowKey:
      key = info.code;
      mods = info.mods;
      break;
    case kHowLeash: {
      const int action = LeashAction(f.leash_known ? f.leash_kind : 0,
                                     info.code);
      if (action < 0) {
        ++g_choices;
        ModLog("ring         : the player chose \"%s\": the leash has this "
               "kind already (kind %d), so no key goes out, frame %llu",
               info.id, f.leash_kind, frame);
        return true;
      }
      if (!ActionKey(f, action, &key, &mods)) {
        ++g_refusals;
        ModLog("ring         : the player chose \"%s\", and it does not act: "
               "action 0x%02X has no key in the control map of the game, "
               "frame %llu", info.id, action, frame);
        return false;
      }
      break;
    }
    case kHowMarkSet: {
      const int index = MarkToSet(f.mark, g_last_mark);
      g_last_mark = index;
      key = 2 + index;
      mods = kModCtrl;
      break;
    }
    default:
      return false;
  }
  ++g_choices;
  const int presses = info.presses > 1 ? info.presses : 1;
  for (int i = 0; i < presses; ++i) QueueKey(key, mods);
  ModLog("ring         : the player chose \"%s\": the key %s%s, %d time%s, "
         "frame %llu", info.id, ModsName(mods), KeyName(key), presses,
         presses == 1 ? "" : "s", frame);
  return true;
}

// --- the picture -----------------------------------------------------------------

// 32 pixels are 1 cm at 100 percent, so the ring is 32 cm across, the items
// are 11 cm from its middle, and a capital letter is about 8 mm high.
const int kSize = 1024;
const float kPixelsPerMeter = 3200.0f;
const float kMiddle = 512.0f;
const float kSlotRadius = 352.0f;
const int kBadgeHeight = 64;
const int kBadgePad = 22;
const int kBadgeLeast = 150;
const float kBadgeRadius = 22.0f;
const int kBadgeEm = 34;
const int kNameEm = 38;
const int kNoteEm = 28;
const int kCardPad = 26;
const int kCardMost = 470;
const float kCardRadius = 24.0f;
const float kFadeIn = 0.10f;
const float kFadeOut = 0.20f;
// A choice that the game does not take shows its reason for this long.
const double kNoticeSeconds = 1.8;

struct Rgb {
  float r, g, b;
};
const Rgb kCardColor = {18.0f, 22.0f, 30.0f};
const Rgb kWhite = {255.0f, 255.0f, 255.0f};
const Rgb kLit = {236.0f, 240.0f, 246.0f};
const Rgb kLitInk = {16.0f, 18.0f, 22.0f};
const Rgb kDimInk = {150.0f, 156.0f, 166.0f};
const Rgb kDimLit = {120.0f, 126.0f, 136.0f};
const Rgb kNoteInk = {255.0f, 196.0f, 60.0f};

// What the picture shows. The picture is drawn again only when this changes.
struct Look {
  int item[kSlots];
  bool dim[kSlots];
  int slot;                // the choice, or -1
  wchar_t name[48];        // the card in the middle: empty for none
  wchar_t note[48];
};

HDC g_dc = nullptr;
HBITMAP g_dib = nullptr;
uint32_t* g_bits = nullptr;
HFONT g_badge_font = nullptr;
HFONT g_name_font = nullptr;
HFONT g_note_font = nullptr;
bool g_raster_failed = false;
uint8_t g_base[kSize * kSize * 4];
uint8_t g_staged[kSize * kSize * 4];
Look g_drawn;
bool g_have_drawn = false;
float g_staged_fade = -1.0f;
int g_saved = 0;

bool RasterStart() {
  if (g_dc != nullptr) return true;
  if (g_raster_failed) return false;
  g_dc = CreateCompatibleDC(nullptr);
  BITMAPINFO info;
  memset(&info, 0, sizeof(info));
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = kSize;
  info.bmiHeader.biHeight = -kSize;  // top first
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  if (g_dc != nullptr) {
    g_dib = CreateDIBSection(g_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  }
  const auto font = [](int em, int weight) {
    return CreateFontW(-em, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS,
                       L"Segoe UI");
  };
  g_badge_font = font(kBadgeEm, FW_BOLD);
  g_name_font = font(kNameEm, FW_SEMIBOLD);
  g_note_font = font(kNoteEm, FW_SEMIBOLD);
  if (g_dc == nullptr || g_dib == nullptr || bits == nullptr ||
      g_badge_font == nullptr || g_name_font == nullptr ||
      g_note_font == nullptr) {
    g_raster_failed = true;
    ModLog("ring         : GDI gave no picture for the ring, so the ring does "
           "not show. Its items still act");
    return false;
  }
  g_bits = static_cast<uint32_t*>(bits);
  SelectObject(g_dc, g_dib);
  SetBkMode(g_dc, TRANSPARENT);
  SetTextColor(g_dc, RGB(255, 255, 255));
  return true;
}

int TextWidth(HFONT font, const wchar_t* text) {
  SelectObject(g_dc, font);
  SIZE size = {0, 0};
  GetTextExtentPoint32W(g_dc, text, static_cast<int>(wcslen(text)), &size);
  return size.cx;
}

// How much of the pixel at px, py is inside a rect with round corners.
float RoundCover(float px, float py, float x0, float y0, float x1, float y1,
                 float radius) {
  const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
  const float qx = fabsf(px - cx) - (0.5f * (x1 - x0) - radius);
  const float qy = fabsf(py - cy) - (0.5f * (y1 - y0) - radius);
  const float ox = qx > 0.0f ? qx : 0.0f;
  const float oy = qy > 0.0f ? qy : 0.0f;
  const float inside = (qx > qy ? qx : qy) < 0.0f ? (qx > qy ? qx : qy) : 0.0f;
  const float d = sqrtf(ox * ox + oy * oy) + inside - radius;
  const float cover = 0.5f - d;
  return cover < 0.0f ? 0.0f : (cover > 1.0f ? 1.0f : cover);
}

// One round box of the picture: its fill, and the letters that GDI drew in
// it. Above split the letters have the color ink, and from split on ink2.
void Box(int x0, int y0, int x1, int y1, float radius, const Rgb& fill,
         float alpha, const Rgb& ink, int split, const Rgb& ink2) {
  const int left = x0 < 0 ? 0 : x0;
  const int top = y0 < 0 ? 0 : y0;
  const int right = x1 > kSize ? kSize : x1;
  const int bottom = y1 > kSize ? kSize : y1;
  for (int y = top; y < bottom; ++y) {
    const float py = static_cast<float>(y) + 0.5f;
    uint8_t* out = g_base + (static_cast<size_t>(y) * kSize + left) * 4;
    const uint32_t* letters = g_bits + static_cast<size_t>(y) * kSize;
    const Rgb& letter_ink = y >= split ? ink2 : ink;
    for (int x = left; x < right; ++x, out += 4) {
      const float cover = RoundCover(
          static_cast<float>(x) + 0.5f, py, static_cast<float>(x0),
          static_cast<float>(y0), static_cast<float>(x1),
          static_cast<float>(y1), radius);
      if (cover <= 0.0f) continue;
      Rgb color = fill;
      float a = alpha;
      const float letter =
          static_cast<float>((letters[x] >> 8) & 0xFF) * (1.0f / 255.0f);
      if (letter > 0.0f) {
        color.r += (letter_ink.r - color.r) * letter;
        color.g += (letter_ink.g - color.g) * letter;
        color.b += (letter_ink.b - color.b) * letter;
        a += (1.0f - a) * letter;
      }
      a *= cover;
      out[0] = static_cast<uint8_t>(color.b * a + 0.5f);
      out[1] = static_cast<uint8_t>(color.g * a + 0.5f);
      out[2] = static_cast<uint8_t>(color.r * a + 0.5f);
      out[3] = static_cast<uint8_t>(255.0f * a + 0.5f);
    }
  }
}

bool SameLook(const Look& a, const Look& b) {
  return memcmp(&a, &b, sizeof(Look)) == 0;
}

bool Raster(const Look& look) {
  if (!RasterStart()) return false;
  const double start = NowSeconds();
  memset(g_bits, 0, static_cast<size_t>(kSize) * kSize * 4);
  memset(g_base, 0, sizeof(g_base));
  // The letters first, white on black. The green of a pixel is its cover.
  RECT badge[kSlots];
  for (int i = 0; i < kSlots; ++i) {
    SetRectEmpty(&badge[i]);
    const wchar_t* word = kItemInfo[look.item[i]].word;
    if (look.item[i] == kItemNone || word[0] == 0) continue;
    int w = TextWidth(g_badge_font, word) + 2 * kBadgePad;
    if (w < kBadgeLeast) w = kBadgeLeast;
    float cx = 0.0f, cy = 0.0f;
    SlotPixel(i, kMiddle, kSlotRadius, &cx, &cy);
    badge[i].left = static_cast<LONG>(cx) - w / 2;
    badge[i].right = badge[i].left + w;
    badge[i].top = static_cast<LONG>(cy) - kBadgeHeight / 2;
    badge[i].bottom = badge[i].top + kBadgeHeight;
    SelectObject(g_dc, g_badge_font);
    RECT r = badge[i];
    DrawTextW(g_dc, word, -1, &r,
              DT_SINGLELINE | DT_NOPREFIX | DT_CENTER | DT_VCENTER |
                  DT_NOCLIP);
  }
  RECT card;
  SetRectEmpty(&card);
  int split = kSize;
  if (look.name[0] != 0) {
    const bool two = look.note[0] != 0;
    int w = TextWidth(g_name_font, look.name);
    if (two) {
      const int note_w = TextWidth(g_note_font, look.note);
      if (note_w > w) w = note_w;
    }
    w += 2 * kCardPad;
    if (w > kCardMost) w = kCardMost;
    const int h = two ? 124 : 80;
    card.left = static_cast<LONG>(kMiddle) - w / 2;
    card.right = card.left + w;
    card.top = static_cast<LONG>(kMiddle) - h / 2;
    card.bottom = card.top + h;
    RECT name = card;
    name.left += kCardPad - 8;
    name.right -= kCardPad - 8;
    if (two) {
      name.top += 14;
      name.bottom = name.top + 50;
      split = name.bottom;
    }
    SelectObject(g_dc, g_name_font);
    DrawTextW(g_dc, look.name, -1, &name,
              DT_SINGLELINE | DT_NOPREFIX | DT_CENTER | DT_VCENTER |
                  DT_END_ELLIPSIS);
    if (two) {
      RECT note = card;
      note.left += kCardPad - 8;
      note.right -= kCardPad - 8;
      note.top = split;
      note.bottom = card.bottom - 14;
      SelectObject(g_dc, g_note_font);
      DrawTextW(g_dc, look.note, -1, &note,
                DT_SINGLELINE | DT_NOPREFIX | DT_CENTER | DT_VCENTER |
                    DT_END_ELLIPSIS);
    }
  }
  GdiFlush();

  for (int i = 0; i < kSlots; ++i) {
    if (IsRectEmpty(&badge[i])) continue;
    const bool lit = i == look.slot;
    const bool dim = look.dim[i];
    const Rgb& fill = lit ? (dim ? kDimLit : kLit) : kCardColor;
    const float alpha = lit ? 1.0f : (dim ? 0.50f : 0.88f);
    const Rgb& ink = lit ? kLitInk : (dim ? kDimInk : kWhite);
    Box(badge[i].left, badge[i].top, badge[i].right, badge[i].bottom,
        kBadgeRadius, fill, alpha, ink, kSize, ink);
  }
  if (!IsRectEmpty(&card)) {
    Box(card.left, card.top, card.right, card.bottom, kCardRadius, kCardColor,
        0.92f, kWhite, split, kNoteInk);
  } else {
    // A small dot marks the middle, where a release chooses nothing.
    const int m = static_cast<int>(kMiddle);
    Box(m - 8, m - 8, m + 8, m + 8, 8.0f, kWhite, 0.55f, kWhite, kSize,
        kWhite);
  }
  g_drawn = look;
  g_have_drawn = true;
  g_staged_fade = -1.0f;
  ++g_rasters;
  const double ms = (NowSeconds() - start) * 1000.0;
  if (ms > g_raster_ms) g_raster_ms = ms;
  if (g_saved < g_save) {
    ++g_saved;
    EyeCaptureSaveUi("ring", ControllersFrameCount(), 1, g_base, kSize * 4,
                     kSize, kSize);
  }
  return true;
}

// The staged picture is the base picture at the fade of this frame.
void Stage(float fade) {
  if (g_staged_fade == fade) return;
  g_staged_fade = fade;
  const double start = NowSeconds();
  const int scale = static_cast<int>(fade * 256.0f + 0.5f);
  if (scale >= 256) {
    memcpy(g_staged, g_base, sizeof(g_staged));
  } else {
    const size_t count = sizeof(g_staged);
    for (size_t i = 0; i < count; ++i) {
      g_staged[i] = static_cast<uint8_t>((g_base[i] * scale) >> 8);
    }
  }
  const double ms = (NowSeconds() - start) * 1000.0;
  if (ms > g_stage_ms) g_stage_ms = ms;
}

// --- the frame -------------------------------------------------------------------

State g_state;
bool g_state_ready = false;
Plane g_plane;
bool g_have_plane = false;
bool g_left = true;
float g_fade = 0.0f;
double g_show_until = 0.0;       // a notice stays until then
bool g_notice = false;
Look g_notice_look;
bool g_was_temple = false;
// A button that is down when the hand wakes is not a press.
bool g_button_seen_up = false;
bool g_trigger_down = false;
bool g_key_down = false;         // the test key

const char* SlotWord(const int (*items)[kSlots], const State& st) {
  if (st.slot < 0) return "the middle";
  return kItemInfo[items[st.page][st.slot]].id;
}

// The test key: Ctrl and the 1 key of the number pad. A longer pulse for
// each next way.
void PollTestKey() {
  if (!g_test_keys) return;
  const bool down = (GetAsyncKeyState(VK_NUMPAD1) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool pressed = down && !g_key_down;
  g_key_down = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_way = (g_way + 1) % 3;
  ApplyWay();
  static const char* const kWays[3] = {
      "both ways open the ring: the button with the hand, and the stick with "
      "the trigger",
      "only the button opens the ring, and the hand picks",
      "only the stick opens the ring, and the trigger chooses"};
  ControllersGrabPulse(1, 0.5f, 0.08f + 0.11f * g_way);
  ModLog("ring         : Ctrl and numpad 1: %s, frame %llu", kWays[g_way],
         ControllersFrameCount());
}

void BuildLook(const int (*items)[kSlots], const State& st, const Facts& f,
               Look* look) {
  memset(look, 0, sizeof(*look));
  look->slot = st.slot;
  for (int i = 0; i < kSlots; ++i) {
    const int item = items[st.page][i];
    look->item[i] = item;
    look->dim[i] = item != kItemNone && !StateOf(item, f).can;
  }
  if (st.slot >= 0) {
    const ItemNow now = StateOf(items[st.page][st.slot], f);
    wcsncpy_s(look->name, now.name, _TRUNCATE);
    wcsncpy_s(look->note, now.note, _TRUNCATE);
  }
}

}  // namespace

bool RadialMenuOpen() { return g_state_ready && g_state.open != kClosed; }

int RadialMenuHintMode() {
  ReadSettings();
  if (!g_on) return 0;
  if (g_settings.button && g_button == kButtonFace) return 1;
  if (g_settings.stick) return 2;
  return 0;
}

void RadialMenuPump(unsigned long long) {
  if (g_queued == 0) return;
  const int count = g_queued;
  g_queued = 0;
  for (int i = 0; i < count; ++i) SendKey(g_queue[i].key, g_queue[i].mods);
}

void RadialMenuFrame() {
  ReadSettings();
  if (!g_on) return;
  if (!g_state_ready) {
    g_state_ready = true;
    Reset(&g_state);
  }
  PollTestKey();
  const uint64_t frame = ControllersFrameCount();
  const double now = NowSeconds();
  static double last_time = 0.0;
  float dt = static_cast<float>(now - last_time);
  if (last_time == 0.0 || dt > 0.25f) dt = 0.25f;
  last_time = now;

  RingHand hand;
  const bool have_hand = ControllersRingHand(&hand);
  Facts facts;
  const bool have_game = ReadFacts(&facts);
  if (have_game) Watch(facts);

  // The ring of the Temple has one page. A ring that is open when the player
  // goes in or out closes, because its items are not the items of the place.
  const bool temple = have_game && facts.temple;
  const int (*items)[kSlots] = temple ? g_temple_items : g_items;
  const int pages = temple ? 1 : g_pages;
  const bool place_changed = temple != g_was_temple;
  g_was_temple = temple;

  Input in;
  memset(&in, 0, sizeof(in));
  in.time = now;
  const char* why = "";
  const bool open_before = g_state.open != kClosed;
  if (!have_hand || !hand.active) {
    why = "the hand has no pose";
  } else if (!hand.usable) {
    why = "the controllers cannot act now";
  } else if (hand.dialog) {
    why = "a dialog box is up";
  } else if (!have_game) {
    why = "no game runs";
  } else if (place_changed) {
    why = "the player went into the Temple or out of it";
  } else if (facts.cutscene || CutsceneWindowFlat()) {
    // The game takes no action in a cut scene, and in the Temple it takes no
    // key at all then (GGame::ProcessKey). The cut scene plays on a screen,
    // and a frame of that screen has no ring. The menu button is still Esc.
    why = "a cut scene plays";
  } else if (!open_before && hand.grab_held) {
    why = "a grip holds the world";
  } else {
    in.can_open = true;
  }
  if (have_hand && hand.active) {
    bool raw = false;
    if (g_button == kButtonFace) raw = hand.button;
    if (g_button == kButtonStickClick) raw = hand.stick_click;
    if (g_button == kButtonTrigger) raw = hand.trigger > 0.55f;
    if (!raw) g_button_seen_up = true;
    in.button = raw && g_button_seen_up;
    g_trigger_down = g_trigger_down ? hand.trigger > 0.35f
                                    : hand.trigger > 0.55f;
    in.trigger = g_button != kButtonTrigger && g_trigger_down;
    if (hand.stick_live) {
      in.stick[0] = hand.stick[0];
      in.stick[1] = hand.stick[1];
    }
    if (open_before && g_have_plane) {
      OnPlane(g_plane, hand.position, in.hand);
      in.have_hand = true;
    }
  } else {
    g_button_seen_up = false;
    g_trigger_down = false;
  }

  Events ev;
  Step(&g_state, g_settings, in, items, pages, &ev);

  if (ev.blocked) {
    ++g_blocked;
    if (LogRoom()) {
      ModLog("ring         : the ring does not open: %s, frame %llu", why,
             frame);
    }
    HapticsPulseHands(1, haptics_math::kRefused);
  }
  if (ev.opened) {
    ++g_opens;
    g_left = hand.left;
    PlaneAt(hand.position, hand.head, &g_plane);
    g_have_plane = true;
    g_notice = false;
    g_show_until = 0.0;
    if (LogRoom()) {
      char words[200];
      PageWords(items[g_state.page], words, sizeof(words));
      ModLog("ring         : the ring opens at the %s hand (%s), at %.3f %.3f "
             "%.3f m, %.0f cm from the head. %s: %s, frame %llu",
             hand.left ? "left" : "right",
             g_state.open == kOpenButton ? "the button, and the hand picks"
                                         : "the stick, and the trigger chooses",
             g_plane.center[0], g_plane.center[1], g_plane.center[2],
             controller_math::Distance(g_plane.center, hand.head) * 100.0f,
             temple ? "The ring of the Temple" : "Page 1", words, frame);
    }
    HapticsPulseHands(1, haptics_math::kTick);
  }
  if (ev.paged && LogRoom()) {
    char words[200];
    PageWords(items[g_state.page], words, sizeof(words));
    ModLog("ring         : page %d: %s, frame %llu", g_state.page + 1, words,
           frame);
  }
  if (ev.moved && g_state.open != kClosed) {
    if (g_state.slot >= 0) {
      // A light pulse and a soft sound when the choice moves to an item.
      HapticsPulseHands(1, haptics_math::kTick);
      AudioCue(kAudioCueRingMove, g_left ? -0.4f : 0.4f);
    }
    if (LogRoom()) {
      if (g_state.open == kOpenButton) {
        ModLog("ring         : the choice is %s, with the hand %.1f cm to the "
               "right and %.1f cm up from the middle, frame %llu",
               SlotWord(items, g_state), in.hand[0] * 100.0f,
               in.hand[1] * 100.0f, frame);
      } else {
        ModLog("ring         : the choice is %s, with the stick at %.2f %.2f, "
               "frame %llu", SlotWord(items, g_state), in.stick[0],
               in.stick[1], frame);
      }
    }
  }
  if (ev.chosen >= 0) {
    const int item = items[ev.chosen_page][ev.chosen];
    const ItemNow item_now = StateOf(item, facts);
    if (Choose(item, facts, item_now)) {
      HapticsPulseHands(1, haptics_math::kActivate);
      AudioCue(kAudioCueRingChoose, g_left ? -0.4f : 0.4f);
    } else {
      // The ring stays for a moment and says why.
      HapticsPulseHands(1, haptics_math::kRefused);
      g_notice = true;
      g_show_until = now + kNoticeSeconds;
      memset(&g_notice_look, 0, sizeof(g_notice_look));
      for (int i = 0; i < kSlots; ++i) {
        g_notice_look.item[i] = items[ev.chosen_page][i];
        g_notice_look.dim[i] = true;
      }
      g_notice_look.slot = ev.chosen;
      wcsncpy_s(g_notice_look.name, item_now.name, _TRUNCATE);
      wcsncpy_s(g_notice_look.note,
                item_now.note[0] != 0 ? item_now.note : L"Not now",
                _TRUNCATE);
    }
  } else if (ev.closed) {
    ++g_no_choice;
    if (LogRoom()) {
      ModLog("ring         : the ring closes with no choice%s%s, frame %llu",
             in.can_open ? "" : ": ", in.can_open ? "" : why, frame);
    }
  }

  // The picture of this frame.
  const bool open = g_state.open != kClosed;
  const bool show = open || (g_notice && now < g_show_until);
  if (g_notice && !show) g_notice = false;
  if (open) {
    Look look;
    BuildLook(items, g_state, facts, &look);
    if (!g_have_drawn || !SameLook(look, g_drawn)) Raster(look);
  } else if (g_notice && (!g_have_drawn || !SameLook(g_notice_look, g_drawn))) {
    Raster(g_notice_look);
  }
  g_fade = hint_card_math::FadeStep(g_fade, show, dt,
                                    show ? kFadeIn : kFadeOut);
  if (g_fade <= 0.0f || !g_have_plane || !g_have_drawn || g_raster_failed ||
      bwvr::XrScreen::CardFailed(bwvr::kCardRing)) {
    return;
  }
  Stage(g_fade);
  const float size = kSize / kPixelsPerMeter * (g_size_pct * 0.01f);
  bwvr::CardLayer layer = {};
  layer.visible = true;
  layer.pixels = g_staged;
  layer.texture_width = kSize;
  layer.texture_height = kSize;
  layer.rect[0] = 0;
  layer.rect[1] = 0;
  layer.rect[2] = kSize;
  layer.rect[3] = kSize;
  for (int i = 0; i < 3; ++i) layer.position[i] = g_plane.center[i];
  for (int i = 0; i < 4; ++i) layer.orientation[i] = g_plane.orientation[i];
  layer.width = size;
  layer.height = size;
  bwvr::XrScreen::StageCard(bwvr::kCardRing, layer);
}

void RadialMenuReport() {
  if (g_opens + g_blocked == 0) return;
  ModLog("ring         : the ring opened %llu times this window: %llu "
         "choices, %llu choices that did not act, %llu times closed with no "
         "choice, %llu keys into the key buffer of the game. %llu times it "
         "did not open",
         g_opens, g_choices, g_refusals, g_no_choice, g_keys_sent, g_blocked);
  if (g_rasters > 0) {
    ModLog("ring         : the picture of the ring was drawn %llu times. The "
           "longest draw took %.2f ms, and the longest step of the fade %.2f "
           "ms", g_rasters, g_raster_ms, g_stage_ms);
  }
  g_opens = g_choices = g_refusals = g_no_choice = g_keys_sent = 0;
  g_blocked = 0;
  g_rasters = 0;
  g_raster_ms = g_stage_ms = 0.0;
}
