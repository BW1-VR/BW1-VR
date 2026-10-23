// THE FRAME TIME OF THE GAME AFTER THE TWO EYE PASSES. See docs/backend.md,
// "Time, bars and the second eye pass".
//
// THE NUMBER. LH3DTech::g_delta_time (0x00C38134) is the time of the last
// frame in ms. In play only LH3DRender::StartFrame (0x0082F0E0) writes it:
// the sample of its clock less the sample of its call before, and 1 at the
// least (frame_time_math.h). GGame::Process3dEngine calls StartFrame first
// (0x0054DAB5), and the mod runs that function two times in a frame. So the
// first eye pass gets the time since the start of the second pass of the
// frame before, and the second eye pass gets the time of the first pass.
// Neither number is the time of a frame.
//
// WHO READS IT OUTSIDE THE PASSES. The frame loop of the game reads the
// number before the scene walk and after it. In the plain game such a reader
// gets the time of one whole frame.
//
//   GCamera::Update 0x00441F80, before the scene walk. GGame::GetCameraTimeInc
//   0x00555820 gives the number as the time of the camera mode of the player
//   and of the 6 Zoomers of the game camera, while no cut scene with the
//   cinema bars plays. CameraModeNew3::Update counts 1 s with it before the
//   camera of a fight starts (0x0045BC87), and CameraModeNew3::UpdateTricons
//   counts a wait down with it (0x0045924F).
//
//   TempleRoom::UpdateMouse 0x0079A3A0, after the scene walk, from
//   CMouse::ProcessButtons. It gives the number to each update of the camera
//   of a room of the Temple as its seconds (0x0079A507).
//
// THE RULE (frame_time_real=1). After the two passes the game gets the time
// of the whole frame: the sample that StartFrame keeps after the passes less
// its sample from before the passes, with the limit of StartFrame. One whole
// frame is between those two samples, with both passes in it, and StartFrame
// would make the same number in a frame with one scene walk. With
// frame_time_real=0 the number from before the passes goes back. No code
// writes it outside the passes, so the game then reads the number that the
// last StartFrame before the first stereo frame left: one number for the
// session. The mod follows that number with each value of the key
// (frame_time_math::Kept), so the test key gives it at each moment.
//
// The number that each pass reads does not change with the key.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "../common/bw_addresses.h"
#include "frame_time_math.h"
#include "temple.h"

extern void ModLog(const char* format, ...);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);

namespace {

using frame_time_math::kKept;
using frame_time_math::kReal;

// The sample of the clock that LH3DRender::StartFrame keeps for its next
// call, in ms. Only StartFrame reads it (0x0082F17C) and writes it
// (0x0082F1A1).
const uintptr_t kClockSample = 0x00EA9EAC;
// The code of the game, for the list of the readers.
const uint32_t kCodeStart = 0x00401000;
const uint32_t kCodeEnd = 0x008A9000;

int g_real = kReal;          // frame_time_real
int g_test_frame = 0;        // frame_time_test_frame
int g_readers_frame = 0;     // frame_time_test_readers
int g_readers_frames = 600;  // frame_time_test_readers_frames

int32_t g_clock_before = 0;
bool g_have_clock = false;
uint32_t g_pass[2] = {0, 0};
frame_time_math::Window g_window = {};
// The number that frame_time_real=0 gives (frame_time_math::Kept), and what
// the mod wrote after the passes of the frame before.
uint32_t g_kept = 0;
uint32_t g_wrote = 0;
int32_t g_clock_wrote = 0;
bool g_have_wrote = false;

const char* RuleText(int rule) {
  return rule == kReal
             ? "After the two eye passes the game has the real time of the "
               "frame as its frame time (LH3DTech::g_delta_time), so the "
               "cameras of the game and each other reader outside the passes "
               "move at the speed of the game"
             : "After the two eye passes the mod puts the frame time of the "
               "game back to the number from before the passes, so the code "
               "of the game outside the passes reads one number for the "
               "session";
}

uint32_t* DeltaTime() {
  return reinterpret_cast<uint32_t*>(bw::tech::kDeltaTime);
}

// --- the readers (a test of a desktop run) ---------------------------------
//
// frame_time_test_readers=N: from frame N, for frame_time_test_readers_frames
// frames, a data breakpoint of the processor is on the frame time for the
// thread of the game. Each instruction that reads or writes the number makes
// a trap, and the handler counts it for the part of the frame that runs:
// outside the hook of the scene walk, in the hook and not in a pass, eye
// pass 0 and eye pass 1. At the end the log has one line for each
// instruction. The address of a line is the address after the instruction,
// which is 4 more than the address of the reference in the code of the game.
//
// The handler must not read the frame time: a read there makes a trap in
// the handler. So the number of a line comes from a copy. The two writes of
// StartFrame give the copy from their register, and the code of the mod
// gives it outside the handler.

frame_time_math::Readers g_readers = {};
int g_part = frame_time_math::kOutside;
bool g_armed = false;
bool g_readers_done = false;
uint32_t g_other_code = 0;   // traps from code that is not the game's
uint32_t g_copy = 0;         // the frame time, for the handler
// The addresses after the two writes of LH3DRender::StartFrame: the number
// from ecx (0x0082F18D), and the limit of 1 from edi (0x0082F195).
const uint32_t kAfterWrite = 0x0082F193;
const uint32_t kAfterLimit = 0x0082F19B;
uint64_t g_readers_start = 0;
uint64_t g_readers_temple = 0;  // frames of the count in the Temple
void* g_trap_handler = nullptr;

LONG CALLBACK ReaderTrap(EXCEPTION_POINTERS* info) {
  if (!g_armed || info == nullptr || info->ExceptionRecord == nullptr ||
      info->ContextRecord == nullptr) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  CONTEXT* context = info->ContextRecord;
  // A trap of the breakpoint, and not a single step of other code.
  if ((context->Dr6 & 0x1) == 0 && (context->EFlags & 0x100) != 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  context->Dr6 &= ~static_cast<DWORD>(0xF);
  const uint32_t after = static_cast<uint32_t>(context->Eip);
  if (after == kAfterWrite) g_copy = static_cast<uint32_t>(context->Ecx);
  if (after == kAfterLimit) g_copy = static_cast<uint32_t>(context->Edi);
  if (after >= kCodeStart && after < kCodeEnd) {
    frame_time_math::Note(&g_readers, after, g_part, g_copy);
  } else {
    ++g_other_code;
  }
  return EXCEPTION_CONTINUE_EXECUTION;
}

struct BreakJob {
  HANDLE thread;
  bool on;
  bool ok;
};

// Runs on a helper thread: the thread of the game is stopped while its debug
// registers change.
DWORD WINAPI BreakThread(void* parameter) {
  BreakJob* job = static_cast<BreakJob*>(parameter);
  job->ok = false;
  if (SuspendThread(job->thread) == static_cast<DWORD>(-1)) return 0;
  CONTEXT context = {};
  context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  if (GetThreadContext(job->thread, &context)) {
    // Breakpoint 0: 4 bytes, on a read and on a write.
    const DWORD mask = 0x000F0003;
    context.Dr7 &= ~mask;
    if (job->on) {
      context.Dr0 = bw::tech::kDeltaTime;
      context.Dr7 |= 0x000F0001;
    } else {
      context.Dr0 = 0;
    }
    context.Dr6 = 0;
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    job->ok = SetThreadContext(job->thread, &context) != 0;
  }
  ResumeThread(job->thread);
  return 0;
}

bool SetBreak(bool on) {
  BreakJob job = {nullptr, on, false};
  if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                       GetCurrentProcess(), &job.thread, 0, FALSE,
                       DUPLICATE_SAME_ACCESS)) {
    return false;
  }
  HANDLE helper = CreateThread(nullptr, 0, BreakThread, &job, 0, nullptr);
  if (helper != nullptr) {
    WaitForSingleObject(helper, 2000);
    CloseHandle(helper);
  }
  CloseHandle(job.thread);
  return job.ok;
}

void ReadersLog(uint64_t frame) {
  using namespace frame_time_math;
  Sort(&g_readers);
  ModLog("frame time   : test key frame_time_test_readers: %d instructions of "
         "the game read or wrote the frame time from frame %llu to frame "
         "%llu. The player was in the Temple for %llu of those frames. %u "
         "traps came from other code, and %u reads had no room in the list. "
         "Each line has the address after the instruction, and for each part "
         "of a frame the count and the least and the most number",
         g_readers.used, g_readers_start, frame, g_readers_temple,
         g_other_code, g_readers.dropped);
  for (int i = 0; i < g_readers.used; ++i) {
    const Reader& r = g_readers.list[i];
    ModLog("frame time   : reader 0x%08X: outside the hook of the scene walk "
           "%u (%u to %u ms), in the hook and not in a pass %u (%u to %u ms), "
           "eye pass 0 %u (%u to %u ms), eye pass 1 %u (%u to %u ms)",
           r.after, r.count[kOutside], r.least[kOutside], r.most[kOutside],
           r.count[kHook], r.least[kHook], r.most[kHook], r.count[kPass0],
           r.least[kPass0], r.most[kPass0], r.count[kPass1], r.least[kPass1],
           r.most[kPass1]);
  }
}

void ReadersFrame(uint64_t frame) {
  if (g_readers_frame <= 0 || g_readers_done) return;
  if (!g_armed) {
    if (frame < static_cast<uint64_t>(g_readers_frame)) return;
    if (g_trap_handler == nullptr) {
      g_trap_handler = AddVectoredExceptionHandler(1, ReaderTrap);
    }
    g_readers = frame_time_math::Readers();
    g_other_code = 0;
    g_readers_temple = 0;
    g_readers_start = frame;
    g_copy = *DeltaTime();
    g_armed = true;
    if (g_trap_handler == nullptr || !SetBreak(true)) {
      g_armed = false;
      g_readers_done = true;
      ModLog("frame time   : test key frame_time_test_readers: the data "
             "breakpoint cannot be set, so the readers are not counted, "
             "frame %llu", frame);
      return;
    }
    ModLog("frame time   : test key frame_time_test_readers: a data "
           "breakpoint is on the frame time for %d frames, frame %llu",
           g_readers_frames, frame);
    return;
  }
  if (TempleInside()) ++g_readers_temple;
  if (frame < g_readers_start + static_cast<uint64_t>(g_readers_frames)) {
    return;
  }
  const bool off = SetBreak(false);
  g_armed = false;
  g_readers_done = true;
  if (!off) {
    ModLog("frame time   : test key frame_time_test_readers: the data "
           "breakpoint cannot be removed, frame %llu", frame);
  }
  ReadersLog(frame);
}

}  // namespace

void FrameTimeSetOptions(int real, int test_frame, int readers_frame,
                         int readers_frames) {
  g_real = real != 0 ? kReal : kKept;
  g_test_frame = test_frame;
  g_readers_frame = readers_frame;
  if (readers_frames > 0) g_readers_frames = readers_frames;
  ModLog("frame time   : frame_time_real=%d. %s", g_real, RuleText(g_real));
}

// The test key (Ctrl, Shift and the star key of the number pad, test_keys=1).
// A long pulse for the real time and a short pulse for the number from before
// the passes.
void FrameTimeToggle(const char* cause) {
  g_real = g_real == kReal ? kKept : kReal;
  ControllersGesturePulse(0.5f, g_real == kReal ? 0.30f : 0.08f);
  ModLog("frame time   : %s: frame_time_real is %d now. %s. The number from "
         "before the passes is %u ms in this session, frame %llu", cause,
         g_real, RuleText(g_real), g_kept, ControllersFrameCount());
}

// The value of frame_time_real for the debug page.
int FrameTimeValue() { return g_real; }

// The part of a frame that runs, for the list of the readers.
void FrameTimeHook(bool inside) {
  g_part = inside ? frame_time_math::kHook : frame_time_math::kOutside;
}

// Before the two eye passes: the sample that StartFrame kept at its last
// call.
void FrameTimeBeforePasses() {
  const uint64_t frame = ControllersFrameCount();
  // A test with no key press: the test key acts at one frame.
  if (g_test_frame > 0) {
    const uint64_t at = static_cast<uint64_t>(g_test_frame);
    static uint64_t done_frame = 0;
    if (frame != done_frame && ControllersTestFrame(at, frame) == at) {
      done_frame = frame;
      FrameTimeToggle("test key frame_time_test_frame");
    }
  }
  ReadersFrame(frame);
  const int32_t* clock = reinterpret_cast<const int32_t*>(kClockSample);
  g_have_clock = !IsBadReadPtr(clock, sizeof(int32_t)) &&
                 !IsBadReadPtr(DeltaTime(), sizeof(uint32_t));
  g_clock_before = g_have_clock ? *clock : 0;
  g_pass[0] = g_pass[1] = 0;
}

void FrameTimePass(int eye) {
  if (g_armed) g_copy = *DeltaTime();
  g_part = eye == 0 ? frame_time_math::kPass0 : frame_time_math::kPass1;
}

// After the scene walk of one eye: the number that StartFrame gave the pass.
void FrameTimePassDone(int eye) {
  g_part = frame_time_math::kHook;
  if (g_have_clock && (eye == 0 || eye == 1)) g_pass[eye] = *DeltaTime();
}

// After the two eye passes: the frame time that the game gets. `before_passes`
// is the number from before the passes.
uint32_t FrameTimeAfterPasses(uint32_t before_passes) {
  if (!g_have_clock) return before_passes;
  const int32_t now = *reinterpret_cast<const int32_t*>(kClockSample);
  const uint32_t real = frame_time_math::FromClock(g_clock_before, now);
  g_kept = frame_time_math::Kept(g_have_wrote, g_kept, g_wrote, g_clock_wrote,
                                 before_passes, g_clock_before);
  const uint32_t given =
      frame_time_math::AfterPasses(g_real, g_kept, g_clock_before, now);
  frame_time_math::Add(&g_window, given, real, g_pass[0], g_pass[1]);
  g_wrote = given;
  g_clock_wrote = now;
  g_have_wrote = true;
  g_copy = given;
  return given;
}

void FrameTimeReport() {
  using namespace frame_time_math;
  const Window& w = g_window;
  if (w.frames == 0) return;
  ModLog("frame time   : after the two eye passes the game had %.1f ms as its "
         "frame time (%u to %u ms, frame_time_real=%d). The real time of a "
         "frame was %.1f ms on the clock of the game, so a move of the game "
         "that counts this number took %.2f times its own time. Eye pass 0 "
         "got %.1f ms and eye pass 1 got %.1f ms, %llu frames",
         Mean(w.given, w.frames), w.least, w.most, g_real,
         Mean(w.real, w.frames), TimesAsLong(w), Mean(w.pass[0], w.frames),
         Mean(w.pass[1], w.frames), w.frames);
  g_window = Window();
}
