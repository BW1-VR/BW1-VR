// Foveated resolution: the outer image and the centre image, a test
// (fovea_res_math.h). main.cpp includes this file after Check and Near,
// just before main.
#pragma once

#include "../../src/mod/fovea_res_math.h"

namespace fovea_res_checks {

inline void FoveaResChecks() {
  using namespace fovea_res_math;
  printf("\nFoveated resolution: the outer image and the centre image\n");

  // A picture of 4000 by 3200 pixels, 100 by 80 degrees.
  const float th = tanf(50.0f / 57.2957795f), tv = tanf(40.0f / 57.2957795f);
  Layout l = MakeLayout(4000, 3200, th, tv, 0.0f, 0.0f, 50.0f, 100.0f, 25.0f);
  Check(l.outer_w == 2000 && l.outer_h == 1600,
        "outer 50 percent: 2000 by 1600 pixels", l.outer_w, 2000);
  Near("the outer scale is 0.5", l.outer_scale_x, 0.5, 1e-6);
  const float half = tanf(25.0f / 57.2957795f) / th * 2000.0f;
  Near("the centre rect is 25 degrees on each side: its width",
       l.inset_w, floorf(2.0f * half), 1.0);
  Near("straight ahead the centre rect is in the middle: x",
       l.inset_x0 + 0.5f * l.inset_w, 2000.0, 1.0);
  Near("straight ahead the centre rect is in the middle: y",
       l.inset_y0 + 0.5f * l.inset_h, 1600.0, 1.0);
  Check(l.center_w == static_cast<uint32_t>(l.inset_w + 0.5f),
        "centre 100 percent: one pixel of the image for a pixel of the game",
        l.center_w, l.inset_w);
  Near("the centre scale is 1", l.center_scale_x, 1.0, 1e-3);
  const float share = PixelShare(l, 4000, 3200);
  Check(share > 0.25f && share < 0.6f,
        "outer 50 and centre 100 with 25 degrees: under 60 percent of the "
        "pixels",
        share, 0.4);

  // More pixels in the centre.
  Layout m = MakeLayout(4000, 3200, th, tv, 0.0f, 0.0f, 40.0f, 150.0f, 20.0f);
  Near("centre 150 percent: the centre scale is 1.5", m.center_scale_x, 1.5,
       2e-3);
  Check(m.outer_w == 1600, "outer 40 percent: 1600 pixels wide", m.outer_w,
        1600);

  // The gaze moves the rect, and the rect stays in the picture.
  Layout g = MakeLayout(4000, 3200, th, tv, th * 0.5f, 0.0f, 50.0f, 100.0f,
                        25.0f);
  Near("the gaze half way to the right: the rect is round x 3000",
       g.inset_x0 + 0.5f * g.inset_w, 3000.0, 1.0);
  Check(g.center_w == l.center_w, "the gaze does not change the size",
        g.center_w, l.center_w);
  Layout e = MakeLayout(4000, 3200, th, tv, th * 1.5f, -tv * 1.5f, 50.0f,
                        100.0f, 25.0f);
  Check(e.inset_x0 + e.inset_w <= 4000.0f && e.inset_y0 + e.inset_h <= 3200.0f &&
            e.inset_x0 >= 0.0f && e.inset_y0 >= 0.0f,
        "a gaze past the corner keeps the rect in the picture", e.inset_x0,
        4000.0f - e.inset_w);
  Layout big = MakeLayout(4000, 3200, th, tv, 0.0f, 0.0f, 50.0f, 100.0f,
                          70.0f);
  Check(big.inset_w <= 4000.0f && big.inset_h <= 3200.0f,
        "70 degrees: the rect is at most the picture", big.inset_w, 4000);

  // The map of a rect.
  float r[4];
  MapRect(1000.0f, 800.0f, 3000.0f, 2400.0f, l.inset_x0, l.inset_y0,
          l.center_scale_x, l.center_scale_y, r);
  Near("a rect in the centre image: x0", r[0],
       (1000.0f - l.inset_x0) * l.center_scale_x, 1e-3);
  MapRect(0.0f, 0.0f, 4000.0f, 3200.0f, 0.0f, 0.0f, l.outer_scale_x,
          l.outer_scale_y, r);
  Near("the whole picture in the outer image: x1", r[2], 2000.0, 1e-3);
  Near("the whole picture in the outer image: y1", r[3], 1600.0, 1e-3);
}

}  // namespace fovea_res_checks
