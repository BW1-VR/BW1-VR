// The stay of the Creature. See docs/creature.md, "The stay".
//
// WHAT THE PLAYER DOES. The player points the hand at the Creature from a
// distance, closes the grip and keeps it closed. The Creature stops, turns to
// the player and stays. When the grip opens, the Creature is free again and
// goes on by itself. A short grip changes the leash. far_grab.cpp has the
// rule of the two (stay_math::Step).
//
// WHAT THE GAME DOES [V, runblack.exe 1.42]. A press of the Action button on
// the Creature starts the locked select of the game on it, as a press on a
// pile of a store does (docs/grab.md, "The bulk take").
//
// - The start. fn_005D1950 calls slot 0x6D4 of the object and sends packet
//   0x1B with the index of the object. The handler fn_005DA950 calls slot
//   0x6D0 (Creature::NetworkFriendlyStartLockedSelect 0x00476E70), writes the
//   object to GInterfaceStatus+0x3C, and sets bit 0x10 of Object+0x24.
// - The Creature stops. Slot 0x6D0 calls
//   Creature::GoIntoStateOfFinishingAction (0x00477B20). It ends the action of
//   the body (fn_0048F710 calls LH3DCreature::StopMoving), turns the Creature
//   to the camera of the player (LH3DCreature::StartTurningAction), and sets
//   Creature+0x3CC to 1.
// - The Creature stays. While bit 0x10 is set, Creature::ProcessState leaves
//   out the mind of the Creature (the jump at 0x00472F7B to 0x004737DB). It
//   only calls fn_00477D00, which waits for the end of the body action. Then
//   it clears +0x3CC, sets the flag LH3DCreature+0x526C, and starts the
//   action of what the Creature thinks of (fn_00477C50,
//   CreatureMental::GetBestContextToLearnFrom).
// - The Creature looks at the Hand. The Creature state of the Hand gives the
//   Creature a look point on each frame: HandStateCreature::Update sets the
//   global 0x00C64210, calls fn_00482C90 on the LH3DCreature with a point at
//   the Hand, and clears the global (0x005B236D to 0x005B23AB). While a
//   locked select has the Creature, fn_00482C90 takes a point only with that
//   global set, so the mind of the Creature cannot turn its head.
// - The end. At the release, the step of the interface action 4 calls slot
//   0x6E0 (0x005D4858). The step of action 5 waits until slot 0x6E4 gives 1,
//   and then fn_005D2210 calls slot 0x6E8 and sends packet 0x1C. The handler
//   fn_005DAA10 calls slot 0x6EC (LH3DCreature::ReconnectToGame), clears
//   +0x3C and clears bit 0x10.
// - The Creature goes on. The next Creature::ProcessState runs the mind
//   again. With a plan that is not done, it starts the plan again
//   (0x0047340C: Creature::FinishActionUnsuccessfully and
//   CreatureAgenda::ConstructSubActionsFromActionAgenda).
//
// NO LESSON, NO CAMERA, NO LEASH. The stroke, the slap and the lesson (the
// packets 0x27 and 0x59) come from the Creature state of the Hand (state 7).
// The game puts the Hand in that state only in its interface action 4, with
// the Creature in the pick record. The camera of a press on the Creature
// comes from the hand mode 0x13 of that action. The mod sends the two
// packets only. The interface stays in its action 0, so the Hand stays in
// its Normal state: no stroke, no slap, no lesson and no camera. The locked
// select does not read or change the leash.
//
// THE LOOK. With no Creature state, no code of the game gives the Creature a
// look point. So the mod gives it the Hand's point on each frame of a stay,
// with the call that the Creature state makes (creature_stay_look=1).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "creature_stay.h"
#include "creature_stay_math.h"

extern void ModLog(const char* format, ...);

// controllers.cpp: a pulse of a test key.
void ControllersGesturePulse(float amplitude, float seconds);
// hand_direct.cpp: whether an object from an earlier frame still lives.
bool HandDirectObjectLives(void* object, void* table);
// hand_direct.cpp: the Hand's point in the world, and whether the controller
// places the Hand.
bool HandDirectHandPosition(float out[3], bool* by_controller);

namespace {

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
const int kFaceLocked = 0x38;        // bit 8: fn_005D1A00 starts nothing
const int kFaceAction = 0x44;        // the interface action, 0 at rest
const int kFaceStatus = 0x39C;       // the local GInterfaceStatus
// GInterfaceStatus: the object of the locked select, or null.
const int kStatusSelect = 0x3C;
// Object: bit 0x10 while a locked select has the object.
const int kObjectFlags = 0x24;
// Creature.
const int kCreatureLink = 0x160;     // [[+0x160]+0x58] is the LH3DCreature
const int kCreatureMind = 0x164;
const int kCreatureEntering = 0x3CC; // 1 until the body action is over
// The plan of the mind: the desire, the action, and the step of the action.
const int kMindDesire = 0xF50;
const int kMindAction = 0xF60;
const int kMindStep = 0xFB4;
const int kMindSteps = 0xFBC;
// LH3DCreature.
const int kModelLink = 0x58;
const int kModelPlace = 0x78;        // the base of the body, in the world
const int kModelBodyAction = 0x4994; // not 0 while an action of the body runs
const int kModelReady = 0x526C;      // 1 while it is ready for the Hand
// Vtable slots of Object.
const int kIsInteractable = 0x190;
const int kIsCannotBePickedUp = 0x180;
// The slots of the locked select: ValidForLockedSelectProcess (the status),
// NetworkUnfriendlyStartLockedSelect,
// GetReadyForNetworkUnfriendlyEndLockedSelect,
// IsReadyForNetworkUnfriendlyEndLockedSelect and
// NetworkUnfriendlyEndLockedSelect.
const int kValidForSelect = 0x6CC;
const int kStartSelect = 0x6D4;
const int kGetReadyForEnd = 0x6E0;
const int kIsReadyForEnd = 0x6E4;
const int kEndSelect = 0x6E8;
// fn_00550C80, thiscall on GGame (int packet, int index), ret 8, and
// Base::GetIndexOffset, thiscall, no arguments.
const uintptr_t kSetPacketIndex = 0x00550C80;
const uintptr_t kIndexOffset = 0x00436A70;
const int32_t kPacketSelectStart = 0x1B;
const int32_t kPacketSelectEnd = 0x1C;
// THE LOOK POINT of a Creature: fn_00482C90, thiscall on the LH3DCreature (a
// point of the world), ret 4. It writes the point to LH3DCreature+0x48C0.
// With bit 0x10 of the Creature set it does so only while the int at
// 0x00C64210 is not 0.
const uintptr_t kSetLook = 0x00482C90;
const uintptr_t kLookFromHand = 0x00C64210;
const int kModelLook = 0x48C0;

typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);
typedef int(__fastcall* VirtualIntFn)(void* self, void* unused);
typedef int(__fastcall* VirtualIntArgFn)(void* self, void* unused, void* arg);
typedef void(__fastcall* SetPacketIndexFn)(void* game, void* unused,
                                           int32_t packet, int32_t index);
typedef int32_t(__fastcall* IndexOffsetFn)(void* object, void* unused);
typedef void(__fastcall* SetLookFn)(void* model, void* unused,
                                    const float* point);

// --- the keys ---------------------------------------------------------------

bool g_ready = false;
int g_mode = stay_math::kModeHold;   // creature_hold_stay
int g_hold_ms = stay_math::kHoldMs;  // creature_hold_stay_ms
bool g_look = true;                  // creature_stay_look
bool g_test_keys = false;

int IniInt(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return fallback;
  slash[1] = 0;
  strcat_s(path, MAX_PATH, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

void LogMode(const char* from) {
  if (g_mode == stay_math::kModeHold) {
    ModLog("creature stay: %s: creature_hold_stay=1. A grip on your Creature "
           "from a distance that opens in less than %d ms is the leash "
           "click, and the click goes out when the grip opens. A grip that "
           "stays closed makes the Creature stay until the grip opens "
           "(creature_hold_stay_ms, the game's own time is %d ms)", from,
           g_hold_ms, stay_math::kGameHoldMs);
  } else {
    ModLog("creature stay: %s: creature_hold_stay=0. A grip on your Creature "
           "from a distance is the leash click when it closes. No grip makes "
           "the Creature stay", from);
  }
}

void Init() {
  if (g_ready) return;
  g_ready = true;
  g_mode = stay_math::ClampMode(IniInt("creature_hold_stay", 1));
  g_hold_ms = stay_math::ClampHoldMs(
      IniInt("creature_hold_stay_ms", stay_math::kHoldMs));
  g_look = IniInt("creature_stay_look", 1) != 0;
  g_test_keys = IniInt("test_keys", 0) != 0;
  LogMode("creature_hold_stay");
  ModLog("creature stay: creature_stay_look=%d. %s", g_look ? 1 : 0,
         g_look ? "In a stay the Creature looks at the Hand: the mod gives "
                  "it the Hand's point as its look point, as the Creature "
                  "state of the game does"
                : "In a stay the Creature looks ahead");
  if (g_test_keys) {
    ModLog("keys         : Shift with End changes creature_hold_stay between "
           "1 and 0 (test_keys=1)");
  }
}

// --- the state --------------------------------------------------------------

enum State {
  kNone,
  kAsked,    // packet 0x1B went, and the game has not started the select
  kHeld,     // the game has the Creature in its locked select
  kEnding,   // the end is asked for, and packet 0x1C waits for the Creature
  kLeaving,  // packet 0x1C went, and the game has not ended the select
  kWatch,    // the stay is over: the log waits for the Creature to go on
};
int g_state = kNone;
void* g_creature = nullptr;
void* g_table = nullptr;     // the first word of the Creature at the start
double g_since = 0.0;        // the time of the state
double g_start = 0.0;        // the time of packet 0x1B
bool g_seen = false;         // the game had the Creature in its select
bool g_stood = false;        // the Creature stood still in this stay
bool g_looks = false;        // the log has the line of the look of this stay
float g_start_at[2] = {0.0f, 0.0f};  // its place at the start, x and z
float g_stood_at[2] = {0.0f, 0.0f};  // and when it stood still
float g_free_at[2] = {0.0f, 0.0f};   // and when the game let it go
int32_t g_free_plan[3] = {0, 0, 0};  // its desire, action and step then
bool g_plan_changed = false;
double g_plan_changed_ms = 0.0;
// The press of the grip: the Creature and its place at the close.
void* g_press_creature = nullptr;
float g_press_at[2] = {0.0f, 0.0f};
double g_press_time = 0.0;

// The log and the counts of a report window.
const int kMaxLogs = 240;
int g_logs = 0;
uint64_t g_starts = 0;
uint64_t g_refused = 0;
uint64_t g_ends = 0;
uint64_t g_late_ends = 0;  // ends that did not wait for the Creature

bool Log() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

void** VTable(void* object) { return *static_cast<void***>(object); }

// What the game says now.
struct Game {
  uint8_t* game;
  uint8_t* face;
  uint8_t* status;
  void* creature;  // the Creature of the local player, or null
};

bool ReadGame(Game* g) {
  memset(g, 0, sizeof(*g));
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return false;
  g->game = static_cast<uint8_t*>(*slot);
  g->face = static_cast<uint8_t*>(
      reinterpret_cast<MyInterfaceFn>(kMyInterface)(g->game, nullptr));
  if (g->face == nullptr) return false;
  g->status = *reinterpret_cast<uint8_t* const*>(g->face + kFaceStatus);
  if (g->status == nullptr) return false;
  const uint8_t* player =
      g->game + kGamePlayers + kGamePlayerSize * g->game[kGameLocalPlayer];
  g->creature = *reinterpret_cast<void* const*>(player + kPlayerCreature);
  return true;
}

// The facts of a Creature that lives.
struct Facts {
  bool locked;          // bit 0x10: a locked select has it
  int32_t entering;     // Creature+0x3CC
  int32_t desire;
  int32_t action;
  uint32_t step;
  uint32_t steps;
  int32_t body_action;  // LH3DCreature+0x4994
  int32_t ready;        // LH3DCreature+0x526C
  float at[2];          // the base of its body in the world, x and z
  float height;         // and its y
  uint8_t* model;       // the LH3DCreature
};

bool ReadFacts(void* creature, Facts* f) {
  memset(f, 0, sizeof(*f));
  f->desire = f->action = -1;
  if (creature == nullptr || IsBadReadPtr(creature, kCreatureEntering + 4)) {
    return false;
  }
  const uint8_t* c = static_cast<const uint8_t*>(creature);
  f->locked = (c[kObjectFlags] & 0x10) != 0;
  f->entering = *reinterpret_cast<const int32_t*>(c + kCreatureEntering);
  const uint8_t* mind = *reinterpret_cast<uint8_t* const*>(c + kCreatureMind);
  if (mind != nullptr && !IsBadReadPtr(mind + kMindDesire, 0x70)) {
    f->desire = *reinterpret_cast<const int32_t*>(mind + kMindDesire);
    f->action = *reinterpret_cast<const int32_t*>(mind + kMindAction);
    f->step = *reinterpret_cast<const uint32_t*>(mind + kMindStep);
    f->steps = *reinterpret_cast<const uint32_t*>(mind + kMindSteps);
  }
  const uint8_t* link = *reinterpret_cast<uint8_t* const*>(c + kCreatureLink);
  if (link == nullptr || IsBadReadPtr(link + kModelLink, sizeof(void*))) {
    return false;
  }
  uint8_t* model = *reinterpret_cast<uint8_t* const*>(link + kModelLink);
  if (model == nullptr || IsBadReadPtr(model, kModelReady + 4)) return false;
  const float* place = reinterpret_cast<const float*>(model + kModelPlace);
  f->model = model;
  f->at[0] = place[0];
  f->at[1] = place[2];
  f->height = place[1];
  f->body_action = *reinterpret_cast<const int32_t*>(model + kModelBodyAction);
  f->ready = *reinterpret_cast<const int32_t*>(model + kModelReady);
  return true;
}

int Slot(void* object, int slot) {
  return reinterpret_cast<VirtualIntFn>(VTable(object)[slot / 4])(object,
                                                                  nullptr);
}

void SendSelect(const Game& g, void* creature, int32_t packet) {
  const int32_t index =
      reinterpret_cast<IndexOffsetFn>(kIndexOffset)(creature, nullptr);
  reinterpret_cast<SetPacketIndexFn>(kSetPacketIndex)(g.game, nullptr, packet,
                                                      index);
}

// Why the game would not start its locked select on the Creature now, or
// null. These are the tests of a press on the Creature in the game
// (fn_005D1330 at 0x005D13BB, fn_005D1A00, and the handler fn_005DA950).
const char* Refuse(const Game& g, void* creature) {
  if (creature != g.creature) return "it is not your Creature";
  const uint8_t* c = static_cast<const uint8_t*>(creature);
  if ((c[kObjectFlags] & 0x10) != 0) {
    return "a press of the game has the Creature now";
  }
  if (*reinterpret_cast<void* const*>(g.status + kStatusSelect) != nullptr) {
    return "the Hand takes from a different object now";
  }
  if ((g.face[kFaceLocked] & 8) != 0) {
    return "the interface of the game takes no press on an object now";
  }
  if (Slot(creature, kIsInteractable) == 0) {
    return "the game does not let the Hand act on the Creature now";
  }
  if (reinterpret_cast<VirtualIntArgFn>(
          VTable(creature)[kValidForSelect / 4])(creature, nullptr,
                                                 g.status) != 1) {
    return "the game does not let the Hand hold the Creature now (its test "
           "ValidForLockedSelectProcess: a miracle is on the Creature, or "
           "the Creature starts an action that the game does not stop)";
  }
  if (Slot(creature, kIsCannotBePickedUp) != 0) {
    return "the game says that the Hand cannot take the Creature now";
  }
  return nullptr;
}

void Forget() {
  g_state = kNone;
  g_creature = nullptr;
  g_table = nullptr;
}

// THE CREATURE LOOKS AT THE HAND, on each frame of a stay from the frame in
// which the Creature is ready, as in the Creature state of the game. The end
// of the stay clears the look point (slot 0x6E0), and the game waits until
// the head is back.
void Look(const Facts& f, uint64_t frame) {
  float hand[3];
  bool by_controller = false;
  if (!HandDirectHandPosition(hand, &by_controller) || !by_controller) return;
  int32_t* from_hand = reinterpret_cast<int32_t*>(kLookFromHand);
  const int32_t before = *from_hand;
  *from_hand = 1;
  reinterpret_cast<SetLookFn>(kSetLook)(f.model, nullptr, hand);
  *from_hand = before;
  if (g_looks) return;
  g_looks = true;
  if (!Log()) return;
  const float* look = reinterpret_cast<const float*>(f.model + kModelLook);
  const bool took =
      look[0] == hand[0] && look[1] == hand[1] && look[2] == hand[2];
  const float flat[2] = {hand[0], hand[2]};
  ModLog("creature stay: the Creature looks at the Hand now "
         "(creature_stay_look=1). The mod gives it the Hand's point %.1f "
         "%.1f %.1f as its look point on each frame of the stay. That is "
         "%.1f units from the Creature on the map and %.1f above its base. "
         "The game %s the point, frame %llu", hand[0], hand[1], hand[2],
         stay_math::MapDistance(flat, f.at), hand[1] - f.height,
         took ? "took" : "did NOT take", frame);
}

// The stay is over. The log waits for the Creature to go on.
void Watch(const Facts& f, double now) {
  g_state = kWatch;
  g_since = now;
  g_free_at[0] = f.at[0];
  g_free_at[1] = f.at[1];
  g_free_plan[0] = f.desire;
  g_free_plan[1] = f.action;
  g_free_plan[2] = static_cast<int32_t>(f.step);
  g_plan_changed = false;
}

// THE A/B KEY (test_keys=1): Shift with End, and Ctrl up, changes
// creature_hold_stay between 1 and 0. The pointing controller gives a long
// pulse for 1 and a short pulse for 0. Ctrl with End is a key of the hint
// card. The key is read once a frame, and only while a window of the game is
// in front.
void PollKey() {
  static bool was = false;
  if (!g_test_keys) return;
  const bool down = (GetAsyncKeyState(VK_END) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0;
  const bool pressed = down && !was;
  was = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_mode = g_mode == stay_math::kModeHold ? stay_math::kModeClose
                                          : stay_math::kModeHold;
  ControllersGesturePulse(0.5f,
                          g_mode == stay_math::kModeHold ? 0.24f : 0.08f);
  LogMode("Shift and End");
}

}  // namespace

void CreatureStayInstall() { Init(); }

int CreatureStayMode() {
  Init();
  return g_mode;
}

int CreatureStayHoldMs() {
  Init();
  return g_hold_ms;
}

void CreatureStayPressed(void* creature, double now) {
  Facts f;
  g_press_creature = nullptr;
  if (!ReadFacts(creature, &f)) return;
  g_press_creature = creature;
  g_press_at[0] = f.at[0];
  g_press_at[1] = f.at[1];
  g_press_time = now;
}

bool CreatureStayStart(void* creature, void* table, double now,
                       uint64_t frame) {
  Init();
  // THE GRIP CLOSED AGAIN before the end packet of the last stay went. The
  // game still has the Creature, so that stay goes on.
  if (g_state == kEnding && creature == g_creature &&
      HandDirectObjectLives(creature, g_table)) {
    g_state = kHeld;
    g_since = now;
    if (Log()) {
      ModLog("creature stay: the grip stayed closed on your Creature %p "
             "again, before the end of the last stay went to the game. The "
             "game still has the Creature, so the stay goes on, frame %llu",
             creature, frame);
    }
    return true;
  }
  Game g;
  Facts f;
  const char* why = nullptr;
  if (!HandDirectObjectLives(creature, table)) {
    why = "the game deleted the Creature";
  } else if (g_state == kLeaving && creature == g_creature) {
    why = "the last stay ends now";
  } else if (!ReadGame(&g)) {
    why = "the game has no interface now";
  } else {
    why = Refuse(g, creature);
    if (why == nullptr && !ReadFacts(creature, &f)) {
      why = "the mod cannot read the body of the Creature";
    }
  }
  if (why != nullptr) {
    ++g_refused;
    if (Log()) {
      ModLog("creature stay: the grip stayed closed on your Creature %p, and "
             "the Creature does not stay: %s. The mod sends nothing, and the "
             "leash does not change, frame %llu", creature, why, frame);
    }
    return false;
  }
  const bool pressed = g_press_creature == creature;
  const double press_ms = pressed ? (now - g_press_time) * 1000.0 : 0.0;
  const float moved =
      pressed ? stay_math::MapDistance(f.at, g_press_at) : 0.0f;
  // As fn_005D1950 does: slot 0x6D4, then the packet. The interface action
  // does not change.
  Slot(creature, kStartSelect);
  SendSelect(g, creature, kPacketSelectStart);
  ++g_starts;
  g_state = kAsked;
  g_creature = creature;
  g_table = table;
  g_since = now;
  g_start = now;
  g_seen = false;
  g_stood = false;
  g_looks = false;
  g_start_at[0] = g_stood_at[0] = f.at[0];
  g_start_at[1] = g_stood_at[1] = f.at[1];
  if (Log()) {
    ModLog("creature stay: the grip stayed closed on your Creature %p for "
           "%.0f ms, so the mod asks the game for the stay "
           "(creature_hold_stay_ms=%d). "
           "Before the stay: the Creature is at %.2f %.2f, and it moved %.2f "
           "units while the grip was closed. Its plan is desire %d, action "
           "%d, step %u of %u, and the action of its body is %d. Packet "
           "0x1B: the locked select of the game starts on it. No press goes "
           "to the game, and the leash does not change, frame %llu",
           creature, press_ms, g_hold_ms, f.at[0], f.at[1], moved, f.desire,
           f.action, f.step, f.steps, f.body_action, frame);
  }
  return true;
}

void CreatureStayEnd(const char* why, double now, uint64_t frame) {
  if (g_state != kAsked && g_state != kHeld) return;
  if (!HandDirectObjectLives(g_creature, g_table)) {
    CreatureStayGone();
    return;
  }
  Facts f;
  ReadFacts(g_creature, &f);
  // As the step of the game's interface action 4 does at the release
  // (0x005D4858): the Creature gets ready for the end.
  Slot(g_creature, kGetReadyForEnd);
  ++g_ends;
  if (Log()) {
    ModLog("creature stay: %s after %.0f ms of the stay, so the stay ends. "
           "The Creature is at %.2f %.2f: it moved %.2f units %s. The game "
           "%s. The end goes to the game when the Creature is ready for it, "
           "frame %llu", why != nullptr ? why : "the grip opened",
           (now - g_start) * 1000.0, f.at[0], f.at[1],
           stay_math::MapDistance(f.at, g_stood ? g_stood_at : g_start_at),
           g_stood ? "after it stood still" : "after the start of the stay",
           f.locked ? "has it in its locked select"
                    : "does not have it in its locked select now",
           frame);
  }
  g_state = kEnding;
  g_since = now;
}

void CreatureStayGone() {
  if (g_state == kNone) return;
  if (Log()) {
    ModLog("creature stay: the game deleted the Creature %p in a stay. The "
           "mod sends nothing more and does not read it again", g_creature);
  }
  Forget();
}

bool CreatureStayHolds() { return g_state == kAsked || g_state == kHeld; }

void CreatureStayFrame(double now, uint64_t frame) {
  Init();
  PollKey();
  if (g_state == kNone) return;
  if (!HandDirectObjectLives(g_creature, g_table)) {
    CreatureStayGone();
    return;
  }
  Facts f;
  Game g;
  if (!ReadFacts(g_creature, &f) || !ReadGame(&g)) return;
  const bool selected =
      f.locked &&
      *reinterpret_cast<void* const*>(g.status + kStatusSelect) == g_creature;
  const double ms = (now - g_since) * 1000.0;
  switch (g_state) {
    case kAsked:
      if (selected) {
        g_state = kHeld;
        g_since = now;
        g_seen = true;
        if (Log()) {
          ModLog("creature stay: the game has the Creature in its locked "
                 "select, %.0f ms after the packet. Its mind stops for the "
                 "time of the stay. The interface action of the game is %d, "
                 "so the Hand does not go to its Creature state, frame %llu",
                 ms,
                 *reinterpret_cast<const int32_t*>(g.face + kFaceAction),
                 frame);
        }
      } else if (ms > 2000.0) {
        if (Log()) {
          ModLog("creature stay: the game did not start its locked select "
                 "on the Creature in 2 s after the packet. The mod lets the "
                 "stay go, frame %llu", frame);
        }
        Forget();
      }
      break;
    case kHeld:
      if (!f.locked) {
        if (Log()) {
          ModLog("creature stay: the game ended its locked select on the "
                 "Creature by itself, %.0f ms after the start of the stay. "
                 "The Creature is free, frame %llu",
                 (now - g_start) * 1000.0, frame);
        }
        Forget();
      } else if (!g_stood && f.entering == 0) {
        g_stood = true;
        g_stood_at[0] = f.at[0];
        g_stood_at[1] = f.at[1];
        if (Log()) {
          ModLog("creature stay: the Creature stands still, %.0f ms after "
                 "the start of the stay: the action of its body is over, and "
                 "it is ready for the Hand (%d). It is at %.2f %.2f, %.2f "
                 "units from its place at the start, frame %llu",
                 (now - g_start) * 1000.0, f.ready, f.at[0], f.at[1],
                 stay_math::MapDistance(f.at, g_start_at), frame);
        }
      }
      if (g_look && g_stood && f.locked) Look(f, frame);
      break;
    case kEnding: {
      if (!f.locked) {
        // The end was asked for before the game started the select: the
        // start packet waits for a game turn. The end goes after the start.
        if (!g_seen && (now - g_start) * 1000.0 <= 2000.0) break;
        if (Log()) {
          ModLog("creature stay: the game %s. The Creature is free, frame "
                 "%llu",
                 g_seen ? "ended its locked select on the Creature before "
                          "the end packet"
                        : "did not start its locked select on the Creature "
                          "in 2 s after the packet",
                 frame);
        }
        Watch(f, now);
        break;
      }
      g_seen = true;
      const bool ready = Slot(g_creature, kIsReadyForEnd) == 1;
      if (!stay_math::EndNow(ready, ms / 1000.0)) break;
      if (!ready) ++g_late_ends;
      // As fn_005D2210 does: slot 0x6E8, then the packet.
      Slot(g_creature, kEndSelect);
      SendSelect(g, g_creature, kPacketSelectEnd);
      if (Log()) {
        ModLog("creature stay: packet 0x1C goes, %.0f ms after the end was "
               "asked for: %s. The game ends its locked select, frame %llu",
               ms,
               ready ? "the Creature is ready for the end (its look is at "
                       "its place, and no action of its body or its face "
                       "runs)"
                     : "the Creature is not ready, and the mod does not "
                       "wait longer",
               frame);
      }
      g_state = kLeaving;
      g_since = now;
      break;
    }
    case kLeaving:
      if (f.locked && ms <= 2000.0) break;
      if (Log()) {
        if (f.locked) {
          ModLog("creature stay: the game still has the Creature in its "
                 "locked select 2 s after packet 0x1C, frame %llu", frame);
        } else {
          ModLog("creature stay: the game let the Creature go, %.0f ms "
                 "after the packet. Its mind runs again. It is at %.2f "
                 "%.2f, and its plan is desire %d, action %d, step %u of "
                 "%u, frame %llu", ms, f.at[0], f.at[1], f.desire, f.action,
                 f.step, f.steps, frame);
        }
      }
      Watch(f, now);
      break;
    case kWatch: {
      // FOR THE LOG ONLY: one line when the Creature goes on.
      const bool changed = f.desire != g_free_plan[0] ||
                           f.action != g_free_plan[1] ||
                           static_cast<int32_t>(f.step) != g_free_plan[2];
      if (changed && !g_plan_changed) {
        g_plan_changed = true;
        g_plan_changed_ms = ms;
      }
      const float moved = stay_math::MapDistance(f.at, g_free_at);
      const bool goes = stay_math::GoesOn(moved);
      if (!stay_math::WatchDone(moved, ms / 1000.0)) break;
      if (Log()) {
        char plan[96];
        if (g_plan_changed) {
          sprintf_s(plan, "changed %.0f ms after the stay",
                    g_plan_changed_ms);
        } else {
          strcpy_s(plan, "did not change");
        }
        ModLog("creature stay: %s: %.0f ms after the stay it is at %.2f "
               "%.2f, %.2f units from the place of its stay. Its plan %s: "
               "desire %d, action %d, step %u of %u, frame %llu",
               goes ? "the Creature goes on"
                    : "the Creature did not leave the place of its stay",
               ms, f.at[0], f.at[1], moved, plan, f.desire, f.action, f.step,
               f.steps, frame);
      }
      Forget();
      break;
    }
    default:
      break;
  }
}

void CreatureStayReport() {
  if (g_starts == 0 && g_refused == 0 && g_ends == 0) return;
  ModLog("creature stay: %llu stays started, %llu refused by the tests of "
         "the game, %llu ended (%llu of them with a Creature that was not "
         "ready in %.0f s), this window", g_starts, g_refused, g_ends,
         g_late_ends, stay_math::kEndWaitS);
  g_starts = g_refused = g_ends = g_late_ends = 0;
}
