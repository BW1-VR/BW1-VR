// Motion controllers in the mod. See docs/controllers.md.
//
// controllers.cpp reads the hands, works out where the aim ray points, and
// holds the fake controller and the report. controller_math.h holds the math.

#pragma once

// The fake controller, from the ini keys of docs/controllers.md,
// "4. The fake controller for desktop tests".
// The fake hand never moves continuously. It stands in one pose, and it steps
// to a second pose only in discrete steps.
struct FakeControllerSettings {
  int mode = 0;            // 0 off, 1 right hand, 2 both hands
  int right_cm = 20;       // from the head center, in the room
  int up_cm = -30;
  int forward_cm = 35;     // positive is forward
  int yaw = 0;             // degrees, positive turns left
  int pitch = -30;         // degrees, negative points down
  int roll = 0;
  bool sweep = false;      // alternate with the second pose, once a report window
  int sweep_yaw = 20;      // the second pose
  int sweep_pitch = -30;
  char script[1024] = {0};  // input@frame+frames, separated by spaces
  // Hand positions for the script word "point": right,up,forward in cm,
  // separated by spaces. Each point@frame+frames steps to the next one.
  char points[512] = {0};
  // The same for the left hand and the word "lpoint". These are not
  // mirrored: -20 is 20 cm to the left of the head.
  char left_points[512] = {0};
  // Template numbers of Data\Gestures.jty, separated by spaces.
  // Each word "trace" steps the right hand through the next template, one
  // still pose every 2 frames, on a plane gesture_cm wide in front of the
  // head.
  char gestures[128] = {0};
  int gesture_cm = 30;
};

// Draw a miracle's gesture in the air. See docs/miracles.md.
struct GestureSettings {
  // gesture_button: 0 off, 1 the second face button of the pointing hand (B
  // on the right hand, Y on the left), 2 the stick click.
  int button = 1;
  // gesture_size_cm: this much hand motion is a third of the view height to
  // the game, the size of a gesture drawn with the mouse.
  int size_cm = 30;
};

// What the draw needs from the last controllers frame.
struct GestureInput {
  bool held;              // the draw button is down and the hand may draw
  bool button_down;       // the draw button is down
  const char* why;        // why held is false
  float grip_room[3];     // the pointing grip in the room, game units
  float head[9];          // the head matrix in the room, of the same sample
  float scale;            // game units per meter
  bool have_pixel;        // the controller's pointer pixel is on the render
  float pixel_x, pixel_y;
  float render_width;     // pixels
  float render_height;
  unsigned long long frame;  // the controllers frame
};

// Move around by grabbing the world. See docs/god-view.md,
// "5.2 The buttons of the grab".
struct GrabSettings {
  bool on = true;          // grab_world
  // grab_hand: 0 off, 1 the other hand's grip, 2 also the pointing hand's
  // grip when the Hand is empty and over nothing it can hold.
  int hand = 1;
  int gain_mode = 1;       // 1 the land looks glued to the hand, 0 one to one
  int gain_pct = 100;
  int gain_max = 60;
  int vertical_pct = 100;  // the gain up and down, in percent of the gain
  int smoothing_ms = 35;   // 0 is no smoothing
  bool two_hands = true;
  bool turn = true;        // two hands turn the world about up
  bool zoom = true;        // two hands move the camera in and out
  int press_pct = 70;      // the squeeze that starts a grab
  int release_pct = 50;    // and the squeeze that ends it
  int limit = 3400;        // units from the map center, (2560, 0, 2560)
  int min_height = 6;      // units above the land
  int max_speed = 0;       // units a second, 0 is no cap
  bool probe = false;      // log the game's land drag gain
};

// One hand for the grab, from controllers.cpp.
struct GrabHand {
  bool held;       // its grip holds the world now
  bool tracked;    // false: room is the last tracked point
  float room[3];   // the grip point in the room, in game units
};

// The hands of the last frame. 0 is the other hand and 1 the pointing hand.
struct GrabInput {
  GrabHand hand[2];
  float head[3];        // the head center in the room
  float scale;          // game units per meter
  unsigned long long frame;  // the controllers frame of the samples
  double time;          // seconds
};

// CameraModeNew3 as comfort_camera.cpp reads it after the game's update, and
// as the grab asks it to write it.
struct GrabCamera {
  float position[3];  // the GCamera Zoomer destinations
  float focus[3];
  float pivot[3];     // mode+0x12C
  float yaw;          // mode+0x50, radians
  bool input_off;     // mode+0x2EC is not zero
  bool fight;         // mode+0x44 is not zero
  bool flying;        // a jump is in flight
  bool may_turn;      // the script and the mode allow a turn
};

// The direct hand. See docs/hand.md, "4. The direct hand".
struct DirectHandSettings {
  int mode = 1;              // 0 the pointer, 1 the direct hand
  // THE HAND IS THE REAL HAND. A reach gain makes a Hand that moves more than
  // the real one. So there is no reach gain by default: the Hand is exactly
  // where the controller is. Reach comes from the world, not from the arm.
  int reach_start_cm = 35;   // one to one up to this far from the anchor
  int reach_gain = 0;        // per meter, past the start. 0 is one to one
  int hand_cm = 19;          // the length of the real hand
  int scale_pct = 100;       // the Hand's size, percent of the real hand
  // The reach anchor: this far below the head center at the last recenter,
  // at about the chest.
  int reach_anchor_down_cm = 20;
  // The throw. The velocity of the Hand at the release, in percent.
  int throw_gain_pct = 150;
  // THE CREATURE BY TOUCH (docs/creature.md, "The Creature by touch").
  // creature_camera: 0 never moves the camera to the Creature, 1 moves it as
  // the game does, 2 moves it only for the mouse outside the table and the
  // God view.
  int creature_camera = 2;
  // The real speed of a slap, in meters per second. A hard slap needs 1.8
  // times this.
  float creature_slap_mps = 1.5f;
  // The body of the Creature reaches this far past its 2D radius, in cm of
  // the room.
  int creature_touch_cm = 5;
  // True keeps the game's wait of 1 s in the Creature state before a slap.
  bool creature_slap_wait = false;
};

// What the controller hints need to know. See docs/controller-hints.md.
struct ControllerHintState {
  bool owns;            // the controller owns the pointer now
  bool left_handed;     // the left hand points
  bool swap_buttons;    // the trigger is the right button, the grip the left
  int gesture_button;   // 0 off, 1 the second face button, 2 the stick click
  bool world_grab;      // the grip of the other hand grabs the world
  int ring;             // 0 no ring, 1 a button opens it, 2 only the stick
  int layout;           // button_layout: 1 the menu button and Y, 0 Y and X
  char profile[64];     // of the pointing hand, "fake" for the fake controller
};
void ControllersHintState(ControllerHintState* out);

// THE LAYOUT OF THE BUTTONS of the hand that does not point (button_layout,
// docs/controllers.md, "The layout of the other hand"). 1: the menu button of
// the left controller opens the game menu, the second face button opens the
// ring, the first one has no job, and the stick does not drive the ring. 0:
// the second face button opens the game menu, the first one opens the ring,
// and the stick can drive the ring.
int ControllersButtonLayout();
// The test key of button_layout (Ctrl and the Menu key, with test_keys=1),
// and the word "layoutkey" of the scripted controller.
void ControllersButtonLayoutToggle(const char* by);

// The overlay renderer: each hand's grip pose in the world, one to one. See
// docs/hand-model.md, "Overlay renderer".
struct OverlayHand {
  bool valid;          // active, with a grip pose that is tracked
  float position[3];   // the grip point in the world
  float rotation[9];   // the grip axes in the world: L * M_grip
  float trigger;       // 0 to 1
  float squeeze;       // 0 to 1
};

struct OverlayHands {
  OverlayHand hand[2];     // 0 the left hand, 1 the right hand
  int pointing;            // the hand that holds the pointer
  float units_per_meter;   // world units in a meter of the room, at the scale now
  unsigned long long frame;  // the controllers frame
};
bool ControllersOverlayHands(OverlayHands* out);

// The teleport: the pointing hand's A and B buttons, and its aim. See
// teleport.cpp and docs/god-view.md, "The teleport".
struct TeleportInput {
  bool hand;              // the pointing hand is active
  bool a_down;            // the A button is down
  bool a_pressed;         // A went down on this frame, and the hand did not wake on it
  bool b_down;            // the second face button is down
  bool b_pressed;
  bool usable;            // the hand may act now: not in a menu, and the game is in front
  bool grab_held;         // a grip holds the world
  bool gesture_held;      // a gesture is being drawn
  bool have_aim;          // the aim pose is tracked and mapped
  float origin_m[3];      // the aim point in the room, meters
  float direction[3];     // the aim direction in the room, a unit vector
  float base[3];          // world = base + map * (a point of the room in meters)
  float map[9];
  float eye[3];           // the eye in the world
  float units_per_meter;  // world units in a meter of the room
  unsigned long long frame;  // the controllers frame
};
bool ControllersTeleportInput(TeleportInput* out);

// The ray of the pointing hand while a dialog box is up and the controller
// owns the pointer. The two ends are in the world. False when none is drawn.
bool ControllersMenuRay(float from[3], float to[3], float* units);

// The ring (radial_menu.cpp, docs/controllers.md, "The ring"): the hand that
// does not point, from the last controllers frame. The poses are in the LOCAL
// space of OpenXR, in meters.
struct RingHand {
  bool active;        // the hand is bound and has a pose
  bool usable;        // the controllers may act now
  bool left;          // it is the left hand
  bool dialog;        // a dialog box is up
  bool grab_held;     // a grip holds the world
  bool button;        // the face button of the ring, by button_layout: Y or X,
                      // and B or A when the left hand points
  bool face_buttons;  // false for a controller with no face buttons
  bool stick_click;
  bool stick_live;    // false for a Vive pad that is not clicked
  float trigger;      // 0 to 1
  float stick[2];     // x right, y forward
  float position[3];  // the grip point
  float head[3];      // the head center
};
bool ControllersRingHand(RingHand* out);
