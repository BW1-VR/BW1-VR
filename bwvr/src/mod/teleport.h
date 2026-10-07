// The teleport with a visible arc. See teleport.cpp and docs/god-view.md,
// "The teleport".

#pragma once

#include <stdint.h>

#include "overlay_math.h"

struct TeleportSettings {
  bool on = true;     // teleport: 1 is the arc, 0 is the old A jump
  int reach_m = 10;   // teleport_reach_m: the flat range of a 45 degree aim
  int ring_cm = 11;   // teleport_ring_cm: the outer radius of the ring
};

void TeleportSetOptions(const TeleportSettings& settings);

// True when A makes the teleport and not the old jump: teleport=1, and the
// head is on the camera.
bool TeleportOwnsA();

// True while the player aims the teleport (A is down).
bool TeleportAiming();

// The land height at a world point for the teleport. The sea and the places
// off the map are 0.
float TeleportGround(float x, float z);

// Once a frame, after ControllersFrame and HandDirectFrame.
void TeleportFrame();

// Once a report window.
void TeleportReport();

// comfort_camera.cpp, before the game's update of the camera mode: the mouse
// double click in the God view. True when it took the click.
bool TeleportMouseDoubleClick(void* mode);

// The camera help event of a jump: 0x305 to the land, 0x306 to an object
// under the Hand, as the game's own jump sends them.
int TeleportJumpEvent();

// What the overlay draws on this frame: the arc, and the ring of the target.
struct TeleportDraw {
  bool active;            // the player aims now
  bool valid;             // the target is valid (cyan), or not (red)
  int count;              // the points of the arc, 0 for none
  float points[overlay_math::kArcMaxPoints][3];  // in the world
  bool ring;              // the target is known
  float target[3];        // in the world
  float units_per_meter;  // world units in a meter of the room
  float ring_cm;          // the outer radius of the ring
};
bool TeleportDrawState(TeleportDraw* out);

// The overlay counts what it drew, for the report.
void TeleportCountDraw(int eye_passes, int triangles);
