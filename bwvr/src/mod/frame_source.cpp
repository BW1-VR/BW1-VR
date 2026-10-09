// Reads the finished game frame out of DirectDraw, once per frame.
//
// The engine draws into an IDirectDrawSurface7 render target. We lock it at
// the end of the frame and copy the pixels into a plain BGRA buffer that the
// VR layer can upload. That is a CPU readback, and it is the part most likely
// to cost frame rate, so it measures itself and reports.
//
// The surface is almost always 32 bit X8R8G8B8, which is BGRA in memory.
//
// THE FAST PATH DOES NOT COPY AT ALL. It locks the surface and hands the
// pointer straight to the VR layer, which copies once into the swapchain
// staging texture. The copy through a buffer here costs a second copy, and a
// per pixel read modify write to force the alpha opaque cannot use a fast
// memcpy. Two copies of 3.7 MB per eye per frame is 15 MB a frame at 90 Hz.
//
// Forcing the alpha is not needed. The quad layer is submitted with
// layerFlags of 0, and without
// XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT the runtime ignores the
// alpha channel.
//
// The 16 bit path still converts through the buffer, because 16 bit pixels
// have to be expanded whatever happens.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdint.h>
#include <string.h>

#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);

namespace {

uint8_t* g_buffer = nullptr;
uint32_t g_buffer_width = 0;
uint32_t g_buffer_height = 0;
uint32_t g_width = 0;
uint32_t g_height = 0;
bool g_failed = false;

// The surface stays locked between FrameSourceBegin and FrameSourceEnd, so
// the VR layer can read it without a copy in between.
IDirectDrawSurface7* g_surface = nullptr;
bool g_locked = false;

// Timing, so the cost of the readback is a number rather than a guess.
LARGE_INTEGER g_frequency = {0};
uint64_t g_lock_ticks = 0;
uint64_t g_copy_ticks = 0;
uint64_t g_samples = 0;

int LowBit(uint32_t mask) {
  if (mask == 0) return 0;
  int bit = 0;
  while ((mask & 1) == 0) {
    mask >>= 1;
    ++bit;
  }
  return bit;
}

int BitCount(uint32_t mask) {
  int count = 0;
  while (mask != 0) {
    count += (mask & 1);
    mask >>= 1;
  }
  return count;
}

uint8_t Expand(uint32_t value, int bits) {
  if (bits <= 0) return 0;
  if (bits >= 8) return static_cast<uint8_t>(value >> (bits - 8));
  const int shift = 8 - bits;
  const uint8_t scaled = static_cast<uint8_t>(value << shift);
  return static_cast<uint8_t>(scaled | (scaled >> bits));
}

IDirectDrawSurface7* RenderTarget() {
  IDirect3DDevice7** slot =
      reinterpret_cast<IDirect3DDevice7**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return nullptr;
  IDirectDrawSurface7* target = nullptr;
  if (FAILED((*slot)->GetRenderTarget(&target))) return nullptr;
  return target;
}

}  // namespace

// Returns the size of the game's render target, or false if there is none yet.
bool FrameSourceSize(uint32_t* width, uint32_t* height) {
  IDirectDrawSurface7* target = RenderTarget();
  if (target == nullptr) return false;

  DDSURFACEDESC2 desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.dwSize = sizeof(desc);
  const HRESULT hr = target->GetSurfaceDesc(&desc);
  target->Release();
  if (FAILED(hr)) return false;

  *width = desc.dwWidth;
  *height = desc.dwHeight;
  return true;
}

// Lock the frame and return the pixels to upload.
//
// For a 32 bit surface this is the locked surface itself, with no copy at all.
// For 16 bit it is an expanded copy. Either way the caller must call
// FrameSourceEnd, and must not hold the pointer past that.
const void* FrameSourceBegin(int* pitch) {
  if (g_failed || g_locked) return nullptr;
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);

  IDirectDrawSurface7* target = RenderTarget();
  if (target == nullptr) return nullptr;

  LARGE_INTEGER before_lock, after_lock, after_copy;
  QueryPerformanceCounter(&before_lock);

  DDSURFACEDESC2 desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.dwSize = sizeof(desc);
  HRESULT hr = target->Lock(nullptr, &desc, DDLOCK_READONLY | DDLOCK_WAIT,
                            nullptr);
  QueryPerformanceCounter(&after_lock);

  if (FAILED(hr)) {
    target->Release();
    return nullptr;
  }

  const uint32_t width = desc.dwWidth;
  const uint32_t height = desc.dwHeight;
  const int depth = static_cast<int>(desc.ddpfPixelFormat.dwRGBBitCount);

  if (width != g_width || height != g_height) {
    g_width = width;
    g_height = height;
    ModLog("  frame source : %ux%u, %d bit", width, height, depth);
  }

  const uint8_t* base = static_cast<const uint8_t*>(desc.lpSurface);

  if (depth == 32) {
    // Nothing to do. X8R8G8B8 is already BGRA in memory, so the VR layer can
    // read the surface where it lies.
    g_surface = target;
    g_locked = true;
    QueryPerformanceCounter(&after_copy);
    g_lock_ticks += after_lock.QuadPart - before_lock.QuadPart;
    g_copy_ticks += after_copy.QuadPart - after_lock.QuadPart;
    ++g_samples;
    *pitch = static_cast<int>(desc.lPitch);
    return base;
  }

  if (g_buffer == nullptr || width != g_buffer_width ||
      height != g_buffer_height) {
    delete[] g_buffer;
    g_buffer = new uint8_t[static_cast<size_t>(width) * height * 4];
    g_buffer_width = width;
    g_buffer_height = height;
  }

  if (depth == 16) {
    const uint32_t r_mask = desc.ddpfPixelFormat.dwRBitMask;
    const uint32_t g_mask = desc.ddpfPixelFormat.dwGBitMask;
    const uint32_t b_mask = desc.ddpfPixelFormat.dwBBitMask;
    const int r_shift = LowBit(r_mask), r_bits = BitCount(r_mask);
    const int g_shift = LowBit(g_mask), g_bits = BitCount(g_mask);
    const int b_shift = LowBit(b_mask), b_bits = BitCount(b_mask);
    for (uint32_t y = 0; y < height; ++y) {
      const uint16_t* source =
          reinterpret_cast<const uint16_t*>(base + y * desc.lPitch);
      uint8_t* destination = g_buffer + static_cast<size_t>(y) * width * 4;
      for (uint32_t x = 0; x < width; ++x) {
        const uint32_t pixel = source[x];
        destination[x * 4 + 0] = Expand((pixel & b_mask) >> b_shift, b_bits);
        destination[x * 4 + 1] = Expand((pixel & g_mask) >> g_shift, g_bits);
        destination[x * 4 + 2] = Expand((pixel & r_mask) >> r_shift, r_bits);
        destination[x * 4 + 3] = 255;
      }
    }
  } else {
    ModLog("  frame source : %d bit surfaces are not supported, VR disabled",
           depth);
    g_failed = true;
    target->Unlock(nullptr);
    target->Release();
    return nullptr;
  }

  QueryPerformanceCounter(&after_copy);
  target->Unlock(nullptr);
  target->Release();

  g_lock_ticks += after_lock.QuadPart - before_lock.QuadPart;
  g_copy_ticks += after_copy.QuadPart - after_lock.QuadPart;
  ++g_samples;

  *pitch = static_cast<int>(width) * 4;
  return g_buffer;
}

// Release the lock taken by FrameSourceBegin. Safe to call when nothing is
// locked, because the 16 bit path unlocks as it goes.
void FrameSourceEnd() {
  if (!g_locked || g_surface == nullptr) return;
  g_surface->Unlock(nullptr);
  g_surface->Release();
  g_surface = nullptr;
  g_locked = false;
}

// The old shape, kept for the callers that want a buffer they can hold: the
// mono path and the eye capture. It copies, because the lock cannot stay open.
const void* FrameSourceGrab(int* pitch) {
  const void* pixels = FrameSourceBegin(pitch);
  if (pixels == nullptr) return nullptr;
  if (!g_locked) return pixels;  // the 16 bit path already copied

  const size_t row_bytes = static_cast<size_t>(g_width) * 4;
  if (g_buffer == nullptr || g_width != g_buffer_width ||
      g_height != g_buffer_height) {
    delete[] g_buffer;
    g_buffer = new uint8_t[row_bytes * g_height];
    g_buffer_width = g_width;
    g_buffer_height = g_height;
  }
  const uint8_t* source = static_cast<const uint8_t*>(pixels);
  for (uint32_t y = 0; y < g_height; ++y) {
    memcpy(g_buffer + static_cast<size_t>(y) * row_bytes,
           source + static_cast<size_t>(y) * (*pitch), row_bytes);
  }
  FrameSourceEnd();
  *pitch = static_cast<int>(row_bytes);
  return g_buffer;
}

// Average cost of the readback, in milliseconds, since the last report.
void FrameSourceReport() {
  if (g_samples == 0 || g_frequency.QuadPart == 0) return;
  const double to_ms = 1000.0 / static_cast<double>(g_frequency.QuadPart);
  ModLog("  frame source : lock %.2f ms, copy %.2f ms, average over %llu frames",
         (static_cast<double>(g_lock_ticks) / g_samples) * to_ms,
         (static_cast<double>(g_copy_ticks) / g_samples) * to_ms, g_samples);
  g_lock_ticks = 0;
  g_copy_ticks = 0;
  g_samples = 0;
}

void FrameSourceShutdown() {
  FrameSourceEnd();
  delete[] g_buffer;
  g_buffer = nullptr;
  g_buffer_width = 0;
  g_buffer_height = 0;
  g_width = 0;
  g_height = 0;
}
