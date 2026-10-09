// THE GRAB FROM A DISTANCE (docs/grab.md, "The grab from a distance").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

namespace far_checks {

// The range of a throw from a height, with gravity g, from the speed and the
// angle. The same formula in the room (meters) and in the game (units).
inline double Range(double vx, double vy, double g, double h) {
  const double t = (vy + sqrt(vy * vy + 2.0 * g * h)) / g;
  return vx * t;
}

inline void FarChecks() {
  using namespace controller_math;
  printf("\nThe grab from a distance\n");

  // THE CONE. The ray goes from the origin along +z.
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float ray[3] = {0.0f, 0.0f, 1.0f};
  {
    const float on_ray[3] = {0.0f, 0.0f, 300.0f};
    FarScore s = FarConeScore(origin, ray, on_ray, 1.0f, 3.0f);
    Check(s.valid, "an object on the ray is valid", s.valid, 1);
    Near("an object on the ray scores te 0", s.te, 0.0, 1e-4);
    Near("its score is the distance term, 1 degree x log2(1 + 3)", s.score,
         2.0, 1e-4);
    const float aside[3] = {10.0f, 0.0f, 300.0f};
    s = FarConeScore(origin, ray, aside, 1.0f, 3.0f);
    const double t = atan(10.0 / 300.0) * 57.29578;
    const double covered = atan(1.0 / sqrt(10.0 * 10.0 + 300.0 * 300.0)) *
                           57.29578;
    Near("an object off the ray: te is the angle less the radius's angle",
         s.te, t - covered, 1e-3);
    // At the angular radius: the center is exactly r/d off the ray.
    const double d = 200.0, r = 6.0;
    const float edge[3] = {static_cast<float>(r), 0.0f,
                           static_cast<float>(sqrt(d * d - r * r))};
    s = FarConeScore(origin, ray, edge, static_cast<float>(r), 3.0f);
    Near("an object whose radius reaches the ray scores te 0", s.te, 0.0,
         1e-3);
    const float behind[3] = {0.0f, 0.0f, -100.0f};
    s = FarConeScore(origin, ray, behind, 1.0f, 3.0f);
    Check(!s.valid, "an object behind the ray is out", s.valid, 0);
    const float side[3] = {100.0f, 0.0f, 0.0f};
    s = FarConeScore(origin, ray, side, 0.0f, 3.0f);
    Check(!s.valid, "an object square to the ray is out", s.valid, 0);
    s = FarConeScore(origin, ray, on_ray, 1.0f, 0.20f);
    Check(!s.valid, "an object 20 cm from the Hand is out", s.valid, 0);
    s = FarConeScore(origin, ray, on_ray, 1.0f, 0.35f);
    Check(s.valid, "an object 35 cm from the Hand is in", s.valid, 1);
    const FarScore near_one = FarConeScore(origin, ray, on_ray, 1.0f, 1.0f);
    const FarScore far_one = FarConeScore(origin, ray, on_ray, 1.0f, 6.0f);
    Check(near_one.score < far_one.score,
          "of two equal targets the nearer one scores lower", near_one.score,
          far_one.score);
  }

  // THE HYSTERESIS.
  {
    FarScore s[2];
    s[0] = {true, 5.0f, 6.0f};   // the target now
    s[1] = {true, 3.0f, 4.5f};   // 1.5 degrees better
    int pick = FarPickTarget(s, 2, 0, kFarSelectDeg, kFarKeepDeg,
                             kFarSwitchDeg);
    Check(pick == 0, "a target 1.5 degrees better does not take over", pick, 0);
    s[1].score = 3.5f;  // 2.5 degrees better
    pick = FarPickTarget(s, 2, 0, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == 1, "a target 2.5 degrees better takes over", pick, 1);
    s[1].score = 4.0f;  // exactly 2 degrees better
    pick = FarPickTarget(s, 2, 0, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == 1, "a target 2 degrees better takes over", pick, 1);
    // A target stays up to te 14 and is lost past it.
    s[0] = {true, 13.0f, 14.0f};
    s[1] = {false, 0.0f, 0.0f};
    pick = FarPickTarget(s, 2, 0, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == 0, "the target stays at te 13 degrees", pick, 0);
    s[0] = {true, 14.5f, 15.0f};
    pick = FarPickTarget(s, 2, 0, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == -1, "the target is lost at te 14.5 degrees", pick, -1);
    // A new target needs te 8 or less.
    pick = FarPickTarget(s, 2, -1, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == -1, "no target at te 14.5 when there is none now", pick, -1);
    s[0] = {true, 9.0f, 9.5f};
    pick = FarPickTarget(s, 2, -1, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == -1, "a new target at te 9 degrees is not picked", pick, -1);
    s[0] = {true, 7.9f, 8.5f};
    pick = FarPickTarget(s, 2, -1, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == 0, "a new target at te 7.9 degrees is picked", pick, 0);
    // A target of te 10 is kept, but a new one of te 10 is not picked.
    s[0] = {true, 10.0f, 11.0f};
    pick = FarPickTarget(s, 2, 0, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == 0, "a target of te 10 degrees is kept", pick, 0);
    // A target that is not valid (nearer than the minimum) is lost.
    s[0] = {false, 0.0f, 0.0f};
    pick = FarPickTarget(s, 2, 0, kFarSelectDeg, kFarKeepDeg, kFarSwitchDeg);
    Check(pick == -1, "a target that turned invalid is lost", pick, -1);
  }

  // THE PULL. p is the unit direction to the head: back and up.
  {
    const float p[3] = {0.0f, 0.0f, -1.0f};
    float along = 0.0f;
    const float toward[3] = {0.0f, 0.0f, -0.9f};
    Check(PullDetect(toward, p, 0.0f, &along),
          "0.9 m/s toward the head fires", along, 0.9);
    const float across[3] = {0.9f, 0.0f, 0.0f};
    Check(!PullDetect(across, p, 0.0f, &along),
          "0.9 m/s across does not fire", along, 0.0);
    const float still[3] = {0.0f, 0.0f, 0.0f};
    Check(!PullDetect(still, p, 0.0f, &along),
          "a head step with a still hand does not fire", along, 0.0);
    const float slow[3] = {0.0f, 0.0f, -0.7f};
    Check(!PullDetect(slow, p, 0.05f, &along),
          "0.7 m/s and 5 cm does not fire", along, 0.7);
    Check(PullDetect(slow, p, 0.12f, &along),
          "a move of 12 cm along p fires at any speed", along, 0.7);
    Check(!PullDetect(still, p, 0.11f, &along),
          "a move of 11 cm does not fire", 0, 0);
    // The angle: 1.0 m/s at 36.9 degrees fires, 1.5 m/s with the same 0.8
    // along p (57.7 degrees) does not.
    const float inside[3] = {0.6f, 0.0f, -0.8f};
    Check(PullDetect(inside, p, 0.0f, &along),
          "0.8 m/s along p at 37 degrees fires", along, 0.8);
    const float wide[3] = {1.269f, 0.0f, -0.8f};
    Check(!PullDetect(wide, p, 0.0f, &along),
          "0.8 m/s along p at 57.8 degrees does not fire", along, 0.8);
    // The flick: up at 1.1 m/s with 0.4 m/s along a p that points up too.
    const float up_p[3] = {0.0f, 0.5f, -0.866025f};
    const float flick[3] = {0.0f, 1.1f, -0.1f};
    Check(PullDetect(flick, up_p, 0.0f, &along),
          "a flick up with v.y 1.1 and 0.64 m/s along p fires", along, 0.64);
    const float weak[3] = {0.0f, 0.9f, 0.0f};
    Check(!PullDetect(weak, up_p, 0.0f, &along),
          "a flick up of 0.9 m/s does not fire", along, 0.45);
    const float away[3] = {0.0f, 1.2f, 0.9f};
    Check(!PullDetect(away, up_p, 0.0f, &along),
          "an upward flick away from the head does not fire", along, 0);
  }

  // THE GLIDE.
  {
    const float p0[3] = {10.0f, 5.0f, 300.0f};
    const float p1[3] = {1.0f, 5.0f, 20.0f};
    float at[3];
    GlideArc(p0, p1, 0.0f, at);
    Near("the glide starts at p0", Distance(at, p0), 0.0, 1e-4);
    GlideArc(p0, p1, 1.0f, at);
    Near("the glide ends at p1", Distance(at, p1), 0.0, 1e-4);
    GlideArc(p0, p1, 0.5f, at);
    const double d = Distance(p0, p1);
    const double e = 1.0 - 0.125;
    Near("at the middle the object is 15 percent of the distance up",
         at[1] - (5.0 + (5.0 - 5.0) * e), 0.15 * d, 1e-3);
    Near("and e(0.5) of the way along", at[2], p0[2] + (p1[2] - p0[2]) * e,
         1e-3);
    // The peak is at u = 0.5 for a flat glide.
    double best_u = 0.0, best_y = -1e9;
    for (int i = 0; i <= 100; ++i) {
      const float u = 0.01f * i;
      GlideArc(p0, p1, u, at);
      if (at[1] > best_y) {
        best_y = at[1];
        best_u = u;
      }
    }
    Near("the peak of a flat glide is at u = 0.5", best_u, 0.5, 0.011);
    float last = -1.0f;
    bool grows = true;
    for (int i = 0; i <= 20; ++i) {
      const float v = GlideEase(0.05f * i);
      if (v < last) grows = false;
      last = v;
    }
    Check(grows, "the ease never goes back", grows, 1);
    Near("the glide takes 0.30 s at 0 m", GlideSeconds(0.0f), 0.30, 1e-6);
    Near("0.40 s at 2 m", GlideSeconds(2.0f), 0.40, 1e-6);
    Near("0.50 s at 4 m", GlideSeconds(4.0f), 0.50, 1e-6);
    Near("and never more than 0.50 s", GlideSeconds(40.0f), 0.50, 1e-6);
    // A moving Hand: the end follows p1.
    const float p1b[3] = {50.0f, 9.0f, 25.0f};
    GlideArc(p0, p1b, 1.0f, at);
    Near("with p1 moved the glide still ends at p1", Distance(at, p1b), 0.0,
         1e-4);
  }

  // THE SQUARE ROOT LAW. A throw at 40 degrees up, 3 m/s, from 1.2 m, at
  // three scales. The room has gravity 9.81 m/s^2 and the game 9.81 u/s^2.
  {
    const float scales[3] = {7.5f, 100.0f, 1000.0f};
    const float life = 7.5f;
    const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const double speed = 3.0;
    const double angle = 40.0 * 3.14159265358979 / 180.0;
    for (int k = 0; k < 3; ++k) {
      const double s = scales[k];
      // A hand velocity in the room in units at the life size scale.
      const float v_room[3] = {static_cast<float>(speed * cos(angle) * life),
                               static_cast<float>(speed * sin(angle) * life),
                               0.0f};
      float out[3], before = 0.0f;
      bool drop = true;
      SqrtThrowVelocity(v_room, v_room, ident, 1.5f, life,
                        static_cast<float>(s), 124.0f, 2.0f, out, &before,
                        &drop);
      Check(!drop, "a throw of 3 m/s is not a drop", drop, 0);
      Near("the speed sent is sqrt(s) v", Length(out), sqrt(s) * speed, 2e-3);
      const double r_room = Range(speed * cos(angle), speed * sin(angle),
                                  9.81, 1.2);
      const double r_game = Range(out[0], out[1], 9.81, 1.2 * s);
      Near("the range is s times the real range (relative)",
           r_game / (s * r_room), 1.0, 1e-4);
      Near("the direction is the hand's (elevation)",
           atan2(out[1], out[0]) * 57.29578, 40.0, 1e-3);
    }
    // The cap at 124 / sqrt(s) m/s.
    {
      const double s = 1000.0;
      const float v_room[3] = {static_cast<float>(5.0 * life), 0.0f, 0.0f};
      float out[3], before = 0.0f;
      bool drop = true;
      SqrtThrowVelocity(v_room, v_room, ident, 1.5f, life,
                        static_cast<float>(s), 124.0f, 2.0f, out, &before,
                        &drop);
      Near("5 m/s at s = 1000 is cut to the cap of 124", Length(out), 124.0,
           1e-3);
      Near("the speed before the cut is sqrt(s) v", before, sqrt(s) * 5.0,
           1e-2);
      const float v_ok[3] = {static_cast<float>(3.9 * life), 0.0f, 0.0f};
      SqrtThrowVelocity(v_ok, v_ok, ident, 1.5f, life, static_cast<float>(s),
                        124.0f, 2.0f, out, &before, &drop);
      Near("3.9 m/s at s = 1000 is under the cap (124 / sqrt(s) = 3.92)",
           Length(out), sqrt(s) * 3.9, 1e-2);
    }
    // The gentle drop rule is the one of TableThrowVelocity.
    {
      int same = 0, total = 0;
      for (int k = 0; k < 3; ++k) {
        for (double v = 0.0; v <= 1.0; v += 0.05) {
          const float v_room[3] = {static_cast<float>(v * life), 0.0f, 0.0f};
          float a[3], b[3], before_a = 0.0f, before_b = 0.0f;
          bool drop_a = false, drop_b = false;
          SqrtThrowVelocity(v_room, v_room, ident, 1.5f, life, scales[k],
                            124.0f, 2.0f, a, &before_a, &drop_a);
          TableThrowVelocity(v_room, v_room, ident, 1.5f, scales[k] / life,
                             124.0f, 2.0f, b, &before_b, &drop_b);
          ++total;
          if (drop_a == drop_b) ++same;
        }
      }
      Check(same == total, "the drop rule does not change", same, total);
    }
    // The old law, for throw_law=0, is TableThrowVelocity: 1.5 s v_room.
    {
      const float v_room[3] = {static_cast<float>(3.0 * life), 0.0f, 0.0f};
      float out[3], before = 0.0f;
      bool drop = false;
      TableThrowVelocity(v_room, v_room, ident, 1.5f, 100.0f / life, 124.0f,
                         2.0f, out, &before, &drop);
      Near("the old throw is cut to the cap at s = 100 and 3 m/s",
           Length(out), 124.0, 1e-3);
      Near("and it would send 1.5 s v before the cut", before, 1.5 * 100 * 3,
           1e-1);
    }
  }

  // THE AIM OF THE FAKE HAND: the yaw and the pitch of FakeAimAngles give an
  // aim ray along the wanted direction, through the quaternion of the fake
  // controller and the flip of z of head_pose.cpp.
  {
    const float wants[5][3] = {{0.0f, 0.0f, 1.0f},
                               {1.0f, 0.0f, 2.0f},
                               {-3.0f, 1.0f, 4.0f},
                               {0.5f, -1.0f, 3.0f},
                               {-2.0f, 0.2f, -1.0f}};
    double worst = 0.0;
    for (int k = 0; k < 5; ++k) {
      float yaw = 0.0f, pitch = 0.0f;
      FakeAimAngles(wants[k], &yaw, &pitch);
      float rotation[9], aim[3];
      exact_checks::GripRotation(yaw, pitch, 0.0f, rotation);
      AimDirection(rotation, aim);
      const float length = Length(wants[k]);
      for (int i = 0; i < 3; ++i) {
        const double e = fabs(aim[i] - wants[k][i] / length);
        if (e > worst) worst = e;
      }
    }
    Near("the angles of FakeAimAngles aim the fake hand along the direction",
         worst, 0.0, 1e-4);
    float yaw = 0.0f, pitch = 0.0f;
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    Check(!FakeAimAngles(zero, &yaw, &pitch), "a zero direction has no angles",
          0, 0);
  }

  // THE HALO. A negative pull puts the depth test point farther away.
  {
    const float near_plane = 3.5f;
    const float o[3] = {0.0f, 0.0f, 0.0f};
    const float x_axis[3] = {1.0f, 0.0f, 0.0f};
    const float y_axis[3] = {0.0f, 1.0f, 0.0f};
    const float z_axis[3] = {0.0f, 0.0f, 1.0f};
    float m[12];
    GameCamera(o, x_axis, y_axis, z_axis, m);
    const float ahead[3] = {0.0f, 0.0f, 50.0f};
    GlowDisc a = {}, b = {};
    GlowProject(m, kScreen.hx, kScreen.hy, near_plane, ahead, 0.5f, 0.0f, &a);
    GlowProject(m, kScreen.hx, kScreen.hy, near_plane, ahead, 0.5f, -5.0f, &b);
    Near("a push of 5 units puts the test point at depth 55 (rhw)", b.rhw,
         near_plane / 55.0, 1e-5);
    Check(b.rhw < a.rhw, "and it is farther than the point itself", b.rhw,
          a.rhw);
  }

  // A TARGET NEAR THE HAND. An object 20 cm from the Hand's point is no
  // target with its own distance, and it is one with the distance of the
  // score. A Creature command takes it, and the far grab has its own least
  // distance.
  {
    const float near_object[3] = {0.0f, 0.0f, 20.0f};
    FarScore s = FarConeScore(origin, ray, near_object, 1.0f, 0.20f);
    Check(!s.valid, "20 cm from the Hand with its own distance: no target",
          s.valid, 0);
    Near("the score takes 35 cm for an object 20 cm from the Hand",
         FarScoreMeters(0.20f), 0.35, 1e-6);
    Near("and the own distance of an object 1.2 m away", FarScoreMeters(1.2f),
         1.2, 1e-6);
    s = FarConeScore(origin, ray, near_object, 1.0f, FarScoreMeters(0.20f));
    Check(s.valid, "with the distance of the score it is a target", s.valid,
          1);
    Near("on the ray, so te is 0", s.te, 0.0, 1e-4);
  }

  // THE SEA DOES NOT HIDE.
  {
    Check(FarLandHides(30.0f, 20.0f, 2.0f),
          "a hill 10 units over the line hides the object", 1, 1);
    Check(!FarLandHides(21.0f, 20.0f, 2.0f),
          "a hill 1 unit over the line, less than the slack, does not", 0, 0);
    Check(!FarLandHides(0.0f, -5.0f, 2.0f),
          "the sea does not hide a line that goes 5 units under it", 0, 0);
    Check(!FarLandHides(0.0f, 20.0f, 2.0f),
          "and it does not hide a line over it", 0, 0);
    Check(FarLandHides(0.5f, -5.0f, 2.0f),
          "land just over the sea level hides a line under it", 1, 1);
  }
}

}  // namespace far_checks
