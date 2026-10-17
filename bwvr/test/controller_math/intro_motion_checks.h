// THE MOTION OF THE LIONHEAD LOGO IN STEREO (docs/intro.md, "The motion" and
// "The flat text and the boxes").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/intro_motion_math.h"

namespace intro_motion_checks {

// The matrix of LH3DTech::ProjectPoint for a camera at `place` that looks
// along -z, moved `eye` units along its right axis. The right axis is -x.
inline void RestMatrix(const float place[3], float eye,
                       const intro_motion::View& view, float m[12]) {
  const float right[3] = {-1.0f, 0.0f, 0.0f};
  const float up[3] = {0.0f, 1.0f, 0.0f};
  const float forward[3] = {0.0f, 0.0f, -1.0f};
  const float at[3] = {place[0] + right[0] * eye, place[1], place[2]};
  for (int row = 0; row < 3; ++row) {
    m[row * 3 + 0] = right[row] * view.inv_half_tan_x;
    m[row * 3 + 1] = up[row] * view.inv_half_tan_y;
    m[row * 3 + 2] = forward[row];
  }
  m[9] = -(at[0] * right[0] + at[1] * right[1] + at[2] * right[2]) *
         view.inv_half_tan_x;
  m[10] = -(at[0] * up[0] + at[1] * up[1] + at[2] * up[2]) *
          view.inv_half_tan_y;
  m[11] = -(at[0] * forward[0] + at[1] * forward[1] + at[2] * forward[2]);
}

inline void IntroMotionChecks() {
  using namespace intro_motion;
  printf("\nThe motion of the Lionhead logo in stereo\n");

  float p[3], f[3];

  // THE CAMERA OF THE GAME, against the camera that the game asked for in a
  // run (the lines "the game asks for the camera at" of the log).
  GameCamera(0.0f, p, f);
  Near("clock 0: the camera of the game, x", p[0], 22.728, 0.03);
  Near("clock 0: y", p[1], -24.748, 0.03);
  Near("clock 0: z", p[2], 16.427, 0.03);
  Check(f[0] == 0.0f && f[1] == 0.0f && f[2] == 0.0f,
        "clock 0: it looks at the logo", f[2], 0.0);
  GameCamera(10.0f, p, f);
  Near("clock 10: x", p[0], -10.291, 0.05);
  Near("clock 10: y", p[1], 14.237, 0.05);
  Near("clock 10: z", p[2], 11.821, 0.05);
  GameCamera(15.01f, p, f);
  Near("clock 15: the end of the orbit, x", p[0], 8.452, 0.05);
  Near("clock 15: y", p[1], 5.229, 0.05);
  Near("clock 15: z", p[2], 1.492, 0.05);
  Near("clock 15: 10 units from the logo",
       sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]), 10.0, 0.1);
  GameCamera(20.01f, p, f);
  Near("clock 20: on the way to the rest pose, x", p[0], 10.084, 0.15);
  Near("clock 20: y", p[1], -19.710, 0.15);
  Near("clock 20: z", p[2], 27.112, 0.15);
  GameCamera(26.01f, p, f);
  Near("clock 26: the rest pose, x", p[0], 0.0, 0.001);
  Near("clock 26: y", p[1], 0.0, 0.001);
  Near("clock 26: z", p[2], 100.0, 0.001);
  GameCamera(31.01f, p, f);
  Near("clock 31: through the logo, z", p[2], 49.672, 0.3);
  Near("clock 31: it looks along -z, 100 units ahead", f[2] - p[2], -100.0,
       0.001);
  GameCamera(36.03f, p, f);
  Near("clock 36: the end, z", p[2], -200.0, 0.001);
  Near("clock 36: the focus", f[2], -300.0, 0.001);
  Check(Part(3.0f) == 0 && Part(15.0f) == 1 && Part(26.0f) == 2 &&
            Part(30.0f) == 3 && Part(40.0f) == 3,
        "the four parts of the path", Part(26.0f), 2);

  // What the game eases: the move to the rest pose has no speed at its start
  // and at its end. The move through the logo starts at full speed.
  Near("the ease starts with no speed", Ease(0.01f), 0.0, 0.001);
  Near("the ease ends with no speed", Ease(0.99f), 1.0, 0.001);
  Near("the ease is half at the middle", Ease(0.5f), 0.5, 0.0001);
  {
    float a[3], b[3], g[3];
    GameCamera(25.9f, a, g);
    GameCamera(26.0f, b, g);
    Near("the camera comes to the rest pose slowly: 0.1 of the clock before "
         "it, it is less than 0.1 units away", b[2] - a[2], 0.0, 0.1);
    GameCamera(30.0f, a, g);
    GameCamera(30.1f, b, g);
    Near("the move through the logo starts at 50 units for each unit of the "
         "clock", (a[2] - b[2]) / 0.1, 50.0, 0.05);
  }

  // THE FIXED HEADING.
  float heading[3];
  RestHeading(heading);
  Check(heading[0] == 0.0f && heading[1] == 0.0f && heading[2] == -1.0f,
        "the fixed heading is the direction of the rest pose, -z",
        heading[2], -1.0);

  // INTRO_MOTION=1: the place of the camera of the game, and no turn.
  float room_p[3], room_f[3], orb_p[3], orb_f[3], game_p[3], game_f[3];
  bool turned = false, moved = false, two = false;
  float before[3] = {0.0f, 0.0f, 0.0f};
  for (int step = 0; step <= 360; ++step) {
    const float clock = step * 0.1f;
    GameCamera(clock, game_p, game_f);
    two |= Cameras(kPath, game_p, game_f, room_p, room_f, orb_p, orb_f);
    for (int i = 0; i < 3; ++i) {
      if (fabsf(room_p[i] - game_p[i]) > 1e-5f) moved = true;
      if (fabsf(orb_p[i] - room_p[i]) > 1e-5f) two = true;
      const float want = heading[i] * kRestDistance;
      if (fabsf((room_f[i] - room_p[i]) - want) > 1e-3f) turned = true;
    }
    if (step > 0 && step <= 300) {
      // 0.1 of the clock is 91 ms. No jump of the place before the move
      // through the logo.
      const float dx = room_p[0] - before[0], dy = room_p[1] - before[1],
                  dz = room_p[2] - before[2];
      if (sqrtf(dx * dx + dy * dy + dz * dz) > 2.0f) moved = true;
    }
    for (int i = 0; i < 3; ++i) before[i] = room_p[i];
  }
  Check(!moved, "motion 1: the camera is at the place of the camera of the "
        "game for the whole path, and the place makes no jump", 0, 0);
  Check(!turned, "motion 1: the direction is the fixed heading at each clock, "
        "so the path does not turn the view", 0, 0);
  Check(!two, "motion 1: one camera draws the room and the orbs", 0, 0);
  GameCamera(28.0f, game_p, game_f);
  Cameras(kPath, game_p, game_f, room_p, room_f, orb_p, orb_f);
  Near("motion 1: at the rest pose the logo is straight ahead",
       LogoAngleDeg(room_p, room_f), 0.0, 0.01);
  Near("motion 1: and 100 units away", room_p[2], 100.0, 0.001);
  GameCamera(5.0f, game_p, game_f);
  Cameras(kPath, game_p, game_f, room_p, room_f, orb_p, orb_f);
  Check(LogoAngleDeg(room_p, room_f) > 20.0f,
        "motion 1: in the orbit the camera does not turn to the logo",
        LogoAngleDeg(room_p, room_f), 20.0);
  Near("the camera of the game does turn to it",
       LogoAngleDeg(game_p, game_f), 0.0, 0.01);

  // INTRO_MOTION=0 and 3.
  Check(!Cameras(kStill, game_p, game_f, room_p, room_f, orb_p, orb_f) &&
            room_p[2] == 100.0f && room_f[2] == 0.0f && orb_p[2] == 100.0f,
        "motion 0: the rest pose at each clock", room_p[2], 100.0);
  Check(!Cameras(kGame, game_p, game_f, room_p, room_f, orb_p, orb_f) &&
            room_p[0] == game_p[0] && room_f[1] == game_f[1] &&
            orb_p[2] == game_p[2],
        "motion 3: the camera of the game as it is", room_p[0], game_p[0]);

  // INTRO_MOTION=2: the room stays still, and the model moves.
  float nearest = 1e9f;
  bool still_room = true, ahead = true;
  for (int step = 0; step <= 300; ++step) {
    GameCamera(step * 0.1f, game_p, game_f);
    const bool is_two =
        Cameras(kModel, game_p, game_f, room_p, room_f, orb_p, orb_f);
    if (!is_two || room_p[0] != 0.0f || room_p[1] != 0.0f ||
        room_p[2] != 100.0f || room_f[2] != 0.0f) {
      still_room = false;
    }
    if (fabsf((orb_f[2] - orb_p[2]) + kRestDistance) > 1e-3f ||
        fabsf(orb_f[0] - orb_p[0]) > 1e-4f) {
      ahead = false;
    }
    if (orb_p[2] < nearest) nearest = orb_p[2];
  }
  Check(still_room, "motion 2: the camera of the room is the rest pose at "
        "each clock", 0, 0);
  Check(ahead, "motion 2: the camera of the orbs has the fixed heading", 0, 0);
  Check(nearest > 50.0f && nearest < 52.0f,
        "motion 2: before the move through the logo the model is 50 units "
        "from the head at the least", nearest, 50.7);
  GameCamera(27.0f, game_p, game_f);
  Cameras(kModel, game_p, game_f, room_p, room_f, orb_p, orb_f);
  Near("motion 2: at the rest pose the two cameras are the same",
       orb_p[2] - room_p[2], 0.0, 0.0001);
  GameCamera(32.8f, game_p, game_f);
  Cameras(kModel, game_p, game_f, room_p, room_f, orb_p, orb_f);
  Near("motion 2: the head comes to the nearest orbs (z = 30) at clock 32.8",
       orb_p[2], 30.0, 0.5);

  // The test key and the scale.
  Check(NextMode(1) == 2 && NextMode(2) == 3 && NextMode(3) == 0 &&
            NextMode(0) == 1,
        "the test key goes 1, 2, 3, 0 and 1 again", NextMode(3), 0);
  Check(ClampMode(7) == kPath && ClampMode(-1) == kPath && ClampMode(2) == 2,
        "a value that is no mode gives motion 1", ClampMode(7), kPath);
  Near("3 m to the logo at rest: 33.3 units for a meter", UnitsPerMeter(3.0f),
       33.333, 0.001);
  Near("2 m for the model: 50 units for a meter", UnitsPerMeter(2.0f), 50.0,
       0.001);
  Check(UnitsPerMeter(0.0f) == 0.0f, "no distance gives no scale", 0, 0);
  Near("a speed of 50 percent", ClockSpeed(50), 0.5, 0.0001);
  Near("the effect keeps its speed at 0.1 or more", ClockSpeed(3), 0.1,
       0.0001);
  Near("and at 1 or less", ClockSpeed(250), 1.0, 0.0001);

  // THE FLAT TEXT AND THE BOXES. A render of 2208 by 2208 with a view of
  // 110 degrees, as in a desktop run.
  const View view = {1104.0f, 1104.0f, 0.70021f, 0.70021f};
  float m[12];
  RestMatrix(kRest, 0.0f, view, m);
  {
    // An orb of a run: the log has its place, and the picture of the left
    // eye has it at 829.3, 1139.9.
    const float orb[3] = {31.018f, -3.918f, 15.867f};
    float left[12];
    RestMatrix(kRest, -1.1133f, view, left);
    float x = 0.0f, y = 0.0f;
    Check(Project(left, view, orb, 0.1f, &x, &y),
          "an orb in front of the camera has a pixel", 1, 1);
    Near("the pixel of an orb in the left eye is where a run drew it, x", x,
         829.3, 0.3);
    Near("and y", y, 1139.9, 0.3);
  }
  float point[3];
  Check(PlanePoint(view, 1104.0f, 1104.0f, point) && point[0] == 0.0f &&
            point[1] == 0.0f && point[2] == 0.0f,
        "the middle of the picture is the middle of the logo", point[2], 0.0);
  PlanePoint(view, 1428.0f, 718.0f, point);
  Check(point[0] < 0.0f && point[1] > 0.0f && point[2] == 0.0f,
        "the mark above the right corner of the logo is at -x and +y, on the "
        "plane of the logo", point[0], -41.9);
  float x = 1428.0f, y = 718.0f;
  Check(PlaceFlat(m, view, 0.1f, &x, &y), "the rest camera sees the mark", 1,
        1);
  Near("the rest camera draws the mark where the game draws it, x", x, 1428.0,
       0.01);
  Near("and y", y, 718.0, 0.01);
  // The two eyes, 2.227 units apart (66.8 mm at 33.3 units for a meter).
  float left[12], right[12];
  RestMatrix(kRest, -1.1133f, view, left);
  RestMatrix(kRest, 1.1133f, view, right);
  float lx = 1428.0f, ly = 718.0f, rx = 1428.0f, ry = 718.0f;
  PlaceFlat(left, view, 0.1f, &lx, &ly);
  PlaceFlat(right, view, 0.1f, &rx, &ry);
  Near("the mark in the right eye is 17.2 pixels left of the mark in the "
       "left eye: the depth of the logo, 100 units", rx - lx, -17.21, 0.05);
  Near("and at the same height", ry - ly, 0.0, 0.001);
  {
    // An orb at 100 units has the same difference.
    const float orb[3] = {-20.0f, 10.0f, 0.0f};
    float ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
    Project(left, view, orb, 0.1f, &ox0, &oy0);
    Project(right, view, orb, 0.1f, &ox1, &oy1);
    Near("an orb on the plane of the logo has the same difference", ox1 - ox0,
         rx - lx, 0.001);
  }
  // A camera that is nearer draws the mark farther from the middle, and a
  // camera behind the plane does not draw it.
  const float near_place[3] = {0.0f, 0.0f, 50.0f};
  RestMatrix(near_place, 0.0f, view, m);
  x = 1428.0f;
  y = 718.0f;
  PlaceFlat(m, view, 0.1f, &x, &y);
  Near("at half the distance the mark is two times as far from the middle",
       x - 1104.0, 648.0, 0.05);
  const float behind[3] = {0.0f, 0.0f, -20.0f};
  RestMatrix(behind, 0.0f, view, m);
  x = 1428.0f;
  y = 718.0f;
  Check(!PlaceFlat(m, view, 0.1f, &x, &y),
        "a camera behind the plane of the logo does not draw the mark", 0, 0);
  const View none = {0.0f, 0.0f, 0.0f, 0.0f};
  Check(!PlanePoint(none, 1.0f, 1.0f, point), "no view gives no place", 0, 0);

  // The width of the logo, from the homes of the orbs.
  const float homes[9] = {-38.0f * 0.7f, 0.0f, 30.0f,   // 70 units away
                          38.0f * 1.3f,  0.0f, -30.0f,  // 130 units away
                          0.0f,          5.0f, 0.0f};
  Near("the width of the logo on its plane, from orbs at each depth",
       LogoWidth(homes, 3), 76.0, 0.001);
  Check(LogoWidth(homes, 0) == 0.0f, "no orbs give no width", 0, 0);

  // THE PUSH OF THE HAND: the motion of the pixel of the hand, as counts of
  // the mouse.
  HandPush hand = {false, 0.0f, 0.0f, 0.0f, 0.0f};
  int dx = 0, dy = 0;
  HandPushStep(&hand, true, 1000.0f, 600.0f, 100, &dx, &dy);
  Check(dx == 0 && dy == 0, "the first pixel of the hand gives no motion", dx,
        dy);
  HandPushStep(&hand, true, 1012.0f, 595.0f, 100, &dx, &dy);
  Check(dx == 12 && dy == -5,
        "12 pixels right and 5 up give 12 counts right and 5 up", dx, dy);
  HandPushStep(&hand, true, 1012.0f, 595.0f, 100, &dx, &dy);
  Check(dx == 0 && dy == 0, "a still hand gives no motion", dx, dy);
  HandPushStep(&hand, true, 1022.0f, 595.0f, 50, &dx, &dy);
  Check(dx == 5 && dy == 0, "50 percent gives half the counts", dx, dy);
  int sum = 0;
  for (int i = 1; i <= 10; ++i) {
    HandPushStep(&hand, true, 1022.0f + 0.5f * i, 595.0f, 100, &dx, &dy);
    sum += dx;
  }
  Check(sum == 5, "a slow hand, 0.5 pixels a frame for 10 frames, gives 5 "
        "counts: the fraction stays", sum, 5);
  HandPushStep(&hand, false, 0.0f, 0.0f, 100, &dx, &dy);
  Check(dx == 0 && dy == 0, "a frame with no pixel gives no motion", dx, dy);
  HandPushStep(&hand, true, 300.0f, 900.0f, 100, &dx, &dy);
  Check(dx == 0 && dy == 0,
        "a pixel that comes back after a gap is not a motion", dx, dy);
  HandPushStep(&hand, true, 290.0f, 900.0f, 0, &dx, &dy);
  Check(dx == 0 && dy == 0, "0 percent gives no motion", dx, dy);
  HandPushStep(&hand, true, 280.0f, 900.0f, 100, &dx, &dy);
  Check(dx == 0 && dy == 0,
        "after 0 percent the next pixel starts again with no motion", dx, dy);

  // THE PUSH IN 3D: an orb in the reach of a hand goes with the hand.
  const HandPose rest = {{10.0f, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
  HandPose hand_moved = rest;
  hand_moved.position[0] = 12.0f;
  float delta[3];
  const float at_hand[3] = {10.5f, 0.0f, 0.0f};
  Check(DragPoint(rest, hand_moved, 3.0f, at_hand, delta),
        "an orb near the hand is in its reach", 1, 1);
  Near("it goes with the hand, one to one: 2 units across", delta[0], 2.0,
       1e-5);
  Near("and not up", delta[1], 0.0, 1e-5);
  const float edge[3] = {12.25f, 0.0f, 0.0f};
  DragPoint(rest, hand_moved, 3.0f, edge, delta);
  Near("an orb at 3/4 of the radius goes half as far", delta[0], 1.0, 1e-5);
  const float far_orb[3] = {14.0f, 0.0f, 0.0f};
  Check(!DragPoint(rest, hand_moved, 3.0f, far_orb, delta),
        "an orb past the radius stays", 0, 0);
  // A turn of the hand of 90 degrees about z, in place: an orb 1 unit along
  // x of the hand goes to 1 unit along y.
  HandPose hand_turned = rest;
  const float quarter[9] = {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 1.0f};
  for (int i = 0; i < 9; ++i) hand_turned.rotation[i] = quarter[i];
  const float beside[3] = {11.0f, 0.0f, 0.0f};
  DragPoint(rest, hand_turned, 3.0f, beside, delta);
  Near("a turn of the hand turns the orb with it, across", delta[0], -1.0,
       1e-5);
  Near("and up", delta[1], 1.0, 1e-5);
  const float still_orb[3] = {10.5f, 0.5f, 0.0f};
  DragPoint(rest, rest, 3.0f, still_orb, delta);
  Near("a hand that does not move moves no orb", delta[0] + delta[1] + delta[2],
       0.0, 1e-6);
  float inv[9];
  const float flat[9] = {1, 0, 0, 0, 0, 0, 0, 0, 1};
  Check(!Invert3(flat, inv), "a flat matrix has no inverse", 0, 0);

  // THE SHOW OF THE HANDS: a hand shows while it moves and fades at rest.
  HandShow show = {false, {0, 0, 0}, 0.0f, 0.0f, 0.0f};
  const float dt = 1.0f / 90.0f;
  float place[3] = {0.0f, 0.0f, 0.0f};
  float opacity = 0.0f;
  for (int i = 0; i < 90; ++i) {
    place[1] = 0.0002f * ((i % 2) * 2 - 1);  // the shake of the tracking
    opacity = HandShowStep(&show, true, place, dt);
  }
  Check(opacity == 0.0f, "a hand at rest with the shake of the tracking does "
        "not show", opacity, 0.0);
  for (int i = 0; i < 30; ++i) {
    place[0] += 0.5f * dt;  // 0.5 m a second
    opacity = HandShowStep(&show, true, place, dt);
  }
  Near("a hand that moves 0.5 m a second shows in full after 1/3 s", opacity,
       1.0, 1e-6);
  for (int i = 0; i < 27; ++i) opacity = HandShowStep(&show, true, place, dt);
  Near("for 0.3 s after it stops it still shows", opacity, 1.0, 1e-6);
  for (int i = 0; i < 90; ++i) opacity = HandShowStep(&show, true, place, dt);
  Check(opacity == 0.0f, "1 s after it stops it does not show", opacity, 0.0);
  show.opacity = 1.0f;
  for (int i = 0; i < 46; ++i) opacity = HandShowStep(&show, false, place, dt);
  Check(opacity == 0.0f, "a hand that is not tracked fades in 0.5 s",
        opacity, 0.0);
}

}  // namespace intro_motion_checks
