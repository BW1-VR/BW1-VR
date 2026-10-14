// The Vulkan core of the mod (vk_core.h).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "vk_core.h"

void ModLog(const char* format, ...);

namespace vk {

Functions f = {};

namespace {

HMODULE g_library = nullptr;
Core g_core = {};
bool g_started = false;
bool g_failed = false;
VkCommandPool g_pool = VK_NULL_HANDLE;
VkFence g_fence = VK_NULL_HANDLE;
CRITICAL_SECTION g_once_lock;

bool Fail(const char* what, VkResult r) {
  ModLog("vulkan       : %s failed (%d). The mod does not use Vulkan", what,
         static_cast<int>(r));
  g_failed = true;
  return false;
}

uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < g_core.memory.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) != 0 &&
        (g_core.memory.memoryTypes[i].propertyFlags & want) == want) {
      return i;
    }
  }
  return UINT32_MAX;
}

bool Allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags want,
              VkDeviceMemory* out) {
  uint32_t type = MemoryType(req.memoryTypeBits, want);
  if (type == UINT32_MAX) return false;
  VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  info.allocationSize = req.size;
  info.memoryTypeIndex = type;
  return f.vkAllocateMemory(g_core.device, &info, nullptr, out) == VK_SUCCESS;
}

}  // namespace

bool Start() {
  if (g_started) return true;
  if (g_failed) return false;
  g_library = LoadLibraryA("vulkan-1.dll");
  if (g_library == nullptr) {
    ModLog("vulkan       : there is no vulkan-1.dll. The mod does not use "
           "Vulkan");
    g_failed = true;
    return false;
  }
  f.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      GetProcAddress(g_library, "vkGetInstanceProcAddr"));
  if (f.vkGetInstanceProcAddr == nullptr) {
    return Fail("vkGetInstanceProcAddr", VK_ERROR_INITIALIZATION_FAILED);
  }
  f.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
      f.vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Black & White VR Mod";
  app.pEngineName = "bwvr";
  app.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instance_info.pApplicationInfo = &app;
  VkResult r = f.vkCreateInstance(&instance_info, nullptr, &g_core.instance);
  if (r != VK_SUCCESS) return Fail("vkCreateInstance", r);
#define BWVR_VK_LOAD_INSTANCE(name)                                    \
  f.name = reinterpret_cast<PFN_##name>(                               \
      f.vkGetInstanceProcAddr(g_core.instance, #name));                \
  if (f.name == nullptr) return Fail(#name, VK_ERROR_INITIALIZATION_FAILED);
  BWVR_VK_INSTANCE_FUNCTIONS(BWVR_VK_LOAD_INSTANCE)
#undef BWVR_VK_LOAD_INSTANCE

  // The GPU: Vulkan 1.3, a queue for graphics, dynamic rendering. A
  // dedicated GPU first.
  VkPhysicalDevice devices[16];
  uint32_t count = 16;
  f.vkEnumeratePhysicalDevices(g_core.instance, &count, devices);
  int best = -1;
  int best_rank = -1;
  uint32_t best_family = 0;
  for (uint32_t i = 0; i < count && i < 16; ++i) {
    VkPhysicalDeviceProperties p;
    f.vkGetPhysicalDeviceProperties(devices[i], &p);
    if (p.apiVersion < VK_API_VERSION_1_3) continue;
    VkPhysicalDeviceVulkan13Features v13 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &v13;
    f.vkGetPhysicalDeviceFeatures2(devices[i], &features);
    if (!v13.dynamicRendering || !v13.synchronization2) continue;
    VkQueueFamilyProperties families[16];
    uint32_t family_count = 16;
    f.vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count,
                                               families);
    uint32_t family = UINT32_MAX;
    for (uint32_t q = 0; q < family_count && q < 16; ++q) {
      if (families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        family = q;
        break;
      }
    }
    if (family == UINT32_MAX) continue;
    const int rank =
        p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
        : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                 : 1;
    if (rank > best_rank) {
      best_rank = rank;
      best = static_cast<int>(i);
      best_family = family;
      strncpy_s(g_core.gpu, p.deviceName, _TRUNCATE);
    }
  }
  if (best < 0) {
    ModLog("vulkan       : no GPU has Vulkan 1.3 with dynamic rendering. The "
           "mod does not use Vulkan");
    f.vkDestroyInstance(g_core.instance, nullptr);
    g_failed = true;
    return false;
  }
  g_core.physical = devices[best];
  g_core.queue_family = best_family;
  f.vkGetPhysicalDeviceMemoryProperties(g_core.physical, &g_core.memory);

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {
      VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue_info.queueFamilyIndex = best_family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkPhysicalDeviceVulkan13Features v13 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  v13.dynamicRendering = VK_TRUE;
  v13.synchronization2 = VK_TRUE;
  VkPhysicalDeviceFeatures2 features = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  features.pNext = &v13;
  VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_info.pNext = &features;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  r = f.vkCreateDevice(g_core.physical, &device_info, nullptr, &g_core.device);
  if (r != VK_SUCCESS) return Fail("vkCreateDevice", r);
#define BWVR_VK_LOAD_DEVICE(name)                                      \
  f.name = reinterpret_cast<PFN_##name>(                               \
      f.vkGetDeviceProcAddr(g_core.device, #name));                    \
  if (f.name == nullptr) return Fail(#name, VK_ERROR_INITIALIZATION_FAILED);
  BWVR_VK_DEVICE_FUNCTIONS(BWVR_VK_LOAD_DEVICE)
#undef BWVR_VK_LOAD_DEVICE
  f.vkGetDeviceQueue(g_core.device, best_family, 0, &g_core.queue);

  VkCommandPoolCreateInfo pool_info = {
      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = best_family;
  r = f.vkCreateCommandPool(g_core.device, &pool_info, nullptr, &g_pool);
  if (r != VK_SUCCESS) return Fail("vkCreateCommandPool", r);
  VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  r = f.vkCreateFence(g_core.device, &fence_info, nullptr, &g_fence);
  if (r != VK_SUCCESS) return Fail("vkCreateFence", r);
  InitializeCriticalSection(&g_once_lock);
  g_started = true;
  ModLog("vulkan       : Vulkan 1.3 on %s", g_core.gpu);
  return true;
}

bool Started() { return g_started; }
const Core& Get() { return g_core; }

void Stop() {
  if (!g_started) return;
  f.vkDeviceWaitIdle(g_core.device);
  f.vkDestroyFence(g_core.device, g_fence, nullptr);
  f.vkDestroyCommandPool(g_core.device, g_pool, nullptr);
  f.vkDestroyDevice(g_core.device, nullptr);
  f.vkDestroyInstance(g_core.instance, nullptr);
  g_started = false;
}

bool MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host,
                Buffer* out) {
  memset(out, 0, sizeof(*out));
  VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = size;
  info.usage = usage;
  if (f.vkCreateBuffer(g_core.device, &info, nullptr, &out->buffer) !=
      VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements req;
  f.vkGetBufferMemoryRequirements(g_core.device, out->buffer, &req);
  const VkMemoryPropertyFlags want =
      host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
           : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (!Allocate(req, want, &out->memory) ||
      f.vkBindBufferMemory(g_core.device, out->buffer, out->memory, 0) !=
          VK_SUCCESS) {
    FreeBuffer(out);
    return false;
  }
  out->size = size;
  if (host && f.vkMapMemory(g_core.device, out->memory, 0, size, 0,
                            &out->mapped) != VK_SUCCESS) {
    FreeBuffer(out);
    return false;
  }
  return true;
}

void FreeBuffer(Buffer* b) {
  if (b->mapped != nullptr) f.vkUnmapMemory(g_core.device, b->memory);
  if (b->buffer != VK_NULL_HANDLE) {
    f.vkDestroyBuffer(g_core.device, b->buffer, nullptr);
  }
  if (b->memory != VK_NULL_HANDLE) {
    f.vkFreeMemory(g_core.device, b->memory, nullptr);
  }
  memset(b, 0, sizeof(*b));
}

bool MakeImage(uint32_t width, uint32_t height, uint32_t levels,
               VkFormat format, VkImageUsageFlags usage,
               VkImageAspectFlags aspect, Image* out) {
  memset(out, 0, sizeof(*out));
  VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = format;
  info.extent = {width, height, 1};
  info.mipLevels = levels;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = usage;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (f.vkCreateImage(g_core.device, &info, nullptr, &out->image) !=
      VK_SUCCESS) {
    return false;
  }
  VkMemoryRequirements req;
  f.vkGetImageMemoryRequirements(g_core.device, out->image, &req);
  if (!Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &out->memory) ||
      f.vkBindImageMemory(g_core.device, out->image, out->memory, 0) !=
          VK_SUCCESS) {
    FreeImage(out);
    return false;
  }
  VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view.image = out->image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = format;
  view.subresourceRange = {aspect, 0, levels, 0, 1};
  if (f.vkCreateImageView(g_core.device, &view, nullptr, &out->view) !=
      VK_SUCCESS) {
    FreeImage(out);
    return false;
  }
  out->format = format;
  out->width = width;
  out->height = height;
  out->levels = levels;
  return true;
}

void FreeImage(Image* image) {
  if (image->view != VK_NULL_HANDLE) {
    f.vkDestroyImageView(g_core.device, image->view, nullptr);
  }
  if (image->image != VK_NULL_HANDLE) {
    f.vkDestroyImage(g_core.device, image->image, nullptr);
  }
  if (image->memory != VK_NULL_HANDLE) {
    f.vkFreeMemory(g_core.device, image->memory, nullptr);
  }
  memset(image, 0, sizeof(*image));
}

VkCommandBuffer BeginOnce() {
  EnterCriticalSection(&g_once_lock);
  VkCommandBufferAllocateInfo info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  info.commandPool = g_pool;
  info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  info.commandBufferCount = 1;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  if (f.vkAllocateCommandBuffers(g_core.device, &info, &cb) != VK_SUCCESS) {
    LeaveCriticalSection(&g_once_lock);
    return VK_NULL_HANDLE;
  }
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  f.vkBeginCommandBuffer(cb, &begin);
  return cb;
}

bool EndOnce(VkCommandBuffer cb) {
  f.vkEndCommandBuffer(cb);
  VkCommandBufferSubmitInfo cb_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cb_info.commandBuffer = cb;
  VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cb_info;
  f.vkResetFences(g_core.device, 1, &g_fence);
  bool ok = f.vkQueueSubmit2(g_core.queue, 1, &submit, g_fence) == VK_SUCCESS;
  if (ok) {
    ok = f.vkWaitForFences(g_core.device, 1, &g_fence, VK_TRUE,
                           UINT64_MAX) == VK_SUCCESS;
  }
  f.vkResetCommandPool(g_core.device, g_pool, 0);
  LeaveCriticalSection(&g_once_lock);
  return ok;
}

void Barrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect,
             VkImageLayout from, VkImageLayout to, uint32_t levels) {
  VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
  b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
  b.oldLayout = from;
  b.newLayout = to;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, 0, levels, 0, 1};
  VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  f.vkCmdPipelineBarrier2(cb, &dep);
}

}  // namespace vk
