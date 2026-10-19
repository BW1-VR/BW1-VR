// The window of the game with a swapchain of Vulkan (vk_window.h).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include "vk_window.h"
#include "vk_fallback.h"

void ModLog(const char* format, ...);
void ControllersGesturePulse(float amplitude, float seconds);

namespace {

using vk::f;

// The functions of the surface and of the swapchain. The core does not read
// them, because a device with no VK_KHR_swapchain has none.
struct WindowFunctions {
  PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
  PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
  PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR
      vkGetPhysicalDeviceSurfacePresentModesKHR;
  PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
  PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
  PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
  PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
  PFN_vkQueuePresentKHR vkQueuePresentKHR;
  PFN_vkCreateSemaphore vkCreateSemaphore;
  PFN_vkDestroySemaphore vkDestroySemaphore;
};
WindowFunctions w = {};

bool g_wanted = true;    // vk_window, the value now
bool g_loaded = false;
bool g_unusable = false;  // the window takes the way through the CPU for good

const uint32_t kMaxImages = 8;
// The presents in flight. The slot of a present is free again three presents
// later, so the frame thread does not wait for its fence.
const int kSlots = 3;
struct Slot {
  VkCommandPool pool;
  VkCommandBuffer cb;
  VkFence fence;
  VkSemaphore acquired;  // the swapchain image is free for the blit
  bool pending;          // submitted, with the fence not waited for
};
Slot g_slots[kSlots] = {};
int g_slot = 0;
bool g_slots_made = false;

HWND g_window = nullptr;
VkSurfaceKHR g_surface = VK_NULL_HANDLE;
VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
VkFormat g_format = VK_FORMAT_UNDEFINED;
VkPresentModeKHR g_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
uint32_t g_width = 0;   // the extent of the swapchain
uint32_t g_height = 0;
uint32_t g_asked_w = 0;  // the client area that the swapchain was made for
uint32_t g_asked_h = 0;
VkImage g_images[kMaxImages] = {};
// The blit into the image is done, and the present can show it.
VkSemaphore g_drawn[kMaxImages] = {};
uint32_t g_image_count = 0;
bool g_remake = false;  // the swapchain is out of date

// For the report.
uint64_t g_presents = 0;
uint64_t g_no_image = 0;
uint64_t g_made = 0;

void Unusable(const char* what, VkResult r) {
  g_unusable = true;
  ModLog("vk window    : %s (%d). The window gets the picture through the CPU "
         "and GDI from now on", what, static_cast<int>(r));
}

bool Load() {
  if (g_loaded) return true;
  const vk::Core& core = vk::Get();
  if (!core.window_present) {
    g_unusable = true;
    ModLog("vk window    : the Vulkan device has no VK_KHR_swapchain, or the "
           "instance no VK_KHR_win32_surface. The window gets the picture "
           "through the CPU and GDI");
    return false;
  }
#define BWVR_WINDOW_INSTANCE(name)                                       \
  w.name = reinterpret_cast<PFN_##name>(                                 \
      f.vkGetInstanceProcAddr(core.instance, #name));                    \
  if (w.name == nullptr) {                                               \
    Unusable("the instance has no " #name, VK_ERROR_EXTENSION_NOT_PRESENT); \
    return false;                                                        \
  }
#define BWVR_WINDOW_DEVICE(name)                                         \
  w.name = reinterpret_cast<PFN_##name>(                                 \
      f.vkGetDeviceProcAddr(core.device, #name));                        \
  if (w.name == nullptr) {                                               \
    Unusable("the device has no " #name, VK_ERROR_EXTENSION_NOT_PRESENT); \
    return false;                                                        \
  }
  BWVR_WINDOW_INSTANCE(vkCreateWin32SurfaceKHR)
  BWVR_WINDOW_INSTANCE(vkDestroySurfaceKHR)
  BWVR_WINDOW_INSTANCE(vkGetPhysicalDeviceSurfaceSupportKHR)
  BWVR_WINDOW_INSTANCE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
  BWVR_WINDOW_INSTANCE(vkGetPhysicalDeviceSurfaceFormatsKHR)
  BWVR_WINDOW_INSTANCE(vkGetPhysicalDeviceSurfacePresentModesKHR)
  BWVR_WINDOW_DEVICE(vkCreateSwapchainKHR)
  BWVR_WINDOW_DEVICE(vkDestroySwapchainKHR)
  BWVR_WINDOW_DEVICE(vkGetSwapchainImagesKHR)
  BWVR_WINDOW_DEVICE(vkAcquireNextImageKHR)
  BWVR_WINDOW_DEVICE(vkQueuePresentKHR)
  BWVR_WINDOW_DEVICE(vkCreateSemaphore)
  BWVR_WINDOW_DEVICE(vkDestroySemaphore)
#undef BWVR_WINDOW_INSTANCE
#undef BWVR_WINDOW_DEVICE
  g_loaded = true;
  return true;
}

VkSemaphore MakeSemaphore() {
  VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore semaphore = VK_NULL_HANDLE;
  w.vkCreateSemaphore(vk::Get().device, &info, nullptr, &semaphore);
  return semaphore;
}

bool MakeSlots() {
  const vk::Core& core = vk::Get();
  for (int i = 0; i < kSlots; ++i) {
    Slot& s = g_slots[i];
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = core.queue_family;
    if (f.vkCreateCommandPool(core.device, &pool, nullptr, &s.pool) !=
        VK_SUCCESS) {
      return false;
    }
    VkCommandBufferAllocateInfo cb_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cb_info.commandPool = s.pool;
    cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_info.commandBufferCount = 1;
    if (f.vkAllocateCommandBuffers(core.device, &cb_info, &s.cb) !=
        VK_SUCCESS) {
      return false;
    }
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (f.vkCreateFence(core.device, &fence, nullptr, &s.fence) !=
        VK_SUCCESS) {
      return false;
    }
    s.acquired = MakeSemaphore();
    if (s.acquired == VK_NULL_HANDLE) return false;
    s.pending = false;
  }
  g_slot = 0;
  g_slots_made = true;
  return true;
}

// Waits until the GPU has done each present in flight.
void WaitSlots() {
  if (!g_slots_made) return;
  const VkDevice device = vk::Get().device;
  for (int i = 0; i < kSlots; ++i) {
    if (!g_slots[i].pending) continue;
    f.vkWaitForFences(device, 1, &g_slots[i].fence, VK_TRUE, UINT64_MAX);
    g_slots[i].pending = false;
  }
}

// The swapchain goes, when the GPU and the present engine have done each
// present of it.
void DropSwapchain() {
  if (g_swapchain == VK_NULL_HANDLE) return;
  const VkDevice device = vk::Get().device;
  WaitSlots();
  // The present engine can still read an image of a present, so the queue
  // becomes idle first. This comes only at a change of the window.
  vk::LockQueue();
  f.vkQueueWaitIdle(vk::Get().queue);
  vk::UnlockQueue();
  w.vkDestroySwapchainKHR(device, g_swapchain, nullptr);
  g_swapchain = VK_NULL_HANDLE;
  g_image_count = 0;
}

void DropSurface() {
  DropSwapchain();
  if (g_surface != VK_NULL_HANDLE) {
    w.vkDestroySurfaceKHR(vk::Get().instance, g_surface, nullptr);
    g_surface = VK_NULL_HANDLE;
  }
  g_window = nullptr;
}

bool MakeSurface(HWND window) {
  const vk::Core& core = vk::Get();
  VkWin32SurfaceCreateInfoKHR info = {
      VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
  info.hinstance = GetModuleHandleA(nullptr);
  info.hwnd = window;
  VkResult r =
      w.vkCreateWin32SurfaceKHR(core.instance, &info, nullptr, &g_surface);
  if (r != VK_SUCCESS) {
    g_surface = VK_NULL_HANDLE;
    Unusable("vkCreateWin32SurfaceKHR failed", r);
    return false;
  }
  VkBool32 supported = VK_FALSE;
  w.vkGetPhysicalDeviceSurfaceSupportKHR(core.physical, core.queue_family,
                                         g_surface, &supported);
  if (!supported) {
    w.vkDestroySurfaceKHR(core.instance, g_surface, nullptr);
    g_surface = VK_NULL_HANDLE;
    Unusable("the queue of the renderer cannot present to the window",
             VK_ERROR_FEATURE_NOT_PRESENT);
    return false;
  }
  g_window = window;
  return true;
}

// 1: a swapchain for the client area of `width` by `height`. 0: no swapchain
// now (a window with no area). -1: the window takes the way through the CPU.
int MakeSwapchain(uint32_t width, uint32_t height) {
  const vk::Core& core = vk::Get();
  VkSurfaceCapabilitiesKHR caps;
  VkResult r = w.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core.physical,
                                                           g_surface, &caps);
  if (r != VK_SUCCESS) {
    Unusable("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed", r);
    return -1;
  }
  VkExtent2D extent = caps.currentExtent;
  if (extent.width == 0xFFFFFFFFu) {
    extent.width = width;
    extent.height = height;
  }
  if (extent.width == 0 || extent.height == 0) return 0;
  if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
    Unusable("the swapchain of the window cannot take a blit",
             VK_ERROR_FEATURE_NOT_PRESENT);
    return -1;
  }

  // 8 bits UNORM: the picture of the game has its gamma already, and a
  // blit into a format with sRGB would change it.
  VkSurfaceFormatKHR formats[64];
  uint32_t format_count = 64;
  w.vkGetPhysicalDeviceSurfaceFormatsKHR(core.physical, g_surface,
                                         &format_count, formats);
  VkSurfaceFormatKHR chosen = {VK_FORMAT_UNDEFINED,
                               VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  const VkFormat wanted[2] = {VK_FORMAT_B8G8R8A8_UNORM,
                              VK_FORMAT_R8G8B8A8_UNORM};
  for (int k = 0; k < 2 && chosen.format == VK_FORMAT_UNDEFINED; ++k) {
    VkFormatProperties properties;
    f.vkGetPhysicalDeviceFormatProperties(core.physical, wanted[k],
                                          &properties);
    if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) ==
        0) {
      continue;
    }
    for (uint32_t i = 0; i < format_count && i < 64; ++i) {
      if (formats[i].format == wanted[k] &&
          formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        chosen = formats[i];
        break;
      }
    }
  }
  if (chosen.format == VK_FORMAT_UNDEFINED) {
    Unusable("the window has no swapchain format of 8 bits UNORM",
             VK_ERROR_FORMAT_NOT_SUPPORTED);
    return -1;
  }

  // The frame rate of the game is the rate of the headset, so a present
  // must not wait for the monitor. FIFO waits, so it is not used.
  VkPresentModeKHR modes[16];
  uint32_t mode_count = 16;
  w.vkGetPhysicalDeviceSurfacePresentModesKHR(core.physical, g_surface,
                                              &mode_count, modes);
  bool mailbox = false, immediate = false;
  for (uint32_t i = 0; i < mode_count && i < 16; ++i) {
    if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) mailbox = true;
    if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) immediate = true;
  }
  if (!mailbox && !immediate) {
    Unusable("the window has no present with no wait for the monitor "
             "(mailbox or immediate)",
             VK_ERROR_FEATURE_NOT_PRESENT);
    return -1;
  }
  const VkPresentModeKHR mode =
      mailbox ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;

  uint32_t count = caps.minImageCount + 1;
  if (count < 3) count = 3;
  if (caps.maxImageCount != 0 && count > caps.maxImageCount) {
    count = caps.maxImageCount;
  }
  if (count > kMaxImages) count = kMaxImages;
  VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if ((caps.supportedCompositeAlpha & alpha) == 0) {
    const VkCompositeAlphaFlagBitsKHR others[3] = {
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (int i = 0; i < 3; ++i) {
      if ((caps.supportedCompositeAlpha & others[i]) != 0) {
        alpha = others[i];
        break;
      }
    }
  }

  DropSwapchain();
  VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  info.surface = g_surface;
  info.minImageCount = count;
  info.imageFormat = chosen.format;
  info.imageColorSpace = chosen.colorSpace;
  info.imageExtent = extent;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform = caps.currentTransform;
  info.compositeAlpha = alpha;
  info.presentMode = mode;
  info.clipped = VK_TRUE;
  r = w.vkCreateSwapchainKHR(core.device, &info, nullptr, &g_swapchain);
  if (r != VK_SUCCESS) {
    g_swapchain = VK_NULL_HANDLE;
    Unusable("vkCreateSwapchainKHR failed", r);
    return -1;
  }
  g_image_count = kMaxImages;
  w.vkGetSwapchainImagesKHR(core.device, g_swapchain, &g_image_count,
                            g_images);
  for (uint32_t i = 0; i < g_image_count; ++i) {
    if (g_drawn[i] == VK_NULL_HANDLE) g_drawn[i] = MakeSemaphore();
    if (g_drawn[i] == VK_NULL_HANDLE) {
      Unusable("vkCreateSemaphore failed", VK_ERROR_OUT_OF_HOST_MEMORY);
      return -1;
    }
  }
  g_format = chosen.format;
  g_mode = mode;
  g_width = extent.width;
  g_height = extent.height;
  g_asked_w = width;
  g_asked_h = height;
  g_remake = false;
  if (g_made++ < 8) {
    ModLog("vk window    : a swapchain of Vulkan on the window of the game, "
           "%u by %u, %u images, format %d, %s",
           g_width, g_height, g_image_count, static_cast<int>(g_format),
           mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "immediate");
  }
  return 1;
}

}  // namespace

void VkWindowSetOptions(int on) {
  g_wanted = on != 0;
  ModLog("vk window    : vk_window=%d. %s", g_wanted ? 1 : 0,
         g_wanted ? "The window of the game gets the picture through a "
                    "swapchain of Vulkan, with no copy to the CPU"
                  : "The window of the game gets the picture through the "
                    "CPU and GDI");
}

void VkWindowToggle(const char* keys) {
  g_wanted = !g_wanted;
  ControllersGesturePulse(0.5f, g_wanted ? 0.30f : 0.08f);
  ModLog("test key     : %s, vk_window is %d from the next present: the "
         "window gets the picture %s",
         keys, g_wanted ? 1 : 0,
         g_wanted ? "through a swapchain of Vulkan" : "through the CPU and GDI");
}

bool VkWindowWanted() { return g_wanted && !g_unusable; }

bool VkWindowPresent(HWND window, const vk::Image& source, uint32_t width,
                     uint32_t height) {
  if (!g_wanted || g_unusable || window == nullptr || width == 0 ||
      height == 0) {
    return false;
  }
  if (!vk::Started() || source.image == VK_NULL_HANDLE) return false;
  if (!Load()) return false;
  if (!g_slots_made && !MakeSlots()) {
    Unusable("the command buffers of the window could not be made",
             VK_ERROR_OUT_OF_DEVICE_MEMORY);
    return false;
  }
  if (window != g_window) DropSurface();
  if (g_surface == VK_NULL_HANDLE && !MakeSurface(window)) return false;
  if (g_swapchain == VK_NULL_HANDLE || g_remake || width != g_asked_w ||
      height != g_asked_h) {
    const int made = MakeSwapchain(width, height);
    if (made < 0) return false;
    if (made == 0) return true;
  }

  const vk::Core& core = vk::Get();
  Slot& s = g_slots[g_slot];
  // The slot of three presents ago: the GPU has done it, or nearly.
  if (s.pending) {
    f.vkWaitForFences(core.device, 1, &s.fence, VK_TRUE, UINT64_MAX);
    s.pending = false;
  }
  // No wait for an image: with none free the window keeps its picture.
  uint32_t index = 0;
  VkResult r = w.vkAcquireNextImageKHR(core.device, g_swapchain, 0,
                                       s.acquired, VK_NULL_HANDLE, &index);
  if (r == VK_NOT_READY || r == VK_TIMEOUT) {
    ++g_no_image;
    return true;
  }
  if (r == VK_ERROR_OUT_OF_DATE_KHR) {
    g_remake = true;
    ++g_no_image;
    return true;
  }
  if (r == VK_ERROR_SURFACE_LOST_KHR) {
    DropSurface();
    ++g_no_image;
    return true;
  }
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
    Unusable("vkAcquireNextImageKHR failed", r);
    return false;
  }
  if (r == VK_SUBOPTIMAL_KHR) g_remake = true;
  if (index >= g_image_count) {
    Unusable("vkAcquireNextImageKHR gave an image that the swapchain does "
             "not have", VK_ERROR_UNKNOWN);
    return false;
  }

  f.vkResetCommandPool(core.device, s.pool, 0);
  VkCommandBuffer cb = s.cb;
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  f.vkBeginCommandBuffer(cb, &begin);
  vk::Barrier(cb, source.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, source.layers);
  vk::Barrier(cb, g_images[index], VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              1);
  VkImageBlit blit = {};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {static_cast<int32_t>(source.width),
                        static_cast<int32_t>(source.height), 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[1] = {static_cast<int32_t>(g_width),
                        static_cast<int32_t>(g_height), 1};
  f.vkCmdBlitImage(cb, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   g_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_LINEAR);
  vk::Barrier(cb, g_images[index], VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1);
  vk::Barrier(cb, source.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, source.layers);
  f.vkEndCommandBuffer(cb);

  VkSemaphoreSubmitInfo wait = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  wait.semaphore = s.acquired;
  wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkSemaphoreSubmitInfo signal = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  signal.semaphore = g_drawn[index];
  signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkCommandBufferSubmitInfo cb_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cb_info.commandBuffer = cb;
  VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.waitSemaphoreInfoCount = 1;
  submit.pWaitSemaphoreInfos = &wait;
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cb_info;
  submit.signalSemaphoreInfoCount = 1;
  submit.pSignalSemaphoreInfos = &signal;
  f.vkResetFences(core.device, 1, &s.fence);

  VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &g_drawn[index];
  present.swapchainCount = 1;
  present.pSwapchains = &g_swapchain;
  present.pImageIndices = &index;
  // The runtime of the headset uses the queue too.
  vk::LockQueue();
  r = f.vkQueueSubmit2(core.queue, 1, &submit, s.fence);
  VkResult shown = VK_ERROR_UNKNOWN;
  if (r == VK_SUCCESS) shown = w.vkQueuePresentKHR(core.queue, &present);
  vk::UnlockQueue();
  if (r != VK_SUCCESS) {
    Unusable("a submit of the window failed", r);
    return false;
  }
  s.pending = true;
  g_slot = (g_slot + 1) % kSlots;
  if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR) {
    g_remake = true;
  } else if (shown == VK_ERROR_SURFACE_LOST_KHR) {
    DropSurface();
  } else if (shown != VK_SUCCESS) {
    Unusable("vkQueuePresentKHR failed", shown);
    return true;
  }
  ++g_presents;
  VkFallbackPicture("the window, through the swapchain");
  return true;
}

void VkWindowStop() {
  if (!g_loaded) return;
  DropSurface();
}

void VkWindowReport() {
  if (g_presents == 0 && g_no_image == 0) return;
  ModLog("vk window    : %llu presents through the swapchain of the window, "
         "%llu with no free image, %llu swapchains made so far",
         g_presents, g_no_image, g_made);
  g_presents = 0;
  g_no_image = 0;
}
