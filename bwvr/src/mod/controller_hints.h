// Controller hints. See docs/controller-hints.md.
//
// While a motion controller owns the pointer, the game's mouse hints show the
// controller word in a key cap, and the help text names the controller.
//   The census: each distinct hint call goes in the log once.
//   Key caps with words, and no hint for an action the controller lacks.
//   The word list for the help text, changed in memory at load.

#pragma once

#include <stdint.h>

// controller_hints and hint_profile of bwvr.ini. An empty profile means the
// profile the runtime reports.
void ControllerHintsSetOptions(bool on, const char* forced_profile);

// The three read only hooks and the help text hook.
bool ControllerHintsInstall();

// Once for each eye pass. Only eye 0 changes anything: it latches what the
// hints show for the whole frame, so the two eyes always agree.
void ControllerHintsPass(int eye);

// True while the words of the controller replace the mouse in the hints.
bool ControllerHintsActive();

// The word for a mouse button code of the game (1 left, 2 right, 4 middle,
// 8 and 0x10 the wheel). Null when the controller has no such input, and then
// the hint is hidden. The pointer stays valid for the whole frame.
const wchar_t* ControllerHintLabel(uint32_t code);

// The word of one controller input, for the prompts of the hint card
// (hint_card.cpp). Null when the controller has no such input. The pointer
// stays valid for the whole frame.
enum ControllerWord {
  kWordAction,   // the game's left button: the trigger
  kWordGrab,     // the game's right button: the grip
  kWordStick,
  kWordJump,     // A, or X on the left Touch hand: the teleport
  kWordGesture,  // the button that draws a gesture
  kWordMenu,     // the button that opens the game menu, on the other hand
  kWordWorld,    // the grip of the other hand: it grabs the land and moves it
  kWordRing,     // the control of the other hand that opens the ring
};
const wchar_t* ControllerHintWord(int word);
// True when the button of the game menu is on the controller of the pointing
// hand: the menu button of the left controller, for a left handed player.
bool ControllerHintMenuOnPointing();

// The census: a call of CameraHelp::DrawKeyOrMouse, before the mod changes it.
void ControllerHintsNoteDraw(uint32_t from, uint32_t blink, uint32_t button,
                             uint32_t row_or_label, const wchar_t* caption);

// Key cap counters, for the report.
void ControllerHintsCountConverted();
void ControllerHintsCountHidden();
