// The ring: a menu around the hand that does not point, for the functions of
// the plain game that have no controller button. See radial_menu.cpp,
// docs/controllers.md, "The ring", and docs/controls-audit.md.

#pragma once

// controllers.cpp, once a frame, after the hands of the frame are known.
void RadialMenuFrame();

// controllers.cpp, once for each pass of the message pump of the game. The
// keys of a choice go into the key buffer of the game here.
void RadialMenuPump(unsigned long long now_ms);

// True while the ring is open. Then a grip does not start a grab of the
// world, and the hint card of that hand does not show.
bool RadialMenuOpen();

// For the hints: 0 the ring is off, 1 its button opens it, 2 only the stick
// opens it.
int RadialMenuHintMode();

// Once a report window.
void RadialMenuReport();
