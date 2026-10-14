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
// The renderer draws the left eye into an image of its own. The picture of
// the game stays the one of dxwrapper. vk_scene_test_frame=N writes the image
// of frame N to a file, so a desktop run compares it with the picture of
// Direct3D 9 of the same frame.
//
// WHAT IT MAKES. The game draws vertices with their place on the screen
// (0x1C4: XYZRHW, diffuse, specular, one texture). docs/backend.md, "The
// calls of DirectDraw and Direct3D 7 in a session", has the states that a
// walk sets. The state of the other states comes from the device at the
// start of each walk. The fragment shader makes texture stage 0, the alpha
// test, the specular color and the fog of the vertices (vk_scene.frag). The
// blend has a pipeline for each pair of factors. The cull mode, the depth
// test, the depth write, the depth function and the primitive type are
// dynamic states.
//
// THE TEXTURES. A texture of the game is a DirectDraw surface of dxwrapper.
// At its first use the renderer reads it with Lock, makes RGBA8 of it, and
// gives the copy its mip levels with blits. A Lock or a Blt of the game on a
// surface (draw_merge.cpp) marks it, and its next use reads it again.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d.h>
#include <ddraw.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "d3d7_thread.h"
#include "one_walk.h"
#include "vk_core.h"
#include "vk_scene.h"
#include "vk_scene_frag.h"
#include "vk_scene_vert.h"

void ModLog(const char* format, ...);
// controllers.cpp
uint64_t ControllersFrameCount();

namespace {

using vk::f;

int g_mode = 0;
int g_test_frame = 0;
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

// The seed of a walk: the frame thread reads the state, and the D3D7 thread
// takes it with the start of the walk.
struct Seed {
  uint32_t rs[kStates];
  uint32_t ts[2][kStageStates];
  void* texture;
  void* texture1;
  D3DVIEWPORT7 viewport;
  uint64_t frame;
};

// THE TARGET of the left eye.
vk::Image g_color = {};
vk::Image g_depth = {};
const VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
const VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

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

// THE FRAME. Two slots, so the CPU fills one while the GPU draws the other.
struct Slot {
  VkCommandPool pool;
  VkCommandBuffer cb;
  VkFence fence;
  vk::Buffer ring;
  VkDeviceSize used;
  bool pending;
  vk::Image garbage[64];
  int garbage_count;
};
const VkDeviceSize kRingBytes = 32u << 20;
Slot g_slots[2];
int g_slot = 0;
bool g_recording = false;
bool g_picture = false;  // this walk writes its image to a file
uint64_t g_picture_frame = 0;

// For the report.
uint64_t g_walks = 0;
uint64_t g_draws = 0;
uint64_t g_skipped = 0;  // a format that the renderer does not draw
uint64_t g_uploads = 0;
uint64_t g_upload_bytes = 0;
uint64_t g_right_only = 0;
uint64_t g_stage1 = 0;  // draws with texture stage 1 on
bool g_said_seed = false;

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
  const VkVertexInputBindingDescription binding = {
      0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
  const VkVertexInputAttributeDescription attributes[4] = {
      {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
      {1, 0, VK_FORMAT_B8G8R8A8_UNORM, 16},
      {2, 0, VK_FORMAT_B8G8R8A8_UNORM, 20},
      {3, 0, VK_FORMAT_R32G32_SFLOAT, 24}};
  VkPipelineVertexInputStateCreateInfo input = {
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  input.vertexBindingDescriptionCount = 1;
  input.pVertexBindingDescriptions = &binding;
  input.vertexAttributeDescriptionCount = 4;
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
  if (key != 0) {
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = Factor((key >> 8) & 255);
    blend.dstColorBlendFactor = Factor(key & 255);
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = blend.srcColorBlendFactor;
    blend.dstAlphaBlendFactor = blend.dstColorBlendFactor;
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
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &kColorFormat;
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
// (mag) anisotropic.
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
  info.mipmapMode = mip == 1 || mip == 0 ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                         : VK_SAMPLER_MIPMAP_MODE_LINEAR;
  info.addressModeU = address(u);
  info.addressModeV = address(v);
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.anisotropyEnable =
      (min == 3 || mag == 5) && anisotropy > 1 ? VK_TRUE : VK_FALSE;
  info.maxAnisotropy = static_cast<float>(anisotropy > 16 ? 16 : anisotropy);
  info.maxLod = mip == 0 ? 0.25f : VK_LOD_CLAMP_NONE;
  VkSampler sampler = VK_NULL_HANDLE;
  f.vkCreateSampler(vk::Get().device, &info, nullptr, &sampler);
  g_samplers[g_sampler_count].key = key;
  g_samplers[g_sampler_count].sampler = sampler;
  ++g_sampler_count;
  return sampler;
}

// The count of the bits of a mask, and the place of its lowest bit.
void MaskShape(uint32_t mask, int* shift, int* bits) {
  *shift = 0;
  *bits = 0;
  if (mask == 0) return;
  while (((mask >> *shift) & 1u) == 0) ++*shift;
  while (((mask >> (*shift + *bits)) & 1u) != 0) ++*bits;
}

uint32_t Expand(uint32_t pixel, uint32_t mask, uint32_t fallback) {
  if (mask == 0) return fallback;
  int shift, bits;
  MaskShape(mask, &shift, &bits);
  const uint32_t v = (pixel & mask) >> shift;
  const uint32_t max = (1u << bits) - 1u;
  return (v * 255u + max / 2u) / max;
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
    const bool has_alpha = (pf.dwFlags & DDPF_ALPHAPIXELS) != 0;
    for (uint32_t y = 0; y < desc.dwHeight; ++y) {
      const uint8_t* row =
          static_cast<const uint8_t*>(desc.lpSurface) + y * desc.lPitch;
      uint8_t* out = *pixels + y * desc.dwWidth * 4;
      for (uint32_t x = 0; x < desc.dwWidth; ++x) {
        uint32_t p = 0;
        if (bpp == 16) {
          p = reinterpret_cast<const uint16_t*>(row)[x];
        } else if (bpp == 32) {
          p = reinterpret_cast<const uint32_t*>(row)[x];
        } else {
          p = row[x * 3] | row[x * 3 + 1] << 8 | row[x * 3 + 2] << 16;
        }
        out[x * 4 + 0] = static_cast<uint8_t>(Expand(p, pf.dwRBitMask, 0));
        out[x * 4 + 1] = static_cast<uint8_t>(Expand(p, pf.dwGBitMask, 0));
        out[x * 4 + 2] = static_cast<uint8_t>(Expand(p, pf.dwBBitMask, 0));
        out[x * 4 + 3] = static_cast<uint8_t>(
            has_alpha ? Expand(p, pf.dwRGBAlphaBitMask, 255) : 255);
      }
    }
  }
  s->Unlock(nullptr);
  return ok;
}

// Copies RGBA8 pixels into a new image with each mip level, and frees the
// pixels. On the D3D7 thread, with a wait for the GPU.
bool Upload(uint32_t width, uint32_t height, uint8_t* pixels,
            vk::Image* image) {
  uint32_t levels = 1;
  while ((width >> levels) > 0 || (height >> levels) > 0) ++levels;
  if (!vk::MakeImage(width, height, levels, kColorFormat,
                     VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT, image)) {
    free(pixels);
    return false;
  }
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
  vk::Buffer staging;
  if (!vk::MakeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true,
                      &staging)) {
    free(pixels);
    vk::FreeImage(image);
    return false;
  }
  memcpy(staging.mapped, pixels, static_cast<size_t>(bytes));
  free(pixels);
  VkCommandBuffer cb = vk::BeginOnce();
  vk::Barrier(cb, image->image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              levels);
  VkBufferImageCopy copy = {};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {width, height, 1};
  f.vkCmdCopyBufferToImage(cb, staging.buffer, image->image,
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
  const bool ok = vk::EndOnce(cb);
  vk::FreeBuffer(&staging);
  ++g_uploads;
  g_upload_bytes += bytes;
  return ok;
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
  if (t->image.image != VK_NULL_HANDLE) Retire(&t->image);
  t->set_count = 0;
  if (!Upload(width, height, pixels, &t->image)) return g_white;
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
  if (f.vkAllocateDescriptorSets(vk::Get().device, &info, &set) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
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
  }
  return set;
}

bool MakeTarget(uint32_t width, uint32_t height) {
  if (g_color.image != VK_NULL_HANDLE && g_color.width == width &&
      g_color.height == height) {
    return true;
  }
  f.vkDeviceWaitIdle(vk::Get().device);
  vk::FreeImage(&g_color);
  vk::FreeImage(&g_depth);
  return vk::MakeImage(width, height, 1, kColorFormat,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, &g_color) &&
         vk::MakeImage(width, height, 1, kDepthFormat,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT, &g_depth);
}

bool Ready() {
  if (g_ready) return true;
  if (g_failed) return false;
  if (!vk::Start()) return Fail("the start of Vulkan");
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
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80};
  VkPipelineLayoutCreateInfo layout_info = {
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &g_set_layout;
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
    if (f.vkAllocateCommandBuffers(device, &cb_info, &s.cb) != VK_SUCCESS) {
      return Fail("a command buffer");
    }
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (f.vkCreateFence(device, &fence, nullptr, &s.fence) != VK_SUCCESS) {
      return Fail("a fence");
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

// Writes the image of the left eye to a file of 24 bits, bottom up.
void WritePicture(uint64_t frame) {
  const uint32_t w = g_color.width, h = g_color.height;
  vk::Buffer read;
  if (!vk::MakeBuffer(static_cast<VkDeviceSize>(w) * h * 4,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &read)) {
    return;
  }
  VkCommandBuffer cb = vk::BeginOnce();
  vk::Barrier(cb, g_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1);
  VkBufferImageCopy copy = {};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {w, h, 1};
  f.vkCmdCopyImageToBuffer(cb, g_color.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read.buffer,
                           1, &copy);
  vk::Barrier(cb, g_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1);
  if (!vk::EndOnce(cb)) {
    vk::FreeBuffer(&read);
    return;
  }
  char path[MAX_PATH];
  GetModuleFileNameA(GetModuleHandleA(nullptr), path, MAX_PATH);
  char* slash = strrchr(path, '\\');
  if (slash != nullptr) slash[1] = 0;
  char name[64];
  sprintf_s(name, "bwvr-vk-left-%05llu.bmp", frame);
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
    for (uint32_t y = 0; y < h; ++y) {
      const uint8_t* src = rgba + (h - 1 - y) * w * 4;
      for (uint32_t x = 0; x < w; ++x) {
        line[x * 3 + 0] = src[x * 4 + 2];
        line[x * 3 + 1] = src[x * 4 + 1];
        line[x * 3 + 2] = src[x * 4 + 0];
      }
      fwrite(line, 1, row, out);
    }
    free(line);
    fclose(out);
    ModLog("vk scene     : the image of the left eye of frame %llu is in %s",
           frame, path);
  }
  vk::FreeBuffer(&read);
}

// THE WALK, on the D3D7 thread.
void BeginOnWorker(const void* data) {
  const Seed* seed = static_cast<const Seed*>(data);
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
  Slot& s = g_slots[g_slot];
  const VkDevice device = vk::Get().device;
  f.vkWaitForFences(device, 1, &s.fence, VK_TRUE, UINT64_MAX);
  f.vkResetFences(device, 1, &s.fence);
  for (int i = 0; i < s.garbage_count; ++i) vk::FreeImage(&s.garbage[i]);
  s.garbage_count = 0;
  s.used = 0;
  f.vkResetCommandPool(device, s.pool, 0);
  VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  f.vkBeginCommandBuffer(s.cb, &begin);
  vk::Barrier(s.cb, g_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1);
  vk::Barrier(s.cb, g_depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, 1);
  VkRenderingAttachmentInfo color = {
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  color.imageView = g_color.view;
  color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingAttachmentInfo depth = {
      VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depth.imageView = g_depth.view;
  depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.clearValue.depthStencil.depth = 1.0f;
  VkRenderingInfo rendering = {VK_STRUCTURE_TYPE_RENDERING_INFO};
  rendering.renderArea = {{0, 0}, {g_color.width, g_color.height}};
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &color;
  rendering.pDepthAttachment = &depth;
  f.vkCmdBeginRendering(s.cb, &rendering);
  const VkViewport viewport = {0.0f, 0.0f,
                               static_cast<float>(g_color.width),
                               static_cast<float>(g_color.height), 0.0f, 1.0f};
  f.vkCmdSetViewport(s.cb, 0, 1, &viewport);
  const VkRect2D scissor = {{0, 0}, {g_color.width, g_color.height}};
  f.vkCmdSetScissor(s.cb, 0, 1, &scissor);
  g_recording = true;
  g_picture = g_test_frame > 0 && seed->frame == static_cast<uint64_t>(g_test_frame);
  g_picture_frame = seed->frame;
  ++g_walks;
}

void EndOnWorker(const void*) {
  if (!g_recording) return;
  g_recording = false;
  Slot& s = g_slots[g_slot];
  f.vkCmdEndRendering(s.cb);
  f.vkEndCommandBuffer(s.cb);
  VkCommandBufferSubmitInfo cb_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cb_info.commandBuffer = s.cb;
  VkSubmitInfo2 submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  submit.commandBufferInfoCount = 1;
  submit.pCommandBufferInfos = &cb_info;
  if (f.vkQueueSubmit2(vk::Get().queue, 1, &submit, s.fence) != VK_SUCCESS) {
    Fail("a submit");
    return;
  }
  if (g_picture) {
    f.vkWaitForFences(vk::Get().device, 1, &s.fence, VK_TRUE, UINT64_MAX);
    WritePicture(g_picture_frame);
  }
}

}  // namespace

void VkSceneSetOptions(int mode, int test_frame) {
  g_mode = mode == 1 ? 1 : 0;
  g_test_frame = test_frame;
  if (g_mode != 0) {
    ModLog("vk scene     : vk_scene=1. With d3d7_thread=1 a Vulkan renderer "
           "draws each walk of the left eye into an image of its own, as a "
           "test. The picture of the game stays the one of dxwrapper. "
           "vk_scene_test_frame=%d", test_frame);
  }
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
  seed.frame = ControllersFrameCount();
  D3D7ThreadPost(&BeginOnWorker, &seed, sizeof(seed));
}

void VkSceneWalkEnd() {
  if (!VkSceneOn()) return;
  D3D7ThreadPost(&EndOnWorker, nullptr, 0);
}

void VkSceneRenderState(uint32_t state, uint32_t value) {
  if (state < static_cast<uint32_t>(kStates)) g_rs[state] = value;
}

void VkSceneStageState(uint32_t stage, uint32_t type, uint32_t value) {
  if (stage < 2 && type < static_cast<uint32_t>(kStageStates)) {
    g_ts[stage][type] = value;
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
  // In one walk a batch of the interface for the right eye only does not go
  // into the image of the left eye.
  if (OneWalkModeNow() == kOneWalkRightOnly) {
    ++g_right_only;
    return;
  }
  // Texture stage 1 adds its texture to stage 0 (seed: color op 7), but
  // the game sets no texture on it in a walk, so it adds nothing.
  if (g_ts[1][D3DTSS_COLOROP] > 1 && g_texture1 != nullptr) ++g_stage1;
  if (fvf != 0x1C4 || vertices == nullptr || vertex_count == 0) {
    ++g_skipped;
    return;
  }
  Slot& s = g_slots[g_slot];
  const VkDeviceSize vertex_bytes = static_cast<VkDeviceSize>(vertex_count) * 32;
  const VkDeviceSize index_bytes = (static_cast<VkDeviceSize>(index_count) * 2 + 3) & ~3ull;
  if (s.used + vertex_bytes + index_bytes > kRingBytes) {
    ++g_skipped;
    return;
  }
  // The state of the draw.
  const uint32_t* rs = g_rs;
  const uint32_t blend =
      rs[D3DRENDERSTATE_ALPHABLENDENABLE] != 0
          ? (1u << 16) | (rs[D3DRENDERSTATE_SRCBLEND] & 255) << 8 |
                (rs[D3DRENDERSTATE_DESTBLEND] & 255)
          : 0u;
  VkPipeline pipeline = Pipeline(blend);
  if (pipeline == VK_NULL_HANDLE) {
    ++g_skipped;
    return;
  }
  Texture* texture = TextureOf(g_texture);
  const uint32_t* ts = g_ts[0];
  const uint32_t address = ts[D3DTSS_ADDRESS] != 0 ? ts[D3DTSS_ADDRESS] : 1;
  VkSampler sampler =
      Sampler(ts[D3DTSS_MAGFILTER], ts[D3DTSS_MINFILTER], ts[D3DTSS_MIPFILTER],
              ts[D3DTSS_ADDRESSU] != 0 ? ts[D3DTSS_ADDRESSU] : address,
              ts[D3DTSS_ADDRESSV] != 0 ? ts[D3DTSS_ADDRESSV] : address,
              ts[D3DTSS_MAXANISOTROPY]);
  VkDescriptorSet set = SetOf(texture, sampler);
  if (set == VK_NULL_HANDLE) {
    ++g_skipped;
    return;
  }
  // Copy the vertices and the indices into the ring.
  uint8_t* ring = static_cast<uint8_t*>(s.ring.mapped);
  const VkDeviceSize vertex_at = s.used;
  memcpy(ring + vertex_at, vertices, static_cast<size_t>(vertex_bytes));
  s.used += vertex_bytes;
  const VkDeviceSize index_at = s.used;
  if (index_count > 0) memcpy(ring + index_at, indices, index_count * 2);
  s.used += index_bytes;
  s.used = (s.used + 31) & ~31ull;

  VkCommandBuffer cb = s.cb;
  f.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  f.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0,
                            1, &set, 0, nullptr);
  // D3DCULL_: 1 none, 2 clockwise, 3 counter-clockwise. The device culls the
  // faces of that turn on the screen. y goes down on both screens.
  const uint32_t cull = rs[D3DRENDERSTATE_CULLMODE];
  f.vkCmdSetCullMode(cb, cull == 2 || cull == 3 ? VK_CULL_MODE_BACK_BIT
                                                : VK_CULL_MODE_NONE);
  f.vkCmdSetFrontFace(cb, cull == 2 ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                    : VK_FRONT_FACE_CLOCKWISE);
  const uint32_t zenable = rs[D3DRENDERSTATE_ZENABLE];
  f.vkCmdSetDepthTestEnable(cb, zenable != 0 ? VK_TRUE : VK_FALSE);
  f.vkCmdSetDepthWriteEnable(
      cb, zenable != 0 && rs[D3DRENDERSTATE_ZWRITEENABLE] != 0 ? VK_TRUE
                                                                : VK_FALSE);
  static const VkCompareOp kCompare[9] = {
      VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_NEVER,
      VK_COMPARE_OP_LESS,          VK_COMPARE_OP_EQUAL,
      VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,
      VK_COMPARE_OP_NOT_EQUAL,     VK_COMPARE_OP_GREATER_OR_EQUAL,
      VK_COMPARE_OP_ALWAYS};
  const uint32_t zfunc = rs[D3DRENDERSTATE_ZFUNC];
  f.vkCmdSetDepthCompareOp(cb, kCompare[zfunc <= 8 ? zfunc : 0]);
  // D3DPT_: 4 list, 5 strip, 6 fan.
  f.vkCmdSetPrimitiveTopology(cb,
                              type == 5   ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                              : type == 6 ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN
                                          : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  struct Push {
    float viewport[4];
    float depth[4];
    uint32_t ops[4];
    float alpha[4];
    float fog_color[4];
  } push = {};
  push.viewport[0] = static_cast<float>(g_viewport.dwX);
  push.viewport[1] = static_cast<float>(g_viewport.dwY);
  push.viewport[2] = 2.0f / static_cast<float>(g_viewport.dwWidth);
  push.viewport[3] = 2.0f / static_cast<float>(g_viewport.dwHeight);
  const float range = g_viewport.dvMaxZ - g_viewport.dvMinZ;
  push.depth[0] = g_viewport.dvMinZ;
  push.depth[1] = range > 0.0f ? 1.0f / range : 1.0f;
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
  push.alpha[0] = static_cast<float>(rs[D3DRENDERSTATE_ALPHAREF] & 255) / 255.0f;
  const uint32_t fog = rs[D3DRENDERSTATE_FOGCOLOR];
  push.fog_color[0] = static_cast<float>((fog >> 16) & 255) / 255.0f;
  push.fog_color[1] = static_cast<float>((fog >> 8) & 255) / 255.0f;
  push.fog_color[2] = static_cast<float>(fog & 255) / 255.0f;
  push.fog_color[3] = 1.0f;
  f.vkCmdPushConstants(cb, g_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
  f.vkCmdBindVertexBuffers(cb, 0, 1, &s.ring.buffer, &vertex_at);
  if (index_count > 0) {
    f.vkCmdBindIndexBuffer(cb, s.ring.buffer, index_at, VK_INDEX_TYPE_UINT16);
    f.vkCmdDrawIndexed(cb, index_count, 1, 0, 0, 0);
  } else {
    f.vkCmdDraw(cb, vertex_count, 1, 0, 0);
  }
  ++g_draws;
}

void VkSceneSurfaceWritten(void* surface) {
  if (!VkSceneOn() || g_textures == nullptr) return;
  AcquireSRWLockExclusive(&g_texture_lock);
  Texture* t = FindTexture(surface, false);
  if (t != nullptr) t->dirty = true;
  ReleaseSRWLockExclusive(&g_texture_lock);
}

void VkSceneReport(uint64_t frames) {
  if (g_mode == 0 || frames == 0) return;
  const double per = static_cast<double>(frames);
  ModLog("vk scene     : a frame: %.1f draws of the right eye only, %.1f "
         "draws with a texture on stage 1, which the renderer does not draw",
         g_right_only / per, g_stage1 / per);
  g_right_only = g_stage1 = 0;
  ModLog("vk scene     : %llu walks, a frame: %.0f draws, %.1f not drawn. "
         "%d textures, %llu reads of a texture (%.1f MiB), %d pipelines, %d "
         "samplers%s",
         g_walks, g_draws / per, g_skipped / per, g_texture_count, g_uploads,
         g_upload_bytes / 1048576.0, g_pipeline_count, g_sampler_count,
         g_failed ? ". The renderer stopped after a fault" : "");
  g_walks = g_draws = g_skipped = g_uploads = g_upload_bytes = 0;
}
