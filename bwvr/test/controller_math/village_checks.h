// THE VILLAGE WITH THE REAL HAND (docs/village.md, "The village with the real
// hand"). main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <wchar.h>

#include "../../src/mod/village_math.h"

namespace village_checks {

// The game's own math of the totem (HandStateTotem::Update 0x005B77A0 and
// TotemStatue::NetworkUnfriendlyLockedSelect 0x007386A0): the value after
// one frame with this many rows.
inline float GameTotemStep(float value, int rows, float height,
                           float screen_height) {
  const float move = rows * height * village_math::kTotemRowGain / screen_height;
  float next = value + village_math::kTotemMoveGain * move;
  if (next < 0.0f) next = 0.0f;
  if (next > 1.0f) next = 1.0f;
  return next;
}

inline void VillageChecks() {
  using namespace village_math;
  printf("\nThe village with the real hand\n");

  // THE TOTEM FOLLOWS THE REAL HAND ONE TO ONE: 8 units of the hand are the
  // whole travel of the pole.
  Near("a still hand keeps the value", TotemValue(0.4f, 20.0f, 20.0f), 0.4,
       1e-6);
  Near("the hand 2 units up: a quarter of the travel",
       TotemValue(0.4f, 20.0f, 22.0f), 0.65, 1e-6);
  Near("the hand 1.6 units down", TotemValue(0.4f, 20.0f, 18.4f), 0.2, 1e-6);
  Near("the hand far up: the value stops at 1",
       TotemValue(0.4f, 20.0f, 60.0f), 1.0, 1e-6);
  Near("the hand far down: the value stops at 0",
       TotemValue(0.4f, 20.0f, -60.0f), 0.0, 1e-6);
  // The hand comes back from above the top: the pole waits until the hand is
  // at the place where it took the pole plus the travel that was left.
  Near("back from above the top: still 1 at 5 units up",
       TotemValue(0.4f, 20.0f, 25.0f), 1.0, 1e-6);
  Near("back from above the top: 0.9 at 4 units up",
       TotemValue(0.4f, 20.0f, 24.0f), 0.9, 1e-6);

  // THE ROWS: the game's own math brings its value to the wanted one.
  const float height = 12.0f, screen = 2880.0f;
  Check(TotemRows(0.4f, 0.4f, height, screen) == 0, "no move: no rows",
        TotemRows(0.4f, 0.4f, height, screen), 0);
  {
    const int rows = TotemRows(0.4f, 0.65f, height, screen);
    Check(rows > 0, "a higher value: the rows go up", rows, 1);
    Near("one frame of the game brings the value there",
         GameTotemStep(0.4f, rows, height, screen), 0.65, 0.002);
  }
  {
    const int rows = TotemRows(0.65f, 0.2f, height, screen);
    Check(rows < 0, "a lower value: the rows go down", rows, -1);
    Near("one frame of the game brings the value down",
         GameTotemStep(0.65f, rows, height, screen), 0.2, 0.002);
  }
  {
    // A desktop screen of 600 rows: a row is a larger step, and the rest goes
    // in the next frame, so no error adds up over a slow lift.
    float value = 0.1f;
    const float low = 600.0f;
    for (int i = 1; i <= 40; ++i) {
      const float wanted = TotemValue(0.1f, 20.0f, 20.0f + 0.1f * i);
      value = GameTotemStep(value, TotemRows(value, wanted, height, low),
                            height, low);
    }
    Near("40 small steps of the hand, 4 units in all: half of the travel",
         value, 0.6, 0.004);
  }
  Check(TotemRows(0.4f, 0.9f, 0.0f, screen) == 0,
        "a totem with no height: no rows", TotemRows(0.4f, 0.9f, 0.0f, screen),
        0);
  Check(TotemRows(0.4f, 0.9f, height, 0.0f) == 0, "no screen: no rows",
        TotemRows(0.4f, 0.9f, height, 0.0f), 0);
  Check(TotemRows(0.0f, 1.0f, 0.02f, screen) <= 4 * 2880,
        "the rows of one frame have a limit",
        TotemRows(0.0f, 1.0f, 0.02f, screen), 4 * 2880);

  // THE COUNT OF THE WORSHIPPERS, as Town::GetWorshipersNeeded.
  Check(Worshippers(15, 0.0f) == 0, "value 0: no villager worships",
        Worshippers(15, 0.0f), 0);
  Check(Worshippers(15, 0.4f) == 6, "40 percent of 15 villagers is 6",
        Worshippers(15, 0.4f), 6);
  Check(Worshippers(15, 0.01f) == 1, "a small value sends one villager",
        Worshippers(15, 0.01f), 1);
  Check(Worshippers(15, 1.0f) == 15, "value 1: each villager worships",
        Worshippers(15, 1.0f), 15);
  Check(Worshippers(0, 0.5f) == 0, "a town with no villager",
        Worshippers(0, 0.5f), 0);
  Check(Percent(0.404f) == 40, "the percent of the card", Percent(0.404f), 40);
  Check(Percent(1.2f) == 100 && Percent(-0.1f) == 0,
        "the percent stays in 0 to 100", Percent(1.2f), 100);

  // THE MIRACLE ICON: what a tap does.
  Check(IconStateOf(true, false, false, true, 500.0f, 0.0f) == kIconIdle,
        "an icon of the player with chants at the site: a tap starts it",
        IconStateOf(true, false, false, true, 500.0f, 0.0f), kIconIdle);
  Check(IconStateOf(true, true, true, true, 500.0f, 100.0f) == kIconCharging,
        "an icon that charges for the player: a tap stops it",
        IconStateOf(true, true, true, true, 500.0f, 100.0f), kIconCharging);
  Check(IconStateOf(true, true, false, true, 500.0f, 100.0f) == kIconOther,
        "an icon that charges for another player",
        IconStateOf(true, true, false, true, 500.0f, 100.0f), kIconOther);
  Check(IconStateOf(true, false, false, true, 0.0f, 0.0f) == kIconNoPower,
        "no chants at the site and none in the icon: no power",
        IconStateOf(true, false, false, true, 0.0f, 0.0f), kIconNoPower);
  Check(IconStateOf(true, false, false, true, 0.0f, 80.0f) == kIconIdle,
        "chants in the icon are enough for a start",
        IconStateOf(true, false, false, true, 0.0f, 80.0f), kIconIdle);
  Check(IconStateOf(true, false, false, false, 500.0f, 0.0f) == kIconHandFull,
        "the Hand holds an object: no start",
        IconStateOf(true, false, false, false, 500.0f, 0.0f), kIconHandFull);
  Check(IconStateOf(false, false, false, true, 500.0f, 0.0f) == kIconNotYours,
        "the icon of another god",
        IconStateOf(false, false, false, true, 500.0f, 0.0f), kIconNotYours);
  Check(ChargePercent(250.0f, 750.0f) == 25, "250 of 1000 chants: 25 percent",
        ChargePercent(250.0f, 750.0f), 25);
  Check(ChargePercent(1000.0f, 0.0f) == 100, "a full icon: 100 percent",
        ChargePercent(1000.0f, 0.0f), 100);
  Check(ChargePercent(0.0f, 0.0f) == 0, "an icon that needs nothing",
        ChargePercent(0.0f, 0.0f), 0);

  // AN OBJECT IN THE HAND and the thing that takes it.
  Check(HoldTakes(3.0f, 9.0f, 4.0f, 1e30f), "a body in the reach of the Hand",
        1, 1);
  Check(HoldTakes(9.0f, 1.0f, 4.0f, 1e30f),
        "a body in the reach of the object in the Hand", 1, 1);
  Check(!HoldTakes(5.0f, 6.0f, 4.0f, 1e30f), "a body out of the reach", 0, 0);
  Check(!HoldTakes(3.0f, 9.0f, 4.0f, 2.0f),
        "a nearer body before it stays the target", 0, 0);
  Check(HoldTakes(0.0f, 5.0f, 0.0f, 1e30f),
        "no reach: the Hand's point in the body is enough", 1, 1);
  Check(!HoldTakes(0.5f, 0.5f, 0.0f, 1e30f),
        "no reach: a point beside the body is not", 0, 0);
  Near("a sacrifice has no reach", HoldReach(kOntoAltar, 2.7f), 0.0, 1e-6);
  Near("a Creature has no reach", HoldReach(kOntoCreature, 2.7f), 0.0, 1e-6);
  Near("a store has the reach of the key", HoldReach(kOntoStore, 2.7f), 2.7,
       1e-6);
  // The line of the card for each pair that the game accepts.
  Check(HoldLineOf(kHeldWood, kOntoStore) == kLineStoreWood,
        "wood at the Village Store", HoldLineOf(kHeldWood, kOntoStore),
        kLineStoreWood);
  Check(HoldLineOf(kHeldFood, kOntoStore) == kLineStoreFood,
        "food at the Village Store", HoldLineOf(kHeldFood, kOntoStore),
        kLineStoreFood);
  Check(HoldLineOf(kHeldCrop, kOntoStore) == kLineStoreFood,
        "a crop at the Village Store", HoldLineOf(kHeldCrop, kOntoStore),
        kLineStoreFood);
  Check(HoldLineOf(kHeldWood, kOntoWorkshop) == kLineWorkshopWood,
        "wood at the Workshop", HoldLineOf(kHeldWood, kOntoWorkshop),
        kLineWorkshopWood);
  Check(HoldLineOf(kHeldWood, kOntoOther) == kLineBuildWood &&
            HoldLineOf(kHeldWood, kOntoScaffold) == kLineBuildWood,
        "wood at a building site and at a planted scaffold",
        HoldLineOf(kHeldWood, kOntoOther), kLineBuildWood);
  Check(HoldLineOf(kHeldFood, kOntoWorshipSite) == kLineWorshipFood,
        "food at the Worship Site", HoldLineOf(kHeldFood, kOntoWorshipSite),
        kLineWorshipFood);
  Check(HoldLineOf(kHeldWood, kOntoPile) == kLinePile &&
            HoldLineOf(kHeldFood, kOntoPile) == kLinePile,
        "a pot on a pile of its kind", HoldLineOf(kHeldWood, kOntoPile),
        kLinePile);
  Check(HoldLineOf(kHeldTree, kOntoStore) == kLineTreeWood &&
            HoldLineOf(kHeldTree, kOntoWorkshop) == kLineTreeWood,
        "a tree at a store of wood", HoldLineOf(kHeldTree, kOntoStore),
        kLineTreeWood);
  Check(HoldLineOf(kHeldAnimal, kOntoStore) == kLineAnimalFood,
        "an animal at the Village Store", HoldLineOf(kHeldAnimal, kOntoStore),
        kLineAnimalFood);
  Check(HoldLineOf(kHeldVillager, kOntoAltar) == kLineSacrifice &&
            HoldLineOf(kHeldAnimal, kOntoAltar) == kLineSacrifice &&
            HoldLineOf(kHeldTree, kOntoAltar) == kLineSacrifice,
        "each thing at the altar is a sacrifice",
        HoldLineOf(kHeldVillager, kOntoAltar), kLineSacrifice);
  Check(HoldLineOf(kHeldScaffold, kOntoScaffold) == kLineJoin,
        "a scaffold at a scaffold", HoldLineOf(kHeldScaffold, kOntoScaffold),
        kLineJoin);
  Check(HoldLineOf(kHeldScaffold, kOntoWorkshop) == kLineScaffoldPut,
        "a scaffold at an other thing",
        HoldLineOf(kHeldScaffold, kOntoWorkshop), kLineScaffoldPut);
  Check(HoldLineOf(kHeldVillager, kOntoTeleport) == kLineTeleport,
        "a villager at a teleport", HoldLineOf(kHeldVillager, kOntoTeleport),
        kLineTeleport);
  Check(HoldLineOf(kHeldFood, kOntoCreature) == kLineGive &&
            HoldLineOf(kHeldOther, kOntoCreature) == kLineGive,
        "each object that a Creature takes",
        HoldLineOf(kHeldFood, kOntoCreature), kLineGive);
  Check(wcscmp(JobName(2), L"a forester") == 0 &&
            wcscmp(JobName(1), L"a farmer") == 0 &&
            wcscmp(JobName(4), L"a builder") == 0 &&
            wcscmp(JobName(8), L"a craftsman") == 0 &&
            wcscmp(JobName(11), L"a worshipper") == 0,
        "the name of each job of a villager", 1, 1);
  Check(wcscmp(JobName(6), L"a disciple") == 0 &&
            wcscmp(JobName(0), L"a disciple") == 0,
        "a job with no name of its own", 1, 1);
  Check(HoldLineOf(kHeldOther, kOntoOther) == kLineOther,
        "a pair with no words of its own", HoldLineOf(kHeldOther, kOntoOther),
        kLineOther);
}

}  // namespace village_checks
