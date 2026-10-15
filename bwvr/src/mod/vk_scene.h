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
// capture: the frame saves pictures (F12, eye_capture.cpp), so the walk
// writes its images too.
void VkSceneFrame(uint64_t frame, uint64_t name, bool capture);

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
// backend=vulkan (gpu_bridge.cpp, GpuBridgePanelBind): the draws after this
// go into the panel (true) or into the eye pictures (false). With no
// Direct3D 9 the panel target is not a surface, so the bind comes here.
void VkScenePanelBind(bool panel);
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

// THE WALK WITHOUT DIRECT3D 9 (vk_only of bwvr.ini, 1 is the default). In a
// walk of one walk whose eye pictures come from the renderer, the D3D7
// thread gives no draw to dxwrapper: the renderer alone draws them. The
// calls that set a state still go to dxwrapper, because the next walk reads
// its state from the device. It needs vk_panel=1, so the help text has a
// panel. stereo.cpp says for each frame when its walk can go without
// Direct3D 9 (VkSceneOnlyFrame): in a session with Vulkan with the eyes of
// the renderer, or on the desktop with vk_only_test=1. 0: Direct3D 9 draws
// each walk too. Shift U (test_keys=1) changes it from the next walk.
void VkSceneSetOnly(int on, int test);
void VkSceneOnlyToggle(const char* keys);
// vk_only_test of bwvr.ini: the walks of the desktop go without Direct3D 9
// too, so a desktop run shows the pictures of the renderer alone.
bool VkSceneOnlyTest();
// stereo.cpp, on the frame thread before the walks of a frame: the walk of
// one walk of this frame can go without Direct3D 9.
void VkSceneOnlyFrame(bool can);
// d3d7_thread.cpp, on the D3D7 thread: the walk now goes without
// dxwrapper, so a draw goes to the renderer alone.
bool VkSceneOnlyNow();

// THE UPLOADS OF A WALK (vk_upload_queue of bwvr.ini, 1 is the default). A
// texture that the game wrote again (a Lock or a Blt) is read at its next
// use in a walk. 1: its copy to the GPU goes into a command buffer that the
// end of the walk submits before the buffer of the walk, with no wait on the
// D3D7 thread. 0: each copy goes to the GPU at once, and the D3D7 thread
// waits until the GPU is done. Shift I (test_keys=1) changes it from the
// next walk. vk_scene_reread_test=N (a test key): each walk reads N
// textures again, in turn, so a desktop run makes many reads in a short
// time.
void VkSceneSetUploads(int queue, int reread_test);
void VkSceneUploadsToggle(const char* keys);

// THE BACKEND OF THE MOD (backend=vulkan, vk_ddraw.cpp). When it is on, the
// device of the mod gives each call to the renderer on the frame thread,
// with no D3D7 thread: VkSceneWalkBegin and VkSceneWalkEnd at each
// BeginScene and EndScene, and the state, the textures, the viewport, the
// clears and the draws as they come. It turns the renderer on.
void VkSceneSetDirect(bool on);
// backend=vulkan, on the frame thread after a FinishFrame: the image of the
// last walk (a VkImage in the layout of a color attachment), the layer
// that it drew into, and its size, for the session (XrScreen::SubmitVulkan,
// SetEyeVulkan). False when there is none, or when the CPU wrote into the
// render target after it: then the pixels of the CPU are the picture.
bool VkSceneLastPicture(uint64_t* image, uint32_t* layer, uint32_t* width,
                        uint32_t* height);
bool VkSceneDirect();
// The picture of the last walk of the left eye (or of a scene with no eye
// pass), at width by height with a linear filter, as BGRA pixels for the
// CPU, for the window on the monitor (see vk_present_late). Null before the
// first walk, and with vk_present_late=1 also at the first present after a
// change of the size.
const uint8_t* VkScenePresentPixels(uint32_t width, uint32_t height,
                                    int* pitch);
// FOVEATED RENDERING (foveation of bwvr.ini, docs/backend.md, "Foveated
// rendering"). level: 0 (the default) each pixel of the eye pictures has the
// full rate. 1 light, 2 medium, 3 strong: the part around the centre of the
// fovea has the full rate and the rest fewer shades (VK_KHR_fragment_shading_
// rate). inner_deg and outer_deg (foveation_inner_deg, foveation_outer_deg)
// replace the degrees of the level when they are not 0. Only the eye
// pictures of the Vulkan renderer have it. show (foveation_show, a test
// key): a colour over the coarse parts, blue for 2 by 2 and red for 4 by 4.
void VkSceneSetFoveation(int level, int inner_deg, int outer_deg, int show);
// The test key: foveation goes on, on with the colour of show, and off.
void VkSceneFoveationToggle(const char* keys);
// FOVEATED RESOLUTION (fovea_res of bwvr.ini, docs/backend.md, "Foveated
// resolution"). on: 0 (the default) the eye pictures have the pixels of the
// render. 1: the draws of the eyes of a walk go into an outer image with
// outer_pct percent of the pixels on each side (fovea_res_outer_pct, 50)
// and into a centre image with center_pct percent (fovea_res_center_pct,
// 100) of a rect inset_deg degrees on each side of the centre of the fovea
// (fovea_res_center_deg, 25). The eye picture is made from the two. Only the
// eye pictures of the Vulkan renderer have it.
void VkSceneSetFoveaRes(int on, int outer_pct, int center_pct, int inset_deg);
// The test key: fovea_res goes between 0 and 1.
void VkSceneFoveaResToggle(const char* keys);
// stereo.cpp, on the frame thread before the walks of a frame: the centre of
// the fovea as tangents of the eye (x right, y up), from an eye tracker.
// valid false: straight ahead.
void VkSceneSetGaze(bool valid, float gx, float gy);
// vk_texture_reuse of bwvr.ini: 1 (the default) a texture that a walk reads
// again with the same size goes into the image it has, when no draw of the
// walk drew with it yet, and its pixels go into a ring of the slot. 0: each
// read makes a new image and a staging buffer.
void VkSceneSetTextureReuse(int on);
// vk_present_late of bwvr.ini: 1 (the default) VkScenePresentPixels gives
// the picture of the present before, with no wait for the GPU. 0: it waits
// for the picture of this present.
void VkSceneSetPresentLate(int on);
