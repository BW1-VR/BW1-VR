// The village with the real hand (docs/hand.md, "The village with the real
// hand"): the totem of the Village Centre, and the miracle icons of the
// Worship Site.

#pragma once

#include <stdint.h>

#include "miracle.h"

// mod_main.cpp, with the direct hand: reads the keys and installs the hook.
void VillageInstall();

// stereo.cpp, once a frame, after MiracleFrame.
void VillageFrame();

// stereo.cpp, once a report window.
void VillageReport();

// hand_direct.cpp, at the end of the first eye pass: the thing at the Hand
// that takes the object in the Hand (a store, a pile, a scaffold, an altar,
// a Creature), from the objects that the draw offered.
void VillageSelect();

// hand_direct.cpp, after each eye pass, before each interface step and
// before each press of the Action button: the pick record holds that thing.
void VillageKeepRecord();

// miracle.cpp, in the hook of the game's press on an object (fn_005D30D0):
// the game sent the packets that put the object in the Hand on this object.
void VillageApplied(void* held, void* object);

// hand_direct.cpp, for the test place "villager": the villager passes the
// tests of the game for a job from a thing near him (Villager::InHand).
bool VillageCanHaveJob(void* villager);

// hand_glow.cpp: the halo of that thing.
struct FarDraw;
bool VillageHalo(FarDraw* out);

// hint_card.cpp: the lines of the card while the Hand's point is at the
// totem or at a miracle icon, and while the Hand moves the totem. The return
// value is the count of lines, 0 with no such thing at the Hand.
int VillageHint(MiracleLine lines[kMiracleLines]);

// hand_direct.cpp, for the test word "seek" (the test key seek_plan). A place
// of the village of the player, from the data of the game and not from the
// draw. False when the game has no such place now.
enum VillagePlace {
  kVillageSite,      // the Worship Site of the first town of the player
  kVillageCentre,    // the totem of that town, on the land
  kVillageIcon,      // the next miracle icon of that Worship Site
  kVillageTotem,     // a point on the pole of the totem
  kVillageScaffold,  // the next scaffold of the test key village_test_scaffolds
  kVillageStore,     // the Village Store of that town
  kVillageWorkshop,  // the Workshop of that town
};
bool VillageTestPlace(int place, float out[3]);
