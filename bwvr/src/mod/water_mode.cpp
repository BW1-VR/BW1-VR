// Choose which sea the engine draws, because one of them cannot be rolled.
//
// LH3DWater has two paths, picked by the float at 0x00C38228:
//
//   non-zero   LH3DWater::Draw 0x00879930. A screen space band of horizontal
//              scanline strips with pre-transformed vertices, whose texture
//              coordinates come from unprojecting each screen row onto the
//              plane y=0. It is the better looking sea and it assumes the
//              camera has no roll, in four separate ways: the strips are
//              horizontal, rhw is 1.0 so the texture interpolates affinely
//              along a row, one depth per row drives the distance fade, and
//              nothing rejects a negative ray-plane intersection, so a row
//              whose ray points upward samples the sea behind the camera and
//              the texture mirrors.
//
//   zero       0x0087A090. One world space quad of +/-70000 at y=0. Plain
//              geometry, so it is correct from any camera, including a rolled
//              one.
//
// A head that tilts is a camera that rolls, so head roll and the scanline sea
// cannot both be had. This is the switch between them.
//
// The value is a float in .data, read at 0x00823889 from the WaterTiling
// setting. Setting WaterTiling in the registry does NOT reach it: two captures
// either side of that change differed by 0.007 levels out of 255, so the
// registry path is not what fills this. It is written here directly, once a
// frame, because a level load may fill it again.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);

namespace {

bool g_simple = false;
bool g_said = false;
float g_original = 1.0f;
bool g_have_original = false;

}  // namespace

void WaterModeSetSimple(bool on) {
  g_simple = on;
  ModLog("water        : the %s sea, which %s roll",
         on ? "plain world space" : "scanline",
         on ? "survives" : "cannot take");
}

bool WaterModeSimple() { return g_simple; }

// Called once a frame, before the scene walk.
void WaterModeApply() {
  float* tiling = reinterpret_cast<float*>(bw::tech::kWaterTiling);
  if (IsBadWritePtr(tiling, sizeof(float))) return;
  if (!g_have_original && *tiling != 0.0f) {
    g_original = *tiling;
    g_have_original = true;
  }
  const float wanted = g_simple ? 0.0f : g_original;
  if (*tiling != wanted) {
    *tiling = wanted;
    if (!g_said) {
      g_said = true;
      ModLog("water        : tiling %.2f -> %.2f", g_original, wanted);
    }
  }
}
