// THE CUT SCENE WINDOW (docs/cutscene-window.md).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/cutscene_window_math.h"

namespace window_checks {

inline void WindowChecks() {
  using namespace window_math;
  printf("\nThe cut scene window\n");

  // THE RULE.
  Check(Wanted(true, false, false, 100.0f, 3.0f),
        "the bars are up: the window is wanted", 1, 1);
  Check(Wanted(true, false, true, 100.0f, 3.0f),
        "the bars are up and a story owns the camera: wanted", 1, 1);
  Check(!Wanted(false, false, true, 100.0f, 3.0f),
        "a story camera with no bars does not start the window", 0, 0);
  Check(!Wanted(false, false, false, 100.0f, 3.0f),
        "play with no bars: not wanted", 0, 0);
  Check(Wanted(false, true, true, 1.0f, 3.0f),
        "the bars went down and the story still owns the camera: it stays",
        1, 1);
  Check(!Wanted(false, true, false, 0.1f, 3.0f),
        "the bars are down and the camera is back: the window goes", 0, 0);
  Check(!Wanted(false, true, true, 3.5f, 3.0f),
        "a story camera that stays past the limit: the window goes", 0, 0);

  // THE STATE, at 90 frames a second and a change of half a second.
  {
    const float dt = 1.0f / 90.0f;
    State s = {false, 1.0f};
    int frames = 0, changes = 0;
    float before = s.open;
    bool down_only = true;
    while (!s.flat && frames < 1000) {
      if (Step(&s, true, dt, 0.5f)) ++changes;
      if (s.open > before) down_only = false;
      before = s.open;
      ++frames;
    }
    Near("the view closes in 45 frames at 90 Hz", frames, 45, 1);
    Check(down_only, "the opening only gets smaller while it closes", 1, 1);
    Check(s.flat && s.open == 0.0f, "then the frame is flat", s.open, 0);
    Check(changes == 1, "the flat state starts one time", changes, 1);
    // The window stays for as long as it is wanted.
    for (int i = 0; i < 500; ++i) Step(&s, true, dt, 0.5f);
    Check(s.flat && s.open == 0.0f, "the window stays while it is wanted",
          s.open, 0);
    // The leave: the first frame is a full view frame, with the opening at
    // the size of the window.
    const bool left = Step(&s, false, dt, 0.5f);
    Check(left && !s.flat, "the first frame after the cut scene is not flat",
          s.flat ? 1 : 0, 0);
    Near("and its opening is one frame past the window", s.open, dt / 0.5f,
         1e-5);
    frames = 1;
    bool up_only = true;
    before = s.open;
    while (s.open < 1.0f && frames < 1000) {
      Step(&s, false, dt, 0.5f);
      if (s.open < before) up_only = false;
      before = s.open;
      ++frames;
    }
    Near("the view opens in 45 frames", frames, 45, 1);
    Check(up_only, "the opening only grows while it opens", 1, 1);
    // A cut scene that starts again while the view opens.
    State again = {false, 0.5f};
    Step(&again, true, dt, 0.5f);
    Check(!again.flat && again.open < 0.5f,
          "a new cut scene closes the view from where it is", again.open,
          0.5f - dt / 0.5f);
    // A change of no length is a cut.
    State cut = {false, 1.0f};
    Step(&cut, true, dt, 0.0f);
    Check(cut.flat, "with a time of 0 the view is flat on the next frame",
          cut.flat ? 1 : 0, 1);
    // A frame of no length changes nothing.
    State still = {false, 0.5f};
    Step(&still, true, 0.0f, 0.5f);
    Near("a frame of no length does not move the opening", still.open, 0.5f,
         1e-6);
  }

  // THE SHRINK.
  Near("at the window the opening has the size of the screen", OpenScale(0.0f),
       1.0, 1e-6);
  Near("at the full view it is 4 times the screen", OpenScale(1.0f),
       kFullScale, 1e-5);
  Near("half way it is 2 times the screen", OpenScale(0.5f), 2.0, 1e-5);
  {
    bool grows = true, smooth = true;
    float last = OpenScale(0.0f);
    for (int i = 1; i <= 100; ++i) {
      const float now = OpenScale(i / 100.0f);
      if (now < last) grows = false;
      // No step of 1 percent of the time changes the size by 4 percent.
      if (now / last > 1.04f) smooth = false;
      last = now;
    }
    Check(grows, "the scale grows with the opening, at each step", 1, 1);
    Check(smooth, "and no step is a jump", 1, 1);
    // The start and the end are slow: the first percent of the time moves
    // the size less than a percent in the middle does.
    const float first = OpenScale(0.01f) / OpenScale(0.0f);
    const float middle = OpenScale(0.51f) / OpenScale(0.50f);
    Check(first < middle, "the change starts slowly", first, middle);
    // 4 times a screen of 2.6 m at 2 m is past the view of the headset:
    // more than 55 degrees to each side.
    const float half_angle = atanf(0.5f * 2.6f * OpenScale(1.0f) / 2.0f);
    Check(half_angle > 55.0f * kDeg,
          "the full opening is wider than the view of the headset",
          half_angle / kDeg, 55);
  }
  Near("the walls are clear at the full view", MaskAlpha(1.0f), 0.0, 1e-6);
  Near("the walls are black at the window", MaskAlpha(0.0f), 1.0, 1e-6);
  Near("the walls are black for the last half of the close", MaskAlpha(0.5f),
       1.0, 1e-6);
  {
    bool darker = true;
    float last = MaskAlpha(1.0f);
    for (int i = 99; i >= 0; --i) {
      const float now = MaskAlpha(i / 100.0f);
      if (now < last) darker = false;
      last = now;
    }
    Check(darker, "the walls only get more black while the view closes", 1,
          1);
  }

  // THE SCREEN POSE. The recenter pose: the head 1.6 m up, no turn.
  {
    const float anchor[3] = {0.2f, 1.6f, -0.1f};
    const float no_turn[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    window_math::Screen s;
    ScreenPose(anchor, no_turn, 2.0f, 2.6f, 1.0f, &s);
    Near("the screen is 2 m ahead of the recenter pose (z)", s.position[2],
         -2.1, 1e-5);
    Near("at the height of the head", s.position[1], 1.6, 1e-6);
    Near("and not to a side", s.position[0], 0.2, 1e-6);
    Near("a square render gives a square screen", s.height, 2.6, 1e-6);
    ScreenPose(anchor, no_turn, 2.0f, 2.6f, 0.5625f, &s);
    Near("a 16 by 9 render gives a screen 1.4625 m high", s.height, 1.4625,
         1e-5);
    // A recenter pose that faces left by 90 degrees (a turn about y).
    const float left[4] = {0.0f, sinf(45.0f * kDeg), 0.0f, cosf(45.0f * kDeg)};
    ScreenPose(anchor, left, 2.0f, 2.6f, 1.0f, &s);
    Near("after a recenter to the left the screen is to the left (x)",
         s.position[0], 0.2 - 2.0, 1e-5);
    Near("at the same depth as the head (z)", s.position[2], -0.1, 1e-5);
    // The front of the screen (+z) faces the anchor.
    const float z_axis[3] = {0.0f, 0.0f, 1.0f};
    float front[3];
    Rotate(s.orientation, z_axis, front);
    const float to_anchor[3] = {anchor[0] - s.position[0],
                                anchor[1] - s.position[1],
                                anchor[2] - s.position[2]};
    Near("the front of the screen faces the recenter pose",
         (front[0] * to_anchor[0] + front[1] * to_anchor[1] +
          front[2] * to_anchor[2]) / 2.0,
         1.0, 1e-5);
    // The screen does not follow the head: the pose has no head in it. The
    // same anchor gives the same screen.
    window_math::Screen t;
    ScreenPose(anchor, left, 2.0f, 2.6f, 1.0f, &t);
    Near("the same recenter pose gives the same screen", t.position[0],
         s.position[0], 0.0);
    // The angle of the screen from the recenter pose: 66 degrees across.
    Near("a screen of 2.6 m at 2 m is 66 degrees wide",
         2.0f * atanf(1.3f / 2.0f) / kDeg, 66.0, 0.1);
  }

  // THE WALLS.
  {
    const float anchor[3] = {0.0f, 1.6f, 0.0f};
    const float no_turn[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    window_math::Screen s;
    ScreenPose(anchor, no_turn, 2.0f, 2.6f, 1.0f, &s);
    Wall w[4];
    Walls(s, 2.6f, 2.6f, 40.0f, w);
    Near("the top wall starts at the top edge of the opening",
         w[0].position[1] - 0.5f * w[0].height, 1.6 + 1.3, 1e-4);
    Near("the bottom wall ends at the bottom edge",
         w[1].position[1] + 0.5f * w[1].height, 1.6 - 1.3, 1e-4);
    Near("the left wall ends at the left edge",
         w[2].position[0] + 0.5f * w[2].width, -1.3, 1e-4);
    Near("the right wall starts at the right edge",
         w[3].position[0] - 0.5f * w[3].width, 1.3, 1e-4);
    Near("the side walls are as high as the opening", w[2].height, 2.6, 1e-6);
    Near("the top wall covers the corners", w[0].width, 2.6 + 80.0, 1e-4);
    Near("the walls are in the plane of the screen", w[0].position[2],
         s.position[2], 1e-6);
    Near("and the side walls too", w[3].position[2], s.position[2], 1e-6);
    // 40 m of wall at 2 m covers 87 degrees to each side.
    Check(atanf((1.3f + 40.0f) / 2.0f) > 87.0f * kDeg,
          "the walls reach 87 degrees to each side", 1, 1);
    // A larger opening moves each wall out by half of the growth.
    Wall big[4];
    Walls(s, 5.2f, 5.2f, 40.0f, big);
    Near("an opening 2 times as wide moves the right wall 1.3 m out",
         big[3].position[0] - w[3].position[0], 1.3, 1e-4);
    // With a turned screen the walls turn with it.
    const float left[4] = {0.0f, sinf(45.0f * kDeg), 0.0f, cosf(45.0f * kDeg)};
    ScreenPose(anchor, left, 2.0f, 2.6f, 1.0f, &s);
    Walls(s, 2.6f, 2.6f, 40.0f, w);
    Near("a screen on the left has its right wall toward -z",
         w[3].position[2] - s.position[2], -(1.3 + 20.0), 1e-3);
    Near("and that wall stays in the plane of the screen (x)",
         w[3].position[0], s.position[0], 1e-4);
  }

  // THE STEREO OF THE WINDOW: two cameras side by side, 66.8 mm of the room
  // apart at 7.5 units per meter, a view of 70 degrees on a screen 2.6 m
  // wide at 2 m.
  {
    const float ipd = 0.0668f;
    const float separation = ipd * 7.5f;
    const float fov = 70.0f * kDeg;
    Near("a point very far away has no disparity",
         ScreenDisparity(2.6f, fov, separation, 1e9f), 0.0, 1e-6);
    Near("so the eyes see it on the screen, 2 m away",
         SeenDistance(2.0f, ipd, ScreenDisparity(2.6f, fov, separation, 1e9f)),
         2.0, 1e-4);
    // A point 15 units from the camera is 2 m of the life size scale.
    const float d = ScreenDisparity(2.6f, fov, separation, 15.0f);
    Near("a point 15 units away is drawn 62 mm apart", d, 0.0620, 5e-4);
    Near("and the eyes see it 1.04 m away, in front of the screen",
         SeenDistance(2.0f, ipd, d), 1.037, 5e-3);
    // Nearer points come nearer, and never go behind the screen.
    bool nearer = true, in_front = true;
    float last = 2.0f;
    for (float depth = 2000.0f; depth > 1.0f; depth *= 0.5f) {
      const float seen = SeenDistance(
          2.0f, ipd, ScreenDisparity(2.6f, fov, separation, depth));
      if (seen >= last) nearer = false;
      if (seen > 2.0f) in_front = false;
      last = seen;
    }
    Check(nearer, "a nearer point is seen nearer", 1, 1);
    Check(in_front, "no point is seen behind the screen", 1, 1);
    // A disparity as large as the eye separation halves the distance.
    Near("a disparity of one eye separation is seen at 1 m",
         SeenDistance(2.0f, ipd, ipd), 1.0, 1e-5);
    // A screen that is as wide as the view it shows (2 S tan(fov / 2)) puts
    // a point at Z units at 1 / (1 / S + u / Z) meters.
    const float exact_width = 2.0f * 2.0f * tanf(fov * 0.5f);
    Near("a screen as wide as its view: 30 units are seen at 1.33 m",
         SeenDistance(2.0f, ipd,
                      ScreenDisparity(exact_width, fov, separation, 30.0f)),
         1.0 / (1.0 / 2.0 + 7.5 / 30.0), 1e-4);
    // No eye separation gives a flat picture on the screen.
    Near("with no separation the picture is flat, on the screen",
         SeenDistance(2.0f, ipd, ScreenDisparity(2.6f, fov, 0.0f, 15.0f)), 2.0,
         1e-6);
  }

  // THE PLACE OF THE SCREEN: ahead of the head of the moment that the cut
  // scene starts.
  {
    const float none[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    Near("a head that looks along -z has a yaw of 0", YawOf(none), 0.0, 1e-6);
    float left30[4];
    YawTurn(30.0f * kDeg, left30);
    Near("a head turned 30 degrees to the left has a yaw of 30",
         YawOf(left30) / kDeg, 30.0, 1e-3);
    // The same head looks 60 degrees down: a turn about its own x axis.
    const float down[4] = {sinf(-30.0f * kDeg), 0.0f, 0.0f,
                           cosf(-30.0f * kDeg)};
    const float q[4] = {
        left30[3] * down[0] + left30[1] * down[2] - left30[2] * down[1],
        left30[3] * down[1] + left30[1] * down[3] + left30[2] * down[0],
        left30[3] * down[2] + left30[2] * down[3] - left30[1] * down[0],
        left30[3] * down[3] - left30[1] * down[1]};
    Near("a head that also looks 60 degrees down keeps its yaw of 30",
         YawOf(q) / kDeg, 30.0, 1e-2);

    const float head[3] = {0.5f, 1.2f, -0.3f};
    Anchor a;
    AnchorAtHead(head, q, 1.6f, &a);
    Near("the anchor is at the head along the floor (x)", a.position[0], 0.5,
         1e-6);
    Near("and along the floor (z)", a.position[2], -0.3, 1e-6);
    Near("the anchor has the height that the caller gives", a.position[1],
         1.6, 1e-6);
    window_math::Screen s;
    ScreenPose(a.position, a.turn, 2.0f, 2.6f, 1.0f, &s);
    Near("the screen is 2 m ahead of the head at its yaw (x)", s.position[0],
         0.5 - 2.0 * sin(30.0 * kDeg), 1e-4);
    Near("and 2 m ahead at its yaw (z)", s.position[2],
         -0.3 - 2.0 * cos(30.0 * kDeg), 1e-4);
    Near("the screen is at the height of the anchor, not of the look",
         s.position[1], 1.6, 1e-6);
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float screen_up[3];
    Rotate(s.orientation, up, screen_up);
    Near("the screen is level: no pitch and no roll", screen_up[1], 1.0,
         1e-6);
    const float front[3] = {0.0f, 0.0f, 1.0f};
    float normal[3];
    Rotate(s.orientation, front, normal);
    Near("the front of the screen is toward the head",
         normal[0] * (a.position[0] - s.position[0]) +
             normal[2] * (a.position[2] - s.position[2]),
         2.0, 1e-4);
  }

  // THE DEPTH LIMIT: eyes 66.8 mm apart, a picture 2.6 m wide of 2880
  // columns, 2 m away, with a view of 70 degrees.
  {
    const float ipd = 0.0668f;
    const float half_tan = tanf(35.0f * kDeg);
    const int crop = Crop(ipd, 2.6f, 2880);
    Check(crop == 72, "each eye picture loses 72 columns", crop, 72);
    Check(Crop(0.0650f, 2.6f, 2880) % 2 == 0 &&
              Crop(0.0700f, 2.6f, 1920) % 2 == 0,
          "the crop is an even number, so the interface moves whole pixels",
          0, 0);
    const float shift = Shift(crop, 2.6f, 2880);
    Check(shift <= ipd, "the two pictures are not more than the eyes apart",
          shift, ipd);
    Near("and less than 2 pixels short of it", ipd - shift, 0.0,
         2.0 * 2.6 / 2880.0);
    // No size of the render and no pair of eyes makes the eyes turn out.
    bool never_out = true, within_pixel = true;
    for (int res = 640; res <= 4096; res += 128) {
      for (float eyes = 0.054f; eyes < 0.076f; eyes += 0.0007f) {
        const float apart = Shift(Crop(eyes, 2.6f, res), 2.6f, res);
        if (apart > eyes) never_out = false;
        if (eyes - apart > 2.0f * 2.6f / res + 1e-6f) within_pixel = false;
      }
    }
    Check(never_out, "the eyes never turn out, at each render size", 1, 1);
    Check(within_pixel, "and a point far away is less than 2 pixels from "
          "parallel", 1, 1);
    Check(Crop(ipd, 2.6f, 0) == 0 && Crop(0.0f, 2.6f, 2880) == 0,
          "with no render size or no eyes there is no crop", 0, 0);

    // The plane at the life size scale: 2 m at 7.5 units per meter.
    const float plane = 15.0f;
    const float e = SeparationFor(shift, 2.6f, half_tan, plane);
    Near("a point at the plane has no parallax", Parallax(shift, 2.6f,
         half_tan, e, plane), 0.0, 1e-6);
    Near("so the eyes see it on the screen, 2 m away",
         SeenBehind(2.0f, ipd, Parallax(shift, 2.6f, half_tan, e, plane)),
         2.0, 1e-4);
    Check(Parallax(shift, 2.6f, half_tan, e, 10.0f) < 0.0f,
          "a point nearer than the plane is in front of the screen", 1, 1);
    bool behind = true, farther = true;
    float last = 2.0f;
    for (float depth = plane * 1.01f; depth < 1e6f; depth *= 1.5f) {
      const float p = Parallax(shift, 2.6f, half_tan, e, depth);
      const float seen = SeenBehind(2.0f, ipd, p);
      if (p <= 0.0f || p > ipd) behind = false;
      if (seen <= last) farther = false;
      last = seen;
    }
    Check(behind, "each point past the plane is behind the screen, and the "
          "eyes do not turn out", 1, 1);
    Check(farther, "a point that is farther away is seen farther away", 1, 1);
    Check(last > 50.0f, "a point very far away is seen more than 50 m away",
          last, 50);
    // With pictures exactly the eyes apart, depth is in proportion: 30 units
    // are seen 2 times as far as the plane of 15 units.
    const float exact = SeparationFor(ipd, 2.6f, half_tan, plane);
    Near("30 units are seen at 4 m, as through a window",
         SeenBehind(2.0f, ipd, Parallax(ipd, 2.6f, half_tan, exact, 30.0f)),
         4.0, 1e-3);
    Near("and 60 units at 8 m",
         SeenBehind(2.0f, ipd, Parallax(ipd, 2.6f, half_tan, exact, 60.0f)),
         8.0, 1e-2);
    Near("the eye cameras are 0.54 units apart at the life size scale", exact,
         2.0 * tan(35.0 * kDeg) * 0.0668 * 15.0 / 2.6, 1e-5);

    // THE RULE.
    DepthRule rule;
    Near("the rule keeps a margin of 15 percent", rule.margin, 0.85, 1e-6);
    Near("a far scene has the plane at the life size scale: 15 units",
         PlaneTarget(100.0f, rule), 15.0, 1e-5);
    Near("a thing at 10 units puts the plane at 8.5 units",
         PlaneTarget(10.0f, rule), 8.5, 1e-5);
    Near("a thing at the near plane puts the plane at the near plane",
         PlaneTarget(3.5f, rule), 3.5, 1e-6);
    Near("no measure gives the worst case: the near plane",
         PlaneTarget(0.0f, rule), 3.5, 1e-6);
    bool not_in_front = true;
    for (float nearest = 3.5f; nearest < 300.0f; nearest *= 1.07f) {
      if (PlaneTarget(nearest, rule) > nearest) not_in_front = false;
    }
    Check(not_in_front, "the plane is never past the nearest thing", 1, 1);
    DepthRule small = rule;
    small.units_per_meter = 1.0f;
    Near("a scale under the near plane limits the plane to the scale",
         PlaneTarget(0.0f, small), 2.0, 1e-6);

    // THE PLANE OF A FRAME, at 90 frames a second.
    const float dt = 1.0f / 90.0f;
    DepthState s;
    Near("the first frame in the window has the worst case",
         PlaneStep(&s, 0.0f, false, dt, rule), 3.5, 1e-6);
    Check(!s.have, "and it is not a measure", 0, 0);
    Near("the first measure sets the plane at once",
         PlaneStep(&s, 40.0f, false, dt, rule), 15.0, 1e-5);
    // A cut to a close shot: the frame of the cut has no measure.
    Near("a frame with no measure keeps the plane", PlaneStep(&s, 0.0f,
         false, dt, rule), 15.0, 1e-5);
    Near("the first measure of the new shot snaps the plane",
         PlaneStep(&s, 8.0f, true, dt, rule), 6.8, 1e-5);
    Near("and a cut to a far shot snaps it back",
         PlaneStep(&s, 80.0f, true, dt, rule), 15.0, 1e-5);
    // Inside a shot a thing comes to 8 units and stays.
    int frames = 0;
    float before = s.plane;
    bool down_only = true;
    while (s.plane > 6.8f * 1.02f && frames < 900) {
      PlaneStep(&s, 8.0f, false, dt, rule);
      if (s.plane > before) down_only = false;
      before = s.plane;
      ++frames;
    }
    Check(frames == 1,
          "inside a shot the plane goes to a nearer thing at once", frames,
          1);
    Check(down_only, "and it only comes nearer", 1, 1);
    // The thing goes away. The plane waits, and then goes back slowly.
    const float held = s.plane;
    for (int i = 0; i < 170; ++i) PlaneStep(&s, 80.0f, false, dt, rule);
    Near("a thing that went away changes nothing for 2 s", s.plane, held,
         0.05);
    for (int i = 0; i < 90; ++i) PlaneStep(&s, 80.0f, false, dt, rule);
    Check(s.plane > held + 0.5f && s.plane < held + 3.0f,
          "then the plane goes back slowly: about 1.6 units in the next "
          "second", s.plane, held + 1.6);
    float fastest = 0.0f;
    for (int i = 0; i < 1800; ++i) {
      const float was = s.plane;
      PlaneStep(&s, 80.0f, false, dt, rule);
      if ((s.plane - was) / dt > fastest) fastest = (s.plane - was) / dt;
    }
    Near("after 20 s it is at the life size scale again", s.plane, 15.0,
         0.1);
    Check(fastest < 0.25f * 15.0f,
          "on the way back the plane changes less than a quarter of its "
          "range in a second", fastest, 3.75);
    // A measure that goes up and down by 10 percent does not pump.
    DepthState noisy;
    PlaneStep(&noisy, 10.0f, false, dt, rule);
    float low = 1e9f, high = 0.0f;
    for (int i = 0; i < 900; ++i) {
      PlaneStep(&noisy, i % 2 == 0 ? 9.0f : 11.0f, false, dt, rule);
      if (i > 90) {
        if (noisy.plane < low) low = noisy.plane;
        if (noisy.plane > high) high = noisy.plane;
      }
    }
    Check(high - low < 0.02f * low,
          "a measure with 10 percent of noise moves the plane less than 2 "
          "percent", high - low, 0);
    Check(high <= 9.0f,
          "and the plane stays at the nearest of the noise", high, 9);
    // A thing that comes nearer by 17 units a second stays behind the screen.
    DepthState walk;
    PlaneStep(&walk, 40.0f, false, dt, rule);
    bool stays_behind = true;
    float worst = 1e9f;
    float measured = 40.0f;
    for (float nearest = 40.0f; nearest > 5.0f; nearest -= 17.0f * dt) {
      // The measure is one frame old.
      PlaneStep(&walk, measured, false, dt, rule);
      measured = nearest;
      const float seen = 2.0f * nearest / walk.plane;
      if (seen < worst) worst = seen;
      if (walk.plane > nearest) stays_behind = false;
    }
    Check(stays_behind,
          "a thing that comes nearer by 17 units a second stays behind the "
          "screen", worst, 2.0);
    // The same at 60 units a second, which is 8 m a second at life size.
    DepthState run;
    PlaneStep(&run, 60.0f, false, dt, rule);
    stays_behind = true;
    measured = 60.0f;
    for (float nearest = 60.0f; nearest > 5.0f; nearest -= 60.0f * dt) {
      PlaneStep(&run, measured, false, dt, rule);
      measured = nearest;
      if (run.plane > nearest) stays_behind = false;
    }
    Check(stays_behind,
          "and a thing that comes nearer by 60 units a second stays behind "
          "it too", 1, 1);
    Check(PlaneStep(&walk, 5.0f, false, 0.0f, rule) == walk.plane,
          "a frame of no length changes nothing", 0, 0);

    // A NEW SHOT.
    const float p0[3] = {100.0f, 20.0f, 100.0f};
    const float f0[3] = {0.0f, 0.0f, 1.0f};
    const float f10[3] = {sinf(10.0f * kDeg), 0.0f, cosf(10.0f * kDeg)};
    const float f25[3] = {sinf(25.0f * kDeg), 0.0f, cosf(25.0f * kDeg)};
    Check(!ShotCut(p0, f0, p0, f10, 30.0f),
          "a turn of 10 degrees in a frame is not a new shot", 0, 0);
    Check(ShotCut(p0, f0, p0, f25, 30.0f),
          "a turn of 25 degrees in a frame is a new shot", 1, 1);
    const float p_glide[3] = {101.0f, 20.0f, 100.0f};
    const float p_far[3] = {120.0f, 20.0f, 100.0f};
    Check(!ShotCut(p0, f0, p_glide, f0, 30.0f),
          "a glide of 1 unit in a frame is not a new shot", 0, 0);
    Check(ShotCut(p0, f0, p_far, f0, 30.0f),
          "a move of 20 units with the nearest thing at 30 is a new shot", 1,
          1);
    Check(!ShotCut(p0, f0, p_far, f0, 300.0f),
          "the same move high over the land is not a new shot", 0, 0);
    Check(ShotCut(p0, f0, p_far, f0, 0.0f),
          "with no measure a move of 20 units is a new shot", 1, 1);
  }

  // THE NEAREST DEPTH IN VIEW: a render of 2880 by 2880 with cinema bars of
  // 630 pixels, so the scene is the rows 630 to 2250.
  {
    struct V {
      float x, y, z, rhw;
      unsigned color, specular;
      float u, v;
    };
    Check(sizeof(V) == 32, "a vertex of the game is 32 bytes", sizeof(V), 32);
    DepthGrid g;
    GridStart(&g, 0.0f, 630.0f, 2880.0f, 2250.0f);
    Near("an empty grid has no nearest depth", GridNearest(g, 2), 0.0, 0.0);
    const unsigned short tri[3] = {0, 1, 2};
    const unsigned short one[3] = {0, 0, 0};
    // A triangle in the view: its nearest vertex counts.
    const V inside[3] = {{1000.0f, 1000.0f, 0.5f, 0.10f, 0, 0, 0, 0},
                         {1400.0f, 1000.0f, 0.5f, 0.25f, 0, 0, 0, 0},
                         {1200.0f, 1400.0f, 0.5f, 0.05f, 0, 0, 0, 0}};
    GridBatch(&g, inside, 3, tri, 3, sizeof(V));
    Near("a triangle in the view gives its nearest vertex", g.strict, 0.25,
         1e-6);
    Check(g.points == 3, "and each of its vertices counts one time", g.points,
          3);
    // A vertex that no triangle uses does not count. The game leaves a
    // vertex of the land that is out of the frustum in camera space: here x
    // 12 and y 700, with a depth of 45 units in the place of rhw.
    GridStart(&g, 0.0f, 630.0f, 2880.0f, 2250.0f);
    const V with_spare[4] = {inside[0], inside[1], inside[2],
                             {12.0f, 700.0f, 0.5f, 45.0f, 0, 0, 0, 0}};
    GridBatch(&g, with_spare, 4, tri, 3, sizeof(V));
    Check(g.points == 3 && g.strict == 0.25f,
          "a vertex that no triangle uses does not count", g.points, 3);
    // Two triangles that share an edge: each vertex counts one time.
    const unsigned short two[6] = {0, 1, 2, 2, 1, 0};
    GridStart(&g, 0.0f, 630.0f, 2880.0f, 2250.0f);
    GridBatch(&g, inside, 3, two, 6, sizeof(V));
    Check(g.points == 3, "a vertex that two triangles share counts one time",
          g.points, 3);
    // A triangle past the left edge does not count.
    GridStart(&g, 0.0f, 630.0f, 2880.0f, 2250.0f);
    const V left_of[3] = {{-900.0f, 1000.0f, 0.5f, 0.9f, 0, 0, 0, 0},
                          {-100.0f, 1000.0f, 0.5f, 0.9f, 0, 0, 0, 0},
                          {-500.0f, 1400.0f, 0.5f, 0.9f, 0, 0, 0, 0}};
    GridBatch(&g, left_of, 3, tri, 3, sizeof(V));
    Check(g.points == 0, "a triangle out of view gives nothing", g.points, 0);
    // A near vertex under the top bar: only the part in view counts. The
    // edge from (1000, 130, 0.9) to (1100, 930, 0.2) is at rhw 0.4625 where
    // it comes out of the bar.
    const V under_bar[3] = {{1000.0f, 130.0f, 0.5f, 0.9f, 0, 0, 0, 0},
                            {1100.0f, 930.0f, 0.5f, 0.2f, 0, 0, 0, 0},
                            {1200.0f, 930.0f, 0.5f, 0.2f, 0, 0, 0, 0}};
    GridBatch(&g, under_bar, 3, tri, 3, sizeof(V));
    Near("a vertex behind a cinema bar does not count, and the part of its "
         "triangle in view does", g.strict, 0.4625, 1e-4);
    // A large triangle of the land with all its vertices out of view: rhw is
    // 0.0001 for each pixel down, so the nearest point in view is at the
    // bottom of the scene.
    GridStart(&g, 0.0f, 630.0f, 2880.0f, 2250.0f);
    const V land[3] = {{-20000.0f, 5000.0f, 0.5f, 0.5f, 0, 0, 0, 0},
                       {20000.0f, 5000.0f, 0.5f, 0.5f, 0, 0, 0, 0},
                       {1440.0f, -5000.0f, 0.5f, -0.5f + 1e-3f, 0, 0, 0, 0}};
    V land_ok[3] = {land[0], land[1], land[2]};
    land_ok[2].y = 0.0f;
    land_ok[2].rhw = 1e-6f;
    GridBatch(&g, land_ok, 3, tri, 3, sizeof(V));
    Near("a triangle that covers the view with no vertex in it gives the "
         "nearest point in view", g.strict, 0.5 * 2250.0 / 5000.0, 1e-3);
    GridStart(&g, 0.0f, 630.0f, 2880.0f, 2250.0f);
    GridBatch(&g, land, 3, tri, 3, sizeof(V));
    Check(g.points == 0, "a triangle with a vertex behind the camera is left "
          "out", g.points, 0);
    // Nearer than the near plane counts as the near plane.
    const V too_near[3] = {{1000.0f, 1000.0f, 0.5f, 6077.0f, 0, 0, 0, 0},
                           {1400.0f, 1000.0f, 0.5f, 0.2f, 0, 0, 0, 0},
                           {1200.0f, 1400.0f, 0.5f, 0.2f, 0, 0, 0, 0}};
    GridBatch(&g, too_near, 3, tri, 3, sizeof(V));
    Near("a vertex nearer than the near plane counts as the near plane",
         g.strict, 1.0, 1e-6);
    // One stray vertex does not set the depth: the second nearest cell does.
    GridStart(&g, 0.0f, 630.0f, 2880.0f, 2250.0f);
    const V stray[1] = {{2000.0f, 2000.0f, 0.5f, 0.9f, 0, 0, 0, 0}};
    GridBatch(&g, stray, 1, one, 3, sizeof(V));
    Near("with one cell, the nearest depth is that cell", GridNearest(g, 2),
         0.9, 1e-6);
    GridBatch(&g, inside, 3, tri, 3, sizeof(V));
    Near("one stray vertex does not set the nearest depth",
         GridNearest(g, kGridRank), 0.25, 1e-6);
    Near("the strict nearest still has it", g.strict, 0.9, 1e-6);
    // A draw in screen space has an rhw of 1 in each vertex, and no depth.
    const V sea[4] = {{0.0f, 1500.0f, 0.5f, 1.0f, 0, 0, 0, 0},
                      {2880.0f, 1500.0f, 0.5f, 1.0f, 0, 0, 0, 0},
                      {2880.0f, 1520.0f, 0.5f, 1.0f, 0, 0, 0, 0},
                      {0.0f, 1520.0f, 0.5f, 1.0f, 0, 0, 0, 0}};
    const unsigned short strip[6] = {0, 1, 2, 0, 2, 3};
    const unsigned before_sea = g.points;
    GridBatch(&g, sea, 4, strip, 6, sizeof(V));
    Check(g.points == before_sea,
          "a batch with an rhw of 1 in each vertex is left out", g.points,
          before_sea);
    // Two vertices of one thing in two cells do set it.
    const V second[1] = {{2400.0f, 2000.0f, 0.5f, 0.8f, 0, 0, 0, 0}};
    GridBatch(&g, second, 1, one, 3, sizeof(V));
    Near("a thing that is in two cells sets the nearest depth",
         GridNearest(g, kGridRank), 0.8, 1e-6);
    // THE TWO EYES. With a crop of 72 columns the left eye shows the columns
    // 72 to 2880 of its picture, and the right eye the columns 0 to 2808.
    GridStart(&g, 72.0f, 630.0f, 2880.0f, 2250.0f);
    const V at_left[1] = {{30.0f, 1000.0f, 0.5f, 0.6f, 0, 0, 0, 0}};
    GridBatch(&g, at_left, 1, one, 3, sizeof(V));
    Check(g.points == 0,
          "a vertex in the columns that the left eye lost is not in its view",
          g.points, 0);
    GridEye(&g, 0.0f, 2808.0f);
    GridBatch(&g, at_left, 1, one, 3, sizeof(V));
    Check(g.points == 1 && g.strict == 0.6f,
          "the same column of the right eye picture is in view", g.points, 1);
    const V at_right[1] = {{2850.0f, 1000.0f, 0.5f, 0.7f, 0, 0, 0, 0}};
    GridBatch(&g, at_right, 1, one, 3, sizeof(V));
    Check(g.points == 1,
          "a vertex in the columns that the right eye lost is not in its "
          "view", g.points, 1);
    // A thing on the screen plane is 72 columns apart in the two pictures,
    // and it is in one cell for both eyes.
    GridStart(&g, 72.0f, 630.0f, 2880.0f, 2250.0f);
    const V on_plane_left[1] = {{1072.0f, 1000.0f, 0.5f, 0.3f, 0, 0, 0, 0}};
    const V on_plane_right[1] = {{1000.0f, 1000.0f, 0.5f, 0.3f, 0, 0, 0, 0}};
    GridBatch(&g, on_plane_left, 1, one, 3, sizeof(V));
    GridEye(&g, 0.0f, 2808.0f);
    GridBatch(&g, on_plane_right, 1, one, 3, sizeof(V));
    int cells_used = 0;
    for (float c : g.cell) {
      if (c > 0.0f) ++cells_used;
    }
    Check(cells_used == 1 && g.points == 2,
          "a thing on the screen plane is in one cell for both eyes",
          cells_used, 1);
    // The depth in units: the near plane divided by rhw.
    Near("rhw 0.25 with a near plane of 3.5 units is 14 units", 3.5 / 0.25,
         14.0, 1e-9);
  }

  // THE SUBTITLES OF THE WINDOW: a card in the plane of the screen, under
  // the scene.
  {
    Near("the scene of a square picture of 2.6 m is 1.46 m high",
         2.0 * SceneHalfHeight(2.6f, 2.6f, 0.5625f), 1.4625, 1e-5);
    Near("a picture wider than 16 to 9 has no bars, so the scene is as high "
         "as the picture", 2.0 * SceneHalfHeight(2.6f, 1.3f, 0.5625f), 1.3,
         1e-6);
    const float anchor[3] = {0.0f, 1.5f, 0.0f};
    const float none[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    window_math::Screen s;
    ScreenPose(anchor, none, 2.0f, 2.6f, 1.0f, &s);
    const float top = SceneHalfHeight(2.6f, 2.6f, 0.5625f) + 0.04f;
    const float rect[4] = {0.0f, 1900.0f, 2880.0f, 384.0f};
    TextQuad q;
    SubtitleQuad(s, top, 1.4f, 2880.0f, rect, &q);
    Near("the whole width of the render is 1.4 m on the card", q.width, 1.4,
         1e-5);
    Near("a strip of 384 pixels is 19 cm high", q.height, 0.18667, 1e-4);
    Near("the card is in the middle under the screen", q.position[0], 0.0,
         1e-5);
    Near("the card is in the plane of the screen", q.position[2],
         s.position[2], 1e-6);
    Near("the top of the card is 4 cm under the scene",
         q.position[1] + 0.5f * q.height, 1.5 - 0.73125 - 0.04, 1e-5);
    Check(q.position[1] - 0.5f * q.height > 1.5f - 1.3f,
          "and the card is over the bottom edge of a square screen: on its "
          "black bar", q.position[1] - 0.5f * q.height, 0.2);
    Near("the top of the card is 21 degrees below eye level",
         atan((0.73125 + 0.04) / 2.0) / kDeg, 21.1, 0.1);
    // A rect with more lines keeps its top.
    const float tall[4] = {0.0f, 1700.0f, 2880.0f, 584.0f};
    TextQuad q2;
    SubtitleQuad(s, top, 1.4f, 2880.0f, tall, &q2);
    Near("a card with more lines has the same top",
         q2.position[1] + 0.5f * q2.height, q.position[1] + 0.5f * q.height,
         1e-5);
    // A rect at the left of the render is at the left of the card.
    const float at_left[4] = {0.0f, 1900.0f, 720.0f, 384.0f};
    SubtitleQuad(s, top, 1.4f, 2880.0f, at_left, &q2);
    Near("a rect at the left of the render is to the left on the card",
         q2.position[0], -(1440.0 - 360.0) * 1.4 / 2880.0, 1e-5);
    // Under the opening and not under the scene.
    SubtitleQuad(s, 1.3f + 0.04f, 1.4f, 2880.0f, rect, &q2);
    Near("with the other place the top of the card is 4 cm under the "
         "opening", q2.position[1] + 0.5f * q2.height, 1.5 - 1.3 - 0.04,
         1e-5);
    // A screen to the left: the card turns with it.
    float left[4];
    YawTurn(90.0f * kDeg, left);
    ScreenPose(anchor, left, 2.0f, 2.6f, 1.0f, &s);
    SubtitleQuad(s, top, 1.4f, 2880.0f, at_left, &q2);
    Near("with a screen on the left the card is in its plane (x)",
         q2.position[0], s.position[0], 1e-5);
    Near("and a rect at the left of the render goes toward +z",
         q2.position[2], (1440.0 - 360.0) * 1.4 / 2880.0, 1e-4);
  }
}

}  // namespace window_checks
