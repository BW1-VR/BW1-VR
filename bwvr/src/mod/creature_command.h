// The Creature commands: the trigger commands the leashed Creature, and a
// shake of the real hand takes the leash off. See creature_command.cpp and
// docs/creature.md, "The commands".
#pragma once

#include <stdint.h>

struct FarDraw;
struct TeleportDraw;

// hand_direct.cpp, once a frame: true while the command needs the cone of the
// aim ray, so the draw offers the objects in it.
bool CreatureCommandWantsCone();

// hand_direct.cpp, after the first eye pass: the target of the aim, from the
// candidates of the cone.
void CreatureCommandSelect();

// controllers.cpp, once a pump, before the buttons go to the game. trigger is
// the trigger of the pointing hand, and ok is true when the hand may act now.
// The answer is true while the trigger must not reach the game.
bool CreatureCommandPumpTrigger(bool trigger, bool ok);

// stereo.cpp, once a frame, after TeleportFrame.
void CreatureCommandFrame();

// Once a report window.
void CreatureCommandReport();

// hand_glow.cpp: the halo of the target of the command. False for none.
// far_target is true when the target is the target of the grab from a
// distance too, which has its own halo.
bool CreatureCommandHalo(FarDraw* out, bool* far_target);

// overlay.cpp: the arc and the ring of the land aim. False for none.
bool CreatureCommandDrawState(TeleportDraw* out);

// hand_glow.cpp: the trigger holds a walk that began on the land, and that
// stays a walk (creature_command_rule=3). No object has a halo then.
bool CreatureCommandWalkHeld();

// hint_card.cpp: what the trigger does now.
enum CreatureCommandHintKind {
  kCommandHintNone,      // the command does not act now
  kCommandHintLeashed,   // the leash is on, and the aim is on nothing
  kCommandHintObject,    // the aim is on an object the Creature can act on
  kCommandHintCreature,  // the aim is on the player's own Creature
  kCommandHintAim,       // the trigger is held, and the arc shows the land
  // The leash is tied to an object: the aim is on that object (a double
  // press takes the leash back), or on the Creature (a short press does).
  kCommandHintTied,
  kCommandHintTiedCreature,
};
int CreatureCommandHint();

// hint_card.cpp: the leash can go on the object of the hint, with a double
// press of the trigger.
bool CreatureCommandCanTie();

// hint_card.cpp: true while the leash is on the Hand and the trigger
// commands the Creature. The trigger then does not reach the game, and a
// grip on the land does not send the Creature.
bool CreatureCommandLeashOn();

// controllers.cpp, for the fake controller's word "aimcmd" in a desktop run:
// the middle of the body of the nearest object that a command can act on,
// from the objects that the last draw offered. False for none.
bool CreatureCommandTestPoint(float out[3]);

// hand_direct.cpp: the player asked for a change of the leash with the grip
// (the leash click at the Creature, or from a distance). A change of the
// leash soon after it gives a pulse (LeashFrame).
void CreatureCommandLeashAsked();
