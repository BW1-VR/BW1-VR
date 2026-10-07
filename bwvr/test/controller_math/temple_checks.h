// THE POINTER IN A ROOM OF THE TEMPLE (docs/controllers.md, "The Temple with
// the controllers"). main.cpp includes this file after Check and Near, just
// before main.
#pragma once

#include "../../src/mod/temple_math.h"

namespace temple_checks {

// A room for the checks: a wall that is a plane (a point and a unit normal
// that points at the player), and a box that is nearer for a part of the
// view. The game gives the point where the line from the eye meets the room.
struct Room {
  float wall_point[3];
  float wall_normal[3];
  bool box;            // a nearer plane at z = box_z for x > box_x
  float box_x, box_z;
};

inline bool LineToPlane(const float from[3], const float d[3],
                        const float point[3], const float normal[3],
                        float out[3]) {
  const float down = temple_math::Dot(d, normal);
  if (fabsf(down) < 1e-6f) return false;
  const float to[3] = {point[0] - from[0], point[1] - from[1],
                       point[2] - from[2]};
  const float t = temple_math::Dot(to, normal) / down;
  if (!(t > 0.0f)) return false;
  for (int i = 0; i < 3; ++i) out[i] = from[i] + d[i] * t;
  return true;
}

// The hit of the game for the pixel of a point: the line from the eye
// through the point, to the room.
inline bool GameHit(const Room& room, const float eye[3], const float through[3],
                    float hit[3]) {
  float d[3] = {through[0] - eye[0], through[1] - eye[1], through[2] - eye[2]};
  const float length = sqrtf(temple_math::Dot(d, d));
  if (!(length > 1e-6f)) return false;
  for (int i = 0; i < 3; ++i) d[i] /= length;
  if (room.box) {
    const float box_point[3] = {0.0f, 0.0f, room.box_z};
    const float box_normal[3] = {0.0f, 0.0f, -1.0f};
    float on_box[3];
    if (LineToPlane(eye, d, box_point, box_normal, on_box) &&
        on_box[0] > room.box_x) {
      for (int i = 0; i < 3; ++i) hit[i] = on_box[i];
      return true;
    }
  }
  return LineToPlane(eye, d, room.wall_point, room.wall_normal, hit);
}

// Runs the pointer for some frames with a ray that does not move. The hit of
// a frame is the hit for the end of the ray of the frame before, as in the
// game. Returns the depth of each of the last two frames.
inline void Run(const Room& room, const float eye[3], const float origin[3],
                const float direction[3], int frames, float fallback,
                temple_math::Depth* depth, float end[3], float* before_last) {
  float asked[3] = {0.0f, 0.0f, 0.0f};
  bool have_asked = false;
  *before_last = 0.0f;
  for (int f = 0; f < frames; ++f) {
    bool ok = false;
    float along = 0.0f, hit[3];
    if (have_asked && GameHit(room, eye, asked, hit)) {
      ok = temple_math::RayToHitPlane(origin, direction, eye, hit, &along);
    }
    *before_last = depth->along;
    const float s =
        temple_math::DepthStep(depth, ok, along, fallback, 2.0f, 450.0f);
    for (int i = 0; i < 3; ++i) asked[i] = origin[i] + direction[i] * s;
    have_asked = true;
  }
  for (int i = 0; i < 3; ++i) end[i] = asked[i];
}

inline void TempleChecks() {
  using namespace temple_math;
  printf("\nThe pointer in a room of the Temple\n");

  // The plane through a hit that faces the eye.
  {
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float hit[3] = {0.0f, 0.0f, 20.0f};
    const float origin[3] = {1.5f, -2.0f, 3.0f};
    float d[3] = {hit[0] - origin[0], hit[1] - origin[1], hit[2] - origin[2]};
    const float length = sqrtf(Dot(d, d));
    for (int i = 0; i < 3; ++i) d[i] /= length;
    float along = 0.0f;
    Check(RayToHitPlane(origin, d, eye, hit, &along),
          "a ray that goes through the hit meets its plane", 1, 1);
    Near("and the distance is the distance to the hit", along, length, 1e-3);
    const float straight[3] = {0.0f, 0.0f, 1.0f};
    Check(RayToHitPlane(origin, straight, eye, hit, &along),
          "a ray that goes straight ahead meets the plane", 1, 1);
    Near("at the depth of the hit less the start of the ray", along, 17.0,
         1e-4);
    const float back[3] = {0.0f, 0.0f, -1.0f};
    Check(!RayToHitPlane(origin, back, eye, hit, &along),
          "a ray that goes away from the plane does not meet it", 0, 0);
    const float side[3] = {1.0f, 0.0f, 0.0f};
    Check(!RayToHitPlane(origin, side, eye, hit, &along),
          "a ray along the plane does not meet it", 0, 0);
    const float behind[3] = {0.0f, 0.0f, 25.0f};
    Check(!RayToHitPlane(behind, straight, eye, hit, &along),
          "a ray that starts behind the plane does not meet it", 0, 0);
    Check(!RayToHitPlane(origin, straight, hit, hit, &along),
          "an eye at the hit gives no plane", 0, 0);
  }

  // The distance from a point to a ray, and the angle at the eye.
  {
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const float d[3] = {0.0f, 0.0f, 1.0f};
    const float p[3] = {3.0f, 4.0f, 10.0f};
    Near("a point 3 and 4 units to the side of a ray is 5 units from it",
         PointToRay(origin, d, p), 5.0, 1e-5);
    const float q[3] = {0.0f, 3.0f, -4.0f};
    Near("a point behind the start measures to the start",
         PointToRay(origin, d, q), 5.0, 1e-5);
    const float a[3] = {0.0f, 0.0f, 10.0f}, b[3] = {10.0f, 0.0f, 10.0f};
    Near("two points 45 degrees apart from the eye", AngleAtEye(origin, a, b),
         45.0, 1e-3);
    Near("a point at the eye gives 180 degrees", AngleAtEye(origin, origin, b),
         180.0, 1e-3);
  }

  // One step of the depth.
  {
    Depth depth = {};
    Near("with no hit the first depth is the depth of the settings",
         DepthStep(&depth, false, 0.0f, 22.5f, 2.0f, 450.0f), 22.5, 1e-5);
    Check(depth.source == kDepthDefault, "and its source is the settings",
          depth.source, kDepthDefault);
    Near("the first hit gives its depth at once",
         DepthStep(&depth, true, 30.0f, 22.5f, 2.0f, 450.0f), 30.0, 1e-5);
    Near("a new hit moves the depth half of the way",
         DepthStep(&depth, true, 40.0f, 22.5f, 2.0f, 450.0f), 35.0, 1e-5);
    Near("a frame with no hit keeps the depth",
         DepthStep(&depth, false, 0.0f, 22.5f, 2.0f, 450.0f), 35.0, 1e-5);
    Check(depth.source == kDepthKept, "and its source is the depth of before",
          depth.source, kDepthKept);
    Near("a hit that is too near gives the least depth in steps",
         DepthStep(&depth, true, 0.5f, 22.5f, 2.0f, 450.0f), 18.5, 1e-5);
    for (int i = 0; i < 20; ++i) {
      DepthStep(&depth, true, 40.0f, 22.5f, 2.0f, 450.0f);
    }
    Near("the depth comes to its target and stays", depth.along, 40.0, 1e-6);
    Near("a hit that is too far gives the most depth",
         DepthStep(&depth, true, 9000.0f, 22.5f, 2.0f, 450.0f), 245.0, 1e-4);
  }

  // The pointer at rest. 7.5 units are 1 m. The eye is at the origin, the
  // hand is 20 cm to the right, 30 cm down and 35 cm ahead, and it points 8
  // degrees to the left and 10 degrees up.
  {
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float origin[3] = {1.5f, -2.25f, 2.625f};
    const float yaw = 8.0f * 0.0174533f, pitch = 10.0f * 0.0174533f;
    const float d[3] = {-sinf(yaw) * cosf(pitch), sinf(pitch),
                        cosf(yaw) * cosf(pitch)};
    // A wall 4 m away that faces the player.
    Room room = {{0.0f, 0.0f, 30.0f}, {0.0f, 0.0f, -1.0f}, false, 0.0f, 0.0f};
    Depth depth = {};
    float end[3], before = 0.0f;
    Run(room, eye, origin, d, 12, 22.5f, &depth, end, &before);
    Near("on a wall that faces the player the ray ends on the wall", end[2],
         30.0, 1e-3);
    Near("and the depth is at rest", depth.along, before, 1e-6);
    float hit[3];
    GameHit(room, eye, end, hit);
    Near("the hit of the game is on the ray", PointToRay(origin, d, hit), 0.0,
         1e-3);

    // A wall that is turned 40 degrees from the player.
    const float turn = 40.0f * 0.0174533f;
    Room turned = {{0.0f, 0.0f, 30.0f}, {-sinf(turn), 0.0f, -cosf(turn)},
                   false, 0.0f, 0.0f};
    Depth depth2 = {};
    Run(turned, eye, origin, d, 30, 22.5f, &depth2, end, &before);
    const float to_wall[3] = {end[0] - turned.wall_point[0],
                              end[1] - turned.wall_point[1],
                              end[2] - turned.wall_point[2]};
    Near("on a wall that is turned 40 degrees the ray ends on the wall",
         Dot(to_wall, turned.wall_normal), 0.0, 2e-3);
    Near("and the depth is at rest", depth2.along, before, 1e-4);

    // The edge of a box 2 m away, in front of the wall. The depth must come
    // to rest: it must not go from the box to the wall and back.
    for (int k = 0; k < 9; ++k) {
      Room edge = {{0.0f, 0.0f, 30.0f}, {0.0f, 0.0f, -1.0f}, true,
                   -1.2f + 0.15f * k, 15.0f};
      Depth depth3 = {};
      Run(edge, eye, origin, d, 80, 22.5f, &depth3, end, &before);
      Near("at the edge of a near box the depth is at rest", depth3.along,
           before, 0.02);
    }
  }

  // The kinds of the controls.
  Check(ControlKindOf(0x0078DC40) == kControlButton,
        "SubOptionEntryButton::UpdateMouse names a button", 1, 1);
  Check(ControlKindOf(0x0078DDD0) == kControlCheckOff &&
            ControlKindOf(0x0078DF60) == kControlCheckOn,
        "the two classes of a check box", 1, 1);
  Check(ControlKindOf(0x0078E320) == kControlScroll,
        "SubOptionEntryScroll::UpdateMouse names a scroll", 1, 1);
  Check(ControlKindOf(0) == kControlNone, "no function is no control", 1, 1);
  Check(ControlKindOf(0x00401000) == kControlOther,
        "a function with no name is a control of a kind with no name", 1, 1);

  // The buttons in a room.
  Check(ButtonAsked(1, 1, true, false, false),
        "temple_click=1: the trigger is the Action button", 1, 1);
  Check(ButtonAsked(1, 1, false, true, false),
        "temple_click=1: the grip is the Action button", 1, 1);
  Check(!ButtonAsked(1, 0, true, true, false),
        "temple_click=1: the game gets no Move button", 0, 0);
  Check(!ButtonAsked(1, 1, false, false, false),
        "temple_click=1: no control is no button", 0, 0);
  Check(ButtonAsked(0, 0, true, false, false) &&
            !ButtonAsked(0, 1, true, false, false),
        "temple_click=0: the trigger is the Move button", 1, 1);
  Check(ButtonAsked(0, 1, false, true, false) &&
            !ButtonAsked(0, 0, false, true, false),
        "temple_click=0: the grip is the Action button", 1, 1);
  Check(ButtonAsked(0, 1, true, false, true) &&
            ButtonAsked(0, 0, false, true, true),
        "temple_click=0 with the buttons changed: the trigger is the Action "
        "button",
        1, 1);
}

}  // namespace temple_checks
