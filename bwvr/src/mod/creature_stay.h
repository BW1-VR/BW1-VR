// The stay of the Creature: a grip that stays closed on the far Creature
// makes it stop and stay. See creature_stay.cpp and docs/creature.md, "The
// stay".

#pragma once

#include <stdint.h>

// hand_direct.cpp, at the start, with the other keys of the Creature.
void CreatureStayInstall();

// creature_hold_stay now: stay_math::kModeHold or stay_math::kModeClose. The
// test key changes it while the game runs.
int CreatureStayMode();
// creature_hold_stay_ms: a grip that is closed for this long is a hold.
int CreatureStayHoldMs();

// far_grab.cpp: the grip closed on the far Creature. The place of the
// Creature at this time is kept for the log of the start.
void CreatureStayPressed(void* creature, double now);

// far_grab.cpp: the grip stayed closed for the hold time. table is the first
// word of the object at its selection, for the test that the object still
// lives. True when the mod asked the game for the stay. False when the game
// would refuse it now: then the mod sends nothing.
bool CreatureStayStart(void* creature, void* table, double now,
                       uint64_t frame);

// far_grab.cpp: the grip opened, or the hand cannot hold it. why is the
// reason, for the log. The Creature is free when the game is ready for it
// (CreatureStayFrame).
void CreatureStayEnd(const char* why, double now, uint64_t frame);

// far_grab.cpp: the game deleted the Creature. Nothing reads it again.
void CreatureStayGone();

// True from the start of a stay until its end is asked for.
bool CreatureStayHolds();

// far_grab.cpp, once a frame, before the eye passes: the end of a stay, the
// log of each change, and the test key.
void CreatureStayFrame(double now, uint64_t frame);

// Once a report window.
void CreatureStayReport();
