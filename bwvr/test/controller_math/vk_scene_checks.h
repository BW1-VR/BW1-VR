// The target, the viewport and the clears of the Vulkan renderer, a test
// (vk_scene_math.h). main.cpp includes this file after Check and Near, just
// before main.
#pragma once

#include "../../src/mod/vk_scene_math.h"

namespace vk_scene_checks {

inline void VkSceneChecks() {
  using namespace vk_scene_math;
  printf("\nThe target, the viewport and the clears of the Vulkan renderer\n");

  // A target of 4080 by 3264, as a render of a Quest 3.
  Rect r;
  Check(InTarget(ViewportRect(0, 0, 4080, 3264), 4080, 3264, &r) &&
            r.x0 == 0 && r.y0 == 0 && r.x1 == 4080 && r.y1 == 3264,
        "the viewport of the whole target is the whole target", 0, 0);
  Check(InTarget(ViewportRect(100, 200, 5000, 5000), 4080, 3264, &r) &&
            r.x0 == 100 && r.y0 == 200 && r.x1 == 4080 && r.y1 == 3264,
        "a viewport past the edge stops at the edge", 0, 0);
  Check(!InTarget(ViewportRect(4080, 0, 10, 10), 4080, 3264, &r),
        "a viewport right of the target has no pixel", 0, 0);
  const Rect left = {-50, -50, 10, 20};
  Check(InTarget(left, 4080, 3264, &r) && r.x0 == 0 && r.y0 == 0 &&
            r.x1 == 10 && r.y1 == 20,
        "a rect over the top left corner starts at 0 0", 0, 0);

  // A clear with a rect clears the part of the rect in the viewport.
  const Rect viewport = ViewportRect(1000, 500, 2040, 1632);
  const Rect asked = {0, 0, 1500, 1000};
  Check(InTarget(Intersect(asked, viewport), 4080, 3264, &r) &&
            r.x0 == 1000 && r.y0 == 500 && r.x1 == 1500 && r.y1 == 1000,
        "a clear rect is cut to the viewport", 0, 0);
  const Rect outside = {0, 0, 900, 400};
  Check(!InTarget(Intersect(outside, viewport), 4080, 3264, &r),
        "a clear rect outside the viewport clears nothing", 0, 0);

  float c[4];
  ClearColor(0x80FF4000u, c);
  Near("the red of 0x80FF4000 is 1", c[0], 1.0, 1e-6);
  Near("the green of 0x80FF4000 is 64 of 255", c[1], 64.0 / 255.0, 1e-6);
  Near("the blue of 0x80FF4000 is 0", c[2], 0.0, 1e-6);
  Near("the alpha of 0x80FF4000 is 128 of 255", c[3], 128.0 / 255.0, 1e-6);
  Near("a depth of 1 stays 1", ClearDepth(1.0f), 1.0, 0.0);
  Near("a depth over 1 is 1", ClearDepth(2.0f), 1.0, 0.0);
  Near("a depth under 0 is 0", ClearDepth(-1.0f), 0.0, 0.0);

  Near("no one walk: the left eye", Eyes(false, 0), 1, 0);
  Near("one walk, both eyes", Eyes(true, 0), 3, 0);
  Near("one walk, the left eye only", Eyes(true, 1), 1, 0);
  Near("one walk, the right eye only", Eyes(true, 2), 2, 0);

  int eye_target = 0, panel = 0;
  Check(!OffTarget(true, &eye_target, &eye_target),
        "a draw to the target of the eyes goes into the eyes", 0, 0);
  Check(OffTarget(true, &eye_target, &panel),
        "a draw to the panel target does not go into the eyes", 0, 0);
  Check(!OffTarget(false, &eye_target, &panel),
        "with vk_scene_follow=0 each draw goes into the eyes", 0, 0);
  Check(!OffTarget(true, nullptr, &panel),
        "with no known target of the eyes each draw goes into the eyes", 0,
        0);
}

}  // namespace vk_scene_checks
