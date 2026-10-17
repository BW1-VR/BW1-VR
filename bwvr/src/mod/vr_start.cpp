// The tries to start the headset session on a thread of the mod, and the
// frame watch. See vr_start.h and docs/backend.md, "The start of the headset
// session".

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "vr_start.h"
#include "vr_start_math.h"
#include "../vr/xr_screen.h"

void ModLog(const char* format, ...);

namespace {

// A call into the runtime that has run this long gets a line in the log.
const uint32_t kSlowCallMs = 10000;
// The frame watch writes one line each this many milliseconds, and at most
// kWatchLines such lines. A try that never comes back must not fill the log.
const double kWatchLineMs = 30000.0;
const int kWatchLines = 8;

void LogNowhere(const char*, ...) {}

const char* ModeText(int mode) {
  switch (mode) {
    case vr_start_math::kOneInstance:
      return "on a thread of the mod, with one instance for all tries";
    case vr_start_math::kNewInstance:
      return "on a thread of the mod, with a new instance for each try";
    default:
      return "on the frame thread, with a new instance for each try";
  }
}

// THE STATE THAT THE TWO THREADS SHARE. g_lock guards each field of this
// group. No thread holds it in a call into the runtime, so the frame thread
// waits for it only while the other thread writes a field.
bool g_begun = false;  // the frame thread only
CRITICAL_SECTION g_lock;
VrStartOptions g_options = {};
HANDLE g_thread = nullptr;
// The frame thread sets it when it has news for the thread: a screen that
// comes back, a session that runs, or the stop.
HANDLE g_wake = nullptr;
bool g_quit = false;
bool g_over = false;                // the thread has ended its tries
int g_tries = 0;                    // the tries that the thread started
bwvr::XrScreen* g_ready = nullptr;  // a screen with a system
bwvr::XrScreen* g_back = nullptr;   // a screen whose session did not start
bool g_session_runs = false;
// The call into the runtime of now. Null between calls.
const char* g_call = nullptr;
int g_call_try = 0;
uint32_t g_call_start = 0;
uint32_t g_call_serial = 0;

// The frame thread only: the call that has its line about a slow call.
uint32_t g_slow_serial = 0;

struct Locked {
  Locked() { EnterCriticalSection(&g_lock); }
  ~Locked() { LeaveCriticalSection(&g_lock); }
};

// One call into the runtime on the thread. The shared fields name the call
// while it runs, so the frame thread can say in the log that it did not
// come back.
template <typename Call>
void InCall(const char* name, int try_number, Call call) {
  const uint32_t start = GetTickCount();
  {
    Locked locked;
    g_call = name;
    g_call_try = try_number;
    g_call_start = start;
    ++g_call_serial;
  }
  call();
  {
    Locked locked;
    g_call = nullptr;
  }
  const uint32_t spent = vr_start_math::TicksSince(start, GetTickCount());
  if (spent >= kSlowCallMs) {
    ModLog("vr start     : try %d: %s came back after %.1f s", try_number,
           name, spent / 1000.0);
  }
}

bool QuitAsked() {
  Locked locked;
  return g_quit;
}

// The end of a screen on the thread. Its session did not start, so Stop has
// only the instance to end (and the session of a screen that came back).
void EndScreen(bwvr::XrScreen* screen, int try_number) {
  if (screen == nullptr) return;
  if (screen->has_instance()) {
    InCall("the end of the instance (xrDestroyInstance)", try_number,
           [screen] { screen->Stop(); });
  }
  delete screen;
}

// Gives the screen to the frame thread, and waits for the answer. True: the
// session runs. False: the session did not start, and the screen is ended
// (the frame thread then falls back to vr_start=0), or the stop came. Each
// false ends the tries of the thread.
bool HandOver(bwvr::XrScreen* screen, int try_number) {
  {
    Locked locked;
    g_ready = screen;
  }
  for (;;) {
    bwvr::XrScreen* back = nullptr;
    bool runs = false;
    bool quit = false;
    {
      Locked locked;
      back = g_back;
      g_back = nullptr;
      runs = g_session_runs;
      quit = g_quit;
      // A stop before the frame thread took the screen.
      if (quit && back == nullptr && g_ready != nullptr) {
        back = g_ready;
        g_ready = nullptr;
      }
    }
    if (runs) return true;
    if (back != nullptr) {
      if (!quit) {
        ModLog("vr start     : try %d: the session did not start on the frame "
               "thread. The thread ends this instance and its tries",
               try_number);
      }
      EndScreen(back, try_number);
      return false;
    }
    if (quit) return false;
    WaitForSingleObject(g_wake, INFINITE);
  }
}

DWORD WINAPI TriesMain(void*) {
  using namespace vr_start_math;
  const VrStartOptions o = g_options;
  const uint32_t retry_ms = static_cast<uint32_t>(o.retry_ms);
  const uint32_t first = GetTickCount();
  bwvr::XrScreen* screen = nullptr;
  int tries = 0;
  bool session = false;
  for (;;) {
    if (QuitAsked()) break;
    const uint32_t started = GetTickCount();
    if (TriesOver(tries, TicksSince(first, started), o.max_tries, retry_ms)) {
      ModLog("vr           : no headset arrived, the game stays on the "
             "monitor");
      break;
    }
    ++tries;
    {
      Locked locked;
      g_tries = tries;
    }
    const bwvr::LogFn log = LoudTry(tries) ? &ModLog : &LogNowhere;
    if (screen == nullptr) screen = new bwvr::XrScreen();
    bool instance = screen->has_instance();
    if (instance) {
      screen->SetLog(log);
    } else {
      InCall("the call xrCreateInstance", tries,
             [&] { instance = screen->StartInstance(log); });
    }
    int system = kSystemFault;
    if (instance) {
      if (tries == o.test_hang_try && o.test_hang_ms > 0) {
        ModLog("vr start     : try %d: the test key vr_start_test_hang_ms "
               "holds the thread for %d ms, as a call into the runtime that "
               "does not come back", tries, o.test_hang_ms);
        InCall("the test wait (vr_start_test_hang_ms)", tries,
               [&] { Sleep(static_cast<DWORD>(o.test_hang_ms)); });
      }
      InCall("the call xrGetSystem", tries,
             [&] { system = screen->StartSystem(); });
    }
    if (system == kSystemReady) {
      screen->SetLog(&ModLog);
      screen->LogSystem();
      ModLog("vr start     : try %d found the headset. The session starts on "
             "the frame thread", tries);
      bwvr::XrScreen* given = screen;
      screen = nullptr;
      session = HandOver(given, tries);
      break;
    } else {
      if (!KeepInstance(o.mode, system)) {
        EndScreen(screen, tries);
        screen = nullptr;
      }
      if (tries == 1) {
        ModLog("vr           : waiting for the headset, will keep trying for "
               "%d seconds. The tries run %s (vr_start=%d)",
               (o.max_tries * o.retry_ms) / 1000, ModeText(o.mode), o.mode);
      }
    }
    for (;;) {
      const uint32_t wait = WaitBeforeNext(started, GetTickCount(), retry_ms);
      if (wait == 0 || QuitAsked()) break;
      WaitForSingleObject(g_wake, wait);
    }
  }
  // The thread that made the instance of the session stays while the
  // session lives. OpenXR does not ask for it, but a runtime can keep a
  // thing of its own on the thread that made the instance.
  if (session) {
    while (!QuitAsked()) WaitForSingleObject(g_wake, INFINITE);
  }
  EndScreen(screen, tries);
  {
    Locked locked;
    g_over = true;
  }
  return 0;
}

}  // namespace

bool VrStartBegin(const VrStartOptions& options) {
  if (g_begun) return true;
  InitializeCriticalSection(&g_lock);
  g_options = options;
  g_wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_wake != nullptr) {
    g_thread = CreateThread(nullptr, 0, &TriesMain, nullptr, 0, nullptr);
  }
  if (g_thread == nullptr) {
    ModLog("vr start     : the thread of the tries did not start (error "
           "%lu). The tries run on the frame thread",
           GetLastError());
    if (g_wake != nullptr) CloseHandle(g_wake);
    g_wake = nullptr;
    DeleteCriticalSection(&g_lock);
    return false;
  }
  g_begun = true;
  return true;
}

bwvr::XrScreen* VrStartTake(bool* over) {
  *over = false;
  if (!g_begun) {
    *over = true;
    return nullptr;
  }
  bwvr::XrScreen* screen = nullptr;
  const char* call = nullptr;
  int call_try = 0;
  uint32_t call_start = 0;
  uint32_t serial = 0;
  {
    Locked locked;
    *over = g_over;
    screen = g_ready;
    g_ready = nullptr;
    call = g_call;
    call_try = g_call_try;
    call_start = g_call_start;
    serial = g_call_serial;
  }
  const uint32_t now = GetTickCount();
  if (call != nullptr && serial != g_slow_serial &&
      vr_start_math::CallSlow(call_start, now, kSlowCallMs)) {
    g_slow_serial = serial;
    ModLog("vr start     : try %d: %s has run for %.1f s and has not come "
           "back. The game goes on, and the frame thread does not wait for it",
           call_try, call,
           vr_start_math::TicksSince(call_start, now) / 1000.0);
  }
  return screen;
}

void VrStartGiveBack(bwvr::XrScreen* screen) {
  if (!g_begun || screen == nullptr) return;
  {
    Locked locked;
    g_back = screen;
  }
  SetEvent(g_wake);
}

int VrStartTries() {
  if (!g_begun) return 0;
  Locked locked;
  return g_tries;
}

void VrStartSessionRuns() {
  if (!g_begun) return;
  {
    Locked locked;
    g_session_runs = true;
  }
  SetEvent(g_wake);
}

void VrStartCancel() {
  if (!g_begun) return;
  {
    Locked locked;
    g_quit = true;
  }
  SetEvent(g_wake);
}

void VrStartShutdown(uint32_t wait_ms) {
  if (!g_begun || g_thread == nullptr) return;
  VrStartCancel();
  if (WaitForSingleObject(g_thread, wait_ms) != WAIT_OBJECT_0) {
    const char* call = nullptr;
    int call_try = 0;
    {
      Locked locked;
      call = g_call;
      call_try = g_call_try;
    }
    // The thread can still use the event and the lock, so both stay.
    ModLog("vr start     : the thread of the tries did not end in %u ms (%s "
           "of try %d). The shutdown does not wait for it",
           wait_ms, call != nullptr ? call : "no call", call_try);
    return;
  }
  CloseHandle(g_thread);
  g_thread = nullptr;
}

// THE FRAME WATCH. The frame thread only.
namespace {

LARGE_INTEGER g_hz = {};
LARGE_INTEGER g_watch_first = {};
LARGE_INTEGER g_watch_last = {};
LARGE_INTEGER g_window_start = {};
vr_start_math::FrameWatch g_window = {};
vr_start_math::FrameWatch g_all = {};
int g_watch_lines = 0;
bool g_watch_ended = false;

double TicksMs(LONGLONG ticks) {
  return g_hz.QuadPart > 0
             ? static_cast<double>(ticks) * 1000.0 /
                   static_cast<double>(g_hz.QuadPart)
             : 0.0;
}

}  // namespace

void VrWatchFrame(double code_ms, int mode) {
  if (g_watch_ended) return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (g_hz.QuadPart == 0) QueryPerformanceFrequency(&g_hz);
  double frame_ms = -1.0;
  if (g_watch_first.QuadPart == 0) {
    g_watch_first = now;
    g_window_start = now;
  } else {
    frame_ms = TicksMs(now.QuadPart - g_watch_last.QuadPart);
  }
  g_watch_last = now;
  vr_start_math::WatchFrame(&g_window, frame_ms, code_ms);
  vr_start_math::WatchFrame(&g_all, frame_ms, code_ms);
  const double window_ms = TicksMs(now.QuadPart - g_window_start.QuadPart);
  if (window_ms < kWatchLineMs) return;
  if (g_watch_lines < kWatchLines) {
    ++g_watch_lines;
    ModLog("vr start     : %.1f s of tries: %u frames, a mean frame of %.2f "
           "ms, the longest frame %.2f ms. The frame thread spent at most "
           "%.3f ms of one frame in the tries (vr_start=%d)",
           window_ms / 1000.0, g_window.frames,
           vr_start_math::WatchMeanFrame(g_window), g_window.frame_longest_ms,
           g_window.code_longest_ms, mode);
  }
  vr_start_math::WatchClear(&g_window);
  g_window_start = now;
}

void VrWatchEnd(const char* how, int mode) {
  if (g_watch_ended || g_watch_first.QuadPart == 0) return;
  g_watch_ended = true;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  ModLog("vr start     : the tries are over, %s. In %.1f s of tries: %u "
         "frames, a mean frame of %.2f ms, the longest frame %.2f ms. The "
         "frame thread spent at most %.3f ms of one frame and %.1f ms in all "
         "in the tries (vr_start=%d)",
         how, TicksMs(now.QuadPart - g_watch_first.QuadPart) / 1000.0,
         g_all.frames, vr_start_math::WatchMeanFrame(g_all),
         g_all.frame_longest_ms, g_all.code_longest_ms, g_all.code_total_ms,
         mode);
}
