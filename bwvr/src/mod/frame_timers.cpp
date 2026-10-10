// Where the frame goes, outside the scene walk.
//
// The stereo report splits a frame into the wait, the two scene walks, the two
// grabs, and everything else. Everything else is normally 0.2 ms, but a bad
// frame can put tens of milliseconds in it.
//
// Such a hitch is not the scene, and it is not the game turn. It is one of the
// other calls in the frame loop, or it is outside the loop altogether.
//
// GGame::Loop runs the whole game, not one frame, so it cannot be timed per
// frame. Its inner loop is short enough to time call by call (Game.cpp):
//
//     control_map->ProcessActionsPerformed();
//     EnterCriticalSection(&LHKeyboard::CriticalSection);
//     ProcessBufferedKeys();
//     LeaveCriticalSection(...);
//     ProcessNetworkPackets();          <- timed in pass_guard.cpp
//     fn_005525E0();                    <- flushes packed network data
//     ProcessGraphicsEngine(0, 0);      <- contains the two scene walks
//     fn_0054D7F0();                    <- counts frames, too small to time
//     ProcessFrameInputs();
//     ScriptedScreenShot::Process();
//     FlipScreen();
//     players[i].SavePlayerAlignment(...);
//     CreatureRoom::ProcessScreenShot();
//
// If every one of these is small while the frame is long, the time is not in
// the game at all, and the next place to look is the driver, another thread,
// or the operating system.
//
// Every address below carries a BW1W120 marker in the decompilation, which is
// the build this binary is. Every one is a member function with no arguments
// except ProcessGraphicsEngine, which takes two. That matters: the callee
// cleans the stack, so a stub with the wrong argument count corrupts it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "MinHook.h"
#include "frame_profile.h"

extern void ModLog(const char* format, ...);
// tabletop.cpp: the render bracket of the table ends with
// GGame::ProcessGraphicsEngine. It does nothing unless the table owns the
// frame.
void TabletopGraphicsDone();

namespace {

struct Slot {
  const char* name;
  uintptr_t address;
  void* original;
  uint64_t ticks;
  uint64_t worst;
  uint64_t calls;
};

enum {
  kActions,
  kBufferedKeys,
  kNetworkFlush,
  kGraphics,
  kFrameInputs,
  kFlipScreen,
  kSlotCount,
};

Slot g_slots[kSlotCount] = {
    {"ControlMap::ProcessActionsPerformed", 0x00470AB0, nullptr, 0, 0, 0},
    {"GGame::ProcessBufferedKeys", 0x0054C420, nullptr, 0, 0, 0},
    {"GGame::network flush", 0x005525E0, nullptr, 0, 0, 0},
    {"GGame::ProcessGraphicsEngine", 0x0054D850, nullptr, 0, 0, 0},
    {"GGame::ProcessFrameInputs", 0x0054C340, nullptr, 0, 0, 0},
    {"GGame::FlipScreen", 0x0054D800, nullptr, 0, 0, 0},
};

LARGE_INTEGER g_frequency = {0};
bool g_installed = false;
bool g_graphics_hooked = false;

typedef void(__fastcall* VoidMemberFn)(void* self, void* unused);
typedef void(__fastcall* GraphicsFn)(void* self, void* unused, uint32_t a,
                                     uint32_t b);

void Record(int slot, uint64_t spent) {
  g_slots[slot].ticks += spent;
  if (spent > g_slots[slot].worst) g_slots[slot].worst = spent;
  ++g_slots[slot].calls;
}

// The timer of the frame cost measure (frame_profile.cpp) that a slot goes
// to, or -1. The network flush is in the part "other" of that measure.
int ProfileTimer(int slot) {
  if (slot == kActions || slot == kBufferedKeys || slot == kFrameInputs) {
    return kFpInput;
  }
  return slot == kFlipScreen ? kFpFlip : -1;
}

// One stub per slot, so each one knows which slot it is timing.
template <int N>
void __fastcall MemberStub(void* self, void* unused) {
  LARGE_INTEGER before, after;
  QueryPerformanceCounter(&before);
  reinterpret_cast<VoidMemberFn>(g_slots[N].original)(self, unused);
  QueryPerformanceCounter(&after);
  Record(N, after.QuadPart - before.QuadPart);
  if (FrameProfileOn() && ProfileTimer(N) >= 0) {
    FrameProfileAdd(ProfileTimer(N), after.QuadPart - before.QuadPart);
  }
}

// ControlMap::ProcessActionsPerformed is the first call of the frame loop,
// so the frame of the frame cost measure starts there.
void __fastcall ActionsStub(void* self, void* unused) {
  FrameProfileLoopTop();
  MemberStub<kActions>(self, unused);
}

void __fastcall GraphicsStub(void* self, void* unused, uint32_t a, uint32_t b) {
  LARGE_INTEGER before, after;
  FrameProfileGraphics(true);
  QueryPerformanceCounter(&before);
  reinterpret_cast<GraphicsFn>(g_slots[kGraphics].original)(self, unused, a, b);
  QueryPerformanceCounter(&after);
  FrameProfileGraphics(false);
  Record(kGraphics, after.QuadPart - before.QuadPart);
  // This is the one hook on this function, so the table ends its bracket
  // here. See tabletop.cpp.
  TabletopGraphicsDone();
}

bool Hook(int slot, void* replacement) {
  void* target = reinterpret_cast<void*>(g_slots[slot].address);
  MH_STATUS status = MH_CreateHook(target, replacement, &g_slots[slot].original);
  if (status == MH_OK) status = MH_EnableHook(target);
  if (status != MH_OK) {
    ModLog("frame timers : %s at 0x%08X FAILED", g_slots[slot].name,
           g_slots[slot].address);
    return false;
  }
  return true;
}

}  // namespace

void FrameTimersInstall() {
  if (g_installed) return;
  QueryPerformanceFrequency(&g_frequency);
  int ok = 0;
  ok += Hook(kActions, reinterpret_cast<void*>(&ActionsStub)) ? 1 : 0;
  ok += Hook(kBufferedKeys,
             reinterpret_cast<void*>(&MemberStub<kBufferedKeys>)) ? 1 : 0;
  ok += Hook(kNetworkFlush,
             reinterpret_cast<void*>(&MemberStub<kNetworkFlush>)) ? 1 : 0;
  g_graphics_hooked = Hook(kGraphics, reinterpret_cast<void*>(&GraphicsStub));
  ok += g_graphics_hooked ? 1 : 0;
  ok += Hook(kFrameInputs,
             reinterpret_cast<void*>(&MemberStub<kFrameInputs>)) ? 1 : 0;
  ok += Hook(kFlipScreen,
             reinterpret_cast<void*>(&MemberStub<kFlipScreen>)) ? 1 : 0;
  g_installed = ok > 0;
  ModLog("frame timers : %d of %d hooked", ok, kSlotCount);
}

// Whether GGame::ProcessGraphicsEngine is hooked, so the table can end its
// render bracket there.
bool FrameTimersGraphicsHooked() { return g_graphics_hooked; }

// Names anything that cost more than a 90 Hz frame, and the mean of each.
// A quiet call is not printed, because a list of zeros hides the one number
// that matters.
void FrameTimersReport(uint64_t frames) {
  if (!g_installed || g_frequency.QuadPart == 0 || frames == 0) return;
  const double to_ms = 1000.0 / static_cast<double>(g_frequency.QuadPart);
  for (int i = 0; i < kSlotCount; ++i) {
    Slot& s = g_slots[i];
    if (s.calls == 0) continue;
    const double worst = static_cast<double>(s.worst) * to_ms;
    const double mean =
        (static_cast<double>(s.ticks) / static_cast<double>(s.calls)) * to_ms;
    if (worst >= 3.0) {
      ModLog("frame timers : %-36s mean %5.2f ms, worst %6.2f ms over %llu "
             "calls", s.name, mean, worst, s.calls);
    }
    s.ticks = 0;
    s.worst = 0;
    s.calls = 0;
  }
}
