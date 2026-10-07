// Save the two eye images to disk, so stereo problems can be looked at.
//
// "The tooltips are duplicated" and "the particles are at the wrong depth"
// are real reports, but they are hard to act on without seeing the two
// pictures side by side. Anything that differs between the eyes by something
// other than a horizontal shift cannot be fused, and that shows up at once in
// a difference image.
//
// Writes bwvr-eye-left-NNNNN.bmp and bwvr-eye-right-NNNNN.bmp beside
// runblack.exe. Control it with bwvr-eye-frames.txt, one stereo frame number
// per line. With no file it saves nothing.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern void ModLog(const char* format, ...);

namespace {

const int kMaxWanted = 8;
uint64_t g_wanted[kMaxWanted];
int g_wanted_count = 0;
char g_dir[MAX_PATH];
bool g_loaded = false;

bool WriteBmp(const char* path, const uint8_t* bgra, int width, int height,
              int pitch) {
  const int row_bytes = width * 3;
  const int padding = (4 - (row_bytes % 4)) % 4;
  const uint32_t pixel_bytes = (row_bytes + padding) * height;

  BITMAPFILEHEADER file;
  ZeroMemory(&file, sizeof(file));
  file.bfType = 0x4D42;
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
  uint8_t* row = new uint8_t[row_bytes];
  for (int y = height - 1; y >= 0; --y) {
    const uint8_t* source = bgra + static_cast<size_t>(y) * pitch;
    for (int x = 0; x < width; ++x) {
      row[x * 3 + 0] = source[x * 4 + 0];
      row[x * 3 + 1] = source[x * 4 + 1];
      row[x * 3 + 2] = source[x * 4 + 2];
    }
    fwrite(row, row_bytes, 1, out);
    if (padding > 0) fwrite(zeros, padding, 1, out);
  }
  delete[] row;
  fclose(out);
  return true;
}

// A 32 bit BMP that keeps the alpha. The V4 header names the four masks, so
// a reader takes the fourth byte as alpha and not as padding.
bool WriteBmpAlpha(const char* path, const uint8_t* bgra, int width,
                   int height, int pitch) {
  const uint32_t pixel_bytes = static_cast<uint32_t>(width) * 4 * height;
  BITMAPFILEHEADER file;
  ZeroMemory(&file, sizeof(file));
  file.bfType = 0x4D42;
  file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPV4HEADER);
  file.bfSize = file.bfOffBits + pixel_bytes;

  BITMAPV4HEADER info;
  ZeroMemory(&info, sizeof(info));
  info.bV4Size = sizeof(info);
  info.bV4Width = width;
  info.bV4Height = height;  // positive, so the rows go bottom up
  info.bV4Planes = 1;
  info.bV4BitCount = 32;
  info.bV4V4Compression = BI_BITFIELDS;
  info.bV4SizeImage = pixel_bytes;
  info.bV4RedMask = 0x00FF0000;
  info.bV4GreenMask = 0x0000FF00;
  info.bV4BlueMask = 0x000000FF;
  info.bV4AlphaMask = 0xFF000000;
  info.bV4CSType = LCS_sRGB;

  FILE* out = nullptr;
  if (fopen_s(&out, path, "wb") != 0 || out == nullptr) return false;
  fwrite(&file, sizeof(file), 1, out);
  fwrite(&info, sizeof(info), 1, out);
  for (int y = height - 1; y >= 0; --y) {
    fwrite(bgra + static_cast<size_t>(y) * pitch, static_cast<size_t>(width) * 4,
           1, out);
  }
  fclose(out);
  return true;
}

// The panel over a gray checkerboard, so a clear pixel and a black pixel
// look different. The panel color is premultiplied, so this is
// color + checker * (1 - alpha).
bool WriteBmpOverChecker(const char* path, const uint8_t* bgra, int width,
                         int height, int pitch) {
  const size_t row_bytes = static_cast<size_t>(width) * 4;
  uint8_t* over = new uint8_t[row_bytes * height];
  for (int y = 0; y < height; ++y) {
    const uint8_t* source = bgra + static_cast<size_t>(y) * pitch;
    uint8_t* target = over + static_cast<size_t>(y) * row_bytes;
    for (int x = 0; x < width; ++x) {
      const int checker = (((x / 32) + (y / 32)) & 1) ? 160 : 96;
      const int alpha = source[x * 4 + 3];
      for (int c = 0; c < 3; ++c) {
        int value = source[x * 4 + c] + (checker * (255 - alpha) + 127) / 255;
        target[x * 4 + c] = static_cast<uint8_t>(value > 255 ? 255 : value);
      }
      target[x * 4 + 3] = 255;
    }
  }
  const bool ok = WriteBmp(path, over, width, height,
                           static_cast<int>(row_bytes));
  delete[] over;
  return ok;
}

void Load() {
  g_loaded = true;
  ZeroMemory(g_dir, sizeof(g_dir));
  GetModuleFileNameA(GetModuleHandleA(nullptr), g_dir, MAX_PATH);
  char* slash = strrchr(g_dir, '\\');
  if (slash != nullptr) *(slash + 1) = '\0';

  char path[MAX_PATH];
  lstrcpynA(path, g_dir, MAX_PATH);
  lstrcatA(path, "bwvr-eye-frames.txt");

  FILE* file = nullptr;
  if (fopen_s(&file, path, "r") != 0 || file == nullptr) return;
  unsigned long frame = 0;
  while (g_wanted_count < kMaxWanted && fscanf_s(file, "%lu", &frame) == 1) {
    g_wanted[g_wanted_count++] = frame;
  }
  fclose(file);
  if (g_wanted_count > 0) {
    ModLog("eye capture  : armed for %d stereo frame(s)", g_wanted_count);
  }
}

}  // namespace

// A frame asked for by a keypress rather than by the frame list. Picking a
// frame number in advance cannot catch something that only appears when the
// player puts the hand over an object, so F12 (a test key) arms the next
// one. It writes a picture of each eye, and one of the frame before
// either eye pass.
uint64_t g_armed_frame = 0;

void EyeCaptureArmNow(uint64_t stereo_frame) {
  g_armed_frame = stereo_frame;
  ModLog("eye capture  : armed for stereo frame %llu", stereo_frame);
}

bool EyeCaptureWanted(uint64_t stereo_frame) {
  if (!g_loaded) Load();
  if (g_armed_frame != 0 && stereo_frame == g_armed_frame) return true;
  for (int i = 0; i < g_wanted_count; ++i) {
    if (g_wanted[i] == stereo_frame) return true;
  }
  return false;
}

// Saves a picture under a name of the caller's choosing. The GPU bridge self
// test uses this to write what came back through the shared texture, so the
// two routes can be compared as images rather than as claims.
void EyeCaptureSaveAs(const char* suffix, uint64_t stereo_frame, int eye,
                      const void* pixels, int pitch, uint32_t width,
                      uint32_t height) {
  if (pixels == nullptr) return;
  if (!g_loaded) Load();
  char path[MAX_PATH];
  _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbwvr-%s-%s-%05llu.bmp", g_dir,
              suffix, eye == 0 ? "left" : "right", stereo_frame);
  const bool ok = WriteBmp(path, static_cast<const uint8_t*>(pixels),
                           static_cast<int>(width), static_cast<int>(height),
                           pitch);
  ModLog("eye capture  : frame %llu %s eye %s -> %s", stereo_frame,
         eye == 0 ? "left" : "right", suffix, ok ? path : "WRITE FAILED");
}

// The interface panel of U1 (docs/ui-layer.md), as bwvr-NAME-NNNNN.bmp.
// style 0 keeps the alpha, 1 puts the panel over a checkerboard, and 2 is a
// plain 24 bit picture.
void EyeCaptureSaveUi(const char* name, uint64_t frame, int style,
                      const void* pixels, int pitch, uint32_t width,
                      uint32_t height) {
  if (pixels == nullptr) return;
  if (!g_loaded) Load();
  char path[MAX_PATH];
  _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbwvr-%s-%05llu.bmp", g_dir,
              name, frame);
  const uint8_t* bgra = static_cast<const uint8_t*>(pixels);
  const int w = static_cast<int>(width);
  const int h = static_cast<int>(height);
  bool ok;
  if (style == 0) {
    ok = WriteBmpAlpha(path, bgra, w, h, pitch);
  } else if (style == 1) {
    ok = WriteBmpOverChecker(path, bgra, w, h, pitch);
  } else {
    ok = WriteBmp(path, bgra, w, h, pitch);
  }
  ModLog("eye capture  : frame %llu %s -> %s", frame, name,
         ok ? path : "WRITE FAILED");
}

void EyeCaptureSave(uint64_t stereo_frame, int eye, const void* pixels,
                    int pitch, uint32_t width, uint32_t height) {
  if (pixels == nullptr) return;
  char path[MAX_PATH];
  _snprintf_s(path, sizeof(path), _TRUNCATE, "%sbwvr-eye-%s-%05llu.bmp", g_dir,
              eye == 0 ? "left" : "right", stereo_frame);
  const bool ok = WriteBmp(path, static_cast<const uint8_t*>(pixels),
                           static_cast<int>(width), static_cast<int>(height),
                           pitch);
  ModLog("eye capture  : frame %llu %s eye -> %s", stereo_frame,
         eye == 0 ? "left" : "right", ok ? path : "WRITE FAILED");
}
