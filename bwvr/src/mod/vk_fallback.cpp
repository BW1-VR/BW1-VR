// THE FALL BACK OF THE RENDERER VULKAN (vk_fallback.h).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "../proxy/ddraw_proxy.h"
#include "vk_fallback.h"

void ModLog(const char* format, ...);

namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
char g_path[MAX_PATH] = "";
// This start uses Vulkan and wrote the file.
bool g_armed = false;
bool g_pictured = false;
bool g_faulted = false;
volatile LONG g_waiting = 0;
bool g_test = false;

bool WriteText(const char* text) {
  FILE* f = nullptr;
  if (fopen_s(&f, g_path, "w") != 0 || f == nullptr) return false;
  fputs(text, f);
  fclose(f);
  return true;
}

}  // namespace

void VkFallbackStart(const char* dir, const char* ini) {
  const char* name = ProxyVulkanPendingFile();
  sprintf_s(g_path, "%s%s", dir, name);
  const char* why = ProxyVulkanFallback();
  if (why != nullptr) {
    const bool file = GetFileAttributesA(g_path) != INVALID_FILE_ATTRIBUTES;
    ModLog("vulkan fallback: backend=vulkan, but %s. This start uses "
           "dxwrapper and Direct3D 9.%s",
           why,
           file ? " The file stays, so each start uses dxwrapper until a "
                  "save of the launcher deletes it"
                : "");
    return;
  }
  if (!ProxyBackendVulkan()) return;
  if (GetPrivateProfileIntA("bwvr", "vulkan_fallback", 1, ini) == 0) {
    ModLog("vulkan fallback: vulkan_fallback=0. The mod writes no %s, and "
           "a fault of Vulkan does not change the next start",
           name);
    return;
  }
  g_test = GetPrivateProfileIntA("bwvr", "vulkan_fallback_test", 0, ini) != 0;
  if (!WriteText("The mod started the game with the renderer Vulkan. The "
                 "first picture deletes this file. When this file is there "
                 "at a start, the game uses dxwrapper and Direct3D 9. A save "
                 "in the launcher deletes it.\n")) {
    ModLog("vulkan fallback: the mod could not write %s (error %lu), so a "
           "fault of Vulkan does not change the next start",
           g_path, GetLastError());
    return;
  }
  g_armed = true;
  InterlockedExchange(&g_waiting, 1);
  ModLog("vulkan fallback: vulkan_fallback=1. The mod wrote %s. The first "
         "picture deletes it%s",
         g_path,
         g_test ? ". vulkan_fallback_test=1: the mod stops the game before "
                  "its first walk"
                : "");
}

void VkFallbackPicture(const char* where) {
  if (InterlockedCompareExchange(&g_waiting, 0, 0) == 0) return;
  AcquireSRWLockExclusive(&g_lock);
  if (g_armed && !g_pictured && !g_faulted) {
    g_pictured = true;
    InterlockedExchange(&g_waiting, 0);
    DeleteFileA(g_path);
    ModLog("vulkan fallback: the first picture of the renderer Vulkan went "
           "to %s. The mod deleted %s",
           where, ProxyVulkanPendingFile());
  }
  ReleaseSRWLockExclusive(&g_lock);
}

bool VkFallbackWaiting() {
  return InterlockedCompareExchange(&g_waiting, 0, 0) != 0;
}

void VkFallbackFault(const char* what) {
  AcquireSRWLockExclusive(&g_lock);
  if (g_armed && !g_faulted) {
    g_faulted = true;
    InterlockedExchange(&g_waiting, 0);
    char text[512];
    sprintf_s(text,
              "The renderer Vulkan failed: %s. The next start uses dxwrapper "
              "and Direct3D 9. A save in the launcher deletes this file.\n",
              what);
    const bool written = WriteText(text);
    ModLog("vulkan fallback: %s. %s", what,
           written ? "The mod wrote the file again, and the next start uses "
                     "dxwrapper and Direct3D 9"
                   : "The mod could not write the file, so the next start "
                     "uses Vulkan again");
  }
  ReleaseSRWLockExclusive(&g_lock);
}

void VkFallbackEnd() {
  AcquireSRWLockExclusive(&g_lock);
  if (g_armed && !g_pictured && !g_faulted) {
    g_pictured = true;
    InterlockedExchange(&g_waiting, 0);
    DeleteFileA(g_path);
    ModLog("vulkan fallback: the game ended before the first picture, with "
           "no fault of Vulkan. The mod deleted %s",
           ProxyVulkanPendingFile());
  }
  ReleaseSRWLockExclusive(&g_lock);
}

void VkFallbackTestStop() {
  if (!g_test) return;
  ModLog("vulkan fallback: vulkan_fallback_test=1. The game made its "
         "DirectDraw object, and the mod stops the process before the first "
         "walk. %s stays",
         ProxyVulkanPendingFile());
  TerminateProcess(GetCurrentProcess(), 3);
}
