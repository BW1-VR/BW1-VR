// The pure math of the village with the real hand (village.cpp). See
// docs/village.md, "The village with the real hand".
//
// No Windows and no game in this file, so the math test can run it.

#pragma once

#include <math.h>

namespace village_math {

// THE TOTEM OF THE VILLAGE CENTRE [V, runblack.exe 1.42].
//
// The game keeps the value of the totem while the Hand moves it at
// TotemStatue+0x88, from 0 to 1. HandStateTotem::Update (0x005B77A0) reads
// how many screen rows the pointer went up since the last frame, and gives
// the totem
//
//   move = rows * height * 3 / screen_height
//
// TotemStatue::NetworkUnfriendlyLockedSelect (0x007386A0) adds 0.1 * move to
// the value and keeps it in 0 to 1. TotemStatue::Draw (0x00738960) puts the
// pole 8 units higher for each 1.0 of the value.
const float kTotemRowGain = 3.0f;      // the 3.0 at 0x008C2C50
const float kTotemMoveGain = 0.1f;     // the 0.1 at 0x008AC404
const float kTotemTravel = 8.0f;       // the 8.0 at 0x008C2C70, in world units

// The value that puts the pole where the real hand is: the pole goes up one
// unit for each unit that the hand went up since the grip closed.
inline float TotemValue(float value_at_grab, float hand_y_at_grab,
                        float hand_y) {
  float value = value_at_grab + (hand_y - hand_y_at_grab) / kTotemTravel;
  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;
  return value;
}

// The screen rows that the game must see in this frame, so that its own math
// brings the value from value_now to wanted. A row is a whole number, so the
// next frame takes the rest: the caller gives the value of the game each
// frame, and no error adds up.
inline int TotemRows(float value_now, float wanted, float height,
                     float screen_height) {
  if (!(height > 0.01f) || !(screen_height > 1.0f)) return 0;
  const float move = (wanted - value_now) / kTotemMoveGain;
  const float rows = move * screen_height / (kTotemRowGain * height);
  // The game takes at most a few screens of rows in one frame.
  const float limit = 4.0f * screen_height;
  const float clamped = rows > limit ? limit : (rows < -limit ? -limit : rows);
  return static_cast<int>(clamped >= 0.0f ? clamped + 0.5f : clamped - 0.5f);
}

// How many villagers the town sends to worship for a value of the totem
// (Town::GetWorshipersNeeded 0x0073C860): the adults times the value, to the
// nearest whole number, and one at the least while the value is above 0.
inline int Worshippers(int adults, float value) {
  if (!(value > 0.0f) || adults <= 0) return 0;
  const int count = static_cast<int>(adults * value + 0.5f);
  return count > 1 ? count : 1;
}

// The value as a percent for the card, as the tooltip of the game shows it
// (the value times 100, 0x00738CDD).
inline int Percent(float value) {
  const float p = value * 100.0f;
  const int whole = static_cast<int>(p + 0.5f);
  return whole < 0 ? 0 : (whole > 100 ? 100 : whole);
}

// THE MIRACLE ICON OF A WORSHIP SITE [V].
//
// A tap on the icon (WorshipSpellIcon::ActualInterfaceTap 0x0077F880) does
// one of three things, in this order: an icon that is full gives its miracle
// to the Hand, an icon that charges for this player stops and gives its
// chants back, and each other icon starts to charge. The Worship Site gives
// chants to each icon that charges, once a game turn
// (WorshipSite::ProcessSpellIcons 0x0077B4D0). The miracle comes into the
// Hand when the icon has the chants that the miracle needs.
enum IconState {
  kIconIdle,       // a tap starts the charge
  kIconCharging,   // the icon charges for this player: a tap stops it
  kIconOther,      // the icon charges for another player
  kIconNoPower,    // the site has no chants, so a tap does nothing
  kIconNotYours,   // the icon of another god
  kIconHandFull,   // the Hand holds an object, so a tap does nothing
};

inline int IconStateOf(bool mine, bool charging, bool for_me, bool hand_ready,
                       float site_chants, float icon_chants) {
  if (!mine) return kIconNotYours;
  if (charging) return for_me ? kIconCharging : kIconOther;
  // A store that is not empty can be stopped by the player who filled it.
  if (!hand_ready) return kIconHandFull;
  if (!(site_chants > 0.0f) && !(icon_chants > 0.0f)) return kIconNoPower;
  return kIconIdle;
}

// The charge of an icon as a percent of the chants that its miracle needs.
inline int ChargePercent(float have, float needed_more) {
  const float all = have + (needed_more > 0.0f ? needed_more : 0.0f);
  if (!(all > 0.0f)) return 0;
  const int p = static_cast<int>(have / all * 100.0f + 0.5f);
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// AN OBJECT IN THE HAND THAT GOES ONTO A THING [V].
//
// A press of the Action button with an object in the Hand (FUN_005D1560)
// acts on the object of the pick record when the game accepts it for the
// object in the Hand: the object in the Hand says yes to it
// (ValidToApplyThisToObject, slot 0x71C), or it is a Creature that takes the
// object (Creature::InterfaceValidToGiveObject 0x0047A330). Else the press
// sets the object down on the land. The mouse has its pointer on the thing
// then. The real hand has the object at the thing, so the mod looks at the
// gap from the Hand's point, and from the object in the Hand, to the body of
// each thing that the game accepts.

// What the Hand holds, by the rule of its class.
enum HeldKind {
  kHeldOther,
  kHeldWood,      // a pot of wood (PileWood, MagicWood)
  kHeldFood,      // a pot of food (PileFood, MagicFood)
  kHeldCrop,      // a crop of a field: food
  kHeldTree,      // a tree, a felled tree: wood
  kHeldAnimal,
  kHeldVillager,
  kHeldScaffold,
};

// The thing that takes it.
enum OntoKind {
  kOntoOther,     // a building that needs wood, and each other thing
  kOntoStore,     // the Village Store
  kOntoWorkshop,
  kOntoWorshipSite,
  kOntoPile,      // a pile of the same resource
  kOntoScaffold,
  kOntoAltar,     // the totem of a Worship Site: a sacrifice
  kOntoTeleport,
  kOntoCreature,
};

// The line of the card for the grip.
enum HoldLine {
  kLineNone,
  kLineStoreWood,     // "Open to store the wood"
  kLineStoreFood,     // "Open to store the food"
  kLineWorkshopWood,  // "Open to give the wood to the Workshop"
  kLineBuildWood,     // "Open to give the wood to the builders"
  kLineWorshipFood,   // "Open to give the food to the worshippers"
  kLinePile,          // "Open to add to the pile"
  kLineTreeWood,      // "Open to give the tree as wood"
  kLineAnimalFood,    // "Open to store it as food"
  kLineSacrifice,     // "Open to sacrifice it here"
  kLineJoin,          // "Open to join the scaffolds"
  kLineScaffoldPut,   // "Open to put the scaffold here"
  kLineTeleport,      // "Open to send the villager through"
  kLineGive,          // "Open to give it to the Creature"
  kLineOther,         // "Open to put it here"
};

inline int HoldLineOf(int held, int onto) {
  if (onto == kOntoCreature) return kLineGive;
  if (onto == kOntoAltar) return kLineSacrifice;
  if (held == kHeldScaffold) {
    return onto == kOntoScaffold ? kLineJoin : kLineScaffoldPut;
  }
  if (held == kHeldVillager) {
    return onto == kOntoTeleport ? kLineTeleport : kLineOther;
  }
  if (onto == kOntoPile) return kLinePile;
  if (held == kHeldWood) {
    return onto == kOntoStore      ? kLineStoreWood
           : onto == kOntoWorkshop ? kLineWorkshopWood
                                   : kLineBuildWood;
  }
  if (held == kHeldFood || held == kHeldCrop) {
    return onto == kOntoStore         ? kLineStoreFood
           : onto == kOntoWorshipSite ? kLineWorshipFood
                                      : kLineOther;
  }
  if (held == kHeldTree) return kLineTreeWood;
  if (held == kHeldAnimal && onto == kOntoStore) return kLineAnimalFood;
  return kLineOther;
}

// The reach of a thing. A sacrifice has no reach: the Hand's point, or the
// object in the Hand, must be in the body of the altar. So a villager that
// the player sets down beside the altar is no sacrifice. A Creature has no
// reach for the same cause: it takes most objects, and it comes to the Hand
// by itself (desktop run vilQ1: a scaffold had the Creature as its target at
// 1.6 cm).
inline float HoldReach(int onto, float reach) {
  return onto == kOntoAltar || onto == kOntoCreature ? 0.0f : reach;
}

// The thing is the target when the smaller of its two gaps is in its reach,
// and smaller than the gap of the best thing before it. A gap is 0 in the
// body.
inline bool HoldTakes(float gap_hand, float gap_held, float reach,
                      float best) {
  const float gap = gap_hand < gap_held ? gap_hand : gap_held;
  return gap <= reach && gap < best;
}

// A VILLAGER IN THE HAND AND HIS JOB [V]. The game gives the job from the
// things near the villager (Villager::InHand 0x0076ADE0), and the villager
// keeps it when the Hand lets him go. The values are those of the functions
// of the things: 1 a field, 2 a tree, 4 a building that is not complete, 5 a
// villager, 7 a building of another god, 8 the Workshop, 9 a store of
// another town, 11 the Worship Site. 3 is the fisherman of a fish farm.
inline const wchar_t* JobName(int job) {
  switch (job) {
    case 1: return L"a farmer";
    case 2: return L"a forester";
    case 3: return L"a fisherman";
    case 4: return L"a builder";
    case 5: return L"a breeder";
    case 7: return L"a missionary";
    case 8: return L"a craftsman";
    case 9: return L"a trader";
    case 11: return L"a worshipper";
    default: return L"a disciple";
  }
}

}  // namespace village_math
