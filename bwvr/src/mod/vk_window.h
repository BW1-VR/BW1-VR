// The window of the game with a swapchain of Vulkan (vk_window.cpp,
// docs/backend.md, "The window with Vulkan").
//
// The present of the DirectDraw of the mod (vk_ddraw.cpp) puts the picture
// of the last walk into the window of the game. With vk_window=1 a swapchain
// of Vulkan on that window takes it: a blit with a linear filter on the GPU
// to the size of the client area, and a present in the queue of the
// renderer. The picture does not go through the CPU, and the frame thread
// does not wait for the GPU. With vk_window=0, or when the window cannot
// have a swapchain, the picture goes through the CPU and GDI
// (VkScenePresentPixels).
//
// The swapchain needs VK_KHR_surface and VK_KHR_win32_surface on the
// instance and VK_KHR_swapchain on the device. vk_core.cpp and the OpenXR
// session (xr_screen.cpp) turn them on when the GPU has them
// (vk::Core::window_present).

#pragma once

#include <windows.h>

#include "vk_core.h"

// vk_window of bwvr.ini: 1 (the default) a swapchain of Vulkan. 0: the way
// through the CPU and GDI. Shift D (test_keys=1) changes it from the next
// present.
void VkWindowSetOptions(int on);
void VkWindowToggle(const char* keys);
bool VkWindowWanted();
// The value of vk_window for the debug page, or kDebugCannot.
int VkWindowValue();

// Blits layer 0 of `source` (in the layout of a color attachment, which it
// keeps) to the client area of `window`, `width` by `height` pixels, and
// presents it. False when the picture did not go to the window: then the
// caller takes the way through the CPU. True also for a present that the
// swapchain had no image for: the window keeps the picture before.
bool VkWindowPresent(HWND window, const vk::Image& source, uint32_t width,
                     uint32_t height);

// The swapchain and the surface go, and GDI can draw into the window again.
// A later present makes them again.
void VkWindowStop();

// For the report of each 450 frames.
void VkWindowReport();
