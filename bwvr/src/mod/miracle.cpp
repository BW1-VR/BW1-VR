// A MIRACLE IN THE HAND. See docs/miracles.md.
//
// WHAT THE GAME DOES [V, runblack.exe 1.42].
// - A one-off seed (OneOffSpellSeed, a reward or a dispenser) is an object.
//   A press of the Action button shorter than 225 ms taps it (the step of
//   interface action 13, 0x005D5250): OneOffSpellSeed::InterfaceTap makes a
//   SpellSeed and puts it in the Hand. A longer press picks the seed up as an
//   object.
// - With a SpellSeed in the Hand, CHand::GetRequiredState gives state 8
//   (Grain), and the interface hand mode is 5 (fn_005D7F20). The Grain state
//   runs the update of the Holding state, so the Hand is at the pointer, and
//   it keeps the speed of the Hand for a throw (0x005B3310).
// - The Action button casts (FUN_005D1560). GSpellSeedInfo+0x110 says how:
//   0 pours while the button is held, 1 casts at the release (a throw, or a
//   circle on the land), 2 casts at the press, on the object of the pick
//   record or at the land point of the record.
// - A cast that the game refuses calls fn_005D18F0: a sound and packet 0x2B,
//   the yellow cross.
// - The shake (the scribble gesture) calls fn_005D4350: packet 0x4D and
//   packet 0x1D, and the game deletes the seed or gives its power back to
//   the Worship Site (SpellSeed::ThrowObjectFromHand).
//
// WHAT THE MOD DOES (miracle_hand=1).
// - The controller places the Hand in the Grain state too (hand_direct.cpp).
// - The target of a miracle for an object is the body at the Hand's point,
//   or the body in the aim. It has a halo, and it is the object of the pick
//   record when the game reads it. With no target the record has no object.
// - A shake of the real hand, and a short press of the second face button,
//   take the miracle out of the Hand with the game's own function.
// - The trigger makes a seed that the Hand carries active: the Hand lets the
//   seed go, and the mod sends the game's tap.
// - Each change goes to the log with the tag "miracle", and the card of the
//   hand says what each control does and why the game refused a cast.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"
#include "controller_hints.h"
#include "controllers.h"
#include "creature_command_math.h"
#include "far_grab.h"
#include "haptics.h"
#include "miracle.h"
#include "miracle_math.h"
#include "teleport.h"
#include "village.h"

extern void ModLog(const char* format, ...);

// controllers.cpp
bool ControllersFarRay(float origin[3], float direction[3],
                       float* units_per_meter, float eye[3]);
bool ControllerDirectMotion(float reach_room[3], float grip_room[3],
                            float room[9]);
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);
bool ControllersSecondButton();
// head_pose.cpp: game units in a real meter.
float HeadPoseScale();
// hand_direct.cpp
bool HandDirectHandPosition(float out[3], bool* by_controller);
int HandDirectConeCount();
bool HandDirectConeAt(int index, void** object, float position[3]);
float HandDirectRadius(void* object);
const char* HandDirectClass(void* object);
bool HandDirectCreatureBody(void* creature, float out[3]);
bool HandDirectRecordTarget(void* object, const float position[3]);
float HandDirectRoomUnits();
bool HandDirectRealPoint(float out[3]);

namespace {

using miracle_math::Seed;

// --- the game ---------------------------------------------------------------

const uintptr_t kGame = 0x00D0195C;
// GGame::MyInterface, thiscall, no arguments.
const uintptr_t kMyInterface = 0x00555850;
// GInterface.
const int kFaceFlags = 0x39;         // 2 a right press, 0x40 a right release
const int kFaceAction = 0x44;        // the interface action, 0 to 23
const int kFaceInfluence = 0x48;     // the influence at the land point +0x3F0
const int kFaceGesture = 0x1A4;      // a byte: the gesture of the next cast
const int kFaceStatus = 0x39C;       // the local GInterfaceStatus
const int kFaceHand = 0x3A0;         // the CHand
const int kFaceHandMode = 0x3AC;
const int kFaceNextLand = 0x3F0;     // the land point of the next pick record
const int kFaceNextObject = 0x400;   // the object of the next pick record
const int kFaceDropTurn = 0x444;     // the game turn of a cast that waits
const int kFaceGetPlayer = 0x1C;     // a vtable slot, thiscall, no arguments
// GGame: the players, 8 of 0xA60 bytes, and the Creature of each one.
const int kGamePlayers = 0x18;
const int kGamePlayerSize = 0xA60;
const int kPlayers = 8;
const int kPlayerCreature = 0xA4C;
// The vtables of the game are in its .rdata.
const uintptr_t kRdataStart = 0x008A9000;
const uintptr_t kRdataEnd = 0x009C6000;
// GInterfaceStatus.
const int kStatusHolding = 0x90;     // a byte: the count of objects in the Hand
// CHand.
const int kHandState = 0x4878;
const int kHandThrow = 0x48C8;       // the throw velocity, units per second
const int kHandHeld = 0x4904;
const int kHandFx = 0x4948;          // the effects of the Hand
const int32_t kStateGrain = 8;
// Vtable slots of Object.
const int kIsCreature = 0x34;
const int kIsSpellDispenser = 0x1F4;
const int kIsSpellSeed = 0x4C4;      // gives the SpellSeed, or 0
const int kIsReturnPoint = 0x670;    // IsSpellSeedReturnPoint: a Worship Site
const int kValidAsTarget = 0x6F0;    // ValidAsInterfaceTarget
const int kValidToApplyToObject = 0x71C;  // (GInterfaceStatus*, Object*)
// The vtable of OneOffSpellSeed, and its fields.
const uintptr_t kOneOffTable = 0x008F3774;
const int kOneOffType = 0x68;        // SPELL_SEED_TYPE
// SpellSeed.
const int kSeedInfo = 0x58;          // GSpellSeedInfo*
const int kSeedIcon = 0x5C;          // the icon of a Worship Site, or 0
const int kSeedSpell = 0x60;         // the Spell that runs, or 0
const int kSeedPower = 0x6C;         // POWER_UP_TYPE
const int kSeedReady = 0x90;         // a byte
// GSpellSeedInfo: 30 of 0x190 bytes at 0x00D9D678 (load_variables 0x0042C28E).
const uintptr_t kSeedInfoTable = 0x00D9D678;
const int kSeedInfoSize = 0x190;
const int kSeedTypes = 30;
const int kInfoGesture = 0x10C;
const int kInfoCast = 0x110;
const int kInfoKept = 0x114;
const int kInfoOnObject = 0x118;
// SpellSeed::GetMagicInfo, thiscall, no arguments. GMagicInfo+0x34 is the
// cast rule (CAST_RULE_TYPE).
const uintptr_t kSeedMagicInfo = 0x007290F0;
const int kMagicCastRule = 0x34;
// OneOffSpellSeed::Create, cdecl (const MapCoords*, SPELL_SEED_TYPE,
// POWER_UP_TYPE, float). The caller cleans 0x10 bytes (0x00722BD5).
const uintptr_t kCreateOneOff = 0x0072A2F0;
const uint8_t kCreateOneOffBytes[8] = {0x83, 0xEC, 0x18, 0x56,
                                       0x8B, 0x74, 0x24, 0x24};
// fn_005D4350, thiscall on GInterface, no arguments: the game's drop of a
// miracle after a shake (called at 0x005CF715 and 0x005D0113).
const uintptr_t kDropMiracle = 0x005D4350;
const uint8_t kDropMiracleBytes[9] = {0x83, 0xEC, 0x30, 0x8B, 0x81,
                                      0xA0, 0x03, 0x00, 0x00};
// fn_005CE420, thiscall on GInterface (bool draw), ret 4: it clears the ring
// of the gestures and starts the wait of 0.4 s.
const uintptr_t kClearGestures = 0x005CE420;
// fn_005D38A0, thiscall on GInterface (Object*), ret 4: the tap of the game.
// It sends packet 0x20 when the object can be tapped and the land point of
// the record is inside the influence.
const uintptr_t kTap = 0x005D38A0;
const uint8_t kTapBytes[9] = {0x56, 0x8B, 0xF1, 0x8B, 0x86,
                              0x9C, 0x03, 0x00, 0x00};
// fn_005D18F0 (Object* held, const MapCoords* at), ret 8: the game refuses a
// cast or a drop. A sound, and packet 0x2B with the land point.
const uintptr_t kRefuse = 0x005D18F0;
// FUN_005D3340, thiscall on GInterface, no arguments: the cast at the land
// point of the record. It sends packets 0x4D, 0x16 and 0x12.
const uintptr_t kCastAtLand = 0x005D3340;
// fn_005D30D0, thiscall on GInterface, no arguments: the cast on the object
// of the record. It sends packets 0x4D and 0x11.
const uintptr_t kCastOnObject = 0x005D30D0;
// MapCoords::InBounds and MapCoords::IsLand, thiscall, no arguments.
const uintptr_t kInBounds = 0x006042C0;
const uintptr_t kIsLand = 0x00603720;
// Influence::CalculatePlayerInfluence, cdecl, the value in st0.
const uintptr_t kPlayerInfluence = 0x005CD170;
// The held bits of the game's mouse: 1 the left button, 2 the right button.
const uintptr_t kHeldBits = 0x00E85304;
const float kMapScale = 6553.6f;

struct MapCoords {
  int32_t x, z;
  float over;
};

typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);
typedef int(__fastcall* VirtualIntFn)(void* self, void* unused);
typedef void*(__fastcall* VirtualPtrFn)(void* self, void* unused);
typedef void(__fastcall* VirtualVoidFn)(void* self, void* unused);
typedef int(__fastcall* ValidToApplyFn)(void* self, void* unused, void* status,
                                        void* object);
typedef void*(__cdecl* CreateOneOffFn)(const MapCoords* at, int32_t type,
                                       int32_t power, float size);
typedef int(__fastcall* FaceFn)(void* face, void* unused);
typedef void(__fastcall* ClearGesturesFn)(void* face, void* unused, int draw);
typedef int(__fastcall* TapFn)(void* face, void* unused, void* object);
typedef int(__fastcall* RefuseFn)(void* self, void* unused, void* held,
                                  const MapCoords* at);
typedef int(__fastcall* CoordsFn)(const MapCoords* at, void* unused);
typedef float(__cdecl* PlayerInfluenceFn)(const MapCoords* at, void* player,
                                          int a, int b, int c);

RefuseFn o_Refuse = nullptr;
FaceFn o_CastAtLand = nullptr;
FaceFn o_CastOnObject = nullptr;

const char* const kSeedName[kSeedTypes] = {
    "Storm",              "Forest",             "Fireball",
    "Food",               "Spiritual Shield",   "Physical Shield",
    "Lightning",          "Heal",               "Wood",
    "Water",              "Flock of birds",     "Flock of animals",
    "Creature Freeze",    "Creature Small",     "Creature Big",
    "Creature Weak",      "Creature Strong",    "Creature Fat",
    "Creature Thin",      "Creature Invisible", "Creature Compassion",
    "Creature Angry",     "Creature Hungry",    "Creature Frightened",
    "Creature Tired",     "Creature Ill",       "Creature Thirsty",
    "Creature Itchy",     "Teleport",           "Explosion"};

const char* SeedName(int type) {
  return type >= 0 && type < kSeedTypes ? kSeedName[type] : "a miracle";
}

const char* CastName(int cast) {
  return cast == miracle_math::kCastPour    ? "to pour (cast in the Hand)"
         : cast == miracle_math::kCastThrow ? "to throw (cast at the release)"
                                            : "to place (cast at the press)";
}

// --- the keys ---------------------------------------------------------------

struct Settings {
  bool on = true;            // miracle_hand
  float shake_mps = 0.6f;    // miracle_shake_mps: 0 turns the shake off
  bool button = true;        // miracle_cancel_button
  int tap_ms = 400;          // miracle_cancel_ms: the longest press that cancels
  bool seed_use = true;      // miracle_seed_use: the trigger uses a seed
  float cone_deg = 6.0f;     // miracle_cone_deg: a new target in the aim
  float keep_deg = 10.0f;    // miracle_keep_deg: the target stays to here
  float touch_cm = 3.0f;     // miracle_touch_cm
  float max_m = 4.0f;        // miracle_max_m: 0 is no limit
  bool test_keys = false;    // test_keys: Ctrl and numpad 8 change miracle_hand
};
Settings g_settings;
bool g_ready = false;
bool g_hooked = false;
uint64_t g_frame = 0;

// The test key miracle_test_seeds, for the desktop runs: "type@frame", with
// spaces between. At that frame of the mod the game makes a one-off seed of
// that type on the land under the Hand.
const int kMaxTestSeeds = 8;
struct TestSeed {
  int type;
  uint64_t frame;
};
TestSeed g_test_seeds[kMaxTestSeeds];
int g_test_seed_count = 0;
// The test key miracle_test_cancel: a list of frames. At each frame the mod
// asks for a cancel, as a shake does.
uint64_t g_test_cancels[kMaxTestSeeds];
int g_test_cancel_count = 0;

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

float IniFloat(const char* key, float fallback) {
  char path[MAX_PATH], text[32] = "";
  if (!IniPath(path)) return fallback;
  GetPrivateProfileStringA("bwvr", key, "", text, sizeof(text), path);
  if (text[0] == 0) return fallback;
  char* end = nullptr;
  const double value = strtod(text, &end);
  return end != text ? static_cast<float>(value) : fallback;
}

void IniText(const char* key, char* out, DWORD size) {
  char path[MAX_PATH];
  out[0] = 0;
  if (!IniPath(path)) return;
  GetPrivateProfileStringA("bwvr", key, "", out, size, path);
}

// --- the state --------------------------------------------------------------

struct Facts {
  uint8_t* game;
  uint8_t* face;
  uint8_t* status;
  uint8_t* hand;
  void* held;          // CHand+0x4904
  int32_t hand_state;
  int32_t hand_mode;
  int32_t action;
  bool one_off;        // the Hand holds a one-off seed as an object
  int one_off_type;
  uint8_t* seed;       // the Hand holds a SpellSeed: a miracle
  Seed info;
  int power;
  bool ready;
  void* spell;
  bool worship;        // the seed is from a Worship Site
};

// The facts of the last frame, and what the log said last.
Facts g_now = {};
bool g_have = false;
void* g_said_held = nullptr;
bool g_said_one_off = false;
void* g_said_seed = nullptr;
void* g_said_spell = nullptr;
int g_said_power = -1;
int g_said_type = -1;

// The target of a miracle for an object, from the last first eye pass.
void* g_target = nullptr;
void* g_target_table = nullptr;
float g_target_at[3] = {0.0f, 0.0f, 0.0f};
float g_target_radius = 0.0f;
bool g_target_at_hand = false;
// What the target is, for the card: a Creature, the place of a Worship Site
// that takes its miracle back (SpellSeed::CanCast 0x00729190), a spell
// dispenser, or an other object.
enum TargetKind { kTargetCreature, kTargetReturn, kTargetDispenser, kTargetOther };
int g_target_kind = kTargetOther;
// A Creature at the Hand or in the aim that the game does not let the
// miracle act on, and why.
void* g_blocked = nullptr;
float g_blocked_at[3] = {0.0f, 0.0f, 0.0f};
float g_blocked_radius = 0.0f;
int g_blocked_why = miracle_math::kRefuseNone;
void* g_said_target = nullptr;
void* g_said_blocked = nullptr;
float g_units = 0.0f;       // world units in a meter of the room
float g_hand_at[3] = {0.0f, 0.0f, 0.0f};

// The cancel that the player asked for, until the interface step runs it.
const char* g_cancel = nullptr;
double g_cancel_sent = -1e9;
const char* g_cancel_how = "";
// The shake of the real hand.
command_math::ShakeState g_shake = {};
// The second face button.
bool g_button_was = false;
double g_button_since = 0.0;
float g_button_from[3] = {0.0f, 0.0f, 0.0f};
float g_button_moved = 0.0f;

// The use of a seed that the Hand carries.
miracle_math::Use g_use = {miracle_math::kUseIdle, 0};
void* g_use_seed = nullptr;
bool g_tap_now = false;
bool g_mask_grip = false;
bool g_trigger_was = false;
const int kUseFrames = 120;

// The place of the game's Hand against the real hand, said a few times after
// a miracle comes into the Hand.
int g_place_said = 99;
const int kPlaceEvery = 90;
const int kPlaceLines = 4;

// The last refusal of the game, for the card.
int g_refusal = miracle_math::kRefuseNone;
double g_refusal_at = -1e9;
const double kRefusalSeconds = 4.0;
uint64_t g_refusal_frame = 0;
// The cast that the game sent, for the line when the miracle leaves.
double g_cast_at = -1e9;
// The casts of the pour that runs, and its first frame.
uint64_t g_pour_casts = 0;
uint64_t g_pour_from = 0;

// The log and the counts of a report window.
const int kMaxLogs = 600;
int g_logs = 0;
uint64_t g_frames_miracle = 0;
uint64_t g_frames_seed = 0;
uint64_t g_casts = 0;
uint64_t g_refusals = 0;
uint64_t g_cancels = 0;
uint64_t g_uses = 0;
uint64_t g_record_puts = 0;
uint64_t g_record_clears = 0;
uint64_t g_target_frames = 0;

bool Log() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

double NowSeconds() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return static_cast<double>(count.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

void** VTable(void* object) { return *static_cast<void***>(object); }

bool SameBytes(uintptr_t address, const uint8_t* bytes, size_t count) {
  const void* at = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(at, count) && memcmp(at, bytes, count) == 0;
}

// The facts of a SpellSeed.
void ReadSeed(uint8_t* seed, Facts* f) {
  f->seed = seed;
  f->info = {-1, miracle_math::kCastPlace, false, false, 0};
  const uint8_t* info = *reinterpret_cast<uint8_t* const*>(seed + kSeedInfo);
  const uintptr_t at = reinterpret_cast<uintptr_t>(info);
  if (info != nullptr && at >= kSeedInfoTable &&
      at < kSeedInfoTable + kSeedInfoSize * kSeedTypes &&
      !IsBadReadPtr(info, kSeedInfoSize)) {
    f->info.type = static_cast<int>((at - kSeedInfoTable) / kSeedInfoSize);
    f->info.cast = *reinterpret_cast<const int32_t*>(info + kInfoCast);
    f->info.kept = *reinterpret_cast<const int32_t*>(info + kInfoKept) != 0;
    f->info.on_object =
        *reinterpret_cast<const int32_t*>(info + kInfoOnObject) != 0;
    f->info.gesture = *reinterpret_cast<const int32_t*>(info + kInfoGesture);
  }
  f->power = *reinterpret_cast<const int32_t*>(seed + kSeedPower);
  f->ready = seed[kSeedReady] != 0;
  f->spell = *reinterpret_cast<void* const*>(seed + kSeedSpell);
  f->worship = *reinterpret_cast<void* const*>(seed + kSeedIcon) != nullptr;
}

// The facts of the game, read now. False with no game or no interface.
bool ReadFacts(Facts* f) {
  memset(f, 0, sizeof(*f));
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return false;
  f->game = static_cast<uint8_t*>(*slot);
  f->face = static_cast<uint8_t*>(
      reinterpret_cast<MyInterfaceFn>(kMyInterface)(f->game, nullptr));
  if (f->face == nullptr) return false;
  f->status = *reinterpret_cast<uint8_t* const*>(f->face + kFaceStatus);
  f->hand = *reinterpret_cast<uint8_t* const*>(f->face + kFaceHand);
  if (f->status == nullptr || f->hand == nullptr) return false;
  f->hand_state = *reinterpret_cast<const int32_t*>(f->hand + kHandState);
  f->hand_mode = *reinterpret_cast<const int32_t*>(f->face + kFaceHandMode);
  f->action = *reinterpret_cast<const int32_t*>(f->face + kFaceAction);
  f->held = *reinterpret_cast<void* const*>(f->hand + kHandHeld);
  if (f->held == nullptr || IsBadReadPtr(f->held, 0x80)) {
    f->held = nullptr;
    return true;
  }
  if (reinterpret_cast<uintptr_t>(*static_cast<void* const*>(f->held)) ==
      kOneOffTable) {
    f->one_off = true;
    f->one_off_type = *reinterpret_cast<const int32_t*>(
        static_cast<const uint8_t*>(f->held) + kOneOffType);
    return true;
  }
  void* seed = reinterpret_cast<VirtualPtrFn>(VTable(f->held)[kIsSpellSeed / 4])(
      f->held, nullptr);
  if (seed != nullptr && !IsBadReadPtr(seed, 0xA0)) {
    ReadSeed(static_cast<uint8_t*>(seed), f);
  }
  return true;
}

// The facts, only while the controller places the Hand. For the log, which
// says what the game does with the key off too.
bool Watching(Facts* f) {
  float at[3];
  bool by_controller = false;
  if (!HandDirectHandPosition(at, &by_controller) || !by_controller) {
    memset(f, 0, sizeof(*f));
    return false;
  }
  return ReadFacts(f);
}

// The facts, only while the controller places the Hand and the key is on.
bool Active(Facts* f) {
  if (!g_settings.on) {
    memset(f, 0, sizeof(*f));
    return false;
  }
  return Watching(f);
}

void* Player(uint8_t* face) {
  return reinterpret_cast<VirtualPtrFn>(VTable(face)[kFaceGetPlayer / 4])(
      face, nullptr);
}

bool GripDown() {
  return (*reinterpret_cast<const uint32_t*>(kHeldBits) & 2) != 0;
}

// --- the refusal ------------------------------------------------------------

// Why the game refuses a cast of this seed at this land point, by its own
// tests.
int WhyRefused(const Facts& f, const MapCoords* at, bool had_object) {
  if (f.seed == nullptr || at == nullptr || IsBadReadPtr(at, sizeof(*at))) {
    return miracle_math::kRefuseOther;
  }
  const bool on_map =
      reinterpret_cast<CoordsFn>(kInBounds)(at, nullptr) != 0;
  const bool land =
      on_map && reinterpret_cast<CoordsFn>(kIsLand)(at, nullptr) != 0;
  void* player = Player(f.face);
  const bool influence =
      on_map && player != nullptr &&
      reinterpret_cast<PlayerInfluenceFn>(kPlayerInfluence)(at, player, 0, 1,
                                                            1) > 0.0f;
  int rule = 0;
  const uint8_t* magic = static_cast<const uint8_t*>(
      reinterpret_cast<VirtualPtrFn>(kSeedMagicInfo)(f.seed, nullptr));
  if (magic != nullptr && !IsBadReadPtr(magic, kMagicCastRule + 4)) {
    rule = *reinterpret_cast<const int32_t*>(magic + kMagicCastRule);
  }
  return miracle_math::RefusalOf(f.ready, on_map, land, influence, rule,
                                 f.info, had_object, f.face[kFaceGesture]);
}

int __fastcall h_Refuse(void* self, void* unused, void* held,
                        const MapCoords* at) {
  Facts f;
  // The handler of the press runs again on each frame while the button is
  // down and no action started, so one press gives one line and one pulse.
  if (g_ready && held != nullptr && Watching(&f) && f.seed != nullptr &&
      f.held == held && g_refusal_frame + 1 < g_frame) {
    void* next = *reinterpret_cast<void* const*>(f.face + kFaceNextObject);
    // The game refused, so the object of the record is no object that it
    // casts this miracle on.
    const bool had_object =
        next != nullptr &&
        reinterpret_cast<ValidToApplyFn>(
            VTable(held)[kValidToApplyToObject / 4])(held, nullptr, f.status,
                                                      next) == 1;
    g_refusal = WhyRefused(f, at, had_object);
    g_refusal_at = NowSeconds();
    ++g_refusals;
    if (g_settings.on) HapticsPulse(haptics_math::kRefused);
    if (Log()) {
      ModLog("miracle      : the game refused the cast of %s at %.1f %.1f: "
             "%s. The pick record holds %p (%s), the influence at the land "
             "point of the record is %s, hand mode 0x%X, interface action %d, "
             "frame %llu", SeedName(f.info.type),
             at != nullptr ? at->x / kMapScale : 0.0f,
             at != nullptr ? at->z / kMapScale : 0.0f,
             miracle_math::RefusalWords(g_refusal), next,
             next != nullptr ? HandDirectClass(next) : "nothing",
             *reinterpret_cast<const int32_t*>(f.face + kFaceInfluence) != 0
                 ? "on" : "off",
             f.hand_mode, f.action, g_frame);
    }
  }
  if (g_ready && held != nullptr) g_refusal_frame = g_frame;
  return o_Refuse(self, unused, held, at);
}

// The game sent the packets of a cast when the game turn at +0x444 is set
// after the call.
void SaidCast(const Facts& f, bool on_object) {
  // The pulse of a cast comes from the game's own effect of the cast
  // (haptics.cpp), when the spell starts.
  ++g_casts;
  g_cast_at = NowSeconds();
  g_refusal = miracle_math::kRefuseNone;
  // A MIRACLE THAT GOES BACK: the press was on its icon, or on the totem of
  // its Worship Site. The game takes the miracle and gives its chants back to
  // the site. It is no cast and no pour.
  void* at_object = on_object
                        ? *reinterpret_cast<void* const*>(f.face + kFaceNextObject)
                        : nullptr;
  if (at_object != nullptr && at_object == g_target &&
      g_target_kind == kTargetReturn) {
    if (Log()) {
      ModLog("miracle      : the game takes %s back at the %s %p of its "
             "Worship Site (packets 0x4D and 0x11), and its chants go back "
             "to the site, frame %llu", SeedName(f.info.type),
             HandDirectClass(at_object), at_object, g_frame);
    }
    return;
  }
  // A POUR sends its cast again on each game turn while the button is held.
  // The log has the first one, and one line when the pour ends.
  if (f.info.cast == miracle_math::kCastPour) {
    if (++g_pour_casts > 1) return;
    g_pour_from = g_frame;
  }
  if (!Log()) return;
  const int32_t* land =
      reinterpret_cast<const int32_t*>(f.face + kFaceNextLand);
  const float* v = reinterpret_cast<const float*>(f.hand + kHandThrow);
  if (on_object) {
    void* object = *reinterpret_cast<void* const*>(f.face + kFaceNextObject);
    ModLog("miracle      : the game casts %s on the %s %p (packets 0x4D and "
           "0x11)%s, frame %llu", SeedName(f.info.type),
           object != nullptr ? HandDirectClass(object) : "object", object,
           object == g_target ? ", the target with the halo" : "", g_frame);
  } else {
    ModLog("miracle      : the game casts %s at the land point %.1f %.1f "
           "(packets 0x4D and 0x12). The Hand moves at %.1f %.1f %.1f units "
           "a second (%.2f m/s of the room), interface action %d, frame %llu",
           SeedName(f.info.type), land[0] / kMapScale, land[1] / kMapScale,
           v[0], v[1], v[2],
           g_units > 0.0f
               ? sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) / g_units
               : 0.0f,
           f.action, g_frame);
  }
}

int __fastcall h_CastAtLand(void* self, void* unused) {
  Facts f = {};
  const bool watch = g_ready && Watching(&f) && f.face == self &&
                     f.seed != nullptr;
  const int32_t before =
      watch ? *reinterpret_cast<const int32_t*>(f.face + kFaceDropTurn) : 0;
  const int result = o_CastAtLand(self, unused);
  if (watch && before == 0 &&
      *reinterpret_cast<const int32_t*>(f.face + kFaceDropTurn) != 0) {
    SaidCast(f, false);
  }
  return result;
}

int __fastcall h_CastOnObject(void* self, void* unused) {
  Facts f = {};
  const bool have = g_ready && Watching(&f) && f.face == self;
  const bool watch = have && f.seed != nullptr;
  // AN OBJECT THAT IS NO MIRACLE goes onto the object of the record with the
  // same function (a pot of wood onto a store, a scaffold onto a scaffold).
  // village.cpp has the log of it.
  const bool other = have && f.seed == nullptr && f.held != nullptr;
  void* object = other ? *reinterpret_cast<void* const*>(
                             f.face + kFaceNextObject)
                       : nullptr;
  const int32_t before =
      have ? *reinterpret_cast<const int32_t*>(f.face + kFaceDropTurn) : 0;
  const int result = o_CastOnObject(self, unused);
  const bool sent =
      have && before == 0 &&
      *reinterpret_cast<const int32_t*>(f.face + kFaceDropTurn) != 0;
  if (watch && sent) SaidCast(f, true);
  if (other && sent) VillageApplied(f.held, object);
  return result;
}

bool HookOne(uintptr_t address, void* replacement, void** original,
             const char* name) {
  void* target = reinterpret_cast<void*>(address);
  MH_STATUS status = MH_CreateHook(target, replacement, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : %s at 0x%08X %s", name,
         static_cast<unsigned>(address), status == MH_OK ? "ok" : "FAILED");
  return status == MH_OK;
}

void LogKeys(const char* from) {
  ModLog("miracle      : %s: miracle_hand=%d. %s, frame %llu", from,
         g_settings.on ? 1 : 0,
         g_settings.on
             ? "The Hand with a miracle is the real hand, the target of a "
               "miracle has a halo and is the object of the game's pick "
               "record, and a shake or the second face button takes the "
               "miracle out of the Hand"
             : "The game places the Hand with a miracle on the land under "
               "the pointer, and the mod gives no target and no cancel",
         g_frame);
}

void Init() {
  if (g_ready) return;
  g_ready = true;
  g_settings.on = IniInt("miracle_hand", 1) != 0;
  g_settings.shake_mps = IniFloat("miracle_shake_mps", 0.6f);
  if (g_settings.shake_mps < 0.0f) g_settings.shake_mps = 0.0f;
  if (g_settings.shake_mps > 0.0f && g_settings.shake_mps < 0.2f) {
    g_settings.shake_mps = 0.2f;
  }
  if (g_settings.shake_mps > 5.0f) g_settings.shake_mps = 5.0f;
  g_settings.button = IniInt("miracle_cancel_button", 1) != 0;
  g_settings.tap_ms = IniInt("miracle_cancel_ms", 400);
  if (g_settings.tap_ms < 50) g_settings.tap_ms = 50;
  if (g_settings.tap_ms > 2000) g_settings.tap_ms = 2000;
  g_settings.seed_use = IniInt("miracle_seed_use", 1) != 0;
  g_settings.cone_deg = IniFloat("miracle_cone_deg", 6.0f);
  if (!(g_settings.cone_deg >= 0.5f)) g_settings.cone_deg = 0.5f;
  if (g_settings.cone_deg > 30.0f) g_settings.cone_deg = 30.0f;
  g_settings.keep_deg =
      IniFloat("miracle_keep_deg", g_settings.cone_deg + 4.0f);
  if (!(g_settings.keep_deg >= g_settings.cone_deg)) {
    g_settings.keep_deg = g_settings.cone_deg;
  }
  if (g_settings.keep_deg > 45.0f) g_settings.keep_deg = 45.0f;
  g_settings.touch_cm = IniFloat("miracle_touch_cm", 3.0f);
  if (!(g_settings.touch_cm >= 0.0f)) g_settings.touch_cm = 0.0f;
  if (g_settings.touch_cm > 50.0f) g_settings.touch_cm = 50.0f;
  g_settings.max_m = IniFloat("miracle_max_m", 4.0f);
  if (!(g_settings.max_m > 0.0f)) g_settings.max_m = 0.0f;
  if (g_settings.max_m > 100.0f) g_settings.max_m = 100.0f;
  g_settings.test_keys = IniInt("test_keys", 0) != 0;
  command_math::ShakeReset(&g_shake);
  // The test keys of the desktop runs.
  char text[160];
  IniText("miracle_test_seeds", text, sizeof(text));
  for (const char* p = text; *p != 0 && g_test_seed_count < kMaxTestSeeds;) {
    while (*p == ' ') ++p;
    int type = 0, frame = 0, used = 0;
    if (sscanf_s(p, "%d@%d%n", &type, &frame, &used) < 2 || used == 0) break;
    g_test_seeds[g_test_seed_count].type = type;
    g_test_seeds[g_test_seed_count].frame = static_cast<uint64_t>(frame);
    ++g_test_seed_count;
    p += used;
  }
  IniText("miracle_test_cancel", text, sizeof(text));
  for (const char* p = text; *p != 0 && g_test_cancel_count < kMaxTestSeeds;) {
    while (*p == ' ' || *p == ',') ++p;
    int frame = 0, used = 0;
    if (sscanf_s(p, "%d%n", &frame, &used) < 1 || used == 0) break;
    g_test_cancels[g_test_cancel_count++] = static_cast<uint64_t>(frame);
    p += used;
  }
  if (g_test_seed_count > 0) {
    ModLog("miracle      : test key miracle_test_seeds: %d one-off seeds, the "
           "first of type %d (%s) at frame %llu", g_test_seed_count,
           g_test_seeds[0].type, SeedName(g_test_seeds[0].type),
           g_test_seeds[0].frame);
  }
  // The hooks only read and log, so they are in with the key off too.
  g_hooked = HookOne(kRefuse, &h_Refuse, reinterpret_cast<void**>(&o_Refuse),
                     "the refusal of a cast fn_005D18F0");
  HookOne(kCastAtLand, &h_CastAtLand, reinterpret_cast<void**>(&o_CastAtLand),
          "the cast at the land point FUN_005D3340");
  HookOne(kCastOnObject, &h_CastOnObject,
          reinterpret_cast<void**>(&o_CastOnObject),
          "the cast on an object fn_005D30D0");
  LogKeys("miracle_hand");
  ModLog("miracle      : miracle_shake_mps=%.2f (%s). miracle_cancel_button="
         "%d: a press of the second face button of the pointing hand, shorter "
         "than %d ms (miracle_cancel_ms), takes the miracle out of the Hand. "
         "miracle_seed_use=%d: the trigger makes a seed that the Hand carries "
         "active. A target is %.1f cm of the room from the Hand's point or "
         "less (miracle_touch_cm), or within %.1f degrees of the aim "
         "(miracle_cone_deg) and %.1f m of the room (miracle_max_m, 0 is no "
         "limit). A target stays to %.1f degrees (miracle_keep_deg)",
         g_settings.shake_mps,
         g_settings.shake_mps > 0.0f
             ? "4 strokes of 6 cm at this mean speed take the miracle out of "
               "the Hand"
             : "the shake is off",
         g_settings.button ? 1 : 0, g_settings.tap_ms,
         g_settings.seed_use ? 1 : 0, g_settings.touch_cm,
         g_settings.cone_deg, g_settings.max_m, g_settings.keep_deg);
  if (g_settings.test_keys) {
    ModLog("keys         : Ctrl with numpad 8 changes miracle_hand between 1 "
           "and 0 (test_keys=1)");
  }
}

}  // namespace

// The switch of the debug page (debug_menu.h).
void MiracleHandChange(const char* by) {
  Init();
  g_settings.on = !g_settings.on;
  ControllersGesturePulse(0.5f, g_settings.on ? 0.24f : 0.08f);
  LogKeys(by);
}

int MiracleHandValue() {
  Init();
  return g_settings.on ? 1 : 0;
}

namespace {

// THE A/B KEY (test_keys=1): Ctrl with the 8 key of the number pad changes
// miracle_hand. The pointing controller gives a long pulse for 1 and a short
// pulse for 0. Only while a window of the game is in front.
void PollKey() {
  static bool was = false;
  if (!g_settings.test_keys) return;
  const bool down = (GetAsyncKeyState(VK_NUMPAD8) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool pressed = down && !was;
  was = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  MiracleHandChange("Ctrl and numpad 8");
}

// --- the test seeds ---------------------------------------------------------

void TestSeeds() {
  for (int i = 0; i < g_test_seed_count; ++i) {
    // The frame of a seed counts as a step of the script counts it
    // (fake_controller_play_frame).
    if (ControllersTestFrame(g_test_seeds[i].frame, g_frame) !=
        g_test_seeds[i].frame) {
      continue;
    }
    float at[3];
    bool by_controller = false;
    if (!HandDirectHandPosition(at, &by_controller)) {
      ModLog("miracle      : test seed: there is no Hand at frame %llu",
             g_frame);
      continue;
    }
    if (!SameBytes(kCreateOneOff, kCreateOneOffBytes,
                   sizeof(kCreateOneOffBytes))) {
      ModLog("miracle      : test seed: OneOffSpellSeed::Create does not "
             "have the bytes of the disassembly, so no seed is made");
      continue;
    }
    const MapCoords coords = {static_cast<int32_t>(at[0] * kMapScale),
                              static_cast<int32_t>(at[2] * kMapScale), 0.0f};
    void* seed = reinterpret_cast<CreateOneOffFn>(kCreateOneOff)(
        &coords, g_test_seeds[i].type, 0, 1.0f);
    ModLog("miracle      : test seed: the game made the one-off seed %p of "
           "type %d (%s) on the land under the Hand, at %.1f %.1f, frame %llu",
           seed, g_test_seeds[i].type, SeedName(g_test_seeds[i].type), at[0],
           at[2], g_frame);
  }
}

// --- the cancel -------------------------------------------------------------

void AskCancel(const char* how) {
  if (g_cancel == nullptr) g_cancel = how;
}

// The game's own drop of a miracle, as its shake does it (0x005CF6FA to
// 0x005CF715): the effect of the Hand ends, the ring of the gestures clears,
// and fn_005D4350 sends packets 0x4D and 0x1D.
void RunCancel(const Facts& f, const char* how) {
  if (!SameBytes(kDropMiracle, kDropMiracleBytes, sizeof(kDropMiracleBytes))) {
    if (Log()) {
      ModLog("miracle      : %s asked for a cancel, but the drop function of "
             "the game does not have the bytes of the disassembly, so the "
             "mod does nothing, frame %llu", how, g_frame);
    }
    return;
  }
  void* fx = *reinterpret_cast<void* const*>(f.hand + kHandFx);
  if (fx != nullptr && !IsBadReadPtr(fx, 4)) {
    reinterpret_cast<VirtualVoidFn>(VTable(fx)[1])(fx, nullptr);
  }
  reinterpret_cast<ClearGesturesFn>(kClearGestures)(f.face, nullptr, 0);
  f.face[kFaceGesture] = 0;
  reinterpret_cast<FaceFn>(kDropMiracle)(f.face, nullptr);
  g_cancel_sent = NowSeconds();
  g_cancel_how = how;
  g_refusal = miracle_math::kRefuseNone;
  ++g_cancels;
  HapticsPulse(haptics_math::kLetGo);
  if (Log()) {
    ModLog("miracle      : %s: the mod takes %s out of the Hand with the "
           "game's own drop (fn_005D4350, packets 0x4D and 0x1D). Hand state "
           "%d, hand mode 0x%X, interface action %d, frame %llu", how,
           SeedName(f.info.type), f.hand_state, f.hand_mode, f.action,
           g_frame);
  }
}

// Once a frame: the shake of the real hand, the second face button, and the
// test key.
void CancelFrame(const Facts& f, bool have) {
  const bool miracle = have && f.seed != nullptr;
  // The shake.
  if (g_settings.shake_mps > 0.0f) {
    float reach[3], grip[3], room[9];
    const float life = HeadPoseScale();
    if (!have || !(life > 0.1f) ||
        !ControllerDirectMotion(reach, grip, room)) {
      command_math::ShakeReset(&g_shake);
    } else {
      const float p[3] = {grip[0] / life, grip[1] / life, grip[2] / life};
      command_math::ShakeResult result = {};
      if (command_math::ShakeStep(&g_shake, g_settings.shake_mps, p,
                                  NowSeconds(), &result) &&
          miracle) {
        if (Log()) {
          ModLog("miracle      : a shake of the hand: %d strokes in %.0f ms, "
                 "the slowest at %.2f m/s, the last one %.0f cm long. A "
                 "stroke needs %.2f m/s (miracle_shake_mps), frame %llu",
                 result.strokes, result.seconds * 1000.0f, result.slowest,
                 result.length * 100.0f, g_settings.shake_mps, g_frame);
        }
        AskCancel("a shake of the hand");
      }
    }
  }
  // The second face button: a short press with a still hand.
  {
    const bool down = have && g_settings.button && ControllersSecondButton();
    float reach[3], grip[3], room[9];
    const float life = HeadPoseScale();
    const bool motion =
        life > 0.1f && ControllerDirectMotion(reach, grip, room);
    const double now = NowSeconds();
    if (down && !g_button_was) {
      g_button_since = now;
      g_button_moved = 0.0f;
      for (int i = 0; i < 3; ++i) g_button_from[i] = motion ? grip[i] : 0.0f;
    } else if (down && motion) {
      const float dx = grip[0] - g_button_from[0],
                  dy = grip[1] - g_button_from[1],
                  dz = grip[2] - g_button_from[2];
      const float moved = sqrtf(dx * dx + dy * dy + dz * dz) / life;
      if (moved > g_button_moved) g_button_moved = moved;
    } else if (!down && g_button_was && miracle && !TeleportAiming()) {
      const float held = static_cast<float>(now - g_button_since);
      if (miracle_math::CancelTap(held, g_button_moved,
                                  g_settings.tap_ms * 0.001f, 0.05f)) {
        AskCancel("a short press of the second face button");
      } else if (Log()) {
        ModLog("miracle      : the second face button was down for %.0f ms "
               "and the hand moved %.1f cm, so it is a draw of a gesture and "
               "no cancel (a cancel is at most %d ms and 5 cm), frame %llu",
               held * 1000.0f, g_button_moved * 100.0f, g_settings.tap_ms,
               g_frame);
      }
    }
    g_button_was = down;
  }
  for (int i = 0; i < g_test_cancel_count; ++i) {
    if (ControllersTestFrame(g_test_cancels[i], g_frame) ==
            g_test_cancels[i] &&
        miracle) {
      AskCancel("the test key miracle_test_cancel");
    }
  }
}

// --- the log of each change -------------------------------------------------

void LogChanges(const Facts& f, bool have) {
  void* held = have ? f.held : nullptr;
  const bool one_off = have && f.one_off;
  void* seed = have ? f.seed : nullptr;
  const double now = NowSeconds();
  if (seed != g_said_seed) {
    if (seed != nullptr) {
      g_refusal = miracle_math::kRefuseNone;
      if (Log()) {
        ModLog("miracle      : %s is active in the Hand: the SpellSeed %p, "
               "power up %d, %s. It is a miracle %s. The game %s it on an "
               "object, the seed %s in the Hand while the spell runs, and "
               "the cast needs %s. Hand state %d, hand mode 0x%X, frame %llu",
               SeedName(f.info.type), seed, f.power,
               f.worship ? "from a Worship Site" : "from a one-off seed",
               CastName(f.info.cast),
               f.info.on_object ? "can cast" : "does not cast",
               f.info.kept ? "stays" : "does not stay",
               f.info.gesture != 0 ? "a circle on the land" : "no gesture",
               f.hand_state, f.hand_mode, g_frame);
      }
      g_place_said = 0;
    } else if (g_said_seed != nullptr && Log()) {
      const char* how =
          now - g_cancel_sent < 1.5   ? g_cancel_how
          : now - g_cast_at < 3.0     ? "after its cast"
          : g_said_spell != nullptr   ? "when its spell ended"
                                      : "and the mod did not ask for it";
      ModLog("miracle      : %s left the Hand (%s). The Hand is %s, Hand "
             "state %d, hand mode 0x%X, frame %llu", SeedName(g_said_type),
             how, held == nullptr ? "free" : "not empty",
             have ? f.hand_state : -1, have ? f.hand_mode : -1, g_frame);
    }
    g_said_seed = seed;
    g_said_spell = nullptr;
    g_said_power = seed != nullptr ? f.power : -1;
    g_said_type = seed != nullptr ? f.info.type : -1;
  } else if (seed != nullptr) {
    if (f.spell != g_said_spell) {
      if (f.spell != nullptr && Log()) {
        ModLog("miracle      : the spell %p of %s runs, and the seed stays "
               "in the Hand, frame %llu", f.spell, SeedName(f.info.type),
               g_frame);
      }
      g_said_spell = f.spell;
    }
    if (f.power != g_said_power) {
      if (Log()) {
        ModLog("miracle      : %s has power up %d now (it was %d), frame "
               "%llu", SeedName(f.info.type), f.power, g_said_power, g_frame);
      }
      g_said_power = f.power;
    }
  }
  if (one_off != g_said_one_off || (one_off && held != g_said_held)) {
    if (one_off && Log()) {
      ModLog("miracle      : the Hand holds the one-off seed %p of %s as an "
             "object. %s, frame %llu", held, SeedName(f.one_off_type),
             g_settings.on && g_settings.seed_use
                 ? "The trigger makes it active, and an open grip drops it"
                 : "A press of the grip shorter than 225 ms on the seed makes "
                   "it active (the rule of the game)",
             g_frame);
    }
    g_said_one_off = one_off;
  }
  g_said_held = held;
}

}  // namespace

// --- for the other files ----------------------------------------------------

void MiracleInstall() { Init(); }

const char* MiracleSeedName(int type) { return SeedName(type); }

bool MiraclePlacesHand() {
  Init();
  return g_settings.on;
}

void MiracleFrame() {
  Init();
  g_frame = ControllersFrameCount();
  PollKey();
  TestSeeds();
  Facts f;
  // The log of each change also with the key off, so a log shows the fault.
  const bool any = ReadFacts(&f);
  LogChanges(f, any);
  const Facts seen = f;
  const bool have = any && Active(&f);
  g_have = have;
  g_now = f;
  // THE CHARGE (world_haptics, haptics.cpp). The press of a miracle that
  // casts at the release (a throw or a circle) starts the interface action
  // 8, and the miracle charges until the release. The Hand feels a short
  // build up at the start of the charge. The game's own effect of the cast
  // then gives the pulse of the cast.
  {
    static bool charge_was = false;
    const bool charge = have && f.seed != nullptr && f.action == 8;
    if (charge && !charge_was) {
      if (Log()) {
        ModLog("miracle      : %s charges (interface action 8), and the Hand "
               "feels the build up (world_haptics=%d), frame %llu",
               SeedName(f.info.type), HapticsWorldOn(), g_frame);
      }
      HapticsWorldStart(haptics_math::kPatternCharge, 2);
    }
    charge_was = charge;
  }
  if (have && f.seed != nullptr) ++g_frames_miracle;
  if (have && f.one_off) ++g_frames_seed;
  // WHERE THE MIRACLE IS: the Hand of the game against the point of the real
  // hand, a few times after a miracle comes into the Hand. The Hand is from
  // the frame before, so the point of that frame is the measure.
  {
    static float last_point[3] = {0.0f, 0.0f, 0.0f};
    static bool have_last = false;
    float point[3], hand_at[3];
    bool by_controller = false;
    const bool real = HandDirectRealPoint(point);
    if (any && seen.seed != nullptr && real && have_last &&
        g_place_said < kPlaceLines * kPlaceEvery &&
        HandDirectHandPosition(hand_at, &by_controller) && by_controller) {
      if (g_place_said % kPlaceEvery == 3) {
        const float units = HandDirectRoomUnits();
        const float dx = hand_at[0] - last_point[0],
                    dy = hand_at[1] - last_point[1],
                    dz = hand_at[2] - last_point[2];
        const float apart = sqrtf(dx * dx + dy * dy + dz * dz);
        ModLog("miracle      : the Hand of the game with the miracle is at "
               "%.1f %.1f %.1f, and the point of the real hand is at %.1f "
               "%.1f %.1f: %.1f cm of the room apart (miracle_hand=%d, Hand "
               "state %d), frame %llu", hand_at[0], hand_at[1], hand_at[2],
               last_point[0], last_point[1], last_point[2],
               units > 0.0f ? apart / units * 100.0f : 0.0f,
               g_settings.on ? 1 : 0, seen.hand_state, g_frame);
      }
      ++g_place_said;
    }
    have_last = real;
    if (real) {
      for (int i = 0; i < 3; ++i) last_point[i] = point[i];
    }
  }
  CancelFrame(f, have);
  // The end of a pour: the interface action is no pour action now (10 pours
  // on the land, 11 on an object).
  if (g_pour_casts > 0 && !(any && (seen.action == 10 || seen.action == 11))) {
    if (Log()) {
      ModLog("miracle      : the pour ends: the game sent %llu casts in %llu "
             "frames, frame %llu", g_pour_casts, g_frame - g_pour_from,
             g_frame);
    }
    g_pour_casts = 0;
  }
  // The use of a seed.
  if (g_use.stage != miracle_math::kUseIdle) {
    const bool lives =
        g_use_seed != nullptr && !IsBadReadPtr(g_use_seed, 4) &&
        reinterpret_cast<uintptr_t>(*static_cast<void* const*>(g_use_seed)) ==
            kOneOffTable;
    const bool empty = have && f.held == nullptr &&
                       f.status[kStatusHolding] == 0;
    bool tap = false;
    const int stage = g_use.stage;
    if (!miracle_math::UseStep(&g_use, empty, lives, have && f.seed != nullptr,
                               kUseFrames, &tap)) {
      g_refusal = miracle_math::kRefuseOther;
      g_refusal_at = NowSeconds();
      if (Log()) {
        ModLog("miracle      : the use of the seed %p ended with no miracle "
               "in the Hand: %s, frame %llu", g_use_seed,
               !lives ? "the seed is gone"
               : stage == miracle_math::kUseLetGo
                   ? "the game did not let the seed go, for example outside "
                     "your influence"
                   : "the game did not answer the tap, for example outside "
                     "your influence",
               g_frame);
      }
    }
    if (tap) g_tap_now = true;
  }
  if (g_refusal != miracle_math::kRefuseNone &&
      NowSeconds() - g_refusal_at > kRefusalSeconds) {
    g_refusal = miracle_math::kRefuseNone;
  }
}

void MiracleSelect() {
  void* last = g_target;
  g_target = nullptr;
  g_blocked = nullptr;
  if (!g_ready || !g_have || g_now.seed == nullptr) {
    g_said_target = nullptr;
    g_said_blocked = nullptr;
    return;
  }
  Facts f;
  if (!Active(&f) || f.seed == nullptr) return;
  float hand[3], origin[3], direction[3], eye[3];
  bool by_controller = false;
  if (!HandDirectHandPosition(hand, &by_controller)) return;
  float units = HandDirectRoomUnits();
  float ray_units = 0.0f;
  const bool have_ray =
      ControllersFarRay(origin, direction, &ray_units, eye) && ray_units > 0.0f;
  if (!(units > 0.0f)) units = have_ray ? ray_units : HeadPoseScale();
  g_units = units;
  for (int i = 0; i < 3; ++i) g_hand_at[i] = hand[i];
  const float touch = g_settings.touch_cm * 0.01f * units;
  const float most = g_settings.max_m * units;
  // The best new body, and the target of the last frame with the keep
  // limits (miracle_math::Takes).
  struct Found {
    void* object;
    float at[3];
    float radius;
    int kind;
    miracle_math::Aim aim;
  };
  Found fresh = {}, kept = {};
  bool last_offered = false, last_valid = false;
  miracle_math::Aim last_now = {false, false, 0.0f, 0.0f};
  miracle_math::Aim best_blocked = {false, false, 0.0f, 0.0f};
  // One candidate: an object, and the point of its mesh when the draw
  // offered it.
  const auto consider = [&](void* object, const float* at) {
    void** table = VTable(object);
    const bool creature =
        reinterpret_cast<VirtualIntFn>(table[kIsCreature / 4])(object,
                                                                nullptr) != 0;
    const bool valid =
        reinterpret_cast<VirtualIntFn>(table[kValidAsTarget / 4])(
            object, nullptr) != 0 &&
        reinterpret_cast<ValidToApplyFn>(
            VTable(f.held)[kValidToApplyToObject / 4])(f.held, nullptr,
                                                        f.status, object) == 1;
    if (object == last) {
      last_offered = true;
      last_valid = valid;
    }
    // Only a Creature shows as a target that the game refuses.
    if (!valid && !(creature && f.info.on_object)) return;
    float radius = HandDirectRadius(object);
    float center[3] = {0.0f, 0.0f, 0.0f};
    if (at != nullptr) {
      center[0] = at[0];
      center[1] = at[1] + radius;
      center[2] = at[2];
    }
    if (creature) {
      if (!HandDirectCreatureBody(object, center) && at == nullptr) return;
    } else if (at == nullptr) {
      return;
    }
    const miracle_math::Aim aim = miracle_math::AimAt(
        hand, origin, direction, have_ray, center, radius, touch,
        g_settings.cone_deg, most);
    if (valid) {
      const int kind =
          creature ? kTargetCreature
          : reinterpret_cast<VirtualIntFn>(table[kIsReturnPoint / 4])(
                object, nullptr) != 0
              ? kTargetReturn
          : reinterpret_cast<VirtualIntFn>(table[kIsSpellDispenser / 4])(
                object, nullptr) != 0
              ? kTargetDispenser
              : kTargetOther;
      if (object == last) {
        // The keep limits: twice the touch, the keep cone, and a quarter
        // more range.
        last_now = miracle_math::AimAt(hand, origin, direction, have_ray,
                                       center, radius, 2.0f * touch,
                                       g_settings.keep_deg, 1.25f * most);
        if (last_now.valid) {
          kept.object = object;
          for (int k = 0; k < 3; ++k) kept.at[k] = center[k];
          kept.radius = radius;
          kept.kind = kind;
          kept.aim = last_now;
        }
      }
      if (!miracle_math::Better(aim, fresh.aim)) return;
      fresh.object = object;
      for (int k = 0; k < 3; ++k) fresh.at[k] = center[k];
      fresh.radius = radius;
      fresh.kind = kind;
      fresh.aim = aim;
    } else {
      if (!miracle_math::Better(aim, best_blocked)) return;
      best_blocked = aim;
      g_blocked = object;
      for (int k = 0; k < 3; ++k) g_blocked_at[k] = center[k];
      g_blocked_radius = radius;
      const MapCoords coords = {static_cast<int32_t>(center[0] * kMapScale),
                                static_cast<int32_t>(center[2] * kMapScale),
                                0.0f};
      g_blocked_why = WhyRefused(f, &coords, true);
    }
  };
  // THE CREATURES OF THE PLAYERS come first, from the game and not from the
  // draw. The draw offers an object in the aim only when the point of its
  // mesh, its feet, is in the cone of the far grab. The body of a Creature is
  // 7 units and more above its feet, so a Creature 50 cm of the room from the
  // Hand has the aim on its body and is not offered. Each player has one Creature at most
  // (GPlayer+0xA4C), and the mod knows its body with no draw.
  void* creatures[kPlayers] = {nullptr};
  for (int i = 0; i < kPlayers; ++i) {
    void* const* slot = reinterpret_cast<void* const*>(
        f.game + kGamePlayers + kGamePlayerSize * i + kPlayerCreature);
    if (IsBadReadPtr(slot, sizeof(void*))) break;
    void* creature = *slot;
    if (creature == nullptr || creature == f.held ||
        IsBadReadPtr(creature, 0x170)) {
      continue;
    }
    const uintptr_t table =
        reinterpret_cast<uintptr_t>(*static_cast<void* const*>(creature));
    if (table < kRdataStart || table >= kRdataEnd) continue;
    if (reinterpret_cast<VirtualIntFn>(VTable(creature)[kIsCreature / 4])(
            creature, nullptr) == 0) {
      continue;
    }
    creatures[i] = creature;
    consider(creature, nullptr);
  }
  const int count = HandDirectConeCount();
  for (int i = 0; i < count; ++i) {
    void* object = nullptr;
    float at[3];
    if (!HandDirectConeAt(i, &object, at) || object == nullptr ||
        object == f.held) {
      continue;
    }
    bool done = false;
    for (int k = 0; k < kPlayers; ++k) done |= creatures[k] == object;
    if (!done) consider(object, at);
  }
  // The target: the kept one, unless a new body takes its place.
  const float kSwitchDegrees = 2.0f;
  const Found* chosen = nullptr;
  if (kept.object != nullptr &&
      (fresh.object == nullptr || fresh.object == kept.object ||
       !miracle_math::Takes(fresh.aim, kept.aim, kSwitchDegrees))) {
    chosen = &kept;
  } else if (fresh.object != nullptr) {
    chosen = &fresh;
  }
  miracle_math::Aim best = {false, false, 0.0f, 0.0f};
  if (chosen != nullptr) {
    best = chosen->aim;
    g_target = chosen->object;
    g_target_table = *static_cast<void**>(chosen->object);
    for (int k = 0; k < 3; ++k) g_target_at[k] = chosen->at[k];
    g_target_radius = chosen->radius;
    g_target_at_hand = chosen->aim.at_hand;
    g_target_kind = chosen->kind;
  }
  if (g_target != nullptr) {
    g_blocked = nullptr;
    ++g_target_frames;
    // The miracle has its target now, so the card does not say that it needs
    // one.
    if (g_refusal == miracle_math::kRefuseNeedsTarget) {
      g_refusal = miracle_math::kRefuseNone;
    }
  }
  if (g_target != g_said_target) {
    if (g_target != nullptr && g_target != last) {
      HapticsPulse(haptics_math::kTarget);
    }
    if (Log()) {
      if (g_target != nullptr) {
        ModLog("miracle      : the target of %s is the %s %p, %s: %.1f cm of "
               "the room from the Hand's point and %.1f degrees off the aim. "
               "A press of the grip casts the miracle on it, frame %llu",
               SeedName(f.info.type), HandDirectClass(g_target), g_target,
               best.at_hand ? "at the Hand" : "in the aim",
               best.gap / units * 100.0f, best.degrees, g_frame);
      } else if (last == nullptr) {
        ModLog("miracle      : %s has no target now, frame %llu",
               SeedName(f.info.type), g_frame);
      } else if (!last_offered) {
        ModLog("miracle      : %s has no target now: %p is no Creature of a "
               "player, and the draw did not offer it to the pick in this "
               "frame, frame %llu", SeedName(f.info.type), last, g_frame);
      } else if (!last_valid) {
        ModLog("miracle      : %s has no target now: the game does not let "
               "the miracle act on %p now, frame %llu", SeedName(f.info.type),
               last, g_frame);
      } else {
        ModLog("miracle      : %s has no target now: %p is %.1f cm of the "
               "room from the Hand's point and %.1f degrees off the aim. A "
               "target stays to %.1f cm and %.1f degrees (miracle_keep_deg), "
               "frame %llu", SeedName(f.info.type), last,
               last_now.gap / units * 100.0f, last_now.degrees,
               2.0f * g_settings.touch_cm, g_settings.keep_deg, g_frame);
      }
    }
    g_said_target = g_target;
  }
  if (g_blocked != g_said_blocked) {
    if (g_blocked != nullptr && Log()) {
      ModLog("miracle      : the %s %p is %s, but the game does not let %s "
             "act on it: %s, frame %llu", HandDirectClass(g_blocked),
             g_blocked, best_blocked.at_hand ? "at the Hand" : "in the aim",
             SeedName(f.info.type),
             miracle_math::RefusalWords(g_blocked_why), g_frame);
    }
    g_said_blocked = g_blocked;
  }
}

void MiracleKeepRecord() {
  if (!g_ready || !g_have || g_now.seed == nullptr || g_now.face == nullptr) {
    return;
  }
  // The seed of the facts is from the start of the frame: the Hand must still
  // hold it.
  if (*reinterpret_cast<void* const*>(g_now.hand + kHandHeld) != g_now.held) {
    return;
  }
  void* want = nullptr;
  if (g_target != nullptr && !IsBadReadPtr(g_target, sizeof(void*)) &&
      *static_cast<void**>(g_target) == g_target_table) {
    want = g_target;
  }
  void* next = *reinterpret_cast<void* const*>(g_now.face + kFaceNextObject);
  if (next == want) return;
  if (!HandDirectRecordTarget(want, g_target_at)) return;
  if (want != nullptr) {
    ++g_record_puts;
  } else {
    ++g_record_clears;
  }
}

void MiracleInterfaceStep(void* face) {
  if (!g_ready || face == nullptr) return;
  Facts f;
  if (!Active(&f) || f.face != face) {
    g_cancel = nullptr;
    g_tap_now = false;
    return;
  }
  if (g_cancel != nullptr) {
    const char* how = g_cancel;
    g_cancel = nullptr;
    if (f.seed != nullptr) RunCancel(f, how);
  }
  if (g_tap_now) {
    g_tap_now = false;
    const bool lives =
        g_use_seed != nullptr && !IsBadReadPtr(g_use_seed, 4) &&
        reinterpret_cast<uintptr_t>(*static_cast<void* const*>(g_use_seed)) ==
            kOneOffTable;
    if (lives && f.held == nullptr &&
        SameBytes(kTap, kTapBytes, sizeof(kTapBytes))) {
      const bool influence =
          *reinterpret_cast<const int32_t*>(f.face + kFaceInfluence) != 0;
      reinterpret_cast<TapFn>(kTap)(f.face, nullptr, g_use_seed);
      if (Log()) {
        ModLog("miracle      : the Hand is empty, so the mod taps the seed "
               "%p with the game's own tap (fn_005D38A0, packet 0x20). The "
               "land under the Hand is %s your influence%s, frame %llu",
               g_use_seed, influence ? "inside" : "outside",
               influence ? "" : ", so the game sends no tap", g_frame);
      }
    }
  }
  MiracleKeepRecord();
}

bool MiraclePumpTrigger(bool trigger, bool ok) {
  const bool pressed = trigger && !g_trigger_was;
  g_trigger_was = trigger;
  if (!g_ready || !g_settings.on || !g_settings.seed_use) return false;
  const bool busy = g_use.stage != miracle_math::kUseIdle;
  const bool carried = g_have && g_now.one_off && g_now.held != nullptr;
  if (!busy && !(carried && ok)) return false;
  if (pressed && !busy && carried) {
    g_use.stage = miracle_math::kUseLetGo;
    g_use.frames = 0;
    g_use_seed = g_now.held;
    g_mask_grip = true;
    ++g_uses;
    HapticsPulse(haptics_math::kActivate);
    if (Log()) {
      ModLog("miracle      : the trigger closes with the one-off seed %p of "
             "%s in the Hand: the Hand lets the seed go, and then the mod "
             "taps it, frame %llu", g_use_seed, SeedName(g_now.one_off_type),
             g_frame);
    }
  }
  // The trigger does not reach the game while the Hand carries a seed, so it
  // does not drag the land.
  return true;
}

bool MiracleGripMasked(bool grip_down) {
  if (!grip_down) g_mask_grip = false;
  return g_mask_grip;
}

bool MiracleHalo(FarDraw* out) {
  memset(out, 0, sizeof(*out));
  if (!g_ready || !g_have || g_now.seed == nullptr) return false;
  const bool blocked = g_target == nullptr && g_blocked != nullptr;
  if (g_target == nullptr && !blocked) return false;
  out->active = true;
  out->valid = !blocked;
  for (int i = 0; i < 3; ++i) {
    out->object[i] = blocked ? g_blocked_at[i] : g_target_at[i];
    out->hand[i] = g_hand_at[i];
  }
  out->radius = blocked ? g_blocked_radius : g_target_radius;
  out->units_per_meter = g_units;
  out->target = blocked ? g_blocked : g_target;
  return true;
}

bool MiracleWantsCone() {
  return g_ready && g_settings.on && g_have && g_now.seed != nullptr;
}

bool MiracleDrawsCircle() {
  return g_ready && g_settings.on && g_have && g_now.seed != nullptr &&
         g_now.info.cast == miracle_math::kCastThrow &&
         g_now.info.gesture != 0 && GripDown() &&
         (g_now.action == 8 || g_now.action == 9);
}

int MiracleHint(MiracleLine lines[kMiracleLines]) {
  if (!g_ready || !g_settings.on || !g_have) return 0;
  const bool seed_held = g_now.one_off && g_now.held != nullptr;
  const bool miracle = g_now.seed != nullptr;
  if (!seed_held && !miracle) return 0;
  int count = 0;
  const auto add = [&](int word, const wchar_t* text) {
    if (count >= kMiracleLines) return;
    lines[count].word = word;
    lines[count].text = text;
    ++count;
  };
  const int prompt = miracle_math::PromptOf(
      seed_held, miracle, g_now.info, g_target != nullptr, GripDown(),
      g_now.spell != nullptr);
  // Why the game refused the last cast, or why it will refuse this one.
  int why = g_refusal;
  if (why == miracle_math::kRefuseNone && miracle && g_blocked != nullptr &&
      g_target == nullptr) {
    why = g_blocked_why;
  }
  switch (prompt) {
    case miracle_math::kPromptSeedHeld:
      if (g_settings.seed_use) add(kWordAction, L"Use the miracle");
      add(kWordGrab, L"Open to drop");
      // With miracle_seed_use=0 only the rule of the game makes the seed
      // active: a short press on the seed.
      if (!g_settings.seed_use) add(-1, L"Then tap the grip on it to use it");
      break;
    case miracle_math::kPromptTarget:
      add(kWordGrab,
          g_target_kind == kTargetCreature    ? L"Cast on the Creature"
          : g_target_kind == kTargetReturn    ? L"Give the miracle back"
          : g_target_kind == kTargetDispenser ? L"Put it in the dispenser"
                                              : L"Cast on the target");
      break;
    case miracle_math::kPromptNeedTarget:
      if (why == miracle_math::kRefuseNone ||
          why == miracle_math::kRefuseNeedsTarget) {
        add(-1, L"Touch a Creature, or point at one");
        why = miracle_math::kRefuseNone;
      }
      break;
    case miracle_math::kPromptPlace:
      add(kWordGrab, L"Cast here");
      break;
    case miracle_math::kPromptPour:
      add(kWordGrab, L"Hold to pour");
      break;
    case miracle_math::kPromptPouring:
      add(kWordGrab, L"Open to stop");
      break;
    case miracle_math::kPromptThrow:
      add(kWordGrab, L"Hold, swing, open to throw");
      break;
    case miracle_math::kPromptCircle:
      add(kWordGrab, L"Hold and draw a circle");
      break;
    case miracle_math::kPromptHeld:
      add(kWordGrab, g_now.info.gesture != 0 ? L"Draw a circle, then open"
                                             : L"Open to throw");
      break;
    case miracle_math::kPromptRuns:
      add(-1, L"The miracle acts now");
      break;
    default:
      break;
  }
  switch (why) {
    case miracle_math::kRefuseNotReady:
      add(-1, L"The miracle is not ready yet");
      break;
    case miracle_math::kRefuseOffMap:
      add(-1, L"The Hand is not over the land");
      break;
    case miracle_math::kRefuseNeedsLand:
      add(-1, L"This miracle needs land here");
      break;
    case miracle_math::kRefuseInfluence:
      add(-1, g_blocked != nullptr && g_target == nullptr
                  ? L"The Creature is outside your influence"
                  : L"This place is outside your influence");
      break;
    case miracle_math::kRefuseNeedsTarget:
      add(-1, L"It needs a Creature as its target");
      break;
    case miracle_math::kRefuseNeedsCircle:
      add(-1, L"Draw a circle on the land first");
      break;
    case miracle_math::kRefuseOther:
      add(-1, miracle ? L"The game refused the cast here"
                      : L"Use it inside your influence");
      break;
    default:
      break;
  }
  if (miracle) {
    add(g_settings.button ? kWordGesture : -1,
        g_settings.button
            ? (g_settings.shake_mps > 0.0f ? L"Tap to cancel, or shake"
                                           : L"Tap to cancel")
            : L"Shake the hand to cancel");
  }
  return count;
}

void MiracleReport() {
  if (!g_ready) return;
  if (g_frames_miracle == 0 && g_frames_seed == 0 && g_casts == 0 &&
      g_refusals == 0 && g_cancels == 0 && g_uses == 0) {
    return;
  }
  ModLog("miracle      : a miracle was in the Hand on %llu frames and a "
         "one-off seed on %llu. %llu casts sent, %llu casts refused, %llu "
         "cancels, %llu seeds used with the trigger. A target on %llu "
         "frames: the mod put it in the pick record %llu times and took "
         "another object out %llu times, this window (miracle_hand=%d)",
         g_frames_miracle, g_frames_seed, g_casts, g_refusals, g_cancels,
         g_uses, g_target_frames, g_record_puts, g_record_clears,
         g_settings.on ? 1 : 0);
  g_frames_miracle = 0;
  g_frames_seed = 0;
  g_casts = 0;
  g_refusals = 0;
  g_cancels = 0;
  g_uses = 0;
  g_target_frames = 0;
  g_record_puts = 0;
  g_record_clears = 0;
}
