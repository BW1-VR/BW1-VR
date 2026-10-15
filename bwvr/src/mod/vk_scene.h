// The Vulkan renderer of the scene (vk_scene.cpp, docs/backend.md, "The
// Vulkan renderer").

#pragma once

#include <stdint.h>

// vk_scene of bwvr.ini: 0 (the default) no Vulkan renderer. 1: with
// d3d7_thread=1 the D3D7 thread gives each call of a walk also to the Vulkan
// renderer, which draws both eyes into an image of its own, a layer for each
// eye. vk_scene_test_frame=N: the images of the walk of frame N go to
// bwvr-vk-left-NNNNN.bmp and bwvr-vk-right-NNNNN.bmp beside runblack.exe
// (VkSceneFrame).
void VkSceneSetOptions(int mode, int test_frame);
bool VkSceneOn();

// stereo.cpp, on the frame thread at the start of a frame, before the
// controllers count the frame. `frame` is the count of the controllers, which
// render_thread_test_frame also reads there. When it is vk_scene_test_frame,
// the first walk of this frame writes its images, with the number `name`:
// the number of the eye captures of this frame. So the image of the Vulkan
// renderer and the picture of Direct3D 9 with one number are of one walk.
void VkSceneFrame(uint64_t frame, uint64_t name);

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

// vk_scene_follow of bwvr.ini: 1 (the default) in a walk of one walk the
// renderer follows render target 0 of Direct3D 9, the viewport of Direct3D
// 7 and the clears of Direct3D 9: a draw or a clear for another target than
// the eye pictures does not go into them. 0: each draw goes into the eye
// pictures, in the viewport of the start of the walk, and the renderer makes
// no clear. Shift X (test_keys=1) changes it from the next walk.
void VkSceneSetFollow(int on);
void VkSceneFollowToggle(const char* keys);
// one_walk.cpp, in a pass: render target 0 of Direct3D 9 is this surface
// now. On the frame thread or on the D3D7 thread.
void VkSceneTarget(void* surface);
// d3d7_thread.cpp, after a SetViewport of Direct3D 7 in a walk: a
// D3DVIEWPORT7.
void VkSceneViewport(const void* viewport7);
// one_walk.cpp, in a pass: a Clear of Direct3D 9 (rects are D3DRECT).
void VkSceneClear(uint32_t count, const void* rects, uint32_t flags,
                  uint32_t color, float z);

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

// THE PANEL (vk_panel of bwvr.ini, 1 is the default). In a walk the mod
// binds the panel target of Direct3D 9 for the help text, the subtitle box,
// the Creature panel and the menus (gpu_bridge.cpp, GpuBridgePanelBind).
// With 1 the renderer draws the draws for that target into a panel image of
// its own, of the size of the eyes, cleared to 0 and the depth to 1 at its
// first draw in a walk. The subtitle card and the panel layer of a session
// with Vulkan then take that image. 0: the draws for the panel target are
// not drawn, and the card and the layer take the panel of Direct3D 9.
// Shift Y (test_keys=1) changes it from the next walk.
void VkSceneSetPanel(int on);
void VkScenePanelToggle(const char* keys);
// True when the card and the panel layer take the panel of the renderer.
bool VkScenePanelOn();
// On the frame thread after the walks of a frame: the panel image of the
// last walk of this frame that drew into the panel, when it has this size.
// image is a VkImage of one layer, in the layout of a color attachment and
// in vk::Core::color_format. False when no walk of this frame drew into the
// panel, or when the panel has another size.
bool VkScenePanelFor(uint32_t width, uint32_t height, uint64_t* image);
// THE SUBTITLE CARD, as GpuBridgePanelReadHalf: the rect x, y, width and
// height of the panel of this frame at half the size, as pixels for the
// CPU: BGRA, the color multiplied by the alpha, each pixel the mean of four.
// The pixels stay valid until VkScenePanelReadDone. Null when no walk of
// this frame drew into the panel, or when a step failed. It waits for the
// GPU.
const void* VkScenePanelReadHalf(const int rect[4], int* pitch,
                                 uint32_t* width, uint32_t* height);
void VkScenePanelReadDone();
