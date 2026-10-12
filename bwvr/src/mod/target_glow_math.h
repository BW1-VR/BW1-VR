// THE GLOW ON THE MESH OF A TARGET (docs/hand-model.md, "The glow on the mesh
// of a target"). Pure math: no Windows, no game. The checks are in
// test/controller_math/target_glow_checks.h.
//
// The mesh of a target of the Hand gets light in the color of its halo. The
// light is a color that Direct3D adds over the texture: the specular color
// of each vertex. The game uses the same color for haze, poison and fire.
//
// THE LOOK. The strength of the light is a product of four numbers:
//
//   the key    target_glow_pct, the share of the halo color
//   the size   1 for a small mesh, less for a large one (SizeShare)
//   the fade   0 to 1 in kFadeSeconds, so a change of target has no jump
//   the pulse  a slow breath between kPulseLow and 1, never 0
//   the wave   a soft band of light that goes up the mesh, from the height
//              of each vertex in the world (Wave)
//
// Each number comes from the real time of the frame, not from a count of
// frames, so each frame rate has the same look. The wave uses the place of a
// vertex in the world. A pattern on the screen would move over the mesh when
// the head moves, and the two eyes would not agree.

#pragma once

#include <math.h>
#include <stdint.h>

namespace target_glow_math {

// target_glow: what marks a target of the Hand.
const int kModeHalo = 0;  // the halo around the target
const int kModeMesh = 1;  // the glow on the mesh, and a halo only for a
                          // target whose mesh does not glow
const int kModeBoth = 2;  // the glow on the mesh and the halo
const int kModes = 3;

// target_glow_anim: how the glow moves.
const int kAnimSteady = 0;  // no animation
const int kAnimPulse = 1;   // the pulse
const int kAnimWave = 2;    // the wave
const int kAnimBoth = 3;    // the pulse and the wave
const int kAnims = 4;

// target_glow_pct: the share of the halo color that the mesh gets at the top
// of the pulse and in the middle of the wave, in percent.
const int kPctDefault = 45;
const int kPctMax = 200;

const float kFadeSeconds = 0.15f;
const float kPulseSeconds = 1.8f;
const float kPulseLow = 0.70f;
const float kWaveSeconds = 1.5f;
const float kWaveDepth = 0.50f;
// The bands on the height of the mesh. With 1 there is always one bright
// band on the mesh: when it leaves at the top, the next one comes in at the
// base.
const float kWaveCycles = 1.0f;

inline int ClampMode(int mode) {
  return (mode >= 0 && mode < kModes) ? mode : kModeMesh;
}

inline int ClampAnim(int anim) {
  return (anim >= 0 && anim < kAnims) ? anim : kAnimBoth;
}

inline int ClampPct(int pct) {
  if (pct < 0) return 0;
  if (pct > kPctMax) return kPctMax;
  return pct;
}

inline bool HasPulse(int anim) {
  return anim == kAnimPulse || anim == kAnimBoth;
}

inline bool HasWave(int anim) { return anim == kAnimWave || anim == kAnimBoth; }

// The next value of target_glow for the test key: 1, 0, 2, and 1 again. The
// first press gives the halo, so one press shows the two looks.
inline int NextMode(int mode) {
  if (mode == kModeMesh) return kModeHalo;
  if (mode == kModeHalo) return kModeBoth;
  return kModeMesh;
}

// The specular color of the game with more light: 0xFF000000 and each
// channel of base plus its part of the glow, at most 255. The game writes
// its own colors in this form.
inline uint32_t AddColor(uint32_t base, int r, int g, int b) {
  const auto channel = [](uint32_t value, int add) -> uint32_t {
    int sum = static_cast<int>(value & 0xFFu) + (add > 0 ? add : 0);
    if (sum > 255) sum = 255;
    return static_cast<uint32_t>(sum);
  };
  return 0xFF000000u | (channel(base >> 16, r) << 16) |
         (channel(base >> 8, g) << 8) | channel(base, b);
}

// The fade: the level goes to 1 while the object is a target and to 0 when it
// is not, in kFadeSeconds. seconds is the time of one frame.
inline float Fade(float level, bool wanted, float seconds) {
  if (!(seconds > 0.0f)) return level;
  const float step = seconds / kFadeSeconds;
  level += wanted ? step : -step;
  if (level < 0.0f) return 0.0f;
  if (level > 1.0f) return 1.0f;
  return level;
}

// The level with a soft start and a soft end.
inline float Ease(float level) {
  if (level <= 0.0f) return 0.0f;
  if (level >= 1.0f) return 1.0f;
  return level * level * (3.0f - 2.0f * level);
}

// A color channel goes to its new value in the time of the fade: 255 for
// each kFadeSeconds.
inline float Approach(float now, float want, float seconds) {
  if (!(seconds > 0.0f)) return now;
  const float step = 255.0f * seconds / kFadeSeconds;
  if (now < want) return now + step < want ? now + step : want;
  return now - step > want ? now - step : want;
}

// The part of a period that is over, 0 to 1.
inline float Phase(double seconds, float period) {
  if (!(period > 0.0f)) return 0.0f;
  const double turns = seconds / period;
  return static_cast<float>(turns - floor(turns));
}

// The pulse: 1 at the start of each period, and kPulseLow in its middle.
inline float Pulse(double seconds) {
  const float c = cosf(6.2831853f * Phase(seconds, kPulseSeconds));
  return kPulseLow + (1.0f - kPulseLow) * (0.5f + 0.5f * c);
}

// The wave at one vertex. share is the height of the vertex on the mesh: 0 at
// the base and 1 at the top. The band is the square of a raised cosine, so
// the bright part is narrower than the dim part and reads as a band of light.
// The square has the mean 3/8, and the wave has the mean 1 over a period: it
// is 1 plus kWaveDepth in the band and 1 less 0.6 kWaveDepth away from it.
inline float Wave(float share, double seconds) {
  const float turn = share * kWaveCycles - Phase(seconds, kWaveSeconds);
  const float raised = 0.5f + 0.5f * cosf(6.2831853f * turn);
  return 1.0f + kWaveDepth * (raised * raised - 0.375f) / 0.625f;
}

// A LARGE MESH GETS LESS. The same share of the color that marks a villager
// makes a house one bright patch. So a mesh up to kSmallUnits high in the
// world gets the full share, and a higher mesh gets less, down to
// kLargeShare. A villager is 1.5 units high, a rock 2 to 3, and a tree and
// the Village Store 13. height is that of the mesh in the frames before, and
// 0 when it is not known yet.
const float kSmallUnits = 3.0f;
const float kLargeShare = 0.55f;

inline float SizeShare(float height) {
  if (!(height > kSmallUnits)) return 1.0f;
  const float share = sqrtf(kSmallUnits / height);
  return share > kLargeShare ? share : kLargeShare;
}

// The share of the halo color that each vertex of the mesh gets before the
// wave: the key, the size of the mesh, the fade and the pulse.
inline float Strength(int pct, float height, float level, int anim,
                      double seconds) {
  const float pulse = HasPulse(anim) ? Pulse(seconds) : 1.0f;
  return 0.01f * static_cast<float>(ClampPct(pct)) * SizeShare(height) *
         Ease(level) * pulse;
}

// One channel of the glow: the halo color (0 to 255) times the strength and
// the wave, as a whole number.
inline int Part(float color, float strength, float wave) {
  const float v = color * strength * wave;
  if (!(v > 0.0f)) return 0;
  if (v >= 255.0f) return 255;
  return static_cast<int>(v + 0.5f);
}

// THE MARK. Each target has a slot, 0 to 7. The color that the object gets
// is the glow with no wave, with the number of the slot in the low 3 bits of
// blue. So the vertices of each target are known by their color when they
// come to the draw, and a vertex that the wave does not reach still glows.
const int kSlots = 8;

inline uint32_t Mark(uint32_t color, int slot) {
  return (color & 0xFFFFFFF8u) | (static_cast<uint32_t>(slot) & 7u);
}

// The red, the green and the blue are the same. The alpha is not compared.
inline bool SameRgb(uint32_t a, uint32_t b) {
  return ((a ^ b) & 0x00FFFFFFu) == 0;
}

// A VERTEX THAT THE GAME FADES. The vertex builder fn_00856D40 draws a tree.
// For a tree at the widest zoom of the God view it scales each channel of the
// specular color of a projected vertex and writes the factor into its alpha:
// the mark 32 25 08 comes to the draw as FE 31 24 07. So a vertex with an
// alpha below FF has the mark when each channel is the channel of the mark
// times alpha / 255, 1 more or less. A vertex with the alpha FF must have
// the mark itself.
inline uint32_t Faded(uint32_t color, uint32_t alpha) {
  alpha &= 0xFFu;
  const auto channel = [alpha](uint32_t value) -> uint32_t {
    return (value & 0xFFu) * alpha / 255u;
  };
  return (alpha << 24) | (channel(color >> 16) << 16) |
         (channel(color >> 8) << 8) | channel(color);
}

// BLACK IS NOT A MARK. In the call of a villager the game also draws 8
// vertices with a specular color of 0 or near 0 (Draw3DScreenTriangle, from
// 0x0081C5B1). A faded mark counts only when one of its channels is 3 or
// more, so a color of 0 is never 1 off from it.
inline bool HasMark(uint32_t color, uint32_t mark) {
  const uint32_t alpha = color >> 24;
  if (alpha == 0xFFu) return SameRgb(color, mark);
  const uint32_t want = Faded(mark, alpha);
  int most = 0;
  for (int shift = 0; shift < 24; shift += 8) {
    const int got = static_cast<int>((color >> shift) & 0xFFu);
    const int expected = static_cast<int>((want >> shift) & 0xFFu);
    if (got < expected - 1 || got > expected + 1) return false;
    if (expected > most) most = expected;
  }
  return most >= 3;
}

// A mark with a channel of 24 or more is far enough from black. Only then
// does a faded first vertex name the target of a batch that the game draws
// after the call of the object (a mesh of the sorted list).
inline bool StrongMark(uint32_t mark) {
  return ((mark >> 16) & 0xFFu) >= 24u || ((mark >> 8) & 0xFFu) >= 24u ||
         (mark & 0xFFu) >= 24u;
}

// THE HEIGHT OF A VERTEX IN THE WORLD, from its place on the screen.
//
// m is the game's world to camera matrix, 4 rows of 3 with the translation in
// the last row (controller_math.h, GlowProject):
//
//   cam[j] = p.x * m[j] + p.y * m[3 + j] + p.z * m[6 + j] + m[9 + j]
//   screen x = (1 + cam.x / cam.z) * hx,  screen y = (1 - cam.y / cam.z) * hy
//   rhw = near / cam.z
//
// So cam.z = near / rhw, cam.x = (x / hx - 1) * cam.z and
// cam.y = (1 - y / hy) * cam.z. The height p.y is the dot product of
// cam - translation with the middle column of the inverse of the 3 by 3
// part. That gives
//
//   p.y = (a * x + b * y + c) / rhw + d
struct HeightMap {
  bool ok;
  float a, b, c, d;
};

inline HeightMap HeightFromScreen(const float m[12], float hx, float hy,
                                  float near_plane) {
  HeightMap out = {false, 0.0f, 0.0f, 0.0f, 0.0f};
  if (!(hx > 0.0f) || !(hy > 0.0f) || !(near_plane > 0.0f)) return out;
  const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
                    m[1] * (m[3] * m[8] - m[5] * m[6]) +
                    m[2] * (m[3] * m[7] - m[4] * m[6]);
  if (!(fabsf(det) > 1e-12f)) return out;
  // The cofactors of the middle row, over the determinant.
  const float k0 = -(m[1] * m[8] - m[2] * m[7]) / det;
  const float k1 = (m[0] * m[8] - m[2] * m[6]) / det;
  const float k2 = -(m[0] * m[7] - m[1] * m[6]) / det;
  out.a = near_plane * k0 / hx;
  out.b = -near_plane * k1 / hy;
  out.c = near_plane * (k1 + k2 - k0);
  out.d = -(k0 * m[9] + k1 * m[10] + k2 * m[11]);
  out.ok = true;
  return out;
}

// False for a vertex that is not in front of the camera.
inline bool HeightAt(const HeightMap& map, float x, float y, float rhw,
                     float* height) {
  if (!map.ok || !(rhw > 1e-9f)) return false;
  const float h = (map.a * x + map.b * y + map.c) / rhw + map.d;
  if (!(h == h) || h > 1e9f || h < -1e9f) return false;
  *height = h;
  return true;
}

// THE BASE AND THE TOP OF THE MESH, from the vertices that came to the draw.
// A frame has only the vertices in the view, so a mesh at the edge of the
// view has fewer of them in some frames. The mesh of a frame can make the
// known mesh larger at once. It makes the known mesh smaller by a tenth of
// the difference in each frame, so the wave and the size of a mesh at the
// edge of the view stay calm. top is not above base when nothing is known.
struct MeshExtent {
  float base, top;
};

inline MeshExtent Follow(MeshExtent known, float low, float high) {
  if (!(high > low)) return known;
  if (!(known.top > known.base)) {
    known.base = low;
    known.top = high;
    return known;
  }
  known.base = low < known.base ? low : known.base + 0.1f * (low - known.base);
  known.top = high > known.top ? high : known.top + 0.1f * (high - known.top);
  return known;
}

// The height of a vertex on the mesh, 0 at the base and 1 at the top. base
// and height are those of the mesh in the frame before.
inline float Share(float y, float base, float height) {
  if (!(height > 1e-4f)) return 0.0f;
  const float s = (y - base) / height;
  if (s < 0.0f) return 0.0f;
  if (s > 1.0f) return 1.0f;
  return s;
}

// THE HALO with target_glow=1. A target whose mesh glows in this frame has no
// halo. A target that is new in this frame has no halo either: its mesh gets
// the glow from the next frame on, and a halo for one frame would be a flash.
// known: the object was a target when the frame started. glows: vertices of
// its mesh went to the draw with the glow in this frame.
inline bool HaloShows(int mode, bool known, bool glows) {
  if (mode == kModeHalo || mode == kModeBoth) return true;
  return known && !glows;
}

}  // namespace target_glow_math
