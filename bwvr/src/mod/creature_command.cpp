// The Creature commands. See docs/creature.md, "The Creature commands".
//
// WHAT THE GAME DOES WITH THE MOUSE [V, runblack.exe 1.42].
//
// The leash is a GLeashStatus at GInterfaceStatus+0x12C. +0x14 is 1 while the
// leash is on, +0x1C is the kind of the leash, and +0x24 is the object that
// the leash is tied to, or null while the Hand holds it.
//
// - A press of the Action button on bare land, with an empty Hand and the
//   leash on the Hand, sends packet 0x5E with the land point of the next pick
//   record (GInterface+0x3F0). fn_005D1330 is the press, and fn_005D3D10
//   sends the packet at 0x005D3D88. The packet runs
//   Creature::ForceActOnMapCoord (0x004C6540): the Creature goes there.
// - A press on an object that the Hand cannot take runs fn_005D3930. With
//   the leash on the Hand, and ValidAsInterfaceTarget (slot 0x6F0) at 1 and
//   ValidAsInterfaceLeashTarget (slot 0x6F4) not 0, fn_005D4180 sends packet
//   0x5D with the object. The packet runs Creature::ForceActOnObject
//   (0x004C5FF0): the Creature acts on the object. A press on an object that
//   the Hand can take picks it up, as with no leash.
// - A double click on an object sends packet 0x5C (fn_005D4210): the leash
//   is tied to the object. The mod does not use it.
// - The shake is the gesture of type 5 (the scribble). With an empty Hand and
//   the leash on the Hand, GInterface::ProcessPowerUpSystem tests the ring of
//   pointer samples against the templates of type 5 (0x005CF735). On a match
//   it clears the ring (fn_005CE420 with 0) and sends packet 0x5F, the leash
//   click (0x005CF76B). The test reads the turns of a path in pixels. It has
//   no speed and no size to set.
// - The game draws nothing on the land where the Creature goes. It shows the
//   tooltip 0xEA2 beside the Hand (GInterface::CalculateLeashToolTip) and
//   plays a sound.
//
// WHAT THE MOD DOES. While the leash is on the Hand and the controller places
// the Hand, the trigger commands the Creature:
//
// - The aim pose of the pointing hand selects an object, with the cone of the
//   grab from a distance and with no limit of the range. The object gets the
//   green halo. A press and a release of the trigger with the aim on it sends
//   the game's packet 0x5D through fn_005D4180.
// - An object at the Hand is a target too: the object that a press of the
//   grip acts on, within hand_pick_cm of the Hand's point. It is the target,
//   and the aim ray does not count. An object in the aim is a target at each
//   distance from the Hand. Over the sea the arc has no land point, so a
//   press on no object sends nothing.
// - A press with the aim on no object shows the arc of the teleport and a
//   green ring on the land. The release sends the game's packet 0x5E with
//   that land point. A tap, a press shorter than creature_command_hold_ms,
//   sends the land point of the press. A hold sends the land point of the
//   release, after the player moved the arc. The ring stays on the place for
//   a moment after the send.
// - THE LIVE TARGET (creature_command_rule=2). While the trigger is held,
//   the target is what the hand points at now: the halo of an object while
//   the aim is on one, and the arc with the ring as soon as the aim is on no
//   object. A hold acts on what shows at the release. A tap acts on what
//   showed at the press. An object takes the aim only in a tight cone, near
//   the player, and not when the land in front of it has the aim
//   (creature_command_math.h, AimRule).
// - THE WALK STAYS A WALK (creature_command_rule=3 and 4). A press that
//   began on the land (no object in the cone of the press) is a walk command
//   for as long as the trigger is held. The aim moves the land point, and no
//   object takes the halo or the command, also when the aim passes over a tree,
//   a rock or a villager. The player aims at the object first and then
//   presses to command the Creature to it. Rule 2 lets a held walk change to
//   an object that stays in the aim for 100 ms.
// - THE WALK MODE (creature_command_rule=4, the default). A press that began
//   on an object is the command on that object while the aim stays on it, and
//   a tap is that command too. When the trigger stays down and the aim is
//   off the object for 100 ms, the press becomes a walk for the rest of it:
//   the halo goes, the arc and the ring show the land, and no object takes
//   the target, also not the first object when the aim comes back to it.
//   While such a press is held, only its own object is a target. With rule 3
//   the target of a press on an object is what the hand points at, object
//   after object.
// - THE CHOICE OF THE OBJECT is the choice of the grab from a distance
//   (far_grab_choice, far_grab_stay, far_choice_math.h): with ranks, a living
//   thing in the cone of the command comes before a tree or a building.
// - THE DOUBLE PRESS ties the leash to an object, as the game's double click
//   does: the first tap sends the Creature to the object (packet 0x5D), and
//   a second press on the same object sends packet 0x5C. While the leash is
//   tied to an object, a short press with the aim on the Creature, or a
//   double press with the aim on that object, takes the leash back to the
//   Hand.
// - A short press with the aim on the player's own Creature sends the leash
//   click, packet 0x5F.
// - A press while the game's pick has the Creature under the Hand is not a
//   command. It goes to the game: the stroke, the slap and the leash click of
//   "The Creature by touch" in docs/creature.md.
// - The trigger of a command does not reach the game, so it does not drag
//   the land.
// - A press of the grip on bare land does not send the Creature: the hook
//   on fn_005D3D10 leaves the packet out while the controller places the
//   Hand. The mouse gets the game's function.
//
// THE SHAKE. The mod does not feed the game's shape test. A real hand moves
// all the time, and the same ring starts the Miracle gestures and changes the
// kind of the leash. So the mod finds the shake in the motion of the real
// grip, in meters and seconds (creature_command_math.h). Then it makes the
// game's own tests of the state and does what the game does on a match: it
// clears the ring and sends packet 0x5F.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"
#include "aim_smooth.h"
#include "controller_math.h"
#include "controllers.h"
#include "creature_command.h"
#include "far_choice_math.h"
#include "creature_command_math.h"
#include "debug_menu.h"
#include "far_grab.h"
#include "haptics.h"
#include "hint_card.h"
#include "hint_learn_math.h"
#include "overlay_math.h"
#include "table_math.h"
#include "tabletop.h"
#include "teleport.h"

extern void ModLog(const char* format, ...);

// controllers.cpp
bool ControllersFarRay(float origin[3], float direction[3],
                       float* units_per_meter, float eye[3]);
bool ControllersFarAllowed(const char** why);
void ControllersGesturePulse(float amplitude, float seconds);
bool ControllerDirectMotion(float reach_room[3], float grip_room[3],
                            float room[9]);
// head_pose.cpp: game units in a real meter.
float HeadPoseScale();
// head_camera.cpp: the head is on the camera.
bool HeadCameraEnabled();
// hand_direct.cpp
bool HandDirectHandPosition(float out[3], bool* by_controller);
int HandDirectConeCount();
bool HandDirectConeAt(int index, void** object, float position[3]);
float HandDirectRadius(void* object);
bool HandDirectSeen(const float at[3], float radius);
bool HandDirectNearObject(void** object, float position[3]);
bool HandDirectObjectLives(void* object, void* table);
const char* HandDirectClass(void* object);
bool HandDirectLiving(void* object);
bool HandDirectCreatureBody(void* creature, float out[3]);
// overlay.cpp: the drawn hand (0 the left hand, 1 the right hand) takes the
// point pose for this frame.
void OverlayHandPoints(int hand);

namespace {

using command_math::Command;
using command_math::PressKind;

// --- the game ---------------------------------------------------------------

const uintptr_t kGame = 0x00D0195C;
// GGame::MyInterface, thiscall, no arguments.
const uintptr_t kMyInterface = 0x00555850;
// GGame: the players, and the number of the local one.
const int kGamePlayers = 0x18;
const int kGamePlayerSize = 0xA60;
const int kGameLocalPlayer = 0x205A59;
const int kPlayerCreature = 0xA4C;   // GPlayer: its Creature
// GInterface.
const int kFaceLocked = 0x38;        // the pick lock
const int kFaceStatus = 0x39C;       // the local GInterfaceStatus
const int kFaceHand = 0x3A0;         // the CHand
const int kFaceObject = 0x3C8;       // the object under the Hand, now
const int kFaceNextObject = 0x400;   // the object of the next pick record
// GInterfaceStatus.
const int kStatusHolding = 0x90;     // a byte: the Hand holds something
const int kStatusLeash = 0x12C;      // the GLeashStatus
// GLeashStatus.
const int kLeashOn = 0x14;
const int kLeashObject = 0x24;
// CHand.
const int kHandState = 0x4878;
const int kHandHeld = 0x4904;
const int32_t kStateNormal = 1;
// Creature::GetInterfaceStatusLeashOn, thiscall, no arguments: the
// GInterfaceStatus whose leash is on this Creature, or null.
const uintptr_t kLeashOnStatus = 0x004CF060;
// The action of the Creature now: [Creature+0x164] is its mind, with the
// count of the actions at +0xFBC, the index at +0xFB4 and the action at
// +0xFD8 + 0x60 * index. While it is 0x5D the game sends no command
// (0x005D3D75 and 0x005D41D5).
const int kCreatureMind = 0x164;
const int kMindCount = 0xFBC;
const int kMindIndex = 0xFB4;
const int kMindAction = 0xFD8;
const int kMindActionSize = 0x60;
const int32_t kActionNoCommand = 0x5D;
// fn_005D4180, thiscall on GInterface (Object*), ret 4. It sends packet 0x5D
// with the object when the player's Creature is on this leash and its action
// is not 0x5D. It gives 1 when it sent the packet.
const uintptr_t kActOnObject = 0x005D4180;
// fn_005D3D10, thiscall on GInterface (const MapCoords*), ret 4: a press of
// the Action button on bare land. With the leash on the Hand it sends packet
// 0x5E and clears the pick lock. Its one caller is 0x005D1548.
const uintptr_t kLandPress = 0x005D3D10;
// fn_00550CB0, thiscall on GGame (int packet, const MapCoords*), ret 8.
const uintptr_t kSetPacketCoords = 0x00550CB0;
const int32_t kPacketGoTo = 0x5E;
// GGame::SetPacket, thiscall (int packet), ret 4.
const uintptr_t kSetPacket = 0x00550DD0;
const int32_t kPacketLeash = 0x5F;
// fn_005CE420, thiscall on GInterface (bool draw), ret 4: it clears the ring
// of the gestures and starts the wait of 0.4 s. With 1 it also draws the
// gesture on the land.
const uintptr_t kClearGestures = 0x005CE420;
// The map coordinates and the land height, as teleport.cpp takes them.
const uintptr_t kMakeMapCoords = 0x004427B0;
// Vtable slots of Object.
const int kValidAsTarget = 0x6F0;       // ValidAsInterfaceTarget
const int kValidAsLeashTarget = 0x6F4;  // ValidAsInterfaceLeashTarget
const int kGetHeight = 0x42C;           // the height, a float
// THE LEASH ON AN OBJECT [V]. The game's double click (fn_005D4210, from the
// step of the default action fn_005D5190 with bit 8 of GInterface+0x39) sends
// packet 0x5C through fn_00550C80 (thiscall on GGame, the packet and the
// index of an object, ret 8). With the index of an object the handler
// (0x0063D416) ties the leash to it (fn_005E6BD0), ends the action of the
// Creature and calls ForceActOnObject. With 0 it takes the leash off the
// object and puts it on the Hand again (fn_005E6EA0). Base::GetIndexOffset
// (thiscall, no arguments) gives the index.
const uintptr_t kSetPacketIndex = 0x00550C80;
const int32_t kPacketTie = 0x5C;
const uintptr_t kIndexOffset = 0x00436A70;
// The double click taps a OneOffSpellSeed and does not tie the leash to it
// (0x005D42BB). ___RTDynamicCast is cdecl: the object, 0, the type of the
// object, the type to cast to, 0.
const uintptr_t kDynamicCast = 0x007C5A19;
const uintptr_t kTypeObject = 0x009C7F50;
const uintptr_t kTypeOneOffSpellSeed = 0x00BECA20;
// LH3DIsland::GetAltitude, thiscall on a MapCoords, as teleport.cpp calls it.
const uintptr_t kGetAltitude = 0x00803090;
const float kMapSize = 5120.0f;

struct MapCoords {
  int32_t x, z;
  float over;  // the height over the land, 0 for a land point
};

typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);
typedef void*(__fastcall* LeashOnStatusFn)(void* creature, void* unused);
typedef int(__fastcall* ActOnObjectFn)(void* face, void* unused, void* object);
typedef int(__fastcall* LandPressFn)(void* face, void* unused,
                                     const MapCoords* at);
typedef void(__fastcall* SetPacketCoordsFn)(void* game, void* unused,
                                            int32_t packet,
                                            const MapCoords* at);
typedef void(__fastcall* SetPacketFn)(void* game, void* unused,
                                      int32_t packet);
typedef void(__fastcall* ClearGesturesFn)(void* face, void* unused, int draw);
typedef MapCoords*(__fastcall* MakeMapCoordsFn)(MapCoords* out, void* unused,
                                                float x, float z);
typedef int(__fastcall* VirtualIntFn)(void* self, void* unused);
typedef float(__fastcall* VirtualFloatFn)(void* self, void* unused);
typedef void(__fastcall* SetPacketIndexFn)(void* game, void* unused,
                                           int32_t packet, int32_t index);
typedef int32_t(__fastcall* IndexOffsetFn)(void* object, void* unused);
typedef void*(__cdecl* DynamicCastFn)(void* object, int32_t delta,
                                      void* from_type, void* to_type,
                                      int32_t reference);
typedef float(__fastcall* GetAltitudeFn)(const MapCoords* at, void* unused);

LandPressFn o_LandPress = nullptr;

// --- the keys ---------------------------------------------------------------

struct Settings {
  bool on = true;           // creature_command
  float shake_mps = 0.6f;   // leash_shake_mps: 0 turns the shake off
  // creature_command_hold_ms: a shorter press on the land is a tap, and it
  // uses the aim of the press.
  int hold_ms = 250;
  int reach_m = 10;         // teleport_reach_m, so the arc is the teleport's
  int ring_cm = 11;         // teleport_ring_cm
  // creature_command_test, for the desktop runs: from this frame of the mod
  // the leash counts as on, and the commands are logged and not sent. 0 is
  // off.
  int test_from = 0;
  bool test = false;
  // creature_command_rule: 4 is the walk mode: a press on the land stays a
  // walk, and a press on an object becomes a walk when the aim leaves the
  // object. 3 is the live target, and a press that began on the land stays a
  // walk. 2 is the live target: a held walk can change to an object. 1: the
  // press fixes its kind, and an object takes the aim in the cone of the grab
  // from a distance.
  int rule = 4;
  // The aim of rule 2 (creature_command_math.h, AimRule).
  float cone_deg = 3.0f;   // creature_command_cone_deg: a new target
  float keep_deg = 5.0f;   // creature_command_keep_deg: the target stays
  float max_m = -1.0f;     // creature_command_max_m: 0 is no limit. Less than
                           // 0 follows far_grab_choice: 10 m, or 4 m for 0
  int behind_cm = 30;      // creature_command_behind_cm: -1 turns it off
  // creature_command_double_ms: a second press on the same object this soon
  // after the first press ties the leash. 0 turns the double press off.
  int double_ms = 500;
  bool test_keys = false;  // test_keys: Ctrl and numpad 5 change the rule
};
Settings g_settings;
bool g_ready = false;
bool g_hooked = false;

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

// --- the state --------------------------------------------------------------

uint64_t g_frame = 0;

// What the game says now.
struct Facts {
  uint8_t* game;
  uint8_t* face;
  uint8_t* status;
  uint8_t* hand;
  uint8_t* leash;
  void* creature;      // the player's Creature, or null
  bool leash_on;       // GLeashStatus+0x14
  void* leash_object;  // GLeashStatus+0x24
  bool mine;           // the Creature's leash is on this status
  bool hand_empty;
  int32_t hand_state;
  void* under;         // the object under the Hand
  void* next;          // the object of the next pick record
  int32_t action;      // the action of the Creature, -1 when not known
};

// The target of the aim, from the last selection.
enum TargetKind { kTargetNone, kTargetObject, kTargetCreature };
struct Target {
  int kind;
  void* object;
  float position[3];  // the base of the object
  float radius;
  float height;       // of the body that the aim was measured against
  float te;
  bool at_hand;       // the object at the Hand's point, not in the aim
  bool can_tie;       // the leash can go on it (CanTie)
  bool holds_leash;   // the leash is tied to it now
  void* table;        // the first word of the object at the selection, for
                      // the test that the object still lives at the release
  char name[48];
};
Target g_target = {};
uint64_t g_select_frame = 0;  // the frame of the last selection

// The cone is wanted: the command can act, or a press of it runs.
bool g_live = false;
// The leash was on the Hand in the last frame, with the controller placing
// the Hand: the trigger commands the Creature.
bool g_leashed = false;
// The leash is tied to an object. Then the trigger acts only on that object
// and on the Creature: it takes the leash back.
bool g_tied = false;
const char* g_live_why = "";

// The press. claim is true from a press that the command takes until the
// trigger opens.
bool g_trigger = false;
bool g_claim = false;
PressKind g_press = command_math::kPressNone;
void* g_press_object = nullptr;
void* g_press_table = nullptr;  // Target::table of the press
double g_press_time = 0.0;
// THE LIVE TARGET (rule 2). The target of the press, and what shows now
// while the trigger is held: the halo of g_shown_target, or the land aim.
Target g_press_target = {};
Target g_shown_target = {};
command_math::HeldState g_held = {};
// The press did its work when it started (a double press), so the release
// does nothing.
bool g_press_done = false;
// THE DOUBLE PRESS. The last tap on an object: the object, and when its
// press started.
void* g_tap_object = nullptr;
double g_tap_time = -1e9;
// The object of the leash in the last frame, for the log.
void* g_leash_object_was = nullptr;
bool g_have_leash_object = false;

// The land aim of a press on the land.
struct Aim {
  bool have;
  bool valid;
  const char* why;
  float target[3];
};
Aim g_aim = {};
// The first land aim of the press, for a tap.
Aim g_press_aim = {};
bool g_have_press_aim = false;
TeleportDraw g_draw = {};
// The ring of the last send to the land, and when the packet went.
TeleportDraw g_sent_draw = {};
double g_sent_at = 0.0;
bool g_aim_reset = true;
aim_smooth::OneEuroVec3 g_filter_point;
aim_smooth::OneEuroVec3 g_filter_direction;
double g_filter_time = 0.0;
float g_arc_room[overlay_math::kArcMaxPoints][3];

// The halo, latched once a frame so both eye passes draw the same one, and
// the object that it is on.
FarDraw g_halo = {};
void* g_halo_object = nullptr;

// The shake, and the leash as the last frame saw it.
command_math::ShakeState g_shake = {};
bool g_have_leash = false;
bool g_leash_was = false;
double g_shake_sent = 0.0;   // the time of the last leash click of a shake
bool g_shake_waits = false;

// The log and the counts of a report window.
const int kMaxLogs = 400;
int g_logs = 0;
int g_refuse_logs = 0;
uint64_t g_presses = 0;
uint64_t g_passed = 0;
uint64_t g_sent_object = 0;
uint64_t g_sent_land = 0;
uint64_t g_sent_taps = 0;  // of g_sent_land: taps, with the aim of the press
uint64_t g_sent_leash = 0;
uint64_t g_nothing = 0;
uint64_t g_shakes = 0;
uint64_t g_shakes_idle = 0;
uint64_t g_grip_land = 0;
uint64_t g_targets = 0;
uint64_t g_aim_frames = 0;
uint64_t g_shown_changes = 0;  // changes of what shows while the trigger is held
uint64_t g_walk_modes = 0;     // of them: a press on an object became a walk
uint64_t g_out_range = 0;      // objects in the cone that the range left out
uint64_t g_out_land = 0;       // and that the land in front left out
uint64_t g_ties = 0;
uint64_t g_takes_back = 0;

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

// The facts of the game, read now. False when there is no game, no
// interface or no leash status.
bool ReadFacts(Facts* f) {
  memset(f, 0, sizeof(*f));
  f->action = -1;
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return false;
  f->game = static_cast<uint8_t*>(*slot);
  f->face = static_cast<uint8_t*>(
      reinterpret_cast<MyInterfaceFn>(kMyInterface)(f->game, nullptr));
  if (f->face == nullptr) return false;
  f->status = *reinterpret_cast<uint8_t* const*>(f->face + kFaceStatus);
  f->hand = *reinterpret_cast<uint8_t* const*>(f->face + kFaceHand);
  if (f->status == nullptr || f->hand == nullptr) return false;
  f->leash = *reinterpret_cast<uint8_t* const*>(f->status + kStatusLeash);
  if (f->leash == nullptr || IsBadReadPtr(f->leash, kLeashObject + 4)) {
    return false;
  }
  f->leash_on = *reinterpret_cast<const int32_t*>(f->leash + kLeashOn) != 0;
  f->leash_object = *reinterpret_cast<void* const*>(f->leash + kLeashObject);
  f->hand_state = *reinterpret_cast<const int32_t*>(f->hand + kHandState);
  f->hand_empty =
      f->status[kStatusHolding] == 0 &&
      *reinterpret_cast<void* const*>(f->hand + kHandHeld) == nullptr;
  f->under = *reinterpret_cast<void* const*>(f->face + kFaceObject);
  f->next = *reinterpret_cast<void* const*>(f->face + kFaceNextObject);
  const uint8_t* player =
      f->game + kGamePlayers + kGamePlayerSize * f->game[kGameLocalPlayer];
  f->creature = *reinterpret_cast<void* const*>(player + kPlayerCreature);
  if (f->creature != nullptr && !IsBadReadPtr(f->creature, kCreatureMind + 4)) {
    f->mine = reinterpret_cast<LeashOnStatusFn>(kLeashOnStatus)(
                  f->creature, nullptr) == f->status;
    const uint8_t* mind = *reinterpret_cast<uint8_t* const*>(
        static_cast<uint8_t*>(f->creature) + kCreatureMind);
    if (mind != nullptr && !IsBadReadPtr(mind + kMindIndex, 0x30)) {
      const uint32_t count =
          *reinterpret_cast<const uint32_t*>(mind + kMindCount);
      const uint32_t index =
          *reinterpret_cast<const uint32_t*>(mind + kMindIndex);
      f->action = 0;
      if (count > 0 && index < 4096 &&
          !IsBadReadPtr(mind + kMindAction + kMindActionSize * index, 4)) {
        f->action = *reinterpret_cast<const int32_t*>(
            mind + kMindAction + kMindActionSize * index);
      }
    }
  } else {
    f->creature = nullptr;
  }
  return true;
}

// The facts, only while the controller places the Hand.
bool Active(Facts* f) {
  float at[3];
  bool by_controller = false;
  if (!HandDirectHandPosition(at, &by_controller) || !by_controller) {
    memset(f, 0, sizeof(*f));
    return false;
  }
  return ReadFacts(f);
}

// The leash is on the player's Creature and the Hand holds it: the game's own
// tests for a command (0x005D3D38 to 0x005D3D61). With the test key the leash
// counts as on.
bool Leashed(const Facts& f) {
  if (g_settings.test) {
    return g_frame >= static_cast<uint64_t>(g_settings.test_from);
  }
  return f.creature != nullptr && f.leash_on && f.leash_object == nullptr &&
         f.mine;
}

// The leash of the player is tied to an object (GLeashStatus+0x24). The game
// then sends no command: its tests need the leash on the Hand.
bool Tied(const Facts& f) {
  return !g_settings.test && f.creature != nullptr && f.leash_object != nullptr;
}

// The rule of the aim in use. Rule 1 is the cone of the grab from a
// distance, with no range and no land rule.
bool LiveRule() { return g_settings.rule >= 2; }
// The rule where a press that began on the land stays a walk.
bool LandStays() { return g_settings.rule >= 3; }
// The rule where a press that began on an object becomes a walk when the aim
// leaves the object.
bool WalkMode() { return g_settings.rule >= 4; }

command_math::AimRule AimRuleNow() {
  command_math::AimRule rule;
  if (LiveRule()) {
    rule.select_deg = g_settings.cone_deg;
    rule.keep_deg = g_settings.keep_deg;
    rule.max_m =
        far_choice_math::CommandRangeM(FarGrabChoice(), g_settings.max_m);
    rule.behind_m = g_settings.behind_cm < 0 ? -1.0f
                                             : g_settings.behind_cm * 0.01f;
  } else {
    rule.select_deg = FarGrabSelectDeg();
    rule.keep_deg = FarGrabKeepDeg();
    rule.max_m = 0.0f;
    rule.behind_m = -1.0f;
  }
  return rule;
}

// The land height at a world point, with the sea and the places off the map
// at 0, as the teleport has it.
float LandHeight(float x, float z) {
  if (!(x >= 0.0f && x < kMapSize && z >= 0.0f && z < kMapSize)) return 0.0f;
  MapCoords at = {0, 0, 0.0f};
  reinterpret_cast<MakeMapCoordsFn>(kMakeMapCoords)(&at, nullptr, x, z);
  at.over = 0.0f;
  const float y = reinterpret_cast<GetAltitudeFn>(kGetAltitude)(&at, nullptr);
  if (!(y > 0.0f && y < 5000.0f)) return 0.0f;
  return y;
}

// The height of an object, or two radii when the game gives no sane height.
float HeightOf(void* object, float radius) {
  const float height = reinterpret_cast<VirtualFloatFn>(
      VTable(object)[kGetHeight / 4])(object, nullptr);
  if (height > 0.0f && height < 2000.0f) return height;
  return 2.0f * radius;
}

// The game's tests of fn_005D3930 for a command on an object.
bool ValidTarget(void* object) {
  void** table = VTable(object);
  return reinterpret_cast<VirtualIntFn>(table[kValidAsTarget / 4])(
             object, nullptr) == 1 &&
         reinterpret_cast<VirtualIntFn>(table[kValidAsLeashTarget / 4])(
             object, nullptr) != 0;
}

// WHETHER THE LEASH CAN GO ON AN OBJECT: the tests of the game's double click
// (fn_005D4210, 0x005D4297 to 0x005D42F8). The object is not the Creature,
// ValidAsInterfaceTarget gives 1, the object is no OneOffSpellSeed, and
// ValidAsInterfaceLeashTarget does not give 0.
bool CanTie(const Facts& f, void* object) {
  if (object == nullptr || object == f.creature) return false;
  if (!ValidTarget(object)) return false;
  return reinterpret_cast<DynamicCastFn>(kDynamicCast)(
             object, 0, reinterpret_cast<void*>(kTypeObject),
             reinterpret_cast<void*>(kTypeOneOffSpellSeed), 0) == nullptr;
}

// --- the commands -------------------------------------------------------------

void Tick() { HapticsPulse(haptics_math::kCommand); }

bool SendObject(const Facts& f, void* object, const char* name) {
  // The game can delete the object between the last draw and this release.
  if (!HandDirectObjectLives(object, g_press_table)) return false;
  if (g_settings.test) {
    ModLog("creature cmd : test: the mod would send packet 0x5D with the %s "
           "%p, frame %llu", name, object, g_frame);
    return true;
  }
  const int sent = reinterpret_cast<ActOnObjectFn>(kActOnObject)(
      f.face, nullptr, object);
  return sent == 1;
}

bool SendLand(const Facts& f, const float target[3]) {
  MapCoords at = {0, 0, 0.0f};
  reinterpret_cast<MakeMapCoordsFn>(kMakeMapCoords)(&at, nullptr, target[0],
                                                    target[2]);
  at.over = 0.0f;
  if (g_settings.test) {
    ModLog("creature cmd : test: the mod would send packet 0x5E with the map "
           "point %d %d, frame %llu", at.x, at.z, g_frame);
    return true;
  }
  // The game's own tests of fn_005D3D10 before the packet.
  if (f.creature == nullptr || f.action == kActionNoCommand) return false;
  if (at.x == 0 && at.z == 0) return false;
  reinterpret_cast<SetPacketCoordsFn>(kSetPacketCoords)(f.game, nullptr,
                                                        kPacketGoTo, &at);
  return true;
}

void SendLeash(const Facts& f) {
  CreatureCommandLeashAsked();
  if (g_settings.test) {
    ModLog("creature cmd : test: the mod would send packet 0x5F, frame %llu",
           g_frame);
    return;
  }
  reinterpret_cast<SetPacketFn>(kSetPacket)(f.game, nullptr, kPacketLeash);
}

// TIE THE LEASH TO AN OBJECT: packet 0x5C with the index of the object, as
// the game's double click sends it. The tests are the game's: the Creature
// is on the leash of this player, and the leash can go on the object.
bool TieLeash(const Facts& f, void* object, void* table, const char* name) {
  if (!HandDirectObjectLives(object, table)) return false;
  if (g_settings.test) {
    ModLog("creature cmd : test: the mod would send packet 0x5C with the %s "
           "%p, frame %llu", name, object, g_frame);
    return true;
  }
  if (!f.mine || !CanTie(f, object)) return false;
  CreatureCommandLeashAsked();
  const int32_t index =
      reinterpret_cast<IndexOffsetFn>(kIndexOffset)(object, nullptr);
  reinterpret_cast<SetPacketIndexFn>(kSetPacketIndex)(f.game, nullptr,
                                                      kPacketTie, index);
  return true;
}

// TAKE A TIED LEASH BACK TO THE HAND: packet 0x5C with 0, as the game's
// double click on the object that holds the leash sends it (0x005D42E8).
bool TakeLeashBack(const Facts& f) {
  if (f.creature == nullptr || f.leash_object == nullptr) return false;
  CreatureCommandLeashAsked();
  reinterpret_cast<SetPacketIndexFn>(kSetPacketIndex)(f.game, nullptr,
                                                      kPacketTie, 0);
  return true;
}

// THE GRIP ON THE LAND. The game's press of the Action button on bare land.
// While the controller places the Hand, the leash is on the Hand and the
// command is on, the press sends no packet 0x5E: the trigger sends the
// Creature. The rest of the function is kept: the pick lock goes off and the
// answer is 1. The mouse gets the game's function.
int __fastcall h_LandPress(void* self, void* unused, const MapCoords* at) {
  Facts f;
  if (g_settings.on && !g_settings.test && self != nullptr && at != nullptr &&
      Active(&f) && f.face == self && Leashed(f) &&
      f.action != kActionNoCommand &&
      !(at->x == 0 && at->z == 0 && at->over == 0.0f)) {
    *reinterpret_cast<int32_t*>(f.face + kFaceLocked) = 0;
    ++g_grip_land;
    if (Log()) {
      ModLog("creature cmd : a grip on the land does not send the Creature "
             "(creature_command=1). Use the trigger. Frame %llu", g_frame);
    }
    return 1;
  }
  return o_LandPress(self, unused, at);
}

// The line of the rule in use, at the start and at each press of the key.
void LogRule(const char* from) {
  if (LiveRule()) {
    ModLog("creature cmd : %s: creature_command_rule=%d. %s A new object "
           "needs the aim within %.1f degrees of its body "
           "and stays to %.1f (creature_command_cone_deg, _keep_deg). It is "
           "at most %.1f m of the room from the head (creature_command_max_m, "
           "0 is no limit). The land wins when the ray meets it more than %d "
           "cm, and 20 percent of the way to it, in front of the object "
           "(creature_command_behind_cm, -1 is off), frame %llu", from,
           g_settings.rule,
           WalkMode()    ? "A press that began on an object shows that object "
                           "while the aim is on it. It becomes a walk when the "
                           "aim is off the object for 100 ms, and it stays a "
                           "walk. A press that began on the land stays a walk: "
                           "no object takes it."
           : LandStays() ? "While the trigger is held, the target is what the "
                           "hand points at: an object, or the land. A press "
                           "that began on the land stays a walk: no object "
                           "takes it."
                         : "While the trigger is held, the target is what the "
                           "hand points at: an object, or the land. A press on "
                           "the land can change to an object.",
           g_settings.cone_deg, g_settings.keep_deg,
           far_choice_math::CommandRangeM(FarGrabChoice(), g_settings.max_m),
           g_settings.behind_cm, g_frame);
  } else {
    ModLog("creature cmd : %s: creature_command_rule=1. The press fixes its "
           "kind, and an object takes the aim in the cone of the grab from a "
           "distance, at each distance, frame %llu", from, g_frame);
  }
}

void Init() {
  if (g_ready) return;
  g_ready = true;
  g_settings.on = IniInt("creature_command", 1) != 0;
  g_settings.shake_mps = IniFloat("leash_shake_mps", 0.6f);
  if (g_settings.shake_mps < 0.0f) g_settings.shake_mps = 0.0f;
  if (g_settings.shake_mps > 0.0f && g_settings.shake_mps < 0.2f) {
    g_settings.shake_mps = 0.2f;
  }
  if (g_settings.shake_mps > 5.0f) g_settings.shake_mps = 5.0f;
  g_settings.hold_ms = IniInt("creature_command_hold_ms", 250);
  if (g_settings.hold_ms < 0) g_settings.hold_ms = 0;
  if (g_settings.hold_ms > 2000) g_settings.hold_ms = 2000;
  g_settings.reach_m = IniInt("teleport_reach_m", 10);
  if (g_settings.reach_m < 2) g_settings.reach_m = 2;
  if (g_settings.reach_m > 50) g_settings.reach_m = 50;
  g_settings.ring_cm = IniInt("teleport_ring_cm", 11);
  if (g_settings.ring_cm < 2) g_settings.ring_cm = 2;
  if (g_settings.ring_cm > 100) g_settings.ring_cm = 100;
  g_settings.test_from = IniInt("creature_command_test", 0);
  g_settings.test = g_settings.test_from > 0;
  g_settings.rule = IniInt("creature_command_rule", 4);
  if (g_settings.rule < 1) g_settings.rule = 1;
  if (g_settings.rule > 4) g_settings.rule = 4;
  g_settings.cone_deg = IniFloat("creature_command_cone_deg", 3.0f);
  if (!(g_settings.cone_deg >= 0.5f)) g_settings.cone_deg = 0.5f;
  if (g_settings.cone_deg > 20.0f) g_settings.cone_deg = 20.0f;
  g_settings.keep_deg =
      IniFloat("creature_command_keep_deg", g_settings.cone_deg + 2.0f);
  if (!(g_settings.keep_deg >= g_settings.cone_deg)) {
    g_settings.keep_deg = g_settings.cone_deg;
  }
  if (g_settings.keep_deg > 30.0f) g_settings.keep_deg = 30.0f;
  g_settings.max_m = IniFloat("creature_command_max_m", -1.0f);
  // Less than 0 follows far_grab_choice (far_choice_math.h, CommandRangeM).
  if (g_settings.max_m < 0.0f) g_settings.max_m = -1.0f;
  if (g_settings.max_m > 100.0f) g_settings.max_m = 100.0f;
  g_settings.behind_cm = IniInt("creature_command_behind_cm", 30);
  if (g_settings.behind_cm < -1) g_settings.behind_cm = -1;
  if (g_settings.behind_cm > 1000) g_settings.behind_cm = 1000;
  g_settings.double_ms = IniInt("creature_command_double_ms", 500);
  if (g_settings.double_ms < 0) g_settings.double_ms = 0;
  if (g_settings.double_ms > 2000) g_settings.double_ms = 2000;
  g_settings.test_keys = IniInt("test_keys", 0) != 0;
  command_math::ShakeReset(&g_shake);
  if (g_settings.on) {
    void* target = reinterpret_cast<void*>(kLandPress);
    MH_STATUS status = MH_CreateHook(target, &h_LandPress,
                                     reinterpret_cast<void**>(&o_LandPress));
    if (status == MH_OK) status = MH_EnableHook(target);
    g_hooked = status == MH_OK;
    ModLog("hooks        : the press on bare land at 0x%08X %s",
           static_cast<unsigned>(kLandPress), g_hooked ? "ok" : "FAILED");
  }
  ModLog("creature cmd : creature_command=%d. %s. A press on the land of "
         "less than %d ms is a tap, and it sends the Creature to the aim of "
         "the press (creature_command_hold_ms). leash_shake_mps=%.2f: %s%s",
         g_settings.on ? 1 : 0,
         g_settings.on
             ? "While the leash is on the Hand, the trigger commands the "
               "Creature"
             : "The trigger does not command the Creature",
         g_settings.hold_ms, g_settings.shake_mps,
         g_settings.shake_mps > 0.0f
             ? "4 strokes of 6 cm at this mean speed take the leash off"
             : "the shake is off",
         g_settings.test ? ". creature_command_test is set: from that frame "
                           "the leash counts as on, and no packet is sent"
                         : "");
  LogRule("creature_command_rule");
  if (g_settings.double_ms > 0) {
    ModLog("creature cmd : creature_command_double_ms=%d. A second press of "
           "the trigger on the same object, this soon after the first press, "
           "ties the leash to the object (packet 0x5C). While the leash is "
           "tied, a short press on the Creature or a double press on that "
           "object takes the leash back", g_settings.double_ms);
  } else {
    ModLog("creature cmd : creature_command_double_ms=0, so a double press "
           "does not tie the leash");
  }
  if (g_settings.test_keys) {
    ModLog("keys         : Ctrl with numpad 5 changes creature_command_rule "
           "from 4 to 3 to 2 to 1 and back (test_keys=1)");
  }
}

// THE A/B KEY (test_keys=1): Ctrl with the 5 key of the number pad changes
// the rule from 4 to 3 to 2 to 1 and back. The pointing controller gives one
// short pulse for each unit of the value: a hand can count pulses, and it
// cannot tell four lengths apart. The key is read once a frame, and only
// while a window of the game is in front.
const float kRulePulseSeconds = 0.08f;
const double kRulePulseStep = 0.25;  // from the start of a pulse to the next
int g_rule_pulses = 0;
double g_rule_pulse_at = 0.0;

}  // namespace

// The switch of the debug page (debug_menu.h). The change starts the train of
// pulses, and PollRuleKey plays it.
void CreatureCommandRuleChange(const char* by) {
  if (!g_ready) return;
  g_settings.rule = g_settings.rule <= 1 ? 4 : g_settings.rule - 1;
  g_rule_pulses = g_settings.rule;
  g_rule_pulse_at = NowSeconds();
  LogRule(by);
}

int CreatureCommandRuleValue() {
  if (!g_ready || !g_settings.on) return kDebugCannot;
  return g_settings.rule;
}

namespace {

// The train of pulses plays with test_keys=0 too, because the debug page can
// start it.
void PollRuleKey() {
  static bool was = false;
  if (g_settings.test_keys) {
    const bool down = (GetAsyncKeyState(VK_NUMPAD5) & 0x8000) != 0 &&
                      (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool pressed = down && !was;
    was = down;
    if (pressed) {
      DWORD process = 0;
      const HWND window = GetForegroundWindow();
      if (window != nullptr) GetWindowThreadProcessId(window, &process);
      if (process != GetCurrentProcessId()) return;
      CreatureCommandRuleChange("Ctrl and numpad 5");
    }
  }
  const double now = NowSeconds();
  if (g_rule_pulses > 0 && now >= g_rule_pulse_at) {
    --g_rule_pulses;
    g_rule_pulse_at = now + kRulePulseStep;
    ControllersGesturePulse(0.5f, kRulePulseSeconds);
  }
}

// Why a press is not a command, or null. Silent reasons give "". While the
// leash is tied to an object, a press is a command only with the aim on that
// object or on the Creature: it takes the leash back.
const char* Refuse(const Facts& f, bool have) {
  if (!have) return "";
  if (!Leashed(f) && !(Tied(f) && g_target.kind != kTargetNone)) return "";
  if (f.hand_state != kStateNormal) {
    return "the Hand is not in its Normal state";
  }
  if (!f.hand_empty) return "the Hand holds an object";
  if (f.creature != nullptr &&
      (f.under == f.creature || f.next == f.creature)) {
    return "the Hand is on the Creature, so the press is the touch";
  }
  if (TeleportAiming()) return "a teleport runs";
  return nullptr;
}

void AimLand();

// The press kind as a kind of what shows (creature_command_math.h).
int ShownOfPress(PressKind press) {
  switch (press) {
    case command_math::kPressObject:
      return command_math::kShownObject;
    case command_math::kPressCreature:
      return command_math::kShownCreature;
    case command_math::kPressLand:
      return command_math::kShownLand;
    default:
      return command_math::kShownNone;
  }
}

// What the release does.
void Release() {
  const double held = NowSeconds() - g_press_time;
  const double hold_s = g_settings.hold_ms * 0.001;
  Facts f;
  const bool have = Active(&f);
  const bool tied = have && Tied(f);
  // WHAT THE RELEASE ACTS ON. With the live target a tap acts on what showed
  // at the press, and a hold on what shows now. With rule 1 the press fixed
  // its kind, and its object counts only while the aim is still on it.
  Command command = command_math::kCommandNone;
  const Target* target = &g_press_target;
  bool from_press = true;
  bool still_on = true;
  if (g_press == command_math::kPressLand && !g_have_press_aim) AimLand();
  const bool press_land = g_press_aim.have && g_press_aim.valid;
  const bool land_now = g_aim.have && g_aim.valid;
  if (g_press == command_math::kPressNone) {
    // The press was stopped: nothing.
  } else if (LiveRule()) {
    const command_math::LiveRelease live = command_math::ReleaseLive(
        held, hold_s, ShownOfPress(g_press), g_held.shown.kind, press_land,
        land_now);
    command = live.command;
    from_press = live.from_press;
    if (!from_press) target = &g_shown_target;
  } else {
    still_on =
        (g_press == command_math::kPressObject &&
         g_target.kind == kTargetObject &&
         g_target.object == g_press_object) ||
        (g_press == command_math::kPressCreature &&
         g_target.kind == kTargetCreature &&
         g_target.object == g_press_object);
    const command_math::LandAim land = command_math::ReleaseLandAim(
        held, hold_s, press_land, land_now);
    from_press = g_press != command_math::kPressLand ||
                 land == command_math::kLandAimPress;
    command = command_math::ReleaseCommand(
        g_press, held, still_on, land != command_math::kLandAimNone);
  }
  const Aim& aim =
      command == command_math::kCommandLand && from_press ? g_press_aim
                                                          : g_aim;
  const bool tap = held < hold_s;
  const char* why = "";
  if (command != command_math::kCommandNone && !have) {
    command = command_math::kCommandNone;
    why = "the controller does not place the Hand now";
  } else if (tied) {
    // THE LEASH IS TIED TO AN OBJECT. The leash click on the Creature takes
    // it back. A tap on the object that holds it is the first press of a
    // double press. No other command acts.
    if (command == command_math::kCommandObject && target->holds_leash) {
      command = command_math::kCommandNone;
      if (tap) {
        g_tap_object = target->object;
        g_tap_time = g_press_time;
        why = "the leash is tied to this object. A second press takes the "
              "leash back";
      } else {
        why = "the leash is tied to this object, and the press was no tap";
      }
    } else if (command != command_math::kCommandLeash &&
               command != command_math::kCommandNone) {
      command = command_math::kCommandNone;
      why = "the leash is tied to an object, so the trigger sends no command";
    }
  } else if (command != command_math::kCommandNone && !Leashed(f)) {
    command = command_math::kCommandNone;
    why = "the leash is not on the Hand now";
  }
  switch (command) {
    case command_math::kCommandObject: {
      g_press_table = target->table;
      const bool sent = SendObject(f, target->object, target->name);
      if (sent) {
        ++g_sent_object;
        Tick();
        HintCardLearnDone(hint_learn_math::kSkillSend,
                          "the trigger sent the Creature to an object");
      } else {
        ++g_nothing;
      }
      // The first press of a double press: a tap on an object.
      if (tap) {
        g_tap_object = target->object;
        g_tap_time = g_press_time;
      }
      if (Log()) {
        ModLog("creature cmd : the trigger opens after %.0f ms, %s, on the %s "
               "%p. %s, frame %llu", held * 1000.0,
               !LiveRule() ? "with the aim still on its target"
               : from_press ? "a tap, so the target of the press counts"
                            : "a hold, so the target that shows now counts",
               target->name, target->object,
               sent ? "The game sends packet 0x5D: the Creature acts on it"
                    : "The game sends nothing: the Creature is not on this "
                      "leash, its action is 0x5D, or the object is gone",
               g_frame);
      }
      break;
    }
    case command_math::kCommandLeash:
      SendLeash(f);
      ++g_sent_leash;
      Tick();
      if (Log()) {
        ModLog("creature cmd : a short press of %.0f ms with the aim on your "
               "Creature: the leash click (packet 0x5F). %s, frame %llu",
               held * 1000.0,
               tied ? "The leash comes back from the object to the Hand"
                    : "The leash comes off",
               g_frame);
      }
      break;
    case command_math::kCommandLand: {
      const bool sent = SendLand(f, aim.target);
      if (sent) {
        ++g_sent_land;
        if (from_press) ++g_sent_taps;
        Tick();
        HintCardLearnDone(hint_learn_math::kSkillSend,
                          "the trigger sent the Creature to a place");
        // The ring stays on the place for a moment.
        g_sent_draw = g_draw;
        g_sent_draw.active = true;
        g_sent_draw.valid = true;
        g_sent_draw.count = 0;
        g_sent_draw.ring = true;
        for (int k = 0; k < 3; ++k) g_sent_draw.target[k] = aim.target[k];
        g_sent_at = NowSeconds();
      } else {
        ++g_nothing;
      }
      if (Log()) {
        ModLog("creature cmd : the trigger opens after %.0f ms, %s. %s %.1f "
               "%.1f %.1f, frame %llu", held * 1000.0,
               from_press ? "a tap, so the aim of the press counts"
               : LiveRule() && g_held.walk_from_object
                   ? "in the walk mode, so the aim of the release counts"
                   : "so the aim of the release counts",
               sent ? "Packet 0x5E: the Creature goes to"
                    : "No packet (the action of the Creature is 0x5D) for",
               aim.target[0], aim.target[1], aim.target[2], g_frame);
      }
      break;
    }
    default:
      ++g_nothing;
      if (Log()) {
        if (why[0] == 0 && tied) {
          why = "the leash is tied to an object, so the trigger sends no "
                "command";
        }
        if (why[0] == 0) {
          if (LiveRule()) {
            switch (g_press == command_math::kPressNone
                        ? command_math::kShownNone
                        : tap ? ShownOfPress(g_press) : g_held.shown.kind) {
              case command_math::kShownCreature:
                why = g_press == command_math::kPressCreature
                          ? "the press on the Creature was not short"
                          : "the aim is on the Creature, and the press did "
                            "not start on it";
                break;
              case command_math::kShownLand:
                why = g_aim.why;
                break;
              default:
                why = "the press was stopped";
                break;
            }
          } else {
            switch (g_press) {
              case command_math::kPressObject:
              case command_math::kPressCreature:
                why = still_on ? "the press on the Creature was not short"
                               : "the aim left the target";
                break;
              case command_math::kPressLand:
                why = g_aim.why;
                break;
              default:
                why = "the press was stopped";
                break;
            }
          }
        }
        ModLog("creature cmd : the trigger opens after %.0f ms, and nothing "
               "is sent: %s, frame %llu", held * 1000.0, why, g_frame);
      }
      break;
  }
}

// --- the land aim ---------------------------------------------------------------

void RoomToWorld(const TeleportInput& in, const float r[3], float out[3]) {
  for (int i = 0; i < 3; ++i) {
    out[i] = in.base[i] + in.map[i * 3 + 0] * r[0] +
             in.map[i * 3 + 1] * r[1] + in.map[i * 3 + 2] * r[2];
  }
}

// The same filter as the teleport, with its own state.
void SmoothAim(TeleportInput* in, bool reset) {
  const double now = NowSeconds();
  float dt = 1.0f / 90.0f;
  if (!reset && g_filter_time > 0.0) {
    dt = static_cast<float>(now - g_filter_time);
  }
  g_filter_time = now;
  if (reset) {
    aim_smooth::Reset(&g_filter_point);
    aim_smooth::Reset(&g_filter_direction);
  }
  float point[3], direction[3];
  aim_smooth::Step(&g_filter_point, in->origin_m, dt,
                   aim_smooth::kPointMinCutoff, aim_smooth::kPointBeta, point);
  aim_smooth::Step(&g_filter_direction, in->direction, dt,
                   aim_smooth::kDirectionMinCutoff,
                   aim_smooth::kDirectionBeta, direction);
  const float length = sqrtf(direction[0] * direction[0] +
                             direction[1] * direction[1] +
                             direction[2] * direction[2]);
  if (!(length > 1e-3f)) return;
  for (int i = 0; i < 3; ++i) {
    in->origin_m[i] = point[i];
    in->direction[i] = direction[i] / length;
  }
}

// THE LAND AIM of one frame: the arc of the teleport in the God view and in
// the immersive view, a straight ray on the table, and the land point. The
// Creature can go to any land on the map. The sea is not a target.
void EvaluateLand(const TeleportInput& in) {
  g_aim.have = false;
  g_aim.valid = false;
  g_aim.why = "no aim";
  g_draw.active = true;
  g_draw.valid = false;
  g_draw.count = 0;
  g_draw.ring = false;
  g_draw.units_per_meter = in.units_per_meter;
  g_draw.ring_cm = static_cast<float>(g_settings.ring_cm);
  const float* o = in.origin_m;
  const float* d = in.direction;
  if (controller_math::ArcIsSky(d)) {
    g_aim.why = "the aim is the sky";
    return;
  }
  const auto ground = [](float x, float z) { return TeleportGround(x, z); };
  const bool god = TabletopGodOwnsView();
  const bool table = !god && TabletopOwnsView();
  table_math::TablePose pose = {};
  float table_room[3] = {0.0f, 0.0f, 0.0f};
  float low = 0.0f, high = 0.0f;
  uint64_t serial = 0;
  const bool have_pose =
      (god || table) && TabletopPose(&pose, table_room, &low, &high, &serial);
  float hit[3] = {0.0f, 0.0f, 0.0f};
  bool got = false;
  if (table) {
    if (!have_pose) {
      g_aim.why = "the table is not known";
      return;
    }
    const float s = in.units_per_meter;
    float room_hit[3];
    got = table_math::RayLand(pose, table_room, o, d,
                              s * 0.02f > 2.0f ? s * 0.02f : 2.0f, 3.0f * s,
                              ground, room_hit);
    float end_room[3];
    for (int i = 0; i < 3; ++i) {
      end_room[i] = got ? room_hit[i] : o[i] + d[i] * 3.0f;
    }
    RoomToWorld(in, o, g_draw.points[0]);
    RoomToWorld(in, end_room, g_draw.points[1]);
    g_draw.count = 2;
    if (got) {
      RoomToWorld(in, room_hit, hit);
      hit[1] = TeleportGround(hit[0], hit[2]);
    }
  } else {
    if (god && !have_pose) {
      g_aim.why = "the God view is not known";
      return;
    }
    float sea_room;
    if (god) {
      sea_room = table_math::GroundInRoom(pose, table_room, 0.0f);
    } else {
      sea_room = in.map[4] != 0.0f ? (0.0f - in.base[1]) / in.map[4] : -2.0f;
    }
    const float speed = controller_math::ArcLaunchSpeed(
        controller_math::kArcGravity, static_cast<float>(g_settings.reach_m));
    const float t_end = controller_math::ArcEndTime(
        o, d, speed, controller_math::kArcGravity, sea_room - 0.5f,
        controller_math::kArcMaxSeconds);
    if (!(t_end > 0.02f)) {
      g_aim.why = "the hand is below the sea level";
      return;
    }
    const int n = controller_math::ArcPoints(
        o, d, speed, controller_math::kArcGravity, t_end,
        controller_math::kArcSteps, g_arc_room);
    float world[overlay_math::kArcMaxPoints][3];
    for (int i = 0; i < n; ++i) RoomToWorld(in, g_arc_room[i], world[i]);
    int segment = 0;
    got = controller_math::ArcLandHit(world, n, ground, hit, &segment);
    if (got) {
      const int keep = segment + 1;
      for (int i = 0; i < keep; ++i) {
        for (int k = 0; k < 3; ++k) g_draw.points[i][k] = world[i][k];
      }
      for (int k = 0; k < 3; ++k) g_draw.points[keep][k] = hit[k];
      g_draw.count = keep + 1;
      if (segment == 0 && world[0][1] <= hit[1]) g_draw.count = 0;
    } else {
      for (int i = 0; i < n; ++i) {
        for (int k = 0; k < 3; ++k) g_draw.points[i][k] = world[i][k];
      }
      g_draw.count = n;
    }
  }
  if (!got) {
    g_aim.why = "the arc meets no land";
    return;
  }
  g_aim.have = true;
  for (int k = 0; k < 3; ++k) {
    g_aim.target[k] = hit[k];
    g_draw.target[k] = hit[k];
  }
  g_draw.ring = true;
  if (!(hit[0] >= 0.0f && hit[0] < 5120.0f && hit[2] >= 0.0f &&
        hit[2] < 5120.0f)) {
    g_aim.why = "the target is off the map";
  } else if (!(hit[1] > 0.0f)) {
    g_aim.why = "the target is the sea";
  } else {
    g_aim.valid = true;
    g_aim.why = "the target is land";
  }
  g_draw.valid = g_aim.valid;
}

// The land aim of now, from the smoothed aim pose. The first aim of a press
// is kept for a tap.
void AimLand() {
  TeleportInput in;
  if (ControllersTeleportInput(&in) && in.have_aim) {
    SmoothAim(&in, g_aim_reset);
    g_aim_reset = false;
    EvaluateLand(in);
    ++g_aim_frames;
  } else {
    g_aim.have = false;
    g_aim.valid = false;
    g_aim.why = "the aim pose is not tracked";
  }
  if (!g_have_press_aim) {
    g_have_press_aim = true;
    g_press_aim = g_aim;
  }
}

// --- the shake -------------------------------------------------------------------

// Once a frame: the grip of this frame goes to the detector. A shake acts
// only when the game's own tests pass (0x005CF6AC to 0x005CF733): the Hand is
// empty, the leash is on, and no object holds the leash.
void ShakeFrame(const Facts& f, bool have, bool leashed) {
  if (!(g_settings.shake_mps > 0.0f)) return;
  float reach[3], grip[3], room[9];
  const float life = HeadPoseScale();
  if (!have || !(life > 0.1f) || !ControllerDirectMotion(reach, grip, room)) {
    command_math::ShakeReset(&g_shake);
    return;
  }
  const float p[3] = {grip[0] / life, grip[1] / life, grip[2] / life};
  const double now = NowSeconds();
  command_math::ShakeResult result = {};
  if (!command_math::ShakeStep(&g_shake, g_settings.shake_mps, p, now,
                               &result)) {
    return;
  }
  const char* why = nullptr;
  const char* allowed = "";
  if (!leashed) {
    why = "";  // the common case: no leash to take off
  } else if (!f.hand_empty || f.hand_state != kStateNormal) {
    why = "the Hand is not empty in its Normal state";
  } else if (g_claim) {
    why = "the trigger holds a command";
  } else if (!ControllersFarAllowed(&allowed)) {
    why = allowed;
  }
  if (why != nullptr) {
    ++g_shakes_idle;
    if (why[0] != 0 && Log()) {
      ModLog("creature cmd : a shake of the hand (%d strokes in %.0f ms, the "
             "slowest at %.2f m/s), but the leash stays: %s, frame %llu",
             result.strokes, result.seconds * 1000.0f, result.slowest, why,
             g_frame);
    }
    return;
  }
  ++g_shakes;
  if (!g_settings.test) {
    reinterpret_cast<ClearGesturesFn>(kClearGestures)(f.face, nullptr, 0);
  }
  SendLeash(f);
  g_shake_sent = now;
  g_shake_waits = true;
  HapticsPulse(haptics_math::kTick);
  if (Log()) {
    ModLog("creature cmd : a shake of the hand: %d strokes in %.0f ms, the "
           "slowest at %.2f m/s, the last one %.0f cm long. A stroke needs "
           "%.2f m/s (leash_shake_mps). The leash click (packet 0x5F), frame "
           "%llu", result.strokes, result.seconds * 1000.0f, result.slowest,
           result.length * 100.0f, g_settings.shake_mps, g_frame);
  }
}

// THE PLAYER ASKED FOR A CHANGE OF THE LEASH: a leash click of the trigger, of
// a shake, or of the grip (hand_direct.cpp, at the Creature and from a
// distance). A change of the leash in the next kLeashAskSeconds gives a pulse.
double g_leash_asked = -1e9;
const double kLeashAskSeconds = 1.5;

// The leash as the game has it, once a frame: a pulse when it changes.
void LeashFrame(const Facts& f, bool have) {
  if (!have) {
    g_have_leash = false;
    g_have_leash_object = false;
    return;
  }
  const bool on = f.leash_on;
  const double now = NowSeconds();
  if (g_have_leash && g_leash_was != on) {
    // A pulse when the leash goes on or comes off, but only after the player
    // asked for it: the story can change the leash too.
    if (now - g_leash_asked < kLeashAskSeconds) {
      HapticsPulse(on ? haptics_math::kLeashOn : haptics_math::kLeashOff);
    }
    if (Log()) {
      if (g_shake_waits && !on) {
        ModLog("creature cmd : the leash is off, %.0f ms after the shake, "
               "frame %llu", (now - g_shake_sent) * 1000.0, g_frame);
      } else {
        ModLog("creature cmd : the leash is %s, frame %llu", on ? "on" : "off",
               g_frame);
      }
    }
    if (!on) g_shake_waits = false;
  }
  if (g_shake_waits && on && now - g_shake_sent > 1.0) {
    g_shake_waits = false;
    if (Log()) {
      ModLog("creature cmd : the leash is still on 1 s after the shake, so "
             "the game did not take it off, frame %llu", g_frame);
    }
  }
  g_have_leash = true;
  g_leash_was = on;
  // The object that the leash is tied to, once for each change.
  if (g_have_leash_object && g_leash_object_was != f.leash_object) {
    if (now - g_leash_asked < kLeashAskSeconds) {
      HapticsPulse(haptics_math::kLeashOn);
    }
    if (Log()) {
      if (f.leash_object != nullptr) {
        ModLog("creature cmd : the leash is tied to the %s %p now (the game "
               "has the leash %s, and the Creature %s it as the leash of this "
               "player). The trigger sends no command until the leash is back "
               "on the Hand, frame %llu", HandDirectClass(f.leash_object),
               f.leash_object, on ? "on" : "off",
               f.mine ? "has" : "does not have", g_frame);
      } else {
        ModLog("creature cmd : the leash is not tied to an object now (it "
               "was on %p). The leash is %s, frame %llu", g_leash_object_was,
               on ? "on the Hand" : "off", g_frame);
      }
    }
  }
  g_have_leash_object = true;
  g_leash_object_was = f.leash_object;
}

}  // namespace

// --- for the other files -----------------------------------------------------------

void CreatureCommandLeashAsked() { g_leash_asked = NowSeconds(); }

bool CreatureCommandWantsCone() { return g_ready && g_settings.on && g_live; }

// THE TEST WORD "aimcmd". The nearest object to the Hand that a command can
// act on: not the Creature, 30 cm of the room or more from the Hand's point,
// in the range of the rule, and not hidden by the land. With the test key
// command_test_class only an object of that class counts. While the leash is
// tied, only the object that holds it counts. The point is the middle of the
// upright body, so the aim ray goes through the body before it meets the
// land.
bool CreatureCommandTestPoint(float out[3]) {
  Init();
  static char only[48] = "?";
  if (only[0] == '?') {
    char path[MAX_PATH];
    only[0] = 0;
    if (IniPath(path)) {
      GetPrivateProfileStringA("bwvr", "command_test_class", "", only,
                               sizeof(only), path);
    }
  }
  Facts f;
  float origin[3], direction[3], eye[3], hand[3], units = 0.0f;
  bool by_controller = false;
  if (!Active(&f) || !ControllersFarRay(origin, direction, &units, eye) ||
      !(units > 0.0f) || !HandDirectHandPosition(hand, &by_controller)) {
    return false;
  }
  const bool tied = Tied(f);
  const command_math::AimRule rule = AimRuleNow();
  float best = 1e30f, best_radius = 0.0f, best_height = 0.0f;
  void* best_object = nullptr;
  int valid = 0;
  const int offered = HandDirectConeCount();
  for (int i = 0; i < offered; ++i) {
    void* object = nullptr;
    float at[3];
    if (!HandDirectConeAt(i, &object, at) || object == nullptr ||
        object == f.creature || !ValidTarget(object)) {
      continue;
    }
    if (tied && object != f.leash_object) continue;
    if (only[0] != 0 && strcmp(HandDirectClass(object), only) != 0) continue;
    ++valid;
    const float dx = at[0] - hand[0], dy = at[1] - hand[1],
                dz = at[2] - hand[2];
    const float meters = sqrtf(dx * dx + dy * dy + dz * dz) / units;
    if (meters < 0.3f || meters >= best) continue;
    if (!command_math::AimInRange(
            rule, controller_math::FarFloorMeters(eye, at, units), false)) {
      continue;
    }
    const float radius = HandDirectRadius(object);
    if (!HandDirectSeen(at, radius)) continue;
    best = meters;
    best_object = object;
    best_radius = radius;
    best_height = HeightOf(object, radius);
    out[0] = at[0];
    out[1] = at[1] + 0.5f * best_height;
    out[2] = at[2];
  }
  if (best_object == nullptr) {
    ModLog("creature cmd : test: aimcmd found no object to aim at, of %d "
           "objects that a command can act on among %d of the draw, frame "
           "%llu", valid, offered, g_frame);
    return false;
  }
  ModLog("creature cmd : test: aimcmd aims at the %s %p, %.2f m of the room "
         "from the Hand, at the middle of its body %.1f %.1f %.1f (radius "
         "%.2f, height %.2f). %d objects that a command can act on among %d "
         "of the draw, frame %llu", HandDirectClass(best_object), best_object,
         best, out[0], out[1], out[2], best_radius, best_height, valid,
         offered, g_frame);
  return true;
}

bool CreatureCommandWalkHeld() { return g_claim && g_held.land_stays; }

void CreatureCommandSelect() {
  Target next = {};
  next.kind = kTargetNone;
  const Target last = g_target;
  g_target = next;
  g_select_frame = g_frame;
  if (!g_ready || !g_settings.on || !g_live) return;
  const bool live = LiveRule();
  if (!live && g_claim && g_press != command_math::kPressObject &&
      g_press != command_math::kPressCreature) {
    return;  // rule 1: a press on the land, or a press that was stopped
  }
  // A walk that began on the land stays a walk: no object is a target until
  // the trigger opens, so no halo, no tick and no log line for one.
  if (g_claim && g_held.land_stays) return;
  Facts f;
  if (!Active(&f)) return;
  const bool tied = Tied(f);
  float origin[3], direction[3], eye[3], hand[3], units = 0.0f;
  bool by_controller = false;
  if (!ControllersFarRay(origin, direction, &units, eye) || !(units > 0.0f) ||
      !HandDirectHandPosition(hand, &by_controller)) {
    return;
  }
  const command_math::AimRule rule = AimRuleNow();
  const float select = rule.select_deg, keep = rule.keep_deg;
  // Each object is a valid target of the leash, so a forest in the cone
  // gives many. When the list is full, a better one takes the place of the
  // worst one.
  const int kMost = 128;
  static controller_math::FarScore scores[kMost];
  static Target found[kMost];
  // For the choice (far_choice_math.h): the class of each candidate, and its
  // angle for the order in the first rank.
  static bool firsts[kMost];
  static float nears[kMost];
  int count = 0, current = -1;
  // THE CHOICE OF THE GRAB FROM A DISTANCE serves the command too. The cone
  // of a living thing is the cone of the command, and a living target keeps
  // its rank to the cone that keeps a target. Rule 1 has the cones of the
  // grab from a distance.
  const int choice = FarGrabChoice();
  const bool balls = live && far_choice_math::ChoiceBalls(choice);
  const far_choice_math::StayRule stay_rule = far_choice_math::MakeStayRule(
      choice, live ? select : FarGrabLivingDeg(), FarGrabStay(),
      live ? keep - select : FarGrabStayDeg(), select, keep,
      live ? command_math::kAimSwitchDeg : controller_math::kFarSwitchDeg);
  // THE WALK MODE (rule 4). While a press that began on an object is held,
  // only that object is a target: the press shows it, or it becomes a walk.
  void* only = WalkMode() && g_claim &&
                       (g_press == command_math::kPressObject ||
                        g_press == command_math::kPressCreature)
                   ? g_press_object
                   : nullptr;
  // THE LAND POINT OF THE AIM RAY, found when the first object needs it: how
  // far along the ray the land is. The sea counts as land at 0.
  bool land_known = false;
  float land = -1.0f;
  const auto land_distance = [&]() {
    if (land_known) return land;
    land_known = true;
    if (rule.behind_m < 0.0f) return land;
    const float most =
        (rule.max_m > 0.0f ? 1.5f * rule.max_m + 1.0f : 20.0f) * units;
    float step = 0.03f * units;
    if (step < 1.5f) step = 1.5f;
    land = command_math::RayLandDistance(origin, direction, step, most,
                                         LandHeight);
    return land;
  };
  const auto add = [&](void* object, const float at[3], int kind) {
    // WHILE THE LEASH IS TIED, only the object that holds it and the
    // Creature are targets: a press on them takes the leash back.
    if (tied && kind == kTargetObject && object != f.leash_object) return;
    if (only != nullptr && object != only) return;
    // The object of a held press of the walk mode has the cone that keeps a
    // target, also after one frame in which it was no target.
    const bool is_current =
        (last.kind == kind && last.object == object && !last.at_hand) ||
        object == only;
    // THE RANGE, before each other test, because on the table the draw
    // offers the whole island.
    if (!command_math::AimInRange(
            rule, controller_math::FarFloorMeters(eye, at, units),
            is_current)) {
      ++g_out_range;
      return;
    }
    const float dx = at[0] - hand[0], dy = at[1] - hand[1],
                dz = at[2] - hand[2];
    const float hand_m = sqrtf(dx * dx + dy * dy + dz * dz) / units;
    // NO LEAST DISTANCE. The cone of the grab from a distance leaves out an
    // object less than 35 cm from the Hand's point, because the Hand takes
    // it. A command has no such rule: the trigger sends the Creature to an
    // object at each distance. The score takes 35 cm for a nearer object.
    const float score_m = controller_math::FarScoreMeters(hand_m);
    // A first cut with the point of the object. With rule 2 the body is
    // upright, and its top can be in the aim when its base is not, so the
    // cut is wide.
    const controller_math::FarScore rough = controller_math::FarConeScore(
        origin, direction, at, 0.0f, score_m);
    if (!live && (!rough.valid || rough.te > keep + 4.0f)) return;
    if (live && rough.valid && rough.te > keep + 45.0f) return;
    if (kind == kTargetObject && !ValidTarget(object)) return;
    const float radius = HandDirectRadius(object);
    // THE BODY. Rule 2 measures the aim against the upright body. Rule 1
    // measures it against the point of the object.
    // The body of the Creature is a ball around its body bone.
    const float height = !live                     ? 0.0f
                         : kind == kTargetCreature ? 2.0f * radius
                                                   : HeightOf(object, radius);
    // THE BALL (far_grab_choice 3 and 4): the angle to the middle of the
    // ball, less its size. The ball needs the class before the measure. The
    // Creature keeps its own ball around its body bone.
    const bool ball = balls && kind != kTargetCreature;
    bool living = ball && HandDirectLiving(object);
    command_math::BodyAim aim;
    float near_deg = 0.0f;
    if (ball) {
      float middle[3];
      far_choice_math::BallMiddle(at, height, living, middle);
      aim = command_math::AimAtBody(origin, direction, middle, 0.0f, 0.0f);
      near_deg = aim.te;
      const float size =
          living ? FarGrabBallLivingDeg() : FarGrabBallOtherDeg();
      aim.te = aim.te > size ? aim.te - size : 0.0f;
    } else {
      // With radius 0, te is the angle to the nearest point of the line of
      // the body, which is the order in the first rank. The radius then
      // covers its angle at that point, as AimAtBody has it.
      aim = command_math::AimAtBody(origin, direction, at, height, 0.0f);
      near_deg = aim.te;
      if (aim.valid) {
        const float covered =
            atanf((radius > 0.0f ? radius : 0.0f) / aim.distance) * 57.29578f;
        aim.te = aim.te > covered ? aim.te - covered : 0.0f;
      }
    }
    if (!aim.valid || aim.te > keep) return;
    // THE LAND IN FRONT WINS.
    if (command_math::AimLandWins(rule, aim.along - radius, land_distance(),
                                  units, is_current)) {
      ++g_out_land;
      return;
    }
    // The class, for a candidate in the cone.
    if (!ball) living = HandDirectLiving(object);
    controller_math::FarScore s;
    s.valid = true;
    s.te = aim.te;
    s.score = aim.te +
              controller_math::kFarDistanceDeg * log2f(1.0f + score_m);
    int slot = count;
    if (count >= kMost) {
      slot = 0;
      for (int i = 1; i < kMost; ++i) {
        if (scores[i].score > scores[slot].score) slot = i;
      }
      // With ranks, a living thing in its cone takes the place of the worst
      // thing that is not living, also when its plain score is not lower.
      int other = -1;
      if (stay_rule.ranks && living && s.te <= stay_rule.first_deg) {
        for (int i = 0; i < kMost; ++i) {
          if (firsts[i] || i == current) continue;
          if (other < 0 || scores[i].score > scores[other].score) other = i;
        }
      }
      if (other >= 0) {
        slot = other;
      } else if (!(s.score < scores[slot].score) || slot == current) {
        return;
      }
    } else {
      ++count;
    }
    Target& t = found[slot];
    t.kind = kind;
    t.object = object;
    for (int k = 0; k < 3; ++k) t.position[k] = at[k];
    t.radius = radius;
    t.height = height;
    t.te = s.te;
    t.at_hand = false;
    t.can_tie = false;
    t.holds_leash = false;
    t.table = *static_cast<void* const*>(object);
    t.name[0] = 0;
    scores[slot] = s;
    firsts[slot] = living;
    nears[slot] = near_deg;
    if (is_current) current = slot;
  };
  // AN OBJECT AT THE HAND. The object that a press of the grip acts on is
  // within hand_pick_cm of the Hand's point, and the aim ray starts beside it
  // or past it. That object is the target: the trigger sends the Creature to
  // the object that the Hand is on, as the game's own press on it does
  // (fn_005D3930).
  int at_hand = -1;
  {
    void* object = nullptr;
    float at[3];
    if (HandDirectNearObject(&object, at) && object != f.creature &&
        ValidTarget(object) && (!tied || object == f.leash_object) &&
        (only == nullptr || object == only)) {
      at_hand = count;
      Target& t = found[count];
      t.kind = kTargetObject;
      t.object = object;
      for (int k = 0; k < 3; ++k) t.position[k] = at[k];
      t.radius = HandDirectRadius(object);
      t.height = 0.0f;
      t.te = 0.0f;
      t.at_hand = true;
      t.can_tie = false;
      t.holds_leash = false;
      t.table = *static_cast<void* const*>(object);
      t.name[0] = 0;
      scores[count].valid = true;
      scores[count].te = 0.0f;
      scores[count].score = -1000.0f;
      firsts[count] = false;
      nears[count] = 0.0f;
      if (last.kind == kTargetObject && last.object == object) current = count;
      ++count;
    }
  }
  // The player's own Creature: its body, as a ball of its 2D radius. With
  // rule 2, a held press takes the Creature only when it started on it: a
  // leash click needs the press and the release on the Creature, and a ring
  // that the player moves past the Creature must stay a ring.
  if (f.creature != nullptr && !g_settings.test &&
      (!live || !g_claim || g_press == command_math::kPressCreature)) {
    float body[3];
    if (HandDirectCreatureBody(f.creature, body)) {
      const float radius = HandDirectRadius(f.creature);
      const float base[3] = {body[0], body[1] - radius, body[2]};
      add(f.creature, base, kTargetCreature);
    }
  }
  const int offered = HandDirectConeCount();
  for (int i = 0; i < offered; ++i) {
    void* object = nullptr;
    float at[3];
    if (!HandDirectConeAt(i, &object, at) || object == nullptr ||
        object == f.creature ||
        (at_hand >= 0 && object == found[at_hand].object)) {
      continue;
    }
    add(object, at, kTargetObject);
  }
  int pick = -1;
  if (!live && g_claim) {
    // Rule 1: the press chose its target. It stays while the aim is in the
    // cone that keeps a target.
    for (int i = 0; i < count; ++i) {
      if (found[i].object == g_press_object) pick = i;
    }
  } else if (at_hand >= 0) {
    pick = at_hand;
  } else {
    // The target of the grab from a distance is the target of the command
    // too, when it is in the cone of the command. Then the player sees one
    // halo.
    void* far_target = FarGrabTargetObject();
    int shared = -1;
    for (int i = 0; far_target != nullptr && i < count; ++i) {
      if (found[i].object == far_target &&
          (scores[i].te <= select || i == current)) {
        shared = i;
      }
    }
    // The choice of the command itself: the ranks and the target that stays
    // (far_choice_math.h, PickRanked), and the winner must be in view.
    int why = far_choice_math::kWhyPlain;
    for (int tries = 0; tries < 8; ++tries) {
      pick = far_choice_math::PickRanked(scores, nears, firsts, count, current,
                                         stay_rule, &why);
      if (pick < 0) break;
      if (HandDirectSeen(found[pick].position, found[pick].radius)) break;
      scores[pick].valid = false;
      pick = -1;
    }
    // The shared target wins over the plain order of the scores. It does not
    // win over a living thing that has the target by its rank or by the stay
    // rule, unless it is a living thing too. So a tree that the grip can
    // fetch does not take the command from a villager.
    if (shared >= 0 &&
        (pick < 0 || why == far_choice_math::kWhyPlain || firsts[shared])) {
      pick = shared;
    }
  }
  if (pick < 0) return;
  g_target = found[pick];
  strcpy_s(g_target.name, g_target.kind == kTargetCreature
                              ? "Creature"
                              : HandDirectClass(g_target.object));
  if (g_target.kind == kTargetObject) {
    g_target.holds_leash = tied && g_target.object == f.leash_object;
    g_target.can_tie = !tied && g_settings.double_ms > 0 &&
                       (g_settings.test || (f.mine && CanTie(f, g_target.object)));
  }
  if (last.kind != g_target.kind || last.object != g_target.object) {
    ++g_targets;
    // The grab from a distance gives the tick for its own target. A held
    // trigger gives its tick when the target shows (CreatureCommandFrame).
    if (FarGrabTargetObject() != g_target.object && !(live && g_claim)) {
      HapticsPulse(haptics_math::kTarget);
    }
    if (Log()) {
      const char* does =
          g_target.kind == kTargetCreature
              ? (tied ? "A short press of the trigger takes the leash back"
                      : "A short press of the trigger is the leash click")
          : g_target.holds_leash
              ? "The leash is tied to it. A double press of the trigger "
                "takes the leash back"
          : g_target.can_tie
              ? "A press of the trigger sends the Creature to it, and a "
                "second press ties the leash to it"
              : "A press of the trigger sends the Creature to it";
      if (g_target.at_hand) {
        ModLog("creature cmd : the Hand is on the %s %p, at %.1f %.1f %.1f. "
               "%s, frame %llu", g_target.name, g_target.object,
               g_target.position[0], g_target.position[1],
               g_target.position[2], does, g_frame);
      } else {
        const float dx = g_target.position[0] - hand[0],
                    dy = g_target.position[1] - hand[1],
                    dz = g_target.position[2] - hand[2];
        ModLog("creature cmd : the aim is on the %s %p, %.1f degrees off the "
               "ray, %.2f m of the room from the Hand and %.2f m from the "
               "head, at %.1f %.1f %.1f. The land point of the ray is %.2f m "
               "along it (less than 0: not known). %s, frame %llu",
               g_target.name, g_target.object, g_target.te,
               sqrtf(dx * dx + dy * dy + dz * dz) / units,
               controller_math::FarFloorMeters(eye, g_target.position, units),
               g_target.position[0], g_target.position[1],
               g_target.position[2], land_known ? land / units : -1.0f, does,
               g_frame);
      }
    }
  }
}

bool CreatureCommandPumpTrigger(bool trigger, bool ok) {
  Init();
  const bool was = g_trigger;
  g_trigger = trigger;
  if (!g_settings.on) {
    g_claim = false;
    g_press = command_math::kPressNone;
    return false;
  }
  if (trigger && !was) {
    g_claim = false;
    g_press = command_math::kPressNone;
    g_press_done = false;
    Facts f;
    const bool have = ok && Active(&f);
    const char* why = ok ? Refuse(f, have) : "";
    if (why == nullptr) {
      const bool tied = Tied(f);
      g_claim = true;
      g_press_time = NowSeconds();
      g_press_target = g_target;
      g_shown_target = g_target;
      g_press_object = g_target.object;
      g_press_table = g_target.table;
      g_press = g_target.kind == kTargetObject
                    ? command_math::kPressObject
                    : (g_target.kind == kTargetCreature
                           ? command_math::kPressCreature
                           : command_math::kPressLand);
      {
        command_math::Shown shown = {ShownOfPress(g_press), g_target.object};
        command_math::HeldStart(
            &g_held, shown,
            LandStays() && g_press == command_math::kPressLand, WalkMode());
      }
      g_aim.have = false;
      g_aim.valid = false;
      g_aim.why = "no aim yet";
      g_aim_reset = true;
      g_have_press_aim = false;
      g_sent_at = 0.0;
      ++g_presses;
      // THE DOUBLE PRESS: a second press on the object of the last tap. With
      // the leash on the Hand it ties the leash to the object. With the
      // leash tied to this object it takes the leash back. The press did its
      // work, so its release does nothing.
      if (g_press == command_math::kPressObject &&
          command_math::DoublePress(g_press_time, g_tap_time,
                                    g_settings.double_ms * 0.001,
                                    g_target.object, g_tap_object)) {
        const double gap_ms = (g_press_time - g_tap_time) * 1000.0;
        g_tap_object = nullptr;
        if (tied && g_target.holds_leash) {
          const bool sent = TakeLeashBack(f);
          if (sent) {
            ++g_takes_back;
            Tick();
          }
          g_press = command_math::kPressNone;
          g_press_done = true;
          if (Log()) {
            ModLog("creature cmd : a double press on the %s %p, %.0f ms "
                   "after the first press. The leash is tied to it. %s, "
                   "frame %llu", g_target.name, g_target.object, gap_ms,
                   sent ? "Packet 0x5C with no object: the leash comes back "
                          "to the Hand"
                        : "Nothing is sent: the game has no Creature or no "
                          "tied leash now",
                   g_frame);
          }
          return g_claim;
        }
        if (!tied && g_target.can_tie) {
          const bool sent =
              TieLeash(f, g_target.object, g_target.table, g_target.name);
          if (sent) {
            ++g_ties;
            Tick();
          }
          g_press = command_math::kPressNone;
          g_press_done = true;
          if (Log()) {
            ModLog("creature cmd : a double press on the %s %p, %.0f ms "
                   "after the first press. %s, frame %llu", g_target.name,
                   g_target.object, gap_ms,
                   sent ? "Packet 0x5C: the game ties the leash to it"
                        : "Nothing is sent: the leash cannot go on this "
                          "object now",
                   g_frame);
          }
          return g_claim;
        }
      }
      // The aim of the press, now: a tap sends the Creature to this point.
      if (g_press == command_math::kPressLand) AimLand();
      if (Log()) {
        if (g_press == command_math::kPressLand) {
          if (g_press_aim.have) {
            ModLog("creature cmd : the trigger closes with the aim on no "
                   "object. It does not go to the game. The arc of the press "
                   "meets %.1f %.1f %.1f (%s), frame %llu",
                   g_press_aim.target[0], g_press_aim.target[1],
                   g_press_aim.target[2], g_press_aim.why, g_frame);
          } else {
            ModLog("creature cmd : the trigger closes with the aim on no "
                   "object. It does not go to the game. The arc of the press "
                   "has no land point: %s, frame %llu", g_press_aim.why,
                   g_frame);
          }
        } else {
          ModLog("creature cmd : the trigger closes with the aim on the %s "
                 "%p. It does not go to the game, frame %llu", g_target.name,
                 g_target.object, g_frame);
        }
      }
    } else if (why[0] != 0) {
      ++g_passed;
      if (g_refuse_logs < 40) {
        ++g_refuse_logs;
        ModLog("creature cmd : the trigger closes, and it goes to the game: "
               "%s, frame %llu", why, g_frame);
      }
    }
  } else if (!trigger && was) {
    if (g_claim && !g_press_done) Release();
    g_claim = false;
    g_press_done = false;
    g_press = command_math::kPressNone;
  } else if (trigger && g_claim && !ok &&
             g_press != command_math::kPressNone) {
    // The hand cannot act: a menu, a teleport, a grab. The press is
    // stopped, and the trigger stays away from the game until it opens.
    g_press = command_math::kPressNone;
    if (Log()) {
      ModLog("creature cmd : the press is stopped: the hand cannot command "
             "now, frame %llu", g_frame);
    }
  }
  return g_claim;
}

void CreatureCommandFrame() {
  Init();
  ++g_frame;
  PollRuleKey();
  g_draw.active = false;
  g_halo.active = false;
  g_live = false;
  g_leashed = false;
  g_tied = false;
  if (!g_settings.on) return;
  // THE MARK of the last send to the land. A new press on the land takes its
  // place below.
  if (command_math::SentMarkShows(g_sent_at, NowSeconds())) {
    g_draw = g_sent_draw;
  }
  Facts f;
  const bool have = Active(&f);
  const bool leashed = have && Leashed(f);
  const bool tied = have && Tied(f);
  g_leashed = leashed;
  g_tied = tied;
  LeashFrame(f, have);
  ShakeFrame(f, have, leashed);

  // THE CONE is wanted while a command can start, and while a press runs.
  const char* why = "";
  if (!leashed && !tied) {
    g_live_why = "the leash is not on the Hand";
  } else if (f.hand_state != kStateNormal || !f.hand_empty) {
    g_live_why = "the Hand is not empty in its Normal state";
  } else if (g_claim) {
    g_live = g_press != command_math::kPressNone;
    g_live_why = "a press runs";
  } else if (!ControllersFarAllowed(&why)) {
    g_live_why = why;
  } else {
    g_live = true;
    g_live_why = "";
  }
  // No selection ran in the last frames (the controller did not place the
  // Hand in an eye pass), so the target is stale.
  if (g_frame > g_select_frame + 2) g_target.kind = kTargetNone;
  if (!g_live) {
    g_target.kind = kTargetNone;
    return;
  }

  // The drawn hand points while a press of a command is held.
  if (g_claim) {
    ControllerHintState hint = {};
    ControllersHintState(&hint);
    OverlayHandPoints(hint.left_handed ? 0 : 1);
  }

  const Target* halo = &g_target;
  if (g_claim && LiveRule()) {
    // THE LIVE TARGET. What the hand points at now is what shows: the halo
    // of an object, or the arc and the ring on the land. HeldStep has the
    // rule (creature_command_math.h).
    command_math::Shown candidate = {command_math::kShownNone, nullptr};
    if (g_target.kind == kTargetObject) {
      candidate.kind = command_math::kShownObject;
      candidate.object = g_target.object;
    } else if (g_target.kind == kTargetCreature) {
      candidate.kind = command_math::kShownCreature;
      candidate.object = g_target.object;
    }
    const command_math::Shown before = g_held.shown;
    const command_math::Shown shown =
        command_math::HeldStep(&g_held, candidate, NowSeconds());
    // The object that shows is the object in the aim, so the target of this
    // frame is its target. With the walk mode the object of the press shows
    // for 100 ms after the aim left it, and then it keeps its last target.
    if (shown.kind != command_math::kShownLand &&
        g_target.kind != kTargetNone && g_target.object == shown.object) {
      g_shown_target = g_target;
    }
    if (shown.kind != before.kind || shown.object != before.object) {
      ++g_shown_changes;
      const bool walk = shown.kind == command_math::kShownLand &&
                        g_held.walk_from_object;
      if (shown.kind == command_math::kShownLand) {
        // The arc starts from the aim of now, with no smoothing from before.
        g_aim_reset = true;
        // THE WALK MODE starts: one tick, as a step of a control.
        if (walk) {
          ++g_walk_modes;
          HapticsPulse(haptics_math::kTick);
        }
      } else {
        HapticsPulse(haptics_math::kTarget);
      }
      if (Log()) {
        if (walk) {
          ModLog("creature cmd : the trigger is held, and the aim was off "
                 "the %s %p for %.0f ms. The press is a walk now "
                 "(creature_command_rule=4): %s, and no object takes the "
                 "target until the trigger opens, %.0f ms after the press, "
                 "frame %llu", g_shown_target.name, before.object,
                 command_math::kWalkLeaveS * 1000.0,
                 tied ? "the leash is tied, so no arc shows"
                      : "the arc and the ring show the land",
                 (NowSeconds() - g_press_time) * 1000.0, g_frame);
        } else if (shown.kind == command_math::kShownLand) {
          ModLog("creature cmd : the trigger is held, and the aim is on no "
                 "object now (it was on %p). %s, %.0f ms after the press, "
                 "frame %llu", before.object,
                 tied ? "The leash is tied, so no arc shows"
                      : "The arc and the ring show the land",
                 (NowSeconds() - g_press_time) * 1000.0, g_frame);
        } else {
          ModLog("creature cmd : the trigger is held, and the aim is on the "
                 "%s %p now, %.1f degrees off the ray. Its halo shows%s, "
                 "%.0f ms after the press, frame %llu", g_shown_target.name,
                 g_shown_target.object, g_shown_target.te,
                 before.kind == command_math::kShownLand
                     ? ", and the arc is gone"
                     : "",
                 (NowSeconds() - g_press_time) * 1000.0, g_frame);
        }
      }
    }
    if (shown.kind == command_math::kShownLand) {
      // While the leash is tied, the land is no target of the trigger.
      if (!tied) AimLand();
      return;
    }
    halo = &g_shown_target;
  } else if (g_claim && g_press == command_math::kPressLand) {
    // Rule 1: the land aim, while a press on the land is held.
    AimLand();
    return;
  }

  // THE HALO of the target, for both eye passes of this frame.
  if (halo->kind != kTargetNone) {
    float hand[3];
    bool by_controller = false;
    float origin[3], direction[3], eye[3], units = 0.0f;
    if (HandDirectHandPosition(hand, &by_controller) &&
        ControllersFarRay(origin, direction, &units, eye) && units > 0.0f) {
      g_halo.active = true;
      g_halo.valid = true;
      g_halo.locked = false;
      for (int k = 0; k < 3; ++k) {
        g_halo.object[k] = halo->position[k];
        g_halo.hand[k] = hand[k];
      }
      g_halo.radius = halo->radius;
      g_halo.land[0] = halo->position[0];
      g_halo.land[1] = halo->position[2];
      g_halo.units_per_meter = units;
      g_halo_object = halo->object;
    }
  }
}

bool CreatureCommandHalo(FarDraw* out, bool* far_target) {
  if (!g_halo.active) return false;
  *out = g_halo;
  out->target = g_halo_object;
  *far_target = FarGrabTargetObject() == g_halo_object;
  return true;
}

bool CreatureCommandDrawState(TeleportDraw* out) {
  if (!g_draw.active) return false;
  *out = g_draw;
  return true;
}

bool CreatureCommandLeashOn() {
  return g_ready && g_settings.on && g_leashed;
}

// The target that the hint is about: what shows while the trigger is held
// with rule 2, and the target of the aim at each other time.
const Target* HintTarget() {
  static const Target none = {};
  if (!g_claim) return &g_target;
  if (!LiveRule()) return &g_press_target;
  return g_held.shown.kind == command_math::kShownLand ? &none
                                                       : &g_shown_target;
}

int CreatureCommandHint() {
  if (!g_ready || !g_settings.on || !g_live) return kCommandHintNone;
  if (g_claim) {
    if (g_press == command_math::kPressNone) return kCommandHintNone;
    if (!LiveRule()) {
      return g_press == command_math::kPressLand ? kCommandHintAim
                                                 : kCommandHintNone;
    }
    if (g_held.shown.kind == command_math::kShownLand) {
      return g_tied ? kCommandHintNone : kCommandHintAim;
    }
  }
  const Target* t = HintTarget();
  if (t->kind == kTargetObject) {
    return t->holds_leash ? kCommandHintTied : kCommandHintObject;
  }
  if (t->kind == kTargetCreature) {
    return g_tied ? kCommandHintTiedCreature : kCommandHintCreature;
  }
  return g_tied ? kCommandHintNone : kCommandHintLeashed;
}

bool CreatureCommandCanTie() {
  if (!g_ready || !g_settings.on || !g_live) return false;
  const Target* t = HintTarget();
  return t->kind == kTargetObject && t->can_tie;
}

void CreatureCommandReport() {
  if (!g_ready || !g_settings.on) return;
  if (g_presses + g_passed + g_shakes + g_shakes_idle + g_grip_land +
          g_targets == 0) {
    return;
  }
  ModLog("creature cmd : %llu presses of the trigger were commands and %llu "
         "went to the game. Sent: %llu to an object, %llu to the land (%llu "
         "of them taps), %llu leash clicks, and %llu releases sent nothing. "
         "%llu new targets, "
         "%llu frames of the land aim. Shakes: %llu took the leash off, %llu "
         "with no leash to take off. %llu grips on the land did not send the "
         "Creature. The cone is %s%s",
         g_presses, g_passed, g_sent_object, g_sent_land, g_sent_taps,
         g_sent_leash, g_nothing, g_targets, g_aim_frames, g_shakes,
         g_shakes_idle,
         g_grip_land, g_live ? "on" : "off: ", g_live ? "" : g_live_why);
  ModLog("creature cmd : creature_command_rule=%d. While the trigger was "
         "held, what shows changed %llu times, and %llu presses on an object "
         "became a walk. The range left out %llu "
         "objects, and the land in front left out %llu objects of the cone. "
         "%llu double presses tied the leash, and %llu took it back",
         g_settings.rule, g_shown_changes, g_walk_modes, g_out_range,
         g_out_land, g_ties, g_takes_back);
  g_presses = g_passed = g_sent_object = g_sent_land = g_sent_leash = 0;
  g_nothing = g_targets = g_aim_frames = g_shakes = g_shakes_idle = 0;
  g_grip_land = g_sent_taps = 0;
  g_shown_changes = g_out_range = g_out_land = g_ties = g_takes_back = 0;
  g_walk_modes = 0;
}
