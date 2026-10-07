// The fingers and the world: the pure math. See docs/hand.md, "Hands that
// react to the world". hand_feel.cpp uses it, and test/controller_math checks
// it (feel_checks.h). Nothing here reads a global or touches the game.
//
// THE RULES.
// - The place and the turn of a hand never change. Only the curl of a finger
//   changes.
// - The player's own finger wins. The world can add bend to a finger. It can
//   open a finger only while the trigger or the grip of that finger is open.
// - A curl changes through a short filter, so no finger jumps.
// - A finger moves only when the hand moved or the world near it changed.

#pragma once

#include <math.h>

#include "near_math.h"
#include "overlay_math.h"

namespace hand_feel {

const int kFingers = overlay_math::kFingers;  // 0 the thumb ... 4 the little
// The points of a finger: the knuckle, the two joints and the tip.
const int kPoints = overlay_math::kJoints + 1;

inline float Clamp01(float v) { return overlay_math::Clamp01(v); }

// --- the finger table ---------------------------------------------------------

// THE FINGER TABLE. The points of one finger at the curls 0, 1/16 ... 1, in
// one frame: the hand frame, or the world for the hand of this frame. A curl
// between two rows is a straight mix of the rows. One row is 4 to 8 degrees
// at a joint, so the mix is less than 0.2 percent of a bone from the true
// place.
const int kCurlSteps = 16;

struct FingerTable {
  float point[kCurlSteps + 1][kPoints][3];
};

// Fills a table from the model of the hand: joints(finger, curl, out).
template <typename Joints>
inline void FillTable(int finger, Joints joints, FingerTable* out) {
  for (int i = 0; i <= kCurlSteps; ++i) {
    joints(finger, static_cast<float>(i) / kCurlSteps, out->point[i]);
  }
}

inline void TableAt(const FingerTable& table, float curl,
                    float out[kPoints][3]) {
  const float c = Clamp01(curl) * kCurlSteps;
  int row = static_cast<int>(c);
  if (row >= kCurlSteps) row = kCurlSteps - 1;
  const float share = c - static_cast<float>(row);
  for (int p = 0; p < kPoints; ++p) {
    for (int k = 0; k < 3; ++k) {
      const float a = table.point[row][p][k];
      out[p][k] = a + share * (table.point[row + 1][p][k] - a);
    }
  }
}

// A table of the hand frame in the world, for the place of a hand
// (overlay_math::PlacePoint).
inline void PlaceTable(const FingerTable& local, const float position[3],
                       const float rotation[9], float length, bool left,
                       FingerTable* out) {
  for (int i = 0; i <= kCurlSteps; ++i) {
    for (int p = 0; p < kPoints; ++p) {
      overlay_math::PlacePoint(position, rotation, length, left,
                               local.point[i][p], out->point[i][p]);
    }
  }
}

// A point of the world in the hand frame: the inverse of
// overlay_math::PlacePoint. The rotation has columns of length 1.
inline void ToHandFrame(const float position[3], const float rotation[9],
                        float length, bool left, const float p[3],
                        float out[3]) {
  const float d[3] = {p[0] - position[0], p[1] - position[1],
                      p[2] - position[2]};
  const float scale = length > 0.0f ? 1.0f / length : 0.0f;
  for (int c = 0; c < 3; ++c) {
    out[c] = scale * (rotation[0 * 3 + c] * d[0] + rotation[1 * 3 + c] * d[1] +
                      rotation[2 * 3 + c] * d[2]);
  }
  if (left) out[0] = -out[0];
}

// THE SAMPLES of a finger: the points that the tests use. The middle of each
// of the three bones, the two joints and the tip. The knuckle does not move
// with the curl, so it is no sample.
const int kSamples = 6;

inline void Samples(const float points[kPoints][3], float out[kSamples][3]) {
  for (int k = 0; k < 3; ++k) {
    out[0][k] = 0.5f * (points[0][k] + points[1][k]);
    out[1][k] = points[1][k];
    out[2][k] = 0.5f * (points[1][k] + points[2][k]);
    out[3][k] = points[2][k];
    out[4][k] = 0.5f * (points[2][k] + points[3][k]);
    out[5][k] = points[3][k];
  }
}

// The radius of each sample. The tip is the end of the mesh, so its radius is
// small: the clearance that the Hand's point keeps over the land (0.5 cm of
// the room), and never more than the finger. The other samples have kSkin of
// the radius of the finger.
const float kSkin = 0.8f;
const float kTipCm = 0.5f;

inline void SampleRadii(float finger_radius, float tip_radius,
                        float out[kSamples]) {
  const float body = kSkin * finger_radius;
  for (int s = 0; s < kSamples - 1; ++s) out[s] = body;
  out[kSamples - 1] = tip_radius < body ? tip_radius : body;
}

// --- the land -----------------------------------------------------------------

// THE LAND PATCH. The height of the land on a square grid under a hand,
// read from the game once a frame. Between the points of the grid the height
// is the mix of the four points around it. Past the edge it is the height of
// the edge.
const int kPatch = 8;

struct LandPatch {
  float x0, z0;  // the corner with the least x and z
  float step;    // between two points of the grid
  float height[kPatch][kPatch];  // [z][x]
};

template <typename Ground>
inline void FillPatch(float center_x, float center_z, float half,
                      Ground ground, LandPatch* out) {
  out->x0 = center_x - half;
  out->z0 = center_z - half;
  out->step = 2.0f * half / (kPatch - 1);
  for (int iz = 0; iz < kPatch; ++iz) {
    for (int ix = 0; ix < kPatch; ++ix) {
      out->height[iz][ix] =
          ground(out->x0 + out->step * ix, out->z0 + out->step * iz);
    }
  }
}

inline float PatchHeight(const LandPatch& patch, float x, float z) {
  if (!(patch.step > 0.0f)) return patch.height[0][0];
  float fx = (x - patch.x0) / patch.step;
  float fz = (z - patch.z0) / patch.step;
  if (!(fx > 0.0f)) fx = 0.0f;
  if (!(fz > 0.0f)) fz = 0.0f;
  if (fx > kPatch - 1.0f) fx = kPatch - 1.0f;
  if (fz > kPatch - 1.0f) fz = kPatch - 1.0f;
  int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
  if (ix > kPatch - 2) ix = kPatch - 2;
  if (iz > kPatch - 2) iz = kPatch - 2;
  const float sx = fx - ix, sz = fz - iz;
  const float a = patch.height[iz][ix] +
                  sx * (patch.height[iz][ix + 1] - patch.height[iz][ix]);
  const float b = patch.height[iz + 1][ix] +
                  sx * (patch.height[iz + 1][ix + 1] - patch.height[iz + 1][ix]);
  return a + sz * (b - a);
}

// --- the bodies ---------------------------------------------------------------

// The distance from a point to the segment from a to b, less the radius: the
// gap to a capsule. A segment with a == b is a ball.
inline float SegmentGap(const float a[3], const float b[3], float radius,
                        const float p[3]) {
  const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const float w[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
  const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  float t = 0.0f;
  if (dd > 1e-12f) t = (w[0] * d[0] + w[1] * d[1] + w[2] * d[2]) / dd;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float sum = 0.0f;
  for (int i = 0; i < 3; ++i) {
    const float e = w[i] - d[i] * t;
    sum += e * e;
  }
  return sqrtf(sum) - radius;
}

// THE BODY of an object near a hand, with the cheap forms that the choice of
// the grip uses (near_math.h): an upright cylinder from the point of the
// object, a tree, the capsule of a Creature, or a ball.
//
// A TREE for a finger is what the player sees: the trunk, a cylinder of
// near_math::kTrunkShare of the radius from the land to half the height, and
// the crown, an upright spheroid in the upper half. The choice of the grip
// has the crown from the land up, which is wider than the tree near the
// land. A finger must not bend in the air beside a trunk.
//
// A BOX is the box of the mesh of an object, with the turn of the object. A
// house has walls, and the cylinder round it is up to half its width away
// from the middle of a wall. So an object that is no tree and no Creature has
// its box when the game gives one.
enum BodyKind { kCylinder = 0, kTree = 1, kCapsule = 2, kBall = 3, kBox = 4 };

struct Body {
  int kind;
  float base[3];  // the point on the land, or the middle of a ball or a box
  float radius;
  float height;
  float axis[9];  // a box: its three axes of length 1, one in each row
  float half[3];  // a box: half its size along each axis
};

// The gap from a point to a box, 0 inside it.
inline float BoxGap(const Body& body, const float p[3]) {
  const float d[3] = {p[0] - body.base[0], p[1] - body.base[1],
                      p[2] - body.base[2]};
  float sum = 0.0f;
  for (int j = 0; j < 3; ++j) {
    const float* axis = &body.axis[j * 3];
    float along = fabsf(d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]) -
                  body.half[j];
    if (along > 0.0f) sum += along * along;
  }
  return sqrtf(sum);
}

// The gap from a point to a body, 0 or less inside it.
inline float BodyGapAt(const Body& body, const float p[3]) {
  switch (body.kind) {
    case kTree: {
      const float half = 0.5f * body.height;
      const float crown[3] = {body.base[0], body.base[1] + 1.5f * half,
                              body.base[2]};
      const float leaves =
          near_math::SpheroidGap(p, crown, body.radius, 0.5f * half).gap;
      const float trunk =
          near_math::BodyGap(p, body.base, near_math::kTrunkShare * body.radius,
                             half).gap;
      return trunk < leaves ? trunk : leaves;
    }
    case kBall:
      return near_math::BallGap(p, body.base, body.radius).gap;
    case kBox:
      return BoxGap(body, p);
    case kCapsule: {
      const float top[3] = {body.base[0], body.base[1] + body.height,
                            body.base[2]};
      return SegmentGap(body.base, top, body.radius, p);
    }
    default:
      return near_math::BodyGap(p, body.base, body.radius, body.height).gap;
  }
}

// A BODY THAT IS LARGER THAN THE HAND DOES NOT COUNT. A cylinder or a box
// with a radius of more than kBodyMaxRadius hand lengths is a large building
// (the Temple, a village center). Its cheap form is far from what the player
// sees, and a finger must not bend in the air beside it. A house has a
// radius of 8 to 14 units, and the real hand is 19 units long at 100 units a
// meter.
const float kBodyMaxRadius = 0.80f;

inline bool BodyCounts(const Body& body, float hand_length) {
  if (body.kind == kCylinder || body.kind == kBox) {
    return body.radius <= kBodyMaxRadius * hand_length;
  }
  return true;
}

// THE WORLD NEAR ONE HAND in one frame: the land patch and the bodies.
const int kMaxBodies = 12;

struct World {
  bool land;
  LandPatch patch;
  int bodies;
  Body body[kMaxBodies];
};

// What a finger is in: nothing, the land or a body.
const int kInNothing = 0;
const int kInLand = 1;
const int kInBody = 2;

// HOW FAR A FINGER IS IN THE WORLD: the largest depth of its samples, in
// world units. For the land the depth is straight down. For a body it is the
// radius of the sample less the gap. 0 or less means that the finger is free.
// `what` gets the thing with the largest depth.
inline float Depth(const World& world, const float samples[kSamples][3],
                   const float radius[kSamples], int* what) {
  float most = -1e30f;
  int in = kInNothing;
  for (int s = 0; s < kSamples; ++s) {
    const float* p = samples[s];
    if (world.land) {
      const float depth =
          PatchHeight(world.patch, p[0], p[2]) + radius[s] - p[1];
      if (depth > most) {
        most = depth;
        in = kInLand;
      }
    }
    for (int b = 0; b < world.bodies; ++b) {
      const float depth = radius[s] - BodyGapAt(world.body[b], p);
      if (depth > most) {
        most = depth;
        in = kInBody;
      }
    }
  }
  if (what != nullptr) *what = most > 0.0f ? in : kInNothing;
  return most;
}

// --- the free curl ------------------------------------------------------------

// THE FREE CURL. The curl nearest to the curl of the pose at which the finger
// is not in the world. depth_at(curl) gives the depth of the finger at a
// curl.
//
// - The pose is free: the pose.
// - If not, the first free curl on the grid of kCurlSteps above the pose, and
//   with may_open the first one below it. The nearer of the two wins. The
//   side of the last frame wins when the other side is nearer by less than
//   kSideHold, so a finger at the middle between two answers does not go
//   back and forth.
// - Then five steps of a search between that curl and the last curl that is
//   not free, so the finger rests on the surface.
// - No free curl: the curl with the least depth, when it is less deep than
//   the pose by more than kStuckGain of the depth. If not, the pose.
const float kSideHold = 0.10f;
const float kStuckGain = 0.10f;
const int kSearchSteps = 5;

struct Free {
  float curl;    // the curl to take
  bool blocked;  // the curl of the pose is in the world
  bool free;     // the curl to take is free
  int side;      // 1 more bend than the pose, -1 less, 0 the pose
};

template <typename DepthAt>
inline Free FreeCurl(float pose, bool may_open, int last_side,
                     DepthAt depth_at) {
  Free out = {Clamp01(pose), false, true, 0};
  const float pose_depth = depth_at(out.curl);
  if (!(pose_depth > 0.0f)) return out;
  out.blocked = true;
  const float step = 1.0f / kCurlSteps;
  float up = -1.0f, down = -1.0f;
  float least_depth = pose_depth, least_curl = out.curl;
  const int first_up = static_cast<int>(floorf(out.curl * kCurlSteps)) + 1;
  for (int i = first_up; i <= kCurlSteps; ++i) {
    const float curl = static_cast<float>(i) * step;
    const float depth = depth_at(curl);
    if (!(depth > 0.0f)) {
      up = curl;
      break;
    }
    if (depth < least_depth) {
      least_depth = depth;
      least_curl = curl;
    }
  }
  if (may_open) {
    const int first_down = static_cast<int>(ceilf(out.curl * kCurlSteps)) - 1;
    for (int i = first_down; i >= 0; --i) {
      const float curl = static_cast<float>(i) * step;
      const float depth = depth_at(curl);
      if (!(depth > 0.0f)) {
        down = curl;
        break;
      }
      if (depth < least_depth) {
        least_depth = depth;
        least_curl = curl;
      }
    }
  }
  if (up < 0.0f && down < 0.0f) {
    out.free = false;
    if (least_depth < pose_depth * (1.0f - kStuckGain)) {
      out.side = least_curl > out.curl ? 1 : -1;
      out.curl = least_curl;
    }
    return out;
  }
  bool take_up = up >= 0.0f;
  if (up >= 0.0f && down >= 0.0f) {
    const float up_gap = up - out.curl, down_gap = out.curl - down;
    const float bias = last_side > 0 ? kSideHold
                                     : (last_side < 0 ? -kSideHold : 0.0f);
    take_up = up_gap <= down_gap + bias;
  }
  float free_curl = take_up ? up : down;
  // The last curl that is not free: one step of the grid toward the pose, or
  // the pose when that is nearer.
  float blocked = take_up ? free_curl - step : free_curl + step;
  if (take_up ? blocked < out.curl : blocked > out.curl) blocked = out.curl;
  for (int i = 0; i < kSearchSteps; ++i) {
    const float middle = 0.5f * (free_curl + blocked);
    if (depth_at(middle) > 0.0f) {
      blocked = middle;
    } else {
      free_curl = middle;
    }
  }
  out.side = take_up ? 1 : -1;
  out.curl = free_curl;
  return out;
}

// THE PLAYER'S OWN FINGER WINS. The world opens a finger only while the
// player's own finger is open: its value (the trigger for the index finger,
// the grip for the middle, ring and little fingers, the larger one for the
// thumb) is kOpenValue or less.
const float kOpenValue = 0.15f;

inline bool MayOpen(float value) { return !(value > kOpenValue); }

// --- the filter ---------------------------------------------------------------

// THE FILTER. What the world adds to the curl of the pose goes to its new
// value with a time constant of kFilterSeconds. The curl of the pose itself
// has no filter, so the trigger and the grip show with no lag.
const float kFilterSeconds = 0.05f;

inline float FilterStep(float value, float target, float dt, float seconds) {
  if (!(seconds > 0.0f) || !(dt > 0.0f)) return dt > 0.0f ? target : value;
  return value + (1.0f - expf(-dt / seconds)) * (target - value);
}

// --- the approach -------------------------------------------------------------

// THE GAME'S OWN POSE FOR A THING THAT THE HAND CAN TAKE: the animation
// `can_pickup` of hh.HBN (number 12), as the curl of each finger of the hand
// model that is nearest to the bends of each of its three frames. The index
// finger is straight in each frame: it points at the thing. Frame 0 is the
// open hand. In frame 2 the thumb comes to the index finger and the middle
// finger is a hook: the hand that takes a small thing.
const float kPickupCurls[3][kFingers] = {
    {0.00f, 0.00f, 0.00f, 0.10f, 0.08f},
    {0.06f, 0.00f, 0.12f, 0.19f, 0.21f},
    {0.62f, 0.00f, 0.24f, 0.20f, 0.10f}};

// The frame for the size of the target: the larger of its radius and half its
// height, over the length of the hand. A target of kPickupLarge or more gets
// the open hand of frame 0, and a small one goes toward frame 2.
const float kPickupLarge = 0.30f;

inline float PickupFrame(float size_over_length) {
  return 2.0f * (1.0f - Clamp01(size_over_length / kPickupLarge));
}

// The curls between the frames of a pose table.
inline void FrameCurls(const float (*frames)[kFingers], int count, float frame,
                       float out[kFingers]) {
  if (!(frame > 0.0f)) frame = 0.0f;
  if (frame > count - 1.0f) frame = count - 1.0f;
  int row = static_cast<int>(frame);
  if (row > count - 2) row = count - 2;
  if (row < 0) row = 0;
  const float share = count > 1 ? frame - static_cast<float>(row) : 0.0f;
  for (int f = 0; f < kFingers; ++f) {
    const float a = frames[row][f];
    const float b = count > 1 ? frames[row + 1][f] : a;
    out[f] = a + share * (b - a);
  }
}

// THE WEIGHT OF THE APPROACH. The grip has a target at the Hand's point from
// the reach of the pick on. The weight is 0 at the reach and 1 when the point
// touches the body, with no step at either end.
inline float ApproachWeight(float gap, float reach, float touch) {
  if (!(reach > touch)) return gap <= reach ? 1.0f : 0.0f;
  const float u = Clamp01((reach - gap) / (reach - touch));
  return u * u * (3.0f - 2.0f * u);
}

// The share of the approach that a finger takes: all of it while the player's
// own finger is open, and none from kApproachClosed on. So a grip that closes
// takes the hand from the open pose to the pose of the grip with no step.
const float kApproachClosed = 0.5f;

inline float ApproachShare(float value) {
  return 1.0f - Clamp01(value / kApproachClosed);
}

inline float ApproachCurl(float pose, float pickup, float weight,
                          float value) {
  return pose + weight * ApproachShare(value) * (pickup - pose);
}

// THE SHAPE OF THE APPROACH. The curls of `can_pickup` are near the curls of
// a hand at rest, so the player does not see the hand change (run hf6: 11 0
// 13 19 20 against 0 12 12 12 12). A real hand that reaches for a thing takes
// the form of the thing before it closes. So each finger goes to kPreShape of
// the curl at which it holds the target, and never to less than the curl of
// `can_pickup`. A small target gives a claw, and a large one an open hand.
const float kPreShape = 0.55f;

inline float PreShapeCurl(float hold, float pickup) {
  const float shaped = kPreShape * Clamp01(hold);
  return shaped > pickup ? shaped : pickup;
}

// --- the hold -----------------------------------------------------------------

// THE HOLD OF THE GAME [V HandStateHolding::Update 0x005B3C70, the switch at
// 0x005B4E9x on CHand+0x490C]. Each object has a hold type
// (Object::GetHoldType, vtable slot 0x58C), a hold radius (GetHoldRadius,
// slot 0x590) and a share of its height that it hangs under the Hand
// (GetHoldLoweringMultiplier, slot 0x594):
//
//   1  each object with no rule of its own, a rock      hold_above
//   2  a miracle seed                                   wiggle, at its middle
//   3  a miracle seed with the horn                     horn
//   5  a tree, a dead tree                              hold_side
//   6  a mobile object, a crop, some rocks              hold_side
//   7  a villager, an animal                            hold_side
//
// The game does not play the animation. It takes one moment of it, by the
// hold radius over the length of the Hand (3.2 times CHand+0x4834), 1 at
// most:
//
//   hold_above  time = duration / 2 * (1 - r), r = radius / (1.2 * length)
//   hold_side   time = duration / 2 * r,       r = radius / length
//
// hold_above has 4 frames and hold_side has 3, and both loop, so the frame is
// 2 * (1 - r) and 1.5 * r. A small object closes the hand, and a large one
// opens it.
const int kHoldAbove = 1;
const int kHoldSeed = 2;
const int kHoldHorn = 3;
const int kHoldTree = 5;
const int kHoldMobile = 6;
const int kHoldLiving = 7;

// The frames of the two animations as curls of the hand model: frames 0, 1
// and 2 of hold_above (open, half and closed), and frames 0 and 1 of
// hold_side (closed and open). The thumb of hold_side stays out.
const float kHoldAboveCurls[3][kFingers] = {
    {0.05f, 0.01f, 0.01f, 0.03f, 0.03f},
    {0.21f, 0.01f, 0.01f, 0.18f, 0.16f},
    {0.84f, 0.66f, 0.58f, 0.65f, 0.64f}};
const float kHoldSideCurls[2][kFingers] = {
    {0.13f, 0.99f, 0.90f, 0.83f, 0.84f},
    {0.00f, 0.00f, 0.03f, 0.11f, 0.11f}};
// The hand of the mouse player (`wiggle`), for a miracle seed.
const float kWiggleCurls[kFingers] = {0.13f, 0.00f, 0.13f, 0.15f, 0.21f};

inline bool HoldFromSide(int type) {
  return type == kHoldTree || type == kHoldMobile || type == kHoldLiving;
}

// The curls of the game's hold for a hold type, a hold radius and the length
// of the hand, in the same unit.
inline void GameHoldCurls(int type, float hold_radius, float hand_length,
                          float out[kFingers]) {
  if (!(hand_length > 0.0f) || !(hold_radius > 0.0f)) hold_radius = 0.0f;
  if (HoldFromSide(type)) {
    const float r = hand_length > 0.0f ? Clamp01(hold_radius / hand_length)
                                       : 0.0f;
    FrameCurls(kHoldSideCurls, 2, 1.5f * r, out);
  } else if (type == kHoldSeed || type == kHoldHorn) {
    for (int f = 0; f < kFingers; ++f) out[f] = kWiggleCurls[f];
  } else {
    const float r = hand_length > 0.0f
                        ? Clamp01(hold_radius / (1.2f * hand_length))
                        : 0.0f;
    FrameCurls(kHoldAboveCurls, 3, 2.0f * (1.0f - r), out);
  }
}

// THE BODY IN THE HAND, in the hand frame, with the length of the hand as the
// unit: a segment and a radius. A ball has a == b.
struct HeldBody {
  float a[3];
  float b[3];
  float radius;
};

// A body is never thinner than kHeldMinRadius, so a finger does not go into
// a small thing. A body that is short too (a villager, a pebble) counts as
// kHeldSmallRadius: the hand stays a loose cup round it, and the player sees
// what is in the hand. A long body (the trunk of a tree) goes out of the
// hand at both ends, so the fingers close on it.
const float kHeldMinRadius = 0.03f;
const float kHeldSmallRadius = 0.07f;
const float kHeldShort = 0.45f;  // the length of the body, with its ends

inline void MakeHeldBody(const float a[3], const float b[3], float radius,
                         HeldBody* out) {
  float length = 0.0f;
  for (int k = 0; k < 3; ++k) {
    out->a[k] = a[k];
    out->b[k] = b[k];
    length += (b[k] - a[k]) * (b[k] - a[k]);
  }
  length = sqrtf(length);
  if (!(radius > kHeldMinRadius)) radius = kHeldMinRadius;
  if (length + 2.0f * radius < kHeldShort && radius < kHeldSmallRadius) {
    radius = kHeldSmallRadius;
  }
  out->radius = radius;
}

// THE WRAP. A finger closes from the open hand until it touches the body:
// the largest curl at which no sample of the finger is in the body, from the
// first row of the table that is in it and a search below that row. `touch`
// is false when the finger closes to a fist with no contact, and when the
// open finger is in the body already (then the curl is 0).
inline float WrapCurl(const FingerTable& table, const float radius[kSamples],
                      const HeldBody& body, bool* touch) {
  const auto in_body = [&](float curl) {
    float points[kPoints][3], samples[kSamples][3];
    TableAt(table, curl, points);
    Samples(points, samples);
    for (int s = 0; s < kSamples; ++s) {
      if (SegmentGap(body.a, body.b, body.radius, samples[s]) < radius[s]) {
        return true;
      }
    }
    return false;
  };
  *touch = false;
  if (in_body(0.0f)) return 0.0f;
  const float step = 1.0f / kCurlSteps;
  for (int i = 1; i <= kCurlSteps; ++i) {
    const float curl = static_cast<float>(i) * step;
    if (!in_body(curl)) continue;
    float free_curl = curl - step, blocked = curl;
    for (int k = 0; k < kSearchSteps; ++k) {
      const float middle = 0.5f * (free_curl + blocked);
      if (in_body(middle)) {
        blocked = middle;
      } else {
        free_curl = middle;
      }
    }
    *touch = true;
    return free_curl;
  }
  return 1.0f;
}

// THE HOLD POSE. The game's rule gives the kind of the hold, and the contact
// gives the curl.
//
// - A finger that can touch the body stops on it. For an object that the
//   game holds from the side, the body is the capsule on the axis of the
//   object (a trunk, a villager). For each other object it is the ball that
//   hand_exact.cpp puts at the palm.
// - A finger that cannot touch the capsule stops on that ball, as in the
//   builds before. The mod puts the middle of the ball on the axis, so a
//   large tree is some cm in front of the palm, and the hand is a cup under
//   it.
// - A finger that can touch neither takes the curl of the game's own hold.
// - From the side the thumb does not close more than the thumb of the
//   game's hold_side: the fist of the game with the thumb out.
//
// The range is that of overlay_math::HoldRange: the grip takes the middle,
// ring and little fingers from the rest curl to the hold, and the index
// finger and the thumb stay near the hold when the trigger is open. Each
// sample has the full radius of the finger here, as the hold of the builds
// before has it. `touches` gets 1 for the capsule, 2 for the ball and 0 for
// no contact.
inline void HoldRange(const FingerTable tables[kFingers],
                      const float finger_radius[kFingers],
                      const HeldBody* axis_body, const HeldBody& ball,
                      const float game[kFingers], bool side,
                      overlay_math::PoseRange* out, int touches[kFingers]) {
  for (int f = 0; f < kFingers; ++f) {
    float radius[kSamples];
    for (int s = 0; s < kSamples; ++s) radius[s] = finger_radius[f];
    bool touch = false;
    int which = 0;
    float wrap = 1.0f;
    if (axis_body != nullptr) {
      wrap = WrapCurl(tables[f], radius, *axis_body, &touch);
      if (touch) which = 1;
    }
    if (!touch && wrap > 0.0f) {
      wrap = WrapCurl(tables[f], radius, ball, &touch);
      if (touch) which = 2;
    }
    float hold = touch ? wrap : (wrap <= 0.0f ? 0.0f : Clamp01(game[f]));
    if (side && f == 0 && hold > game[0]) hold = Clamp01(game[0]);
    if (touches != nullptr) touches[f] = which;
    out->hi[f] = hold;
    if (f <= 1) {
      out->lo[f] = overlay_math::kHoldLoose * hold;
    } else {
      out->lo[f] = hold < overlay_math::kRestCurl ? hold
                                                  : overlay_math::kRestCurl;
    }
  }
}

// --- the pulse ----------------------------------------------------------------

// A PULSE WHEN A HAND FIRST MEETS THE LAND OR A BODY. One faint pulse when a
// finger of the hand is in the world and none was for kPulseQuietSeconds. So
// a hand that rests at the border of a surface does not buzz. `quiet` keeps
// the time with no contact.
const float kPulseQuietSeconds = 0.20f;

inline bool MeetPulse(bool touching, float dt, float* quiet) {
  if (!touching) {
    if (dt > 0.0f) *quiet += dt;
    if (*quiet > 10.0f) *quiet = 10.0f;
    return false;
  }
  const bool pulse = *quiet >= kPulseQuietSeconds;
  *quiet = 0.0f;
  return pulse;
}

// --- the idle wave ------------------------------------------------------------

// THE IDLE WAVE (hand_feel_idle, off). A hand at rest moves its fingers a
// little, each finger a part of a wave behind the finger before it. This is
// motion that the player did not make, so it is off unless the player turns
// it on. One wave takes kIdleSeconds and adds kIdleCurl at most.
const float kIdleSeconds = 4.0f;
const float kIdleCurl = 0.05f;

inline float IdlePhase(float phase, float dt) {
  if (!(dt > 0.0f)) return phase;
  phase += 6.2831853f * dt / kIdleSeconds;
  return phase - 6.2831853f * floorf(phase / 6.2831853f);
}

inline float IdleCurl(float phase, int finger) {
  return kIdleCurl * 0.5f * (1.0f - cosf(phase + 1.3f * finger));
}

}  // namespace hand_feel
