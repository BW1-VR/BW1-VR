// The Vulkan renderer of the scene (vk_scene.cpp, docs/backend.md, "The
// Vulkan renderer").

#pragma once

#include <stdint.h>

// vk_scene of bwvr.ini: 0 (the default) no Vulkan renderer. 1: the shadow
// test. With d3d7_thread=1 the D3D7 thread gives each call of a walk also to
// the Vulkan renderer, which draws the left eye into an image of its own.
// The picture of the game stays the one of dxwrapper.
// vk_scene_test_frame=N: the image of the walk of frame N goes to
// bwvr-vk-left-NNNNN.bmp beside runblack.exe.
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
