// A text of the game that waits for a click. See text_click.cpp and
// docs/controller-hints.md, "A text that waits for a click".

#pragma once

#include <stdint.h>

// controllers.cpp, once a frame (ControllersFrame): latches the flag of the
// game for the frame, logs each change, and reads the test keys.
void TextClickFrame(uint64_t frame);

// controllers.cpp, once for each pass of the message pump, on the game
// thread: the work of the test key text_click_test.
void TextClickPump();

// text_click now. The test key changes it while the game runs.
bool TextClickOn();

// A text of the game waits for a click in this frame, with each value of
// text_click.
bool TextClickFlag();

// hint_card.cpp, inside the draw of a hint of the game: the hint with this
// text is the prompt of the game for that click ("Continue").
bool TextClickIsPrompt(const wchar_t* text);

// controllers.cpp: the controller sent a press of the left button to the
// game.
void TextClickSent();

// Once a report window.
void TextClickReport();
