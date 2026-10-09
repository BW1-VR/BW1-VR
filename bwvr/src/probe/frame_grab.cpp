// Save a frame to disk from inside the game.
//
// Capturing the game window from the desktop is unreliable. The window has to
// be visible and in front, which fights with using the machine for anything
// else, and a DirectDraw window does not redraw for a screen capture. So the
// probe reads the render target itself, at the end of the frame, and writes a
// BMP beside runblack.exe. This works while the window is behind other
// windows, minimized, or on another monitor.
//
// Control it with a file named bwvr-shot-frames.txt beside runblack.exe, one
// frame number per line. With no file, the probe saves frames 600 and 1800.

#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../common/bw_addresses.h"

extern void ProbeLog(const char* format, ...);

namespace {

const int kMaxFrames = 16;
uint64_t g_wanted[kMaxFrames];
int g_wanted_count = 0;
char g_dir[MAX_PATH];

// A 24 bit BMP is enough, and it needs no library.
bool WriteBmp(const char* path, const uint8_t* bgr, int width, int height) {
  const int row_bytes = width * 3;
  const int padding = (4 - (row_bytes % 4)) % 4;
  const uint32_t pixel_bytes = (row_bytes + padding) * height;

  BITMAPFILEHEADER file;
  ZeroMemory(&file, sizeof(file));
  file.bfType = 0x4D42;  // "BM"
  file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  file.bfSize = file.bfOffBits + pixel_bytes;

  BITMAPINFOHEADER info;
  ZeroMemory(&info, sizeof(info));
  info.biSize = sizeof(info);
  info.biWidth = width;
  info.biHeight = height;  // positive, so the rows go bottom up
  info.biPlanes = 1;
  info.biBitCount = 24;
  info.biCompression = BI_RGB;
  info.biSizeImage = pixel_bytes;

  FILE* out = nullptr;
  if (fopen_s(&out, path, "wb") != 0 || out == nullptr) return false;
  fwrite(&file, sizeof(file), 1, out);
  fwrite(&info, sizeof(info), 1, out);

  const uint8_t zeros[3] = {0, 0, 0};
  for (int y = height - 1; y >= 0; --y) {
    fwrite(bgr + y * row_bytes, row_bytes, 1, out);
    if (padding > 0) fwrite(zeros, padding, 1, out);
  }
  fclose(out);
  return true;
}

// Count the low zero bits, so a channel mask becomes a shift.
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

// Scale a channel of any width up to 8 bits.
uint8_t Expand(uint32_t value, int bits) {
  if (bits <= 0) return 0;
  if (bits >= 8) return static_cast<uint8_t>(value >> (bits - 8));
  const int shift = 8 - bits;
  const uint8_t scaled = static_cast<uint8_t>(value << shift);
  return static_cast<uint8_t>(scaled | (scaled >> bits));
}

bool Convert(const DDSURFACEDESC2& desc, uint8_t* out) {
  const DDPIXELFORMAT& format = desc.ddpfPixelFormat;
  const int width = static_cast<int>(desc.dwWidth);
  const int height = static_cast<int>(desc.dwHeight);
  const int depth = static_cast<int>(format.dwRGBBitCount);
  const uint8_t* base = static_cast<const uint8_t*>(desc.lpSurface);

  const uint32_t r_mask = format.dwRBitMask;
  const uint32_t g_mask = format.dwGBitMask;
  const uint32_t b_mask = format.dwBBitMask;
  const int r_shift = LowBit(r_mask), r_bits = BitCount(r_mask);
  const int g_shift = LowBit(g_mask), g_bits = BitCount(g_mask);
  const int b_shift = LowBit(b_mask), b_bits = BitCount(b_mask);

  if (depth != 16 && depth != 32) return false;

  for (int y = 0; y < height; ++y) {
    const uint8_t* row = base + y * desc.lPitch;
    uint8_t* dest = out + y * width * 3;
    for (int x = 0; x < width; ++x) {
      uint32_t pixel;
      if (depth == 16) {
        pixel = reinterpret_cast<const uint16_t*>(row)[x];
      } else {
        pixel = reinterpret_cast<const uint32_t*>(row)[x];
      }
      dest[x * 3 + 0] = Expand((pixel & b_mask) >> b_shift, b_bits);
      dest[x * 3 + 1] = Expand((pixel & g_mask) >> g_shift, g_bits);
      dest[x * 3 + 2] = Expand((pixel & r_mask) >> r_shift, r_bits);
    }
  }
  return true;
}

}  // namespace

void FrameGrabLoad() {
  ZeroMemory(g_dir, sizeof(g_dir));
  GetModuleFileNameA(GetModuleHandleA(nullptr), g_dir, MAX_PATH);
  char* slash = strrchr(g_dir, '\\');
  if (slash != nullptr) *(slash + 1) = '\0';

  char path[MAX_PATH];
  lstrcpynA(path, g_dir, MAX_PATH);
  lstrcatA(path, "bwvr-shot-frames.txt");

  FILE* file = nullptr;
  if (fopen_s(&file, path, "r") == 0 && file != nullptr) {
    unsigned long frame = 0;
    while (g_wanted_count < kMaxFrames && fscanf_s(file, "%lu", &frame) == 1) {
      g_wanted[g_wanted_count++] = frame;
    }
    fclose(file);
  }
  if (g_wanted_count == 0) {
    g_wanted[g_wanted_count++] = 600;
    g_wanted[g_wanted_count++] = 1800;
  }

  char list[256];
  list[0] = '\0';
  for (int i = 0; i < g_wanted_count; ++i) {
    char one[24];
    _snprintf_s(one, sizeof(one), _TRUNCATE, "%s%llu", i ? ", " : "", g_wanted[i]);
    lstrcatA(list, one);
  }
  ProbeLog("frame grab   : will save frames %s", list);
}

namespace {
// Loading a land takes minutes, and a fixed frame number usually lands on the
// progress bar. Trigger on the scene instead: LH3DRender::DrawTriangle stays
// near zero in menus and videos, and jumps once the world is being drawn.
const uint64_t kSceneDrawCalls = 200;
uint64_t g_scene_start = 0;
bool g_scene_seen = false;
}  // namespace

void FrameGrabSave(uint64_t frame, const char* tag);

// Call at the end of the frame, before the engine flips. draw_calls is the
// running LH3DRender::DrawTriangle count.
void FrameGrabTick(uint64_t frame, uint64_t draw_calls) {
  if (!g_scene_seen && draw_calls >= kSceneDrawCalls) {
    g_scene_seen = true;
    g_scene_start = frame;
    ProbeLog("frame grab   : scene geometry started at frame %llu (%llu draws)",
             frame, draw_calls);
    FrameGrabSave(frame, "scene");
  }
  // The intro cinematic is 3D too, so one shot at the start of the scene can
  // easily miss gameplay. Take a spread instead.
  if (g_scene_seen && frame > g_scene_start &&
      ((frame - g_scene_start) % 1500) == 0 && frame < g_scene_start + 15001) {
    FrameGrabSave(frame, "scene");
  }

  bool wanted = false;
  for (int i = 0; i < g_wanted_count; ++i) {
    if (g_wanted[i] == frame) wanted = true;
  }
  if (!wanted) return;
  FrameGrabSave(frame, "fixed");
}

void FrameGrabSave(uint64_t frame, const char* tag) {
  (void)tag;

  IDirect3DDevice7** slot =
      reinterpret_cast<IDirect3DDevice7**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) {
    ProbeLog("frame grab   : frame %llu, no device", frame);
    return;
  }

  IDirectDrawSurface7* target = nullptr;
  HRESULT hr = (*slot)->GetRenderTarget(&target);
  if (FAILED(hr) || target == nullptr) {
    ProbeLog("frame grab   : frame %llu, GetRenderTarget failed 0x%08X", frame, hr);
    return;
  }

  DDSURFACEDESC2 desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.dwSize = sizeof(desc);
  hr = target->Lock(nullptr, &desc, DDLOCK_READONLY | DDLOCK_WAIT, nullptr);
  if (FAILED(hr)) {
    ProbeLog("frame grab   : frame %llu, Lock failed 0x%08X", frame, hr);
    target->Release();
    return;
  }

  const int width = static_cast<int>(desc.dwWidth);
  const int height = static_cast<int>(desc.dwHeight);
  uint8_t* pixels = static_cast<uint8_t*>(
      HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(width) * height * 3));

  bool converted = false;
  if (pixels != nullptr) converted = Convert(desc, pixels);
  const int depth = static_cast<int>(desc.ddpfPixelFormat.dwRGBBitCount);

  target->Unlock(nullptr);
  target->Release();

  if (!converted) {
    ProbeLog("frame grab   : frame %llu, cannot convert %d bit surface", frame,
             depth);
    if (pixels != nullptr) HeapFree(GetProcessHeap(), 0, pixels);
    return;
  }

  char path[MAX_PATH];
  _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbwvr-frame-%05llu.bmp", g_dir,
              frame);
  const bool ok = WriteBmp(path, pixels, width, height);
  HeapFree(GetProcessHeap(), 0, pixels);

  ProbeLog("frame grab   : frame %llu, %dx%d %d bit -> %s", frame, width, height,
           depth, ok ? path : "WRITE FAILED");
}
