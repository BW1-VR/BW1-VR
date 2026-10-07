// The hint card: one small picture in the room, beside the wrist of the
// hand that points. It holds the hint that the game shows beside the Hand,
// and the prompts of the controller. See hint_card.cpp and
// docs/controller-hints.md, "The hint card".

#pragma once

#include <stdint.h>

// Once a stereo frame, before the eye passes (ui_panel.cpp passes it on).
// to_headset: the frame goes to a headset. projection: as a projection layer.
// capture: this frame saves the eye pictures, so it saves the card too.
void HintCardBeginFrame(bool to_headset, bool projection, uint64_t frame,
                        bool capture);

// At the start of the left eye pass, after ControllerHintsPass. True when the
// hints of this frame go to the card, and then no hint draws in an eye
// picture. The answer holds for both eye passes.
bool HintCardRoute();

// A hint of the game that did not draw: the word of the controller button (or
// null for a hint with no button) and the caption. `button_code` is the mouse
// button code of the game's call, 0 for none.
void HintCardGameHint(int eye, const wchar_t* button, uint32_t button_code,
                      const wchar_t* text);

// Inside the left eye pass, after the world drew: what the Hand can do now.
void HintCardLatchContext(int eye);

// After both eye passes: the card of this frame goes to the headset.
void HintCardEndFrame();

// The test key (Ctrl Shift F11 with ui_panel=0): the card, or the hints in
// the eye pictures as before.
void HintCardToggle();

// Once a report window.
void HintCardReport();
