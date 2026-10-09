// The math of the hint card, as pure functions with no game state.
//
// hint_card.cpp calls these with the poses of the frame, and
// test/controller_math calls them with numbers it makes up. Nothing here reads
// a global or touches the game. See docs/controller-hints.md, "The hint
// card".
//
// THE SPACE is the LOCAL space of OpenXR: right handed, meters, y up and -z
// ahead. A pose is a position and a quaternion x y z w. The card is a quad
// that shows its +z side, as a quad layer does.

#pragma once

#include <math.h>

#include "controller_math.h"
#include "overlay_math.h"

namespace hint_card_math {

// THE HAND, from the grip pose (overlay_math.h, THE HAND FRAME). The open
// fingers go along -y of the grip. The back of the right hand is at +x of the
// grip, and the left hand is the mirror image in x. The flip of z between the
// runtime's frame and the game's frame changes none of these two axes. The
// lengths are shares of the hand's length.
struct Hand {
  float wrist[3];    // the middle of the wrist
  float tip[3];      // the tip of the open middle finger
  float fingers[3];  // a unit vector from the wrist to the finger tips
};

inline void HandFromGrip(const float grip_position[3],
                         const float grip_orientation[4], bool left,
                         float hand_m, Hand* out) {
  const float minus_y[3] = {0.0f, -1.0f, 0.0f};
  const float plus_x[3] = {left ? -1.0f : 1.0f, 0.0f, 0.0f};
  float back[3];
  controller_math::QuatRotate(grip_orientation, minus_y, out->fingers);
  controller_math::QuatRotate(grip_orientation, plus_x, back);
  overlay_math::Normalize(out->fingers);
  overlay_math::Normalize(back);
  const float to_wrist =
      (overlay_math::kKnuckleY + overlay_math::kPalmLength) * hand_m;
  const float to_tip = (1.0f - overlay_math::kKnuckleY -
                        overlay_math::kPalmLength) * hand_m;
  for (int i = 0; i < 3; ++i) {
    const float palm = grip_position[i] +
                       back[i] * overlay_math::kPalmX * hand_m;
    out->wrist[i] = palm - out->fingers[i] * to_wrist;
    out->tip[i] = palm + out->fingers[i] * to_tip;
  }
}

// THE OUTLINE OF THE HAND: the corners of the palm at the wrist and at the
// knuckles, and the five finger tips of the open hand, from the test hand of
// the overlay (overlay_math.h). A point of the hand frame (x, y, z) is at
// grip + R * (x, y, -z) in the LOCAL space, because the frame of the game is
// the frame of the runtime with z the other way. The left hand has x the
// other way.
const int kOutlinePoints = 13;

inline void HandOutline(const float grip_position[3],
                        const float grip_orientation[4], bool left,
                        float hand_m, float out[kOutlinePoints][3]) {
  float local[kOutlinePoints][3];
  int n = 0;
  const float ends[2] = {
      overlay_math::kKnuckleY + overlay_math::kPalmLength,
      overlay_math::kKnuckleY - overlay_math::kPalmPast};
  for (int e = 0; e < 2; ++e) {
    for (int c = 0; c < 4; ++c) {
      local[n][0] = overlay_math::kPalmX +
                    ((c & 1) ? 1.0f : -1.0f) * overlay_math::kPalmHalfThick;
      local[n][1] = ends[e];
      local[n][2] = ((c & 2) ? 1.0f : -1.0f) * overlay_math::kPalmHalfWide;
      ++n;
    }
  }
  for (int f = 0; f < overlay_math::kFingers; ++f) {
    overlay_math::FingerTip(f, 0.0f, local[n]);
    ++n;
  }
  for (int i = 0; i < kOutlinePoints; ++i) {
    const float in[3] = {(left ? -local[i][0] : local[i][0]) * hand_m,
                         local[i][1] * hand_m, -local[i][2] * hand_m};
    float turned[3];
    controller_math::QuatRotate(grip_orientation, in, turned);
    for (int k = 0; k < 3; ++k) out[i][k] = grip_position[k] + turned[k];
  }
}

// THE PLACE OF THE CARD.
//
// The runtime draws a quad layer over the eye pictures, so the Hand cannot
// hide the card. The card must not hide the Hand, and it must not hide the
// place where the fingers work. So the card is beside the wrist, as the head
// sees it:
//
//   v     the line of sight from the head to the wrist
//   r     right in the view: level, and across v
//   ext   how far the outline of the hand goes to that side of the wrist. A
//         point nearer to the head than the wrist counts for more, as it
//         looks larger.
//
//   center = wrist + side * r * (ext + gap + width / 2)
//
// The card is at the same distance from the head as the wrist, so it has the
// depth of the hand. The fingers reach away from the head or toward it much
// more than to a side, so the side of the wrist stays free while the hand
// works.
//
// BY CONSTRUCTION: each point of the outline is at ext or less to the side,
// and the card starts at ext + gap. So the card is not in the hand in any
// pose, and in the view it is beside the hand and not on it.
//
// THE SIDE. The card starts on the outer side: right of the right hand, and
// left of the left hand. A hand at one side of the body that points ahead has
// its fingers toward the middle of the view, so the outer side is the free
// one. When the fingers point far to the side of the card, the card would go
// past the finger tips. So when the extent on its side is more than the
// extent on the other side by `hysteresis`, the card changes side, once. It
// changes back only after the same excess the other way. So a hand that
// shakes does not move the card from side to side.
//
// r has no value when the wrist is straight under or over the head. Then the
// last r is used (`last_right`), made square to v. False when that fails too.
//
// THE TURN. The card is square to the line of sight to the wrist, with no
// roll (controller_math::FacePose from the wrist). So the whole card is at
// the distance of the wrist, its width goes along r, and the two extents
// above are exact for each of its points.
const float kMostExtent = 0.25f;  // meters

struct Card {
  float center[3];
  float orientation[4];
  float right[3];  // r
  int side;        // -1 left of the wrist in the view, 1 right of it
  float extent;    // ext on that side, in meters
  float other;     // ext on the other side
  float reach;     // the distance from the wrist to the center
  bool changed;    // the card changed side on this call
};

// How far the outline goes from the wrist along `direction`, as the head
// sees it.
inline float OutlineExtent(const Hand& hand, const float outline[][3],
                           int points, const float head[3], const float v[3],
                           float distance, const float direction[3]) {
  float extent = 0.0f;
  for (int p = 0; p < points; ++p) {
    const float along = (outline[p][0] - hand.wrist[0]) * direction[0] +
                        (outline[p][1] - hand.wrist[1]) * direction[1] +
                        (outline[p][2] - hand.wrist[2]) * direction[2];
    if (along <= 0.0f) continue;
    // In the view, a point nearer than the wrist is larger by this.
    float depth = (outline[p][0] - head[0]) * v[0] +
                  (outline[p][1] - head[1]) * v[1] +
                  (outline[p][2] - head[2]) * v[2];
    if (depth < 0.05f) depth = 0.05f;
    const float seen = along * distance / depth;
    const float most = seen > along ? seen : along;
    if (most > extent) extent = most;
  }
  return extent > kMostExtent ? kMostExtent : extent;
}

// `side` is the side of the last frame, or 0 for none yet. `first` is the
// side to start on.
inline bool CardPose(const Hand& hand, const float outline[][3], int points,
                     const float head[3], float width, float gap,
                     float hysteresis, const float last_right[3], int side,
                     int first, Card* out) {
  float v[3] = {hand.wrist[0] - head[0], hand.wrist[1] - head[1],
                hand.wrist[2] - head[2]};
  const float distance = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (!(distance > 1e-4f)) return false;
  for (int i = 0; i < 3; ++i) v[i] /= distance;
  // v x up: level, and to the right of the line of sight.
  float r[3] = {-v[2], 0.0f, v[0]};
  float length = sqrtf(r[0] * r[0] + r[2] * r[2]);
  if (length < 0.1f) {
    const float along =
        last_right[0] * v[0] + last_right[1] * v[1] + last_right[2] * v[2];
    for (int i = 0; i < 3; ++i) r[i] = last_right[i] - along * v[i];
    length = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (length < 0.1f) return false;
  }
  for (int i = 0; i < 3; ++i) r[i] /= length;
  const float l[3] = {-r[0], -r[1], -r[2]};
  const float to_right =
      OutlineExtent(hand, outline, points, head, v, distance, r);
  const float to_left =
      OutlineExtent(hand, outline, points, head, v, distance, l);

  out->changed = false;
  if (side != -1 && side != 1) side = first < 0 ? -1 : 1;
  float mine = side < 0 ? to_left : to_right;
  float other = side < 0 ? to_right : to_left;
  if (mine > other + hysteresis) {
    side = -side;
    const float swap = mine;
    mine = other;
    other = swap;
    out->changed = true;
  }
  out->side = side;
  out->extent = mine;
  out->other = other;
  out->reach = mine + gap + 0.5f * width;
  for (int i = 0; i < 3; ++i) {
    out->right[i] = r[i];
    out->center[i] = hand.wrist[i] + r[i] * (side * out->reach);
  }
  controller_math::FacePose(hand.wrist, head, out->orientation);
  return true;
}

// The four corners of the card: top left, top right, bottom right, bottom
// left, as the head sees them.
inline void CardCorners(const Card& card, float width, float height,
                        float out[4][3]) {
  const float sx[4] = {-0.5f, 0.5f, 0.5f, -0.5f};
  const float sy[4] = {0.5f, 0.5f, -0.5f, -0.5f};
  for (int c = 0; c < 4; ++c) {
    const float local[3] = {sx[c] * width, sy[c] * height, 0.0f};
    float turned[3];
    controller_math::QuatRotate(card.orientation, local, turned);
    for (int i = 0; i < 3; ++i) out[c][i] = card.center[i] + turned[i];
  }
}

// True when the card is on the line from `eye` through `point`, with `margin`
// meters added on each side of the card. The runtime draws the card over the
// eye pictures, so the card covers the point when the point is behind it and
// also when the point is in front of it.
inline bool CardCovers(const Card& card, float width, float height,
                       float margin, const float eye[3],
                       const float point[3]) {
  const float inverse[4] = {-card.orientation[0], -card.orientation[1],
                            -card.orientation[2], card.orientation[3]};
  const float e_world[3] = {eye[0] - card.center[0], eye[1] - card.center[1],
                            eye[2] - card.center[2]};
  const float p_world[3] = {point[0] - card.center[0],
                            point[1] - card.center[1],
                            point[2] - card.center[2]};
  float e[3], p[3];
  controller_math::QuatRotate(inverse, e_world, e);
  controller_math::QuatRotate(inverse, p_world, p);
  // The card is the plane z = 0 of its frame. The eye is at z > 0, and the
  // line must go toward the plane.
  if (!(e[2] > 0.0f) || !(e[2] - p[2] > 1e-6f)) return false;
  const float t = e[2] / (e[2] - p[2]);
  const float x = e[0] + (p[0] - e[0]) * t;
  const float y = e[1] + (p[1] - e[1]) * t;
  return fabsf(x) <= 0.5f * width + margin &&
         fabsf(y) <= 0.5f * height + margin;
}

// True when the card is on the line of sight from `eye` to one of the
// points. For the card of one hand and the outline of the other hand: a card
// must not hide a hand.
inline bool CardCoversAny(const Card& card, float width, float height,
                          float margin, const float eye[3],
                          const float points[][3], int count) {
  for (int i = 0; i < count; ++i) {
    if (CardCovers(card, width, height, margin, eye, points[i])) return true;
  }
  return false;
}

// True when the two cards overlap in the view from `eye`: a corner or the
// center of one card is on the other card.
inline bool CardsOverlap(const Card& a, float a_width, float a_height,
                         const Card& b, float b_width, float b_height,
                         const float eye[3]) {
  float corners[4][3];
  CardCorners(b, b_width, b_height, corners);
  if (CardCoversAny(a, a_width, a_height, 0.0f, eye, corners, 4) ||
      CardCovers(a, a_width, a_height, 0.0f, eye, b.center)) {
    return true;
  }
  CardCorners(a, a_width, a_height, corners);
  return CardCoversAny(b, b_width, b_height, 0.0f, eye, corners, 4) ||
         CardCovers(b, b_width, b_height, 0.0f, eye, a.center);
}

// WHICH HINTS OF THE GAME ARE TRUE WITH THE CONTROLLERS.
//
// The game makes its hints for the mouse, and the mod gives each one the
// word of the controller control that does the work of that mouse button.
// Two of them say a thing that the control does not do:
//
//   Code 1, the left button, "Move". The game means a drag of the land. The
//   trigger drags the land only in the immersive view. In the God view
//   and on the table the picture does not come from the game camera, so the
//   drag moves nothing (comfort_camera.cpp, GodDragAfter): the grip of the
//   other hand moves the land. With the leash on, the trigger commands the
//   Creature and does not reach the game. The game's left press is the land
//   drag for each thing under the Hand (0x005D1320 is a jump to
//   fn_005D1AB0), so the hint is false over an object and over the Creature
//   too.
//
//   Code 2, the right button, over the land with the leash on. The game's
//   hint is the tooltip of the leash: a press sends the Creature to that
//   place. creature_command.cpp leaves that packet out for the grip, and the
//   trigger sends the Creature.
//
// Each other hint is true: the grip takes, plants, drops and throws as the
// right button does, it touches the Creature ("Interact"), and the stick
// zooms as the wheel does.
const int kHandModeLand = 3;
const int kHandModeCreature = 0x13;

// code: the mouse button code of the game's call (1 left, 2 right, 8 and
// 0x10 the wheel, 0 for a hint with no button). mode: the interface hand
// mode, when have_mode. trigger_drags_land: the game's land drag moves the
// view now. leash_command: the leash is on the Hand, and the trigger
// commands the Creature.
inline bool GameHintTrue(unsigned code, bool have_mode, int mode,
                         bool trigger_drags_land, bool leash_command) {
  if (code == 1) return trigger_drags_land;
  if (code == 2) {
    return !(leash_command && have_mode && mode == kHandModeLand);
  }
  return true;
}

// THE SIZE. The card texture has a fixed density, so a letter has one size in
// the room. A card wider than `most` meters becomes `most` wide, and its
// height keeps the aspect.
inline void CardSize(int pixels_w, int pixels_h, float pixels_per_meter,
                     float most, float* width, float* height) {
  *width = 0.0f;
  *height = 0.0f;
  if (pixels_w <= 0 || pixels_h <= 0 || !(pixels_per_meter > 0.0f)) return;
  float w = static_cast<float>(pixels_w) / pixels_per_meter;
  float h = static_cast<float>(pixels_h) / pixels_per_meter;
  if (most > 0.0f && w > most) {
    h *= most / w;
    w = most;
  }
  *width = w;
  *height = h;
}

// WHEN A PROMPT SHOWS.
//
// A prompt belongs to a context: what the hand can do now. A prompt must
// help and must not stay in the way, so:
//
//   1. The context must hold for `dwell` seconds before its prompt shows. A
//      hand that moves across objects shows nothing.
//   2. The prompt shows for `hold` seconds at most. Then it goes, and it
//      stays away until the context ends and starts again.
//   3. A context shows its prompt `most` times in a session. After that the
//      player knows it.
//
// With `always`, rules 2 and 3 do not apply (prompts=2).
const int kMaxContexts = 16;

struct PromptRule {
  float dwell;  // seconds
  float hold;   // seconds
  int most;     // shows in a session
};

struct PromptState {
  int context = 0;     // 0 is no context
  double since = 0.0;  // when the context started
  bool showing = false;
  double shown_at = 0.0;
  int shows[kMaxContexts] = {0};
};

// One frame. Returns the context whose prompt shows now, or 0.
inline int PromptStep(PromptState* s, int context, double now,
                      const PromptRule& rule, bool always) {
  if (context < 0 || context >= kMaxContexts) context = 0;
  if (context != s->context) {
    s->context = context;
    s->since = now;
    s->showing = false;
  }
  if (context == 0) return 0;
  if (!s->showing) {
    const bool seen_all = !always && s->shows[context] >= rule.most;
    // A prompt that ended does not start again in the same context.
    const bool ended = s->shown_at >= s->since && s->shows[context] > 0 &&
                       !always;
    if (!seen_all && !ended && now - s->since >= rule.dwell) {
      s->showing = true;
      s->shown_at = now;
      ++s->shows[context];
    }
  } else if (!always && now - s->shown_at > rule.hold) {
    s->showing = false;
  }
  return s->showing ? context : 0;
}

// The fade of the card: toward 1 while it shows and toward 0 while it does
// not, in `seconds` for the whole way. The card does not move while it
// fades.
inline float FadeStep(float fade, bool show, float dt, float seconds) {
  if (!(seconds > 0.0f)) return show ? 1.0f : 0.0f;
  if (!(dt > 0.0f)) return fade;
  const float step = dt / seconds;
  fade += show ? step : -step;
  if (fade < 0.0f) fade = 0.0f;
  if (fade > 1.0f) fade = 1.0f;
  return fade;
}

}  // namespace hint_card_math
