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
// With bubble_card=0 the mod changes nothing of the bubble.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "bubble_card.h"
#include "bubble_card_math.h"

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

typedef int(__fastcall* PlaceFn)(void* self, void* unused,
                                 const float* position, float offset,
                                 float anchor);
typedef void(__fastcall* DrawFn)(void* self, void* unused);
PlaceFn o_Place = nullptr;
DrawFn o_Draw = nullptr;

const uint32_t kFvf = 0x1C4;  // XYZRHW, two colors, one texture: 32 bytes
const int kStride = 32;
const uint32_t kMaxCorners = 64;

bool g_on = true;        // bubble_card
bool g_test = false;     // bubble_card_test: a line of the log each 30 frames
int g_test_frame = 0;    // bubble_card_test_frame
bool g_installed = false;
int g_eye = -1;

// One bubble of the game, by its address.
struct Seen {
  const void* bubble = nullptr;
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
  float depth = 0.0f;
  float drawn_box[2][2] = {};   // the least and the largest x of the corners
  float drawn_flat[2][2] = {};
  bool have_box[2] = {false, false};
  bool have_flat[2] = {false, false};
};
const int kMaxSeen = 16;
Seen g_seen[kMaxSeen];
Seen* g_drawing = nullptr;
int g_scope = 0;

float g_saved_x[kMaxCorners];
float g_saved_y[kMaxCorners];

// For the report.
uint64_t g_frames_shown = 0;
uint64_t g_layouts_kept = 0;   // asks that got the layout of the frame
uint64_t g_layouts_differ = 0; // of those, asks for a different layout
uint64_t g_sides_kept = 0;
uint64_t g_boxes = 0;
double g_box_moved = 0.0;
uint64_t g_flat = 0;
uint64_t g_too_large = 0;

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

void __fastcall h_Draw(void* self, void* unused) {
  if (g_eye < 0 || g_eye > 1 || self == nullptr) {
    o_Draw(self, unused);
    return;
  }
  Seen* outer = g_drawing;
  g_drawing = SeenOf(self);
  ++g_scope;
  o_Draw(self, unused);
  --g_scope;
  Seen* seen = g_drawing;
  g_drawing = outer;
  seen->drew[g_eye] = true;
  // The log: one line when a bubble comes into view, and with the test key
  // one line each 30 frames.
  if (g_eye != 1 || !seen->drew[0] || !seen->placed[0] || !seen->placed[1] ||
      !seen->have_box[0] || !seen->have_box[1]) {
    return;
  }
  const uint64_t frame = ControllersFrameCount();
  if (seen->first_frame != 0 && seen->last_said < seen->first_frame) {
    seen->last_said = frame;
    Say(*seen, "a bubble of the game comes into view");
  } else if (g_test && frame - seen->last_said >= 30) {
    seen->last_said = frame;
    Say(*seen, "test key bubble_card_test: a bubble in view");
  }
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

void BubbleCardSetOptions(int on, int test, int test_frame) {
  g_on = on != 0;
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

bool BubbleCardOn() { return g_on; }

void BubbleCardFlat(const void* verts, uint32_t fvf, uint32_t count) {
  if (g_drawing == nullptr || g_eye < 0 || g_eye > 1) return;
  ++g_flat;
  if (verts == nullptr || fvf != kFvf) return;
  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kStride) {
    Widen(g_drawing->drawn_flat[g_eye], &g_drawing->have_flat[g_eye],
          *reinterpret_cast<const float*>(vertex));
  }
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
  if (g_on && count <= kMaxCorners && GetView(&view)) {
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
  } else if (g_on && count > kMaxCorners) {
    ++g_too_large;
  }
  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kStride) {
    Widen(g_drawing->drawn_box[g_eye], &g_drawing->have_box[g_eye],
          *reinterpret_cast<const float*>(vertex));
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
  if (!g_installed || g_frames_shown == 0) return;
  ModLog("bubble card  : bubble_card=%d. A bubble of the game was in view on "
         "%llu frames. %llu asks got the layout of the frame, and %llu of "
         "them had asked for a different one. The side of the frame before "
         "stayed near the middle on %llu frames. %llu boxes went to their "
         "pixels (%.1f pixels across in the mean), %llu "
         "flat batches were drawn at the pixels of the game, %llu boxes had "
         "too many corners",
         g_on ? 1 : 0, g_frames_shown, g_layouts_kept, g_layouts_differ,
         g_sides_kept, g_boxes, g_boxes > 0 ? g_box_moved / g_boxes : 0.0,
         g_on ? g_flat : 0, g_too_large);
  g_frames_shown = g_layouts_kept = g_layouts_differ = g_sides_kept = 0;
  g_boxes = g_flat = g_too_large = 0;
  g_box_moved = 0.0;
}
