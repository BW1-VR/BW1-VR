// THE FALL BACK OF THE RENDERER VULKAN (vk_fallback.cpp, docs/backend.md,
// "The fall back of the renderer Vulkan").
//
// With backend=vulkan and vulkan_fallback=1 (the default) the mod writes the
// file bwvr-vulkan-pending.txt beside the game before it uses Vulkan. The
// first picture that goes to the window or to the headset deletes it. A
// fault of Vulkan writes it again and keeps it. When the file is there at a
// start, the proxy takes dxwrapper for that start (ddraw_proxy.cpp,
// TestVulkan). A save of the launcher deletes the file.
#pragma once

// In DllMain, after the proxy chose the backend. Writes the log line of a
// fall back, or the file when the start uses Vulkan. `dir` is the folder of
// the game with a backslash at the end, `ini` the path of bwvr.ini.
void VkFallbackStart(const char* dir, const char* ini);

// The first picture of the renderer went to the window or to the headset.
// `where` names it for the log. It deletes the file one time.
void VkFallbackPicture(const char* where);

// True until the first picture, when the file is there.
bool VkFallbackWaiting();

// A fault of Vulkan that leaves the game with no picture. The file stays
// with the reason, and the next start uses dxwrapper.
void VkFallbackFault(const char* what);

// The game loop ended as it does for Quit (the hook of free_system). A
// start that waited for the headset and ended before its first picture is
// not a fault, so the file goes when no fault came. A fault and a stop of
// the process do not come here, and the file stays.
void VkFallbackEnd();

// vulkan_fallback_test=1: the game makes its DirectDraw object, and the mod
// stops the process before the first walk, as a fault of Vulkan can.
void VkFallbackTestStop();
