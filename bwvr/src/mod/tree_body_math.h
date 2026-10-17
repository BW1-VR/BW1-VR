// THE TEST OF A BODY IN THE WAY OF A TREE (docs/grab.md, "The test of a body
// in the way"). Pure math: no Windows and no game.
//
// The release of a tree (Object::InitialisePhysicsFromHand 0x00636F00) makes
// the physics body of the tree, puts it on the land
// (PhysOb::AdjustToGroundLevel 0x007FCB80) and calls
// PhysicsObject::RaiseUntilNotIntersecting 0x00644800. When that call raises
// the tree, the release does not plant it, and the tree falls. The call
// compares the tree with each body of the physics whose sphere meets the
// sphere of the tree. A body is in the way when the depth of the one in the
// other along up is more than 0.001 units.
//
// The functions here make the same bodies and do the same steps on memory of
// the mod, so the card knows the answer before the release. The bodies:
//   - a tree or a dead tree (Object::SetUpPhysObAsATree 0x0063A230),
//   - a villager or an animal (Villager::SetUpPhysOb 0x005EFF40,
//     Animal::SetUpPhysOb 0x005F04E0),
//   - the hull of a mesh (fn_007FBAE0, from PhysOb::BuildFromVertices),
//   - a Creature: one ellipsoid for each bone (Creature::SetUpPhysOb
//     0x00479B90, fn_00868910).
// A frame is 12 floats: the rows x, y and z, then the translation. A point p
// in the frame is at p.x * x + p.y * y + p.z * z + translation.
#pragma once

#include <math.h>
#include <stdint.h>

namespace tree_body_math {

const float kRaiseLimit = 0.001f;    // a deeper body raises the tree (0x008AA3B0)
const double kFacing = -0.0001;      // a face is in the test when n.d is less (0x009A2BC0)
const float kNoFaceHit = -10000.0f;  // the first best t of fn_007FC310
const float kNoEllipsoidHit = 1e20f; // the first best t of fn_008683C0
const float kSmallDeterminant = 1e-10f;  // LHMatrix::SetInverse (0x00C371D4)
const int kTreePoints = 16;
const int kTreeFaces = 24;
const int kLivingPoints = 12;
const int kLivingFaces = 20;

// One ellipsoid of a Creature, as the game keeps it (0x78 bytes): the box in
// its frame, the frame and its inverse. The ellipsoid fills the box.
struct Ellipsoid {
  float box_min[3];
  float box_max[3];
  float frame[12];
  float inverse[12];
};

// A body as RaiseUntilNotIntersecting compares it: the points in the world,
// the faces as three indices of the points, or ellipsoids in place of the
// faces (a Creature). center and radius give the sphere of the first test.
struct Body {
  const float (*points)[3];
  int point_count;
  const int (*faces)[3];
  int face_count;
  const Ellipsoid* ellipsoids;
  int ellipsoid_count;
  float center[3];
  float radius;
};

inline float Dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline void Cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

inline void Sub(const float a[3], const float b[3], float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = a[i] - b[i];
}

// A point of a frame in the world.
inline void Apply(const float f[12], const float p[3], float out[3]) {
  for (int i = 0; i < 3; ++i) {
    out[i] = p[0] * f[i] + p[1] * f[3 + i] + p[2] * f[6 + i] + f[9 + i];
  }
}

// A direction of a frame in the world: the rows only.
inline void Turn(const float f[12], const float d[3], float out[3]) {
  for (int i = 0; i < 3; ++i) {
    out[i] = d[0] * f[i] + d[1] * f[3 + i] + d[2] * f[6 + i];
  }
}

// The inverse of a frame, as LHMatrix::SetInverse 0x007FB290 makes it.
inline void Invert(const float m[12], float r[12]) {
  const float c0 = m[8] * m[4] - m[7] * m[5];
  const float det = c0 * m[0] + (m[5] * m[1] - m[2] * m[4]) * m[6] +
                    (m[2] * m[7] - m[8] * m[1]) * m[3];
  float d = det;
  if (fabsf(det) < kSmallDeterminant) {
    d = det < 0.0f ? -kSmallDeterminant : kSmallDeterminant;
  }
  const float k = 1.0f / d;
  r[0] = c0 * k;
  r[3] = (m[6] * m[5] - m[8] * m[3]) * k;
  r[6] = (m[7] * m[3] - m[6] * m[4]) * k;
  r[1] = (m[2] * m[7] - m[8] * m[1]) * k;
  r[4] = (m[8] * m[0] - m[6] * m[2]) * k;
  r[7] = (m[6] * m[1] - m[0] * m[7]) * k;
  r[2] = (m[5] * m[1] - m[2] * m[4]) * k;
  r[5] = (m[2] * m[3] - m[0] * m[5]) * k;
  r[8] = (m[0] * m[4] - m[3] * m[1]) * k;
  r[9] = -(m[9] * r[0] + m[11] * r[6] + m[10] * r[3]);
  r[10] = -(r[4] * m[10] + m[9] * r[1] + r[7] * m[11]);
  r[11] = -(r[5] * m[10] + r[8] * m[11] + m[9] * r[2]);
}

// --- the shapes, in the frame of the body ----------------------------------

// The faces of a tree: two bands of 8 faces between the three rings, 4 faces
// down to the point at the bottom (12) and 4 up to the point at the top (13).
// The points 14 and 15 are copies of the point 12: the game makes them so.
inline const int (*TreeFaces())[3] {
  static const int faces[kTreeFaces][3] = {
      {0, 1, 5},  {0, 5, 4},   {1, 2, 6},   {1, 6, 5},   {2, 3, 7},
      {2, 7, 6},  {3, 0, 4},   {3, 4, 7},   {4, 5, 9},   {4, 9, 8},
      {5, 6, 10}, {5, 10, 9},  {6, 7, 11},  {6, 11, 10}, {7, 4, 8},
      {7, 8, 11}, {12, 1, 0},  {12, 2, 1},  {12, 3, 2},  {12, 0, 3},
      {13, 8, 9}, {13, 9, 10}, {13, 10, 11}, {13, 11, 8}};
  return faces;
}

// The body of a tree (Object::SetUpPhysObAsATree). height and radius are the
// game's height and 2D radius of the object (slots 0x42C and 0x64), scale its
// scale (slot 0x120). rooted is IsARootedObject (slot 0x7D0): 1 for a tree
// that stands, 0 for a dead tree. The points are about the middle of mass,
// and middle is that middle in the frame of the mesh. False for a scale of 0.
inline bool TreeShape(float height, float radius, bool rooted, float scale,
                      float points[kTreePoints][3], float middle[3],
                      float* body_radius) {
  if (!(scale != 0.0f)) return false;
  const float half = height * (rooted ? 0.6f : 0.5f);
  middle[0] = 0.0f;
  middle[1] = ((rooted ? 0.4f : 0.5f) / scale) * height;
  middle[2] = 0.0f;
  const float ring_y[3] = {half * -0.6f, 0.0f, half * 0.6f};
  const float ring_r[3] = {radius * 0.65f, radius, radius * 0.65f};
  for (int ring = 0; ring < 3; ++ring) {
    const float y = ring_y[ring], r = ring_r[ring];
    float* p = points[ring * 4];
    p[0] = r, p[1] = y, p[2] = 0.0f;
    p = points[ring * 4 + 1];
    p[0] = 0.0f, p[1] = y, p[2] = -r;
    p = points[ring * 4 + 2];
    p[0] = -r, p[1] = y, p[2] = 0.0f;
    p = points[ring * 4 + 3];
    p[0] = 0.0f, p[1] = y, p[2] = r;
  }
  const float foot = radius * 0.1f;
  const int feet[3] = {12, 14, 15};
  for (int i = 0; i < 3; ++i) {
    points[feet[i]][0] = foot;
    points[feet[i]][1] = -half;
    points[feet[i]][2] = 0.0f;
  }
  points[13][0] = 0.0f;
  points[13][1] = half;
  points[13][2] = 0.0f;
  *body_radius = half > radius ? half : radius;
  return true;
}

// The faces of a villager and of an animal: a box of 12 points in three
// levels of 4 (the top, the middle and the bottom).
inline const int (*LivingFaces())[3] {
  static const int faces[kLivingFaces][3] = {
      {1, 2, 0},  {0, 3, 1},  {5, 1, 3},   {7, 5, 3},  {7, 3, 0},
      {4, 7, 0},  {6, 4, 0},  {2, 6, 0},   {6, 2, 1},  {5, 6, 1},
      {9, 5, 7},  {11, 9, 7}, {11, 7, 4},  {8, 11, 4}, {10, 8, 4},
      {6, 10, 4}, {10, 6, 5}, {9, 10, 5},  {11, 10, 9}, {8, 10, 11}};
  return faces;
}

// The body of a villager (animal false) or of an animal (animal true). The
// game turns the box 90 degrees about up. The other values are as for
// TreeShape.
inline bool LivingShape(float height, float radius, bool animal, float scale,
                        float points[kLivingPoints][3], float middle[3],
                        float* body_radius) {
  if (!(scale != 0.0f)) return false;
  const float half = height * 0.5f;
  middle[0] = 0.0f;
  middle[1] = height * (0.5f / scale);
  middle[2] = 0.0f;
  // The half sizes across (x) and along (z), at the top and the bottom, and
  // in the middle.
  const float end_x = radius * (animal ? 0.25f : 0.8f);
  const float mid_x = radius * (animal ? 0.3f : 0.9f);
  const float end_z = radius * (animal ? 0.8f : 0.32f);
  const float mid_z = radius * (animal ? 0.9f : 0.32f);
  const float level_y[3] = {half, 0.0f, -half};
  const float level_x[3] = {end_x, mid_x, end_x};
  const float level_z[3] = {end_z, mid_z, end_z};
  const float sign_x[4] = {1.0f, -1.0f, -1.0f, 1.0f};
  const float sign_z[4] = {1.0f, -1.0f, 1.0f, -1.0f};
  const float c = static_cast<float>(cos(1.5707963705062866));
  const float s = static_cast<float>(sin(1.5707963705062866));
  float most = 0.0f;
  for (int level = 0; level < 3; ++level) {
    for (int k = 0; k < 4; ++k) {
      const float x = sign_x[k] * level_x[level];
      const float y = level_y[level];
      const float z = sign_z[k] * level_z[level];
      float* p = points[level * 4 + k];
      p[0] = x * c - z * s;
      p[1] = y;
      p[2] = x * s + z * c;
      const float length = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
      if (most <= length) most = length;
    }
  }
  *body_radius = most;
  return true;
}

// The hull of a mesh (fn_007FBAE0). raw holds the vertices of the parts of
// the mesh that the game takes, in their order, and lifted holds the same
// vertices with the lift of slot 0x1F4 added to y (the same values when the
// mesh has none). The middle is the mean of raw. The points are (lifted -
// middle) times scale. False with no vertex.
inline bool HullShape(const float (*raw)[3], const float (*lifted)[3],
                      int count, float scale, float (*points)[3],
                      float middle[3], float* body_radius) {
  if (count <= 0) return false;
  float sum[3] = {0.0f, 0.0f, 0.0f};
  for (int i = 0; i < count; ++i) {
    for (int k = 0; k < 3; ++k) sum[k] = raw[i][k] + sum[k];
  }
  const float inverse = 1.0f / static_cast<float>(count);
  for (int k = 0; k < 3; ++k) middle[k] = inverse * sum[k];
  float most = 0.0f;
  for (int i = 0; i < count; ++i) {
    for (int k = 0; k < 3; ++k) {
      points[i][k] = (lifted[i][k] - middle[k]) * scale;
    }
    const float length =
        sqrtf(points[i][0] * points[i][0] + points[i][1] * points[i][1] +
              points[i][2] * points[i][2]);
    most = most <= length ? length : most;
  }
  *body_radius = most;
  return true;
}

// --- the place of a body -----------------------------------------------------

// PhysOb::SetUpPos 0x007FC760: the body takes the frame of its mesh. The rows
// are made of length 1, the middle of mass goes through the frame of the mesh
// as it is, and each point goes through the rows of length 1 from there.
inline void PlaceBody(const float mesh[12], const float middle[3],
                      const float (*local)[3], int count, float (*world)[3],
                      float center[3]) {
  float rows[12];
  for (int i = 0; i < 12; ++i) rows[i] = mesh[i];
  for (int r = 0; r < 3; ++r) {
    float* row = rows + r * 3;
    if (row[0] != 0.0f || row[1] != 0.0f || row[2] != 0.0f) {
      const float k =
          1.0f / sqrtf(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
      for (int i = 0; i < 3; ++i) row[i] = k * row[i];
    }
  }
  Apply(mesh, middle, center);
  for (int i = 0; i < 3; ++i) rows[9 + i] = center[i];
  for (int i = 0; i < count; ++i) Apply(rows, local[i], world[i]);
}

// A MapCoords int of a world unit, as AdjustToGroundLevel makes it (x times
// 65536 times 0.1, cut toward 0).
inline int32_t LandInt(float v) {
  return static_cast<int32_t>(static_cast<double>(v) * 65536.0 *
                              static_cast<double>(0.1f));
}

// A MapCoords int of a world unit, as RaiseUntilNotIntersecting makes it for
// the square of map cells (x times 6553.6, cut toward 0).
inline int32_t CellInt(float v) {
  return static_cast<int32_t>(static_cast<double>(v) *
                              static_cast<double>(6553.6f));
}

// AdjustToGroundLevel with no lift that only raises: the least height of a
// point over the land under it (land[i] for the point i). The body goes down
// by this, so its lowest point is on the land. 1000 with no point.
inline float LandGap(const float (*world)[3], const float* land, int count) {
  float least = 1000.0f;
  for (int i = 0; i < count; ++i) {
    const float over = world[i][1] - land[i];
    if (over < least) least = over;
  }
  return least;
}

// The map cells that RaiseUntilNotIntersecting reads: the square of the
// center x and z plus and minus the radius, in cells of 65536 MapCoords. The
// game holds each corner in the map: cell_x from 0 to cells_x - 1.
inline void CellSquare(float x, float z, float radius, int cells_x,
                       int cells_z, int* x0, int* x1, int* z0, int* z1) {
  auto cell = [](float v, int cells) {
    const int32_t at = CellInt(v);
    int c = at < 0 ? -1 : static_cast<int>(at >> 16);
    if (c < 0) c = 0;
    if (c > cells - 1) c = cells - 1;
    return c;
  };
  *x0 = cell(x - radius, cells_x);
  *x1 = cell(x + radius, cells_x);
  *z0 = cell(z - radius, cells_z);
  *z1 = cell(z + radius, cells_z);
}

// --- the test ------------------------------------------------------------------

// fn_007FC310: the line from p along d meets a face of the body behind p
// (t < 0). The face must face the line (n.d < -0.0001, with n the cross of
// two edges, not of length 1), and the meet must be inside its three edges.
// The meet with the largest t (the nearest one behind p) is the hit.
inline bool FaceHit(const Body& b, const float p[3], const float d[3],
                    float hit[3]) {
  float best = kNoFaceHit;
  bool found = false;
  for (int i = 0; i < b.face_count; ++i) {
    const float* v0 = b.points[b.faces[i][0]];
    const float* v1 = b.points[b.faces[i][1]];
    const float* v2 = b.points[b.faces[i][2]];
    float e1[3], e2[3], n[3];
    Sub(v1, v0, e1);
    Sub(v2, v0, e2);
    Cross(e1, e2, n);
    const float nd = Dot(n, d);
    if (!(static_cast<double>(nd) < kFacing)) continue;
    float w[3];
    Sub(p, v0, w);
    const float t = -(Dot(n, w) / nd);
    if (!(t < 0.0f) || !(t > best)) continue;
    float h[3];
    for (int k = 0; k < 3; ++k) h[k] = t * d[k] + p[k];
    float hv[3], c[3], edge[3];
    Sub(h, v0, hv);
    Cross(e1, hv, c);
    if (!(Dot(c, n) > 0.0f)) continue;
    Sub(v2, v1, edge);
    Sub(h, v1, hv);
    Cross(edge, hv, c);
    if (!(Dot(c, n) > 0.0f)) continue;
    Sub(h, v2, hv);
    Cross(hv, e2, c);
    if (!(Dot(c, n) > 0.0f)) continue;
    best = t;
    for (int k = 0; k < 3; ++k) hit[k] = h[k];
    found = true;
  }
  return found;
}

// fn_00868910 on one ellipsoid: the t of the first meet of the line from p
// along d (it can be less than 0), and the meet in the world.
inline bool EllipsoidMeet(const Ellipsoid& e, const float p[3],
                          const float d[3], float* t_out, float hit[3]) {
  float h[3];
  for (int k = 0; k < 3; ++k) h[k] = (e.box_max[k] - e.box_min[k]) * 0.5f;
  if (!(h[0] > 0.0001f) || !(h[1] > 0.0001f) || !(h[2] > 0.0001f)) {
    return false;
  }
  float lp[3], ld[3], u[3], v[3];
  Apply(e.inverse, p, lp);
  Turn(e.inverse, d, ld);
  for (int k = 0; k < 3; ++k) {
    u[k] = (lp[k] - e.box_min[k]) / h[k] - 1.0f;
    v[k] = ld[k] / h[k];
  }
  const float a = Dot(v, v);
  if (!(a > 0.0001f)) return false;
  float b = Dot(v, u);
  b = b + b;
  const float disc = b * b - (Dot(u, u) - 1.0f) * a * 4.0f;
  if (!(disc >= 0.0f)) return false;
  const float t = (-b - sqrtf(disc)) / (a + a);
  float local[3];
  for (int k = 0; k < 3; ++k) {
    local[k] = (v[k] * t + u[k] + 1.0f) * h[k] + e.box_min[k];
  }
  Apply(e.frame, local, hit);
  *t_out = t;
  return true;
}

// fn_008683C0: of all the ellipsoids, the meet with the least t.
inline bool EllipsoidHit(const Body& b, const float p[3], const float d[3],
                         float hit[3]) {
  float best = kNoEllipsoidHit;
  bool found = false;
  for (int i = 0; i < b.ellipsoid_count; ++i) {
    float t, h[3];
    if (!EllipsoidMeet(b.ellipsoids[i], p, d, &t, h)) continue;
    if (!(t < best)) continue;
    best = t;
    for (int k = 0; k < 3; ++k) hit[k] = h[k];
    found = true;
  }
  return found;
}

// fn_007FDD60: how deep the points of a are in b along d. For each point p of
// a, the hit h in b (its ellipsoids when it has some, else its faces) gives
// (p - h).d. The result is the most of these, and 0 at the least.
inline float BodyDepth(const Body& a, const Body& b, const float d[3]) {
  float most = 0.0f;
  for (int i = 0; i < a.point_count; ++i) {
    const float* p = a.points[i];
    float h[3];
    const bool met = b.ellipsoids != nullptr ? EllipsoidHit(b, p, d, h)
                                             : FaceHit(b, p, d, h);
    if (!met) continue;
    float gap[3];
    Sub(p, h, gap);
    const float depth = Dot(gap, d);
    if (most < depth) most = depth;
  }
  return most;
}

// RaiseUntilNotIntersecting 0x00644800 for one other body: the spheres meet
// (the distance of the centers is less than the sum of the radii), and the
// larger of two depths: the tree in the other body along down, and the other
// body in the tree along up. 0 when the spheres do not meet.
inline float RaiseDepth(const Body& tree, const Body& other) {
  float gap[3];
  Sub(tree.center, other.center, gap);
  const float reach = other.radius + tree.radius;
  if (!(Dot(gap, gap) < reach * reach)) return 0.0f;
  const float down[3] = {0.0f, -1.0f, 0.0f};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  const float a = BodyDepth(tree, other, down);
  const float b = BodyDepth(other, tree, up);
  return b < a ? a : b;
}

// The release raises the tree over the other body: it does not plant it.
inline bool InTheWay(const Body& tree, const Body& other, float* depth) {
  *depth = RaiseDepth(tree, other);
  return *depth > kRaiseLimit;
}

}  // namespace tree_body_math
