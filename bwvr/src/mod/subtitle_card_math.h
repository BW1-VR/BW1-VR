// The math of the subtitle card, as pure functions with no game state.
//
// ui_panel.cpp calls these with the poses of the frame, and
// test/controller_math calls them with numbers it makes up. Nothing here
// reads a global or touches the game. See docs/ui-layer.md, "The subtitle
// card".
//
// THE SPACE is the LOCAL space of OpenXR: right handed, meters, y up and -z
// ahead. A pose is a position and a quaternion x y z w. A quad layer shows
// its +z side.

#pragma once

#include <math.h>
#include <stdint.h>

#include "controller_math.h"

namespace subtitle_math {

const float kPi = 3.14159265f;
const float kDegrees = 0.01745329f;  // radians in a degree

// WHY THE SUBTITLES WENT APART. The subtitles were flat interface in the two
// eye pictures, moved `shift` pixels toward the nose in each eye. Two
// pictures that are 2 * shift pixels apart show at this depth, with `focal`
// the pixels of one unit of tan(angle) and `eyes` the distance between the
// eyes in meters. No shift is a depth with no end.
inline float DepthOfShift(float focal, float eyes, float shift) {
  if (!(shift > 0.0f)) return 1e9f;
  return focal * eyes / (2.0f * shift);
}

// The shift in each eye that puts the flat interface at `depth` meters.
inline float ShiftOfDepth(float focal, float eyes, float depth) {
  if (!(depth > 0.0f)) return 0.0f;
  return focal * eyes / (2.0f * depth);
}

// The angle in degrees between the two pictures of the interface while the
// eyes look at a thing at `looked` meters.
inline float SplitDegrees(float eyes, float depth, float looked) {
  if (!(depth > 0.0f) || !(looked > 0.0f)) return 0.0f;
  return fabsf(eyes / looked - eyes / depth) / kDegrees;
}

// THE YAW OF THE HEAD: its turn about y, in radians. 0 is -z, and a turn to
// the left is positive. It comes from the right axis of the head, which
// stays level while the head looks up or down. So a head that looks straight
// down at the land has the same yaw as before.
inline float HeadYaw(const float orientation[4]) {
  const float right[3] = {1.0f, 0.0f, 0.0f};
  float r[3];
  controller_math::QuatRotate(orientation, right, r);
  return atan2f(-r[2], r[0]);
}

// The difference of two yaws, from 0 to pi.
inline float YawBetween(float a, float b) {
  float d = fmodf(a - b, 2.0f * kPi);
  if (d > kPi) d -= 2.0f * kPi;
  if (d < -kPi) d += 2.0f * kPi;
  return fabsf(d);
}

// THE ANCHOR OF THE CARD: a head position and a yaw. The card hangs from it,
// so the card is fixed in the room.
//
//   - The anchor starts at the recenter pose, and each recenter puts it there
//     again.
//   - The card does not follow the head. A head that turns, leans or looks
//     down moves nothing.
//   - The head is AWAY when it has turned more than away_rad from the anchor,
//     or moved more than away_m along the floor, or more than away_up_m up
//     or down. Then the card is at the edge of the view or out of it.
//   - While the card does not show, an away head takes the anchor with it at
//     once. Nothing is in the view, so nothing moves in the view.
//   - While the card shows, the anchor goes to the head only after the head
//     was away for hold_s without a break. The card then fades out at its
//     old place and fades in at the new one (ShownStep). It never slides.
struct Anchor {
  bool have = false;
  float head[3] = {0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  uint64_t serial = 0;        // of the recenter pose that it came from
  double away_since = -1.0;   // less than 0: the head is not away
};

struct AnchorRule {
  float away_rad = 50.0f * kDegrees;
  float away_m = 1.0f;
  float away_up_m = 0.35f;
  double hold_s = 1.0;
};

enum AnchorEvent { kAnchorKept, kAnchorAtRecenter, kAnchorMoved };

inline void AnchorPlace(Anchor* a, const float head[3], float yaw) {
  for (int i = 0; i < 3; ++i) a->head[i] = head[i];
  a->yaw = yaw;
  a->have = true;
  a->away_since = -1.0;
}

inline bool AnchorAway(const Anchor& a, const float head[3], float yaw,
                       const AnchorRule& rule) {
  const float dx = head[0] - a.head[0], dz = head[2] - a.head[2];
  return YawBetween(yaw, a.yaw) > rule.away_rad ||
         sqrtf(dx * dx + dz * dz) > rule.away_m ||
         fabsf(head[1] - a.head[1]) > rule.away_up_m;
}

// One frame. `showing` says that the card was in the view on the last frame.
inline int AnchorStep(Anchor* a, bool have_origin, const float origin[3],
                      float origin_yaw, uint64_t origin_serial,
                      const float head[3], float head_yaw, bool showing,
                      double now, const AnchorRule& rule) {
  int event = kAnchorKept;
  if (have_origin && (!a->have || a->serial != origin_serial)) {
    AnchorPlace(a, origin, origin_yaw);
    a->serial = origin_serial;
    event = kAnchorAtRecenter;
  } else if (!a->have) {
    AnchorPlace(a, head, head_yaw);
    return kAnchorMoved;
  }
  if (!AnchorAway(*a, head, head_yaw, rule)) {
    a->away_since = -1.0;
    return event;
  }
  if (!showing) {
    AnchorPlace(a, head, head_yaw);
    return kAnchorMoved;
  }
  if (a->away_since < 0.0) a->away_since = now;
  if (now - a->away_since >= rule.hold_s) {
    AnchorPlace(a, head, head_yaw);
    return kAnchorMoved;
  }
  return event;
}

// THE PLACE THAT THE CARD SHOWS AT. It is the anchor, but a change of the
// anchor while the card shows does not move the card: the card fades out
// where it is, takes the new place while it is clear, and fades in.
struct Shown {
  bool have = false;
  float head[3] = {0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  float fade = 0.0f;  // 0 clear, 1 full
};

inline bool ShownAt(const Shown& s, const Anchor& a) {
  return s.have && s.yaw == a.yaw && s.head[0] == a.head[0] &&
         s.head[1] == a.head[1] && s.head[2] == a.head[2];
}

// One frame. `content` says that the card has a picture in this frame.
inline void ShownStep(Shown* s, const Anchor& a, bool content, float dt,
                      float seconds) {
  const float step = seconds > 0.0f && dt > 0.0f ? dt / seconds : 1.0f;
  if (!ShownAt(*s, a)) {
    if (s->have && content && s->fade > 0.0f) {
      // Fade out at the old place.
      s->fade -= step;
      if (s->fade > 0.0f) return;
    }
    for (int i = 0; i < 3; ++i) s->head[i] = a.head[i];
    s->yaw = a.yaw;
    s->have = true;
    s->fade = 0.0f;
    return;
  }
  s->fade += content ? step : -step;
  if (s->fade > 1.0f) s->fade = 1.0f;
  if (s->fade < 0.0f) s->fade = 0.0f;
}

// THE SCREEN IN THE ROOM. The game draws the subtitles and the help text in
// pixels of the render. The card shows those pixels on a flat screen that is
// fixed to the anchor:
//
//   - The row ref_y of the render (a share of its height, the middle of the
//     subtitle box) is `distance` meters from the anchor head, `down`
//     radians below eye level, straight ahead of the anchor yaw.
//   - The screen is square to the line from the anchor head to that row, so
//     the subtitles face the reader.
//   - The whole width of the render is `width` meters. So a letter has one
//     size in the room, at each size of the render.
//
// The quad layer of a frame is the part of that screen that the game drew
// in: a rect in pixels of the render. A rect that grows or gets smaller
// moves no pixel in the room.
struct Layout {
  float distance = 1.6f;
  float down = 17.0f * kDegrees;
  float width = 1.0f;
  float ref_y = 0.93f;
};

struct Quad {
  float position[3];
  float orientation[4];
  float width;
  float height;
};

// rect is x, y, width and height in pixels of the render.
inline void RectQuad(const float head[3], float yaw, const Layout& layout,
                     float render_w, float render_h, const float rect[4],
                     Quad* out) {
  const float yaw_q[4] = {0.0f, sinf(yaw * 0.5f), 0.0f, cosf(yaw * 0.5f)};
  float ref[3];
  controller_math::PanelPose(head, yaw_q, layout.distance, -layout.down, ref,
                             out->orientation);
  const float meter = render_w > 0.0f ? layout.width / render_w : 0.0f;
  const float local[3] = {
      (rect[0] + 0.5f * rect[2] - 0.5f * render_w) * meter,
      (layout.ref_y * render_h - (rect[1] + 0.5f * rect[3])) * meter, 0.0f};
  float turned[3];
  controller_math::QuatRotate(out->orientation, local, turned);
  for (int i = 0; i < 3; ++i) out->position[i] = ref[i] + turned[i];
  out->width = rect[2] * meter;
  out->height = rect[3] * meter;
}

// The rect of the card of a frame: the bounds of what drew, with `pad`
// pixels around it, on a grid of `grid` pixels, inside the render. The grid
// keeps the rect still while letters come one at a time. False when there is
// nothing to show.
inline bool CardRect(const float bounds[4], float pad, int grid, int render_w,
                     int render_h, int out[4]) {
  if (grid < 1) grid = 1;
  int x0 = static_cast<int>(floorf(bounds[0] - pad));
  int y0 = static_cast<int>(floorf(bounds[1] - pad));
  int x1 = static_cast<int>(ceilf(bounds[2] + pad));
  int y1 = static_cast<int>(ceilf(bounds[3] + pad));
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > render_w) x1 = render_w;
  if (y1 > render_h) y1 = render_h;
  x0 -= x0 % grid;
  y0 -= y0 % grid;
  if (x1 % grid != 0) x1 += grid - x1 % grid;
  if (y1 % grid != 0) y1 += grid - y1 % grid;
  if (x1 > render_w) x1 = render_w;
  if (y1 > render_h) y1 = render_h;
  if (x1 - x0 < 2 || y1 - y0 < 2) return false;
  out[0] = x0;
  out[1] = y0;
  out[2] = x1 - x0;
  out[3] = y1 - y0;
  return true;
}

}  // namespace subtitle_math
