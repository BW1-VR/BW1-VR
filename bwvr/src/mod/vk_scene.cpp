// The Vulkan renderer of the scene (vk_scene=1, docs/backend.md, "The Vulkan
// renderer").
//
// WHY. Each draw of a walk goes from the game through Direct3D 7, dxwrapper,
// Direct3D 9 and the driver. Each layer costs time on the CPU for each draw,
// and on a headset with an ARM CPU the x86 code of each layer runs through
// an emulator. This renderer takes the calls of Direct3D 7 of a walk and
// draws them with Vulkan, with no dxwrapper and no Direct3D 9.
//
// THE SHADOW TEST (vk_scene=1). With d3d7_thread=1 the D3D7 thread gives
// each call of the queue to dxwrapper as before, and also to this renderer.
// The renderer draws both eyes into an image of its own, with a layer for
// each eye. The picture of the game stays the one of dxwrapper.
// vk_scene_test_frame=N writes the images of the walk of frame N to files,
// so a desktop run compares them with the pictures of Direct3D 9 of the same
// walk. Frame N is the frame that starts with the count N of the controllers,
// as for render_thread_test_frame=N (VkSceneFrame). The controllers count the
// frame before its walk, so the walk itself starts with the count N + 1. The
// files have the number of the eye captures of that frame (stereo.cpp), so
// the image of the Vulkan renderer and the picture of Direct3D 9 of one walk
// have one number.
//
// BOTH EYES. In one walk the game draws the left eye, and one walk makes the
// right eye from it (one_walk.cpp). Here each draw goes to both layers at
// once (multiview), and the vertex shader moves each vertex of the right eye
// by per_rhw * rhw, unless each vertex of the batch has an rhw of 1 or a z
// of 1 or more (one_walk_math.h, CopyForRightEye). A batch of the interface
// for one eye only (OneWalkModeNow) goes to that layer only. With no one walk
// the walk draws the left layer only.
//
// WHAT IT MAKES. The game draws vertices with their place on the screen
// (0x1C4: XYZRHW, diffuse, specular, one texture). docs/backend.md, "The
// calls of DirectDraw and Direct3D 7 in a session", has the states that a
// walk sets. The state of the other states comes from the device at the
// start of each walk. The fragment shader makes texture stage 0, texture
// stage 1 (THE SECOND STAGE), the alpha test, the specular color and the fog
// of the vertices (vk_scene.frag). The blend has a pipeline for each pair of
// factors. The cull mode, the depth test, the depth write, the depth
// function and the primitive type are dynamic states.
//
// THE EYES IN THE HEADSET (vk_eyes=1, with xr_vulkan=1). The renderer then
// has the instance and the device of the OpenXR session (vk_core.h), and it
// waits for them before its first walk. Each walk draws into the next of two
// targets, so the copy of one frame into the swapchains can still read its
// target while the next walk draws. VkSceneEyes gives the target of the last
// walk to the frame thread (stereo.cpp), and the session copies its two
// layers into the two eye swapchains (XrScreen::SetEyeVulkan). The format of
// the targets is the UNORM format of the swapchains, so the copy changes no
// byte.
//
// THE TEXTURES. A texture of the game is a DirectDraw surface of dxwrapper.
// At its first use the renderer reads it with Lock, makes RGBA8 of it, and
// gives the copy its mip levels with blits. A Lock or a Blt of the game on a
// surface (draw_merge.cpp) marks it, and its next use reads it again.
//
// THE TARGET, THE VIEWPORT AND THE CLEARS (vk_scene_follow=1). In a walk the
// mod can bind a target of its own on the device of Direct3D 9, for example
// the panel target of the interface (gpu_bridge.cpp, GpuBridgePanelBind).
// The draws of the game that follow go to that target, and not to the eye
// pictures. So the renderer follows render target 0 of Direct3D 9, as one
// walk does (one_walk.cpp, InWide): its image is the target of the eyes at
// the start of the walk, and a draw or a clear while another target is bound
// does not go into it. It also follows the viewport of Direct3D 7
// (SetViewport, d3d7_thread.cpp), and it makes each clear of Direct3D 9 in
// the walk (one_walk.cpp, h_Clear) in the rect of that viewport, for the eyes
// of the mode of one walk. A call on the frame thread goes into the queue of
// the D3D7 thread (D3D7ThreadPost), so it comes in the order of the draws. A
// call on the D3D7 thread is in that order already. Without one walk the
// target of the eyes is not known, and each draw goes into the image.
//
// THE PANEL (vk_panel=1). The panel target of Direct3D 9 (GpuBridgeIsPanel)
// has a panel image here, one for each target of the eyes, of their size,
// with a depth of its own. A draw while the panel target is bound goes into
// that image, for no eye: the rendering of the eyes ends, and a rendering
// of the panel starts. Its first rendering in a walk clears it to 0 and its
// depth to 1, as GpuBridgePanelBind clears the panel target at its first
// bind. The alpha has the blend of the panel target (ONE, INVSRCALPHA), so
// the panel holds the color multiplied by the alpha. The next draw for the
// eyes ends the rendering of the panel and starts the one of the eyes again,
// with their pixels and depth. A clear while the panel target is bound is
// the clear of GpuBridgePanelBind, and the first rendering makes it. The
// subtitle card (VkScenePanelReadHalf) and the panel layer of a session with
// Vulkan (VkScenePanelFor) read the panel of the last walk that drew into
// it in the frame.
//
// THE SECOND STAGE (vk_stage1=1). The game puts a second texture on texture
// stage 1 for some draws, for example the light maps of the rooms of the
// Temple and of the land (D3DTOP_MODULATE2X or D3DTOP_ADD of the texture and
// the current color).
// These draws come in a format with two sets of texture coordinates. The
// renderer then binds that texture as descriptor set 1, and the set of
// coordinates of D3DTSS_TEXCOORDINDEX of stage 1 goes into the ring after
// the vertices, 8 bytes for each vertex, as vertex buffer 1. A draw with no
// stage 1 binds the set of stage 0 as set 1 and its vertices as buffer 1,
// so each draw has each binding, and the shader does not read them. With
// vk_stage1=0 the renderer makes stage 0 alone.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d.h>
#include <intrin.h>
#include <smmintrin.h>
#include <ddraw.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "d3d7_thread.h"
#include "ui_panel.h"
#include "one_walk.h"
#include "one_walk_math.h"
#include "vk_core.h"
#include "vk_scene.h"
#include "../common/bw_addresses.h"
#include "fovea_res_math.h"
#include "foveation_math.h"
#include "vk_scene_math.h"
#include "vk_texture_math.h"
#include "vk_scene_frag.h"
#include "vk_scene_vert.h"

void ModLog(const char* format, ...);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
// stereo.cpp: the eye pass now, or -1 outside the eye passes.
int StereoPassEye();
// intro_vr.cpp: the eye pass of the intro in stereo with intro_eye_trails,
// or -1.
int IntroPassEye();

namespace {

using vk::f;

int g_mode = 0;
int g_test_frame = 0;
// THE BACKEND OF THE MOD (backend=vulkan, vk_ddraw.cpp): the calls come
// from the device of the mod on the frame thread, with no D3D7 thread, and
// each scene of the device is a walk.
bool g_direct = false;
// The target of the last walk for the window (VkScenePresentPixels), and the
// small copy of it that the CPU reads.
int g_present_index = 0;
bool g_present_ready = false;
bool g_failed = false;
bool g_ready = false;

// THE STATE, on the D3D7 thread. Render states 0 to 63 and the states 0 to
// 31 of stages 0 and 1, as Direct3D 7 numbers them.
const int kStates = 64;
const int kStageStates = 32;
uint32_t g_rs[kStates];
uint32_t g_ts[2][kStageStates];
void* g_texture = nullptr;  // stage 0
void* g_texture1 = nullptr;  // stage 1
D3DVIEWPORT7 g_viewport = {};

// THE TARGET, THE VIEWPORT AND THE CLEARS, on the D3D7 thread.
bool g_walk_follow = false;     // vk_scene_follow, for the walk now
void* g_eye_target = nullptr;   // the target of the eyes in Direct3D 9
void* g_draw_target = nullptr;  // render target 0 of Direct3D 9 now
// On the frame thread: vk_scene_follow now (the test key changes it).
bool g_follow = true;
// On the frame thread: the next walk writes its images to files
// (VkSceneFrame), with this number.
bool g_picture_next = false;
uint64_t g_picture_name = 0;

// The seed of a walk: the frame thread reads the state, and the D3D7 thread
// takes it with the start of the walk.
struct Seed {
  uint32_t rs[kStates];
  uint32_t ts[2][kStageStates];
  void* texture;
  void* texture1;
  D3DVIEWPORT7 viewport;
  uint64_t frame;  // the number of the files of the images
  bool picture;    // the walk writes its images to files
  bool both;  // a walk of one walk: both layers get a picture
  bool follow;
  bool panel;    // vk_panel
  bool stage1;   // vk_stage1
  bool only;     // the walk goes without Direct3D 9 (vk_only)
  bool upload_queue;  // vk_upload_queue
  int reread;    // vk_scene_reread_test: the textures that the walk reads again
  void* target;  // the target of the eyes, or null when it is not known
  int eye;       // the eye pass of the walk (StereoPassEye), -1 for none
  int intro_eye;  // the eye pass of the intro in stereo, -1 for none
  // FOVEATED RENDERING: the fovea of the walk (inner_deg 0: none), and the
  // centre of the fovea as tangents of the eye.
  foveation_math::Params fovea;
  float gaze[2];
  bool fovea_show;  // foveation_show: the marks of the coarse parts
  // FOVEATED RESOLUTION: the walk goes into an outer image and a centre
  // image (fovea_res), with their factors in percent and the degrees of
  // the centre.
  bool mres;
  float mres_outer, mres_center, mres_deg;
};

// A bind of render target 0 of Direct3D 9 in a walk.
struct TargetArgs {
  void* surface;
  uint32_t panel;  // 1: the surface is the panel target
};

// A clear of Direct3D 9 in the walk. A clear with more rects clears the
// whole viewport.
const uint32_t kClearRects = 16;
struct ClearArgs {
  uint32_t count;
  uint32_t flags;
  uint32_t color;
  float z;
  D3DRECT rects[kClearRects];
};

// THE TARGETS: two, for the walks in turn, each with a layer for each eye
// (THE EYES IN THE HEADSET). The depth is one.
vk::Image g_colors[2] = {};
int g_target = 0;
vk::Image g_depth = {};
const VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
// The texture images are RGBA8.
const VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;

// THE PANEL: an image for each target of the eyes, and one depth.
vk::Image g_panels[2] = {};
vk::Image g_panel_depth = {};
bool g_walk_panel = false;       // vk_panel, for the walk now
bool g_draw_panel = false;       // render target 0 is the panel target now
bool g_panel_drawn = false;      // the walk drew into the panel
bool g_rendering_panel = false;  // the rendering now is the one of the panel
// On the frame thread: vk_panel now (the test key changes it).
bool g_panel_mode = true;
// The panel of the last walk that drew into it. The D3D7 thread writes it
// at the end of the walk, and the frame thread reads it after the end.
// g_walk_serial counts the walks, and g_panel_walk is the count of the walk
// that drew into the panel. g_frame_walks is the count at the start of the
// frame (VkSceneFrame).
volatile LONG g_walk_serial = 0;
volatile LONG g_panel_walk = 0;
LONG g_frame_walks = 0;
int g_panel_index = 0;  // in g_panels
LONG g_panel_taken = 0;  // the walk of the last VkScenePanelFor
// The half size copy for the subtitle card.
vk::Image g_half = {};
vk::Buffer g_half_buffer = {};
bool g_half_locked = false;
// Pipelines of the panel: no multiview, and the alpha blend of the panel.
const uint32_t kPanelKey = 1u << 24;

// THE WALK WITHOUT DIRECT3D 9 (vk_only). On the frame thread: the key now,
// the test key of the desktop, the frame of stereo.cpp, and a walk of
// another size than the swapchains (VkSceneEyesFor). On the D3D7 thread:
// the walk now.
bool g_only_mode = true;
bool g_only_test = false;
bool g_only_frame = false;
bool g_size_differs = false;
volatile bool g_walk_only = false;
uint64_t g_only_walks = 0;  // for the report

// The last walk that ended, for VkSceneEyes. The D3D7 thread writes it
// before the end of the walk, and the frame thread reads it after the end.
volatile LONG g_done_serial = 0;
LONG g_taken_serial = 0;
uint64_t g_done_image = 0;
uint32_t g_done_width = 0;
uint32_t g_done_height = 0;
bool g_said_pending = false;

// The target of the walk now.
vk::Image& Target() { return g_colors[g_target]; }

// THE PIPELINES: one for each blend (off, or a pair of factors).
VkShaderModule g_vert = VK_NULL_HANDLE;
VkShaderModule g_frag = VK_NULL_HANDLE;
VkDescriptorSetLayout g_set_layout = VK_NULL_HANDLE;
VkPipelineLayout g_layout = VK_NULL_HANDLE;
VkDescriptorPool g_pool = VK_NULL_HANDLE;
struct PipelineSlot {
  uint32_t key;  // 0 for no blend, else 1 << 16 | src << 8 | dst
  VkPipeline pipeline;
};
const int kMaxPipelines = 32;
PipelineSlot g_pipelines[kMaxPipelines];
int g_pipeline_count = 0;

// The texture of no texture: one white texel.
struct Texture;
Texture* g_white = nullptr;

// THE SAMPLERS, by their key.
struct SamplerSlot {
  uint32_t key;
  VkSampler sampler;
};
const int kMaxSamplers = 64;
SamplerSlot g_samplers[kMaxSamplers];
int g_sampler_count = 0;

// THE TEXTURES, by the address of the surface.
struct Texture {
  void* surface;
  vk::Image image;
  bool dirty;
  uint64_t used_walk;  // g_walk_number of the last walk that drew with it
  struct SetSlot {
    VkSampler sampler;
    VkDescriptorSet set;
  } sets[8];
  int set_count;
};
const int kMaxTextures = 4096;
Texture* g_textures = nullptr;
int g_texture_count = 0;
SRWLOCK g_texture_lock = SRWLOCK_INIT;  // the dirty marks of the frame thread
// FOVEATED RESOLUTION: the layers of the outer and the centre image as
// textures for the eye picture, [image][eye].
Texture g_mres_texture[2][2] = {};

// THE FRAME. Two slots, so the CPU fills one while the GPU draws the other.
// THE UPLOADS OF A WALK (vk_upload_queue=1). A texture that the walk reads
// again goes into the upload buffer of the slot, with no wait for the GPU.
// The end of the walk submits it before the buffer of the walk, in one
// submit, so the queue copies each texture before a draw reads it. The
// staging buffers stay until the fence of the slot says the GPU is done.
const int kSlotStaging = 64;
struct Slot {
  VkCommandPool pool;
  VkCommandBuffer cb;
  VkCommandBuffer upload_cb;
  bool upload_open;  // upload_cb has commands of this walk
  VkFence fence;
  // THE TIME OF THE GPU: a timestamp at the start and at the end of the
  // buffer of the walk.
  VkQueryPool query;
  bool query_written;
  vk::Buffer ring;
  VkDeviceSize used;
  bool pending;
  vk::Image garbage[64];
  int garbage_count;
  vk::Buffer staging[kSlotStaging];
  int staging_count;
  // THE RING OF THE STAGING (vk_texture_reuse=1): the pixels of the reads of
  // the walk, one after the other. It is empty again after the fence.
  vk::Buffer stage_ring;
  VkDeviceSize stage_used;
  // The descriptor sets of the textures that the walk read again. They go
  // back to the pool when the fence of the slot says the GPU is done.
  VkDescriptorSet dead_sets[1024];
  int dead_count;
  // THE CHECK FOR A BLACK PICTURE (vk_black_check): 16 by 16 points of each
  // layer of the eye picture of the walk, and what the walk drew.
  vk::Buffer probe;
  bool probe_written;
  uint64_t probe_walk;
  char probe_text[1024];
};
// On the D3D7 thread: vk_upload_queue for the walk now. On the frame
// thread: the key now (the test key changes it).
bool g_walk_upload_queue = false;
bool g_upload_queue = true;
uint64_t g_uploads_queued = 0;  // for the report
uint64_t g_upload_wait_ticks = 0;  // the uploads that waited, on the CPU
// vk_texture_reuse of bwvr.ini: 1 (the default) a texture that the walk
// reads again with the same size goes into the image it has, when no draw of
// the walk drew with it yet, and the pixels go into the ring of the slot. So
// a read makes no allocation of the GPU. 0: each read makes a new image and
// a staging buffer of its own.
bool g_texture_reuse = true;
const VkDeviceSize kStageRingBytes = 32ull << 20;
uint64_t g_walk_number = 0;      // counts each walk, from the start
uint64_t g_walk_draws_start = 0;  // g_draws at the start of the walk now
uint64_t g_reused = 0;           // for the report
uint64_t g_ring_stages = 0;
uint64_t g_fence_waits = 0;      // walks that waited for the fence of their slot
uint64_t g_fence_wait_ticks = 0;
// The descriptor sets: in use now, and made since the start, against the
// room of the pool (kMaxTextures * 8).
int64_t g_sets_live = 0;
uint64_t g_sets_made = 0;
uint64_t g_set_failures = 0;
bool g_said_set_failure = false;
// vk_scene_reread_test=N (a test key): each walk reads N textures again,
// so a desktop run makes many reads in a short time.
int g_reread_test = 0;
int g_reread_next = 0;  // on the D3D7 thread: the next texture to read
const VkDeviceSize kRingBytes = 32u << 20;
Slot g_slots[2];
int g_slot = 0;
bool g_recording = false;
bool g_picture = false;  // this walk writes its image to a file
uint64_t g_picture_frame = 0;

// For the report.
uint64_t g_walks = 0;
uint64_t g_draws = 0;
// The draws that the renderer does not draw, for each reason.
uint64_t g_skipped = 0;          // all of them
uint64_t g_skip_format = 0;      // a vertex format with no XYZRHW
uint64_t g_skip_ring = 0;        // no room in the ring of the vertices
uint64_t g_skip_state = 0;       // no pipeline or no descriptor set
uint64_t g_skip_target = 0;      // a draw for another target
uint64_t g_converted = 0;        // a format of XYZRHW other than 0x1C4
// THE TARGET, THE VIEWPORT AND THE CLEARS.
uint64_t g_target_calls = 0;     // binds of render target 0
uint64_t g_viewport_calls = 0;
uint64_t g_clears = 0;           // made
uint64_t g_clears_one_eye = 0;   // of them for one eye only
uint64_t g_clears_off = 0;       // for another target, not made
uint64_t g_clears_panel = 0;     // while the panel target is bound
uint64_t g_panel_draws = 0;      // draws into the panel
uint64_t g_panel_walks = 0;      // walks that drew into the panel
uint64_t g_panel_reads = 0;      // half size copies for the subtitle card
uint64_t g_panel_read_failures = 0;
uint64_t g_panel_layers = 0;     // panels for the panel layer of a session
uint32_t g_said_formats[8] = {};  // the formats that the log has
int g_said_format_count = 0;
uint64_t g_uploads = 0;
uint64_t g_upload_bytes = 0;
uint64_t g_right_only = 0;
uint64_t g_stage1 = 0;  // draws with texture stage 1 on
uint64_t g_stage1_drawn = 0;  // of them with stage 1 in the shader
// THE SECOND STAGE. On the frame thread: vk_stage1 now (the test key changes
// it). On the D3D7 thread: the walk now.
bool g_stage1_mode = true;
bool g_walk_stage1 = false;
// THE STATES OF THE DRAWS (a census for the log): each new combination of
// the operations and the inputs of texture stages 0 and 1, the blend and a
// texture writes one line. A line with a complement input (0x10) names a
// draw that can look wrong.
struct StateKind {
  uint32_t key[4];
  uint64_t draws;
};
const int kMaxStateKinds = 64;
StateKind g_state_kinds[kMaxStateKinds];
int g_state_kind_count = 0;
bool g_said_seed = false;
bool g_walk_both = false;  // the walk now draws both eyes
// TWO WALKS FOR THE EYES. A frame with no walk of one walk has a walk for each
// eye. The walk of the left eye draws into layer 0 of its target. The walk of
// the right eye that comes just after it takes the same target, keeps layer
// 0, clears layer 1 and draws into it. Then the target has both eyes.
bool g_walk_right = false;  // the walk now is the right eye of a pair
LONG g_left_serial = -1;    // g_walk_serial at the end of a walk of the left eye
VkImage g_left_image = VK_NULL_HANDLE;  // its target
uint64_t g_pair_walks = 0;  // for the report
// THE PICTURE OF THE LAST WALK (backend=vulkan), for a frame with no eye
// pass: its image, the layer that the walk drew into, and its size. A write
// of the CPU into the render target after it (a picture of a Blt) makes
// the pixels of the CPU the newer picture.
uint64_t g_last_image = 0;
uint32_t g_last_layer = 0;
uint32_t g_last_width = 0, g_last_height = 0;
bool g_cpu_newer = false;
// THE PICTURE OF THE WALK BEFORE (vk_keep_picture, backend=vulkan). In
// Direct3D 7 the render target keeps its pixels from one scene to the next,
// and the renderer has two targets in turn. So a walk that draws, or clears
// a part, before a clear of the whole target starts with the picture of the
// walk before it (a copy from the other target).
bool g_keep_picture = true;
bool g_walk_keep = false;          // the walk now can still take the picture
bool g_target_drawn[2] = {false, false};  // the target has a picture
uint64_t g_kept_walks = 0;         // for the report
uint64_t g_cleared_walks = 0;      // walks that cleared the whole target
// The intro in stereo: the walk now draws over the picture of its eye in the
// target of its eye (target 0 the left eye, target 1 the right eye).
bool g_walk_own = false;
uint64_t g_own_walks = 0;          // for the report
uint64_t g_gpu_walks = 0;   // for the report: the walks with a time of the GPU
uint64_t g_gpu_ticks = 0;
uint64_t g_gpu_odd = 0;  // the times that are not times of a walk
int g_gpu_odd_said = 0;
// FOVEATED RENDERING (foveation, docs/backend.md, "Foveated rendering"). A
// shading rate attachment of R8_UINT with a layer for each eye. On the frame
// thread: the key now, the degrees of the ini, and the gaze of the frame. On
// the D3D7 thread: the map in the image, and the walk now.
int g_fovea_level = 0;
bool g_fovea_show = false;  // foveation_show, on the frame thread
bool g_walk_show = false;   // the walk draws the marks
float g_walk_gaze[2] = {0.0f, 0.0f};
foveation_math::Params g_walk_fovea_params = {};
float g_fovea_inner_ini = 0.0f;  // foveation_inner_deg, 0: the level gives it
float g_fovea_outer_ini = 0.0f;
bool g_gaze_valid = false;
float g_gaze_now[2] = {0.0f, 0.0f};
bool g_said_no_rate = false;
vk::Image g_rate = {};
vk::Buffer g_rate_stage[2] = {};  // for each slot
uint32_t g_rate_map_w = 0, g_rate_map_h = 0;
// The map in the image: the fovea and the texel of its centre.
float g_rate_inner = -1.0f, g_rate_outer = -1.0f;
int g_rate_cx = -1, g_rate_cy = -1;
bool g_walk_fovea = false;  // the renderings of the walk use the map
uint64_t g_fovea_walks = 0;  // for the report
uint64_t g_fovea_maps = 0;
// FOVEATED RESOLUTION (fovea_res, docs/backend.md, "Foveated resolution").
// The draws of the eyes of a walk are kept as records and go at the end of
// the walk into an outer image with fewer pixels and a centre image with
// more (fovea_res_math.h). Then the eye picture is made from the two: the
// outer image over the whole picture and the centre image over its rect,
// with a soft edge. On the frame thread: the key now. On the D3D7 thread:
// the walk now and the images.
bool g_mres_mode = false;
float g_mres_outer = 50.0f, g_mres_center = 100.0f, g_mres_deg = 25.0f;
bool g_walk_mres = false;
fovea_res_math::Layout g_mres = {};
vk::Image g_mres_color[2] = {};  // 0 the outer image, 1 the centre image
vk::Image g_mres_depth[2] = {};
VkImageView g_mres_view[2][2] = {};  // the view of each layer, [image][eye]
bool g_said_mres_size = false;
// The values of the push constants of a draw.
struct DrawPush {
  float viewport[4];
  float depth[4];
  uint32_t ops[4];
  float alpha[4];
  float fog_color[4];
  uint32_t ops1[4];  // stage 1, see vk_scene.frag
};
// A draw or a clear of the walk, in pixels of the game.
struct MresRecord {
  int clear;  // -1 a draw, else the index of the clear
  VkPipeline pipeline;
  uint32_t blend;  // the key of the blend of the pipeline, for MresWalkLine
  VkDescriptorSet set, set1;  // the sets of stage 0 and stage 1
  uint32_t cull, zenable, zwrite, zfunc, type;
  DrawPush push;
  VkDeviceSize vertex_at, uv1_at, index_at;
  uint32_t vertex_count, index_count;
  float viewport[4];  // x, y, width, height of the viewport of the game
  // The rect of its vertices in pixels of the game, x0, y0, x1, y1, with
  // the move of the right eye. The pass of the centre image leaves out a
  // draw whose rect is not in the rect of the centre.
  float bounds[4];
};
struct MresClear {
  uint32_t flags, color;
  float z;
  uint32_t eyes;
  uint32_t count;
  float rects[16][4];  // x0, y0, x1, y1
};
std::vector<MresRecord> g_mres_records;
std::vector<MresClear> g_mres_clears;
uint64_t g_mres_walks = 0;  // for the report
uint64_t g_mres_remade = 0;  // the images made again, since the start
uint64_t g_mres_draws = 0;  // draws of the walks
uint64_t g_mres_center_draws = 0;  // of them in the centre image
uint64_t g_fovea_marks = 0;  // walks with the marks of the test
uint64_t g_gaze_frames = 0;

bool Fail(const char* what) {
  ModLog("vk scene     : %s failed. The Vulkan renderer stops", what);
  g_failed = true;
  return false;
}

VkShaderModule Module(const uint32_t* code, size_t bytes) {
  VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = bytes;
  info.pCode = code;
  VkShaderModule m = VK_NULL_HANDLE;
  f.vkCreateShaderModule(vk::Get().device, &info, nullptr, &m);
  return m;
}

// D3DBLEND_* to Vulkan.
VkBlendFactor Factor(uint32_t blend) {
  switch (blend) {
    case 1: return VK_BLEND_FACTOR_ZERO;
    case 2: return VK_BLEND_FACTOR_ONE;
    case 3: return VK_BLEND_FACTOR_SRC_COLOR;
    case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 5: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 7: return VK_BLEND_FACTOR_DST_ALPHA;
    case 8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 9: return VK_BLEND_FACTOR_DST_COLOR;
    case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default: return VK_BLEND_FACTOR_ONE;
  }
}

VkPipeline Pipeline(uint32_t key) {
  for (int i = 0; i < g_pipeline_count; ++i) {
    if (g_pipelines[i].key == key) return g_pipelines[i].pipeline;
  }
  if (g_pipeline_count >= kMaxPipelines) return VK_NULL_HANDLE;
  VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = g_vert;
  stages[0].pName = "main";
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = g_frag;
  stages[1].pName = "main";
  // Buffer 1 has the texture coordinates of stage 1 (THE SECOND STAGE).
  const VkVertexInputBindingDescription bindings[2] = {
      {0, 32, VK_VERTEX_INPUT_RATE_VERTEX}, {1, 8, VK_VERTEX_INPUT_RATE_VERTEX}};
  const VkVertexInputAttributeDescription attributes[5] = {
      {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
      {1, 0, VK_FORMAT_B8G8R8A8_UNORM, 16},
      {2, 0, VK_FORMAT_B8G8R8A8_UNORM, 20},
      {3, 0, VK_FORMAT_R32G32_SFLOAT, 24},
      {4, 1, VK_FORMAT_R32G32_SFLOAT, 0}};
  VkPipelineVertexInputStateCreateInfo input = {
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  input.vertexBindingDescriptionCount = 2;
  input.pVertexBindingDescriptions = bindings;
  input.vertexAttributeDescriptionCount = 5;
  input.pVertexAttributeDescriptions = attributes;
  VkPipelineInputAssemblyStateCreateInfo assembly = {
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport = {
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster = {
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample = {
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depth = {
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState blend = {};
  blend.colorWriteMask = 0xF;
  const bool panel = (key & kPanelKey) != 0;
  if ((key & ~kPanelKey) != 0) {
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = Factor((key >> 8) & 255);
    blend.dstColorBlendFactor = Factor(key & 255);
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    // The panel target has a separate blend of the alpha (gpu_bridge.cpp,
    // kPanelAlpha): ONE and INVSRCALPHA.
    blend.srcAlphaBlendFactor =
        panel ? VK_BLEND_FACTOR_ONE : blend.srcColorBlendFactor;
    blend.dstAlphaBlendFactor =
        panel ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : blend.dstColorBlendFactor;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo blend_state = {
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend_state.attachmentCount = 1;
  blend_state.pAttachments = &blend;
  const VkDynamicState dynamic[] = {
      VK_DYNAMIC_STATE_VIEWPORT,          VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_CULL_MODE,         VK_DYNAMIC_STATE_FRONT_FACE,
      VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
      VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,  VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = sizeof(dynamic) / sizeof(dynamic[0]);
  dynamic_state.pDynamicStates = dynamic;
  VkPipelineRenderingCreateInfo rendering = {
      VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  // The panel has one layer and no eye.
  rendering.viewMask = panel ? 0u : 3u;
  rendering.colorAttachmentCount = 1;
  const VkFormat color_format = vk::Get().color_format;
  rendering.pColorAttachmentFormats = &color_format;
  rendering.depthAttachmentFormat = kDepthFormat;
  VkGraphicsPipelineCreateInfo info = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.pNext = &rendering;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &input;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depth;
  info.pColorBlendState = &blend_state;
  info.pDynamicState = &dynamic_state;
  info.layout = g_layout;
  // FOVEATED RENDERING: the rate of the attachment replaces the rate of the
  // pipeline. With no attachment in a rendering the rate is 1 by 1.
  VkPipelineFragmentShadingRateStateCreateInfoKHR rate_state = {
      VK_STRUCTURE_TYPE_PIPELINE_FRAGMENT_SHADING_RATE_STATE_CREATE_INFO_KHR};
  rate_state.fragmentSize = {1, 1};
  rate_state.combinerOps[0] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
  rate_state.combinerOps[1] = VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR;
  if (vk::Get().shading_rate && !panel) {
    rendering.pNext = &rate_state;
    info.flags |=
        VK_PIPELINE_CREATE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
  }
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (f.vkCreateGraphicsPipelines(vk::Get().device, VK_NULL_HANDLE, 1, &info,
                                  nullptr, &pipeline) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  g_pipelines[g_pipeline_count].key = key;
  g_pipelines[g_pipeline_count].pipeline = pipeline;
  ++g_pipeline_count;
  return pipeline;
}

// D3DTFG_ / D3DTFN_ / D3DTFP_ to Vulkan: 1 point, 2 linear, 3 (min) or 5
// (mag) anisotropic. D3DTFP_NONE (1) uses level 0 only. An anisotropic
// filter needs the feature of the device (vk::Core, max_anisotropy).
VkSampler Sampler(uint32_t mag, uint32_t min, uint32_t mip, uint32_t u,
                  uint32_t v, uint32_t anisotropy) {
  const uint32_t key = (mag & 7) | (min & 7) << 3 | (mip & 7) << 6 |
                       (u & 7) << 9 | (v & 7) << 12 | (anisotropy & 31) << 15;
  for (int i = 0; i < g_sampler_count; ++i) {
    if (g_samplers[i].key == key) return g_samplers[i].sampler;
  }
  if (g_sampler_count >= kMaxSamplers) return g_samplers[0].sampler;
  auto filter = [](uint32_t value) {
    return value == 1 ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
  };
  auto address = [](uint32_t value) {
    // D3DTADDRESS_: 1 wrap, 2 mirror, 3 clamp, 4 border.
    return value == 2   ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
           : value == 3 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
           : value == 4 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                        : VK_SAMPLER_ADDRESS_MODE_REPEAT;
  };
  VkSamplerCreateInfo info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  info.magFilter = filter(mag);
  info.minFilter = filter(min);
  info.mipmapMode = mip == 3 ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                             : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  info.addressModeU = address(u);
  info.addressModeV = address(v);
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  const float most = vk::Get().max_anisotropy;
  info.anisotropyEnable =
      (min == 3 || mag == 5) && anisotropy > 1 && most > 1.0f ? VK_TRUE
                                                              : VK_FALSE;
  info.maxAnisotropy = static_cast<float>(anisotropy);
  if (info.maxAnisotropy > most) info.maxAnisotropy = most;
  info.maxLod = mip <= 1 ? 0.25f : VK_LOD_CLAMP_NONE;
  VkSampler sampler = VK_NULL_HANDLE;
  f.vkCreateSampler(vk::Get().device, &info, nullptr, &sampler);
  g_samplers[g_sampler_count].key = key;
  g_samplers[g_sampler_count].sampler = sampler;
  ++g_sampler_count;
  return sampler;
}

// THE TABLES OF THE FORMATS OF 16 BITS (vk_texture_math.h). A table is made
// at the first texture of its format, on the D3D7 thread.
struct Lut16 {
  vk_texture_math::Format format;
  uint32_t* rgba;  // 65536 values
};
const int kLuts = 8;
Lut16 g_luts[kLuts] = {};
int g_lut_count = 0;

const uint32_t* LutOf(const vk_texture_math::Format& format) {
  for (int i = 0; i < g_lut_count; ++i) {
    const vk_texture_math::Format& g = g_luts[i].format;
    if (g.r == format.r && g.g == format.g && g.b == format.b &&
        g.a == format.a && g.has_alpha == format.has_alpha) {
      return g_luts[i].rgba;
    }
  }
  if (g_lut_count >= kLuts) return nullptr;
  uint32_t* rgba = static_cast<uint32_t*>(malloc(65536 * sizeof(uint32_t)));
  if (rgba == nullptr) return nullptr;
  vk_texture_math::BuildLut16(format, rgba);
  g_luts[g_lut_count].format = format;
  g_luts[g_lut_count].rgba = rgba;
  ++g_lut_count;
  return rgba;
}

// THE VERTEX FORMATS. The shaders read the layout of 0x1C4: XYZRHW, the
// diffuse and the specular color, and one set of texture coordinates, 32
// bytes. A draw in another format with XYZRHW (for example two sets of
// texture coordinates) is copied into that layout: a missing diffuse color
// is white, a missing specular color is black, and the set of texture
// coordinates is the one of D3DTSS_TEXCOORDINDEX of stage 0. The set of
// stage 1 goes into a buffer of its own (THE SECOND STAGE).
struct VertexLayout {
  uint32_t stride;
  int diffuse;   // the offset, or -1
  int specular;
  int uv;        // the offset of the set that stage 0 reads, or -1
  int uv1;       // the offset of the set that stage 1 reads, or -1
};

// False for a format that has no XYZRHW.
bool LayoutOf(uint32_t fvf, uint32_t uv_set, uint32_t uv1_set,
              VertexLayout* out) {
  if ((fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW) return false;
  uint32_t at = 16;
  out->diffuse = -1;
  out->specular = -1;
  out->uv = -1;
  out->uv1 = -1;
  if (fvf & D3DFVF_DIFFUSE) {
    out->diffuse = static_cast<int>(at);
    at += 4;
  }
  if (fvf & D3DFVF_SPECULAR) {
    out->specular = static_cast<int>(at);
    at += 4;
  }
  const uint32_t sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  for (uint32_t i = 0; i < sets && i < 8; ++i) {
    // D3DFVF_TEXCOORDSIZEn: 0 two floats, 1 three, 2 four, 3 one.
    const uint32_t size = (fvf >> (16 + i * 2)) & 3;
    const uint32_t floats = size == 0 ? 2 : size == 1 ? 3 : size == 2 ? 4 : 1;
    if (i == uv_set && floats >= 2) out->uv = static_cast<int>(at);
    if (i == uv1_set && floats >= 2) out->uv1 = static_cast<int>(at);
    at += floats * 4;
  }
  const int first = static_cast<int>(16 + (out->diffuse >= 0 ? 4 : 0) +
                                     (out->specular >= 0 ? 4 : 0));
  if (out->uv < 0 && sets > 0 && ((fvf >> 16) & 3) == 0) out->uv = first;
  if (out->uv1 < 0 && sets > 0 && ((fvf >> 16) & 3) == 0) out->uv1 = first;
  out->stride = at;
  return true;
}

// Copies the vertices in the layout of 0x1C4.
void ConvertVertices(const uint8_t* from, const VertexLayout& layout,
                     uint32_t count, uint8_t* to) {
  for (uint32_t i = 0; i < count; ++i, from += layout.stride, to += 32) {
    memcpy(to, from, 16);
    const uint32_t white = 0xFFFFFFFFu, black = 0u;
    memcpy(to + 16, layout.diffuse >= 0 ? from + layout.diffuse : reinterpret_cast<const uint8_t*>(&white), 4);
    memcpy(to + 20, layout.specular >= 0 ? from + layout.specular : reinterpret_cast<const uint8_t*>(&black), 4);
    if (layout.uv >= 0) {
      memcpy(to + 24, from + layout.uv, 8);
    } else {
      memset(to + 24, 0, 8);
    }
  }
}

// Copies the texture coordinates of stage 1, 8 bytes for each vertex.
void CopyUv1(const uint8_t* from, const VertexLayout& layout, uint32_t count,
             uint8_t* to) {
  for (uint32_t i = 0; i < count; ++i, from += layout.stride, to += 8) {
    if (layout.uv1 >= 0) {
      memcpy(to, from + layout.uv1, 8);
    } else {
      memset(to, 0, 8);
    }
  }
}

// THE READ OF A LOCKED SURFACE. A Lock of dxwrapper can give memory of the
// GPU that the CPU reads with no cache (write combined), and there each
// plain read waits for the bus. With SSE4.1 a row goes into a buffer of the
// CPU with streaming loads (MOVNTDQA), 64 bytes at a time, and the
// conversion reads that buffer. Without SSE4.1 the conversion reads the
// surface. On the D3D7 thread.
bool g_sse41 = false;
bool g_sse41_known = false;
uint8_t* g_row = nullptr;
uint32_t g_row_size = 0;

const uint8_t* ReadRow(const uint8_t* src, uint32_t bytes) {
  if (!g_sse41_known) {
    int regs[4];
    __cpuid(regs, 1);
    g_sse41 = (regs[2] & (1 << 19)) != 0;
    g_sse41_known = true;
  }
  if (!g_sse41) return src;
  if (g_row_size < bytes) {
    _aligned_free(g_row);
    g_row = static_cast<uint8_t*>(_aligned_malloc(bytes + 64, 64));
    g_row_size = g_row != nullptr ? bytes : 0;
    if (g_row == nullptr) return src;
  }
  // The bytes before the first address of 16, then whole blocks.
  const uint32_t head = static_cast<uint32_t>(
      (16 - (reinterpret_cast<uintptr_t>(src) & 15)) & 15);
  uint32_t at = head < bytes ? head : bytes;
  memcpy(g_row, src, at);
  for (; at + 64 <= bytes; at += 64) {
    __m128i* from = reinterpret_cast<__m128i*>(const_cast<uint8_t*>(src + at));
    const __m128i a = _mm_stream_load_si128(from);
    const __m128i b = _mm_stream_load_si128(from + 1);
    const __m128i c = _mm_stream_load_si128(from + 2);
    const __m128i d = _mm_stream_load_si128(from + 3);
    __m128i* to = reinterpret_cast<__m128i*>(g_row + at);
    _mm_storeu_si128(to, a);
    _mm_storeu_si128(to + 1, b);
    _mm_storeu_si128(to + 2, c);
    _mm_storeu_si128(to + 3, d);
  }
  for (; at + 16 <= bytes; at += 16) {
    _mm_storeu_si128(
        reinterpret_cast<__m128i*>(g_row + at),
        _mm_stream_load_si128(
            reinterpret_cast<__m128i*>(const_cast<uint8_t*>(src + at))));
  }
  memcpy(g_row + at, src + at, bytes - at);
  return g_row;
}

// Reads a surface of the game into RGBA8. False when it cannot.
bool ReadSurface(IDirectDrawSurface7* s, uint32_t* width, uint32_t* height,
                 uint8_t** pixels) {
  DDSURFACEDESC2 desc = {};
  desc.dwSize = sizeof(desc);
  if (FAILED(s->Lock(nullptr, &desc, DDLOCK_READONLY | DDLOCK_WAIT, nullptr))) {
    return false;
  }
  const DDPIXELFORMAT& pf = desc.ddpfPixelFormat;
  const uint32_t bpp = pf.dwRGBBitCount;
  bool ok = (pf.dwFlags & DDPF_RGB) != 0 && (bpp == 16 || bpp == 32 ||
                                             bpp == 24) &&
            desc.dwWidth > 0 && desc.dwHeight > 0;
  if (ok) {
    *width = desc.dwWidth;
    *height = desc.dwHeight;
    *pixels = static_cast<uint8_t*>(malloc(desc.dwWidth * desc.dwHeight * 4));
    ok = *pixels != nullptr;
  }
  if (ok) {
    // The masks of the format, and its table (16 bits) or its channels (24
    // and 32 bits). Each gives the values of vk_texture_math::ToRgba.
    vk_texture_math::Format format;
    format.r = pf.dwRBitMask;
    format.g = pf.dwGBitMask;
    format.b = pf.dwBBitMask;
    format.has_alpha = (pf.dwFlags & DDPF_ALPHAPIXELS) != 0;
    format.a = format.has_alpha ? pf.dwRGBAlphaBitMask : 0u;
    const uint32_t* lut = bpp == 16 ? LutOf(format) : nullptr;
    vk_texture_math::Channel r, g, b, a;
    if (lut == nullptr) {
      vk_texture_math::BuildChannel(format.r, 0, &r);
      vk_texture_math::BuildChannel(format.g, 0, &g);
      vk_texture_math::BuildChannel(format.b, 0, &b);
      vk_texture_math::BuildChannel(format.a, 255, &a);
    }
    const uint32_t row_bytes = desc.dwWidth * (bpp / 8);
    for (uint32_t y = 0; y < desc.dwHeight; ++y) {
      const uint8_t* row = ReadRow(
          static_cast<const uint8_t*>(desc.lpSurface) + y * desc.lPitch,
          row_bytes);
      uint32_t* out =
          reinterpret_cast<uint32_t*>(*pixels + y * desc.dwWidth * 4);
      if (lut != nullptr) {
        const uint16_t* in = reinterpret_cast<const uint16_t*>(row);
        for (uint32_t x = 0; x < desc.dwWidth; ++x) out[x] = lut[in[x]];
        continue;
      }
      for (uint32_t x = 0; x < desc.dwWidth; ++x) {
        uint32_t p = 0;
        if (bpp == 16) {
          p = reinterpret_cast<const uint16_t*>(row)[x];
        } else if (bpp == 32) {
          p = reinterpret_cast<const uint32_t*>(row)[x];
        } else {
          p = row[x * 3] | row[x * 3 + 1] << 8 | row[x * 3 + 2] << 16;
        }
        out[x] = vk_texture_math::ChannelValue(p, r, format.r) |
                 vk_texture_math::ChannelValue(p, g, format.g) << 8 |
                 vk_texture_math::ChannelValue(p, b, format.b) << 16 |
                 vk_texture_math::ChannelValue(p, a, format.a) << 24;
      }
    }
  }
  s->Unlock(nullptr);
  return ok;
}

// Records the copy of `staging` (width by height pixels of RGBA8) into
// `image`, each mip level from the one before it, and the layout for the
// fragment shader at the end.
void RecordUpload(VkCommandBuffer cb, const vk::Image* image,
                  VkBuffer staging, VkDeviceSize offset, uint32_t width,
                  uint32_t height, uint32_t levels) {
  vk::Barrier(cb, image->image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              levels);
  VkBufferImageCopy copy = {};
  copy.bufferOffset = offset;
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {width, height, 1};
  f.vkCmdCopyBufferToImage(cb, staging, image->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
  // Each level from the one before it, with a linear blit.
  for (uint32_t level = 1; level < levels; ++level) {
    VkImageMemoryBarrier2 b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image->image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, 1};
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    f.vkCmdPipelineBarrier2(cb, &dep);
    VkImageBlit blit = {};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(width >> (level - 1) ? width >> (level - 1) : 1),
                          static_cast<int32_t>(height >> (level - 1) ? height >> (level - 1) : 1), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(width >> level ? width >> level : 1),
                          static_cast<int32_t>(height >> level ? height >> level : 1), 1};
    f.vkCmdBlitImage(cb, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                     &blit, VK_FILTER_LINEAR);
  }
  // Each level but the last is in the layout of a source now.
  if (levels > 1) {
    VkImageMemoryBarrier2 b[2] = {{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2},
                                  {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2}};
    for (int i = 0; i < 2; ++i) {
      b[i].srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
      b[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      b[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
      b[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
      b[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b[i].srcQueueFamilyIndex = b[i].dstQueueFamilyIndex =
          VK_QUEUE_FAMILY_IGNORED;
      b[i].image = image->image;
    }
    b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels - 1, 0, 1};
    b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, levels - 1, 1, 0, 1};
    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 2;
    dep.pImageMemoryBarriers = b;
    f.vkCmdPipelineBarrier2(cb, &dep);
  } else {
    vk::Barrier(cb, image->image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
  }
}

// Makes the image of RGBA8 pixels with each mip level and its staging
// buffer, and frees the pixels. False with nothing kept when it cannot.
bool PrepareUpload(uint32_t width, uint32_t height, uint8_t* pixels,
                   vk::Image* image, vk::Buffer* staging, uint32_t* levels) {
  *levels = 1;
  while ((width >> *levels) > 0 || (height >> *levels) > 0) ++*levels;
  if (!vk::MakeImage(width, height, *levels, kTextureFormat,
                     VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, image)) {
    free(pixels);
    return false;
  }
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
  if (!vk::MakeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true,
                      staging)) {
    free(pixels);
    vk::FreeImage(image);
    return false;
  }
  memcpy(staging->mapped, pixels, static_cast<size_t>(bytes));
  free(pixels);
  ++g_uploads;
  g_upload_bytes += bytes;
  return true;
}

// Copies RGBA8 pixels into a new image with each mip level, and frees the
// pixels. With a wait for the GPU.
bool Upload(uint32_t width, uint32_t height, uint8_t* pixels,
            vk::Image* image) {
  LARGE_INTEGER start, done;
  QueryPerformanceCounter(&start);
  vk::Buffer staging;
  uint32_t levels = 1;
  if (!PrepareUpload(width, height, pixels, image, &staging, &levels)) {
    return false;
  }
  VkCommandBuffer cb = vk::BeginOnce();
  RecordUpload(cb, image, staging.buffer, 0, width, height, levels);
  const bool ok = vk::EndOnce(cb);
  vk::FreeBuffer(&staging);
  QueryPerformanceCounter(&done);
  g_upload_wait_ticks += static_cast<uint64_t>(done.QuadPart - start.QuadPart);
  return ok;
}

// THE UPLOADS OF A WALK: as Upload, into the upload buffer of the slot of
// the walk, with no wait. With no room for one more staging buffer it is
// an Upload.
// The pixels in the ring of the staging of the slot of the walk. False when
// the ring has no room. Frees nothing.
bool StageInRing(const uint8_t* pixels, VkDeviceSize bytes,
                 VkDeviceSize* offset) {
  Slot& s = g_slots[g_slot];
  const VkDeviceSize room = (bytes + 15) & ~static_cast<VkDeviceSize>(15);
  if (s.stage_ring.buffer == VK_NULL_HANDLE &&
      !vk::MakeBuffer(kStageRingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true,
                      &s.stage_ring)) {
    return false;
  }
  if (s.stage_used + room > kStageRingBytes) return false;
  memcpy(static_cast<uint8_t*>(s.stage_ring.mapped) + s.stage_used, pixels,
         static_cast<size_t>(bytes));
  *offset = s.stage_used;
  s.stage_used += room;
  ++g_ring_stages;
  ++g_uploads;
  g_upload_bytes += bytes;
  return true;
}

void OpenUploads() {
  Slot& s = g_slots[g_slot];
  if (s.upload_open) return;
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  f.vkBeginCommandBuffer(s.upload_cb, &begin);
  s.upload_open = true;
}

uint32_t LevelsOf(uint32_t width, uint32_t height) {
  uint32_t levels = 1;
  while ((width >> levels) > 0 || (height >> levels) > 0) ++levels;
  return levels;
}

// vk_texture_reuse=1: the pixels go into the image that the texture has.
// The upload buffer of the slot runs before the buffer of the walk, so no
// draw of this walk may have drawn with the image before (the caller looks).
// The draws of the walks before it come first in the queue. False with the
// pixels kept when the ring has no room.
bool UploadInPlace(uint32_t width, uint32_t height, const uint8_t* pixels,
                   vk::Image* image) {
  VkDeviceSize offset = 0;
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
  if (!StageInRing(pixels, bytes, &offset)) return false;
  OpenUploads();
  RecordUpload(g_slots[g_slot].upload_cb, image,
               g_slots[g_slot].stage_ring.buffer, offset, width, height,
               LevelsOf(width, height));
  ++g_uploads_queued;
  ++g_reused;
  return true;
}

bool UploadQueued(uint32_t width, uint32_t height, uint8_t* pixels,
                  vk::Image* image) {
  Slot& s = g_slots[g_slot];
  // vk_texture_reuse=1: the pixels go into the ring, and only the image is
  // new.
  if (g_texture_reuse) {
    const uint32_t levels = LevelsOf(width, height);
    if (!vk::MakeImage(width, height, levels, kTextureFormat,
                       VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, image)) {
      free(pixels);
      return false;
    }
    VkDeviceSize offset = 0;
    if (StageInRing(pixels, static_cast<VkDeviceSize>(width) * height * 4,
                    &offset)) {
      free(pixels);
      OpenUploads();
      RecordUpload(s.upload_cb, image, s.stage_ring.buffer, offset, width,
                   height, levels);
      ++g_uploads_queued;
      return true;
    }
    vk::FreeImage(image);
  }
  if (s.staging_count >= kSlotStaging) {
    return Upload(width, height, pixels, image);
  }
  vk::Buffer staging;
  uint32_t levels = 1;
  if (!PrepareUpload(width, height, pixels, image, &staging, &levels)) {
    return false;
  }
  OpenUploads();
  RecordUpload(s.upload_cb, image, staging.buffer, 0, width, height, levels);
  s.staging[s.staging_count++] = staging;
  ++g_uploads_queued;
  return true;
}

Texture* FindTexture(void* surface, bool make) {
  for (int i = 0; i < g_texture_count; ++i) {
    if (g_textures[i].surface == surface) return &g_textures[i];
  }
  if (!make || g_texture_count >= kMaxTextures) return nullptr;
  Texture* t = &g_textures[g_texture_count++];
  memset(t, 0, sizeof(*t));
  t->surface = surface;
  t->dirty = true;
  return t;
}

void Retire(vk::Image* image) {
  Slot& s = g_slots[g_slot];
  if (s.garbage_count < 64) {
    s.garbage[s.garbage_count++] = *image;
  } else {
    vk::FreeImage(image);
  }
  memset(image, 0, sizeof(*image));
}

// The texture of a surface, read now when it is new or marked.
Texture* TextureOf(void* surface) {
  if (surface == nullptr) return g_white;
  AcquireSRWLockExclusive(&g_texture_lock);
  Texture* t = FindTexture(surface, true);
  const bool dirty = t != nullptr && t->dirty;
  if (t != nullptr) t->dirty = false;
  ReleaseSRWLockExclusive(&g_texture_lock);
  if (t == nullptr) return g_white;
  if (!dirty && t->image.image != VK_NULL_HANDLE) return t;
  uint32_t width = 0, height = 0;
  uint8_t* pixels = nullptr;
  if (!ReadSurface(static_cast<IDirectDrawSurface7*>(surface), &width, &height,
                   &pixels)) {
    return t->image.image != VK_NULL_HANDLE ? t : g_white;
  }
  // The image it has, when it has the size and no draw of this walk drew
  // with it.
  if (g_texture_reuse && g_recording && g_walk_upload_queue &&
      t->image.image != VK_NULL_HANDLE && t->image.width == width &&
      t->image.height == height && t->used_walk != g_walk_number &&
      UploadInPlace(width, height, pixels, &t->image)) {
    free(pixels);
    return t;
  }
  if (t->image.image != VK_NULL_HANDLE) Retire(&t->image);
  // The sets of the old image go back to the pool after the GPU is done.
  Slot& s = g_slots[g_slot];
  for (int i = 0; i < t->set_count; ++i) {
    if (s.dead_count < 1024) s.dead_sets[s.dead_count++] = t->sets[i].set;
  }
  t->set_count = 0;
  const bool ok = g_recording && g_walk_upload_queue
                      ? UploadQueued(width, height, pixels, &t->image)
                      : Upload(width, height, pixels, &t->image);
  if (!ok) return g_white;
  return t;
}

VkDescriptorSet SetOf(Texture* t, VkSampler sampler) {
  for (int i = 0; i < t->set_count; ++i) {
    if (t->sets[i].sampler == sampler) return t->sets[i].set;
  }
  VkDescriptorSetAllocateInfo info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  info.descriptorPool = g_pool;
  info.descriptorSetCount = 1;
  info.pSetLayouts = &g_set_layout;
  VkDescriptorSet set = VK_NULL_HANDLE;
  // The renderer never asks the pool for more sets than it has room for:
  // the driver must not get an allocation past the room of the pool.
  const bool room = g_sets_live < static_cast<int64_t>(kMaxTextures) * 8 - 16;
  if (!room ||
      f.vkAllocateDescriptorSets(vk::Get().device, &info, &set) != VK_SUCCESS) {
    ++g_set_failures;
    if (!g_said_set_failure) {
      g_said_set_failure = true;
      ModLog("vk scene     : the pool has no room for one more descriptor set "
             "(%lld in use, %llu made), so a draw is not drawn",
             g_sets_live, g_sets_made);
    }
    return VK_NULL_HANDLE;
  }
  ++g_sets_live;
  ++g_sets_made;
  VkDescriptorImageInfo image = {sampler, t->image.view,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = set;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &image;
  f.vkUpdateDescriptorSets(vk::Get().device, 1, &write, 0, nullptr);
  if (t->set_count < 8) {
    t->sets[t->set_count].sampler = sampler;
    t->sets[t->set_count].set = set;
    ++t->set_count;
  } else {
    // No room in the texture: the set goes back to the pool after the GPU
    // is done with this walk.
    Slot& s = g_slots[g_slot];
    if (s.dead_count < 1024) s.dead_sets[s.dead_count++] = set;
  }
  return set;
}

bool MakeTarget(uint32_t width, uint32_t height) {
  if (g_colors[0].image != VK_NULL_HANDLE && g_colors[0].width == width &&
      g_colors[0].height == height) {
    return true;
  }
  vk::LockQueue();
  f.vkDeviceWaitIdle(vk::Get().device);
  vk::UnlockQueue();
  vk::FreeImage(&g_colors[0]);
  vk::FreeImage(&g_colors[1]);
  vk::FreeImage(&g_depth);
  vk::FreeImage(&g_panels[0]);
  vk::FreeImage(&g_panels[1]);
  vk::FreeImage(&g_panel_depth);
  InterlockedExchange(&g_done_serial, 0);
  g_taken_serial = 0;
  g_done_image = 0;
  g_last_image = 0;
  g_target_drawn[0] = g_target_drawn[1] = false;
  InterlockedExchange(&g_panel_walk, 0);
  for (int i = 0; i < 2; ++i) {
    if (!vk::MakeImage(width, height, 1, vk::Get().color_format,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, &g_colors[i], 2) ||
        !vk::MakeImage(width, height, 1, vk::Get().color_format,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, &g_panels[i])) {
      return false;
    }
  }
  return vk::MakeImage(width, height, 1, kDepthFormat,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT, &g_depth, 2) &&
         vk::MakeImage(width, height, 1, kDepthFormat,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT, &g_panel_depth);
}

bool Ready() {
  if (g_ready) return true;
  if (g_failed) return false;
  if (!vk::Start()) {
    if (vk::Pending()) {
      // The walks before the start of the OpenXR session draw nothing here.
      if (!g_said_pending) {
        g_said_pending = true;
        ModLog("vk scene     : the renderer waits for the Vulkan of the "
               "OpenXR session (xr_vulkan=1)");
      }
      return false;
    }
    return Fail("the start of Vulkan");
  }
  const VkDevice device = vk::Get().device;
  g_vert = Module(kVkSceneVert, sizeof(kVkSceneVert));
  g_frag = Module(kVkSceneFrag, sizeof(kVkSceneFrag));
  if (g_vert == VK_NULL_HANDLE || g_frag == VK_NULL_HANDLE) {
    return Fail("the shaders");
  }
  VkDescriptorSetLayoutBinding binding = {
      0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
      VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo set_info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  set_info.bindingCount = 1;
  set_info.pBindings = &binding;
  if (f.vkCreateDescriptorSetLayout(device, &set_info, nullptr,
                                    &g_set_layout) != VK_SUCCESS) {
    return Fail("the descriptor set layout");
  }
  VkPushConstantRange push = {
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
      sizeof(DrawPush)};
  // Set 0 has the texture of stage 0, and set 1 the texture of stage 1.
  const VkDescriptorSetLayout set_layouts[2] = {g_set_layout, g_set_layout};
  VkPipelineLayoutCreateInfo layout_info = {
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 2;
  layout_info.pSetLayouts = set_layouts;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push;
  if (f.vkCreatePipelineLayout(device, &layout_info, nullptr, &g_layout) !=
      VK_SUCCESS) {
    return Fail("the pipeline layout");
  }
  VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               kMaxTextures * 8};
  VkDescriptorPoolCreateInfo pool_info = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  // A texture that the game writes again gets new sets, and its old sets go
  // back to the pool (Slot::dead_sets).
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = kMaxTextures * 8;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &size;
  if (f.vkCreateDescriptorPool(device, &pool_info, nullptr, &g_pool) !=
      VK_SUCCESS) {
    return Fail("the descriptor pool");
  }
  g_textures = static_cast<Texture*>(
      VirtualAlloc(nullptr, sizeof(Texture) * (kMaxTextures + 1),
                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (g_textures == nullptr) return Fail("the table of the textures");
  // The white texel of a draw with no texture.
  g_white = &g_textures[kMaxTextures];
  memset(g_white, 0, sizeof(*g_white));
  uint8_t* white = static_cast<uint8_t*>(malloc(4));
  white[0] = white[1] = white[2] = white[3] = 255;
  if (!Upload(1, 1, white, &g_white->image)) return Fail("the white texture");
  for (int i = 0; i < 2; ++i) {
    Slot& s = g_slots[i];
    memset(&s, 0, sizeof(s));
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = vk::Get().queue_family;
    if (f.vkCreateCommandPool(device, &pool, nullptr, &s.pool) != VK_SUCCESS) {
      return Fail("a command pool");
    }
    VkCommandBufferAllocateInfo cb_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cb_info.commandPool = s.pool;
    cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_info.commandBufferCount = 1;
    if (f.vkAllocateCommandBuffers(device, &cb_info, &s.cb) != VK_SUCCESS ||
        f.vkAllocateCommandBuffers(device, &cb_info, &s.upload_cb) !=
            VK_SUCCESS) {
      return Fail("a command buffer");
    }
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (f.vkCreateFence(device, &fence, nullptr, &s.fence) != VK_SUCCESS) {
      return Fail("a fence");
    }
    if (vk::Get().timestamp_ns > 0.0f) {
      VkQueryPoolCreateInfo query = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
      query.queryType = VK_QUERY_TYPE_TIMESTAMP;
      query.queryCount = 2;
      if (f.vkCreateQueryPool(device, &query, nullptr, &s.query) !=
          VK_SUCCESS) {
        s.query = VK_NULL_HANDLE;
      }
    }
    if (!vk::MakeBuffer(kRingBytes,
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                        true, &s.ring)) {
      return Fail("a vertex buffer");
    }
  }
  g_ready = true;
  ModLog("vk scene     : the Vulkan renderer is ready (the shadow test)");
  return true;
}

// Writes one layer of an image (an eye or the panel) to a file of 24 bits,
// bottom up. `what` is the word in the name of the file.
void WritePicture(const vk::Image& image, uint32_t layer, const char* what,
                  uint64_t frame,
                  VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
  const uint32_t w = image.width, h = image.height;
  vk::Buffer read;
  if (!vk::MakeBuffer(static_cast<VkDeviceSize>(w) * h * 4,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &read)) {
    return;
  }
  VkCommandBuffer cb = vk::BeginOnce();
  vk::Barrier(cb, image.image, VK_IMAGE_ASPECT_COLOR_BIT, layout,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, image.layers);
  VkBufferImageCopy copy = {};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1};
  copy.imageExtent = {w, h, 1};
  f.vkCmdCopyImageToBuffer(cb, image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read.buffer,
                           1, &copy);
  vk::Barrier(cb, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout, 1, image.layers);
  if (!vk::EndOnce(cb)) {
    vk::FreeBuffer(&read);
    return;
  }
  char path[MAX_PATH];
  GetModuleFileNameA(GetModuleHandleA(nullptr), path, MAX_PATH);
  char* slash = strrchr(path, '\\');
  if (slash != nullptr) slash[1] = 0;
  char name[64];
  sprintf_s(name, "bwvr-vk-%s-%05llu.bmp", what,
            frame);
  strcat_s(path, name);
  FILE* out = nullptr;
  if (fopen_s(&out, path, "wb") == 0 && out != nullptr) {
    const uint32_t row = (w * 3 + 3) & ~3u;
    const uint32_t data = row * h;
    uint8_t header[54] = {'B', 'M'};
    const uint32_t file = 54 + data;
    memcpy(header + 2, &file, 4);
    const uint32_t offset = 54, info = 40, planes_bits = 1 | (24 << 16);
    memcpy(header + 10, &offset, 4);
    memcpy(header + 14, &info, 4);
    memcpy(header + 18, &w, 4);
    memcpy(header + 22, &h, 4);
    memcpy(header + 26, &planes_bits, 4);
    memcpy(header + 34, &data, 4);
    fwrite(header, 1, 54, out);
    uint8_t* line = static_cast<uint8_t*>(calloc(row, 1));
    const uint8_t* rgba = static_cast<const uint8_t*>(read.mapped);
    // The target is RGBA or BGRA (THE EYES IN THE HEADSET). The file is BGR.
    const bool bgra = image.format == VK_FORMAT_B8G8R8A8_UNORM;
    const int red = bgra ? 2 : 0, blue = bgra ? 0 : 2;
    for (uint32_t y = 0; y < h; ++y) {
      const uint8_t* src = rgba + (h - 1 - y) * w * 4;
      for (uint32_t x = 0; x < w; ++x) {
        line[x * 3 + 0] = src[x * 4 + blue];
        line[x * 3 + 1] = src[x * 4 + 1];
        line[x * 3 + 2] = src[x * 4 + red];
      }
      fwrite(line, 1, row, out);
    }
    free(line);
    fclose(out);
    ModLog("vk scene     : the image %s of frame %llu is in %s", what,
           frame, path);
  }
  vk::FreeBuffer(&read);
}

// Starts a rendering into color_image and depth_image, for the layers of
// view_mask (bit 0 the left eye, bit 1 the right eye, 0 for an image of one
// layer with no eye). load keeps the pixels and the depth that the images
// have, else both are cleared: the color to 0 and the depth to 1. keep_depth
// stores the depth at the end of the rendering, for a rendering after it in
// the same walk.
void BeginRenderingOf(VkCommandBuffer cb, const vk::Image& color_image,
                      const vk::Image& depth_image, uint32_t view_mask,
                      bool load, bool keep_depth) {
  VkRenderingAttachmentInfo color = {
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = color_image.view;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingAttachmentInfo depth = {
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = depth_image.view;
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = keep_depth ? VK_ATTACHMENT_STORE_OP_STORE
                             : VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.clearValue.depthStencil.depth = 1.0f;
  VkRenderingInfo rendering = {VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, {color_image.width, color_image.height}};
  rendering.layerCount = 1;
  rendering.viewMask = view_mask;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  rendering.pDepthAttachment = &depth;
  // FOVEATED RENDERING: the map of the rates, a layer for each eye.
  VkRenderingFragmentShadingRateAttachmentInfoKHR rate = {
      VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR};
  if (g_walk_fovea && view_mask != 0 && g_rate.view != VK_NULL_HANDLE) {
    rate.imageView = g_rate.view;
    rate.imageLayout =
        VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
    rate.shadingRateAttachmentTexelSize = {vk::Get().rate_texel_w,
                                           vk::Get().rate_texel_h};
    rendering.pNext = &rate;
  }
  f.vkCmdBeginRendering(cb, &rendering);
}

// Starts the rendering into the target of the walk (BeginRenderingOf).
void BeginRendering(VkCommandBuffer cb, uint32_t view_mask, bool load,
                    bool keep_depth) {
  BeginRenderingOf(cb, Target(), g_depth, view_mask, load, keep_depth);
}

// The part of a rect of pixels that is in the target. False when nothing
// is.
bool InTarget(const vk_scene_math::Rect& r, VkRect2D* out) {
  vk_scene_math::Rect in;
  if (!vk_scene_math::InTarget(r, Target().width, Target().height, &in)) {
    return false;
  }
  out->offset = {static_cast<int32_t>(in.x0), static_cast<int32_t>(in.y0)};
  out->extent = {static_cast<uint32_t>(in.x1 - in.x0),
                 static_cast<uint32_t>(in.y1 - in.y0)};
  return true;
}

// The viewport of Direct3D 7 as the viewport and the scissor rect of Vulkan.
// The vertex shader gives the clip position in that viewport, so a vertex
// comes to its pixel of the target, and the scissor rect cuts each pixel
// outside the viewport, as in Direct3D 9.
void SetViewportState(VkCommandBuffer cb) {
  const D3DVIEWPORT7& v = g_viewport;
  const VkViewport viewport = {
      static_cast<float>(v.dwX), static_cast<float>(v.dwY),
      static_cast<float>(v.dwWidth), static_cast<float>(v.dwHeight), 0.0f,
      1.0f};
  f.vkCmdSetViewport(cb, 0, 1, &viewport);
  VkRect2D scissor = {{0, 0}, {0, 0}};
  InTarget(vk_scene_math::ViewportRect(v.dwX, v.dwY, v.dwWidth, v.dwHeight),
           &scissor);
  f.vkCmdSetScissor(cb, 0, 1, &scissor);
}

// True when a draw or a clear goes to another target than the image of the
// eyes (THE TARGET, THE VIEWPORT AND THE CLEARS).
bool OffTarget() {
  return vk_scene_math::OffTarget(g_walk_follow, g_eye_target, g_draw_target);
}

// True when a draw goes into the panel (THE PANEL).
bool ForPanel() { return g_walk_follow && g_walk_panel && g_draw_panel; }

// Ends the rendering now and starts the one of the panel (panel) or of the
// eyes. The dynamic states stay.
void UsePanel(bool panel) {
  if (panel == g_rendering_panel) return;
  const VkCommandBuffer cb = g_slots[g_slot].cb;
  // With foveated resolution only the rendering of the panel is open in a
  // walk: the draws of the eyes are records.
  if (!g_walk_mres || g_rendering_panel) f.vkCmdEndRendering(cb);
  if (panel) {
    vk::Image& image = g_panels[g_target];
    if (!g_panel_drawn) {
      vk::Barrier(cb, image.image, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1);
      vk::Barrier(cb, g_panel_depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 1);
    }
    BeginRenderingOf(cb, image, g_panel_depth, 0u, g_panel_drawn, true);
    g_panel_drawn = true;
  } else if (!g_walk_mres) {
    BeginRendering(cb, 3u, true, true);
  }
  g_rendering_panel = panel;
}

// THE PICTURE OF THE WALK BEFORE: before the first draw or part clear of a
// walk with no clear of the whole target, a copy of both layers of the other
// target into this one. The rendering of the eyes is open, and it is open
// again after the copy, with the dynamic states of the walk.
void KeepPicture() {
  if (!g_walk_keep) return;
  g_walk_keep = false;
  const int before = g_target ^ 1;
  if (!g_target_drawn[before]) return;
  const vk::Image& from = g_colors[before];
  const vk::Image& to = Target();
  if (from.width != to.width || from.height != to.height) return;
  const VkCommandBuffer cb = g_slots[g_slot].cb;
  f.vkCmdEndRendering(cb);
  vk::Barrier(cb, from.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 2);
  vk::Barrier(cb, to.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 2);
  VkImageBlit region = {};
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
  region.srcOffsets[1] = {static_cast<int32_t>(from.width),
                          static_cast<int32_t>(from.height), 1};
  region.dstSubresource = region.srcSubresource;
  region.dstOffsets[1] = region.srcOffsets[1];
  f.vkCmdBlitImage(cb, from.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   to.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                   VK_FILTER_NEAREST);
  vk::Barrier(cb, from.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 2);
  vk::Barrier(cb, to.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 2);
  BeginRendering(cb, 3u, true, true);
  ++g_kept_walks;
  // The first ones go into the log: the walks of a frame with no eye pass
  // have no report of 450 frames.
  static int s_said = 0;
  if (s_said < 24) {
    ++s_said;
    ModLog("vk scene     : walk %llu has no clear of the whole target before "
           "its first draw, so it starts with the picture of the walk before",
           g_walk_number);
  }
}

void TargetOnWorker(const void* data) {
  const TargetArgs* a = static_cast<const TargetArgs*>(data);
  g_draw_target = a->surface;
  g_draw_panel = a->panel != 0;
  ++g_target_calls;
}

void ViewportOnWorker(const void* data) {
  if (!g_recording || !g_walk_follow) return;
  memcpy(&g_viewport, data, sizeof(g_viewport));
  ++g_viewport_calls;
  if (g_viewport.dwWidth == 0 || g_viewport.dwHeight == 0) return;
  SetViewportState(g_slots[g_slot].cb);
}

// A clear of Direct3D 9: the rects (or the whole viewport) in the viewport,
// for the color and the depth that the flags name.
void ClearOnWorker(const void* data) {
  if (!g_recording || !g_walk_follow) return;
  // The clear of the panel target is the first rendering of the panel.
  if (ForPanel()) {
    ++g_clears_panel;
    return;
  }
  if (OffTarget()) {
    ++g_clears_off;
    return;
  }
  UsePanel(false);
  const ClearArgs* a = static_cast<const ClearArgs*>(data);
  VkClearAttachment attachments[2] = {};
  uint32_t count = 0;
  if (a->flags & D3DCLEAR_TARGET) {
    attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachments[count].colorAttachment = 0;
    vk_scene_math::ClearColor(a->color,
                              attachments[count].clearValue.color.float32);
    ++count;
  }
  if (a->flags & D3DCLEAR_ZBUFFER) {
    attachments[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    attachments[count].clearValue.depthStencil.depth =
        vk_scene_math::ClearDepth(a->z);
    ++count;
  }
  if (count == 0) return;
  // Direct3D 9 clears in the viewport. With multiview, a rect of a clear
  // has layer 0 and one layer, and the view mask names the layers.
  const D3DVIEWPORT7& v = g_viewport;
  const vk_scene_math::Rect viewport =
      vk_scene_math::ViewportRect(v.dwX, v.dwY, v.dwWidth, v.dwHeight);
  VkClearRect rects[kClearRects];
  uint32_t rect_count = 0;
  if (a->count == 0 || a->count > kClearRects) {
    if (InTarget(viewport, &rects[0].rect)) rect_count = 1;
  } else {
    for (uint32_t i = 0; i < a->count; ++i) {
      const D3DRECT& d = a->rects[i];
      const vk_scene_math::Rect asked = {d.x1, d.y1, d.x2, d.y2};
      if (InTarget(vk_scene_math::Intersect(asked, viewport),
                   &rects[rect_count].rect)) {
        ++rect_count;
      }
    }
  }
  if (rect_count == 0) return;
  for (uint32_t i = 0; i < rect_count; ++i) {
    rects[i].baseArrayLayer = 0;
    rects[i].layerCount = 1;
  }
  // A clear for one eye: a rendering of that layer alone, between two
  // renderings of both. The dynamic states stay.
  const VkCommandBuffer cb = g_slots[g_slot].cb;
  const uint32_t eyes =
      g_walk_right ? 2u
                   : vk_scene_math::Eyes(OneWalkActive(), OneWalkModeNow());
  // A clear of the color of the whole target in both eyes needs no picture
  // of the walk before. A different clear comes after that picture.
  if (g_walk_keep) {
    const bool whole = (a->flags & D3DCLEAR_TARGET) != 0 && eyes == 3u &&
                       rect_count == 1 && rects[0].rect.offset.x == 0 &&
                       rects[0].rect.offset.y == 0 &&
                       rects[0].rect.extent.width == Target().width &&
                       rects[0].rect.extent.height == Target().height;
    if (whole) {
      g_walk_keep = false;
      ++g_cleared_walks;
    } else {
      KeepPicture();
    }
  }
  if (g_walk_mres) {
    MresClear c = {};
    c.flags = a->flags;
    c.color = a->color;
    c.z = a->z;
    c.eyes = eyes;
    c.count = rect_count;
    for (uint32_t i = 0; i < rect_count && i < 16; ++i) {
      c.rects[i][0] = static_cast<float>(rects[i].rect.offset.x);
      c.rects[i][1] = static_cast<float>(rects[i].rect.offset.y);
      c.rects[i][2] = c.rects[i][0] + static_cast<float>(rects[i].rect.extent.width);
      c.rects[i][3] = c.rects[i][1] + static_cast<float>(rects[i].rect.extent.height);
    }
    MresRecord r = {};
    r.clear = static_cast<int>(g_mres_clears.size());
    g_mres_clears.push_back(c);
    g_mres_records.push_back(r);
    ++g_clears;
    return;
  }
  if (eyes != 3u) {
    f.vkCmdEndRendering(cb);
    BeginRendering(cb, eyes, true, true);
    ++g_clears_one_eye;
  }
  f.vkCmdClearAttachments(cb, count, attachments, rect_count, rects);
  if (eyes != 3u) {
    f.vkCmdEndRendering(cb);
    BeginRendering(cb, 3u, true, true);
  }
  ++g_clears;
}

// Gives a change of the frame thread to the D3D7 thread in the order of the
// draws, or makes it now on the D3D7 thread.
void Deliver(D3D7ThreadFn fn, const void* data, unsigned bytes) {
  if (g_direct || D3D7ThreadOnWorker()) {
    fn(data);
  } else {
    D3D7ThreadPost(fn, data, bytes);
  }
}

// FOVEATED RESOLUTION: the layout of the walk, and the two images in its
// sizes. False when the walk has no foveated resolution.
void FreeMresImages() {
  const VkDevice device = vk::Get().device;
  for (int i = 0; i < 2; ++i) {
    for (int e = 0; e < 2; ++e) {
      Texture& t = g_mres_texture[i][e];
      for (int k = 0; k < t.set_count; ++k) {
        f.vkFreeDescriptorSets(device, g_pool, 1, &t.sets[k].set);
        --g_sets_live;
      }
      memset(&t, 0, sizeof(t));
      if (g_mres_view[i][e] != VK_NULL_HANDLE) {
        f.vkDestroyImageView(device, g_mres_view[i][e], nullptr);
        g_mres_view[i][e] = VK_NULL_HANDLE;
      }
    }
    vk::FreeImage(&g_mres_color[i]);
    vk::FreeImage(&g_mres_depth[i]);
  }
}

bool PrepareMres(const Seed* seed) {
  g_mres_records.clear();
  g_mres_clears.clear();
  // Only a walk of the eyes: another walk of the game (with the angle of
  // the game, not the one of the headset) keeps the pixels of the render.
  if (!seed->mres || seed->fovea.tan_h <= 0.0f || seed->fovea.tan_v <= 0.0f ||
      (seed->eye < 0 && !seed->both)) {
    return false;
  }
  const uint32_t width = Target().width, height = Target().height;
  g_mres = fovea_res_math::MakeLayout(
      width, height, seed->fovea.tan_h, seed->fovea.tan_v, seed->gaze[0],
      seed->gaze[1], seed->mres_outer, seed->mres_center, seed->mres_deg);
  // The images only grow: a smaller layout uses the top left part of them,
  // so a change of the layout does not stop the GPU to make new images.
  uint32_t sizes[2][2] = {{g_mres.outer_w, g_mres.outer_h},
                          {g_mres.center_w, g_mres.center_h}};
  if (g_mres_color[0].width < sizes[0][0] ||
      g_mres_color[0].height < sizes[0][1] ||
      g_mres_color[1].width < sizes[1][0] ||
      g_mres_color[1].height < sizes[1][1]) {
    for (int i = 0; i < 2; ++i) {
      if (g_mres_color[i].width > sizes[i][0]) sizes[i][0] = g_mres_color[i].width;
      if (g_mres_color[i].height > sizes[i][1]) sizes[i][1] = g_mres_color[i].height;
    }
    ++g_mres_remade;
    if (g_mres_color[0].image != VK_NULL_HANDLE) {
      vk::LockQueue();
      f.vkDeviceWaitIdle(vk::Get().device);
      vk::UnlockQueue();
    }
    FreeMresImages();
    for (int i = 0; i < 2; ++i) {
      if (!vk::MakeImage(sizes[i][0], sizes[i][1], 1, vk::Get().color_format,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, &g_mres_color[i], 2) ||
          !vk::MakeImage(sizes[i][0], sizes[i][1], 1, kDepthFormat,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, &g_mres_depth[i], 2)) {
        FreeMresImages();
        return false;
      }
      for (int e = 0; e < 2; ++e) {
        VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = g_mres_color[i].image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = vk::Get().color_format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                                 static_cast<uint32_t>(e), 1};
        if (f.vkCreateImageView(vk::Get().device, &view, nullptr,
                                &g_mres_view[i][e]) != VK_SUCCESS) {
          FreeMresImages();
          return false;
        }
        g_mres_texture[i][e].image.view = g_mres_view[i][e];
      }
    }
    if (!g_said_mres_size) {
      g_said_mres_size = true;
      ModLog("vk scene     : fovea_res: the picture is %u by %u, the outer "
             "image %u by %u, the centre image %u by %u for a rect of %.0f "
             "by %.0f pixels. The two images have %.0f percent of the pixels "
             "of the picture",
             width, height, g_mres.outer_w, g_mres.outer_h, g_mres.center_w,
             g_mres.center_h, g_mres.inset_w, g_mres.inset_h,
             100.0f * fovea_res_math::PixelShare(g_mres, width, height));
    }
  }
  ++g_mres_walks;
  return true;
}

// The rect of a viewport of the game in an image of a pass, cut to the
// image. False when no pixel is in it.
bool PassScissor(const float vp[4], float ox, float oy, float sx, float sy,
                 uint32_t used_w, uint32_t used_h, VkRect2D* out) {
  float r[4];
  fovea_res_math::MapRect(vp[0], vp[1], vp[0] + vp[2], vp[1] + vp[3], ox, oy,
                          sx, sy, r);
  const vk_scene_math::Rect asked = {static_cast<int64_t>(floorf(r[0])),
                                     static_cast<int64_t>(floorf(r[1])),
                                     static_cast<int64_t>(ceilf(r[2])),
                                     static_cast<int64_t>(ceilf(r[3]))};
  vk_scene_math::Rect in;
  if (!vk_scene_math::InTarget(asked, used_w, used_h, &in)) return false;
  out->offset = {static_cast<int32_t>(in.x0), static_cast<int32_t>(in.y0)};
  out->extent = {static_cast<uint32_t>(in.x1 - in.x0),
                 static_cast<uint32_t>(in.y1 - in.y0)};
  return true;
}

// The states of a draw that are not in the pipeline (D3D values).
void DrawStates(VkCommandBuffer cb, uint32_t cull, uint32_t zenable,
                uint32_t zwrite, uint32_t zfunc, uint32_t type) {
  // D3DCULL_: 1 none, 2 clockwise, 3 counter-clockwise. The device culls the
  // faces of that turn on the screen. y goes down on both screens.
  f.vkCmdSetCullMode(cb, cull == 2 || cull == 3 ? VK_CULL_MODE_BACK_BIT
                                                : VK_CULL_MODE_NONE);
  f.vkCmdSetFrontFace(cb, cull == 2 ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                    : VK_FRONT_FACE_CLOCKWISE);
  f.vkCmdSetDepthTestEnable(cb, zenable != 0 ? VK_TRUE : VK_FALSE);
  f.vkCmdSetDepthWriteEnable(cb, zenable != 0 && zwrite != 0 ? VK_TRUE
                                                             : VK_FALSE);
  static const VkCompareOp kCompare[9] = {
      VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_NEVER,
      VK_COMPARE_OP_LESS,          VK_COMPARE_OP_EQUAL,
      VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,
      VK_COMPARE_OP_NOT_EQUAL,     VK_COMPARE_OP_GREATER_OR_EQUAL,
      VK_COMPARE_OP_ALWAYS};
  f.vkCmdSetDepthCompareOp(cb, kCompare[zfunc <= 8 ? zfunc : 0]);
  // D3DPT_: 4 list, 5 strip, 6 fan.
  f.vkCmdSetPrimitiveTopology(cb,
                              type == 5   ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                              : type == 6 ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN
                                          : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
}

// Vertices of the format 0x1C4 into the ring of the slot. UINT64_MAX when
// the ring has no room.
// THE SECOND STAGE: set 0 and set 1 of a draw. A draw with no stage 1 gives
// null, and set 1 is then the set of stage 0.
void BindSets(VkCommandBuffer cb, VkDescriptorSet set, VkDescriptorSet set1) {
  const VkDescriptorSet sets[2] = {set, set1 != VK_NULL_HANDLE ? set1 : set};
  f.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0,
                            2, sets, 0, nullptr);
}

// Vertex buffer 0 and vertex buffer 1 (the texture coordinates of stage 1)
// of a draw, both in the ring. A draw with no stage 1 gives the place of its
// vertices for both.
void BindVertices(VkCommandBuffer cb, const Slot& s, VkDeviceSize vertex_at,
                  VkDeviceSize uv1_at) {
  const VkBuffer buffers[2] = {s.ring.buffer, s.ring.buffer};
  const VkDeviceSize offsets[2] = {vertex_at, uv1_at};
  f.vkCmdBindVertexBuffers(cb, 0, 2, buffers, offsets);
}

VkDeviceSize RingVertices(Slot& s, const void* data, VkDeviceSize bytes) {
  if (s.used + bytes > kRingBytes) return UINT64_MAX;
  const VkDeviceSize at = s.used;
  memcpy(static_cast<uint8_t*>(s.ring.mapped) + at, data,
         static_cast<size_t>(bytes));
  s.used = (s.used + bytes + 31) & ~31ull;
  return at;
}

// A clear of the records in an image of a pass.
void PlayClear(VkCommandBuffer cb, const MresClear& c, vk::Image& color,
               vk::Image& depth, float ox, float oy, float sx, float sy,
               uint32_t used_w, uint32_t used_h) {
  VkClearAttachment attachments[2] = {};
  uint32_t count = 0;
  if (c.flags & D3DCLEAR_TARGET) {
    attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vk_scene_math::ClearColor(c.color,
                              attachments[count].clearValue.color.float32);
    ++count;
  }
  if (c.flags & D3DCLEAR_ZBUFFER) {
    attachments[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    attachments[count].clearValue.depthStencil.depth =
        vk_scene_math::ClearDepth(c.z);
    ++count;
  }
  if (count == 0) return;
  VkClearRect rects[16];
  uint32_t rect_count = 0;
  for (uint32_t i = 0; i < c.count && i < 16; ++i) {
    const float vp[4] = {c.rects[i][0], c.rects[i][1],
                         c.rects[i][2] - c.rects[i][0],
                         c.rects[i][3] - c.rects[i][1]};
    if (PassScissor(vp, ox, oy, sx, sy, used_w, used_h,
                    &rects[rect_count].rect)) {
      rects[rect_count].baseArrayLayer = 0;
      rects[rect_count].layerCount = 1;
      ++rect_count;
    }
  }
  if (rect_count == 0) return;
  if (c.eyes != 3u) {
    f.vkCmdEndRendering(cb);
    BeginRenderingOf(cb, color, depth, c.eyes, true, true);
  }
  f.vkCmdClearAttachments(cb, count, attachments, rect_count, rects);
  if (c.eyes != 3u) {
    f.vkCmdEndRendering(cb);
    BeginRenderingOf(cb, color, depth, 3u, true, true);
  }
}

void ComposeMres(Slot& s);

// A TEST AID of foveated resolution: one line for a walk that saves its
// pictures, and for each of the first 40 walks with draws of which the
// outer image got none, or whose last record is a clear of the color. The
// line has the layout, the records, the clears and what each pass did.
void MresWalkLine(const Slot& s, const uint32_t drawn[2],
                  const uint32_t out_of_view[2], const uint32_t left_out[2]) {
  uint32_t draws = 0;
  for (const MresRecord& r : g_mres_records) {
    if (r.clear < 0) ++draws;
  }
  const bool last_clear =
      !g_mres_records.empty() && g_mres_records.back().clear >= 0 &&
      (g_mres_clears[g_mres_records.back().clear].flags & D3DCLEAR_TARGET);
  static int s_lines = 0;
  const bool odd = (draws > 0 && drawn[0] == 0) || last_clear;
  if (!g_picture && !(odd && s_lines < 40)) return;
  if (!g_picture) ++s_lines;
  char clears[256] = "";
  int at = 0;
  for (size_t i = 0; i < g_mres_clears.size() && i < 4 && at < 200; ++i) {
    const MresClear& c = g_mres_clears[i];
    at += _snprintf_s(clears + at, sizeof(clears) - at, _TRUNCATE,
                      " [flags 0x%X color 0x%08X z %.2f eyes %u rects %u]",
                      c.flags, c.color, c.z, c.eyes, c.count);
  }
  const float* vp = nullptr;
  for (const MresRecord& r : g_mres_records) {
    if (r.clear < 0) {
      vp = r.viewport;
      break;
    }
  }
  ModLog("vk scene     : fovea_res walk %llu%s: %u draws and %u clears%s. "
         "The outer image %u by %u (scale %.3f %.3f), the centre %u by %u at "
         "%.0f %.0f, %.0f by %.0f (scale %.3f %.3f). Drawn %u and %u, out of "
         "the view %u and %u, not in the centre %u. The first viewport %.0f "
         "%.0f %.0f %.0f. The last record is %s",
         g_walk_number, g_picture ? " (a picture)" : "", draws,
         static_cast<unsigned>(g_mres_clears.size()), clears, g_mres.outer_w,
         g_mres.outer_h, g_mres.outer_scale_x, g_mres.outer_scale_y,
         g_mres.center_w, g_mres.center_h, g_mres.inset_x0, g_mres.inset_y0,
         g_mres.inset_w, g_mres.inset_h, g_mres.center_scale_x,
         g_mres.center_scale_y, drawn[0], drawn[1], out_of_view[0],
         out_of_view[1], left_out[1], vp ? vp[0] : 0.0f, vp ? vp[1] : 0.0f,
         vp ? vp[2] : 0.0f, vp ? vp[3] : 0.0f,
         last_clear ? "a clear of the color" : "a draw or a clear of the depth");
  // The quads of the screen (8 vertices or fewer) that cover half of the
  // picture or more: such a quad can hide each draw before it. The first
  // 12, with their place in the records.
  const float width = static_cast<float>(Target().width);
  const float height = static_cast<float>(Target().height);
  int listed = 0;
  for (size_t i = 0; i < g_mres_records.size() && listed < 12; ++i) {
    const MresRecord& r = g_mres_records[i];
    if (r.clear >= 0 || r.vertex_count > 8) continue;
    const float x0 = r.bounds[0] > 0.0f ? r.bounds[0] : 0.0f;
    const float y0 = r.bounds[1] > 0.0f ? r.bounds[1] : 0.0f;
    const float x1 = r.bounds[2] < width ? r.bounds[2] : width;
    const float y1 = r.bounds[3] < height ? r.bounds[3] : height;
    const float area = x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) : 0.0f;
    if (area < 0.5f * width * height) continue;
    ++listed;
    uint32_t diffuse = 0;
    float z = 0.0f;
    const uint8_t* v = static_cast<const uint8_t*>(s.ring.mapped) + r.vertex_at;
    memcpy(&z, v + 8, 4);
    memcpy(&diffuse, v + 16, 4);
    ModLog("vk scene     :   record %u of %u: a draw over %.0f percent of "
           "the picture, blend 0x%X, ops 0x%X 0x%X 0x%X 0x%X, z enable %u "
           "write %u func %u, %u vertices, the first diffuse 0x%08X and z "
           "%.3f",
           static_cast<unsigned>(i),
           static_cast<unsigned>(g_mres_records.size()),
           100.0f * area / (width * height), r.blend, r.push.ops[0],
           r.push.ops[1], r.push.ops[2], r.push.ops[3], r.zenable, r.zwrite,
           r.zfunc, r.vertex_count, diffuse, z);
  }
}

// The records of the walk into the outer image and the centre image, then
// the eye picture from the two.
void PlayMres(Slot& s) {
  const VkCommandBuffer cb = s.cb;
  // A TEST AID: what each pass did with the records (MresWalkLine).
  uint32_t drawn[2] = {0, 0}, out_of_view[2] = {0, 0}, left_out[2] = {0, 0};
  for (int pass = 0; pass < 2; ++pass) {
    vk::Image& color = g_mres_color[pass];
    vk::Image& depth = g_mres_depth[pass];
    const float ox = pass == 1 ? g_mres.inset_x0 : 0.0f;
    const float oy = pass == 1 ? g_mres.inset_y0 : 0.0f;
    const float sx = pass == 1 ? g_mres.center_scale_x : g_mres.outer_scale_x;
    const float sy = pass == 1 ? g_mres.center_scale_y : g_mres.outer_scale_y;
    // The part of the image that the layout uses.
    const uint32_t used_w = pass == 1 ? g_mres.center_w : g_mres.outer_w;
    const uint32_t used_h = pass == 1 ? g_mres.center_h : g_mres.outer_h;
    if (g_walk_right) {
      // Layer 0 has the left eye of the walk before: the barrier keeps it.
      vk::Barrier(cb, color.image, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);
      vk::Barrier(cb, color.image, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1, 1);
    } else {
      vk::Barrier(cb, color.image, VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 2);
    }
    vk::Barrier(cb, depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 1, 2);
    if (g_walk_right) {
      BeginRenderingOf(cb, color, depth, 2u, false, true);
      f.vkCmdEndRendering(cb);
      BeginRenderingOf(cb, color, depth, 3u, true, true);
    } else {
      BeginRenderingOf(cb, color, depth, 3u, false, true);
    }
    float last_vp[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    bool in_view = false;
    VkPipeline last_pipeline = VK_NULL_HANDLE;
    VkDescriptorSet last_set = VK_NULL_HANDLE;
    VkDescriptorSet last_set1 = VK_NULL_HANDLE;
    for (const MresRecord& r : g_mres_records) {
      if (r.clear >= 0) {
        PlayClear(cb, g_mres_clears[r.clear], color, depth, ox, oy, sx, sy,
                  used_w, used_h);
        continue;
      }
      if (memcmp(r.viewport, last_vp, sizeof(last_vp)) != 0) {
        memcpy(last_vp, r.viewport, sizeof(last_vp));
        const VkViewport viewport = {(r.viewport[0] - ox) * sx,
                                     (r.viewport[1] - oy) * sy,
                                     r.viewport[2] * sx, r.viewport[3] * sy,
                                     0.0f, 1.0f};
        f.vkCmdSetViewport(cb, 0, 1, &viewport);
        VkRect2D scissor = {{0, 0}, {0, 0}};
        in_view = PassScissor(r.viewport, ox, oy, sx, sy, used_w, used_h,
                              &scissor);
        if (in_view) f.vkCmdSetScissor(cb, 0, 1, &scissor);
      }
      if (!in_view) {
        ++out_of_view[pass];
        continue;
      }
      if (pass == 1 && (r.bounds[2] < g_mres.inset_x0 - 1.0f ||
                        r.bounds[0] > g_mres.inset_x0 + g_mres.inset_w + 1.0f ||
                        r.bounds[3] < g_mres.inset_y0 - 1.0f ||
                        r.bounds[1] > g_mres.inset_y0 + g_mres.inset_h + 1.0f)) {
        ++left_out[pass];
        continue;
      }
      ++drawn[pass];
      ++(pass == 1 ? g_mres_center_draws : g_mres_draws);
      if (r.pipeline != last_pipeline) {
        f.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipeline);
        last_pipeline = r.pipeline;
      }
      if (r.set != last_set || r.set1 != last_set1) {
        BindSets(cb, r.set, r.set1);
        last_set = r.set;
        last_set1 = r.set1;
      }
      DrawStates(cb, r.cull, r.zenable, r.zwrite, r.zfunc, r.type);
      f.vkCmdPushConstants(
          cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
          0, sizeof(r.push), &r.push);
      BindVertices(cb, s, r.vertex_at, r.uv1_at);
      if (r.index_count > 0) {
        f.vkCmdBindIndexBuffer(cb, s.ring.buffer, r.index_at,
                               VK_INDEX_TYPE_UINT16);
        f.vkCmdDrawIndexed(cb, r.index_count, 1, 0, 0, 0);
      } else {
        f.vkCmdDraw(cb, r.vertex_count, 1, 0, 0);
      }
    }
    f.vkCmdEndRendering(cb);
    vk::Barrier(cb, color.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 2);
  }
  MresWalkLine(s, drawn, out_of_view, left_out);
  // For the check for a black picture: what the walk drew, and the quads
  // of the screen that cover half of the picture or more (the first 3).
  {
    uint32_t draws = 0;
    for (const MresRecord& r : g_mres_records) {
      if (r.clear < 0) ++draws;
    }
    int at = _snprintf_s(
        s.probe_text, sizeof(s.probe_text), _TRUNCATE,
        "fovea_res, %u draws, %u clears, drawn %u and %u, out of the view %u "
        "and %u",
        draws, static_cast<unsigned>(g_mres_clears.size()), drawn[0], drawn[1],
        out_of_view[0], out_of_view[1]);
    const float width = static_cast<float>(Target().width);
    const float height = static_cast<float>(Target().height);
    int listed = 0;
    for (size_t i = 0; i < g_mres_records.size() && listed < 3 && at > 0; ++i) {
      const MresRecord& r = g_mres_records[i];
      if (r.clear >= 0) {
        const MresClear& c = g_mres_clears[r.clear];
        if ((c.flags & D3DCLEAR_TARGET) && i + 1 == g_mres_records.size()) {
          at += _snprintf_s(s.probe_text + at, sizeof(s.probe_text) - at,
                            _TRUNCATE, ", the last record is a clear of 0x%08X",
                            c.color);
        }
        continue;
      }
      if (r.vertex_count > 8) continue;
      const float x0 = r.bounds[0] > 0.0f ? r.bounds[0] : 0.0f;
      const float y0 = r.bounds[1] > 0.0f ? r.bounds[1] : 0.0f;
      const float x1 = r.bounds[2] < width ? r.bounds[2] : width;
      const float y1 = r.bounds[3] < height ? r.bounds[3] : height;
      if (!(x1 > x0 && y1 > y0) ||
          (x1 - x0) * (y1 - y0) < 0.5f * width * height) {
        continue;
      }
      ++listed;
      uint32_t diffuse = 0;
      memcpy(&diffuse,
             static_cast<const uint8_t*>(s.ring.mapped) + r.vertex_at + 16, 4);
      at += _snprintf_s(s.probe_text + at, sizeof(s.probe_text) - at,
                        _TRUNCATE,
                        ", a quad of the screen at record %u: blend 0x%X, ops "
                        "0x%X 0x%X, diffuse 0x%08X",
                        static_cast<unsigned>(i), r.blend, r.push.ops[0],
                        r.push.ops[1], diffuse);
    }
  }
  ComposeMres(s);
}

// THE CHECK FOR A BLACK PICTURE (vk_black_check=1, backend=vulkan). At the
// end of a walk that finishes the eye picture, 16 by 16 points of each layer
// go into the buffer of the slot (a blit with the nearest pixel, and a
// copy). At the next use of the slot, after its fence, the frame thread
// reads them (BlackCheckRead). A layer with no point above 0 is a black
// picture: the log gets a line with what the walk drew.
const uint32_t kProbeSide = 16;
bool g_black_check = true;
vk::Image g_probe_image = {};
uint64_t g_black_walks = 0;  // for the report
uint64_t g_probed_walks = 0;
int g_black_lines = 0;
bool g_black_before = false;  // the walk checked before was black

void BlackCheckWrite(Slot& s) {
  if (!g_black_check || !g_direct || !(g_walk_both || g_walk_right)) return;
  const VkDeviceSize bytes = kProbeSide * kProbeSide * 4 * 2;
  if (g_probe_image.image == VK_NULL_HANDLE &&
      !vk::MakeImage(kProbeSide, kProbeSide, 1, vk::Get().color_format,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, &g_probe_image, 2)) {
    g_black_check = false;
    return;
  }
  if (s.probe.buffer == VK_NULL_HANDLE &&
      !vk::MakeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                      &s.probe)) {
    g_black_check = false;
    return;
  }
  const VkCommandBuffer cb = s.cb;
  vk::Barrier(cb, Target().image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 2);
  vk::Barrier(cb, g_probe_image.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              1, 2);
  VkImageBlit blit = {};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
  blit.srcOffsets[1] = {static_cast<int32_t>(Target().width),
                        static_cast<int32_t>(Target().height), 1};
  blit.dstSubresource = blit.srcSubresource;
  blit.dstOffsets[1] = {static_cast<int32_t>(kProbeSide),
                        static_cast<int32_t>(kProbeSide), 1};
  f.vkCmdBlitImage(cb, Target().image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   g_probe_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);
  vk::Barrier(cb, g_probe_image.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 2);
  VkBufferImageCopy copy = {};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
  copy.imageExtent = {kProbeSide, kProbeSide, 1};
  f.vkCmdCopyImageToBuffer(cb, g_probe_image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           s.probe.buffer, 1, &copy);
  vk::Barrier(cb, Target().image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 2);
  s.probe_written = true;
  s.probe_walk = g_walk_number;
  if (!g_walk_mres) {
    _snprintf_s(s.probe_text, sizeof(s.probe_text), _TRUNCATE,
                "no fovea_res, %llu draws in the walk",
                g_draws - g_walk_draws_start);
  }
}

// After the fence of the slot: the points of the walk that used it before.
void BlackCheckRead(Slot& s) {
  if (!s.probe_written) return;
  s.probe_written = false;
  const uint8_t* p = static_cast<const uint8_t*>(s.probe.mapped);
  if (p == nullptr) return;
  ++g_probed_walks;
  bool black[2] = {true, true};
  for (int layer = 0; layer < 2; ++layer) {
    const uint8_t* q = p + layer * kProbeSide * kProbeSide * 4;
    for (uint32_t i = 0; i < kProbeSide * kProbeSide; ++i) {
      if (q[i * 4] != 0 || q[i * 4 + 1] != 0 || q[i * 4 + 2] != 0) {
        black[layer] = false;
        break;
      }
    }
  }
  const bool any = black[0] || black[1];
  if (any) ++g_black_walks;
  // A line at the start of each run of black pictures, the first 60.
  if (any && !g_black_before && g_black_lines < 60) {
    ++g_black_lines;
    ModLog("vk scene     : a black eye picture in walk %llu (%s): %s",
           s.probe_walk,
           black[0] && black[1] ? "both eyes"
           : black[0]           ? "the left eye"
                                : "the right eye",
           s.probe_text);
  }
  g_black_before = any;
}

// THE EYE PICTURE from the two images: for each eye the outer image over
// the whole picture, and the centre image over its rect, with an edge of 8
// percent of the rect where its alpha goes from 1 to 0. With the marks of
// the test the outer image has a blue colour.
void ComposeMres(Slot& s) {
  const VkCommandBuffer cb = s.cb;
  const uint32_t width = Target().width, height = Target().height;
  BeginRenderingOf(cb, Target(), g_depth, 3u, false, false);
  const VkViewport viewport = {0.0f, 0.0f, static_cast<float>(width),
                               static_cast<float>(height), 0.0f, 1.0f};
  f.vkCmdSetViewport(cb, 0, 1, &viewport);
  const VkRect2D scissor = {{0, 0}, {width, height}};
  f.vkCmdSetScissor(cb, 0, 1, &scissor);
  DrawStates(cb, D3DCULL_NONE, 0, 0, D3DCMP_ALWAYS, D3DPT_TRIANGLELIST);
  const VkPipeline opaque = Pipeline(0);
  const VkPipeline blended =
      Pipeline((1u << 16) | (D3DBLEND_SRCALPHA << 8) | D3DBLEND_INVSRCALPHA);
  const VkSampler sampler = Sampler(2, 2, 1, 3, 3, 1);
  if (opaque == VK_NULL_HANDLE || blended == VK_NULL_HANDLE) {
    f.vkCmdEndRendering(cb);
    return;
  }
  struct Vertex {
    float x, y, z, rhw;
    uint32_t diffuse, specular;
    float u, v;
  };
  const float x0 = -0.5f, y0 = -0.5f;
  const float x1 = static_cast<float>(width) - 0.5f;
  const float y1 = static_cast<float>(height) - 0.5f;
  // The centre: a grid of 4 by 4 points, alpha 1 inside the edge.
  const float ix0 = g_mres.inset_x0 - 0.5f, iy0 = g_mres.inset_y0 - 0.5f;
  const float iw = g_mres.inset_w, ih = g_mres.inset_h;
  const float edge = 0.08f * (iw < ih ? iw : ih);
  const float gx[4] = {ix0, ix0 + edge, ix0 + iw - edge, ix0 + iw};
  const float gy[4] = {iy0, iy0 + edge, iy0 + ih - edge, iy0 + ih};
  for (int eye = 0; eye < 2; ++eye) {
    DrawPush push = {};
    push.viewport[2] = 2.0f / static_cast<float>(width);
    push.viewport[3] = 2.0f / static_cast<float>(height);
    push.depth[1] = 1.0f;
    // D3DTOP_SELECTARG1 = 2, D3DTOP_MODULATE = 4. D3DTA_DIFFUSE = 0,
    // D3DTA_TEXTURE = 2.
    push.ops[0] = g_walk_show ? (4u | 2u << 8 | 0u << 16) : (2u | 2u << 8);
    push.ops[1] = 2u | 0u << 8;
    push.ops[2] = 1u;  // a texture
    push.ops[3] = 1u | (1u << eye) << 8;  // still, the eye
    const uint32_t tint = g_walk_show ? 0xFF90B8FFu : 0xFFFFFFFFu;
    // The used part of each image.
    const float ou = static_cast<float>(g_mres.outer_w) /
                     static_cast<float>(g_mres_color[0].width);
    const float ov = static_cast<float>(g_mres.outer_h) /
                     static_cast<float>(g_mres_color[0].height);
    const float cu = static_cast<float>(g_mres.center_w) /
                     static_cast<float>(g_mres_color[1].width);
    const float cv = static_cast<float>(g_mres.center_h) /
                     static_cast<float>(g_mres_color[1].height);
    const Vertex outer[6] = {{x0, y0, 0, 1, tint, 0, 0, 0},
                             {x1, y0, 0, 1, tint, 0, ou, 0},
                             {x1, y1, 0, 1, tint, 0, ou, ov},
                             {x0, y0, 0, 1, tint, 0, 0, 0},
                             {x1, y1, 0, 1, tint, 0, ou, ov},
                             {x0, y1, 0, 1, tint, 0, 0, ov}};
    const VkDescriptorSet outer_set = SetOf(&g_mres_texture[0][eye], sampler);
    const VkDescriptorSet center_set = SetOf(&g_mres_texture[1][eye], sampler);
    if (outer_set == VK_NULL_HANDLE || center_set == VK_NULL_HANDLE) continue;
    VkDeviceSize at = RingVertices(s, outer, sizeof(outer));
    if (at == UINT64_MAX) continue;
    f.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, opaque);
    BindSets(cb, outer_set, VK_NULL_HANDLE);
    f.vkCmdPushConstants(cb, g_layout,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof(push), &push);
    BindVertices(cb, s, at, at);
    f.vkCmdDraw(cb, 6, 1, 0, 0);
    // The centre with its soft edge.
    Vertex center[54];
    int n = 0;
    for (int j = 0; j < 3; ++j) {
      for (int i = 0; i < 3; ++i) {
        const int corner[6][2] = {{i, j},         {i + 1, j},
                                  {i + 1, j + 1}, {i, j},
                                  {i + 1, j + 1}, {i, j + 1}};
        for (int k = 0; k < 6; ++k) {
          const int ci = corner[k][0], cj = corner[k][1];
          const bool inside = ci >= 1 && ci <= 2 && cj >= 1 && cj <= 2;
          Vertex& v = center[n++];
          v.x = gx[ci];
          v.y = gy[cj];
          v.z = 0.0f;
          v.rhw = 1.0f;
          v.diffuse = inside ? 0xFFFFFFFFu : 0x00FFFFFFu;
          v.specular = 0;
          v.u = (gx[ci] - ix0) / iw * cu;
          v.v = (gy[cj] - iy0) / ih * cv;
        }
      }
    }
    at = RingVertices(s, center, sizeof(center));
    if (at == UINT64_MAX) continue;
    push.ops[0] = 2u | 2u << 8;  // the texture alone
    f.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blended);
    BindSets(cb, center_set, VK_NULL_HANDLE);
    f.vkCmdPushConstants(cb, g_layout,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                         0, sizeof(push), &push);
    BindVertices(cb, s, at, at);
    f.vkCmdDraw(cb, 54, 1, 0, 0);
  }
  f.vkCmdEndRendering(cb);
}

// FOVEATED RENDERING: the map of the rates for the walk, into the image in
// the buffer of the walk when the fovea or its centre changed. False when the
// walk has no fovea.
bool PrepareFovea(Slot& s, const Seed* seed) {
  const foveation_math::Params& p = seed->fovea;
  if (p.inner_deg <= 0.0f || p.tan_h <= 0.0f || p.tan_v <= 0.0f) return false;
  const vk::Core& core = vk::Get();
  if (!core.shading_rate) {
    if (!g_said_no_rate) {
      g_said_no_rate = true;
      ModLog("vk scene     : foveation is on, but the GPU has no shading rate "
             "attachment, so each pixel has the full rate");
    }
    return false;
  }
  const uint32_t width = Target().width, height = Target().height;
  const uint32_t map_w = foveation_math::TexelCount(width, core.rate_texel_w);
  const uint32_t map_h = foveation_math::TexelCount(height, core.rate_texel_h);
  const VkDevice device = core.device;
  const int slot = static_cast<int>(&s - g_slots);
  if (g_rate.image == VK_NULL_HANDLE || g_rate_map_w != map_w ||
      g_rate_map_h != map_h) {
    if (g_rate.image != VK_NULL_HANDLE) {
      vk::LockQueue();
      f.vkDeviceWaitIdle(device);
      vk::UnlockQueue();
    }
    vk::FreeImage(&g_rate);
    for (int i = 0; i < 2; ++i) vk::FreeBuffer(&g_rate_stage[i]);
    if (!vk::MakeImage(map_w, map_h, 1, VK_FORMAT_R8_UINT,
                       VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, &g_rate, 2)) {
      return false;
    }
    g_rate_map_w = map_w;
    g_rate_map_h = map_h;
    g_rate_inner = -1.0f;
  }
  // The centre of the fovea, in texels.
  float px = 0.0f, py = 0.0f;
  foveation_math::TangentPixel(seed->gaze[0], seed->gaze[1], width, height, p,
                               &px, &py);
  const int cx = static_cast<int>(px) / static_cast<int>(core.rate_texel_w);
  const int cy = static_cast<int>(py) / static_cast<int>(core.rate_texel_h);
  if (g_rate_inner != p.inner_deg || g_rate_outer != p.outer_deg ||
      cx != g_rate_cx || cy != g_rate_cy) {
    const VkDeviceSize layer = static_cast<VkDeviceSize>(map_w) * map_h;
    vk::Buffer& stage = g_rate_stage[slot];
    if (stage.buffer == VK_NULL_HANDLE &&
        !vk::MakeBuffer(layer * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true,
                        &stage)) {
      return false;
    }
    uint8_t* map = static_cast<uint8_t*>(stage.mapped);
    foveation_math::BuildMap(map, map_w, map_h, core.rate_texel_w,
                             core.rate_texel_h, width, height, seed->gaze[0],
                             seed->gaze[1], p);
    memcpy(map + layer, map, static_cast<size_t>(layer));
    vk::Barrier(s.cb, g_rate.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, 2);
    VkBufferImageCopy copy = {};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 2};
    copy.imageExtent = {map_w, map_h, 1};
    f.vkCmdCopyBufferToImage(s.cb, stage.buffer, g_rate.image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    vk::Barrier(s.cb, g_rate.image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
                1, 2);
    g_rate_inner = p.inner_deg;
    g_rate_outer = p.outer_deg;
    g_rate_cx = cx;
    g_rate_cy = cy;
    ++g_fovea_maps;
  }
  ++g_fovea_walks;
  return true;
}

// THE WALK, on the D3D7 thread.
void BeginOnWorker(const void* data) {
  const Seed* seed = static_cast<const Seed*>(data);
  g_walk_only = false;
  g_walk_mres = false;
  if (!Ready()) return;
  memcpy(g_rs, seed->rs, sizeof(g_rs));
  memcpy(g_ts, seed->ts, sizeof(g_ts));
  g_texture = seed->texture;
  g_texture1 = seed->texture1;
  g_viewport = seed->viewport;
  if (!g_said_seed) {
    g_said_seed = true;
    ModLog("vk scene     : the seed of the first walk: stage 0 color op %u "
           "args %u %u, alpha op %u args %u %u, index %u. Stage 1 color op "
           "%u args %u %u, alpha op %u, index %u. Fog %u, table %u, "
           "specular %u, alpha func %u, z enable %u, the texture transform "
           "0x%X",
           g_ts[0][1], g_ts[0][2], g_ts[0][3], g_ts[0][4], g_ts[0][5],
           g_ts[0][6], g_ts[0][11], g_ts[1][1], g_ts[1][2], g_ts[1][3],
           g_ts[1][4], g_ts[1][11], g_rs[28], g_rs[35], g_rs[29], g_rs[25],
           g_rs[7], g_ts[0][24]);
  }
  if (g_viewport.dwWidth == 0 || g_viewport.dwHeight == 0) return;
  if (!MakeTarget(g_viewport.dwWidth, g_viewport.dwHeight)) {
    Fail("the target");
    return;
  }
  g_slot ^= 1;
  g_walk_both = seed->both;
  g_walk_right = !seed->both && seed->eye == 1 &&
                 g_left_serial ==
                     InterlockedCompareExchange(&g_walk_serial, 0, 0) &&
                 g_left_image == Target().image;
  // THE INTRO IN STEREO (backend=vulkan, vk_keep_picture=1). The effect
  // draws over the picture of the frame before, and each eye must have its
  // own (intro.md, "The trails"). So the walk of an eye pass takes the target
  // of its eye and keeps its pixels.
  g_walk_own = false;
  if (seed->intro_eye >= 0 && seed->intro_eye <= 1 && g_keep_picture &&
      !g_walk_right) {
    g_target = seed->intro_eye;
    g_walk_own = g_target_drawn[g_target];
  } else if (!g_walk_right) {
    g_target ^= 1;
  }
  Slot& s = g_slots[g_slot];
  const VkDevice device = vk::Get().device;
  if (f.vkGetFenceStatus(device, s.fence) != VK_SUCCESS) {
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    f.vkWaitForFences(device, 1, &s.fence, VK_TRUE, UINT64_MAX);
    QueryPerformanceCounter(&t1);
    ++g_fence_waits;
    g_fence_wait_ticks += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
  }
  BlackCheckRead(s);
  f.vkResetFences(device, 1, &s.fence);
  s.stage_used = 0;
  if (s.query_written) {
    // Each query with its availability. A time over 100 ms is not a time
    // of a walk, and the report counts it apart.
    uint64_t stamps[4] = {0, 0, 0, 0};
    if (f.vkGetQueryPoolResults(
            device, s.query, 0, 2, sizeof(stamps), stamps,
            2 * sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ==
            VK_SUCCESS &&
        stamps[1] != 0 && stamps[3] != 0) {
      // The valid bits of the queue: a difference of two stamps in them.
      const uint32_t bits = vk::Get().timestamp_bits;
      const uint64_t mask = bits >= 64 || bits == 0 ? ~0ull : (1ull << bits) - 1;
      const uint64_t ticks = (stamps[2] - stamps[0]) & mask;
      if (static_cast<double>(ticks) * vk::Get().timestamp_ns < 1e8) {
        g_gpu_ticks += ticks;
        ++g_gpu_walks;
      } else {
        if (g_gpu_odd_said < 4) {
          ++g_gpu_odd_said;
          ModLog("vk scene     : a time of the GPU that is not used: the "
                 "stamps 0x%llX and 0x%llX, %u valid bits, %.3f ns a tick",
                 stamps[0], stamps[2], bits, vk::Get().timestamp_ns);
        }
        ++g_gpu_odd;
      }
    }
    s.query_written = false;
  }
  for (int i = 0; i < s.garbage_count; ++i) vk::FreeImage(&s.garbage[i]);
  s.garbage_count = 0;
  for (int i = 0; i < s.staging_count; ++i) vk::FreeBuffer(&s.staging[i]);
  s.staging_count = 0;
  if (s.dead_count > 0) {
    f.vkFreeDescriptorSets(device, g_pool, static_cast<uint32_t>(s.dead_count),
                           s.dead_sets);
    g_sets_live -= s.dead_count;
    s.dead_count = 0;
  }
  s.upload_open = false;
  // The stress test: the next N textures, in turn, are read again at their
  // next use.
  if (seed->reread > 0 && g_texture_count > 0) {
    AcquireSRWLockExclusive(&g_texture_lock);
    for (int i = 0; i < seed->reread && i < g_texture_count; ++i) {
      g_textures[g_reread_next % g_texture_count].dirty = true;
      g_reread_next = (g_reread_next + 1) % g_texture_count;
    }
    ReleaseSRWLockExclusive(&g_texture_lock);
  }
  g_walk_upload_queue = seed->upload_queue;
  s.used = 0;
  f.vkResetCommandPool(device, s.pool, 0);
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  f.vkBeginCommandBuffer(s.cb, &begin);
  if (s.query != VK_NULL_HANDLE) {
    f.vkCmdResetQueryPool(s.cb, s.query, 0, 2);
    f.vkCmdWriteTimestamp2(s.cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           s.query, 0);
  }
  if (g_walk_right) {
    // Layer 0 has the left eye: the barrier keeps it.
    vk::Barrier(s.cb, Target().image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);
    vk::Barrier(s.cb, Target().image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1, 1);
  } else {
    vk::Barrier(s.cb, Target().image, VK_IMAGE_ASPECT_COLOR_BIT,
                g_walk_own ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                           : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 2);
  }
  vk::Barrier(s.cb, g_depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 1, 2);
  g_walk_mres = PrepareMres(seed);
  g_walk_fovea = !g_walk_mres && PrepareFovea(s, seed);
  g_walk_show = (g_walk_fovea || g_walk_mres) && seed->fovea_show;
  g_walk_gaze[0] = seed->gaze[0];
  g_walk_gaze[1] = seed->gaze[1];
  g_walk_fovea_params = seed->fovea;
  g_walk_follow = seed->follow;
  // In direct mode the walks are on the frame thread, so the key is read
  // here.
  g_walk_keep = g_keep_picture && g_direct && g_walk_follow && !g_walk_right &&
                !g_walk_mres && seed->intro_eye < 0;
  g_eye_target = seed->target;
  g_draw_target = seed->target;
  g_walk_panel = seed->panel;
  g_walk_stage1 = seed->stage1;
  g_draw_panel = false;
  g_panel_drawn = false;
  g_rendering_panel = false;
  if (g_walk_mres) {
    // The draws of the eyes are records until the end of the walk.
    if (g_walk_right) ++g_pair_walks;
  } else if (g_walk_right) {
    // A clear of layer 1 alone, then the rendering of both layers with the
    // pixels of layer 0. The draws of the walk go to layer 1 alone (the
    // eyes of the push constants).
    BeginRendering(s.cb, 2u, false, true);
    f.vkCmdEndRendering(s.cb);
    BeginRendering(s.cb, 3u, true, g_walk_follow);
    ++g_pair_walks;
  } else if (g_walk_own) {
    // The colors of the target stay. The depth starts at the far plane: its
    // image has no pixels of the walk before (the barrier above).
    BeginRendering(s.cb, 3u, true, true);
    VkClearAttachment depth = {};
    depth.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth.clearValue.depthStencil.depth = 1.0f;
    VkClearRect rect = {};
    rect.rect.extent = {Target().width, Target().height};
    rect.layerCount = 1;
    f.vkCmdClearAttachments(s.cb, 1, &depth, 1, &rect);
    ++g_own_walks;
    static bool s_said = false;
    if (!s_said) {
      s_said = true;
      ModLog("vk scene     : the intro in stereo: the walk of eye %d draws "
             "over the last picture of that eye, in target %d", g_target,
             g_target);
    }
  } else {
    BeginRendering(s.cb, 3u, false, g_walk_follow);
  }
  if (g_walk_follow) {
    SetViewportState(s.cb);
  } else {
    const VkViewport viewport = {0.0f, 0.0f,
                                 static_cast<float>(Target().width),
                                 static_cast<float>(Target().height), 0.0f,
                                 1.0f};
    f.vkCmdSetViewport(s.cb, 0, 1, &viewport);
    const VkRect2D scissor = {{0, 0}, {Target().width, Target().height}};
    f.vkCmdSetScissor(s.cb, 0, 1, &scissor);
  }
  g_recording = true;
  g_picture = seed->picture;
  g_picture_frame = seed->frame;
  ++g_walks;
  ++g_walk_number;
  g_walk_draws_start = g_draws;
  // Without Direct3D 9 only when the renderer follows the target and has
  // the panel, so each draw has its place.
  g_walk_only = seed->only && g_walk_follow && g_walk_panel;
  if (g_walk_only) ++g_only_walks;
}

// THE MARKS OF THE TEST (foveation_show): at the end of a walk with the map,
// a blue colour over the part with 2 by 2 pixels for one shade, and a red one
// over the part with 4 by 4 (blue too when the GPU has no 4 by 4). Each mark
// is a ring of triangles between two edges of the cone around the centre of
// the fovea (foveation_math::ConePoint). The draw goes through VkSceneDraw
// with the states of a flat colour, and the states of the walk come back
// after it.
void DrawFoveaMarks() {
  const foveation_math::Params& p = g_walk_fovea_params;
  const uint32_t width = Target().width, height = Target().height;
  float cx = 0.0f, cy = 0.0f;
  foveation_math::TangentPixel(g_walk_gaze[0], g_walk_gaze[1], width, height,
                               p, &cx, &cy);
  const int kEdge = 64;
  // The vertices of D3DFVF_XYZRHW | DIFFUSE | SPECULAR | TEX1 (0x1C4).
  struct Vertex {
    float x, y, z, rhw;
    uint32_t diffuse, specular;
    float u, v;
  };
  static Vertex vertices[2 * kEdge * 6];
  int count = 0;
  const float far_out = 4.0f * static_cast<float>(width + height);
  for (int ring = 0; ring < 2; ++ring) {
    const float from_deg = ring == 0 ? p.inner_deg : p.outer_deg;
    const uint32_t colour =
        ring == 1 && p.quarter ? 0x50FF3030u : 0x503060FFu;  // ARGB
    for (int k = 0; k < kEdge; ++k) {
      float a[2][2], b[2][2];  // the inner and the outer point of k and k+1
      bool ok = true;
      for (int j = 0; j < 2; ++j) {
        float tx = 0.0f, ty = 0.0f;
        if (!foveation_math::ConePoint(g_walk_gaze[0], g_walk_gaze[1],
                                       from_deg, k + j, kEdge, &tx, &ty)) {
          ok = false;
          break;
        }
        foveation_math::TangentPixel(tx, ty, width, height, p, &a[j][0],
                                     &a[j][1]);
        if (ring == 0) {
          if (!foveation_math::ConePoint(g_walk_gaze[0], g_walk_gaze[1],
                                         p.outer_deg, k + j, kEdge, &tx,
                                         &ty)) {
            ok = false;
            break;
          }
          foveation_math::TangentPixel(tx, ty, width, height, p, &b[j][0],
                                       &b[j][1]);
        } else {
          // Far past each edge of the picture, on the line from the centre.
          const float dx = a[j][0] - cx, dy = a[j][1] - cy;
          const float l = sqrtf(dx * dx + dy * dy);
          if (l < 1.0f) {
            ok = false;
            break;
          }
          b[j][0] = cx + dx / l * far_out;
          b[j][1] = cy + dy / l * far_out;
        }
      }
      if (!ok) continue;
      const float* corner[6] = {a[0], b[0], a[1], b[0], b[1], a[1]};
      for (int i = 0; i < 6; ++i) {
        Vertex& v = vertices[count++];
        v.x = corner[i][0];
        v.y = corner[i][1];
        v.z = 0.0f;
        v.rhw = 1.0f;
        v.diffuse = colour;
        v.specular = 0;
        v.u = v.v = 0.0f;
      }
    }
  }
  if (count == 0) return;
  UsePanel(false);
  uint32_t rs[kStates];
  uint32_t ts[kStageStates];
  memcpy(rs, g_rs, sizeof(rs));
  memcpy(ts, g_ts[0], sizeof(ts));
  void* texture = g_texture;
  const D3DVIEWPORT7 viewport = g_viewport;
  void* draw_target = g_draw_target;
  const bool draw_panel = g_draw_panel;
  g_rs[D3DRENDERSTATE_ZENABLE] = 0;
  g_rs[D3DRENDERSTATE_ZWRITEENABLE] = 0;
  g_rs[D3DRENDERSTATE_ALPHABLENDENABLE] = 1;
  g_rs[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_SRCALPHA;
  g_rs[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_INVSRCALPHA;
  g_rs[D3DRENDERSTATE_CULLMODE] = D3DCULL_NONE;
  g_rs[D3DRENDERSTATE_ALPHATESTENABLE] = 0;
  g_rs[D3DRENDERSTATE_FOGENABLE] = 0;
  g_rs[D3DRENDERSTATE_SPECULARENABLE] = 0;
  g_ts[0][D3DTSS_COLOROP] = D3DTOP_SELECTARG1;
  g_ts[0][D3DTSS_COLORARG1] = D3DTA_DIFFUSE;
  g_ts[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
  g_ts[0][D3DTSS_ALPHAARG1] = D3DTA_DIFFUSE;
  g_texture = nullptr;
  g_draw_target = g_eye_target;
  g_draw_panel = false;
  g_viewport.dwX = 0;
  g_viewport.dwY = 0;
  g_viewport.dwWidth = width;
  g_viewport.dwHeight = height;
  g_viewport.dvMinZ = 0.0f;
  g_viewport.dvMaxZ = 1.0f;
  SetViewportState(g_slots[g_slot].cb);
  VkSceneDraw(D3DPT_TRIANGLELIST, 0x1C4, vertices,
              static_cast<uint32_t>(count), nullptr, 0);
  memcpy(g_rs, rs, sizeof(rs));
  memcpy(g_ts[0], ts, sizeof(ts));
  g_texture = texture;
  g_viewport = viewport;
  g_draw_target = draw_target;
  g_draw_panel = draw_panel;
  ++g_fovea_marks;
}

void EndOnWorker(const void*) {
  g_walk_only = false;
  if (!g_recording) return;
  if (g_walk_show && g_walk_fovea) DrawFoveaMarks();
  // A walk with no draw and no clear for the eyes keeps the picture too.
  KeepPicture();
  g_recording = false;
  Slot& s = g_slots[g_slot];
  if (g_walk_mres) {
    if (g_rendering_panel) f.vkCmdEndRendering(s.cb);
    g_rendering_panel = false;
    PlayMres(s);
  } else {
    f.vkCmdEndRendering(s.cb);
  }
  BlackCheckWrite(s);
  if (s.query != VK_NULL_HANDLE) {
    f.vkCmdWriteTimestamp2(s.cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           s.query, 1);
    s.query_written = true;
  }
  f.vkEndCommandBuffer(s.cb);
  // THE UPLOADS OF A WALK go first, so each copy is done before a draw
  // reads its texture.
  VkCommandBufferSubmitInfo cb_info[2] = {
      {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO},
      {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}};
  uint32_t cb_count = 0;
  if (s.upload_open) {
    f.vkEndCommandBuffer(s.upload_cb);
    cb_info[cb_count++].commandBuffer = s.upload_cb;
  }
  cb_info[cb_count++].commandBuffer = s.cb;
  VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = cb_count;
  submit.pCommandBufferInfos = cb_info;
  vk::LockQueue();
  const VkResult submitted =
      f.vkQueueSubmit2(vk::Get().queue, 1, &submit, s.fence);
  vk::UnlockQueue();
  if (submitted != VK_SUCCESS) {
    Fail("a submit");
    return;
  }
  g_target_drawn[g_target] = true;
  g_walk_keep = false;
  if (g_walk_both || g_walk_right) {
    g_done_image = (uint64_t)Target().image;  // a handle of 64 bits
    g_done_width = Target().width;
    g_done_height = Target().height;
    InterlockedIncrement(&g_done_serial);
  }
  const LONG walk = InterlockedIncrement(&g_walk_serial);
  if (g_direct) {
    g_last_image = (uint64_t)Target().image;  // a handle of 64 bits
    g_last_layer = g_walk_right ? 1u : 0u;
    g_last_width = Target().width;
    g_last_height = Target().height;
    g_cpu_newer = false;
  }
  // A walk of the left eye can have the walk of the right eye after it.
  const bool left = !g_walk_both && !g_walk_right && StereoPassEye() == 0;
  g_left_serial = left ? walk : -1;
  g_left_image = left ? Target().image : VK_NULL_HANDLE;
  // The picture of the window: the last walk of the left eye, or of a scene
  // with no eye pass (a menu).
  if (StereoPassEye() <= 0) {
    g_present_index = g_target;
    g_present_ready = true;
  }
  if (g_panel_drawn) {
    g_panel_index = g_target;
    InterlockedExchange(&g_panel_walk, walk);
    ++g_panel_walks;
  }
  if (g_picture) {
    f.vkWaitForFences(vk::Get().device, 1, &s.fence, VK_TRUE, UINT64_MAX);
    WritePicture(Target(), 0, "left", g_picture_frame);
    WritePicture(Target(), 1, "right", g_picture_frame);
    if (g_panel_drawn) {
      WritePicture(g_panels[g_target], 0, "panel", g_picture_frame);
    }
    // FOVEATED RESOLUTION: the two images of the left eye too.
    if (g_walk_mres) {
      WritePicture(g_mres_color[0], 0, "outer", g_picture_frame,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      WritePicture(g_mres_color[1], 0, "centre", g_picture_frame,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }
}

}  // namespace

void VkSceneSetOptions(int mode, int test_frame) {
  g_mode = mode == 1 ? 1 : 0;
  g_test_frame = test_frame;
  if (g_mode != 0) {
    ModLog("vk scene     : vk_scene=1. With d3d7_thread=1 a Vulkan renderer "
           "draws each walk into an image of its own, with a layer for each "
           "eye. vk_scene_test_frame=%d", test_frame);
  }
}

namespace {
bool g_eyes = false;
bool g_said_size = false;
uint64_t g_eye_frames = 0;  // for the report
}  // namespace

void VkSceneSetEyes(bool on) {
  g_eyes = on;
  if (on) {
    ModLog("vk scene     : vk_eyes=1. In a frame of one walk the eye pictures "
           "go from the Vulkan renderer to the headset (xr_vulkan=1)");
  }
}

// The images of the renderer go to a session only when the renderer draws
// with the device of the session (vk::UseExternal): an image of another
// device is not an image of the session.
bool SameDeviceAsSession() { return vk::Started() && vk::Get().external; }

bool VkSceneEyesWanted() {
  return g_eyes && VkSceneOn() && SameDeviceAsSession();
}

void VkSceneEyesToggle(const char* keys) {
  g_eyes = !g_eyes;
  ControllersGesturePulse(0.5f, g_eyes ? 0.30f : 0.08f);
  ModLog("test key     : %s, vk_eyes is %d now: the eye pictures come from %s",
         keys, g_eyes ? 1 : 0,
         g_eyes ? "the Vulkan renderer" : "Direct3D 9");
}

bool VkSceneEyesFor(uint32_t width, uint32_t height, uint64_t* image) {
  uint32_t w = 0, h = 0;
  if (!VkSceneEyes(image, &w, &h)) return false;
  if (w != width || h != height) {
    if (!g_said_size) {
      g_said_size = true;
      ModLog("vk scene     : the walk is %u by %u and the swapchains are %u by "
             "%u, so the eye pictures come from Direct3D 9",
             w, h, width, height);
    }
    // Direct3D 9 draws the next walks again, until a walk has the size of
    // the swapchains.
    g_size_differs = true;
    return false;
  }
  g_size_differs = false;
  ++g_eye_frames;
  return true;
}

bool VkSceneEyes(uint64_t* image, uint32_t* width, uint32_t* height) {
  if (!VkSceneOn()) return false;
  const LONG serial = InterlockedCompareExchange(&g_done_serial, 0, 0);
  if (serial == 0 || serial == g_taken_serial) return false;
  g_taken_serial = serial;
  *image = g_done_image;
  *width = g_done_width;
  *height = g_done_height;
  return *image != 0;
}

bool VkSceneOn() { return g_mode != 0 && !g_failed; }

void VkSceneWalkBegin(void* device7) {
  if (!VkSceneOn() || device7 == nullptr) return;
  IDirect3DDevice7* d = static_cast<IDirect3DDevice7*>(device7);
  Seed seed;
  memset(&seed, 0, sizeof(seed));
  for (int i = 1; i < kStates; ++i) {
    DWORD v = 0;
    if (SUCCEEDED(d->GetRenderState(static_cast<D3DRENDERSTATETYPE>(i), &v))) {
      seed.rs[i] = v;
    }
  }
  for (int stage = 0; stage < 2; ++stage) {
    for (int i = 1; i < kStageStates; ++i) {
      DWORD v = 0;
      if (SUCCEEDED(d->GetTextureStageState(
              stage, static_cast<D3DTEXTURESTAGESTATETYPE>(i), &v))) {
        seed.ts[stage][i] = v;
      }
    }
  }
  IDirectDrawSurface7* texture = nullptr;
  if (SUCCEEDED(d->GetTexture(0, &texture)) && texture != nullptr) {
    seed.texture = texture;
    texture->Release();
  }
  texture = nullptr;
  if (SUCCEEDED(d->GetTexture(1, &texture)) && texture != nullptr) {
    seed.texture1 = texture;
    texture->Release();
  }
  d->GetViewport(&seed.viewport);
  // The first walk of the frame of vk_scene_test_frame writes its images. In
  // a frame of two walks it is the walk of the right eye, which has both.
  seed.frame = g_picture_name;
  seed.picture = g_picture_next && (OneWalkActive() || StereoPassEye() != 0);
  if (seed.picture) g_picture_next = false;
  seed.both = OneWalkActive();
  seed.follow = g_follow;
  seed.panel = g_panel_mode;
  seed.stage1 = g_stage1_mode;
  seed.upload_queue = g_upload_queue;
  seed.reread = g_reread_test;
  seed.only = g_only_mode && g_only_frame && g_panel_mode && g_follow &&
              !g_size_differs && OneWalkActive();
  g_only_frame = false;
  seed.target = g_follow ? OneWalkBoundTarget() : nullptr;
  seed.eye = StereoPassEye();
  seed.intro_eye = g_direct ? IntroPassEye() : -1;
  // FOVEATED RENDERING: the angles of the picture from the camera of the
  // game, and the centre of the fovea: the gaze, or straight ahead.
  // FOVEATED RESOLUTION needs the angles and the gaze too.
  if (g_fovea_level > 0 || g_mres_mode) {
    if (g_fovea_level > 0) {
      const foveation_math::Level level =
          foveation_math::LevelOf(g_fovea_level);
      seed.fovea.inner_deg =
          g_fovea_inner_ini > 0.0f ? g_fovea_inner_ini : level.inner_deg;
      seed.fovea.outer_deg =
          g_fovea_outer_ini > 0.0f ? g_fovea_outer_ini : level.outer_deg;
    }
    seed.fovea.quarter = vk::Get().rate_quarter;
    const bw::tech::InfoTransform* info =
        reinterpret_cast<const bw::tech::InfoTransform*>(
            bw::tech::kInfoTransform);
    if (info->inv_half_tan_fov_x > 0.0f && info->res_x > 0 &&
        info->res_y > 0) {
      seed.fovea.tan_h = 1.0f / info->inv_half_tan_fov_x;
      seed.fovea.tan_v = seed.fovea.tan_h * static_cast<float>(info->res_y) /
                         static_cast<float>(info->res_x);
    }
    seed.fovea_show = g_fovea_show;
    seed.gaze[0] = g_gaze_valid ? g_gaze_now[0] : 0.0f;
    seed.gaze[1] = g_gaze_valid ? g_gaze_now[1] : 0.0f;
    seed.mres = g_mres_mode;
    seed.mres_outer = g_mres_outer;
    seed.mres_center = g_mres_center;
    seed.mres_deg = g_mres_deg;
  }
  if (g_direct) {
    BeginOnWorker(&seed);
  } else {
    D3D7ThreadPost(&BeginOnWorker, &seed, sizeof(seed));
  }
}

void VkSceneFrame(uint64_t frame, uint64_t name, bool capture) {
  if (!VkSceneOn()) return;
  // The walks of the frames before this one are over (D3D7ThreadWalk).
  g_frame_walks = InterlockedCompareExchange(&g_walk_serial, 0, 0);
  g_picture_next =
      capture ||
      (g_test_frame > 0 && frame == static_cast<uint64_t>(g_test_frame));
  g_picture_name = name;
}

void VkSceneSetFollow(int on) {
  g_follow = on != 0;
  if (g_mode == 0) return;
  ModLog("vk scene     : vk_scene_follow=%d. %s", g_follow ? 1 : 0,
         g_follow ? "In a walk of one walk the renderer follows render target "
                    "0 of Direct3D 9, the viewport of Direct3D 7 and the "
                    "clears: a draw for another target does not go into the "
                    "eye pictures"
                  : "Each draw of a walk goes into the eye pictures, in the "
                    "viewport of the start of the walk, and the renderer "
                    "makes no clear");
}

void VkSceneFollowToggle(const char* keys) {
  g_follow = !g_follow;
  ControllersGesturePulse(0.5f, g_follow ? 0.30f : 0.08f);
  ModLog("test key     : %s, vk_scene_follow is %d from the next walk", keys,
         g_follow ? 1 : 0);
}

void VkSceneTarget(void* surface) {
  if (!VkSceneOn() || (!g_direct && !D3D7ThreadActive())) return;
  TargetArgs a = {surface, GpuBridgeIsPanel(surface) ? 1u : 0u};
  Deliver(&TargetOnWorker, &a, sizeof(a));
}

void VkScenePanelBind(bool panel) {
  if (!VkSceneOn() || !g_direct) return;
  TargetArgs a = {nullptr, panel ? 1u : 0u};
  Deliver(&TargetOnWorker, &a, sizeof(a));
}

void VkSceneViewport(const void* viewport7) {
  if (!VkSceneOn() || (!g_direct && !D3D7ThreadActive()) ||
      viewport7 == nullptr) {
    return;
  }
  Deliver(&ViewportOnWorker, viewport7, sizeof(D3DVIEWPORT7));
}

void VkSceneClear(uint32_t count, const void* rects, uint32_t flags,
                  uint32_t color, float z) {
  if (!VkSceneOn() || (!g_direct && !D3D7ThreadActive())) return;
  ClearArgs a;
  memset(&a, 0, sizeof(a));
  a.count = rects != nullptr ? count : 0;
  a.flags = flags;
  a.color = color;
  a.z = z;
  if (a.count > 0 && a.count <= kClearRects) {
    memcpy(a.rects, rects, a.count * sizeof(D3DRECT));
  }
  Deliver(&ClearOnWorker, &a, sizeof(a));
}

void VkSceneWalkEnd() {
  if (!VkSceneOn()) return;
  if (g_direct) {
    EndOnWorker(nullptr);
  } else {
    D3D7ThreadPost(&EndOnWorker, nullptr, 0);
  }
}

void VkSceneRenderState(uint32_t state, uint32_t value) {
  if (state < static_cast<uint32_t>(kStates)) g_rs[state] = value;
}

void VkSceneStageState(uint32_t stage, uint32_t type, uint32_t value) {
  if (stage < 2 && type < static_cast<uint32_t>(kStageStates)) {
    g_ts[stage][type] = value;
    // In Direct3D 7 D3DTSS_ADDRESS sets the address mode of u and of v. The
    // game sets only this state, for example wrap for the sea after clamp
    // for an earlier draw.
    if (type == D3DTSS_ADDRESS) {
      g_ts[stage][D3DTSS_ADDRESSU] = value;
      g_ts[stage][D3DTSS_ADDRESSV] = value;
    }
  }
}

void VkSceneTexture(uint32_t stage, void* surface) {
  if (stage == 0) g_texture = surface;
  if (stage == 1) g_texture1 = surface;
}

void VkSceneDraw(uint32_t type, uint32_t fvf, const void* vertices,
                 uint32_t vertex_count, const uint16_t* indices,
                 uint32_t index_count) {
  if (!g_recording) return;
  // A draw for the panel target goes into the panel (THE PANEL).
  const bool panel = ForPanel();
  if (!panel && OffTarget()) {
    ++g_skipped;
    ++g_skip_target;
    return;
  }
  if (OneWalkActive() && OneWalkModeNow() == kOneWalkRightOnly) {
    ++g_right_only;
  }
  // THE SECOND STAGE: a draw with a color operation and a texture on stage
  // 1. With no texture there the draw keeps the result of stage 0.
  const bool stage1 = g_ts[1][D3DTSS_COLOROP] > 1 && g_texture1 != nullptr;
  if (stage1) ++g_stage1;
  {
    const uint32_t* t0 = g_ts[0];
    const uint32_t* t1 = g_ts[1];
    const uint32_t key[4] = {
        (t0[D3DTSS_COLOROP] & 255) | (t0[D3DTSS_COLORARG1] & 255) << 8 |
            (t0[D3DTSS_COLORARG2] & 255) << 16 |
            (g_texture != nullptr ? 1u : 0u) << 24,
        (t0[D3DTSS_ALPHAOP] & 255) | (t0[D3DTSS_ALPHAARG1] & 255) << 8 |
            (t0[D3DTSS_ALPHAARG2] & 255) << 16,
        (t1[D3DTSS_COLOROP] & 255) | (t1[D3DTSS_COLORARG1] & 255) << 8 |
            (t1[D3DTSS_COLORARG2] & 255) << 16 |
            (g_texture1 != nullptr ? 1u : 0u) << 24,
        (g_rs[D3DRENDERSTATE_ALPHABLENDENABLE] != 0 ? 1u : 0u) |
            (g_rs[D3DRENDERSTATE_SRCBLEND] & 255) << 8 |
            (g_rs[D3DRENDERSTATE_DESTBLEND] & 255) << 16 |
            (t1[D3DTSS_ALPHAOP] & 255) << 24};
    int k = 0;
    for (; k < g_state_kind_count; ++k) {
      if (memcmp(g_state_kinds[k].key, key, sizeof(key)) == 0) break;
    }
    if (k == g_state_kind_count && k < kMaxStateKinds) {
      memcpy(g_state_kinds[k].key, key, sizeof(key));
      g_state_kinds[k].draws = 0;
      ++g_state_kind_count;
      const bool complement =
          ((key[0] >> 8) & 0x10) || ((key[0] >> 16) & 0x10) ||
          ((key[1] >> 8) & 0x10) || ((key[1] >> 16) & 0x10);
      ModLog("vk scene     : states %d: stage 0 color op %u args 0x%X 0x%X, "
             "alpha op %u args 0x%X 0x%X, %s. Stage 1 color op %u args 0x%X "
             "0x%X, alpha op %u, %s. Blend %s %u %u%s%s",
             k, key[0] & 255, (key[0] >> 8) & 255, (key[0] >> 16) & 255,
             key[1] & 255, (key[1] >> 8) & 255, (key[1] >> 16) & 255,
             (key[0] >> 24) ? "a texture" : "no texture", key[2] & 255,
             (key[2] >> 8) & 255, (key[2] >> 16) & 255, key[3] >> 24,
             (key[2] >> 24) ? "a texture" : "no texture",
             (key[3] & 1) ? "on" : "off", (key[3] >> 8) & 255,
             (key[3] >> 16) & 255,
             complement ? ". A complement input" : "",
             (key[2] & 255) > 1 && (key[2] >> 24) != 0
                 ? (g_walk_stage1 ? ". Stage 1 is on, and the renderer makes it"
                                  : ". STAGE 1 IS ON, the renderer leaves it "
                                    "out (vk_stage1=0)")
                 : "");
    }
    if (k < g_state_kind_count) ++g_state_kinds[k].draws;
  }
  if (vertices == nullptr || vertex_count == 0) return;
  VertexLayout layout = {32, 16, 20, 24, 24};
  if (fvf != 0x1C4 &&
      !LayoutOf(fvf, g_ts[0][D3DTSS_TEXCOORDINDEX] & 0xFFFF,
                g_ts[1][D3DTSS_TEXCOORDINDEX] & 0xFFFF, &layout)) {
    ++g_skipped;
    ++g_skip_format;
    bool said = false;
    for (int i = 0; i < g_said_format_count; ++i) {
      if (g_said_formats[i] == fvf) said = true;
    }
    if (!said && g_said_format_count < 8) {
      g_said_formats[g_said_format_count++] = fvf;
      ModLog("vk scene     : a draw with the vertex format 0x%X (%u vertices, "
             "%u indices, type %u) has no XYZRHW, so the renderer does not "
             "draw it",
             fvf, vertex_count, index_count, type);
    }
    return;
  }
  Slot& s = g_slots[g_slot];
  const bool with1 = stage1 && g_walk_stage1;
  const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(vertex_count) * 32;
  const VkDeviceSize uv1_bytes =
      with1 ? static_cast<VkDeviceSize>(vertex_count) * 8 : 0;
  const VkDeviceSize index_bytes = (static_cast<VkDeviceSize>(index_count) * 2 + 3) & ~3ull;
  if (s.used + vertex_bytes + uv1_bytes + index_bytes > kRingBytes) {
    ++g_skipped;
    ++g_skip_ring;
    return;
  }
  // The state of the draw.
  const uint32_t* rs = g_rs;
  const uint32_t blend =
      rs[D3DRENDERSTATE_ALPHABLENDENABLE] != 0
          ? (1u << 16) | (rs[D3DRENDERSTATE_SRCBLEND] & 255) << 8 |
                (rs[D3DRENDERSTATE_DESTBLEND] & 255)
          : 0u;
  VkPipeline pipeline = Pipeline(blend | (panel ? kPanelKey : 0u));
  if (pipeline == VK_NULL_HANDLE) {
    ++g_skipped;
    ++g_skip_state;
    return;
  }
  Texture* texture = TextureOf(g_texture);
  texture->used_walk = g_walk_number;
  const uint32_t* ts = g_ts[0];
  VkSampler sampler =
      Sampler(ts[D3DTSS_MAGFILTER], ts[D3DTSS_MINFILTER], ts[D3DTSS_MIPFILTER],
              ts[D3DTSS_ADDRESSU] != 0 ? ts[D3DTSS_ADDRESSU] : 1,
              ts[D3DTSS_ADDRESSV] != 0 ? ts[D3DTSS_ADDRESSV] : 1,
              ts[D3DTSS_MAXANISOTROPY]);
  VkDescriptorSet set = SetOf(texture, sampler);
  if (set == VK_NULL_HANDLE) {
    ++g_skipped;
    ++g_skip_state;
    return;
  }
  // THE SECOND STAGE: the texture and the sampler of stage 1. With no set,
  // or with no read of the texture, the draw keeps the result of stage 0.
  VkDescriptorSet set1 = VK_NULL_HANDLE;
  Texture* texture1 = with1 ? TextureOf(g_texture1) : g_white;
  if (texture1 != g_white) {
    texture1->used_walk = g_walk_number;
    const uint32_t* ts1 = g_ts[1];
    const VkSampler sampler1 = Sampler(
        ts1[D3DTSS_MAGFILTER], ts1[D3DTSS_MINFILTER], ts1[D3DTSS_MIPFILTER],
        ts1[D3DTSS_ADDRESSU] != 0 ? ts1[D3DTSS_ADDRESSU] : 1,
        ts1[D3DTSS_ADDRESSV] != 0 ? ts1[D3DTSS_ADDRESSV] : 1,
        ts1[D3DTSS_MAXANISOTROPY]);
    set1 = SetOf(texture1, sampler1);
  }
  // Copy the vertices and the indices into the ring, in the layout of 0x1C4
  // (THE VERTEX FORMATS).
  uint8_t* ring = static_cast<uint8_t*>(s.ring.mapped);
  const VkDeviceSize vertex_at = s.used;
  if (fvf == 0x1C4) {
    memcpy(ring + vertex_at, vertices, static_cast<size_t>(vertex_bytes));
  } else {
    ConvertVertices(static_cast<const uint8_t*>(vertices), layout,
                    vertex_count, ring + vertex_at);
    ++g_converted;
  }
  s.used += vertex_bytes;
  // The texture coordinates of stage 1, after the vertices.
  VkDeviceSize uv1_at = vertex_at;
  if (set1 != VK_NULL_HANDLE) {
    uv1_at = s.used;
    CopyUv1(static_cast<const uint8_t*>(vertices), layout, vertex_count,
            ring + uv1_at);
    s.used += uv1_bytes;
    ++g_stage1_drawn;
  }
  if (fvf != 0x1C4) vertices = ring + vertex_at;
  const VkDeviceSize index_at = s.used;
  if (index_count > 0) memcpy(ring + index_at, indices, index_count * 2);
  s.used += index_bytes;
  s.used = (s.used + 31) & ~31ull;

  // The values of the push constants.
  DrawPush push = {};
  push.viewport[0] = static_cast<float>(g_viewport.dwX);
  push.viewport[1] = static_cast<float>(g_viewport.dwY);
  push.viewport[2] = 2.0f / static_cast<float>(g_viewport.dwWidth);
  push.viewport[3] = 2.0f / static_cast<float>(g_viewport.dwHeight);
  const float range = g_viewport.dvMaxZ - g_viewport.dvMinZ;
  push.depth[0] = g_viewport.dvMinZ;
  push.depth[1] = range > 0.0f ? 1.0f / range : 1.0f;
  // The eyes of the draw and the move of the right eye (BOTH EYES).
  // The panel has one layer (view 0) and no move.
  uint32_t eyes = g_walk_right && !panel ? 2u : 1u;
  uint32_t still = 1u;
  if (OneWalkActive() && !panel) {
    const int mode = OneWalkModeNow();
    eyes = mode == kOneWalkLeftOnly ? 1u : mode == kOneWalkRightOnly ? 2u : 3u;
    if (mode == kOneWalkBoth) {
      bool all_rhw_one = true, all_far = true;
      const float* v = static_cast<const float*>(vertices);
      for (uint32_t i = 0; i < vertex_count; ++i, v += 8) {
        if (v[3] != 1.0f) all_rhw_one = false;
        if (v[2] < 1.0f) all_far = false;
      }
      still = all_rhw_one || all_far ? 1u : 0u;
      push.depth[2] = OneWalkShiftPerRhw();
    }
  }
  push.ops[0] = (ts[D3DTSS_COLOROP] & 255) | (ts[D3DTSS_COLORARG1] & 255) << 8 |
                (ts[D3DTSS_COLORARG2] & 255) << 16;
  push.ops[1] = (ts[D3DTSS_ALPHAOP] & 255) | (ts[D3DTSS_ALPHAARG1] & 255) << 8 |
                (ts[D3DTSS_ALPHAARG2] & 255) << 16;
  uint32_t flags = texture != g_white ? 1u : 0u;
  if (rs[D3DRENDERSTATE_ALPHATESTENABLE] != 0) {
    flags |= 2u | (rs[D3DRENDERSTATE_ALPHAFUNC] & 15) << 4;
  }
  if (rs[D3DRENDERSTATE_SPECULARENABLE] != 0) flags |= 0x100u;
  if (rs[D3DRENDERSTATE_FOGENABLE] != 0 &&
      rs[D3DRENDERSTATE_FOGTABLEMODE] == 0) {
    flags |= 0x200u;
  }
  push.ops[2] = flags;
  push.ops[3] = still | eyes << 8;
  if (set1 != VK_NULL_HANDLE) {
    const uint32_t* ts1 = g_ts[1];
    push.ops1[0] = (ts1[D3DTSS_COLOROP] & 255) |
                   (ts1[D3DTSS_COLORARG1] & 255) << 8 |
                   (ts1[D3DTSS_COLORARG2] & 255) << 16;
    push.ops1[1] = (ts1[D3DTSS_ALPHAOP] & 255) |
                   (ts1[D3DTSS_ALPHAARG1] & 255) << 8 |
                   (ts1[D3DTSS_ALPHAARG2] & 255) << 16;
  }
  push.alpha[0] = static_cast<float>(rs[D3DRENDERSTATE_ALPHAREF] & 255) / 255.0f;
  const uint32_t fog = rs[D3DRENDERSTATE_FOGCOLOR];
  push.fog_color[0] = static_cast<float>((fog >> 16) & 255) / 255.0f;
  push.fog_color[1] = static_cast<float>((fog >> 8) & 255) / 255.0f;
  push.fog_color[2] = static_cast<float>(fog & 255) / 255.0f;
  push.fog_color[3] = 1.0f;
  // FOVEATED RESOLUTION: a draw of the eyes is a record until the end of
  // the walk.
  if (g_walk_mres && !panel) {
    MresRecord r = {};
    r.clear = -1;
    r.pipeline = pipeline;
    r.blend = blend;
    r.set = set;
    r.set1 = set1;
    r.cull = rs[D3DRENDERSTATE_CULLMODE];
    r.zenable = rs[D3DRENDERSTATE_ZENABLE];
    r.zwrite = rs[D3DRENDERSTATE_ZWRITEENABLE];
    r.zfunc = rs[D3DRENDERSTATE_ZFUNC];
    r.type = type;
    r.push = push;
    r.vertex_at = vertex_at;
    r.uv1_at = uv1_at;
    r.index_at = index_at;
    r.vertex_count = vertex_count;
    r.index_count = index_count;
    if (g_walk_follow) {
      r.viewport[0] = static_cast<float>(g_viewport.dwX);
      r.viewport[1] = static_cast<float>(g_viewport.dwY);
      r.viewport[2] = static_cast<float>(g_viewport.dwWidth);
      r.viewport[3] = static_cast<float>(g_viewport.dwHeight);
    } else {
      r.viewport[2] = static_cast<float>(Target().width);
      r.viewport[3] = static_cast<float>(Target().height);
    }
    // The rect of the vertices, and of the right eye with its move.
    const float* v = static_cast<const float*>(vertices);
    float b[4] = {1e30f, 1e30f, -1e30f, -1e30f};
    const float shift = still == 0u ? push.depth[2] : 0.0f;
    for (uint32_t i = 0; i < vertex_count; ++i, v += 8) {
      const float x = v[0], y = v[1];
      const float xr = x + shift * v[3];
      const float lo = x < xr ? x : xr, hi = x < xr ? xr : x;
      if (lo < b[0]) b[0] = lo;
      if (y < b[1]) b[1] = y;
      if (hi > b[2]) b[2] = hi;
      if (y > b[3]) b[3] = y;
    }
    memcpy(r.bounds, b, sizeof(b));
    g_mres_records.push_back(r);
    ++g_draws;
    return;
  }

  UsePanel(panel);
  if (panel) ++g_panel_draws;
  if (!panel) KeepPicture();
  VkCommandBuffer cb = s.cb;
  f.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  BindSets(cb, set, set1);
  DrawStates(cb, rs[D3DRENDERSTATE_CULLMODE], rs[D3DRENDERSTATE_ZENABLE],
             rs[D3DRENDERSTATE_ZWRITEENABLE], rs[D3DRENDERSTATE_ZFUNC], type);
  f.vkCmdPushConstants(cb, g_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
  BindVertices(cb, s, vertex_at, uv1_at);
  if (index_count > 0) {
    f.vkCmdBindIndexBuffer(cb, s.ring.buffer, index_at, VK_INDEX_TYPE_UINT16);
    f.vkCmdDrawIndexed(cb, index_count, 1, 0, 0, 0);
  } else {
    f.vkCmdDraw(cb, vertex_count, 1, 0, 0);
  }
  ++g_draws;
}

void VkSceneSetPanel(int on) {
  g_panel_mode = on != 0;
  if (g_mode == 0) return;
  ModLog("vk scene     : vk_panel=%d. %s", g_panel_mode ? 1 : 0,
         g_panel_mode
             ? "The renderer draws the draws for the panel target into a "
               "panel of its own, and the subtitle card and the panel layer "
               "take it"
             : "The renderer does not draw the draws for the panel target, "
               "and the subtitle card and the panel layer take the panel of "
               "Direct3D 9");
}

void VkScenePanelToggle(const char* keys) {
  g_panel_mode = !g_panel_mode;
  ControllersGesturePulse(0.5f, g_panel_mode ? 0.30f : 0.08f);
  ModLog("test key     : %s, vk_panel is %d from the next walk: the subtitle "
         "card and the panel layer take the panel of %s",
         keys, g_panel_mode ? 1 : 0,
         g_panel_mode ? "the Vulkan renderer" : "Direct3D 9");
}

bool VkScenePanelOn() { return g_panel_mode && VkSceneOn() && g_ready; }

namespace {
// The panel of the last walk of this frame that drew into it, or null.
const vk::Image* PanelOfFrame() {
  if (!VkScenePanelOn()) return nullptr;
  const LONG walk = InterlockedCompareExchange(&g_panel_walk, 0, 0);
  if (walk == 0 || walk <= g_frame_walks) return nullptr;
  const vk::Image& image = g_panels[g_panel_index];
  return image.image != VK_NULL_HANDLE ? &image : nullptr;
}
}  // namespace

bool VkScenePanelFor(uint32_t width, uint32_t height, uint64_t* image) {
  if (!SameDeviceAsSession()) return false;
  const vk::Image* panel = PanelOfFrame();
  if (panel == nullptr || panel->width != width || panel->height != height) {
    return false;
  }
  // One copy into the swapchain for each walk that drew into the panel.
  const LONG walk = InterlockedCompareExchange(&g_panel_walk, 0, 0);
  if (walk == g_panel_taken) return false;
  g_panel_taken = walk;
  *image = (uint64_t)panel->image;  // a handle of 64 bits
  ++g_panel_layers;
  return true;
}

const void* VkScenePanelReadHalf(const int rect[4], int* pitch,
                                 uint32_t* width, uint32_t* height) {
  const vk::Image* panel = PanelOfFrame();
  if (panel == nullptr || g_half_locked) return nullptr;
  const uint32_t panel_w = panel->width, panel_h = panel->height;
  // As GpuBridgePanelReadHalf: even numbers, inside the panel.
  int x0 = rect[0] & ~1, y0 = rect[1] & ~1;
  int x1 = (rect[0] + rect[2] + 1) & ~1, y1 = (rect[1] + rect[3] + 1) & ~1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > static_cast<int>(panel_w & ~1u)) x1 = static_cast<int>(panel_w & ~1u);
  if (y1 > static_cast<int>(panel_h & ~1u)) y1 = static_cast<int>(panel_h & ~1u);
  if (x1 - x0 < 2 || y1 - y0 < 2) {
    ++g_panel_read_failures;
    return nullptr;
  }
  const uint32_t out_w = static_cast<uint32_t>(x1 - x0) / 2;
  const uint32_t out_h = static_cast<uint32_t>(y1 - y0) / 2;
  // One image of half the size of the panel takes each part.
  const uint32_t half_w = panel_w / 2, half_h = panel_h / 2;
  if (g_half.image == VK_NULL_HANDLE || g_half.width != half_w ||
      g_half.height != half_h || g_half.format != panel->format) {
    vk::FreeImage(&g_half);
    vk::FreeBuffer(&g_half_buffer);
    if (!vk::MakeImage(half_w, half_h, 1, panel->format,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, &g_half) ||
        !vk::MakeBuffer(static_cast<VkDeviceSize>(half_w) * half_h * 4,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                        &g_half_buffer)) {
      vk::FreeImage(&g_half);
      vk::FreeBuffer(&g_half_buffer);
      ++g_panel_read_failures;
      return nullptr;
    }
  }
  // Each pixel the mean of four: a blit with a linear filter of an even rect
  // to half its size, as the StretchRect of Direct3D 9.
  VkCommandBuffer cb = vk::BeginOnce();
  if (cb == VK_NULL_HANDLE) {
    ++g_panel_read_failures;
    return nullptr;
  }
  vk::Barrier(cb, panel->image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1);
  vk::Barrier(cb, g_half.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              1);
  VkImageBlit blit = {};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[0] = {x0, y0, 0};
  blit.srcOffsets[1] = {x1, y1, 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[0] = {0, 0, 0};
  blit.dstOffsets[1] = {static_cast<int32_t>(out_w),
                        static_cast<int32_t>(out_h), 1};
  f.vkCmdBlitImage(cb, panel->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   g_half.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);
  vk::Barrier(cb, g_half.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1);
  VkBufferImageCopy copy = {};
  copy.bufferRowLength = out_w;
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {out_w, out_h, 1};
  f.vkCmdCopyImageToBuffer(cb, g_half.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           g_half_buffer.buffer, 1, &copy);
  vk::Barrier(cb, panel->image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1);
  if (!vk::EndOnce(cb)) {
    ++g_panel_read_failures;
    return nullptr;
  }
  // The card is BGRA.
  uint8_t* pixels = static_cast<uint8_t*>(g_half_buffer.mapped);
  if (panel->format != VK_FORMAT_B8G8R8A8_UNORM) {
    const size_t count = static_cast<size_t>(out_w) * out_h;
    for (size_t i = 0; i < count; ++i) {
      const uint8_t red = pixels[i * 4];
      pixels[i * 4] = pixels[i * 4 + 2];
      pixels[i * 4 + 2] = red;
    }
  }
  g_half_locked = true;
  ++g_panel_reads;
  *pitch = static_cast<int>(out_w) * 4;
  *width = out_w;
  *height = out_h;
  return pixels;
}

void VkScenePanelReadDone() { g_half_locked = false; }

void VkSceneSetFoveation(int level, int inner_deg, int outer_deg,
                         int show) {
  g_fovea_level = level >= 0 && level <= 3 ? level : 0;
  g_fovea_show = show != 0;
  g_fovea_inner_ini = static_cast<float>(inner_deg);
  g_fovea_outer_ini = static_cast<float>(outer_deg);
  // The ring of 2 by 2 is never inside the fovea.
  if (g_fovea_inner_ini > 0.0f && g_fovea_outer_ini < g_fovea_inner_ini) {
    g_fovea_outer_ini = g_fovea_inner_ini;
  }
  if (g_fovea_level == 0) return;
  const foveation_math::Level l = foveation_math::LevelOf(g_fovea_level);
  ModLog("vk scene     : foveation=%d. The eye pictures of the Vulkan renderer "
         "have the full rate within %.0f degrees of the centre of the fovea, "
         "2 by 2 pixels for one shade within %.0f degrees, and 4 by 4 "
         "outside. The centre is the gaze of an eye tracker, or straight "
         "ahead",
         g_fovea_level, g_fovea_inner_ini > 0.0f ? g_fovea_inner_ini : l.inner_deg,
         g_fovea_outer_ini > 0.0f ? g_fovea_outer_ini : l.outer_deg);
}

void VkSceneFoveationToggle(const char* keys) {
  // On, on with the marks, off, and on again.
  static int s_last = 2;
  if (g_fovea_level == 0) {
    g_fovea_level = s_last;
    g_fovea_show = false;
  } else if (!g_fovea_show) {
    g_fovea_show = true;
  } else {
    s_last = g_fovea_level;
    g_fovea_level = 0;
    g_fovea_show = false;
  }
  ControllersGesturePulse(0.5f, g_fovea_level > 0 ? 0.30f : 0.08f);
  ModLog("test key     : %s, foveation is %d%s from the next walk", keys,
         g_fovea_level,
         g_fovea_show ? ", with the marks of the coarse parts" : "");
}

void VkSceneSetFoveaRes(int on, int outer_pct, int center_pct,
                        int inset_deg) {
  g_mres_mode = on != 0;
  g_mres_outer = static_cast<float>(
      outer_pct < 10 ? 10 : (outer_pct > 100 ? 100 : outer_pct));
  g_mres_center = static_cast<float>(
      center_pct < 25 ? 25 : (center_pct > 300 ? 300 : center_pct));
  g_mres_deg = static_cast<float>(
      inset_deg < 5 ? 5 : (inset_deg > 80 ? 80 : inset_deg));
  if (!g_mres_mode) return;
  ModLog("vk scene     : fovea_res=1. The eye pictures of the Vulkan renderer "
         "come from an outer image with %.0f percent of the pixels on each "
         "side and a centre image with %.0f percent, %.0f degrees on each "
         "side of the centre of the fovea (the gaze of an eye tracker, or "
         "straight ahead)",
         g_mres_outer, g_mres_center, g_mres_deg);
}

void VkSceneFoveaResToggle(const char* keys) {
  g_mres_mode = !g_mres_mode;
  ControllersGesturePulse(0.5f, g_mres_mode ? 0.30f : 0.08f);
  ModLog("test key     : %s, fovea_res is %d from the next walk (outer %.0f "
         "percent, centre %.0f percent, %.0f degrees)",
         keys, g_mres_mode ? 1 : 0, g_mres_outer, g_mres_center, g_mres_deg);
}

void VkSceneSetGaze(bool valid, float gx, float gy) {
  g_gaze_valid = valid;
  g_gaze_now[0] = gx;
  g_gaze_now[1] = gy;
  if (valid) ++g_gaze_frames;
}

void VkSceneSetDirect(bool on) {
  g_direct = on;
  if (on) g_mode = 1;
}

bool VkSceneDirect() { return g_direct; }

namespace {
// THE WINDOW. Two entries in turn. A present records a blit of the picture
// to the size of the window and a copy to the CPU, and submits them with no
// wait. vk_present_late=1 gives the pixels of the entry before it, which the
// GPU made one present ago, so the frame thread does not wait for the walks
// of its own frame. 0: the present waits for its own copy.
struct PresentEntry {
  vk::Image image;
  vk::Buffer buffer;
  VkCommandPool pool;
  VkCommandBuffer cb;
  VkFence fence;
  bool pending;  // submitted, and its pixels not given yet
};
PresentEntry g_present[2] = {};
int g_present_next = 0;
bool g_present_late = true;
bool g_present_made = false;
uint64_t g_present_waits = 0;      // for the report: presents that waited
uint64_t g_present_wait_ticks = 0;

bool MakePresentEntries() {
  const VkDevice device = vk::Get().device;
  for (int i = 0; i < 2; ++i) {
    PresentEntry& e = g_present[i];
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = vk::Get().queue_family;
    if (f.vkCreateCommandPool(device, &pool, nullptr, &e.pool) != VK_SUCCESS) {
      return false;
    }
    VkCommandBufferAllocateInfo cb_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cb_info.commandPool = e.pool;
    cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_info.commandBufferCount = 1;
    if (f.vkAllocateCommandBuffers(device, &cb_info, &e.cb) != VK_SUCCESS) {
      return false;
    }
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (f.vkCreateFence(device, &fence, nullptr, &e.fence) != VK_SUCCESS) {
      return false;
    }
  }
  return true;
}

// Waits for the fence of an entry, and counts a wait that took time.
void WaitPresent(PresentEntry& e) {
  const VkDevice device = vk::Get().device;
  if (f.vkGetFenceStatus(device, e.fence) == VK_SUCCESS) return;
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  f.vkWaitForFences(device, 1, &e.fence, VK_TRUE, UINT64_MAX);
  QueryPerformanceCounter(&t1);
  ++g_present_waits;
  g_present_wait_ticks += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
}
}  // namespace

void VkSceneSetTextureReuse(int on) {
  g_texture_reuse = on != 0;
  if (g_mode == 0) return;
  ModLog("vk scene     : vk_texture_reuse=%d. %s", g_texture_reuse ? 1 : 0,
         g_texture_reuse
             ? "A texture that a walk reads again goes into the image it has "
               "when it can, and its pixels into the ring of the slot"
             : "Each read of a texture makes a new image and a staging "
               "buffer");
}

void VkSceneSetPresentLate(int on) {
  g_present_late = on != 0;
  if (!g_direct) return;
  ModLog("vk scene     : vk_present_late=%d. The window shows %s",
         g_present_late ? 1 : 0,
         g_present_late ? "the picture of one present ago, and the frame "
                          "thread does not wait for the GPU"
                        : "the picture of this frame, and the frame thread "
                          "waits for the GPU");
}

void VkSceneSetKeepPicture(int on) {
  g_keep_picture = on != 0;
  if (!g_direct) return;
  ModLog("vk scene     : vk_keep_picture=%d. %s", g_keep_picture ? 1 : 0,
         g_keep_picture
             ? "A walk with no clear of the whole target before its first "
               "draw starts with the picture of the walk before it, as in "
               "Direct3D 7"
             : "Each walk starts with a black target");
}

void VkSceneSetStage1(int on) {
  g_stage1_mode = on != 0;
  if (g_mode == 0) return;
  ModLog("vk scene     : vk_stage1=%d. %s", g_stage1_mode ? 1 : 0,
         g_stage1_mode
             ? "A draw with a texture on stage 1 gets that stage, for example "
               "the light maps of the Temple"
             : "The renderer makes stage 0 alone, and a draw with a texture "
               "on stage 1 has less light than with Direct3D 9");
}

void VkSceneStage1Toggle(const char* keys) {
  g_stage1_mode = !g_stage1_mode;
  ControllersGesturePulse(0.5f, g_stage1_mode ? 0.30f : 0.08f);
  ModLog("test key     : %s, vk_stage1 is %d from the next walk", keys,
         g_stage1_mode ? 1 : 0);
}

void VkSceneSetBlackCheck(int on) {
  g_black_check = on != 0;
  if (!g_direct) return;
  ModLog("vk scene     : vk_black_check=%d. %s", g_black_check ? 1 : 0,
         g_black_check
             ? "16 by 16 points of each eye picture go to the CPU, and a "
               "black picture gives a line with what its walk drew"
             : "No check for a black eye picture");
}

void VkSceneKeepPictureToggle(const char* keys) {
  g_keep_picture = !g_keep_picture;
  ControllersGesturePulse(0.5f, g_keep_picture ? 0.30f : 0.08f);
  ModLog("test key     : %s, vk_keep_picture is %d from the next walk", keys,
         g_keep_picture ? 1 : 0);
}

const uint8_t* VkScenePresentPixels(uint32_t width, uint32_t height,
                                    int* pitch) {
  if (!g_ready || !g_present_ready || width == 0 || height == 0) {
    return nullptr;
  }
  const vk::Image& source = g_colors[g_present_index];
  if (source.image == VK_NULL_HANDLE) return nullptr;
  if (!g_present_made) {
    if (!MakePresentEntries()) {
      Fail("the entries of the window");
      return nullptr;
    }
    g_present_made = true;
  }
  const VkDevice device = vk::Get().device;
  PresentEntry& e = g_present[g_present_next];
  // The copy of two presents ago is done, or nearly.
  WaitPresent(e);
  e.pending = false;
  if (e.image.image == VK_NULL_HANDLE || e.image.width != width ||
      e.image.height != height || e.image.format != source.format) {
    vk::FreeImage(&e.image);
    vk::FreeBuffer(&e.buffer);
    if (!vk::MakeImage(width, height, 1, source.format,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, &e.image) ||
        !vk::MakeBuffer(static_cast<VkDeviceSize>(width) * height * 4,
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &e.buffer)) {
      vk::FreeImage(&e.image);
      vk::FreeBuffer(&e.buffer);
      return nullptr;
    }
  }
  f.vkResetCommandPool(device, e.pool, 0);
  VkCommandBuffer cb = e.cb;
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  f.vkBeginCommandBuffer(cb, &begin);
  vk::Barrier(cb, source.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, source.layers);
  vk::Barrier(cb, e.image.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              1);
  VkImageBlit blit = {};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {static_cast<int32_t>(source.width),
                        static_cast<int32_t>(source.height), 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[1] = {static_cast<int32_t>(width),
                        static_cast<int32_t>(height), 1};
  f.vkCmdBlitImage(cb, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   e.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_LINEAR);
  vk::Barrier(cb, e.image.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1);
  VkBufferImageCopy copy = {};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {width, height, 1};
  f.vkCmdCopyImageToBuffer(cb, e.image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           e.buffer.buffer, 1, &copy);
  vk::Barrier(cb, source.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, source.layers);
  f.vkEndCommandBuffer(cb);
  VkCommandBufferSubmitInfo cb_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cb_info.commandBuffer = cb;
  VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cb_info;
  f.vkResetFences(device, 1, &e.fence);
  vk::LockQueue();
  const VkResult submitted =
      f.vkQueueSubmit2(vk::Get().queue, 1, &submit, e.fence);
  vk::UnlockQueue();
  if (submitted != VK_SUCCESS) {
    Fail("a submit of the window");
    return nullptr;
  }
  e.pending = true;
  PresentEntry* out = &e;
  if (g_present_late) {
    PresentEntry& last = g_present[g_present_next ^ 1];
    g_present_next ^= 1;
    if (!last.pending || last.image.width != width ||
        last.image.height != height) {
      return nullptr;
    }
    out = &last;
  }
  WaitPresent(*out);
  out->pending = false;
  uint8_t* pixels = static_cast<uint8_t*>(out->buffer.mapped);
  // The window takes BGRA.
  if (source.format != VK_FORMAT_B8G8R8A8_UNORM) {
    const size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; ++i) {
      const uint8_t red = pixels[i * 4];
      pixels[i * 4] = pixels[i * 4 + 2];
      pixels[i * 4 + 2] = red;
    }
  }
  *pitch = static_cast<int>(width) * 4;
  return pixels;
}

void VkSceneSetOnly(int on, int test) {
  g_only_mode = on != 0;
  g_only_test = test != 0;
  if (g_mode == 0) return;
  ModLog("vk scene     : vk_only=%d, vk_only_test=%d. %s", g_only_mode ? 1 : 0,
         g_only_test ? 1 : 0,
         !g_only_mode
             ? "Direct3D 9 draws each walk too"
         : g_only_test
             ? "In a walk of one walk the renderer alone draws, also on the "
               "desktop: the pictures of Direct3D 9 have no scene"
             : "In a walk of one walk whose eye pictures come from the "
               "renderer, the renderer alone draws");
}

void VkSceneOnlyToggle(const char* keys) {
  g_only_mode = !g_only_mode;
  ControllersGesturePulse(0.5f, g_only_mode ? 0.30f : 0.08f);
  ModLog("test key     : %s, vk_only is %d from the next walk: %s", keys,
         g_only_mode ? 1 : 0,
         g_only_mode ? "a walk with the eyes of the renderer goes without "
                       "Direct3D 9"
                     : "Direct3D 9 draws each walk too");
}

bool VkSceneOnlyTest() { return g_only_test; }

void VkSceneSetUploads(int queue, int reread_test) {
  g_upload_queue = queue != 0;
  g_reread_test = reread_test > 0 ? reread_test : 0;
  if (g_mode == 0) return;
  ModLog("vk scene     : vk_upload_queue=%d. %s. vk_scene_reread_test=%d",
         g_upload_queue ? 1 : 0,
         g_upload_queue
             ? "A texture that a walk reads again goes to the GPU in the "
               "submit of the walk, with no wait"
             : "A texture that a walk reads again goes to the GPU at once, "
               "with a wait for the GPU",
         g_reread_test);
}

void VkSceneUploadsToggle(const char* keys) {
  g_upload_queue = !g_upload_queue;
  ControllersGesturePulse(0.5f, g_upload_queue ? 0.30f : 0.08f);
  ModLog("test key     : %s, vk_upload_queue is %d from the next walk: a "
         "texture that a walk reads again goes to the GPU %s",
         keys, g_upload_queue ? 1 : 0,
         g_upload_queue ? "in the submit of the walk, with no wait"
                        : "at once, with a wait for the GPU");
}

void VkSceneOnlyFrame(bool can) { g_only_frame = can && VkSceneOn(); }

bool VkSceneOnlyNow() { return g_walk_only; }

bool VkSceneLastPicture(uint64_t* image, uint32_t* layer, uint32_t* width,
                        uint32_t* height) {
  if (!g_direct || !VkSceneOn() || g_cpu_newer || g_last_image == 0 ||
      !SameDeviceAsSession()) {
    return false;
  }
  *image = g_last_image;
  *layer = g_last_layer;
  *width = g_last_width;
  *height = g_last_height;
  return true;
}

void VkSceneCounts(uint64_t* walks, uint64_t* draws, uint64_t* clears,
                   uint64_t* kept, uint64_t* whole) {
  *walks = g_walks;
  *draws = g_draws;
  *clears = g_clears;
  *kept = g_kept_walks;
  *whole = g_cleared_walks;
}

void VkSceneSurfaceWritten(void* surface) {
  // A write of the CPU into the render target: its pixels are the newer
  // picture.
  if (g_direct && surface != nullptr) {
    DDSCAPS2 caps = {};
    if (SUCCEEDED(static_cast<IDirectDrawSurface7*>(surface)->GetCaps(&caps)) &&
        (caps.dwCaps & DDSCAPS_3DDEVICE) != 0) {
      g_cpu_newer = true;
    }
  }
  if (!VkSceneOn() || g_textures == nullptr) return;
  AcquireSRWLockExclusive(&g_texture_lock);
  Texture* t = FindTexture(surface, false);
  if (t != nullptr) t->dirty = true;
  ReleaseSRWLockExclusive(&g_texture_lock);
}

void VkSceneReport(uint64_t frames) {
  if (g_mode == 0 || frames == 0) return;
  const double per = static_cast<double>(frames);
  ModLog("vk scene     : a frame: %.1f draws for the right eye only, %.1f "
         "draws with a texture on stage 1, %.1f of them with stage 1 "
         "(vk_stage1=%d)",
         g_right_only / per, g_stage1 / per, g_stage1_drawn / per,
         g_stage1_mode ? 1 : 0);
  g_right_only = g_stage1 = g_stage1_drawn = 0;
  ModLog("vk scene     : %llu walks, a frame: %.0f draws, %.1f not drawn "
         "(%.1f with no XYZRHW, %.1f with no room in the ring, %.1f with no "
         "pipeline or set, %.1f for another target), %.1f in another format "
         "of XYZRHW. %d textures, %llu reads of a texture (%.1f MiB), %d "
         "pipelines, %d samplers%s",
         g_walks, g_draws / per, g_skipped / per, g_skip_format / per,
         g_skip_ring / per, g_skip_state / per, g_skip_target / per,
         g_converted / per, g_texture_count, g_uploads,
         g_upload_bytes / 1048576.0, g_pipeline_count, g_sampler_count,
         g_failed ? ". The renderer stopped after a fault" : "");
  ModLog("vk scene     : vk_scene_follow=%d. A frame: %.1f binds of render "
         "target 0, %.1f viewports, %.1f clears made (%.1f of them for one "
         "eye), %.1f clears for another target",
         g_follow ? 1 : 0, g_target_calls / per, g_viewport_calls / per,
         g_clears / per, g_clears_one_eye / per, g_clears_off / per);
  {
    LARGE_INTEGER hz;
    QueryPerformanceFrequency(&hz);
    const uint64_t waited = g_uploads - g_uploads_queued;
    ModLog("vk scene     : vk_upload_queue=%d. %llu reads went in the submit "
           "of their walk, %llu waited for the GPU (%.3f ms each). %lld "
           "descriptor sets in use, %llu made since the start, room for %d, "
           "%llu that the pool did not give",
           g_upload_queue ? 1 : 0, g_uploads_queued, waited,
           waited > 0 ? g_upload_wait_ticks * 1000.0 /
                            static_cast<double>(hz.QuadPart) / waited
                      : 0.0,
           g_sets_live, g_sets_made, kMaxTextures * 8, g_set_failures);
    g_uploads_queued = g_upload_wait_ticks = 0;
    g_set_failures = 0;
  }
  g_walks = g_draws = g_skipped = g_uploads = g_upload_bytes = 0;
  g_skip_format = g_skip_ring = g_skip_state = g_converted = 0;
  g_skip_target = g_target_calls = g_viewport_calls = 0;
  g_clears = g_clears_one_eye = g_clears_off = 0;
  ModLog("vk scene     : vk_panel=%d. %llu walks drew into the panel, a "
         "frame: %.1f draws into it, %.1f clears of the panel target. %llu "
         "half size copies for the subtitle card (%llu failed), %llu panels "
         "for the panel layer",
         g_panel_mode ? 1 : 0, g_panel_walks, g_panel_draws / per,
         g_clears_panel / per, g_panel_reads, g_panel_read_failures,
         g_panel_layers);
  g_panel_walks = g_panel_draws = g_clears_panel = 0;
  ModLog("vk scene     : vk_only=%d. %llu walks without Direct3D 9",
         g_only_mode ? 1 : 0, g_only_walks);
  g_only_walks = 0;
  {
    LARGE_INTEGER hz;
    QueryPerformanceFrequency(&hz);
    ModLog("vk scene     : vk_texture_reuse=%d. %llu reads went into the image "
           "they had, %llu put their pixels into the ring of the slot. %llu "
           "walks waited for the fence of their slot, %.2f ms in all",
           g_texture_reuse ? 1 : 0, g_reused, g_ring_stages, g_fence_waits,
           g_fence_wait_ticks * 1000.0 / static_cast<double>(hz.QuadPart));
    g_reused = g_ring_stages = g_fence_waits = g_fence_wait_ticks = 0;
  }
  if (g_direct) {
    LARGE_INTEGER hz;
    QueryPerformanceFrequency(&hz);
    ModLog("vk scene     : vk_present_late=%d. %llu presents to the window "
           "waited for the GPU, %.2f ms in all",
           g_present_late ? 1 : 0, g_present_waits,
           g_present_wait_ticks * 1000.0 / static_cast<double>(hz.QuadPart));
    g_present_waits = g_present_wait_ticks = 0;
    ModLog("vk scene     : vk_keep_picture=%d. %llu walks started with the "
           "picture of the walk before, and %llu walks cleared the whole "
           "target first. %llu walks of the intro drew over the picture of "
           "their eye",
           g_keep_picture ? 1 : 0, g_kept_walks, g_cleared_walks, g_own_walks);
    g_kept_walks = g_cleared_walks = g_own_walks = 0;
    if (g_black_check) {
      ModLog("vk scene     : vk_black_check=1. %llu of %llu checked eye "
             "pictures were black", g_black_walks, g_probed_walks);
      g_black_walks = g_probed_walks = 0;
    }
  }
  if (g_gpu_walks > 0) {
    ModLog("vk scene     : the GPU took %.3f ms for a walk, in %llu walks "
           "(%llu times not used)",
           g_gpu_ticks * vk::Get().timestamp_ns * 1e-6 /
               static_cast<double>(g_gpu_walks),
           g_gpu_walks, g_gpu_odd);
    g_gpu_walks = g_gpu_ticks = g_gpu_odd = 0;
  }
  if (g_mres_walks > 0) {
    ModLog("vk scene     : fovea_res=%d. %llu walks in the outer image (%u by "
           "%u) and the centre image (%u by %u). A walk: %.0f draws in the "
           "outer image, %.0f of them in the centre image. The images were "
           "made %llu times since the start",
           g_mres_mode ? 1 : 0, g_mres_walks, g_mres.outer_w, g_mres.outer_h,
           g_mres.center_w, g_mres.center_h,
           g_mres_draws / static_cast<double>(g_mres_walks),
           g_mres_center_draws / static_cast<double>(g_mres_walks),
           g_mres_remade);
    g_mres_walks = g_mres_draws = g_mres_center_draws = 0;
  }
  if (g_fovea_level > 0 || g_fovea_walks > 0) {
    ModLog("vk scene     : foveation=%d. %llu walks with the map of the rates, "
           "%llu new maps, %llu frames with the gaze of an eye tracker, %llu "
           "walks with the marks",
           g_fovea_level, g_fovea_walks, g_fovea_maps, g_gaze_frames,
           g_fovea_marks);
    g_fovea_walks = g_fovea_maps = g_gaze_frames = g_fovea_marks = 0;
  }
  if (g_pair_walks != 0) {
    ModLog("vk scene     : %llu walks of the right eye drew into the target "
           "of the walk of the left eye before them, so the target has both "
           "eyes", g_pair_walks);
    g_pair_walks = 0;
  }
  g_panel_reads = g_panel_read_failures = g_panel_layers = 0;
  if (g_eyes) {
    ModLog("vk scene     : %llu of %llu frames had the eye pictures of the "
           "Vulkan renderer in the headset", g_eye_frames, frames);
  }
  g_eye_frames = 0;
}
