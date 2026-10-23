// A SPEECH BUBBLE OF THE GAME AS ONE CARD. See docs/ui-layer.md, "A speech
// bubble as one card".
//
// THE BUBBLE. A wooden signpost (a ScriptHighlight of the kind "did you
// know"), a villager that speaks and the Creature show a text in a blue box
// with a tail. One class draws all three:
//
//   fn_00576F20  each frame, from HelpSystem::Draw3D (0x005C8478), from
//                Creature::Draw and from SpecialVillager::Draw. It finds the
//                pixel of the sign, and from that pixel the side of the box,
//                the tail and the part of the box that is left of the sign.
//   fn_00575EA0  the place function. It keeps that layout, and it makes the
//                size of the box from the depth of the sign and the place of
//                the box from the pixel of the sign.
//   0x005767D0   the draw, later in the frame, as an object of the Z sort.
//                GatheringText::DrawBubble draws the box and the tail, and
//                GatheringText::DrawText the text.
//
// IN TWO EYE PASSES each of these runs two times, with the camera of the
// eye. Three things then put the bubble at three depths:
//
//   1. The layout comes from the pixel of the sign in each pass. That pixel
//      differs between the eyes, so the box can have a different side, a
//      different tail and a different place in each eye.
//   2. The box is not drawn at its pixels. DrawBubble makes a point of the
//      world for each corner, at the depth of the bubble (the depth of the
//      sign less 5 units, and less again while the camera looks down), from
//      the camera point of the game, which does not move with the eye. Drawn
//      through the camera of the eye, the box has the eye difference of that
//      depth on top of the eye difference of its pixels.
//   3. The text is drawn at its pixels, and draw_hooks.cpp gives each flat
//      batch of the text code the interface depth.
//
// From far all three are small. From near they are not, and the eyes cannot
// bring the text and the box together.
//
// THE RULE (bubble_card=1). The bubble is one flat card at the depth of the
// sign:
//
//   - The layout of a frame is the one that the game asks for in the first
//     eye pass. Each later ask of the frame gets the same one. Near the
//     middle of the picture the side of the frame before stays
//     (bubble_card_math.h, Choose).
//   - Each corner of the box goes back to the pixel that the game made it
//     from (CornerToItsPixel).
//   - The flat batches of the bubble are drawn at the pixels of the game,
//     with none of the steps of the interface (draw_hooks.cpp).
//
// The box of each eye is then the same box, moved by the eye difference of
// the sign. The Z value of the box stays the one of the game, so the land
// near the sign does not cut the card.
//
// IN ONE WALK (one_walk.cpp) the game places and draws the bubble one time,
// for the left eye. No batch of the bubble has the depth of the sign in it:
// a corner of the box has the rhw of the depth of the bubble, which is
// nearer than the sign, and the flat batches have no depth. So the card does
// not go to the right eye by the rule of one walk for a batch. With
// one_walk_marks=1 draw_hooks.cpp draws each batch of the card for the left
// eye only, and then for the right eye only, moved by the eye difference of
// the sign (BubbleCardWalkMove, one_walk_math::ShiftAtDepth). That is the
// place that the place function gives the card in a pass of the right eye.
//
// With bubble_card=0 the mod changes nothing of the bubble.
//
// THE NAME OF A VILLAGER (name_card=1). VillagerName::Add (0x007629E0)
// projects the point over the head of a villager that shows its details,
// and keeps its pixel (+0x14) and its depth in the camera (+0x1C). VillagerName::Draw
// (0x00762DC0), an object of the Z sort, draws the box through DrawBubble at
// the depth of the bubble (fn_005766E0 of that depth) and the text through
// GatheringText::DrawTextRaw. It has no layout and no place function. With
// name_card=1 its draw is a card by the same rule as a bubble: the box at its
// pixels, the flat batches with no interface depth, and in one walk the copy
// for the right eye moved by the eye difference of the villager. With
// name_card=0 the mod changes nothing of the name, and the log has its
// numbers.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "bubble_card.h"
#include "bubble_card_math.h"
#include "debug_menu.h"
#include "one_walk.h"
#include "one_walk_math.h"

extern void ModLog(const char* format, ...);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);

namespace {

using namespace bubble_card;

// The place function: __thiscall with the bubble in ecx, then the point of
// the sign, the offset and the anchor on the stack, and "ret 0xC". It gives
// 0 when the sign is not in front of the camera.
const uintptr_t kBubblePlace = 0x00575EA0;
// The draw: the bubble in ecx, no argument on the stack, a plain ret.
const uintptr_t kBubbleDraw = 0x005767D0;
// VillagerName::Draw: the name in ecx, no argument on the stack, a plain ret.
// It deletes the name at its end.
const uintptr_t kNameDraw = 0x00762DC0;
// LH3DTech::Draw3DWorldTriangle, to the function after it. The box is the
// only batch of a bubble that comes from there.
const uintptr_t kWorldTriangleBegin = 0x0081C090;
const uintptr_t kWorldTriangleEnd = 0x0081C5C0;

// The floats of a bubble that the place function writes.
const int kBoxWidth = 0x50;
const int kBoxHeight = 0x54;
const int kSignDepth = 0x58;  // the depth of the sign in the camera
const int kBoxLeft = 0x68;
const int kBoxTop = 0x6C;
// The floats of a VillagerName that VillagerName::Add writes.
const int kNamePixel = 0x14;  // the pixel of the villager across the picture
const int kNameDepth = 0x1C;  // the depth of the villager in the camera

typedef int(__fastcall* PlaceFn)(void* self, void* unused,
                                 const float* position, float offset,
                                 float anchor);
typedef void(__fastcall* DrawFn)(void* self, void* unused);
PlaceFn o_Place = nullptr;
DrawFn o_Draw = nullptr;
DrawFn o_NameDraw = nullptr;

const uint32_t kFvf = 0x1C4;  // XYZRHW, two colors, one texture: 32 bytes
const int kStride = 32;
const uint32_t kMaxCorners = 64;

bool g_on = true;        // bubble_card
bool g_name_on = true;   // name_card
bool g_test = false;     // bubble_card_test: a line of the log each 30 frames
int g_test_frame = 0;    // bubble_card_test_frame
bool g_installed = false;
int g_eye = -1;

// One bubble of the game, by its address, or the name of a villager.
struct Seen {
  const void* bubble = nullptr;
  bool name = false;        // the name of a villager, not a bubble
  bool have = false;
  uint64_t frame = 0;       // the frame of the layout
  Layout layout;            // the layout of that frame
  uint64_t first_frame = 0; // of this time in view
  uint64_t last_said = 0;
  // The numbers of the frame, for the log. One set for each eye pass.
  bool placed[2] = {false, false};
  bool drew[2] = {false, false};
  Layout asked[2];
  Layout used[2];
  float box[2][4] = {};     // left, top, width, height
  float pixel[2] = {0.0f, 0.0f};  // the pixel of the villager (a name)
  float depth = 0.0f;
  // The least and the largest x of the corners and of the flat parts as
  // drawn: in each eye pass, and at kWalkRight for the right eye of one walk.
  float drawn_box[3][2] = {};
  float drawn_flat[3][2] = {};
  bool have_box[3] = {false, false, false};
  bool have_flat[3] = {false, false, false};
  // One walk: the frame in which the pass of the left eye drew the right eye
  // too, the eye difference of the sign in that frame, and whether the card
  // went to the right eye with it (one_walk_marks).
  uint64_t walk_frame = 0;
  float walk_move = 0.0f;
  bool walk_moved = false;
};
const int kWalkRight = 2;
const int kMaxSeen = 16;
Seen g_seen[kMaxSeen];
// The game makes a new VillagerName in each pass and deletes it in its draw,
// so the names have one record: the first name that a pass draws. Each
// other name of the pass is drawn with a record of its own that the log does
// not read.
Seen g_name;
Seen g_name_more;
Seen* g_drawing = nullptr;
int g_scope = 0;

float g_saved_x[kMaxCorners];
float g_saved_y[kMaxCorners];
// The batch of the box with each corner at its pixel, as it is before the
// draw for the left eye. LH3DRender::DrawTriangle changes the vertices that
// it gets (0x0082F82C to 0x0082F8F7: an offset of the picture, and an offset
// of the texture), so the draw for the right eye of one walk starts from this
// copy.
uint8_t g_at_pixels[kMaxCorners * kStride];

// For the report.
uint64_t g_frames_shown = 0;
uint64_t g_layouts_kept = 0;   // asks that got the layout of the frame
uint64_t g_layouts_differ = 0; // of those, asks for a different layout
uint64_t g_sides_kept = 0;
uint64_t g_boxes = 0;
double g_box_moved = 0.0;
uint64_t g_flat = 0;
uint64_t g_too_large = 0;
uint64_t g_walk_boxes = 0;  // boxes that one walk drew for the right eye
uint64_t g_name_frames = 0; // frames with the name of a villager in view

// True when the bubble or the name that draws now is a card.
bool ScopeOn() {
  if (g_drawing == nullptr) return false;
  return g_drawing->name ? g_name_on : g_on;
}

Seen* SeenOf(const void* bubble) {
  Seen* oldest = &g_seen[0];
  for (int i = 0; i < kMaxSeen; ++i) {
    if (g_seen[i].bubble == bubble) return &g_seen[i];
    if (g_seen[i].frame < oldest->frame) oldest = &g_seen[i];
  }
  *oldest = Seen();
  oldest->bubble = bubble;
  return oldest;
}

float Field(const void* bubble, int offset) {
  return *reinterpret_cast<const float*>(
      static_cast<const uint8_t*>(bubble) + offset);
}

bool GetView(View* view) {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return false;
  view->half_res_x = info->half_res_x;
  view->half_res_y = info->half_res_y;
  view->inv_half_tan_x = info->inv_half_tan_fov_x;
  view->inv_half_tan_y = info->inv_half_tan_fov_y;
  view->near_plane = *reinterpret_cast<const float*>(bw::tech::kNearPlane);
  return true;
}

float Middle(const float range[2]) { return (range[0] + range[1]) * 0.5f; }

// One line with the numbers of a frame of a bubble, after the second eye
// pass drew it.
void Say(const Seen& s, const char* why) {
  float sign[2] = {0.0f, 0.0f};
  for (int e = 0; e < 2; ++e) {
    sign[e] = SignPixel(s.box[e][0], s.box[e][2], s.used[e]);
  }
  const float box_game[2][2] = {{s.box[0][0], s.box[0][0] + s.box[0][2]},
                                {s.box[1][0], s.box[1][0] + s.box[1][2]}};
  ModLog("bubble card  : %s. The sign is %.1f units deep, and its pixel is "
         "%.1f in the left picture and %.1f in the right one: %.1f apart. "
         "The game asks for the offset %+.0f and the anchor %.3f in the left "
         "pass, and for %+.0f and %.3f in the right pass. The passes get "
         "%+.0f and %.3f, and %+.0f and %.3f. The box of the game is %.1f to "
         "%.1f and %.1f to %.1f across, %.0f by %.0f and %.0f by %.0f: %.1f "
         "apart. The box as drawn is %.1f to %.1f and %.1f to %.1f: %.1f "
         "apart. The flat parts as drawn (the text) are %.1f to %.1f and "
         "%.1f to %.1f: %.1f apart. bubble_card=%d, frame %llu",
         why, s.depth, sign[0], sign[1], sign[0] - sign[1], s.asked[0].offset,
         s.asked[0].anchor, s.asked[1].offset, s.asked[1].anchor,
         s.used[0].offset, s.used[0].anchor, s.used[1].offset,
         s.used[1].anchor, box_game[0][0], box_game[0][1], box_game[1][0],
         box_game[1][1], s.box[0][2], s.box[0][3], s.box[1][2], s.box[1][3],
         Middle(box_game[0]) - Middle(box_game[1]), s.drawn_box[0][0],
         s.drawn_box[0][1], s.drawn_box[1][0], s.drawn_box[1][1],
         Middle(s.drawn_box[0]) - Middle(s.drawn_box[1]), s.drawn_flat[0][0],
         s.drawn_flat[0][1], s.drawn_flat[1][0], s.drawn_flat[1][1],
         Middle(s.drawn_flat[0]) - Middle(s.drawn_flat[1]), g_on ? 1 : 0,
         ControllersFrameCount());
}

void Widen(float range[2], bool* have, float x) {
  if (!*have) {
    range[0] = range[1] = x;
    *have = true;
    return;
  }
  if (x < range[0]) range[0] = x;
  if (x > range[1]) range[1] = x;
}

// ONE WALK. True in the pass of the left eye when that pass draws the right
// eye too.
bool InWalk() { return g_eye == 0 && OneWalkActive(); }

// The words of a log line for a bubble or for the name of a villager.
const char* Tag(const Seen& s) {
  return s.name ? "name card    " : "bubble card  ";
}
const char* Thing(const Seen& s) { return s.name ? "villager" : "sign"; }
const char* Key(const Seen& s) { return s.name ? "name_card" : "bubble_card"; }
int KeyValue(const Seen& s) { return (s.name ? g_name_on : g_on) ? 1 : 0; }

// The pixel of the sign or of the villager in the pass of an eye.
float PixelOf(const Seen& s, int eye) {
  if (s.name) return s.pixel[eye];
  return SignPixel(s.box[eye][0], s.box[eye][2], s.used[eye]);
}

// The pixels from the left eye to the right eye for a thing at the depth of
// the sign. Negative: the right eye sees a near thing further to the left.
float SignMove(const Seen& s) {
  return one_walk_math::ShiftAtDepth(
      OneWalkShiftPerRhw(),
      *reinterpret_cast<const float*>(bw::tech::kNearPlane), s.depth);
}

// One line with the numbers of a frame of a bubble in one walk, after the
// pass of the left eye drew it for both eyes.
void SayWalk(const Seen& s, const char* why) {
  const float sign = PixelOf(s, 0);
  ModLog("%s: %s. One walk: the pass of the left eye draws the "
         "right eye too. The %s is %.1f units deep, and its pixel is %.1f "
         "in the left picture. The eye difference of that depth is %.1f "
         "pixels. The card %s (one_walk_marks=%d). The box as drawn is %.1f "
         "to %.1f in the left picture and %.1f to %.1f in the right one: "
         "%.1f apart. The flat parts as drawn (the text) are %.1f to %.1f and "
         "%.1f to %.1f: %.1f apart. %s=%d, frame %llu",
         Tag(s), why, Thing(s), s.depth, sign, -s.walk_move,
         s.walk_moved ? "goes to the right eye with that difference"
                      : "goes to the right eye as each other batch",
         s.walk_moved ? 1 : 0, s.drawn_box[0][0], s.drawn_box[0][1],
         s.drawn_box[kWalkRight][0], s.drawn_box[kWalkRight][1],
         Middle(s.drawn_box[0]) - Middle(s.drawn_box[kWalkRight]),
         s.drawn_flat[0][0], s.drawn_flat[0][1], s.drawn_flat[kWalkRight][0],
         s.drawn_flat[kWalkRight][1],
         Middle(s.drawn_flat[0]) - Middle(s.drawn_flat[kWalkRight]),
         Key(s), KeyValue(s), ControllersFrameCount());
}

// one_walk_check=1: the right eye also got a walk of its own in this frame.
// One line with the card of that walk beside the card that one walk drew.
void SayCheck(const Seen& s) {
  const float sign_left = PixelOf(s, 0);
  const float sign_own = PixelOf(s, 1);
  ModLog("%s: test key one_walk_check: the right eye in its own "
         "walk. The pixel of the %s is %.1f there, and the pixel of the "
         "left picture with the eye difference of the %s is %.1f. The box "
         "as drawn is %.1f to %.1f in its own walk and %.1f to %.1f in one "
         "walk: %.1f apart. The flat parts as drawn (the text) are %.1f to "
         "%.1f and %.1f to %.1f: %.1f apart. one_walk_marks=%d, "
         "%s=%d, frame %llu",
         Tag(s), Thing(s), sign_own, Thing(s), sign_left + s.walk_move,
         s.drawn_box[1][0],
         s.drawn_box[1][1], s.drawn_box[kWalkRight][0],
         s.drawn_box[kWalkRight][1],
         Middle(s.drawn_box[1]) - Middle(s.drawn_box[kWalkRight]),
         s.drawn_flat[1][0], s.drawn_flat[1][1], s.drawn_flat[kWalkRight][0],
         s.drawn_flat[kWalkRight][1],
         Middle(s.drawn_flat[1]) - Middle(s.drawn_flat[kWalkRight]),
         s.walk_moved ? 1 : 0, Key(s), KeyValue(s), ControllersFrameCount());
}

// Two walks: one line with the numbers of a frame of the name of a villager,
// after the second eye pass drew it.
void SayName(const Seen& s, const char* why) {
  ModLog("name card    : %s. The villager is %.1f units deep, and its pixel "
         "is %.1f in the left picture and %.1f in the right one: %.1f apart. "
         "The box as drawn is %.1f to %.1f and %.1f to %.1f: %.1f apart. The "
         "flat parts as drawn (the text) are %.1f to %.1f and %.1f to %.1f: "
         "%.1f apart. name_card=%d, frame %llu",
         why, s.depth, s.pixel[0], s.pixel[1], s.pixel[0] - s.pixel[1],
         s.drawn_box[0][0], s.drawn_box[0][1], s.drawn_box[1][0],
         s.drawn_box[1][1], Middle(s.drawn_box[0]) - Middle(s.drawn_box[1]),
         s.drawn_flat[0][0], s.drawn_flat[0][1], s.drawn_flat[1][0],
         s.drawn_flat[1][1], Middle(s.drawn_flat[0]) - Middle(s.drawn_flat[1]),
         g_name_on ? 1 : 0, ControllersFrameCount());
}

int __fastcall h_Place(void* self, void* unused, const float* position,
                       float offset, float anchor) {
  if (g_eye < 0 || g_eye > 1 || self == nullptr) {
    return o_Place(self, unused, position, offset, anchor);
  }
  Seen* seen = SeenOf(self);
  const uint64_t frame = ControllersFrameCount();
  const Layout asked = {offset, anchor};
  if (!seen->have || seen->frame != frame) {
    // The first ask of this frame decides its layout.
    const bool have_last = seen->have && frame - seen->frame <= 3;
    const Layout chosen = Choose(asked, have_last, seen->layout, kSideBand);
    if (g_on && chosen.offset != asked.offset) ++g_sides_kept;
    if (!have_last) seen->first_frame = frame;
    seen->layout = chosen;
    seen->frame = frame;
    seen->have = true;
    for (int e = 0; e < 2; ++e) {
      seen->placed[e] = seen->drew[e] = false;
      seen->have_box[e] = seen->have_flat[e] = false;
    }
    seen->have_box[kWalkRight] = seen->have_flat[kWalkRight] = false;
    ++g_frames_shown;
  }
  Layout used = asked;
  if (g_on) {
    used = seen->layout;
    ++g_layouts_kept;
    if (used.offset != asked.offset || used.anchor != asked.anchor) {
      ++g_layouts_differ;
    }
  }
  const int placed =
      o_Place(self, unused, position, used.offset, used.anchor);
  if (placed != 0) {
    seen->placed[g_eye] = true;
    seen->asked[g_eye] = asked;
    seen->used[g_eye] = used;
    seen->box[g_eye][0] = Field(self, kBoxLeft);
    seen->box[g_eye][1] = Field(self, kBoxTop);
    seen->box[g_eye][2] = Field(self, kBoxWidth);
    seen->box[g_eye][3] = Field(self, kBoxHeight);
    seen->depth = Field(self, kSignDepth);
  }
  return placed;
}

// The log after a draw of a bubble or of a name: one line when it comes into
// view, and with the test key one line each 30 frames.
void AfterDraw(Seen* seen, uint64_t frame) {
  seen->drew[g_eye] = true;
  const char* comes = seen->name ? "the name of a villager comes into view"
                                 : "a bubble of the game comes into view";
  const char* tested = seen->name
                           ? "test key bubble_card_test: a name in view"
                           : "test key bubble_card_test: a bubble in view";
  if (seen->walk_frame == frame) {
    if (g_eye == 1) {
      // The pass of the right eye in a frame of one walk: one_walk_check.
      if (seen->placed[1] && seen->have_box[1] && seen->have_box[kWalkRight]) {
        SayCheck(*seen);
      }
      return;
    }
    if (!seen->have_box[0] || !seen->have_box[kWalkRight]) return;
    if (seen->first_frame != 0 && seen->last_said < seen->first_frame) {
      seen->last_said = frame;
      SayWalk(*seen, comes);
    } else if (g_test && frame - seen->last_said >= 30) {
      seen->last_said = frame;
      SayWalk(*seen, tested);
    }
    return;
  }
  if (g_eye != 1 || !seen->drew[0] || !seen->placed[0] || !seen->placed[1] ||
      !seen->have_box[0] || !seen->have_box[1]) {
    return;
  }
  if (seen->first_frame != 0 && seen->last_said < seen->first_frame) {
    seen->last_said = frame;
    if (seen->name) {
      SayName(*seen, comes);
    } else {
      Say(*seen, comes);
    }
  } else if (g_test && frame - seen->last_said >= 30) {
    seen->last_said = frame;
    if (seen->name) {
      SayName(*seen, tested);
    } else {
      Say(*seen, tested);
    }
  }
}

void __fastcall h_Draw(void* self, void* unused) {
  if (g_eye < 0 || g_eye > 1 || self == nullptr) {
    o_Draw(self, unused);
    return;
  }
  Seen* outer = g_drawing;
  g_drawing = SeenOf(self);
  ++g_scope;
  // ONE WALK: this draw is for both eyes. The numbers of the frame are for
  // the log, also when the card goes to the right eye as each other batch.
  const uint64_t frame = ControllersFrameCount();
  if (InWalk() && g_drawing->have && g_drawing->frame == frame &&
      g_drawing->placed[0]) {
    g_drawing->walk_frame = frame;
    g_drawing->walk_move = SignMove(*g_drawing);
    g_drawing->walk_moved = false;
  }
  o_Draw(self, unused);
  --g_scope;
  Seen* seen = g_drawing;
  g_drawing = outer;
  AfterDraw(seen, frame);
}

// THE NAME OF A VILLAGER. The game makes the name in the scene walk of each
// eye pass (VillagerName::Add) and draws it later in that pass, as an object
// of the Z sort. The draw is a card by the rule of a bubble, with the depth
// and the pixel of the villager in the place of those of the sign.
void __fastcall h_NameDraw(void* self, void* unused) {
  if (g_eye < 0 || g_eye > 1 || self == nullptr) {
    o_NameDraw(self, unused);
    return;
  }
  Seen* seen = &g_name;
  const uint64_t frame = ControllersFrameCount();
  if (!seen->have || seen->frame != frame) {
    // The first name of this frame.
    const bool have_last = seen->have && frame - seen->frame <= 3;
    if (!have_last) seen->first_frame = frame;
    seen->frame = frame;
    seen->have = true;
    for (int e = 0; e < 2; ++e) {
      seen->placed[e] = seen->drew[e] = false;
      seen->have_box[e] = seen->have_flat[e] = false;
    }
    seen->have_box[kWalkRight] = seen->have_flat[kWalkRight] = false;
    ++g_name_frames;
  }
  seen->name = true;
  if (seen->placed[g_eye]) {
    seen = &g_name_more;
    *seen = Seen();
    seen->name = true;
    seen->have = true;
    seen->frame = frame;
    seen->last_said = frame;
  }
  seen->placed[g_eye] = true;
  seen->pixel[g_eye] = Field(self, kNamePixel);
  seen->depth = Field(self, kNameDepth);
  if (InWalk()) {
    seen->walk_frame = frame;
    seen->walk_move = SignMove(*seen);
    seen->walk_moved = false;
  }
  Seen* outer = g_drawing;
  g_drawing = seen;
  ++g_scope;
  o_NameDraw(self, unused);
  --g_scope;
  g_drawing = outer;
  AfterDraw(seen, frame);
}

bool Hook(uintptr_t address, void* replacement, void** original,
          const char* name) {
  void* target = reinterpret_cast<void*>(address);
  MH_STATUS status = MH_CreateHook(target, replacement, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : %s at 0x%08X %s", name, address,
         status == MH_OK ? "ok" : "FAILED");
  return status == MH_OK;
}

const char* RuleText(bool on) {
  return on ? "a speech bubble of the game is one flat card at the depth of "
              "its sign: one layout for both eyes, the box at its pixels, "
              "and the text with no interface depth"
            : "a speech bubble of the game is drawn as the game and the "
              "interface depth make it in each eye pass";
}

void Toggle(const char* cause) {
  g_on = !g_on;
  ControllersGesturePulse(0.5f, g_on ? 0.30f : 0.08f);
  ModLog("bubble card  : %s: bubble_card is %d now (%s), frame %llu", cause,
         g_on ? 1 : 0, RuleText(g_on), ControllersFrameCount());
}

}  // namespace

void BubbleCardSetOptions(int on, int name_on, int test, int test_frame) {
  g_on = on != 0;
  g_name_on = name_on != 0;
  g_test = test != 0;
  g_test_frame = test_frame;
}

void BubbleCardInstall() {
  if (g_installed) return;
  // Both functions start as the listing has them: "sub esp, 0x38" and
  // "sub esp, 0x820".
  static const uint8_t place_start[3] = {0x83, 0xEC, 0x38};
  static const uint8_t draw_start[6] = {0x81, 0xEC, 0x20, 0x08, 0x00, 0x00};
  const uint8_t* place = reinterpret_cast<const uint8_t*>(kBubblePlace);
  const uint8_t* draw = reinterpret_cast<const uint8_t*>(kBubbleDraw);
  if (IsBadReadPtr(place, sizeof(place_start)) ||
      memcmp(place, place_start, sizeof(place_start)) != 0 ||
      IsBadReadPtr(draw, sizeof(draw_start)) ||
      memcmp(draw, draw_start, sizeof(draw_start)) != 0) {
    ModLog("bubble card  : the code of the bubble is not as expected, so a "
           "bubble is drawn as before");
    return;
  }
  if (!Hook(kBubblePlace, reinterpret_cast<void*>(&h_Place),
            reinterpret_cast<void**>(&o_Place),
            "the place of a speech bubble") ||
      !Hook(kBubbleDraw, reinterpret_cast<void*>(&h_Draw),
            reinterpret_cast<void**>(&o_Draw),
            "the draw of a speech bubble")) {
    return;
  }
  g_installed = true;
  ModLog("bubble card  : bubble_card=%d, %s", g_on ? 1 : 0, RuleText(g_on));
  // VillagerName::Draw starts as the listing has it: "sub esp, 0x10",
  // "push esi" and "mov esi, ecx".
  static const uint8_t name_start[6] = {0x83, 0xEC, 0x10, 0x56, 0x8B, 0xF1};
  const uint8_t* name = reinterpret_cast<const uint8_t*>(kNameDraw);
  if (IsBadReadPtr(name, sizeof(name_start)) ||
      memcmp(name, name_start, sizeof(name_start)) != 0) {
    ModLog("name card    : the code of the name of a villager is not as "
           "expected, so a name is drawn as before");
  } else if (Hook(kNameDraw, reinterpret_cast<void*>(&h_NameDraw),
                  reinterpret_cast<void**>(&o_NameDraw),
                  "the draw of the name of a villager")) {
    ModLog("name card    : name_card=%d, %s", g_name_on ? 1 : 0,
           g_name_on ? "the name of a villager is one flat card at the depth "
                       "of the villager, by the rule of a speech bubble"
                     : "the name of a villager is drawn as the game and the "
                       "interface depth make it in each eye pass");
  }
  if (g_test) {
    ModLog("bubble card  : test key bubble_card_test=1: one line of the log "
           "each 30 frames while a bubble is in view");
  }
}

// The test key (Shift and Delete in play, test_keys=1). A long pulse for 1
// and a short pulse for 0.
void BubbleCardToggle(const char* cause) {
  if (!g_installed) return;
  Toggle(cause);
}

int BubbleCardValue() {
  if (!g_installed) return kDebugCannot;
  return g_on ? 1 : 0;
}

void BubbleCardPass(int eye) {
  g_eye = g_installed ? eye : -1;
  // A test with no key press: the test key acts at one frame.
  if (eye == 0 && g_installed && g_test_frame > 0) {
    const uint64_t frame = ControllersFrameCount();
    const uint64_t at = static_cast<uint64_t>(g_test_frame);
    static uint64_t done_frame = 0;
    if (frame != done_frame && ControllersTestFrame(at, frame) == at) {
      done_frame = frame;
      Toggle("test key bubble_card_test_frame");
    }
  }
}

bool BubbleCardInBubble() { return g_scope > 0; }

bool BubbleCardOn() { return ScopeOn(); }

void BubbleCardFlat(const void* verts, uint32_t fvf, uint32_t count,
                    bool right) {
  if (g_drawing == nullptr || g_eye < 0 || g_eye > 1) return;
  ++g_flat;
  if (verts == nullptr || fvf != kFvf) return;
  const int to = right ? kWalkRight : g_eye;
  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kStride) {
    Widen(g_drawing->drawn_flat[to], &g_drawing->have_flat[to],
          *reinterpret_cast<const float*>(vertex));
  }
}

bool BubbleCardWalkMove(float* move) {
  if (!ScopeOn() || !InWalk() || !OneWalkMarks()) {
    return false;
  }
  // The place function gave the depth of the sign in this frame.
  if (g_drawing->walk_frame != ControllersFrameCount()) return false;
  g_drawing->walk_moved = true;
  *move = g_drawing->walk_move;
  return true;
}

void BubbleCardBoxRight(void* verts, uint32_t count, float move) {
  if (g_drawing == nullptr || verts == nullptr || count > kMaxCorners) return;
  memcpy(verts, g_at_pixels, static_cast<size_t>(count) * kStride);
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kStride) {
    float* p = reinterpret_cast<float*>(vertex);
    p[0] += move;
    Widen(g_drawing->drawn_box[kWalkRight], &g_drawing->have_box[kWalkRight],
          p[0]);
  }
  ++g_walk_boxes;
}

bool BubbleCardBoxIn(void* verts, uint32_t count, uintptr_t caller) {
  if (g_drawing == nullptr || g_eye < 0 || g_eye > 1 || verts == nullptr ||
      count == 0) {
    return false;
  }
  if (caller < kWorldTriangleBegin || caller >= kWorldTriangleEnd) {
    return false;
  }
  bool moved = false;
  View view;
  if (ScopeOn() && count <= kMaxCorners && GetView(&view)) {
    float k[3];
    CameraPointInEye(
        reinterpret_cast<const float*>(bw::tech::kWorldToCameraUnscaled),
        reinterpret_cast<const float*>(bw::tech::kCameraPos), k);
    uint8_t* vertex = static_cast<uint8_t*>(verts);
    double sum = 0.0;
    for (uint32_t i = 0; i < count; ++i, vertex += kStride) {
      float* p = reinterpret_cast<float*>(vertex);
      g_saved_x[i] = p[0];
      g_saved_y[i] = p[1];
      CornerToItsPixel(view, k, p[3], &p[0], &p[1]);
      sum += p[0] - g_saved_x[i];
    }
    g_box_moved += fabs(sum / count);
    ++g_boxes;
    moved = true;
    memcpy(g_at_pixels, verts, static_cast<size_t>(count) * kStride);
  } else if (ScopeOn() && count > kMaxCorners) {
    ++g_too_large;
  }
  // With no draw of its own for the right eye, one walk moves each corner by
  // the rhw of the corner. That place is for the log.
  const bool by_rhw = InWalk() && !(moved && OneWalkMarks() &&
                                    g_drawing->walk_frame ==
                                        ControllersFrameCount());
  const float per_rhw = by_rhw ? OneWalkShiftPerRhw() : 0.0f;
  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kStride) {
    const float* p = reinterpret_cast<const float*>(vertex);
    Widen(g_drawing->drawn_box[g_eye], &g_drawing->have_box[g_eye], p[0]);
    if (by_rhw) {
      Widen(g_drawing->drawn_box[kWalkRight],
            &g_drawing->have_box[kWalkRight], p[0] + per_rhw * p[3]);
    }
  }
  return moved;
}

void BubbleCardBoxOut(void* verts, uint32_t count) {
  if (verts == nullptr || count > kMaxCorners) return;
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kStride) {
    float* p = reinterpret_cast<float*>(vertex);
    p[0] = g_saved_x[i];
    p[1] = g_saved_y[i];
  }
}

void BubbleCardReport() {
  if (g_installed && g_name_frames > 0) {
    ModLog("name card    : name_card=%d. The name of a villager was in view on "
           "%llu frames", g_name_on ? 1 : 0, g_name_frames);
    g_name_frames = 0;
  }
  if (!g_installed || g_frames_shown == 0) return;
  ModLog("bubble card  : bubble_card=%d. A bubble of the game was in view on "
         "%llu frames. %llu asks got the layout of the frame, and %llu of "
         "them had asked for a different one. The side of the frame before "
         "stayed near the middle on %llu frames. %llu boxes went to their "
         "pixels (%.1f pixels across in the mean), %llu "
         "flat batches were drawn at the pixels of the game, %llu boxes had "
         "too many corners. In one walk %llu boxes were drawn for the right "
         "eye with the eye difference of the sign (one_walk_marks=%d)",
         g_on ? 1 : 0, g_frames_shown, g_layouts_kept, g_layouts_differ,
         g_sides_kept, g_boxes, g_boxes > 0 ? g_box_moved / g_boxes : 0.0,
         g_on ? g_flat : 0, g_too_large, g_walk_boxes,
         OneWalkMarks() ? 1 : 0);
  g_frames_shown = g_layouts_kept = g_layouts_differ = g_sides_kept = 0;
  g_boxes = g_flat = g_too_large = g_walk_boxes = 0;
  g_box_moved = 0.0;
}
