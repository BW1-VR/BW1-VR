// The math of a speech bubble as one card (docs/ui-layer.md, "A speech
// bubble as one card"). It has no game and no Direct3D in it. bubble_card.cpp
// uses it, and test/controller_math checks it.
#pragma once

namespace bubble_card {

// THE LAYOUT OF A BUBBLE. The game gives it to the place function of the
// bubble (fn_00575EA0): an offset in pixels and the part of the width of the
// box that is left of the sign.
struct Layout {
  float offset = 0.0f;  // +10 or -10 pixels
  float anchor = 0.0f;  // 0.2 to 0.8
};

// The rule of the game (fn_00576F20, 0x005770D6 to 0x00577134). `across` is
// the pixel of the sign across the picture: -1 at the left edge, 0 in the
// middle and 1 at the right edge. A sign in the right half has the box more
// to its left, the offset -10 and the tail on the other side.
inline Layout GameLayout(float pixel_x, float picture_width) {
  Layout layout;
  float across = 0.0f;
  if (picture_width > 0.0f) across = 2.0f * pixel_x / picture_width - 1.0f;
  if (across < -1.0f) across = -1.0f;
  if (across > 1.0f) across = 1.0f;
  layout.offset = across > 0.0f ? -10.0f : 10.0f;
  layout.anchor = (across < 0.0f ? -across : across) * 0.6f + 0.2f;
  return layout;
}

// How far the sign is from the middle of the picture, 0 to 1, from the
// anchor of a layout.
inline float FromMiddle(const Layout& layout) {
  const float from = (layout.anchor - 0.2f) / 0.6f;
  return from < 0.0f ? 0.0f : from;
}

// The sign that a player reads is in the middle of the view, where the rule
// of the game changes the side of the box. Nearer to the middle than this,
// the side of the frame before stays.
const float kSideBand = 0.06f;

// The layout of a frame, from the layout that the game asks for in the first
// eye pass. Each later ask of the frame gets the same one.
inline Layout Choose(const Layout& asked, bool have_last, const Layout& last,
                     float band) {
  Layout layout = asked;
  if (have_last && FromMiddle(asked) < band) layout.offset = last.offset;
  return layout;
}

// The pixel of the sign, from the box that the place function made: its left
// edge and its width, with the layout of the call.
inline float SignPixel(float box_left, float box_width, const Layout& layout) {
  return box_left - layout.offset + layout.anchor * box_width;
}

// THE CORNERS OF THE BOX. GatheringText::DrawBubble does not draw the box at
// its pixels. It makes a point of the world for each corner
// (LH3DTech::Get3DPointFromScreen: the pixel, at the depth of the bubble,
// from the camera point of the game) and draws those points through the
// camera of the eye pass. The camera point does not move with the eye, so a
// corner comes back at a different pixel: it has the eye difference of the
// depth of the bubble, on top of the eye difference of its pixel.

struct View {
  float half_res_x = 0.0f;
  float half_res_y = 0.0f;
  float inv_half_tan_x = 0.0f;
  float inv_half_tan_y = 0.0f;
  float near_plane = 0.0f;
};

// The camera point of the game in the camera space of the eye pass. `m` is
// the world to camera matrix with no field of view in it (12 floats, the
// basis in the columns, the translation in the last row), and `c` is the
// camera point. For the camera of the game itself this is 0 0 0.
inline void CameraPointInEye(const float m[12], const float c[3],
                             float out[3]) {
  for (int j = 0; j < 3; ++j) {
    out[j] = c[0] * m[j] + c[1] * m[3 + j] + c[2] * m[6 + j] + m[9 + j];
  }
}

// One corner as it was drawn: the pixel `x`, `y` and rhw (the near plane
// over the depth). The corner goes back to the pixel that the game made it
// from. False when the corner cannot be placed: then it stays.
inline bool CornerToItsPixel(const View& view, const float k[3], float rhw,
                             float* x, float* y) {
  if (!(rhw > 0.0f) || !(view.near_plane > 0.0f) ||
      !(view.half_res_x > 0.0f) || !(view.half_res_y > 0.0f) ||
      !(view.inv_half_tan_x > 0.0f) || !(view.inv_half_tan_y > 0.0f)) {
    return false;
  }
  // The corner in the camera space of the eye pass.
  const float z = view.near_plane / rhw;
  const float cx = (*x - view.half_res_x) / view.half_res_x * z /
                   view.inv_half_tan_x;
  const float cy = (view.half_res_y - *y) / view.half_res_y * z /
                   view.inv_half_tan_y;
  // The same corner from the camera point of the game.
  const float vx = cx - k[0];
  const float vy = cy - k[1];
  const float vz = z - k[2];
  if (!(vz > 1e-4f)) return false;
  *x = view.half_res_x + view.half_res_x * view.inv_half_tan_x * vx / vz;
  *y = view.half_res_y - view.half_res_y * view.inv_half_tan_y * vy / vz;
  return true;
}

}  // namespace bubble_card
