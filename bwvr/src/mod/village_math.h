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

// THE GHOST OF THE POLE [V]. While the value that the pole shows is not the
// value of the town, TotemStatue::Draw (0x00738960) draws the two meshes of
// the totem a second time at the other value, with an alpha of 0x80 (the
// byte at +0x4F of the 3D object). While the Hand moves the totem, the pole
// is at the value of the Hand and the ghost at the value that the pole
// showed. After the release the game moves the pole to the new value, and
// the ghost is there until the pole arrives ("The pole after the release").
// The ghost is the same mesh, higher or lower on the line of the pole. So each
// vertical face of the ghost is in the plane of a face of the pole, and
// where the two meshes overlap, the depth test gives noise: the faces fight.
//
// With totem_ghost=1 the ghost has kGhostWidth of the width of the pole and
// the same height. Each side of the ghost is then inside the pole, and the
// pole hides the ghost where the two overlap. LH3DObject::SetPosition
// (0x00423140) gets the scale, and the y row of the matrix (+0x24) gets the
// scale of the game again.
const float kGhostWidth = 0.97f;

// The scale that the ghost gets for the scale of the game.
inline float GhostScale(float scale, bool narrow) {
  return narrow ? scale * kGhostWidth : scale;
}

// How far a face of the ghost is inside the face of the pole, for a face at
// this distance from the line of the pole.
inline float GhostInset(float radius, bool narrow) {
  return radius - GhostScale(radius, narrow);
}

// THE POLE AFTER THE RELEASE [V]. The value that the pole shows (+0x9C) is
// only the picture. The release (TotemStatue::NetworkUnfriendlyEndLockedSelect
// 0x00738630) sends packet 0x29 with the value of the Hand (+0x88), and then
// packet 0x1C ends the locked select. Packet 0x29 gives the value to the town
// (Town::SetWorshipPercentage 0x0073C060, Town+0x5C0) and to the totem
// (TotemStatue::SetWorshipPercentage 0x00738270). The town has the new value
// at once. The totem keeps it at +0x80, and the pole goes from the value that
// it shows to the new value in kTotemSlideMs for each 1.0 of change from the
// old +0x80. When that time is less than kTotemSnapMs, the game puts the pole
// at the new value at once. TotemStatue::Draw shows the pole at +0x88 while
// the Hand moves the totem (bit 0x10 of +0x24) and at +0x9C after it, and it
// draws the ghost at the other one of the two values.
//
// With totem_hold=1 the mod writes the new value to +0x80 just before the
// call of packet 0x29, so the game takes its own path with no slide. The
// pole then stays where the Hand let it go.
const float kTotemSlideMs = 5200.0f;
const float kTotemSnapMs = 0.001f;

// The time in ms in which the game moves the pole to a new value: from the
// value that the totem had (+0x80), not from the value that the pole shows.
inline float TotemSlideMs(float value_before, float value_new) {
  return fabsf(value_before - value_new) * kTotemSlideMs;
}

// The value before (+0x80) that the game must see for a new value. With
// totem_hold=1 and the value of a release, it is the new value.
inline float TotemValueBefore(float value_before, float value_new, bool hold,
                              bool from_release) {
  return hold && from_release ? value_new : value_before;
}

// The value at which TotemStatue::Draw puts the pole, and the value of the
// ghost. The game draws the ghost only when the two are not the same.
inline float TotemPoleValue(float shown, float drag, bool locked) {
  return locked ? drag : shown;
}
inline float TotemGhostValue(float shown, float drag, bool locked) {
  return locked ? shown : drag;
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

// A CREATURE THAT IS THE TARGET STAYS THE TARGET. The body of a small
// Creature is some cm of the room across, and it moves when the Creature
// turns. So a Creature that was the target in the last frame has the reach of
// each other thing. It becomes the target only in its body.
inline float HoldReachNow(int onto, float reach, bool was_target,
                          bool creature_stays) {
  if (onto == kOntoCreature && was_target && creature_stays) return reach;
  return HoldReach(onto, reach);
}

// The thing is the target when the smaller of its two gaps is in its reach,
// and smaller than the gap of the best thing before it. A gap is 0 in the
// body.
inline bool HoldTakes(float gap_hand, float gap_held, float reach,
                      float best) {
  const float gap = gap_hand < gap_held ? gap_hand : gap_held;
  return gap <= reach && gap < best;
}

// THE GIFT TO A CREATURE [V].
//
// A press with an object in the Hand on a Creature that takes it sends packet
// 0x1F (fn_005D3080). Its handler (fn_005DA2D0) starts the action of the
// Creature that takes an object from the Hand (action 0x8E), and the Hand
// keeps the Creature at CHand+0x49BC. From then HandStateHolding::Update
// (0x005B3C70) measures its Hand against the hand-over point of the Creature
// (fn_00480810): the place of the hand of the Creature in its take animation,
// on the side of the camera. The game puts its Hand at the distance of that
// point from the view, so the Hand of the mouse is near it. When the Hand is
// more than 15 times the size of the Creature (LH3DCreature+0x90) from the
// point, the game sends packet 0x52 (0x005B424C). The handler of that packet
// ends the action of the Creature and the gift, and the object stays in the
// Hand.
//
// The Hand of the mod stays at the real hand. So the test of the game
// measures a place that the game did not bring the Hand to, and it ends a
// gift that the player did not take back. The test of the mod takes its
// place: the gift goes on while the Hand's point is at the Creature.
const float kGiftGameSizes = 15.0f;    // the 15.0 at 0x008C2C40

// The reach of the test of the game, in world units.
inline float GiftGameReach(float size) { return kGiftGameSizes * size; }

// The reach of the test of the mod, in world units, from the body of the
// Creature to the Hand's point: the reach of the test of the game, and
// keep_cm of the room more.
inline float GiftReach(float size, float units_per_meter, float keep_cm) {
  return GiftGameReach(size) + keep_cm * 0.01f * units_per_meter;
}

// True: the gift goes on, and the end that the game asks for does not go.
// With no scale of the room the game decides.
inline bool GiftStays(float distance, float size, float units_per_meter,
                      float keep_cm) {
  return units_per_meter > 0.0f && size > 0.0f &&
         distance <= GiftReach(size, units_per_meter, keep_cm);
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

// A THING THAT A SCRIPT TURNS TO THE CAMERA [V]. The script word SET_FOCUS
// (GScript::SetFocus 0x006F90B0) gives a thing a point. For a villager,
// Object::SetFocus (0x006393A0) turns the whole body to the point on the map:
// LH3DMath::GetYAngle (0x00841260) is atan2(dz, dx) from the thing to the
// point, from 0 to 2 pi. The height of the point does not count.
inline float FocusYAngle(float from_x, float from_z, float to_x, float to_z) {
  const float two_pi = 6.28318531f;
  float angle = atan2f(to_z - from_z, to_x - from_x);
  if (angle < 0.0f) angle += two_pi;
  return angle;
}

// The angle of the game for a heading (GUtils::ConvertAngle3DToGame
// 0x0074DC30 keeps 11 bits: 2048 steps for a full turn), in degrees.
inline float GameAngleDegrees(unsigned game_angle) {
  return static_cast<float>(game_angle & 0x7FFu) * (360.0f / 2048.0f);
}

// The turn from heading a to heading b, in degrees, from -180 to 180.
inline float TurnDegrees(float a_degrees, float b_degrees) {
  float turn = fmodf(b_degrees - a_degrees, 360.0f);
  if (turn > 180.0f) turn -= 360.0f;
  if (turn <= -180.0f) turn += 360.0f;
  return turn;
}

// True when the point of SET_FOCUS goes to the head (villager_look_head=1).
// The point must be the engine camera to the last bit: that is the script
// word "camera position" (GScript::GetCameraPos 0x006ECFF0). A point that a
// script keeps, a marker and each other point stay. The player must stand for
// the camera at the moment of the word (near_math::PlayerIsCamera).
inline bool FocusOnHead(bool key_on, bool player_is_camera,
                        const unsigned point_bits[3],
                        const unsigned camera_bits[3]) {
  return key_on && player_is_camera && point_bits[0] == camera_bits[0] &&
         point_bits[1] == camera_bits[1] && point_bits[2] == camera_bits[2];
}

}  // namespace village_math
