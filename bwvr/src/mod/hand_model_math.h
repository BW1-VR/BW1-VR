// The math of the hand model, as pure functions with no game state.
//
// hand_model.cpp calls these with the mesh and the bones that it reads from
// the game's own files, and test/controller_math calls them with a made up
// hand. Nothing here reads a global or touches the game. See docs/hand-model.md,
// "The game's hand on both real hands".
//
// TWO CONVENTIONS, as in hand_exact_math.h. A bone is a game matrix of 4 rows
// of 3: a point is a row, `point * matrix`, and a bone in the frame of the
// mesh is `bone * parent`. A 3x3 matrix of the hand frame is m[r * 3 + c] and
// a vector is a column.
//
// THE MESH FRAME is the frame of the stored mesh (hand_exact_math.h, THE
// MESH): +x to the thumb, +y along the fingers, +z out of the palm, and the
// origin at the tip of the index finger. The stored mesh is a left hand.
//
// THE HAND FRAME is the frame of overlay_math.h: the grip pose of the right
// hand, with the palm to -x, the fingers along -y and +z from the little
// finger to the index finger. One unit is the length of the hand. The left
// hand is the mirror image in x.

#pragma once

#include <math.h>
#include <stdint.h>

#include "hand_exact_math.h"
#include "overlay_math.h"

namespace hand_model {

namespace hx = hand_exact_math;

const int kBones = 22;
const int kFingers = overlay_math::kFingers;  // 0 the thumb ... 4 the little
const int kChain = 4;  // the bones of a finger, from the palm to the tip

// THE BONES OF EACH FINGER in the game's file, from the palm to the tip. The
// first bone of a finger is in the palm, from the root of the fingers to the
// knuckle. The thumb has three bones, and its first one is its base.
const int kFingerBones[kFingers][kChain] = {{3, 20, 21, -1},
                                            {4, 17, 18, 19},
                                            {7, 8, 9, 10},
                                            {5, 14, 15, 16},
                                            {6, 11, 12, 13}};

// THE BEND OF EACH BONE AT A CURL OF 1, in degrees, about the bone's own x.
// These are the game's own poses, measured in hh.HBN: frame 1 of the
// animation `point` for the thumb and the middle, ring and little fingers,
// which is the tightest pose of the file, and `tap_house` for the index
// finger. The animations of the game bend a finger about the x of its bones
// and about no other axis. A curl of 0 is the rest pose of the file.
const float kFullBendDeg[kFingers][kChain] = {{0.0f, 44.9f, 90.3f, 0.0f},
                                              {0.0f, 73.8f, 66.6f, 66.6f},
                                              {0.0f, 89.4f, 83.5f, 53.9f},
                                              {0.0f, 113.8f, 94.3f, 64.7f},
                                              {0.0f, 123.7f, 70.9f, 59.3f}};

// The bone of a finger that takes the spread: the first one that bends. The
// thumb spreads at its base.
inline int SpreadLink(int finger) { return finger == 0 ? 0 : 1; }

// THE SKELETON of the hand, as the L3D file gives it.
struct Skeleton {
  int parent[kBones];
  float local[kBones][12];  // the rest pose, in the frame of the parent
  float rest[kBones][12];   // the rest pose, in the frame of the mesh
  float rest_inverse[kBones][12];
  // The length of the last bone of each finger: to the end of its vertices.
  float tip[kFingers];
};

// Fills rest and rest_inverse from local and parent. False when a bone comes
// before its parent or a matrix has no inverse.
inline bool PrepareSkeleton(Skeleton* s) {
  for (int b = 0; b < kBones; ++b) {
    const int p = s->parent[b];
    if (p >= b) return false;
    if (p < 0) {
      for (int i = 0; i < 12; ++i) s->rest[b][i] = s->local[b][i];
    } else {
      hx::MultiplyGame(s->local[b], s->rest[p], s->rest[b]);
    }
    if (!hx::InverseGame(s->rest[b], s->rest_inverse[b])) return false;
  }
  return true;
}

// THE FINGER POSE: all that the hand model needs to draw a hand. Each finger
// has one curl, one spread and one outer bend, and a caller can set each one
// by itself. The thumb also has a turn.
//
//   curl    0 is the rest pose of the mesh, an open hand. 1 is a fist. The
//           curl scales the bend of each bone of the finger (kFullBendDeg).
//   spread  radians about the palm normal at the knuckle, plus toward the
//           thumb side. 0 is the rest pose. For the thumb it turns the base.
//   outer   the bend that the outer joints take on top of the curl, as a
//           share of their full bend. The outer joints are the two joints
//           past the knuckle of a finger, and the last joint of the thumb.
//           So a curl of 0.2 with an outer bend of 0.5 bends the knuckle by
//           0.2 and the outer joints by 0.7 of their full bend. 0 bends each
//           joint by the curl.
//   thumb_turn  0 to 1. The turn of the thumb toward the palm at its base
//           (kThumbTurn): 1 is the thumb of the game's own open hold pose.
struct FingerPose {
  float curl[kFingers];
  float spread[kFingers];
  float outer[kFingers];
  float thumb_turn;
};

inline void OpenPose(FingerPose* pose) {
  for (int f = 0; f < kFingers; ++f) {
    pose->curl[f] = pose->spread[f] = pose->outer[f] = 0.0f;
  }
  pose->thumb_turn = 0.0f;
}

// The first joint of a finger chain that is an outer joint: the joints past
// the knuckle of a finger (chain 2 and 3), and the last joint of the thumb
// (chain 2, bone 21).
const int kFirstOuter = 2;

// THE TURN OF THE THUMB TOWARD THE PALM. The game's own open hold pose
// (hold_side, frame 1, "The hold pose of the game" in docs/hand-model.md)
// turns the base of the thumb (bone 3) and its next bone (bone 20) as well as
// bending it. These are those two turns, measured in hh.HBN: an axis in the
// frame of the bone and an angle, with the bend of the bone left out. The
// turn of bone 3 is mostly about the long axis of the thumb, so it turns the
// pad of the thumb toward the fingers, and the tip of the open thumb goes out
// in front of the palm. The game's closed hold pose (hold_side, frame 0) has
// 10.8 degrees of the first turn. The game's animations turn the thumb so,
// and the mesh shows these turns with no gap.
struct ThumbTurn {
  int bone;
  float axis[3];
  float degrees;
};
const ThumbTurn kThumbTurn[2] = {{3, {-0.1184f, 0.3288f, 0.9370f}, 42.04f},
                                 {20, {-0.3056f, 0.9209f, -0.2421f}, 30.28f}};

// A turn about an axis, as a game matrix (a point is a row), in
// the sense of the turns that the game's animations give a bone.
inline void AxisTurn(const float axis[3], float radians, float out[12]) {
  const float c = cosf(radians), s = sinf(radians), t = 1.0f - c;
  const float n = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] +
                        axis[2] * axis[2]);
  const float inv = n > 1e-6f ? 1.0f / n : 0.0f;
  const float x = axis[0] * inv, y = axis[1] * inv, z = axis[2] * inv;
  // The turn of a column vector. The rows of the game matrix are its
  // columns.
  const float col[9] = {t * x * x + c,     t * x * y - s * z, t * x * z + s * y,
                        t * x * y + s * z, t * y * y + c,     t * y * z - s * x,
                        t * x * z - s * y, t * y * z + s * x, t * z * z + c};
  for (int r = 0; r < 3; ++r) {
    for (int k = 0; k < 3; ++k) out[r * 3 + k] = col[k * 3 + r];
  }
  out[9] = out[10] = out[11] = 0.0f;
}

// The turn of a bone in its own frame, as a game matrix: a bend toward the
// palm about x, then a spread about y. The length of a bone is along its z
// and its y goes out of the palm, so a bend turns z toward +y.
inline void BoneTurn(float bend, float spread, float out[12]) {
  const float cb = cosf(bend), sb = sinf(bend);
  const float cs = cosf(spread), ss = sinf(spread);
  // The rows of Rx, then of Ry. A point goes through Rx first.
  const float rx[12] = {1.0f, 0.0f, 0.0f, 0.0f, cb,   -sb,
                        0.0f, sb,   cb,   0.0f, 0.0f, 0.0f};
  const float ry[12] = {cs,   0.0f, -ss,  0.0f, 1.0f, 0.0f,
                        ss,   0.0f, cs,   0.0f, 0.0f, 0.0f};
  hx::MultiplyGame(rx, ry, out);
}

// THE BONES OF A POSE in the frame of the mesh. A finger bone is its turn
// times its rest matrix, and each other bone keeps its rest matrix.
inline void PoseBones(const Skeleton& s, const FingerPose& pose,
                      float world[kBones][12]) {
  float bend[kBones], spread[kBones];
  for (int b = 0; b < kBones; ++b) bend[b] = spread[b] = 0.0f;
  const float k = 3.14159265f / 180.0f;
  for (int f = 0; f < kFingers; ++f) {
    const float curl = overlay_math::Clamp01(pose.curl[f]);
    const float outer = overlay_math::Clamp01(pose.curl[f] + pose.outer[f]);
    for (int j = 0; j < kChain; ++j) {
      const int b = kFingerBones[f][j];
      if (b < 0) continue;
      bend[b] = (j >= kFirstOuter ? outer : curl) * kFullBendDeg[f][j] * k;
      if (j == SpreadLink(f)) spread[b] = pose.spread[f];
    }
  }
  const float thumb_turn = overlay_math::Clamp01(pose.thumb_turn);
  for (int b = 0; b < kBones; ++b) {
    float local[12];
    const ThumbTurn* extra = nullptr;
    if (thumb_turn > 0.0f) {
      for (int t = 0; t < 2; ++t) {
        if (kThumbTurn[t].bone == b) extra = &kThumbTurn[t];
      }
    }
    if (bend[b] != 0.0f || spread[b] != 0.0f || extra != nullptr) {
      float turn[12];
      BoneTurn(bend[b], spread[b], turn);
      if (extra != nullptr) {
        // The bend and the spread, then the turn of the thumb, in the frame
        // of the bone.
        float more[12], both[12];
        AxisTurn(extra->axis, thumb_turn * extra->degrees * k, more);
        hx::MultiplyGame(turn, more, both);
        for (int i = 0; i < 12; ++i) turn[i] = both[i];
      }
      hx::MultiplyGame(turn, s.local[b], local);
    } else {
      for (int i = 0; i < 12; ++i) local[i] = s.local[b][i];
    }
    const int p = s.parent[b];
    if (p < 0) {
      for (int i = 0; i < 12; ++i) world[b][i] = local[i];
    } else {
      hx::MultiplyGame(local, world[p], world[b]);
    }
  }
}

// THE SKIN MATRIX of each bone: a point of the rest mesh goes through it to
// its place in the pose. skin = inverse(rest) * world.
inline void SkinMatrices(const Skeleton& s, const float world[kBones][12],
                         float skin[kBones][12]) {
  for (int b = 0; b < kBones; ++b) {
    hx::MultiplyGame(s.rest_inverse[b], world[b], skin[b]);
  }
}

// A point and a direction of the rest mesh through a skin matrix.
inline void SkinPoint(const float skin[12], const float p[3], float out[3]) {
  hx::ModelToWorld(skin, p, out);
}
inline void SkinVector(const float skin[12], const float v[3], float out[3]) {
  for (int i = 0; i < 3; ++i) {
    out[i] = v[0] * skin[0 + i] + v[1] * skin[3 + i] + v[2] * skin[6 + i];
  }
}

// THE FOLD LIMIT OF THE OUTER JOINTS of one finger, from the mesh. Each
// vertex of the mesh goes with its bones, and a joint has no skin that
// stretches. So a triangle that has its corners on two bones of a joint is
// sheared when the joint bends, and past some bend it folds over and makes a
// sharp crease. For each triangle of the finger with its corners on more
// than one bone, the mod compares its normal in the pose with its normal at
// rest turned by the bones of its corners. The limit is the largest outer
// curl, on a grid of kFoldSteps with the knuckle at the rest curl, at which
// no such triangle turned more than kFoldDeg from that normal. The vertices
// are those of the rest mesh, with up to 4 bones and weights each.
const float kFoldDeg = 45.0f;
const int kFoldSteps = 32;

inline float OuterFoldLimit(const Skeleton& s, int finger, int vertices,
                            const float* rest, const uint8_t* bones,
                            const float* weights, int triangles,
                            const uint32_t* index) {
  if (finger < 0 || finger >= kFingers || vertices <= 0 || triangles <= 0) {
    return 1.0f;
  }
  // The bone of a vertex with the most weight, and whether it is a bone of
  // the finger.
  const auto main_bone = [&](int v) {
    int best = bones[v * 4];
    float most = weights[v * 4];
    for (int k = 1; k < 4; ++k) {
      if (weights[v * 4 + k] > most) {
        most = weights[v * 4 + k];
        best = bones[v * 4 + k];
      }
    }
    return best;
  };
  const auto in_finger = [&](int b) {
    for (int j = 0; j < kChain; ++j) {
      if (kFingerBones[finger][j] == b) return true;
    }
    return false;
  };
  const float cos_limit = cosf(kFoldDeg * 3.14159265f / 180.0f);
  float limit = 1.0f;
  for (int step = 1; step <= kFoldSteps; ++step) {
    const float outer = static_cast<float>(step) / kFoldSteps;
    FingerPose pose;
    OpenPose(&pose);
    pose.curl[finger] = overlay_math::kRestCurl;
    pose.outer[finger] = outer - overlay_math::kRestCurl;
    float world[kBones][12], skin[kBones][12];
    PoseBones(s, pose, world);
    SkinMatrices(s, world, skin);
    bool folded = false;
    for (int t = 0; t < triangles && !folded; ++t) {
      const uint32_t* tri = index + t * 3;
      int corner_bone[3];
      bool all = true, two = false;
      for (int c = 0; c < 3; ++c) {
        if (tri[c] >= static_cast<uint32_t>(vertices)) {
          all = false;
          break;
        }
        corner_bone[c] = main_bone(static_cast<int>(tri[c]));
        if (!in_finger(corner_bone[c])) all = false;
        if (c > 0 && corner_bone[c] != corner_bone[0]) two = true;
      }
      if (!all || !two) continue;
      float p[3][3], q[3][3];
      for (int c = 0; c < 3; ++c) {
        const int v = static_cast<int>(tri[c]);
        for (int k = 0; k < 3; ++k) {
          p[c][k] = rest[v * 3 + k];
          q[c][k] = 0.0f;
        }
        for (int k = 0; k < 4; ++k) {
          const float w = weights[v * 4 + k];
          if (!(w > 0.0f)) continue;
          float moved[3];
          SkinPoint(skin[bones[v * 4 + k]], p[c], moved);
          for (int i = 0; i < 3; ++i) q[c][i] += w * moved[i];
        }
      }
      float e1[3], e2[3], rest_n[3], pose_n[3];
      for (int k = 0; k < 3; ++k) {
        e1[k] = p[1][k] - p[0][k];
        e2[k] = p[2][k] - p[0][k];
      }
      hx::Cross(e1, e2, rest_n);
      if (!hx::Normalize(rest_n)) continue;
      for (int k = 0; k < 3; ++k) {
        e1[k] = q[1][k] - q[0][k];
        e2[k] = q[2][k] - q[0][k];
      }
      hx::Cross(e1, e2, pose_n);
      if (!hx::Normalize(pose_n)) {
        folded = true;
        break;
      }
      // The rest normal turned by the bone of each corner, summed.
      float want[3] = {0.0f, 0.0f, 0.0f};
      for (int c = 0; c < 3; ++c) {
        float turned[3];
        SkinVector(skin[corner_bone[c]], rest_n, turned);
        for (int k = 0; k < 3; ++k) want[k] += turned[k];
      }
      if (!hx::Normalize(want)) continue;
      if (hx::Dot(pose_n, want) < cos_limit) folded = true;
    }
    if (folded) break;
    limit = outer;
  }
  return limit;
}

// THE PLACE OF THE MESH in the hand frame of the right hand:
//
//   p hand = a * (p mesh) + t
//
// a is the rest turn Q of the mesh (hand_exact_math.h), then the change of
// axes with the mirror that makes the stored left hand a right hand (x of the
// hand is -z of the mesh, y is -y and z is x), then the size: one length of
// the mesh is one unit. This is the turn of the exact root, so the mesh lies
// on the real hand as the game's Hand did.
struct Placement {
  float a[9];
  float t[3];
};

inline void MeshToHand(const Placement& place, const float p[3], float out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = place.a[r * 3 + 0] * p[0] + place.a[r * 3 + 1] * p[1] +
             place.a[r * 3 + 2] * p[2] + place.t[r];
  }
}
inline void MeshToHandVector(const Placement& place, const float v[3],
                             float out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = place.a[r * 3 + 0] * v[0] + place.a[r * 3 + 1] * v[1] +
             place.a[r * 3 + 2] * v[2];
  }
}

// The tip of the index finger of the mesh in the frame of the mesh, for a
// pose. The tip is the origin of the rest mesh, and it moves with the last
// bone of the index finger.
inline void IndexTipMesh(const Skeleton& s, const FingerPose& pose,
                         float out[3]) {
  float world[kBones][12], skin[kBones][12];
  PoseBones(s, pose, world);
  SkinMatrices(s, world, skin);
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  SkinPoint(skin[kFingerBones[1][kChain - 1]], origin, out);
}

// THE ANCHOR. The point of the Hand is the tip of the real index finger at
// the rest curl (overlay_math::FingerTip at kRestCurl), and each interaction
// of the mod uses that point. So the mesh is moved until the tip of its own
// index finger, at the same rest curl, is on that point. The palm of the mesh
// is then at the palm that the code uses (overlay_math::HoldCenter), within a
// few mm for the game's mesh.
inline void MakePlacement(const Skeleton& s, const float rest_turn[9],
                          float mesh_length, Placement* out) {
  const float axes[9] = {0.0f, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f,
                         1.0f, 0.0f, 0.0f};
  hx::Multiply3(axes, rest_turn, out->a);
  const float scale = mesh_length > 0.0f ? 1.0f / mesh_length : 1.0f;
  for (int i = 0; i < 9; ++i) out->a[i] *= scale;
  for (int i = 0; i < 3; ++i) out->t[i] = 0.0f;
  FingerPose rest;
  OpenPose(&rest);
  rest.curl[1] = overlay_math::kRestCurl;
  float tip_mesh[3], tip_hand[3], want[3];
  IndexTipMesh(s, rest, tip_mesh);
  MeshToHand(*out, tip_mesh, tip_hand);
  overlay_math::FingerTip(1, overlay_math::kRestCurl, want);
  for (int i = 0; i < 3; ++i) out->t[i] = want[i] - tip_hand[i];
}

// THE MAP TO THE WORLD of one hand, for each bone:
//
//   world = m * p + o
//
// with p a point of the rest mesh. It is the skin matrix of the bone, then
// the place of the mesh in the hand frame, then overlay_math::PlacePoint: the
// mirror of the left hand, the size and the turn of the grip. `mirror` is -1
// when the whole map has a mirror in it and 1 when not, for FacesCamera.
inline void WorldMaps(const Placement& place, const float position[3],
                      const float rotation[9], float length, bool left,
                      const float skin[kBones][12], float m[kBones][9],
                      float o[kBones][3], float* mirror) {
  const float side = left ? -1.0f : 1.0f;
  float g[9], g0[3];
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      g[r * 3 + c] = length * (rotation[r * 3 + 0] * side * place.a[0 + c] +
                               rotation[r * 3 + 1] * place.a[3 + c] +
                               rotation[r * 3 + 2] * place.a[6 + c]);
    }
    g0[r] = position[r] + length * (rotation[r * 3 + 0] * side * place.t[0] +
                                    rotation[r * 3 + 1] * place.t[1] +
                                    rotation[r * 3 + 2] * place.t[2]);
  }
  const float det = g[0] * (g[4] * g[8] - g[5] * g[7]) -
                    g[1] * (g[3] * g[8] - g[5] * g[6]) +
                    g[2] * (g[3] * g[7] - g[4] * g[6]);
  *mirror = det < 0.0f ? -1.0f : 1.0f;
  for (int b = 0; b < kBones; ++b) {
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        // The skin matrix has rows, so its transpose acts on a column.
        m[b][r * 3 + c] = g[r * 3 + 0] * skin[b][c * 3 + 0] +
                          g[r * 3 + 1] * skin[b][c * 3 + 1] +
                          g[r * 3 + 2] * skin[b][c * 3 + 2];
      }
      o[b][r] = g[r * 3 + 0] * skin[b][9] + g[r * 3 + 1] * skin[b][10] +
                g[r * 3 + 2] * skin[b][11] + g0[r];
    }
  }
}

// THE JOINTS OF A FINGER of the mesh in the hand frame, for a curl and a
// spread: the knuckle, the two middle joints and the tip. The thumb gives its
// base, its two joints and its tip. The same form as
// overlay_math::FingerJoints.
// With an outer bend and a turn of the thumb (FingerPose): the knuckle takes
// the curl, the outer joints take the curl and the outer bend, and the thumb
// takes the turn.
inline void MeshFingerJointsPose(const Skeleton& s, const Placement& place,
                                 int finger, float curl, float outer,
                                 float spread, float thumb_turn,
                                 float joints[overlay_math::kJoints + 1][3]) {
  FingerPose pose;
  OpenPose(&pose);
  pose.curl[finger] = curl;
  pose.outer[finger] = outer;
  pose.spread[finger] = spread;
  if (finger == 0) pose.thumb_turn = thumb_turn;
  float world[kBones][12];
  PoseBones(s, pose, world);
  const int first = finger == 0 ? 0 : 1;
  int last = -1;
  for (int j = 0; j < overlay_math::kJoints; ++j) {
    const int b = kFingerBones[finger][first + j];
    last = b;
    MeshToHand(place, &world[b][9], joints[j]);
  }
  // The tip: the end of the last bone, along its z.
  const float end[3] = {0.0f, 0.0f, s.tip[finger]};
  float tip_mesh[3];
  hx::ModelToWorld(world[last], end, tip_mesh);
  MeshToHand(place, tip_mesh, joints[overlay_math::kJoints]);
}

inline void MeshFingerJointsSpread(const Skeleton& s, const Placement& place,
                                   int finger, float curl, float spread,
                                   float joints[overlay_math::kJoints + 1][3]) {
  MeshFingerJointsPose(s, place, finger, curl, 0.0f, spread, 0.0f, joints);
}

inline void MeshFingerJoints(const Skeleton& s, const Placement& place,
                             int finger, float curl,
                             float joints[overlay_math::kJoints + 1][3]) {
  MeshFingerJointsSpread(s, place, finger, curl, 0.0f, joints);
}

// THE CONTACT of a finger of the mesh with the ball of a held object, as
// overlay_math::ContactCurl does it for the test hand: the largest curl, in
// steps of 1/40, at which no joint and no middle of a bone is in the ball.
inline float MeshContactCurl(const Skeleton& s, const Placement& place,
                             int finger, const float center[3], float radius) {
  const int kSteps = 40;
  const float reach = radius + overlay_math::kShape[finger].radius;
  float free_curl = 0.0f;
  for (int i = 0; i <= kSteps; ++i) {
    const float curl = static_cast<float>(i) / kSteps;
    float joints[overlay_math::kJoints + 1][3];
    MeshFingerJoints(s, place, finger, curl, joints);
    for (int j = 1; j <= overlay_math::kJoints; ++j) {
      for (int half = 0; half < 2; ++half) {
        float d2 = 0.0f;
        for (int k = 0; k < 3; ++k) {
          const float p = half == 0 ? joints[j][k]
                                    : 0.5f * (joints[j][k] + joints[j - 1][k]);
          d2 += (p - center[k]) * (p - center[k]);
        }
        if (d2 < reach * reach) return free_curl;
      }
    }
    free_curl = curl;
  }
  return 1.0f;
}

// THE HOLD POSE of the mesh for an object of the given radius, in hand
// lengths. The rule of overlay_math::HoldRange, with the fingers of the mesh.
inline void MeshHoldRange(const Skeleton& s, const Placement& place,
                          float radius, overlay_math::PoseRange* out) {
  const float r = radius > overlay_math::kHoldMinRadius
                      ? radius
                      : overlay_math::kHoldMinRadius;
  float center[3];
  overlay_math::HoldCenter(r, center);
  for (int f = 0; f < kFingers; ++f) {
    const float contact = MeshContactCurl(s, place, f, center, r);
    out->hi[f] = contact;
    if (f <= 1) {
      out->lo[f] = overlay_math::kHoldLoose * contact;
    } else {
      out->lo[f] = contact < overlay_math::kRestCurl ? contact
                                                    : overlay_math::kRestCurl;
    }
  }
}

// THE LIGHT OF THE GAME on a vertex of a boned mesh [V fn_0084C3C0, 0x0084C626
// to 0x0084C67D]: the normal times the direction to the light, 0 when it
// faces away and times 1.3 when not, plus 0.4, and 1 at most. The game then
// multiplies the color of the object by this level.
inline float GameLightLevel(float n_dot_l) {
  float level = n_dot_l > 0.0f ? n_dot_l * 1.3f : 0.0f;
  level += 0.4f;
  return level > 1.0f ? 1.0f : level;
}

// THE MIX OF TWO TEXELS as the game does it for the alignment [V fn_00870640]:
// each of the four channels of 4 bits is (base * (255 - w) + other * w) / 255.
// The weight is the alignment times 256 as an integer, 255 at most.
inline int AlignmentWeight(float alignment) {
  const float a = alignment < 0.0f ? -alignment : alignment;
  int w = static_cast<int>(a * 256.0f);
  if (w > 255) w = 255;
  return w < 0 ? 0 : w;
}

inline uint16_t MixTexel(uint16_t base, uint16_t other, int weight) {
  uint32_t out = 0;
  for (int shift = 0; shift < 16; shift += 4) {
    const uint32_t a = (base >> shift) & 15u, b = (other >> shift) & 15u;
    const uint32_t mixed = (a * static_cast<uint32_t>(255 - weight) +
                            b * static_cast<uint32_t>(weight)) / 255u;
    out |= (mixed & 15u) << shift;
  }
  return static_cast<uint16_t>(out);
}

// A texel of 4 bits a channel as 8 bits a channel (A8R8G8B8).
inline uint32_t Texel32(uint16_t texel) {
  const uint32_t a = (texel >> 12) & 15u, r = (texel >> 8) & 15u;
  const uint32_t g = (texel >> 4) & 15u, b = texel & 15u;
  return (a * 17u) << 24 | (r * 17u) << 16 | (g * 17u) << 8 | (b * 17u);
}

// THE LOOK from the alignment, -1 (evil) to 1 (good), as the game mixes it
// [V Morphable::MorphVertices, Morphable::MorphTexture]: below 0 the other
// mesh and texture are the evil ones, from 0 the good ones, and the weight is
// the size of the alignment.
struct Look {
  bool evil;     // the other mesh is the evil one, else the good one
  float weight;  // 0 to 1, for the vertices
  int texture;   // 0 to 255, for the texture
};

inline void LookOf(float alignment, Look* out) {
  float a = alignment;
  if (!(a > -1.0f)) a = -1.0f;
  if (a > 1.0f) a = 1.0f;
  out->evil = a < 0.0f;
  out->weight = a < 0.0f ? -a : a;
  out->texture = AlignmentWeight(a);
}

// The sign of the determinant of a 3x3 matrix m[r * 3 + c].
inline float Det3(const float m[9]) {
  return m[0] * (m[4] * m[8] - m[5] * m[7]) -
         m[1] * (m[3] * m[8] - m[5] * m[6]) +
         m[2] * (m[3] * m[7] - m[4] * m[6]);
}

// THE FRONT OF A TRIANGLE. In the frame of the mesh the outside of a triangle
// is (v1 - v0) x (v2 - v0). The mesh goes through a chain of maps to the
// camera, and each mirror in the chain turns that cross product. `mirror` is
// -1 when the chain has an odd count of mirrors, and 1 when not. The corners
// are in camera space, where the camera is at the origin. True when the
// outside of the triangle faces the camera.
inline bool FacesCamera(const float c0[3], const float c1[3],
                        const float c2[3], float mirror) {
  const float e1[3] = {c1[0] - c0[0], c1[1] - c0[1], c1[2] - c0[2]};
  const float e2[3] = {c2[0] - c0[0], c2[1] - c0[1], c2[2] - c0[2]};
  float n[3];
  hx::Cross(e1, e2, n);
  return mirror * hx::Dot(n, c0) < 0.0f;
}

// --- a hand from a file of the player ----------------------------------------

// THE FIT OF A FILE. A hand that the player edited comes from a 3D editor,
// with the units and the axes of that editor. The mod does not trust them. It
// takes the places of the 22 bone joints in the file and in the game, and it
// finds the one map that puts the bones of the file on the bones of the game:
//
//   p game = m * (p file) + t
//
// The map is a turn, one size for all axes, and a mirror when the file needs
// one. It comes from three joints that are far apart: the wrist and the
// knuckles of the index finger and the little finger. Then each of the 22
// joints must be at its place. `worst` is the largest distance of a joint
// from its place, in mesh units, and `worst_bone` is that joint.
struct Fit {
  float m[9];
  float t[3];
  float scale;   // mesh units of the game in one unit of the file
  bool mirror;   // the map has a mirror in it
  float worst;
  int worst_bone;
};

// The frame of three points as the columns of e: e1 from a to b, e2 toward c
// and square to e1, and e3 = e1 x e2. False when the points are on one line.
inline bool FrameOf(const float a[3], const float b[3], const float c[3],
                    float e[9]) {
  float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
  if (!hx::Normalize(e1)) return false;
  const float along = hx::Dot(v, e1);
  float e2[3] = {v[0] - along * e1[0], v[1] - along * e1[1],
                 v[2] - along * e1[2]};
  if (!(hx::Length(e2) > 1e-6f * (hx::Length(v) + 1e-30f))) return false;
  if (!hx::Normalize(e2)) return false;
  float e3[3];
  hx::Cross(e1, e2, e3);
  for (int r = 0; r < 3; ++r) {
    e[r * 3 + 0] = e1[r];
    e[r * 3 + 1] = e2[r];
    e[r * 3 + 2] = e3[r];
  }
  return true;
}

const int kFitWrist = 0;
const int kFitIndex = 17;
const int kFitLittle = 11;

inline bool FitJoints(const float file[kBones][3], const float game[kBones][3],
                      Fit* out) {
  float ef[9], eg[9];
  if (!FrameOf(file[kFitWrist], file[kFitIndex], file[kFitLittle], ef) ||
      !FrameOf(game[kFitWrist], game[kFitIndex], game[kFitLittle], eg)) {
    return false;
  }
  float span_file = 0.0f, span_game = 0.0f;
  const int ends[2] = {kFitIndex, kFitLittle};
  for (int i = 0; i < 2; ++i) {
    const float df[3] = {file[ends[i]][0] - file[kFitWrist][0],
                         file[ends[i]][1] - file[kFitWrist][1],
                         file[ends[i]][2] - file[kFitWrist][2]};
    const float dg[3] = {game[ends[i]][0] - game[kFitWrist][0],
                         game[ends[i]][1] - game[kFitWrist][1],
                         game[ends[i]][2] - game[kFitWrist][2]};
    span_file += hx::Length(df);
    span_game += hx::Length(dg);
  }
  if (!(span_file > 0.0f) || !(span_game > 0.0f)) return false;
  const float scale = span_game / span_file;
  bool have = false;
  for (int pass = 0; pass < 2; ++pass) {
    // m = scale * eg * diag(1, 1, sign) * transpose(ef). The three joints are
    // on their places with each sign, and the other joints tell which sign
    // is the right one.
    const float sign = pass == 0 ? 1.0f : -1.0f;
    Fit fit;
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        fit.m[r * 3 + c] = scale * (eg[r * 3 + 0] * ef[c * 3 + 0] +
                                    eg[r * 3 + 1] * ef[c * 3 + 1] +
                                    sign * eg[r * 3 + 2] * ef[c * 3 + 2]);
      }
    }
    for (int r = 0; r < 3; ++r) {
      fit.t[r] = game[kFitWrist][r] -
                 (fit.m[r * 3 + 0] * file[kFitWrist][0] +
                  fit.m[r * 3 + 1] * file[kFitWrist][1] +
                  fit.m[r * 3 + 2] * file[kFitWrist][2]);
    }
    fit.scale = scale;
    fit.mirror = sign < 0.0f;
    fit.worst = 0.0f;
    fit.worst_bone = 0;
    for (int b = 0; b < kBones; ++b) {
      float d[3];
      for (int r = 0; r < 3; ++r) {
        d[r] = fit.m[r * 3 + 0] * file[b][0] + fit.m[r * 3 + 1] * file[b][1] +
               fit.m[r * 3 + 2] * file[b][2] + fit.t[r] - game[b][r];
      }
      const float gap = hx::Length(d);
      if (gap > fit.worst) {
        fit.worst = gap;
        fit.worst_bone = b;
      }
    }
    if (!have || fit.worst < out->worst) {
      *out = fit;
      have = true;
    }
  }
  return true;
}

inline void FitPoint(const Fit& fit, const float p[3], float out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = fit.m[r * 3 + 0] * p[0] + fit.m[r * 3 + 1] * p[1] +
             fit.m[r * 3 + 2] * p[2] + fit.t[r];
  }
}

// A step between two places of a point (the delta of a shape key): the map
// without its offset.
inline void FitStep(const Fit& fit, const float d[3], float out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = fit.m[r * 3 + 0] * d[0] + fit.m[r * 3 + 1] * d[1] +
             fit.m[r * 3 + 2] * d[2];
  }
}

// A direction of length 1 (a normal): the map without its offset and its size.
inline void FitNormal(const Fit& fit, const float n[3], float out[3]) {
  FitStep(fit, n, out);
  hx::Normalize(out);
}

// The largest distance of a joint from its place that the mod accepts, as a
// share of the length of the mesh: 2 %, about 4 mm on a hand of 19 cm.
const float kFitTolerance = 0.02f;

// The number of the bone in a bone name of the export: `b07_middle0` is bone
// 7. -1 when the name does not start with b and two digits, or the number is
// not a bone. A 3D editor can put a prefix before the name, so the caller
// gives the part after the last `:` or `|`.
inline int BoneOfName(const char* name) {
  if (name == nullptr || name[0] != 'b') return -1;
  if (name[1] < '0' || name[1] > '9' || name[2] < '0' || name[2] > '9') {
    return -1;
  }
  if (name[3] != '_' && name[3] != 0) return -1;
  const int bone = (name[1] - '0') * 10 + (name[2] - '0');
  return bone < kBones ? bone : -1;
}

}  // namespace hand_model
