// The Vulkan renderer of the scene (vk_scene.cpp, docs/backend.md, "The
// Vulkan renderer").

#pragma once

#include <stdint.h>

// vk_scene of bwvr.ini: 0 (the default) no Vulkan renderer. 1: with
// d3d7_thread=1 the D3D7 thread gives each call of a walk also to the Vulkan
// renderer, which draws both eyes into an image of its own, a layer for each
// eye. vk_scene_test_frame=N: the images of the walk of frame N go to
// bwvr-vk-left-NNNNN.bmp and bwvr-vk-right-NNNNN.bmp beside runblack.exe.
void VkSceneSetOptions(int mode, int test_frame);
bool VkSceneOn();

// d3d7_thread.cpp, on the frame thread at the start of a walk, before the
// queue takes calls: the state of the device for the walk. Then the start
// goes into the queue.
void VkSceneWalkBegin(void* device7);
// d3d7_thread.cpp, on the frame thread at the end of a walk, before the
// queue empties.
void VkSceneWalkEnd();

// d3d7_thread.cpp, on the D3D7 thread, for each call of the queue.
void VkSceneRenderState(uint32_t state, uint32_t value);
void VkSceneStageState(uint32_t stage, uint32_t type, uint32_t value);
void VkSceneTexture(uint32_t stage, void* surface);
void VkSceneDraw(uint32_t type, uint32_t fvf, const void* vertices,
                 uint32_t vertex_count, const uint16_t* indices,
                 uint32_t index_count);

// draw_merge.cpp, on the frame thread: the game writes this surface (a Lock
// or a Blt to it). Its copy in Vulkan is read again at its next use.
void VkSceneSurfaceWritten(void* surface);

void VkSceneReport(uint64_t frames);

// On the frame thread after the end of a walk: the image of the last walk
// of one walk, once for each walk. image is a VkImage with a layer for each
// eye, in the layout of a color attachment and in vk::Core::color_format.
// False when no walk of one walk ended since the last call.
bool VkSceneEyes(uint64_t* image, uint32_t* width, uint32_t* height);

// vk_eyes of bwvr.ini, with xr_vulkan=1: 1 (the default) the eye pictures of
// a frame of one walk come from the Vulkan renderer, 0 from Direct3D 9.
// Shift C (test_keys=1) changes it in the session.
void VkSceneSetEyes(bool on);
bool VkSceneEyesWanted();
void VkSceneEyesToggle(const char* keys);
// The image for the eye swapchains of this frame: as VkSceneEyes, and only
// when the walk had the size of the swapchains. One line of the log when it
// did not.
bool VkSceneEyesFor(uint32_t width, uint32_t height, uint64_t* image);
