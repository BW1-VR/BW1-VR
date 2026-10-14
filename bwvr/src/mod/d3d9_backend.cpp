// The Direct3D 9 under dxwrapper: the one of Windows, or DXVK
// (d3d9_backend, docs/backend.md, "DXVK under dxwrapper").
//
// WHY. With the render thread the driver of Direct3D 9 is the slowest step
// of a frame in a headset. DXVK makes the calls of Direct3D 9 into calls of
// Vulkan and gives its work to the driver on a thread of its own.
//
// HOW. dxwrapper loads "d3d9.dll" by its name, and the bridge loads it first
// (gpu_bridge.cpp, GpuBridgeInstall). Windows gives a module that is loaded
// already for a load by the same name. So when DXVK is wanted and the PC can
// run it, this file loads <game>\dxvk\d3d9.dll first, and dxwrapper and the
// bridge get DXVK.
//
// WHEN THE PC CAN RUN IT. vulkan-1.dll is there, a GPU has Vulkan 1.3, the
// file is there, and the start before did not fail. Before it loads DXVK the
// mod writes the file dxvk\start-pending.txt, and the first scene walk of
// the game removes it (D3D9BackendConfirm). When the file is there at a start,
// the start before ended before a walk with DXVK, and this start uses the
// Direct3D 9 of Windows. The log says why.
//
//   d3d9_backend=0  the Direct3D 9 of Windows (the default)
//   d3d9_backend=1  DXVK when the PC can run it, else the one of Windows

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void ModLog(const char* format, ...);

namespace {

// The few parts of Vulkan that the check needs (vulkan_core.h).
typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef int32_t VkResult;
typedef void(__stdcall* PFN_vkVoidFunction)(void);
struct VkApplicationInfo {
  int32_t sType;  // 0
  const void* pNext;
  const char* pApplicationName;
  uint32_t applicationVersion;
  const char* pEngineName;
  uint32_t engineVersion;
  uint32_t apiVersion;
};
struct VkInstanceCreateInfo {
  int32_t sType;  // 1
  const void* pNext;
  uint32_t flags;
  const VkApplicationInfo* pApplicationInfo;
  uint32_t enabledLayerCount;
  const char* const* ppEnabledLayerNames;
  uint32_t enabledExtensionCount;
  const char* const* ppEnabledExtensionNames;
};
// The start of VkPhysicalDeviceProperties. The whole struct is less than
// 1 KiB, and the check gives it 4 KiB.
struct VkPhysicalDevicePropertiesStart {
  uint32_t apiVersion;
  uint32_t driverVersion;
  uint32_t vendorID;
  uint32_t deviceID;
  int32_t deviceType;
  char deviceName[256];
};
typedef PFN_vkVoidFunction(__stdcall* GetInstanceProcAddrFn)(VkInstance,
                                                             const char*);
typedef VkResult(__stdcall* EnumerateInstanceVersionFn)(uint32_t*);
typedef VkResult(__stdcall* CreateInstanceFn)(const VkInstanceCreateInfo*,
                                              const void*, VkInstance*);
typedef void(__stdcall* DestroyInstanceFn)(VkInstance, const void*);
typedef VkResult(__stdcall* EnumeratePhysicalDevicesFn)(VkInstance, uint32_t*,
                                                        VkPhysicalDevice*);
typedef void(__stdcall* GetPhysicalDevicePropertiesFn)(VkPhysicalDevice,
                                                       void*);

const uint32_t kVulkan13 = (1u << 22) | (3u << 12);

uint32_t Major(uint32_t v) { return (v >> 22) & 0x7Fu; }
uint32_t Minor(uint32_t v) { return (v >> 12) & 0x3FFu; }

int g_mode = 0;
bool g_dxvk = false;
char g_pending[MAX_PATH] = "";

// True when a GPU has Vulkan 1.3. `why` gets the reason when not.
bool VulkanReady(char* why, size_t why_size, char* gpu, size_t gpu_size) {
  HMODULE vulkan = LoadLibraryA("vulkan-1.dll");
  if (vulkan == nullptr) {
    sprintf_s(why, why_size, "there is no vulkan-1.dll (no Vulkan driver)");
    return false;
  }
  GetInstanceProcAddrFn get = reinterpret_cast<GetInstanceProcAddrFn>(
      GetProcAddress(vulkan, "vkGetInstanceProcAddr"));
  if (get == nullptr) {
    sprintf_s(why, why_size, "vulkan-1.dll has no vkGetInstanceProcAddr");
    return false;
  }
  uint32_t loader = (1u << 22);
  EnumerateInstanceVersionFn version = reinterpret_cast<EnumerateInstanceVersionFn>(
      get(nullptr, "vkEnumerateInstanceVersion"));
  if (version != nullptr) version(&loader);
  if (loader < kVulkan13) {
    sprintf_s(why, why_size, "the Vulkan loader has version %u.%u, not 1.3",
              Major(loader), Minor(loader));
    return false;
  }
  CreateInstanceFn create =
      reinterpret_cast<CreateInstanceFn>(get(nullptr, "vkCreateInstance"));
  if (create == nullptr) {
    sprintf_s(why, why_size, "vulkan-1.dll has no vkCreateInstance");
    return false;
  }
  VkApplicationInfo app = {0, nullptr, "bwvr", 1, "bwvr", 1, kVulkan13};
  VkInstanceCreateInfo info = {1, nullptr, 0, &app, 0, nullptr, 0, nullptr};
  VkInstance instance = nullptr;
  if (create(&info, nullptr, &instance) != 0 || instance == nullptr) {
    sprintf_s(why, why_size, "vkCreateInstance for Vulkan 1.3 failed");
    return false;
  }
  DestroyInstanceFn destroy =
      reinterpret_cast<DestroyInstanceFn>(get(instance, "vkDestroyInstance"));
  EnumeratePhysicalDevicesFn enumerate =
      reinterpret_cast<EnumeratePhysicalDevicesFn>(
          get(instance, "vkEnumeratePhysicalDevices"));
  GetPhysicalDevicePropertiesFn properties =
      reinterpret_cast<GetPhysicalDevicePropertiesFn>(
          get(instance, "vkGetPhysicalDeviceProperties"));
  bool ok = false;
  uint32_t best = 0;
  if (enumerate != nullptr && properties != nullptr) {
    VkPhysicalDevice devices[16] = {};
    uint32_t count = 16;
    enumerate(instance, &count, devices);
    for (uint32_t i = 0; i < count && i < 16; ++i) {
      static uint8_t buffer[4096];
      memset(buffer, 0, sizeof(buffer));
      properties(devices[i], buffer);
      const VkPhysicalDevicePropertiesStart* p =
          reinterpret_cast<const VkPhysicalDevicePropertiesStart*>(buffer);
      if (p->apiVersion > best) {
        best = p->apiVersion;
        sprintf_s(gpu, gpu_size, "%.200s", p->deviceName);
      }
      if (p->apiVersion >= kVulkan13) ok = true;
    }
  }
  if (destroy != nullptr) destroy(instance, nullptr);
  if (!ok) {
    sprintf_s(why, why_size, "no GPU has Vulkan 1.3 (the best has %u.%u)",
              Major(best), Minor(best));
  }
  return ok;
}

bool FileThere(const char* path) {
  return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

// Call it before anything loads d3d9.dll. `dir` is the folder of the game
// with a backslash at the end, `ini` the path of bwvr.ini.
void D3D9BackendLoad(const char* dir, const char* ini) {
  g_mode = static_cast<int>(GetPrivateProfileIntA("bwvr", "d3d9_backend", 0, ini));
  if (g_mode != 1) {
    ModLog("d3d9 backend : d3d9_backend=0. dxwrapper draws with the Direct3D 9 "
           "of Windows");
    return;
  }
  char folder[MAX_PATH], dll[MAX_PATH];
  sprintf_s(folder, "%sdxvk", dir);
  sprintf_s(dll, "%s\\d3d9.dll", folder);
  sprintf_s(g_pending, "%s\\start-pending.txt", folder);
  char why[256] = "", gpu[256] = "";
  if (!FileThere(dll)) {
    ModLog("d3d9 backend : d3d9_backend=1, but %s is not there. dxwrapper "
           "draws with the Direct3D 9 of Windows", dll);
    return;
  }
  if (FileThere(g_pending)) {
    ModLog("d3d9 backend : d3d9_backend=1, but the start before ended before "
           "the first frame with DXVK (%s is there). This start uses the "
           "Direct3D 9 of Windows. Delete the file to try DXVK again",
           g_pending);
    g_pending[0] = 0;
    return;
  }
  if (!VulkanReady(why, sizeof(why), gpu, sizeof(gpu))) {
    ModLog("d3d9 backend : d3d9_backend=1, but %s. dxwrapper draws with the "
           "Direct3D 9 of Windows", why);
    return;
  }
  // The settings of DXVK for the mod. No vertical sync: the frame goes to
  // the headset, not to the monitor. No OpenXR or OpenVR of DXVK: the mod
  // has the session.
  SetEnvironmentVariableA("DXVK_CONFIG", "d3d9.presentInterval = 0");
  SetEnvironmentVariableA("DXVK_NO_VR", "1");
  SetEnvironmentVariableA("DXVK_LOG_PATH", folder);
  SetEnvironmentVariableA("DXVK_SHADER_CACHE_PATH", folder);
  FILE* f = nullptr;
  if (fopen_s(&f, g_pending, "w") == 0 && f != nullptr) {
    fputs("The mod started the game with DXVK. The first scene walk removes "
          "this file.\n", f);
    fclose(f);
  }
  HMODULE module = LoadLibraryA(dll);
  if (module == nullptr) {
    ModLog("d3d9 backend : d3d9_backend=1, but %s did not load (error %lu). "
           "dxwrapper draws with the Direct3D 9 of Windows", dll,
           GetLastError());
    DeleteFileA(g_pending);
    g_pending[0] = 0;
    return;
  }
  g_dxvk = true;
  ModLog("d3d9 backend : d3d9_backend=1. dxwrapper draws with DXVK (%s) on "
         "%s. The log of DXVK is in %s", dll, gpu, folder);
}

// The first scene walk: the start with DXVK did not fail.
void D3D9BackendConfirm() {
  if (g_pending[0] == 0) return;
  DeleteFileA(g_pending);
  g_pending[0] = 0;
}

bool D3D9BackendDxvk() { return g_dxvk; }
