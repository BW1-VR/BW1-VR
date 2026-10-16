// The teleport with a visible arc. See teleport.cpp and docs/god-view.md,
// "The teleport".

#pragma once

#include <stdint.h>

#include "overlay_math.h"

struct TeleportSettings {
  bool on = true;     // teleport: 1 is the arc, 0 is the A jump of the game
  int reach_m = 10;   // teleport_reach_m: the flat range of a 45 degree aim
  int ring_cm = 11;   // teleport_ring_cm: the outer radius of the ring
  // teleport_ring_mode: 1 is a ring of one size in the world (the default),
  // 0 is a ring of one size in the room (ring_cm).
  int ring_mode = 1;
  int ring_units = 11;      // teleport_ring_units: the world size of mode 1
  int ring_min_cm = 4;      // teleport_ring_min_cm: the smallest size, mode 1
  int ring_max_cm = 40;     // teleport_ring_max_cm: the largest size, mode 1
  // teleport_switch: which switch of the game the press of A keeps to. 1 is
  // the switch for the drag of the land, as the walk with the stick. 0 is the
  // switch of the game's double click jump.
  int move_switch = 1;
  // test_keys: Ctrl and Insert change the mode. Ctrl, Shift and the comma key
  // change move_switch.
  bool test_keys = false;
};

void TeleportSetOptions(const TeleportSettings& settings);

// True when A makes the teleport and not the game's jump: teleport=1, and the
// head is on the camera.
bool TeleportOwnsA();

// True while the player aims the teleport (A is down).
bool TeleportAiming();

// The test key of teleport_switch (Ctrl, Shift and the comma key, with
// test_keys=1), and the word "teleportkey" of the scripted controller: the
// switch changes between 1 and 0. `by` names the cause in the log.
void TeleportSwitchToggle(const char* by);

// True while a press of A can move the player, for the hint card. With
// teleport=1: the last TeleportFrame found no reason to refuse the press.
// With teleport=0: the game's jump switch is on.
bool TeleportPossible();

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
