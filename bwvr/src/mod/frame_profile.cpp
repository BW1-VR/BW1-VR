// THE FRAME COST MEASURE (profile=1 and profile=2, test keys).
//
// The report of stereo.cpp gives the mean of 450 frames and the worst frame.
// It does not say what a slow frame is made of, and its windows do not start
// and end with a scene. This file keeps the time of each frame and of each
// part of it, and it writes a summary for a group of frames. The summary has
// the mean, the median, the 99th percentile and the worst frame, and the
// mean of each part. For the slow frames it names the part that is the cause.
//
// A FRAME is one turn of the frame loop of GGame::Loop (0x0054D262 to
// 0x0054D460), from one call of ControlMap::ProcessActionsPerformed to the
// next. frame_timers.cpp has the calls of that loop. The parts of a frame do
// not overlap, and their sum is the frame:
//
//   input             ProcessActionsPerformed, ProcessBufferedKeys and
//                     ProcessFrameInputs
//   game turn         GGame::ProcessNetworkPackets. It runs one game turn
//                     when 100 ms of game time are over
//                     (GGame::LocalTimerSaysDoATurn, 0x0054C4A0)
//   before the scene  GGame::ProcessGraphicsEngine before the stereo hook:
//                     the mouse and the camera of the game
//   mod frame         the stereo hook less the four parts below: the head,
//                     the frame functions of the mod, the frame to the
//                     runtime, and the report of each 450 frames
//   wait              the wait for the runtime of the headset
//   eye 0, eye 1      the scene walk of each eye pass
//   to the headset    the copy of each eye picture to the bridge
//   after the scene   the rest of GGame::ProcessGraphicsEngine
//   desktop copy      GGame::FlipScreen: the copy to the monitor. The mod
//                     skips that copy while a headset shows the frames
//   other             the rest of the loop: ScriptedScreenShot::Process,
//                     GPlayer::SavePlayerAlignment,
//                     CreatureRoom::ProcessScreenShot and the network flush
//
// Some timers are inside those parts: the Direct3D draw calls, the lines of
// the log, the surface locks, the file calls and Sleep. They say what a part
// is made of. For Sleep, the surface calls and ReadFile the summary names the
// code that made the calls with the most time. For the longest call it also
// names the code of the game on the stack above it.
//
// A GROUP is a scene or a window. The word "bench" of the scripted controller
// starts a scene of a fixed count of frames (tools/desk/bench_run.ps1). A run
// with no such word has a window for each 450 frames, so a session in a
// headset gets the same lines.
//
// THE COST. With profile=0 nothing here runs: no hook of this file is in, and
// each caller tests one flag. With profile=1 or 2 a frame has about 60 calls
// of QueryPerformanceCounter for the parts. Each surface call, each file call
// and each line of the log has one more pair. profile=1 also has a pair for
// each Direct3D draw call (draw_probe.cpp, draw_hooks.cpp). The mod makes the
// summary of a group between two frames, so its time is in no frame.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "frame_profile.h"
#include "frame_stats_math.h"

extern void ModLog(const char* format, ...);
// draw_merge.cpp: the batch of the merge of draw calls goes to Direct3D
// before the game reads or writes a surface.
void DrawMergeFlush();
// exit_test.cpp: the hooks of CreateFileA and CreateFileW. They give the time
// of each call to this file.
int ExitTestFileHooks();
// game_start.cpp: the clock of the log.
double SecondsSinceProcessStart();

bool g_frame_profile_on = false;

namespace {

// The game turns that the game has made: GGame + 0x205A40. A game turn is due
// when the game time is this number times 100 ms (0x0054C4EA).
const uintptr_t kGameTurnOffset = 0x205A40;

// IDirectDrawSurface7 vtable slots.
const int kSlotBltFast = 7;
const int kSlotLock = 25;
const int kSlotUnlock = 32;

const int kMaxFrames = 4096;   // the most frames of one scene
const int kWindowFrames = 450;
const int kMaxFileLines = 40;
const float kFileLineMs = 2.0f;

enum Part {
  kPartInput,
  kPartTurn,
  kPartBefore,
  kPartMod,
  kPartWait,
  kPartEye0,
  kPartEye1,
  kPartGrab,
  kPartAfter,
  kPartFlip,
  kPartOther,
  kParts
};
const char* const kPartName[kParts] = {
    "input",  "game turn", "before the scene", "mod frame",
    "wait",   "eye 0",     "eye 1",            "to the headset",
    "after the scene", "desktop copy", "other"};

const int kInside = kFpTimers - kFpFirstInside;
const char* const kInsideName[kInside] = {
    "frame functions", "pass functions", "frame to the runtime", "report",
    "Direct3D draw calls", "log", "surface locks", "surface copies",
    "file opens", "file reads", "sleep"};

const char* const kFunctionName[kFpFunctions] = {
    "controllers", "hand",    "teleport", "Creature command", "miracle",
    "village",     "overlay", "water",    "table"};

// The frame that runs now.
struct Current {
  bool started;
  bool stereo;
  uint64_t start;
  uint64_t timer[kFpTimers];
  uint32_t count[kFpcCounters];
  uint64_t graphics_in;
  uint64_t hook_in;
  uint64_t hook_out;
  uint64_t before;
  uint64_t hook;
  uint64_t after;
  uint32_t frame;
  char file[72];
};

// One frame of a group.
struct Frame {
  float total;
  float part[kParts];
  float inside[kInside];
  uint32_t turns;
  uint32_t draws;
  uint32_t vertices;
  uint32_t frame;
};

int g_level = 0;
bool g_installed = false;
DWORD g_thread = 0;
double g_ms_per_tick = 0.0;
Current g_cur;
Frame* g_frames = nullptr;
float* g_values = nullptr;
float* g_scratch = nullptr;
int g_count = 0;
// The counts of the group, and the frame functions of the mod in it.
uint64_t g_group_count[kFpcCounters];
uint64_t g_function_ticks[kFpFunctions];
uint64_t g_function_worst[kFpFunctions];
// The file of the slowest frame of the group so far.
float g_slowest = 0.0f;
char g_slowest_file[72] = "";
int g_file_lines = 0;
// Who made the calls of Windows and of DirectDraw in the group.
const int kChain = 10;
struct Caller {
  int kind;
  uintptr_t address;
  uint32_t asked;
  uint64_t calls;
  uint64_t ticks;
  uint64_t worst;
  // The code of the game on the stack above the longest call.
  uintptr_t chain[kChain];
  int chain_count;
};
const int kMaxCallers = 64;
const int kCallersShown = 4;
// A call that is shorter than this gets no stack.
const float kChainLeastMs = 0.1f;
uint64_t g_chain_least_ticks = 0;
// The code of runblack.exe.
uintptr_t g_code_begin = 0;
uintptr_t g_code_end = 0;
Caller g_callers[kMaxCallers];
int g_caller_count = 0;
const char* const kCallKindName[kFpCallKinds] = {
    "Sleep", "the surface locks", "the surface copies", "ReadFile"};
// The game turns at the end of the frame before. -1 before the first frame.
int64_t g_last_turn = -1;
// The scenes of the word "bench".
uint64_t g_scene_ask = 0;
bool g_in_scene = false;
int g_scene_frames = 0;
int g_scenes = 0;
bool g_said_off = false;
uint64_t g_no_stereo = 0;

float Ms(uint64_t ticks) {
  return static_cast<float>(static_cast<double>(ticks) * g_ms_per_tick);
}

bool OnThread() {
  return g_frame_profile_on && GetCurrentThreadId() == g_thread;
}

int64_t GameTurn() {
  const uintptr_t game =
      *reinterpret_cast<const uintptr_t*>(bw::game::kGGame);
  if (game == 0) return -1;
  return *reinterpret_cast<const uint32_t*>(game + kGameTurnOffset);
}

void StartGroup() {
  g_count = 0;
  memset(g_group_count, 0, sizeof(g_group_count));
  memset(g_function_ticks, 0, sizeof(g_function_ticks));
  memset(g_function_worst, 0, sizeof(g_function_worst));
  g_slowest = 0.0f;
  g_slowest_file[0] = 0;
  g_no_stereo = 0;
  g_caller_count = 0;
}

// The module of an address and the offset in it, for the log.
void Place(uintptr_t address, char* out, size_t size) {
  HMODULE module = nullptr;
  char path[MAX_PATH] = "";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(address), &module) &&
      module != nullptr &&
      GetModuleFileNameA(module, path, MAX_PATH) != 0) {
    const char* name = strrchr(path, '\\');
    _snprintf_s(out, size, _TRUNCATE, "0x%08X (%s+0x%X)",
                static_cast<unsigned>(address),
                name != nullptr ? name + 1 : path,
                static_cast<unsigned>(address -
                                      reinterpret_cast<uintptr_t>(module)));
    return;
  }
  _snprintf_s(out, size, _TRUNCATE, "0x%08X", static_cast<unsigned>(address));
}

// THE CODE OF THE GAME ABOVE A CALL. The game has no frame pointers, so a
// walk of the frames is not possible. This reads the stack from the return
// address of the call to the base of the stack. It keeps each value that is
// an address in the code of runblack.exe with a call instruction before it.
// A value of an old call can still be on the stack, so the list is a hint.
// The listing of each address says if it is a real caller.
void FindCode() {
  const uint8_t* base =
      reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
  if (base == nullptr) return;
  const IMAGE_DOS_HEADER* dos =
      reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const IMAGE_NT_HEADERS* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
  for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
    if ((section[i].Characteristics & IMAGE_SCN_CNT_CODE) == 0) continue;
    g_code_begin =
        reinterpret_cast<uintptr_t>(base) + section[i].VirtualAddress;
    g_code_end = g_code_begin + section[i].Misc.VirtualSize;
    return;
  }
}

bool AfterCall(uintptr_t address) {
  if (address < g_code_begin + 8 || address >= g_code_end) return false;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(address);
  if (p[-5] == 0xE8) return true;                             // call rel32
  if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;   // call reg
  if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;   // call [reg+8]
  if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;   // call [reg+32]
  return false;
}

int StackChain(const void* stack, uintptr_t* out) {
  if (stack == nullptr || g_code_begin == 0) return 0;
  const uintptr_t* at = static_cast<const uintptr_t*>(stack) + 1;
  const uintptr_t* end = static_cast<const uintptr_t*>(
      reinterpret_cast<const NT_TIB*>(NtCurrentTeb())->StackBase);
  int count = 0;
  for (int i = 0; i < 600 && at + i < end && count < kChain; ++i) {
    if (AfterCall(at[i])) out[count++] = at[i];
  }
  return count;
}

// One line for each kind of call: the callers with the most time.
void SummarizeCallers(const char* label) {
  for (int kind = 0; kind < kFpCallKinds; ++kind) {
    bool used[kMaxCallers] = {false};
    char line[1200];
    line[0] = 0;
    int shown = 0, more = 0;
    for (int rank = 0; rank < g_caller_count; ++rank) {
      int best = -1;
      for (int i = 0; i < g_caller_count; ++i) {
        if (g_callers[i].kind != kind || used[i]) continue;
        if (best < 0 || g_callers[i].ticks > g_callers[best].ticks) best = i;
      }
      if (best < 0) break;
      used[best] = true;
      if (shown >= kCallersShown) {
        ++more;
        continue;
      }
      ++shown;
      const Caller& c = g_callers[best];
      char place[160], one[280];
      Place(c.address, place, sizeof(place));
      if (kind == kFpCallSleep) {
        _snprintf_s(one, sizeof(one), _TRUNCATE,
                    "%s%s asked %u ms %llu times, %.2f ms in all, worst %.2f",
                    line[0] != 0 ? ". " : "", place, c.asked, c.calls,
                    Ms(c.ticks), Ms(c.worst));
      } else if (kind == kFpCallLock && c.asked != 0) {
        _snprintf_s(one, sizeof(one), _TRUNCATE,
                    "%s%s a surface of %u by %u, %llu times, %.2f ms in all, "
                    "worst %.2f", line[0] != 0 ? ". " : "", place,
                    c.asked >> 16, c.asked & 0xFFFF, c.calls, Ms(c.ticks),
                    Ms(c.worst));
      } else {
        _snprintf_s(one, sizeof(one), _TRUNCATE,
                    "%s%s %llu times, %.2f ms in all, worst %.2f",
                    line[0] != 0 ? ". " : "", place, c.calls, Ms(c.ticks),
                    Ms(c.worst));
      }
      strcat_s(line, sizeof(line), one);
      // The first caller of the kind, when a call of it was long.
      if (shown == 1 && c.chain_count > 0) {
        char chain[kChain * 12 + 8];
        chain[0] = 0;
        for (int k = 0; k < c.chain_count; ++k) {
          char hex[16];
          _snprintf_s(hex, sizeof(hex), _TRUNCATE, "%s0x%08X",
                      k > 0 ? " " : "", static_cast<unsigned>(c.chain[k]));
          strcat_s(chain, sizeof(chain), hex);
        }
        ModLog("frame cost   : %s, the longest call of %s from 0x%08X took "
               "%.2f ms. Code of the game on the stack above it, the nearest "
               "first: %s", label, kCallKindName[kind],
               static_cast<unsigned>(c.address), Ms(c.worst), chain);
      }
    }
    if (shown == 0) continue;
    ModLog("frame cost   : %s, the callers of %s with the most time: %s. %d "
           "more callers", label, kCallKindName[kind], line, more);
  }
}

// Adds "name value" for each number of a list to a line.
void AddList(char* line, size_t size, const char* const* names,
             const float* values, int count, const char* format) {
  for (int i = 0; i < count; ++i) {
    char one[96];
    _snprintf_s(one, sizeof(one), _TRUNCATE, format, i > 0 ? ", " : "",
                names[i], values[i]);
    strcat_s(line, size, one);
  }
}

// The lines of one group. `label` is "scene N" or "window".
void Summarize(const char* label) {
  using frame_stats_math::Summary;
  const int n = g_count;
  if (n < 2 || g_frames == nullptr) return;

  // The frame, and the frame less the copy to the monitor. A headset session
  // has no such copy, so the second number is the nearer one to it.
  double seconds = 0.0;
  for (int i = 0; i < n; ++i) {
    g_values[i] = g_frames[i].total;
    seconds += g_frames[i].total;
  }
  seconds *= 0.001;
  const Summary all = frame_stats_math::Summarize(g_values, n, g_scratch);
  for (int i = 0; i < n; ++i) {
    g_values[i] = g_frames[i].total - g_frames[i].part[kPartFlip];
  }
  const Summary work = frame_stats_math::Summarize(g_values, n, g_scratch);
  ModLog("frame cost   : %s, %d frames in %.2f s (%.1f fps), frames %u to %u "
         "of the controllers. Frame ms: mean %.2f, median %.2f, 99th "
         "percentile %.2f, worst %.2f. Less the desktop copy: mean %.2f, "
         "median %.2f, 99th percentile %.2f, worst %.2f",
         label, n, seconds, seconds > 0.0 ? n / seconds : 0.0,
         g_frames[0].frame, g_frames[n - 1].frame, all.mean, all.median,
         all.p99, all.worst, work.mean, work.median, work.p99, work.worst);

  float mean[kParts], median[kParts];
  for (int p = 0; p < kParts; ++p) {
    for (int i = 0; i < n; ++i) g_values[i] = g_frames[i].part[p];
    const Summary s = frame_stats_math::Summarize(g_values, n, g_scratch);
    mean[p] = s.mean;
    median[p] = s.median;
  }
  char line[1600];
  line[0] = 0;
  AddList(line, sizeof(line), kPartName, mean, kParts, "%s%s %.3f");
  ModLog("frame cost   : %s, mean ms of a frame by part: %s", label, line);

  float inside[kInside];
  for (int k = 0; k < kInside; ++k) {
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += g_frames[i].inside[k];
    inside[k] = static_cast<float>(sum / n);
  }
  line[0] = 0;
  AddList(line, sizeof(line), kInsideName, inside, kInside, "%s%s %.3f");
  ModLog("frame cost   : %s, inside those parts, mean ms of a frame: %s",
         label, line);

  float functions[kFpFunctions];
  line[0] = 0;
  for (int k = 0; k < kFpFunctions; ++k) {
    functions[k] = Ms(g_function_ticks[k]) / static_cast<float>(n);
    char one[96];
    _snprintf_s(one, sizeof(one), _TRUNCATE, "%s%s %.3f (%.3f)",
                k > 0 ? ", " : "", kFunctionName[k], functions[k],
                Ms(g_function_worst[k]));
    strcat_s(line, sizeof(line), one);
  }
  ModLog("frame cost   : %s, the frame functions of the mod, mean ms of a "
         "frame (and the worst call): %s", label, line);

  // The game turns. A frame with a turn is not a slow frame by itself: the
  // numbers say how long a turn is.
  uint64_t draws = 0, vertices = 0, turns = 0;
  int with_turn = 0;
  for (int i = 0; i < n; ++i) {
    draws += g_frames[i].draws;
    vertices += g_frames[i].vertices;
    turns += g_frames[i].turns;
    if (g_frames[i].turns > 0) {
      g_values[with_turn++] = g_frames[i].part[kPartTurn];
    }
  }
  const Summary turn =
      frame_stats_math::Summarize(g_values, with_turn, g_scratch);
  ModLog("frame cost   : %s, %llu game turn%s in %d frame%s. The turn of such "
         "a frame, ms: mean %.2f, median %.2f, 99th percentile %.2f, worst "
         "%.2f. A frame has %.0f draw calls and %.0f vertices in both "
         "passes. In the %d frames: %llu surface locks, %llu surface copies, "
         "%llu file opens, %llu file reads, %llu lines of the log, %llu "
         "calls of Sleep",
         label, turns, turns == 1 ? "" : "s", with_turn,
         with_turn == 1 ? "" : "s", turn.mean, turn.median, turn.p99,
         turn.worst, static_cast<double>(draws) / n,
         static_cast<double>(vertices) / n, n, g_group_count[kFpcLocks],
         g_group_count[kFpcCopies], g_group_count[kFpcOpens],
         g_group_count[kFpcReads], g_group_count[kFpcLogLines],
         g_group_count[kFpcSleeps]);

  // The state count of profile=3: the draw calls that a merge can save.
  if (g_level == 3 && draws > 0) {
    const double frames = static_cast<double>(n);
    const double runs = static_cast<double>(g_group_count[kFpcRuns]);
    ModLog("frame cost   : %s, the state count, means of a frame in both "
           "passes: %.0f draw calls in %.0f runs with one state, which is "
           "%.1f percent fewer draw calls. A run starts at a change of a "
           "texture only %.0f times, at a change of a render state or of a "
           "stage state %.0f times, and for a different cause %.0f times. "
           "%.0f draw calls are not triangle lists. The game makes %.0f "
           "calls for render states, stage states and textures, and %.0f of "
           "them change a value. %.0f calls of BeginScene and EndScene and "
           "%.0f other calls end a run",
           label, static_cast<double>(draws) / frames, runs / frames,
           100.0 * (1.0 - runs / static_cast<double>(draws)),
           static_cast<double>(g_group_count[kFpcRunsTexture]) / frames,
           static_cast<double>(g_group_count[kFpcRunsState]) / frames,
           static_cast<double>(g_group_count[kFpcRunsOther]) / frames,
           static_cast<double>(g_group_count[kFpcNotLists]) / frames,
           static_cast<double>(g_group_count[kFpcStateCalls]) / frames,
           static_cast<double>(g_group_count[kFpcStateChanges]) / frames,
           static_cast<double>(g_group_count[kFpcSceneCalls]) / frames,
           static_cast<double>(g_group_count[kFpcOtherCalls]) / frames);
  }

  // The slow frames, and the part of each one that is the cause. The test
  // is on the frame less the copy to the monitor. So the limit is the one of
  // a headset session, and a slow copy to the monitor makes no slow frame.
  const float limit = frame_stats_math::SlowLimit(work.median);
  int slow = 0, slow_turn = 0, worst_index = 0;
  int cause_count[kParts] = {0};
  double cause_over[kParts] = {0.0};
  float cause_worst[kParts] = {0.0f};
  double slow_inside[kInside] = {0.0};
  median[kPartFlip] = 1e30f;
  for (int i = 0; i < n; ++i) {
    const Frame& f = g_frames[i];
    const float f_work = f.total - f.part[kPartFlip];
    if (f_work > g_frames[worst_index].total -
                     g_frames[worst_index].part[kPartFlip]) {
      worst_index = i;
    }
    if (!(f_work > limit)) continue;
    ++slow;
    if (f.turns > 0) ++slow_turn;
    for (int k = 0; k < kInside; ++k) slow_inside[k] += f.inside[k];
    float over = 0.0f;
    const int cause = frame_stats_math::MostOver(f.part, median, kParts, &over);
    if (cause < 0) continue;
    ++cause_count[cause];
    cause_over[cause] += over;
    if (over > cause_worst[cause]) cause_worst[cause] = over;
  }
  line[0] = 0;
  for (int p = 0; p < kParts; ++p) {
    if (cause_count[p] == 0) continue;
    char one[128];
    _snprintf_s(one, sizeof(one), _TRUNCATE,
                "%s%s in %d (mean %.2f ms over, worst %.2f)",
                line[0] != 0 ? ", " : "", kPartName[p], cause_count[p],
                cause_over[p] / cause_count[p], cause_worst[p]);
    strcat_s(line, sizeof(line), one);
  }
  ModLog("frame cost   : %s, %d slow frame%s of %d (over %.2f ms less the "
         "desktop copy), %d of them with a game turn. The part that is the "
         "most over its median: %s",
         label, slow, slow == 1 ? "" : "s", n, limit, slow_turn,
         slow > 0 ? line : "none");
  if (slow > 0) {
    float slow_mean[kInside];
    for (int k = 0; k < kInside; ++k) {
      slow_mean[k] = static_cast<float>(slow_inside[k] / slow);
    }
    line[0] = 0;
    AddList(line, sizeof(line), kInsideName, slow_mean, kInside, "%s%s %.3f");
    ModLog("frame cost   : %s, inside the slow frames, mean ms of a frame: %s",
           label, line);
  }

  const Frame& w = g_frames[worst_index];
  line[0] = 0;
  AddList(line, sizeof(line), kPartName, w.part, kParts, "%s%s %.3f");
  char inside_line[900];
  inside_line[0] = 0;
  AddList(inside_line, sizeof(inside_line), kInsideName, w.inside, kInside,
          "%s%s %.3f");
  ModLog("frame cost   : %s, the worst frame is frame %u of the controllers, "
         "%.2f ms less the desktop copy, with %u game turn%s and %u draw "
         "call%s. Its parts, ms: %s. Inside them: %s. %s%s",
         label, w.frame, w.total - w.part[kPartFlip], w.turns,
         w.turns == 1 ? "" : "s", w.draws, w.draws == 1 ? "" : "s", line,
         inside_line,
         g_slowest_file[0] != 0 ? "The last file that it opened: "
                                : "It opened no file",
         g_slowest_file);
  SummarizeCallers(label);
  if (g_no_stereo > 0) {
    ModLog("frame cost   : %s, %llu more frames had no eye passes, and they "
           "are not in the numbers", label, g_no_stereo);
  }
}

// The frame that ran is over at `now`.
void CloseFrame(uint64_t now) {
  const int64_t turn = GameTurn();
  uint32_t turns = 0;
  if (turn >= 0 && g_last_turn >= 0 && turn > g_last_turn) {
    turns = static_cast<uint32_t>(turn - g_last_turn);
  }
  g_last_turn = turn;
  if (!g_cur.stereo) {
    ++g_no_stereo;
    return;
  }
  if (g_frames == nullptr || g_count >= kMaxFrames) return;

  Frame& f = g_frames[g_count];
  f.total = Ms(now - g_cur.start);
  f.part[kPartInput] = Ms(g_cur.timer[kFpInput]);
  f.part[kPartTurn] = Ms(g_cur.timer[kFpTurn]);
  f.part[kPartBefore] = Ms(g_cur.before);
  f.part[kPartWait] = Ms(g_cur.timer[kFpWait]);
  f.part[kPartEye0] = Ms(g_cur.timer[kFpEye0]);
  f.part[kPartEye1] = Ms(g_cur.timer[kFpEye1]);
  f.part[kPartGrab] = Ms(g_cur.timer[kFpGrab]);
  const uint64_t named = g_cur.timer[kFpWait] + g_cur.timer[kFpEye0] +
                         g_cur.timer[kFpEye1] + g_cur.timer[kFpGrab];
  f.part[kPartMod] = Ms(g_cur.hook > named ? g_cur.hook - named : 0);
  f.part[kPartAfter] = Ms(g_cur.after);
  f.part[kPartFlip] = Ms(g_cur.timer[kFpFlip]);
  float sum = 0.0f;
  for (int p = 0; p < kPartOther; ++p) sum += f.part[p];
  f.part[kPartOther] = f.total > sum ? f.total - sum : 0.0f;
  for (int k = 0; k < kInside; ++k) {
    f.inside[k] = Ms(g_cur.timer[kFpFirstInside + k]);
  }
  f.turns = turns;
  f.draws = g_cur.count[kFpcDraws];
  f.vertices = g_cur.count[kFpcVertices];
  f.frame = g_cur.frame;
  ++g_count;
  for (int c = 0; c < kFpcCounters; ++c) g_group_count[c] += g_cur.count[c];
  if (f.total - f.part[kPartFlip] > g_slowest) {
    g_slowest = f.total - f.part[kPartFlip];
    strcpy_s(g_slowest_file, g_cur.file);
  }
  // A frame that waits for a file is rare, and its line names the file.
  const float files = f.inside[kFpOpen - kFpFirstInside] +
                      f.inside[kFpRead - kFpFirstInside];
  if (files >= kFileLineMs && g_file_lines < kMaxFileLines) {
    ++g_file_lines;
    // The part of the frame that holds the file calls: the longest part but
    // the copy to the monitor.
    int longest = kPartInput;
    for (int p = 0; p < kParts; ++p) {
      if (p != kPartFlip && f.part[p] > f.part[longest]) longest = p;
    }
    ModLog("frame cost   : frame %u of the controllers took %.2f ms, with "
           "%.2f ms in %u file opens and %u file reads. Its longest part is "
           "\"%s\", %.2f ms. The last file that it opened: %s%s",
           f.frame, f.total, files, g_cur.count[kFpcOpens],
           g_cur.count[kFpcReads], kPartName[longest], f.part[longest],
           g_cur.file[0] != 0 ? g_cur.file : "none",
           g_file_lines == kMaxFileLines
               ? ". The log gets no more lines of this kind" : "");
  }
}

// --- the hooks of the measure ----------------------------------------------

typedef HRESULT(WINAPI* LockFn)(IDirectDrawSurface7*, LPRECT,
                                LPDDSURFACEDESC2, DWORD, HANDLE);
typedef HRESULT(WINAPI* UnlockFn)(IDirectDrawSurface7*, LPRECT);
typedef HRESULT(WINAPI* BltFastFn)(IDirectDrawSurface7*, DWORD, DWORD,
                                   IDirectDrawSurface7*, LPRECT, DWORD);
typedef BOOL(WINAPI* ReadFileFn)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef void(WINAPI* SleepFn)(DWORD);
LockFn o_Lock = nullptr;
UnlockFn o_Unlock = nullptr;
BltFastFn o_BltFast = nullptr;
ReadFileFn o_ReadFile = nullptr;
SleepFn o_Sleep = nullptr;

HRESULT WINAPI h_Lock(IDirectDrawSurface7* self, LPRECT rect,
                      LPDDSURFACEDESC2 desc, DWORD flags, HANDLE event) {
  DrawMergeFlush();
  if (!OnThread()) return o_Lock(self, rect, desc, flags, event);
  const uint64_t start = FrameProfileTicks();
  const HRESULT hr = o_Lock(self, rect, desc, flags, event);
  const uint64_t spent = FrameProfileTicks() - start;
  g_cur.timer[kFpLock] += spent;
  ++g_cur.count[kFpcLocks];
  uint32_t size = 0;
  if (SUCCEEDED(hr) && desc != nullptr) {
    size = (desc->dwWidth << 16) | (desc->dwHeight & 0xFFFF);
  }
  FrameProfileCaller(kFpCallLock, _ReturnAddress(), size, spent,
                     _AddressOfReturnAddress());
  return hr;
}

HRESULT WINAPI h_Unlock(IDirectDrawSurface7* self, LPRECT rect) {
  if (!OnThread()) return o_Unlock(self, rect);
  const uint64_t start = FrameProfileTicks();
  const HRESULT hr = o_Unlock(self, rect);
  const uint64_t spent = FrameProfileTicks() - start;
  g_cur.timer[kFpLock] += spent;
  FrameProfileCaller(kFpCallLock, _ReturnAddress(), 0, spent,
                     _AddressOfReturnAddress());
  return hr;
}

HRESULT WINAPI h_BltFast(IDirectDrawSurface7* self, DWORD x, DWORD y,
                         IDirectDrawSurface7* source, LPRECT rect,
                         DWORD flags) {
  DrawMergeFlush();
  if (!OnThread()) return o_BltFast(self, x, y, source, rect, flags);
  const uint64_t start = FrameProfileTicks();
  const HRESULT hr = o_BltFast(self, x, y, source, rect, flags);
  const uint64_t spent = FrameProfileTicks() - start;
  g_cur.timer[kFpLock] += spent;
  ++g_cur.count[kFpcLocks];
  FrameProfileCaller(kFpCallLock, _ReturnAddress(), 0, spent,
                     _AddressOfReturnAddress());
  return hr;
}

BOOL WINAPI h_ReadFile(HANDLE file, LPVOID buffer, DWORD bytes, LPDWORD got,
                       LPOVERLAPPED overlapped) {
  if (!OnThread()) return o_ReadFile(file, buffer, bytes, got, overlapped);
  const uint64_t start = FrameProfileTicks();
  const BOOL ok = o_ReadFile(file, buffer, bytes, got, overlapped);
  const uint64_t spent = FrameProfileTicks() - start;
  g_cur.timer[kFpRead] += spent;
  ++g_cur.count[kFpcReads];
  FrameProfileCaller(kFpCallRead, _ReturnAddress(), 0, spent,
                     _AddressOfReturnAddress());
  return ok;
}

void WINAPI h_Sleep(DWORD ms) {
  if (!OnThread()) {
    o_Sleep(ms);
    return;
  }
  const uint64_t start = FrameProfileTicks();
  o_Sleep(ms);
  const uint64_t spent = FrameProfileTicks() - start;
  g_cur.timer[kFpSleep] += spent;
  ++g_cur.count[kFpcSleeps];
  FrameProfileCaller(kFpCallSleep, _ReturnAddress(), ms, spent,
                     _AddressOfReturnAddress());
}

bool HookAt(void* target, void* replacement, void** original) {
  return target != nullptr &&
         MH_CreateHook(target, replacement, original) == MH_OK &&
         MH_EnableHook(target) == MH_OK;
}

}  // namespace

void FrameProfileSetLevel(int level) {
  g_level = level;
  if (level <= 0 || g_frames != nullptr) return;
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  if (frequency.QuadPart == 0) return;
  g_ms_per_tick = 1000.0 / static_cast<double>(frequency.QuadPart);
  g_chain_least_ticks =
      static_cast<uint64_t>(kChainLeastMs / g_ms_per_tick);
  FindCode();
  g_frames = static_cast<Frame*>(calloc(kMaxFrames, sizeof(Frame)));
  g_values = static_cast<float*>(calloc(kMaxFrames, sizeof(float)));
  g_scratch = static_cast<float*>(calloc(kMaxFrames, sizeof(float)));
  if (g_frames == nullptr || g_values == nullptr || g_scratch == nullptr) {
    return;
  }
  memset(&g_cur, 0, sizeof(g_cur));
  StartGroup();
  g_frame_profile_on = true;
}

void FrameProfileInstall() {
  if (!g_frame_profile_on || g_installed) return;
  IDirect3DDevice7** slot =
      reinterpret_cast<IDirect3DDevice7**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return;
  IDirectDrawSurface7* target = nullptr;
  if (FAILED((*slot)->GetRenderTarget(&target)) || target == nullptr) return;
  g_installed = true;

  int surface = 0;
  void** vtable = *reinterpret_cast<void***>(target);
  if (!IsBadReadPtr(vtable, sizeof(void*) * (kSlotUnlock + 1))) {
    surface += HookAt(vtable[kSlotLock], reinterpret_cast<void*>(&h_Lock),
                      reinterpret_cast<void**>(&o_Lock)) ? 1 : 0;
    surface += HookAt(vtable[kSlotUnlock], reinterpret_cast<void*>(&h_Unlock),
                      reinterpret_cast<void**>(&o_Unlock)) ? 1 : 0;
    surface += HookAt(vtable[kSlotBltFast],
                      reinterpret_cast<void*>(&h_BltFast),
                      reinterpret_cast<void**>(&o_BltFast)) ? 1 : 0;
  }
  target->Release();

  const HMODULE kernel = GetModuleHandleA("kernel32.dll");
  int system = 0;
  if (kernel != nullptr) {
    void* read = reinterpret_cast<void*>(GetProcAddress(kernel, "ReadFile"));
    void* sleep = reinterpret_cast<void*>(GetProcAddress(kernel, "Sleep"));
    system += HookAt(read, reinterpret_cast<void*>(&h_ReadFile),
                     reinterpret_cast<void**>(&o_ReadFile)) ? 1 : 0;
    system += HookAt(sleep, reinterpret_cast<void*>(&h_Sleep),
                     reinterpret_cast<void**>(&o_Sleep)) ? 1 : 0;
  }
  const int files = ExitTestFileHooks();
  ModLog("frame cost   : profile=%d. The measure times each frame of the "
         "frame loop of the game and its parts%s. Hooks of the measure: %d "
         "of 3 surface calls (Lock, Unlock, BltFast), %d of 2 of ReadFile "
         "and Sleep, %d of 2 of CreateFile. A run with no word \"bench\" "
         "gets one summary for each %d frames",
         g_level,
         g_level == 1   ? ", and each Direct3D draw call"
         : g_level == 3 ? ", each Direct3D draw call, and the state calls "
                          "between the draw calls"
                        : ", with no timer on the Direct3D draw calls",
         surface, system, files, kWindowFrames);
}

uint64_t FrameProfileTicks() {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<uint64_t>(now.QuadPart);
}

void FrameProfileAdd(int timer, uint64_t ticks) {
  if (!OnThread()) return;
  g_cur.timer[timer] += ticks;
}

void FrameProfileCount(int counter, uint32_t n) {
  if (!OnThread()) return;
  g_cur.count[counter] += n;
}

void FrameProfileFunction(int index, uint64_t ticks) {
  if (!OnThread()) return;
  g_cur.timer[kFpModFrame] += ticks;
  g_function_ticks[index] += ticks;
  if (ticks > g_function_worst[index]) g_function_worst[index] = ticks;
}

void FrameProfileCaller(int kind, const void* address, uint32_t asked,
                        uint64_t ticks, const void* stack) {
  if (!OnThread()) return;
  const uintptr_t at = reinterpret_cast<uintptr_t>(address);
  Caller* c = nullptr;
  for (int i = 0; i < g_caller_count; ++i) {
    if (g_callers[i].address == at && g_callers[i].kind == kind &&
        g_callers[i].asked == asked) {
      c = &g_callers[i];
      break;
    }
  }
  if (c == nullptr) {
    if (g_caller_count >= kMaxCallers) return;
    c = &g_callers[g_caller_count++];
    c->kind = kind;
    c->address = at;
    c->asked = asked;
    c->calls = 0;
    c->ticks = 0;
    c->worst = 0;
    c->chain_count = 0;
  }
  ++c->calls;
  c->ticks += ticks;
  if (ticks > c->worst) {
    c->worst = ticks;
    // One read of a file is short, and many of them make a long frame, so a
    // read gets its stack at each length.
    if (ticks >= g_chain_least_ticks || kind == kFpCallRead) {
      c->chain_count = StackChain(stack, c->chain);
    }
  }
}

void FrameProfileFile(const char* name) {
  if (!OnThread() || name == nullptr) return;
  // The end of the path, which has the name of the file.
  const size_t length = strlen(name);
  const size_t room = sizeof(g_cur.file) - 1;
  strcpy_s(g_cur.file, length > room ? name + (length - room) : name);
}

void FrameProfileLoopTop() {
  if (!g_frame_profile_on) return;
  const uint64_t now = FrameProfileTicks();
  g_thread = GetCurrentThreadId();
  if (g_cur.started) CloseFrame(now);

  if (g_scene_ask > 0) {
    // A scene starts with this frame. The frames before it are no group.
    g_scene_frames = static_cast<int>(g_scene_ask);
    g_scene_ask = 0;
    g_in_scene = true;
    ++g_scenes;
    StartGroup();
  } else if (g_in_scene && g_count >= g_scene_frames) {
    char label[32];
    _snprintf_s(label, sizeof(label), _TRUNCATE, "scene %d", g_scenes);
    Summarize(label);
    g_in_scene = false;
    StartGroup();
  } else if (!g_in_scene && g_scenes == 0 && g_count >= kWindowFrames) {
    Summarize("window");
    StartGroup();
  } else if (!g_in_scene && g_scenes > 0) {
    // Between two scenes no frame is kept.
    StartGroup();
  }

  memset(&g_cur, 0, sizeof(g_cur));
  g_cur.started = true;
  // After the summary, so its time is in no frame.
  g_cur.start = FrameProfileTicks();
}

void FrameProfileGraphics(bool begin) {
  if (!OnThread()) return;
  const uint64_t now = FrameProfileTicks();
  if (begin) {
    g_cur.graphics_in = now;
    g_cur.hook_out = 0;
    return;
  }
  if (g_cur.graphics_in == 0) return;
  if (g_cur.hook_out != 0) {
    g_cur.after += now - g_cur.hook_out;
  } else {
    g_cur.before += now - g_cur.graphics_in;
  }
  g_cur.graphics_in = 0;
}

void FrameProfileHook(bool begin) {
  if (!OnThread()) return;
  const uint64_t now = FrameProfileTicks();
  if (begin) {
    g_cur.hook_in = now;
    if (g_cur.graphics_in != 0 && g_cur.hook_out == 0) {
      g_cur.before += now - g_cur.graphics_in;
    }
    return;
  }
  if (g_cur.hook_in == 0) return;
  g_cur.hook += now - g_cur.hook_in;
  g_cur.hook_in = 0;
  g_cur.hook_out = now;
}

void FrameProfileStereoFrame(uint64_t frame) {
  if (!OnThread()) return;
  g_cur.stereo = true;
  g_cur.frame = static_cast<uint32_t>(frame);
}

void FrameProfileSceneStart(uint64_t frames, uint64_t frame) {
  if (!g_frame_profile_on) {
    if (!g_said_off) {
      g_said_off = true;
      ModLog("frame cost   : the word \"bench\" of the script does nothing "
             "with profile=0, frame %llu", frame);
    }
    return;
  }
  if (frames < 2) frames = 2;
  if (frames > static_cast<uint64_t>(kMaxFrames)) frames = kMaxFrames;
  g_scene_ask = frames;
  ModLog("frame cost   : scene %d starts with the next frame and has %llu "
         "frames, frame %llu of the controllers, %.1f s after the start of "
         "the game process",
         g_scenes + 1, frames, frame, SecondsSinceProcessStart());
}
