// Step M8: controller hints. See docs/controller-hints.md, "As built".
//
// The words come from one place, BuildWords, so the key caps (H1) and the
// help text (H2) always agree with the mapping of controllers.cpp and with
// the bindings of xr_screen.cpp.
#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wctype.h>

#include <string>

#include "MinHook.h"
#include "controllers.h"
#include "controller_hints.h"

extern void ModLog(const char* format, ...);

namespace {

bool g_enabled = true;
char g_forced[64] = {0};

// --- the words ---------------------------------------------------------------

enum Family { kTouch, kIndex, kWand, kMixed, kSimple };
const char* const kFamilyName[5] = {"touch like", "index", "wand",
                                    "mixed reality", "simple"};

// Each word is a string literal, so a pointer to it stays valid for good. A
// null word means the controller has no such input.
struct Words {
  Family family;
  const wchar_t* trigger;
  const wchar_t* grip;
  const wchar_t* stick;
  const wchar_t* stick_up;    // the word of the help text for "Wheel Up"
  const wchar_t* stick_down;
  const wchar_t* jump;        // the A button, or X on the left Touch hand
  const wchar_t* gesture;     // the button that draws a gesture
  const wchar_t* menu;        // the button that opens the game menu
  const wchar_t* world;       // the grip of the other hand: it moves the land
  const wchar_t* ring;        // the control of the other hand: it opens the ring
  const wchar_t* noun;        // "stick" or "pad", for a sentence
  const wchar_t* verb;        // "Push" or "Press"
  bool swap;
};

Family FamilyOf(const char* profile) {
  if (strstr(profile, "simple") != nullptr) return kSimple;
  if (strstr(profile, "index") != nullptr) return kIndex;
  if (strstr(profile, "vive") != nullptr) return kWand;
  if (strstr(profile, "microsoft") != nullptr) return kMixed;
  // Touch, the fake controller, an unknown profile and none yet.
  return kTouch;
}

// The one table. It follows the bindings of xr_screen.cpp (which controls a
// profile has) and the mapping of controllers.cpp (what each one does).
Words BuildWords(const ControllerHintState& state, const char* profile) {
  Words w;
  memset(&w, 0, sizeof(w));
  w.family = FamilyOf(profile);
  w.swap = state.swap_buttons;
  const bool left = state.left_handed;
  const Family f = w.family;
  w.trigger = (f == kSimple) ? L"Select" : L"Trigger";
  w.grip = (f == kSimple) ? nullptr : L"Grip";
  if (f == kWand) {
    // The stick moves the view only while the pad is clicked (StickLive).
    w.stick = L"Pad click";
    w.stick_up = L"Pad up";
    w.stick_down = L"Pad down";
    w.noun = L"pad";
    w.verb = L"Press";
  } else if (f != kSimple) {
    w.stick = L"Stick";
    w.stick_up = L"Stick up";
    w.stick_down = L"Stick down";
    w.noun = L"stick";
    w.verb = L"Push";
  }
  // Index has A and B on both hands. Touch has A and B on the right hand and
  // X and Y on the left one. The other profiles have no face buttons.
  if (f == kTouch) {
    w.jump = left ? L"X" : L"A";
  } else if (f == kIndex) {
    w.jump = L"A";
  }
  if (state.gesture_button == 1) {
    if (f == kTouch) w.gesture = left ? L"Y" : L"B";
    if (f == kIndex) w.gesture = L"B";
  } else if (state.gesture_button == 2 && w.stick != nullptr) {
    w.gesture = (f == kWand) ? L"Pad click" : L"Stick click";
  }
  // The game menu (controllers.cpp, LatchEsc): the second face button of the
  // hand that does not point, or the menu button of the left hand.
  if (f == kTouch) {
    w.menu = left ? L"B" : L"Y";
  } else if (f == kIndex) {
    w.menu = L"B";
  } else {
    w.menu = L"Menu";
  }
  // The grab of the world (controllers.cpp, UpdateGrab): the grip of the
  // hand that does not point.
  if (state.world_grab && f != kSimple) w.world = L"Grip";
  // The ring (radial_menu.cpp): the first face button of the hand that does
  // not point, or the stick of that hand when only the stick opens it.
  if (state.ring == 1) {
    if (f == kTouch) w.ring = left ? L"A" : L"X";
    if (f == kIndex) w.ring = L"A";
  }
  if (state.ring != 0 && w.ring == nullptr) w.ring = w.stick;
  return w;
}

Words g_words;
uint32_t g_signature = 0xFFFFFFFFu;
bool g_active = false;
bool g_latched = false;

uint32_t SignatureOf(const Words& w, const ControllerHintState& s) {
  return static_cast<uint32_t>(w.family) | (s.left_handed ? 0x10u : 0u) |
         (s.swap_buttons ? 0x20u : 0u) | (s.world_grab ? 0x100u : 0u) |
         (static_cast<uint32_t>(s.gesture_button & 3) << 6) |
         (static_cast<uint32_t>(s.ring & 3) << 9);
}

// --- the census (H0) ----------------------------------------------------------

struct CensusRow {
  uint32_t key;
  uint64_t count;
  uint64_t logged;
  char line[220];
};
const int kMaxCensus = 128;
CensusRow g_census[kMaxCensus];
int g_census_count = 0;
bool g_census_full = false;

uint32_t Mix(uint32_t h, uint32_t v) {
  h ^= v;
  h *= 16777619u;
  return h;
}

// The row of a known call, counted, or null for a call not seen yet.
CensusRow* CensusHit(uint32_t key) {
  for (int i = 0; i < g_census_count; ++i) {
    if (g_census[i].key == key) {
      ++g_census[i].count;
      return &g_census[i];
    }
  }
  return nullptr;
}

void CensusAdd(uint32_t key, const char* line) {
  if (g_census_count >= kMaxCensus) {
    if (!g_census_full) {
      g_census_full = true;
      ModLog("hints        : the census table is full, so new calls are not "
             "listed");
    }
    return;
  }
  CensusRow& row = g_census[g_census_count++];
  row.key = key;
  row.count = 1;
  row.logged = 1;
  strncpy_s(row.line, line, _TRUNCATE);
  ModLog("hints        : census %s", row.line);
}

// Plain ASCII of a wide string, for the log.
void Narrow(const wchar_t* text, char* out, size_t size) {
  size_t i = 0;
  if (text != nullptr && !IsBadReadPtr(text, 2)) {
    for (; i + 1 < size && text[i] != 0 && i < 60; ++i) {
      out[i] = (text[i] >= 32 && text[i] < 127) ? static_cast<char>(text[i])
                                                : '?';
    }
  }
  out[i] = 0;
}

uint32_t g_frames = 0;
uint64_t g_converted = 0;
uint64_t g_hidden = 0;
uint64_t g_edge_hidden = 0;

void CensusReport() {
  for (int i = 0; i < g_census_count; ++i) {
    CensusRow& row = g_census[i];
    if (row.count == row.logged) continue;
    row.logged = row.count;
    ModLog("hints        : census count %llu %s", row.count, row.line);
  }
}

// --- hook 1: the control map (fn_005C78F0) ------------------------------------
//
// int __cdecl(int action, int* kind, int* code, int* row_or_label). A mouse
// binding gives kind 0, the button code and the mouse row. A key gives kind
// -1 and a pointer to the key name.
typedef int(__cdecl* AskFn)(int action, int* kind, int* code, int* row);
AskFn o_Ask = nullptr;
const uintptr_t kAsk = 0x005C78F0;

int __cdecl h_Ask(int action, int* kind, int* code, int* row) {
  const uint32_t from = reinterpret_cast<uint32_t>(_ReturnAddress());
  const int result = o_Ask(action, kind, code, row);
  if (!result || kind == nullptr || code == nullptr || row == nullptr) {
    return result;
  }
  const int k = *kind;
  uint32_t key = Mix(2166136261u, 1);
  key = Mix(key, from);
  key = Mix(key, static_cast<uint32_t>(action));
  key = Mix(key, static_cast<uint32_t>(k));
  key = Mix(key, static_cast<uint32_t>(*code));
  if (k != -1) key = Mix(key, static_cast<uint32_t>(*row));
  if (CensusHit(key) != nullptr) return result;
  char line[220];
  if (k == -1) {
    char name[64];
    Narrow(reinterpret_cast<const wchar_t*>(*row), name, sizeof(name));
    _snprintf_s(line, _TRUNCATE,
                "ask from 0x%08X: action %d is the key \"%s\"", from, action,
                name);
  } else {
    _snprintf_s(line, _TRUNCATE,
                "ask from 0x%08X: action %d is mouse button code 0x%X, mouse "
                "row %d",
                from, action, static_cast<unsigned>(*code), *row);
  }
  CensusAdd(key, line);
  return result;
}

// --- hook 2: the picture of a hint (fn_00447450) --------------------------------
//
// void __cdecl with 7 arguments (every caller cleans 0x1C). The first three
// are the button code, the row of the mouse picture and the blink style. The
// screen edge hint calls it directly from 0x004478CD, with no text. It only
// tells the player about the mouse at the edge of the screen, so a controller
// hides it.
typedef void(__cdecl* PictureFn)(uint32_t a1, uint32_t a2, uint32_t a3,
                                 uint32_t a4, uint32_t a5, uint32_t a6,
                                 uint32_t a7);
PictureFn o_Picture = nullptr;
const uintptr_t kPicture = 0x00447450;
const uintptr_t kFromEdge = 0x004478D2;  // after the call at 0x004478CD

void __cdecl h_Picture(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                       uint32_t a5, uint32_t a6, uint32_t a7) {
  const uint32_t from = reinterpret_cast<uint32_t>(_ReturnAddress());
  if (from == kFromEdge) {
    uint32_t key = Mix(2166136261u, 2);
    key = Mix(key, a1);
    key = Mix(key, a2);
    key = Mix(key, a3);
    if (CensusHit(key) == nullptr) {
      char line[220];
      _snprintf_s(line, _TRUNCATE,
                  "edge picture from 0x%08X: button 0x%X, row %u, blink %u",
                  from, a1, a2, a3);
      CensusAdd(key, line);
    }
    // -1 is the game's own "nothing to draw".
    if (g_active && a1 != 0xFFFFFFFFu) {
      ++g_edge_hidden;
      return;
    }
  }
  o_Picture(a1, a2, a3, a4, a5, a6, a7);
}

// --- hook 3: the help text (H2) ---------------------------------------------------
//
// 0x005CAD00 stores one ADD_TEXT line: entry+0 and +4 are two numbers,
// entry+8 is a copy of the text made with the game's allocator. The key
// name of the line is the third argument. Nothing else reads the key, so the
// hook is the only place that can match on it.
//
// A line that the mod words differently gets a second copy of its text, made
// with the same allocator. The pointer at entry+8 switches between the two,
// once a frame, so the game's own free stays valid and the mouse words come
// back when the mouse owns the pointer. Nothing is written to disk and the
// mod ships no sentence of the game: only its own words and the key names.
typedef void(__fastcall* AddTextFn)(void* entry, void* edx, int a, int b,
                                    const wchar_t* key, const wchar_t* text);
AddTextFn o_AddText = nullptr;
const uintptr_t kAddText = 0x005CAD00;

typedef void*(__cdecl* GameNewFn)(unsigned size, const char* file, int line);
const uintptr_t kGameNew = 0x007DB790;
const uintptr_t kGameNewFile = 0x00BF1A2C;

enum TextKind {
  kLmb,
  kRmb,
  kWheelUp,
  kWheelDown,
  kKeyboard,   // "You can also use the keyboard ..."
  kZoomLong,   // a long line about the wheel, with the middle button in it
  kZoomShort,
  kPhrase,     // the words "mouse wheel" or "the wheel" in a longer line
};

struct KeyRow {
  const wchar_t* key;
  TextKind kind;
};
const KeyRow kKeys[] = {
    {L"HELP_TEXT_ACTIONS_LEFTMOUSEBUTTON", kLmb},
    {L"HELP_TEXT_ACTIONS_RIGHTMOUSEBUTTON", kRmb},
    {L"HELP_TEXT_ACTIONS_WHEEL_UP", kWheelUp},
    {L"HELP_TEXT_ACTIONS_WHEEL_DOWN", kWheelDown},
    {L"HELP_TEXT_HAND_DEMO_USE_KEYBOARD_01", kKeyboard},
    {L"HELP_TEXT_HAND_DEMO_USE_KEYBOARD_02", kKeyboard},
    {L"HELP_TEXT_HAND_DEMO_USE_KEYBOARD_03", kKeyboard},
    {L"HELP_TEXT_HAND_DEMO_USE_KEYBOARD_04", kKeyboard},
    {L"HELP_TEXT_TUTORIAL_LAND_108", kKeyboard},
    {L"HELP_TEXT_DYK_18", kZoomLong},
    {L"HELP_TEXT_JUST_IN_CASE_37", kZoomShort},
};

struct TextRow {
  void** slot;            // entry+8, the pointer the game reads
  wchar_t* original;      // the game's own copy
  wchar_t* alt;           // the copy with controller words, or null
  uint32_t alt_signature;
  TextKind kind;
  bool dead;              // the game changed the slot, so the mod leaves it
  wchar_t key[48];        // for the log
};
const int kMaxTexts = 96;
TextRow g_texts[kMaxTexts];
int g_text_count = 0;
bool g_text_full = false;
int g_key_logs = 0;
uint32_t g_texts_swapped = 0;

bool WordAt(const wchar_t* text, size_t at, const wchar_t* phrase,
            size_t length) {
  if (wcsncmp(text + at, phrase, length) != 0) return false;
  if (at > 0 && iswalpha(text[at - 1])) return false;
  return !iswalpha(text[at + length]);
}

bool HasPhrase(const wchar_t* text) {
  const size_t n = wcslen(text);
  for (size_t i = 0; i < n; ++i) {
    if (WordAt(text, i, L"mouse wheel", 11) || WordAt(text, i, L"the wheel", 9)) {
      return true;
    }
  }
  return false;
}

void __fastcall h_AddText(void* entry, void* edx, int a, int b,
                          const wchar_t* key, const wchar_t* text) {
  o_AddText(entry, edx, a, b, key, text);
  if (!g_enabled || entry == nullptr || key == nullptr || text == nullptr ||
      g_text_count >= kMaxTexts) {
    if (g_text_count >= kMaxTexts && !g_text_full) {
      g_text_full = true;
      ModLog("hints        : the word list has no room for more lines");
    }
    return;
  }
  bool found = false;
  TextKind kind = kPhrase;
  for (const KeyRow& row : kKeys) {
    if (wcscmp(key, row.key) == 0) {
      kind = row.kind;
      found = true;
      break;
    }
  }
  if (!found && !HasPhrase(text)) return;
  void** slot = reinterpret_cast<void**>(static_cast<uint8_t*>(entry) + 8);
  if (*slot == nullptr) return;
  TextRow& row = g_texts[g_text_count++];
  memset(&row, 0, sizeof(row));
  row.slot = slot;
  row.original = static_cast<wchar_t*>(*slot);
  row.alt_signature = 0xFFFFFFFFu;
  row.kind = kind;
  wcsncpy_s(row.key, key, _TRUNCATE);
}

std::wstring ReplacePhrases(const wchar_t* text, const wchar_t* noun) {
  std::wstring out;
  const size_t n = wcslen(text);
  size_t i = 0;
  while (i < n) {
    if (WordAt(text, i, L"mouse wheel", 11)) {
      out += noun;
      i += 11;
    } else if (WordAt(text, i, L"the wheel", 9)) {
      out += L"the ";
      out += noun;
      i += 9;
    } else {
      out += text[i++];
    }
  }
  return out;
}

// The controller version of a line. False when the controller has nothing to
// say there, and then the game's own text stays.
bool BuildAlt(const TextRow& row, const Words& w, std::wstring* out) {
  switch (row.kind) {
    case kLmb:
      if (const wchar_t* word = w.swap ? w.grip : w.trigger) {
        *out = word;
        return true;
      }
      return false;
    case kRmb:
      if (const wchar_t* word = w.swap ? w.trigger : w.grip) {
        *out = word;
        return true;
      }
      return false;
    case kWheelUp:
      if (w.stick_up == nullptr) return false;
      *out = w.stick_up;
      return true;
    case kWheelDown:
      if (w.stick_down == nullptr) return false;
      *out = w.stick_down;
      return true;
    case kKeyboard:
      if (w.noun == nullptr) return false;
      *out = std::wstring(w.verb) + L" the " + w.noun + L" to turn and zoom.";
      return true;
    case kZoomLong:
      if (w.noun == nullptr) return false;
      *out = std::wstring(w.verb) + L" the " + w.noun +
             L" forward or back to zoom. " + w.verb + L" it left or right to "
             L"turn. $m4";
      return true;
    case kZoomShort:
      if (w.noun == nullptr) return false;
      *out = std::wstring(w.verb) + L" the " + w.noun +
             L" forward or back to zoom.$m4";
      return true;
    case kPhrase: {
      if (w.noun == nullptr) return false;
      *out = ReplacePhrases(row.original, w.noun);
      return true;
    }
  }
  return false;
}

wchar_t* GameCopy(const std::wstring& text) {
  const GameNewFn game_new = reinterpret_cast<GameNewFn>(kGameNew);
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  wchar_t* copy = static_cast<wchar_t*>(game_new(
      static_cast<unsigned>(bytes), reinterpret_cast<const char*>(kGameNewFile),
      0x32));
  if (copy != nullptr) memcpy(copy, text.c_str(), bytes);
  return copy;
}

// Put the controller words in the lines, or the game's own words back. Runs
// once a frame, on the game's thread, from eye 0.
void ApplyTexts(bool active, uint32_t signature) {
  uint32_t swapped = 0;
  for (int i = 0; i < g_text_count; ++i) {
    TextRow& row = g_texts[i];
    if (row.dead) continue;
    void* now = *row.slot;
    if (now != row.original && now != row.alt) {
      row.dead = true;
      ModLog("hints        : the game changed the line %ls, so it is left "
             "alone", row.key);
      continue;
    }
    if (active && row.alt_signature != signature) {
      // A new copy for a new profile. The old one is not freed, because the
      // game may still show it, and a profile changes once at most.
      std::wstring text;
      row.alt = BuildAlt(row, g_words, &text) ? GameCopy(text) : nullptr;
      row.alt_signature = signature;
      if (row.alt != nullptr && g_key_logs < 40) {
        ++g_key_logs;
        ModLog("hints        : word list, %ls: \"%ls\"", row.key, row.alt);
      }
    }
    void* want = (active && row.alt != nullptr) ? row.alt : row.original;
    *row.slot = want;
    if (want == row.alt && want != nullptr) ++swapped;
  }
  if (swapped != g_texts_swapped) {
    g_texts_swapped = swapped;
    ModLog("hints        : %u of %d word list lines show controller words",
           swapped, g_text_count);
  }
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

// --- the game's help options -------------------------------------------------
//
// HelpSystem (GGame+0x25005C) reads four options of the player's profile at
// fn_005C6CF0 [V]. A missing value gives the second number:
//
//   +0x45FC  TOOLTIP_LEVEL  2  0 No, 1 Minimum, 2 Intelligent, 3 All ToolTips
//   +0x4604  TEXT_DRAW      1  0 No Text, 1 Story Text, 2 All Text
//   +0x4608  CREATURE_HELP  1  0 off, 1 on
//   +0x45F4  HELP_LEVEL     3  0 No Help to 4 All Help
//
// With tooltips at 0, fn_005C9D00 makes no tooltip at all: no hint beside the
// Hand and no "Amount" of a bulk take (ForceToolTips 0xEEA). With text at 0,
// fn_005C6E60 gives no subtitles. The log says the values once a game, so a
// player with all help off can be told. The options menu of the game writes
// them to the profile, and the mod never does.
const uintptr_t kGamePointer = 0x00D0195C;
const int kGameHelp = 0x25005C;
const int kHelpTooltips = 0x45FC;
const int kHelpText = 0x4604;
const int kHelpCreature = 0x4608;
const int kHelpLevel = 0x45F4;

uint8_t* g_logged_help = nullptr;
int g_help_test = -1;  // the test key help_test, read once
bool g_said_help_test = false;

// A test key of bwvr.ini in the game folder. Not in the launcher.
int TestKey(const char* key) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return 0;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return 0;
  slash[1] = 0;
  if (strlen(path) + 9 >= MAX_PATH) return 0;
  strcat_s(path, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, 0, path));
}

const char* TooltipName(int32_t v) {
  static const char* const kNames[4] = {"No ToolTips", "Minimum ToolTips",
                                        "Intelligent ToolTips",
                                        "All ToolTips"};
  return v >= 0 && v < 4 ? kNames[v] : "unknown";
}

const char* TextName(int32_t v) {
  static const char* const kNames[3] = {"No Text", "Story Text", "All Text"};
  return v >= 0 && v < 3 ? kNames[v] : "unknown";
}

const char* HelpName(int32_t v) {
  static const char* const kNames[5] = {"No Help", "Low Help", "Medium Help",
                                        "High Help", "All Help"};
  return v >= 0 && v < 5 ? kNames[v] : "unknown";
}

// Once a frame, at the left eye. help_test=1 is for the desktop runs: the
// profile of the test PC can have all help off, and then no hint, tooltip or
// subtitle shows. It sets All ToolTips, All Text, High Help (the default)
// and the Creature help in the game's memory, never in the profile.
void HelpOptions() {
  const uint8_t* const* game =
      reinterpret_cast<const uint8_t* const*>(kGamePointer);
  if (IsBadReadPtr(game, sizeof(void*)) || *game == nullptr) return;
  uint8_t* help = *reinterpret_cast<uint8_t* const*>(*game + kGameHelp);
  if (help == nullptr || IsBadReadPtr(help + kHelpLevel, 0x20)) return;
  int32_t* tooltips = reinterpret_cast<int32_t*>(help + kHelpTooltips);
  int32_t* text = reinterpret_cast<int32_t*>(help + kHelpText);
  int32_t* creature = reinterpret_cast<int32_t*>(help + kHelpCreature);
  int32_t* level = reinterpret_cast<int32_t*>(help + kHelpLevel);
  if (help != g_logged_help) {
    g_logged_help = help;
    ModLog("hints        : the game's help options are \"%s\", \"%s\", \"%s\" "
           "and Creature help %s (%d %d %d %d)%s", TooltipName(*tooltips),
           TextName(*text), HelpName(*level), *creature != 0 ? "on" : "off",
           *tooltips, *text, *level, *creature,
           *tooltips == 0 ? ". With No ToolTips the game shows no hint beside "
                            "the Hand and no amount of a bulk take"
                          : "");
  }
  if (g_help_test < 0) g_help_test = TestKey("help_test") != 0 ? 1 : 0;
  if (g_help_test == 0) return;
  if (*tooltips == 3 && *text == 2 && *level == 3 && *creature == 1) return;
  *tooltips = 3;
  *text = 2;
  *level = 3;
  *creature = 1;
  if (!g_said_help_test) {
    g_said_help_test = true;
    ModLog("hints        : test key help_test=1, so the game shows All "
           "ToolTips, All Text, High Help and the Creature help, in memory "
           "only");
  }
}

}  // namespace

void ControllerHintsSetOptions(bool on, const char* forced_profile) {
  g_enabled = on;
  g_forced[0] = 0;
  if (forced_profile != nullptr) strcpy_s(g_forced, forced_profile);
  ModLog("hints        : controller hints %s%s%s", on ? "on" : "off",
         g_forced[0] ? ", profile forced to " : "", g_forced);
}

bool ControllerHintsInstall() {
  bool ok = true;
  ok &= Hook(kAsk, reinterpret_cast<void*>(&h_Ask),
             reinterpret_cast<void**>(&o_Ask), "hint action lookup");
  ok &= Hook(kPicture, reinterpret_cast<void*>(&h_Picture),
             reinterpret_cast<void**>(&o_Picture), "hint picture");
  ok &= Hook(kAddText, reinterpret_cast<void*>(&h_AddText),
             reinterpret_cast<void**>(&o_AddText), "help text store");
  return ok;
}

void ControllerHintsPass(int eye) {
  if (eye != 0) return;
  ++g_frames;
  HelpOptions();
  ControllerHintState state;
  ControllersHintState(&state);
  const char* profile = g_forced[0] ? g_forced : state.profile;
  const Words words = BuildWords(state, profile);
  const uint32_t signature = SignatureOf(words, state);
  const bool active = g_enabled && state.owns;
  if (!g_latched || signature != g_signature) {
    g_latched = true;
    g_signature = signature;
    g_words = words;
    ModLog("hints        : the pointing hand reports \"%s\", the profile used "
           "is \"%s\", family %s. Words: trigger \"%ls\", grip \"%ls\", stick "
           "\"%ls\", jump \"%ls\", gesture \"%ls\"",
           state.profile, profile, kFamilyName[words.family],
           words.trigger ? words.trigger : L"none",
           words.grip ? words.grip : L"none",
           words.stick ? words.stick : L"none",
           words.jump ? words.jump : L"none",
           words.gesture ? words.gesture : L"none");
  }
  if (active != g_active) {
    ModLog("hints        : the hints now show %s",
           active ? "controller words" : "the mouse");
  }
  g_active = active;
  if (g_enabled) ApplyTexts(active, signature);
  if (g_frames % 1200 == 0) {
    CensusReport();
    if (g_converted != 0 || g_hidden != 0 || g_edge_hidden != 0) {
      ModLog("hints        : %llu hints became key caps, %llu were hidden, "
             "%llu edge pictures were hidden", g_converted, g_hidden,
             g_edge_hidden);
    }
  }
}

bool ControllerHintsActive() { return g_active; }

const wchar_t* ControllerHintLabel(uint32_t code) {
  switch (code) {
    case 1:
      return g_words.swap ? g_words.grip : g_words.trigger;
    case 2:
      return g_words.swap ? g_words.trigger : g_words.grip;
    case 8:
    case 0x10:
      return g_words.stick;
    default:
      // The middle button has no controller input, and code 3 (both buttons)
      // is not in the control map.
      return nullptr;
  }
}

void ControllerHintsNoteDraw(uint32_t from, uint32_t blink, uint32_t button,
                             uint32_t row_or_label, const wchar_t* caption) {
  const bool cap = blink == 0xFFFFFFFFu;
  uint32_t key = Mix(2166136261u, 3);
  key = Mix(key, from);
  key = Mix(key, blink);
  key = Mix(key, button);
  key = Mix(key, cap ? 0xC0DEu : row_or_label);
  char label[64] = {0};
  if (cap) {
    Narrow(reinterpret_cast<const wchar_t*>(row_or_label), label,
           sizeof(label));
    // The cap text is part of the key, so two caps with two words are two
    // rows.
    for (const char* c = label; *c != 0; ++c) key = Mix(key, *c);
  }
  if (CensusHit(key) != nullptr) return;
  char words[64];
  Narrow(caption, words, sizeof(words));
  char line[220];
  if (cap) {
    _snprintf_s(line, _TRUNCATE,
                "draw from 0x%08X: key cap \"%s\", button 0x%X, caption "
                "\"%s\"", from, label, button, words);
  } else if (row_or_label == 3) {
    _snprintf_s(line, _TRUNCATE,
                "draw from 0x%08X: text only, blink %u, caption \"%s\"", from,
                blink, words);
  } else {
    _snprintf_s(line, _TRUNCATE,
                "draw from 0x%08X: mouse picture, button 0x%X, row %u, blink "
                "%u, caption \"%s\"", from, button, row_or_label, blink,
                words);
  }
  CensusAdd(key, line);
}

const wchar_t* ControllerHintWord(int word) {
  switch (word) {
    case kWordAction:
      return ControllerHintLabel(1);
    case kWordGrab:
      return ControllerHintLabel(2);
    case kWordStick:
      return g_words.stick;
    case kWordJump:
      return g_words.jump;
    case kWordGesture:
      return g_words.gesture;
    case kWordMenu:
      return g_words.menu;
    case kWordWorld:
      return g_words.world;
    case kWordRing:
      return g_words.ring;
    default:
      return nullptr;
  }
}

void ControllerHintsCountConverted() { ++g_converted; }
void ControllerHintsCountHidden() { ++g_hidden; }
