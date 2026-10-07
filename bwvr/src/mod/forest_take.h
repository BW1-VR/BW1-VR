// The take from a forest (docs/hand.md, "Many trees from a forest"). The
// plain game gives one tree for each press, and its Hand holds one object.
// While the grip stays closed in a forest, the mod puts tree after tree in a
// bundle of wood in the Hand.

#pragma once

#include <stdint.h>

// A tree that the take can get, from the objects that the draw offered in
// this frame (hand_direct.cpp, HandDirectForestTrees).
struct ForestCandidate {
  void* object;
  float base[3];   // the point of the object
  float radius;    // the game's 2D radius
  float height;    // the game's height
  bool forest;     // a BigForest: it gives the wood of one tree at a time
  bool inside;     // the Hand can take it: it is in the player's influence
};

// hand_direct.cpp, at the end of the first eye pass: the next tree of the
// take, from the objects that the draw offered.
void ForestTakeSelect();

// hand_direct.cpp, before the interface step of the game (fn_005D1120). The
// take acts here, where the game itself puts objects in the Hand. face is the
// GInterface, hand the CHand. owns says that the controller places the Hand
// and the game is not a multiplayer game.
void ForestTakeInterfaceStep(void* face, void* hand, bool owns);

// hand_direct.cpp, for the grip machine: the take runs, so the closed grip
// collects and the opening keeps the bundle (GripFacts::mod_take).
bool ForestTakeRuns();

// hand_direct.cpp: a take that started with a fetch from a distance needs the
// objects in the cone of the aim in the pick, as a far grab does.
bool ForestTakeWantsCone();

// hint_card.cpp: the line of the grip while the Hand holds a tree or the
// bundle of a take. Null when the take has nothing to say.
const wchar_t* ForestTakeHint();

// hand_direct.cpp, once a report window.
void ForestTakeReport();
