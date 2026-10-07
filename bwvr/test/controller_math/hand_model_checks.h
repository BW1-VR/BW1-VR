// THE HAND MODEL (docs/hand.md, "The game's hand on both real hands"): the
// bones of a pose, the place of the mesh on the hand, the skin, the hold, the
// light and the look. main.cpp includes this file after Check and Near, just
// before main.
//
// The test has no game files. It makes a hand with the layout of the game's
// bones: 22 bones with the same parents, the length of a bone along its own
// z, the bend about its own x, a mirror in the wrist, and the tip of the
// index finger at the origin of the mesh. The lengths are round numbers.
#pragma once

#include "../../src/mod/hand_model_math.h"

namespace hand_model_checks {

namespace hm = hand_model;
namespace om = overlay_math;
namespace hx = hand_exact_math;

inline double Gap(const float a[3], const float b[3]) {
  double sum = 0.0;
  for (int i = 0; i < 3; ++i) {
    const double d = static_cast<double>(a[i]) - b[i];
    sum += d * d;
  }
  return sqrt(sum);
}

inline void SetBone(hm::Skeleton* s, int bone, int parent, float x, float y,
                    float z) {
  const float identity[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, x,    y,    z};
  s->parent[bone] = parent;
  for (int i = 0; i < 12; ++i) s->local[bone][i] = identity[i];
}

// The made up hand. In the frame of a bone, x goes to the thumb side, y out
// of the palm and z along the bone.
inline void MadeUpHand(hm::Skeleton* s, float* mesh_length) {
  // The wrist: its x is +x of the mesh, its y is +z (out of the palm) and its
  // z is +y (the fingers). The determinant is -1, as in the file.
  SetBone(s, 0, -1, -30.0f, -595.0f, 0.0f);
  const float wrist[9] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
  for (int i = 0; i < 9; ++i) s->local[0][i] = wrist[i];
  SetBone(s, 1, 0, 0.0f, 100.0f, 250.0f);  // the attach bone
  SetBone(s, 2, 0, 0.0f, 0.0f, 80.0f);     // the palm
  // The thumb: its base turns 45 degrees toward the thumb side.
  SetBone(s, 3, 2, 60.0f, 10.0f, 0.0f);
  const float c = 0.70710678f;
  const float turn[9] = {c, 0.0f, -c, 0.0f, 1.0f, 0.0f, c, 0.0f, c};
  for (int i = 0; i < 9; ++i) s->local[3][i] = turn[i];
  SetBone(s, 20, 3, 0.0f, 0.0f, 140.0f);
  SetBone(s, 21, 20, 0.0f, 0.0f, 90.0f);
  // The index finger. The chain ends at the origin of the mesh:
  // 80 + 40 + 250 + 100 + 60 + 65 = 595 along the fingers, and 30 across.
  SetBone(s, 4, 2, 30.0f, 0.0f, 40.0f);
  SetBone(s, 17, 4, 0.0f, 0.0f, 250.0f);
  SetBone(s, 18, 17, 0.0f, 0.0f, 100.0f);
  SetBone(s, 19, 18, 0.0f, 0.0f, 60.0f);
  // The middle finger.
  SetBone(s, 7, 2, -40.0f, 0.0f, 40.0f);
  SetBone(s, 8, 7, 0.0f, 0.0f, 260.0f);
  SetBone(s, 9, 8, 0.0f, 0.0f, 105.0f);
  SetBone(s, 10, 9, 0.0f, 0.0f, 70.0f);
  // The ring finger.
  SetBone(s, 5, 2, -100.0f, 0.0f, 40.0f);
  SetBone(s, 14, 5, 0.0f, 0.0f, 250.0f);
  SetBone(s, 15, 14, 0.0f, 0.0f, 95.0f);
  SetBone(s, 16, 15, 0.0f, 0.0f, 65.0f);
  // The little finger.
  SetBone(s, 6, 2, -160.0f, 0.0f, 30.0f);
  SetBone(s, 11, 6, 0.0f, 0.0f, 245.0f);
  SetBone(s, 12, 11, 0.0f, 0.0f, 70.0f);
  SetBone(s, 13, 12, 0.0f, 0.0f, 65.0f);
  const float tips[hm::kFingers] = {85.0f, 65.0f, 80.0f, 65.0f, 60.0f};
  for (int f = 0; f < hm::kFingers; ++f) s->tip[f] = tips[f];
  // The wrist joint to the end of the middle finger: 40 across and
  // 80 + 40 + 260 + 105 + 70 + 80 = 635 along.
  *mesh_length = sqrtf(40.0f * 40.0f + 635.0f * 635.0f);
}

// The angle between the z axes of a bone in two poses, in degrees.
inline double BoneAngle(const float a[12], const float b[12]) {
  return hx::AngleDeg(&a[6], &b[6]);
}

inline void HandModelChecks() {
  printf("\nThe hand model: the bones of a pose\n");
  hm::Skeleton s;
  float mesh_length = 0.0f;
  MadeUpHand(&s, &mesh_length);
  Check(hm::PrepareSkeleton(&s), "the made up hand is a tree of bones", 1, 1);
  Near("each bone in the frame of the mesh has a mirror in it (determinant)",
       hx::Det(s.rest[19]), -1.0, 1e-5);
  {
    const float end[3] = {0.0f, 0.0f, s.tip[1]};
    float tip[3];
    hx::ModelToWorld(s.rest[19], end, tip);
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    Near("the tip of the index finger is the origin of the mesh, units",
         Gap(tip, origin), 0.0, 1e-3);
  }

  hm::FingerPose open;
  hm::OpenPose(&open);
  float world[hm::kBones][12], skin[hm::kBones][12];
  hm::PoseBones(s, open, world);
  hm::SkinMatrices(s, world, skin);
  {
    double worst = 0.0, worst_skin = 0.0;
    const float identity[12] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    for (int b = 0; b < hm::kBones; ++b) {
      for (int i = 0; i < 12; ++i) {
        worst = fmax(worst, fabs(world[b][i] - s.rest[b][i]));
        worst_skin = fmax(worst_skin, fabs(skin[b][i] - identity[i]));
      }
    }
    Near("a curl of 0 is the rest pose of the file", worst, 0.0, 1e-4);
    Near("and its skin matrices change nothing", worst_skin, 0.0, 1e-3);
  }

  // A FIST of the middle finger: each bone keeps its length, bends by the
  // angle of the game's pose, and the tip goes to the palm side.
  hm::FingerPose fist = open;
  fist.curl[2] = 1.0f;
  float bent[hm::kBones][12];
  hm::PoseBones(s, fist, bent);
  {
    const int* chain = hm::kFingerBones[2];
    double worst_length = 0.0;
    for (int j = 1; j < hm::kChain; ++j) {
      const double rest = Gap(&s.rest[chain[j]][9], &s.rest[chain[j - 1]][9]);
      const double now = Gap(&bent[chain[j]][9], &bent[chain[j - 1]][9]);
      worst_length = fmax(worst_length, fabs(rest - now));
    }
    Near("a curl keeps the length of each bone, units", worst_length, 0.0,
         1e-2);
    Near("the first bone of the middle finger bends 89.4 degrees",
         BoneAngle(bent[chain[1]], s.rest[chain[1]]), 89.4, 0.05);
    Near("the second bone bends 83.5 degrees more",
         BoneAngle(bent[chain[2]], bent[chain[1]]), 83.5, 0.05);
    Near("the last bone bends 53.9 degrees more",
         BoneAngle(bent[chain[3]], bent[chain[2]]), 53.9, 0.05);
    Near("the bone in the palm does not move",
         BoneAngle(bent[chain[0]], s.rest[chain[0]]), 0.0, 1e-3);
    // +z of the mesh is out of the palm.
    Check(bent[chain[2]][11] > s.rest[chain[2]][11] + 50.0f,
          "the finger closes over the palm side (+z of the mesh)",
          bent[chain[2]][11], s.rest[chain[2]][11] + 50.0);
    double other = 0.0;
    for (int b = 0; b < hm::kBones; ++b) {
      bool in_finger = false;
      for (int j = 0; j < hm::kChain; ++j) in_finger |= chain[j] == b;
      if (in_finger) continue;
      for (int i = 0; i < 12; ++i) {
        other = fmax(other, fabs(bent[b][i] - s.rest[b][i]));
      }
    }
    Near("no bone of another finger moves", other, 0.0, 1e-4);
  }
  {
    hm::FingerPose half = open;
    half.curl[2] = 0.5f;
    float mid[hm::kBones][12];
    hm::PoseBones(s, half, mid);
    Near("half the curl is half the bend, degrees",
         BoneAngle(mid[8], s.rest[8]), 44.7, 0.05);
  }
  {
    hm::FingerPose thumb = open;
    thumb.curl[0] = 1.0f;
    float t[hm::kBones][12];
    hm::PoseBones(s, thumb, t);
    Near("the base of the thumb does not bend", BoneAngle(t[3], s.rest[3]),
         0.0, 1e-3);
    Near("the second bone of the thumb bends 44.9 degrees",
         BoneAngle(t[20], s.rest[20]), 44.9, 0.05);
    Near("the last bone of the thumb bends 90.3 degrees more",
         BoneAngle(t[21], t[20]), 90.3, 0.05);
  }

  // THE SPREAD turns the finger at its knuckle, toward the thumb side.
  {
    hm::FingerPose spread = open;
    spread.spread[1] = 0.2f;
    float w[hm::kBones][12];
    hm::PoseBones(s, spread, w);
    Near("a spread of 0.2 turns the index finger 11.46 degrees",
         BoneAngle(w[17], s.rest[17]), 11.459, 0.01);
    Check(w[18][9] > s.rest[18][9] + 15.0f,
          "a plus spread goes to the thumb side (+x of the mesh)", w[18][9],
          s.rest[18][9] + 15.0);
    Near("and it does not bend the finger toward the palm, units",
         w[18][11] - s.rest[18][11], 0.0, 1e-3);
    Near("the knuckle stays", Gap(&w[17][9], &s.rest[17][9]), 0.0, 1e-3);
    hm::FingerPose thumb = open;
    thumb.spread[0] = 0.2f;
    hm::PoseBones(s, thumb, w);
    Near("the spread of the thumb turns its base",
         BoneAngle(w[3], s.rest[3]), 11.459, 0.01);
  }

  printf("\nThe hand model: the place of the mesh on the hand\n");
  float rest_turn[9];
  hx::RestTurn(0.0f, 0.0f, 0.0f, rest_turn);
  hm::Placement place;
  hm::MakePlacement(s, rest_turn, mesh_length, &place);
  {
    hm::FingerPose rest = open;
    rest.curl[1] = om::kRestCurl;
    float tip_mesh[3], tip_hand[3], want[3];
    hm::IndexTipMesh(s, rest, tip_mesh);
    hm::MeshToHand(place, tip_mesh, tip_hand);
    om::FingerTip(1, om::kRestCurl, want);
    Near("the index tip of the mesh at the rest curl is the point of the Hand",
         Gap(tip_hand, want), 0.0, 1e-5);
    Check(hm::Det3(place.a) < 0.0f,
          "the map has a mirror: the stored left hand is a right hand",
          hm::Det3(place.a), -1.0);
    // The axes: the fingers along -y, the palm to -x, the thumb side to +z.
    const float along[3] = {0.0f, 1.0f, 0.0f}, palm[3] = {0.0f, 0.0f, 1.0f};
    const float thumb[3] = {1.0f, 0.0f, 0.0f};
    float h[3];
    hm::MeshToHandVector(place, along, h);
    Near("the fingers of the mesh go along -y of the hand frame",
         h[1] * mesh_length, -1.0, 1e-4);
    hm::MeshToHandVector(place, palm, h);
    Near("the palm of the mesh faces -x", h[0] * mesh_length, -1.0, 1e-4);
    hm::MeshToHandVector(place, thumb, h);
    Near("the thumb side of the mesh is +z, the index side",
         h[2] * mesh_length, 1.0, 1e-4);
    float wrist[3], middle_tip_mesh[3], middle_tip[3];
    hm::MeshToHand(place, &s.rest[0][9], wrist);
    const float end[3] = {0.0f, 0.0f, s.tip[2]};
    hx::ModelToWorld(s.rest[10], end, middle_tip_mesh);
    hm::MeshToHand(place, middle_tip_mesh, middle_tip);
    Near("the wrist joint to the end of the middle finger is 1 hand length",
         Gap(wrist, middle_tip), 1.0, 1e-4);
    // The trigger bends the index finger of the mesh, and the tip leaves the
    // point of the Hand as the tip of the low poly hand does.
    hm::FingerPose pulled = rest;
    pulled.curl[1] = 1.0f;
    hm::IndexTipMesh(s, pulled, tip_mesh);
    hm::MeshToHand(place, tip_mesh, tip_hand);
    Check(tip_hand[0] < want[0] - 0.05f,
          "a full trigger takes the index tip toward the palm side",
          tip_hand[0], want[0] - 0.05);
  }
  {
    // A rest turn of 10 degrees of yaw turns the fingers of the mesh, and
    // the index tip stays on the point of the Hand.
    float turned[9];
    hx::RestTurn(10.0f, 0.0f, 0.0f, turned);
    hm::Placement p2;
    hm::MakePlacement(s, turned, mesh_length, &p2);
    hm::FingerPose rest = open;
    rest.curl[1] = om::kRestCurl;
    float tip_mesh[3], tip_hand[3], want[3];
    hm::IndexTipMesh(s, rest, tip_mesh);
    hm::MeshToHand(p2, tip_mesh, tip_hand);
    om::FingerTip(1, om::kRestCurl, want);
    Near("with a rest turn the index tip is still the point of the Hand",
         Gap(tip_hand, want), 0.0, 1e-5);
    const float along[3] = {0.0f, 1.0f, 0.0f};
    float h[3], h0[3];
    hm::MeshToHandVector(p2, along, h);
    hm::MeshToHandVector(place, along, h0);
    Near("and the fingers turn by the rest turn, degrees",
         hx::AngleDeg(h, h0), 10.0, 1e-3);
  }

  printf("\nThe hand model: the map to the world and the skin\n");
  {
    const float position[3] = {120.0f, 40.0f, -75.0f};
    // A grip turned about two axes.
    const float ca = cosf(0.6f), sa = sinf(0.6f), cb = cosf(-0.9f),
                sb = sinf(-0.9f);
    const float ry[9] = {ca, 0.0f, sa, 0.0f, 1.0f, 0.0f, -sa, 0.0f, ca};
    const float rx[9] = {1.0f, 0.0f, 0.0f, 0.0f, cb, -sb, 0.0f, sb, cb};
    float rotation[9];
    hx::Multiply3(ry, rx, rotation);
    const float length = 1.425f;
    hm::FingerPose pose = open;
    pose.curl[1] = 0.4f;
    pose.curl[3] = 0.8f;
    pose.spread[4] = -0.1f;
    hm::PoseBones(s, pose, world);
    hm::SkinMatrices(s, world, skin);
    float mirror_side[2] = {0.0f, 0.0f};
    double worst = 0.0;
    for (int hand = 0; hand < 2; ++hand) {
      const bool left = hand == 0;
      float m[hm::kBones][9], o[hm::kBones][3];
      hm::WorldMaps(place, position, rotation, length, left, skin, m, o,
                    &mirror_side[hand]);
      // A point of the rest mesh on each bone: one map against the chain of
      // the skin, the place on the hand and the place in the world.
      for (int b = 0; b < hm::kBones; ++b) {
        const float p[3] = {s.rest[b][9] + 7.0f, s.rest[b][10] - 3.0f,
                            s.rest[b][11] + 11.0f};
        float one[3], skinned[3], in_hand[3], chain[3];
        for (int r = 0; r < 3; ++r) {
          one[r] = m[b][r * 3] * p[0] + m[b][r * 3 + 1] * p[1] +
                   m[b][r * 3 + 2] * p[2] + o[b][r];
        }
        hm::SkinPoint(skin[b], p, skinned);
        hm::MeshToHand(place, skinned, in_hand);
        om::PlacePoint(position, rotation, length, left, in_hand, chain);
        worst = fmax(worst, Gap(one, chain));
      }
    }
    Near("one map for each bone gives the place of the long way, world units",
         worst, 0.0, 1e-3);
    Near("the right hand is the mirror image of the stored mesh",
         mirror_side[1], -1.0, 0.0);
    Near("the left hand is the stored mesh", mirror_side[0], 1.0, 0.0);
    // A vertex with two bones is between its two places.
    const float p[3] = {-70.0f, -200.0f, 5.0f};
    float a[3], b[3];
    hm::SkinPoint(skin[14], p, a);
    hm::SkinPoint(skin[15], p, b);
    Check(Gap(a, b) > 1.0, "two bones give one rest point two places",
          Gap(a, b), 1.0);
  }
  {
    // THE FRONT OF A TRIANGLE. The camera is at the origin and looks along
    // +z. The outside of this triangle is +z, away from the camera.
    const float c0[3] = {0.0f, 0.0f, 5.0f}, c1[3] = {1.0f, 0.0f, 5.0f};
    const float c2[3] = {0.0f, 1.0f, 5.0f};
    Check(!hm::FacesCamera(c0, c1, c2, 1.0f),
          "a triangle with its outside away from the camera is not drawn", 0,
          0);
    Check(hm::FacesCamera(c0, c2, c1, 1.0f),
          "the same triangle with the other turn faces the camera", 1, 1);
    Check(hm::FacesCamera(c0, c1, c2, -1.0f),
          "a mirror in the map turns the triangle, so it faces the camera", 1,
          1);
    const float side[3] = {4.0f, 0.0f, 5.0f};
    const float s1[3] = {5.0f, 0.0f, 5.0f}, s2[3] = {4.0f, 1.0f, 5.0f};
    Check(!hm::FacesCamera(side, s1, s2, 1.0f),
          "and the same at the side of the view", 0, 0);
  }

  printf("\nThe hand model: the hold, the light and the look\n");
  {
    double worst_length = 0.0;
    for (int f = 0; f < hm::kFingers; ++f) {
      float at_rest[om::kJoints + 1][3];
      hm::MeshFingerJoints(s, place, f, 0.0f, at_rest);
      for (int step = 1; step <= 4; ++step) {
        float joints[om::kJoints + 1][3];
        hm::MeshFingerJoints(s, place, f, 0.25f * static_cast<float>(step),
                             joints);
        for (int j = 0; j < om::kJoints; ++j) {
          worst_length = fmax(worst_length,
                              fabs(Gap(joints[j], joints[j + 1]) -
                                   Gap(at_rest[j], at_rest[j + 1])));
        }
      }
    }
    Near("the joints of each finger keep their distances at each curl, hand "
         "lengths", worst_length, 0.0, 1e-4);
    float joints[om::kJoints + 1][3];
    hm::MeshFingerJoints(s, place, 2, 0.0f, joints);
    Near("the bones of the middle finger are 105 mesh units after the knuckle",
         Gap(joints[0], joints[1]) * mesh_length, 105.0, 0.01);
    Near("and its tip is 80 mesh units after the last joint",
         Gap(joints[2], joints[3]) * mesh_length, 80.0, 0.01);
  }
  {
    om::PoseRange small_ball, large_ball;
    hm::MeshHoldRange(s, place, 0.12f, &small_ball);
    hm::MeshHoldRange(s, place, 0.30f, &large_ball);
    bool opens = true, ordered = true;
    for (int f = 1; f < hm::kFingers; ++f) {
      opens = opens && large_ball.hi[f] <= small_ball.hi[f];
      ordered = ordered && small_ball.lo[f] <= small_ball.hi[f] &&
                large_ball.lo[f] <= large_ball.hi[f];
    }
    Check(opens, "a larger object opens each finger of the mesh more",
          large_ball.hi[2], small_ball.hi[2]);
    Check(ordered, "the open end of each range is not past its closed end", 1,
          1);
    Check(large_ball.hi[2] < 1.0f,
          "the middle finger touches a ball of 0.30 hand lengths before a fist",
          large_ball.hi[2], 1.0);
    // At the contact no joint is in the ball.
    float center[3];
    om::HoldCenter(0.30f, center);
    float joints[om::kJoints + 1][3];
    hm::MeshFingerJoints(s, place, 2, large_ball.hi[2], joints);
    double least = 1e9;
    for (int j = 1; j <= om::kJoints; ++j) {
      least = fmin(least, Gap(joints[j], center));
    }
    Check(least >= 0.30 + om::kShape[2].radius - 1e-4,
          "at the contact no joint of the finger is in the ball", least,
          0.30 + om::kShape[2].radius);
  }
  Near("the light of the game on a face away from the light is 0.4",
       hm::GameLightLevel(-0.7f), 0.4, 1e-6);
  Near("a face at 0.25 to the light has 0.725", hm::GameLightLevel(0.25f),
       0.725, 1e-6);
  Near("a face at 0.5 to the light has the full light",
       hm::GameLightLevel(0.5f), 1.0, 1e-6);
  Near("and a face toward the light too", hm::GameLightLevel(1.0f), 1.0, 1e-6);

  Near("an alignment of 0.5 gives the texture weight 128",
       hm::AlignmentWeight(0.5f), 128.0, 0.0);
  Near("an alignment of -1 gives 255", hm::AlignmentWeight(-1.0f), 255.0, 0.0);
  Near("an alignment of -0.03 gives 7", hm::AlignmentWeight(-0.03f), 7.0, 0.0);
  Near("the weight 0 keeps the base texel", hm::MixTexel(0xF84Au, 0x1234u, 0),
       0xF84A, 0.0);
  Near("the weight 255 gives the other texel",
       hm::MixTexel(0xF84Au, 0x1234u, 255), 0x1234, 0.0);
  // 128 of 255: each channel goes half the way, rounded down.
  Near("the weight 128 mixes each channel of 4 bits, the alpha too",
       hm::MixTexel(0xF000u, 0x1E60u, 128), 0x7730, 0.0);
  Near("a texel of 4 bits a channel as 8 bits a channel",
       hm::Texel32(0xF84Au), static_cast<double>(0xFF8844AAu), 0.0);
  {
    hm::Look look;
    hm::LookOf(-0.5f, &look);
    Check(look.evil, "an alignment below 0 mixes with the evil mesh", 1, 1);
    Near("with the size of the alignment as the weight", look.weight, 0.5,
         1e-6);
    hm::LookOf(0.25f, &look);
    Check(!look.evil, "an alignment above 0 mixes with the good mesh", 0, 0);
    Near("and its texture weight is 64", look.texture, 64.0, 0.0);
    hm::LookOf(-7.0f, &look);
    Near("an alignment past -1 counts as -1", look.weight, 1.0, 1e-6);
  }

  printf("\nThe hand model: a hand from a file of the player\n");
  {
    // The joints of a file: the joints of the game through a turn, a size
    // of 0.0003, an offset, and with or without a mirror of x.
    float game[hm::kBones][3];
    for (int b = 0; b < hm::kBones; ++b) {
      for (int k = 0; k < 3; ++k) game[b][k] = s.rest[b][9 + k];
    }
    const float ca = cosf(0.8f), sa = sinf(0.8f), cb = cosf(0.3f),
                sb = sinf(0.3f);
    const float ry[9] = {ca, 0.0f, sa, 0.0f, 1.0f, 0.0f, -sa, 0.0f, ca};
    const float rz[9] = {cb, -sb, 0.0f, sb, cb, 0.0f, 0.0f, 0.0f, 1.0f};
    float turn[9];
    hx::Multiply3(ry, rz, turn);
    const float size = 0.0003f;
    const float shift[3] = {0.4f, -1.2f, 0.25f};
    for (int mirrored = 0; mirrored < 2; ++mirrored) {
      float file[hm::kBones][3];
      for (int b = 0; b < hm::kBones; ++b) {
        const float q[3] = {mirrored != 0 ? -game[b][0] : game[b][0],
                            game[b][1], game[b][2]};
        float turned[3];
        hx::Turn(turn, q, turned);
        for (int k = 0; k < 3; ++k) file[b][k] = size * turned[k] + shift[k];
      }
      hm::Fit fit;
      Check(hm::FitJoints(file, game, &fit),
            mirrored != 0 ? "a file with a mirror has a fit"
                          : "a file with no mirror has a fit", 1, 1);
      Near("each bone joint of the file is on its place, mesh units", fit.worst,
           0.0, 0.05);
      Near("the fit finds the mirror", fit.mirror ? 1.0 : 0.0,
           mirrored != 0 ? 1.0 : 0.0, 0.0);
      Near("the fit finds the size: one unit of the file in mesh units",
           fit.scale, 1.0 / size, 0.5);
      // A point of the file that is no joint goes to its place too.
      const float p_game[3] = {-12.0f, -345.0f, 67.0f};
      const float q[3] = {mirrored != 0 ? -p_game[0] : p_game[0], p_game[1],
                          p_game[2]};
      float turned[3], p_file[3], back[3];
      hx::Turn(turn, q, turned);
      for (int k = 0; k < 3; ++k) p_file[k] = size * turned[k] + shift[k];
      hm::FitPoint(fit, p_file, back);
      Near("a vertex of the file goes to its place in the mesh, units",
           Gap(back, p_game), 0.0, 0.05);
      const float n_game[3] = {0.0f, 0.0f, 1.0f};
      const float nq[3] = {0.0f, 0.0f, 1.0f};
      float n_file[3], n_back[3];
      hx::Turn(turn, nq, n_file);
      hm::FitNormal(fit, n_file, n_back);
      Near("a normal of the file goes to its direction, with the length 1",
           Gap(n_back, n_game), 0.0, 1e-4);

      // A bone that the player moved: 30 mesh units in the file.
      float moved[hm::kBones][3];
      for (int b = 0; b < hm::kBones; ++b) {
        for (int k = 0; k < 3; ++k) moved[b][k] = file[b][k];
      }
      moved[12][1] += 30.0f * size;
      hm::Fit bad;
      hm::FitJoints(moved, game, &bad);
      Near("a bone that was moved by 30 units is 30 units from its place",
           bad.worst, 30.0, 0.1);
      Near("and the fit names that bone", bad.worst_bone, 12.0, 0.0);
      Check(bad.worst > hm::kFitTolerance * mesh_length,
            "30 units are past the limit of 2 % of the hand, so the file is "
            "not used", bad.worst, hm::kFitTolerance * mesh_length);
    }
    float line[hm::kBones][3];
    for (int b = 0; b < hm::kBones; ++b) {
      line[b][0] = static_cast<float>(b);
      line[b][1] = line[b][2] = 0.0f;
    }
    hm::Fit none;
    Check(!hm::FitJoints(line, game, &none),
          "joints on one line give no fit", 0, 0);
  }
  Near("the bone of the name b07_middle0 is 7", hm::BoneOfName("b07_middle0"),
       7.0, 0.0);
  Near("the bone of the name b21_thumb3 is 21", hm::BoneOfName("b21_thumb3"),
       21.0, 0.0);
  Near("a name with only the number is a bone", hm::BoneOfName("b00"), 0.0,
       0.0);
  Near("b22 is no bone of the hand", hm::BoneOfName("b22_more"), -1.0, 0.0);
  Near("a name of another kind is no bone", hm::BoneOfName("Bone.001"), -1.0,
       0.0);
  Near("a name with one digit is no bone", hm::BoneOfName("b7_middle0"), -1.0,
       0.0);
}

}  // namespace hand_model_checks
