// The pick at the glow: the pure math. See docs/grab.md, "One point for each
// interaction". hand_direct.cpp uses it, and test/controller_math checks it
// (near_checks.h).
//
// THE POINT is the Hand's point, the tip of the real index finger, where the
// glow is. Each object that a press acts on is measured from that point. The
// nearest one inside the reach of the pick is the object of the press. While
// one of them is near the point, the grab from a distance is off.

#pragma once

#include <math.h>

namespace near_math {

// THE DEFAULTS, in centimeters of the room.
const int kPickCm = 4;      // hand_pick_cm: the reach of the pick from the point
const int kNearInCm = 10;   // far_grab_near_cm: the far grab goes off at this gap
const int kNearOutCm = 15;  // far_grab_near_out_cm: and comes back past this gap

// WHAT A PRESS OF THE GRIP DOES to an object. The larger kind wins a tie of
// two objects that are as near as each other (Beats).
enum Kind {
  kNone = 0,      // nothing
  kCreature = 1,  // the Creature: the game's own pick and its touch stay
  kTap = 2,       // the game taps it: a scroll, a house
  kTake = 3,      // the game takes from it: a pile, a field, a fish farm
  kHold = 4       // the Hand picks it up
};

// THE BODY of an object is an upright cylinder: its base is the object's
// point, its radius the game's 2D radius, and its height the game's height.
// gap is the distance from the point to the body, 0 inside it. middle is the
// distance from the point to the middle of the body.
struct Gap {
  float gap;
  float middle;
};

inline Gap BodyGap(const float point[3], const float base[3], float radius,
                   float height) {
  if (!(radius > 0.0f)) radius = 0.0f;
  if (!(height > 0.0f)) height = 0.0f;
  const float dx = point[0] - base[0];
  const float dz = point[2] - base[2];
  const float flat = sqrtf(dx * dx + dz * dz);
  float across = flat - radius;
  if (across < 0.0f) across = 0.0f;
  float up = 0.0f;
  if (point[1] < base[1]) up = base[1] - point[1];
  if (point[1] > base[1] + height) up = point[1] - (base[1] + height);
  const float dy = point[1] - (base[1] + 0.5f * height);
  Gap out;
  out.gap = sqrtf(across * across + up * up);
  out.middle = sqrtf(flat * flat + dy * dy);
  return out;
}

// THE CHOICE. True when the object a beats the object b: its body is nearer
// to the point by more than eps. When the two gaps agree to eps (the point is
// in both bodies, for example a villager who stands in a forest, or a tree
// beside a scroll), the object with the nearer middle wins. When the middles
// agree too, the larger kind wins.
inline bool Beats(const Gap& a, int kind_a, const Gap& b, int kind_b,
                  float eps) {
  if (a.gap < b.gap - eps) return true;
  if (a.gap > b.gap + eps) return false;
  if (a.middle < b.middle - eps) return true;
  if (a.middle > b.middle + eps) return false;
  return kind_a > kind_b;
}

// AN OBJECT THAT MOVES. The player closes the hand on the place where the
// object was a moment ago: the eye, the hand and the closing of the grip take
// some time, and the pick of a frame is one frame old. So the reach of the
// pick is longer for an object that moves, by its speed times that lag. A
// speed below kCatchStillCmps counts as still, so the noise of a draw gives
// nothing. The extra reach never passes the cap.
const int kCatchLagMs = 100;      // catch_lag_ms
const int kCatchCapCm = 8;        // catch_cap_cm
const int kCatchGraceMs = 600;    // catch_grace_ms
const int kCatchStillCmps = 15;   // an object slower than this is still

inline float ObjectSpeed(const float from[3], const float to[3], float seconds) {
  if (!(seconds > 1e-4f)) return 0.0f;
  const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
  const float speed = sqrtf(dx * dx + dy * dy + dz * dz) / seconds;
  return speed == speed ? speed : 0.0f;
}

// The extra reach in the units of the speed times seconds. still is the speed
// that counts as 0, cap the most.
inline float MovingExtra(float speed, float still, float lag_seconds,
                         float cap) {
  if (!(speed > still) || !(lag_seconds > 0.0f) || !(cap > 0.0f)) return 0.0f;
  const float extra = (speed - still) * lag_seconds;
  return extra > cap ? cap : extra;
}

// The object that the Hand let go just now gets no extra reach, so a throw
// does not come back to the hand in the same closing of the grip. since is the
// time from the release in seconds, grace the time that the guard holds.
inline bool JustLetGo(double since, double grace) {
  return since >= 0.0 && since < grace;
}

// THE BODY OF A SCROLL is a ball: the middle of the box of its mesh in the
// world, and the half diagonal of that box. The game's own pick has a scroll
// there (ScriptHighlight::Draw gives this point and this radius to
// SendInvisibleDrawCollision). A scroll floats and turns, so no upright
// cylinder from its point fits it.
inline Gap BallGap(const float point[3], const float center[3], float radius) {
  if (!(radius > 0.0f)) radius = 0.0f;
  const float dx = point[0] - center[0];
  const float dy = point[1] - center[1];
  const float dz = point[2] - center[2];
  Gap out;
  out.middle = sqrtf(dx * dx + dy * dy + dz * dz);
  out.gap = out.middle > radius ? out.middle - radius : 0.0f;
  return out;
}

// THE REACH OF A SCROLL. The player goes to a scroll on purpose, and the game
// puts a glow in front of it, toward the eye. So the pick reaches farther for
// a scroll than for each other object: scroll_reach_cm of the room from the
// ball, and never less than the reach of the pick. The default is the gap at
// which the grab from a distance goes off (kNearInCm): a hand that comes to a
// scroll has it as a far target to that gap, and as the target of the pick
// from that gap on, with no gap between the two.
const int kScrollReachCm = 10;

inline float ScrollReach(float pick_reach, float scroll_reach) {
  if (!(scroll_reach > pick_reach)) return pick_reach;
  return scroll_reach;
}

// THE DISTANCE FROM THE PLAYER (docs/god-view.md, "The player that a script sees"). A script of
// the game measures the distance on the map from the camera to a thing, to
// learn if the player came to look. The game measures with x and z only. In
// the God view the player is the head and each hand that is tracked: the
// least of their distances is the distance of the player. A hand at a thing
// is "the player came close". A test that asks if the player went away is
// true only when the head and the hands are all away.
struct PlayerPoints {
  bool head;
  float head_at[3];
  bool hand[3];         // the Hand's point, the left grip, the right grip
  float hand_at[3][3];
};

inline float MapDistance(const float a[3], const float b[3]) {
  const float dx = b[0] - a[0], dz = b[2] - a[2];
  return sqrtf(dx * dx + dz * dz);
}

// The least distance on the map from a point of the player to the thing, and
// which point gave it: 0 the head, 1 to 3 a hand. -1 with no point, and then
// the distance is not changed.
inline int PlayerDistance(const PlayerPoints& p, const float thing[3],
                          float* distance) {
  int which = -1;
  float best = 0.0f;
  if (p.head) {
    const float d = MapDistance(p.head_at, thing);
    if (d == d) {
      best = d;
      which = 0;
    }
  }
  for (int i = 0; i < 3; ++i) {
    if (!p.hand[i]) continue;
    const float d = MapDistance(p.hand_at[i], thing);
    if (!(d == d)) continue;
    if (which < 0 || d < best) {
      best = d;
      which = i + 1;
    }
  }
  if (which >= 0) *distance = best;
  return which;
}

// A test "distance < limit" changes its answer when the two distances are on
// two sides of the limit.
inline bool AnswerChanges(float game, float player, float limit) {
  return (game < limit) != (player < limit);
}

// WHEN THE PLAYER STANDS FOR THE CAMERA OF A SCRIPT. The place of the player
// is the answer only while the view of the player is at the place of the
// game camera. In each other case the script gets the game's own answer, as
// in the plain game:
// - latch: the God view did not own the last frame (the menu, the intro, the
//   first frames, a Temple room, a cut scene in its window).
// - view: the God view does not own the view now (a load, Ctrl F11).
// - owner: the camera mode on top is not a mode of the player now. A script
//   put a story camera there in this game turn. 0 is the player, 1 a story,
//   -1 not known.
// - bars: the cinema bars are up now.
// - game_moves: the game moves its own camera to a new place, and the view
//   goes there only when that camera is at rest.
inline bool PlayerIsCamera(bool latch, bool view, int owner, bool bars,
                           bool game_moves) {
  return latch && view && owner == 0 && !bars && !game_moves;
}

// THE VIEW OF THE HEAD, for a script that asks if the camera sees a point.
// basis has the columns right, up and forward of the head in the world, and
// inv_tan_x and inv_tan_y are 1 / tan of half of the view angle across and
// up, as the game keeps them for its own test. The game's test says yes when
// the point is in front of the camera and its picture is on the screen. This
// is the same test for the picture that the headset shows.
inline bool HeadSees(const float head[3], const float basis[9],
                     const float point[3], float inv_tan_x, float inv_tan_y) {
  const float d[3] = {point[0] - head[0], point[1] - head[1],
                      point[2] - head[2]};
  const float x = d[0] * basis[0] + d[1] * basis[3] + d[2] * basis[6];
  const float y = d[0] * basis[1] + d[1] * basis[4] + d[2] * basis[7];
  const float z = d[0] * basis[2] + d[1] * basis[5] + d[2] * basis[8];
  if (!(z > 0.0f) || !(inv_tan_x > 0.0f) || !(inv_tan_y > 0.0f)) return false;
  return fabsf(x) * inv_tan_x <= z && fabsf(y) * inv_tan_y <= z;
}

// The view angle of the test: the value of the game when it is sane, and
// 100 degrees when it is not (1 / tan of 50 degrees).
const float kHeadViewInvTan = 0.8390996f;

inline float HeadViewInvTan(float game) {
  return game > 0.2f && game < 10.0f ? game : kHeadViewInvTan;
}

// THE POINT OF THE FLAT HAND (docs/creature.md, "The open hand").
// The Hand acts from one point: the tip of the index finger. The flat hand of
// a stroke touches with its palm, so its point is the palm: the glow shows
// there, and the ray of the game's stroke goes through it. The palm of this
// frame is the tip of this frame and the offset from the tip to the palm of
// the frame before, so the point has no lag behind a hand that moves.
// offset_age: the frames since the offset was made. An old offset is not
// used, and the point stays at the tip.
const int kPalmOffsetFrames = 8;

inline bool PalmPoint(const float tip[3], const float offset[3],
                      int offset_age, bool flat_hand, bool key_on,
                      float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = tip[i];
  if (!flat_hand || !key_on) return false;
  if (offset_age < 0 || offset_age > kPalmOffsetFrames) return false;
  for (int i = 0; i < 3; ++i) {
    if (!(offset[i] == offset[i])) return false;
  }
  for (int i = 0; i < 3; ++i) out[i] = tip[i] + offset[i];
  return true;
}

// The offset from the tip to the palm. palm is the palm of the mod's hand of
// the frame before, and stood_on is the point that this hand stood on in that
// frame. That point must be the tip, and not the Hand's point: with the
// Hand's point at the palm, an offset from the Hand's point grows by one
// offset on each frame, and the point runs away.
inline void PalmOffset(const float palm[3], const float stood_on[3],
                       float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = palm[i] - stood_on[i];
}

// A PRESS ON A SCROLL (docs/village.md, "The scroll"). The game taps
// a scroll only while the scroll is active. The scroll of a challenge is not
// active until the player clicks it: a script of the game waits for the
// click, and then it makes the scroll active and starts the challenge. So a
// press on a scroll that the game draws and does not tap is a click for the
// scripts. taps: the game's own test of a tap says yes. drawn: the game drew
// the scroll in this frame. click_on: the key scroll_click, with one target
// and the scroll from its draw.
const int kScrollPressNone = 0;
const int kScrollPressTap = 1;
const int kScrollPressClick = 2;

inline int ScrollPress(bool taps, bool drawn, bool click_on) {
  if (taps) return kScrollPressTap;
  return drawn && click_on ? kScrollPressClick : kScrollPressNone;
}

// THE PLACE OF A POINT OF THE PICTURE (a desktop test). A point of the world
// shows at one pixel in the picture of each eye. From the two pixels and the
// two cameras the point is known again: each eye gives two planes through its
// camera, and the point is where the four planes meet, by least squares.
//
// m is the world to camera matrix of an eye pass as the glow uses it: the
// camera point is (x*m[0] + y*m[3] + z*m[6] + m[9], x*m[1] + y*m[4] + z*m[7] +
// m[10], x*m[2] + y*m[5] + z*m[8] + m[11]), and the pixel is ((1 + cx/cz) *
// half_x, (1 - cy/cz) * half_y). False when the two rays do not meet at one
// place (the same pixel in both eyes is a point at no distance).
inline bool PicturePoint(const float left[12], const float right[12],
                         float half_x, float half_y, float lx, float ly,
                         float rx, float ry, float out[3]) {
  if (!(half_x > 0.0f) || !(half_y > 0.0f)) return false;
  // The planes n.p = d, four of them.
  double n[4][3], d[4];
  const float* m[2] = {left, right};
  const float px[2] = {lx, rx}, py[2] = {ly, ry};
  for (int e = 0; e < 2; ++e) {
    const double a = px[e] / half_x - 1.0;
    const double b = 1.0 - py[e] / half_y;
    for (int k = 0; k < 3; ++k) {
      n[2 * e][k] = m[e][3 * k] - a * m[e][3 * k + 2];
      n[2 * e + 1][k] = m[e][3 * k + 1] - b * m[e][3 * k + 2];
    }
    d[2 * e] = a * m[e][11] - m[e][9];
    d[2 * e + 1] = b * m[e][11] - m[e][10];
  }
  // The normal equations, and Cramer's rule.
  double s[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, t[3] = {0, 0, 0};
  for (int i = 0; i < 4; ++i) {
    for (int r = 0; r < 3; ++r) {
      t[r] += n[i][r] * d[i];
      for (int c = 0; c < 3; ++c) s[r][c] += n[i][r] * n[i][c];
    }
  }
  const double det =
      s[0][0] * (s[1][1] * s[2][2] - s[1][2] * s[2][1]) -
      s[0][1] * (s[1][0] * s[2][2] - s[1][2] * s[2][0]) +
      s[0][2] * (s[1][0] * s[2][1] - s[1][1] * s[2][0]);
  const double size = s[0][0] + s[1][1] + s[2][2];
  if (!(fabs(det) > 1e-9 * size * size * size)) return false;
  const double x =
      (t[0] * (s[1][1] * s[2][2] - s[1][2] * s[2][1]) -
       s[0][1] * (t[1] * s[2][2] - s[1][2] * t[2]) +
       s[0][2] * (t[1] * s[2][1] - s[1][1] * t[2])) / det;
  const double y =
      (s[0][0] * (t[1] * s[2][2] - s[1][2] * t[2]) -
       t[0] * (s[1][0] * s[2][2] - s[1][2] * s[2][0]) +
       s[0][2] * (s[1][0] * t[2] - t[1] * s[2][0])) / det;
  const double z =
      (s[0][0] * (s[1][1] * t[2] - t[1] * s[2][1]) -
       s[0][1] * (s[1][0] * t[2] - t[1] * s[2][0]) +
       t[0] * (s[1][0] * s[2][1] - s[1][1] * s[2][0])) / det;
  out[0] = static_cast<float>(x);
  out[1] = static_cast<float>(y);
  out[2] = static_cast<float>(z);
  // The point must be in front of each camera.
  for (int e = 0; e < 2; ++e) {
    const double cz = x * m[e][2] + y * m[e][5] + z * m[e][8] + m[e][11];
    if (!(cz > 0.0)) return false;
  }
  return true;
}

// THE BODY OF A TREE. The cylinder of a tree is as wide as its crown from the
// land to the top, so most of it is air beside the trunk. The player sees a
// trunk and a round crown. So the body is two parts, and the nearer one
// counts:
//   the crown: the upright spheroid in the box of the mesh. Its middle is
//     half the height over the base, its radius is the game's 2D radius, and
//     its half height is half the game's height.
//   the trunk: a cylinder of kTrunkShare of the radius, from the base to the
//     middle.
// The distance from a point to a spheroid has no closed form. SpheroidGap
// finds the nearest point of the ellipse in the plane of the axis and the
// point, in four steps (the test compares it with a search).
const float kTrunkShare = 0.25f;

inline float EllipseGap(float px, float py, float a, float b) {
  px = fabsf(px);
  py = fabsf(py);
  if (!(a > 1e-6f) || !(b > 1e-6f)) {
    // A line or a point.
    const float ox = px > a ? px - (a > 0.0f ? a : 0.0f) : 0.0f;
    const float oy = py > b ? py - (b > 0.0f ? b : 0.0f) : 0.0f;
    return sqrtf(ox * ox + oy * oy);
  }
  const float inside = (px / a) * (px / a) + (py / b) * (py / b);
  if (inside <= 1.0f) return 0.0f;
  float tx = 0.70710678f, ty = 0.70710678f;
  for (int i = 0; i < 4; ++i) {
    const float x = a * tx, y = b * ty;
    const float ex = (a * a - b * b) * tx * tx * tx / a;
    const float ey = (b * b - a * a) * ty * ty * ty / b;
    const float rx = x - ex, ry = y - ey;
    const float qx = px - ex, qy = py - ey;
    const float r = sqrtf(rx * rx + ry * ry);
    const float q = sqrtf(qx * qx + qy * qy);
    if (!(q > 1e-9f)) break;
    tx = (qx * r / q + ex) / a;
    ty = (qy * r / q + ey) / b;
    if (tx < 0.0f) tx = 0.0f;
    if (ty < 0.0f) ty = 0.0f;
    if (tx > 1.0f) tx = 1.0f;
    if (ty > 1.0f) ty = 1.0f;
    const float t = sqrtf(tx * tx + ty * ty);
    if (!(t > 1e-9f)) break;
    tx /= t;
    ty /= t;
  }
  const float dx = px - a * tx, dy = py - b * ty;
  return sqrtf(dx * dx + dy * dy);
}

inline Gap SpheroidGap(const float point[3], const float center[3],
                       float radius, float half_height) {
  const float dx = point[0] - center[0];
  const float dy = point[1] - center[1];
  const float dz = point[2] - center[2];
  const float flat = sqrtf(dx * dx + dz * dz);
  Gap out;
  out.gap = EllipseGap(flat, dy, radius, half_height);
  out.middle = sqrtf(flat * flat + dy * dy);
  return out;
}

inline Gap TreeGap(const float point[3], const float base[3], float radius,
                   float height) {
  if (!(radius > 0.0f)) radius = 0.0f;
  if (!(height > 0.0f)) height = 0.0f;
  const float center[3] = {base[0], base[1] + 0.5f * height, base[2]};
  Gap out = SpheroidGap(point, center, radius, 0.5f * height);
  const Gap trunk = BodyGap(point, base, kTrunkShare * radius, 0.5f * height);
  if (trunk.gap < out.gap) out.gap = trunk.gap;
  return out;
}

// THE TARGET OF A PRESS (the rule of near_rule=2). One of the bodies in the
// reach of the pick is the target of the grip. The player must know which one
// before the press, so the rule is short:
//
//   1. A body that the point touches wins, when it is not a loose body. The
//      point touches a body when the gap is kTouchCm of the room or less. A
//      loose body is a tree, a forest, a building or a bulk source: it is
//      large, and its body is wider than what the player sees.
//   2. Then a scroll in the reach. A scroll is small, it floats, and the
//      player goes to it on purpose. Trees and houses stand all around it.
//   3. Then a loose body that the point touches.
//   4. Then the nearest body.
//
// Of two bodies of rule 1, and of two of rule 3, the smaller one wins: a
// villager at the Creature, a small tree under a large one. The size is the
// larger of the radius and half the height, and it counts in steps of a
// factor of 2 (SizeClass). In one step the nearer middle wins, so two trees
// with crowns that meet share the room between them.
//
// A scroll in the reach must not win over each body: a Creature that stands
// 4 cm of the room from a scroll would lose a grip at the Creature to a tap
// on the scroll.
//
// Each rule gives a full order, so the choice does not depend on the order of
// the candidates.
const int kTouchCm = 1;  // hand_touch_cm

//
//   5. Last, a scroll that is past the reach of the pick and in the longer
//      reach of a scroll (ScrollReach). It is the target only when no other
//      body is in the reach of the pick, so the longer reach takes no tree
//      and no house from the Hand.
struct Target {
  Gap gap;
  int kind;     // Kind
  float size;   // world units: the larger of the radius and half the height
  bool scroll;
  bool loose;   // a tree, a forest, a building or a bulk source
  bool outer;   // a scroll past the reach of the pick, in its longer reach
};

// The rule that a target comes under, 1 to 5. The lower one wins.
inline int TargetRule(const Target& t, float touch) {
  const bool touched = t.gap.gap <= touch;
  if (touched && !t.loose) return 1;
  if (t.scroll) return t.outer ? 5 : 2;
  return touched ? 3 : 4;
}

inline int SizeClass(float size) {
  if (!(size > 0.01f)) size = 0.01f;
  return static_cast<int>(floorf(logf(size) / 0.69314718f));
}

inline bool BeatsTarget(const Target& a, const Target& b, float touch,
                        float eps) {
  const int a_rule = TargetRule(a, touch), b_rule = TargetRule(b, touch);
  if (a_rule != b_rule) return a_rule < b_rule;
  if (a_rule == 1 || a_rule == 3) {
    const int a_class = SizeClass(a.size), b_class = SizeClass(b.size);
    if (a_class != b_class) return a_class < b_class;
    if (a.gap.middle < b.gap.middle - eps) return true;
    if (a.gap.middle > b.gap.middle + eps) return false;
    if (a.size < b.size - eps) return true;
    if (a.size > b.size + eps) return false;
    return a.kind > b.kind;
  }
  if (a.gap.gap < b.gap.gap - eps) return true;
  if (a.gap.gap > b.gap.gap + eps) return false;
  if (a.gap.middle < b.gap.middle - eps) return true;
  if (a.gap.middle > b.gap.middle + eps) return false;
  if (a.size < b.size - eps) return true;
  if (a.size > b.size + eps) return false;
  return a.kind > b.kind;
}

// THE DISTANCE OF THE CHOICE IN THE PICK RECORD. The game keeps the object of
// the next pick record only when its distance is less than the distance of
// the land under the pointer, or when that land point is in the circle of the
// object (GInterface::UpdateInterfaceCollide 0x005D5A70). The mod's choice is
// at the Hand's point, and the land under the pointer can be nearer to the
// camera than the point of the object: a tree on a slope, with the Hand at
// its crown. The game then took the choice out of the record, and a press did
// nothing. So the distance of each choice is at most half of the land
// distance.
inline float RecordDistance(float distance, float land_distance) {
  if (land_distance > 0.01f && land_distance < 1e30f &&
      !(distance < land_distance)) {
    return 0.5f * land_distance;
  }
  return distance;
}

// THE HALO OF THE TARGET AT THE HAND. The halo of a far target is a round
// disc behind the object. A tree at the Hand is five times as tall as it is
// wide, so its halo is an ellipse along the image of the upright axis of the
// body. axis_x and axis_y are the image of half the height of the body, from
// its middle to its top, in pixels. radius_px is the image of the radius of
// the halo. grow is the share by which the halo is larger than the body.
// From straight above the image of the axis is a point, and the halo is
// round.
struct HaloShape {
  float across;  // the half axis across the body, in pixels
  float along;   // the half axis along the body
  float tilt_x;  // the direction of the long axis on the screen, a unit
  float tilt_y;
};

inline HaloShape HaloEllipse(float axis_x, float axis_y, float radius_px,
                             float grow) {
  HaloShape out = {radius_px, radius_px, 0.0f, 1.0f};
  const float axis = sqrtf(axis_x * axis_x + axis_y * axis_y);
  if (!(axis > 0.5f) || !(radius_px > 0.0f)) return out;
  out.along = sqrtf(grow * axis * grow * axis + radius_px * radius_px);
  out.tilt_x = axis_x / axis;
  out.tilt_y = axis_y / axis;
  return out;
}

// THE HAND OF A STROKE (docs/creature.md, "The open hand"). While
// the Hand strokes the Creature, the mod's hand is open. Against a Creature
// that is large for the hand it is the flat hand, and the palm touches.
// Against a small one it is the index finger, and the tip touches. ratio is
// the height of the body over the length of the hand. Between the two limits
// the answer of the last frame stays, so a zoom step at the border does not
// change the hand back and forth. A stroke that starts between the limits
// takes the hand of the nearer limit (StrokeFingerAtStart).
//
// The limits are percent of the hand's length (creature_finger_below_pct and
// creature_flat_above_pct). They are 60 and 90, so a stroke that starts on a
// Creature that is three quarters of the hand or more starts with the flat
// hand.
const int kStrokeFingerBelowPct = 60;
const int kStrokeFlatAbovePct = 90;
const int kStrokeFingerBelowPctBefore = 80;
const int kStrokeFlatAbovePctBefore = 120;

inline bool StrokeFinger(float ratio, bool was_finger, float finger_below,
                         float flat_above) {
  if (!(ratio == ratio)) return was_finger;
  if (ratio < finger_below) return true;
  if (ratio > flat_above) return false;
  return was_finger;
}

// The hand at the start of a stroke: the finger below the middle of the two
// limits, and the flat hand from the middle on.
inline bool StrokeFingerAtStart(float ratio, float finger_below,
                                float flat_above) {
  if (!(ratio == ratio)) return true;
  return ratio < 0.5f * (finger_below + flat_above);
}

// The two limits from the ini values, in percent. A limit below 10 or above
// 500 is not sane, and the flat limit is never below the finger limit.
inline void StrokeLimits(int finger_pct, int flat_pct, float* finger_below,
                         float* flat_above) {
  if (finger_pct < 10) finger_pct = 10;
  if (finger_pct > 500) finger_pct = 500;
  if (flat_pct < finger_pct) flat_pct = finger_pct;
  if (flat_pct > 500) flat_pct = 500;
  *finger_below = finger_pct * 0.01f;
  *flat_above = flat_pct * 0.01f;
}

// THE TICKLE. The fingers move a little as the hand moves over the body. The
// wave goes on only with the path of the real hand: kTickleCm of path is one
// wave, and a hand that is still has still fingers. Each finger is a part of
// a wave behind the finger before it. The wave only closes a finger, by
// kTickleCurl at most.
const float kTickleCm = 5.0f;
const float kTickleCurl = 0.12f;

inline float TicklePhase(float phase, float moved_cm) {
  if (!(moved_cm > 0.0f)) return phase;
  // A jump of the tracking is no stroke.
  if (moved_cm > 10.0f) moved_cm = 10.0f;
  phase += 6.2831853f * moved_cm / kTickleCm;
  return phase - 6.2831853f * floorf(phase / 6.2831853f);
}

inline float TickleCurl(float phase, int finger) {
  return kTickleCurl * 0.5f * (1.0f - cosf(phase + 1.3f * finger));
}

// THE SPEED OF A SLAP is the speed of the part of the hand that hits. A slap
// with the arm moves the grip, and a slap with the wrist (a backhand) moves
// the finger tips and keeps the grip almost still. So the speed is the larger
// of the two.
inline float SlapSpeed(float grip_mps, float tip_mps) {
  if (!(grip_mps > 0.0f)) grip_mps = 0.0f;
  if (!(tip_mps > 0.0f)) tip_mps = 0.0f;
  return grip_mps > tip_mps ? grip_mps : tip_mps;
}

// THE LEASH CLICK (docs/creature.md, "The leash from a distance"). What a click
// on the Creature does to the leash, as the game's handler of packet 0x5F
// has it (fn_005D06E0 and fn_005E7140) [V]:
//
//   mine      the Creature is the Creature of the local player
//   ready     the Creature lives and can have a leash now (Creature+0x1110)
//   other     the leash of another player is on it
//   on        the leash of the local player is on (GLeashStatus+0x14)
//   tied      and that leash is tied to an object (GLeashStatus+0x24)
//
// The answer is the kind of the click: kLeashNone when the game does nothing,
// kLeashPut when the leash goes on, kLeashOff when it comes off, and
// kLeashBack when it comes back from the object to the Hand.
enum LeashKind { kLeashNone = 0, kLeashPut = 1, kLeashOff = 2, kLeashBack = 3 };

inline int LeashClick(bool mine, bool ready, bool other, bool on, bool tied) {
  if (!mine || !ready || other) return kLeashNone;
  if (tied) return kLeashBack;
  return on ? kLeashOff : kLeashPut;
}

// THE BODY BONE BEFORE A PRESS. The game keeps the body bone of a Creature in
// the bone buffer of its LH3DCreature at each time, and the mod reads it for
// the capsule of the choice. The bone of a Creature that the game does not
// animate can be old. The bone counts when it is not more than the height of
// the Creature (S = 15 * size) and 2 units from the place of the Creature on
// the map, in x and z. dx and dz are the bone less that place.
inline bool BoneSane(float dx, float dz, float size) {
  if (!(dx == dx) || !(dz == dz) || !(size > 0.0f)) return false;
  const float reach = 15.0f * size + 2.0f;
  return dx * dx + dz * dz <= reach * reach;
}

// THE WIDE PICK. Between the reach of the pick (hand_pick_cm) and the gap at
// which the grab from a distance comes back, an object was no target: the pick
// did not reach it, and the object itself kept the far grab off. So a holdable
// object past the reach of the pick and inside the wide reach
// (hand_wide_pick_cm, 0 is off) is the target when no object that a press acts
// on is nearer than it (nearest is the least gap of them, the Creature too).
// The wide reach is at least the gap at which the far grab comes back, so each
// gap has a target. An object inside the reach of the pick is chosen by the
// plain rules, and wins over a wide one.
const int kWideCm = 15;  // hand_wide_pick_cm

inline bool WidePick(float gap, float reach, float wide_reach, float nearest,
                     float eps) {
  if (!(wide_reach > reach)) return false;
  return gap >= reach && gap < wide_reach && gap <= nearest + eps;
}

// NEAR OBJECTS WIN. The grab from a distance is off while an object that a
// press acts on is near the point. It goes off when the gap is in_gap or
// less, and it comes back when the gap is more than out_gap. Between the two
// the answer is the answer of the last frame, so a hand that rests at the
// border does not turn the far target on and off.
inline bool NearBlocks(float gap, bool blocked, float in_gap, float out_gap) {
  if (!(gap == gap)) return false;
  if (out_gap < in_gap) out_gap = in_gap;
  if (gap <= in_gap) return true;
  if (gap > out_gap) return false;
  return blocked;
}

// NO BAND WITH NO TARGET. The least distance of a far target from the Hand's
// point, to the object's point, in meters of the room. The cone has its own
// least distance, cone_min_m (35 cm). With the rule of the near objects it is
// the distance at which the far grab comes back, out_cm, and never more than
// the cone's. So an object is a target of the Hand, or it turns the far grab
// off, or it is a target of the aim.
inline float FarLeastMeters(bool near_rule, float out_cm, float cone_min_m) {
  if (!near_rule) return cone_min_m;
  const float out = out_cm > 0.0f ? out_cm / 100.0f : 0.0f;
  return out < cone_min_m ? out : cone_min_m;
}

// Centimeters of the room in world units, at the scale now.
inline float CmToUnits(float cm, float units_per_meter) {
  if (!(units_per_meter > 0.0f) || !(cm > 0.0f)) return 0.0f;
  return cm * units_per_meter / 100.0f;
}

// THE WAIT OF A FAR TOUCH. A scroll that the aim ray selects takes one press
// of the grip, with no pull. The press stays down for this many frames, so
// one step of the interface reads it, and then the far grab lets it go.
const int kTouchFrames = 8;
// A FOREST gives a new tree after the game's wait of 225 ms and one game
// turn. A fetch from a forest waits this many frames for an object in the
// Hand, in place of the 40 frames of a plain fetch.
const int kForestFetchFrames = 90;

}  // namespace near_math
