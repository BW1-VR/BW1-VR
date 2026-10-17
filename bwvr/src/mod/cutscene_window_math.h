// The math of the cut scene window (docs/cutscene-window.md). It has no game
// and no headset in it. cutscene_window.cpp uses it, and
// test/controller_math checks it.
#pragma once

#include <math.h>
#include <string.h>

namespace window_math {

// 0 at a and below, 1 at b and above, and a smooth step between them.
inline float Smooth(float a, float b, float x) {
  if (!(b > a)) return x < a ? 0.0f : 1.0f;
  float t = (x - a) / (b - a);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// THE RULE. A cut scene is the time while the cinema bars of the game are up
// (HelpSystem::SetWideScreen). The window is wanted for all of that time.
//
// A script takes the bars down before it gives the camera back. So after the
// bars go down, the window stays while a story camera still owns the view.
// Then the God view is built once, at the place where the story left the
// camera. A story camera that stays longer than `linger` seconds is not the
// end of a cut scene (a follow camera, for example), and the window goes.
//
// A story camera with no bars does not start the window.
inline bool Wanted(bool bars_up, bool was_wanted, bool story_owns,
                   float seconds_since_bars, float linger) {
  if (bars_up) return true;
  if (!was_wanted) return false;
  return story_owns && seconds_since_bars < linger;
}

// THE STATE. `open` is 1 for the full view and 0 for the opening that has
// the size of the window. `flat` is true while the eye pictures go to the
// screen of the window.
struct State {
  bool flat;
  float open;
};

// One frame of `dt` seconds. The view closes to the window in `duration`
// seconds and then goes flat. When the window is not wanted, the view leaves
// the flat state at once and opens in the same time. True when `flat`
// changed.
inline bool Step(State* s, bool wanted, float dt, float duration) {
  const bool before = s->flat;
  float step = 1.0f;
  if (duration > 0.0f) step = dt > 0.0f ? dt / duration : 0.0f;
  if (wanted) {
    if (!s->flat) {
      s->open -= step;
      if (s->open <= 0.0f) {
        s->open = 0.0f;
        s->flat = true;
      }
    }
  } else {
    s->flat = false;
    s->open += step;
    if (s->open > 1.0f) s->open = 1.0f;
  }
  return s->flat != before;
}

// THE OPENING. At the full view the opening is kFullScale times the window,
// which is past the edges of the view of a head that looks at the window.
// The scale changes by the same ratio in the same time, and the time has a
// smooth start and a smooth end.
const float kFullScale = 4.0f;
inline float OpenScale(float open) {
  return powf(kFullScale, Smooth(0.0f, 1.0f, open));
}

// The walls around the opening are clear at the full view. They become black
// in the first part of the change, so a wall that is already in the view
// (the head looks down at the land) does not appear in one frame.
const float kFadeFrom = 0.6f;
inline float MaskAlpha(float open) {
  return 1.0f - Smooth(kFadeFrom, 1.0f, Smooth(0.0f, 1.0f, open));
}

// q is x y z w. The result may be v.
inline void Rotate(const float q[4], const float v[3], float out[3]) {
  const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
  const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
  const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  const float x = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
  const float y = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
  const float z = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
  out[0] = x;
  out[1] = y;
  out[2] = z;
}

// THE SCREEN OF THE WINDOW, in the LOCAL space of the runtime. The center is
// `distance` meters ahead of the recenter pose, at the height of the head at
// the recenter. The screen is upright and its front (+z) faces the anchor.
// It does not follow the head.
struct Screen {
  float position[3];
  float orientation[4];  // x y z w
  float width;           // meters
  float height;
};

// anchor and yaw are the recenter pose: the head center and a turn about y.
// height_over_width is the shape of the render.
inline void ScreenPose(const float anchor[3], const float yaw[4],
                       float distance, float width, float height_over_width,
                       Screen* out) {
  const float ahead[3] = {0.0f, 0.0f, -distance};
  float turned[3];
  Rotate(yaw, ahead, turned);
  for (int i = 0; i < 3; ++i) out->position[i] = anchor[i] + turned[i];
  for (int i = 0; i < 4; ++i) out->orientation[i] = yaw[i];
  out->width = width;
  out->height = width * height_over_width;
}

// THE WALLS: four black quads in the plane of the screen, around an opening
// of open_w by open_h meters at the center of the screen. Each wall reaches
// `border` meters out from the opening. The order is top, bottom, left,
// right. The top and the bottom wall take the corners.
struct Wall {
  float position[3];
  float width;
  float height;
};

inline void Walls(const Screen& screen, float open_w, float open_h,
                  float border, Wall out[4]) {
  const float x_axis[3] = {1.0f, 0.0f, 0.0f};
  const float y_axis[3] = {0.0f, 1.0f, 0.0f};
  float right[3], up[3];
  Rotate(screen.orientation, x_axis, right);
  Rotate(screen.orientation, y_axis, up);
  const float across = 0.5f * (open_w + border);
  const float along = 0.5f * (open_h + border);
  for (int i = 0; i < 3; ++i) {
    out[0].position[i] = screen.position[i] + up[i] * along;
    out[1].position[i] = screen.position[i] - up[i] * along;
    out[2].position[i] = screen.position[i] - right[i] * across;
    out[3].position[i] = screen.position[i] + right[i] * across;
  }
  out[0].width = out[1].width = open_w + 2.0f * border;
  out[0].height = out[1].height = border;
  out[2].width = out[3].width = border;
  out[2].height = out[3].height = open_h;
}

// THE STEREO WITH NO DEPTH LIMIT (cutscene_depth=0, and play mode 1). The
// two eye pictures come from two cameras that look the same way,
// `separation` world units apart. Both pictures are at one place on the
// screen. A point at `depth` units is then drawn at two places on the
// screen. This is the distance between them in meters, for a screen
// `screen_width` meters wide that shows a view of fov_x radians. The left
// eye sees the point on the right, so the point is in front of the screen.
inline float ScreenDisparity(float screen_width, float fov_x,
                             float separation, float depth) {
  const float half = tanf(fov_x * 0.5f);
  if (!(depth > 0.0f) || !(half > 0.0f)) return 0.0f;
  return screen_width * separation / (2.0f * depth * half);
}

// Where the eyes see a point with that disparity, in meters from the eyes.
// The eyes are `ipd` meters apart and the screen is screen_distance meters
// away. A point far away has no disparity, so it is on the screen. All of
// the scene is between the screen and the eyes.
inline float SeenDistance(float screen_distance, float ipd, float disparity) {
  if (!(ipd + disparity > 0.0f)) return screen_distance;
  return screen_distance * ipd / (ipd + disparity);
}

// THE PLACE OF THE SCREEN (cutscene_face_head). The screen goes ahead of the
// head of the moment that the cut scene starts: at the yaw of the head, with
// no pitch and no roll, at a height that the caller gives. After that the
// screen stays where it is in the room.

// The yaw of a head: its turn about y, in radians. 0 is -z, and a turn to
// the left is more than 0. It comes from the right axis of the head, which
// stays level while the head looks up or down.
inline float YawOf(const float orientation[4]) {
  const float right[3] = {1.0f, 0.0f, 0.0f};
  float r[3];
  Rotate(orientation, right, r);
  return atan2f(-r[2], r[0]);
}

// A turn of `yaw` radians about y, as x y z w.
inline void YawTurn(float yaw, float out[4]) {
  out[0] = 0.0f;
  out[1] = sinf(yaw * 0.5f);
  out[2] = 0.0f;
  out[3] = cosf(yaw * 0.5f);
}

// The anchor of ScreenPose: a position and a turn about y.
struct Anchor {
  float position[3];
  float turn[4];  // x y z w
};

// The anchor at the head: the place of the head along the floor, the height
// `height`, and the yaw of the head.
inline void AnchorAtHead(const float head[3], const float head_orientation[4],
                         float height, Anchor* out) {
  out->position[0] = head[0];
  out->position[1] = height;
  out->position[2] = head[2];
  YawTurn(YawOf(head_orientation), out->turn);
}

// THE DEPTH LIMIT (cutscene_depth). The picture is a view through a window:
// nothing is in front of the screen, and the eyes are parallel for a point
// that is far away.
//
// The two eye pictures are `shift` meters apart on the screen, and the
// picture of the left eye is the one to the left. A point far away is at one
// pixel in both pictures, so it is `shift` apart on the screen. With a shift
// of the distance between the eyes, the eyes are parallel for it. The shift
// is a whole number of pixels, `crop`, and it is never more than the distance
// between the eyes. So the eyes never turn out.
//
// THE OPENING is the part of the screen that has both pictures. Each eye
// picture loses `crop` columns: the left eye at its left side and the right
// eye at its right side. So the opening is one rectangle in the room, and
// each eye picture fills it to its edges.
//
// The crop is an even number. The flat interface is on the screen when each
// eye moves it half of the crop, and a move of a whole number of pixels
// keeps its letters sharp.
inline int Crop(float ipd, float picture_width, int res_x) {
  if (!(ipd > 0.0f) || !(picture_width > 0.0f) || res_x <= 0) return 0;
  int crop = static_cast<int>(floorf(ipd * res_x / picture_width));
  if (crop > res_x / 4) crop = res_x / 4;
  if (crop < 0) crop = 0;
  return crop - crop % 2;
}

inline float Shift(int crop, float picture_width, int res_x) {
  return res_x > 0 ? crop * picture_width / res_x : 0.0f;
}

// THE EDGE COLUMNS (cutscene_edge_columns). The game draws a flat batch (the
// cinema bars, a fade) as a rectangle with its edges on whole pixels. The
// render has multisampling, so the first column of a picture has only half
// of its samples in that rectangle. In the right eye picture the scene comes
// from the walk of the left eye, moved to the left, so it fills all of that
// column. The scene then shows through the bars at the left edge of the
// opening, as a column of light. The last column of the left eye picture
// shows through a fade in the same way.
//
// So each eye picture also loses `edge` columns at each side of the part
// that the opening shows. The two parts keep `crop` columns between their
// starts, so the depth of the picture does not change, and the opening stays
// one rectangle. `offset` is the first column that an eye shows, and
// `width` is the number of columns.
struct EyeColumns {
  int offset;
  int width;
};

inline EyeColumns Columns(int eye, int crop, int edge, int res_x) {
  EyeColumns out = {0, res_x > 0 ? res_x : 0};
  if (res_x <= 0) return out;
  if (crop < 0 || crop >= res_x / 2) crop = 0;
  if (edge < 0 || res_x - crop - 2 * edge < res_x / 2) edge = 0;
  out.offset = (eye == 0 ? crop : 0) + edge;
  out.width = res_x - crop - 2 * edge;
  return out;
}

// The distance on the screen between the two pictures of a point at `depth`
// units, in meters. 0 is on the screen and more than 0 is behind it.
// `half_tan` is the tangent of half of the view angle of a picture.
inline float Parallax(float shift, float picture_width, float half_tan,
                      float separation, float depth) {
  if (!(depth > 0.0f) || !(half_tan > 0.0f)) return shift;
  return shift - picture_width * separation / (2.0f * depth * half_tan);
}

// The eye separation in world units that puts a point at `plane` units on
// the screen. A nearer point is then in front of the screen, and a point
// that is more units away is behind it.
inline float SeparationFor(float shift, float picture_width, float half_tan,
                           float plane) {
  if (!(picture_width > 0.0f)) return 0.0f;
  return 2.0f * half_tan * shift * plane / picture_width;
}

// Where eyes `ipd` meters apart see a point with that parallax, in meters
// from the eyes. With parallel eyes, or eyes that turn out, the answer is
// 1e9.
inline float SeenBehind(float screen_distance, float ipd, float parallax) {
  if (!(ipd - parallax > 1e-6f)) return 1e9f;
  return screen_distance * ipd / (ipd - parallax);
}

// THE PLANE is the depth, in world units, that is on the screen. The rule:
//
//   - The nearest thing in view is not in front of the screen. So the plane
//     is at the nearest depth, less a margin.
//   - The plane is not farther than the screen distance at the life size
//     scale. A scene with all its things far away then has its true depth,
//     as a real window shows it.
//   - With no measure of the nearest depth, the plane is at the near plane of
//     the game. Nothing draws nearer than that, so it is the worst case.
struct DepthRule {
  float screen_distance = 2.0f;  // meters
  float units_per_meter = 7.5f;  // the life size scale
  float near_plane = 3.5f;       // units
  float margin = 0.85f;          // the plane is at this share of the nearest
  // Inside a shot the plane goes to a nearer target in in_seconds (0 is at
  // once), waits hold_seconds, and goes back to a farther target in
  // out_seconds.
  float in_seconds = 0.0f;
  float hold_seconds = 2.0f;
  float out_seconds = 4.0f;
};

// The plane that the rule asks for. `nearest` is the nearest depth in view
// in units, or 0 for no measure.
inline float PlaneTarget(float nearest, const DepthRule& rule) {
  const float far_limit = rule.screen_distance * rule.units_per_meter;
  const float least =
      rule.near_plane < far_limit ? rule.near_plane : far_limit;
  if (!(nearest > 0.0f)) return least;
  float plane = nearest * rule.margin;
  if (plane < least) plane = least;
  if (plane > far_limit) plane = far_limit;
  return plane;
}

// THE PLANE OF A FRAME. A change of the plane changes the depth of all the
// picture, so it must not pump inside a shot.
//
//   - The first measure of a shot sets the plane at once (`first`): a cut may
//     snap. The frame of the cut itself has the measure of the shot before,
//     so it keeps the rule of a shot.
//   - Inside a shot, a nearer target takes the plane with it at once. So a
//     thing that comes near is not in front of the screen for longer than
//     the one frame that the measure is old. The plane then keeps that
//     depth: it is a latch, and a measure that goes up and down does not
//     move it.
//   - A farther target must stay for hold_seconds. Then the plane goes back
//     in out_seconds, which is slow.
//   - A frame with no measure (the first frame of a shot) keeps the plane of
//     the shot before. With no plane at all it gets the worst case.
struct DepthState {
  bool have = false;
  float plane = 0.0f;
  float hold = 0.0f;
};

inline float PlaneStep(DepthState* s, float nearest, bool first, float dt,
                       const DepthRule& rule) {
  const float target = PlaneTarget(nearest, rule);
  if (!(nearest > 0.0f)) {
    if (!s->have) s->plane = target;
    return s->plane;
  }
  if (!s->have || first) {
    s->have = true;
    s->plane = target;
    s->hold = 0.0f;
    return s->plane;
  }
  if (!(dt > 0.0f)) return s->plane;
  if (target < s->plane) {
    const float share =
        rule.in_seconds > 0.0f ? 1.0f - expf(-dt / rule.in_seconds) : 1.0f;
    s->plane += (target - s->plane) * share;
    s->hold = rule.hold_seconds;
  } else if (s->hold > 0.0f) {
    s->hold -= dt;
  } else {
    const float share =
        rule.out_seconds > 0.0f ? 1.0f - expf(-dt / rule.out_seconds) : 1.0f;
    s->plane += (target - s->plane) * share;
  }
  return s->plane;
}

// A NEW SHOT: between two frames the camera turned more than 20 degrees, or
// it moved more than half of the nearest depth in view (and more than 2
// units). A glide and a pan of a story are slower than that. The two forward
// axes are unit vectors.
inline bool ShotCut(const float position_before[3],
                    const float forward_before[3], const float position[3],
                    const float forward[3], float nearest) {
  const float dot = forward_before[0] * forward[0] +
                    forward_before[1] * forward[1] +
                    forward_before[2] * forward[2];
  if (dot < 0.9396926f) return true;  // the cosine of 20 degrees
  const float dx = position[0] - position_before[0];
  const float dy = position[1] - position_before[1];
  const float dz = position[2] - position_before[2];
  float limit = nearest > 0.0f ? 0.5f * nearest : 10.0f;
  if (limit < 2.0f) limit = 2.0f;
  return dx * dx + dy * dy + dz * dz > limit * limit;
}

// THE NEAREST DEPTH IN VIEW. The game transforms its vertices on the CPU.
// Each world batch is a list of triangles in pixels of the render, and each
// vertex has rhw, which is the near plane divided by its depth. So a larger
// rhw is nearer, and rhw is linear across a triangle on the screen.
//
// The grid keeps the largest rhw in each of 16 by 16 cells of the opening.
// Each eye pass gives its batches. The part of an eye picture that is in
// view is the columns that the opening shows of it (the picture less its
// crop) and the rows between the cinema bars. So a thing counts when one eye
// or the other shows it. A triangle that goes across an edge of that part is
// cut at the edge, so a vertex that is out of view does not count, and the
// part of its triangle that is in view does.
//
// The nearest depth is the second nearest cell. So one stray vertex, or a
// thing smaller than a cell, cannot make all the picture flat. The margin of
// the rule covers what is left.
//
// A batch with an rhw of exactly 1 in each vertex that it uses has no depth:
// it is a draw in screen space, as the scanline sea of LH3DWater::Draw
// 0x00879930 is. The grid leaves it out. A mesh cannot have all of its
// vertices on the near plane.
const int kGridCells = 16;
const int kGridRank = 2;

struct DepthGrid {
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;  // pixels
  float per_x = 0.0f, per_y = 0.0f;                  // cells in a pixel
  float cell[kGridCells * kGridCells] = {};          // 0 is an empty cell
  float strict = 0.0f;                               // the largest rhw of all
  unsigned points = 0;
};

inline void GridStart(DepthGrid* g, float x0, float y0, float x1, float y1) {
  g->x0 = x0;
  g->y0 = y0;
  g->x1 = x1;
  g->y1 = y1;
  g->per_x = x1 > x0 ? kGridCells / (x1 - x0) : 0.0f;
  g->per_y = y1 > y0 ? kGridCells / (y1 - y0) : 0.0f;
  for (float& c : g->cell) c = 0.0f;
  g->strict = 0.0f;
  g->points = 0;
}

// The columns of the next eye picture that are in view. The cells keep what
// the eye before gave them.
inline void GridEye(DepthGrid* g, float x0, float x1) {
  g->x0 = x0;
  g->x1 = x1;
  g->per_x = x1 > x0 ? kGridCells / (x1 - x0) : 0.0f;
}

// One point that is in view. A point nearer than the near plane counts as
// the near plane, because the game draws nothing nearer.
inline void GridPoint(DepthGrid* g, float x, float y, float rhw) {
  if (rhw > 1.0f) rhw = 1.0f;
  int cx = static_cast<int>((x - g->x0) * g->per_x);
  int cy = static_cast<int>((y - g->y0) * g->per_y);
  if (cx < 0) cx = 0;
  if (cx >= kGridCells) cx = kGridCells - 1;
  if (cy < 0) cy = 0;
  if (cy >= kGridCells) cy = kGridCells - 1;
  float& cell = g->cell[cy * kGridCells + cx];
  if (rhw > cell) cell = rhw;
  if (rhw > g->strict) g->strict = rhw;
  ++g->points;
}

struct GridVertex {
  float x, y, rhw;
};

// Cuts a polygon at one edge of the view. `axis` is 0 for x and 1 for y, and
// `below` keeps the side below `limit`. Returns the new count.
inline int GridCut(const GridVertex* in, int count, GridVertex* out, int axis,
                   float limit, bool below) {
  int made = 0;
  for (int i = 0; i < count; ++i) {
    const GridVertex& a = in[i];
    const GridVertex& b = in[(i + 1) % count];
    const float av = axis == 0 ? a.x : a.y;
    const float bv = axis == 0 ? b.x : b.y;
    const bool a_in = below ? av <= limit : av >= limit;
    const bool b_in = below ? bv <= limit : bv >= limit;
    if (a_in) out[made++] = a;
    if (a_in != b_in) {
      const float t = (limit - av) / (bv - av);
      out[made].x = a.x + (b.x - a.x) * t;
      out[made].y = a.y + (b.y - a.y) * t;
      out[made].rhw = a.rhw + (b.rhw - a.rhw) * t;
      ++made;
    }
  }
  return made;
}

// One batch: `count` vertices of `stride` bytes with x, y, z and rhw first,
// and `n` indices of a triangle list.
//
// ONLY A VERTEX THAT A TRIANGLE USES COUNTS. The game keeps vertices in the
// array that it did not project: a vertex of the land that is out of the view
// frustum keeps its camera space values (fn_00874AA0, the test at
// 0x00874E74), and no triangle of the batch uses it. Read as a vertex on the
// screen, it has a small x and y and its depth in the place of rhw, so it
// looks like a thing at the near plane.
inline void GridBatch(DepthGrid* g, const void* verts, unsigned count,
                      const unsigned short* idx, unsigned n,
                      unsigned stride) {
  if (verts == nullptr || idx == nullptr || count == 0 || n < 3 ||
      !(g->per_x > 0.0f) || !(g->per_y > 0.0f)) {
    return;
  }
  if (count > 65536) count = 65536;
  const unsigned char* base = static_cast<const unsigned char*>(verts);
  bool has_depth = false;
  for (unsigned j = 0; j < n && !has_depth; ++j) {
    const unsigned index = idx[j];
    if (index < count) {
      has_depth =
          reinterpret_cast<const float*>(base + index * stride)[3] != 1.0f;
    }
  }
  if (!has_depth) return;
  // What each vertex is. 0: not looked at yet.
  const unsigned char kIn = 1, kOut = 2, kNoDepth = 3;
  static unsigned char state[65536];
  memset(state, 0, count);
  for (unsigned t = 0; t + 2 < n; t += 3) {
    GridVertex poly[8], next[8];
    int out_all = 0xF;
    bool valid = true, some_out = false;
    for (int k = 0; k < 3; ++k) {
      const unsigned index = idx[t + k];
      if (index >= count) {
        valid = false;
        break;
      }
      const float* v = reinterpret_cast<const float*>(base + index * stride);
      const int code = (v[0] < g->x0 ? 1 : 0) | (v[0] > g->x1 ? 2 : 0) |
                       (v[1] < g->y0 ? 4 : 0) | (v[1] > g->y1 ? 8 : 0);
      unsigned char s = state[index];
      if (s == 0) {
        if (!(v[3] > 0.0f)) {
          s = kNoDepth;
        } else if (code != 0) {
          s = kOut;
        } else {
          s = kIn;
          GridPoint(g, v[0], v[1], v[3]);
        }
        state[index] = s;
      }
      if (s == kNoDepth) {
        valid = false;
        break;
      }
      if (s == kOut) some_out = true;
      poly[k].x = v[0];
      poly[k].y = v[1];
      poly[k].rhw = v[3];
      out_all &= code;
    }
    // All in view: the vertices gave it. All past one edge: not in view.
    // Only a triangle with a vertex out of view can have a part in view that
    // no vertex gives.
    if (!valid || !some_out || out_all != 0) continue;
    int made = GridCut(poly, 3, next, 0, g->x0, false);
    made = GridCut(next, made, poly, 0, g->x1, true);
    made = GridCut(poly, made, next, 1, g->y0, false);
    made = GridCut(next, made, poly, 1, g->y1, true);
    for (int k = 0; k < made; ++k) {
      GridPoint(g, poly[k].x, poly[k].y, poly[k].rhw);
    }
  }
}

// The rhw of the cell that is number `rank` from the nearest (1 is the
// nearest cell). With fewer cells that have a point, the farthest of them.
// 0 with no point at all.
inline float GridNearest(const DepthGrid& g, int rank) {
  float best[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  if (rank < 1) rank = 1;
  if (rank > 4) rank = 4;
  for (float c : g.cell) {
    if (!(c > 0.0f)) continue;
    for (int i = 0; i < rank; ++i) {
      if (c > best[i]) {
        for (int j = rank - 1; j > i; --j) best[j] = best[j - 1];
        best[i] = c;
        break;
      }
    }
  }
  for (int i = rank - 1; i >= 0; --i) {
    if (best[i] > 0.0f) return best[i];
  }
  return 0.0f;
}

// THE SUBTITLES OF THE WINDOW (cutscene_subtitles). The subtitle card is in
// the plane of the screen, under the scene. A picture of the game is 16
// wide and 9 high between its cinema bars (`scene_shape`, height over
// width), so on a screen that is not that wide the scene has a black bar
// under it. The card hangs under the scene, on that bar and past it.

// Half of the height of the scene, in meters.
inline float SceneHalfHeight(float picture_width, float picture_height,
                             float scene_shape) {
  float height = picture_width * scene_shape;
  if (!(scene_shape > 0.0f) || height > picture_height) {
    height = picture_height;
  }
  return 0.5f * height;
}

struct TextQuad {
  float position[3];
  float orientation[4];
  float width;
  float height;
};

// The quad of the card. `rect` is the part of the render that the game drew
// its text in: x, y, width and height in pixels. The whole width of the
// render is `text_width` meters on the card, so a letter has one size in the
// room. The top of the card is `top` meters below the center of the screen,
// and a rect to the left of the middle of the render is to the left on the
// card.
inline void SubtitleQuad(const Screen& screen, float top, float text_width,
                         float render_w, const float rect[4], TextQuad* out) {
  const float meter = render_w > 0.0f ? text_width / render_w : 0.0f;
  const float x_axis[3] = {1.0f, 0.0f, 0.0f};
  const float y_axis[3] = {0.0f, 1.0f, 0.0f};
  float right[3], up[3];
  Rotate(screen.orientation, x_axis, right);
  Rotate(screen.orientation, y_axis, up);
  out->width = rect[2] * meter;
  out->height = rect[3] * meter;
  const float across = (rect[0] + 0.5f * rect[2] - 0.5f * render_w) * meter;
  const float down = top + 0.5f * out->height;
  for (int i = 0; i < 3; ++i) {
    out->position[i] = screen.position[i] + right[i] * across - up[i] * down;
  }
  for (int i = 0; i < 4; ++i) out->orientation[i] = screen.orientation[i];
}

// THE ADVISORS (cutscene_advisors). The game draws its two advisors as meshes
// of the scene at the side of the picture, 7.5 to 11 units from the camera.
// In the window each advisor gets a place of its own between the player and
// the screen: one eye difference for the advisor, the same in each of its
// batches.
//
// The eye difference is x(right) - x(left) of a point, in pixels of an eye
// picture. The two pictures are `crop` columns apart on the screen, so a
// point on the screen has the difference -crop. Eyes `ipd` meters apart see
// a point `seen` meters away, with the screen `screen_distance` meters away,
// when its two pictures are ipd * (1 - screen_distance / seen) meters apart
// on the screen. Less than 0 is in front of the screen.
inline float AdvisorDifference(float ipd, float screen_distance, float seen,
                               float picture_width, int res_x, int crop) {
  if (!(ipd > 0.0f) || !(screen_distance > 0.0f) || !(seen > 0.0f) ||
      !(picture_width > 0.0f) || res_x <= 0) {
    return -static_cast<float>(crop);
  }
  const float apart = ipd * (1.0f - screen_distance / seen);
  return apart * static_cast<float>(res_x) / picture_width -
         static_cast<float>(crop);
}

// THE EYE DIFFERENCE THAT THE GAME GIVES AN ADVISOR. HelpDudeControl::Update
// puts the advisor at a point of the world, and the eye cameras see it as
// each other thing of the world: a vertex has the difference per_rhw * rhw
// (one_walk_math). In one walk the right eye gets each vertex moved by that.
// `natural` is the difference of the middle of the advisor
// (one_walk_math::ShiftAtDepth with the depth of the middle).

// THE EDGE OF THE OPENING. A real window cannot cut a thing that is in front
// of it. The right eye shows from its column `first`, and the left eye to
// the column before `last` (Columns). `low_right` is the first column of the
// advisor in the right eye and `high_left` its last column in the left eye,
// as the game draws them, with the eye difference `natural`. A move to the
// difference d moves the right eye by (d - natural) / 2 to the right and the
// left eye by as much to the left. So the advisor stays in the opening while
// d is not less than natural - 2 * room, with `room` the room of the nearer
// edge. The difference is never less than in front of the screen plane: at
// the edge the advisor is on the screen, where the edge of a window can cut
// a thing.
inline float AdvisorEdge(float difference, int crop, float first, float last,
                         float low_right, float high_left, float natural) {
  const float plane = -static_cast<float>(crop);
  if (!(difference < plane)) return plane;
  const float room = fminf(low_right - first, last - high_left);
  const float most = natural - 2.0f * room;
  const float out = difference > most ? difference : most;
  return out < plane ? out : plane;
}

// What the batches of an advisor gave in the left eye pass of one frame: its
// columns as the game drew them, and its depth in units. Only a vertex that
// a triangle uses counts (GridBatch).
struct AdvisorExtent {
  bool have;
  float low, high;          // columns
  float nearest, farthest;  // units
};

// One batch of an advisor: `count` vertices of `stride` bytes with x, y, z
// and rhw first, and `n` indices of a triangle list.
inline void AdvisorTrack(AdvisorExtent* e, const void* verts, unsigned count,
                         const unsigned short* idx, unsigned n,
                         unsigned stride, float near_plane) {
  if (verts == nullptr || idx == nullptr || !(near_plane > 0.0f)) return;
  const unsigned char* base = static_cast<const unsigned char*>(verts);
  for (unsigned j = 0; j + 2 < n; j += 3) {
    for (int k = 0; k < 3; ++k) {
      const unsigned index = idx[j + k];
      if (index >= count) continue;
      const float* v = reinterpret_cast<const float*>(base + index * stride);
      if (!(v[3] > 0.0f)) continue;
      const float depth = near_plane / v[3];
      if (!e->have) {
        e->have = true;
        e->low = e->high = v[0];
        e->nearest = e->farthest = depth;
        continue;
      }
      if (v[0] < e->low) e->low = v[0];
      if (v[0] > e->high) e->high = v[0];
      if (depth < e->nearest) e->nearest = depth;
      if (depth > e->farthest) e->farthest = depth;
    }
  }
}

// THE MOVE of a vertex of an advisor. The middle of the advisor gets the eye
// difference `difference`: the change is difference - natural, the same for
// each vertex of each batch of the advisor. Each eye moves half of the
// change, so the advisor keeps the column between its two pictures. Each
// vertex keeps its depth from the middle, as the eye cameras give it.
// `keep`: no move. The first frame of an advisor has no measure yet.
struct AdvisorShift {
  float difference;
  float per_rhw;
  float natural;
  bool keep;
};

// The x of a vertex in the picture of `eye` (0 is the left eye). In one walk
// (`one_walk`) the right eye comes from the x of the left eye: the vertex
// moves by per_rhw * rhw, as one walk moves each vertex of the world.
inline float AdvisorX(const AdvisorShift& s, int eye, float x, float rhw,
                      bool one_walk) {
  const float own = one_walk && eye == 1 ? x + s.per_rhw * rhw : x;
  if (s.keep) return own;
  const float half = 0.5f * (s.difference - s.natural);
  return eye == 0 ? own - half : own + half;
}

}  // namespace window_math
