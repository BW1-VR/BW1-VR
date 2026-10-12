// The way of a piece of the stream of a take at a distance: the math, with no
// game state (docs/grab.md, "The stream of a take at a distance").
// take_stream.cpp gives it the top of the source and the Hand, and writes the
// place that it gives to each piece. The math test runs it with no game.

#pragma once

#include <math.h>

namespace take_stream_math {

// THE CURVE. A cubic Bezier curve from the top of the source to the Hand, in
// world units, with y up:
//
//   P0   the top of the source
//   P1   straight over P0, at the height of the top level
//   P2   over the way to the Hand, kLean of the distance along the floor
//        from the source, at the height of the top level
//   P3   the Hand
//
// The top level is kArc of the distance along the floor over the higher one
// of the two ends. So a piece goes straight up out of the source first, then
// over to the Hand, and it comes down into the Hand. The shape is the same
// for a short way and for a long way, because each measure is a share of the
// way.
//
// No point of the curve is lower than the lower end, because no control
// point is. With a Hand that is not straight over the source, the top of the
// curve is over both ends. So the pieces do not go through flat land between
// the source and the Hand. With the Hand straight over the source the curve
// is the straight line up, as in the plain game.
const float kArc = 0.25f;
const float kLean = 0.5f;

struct Curve {
  float p[4][3];
};

inline bool Finite(float v) { return v == v && v > -1.0e9f && v < 1.0e9f; }

inline bool Finite3(const float v[3]) {
  return Finite(v[0]) && Finite(v[1]) && Finite(v[2]);
}

// WHETHER THE MOD PLACES THE PIECES. The key must be on (take_stream=1), the
// far grab must have the source of a take at a distance, and the two ends
// must be numbers. In each other case the line of the game stays: a take
// with the Hand at the source, and take_stream=0.
inline bool Places(bool on, bool far_take, bool ends_ok) {
  return on && far_take && ends_ok;
}

// THE TOP OF A SOURCE. point_y is the height of the point of its drawn mesh,
// which is the base of the mesh. height is what the game gives for the
// height of the Hand over the source in a take. A height that is no sane
// number gives the point.
inline float Top(float point_y, float height, float max_height) {
  if (!(height > 0.0f) || !(height <= max_height)) return point_y;
  return point_y + height;
}

inline Curve Make(const float source[3], const float hand[3]) {
  Curve c;
  const float dx = hand[0] - source[0];
  const float dz = hand[2] - source[2];
  const float floor = sqrtf(dx * dx + dz * dz);
  const float high = source[1] > hand[1] ? source[1] : hand[1];
  const float level = high + kArc * floor;
  for (int i = 0; i < 3; ++i) {
    c.p[0][i] = source[i];
    c.p[3][i] = hand[i];
  }
  c.p[1][0] = source[0];
  c.p[1][1] = level;
  c.p[1][2] = source[2];
  c.p[2][0] = source[0] + kLean * dx;
  c.p[2][1] = level;
  c.p[2][2] = source[2] + kLean * dz;
  return c;
}

// The share of its life that a piece has behind it: 0 at its start, 1 at its
// end. A life that is no positive number gives 1, the Hand.
inline float Share(float age, float life) {
  if (!(life > 0.0f) || !Finite(age)) return 1.0f;
  const float t = age / life;
  return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// The place of a piece at the share t of its life.
inline void Point(const Curve& c, float t, float out[3]) {
  const float u = 1.0f - t;
  const float a = u * u * u;
  const float b = 3.0f * u * u * t;
  const float d = 3.0f * u * t * t;
  const float e = t * t * t;
  for (int i = 0; i < 3; ++i) {
    out[i] = a * c.p[0][i] + b * c.p[1][i] + d * c.p[2][i] + e * c.p[3][i];
  }
}

// The velocity of a piece at the share t of its life, in world units a
// second. life is in seconds. The tumble rule of the piece of wood reads it:
// the speed gives the rate of the turn, and the larger one of x and z gives
// the axis.
inline void Velocity(const Curve& c, float t, float life, float out[3]) {
  const float u = 1.0f - t;
  const float a = 3.0f * u * u;
  const float b = 6.0f * u * t;
  const float d = 3.0f * t * t;
  const float rate = life > 0.0f ? 1.0f / life : 0.0f;
  for (int i = 0; i < 3; ++i) {
    out[i] = rate * (a * (c.p[1][i] - c.p[0][i]) + b * (c.p[2][i] - c.p[1][i]) +
                     d * (c.p[3][i] - c.p[2][i]));
  }
}

// The length of the way, as the sum of steps straight lines. For the log.
inline float Length(const Curve& c, int steps) {
  if (steps < 1) steps = 1;
  float sum = 0.0f;
  float last[3];
  Point(c, 0.0f, last);
  for (int i = 1; i <= steps; ++i) {
    float now[3];
    Point(c, static_cast<float>(i) / static_cast<float>(steps), now);
    const float dx = now[0] - last[0];
    const float dy = now[1] - last[1];
    const float dz = now[2] - last[2];
    sum += sqrtf(dx * dx + dy * dy + dz * dz);
    for (int k = 0; k < 3; ++k) last[k] = now[k];
  }
  return sum;
}

// How far the highest point of the curve is over the higher one of its two
// ends, from steps points. For the log.
inline float Rise(const Curve& c, int steps) {
  if (steps < 1) steps = 1;
  const float high = c.p[0][1] > c.p[3][1] ? c.p[0][1] : c.p[3][1];
  float top = high;
  for (int i = 0; i <= steps; ++i) {
    float now[3];
    Point(c, static_cast<float>(i) / static_cast<float>(steps), now);
    if (now[1] > top) top = now[1];
  }
  return top - high;
}

}  // namespace take_stream_math
