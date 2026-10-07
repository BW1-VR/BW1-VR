// The take from a forest (docs/hand.md, "Many trees from a forest").
//
// WHAT THE GAME DOES [V]. The Hand of the plain game holds one object:
// GMagicHand has room for one (0x005FAFC0 refuses a second), and
// GInterfaceStatus::IsSpaceInHands 0x005DC2F0 says so. A press on a Tree
// starts the Tug and puts that tree in the Hand. A press on a BigForest makes
// one new Tree and puts it in the Hand (BigForest::InterfaceSetInMagicHand
// 0x004393C0). A Tree and a BigForest give no bulk take
// (ValidForLockedSelectProcess is 0), and the hand state MultiPickUp (6) is
// never the state of the Hand: CHand::GetRequiredState 0x0046CD10 has no path
// that gives 6. So the wood of a forest comes one tree at a time, and the
// player must set each tree down before the next one.
//
// WHAT THE MOD ADDS (forest_take=1). While the grip stays closed in a forest,
// tree after tree goes into one bundle of wood in the Hand. The bundle is
// the game's own object for wood in the Hand: the PileWood that a take from
// the wood of a store makes (Pot::Create 0x0066CF10 with pot info 11). A
// tree goes into it as a tree goes into a store: with
// Object::DoDeleteObjectAndTakeResource 0x0063A940, which adds the wood of
// the tree (Tree::GetWoodValue) to the taker and deletes the tree. So the
// wood is the same as for a tree that the player drops on the store, and
// the game shows the amount with its own text ("Amount: N", text 0xEEA).
//
// THE START changes the object in the Hand, in the steps that the game
// itself uses:
//   1. Pot::Create makes the bundle with the wood of the tree in the Hand.
//   2. The tree leaves the Hand (GInterfaceStatus 0x005DC470, and
//      CHand::ThrowObject 0x0046DDD0 on the side of the CHand), and it is
//      deleted as a store deletes it (GoolooGooloo, ToBeDeleted).
//   3. GInterface::PlaceObjectInMagicHand 0x005DA6F0 puts the bundle in the
//      Hand, as it does for the Pot of a pile.
// All of this runs before the interface step of the game, where the game
// puts objects in the Hand too. Never in a multiplayer game: the calls are
// not packets.
//
// THE RULE and the choice of the next tree are in forest_take_math.h.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include "controller_math.h"
#include "far_grab.h"
#include "forest_take.h"
#include "forest_take_math.h"

extern void ModLog(const char* format, ...);

// hand_direct.cpp
int HandDirectForestTrees(ForestCandidate* out, int max);
int HandDirectConeCount();
bool HandDirectConeAt(int index, void** object, float position[3]);
bool HandDirectObjectLives(void* object, void* table);
bool HandDirectRealPoint(float out[3]);
float HandDirectRoomUnits();
bool HandDirectSeen(const float at[3], float radius);
const char* HandDirectClass(void* object);
bool HandDirectIsTree(void* object);
// controllers.cpp
bool ControllersGripClosed();
bool ControllersFarRay(float origin[3], float direction[3], float* units,
                       float eye[3]);
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();

namespace {

// GInterface, CHand, GInterfaceStatus and Object.
const int kFaceStatus = 0x39C;
const int kHandHeld = 0x4904;
const int kThingPosition = 0x14;   // MapCoords of a GameThingWithPos: the
                                   // place of the Hand, or of an object
const int kPotAmount = 0x70;       // an unsigned int
// vtable slots of Object.
const int kToBeDeleted = 0x0C;     // thiscall (int), ret 4
const int kGetResource = 0x98;     // thiscall (RESOURCE_TYPE), ret 4
const int kAddResource = 0x9C;     // thiscall, six arguments, ret 0x18
const int kRemoveResource = 0xA0;  // thiscall, four arguments, ret 0x10
const int kResourceWood = 1;       // RESOURCE_TYPE
// The game's functions.
const uintptr_t kPotCreate = 0x0066CF10;        // cdecl, nine arguments
const uintptr_t kPotInfos = 0x00D4C660;         // GPotInfo, 0x144 bytes each
const int kPotInfoSize = 0x144;
const int kPotInfoPileWood = 11;                // PileWood, 0x0066EC40
const uintptr_t kNearestTown = 0x006020E0;      // thiscall (float), ret 4
const uintptr_t kPlaceInHand = 0x005DA6F0;      // thiscall (Object*), ret 4
const uintptr_t kRemoveFromHands = 0x005DC470;  // thiscall (Object*), ret 4
const uintptr_t kFirstInHand = 0x005DC130;      // thiscall
const uintptr_t kHandLetGo = 0x0046DDD0;        // CHand::ThrowObject, thiscall
const uintptr_t kTakeResource = 0x0063A940;     // thiscall (Object*, status)
const uintptr_t kGooloo = 0x005E6540;           // cdecl (Object*)
const uintptr_t kForceToolTip = 0x005C9C60;     // cdecl (int text, float)
const int kTextAmount = 0xEEA;                  // "Amount: %3.0f"
// The wood of the tree that a BigForest gives: GTreeInfo 0x00DA3E98 + 0x6C,
// as BigForest::InterfaceSetInMagicHand reads it (0x004393C3).
const uintptr_t kForestTreeWood = 0x00DA3F04;

typedef void*(__cdecl* PotCreateFn)(const void* position, const void* info,
                                    uint32_t amount, int zero, void* town,
                                    int a, int b, float scale, int c);
typedef void*(__fastcall* NearestTownFn)(const void* position, void* unused,
                                         float range);
typedef int(__fastcall* ObjectArgFn)(void* self, void* unused, void* object);
typedef void*(__fastcall* SelfFn)(void* self, void* unused);
typedef void(__fastcall* TakeResourceFn)(void* taker, void* unused,
                                         void* object, void* status);
typedef void(__cdecl* GoolooFn)(void* object);
typedef void(__cdecl* ToolTipFn)(int text, float value);
typedef void(__fastcall* DeleteFn)(void* self, void* unused, int now);
typedef uint32_t(__fastcall* GetResourceFn)(void* self, void* unused,
                                            int type);
typedef uint32_t(__fastcall* AddResourceFn)(void* self, void* unused, int type,
                                            uint32_t amount, void* status,
                                            int poisoned, const void* position,
                                            int zero);
typedef uint32_t(__fastcall* RemoveResourceFn)(void* self, void* unused,
                                               int type, uint32_t amount,
                                               void* status, void* flag);

void** VTable(void* object) { return *static_cast<void***>(object); }

// --- the keys --------------------------------------------------------------------

struct Settings {
  bool read;
  bool on;        // forest_take
  int wait_ms;    // forest_take_wait_ms
  int step_ms;    // forest_take_ms
  int reach_cm;   // forest_take_cm
  bool test_keys;
};
Settings g_settings = {false, true, 600, 400, 10, false};
const int kLeaveMs = 300;

int IniKey(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash = length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return fallback;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

int Clamp(int value, int low, int high) {
  return value < low ? low : value > high ? high : value;
}

void SayRule() {
  if (g_settings.on) {
    ModLog("forest take  : forest_take=1. The plain game gives one tree for "
           "each press. With the grip closed on a tree for %d ms and a second "
           "tree within %d cm of the Hand's point, the trees go into one "
           "bundle of wood in the Hand, one tree each %d ms "
           "(forest_take_wait_ms, forest_take_cm, forest_take_ms). Open the "
           "grip to stop and keep the bundle. Close and open it again to let "
           "the bundle go", g_settings.wait_ms, g_settings.reach_cm,
           g_settings.step_ms);
  } else {
    ModLog("forest take  : forest_take=0. A grip on a tree gives one tree, as "
           "in the plain game");
  }
}

void ReadSettings() {
  if (g_settings.read) return;
  g_settings.read = true;
  g_settings.on = IniKey("forest_take", 1) != 0;
  g_settings.wait_ms = Clamp(IniKey("forest_take_wait_ms", 600), 100, 5000);
  g_settings.step_ms = Clamp(IniKey("forest_take_ms", 400), 50, 5000);
  g_settings.reach_cm = Clamp(IniKey("forest_take_cm", 10), 1, 100);
  g_settings.test_keys = IniKey("test_keys", 0) != 0;
  SayRule();
  if (g_settings.test_keys) {
    ModLog("keys         : Ctrl with numpad 9 turns the take from a forest "
           "off and on (test_keys=1)");
  }
}

// THE A/B KEY (test_keys=1): Ctrl with the 9 key of the number pad changes
// forest_take. The pointing controller gives a long pulse for 1 and a short
// pulse for 0. Only while a window of the game is in front.
void PollKey() {
  static bool was = false;
  if (!g_settings.test_keys) return;
  const bool down = (GetAsyncKeyState(VK_NUMPAD9) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool pressed = down && !was;
  was = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_settings.on = !g_settings.on;
  ControllersGesturePulse(0.5f, g_settings.on ? 0.20f : 0.05f);
  SayRule();
}

// --- the state -------------------------------------------------------------------

forest_take::State g_state = {};
bool g_runs = false;
void* g_bundle = nullptr;         // the bundle of this take
void* g_bundle_table = nullptr;   // its first word, for the test that it lives
uint32_t g_bundle_start = 0;      // its wood at the start
bool g_far = false;               // the take started with a fetch
double g_fetch_ms = -1e9;         // a fetch from a distance last ran
double g_tip_ms = 0.0;            // the amount text was last shown
double g_left_ms = 0.0;           // the bundle left the Hand at this time
int g_pile_look = 0;              // frames left to find its pile on the land
const float kMapScale = 6553.6f;  // a MapCoords int is the world unit times this

// The next tree, from the first eye pass of this frame.
struct Next {
  bool valid;
  ForestCandidate tree;
  void* table;
  float measure;  // cm of the room from the Hand's point, or degrees
};
Next g_next = {};
int g_trees_seen = 0;  // trees that the draw offered, for the log

// For the log and the report.
int g_logs = 0;
const int kMaxLogs = 160;
uint64_t g_takes = 0;
uint64_t g_trees = 0;
uint64_t g_refused = 0;
uint64_t g_gone = 0;

double NowMs() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return 1000.0 * static_cast<double>(count.QuadPart) /
         static_cast<double>(frequency.QuadPart);
}

bool LogRoom() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

uint32_t WoodOf(void* object) {
  return reinterpret_cast<GetResourceFn>(VTable(object)[kGetResource / 4])(
      object, nullptr, kResourceWood);
}

uint32_t AmountOf(void* pot) {
  return *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(pot) +
                                            kPotAmount);
}

void ShowAmount(void* pot) {
  reinterpret_cast<ToolTipFn>(kForceToolTip)(
      kTextAmount, static_cast<float>(AmountOf(pot)));
}

void* FirstInHand(void* status) {
  return reinterpret_cast<SelfFn>(kFirstInHand)(status, nullptr);
}

// The next tree goes into the bundle. A Tree goes in whole, as into a store.
// A BigForest gives the wood of one of its trees, as it does for the tree
// that it puts in the Hand.
uint32_t TakeTree(void* pot, void* status, const Next& next) {
  const uint32_t before = AmountOf(pot);
  void* tree = next.tree.object;
  if (next.tree.forest) {
    const uint32_t ask = *reinterpret_cast<const uint32_t*>(kForestTreeWood);
    const uint32_t got = reinterpret_cast<RemoveResourceFn>(
        VTable(tree)[kRemoveResource / 4])(tree, nullptr, kResourceWood, ask,
                                           nullptr, nullptr);
    if (got != 0) {
      reinterpret_cast<AddResourceFn>(VTable(pot)[kAddResource / 4])(
          pot, nullptr, kResourceWood, got, status, 0, nullptr, 0);
    }
  } else {
    reinterpret_cast<TakeResourceFn>(kTakeResource)(pot, nullptr, tree, status);
  }
  return AmountOf(pot) - before;
}

// THE START: the tree in the Hand becomes the bundle. False when the game
// refused a step. Then the Hand keeps its tree.
bool StartBundle(uint8_t* face, uint8_t* hand, void* status, void* held) {
  const uint32_t wood = WoodOf(held);
  if (wood == 0) {
    if (LogRoom()) {
      ModLog("forest take  : the tree %p in the Hand has no wood, so no take "
             "starts, frame %llu", held, ControllersFrameCount());
    }
    return false;
  }
  const uint8_t* position = static_cast<const uint8_t*>(status) + kThingPosition;
  void* town = reinterpret_cast<NearestTownFn>(kNearestTown)(position, nullptr,
                                                             3.4e38f);
  const void* info = reinterpret_cast<const uint8_t*>(kPotInfos) +
                     kPotInfoPileWood * kPotInfoSize;
  void* pot = reinterpret_cast<PotCreateFn>(kPotCreate)(position, info, wood, 0,
                                                        town, 0, 0, 1.0f, 0);
  if (pot == nullptr) {
    if (LogRoom()) {
      ModLog("forest take  : the game made no bundle of wood, so no take "
             "starts, frame %llu", ControllersFrameCount());
    }
    return false;
  }
  // The tree leaves the Hand, and the game deletes it as a store does.
  const int removed = reinterpret_cast<ObjectArgFn>(kRemoveFromHands)(
      status, nullptr, held);
  reinterpret_cast<SelfFn>(kHandLetGo)(hand, nullptr);
  reinterpret_cast<GoolooFn>(kGooloo)(held);
  reinterpret_cast<DeleteFn>(VTable(held)[kToBeDeleted / 4])(held, nullptr, 0);
  // The bundle goes in the Hand.
  reinterpret_cast<ObjectArgFn>(kPlaceInHand)(face, nullptr, pot);
  const bool in_hand = FirstInHand(status) == pot &&
                       *reinterpret_cast<void* const*>(hand + kHandHeld) == pot;
  g_bundle = pot;
  g_bundle_table = *static_cast<void* const*>(pot);
  g_bundle_start = wood;
  if (LogRoom()) {
    char name[48];
    strcpy_s(name, HandDirectClass(pot));
    ModLog("forest take  : the take starts. The tree %p of the Hand (%u wood) "
           "is now the bundle %p (%s) of %u wood. The tree left the hand list "
           "%d, and the bundle is %s, frame %llu", held, wood, pot, name,
           AmountOf(pot), removed,
           in_hand ? "in the Hand" : "NOT in the Hand: it is on the land",
           ControllersFrameCount());
  }
  return in_hand;
}

void SayEnd(const char* why, void* held) {
  ++g_takes;
  if (!LogRoom()) return;
  const bool lives = g_bundle != nullptr && held == g_bundle &&
                     HandDirectObjectLives(g_bundle, g_bundle_table);
  ModLog("forest take  : the take ends: %s. %d trees went into the bundle, "
         "and it holds %u wood. %s, frame %llu", why, g_state.trees,
         lives ? AmountOf(g_bundle) : 0,
         lives ? "The bundle stays in the Hand: close and open the grip to "
                 "let it go"
               : "The bundle is not in the Hand",
         ControllersFrameCount());
}

}  // namespace

bool ForestTakeRuns() { return g_runs; }

// A take that started with a fetch looks along the aim, and so does the wait
// before it, while the tree of a fetch is in the closed grip.
bool ForestTakeWantsCone() {
  return g_settings.on && g_far &&
         (g_state.phase == forest_take::kArmed ||
          g_state.phase == forest_take::kTaking);
}

// At the end of the first eye pass: the next tree, from the trees that the
// draw offered in this frame. The interface step uses it once, and tests
// first that the tree still lives.
void ForestTakeSelect() {
  g_next.valid = false;
  g_trees_seen = 0;
  // The bundle left the Hand, and the game deleted it: the pile of wood that
  // the game made on the land, for the log. Pot::InitialisePhysicsFromHand
  // gives the wood to a store or to a pile near the place (0x0066F270), or it
  // makes a MagicWood there (0x005FA8B0): the game's pile of wood outside a
  // store.
  float hand_point[3];
  if (g_pile_look > 0 && HandDirectRealPoint(hand_point)) {
    --g_pile_look;
    const float units = HandDirectRoomUnits();
    void* pile = nullptr;
    float best = 1e30f;
    for (int i = 0; i < HandDirectConeCount(); ++i) {
      void* object = nullptr;
      float at[3];
      if (!HandDirectConeAt(i, &object, at) ||
          strstr(HandDirectClass(object), "Wood") == nullptr) {
        continue;
      }
      const float dx = at[0] - hand_point[0], dy = at[1] - hand_point[1],
                  dz = at[2] - hand_point[2];
      const float d = sqrtf(dx * dx + dy * dy + dz * dz);
      if (d < best) {
        best = d;
        pile = object;
      }
    }
    if (pile != nullptr || g_pile_look == 0) {
      if (pile != nullptr && LogRoom()) {
        char name[48];
        strcpy_s(name, HandDirectClass(pile));
        ModLog("forest take  : the nearest pile of wood on the land is %p "
               "(%s), with %u wood, %.1f cm of the room from the Hand's "
               "point, frame %llu", pile, name, AmountOf(pile),
               units > 0.0f ? best / units * 100.0f : 0.0f,
               ControllersFrameCount());
      } else if (pile == nullptr && LogRoom()) {
        ModLog("forest take  : the draw offered no pile of wood near the "
               "Hand in 60 frames, frame %llu", ControllersFrameCount());
      }
      g_pile_look = 0;
    }
  }
  if (g_state.phase == forest_take::kIdle ||
      g_state.phase == forest_take::kCarried) {
    return;
  }
  static ForestCandidate trees[512];
  const int count = HandDirectForestTrees(trees, 512);
  g_trees_seen = count;
  float point[3];
  if (count == 0 || !HandDirectRealPoint(point)) return;
  const float units = HandDirectRoomUnits();
  if (!(units > 0.0f)) return;
  float best = 1e30f;
  int pick = -1;
  if (!g_far) {
    const float reach = g_settings.reach_cm * 0.01f * units;
    for (int i = 0; i < count; ++i) {
      if (!trees[i].inside) continue;
      const forest_take::Tree t = {
          {trees[i].base[0], trees[i].base[1], trees[i].base[2]},
          trees[i].radius, trees[i].height};
      const float gap = forest_take::HandGap(point, t);
      if (gap <= reach && gap < best) {
        best = gap;
        pick = i;
      }
    }
    if (pick >= 0) best = best / units * 100.0f;
  } else {
    float origin[3], direction[3], eye[3], ray_units = 0.0f;
    if (!ControllersFarRay(origin, direction, &ray_units, eye) ||
        !(ray_units > 0.0f)) {
      return;
    }
    const float cone = FarGrabSelectDeg();
    for (int i = 0; i < count; ++i) {
      if (!trees[i].inside) continue;
      const forest_take::Tree t = {
          {trees[i].base[0], trees[i].base[1], trees[i].base[2]},
          trees[i].radius, trees[i].height};
      const float angle = forest_take::AimAngle(origin, direction, t);
      if (angle > cone || angle >= best) continue;
      if (!controller_math::FarInRange(
              controller_math::FarFloorMeters(eye, trees[i].base, ray_units),
              FarGrabMaxMeters(), false)) {
        continue;
      }
      if (!HandDirectSeen(trees[i].base, trees[i].radius)) continue;
      best = angle;
      pick = i;
    }
  }
  if (pick < 0) return;
  g_next.valid = true;
  g_next.tree = trees[pick];
  g_next.table = *static_cast<void* const*>(trees[pick].object);
  g_next.measure = best;
}

void ForestTakeInterfaceStep(void* face_in, void* hand_in, bool owns) {
  ReadSettings();
  PollKey();
  uint8_t* face = static_cast<uint8_t*>(face_in);
  uint8_t* hand = static_cast<uint8_t*>(hand_in);
  const double now = NowMs();
  // A fetch from a distance runs: a tree that comes into the Hand soon after
  // it came from far.
  {
    void* object = nullptr;
    float at[3], land[2];
    if (FarGrabFetching(&object, at, land)) g_fetch_ms = now;
  }
  void* status = face != nullptr
                     ? *reinterpret_cast<void* const*>(face + kFaceStatus)
                     : nullptr;
  void* held = hand != nullptr
                   ? *reinterpret_cast<void* const*>(hand + kHandHeld)
                   : nullptr;
  forest_take::Facts facts = {};
  facts.on = g_settings.on && owns && face != nullptr && hand != nullptr &&
             status != nullptr;
  facts.grip = ControllersGripClosed();
  facts.now_ms = now;
  facts.held = forest_take::kHeldNothing;
  if (held != nullptr) {
    facts.held = forest_take::kHeldOther;
    if (g_bundle != nullptr && held == g_bundle &&
        HandDirectObjectLives(g_bundle, g_bundle_table)) {
      facts.held = forest_take::kHeldBundle;
    } else if (facts.on && HandDirectIsTree(held)) {
      facts.held = forest_take::kHeldTree;
    }
  }
  const int before = g_state.phase;
  // The bundle of the last take left the Hand: one line says where the game
  // put it, 1.5 s later, or that the game deleted it (a store took the wood).
  if (g_bundle != nullptr && g_state.phase == forest_take::kIdle) {
    const bool lives = HandDirectObjectLives(g_bundle, g_bundle_table);
    if (held == g_bundle && lives) {
      g_left_ms = 0.0;
    } else {
      if (g_left_ms == 0.0) g_left_ms = now;
      if (!lives || now - g_left_ms >= 1500.0) {
        if (LogRoom()) {
          if (lives) {
            const uint8_t* at = static_cast<const uint8_t*>(g_bundle);
            ModLog("forest take  : the bundle %p left the Hand. %.0f ms later "
                   "it is a %s on the map at %.1f %.1f with %u wood, frame "
                   "%llu", g_bundle, now - g_left_ms, HandDirectClass(g_bundle),
                   *reinterpret_cast<const int32_t*>(at + kThingPosition) /
                       kMapScale,
                   *reinterpret_cast<const int32_t*>(at + kThingPosition + 4) /
                       kMapScale,
                   AmountOf(g_bundle), ControllersFrameCount());
          } else {
            ModLog("forest take  : the bundle %p left the Hand, and the game "
                   "deleted it in %.0f ms (its wood went to a store, or the "
                   "game put it in a different pile), frame %llu", g_bundle,
                   now - g_left_ms, ControllersFrameCount());
          }
        }
        if (!lives) g_pile_look = 60;
        g_bundle = nullptr;
        g_left_ms = 0.0;
      }
    }
  }
  // The next tree is from the draw of this frame (ForestTakeSelect). A tree
  // that the game deleted since then is no tree.
  if (g_next.valid &&
      !HandDirectObjectLives(g_next.tree.object, g_next.table)) {
    g_next.valid = false;
    ++g_gone;
  }
  facts.next = g_next.valid;
  const Next next = g_next;
  const forest_take::State was = g_state;
  const forest_take::Out out = forest_take::Step(
      &g_state, facts, g_settings.wait_ms, g_settings.step_ms, kLeaveMs);

  if (before == forest_take::kIdle && g_state.phase == forest_take::kArmed) {
    // The tree came into the closed grip. Soon after a fetch, the take looks
    // along the aim, and not around the Hand.
    g_far = now - g_fetch_ms < 1500.0;
    if (LogRoom()) {
      ModLog("forest take  : a tree is in the closed grip (%p). It came %s. A "
             "second tree %s starts the take in %d ms, frame %llu", held,
             g_far ? "with a fetch from a distance" : "at the Hand",
             g_far ? "in the aim" : "in the reach of the Hand's point",
             g_settings.wait_ms, ControllersFrameCount());
    }
  }
  if (before == forest_take::kArmed && g_state.phase == forest_take::kCarried &&
      LogRoom()) {
    ModLog("forest take  : no second tree was in reach for %d ms, so the tree "
           "in the Hand stays one tree (%d trees in the pick), frame %llu",
           kLeaveMs, g_trees_seen, ControllersFrameCount());
  }

  // The tree of an act is used once: the game deletes it.
  if (out.act != forest_take::kActNone) g_next.valid = false;
  if (out.act == forest_take::kActStart) {
    if (!StartBundle(face, hand, status, held)) {
      ++g_refused;
      forest_take::Failed(&g_state);
    } else {
      const uint32_t got = TakeTree(g_bundle, status, next);
      ++g_trees;
      ShowAmount(g_bundle);
      g_tip_ms = now;
      ControllersGesturePulse(0.4f, 0.03f);
      if (LogRoom()) {
        ModLog("forest take  : tree 2 goes in: %p (%s), %.1f %s, %u wood. "
               "The bundle holds %u wood, frame %llu", next.tree.object,
               next.tree.forest ? "a forest" : "a tree", next.measure,
               g_far ? "degrees from the aim"
                     : "cm of the room from the Hand's point",
               got, AmountOf(g_bundle), ControllersFrameCount());
      }
    }
  } else if (out.act == forest_take::kActAdd) {
    const uint32_t got = TakeTree(g_bundle, status, next);
    ++g_trees;
    ShowAmount(g_bundle);
    g_tip_ms = now;
    ControllersGesturePulse(0.4f, 0.03f);
    if ((g_state.trees <= 6 || g_state.trees % 10 == 0) && LogRoom()) {
      ModLog("forest take  : tree %d goes in: %p (%s), %.1f %s, %u wood. The "
             "bundle holds %u wood, frame %llu", g_state.trees,
             next.tree.object, next.tree.forest ? "a forest" : "a tree",
             next.measure,
             g_far ? "degrees from the aim"
                   : "cm of the room from the Hand's point",
             got, AmountOf(g_bundle), ControllersFrameCount());
    }
  } else if (out.runs && now - g_tip_ms >= 100.0) {
    // The amount stays on show while the take runs, as for a pile.
    ShowAmount(g_bundle);
    g_tip_ms = now;
  }
  if (out.ended) {
    const forest_take::State now_state = g_state;
    g_state = was;
    SayEnd(!facts.on ? "the controller does not place the Hand now"
           : facts.held != forest_take::kHeldBundle
               ? "the bundle left the Hand"
               : "the grip opened",
           held);
    g_state = now_state;
  }
  g_runs = g_state.phase == forest_take::kTaking;
  if (g_state.phase == forest_take::kIdle) g_far = false;
}

// The line of the grip on the card.
const wchar_t* ForestTakeHint() {
  static wchar_t text[64];
  if (!g_settings.on) return nullptr;
  if (g_state.phase == forest_take::kTaking) {
    swprintf_s(text, L"Open to stop (%d trees)", g_state.trees);
    return text;
  }
  if (g_state.phase == forest_take::kArmed && g_next.valid) {
    return L"Hold to collect";
  }
  return nullptr;
}

void ForestTakeReport() {
  if (g_takes == 0 && g_trees == 0 && g_refused == 0) return;
  ModLog("forest take  : %llu takes ended, %llu trees went into a bundle, %llu "
         "starts that the game refused, %llu trees that the game deleted "
         "before the take got them, this window", g_takes, g_trees, g_refused,
         g_gone);
  g_takes = g_trees = g_refused = g_gone = 0;
}
