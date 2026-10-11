// A SPEECH BUBBLE AS ONE CARD (docs/ui-layer.md, "A speech bubble as one
// card").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <math.h>

#include "../../src/mod/bubble_card_math.h"

namespace bubble_card_checks {

// A world to camera matrix of the game with no field of view in it: 12
// floats, the basis in the columns and the translation in the last row. The
// camera is at `eye`, turned by a yaw, a pitch and a roll in degrees.
inline void CameraMatrix(const float eye[3], float yaw, float pitch,
                         float roll, float m[12]) {
  const float d = 0.01745329f;
  const float cy = cosf(yaw * d), sy = sinf(yaw * d);
  const float cp = cosf(pitch * d), sp = sinf(pitch * d);
  const float cr = cosf(roll * d), sr = sinf(roll * d);
  // The axes of the camera in the world: right, up and forward.
  const float forward[3] = {sy * cp, sp, cy * cp};
  const float right0[3] = {cy, 0.0f, -sy};
  const float up0[3] = {-sy * sp, cp, -cy * sp};
  float right[3], up[3];
  for (int i = 0; i < 3; ++i) {
    right[i] = right0[i] * cr + up0[i] * sr;
    up[i] = up0[i] * cr - right0[i] * sr;
  }
  for (int i = 0; i < 3; ++i) {
    m[3 * i + 0] = right[i];
    m[3 * i + 1] = up[i];
    m[3 * i + 2] = forward[i];
  }
  for (int j = 0; j < 3; ++j) {
    m[9 + j] = -(eye[0] * m[j] + eye[1] * m[3 + j] + eye[2] * m[6 + j]);
  }
}

// LH3DTech::Get3DPointFromScreen 0x0081B370, as its listing has it: the
// point of the world for a pixel at a depth, from the matrix with no field
// of view in it and from the camera point of the game.
inline void GamePointFromScreen(const bubble_card::View& view,
                                const float m[12], const float camera[3],
                                float px, float py, float depth,
                                float out[3]) {
  const float v[3] = {
      (px - view.half_res_x) / view.half_res_x * depth / view.inv_half_tan_x,
      (view.half_res_y - py) / view.half_res_y * depth / view.inv_half_tan_y,
      depth};
  for (int i = 0; i < 3; ++i) {
    out[i] = m[3 * i] * v[0] + m[3 * i + 1] * v[1] + m[3 * i + 2] * v[2] +
             camera[i];
  }
}

// The draw of a point of the world through the camera of an eye pass: the
// pixel and rhw.
inline void GameProject(const bubble_card::View& view, const float m[12],
                        const float p[3], float* x, float* y, float* rhw) {
  float c[3];
  for (int j = 0; j < 3; ++j) {
    c[j] = p[0] * m[j] + p[1] * m[3 + j] + p[2] * m[6 + j] + m[9 + j];
  }
  *x = view.half_res_x +
       view.half_res_x * view.inv_half_tan_x * c[0] / c[2];
  *y = view.half_res_y -
       view.half_res_y * view.inv_half_tan_y * c[1] / c[2];
  *rhw = view.near_plane / c[2];
}

inline void BubbleCardChecks() {
  using namespace bubble_card;
  printf("\nA speech bubble as one card\n");

  // THE RULE OF THE GAME for the layout, in a picture 2880 pixels wide.
  {
    const Layout left = GameLayout(720.0f, 2880.0f);
    Check(left.offset == 10.0f, "a sign in the left half has the offset +10",
          left.offset, 10.0);
    Near("and the anchor 0.5 at a quarter of the width", left.anchor, 0.5,
         0.0001);
    const Layout right = GameLayout(2160.0f, 2880.0f);
    Check(right.offset == -10.0f,
          "a sign in the right half has the offset -10", right.offset, -10.0);
    Near("and the same anchor", right.anchor, 0.5, 0.0001);
    Near("the anchor is 0.2 in the middle",
         GameLayout(1440.0f, 2880.0f).anchor, 0.2, 0.0001);
    Near("and 0.8 at an edge and past it", GameLayout(4000.0f, 2880.0f).anchor,
         0.8, 0.0001);
    Check(GameLayout(1440.0f, 2880.0f).offset == 10.0f,
          "the middle pixel counts as the left half",
          GameLayout(1440.0f, 2880.0f).offset, 10.0);
  }

  // THE TWO EYES OF A NEAR SIGN. The sign is in the middle for the head: 75
  // pixels to the right in the left picture and 75 to the left in the right
  // one. The game gives the two passes a different side.
  const Layout from_left = GameLayout(1440.0f + 75.0f, 2880.0f);
  const Layout from_right = GameLayout(1440.0f - 75.0f, 2880.0f);
  Check(from_left.offset != from_right.offset,
        "a near sign in the middle: the game asks for a different side in "
        "each eye pass", from_left.offset, from_right.offset);

  // THE LAYOUT OF A FRAME.
  {
    Layout none;
    const Layout first = Choose(from_left, false, none, kSideBand);
    Check(first.offset == from_left.offset && first.anchor == from_left.anchor,
          "the first frame of a bubble has the layout that the game asks "
          "for", first.offset, from_left.offset);
    // The sign goes across the middle by a small turn of the head.
    const Layout next = Choose(from_right, true, first, kSideBand);
    Check(next.offset == first.offset,
          "near the middle the side of the frame before stays", next.offset,
          first.offset);
    Near("and the anchor is the one that the game asks for", next.anchor,
         from_right.anchor, 0.0001);
    // The sign goes far to the other side: the side follows the game.
    const Layout far_side = GameLayout(1440.0f - 400.0f, 2880.0f);
    const Layout turned = Choose(far_side, true, first, kSideBand);
    Check(turned.offset == far_side.offset,
          "far from the middle the side is the one of the game",
          turned.offset, far_side.offset);
    Near("the band of the middle is 86 pixels each way at 2880",
         kSideBand * 1440.0f, 86.4, 0.01);
    Near("a sign 75 pixels from the middle is in the band",
         FromMiddle(from_left), 75.0 / 1440.0, 0.0001);
  }

  // THE PIXEL OF THE SIGN FROM THE BOX. The place function puts the left
  // edge of the box at the pixel, plus the offset, less the anchor times the
  // width.
  {
    const Layout layout = GameLayout(1700.0f, 2880.0f);
    const float width = 640.0f;
    const float box_left = 1700.0f + layout.offset - layout.anchor * width;
    Near("the pixel of the sign comes back from the box",
         SignPixel(box_left, width, layout), 1700.0, 0.001);
  }

  // THE CORNERS OF THE BOX. A render of 2880 by 2880 with a view of 110
  // degrees, a near plane of 0.1 units, eyes 6.6 units apart (66 mm at 100
  // units for each meter), and a bubble 34 units deep.
  View view;
  view.half_res_x = view.half_res_y = 1440.0f;
  view.inv_half_tan_x = view.inv_half_tan_y = 1.0f / tanf(55.0f * 0.01745329f);
  view.near_plane = 0.1f;
  const float separation = 6.6f;
  const float depth = 34.0f;
  const float camera[3] = {1500.0f, 80.0f, 1900.0f};
  float mono[12];
  CameraMatrix(camera, 30.0f, -40.0f, 0.0f, mono);
  {
    float k[3];
    CameraPointInEye(mono, camera, k);
    Check(fabsf(k[0]) < 0.001f && fabsf(k[1]) < 0.001f && fabsf(k[2]) < 0.001f,
          "the camera point of the game is at 0 in its own camera", k[0], 0.0);
  }
  const float corners[4][2] = {{900.0f, 700.0f}, {1540.0f, 700.0f},
                               {900.0f, 1000.0f}, {1540.0f, 1000.0f}};
  for (int eye = 0; eye < 2; ++eye) {
    // The eye is half the separation to the side of the camera point, along
    // the right axis of the camera.
    const float side = (eye == 0 ? -0.5f : 0.5f) * separation;
    const float at[3] = {camera[0] + mono[0] * side, camera[1] + mono[3] * side,
                         camera[2] + mono[6] * side};
    float m[12];
    CameraMatrix(at, 30.0f, -40.0f, 0.0f, m);
    float k[3];
    CameraPointInEye(m, camera, k);
    Near(eye == 0 ? "for the left eye the camera point is half the "
                    "separation to the right"
                  : "for the right eye it is half the separation to the left",
         k[0], -side, 0.001);
    float worst = 0.0f, moved = 0.0f;
    for (int c = 0; c < 4; ++c) {
      float world[3], x = 0.0f, y = 0.0f, rhw = 0.0f;
      GamePointFromScreen(view, m, camera, corners[c][0], corners[c][1],
                          depth, world);
      GameProject(view, m, world, &x, &y, &rhw);
      moved += x - corners[c][0];
      const bool ok = CornerToItsPixel(view, k, rhw, &x, &y);
      const float miss = fabsf(x - corners[c][0]) + fabsf(y - corners[c][1]);
      if (!ok || miss > worst) worst = ok ? miss : 1e9f;
    }
    // Half the eye difference of the depth of the bubble: the focal length
    // (1008.3 pixels) times 3.3 units over 34 units.
    Near(eye == 0 ? "the game draws the box 97.9 pixels to the right in the "
                    "left picture"
                  : "and 97.9 pixels to the left in the right picture",
         moved / 4.0f, eye == 0 ? 97.87 : -97.87, 0.05);
    Check(worst < 0.01f, "each corner goes back to the pixel of the game",
          worst, 0.0);
  }
  // An eye with its own turn and a roll, half the separation away and 2
  // units in front: the rule holds for each camera of an eye pass.
  {
    const float at[3] = {camera[0] + 2.1f, camera[1] - 0.7f, camera[2] + 3.0f};
    float m[12];
    CameraMatrix(at, 36.0f, -33.0f, 12.0f, m);
    float k[3];
    CameraPointInEye(m, camera, k);
    float worst = 0.0f;
    for (int c = 0; c < 4; ++c) {
      float world[3], x = 0.0f, y = 0.0f, rhw = 0.0f;
      GamePointFromScreen(view, m, camera, corners[c][0], corners[c][1],
                          depth, world);
      GameProject(view, m, world, &x, &y, &rhw);
      const bool ok = CornerToItsPixel(view, k, rhw, &x, &y);
      const float miss = fabsf(x - corners[c][0]) + fabsf(y - corners[c][1]);
      if (!ok || miss > worst) worst = ok ? miss : 1e9f;
    }
    Check(worst < 0.02f,
          "a camera of an eye with a turn and a roll: each corner goes back "
          "to its pixel", worst, 0.0);
  }
  // A corner with no depth stays.
  {
    const float k[3] = {3.3f, 0.0f, 0.0f};
    float x = 500.0f, y = 600.0f;
    Check(!CornerToItsPixel(view, k, 0.0f, &x, &y) && x == 500.0f &&
              y == 600.0f,
          "a corner with no depth stays where it is", x, 500.0);
  }
}

}  // namespace bubble_card_checks
