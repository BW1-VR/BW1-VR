// The math of the exact Hand, as pure functions with no game state.
//
// hand_exact.cpp and controllers.cpp call these with the numbers they read
// from the game, and test/controller_math calls them with numbers it makes up.
// Nothing here reads a global or touches the game. See docs/hand.md, "The
// exact Hand".
//
// TWO CONVENTIONS. A 3x3 rotation of controller_math.h is m[r * 3 + c] and
// turns a column: out = m * in. The game's matrix (an LHMatrix) is 4 rows of
// 3 floats, and a point is a row: world = p.x * row0 + p.y * row1 +
// p.z * row2 + row3 [V, fn_0046D9D0 and fn_007FAFF0].

#pragma once

#include <math.h>

#include "overlay_math.h"

namespace hand_exact_math {

// THE MESH, measured in Data\CreatureMesh\Hand_Boned_Base2.l3d (the evil and
// good meshes have the same 22 bones). The units are mesh units. The origin is
// the tip of the index finger. The stored mesh is a left hand:
//
//   +y  along the fingers, from the wrist to the finger tips
//   +z  out of the palm
//   +x  toward the thumb
//
// The game mirrors x once at load (CHand::LoadBinary calls ToggleLeftRight at
// 0x0046D56F, so CHand+0x484C is 1), and the Hand it draws is a right hand.
// Bone 0 is the wrist joint. The end of the middle finger is the vertex of
// bone 10 with the largest y.
const float kMeshWrist[3] = {-229.27f, -555.29f, -4.86f};
const float kMeshMiddleTip[3] = {-110.34f, 56.79f, 1.45f};
// The wrist joint to the end of the middle finger. The real hand's length
// (hand_real_cm) is the same distance on a real hand.
const float kMeshLength = 623.6f;
// CHand+0x8C, as LH3DAnim::SetTransform gives it in Morphable::LoadBase
// (0x00618602): the span in y of the bone joints, with 0 in it. For the stored
// bones that is 555.29, the wrist joint to the index tip along y. The game's
// size k = 3.2 / (+0x8C) * (+0x4834) * (+0x90) uses it.
const float kMeshBoneSpan = 555.29f;
// The bones of the log: the wrist, the index finger's last joint and the
// middle finger's last joint.
const int kBoneWrist = 0;
const int kBoneIndexLast = 19;
const int kBoneMiddleLast = 10;

// THE REST TURN Q of the mesh, in the stored mesh frame, as degrees. The
// stored line from the wrist to the middle finger tip leans 11.0 degrees from
// +y toward the thumb and 0.58 degrees toward the palm. A yaw of 11 and a
// pitch of -0.58 put it on +y.
//
//   yaw    about +z (the palm normal). Plus turns the fingers toward the
//          little finger.
//   pitch  about +x (the thumb side). Plus turns the fingers toward the palm.
//   roll   about +y (the fingers). Plus turns the palm toward the thumb.
//
// Q = Ry(roll) * Rx(pitch) * Rz(yaw), so the yaw comes first, and the pitch
// and the roll then turn the line that the yaw put on +y.
const float kRestYawDeg = 11.0f;
const float kRestPitchDeg = -0.58f;
const float kRestRollDeg = 0.0f;

inline float Dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline void Cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

inline float Length(const float v[3]) { return sqrtf(Dot(v, v)); }

inline bool Normalize(float v[3]) {
  const float length = Length(v);
  if (!(length > 1e-12f)) return false;
  for (int i = 0; i < 3; ++i) v[i] /= length;
  return true;
}

// The angle between two directions, in degrees.
inline float AngleDeg(const float a[3], const float b[3]) {
  const float la = Length(a), lb = Length(b);
  if (!(la > 1e-12f) || !(lb > 1e-12f)) return 180.0f;
  float c = Dot(a, b) / (la * lb);
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 57.29578f;
}

// out = a * b, for 3x3 matrices m[r * 3 + c].
inline void Multiply3(const float a[9], const float b[9], float out[9]) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      float sum = 0.0f;
      for (int k = 0; k < 3; ++k) sum += a[r * 3 + k] * b[k * 3 + c];
      out[r * 3 + c] = sum;
    }
  }
}

inline void RestTurn(float yaw_deg, float pitch_deg, float roll_deg,
                     float q[9]) {
  const float k = 3.14159265f / 180.0f;
  const float cy = cosf(yaw_deg * k), sy = sinf(yaw_deg * k);
  const float cp = cosf(pitch_deg * k), sp = sinf(pitch_deg * k);
  const float cr = cosf(roll_deg * k), sr = sinf(roll_deg * k);
  const float rz[9] = {cy, -sy, 0.0f, sy, cy, 0.0f, 0.0f, 0.0f, 1.0f};
  const float rx[9] = {1.0f, 0.0f, 0.0f, 0.0f, cp, -sp, 0.0f, sp, cp};
  const float ry[9] = {cr, 0.0f, sr, 0.0f, 1.0f, 0.0f, -sr, 0.0f, cr};
  float yx[9];
  Multiply3(ry, rx, yx);
  Multiply3(yx, rz, q);
}

// THE HAND FRAME of a controller (overlay_math.h, THE HAND FRAME): f along the
// open fingers (-y of the grip) and b out of the back of the hand (+x of the
// grip). The left hand is the mirror image in x, so its b is -x of the grip.
// rotation is the grip's axes in the world, L * M_grip.
inline void HandAxes(const float rotation[9], bool left, float f[3],
                     float b[3]) {
  const float side = left ? -1.0f : 1.0f;
  for (int r = 0; r < 3; ++r) {
    f[r] = -rotation[r * 3 + 1];
    b[r] = side * rotation[r * 3 + 0];
  }
}

// THE POINT OF THE HAND: the tip of the real index finger at rest, as the
// overlay's test hand puts it (overlay_math.h, FingerTip at kRestCurl). The
// trigger does not move it, so a press does not move the Hand. length is the
// hand's length in world units.
inline void TipOffset(const float rotation[9], bool left, float length,
                      float out[3]) {
  float tip[3];
  overlay_math::FingerTip(1, overlay_math::kRestCurl, tip);
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  overlay_math::PlacePoint(zero, rotation, length, left, tip, out);
}

// THE ROOT MATRIX of the exact Hand, as the game composes the bones with it
// (fn_00839F10). The game's pose builder gives rows (s k A, k B, k C) with
// B = A x u and C = -u [V 0x0046E193 to 0x0046E2C7], and s = -1 while
// CHand+0x484C is set [V fn_0046D080]. Here A = b x f, B = f and C = -b, so
// the mesh's fingers go along f and its palm faces away from b, and the game's
// relations stay. With the rest turn Q:
//
//   F = [b x f, f, -b] (columns), M = diag(s, 1, 1)
//   row j = k * F * M * Q * e_j,   row 3 = tip
//
// So the mesh's own frame is turned by Q, then mirrored as the game mirrors
// it. k is mesh units to world units. False when f and b are not usable.
inline bool ExactRoot(const float f_in[3], const float b_in[3],
                      const float q[9], float k, bool mirrored,
                      const float tip[3], float out[12]) {
  float f[3] = {f_in[0], f_in[1], f_in[2]};
  float b[3] = {b_in[0], b_in[1], b_in[2]};
  if (!Normalize(f)) return false;
  // b square to f, so the frame is orthonormal for any input.
  const float along = Dot(b, f);
  for (int i = 0; i < 3; ++i) b[i] -= along * f[i];
  if (!Normalize(b)) return false;
  float a[3];
  Cross(b, f, a);
  const float s = mirrored ? -1.0f : 1.0f;
  for (int j = 0; j < 3; ++j) {
    // M * Q * e_j: column j of Q with its x times s.
    const float mx = s * q[0 * 3 + j], my = q[1 * 3 + j], mz = q[2 * 3 + j];
    for (int i = 0; i < 3; ++i) {
      out[j * 3 + i] = k * (a[i] * mx + f[i] * my - b[i] * mz);
    }
  }
  for (int i = 0; i < 3; ++i) out[9 + i] = tip[i];
  return true;
}

// THE GAME'S OWN ROOT, for the checks: the pose builder from a yaw (+0x84) and
// an up vector, with fn_0046D080's scale and mirror [V].
inline void GameRoot(float yaw, const float up[3], float k, bool mirrored,
                     const float position[3], float out[12]) {
  const float y = yaw + 3.14159265f;
  const float d[3] = {sinf(y), 0.0f, -cosf(y)};
  float a[3];
  Cross(d, up, a);
  Normalize(a);
  float b[3];
  Cross(a, up, b);
  const float s = mirrored ? -1.0f : 1.0f;
  for (int i = 0; i < 3; ++i) {
    out[0 + i] = s * k * a[i];
    out[3 + i] = k * b[i];
    out[6 + i] = -k * up[i];
    out[9 + i] = position[i];
  }
}

// The determinant of the 3x3 rows of a game matrix.
inline float Det(const float m[12]) {
  return m[0] * (m[4] * m[8] - m[5] * m[7]) -
         m[1] * (m[3] * m[8] - m[5] * m[6]) +
         m[2] * (m[3] * m[7] - m[4] * m[6]);
}

// A model point in the world: p * rows + translation.
inline void ModelToWorld(const float m[12], const float p[3], float out[3]) {
  for (int i = 0; i < 3; ++i) {
    out[i] = p[0] * m[0 + i] + p[1] * m[3 + i] + p[2] * m[6 + i] + m[9 + i];
  }
}

// The inverse of the 3x3 rows of a game matrix. False when it has none.
inline bool Inverse3(const float m[12], float out[9]) {
  const float det = Det(m);
  if (!(fabsf(det) > 1e-20f)) return false;
  const float r = 1.0f / det;
  out[0] = (m[4] * m[8] - m[5] * m[7]) * r;
  out[1] = (m[2] * m[7] - m[1] * m[8]) * r;
  out[2] = (m[1] * m[5] - m[2] * m[4]) * r;
  out[3] = (m[5] * m[6] - m[3] * m[8]) * r;
  out[4] = (m[0] * m[8] - m[2] * m[6]) * r;
  out[5] = (m[2] * m[3] - m[0] * m[5]) * r;
  out[6] = (m[3] * m[7] - m[4] * m[6]) * r;
  out[7] = (m[1] * m[6] - m[0] * m[7]) * r;
  out[8] = (m[0] * m[4] - m[1] * m[3]) * r;
  return true;
}

// A world point in the model frame of m. False when m has no inverse.
inline bool WorldToModel(const float m[12], const float w[3], float out[3]) {
  float inv[9];
  if (!Inverse3(m, inv)) return false;
  const float d[3] = {w[0] - m[9], w[1] - m[10], w[2] - m[11]};
  // d is a row: out = d * inv.
  for (int i = 0; i < 3; ++i) {
    out[i] = d[0] * inv[0 + i] + d[1] * inv[3 + i] + d[2] * inv[6 + i];
  }
  return true;
}

// THE BLEND OF A STATE CHANGE (CHand::PrepareForDrawing, 0x0046C932 to
// 0x0046CA28) mixes each bone's world matrix of the old pose with the new one
// for 0.13 s. The old pose was composed with the root of its frame, so a Hand
// that moved glides from there. Rebase moves a bone matrix of the old pose to
// the new root, with no change in the model frame:
//
//   m' = m * inverse(from) * to
//
// Then the blend mixes only the bones (the fingers), and the Hand does not
// lag. False when from has no inverse, and then out is not written.
inline bool Rebase(const float m[12], const float from[12], const float to[12],
                   float out[12]) {
  float inv[9];
  if (!Inverse3(from, inv)) return false;
  // to3 = inv * to's rows, so a row r goes to r * inv * to.
  float to3[9];
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      to3[r * 3 + c] = inv[r * 3 + 0] * to[0 + c] + inv[r * 3 + 1] * to[3 + c] +
                       inv[r * 3 + 2] * to[6 + c];
    }
  }
  float result[12];
  for (int row = 0; row < 3; ++row) {
    for (int c = 0; c < 3; ++c) {
      result[row * 3 + c] = m[row * 3 + 0] * to3[0 + c] +
                            m[row * 3 + 1] * to3[3 + c] +
                            m[row * 3 + 2] * to3[6 + c];
    }
  }
  const float d[3] = {m[9] - from[9], m[10] - from[10], m[11] - from[11]};
  for (int c = 0; c < 3; ++c) {
    result[9 + c] = d[0] * to3[0 + c] + d[1] * to3[3 + c] + d[2] * to3[6 + c] +
                    to[9 + c];
  }
  for (int i = 0; i < 12; ++i) out[i] = result[i];
  return true;
}

// --- the wrist lock ----------------------------------------------------------

// THE WRIST BONE of the file (bone 0 of Hand_Boned_Base2.l3d, the same in the
// good and the evil file): 4 rows of 3, in the frame of the root. An animation
// gives each bone a matrix in the frame of its parent, and the compose makes
// bone 0 * root the wrist in the world.
const float kBindWrist[12] = {0.954906f,    -0.296907f,   0.0f,
                              0.0f,         0.0f,         1.0f,
                              0.296907f,    0.954906f,    0.0f,
                              -229.266907f, -555.294373f, -4.861393f};

// out = a * b for two game matrices: a point goes through a, then through b.
inline void MultiplyGame(const float a[12], const float b[12], float out[12]) {
  float result[12];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 3; ++c) {
      result[r * 3 + c] = a[r * 3 + 0] * b[0 + c] + a[r * 3 + 1] * b[3 + c] +
                          a[r * 3 + 2] * b[6 + c] + (r == 3 ? b[9 + c] : 0.0f);
    }
  }
  for (int i = 0; i < 12; ++i) out[i] = result[i];
}

// The inverse of a game matrix. False when it has none.
inline bool InverseGame(const float m[12], float out[12]) {
  float inv[9];
  if (!Inverse3(m, inv)) return false;
  for (int i = 0; i < 9; ++i) out[i] = inv[i];
  for (int c = 0; c < 3; ++c) {
    out[9 + c] = -(m[9] * inv[0 + c] + m[10] * inv[3 + c] + m[11] * inv[6 + c]);
  }
  return true;
}

// THE WRIST LOCK. Some animations of the Hand move the wrist bone itself: the
// Holding animation turns the whole hand 79 degrees about its fingers and
// moves the wrist 14 cm (measured, docs/hand.md). The root alone then does not
// put the Hand on the real hand. This gives the compose a root that puts the
// wrist bone where the file's wrist bone is under the exact root:
//
//   root' = inverse(wrist of the animation) * (wrist of the file) * exact
//
// so wrist * root' = (wrist of the file) * exact in each animation, and only
// the fingers move. False when the animation's wrist has no inverse.
inline bool WristLockRoot(const float wrist[12], const float exact[12],
                          float out[12]) {
  float inverse[12];
  if (!InverseGame(wrist, inverse)) return false;
  float bind[12];
  MultiplyGame(kBindWrist, exact, bind);
  MultiplyGame(inverse, bind, out);
  return true;
}

// How far an animation's wrist is from the file's: the angle between the two
// turns in degrees, and the distance in mesh units.
inline void WristOff(const float wrist[12], float* degrees, float* units) {
  float trace = 0.0f;
  for (int r = 0; r < 3; ++r) {
    float a[3] = {wrist[r * 3], wrist[r * 3 + 1], wrist[r * 3 + 2]};
    Normalize(a);
    trace += a[0] * kBindWrist[r * 3] + a[1] * kBindWrist[r * 3 + 1] +
             a[2] * kBindWrist[r * 3 + 2];
  }
  float c = (trace - 1.0f) * 0.5f;
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  *degrees = acosf(c) * 57.29578f;
  const float d[3] = {wrist[9] - kBindWrist[9], wrist[10] - kBindWrist[10],
                      wrist[11] - kBindWrist[11]};
  *units = Length(d);
}

// --- the held object in the palm ---------------------------------------------

// THE BODY OF A HELD OBJECT. The game hangs the object from the Hand: the
// object's origin is the Hand's point plus the hang offset (0x00D13F20), and
// the Hand's point is on the top of the object. So the body is taken as a
// ball between the two: its middle is half the hang offset before the origin,
// and its radius is the larger of half the hang and the object's 2D radius.
inline float BodyRadius(const float hang[3], float radius_2d) {
  const float half = 0.5f * Length(hang);
  return half > radius_2d ? half : radius_2d;
}

// D = now * transpose(then): the turn of the hand in the world since the
// object came into it. Both are the grip's axes in the world, m[r * 3 + c].
inline void TurnSince(const float now[9], const float then[9], float d[9]) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      d[r * 3 + c] = now[r * 3 + 0] * then[c * 3 + 0] +
                     now[r * 3 + 1] * then[c * 3 + 1] +
                     now[r * 3 + 2] * then[c * 3 + 2];
    }
  }
}

inline void Turn(const float d[9], const float v[3], float out[3]) {
  const float x = v[0], y = v[1], z = v[2];
  for (int r = 0; r < 3; ++r) {
    out[r] = d[r * 3 + 0] * x + d[r * 3 + 1] * y + d[r * 3 + 2] * z;
  }
}

// THE PLACE OF THE HELD OBJECT. The ball of the body touches the palm of the
// mod's hand (overlay_math.h, HoldCenter), and the object turns with the hand
// from the pose it had when it came into the hand.
//
//   middle = grip + R * (HoldCenter(radius / length) * length)
//   origin = middle + D * (hang / 2)
//   row j of the object's matrix = D * (row j when it came into the hand)
//
// grip is the grip point that goes with the Hand's point (the point less the
// tip offset), so a Hand that the land test lifted lifts the object too.
inline void HoldOriginAt(const float grip[3], const float rotation[9],
                         float length, bool left, const float local[3],
                         const float hang[3], const float d[9], float out[3]) {
  float middle[3], half[3];
  overlay_math::PlacePoint(grip, rotation, length, left, local, middle);
  const float h[3] = {0.5f * hang[0], 0.5f * hang[1], 0.5f * hang[2]};
  Turn(d, h, half);
  for (int i = 0; i < 3; ++i) out[i] = middle[i] + half[i];
}

// The same, with the middle of the ball at the palm (the fist) or, with
// `pinch` from 0 to 1, moved toward a point of the hand frame between the tips
// of the thumb and of the index finger (pinch_math.h).
inline void HoldOrigin(const float grip[3], const float rotation[9],
                       float length, bool left, float radius,
                       const float hang[3], const float d[9], float out[3],
                       float pinch = 0.0f, const float point[3] = nullptr) {
  float local[3];
  overlay_math::HoldCenter(length > 0.0f ? radius / length : 0.0f, local);
  if (pinch > 0.0f && point != nullptr) {
    for (int i = 0; i < 3; ++i) local[i] += pinch * (point[i] - local[i]);
  }
  HoldOriginAt(grip, rotation, length, left, local, hang, d, out);
}

// THE SETTLE. On the first frame in the hand the object is where the game put
// it, and it moves to its place in the palm in kSettleSeconds. The offset is
// kept in the frame of the hand, so it turns with the hand. This is the share
// of the offset that is left at the time t.
const float kSettleSeconds = 0.12f;
inline float SettleShare(float t) {
  if (!(t > 0.0f)) return 1.0f;
  if (t >= kSettleSeconds) return 0.0f;
  const float u = t / kSettleSeconds;
  return 1.0f - u * u * (3.0f - 2.0f * u);
}

}  // namespace hand_exact_math
