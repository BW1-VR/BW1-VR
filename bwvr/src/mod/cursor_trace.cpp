// WHO TOUCHES THE CURSOR OF WINDOWS, AND WHO TAKES THE FRONT. A test aid, the
// key cursor_trace=1. It is off for players. The desktop test runs turn it on
// (tools/desk/desk_test.ps1).
//
// A desktop test must leave the real cursor and the window in front as they
// are. When one of them changes in a run, this file gives the facts. It hooks
// the functions of user32 that can do it, for the whole process: the game, the
// DirectX layer and the mod call the same functions. Each call goes on to
// Windows with no change.
//
// A caller is one function and the address that the call returns to. The log
// has:
//
//   one line for each of the first calls of a caller, and one line each time
//   a call moves the cursor after calls that did not (or the reverse), or the
//   window in front is a different one than at the caller's call before
//
//   one line for each change of the window in front, with the place of the
//   cursor (CursorTraceFrame, once a frame)
//
//   one line when the cursor is at a different place than this file saw last,
//   with no call of the process between: a person or a different program
//   moved it
//
//   the counts of each caller in the periodic report

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"

extern void ModLog(const char* format, ...);

namespace {

// The window that the game made (see pointer_map.cpp).
const uintptr_t kGameWindowMade = 0x00E8C0F4;

enum TracedCall {
  kSetCursorPos,
  kClipCursor,
  kSetForegroundWindow,
  kSetActiveWindow,
  kTracedCount
};
const char* const kCallName[kTracedCount] = {
    "SetCursorPos", "ClipCursor", "SetForegroundWindow", "SetActiveWindow"};

struct Caller {
  int call;
  uintptr_t from;
  char where[80];
  uint64_t calls;
  uint64_t moved;
  uint64_t reported_calls;
  uint64_t reported_moved;
  int lines;
  bool last_moved;
  bool last_front;
  char last_what[96];
};
const int kMaxCallers = 48;
const int kFirstLines = 3;
const int kMaxLinesForCaller = 12;
Caller g_callers[kMaxCallers];
int g_caller_count = 0;
bool g_table_full_said = false;

bool g_installed = false;
CRITICAL_SECTION g_lock;
uint64_t g_frame = 0;

// The place of the cursor that this file saw last, after a call or at a
// frame.
POINT g_known = {0, 0};
bool g_have_known = false;
int g_outside_lines = 0;
const int kMaxOutsideLines = 20;

// The window in front at the last frame.
HWND g_front = nullptr;
bool g_have_front = false;
int g_front_lines = 0;
const int kMaxFrontLines = 40;

typedef BOOL(WINAPI* SetCursorPosFn)(int, int);
typedef BOOL(WINAPI* ClipCursorFn)(const RECT*);
typedef BOOL(WINAPI* SetForegroundWindowFn)(HWND);
typedef HWND(WINAPI* SetActiveWindowFn)(HWND);
SetCursorPosFn o_SetCursorPos = nullptr;
ClipCursorFn o_ClipCursor = nullptr;
SetForegroundWindowFn o_SetForegroundWindow = nullptr;
SetActiveWindowFn o_SetActiveWindow = nullptr;

HWND GameWindow() {
  return *reinterpret_cast<const HWND*>(kGameWindowMade);
}

bool GameInFront() {
  const HWND game = GameWindow();
  return game != nullptr && GetForegroundWindow() == game;
}

// The module of an address and the offset in it, for example
// "runblack.exe+0x3E4EC8".
void DescribeAddress(uintptr_t address, char* out, size_t size) {
  HMODULE module = nullptr;
  char path[MAX_PATH] = "";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(address), &module) &&
      module != nullptr &&
      GetModuleFileNameA(module, path, MAX_PATH) != 0) {
    const char* slash = strrchr(path, '\\');
    _snprintf_s(out, size, _TRUNCATE, "%s+0x%X",
                slash != nullptr ? slash + 1 : path,
                static_cast<unsigned>(address -
                                      reinterpret_cast<uintptr_t>(module)));
  } else {
    _snprintf_s(out, size, _TRUNCATE, "0x%08X (no module)",
                static_cast<unsigned>(address));
  }
}

// A window for the log: the game window, or the class of a different one.
// The title is not in the log, because it can be private.
void DescribeWindow(HWND window, char* out, size_t size) {
  if (window == nullptr) {
    strcpy_s(out, size, "no window");
  } else if (window == GameWindow()) {
    strcpy_s(out, size, "the game window");
  } else {
    char window_class[64] = "?";
    GetClassNameA(window, window_class, sizeof(window_class));
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    _snprintf_s(out, size, _TRUNCATE, "a window of %s, class \"%s\"",
                process == GetCurrentProcessId() ? "the game process"
                                                 : "a different program",
                window_class);
  }
}

Caller* FindCaller(int call, uintptr_t from) {
  for (int i = 0; i < g_caller_count; ++i) {
    if (g_callers[i].call == call && g_callers[i].from == from) {
      return &g_callers[i];
    }
  }
  if (g_caller_count >= kMaxCallers) return nullptr;
  Caller* caller = &g_callers[g_caller_count++];
  memset(caller, 0, sizeof(*caller));
  caller->call = call;
  caller->from = from;
  DescribeAddress(from, caller->where, sizeof(caller->where));
  return caller;
}

// One call that is done. before and after are the place of the cursor around
// it, and front says whether the game window was in front before it.
void Note(int call, void* return_address, const char* what, POINT before,
          POINT after, bool front) {
  const bool moved = before.x != after.x || before.y != after.y;
  EnterCriticalSection(&g_lock);
  g_known = after;
  g_have_known = true;
  Caller* caller = FindCaller(call, reinterpret_cast<uintptr_t>(return_address));
  if (caller == nullptr) {
    const bool say = !g_table_full_said;
    g_table_full_said = true;
    LeaveCriticalSection(&g_lock);
    if (say) {
      ModLog("cursor trace : more than %d callers. The calls of each other "
             "caller are not in the log", kMaxCallers);
    }
    return;
  }
  ++caller->calls;
  if (moved) ++caller->moved;
  const bool change = caller->calls > 1 && (moved != caller->last_moved ||
                                            front != caller->last_front);
  const bool say = caller->lines < kMaxLinesForCaller &&
                   (caller->calls <= kFirstLines || change);
  if (say) ++caller->lines;
  caller->last_moved = moved;
  caller->last_front = front;
  strcpy_s(caller->last_what, what);
  const uint64_t count = caller->calls;
  const uint64_t frame = g_frame;
  char where[80];
  strcpy_s(where, caller->where);
  LeaveCriticalSection(&g_lock);
  if (!say) return;
  if (moved) {
    ModLog("cursor trace : %s%s from %s, call %llu of this caller. It moved "
           "the cursor from %ld %ld to %ld %ld. The game window was %s, frame "
           "%llu", kCallName[call], what, where, count, before.x, before.y,
           after.x, after.y, front ? "in front" : "not in front", frame);
  } else {
    ModLog("cursor trace : %s%s from %s, call %llu of this caller. The cursor "
           "stayed at %ld %ld. The game window was %s, frame %llu",
           kCallName[call], what, where, count, after.x, after.y,
           front ? "in front" : "not in front", frame);
  }
}

BOOL WINAPI h_SetCursorPos(int x, int y) {
  POINT before = {0, 0}, after = {0, 0};
  const bool front = GameInFront();
  GetCursorPos(&before);
  const BOOL result = o_SetCursorPos(x, y);
  GetCursorPos(&after);
  char what[96];
  _snprintf_s(what, sizeof(what), _TRUNCATE, "(%d, %d)", x, y);
  Note(kSetCursorPos, _ReturnAddress(), what, before, after, front);
  return result;
}

BOOL WINAPI h_ClipCursor(const RECT* rect) {
  POINT before = {0, 0}, after = {0, 0};
  const bool front = GameInFront();
  GetCursorPos(&before);
  const BOOL result = o_ClipCursor(rect);
  GetCursorPos(&after);
  char what[96];
  if (rect != nullptr) {
    _snprintf_s(what, sizeof(what), _TRUNCATE, "(%ld %ld to %ld %ld)",
                rect->left, rect->top, rect->right, rect->bottom);
  } else {
    strcpy_s(what, "(no rectangle)");
  }
  Note(kClipCursor, _ReturnAddress(), what, before, after, front);
  return result;
}

BOOL WINAPI h_SetForegroundWindow(HWND window) {
  POINT before = {0, 0}, after = {0, 0};
  const bool front = GameInFront();
  GetCursorPos(&before);
  const BOOL result = o_SetForegroundWindow(window);
  GetCursorPos(&after);
  char name[80], what[96];
  DescribeWindow(window, name, sizeof(name));
  _snprintf_s(what, sizeof(what), _TRUNCATE, "(%s)%s", name,
              result ? "" : ", which Windows refused,");
  Note(kSetForegroundWindow, _ReturnAddress(), what, before, after, front);
  return result;
}

HWND WINAPI h_SetActiveWindow(HWND window) {
  POINT before = {0, 0}, after = {0, 0};
  const bool front = GameInFront();
  GetCursorPos(&before);
  const HWND result = o_SetActiveWindow(window);
  GetCursorPos(&after);
  char name[80], what[96];
  DescribeWindow(window, name, sizeof(name));
  _snprintf_s(what, sizeof(what), _TRUNCATE, "(%s)", name);
  Note(kSetActiveWindow, _ReturnAddress(), what, before, after, front);
  return result;
}

bool HookApi(const char* name, void* replacement, void** original) {
  void* target = nullptr;
  MH_STATUS status =
      MH_CreateHookApiEx(L"user32", name, replacement, original, &target);
  if (status == MH_OK) status = MH_EnableHook(target);
  return status == MH_OK;
}

}  // namespace

// cursor_trace=1. Call it after MH_Initialize, before the game makes its
// window.
void CursorTraceInstall() {
  if (g_installed) return;
  InitializeCriticalSection(&g_lock);
  const bool ok =
      HookApi("SetCursorPos", reinterpret_cast<void*>(&h_SetCursorPos),
              reinterpret_cast<void**>(&o_SetCursorPos)) &&
      HookApi("ClipCursor", reinterpret_cast<void*>(&h_ClipCursor),
              reinterpret_cast<void**>(&o_ClipCursor)) &&
      HookApi("SetForegroundWindow",
              reinterpret_cast<void*>(&h_SetForegroundWindow),
              reinterpret_cast<void**>(&o_SetForegroundWindow)) &&
      HookApi("SetActiveWindow", reinterpret_cast<void*>(&h_SetActiveWindow),
              reinterpret_cast<void**>(&o_SetActiveWindow));
  g_installed = true;
  POINT cursor = {0, 0};
  GetCursorPos(&cursor);
  char front[80];
  DescribeWindow(GetForegroundWindow(), front, sizeof(front));
  ModLog("cursor trace : cursor_trace=1 (a test). The log names each caller "
         "of SetCursorPos, ClipCursor, SetForegroundWindow and "
         "SetActiveWindow in the process%s. At the start the cursor is at "
         "%ld %ld, and in front is %s", ok ? "" : " (a hook FAILED)", cursor.x,
         cursor.y, front);
}

// Once a frame, with the count of frames: the window in front, a cursor that
// moved with no call of the process, and the report.
void CursorTraceFrame(uint64_t frame) {
  if (!g_installed) return;
  POINT cursor = {0, 0};
  GetCursorPos(&cursor);
  const HWND front = GetForegroundWindow();

  EnterCriticalSection(&g_lock);
  g_frame = frame;
  const bool outside = g_have_known &&
                       (cursor.x != g_known.x || cursor.y != g_known.y);
  const POINT known = g_known;
  g_known = cursor;
  g_have_known = true;
  const bool front_changed = !g_have_front || front != g_front;
  g_front = front;
  g_have_front = true;
  LeaveCriticalSection(&g_lock);

  if (outside && g_outside_lines < kMaxOutsideLines) {
    ++g_outside_lines;
    ModLog("cursor trace : the cursor went from %ld %ld to %ld %ld with no "
           "call of the game process (a person or a different program moved "
           "it), frame %llu", known.x, known.y, cursor.x, cursor.y, frame);
  }
  if (front_changed && g_front_lines < kMaxFrontLines) {
    ++g_front_lines;
    char name[80];
    DescribeWindow(front, name, sizeof(name));
    ModLog("cursor trace : in front is %s. The cursor is at %ld %ld, frame "
           "%llu", name, cursor.x, cursor.y, frame);
  }

  if ((frame % 900) != 0) return;
  for (int i = 0;; ++i) {
    EnterCriticalSection(&g_lock);
    if (i >= g_caller_count) {
      LeaveCriticalSection(&g_lock);
      break;
    }
    Caller* caller = &g_callers[i];
    const uint64_t calls = caller->calls - caller->reported_calls;
    const uint64_t moved = caller->moved - caller->reported_moved;
    caller->reported_calls = caller->calls;
    caller->reported_moved = caller->moved;
    const int call = caller->call;
    char where[80], what[96];
    strcpy_s(where, caller->where);
    strcpy_s(what, caller->last_what);
    LeaveCriticalSection(&g_lock);
    if (calls == 0) continue;
    ModLog("cursor trace : %llu calls of %s from %s since the report before, "
           "and %llu of them moved the cursor. The last one was %s%s", calls,
           kCallName[call], where, moved, kCallName[call], what);
  }
}
