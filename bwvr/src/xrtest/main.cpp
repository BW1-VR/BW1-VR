// Proves the VR output chain on its own, with no game involved.
//
// It draws a test pattern that makes three things obvious at a glance:
//
//   - a colored border, so the edges of the screen are visible,
//   - large "UP" text at the top, so the picture is clearly not upside down
//     or mirrored,
//   - a bar that sweeps left to right once a second, so judder and frame
//     pacing are visible without any measurement.
//
// Run it with the Virtual Desktop Streamer connected. If this shows a clean,
// stable screen in the headset, the 32 bit OpenXR loader, the runtime, the
// Direct3D 11 swapchain and the frame pacing all work, and anything that
// breaks later is the game side.
//
//   bwvr-xrtest [seconds] [vulkan] [relay | alone] [hz=<rate>]
//
// With the word vulkan the session is one with Vulkan (XrScreen::UseVulkan,
// xr_vulkan=1 of the mod): the runtime makes the Vulkan device, and Vulkan
// copies the pattern from the CPU into the Vulkan swapchain
// (XrScreen::SetVulkanUpload). With the word relay too, the pattern goes
// through a Direct3D 11 texture that Vulkan shares (xr_vulkan_upload=0).
// With the word alone too, the session has no Direct3D 11 device
// (XrScreen::UseD3D11, the session of backend=vulkan with vulkan_only=1).
// With hz=<rate> the test asks the headset for that refresh rate, as
// refresh_rate_hz of the mod does (XrScreen::SetRefreshRate): hz=-1 asks
// for the highest rate. The log has the rates of the headset with each
// value, and the frames a second show the pace of the runtime with no game.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../vr/xr_screen.h"

namespace {

const uint32_t kWidth = 1280;
const uint32_t kHeight = 720;

void Log(const char* format, ...) {
  char line[1024];
  va_list args;
  va_start(args, format);
  _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
  va_end(args);
  printf("%s\n", line);
  fflush(stdout);
}

inline void Pixel(uint8_t* image, uint32_t x, uint32_t y, uint8_t b, uint8_t g,
                  uint8_t r) {
  if (x >= kWidth || y >= kHeight) return;
  uint8_t* p = image + (static_cast<size_t>(y) * kWidth + x) * 4;
  p[0] = b;
  p[1] = g;
  p[2] = r;
  p[3] = 255;
}

void FillRect(uint8_t* image, int x0, int y0, int w, int h, uint8_t b,
              uint8_t g, uint8_t r) {
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) {
      if (x >= 0 && y >= 0) Pixel(image, x, y, b, g, r);
    }
  }
}

// "UP" drawn with rectangles, so there is no font dependency and no doubt
// about which way round the picture is.
void DrawUp(uint8_t* image, int x, int y, int scale) {
  const int t = scale;            // stroke thickness
  const int h = scale * 6;        // letter height
  const int w = scale * 4;        // letter width
  // U: two uprights and a base.
  FillRect(image, x, y, t, h - t, 255, 255, 255);
  FillRect(image, x + w - t, y, t, h - t, 255, 255, 255);
  FillRect(image, x, y + h - t, w, t, 255, 255, 255);
  // P: one upright, a top bar and a middle bar, with a short right side.
  const int px = x + w + scale * 2;
  FillRect(image, px, y, t, h, 255, 255, 255);
  FillRect(image, px, y, w, t, 255, 255, 255);
  FillRect(image, px, y + h / 2 - t, w, t, 255, 255, 255);
  FillRect(image, px + w - t, y, t, h / 2, 255, 255, 255);
}

void DrawPattern(uint8_t* image, double seconds) {
  // A dark blue field, so a black frame is obviously a failure.
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      const uint8_t shade = static_cast<uint8_t>(20 + (y * 40) / kHeight);
      Pixel(image, x, y, static_cast<uint8_t>(shade + 30), shade, shade / 2);
    }
  }

  // A border, 8 pixels, a different color on each edge.
  FillRect(image, 0, 0, kWidth, 8, 0, 255, 0);               // top, green
  FillRect(image, 0, kHeight - 8, kWidth, 8, 0, 0, 255);     // bottom, red
  FillRect(image, 0, 0, 8, kHeight, 255, 255, 0);            // left, cyan
  FillRect(image, kWidth - 8, 0, 8, kHeight, 255, 0, 255);   // right, magenta

  DrawUp(image, 60, 40, 14);

  // A bar that sweeps once a second. Judder shows up as a stutter in it.
  const double phase = seconds - floor(seconds);
  const int bar = static_cast<int>(phase * (kWidth - 60));
  FillRect(image, bar, kHeight - 140, 60, 60, 255, 255, 255);

  // A center cross, to check the screen is not stretched.
  FillRect(image, kWidth / 2 - 1, kHeight / 2 - 40, 3, 80, 255, 255, 255);
  FillRect(image, kWidth / 2 - 40, kHeight / 2 - 1, 80, 3, 255, 255, 255);
}

// One line for one hand. With no controller on, it says so.
void PrintHand(const bwvr::XrScreen& screen, int index) {
  const bwvr::HandInput& h = screen.hand(index);
  const char* name = index == 0 ? "left " : "right";
  if (!h.active) {
    Log("  %s hand: not active (profile \"%s\", input focus %s)", name,
        screen.HandProfile(index), screen.InputFocused() ? "yes" : "no");
    return;
  }
  Log("  %s hand: \"%s\" aim %s%s (%.2f %.2f %.2f) grip %s%s trigger %.2f "
      "squeeze %.2f stick %.2f %.2f",
      name, screen.HandProfile(index), h.aim_valid ? "valid" : "lost",
      h.aim_tracked ? " tracked" : "", h.aim_position[0], h.aim_position[1],
      h.aim_position[2], h.grip_valid ? "valid" : "lost",
      h.grip_tracked ? " tracked" : "", h.trigger, h.squeeze, h.stick[0],
      h.stick[1]);
  Log("             stick click %d, primary %d, secondary %d, menu %d, "
      "serial %llu", h.stick_click, h.primary, h.secondary, h.menu, h.serial);
}

}  // namespace

int main(int argc, char** argv) {
  int seconds_to_run = 30;
  bool vulkan = false;
  bool relay = false;
  bool alone = false;
  int hz = 0;
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "hz=", 3) == 0) {
      hz = atoi(argv[i] + 3);
    } else if (strcmp(argv[i], "vulkan") == 0) {
      vulkan = true;
    } else if (strcmp(argv[i], "relay") == 0) {
      relay = true;
    } else if (strcmp(argv[i], "alone") == 0) {
      alone = true;
    } else {
      seconds_to_run = atoi(argv[i]);
    }
  }

  Log("Black and White VR, OpenXR output test");
  Log("Showing a test pattern for %d seconds. Put the headset on.",
      seconds_to_run);
  Log("");

  bwvr::XrScreen screen;
  screen.UseVulkan(vulkan);
  screen.UseD3D11(!(vulkan && alone));
  screen.SetVulkanUpload(!relay);
  screen.SetRefreshRate(hz);
  Log("The session uses %s.",
      !vulkan ? "Direct3D 11"
      : alone ? "Vulkan with no Direct3D 11, and Vulkan copies the pattern "
                "from the CPU"
      : relay ? "Vulkan, and the pattern goes through a relay of Direct3D 11"
              : "Vulkan, and Vulkan copies the pattern from the CPU");
  if (!screen.Start(kWidth, kHeight, 1, &Log)) {
    Log("");
    Log("Could not start the VR session. Nothing was changed.");
    return 1;
  }

  uint8_t* image = new uint8_t[static_cast<size_t>(kWidth) * kHeight * 4];

  LARGE_INTEGER frequency, start, now;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);

  uint64_t frames = 0;
  double elapsed = 0.0;
  double next_hands = 1.0;
  while (elapsed < seconds_to_run) {
    QueryPerformanceCounter(&now);
    elapsed = static_cast<double>(now.QuadPart - start.QuadPart) /
              static_cast<double>(frequency.QuadPart);

    DrawPattern(image, elapsed);
    if (!screen.SubmitBgra(image, static_cast<int>(kWidth) * 4)) {
      Log("The session ended.");
      break;
    }
    ++frames;

    // Both hands, once a second.
    if (elapsed >= next_hands) {
      next_hands = elapsed + 1.0;
      PrintHand(screen, 0);
      PrintHand(screen, 1);
    }

    if ((frames % 120) == 0) {
      Log("  %llu frames, %.1f per second, %llu presented, %llu skipped",
          frames, frames / (elapsed > 0.0 ? elapsed : 1.0), screen.presented(),
          screen.skipped());
      if (screen.presented() == 0) {
        Log("  Nothing shown yet. Put the headset on and look forward.");
      }
    }
  }

  Log("");
  Log("Done. %llu frames in %.1f seconds, %.1f per second.", frames, elapsed,
      frames / (elapsed > 0.0 ? elapsed : 1.0));
  Log("%llu presented, %llu skipped by the runtime.", screen.presented(),
      screen.skipped());
  // The cost of the upload, and with Vulkan of the copies into the Vulkan
  // swapchain.
  screen.ReportUpload(frames);

  delete[] image;
  screen.Stop();
  return 0;
}
