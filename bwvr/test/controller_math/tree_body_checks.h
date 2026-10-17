// The test of a body in the way of a tree (tree_body_math.h). main.cpp
// includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/tree_body_math.h"

namespace tree_body_checks {

using namespace tree_body_math;

// A box from lo to hi on each axis, with its 12 faces out.
struct Box {
  float points[8][3];
  int faces[12][3];
  Body body;
};

inline void MakeBox(Box* b, const float lo[3], const float hi[3]) {
  for (int i = 0; i < 8; ++i) {
    b->points[i][0] = (i & 1) ? hi[0] : lo[0];
    b->points[i][1] = (i & 2) ? hi[1] : lo[1];
    b->points[i][2] = (i & 4) ? hi[2] : lo[2];
  }
  // The point i has x from bit 1, y from bit 2 and z from bit 4.
  const int faces[12][3] = {{0, 4, 6}, {0, 6, 2},   // x low
                            {1, 3, 7}, {1, 7, 5},   // x high
                            {0, 1, 5}, {0, 5, 4},   // y low
                            {2, 6, 7}, {2, 7, 3},   // y high
                            {0, 2, 3}, {0, 3, 1},   // z low
                            {4, 5, 7}, {4, 7, 6}};  // z high
  for (int f = 0; f < 12; ++f) {
    for (int k = 0; k < 3; ++k) b->faces[f][k] = faces[f][k];
  }
  b->body = Body{b->points, 8, b->faces, 12, nullptr, 0, {0, 0, 0}, 0.0f};
  float most = 0.0f;
  for (int k = 0; k < 3; ++k) b->body.center[k] = (lo[k] + hi[k]) * 0.5f;
  for (int i = 0; i < 8; ++i) {
    float d[3];
    Sub(b->points[i], b->body.center, d);
    const float length = sqrtf(Dot(d, d));
    if (length > most) most = length;
  }
  b->body.radius = most;
}

// Each face of a shape faces away from its middle of mass (the origin).
inline bool FacesOut(const float (*points)[3], const int (*faces)[3],
                     int count) {
  for (int f = 0; f < count; ++f) {
    const float* a = points[faces[f][0]];
    const float* b = points[faces[f][1]];
    const float* c = points[faces[f][2]];
    float e1[3], e2[3], n[3], mid[3];
    Sub(b, a, e1);
    Sub(c, a, e2);
    Cross(e1, e2, n);
    for (int k = 0; k < 3; ++k) mid[k] = (a[k] + b[k] + c[k]) / 3.0f;
    if (!(Dot(n, mid) > 0.0f)) return false;
  }
  return true;
}

// A tree of height 10 and 2D radius 1 that stands at the origin on flat
// land at height 0, in its body after the land step.
struct Tree {
  float local[kTreePoints][3];
  float world[kTreePoints][3];
  Body body;
};

inline void MakeTree(Tree* t, float x, float z) {
  float middle[3], radius = 0.0f;
  TreeShape(10.0f, 1.0f, true, 1.0f, t->local, middle, &radius);
  const float mesh[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, x, 0, z};
  float center[3];
  PlaceBody(mesh, middle, t->local, kTreePoints, t->world, center);
  float land[kTreePoints] = {};
  const float gap = LandGap(t->world, land, kTreePoints);
  for (int i = 0; i < kTreePoints; ++i) t->world[i][1] -= gap;
  center[1] -= gap;
  t->body = Body{t->world, kTreePoints, TreeFaces(), kTreeFaces, nullptr, 0,
                 {center[0], center[1], center[2]}, radius};
}

inline void TreeBodyChecks() {
  // The shape of a tree that stands: height 10, 2D radius 1, scale 1.
  float points[kTreePoints][3], middle[3], radius = 0.0f;
  Check(TreeShape(10.0f, 1.0f, true, 1.0f, points, middle, &radius),
        "a tree with a scale has a shape", 1, 1);
  Near("the middle of a tree that stands is 0.4 of its height up",
       middle[1], 4.0, 1e-5);
  Near("its body goes 0.6 of its height up and down from the middle",
       points[13][1], 6.0, 1e-5);
  Near("so its feet are 0.2 of its height under its base",
       middle[1] + points[12][1], -2.0, 1e-5);
  Near("the middle ring has the 2D radius", points[4][0], 1.0, 1e-6);
  Near("the low ring has 0.65 of it", points[1][2], -0.65, 1e-6);
  Near("the sphere of a tall tree is half its body", radius, 6.0, 1e-6);
  Check(points[14][0] == points[12][0] && points[15][1] == points[12][1],
        "the points 14 and 15 are copies of the foot", 1, 1);
  Check(FacesOut(points, TreeFaces(), kTreeFaces),
        "each face of a tree faces out", 1, 1);
  TreeShape(10.0f, 1.0f, false, 2.0f, points, middle, &radius);
  Near("a dead tree has its middle at half its height, in mesh units",
       middle[1], 2.5, 1e-5);
  Check(!TreeShape(10.0f, 1.0f, true, 0.0f, points, middle, &radius),
        "a scale of 0 gives no shape", 0, 0);

  // A villager and an animal: height 2, 2D radius 1.
  float living[kLivingPoints][3];
  LivingShape(2.0f, 1.0f, false, 1.0f, living, middle, &radius);
  Near("the middle of a villager is half its height up", middle[1], 1.0,
       1e-6);
  Near("the box of a villager turns: its width of 0.8 is along z",
       living[0][2], 0.8, 1e-5);
  Near("and its depth of 0.32 is along x", living[0][0], -0.32, 1e-5);
  Check(FacesOut(living, LivingFaces(), kLivingFaces),
        "each face of a villager faces out", 1, 1);
  LivingShape(2.0f, 1.0f, true, 1.0f, living, middle, &radius);
  Near("an animal is 0.9 long in the middle, along x after the turn",
       living[5][0], 0.9, 1e-5);
  Check(FacesOut(living, LivingFaces(), kLivingFaces),
        "each face of an animal faces out", 1, 1);

  // The hull of a mesh: the middle is the mean, the points are scaled.
  const float raw[4][3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {2, 2, 4}};
  float lifted[4][3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {2, 2, 4}};
  lifted[3][1] += 1.0f;
  float hull[4][3];
  Check(HullShape(raw, lifted, 4, 2.0f, hull, middle, &radius),
        "a hull with points has a shape", 1, 1);
  Near("the middle of a hull is the mean of its raw points", middle[2], 1.0,
       1e-6);
  Near("a point of a hull is its lifted vertex less the middle, scaled",
       hull[3][1], 4.0, 1e-5);
  Check(!HullShape(raw, lifted, 0, 1.0f, hull, middle, &radius),
        "a hull with no point has no shape", 0, 0);

  // The place: rows of length 2 move the middle by 2, and the points by 1.
  const float mesh[12] = {2, 0, 0, 0, 2, 0, 0, 0, 2, 10, 5, -3};
  const float one[1][3] = {{1, 0, 0}};
  const float mid[3] = {0, 1, 0};
  float placed[1][3], center[3];
  PlaceBody(mesh, mid, one, 1, placed, center);
  Near("the middle goes through the frame of the mesh as it is", center[1],
       7.0, 1e-6);
  Near("a point goes through rows of length 1", placed[0][0], 11.0, 1e-6);

  // The inverse of a frame.
  const float frame[12] = {0, 0, -2, 0, 3, 0, 2, 0, 0, 4, 5, 6};
  float inverse[12], p[3], back[3];
  Invert(frame, inverse);
  const float q[3] = {0.5f, -1.0f, 2.0f};
  Apply(frame, q, p);
  Apply(inverse, p, back);
  Near("the inverse of a frame takes a point back (x)", back[0], 0.5, 1e-5);
  Near("the inverse of a frame takes a point back (z)", back[2], 2.0, 1e-5);

  // The land.
  const float three[3][3] = {{0, 5, 0}, {1, 3, 0}, {2, 4, 0}};
  const float under[3] = {1, 2, 0};
  Near("the land gap is the least height over the land", LandGap(three, under,
       3), 1.0, 1e-6);
  const float sunk[1][3] = {{0, -1, 0}};
  const float flat_land[1] = {0};
  Near("a body that is in the land has a gap under 0",
       LandGap(sunk, flat_land, 1), -1.0, 1e-6);
  Check(LandInt(1.0f) == 6553 && CellInt(10.0f) == 65536,
        "a world unit is 6553.6 MapCoords, and a cell is 10 units",
        LandInt(1.0f), 6553);
  int x0, x1, z0, z1;
  CellSquare(15.0f, 15.0f, 3.0f, 256, 256, &x0, &x1, &z0, &z1);
  Check(x0 == 1 && x1 == 1, "a small square in one cell reads one cell", x1,
        1);
  CellSquare(9.0f, -2.0f, 3.0f, 256, 256, &x0, &x1, &z0, &z1);
  Check(x0 == 0 && x1 == 1 && z0 == 0 && z1 == 0,
        "the square reads each cell that it touches, and holds 0", x1, 1);
  CellSquare(2555.0f, 0.0f, 20.0f, 256, 256, &x0, &x1, &z0, &z1);
  Check(x1 == 255, "the square holds the last cell", x1, 255);

  // The face test on a box from -1 to 1.
  Box box;
  const float lo[3] = {-1, -1, -1}, hi[3] = {1, 1, 1};
  MakeBox(&box, lo, hi);
  Check(FacesOut(box.points, box.faces, 12), "the faces of the test box face "
        "out", 1, 1);
  const float down[3] = {0, -1, 0}, up[3] = {0, 1, 0};
  float hit[3];
  const float inside[3] = {0.2f, 0.0f, 0.3f};
  bool met = FaceHit(box.body, inside, down, hit);
  Check(met && fabsf(hit[1] - 1.0f) < 1e-6f,
        "a point in the box meets the top of the box along down", hit[1], 1);
  const float over[3] = {0.2f, 2.0f, 0.3f};
  Check(!FaceHit(box.body, over, down, hit),
        "a point over the box does not meet it behind", 0, 0);
  const float under_box[3] = {0.2f, -5.0f, 0.3f};
  Check(FaceHit(box.body, under_box, down, hit),
        "a point under the box meets its top too, as in the game", 1, 1);
  const float beside[3] = {3.0f, 0.0f, 0.0f};
  Check(!FaceHit(box.body, beside, down, hit),
        "a point beside the box does not meet it", 0, 0);

  // The depth of one body in another.
  float one_point[1][3] = {{0.2f, 0.25f, 0.3f}};
  Body probe{one_point, 1, nullptr, 0, nullptr, 0, {0, 0, 0}, 0.0f};
  Near("the depth of a point in the box along down is the way to its top",
       BodyDepth(probe, box.body, down), 0.75, 1e-6);
  Near("along up it is the way to its bottom", BodyDepth(probe, box.body, up),
       1.25, 1e-6);

  // A tree of height 10 and radius 1 at the origin, and a rock beside it.
  Tree tree;
  MakeTree(&tree, 0.0f, 0.0f);
  Near("after the land step the feet of the tree are on the land",
       tree.world[12][1], 0.0, 1e-5);
  // The low cone of the tree goes from the feet at the land up to the low
  // ring, 2.4 units over the land. A low rock under it is not in the way.
  Box rock;
  const float rock_lo[3] = {0.3f, 0.0f, -0.4f}, rock_hi[3] = {1.1f, 0.6f, 0.4f};
  MakeBox(&rock, rock_lo, rock_hi);
  float depth = 0.0f;
  bool way = InTheWay(tree.body, rock.body, &depth);
  Check(!way,
        "a low rock under the crown of the tree is not in the way", depth, 0);
  Box tall;
  const float tall_lo[3] = {0.2f, 0.0f, -0.3f}, tall_hi[3] = {1.0f, 4.0f, 0.3f};
  MakeBox(&tall, tall_lo, tall_hi);
  way = InTheWay(tree.body, tall.body, &depth);
  Check(way && depth > 2.0f,
        "a rock that goes up into the low part of the tree is in the way",
        depth, 2.25);
  Box far_rock;
  const float far_lo[3] = {9.3f, 0.0f, -0.4f}, far_hi[3] = {10.1f, 0.6f, 0.4f};
  MakeBox(&far_rock, far_lo, far_hi);
  way = InTheWay(tree.body, far_rock.body, &depth);
  Check(!way && depth == 0.0f,
        "a rock whose sphere does not meet the tree is not in the way",
        depth, 0);
  Box near_rock;
  const float near_lo[3] = {1.6f, 0.0f, -0.4f}, near_hi[3] = {2.4f, 0.6f, 0.4f};
  MakeBox(&near_rock, near_lo, near_hi);
  way = InTheWay(tree.body, near_rock.body, &depth);
  Check(!way,
        "a rock past the 2D radius of the tree is not in the way", depth, 0);
  Box flat;
  const float flat_lo[3] = {-3.0f, -0.5f, -3.0f}, flat_hi[3] = {3.0f, 0.0f, 3.0f};
  MakeBox(&flat, flat_lo, flat_hi);
  way = InTheWay(tree.body, flat.body, &depth);
  Check(!way,
        "a flat body with its top at the land is not in the way", depth, 0);

  // The ellipsoids of a Creature: one sphere of radius 1 at the origin.
  Ellipsoid ball = {{-1, -1, -1}, {1, 1, 1},
                    {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}, {}};
  Invert(ball.frame, ball.inverse);
  const float origin[1][3] = {{0, 0, 0}};
  Body creature{origin, 1, nullptr, 0, &ball, 1, {0, 0, 0}, 1.0f};
  float t = 0.0f;
  const float middle_point[3] = {0, 0, 0};
  met = EllipsoidMeet(ball, middle_point, down, &t, hit);
  Check(met && fabsf(t + 1.0f) < 1e-6f && fabsf(hit[1] - 1.0f) < 1e-5f,
        "a point in the middle of a ball meets its top at t -1", t, -1);
  const Body middle_probe{origin, 1, nullptr, 0, nullptr, 0, {0, 0, 0}, 0};
  Near("the depth of the middle in the ball along down is 1",
       BodyDepth(middle_probe, creature, down), 1.0, 1e-5);
  const float high[1][3] = {{0, 3, 0}};
  Near("a point over the ball has no depth in it",
       BodyDepth(Body{high, 1, nullptr, 0, nullptr, 0, {0, 0, 0}, 0},
                 creature, down),
       0.0, 1e-6);
  Ellipsoid wide = {{-1, -1, -1}, {1, 1, 1},
                    {2, 0, 0, 0, 2, 0, 0, 0, 2, 5, 0, 0}, {}};
  Invert(wide.frame, wide.inverse);
  const float in_wide[3] = {5.0f, 1.0f, 0.0f};
  met = EllipsoidMeet(wide, in_wide, down, &t, hit);
  Check(met && fabsf(hit[1] - 2.0f) < 1e-4f,
        "a ball of radius 2 in a scaled frame has its top at 2", hit[1], 2);
  Ellipsoid over_tree = {{-1, -1, -1}, {1, 1, 1},
                         {1, 0, 0, 0, 1, 0, 0, 0, 1, 0.5f, 2.0f, 0.0f}, {}};
  Invert(over_tree.frame, over_tree.inverse);
  const float bone[1][3] = {{0.5f, 2.0f, 0.0f}};
  Body arm{bone, 1, nullptr, 0, &over_tree, 1, {0.5f, 2.0f, 0.0f}, 1.0f};
  way = InTheWay(tree.body, arm, &depth);
  Check(way,
        "a ball of a Creature over the feet of the tree is in the way",
        depth, 2.92);
}

}  // namespace tree_body_checks
