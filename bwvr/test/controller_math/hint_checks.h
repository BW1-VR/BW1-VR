// THE HINT CARD (hint_card_math.h). main.cpp includes this file after Check
// and Near, just before main.
#pragma once

#include "../../src/mod/hint_card_math.h"
#include "../../src/mod/subtitle_card_math.h"

namespace hint_checks {

using namespace hint_card_math;
// main.cpp has a Hand of its own.
typedef hint_card_math::Hand CardHand;
typedef hint_card_math::Card CardPlace;

const float kHandM = 0.19f;
const float kWide = 0.10f;
const float kHigh = 0.03f;
const float kGapM = 0.03f;
const float kExcess = 0.04f;

// A unit quaternion from yaw (about y), pitch (about x) and roll (about z),
// in degrees, as a controller turns.
inline void Turn(float yaw_deg, float pitch_deg, float roll_deg, float q[4]) {
  const float k = 0.5f / 57.29578f;
  const float y[4] = {0.0f, sinf(yaw_deg * k), 0.0f, cosf(yaw_deg * k)};
  const float x[4] = {sinf(pitch_deg * k), 0.0f, 0.0f, cosf(pitch_deg * k)};
  const float z[4] = {0.0f, 0.0f, sinf(roll_deg * k), cosf(roll_deg * k)};
  float yx[4];
  controller_math::QuatMultiply(y, x, yx);
  controller_math::QuatMultiply(yx, z, q);
}

// A small generator of the same numbers on each run.
inline float Random(uint32_t* state) {
  *state = *state * 1664525u + 1013904223u;
  return static_cast<float>((*state >> 8) & 0xFFFF) / 65535.0f;
}

// The hand, its outline and the card for one pose. False when the card has
// no place.
struct Posed {
  CardHand hand;
  float outline[kOutlinePoints][3];
  CardPlace card;
};

inline bool Pose(const float grip[3], const float q[4], bool left,
                 const float head[3], float wide, const float last_right[3],
                 int side, Posed* out) {
  HandFromGrip(grip, q, left, kHandM, &out->hand);
  HandOutline(grip, q, left, kHandM, out->outline);
  return CardPose(out->hand, out->outline, kOutlinePoints, head, wide, kGapM,
                  kExcess, last_right, side, left ? -1 : 1, &out->card);
}

// The least distance from a point to the card, a flat rect.
inline double PointToCard(const CardPlace& card, float wide, float high,
                          const float point[3]) {
  const float inverse[4] = {-card.orientation[0], -card.orientation[1],
                            -card.orientation[2], card.orientation[3]};
  const float world[3] = {point[0] - card.center[0], point[1] - card.center[1],
                          point[2] - card.center[2]};
  float p[3];
  controller_math::QuatRotate(inverse, world, p);
  const double dx = fabs(p[0]) > 0.5 * wide ? fabs(p[0]) - 0.5 * wide : 0.0;
  const double dy = fabs(p[1]) > 0.5 * high ? fabs(p[1]) - 0.5 * high : 0.0;
  return sqrt(dx * dx + dy * dy + static_cast<double>(p[2]) * p[2]);
}

// True when the card covers a point of the hand in an eye: the outline, or
// the line from the wrist to the finger tips.
inline bool Covers(const Posed& p, float wide, float high) {
  for (int eye = 0; eye < 2; ++eye) {
    const float at[3] = {eye == 0 ? -0.032f : 0.032f, 0.0f, 0.0f};
    for (int k = 0; k <= 10; ++k) {
      const float t = k / 10.0f;
      const float on_line[3] = {
          p.hand.wrist[0] + (p.hand.tip[0] - p.hand.wrist[0]) * t,
          p.hand.wrist[1] + (p.hand.tip[1] - p.hand.wrist[1]) * t,
          p.hand.wrist[2] + (p.hand.tip[2] - p.hand.wrist[2]) * t};
      if (CardCovers(p.card, wide, high, 0.005f, at, on_line)) return true;
    }
    for (int i = 0; i < kOutlinePoints; ++i) {
      if (CardCovers(p.card, wide, high, 0.005f, at, p.outline[i])) {
        return true;
      }
    }
  }
  return false;
}

// THE SUBTITLE CARD (subtitle_card_math.h).
inline void SubtitleCardChecks() {
  printf("\nThe subtitle card\n");
  using namespace subtitle_math;

  // WHY THE SUBTITLES WENT APART, with the numbers of a render of 2880
  // pixels for 110 degrees, and eyes 65 mm apart.
  const float focal = 1440.0f / tanf(55.0f * kDegrees);
  const float eyes = 0.065f;
  Near("the focal length of the render, px", focal, 1008.3, 0.2);
  Near("a shift of 24 px, the ceiling, puts the subtitles at 1.37 m",
       DepthOfShift(focal, eyes, 24.0f), 1.365, 0.01);
  Near("a shift of 8 px, the resting shift, puts them at 4.10 m",
       DepthOfShift(focal, eyes, 8.0f), 4.096, 0.01);
  Check(DepthOfShift(focal, eyes, 0.0f) > 1e8f,
        "no shift puts them at a depth with no end", 1, 1);
  // The Hand's point at x 1245 in the left eye and 1091 in the right eye is
  // "0.43 m". That is a shift of 77 px in each eye.
  Near("the Hand's point of frame 1078 (154 px apart) is at 0.43 m",
       DepthOfShift(focal, eyes, 77.0f), 0.426, 0.01);
  Near("with the eyes on that Hand, the subtitles at 1.37 m are 6.0 degrees "
       "apart", SplitDegrees(eyes, 1.365f, 0.426f), 6.01, 0.1);
  Near("and at 4.10 m they are 7.8 degrees apart",
       SplitDegrees(eyes, 4.096f, 0.426f), 7.83, 0.1);
  // The land under the subtitles, 35 degrees below the middle of the view,
  // with the head level and the land 0.6 m below it: 1.05 m away.
  const float land = 0.6f / sinf(35.0f * kDegrees);
  Near("the land under the subtitles is 1.05 m away", land, 1.046, 0.01);
  Check(DepthOfShift(focal, eyes, 24.0f) > land,
        "at the ceiling the subtitles are behind the land that they cover",
        DepthOfShift(focal, eyes, 24.0f), land);
  Near("with the eyes on that land, the subtitles at 4.10 m are 2.7 degrees "
       "apart", SplitDegrees(eyes, 4.096f, land), 2.65, 0.1);
  // The fallback: a fixed depth of 0.75 m is in front of that land.
  Near("the fixed shift of the fallback at 0.75 m, px",
       ShiftOfDepth(focal, 0.064f, 0.75f), 43.0, 0.5);
  Check(0.75f < land * cosf(35.0f * kDegrees),
        "the fallback depth is in front of the land under the subtitles",
        0.75, land * cosf(35.0f * kDegrees));
  Near("a depth and its shift agree",
       DepthOfShift(focal, eyes, ShiftOfDepth(focal, eyes, 1.2f)), 1.2, 1e-4);

  // THE YAW OF THE HEAD.
  {
    float q[4];
    Turn(0.0f, 0.0f, 0.0f, q);
    Near("a head with no turn has a yaw of 0", HeadYaw(q), 0.0, 1e-6);
    Turn(90.0f, 0.0f, 0.0f, q);
    Near("a head turned 90 degrees to the left", HeadYaw(q) / kDegrees, 90.0,
         1e-3);
    Turn(-135.0f, 0.0f, 0.0f, q);
    Near("a head turned 135 degrees to the right", HeadYaw(q) / kDegrees,
         -135.0, 1e-3);
    Turn(30.0f, -80.0f, 0.0f, q);
    Near("a head that looks 80 degrees down keeps its yaw of 30",
         HeadYaw(q) / kDegrees, 30.0, 1e-2);
    Turn(30.0f, -90.0f, 0.0f, q);
    Near("a head that looks straight down keeps it too",
         HeadYaw(q) / kDegrees, 30.0, 1e-2);
    Near("170 and -170 degrees are 20 degrees apart",
         YawBetween(170.0f * kDegrees, -170.0f * kDegrees) / kDegrees, 20.0,
         1e-3);
  }

  // THE ANCHOR. The recenter pose is at the origin with a yaw of 0.
  const AnchorRule rule;
  const float origin[3] = {0.0f, 1.2f, 0.0f};
  {
    Anchor a;
    int event = AnchorStep(&a, true, origin, 0.0f, 1, origin, 0.0f, false,
                           0.0, rule);
    Check(event == kAnchorAtRecenter && a.have,
          "the anchor starts at the recenter pose", event, kAnchorAtRecenter);
    // THE CARD DOES NOT FOLLOW THE HEAD. The head turns from 0 to 49 degrees
    // and back, leans 40 cm and looks down, for 20 s. The anchor stays.
    int moves = 0;
    for (int i = 0; i < 1800; ++i) {
      const float t = i / 90.0f;
      const float yaw = 49.0f * kDegrees * sinf(t * 0.7f);
      const float head[3] = {0.4f * sinf(t), 1.2f - 0.1f * sinf(t * 1.3f),
                             0.3f * cosf(t * 0.5f) - 0.3f};
      if (AnchorStep(&a, true, origin, 0.0f, 1, head, yaw, true, t, rule) !=
          kAnchorKept) {
        ++moves;
      }
    }
    Check(moves == 0 && a.yaw == 0.0f && a.head[0] == 0.0f,
          "a head that turns 49 degrees and leans 40 cm for 20 s moves no "
          "card", moves, 0);
    // A look of 0.9 s at 80 degrees, and back: the card stays.
    double t = 30.0;
    moves = 0;
    for (int i = 0; i < 81; ++i, t += 1.0 / 90.0) {
      if (AnchorStep(&a, true, origin, 0.0f, 1, origin, 80.0f * kDegrees, true,
                     t, rule) != kAnchorKept) {
        ++moves;
      }
    }
    for (int i = 0; i < 30; ++i, t += 1.0 / 90.0) {
      AnchorStep(&a, true, origin, 0.0f, 1, origin, 0.0f, true, t, rule);
    }
    Check(moves == 0 && a.yaw == 0.0f,
          "a look of 0.9 s to the side at 80 degrees moves no card", moves, 0);
    // The head stays at 80 degrees: the card goes there after 1 s, one time.
    moves = 0;
    double moved_at = 0.0;
    const double start = t;
    for (int i = 0; i < 270; ++i, t += 1.0 / 90.0) {
      if (AnchorStep(&a, true, origin, 0.0f, 1, origin, 80.0f * kDegrees, true,
                     t, rule) == kAnchorMoved) {
        ++moves;
        moved_at = t - start;
      }
    }
    Check(moves == 1, "a head that stays at 80 degrees moves the card one "
          "time in 3 s", moves, 1);
    Near("after 1 s", moved_at, 1.0, 0.02);
    Near("to the yaw of the head, degrees", a.yaw / kDegrees, 80.0, 1e-3);
    // The card is never lost behind the player: a turn of 180 degrees.
    for (int i = 0; i < 100; ++i, t += 1.0 / 90.0) {
      AnchorStep(&a, true, origin, 0.0f, 1, origin, -100.0f * kDegrees, true,
                 t, rule);
    }
    Near("a turn of 180 degrees: the card is ahead again after 1.1 s",
         a.yaw / kDegrees, -100.0, 1e-3);
  }
  {
    // With no card in the view, an away head takes the anchor at once.
    Anchor a;
    AnchorStep(&a, true, origin, 0.0f, 1, origin, 0.0f, false, 0.0, rule);
    const int event = AnchorStep(&a, true, origin, 0.0f, 1, origin,
                                 120.0f * kDegrees, false, 0.1, rule);
    Check(event == kAnchorMoved, "no card in the view: the anchor goes with "
          "an away head at once", event, kAnchorMoved);
    Near("to the yaw of the head", a.yaw / kDegrees, 120.0, 1e-3);
    // A head that walks 1.2 m away, with the card in the view.
    const float walked[3] = {1.2f, 1.2f, 0.0f};
    int moves = 0;
    for (int i = 0; i < 100; ++i) {
      if (AnchorStep(&a, true, origin, 0.0f, 1, walked, 120.0f * kDegrees,
                     true, 1.0 + i / 90.0, rule) == kAnchorMoved) {
        ++moves;
      }
    }
    Check(moves == 1 && a.head[0] == 1.2f,
          "a head that walks 1.2 m away moves the card after 1 s", moves, 1);
    // A player who stands up: 45 cm higher.
    const float stood[3] = {1.2f, 1.65f, 0.0f};
    moves = 0;
    for (int i = 0; i < 100; ++i) {
      if (AnchorStep(&a, true, origin, 0.0f, 1, stood, 120.0f * kDegrees, true,
                     3.0 + i / 90.0, rule) == kAnchorMoved) {
        ++moves;
      }
    }
    Check(moves == 1 && a.head[1] == 1.65f,
          "a player who stands up moves the card after 1 s", moves, 1);
    // A recenter puts the anchor at the new recenter pose.
    const float fresh[3] = {0.5f, 1.3f, 0.2f};
    const int at = AnchorStep(&a, true, fresh, 45.0f * kDegrees, 2, fresh,
                              45.0f * kDegrees, true, 9.0, rule);
    Check(at == kAnchorAtRecenter && a.head[0] == 0.5f && a.serial == 2,
          "a recenter puts the card at the new recenter pose", at,
          kAnchorAtRecenter);
    // With no recenter pose, the first head is the anchor.
    Anchor b;
    Check(AnchorStep(&b, false, origin, 0.0f, 0, walked, 1.0f, false, 0.0,
                     rule) == kAnchorMoved && b.head[0] == 1.2f,
          "with no recenter pose the first head is the anchor", 1, 1);
  }

  // THE CARD NEVER SLIDES. A change of the anchor while the card shows: the
  // card fades out at the old place, and fades in at the new one.
  {
    Anchor a;
    AnchorStep(&a, true, origin, 0.0f, 1, origin, 0.0f, false, 0.0, rule);
    Shown s;
    for (int i = 0; i < 30; ++i) ShownStep(&s, a, true, 1.0f / 90.0f, 0.2f);
    Near("a card with content fades in to 1", s.fade, 1.0, 1e-6);
    Near("at the anchor", s.yaw, a.yaw, 0.0);
    Anchor moved = a;
    const float there[3] = {0.3f, 1.2f, -0.1f};
    AnchorPlace(&moved, there, 80.0f * kDegrees);
    int at_old = 0, between = 0;
    float least = 1.0f;
    bool jumped = false;
    for (int i = 0; i < 60; ++i) {
      ShownStep(&s, moved, true, 1.0f / 90.0f, 0.2f);
      const bool old_place = s.yaw == 0.0f && s.head[0] == 0.0f;
      const bool new_place = ShownAt(s, moved);
      if (!old_place && !new_place) ++between;
      if (old_place) ++at_old;
      if (new_place && !jumped) {
        jumped = true;
        least = s.fade;
      }
    }
    Check(between == 0, "the card is at the old place or at the new one, "
          "never between them", between, 0);
    Check(at_old >= 17 && at_old <= 18,
          "it fades out for 0.2 s at the old place", at_old, 18);
    Near("it takes the new place while it is clear", least, 0.0, 1e-6);
    Near("and it fades in there", s.fade, 1.0, 1e-6);
    // With no content the card takes the new place at once.
    Shown hidden;
    ShownStep(&hidden, a, false, 1.0f / 90.0f, 0.2f);
    ShownStep(&hidden, moved, false, 1.0f / 90.0f, 0.2f);
    Check(ShownAt(hidden, moved) && hidden.fade == 0.0f,
          "a card with no content takes a new place at once", 1, 1);
  }

  // THE SCREEN IN THE ROOM. A render of 2880 by 2880 pixels is 1 m wide, and
  // the row at 93 percent of its height is 1.6 m from the head, 17 degrees
  // below eye level.
  {
    const Layout layout;
    const float head[3] = {0.2f, 1.2f, 0.1f};
    const float render = 2880.0f;
    // A strip at the bottom: the whole width, 364 px high, with its middle
    // on the reference row.
    const float ref_row = layout.ref_y * render;
    const float strip[4] = {0.0f, ref_row - 182.0f, render, 364.0f};
    Quad q;
    RectQuad(head, 0.0f, layout, render, render, strip, &q);
    Near("the subtitle strip is 1.6 m from the head, m",
         controller_math::Distance(q.position, head), 1.6, 1e-4);
    Near("17 degrees below eye level",
         asinf((head[1] - q.position[1]) / 1.6f) / kDegrees, 17.0, 1e-2);
    Near("straight ahead of the anchor", q.position[0] - head[0], 0.0, 1e-5);
    Near("the strip is 1 m wide", q.width, 1.0, 1e-5);
    Near("and 12.6 cm high", q.height, 0.1264, 1e-4);
    Check(head[1] - q.position[1] < 0.6f,
          "the strip is above a land that is 0.6 m below the head, m",
          head[1] - q.position[1], 0.6);
    const double wide_deg = 2.0 * atan(0.5 / 1.6) * 57.29578;
    Near("the strip is 34.7 degrees wide", wide_deg, 34.7, 0.1);
    const double letter_deg = atan(77.0 / render * 1.0 / 1.6) * 57.29578;
    Near("a letter of 77 px is 0.96 degrees high", letter_deg, 0.96, 0.02);
    // The front of the quad faces the head.
    const float front[3] = {0.0f, 0.0f, 1.0f};
    float normal[3];
    controller_math::QuatRotate(q.orientation, front, normal);
    const float to_head[3] = {(head[0] - q.position[0]) / 1.6f,
                              (head[1] - q.position[1]) / 1.6f,
                              (head[2] - q.position[2]) / 1.6f};
    Near("the card faces the head",
         normal[0] * to_head[0] + normal[1] * to_head[1] +
             normal[2] * to_head[2], 1.0, 1e-5);
    // A PIXEL HAS ONE PLACE IN THE ROOM, in each rect that holds it. The
    // pixel 700, 2700 in a small rect and in a large one.
    const float small_rect[4] = {640.0f, 2640.0f, 160.0f, 96.0f};
    const float large_rect[4] = {0.0f, 720.0f, 2880.0f, 2160.0f};
    float place[2][3];
    for (int k = 0; k < 2; ++k) {
      const float* r = k == 0 ? small_rect : large_rect;
      Quad rq;
      RectQuad(head, 0.0f, layout, render, render, r, &rq);
      // The pixel as a share of the quad, from its center.
      const float local[3] = {(700.0f - (r[0] + 0.5f * r[2])) / r[2] * rq.width,
                              ((r[1] + 0.5f * r[3]) - 2700.0f) / r[3] *
                                  rq.height,
                              0.0f};
      float turned[3];
      controller_math::QuatRotate(rq.orientation, local, turned);
      for (int i = 0; i < 3; ++i) place[k][i] = rq.position[i] + turned[i];
    }
    Near("a pixel is at one place in the room in each rect, m",
         controller_math::Distance(place[0], place[1]), 0.0, 1e-5);
    // The Creature panel, at the left of the render, is left of the middle
    // and above the subtitles.
    const float creature[4] = {64.0f, 712.0f, 736.0f, 976.0f};
    Quad cq;
    RectQuad(head, 0.0f, layout, render, render, creature, &cq);
    Check(cq.position[0] < head[0] - 0.3f && cq.position[1] > q.position[1],
          "the Creature panel is left of the middle and above the subtitles",
          cq.position[0] - head[0], -0.35);
    // A yaw of 90 degrees to the left puts the card at -x.
    RectQuad(head, 90.0f * kDegrees, layout, render, render, strip, &q);
    Near("an anchor that faces left has its card at the left, m",
         q.position[0] - head[0], -1.6f * cosf(17.0f * kDegrees), 1e-4);
  }

  // THE RECT OF THE CARD: the bounds, 8 px of room, on a grid of 16 px.
  {
    const float bounds[4] = {403.2f, 2511.7f, 2476.9f, 2790.1f};
    int rect[4];
    Check(CardRect(bounds, 8.0f, 16, 2880, 2880, rect),
          "bounds give a rect", 1, 1);
    Check(rect[0] == 384 && rect[1] == 2496 && rect[2] == 2112 &&
              rect[3] == 304,
          "the rect holds the bounds, on the grid", rect[2], 2112);
    Check(rect[0] <= 395 && rect[1] <= 2503 && rect[0] + rect[2] >= 2485 &&
              rect[1] + rect[3] >= 2799,
          "with 8 px of room on each side", 1, 1);
    // A letter that comes later changes no edge that it is inside.
    const float more[4] = {403.2f, 2511.7f, 2480.0f, 2790.1f};
    int again[4];
    CardRect(more, 8.0f, 16, 2880, 2880, again);
    Check(again[0] == rect[0] && again[2] == rect[2],
          "3 px more of text keep the rect", again[2], rect[2]);
    const float outside[4] = {-50.0f, 2700.0f, 3000.0f, 2950.0f};
    CardRect(outside, 8.0f, 16, 2880, 2880, rect);
    Check(rect[0] == 0 && rect[2] == 2880 && rect[1] + rect[3] == 2880,
          "bounds past the render stop at the render", rect[2], 2880);
    const float none[4] = {100.0f, 100.0f, 100.0f, 100.0f};
    Check(!CardRect(none, 0.0f, 16, 2880, 2880, rect) ||
              (rect[2] >= 2 && rect[3] >= 2),
          "bounds with no size give no rect, or a rect of the grid", 1, 1);
    const float off[4] = {5000.0f, 5000.0f, 5100.0f, 5100.0f};
    Check(!CardRect(off, 8.0f, 16, 2880, 2880, rect),
          "bounds off the render give no rect", 0, 0);
  }
}

inline void HintCardChecks() {
  printf("\nThe hint card\n");
  const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  const float east[3] = {1.0f, 0.0f, 0.0f};
  const float origin[3] = {0.0f, 0.0f, 0.0f};

  // THE HAND. With no turn, the fingers go along -y of the grip, and the back
  // of the right hand is at +x.
  {
    const float grip[3] = {0.20f, -0.30f, -0.40f};
    CardHand right, left;
    HandFromGrip(grip, identity, false, kHandM, &right);
    HandFromGrip(grip, identity, true, kHandM, &left);
    Near("the fingers of a grip with no turn point down", right.fingers[1],
         -1.0, 1e-6);
    Near("the wrist is 9.3 cm over the grip, m", right.wrist[1] - grip[1],
         0.49 * kHandM, 1e-6);
    Near("the wrist of the right hand is 2.5 cm to its back, m",
         right.wrist[0] - grip[0], 0.13 * kHandM, 1e-6);
    Near("the left hand is the mirror image, m", left.wrist[0] - grip[0],
         -0.13 * kHandM, 1e-6);
    Near("the wrist to the finger tips is the length of the hand, m",
         controller_math::Distance(right.wrist, right.tip), kHandM, 1e-6);
    // The outline: the tip of the open middle finger is as far along the
    // fingers as the tip of the hand, and the index side is at -z of the grip.
    float outline[kOutlinePoints][3];
    HandOutline(grip, identity, false, kHandM, outline);
    Near("the outline has the tip of the open middle finger, m",
         outline[10][1], right.tip[1], 1e-6);
    Check(outline[9][2] < grip[2] && outline[12][2] > grip[2],
          "the index finger is at -z of the grip and the little finger at +z",
          outline[9][2] - grip[2], -0.01);
  }

  // THE PLACE. The reach of play: the right hand ahead and under the head,
  // with the fingers ahead and level. The card is right of the wrist, level
  // with it, at its distance from the head.
  {
    float q[4];
    Turn(0.0f, 90.0f, 0.0f, q);  // -y of the grip turns to -z, ahead
    const float grip[3] = {0.15f, -0.40f, -0.45f};
    Posed p;
    const bool ok = Pose(grip, q, false, origin, kWide, east, 0, &p);
    Check(ok, "a wrist ahead of the head has a card", ok, 1.0);
    Near("a pitch of 90 degrees points the fingers ahead", p.hand.fingers[2],
         -1.0, 1e-5);
    Near("the card of the right hand is on the right", p.card.side, 1.0, 0.0);
    Check(p.card.center[0] > p.hand.wrist[0] + 0.07f,
          "the card center is more than 7 cm right of the wrist, m",
          p.card.center[0] - p.hand.wrist[0], 0.07);
    Near("the card is level with the wrist, m", p.card.center[1],
         p.hand.wrist[1], 1e-6);
    Near("the card center is the extent, the gap and half the card from the "
         "wrist, m", p.card.reach, p.card.extent + kGapM + 0.5 * kWide, 1e-6);
    Check(p.card.extent > 0.01f && p.card.extent < 0.03f,
          "the hand goes 1 to 3 cm to the right of its wrist, m",
          p.card.extent, 0.02);
    Check(p.card.other > p.card.extent + 0.03f,
          "the fingers lean to the middle of the view, the other side, m",
          p.card.other, p.card.extent + 0.03);
    Near("the card is at the distance of the wrist from the head, m",
         controller_math::Distance(p.card.center, origin),
         sqrt(pow(controller_math::Distance(p.hand.wrist, origin), 2.0) +
              pow(static_cast<double>(p.card.reach), 2.0)), 1e-5);
    // The left hand at the mirror place has the mirror card.
    float ql[4];
    Turn(0.0f, 90.0f, 0.0f, ql);
    const float grip_left[3] = {-0.15f, -0.40f, -0.45f};
    Posed m;
    Pose(grip_left, ql, true, origin, kWide, east, 0, &m);
    Near("the card of the left hand is on the left", m.card.side, -1.0, 0.0);
    Near("and it is the mirror image, m", m.card.center[0],
         -p.card.center[0], 1e-5);
  }

  // NEVER IN THE HAND, in any pose. 4000 poses of both hands, with the head
  // anywhere near: no point of the outline is near the card, the card starts
  // the gap past the hand, it is at the distance of the wrist, and it faces
  // the head.
  {
    uint32_t seed = 12345u;
    double least = 1e9, worst_face = 1.0, worst_roll = 0.0, worst_depth = 0.0;
    double least_side = 1e9, worst_sight = 1.0;
    int placed = 0;
    for (int n = 0; n < 4000; ++n) {
      float q[4];
      Turn(Random(&seed) * 360.0f - 180.0f, Random(&seed) * 360.0f - 180.0f,
           Random(&seed) * 360.0f - 180.0f, q);
      const float grip[3] = {Random(&seed) * 1.2f - 0.6f,
                             Random(&seed) * 1.2f - 0.9f,
                             -0.20f - Random(&seed) * 0.6f};
      const float head[3] = {Random(&seed) * 0.2f - 0.1f,
                             Random(&seed) * 0.2f - 0.1f,
                             Random(&seed) * 0.2f - 0.1f};
      const bool left = (n & 1) != 0;
      const float wide = 0.04f + Random(&seed) * 0.08f;
      const float high = 0.017f + Random(&seed) * 0.04f;
      Posed p;
      if (!Pose(grip, q, left, head, wide, east, 0, &p)) continue;
      ++placed;
      for (int i = 0; i < kOutlinePoints; ++i) {
        const double d = PointToCard(p.card, wide, high, p.outline[i]);
        if (d < least) least = d;
      }
      // To its side, the card starts at the extent and the gap.
      float corners[4][3];
      CardCorners(p.card, wide, high, corners);
      const double wrist_far = controller_math::Distance(p.hand.wrist, head);
      for (int c = 0; c < 4; ++c) {
        double along = 0.0, depth = 0.0;
        for (int k = 0; k < 3; ++k) {
          along += (corners[c][k] - p.hand.wrist[k]) * p.card.right[k] *
                   p.card.side;
          depth += (corners[c][k] - head[k]) * (p.hand.wrist[k] - head[k]);
        }
        if (along - p.card.extent < least_side) {
          least_side = along - p.card.extent;
        }
        // Each corner is as far along the line of sight as the wrist.
        depth = depth / wrist_far - wrist_far;
        if (fabs(depth) > worst_depth) worst_depth = fabs(depth);
      }
      const float front_local[3] = {0.0f, 0.0f, 1.0f};
      const float right_local[3] = {1.0f, 0.0f, 0.0f};
      float front[3], right[3];
      controller_math::QuatRotate(p.card.orientation, front_local, front);
      controller_math::QuatRotate(p.card.orientation, right_local, right);
      float to_head[3] = {head[0] - p.card.center[0],
                          head[1] - p.card.center[1],
                          head[2] - p.card.center[2]};
      overlay_math::Normalize(to_head);
      const double face = front[0] * to_head[0] + front[1] * to_head[1] +
                          front[2] * to_head[2];
      if (face < worst_face) worst_face = face;
      float sight[3] = {head[0] - p.hand.wrist[0], head[1] - p.hand.wrist[1],
                        head[2] - p.hand.wrist[2]};
      overlay_math::Normalize(sight);
      const double square = front[0] * sight[0] + front[1] * sight[1] +
                            front[2] * sight[2];
      if (square < worst_sight) worst_sight = square;
      if (fabs(right[1]) > worst_roll) worst_roll = fabs(right[1]);
    }
    Check(placed == 4000, "4000 poses: each one has a card", placed, 4000.0);
    Check(least >= kGapM - 1e-4,
          "4000 poses: no point of the outline of the hand is nearer than "
          "3 cm to the card, m", least, kGapM);
    Check(least_side >= kGapM - 1e-4,
          "4000 poses: each corner of the card is 3 cm or more past the "
          "hand, to the side, m", least_side, kGapM);
    Near("4000 poses: each corner of the card is at the depth of the wrist, m",
         worst_depth, 0.0, 1e-5);
    Check(worst_sight > 0.99999,
          "4000 poses: the card is square to the line of sight to the wrist "
          "(cosine)", worst_sight, 1.0);
    // A hand at the face with a wide card is the worst case.
    Check(worst_face > 0.40,
          "4000 poses: the front of the card is on the side of the head "
          "(cosine)", worst_face, 0.40);
    Near("4000 poses: the card has no roll", worst_roll, 0.0, 1e-5);
  }

  // THE CARD DOES NOT COVER THE HAND in the view, and it stays near the
  // wrist. The poses of play: the hand 25 to 60 cm ahead and 20 to 50 cm
  // under the head, with the fingers ahead and down, turned to each side and
  // rolled.
  {
    int covered = 0, poses = 0, past_tips = 0;
    double most_reach = 0.0, least_reach = 1e9, worst_face = 1.0;
    for (int yaw = -40; yaw <= 40; yaw += 20) {
      for (int pitch = 0; pitch <= 80; pitch += 20) {
        for (int roll = -60; roll <= 60; roll += 30) {
          for (int ahead = 25; ahead <= 60; ahead += 7) {
            for (int down = 20; down <= 50; down += 10) {
              float q[4];
              // pitch 90 is level and ahead. Less is more down.
              Turn(static_cast<float>(yaw), 90.0f - pitch,
                   static_cast<float>(roll), q);
              const float grip[3] = {0.15f, -down * 0.01f, -ahead * 0.01f};
              Posed p;
              if (!Pose(grip, q, false, origin, kWide, east, 0, &p)) continue;
              ++poses;
              if (p.card.reach > most_reach) most_reach = p.card.reach;
              if (p.card.reach < least_reach) least_reach = p.card.reach;
              if (Covers(p, kWide, kHigh)) ++covered;
              const float front_local[3] = {0.0f, 0.0f, 1.0f};
              float front[3];
              controller_math::QuatRotate(p.card.orientation, front_local,
                                          front);
              float to_head[3] = {-p.card.center[0], -p.card.center[1],
                                  -p.card.center[2]};
              overlay_math::Normalize(to_head);
              const double face = front[0] * to_head[0] +
                                  front[1] * to_head[1] +
                                  front[2] * to_head[2];
              if (face < worst_face) worst_face = face;
              // The card must stay at the wrist, not go past the fingers.
              if (p.card.extent > 0.10f) ++past_tips;
            }
          }
        }
      }
    }
    Check(poses == 5 * 5 * 5 * 6 * 4, "the count of the poses of play", poses,
          3000.0);
    Check(covered == 0,
          "the poses of play: the card covers no point of the hand in an eye "
          "(poses that cover)", covered, 0.0);
    Check(past_tips == 0,
          "the poses of play: the hand never goes more than 10 cm to the side "
          "of the card (poses)", past_tips, 0.0);
    Check(worst_face > 0.85,
          "the poses of play: the front of the card is less than 32 degrees "
          "from the head (cosine)", worst_face, 0.85);
    Check(least_reach > 0.08 && most_reach < 0.19,
          "the poses of play: the card center is 8 to 19 cm from the wrist, "
          "m (the most)", most_reach, 0.19);
    printf("        the card center is %.1f to %.1f cm from the wrist\n",
           least_reach * 100.0, most_reach * 100.0);
  }

  // THE SIDE. The right hand turns its fingers to the right in steps of 1
  // degree, from 40 degrees left to 80 degrees right, and back. The card
  // changes side once on the way, and once on the way back, at a different
  // angle. Between the changes it moves less than 1 cm for each degree.
  {
    const float grip[3] = {0.15f, -0.40f, -0.45f};
    int side = 0, changes_out = 0, changes_back = 0;
    int angle_out = 0, angle_back = 0;
    double most_step = 0.0;
    float before[3] = {0.0f, 0.0f, 0.0f};
    bool have_before = false;
    for (int pass = 0; pass < 2; ++pass) {
      for (int i = 0; i <= 120; ++i) {
        const int yaw = pass == 0 ? -40 + i : 80 - i;  // to the right
        float q[4];
        Turn(static_cast<float>(-yaw), 60.0f, 0.0f, q);
        Posed p;
        Pose(grip, q, false, origin, kWide, east, side, &p);
        float now[3];
        for (int k = 0; k < 3; ++k) now[k] = p.card.center[k] - p.hand.wrist[k];
        if (p.card.changed) {
          if (pass == 0) {
            ++changes_out;
            angle_out = yaw;
          } else {
            ++changes_back;
            angle_back = yaw;
          }
        } else if (have_before) {
          const double moved = controller_math::Distance(now, before);
          if (moved > most_step) most_step = moved;
        }
        for (int k = 0; k < 3; ++k) before[k] = now[k];
        have_before = true;
        side = p.card.side;
      }
    }
    Check(changes_out == 1,
          "fingers that turn to the right: the card changes side one time",
          changes_out, 1.0);
    Check(changes_back == 1,
          "fingers that turn back: the card changes back one time",
          changes_back, 1.0);
    Check(angle_out > angle_back + 10,
          "the two changes are more than 10 degrees apart, so a hand that "
          "shakes does not move the card (degrees out)", angle_out,
          angle_back + 10.0);
    Check(angle_out >= 5 && angle_out <= 45,
          "the card changes side when the fingers point 5 to 45 degrees to "
          "its side (degrees)", angle_out, 25.0);
    Check(most_step < 0.01,
          "between the changes, 1 degree of turn moves the card less than "
          "1 cm from the wrist, m", most_step, 0.01);
    // A hand that shakes 3 degrees about straight ahead: no change.
    side = 0;
    int shakes = 0;
    for (int i = 0; i < 200; ++i) {
      float q[4];
      Turn(3.0f * sinf(i * 0.7f), 60.0f + 3.0f * cosf(i * 0.9f),
           4.0f * sinf(i * 1.3f), q);
      Posed p;
      Pose(grip, q, false, origin, kWide, east, side, &p);
      if (p.card.changed) ++shakes;
      side = p.card.side;
    }
    Check(shakes == 0, "a hand that shakes 3 degrees never moves the card "
          "to the other side", shakes, 0.0);
  }

  // A WRIST STRAIGHT UNDER THE HEAD has no right in the view. The last one is
  // used. With no last one that fits, there is no card.
  {
    const float grip[3] = {0.0f, -0.50f, 0.0f};
    float q[4];
    Turn(0.0f, 90.0f, 0.0f, q);
    Posed p;
    HandFromGrip(grip, q, false, kHandM, &p.hand);
    // Move the head over the wrist.
    const float over[3] = {p.hand.wrist[0], 0.0f, p.hand.wrist[2]};
    const bool ok = Pose(grip, q, false, over, kWide, east, 0, &p);
    Check(ok, "a wrist straight under the head: the last right gives a card",
          ok, 1.0);
    Near("and the card goes along the last right", p.card.right[0], 1.0, 1e-5);
    const float along_sight[3] = {0.0f, -1.0f, 0.0f};
    Check(!Pose(grip, q, false, over, kWide, along_sight, 0, &p),
          "a last right along the line of sight gives no card", 0.0, 0.0);
  }

  // CardCovers itself: a point straight behind the card is covered, and a
  // point in front of it on the same line too. A point beside it is not.
  {
    CardPlace card = {};
    card.center[2] = -0.5f;
    card.orientation[3] = 1.0f;  // the front faces +z, toward the origin
    const float behind[3] = {0.0f, 0.0f, -1.0f};
    const float beside[3] = {0.2f, 0.0f, -1.0f};
    const float before[3] = {0.0f, 0.0f, -0.3f};
    Check(CardCovers(card, 0.1f, 0.05f, 0.0f, origin, behind),
          "the card covers a point behind it", 1.0, 1.0);
    Check(!CardCovers(card, 0.1f, 0.05f, 0.0f, origin, beside),
          "it does not cover a point beside it", 0.0, 0.0);
    Check(CardCovers(card, 0.1f, 0.05f, 0.0f, origin, before),
          "it covers a point in front of it on the same line", 1.0, 1.0);
  }

  // THE SIZE. 50 pixels are 1 cm. A card that is too wide gets the limit and
  // keeps its aspect.
  {
    float w = 0.0f, h = 0.0f;
    CardSize(300, 86, 5000.0f, 0.12f, &w, &h);
    Near("300 px are 6 cm wide, m", w, 0.06, 1e-6);
    Near("86 px are 1.72 cm high, m", h, 0.0172, 1e-6);
    CardSize(768, 152, 5000.0f, 0.12f, &w, &h);
    Near("768 px stop at 12 cm, m", w, 0.12, 1e-6);
    Near("and the height keeps the aspect, m", h, 0.12 * 152.0 / 768.0, 1e-6);
    CardSize(0, 86, 5000.0f, 0.12f, &w, &h);
    Near("no pixels give no card", w + h, 0.0, 1e-9);
  }

  // WHEN A PROMPT SHOWS. Context 5, a dwell of 0.3 s, 4 s of show, 2 times.
  {
    const PromptRule rule = {0.3f, 4.0f, 2};
    PromptState s;
    Check(PromptStep(&s, 5, 10.00, rule, false) == 0,
          "a new context shows nothing at once", 0.0, 0.0);
    Check(PromptStep(&s, 5, 10.20, rule, false) == 0,
          "and nothing before the dwell", 0.0, 0.0);
    Check(PromptStep(&s, 5, 10.31, rule, false) == 5,
          "after the dwell the prompt shows", 5.0, 5.0);
    Check(PromptStep(&s, 5, 14.20, rule, false) == 5,
          "it shows for its 4 s", 5.0, 5.0);
    Check(PromptStep(&s, 5, 14.40, rule, false) == 0,
          "then it goes", 0.0, 0.0);
    Check(PromptStep(&s, 5, 30.00, rule, false) == 0,
          "and it stays away while the context holds", 0.0, 0.0);
    Check(PromptStep(&s, 0, 30.10, rule, false) == 0,
          "no context shows nothing", 0.0, 0.0);
    PromptStep(&s, 5, 31.00, rule, false);
    Check(PromptStep(&s, 5, 31.40, rule, false) == 5,
          "the context comes back: the prompt shows a second time", 5.0, 5.0);
    PromptStep(&s, 0, 32.00, rule, false);
    PromptStep(&s, 5, 33.00, rule, false);
    Check(PromptStep(&s, 5, 34.00, rule, false) == 0,
          "after 2 times the player knows it, and it does not show", 0.0,
          0.0);
    Near("the count of the shows", s.shows[5], 2.0, 0.0);
    // A hand that goes across objects: each context is too short.
    PromptState t;
    int shown = 0;
    for (int i = 0; i < 40; ++i) {
      shown += PromptStep(&t, 1 + (i / 2) % 3, 50.0 + i * 0.1, rule, false);
    }
    Check(shown == 0, "contexts of 0.2 s each show no prompt", shown, 0.0);
    // prompts=2: always.
    PromptState a;
    PromptStep(&a, 5, 60.0, rule, true);
    Check(PromptStep(&a, 5, 60.4, rule, true) == 5,
          "always: the prompt shows after the dwell", 5.0, 5.0);
    Check(PromptStep(&a, 5, 90.0, rule, true) == 5,
          "always: it stays while the context holds", 5.0, 5.0);
    for (int i = 0; i < 6; ++i) {
      PromptStep(&a, 0, 100.0 + i, rule, true);
      PromptStep(&a, 5, 100.1 + i, rule, true);
      PromptStep(&a, 5, 100.6 + i, rule, true);
    }
    Check(PromptStep(&a, 5, 106.7, rule, true) == 5,
          "always: it shows each time", 5.0, 5.0);
    // A context out of range is no context.
    PromptState b;
    Check(PromptStep(&b, 99, 1.0, rule, false) == 0 &&
              PromptStep(&b, -3, 2.0, rule, false) == 0,
          "a context out of range shows nothing", 0.0, 0.0);
  }

  // THE FADE: 0.15 s for the whole way, and it stops at 0 and 1.
  {
    float fade = 0.0f;
    for (int i = 0; i < 5; ++i) fade = FadeStep(fade, true, 0.02f, 0.15f);
    Near("5 frames of 20 ms give a fade of 0.667", fade, 0.6667, 1e-3);
    for (int i = 0; i < 5; ++i) fade = FadeStep(fade, true, 0.02f, 0.15f);
    Near("the fade stops at 1", fade, 1.0, 1e-6);
    for (int i = 0; i < 20; ++i) fade = FadeStep(fade, false, 0.02f, 0.15f);
    Near("and at 0", fade, 0.0, 1e-6);
    // The soft fade of the two cards: 0.25 s in and 0.35 s out, at 90 frames
    // a second.
    fade = 0.0f;
    int frames_in = 0, frames_out = 0;
    while (fade < 1.0f && frames_in < 100) {
      fade = FadeStep(fade, true, 1.0f / 90.0f, 0.25f);
      ++frames_in;
    }
    while (fade > 0.0f && frames_out < 100) {
      fade = FadeStep(fade, false, 1.0f / 90.0f, 0.35f);
      ++frames_out;
    }
    Check(frames_in >= 22 && frames_in <= 24,
          "a card comes in 0.25 s, 23 frames", frames_in, 23);
    Check(frames_out >= 31 && frames_out <= 33,
          "a card goes in 0.35 s, 32 frames", frames_out, 32);
  }

  // THE SIZE OF THE TWO CARDS. 40 pixels are 1 cm, so a capital letter of 31
  // pixels is 7.8 mm high: 0.8 degrees at 55 cm.
  {
    float w = 0.0f, h = 0.0f;
    CardSize(310, 86, 4000.0f, 0.19f, &w, &h);
    Near("a card of 310 px is 7.75 cm wide, m", w, 0.0775, 1e-6);
    Near("and one line is 2.15 cm high, m", h, 0.0215, 1e-6);
    CardSize(768, 152, 4000.0f, 0.19f, &w, &h);
    Near("the widest card stops at 19 cm, m", w, 0.19, 1e-6);
    const double letter_deg = atan(31.0 / 4000.0 / 0.55) * 57.29578;
    Check(letter_deg > 0.75 && letter_deg < 0.9,
          "a capital letter is 0.8 degrees high at 55 cm", letter_deg, 0.8);
  }

  // WHICH HINTS OF THE GAME ARE TRUE. Code 1 is the left button, the trigger.
  // Code 2 is the right button, the grip. Mode 3 is the land, 0x13 the
  // Creature, 9 an object to take and 0x14 a land drag.
  {
    // The old immersive view: the trigger drags the land.
    Check(GameHintTrue(1, true, 3, true, false),
          "the immersive view: \"Move\" for the trigger is true", 1, 1);
    // The God view and the table: the drag moves nothing.
    Check(!GameHintTrue(1, true, 3, false, false),
          "the God view: \"Move\" for the trigger is false", 0, 0);
    Check(!GameHintTrue(1, true, 0x14, false, false),
          "the God view, in a land drag: false too", 0, 0);
    Check(!GameHintTrue(1, false, 0, false, false),
          "the God view, with no hand mode: false too", 0, 0);
    Check(!GameHintTrue(1, true, 9, false, false),
          "the God view, over an object: the left button moves nothing", 0, 0);
    Check(!GameHintTrue(1, true, 0x13, false, false),
          "the God view, on the Creature: the left button moves nothing", 0,
          0);
    Check(GameHintTrue(2, true, 0x13, false, false) &&
              GameHintTrue(2, true, 0x13, false, true),
          "\"Interact\" for the grip on the Creature is true, with the "
          "leash on too", 1, 1);
    // The leash on: the trigger commands the Creature, in each view.
    Check(!GameHintTrue(1, true, 3, false, true),
          "the leash on: \"Move\" for the trigger is false", 0, 0);
    // The grip.
    Check(GameHintTrue(2, true, 9, false, false),
          "\"Pick Up\" for the grip is true", 1, 1);
    Check(GameHintTrue(2, true, 3, false, false),
          "a grip hint over the land with no leash is true", 1, 1);
    Check(!GameHintTrue(2, true, 3, false, true),
          "the leash on, over the land: the grip does not send the Creature",
          0, 0);
    Check(GameHintTrue(2, true, 9, false, true),
          "the leash on, over an object: the grip acts on it", 1, 1);
    Check(GameHintTrue(2, false, 0, false, true),
          "the leash on, with no hand mode: the hint stays", 1, 1);
    // The stick and a hint with no button.
    Check(GameHintTrue(0x10, true, 3, false, true) &&
              GameHintTrue(8, true, 3, false, false),
          "\"Zoom Out\" for the stick is true", 1, 1);
    Check(GameHintTrue(0, true, 3, false, true),
          "a hint with no button (\"Temple Health\") is true", 1, 1);
  }

  // ONE CARD FOR EACH HAND. The right hand at the right and the left hand at
  // the left, both ahead and under the head, with the fingers ahead and
  // down. The card of the right hand starts right of its wrist, and the card
  // of the left hand left of its wrist. No card is on a hand, and the two
  // cards do not meet.
  {
    uint32_t seed = 20261003u;
    int cases = 0, right_side = 0, left_side = 0, on_own = 0, on_other = 0;
    int meet = 0;
    for (int i = 0; i < 2000; ++i) {
      const float right_grip[3] = {0.12f + 0.20f * Random(&seed),
                                   -0.20f - 0.30f * Random(&seed),
                                   -0.25f - 0.35f * Random(&seed)};
      const float left_grip[3] = {-0.12f - 0.20f * Random(&seed),
                                  -0.20f - 0.30f * Random(&seed),
                                  -0.25f - 0.35f * Random(&seed)};
      float qr[4], ql[4];
      Turn(-20.0f + 40.0f * Random(&seed), 40.0f + 50.0f * Random(&seed),
           -30.0f + 60.0f * Random(&seed), qr);
      Turn(-20.0f + 40.0f * Random(&seed), 40.0f + 50.0f * Random(&seed),
           -30.0f + 60.0f * Random(&seed), ql);
      Posed r, l;
      if (!Pose(right_grip, qr, false, origin, kWide, east, 0, &r) ||
          !Pose(left_grip, ql, true, origin, kWide, east, 0, &l)) {
        continue;
      }
      ++cases;
      if (r.card.side == 1) ++right_side;
      if (l.card.side == -1) ++left_side;
      if (Covers(r, kWide, kHigh) || Covers(l, kWide, kHigh)) ++on_own;
      if (CardCoversAny(r.card, kWide, kHigh, 0.01f, origin, l.outline,
                        kOutlinePoints) ||
          CardCoversAny(l.card, kWide, kHigh, 0.01f, origin, r.outline,
                        kOutlinePoints)) {
        ++on_other;
      }
      if (CardsOverlap(r.card, kWide, kHigh, l.card, kWide, kHigh, origin)) {
        ++meet;
      }
    }
    Check(cases == 2000, "2000 poses of two hands each have two cards", cases,
          2000);
    // Fingers that point far to the outer side put the card on the inner
    // side (THE SIDE in hint_card_math.h). That is 2 to 3 percent of these
    // poses.
    Check(right_side > cases * 95 / 100,
          "the card of the right hand is right of its wrist in more than 95 "
          "percent of the poses", right_side, cases);
    Check(left_side > cases * 95 / 100,
          "the card of the left hand is left of its wrist in more than 95 "
          "percent of the poses", left_side, cases);
    Check(on_own == 0, "no card is on its own hand", on_own, 0);
    Check(on_other == 0, "no card is on the other hand", on_other, 0);
    Check(meet == 0, "the two cards do not meet", meet, 0);
  }

  // A CARD THAT WOULD HIDE THE OTHER HAND. The left hand is where the card of
  // the right hand is. The rule finds it, so that card does not show.
  {
    float q[4];
    Turn(0.0f, 90.0f, 0.0f, q);
    const float right_grip[3] = {0.05f, -0.40f, -0.45f};
    Posed r;
    Pose(right_grip, q, false, origin, kWide, east, 0, &r);
    // The left grip at the center of the right card.
    Posed l;
    Pose(r.card.center, q, true, origin, kWide, east, 0, &l);
    Check(CardCoversAny(r.card, kWide, kHigh, 0.01f, origin, l.outline,
                        kOutlinePoints),
          "a left hand at the card of the right hand: the card is on it", 1,
          1);
    // The same left hand 40 cm to the left is clear.
    const float far_left[3] = {r.card.center[0] - 0.40f, r.card.center[1],
                               r.card.center[2]};
    Pose(far_left, q, true, origin, kWide, east, 0, &l);
    Check(!CardCoversAny(r.card, kWide, kHigh, 0.01f, origin, l.outline,
                         kOutlinePoints),
          "a left hand 40 cm to the left: the card is clear of it", 0, 0);
    // Two cards at one place meet, and two cards 30 cm apart do not.
    CardPlace a = r.card, b = r.card;
    Check(CardsOverlap(a, kWide, kHigh, b, kWide, kHigh, origin),
          "two cards at one place meet", 1, 1);
    b.center[0] -= 0.30f;
    Check(!CardsOverlap(a, kWide, kHigh, b, kWide, kHigh, origin),
          "two cards 30 cm apart do not meet", 0, 0);
    b = a;
    b.center[0] += 0.5f * kWide;
    Check(CardsOverlap(a, kWide, kHigh, b, kWide, kHigh, origin),
          "two cards that share half of their width meet", 1, 1);
  }

  SubtitleCardChecks();
}

}  // namespace hint_checks
