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
  // The zoom of the stick (docs/god-view.md, "The zoom"): 0 is one step for
  // each push, and 1 is the smooth zoom, which changes the scale while the
  // stick is held.
  int god_zoom_smooth = 0;       // god_zoom_smooth: 0 the steps, 1 smooth
  int god_zoom_smooth_ms = 500;  // god_zoom_smooth_ms: one step, a full push
  int god_zoom_test_toggle = 0;  // god_zoom_test_toggle: a test, see below
  // The direction of the stick: 0 zooms out with the stick forward and in
  // with the stick back, and 1 zooms in with the stick forward.
  int god_zoom_forward_in = 0;   // god_zoom_forward_in: 0 out, 1 in
  // The point that a zoom keeps in the room (docs/god-view.md, "The zoom"):
  // 0 is the land point under the head, 1 is the land point that the head
  // looks at, and 2 is the Hand's point. With 1, the point is at most
  // god_zoom_about_far_cm from the head.
  int god_zoom_about = 1;        // god_zoom_about: 0 head, 1 look, 2 Hand
  int god_zoom_about_far_cm = 300;  // god_zoom_about_far_cm: the limit of 1
  int god_zoom_about_test = 0;   // god_zoom_about_test: a test, see below
  int god_step_cm = 5;           // god_ground_step_cm: one ground step
  int god_most_units = 4000;     // god_far_units_per_meter: the farthest zoom
  // Where the player goes when the game puts its own camera at a place
  // (docs/god-view.md, "The place at a camera of the game").
  int god_spawn = 1;             // god_spawn: 1 follows the game, 0 does not
  int god_spawn_ahead_cm = 50;   // god_spawn_ahead_cm: the look point, ahead
  // Where the game camera stands (docs/god-view.md, "The game camera"): 1
  // over the land point under the head, 0 over the recenter place.
  int god_camera_at = 1;         // god_camera_at: the place of the game camera
  // The walk with the stick of the hand that does not point (docs/god-view.md,
  // "The walk with the stick"): 0 is off, 1 moves along the direction of the
  // head, and 2 along the direction in which that controller points.
  int move_stick = 1;            // move_stick: 0 off, 1 head, 2 controller
  int move_stick_speed_cm = 150; // move_stick_speed_cm: a full push, a second
  // god_display_time: 1 the time of a frame for the walk and the smooth zoom
  // is the time between the display times of two frames of the headset, 0
  // the time of the CPU between two main camera calls.
  int god_display_time = 1;
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
// ground limits of docs/god-view.md, "The ground height". False when the God view does
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

// THE SMOOTH ZOOM (god_zoom_smooth=1). TabletopGodZoomSmooth is true while
// the stick zooms that way. TabletopGodZoomHold is the request of the stick
// for the next frame: push is the share of the top rate, above 0 to zoom in
// and below 0 to zoom out, at most 1. The next main camera call uses it
// once, for the time of that frame. The stick asks again before each frame
// while it is held, and a frame with no request changes nothing. False when
// the view does not zoom: the God view does not own the view, the steps are
// chosen, or the game's switch for the zoom is off. The push must then come
// back before it asks again.
bool TabletopGodZoomSmooth();
bool TabletopGodZoomHold(float push);
// The direction of the stick (god_zoom_forward_in): true when the stick
// forward zooms in, false when it zooms out.
bool TabletopGodZoomForwardIn();

// The test key of god_zoom_smooth (Ctrl, Shift and Insert, with
// test_keys=1): the zoom of the stick changes between the steps and the
// smooth zoom. The test god_zoom_test_toggle=N does the same at frame N of
// the controllers, with no key press, for a desktop run.
void TabletopGodZoomSmoothToggle();

// The test key of god_zoom_about (Ctrl, Shift and Delete, with test_keys=1):
// the point of a zoom goes from the land that the head looks at (1) to the
// land under the head (0), to the Hand's point (2) and back. The test
// god_zoom_about_test=N does the same at frame N of the controllers, with no
// key press, for a desktop run.
void TabletopGodZoomAboutCycle();

// THE WALK WITH THE STICK (move_stick). TabletopGodMoveStick gives the rule
// of now: 0 off, 1 the direction of the head, 2 the direction of the
// controller. It is 0 when the God view is not the play mode.
//
// TabletopGodWalk is the request of the stick for the next frame. push is the
// stick, x to the right and y forward, and its length is the share of the top
// speed (walk_math::Push). aim is the forward direction of the controller on
// the land in the room (x and z), or null when that controller has none. The
// next main camera call uses the request once, for the time of that frame.
// The stick asks again before each frame while it is pushed, and a frame with
// no request moves nothing. False when the player does not move: the walk is
// off, the God view does not own the view, or the game's switch for the drag
// of the land is off. The push must then come back before it asks again.
int TabletopGodMoveStick();
bool TabletopGodWalk(const float push[2], const float* aim);

// THE SWITCH OF A MOVE OVER THE LAND. True while the game's switch for the
// drag of the land is on (bit 8 of CameraHelp::EnabledFeatures). The grab,
// the walk with the stick and the teleport keep to it, and so do the lines of
// the hint card for the walk and the teleport.
bool TabletopMoveAllowed();

// The test key of move_stick (Shift and the Menu key, with test_keys=1): the
// rule goes from 1 to 2 to 0 and back to 1.
void TabletopGodMoveStickCycle();

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

// True when the view only turned with snap turns since the game camera and
// the view were last kept as a pair, and turned gives by how much (radians,
// to the right above 0). GodAfter then turns the game camera about its own
// focus (god_turn_keeps_focus).
bool TabletopGodTurnedOnly(float* turned);

// THE GAME PUT ITS OWN CAMERA AT A NEW PLACE (docs/god-view.md, "The place at
// a camera of the game"). comfort_camera.cpp calls this when the game camera is at rest after a move
// that the player did not make. The God view goes to that place at its next
// main camera call, as one cut.
void TabletopGodGameMoved(const char* cause, const float position[3],
                          const float focus[3]);

// The test key of god_spawn (Ctrl and Numpad 0, with test_keys=1): the rule
// changes between 1 and 0, and the view goes to the game camera again by the
// new rule.
void TabletopGodSpawnToggle();

// The test key of god_camera_at (Ctrl and Numpad 1, with test_keys=1): the
// game camera stands over the land point under the head or over the
// recenter place.
void TabletopGodCameraAtToggle();
