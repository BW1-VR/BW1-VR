// The math of the tabletop mode and of the God view, as pure functions with
// no game state.
//
// tabletop.cpp calls these with the numbers it reads from the game, and
// test/controller_math calls them with numbers it makes up. Nothing here reads
// a global or touches the game. See docs/tabletop.md, "The table camera". RayLand
// asks for the land height through the function that the caller gives it.
//
// Every 3x3 matrix is m[r * 3 + c], and a vector is a column: out = m * in.
// The columns of a basis are its right, its up and its forward, in world
// space. This is the convention head_camera.cpp uses for the room basis L.
//
// The room is the frame of head_pose.cpp: meters, x right, y up, z forward,
// with the origin at the recenter pose. The table maps the room to the world
// with one similarity:
//
//     world = C + s * Lt * (room - Tc)
//
// C is the table center in the world, s the table scale in units per meter,
// Lt the table basis, and Tc the table center in the room.

#pragma once

#include <math.h>

namespace table_math {

// The table basis Lt: world up, and the room forward along the heading psi
// in radians. A heading of 0 is world +z, and a heading of 90 degrees is
// world +x.
inline void Basis(float psi, float out[9]) {
  const float c = cosf(psi), s = sinf(psi);
  const float right[3] = {c, 0.0f, -s};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  const float forward[3] = {s, 0.0f, c};
  for (int i = 0; i < 3; ++i) {
    out[i * 3 + 0] = right[i];
    out[i * 3 + 1] = up[i];
    out[i * 3 + 2] = forward[i];
  }
}

// The heading of a camera from its position to its focus, in radians. False
// when the camera looks straight up or down.
inline bool Heading(const float from[3], const float to[3], float* psi) {
  const float dx = to[0] - from[0];
  const float dz = to[2] - from[2];
  if (dx * dx + dz * dz < 1e-6f) return false;
  *psi = atan2f(dx, dz);
  return true;
}

// basis * v, added to base.
inline void Along(const float base[3], const float basis[9], const float v[3],
                  float out[3]) {
  for (int i = 0; i < 3; ++i) {
    out[i] = base[i] + basis[i * 3 + 0] * v[0] + basis[i * 3 + 1] * v[1] +
             basis[i * 3 + 2] * v[2];
  }
}

// A point of the room in the world: C + s * Lt * (room - Tc).
inline void RoomToWorld(const float center[3], const float basis[9],
                        float scale, const float table_room[3],
                        const float room[3], float out[3]) {
  const float d[3] = {(room[0] - table_room[0]) * scale,
                      (room[1] - table_room[1]) * scale,
                      (room[2] - table_room[2]) * scale};
  Along(center, basis, d, out);
}

// Where the recenter pose lands in the world: P0 = C - s * Lt * Tc.
inline void Origin(const float center[3], const float basis[9], float scale,
                   const float table_room[3], float out[3]) {
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  RoomToWorld(center, basis, scale, table_room, zero, out);
}

// The render head: E = P0 + Lt * t. The lean t is the head's offset from the
// recenter pose in world units at the table scale. head_pose.cpp gives the
// lean at the life size scale, so the caller multiplies it by LeanGain first.
inline void Eye(const float origin[3], const float basis[9],
                const float lean[3], float out[3]) {
  Along(origin, basis, lean, out);
}

// What the life size lean is multiplied by to give the lean at the table
// scale.
inline float LeanGain(float table_scale, float life_scale) {
  return life_scale > 0.0f ? table_scale / life_scale : 1.0f;
}

// The scale that fits a land on a table. far_radius is the distance from the
// table center to the farthest land cell, in units. The land then fills the
// table with a margin at the edge, for example 0.1 for 10 percent.
inline float FitScale(float far_radius, float diameter_m, float margin) {
  if (!(diameter_m > 0.0f)) return 0.0f;
  return 2.0f * (1.0f + margin) * far_radius / diameter_m;
}

// The radius of the table in the world, in units. Nothing past it is drawn.
inline float CutRadius(float scale, float diameter_m) {
  return 0.5f * scale * diameter_m;
}

// The distance from the center to the nearest point of a square block, and
// to its middle. The block's corner is at (x0, z0).
inline float BlockNearest(float x0, float z0, float size,
                          const float center[3]) {
  float dx = 0.0f, dz = 0.0f;
  if (center[0] < x0) dx = x0 - center[0];
  if (center[0] > x0 + size) dx = center[0] - (x0 + size);
  if (center[2] < z0) dz = z0 - center[2];
  if (center[2] > z0 + size) dz = center[2] - (z0 + size);
  return sqrtf(dx * dx + dz * dz);
}

inline float BlockMiddle(float x0, float z0, float size,
                         const float center[3]) {
  const float dx = x0 + 0.5f * size - center[0];
  const float dz = z0 + 0.5f * size - center[2];
  return sqrtf(dx * dx + dz * dz);
}

// Whether a block goes at the edge of the table. A block with land stays
// while any part of it is inside the radius. A block with only sea goes when
// its middle is outside, so the edge of the land is ragged by half a block at
// most, not by a whole block.
inline bool BlockCut(float x0, float z0, float size, const float center[3],
                     float radius, bool has_land) {
  if (BlockNearest(x0, z0, size, center) > radius) return true;
  if (!has_land && BlockMiddle(x0, z0, size, center) > radius) return true;
  return false;
}

// THE SEA AS A DISC.
//
// The plain sea of the game is one quad from (-70000, 0, 70000) to
// (70000, 0, -70000). The texture repeats every 2800 units, and the corner
// (-70000, 0, 70000) has the texture point (s0, t0). The disc keeps that
// mapping, so it looks the same as the quad.
const float kSeaHalf = 70000.0f;
const float kSeaRepeat = 2800.0f;
const int kFanSegments = 64;
// The center, then one point on the rim for each segment.
const int kFanVertices = kFanSegments + 1;

inline void SeaUv(float x, float z, float s0, float t0, float* u, float* v) {
  *u = s0 + (x + kSeaHalf) / kSeaRepeat;
  *v = t0 + (kSeaHalf - z) / kSeaRepeat;
}

// The positions and the triangles of a disc on the sea plane. The rim goes
// round in the same direction as the corners of the game's quad, so the
// triangles face the same way. Rim point i is at the angle 2 pi i / N from
// world +z toward world +x.
inline void SeaFan(const float center[3], float radius,
                   float positions[kFanVertices][3],
                   unsigned indices[kFanSegments][3]) {
  positions[0][0] = center[0];
  positions[0][1] = 0.0f;
  positions[0][2] = center[2];
  for (int i = 0; i < kFanSegments; ++i) {
    const float a = 6.28318531f * static_cast<float>(i) /
                    static_cast<float>(kFanSegments);
    positions[i + 1][0] = center[0] + radius * sinf(a);
    positions[i + 1][1] = 0.0f;
    positions[i + 1][2] = center[2] + radius * cosf(a);
  }
  for (int i = 0; i < kFanSegments; ++i) {
    indices[i][0] = 0;
    indices[i][1] = static_cast<unsigned>(i + 1);
    indices[i][2] = static_cast<unsigned>(i + 1 < kFanSegments ? i + 2 : 1);
  }
}

// THE KEY COLOR OUTSIDE THE TABLE.
//
// The sea is not opaque. It shows what is behind it, which in the game is
// the sky. So the sky stays behind the sea, and the key color fills the
// screen outside the outline of the sea disc, before the land draws. These
// find that outline on the screen of one eye.

// A world point in the camera of the eye, from the scaled world to camera
// matrix at 0x00EA9E40: 12 floats, p_camera = p_world * R + t, as the
// triangle submit fn_0081C090 does it.
inline void ToCamera(const float m[12], const float p[3], float out[3]) {
  out[0] = p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9];
  out[1] = p[0] * m[1] + p[1] * m[4] + p[2] * m[7] + m[10];
  out[2] = p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11];
}

// The part of a convex polygon in camera space that is in front of the near
// plane. out needs room for n + 1 points. Returns the count of points.
inline int ClipNear(const float in[][3], int n, float near_z,
                    float out[][3]) {
  int count = 0;
  for (int i = 0; i < n; ++i) {
    const float* a = in[i];
    const float* b = in[(i + 1) % n];
    const bool a_in = a[2] >= near_z;
    const bool b_in = b[2] >= near_z;
    if (a_in) {
      for (int k = 0; k < 3; ++k) out[count][k] = a[k];
      ++count;
    }
    if (a_in != b_in) {
      const float t = (near_z - a[2]) / (b[2] - a[2]);
      for (int k = 0; k < 3; ++k) out[count][k] = a[k] + t * (b[k] - a[k]);
      ++count;
    }
  }
  return count;
}

// A camera point on the screen, as the game projects it:
// x = (1 + cx / cz) * hx and y = (1 - cy / cz) * hy.
inline void ToScreen(const float c[3], float hx, float hy, float out[2]) {
  out[0] = (1.0f + c[0] / c[2]) * hx;
  out[1] = (1.0f - c[1] / c[2]) * hy;
}

// Where a convex screen polygon crosses the middle line of each pixel row,
// for the rows first to first + rows - 1. The middle of row y is y + 0.5.
// Direct3D draws a pixel when its middle is inside a triangle, so the sea
// fills exactly the pixels between left and right, and the key takes the
// rest. A row that the polygon does not cross keeps left above right.
inline void RowSpans(const float points[][2], int n, int first, int rows,
                     float* left, float* right) {
  for (int i = 0; i < rows; ++i) {
    left[i] = 1e30f;
    right[i] = -1e30f;
  }
  for (int i = 0; i < n; ++i) {
    const float* a = points[i];
    const float* b = points[(i + 1) % n];
    if (a[1] == b[1]) continue;
    const float low = a[1] < b[1] ? a[1] : b[1];
    const float high = a[1] < b[1] ? b[1] : a[1];
    // The rows whose middle is in [low, high].
    int r0 = static_cast<int>(ceilf(low - 0.5f)) - first;
    int r1 = static_cast<int>(floorf(high - 0.5f)) - first;
    if (r1 < 0 || r0 > rows - 1) continue;
    if (r0 < 0) r0 = 0;
    if (r1 > rows - 1) r1 = rows - 1;
    const float slope = (b[0] - a[0]) / (b[1] - a[1]);
    for (int r = r0; r <= r1; ++r) {
      const float y = static_cast<float>(first + r) + 0.5f;
      const float x = a[0] + (y - a[1]) * slope;
      if (x < left[r]) left[r] = x;
      if (x > right[r]) right[r] = x;
    }
  }
}

// The pixels of a row outside [left, right]: the pixels before KeyBefore,
// and the pixels from KeyFrom on. A pixel is outside when its middle is. A
// middle on the right edge is outside, as Direct3D does not draw it.
inline float KeyBefore(float left) { return ceilf(left - 0.5f); }
inline float KeyFrom(float right) { return ceilf(right - 0.5f); }

// The facing of a triangle on the sea plane: the y part of (b - a) x (c - a).
// Positive for the triangles of the game's own quad.
inline float Facing(const float a[3], const float b[3], const float c[3]) {
  const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
  const float w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
  return u[2] * w[0] - u[0] * w[2];
}

// THE HAND ON THE TABLE (docs/tabletop.md, "The hand on the table").
//
// The game's Hand at size 1 is 3.2 units long (fn_0046C040). The size that
// gives a Hand of length_m in the room at a table scale of s units per meter.
const float kHandLength = 3.2f;

inline float HandSize(float length_m, float scale) {
  return length_m * scale / kHandLength;
}

// THE GRAB OF THE TABLE (docs/tabletop.md, "Moving the table by the grips").
//
// The table top stays where it is in the room. The land slides, turns and
// scales on it. A grab keeps the land point under the middle of the hands at
// the press under that middle. It solves the table from the press every time
// and never adds steps, as the grab of the world does (controller_math.h,
// GrabSolve). Only the hands go in, so a head that moves with still hands
// moves nothing, and a second solve in one frame gives the same table.
struct TablePose {
  float center[3];  // C, the land point at the table center, y 0
  float heading;    // psi, radians
  float scale;      // s, units per meter
};

// What a grab of the table keeps from its press. The room points are in
// meters, in the frame of head_pose.cpp.
struct TableGrab {
  int hands;        // 1 or 2
  TablePose pose;   // the table at the press
  float middle[3];  // the grip, or the middle of the two grips
  float spread[3];  // two hands: one grip minus the other
};

inline float Wrap(float angle) {
  const float pi = 3.14159265f;
  while (angle > pi) angle -= 2.0f * pi;
  while (angle < -pi) angle += 2.0f * pi;
  return angle;
}

// The land point under a room point: the table map with the height left out.
// The land slides on the table, so a hand that moves up or down moves no
// land.
inline void FlatWorld(const TablePose& pose, const float table_room[3],
                      const float room[3], float out[3]) {
  float basis[9];
  Basis(pose.heading, basis);
  const float d[3] = {(room[0] - table_room[0]) * pose.scale, 0.0f,
                      (room[2] - table_room[2]) * pose.scale};
  Along(pose.center, basis, d, out);
  out[1] = pose.center[1];
}

// The heading of a room vector about up: 0 is forward, 90 degrees is right.
inline float FlatHeading(const float v[3]) { return atan2f(v[0], v[2]); }

// One solve of the table from the hands now. middle and spread are the same
// kind of points as at the press, in room meters.
//
//   theta = the change of the heading of the spread, about up
//   k     = |spread| / |spread at the press|, kept between 0.25 and 4
//   psi   = psi_a - theta           the land turns with the hands
//   s     = s_a / k                 hands apart make the land larger
//   C     = W - s * Lt(psi) * (m - Tc), with the height left out
//
// W is the land point under the middle at the press, so the same land point
// stays under the middle of the hands. s is kept between low and high, and W
// stays under the middle at the limit too. With one hand, theta is 0 and k is
// 1, so C = C_a - s_a * Lt_a * (m - m_a) with the height left out.
//
// With vertical set (the God view), one hand also moves the land up and down
// one to one: C.y = C_a.y - s * (m.y - m_a.y). A hand that moves up raises
// the land in the room by the same distance. The height is from the press
// too, so a still hand moves nothing. Two hands keep the height of the press.
// GodGroundHeight keeps the result inside the ground limits.
//
// theta and k hold their last values (the ones passed in) when the turn or
// the zoom is off, or when a spread is below min_spread, where its heading is
// noise. So they freeze, and they never jump.
inline void TableGrabSolve(const TableGrab& a, const float table_room[3],
                           const float middle[3], const float spread[3],
                           bool turn, bool zoom, float min_spread, float low,
                           float high, float* theta_io, float* k_io,
                           TablePose* out, bool vertical = false) {
  float theta = *theta_io;
  float k = *k_io;
  if (a.hands == 2) {
    const float flat_a = sqrtf(a.spread[0] * a.spread[0] +
                               a.spread[2] * a.spread[2]);
    const float flat = sqrtf(spread[0] * spread[0] + spread[2] * spread[2]);
    if (turn && flat_a >= min_spread && flat >= min_spread) {
      theta = Wrap(FlatHeading(spread) - FlatHeading(a.spread));
    }
    const float length_a =
        sqrtf(a.spread[0] * a.spread[0] + a.spread[1] * a.spread[1] +
              a.spread[2] * a.spread[2]);
    const float length = sqrtf(spread[0] * spread[0] + spread[1] * spread[1] +
                               spread[2] * spread[2]);
    if (zoom && length_a >= min_spread && length >= min_spread) {
      k = length / length_a;
      if (k < 0.25f) k = 0.25f;
      if (k > 4.0f) k = 4.0f;
    }
  } else {
    theta = 0.0f;
    k = 1.0f;
  }
  *theta_io = theta;
  *k_io = k;

  float grabbed[3];
  FlatWorld(a.pose, table_room, a.middle, grabbed);
  out->heading = Wrap(a.pose.heading - theta);
  float scale = a.pose.scale / k;
  if (scale < low) scale = low;
  if (scale > high) scale = high;
  out->scale = scale;
  float basis[9];
  Basis(out->heading, basis);
  const float d[3] = {-(middle[0] - table_room[0]) * scale, 0.0f,
                      -(middle[2] - table_room[2]) * scale};
  Along(grabbed, basis, d, out->center);
  out->center[1] = a.pose.center[1];
  if (vertical && a.hands == 1) {
    out->center[1] = a.pose.center[1] - scale * (middle[1] - a.middle[1]);
  }
}

// THE EARS ON THE TABLE (docs/tabletop.md, "The ears on the table").
//
// The render head is hundreds or thousands of units over the land, where the
// game hears almost nothing. So the ears go straight down from the head, to
// at most most_above units over the land under them, and they stay inside
// the edge of the table. First the point inside the edge (out y is the head's
// y), then the height over the land there.
inline void InsideEdge(const float p[3], const float center[3], float radius,
                       float out[3]) {
  float dx = p[0] - center[0], dz = p[2] - center[2];
  const float d = sqrtf(dx * dx + dz * dz);
  if (d > radius && d > 0.0f) {
    dx *= radius / d;
    dz *= radius / d;
  }
  out[0] = center[0] + dx;
  out[1] = p[1];
  out[2] = center[2] + dz;
}

inline float EarsHeight(float head_y, float ground, float most_above) {
  const float top = ground + most_above;
  return head_y < top ? head_y : top;
}

// THE GOD VIEW (docs/god-view.md).
//
// The same map as the table, with the table center in the room right under
// the recenter pose, down_m below it:
//
//     world = G + s * Lt * (room - Tg),   Tg = (0, -down_m, 0)
//
// G is the world point at the waist, the center of a TablePose. Its height
// is not the sea level: it is the height of the land that is at the waist.
// The scale s goes from the life size scale to the table scales. A pose
// changes only by the player's hands and steps, never by the land under the
// head, so the view does not rise and fall with the hills.
inline void GodRoom(float down_m, float out[3]) {
  out[0] = 0.0f;
  out[1] = -down_m;
  out[2] = 0.0f;
}

// A room point in the world, with a pose.
inline void PoseWorld(const TablePose& pose, const float table_room[3],
                      const float room[3], float out[3]) {
  float basis[9];
  Basis(pose.heading, basis);
  RoomToWorld(pose.center, basis, pose.scale, table_room, room, out);
}

// A world point in the room, with a pose: the map backward.
inline void PoseRoom(const TablePose& pose, const float table_room[3],
                     const float world[3], float out[3]) {
  float basis[9];
  Basis(pose.heading, basis);
  const float d[3] = {world[0] - pose.center[0], world[1] - pose.center[1],
                      world[2] - pose.center[2]};
  for (int i = 0; i < 3; ++i) {
    // The transpose of the basis, then the scale.
    const float along = basis[0 * 3 + i] * d[0] + basis[1 * 3 + i] * d[1] +
                        basis[2 * 3 + i] * d[2];
    out[i] = table_room[i] + (pose.scale > 0.0f ? along / pose.scale : 0.0f);
  }
}

// The height in the room of the land at world height ground_y, relative to
// the recenter pose, in meters.
inline float GroundInRoom(const TablePose& pose, const float table_room[3],
                          float ground_y) {
  if (!(pose.scale > 0.0f)) return table_room[1];
  return table_room[1] + (ground_y - pose.center[1]) / pose.scale;
}

// THE GROUND LIMITS FOR A GRAB (docs/god-view.md, "The ground height"). The land under
// the head stays between nearest and farthest meters below the head. This
// takes the height C.y that the grab asks for and gives the one to use.
//
//   under(C.y) = table_room.y + (land - C.y) / s
//
// land is the height of the land under the head in the new pose, head_y and
// room_y are in the room, and current is the C.y now. The result stays
// inside the two limits, and it never moves C.y farther out than current
// already is. So a land that is outside the range (a hill under the head)
// can only move toward it, and at a limit the land stops. The result is the
// request cut to a range, so it is continuous in the hand and never jumps.
inline float GodGroundHeight(float wanted, float current, float land,
                             float room_y, float scale, float head_y,
                             float nearest, float farthest) {
  if (!(scale > 0.0f)) return current;
  const float top = head_y - nearest;
  const float bottom = head_y - farthest;
  float low = land - (top - room_y) * scale;   // the land at nearest
  float high = land - (bottom - room_y) * scale;  // the land at farthest
  if (current < low) low = current;
  if (current > high) high = current;
  if (wanted < low) return low;
  if (wanted > high) return high;
  return wanted;
}

// Scale the world about a world point that keeps its place in the room. The
// ratio is the new scale over the old one. A ratio above 1 shows more of the
// world in the same room, so the world looks smaller.
//
//     G' = pivot + ratio * (G - pivot),   s' = ratio * s
inline void ScaleAbout(const TablePose& pose, const float pivot[3],
                       float ratio, TablePose* out) {
  for (int i = 0; i < 3; ++i) {
    out->center[i] = pivot[i] + ratio * (pose.center[i] - pivot[i]);
  }
  out->heading = pose.heading;
  out->scale = pose.scale * ratio;
}

// ONE ZOOM STEP about the ground under the head. The land point straight
// under the head keeps its place in the room, so the player keeps their
// place in the world, and the ground under them stays at the same height.
// head_room is the head in the room, in meters, and ground_y the height of
// the land under it. The new scale stays between low and high. Returns the
// ratio that was used, 1 when the scale was already at the limit.
inline float GodZoom(const TablePose& pose, const float table_room[3],
                     const float head_room[3], float ground_y, float ratio,
                     float low, float high, TablePose* out) {
  float head[3];
  PoseWorld(pose, table_room, head_room, head);
  const float pivot[3] = {head[0], ground_y, head[2]};
  float scale = pose.scale * ratio;
  if (scale < low) scale = low;
  if (scale > high) scale = high;
  const float used = pose.scale > 0.0f ? scale / pose.scale : 1.0f;
  ScaleAbout(pose, pivot, used, out);
  out->scale = scale;
  return used;
}

// ONE STEP OF THE GROUND: all of the world goes up by rise_m in the room, or
// down for a rise below 0. The scale and the heading stay.
inline void GodRaise(const TablePose& pose, float rise_m, TablePose* out) {
  *out = pose;
  out->center[1] = pose.center[1] - pose.scale * rise_m;
}

// A SNAP TURN about the head: the view turns by angle in radians, to the
// right for an angle above 0, and the world point of the head stays where it
// is. The land turns about the head, not about the waist point.
inline void GodTurn(const TablePose& pose, const float table_room[3],
                    const float head_room[3], float angle, TablePose* out) {
  float head[3];
  PoseWorld(pose, table_room, head_room, head);
  out->heading = Wrap(pose.heading + angle);
  out->scale = pose.scale;
  float basis[9];
  Basis(out->heading, basis);
  const float d[3] = {-(head_room[0] - table_room[0]) * pose.scale,
                      -(head_room[1] - table_room[1]) * pose.scale,
                      -(head_room[2] - table_room[2]) * pose.scale};
  Along(head, basis, d, out->center);
}

// THE GAME'S OWN CAMERA over the player's place: it looks at the ground
// point along the heading, from distance units away, pitched down by pitch
// radians. The game logic, the sound and the game's limits then see a camera
// at a normal play distance.
inline void GameCameraOver(const float ground[3], float heading,
                           float distance, float pitch, float position[3]) {
  const float flat = distance * cosf(pitch);
  position[0] = ground[0] - sinf(heading) * flat;
  position[1] = ground[1] + distance * sinf(pitch);
  position[2] = ground[2] - cosf(heading) * flat;
}

// THE THROW FROM INSIDE THE INFLUENCE (docs/tabletop.md, "The throw from
// inside the influence").
// The game lets go of an object only when the land point of the release is
// inside the player's influence. At a large scale a swing carries the Hand
// far over the land, so the swing can start inside the influence and end
// outside it. This finds the point that the release uses then: the newest
// point of the Hand's path, not older than window seconds before now, that
// inside(i) accepts. times holds the time of each point in seconds, oldest
// first. Returns -1 when no point in the window is inside.
template <typename Inside>
inline int ThrowFromIndex(const double* times, int count, double now,
                          double window, Inside inside) {
  for (int i = count - 1; i >= 0; --i) {
    if (now - times[i] > window) break;
    if (inside(i)) return i;
  }
  return -1;
}

// THE MOUSE PAN OF THE GOD VIEW (docs/god-view.md, "Moving around: the grab
// and the mouse").
//
// The mouse is a hand that points. At the press, the land point under the
// mouse ray is the grabbed point, and its height in the room sets a level
// plane of the room. On each frame the ray of the pointer meets that plane,
// and the pan of one hand (TableGrabSolve) puts the grabbed point there. So
// the grabbed land stays on the ray. The ray starts at the head of the
// press, so a head that moves with a still mouse moves no land.

// Where a ray of the room meets the level plane at height y. origin and dir
// are in room meters, and dir is a unit vector. False when the ray goes away
// from the plane, or meets it farther than most_m.
inline bool RayLevel(const float origin[3], const float dir[3], float y,
                     float most_m, float out[3]) {
  if (!(fabsf(dir[1]) > 1e-6f)) return false;
  const float t = (y - origin[1]) / dir[1];
  if (!(t > 0.0f) || t > most_m) return false;
  for (int i = 0; i < 3; ++i) out[i] = origin[i] + t * dir[i];
  out[1] = y;
  return true;
}

// The first land point on a ray of the room. height(x, z) is the land height
// at a world point. The ray goes from origin along the unit vector dir, in
// steps of step_units of the world, up to most_units. Then the last step is
// halved 30 times. A ray that starts under the land must come out of it
// first. False when the ray meets no land. out is the land point in the room.
template <typename Height>
inline bool RayLand(const TablePose& pose, const float table_room[3],
                    const float origin[3], const float dir[3],
                    float step_units, float most_units, Height height,
                    float out[3]) {
  if (!(pose.scale > 0.0f) || !(step_units > 0.0f) || !(most_units > 0.0f)) {
    return false;
  }
  // How far the ray point at t meters is over the land, in world units.
  const auto over = [&](float t) {
    const float room[3] = {origin[0] + t * dir[0], origin[1] + t * dir[1],
                           origin[2] + t * dir[2]};
    float world[3];
    PoseWorld(pose, table_room, room, world);
    return world[1] - height(world[0], world[2]);
  };
  const float step = step_units / pose.scale;
  const int steps = static_cast<int>(ceilf(most_units / step_units));
  bool was_over = over(0.0f) > 0.0f;
  for (int i = 1; i <= steps; ++i) {
    const float t = static_cast<float>(i) * step;
    const bool now_over = over(t) > 0.0f;
    if (was_over && !now_over) {
      float above = t - step, below = t;
      for (int n = 0; n < 30; ++n) {
        const float middle = 0.5f * (above + below);
        if (over(middle) > 0.0f) {
          above = middle;
        } else {
          below = middle;
        }
      }
      for (int k = 0; k < 3; ++k) out[k] = origin[k] + below * dir[k];
      return true;
    }
    was_over = now_over;
  }
  return false;
}

// THE TELEPORT OF THE GOD VIEW (docs/god-view.md, "The teleport").
//
// The head goes over the land point target, and the land under the head keeps
// its height in the room, the same rule as the zoom. Keeping the height of
// the waist point instead would let a hill come up through the head. The
// scale and the heading stay, so it is one cut and nothing moves after it.
//
//   H   = the head in the world
//   r0  = the height in the room of the land under H, kept between 2.5 m and
//         0.10 m under the head
//   G'  = (G.x + T.x - H.x, ground(T) - (r0 - Tg.y) * s, G.z + T.z - H.z)
//
// ground_head is the land height under H, and ground_target the land height
// at the target. The target's own height in target[1] is not read.
inline void GodTeleport(const TablePose& pose, const float table_room[3],
                        const float head_room[3], float ground_head,
                        const float target[3], float ground_target,
                        float nearest_m, float farthest_m, TablePose* out) {
  float head[3];
  PoseWorld(pose, table_room, head_room, head);
  float in_room = GroundInRoom(pose, table_room, ground_head);
  const float top = head_room[1] - nearest_m;
  const float bottom = head_room[1] - farthest_m;
  if (in_room > top) in_room = top;
  if (in_room < bottom) in_room = bottom;
  out->heading = pose.heading;
  out->scale = pose.scale;
  out->center[0] = pose.center[0] + target[0] - head[0];
  out->center[2] = pose.center[2] + target[2] - head[2];
  out->center[1] = ground_target - (in_room - table_room[1]) * pose.scale;
}

// THE PLACE OF THE GOD VIEW AT A CAMERA OF THE GAME (docs/god-view.md,
// "The place at a camera of the game").
//
// The game put its camera at a place, and that camera looks at a land point.
// The player goes there. The land point is ahead_m in front of the head, and
// the flat forward of the head gets the heading of the camera. So the player
// sees what the game shows, from the side that the game shows it from.
//
//   theta = the yaw of the head in the room
//   psi   = camera_heading - theta
//   r     = head + ahead_m * (sin theta, 0, cos theta)      across only
//   G     = look - s * Lt(psi) * (r - Tg)                   across only
//   G.y   = look.y - (look_room_y - Tg.y) * s
//
// look is the land point with its land height. look_room_y is the height in
// the room that the land at the look point gets. The scale does not change.
// A head at the recenter pose with a yaw of 0, and ahead_m of 0, puts the land
// point under the recenter pose.
inline void GodSpawn(const float look[3], float camera_heading, float scale,
                     const float table_room[3], const float head_room[3],
                     float head_yaw, float ahead_m, float look_room_y,
                     TablePose* out) {
  out->heading = Wrap(camera_heading - head_yaw);
  out->scale = scale;
  float basis[9];
  Basis(out->heading, basis);
  const float ahead[3] = {head_room[0] + ahead_m * sinf(head_yaw), 0.0f,
                          head_room[2] + ahead_m * cosf(head_yaw)};
  const float d[3] = {-(ahead[0] - table_room[0]) * scale, 0.0f,
                      -(ahead[2] - table_room[2]) * scale};
  Along(look, basis, d, out->center);
  out->center[1] = look[1] - (look_room_y - table_room[1]) * scale;
}

// THE LAND POINT THAT A CAMERA LOOKS AT. The game keeps the focus of the
// player's camera on the land. A script can put the focus under the land or
// in the air, and the player's camera mode then brings the focus back along
// the camera line (run sp7: 22.5 units, with no move and no turn of the
// camera). So the look point is the first land point on the line from the
// camera to its focus, when the line meets the land before the focus.
// Otherwise it is the focus, put on the land: a focus in the air is over the
// feet of its subject. Both poses of such a camera give one look point.
// height(x, z) is the land height. The line goes in steps of step_units, and
// the last step is halved 20 times.
template <typename Height>
inline void CameraLookPoint(const float position[3], const float focus[3],
                            float step_units, Height height, float out[3]) {
  out[0] = focus[0];
  out[2] = focus[2];
  out[1] = height(focus[0], focus[2]);
  const float d[3] = {focus[0] - position[0], focus[1] - position[1],
                      focus[2] - position[2]};
  const float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
  if (!(length > 1e-3f) || !(step_units > 0.0f)) return;
  // How far the point of the line at t units is over the land.
  const auto over = [&](float t) {
    const float k = t / length;
    const float x = position[0] + d[0] * k, z = position[2] + d[2] * k;
    return position[1] + d[1] * k - height(x, z);
  };
  // A camera under the land has no line of sight.
  if (!(over(0.0f) > 0.0f)) return;
  const int steps = static_cast<int>(ceilf(length / step_units));
  float above = 0.0f;
  for (int i = 1; i <= steps; ++i) {
    float t = static_cast<float>(i) * step_units;
    if (t > length) t = length;
    if (over(t) > 0.0f) {
      above = t;
      continue;
    }
    float below = t;
    for (int n = 0; n < 20; ++n) {
      const float middle = 0.5f * (above + below);
      if (over(middle) > 0.0f) {
        above = middle;
      } else {
        below = middle;
      }
    }
    const float k = below / length;
    out[0] = position[0] + d[0] * k;
    out[2] = position[2] + d[2] * k;
    out[1] = height(out[0], out[2]);
    return;
  }
}

// The yaw of a head about up, from its matrix in the room (the columns are
// its right, its up and its forward). 0 is the room forward, and a yaw of
// more than 0 is to the right. It comes from the right axis, which stays
// level when the head looks down. False for a head that lies on its side.
inline bool HeadYaw(const float head[9], float* yaw) {
  const float rx = head[0], rz = head[6];
  if (rx * rx + rz * rz < 0.01f) return false;
  *yaw = atan2f(-rz, rx);
  return true;
}

}  // namespace table_math
