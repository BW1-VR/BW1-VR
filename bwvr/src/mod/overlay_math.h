// The math of the overlay renderer, as pure functions with no game state.
//
// overlay.cpp calls these with the numbers it reads from the game, and
// test/controller_math calls them with numbers it makes up. Nothing here reads
// a global or touches the game. See docs/hand.md, "Overlay renderer".
//
// Every 3x3 matrix is m[r * 3 + c], and a vector is a column: out = m * in,
// as in controller_math.h. The game's world to camera matrix is 4 rows of 3,
// with the translation in the last row (docs/software-tnl.md).

#pragma once

#include <math.h>

namespace overlay_math {

// One triangle, with a normal at each corner.
struct Triangle {
  float p[3][3];
  float n[3][3];
};

inline float Clamp01(float v) {
  if (!(v > 0.0f)) return 0.0f;
  return v > 1.0f ? 1.0f : v;
}

inline void Normalize(float v[3]) {
  const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (!(length > 1e-12f)) return;
  for (int i = 0; i < 3; ++i) v[i] /= length;
}

inline void Cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

// --- the test hand -----------------------------------------------------------

// THE HAND FRAME is the frame of the grip pose, after head_pose.cpp turns the
// runtime's axes into the game's left handed frame (a flip of z). For the
// right hand:
//
//   +x  out of the back of the hand, so the palm faces -x
//   -y  along the open fingers, from the wrist to the finger tips
//   +z  across the knuckles, from the little finger to the index finger
//
// The origin is the grip point, the middle of the controller's handle. The
// left hand is the mirror image in x. Every length is a share of the hand's
// length: the wrist to the tip of the middle finger is 1. The overlay scales
// the mesh by the real hand's length in world units.
//
// A FINGER is a chain of three segments from its knuckle. A bend turns a
// segment about +z, from -y toward -x, so the finger closes over the palm
// side and around the handle. After a sum of bends A a segment points along
// (-sin A, -cos A, 0). The curl, from 0 (straight) to 1 (a fist), scales the
// bends of kFullBend at each joint.
//
// THE THUMB has a direction for each segment. Its curl mixes two sets of
// directions: at 0 the thumb rests on the top of the controller, and at 1 it
// lies across the front of the closed fingers, as in a fist.

const int kFingers = 5;  // 0 the thumb, 1 the index ... 4 the little finger
const int kJoints = 3;   // segments in each finger
const int kSides = 6;    // sides of each segment

const float kKnuckleY = -0.03f;    // the middle finger's knuckle
const float kPalmX = 0.13f;        // the middle of the palm's thickness
const float kPalmLength = 0.52f;   // the wrist to the middle knuckle
const float kPalmHalfThick = 0.065f;
const float kPalmHalfWide = 0.215f;
const float kPalmPast = 0.01f;     // the palm goes this far past the knuckles
const float kTaper = 0.08f;        // the radius each joint loses, of the base
const float kTipCap = 0.7f;        // the round end of a finger, of its radius
const float kOverlap = 0.5f;       // a segment starts this far back, of radius
// 75, 95 and 55 degrees.
const float kFullBend[kJoints] = {1.3090f, 1.6581f, 0.9599f};

struct FingerShape {
  float base[3];                // the knuckle, or the root of the thumb
  float length[kJoints];        // of each segment
  float radius;                 // at the base
  float direction[kJoints][3];  // the thumb only: each segment's direction
};

// The middle finger's segments sum to 1 - kPalmLength, so the wrist to its
// tip is 1.
const FingerShape kShape[kFingers] = {
    // The thumb: from the base of the palm on the index side, across the
    // palm side and up, to rest on the top of the controller.
    {{0.10f, 0.36f, 0.16f},
     {0.19f, 0.15f, 0.12f},
     0.058f,
     {{-0.45f, -0.65f, 0.55f},
      {-0.40f, -0.85f, 0.30f},
      {-0.30f, -0.95f, 0.05f}}},
    {{kPalmX, kKnuckleY + 0.010f, 0.155f}, {0.200f, 0.135f, 0.105f}, 0.046f,
     {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
    {{kPalmX, kKnuckleY, 0.050f}, {0.220f, 0.150f, 0.110f}, 0.047f,
     {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
    {{kPalmX, kKnuckleY + 0.015f, -0.055f}, {0.205f, 0.140f, 0.105f}, 0.044f,
     {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
    {{kPalmX, kKnuckleY + 0.045f, -0.155f}, {0.160f, 0.110f, 0.090f}, 0.038f,
     {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
};

// The palm has 20 triangles. Each finger has two inner segments of 20 and an
// end segment of 22.
const int kSegmentTriangles = 4 * kSides - 4;
const int kTipTriangles = 5 * kSides - 8;
const int kHandTriangles =
    kSegmentTriangles +
    kFingers * ((kJoints - 1) * kSegmentTriangles + kTipTriangles);

// The thumb at a curl of 1: across the front of the closed fingers.
const float kThumbClosed[kJoints][3] = {{-0.62f, -0.72f, 0.05f},
                                        {-0.66f, -0.68f, -0.32f},
                                        {-0.52f, -0.60f, -0.61f}};

// The curl of a finger from its controller value, 0 to 1. A hand at rest on
// a controller is a little bent, so the curl starts at kRestCurl.
const float kRestCurl = 0.12f;
inline float CurlOf(float value) {
  return kRestCurl + (1.0f - kRestCurl) * Clamp01(value);
}

// The joints of a finger in the hand frame: the base, the two middle joints
// and the tip.
inline void FingerJoints(int finger, float curl, float joints[kJoints + 1][3]) {
  const FingerShape& f = kShape[finger];
  for (int k = 0; k < 3; ++k) joints[0][k] = f.base[k];
  const float c = Clamp01(curl);
  float bend = 0.0f;
  for (int j = 0; j < kJoints; ++j) {
    float d[3];
    if (finger == 0) {
      float open[3], closed[3];
      for (int k = 0; k < 3; ++k) {
        open[k] = f.direction[j][k];
        closed[k] = kThumbClosed[j][k];
      }
      Normalize(open);
      Normalize(closed);
      for (int k = 0; k < 3; ++k) d[k] = open[k] + c * (closed[k] - open[k]);
      Normalize(d);
    } else {
      bend += c * kFullBend[j];
      d[0] = -sinf(bend);
      d[1] = -cosf(bend);
      d[2] = 0.0f;
    }
    for (int k = 0; k < 3; ++k) {
      joints[j + 1][k] = joints[j][k] + f.length[j] * d[k];
    }
  }
}

// The tip of a finger in the hand frame. It is a corner of the mesh.
inline void FingerTip(int finger, float curl, float out[3]) {
  float joints[kJoints + 1][3];
  FingerJoints(finger, curl, joints);
  for (int k = 0; k < 3; ++k) out[k] = joints[kJoints][k];
}

struct MeshOut {
  Triangle* triangles;
  int count;
  int most;
};

inline void Add(MeshOut* out, const float* a, const float* b, const float* c,
                const float* na, const float* nb, const float* nc) {
  if (out->count >= out->most) return;
  Triangle& t = out->triangles[out->count++];
  const float* p[3] = {a, b, c};
  const float* n[3] = {na, nb, nc};
  for (int v = 0; v < 3; ++v) {
    for (int k = 0; k < 3; ++k) {
      t.p[v][k] = p[v][k];
      t.n[v][k] = n[v][k];
    }
  }
}

// kSides points on an ellipse around center, in the plane of u and s, with
// the half size a along u and b along s, and the outward normal at each.
inline void Ring(const float center[3], const float u[3], const float s[3],
                 float a, float b, float points[kSides][3],
                 float normals[kSides][3]) {
  for (int i = 0; i < kSides; ++i) {
    const float angle = 6.2831853f * static_cast<float>(i) / kSides;
    const float cu = cosf(angle), su = sinf(angle);
    for (int k = 0; k < 3; ++k) {
      points[i][k] = center[k] + a * cu * u[k] + b * su * s[k];
      normals[i][k] = cu / a * u[k] + su / b * s[k];
    }
    Normalize(normals[i]);
  }
}

// A segment from a to b with an elliptical section: half sizes ua and sa at
// a, ub and sb at b. Its side axis s is +z made square to the segment, and u
// is s x d, so u is +x for a segment along -y. A flat cap closes a. A flat cap
// closes b too, or with tip a round end that comes to a point exactly at b.
inline void Segment(MeshOut* out, const float a[3], const float b[3],
                    float ua, float sa, float ub, float sb, bool tip) {
  float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  Normalize(d);
  float ref[3] = {0.0f, 0.0f, 1.0f};
  if (fabsf(d[2]) > 0.9f) {
    ref[0] = 1.0f;
    ref[2] = 0.0f;
  }
  const float along = ref[0] * d[0] + ref[1] * d[1] + ref[2] * d[2];
  float s[3] = {ref[0] - along * d[0], ref[1] - along * d[1],
                ref[2] - along * d[2]};
  Normalize(s);
  float u[3];
  Cross(s, d, u);
  float end[3];
  for (int k = 0; k < 3; ++k) end[k] = tip ? b[k] - kTipCap * ub * d[k] : b[k];
  float pa[kSides][3], na[kSides][3], pb[kSides][3], nb[kSides][3];
  Ring(a, u, s, ua, sa, pa, na);
  Ring(end, u, s, ub, sb, pb, nb);
  const float back[3] = {-d[0], -d[1], -d[2]};
  for (int i = 0; i < kSides; ++i) {
    const int j = (i + 1) % kSides;
    Add(out, pa[i], pb[i], pb[j], na[i], nb[i], nb[j]);
    Add(out, pa[i], pb[j], pa[j], na[i], nb[j], na[j]);
  }
  for (int i = 1; i + 1 < kSides; ++i) {
    Add(out, pa[0], pa[i + 1], pa[i], back, back, back);
  }
  if (tip) {
    float slope[kSides][3];
    for (int i = 0; i < kSides; ++i) {
      for (int k = 0; k < 3; ++k) slope[i][k] = nb[i][k] + d[k];
      Normalize(slope[i]);
    }
    for (int i = 0; i < kSides; ++i) {
      const int j = (i + 1) % kSides;
      Add(out, pb[i], b, pb[j], slope[i], d, slope[j]);
    }
  } else {
    for (int i = 1; i + 1 < kSides; ++i) {
      Add(out, pb[0], pb[i], pb[i + 1], d, d, d);
    }
  }
}

// The right hand in the hand frame, kHandTriangles triangles, with a curl for
// each finger from 0 (straight, and the thumb at rest) to 1 (a fist). Returns
// the count written.
inline int BuildHandPose(const float curls[kFingers], Triangle* out) {
  MeshOut mesh = {out, 0, kHandTriangles};
  const float wrist[3] = {kPalmX, kKnuckleY + kPalmLength, 0.0f};
  const float knuckles[3] = {kPalmX, kKnuckleY - kPalmPast, 0.0f};
  Segment(&mesh, wrist, knuckles, kPalmHalfThick, kPalmHalfWide,
          kPalmHalfThick, kPalmHalfWide, false);
  for (int f = 0; f < kFingers; ++f) {
    const float curl = curls[f];
    float joints[kJoints + 1][3];
    FingerJoints(f, curl, joints);
    const float r = kShape[f].radius;
    for (int j = 0; j < kJoints; ++j) {
      const float ra = r * (1.0f - kTaper * static_cast<float>(j));
      const float rb = r * (1.0f - kTaper * static_cast<float>(j + 1));
      float start[3];
      for (int k = 0; k < 3; ++k) start[k] = joints[j][k];
      if (j > 0) {
        // Back into the segment before, so a bent joint shows no gap.
        float d[3] = {joints[j + 1][0] - joints[j][0],
                      joints[j + 1][1] - joints[j][1],
                      joints[j + 1][2] - joints[j][2]};
        Normalize(d);
        for (int k = 0; k < 3; ++k) start[k] -= kOverlap * ra * d[k];
      }
      Segment(&mesh, start, joints[j + 1], ra, ra, rb, rb, j == kJoints - 1);
    }
  }
  return mesh.count;
}

// The same hand from two curls: index_curl bends the index finger and
// finger_curl the middle, ring and little fingers. The thumb is at rest.
inline int BuildHand(float index_curl, float finger_curl, Triangle* out) {
  const float curls[kFingers] = {0.0f, index_curl, finger_curl, finger_curl,
                                 finger_curl};
  return BuildHandPose(curls, out);
}

// --- the poses ---------------------------------------------------------------

// A POSE gives each finger a range of curl. The player's own finger moves the
// curl in that range: the trigger for the index finger, the grip for the
// middle, ring and little fingers, and the larger of the two for the thumb.
// So the fingers follow the player in each pose, and a pose only says where
// they can go.
//
//   curl = lo + (hi - lo) * value
//
//   relaxed  the range of the first test hand: the rest curl to a fist
//   hold     closed on the held object (HoldRange)
//   point    the index finger straight, the other fingers and the thumb closed
//   flat     an open hand, for a stroke
//   fist     all the fingers closed, for a punch or a grip on nothing
//
// The overlay mixes the poses with a weight for each (StepWeights), so a
// change of pose takes about 0.1 s and the value of a finger has no lag.
enum Pose {
  kPoseRelaxed = 0,
  kPoseHold,
  kPosePoint,
  kPoseFlat,
  kPoseFist,
  kPoseCount
};

struct PoseRange {
  float lo[kFingers];
  float hi[kFingers];
};

// The row of kPoseHold is a loose cup. HoldRange gives the real one.
const PoseRange kPoseRanges[kPoseCount] = {
    {{0.0f, kRestCurl, kRestCurl, kRestCurl, kRestCurl},
     {0.85f, 1.0f, 1.0f, 1.0f, 1.0f}},
    {{0.30f, 0.30f, 0.12f, 0.12f, 0.12f}, {0.40f, 0.40f, 0.40f, 0.40f, 0.40f}},
    {{0.85f, 0.0f, 0.88f, 0.90f, 0.92f}, {1.0f, 0.18f, 1.0f, 1.0f, 1.0f}},
    {{0.0f, 0.02f, 0.02f, 0.02f, 0.02f}, {0.20f, 0.20f, 0.20f, 0.20f, 0.20f}},
    {{0.90f, 0.82f, 0.90f, 0.90f, 0.90f}, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f}},
};

// The value of each finger from the controller: the thumb takes the larger of
// the trigger and the grip.
inline void FingerValues(float trigger, float squeeze, float values[kFingers]) {
  const float t = Clamp01(trigger), s = Clamp01(squeeze);
  values[0] = t > s ? t : s;
  values[1] = t;
  values[2] = values[3] = values[4] = s;
}

// THE HELD OBJECT is a ball of the given radius (in hand lengths) that touches
// the palm, a little behind the knuckles. This is its middle in the hand frame
// of the right hand.
const float kHoldY = kKnuckleY + 0.17f;
inline void HoldCenter(float radius, float out[3]) {
  out[0] = kPalmX - kPalmHalfThick - (radius > 0.0f ? radius : 0.0f);
  out[1] = kHoldY;
  out[2] = 0.0f;
}

// The largest curl of a finger, in steps of 1/40, at which no joint and no
// middle of a segment is in the ball. 0 when the straight finger is in it
// already, and 1 when the finger closes to a fist with no contact.
inline float ContactCurl(int finger, const float center[3], float radius) {
  const int kSteps = 40;
  const float reach = radius + kShape[finger].radius;
  float free_curl = 0.0f;
  for (int i = 0; i <= kSteps; ++i) {
    const float curl = static_cast<float>(i) / kSteps;
    float joints[kJoints + 1][3];
    FingerJoints(finger, curl, joints);
    for (int j = 1; j <= kJoints; ++j) {
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

// THE HOLD POSE for an object of the given radius, in hand lengths. Each
// finger closes until it touches the ball, so a large object opens the hand.
// A small object counts as kHoldMinRadius, so the hand stays a loose cup and
// the object can be seen. The middle, ring and little fingers go from the
// rest curl to the contact with the grip. The index finger and the thumb stay
// near the contact when the trigger is open.
const float kHoldMinRadius = 0.10f;
const float kHoldLoose = 0.75f;
inline void HoldRange(float radius, PoseRange* out) {
  const float r = radius > kHoldMinRadius ? radius : kHoldMinRadius;
  float center[3];
  HoldCenter(r, center);
  for (int f = 0; f < kFingers; ++f) {
    const float contact = ContactCurl(f, center, r);
    out->hi[f] = contact;
    if (f <= 1) {
      out->lo[f] = kHoldLoose * contact;
    } else {
      out->lo[f] = contact < kRestCurl ? contact : kRestCurl;
    }
  }
}

// The curl of each finger: the mix of the poses by their weights. hold is
// the range of kPoseHold for the object in the hand.
inline void PoseCurls(const float weights[kPoseCount], const PoseRange& hold,
                      float trigger, float squeeze, float curls[kFingers]) {
  float values[kFingers];
  FingerValues(trigger, squeeze, values);
  float total = 0.0f;
  for (int p = 0; p < kPoseCount; ++p) total += weights[p];
  for (int f = 0; f < kFingers; ++f) {
    float sum = 0.0f;
    for (int p = 0; p < kPoseCount; ++p) {
      const PoseRange& range = p == kPoseHold ? hold : kPoseRanges[p];
      sum += weights[p] *
             (range.lo[f] + (range.hi[f] - range.lo[f]) * values[f]);
    }
    curls[f] = Clamp01(total > 1e-6f ? sum / total : kRestCurl);
  }
}

// THE BLEND of a change of pose. The weight of the target pose grows by
// dt / seconds, and the other weights keep their shares of the rest. From one
// pose to the next the mix is a straight line that takes `seconds`.
const float kPoseBlendSeconds = 0.10f;
inline void StepWeights(float weights[kPoseCount], int target, float dt,
                        float seconds) {
  if (target < 0 || target >= kPoseCount) return;
  float rest = 0.0f;
  for (int p = 0; p < kPoseCount; ++p) {
    if (p != target) rest += weights[p];
  }
  const float step = seconds > 0.0f ? Clamp01(dt / seconds) : 1.0f;
  float next = Clamp01(weights[target]) + step;
  if (next > 1.0f || !(rest > 1e-6f)) next = 1.0f;
  const float scale = rest > 1e-6f ? (1.0f - next) / rest : 0.0f;
  for (int p = 0; p < kPoseCount; ++p) {
    weights[p] = p == target ? next : weights[p] * scale;
  }
}

// THE TINT of the skin from the player's alignment, -1 (evil) to 1 (good):
// a pale, clean hand for good, and a dark, red one for evil. The game mixes
// the texture of its Hand between hand_boned_base2 and good2 or evil2 with
// the same value. The mean colors of those textures are about 0.43 0.26 0.08
// (base), 0.59 0.53 0.33 (good) and 0.23 0.04 0.04 (evil).
const float kSkinNeutral[3] = {0.90f, 0.70f, 0.57f};
const float kSkinGood[3] = {0.98f, 0.91f, 0.80f};
const float kSkinEvil[3] = {0.42f, 0.11f, 0.09f};
inline void SkinTint(float alignment, float out[3]) {
  float a = alignment;
  if (!(a > -1.0f)) a = -1.0f;
  if (a > 1.0f) a = 1.0f;
  const float* to = a >= 0.0f ? kSkinGood : kSkinEvil;
  const float share = a >= 0.0f ? a : -a;
  for (int k = 0; k < 3; ++k) {
    out[k] = kSkinNeutral[k] + share * (to[k] - kSkinNeutral[k]);
  }
}

// A point of the hand frame in the world: position + rotation * (p * length).
// The left hand mirrors x first. The rotation is the grip's axes in the world.
inline void PlacePoint(const float position[3], const float rotation[9],
                       float length, bool left, const float p[3],
                       float out[3]) {
  const float q[3] = {(left ? -p[0] : p[0]) * length, p[1] * length,
                      p[2] * length};
  for (int r = 0; r < 3; ++r) {
    out[r] = position[r] + rotation[r * 3 + 0] * q[0] +
             rotation[r * 3 + 1] * q[1] + rotation[r * 3 + 2] * q[2];
  }
}

inline void PlaceNormal(const float rotation[9], bool left, const float n[3],
                        float out[3]) {
  const float q[3] = {left ? -n[0] : n[0], n[1], n[2]};
  for (int r = 0; r < 3; ++r) {
    out[r] = rotation[r * 3 + 0] * q[0] + rotation[r * 3 + 1] * q[1] +
             rotation[r * 3 + 2] * q[2];
  }
}

inline void PlaceTriangles(const Triangle* in, int count,
                           const float position[3], const float rotation[9],
                           float length, bool left, Triangle* out) {
  for (int t = 0; t < count; ++t) {
    for (int v = 0; v < 3; ++v) {
      PlacePoint(position, rotation, length, left, in[t].p[v], out[t].p[v]);
      PlaceNormal(rotation, left, in[t].n[v], out[t].n[v]);
    }
  }
}

// THE LIGHT: a fixed light from straight above (world +y), and some ambient.
//
//   level = ambient + (1 - ambient) * max(0, n.y)
inline float LightLevel(const float n[3], float ambient) {
  const float up = n[1] > 0.0f ? n[1] : 0.0f;
  return ambient + (1.0f - ambient) * up;
}

// --- the teleport arc and ring -----------------------------------------------

// THE TUBE of the teleport arc: a ring of kTubeSides corners round each point
// of a polyline, in the plane that is square to the line there (the mean of
// the two segments at an inner point). Two triangles join each pair of
// rings, so the tube has no inner cap and no end cap. The corners and the
// normals are the same at a joint, so there is no gap. The side axis is the
// line crossed with world up, so the tube keeps one roll along a climb and a
// fall. A tube of n points has (n - 1) * kTubeSides * 2 triangles.
const int kTubeSides = 6;
const int kArcMaxPoints = 49;
const int kTubeMaxTriangles = (kArcMaxPoints - 1) * kTubeSides * 2;
// The ring of the target: a band of kRingPoints quads, and a disc or a cross
// in it.
const int kRingPoints = 24;
const int kRingMaxTriangles = kRingPoints * 2 + kRingPoints;
const int kTeleportMaxTriangles = kTubeMaxTriangles + kRingMaxTriangles;

inline int BuildTube(const float (*points)[3], int count, float radius,
                     Triangle* out, int most) {
  if (count < 2 || !(radius > 0.0f)) return 0;
  MeshOut mesh = {out, 0, most};
  float ring[kArcMaxPoints][kTubeSides][3];
  float normal[kArcMaxPoints][kTubeSides][3];
  if (count > kArcMaxPoints) count = kArcMaxPoints;
  for (int i = 0; i < count; ++i) {
    const float* a = points[i > 0 ? i - 1 : 0];
    const float* b = points[i + 1 < count ? i + 1 : count - 1];
    float t[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    Normalize(t);
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const float across[3] = {1.0f, 0.0f, 0.0f};
    float s[3];
    Cross(t, up, s);
    if (s[0] * s[0] + s[1] * s[1] + s[2] * s[2] < 1e-8f) Cross(t, across, s);
    Normalize(s);
    float u[3];
    Cross(s, t, u);
    for (int k = 0; k < kTubeSides; ++k) {
      const float angle = 6.2831853f * static_cast<float>(k) / kTubeSides;
      const float cu = cosf(angle), su = sinf(angle);
      for (int c = 0; c < 3; ++c) {
        normal[i][k][c] = cu * u[c] + su * s[c];
        ring[i][k][c] = points[i][c] + radius * normal[i][k][c];
      }
    }
  }
  for (int i = 0; i + 1 < count; ++i) {
    for (int k = 0; k < kTubeSides; ++k) {
      const int j = (k + 1) % kTubeSides;
      Add(&mesh, ring[i][k], ring[i + 1][k], ring[i + 1][j], normal[i][k],
          normal[i + 1][k], normal[i + 1][j]);
      Add(&mesh, ring[i][k], ring[i + 1][j], ring[i][j], normal[i][k],
          normal[i + 1][j], normal[i][j]);
    }
  }
  return mesh.count;
}

// THE RING OF THE TARGET, on the land. kRingPoints points follow the land at
// ground(x, z) + lift, from inner to outer from the target. A valid target
// has a disc of a third of the inner radius in the middle. An invalid one has
// a cross of two bars, from corner to corner of the inner circle. The
// normals are straight up.
template <typename Ground>
inline int BuildLandRing(float cx, float cz, float inner, float outer,
                         float lift, bool valid, Ground ground, Triangle* out,
                         int most) {
  if (!(inner > 0.0f) || !(outer > inner)) return 0;
  MeshOut mesh = {out, 0, most};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  float in_p[kRingPoints][3], out_p[kRingPoints][3];
  for (int i = 0; i < kRingPoints; ++i) {
    const float a = 6.2831853f * static_cast<float>(i) / kRingPoints;
    const float sx = sinf(a), sz = cosf(a);
    in_p[i][0] = cx + inner * sx;
    in_p[i][2] = cz + inner * sz;
    in_p[i][1] = ground(in_p[i][0], in_p[i][2]) + lift;
    out_p[i][0] = cx + outer * sx;
    out_p[i][2] = cz + outer * sz;
    out_p[i][1] = ground(out_p[i][0], out_p[i][2]) + lift;
  }
  for (int i = 0; i < kRingPoints; ++i) {
    const int j = (i + 1) % kRingPoints;
    Add(&mesh, in_p[i], out_p[i], out_p[j], up, up, up);
    Add(&mesh, in_p[i], out_p[j], in_p[j], up, up, up);
  }
  const float middle[3] = {cx, ground(cx, cz) + lift, cz};
  if (valid) {
    float rim[kRingPoints][3];
    const float r = inner / 3.0f;
    for (int i = 0; i < kRingPoints; ++i) {
      const float a = 6.2831853f * static_cast<float>(i) / kRingPoints;
      rim[i][0] = cx + r * sinf(a);
      rim[i][2] = cz + r * cosf(a);
      rim[i][1] = ground(rim[i][0], rim[i][2]) + lift;
    }
    for (int i = 0; i < kRingPoints; ++i) {
      const int j = (i + 1) % kRingPoints;
      Add(&mesh, middle, rim[i], rim[j], up, up, up);
    }
  } else {
    const float half_length = inner * 0.9f;
    const float half_width = inner * 0.12f;
    const float root = 0.70710678f;
    for (int bar = 0; bar < 2; ++bar) {
      // The direction along the bar, and the one across it.
      const float ax = root, az = bar == 0 ? root : -root;
      const float bx = -az, bz = ax;
      float c[4][3];
      const float sl[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
      const float sw[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
      for (int k = 0; k < 4; ++k) {
        c[k][0] = cx + sl[k] * half_length * ax + sw[k] * half_width * bx;
        c[k][2] = cz + sl[k] * half_length * az + sw[k] * half_width * bz;
        c[k][1] = ground(c[k][0], c[k][2]) + lift;
      }
      Add(&mesh, c[0], c[1], c[2], up, up, up);
      Add(&mesh, c[0], c[2], c[3], up, up, up);
    }
  }
  return mesh.count;
}

// --- the projection ----------------------------------------------------------

// A corner on the screen as the game writes it, with a color from 0 to 1.
struct ScreenVertex {
  float x, y, z, rhw;
  float color[3];
};

// A world point in camera space, as LH3DTech::ProjectPoint does it.
inline void ToCamera(const float m[12], const float p[3], float out[3]) {
  out[0] = p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9];
  out[1] = p[0] * m[1] + p[1] * m[4] + p[2] * m[7] + m[10];
  out[2] = p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11];
}

// A camera point on the screen, with the depth the game writes:
//
//   x = (1 + cx / cz) * hx      y = (1 - cy / cz) * hy
//   rhw = near / cz             z = 1 - rhw
inline void Project(const float c[3], const float color[3], float near_plane,
                    float hx, float hy, ScreenVertex* out) {
  out->x = (1.0f + c[0] / c[2]) * hx;
  out->y = (1.0f - c[1] / c[2]) * hy;
  out->rhw = near_plane / c[2];
  out->z = 1.0f - out->rhw;
  for (int k = 0; k < 3; ++k) out->color[k] = color[k];
}

// THE NEAR PLANE. The part of a camera space triangle with cz >= near_plane,
// projected, as 0, 1 or 2 triangles (3 or 6 corners in out). A cut point of
// an edge takes the position and the color at the same share of the edge,
// and lies exactly on the near plane, so it gets rhw 1 and z 0. Returns the
// count of triangles.
inline int ClipTriangle(const float c[3][3], const float color[3][3],
                        float near_plane, float hx, float hy,
                        ScreenVertex out[6]) {
  if (!(near_plane > 0.0f)) return 0;
  float pc[4][3], pk[4][3];
  int n = 0;
  for (int i = 0; i < 3; ++i) {
    const int j = (i + 1) % 3;
    const bool i_in = c[i][2] >= near_plane;
    const bool j_in = c[j][2] >= near_plane;
    if (i_in) {
      for (int k = 0; k < 3; ++k) {
        pc[n][k] = c[i][k];
        pk[n][k] = color[i][k];
      }
      ++n;
    }
    if (i_in != j_in) {
      const float t = (near_plane - c[i][2]) / (c[j][2] - c[i][2]);
      for (int k = 0; k < 3; ++k) {
        pc[n][k] = c[i][k] + t * (c[j][k] - c[i][k]);
        pk[n][k] = color[i][k] + t * (color[j][k] - color[i][k]);
      }
      pc[n][2] = near_plane;
      ++n;
    }
  }
  if (n < 3) return 0;
  ScreenVertex v[4];
  for (int i = 0; i < n; ++i) Project(pc[i], pk[i], near_plane, hx, hy, &v[i]);
  out[0] = v[0];
  out[1] = v[1];
  out[2] = v[2];
  if (n == 3) return 1;
  out[3] = v[0];
  out[4] = v[2];
  out[5] = v[3];
  return 2;
}

// True when all three corners are past the same edge of the render, which
// is 0 to 2 * hx across and 0 to 2 * hy down. Such a triangle cannot show.
inline bool OffScreen(const ScreenVertex* v, float hx, float hy) {
  bool left = true, right = true, top = true, bottom = true;
  for (int i = 0; i < 3; ++i) {
    left = left && v[i].x < 0.0f;
    right = right && v[i].x > 2.0f * hx;
    top = top && v[i].y < 0.0f;
    bottom = bottom && v[i].y > 2.0f * hy;
  }
  return left || right || top || bottom;
}

}  // namespace overlay_math
