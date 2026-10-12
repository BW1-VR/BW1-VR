// THE VILLAGE WITH THE REAL HAND. See docs/village.md, "The village with the
// real hand".
//
// WHAT THE GAME DOES [V, runblack.exe 1.42].
// - THE TOTEM of the Village Centre is a TotemStatue. A press of the Action
//   button on it starts the locked select of the game (the same action as a
//   take from a store), because TotemStatue::ValidForLockedSelectProcess
//   (0x00738500) says yes for the totem of the player. The interface then
//   has hand mode 0xE (fn_005D80C0), and the Hand is in state 5, Totem.
//   HandStateTotem::Update (0x005B77A0) puts the Hand on the top of the pole
//   and reads how many screen rows the pointer moved since the last frame:
//   move = rows * height * 3 / screen height. The totem adds 0.1 * move to
//   its value at +0x88, in 0 to 1 (0x007386A0), and the pole is 8 units
//   higher for each 1.0 (TotemStatue::Draw). The game shows the value times
//   100 in a tooltip. The release sends packet 0x29 with the value, and the
//   town then sends that part of its villagers to the Worship Site.
// - A MIRACLE ICON of a Worship Site is a WorshipSpellIcon. A tap on it
//   (SpellIcon::InterfaceTap 0x00726430, WorshipSpellIcon::ActualInterfaceTap
//   0x0077F880) starts its charge. The Worship Site gives its chants to each
//   icon that charges, once a game turn (0x0077B4D0). When the icon has the
//   chants that the miracle needs, the SpellSeed goes into the Hand
//   (0x0077F8F0). A second tap while it charges stops it, and the chants go
//   back.
//
// WHAT THE MOD DOES.
// - totem_hand=1: while the Hand is in the Totem state and the controller
//   owns the Hand, the pole follows the real hand one to one: one unit up of
//   the hand is one unit up of the pole. The mod gives the game the screen
//   rows that make its own math do that (village_math.h). With 0 the game
//   reads the rows of the pixel of the Hand's point, as the plain game does.
// - The card says what the grip does at the totem and at a miracle icon, and
//   it shows the value of the totem as the game does, with the count of the
//   villagers that the town sends.
// - hold_target=1: while the Hand carries an object, the thing at the Hand
//   that the game accepts for it (the Village Store for wood and food, a
//   building that needs wood, a pile, a second scaffold, the altar of a
//   Worship Site, a Creature that takes the object) is the object of the pick
//   record. It has the halo, and the card says what the opening of the grip
//   does. So the release acts on it, as a release with the pointer on it does
//   in the plain game. With 0 the record has the object behind the pixel of
//   the Hand's point.
// - Each change goes to the log with the tag "village".
// - Test keys for the desktop runs (the launcher removes them):
//   village_test_site, village_test_miracles, village_test_chants,
//   village_test_census, village_test_icon, village_test_scaffolds,
//   village_test_workshop.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"
#include "controller_hints.h"
#include "far_grab.h"
#include "village.h"
#include "village_math.h"

extern void ModLog(const char* format, ...);

// controllers.cpp
uint64_t ControllersFrameCount();
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);
void ControllersGesturePulse(float amplitude, float seconds);
// hand_direct.cpp
bool HandDirectRealPoint(float out[3]);
bool HandDirectNearObject(void** object, float position[3]);
int HandDirectConeCount();
bool HandDirectConeAt(int index, void** object, float position[3]);
float HandDirectRadius(void* object);
bool HandDirectRecordTarget(void* object, const float position[3]);
bool HandDirectConeBody(int index, const float point[3], float* gap,
                        float center[3], float* radius, float* half);
bool HandDirectTakeRuns();
float HandDirectRoomUnits();
const char* HandDirectClass(void* object);
// miracle.cpp
const char* MiracleSeedName(int type);

namespace {

// --- the game ---------------------------------------------------------------

const uintptr_t kGame = 0x00D0195C;
// GGame::MyInterface, thiscall, no arguments.
const uintptr_t kMyInterface = 0x00555850;
// The height of the game's screen in pixels, a word.
const uintptr_t kScreenHeight = 0x00E8505A;
// GInterface.
const int kFaceInfluence = 0x48;     // the influence at the land point
const int kFaceStatus = 0x39C;       // the local GInterfaceStatus
const int kFaceHand = 0x3A0;         // the CHand
const int kFaceHandMode = 0x3AC;
const int kFaceObject = 0x3C8;       // the object of the pick record of now
const int kFaceNextObject = 0x400;   // the object of the next pick record
const int kFaceGetPlayer = 0x1C;     // a vtable slot, thiscall, no arguments
// GInterfaceStatus::IsHandReadyForObject, thiscall, no arguments.
const uintptr_t kHandReady = 0x005DC890;
// CHand.
const int kHandPosition = 0x78;
const int kHandRowNow = 0x4860;      // the screen row of the pointer, this frame
const int kHandRowLast = 0x4868;     // and of the last frame
const int kHandState = 0x4878;
const int kHandHeld = 0x4904;        // the object in the Hand
const int32_t kStateTotem = 5;
const int32_t kStateHolding = 4;
// Object::ValidToApplyThisToObject, a vtable slot (GInterfaceStatus*,
// Object*): the game lets the object in the Hand go on this object. The
// function in the slot tells the class of the object in the Hand.
const int kValidToApply = 0x71C;
const uintptr_t kApplyPot = 0x0066DD50;       // PileFood, PileWood, Magic*
const uintptr_t kApplyTree = 0x0074BD50;      // Tree, MagicTree
const uintptr_t kApplyDeadTree = 0x00510E90;  // DeadTree, FelledTree
const uintptr_t kApplyAnimal = 0x0041B320;
const uintptr_t kApplyVillager = 0x00752BD0;
const uintptr_t kApplyCrop = 0x00608360;      // FieldCrop
const uintptr_t kApplyScaffold = 0x006E9900;
// Object::GetResourceType, a vtable slot: 0 food, 1 wood.
const int kGetResourceType = 0x690;
// Object::ValidAsInterfaceTarget, Object::IsCreature and
// Object::InterfaceMustBeInInfluenceForInteraction, vtable slots.
const int kValidAsTarget = 0x6F0;
const int kIsCreature = 0x34;
const int kMustBeInInfluence = 0x714;
// Object::InterfaceValidToGiveObject, a vtable slot (GInterfaceStatus*,
// Object*). Only a Creature says yes (0x0047A330): it takes the object.
const int kValidToGive = 0x748;
// The vtables of the things that take an object.
const uintptr_t kStoreTable = 0x008F7030;        // StoragePit
const uintptr_t kWorkshopTable = 0x0099BB20;
const uintptr_t kWorshipSiteTable = 0x008F2840;
const uintptr_t kAltarTable = 0x0099E1BC;        // WorshipTotem
const uintptr_t kTeleportTable = 0x0092C110;     // MagicTeleport
// Town: the first StoragePit, the Village Store.
const int kTownStore = 0x30;
// Villager: the job that the game gave him (VILLAGER_DISCIPLE, 0 is none).
// Villager::InHand (0x0076ADE0) sets it each game turn from the objects near
// the villager, with Villager::SetVillagerDisciple (0x00756000).
const int kVillagerJob = 0xF2;
// The tests of Villager::InHand before it gives a job other than 11 (the
// Worship Site), at 0x0076AE6D to 0x0076AEBC: bit 0x400 of the word at +0x24
// and bit 0x80 of the byte at +0xE0 are clear, slot 0x2C says yes, and the
// slots 0xAF8, 0x4A0 and 0xAF4 say no. What each of them means was not
// traced: some villagers of a town pass and some do not.
const int kVillagerFlags = 0x24;
const int kVillagerState = 0xE0;
// SpellSeed, the miracle in the Hand (Object::IsSpellSeed, slot 0x4C4).
const int kIsSpellSeed = 0x4C4;
const int kSeedIcon = 0x5C;          // the icon of its Worship Site, or 0
const int kSeedPower = 0x6C;         // the power up
const int kSeedChants = 0x74;        // the chants in the seed
const int kSeedReady = 0x90;         // a byte
const int kSeedCast = 0x98;          // not 0: no power up now (0x005CEF63)
// HandState: the CHand at +4.
const int kStateHand = 4;
// HandStateTotem::Update, thiscall (float seconds, LHMatrix*), ret 8.
const uintptr_t kTotemUpdate = 0x005B77A0;
const uint8_t kTotemUpdateBytes[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC,
                                      0xA4, 0x00, 0x00, 0x00};
// GPlayer: the first town. Town: the next town of the player.
const int kPlayerTowns = 0xA50;
const int kPlayerCitadel = 0xA48;    // the Citadel, the Temple of the player
const int kCitadelHeart = 0x30;      // its CitadelHeart
// Town::GetTribe, thiscall, no arguments: the GTribeInfo of the town.
const uintptr_t kTownTribe = 0x0073C840;
// CitadelHeart::CreateBuiltWorshipSite, thiscall (const MapCoords*, info,
// Citadel*, GTribeInfo*), ret 0x10. The setup of a land calls it (0x00716133).
// It reads only the Citadel and the tribe: it finds or makes the Worship Site
// of the tribe, gives it the town of the player with that tribe
// (WorshipSite::AddTown 0x0077C800, with the miracle icons of the town), and
// makes it complete.
const uintptr_t kBuiltSite = 0x00465110;
const uint8_t kBuiltSiteBytes[8] = {0x8B, 0x01, 0x53, 0x56,
                                    0x57, 0xFF, 0x50, 0x1C};
// Town::AddMagicTypesHeld, thiscall (MAGIC_TYPE), ret 4: the town knows a
// miracle, or a power up of one. The setup of a land calls it for each
// miracle of a town, and so does a reward (Reward::InterfaceTap). It turns
// the magic type on for the player (GPlayer::SetMagicTypeEnabled) and gives
// the Village Centre the miracle icon (TownCentre::AddSpell) or the power up.
const uintptr_t kTownAddMagic = 0x0073D380;
const uint8_t kTownAddMagicBytes[9] = {0x55, 0x57, 0x8B, 0x7C, 0x24,
                                       0x0C, 0x83, 0xFF, 0x2A};
const int kMagicTypes = 0x2A;
// Town: the first Workshop, and the count of the Workshops.
const int kTownWorkshop = 0x74C;
const int kTownWorkshops = 0x750;
// Scaffold.
const uintptr_t kScaffoldTable = 0x008E552C;
const int kScaffoldSite = 0x68;      // its BuildingSite, or 0
// fn_006E8460, cdecl (const MapCoords*, GScaffoldInfo*, Town*, Workshop*,
// int, float), the caller cleans 0x18 bytes: a new scaffold. A reward makes
// its scaffold with it (Reward::InterfaceTap, 0x006E5F37), then sets its
// count to 1 (Scaffold::SetScaffoldValue) and gives it to the Workshop of
// the town (fn_00779C10).
const uintptr_t kScaffoldCreate = 0x006E8460;
const uint8_t kScaffoldCreateBytes[9] = {0x56, 0x57, 0x6A, 0x42, 0x68,
                                         0xE4, 0xBF, 0xC0, 0x00};
const uintptr_t kScaffoldInfo = 0x00D959D0;
const uintptr_t kScaffoldSetValue = 0x006E9D40;   // thiscall (int), ret 4
const uintptr_t kWorkshopTakes = 0x00779C10;      // thiscall (Scaffold*), ret 4
// fn_006E9D10, thiscall, no arguments: the count of scaffolds in one.
const uintptr_t kScaffoldValue = 0x006E9D10;
// fn_006EA950, thiscall (int), ret 4: with 0, the scaffold has a planned
// building with a place that the game accepts. Scaffold::ProcessInHand
// (0x006E8570) finds the place each game turn while the Hand carries the
// scaffold, and the game then shows the building there.
const uintptr_t kScaffoldPlanned = 0x006EA950;
// The infos of the buildings (GAbodeInfo::Find 0x00405B30 walks them): 147
// records of 0x1C8 bytes. +0x120 is the kind of the building (0x84 is a
// Workshop, the case of Abode::Create that calls Workshop::Create) and
// +0x158 the tribe, or -1 for each tribe.
const uintptr_t kAbodeInfos = 0x00C3C690;
const int kAbodeInfoSize = 0x1C8;
const int kAbodeInfoCount = 147;
const int kAbodeInfoKind = 0x120;
const int kAbodeInfoTribe = 0x158;
const int32_t kAbodeKindWorkshop = 0x84;
// GTribeInfo: the tribe type.
const int kTribeType = 0x10;
// Abode::Create, cdecl (const MapCoords*, const GAbodeInfo*, Town*, float
// angle, float scale, int food, int wood, float built, int, int), the caller
// cleans 0x28 bytes. The setup of a land calls it for each building of a
// town (0x00715750), and a town calls it with these values (0x0073A707).
const uintptr_t kAbodeCreate = 0x00402E20;
const uint8_t kAbodeCreateBytes[10] = {0x8B, 0x44, 0x24, 0x08, 0x8B,
                                       0x88, 0x20, 0x01, 0x00, 0x00};
// WorshipSite::AddTownSpells, thiscall (Town*), ret 4: the Worship Site gets
// a miracle icon for each miracle of the town.
const uintptr_t kSiteAddTownSpells = 0x0077C910;
// WorshipSpellIcon::ValidForRequestSpell, thiscall (GInterfaceStatus*, the
// power up, bool), ret 0xC: the test of the game for a miracle that a
// gesture asks for (fn_005CF040).
const uintptr_t kIconValidForRequest = 0x0077FBA0;
const int kTownNext = 0x75C;
const int kTownWorship = 0x5C0;      // the value that the town works to
const int kTownWorshipping = 0x5C4;  // the villagers that worship now
const int kTownAdultsA = 0x618;      // the two counts that the game adds for
const int kTownAdultsB = 0x61C;      // the villagers that can worship
const int kTownSite = 0x98C;         // the WorshipSite
const int kTownCentre = 0x9A4;       // the TownCentre
const int kCentreTotem = 0xCC;       // its TotemStatue
const int kMaxTowns = 8;
// Object.
const int kObjectPlace = 0x14;       // MapCoords
const int kObjectFlags = 0x24;       // bit 0x10: a locked select holds it
const int kObject3D = 0x40;          // the LH3DObject
const int k3DPlace = 0x38;           // its place, 3 floats
const int kGetHeight = 0x42C;        // a vtable slot, the value in st0
const float kMapScale = 6553.6f;
// TotemStatue.
const uintptr_t kTotemTable = 0x008E029C;
const int kTotemValue = 0x80;        // the value of the totem
const int kTotemDrag = 0x88;         // the value while the Hand moves it
const int kValidForLockedSelect = 0x6CC;  // a vtable slot (GInterfaceStatus*)
// WorshipSite.
const int kSiteIcons = 0xE0;         // the first WorshipSpellIcon
// The chants of a Worship Site (its step of a game turn ends at 0x0077B894):
// the pool stays from turn to turn. Each turn the site can give the pool and
// what its worshippers made in that turn. A miracle that goes back gives its
// chants to the pool (0x0077FD60).
const int kSitePool = 0xF0;          // the chants that the site keeps
const int kSiteTurn = 0xF8;          // the chants of this game turn
const int kSiteUsed = 0xFC;          // the chants that this turn used
const int kSiteNoLimit = 0x108;      // not 0: the site has 1,000,000 chants
// WorshipSpellIcon.
const uintptr_t kIconTable = 0x0099D878;
const int kIconInfo = 0x80;          // GSpellSeedInfo*
const int kIconNext = 0x110;
const int kIconCharging = 0x120;     // a byte
const int kIconLevel = 0x124;        // the power up that it charges for
const int kIconStatus = 0x128;       // the GInterfaceStatus that it charges for
const int kIconStore = 0x134;        // the chants in the icon
const int kMaxIcons = 16;
// WorshipSpellIcon::GetChantNeededForSpellSeed, thiscall, the value in st0:
// the chants that the icon still needs.
const uintptr_t kIconNeeded = 0x0077FE40;
// GSpellSeedInfo: 30 of 0x190 bytes at 0x00D9D678.
const uintptr_t kSeedInfoTable = 0x00D9D678;
const int kSeedInfoSize = 0x190;
const int kSeedTypes = 30;

typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);
typedef void*(__fastcall* VirtualPtrFn)(void* self, void* unused);
typedef int(__fastcall* VirtualIntFn)(void* self, void* unused);
typedef int(__fastcall* VirtualIntArgFn)(void* self, void* unused, void* arg);
typedef float(__fastcall* VirtualFloatFn)(void* self, void* unused);
typedef void(__fastcall* TotemUpdateFn)(void* self, void* unused, float seconds,
                                        void* matrix);
typedef int(__fastcall* IntArgFn)(void* self, void* unused, int arg);
typedef int(__fastcall* ValidForRequestFn)(void* self, void* unused,
                                           void* status, int level, int flag);
typedef int(__fastcall* ValidToApplyFn)(void* self, void* unused, void* status,
                                        void* object);
typedef void*(__cdecl* AbodeCreateFn)(const void* at, const void* info,
                                      void* town, float angle, float scale,
                                      int food, int wood, float built,
                                      int zero, int flag);
typedef void*(__cdecl* ScaffoldCreateFn)(const void* at, void* info,
                                         void* town, void* workshop, int zero,
                                         float size);
typedef void*(__fastcall* BuiltSiteFn)(void* heart, void* unused,
                                       const void* at, const void* info,
                                       void* citadel, void* tribe);

TotemUpdateFn o_TotemUpdate = nullptr;

// --- the keys ---------------------------------------------------------------

struct Settings {
  bool totem = true;       // totem_hand
  bool join = true;        // hold_target
  float join_cm = 6.0f;    // hold_target_cm: the reach of a thing
  bool test_keys = false;  // test_keys: Ctrl and numpad 4 change totem_hand,
                           // Ctrl and numpad 2 change hold_target
};
Settings g_settings;
bool g_ready = false;
bool g_hooked = false;
uint64_t g_frame = 0;

// The test keys of the desktop runs. village_test_site is a frame: at that
// frame the first town of the player gets its Worship Site, complete, from
// the function that the setup of a land uses. The start with no story has
// none: a script of the story permits it (SET_CAN_BUILD_WORSHIPSITE,
// 0x006FEC40). village_test_miracles is a list of magic types (MAGIC_TYPE,
// for example 22 Water and 23 its power up): before the Worship Site is
// made, that town knows each of them (Town::AddMagicTypesHeld).
// village_test_chants is "amount@frame"
// with spaces between: at that frame each Worship Site of the player has
// that many chants. village_test_census is a list of frames: at each one the
// log gets the towns, the totems and the miracle icons of the player.
// village_test_icon is the seed type of the icon that the place "icon" of
// the word "seek" gives (-1: each icon in its turn).
const int kMaxTests = 8;
struct TestChants {
  float amount;
  uint64_t frame;
};
TestChants g_test_chants[kMaxTests];
int g_test_chant_count = 0;
uint64_t g_test_census[kMaxTests];
int g_test_census_count = 0;
int g_test_icon = -1;
int g_test_icon_used = 0;
uint64_t g_test_site = 0;
int g_test_miracles[kMaxTests];
int g_test_miracle_count = 0;
// village_test_scaffolds is "count@frame": at that frame the game makes that
// many scaffolds on the land under the Hand, 12 units apart, as a reward
// makes one. The place "scaffold" of the word "seek" gives each in its turn.
int g_test_scaffold_count = 0;
uint64_t g_test_scaffold_frame = 0;
void* g_test_scaffolds[kMaxTests] = {nullptr};
int g_test_scaffolds_made = 0;
int g_test_scaffold_used = 0;
// village_test_workshop is a frame: at that frame the first town of the
// player gets a Workshop on the land under the Hand, complete, from the
// function that the setup of a land uses.
uint64_t g_test_workshop = 0;
void* g_test_workshop_made = nullptr;

bool IniPath(char path[MAX_PATH]) {
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return false;
  slash[1] = 0;
  strcat_s(path, MAX_PATH, "bwvr.ini");
  return true;
}

int IniInt(const char* key, int fallback) {
  char path[MAX_PATH];
  if (!IniPath(path)) return fallback;
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

void IniText(const char* key, char* out, DWORD size) {
  char path[MAX_PATH];
  out[0] = 0;
  if (!IniPath(path)) return;
  GetPrivateProfileStringA("bwvr", key, "", out, size, path);
}

// --- the log ----------------------------------------------------------------

const int kMaxLogs = 400;
int g_logs = 0;
uint64_t g_frames_totem = 0;      // frames in the Totem state
uint64_t g_frames_rows = 0;       // frames on which the mod gave the rows
uint64_t g_drags = 0;
uint64_t g_charges = 0;
uint64_t g_charge_ends = 0;
uint64_t g_join_frames = 0;       // frames with a target of a scaffold
uint64_t g_join_puts = 0;         // the mod put it in the pick record
uint64_t g_applies = 0;           // the game put an object on a thing

bool Log() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

// --- the facts of the game --------------------------------------------------

void** VTable(void* object) { return *static_cast<void***>(object); }

uintptr_t TableOf(const void* object) {
  return reinterpret_cast<uintptr_t>(*static_cast<void* const*>(object));
}

bool SameBytes(uintptr_t address, const uint8_t* bytes, size_t count) {
  const void* at = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(at, count) && memcmp(at, bytes, count) == 0;
}

uint8_t* Face() {
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return nullptr;
  return static_cast<uint8_t*>(
      reinterpret_cast<MyInterfaceFn>(kMyInterface)(*slot, nullptr));
}

uint8_t* Player(uint8_t* face) {
  return static_cast<uint8_t*>(reinterpret_cast<VirtualPtrFn>(
      VTable(face)[kFaceGetPlayer / 4])(face, nullptr));
}

// The towns of the player of this interface.
int Towns(uint8_t* face, uint8_t* out[kMaxTowns]) {
  int count = 0;
  uint8_t* player = face != nullptr ? Player(face) : nullptr;
  if (player == nullptr || IsBadReadPtr(player + kPlayerTowns, 4)) return 0;
  uint8_t* town = *reinterpret_cast<uint8_t* const*>(player + kPlayerTowns);
  while (town != nullptr && count < kMaxTowns &&
         !IsBadReadPtr(town, kTownCentre + 4)) {
    out[count++] = town;
    town = *reinterpret_cast<uint8_t* const*>(town + kTownNext);
  }
  return count;
}

uint8_t* TotemOf(const uint8_t* town) {
  uint8_t* centre = *reinterpret_cast<uint8_t* const*>(town + kTownCentre);
  if (centre == nullptr || IsBadReadPtr(centre, kCentreTotem + 4)) {
    return nullptr;
  }
  uint8_t* totem = *reinterpret_cast<uint8_t* const*>(centre + kCentreTotem);
  if (totem == nullptr || IsBadReadPtr(totem, 0xD8) ||
      TableOf(totem) != kTotemTable) {
    return nullptr;
  }
  return totem;
}

uint8_t* SiteOf(const uint8_t* town) {
  uint8_t* site = *reinterpret_cast<uint8_t* const*>(town + kTownSite);
  return site != nullptr && !IsBadReadPtr(site, 0x118) ? site : nullptr;
}

int Icons(const uint8_t* site, uint8_t* out[kMaxIcons]) {
  int count = 0;
  uint8_t* icon = *reinterpret_cast<uint8_t* const*>(site + kSiteIcons);
  while (icon != nullptr && count < kMaxIcons &&
         !IsBadReadPtr(icon, kIconStore + 4)) {
    if (TableOf(icon) == kIconTable) out[count++] = icon;
    icon = *reinterpret_cast<uint8_t* const*>(icon + kIconNext);
  }
  return count;
}

// The town of a totem, or null.
uint8_t* TownOfTotem(uint8_t* face, const uint8_t* totem) {
  uint8_t* towns[kMaxTowns];
  const int count = Towns(face, towns);
  for (int i = 0; i < count; ++i) {
    if (TotemOf(towns[i]) == totem) return towns[i];
  }
  return nullptr;
}

int AdultsOf(const uint8_t* town) {
  return *reinterpret_cast<const int32_t*>(town + kTownAdultsA) +
         *reinterpret_cast<const int32_t*>(town + kTownAdultsB);
}

// The chants that a Worship Site can give now (fn_0077CC10).
float SiteChants(const uint8_t* site) {
  if (*reinterpret_cast<const int32_t*>(site + kSiteNoLimit) != 0) {
    return 1000000.0f;
  }
  return *reinterpret_cast<const float*>(site + kSiteTurn) -
         *reinterpret_cast<const float*>(site + kSiteUsed);
}

int SeedTypeOf(const uint8_t* icon) {
  const uintptr_t info =
      reinterpret_cast<uintptr_t>(*reinterpret_cast<void* const*>(icon + kIconInfo));
  if (info < kSeedInfoTable ||
      info >= kSeedInfoTable + kSeedInfoSize * kSeedTypes) {
    return -1;
  }
  return static_cast<int>((info - kSeedInfoTable) / kSeedInfoSize);
}

float IconNeeds(uint8_t* icon) {
  return reinterpret_cast<VirtualFloatFn>(kIconNeeded)(icon, nullptr);
}

// The place of an object: the place of its LH3DObject, or its map point.
bool PlaceOf(const uint8_t* object, float out[3], bool* from_3d) {
  const uint8_t* mesh = *reinterpret_cast<uint8_t* const*>(object + kObject3D);
  if (mesh != nullptr && !IsBadReadPtr(mesh, k3DPlace + 12)) {
    const float* at = reinterpret_cast<const float*>(mesh + k3DPlace);
    for (int i = 0; i < 3; ++i) out[i] = at[i];
    if (from_3d != nullptr) *from_3d = true;
    return true;
  }
  const int32_t* map = reinterpret_cast<const int32_t*>(object + kObjectPlace);
  out[0] = map[0] / kMapScale;
  out[1] = 0.0f;
  out[2] = map[1] / kMapScale;
  if (from_3d != nullptr) *from_3d = false;
  return true;
}

float HeightOf(uint8_t* object) {
  return reinterpret_cast<VirtualFloatFn>(VTable(object)[kGetHeight / 4])(
      object, nullptr);
}

// --- the totem follows the real hand -----------------------------------------

struct Drag {
  bool on;
  void* totem;
  float value0;      // the value of the totem when the hand took it
  float y0;          // the height of the real hand then, in world units
  uint64_t frame;    // the last frame of the mod on which the game moved it
};
Drag g_drag = {};
// The last rows that the mod gave, and the value it wanted, for the log.
int g_drag_rows = 0;
float g_drag_wanted = 0.0f;
float g_drag_hand_y = 0.0f;

// Before HandStateTotem::Update: the rows of this frame. The update runs once
// a frame, and a second call in one frame gets no rows.
void TotemRowsNow(uint8_t* state) {
  if (!g_settings.totem || state == nullptr) return;
  uint8_t* hand = *reinterpret_cast<uint8_t* const*>(state + kStateHand);
  float point[3];
  if (hand == nullptr || !HandDirectRealPoint(point)) {
    g_drag.on = false;
    return;
  }
  uint8_t* face = Face();
  if (face == nullptr) return;
  uint8_t* totem = *reinterpret_cast<uint8_t* const*>(face + kFaceObject);
  // The game moves only the object of the pick record, and only while the
  // locked select holds it. With another object the mod changes nothing.
  if (totem == nullptr || IsBadReadPtr(totem, 0xD8) ||
      TableOf(totem) != kTotemTable ||
      (totem[kObjectFlags] & 0x10) == 0) {
    g_drag.on = false;
    return;
  }
  int32_t* row_now = reinterpret_cast<int32_t*>(hand + kHandRowNow);
  int32_t* row_last = reinterpret_cast<int32_t*>(hand + kHandRowLast);
  const uint64_t frame = ControllersFrameCount();
  if (g_drag.on && g_drag.frame == frame) {
    *row_last = *row_now;
    return;
  }
  const float value = *reinterpret_cast<const float*>(totem + kTotemDrag);
  if (!g_drag.on || g_drag.totem != totem || frame > g_drag.frame + 2) {
    g_drag.on = true;
    g_drag.totem = totem;
    g_drag.value0 = value;
    g_drag.y0 = point[1];
  }
  g_drag.frame = frame;
  const float wanted =
      village_math::TotemValue(g_drag.value0, g_drag.y0, point[1]);
  const int rows = village_math::TotemRows(
      value, wanted, HeightOf(totem),
      static_cast<float>(*reinterpret_cast<const uint16_t*>(kScreenHeight)));
  *row_last = *row_now + rows;
  g_drag_rows = rows;
  g_drag_wanted = wanted;
  g_drag_hand_y = point[1];
  ++g_frames_rows;
}

void __fastcall h_TotemUpdate(void* self, void* unused, float seconds,
                              void* matrix) {
  if (g_ready) TotemRowsNow(static_cast<uint8_t*>(self));
  o_TotemUpdate(self, unused, seconds, matrix);
}

// --- what the log said last ---------------------------------------------------

struct TotemSeen {
  void* totem;
  bool locked;
  float drag;
  float value;
  float town_value;
  int said_percent;
  uint64_t since;
};
TotemSeen g_totems[kMaxTowns] = {};

struct IconSeen {
  void* icon;
  bool charging;
  float store;
};
IconSeen g_icons[kMaxTowns * kMaxIcons] = {};
int g_icon_seen = 0;

// The pool of chants of each Worship Site, for the log of a large change: a
// miracle that the site makes, and a miracle that goes back.
struct PoolSeen {
  void* site;
  float pool;
};
PoolSeen g_pools[kMaxTowns] = {};
const float kPoolStep = 100.0f;

bool g_state_totem = false;
uint64_t g_state_totem_since = 0;

void LogKeys(const char* from) {
  ModLog("village      : %s: totem_hand=%d. %s, frame %llu", from,
         g_settings.totem ? 1 : 0,
         g_settings.totem
             ? "The pole of the totem follows the real hand one to one "
               "while the grip holds it"
             : "The game moves the totem with the screen rows of the pixel "
               "of the Hand's point",
         g_frame);
}

void LogHoldKey(const char* from) {
  ModLog("village      : %s: hold_target=%d. %s (hold_target_cm=%.0f), "
         "frame %llu", from, g_settings.join ? 1 : 0,
         g_settings.join
             ? "The thing at the Hand that takes the object in the Hand has "
               "the halo and is the object of the pick record, so the "
               "opening of the grip puts the object on it"
             : "The pick record of an object in the Hand is the object "
               "behind the pixel of the Hand's point",
         g_settings.join_cm, g_frame);
}

void Init() {
  if (g_ready) return;
  g_ready = true;
  g_settings.totem = IniInt("totem_hand", 1) != 0;
  // The keys scaffold_join and scaffold_join_cm are the old names of these
  // two. They count when the new keys are not there.
  g_settings.join =
      IniInt("hold_target", IniInt("scaffold_join", 1)) != 0;
  g_settings.join_cm = static_cast<float>(
      IniInt("hold_target_cm", IniInt("scaffold_join_cm", 6)));
  if (g_settings.join_cm < 1.0f) g_settings.join_cm = 1.0f;
  if (g_settings.join_cm > 50.0f) g_settings.join_cm = 50.0f;
  g_settings.test_keys = IniInt("test_keys", 0) != 0;
  char text[160];
  IniText("village_test_chants", text, sizeof(text));
  for (const char* p = text; *p != 0 && g_test_chant_count < kMaxTests;) {
    while (*p == ' ') ++p;
    int amount = 0, frame = 0, used = 0;
    if (sscanf_s(p, "%d@%d%n", &amount, &frame, &used) < 2 || used == 0) break;
    g_test_chants[g_test_chant_count].amount = static_cast<float>(amount);
    g_test_chants[g_test_chant_count].frame = static_cast<uint64_t>(frame);
    ++g_test_chant_count;
    p += used;
  }
  IniText("village_test_census", text, sizeof(text));
  for (const char* p = text; *p != 0 && g_test_census_count < kMaxTests;) {
    while (*p == ' ' || *p == ',') ++p;
    int frame = 0, used = 0;
    if (sscanf_s(p, "%d%n", &frame, &used) < 1 || used == 0) break;
    g_test_census[g_test_census_count++] = static_cast<uint64_t>(frame);
    p += used;
  }
  g_test_icon = IniInt("village_test_icon", -1);
  g_test_site = static_cast<uint64_t>(IniInt("village_test_site", 0));
  g_test_workshop = static_cast<uint64_t>(IniInt("village_test_workshop", 0));
  IniText("village_test_scaffolds", text, sizeof(text));
  {
    int count = 0, frame = 0;
    if (sscanf_s(text, "%d@%d", &count, &frame) == 2 && count > 0) {
      g_test_scaffold_count = count < kMaxTests ? count : kMaxTests;
      g_test_scaffold_frame = static_cast<uint64_t>(frame);
    }
  }
  IniText("village_test_miracles", text, sizeof(text));
  for (const char* p = text; *p != 0 && g_test_miracle_count < kMaxTests;) {
    while (*p == ' ' || *p == ',') ++p;
    int type = 0, used = 0;
    if (sscanf_s(p, "%d%n", &type, &used) < 1 || used == 0) break;
    if (type > 0 && type < kMagicTypes) {
      g_test_miracles[g_test_miracle_count++] = type;
    }
    p += used;
  }
  if (SameBytes(kTotemUpdate, kTotemUpdateBytes, sizeof(kTotemUpdateBytes))) {
    void* target = reinterpret_cast<void*>(kTotemUpdate);
    MH_STATUS status = MH_CreateHook(target, &h_TotemUpdate,
                                     reinterpret_cast<void**>(&o_TotemUpdate));
    if (status == MH_OK) status = MH_EnableHook(target);
    g_hooked = status == MH_OK;
    ModLog("hooks        : the Totem state of the Hand, "
           "HandStateTotem::Update at 0x%08X %s",
           static_cast<unsigned>(kTotemUpdate), g_hooked ? "ok" : "FAILED");
  } else {
    ModLog("village      : HandStateTotem::Update does not have the bytes of "
           "the disassembly, so the mod does not hook it and the game moves "
           "the totem with the screen rows of the pointer");
  }
  LogKeys("totem_hand");
  LogHoldKey("hold_target");
  if (g_settings.test_keys) {
    ModLog("keys         : Ctrl with numpad 4 changes totem_hand between 1 "
           "and 0, and Ctrl with numpad 2 changes hold_target (test_keys=1)");
  }
  if (g_test_chant_count > 0) {
    ModLog("village      : test key village_test_chants: %d times, the first "
           "%.0f chants at frame %llu", g_test_chant_count,
           g_test_chants[0].amount, g_test_chants[0].frame);
  }
}

// THE A/B KEY (test_keys=1): Ctrl with the 4 key of the number pad changes
// totem_hand. The pointing controller gives a long pulse for 1 and a short
// pulse for 0. Only while a window of the game is in front.
void PollKey() {
  static bool was = false;
  if (!g_settings.test_keys) return;
  const bool down = (GetAsyncKeyState(VK_NUMPAD4) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool pressed = down && !was;
  was = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_settings.totem = !g_settings.totem;
  g_drag.on = false;
  ControllersGesturePulse(0.5f, g_settings.totem ? 0.24f : 0.08f);
  LogKeys("Ctrl and numpad 4");
}

// THE A/B KEY of hold_target (test_keys=1): Ctrl with the 2 key of the number
// pad. A long pulse for 1 and a short pulse for 0.
void PollHoldKey() {
  static bool was = false;
  if (!g_settings.test_keys) return;
  const bool down = (GetAsyncKeyState(VK_NUMPAD2) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool pressed = down && !was;
  was = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_settings.join = !g_settings.join;
  ControllersGesturePulse(0.5f, g_settings.join ? 0.24f : 0.08f);
  LogHoldKey("Ctrl and numpad 2");
}

// --- the census of the test key ----------------------------------------------

// A scaffold of the test key village_test_scaffolds still lives.
bool ScaffoldLives(void* scaffold) {
  return scaffold != nullptr && !IsBadReadPtr(scaffold, 0x9C) &&
         TableOf(scaffold) == kScaffoldTable;
}

void Census(uint8_t* face) {
  uint8_t* towns[kMaxTowns];
  const int count = Towns(face, towns);
  ModLog("village      : census: the player has %d towns, frame %llu", count,
         g_frame);
  uint8_t* hand = *reinterpret_cast<uint8_t* const*>(face + kFaceHand);
  void* held = hand != nullptr
                   ? *reinterpret_cast<void* const*>(hand + kHandHeld)
                   : nullptr;
  if (held != nullptr && !IsBadReadPtr(held, 0x120) &&
      reinterpret_cast<uintptr_t>(VTable(held)[kValidToApply / 4]) ==
          kApplyVillager) {
    const uint8_t* villager = static_cast<const uint8_t*>(held);
    const int32_t* map =
        reinterpret_cast<const int32_t*>(villager + kObjectPlace);
    const float* hand_at =
        reinterpret_cast<const float*>(hand + kHandPosition);
    ModLog("village      : census: the Hand holds the villager %p with the "
           "job %d. His map point is %.1f %.1f, and the Hand of the game is "
           "at %.1f %.1f %.1f. The game %s give him a job from a thing "
           "(the tests of Villager::InHand)", held, villager[kVillagerJob],
           map[0] / kMapScale, map[1] / kMapScale, hand_at[0], hand_at[1],
           hand_at[2], VillageCanHaveJob(held) ? "can" : "cannot");
    {
      void** table = VTable(held);
      const auto slot = [&](int offset) {
        return reinterpret_cast<VirtualIntFn>(table[offset / 4])(held,
                                                                  nullptr);
      };
      ModLog("village      : census: the tests of the villager: bit 0x400 of "
             "+0x24 is %d, bit 0x80 of +0xE0 is %d, slot 0x2C says %d, slot "
             "0xAF8 %d, slot 0x4A0 %d, slot 0xAF4 %d. His player is %p, and "
             "the player of the Hand is %p",
             (*reinterpret_cast<const uint16_t*>(villager + kVillagerFlags) &
              0x400) != 0,
             (villager[kVillagerState] & 0x80) != 0, slot(0x2C), slot(0xAF8),
             slot(0x4A0), slot(0xAF4),
             reinterpret_cast<VirtualPtrFn>(table[kFaceGetPlayer / 4])(
                 held, nullptr),
             Player(face));
    }
  }
  if (held != nullptr && !IsBadReadPtr(held, 0xA0)) {
    const uint8_t* seed = static_cast<const uint8_t*>(
        reinterpret_cast<VirtualPtrFn>(VTable(held)[kIsSpellSeed / 4])(
            held, nullptr));
    if (seed != nullptr && !IsBadReadPtr(seed, 0xA0)) {
      ModLog("village      : census: the Hand holds the miracle %p: power up "
             "%d, %.0f chants in it, its icon is %p, ready %d, the field at "
             "+0x98 is %d (not 0: the game looks for no power up gesture)",
             seed, *reinterpret_cast<const int32_t*>(seed + kSeedPower),
             *reinterpret_cast<const float*>(seed + kSeedChants),
             *reinterpret_cast<void* const*>(seed + kSeedIcon),
             seed[kSeedReady],
             *reinterpret_cast<const int32_t*>(seed + kSeedCast));
    }
  }
  for (int t = 0; t < count; ++t) {
    uint8_t* town = towns[t];
    const int32_t* map = reinterpret_cast<const int32_t*>(town + kObjectPlace);
    uint8_t* totem = TotemOf(town);
    uint8_t* site = SiteOf(town);
    float at[3] = {0.0f, 0.0f, 0.0f};
    bool from_3d = false;
    if (totem != nullptr) PlaceOf(totem, at, &from_3d);
    ModLog("village      : census: town %p at %.1f %.1f. Its value of worship "
           "is %.2f, %d villagers can worship and %d worship now. The totem "
           "%p is at %.1f %.1f %.1f (%s), value %.2f, height %.2f units, "
           "flags 0x%02X", town, map[0] / kMapScale, map[1] / kMapScale,
           *reinterpret_cast<const float*>(town + kTownWorship), AdultsOf(town),
           *reinterpret_cast<const int32_t*>(town + kTownWorshipping), totem,
           at[0], at[1], at[2], from_3d ? "its mesh" : "its map point",
           totem != nullptr
               ? *reinterpret_cast<const float*>(totem + kTotemValue) : 0.0f,
           totem != nullptr ? HeightOf(totem) : 0.0f,
           totem != nullptr ? totem[kObjectFlags] : 0);
    ModLog("village      : census: the town has %d Workshops (the first is "
           "%p)", *reinterpret_cast<const int32_t*>(town + kTownWorkshops),
           *reinterpret_cast<void* const*>(town + kTownWorkshop));
    if (g_test_workshop_made != nullptr &&
        !IsBadReadPtr(g_test_workshop_made, 0x80)) {
      uint8_t* shop = static_cast<uint8_t*>(g_test_workshop_made);
      PlaceOf(shop, at, &from_3d);
      ModLog("village      : census: the Workshop %p of the test (%s) is at "
             "%.1f %.1f %.1f, flags 0x%02X, its building site is %p", shop,
             HandDirectClass(shop), at[0], at[1], at[2], shop[kObjectFlags],
             *reinterpret_cast<void* const*>(shop + 0x74));
    }
    for (int i = 0; i < g_test_scaffolds_made; ++i) {
      uint8_t* scaffold = static_cast<uint8_t*>(g_test_scaffolds[i]);
      if (!ScaffoldLives(scaffold)) {
        ModLog("village      : census: the scaffold %p of the test is gone",
               scaffold);
        continue;
      }
      PlaceOf(scaffold, at, &from_3d);
      ModLog("village      : census: the scaffold %p of the test is at %.1f "
             "%.1f %.1f (%s). It is a scaffold of %d. Its building site is "
             "%p, flags 0x%02X. The Hand %s it", scaffold, at[0], at[1],
             at[2], from_3d ? "its mesh" : "its map point",
             reinterpret_cast<VirtualIntFn>(kScaffoldValue)(scaffold, nullptr),
             *reinterpret_cast<void* const*>(scaffold + kScaffoldSite),
             scaffold[kObjectFlags], held == scaffold ? "holds" : "does not hold");
    }
    if (site == nullptr) {
      ModLog("village      : census: the town has no Worship Site");
      continue;
    }
    PlaceOf(site, at, &from_3d);
    uint8_t* icons[kMaxIcons];
    const int icon_count = Icons(site, icons);
    ModLog("village      : census: the Worship Site %p (%s) is at %.1f %.1f "
           "%.1f (%s). Chants: a pool of %.0f, %.0f for this game turn and "
           "%.0f used in it, %.0f to give now%s. %d miracle icons", site,
           HandDirectClass(site), at[0], at[1], at[2],
           from_3d ? "its mesh" : "its map point",
           *reinterpret_cast<const float*>(site + kSitePool),
           *reinterpret_cast<const float*>(site + kSiteTurn),
           *reinterpret_cast<const float*>(site + kSiteUsed), SiteChants(site),
           *reinterpret_cast<const int32_t*>(site + kSiteNoLimit) != 0
               ? " (the site has no limit)" : "",
           icon_count);
    for (int i = 0; i < icon_count; ++i) {
      uint8_t* icon = icons[i];
      PlaceOf(icon, at, &from_3d);
      const int32_t* spot =
          reinterpret_cast<const int32_t*>(icon + kObjectPlace);
      void* status = *reinterpret_cast<void* const*>(face + kFaceStatus);
      ModLog("village      : census: icon %p of %s at %.1f %.1f %.1f (%s, "
             "map point %.1f %.1f). It has %.0f chants and needs %.0f more. "
             "Charging %d, level %d, flags 0x%02X. The game says that the "
             "player %s ask for it now (ValidForRequestSpell)", icon,
             MiracleSeedName(SeedTypeOf(icon)), at[0], at[1], at[2],
             from_3d ? "its mesh" : "its map point", spot[0] / kMapScale,
             spot[1] / kMapScale,
             *reinterpret_cast<const float*>(icon + kIconStore),
             IconNeeds(icon), icon[kIconCharging],
             *reinterpret_cast<const int32_t*>(icon + kIconLevel),
             icon[kObjectFlags],
             reinterpret_cast<ValidForRequestFn>(kIconValidForRequest)(
                 icon, nullptr, status, -1, 1) != 0 ? "can" : "cannot");
    }
  }
}

// The test key village_test_site.
void TestSite(uint8_t* face) {
  uint8_t* towns[kMaxTowns];
  if (Towns(face, towns) == 0) {
    ModLog("village      : test key village_test_site: the player has no "
           "town, frame %llu", g_frame);
    return;
  }
  uint8_t* town = towns[0];
  uint8_t* player = Player(face);
  uint8_t* citadel =
      *reinterpret_cast<uint8_t* const*>(player + kPlayerCitadel);
  uint8_t* heart =
      citadel != nullptr && !IsBadReadPtr(citadel, 0x78)
          ? *reinterpret_cast<uint8_t* const*>(citadel + kCitadelHeart)
          : nullptr;
  // The miracles of the town first: the Worship Site takes its miracle icons
  // from the town when the town comes to it.
  if (g_test_miracle_count > 0 &&
      SameBytes(kTownAddMagic, kTownAddMagicBytes,
                sizeof(kTownAddMagicBytes))) {
    for (int i = 0; i < g_test_miracle_count; ++i) {
      const int added = reinterpret_cast<IntArgFn>(kTownAddMagic)(
          town, nullptr, g_test_miracles[i]);
      ModLog("village      : test key village_test_miracles: the town %p %s "
             "the magic type %d (Town::AddMagicTypesHeld), frame %llu", town,
             added != 0 ? "now knows" : "did not take", g_test_miracles[i],
             g_frame);
    }
    if (SiteOf(town) != nullptr) {
      reinterpret_cast<VirtualIntArgFn>(kSiteAddTownSpells)(SiteOf(town),
                                                             nullptr, town);
    }
  }
  if (SiteOf(town) != nullptr || heart == nullptr ||
      !SameBytes(kBuiltSite, kBuiltSiteBytes, sizeof(kBuiltSiteBytes))) {
    ModLog("village      : test key village_test_site: nothing to do (the "
           "town has the Worship Site %p, the heart of the Temple is %p), "
           "frame %llu", SiteOf(town), heart, g_frame);
    return;
  }
  void* tribe = reinterpret_cast<VirtualPtrFn>(kTownTribe)(town, nullptr);
  void* site = reinterpret_cast<BuiltSiteFn>(kBuiltSite)(
      heart, nullptr, nullptr, nullptr, citadel, tribe);
  ModLog("village      : test key village_test_site: the game made the "
         "Worship Site %p for the town %p (CitadelHeart::"
         "CreateBuiltWorshipSite). The town now has the Worship Site %p, "
         "frame %llu", site, town, SiteOf(town), g_frame);
}

// The test key village_test_scaffolds.
void TestScaffolds(uint8_t* face) {
  uint8_t* towns[kMaxTowns];
  float point[3];
  if (Towns(face, towns) == 0 || !HandDirectRealPoint(point)) {
    ModLog("village      : test key village_test_scaffolds: no town or no "
           "Hand of the controller, frame %llu", g_frame);
    return;
  }
  uint8_t* town = towns[0];
  void* workshop = *reinterpret_cast<void* const*>(town + kTownWorkshop);
  const int32_t workshops =
      *reinterpret_cast<const int32_t*>(town + kTownWorkshops);
  if (!SameBytes(kScaffoldCreate, kScaffoldCreateBytes,
                 sizeof(kScaffoldCreateBytes))) {
    ModLog("village      : test key village_test_scaffolds: the function "
           "that makes a scaffold does not have the bytes of the "
           "disassembly, so no scaffold is made, frame %llu", g_frame);
    return;
  }
  // A town with no Workshop: a reward gives it no scaffold (0x006E5F11). The
  // test makes the scaffold with no Workshop then, and no Workshop gets it.
  if (workshops <= 0) workshop = nullptr;
  for (int i = 0; i < g_test_scaffold_count; ++i) {
    struct {
      int32_t x, z;
      float over;
    } at = {static_cast<int32_t>((point[0] + 12.0f * i) * kMapScale),
            static_cast<int32_t>(point[2] * kMapScale), 0.0f};
    void* scaffold = reinterpret_cast<ScaffoldCreateFn>(kScaffoldCreate)(
        &at, reinterpret_cast<void*>(kScaffoldInfo), town, workshop, 0, 1.0f);
    if (scaffold == nullptr) continue;
    reinterpret_cast<IntArgFn>(kScaffoldSetValue)(scaffold, nullptr, 1);
    if (workshop != nullptr) {
      reinterpret_cast<VirtualIntArgFn>(kWorkshopTakes)(workshop, nullptr,
                                                         scaffold);
    }
    g_test_scaffolds[g_test_scaffolds_made++] = scaffold;
    ModLog("village      : test key village_test_scaffolds: the game made "
           "the scaffold %p at %.1f %.1f for the town %p (%d Workshops, the "
           "first is %p), frame %llu", scaffold, point[0] + 12.0f * i,
           point[2], town, workshops, workshop, g_frame);
  }
}

// The test key village_test_workshop.
void TestWorkshop(uint8_t* face) {
  uint8_t* towns[kMaxTowns];
  float point[3];
  if (Towns(face, towns) == 0 || !HandDirectRealPoint(point) ||
      !SameBytes(kAbodeCreate, kAbodeCreateBytes, sizeof(kAbodeCreateBytes))) {
    ModLog("village      : test key village_test_workshop: no town, no Hand "
           "of the controller, or Abode::Create does not have the bytes of "
           "the disassembly, frame %llu", g_frame);
    return;
  }
  uint8_t* town = towns[0];
  const uint8_t* tribe = static_cast<const uint8_t*>(
      reinterpret_cast<VirtualPtrFn>(kTownTribe)(town, nullptr));
  const int32_t tribe_type =
      tribe != nullptr && !IsBadReadPtr(tribe, kTribeType + 4)
          ? *reinterpret_cast<const int32_t*>(tribe + kTribeType)
          : -1;
  const uint8_t* info = nullptr;
  for (int i = 0; i < kAbodeInfoCount && info == nullptr; ++i) {
    const uint8_t* at = reinterpret_cast<const uint8_t*>(kAbodeInfos) +
                        i * kAbodeInfoSize;
    if (IsBadReadPtr(at, kAbodeInfoSize)) break;
    const int32_t of = *reinterpret_cast<const int32_t*>(at + kAbodeInfoTribe);
    if (*reinterpret_cast<const int32_t*>(at + kAbodeInfoKind) ==
            kAbodeKindWorkshop &&
        (of == tribe_type || of == -1)) {
      info = at;
    }
  }
  if (info == nullptr) {
    ModLog("village      : test key village_test_workshop: the game has no "
           "Workshop for the tribe %d of the town, frame %llu", tribe_type,
           g_frame);
    return;
  }
  struct {
    int32_t x, z;
    float over;
  } at = {static_cast<int32_t>(point[0] * kMapScale),
          static_cast<int32_t>(point[2] * kMapScale), 0.0f};
  g_test_workshop_made = reinterpret_cast<AbodeCreateFn>(kAbodeCreate)(
      &at, info, town, 0.0f, 1.0f, 0, 0, 1.0f, 0, 1);
  ModLog("village      : test key village_test_workshop: the game made the "
         "Workshop %p at %.1f %.1f for the town %p (Abode::Create with the "
         "info %p of the tribe %d). The town now has %d Workshops, frame %llu",
         g_test_workshop_made, point[0], point[2], town, info, tribe_type,
         *reinterpret_cast<const int32_t*>(town + kTownWorkshops), g_frame);
}

// True at the frame of a test key. The frame counts as a step of the script
// counts it (fake_controller_play_frame).
bool TestFrameIsNow(uint64_t key_frame) {
  return key_frame != 0 &&
         ControllersTestFrame(key_frame, g_frame) == key_frame;
}

void TestKeys(uint8_t* face) {
  if (TestFrameIsNow(g_test_site)) TestSite(face);
  if (TestFrameIsNow(g_test_workshop)) TestWorkshop(face);
  if (g_test_scaffold_count > 0 && TestFrameIsNow(g_test_scaffold_frame)) {
    TestScaffolds(face);
  }
  for (int i = 0; i < g_test_census_count; ++i) {
    if (TestFrameIsNow(g_test_census[i])) Census(face);
  }
  for (int i = 0; i < g_test_chant_count; ++i) {
    if (!TestFrameIsNow(g_test_chants[i].frame)) continue;
    uint8_t* towns[kMaxTowns];
    const int count = Towns(face, towns);
    int sites = 0;
    for (int t = 0; t < count; ++t) {
      uint8_t* site = SiteOf(towns[t]);
      if (site == nullptr) continue;
      const float used = *reinterpret_cast<const float*>(site + kSiteUsed);
      *reinterpret_cast<float*>(site + kSitePool) = g_test_chants[i].amount;
      *reinterpret_cast<float*>(site + kSiteTurn) =
          used + g_test_chants[i].amount;
      // The log of the pool starts again from this value.
      g_pools[t].site = nullptr;
      ++sites;
    }
    ModLog("village      : test key village_test_chants: %d Worship Sites of "
           "the player have a pool of %.0f chants now, frame %llu", sites,
           g_test_chants[i].amount, g_frame);
  }
}

// --- the log of each change --------------------------------------------------

void WatchTotems(uint8_t* face, uint8_t* hand) {
  uint8_t* towns[kMaxTowns];
  const int count = Towns(face, towns);
  const float units = HandDirectRoomUnits();
  for (int t = 0; t < count; ++t) {
    uint8_t* town = towns[t];
    uint8_t* totem = TotemOf(town);
    TotemSeen& seen = g_totems[t];
    if (totem == nullptr) {
      seen.totem = nullptr;
      continue;
    }
    const bool locked = (totem[kObjectFlags] & 0x10) != 0;
    const float drag = *reinterpret_cast<const float*>(totem + kTotemDrag);
    const float value = *reinterpret_cast<const float*>(totem + kTotemValue);
    const float town_value =
        *reinterpret_cast<const float*>(town + kTownWorship);
    const int adults = AdultsOf(town);
    if (seen.totem != totem) {
      seen = {totem, locked, drag, value, town_value,
              village_math::Percent(value), g_frame};
      continue;
    }
    if (locked && !seen.locked) {
      ++g_drags;
      seen.since = g_frame;
      seen.said_percent = village_math::Percent(drag);
      float point[3] = {0.0f, 0.0f, 0.0f};
      const bool real = HandDirectRealPoint(point);
      const float* at = reinterpret_cast<const float*>(hand + kHandPosition);
      const float dx = at[0] - point[0], dy = at[1] - point[1],
                  dz = at[2] - point[2];
      if (Log()) {
        ModLog("village      : the Hand holds the totem %p of the Village "
               "Centre: its value is %.2f (%d percent, %d of %d villagers). "
               "%s. Hand state %d, hand mode 0x%X. The Hand of the game is at "
               "%.1f %.1f %.1f and the point of the real hand at %.1f %.1f "
               "%.1f: %.1f cm of the room apart, frame %llu", totem, drag,
               village_math::Percent(drag),
               village_math::Worshippers(adults, drag), adults,
               !real ? "The mouse moves it"
               : g_settings.totem && g_hooked
                   ? "The pole follows the real hand one to one (totem_hand=1)"
                   : "The game moves it with the screen rows of the Hand's "
                     "point (totem_hand=0)",
               *reinterpret_cast<const int32_t*>(hand + kHandState),
               *reinterpret_cast<const int32_t*>(face + kFaceHandMode), at[0],
               at[1], at[2], point[0], point[1], point[2],
               real && units > 0.0f
                   ? sqrtf(dx * dx + dy * dy + dz * dz) / units * 100.0f
                   : 0.0f,
               g_frame);
      }
    } else if (locked) {
      const int percent = village_math::Percent(drag);
      if (abs(percent - seen.said_percent) >= 10 && Log()) {
        ModLog("village      : the totem is at %.2f (%d percent, %d of %d "
               "villagers). The real hand is %.1f cm of the room %s where it "
               "took the totem, the mod wanted %.2f and gave %d rows, frame "
               "%llu", drag, percent,
               village_math::Worshippers(adults, drag), adults,
               units > 0.0f
                   ? fabsf(g_drag_hand_y - g_drag.y0) / units * 100.0f : 0.0f,
               g_drag_hand_y >= g_drag.y0 ? "above" : "below", g_drag_wanted,
               g_drag_rows, g_frame);
        seen.said_percent = percent;
      }
    } else if (seen.locked) {
      if (Log()) {
        ModLog("village      : the Hand let the totem %p go after %llu frames "
               "at %.2f (%d percent, %d of %d villagers). The game sends the "
               "value in packet 0x29, frame %llu", totem, g_frame - seen.since,
               seen.drag, village_math::Percent(seen.drag),
               village_math::Worshippers(adults, seen.drag), adults, g_frame);
      }
      g_drag.on = false;
    }
    if (fabsf(town_value - seen.town_value) > 0.004f && Log()) {
      ModLog("village      : the town %p now works to a value of worship of "
             "%.2f (it was %.2f): %d of %d villagers, and %d worship now, "
             "frame %llu", town, town_value, seen.town_value,
             village_math::Worshippers(adults, town_value), adults,
             *reinterpret_cast<const int32_t*>(town + kTownWorshipping),
             g_frame);
    }
    seen.locked = locked;
    seen.drag = drag;
    seen.value = value;
    seen.town_value = town_value;
  }
}

void WatchIcons(uint8_t* face) {
  uint8_t* towns[kMaxTowns];
  const int count = Towns(face, towns);
  void* status = *reinterpret_cast<void* const*>(face + kFaceStatus);
  IconSeen now[kMaxTowns * kMaxIcons];
  int now_count = 0;
  uint8_t* done[kMaxTowns] = {nullptr};
  for (int t = 0; t < count; ++t) {
    uint8_t* site = SiteOf(towns[t]);
    bool again = false;
    for (int k = 0; k < t; ++k) again |= done[k] == site;
    done[t] = site;
    if (site == nullptr || again) continue;
    {
      const float pool = *reinterpret_cast<const float*>(site + kSitePool);
      PoolSeen& seen = g_pools[t];
      if (seen.site == site && fabsf(pool - seen.pool) >= kPoolStep && Log()) {
        ModLog("village      : the pool of chants of the Worship Site %p "
               "went from %.0f to %.0f: %s, frame %llu", site, seen.pool, pool,
               pool > seen.pool ? "chants came back to the site"
                                : "the site gave chants to a miracle",
               g_frame);
      }
      seen.site = site;
      seen.pool = pool;
    }
    uint8_t* icons[kMaxIcons];
    const int icon_count = Icons(site, icons);
    for (int i = 0; i < icon_count; ++i) {
      uint8_t* icon = icons[i];
      const bool charging = icon[kIconCharging] != 0;
      const float store = *reinterpret_cast<const float*>(icon + kIconStore);
      const IconSeen* before = nullptr;
      for (int k = 0; k < g_icon_seen; ++k) {
        if (g_icons[k].icon == icon) before = &g_icons[k];
      }
      now[now_count++] = {icon, charging, store};
      if (before == nullptr || before->charging == charging) continue;
      const bool mine =
          *reinterpret_cast<void* const*>(icon + kIconStatus) == status;
      if (charging) {
        ++g_charges;
        if (Log()) {
          ModLog("village      : the miracle icon %p of %s starts to charge "
                 "%s, power up %d. It has %.0f chants and needs %.0f more. "
                 "The Worship Site has %.0f chants to give and a pool of "
                 "%.0f, frame %llu", icon, MiracleSeedName(SeedTypeOf(icon)),
                 mine ? "for the player" : "for another player",
                 *reinterpret_cast<const int32_t*>(icon + kIconLevel), store,
                 IconNeeds(icon), SiteChants(site),
                 *reinterpret_cast<const float*>(site + kSitePool), g_frame);
        }
      } else {
        ++g_charge_ends;
        if (Log()) {
          ModLog("village      : the charge of the miracle icon %p of %s "
                 "ended. The icon has %.0f chants (%.0f before). The Worship "
                 "Site has %.0f chants to give and a pool of %.0f, frame %llu",
                 icon, MiracleSeedName(SeedTypeOf(icon)), store,
                 before->store, SiteChants(site),
                 *reinterpret_cast<const float*>(site + kSitePool), g_frame);
        }
      }
    }
  }
  memcpy(g_icons, now, sizeof(IconSeen) * now_count);
  g_icon_seen = now_count;
}

// The thing that takes the object in the Hand, from the last first eye pass.
struct Hold {
  void* held;
  void* target;
  void* table;
  float at[3];       // the point of its mesh, for the pick record
  float center[3];   // the middle of its body, for the halo
  float radius;
  int line;          // village_math::HoldLine
  bool acts;         // false: the influence stops the press
};
Hold g_join = {};
void* g_said_join = nullptr;
float g_join_units = 0.0f;
float g_join_point[3] = {0.0f, 0.0f, 0.0f};

uintptr_t ApplyRule(void* object) {
  return reinterpret_cast<uintptr_t>(VTable(object)[kValidToApply / 4]);
}

int HeldKindOf(void* held) {
  const uintptr_t rule = ApplyRule(held);
  if (rule == kApplyScaffold) return village_math::kHeldScaffold;
  if (rule == kApplyPot) {
    return reinterpret_cast<VirtualIntFn>(
               VTable(held)[kGetResourceType / 4])(held, nullptr) == 1
               ? village_math::kHeldWood
               : village_math::kHeldFood;
  }
  if (rule == kApplyTree || rule == kApplyDeadTree) {
    return village_math::kHeldTree;
  }
  if (rule == kApplyAnimal) return village_math::kHeldAnimal;
  if (rule == kApplyVillager) return village_math::kHeldVillager;
  if (rule == kApplyCrop) return village_math::kHeldCrop;
  return village_math::kHeldOther;
}

int OntoKindOf(void* target) {
  if (reinterpret_cast<VirtualIntFn>(VTable(target)[kIsCreature / 4])(
          target, nullptr) != 0) {
    return village_math::kOntoCreature;
  }
  const uintptr_t table = TableOf(target);
  if (table == kAltarTable) return village_math::kOntoAltar;
  if (table == kStoreTable) return village_math::kOntoStore;
  if (table == kWorkshopTable) return village_math::kOntoWorkshop;
  if (table == kWorshipSiteTable) return village_math::kOntoWorshipSite;
  if (table == kScaffoldTable) return village_math::kOntoScaffold;
  if (table == kTeleportTable) return village_math::kOntoTeleport;
  if (ApplyRule(target) == kApplyPot) return village_math::kOntoPile;
  return village_math::kOntoOther;
}

const wchar_t* HoldLineText(int line) {
  switch (line) {
    case village_math::kLineStoreWood: return L"Open to store the wood";
    case village_math::kLineStoreFood: return L"Open to store the food";
    case village_math::kLineWorkshopWood:
      return L"Open to give the wood to the Workshop";
    case village_math::kLineBuildWood:
      return L"Open to give the wood to the builders";
    case village_math::kLineWorshipFood:
      return L"Open to give the food to the worshippers";
    case village_math::kLinePile: return L"Open to add to the pile";
    case village_math::kLineTreeWood: return L"Open to give the tree as wood";
    case village_math::kLineAnimalFood: return L"Open to store it as food";
    case village_math::kLineSacrifice: return L"Open to sacrifice it here";
    case village_math::kLineJoin: return L"Open to join the scaffolds";
    case village_math::kLineScaffoldPut:
      return L"Open to put the scaffold here";
    case village_math::kLineTeleport:
      return L"Open to send the villager through";
    case village_math::kLineGive: return L"Open to give it to the Creature";
    default: return L"Open to put it here";
  }
}

// A VILLAGER IN THE HAND: the job that the game gives him from the place
// where the Hand has him. The log has each change.
void* g_job_villager = nullptr;
int g_job_said = -1;

void WatchVillager(uint8_t* hand) {
  uint8_t* held = *reinterpret_cast<uint8_t* const*>(hand + kHandHeld);
  const bool villager = held != nullptr && !IsBadReadPtr(held, 0x120) &&
                        ApplyRule(held) == kApplyVillager;
  if (!villager) {
    if (g_job_villager != nullptr && Log()) {
      uint8_t* was = static_cast<uint8_t*>(g_job_villager);
      const bool lives = !IsBadReadPtr(was, 0x120) &&
                         ApplyRule(was) == kApplyVillager;
      ModLog("village      : the villager %p left the Hand with the job %d "
             "(0 is no job), frame %llu", g_job_villager,
             lives ? was[kVillagerJob] : -1, g_frame);
    }
    g_job_villager = nullptr;
    g_job_said = -1;
    return;
  }
  const int job = held[kVillagerJob];
  if (held == g_job_villager && job == g_job_said) return;
  const int32_t* map = reinterpret_cast<const int32_t*>(held + kObjectPlace);
  if (Log()) {
    ModLog("village      : the villager %p in the Hand has the job %d now "
           "(it was %d, 0 is no job). The game gives the job from the "
           "objects near the villager, who is at %.1f %.1f, frame %llu", held,
           job, held == g_job_villager ? g_job_said : -1, map[0] / kMapScale,
           map[1] / kMapScale, g_frame);
  }
  g_job_villager = held;
  g_job_said = job;
}

// A SCAFFOLD IN THE HAND: whether the game has a place for its building now.
// The game shows the building there, and its hint for the grip is "Build".
// The log has each change.
void* g_plan_scaffold = nullptr;
int g_plan_said = -1;

void WatchScaffold(uint8_t* hand) {
  uint8_t* held = *reinterpret_cast<uint8_t* const*>(hand + kHandHeld);
  if (!ScaffoldLives(held)) {
    g_plan_scaffold = nullptr;
    g_plan_said = -1;
    return;
  }
  const int planned =
      reinterpret_cast<IntArgFn>(kScaffoldPlanned)(held, nullptr, 0) != 0 ? 1
                                                                          : 0;
  if (held == g_plan_scaffold && planned == g_plan_said) return;
  const int32_t* map = reinterpret_cast<const int32_t*>(held + kObjectPlace);
  if (Log()) {
    ModLog("village      : the game has %s for the scaffold %p in the Hand, "
           "which is over the land at %.1f %.1f, frame %llu",
           planned ? "a place for a building" : "no place for a building",
           held, map[0] / kMapScale, map[1] / kMapScale, g_frame);
  }
  g_plan_scaffold = held;
  g_plan_said = planned;
}

// The card: its texts stay until the next frame.
wchar_t g_card_text[kMiracleLines][48];

}  // namespace

// --- for the other files ----------------------------------------------------

void VillageInstall() { Init(); }

void VillageSelect() {
  g_join.target = nullptr;
  float point[3];
  if (!g_ready || !g_settings.join || !HandDirectRealPoint(point)) {
    g_said_join = nullptr;
    return;
  }
  uint8_t* face = Face();
  uint8_t* hand = face != nullptr
                      ? *reinterpret_cast<uint8_t* const*>(face + kFaceHand)
                      : nullptr;
  void* status = face != nullptr
                     ? *reinterpret_cast<void* const*>(face + kFaceStatus)
                     : nullptr;
  // Only an object that the Hand carries: not a miracle (miracle.cpp has its
  // own target), and not while the game takes from a bulk source, which must
  // stay in the record.
  if (hand == nullptr || status == nullptr ||
      *reinterpret_cast<const int32_t*>(hand + kHandState) != kStateHolding ||
      HandDirectTakeRuns()) {
    g_said_join = nullptr;
    return;
  }
  void* held = *reinterpret_cast<void* const*>(hand + kHandHeld);
  if (held == nullptr || IsBadReadPtr(held, 0x40) ||
      reinterpret_cast<VirtualPtrFn>(VTable(held)[kIsSpellSeed / 4])(
          held, nullptr) != nullptr) {
    g_said_join = nullptr;
    return;
  }
  const float units = HandDirectRoomUnits();
  const float reach = g_settings.join_cm * 0.01f * units;
  const int held_kind = HeldKindOf(held);
  float held_at[3];
  PlaceOf(static_cast<uint8_t*>(held), held_at, nullptr);
  float best = 1e30f;
  const int count = HandDirectConeCount();
  for (int i = 0; i < count; ++i) {
    void* object = nullptr;
    float at[3];
    if (!HandDirectConeAt(i, &object, at) || object == nullptr ||
        object == held || IsBadReadPtr(object, 0x40)) {
      continue;
    }
    void** table = VTable(object);
    // The tests of the game's press, in its order (FUN_005D1560).
    if (reinterpret_cast<VirtualIntFn>(table[kValidAsTarget / 4])(
            object, nullptr) == 0) {
      continue;
    }
    const bool gives = reinterpret_cast<ValidToApplyFn>(
                           table[kValidToGive / 4])(object, nullptr, status,
                                                    held) == 1;
    if (!gives &&
        reinterpret_cast<ValidToApplyFn>(VTable(held)[kValidToApply / 4])(
            held, nullptr, status, object) != 1) {
      continue;
    }
    const int onto = OntoKindOf(object);
    float gap_hand = 0.0f, gap_held = 0.0f, center[3], other[3];
    float radius = 0.0f, half = 0.0f, r2 = 0.0f, h2 = 0.0f;
    if (!HandDirectConeBody(i, point, &gap_hand, center, &radius, &half) ||
        !HandDirectConeBody(i, held_at, &gap_held, other, &r2, &h2)) {
      continue;
    }
    if (!village_math::HoldTakes(gap_hand, gap_held,
                                 village_math::HoldReach(onto, reach), best)) {
      continue;
    }
    best = gap_hand < gap_held ? gap_hand : gap_held;
    g_join.held = held;
    g_join.target = object;
    g_join.table = *static_cast<void**>(object);
    for (int k = 0; k < 3; ++k) {
      g_join.at[k] = at[k];
      g_join.center[k] = center[k];
    }
    g_join.radius = radius > half ? radius : half;
    g_join.line = village_math::HoldLineOf(held_kind, onto);
    // The press acts only in the influence, for a thing that needs it
    // (0x005D15D6).
    g_join.acts =
        *reinterpret_cast<const int32_t*>(face + kFaceInfluence) != 0 ||
        reinterpret_cast<VirtualIntFn>(table[kMustBeInInfluence / 4])(
            object, nullptr) == 0;
  }
  g_join_units = units;
  for (int k = 0; k < 3; ++k) g_join_point[k] = point[k];
  if (g_join.target != nullptr) ++g_join_frames;
  if (g_join.target != g_said_join) {
    if (Log()) {
      char name[48];
      strcpy_s(name, HandDirectClass(held));
      if (g_join.target != nullptr) {
        ModLog("village      : the %s %p in the Hand can go on the %s %p: "
               "its body is %.1f cm of the room away (a reach of %.0f cm, "
               "hold_target_cm). The card says \"%ls\"%s. The mod puts it in "
               "the pick record, so the opening of the grip acts on it, "
               "frame %llu", name, held, HandDirectClass(g_join.target),
               g_join.target, units > 0.0f ? best / units * 100.0f : 0.0f,
               g_settings.join_cm, HoldLineText(g_join.line),
               g_join.acts ? "" : ", but the land under the Hand is outside "
                                  "the influence",
               g_frame);
      } else {
        ModLog("village      : the %s %p in the Hand has no thing to go on "
               "now, frame %llu", name, held, g_frame);
      }
    }
    g_said_join = g_join.target;
  }
}

void VillageKeepRecord() {
  if (!g_ready || g_join.target == nullptr) return;
  uint8_t* face = Face();
  uint8_t* hand = face != nullptr
                      ? *reinterpret_cast<uint8_t* const*>(face + kFaceHand)
                      : nullptr;
  // The target is from the first eye pass: the Hand must still hold the
  // object, and the target must still be the same object.
  if (hand == nullptr ||
      *reinterpret_cast<void* const*>(hand + kHandHeld) != g_join.held ||
      IsBadReadPtr(g_join.target, sizeof(void*)) ||
      *static_cast<void**>(g_join.target) != g_join.table) {
    g_join.target = nullptr;
    return;
  }
  if (*reinterpret_cast<void* const*>(face + kFaceNextObject) ==
      g_join.target) {
    return;
  }
  if (HandDirectRecordTarget(g_join.target, g_join.at)) ++g_join_puts;
}

bool VillageCanHaveJob(void* villager) {
  if (villager == nullptr || IsBadReadPtr(villager, 0x120) ||
      ApplyRule(villager) != kApplyVillager) {
    return false;
  }
  const uint8_t* bytes = static_cast<const uint8_t*>(villager);
  void** table = VTable(villager);
  const auto slot = [&](int offset) {
    return reinterpret_cast<VirtualIntFn>(table[offset / 4])(villager,
                                                              nullptr);
  };
  return (*reinterpret_cast<const uint16_t*>(bytes + kVillagerFlags) &
          0x400) == 0 &&
         (bytes[kVillagerState] & 0x80) == 0 && slot(0x2C) != 0 &&
         slot(0xAF8) == 0 && slot(0x4A0) == 0 && slot(0xAF4) == 0;
}

void VillageApplied(void* held, void* object) {
  if (!g_ready || held == nullptr || object == nullptr) return;
  ++g_applies;
  if (!Log()) return;
  char name[48];
  strcpy_s(name, HandDirectClass(held));
  ModLog("village      : the game puts the %s %p in the Hand on the %s %p "
         "(packets 0x4D and 0x11)%s, frame %llu", name, held,
         HandDirectClass(object), object,
         object == g_join.target ? ", the thing with the halo"
                                 : ", the object of the game's own pick",
         ControllersFrameCount());
}

bool VillageHalo(FarDraw* out) {
  memset(out, 0, sizeof(*out));
  if (!g_ready || g_join.target == nullptr) return false;
  out->active = true;
  out->valid = g_join.acts;
  for (int i = 0; i < 3; ++i) {
    out->object[i] = g_join.center[i];
    out->hand[i] = g_join_point[i];
  }
  out->radius = g_join.radius;
  out->units_per_meter = g_join_units;
  out->target = g_join.target;
  return true;
}

bool VillageTakesObject() {
  return g_ready && g_join.target != nullptr && g_join.acts;
}

void VillageFrame() {
  Init();
  g_frame = ControllersFrameCount();
  PollKey();
  PollHoldKey();
  uint8_t* face = Face();
  if (face == nullptr) return;
  uint8_t* hand = *reinterpret_cast<uint8_t* const*>(face + kFaceHand);
  if (hand == nullptr) return;
  TestKeys(face);
  WatchTotems(face, hand);
  WatchIcons(face);
  WatchVillager(hand);
  WatchScaffold(hand);
  const bool totem_state =
      *reinterpret_cast<const int32_t*>(hand + kHandState) == kStateTotem;
  if (totem_state) ++g_frames_totem;
  if (totem_state != g_state_totem) {
    g_state_totem = totem_state;
    g_state_totem_since = g_frame;
    if (!totem_state) g_drag.on = false;
  }
  // WHERE THE HAND OF THE GAME IS in the Totem state: the game puts it on the
  // top of the pole, and the real hand is where the player holds the pole.
  // The mesh of the game's Hand is hidden, and the hands that the player
  // sees are at the real hands. One line, 20 frames after the state starts.
  if (totem_state && g_frame == g_state_totem_since + 20) {
    float point[3];
    const float units = HandDirectRoomUnits();
    if (HandDirectRealPoint(point) && units > 0.0f && Log()) {
      const float* at = reinterpret_cast<const float*>(hand + kHandPosition);
      const float dx = at[0] - point[0], dy = at[1] - point[1],
                  dz = at[2] - point[2];
      ModLog("village      : in the Totem state the Hand of the game is at "
             "%.1f %.1f %.1f (the top of the pole), and the point of the real "
             "hand is at %.1f %.1f %.1f: %.1f cm of the room apart, %.1f cm "
             "of them in height, frame %llu", at[0], at[1], at[2], point[0],
             point[1], point[2],
             sqrtf(dx * dx + dy * dy + dz * dz) / units * 100.0f,
             fabsf(dy) / units * 100.0f, g_frame);
    }
  }
}

int VillageHint(MiracleLine lines[kMiracleLines]) {
  if (!g_ready) return 0;
  float point[3];
  if (!HandDirectRealPoint(point)) return 0;
  uint8_t* face = Face();
  if (face == nullptr) return 0;
  uint8_t* hand = *reinterpret_cast<uint8_t* const*>(face + kFaceHand);
  uint8_t* status = *reinterpret_cast<uint8_t* const*>(face + kFaceStatus);
  if (hand == nullptr || status == nullptr) return 0;
  int count = 0;
  const auto add = [&](int word, const wchar_t* text) {
    if (count >= kMiracleLines) return;
    wcsncpy_s(g_card_text[count], text, _TRUNCATE);
    lines[count].word = word;
    lines[count].text = g_card_text[count];
    ++count;
  };
  const auto worship = [&](uint8_t* totem, float value) {
    uint8_t* town = TownOfTotem(face, totem);
    wchar_t text[48];
    if (town != nullptr) {
      const int adults = AdultsOf(town);
      swprintf_s(text, L"Worship: %d%%, %d of %d villagers",
                 village_math::Percent(value),
                 village_math::Worshippers(adults, value), adults);
    } else {
      swprintf_s(text, L"Worship: %d%%", village_math::Percent(value));
    }
    add(-1, text);
  };
  // AN OBJECT IN THE HAND AT A THING THAT TAKES IT.
  if (g_join.target != nullptr &&
      *reinterpret_cast<void* const*>(hand + kHandHeld) == g_join.held) {
    add(kWordGrab, HoldLineText(g_join.line));
    if (!g_join.acts) add(-1, L"This place is outside your influence");
    return count;
  }
  // A VILLAGER IN THE HAND AT A THING THAT GIVES A JOB. The game shows the
  // icon of the job on the villager, and the card says the job.
  {
    uint8_t* held = *reinterpret_cast<uint8_t* const*>(hand + kHandHeld);
    if (*reinterpret_cast<const int32_t*>(hand + kHandState) ==
            kStateHolding &&
        held != nullptr && !IsBadReadPtr(held, 0x120) &&
        ApplyRule(held) == kApplyVillager && held[kVillagerJob] != 0) {
      wchar_t text[48];
      swprintf_s(text, L"Open to make %ls",
                 village_math::JobName(held[kVillagerJob]));
      add(kWordGrab, text);
      return count;
    }
  }
  // THE HAND MOVES THE TOTEM.
  if (*reinterpret_cast<const int32_t*>(hand + kHandState) == kStateTotem) {
    uint8_t* totem = *reinterpret_cast<uint8_t* const*>(face + kFaceObject);
    if (totem == nullptr || IsBadReadPtr(totem, 0xD8) ||
        TableOf(totem) != kTotemTable) {
      return 0;
    }
    add(kWordGrab, L"Raise or lower, open to set");
    worship(totem, *reinterpret_cast<const float*>(totem + kTotemDrag));
    return count;
  }
  void* object = nullptr;
  float at[3];
  if (!HandDirectNearObject(&object, at) || object == nullptr ||
      IsBadReadPtr(object, 0x138)) {
    return 0;
  }
  uint8_t* bytes = static_cast<uint8_t*>(object);
  // THE TOTEM AT THE HAND'S POINT.
  if (TableOf(object) == kTotemTable) {
    if (reinterpret_cast<VirtualIntArgFn>(
            VTable(object)[kValidForLockedSelect / 4])(object, nullptr,
                                                        status) != 1) {
      return 0;
    }
    add(kWordGrab, L"Hold, then raise or lower");
    worship(bytes, *reinterpret_cast<const float*>(bytes + kTotemValue));
    return count;
  }
  // A MIRACLE ICON AT THE HAND'S POINT.
  if (TableOf(object) == kIconTable) {
    const bool charging = bytes[kIconCharging] != 0;
    const bool for_me =
        *reinterpret_cast<void* const*>(bytes + kIconStatus) == status;
    const float store = *reinterpret_cast<const float*>(bytes + kIconStore);
    const bool ready =
        reinterpret_cast<VirtualIntFn>(kHandReady)(status, nullptr) != 0;
    float site_chants = 0.0f;
    uint8_t* towns[kMaxTowns];
    const int town_count = Towns(face, towns);
    bool mine = false;
    for (int t = 0; t < town_count && !mine; ++t) {
      uint8_t* site = SiteOf(towns[t]);
      if (site == nullptr) continue;
      uint8_t* icons[kMaxIcons];
      const int icon_count = Icons(site, icons);
      for (int i = 0; i < icon_count; ++i) {
        if (icons[i] != bytes) continue;
        mine = true;
        site_chants = SiteChants(site);
      }
    }
    switch (village_math::IconStateOf(mine, charging, for_me, ready,
                                      site_chants, store)) {
      case village_math::kIconIdle:
        add(kWordGrab, L"Tap to take the miracle");
        break;
      case village_math::kIconCharging: {
        wchar_t text[48];
        swprintf_s(text, L"The miracle charges: %d%%",
                   village_math::ChargePercent(store, IconNeeds(bytes)));
        add(kWordGrab, L"Tap to stop");
        add(-1, text);
        break;
      }
      case village_math::kIconNoPower:
        add(-1, L"No prayer power. Villagers must worship");
        break;
      default:
        break;
    }
    return count;
  }
  return 0;
}

bool VillageTestPlace(int place, float out[3]) {
  Init();
  uint8_t* face = Face();
  if (face == nullptr) return false;
  uint8_t* towns[kMaxTowns];
  if (Towns(face, towns) == 0) return false;
  uint8_t* town = towns[0];
  uint8_t* totem = TotemOf(town);
  uint8_t* site = SiteOf(town);
  bool from_3d = false;
  switch (place) {
    case kVillageSite:
      return site != nullptr && PlaceOf(site, out, &from_3d);
    case kVillageCentre:
      return totem != nullptr && PlaceOf(totem, out, &from_3d);
    case kVillageTotem: {
      if (totem == nullptr || !PlaceOf(totem, out, &from_3d)) return false;
      // The middle of the pole.
      out[1] += 0.5f * HeightOf(totem);
      return true;
    }
    case kVillageScaffold: {
      // The next scaffold of the test that lives and is not in the Hand.
      uint8_t* hand = *reinterpret_cast<uint8_t* const*>(face + kFaceHand);
      void* held = hand != nullptr
                       ? *reinterpret_cast<void* const*>(hand + kHandHeld)
                       : nullptr;
      for (int i = 0; i < g_test_scaffolds_made; ++i) {
        void* scaffold = g_test_scaffolds[(g_test_scaffold_used + i) %
                                          g_test_scaffolds_made];
        if (!ScaffoldLives(scaffold) || scaffold == held) continue;
        g_test_scaffold_used =
            (g_test_scaffold_used + i + 1) % g_test_scaffolds_made;
        if (!PlaceOf(static_cast<uint8_t*>(scaffold), out, &from_3d)) {
          return false;
        }
        out[1] += 0.5f;
        return true;
      }
      return false;
    }
    case kVillageWorkshop: {
      uint8_t* shop = *reinterpret_cast<uint8_t* const*>(town + kTownWorkshop);
      if (shop == nullptr) shop = static_cast<uint8_t*>(g_test_workshop_made);
      return shop != nullptr && !IsBadReadPtr(shop, 0x44) &&
             PlaceOf(shop, out, &from_3d);
    }
    case kVillageStore: {
      uint8_t* store = *reinterpret_cast<uint8_t* const*>(town + kTownStore);
      return store != nullptr && !IsBadReadPtr(store, 0x44) &&
             PlaceOf(store, out, &from_3d);
    }
    case kVillageIcon: {
      if (site == nullptr) return false;
      uint8_t* icons[kMaxIcons];
      const int count = Icons(site, icons);
      if (count == 0) return false;
      uint8_t* icon = icons[g_test_icon_used % count];
      if (g_test_icon >= 0) {
        icon = nullptr;
        for (int i = 0; i < count; ++i) {
          if (SeedTypeOf(icons[i]) == g_test_icon) icon = icons[i];
        }
        if (icon == nullptr) return false;
      } else {
        ++g_test_icon_used;
      }
      return PlaceOf(icon, out, &from_3d);
    }
    default:
      return false;
  }
}

void VillageReport() {
  if (!g_ready) return;
  if (g_join_frames > 0 || g_applies > 0) {
    ModLog("village      : an object in the Hand had a thing to go on on "
           "%llu frames, and the mod put that thing in the pick record %llu "
           "times. The game put an object on a thing %llu times, this window "
           "(hold_target=%d)", g_join_frames, g_join_puts, g_applies,
           g_settings.join ? 1 : 0);
    g_join_frames = 0;
    g_join_puts = 0;
    g_applies = 0;
  }
  if (g_frames_totem == 0 && g_drags == 0 && g_charges == 0 &&
      g_charge_ends == 0) {
    return;
  }
  ModLog("village      : the Hand was in the Totem state on %llu frames, and "
         "the mod gave the rows of the real hand on %llu. %llu times the Hand "
         "took a totem. %llu miracle icons started to charge and %llu charges "
         "ended, this window (totem_hand=%d)", g_frames_totem, g_frames_rows,
         g_drags, g_charges, g_charge_ends, g_settings.totem ? 1 : 0);
  g_frames_totem = 0;
  g_frames_rows = 0;
  g_drags = 0;
  g_charges = 0;
  g_charge_ends = 0;
}
