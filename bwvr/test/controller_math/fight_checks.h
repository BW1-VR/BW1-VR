// A FIGHT OF CREATURES (docs/creature.md, "A fight of Creatures").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/fight_math.h"

namespace fight_checks {

inline float Length(const float v[3]) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

inline void FightChecks() {
  using namespace fight_math;
  printf("\nA fight of Creatures\n");

  // A body as in the fight of the log: a radius of 1.2 units and a height of
  // 4.2 units, at 19.8 units a meter.
  const float base[3] = {100.0f, 0.0f, 200.0f};
  const float height = 4.2f, radius = 1.2f;
  const float camera[3] = {100.0f, 10.0f, 190.0f};
  float origin[3], direction[3];

  // THE RAY OF A PRESS (fight_grip=1).
  {
    // The Hand 3 units to the side of the axis, at the middle of the body.
    const float hand[3] = {103.0f, 2.0f, 200.0f};
    const bool ok = BodyRay(base, height, radius, hand, camera, origin,
                            direction);
    Check(ok, "a Hand at the side of the body gives a ray", ok ? 1 : 0, 1);
    Near("the ray starts at the Hand's point, x", origin[0], 103.0, 1e-4);
    Near("and y", origin[1], 2.0, 1e-4);
    Near("it goes straight to the axis, x", direction[0], -1.0, 1e-5);
    Near("with no part up or down", direction[1], 0.0, 1e-5);
    Near("the direction has the length 1, as the game gives",
         Length(direction), 1.0, 1e-5);
    // The ray meets the side of the body at the height of the Hand. That
    // place is the place of the body that is nearest to the Hand.
    const float hit_x = origin[0] + direction[0] * (3.0f - radius);
    Near("it meets the body 1.8 units from the Hand, at the side", hit_x,
         101.2, 1e-4);
    Near("the Hand is at the middle part of the height (0.33 to 0.66)",
         BodyLevel(base, height, hand), 2.0 / 4.2, 1e-4);
  }
  {
    // The Hand low at the legs: the game makes a low attack from that place.
    const float hand[3] = {100.0f, 0.5f, 203.0f};
    BodyRay(base, height, radius, hand, camera, origin, direction);
    Near("a Hand at the legs: the ray stays at the height of the Hand",
         origin[1], 0.5, 1e-4);
    Check(BodyLevel(base, height, hand) < 0.33f,
          "and that height is the low part of the body",
          BodyLevel(base, height, hand), 0.33);
  }
  {
    // The Hand over the head: the ray goes down to the top of the axis.
    const float hand[3] = {100.5f, 7.0f, 200.0f};
    BodyRay(base, height, radius, hand, camera, origin, direction);
    Check(direction[1] < -0.9f,
          "a Hand over the head: the ray goes down to the top of the head",
          direction[1], -1.0);
    const float top[3] = {100.0f - origin[0], 4.2f - origin[1],
                          200.0f - origin[2]};
    const float to_top = Length(top);
    Near("and it starts at the Hand's point, past the radius of the body",
         to_top, sqrtf(0.25f + 2.8f * 2.8f), 1e-4);
  }
  {
    // The Hand's point in the body: the ray starts two radii from the axis,
    // on the side of the Hand, so that it starts outside the body.
    const float hand[3] = {100.3f, 3.0f, 200.0f};
    BodyRay(base, height, radius, hand, camera, origin, direction);
    Near("a Hand in the body: the ray starts two radii from the axis",
         origin[0], 100.0 + 2.0 * 1.2, 1e-4);
    Near("on the side of the Hand, and it goes to the axis", direction[0],
         -1.0, 1e-5);
  }
  {
    // The Hand's point on the axis: the ray comes from the side of the
    // camera.
    const float hand[3] = {100.0f, 2.0f, 200.0f};
    const bool ok = BodyRay(base, height, radius, hand, camera, origin,
                            direction);
    Check(ok, "a Hand on the axis gives a ray", ok ? 1 : 0, 1);
    Near("it goes across the body, away from the camera", direction[2], 1.0,
         1e-5);
    Near("from two radii in front of the axis", origin[2], 200.0 - 2.4,
         1e-4);
  }
  {
    const float hand[3] = {103.0f, 2.0f, 200.0f};
    const bool ok = BodyRay(base, 0.0f, radius, hand, camera, origin,
                            direction);
    Check(!ok, "a body with no height gives no ray: the game's ray stays",
          ok ? 1 : 0, 0);
  }

  // THE MARKS (fight_mark_depth=1).
  {
    // The camera at 0, the place of the mark 10 units away. The game draws
    // the mark at 5 units with the size 0.2.
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float middle[3] = {0.0f, 0.0f, 5.0f};
    float place[3], size = 0.0f;
    const bool ok = MarkPlace(eye, middle, 0.2f, place, &size);
    Check(ok, "a mark of the game gives a place", ok ? 1 : 0, 1);
    Near("the mark goes back to its place, less one size of the mark (0.4)",
         place[2], 9.6, 1e-4);
    Near("its size is larger", size, 0.4 * 9.6 / 10.0, 1e-5);
    Near("so its size on the screen is the size that the game gives",
         size / place[2], 0.2 / 5.0, 1e-6);
  }
  {
    // A large mark: it never comes nearer to the camera than the game's
    // middle.
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float middle[3] = {0.0f, 0.0f, 5.0f};
    float place[3], size = 0.0f;
    MarkPlace(eye, middle, 4.0f, place, &size);
    Near("a mark of size 4 at 10 units stays at the middle", place[2], 5.0,
         1e-4);
    Near("with the size of the game", size, 4.0, 1e-4);
  }
  {
    // A camera and a mark at a slant: the place stays on the line from the
    // camera to the mark.
    const float eye[3] = {1.0f, 2.0f, 3.0f};
    const float mark[3] = {11.0f, -3.0f, 23.0f};
    const float middle[3] = {6.0f, -0.5f, 13.0f};
    float place[3], size = 0.0f;
    MarkPlace(eye, middle, 0.25f, place, &size);
    const float a[3] = {mark[0] - eye[0], mark[1] - eye[1], mark[2] - eye[2]};
    const float b[3] = {place[0] - eye[0], place[1] - eye[1],
                        place[2] - eye[2]};
    const float cross[3] = {a[1] * b[2] - a[2] * b[1],
                            a[2] * b[0] - a[0] * b[2],
                            a[0] * b[1] - a[1] * b[0]};
    Near("a slant: the place is on the line from the camera to the mark",
         Length(cross), 0.0, 1e-3);
    const float gap[3] = {mark[0] - place[0], mark[1] - place[1],
                          mark[2] - place[2]};
    Near("one size of the mark (0.5) in front of the mark", Length(gap), 0.5,
         1e-4);
    Near("its size on the screen is the size that the game gives",
         size / Length(b), 0.25 / (0.5 * Length(a)), 1e-6);
  }
  {
    const float eye[3] = {4.0f, 5.0f, 6.0f};
    float place[3], size = 0.0f;
    const bool ok = MarkPlace(eye, eye, 0.2f, place, &size);
    Check(!ok, "a mark at the camera: the game's place stays", ok ? 1 : 0,
          0);
  }
}

}  // namespace fight_checks
