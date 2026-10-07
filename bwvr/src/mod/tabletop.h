// The settings of the tabletop mode and of the God view, from bwvr.ini, and
// what the other parts of the mod ask the table. See tabletop.cpp,
// docs/tabletop.md and docs/god-view.md.

#pragma once

#include <stdint.h>

#include "table_math.h"

struct TabletopSettings {
  int diameter_cm = 150;      // table_diameter_cm: the table in the room
  int forward_cm = 80;        // table_forward_cm: its center, ahead of you
  int down_cm = 40;           // table_down_cm: its top, below your head
  int units_per_meter = 0;    // table_units_per_meter: 0 fits the land
  int margin_pct = 10;        // table_margin_pct: room at the edge
  int center_x = -1;          // table_center_x: -1 is the middle of the land
  int center_z = -1;          // table_center_z
  bool full_textures = true;  // table_textures: full textures on all land
  bool sweep = false;         // table_sweep: a test, off and on each window
  bool key = false;           // table_key_color is set
  uint32_t key_color = 0;     // 0xAARRGGBB
  int hand_cm = 10;           // table_hand_cm: the Hand's length on the table
  // THE GOD VIEW (play_mode=4). The same camera as the table, with the
  // ground at the waist and no edge. See docs/god-view.md.
  bool god = false;
  int god_down_cm = 60;          // god_ground_down_cm: the ground, below you
  int god_units_per_meter = 100; // god_units_per_meter: the scale at start
  int god_zoom_pct = 50;         // god_zoom_step_pct: one zoom step
  int god_step_cm = 5;           // god_ground_step_cm: one ground step
  int god_most_units = 4000;     // god_far_units_per_meter: the farthest zoom
  // Where the player goes when the game puts its own camera at a place
  // (docs/god-view.md, section 15).
  int god_spawn = 1;             // god_spawn: 1 follows the game, 0 is before
  int god_spawn_ahead_cm = 50;   // god_spawn_ahead_cm: the look point, ahead
  bool test_keys = false;        // test_keys: Ctrl and Numpad 0 change it
};

void TabletopSetOptions(const TabletopSettings& settings);
void TabletopInstall();

// True while the table owns the view: the last main camera call was the
// table's. Valid outside GGame::ProcessGraphicsEngine too. The God view is a
// table too, so this is true for it.
bool TabletopOwnsView();

// The room of the table in this frame, inside GGame::ProcessGraphicsEngine:
// the recenter pose in the world (P0), the table basis (Lt), the table
// center in the world (C) and the gain s / 7.5 on a room point. False when
// the table does not own this frame.
bool TabletopRoom(float origin[3], float basis[9], float center[3],
                  float* gain);

// The table scale while the table owns the view, and 0 otherwise.
float TabletopViewScale();

// The Hand's size on the table, and 0 when the table does not own the view.
// In the God view it is the size of the real hand at the scale now.
float TabletopHandSize();

// For the grab of the table (T4): the table now, the table center in the
// room in meters, the limits of the scale and a serial that goes up each
// time the table is built again or a step moves it. False when the table
// does not own the view.
bool TabletopPose(table_math::TablePose* pose, float table_room[3],
                  float* lowest_scale, float* highest_scale,
                  uint64_t* serial);

// The grab puts the table here. The center stays on the map. While the
// game's switch for the drag of the land is off, nothing moves.
void TabletopMove(const table_math::TablePose& pose);

// The God view: cut the height of a one hand grab (pose->center[1]) to the
// ground limits of docs/god-view.md, section 3. False when the God view does
// not own the view. cut is true when a limit held the height back.
bool TabletopGodLimitHeight(table_math::TablePose* pose, bool* cut);

// The game's switches for the turn and the zoom of a grab: each one that is
// off sets its flag to false.
void TabletopGrabAllows(bool* turn, bool* zoom);

// Where the ears go on the table, from the render head. False when the table
// does not own the view, and then the ears are the head.
bool TabletopEars(const float head[3], float out[3]);

// THE GOD VIEW. True while the God view owns the view.
bool TabletopGodOwnsView();

// One step of the player: zoom 1 zooms in (the world gets larger) and -1
// zooms out. ground 1 raises the ground and -1 lowers it. The step is used
// once, at the next main camera call. False when the God view does not own
// the view, and then nothing is kept.
bool TabletopGodStep(int zoom, int ground);

// True while the table or the God view is on, also while it stands aside.
bool TabletopIsOn();

// THE TELEPORT (docs/god-view.md, "The teleport"). target is a land point of
// the world, and only its x and z are read. The move is made once, at the next
// main camera call. TabletopGodTeleport puts the head over it and keeps the
// land under the head at its height in the room. TabletopTableTeleport puts it
// at the table center. False when that view does not own the frame.
bool TabletopGodTeleport(const float target[3]);
bool TabletopTableTeleport(const float target[3]);
uint64_t TabletopTakeTeleportMoves();

// A snap turn of the God view: direction 1 turns the view right and -1 left,
// by degrees, about the head. The same rule: once, at the next main call.
bool TabletopGodTurn(int direction, float degrees);

// The game's own camera over the player's place, for comfort_camera.cpp to
// write after the game's update of the player's camera mode, once for each
// version. The version goes up each time the player moves the view. False
// when the God view does not own the view.
bool TabletopGodGameCamera(float position[3], float focus[3],
                           uint64_t* version);

// THE GAME PUT ITS OWN CAMERA AT A NEW PLACE (docs/god-view.md, section 15).
// comfort_camera.cpp calls this when the game camera is at rest after a move
// that the player did not make. The God view goes to that place at its next
// main camera call, as one cut.
void TabletopGodGameMoved(const char* cause, const float position[3],
                          const float focus[3]);

// The test key of god_spawn (Ctrl and Numpad 0, with test_keys=1): the rule
// changes between 1 and 0, and the view goes to the game camera again by the
// rule that it has now.
void TabletopGodSpawnToggle();
