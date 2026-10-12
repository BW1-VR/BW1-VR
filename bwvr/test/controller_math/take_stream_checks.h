// THE STREAM OF A TAKE AT A DISTANCE (docs/grab.md, "The stream of a take at
// a distance"). main.cpp includes this file after Check and Near, just before
// main.
#pragma once

#include "../../src/mod/take_stream_math.h"

namespace take_stream_checks {

// The lowest and the highest y of a curve, and whether a piece ever goes back
// along the floor from the source to the Hand, from 200 points.
struct Shape {
  float low;
  float high;
  bool back;
};

inline Shape ShapeOf(const take_stream_math::Curve& c) {
  Shape s = {c.p[0][1], c.p[0][1], false};
  const float dx = c.p[3][0] - c.p[0][0];
  const float dz = c.p[3][2] - c.p[0][2];
  float along = 0.0f;
  for (int i = 0; i <= 200; ++i) {
    float p[3];
    take_stream_math::Point(c, static_cast<float>(i) / 200.0f, p);
    if (p[1] < s.low) s.low = p[1];
    if (p[1] > s.high) s.high = p[1];
    const float now = (p[0] - c.p[0][0]) * dx + (p[2] - c.p[0][2]) * dz;
    if (now < along - 1e-3f) s.back = true;
    along = now;
  }
  return s;
}

inline void TakeStreamChecks() {
  using namespace take_stream_math;
  printf("\nthe stream of a take at a distance\n");

  // THE RULE OF THE KEY.
  Check(Places(true, true, true),
        "a take at a distance: the mod places the pieces (take_stream=1)", 1,
        1);
  Check(!Places(false, true, true),
        "with take_stream=0 the line of the game stays", 0, 0);
  Check(!Places(true, false, true),
        "a take with the Hand at the source keeps the line of the game", 0, 0);
  Check(!Places(true, true, false),
        "an end that is no number keeps the line of the game", 0, 0);

  // THE TOP OF A SOURCE.
  Near("the top is the point of the mesh plus the height of the game",
       Top(12.0f, 3.5f, 2000.0f), 15.5, 1e-5);
  Near("a height of 0 gives the point", Top(12.0f, 0.0f, 2000.0f), 12.0, 1e-5);
  Near("a height below 0 gives the point", Top(12.0f, -4.0f, 2000.0f), 12.0,
       1e-5);
  Near("a height past the limit gives the point",
       Top(12.0f, 5000.0f, 2000.0f), 12.0, 1e-5);
  const float nan = nanf("");
  Near("a height that is no number gives the point",
       Top(12.0f, nan, 2000.0f), 12.0, 1e-5);

  // THE SHARE OF THE LIFE.
  Near("a new piece is at the start", Share(0.0f, 1.0f), 0.0, 1e-6);
  Near("half of the life", Share(0.5f, 1.0f), 0.5, 1e-6);
  Near("a life of 2 s", Share(0.5f, 2.0f), 0.25, 1e-6);
  Near("a piece past its life is at the Hand", Share(1.4f, 1.0f), 1.0, 1e-6);
  Near("an age below 0 is the start", Share(-0.1f, 1.0f), 0.0, 1e-6);
  Near("a life of 0 gives the Hand", Share(0.3f, 0.0f), 1.0, 1e-6);

  // A WAY OF 60 UNITS ALONG THE FLOOR, the Hand 20 units over the top of the
  // source: about 0.6 m and 0.2 m at 100 units a meter.
  const float source[3] = {1000.0f, 50.0f, 2000.0f};
  const float hand[3] = {1036.0f, 70.0f, 2048.0f};
  const Curve c = Make(source, hand);
  float p[3];
  Point(c, 0.0f, p);
  Near("the start is the top of the source, x", p[0], source[0], 1e-3);
  Near("the start is the top of the source, y", p[1], source[1], 1e-3);
  Near("the start is the top of the source, z", p[2], source[2], 1e-3);
  Point(c, 1.0f, p);
  Near("the end is the Hand, x", p[0], hand[0], 1e-3);
  Near("the end is the Hand, y", p[1], hand[1], 1e-3);
  Near("the end is the Hand, z", p[2], hand[2], 1e-3);

  float v[3];
  Velocity(c, 0.0f, 1.0f, v);
  Near("a piece starts straight up: no speed along x", v[0], 0.0, 1e-3);
  Near("a piece starts straight up: no speed along z", v[2], 0.0, 1e-3);
  Check(v[1] > 0.0f, "a piece starts straight up: it goes up", v[1], 1);
  // The top level is 20 + 0.25 * 60 = 35 units over the source.
  Near("the speed at the start is 3 times the way to the top level", v[1],
       105.0, 1e-2);
  Velocity(c, 1.0f, 1.0f, v);
  Check(v[1] < 0.0f, "a piece comes down into the Hand", v[1], -1);
  Check(v[0] > 0.0f && v[2] > 0.0f,
        "and it comes from the side of the source", v[0], 1);
  Near("the speed along x at the Hand is 1.5 times the way", v[0], 54.0, 1e-2);
  Near("the speed along z at the Hand is 1.5 times the way", v[2], 72.0, 1e-2);

  Shape s = ShapeOf(c);
  Check(s.low >= source[1] - 1e-3f,
        "the Hand is higher: no point is lower than the top of the source",
        s.low, source[1]);
  Check(s.high > hand[1] + 1.0f,
        "the Hand is higher: the top of the curve is over the Hand", s.high,
        hand[1]);
  Check(!s.back, "a piece never goes back along the floor", 0, 0);
  Near("the rise over the higher end is what the shape has",
       Rise(c, 200), s.high - hand[1], 1e-3);

  // THE VELOCITY IS THE CHANGE OF THE PLACE, also with a life that is not
  // 1 s.
  {
    const float life = 2.0f;
    const float t = 0.37f;
    const float h = 1e-2f;
    float a[3], b[3];
    Point(c, t - h, a);
    Point(c, t + h, b);
    Velocity(c, t, life, v);
    for (int i = 0; i < 3; ++i) {
      Near("the velocity is the change of the place in a second",
           v[i], (b[i] - a[i]) / (2.0f * h * life), 0.05);
    }
    Velocity(c, t, 0.0f, v);
    Near("a life of 0 gives no velocity", v[1], 0.0, 1e-6);
  }

  // FLAT LAND: the two ends at one height, a short way and a long way.
  const float ways[3] = {13.0f, 60.0f, 200.0f};
  for (int i = 0; i < 3; ++i) {
    const float from[3] = {0.0f, 10.0f, 0.0f};
    const float to[3] = {ways[i], 10.0f, 0.0f};
    const Curve flat = Make(from, to);
    s = ShapeOf(flat);
    Check(s.low >= 10.0f - 1e-3f,
          "flat land: no point of the curve is lower than the ends", s.low,
          10.0);
    // 3 * 0.25 * 0.25 of the way, at half of the life.
    Near("flat land: the top of the curve is 0.1875 of the way over the ends",
         s.high - 10.0f, 0.1875f * ways[i], 0.01f * ways[i]);
    Check(!s.back, "flat land: a piece never goes back", 0, 0);
    Check(Length(flat, 64) > ways[i],
          "flat land: the way is longer than the straight line",
          Length(flat, 64), ways[i]);
    Check(Length(flat, 64) < 1.25f * ways[i],
          "flat land: and less than 1.25 times as long", Length(flat, 64),
          1.25f * ways[i]);
  }

  // THE SHAPE IS THE SAME AT EACH LENGTH: 10 times the way gives 10 times
  // each offset from the source.
  {
    const float from[3] = {5.0f, 2.0f, -3.0f};
    const float near_hand[3] = {5.0f + 6.0f, 2.0f + 2.0f, -3.0f + 8.0f};
    const float far_hand[3] = {5.0f + 60.0f, 2.0f + 20.0f, -3.0f + 80.0f};
    const Curve a = Make(from, near_hand);
    const Curve b = Make(from, far_hand);
    float pa[3], pb[3];
    Point(a, 0.3f, pa);
    Point(b, 0.3f, pb);
    for (int i = 0; i < 3; ++i) {
      Near("10 times the way gives 10 times the offset",
           pb[i] - from[i], 10.0f * (pa[i] - from[i]), 1e-3);
    }
  }

  // THE HAND IS LOWER THAN THE TOP OF THE SOURCE: a pile on a hill.
  {
    const float from[3] = {0.0f, 80.0f, 0.0f};
    const float to[3] = {0.0f, 50.0f, 100.0f};
    const Curve down = Make(from, to);
    s = ShapeOf(down);
    Check(s.low >= 50.0f - 1e-3f,
          "the Hand is lower: no point is lower than the Hand", s.low, 50.0);
    Check(s.high > 80.0f + 1.0f,
          "the Hand is lower: the piece goes up out of the source first",
          s.high, 80.0);
    Check(!s.back, "the Hand is lower: a piece never goes back", 0, 0);
  }

  // THE HAND IS STRAIGHT OVER THE SOURCE: the straight line up, as in the
  // plain game, and no point over the Hand.
  {
    const float from[3] = {7.0f, 10.0f, 9.0f};
    const float to[3] = {7.0f, 40.0f, 9.0f};
    const Curve up = Make(from, to);
    s = ShapeOf(up);
    Near("the Hand over the source: the highest point is the Hand", s.high,
         40.0, 1e-3);
    Near("the Hand over the source: the lowest point is the source", s.low,
         10.0, 1e-3);
    Point(up, 0.4f, p);
    Near("the Hand over the source: no offset along x", p[0], 7.0, 1e-4);
    Near("the Hand over the source: no offset along z", p[2], 9.0, 1e-4);
    Near("the Hand over the source: the way is the straight line",
         Length(up, 64), 30.0, 1e-2);
    Near("the Hand over the source: no rise over the Hand", Rise(up, 64), 0.0,
         1e-3);
  }

  // THE ENDS ARE NUMBERS.
  {
    const float bad[3] = {1.0f, nan, 3.0f};
    Check(!Finite3(bad), "an end with a value that is no number is refused",
          0, 0);
    Check(Finite3(source), "an end with 3 numbers is accepted", 1, 1);
  }
}

}  // namespace take_stream_checks
