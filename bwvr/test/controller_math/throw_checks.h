// THE THROW OF A REAL HAND (docs/throw.md, "The throw physics").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/throw_math.h"

namespace throw_checks {

// A SWING for the checks. The hand goes along a direction. Its speed rises
// to `top` m/s over `rise` seconds, as half a cosine, and falls to 0 over
// `stop` seconds in the same way. The samples come `rate` times a second and
// end `after` seconds after the top speed. The points are in units at the
// life size scale (7.5 in a meter), as the history of the direct hand.
struct Swing {
  controller_math::MotionSample samples[64];
  int count;
  double end;  // the time of the last sample
};

inline double SwingSpeed(double t, double top, double rise, double stop) {
  const double pi = 3.14159265358979;
  if (t < -rise || t > stop) return 0.0;
  if (t <= 0.0) return top * 0.5 * (1.0 + cos(pi * t / rise));
  return top * 0.5 * (1.0 + cos(pi * t / stop));
}

inline Swing MakeSwing(double top, double rise, double stop, double after,
                       double rate, const double direction[3]) {
  Swing s = {};
  const double life = 7.5;
  const double step = 1.0 / rate;
  const int n = 40;
  // The path is the sum of the speed in steps of 0.1 ms.
  for (int k = 0; k < n; ++k) {
    const double t = after - (n - 1 - k) * step;  // 0 is the top speed
    double path = 0.0;
    for (double u = -rise; u < t; u += 0.0001) {
      path += SwingSpeed(u, top, rise, stop) * 0.0001;
    }
    s.samples[k].time = 100.0 + t;
    for (int i = 0; i < 3; ++i) {
      s.samples[k].point[i] = static_cast<float>(path * direction[i] * life);
    }
  }
  s.count = n;
  s.end = 100.0 + after;
  return s;
}

inline double Mps(const float v[3]) {
  return controller_math::Length(v) / 7.5;
}

// A BALL ON THE CONTACT OF THE GAME, for the checks of the low bounce. The
// ball has one point, under its center, and the land is at the height 0.
// fn_007FD200 puts the point 0.06 s ahead along the velocity. fn_007FDE40
// gives the force of the contact: `spring` times the depth on the first step
// in the land, and after that the force of the first depth plus 200 times
// `spring2` times the change of the depth, never below 0. The values are for
// each unit of mass, and those of a rock are 21.56 and 1.328. ReboundFactor cuts the force as
// throw_physics.cpp does. The ball falls on the land with the speed `down`. *up is the speed with which it
// leaves the land the first time (0 when it stays), and *depth and *speed are
// the depth of the point and the speed of the ball 20 s later.
inline void BallOnLand(double down, float keep, double* up, double* depth,
                       double* speed) {
  const double spring = 21.56, spring2 = 1.328, step = 0.005;
  double y = 0.06 * down + 0.5, v = -down, first = 0.0;
  bool touched = false, left = false;
  *up = 0.0;
  for (int k = 0; k < 4000; ++k) {
    const double d = -(y + 0.06 * v);
    double force = 0.0;
    if (d > 0.0) {
      if (first == 0.0) {
        first = d;
        force = spring * d;
      } else {
        force = spring * first + 200.0 * spring2 * (d - first);
      }
      if (force < 0.0) force = 0.0;
      touched = true;
    } else {
      first = 0.0;
      if (touched && !left && v > 0.0) {
        left = true;
        *up = v;
      }
    }
    const float f3[3] = {0.0f, static_cast<float>(force), 0.0f};
    const float v3[3] = {0.0f, static_cast<float>(v), 0.0f};
    force *= throw_math::ReboundFactor(f3, v3, keep, throw_math::kReboundSoft);
    v += (force - 9.81) * step;
    y += v * step;
  }
  *depth = -y;
  *speed = fabs(v);
}

inline void ThrowChecks() {
  using namespace throw_math;
  using controller_math::MeanVelocity;
  using controller_math::MotionSample;
  printf("\nThe throw of a real hand\n");

  const double kForward[3] = {0.6, 0.0, 0.8};
  const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

  // THE TOP SPEED. A swing to 6 m/s in 150 ms that stops in 200 ms. The grip
  // reads as open 44 ms after the top speed (4 frames at 90 Hz).
  {
    const Swing s = MakeSwing(6.0, 0.150, 0.200, 0.044, 90.0, kForward);
    float mean[3], peak[3];
    double at = 0.0;
    const bool have_mean = MeanVelocity(s.samples, s.count, 0.080, mean);
    const bool have_peak = PeakVelocity(s.samples, s.count,
                                        s.end - kPeakWindow, s.end,
                                        kPeakHalfSpan, peak, &at);
    Check(have_mean && have_peak, "a swing gives both speeds", have_peak, 1);
    printf("    a swing to 6 m/s, opened 44 ms late: the mean of 80 ms is "
           "%.2f m/s, the top speed is %.2f m/s\n", Mps(mean), Mps(peak));
    Check(Mps(mean) < Mps(peak), "the mean of the 80 ms before a late "
          "opening is less than the top speed", Mps(mean), Mps(peak));
    Near("the top speed of the last 120 ms is the speed of the swing",
         Mps(peak), 6.0, 0.25);
    Near("the top speed is found at the top of the swing", at, 100.0, 0.012);
    Near("its direction is the direction of the swing, x", peak[0] /
         controller_math::Length(peak), 0.6, 1e-3);
    Near("its direction is the direction of the swing, z", peak[2] /
         controller_math::Length(peak), 0.8, 1e-3);
  }
  // The grip reads as open 100 ms after the top speed: the hand is slow.
  {
    const Swing s = MakeSwing(6.0, 0.150, 0.200, 0.100, 90.0, kForward);
    float mean[3], peak[3];
    MeanVelocity(s.samples, s.count, 0.080, mean);
    PeakVelocity(s.samples, s.count, s.end - kPeakWindow, s.end,
                 kPeakHalfSpan, peak, nullptr);
    printf("    the same swing, opened 100 ms late: the mean of 80 ms is "
           "%.2f m/s, the top speed is %.2f m/s\n", Mps(mean), Mps(peak));
    Check(Mps(mean) < 5.0, "100 ms late, the mean of 80 ms lost a sixth or "
          "more", Mps(mean), 5.0);
    Near("100 ms late, the top speed is still the speed of the swing",
         Mps(peak), 6.0, 0.25);
  }
  // The opening at the top speed, and the release two frames later.
  {
    const Swing s = MakeSwing(4.0, 0.150, 0.200, 0.022, 90.0, kForward);
    float peak[3];
    PeakVelocity(s.samples, s.count, s.end - 0.022 - kPeakWindow, s.end,
                 kPeakHalfSpan, peak, nullptr);
    Near("an opening at the top speed gives the top speed", Mps(peak), 4.0,
         0.15);
  }
  // Other frame rates.
  {
    const Swing a = MakeSwing(6.0, 0.150, 0.200, 0.050, 72.0, kForward);
    const Swing b = MakeSwing(6.0, 0.150, 0.200, 0.050, 120.0, kForward);
    float peak[3];
    PeakVelocity(a.samples, a.count, a.end - kPeakWindow, a.end,
                 kPeakHalfSpan, peak, nullptr);
    Near("at 72 samples a second the top speed is the same", Mps(peak), 6.0,
         0.3);
    PeakVelocity(b.samples, b.count, b.end - kPeakWindow, b.end,
                 kPeakHalfSpan, peak, nullptr);
    Near("at 120 samples a second the top speed is the same", Mps(peak), 6.0,
         0.3);
  }
  // A constant speed, and too few samples.
  {
    MotionSample line[8] = {};
    for (int k = 0; k < 8; ++k) {
      line[k].time = 5.0 + k / 90.0;
      line[k].point[0] = static_cast<float>(2.0 * 7.5 * k / 90.0);
    }
    float peak[3] = {0, 0, 0};
    const bool ok = PeakVelocity(line, 8, 5.0, 6.0, kPeakHalfSpan, peak,
                                 nullptr);
    Check(ok, "a constant speed gives a top speed", ok, 1);
    Near("a constant 2 m/s reads as 2 m/s", Mps(peak), 2.0, 1e-3);
    const bool one = PeakVelocity(line, 1, 5.0, 6.0, kPeakHalfSpan, peak,
                                  nullptr);
    Check(!one, "one sample gives no speed", one, 0);
    // Only the samples up to `to` count.
    line[7].point[0] += 30.0f;
    PeakVelocity(line, 8, 5.0, line[6].time, kPeakHalfSpan, peak, nullptr);
    Near("a sample after the window does not count", Mps(peak), 2.0, 1e-3);
  }

  // THE SET DOWN. gain 1.5 and drop speed 2, as in the mod.
  {
    const float still[3] = {0.5f, 0.3f, 0.2f};       // 0.07 m/s across
    const float carry[3] = {1.0f, 0.0f, 0.5f};       // 0.15 m/s across
    const float moving[3] = {2.0f, 0.0f, 1.0f};      // 0.30 m/s across
    const float flick[3] = {15.0f, 5.0f, 10.0f};     // 2.5 m/s
    const float slow_peak[3] = {4.0f, 0.0f, 3.0f};   // 0.67 m/s
    Check(SetDown(still, still, 1.5f, 7.5f, 2.0f, kSwingMps),
          "a still hand sets the object down", 1, 1);
    Check(SetDown(carry, slow_peak, 1.5f, 7.5f, 2.0f, kSwingMps),
          "a hand that came to rest from 0.67 m/s sets the object down", 1,
          1);
    Check(!SetDown(moving, moving, 1.5f, 7.5f, 2.0f, kSwingMps),
          "a hand at 0.30 m/s across the land throws", 0, 0);
    Check(!SetDown(still, flick, 1.5f, 7.5f, 2.0f, kSwingMps),
          "a still hand after a flick of 2.5 m/s throws", 0, 0);
    // A hand that went at 1.5 m/s and stopped 200 ms before the opening.
    const Swing s = MakeSwing(1.5, 0.150, 0.100, 0.300, 90.0, kForward);
    float mean[3], peak[3];
    MeanVelocity(s.samples, s.count, 0.080, mean);
    PeakVelocity(s.samples, s.count, s.end - kPeakWindow, s.end,
                 kPeakHalfSpan, peak, nullptr);
    Check(SetDown(mean, peak, 1.5f, 7.5f, 2.0f, kSwingMps),
          "a move that stopped 200 ms before the opening is a set down",
          Mps(peak), 0.0);
    RealThrow plan;
    RealThrowPlan(mean, peak, identity, 1.5f, 7.5f, 100.0f, 124.0f, 2.0f,
                  &plan);
    Check(plan.drop && plan.sent[0] == 0.0f && plan.sent[1] == 0.0f &&
          plan.sent[2] == 0.0f, "a set down sends zero", plan.drop, 1);
    // A flick of 2.5 m/s that stopped 60 ms before the opening.
    const Swing f = MakeSwing(2.5, 0.100, 0.050, 0.060, 90.0, kForward);
    MeanVelocity(f.samples, f.count, 0.080, mean);
    PeakVelocity(f.samples, f.count, f.end - kPeakWindow, f.end,
                 kPeakHalfSpan, peak, nullptr);
    RealThrowPlan(mean, peak, identity, 1.5f, 7.5f, 100.0f, 124.0f, 2.0f,
                  &plan);
    Check(!plan.drop, "a flick that stopped 60 ms before the opening throws",
          plan.drop, 0);
    Near("the flick throws at its top speed", plan.real_mps, 2.5, 0.4);
  }

  // THE HEIGHT RULE. A still hand over the limit lets the object fall.
  {
    Check(!FallsFromHeight(0.05f, 0.15f), "5 cm over the land is a set down",
          0, 0);
    Check(!FallsFromHeight(0.15f, 0.15f), "at the limit is a set down", 0, 0);
    Check(FallsFromHeight(0.30f, 0.15f), "30 cm over the land is a fall", 1,
          1);
    Check(!FallsFromHeight(2.0f, 0.0f), "a limit of 0 is the set down at each "
          "height", 0, 0);
    Check(FallsFromHeight(-1.0f, 0.15f) == false, "a point under the land is "
          "no fall", 0, 0);
    const float still[3] = {0.5f, -0.3f, 0.2f};  // 0.07 m/s across, 0.04 down
    for (int k = 0; k < 3; ++k) {
      const float scales[3] = {7.5f, 67.0f, 1000.0f};
      const float s = scales[k];
      RealThrow plan;
      RealThrowPlan(still, still, identity, 1.5f, 7.5f, s, 124.0f, 2.0f,
                    &plan, 2.0f, false);
      Check(plan.drop && !plan.fall, "a still hand near the land sets down",
            plan.drop, 1);
      RealThrowPlan(still, still, identity, 1.5f, 7.5f, s, 124.0f, 2.0f,
                    &plan, 2.0f, true);
      Check(!plan.drop && plan.fall, "a still hand high up lets it fall",
            plan.fall, 1);
      Near("the fall has the velocity of the hand, across the land",
           sqrt(plan.world[0] * plan.world[0] + plan.world[2] * plan.world[2]),
           sqrt(0.5 * 0.5 + 0.2 * 0.2) / 7.5 * s, 1e-3 * s);
      Near("and down", plan.world[1], -0.3 / 7.5 * s, 1e-3 * s);
      Check(plan.sent[0] * plan.sent[0] + plan.sent[2] * plan.sent[2] >
                2.0f * 2.0f,
            "the release sends more than the speed of a set down", 0, 0);
    }
    // A hand with no motion at all falls from rest, and the release still
    // sends a speed across the land.
    const float none[3] = {0.0f, 0.0f, 0.0f};
    RealThrow rest;
    RealThrowPlan(none, none, identity, 1.5f, 7.5f, 67.0f, 124.0f, 2.0f,
                  &rest, 2.0f, true);
    Check(rest.fall && !rest.drop, "a hand at rest high up falls", rest.fall,
          1);
    Near("from rest the real flight has no velocity",
         controller_math::Length(rest.world), 0.0, 1e-6);
    Near("and the release sends kSendFlat across the land",
         sqrt(rest.sent[0] * rest.sent[0] + rest.sent[2] * rest.sent[2]),
         kSendFlat, 1e-4);
    // A throw is a throw at each height.
    const float fast[3] = {15.0f, 5.0f, 10.0f};
    RealThrow thrown;
    RealThrowPlan(fast, fast, identity, 1.5f, 7.5f, 67.0f, 124.0f, 2.0f,
                  &thrown, 2.0f, true);
    Check(!thrown.drop && !thrown.fall, "a throw is no fall", thrown.fall, 0);
  }

  // THE PLAN. A hand at 5 m/s, 30 degrees up, in a room turned 90 degrees.
  {
    const float c = cosf(30.0f * 3.14159265f / 180.0f);
    const float sn = sinf(30.0f * 3.14159265f / 180.0f);
    const float v[3] = {0.0f, 5.0f * 7.5f * sn, 5.0f * 7.5f * c};
    // The basis L turns the room's +z to the world's +x.
    const float turned[9] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    const float scales[3] = {7.5f, 100.0f, 1000.0f};
    for (int k = 0; k < 3; ++k) {
      const float s = scales[k];
      RealThrow plan;
      RealThrowPlan(v, v, turned, 1.5f, 7.5f, s, 124.0f, 2.0f, &plan);
      char what[160];
      sprintf_s(what, "at %.1f units per meter the real flight is s v", s);
      Near(what, controller_math::Length(plan.world), 5.0 * s, 5.0 * s * 1e-4);
      sprintf_s(what, "at %.1f units per meter the game's velocity is "
                "sqrt(s) v", s);
      Near(what, controller_math::Length(plan.game), 5.0 * sqrt(s),
           5.0 * sqrt(s) * 1e-4);
      float want[3];
      controller_math::Apply(turned, false, v, want);
      const float len = controller_math::Length(want);
      double off = 0.0;
      for (int i = 0; i < 3; ++i) {
        off += fabs(plan.world[i] / (5.0 * s) - want[i] / len);
      }
      sprintf_s(what, "at %.1f units per meter the flight goes where the "
                "hand went, in the world", s);
      Near(what, off, 0.0, 1e-4);
      sprintf_s(what, "at %.1f units per meter the release sends the cap at "
                "most", s);
      Check(controller_math::Length(plan.sent) <= 124.001f, what,
            controller_math::Length(plan.sent), 124.0);
    }
    RealThrow big;
    RealThrowPlan(v, v, turned, 1.5f, 7.5f, 1000.0f, 124.0f, 2.0f, &big);
    Near("at 1000 units per meter the game's velocity is over the cap",
         controller_math::Length(big.game), 158.1, 0.1);
    Near("and the release sends the cap", controller_math::Length(big.sent),
         124.0, 0.01);
    // A toss straight up: the release still sends a speed across the land.
    const float up[3] = {0.0f, 3.0f * 7.5f, 0.0f};
    RealThrow toss;
    RealThrowPlan(up, up, identity, 1.5f, 7.5f, 67.0f, 124.0f, 2.0f, &toss);
    Check(!toss.drop, "a toss straight up at 3 m/s is a throw", toss.drop, 0);
    Near("a toss straight up sends 2.5 units/s across the land",
         sqrt(toss.sent[0] * toss.sent[0] + toss.sent[2] * toss.sent[2]),
         kSendFlat, 1e-4);
    Near("but the real flight has no speed across the land",
         sqrt(toss.world[0] * toss.world[0] + toss.world[2] * toss.world[2]),
         0.0, 1e-4);
  }

  // THE ARC IN THE ROOM. The game's own step flies the object, sqrt(s)
  // times in each substep. The path, in meters of the room, is the path of a
  // real throw at 5 m/s and 40 degrees up, from a height of 1.0 m.
  {
    const float scales[3] = {7.5f, 100.0f, 1000.0f};
    const double tolerance[3] = {0.03, 0.01, 0.01};
    const double g = 9.81, h = 1.0;
    const double vx = 5.0 * cos(40.0 * 3.14159265 / 180.0);
    const double vy = 5.0 * sin(40.0 * 3.14159265 / 180.0);
    const double real_time = (vy + sqrt(vy * vy + 2.0 * g * h)) / g;
    const double real_range = far_checks::Range(vx, vy, g, h);
    for (int k = 0; k < 3; ++k) {
      const float s = scales[k];
      const float scale = sqrtf(s);
      float p[3] = {0.0f, static_cast<float>(h * s), 0.0f};
      float v[3] = {static_cast<float>(vx * scale),
                    static_cast<float>(vy * scale), 0.0f};
      float remainder = 0.0f;
      double worst = 0.0, land_time = -1.0, land_x = 0.0, top = 0.0;
      int steps = 0, substeps = 0;
      // 1.5 s of the room, in substeps of 5 ms.
      for (int sub = 0; sub < 300 && land_time < 0.0; ++sub) {
        const int n = MiniSteps(scale, &remainder);
        for (int j = 0; j < n; ++j) {
          GameFreeStep(p, v);
          ++steps;
          const double t = steps * static_cast<double>(kSubstep) / scale;
          const double x_room = p[0] / s, y_room = p[1] / s;
          const double x_real = vx * t;
          const double y_real = h + vy * t - 0.5 * g * t * t;
          const double off = sqrt((x_room - x_real) * (x_room - x_real) +
                                  (y_room - y_real) * (y_room - y_real));
          if (off > worst) worst = off;
          if (y_room > top) top = y_room;
          if (y_room <= 0.0) {
            land_time = t;
            land_x = x_room;
            break;
          }
        }
        ++substeps;
      }
      char what[160];
      sprintf_s(what, "at %.1f units per meter the arc in the room is the "
                "real arc (worst distance, m)", s);
      Near(what, worst, 0.0, tolerance[k]);
      sprintf_s(what, "at %.1f units per meter the flight lasts as long as "
                "the real flight (s)", s);
      Near(what, land_time, real_time, 0.01);
      sprintf_s(what, "at %.1f units per meter the object lands where the "
                "real throw lands (m)", s);
      Near(what, land_x, real_range, 0.04);
      sprintf_s(what, "at %.1f units per meter the top of the arc is the "
                "real top (m)", s);
      Near(what, top, h + vy * vy / (2.0 * g), tolerance[k]);
      // The substeps are the real time: each one is 5 ms of the room.
      sprintf_s(what, "at %.1f units per meter the flight takes that time "
                "in substeps of 5 ms", s);
      Near(what, substeps * 0.005, real_time, 0.011);
      // The speed in the game stays under the cap at 100 units per meter.
      if (k == 1) {
        Check(5.0 * scale < 124.0, "at 100 units per meter a throw of 5 m/s "
              "is under the game's cap", 5.0 * scale, 124.0);
      }
    }
    // The square root law alone flies sqrt(s) times longer.
    Near("with the square root law alone the flight at 100 units per meter "
         "lasts 10 times longer", real_time * sqrt(100.0), real_time * 10.0,
         1e-9);
  }

  // THE TIME FACTOR k. The same throw (5 m/s at 40 degrees from 1.0 m)
  // flies the same arc, k times slower: the landing point is the real one and
  // the flight takes k times as long. The game's velocity is sqrt(s) v for
  // each k, and the steps in a substep are sqrt(s) / k. k is cut to sqrt(s).
  {
    const float scales[3] = {7.5f, 100.0f, 1000.0f};
    const float factors[3] = {1.0f, 2.0f, 4.0f};
    const double g = 9.81, h = 1.0;
    const double vx = 5.0 * cos(40.0 * 3.14159265 / 180.0);
    const double vy = 5.0 * sin(40.0 * 3.14159265 / 180.0);
    const double real_time = (vy + sqrt(vy * vy + 2.0 * g * h)) / g;
    const double real_range = far_checks::Range(vx, vy, g, h);
    const float ident9[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int a = 0; a < 3; ++a) {
      for (int b = 0; b < 3; ++b) {
        const float s = scales[a];
        const float life = 7.5f;
        const float v[3] = {static_cast<float>(vx * life),
                            static_cast<float>(vy * life), 0.0f};
        RealThrow plan;
        RealThrowPlan(v, v, ident9, 1.5f, life, s, 1.0e9f, 2.0f, &plan,
                      factors[b]);
        const float want_k = factors[b] < sqrtf(s) ? factors[b] : sqrtf(s);
        char what[200];
        sprintf_s(what, "at %.1f units per meter and k = %.1f the factor in "
                  "use is k, or sqrt(s) at most", s, factors[b]);
        Near(what, plan.slow, want_k, 1e-4);
        sprintf_s(what, "at %.1f units per meter and k = %.1f the game's "
                  "velocity is still sqrt(s) v", s, factors[b]);
        Near(what, controller_math::Length(plan.game),
             5.0 * sqrt(static_cast<double>(s)), 1e-3);
        // Fly it with the game's own step, `rate` times a substep.
        float p[3] = {0.0f, static_cast<float>(h * s), 0.0f};
        float gv[3] = {plan.game[0], plan.game[1], plan.game[2]};
        float remainder = 0.0f;
        double land_time = -1.0, land_x = 0.0, worst = 0.0;
        int steps = 0;
        for (int sub = 0; sub < 4000 && land_time < 0.0; ++sub) {
          const int n = MiniSteps(plan.rate, &remainder);
          for (int j = 0; j < n; ++j) {
            GameFreeStep(p, gv);
            ++steps;
            const double time = steps * static_cast<double>(kSubstep) /
                                plan.rate;
            // The arc of the plan: s v / k under s g / k^2.
            float want_p[3], want_v[3];
            const float p0[3] = {0.0f, static_cast<float>(h * s), 0.0f};
            RealArc(p0, plan.flight, plan.gravity, time, want_p, want_v);
            const double off = sqrt(
                (p[0] - want_p[0]) * (p[0] - want_p[0]) +
                (p[1] - want_p[1]) * (p[1] - want_p[1])) / s;
            if (off > worst) worst = off;
            if (p[1] <= 0.0f) {
              land_time = time;
              land_x = p[0] / s;
              break;
            }
          }
        }
        sprintf_s(what, "at %.1f units per meter and k = %.1f the object "
                  "lands where the real throw lands (m)", s, factors[b]);
        Near(what, land_x, real_range, 0.05);
        sprintf_s(what, "at %.1f units per meter and k = %.1f the flight "
                  "takes k times the real time (s)", s, factors[b]);
        Near(what, land_time, real_time * plan.slow, 0.012 * plan.slow);
        sprintf_s(what, "at %.1f units per meter and k = %.1f the game's "
                  "steps stay on the arc of the plan (m)", s, factors[b]);
        Near(what, worst, 0.0, 0.03);
      }
    }
    float rate = 0.0f;
    Near("k below 1 is cut to 1", SlowFactor(0.5f, 10.0f, &rate), 1.0, 1e-6);
    Near("and k = 1 runs sqrt(s) steps", rate, 10.0, 1e-5);
    Near("k above sqrt(s) is cut to sqrt(s)", SlowFactor(50.0f, 10.0f, &rate),
         10.0, 1e-6);
    Near("and it runs one step", rate, 1.0, 1e-6);
  }

  // THE STEPS OF A SUBSTEP.
  {
    const float scales[4] = {7.5f, 67.0f, 100.0f, 4000.0f};
    for (int k = 0; k < 4; ++k) {
      const float scale = sqrtf(scales[k]);
      float remainder = 0.0f;
      int total = 0, least = 1000, most = 0;
      for (int sub = 0; sub < 200; ++sub) {
        const int n = MiniSteps(scale, &remainder);
        total += n;
        if (n < least) least = n;
        if (n > most) most = n;
      }
      char what[160];
      sprintf_s(what, "at %.1f units per meter 200 substeps run 200 sqrt(s) "
                "steps", scales[k]);
      Near(what, total, 200.0 * scale, 1.0);
      sprintf_s(what, "at %.1f units per meter each substep runs sqrt(s) "
                "steps, to 1", scales[k]);
      Check(least >= static_cast<int>(scale) && most <= static_cast<int>(
            scale) + 1, what, most, scale);
    }
    float remainder = 0.3f;
    Check(MiniSteps(1.0f, &remainder) == 1 && remainder == 0.0f,
          "a scale of 1 runs the game's one step", 1, 1);
  }

  // THE REAL ARC and the game's steps agree in world units.
  {
    const float p0[3] = {100.0f, 50.0f, 200.0f};
    const float v0[3] = {300.0f, 200.0f, -100.0f};
    float p[3], v[3];
    RealArc(p0, v0, 67.0f, 0.5, p, v);
    Near("the real arc: x after 0.5 s", p[0], 100.0 + 150.0, 1e-3);
    Near("the real arc: y after 0.5 s", p[1],
         50.0 + 100.0 - 0.5 * 67.0 * 9.81 * 0.25, 1e-2);
    Near("the real arc: the speed down after 0.5 s", v[1],
         200.0 - 67.0 * 9.81 * 0.5, 1e-2);
  }

  // THE FLIGHT THAT THE MOD DRAWS.
  {
    FlightSample f[3] = {};
    for (int k = 0; k < 3; ++k) {
      f[k].time = 0.2 + 0.005 * k;
      f[k].matrix[0] = static_cast<float>(k);  // the axes of each sample
      f[k].matrix[9] = 10.0f * k;
      f[k].matrix[10] = 50.0f - k;
      f[k].velocity[0] = 2000.0f;
      f[k].velocity[1] = -200.0f;
    }
    float m[12];
    double late = 0.0;
    Check(!FlightAt(f, 3, 0.19, 100.0f, m, &late),
          "before the first sample there is no flight to draw", 0, 0);
    Check(FlightAt(f, 3, 0.2025, 100.0f, m, &late),
          "between two samples there is a flight to draw", 1, 1);
    Near("between two samples the position is on the line, x", m[9], 5.0,
         1e-3);
    Near("between two samples the position is on the line, y", m[10], 49.5,
         1e-3);
    Near("it is not late", late, 0.0, 1e-12);
    FlightAt(f, 3, 0.2040, 100.0f, m, &late);
    Near("the axes are those of the nearer sample", m[0], 1.0, 1e-6);
    FlightAt(f, 3, 0.2200, 100.0f, m, &late);
    Near("10 ms after the newest sample a free flight goes on, x", m[9],
         20.0 + 2000.0 * 0.010, 1e-3);
    Near("and it falls with the gravity of the real flight, y", m[10],
         48.0 - 200.0 * 0.010 - 0.5 * 100.0 * 9.81 * 0.010 * 0.010, 1e-3);
    Near("the clock is 10 ms after the newest sample", late, 0.010, 1e-9);
    FlightAt(f, 3, 0.5, 100.0f, m, &late);
    Near("a late turn: the flight stops 50 ms after the newest sample",
         m[9], 20.0 + 2000.0 * kFlightAhead, 1e-2);
    f[2].contact = true;
    FlightAt(f, 3, 0.2200, 100.0f, m, &late);
    Near("an object in contact keeps its height", m[10], 48.0, 1e-6);
    Near("and goes on across the land", m[9], 20.0 + 2000.0 * 0.010, 1e-3);
  }

  // THE TIME AFTER THE LANDING.
  {
    Near("before the object touches, the rate is the rate of the flight",
         LandRate(5.0f, 1.0f, 0.0, kLandEase), 5.0, 1e-6);
    Near("after one time constant 37 percent of the change is left",
         LandRate(5.0f, 1.0f, kLandEase, kLandEase), 1.0 + 4.0 * exp(-1.0),
         1e-5);
    Near("after 10 time constants the rate is the rate of the land",
         LandRate(5.0f, 1.0f, 10.0 * kLandEase, kLandEase), 1.0, 1e-3);
    Near("a rate of the land above the rate of the flight changes nothing",
         LandRate(2.0f, 3.0f, 1.0, kLandEase), 2.0, 1e-6);
    Near("with no time constant the rate is the rate of the land at once",
         LandRate(5.0f, 1.0f, 0.0, 0.0), 1.0, 1e-6);
    // No step in the speed of the picture: one substep of 5 ms changes the
    // rate by less than 5 percent of the full change, and the rate only
    // falls.
    double worst = 0.0;
    bool falls = true;
    for (int k = 0; k < 200; ++k) {
      const double a = LandRate(5.0f, 1.0f, 0.005 * k, kLandEase);
      const double b = LandRate(5.0f, 1.0f, 0.005 * (k + 1), kLandEase);
      if (b > a + 1e-6 || b < 1.0 - 1e-6) falls = false;
      if ((a - b) / 4.0 > worst) worst = (a - b) / 4.0;
    }
    Check(worst < 0.05, "one substep changes the rate by less than 5 percent "
          "of the full change", worst, 0.05);
    Check(falls, "the rate only falls, and not below the rate of the land",
          falls, 1);
  }

  // THE LOW BOUNCE.
  {
    Near("a bounce that keeps half of its speed keeps a quarter of the force",
         ReboundKeep(0.5f), 0.25, 1e-6);
    Near("a share below 0 keeps nothing", ReboundKeep(-1.0f), 0.0, 1e-6);
    Near("a share above 1 keeps all", ReboundKeep(2.0f), 1.0, 1e-6);
    const float up[3] = {0.0f, 100.0f, 0.0f};
    const float in[3] = {3.0f, -20.0f, 0.0f};
    const float out[3] = {3.0f, 5.0f, 0.0f};
    const float slow[3] = {0.0f, 0.25f, 0.0f};
    const float none[3] = {0.0f, 0.0f, 0.0f};
    Near("the force against the motion stays whole",
         ReboundFactor(up, in, 0.25f, kReboundSoft), 1.0, 1e-6);
    Near("the force along the motion is cut to the part that stays",
         ReboundFactor(up, out, 0.25f, kReboundSoft), 0.25, 1e-6);
    Near("at half of the soft speed the cut is half",
         ReboundFactor(up, slow, 0.25f, kReboundSoft), 0.625, 1e-6);
    Near("no force gives no cut", ReboundFactor(none, out, 0.25f,
                                                kReboundSoft), 1.0, 1e-6);
    Near("a part of 1 gives no cut", ReboundFactor(up, out, 1.0f,
                                                   kReboundSoft), 1.0, 1e-6);
    // A ball on the contact of the game, at a landing speed of
    // 26.5 units a second down.
    double game_up = 0.0, low_up = 0.0, depth = 0.0, speed = 0.0;
    double game_depth = 0.0, game_speed = 0.0;
    BallOnLand(26.5, 1.0f, &game_up, &game_depth, &game_speed);
    BallOnLand(26.5, ReboundKeep(0.5f), &low_up, &depth, &speed);
    printf("  (a ball that lands at 26.5: the game gives %.2f up, the low "
           "bounce %.2f, and it lies %.3f deep, the game's %.3f)\n", game_up,
           low_up, depth, game_depth);
    Check(game_up > 1.0, "the contact of the game lets the ball bounce",
          game_up, 1.0);
    Check(low_up < 0.7 * game_up, "the low bounce takes 30 percent or more "
          "of the speed of the bounce", low_up, 0.7 * game_up);
    Check(speed < 0.05, "with the low bounce the ball comes to rest", speed,
          0.05);
    Check(depth < game_depth + 0.02, "and it does not lie deeper in the land "
          "than with the game's bounce", depth, game_depth + 0.02);
  }

  // THE SETTLE OF A TREE. The turn factor of one step after the first contact.
  {
    const float game = powf(0.728f, kSubstep);
    const float land = powf(kLandTurnKeep, kSubstep);
    const float settle = powf(kSettleTurnKeep, kSubstep);
    Near("with no brake and no settle the game's factor stays",
         TurnKeepAfterLanding(game, false, false, true, 0.0f), game, 1e-7);
    Near("the brake of the land gives the land factor",
         TurnKeepAfterLanding(game, true, false, true, 100.0f), land, 1e-7);
    Near("a tree that touches the land and is slow gets the settle factor",
         TurnKeepAfterLanding(game, true, true, true, kSettleSpeed), settle,
         1e-7);
    Near("a tree that is fast keeps the land factor",
         TurnKeepAfterLanding(game, true, true, true, kSettleSpeed + 1.0f),
         land, 1e-7);
    Near("a tree in the air keeps the land factor",
         TurnKeepAfterLanding(game, true, true, false, 0.0f), land, 1e-7);
    Near("the settle works with no brake too",
         TurnKeepAfterLanding(game, false, true, true, 1.0f), settle, 1e-7);
    Near("a factor that is already lower stays",
         TurnKeepAfterLanding(settle * 0.5f, true, true, true, 0.0f),
         settle * 0.5, 1e-9);
    // The turn of a tree with 30 times the cap of 3 pi radians a second
    // comes below 0.5 radians a second (the game's rest test, T / 2) in less
    // than 1 s with the settle. The game's factor needs 10 s or more.
    const double start = 30.0 * 3.14159265 * 3.0;
    double turn = start;
    int steps = 0;
    while (turn > 0.5 && steps < 100000) {
      turn *= settle;
      ++steps;
    }
    Check(steps * kSubstep < 1.0, "the settle brings a large turn to rest in "
          "less than 1 s", steps * kSubstep, 1.0);
    turn = start;
    steps = 0;
    while (turn > 0.5 && steps < 100000) {
      turn *= game;
      ++steps;
    }
    Check(steps * kSubstep > 10.0, "and the game's factor needs more than 10 s",
          steps * kSubstep, 10.0);
  }

  // THE BRAKE OF THE LAND. A block of the mass 2000 on the land, with the
  // game's gravity.
  {
    const float mass = 2000.0f, friction = 0.8f;
    const float flat[3] = {0.0f, mass * kGravity, 0.0f};
    const float fast[3] = {10.0f, 0.0f, 0.0f};
    const float slow[3] = {0.01f, 0.0f, 0.0f};
    const float none[3] = {0.0f, 0.0f, 0.0f};
    float out[3];
    LandBrake(flat, fast, mass, friction, out);
    Near("the brake is the friction times the force of the contact, against "
         "the velocity", out[0], -0.8 * 2000.0 * 9.81, 0.5);
    Near("and it has no part along the force of the contact", out[1], 0.0,
         1e-6);
    LandBrake(flat, slow, mass, friction, out);
    Near("a slow object gets the force that stops it in this step", out[0],
         -2000.0 * 0.01 / 0.005, 0.01);
    LandBrake(none, fast, mass, friction, out);
    Near("no contact gives no brake", out[0], 0.0, 1e-6);
    LandBrake(flat, fast, mass, 0.0f, out);
    Near("no friction gives no brake", out[0], 0.0, 1e-6);
    // The block slides at 15 units a second on flat land: it stops after
    // 15 / (0.8 g) = 1.91 s, and it does not go back.
    double v = 15.0, least = 15.0;
    int steps = 0;
    for (int k = 0; k < 1000; ++k) {
      const float v3[3] = {static_cast<float>(v), 0.0f, 0.0f};
      LandBrake(flat, v3, mass, friction, out);
      v += out[0] / mass * kSubstep;
      if (v < least) least = v;
      if (v > 1e-4) ++steps;
    }
    Near("a slide at 15 units a second stops after 1.91 s", steps * kSubstep,
         15.0 / (0.8 * 9.81), 0.011);
    Check(least > -1e-4, "the brake does not push the block back", least, 0);
    // On a slope the weight pulls the block along the land. Below the
    // friction angle the block stays, and above it the block goes on.
    for (int degrees = 30; degrees <= 45; degrees += 15) {
      const double a = degrees * 3.14159265 / 180.0;
      const float push[3] = {0.0f, static_cast<float>(mass * 9.81 * cos(a)),
                             0.0f};
      v = 5.0;
      for (int k = 0; k < 800; ++k) {
        const float v3[3] = {static_cast<float>(v), 0.0f, 0.0f};
        LandBrake(push, v3, mass, friction, out);
        v += (9.81 * sin(a) + out[0] / mass) * kSubstep;
      }
      if (degrees == 30) {
        Check(v < 0.1, "on a slope of 30 degrees the block stays", v, 0.1);
      } else {
        Check(v > 5.0, "on a slope of 45 degrees the block goes on", v, 5.0);
      }
    }
  }

  // THE NUMBERS OF THE TIME AFTER THE FIRST CONTACT.
  {
    // Axes with three different scales, as the mesh of a rock has them.
    const float same[12] = {1.01f, 0, 0, 0, 0.98f, 0, 0, 0, 1, 5, 6, 7};
    const float quarter[12] = {0, 0, -2, 0, 2, 0, 2, 0, 0, 0, 0, 0};
    const float big[12] = {2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0};
    Near("the same axes have no turn between them", TurnBetween(same, same),
         0.0, 1e-6);
    Near("a quarter turn is pi / 2, at each scale of the mesh",
         TurnBetween(big, quarter), 1.5707963, 1e-5);
    const float small[12] = {cosf(0.002f), 0, -sinf(0.002f), 0, 1, 0,
                             sinf(0.002f), 0, cosf(0.002f), 0, 0, 0};
    Near("a turn of 0.002 radians reads as 0.002",
         TurnBetween(big, small), 0.002, 1e-6);
    // An object that rolls for 1 s, makes a hop of 0.2 s to 2 units over
    // its height, rolls for 1 s, loses the land for 20 ms, and then shakes
    // by 0.3 units 5 times a second for 2 s.
    Landing l = {};
    int n = 0;
    for (int k = 0; k <= 840; ++k) {
      const double t = 0.005 * k;
      float over = 1.0f;
      bool touch = true;
      if (t > 1.0 && t < 1.2) {
        const double u = (t - 1.0) / 0.2;
        over = 1.0f + static_cast<float>(8.0 * u * (1.0 - u));
        touch = false;
      } else if (t > 2.2 && t < 2.215) {
        touch = false;
      } else if (t > 2.2) {
        over = 1.0f + 0.15f * static_cast<float>(
                          sin(2.0 * 3.14159265 * 5.0 * (t - 2.2)));
      }
      const float center[3] = {static_cast<float>(10.0 * t), over, 0.0f};
      LandingAdd(&l, 3.0 + t, center, over, touch, 0.01f, 0.05f);
      ++n;
    }
    LandingEndHop(&l, l.last);
    Check(l.hops == 1, "one hop: 20 ms without the land is no hop", l.hops,
          1);
    Near("the hop is 2 units high", l.top_hop, 2.0, 0.05);
    Near("the hop lasts 0.2 s", l.long_hop, 0.2, 0.011);
    Near("the object moved for 4.2 s", l.last - l.first, 4.2, 1e-6);
    Near("and 42 units across the land", l.path, 42.0, 0.01);
    Check(l.bobs >= 21 && l.bobs <= 23, "the hop gives 2 changes of the "
          "vertical direction, and the shake 20", l.bobs, 22);
    Near("the turn is the sum of the turns", l.turned, 0.01 * (n - 1), 1e-3);
    Near("the top turn rate is 2 radians a second", l.top_turn, 2.0, 1e-2);
    // The picture: 10 frames forward, one frame back by 0.4, 10 forward.
    DrawTrack d = {};
    float p[3] = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < 21; ++k) {
      p[0] += k == 10 ? -0.4f : 1.0f;
      DrawTrackAdd(&d, p, k == 3 ? 0.004 : 0.0, 0.05f);
    }
    Check(d.frames == 21 && d.late == 1, "21 frames, one of them after the "
          "newest step", d.late, 1);
    Near("that frame was 4 ms late", d.top_late, 0.004, 1e-9);
    Check(d.back == 2, "the picture turned back and then forward again",
          d.back, 2);
    Near("the longest move against the move before is 1 unit", d.top_back,
         1.0, 1e-5);
    Check(d.back_late == 0, "no turn back came after a late frame",
          d.back_late, 0);
    Check(d.jumps == 0, "the picture did not stand and jump", d.jumps, 0);
    Near("the longest move of one frame is 1 unit", d.top_step, 1.0, 1e-5);
    // The picture moves, stands for 2 frames, and goes on with a move of 0.8
    // units. Then it goes back by 0.3 in the frame after a late frame.
    DrawTrack j = {};
    float q[3] = {0.0f, 0.0f, 0.0f};
    const float moves[8] = {0.5f, 0.4f, 0.0f, 0.0f, 0.8f, 0.3f, -0.3f, 0.3f};
    for (int k = 0; k < 8; ++k) {
      q[0] += moves[k];
      DrawTrackAdd(&j, q, k == 5 ? 0.020 : 0.0, 0.05f);
    }
    Check(j.jumps == 1, "one stand and jump", j.jumps, 1);
    Near("the jump is 0.8 units", j.top_jump, 0.8, 1e-5);
    Check(j.back == 2 && j.back_late == 1,
          "one of the two turns back came after a late frame", j.back_late, 1);
    // A picture that comes to rest stands, and it does not jump.
    DrawTrack r = {};
    float at[3] = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < 8; ++k) {
      at[0] += k < 4 ? 0.5f : 0.0f;
      DrawTrackAdd(&r, at, 0.0, 0.05f);
    }
    Check(r.jumps == 0, "a picture that comes to rest does not jump", r.jumps,
          0);
  }

  // THE PICTURE BETWEEN THE STEPS (throw_draw).
  {
    // A substep of the flight at a rate of 3.33: 3 steps, 4.5 ms.
    const double flight = 3 * 0.005 / 3.33;
    Near("a substep of the flight keeps its time in the picture",
         PictureSpan(flight, false, kDrawReal), flight, 1e-12);
    // A substep with a touch of another object: one step, 1.5 ms of the
    // flight.
    const double touch = 1 * 0.005 / 3.33;
    Near("rule 1: a substep with a touch is 5 ms of the picture",
         PictureSpan(touch, true, kDrawReal), 0.005, 1e-8);
    Near("rule 0: a substep with a touch has its time of the flight",
         PictureSpan(touch, true, kDrawFlight), touch, 1e-12);
    // A turn of 20 substeps with a touch is 100 ms of the picture with the
    // rule 1, and 30 ms with the rule 0.
    double with = 0.0, without = 0.0;
    for (int k = 0; k < kSubstepsInTurn; ++k) {
      with += PictureSpan(touch, true, kDrawReal);
      without += PictureSpan(touch, true, kDrawFlight);
    }
    Near("rule 1: a turn of substeps with a touch is 100 ms of the picture",
         with, 0.100, 1e-7);
    Near("rule 0: it is 30 ms of the picture", without, 0.030, 1e-4);
    Near("in the flight the picture has the rate of the flight",
         PictureRate(3, flight), 3.33, 1e-4);
    Near("in a touch the picture has the speed of the game's own time",
         PictureRate(1, PictureSpan(touch, true, kDrawReal)), 1.0, 1e-6);
    Near("no time gives the rate 1", PictureRate(1, 0.0), 1.0, 1e-9);

    // The speed of the picture between two samples and after the newest one.
    FlightSample f[2] = {};
    f[0].time = 1.0;
    f[1].time = 1.005;
    f[1].matrix[9] = 0.5f;
    f[1].velocity[0] = 80.0f;
    float m[12], v[3];
    double late = 0.0;
    FlightAt(f, 2, 1.002, 100.0f, m, &late, v);
    Near("between two samples the speed is their distance over their time",
         v[0], 100.0, 1e-2);
    FlightAt(f, 2, 1.010, 100.0f, m, &late, v);
    Near("after the newest sample the speed is that of the sample", v[0],
         80.0, 1e-4);

    // THE PICTURE DOES NOT GO BACK. The guess is at x 10 and moved along x.
    const float guess[3] = {10.0f, 5.0f, 0.0f};
    const float dir[3] = {2.0f, 0.0f, 0.0f};
    const float behind[3] = {9.5f, 5.2f, 0.0f};
    const float past[3] = {10.1f, 4.0f, 0.0f};
    const float comes[3] = {30.0f, -5.0f, 0.0f};
    const float turned[3] = {-30.0f, 5.0f, 0.0f};
    Check(GuessHolds(behind, comes, guess, dir, 0.0, kFlightAhead),
          "the object is behind the guess and comes to it: the picture stays",
          1, 1);
    Check(!GuessHolds(past, comes, guess, dir, 0.0, kFlightAhead),
          "the object is past the guess: the picture goes with it", 0, 0);
    Check(!GuessHolds(behind, turned, guess, dir, 0.0, kFlightAhead),
          "the object turned in a contact: the picture goes with it", 0, 0);
    Check(!GuessHolds(behind, comes, guess, dir, kFlightAhead, kFlightAhead),
          "the picture never stays longer than the limit", 0, 0);

    // THE CLOCK FALLS BACK: 50 ms at a fifth of each frame of 20 ms.
    double debt = kFlightAhead, paid = 0.0;
    int frames = 0;
    while (debt > 0.0 && frames < 100) {
      paid += ClockFallBack(&debt, 0.020);
      ++frames;
    }
    Near("the clock gives back all of the time", paid, kFlightAhead, 1e-12);
    Check(frames == 13, "in 13 frames of 20 ms", frames, 13);
    Near("with no debt the clock gives nothing back",
         ClockFallBack(&debt, 0.020), 0.0, 1e-12);
  }

  // A RELEASE IN THE GLIDE OF A FAR FETCH.
  {
    Near("at the release the picture is at the point of the glide",
         CatchUpLeft(0.0, kCatchUp), 1.0, 1e-6);
    Near("at the half of the time one eighth of the way is left",
         CatchUpLeft(0.5 * kCatchUp, kCatchUp), 0.125, 1e-6);
    Near("after the time the picture is on the flight",
         CatchUpLeft(kCatchUp, kCatchUp), 0.0, 1e-6);
  }
}

}  // namespace throw_checks
