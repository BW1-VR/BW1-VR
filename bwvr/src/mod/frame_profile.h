// The frame cost measure of profile=1, profile=2 and profile=3. See frame_profile.cpp
// and docs/backend.md, "Frame cost".

#pragma once

#include <stdint.h>

// The timers of one frame. A file that times a block gives the ticks of
// QueryPerformanceCounter to FrameProfileAdd.
enum FrameProfileTimer {
  // The calls of the frame loop of the game (frame_timers.cpp, pass_guard.cpp).
  kFpInput,     // ProcessActionsPerformed, ProcessBufferedKeys and
                // ProcessFrameInputs
  kFpTurn,      // GGame::ProcessNetworkPackets, which runs the game turn
  kFpFlip,      // GGame::FlipScreen, which copies the frame to the monitor
  // The stereo hook (stereo.cpp).
  kFpWait,      // the wait for the runtime of the headset
  kFpEye0,      // the scene walk of eye pass 0
  kFpEye1,      // the scene walk of eye pass 1
  kFpGrab,      // the eye pictures go to the bridge, or the read back
  // Inside the parts above. kFpFirstInside is the first of them.
  kFpModFrame,  // the frame functions of the mod, before the eye passes
  kFpModPass,   // the pass functions of the mod, around each scene walk
  kFpEndFrame,  // the frame goes to the runtime of the headset
  kFpReport,    // the report of the mod, one time in 450 frames
  kFpD3d,       // Direct3D draw calls (profile=1 only)
  kFpLog,       // lines of the log of the mod
  kFpLock,      // IDirectDrawSurface7::Lock, Unlock and BltFast
  kFpCopy,      // IDirectDrawSurface7::Blt, but not the copy to the monitor
  kFpOpen,      // CreateFile
  kFpRead,      // ReadFile
  kFpSleep,     // Sleep
  kFpTimers
};
const int kFpFirstInside = kFpModFrame;

enum FrameProfileCounter {
  kFpcDraws,
  kFpcVertices,
  kFpcLocks,
  kFpcCopies,
  kFpcOpens,
  kFpcReads,
  kFpcLogLines,
  kFpcSleeps,
  // The state count of profile=3 (draw_probe.cpp).
  kFpcRuns,          // rows of draw calls with one state
  kFpcRunsTexture,   // runs that start at a change of a texture only
  kFpcRunsState,     // runs that start at a change of a render or stage state
  kFpcRunsOther,     // runs that start for a different cause
  kFpcNotLists,      // draw calls that are not triangle lists
  kFpcStateCalls,    // calls for render states, stage states and textures
  kFpcStateChanges,  // of them, the calls that change a value
  kFpcSceneCalls,    // calls of BeginScene and EndScene
  kFpcOtherCalls,    // the other calls that end a run
  kFpcCounters
};

// The frame functions of the mod that stereo.cpp calls before the eye passes.
enum FrameProfileFunctionIndex {
  kFpFnControllers,
  kFpFnHand,
  kFpFnTeleport,
  kFpFnCreatureCommand,
  kFpFnMiracle,
  kFpFnVillage,
  kFpFnOverlay,
  kFpFnWater,
  kFpFnTable,
  kFpFunctions
};

// True with profile=1, 2 or 3. With profile=0 each call below returns at
// its first line, and a caller that reads this flag first makes no call.
extern bool g_frame_profile_on;
inline bool FrameProfileOn() { return g_frame_profile_on; }

// profile of bwvr.ini: 0 is off, 1 is the measure with a timer on each
// Direct3D draw call, 2 is the measure with no such timer, and 3 is the
// measure of 1 with the state count of the draw probe.
void FrameProfileSetLevel(int level);
// The hooks of the measure: the surface calls, the file calls and Sleep. It
// needs the render target of the game. With profile=0 it does nothing.
void FrameProfileInstall();

uint64_t FrameProfileTicks();
void FrameProfileAdd(int timer, uint64_t ticks);
void FrameProfileCount(int counter, uint32_t n);
// One frame function of the mod: its time goes to kFpModFrame too.
void FrameProfileFunction(int index, uint64_t ticks);
// The name of a file that the game opens in this frame.
void FrameProfileFile(const char* name);

// The code that made a call of Windows or of DirectDraw on the frame thread:
// the address that the call returns to. `asked` is a number of the call (the
// ms of a Sleep, the width and the height of a locked surface), or 0. The
// summary of a group names the callers with the most time. `stack` is the
// place of that return address on the stack (_AddressOfReturnAddress), or
// null. For the longest call of a caller, the summary names the code of the
// game that the stack has above it.
enum FrameProfileCallKind {
  kFpCallSleep,
  kFpCallLock,
  kFpCallCopy,
  kFpCallRead,
  kFpCallKinds
};
void FrameProfileCaller(int kind, const void* address, uint32_t asked,
                        uint64_t ticks, const void* stack);

// The first call of the frame loop of the game: a frame ends and the next
// one starts.
void FrameProfileLoopTop();
// Around GGame::ProcessGraphicsEngine, and around the stereo hook in it.
void FrameProfileGraphics(bool begin);
void FrameProfileHook(bool begin);
// The stereo hook draws two eye passes in this frame. `frame` is the frame of
// the controllers. A frame with no eye passes is not in the measure.
void FrameProfileStereoFrame(uint64_t frame);
// The word "bench" of the scripted controller: the next `frames` frames are
// one scene of the measure.
void FrameProfileSceneStart(uint64_t frames, uint64_t frame);
