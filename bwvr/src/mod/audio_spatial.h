// The pure signal math of the mixer of the mod (docs/audio.md, section 15).
// No Windows, no game: the math test compiles it as it is.
//
// A source with a place gets its direction for two ears from a structural
// model of the head. The model is made of small filters with a physical
// meaning, after C. P. Brown and R. O. Duda, "A Structural Model for Binaural
// Sound Synthesis" (IEEE Transactions on Speech and Audio Processing, 1998):
//
// - the delay between the ears (Woodworth's formula for a sphere),
// - the shadow of the head (one pole and one zero for each ear),
// - the echoes of the outer ear, whose delays change with the elevation.
//
// To these it adds a model of the body for sources below and behind, a low
// pass by distance, and the near field. The model needs no measured data
// set, so there is no file to license or to ship, and it costs a few
// multiplications for each sample. A measured HRTF can replace Process later:
// the callers give only the place of the source.
#pragma once

#include <math.h>
#include <stdint.h>
#include <string.h>

namespace audio_spatial {

const float kPi = 3.14159265f;
const float kHeadRadius = 0.0875f;    // meters
const float kSpeedOfSound = 343.0f;   // meters a second
const int kHistory = 64;              // source samples that a voice keeps
const int kEarLine = 64;              // samples of the delay line of an ear
const int kHop = 16;                  // the controls move once for each hop
// A voice starts at its full level, so the attack of a crash or of a step
// stays sharp. Only its first samples rise from zero, in 1.5 ms.
const int kStartRamp = 32;
// The time in which a control goes most of the way to a new value. It is the
// ramp that keeps a change of place or of level free of clicks.
const float kSmoothSeconds = 0.015f;

inline float Clamp(float v, float lo, float hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

inline float Smooth(float t) {
  t = Clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// WHERE A SOURCE IS in the frame of the head.
struct Aim {
  float lateral;     // radians to the right of the middle plane, -pi/2..pi/2
  float elevation;   // radians over the level of the ears, -pi/2..pi/2
  float back;        // 0 in front, 1 straight behind
};

// basis has the columns right, up and forward, as HeadCameraRenderBasis
// gives them (a column is read with a stride of three). distance is in the
// units of the two points. A source at the ears has no direction: it is
// straight ahead.
inline Aim AimOf(const float basis[9], const float ears[3],
                 const float point[3], float* distance) {
  const float d[3] = {point[0] - ears[0], point[1] - ears[1],
                      point[2] - ears[2]};
  const float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  *distance = n;
  Aim a = {0.0f, 0.0f, 0.0f};
  if (!(n > 1e-6f)) return a;
  const float r = (basis[0] * d[0] + basis[3] * d[1] + basis[6] * d[2]) / n;
  const float u = (basis[1] * d[0] + basis[4] * d[1] + basis[7] * d[2]) / n;
  const float f = (basis[2] * d[0] + basis[5] * d[1] + basis[8] * d[2]) / n;
  a.lateral = asinf(Clamp(r, -1.0f, 1.0f));
  a.elevation = asinf(Clamp(u, -1.0f, 1.0f));
  a.back = Clamp(-f, 0.0f, 1.0f);
  return a;
}

// THE DELAY BETWEEN THE EARS, in seconds (Woodworth). More than 0 when the
// source is at the right, and then the left ear hears it later.
inline float InterauralDelay(float lateral) {
  return kHeadRadius / kSpeedOfSound * (lateral + sinf(lateral));
}

// THE SHADOW OF THE HEAD for one ear (Brown and Duda). incidence is the
// angle between the source and the axis of that ear, 0 to pi. The result is
// the gain of the high frequencies: 2 for a source at the ear, 0.1 at 150
// degrees, a little more straight across (the bright spot).
inline float ShadowHigh(float incidence) {
  const float kLeast = 0.1f;
  const float kAngleOfLeast = 150.0f / 180.0f * kPi;
  return (1.0f + kLeast * 0.5f) +
         (1.0f - kLeast * 0.5f) * cosf(incidence / kAngleOfLeast * kPi);
}

// The digital filter of the shadow: y = b0 x + b1 x1 - a1 y1. Its gain is 1
// at 0 Hz and `high` at half the rate. The pole is at 2 c / a, 1248 Hz, and
// it does not move, so a change of `high` moves only the zero.
inline void ShadowFilter(float high, float rate, float* b0, float* b1,
                         float* a1) {
  const float k = rate * kHeadRadius / kSpeedOfSound;
  const float a0 = 1.0f + k;
  *b0 = (1.0f + high * k) / a0;
  *b1 = (1.0f - high * k) / a0;
  *a1 = (1.0f - k) / a0;
}

// THE ECHOES OF THE OUTER EAR (Brown and Duda, table 1): the reflection of
// each echo, and its delay A cos(azimuth / 2) sin(D (90 deg - elevation)) + B
// in samples at 44100 Hz. The reflections sum to 0, so the low frequencies
// do not change. The second echo makes a notch that goes from 4.9 kHz for a
// source straight below to 11 kHz for a source straight above.
const int kPinnaEchoes = 5;
const float kPinnaReflection[kPinnaEchoes] = {0.5f, -1.0f, 0.5f, -0.25f,
                                              0.25f};
const float kPinnaA[kPinnaEchoes] = {1.0f, 5.0f, 5.0f, 5.0f, 5.0f};
const float kPinnaB[kPinnaEchoes] = {2.0f, 4.0f, 7.0f, 11.0f, 13.0f};
const float kPinnaD[kPinnaEchoes] = {1.0f, 0.5f, 0.5f, 0.5f, 0.5f};
// How much of the echoes the mixer uses. The full model colors a sound more
// than a game sample of 22 kHz can take.
const float kPinnaAmount = 0.7f;

inline float PinnaDelay(int echo, float lateral, float elevation) {
  return (kPinnaA[echo] * cosf(lateral * 0.5f) *
              sinf(kPinnaD[echo] * (kPi * 0.5f - elevation)) +
          kPinnaB[echo]) / 44100.0f;
}

// THE BODY. A source below the ears is behind the shoulders and the chest,
// and a source behind the head is behind the outer ear: both lose high
// frequencies. This is the gain of the part over 1.5 kHz: 1 for a source
// ahead at the level of the ears, 0.75 straight below (-2.5 dB), 0.7
// straight behind (-3.1 dB).
const float kBodyCornerHz = 1500.0f;
inline float BodyHigh(float elevation, float back) {
  const float below = Clamp(-elevation / (kPi * 0.5f), 0.0f, 1.0f);
  return (1.0f - 0.25f * below) * (1.0f - 0.30f * Clamp(back, 0.0f, 1.0f));
}

// THE HEIGHT BAND. The ear takes a strong top octave (near 8 kHz) as "above"
// and a weak one as "below" (the directional bands of Blauert). The echoes
// above move a narrow notch, which is a small change for a game sample. So
// one shelf filter over 5 kHz makes the cue clear: +7 dB straight above, 0
// at the level of the ears, -9 dB straight below. The notch of the echoes
// moves inside that band. `strength` scales the shelf (the key
// audio_height_pct).
const float kHeightHz = 5000.0f;
inline float HeightDb(float elevation, float strength) {
  const float s = sinf(elevation);
  return strength * (s >= 0.0f ? 7.0f : 9.0f) * s;
}

// A high shelf filter (the Audio EQ Cookbook of R. Bristow Johnson, with
// the shelf slope 1). c gets b0, b1, b2, a1 and a2 for
// y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
inline void ShelfFilter(float gain_db, float hz, float rate, float c[5]) {
  if (hz > 0.4f * rate) hz = 0.4f * rate;
  const float a = powf(10.0f, gain_db / 40.0f);
  const float w = 2.0f * kPi * hz / rate;
  const float cw = cosf(w);
  const float beta = sinf(w) * sqrtf(2.0f * a);   // 2 sqrt(A) alpha
  const float a0 = (a + 1.0f) - (a - 1.0f) * cw + beta;
  c[0] = a * ((a + 1.0f) + (a - 1.0f) * cw + beta) / a0;
  c[1] = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cw) / a0;
  c[2] = a * ((a + 1.0f) + (a - 1.0f) * cw - beta) / a0;
  c[3] = 2.0f * ((a - 1.0f) - (a + 1.0f) * cw) / a0;
  c[4] = ((a + 1.0f) - (a - 1.0f) * cw - beta) / a0;
}

// The echo of the shoulder, for a source at the level of the ears and over
// it. Its level, and its delay in seconds.
inline float ShoulderLevel(float elevation) {
  return 0.2f * Smooth((elevation + 20.0f / 180.0f * kPi) /
                       (30.0f / 180.0f * kPi));
}
inline float ShoulderDelay(float elevation) {
  const float s = sinf(elevation);
  return 0.00025f + 0.00055f * (s > 0.0f ? s : 0.0f);
}

// THE NEAR FIELD. A source very near the head would sit hard in one ear.
// Half-Life: Alyx sends a source nearer than 25 to 50 cm to both ears. Here
// the source keeps this share of its angle to the side: all of it from 50 cm
// of the room, 0.2 of it at 20 cm and nearer.
inline float NearFocus(float meters) {
  return 0.2f + 0.8f * Smooth((meters - 0.20f) / 0.30f);
}

// DISTANCE. part says where a source is in its own range: 0 at its minimum
// distance or nearer, 1 at its maximum distance. The game gives each sample
// this range, and the room factor makes it a range in the room, so `part`
// is the same at each zoom.
inline float RangePart(float distance, float min_distance,
                       float max_distance) {
  if (!(max_distance > min_distance)) return 0.0f;
  return Clamp((distance - min_distance) / (max_distance - min_distance),
               0.0f, 1.0f);
}

// A far sound loses its high frequencies: the corner of the low pass in Hz.
// 12 kHz at the minimum distance (open for a 22 kHz sample), 5 kHz half
// way, 1.2 kHz at the maximum distance.
inline float DistanceCorner(float part) {
  const float open = 1.0f - Clamp(part, 0.0f, 1.0f);
  return 1200.0f + 10800.0f * open * sqrtf(open);
}

// A far sound is a little diffuse: this share of it goes to both ears with
// no direction.
inline float DiffuseShare(float part) {
  return 0.25f * Clamp(part, 0.0f, 1.0f);
}

// The step of a one pole low pass, y += c (x - y), for a corner in Hz.
inline float OnePole(float corner, float rate) {
  const float c = 1.0f - expf(-2.0f * kPi * corner / rate);
  return c > 1.0f ? 1.0f : c;
}

// THE SUM of the stream and the voices stays inside 16 bits. Up to the knee
// a sample does not change. Over it the curve bends, and it comes to full
// level only for a sum without limit. So a loud moment has no hard edge.
const float kLimitKnee = 26000.0f;
const float kLimitFull = 32767.0f;
inline float SoftLimit(float v) {
  const float a = v < 0.0f ? -v : v;
  if (a <= kLimitKnee) return v;
  const float room = kLimitFull - kLimitKnee;
  const float over = (a - kLimitKnee) / room;
  const float bent = kLimitKnee + room * (over / (1.0f + over));
  return v < 0.0f ? -bent : bent;
}

// What the mixer wants for a voice now. Process moves to it in small steps.
struct Target {
  float lateral;     // after the near field
  float elevation;
  float back;
  float gain;        // the level of the voice, all in
  float corner;      // the low pass by distance, Hz
  float diffuse;     // the share with no direction
  float height;      // the strength of the height band, 1 as designed
};

// The state of the filters of one voice.
struct Voice {
  float history[kHistory];
  float ear[2][kEarLine];
  int at;                 // write index of the two lines
  int started;            // samples since the start, up to kStartRamp
  bool primed;            // the controls have their first values
  Target now;
  float body_low;         // state of the body shelf
  float distance_low;     // state of the low pass by distance
  float shadow_x[2], shadow_y[2];
  float height_x[2], height_y[2];   // state of the height band
  // Values for the hop in progress.
  float height_c[5];
  float echo_delay[kPinnaEchoes + 1];   // in samples, the shoulder last
  float echo_level[kPinnaEchoes + 1];
  float body_high;
  float corner_step;
  float b0[2], b1[2], a1;
  float ear_delay[2];
  int hop_left;
};

inline void Reset(Voice* v) { memset(v, 0, sizeof(*v)); }

inline float Tap(const float* line, int size, int at, float delay) {
  const int whole = static_cast<int>(delay);
  const float part = delay - static_cast<float>(whole);
  const int i0 = (at - whole) & (size - 1);
  const int i1 = (at - whole - 1) & (size - 1);
  return line[i0] + (line[i1] - line[i0]) * part;
}

// The values of one hop, from the controls as they are now.
inline void Hop(Voice* v, float rate) {
  const Target& n = v->now;
  for (int e = 0; e < kPinnaEchoes; ++e) {
    v->echo_delay[e] = PinnaDelay(e, n.lateral, n.elevation) * rate;
    v->echo_level[e] = kPinnaReflection[e] * kPinnaAmount;
  }
  v->echo_delay[kPinnaEchoes] = ShoulderDelay(n.elevation) * rate;
  v->echo_level[kPinnaEchoes] = ShoulderLevel(n.elevation);
  v->body_high = BodyHigh(n.elevation, n.back);
  ShelfFilter(HeightDb(n.elevation, n.height), kHeightHz, rate,
              v->height_c);
  v->corner_step = OnePole(n.corner, rate);
  // Ear 0 is the left ear, ear 1 the right ear.
  float unused = 0.0f;
  ShadowFilter(ShadowHigh(kPi * 0.5f + n.lateral), rate, &v->b0[0],
               &v->b1[0], &v->a1);
  ShadowFilter(ShadowHigh(kPi * 0.5f - n.lateral), rate, &v->b0[1],
               &v->b1[1], &unused);
  const float delay = InterauralDelay(n.lateral) * rate;
  v->ear_delay[0] = 1.0f + (delay > 0.0f ? delay : 0.0f);
  v->ear_delay[1] = 1.0f + (delay < 0.0f ? -delay : 0.0f);
}

// Renders `count` samples of one voice. in is the mono source at the rate of
// the output. The result is ADDED to left and right. The controls go to
// `target` with the time constant kSmoothSeconds, so no value jumps.
inline void Process(Voice* v, const Target& target, float rate,
                    const float* in, int count, float* left, float* right) {
  if (!v->primed) {
    v->now = target;
    v->started = 0;
    v->primed = true;
    v->hop_left = 0;
  }
  const float step = 1.0f - expf(-static_cast<float>(kHop) /
                                 (rate * kSmoothSeconds));
  const float body_step = OnePole(kBodyCornerHz, rate);
  float* out[2] = {left, right};
  for (int i = 0; i < count; ++i) {
    if (v->hop_left <= 0) {
      Target& n = v->now;
      n.lateral += (target.lateral - n.lateral) * step;
      n.elevation += (target.elevation - n.elevation) * step;
      n.back += (target.back - n.back) * step;
      n.gain += (target.gain - n.gain) * step;
      n.corner += (target.corner - n.corner) * step;
      n.diffuse += (target.diffuse - n.diffuse) * step;
      n.height = target.height;
      Hop(v, rate);
      v->hop_left = kHop;
    }
    --v->hop_left;
    const int at = v->at;
    v->history[at & (kHistory - 1)] = in[i];
    // The outer ear and the shoulder.
    float y = in[i];
    for (int e = 0; e <= kPinnaEchoes; ++e) {
      y += v->echo_level[e] *
           Tap(v->history, kHistory, at, v->echo_delay[e]);
    }
    // The body: the part over the corner gets the gain body_high.
    v->body_low += body_step * (y - v->body_low);
    y = v->body_low + v->body_high * (y - v->body_low);
    // The height band.
    const float h = v->height_c[0] * y + v->height_c[1] * v->height_x[0] +
                    v->height_c[2] * v->height_x[1] -
                    v->height_c[3] * v->height_y[0] -
                    v->height_c[4] * v->height_y[1];
    v->height_x[1] = v->height_x[0];
    v->height_x[0] = y;
    v->height_y[1] = v->height_y[0];
    v->height_y[0] = h;
    y = h;
    // The distance.
    v->distance_low += v->corner_step * (y - v->distance_low);
    y = v->distance_low;
    const float diffuse = v->now.diffuse * y;
    const float direct = 1.0f - v->now.diffuse;
    float level = v->now.gain;
    if (v->started < kStartRamp) {
      level *= static_cast<float>(v->started) / kStartRamp;
      ++v->started;
    }
    for (int e = 0; e < 2; ++e) {
      const float s = v->b0[e] * y + v->b1[e] * v->shadow_x[e] -
                      v->a1 * v->shadow_y[e];
      v->shadow_x[e] = y;
      v->shadow_y[e] = s;
      v->ear[e][at & (kEarLine - 1)] = s;
      out[e][i] += level *
                   (direct * Tap(v->ear[e], kEarLine, at, v->ear_delay[e]) +
                    diffuse);
    }
    v->at = at + 1;
  }
}

// THE BED OF THE ATMOSPHERE is one stereo wave. It stays fixed in the world
// when the head turns: the field turns against the yaw of the head. With
// M = (L + R) / 2 and S = (L - R) / 2,
//   left  = M + cos(yaw) S + sin(yaw) Q
//   right = M - cos(yaw) S - sin(yaw) Q
// Q is S through an all pass filter, so it has the level of S and is not in
// step with it. At yaw 0 the wave plays as it is. At 180 degrees the two
// channels have changed places. At 90 degrees both ears get M, and Q keeps
// the width. The level of each ear stays the same at each yaw.
const int kBedLine = 256;
struct Bed {
  float line_a[kBedLine], line_b[kBedLine];
  int at;
  float cos_now, sin_now;
  float gain_now;         // the level, which goes to `gain` in small steps
  bool primed;
};

inline void ResetBed(Bed* b) { memset(b, 0, sizeof(*b)); }

inline float AllPass(float* line, int at, int delay, float g, float x) {
  const float delayed = line[(at - delay) & (kBedLine - 1)];
  const float w = x + g * delayed;
  line[at & (kBedLine - 1)] = w;
  return delayed - g * w;
}

// in is interleaved stereo at the rate of the output. The result is ADDED
// to left and right with `gain`.
inline void ProcessBed(Bed* b, float yaw, float gain, float rate,
                       const float* in, int count, float* left,
                       float* right) {
  const float c = cosf(yaw), s = sinf(yaw);
  if (!b->primed) {
    b->cos_now = c;
    b->sin_now = s;
    b->gain_now = 0.0f;   // the bed starts from silence
    b->primed = true;
  }
  const float step = 1.0f - expf(-1.0f / (rate * kSmoothSeconds));
  for (int i = 0; i < count; ++i) {
    b->cos_now += (c - b->cos_now) * step;
    b->sin_now += (s - b->sin_now) * step;
    b->gain_now += (gain - b->gain_now) * step;
    const float l = in[2 * i], r = in[2 * i + 1];
    const float mid = 0.5f * (l + r), side = 0.5f * (l - r);
    float q = AllPass(b->line_a, b->at, 89, 0.6f, side);
    q = AllPass(b->line_b, b->at, 37, -0.6f, q);
    ++b->at;
    const float turned = b->cos_now * side + b->sin_now * q;
    left[i] += b->gain_now * (mid + turned);
    right[i] += b->gain_now * (mid - turned);
  }
}

// The yaw of the head in the world, from the forward column of its basis.
// 0 is the +z axis of the world, and a turn to the right is positive.
inline float HeadYaw(const float basis[9]) {
  return atan2f(basis[2], basis[8]);
}

}  // namespace audio_spatial
