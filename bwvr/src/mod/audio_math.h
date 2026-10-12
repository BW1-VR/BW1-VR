// The pure math of the sound in the room (docs/audio.md, "6.3 Room distances"). No
// Windows, no game: the math test compiles it as it is.
#pragma once

#include <math.h>

namespace audio_math {

// THE DISTANCE GAIN OF QMIXER, as QMixer.dll computes it for a channel
// (0x0000AD4C, and the rolloff at 0x0002CE50). min, max and rolloff are the
// three values of QSWaveMixSetDistanceMapping. Past max the sound is silent.
// Inside min it has full level. Between the two the level falls as in
// DirectSound: min / (min + rolloff * (d - min)).
inline float QMixerDistanceGain(float distance, float min_distance,
                                float max_distance, float rolloff) {
  if (distance > max_distance) return 0.0f;
  if (distance <= min_distance || rolloff == 0.0f) return 1.0f;
  if (rolloff == 1.0f) return min_distance / distance;
  return min_distance / (min_distance + rolloff * (distance - min_distance));
}

// THE ROOM FACTOR. The game made each distance mapping for a listener in the
// world. In the God view and on the table the world is s units for each meter
// of the room. The factor k = s / reference, kept between lowest and highest,
// makes a world distance d sound as d / k. So one meter in the room sounds as
// `reference` units at every zoom. With s equal to the reference, nothing
// changes.
inline float RoomFactor(float units_per_meter, float reference,
                        float lowest, float highest) {
  if (!(units_per_meter > 0.0f) || !(reference > 0.0f)) return 1.0f;
  float k = units_per_meter / reference;
  if (k < lowest) k = lowest;
  if (k > highest) k = highest;
  return k;
}

// THE ZOOM (docs/audio.md, "6.3 Room distances"). With the room factor above, one
// meter of the room sounds the same at each zoom: a villager the size of an
// ant is as loud as a villager of life size. The player wants the other
// thing: zoomed out, the small sounds go away, and a crash or an explosion
// still carries. So the factor grows more slowly than the scale:
//
//   k = (pivot / reference) * (s / pivot) ^ exponent
//
// At the pivot scale (100 units a meter, the scale at the start) it is the
// room factor. With the exponent 1 it is the room factor at each scale.
// With 0.5, at 4000 units a meter one meter of the room sounds as 253 units:
// a sound with a maximum distance of 300 units is heard to 1.2 m, and one
// with 2000 units to 7.9 m.
inline float ZoomFactor(float units_per_meter, float reference, float pivot,
                        float exponent, float lowest, float highest) {
  if (!(units_per_meter > 0.0f) || !(reference > 0.0f) || !(pivot > 0.0f)) {
    return 1.0f;
  }
  float k = pivot / reference * powf(units_per_meter / pivot, exponent);
  if (k < lowest) k = lowest;
  if (k > highest) k = highest;
  return k;
}

// The mapping that QMixer gets: min and max times k, and the same rolloff.
// QMixerDistanceGain(d, k * min, k * max, r) equals
// QMixerDistanceGain(d / k, min, max, r), so this is the same as all
// distances divided by k.
inline void ScaleMapping(const float game[3], float k, float out[3]) {
  out[0] = game[0] * k;
  out[1] = game[1] * k;
  out[2] = game[2];
}

// True when two factors differ by more than share of the first. A new
// mapping goes to QMixer only then.
inline bool FactorChanged(float sent, float now, float share) {
  if (!(sent > 0.0f)) return true;
  return fabsf(now - sent) > share * sent;
}

// THE DISTANCE THAT DECIDES WHAT PLAYS (docs/audio.md, "6.4 The ears decide what plays"). The game
// starts a positional sound only within its maximum distance from the game
// camera (GAudio::PlaySoundEffect, 0x00429F16). The audio DLL stops it at
// that distance or more (LHSampleUpdate3DChannels, DLL RVA 0x143AB). With
// room distances, QMixer hears a sound up to max * k from the ears. So both
// tests get the distance from the ears divided by k. The game's maximum then
// gives the range that QMixer uses.
inline float RangeDistance(const float ears[3], const float point[3],
                           float k) {
  const float d[3] = {point[0] - ears[0], point[1] - ears[1],
                      point[2] - ears[2]};
  const float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  return k > 0.0f ? n / k : n;
}

// The start test of the game: a sound past its maximum distance does not
// start.
inline bool RangeStarts(float distance, float max_distance) {
  return !(distance > max_distance);
}

// The stop test of the DLL: a sound stops when it is not nearer than its
// maximum distance.
inline bool RangeStops(float distance, float max_distance) {
  return !(distance < max_distance);
}

// THE LOOP OF A BULK TAKE (docs/audio.md, "6.4 The ears decide what plays").
// While the Hand takes from a pile or a field, the game asks for a loop on
// each game turn: a positional sound with no object, at the place of the
// Hand. The game tests the start with the distance from its camera to that
// place. In the God view the game camera is 400 units from the player's
// place, and the loop has a maximum of 180 units, so the game refuses it on
// each turn. The ears decide the start, as for a sound with an object: head
// is the distance from the ears in world units, and k is the room factor.
//
// What decides the start of the loop on this ask.
enum TakeLoopRule {
  kTakeLoopGame = 0,   // the test of the game, with its camera
  kTakeLoopStart = 1,  // in range of the ears: the test of the game passes
  kTakeLoopRefuse = 2  // out of range of the ears: no start
};
inline int TakeLoopStart(bool on, bool by_ears, float head, float k,
                         float max_distance) {
  if (!on || !by_ears || !(k > 0.0f) || head < 0.0f) return kTakeLoopGame;
  return RangeStarts(head / k, max_distance) ? kTakeLoopStart
                                             : kTakeLoopRefuse;
}

// A LOOP OUT OF RANGE keeps its sample slot while the DLL has spare slots,
// because the game does not start each loop again. QMixer makes it silent.
// The reserve is half of the slots, and 4 at least: the DLL of the game has
// 16 slots. With fewer free slots than the reserve, a loop out of range
// stops, as in the game.
inline int SlotReserve(int slots) {
  const int reserve = slots / 2;
  return reserve < 4 ? 4 : reserve;
}

inline bool KeepSilentLoop(int free_slots, int slots) {
  return slots > 0 && free_slots >= SlotReserve(slots);
}

// THE AZIMUTH of a point in the frame of the ears, in degrees from 0 to 360.
// 0 is straight ahead and 90 is to the right. basis has the columns right,
// up and forward, row major (a column is read with a stride of three). False
// when the point is less than min_horizontal from the ears on the ground
// plane of the ears, and then the angle is not stable.
inline bool Azimuth(const float basis[9], const float ears[3],
                    const float point[3], float min_horizontal,
                    float* azimuth, float* distance) {
  const float d[3] = {point[0] - ears[0], point[1] - ears[1],
                      point[2] - ears[2]};
  const float r = basis[0] * d[0] + basis[3] * d[1] + basis[6] * d[2];
  const float f = basis[2] * d[0] + basis[5] * d[1] + basis[8] * d[2];
  *distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (sqrtf(r * r + f * f) < min_horizontal) return false;
  float a = atan2f(r, f) * 57.29578f;
  if (a < 0.0f) a += 360.0f;
  *azimuth = a;
  return true;
}

// THE ELEVATION of a point from the ears, in degrees. Up is positive.
inline float Elevation(const float basis[9], const float ears[3],
                       const float point[3]) {
  const float d[3] = {point[0] - ears[0], point[1] - ears[1],
                      point[2] - ears[2]};
  const float r = basis[0] * d[0] + basis[3] * d[1] + basis[6] * d[2];
  const float u = basis[1] * d[0] + basis[4] * d[1] + basis[7] * d[2];
  const float f = basis[2] * d[0] + basis[5] * d[1] + basis[8] * d[2];
  return atan2f(u, sqrtf(r * r + f * f)) * 57.29578f;
}

// THE RELEASE OF A VOICE OF THE MIXER (docs/audio.md, 7.4 and "7.6 The wait
// of a stop"). When the DLL flushes the channel of a voice, the level of the
// voice goes to 0 and a count runs. The count starts at ReleaseStart, and it
// goes down by 1 in each kReleaseSeconds. The slot of the voice is free at
// kReleaseEnd. So a voice has 80 ms from the flush to the free slot.
//
// The audio DLL waits kStopWaitSeconds between the volume 0 of a stop and
// the flush. When the mod skips that wait, the flush comes that much sooner,
// and the count starts that much higher. The slot is then free at the same
// time after the volume 0 as with the wait.
const float kReleaseSeconds = 0.008f;
const float kReleaseEnd = -9.0f;
const float kStopWaitSeconds = 0.020f;

inline float ReleaseStart(bool wait_skipped) {
  return 1.0f + (wait_skipped ? kStopWaitSeconds / kReleaseSeconds : 0.0f);
}

// The seconds from the flush of the channel to the free slot.
inline float ReleaseLength(float start) {
  return (start - kReleaseEnd) * kReleaseSeconds;
}

// A POINT IN THE WORLD from a direction in the frame of the ears (x right,
// y up, z forward, as the DLL reads a relative position), at distance from
// the ears. False when the direction has no length.
inline bool PointFromEars(const float basis[9], const float ears[3],
                          const float local[3], float distance,
                          float out[3]) {
  const float n = sqrtf(local[0] * local[0] + local[1] * local[1] +
                        local[2] * local[2]);
  if (!(n > 1e-4f)) return false;
  const float s = distance / n;
  for (int i = 0; i < 3; ++i) {
    out[i] = ears[i] + (basis[i * 3 + 0] * local[0] +
                        basis[i * 3 + 1] * local[1] +
                        basis[i * 3 + 2] * local[2]) * s;
  }
  return true;
}

}  // namespace audio_math
