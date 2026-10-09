// Does any GDI drawing happen while the game is playing?
//
// The answer decides which modern backend can work.
//
// runblack.exe imports 13 functions from GDI32: CreateFontIndirectA, TextOutA,
// SetTextColor, SetBkMode, SetBkColor, GetObjectA, SelectObject, DeleteObject,
// GetDeviceCaps, CreateCompatibleDC, DeleteDC, CreateDIBSection and BitBlt.
// That set is font and text drawing into a DIB section. The question is when
// it runs.
//
//   Only at startup, or only in the assert dialog
//       The GDI code is not part of the frame. d7vk (Direct3D 7 to Vulkan)
//       stays on the table, and a Vulkan backend is the cleanest route to
//       OpenXR, because OpenXR binds a Vulkan image with no sharing hop.
//
//   Every frame, on the game window
//       Direct3D and GDI are mixed on the same surface. d7vk says plainly
//       that it fails in that case, so the backend has to be dxwrapper
//       (Direct3D 7 to Direct3D 9), and the frame reaches OpenXR through a
//       shared surface opened by Direct3D 11.
//
// Six of the thirteen decide the answer. The rest are state setters and
// cleanup that cannot draw anything on their own.
//
// Counting happens on the resolved function addresses, so calls from any
// module are seen, not only the ones runblack.exe makes through its import
// table. A backend that mixes Direct3D and GDI is broken by either.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "MinHook.h"

extern void ModLog(const char* format, ...);

namespace {

bool g_enabled = false;
bool g_installed = false;

// Counts since the mod started, and the value at the last report, so a report
// can say what happened recently as well as in total.
struct Counter {
  const char* name;
  uint64_t total;
  uint64_t at_last_report;
};

enum {
  kTextOut = 0,
  kBitBlt,
  kCreateDIBSection,
  kCreateCompatibleDC,
  kCreateFontIndirect,
  kSelectObject,
  kCounterCount
};

Counter g_counter[kCounterCount] = {
    {"TextOutA", 0, 0},
    {"BitBlt", 0, 0},
    {"CreateDIBSection", 0, 0},
    {"CreateCompatibleDC", 0, 0},
    {"CreateFontIndirectA", 0, 0},
    {"SelectObject", 0, 0},
};

// The window the game draws into. A GDI call against this device context is a
// call that lands on the picture. A call against a memory device context does
// not, at least not until something blits it across.
HWND g_game_window = nullptr;
uint64_t g_on_game_window = 0;

bool IsGameWindow(HDC dc) {
  if (dc == nullptr) return false;
  const HWND owner = WindowFromDC(dc);
  if (owner == nullptr) return false;
  if (g_game_window == nullptr) g_game_window = owner;
  return owner == g_game_window;
}

typedef BOOL(WINAPI* TextOutAFn)(HDC, int, int, LPCSTR, int);
typedef BOOL(WINAPI* BitBltFn)(HDC, int, int, int, int, HDC, int, int, DWORD);
typedef HBITMAP(WINAPI* CreateDIBSectionFn)(HDC, const BITMAPINFO*, UINT,
                                            void**, HANDLE, DWORD);
typedef HDC(WINAPI* CreateCompatibleDCFn)(HDC);
typedef HFONT(WINAPI* CreateFontIndirectAFn)(const LOGFONTA*);
typedef HGDIOBJ(WINAPI* SelectObjectFn)(HDC, HGDIOBJ);

TextOutAFn o_TextOutA = nullptr;
BitBltFn o_BitBlt = nullptr;
CreateDIBSectionFn o_CreateDIBSection = nullptr;
CreateCompatibleDCFn o_CreateCompatibleDC = nullptr;
CreateFontIndirectAFn o_CreateFontIndirectA = nullptr;
SelectObjectFn o_SelectObject = nullptr;

BOOL WINAPI h_TextOutA(HDC dc, int x, int y, LPCSTR text, int length) {
  ++g_counter[kTextOut].total;
  if (IsGameWindow(dc)) ++g_on_game_window;
  return o_TextOutA(dc, x, y, text, length);
}

BOOL WINAPI h_BitBlt(HDC destination, int x, int y, int width, int height,
                     HDC source, int source_x, int source_y, DWORD rop) {
  ++g_counter[kBitBlt].total;
  if (IsGameWindow(destination)) ++g_on_game_window;
  return o_BitBlt(destination, x, y, width, height, source, source_x, source_y,
                  rop);
}

HBITMAP WINAPI h_CreateDIBSection(HDC dc, const BITMAPINFO* info, UINT usage,
                                  void** bits, HANDLE section, DWORD offset) {
  ++g_counter[kCreateDIBSection].total;
  return o_CreateDIBSection(dc, info, usage, bits, section, offset);
}

HDC WINAPI h_CreateCompatibleDC(HDC dc) {
  ++g_counter[kCreateCompatibleDC].total;
  return o_CreateCompatibleDC(dc);
}

HFONT WINAPI h_CreateFontIndirectA(const LOGFONTA* font) {
  ++g_counter[kCreateFontIndirect].total;
  return o_CreateFontIndirectA(font);
}

HGDIOBJ WINAPI h_SelectObject(HDC dc, HGDIOBJ object) {
  ++g_counter[kSelectObject].total;
  return o_SelectObject(dc, object);
}

bool Hook(HMODULE module, const char* name, void* replacement,
          void** original) {
  void* target = reinterpret_cast<void*>(GetProcAddress(module, name));
  if (target == nullptr) return false;
  if (MH_CreateHook(target, replacement, original) != MH_OK) return false;
  return MH_EnableHook(target) == MH_OK;
}

}  // namespace

void GdiWatchSetEnabled(bool on) { g_enabled = on; }

// Hooks the six GDI functions that can draw. Safe to call more than once.
void GdiWatchInstall() {
  if (!g_enabled || g_installed) return;
  HMODULE gdi = GetModuleHandleA("gdi32.dll");
  if (gdi == nullptr) return;

  int hooked = 0;
  hooked += Hook(gdi, "TextOutA", reinterpret_cast<void*>(&h_TextOutA),
                 reinterpret_cast<void**>(&o_TextOutA)) ? 1 : 0;
  hooked += Hook(gdi, "BitBlt", reinterpret_cast<void*>(&h_BitBlt),
                 reinterpret_cast<void**>(&o_BitBlt)) ? 1 : 0;
  hooked += Hook(gdi, "CreateDIBSection",
                 reinterpret_cast<void*>(&h_CreateDIBSection),
                 reinterpret_cast<void**>(&o_CreateDIBSection)) ? 1 : 0;
  hooked += Hook(gdi, "CreateCompatibleDC",
                 reinterpret_cast<void*>(&h_CreateCompatibleDC),
                 reinterpret_cast<void**>(&o_CreateCompatibleDC)) ? 1 : 0;
  hooked += Hook(gdi, "CreateFontIndirectA",
                 reinterpret_cast<void*>(&h_CreateFontIndirectA),
                 reinterpret_cast<void**>(&o_CreateFontIndirectA)) ? 1 : 0;
  hooked += Hook(gdi, "SelectObject", reinterpret_cast<void*>(&h_SelectObject),
                 reinterpret_cast<void**>(&o_SelectObject)) ? 1 : 0;

  g_installed = hooked > 0;
  ModLog("gdi watch    : %d of 6 drawing functions hooked", hooked);
}

// Reports what changed since the last report. The verdict is in the "since
// last report" column: a count that stops growing once the game is playing
// means GDI is not part of the frame.
void GdiWatchReport() {
  if (!g_installed) return;
  uint64_t recent = 0;
  for (int i = 0; i < kCounterCount; ++i) {
    recent += g_counter[i].total - g_counter[i].at_last_report;
  }
  ModLog("gdi watch    : %llu calls since the last report, %llu ever on the "
         "game window", recent, g_on_game_window);
  for (int i = 0; i < kCounterCount; ++i) {
    const uint64_t change = g_counter[i].total - g_counter[i].at_last_report;
    if (g_counter[i].total == 0) continue;
    ModLog("gdi watch    :   %-20s %llu total, %llu since", g_counter[i].name,
           g_counter[i].total, change);
    g_counter[i].at_last_report = g_counter[i].total;
  }
  if (recent == 0) {
    ModLog("gdi watch    : nothing drew through GDI in this window, so the "
           "frame is Direct3D only");
  }
}
