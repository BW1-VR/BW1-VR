// The hint cards. See docs/controller-hints.md, "The hint card" and "Two
// cards".
//
// WHY. The game draws the hint beside the Hand as flat interface, in screen
// pixels. In stereo a flat hint needs a place in each eye picture, and each
// rule that gave it one had a case where the two pictures did not agree: a
// hint with no disparity while the eyes look at the hand, a hint at the Hand
// in one state and at the edge of the view in the next, a hint behind the
// Hand's mesh. The player sees the hint double in each of those cases.
//
// WHAT. While a controller owns the pointer, no hint of the game draws in an
// eye picture. draw_hooks.cpp gives each hint to this file as words. This
// file draws the words into a small picture with GDI, and the picture goes
// to the headset as a quad layer in the room (xr_screen.cpp). The runtime
// shows a quad at one place in the room to both eyes. So there is one hint,
// with the right depth, in every state of the Hand. Nothing here needs the
// interface panel (ui_panel=0 is fine).
//
// ONE CARD FOR EACH CONTROLLER. A line names a control of one controller, so
// it shows at the hand that holds that controller. The card of the hand that
// points has the hints of the game and the prompts of the trigger, the grip,
// the stick and the face buttons of that hand. The card of the other hand has
// the prompts of its controls: the grip that moves the land, and the button
// of the game menu. Both cards can show in one frame.
//
// WHERE. Beside the wrist of the real hand, as the head sees it
// (hint_card_math.h, CardPose): right of the right wrist, left of the left
// wrist, at the height of the wrist and at its distance from the head. The
// card faces the head. It starts kGap from the outline of the hand, so it is
// not in the hand and not on it in the view, and it is not on the place where
// the fingers work. The runtime draws it over the eye pictures, so the Hand
// cannot hide it. A card that would hide the other hand does not show. A card
// goes with its hand, one to one. It changes side only when the fingers point
// far to its side, and it never moves by itself.
//
// TRUE HINTS ONLY. The game makes its hints for the mouse. Two of them say a
// thing that the controller does not do (hint_card_math.h, GameHintTrue):
// "Move" for the trigger while the trigger does not move the land, and the
// tooltip of the leash for the grip while the trigger sends the Creature.
// They do not show. The prompt of the mod for that place says what is true.
//
// THE PROMPTS. A card shows what the controller can do now, as a button word
// and a short order: "Grip Take", "Grip Pull to fetch". The context comes
// from the mod's own state: the Hand's mode (as the glow reads it), the far
// target, the teleport and a dialog. A prompt shows after the context held
// for a moment, for a few seconds, and only the first times
// (hint_card_math.h, PromptStep). A hint of the game for the same button
// takes the place of the prompt.
//
// THE KEYS of bwvr.ini:
//
//   hint_card=1          0 keeps the hints in the eye pictures
//   prompts=1            0 no prompts, 1 the first times, 2 always
//   hint_card_size_pct   the size of the cards, 50 to 200, 100 by default
//
// With no headset (the desktop test) the route runs too. A capture frame
// saves each card as bwvr-hintcard-right-NNNNN.bmp or
// bwvr-hintcard-left-NNNNN.bmp, and the log gives its place.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../vr/xr_screen.h"
#include "controller_hints.h"
#include "controllers.h"
#include "creature_command.h"
#include "far_grab.h"
#include "forest_take.h"
#include "hint_card.h"
#include "hint_card_math.h"
#include "miracle.h"
#include "radial_menu.h"
#include "temple.h"
#include "teleport.h"
#include "village.h"

extern void ModLog(const char* format, ...);

// controllers.cpp: the grip pose of one hand (0 the hand that points, 1 the
// other hand) and the head center in the LOCAL space, and the length of the
// real hand.
bool ControllersCardPose(int which, float grip_position[3],
                         float grip_orientation[4], float head[3], bool* left);
int ControllersHandCm();
int ControllersHandScalePct();
// hand_direct.cpp: the Hand's point and what a press does there.
bool HandDirectGlowFacts(float point[3], float* length, int32_t* mode,
                         bool* holding, int* eye, uint64_t* frame);
// tabletop.cpp: the God view or the table makes the picture.
bool TabletopOwnsView();
// eye_capture.cpp
void EyeCaptureSaveUi(const char* name, uint64_t frame, int style,
                      const void* pixels, int pitch, uint32_t width,
                      uint32_t height);

namespace {

using bwvr::kHintCardHeight;
using bwvr::kHintCardWidth;

// --- the settings --------------------------------------------------------------

bool g_read = false;
bool g_on = true;        // hint_card
int g_prompts = 1;       // prompts
int g_size_pct = 100;    // hint_card_size_pct
bool g_toggled_off = false;  // the test key

int IniKey(const char* key, int fallback) {
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

void ReadSettings() {
  if (g_read) return;
  g_read = true;
  g_on = IniKey("hint_card", 1) != 0;
  g_prompts = IniKey("prompts", 1);
  if (g_prompts < 0 || g_prompts > 2) g_prompts = 1;
  g_size_pct = IniKey("hint_card_size_pct", 100);
  if (g_size_pct < 50) g_size_pct = 50;
  if (g_size_pct > 200) g_size_pct = 200;
  ModLog("hint card    : hint_card=%d, %s. prompts=%d, %s. The size is %d "
         "percent", g_on ? 1 : 0,
         g_on ? "with a controller the hints show on a card beside the wrist "
                "of each hand"
              : "the hints stay in the eye pictures",
         g_prompts,
         g_prompts == 0 ? "no controller prompts"
                        : (g_prompts == 1 ? "the prompts show the first times"
                                          : "the prompts always show"),
         g_size_pct);
}

// --- the words -----------------------------------------------------------------

// The two cards: the hand that points, and the other hand.
enum { kPointing = 0, kOther = 1, kHands = 2 };

// The tone of a button word: the color of its badge. The trigger is green and
// the grip is gold, as the glow at the Hand's point (hand_glow.cpp).
enum Tone { kToneNone, kToneAction, kToneGrab, kToneOther };

const int kMaxLines = 4;
const int kMaxGameLines = 3;
struct Line {
  wchar_t button[24];  // empty for a line with no button
  wchar_t text[48];
  int tone;
  uint32_t code;       // the mouse button code of a hint of the game, or 0
};
struct Content {
  int count;
  Line line[kMaxLines];
};

void SetLine(Line* line, const wchar_t* button, const wchar_t* text, int tone,
             uint32_t code) {
  memset(line, 0, sizeof(*line));
  if (button != nullptr) wcsncpy_s(line->button, button, _TRUNCATE);
  if (text != nullptr) wcsncpy_s(line->text, text, _TRUNCATE);
  line->tone = line->button[0] != 0 ? tone : kToneNone;
  line->code = code;
}

bool SameContent(const Content& a, const Content& b) {
  if (a.count != b.count) return false;
  for (int i = 0; i < a.count; ++i) {
    if (a.line[i].tone != b.line[i].tone ||
        wcscmp(a.line[i].button, b.line[i].button) != 0 ||
        wcscmp(a.line[i].text, b.line[i].text) != 0) {
      return false;
    }
  }
  return true;
}

// --- the frame ------------------------------------------------------------------

uint64_t g_frame = 0;
bool g_to_headset = false;
bool g_projection = false;
bool g_capture = false;
bool g_route = false;        // the hints of this frame go to the cards
uint64_t g_route_frame = 0;

// The hints of the game in this frame. The first eye pass that gives one is
// the only one that counts, so each hint is taken once.
Content g_noted = {};
int g_noted_eye = -1;
// The game's hints stay this many frames after the last one came, so a frame
// with no draw of the tooltip does not blink the card.
const int kHoldFrames = 4;
Content g_game = {};
int g_game_age = 0;

// What the Hand can do now, latched inside the left eye pass.
bool g_have_facts = false;
uint64_t g_facts_frame = 0;
int32_t g_facts_mode = 0;
bool g_facts_holding = false;

// --- the prompts -----------------------------------------------------------------

enum Context {
  kCtxNone,
  kCtxDialog,    // a dialog box is up, and the controller points at it
  kCtxTeleport,  // the player aims the teleport
  kCtxFar,       // a far object is the target
  kCtxCollect,   // the Hand is on a pile, a field or a fish farm
  kCtxTake,      // the Hand is on an object that the grip can take
  kCtxHolding,   // the Hand holds an object
  kCtxCreature,  // the Hand touches the Creature
  kCtxLand,      // the Hand is over the land, empty
  // The Creature commands (creature_command.cpp), while the leash is on.
  kCtxCommand,      // the aim is on an object the Creature can act on
  kCtxLeashOff,     // the aim is on the player's own Creature
  kCtxCommandAim,   // the trigger is held, and the arc shows the land
  kCtxLeashedLand,  // the Hand is over the land, empty, with the leash on
  kCtxLeashBack,    // the leash is tied, and the aim is on its object or on
                    // the Creature
  // A miracle, or a one-off seed, is in the Hand (miracle.cpp). Its lines
  // show for as long as it is there: a player with a miracle in the Hand
  // must always see how to cast it and how to cancel it.
  kCtxMiracle,
  // The Hand's point is at the totem of the Village Centre or at a miracle
  // icon of a Worship Site, or the Hand moves the totem (village.cpp). The
  // lines stay while the Hand is there: the card shows the value of the
  // totem, as the tooltip of the game does.
  kCtxVillage,
  // The pointing hand points with a ray in a room of the Temple (temple.cpp).
  kCtxTemple,
  // The Hand's point is at the door of the Temple or at a leash post, on the
  // land (temple.cpp).
  kCtxTempleLand,
  kContexts
};

const char* const kContextName[kContexts] = {
    "none",   "a dialog box",  "the teleport aim", "a far target",
    "a bulk source", "an object to take", "an object in the Hand",
    "the Creature", "the land", "a target of the Creature",
    "the leashed Creature", "the aim of a Creature command",
    "the land, with the leash on", "a leash that is tied to an object",
    "a miracle in the Hand", "the totem or a miracle icon",
    "a room of the Temple", "the door of the Temple or a leash post"};

// The dwell, the hold and the count of each context (PromptStep).
const hint_card_math::PromptRule kRules[kContexts] = {
    {0.0f, 0.0f, 0},   // none
    {0.5f, 6.0f, 2},   // dialog
    {0.3f, 5.0f, 3},   // teleport
    {0.3f, 5.0f, 4},   // far
    {0.3f, 5.0f, 3},   // collect
    {0.3f, 4.0f, 4},   // take
    {0.6f, 4.0f, 3},   // holding
    {0.3f, 5.0f, 3},   // creature
    {1.5f, 6.0f, 3},   // land
    {0.3f, 5.0f, 4},   // command
    {0.3f, 5.0f, 3},   // leash off
    {0.3f, 5.0f, 3},   // command aim
    {1.0f, 6.0f, 4},   // leashed land
    {0.3f, 5.0f, 4},   // leash back
    {0.0f, 5.0f, 4},   // miracle: it always shows (HintCardEndFrame)
    {0.0f, 5.0f, 4},   // village: it always shows too
    {0.5f, 6.0f, 3},   // a room of the Temple
    {0.3f, 5.0f, 4},   // the door of the Temple or a leash post
};

hint_card_math::PromptState g_prompt_state;
int g_prompt_shown = kCtxNone;
uint64_t g_prompt_shows[kContexts] = {0};

// The interface hand modes that the glow reads (hand_glow.cpp).
const int32_t kModeLand = hint_card_math::kHandModeLand;
const int32_t kModeTake = 9;
const int32_t kModeBulk = 0x0D;
const int32_t kModeCreature = hint_card_math::kHandModeCreature;

double NowSeconds() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return static_cast<double>(count.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

// The game's land drag moves the view now: not in the God view, not on the
// table, and not while the trigger commands the leashed Creature.
bool TriggerDragsLand() {
  return !TabletopOwnsView() && !CreatureCommandLeashOn();
}

int ContextNow() {
  float from[3], to[3], units = 0.0f;
  // The ray of a dialog box, or of a room of the Temple. A dialog box in the
  // Temple keeps the lines of a dialog box.
  if (ControllersMenuRay(from, to, &units)) {
    return TempleCardRoom() >= 0 ? kCtxTemple : kCtxDialog;
  }
  if (TeleportAiming()) return kCtxTeleport;
  {
    MiracleLine lines[kMiracleLines];
    if (MiracleHint(lines) > 0) return kCtxMiracle;
    if (VillageHint(lines) > 0) return kCtxVillage;
    if (TempleLandHint(lines) > 0) return kCtxTempleLand;
  }
  const int command = CreatureCommandHint();
  if (command == kCommandHintAim) return kCtxCommandAim;
  FarDraw far_draw = {};
  if (FarGrabDrawState(&far_draw) && far_draw.active && far_draw.valid) {
    return kCtxFar;
  }
  if (command == kCommandHintObject) return kCtxCommand;
  if (command == kCommandHintCreature) return kCtxLeashOff;
  if (command == kCommandHintTied || command == kCommandHintTiedCreature) {
    return kCtxLeashBack;
  }
  if (!g_have_facts || g_facts_frame != g_frame) return kCtxNone;
  if (g_facts_holding) return kCtxHolding;
  switch (g_facts_mode) {
    case kModeTake:
      return kCtxTake;
    case kModeBulk:
      return kCtxCollect;
    case kModeCreature:
      return kCtxCreature;
    case kModeLand:
      return command == kCommandHintLeashed ? kCtxLeashedLand : kCtxLand;
    default:
      return kCtxNone;
  }
}

// The card of a control: the menu button, the grip that moves the land and
// the control of the ring are on the controller of the other hand.
int WordHand(int word) {
  return word == kWordMenu || word == kWordWorld || word == kWordRing
             ? kOther
             : kPointing;
}

// The lines of a prompt. The texts are short orders, in Simplified Technical
// English. A line for a button that the controller does not have is left out.
void AddPrompt(Content cards[kHands], int word, const wchar_t* text,
               int tone) {
  Content* out = &cards[WordHand(word)];
  if (out->count >= kMaxLines) return;
  const wchar_t* button = ControllerHintWord(word);
  if (button == nullptr) return;
  // A hint of the game for the same button says what the button does now.
  for (int i = 0; i < out->count; ++i) {
    if (wcscmp(out->line[i].button, button) == 0) return;
  }
  SetLine(&out->line[out->count++], button, text, tone, 0);
}

// A line for two presses of a button, one soon after the other: the word of
// the button with "x2". It has its own line beside the line of one press.
void AddPromptTwice(Content cards[kHands], int word, const wchar_t* text,
                    int tone) {
  Content* out = &cards[WordHand(word)];
  if (out->count >= kMaxLines) return;
  const wchar_t* button = ControllerHintWord(word);
  if (button == nullptr) return;
  wchar_t twice[24];
  swprintf_s(twice, L"%.18s x2", button);
  for (int i = 0; i < out->count; ++i) {
    if (wcscmp(out->line[i].button, twice) == 0) return;
  }
  SetLine(&out->line[out->count++], twice, text, tone, 0);
}

// The lines of the trigger on a target of a command: one press sends the
// Creature, and two presses tie the leash when the leash can go on it.
void AddCommandPrompts(Content cards[kHands]) {
  AddPrompt(cards, kWordAction, L"Send the Creature", kToneAction);
  if (CreatureCommandCanTie()) {
    AddPromptTwice(cards, kWordAction, L"Tie the leash", kToneAction);
  }
}

// The lines of the trigger while the leash is tied to an object.
void AddLeashBackPrompts(Content cards[kHands]) {
  if (CreatureCommandHint() == kCommandHintTiedCreature) {
    AddPrompt(cards, kWordAction, L"Take the leash back", kToneAction);
  } else {
    AddPromptTwice(cards, kWordAction, L"Take the leash back", kToneAction);
  }
}

void AddPrompts(Content cards[kHands], int context, bool first_show) {
  switch (context) {
    case kCtxDialog:
      AddPrompt(cards, kWordAction, L"Select", kToneAction);
      break;
    case kCtxTemple:
      // With temple_click=1 the trigger and the grip both click. With the
      // plain buttons the grip is the Action button of the game.
      AddPrompt(cards, TempleClick() != 0 ? kWordAction : kWordGrab, L"Select",
                kToneAction);
      AddPrompt(cards, kWordRing, L"Rooms", kToneOther);
      AddPrompt(cards, kWordMenu,
                TempleCardRoom() == 0 ? L"Leave the Temple"
                                      : L"Back to the hall",
                kToneOther);
      break;
    case kCtxTeleport:
      AddPrompt(cards, kWordJump, L"Release to go", kToneOther);
      break;
    case kCtxFar: {
      // A scroll in the aim takes a closed grip with no pull, and so does a
      // bulk source. All other far targets come to the Hand with a pull.
      FarDraw far_draw = {};
      FarGrabDrawState(&far_draw);
      // The player's own Creature in the aim: the grip changes the leash, as
      // a click on the Creature does in the plain game.
      AddPrompt(cards, kWordGrab,
                far_draw.leash == 1 ? L"Put the leash on"
                : far_draw.leash == 2 ? L"Take the leash off"
                : far_draw.leash == 3 ? L"Take the leash back"
                : far_draw.touch ? L"Activate"
                : far_draw.bulk ? L"Hold to collect"
                                : L"Pull to fetch",
                kToneGrab);
      // With the leash on the Hand, a short press of the trigger on the
      // Creature takes the leash off too.
      if (CreatureCommandHint() == kCommandHintCreature) {
        AddPrompt(cards, kWordAction, L"Take the leash off", kToneAction);
      }
      // With the leash on, the trigger sends the Creature to the same object.
      if (CreatureCommandHint() == kCommandHintObject) {
        AddCommandPrompts(cards);
      }
      // With the leash tied to an object, the trigger takes it back.
      if (CreatureCommandHint() == kCommandHintTied ||
          CreatureCommandHint() == kCommandHintTiedCreature) {
        AddLeashBackPrompts(cards);
      }
      break;
    }
    case kCtxCollect:
      AddPrompt(cards, kWordGrab, L"Hold to collect", kToneGrab);
      break;
    case kCtxTake:
      AddPrompt(cards, kWordGrab, L"Take", kToneGrab);
      break;
    case kCtxHolding: {
      // A tree in a forest, or the bundle of a take from a forest: the grip
      // collects, and the opening stops the take (forest_take.cpp).
      const wchar_t* forest = ForestTakeHint();
      AddPrompt(cards, kWordGrab, forest != nullptr ? forest : L"Open to drop",
                kToneGrab);
      break;
    }
    case kCtxCreature:
      // The touch of the Creature is the game's right press, so it is the
      // grip. The game's own hint there is "Interact" for that button.
      AddPrompt(cards, kWordGrab, L"Stroke or slap", kToneGrab);
      break;
    case kCtxLand:
      // The control that moves the land: the trigger in the immersive
      // view, and the grip of the other hand in the God view and on the
      // table.
      if (TriggerDragsLand()) {
        AddPrompt(cards, kWordAction, L"Move the land", kToneAction);
      } else {
        AddPrompt(cards, kWordWorld, L"Move the land", kToneGrab);
      }
      // With teleport=0 the A button is the old jump of the game.
      AddPrompt(cards, kWordJump, TeleportOwnsA() ? L"Teleport" : L"Jump",
                kToneOther);
      // The two menus of the other hand: the ring (the Temple, the Creature,
      // the leashes and more) and the menu of the game.
      AddPrompt(cards, kWordRing, L"Ring menu", kToneOther);
      if (first_show) AddPrompt(cards, kWordMenu, L"Game menu", kToneOther);
      break;
    case kCtxCommand:
      AddCommandPrompts(cards);
      break;
    case kCtxLeashBack:
      AddLeashBackPrompts(cards);
      break;
    case kCtxLeashOff:
      AddPrompt(cards, kWordAction, L"Take the leash off", kToneAction);
      break;
    case kCtxCommandAim:
      AddPrompt(cards, kWordAction, L"Release to send", kToneAction);
      break;
    case kCtxMiracle:
    case kCtxTempleLand:
    case kCtxVillage: {
      // The lines come from miracle.cpp: what the grip does with this
      // miracle now, why the game refused the last cast, and the cancel. Or
      // from village.cpp: what the grip does at the totem or at a miracle
      // icon.
      MiracleLine lines[kMiracleLines];
      // Or from temple.cpp: the door of the Temple or a leash post.
      const int count = context == kCtxMiracle      ? MiracleHint(lines)
                        : context == kCtxTempleLand ? TempleLandHint(lines)
                                                    : VillageHint(lines);
      for (int i = 0; i < count; ++i) {
        if (lines[i].word >= 0) {
          AddPrompt(cards, lines[i].word, lines[i].text,
                    lines[i].word == kWordGrab     ? kToneGrab
                    : lines[i].word == kWordAction ? kToneAction
                                                   : kToneOther);
        } else {
          Content* out = &cards[kPointing];
          if (out->count < kMaxLines) {
            SetLine(&out->line[out->count++], L"", lines[i].text, kToneNone, 0);
          }
        }
      }
      break;
    }
    case kCtxLeashedLand:
      // A tap sends the Creature to the aim, and a hold shows the arc.
      AddPrompt(cards, kWordAction, L"Send the Creature", kToneAction);
      AddPrompt(cards, kWordJump, TeleportOwnsA() ? L"Teleport" : L"Jump",
                kToneOther);
      break;
    default:
      break;
  }
}

// --- the picture -----------------------------------------------------------------

// The layout, in pixels of the card. kPixelsPerMeter makes a capital letter
// about 8 mm high in the room at 100 percent: 0.8 degrees at arm's length.
const float kPixelsPerMeter = 4000.0f;
const float kMostWidth = 0.19f;   // meters, at 100 percent
const float kGap = 0.03f;         // meters from the outline of the hand
const float kSideExcess = 0.04f;  // meters of extent before a change of side
const float kMinEyeDistance = 0.15f;
// A card this near to a point of the other hand in the view does not show.
const float kOtherHandMargin = 0.01f;
const int kPad = 18;
const int kRow = 66;
const int kBadgeHeight = 50;
const int kBadgePad = 16;
const int kBadgeGap = 16;
const int kTextEm = 44;
const int kBadgeEm = 34;
const float kCardRadius = 24.0f;
const float kBadgeRadius = 16.0f;
const float kCardAlpha = 0.86f;
// A soft fade: the card comes in 0.25 s and goes in 0.35 s, in place.
const float kFadeInSeconds = 0.25f;
const float kFadeOutSeconds = 0.35f;

HDC g_dc = nullptr;
HBITMAP g_dib = nullptr;
uint32_t* g_bits = nullptr;  // the GDI picture: white letters on black
HFONT g_text_font = nullptr;
HFONT g_badge_font = nullptr;
bool g_raster_failed = false;

// One card. base is the picture, color multiplied by alpha, BGRA, at full
// strength. staged is the picture as it goes to the headset, with the fade.
struct CardState {
  uint8_t base[kHintCardWidth * kHintCardHeight * 4];
  uint8_t staged[kHintCardWidth * kHintCardHeight * 4];
  Content drawn;
  Content logged;
  int rect_w;
  int rect_h;
  float fade;
  float staged_fade;
  bool visible;
  // Right in the view, from the last frame that had one, and the side of the
  // wrist that the card is on (CardPose). 0 is no side yet.
  float last_right[3];
  int side;
  bool side_left_hand;
  // For the report.
  uint64_t frames_shown;
  uint64_t frames_no_hand;
  uint64_t frames_hidden;  // the card would hide the other hand or card
  uint64_t side_changes;
};
CardState g_cards[kHands] = {};
bool g_cards_ready = false;

void CardsStart() {
  if (g_cards_ready) return;
  g_cards_ready = true;
  for (int h = 0; h < kHands; ++h) {
    g_cards[h].staged_fade = -1.0f;
    g_cards[h].last_right[0] = 1.0f;
  }
}

bool RasterStart() {
  if (g_dc != nullptr) return true;
  if (g_raster_failed) return false;
  g_dc = CreateCompatibleDC(nullptr);
  BITMAPINFO info;
  memset(&info, 0, sizeof(info));
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(kHintCardWidth);
  info.bmiHeader.biHeight = -static_cast<LONG>(kHintCardHeight);  // top first
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
  g_text_font = font(kTextEm, FW_SEMIBOLD);
  g_badge_font = font(kBadgeEm, FW_BOLD);
  if (g_dc == nullptr || g_dib == nullptr || bits == nullptr ||
      g_text_font == nullptr || g_badge_font == nullptr) {
    g_raster_failed = true;
    ModLog("hint card    : GDI gave no picture for the card, so the hints stay "
           "in the eye pictures");
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

struct Rgb {
  float r, g, b;
};
const Rgb kCardColor = {18.0f, 22.0f, 30.0f};
const Rgb kTextColor = {255.0f, 255.0f, 255.0f};
const Rgb kBadgeTextColor = {16.0f, 18.0f, 22.0f};
const Rgb kBadgeColor[4] = {
    {0.0f, 0.0f, 0.0f},        // none
    {90.0f, 235.0f, 150.0f},   // the trigger: green, as the glow of a touch
    {255.0f, 196.0f, 60.0f},   // the grip: gold, as the glow of a take
    {232.0f, 236.0f, 242.0f},  // any other button
};

// Draws the content into the base picture of the card. The used part starts
// at 0, 0 and is rect_w by rect_h.
bool Raster(const Content& content, CardState* card) {
  if (!RasterStart()) return false;
  const int width = static_cast<int>(kHintCardWidth);
  const int height = static_cast<int>(kHintCardHeight);
  int badge_x0[kMaxLines], badge_x1[kMaxLines], text_x0[kMaxLines];
  int text_w[kMaxLines];
  int most = 0;
  for (int i = 0; i < content.count; ++i) {
    const Line& line = content.line[i];
    int x = kPad;
    badge_x0[i] = badge_x1[i] = x;
    if (line.button[0] != 0) {
      int w = TextWidth(g_badge_font, line.button) + 2 * kBadgePad;
      if (w < kBadgeHeight) w = kBadgeHeight;
      badge_x1[i] = x + w;
      x = badge_x1[i] + kBadgeGap;
    }
    text_x0[i] = x;
    text_w[i] = TextWidth(g_text_font, line.text);
    int end = x + text_w[i];
    if (line.text[0] == 0 && line.button[0] != 0) end = badge_x1[i];
    if (end > most) most = end;
  }
  int card_w = most + kPad + 4;
  if (card_w > width) card_w = width;
  int card_h = 2 * kPad + content.count * kRow - (kRow - kBadgeHeight);
  if (card_h > height) card_h = height;

  // The letters, white on black. The green of a pixel is its cover.
  for (int y = 0; y < card_h; ++y) {
    memset(g_bits + y * width, 0, static_cast<size_t>(card_w) * 4);
  }
  for (int i = 0; i < content.count; ++i) {
    const Line& line = content.line[i];
    const int top = kPad + i * kRow;
    if (line.button[0] != 0) {
      RECT r = {badge_x0[i], top, badge_x1[i], top + kBadgeHeight};
      SelectObject(g_dc, g_badge_font);
      DrawTextW(g_dc, line.button, -1, &r,
                DT_SINGLELINE | DT_NOPREFIX | DT_CENTER | DT_VCENTER |
                    DT_NOCLIP);
    }
    if (line.text[0] != 0) {
      RECT r = {text_x0[i], top, card_w - kPad, top + kBadgeHeight};
      if (r.right > r.left) {
        SelectObject(g_dc, g_text_font);
        DrawTextW(g_dc, line.text, -1, &r,
                  DT_SINGLELINE | DT_NOPREFIX | DT_LEFT | DT_VCENTER |
                      DT_END_ELLIPSIS | DT_NOCLIP);
      }
    }
  }
  GdiFlush();

  for (int y = 0; y < card_h; ++y) {
    const float py = static_cast<float>(y) + 0.5f;
    const int row = (y - kPad) / kRow;
    const bool in_row = y >= kPad && row >= 0 && row < content.count &&
                        y < kPad + row * kRow + kBadgeHeight;
    const bool badge_row = in_row && content.line[row].button[0] != 0;
    uint8_t* out = card->base + (y * width) * 4;
    const uint32_t* letters = g_bits + y * width;
    for (int x = 0; x < card_w; ++x, out += 4) {
      const float px = static_cast<float>(x) + 0.5f;
      const float cover = RoundCover(px, py, 0.0f, 0.0f,
                                     static_cast<float>(card_w),
                                     static_cast<float>(card_h), kCardRadius);
      if (cover <= 0.0f) {
        out[0] = out[1] = out[2] = out[3] = 0;
        continue;
      }
      Rgb color = kCardColor;
      float alpha = kCardAlpha;
      Rgb ink = kTextColor;
      if (badge_row && x >= badge_x0[row] - 1 && x <= badge_x1[row]) {
        const float top = static_cast<float>(kPad + row * kRow);
        const float badge_cover = RoundCover(
            px, py, static_cast<float>(badge_x0[row]), top,
            static_cast<float>(badge_x1[row]), top + kBadgeHeight,
            kBadgeRadius);
        const Rgb& badge = kBadgeColor[content.line[row].tone & 3];
        color.r += (badge.r - color.r) * badge_cover;
        color.g += (badge.g - color.g) * badge_cover;
        color.b += (badge.b - color.b) * badge_cover;
        alpha += (1.0f - alpha) * badge_cover;
        ink = kBadgeTextColor;
      }
      const float letter =
          static_cast<float>((letters[x] >> 8) & 0xFF) * (1.0f / 255.0f);
      if (letter > 0.0f) {
        color.r += (ink.r - color.r) * letter;
        color.g += (ink.g - color.g) * letter;
        color.b += (ink.b - color.b) * letter;
        alpha += (1.0f - alpha) * letter;
      }
      alpha *= cover;
      out[0] = static_cast<uint8_t>(color.b * alpha + 0.5f);
      out[1] = static_cast<uint8_t>(color.g * alpha + 0.5f);
      out[2] = static_cast<uint8_t>(color.r * alpha + 0.5f);
      out[3] = static_cast<uint8_t>(255.0f * alpha + 0.5f);
    }
  }
  card->rect_w = card_w;
  card->rect_h = card_h;
  card->drawn = content;
  card->staged_fade = -1.0f;
  return true;
}

// The staged picture is the base picture at the fade of this frame.
void Stage(CardState* card) {
  if (card->staged_fade == card->fade) return;
  card->staged_fade = card->fade;
  const int width = static_cast<int>(kHintCardWidth);
  const int scale = static_cast<int>(card->fade * 256.0f + 0.5f);
  for (int y = 0; y < card->rect_h; ++y) {
    const uint8_t* in = card->base + (y * width) * 4;
    uint8_t* out = card->staged + (y * width) * 4;
    if (scale >= 256) {
      memcpy(out, in, static_cast<size_t>(card->rect_w) * 4);
      continue;
    }
    for (int i = 0; i < card->rect_w * 4; ++i) {
      out[i] = static_cast<uint8_t>((in[i] * scale) >> 8);
    }
  }
}

// --- the log ---------------------------------------------------------------------

int g_logs = 0;
const int kMaxLogs = 160;
uint64_t g_game_hints = 0;
uint64_t g_frames_routed = 0;
// Frames with a hint of the game that did not show, because it is false with
// the controllers (GameHintTrue): for the trigger, and for the grip.
uint64_t g_false_trigger = 0;
uint64_t g_false_grip = 0;
bool g_said_false[2] = {false, false};
bool g_said_route = false;

void Narrow(const wchar_t* in, char* out, size_t size) {
  size_t n = 0;
  for (; n + 1 < size && in[n] != 0; ++n) {
    out[n] = (in[n] >= 32 && in[n] < 127) ? static_cast<char>(in[n]) : '?';
  }
  out[n] = 0;
}

void Describe(const Content& content, char* out, size_t size) {
  out[0] = 0;
  for (int i = 0; i < content.count; ++i) {
    char line[160];
    char button[32], text[64];
    Narrow(content.line[i].button, button, sizeof(button));
    Narrow(content.line[i].text, text, sizeof(text));
    if (button[0] != 0) {
      _snprintf_s(line, _TRUNCATE, "%s[%s] %s", i > 0 ? ", " : "", button,
                  text);
    } else {
      _snprintf_s(line, _TRUNCATE, "%s%s", i > 0 ? ", " : "", text);
    }
    strncat_s(out, size, line, _TRUNCATE);
  }
}

// The hints of the game of this frame that are true with the controllers.
void KeepTrueHints(const Content& noted, Content* out) {
  out->count = 0;
  const bool have_mode = g_have_facts && g_facts_frame == g_frame;
  const bool drags = TriggerDragsLand();
  const bool leash = CreatureCommandLeashOn();
  bool false_trigger = false, false_grip = false;
  for (int i = 0; i < noted.count; ++i) {
    const Line& line = noted.line[i];
    if (!hint_card_math::GameHintTrue(line.code, have_mode, g_facts_mode,
                                      drags, leash)) {
      const int which = line.code == 1 ? 0 : 1;
      if (which == 0) {
        false_trigger = true;
      } else {
        false_grip = true;
      }
      if (!g_said_false[which]) {
        g_said_false[which] = true;
        char button[32], text[64];
        Narrow(line.button, button, sizeof(button));
        Narrow(line.text, text, sizeof(text));
        ModLog("hint card    : the hint \"[%s] %s\" of the game does not show: "
               "%s, frame %llu", button, text,
               which == 0
                   ? "the trigger does not move the land here. The grip of "
                     "the other hand moves it"
                   : "with the leash on, the grip on the land does not send "
                     "the Creature. The trigger sends it",
               g_frame);
      }
      continue;
    }
    if (out->count < kMaxGameLines) out->line[out->count++] = line;
  }
  if (false_trigger) ++g_false_trigger;
  if (false_grip) ++g_false_grip;
}

// What one card needs for its place in this frame.
struct Plan {
  bool have_hand;
  bool left;
  float grip_position[3];
  float grip_orientation[4];
  hint_card_math::Hand hand;
  float outline[hint_card_math::kOutlinePoints][3];
  bool placed;
  hint_card_math::Card card;
  float width;
  float height;
  float eye_distance;
  bool show;
};

}  // namespace

void HintCardBeginFrame(bool to_headset, bool projection, uint64_t frame,
                        bool capture) {
  ReadSettings();
  CardsStart();
  g_frame = frame;
  g_to_headset = to_headset;
  g_projection = projection;
  g_capture = capture;
  g_route = false;
  g_noted.count = 0;
  g_noted_eye = -1;
}

bool HintCardRoute() {
  ReadSettings();
  g_route = false;
  if (!g_on || g_toggled_off || g_raster_failed) return false;
  if (!ControllerHintsActive()) return false;
  // In a headset the cards need the projection layer. The stereoscopic window
  // is a quad, and its hints stay in its picture. With no headset this is the
  // desktop test.
  if (g_to_headset &&
      (!g_projection ||
       bwvr::XrScreen::CardFailed(bwvr::kCardHintPointing) ||
       bwvr::XrScreen::CardFailed(bwvr::kCardHintOther))) {
    return false;
  }
  g_route = true;
  g_route_frame = g_frame;
  ++g_frames_routed;
  if (!g_said_route) {
    g_said_route = true;
    ModLog("hint card    : the hints of the game now go to the cards, and none "
           "draws in an eye picture, frame %llu", g_frame);
  }
  return true;
}

void HintCardGameHint(int eye, const wchar_t* button, uint32_t button_code,
                      const wchar_t* text) {
  if (!g_route || g_route_frame != g_frame) return;
  if (g_noted_eye < 0) g_noted_eye = eye;
  if (eye != g_noted_eye) return;
  if (text == nullptr) text = L"";
  if ((button == nullptr || button[0] == 0) && text[0] == 0) return;
  ++g_game_hints;
  Line line;
  // Code 1 is the game's left button, the trigger. Code 2 is the grip.
  const int tone = button_code == 1 ? kToneAction
                                    : (button_code == 2 ? kToneGrab
                                                        : kToneOther);
  SetLine(&line, button, text, tone, button_code);
  for (int i = 0; i < g_noted.count; ++i) {
    if (wcscmp(g_noted.line[i].button, line.button) == 0 &&
        wcscmp(g_noted.line[i].text, line.text) == 0) {
      return;
    }
  }
  if (g_noted.count < kMaxLines) g_noted.line[g_noted.count++] = line;
}

void HintCardLatchContext(int eye) {
  if (eye != 0 || g_facts_frame == g_frame) return;
  float point[3], length = 0.0f;
  int now_eye = -1;
  uint64_t frame = 0;
  g_facts_frame = g_frame;
  g_have_facts = HandDirectGlowFacts(point, &length, &g_facts_mode,
                                     &g_facts_holding, &now_eye, &frame);
}

void HintCardEndFrame() {
  CardsStart();
  const double now = NowSeconds();
  static double last_time = 0.0;
  float dt = static_cast<float>(now - last_time);
  if (last_time == 0.0 || dt > 0.25f) dt = 0.25f;
  last_time = now;
  if (!g_route || g_route_frame != g_frame) {
    // The hints are in the eye pictures, or no controller points. The cards
    // are gone at once, so a hint never shows at two places.
    for (int h = 0; h < kHands; ++h) {
      g_cards[h].fade = 0.0f;
      g_cards[h].visible = false;
    }
    g_game.count = 0;
    g_game_age = 0;
    hint_card_math::PromptStep(&g_prompt_state, kCtxNone, now,
                               kRules[kCtxNone], false);
    g_prompt_shown = kCtxNone;
    return;
  }

  // The hints of the game that are true with the controllers.
  if (g_noted.count > 0) {
    KeepTrueHints(g_noted, &g_game);
    g_game_age = 0;
  } else if (g_game.count > 0 && ++g_game_age > kHoldFrames) {
    g_game.count = 0;
  }

  // The prompt of the context.
  int shown = kCtxNone;
  bool first_show = false;
  if (g_prompts != 0) {
    int context = ContextNow();
    // A hint of the game for the same button says it already. Then the
    // prompt adds no line, and that must not count as one of its shows.
    if (context != kCtxNone && context != kCtxMiracle &&
        context != kCtxVillage) {
      Content probe[kHands] = {g_game, {}};
      AddPrompts(probe, context, false);
      if (probe[kPointing].count == g_game.count &&
          probe[kOther].count == 0) {
        context = kCtxNone;
      }
    }
    // The lines of a miracle in the Hand do not go after 5 s or after 4
    // shows: they stay while the miracle is there.
    shown = hint_card_math::PromptStep(
        &g_prompt_state, context, now, kRules[context],
        g_prompts == 2 || context == kCtxMiracle || context == kCtxVillage);
    if (shown != kCtxNone && shown != g_prompt_shown) {
      ++g_prompt_shows[shown];
    }
    first_show = shown != kCtxNone && g_prompt_state.shows[shown] <= 1;
  }
  const bool prompt_changed = shown != g_prompt_shown;
  g_prompt_shown = shown;

  // The hints of the game are for the hand that points. Each prompt goes to
  // the card of the hand that holds its control.
  Content content[kHands] = {g_game, {}};
  if (shown == kCtxMiracle || shown == kCtxVillage) {
    // A MIRACLE IN THE HAND: the lines of the mod say what the controls do,
    // so the hints of the game for a button go, and one line of its text
    // stays (the name of the miracle). Then the three lines of the mod fit.
    Content& game = content[kPointing];
    int kept = 0;
    for (int i = 0; i < game.count && kept < 1; ++i) {
      if (game.line[i].button[0] != 0) continue;
      game.line[kept++] = game.line[i];
    }
    game.count = kept;
  }
  AddPrompts(content, shown, first_show);
  // The ring is open at the other hand, and its picture is there. The card
  // of that hand comes back when the ring closes.
  if (RadialMenuOpen()) content[kOther].count = 0;

  // THE PLACE of each card: the wrist of its hand.
  const float hand_length = ControllersHandCm() * 0.01f *
                            (ControllersHandScalePct() * 0.01f);
  const float hand_m = hand_length > 0.05f ? hand_length : 0.19f;
  const float size = g_size_pct * 0.01f;
  Plan plan[kHands] = {};
  float head[3] = {0.0f, 0.0f, 0.0f};
  for (int h = 0; h < kHands; ++h) {
    Plan& p = plan[h];
    CardState& c = g_cards[h];
    float head_now[3];
    p.have_hand = ControllersCardPose(h, p.grip_position, p.grip_orientation,
                                      head_now, &p.left);
    if (p.have_hand) {
      for (int i = 0; i < 3; ++i) head[i] = head_now[i];
      hint_card_math::HandFromGrip(p.grip_position, p.grip_orientation, p.left,
                                   hand_m, &p.hand);
      hint_card_math::HandOutline(p.grip_position, p.grip_orientation, p.left,
                                  hand_m, p.outline);
    }
    if (content[h].count > 0 && !p.have_hand) ++c.frames_no_hand;
    p.show = content[h].count > 0 && p.have_hand;
    if (p.show && !SameContent(content[h], c.drawn)) {
      if (!Raster(content[h], &c)) return;
    }
    if (!p.have_hand || c.rect_w <= 0 || c.rect_h <= 0) continue;
    hint_card_math::CardSize(c.rect_w, c.rect_h, kPixelsPerMeter / size,
                             kMostWidth * size, &p.width, &p.height);
    // The side starts again when the card comes, and when the hand of the
    // card changes.
    if (!c.visible || p.left != c.side_left_hand) c.side = 0;
    c.side_left_hand = p.left;
    p.placed = hint_card_math::CardPose(
        p.hand, p.outline, hint_card_math::kOutlinePoints, head, p.width, kGap,
        kSideExcess, c.last_right, c.side, p.left ? -1 : 1, &p.card);
    if (!p.placed) continue;
    for (int i = 0; i < 3; ++i) c.last_right[i] = p.card.right[i];
    c.side = p.card.side;
    if (p.card.changed && c.visible) ++c.side_changes;
    p.eye_distance = controller_math::Distance(p.card.center, head);
    // A card at the face is not readable, and it hides the view.
    if (p.eye_distance < kMinEyeDistance) p.placed = false;
  }
  // A card must not hide the other hand, and the two cards must not overlap.
  // The card of the hand that points wins.
  for (int h = 0; h < kHands; ++h) {
    Plan& p = plan[h];
    const Plan& other = plan[1 - h];
    if (!p.show || !p.placed) continue;
    bool hides = other.have_hand &&
                 hint_card_math::CardCoversAny(
                     p.card, p.width, p.height, kOtherHandMargin, head,
                     other.outline, hint_card_math::kOutlinePoints);
    if (!hides && h == kOther && other.show && other.placed) {
      hides = hint_card_math::CardsOverlap(other.card, other.width,
                                           other.height, p.card, p.width,
                                           p.height, head);
    }
    if (hides) {
      p.show = false;
      ++g_cards[h].frames_hidden;
    }
  }

  for (int h = 0; h < kHands; ++h) {
    Plan& p = plan[h];
    CardState& c = g_cards[h];
    const bool show = p.show && p.placed;
    c.fade = hint_card_math::FadeStep(
        c.fade, show, dt, show ? kFadeInSeconds : kFadeOutSeconds);
    if (!p.placed || c.fade <= 0.0f) {
      c.visible = false;
      c.fade = 0.0f;
      continue;
    }
    Stage(&c);
    bwvr::CardLayer layer = {};
    layer.visible = true;
    layer.pixels = c.staged;
    layer.texture_width = kHintCardWidth;
    layer.texture_height = kHintCardHeight;
    layer.rect[0] = 0;
    layer.rect[1] = 0;
    layer.rect[2] = c.rect_w;
    layer.rect[3] = c.rect_h;
    for (int i = 0; i < 3; ++i) layer.position[i] = p.card.center[i];
    for (int i = 0; i < 4; ++i) layer.orientation[i] = p.card.orientation[i];
    layer.width = p.width;
    layer.height = p.height;
    bwvr::XrScreen::StageCard(
        h == kPointing ? bwvr::kCardHintPointing : bwvr::kCardHintOther,
        layer);
    ++c.frames_shown;

    const bool fresh = !c.visible;
    c.visible = true;
    const bool new_words = !SameContent(c.logged, c.drawn);
    const char* name = p.left ? "left" : "right";
    if (show &&
        (fresh || new_words || (prompt_changed && h == kPointing) ||
         p.card.changed || g_capture) &&
        (g_logs < kMaxLogs || g_capture)) {
      ++g_logs;
      c.logged = c.drawn;
      char words[320];
      Describe(c.drawn, words, sizeof(words));
      ModLog("hint card    : frame %llu, the card of the %s hand: %s. %.1f by "
             "%.1f cm, %d by %d px, at %.3f %.3f %.3f m, %.1f cm to the %s of "
             "the wrist in the view and %.0f cm from the head%s%s", g_frame,
             name, words, p.width * 100.0f, p.height * 100.0f, c.rect_w,
             c.rect_h, p.card.center[0], p.card.center[1], p.card.center[2],
             p.card.reach * 100.0f, p.card.side < 0 ? "left" : "right",
             p.eye_distance * 100.0f,
             shown != kCtxNone ? ". The prompt is for " : "",
             shown != kCtxNone ? kContextName[shown] : "");
    }
    if (g_capture) {
      ModLog("hint card    : frame %llu, the pose of the card of the %s hand: "
             "turn %.4f %.4f %.4f %.4f, the wrist at %.3f %.3f %.3f, the "
             "finger tips at %.3f %.3f %.3f, the grip at %.3f %.3f %.3f, the "
             "head at %.3f %.3f %.3f, the hand goes %.1f cm to the side of "
             "the card and %.1f cm to the other side, fade %.2f",
             g_frame, name, p.card.orientation[0], p.card.orientation[1],
             p.card.orientation[2], p.card.orientation[3], p.hand.wrist[0],
             p.hand.wrist[1], p.hand.wrist[2], p.hand.tip[0], p.hand.tip[1],
             p.hand.tip[2], p.grip_position[0], p.grip_position[1],
             p.grip_position[2], head[0], head[1], head[2],
             p.card.extent * 100.0f, p.card.other * 100.0f, c.fade);
      const int pitch = static_cast<int>(kHintCardWidth) * 4;
      EyeCaptureSaveUi(p.left ? "hintcard-left" : "hintcard-right", g_frame, 0,
                       c.staged, pitch, static_cast<uint32_t>(c.rect_w),
                       static_cast<uint32_t>(c.rect_h));
      EyeCaptureSaveUi(p.left ? "hintcard-left-over" : "hintcard-right-over",
                       g_frame, 1, c.staged, pitch,
                       static_cast<uint32_t>(c.rect_w),
                       static_cast<uint32_t>(c.rect_h));
    }
  }
}

void HintCardToggle() {
  ReadSettings();
  if (!g_on) {
    ModLog("test key     : hint_card is 0 in bwvr.ini, so there is no hint "
           "card to switch to");
    return;
  }
  g_toggled_off = !g_toggled_off;
  ModLog("test key     : %s",
         g_toggled_off ? "the hints draw in the eye pictures, as before"
                       : "the hints show on the cards beside the wrists");
}

void HintCardReport() {
  if (g_frames_routed == 0) return;
  const CardState& a = g_cards[kPointing];
  const CardState& b = g_cards[kOther];
  ModLog("hint card    : %llu frames with the hints on the cards. The card of "
         "the pointing hand showed on %llu and the card of the other hand on "
         "%llu. %llu hints of the game went to the cards and none drew in an "
         "eye picture. Hints that are false with the controllers did not "
         "show on %llu frames (the trigger and the land) and on %llu (the "
         "grip and the leashed Creature)",
         g_frames_routed, a.frames_shown, b.frames_shown, g_game_hints,
         g_false_trigger, g_false_grip);
  ModLog("hint card    : %llu and %llu frames had words and no tracked hand. "
         "%llu and %llu frames a card did not show, because it was on the "
         "other hand or on the other card. The cards changed side %llu and "
         "%llu times (the pointing hand first)",
         a.frames_no_hand, b.frames_no_hand, a.frames_hidden, b.frames_hidden,
         a.side_changes, b.side_changes);
  uint64_t prompts = 0;
  for (int i = 1; i < kContexts; ++i) prompts += g_prompt_shows[i];
  if (prompts > 0) {
    ModLog("hint card    : prompts shown: %llu dialog, %llu teleport, %llu far "
           "target, %llu collect, %llu take, %llu holding, %llu Creature, "
           "%llu land, %llu send to an object, %llu leash off, %llu aim of a "
           "command, %llu land with the leash on, %llu leash back, %llu "
           "miracle, %llu totem or miracle icon, %llu room of the Temple, "
           "%llu door of the Temple or leash post",
           g_prompt_shows[kCtxDialog], g_prompt_shows[kCtxTeleport],
           g_prompt_shows[kCtxFar], g_prompt_shows[kCtxCollect],
           g_prompt_shows[kCtxTake], g_prompt_shows[kCtxHolding],
           g_prompt_shows[kCtxCreature], g_prompt_shows[kCtxLand],
           g_prompt_shows[kCtxCommand], g_prompt_shows[kCtxLeashOff],
           g_prompt_shows[kCtxCommandAim], g_prompt_shows[kCtxLeashedLand],
           g_prompt_shows[kCtxLeashBack],
           g_prompt_shows[kCtxMiracle], g_prompt_shows[kCtxVillage],
           g_prompt_shows[kCtxTemple], g_prompt_shows[kCtxTempleLand]);
  }
  g_frames_routed = 0;
  g_game_hints = 0;
  g_false_trigger = g_false_grip = 0;
  for (int h = 0; h < kHands; ++h) {
    g_cards[h].frames_shown = 0;
    g_cards[h].frames_no_hand = 0;
    g_cards[h].frames_hidden = 0;
    g_cards[h].side_changes = 0;
  }
  for (int i = 0; i < kContexts; ++i) g_prompt_shows[i] = 0;
}
