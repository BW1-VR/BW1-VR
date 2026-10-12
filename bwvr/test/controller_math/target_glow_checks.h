// THE GLOW ON THE MESH OF A TARGET (docs/hand-model.md, "The glow on the mesh
// of a target"). main.cpp includes this file after Check and Near, just
// before main.
#pragma once

#include "../../src/mod/target_glow_math.h"

namespace target_glow_checks {

inline void NearAt(double got, double want, double tolerance,
                   const char* what) {
  Near(what, got, want, tolerance);
}

inline void Same(uint32_t got, uint32_t want, const char* what) {
  Check(got == want, what, static_cast<double>(got),
        static_cast<double>(want));
}

inline void TargetGlowChecks() {
  using namespace target_glow_math;
  printf("\nThe glow on the mesh of a target\n");

  // The color of the game with more light.
  Same(AddColor(0xFF000000u, 115, 86, 18), 0xFF735612u,
       "the glow on no color is the glow, with full alpha");
  Same(AddColor(0x00102030u, 1, 2, 3), 0xFF112233u,
       "each channel gets its part, and the alpha is full");
  Same(AddColor(0xFFF0F0F0u, 100, 20, 15), 0xFFFFFFFFu,
       "a channel stops at 255");
  Same(AddColor(0xFF405060u, -5, 0, 0), 0xFF405060u,
       "a part below 0 adds nothing");

  // The fade.
  NearAt(Fade(0.0f, true, 0.075f), 0.5f, 1e-5f,
         "half the fade time gives half the level");
  NearAt(Fade(0.9f, true, 0.1f), 1.0f, 1e-6f, "the level stops at 1");
  NearAt(Fade(0.2f, false, 0.1f), 0.0f, 1e-6f, "the level stops at 0");
  NearAt(Fade(0.4f, true, 0.0f), 0.4f, 1e-6f, "no time gives no change");
  // 72, 90 and 120 frames in a second come to the same level.
  bool same_rate = true;
  const int rates[3] = {72, 90, 120};
  for (int r = 0; r < 3; ++r) {
    float level = 0.0f;
    const int frames = rates[r] / 10;  // 0.1 s, less the part of a frame
    for (int i = 0; i < frames; ++i) {
      level = Fade(level, true, 1.0f / static_cast<float>(rates[r]));
    }
    const float want = static_cast<float>(frames) /
                       static_cast<float>(rates[r]) / kFadeSeconds;
    if (fabsf(level - want) > 1e-4f) same_rate = false;
  }
  Check(same_rate, "the fade has the same speed at 72, 90 and 120 Hz",
        same_rate, 1);
  NearAt(Ease(0.0f), 0.0f, 1e-6f, "the ease starts at 0");
  NearAt(Ease(0.5f), 0.5f, 1e-6f, "the ease is half at half");
  NearAt(Ease(1.0f), 1.0f, 1e-6f, "the ease ends at 1");
  Check(Ease(0.1f) < 0.1f && Ease(0.9f) > 0.9f,
        "the ease is soft at both ends", Ease(0.1f), 0.028);
  NearAt(Approach(100.0f, 255.0f, 0.03f), 151.0f, 0.01f,
         "a channel goes 51 in 30 ms");
  NearAt(Approach(250.0f, 255.0f, 0.03f), 255.0f, 1e-4f,
         "a channel stops at its new value");
  NearAt(Approach(70.0f, 40.0f, 0.03f), 40.0f, 1e-4f,
         "a channel goes down too");

  // The pulse.
  NearAt(Pulse(0.0), 1.0f, 1e-5f, "the pulse starts at 1");
  NearAt(Pulse(0.5 * kPulseSeconds), kPulseLow, 1e-5f,
         "the pulse is at its low value in the middle of a period");
  NearAt(Pulse(7.0 * kPulseSeconds), 1.0f, 1e-4f,
         "the pulse is the same after each period");
  bool pulse_in = true;
  for (int i = 0; i < 400; ++i) {
    const float p = Pulse(0.013 * i);
    if (p < kPulseLow - 1e-5f || p > 1.0f + 1e-5f) pulse_in = false;
  }
  Check(pulse_in, "the pulse stays from its low value to 1, never 0",
        pulse_in, 1);
  // A long session: the time is a double, so the phase stays exact.
  NearAt(Pulse(36000.0 + 0.5 * kPulseSeconds), kPulseLow, 1e-3f,
         "the pulse is right after 10 hours");

  // The wave.
  NearAt(Wave(0.0f, 0.0), 1.0f + kWaveDepth, 1e-5f,
         "the band starts at the base");
  NearAt(Wave(0.5f, 0.5 * kWaveSeconds), 1.0f + kWaveDepth, 1e-4f,
         "after half a period the band is at half the height: it goes up");
  const float wave_low = 1.0f - 0.6f * kWaveDepth;
  NearAt(Wave(0.5f, 0.0), wave_low, 1e-5f,
         "half a band away the wave is at its low value");
  // The bright part is narrower than the dim part: a quarter of a band away
  // the wave is below its mean.
  Check(Wave(0.25f, 0.0) < 1.0f, "the band is narrow: a quarter of a band "
                                 "away the wave is below 1",
        Wave(0.25f, 0.0), 1.0);
  double sum = 0.0;
  bool wave_in = true;
  for (int i = 0; i < 1000; ++i) {
    const float w = Wave(0.37f, kWaveSeconds * i / 1000.0);
    sum += w;
    if (w < wave_low - 1e-5f || w > 1.0f + kWaveDepth + 1e-5f) {
      wave_in = false;
    }
  }
  NearAt(sum / 1000.0, 1.0, 1e-3, "the mean of the wave over a period is 1");
  Check(wave_in && wave_low > 0.5f,
        "the wave stays between its low value and its top, never 0", wave_in,
        1);

  // A large mesh gets less.
  NearAt(SizeShare(0.0f), 1.0f, 1e-6f,
         "a mesh with no known height gets the full share");
  NearAt(SizeShare(1.5f), 1.0f, 1e-6f, "a villager gets the full share");
  NearAt(SizeShare(kSmallUnits), 1.0f, 1e-6f,
         "a mesh of 3 units gets the full share");
  NearAt(SizeShare(6.0f), 0.7071f, 1e-3f, "a mesh of 6 units gets 71 percent");
  NearAt(SizeShare(13.0f), kLargeShare, 1e-6f,
         "a tree and the Village Store get the least share");
  NearAt(SizeShare(27.0f), kLargeShare, 1e-6f,
         "a mesh of 27 units gets the least share");
  bool size_down = true;
  for (float h = 0.5f; h < 60.0f; h += 0.5f) {
    if (SizeShare(h + 0.5f) > SizeShare(h) + 1e-6f) size_down = false;
  }
  Check(size_down, "a higher mesh never gets more", size_down, 1);

  // The strength and the parts.
  NearAt(Strength(45, 1.5f, 1.0f, kAnimSteady, 0.9), 0.45f, 1e-6f,
         "a steady glow has the share of the key");
  NearAt(Strength(45, 1.5f, 1.0f, kAnimWave, 0.9), 0.45f, 1e-6f,
         "the wave alone has no pulse");
  NearAt(Strength(45, 1.5f, 1.0f, kAnimPulse, 0.5 * kPulseSeconds),
         0.45f * kPulseLow, 1e-5f, "the pulse makes the glow less, not 0");
  NearAt(Strength(45, 1.5f, 0.0f, kAnimBoth, 0.0), 0.0f, 1e-6f,
         "no level gives no glow");
  NearAt(Strength(900, 1.5f, 1.0f, kAnimSteady, 0.0), 2.0f, 1e-6f,
         "the key stops at 200 percent");
  NearAt(Strength(45, 27.0f, 1.0f, kAnimSteady, 0.0), 0.45f * kLargeShare,
         1e-6f, "a house gets 55 percent of the share of the key");
  Check(HasPulse(kAnimPulse) && HasPulse(kAnimBoth) && !HasPulse(kAnimWave) &&
            !HasPulse(kAnimSteady),
        "the values 1 and 3 have the pulse", 1, 1);
  Check(HasWave(kAnimWave) && HasWave(kAnimBoth) && !HasWave(kAnimPulse) &&
            !HasWave(kAnimSteady),
        "the values 2 and 3 have the wave", 1, 1);
  Check(Part(255.0f, 0.45f, 1.0f) == 115, "255 at 45 percent is 115",
        Part(255.0f, 0.45f, 1.0f), 115);
  Check(Part(255.0f, 2.0f, 1.3f) == 255, "a part stops at 255",
        Part(255.0f, 2.0f, 1.3f), 255);
  Check(Part(40.0f, 0.0f, 1.0f) == 0, "no strength gives no part",
        Part(40.0f, 0.0f, 1.0f), 0);
  // The least light of the default look still shows: gold on a small mesh
  // at the low value of the pulse and of the wave.
  const int least = Part(255.0f, Strength(kPctDefault, 1.5f, 1.0f, kAnimBoth,
                                          0.5 * kPulseSeconds),
                         wave_low);
  Check(least >= 48, "the least red of a small gold target is 48 of 255 or "
                     "more",
        least, 48);

  // The keys.
  Check(ClampMode(7) == kModeMesh && ClampMode(-1) == kModeMesh &&
            ClampMode(0) == kModeHalo && ClampMode(2) == kModeBoth,
        "a bad value of target_glow gives 1", ClampMode(7), kModeMesh);
  Check(ClampAnim(9) == kAnimBoth && ClampAnim(0) == kAnimSteady,
        "a bad value of target_glow_anim gives 3", ClampAnim(9), kAnimBoth);
  Check(NextMode(kModeMesh) == kModeHalo && NextMode(kModeHalo) == kModeBoth &&
            NextMode(kModeBoth) == kModeMesh,
        "the test key goes 1, 0, 2, 1", NextMode(kModeMesh), kModeHalo);

  // The mark.
  const uint32_t flat = AddColor(0xFF000000u, 115, 86, 18);
  bool marks = true;
  for (int a = 0; a < kSlots; ++a) {
    if ((Mark(flat, a) & 7u) != static_cast<uint32_t>(a)) marks = false;
    if ((Mark(flat, a) >> 24) != 0xFFu) marks = false;
    if ((Mark(flat, a) & 0x00FFFF00u) != (flat & 0x00FFFF00u)) marks = false;
    for (int b = 0; b < kSlots; ++b) {
      if ((a == b) != SameRgb(Mark(flat, a), Mark(flat, b))) marks = false;
    }
  }
  Check(marks, "each slot has its own mark, 7 of 255 from the glow at most",
        marks, 1);
  Check(SameRgb(0x80112233u, 0xFF112233u),
        "the alpha does not count for the same red, green and blue", 1, 1);

  // A vertex that the game fades. The numbers are from a run: the mark
  // 32 25 08 came to the draw as FE 31 24 07.
  Same(Faded(0xFF322508u, 0xFEu), 0xFE312407u,
       "the mark 32 25 08 with the alpha FE is 31 24 07");
  Same(Faded(0xFF735612u, 0xFFu), 0xFF735612u, "the alpha FF changes nothing");
  Same(Faded(0xFF735612u, 0x80u), 0x80392B09u, "half the alpha is half");
  Same(Faded(0xFF735612u, 0u), 0u, "no alpha gives no color");
  Check(HasMark(0xFE312407u, 0xFF322508u),
        "a faded vertex of the run has the mark", 1, 1);
  Check(HasMark(0xFF322508u, 0xFF322508u), "the mark itself has the mark", 1,
        1);
  Check(!HasMark(0xFF322509u, 0xFF322508u),
        "with the alpha FF the color must be the mark: the mark of the next "
        "slot is not the mark",
        0, 0);
  Check(!HasMark(0xFF000000u, 0xFF040300u) && !HasMark(0xFE000000u, 0xFF040300u),
        "a vertex with no specular color does not have a mark of 4 3 0", 0,
        0);
  Check(HasMark(0xFE302508u, 0xFF322508u) && !HasMark(0xFE2F2407u, 0xFF322508u),
        "a faded channel can be 1 off, and not 2", 1, 1);
  Check(!HasMark(0x80322508u, 0xFF322508u),
        "half the alpha with the whole color is not the mark", 0, 0);
  // Black is not a mark, with each alpha and with each strength of the mark.
  bool black = true;
  for (uint32_t alpha = 0; alpha < 0xFFu; ++alpha) {
    for (int part = 0; part <= 255; part += 5) {
      const uint32_t mark = Mark(AddColor(0xFF000000u, part, part * 3 / 4,
                                          part / 6), 2);
      if (HasMark(alpha << 24, mark)) black = false;
    }
  }
  Check(black, "a faded vertex with no color never has a mark", black, 1);
  Check(!HasMark(0x00000000u, 0xFF735612u),
        "a vertex with no alpha and no color does not have the mark", 0, 0);
  // A new color keeps the fade of the game, and it is then a mark for that
  // alpha.
  bool keeps = true;
  for (uint32_t alpha = 0x10u; alpha <= 0xFFu; alpha += 0x0Fu) {
    const uint32_t mark = Mark(AddColor(0xFF000000u, 115, 86, 18), 3);
    if (!HasMark(Faded(mark, alpha), mark)) keeps = false;
    if ((Faded(mark, alpha) >> 24) != alpha) keeps = false;
  }
  Check(keeps, "the mark with a fade is the mark at each alpha", keeps, 1);
  Check(StrongMark(0xFF180000u) && StrongMark(0xFF001800u) &&
            StrongMark(0xFF000018u) && !StrongMark(0xFF171717u),
        "a mark is strong with a channel of 24 or more", 1, 1);

  // The height of a vertex in the world, from its place on the screen. A
  // camera that looks 30 degrees down and 40 degrees to the side, with field
  // of view terms on columns 0 and 1, and a translation.
  const float yaw = 0.7f, pitch = -0.52f;
  const float cy = cosf(yaw), sy = sinf(yaw);
  const float cp = cosf(pitch), sp = sinf(pitch);
  // The camera axes in the world: right, up, forward.
  const float right[3] = {cy, 0.0f, -sy};
  const float forward[3] = {sy * cp, sp, cy * cp};
  const float up[3] = {forward[1] * right[2] - forward[2] * right[1],
                       forward[2] * right[0] - forward[0] * right[2],
                       forward[0] * right[1] - forward[1] * right[0]};
  const float fov_x = 1.19f, fov_y = 1.43f;
  const float eye[3] = {2310.0f, 41.5f, 1877.0f};
  float m[12];
  for (int i = 0; i < 3; ++i) {
    m[3 * i + 0] = right[i] * fov_x;
    m[3 * i + 1] = up[i] * fov_y;
    m[3 * i + 2] = forward[i];
  }
  for (int j = 0; j < 3; ++j) {
    m[9 + j] = -(eye[0] * m[j] + eye[1] * m[3 + j] + eye[2] * m[6 + j]);
  }
  const float hx = 1104.0f, hy = 1104.0f, near_plane = 0.75f;
  const HeightMap map = HeightFromScreen(m, hx, hy, near_plane);
  Check(map.ok, "the camera gives a height map", map.ok, 1);
  bool heights = true;
  float worst = 0.0f;
  const float points[5][3] = {{2330.0f, 20.0f, 1890.0f},
                              {2318.5f, 23.25f, 1885.0f},
                              {2360.0f, 2.5f, 1930.0f},
                              {2322.0f, 31.0f, 1894.0f},
                              {2400.0f, 0.0f, 1990.0f}};
  for (int n = 0; n < 5; ++n) {
    const float* p = points[n];
    const float cx = p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9];
    const float cyy = p[0] * m[1] + p[1] * m[4] + p[2] * m[7] + m[10];
    const float cz = p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11];
    if (!(cz > near_plane)) {
      heights = false;
      continue;
    }
    const float x = (1.0f + cx / cz) * hx;
    const float y = (1.0f - cyy / cz) * hy;
    const float rhw = near_plane / cz;
    float h = 0.0f;
    if (!HeightAt(map, x, y, rhw, &h)) {
      heights = false;
      continue;
    }
    const float error = fabsf(h - p[1]);
    if (error > worst) worst = error;
    if (error > 0.02f) heights = false;
  }
  Check(heights, "the height of 5 points comes back within 0.02 units",
        worst, 0.02);
  // An eye 0.5 units to the side gives the same height for the same point,
  // so both eyes get the same wave.
  float m2[12];
  for (int i = 0; i < 12; ++i) m2[i] = m[i];
  const float eye2[3] = {eye[0] + 0.5f * right[0], eye[1] + 0.5f * right[1],
                         eye[2] + 0.5f * right[2]};
  for (int j = 0; j < 3; ++j) {
    m2[9 + j] = -(eye2[0] * m[j] + eye2[1] * m[3 + j] + eye2[2] * m[6 + j]);
  }
  const HeightMap map2 = HeightFromScreen(m2, hx, hy, near_plane);
  {
    const float* p = points[1];
    float h1 = 0.0f, h2 = 0.0f;
    const float cz = p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11];
    const float x1 = (1.0f + (p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9]) /
                                 cz) * hx;
    const float y1 = (1.0f - (p[0] * m[1] + p[1] * m[4] + p[2] * m[7] +
                              m[10]) / cz) * hy;
    const float cz2 = p[0] * m2[2] + p[1] * m2[5] + p[2] * m2[8] + m2[11];
    const float x2 = (1.0f + (p[0] * m2[0] + p[1] * m2[3] + p[2] * m2[6] +
                              m2[9]) / cz2) * hx;
    const float y2 = (1.0f - (p[0] * m2[1] + p[1] * m2[4] + p[2] * m2[7] +
                              m2[10]) / cz2) * hy;
    const bool ok1 = HeightAt(map, x1, y1, near_plane / cz, &h1);
    const bool ok2 = HeightAt(map2, x2, y2, near_plane / cz2, &h2);
    Check(ok1 && ok2 && fabsf(h1 - h2) < 0.02f && fabsf(x1 - x2) > 1.0f,
          "the two eyes see the point at different pixels and at one height",
          fabsf(h1 - h2), 0.0);
  }
  float unused = 0.0f;
  Check(!HeightAt(map, 500.0f, 500.0f, 0.0f, &unused) &&
            !HeightAt(map, 500.0f, 500.0f, -0.1f, &unused),
        "a vertex that is not in front of the camera has no height", 0, 0);
  const float flat_m[12] = {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0};
  Check(!HeightFromScreen(flat_m, hx, hy, near_plane).ok,
        "a matrix with no inverse gives no map", 0, 0);
  Check(!HeightFromScreen(m, 0.0f, hy, near_plane).ok,
        "no render size gives no map", 0, 0);

  // The base and the top of the mesh.
  MeshExtent mesh = {0.0f, 0.0f};
  mesh = Follow(mesh, 28.0f, 55.0f);
  Check(mesh.base == 28.0f && mesh.top == 55.0f,
        "the first frame gives the base and the top", mesh.top, 55.0);
  mesh = Follow(mesh, 27.5f, 56.0f);
  Check(mesh.base == 27.5f && mesh.top == 56.0f,
        "a larger mesh counts at once", mesh.top, 56.0);
  mesh = Follow(mesh, 30.0f, 40.0f);
  NearAt(mesh.base, 27.75f, 1e-4f,
         "a higher base counts by a tenth in a frame");
  NearAt(mesh.top, 54.4f, 1e-4f, "a lower top counts by a tenth in a frame");
  for (int i = 0; i < 100; ++i) mesh = Follow(mesh, 30.0f, 40.0f);
  NearAt(mesh.base, 30.0f, 0.01f, "the base comes to the new mesh in time");
  NearAt(mesh.top, 40.0f, 0.01f, "the top comes to the new mesh in time");
  const MeshExtent kept = Follow(mesh, 1e30f, -1e30f);
  Check(kept.base == mesh.base && kept.top == mesh.top,
        "a frame with no vertex keeps the known mesh", kept.top, mesh.top);

  NearAt(Share(12.0f, 10.0f, 8.0f), 0.25f, 1e-6f,
         "a vertex 2 units over the base of a mesh of 8 units is at 0.25");
  NearAt(Share(5.0f, 10.0f, 8.0f), 0.0f, 1e-6f, "the share stops at 0");
  NearAt(Share(50.0f, 10.0f, 8.0f), 1.0f, 1e-6f, "the share stops at 1");
  NearAt(Share(12.0f, 10.0f, 0.0f), 0.0f, 1e-6f,
         "a mesh with no height has the share 0");

  // The halo.
  Check(HaloShows(kModeHalo, false, false) && HaloShows(kModeHalo, true, true),
        "target_glow=0 always shows the halo", 1, 1);
  Check(HaloShows(kModeBoth, true, true) && HaloShows(kModeBoth, false, false),
        "target_glow=2 always shows the halo", 1, 1);
  Check(!HaloShows(kModeMesh, true, true),
        "target_glow=1: a target whose mesh glows has no halo", 0, 0);
  Check(HaloShows(kModeMesh, true, false),
        "target_glow=1: a target whose mesh does not glow keeps its halo", 1,
        1);
  Check(!HaloShows(kModeMesh, false, false),
        "target_glow=1: a target that is new in this frame has no halo yet",
        0, 0);
}

}  // namespace target_glow_checks
