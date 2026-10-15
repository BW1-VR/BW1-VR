// The Vulkan core of the mod (vk_core.cpp, docs/backend.md, "The Vulkan
// renderer"): the loader, one instance, one device with one queue, and
// helpers for memory, buffers, images and one-time command buffers.
//
// The mod loads vulkan-1.dll itself and reads each function from it, so the
// DLL of the mod has no import of Vulkan and starts on a PC with no Vulkan.
// Vulkan 1.3 is the floor: dynamic rendering, synchronization2 and the
// extended dynamic state are core there.
//
// THE VULKAN OF AN OPENXR SESSION (xr_vulkan=1). Then the runtime makes the
// instance and the device (XrScreen::UseVulkan), and the core takes them
// (UseExternal), so the images of the renderer go into the swapchains of the
// session with no copy between two devices. WaitForExternal(true) makes Start
// wait for them: Start returns false and Pending() is true until UseExternal.
// The runtime uses the queue too, so each submit and each wait for the idle
// device holds the lock of the queue (LockQueue), and the session gives that
// lock.

#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdint.h>

namespace vk {

// The functions of Vulkan that the mod uses, read from the loader.
#define BWVR_VK_INSTANCE_FUNCTIONS(X)        \
  X(vkDestroyInstance)                       \
  X(vkEnumeratePhysicalDevices)              \
  X(vkGetPhysicalDeviceProperties)           \
  X(vkGetPhysicalDeviceFeatures2)            \
  X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkGetPhysicalDeviceMemoryProperties)     \
  X(vkGetPhysicalDeviceFormatProperties)     \
  X(vkCreateDevice)                          \
  X(vkGetDeviceProcAddr)

#define BWVR_VK_DEVICE_FUNCTIONS(X)   \
  X(vkDestroyDevice)                  \
  X(vkGetDeviceQueue)                 \
  X(vkDeviceWaitIdle)                 \
  X(vkQueueSubmit2)                   \
  X(vkQueueWaitIdle)                  \
  X(vkAllocateMemory)                 \
  X(vkFreeMemory)                     \
  X(vkMapMemory)                      \
  X(vkUnmapMemory)                    \
  X(vkFlushMappedMemoryRanges)        \
  X(vkCreateBuffer)                   \
  X(vkDestroyBuffer)                  \
  X(vkGetBufferMemoryRequirements)    \
  X(vkBindBufferMemory)               \
  X(vkCreateImage)                    \
  X(vkDestroyImage)                   \
  X(vkGetImageMemoryRequirements)     \
  X(vkBindImageMemory)                \
  X(vkCreateImageView)                \
  X(vkDestroyImageView)               \
  X(vkCreateSampler)                  \
  X(vkDestroySampler)                 \
  X(vkCreateCommandPool)              \
  X(vkDestroyCommandPool)             \
  X(vkResetCommandPool)               \
  X(vkAllocateCommandBuffers)         \
  X(vkBeginCommandBuffer)             \
  X(vkEndCommandBuffer)               \
  X(vkCreateFence)                    \
  X(vkDestroyFence)                   \
  X(vkWaitForFences)                  \
  X(vkResetFences)                    \
  X(vkCreateShaderModule)             \
  X(vkDestroyShaderModule)            \
  X(vkCreateDescriptorSetLayout)      \
  X(vkDestroyDescriptorSetLayout)     \
  X(vkCreatePipelineLayout)           \
  X(vkDestroyPipelineLayout)          \
  X(vkCreateGraphicsPipelines)        \
  X(vkDestroyPipeline)                \
  X(vkCreateDescriptorPool)           \
  X(vkDestroyDescriptorPool)          \
  X(vkResetDescriptorPool)            \
  X(vkAllocateDescriptorSets)         \
  X(vkFreeDescriptorSets)             \
  X(vkUpdateDescriptorSets)           \
  X(vkCmdPipelineBarrier2)            \
  X(vkCmdCopyBufferToImage)           \
  X(vkCmdCopyImageToBuffer)           \
  X(vkCmdBlitImage)                   \
  X(vkCmdBeginRendering)              \
  X(vkCmdEndRendering)                \
  X(vkCmdBindPipeline)                \
  X(vkCmdBindDescriptorSets)          \
  X(vkCmdBindVertexBuffers)           \
  X(vkCmdBindIndexBuffer)             \
  X(vkCmdPushConstants)               \
  X(vkCmdSetViewport)                 \
  X(vkCmdSetScissor)                  \
  X(vkCmdSetCullMode)                 \
  X(vkCmdSetFrontFace)                \
  X(vkCmdSetPrimitiveTopology)        \
  X(vkCmdSetDepthTestEnable)          \
  X(vkCmdSetDepthWriteEnable)         \
  X(vkCmdSetDepthCompareOp)           \
  X(vkCmdDrawIndexed)                 \
  X(vkCmdDraw)                        \
  X(vkCmdClearAttachments)

struct Functions {
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
  PFN_vkCreateInstance vkCreateInstance;
#define BWVR_VK_DECLARE(name) PFN_##name name;
  BWVR_VK_INSTANCE_FUNCTIONS(BWVR_VK_DECLARE)
  BWVR_VK_DEVICE_FUNCTIONS(BWVR_VK_DECLARE)
#undef BWVR_VK_DECLARE
};

// The functions, after Start.
extern Functions f;

struct Core {
  VkInstance instance;
  VkPhysicalDevice physical;
  VkDevice device;
  VkQueue queue;
  uint32_t queue_family;
  VkPhysicalDeviceMemoryProperties memory;
  // The largest anisotropy of a sampler, or 0 when the GPU has no
  // anisotropic filter.
  float max_anisotropy;
  // The format of the eye images of the renderer. A session with Vulkan
  // gives the UNORM format of its swapchains, so an eye image goes into a
  // swapchain image by a copy of its bytes.
  VkFormat color_format;
  bool external;  // the instance and the device are the ones of a session
  char gpu[256];
};

struct External {
  PFN_vkGetInstanceProcAddr get_instance_proc_addr;
  VkInstance instance;
  VkPhysicalDevice physical;
  VkDevice device;
  uint32_t queue_family;
  uint32_t queue_index;
  float max_anisotropy;
  VkFormat color_format;
  void (*lock)();  // the lock of the queue of the session
  void (*unlock)();
};
void WaitForExternal(bool on);
void UseExternal(const External& external);
bool Pending();

// The lock of the queue. Each submit and each wait for the idle device
// holds it.
void LockQueue();
void UnlockQueue();

// Starts Vulkan 1.3 on the first GPU that has it, a dedicated one before an
// integrated one, with multiview (one draw for both eyes, each in a layer of
// the target). False with a reason in the log when it cannot.
bool Start();
bool Started();
const Core& Get();
void Stop();

// A buffer with its memory. host: the CPU can write it (mapped while it
// lives).
struct Buffer {
  VkBuffer buffer;
  VkDeviceMemory memory;
  VkDeviceSize size;
  void* mapped;
};
bool MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host,
                Buffer* out);
void FreeBuffer(Buffer* b);

// An image of one sample with its memory and a view of all its levels and
// layers (an array view when it has more than one layer).
struct Image {
  VkImage image;
  VkDeviceMemory memory;
  VkImageView view;
  VkFormat format;
  uint32_t width, height, levels, layers;
};
bool MakeImage(uint32_t width, uint32_t height, uint32_t levels,
               VkFormat format, VkImageUsageFlags usage,
               VkImageAspectFlags aspect, Image* out, uint32_t layers = 1);
void FreeImage(Image* image);

// A command buffer for one job: Begin gives it, End submits it and waits.
VkCommandBuffer BeginOnce();
bool EndOnce(VkCommandBuffer cb);

// A barrier of one image from one layout to another, for all stages. The
// layers from base_layer.
void Barrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect,
             VkImageLayout from, VkImageLayout to, uint32_t levels,
             uint32_t layers = 1, uint32_t base_layer = 0);

}  // namespace vk
