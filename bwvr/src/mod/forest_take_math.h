// The take from a forest: the rule, with no game state (docs/hand.md, "Many
// trees from a forest"). forest_take.cpp gives it the facts of the Hand and
// does what it asks. The math test runs it with no game.

#pragma once

#include <math.h>

#include "near_math.h"

namespace forest_take {

// What the Hand holds.
enum Held {
  kHeldNothing = 0,
  kHeldTree,    // one whole tree
  kHeldBundle,  // the bundle of wood that this take made
  kHeldOther
};

// THE RULE. The plain game gives one tree for each press, and the Hand holds
// one object. The mod adds a take:
//
//   Close the grip on a tree       The game puts the tree in the Hand, as
//                                  before.
//   Keep the grip closed           After wait_ms, when a second tree is in
//                                  reach, the take starts: the tree in the
//                                  Hand and that tree become one bundle of
//                                  wood. Then one more tree goes in the
//                                  bundle each step_ms, while a tree is in
//                                  reach.
//   Open the grip                  The take stops, and the bundle stays in
//                                  the Hand.
//   Close and open the grip again  The Hand lets the bundle go.
//
// A grip that opens before the take starts lets the one tree go, as before.
// A tree that the player lifts out of the forest stays one whole tree: when
// no second tree is in reach for leave_ms, the take is off for that tree. So
// a tree that the player carries to a different forest, to plant it there,
// does not start a take.
enum Phase {
  kIdle = 0,
  kArmed,    // one tree is in the closed grip, and the wait runs
  kTaking,   // the bundle is in the closed grip
  kCarried   // one tree is in the closed grip, and it left the forest
};

struct Facts {
  bool on;        // the take can run: the key is on, and the controller
                  // places the Hand
  bool grip;      // the grip is closed
  int held;       // Held
  bool next;      // a tree is in reach now
  double now_ms;
};

struct State {
  int phase;
  double since_ms;  // Armed: the tree came. Taking: the last tree went in
  double seen_ms;   // Armed: a second tree was last in reach
  int trees;        // the trees in the bundle
};

enum Act {
  kActNone = 0,
  kActStart,  // make the bundle from the tree in the Hand and the next tree
  kActAdd     // put the next tree in the bundle
};

struct Out {
  int act;
  bool runs;    // the take runs after this step: the grip collects, and its
                // opening keeps the bundle
  bool ended;   // the take ended on this step
};

inline void Reset(State* s) {
  s->phase = kIdle;
  s->since_ms = 0.0;
  s->seen_ms = 0.0;
  s->trees = 0;
}

// One step, once a frame. The caller does the act before the next step, and
// calls Failed when the game refused it.
inline Out Step(State* s, const Facts& f, int wait_ms, int step_ms,
                int leave_ms) {
  Out out = {kActNone, false, false};
  if (!f.on) {
    out.ended = s->phase == kTaking;
    Reset(s);
    return out;
  }
  switch (s->phase) {
    case kArmed:
      if (!f.grip || f.held != kHeldTree) {
        Reset(s);
        break;
      }
      if (f.next) s->seen_ms = f.now_ms;
      if (f.next && f.now_ms - s->since_ms >= wait_ms) {
        out.act = kActStart;
        s->phase = kTaking;
        s->since_ms = f.now_ms;
        s->trees = 2;
      } else if (!f.next && f.now_ms - s->seen_ms > leave_ms) {
        s->phase = kCarried;
      }
      break;
    case kCarried:
      if (!f.grip || f.held != kHeldTree) Reset(s);
      break;
    case kTaking:
      if (!f.grip || f.held != kHeldBundle) {
        out.ended = true;
        Reset(s);
      } else if (f.next && f.now_ms - s->since_ms >= step_ms) {
        out.act = kActAdd;
        s->since_ms = f.now_ms;
        ++s->trees;
      }
      break;
    default:
      if (f.grip && f.held == kHeldTree) {
        s->phase = kArmed;
        s->since_ms = f.now_ms;
        s->seen_ms = f.now_ms;
        s->trees = 1;
      }
      break;
  }
  out.runs = s->phase == kTaking;
  return out;
}

// The game refused the start: the Hand keeps its tree, and it stays one tree.
inline void Failed(State* s) {
  s->phase = kCarried;
  s->trees = 1;
}

// THE NEXT TREE. A tree is in reach of the Hand when its body is at most
// reach units from the Hand's point (near_math::TreeGap: the crown and the
// trunk). A take that started with a fetch from a distance has no hand in the
// forest, so its trees are the trees in the cone of the aim: at most cone_deg
// from the ray, with the radius of the tree counted. The nearest gap wins,
// and then the smallest angle.
struct Tree {
  float base[3];   // the point of the object
  float radius;    // the game's 2D radius
  float height;    // the game's height
};

inline float HandGap(const float point[3], const Tree& t) {
  return near_math::TreeGap(point, t.base, t.radius, t.height).gap;
}

// The angle from the ray to the tree, in degrees, less the angle of its
// radius. 0 when the ray goes through the tree. 180 for a tree behind the
// origin.
inline float AimAngle(const float origin[3], const float direction[3],
                      const Tree& t) {
  const float to[3] = {t.base[0] - origin[0],
                       t.base[1] + 0.5f * t.height - origin[1],
                       t.base[2] - origin[2]};
  const float length = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
  if (!(length > 1e-4f)) return 0.0f;
  float along =
      (to[0] * direction[0] + to[1] * direction[1] + to[2] * direction[2]) /
      length;
  if (along > 1.0f) along = 1.0f;
  if (along < -1.0f) along = -1.0f;
  const float angle = acosf(along) * 57.29578f;
  if (along <= 0.0f) return 180.0f;
  float ratio = t.radius / length;
  if (ratio > 1.0f) ratio = 1.0f;
  const float width = asinf(ratio > 0.0f ? ratio : 0.0f) * 57.29578f;
  return angle > width ? angle - width : 0.0f;
}

}  // namespace forest_take
