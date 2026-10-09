// Things that must happen once a frame, not once an eye.
//
// The mod renders the scene twice, once per eye, by calling
// GGame::Process3dEngine twice. That function is not purely a renderer. Among
// the drawing it also steps state, and anything it steps runs twice:
//
//     PhysicsObject::GameTurnUpdate()                  a physics step
//     MyInterface()->hand.Get()->UpdateHeldObject()    the Hand
//     TemporaryShadow::UpdateAll()
//     UpdateLiquidParticles(...)
//     Temple::UpdateFade()
//
// The animation clock is one case: Windmill::PreDraw steps its own rotation
// by the time increment on every call, so windmills would spin twice as fast.
// The mod freezes the two time globals for the second pass.
//
// The camera drag is the second case. It does not depend on time. A Hand that
// updates twice against a camera that moved in between is a feedback loop, and
// the drag would resist and then overshoot.
//
// The rule is the same as for the time freeze: the first pass steps the
// state, and the second pass draws what the first pass decided. The two eyes
// then show the same moment.
//
// Each one is a separate switch, so a test can narrow the cause by bisection.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "MinHook.h"

extern void ModLog(const char* format, ...);
// hand_exact.cpp: the drawn Hand after its draw setup, and the calls of later
// features. See docs/hand.md, "The exact Hand".
void HandExactAfterPrepare(void* hand);
// throw_physics.cpp: the real flight of a thrown object runs its steps in
// the game's physics turn. See docs/throw.md, "The throw physics".
void ThrowPhysicsTurnBegin();
void ThrowPhysicsTurnEnd();
// head_camera.cpp

namespace {

// From bw1-decomp, which matches our v1.20 binary at identical addresses.
const uintptr_t kHandUpdateHeldObject = 0x0046E490;
const uintptr_t kPhysicsGameTurnUpdate = 0x00644FC0;

// CHand::PrepareForDrawing, which the decompilation names but has not yet
// recovered. It runs inside Process3dEngine, beside UpdateHeldObject, so the
// mod calls it twice a frame against two different eye cameras.
const uintptr_t kHandPrepareForDrawing = 0x0046C550;

// Where the Hand's world position is copied to, by sub_0046E160 called at
// 0x005B725F. Used to check the anchor numerically rather than by eye.
const uintptr_t kHandWorldPosition = 0x00C5E680;
int g_log_hand = 0;
// Calls to wait before logging, so a head step and the cursor move that
// follows it have both settled.
int g_log_hand_wait = 0;

// WHAT THE GAME THINKS IS UNDER THE HAND.
//
// GInterface::PreDrawProcess 0x005CE9E0 copies what the last frame's drawing
// found under the pointer into the interface, then asks the landscape which
// point is under the pointer for this frame. The fields, read from
// GInterface::UpdateHandRenderCollide 0x005D0610 and fn_005D5980:
//
//     +0x3B8  the land point under the pointer, and +0x3C4 its distance
//     +0x3C8  the object under the pointer, and +0x3CC its distance
//     +0x428  the pointer position the landscape was asked about
const uintptr_t kGame = 0x00D0195C;
const uintptr_t kMyInterface = 0x00555850;
typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);

// THE WORLD STEPPERS.
//
// These four run inside Process3dEngine as well, so the mod calls each of them
// twice a frame. With the head on the camera the two cameras differ by a whole
// head turn, and the sea shows it: the horizon slides as the head moves.
//
// All four are __cdecl (checked in the binary: every one ends in C3, so the
// caller cleans). Two of them take a float. ChangeFov is __stdcall, so a wrong
// guess of the convention for such a function crashes the game.
const uintptr_t kAtmosUpdate3D = 0x008357A0;         // LH3DAtmos::Update3D(float)
const uintptr_t kUpdateLiquidParticles = 0x00845D00;  // (float)
const uintptr_t kTemporaryShadowUpdateAll = 0x00825190;
const uintptr_t kTempleUpdateFade = 0x00794280;

// THE GAME TURN.
//
// The simulation, the script machine and audio do not run per frame. They run
// on a 10 Hz turn, and a single player game allows at most one turn per frame.
// GGame::ProcessNetworkPackets asks GGame::LocalTimerSaysDoATurn whether one
// is due and runs GGame::ProcessOneGameTurn when it is.
//
// At 30 fps a turn hides inside a 33 ms frame. At 90 fps the frame is 11.11 ms
// and the turn does not fit, so it arrives as a hitch rather than as a cost.
// This times the turn directly.
const uintptr_t kProcessNetworkPackets = 0x0054CC30;

// CHand::UpdateHeldObject is a member function with no arguments, so this is
// in ecx. __fastcall keeps ecx and edx, which is what a thiscall needs.
typedef void(__fastcall* HandUpdateFn)(void* self, void* unused);
typedef void(__cdecl* PhysicsUpdateFn)();
// A member function with no arguments, so this is in ecx.
typedef void(__fastcall* NetworkPacketsFn)(void* self, void* unused);

HandUpdateFn o_HandUpdateHeldObject = nullptr;
HandUpdateFn o_HandPrepareForDrawing = nullptr;

typedef void(__cdecl* FloatFn)(float value);
typedef void(__cdecl* VoidFn)();
FloatFn o_AtmosUpdate3D = nullptr;
FloatFn o_UpdateLiquidParticles = nullptr;
VoidFn o_TemporaryShadowUpdateAll = nullptr;
VoidFn o_TempleUpdateFade = nullptr;
PhysicsUpdateFn o_PhysicsGameTurnUpdate = nullptr;
NetworkPacketsFn o_ProcessNetworkPackets = nullptr;

LARGE_INTEGER g_frequency = {0};
uint64_t g_turn_ticks = 0;
uint64_t g_turn_worst = 0;
uint64_t g_turn_calls = 0;
uint64_t g_turn_heavy = 0;   // calls that took more than a 90 Hz frame

bool g_installed = false;
bool g_skip_hand = true;
bool g_skip_physics = true;
// Off until it is shown to be worth having. A Hand that does not prepare for
// the second eye may draw the second eye wrong, and that would be worse than
// the fault it is aimed at.
bool g_skip_prepare = false;
// The world steppers, on by default: nothing inside the scene walk should step
// the world twice, and the sea proved it.
bool g_skip_world = true;

// Which eye pass is running. -1 means the mod is not inside a pass, and then
// nothing is skipped, because a single walk has to do all of the work.
int g_eye = -1;

uint64_t g_hand_skipped = 0;
uint64_t g_physics_skipped = 0;
uint64_t g_prepare_skipped = 0;
uint64_t g_world_skipped = 0;

bool InSecondPass() { return g_eye == 1; }

void __fastcall h_HandUpdateHeldObject(void* self, void* unused) {
  if (g_skip_hand && InSecondPass()) {
    ++g_hand_skipped;
    return;
  }
  o_HandUpdateHeldObject(self, unused);
}

void __fastcall h_HandPrepareForDrawing(void* self, void* unused) {
  if (g_skip_prepare && InSecondPass()) {
    ++g_prepare_skipped;
    return;
  }
  // This is where the Hand is placed, through HandState::Update and then
  // ScreenPointToWorldAtDistance 0x0074CAF0. It sees the same camera as every
  // other part of the game. Keeping the Hand still when the head turns is done
  // by moving the cursor, in pointer_map.cpp, not by swapping the camera here.
  o_HandPrepareForDrawing(self, unused);
  HandExactAfterPrepare(self);

  // WHERE THE HAND ENDED UP.
  //
  // The anchor is meant to make the Hand ignore the head, and that is a
  // numerical claim, not a visual one: with the mouse still, the Hand's world
  // position must not change when the head turns. 0x00C5E680 is where
  // sub_0046E160 copies it to at 0x005B725F.
  if (g_log_hand > 0) {
    if (g_log_hand_wait > 0) {
      --g_log_hand_wait;
      return;
    }
    --g_log_hand;
    const float* hand = reinterpret_cast<const float*>(kHandWorldPosition);
    if (!IsBadReadPtr(hand, sizeof(float) * 3)) {
      // The camera the game asked for, which the camera hook hands back.
      const float* camera = reinterpret_cast<const float*>(0x00EA1DB8);
      ModLog("hand         : world position %10.2f %10.2f %10.2f, game camera "
             "%.2f %.2f %.2f", hand[0], hand[1], hand[2], camera[0], camera[1],
             camera[2]);
    }
    void* const* game = reinterpret_cast<void* const*>(kGame);
    if (!IsBadReadPtr(game, sizeof(void*)) && *game != nullptr) {
      const MyInterfaceFn my_interface =
          reinterpret_cast<MyInterfaceFn>(kMyInterface);
      const uint8_t* face =
          static_cast<const uint8_t*>(my_interface(*game, nullptr));
      if (face != nullptr && !IsBadReadPtr(face, 0x430)) {
        // MapCoords: whole map units, not floats.
        const int32_t* land = reinterpret_cast<const int32_t*>(face + 0x3B8);
        const float land_distance =
            *reinterpret_cast<const float*>(face + 0x3C4);
        const void* object = *reinterpret_cast<void* const*>(face + 0x3C8);
        const float object_distance =
            *reinterpret_cast<const float*>(face + 0x3CC);
        const int32_t* asked = reinterpret_cast<const int32_t*>(face + 0x428);
        const int32_t* pointer = reinterpret_cast<const int32_t*>(0x00E852C0);
        ModLog("hand         : under the pointer, land %d %d %d at %.2f, object "
               "%p at %.2f, asked at %d %d, pointer %d %d",
               land[0], land[1], land[2], land_distance, object,
               object_distance, asked[0], asked[1], pointer[0], pointer[1]);
      }
    }
  }
}

void __cdecl h_AtmosUpdate3D(float value) {
  if (g_skip_world && InSecondPass()) {
    ++g_world_skipped;
    return;
  }
  o_AtmosUpdate3D(value);
}

void __cdecl h_UpdateLiquidParticles(float value) {
  if (g_skip_world && InSecondPass()) {
    ++g_world_skipped;
    return;
  }
  o_UpdateLiquidParticles(value);
}

void __cdecl h_TemporaryShadowUpdateAll() {
  if (g_skip_world && InSecondPass()) {
    ++g_world_skipped;
    return;
  }
  o_TemporaryShadowUpdateAll();
}

void __cdecl h_TempleUpdateFade() {
  if (g_skip_world && InSecondPass()) {
    ++g_world_skipped;
    return;
  }
  o_TempleUpdateFade();
}

void __cdecl h_PhysicsGameTurnUpdate() {
  if (g_skip_physics && InSecondPass()) {
    ++g_physics_skipped;
    return;
  }
  ThrowPhysicsTurnBegin();
  o_PhysicsGameTurnUpdate();
  ThrowPhysicsTurnEnd();
}

void __fastcall h_ProcessNetworkPackets(void* self, void* unused) {
  LARGE_INTEGER before, after;
  QueryPerformanceCounter(&before);
  o_ProcessNetworkPackets(self, unused);
  QueryPerformanceCounter(&after);
  const uint64_t spent = after.QuadPart - before.QuadPart;
  g_turn_ticks += spent;
  if (spent > g_turn_worst) g_turn_worst = spent;
  ++g_turn_calls;
  if (g_frequency.QuadPart != 0 &&
      (static_cast<double>(spent) * 1000.0 /
       static_cast<double>(g_frequency.QuadPart)) > 11.11) {
    ++g_turn_heavy;
  }
}

bool Hook(uintptr_t address, void* replacement, void** original,
          const char* name) {
  void* target = reinterpret_cast<void*>(address);
  if (MH_CreateHook(target, replacement, original) != MH_OK) {
    ModLog("pass guard   : %s at 0x%08X FAILED to hook", name, address);
    return false;
  }
  if (MH_EnableHook(target) != MH_OK) {
    ModLog("pass guard   : %s at 0x%08X FAILED to enable", name, address);
    return false;
  }
  return true;
}

}  // namespace

void PassGuardSetOptions(bool skip_hand, bool skip_physics) {
  g_skip_hand = skip_hand;
  g_skip_physics = skip_physics;
}

// Called at the start of each eye pass, and with -1 once both are done.
void PassGuardSetEye(int eye) { g_eye = eye; }

// Ask for the next few Hand positions to be written out.
void PassGuardLogHand(int count) {
  g_log_hand = count;
  g_log_hand_wait = 60;
}

void PassGuardSetSkipWorld(bool on) {
  g_skip_world = on;
  ModLog("pass guard   : the world steppers run %s",
         on ? "once a frame" : "once an eye");
}

bool PassGuardSkipWorld() { return g_skip_world; }

void PassGuardSetSkipPrepare(bool on) {
  g_skip_prepare = on;
  ModLog("pass guard   : CHand::PrepareForDrawing on the second pass is %s",
         on ? "skipped" : "allowed");
}

bool PassGuardSkipPrepare() { return g_skip_prepare; }

void PassGuardInstall() {
  if (g_installed) return;
  int hooked = 0;
  hooked += Hook(kHandUpdateHeldObject,
                 reinterpret_cast<void*>(&h_HandUpdateHeldObject),
                 reinterpret_cast<void**>(&o_HandUpdateHeldObject),
                 "CHand::UpdateHeldObject") ? 1 : 0;
  hooked += Hook(kPhysicsGameTurnUpdate,
                 reinterpret_cast<void*>(&h_PhysicsGameTurnUpdate),
                 reinterpret_cast<void**>(&o_PhysicsGameTurnUpdate),
                 "PhysicsObject::GameTurnUpdate") ? 1 : 0;
  Hook(kHandPrepareForDrawing,
       reinterpret_cast<void*>(&h_HandPrepareForDrawing),
       reinterpret_cast<void**>(&o_HandPrepareForDrawing),
       "CHand::PrepareForDrawing");
  Hook(kAtmosUpdate3D, reinterpret_cast<void*>(&h_AtmosUpdate3D),
       reinterpret_cast<void**>(&o_AtmosUpdate3D), "LH3DAtmos::Update3D");
  Hook(kUpdateLiquidParticles,
       reinterpret_cast<void*>(&h_UpdateLiquidParticles),
       reinterpret_cast<void**>(&o_UpdateLiquidParticles),
       "UpdateLiquidParticles");
  Hook(kTemporaryShadowUpdateAll,
       reinterpret_cast<void*>(&h_TemporaryShadowUpdateAll),
       reinterpret_cast<void**>(&o_TemporaryShadowUpdateAll),
       "TemporaryShadow::UpdateAll");
  Hook(kTempleUpdateFade, reinterpret_cast<void*>(&h_TempleUpdateFade),
       reinterpret_cast<void**>(&o_TempleUpdateFade), "Temple::UpdateFade");
  QueryPerformanceFrequency(&g_frequency);
  Hook(kProcessNetworkPackets,
       reinterpret_cast<void*>(&h_ProcessNetworkPackets),
       reinterpret_cast<void**>(&o_ProcessNetworkPackets),
       "GGame::ProcessNetworkPackets");
  g_installed = hooked > 0;
  ModLog("pass guard   : %d of 2 hooked, skipping on the second pass: "
         "hand=%d physics=%d", hooked, g_skip_hand ? 1 : 0,
         g_skip_physics ? 1 : 0);
}

void PassGuardReport() {
  if (!g_installed) return;
  ModLog("pass guard   : held object %llu, physics %llu, prepare %llu, world "
         "%llu skipped on second passes", g_hand_skipped, g_physics_skipped,
         g_prepare_skipped, g_world_skipped);
  g_world_skipped = 0;
  g_hand_skipped = 0;
  g_physics_skipped = 0;
  g_prepare_skipped = 0;

  if (g_turn_calls > 0 && g_frequency.QuadPart != 0) {
    const double to_ms = 1000.0 / static_cast<double>(g_frequency.QuadPart);
    ModLog("game turn    : mean %.2f ms, worst %.2f ms, %llu calls, %llu over "
           "a 90 Hz frame",
           (static_cast<double>(g_turn_ticks) / g_turn_calls) * to_ms,
           static_cast<double>(g_turn_worst) * to_ms, g_turn_calls,
           g_turn_heavy);
    g_turn_ticks = 0;
    g_turn_worst = 0;
    g_turn_calls = 0;
    g_turn_heavy = 0;
  }
}
