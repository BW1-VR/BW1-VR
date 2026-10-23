// A text of the game that waits for a click. See docs/controller-hints.md,
// "A text that waits for a click".
//
// WHAT THE GAME DOES. A script says a text with the script word RUN_TEXT
// (GScript::RunText 0x006F7D60). Its last value is a mode. Mode 1 is "with
// interaction": fn_005C6100 then sets HelpSystem + 0x57C. While that flag is
// set:
//
//   - HelpSystem::IsTextRead 0x005C64E0 gives 0, so the script word
//     TEXT_READ ("wait until read") never becomes true.
//   - HelpSystem::Draw3D 0x005C59A0 makes a hint with the picture of the
//     left button and the caption of help text 3705 ("Continue"), and keeps
//     its node at HelpSystem + 0x24. The node has a copy of the caption at
//     + 0x20 (fn_004489D0), and the draw of the node gives that copy to
//     CameraHelp::DrawKeyOrMouse (fn_00448AC0).
//   - HelpSystem::ProcessInterface 0x005C69B0 clears the flag when the left
//     button goes up, 1 s or more after the text came. No time clears it.
//
// A script says such a text in a dialogue, and it keeps the dialogue while it
// waits (HelpSystem + 0x45CC is its task). GScript::StartDialogue 0x00710690
// gives 0 to each other challenge script until the dialogue is free. A
// script that starts a cinema takes the camera first and then waits for the
// dialogue. So one text that waits for a click stops its own script, and the
// start of each other story.
//
// WHAT THE MOD DOES (text_click=1).
//
//   - The prompt of the game shows on the card of the pointing hand in each
//     view (hint_card.cpp). Its button is the left button, and the rule of
//     the hint card hides each other hint of the left button where the
//     trigger does not move the land.
//   - The trigger is the click: while the flag is set, the press of the
//     trigger is no Creature command and makes no seed active, and it starts
//     with the Hand off the view too (controllers.cpp,
//     text_click_math.h).
//
// The mod writes nothing in the game for this, except with the test key
// text_click_test. It reads the flag once a frame. text_click=0 leaves the
// prompt to the rule of the left button and the trigger to its other jobs.
//
// THE KEYS of bwvr.ini:
//
//   text_click=1          see above. Shift and Insert switch it, with
//                         test_keys=1
//   text_click_test       a test for the desktop runs: "frame" or
//                         "frame,frame". At each frame the mod starts a wait
//   text_click_test_text  0: the test sets the flag of the game. A text
//                         number: the test makes the game say that text with
//                         interaction, through the function of RUN_TEXT

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "text_click.h"
#include "text_click_math.h"

extern void ModLog(const char* format, ...);
// controllers.cpp: one pulse of the pointing controller.
void ControllersGesturePulse(float amplitude, float seconds);
// controllers.cpp: the number that a test key with a frame compares with it.
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);

namespace {

const uintptr_t kGame = 0x00D0195C;    // GGame::g_game
const int kGameHelpSystem = 0x25005C;  // GGame + this: the HelpSystem pointer
// HelpSystem + this: 1 while a text waits for a click.
const int kHelpWaitsForClick = 0x57C;
// HelpSystem + this: the script task that has the dialogue, or 0.
const int kHelpDialogueTask = 0x45CC;
const int kHelpWideScreen = 0x45E8;  // HelpSystem + this: 1 with the bars up
// HelpSystem + this: the object that shows the text. Its + 0xB0 is the first
// value of RUN_TEXT.
const int kHelpTextShow = 0x14;
const int kTextShowSingle = 0xB0;
// HelpSystem + this: the node of the hint that HelpSystem::Draw3D makes while
// a text waits for a click. The node has its caption at + 0x20.
const int kHelpClickHint = 0x24;
const int kHintCaption = 0x20;
// The function of RUN_TEXT: thiscall on the HelpSystem, with the text
// number, the mode, 0 and 1. It takes its 4 values off the stack.
const uintptr_t kHelpSayText = 0x005C5F90;
const uintptr_t kHelpClearAllText = 0x005C5550;  // thiscall, no values
const uint32_t kHelpTextLimit = 0x1B3E;  // RUN_TEXT refuses a number from here

typedef void(__fastcall* SayTextFn)(void* help, void* unused, int32_t text,
                                    int32_t mode, int32_t zero, int32_t one);
typedef void(__fastcall* ClearAllTextFn)(void* help, void* unused);

bool g_read = false;
bool g_on = true;          // text_click, and its test key
bool g_test_keys = false;  // test_keys
// The test key text_click_test.
const int kMaxTestFrames = 4;
uint64_t g_test_frames[kMaxTestFrames] = {0};
int g_test_count = 0;
int g_test_text = 0;       // text_click_test_text
bool g_test_pending = false;

uint64_t g_frame = 0;
bool g_flag = false;  // the flag of the game, latched for the frame
text_click_math::Wait g_wait;
int g_logs = 0;
const int kMaxLogs = 200;
uint64_t g_waits = 0;           // waits that started, this window
uint64_t g_frames_waiting = 0;  // frames with the flag set, this window

int IniInt(const char* key, int fallback) {
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

void IniText(const char* key, char* out, DWORD size) {
  out[0] = 0;
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return;
  slash[1] = 0;
  if (strlen(path) + 9 >= MAX_PATH) return;
  strcat_s(path, "bwvr.ini");
  GetPrivateProfileStringA("bwvr", key, "", out, size, path);
}

const char* RuleText() {
  return g_on ? "The card of the pointing hand shows the prompt of the game "
                "in each view, and the trigger is the click: no Creature "
                "command, no seed, and with the Hand off the view too"
              : "The prompt follows the rule of the left button, so it does "
                "not show where the trigger does not move the land, and the "
                "trigger keeps its other jobs";
}

void ReadSettings() {
  if (g_read) return;
  g_read = true;
  g_on = IniInt("text_click", 1) != 0;
  g_test_keys = IniInt("test_keys", 0) != 0;
  char text[96];
  IniText("text_click_test", text, sizeof(text));
  g_test_count =
      text_click_math::ParseFrames(text, g_test_frames, kMaxTestFrames);
  g_test_text = IniInt("text_click_test_text", 0);
  if (g_test_text < 0 || static_cast<uint32_t>(g_test_text) >= kHelpTextLimit) {
    g_test_text = 0;
  }
  ModLog("text click   : text_click=%d. A text of the game can wait for a "
         "click of the left button. %s", g_on ? 1 : 0, RuleText());
  if (g_test_keys) {
    ModLog("text click   : test_keys=1: Shift and Insert switch text_click");
  }
  if (g_test_count > 0) {
    char how[96];
    if (g_test_text != 0) {
      _snprintf_s(how, _TRUNCATE, "makes the game say text %d with "
                  "interaction", g_test_text);
    } else {
      _snprintf_s(how, _TRUNCATE, "sets the flag of the game");
    }
    ModLog("text click   : test key text_click_test: at %d frames, the first "
           "is %llu, the mod %s", g_test_count, g_test_frames[0], how);
  }
}

// The HelpSystem of the game, or null.
uint8_t* Help() {
  uint8_t* const* game_at = reinterpret_cast<uint8_t* const*>(kGame);
  uint8_t* const game = *game_at;
  if (game == nullptr ||
      IsBadReadPtr(game + kGameHelpSystem, sizeof(uintptr_t))) {
    return nullptr;
  }
  uint8_t* const help = *reinterpret_cast<uint8_t* const*>(game + kGameHelpSystem);
  if (help == nullptr ||
      IsBadReadPtr(help + kHelpClickHint, sizeof(uintptr_t)) ||
      IsBadReadPtr(help + kHelpWaitsForClick, sizeof(int32_t)) ||
      IsBadReadPtr(help + kHelpDialogueTask, 0x24)) {
    return nullptr;
  }
  return help;
}

}  // namespace

// The switch of the debug page (debug_menu.h).
void TextClickChange(const char* by) {
  ReadSettings();
  g_on = !g_on;
  ControllersGesturePulse(0.5f, g_on ? 0.24f : 0.08f);
  ModLog("test key     : %s: text_click is %d now, frame %llu. %s", by,
         g_on ? 1 : 0, g_frame, RuleText());
}

int TextClickValue() {
  ReadSettings();
  return g_on ? 1 : 0;
}

namespace {

// THE A/B KEY (test_keys=1): Shift with Insert, and Ctrl up, switches
// text_click between 1 and 0. The pointing controller gives a long pulse for
// 1 and a short pulse for 0. Ctrl with Insert is the key of the ring of the
// teleport. The key is read once a frame, and only while a window of the
// game is in front.
void PollKey() {
  static bool was = false;
  if (!g_test_keys) return;
  const bool down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0;
  const bool pressed = down && !was;
  was = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  TextClickChange("Shift and Insert");
}

// The test: start a wait, as a script does with a text with interaction.
void TestStart() {
  uint8_t* const help = Help();
  if (help == nullptr) {
    ModLog("text click   : test key text_click_test: the game has no help "
           "system now, so nothing is done, frame %llu", g_frame);
    return;
  }
  if (g_test_text == 0) {
    *reinterpret_cast<int32_t*>(help + kHelpWaitsForClick) = 1;
    ModLog("text click   : test key text_click_test: the mod sets the flag "
           "of the game (HelpSystem + 0x57C), frame %llu", g_frame);
    return;
  }
  uint8_t* const show = *reinterpret_cast<uint8_t* const*>(help + kHelpTextShow);
  if (show == nullptr || IsBadWritePtr(show + kTextShowSingle, sizeof(int32_t))) {
    ModLog("text click   : test key text_click_test: the help system shows "
           "no text now, so nothing is done, frame %llu", g_frame);
    return;
  }
  ModLog("text click   : test key text_click_test: the mod makes the game "
         "say text %d with interaction (the values 1, %d and 1 of RUN_TEXT), "
         "frame %llu", g_test_text, g_test_text, g_frame);
  // As GScript::RunText does it for the values (1, text, 1).
  reinterpret_cast<ClearAllTextFn>(kHelpClearAllText)(help, nullptr);
  *reinterpret_cast<int32_t*>(show + kTextShowSingle) = 1;
  reinterpret_cast<SayTextFn>(kHelpSayText)(help, nullptr, g_test_text, 1, 0,
                                            1);
}

}  // namespace

void TextClickFrame(uint64_t frame) {
  ReadSettings();
  g_frame = frame;
  PollKey();
  for (int i = 0; i < g_test_count; ++i) {
    // The frame of the key counts as a step of the script counts it
    // (fake_controller_play_frame).
    if (ControllersTestFrame(g_test_frames[i], frame) == g_test_frames[i]) {
      g_test_pending = true;
    }
  }
  const uint8_t* const help = Help();
  g_flag = help != nullptr &&
           *reinterpret_cast<const int32_t*>(help + kHelpWaitsForClick) != 0;
  uint64_t frames = 0, clicks = 0;
  const int change =
      text_click_math::WaitStep(&g_wait, g_flag, frame, &frames, &clicks);
  if (g_flag) ++g_frames_waiting;
  if (change == text_click_math::kStarted) {
    ++g_waits;
    if (g_logs < kMaxLogs) {
      ++g_logs;
      const int32_t task =
          *reinterpret_cast<const int32_t*>(help + kHelpDialogueTask);
      const bool bars =
          *reinterpret_cast<const int32_t*>(help + kHelpWideScreen) != 0;
      ModLog("text click   : a text of the game waits for a click now "
             "(HelpSystem + 0x57C), frame %llu. The script task %d has the "
             "dialogue (0 is none), and the cinema bars are %s. text_click=%d",
             frame, task, bars ? "up" : "down", g_on ? 1 : 0);
    }
  } else if (change == text_click_math::kEnded) {
    if (g_logs < kMaxLogs) {
      ++g_logs;
      ModLog("text click   : the wait for a click is over after %llu frames, "
             "frame %llu. The controller sent %llu presses of the left "
             "button to the game in that time", frames, frame, clicks);
    }
  }
}

void TextClickPump() {
  if (!g_test_pending) return;
  g_test_pending = false;
  TestStart();
}

bool TextClickOn() {
  ReadSettings();
  return g_on;
}

bool TextClickFlag() { return g_flag; }

// The game calls the draw of a hint inside HelpSystem::Draw3D, which made the
// node from the flag a moment before. So this reads the flag of that moment,
// not the flag of the start of the frame.
bool TextClickIsPrompt(const wchar_t* text) {
  if (text == nullptr) return false;
  const uint8_t* const help = Help();
  if (help == nullptr ||
      *reinterpret_cast<const int32_t*>(help + kHelpWaitsForClick) == 0) {
    return false;
  }
  const uint8_t* const hint =
      *reinterpret_cast<const uint8_t* const*>(help + kHelpClickHint);
  return hint != nullptr &&
         text == reinterpret_cast<const wchar_t*>(hint + kHintCaption);
}

void TextClickSent() { text_click_math::WaitClick(&g_wait); }

void TextClickReport() {
  if (g_waits == 0 && g_frames_waiting == 0) return;
  if (g_flag) {
    ModLog("text click   : %llu waits for a click started, and %llu frames "
           "had a text that waits, this window. A text waits since frame "
           "%llu, and the controller sent %llu presses of the left button "
           "to the game since then", g_waits, g_frames_waiting, g_wait.since,
           g_wait.clicks);
  } else {
    ModLog("text click   : %llu waits for a click started, and %llu frames "
           "had a text that waits, this window. No text waits now", g_waits,
           g_frames_waiting);
  }
  g_waits = 0;
  g_frames_waiting = 0;
}
