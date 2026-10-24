// The Creature looks at the player: where the game makes a Creature look at
// the camera of the player, the mod gives it the point of the player's head.
// See creature_look.cpp and docs/creature.md, "The Creature looks at the
// player".

#pragma once

#include <stdint.h>

// hand_direct.cpp, at the start, with the other keys of the Creature.
void CreatureLookInstall();

// far_grab.cpp, once a frame: the head of this frame, and the log of each
// look.
void CreatureLookFrame(double now, uint64_t frame);

// Once a report window.
void CreatureLookReport();
