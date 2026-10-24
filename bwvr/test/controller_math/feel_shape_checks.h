// HANDS THAT REACT TO THE WORLD, the second part (docs/hand-model.md, "Hands
// that react to the world"): the outer joints of a finger, the turn of the
// thumb, the spread, and the sea. main.cpp includes this file after
// hand_model_checks.h and feel_checks.h.
//
// The hand is the made up hand of hand_model_checks.h, with the place of the
// mesh on the hand, so a finger has a knuckle and outer joints as the mesh of
// the game has them.
#pragma once

#include "../../src/mod/hand_feel_math.h"
#include "../../src/mod/hand_model_math.h"

namespace feel_shape_checks {

namespace hf = hand_feel;
namespace hm = hand_model;
namespace om = overlay_math;
namespace hx = hand_exact_math;

// The angle of a turn in the frame of a bone, in degrees: the turn of a pose
// against the rest pose (world times the inverse of the rest).
inline double TurnDeg(const float world[12], const float rest_inverse[12]) {
  float t[12];
  hx::MultiplyGame(world, rest_inverse, t);
  double c = (static_cast<double>(t[0]) + t[4] + t[8] - 1.0) * 0.5;
  if (c > 1.0) c = 1.0;
  if (c < -1.0) c = -1.0;
  return acos(c) * 180.0 / 3.14159265358979;
}

struct MeshHand {
  hm::Skeleton s;
  hm::Placement place;
};

inline void MakeMeshHand(MeshHand* out) {
  float mesh_length = 0.0f;
  hand_model_checks::MadeUpHand(&out->s, &mesh_length);
  hm::PrepareSkeleton(&out->s);
  float rest_turn[9];
  hx::RestTurn(0.0f, 0.0f, 0.0f, rest_turn);
  hm::MakePlacement(out->s, rest_turn, mesh_length, &out->place);
}

inline void FeelShapeChecks() {
  printf("\nHands that react to the world: the outer joints and the thumb\n");
  MeshHand mesh;
  MakeMeshHand(&mesh);
  const hm::Skeleton& s = mesh.s;
  hm::FingerPose open;
  hm::OpenPose(&open);
  {
    hm::FingerPose pose = open;
    pose.curl[1] = 0.2f;
    pose.outer[1] = 0.5f;
    float w[hm::kBones][12];
    hm::PoseBones(s, pose, w);
    Near("the knuckle of the index finger takes the curl: 0.2 of 73.8 "
         "degrees", hand_model_checks::BoneAngle(w[17], s.rest[17]), 14.76,
         0.02);
    Near("the next joint takes the curl and the outer bend: 0.7 of 66.6 "
         "degrees", hand_model_checks::BoneAngle(w[18], w[17]), 46.62, 0.02);
    Near("and so does the last joint", hand_model_checks::BoneAngle(w[19], w[18]),
         46.62, 0.02);
    hm::FingerPose much = open;
    much.curl[2] = 0.8f;
    much.outer[2] = 0.5f;
    hm::PoseBones(s, much, w);
    Near("an outer joint bends to its full bend at most",
         hand_model_checks::BoneAngle(w[9], w[8]), 83.5, 0.02);
    hm::FingerPose thumb = open;
    thumb.curl[0] = 0.4f;
    thumb.outer[0] = 0.3f;
    hm::PoseBones(s, thumb, w);
    Near("the thumb: its second bone takes the curl, 0.4 of 44.9 degrees",
         hand_model_checks::BoneAngle(w[20], s.rest[20]), 17.96, 0.02);
    Near("and its last bone the curl and the outer bend, 0.7 of 90.3 degrees",
         hand_model_checks::BoneAngle(w[21], w[20]), 63.21, 0.02);
  }
  {
    float m[12];
    hm::AxisTurn(hm::kThumbTurn[0].axis, 0.7f, m);
    const float* a = hm::kThumbTurn[0].axis;
    float moved[3];
    for (int k = 0; k < 3; ++k) {
      moved[k] = a[0] * m[0 + k] + a[1] * m[3 + k] + a[2] * m[6 + k];
    }
    Near("the turn of an axis leaves the axis where it is",
         hand_model_checks::Gap(moved, a), 0.0, 1e-5);
    Near("and it has no mirror", hx::Det(m), 1.0, 1e-5);
    hm::FingerPose turned = open;
    turned.thumb_turn = 1.0f;
    float w[hm::kBones][12];
    hm::PoseBones(s, turned, w);
    Near("a thumb turn of 1 turns the base of the thumb by the 42.04 degrees "
         "of the game's open hold", TurnDeg(w[3], s.rest_inverse[3]), 42.04,
         0.05);
    double worst = 0.0;
    for (int b = 0; b < hm::kBones; ++b) {
      if (b == 3 || b == 20 || b == 21) continue;
      for (int i = 0; i < 12; ++i) {
        worst = fmax(worst, fabs(w[b][i] - s.rest[b][i]));
      }
    }
    Near("and it moves no other finger", worst, 0.0, 1e-4);
    Near("the bones of the thumb keep their lengths",
         hand_model_checks::Gap(&w[21][9], &w[20][9]),
         hand_model_checks::Gap(&s.rest[21][9], &s.rest[20][9]), 1e-3);
    hm::FingerPose half = open;
    half.thumb_turn = 0.5f;
    hm::PoseBones(s, half, w);
    Near("half the turn is half the angle at the base",
         TurnDeg(w[3], s.rest_inverse[3]), 21.02, 0.05);
  }

  printf("\nHands that react to the world: the grid of the outer joints\n");
  hf::FingerGrid grid;
  hf::FillGrid([&](float knuckle, float outer, float out[hf::kPoints][3]) {
    hm::MeshFingerJointsPose(s, mesh.place, 2, knuckle, outer - knuckle, 0.0f,
                             0.0f, out);
  }, &grid);
  {
    double worst_node = 0.0, worst_mix = 0.0, worst_line = 0.0;
    for (int i = 0; i <= 32; ++i) {
      for (int j = 0; j <= 32; ++j) {
        const float k = static_cast<float>(i) / 32.0f;
        const float q = static_cast<float>(j) / 32.0f;
        float mix[hf::kPoints][3], exact[hf::kPoints][3];
        hf::GridAt(grid, k, q, mix);
        hm::MeshFingerJointsPose(s, mesh.place, 2, k, q - k, 0.0f, 0.0f,
                                 exact);
        for (int p = 0; p < hf::kPoints; ++p) {
          const double d = hand_model_checks::Gap(mix[p], exact[p]);
          if (i % 2 == 0 && j % 2 == 0) worst_node = fmax(worst_node, d);
          worst_mix = fmax(worst_mix, d);
        }
        if (i == j) {
          float line[hf::kPoints][3];
          hm::MeshFingerJoints(s, mesh.place, 2, k, line);
          for (int p = 0; p < hf::kPoints; ++p) {
            worst_line = fmax(worst_line, hand_model_checks::Gap(mix[p], line[p]));
          }
        }
      }
    }
    Near("a point on the grid gives the joints of the mesh", worst_node, 0.0,
         1e-4);
    Check(worst_mix < 0.003, "a point between the rows of the grid is within "
          "0.3 percent of the hand length of the true place", worst_mix,
          0.003);
    Check(worst_line < 0.003, "the same knuckle curl and outer curl is the "
          "finger of one curl", worst_line, 0.003);
  }
  {
    float k = 0.0f, q = 0.0f;
    hf::PathCurls(0.2f, 0.6f, hf::kPathEven, 1.0f, &k, &q);
    Check(k == 0.6f && q == 0.6f, "on the even path each joint takes the "
          "curl", k, 0.6);
    hf::PathCurls(0.2f, 0.2f, hf::kPathOuter, 1.0f, &k, &q);
    Check(k == 0.2f && q == 0.2f, "on the outer path the curl of the pose is "
          "the pose", k, 0.2);
    hf::PathCurls(0.2f, 0.3f, hf::kPathOuter, 1.0f, &k, &q);
    Near("on the outer path the knuckle takes the curl", k, 0.3, 1e-6);
    Near("and the outer joints take as much again past the pose", q, 0.4,
         1e-6);
    hf::PathCurls(0.2f, 0.7f, hf::kPathOuter, 1.0f, &k, &q);
    Near("but never more than kOuterExtra past the knuckle", q - k,
         hf::kOuterExtra, 1e-6);
    double worst_step = 0.0;
    float last_k = 0.2f, last_q = 0.2f;
    for (int i = 1; i <= 200; ++i) {
      const float curl = 0.2f + 0.8f * static_cast<float>(i) / 200.0f;
      hf::PathCurls(0.2f, curl, hf::kPathOuter, 0.6f, &k, &q);
      worst_step = fmax(worst_step, fmax(fabs(k - last_k), fabs(q - last_q)));
      last_k = k;
      last_q = q;
    }
    Check(worst_step <= 0.0081, "each share changes with no step as the curl "
          "grows: at most twice the step of the curl", worst_step, 0.008);
    hf::PathCurls(0.2f, 0.9f, hf::kPathOuter, 0.6f, &k, &q);
    Check(k == 0.9f && q == 0.6f, "the outer joints stop at the fold limit, "
          "and the knuckle takes the rest", q, 0.6);
    hf::PathCurls(0.2f, 0.9f, hf::kPathEven, 0.6f, &k, &q);
    Check(q == 0.6f, "the fold limit holds on the even path too", q, 0.6);
    hf::PathCurls(0.8f, 0.9f, hf::kPathOuter, 0.6f, &k, &q);
    Check(q == 0.8f, "a pose that bends the outer joints past the limit keeps "
          "its bend: the world adds none to them", q, 0.8);
    hf::PathCurls(0.2f, 0.05f, hf::kPathOuter, 0.6f, &k, &q);
    Check(k == 0.05f && q == 0.05f, "a curl below the pose opens each joint "
          "the same, so a finger on a wall stays straight", q, 0.05);
    Check(hf::PathFor(0) == hf::kPathEven && hf::PathFor(1) == hf::kPathOuter &&
              hf::PathFor(5) == hf::kPathOuter,
          "a contact at the first bone bends at the knuckle, and a contact "
          "past the first joint bends at the outer joints",
          hf::PathFor(5), hf::kPathOuter);
  }
  {
    // The fingers down over flat land. The tip of the middle finger at the
    // rest curl goes 0.4 units into the land.
    const float length = feel_checks::kLength;
    const float at[3] = {1000.0f, 40.0f, 1000.0f};
    float local[hf::kPoints][3], tip[3];
    hf::GridAt(grid, om::kRestCurl, om::kRestCurl, local);
    om::PlacePoint(at, feel_checks::kFingersDown, length, false,
                   local[hf::kPoints - 1], tip);
    const float land = tip[1] + 0.4f;
    hf::World world;
    world.land = true;
    world.bodies = 0;
    hf::FillPatch(at[0], at[2], 0.6f * length,
                  [&](float, float) { return land; }, &world.patch);
    float radius[hf::kSamples];
    hf::SampleRadii(om::kShape[2].radius * length, 0.5f, radius);
    const auto depth = [&](int path, float curl, int* deepest) {
      float k, q, pts[hf::kPoints][3], placed[hf::kPoints][3];
      float samples[hf::kSamples][3];
      hf::PathCurls(om::kRestCurl, curl, path, 1.0f, &k, &q);
      hf::GridAt(grid, k, q, pts);
      for (int p = 0; p < hf::kPoints; ++p) {
        om::PlacePoint(at, feel_checks::kFingersDown, length, false, pts[p],
                       placed[p]);
      }
      hf::Samples(placed, samples);
      return hf::Depth(world, samples, radius, nullptr, deepest);
    };
    int deepest = -1;
    Check(depth(hf::kPathEven, om::kRestCurl, &deepest) > 0.0f &&
              deepest == hf::kSamples - 1,
          "the fingers down: the tip of the middle finger is the deepest "
          "sample in the land", deepest, hf::kSamples - 1);
    const int path = hf::PathFor(deepest);
    const hf::Free free = hf::FreeCurl(om::kRestCurl, false, 0, [&](float c) {
      return depth(path, c, nullptr);
    });
    float k, q;
    hf::PathCurls(om::kRestCurl, free.curl, path, 1.0f, &k, &q);
    Check(free.blocked && free.free && path == hf::kPathOuter,
          "so the finger takes the outer path and finds a free curl",
          free.curl, om::kRestCurl);
    const hf::Free even = hf::FreeCurl(om::kRestCurl, false, 0, [&](float c) {
      return depth(hf::kPathEven, c, nullptr);
    });
    Check(k < even.curl && q > k + 0.02f,
          "the knuckle bends less than on the even path, and the outer joints "
          "bend more than the knuckle", q - k, 0.02);
    Check(fabsf(depth(path, free.curl, nullptr)) < 0.08f,
          "the tip rests on the land", depth(path, free.curl, nullptr), 0.0);
  }

  printf("\nHands that react to the world: the fold limit of the skin\n");
  {
    // A tube round the middle finger of the made up hand: a ring of 6
    // vertices near the end of bone 8 and one near the start of bone 9, and
    // the same for bones 9 and 10. The triangles between two rings have
    // their corners on two bones, as the skin of a joint has.
    const auto tube = [&](float near_joint, bool bridge) {
      const int rings = 4, sides = 6;
      static float rest[rings * sides * 3];
      static uint8_t bones[rings * sides * 4];
      static float weights[rings * sides * 4];
      static uint32_t index[rings * sides * 6];
      const int ring_bone[rings] = {8, 9, 9, 10};
      const float ring_at[rings] = {1.0f - near_joint, near_joint,
                                    1.0f - near_joint, near_joint};
      const float lengths[3] = {105.0f, 70.0f, 80.0f};  // bones 8, 9, 10
      for (int r = 0; r < rings; ++r) {
        const int b = ring_bone[r];
        const float* m = s.rest[b];
        const float along = ring_at[r] * lengths[b - 8];
        for (int i = 0; i < sides; ++i) {
          const float a = 6.2831853f * i / sides;
          const int v = r * sides + i;
          for (int k = 0; k < 3; ++k) {
            rest[v * 3 + k] = m[9 + k] + along * m[6 + k] +
                              25.0f * (cosf(a) * m[0 + k] + sinf(a) * m[3 + k]);
          }
          for (int k = 0; k < 4; ++k) {
            bones[v * 4 + k] = static_cast<uint8_t>(b);
            weights[v * 4 + k] = k == 0 ? 1.0f : 0.0f;
          }
        }
      }
      int n = 0;
      if (bridge) {
        for (int pair = 0; pair < 2; ++pair) {
          const int a0 = pair * 2 * sides, b0 = a0 + sides;
          for (int i = 0; i < sides; ++i) {
            const int j = (i + 1) % sides;
            const uint32_t t[6] = {static_cast<uint32_t>(a0 + i),
                                   static_cast<uint32_t>(a0 + j),
                                   static_cast<uint32_t>(b0 + i),
                                   static_cast<uint32_t>(a0 + j),
                                   static_cast<uint32_t>(b0 + j),
                                   static_cast<uint32_t>(b0 + i)};
            for (int k = 0; k < 6; ++k) index[n * 3 + k] = t[k];
            n += 2;
          }
        }
      } else {
        // One triangle in each ring: no corner on a second bone.
        for (int r = 0; r < rings; ++r) {
          const uint32_t t[3] = {static_cast<uint32_t>(r * sides),
                                 static_cast<uint32_t>(r * sides + 2),
                                 static_cast<uint32_t>(r * sides + 4)};
          for (int k = 0; k < 3; ++k) index[n * 3 + k] = t[k];
          ++n;
        }
      }
      return hm::OuterFoldLimit(s, 2, rings * sides, rest, bones, weights, n,
                                index);
    };
    const float close = tube(0.05f, true);
    const float apart = tube(0.30f, true);
    const float none = tube(0.05f, false);
    Check(close > 0.2f && close < 1.0f, "a skin with short triangles across "
          "the outer joints folds before the full bend", close, 0.5);
    Check(apart > close, "longer triangles across the joints fold later",
          apart, close);
    Near("a skin with no triangle across a joint has no fold limit", none, 1.0,
         1e-6);
    Check(hm::OuterFoldLimit(s, 2, 0, nullptr, nullptr, nullptr, 0,
                             nullptr) == 1.0f,
          "no mesh gives no limit", 1, 1);
  }

  printf("\nHands that react to the world: the spread and the turn of the "
         "thumb in the hold\n");
  {
    Near("a finger that the world opened by the rest curl spreads fully",
         hf::OpenShare(om::kRestCurl, 0.0f), 1.0, 1e-6);
    Near("half of it spreads half", hf::OpenShare(om::kRestCurl, 0.06f), 0.5,
         1e-5);
    Check(hf::OpenShare(0.3f, 0.3f) == 0.0f && hf::OpenShare(0.3f, 0.5f) == 0.0f,
          "a finger at its pose or bent more does not spread",
          hf::OpenShare(0.3f, 0.5f), 0.0);
    Check(hf::kFlatSpread[2] == 0.0f && hf::kFlatSpread[1] > 0.0f &&
              hf::kFlatSpread[3] < 0.0f &&
              hf::kFlatSpread[4] < hf::kFlatSpread[3],
          "the middle finger stays and the others fan out from it",
          hf::kFlatSpread[4], hf::kFlatSpread[3]);
    Check(fabsf(hf::kFlatSpread[4]) < 0.2f && fabsf(hf::kHoldSpread[4]) < 0.2f,
          "a spread is a little: less than 12 degrees", hf::kFlatSpread[4],
          -0.2);
    Check(hf::SizeShare(0.10f) == 0.0f && hf::SizeShare(0.30f) == 1.0f &&
              fabsf(hf::SizeShare(0.20f) - 0.5f) < 1e-5f,
          "a large object spreads the fingers, a small one does not",
          hf::SizeShare(0.20f), 0.5);
    Near("a thin object turns the thumb by the 10.8 degrees of hold_side "
         "frame 0", hf::HoldThumbTurn(hf::kHoldTree, 0.0f, 19.0f) * 42.04,
         10.8, 1e-3);
    Near("an object of two thirds of the hand turns it by the 42 degrees of "
         "frame 1", hf::HoldThumbTurn(hf::kHoldLiving, 12.7f, 19.0f), 1.0,
         1e-6);
    Check(hf::HoldThumbTurn(hf::kHoldSeed, 5.0f, 19.0f) == 0.0f,
          "a miracle seed keeps the hand of the mouse player", 0, 0);
    // The thumb that goes round: with its own table, the cap of hold_side
    // does not hold.
    hf::FingerTable tables[hf::kFingers];
    for (int f = 0; f < hf::kFingers; ++f) {
      hf::FillTable(f, [](int finger, float curl, float j[hf::kPoints][3]) {
        om::FingerJoints(finger, curl, j);
      }, &tables[f]);
    }
    float finger_radius[hf::kFingers];
    for (int f = 0; f < hf::kFingers; ++f) {
      finger_radius[f] = om::kShape[f].radius;
    }
    hf::HeldBody ball;
    float center[3];
    om::HoldCenter(0.10f, center);
    hf::MakeHeldBody(center, center, 0.10f, &ball);
    float game[hf::kFingers];
    hf::GameHoldCurls(hf::kHoldLiving, 0.6f, feel_checks::kLength, game);
    om::PoseRange capped, round;
    int touches[hf::kFingers];
    hf::HoldRange(tables, finger_radius, nullptr, ball, game, true, &capped,
                  touches);
    hf::HoldRange(tables, finger_radius, nullptr, ball, game, true, &round,
                  touches, &tables[0]);
    Check(capped.hi[0] <= game[0] + 1e-6f && round.hi[0] > capped.hi[0],
          "with the table of the turned thumb the thumb closes past the "
          "thumb of hold_side, round a villager", round.hi[0], capped.hi[0]);
    Check(round.hi[2] == capped.hi[2],
          "and the fingers close as before", round.hi[2], capped.hi[2]);
  }

  printf("\nHands that react to the world: the sea\n");
  {
    const float length = feel_checks::kLength;
    Near("with the sea as water, a point of the sea is deep under the plane",
         hf::SeaHeight(0.0f, true, length), -2.0 * length, 1e-5);
    Check(hf::SeaHeight(0.0f, false, length) == 0.0f &&
              hf::SeaHeight(3.0f, true, length) == 3.0f,
          "with the sea as land it is at 0, and the land keeps its height",
          hf::SeaHeight(0.0f, false, length), 0.0);
    // A finger 1 unit under the plane of the sea.
    const float under[3] = {500.0f, -1.0f, 500.0f};
    float samples[hf::kSamples][3];
    for (int i = 0; i < hf::kSamples; ++i) {
      for (int k = 0; k < 3; ++k) samples[i][k] = under[k];
      samples[i][0] += 0.3f * i;
    }
    float radius[hf::kSamples];
    hf::SampleRadii(om::kShape[2].radius * length, 0.5f, radius);
    hf::World water, rock;
    water.land = rock.land = true;
    water.bodies = rock.bodies = 0;
    hf::FillPatch(500.0f, 500.0f, 0.6f * length, [&](float, float) {
      return hf::SeaHeight(0.0f, true, length);
    }, &water.patch);
    hf::FillPatch(500.0f, 500.0f, 0.6f * length, [&](float, float) {
      return hf::SeaHeight(0.0f, false, length);
    }, &rock.patch);
    Check(!(hf::Depth(water, samples, radius, nullptr) > 0.0f),
          "a finger goes into the sea", hf::Depth(water, samples, radius,
                                                  nullptr), 0.0);
    Check(hf::Depth(rock, samples, radius, nullptr) > 0.0f,
          "with the sea as land it is in the land, and it bends out of it",
          hf::Depth(rock, samples, radius, nullptr), 1.0);
    Check(hf::InWater(water.patch, under), "a point under the plane of the "
          "sea is in the water", 1, 1);
    const float over[3] = {500.0f, 0.5f, 500.0f};
    Check(!hf::InWater(water.patch, over) && !hf::InWater(rock.patch, under),
          "a point over the plane is not, and with the sea as land nothing "
          "is in the water", 0, 0);
    // The beach: land of 1 unit on the side of less x, the sea past it.
    hf::World beach;
    beach.land = true;
    beach.bodies = 0;
    hf::FillPatch(500.0f, 500.0f, 0.6f * length, [&](float x, float) {
      return hf::SeaHeight(x < 500.0f ? 1.0f : 0.0f, true, length);
    }, &beach.patch);
    const float on_beach[3] = {492.0f, 1.2f, 500.0f};
    const float in_sea[3] = {508.0f, -1.0f, 500.0f};
    Check(hf::PatchHeight(beach.patch, on_beach[0], on_beach[2]) == 1.0f &&
              hf::PatchHeight(beach.patch, in_sea[0], in_sea[2]) < -10.0f,
          "a finger on the beach meets the beach, and past the water line it "
          "goes into the sea", hf::PatchHeight(beach.patch, in_sea[0],
                                               in_sea[2]), -10.0);
  }
}

}  // namespace feel_shape_checks
