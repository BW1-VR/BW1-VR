// A miracle in the Hand (docs/hand.md, "A miracle in the Hand"). The Hand
// with a miracle is the real hand, the cast has its target in the pick record
// of the game, a shake or a button takes the miracle out of the Hand, and the
// card says what each control does.

#pragma once

#include <stdint.h>

struct FarDraw;

// mod_main.cpp, with the direct hand: reads the keys and installs the hooks.
void MiracleInstall();

// village.cpp: the name of a seed type for the log, "a miracle" for a type
// that the table does not have.
const char* MiracleSeedName(int type);

// stereo.cpp, once a frame, after HandDirectFrame.
void MiracleFrame();

// stereo.cpp, once a report window.
void MiracleReport();

// hand_direct.cpp: miracle_hand is on, so the controller places the Hand in
// the Grain state too (the state of a miracle in the Hand).
bool MiraclePlacesHand();

// hand_direct.cpp: a miracle is in the Hand, so the draw must offer the
// objects in the cone of the aim to the pick, as it does for a far grab.
bool MiracleWantsCone();

// hand_direct.cpp, at the end of the first eye pass: the target of a miracle
// for an object, from the objects that the draw offered.
void MiracleSelect();

// hand_direct.cpp, after each eye pass, before each interface step and
// before each press of the Action button: the pick record holds the target of
// the miracle, or no object.
void MiracleKeepRecord();

// hand_direct.cpp, before the interface step of the game (fn_005D1120): a
// cancel or a tap that the player asked for runs here, where the game itself
// runs them.
void MiracleInterfaceStep(void* face);

// controllers.cpp, once a pump. The trigger makes a seed that the Hand
// carries active. True while the trigger must not reach the game.
bool MiraclePumpTrigger(bool trigger, bool ok);

// controllers.cpp, once a pump: the grip looks open to the grip machine while
// the mod lets a seed go, until the real grip opens.
bool MiracleGripMasked(bool grip_down);

// hand_glow.cpp: the halo of the target of the miracle.
bool MiracleHalo(FarDraw* out);

// hint_card.cpp: the lines of the card while a miracle, or a seed, is in the
// Hand. word is a ControllerHintWord, or -1 for a line with no button. The
// return value is the count of lines, 0 with no miracle.
struct MiracleLine {
  int word;
  const wchar_t* text;
};
const int kMiracleLines = 3;
int MiracleHint(MiracleLine lines[kMiracleLines]);

// gesture_air.cpp, through controllers.cpp: the grip is closed with a miracle
// that needs a circle on the land, so the path of the hand goes to the game.
bool MiracleDrawsCircle();
