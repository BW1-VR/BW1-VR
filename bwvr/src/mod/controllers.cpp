// Motion controllers: read the hands, work out where the aim ray points, and
// act as a second mouse. See docs/controllers.md.
//
// THE MOUSE. The trigger is the left button and the grip is the right button.
// With the direct hand the grip holds like a real hand: close it on an object
// to take it, keep it closed to carry, open it to let go or throw. The mod
// sends the game's second click for the opening (GripStep in
// controller_math.h, docs/grab.md "The grip holds").
// Every button event of the game goes through LHMouse::SetButtons, and only the
// window procedure calls it. So this file hooks it, and it keeps one held mask
// for the real mouse and one for the controller. A call reaches the game only
// when it changes the union of the two. The controller's own events come from a
// pump that runs once a frame in AnyWindowMessages, where the real ones arrive.
// The pump compares the buttons it wants with the buttons it has sent, so a
// second run in one frame does nothing.
//
// THE SOURCE. Real hands come from XrScreen, one sync a frame. The fake
// controller builds the same HandInput values, so everything after it is the
// same path, exactly as the fake head does (head_pose.cpp).
//
// THE TRANSFORM. The head reaches the game in two steps: head_pose.cpp puts it
// in the room, and head_camera.cpp puts the room in the world through the
// basis L. A hand takes the same two steps, or a controller at the head would
// not show at the head. The math is in controller_math.h, with no game state,
// so test/controller_math can check it.
//
// THE PIXEL. The aim ray starts at the hand, and the game casts the Hand's ray
// from the eye. The two rays meet at one point only, so the pixel needs a
// target distance s. The distance s is the distance from the eye to the game
// camera's focus, which is the land in the middle of the game's view (see
// docs/controllers.md, "3.2 Parallax and the projection depth").
//
// THE DIRECT HAND. The Hand's point is the grip pose in the world, moved
// out by the reach curve (controller_math.h, ReachDistance) along the line
// from a fixed anchor in the room at about the chest. The eye, the head
// matrix and the head center all come from one head sample, so a head that
// moves does not move the Hand. The pixel is that point seen from the eye,
// so the render pick and the land lookup see what is under the Hand.
// hand_direct.cpp puts the Hand on the point. The land drag casts its rays
// from the game camera to the point (ControllerCameraRay).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/bw_addresses.h"
#include "../vr/xr_screen.h"
#include "MinHook.h"
#include "aim_math.h"
#include "aim_smooth.h"
#include "controller_math.h"
#include "controllers.h"
#include "creature_command.h"
#include "debug_menu.h"
#include "far_grab.h"
#include "frame_profile.h"
#include "haptics.h"
#include "hand_exact_math.h"
#include "miracle.h"
#include "note_depth.h"
#include "radial_menu.h"
#include "script_frame_math.h"
#include "temple.h"
#include "temple_math.h"
#include "text_click.h"
#include "text_click_math.h"
#include "walk_math.h"
#include "zoom_math.h"

extern void ModLog(const char* format, ...);

// head_pose.cpp
float HeadPoseScale();
bool HeadPoseCurrent(float matrix_out[9], float translation_out[3]);
bool HeadPoseToRoom(const float position[3], const float orientation[4],
                    float rotation_out[9], float translation_out[3]);
bool HeadPoseHeadInRoom(float translation_out[3]);
// head_camera.cpp
bool HeadCameraStageBasis(float basis_out[9]);
bool HeadCameraGameCamera(float position[3], float focus[3]);
// comfort_camera.cpp
bool ComfortCameraRequestTurn(int direction);
const void* ComfortCameraLastMode();
// stereo.cpp
bool StereoIdle();
// stereo.cpp: the interface shift and the eye separation of the last frame.
bool StereoUiDepthNow(float* shift_pixels, float* separation);
// hand_direct.cpp: the nearest holdable object of the last frame, and the
// CHand far limit.
bool HandDirectSeekPoint(float out[3]);
bool HandDirectSeekCreaturePoint(float out[3]);
// The body bone of the nearest Creature, for the word "touchc".
bool HandDirectTouchCreaturePoint(float out[3]);
float HandDirectFarLimit();
// hand_direct.cpp: the nearest far target (more than 0.5 m of the room from
// the Hand, and in the range of far_grab_max_m from the head), for the word
// "aimfar", and the wider pick that finds it.
bool HandDirectSeekFarPoint(float out[3]);
void HandDirectWidenFar(int frames, float meters);
// hand_direct.cpp: whether the pointing grip may grab the world.
bool HandDirectClaimFree(const char** why);
// hand_direct.cpp: the Hand facts of the grip, and the latch of the throw at
// the opening of the grip. See docs/grab.md, "The grip holds".
bool HandDirectGripFacts(bool* holding, bool* settled, bool* spring,
                         bool* take);
void HandDirectGripOpened();
// hand_direct.cpp: the facts of the press of the game (the game has an object
// in its Hand, the object is in the physics of the game, and the age of the
// press on the clock of the game), and the grip is fully open now.
bool HandDirectGripPress(bool* game_holds, bool* moving,
                         uint32_t* press_age_ms);
void HandDirectGripFullyOpen();
// hand_direct.cpp: a bulk take still runs with the grip open, so the next
// interface step must end it. See docs/grab.md, "The bulk take".
void HandDirectEndTake();
// hand_direct.cpp: the take of the mod from a forest runs. See docs/grab.md,
// "Many trees from a forest".
bool HandDirectModTake();
// hand_direct.cpp: the release at once is on now (release_at_once, and the
// A/B key of test_keys=1). The fast opening of the grip follows it.
bool HandDirectReleaseAtOnce();
// hand_direct.cpp: on the table, offer the objects in a wider ball around the
// Hand to the pick for a few frames, for the word "seek".
void HandDirectWidenPick(int frames);
// eye_capture.cpp: a picture of both eyes at a stereo frame, for the word
// "shot".
void EyeCaptureArmNow(uint64_t stereo_frame);
// exit_test.cpp: the game ends by itself, for the word "exit".
void ExitTestNow(uint64_t frame);
// game_start.cpp: the clock of the log.
double SecondsSinceProcessStart();
// head_pose.cpp: the steps of the invented head, for the grab tests.
void HeadPoseFakeLeanStep(bool on);
void HeadPoseFakeLookStep(bool on);
// world_grab.cpp: the player's camera mode ran on a recent frame, with input.
bool WorldGrabCameraReady();
// mod_main.cpp: the predicted display time of the open frame of the headset,
// in seconds, or 0 with no session.
double ModDisplaySeconds();
// tabletop.cpp: the room of the table in this frame. See docs/tabletop.md,
// "The hand on the table".
// hand_exact.cpp: the exact Hand is on, so the Hand's point is the real
// index tip (docs/hand.md, "7. The exact Hand").
bool HandExactOn();
// tabletop.cpp: the Hand's size while the table or the God view owns the
// view, and 0 otherwise.
float TabletopHandSize();
bool TabletopRoom(float origin[3], float basis[9], float center[3],
                  float* gain);
// teleport.cpp: A makes the teleport.
bool TeleportOwnsA();
// teleport.cpp: the player aims the teleport now (A is down).
bool TeleportAiming();
// far_grab.cpp: the grab from a distance (docs/grab.md, "The grab from a
// distance"). The pump gives it the grip, and it says when to hold the grip
// back from the grip machine.
bool FarGrabPumpGrip(bool grip, bool ok, bool hand);
// This file, below: a pulse on the pointing hand.
void ControllersGesturePulse(float amplitude, float seconds);
bool FarGrabPressing();
// tabletop.cpp: the God view, and one zoom step or one ground step of it.
// The smooth zoom: whether the stick zooms that way, and its request for the
// next frame. See docs/god-view.md.
bool TabletopGodOwnsView();
bool TabletopGodStep(int zoom, int ground);
bool TabletopGodZoomSmooth();
bool TabletopGodZoomHold(float push);
bool TabletopGodZoomForwardIn();
// tabletop.cpp: the walk with the stick of the hand that does not point. Its
// rule, its request for the next frame, and its test key. And the game's
// switch for the drag of the land, which the walk keeps to.
int TabletopGodMoveStick();
bool TabletopGodWalk(const float push[2], const float* aim);
void TabletopGodMoveStickCycle();
bool TabletopMoveAllowed();
// teleport.cpp and hint_card.cpp: the test keys of teleport_switch and of
// prompt_moves, for the words "teleportkey" and "promptkey" of the script.
void TeleportSwitchToggle(const char* by);
void HintCardMovesToggle(const char* by);

void ControllersPump();
bool ControllersFarAllowed(const char** why);
// cutscene_window.cpp: this frame draws on the screen of the cut scene window.
bool CutsceneWindowFlat();
// intro_vr.cpp: true while the Lionhead intro draws in stereo.
bool IntroBlocksButtons();
// hand_direct.cpp: the CHand of this frame.
void* HandDirectHand(bool* active);

namespace {

bool g_on = true;
// The left controller is the Hand. UpdateHandSide sets it once a frame from
// the key left_hand and the game's own option.
bool g_left_handed = false;
int g_left_hand_key = -1;  // -1 follows the game, 0 right, 1 left
bool g_haptics = true;
bool g_swap_buttons = false;
bwvr::XrScreen* g_screen = nullptr;
DirectHandSettings g_direct;
// Set by pointer_map.cpp once a frame: the owner rule gave the controller the
// pointer. Only then does the controller own the Hand.
bool g_pointer_owned = false;

// --- the fake controller -----------------------------------------------------

const int kMaxScript = 192;

enum FakeInput {
  kTrigger,
  kGrip,
  kA,
  kB,
  kX,
  kY,
  kMenu,
  kStickClick,
  kStickLeft,
  kStickRight,
  kStickUp,
  kStickDown,
  kPose2,
  kPoint,
  kSeek,
  kSeekCreature,
  kNudge,
  kSlap,
  // The left hand's own points and grip, and steps of the head.
  kLPoint,
  kLGrip,
  kLean,
  kLook,
  // Steps through a gesture template.
  kTrace,
  // A picture of both eyes, 3 frames later. It moves nothing.
  kShot,
  // The Creature by touch: the hand onto the Creature's body bone.
  kTouchCreature,
  // The exact Hand: the right hand turns to the next yaw, pitch and roll of
  // fake_controller_rots.
  kRot,
  // The grab from a distance: the aim of the right hand turns to the nearest
  // far target (a holdable object, or with seek_bulk=1 a bulk source), and
  // the right hand steps 15 cm toward the head, or back.
  kAimFar,
  kPull,
  // The Creature commands: the aim of the right hand turns to the nearest
  // object that a command can act on ("aimcmd"), and it turns by the next
  // yaw and pitch of fake_controller_turns ("aimturn").
  kAimCommand,
  kAimTurn,
  // The game window behind: the headset session has no focus of the runtime
  // while "nofocus" is held, and "deactivate" tells the game that a
  // different program is now the active one.
  kNoFocus,
  kDeactivate,
  // The stick and the trigger of the left hand: the walk with the stick, and
  // the ring with button_layout=0.
  kLStickLeft,
  kLStickRight,
  kLStickUp,
  kLStickDown,
  kLTrigger,
  // The Temple: the aim of the right hand turns to the next target of
  // temple_test_targets (temple.cpp).
  kAimTemple,
  // The grip of the right hand reads 0.59 instead of 1.0 for the frames of the
  // word, while the grip word holds: a real hand that jerks away loses part of
  // its squeeze for a frame or two (grip_press_guard).
  kGripDip,
  // The mod asks the game to end by itself, on its usual exit path
  // (exit_test.cpp).
  kExit,
  // The frame cost measure: the frames of the word are one scene
  // (frame_profile.cpp). It moves nothing.
  kBench,
  // The menu button of the left hand.
  kLMenu,
  // The test keys of button_layout and of move_stick act on the first frame
  // of the word.
  kLayoutKey,
  kMoveKey,
  // The test keys of teleport_switch (teleport.cpp) and of prompt_moves
  // (hint_card.cpp) act on the first frame of the word.
  kTeleportKey,
  kPromptKey,
  kInputCount
};

const char* const kInputName[kInputCount] = {
    "trigger",    "grip",       "a",          "b",        "x",
    "y",          "menu",       "stick_click", "stick_left", "stick_right",
    "stick_up",   "stick_down", "pose2",      "point",      "seek",
    "seekc",      "nudge",      "slap",       "lpoint",     "lgrip",
    "lean",       "look",       "trace",      "shot",       "touchc",
    "rot",        "aimfar",     "pull",       "aimcmd",     "aimturn",
    "nofocus",    "deactivate",
    "lstick_left", "lstick_right", "lstick_up", "lstick_down", "ltrigger",
    "aimtemple",  "gripdip",    "exit",       "bench",
    "lmenu",      "layoutkey",  "movekey",    "teleportkey", "promptkey"};

// The fake hand positions for the word "point", in cm from the head center.
const int kMaxPoints = 16;
int g_points[kMaxPoints][3];
int g_point_count = 0;
int g_point_index = -1;  // -1 is the base pose
int g_points_used = 0;
// The same for the left hand and the word "lpoint".
int g_lpoints[kMaxPoints][3];
int g_lpoint_count = 0;
int g_lpoint_index = -1;
int g_lpoints_used = 0;
// The turns of the right hand for the word "rot": yaw,pitch,roll in degrees,
// from the test key fake_controller_rots. -1 is the pose of the settings.
int g_rots[kMaxPoints][3];
int g_rot_count = 0;
int g_rot_index = -1;
int g_rots_used = 0;
// The word "aimfar" turns the right hand to a yaw and a pitch that point the
// aim ray at an object. The word "pull" steps the right hand 15 cm toward the
// head center (the origin of the cm of the points) over a few frames, and the
// next "pull" steps it back.
bool g_aim_set = false;
float g_aim_yaw = 0.0f;
float g_aim_pitch = 0.0f;
// The word "aimturn" turns the aim of the right hand by the next step of the
// test key fake_controller_turns: yaw,pitch,0 in degrees, yaw to the left
// and pitch up. A step, and not a motion.
int g_turns[kMaxPoints][3];
int g_turn_count = 0;
int g_turns_used = 0;
float g_pull_cm[3] = {0.0f, 0.0f, 0.0f};       // the step now, in cm
float g_pull_from[3] = {0.0f, 0.0f, 0.0f};
float g_pull_to[3] = {0.0f, 0.0f, 0.0f};
uint64_t g_pull_start = 0;
uint64_t g_pull_frames = 0;
// The exact Hand: the offset from the grip to the real index tip in the
// world, on the last frame, so the word "seek" puts the tip on the object.
bool g_have_tip_offset = false;
float g_last_tip_offset[3] = {0.0f, 0.0f, 0.0f};
// The words "lean" and "look" step the invented head. Without a "lean" word,
// a lean that is set holds for the whole run.
bool g_script_leans = false;
// The word "seek" steps the hand once onto the nearest holdable object, for
// the pick test. The pose is worked out from the last frame's room and reach
// anchor.
const int kSeekIndex = -2;
float g_seek_cm[3] = {0.0f, 0.0f, 0.0f};
// The words "nudge" and "slap" step the hand sideways by a known distance, in
// one frame, for the Creature tests. Each one toggles between the two
// sides of the pose. A seek clears the offset. "seekc" seeks the Creature,
// and "touchc" puts the hand on its body bone.
float g_side_cm = 0.0f;
// The word "trace" steps the hand through a gesture template of
// Data\Gestures.jty. It holds the first pose for kTraceLead frames, so the
// script can press the draw button there, and then takes the next still pose
// every kTraceFrames frames. The template
// holds only the corners of the path, on a square of side 1 with y down. The
// trace puts it on a square of gesture_cm in front of the head and adds a
// point every 8.5% of that side between the corners, so each pose moves the
// hand a little more than the sample spacing of the draw (7.5% of the side).
const int kTraceIndex = -3;
const int kMaxTraces = 8;
const int kMaxTracePoints = 160;
const uint64_t kTraceFrames = 2;
const uint64_t kTraceLead = 10;  // frames on the first pose, to press the button
const float kTraceCenterCm[3] = {0.0f, -10.0f, 45.0f};  // right, up, forward
float g_trace_cm[kMaxTraces][kMaxTracePoints][3];
int g_trace_points[kMaxTraces] = {0};
int g_trace_template[kMaxTraces] = {0};
int g_trace_id[kMaxTraces] = {0};
int g_trace_count = 0;
int g_traces_used = 0;
int g_trace_now = 0;
int g_trace_step = 0;
float g_last_anchor[3];       // the reach anchor in the world
float g_last_anchor_room[3];  // the same in the room
float g_last_room[9];
bool g_have_last_room = false;
// The table of the last frame, for the word "seek" on the table.
bool g_last_table = false;
float g_last_table_origin[3];
float g_last_table_gain = 1.0f;
// On the table the pick only gets the objects near the Hand, so a seek first
// asks for a wider ball and steps 2 frames later.
uint64_t g_seek_due = 0;
// What a seek looks for.
const int kSeekObject = 0;    // "seek": the nearest holdable object
const int kSeekLine = 1;      // "seekc": a point on the line to the Creature
const int kSeekBody = 2;      // "touchc": the Creature's body bone
const int kSeekFar = 3;       // "aimfar": a far target, to aim at
const int kSeekCommand = 4;   // "aimcmd": a target of a Creature command
int g_seek_due_kind = kSeekObject;

struct ScriptStep {
  int input;
  uint64_t start;
  uint64_t frames;
};

FakeControllerSettings g_fake;
ScriptStep g_script[kMaxScript];
int g_script_count = 0;
bool g_held[kInputCount] = {false};  // what the script holds this frame
bool g_pose2_latched = false;        // pose2 moves the hand for good
bool g_sweep_turned = false;         // the sweep is at its second pose

// input@frame+frames, separated by spaces.
void ParseScript(const char* text) {
  g_script_count = 0;
  const char* p = text;
  while (*p != 0) {
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 0) break;
    const char* end = p;
    while (*end != 0 && *end != ' ' && *end != '\t') ++end;
    char word[64];
    const size_t length = static_cast<size_t>(end - p);
    if (length >= sizeof(word)) {
      ModLog("controllers  : the script word \"%.20s...\" is too long, skipped",
             p);
      p = end;
      continue;
    }
    memcpy(word, p, length);
    word[length] = 0;
    p = end;

    char* at = strchr(word, '@');
    char* plus = at != nullptr ? strchr(at, '+') : nullptr;
    int input = -1;
    if (at != nullptr && plus != nullptr) {
      *at = 0;
      *plus = 0;
      for (int i = 0; i < kInputCount; ++i) {
        if (strcmp(word, kInputName[i]) == 0) input = i;
      }
    }
    if (input < 0 || g_script_count >= kMaxScript) {
      ModLog("controllers  : the script word \"%.40s\" is not input@frame+frames"
             ", skipped", word);
      continue;
    }
    ScriptStep& step = g_script[g_script_count++];
    step.input = input;
    step.start = static_cast<uint64_t>(strtoul(at + 1, nullptr, 10));
    step.frames = static_cast<uint64_t>(strtoul(plus + 1, nullptr, 10));
    if (input == kLean) g_script_leans = true;
  }
}

// right,up,forward in cm, separated by spaces.
void ParsePoints(const char* text, int (*points)[3], int* count) {
  int& point_count = *count;
  point_count = 0;
  const char* p = text;
  while (*p != 0 && point_count < kMaxPoints) {
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == 0) break;
    int value[3] = {0, 0, 0};
    char* end = nullptr;
    bool ok = true;
    for (int i = 0; i < 3 && ok; ++i) {
      value[i] = static_cast<int>(strtol(p, &end, 10));
      if (end == p) ok = false;
      p = end;
      if (i < 2) {
        if (*p == ',') {
          ++p;
        } else {
          ok = false;
        }
      }
    }
    if (!ok) {
      ModLog("controllers  : a fake point is not right,up,forward in cm, the "
             "rest is skipped");
      break;
    }
    for (int i = 0; i < 3; ++i) points[point_count][i] = value[i];
    ++point_count;
  }
}

// One template of Data\Gestures.jty: an int count of templates, then 1628
// bytes each. A template starts with 80 samples of five floats (x, a pad, y,
// the turn and the direction), then the corner count and the gesture type in
// the first bytes of the next two ints, and five more numbers. See
// docs/miracles.md, "Storage".
const long kJtyRecord = 0x640 + 7 * 4;

void AddTracePoint(int trace, float x, float y) {
  if (g_trace_points[trace] >= kMaxTracePoints) return;
  const float side = static_cast<float>(g_fake.gesture_cm);
  float* cm = g_trace_cm[trace][g_trace_points[trace]++];
  cm[0] = kTraceCenterCm[0] + (x - 0.5f) * side;
  cm[1] = kTraceCenterCm[1] + (0.5f - y) * side;
  cm[2] = kTraceCenterCm[2];
}

void LoadTraces(const char* list) {
  g_trace_count = 0;
  g_traces_used = 0;
  if (list[0] == 0) return;
  FILE* file = nullptr;
  if (fopen_s(&file, "Data\\Gestures.jty", "rb") != 0 || file == nullptr) {
    ModLog("controllers  : the fake hand cannot read Data\\Gestures.jty");
    return;
  }
  int32_t total = 0;
  if (fread(&total, 4, 1, file) != 1) total = 0;
  const char* p = list;
  while (*p != 0 && g_trace_count < kMaxTraces) {
    char* end = nullptr;
    const long index = strtol(p, &end, 10);
    if (end == p) break;
    p = end;
    if (index < 0 || index >= total) {
      ModLog("controllers  : Data\\Gestures.jty has no template %ld", index);
      continue;
    }
    float samples[80][5];
    uint8_t tail[8];
    if (fseek(file, 4 + index * kJtyRecord, SEEK_SET) != 0 ||
        fread(samples, sizeof(samples), 1, file) != 1 ||
        fread(tail, sizeof(tail), 1, file) != 1) {
      ModLog("controllers  : Data\\Gestures.jty template %ld cannot be read",
             index);
      continue;
    }
    int corners = tail[0] <= 80 ? tail[0] : 80;
    // The corners are on a square of side 1. Anything else is not a template.
    for (int i = 0; i < corners; ++i) {
      const float x = samples[i][0], y = samples[i][2];
      if (!(x >= -0.5f && x <= 1.5f && y >= -0.5f && y <= 1.5f)) corners = 0;
    }
    if (corners < 2) {
      ModLog("controllers  : Data\\Gestures.jty template %ld has no path",
             index);
      continue;
    }
    const int trace = g_trace_count;
    g_trace_points[trace] = 0;
    g_trace_template[trace] = static_cast<int>(index);
    g_trace_id[trace] = tail[4];
    // The corners, with points between them every 8.5% of the side.
    const float step = 0.085f;
    for (int i = 0; i < corners; ++i) {
      const float x = samples[i][0], y = samples[i][2];
      if (i > 0) {
        const float x0 = samples[i - 1][0], y0 = samples[i - 1][2];
        const float span = sqrtf((x - x0) * (x - x0) + (y - y0) * (y - y0));
        const int parts = static_cast<int>(span / step);
        for (int k = 1; k <= parts; ++k) {
          const float t = k * step / span;
          if (t < 0.999f) AddTracePoint(trace, x0 + (x - x0) * t,
                                        y0 + (y - y0) * t);
        }
      }
      AddTracePoint(trace, x, y);
    }
    ++g_trace_count;
    ModLog("controllers  : the fake hand will trace template %ld of %d, "
           "gesture type %d, %d corners, as %d poses on a square of %d cm",
           index, total, g_trace_id[trace], corners, g_trace_points[trace],
           g_fake.gesture_cm);
  }
  fclose(file);
}

// The values the runtime would report for one fake hand. The grip pose is the
// aim pose. The script drives the pointing hand (the right hand, or the left
// hand when the left controller is the Hand), and the other hand's grip and
// points.
// Where the fake pointing hand is, in cm from the head center (right, up,
// forward): the base pose or the point of a word, then the sideways step and
// the step of the word "pull". The base pose is on the side of the
// controller: on the left of the head for the left controller. The point of a
// word is a place in the room, so it is not mirrored.
void RightHandCm(float cm[3]) {
  cm[0] = static_cast<float>(g_fake.right_cm) * (g_left_handed ? -1.0f : 1.0f);
  cm[1] = static_cast<float>(g_fake.up_cm);
  cm[2] = static_cast<float>(g_fake.forward_cm);
  if (g_point_index >= 0 && g_point_index < g_point_count) {
    for (int i = 0; i < 3; ++i) {
      cm[i] = static_cast<float>(g_points[g_point_index][i]);
    }
  } else if (g_point_index == kSeekIndex) {
    for (int i = 0; i < 3; ++i) cm[i] = g_seek_cm[i];
  } else if (g_point_index == kTraceIndex && g_trace_now < g_trace_count &&
             g_trace_points[g_trace_now] > 0) {
    for (int i = 0; i < 3; ++i) cm[i] = g_trace_cm[g_trace_now][g_trace_step][i];
  }
  cm[0] += g_side_cm;
  for (int i = 0; i < 3; ++i) cm[i] += g_pull_cm[i];
}

// The yaw to the left that the pose of the left controller needs, so that its
// aim ray points as the ray of the right hand with the same pose (the aim
// block below).
float FakeLeftAimFix();

void FakeHand(int index, bwvr::HandInput* out) {
  const bool both = g_fake.mode >= 2;
  // The job of this controller in the script: 1 the words of the right hand
  // ("trigger", "point", "seek"), 0 the words of the left hand ("lgrip",
  // "lpoint"). When the left controller is the Hand, the words of the right
  // hand drive it, so each recipe runs with either hand.
  const int role = g_left_handed ? 1 - index : index;
  memset(out, 0, sizeof(*out));
  out->aim_orientation[3] = 1.0f;
  out->grip_orientation[3] = 1.0f;
  if (g_fake.mode == 0 || (role == 0 && !both)) return;
  // The word "nofocus": the headset session has no focus of the runtime, as
  // with the menu of the headset open. XrScreen then gives hands that are
  // not active, and so does the fake controller.
  if (g_held[kNoFocus]) return;

  // The other hand of a pair is the mirror image of the base pose: the
  // sideways offset, the yaw and the roll change sign on the left
  // controller. The pointing hand keeps the turns of the script, and on the
  // left controller it also turns by the difference of the aim ray of the two
  // hands. So its ray points where the ray of the right hand points, and a
  // recipe clicks the same menu items with either hand.
  const float mirror = (index == 0) ? -1.0f : 1.0f;
  const float turn = role == 1 ? 1.0f : mirror;
  const float fix = role == 1 && index == 0 ? FakeLeftAimFix() : 0.0f;
  const bool second = g_sweep_turned || g_pose2_latched;
  const float yaw = (second ? g_fake.sweep_yaw : g_fake.yaw) * turn + fix;
  const float pitch = static_cast<float>(second ? g_fake.sweep_pitch
                                                : g_fake.pitch);
  const float roll = g_fake.roll * turn;
  float q[4];
  if (role == 1 && g_aim_set) {
    controller_math::FakeOrientation(g_aim_yaw, g_aim_pitch, roll, q);
  } else if (role == 1 && g_rot_index >= 0 && g_rot_index < g_rot_count) {
    controller_math::FakeOrientation(
        static_cast<float>(g_rots[g_rot_index][0]) + fix,
        static_cast<float>(g_rots[g_rot_index][1]),
        static_cast<float>(g_rots[g_rot_index][2]), q);
  } else {
    controller_math::FakeOrientation(yaw, pitch, roll, q);
  }

  // LOCAL space: right is +X, up is +Y, forward is -Z. The word "point"
  // steps the pointing hand, and "lpoint" the other hand.
  float cm[3];
  if (role == 1) {
    RightHandCm(cm);
  } else {
    // The other hand: the mirror of the base pose, on its own side.
    cm[0] = static_cast<float>(g_fake.right_cm) * mirror;
    cm[1] = static_cast<float>(g_fake.up_cm);
    cm[2] = static_cast<float>(g_fake.forward_cm);
  }
  // A left point is given as it is, not mirrored.
  if (role == 0 && g_lpoint_index >= 0 && g_lpoint_index < g_lpoint_count) {
    for (int i = 0; i < 3; ++i) {
      cm[i] = static_cast<float>(g_lpoints[g_lpoint_index][i]);
    }
  }
  const float position[3] = {cm[0] * 0.01f, cm[1] * 0.01f, -cm[2] * 0.01f};
  for (int i = 0; i < 3; ++i) {
    out->aim_position[i] = position[i];
    out->grip_position[i] = position[i];
  }
  for (int i = 0; i < 4; ++i) {
    out->aim_orientation[i] = q[i];
    out->grip_orientation[i] = q[i];
  }
  out->active = true;
  out->aim_valid = true;
  out->grip_valid = true;
  out->aim_tracked = true;
  out->grip_tracked = true;

  if (role == 1) {
    out->trigger = g_held[kTrigger] ? 1.0f : 0.0f;
    out->squeeze = g_held[kGrip] ? (g_held[kGripDip] ? 0.59f : 1.0f) : 0.0f;
    out->stick[0] = (g_held[kStickRight] ? 1.0f : 0.0f) -
                    (g_held[kStickLeft] ? 1.0f : 0.0f);
    out->stick[1] = (g_held[kStickUp] ? 1.0f : 0.0f) -
                    (g_held[kStickDown] ? 1.0f : 0.0f);
    out->stick_click = g_held[kStickClick];
    out->primary = g_held[kA];
    out->secondary = g_held[kB];
    out->menu = g_held[kMenu];
  } else {
    out->squeeze = g_held[kLGrip] ? 1.0f : 0.0f;
    // The face buttons of the left hand, X and Y, and its menu button.
    out->primary = g_held[kX];
    out->secondary = g_held[kY];
    out->menu = g_held[kLMenu];
    // The stick and the trigger of the left hand.
    out->trigger = g_held[kLTrigger] ? 1.0f : 0.0f;
    out->stick[0] = (g_held[kLStickRight] ? 1.0f : 0.0f) -
                    (g_held[kLStickLeft] ? 1.0f : 0.0f);
    out->stick[1] = (g_held[kLStickUp] ? 1.0f : 0.0f) -
                    (g_held[kLStickDown] ? 1.0f : 0.0f);
  }
}

// --- one frame ----------------------------------------------------------------

struct Pointing {
  bool have_hand;        // a hand that is active and tracked
  bool have_world;       // the transform into the world worked
  bool have_pixel;
  bool off_view;
  float origin[3];       // the aim ray, in world units
  float direction[3];
  float eye_distance;    // origin to the eye
  float target_distance; // s
  bool menu_depth;       // s is the depth of the menu or of a room of the
                         // Temple, not the focus
  float x, y;            // the render pixel
  char profile[64];
  float room_direction[3];  // the aim direction in the room, before L
  bool aim_tracked;      // tracked, or no tracking reported yet
  bool grip_tracked;
  // The direct hand.
  bool have_grip;        // the grip point is known in the world
  bool grip_frozen;      // not tracked, so the last tracked point is used
  float grip[3];         // the grip point, one to one
  float grip_room[3];    // the same in the room, for the motion test
  float anchor[3];       // the reach anchor in the world
  float reach[3];        // the grip point moved out by the reach curve
  float reach_room[3];   // the same in the room, for the velocity
  float room_basis[9];   // L, to turn a room velocity into the world
  float reach_factor;    // reach distance / real distance, 1 up close
  float grip_distance;   // the real distance from the anchor, units
  bool have_camera_dir;
  float camera_dir[3];   // the game camera to the reach point, for the drag
  bool have_hand_pixel;
  bool hand_off_view;
  float hand_x, hand_y;  // the render pixel of the reach point
  // The head matrix in the room, of the same head sample, and the
  // render size.
  float head[9];
  float render_w, render_h;
  // The table owns this frame, and the hand goes on it (docs/tabletop.md,
  // "The hand on the table").
  bool table;
  // The exact Hand (docs/hand.md, "7. The exact Hand"): the grip's axes in the
  // world, L * M_grip, and the offset from the grip to the real index tip.
  bool have_rotation;
  float grip_rotation[9];
  float tip_offset[3];
};

bwvr::HandInput g_hands[2];
// The aim orientation of each hand in this frame, with the turn of UpdateAim.
// Every user of the aim reads this one and the point below: the pointer, the
// menu ray, the teleport, the grab from a distance and the Creature commands.
float g_aim_q[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
// The aim point of each hand in this frame, in the space of the runtime: the
// aim point of the runtime, moved by the preset.
float g_aim_p[2][3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
Pointing g_point;
uint64_t g_frame = 0;
uint64_t g_frames_in_window = 0;
bool g_said_no_head = false;

// TRACKED OR INFERRED. A controller outside the headset's cameras keeps its
// orientation, but its position holds or drifts, then snaps back. Through
// the reach gain a snap of 3 cm moves a far Hand by meters. So an untracked
// grip holds the direct Hand at its last tracked point, and an untracked
// pose is not motion for the owner rule.
//
// A runtime that never reports the tracked bits would hold the Hand still
// for good. So until a pose has been tracked once, a valid pose counts as
// tracked.
bool g_seen_aim_tracked = false;
bool g_seen_grip_tracked = false;
bool g_logged_aim_tracked = true;
bool g_logged_grip_tracked = true;
int g_track_logs = 0;
const int kMaxTrackLogs = 100;
// The grip in the room on the last tracked frame.
float g_tracked_grip[3];
bool g_have_tracked_grip = false;

bool Tracked(bool reported, bool* seen) {
  if (reported) *seen = true;
  return reported || !*seen;
}

// Each change of the tracked state, not each frame.
void LogTracked(const char* pose, const char* held, bool tracked,
                bool* logged) {
  if (tracked == *logged) return;
  *logged = tracked;
  if (g_track_logs >= kMaxTrackLogs) return;
  ++g_track_logs;
  if (tracked) {
    ModLog("controllers  : the %s pose is tracked again, frame %llu", pose,
           g_frame);
  } else {
    ModLog("controllers  : the %s pose is not tracked, so %s, frame %llu",
           pose, held, g_frame);
  }
}

// The turn of the aim ray of the pointing hand now, to the left and up (the
// aim block below). The word "aimfar" takes it off the pose of the fake hand,
// so its ray goes to the object with each preset and each trim.
void AimTurnOfPointingHand(float* yaw_left_deg, float* pitch_up_deg);

// The fake hand pose in cm that puts the direct hand on a point in the world:
// the reach curve backward from the anchor, then the room.
void Seek(int kind) {
  if (g_last_table && g_seek_due == 0) {
    if (kind == kSeekFar || kind == kSeekCommand) {
      HandDirectWidenFar(3, 6.5f);
    } else {
      HandDirectWidenPick(3);
    }
    g_seek_due = g_frame + 2;
    g_seek_due_kind = kind;
    return;
  }
  g_seek_due = 0;
  float world[3];
  const float scale = HeadPoseScale();
  if (kind == kSeekFar || kind == kSeekCommand) {
    // "aimfar": the hand stays where it is, and its aim turns to the object.
    // Only on the table and in the God view, where the room is the table's.
    // "aimcmd" is the same with a target of a Creature command.
    if (!g_have_last_room || scale <= 0.0f || !g_last_table ||
        !(kind == kSeekFar ? HandDirectSeekFarPoint(world)
                           : CreatureCommandTestPoint(world))) {
      ModLog("controllers  : the fake hand has nothing far to aim at, frame "
             "%llu", g_frame);
      return;
    }
    float in_room[3];
    controller_math::TableRoomPoint(g_last_table_origin, g_last_room,
                                    g_last_table_gain, world, in_room);
    float hand[3];
    RightHandCm(hand);
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = in_room[i] / scale * 100.0f - hand[i];
    if (!controller_math::FakeAimAngles(d, &g_aim_yaw, &g_aim_pitch)) return;
    // The ray has the turn of the preset and of the trim on top of the pose.
    // For small angles the two turns add, so the pose gets the angles less
    // the turn.
    float turn_yaw = 0.0f, turn_pitch = 0.0f;
    AimTurnOfPointingHand(&turn_yaw, &turn_pitch);
    g_aim_yaw -= turn_yaw;
    g_aim_pitch -= turn_pitch;
    g_aim_set = true;
    ModLog("controllers  : the fake hand aims its ray at the far object: the "
           "pose has yaw %.2f, pitch %.2f degrees, and the ray turns %.2f to "
           "the left and %.2f up from it. From %.1f %.1f %.1f cm, %.0f cm "
           "away, at frame %llu", g_aim_yaw, g_aim_pitch, turn_yaw,
           turn_pitch, hand[0], hand[1], hand[2], controller_math::Length(d),
           g_frame);
    return;
  }
  g_side_cm = 0.0f;
  if (!g_have_last_room || scale <= 0.0f ||
      !(kind == kSeekBody   ? HandDirectTouchCreaturePoint(world)
        : kind == kSeekLine ? HandDirectSeekCreaturePoint(world)
                            : HandDirectSeekPoint(world))) {
    ModLog("controllers  : the fake hand has nothing to seek at frame %llu",
           g_frame);
    return;
  }
  // With the exact Hand the point is the index tip, so the grip goes to the
  // object less the tip offset of the last frame. A seek does not turn the
  // hand, so the offset stays.
  if (g_have_tip_offset) {
    for (int i = 0; i < 3; ++i) world[i] -= g_last_tip_offset[i];
  }
  if (g_last_table) {
    // On the table there is no reach curve: the room point is the table map
    // backward.
    float in_room[3];
    controller_math::TableRoomPoint(g_last_table_origin, g_last_room,
                                    g_last_table_gain, world, in_room);
    for (int i = 0; i < 3; ++i) g_seek_cm[i] = in_room[i] / scale * 100.0f;
    g_point_index = kSeekIndex;
    ModLog("controllers  : on the table the fake hand steps to %.1f %.1f %.1f "
           "cm, which reaches %.2f %.2f %.2f, at frame %llu", g_seek_cm[0],
           g_seek_cm[1], g_seek_cm[2], world[0], world[1], world[2], g_frame);
    return;
  }
  float offset[3];
  for (int i = 0; i < 3; ++i) offset[i] = world[i] - g_last_anchor[i];
  const float span = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] +
                           offset[2] * offset[2]);
  if (!(span > 1e-4f)) return;
  const float real = controller_math::ReachInverse(
      span / scale, g_direct.reach_start_cm * 0.01f,
      static_cast<float>(g_direct.reach_gain)) * scale;
  for (int i = 0; i < 3; ++i) offset[i] *= real / span;
  float in_room[3];
  controller_math::Apply(g_last_room, true, offset, in_room);
  for (int i = 0; i < 3; ++i) {
    g_seek_cm[i] = (in_room[i] + g_last_anchor_room[i]) / scale * 100.0f;
  }
  g_point_index = kSeekIndex;
  ModLog("controllers  : the fake hand steps to %.1f %.1f %.1f cm, which "
         "reaches %.2f %.2f %.2f, at frame %llu", g_seek_cm[0], g_seek_cm[1],
         g_seek_cm[2], world[0], world[1], world[2], g_frame);
}

// THE SCRIPT AFTER THE FIRST STORY (the test key fake_controller_play_frame).
// The first story of a new game ends at a time, not at a frame. So the frame
// at which the player gets the Hand changes with the frame rate and the
// render size. With the key at N, the steps of the script from frame N on
// count from the frame at which the controller first places the Hand after
// frame 1000: that frame is N. The steps before N keep their frames. 0 is off.
// fake_controller_play_after gives a different frame than 1000. A run that
// loads a test save has no start dialog and no first story, so its script
// can count from an early frame, and the shift is then less than 0.
int IniInt(const char* key, int fallback);
bool DirectHandNow();
int g_script_play = -1;
int g_script_play_after = 1000;
bool g_script_shift_known = false;
int64_t g_script_shift = 0;
// The word "deactivate" came. The pump acts on it.
bool g_fake_deactivate = false;

// The frame that a step of the script compares with its own frames.
uint64_t StepFrame(const ScriptStep& step) {
  return script_frame_math::Counted(g_frame, step.start, g_script_play,
                                    g_script_shift_known, g_script_shift);
}

// The word "aimtemple": the hand stays where it is, and its aim turns to the
// next target of temple_test_targets, a control that the scan of the room
// found (temple.cpp). The direction comes from the ray of the last frame.
void AimTemple() {
  float world[3];
  if (!g_have_last_room || !g_point.have_world || !TempleTestTarget(world)) {
    ModLog("controllers  : the fake hand has no target in the Temple to aim "
           "at, frame %llu", g_frame);
    return;
  }
  float offset[3], d[3];
  for (int i = 0; i < 3; ++i) offset[i] = world[i] - g_point.origin[i];
  controller_math::Apply(g_last_room, true, offset, d);
  if (!controller_math::FakeAimAngles(d, &g_aim_yaw, &g_aim_pitch)) return;
  float turn_yaw = 0.0f, turn_pitch = 0.0f;
  AimTurnOfPointingHand(&turn_yaw, &turn_pitch);
  g_aim_yaw -= turn_yaw;
  g_aim_pitch -= turn_pitch;
  g_aim_set = true;
  ModLog("controllers  : the fake hand aims its ray at the target in the "
         "Temple: the pose has yaw %.2f, pitch %.2f degrees, at frame %llu",
         g_aim_yaw, g_aim_pitch, g_frame);
}

void UpdateScript() {
  if (g_script_play < 0) {
    g_script_play = IniInt("fake_controller_play_frame", 0);
    g_script_play_after = IniInt("fake_controller_play_after", 1000);
    if (g_script_play_after < 0) g_script_play_after = 0;
  }
  if (g_script_play > 0 && !g_script_shift_known &&
      g_frame > static_cast<uint64_t>(g_script_play_after) &&
      DirectHandNow() && g_pointer_owned && g_point.have_grip) {
    g_script_shift_known = true;
    // With fake_controller_play_after at 1000 or more, a script keeps its
    // frames when the Hand comes before frame N: the shift is then 0.
    g_script_shift =
        script_frame_math::Shift(g_frame, g_script_play, g_script_play_after);
    ModLog("controllers  : the controller places the Hand after frame %d at "
           "frame %llu. The steps of the script from frame %d on are %lld "
           "frames %s (fake_controller_play_frame), %.1f s after the start "
           "of the game process", g_script_play_after, g_frame, g_script_play,
           g_script_shift < 0 ? -g_script_shift : g_script_shift,
           g_script_shift < 0 ? "sooner" : "later",
           SecondsSinceProcessStart());
  }
  bool now[kInputCount] = {false};
  for (int i = 0; i < g_script_count; ++i) {
    const ScriptStep& step = g_script[i];
    const uint64_t t = StepFrame(step);
    if (t >= step.start && t < step.start + step.frames) {
      now[step.input] = true;
    }
    if (step.input == kPose2 && t >= step.start) g_pose2_latched = true;
    // One step to the next point, on the first frame of the word only.
    // After the last point the list starts again.
    if (step.input == kPoint && t == step.start && g_point_count > 0) {
      g_point_index = g_points_used++ % g_point_count;
      ModLog("controllers  : the fake hand steps to point %d (%d %d %d cm) "
             "at frame %llu", g_point_index, g_points[g_point_index][0],
             g_points[g_point_index][1], g_points[g_point_index][2], g_frame);
    }
    if (step.input == kLPoint && t == step.start && g_lpoint_count > 0) {
      g_lpoint_index = g_lpoints_used++ % g_lpoint_count;
      ModLog("controllers  : the fake left hand steps to point %d (%d %d %d "
             "cm) at frame %llu", g_lpoint_index,
             g_lpoints[g_lpoint_index][0], g_lpoints[g_lpoint_index][1],
             g_lpoints[g_lpoint_index][2], g_frame);
    }
    // The next template, a still pose every kTraceFrames frames. After the
    // last pose the hand stays there.
    if (step.input == kTrace && t == step.start) {
      if (g_trace_count > 0) {
        g_trace_now = g_traces_used++ % g_trace_count;
        g_trace_step = 0;
        g_point_index = kTraceIndex;
        ModLog("controllers  : the fake hand traces template %d (gesture "
               "type %d), %d poses from frame %llu", g_trace_template[
               g_trace_now], g_trace_id[g_trace_now],
               g_trace_points[g_trace_now], g_frame);
      } else {
        ModLog("controllers  : the fake hand has no template to trace at "
               "frame %llu", g_frame);
      }
    }
    if (step.input == kTrace && g_point_index == kTraceIndex &&
        t >= step.start && t < step.start + step.frames &&
        g_trace_now < g_trace_count) {
      const uint64_t since = t - step.start;
      const uint64_t pose =
          since < kTraceLead ? 0 : (since - kTraceLead) / kTraceFrames + 1;
      const int last = g_trace_points[g_trace_now] - 1;
      g_trace_step = pose < static_cast<uint64_t>(last)
                         ? static_cast<int>(pose)
                         : (last > 0 ? last : 0);
    }
    if (step.input == kAimFar && t == step.start) Seek(kSeekFar);
    if (step.input == kAimCommand && t == step.start) Seek(kSeekCommand);
    if (step.input == kAimTurn && t == step.start && g_turn_count > 0) {
      const int* turn = g_turns[g_turns_used++ % g_turn_count];
      if (!g_aim_set) {
        g_aim_yaw = static_cast<float>(g_fake.yaw);
        g_aim_pitch = static_cast<float>(g_fake.pitch);
        g_aim_set = true;
      }
      // A third number that is not 0: the two angles are the aim itself,
      // and not a turn from the last aim.
      if (turn[2] != 0) {
        g_aim_yaw = 0.0f;
        g_aim_pitch = 0.0f;
      }
      g_aim_yaw += static_cast<float>(turn[0]);
      g_aim_pitch += static_cast<float>(turn[1]);
      ModLog("controllers  : the fake hand turns its aim %d degrees to the "
             "left and %d degrees up, to yaw %.2f and pitch %.2f, at frame "
             "%llu", turn[0], turn[1], g_aim_yaw, g_aim_pitch, g_frame);
    }
    if (step.input == kAimTemple && t == step.start) AimTemple();
    if (step.input == kPull && t == step.start) {
      // 15 cm toward the head center, or back to where the hand was.
      float here[3];
      RightHandCm(here);
      for (int k = 0; k < 3; ++k) here[k] -= g_pull_cm[k];
      const float length = controller_math::Length(here);
      const bool back = controller_math::Length(g_pull_cm) > 0.5f;
      for (int k = 0; k < 3; ++k) {
        g_pull_from[k] = g_pull_cm[k];
        g_pull_to[k] = back || !(length > 1.0f) ? 0.0f
                                                : -here[k] / length * 15.0f;
      }
      g_pull_start = g_frame;
      g_pull_frames = step.frames > 0 ? step.frames : 1;
      ModLog("controllers  : the fake hand steps %s over %llu frames at frame "
             "%llu", back ? "back to where it was" : "15 cm toward the head",
             g_pull_frames, g_frame);
    }
    if (step.input == kSeek && t == step.start) Seek(kSeekObject);
    if (step.input == kSeekCreature && t == step.start) Seek(kSeekLine);
    if (step.input == kTouchCreature && t == step.start) Seek(kSeekBody);
    if (step.input == kRot && t == step.start && g_rot_count > 0) {
      g_rot_index = g_rots_used++ % g_rot_count;
      ModLog("controllers  : the fake hand turns to yaw %d, pitch %d, roll %d "
             "at frame %llu", g_rots[g_rot_index][0], g_rots[g_rot_index][1],
             g_rots[g_rot_index][2], g_frame);
    }
    if (step.input == kShot && t == step.start) {
      EyeCaptureArmNow(g_frame + 3);
    }
    // The next pump tells the game (FakeDeactivate).
    if (step.input == kDeactivate && t == step.start) g_fake_deactivate = true;
    if (step.input == kExit && t == step.start) ExitTestNow(g_frame);
    if (step.input == kBench && t == step.start) {
      FrameProfileSceneStart(step.frames, g_frame);
    }
    if (step.input == kLayoutKey && t == step.start) {
      ControllersButtonLayoutToggle("The word layoutkey");
    }
    if (step.input == kMoveKey && t == step.start) {
      TabletopGodMoveStickCycle();
    }
    if (step.input == kTeleportKey && t == step.start) {
      TeleportSwitchToggle("The word teleportkey");
    }
    if (step.input == kPromptKey && t == step.start) {
      HintCardMovesToggle("The word promptkey");
    }
    if ((step.input == kNudge || step.input == kSlap) &&
        t == step.start) {
      const float side = step.input == kNudge ? 2.0f : 12.0f;
      g_side_cm = g_side_cm > 0.0f ? -side : side;
      ModLog("controllers  : the fake hand steps %.1f cm to the side at frame "
             "%llu", g_side_cm, g_frame);
    }
  }
  // A seek on the table that waited for the wider pick.
  if (g_seek_due != 0 && g_frame == g_seek_due) Seek(g_seek_due_kind);
  // The step of the word "pull": a part of it each frame, then it stays.
  if (g_pull_frames > 0 && g_frame >= g_pull_start) {
    float t = static_cast<float>(g_frame - g_pull_start + 1) /
              static_cast<float>(g_pull_frames);
    if (t > 1.0f) t = 1.0f;
    for (int i = 0; i < 3; ++i) {
      g_pull_cm[i] = g_pull_from[i] + (g_pull_to[i] - g_pull_from[i]) * t;
    }
    if (t >= 1.0f) g_pull_frames = 0;
  }
  for (int i = 0; i < kInputCount; ++i) {
    if (i == kPoint || i == kSeek || i == kSeekCreature || i == kNudge ||
        i == kSlap || i == kLPoint || i == kTrace || i == kShot ||
        i == kTouchCreature || i == kRot || i == kAimFar || i == kPull ||
        i == kAimCommand || i == kAimTurn || i == kDeactivate ||
        i == kAimTemple || i == kExit || i == kBench || i == kLayoutKey ||
        i == kMoveKey) {
      continue;
    }
    if (i == kPose2) {
      if (g_pose2_latched && !g_held[i]) {
        ModLog("controllers  : the fake hand steps to its second pose at "
               "frame %llu", g_frame);
      }
      g_held[i] = g_pose2_latched;
      continue;
    }
    if (now[i] != g_held[i]) {
      ModLog("controllers  : the fake %s is %s at frame %llu", kInputName[i],
             now[i] ? "down" : "up", g_frame);
    }
    g_held[i] = now[i];
  }
  // The head takes these on the next frame, a step and never a motion.
  if (g_script_leans) HeadPoseFakeLeanStep(g_held[kLean]);
  HeadPoseFakeLookStep(g_held[kLook]);
}

bool MenuUp();

void Compute() {
  Pointing& p = g_point;
  memset(&p, 0, sizeof(p));

  const int index = g_left_handed ? 0 : 1;
  const bwvr::HandInput& hand = g_hands[index];
  if (!hand.active || !hand.aim_valid) return;
  p.have_hand = true;
  if (g_fake.mode != 0) {
    strcpy_s(p.profile, "fake");
  } else if (g_screen != nullptr) {
    strcpy_s(p.profile, g_screen->HandProfile(index));
  }

  // A runtime that gives no grip pose gives the aim pose instead.
  const bool grip_pose = hand.grip_valid;
  p.aim_tracked = Tracked(hand.aim_tracked, &g_seen_aim_tracked);
  p.grip_tracked = grip_pose ? Tracked(hand.grip_tracked, &g_seen_grip_tracked)
                             : p.aim_tracked;
  LogTracked("aim", "its turns do not take the pointer", p.aim_tracked,
             &g_logged_aim_tracked);
  LogTracked("grip", "the direct Hand stays at its last tracked point",
             p.grip_tracked, &g_logged_grip_tracked);

  // ONE HEAD SAMPLE. The head matrix, the lean and the head center all come
  // from the last HeadPoseUpdate. The eye is the game camera moved by that
  // lean, which is where the next frame renders from (EyeFromLean).
  float head[9], lean[3], room[9], game_position[3], focus[3], eye[3],
      t_head[3], t_hand[3], m_hand[9];
  if (!HeadPoseCurrent(head, lean) || !HeadCameraStageBasis(room) ||
      !HeadCameraGameCamera(game_position, focus) ||
      !HeadPoseHeadInRoom(t_head) ||
      !HeadPoseToRoom(g_aim_p[index], g_aim_q[index], m_hand, t_hand)) {
    return;
  }
  // THE TABLE. On the table the room goes into the world from the table's
  // own origin and basis, and every room point is multiplied by s / 7.5
  // (controller_math.h, TableHand). The room points that the motion tests,
  // the throw and the grab keep stay at the life size scale.
  float table_origin[3], table_basis[9], table_center[3], gain = 1.0f;
  p.table = TabletopRoom(table_origin, table_basis, table_center, &gain);
  const float* origin = game_position;
  float t_head_w[3], t_hand_w[3];
  for (int i = 0; i < 3; ++i) {
    t_head_w[i] = t_head[i];
    t_hand_w[i] = t_hand[i];
  }
  if (p.table) {
    origin = table_origin;
    for (int i = 0; i < 9; ++i) room[i] = table_basis[i];
    controller_math::ScaleRoom(t_head, gain, t_head_w);
    controller_math::ScaleRoom(t_hand, gain, t_hand_w);
    float lean_w[3];
    controller_math::ScaleRoom(lean, gain, lean_w);
    controller_math::EyeFromLean(origin, room, lean_w, eye);
  } else {
    controller_math::EyeFromLean(game_position, room, lean, eye);
  }
  p.have_world = true;
  for (int i = 0; i < 9; ++i) p.head[i] = head[i];

  float rotation[9];
  controller_math::HandToWorld(eye, room, t_head_w, t_hand_w, m_hand,
                               p.origin, rotation);
  controller_math::AimDirection(rotation, p.direction);
  // The same direction in the room, before L. The owner rule compares this
  // one, so a turn of the game camera is not a turn of the hand.
  controller_math::AimDirection(m_hand, p.room_direction);
  p.eye_distance = controller_math::Distance(p.origin, eye);

  // The direct hand: the grip pose in the room, then the reach from the
  // anchor, then the world.
  {
    float m_grip[9], t_grip[3];
    if (HeadPoseToRoom(grip_pose ? hand.grip_position : hand.aim_position,
                       grip_pose ? hand.grip_orientation
                                 : hand.aim_orientation,
                       m_grip, t_grip)) {
      if (p.grip_tracked) {
        for (int i = 0; i < 3; ++i) g_tracked_grip[i] = t_grip[i];
        g_have_tracked_grip = true;
      } else if (g_have_tracked_grip) {
        for (int i = 0; i < 3; ++i) t_grip[i] = g_tracked_grip[i];
        p.grip_frozen = true;
      }
      const float scale = HeadPoseScale();
      float anchor_room[3];
      controller_math::ReachAnchor(
          scale, g_direct.reach_anchor_down_cm * 0.01f, anchor_room);
      for (int i = 0; i < 3; ++i) p.grip_room[i] = t_grip[i];
      if (p.table) {
        // No reach curve on the table: the whole land is within arm's
        // reach, and the Hand is where the hand is.
        float eye_again[3];
        controller_math::TableHand(origin, room, gain, lean, t_head, t_grip,
                                   eye_again, p.grip);
        float anchor_w[3];
        controller_math::ScaleRoom(anchor_room, gain, anchor_w);
        controller_math::RoomToWorld(eye, room, t_head_w, anchor_w, p.anchor);
        for (int i = 0; i < 3; ++i) {
          p.reach[i] = p.grip[i];
          p.reach_room[i] = t_grip[i];
        }
        p.reach_factor = 1.0f;
        // In room units at the life size scale, as off the table.
        p.grip_distance = controller_math::Distance(t_grip, anchor_room);
      } else {
        controller_math::DirectReach(
            eye, room, t_head, t_grip, anchor_room, scale,
            g_direct.reach_start_cm * 0.01f,
            static_cast<float>(g_direct.reach_gain), HandDirectFarLimit(),
            p.anchor, p.grip, p.reach, &p.reach_factor);
        p.grip_distance = controller_math::Distance(p.grip, p.anchor);
        for (int i = 0; i < 3; ++i) {
          p.reach_room[i] =
              anchor_room[i] + (t_grip[i] - anchor_room[i]) * p.reach_factor;
        }
      }
      // THE EXACT HAND. hand_exact.cpp puts the index tip of the game's Hand
      // mesh at the Hand's point, so with hand_exact=1 the point is the real
      // index tip (the overlay's at rest), not the grip. The pick, the glow,
      // the land test and the pixel then use the tip. The throw keeps the
      // motion of the grip (reach_room).
      controller_math::Multiply(room, m_grip, p.grip_rotation);
      p.have_rotation = true;
      g_have_tip_offset = false;
      if (HandExactOn()) {
        const float table_size = TabletopHandSize();
        const float length =
            table_size > 0.0f
                ? 3.2f * table_size
                : g_direct.hand_cm * 0.01f * g_direct.scale_pct * 0.01f *
                      scale;
        hand_exact_math::TipOffset(p.grip_rotation, index == 0, length,
                                   p.tip_offset);
        for (int i = 0; i < 3; ++i) {
          p.reach[i] += p.tip_offset[i];
          g_last_tip_offset[i] = p.tip_offset[i];
        }
        g_have_tip_offset = true;
      }
      for (int i = 0; i < 9; ++i) p.room_basis[i] = room[i];
      p.have_camera_dir =
          controller_math::DirectionTo(game_position, p.reach, p.camera_dir);
      p.have_grip = true;
      for (int i = 0; i < 3; ++i) {
        g_last_anchor[i] = p.anchor[i];
        g_last_anchor_room[i] = anchor_room[i];
      }
      for (int i = 0; i < 9; ++i) g_last_room[i] = room[i];
      g_have_last_room = true;
      g_last_table = p.table;
      for (int i = 0; i < 3; ++i) g_last_table_origin[i] = origin[i];
      g_last_table_gain = gain;
    }
  }

  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return;
  controller_math::Screen screen = {info->half_res_x, info->half_res_y,
                                    info->inv_half_tan_fov_x,
                                    info->inv_half_tan_fov_y};
  if (screen.hx <= 0.0f || screen.hy <= 0.0f || screen.fx <= 0.0f ||
      screen.fy <= 0.0f) {
    return;
  }
  p.render_w = 2.0f * screen.hx;
  p.render_h = 2.0f * screen.hy;
  if (p.have_grip) {
    p.have_hand_pixel = controller_math::PointPixel(
        eye, room, head, p.reach, screen, &p.hand_x, &p.hand_y,
        &p.hand_off_view);
  }

  // The target distance: the eye to the game camera's focus. On
  // the table the game camera is not in the picture, so it is the eye to the
  // table center.
  p.target_distance = controller_math::Distance(p.table ? table_center : focus,
                                                eye);

  // THE MENU. While a dialog box is up, the game draws it with the interface
  // shift, so the player sees it at one depth in front of the eyes (see
  // aim_smooth.h, MenuDepth). The aim ray must meet the menu there. With the
  // distance of the focus, the pixel is the line from the eye through a far
  // point of the ray. The menu is much nearer, and the hand is not at the eye,
  // so the cursor and the end of the drawn ray would be far apart.
  if (MenuUp()) {
    float shift_px = 0.0f, separation = 0.0f;
    if (StereoUiDepthNow(&shift_px, &separation)) {
      const float depth =
          aim_smooth::MenuDepth(screen.fx, screen.hx, separation, shift_px);
      const float from_eye[3] = {p.origin[0] - eye[0], p.origin[1] - eye[1],
                                 p.origin[2] - eye[2]};
      float in_room[3], cam_o[3], cam_d[3], along = 0.0f;
      controller_math::Apply(room, true, from_eye, in_room);
      controller_math::Apply(head, true, in_room, cam_o);
      controller_math::Apply(room, true, p.direction, in_room);
      controller_math::Apply(head, true, in_room, cam_d);
      if (depth > 0.0f && aim_smooth::RayToDepth(cam_o, cam_d, depth, &along)) {
        p.target_distance = along;
        p.menu_depth = true;
      }
    }
  }

  // THE TEMPLE. In a room the ray ends on the thing that the game finds
  // under the pointer (temple.cpp). The pixel is that end, seen from the eye.
  bool room_depth = false;
  if (!p.menu_depth) {
    float along = 0.0f;
    if (TempleRayDistance(p.origin, p.direction, eye,
                          HeadPoseScale() * (p.table ? gain : 1.0f), &along)) {
      p.target_distance = along;
      p.menu_depth = true;
      room_depth = true;
    }
  }

  p.have_pixel = controller_math::RenderPixel(
      eye, room, head, p.origin, p.direction, p.target_distance, screen, &p.x,
      &p.y, &p.off_view);
  if (p.menu_depth && !room_depth) {
    static int said = 0;
    if (said < 40 && g_frame % 45 == 1) {
      ++said;
      ModLog("controllers  : the menu ray meets the menu at %.1f units along "
             "the ray, and the pixel is %.1f %.1f (%s), frame %llu. The "
             "focus is %.1f units away", p.target_distance, p.x, p.y,
             p.off_view ? "off the view" : "in the view",
             g_frame, controller_math::Distance(focus, eye));
    }
  }
}

// --- the mouse ------------------------------------------------------------------

// LHMouse::SetButtons, thiscall (uint8 flag), ret 4. AnyWindowMessages, cdecl,
// returns a bool. The LHMouse object, the wheel sum and gWindowActive.
const uintptr_t kSetButtons = 0x007E4B80;
const uintptr_t kAnyWindowMessages = 0x007DB910;
const uintptr_t kMouseObject = 0x00E85204;
const uintptr_t kWheelSum = 0x00E85300;
const uintptr_t kWindowActive = 0x00C311A8;
typedef void(__fastcall* SetButtonsFn)(void* self, void* unused, int flag);
typedef bool(__cdecl* AnyMessagesFn)();
SetButtonsFn o_SetButtons = nullptr;
AnyMessagesFn o_AnyWindowMessages = nullptr;
bool g_installed = false;

// The event flags of SetButtons. A press sets a held bit, and a release clears
// it. The double click flag counts as a press, because it starts the second
// press of a double click, and the release after it is a plain release.
const int kLeft = 0;
const int kRight = 1;
const uint8_t kPressFlag[3] = {0x01, 0x02, 0x40};
const uint8_t kReleaseFlag[3] = {0x04, 0x08, 0x80};
const uint8_t kDoubleFlag[3] = {0x10, 0x20, 0x00};
const char* const kButtonName[3] = {"left", "right", "middle"};

// One bit for each of the three buttons, one mask for each source. The game
// mask is what the game has been told.
uint8_t g_mouse_mask = 0;
uint8_t g_ctrl_mask = 0;
uint8_t g_game_mask = 0;

// Set by the hook on a real press, and by the pump on a controller change. The
// owner rule of pointer_map.cpp reads and clears them.
bool g_mouse_event = false;
bool g_button_event = false;

// The pointing hand's trigger and grip, with hysteresis: closed above
// kGripCloseAbove (0.55) and open below kGripOpenBelow (0.35), in
// GripLevelStep of controller_math.h. With the direct hand, the control that
// is the game's right button also opens when its value falls fast, so the
// Hand lets go when the real hand starts to open (grip_fast_open,
// docs/throw.md, "The release at once").
bool g_trigger_down = false;
bool g_grip_down = false;
controller_math::GripLevel g_trigger_level = {};
controller_math::GripLevel g_grip_level = {};
int g_fast_open = -1;           // grip_fast_open, read at the first pump
// grip_press_guard, read at the first pump: 1 keeps the fast fall from opening
// a grip in the first 300 ms of a press (GripPressGuarded). The test key
// Ctrl Shift Home changes it while the game runs.
int g_press_guard = -1;
bool g_press_guard_key_down = false;
// grip_press_hold, read at the first pump: 1 keeps the button of the game
// down until the pick up of the game is over, and a press on an object that
// moves is never a click for the game (GripStep). 0 gives a carry 167 ms
// after the object came into the Hand of the picture. The test key Shift and
// the plus key of the number pad changes it while the game runs.
int g_press_hold = -1;
bool g_press_hold_key_down = false;
// The time of the frame for the grip machine, in ms: the time of the first
// pump of a frame, so each step of one frame has the same time.
uint64_t g_grip_ms = 0;
uint64_t g_grip_ms_frame = ~0ull;
// The time the button of the game last went down by the grip machine, for
// the log of how long a press was for the game.
uint64_t g_grip_down_ms = 0;
bool g_grip_button_was = false;
ULONGLONG g_right_close_ms = 0;  // the right control closed at this time
uint64_t g_fast_opens = 0;      // openings by a fast fall, this window
uint64_t g_slow_opens = 0;      // openings below kGripOpenBelow
int g_level_logs = 0;
const int kMaxLevelLogs = 80;

// An int key of the game folder's bwvr.ini. For the keys that are not in the
// launcher.
int IniInt(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return fallback;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

struct Button {
  bool need_up;           // a forced release needs a fresh press
  bool have_last;         // a plain press is waiting for its double click
  ULONGLONG last_press;
  float last_direction[3];
};
Button g_button[2];

const ULONGLONG kWatchdogMs = 100;
ULONGLONG g_last_pump = 0;
ULONGLONG g_frame_tick = 0;

// The A button jump: down, up, double click, up, one step a frame. The stage
// is the next step to take. The jump holds the left button while it runs.
bool g_a_event = false;
bool g_a_before = false;
// The teleport reads A and B as edges that ControllersFrame latched once.
bool g_tele_a_press = false;
bool g_tele_b_before = false;
bool g_tele_b_press = false;
bool g_tele_seen_tracked = false;
bool g_jump_control = false;
int g_jump_stage = 0;
bool g_jump_left = false;
bool g_force_double = false;
uint64_t g_jump_frame = ~0ull;

// The stick.
const float kStickPast = 0.7f;
const float kStickBack = 0.3f;
const ULONGLONG kWheelRepeatMs = 250;
const int kWheelClick = 120;
bool g_turn_armed = true;
ULONGLONG g_wheel_tick = 0;
bool g_zoom_blocked = false;  // a push refused during a grab, not back yet
// The God view: one zoom step for each push of the stick. A push acts once,
// and the stick must come back before the next one. The other hand's stick
// moves the player over the land (PumpWalk).
bool g_god_zoom_armed = true;
uint64_t g_god_zooms = 0;
// The smooth zoom of the God view (god_zoom_smooth=1): the scale changes
// while the stick is held past kStickBack, at a rate that follows the push
// and is full from kStickFull. A hold starts with a fresh push only, and it
// holds only in a pump that asks the God view again.
const float kStickFull = 0.95f;
bool g_god_zoom_holding = false;
uint64_t g_god_holds = 0;
bool g_other_before[3] = {false};  // B or Y, the menu button, the stick click

// Which owner the test key Ctrl+F12 asks for: automatic, controller only or
// mouse only.
int g_owner_mode = 0;
const char* const kOwnerModeName[3] = {"automatic", "controller only",
                                       "mouse only"};
// The aim direction the owner rule compares with.
float g_rest_direction[3];
bool g_have_rest = false;

// For the report and the log.
uint64_t g_presses_sent = 0;
uint64_t g_doubles_sent = 0;
uint64_t g_releases_sent = 0;
uint64_t g_jumps_started = 0;
uint64_t g_wheel_sent = 0;
uint64_t g_turns_asked = 0;
uint64_t g_turns_done = 0;
uint64_t g_pulses = 0;
uint64_t g_dropped = 0;
uint64_t g_forced_releases = 0;
int g_event_logs = 0;
const int kMaxEventLogs = 80;
bool g_not_usable[4] = {false};  // the reasons, for the report

// Whether the pointing hand was active on the last frame, and a latch for
// the pump: the hand became active again. See PumpButtons.
bool g_pointing_active = false;
bool g_hand_back = false;

// THE GRIP HOLDS (docs/grab.md, "The grip holds"). With the direct hand the
// right button comes from GripStep, not straight from the grip. The control
// is the grip, or the trigger with controller_swap_buttons=1.
controller_math::GripHold g_hold = {};
bool g_hold_waits = false;  // the last step said the click waits
int g_hold_logs = 0;
const int kMaxHoldLogs = 400;
uint64_t g_hold_take_opens = 0;  // openings that ended a bulk take
uint64_t g_hold_take_ends = 0;   // takes that the mod had to end
bool g_hold_end_take = false;    // the last step asked for the end of a take
uint64_t g_hold_carries = 0;
uint64_t g_hold_let_go = 0;
uint64_t g_hold_late = 0;
uint64_t g_hold_clicks = 0;
uint64_t g_hold_regrips = 0;
uint64_t g_hold_waited = 0;
uint64_t g_hold_held_on = 0;  // presses that the mod held for the game
uint64_t g_hold_short = 0;    // presses that were a click on a moving object

bool DirectMode() { return g_on && g_direct.mode == 1; }

// A dialog box is up: SetupBox::GetCurrentActiveBox 0x00407ED0 returns
// [0x00C4CD20], and a value other than 0 is a box, for example the Esc menu.
const uintptr_t kActiveBox = 0x00C4CD20;
bool MenuUp() {
  return *reinterpret_cast<const uintptr_t*>(kActiveBox) != 0;
}

// The direct hand places the Hand now. While a dialog box is up it steps
// aside, and the pointing hand points like a mouse instead: the aim ray
// picks the pixel and the trigger clicks, as with controller_hand=0. The
// Hand cannot reach the buttons of a box well. The direct hand comes back
// when the box closes. A room of the Temple has the same rule (temple.cpp).
bool DirectHandNow() {
  return DirectMode() && !MenuUp() && !TempleRayOn();
}

bool WindowActive() {
  return *reinterpret_cast<const int32_t*>(kWindowActive) != 0;
}

// A button pressed in another program is not for the game.
bool GameInFront() {
  HWND window = GetForegroundWindow();
  if (window == nullptr) return false;
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  return process == GetCurrentProcessId() && WindowActive();
}

int PointingIndex() { return g_left_handed ? 0 : 1; }

// A TEST KEY for desktop runs, fake_controller_front=1, read only with the
// fake controller on. Windows does not always give the front to a game that
// a background process starts. The fake controller then acts while the game
// window is behind. Its buttons go in through LHMouse::SetButtons in the
// process, so no other program gets them. A real controller never comes here.
bool FakeFront() {
  if (g_fake.mode == 0) return false;
  static int on = -1;
  if (on < 0) {
    on = IniInt("fake_controller_front", 0) != 0 ? 1 : 0;
    if (on == 1) {
      ModLog("controllers  : fake_controller_front=1 (a test). The fake "
             "controller acts while the game window is not in front");
    }
  }
  return on == 1;
}

// THE GAME WINDOW BEHIND (docs/controllers.md, "9. The game window behind"). A
// player in the headset cannot see the desktop. When a different program is
// in front there, the hands would do nothing, and the player would not know
// why. So the headset session stands for the front:
// while it has the focus of the runtime, the controllers act in the game.
// Each input of a controller goes into the game in the process: the buttons,
// the pointer, the wheel sum and Esc. So no other program gets it. The mouse
// and the keyboard of the desktop keep the rule of Windows: they act in the
// program in front. controller_window_behind=0 gives the rule of the plain game, and
// a hand then acts only while the game window is in front.
//
// ControllersFrame latches the focus of the session once a frame. The fake
// controller stands for a session that has the focus, and the script word
// "nofocus" takes that focus away.
int g_behind_option = -1;      // controller_window_behind, read once
bool g_session_focus = false;  // the headset session has the focus

bool BehindOption() {
  if (g_behind_option < 0) {
    g_behind_option = IniInt("controller_window_behind", 1) != 0 ? 1 : 0;
    ModLog("controllers  : controller_window_behind=%d. %s", g_behind_option,
           g_behind_option != 0
               ? "While the headset session has the focus of the runtime, "
                 "the controllers act in the game, also with the game window "
                 "behind a different window"
               : "The controllers act only while the game window is in front");
  }
  return g_behind_option == 1;
}

// The headset session stands for the front now. A latch older than 250 ms is
// from a screen with no scene, and it does not count.
bool SessionFront() {
  return g_on && BehindOption() && g_session_focus &&
         GetTickCount64() - g_frame_tick <= 250;
}

// The controllers act while the game window is behind.
bool ActsBehind() { return FakeFront() || SessionFront(); }

// The window of the game, from the game itself: the handle that the camera
// code compares with GetFocus (0x0045ABD4), or the one that the game keeps
// when it makes the window (0x007DBB61). Null before the window is there.
const uintptr_t kGameWindow = 0x00E85078;
const uintptr_t kGameWindowMade = 0x00E8C0F4;
HWND GameWindowHandle() {
  HWND window = *reinterpret_cast<const HWND*>(kGameWindow);
  if (window == nullptr) window = *reinterpret_cast<const HWND*>(kGameWindowMade);
  return window != nullptr && IsWindow(window) ? window : nullptr;
}

// Whether a hand can act as a mouse now: the option is on, the owner mode
// allows it, the game is in front or the headset session stands for it, a
// scene is drawn, and the hand is bound. Menus, loading screens and videos
// are left to the mouse.
bool Usable() {
  // The intro ends on a click. See intro_vr.cpp.
  if (IntroBlocksButtons()) return false;
  g_not_usable[0] = !GameInFront() && !ActsBehind();
  g_not_usable[1] = StereoIdle();
  g_not_usable[2] = GetTickCount64() - g_frame_tick > 250;
  g_not_usable[3] = !g_hands[PointingIndex()].active;
  if (!g_on || !g_installed || g_owner_mode == 2) return false;
  return !(g_not_usable[0] || g_not_usable[1] || g_not_usable[2] ||
           g_not_usable[3]);
}

// With the direct hand, the pixel that counts is the Hand's own point.
bool InView() {
  if (DirectHandNow()) {
    return g_point.have_hand_pixel && !g_point.hand_off_view;
  }
  return g_point.have_pixel && !g_point.off_view;
}

// The trigger or the grip that makes a game button, as the pump last saw it.
bool RawDown(int button) {
  if (button == kLeft) return g_swap_buttons ? g_grip_down : g_trigger_down;
  return g_swap_buttons ? g_trigger_down : g_grip_down;
}

// The Vive trackpad reports a position whenever it is touched, so a resting
// thumb would turn and zoom. That profile needs the click.
bool StickLive(const bwvr::HandInput& hand) {
  return strstr(g_point.profile, "vive") == nullptr || hand.stick_click;
}

void PulseWith(int index, float amplitude, float seconds) {
  if (!g_haptics) return;
  ++g_pulses;
  if (g_fake.mode != 0) {
    if (g_event_logs < kMaxEventLogs) {
      ++g_event_logs;
      ModLog("controllers  : haptic pulse, %.0f ms at %.2f, frame %llu",
             seconds * 1000.0f, amplitude, g_frame);
    }
  } else if (g_screen != nullptr) {
    g_screen->Haptic(index, amplitude, seconds);
  }
}

void Pulse(int index) { PulseWith(index, 0.25f, 0.015f); }

// THE GAME'S OWN HAPTICS. GInterface::StartImmersion 0x005D8A10 starts a
// force feedback effect of the Immersion glove and mouse. The game calls it
// with an effect index from 0 to 0x2F, thiscall (int effect, unsigned int),
// ret 8. The hook sits at the entry, before the gate that tests for the
// Immersion device, and always calls the original. Then it sends a short
// pulse to the hand that holds the pointer.
//
// The names are from the callers (docs/hand-model.md, "Haptics"). The other
// indices get a mild pulse, and the first start of each one is logged so it can be
// named later.
const int kEffectCount = 0x30;
struct EffectPulse {
  float amplitude;
  float seconds;
  int gap_ms;  // the least time between two pulses of this effect
  const char* name;
};
const EffectPulse kDefaultEffect = {0.30f, 0.040f, 80, "not named yet"};
EffectPulse EffectFor(int effect) {
  switch (effect) {
    case 0: return {0.15f, 0.030f, 250, "touch the Creature"};
    case 1: return {0.60f, 0.060f, 100, "soft slap"};
    case 2: return {1.00f, 0.100f, 100, "hard slap"};
    case 3: return {0.70f, 0.120f, 100, "gesture success"};
    // The names of 4, 5, 6, 44 and 46 are from when they start in a desktop
    // run, not from the callers.
    case 4: return {0.35f, 0.050f, 100, "a pick up starts"};
    case 5: return {0.60f, 0.080f, 100, "the object is in the Hand"};
    case 6: return {0.15f, 0.020f, 200, "the Hand is over an object"};
    case 7: return {0.30f, 0.040f, 80, "the leash goes on"};
    // The game's own names of 8, 9, 14 and 18 to 28 are in its enum
    // IMMERSION_EFFECT_TYPE (docs/miracles.md, "The Immersion effects").
    case 8: return {0.50f, 0.150f, 150, "the trail of a gesture"};
    case 9: return {0.50f, 0.150f, 150, "a miracle crackles in the Hand"};
    case 14: return {0.30f, 0.040f, 80, "a one-off seed pops"};
    case 18: case 19: case 20:
      return {0.30f, 0.040f, 80, "a fireball is cast"};
    case 21: return {0.30f, 0.040f, 80, "lightning is cast"};
    case 23: return {0.30f, 0.040f, 80, "a miracle is cast"};
    case 24: return {0.30f, 0.040f, 80, "a shield is cast"};
    case 25: return {0.30f, 0.040f, 80, "water pours"};
    case 26: case 27: return {0.30f, 0.040f, 80, "a flock is cast"};
    case 28: return {0.30f, 0.040f, 80, "a Creature miracle is cast"};
    case 44: return {0.30f, 0.040f, 80, "a menu click"};
    case 46: return {0.40f, 0.070f, 100, "the release"};
    default: return kDefaultEffect;
  }
}
const uintptr_t kStartImmersion = 0x005D8A10;
typedef void(__fastcall* StartImmersionFn)(void* self, void* unused,
                                           int effect, unsigned int arg2);
StartImmersionFn o_StartImmersion = nullptr;
bool g_effect_seen[kEffectCount] = {false};
ULONGLONG g_effect_last[kEffectCount] = {0};
bool g_haptics_hooked = false;

void __fastcall h_StartImmersion(void* self, void* unused, int effect,
                                 unsigned int arg2) {
  o_StartImmersion(self, unused, effect, arg2);
  if (effect < 0 || effect >= kEffectCount) return;
  const EffectPulse pulse = EffectFor(effect);
  if (!g_effect_seen[effect]) {
    g_effect_seen[effect] = true;
    ModLog("haptics      : effect %d (%s) started for the first time, "
           "argument 0x%08X, %s owns the pointer, frame %llu", effect,
           pulse.name, arg2, g_pointer_owned ? "the controller" : "the mouse",
           g_frame);
  }
  if (!g_on || !g_haptics || !g_pointer_owned) return;
  const ULONGLONG now = GetTickCount64();
  if (now - g_effect_last[effect] < static_cast<ULONGLONG>(pulse.gap_ms)) {
    return;
  }
  g_effect_last[effect] = now;
  // The kind of the effect gives the pulse (haptics.cpp).
  HapticsGameEffect(effect);
}

// The event a controller press sends: a plain press, or the double click flag
// when the press follows another one the way Windows counts it. That needs the
// double click time and a small move of the aim. After a double click, the
// next press is a plain one again.
//
// With the direct hand the aim says nothing about where the Hand is. A short
// ratchet of land drags with a still wrist would read as a double click, and
// a double click on the land is a jump. So there the trigger and the grip
// only make plain presses, and the A button makes the jump.
bool AimNear(const float from[3], float degrees) {
  if (!g_point.have_world) return false;
  float along = from[0] * g_point.direction[0] + from[1] * g_point.direction[1] +
                from[2] * g_point.direction[2];
  if (along > 1.0f) along = 1.0f;
  if (along < -1.0f) along = -1.0f;
  return acosf(along) * 57.29578f < degrees;
}

uint8_t PressFlagFor(int button, ULONGLONG now) {
  Button& b = g_button[button];
  const bool forced = button == kLeft && g_force_double;
  if (forced) g_force_double = false;
  if (!forced && DirectHandNow()) {
    b.have_last = false;
    return kPressFlag[button];
  }
  if (forced || (b.have_last && now - b.last_press <= GetDoubleClickTime() &&
                 AimNear(b.last_direction, 1.5f))) {
    b.have_last = false;
    ++g_doubles_sent;
    return kDoubleFlag[button];
  }
  b.have_last = true;
  b.last_press = now;
  for (int i = 0; i < 3; ++i) b.last_direction[i] = g_point.direction[i];
  return kPressFlag[button];
}

// Tell the game about a change of one button, with the original function.
void SendToGame(int button, uint8_t flag) {
  o_SetButtons(reinterpret_cast<void*>(kMouseObject), nullptr, flag);
  if (flag == kReleaseFlag[button]) {
    g_game_mask &= static_cast<uint8_t>(~(1u << button));
  } else {
    g_game_mask |= static_cast<uint8_t>(1u << button);
  }
}

// Send what the union of the two sources needs, and no more. Only the
// controller buttons come here. The real mouse goes through h_SetButtons.
// A quiet press is the click of an opening grip: it gets no pulse, because
// the hand let go and pressed nothing.
void ReconcileController(int button, ULONGLONG now, bool quiet = false) {
  const bool want = (((g_mouse_mask | g_ctrl_mask) >> button) & 1) != 0;
  const bool have = ((g_game_mask >> button) & 1) != 0;
  if (want == have) return;
  g_button_event = true;
  if (want) {
    const uint8_t flag = PressFlagFor(button, now);
    SendToGame(button, flag);
    ++g_presses_sent;
    if (button == kLeft) TextClickSent();
    if (!quiet) Pulse(PointingIndex());
    if (g_event_logs < kMaxEventLogs) {
      ++g_event_logs;
      ModLog("controllers  : the %s button is down%s, frame %llu",
             kButtonName[button],
             flag == kDoubleFlag[button] ? " (double click)"
             : quiet                     ? " (the click of the open grip)"
                                         : "",
             g_frame);
    }
  } else {
    SendToGame(button, kReleaseFlag[button]);
    ++g_releases_sent;
    if (g_event_logs < kMaxEventLogs) {
      ++g_event_logs;
      ModLog("controllers  : the %s button is up, frame %llu",
             kButtonName[button], g_frame);
    }
  }
}

// Let go of everything the controller holds. A button that is still down must
// go up and down again to press once more.
void ReleaseController(const char* why) {
  if (g_ctrl_mask == 0) return;
  ++g_forced_releases;
  if (g_event_logs < kMaxEventLogs) {
    ++g_event_logs;
    ModLog("controllers  : the controller lets go of its buttons (%s), frame "
           "%llu", why, g_frame);
  }
  for (int b = 0; b < 2; ++b) {
    if (((g_ctrl_mask >> b) & 1) != 0 && RawDown(b)) g_button[b].need_up = true;
  }
  g_ctrl_mask = 0;
  g_jump_control = false;
  g_jump_stage = 0;
  g_force_double = false;
}

// --- the grab of the world ---------------------------------------------------
//
// THE BUTTONS. The other hand's grip grabs the world. It is no mouse button,
// so it takes nothing from pick up, the land drag, casting or the Creature,
// and it works while the pointing hand holds an object. With both grips, the
// pointing grip joins the grab, but only when the Hand is empty and over
// nothing it can hold. Then the game never gets that press. With an object
// under the Hand, the pointing grip stays the game's pick up. With
// grab_hand=2 the pointing grip can also start a grab alone, by the same rule.
// See docs/god-view.md, "5.2 The buttons of the grab".
//
// While a grab holds, the trigger and any new left press are held back from
// the game: a land drag would fight the camera. A trigger that is already
// down when a grip closes stops the grab from starting. One thing at a time.
//
// A grab starts only on a press of a grip, never on a grip that is already
// closed, so a hand that rests half closed on the controller cannot start
// one. The camera side is world_grab.cpp.
GrabSettings g_grab;
bool g_off_level = false;    // the other hand's grip, past the grab levels
bool g_off_before = false;   // the same at the last pump
bool g_off_engaged = false;  // the other hand's grip holds the world
bool g_other_active = false; // the other hand was active at the last pump
bool g_grip_before = false;  // the pointing grip's game level at the last pump
bool g_grip_claimed = false; // the pointing grip holds the world
// The grip points of both hands in the room, 0 the other hand and 1 the
// pointing hand. An untracked grip keeps its last tracked point.
float g_grab_room[2][3];
bool g_grab_have[2] = {false, false};
bool g_grab_tracked[2] = {false, false};
bool g_grab_seen_tracked[2] = {false, false};
float g_grab_head[3] = {0.0f, 0.0f, 0.0f};
bool g_grab_have_head = false;
double g_grab_time = 0.0;
uint64_t g_grab_frame = 0;
// For the report.
uint64_t g_grab_starts = 0;
uint64_t g_grab_claims = 0;
uint64_t g_grab_refused = 0;
uint64_t g_grab_picks = 0;
uint64_t g_grab_trigger_held = 0;
uint64_t g_grab_stick_refused = 0;
int g_grab_logs = 0;
const int kMaxGrabLogs = 120;

int OtherIndex() { return 1 - PointingIndex(); }

// The trigger's and the grip's game buttons, after controller_swap_buttons.
int TriggerButton() { return g_swap_buttons ? kRight : kLeft; }
int GripButton() { return g_swap_buttons ? kLeft : kRight; }

bool GrabHeld() { return g_off_engaged || g_grip_claimed; }

bool GrabLogRoom() {
  if (g_grab_logs >= kMaxGrabLogs) return false;
  ++g_grab_logs;
  return true;
}

double NowSeconds() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return frequency.QuadPart > 0
             ? static_cast<double>(count.QuadPart) / frequency.QuadPart
             : GetTickCount64() / 1000.0;
}

// The other hand's grab level: its squeeze. A profile with no squeeze bound
// (khr/simple_controller) grabs with its select, which is the trigger here.
float OtherGrabLevel(const bwvr::HandInput& hand) {
  if (g_fake.mode == 0 && g_screen != nullptr &&
      strstr(g_screen->HandProfile(OtherIndex()), "simple") != nullptr) {
    return hand.trigger;
  }
  return hand.squeeze;
}

// Let go of the world. A pointing grip that is still closed must open before
// it can press the game's button, because the game never had its press.
void LetGoOfWorld(const char* why) {
  if (!GrabHeld()) return;
  if (GrabLogRoom()) {
    ModLog("grab         : the hands let go of the world (%s), frame %llu", why,
           g_frame);
  }
  g_off_engaged = false;
  if (g_grip_claimed) {
    g_grip_claimed = false;
    if (g_grip_down) g_button[GripButton()].need_up = true;
  }
}

// Whether a grab may hold now. why says the first reason for a no.
bool GrabAllowed(bool usable, const char** why) {
  if (!g_grab.on || g_grab.hand <= 0) {
    *why = "the grab is off";
    return false;
  }
  if (!usable) {
    *why = "the hand cannot act now, for example in a menu";
    return false;
  }
  if (!g_pointer_owned) {
    *why = "the mouse owns the pointer";
    return false;
  }
  if (!WorldGrabCameraReady()) {
    *why = "the camera is not the player's now";
    return false;
  }
  return true;
}

// A trigger or a left button that is down stops a grab from starting.
bool TriggerBlocks(const char** why) {
  if (RawDown(TriggerButton())) {
    *why = "the trigger is down";
    return true;
  }
  if (((g_game_mask | g_mouse_mask | g_ctrl_mask) & 1) != 0) {
    *why = "the game holds the left button";
    return true;
  }
  if (g_jump_control) {
    *why = "a jump is running";
    return true;
  }
  // The hand moves over the ring to pick an item. A grip that closes then
  // must not pull the land with it.
  if (RadialMenuOpen()) {
    *why = "the ring is open";
    return true;
  }
  return false;
}

// Once a pump, after the pointing hand's levels. The hands are latched once a
// frame, so an edge is seen once.
void UpdateGrab(bool usable, bool back, bool far_claim) {
  const int other_index = OtherIndex();
  const bwvr::HandInput& other = g_hands[other_index];
  const float level = OtherGrabLevel(other);
  const float press = g_grab.press_pct * 0.01f;
  const float release = g_grab.release_pct * 0.01f;
  g_off_level = other.active && (g_off_level ? level > release : level > press);
  // A grip that closed while a hand could not act is not a new press.
  const bool other_back = other.active && !g_other_active;
  g_other_active = other.active;
  const bool off_edge = g_off_level && !g_off_before && !other_back;
  g_off_before = g_off_level;
  const bool grip_edge = g_grip_down && !g_grip_before && !back;
  g_grip_before = g_grip_down;

  const char* why = "";
  const bool allowed = GrabAllowed(usable, &why);

  // The ends come first.
  if (g_off_engaged && !g_off_level) {
    g_off_engaged = false;
    if (GrabLogRoom()) {
      ModLog("grab         : the other hand's grip opens, frame %llu", g_frame);
    }
  }
  if (g_grip_claimed && !g_grip_down) {
    g_grip_claimed = false;
    if (GrabLogRoom()) {
      ModLog("grab         : the pointing grip opens, frame %llu", g_frame);
    }
  }
  if (GrabHeld() && !allowed) LetGoOfWorld(why);

  // Then the starts.
  if (off_edge && !g_off_engaged) {
    const char* blocked = "";
    if (!allowed) {
      ++g_grab_refused;
      if (GrabLogRoom()) {
        ModLog("grab         : the other hand's grip does not grab: %s, frame "
               "%llu", why, g_frame);
      }
    } else if (TriggerBlocks(&blocked)) {
      ++g_grab_refused;
      if (GrabLogRoom()) {
        ModLog("grab         : the other hand's grip does not grab: %s, frame "
               "%llu", blocked, g_frame);
      }
    } else {
      g_off_engaged = true;
      ++g_grab_starts;
      if (GrabLogRoom()) {
        ModLog("grab         : the other hand's grip grabs the world, frame "
               "%llu", g_frame);
      }
      HapticsPulseHands(1, haptics_math::kWorld);
    }
  }
  if (grip_edge && !g_grip_claimed && allowed && !far_claim &&
      (g_grab.hand >= 2 || (g_off_engaged && g_grab.two_hands))) {
    const char* blocked = "";
    const char* hand_why = "";
    bool claim = false;
    if (TriggerBlocks(&blocked)) {
      // The reason is in blocked.
    } else if (((g_game_mask | g_mouse_mask) & 3) != 0) {
      blocked = "the game holds a button";
    } else if (!HandDirectClaimFree(&hand_why)) {
      blocked = hand_why;
    } else {
      claim = true;
    }
    if (claim) {
      g_grip_claimed = true;
      ++g_grab_claims;
      if (GrabLogRoom()) {
        ModLog("grab         : the pointing grip grabs the world, not a button "
               "(%s), frame %llu", hand_why, g_frame);
      }
      HapticsPulseHands(2, haptics_math::kWorld);
    } else {
      ++g_grab_picks;
      if (GrabLogRoom()) {
        ModLog("grab         : the pointing grip stays the game's button (%s), "
               "frame %llu", blocked, g_frame);
      }
    }
  }
}

// The grip points of both hands in the room, once a frame, for world_grab.cpp.
void GrabSamples() {
  g_grab_have_head = HeadPoseHeadInRoom(g_grab_head);
  for (int slot = 0; slot < 2; ++slot) {
    const int index = slot == 0 ? OtherIndex() : PointingIndex();
    const bwvr::HandInput& hand = g_hands[index];
    const bool grip_pose = hand.grip_valid;
    if (!hand.active || !(grip_pose || hand.aim_valid)) {
      g_grab_have[slot] = false;
      continue;
    }
    const bool reported = grip_pose ? hand.grip_tracked : hand.aim_tracked;
    const bool tracked = Tracked(reported, &g_grab_seen_tracked[slot]);
    float m[9], t[3];
    if (!HeadPoseToRoom(grip_pose ? hand.grip_position : hand.aim_position,
                        grip_pose ? hand.grip_orientation
                                  : hand.aim_orientation,
                        m, t)) {
      g_grab_have[slot] = false;
      continue;
    }
    if (tracked || !g_grab_have[slot]) {
      for (int i = 0; i < 3; ++i) g_grab_room[slot][i] = t[i];
    }
    g_grab_tracked[slot] = tracked;
    g_grab_have[slot] = true;
  }
  // The time of the sample, for the smoothing of the grips: the display time
  // of the frame of the headset with god_display_time=1, so each frame eases
  // the grips by the time between two pictures. The time of the CPU changes
  // by some milliseconds from frame to frame, and a fast grab then judders.
  static int display_time = -1;
  if (display_time < 0) display_time = IniInt("god_display_time", 1) != 0;
  const double display = display_time != 0 ? ModDisplaySeconds() : 0.0;
  g_grab_time = display > 0.0 ? display : NowSeconds();
  g_grab_frame = g_frame;
}

// --- the draw of a gesture ---------------------------------------------------
//
// THE BUTTON. gesture_button draws: B by default, or Y on the left hand. While
// it is held, gesture_air.cpp puts the grip point on a plane and gives the
// game the path. A press needs the hand to act as a mouse, and no grab of the
// world. The press itself gives the controller the pointer, so the draw waits
// for the owner rule. A draw that stops while the button stays down needs a
// new press, as the game's buttons do. The button is no mouse button, so it
// takes nothing from pick up, the land drag or the grab.
GestureSettings g_gesture;
bool g_gesture_down = false;    // the button on this frame
bool g_gesture_before = false;  // and on the last frame
bool g_gesture_need_up = false;
bool g_gesture_held = false;    // the draw may run on this frame
const char* g_gesture_why = "the button is up";

bool GestureButtonDown(const bwvr::HandInput& hand) {
  if (g_gesture.button == 1) return hand.secondary;
  if (g_gesture.button == 2) return hand.stick_click;
  return false;
}

// Once a frame, from ControllersFrame, after the latches of the pump.
void LatchGesture(bool back) {
  const bwvr::HandInput& hand = g_hands[PointingIndex()];
  const bool down = hand.active && GestureButtonDown(hand);
  const bool press = down && !g_gesture_before;
  g_gesture_before = down;
  g_gesture_down = down;
  if (!down) g_gesture_need_up = false;
  // While A holds a teleport, B cancels it and starts no gesture.
  if (down && !g_gesture_held && hand.primary && TeleportOwnsA()) {
    g_gesture_need_up = true;
  }
  const bool usable = Usable();
  if (down && (back || (press && (!usable || GrabHeld())))) {
    g_gesture_need_up = true;
  }
  const char* why = nullptr;
  if (g_gesture.button == 0) {
    why = "the draw button is off";
  } else if (!down) {
    why = "the button is up";
  } else if (g_gesture_need_up) {
    why = "the button must go up before it draws again";
  } else if (!usable) {
    why = "the hand cannot act now, for example in a menu";
  } else if (GrabHeld()) {
    why = "a grip holds the world";
  } else if (!g_pointer_owned) {
    why = "the mouse owns the pointer";
  } else if (!g_point.have_grip || !g_point.have_world) {
    why = "the grip is not known";
  }
  const bool held = why == nullptr;
  if (g_gesture_held && !held && down) g_gesture_need_up = true;
  g_gesture_held = held;
  g_gesture_why = held ? "the button is down" : why;
}

// --- the layout of the buttons ---------------------------------------------
//
// THE LAYOUT OF THE BUTTONS of the hand that does not point (button_layout,
// docs/controllers.md, "The layout of the other hand").
//
//   1  The menu button of the left controller opens the game menu. The
//      second face button (Y, or B when the left hand points) opens the ring,
//      and the first one (X, or A) has no job. The stick does not drive the
//      ring, so it can move the player (PumpWalk).
//   0  The second face button opens the game menu, and so does the menu
//      button of the left controller. The first face button opens the ring,
//      and the stick can drive the ring (radial_menu_stick).
//
// With test_keys=1, Ctrl and the Menu key of the keyboard change it while
// the game runs (PollLayoutKeys).
int g_layout = -1;

void LogLayout(const char* by) {
  const bool keys = IniInt("test_keys", 0) != 0;
  ModLog("controllers  : %s%sbutton_layout=%d. %s%s", by,
         by[0] != 0 ? ": " : "", g_layout,
         g_layout != 0
             ? "The menu button of the left controller opens the game menu. "
               "On the hand that does not point, the second face button "
               "opens the ring (Y, or B when the left hand points), the "
               "first face button has no job, and the stick does not drive "
               "the ring"
             : "On the hand that does not point, the second face button "
               "opens the game menu (Y, or B when the left hand points), and "
               "so does the menu button of the left controller. The first "
               "face button opens the ring, and the stick can drive the ring",
         keys ? ". Ctrl and the Menu key change it" : "");
}

int ButtonLayout() {
  if (g_layout < 0) {
    g_layout = IniInt("button_layout", 1) != 0 ? 1 : 0;
    LogLayout("");
  }
  return g_layout;
}

// The profile of a hand: "fake" for the scripted controller, and empty until
// the runtime tells it.
const char* HandProfileOf(int index) {
  if (g_fake.mode != 0) return "fake";
  return g_screen != nullptr ? g_screen->HandProfile(index) : "";
}

// --- the menu button (Esc) -------------------------------------------------
//
// The menu button of the left controller sends the game's Esc key. With
// button_layout=1 it is the one button of the game menu. A Quest gives a
// program no menu button of the right controller, so a left handed player
// has the same button: it is on the controller of the pointing hand then. An
// Index controller has no menu button that a program gets. There the first
// face button of the hand that does not point (A) sends Esc, because it has
// no other job.
//
// With button_layout=0 the second face button of the hand that does not
// point sends Esc too: Y on the left controller, or B on the right
// controller of a left handed player. It never clashes with the draw button
// or with the B of the teleport, which are on the pointing hand.
//
// THE KEY. The game reads the keyboard only from window messages: the window
// procedure passes WM_KEYDOWN and WM_KEYUP to LHKeyboard::ProcessKeyboard
// 0x007DCAF0, which keeps one byte for each scan code. So the pump sends the
// two messages to the game window, with scan code 1 (Esc). One press is one
// Esc: the key goes down once, stays down for kEscHoldMs so that a frame
// sees it, and goes up. A new press while the key is down is dropped.
// The mouse and the keyboard are not changed.
const WPARAM kEscVk = VK_ESCAPE;
const LPARAM kEscDownParam = 0x00010001;  // repeat 1, scan code 1
const LPARAM kEscUpParam = static_cast<LPARAM>(0xC0010001u);
const ULONGLONG kEscHoldMs = 60;
bool g_esc_before = false;       // the button was down on the last frame
bool g_esc_prev_active = false;  // a hand that has the button was active
bool g_esc_event = false;        // a new press, for the pump
int g_esc_stage = 0;             // 1 while the key is down
ULONGLONG g_esc_up_at = 0;
HWND g_esc_window = nullptr;
uint64_t g_esc_sent = 0;
int g_esc_logs = 0;
const int kMaxEscLogs = 40;
bool g_box_before = false;
// The first face button of the hand that does not point was down on the last
// frame, while it has no job (button_layout=1).
bool g_idle_before = false;
int g_idle_logs = 0;
const int kMaxIdleLogs = 20;

// Once a frame, from ControllersFrame, so an edge is seen once. A button that
// is already down when a hand wakes is not a new press.
void LatchEsc() {
  const bwvr::HandInput& left = g_hands[0];
  const bwvr::HandInput& free_hand = g_hands[OtherIndex()];
  const bool active = left.active || free_hand.active;
  // The face button of the game menu, on the hand that does not point.
  const bool index =
      strstr(HandProfileOf(OtherIndex()), "index") != nullptr;
  bool face = false;
  if (ButtonLayout() == 0) {
    face = free_hand.secondary;
  } else if (index) {
    face = free_hand.primary;
  }
  const bool down = (left.active && left.menu) || (free_hand.active && face);
  if (down && !g_esc_before && g_esc_prev_active && active) {
    g_esc_event = true;
  }
  g_esc_before = down;
  g_esc_prev_active = active;
  // The first face button has no job with button_layout=1. The log says so
  // for a press, so a log from a headset session shows why nothing happened.
  const bool idle = ButtonLayout() != 0 && !index && free_hand.active &&
                    free_hand.primary;
  if (idle && !g_idle_before && g_idle_logs < kMaxIdleLogs) {
    ++g_idle_logs;
    ModLog("controllers  : the first face button of the %s hand is down. It "
           "has no job with button_layout=1, frame %llu",
           OtherIndex() == 0 ? "left" : "right", g_frame);
  }
  g_idle_before = idle;
}

void SendEsc(bool down) {
  SendMessageA(g_esc_window, down ? WM_KEYDOWN : WM_KEYUP, kEscVk,
               down ? kEscDownParam : kEscUpParam);
}

// Each pump. It only compares what it wants with what it sent, so a second
// run in one frame does nothing.
void PumpEsc(ULONGLONG now) {
  const bool box = MenuUp();
  if (box != g_box_before) {
    g_box_before = box;
    if (g_esc_logs < kMaxEscLogs) {
      ++g_esc_logs;
      ModLog("controllers  : a dialog box is %s now, frame %llu",
             box ? "up" : "closed", g_frame);
    }
  }
  if (g_esc_stage == 1 && now >= g_esc_up_at) {
    SendEsc(false);
    g_esc_stage = 0;
  }
  if (!g_esc_event) return;
  g_esc_event = false;
  if (g_esc_stage != 0) return;
  // The key goes to the window of the game, and SendMessageA calls the
  // window procedure in the process. So it also gets there while the window
  // is behind, and no other program gets it.
  const bool front = GameInFront();
  g_esc_window = GameWindowHandle();
  if (g_esc_window == nullptr && front) g_esc_window = GetForegroundWindow();
  if ((!front && !ActsBehind()) || g_esc_window == nullptr) {
    if (g_esc_logs < kMaxEscLogs) {
      ++g_esc_logs;
      ModLog("controllers  : the menu button is dropped, %s, frame %llu",
             g_esc_window == nullptr
                 ? "the game has no window"
                 : "the game window is behind and the headset session does "
                   "not stand for it",
             g_frame);
    }
    return;
  }
  SendEsc(true);
  g_esc_stage = 1;
  g_esc_up_at = now + kEscHoldMs;
  ++g_esc_sent;
  if (g_esc_logs < kMaxEscLogs) {
    ++g_esc_logs;
    ModLog("controllers  : the menu button sends Esc, a dialog box is %s, "
           "frame %llu", box ? "up" : "closed", g_frame);
  }
}

}  // namespace

// True while a controller button holds a mouse button in the game. Then the
// game's own held bits are the union of both sources. pointer_map.cpp reads
// the real mouse directly the rest of the time, as it always did.
bool ControllersHoldButtons() { return g_ctrl_mask != 0; }

// True while the grip carries an object with no game button, or clicks to let
// it go (docs/grab.md, "The grip holds"). The owner rule of pointer_map.cpp
// keeps the pointer with the controller then, as for a held button. It does
// not change how the real mouse buttons reach the game.
// For miracle.cpp: the second face button of the pointing hand is down on
// this frame (B, or Y on the left hand), and the hand can act.
bool ControllersSecondButton() {
  if (!g_on) return false;
  const bwvr::HandInput& hand = g_hands[PointingIndex()];
  return hand.active && hand.secondary && Usable();
}

bool ControllersGripCarries() {
  return g_on && DirectMode() &&
         (g_hold.phase == controller_math::kGripCarry ||
          g_hold.phase == controller_math::kGripLetGo);
}

// For forest_take.cpp: the control of the game's right button is closed now,
// as the pump last saw it, and the hand can act.
bool ControllersGripClosed() { return g_on && RawDown(kRight) && Usable(); }

namespace {

// Real button events, one at a time, from the window procedure.
void __fastcall h_SetButtons(void* self, void* unused, int flag) {
  const uint8_t f = static_cast<uint8_t>(flag);
  // The pump did not run for a while, so a loop with its own messages has the
  // game. A button that nobody watches must not stay down.
  if (g_ctrl_mask != 0 && GetTickCount64() - g_last_pump > kWatchdogMs) {
    ReleaseController("the pump stopped");
    const ULONGLONG now = GetTickCount64();
    for (int b = 0; b < 2; ++b) ReconcileController(b, now);
  }
  int button = -1;
  bool press = false;
  for (int b = 0; b < 3; ++b) {
    if (f == kPressFlag[b] || (f != 0 && f == kDoubleFlag[b])) {
      button = b;
      press = true;
    } else if (f == kReleaseFlag[b]) {
      button = b;
    }
  }
  if (button < 0) {
    o_SetButtons(self, unused, flag);
    return;
  }
  const uint8_t bit = static_cast<uint8_t>(1u << button);
  // The window lost focus, and the game lets go of every button. So does the
  // controller, and its next press has to be a new one. Not while the
  // headset session stands for the front: the release is then the real
  // mouse's only, and a button that the controller holds stays down.
  if (!press && !WindowActive() && !ActsBehind()) {
    if (button < 2 && (g_ctrl_mask & bit) != 0 && RawDown(button)) {
      g_button[button].need_up = true;
    }
    g_ctrl_mask &= static_cast<uint8_t>(~bit);
    g_mouse_mask &= static_cast<uint8_t>(~bit);
    g_game_mask &= static_cast<uint8_t>(~bit);
    o_SetButtons(self, unused, flag);
    return;
  }
  if (press) {
    g_mouse_mask |= bit;
    g_mouse_event = true;
  } else {
    g_mouse_mask &= static_cast<uint8_t>(~bit);
  }
  // While the controller holds no button, the real mouse reaches the game
  // exactly as the plain game: every event, even one that repeats
  // a state. A release can happen outside the window and never arrive, and
  // the game then needs the next press, or the double click after it.
  if (g_ctrl_mask == 0) {
    if (press) {
      g_game_mask |= bit;
    } else {
      g_game_mask &= static_cast<uint8_t>(~bit);
    }
    o_SetButtons(self, unused, flag);
    return;
  }
  const bool want = ((g_mouse_mask | g_ctrl_mask) & bit) != 0;
  const bool have = (g_game_mask & bit) != 0;
  if (want == have) return;  // the other source holds it, or has let go
  if (want) {
    g_game_mask |= bit;
  } else {
    g_game_mask &= static_cast<uint8_t>(~bit);
  }
  o_SetButtons(self, unused, flag);
}

bool HoldLogRoom() {
  if (g_hold_logs >= kMaxHoldLogs) return false;
  ++g_hold_logs;
  return true;
}

// THE GRIP HOLDS: one step of GripStep, once a pump, before the buttons. Only
// with the direct hand. The facts come from the Hand of the last frame, read
// now. A grip that opens latches the throw at once.
controller_math::GripStepOut StepGrip(bool usable, bool far_claim) {
  using namespace controller_math;
  GripFacts facts = {};
  bool holding = false, settled = false, spring = false, take = false;
  facts.direct = usable && g_pointer_owned &&
                 HandDirectGripFacts(&holding, &settled, &spring, &take);
  facts.holding = holding;
  facts.settled = settled;
  facts.spring = spring;
  // THE BULK TAKE: while the game takes from a source, the grip is the
  // button, and the opening ends the take.
  facts.take = take;
  // MANY TREES FROM A FOREST: the mod's own take needs no button.
  facts.mod_take = facts.direct && HandDirectModTake();
  // Both buttons together are the game's camera mode, not a drop.
  facts.other_held = RawDown(kLeft) || (g_game_mask & 1) != 0;
  // THE PRESS OF THE GAME (grip_press_hold): the game has the object in its
  // Hand, the object is in the physics of the game, and a release now is a
  // click for the game.
  bool game_holds = false, moving = false;
  uint32_t press_age = 0;
  const bool have_press =
      HandDirectGripPress(&game_holds, &moving, &press_age);
  facts.game_holds = game_holds;
  facts.moving = have_press && moving;
  facts.click = have_press && GameClick(press_age);
  facts.press_hold = g_press_hold != 0;
  // A grip that the grab from a distance holds back looks open to the machine.
  // While the mod lets a one-off seed go to make it active, the grip looks
  // open too, until the real grip opens (miracle.cpp). The call comes first,
  // on each pump, because an open grip ends the mask.
  const bool masked = MiracleGripMasked(RawDown(kRight));
  const bool grip = RawDown(kRight) && !far_claim && !masked;
  // Each step of one frame has the time of the first pump of that frame.
  if (g_grip_ms_frame != g_frame) {
    g_grip_ms_frame = g_frame;
    g_grip_ms = static_cast<uint64_t>(NowSeconds() * 1000.0);
  }
  const GripStepOut out = GripStep(&g_hold, grip, facts, g_frame, g_grip_ms);
  if (out.opened) HandDirectGripOpened();
  // How long the button was down for the game, for the log.
  if (out.button && !g_grip_button_was) g_grip_down_ms = g_grip_ms;
  const uint64_t down_ms = g_grip_ms - g_grip_down_ms;
  const bool went_up = !out.button && g_grip_button_was;
  g_grip_button_was = out.button;
  // Nothing collects after the grip opens: a take that still runs with the
  // button up is ended in the next interface step.
  if (out.end_take) HandDirectEndTake();
  if (out.take_opened) ++g_hold_take_opens;
  const bool new_end = out.end_take && !g_hold_end_take;
  g_hold_end_take = out.end_take;
  if (new_end) ++g_hold_take_ends;
  if ((out.take_opened || new_end) && HoldLogRoom()) {
    ModLog("grip         : %s, frame %llu",
           new_end ? "the bulk take still runs after the grip opened, so the "
                     "mod ends it"
           : facts.mod_take
                 ? "the grip opens during a take from a forest: the take "
                   "stops, and the bundle stays in the Hand"
                 : "the grip opens during a bulk take: the button goes up, "
                   "and the game ends the take",
           g_frame);
  }
  const int e = out.events;
  if ((e & kGripCarried) != 0) ++g_hold_carries;
  if ((e & kGripLettingGo) != 0) ++g_hold_let_go;
  if ((e & kGripLate) != 0) ++g_hold_late;
  if ((e & kGripClickDown) != 0) ++g_hold_clicks;
  if ((e & kGripRegrip) != 0) ++g_hold_regrips;
  // The wait shows once, when it starts.
  const bool waits = (e & kGripWaits) != 0;
  const bool new_wait = waits && !g_hold_waits;
  g_hold_waits = waits;
  if (new_wait) ++g_hold_waited;
  if ((e & kGripHeldOn) != 0) ++g_hold_held_on;
  // A PRESS THAT IS A CLICK FOR THE GAME: the button goes up while the pick
  // is locked, a release is a click and the physics of the game has the
  // object. The Hand of the game lets the object go, and a rock splits.
  if (went_up && facts.direct && facts.moving && facts.click &&
      !facts.settled) {
    ++g_hold_short;
    if (HoldLogRoom()) {
      ModLog("grip         : the button of the game goes up %llu ms after it "
             "went down, on an object that the physics of the game has. The "
             "press is %u ms old on the clock of the game, so it is a click: "
             "the Hand lets the object go, and the game taps it "
             "(grip_press_hold=%d), frame %llu", down_ms, press_age,
             g_press_hold, g_frame);
    }
  }
  const int shown = (e & ~kGripWaits) | (new_wait ? kGripWaits : 0);
  if (shown == 0 || !HoldLogRoom()) return out;
  static const char* const kName[12] = {
      "the pick up is over, the grip carries the object",
      "an object is in the Hand with the grip open",
      "the grip opens: let go",
      "the object came after the grip opened: let go",
      "the click to let go presses",
      "the click to let go releases",
      "the grip closes on the object again",
      "the object left the Hand another way",
      "the controller does not place the Hand now: the grip is the button",
      "the click waits until the other button is up",
      "the grip opened on an object that moves, and a release now is a click "
      "for the game: the button of the game stays down until it is no click",
      "the press that the mod held for the game is over"};
  for (int i = 0; i < 12; ++i) {
    if ((shown & (1 << i)) == 0) continue;
    // The carry, and the two lines of a press that the mod held, say how
    // long the button was down for the game.
    char press[160] = "";
    if (i == 0 || i == 10 || i == 11) {
      sprintf_s(press, ". The button of the game was down for %llu ms, and "
                       "the press is %u ms old on the clock of the game",
                down_ms, have_press ? press_age : 0u);
    }
    ModLog("grip         : %s (holding %d, pick %s, the game holds %d, the "
           "physics has the object %d, hand mode 0x17 %d)%s, frame %llu",
           kName[i], holding, settled ? "open" : "locked", game_holds,
           moving, spring, press, g_frame);
  }
  return out;
}

// One frame of the pointing hand's buttons, from the values ControllersFrame
// latched. It only compares what it wants with what it has sent.
// The control of a game button is down. In a room of the Temple the trigger
// and the grip are both the Action button (temple.cpp, temple_click). A
// dialog box keeps the plain buttons.
bool ButtonDown(int button) {
  if (!TempleRayOn() || MenuUp()) return RawDown(button);
  return temple_math::ButtonAsked(TempleClick(), button, g_trigger_down,
                                  g_grip_down, g_swap_buttons);
}

}  // namespace

// THE SWITCHES OF THE TWO TEST KEYS BELOW (debug_menu.h). The first pump of
// the buttons reads grip_press_guard and grip_press_hold. Before it nothing
// changes.
void ControllersPressGuardChange(const char* by) {
  if (g_press_guard < 0) return;
  g_press_guard = g_press_guard != 0 ? 0 : 1;
  ControllersGesturePulse(0.5f, g_press_guard != 0 ? 0.30f : 0.08f);
  ModLog("grip         : %s: grip_press_guard is %d now, "
         "frame %llu. %s", by, g_press_guard, g_frame,
         g_press_guard != 0
             ? "A dip of the squeeze in the first 300 ms of a press does not "
               "open the grip"
             : "A fast fall of the squeeze opens the grip at any time");
}
int ControllersPressGuardValue() {
  return g_installed && g_press_guard >= 0 ? g_press_guard : kDebugCannot;
}

void ControllersPressHoldChange(const char* by) {
  if (g_press_hold < 0) return;
  g_press_hold = g_press_hold != 0 ? 0 : 1;
  ControllersGesturePulse(0.5f, g_press_hold != 0 ? 0.30f : 0.08f);
  ModLog("grip         : %s: "
         "grip_press_hold is %d now, frame %llu. %s", by, g_press_hold, g_frame,
         g_press_hold != 0
             ? "The button of the game stays down until the pick up of the "
               "game is over, and a press on an object that moves is no "
               "click"
             : "The button of the game goes up 167 ms after the object came "
               "into the Hand of the picture");
}
int ControllersPressHoldValue() {
  return g_installed && g_press_hold >= 0 ? g_press_hold : kDebugCannot;
}

namespace {

// THE TEST KEY OF THE PRESS GUARD: Ctrl, Shift and Home (test_keys=1) turn
// grip_press_guard off and on while the game runs.
void PollPressGuardKey() {
  static int test_keys = -1;
  if (test_keys < 0) test_keys = IniInt("test_keys", 0) != 0 ? 1 : 0;
  if (test_keys == 0) return;
  const bool down = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool pressed = down && !g_press_guard_key_down;
  g_press_guard_key_down = down;
  if (!pressed || !GameInFront()) return;
  ControllersPressGuardChange("Ctrl, Shift and Home");
}

// THE TEST KEY OF THE PRESS OF THE GAME: Shift and the plus key of the number
// pad, with Ctrl up (test_keys=1), turn grip_press_hold off and on while the
// game runs.
void PollPressHoldKey() {
  static int test_keys = -1;
  if (test_keys < 0) test_keys = IniInt("test_keys", 0) != 0 ? 1 : 0;
  if (test_keys == 0) return;
  const bool down = (GetAsyncKeyState(VK_ADD) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0;
  const bool pressed = down && !g_press_hold_key_down;
  g_press_hold_key_down = down;
  if (!pressed || !GameInFront()) return;
  ControllersPressHoldChange("Shift and the plus key of the number pad");
}

void PumpButtons(ULONGLONG now) {
  const bwvr::HandInput& hand = g_hands[PointingIndex()];
  // THE GRIP OPENS AT ONCE. The control that is the game's right button with
  // the direct hand (the grip, or the trigger with controller_swap_buttons=1)
  // also opens on a fast fall of its value. The other control keeps the plain
  // hysteresis.
  if (g_fast_open < 0) {
    g_fast_open = IniInt("grip_fast_open", 1) != 0 ? 1 : 0;
    ModLog("grip         : grip_fast_open=%d. %s", g_fast_open,
           g_fast_open != 0
               ? "With the direct hand a closed grip opens below 35%, and "
                 "also when it falls by 30% in 3 frames to less than 80%"
               : "A closed grip opens only below 35%");
  }
  if (g_press_guard < 0) {
    g_press_guard = IniInt("grip_press_guard", 1) != 0 ? 1 : 0;
    ModLog("grip         : grip_press_guard=%d. %s", g_press_guard,
           g_press_guard != 0
               ? "In the first 300 ms of a press, before the Hand holds the "
                 "object, a closed grip opens only below 35%, so a dip of "
                 "the squeeze does not end the press early"
               : "A closed grip can open on a fast fall at any time");
  }
  if (g_press_hold < 0) {
    g_press_hold = IniInt("grip_press_hold", 1) != 0 ? 1 : 0;
    ModLog("grip         : grip_press_hold=%d. %s", g_press_hold,
           g_press_hold != 0
               ? "While the grip is closed, the button of the game stays "
                 "down until the game has the object in its Hand and the "
                 "pick is open. The game takes an object that moves (one "
                 "that its physics has) only after 225 ms of a held button, "
                 "and a shorter press is a click that lets the object go and "
                 "splits a rock. A grip that opens sooner on such an object "
                 "keeps the button down until the press is 240 ms old on the "
                 "clock of the game"
               : "The button of the game goes up when the pick is open, or "
                 "167 ms after the object came into the Hand of the picture. "
                 "That is less than the 225 ms that the game needs for an "
                 "object that moves");
  }
  PollPressGuardKey();
  PollPressHoldKey();
  const bool trigger_was = g_trigger_down, grip_was = g_grip_down;
  // The control of the game's right button, as the last pump left it.
  const bool right_was = g_swap_buttons ? trigger_was : grip_was;
  const bool guarded = controller_math::GripPressGuarded(
      g_press_guard != 0, right_was, g_hold.phase,
      now - g_right_close_ms);
  const bool fast =
      g_fast_open != 0 && HandDirectReleaseAtOnce() && DirectHandNow() &&
      !guarded;
  g_trigger_down = controller_math::GripLevelStep(
      &g_trigger_level, hand.trigger, fast && g_swap_buttons);
  g_grip_down = controller_math::GripLevelStep(&g_grip_level, hand.squeeze,
                                               fast && !g_swap_buttons);
  if (g_swap_buttons ? (!trigger_was && g_trigger_down)
                     : (!grip_was && g_grip_down)) {
    g_right_close_ms = now;
  }
  // THE GRIP IS FULLY OPEN: the value of the control of the right button is
  // at or below the level that opens a slow grip. The guard of a throw ends
  // here (hand_direct.cpp, catch_moving=3).
  if (!((g_swap_buttons ? hand.trigger : hand.squeeze) >
        controller_math::kGripOpenBelow)) {
    HandDirectGripFullyOpen();
  }
  {
    // Each opening of the right button's control, with the values of the
    // frames before it, so a log shows how the real hand opened.
    const bool opened = g_swap_buttons ? trigger_was && !g_trigger_down
                                       : grip_was && !g_grip_down;
    if (opened && DirectHandNow()) {
      const controller_math::GripLevel& level =
          g_swap_buttons ? g_trigger_level : g_grip_level;
      if (level.fast) ++g_fast_opens;
      else ++g_slow_opens;
      if (g_level_logs < kMaxLevelLogs) {
        ++g_level_logs;
        char trace[160] = "";
        size_t used = 0;
        for (int i = 0; i < level.count && used + 8 < sizeof(trace); ++i) {
          used += static_cast<size_t>(sprintf_s(
              trace + used, sizeof(trace) - used, " %.2f", level.trace[i]));
        }
        ModLog("grip         : the %s opens %s, frame %llu. Its values over "
               "the last %d frames:%s", g_swap_buttons ? "trigger" : "grip",
               level.fast ? "on a fast fall" : "below 35%", g_frame,
               level.count, trace);
      }
    }
  }

  // The pointing hand is active again: the session has input focus again,
  // or the controller woke. A focus loss reads every button as up, so a
  // trigger held through it looks like a new press now. A button that is
  // already down must go up before it can press.
  const bool back = g_hand_back;
  if (g_hand_back) {
    g_hand_back = false;
    for (int b = 0; b < 2; ++b) {
      if (RawDown(b)) g_button[b].need_up = true;
    }
  }

  const bool usable = Usable();
  if (!usable) ReleaseController("not usable now");
  // THE GRAB FROM A DISTANCE. A grip that closes on a far target is held back
  // from the grip machine and from the grab of the world until the pull, or
  // until it opens (far_grab.cpp). The fetch is a press with no pixel in view,
  // because after a pull the hand is near the head.
  bool far_claim = false;
  {
    const char* why = "";
    // A grip that is closed on the Creature needs only a hand that can hold
    // it (far_grab.cpp, PumpHold).
    const bool can_hold = usable && DirectHandNow();
    const bool ok = can_hold && ControllersFarAllowed(&why);
    far_claim = FarGrabPumpGrip(RawDown(kRight), ok, can_hold);
  }
  UpdateGrab(usable, back, far_claim);
  // THE CREATURE COMMAND. While the leash is on the Hand, a press of the
  // trigger commands the Creature and does not reach the game
  // (creature_command.cpp). Not with controller_swap_buttons=1, where the
  // trigger does the work of the grip.
  //
  // A TEXT THAT WAITS FOR A CLICK (text_click.cpp). The game ends that wait
  // only when its left button goes up. So while a text waits, a new press of
  // the trigger is no command and makes no seed active: it goes to the game.
  const bool click_waits = TextClickFlag();
  const bool click_on = TextClickOn();
  bool command_claim = false;
  {
    const bool ok = usable && DirectHandNow() && g_pointer_owned &&
                    !g_swap_buttons && !GrabHeld() && !far_claim &&
                    !g_jump_control && !g_gesture_held && !TeleportAiming() &&
                    text_click_math::TriggerMayCommand(click_on, click_waits);
    command_claim = CreatureCommandPumpTrigger(RawDown(kLeft), ok);
    // A SEED IN THE HAND. While the Hand carries a one-off seed, a press of
    // the trigger makes the seed active and does not reach the game
    // (miracle.cpp).
    if (!command_claim && MiraclePumpTrigger(RawDown(kLeft), ok)) {
      command_claim = true;
    }
  }

  // The A button starts a jump when no source holds the left button, and no
  // grab holds the world.
  if (g_a_event) {
    g_a_event = false;
    if (usable && InView() && !g_jump_control && !RawDown(kLeft) &&
        !GrabHeld() && ((g_game_mask | g_mouse_mask) & 1) == 0) {
      g_jump_control = true;
      g_jump_stage = 1;
      g_jump_frame = ~0ull;
      ++g_jumps_started;
      if (g_event_logs < kMaxEventLogs) {
        ++g_event_logs;
        ModLog("controllers  : the A button starts a jump, frame %llu",
               g_frame);
      }
    }
  }
  // One step for each frame, so the game reads each event in its own frame.
  if (g_jump_control && g_jump_frame != g_frame) {
    g_jump_frame = g_frame;
    if (g_jump_stage == 0) {
      g_jump_control = false;
    } else {
      g_jump_left = (g_jump_stage % 2) == 1;
      g_force_double = g_jump_stage == 3;
      g_jump_stage = g_jump_stage >= 4 ? 0 : g_jump_stage + 1;
    }
  }

  // THE GRIP HOLDS. With the direct hand, the right button is what GripStep
  // asks for. In its Pass phase that is the control itself.
  const bool hold_on = DirectHandNow();
  controller_math::GripStepOut hold = {false, false, false, 0};
  if (hold_on) {
    hold = StepGrip(usable, far_claim);
  } else {
    controller_math::GripReset(&g_hold);
  }

  // The ray of the Temple comes on or goes off with a control down, for
  // example when a click starts a guide of the game. The control then stands
  // for a different game button, and that must not be a new press.
  {
    static bool ray_before = false;
    const bool ray = TempleRayOn() && !MenuUp();
    if (ray != ray_before && (g_trigger_down || g_grip_down)) {
      g_button[kLeft].need_up = true;
      g_button[kRight].need_up = true;
    }
    ray_before = ray;
  }

  uint8_t next = 0;
  bool quiet[2] = {false, false};
  for (int b = 0; b < 2; ++b) {
    if (!ButtonDown(b)) {
      g_button[b].need_up = false;
    } else if (!usable) {
      // Down while the hand cannot act, for example in a menu or with the
      // game behind another window. It must go up before it can press.
      g_button[b].need_up = true;
    }
    // A claimed grip never reaches the game. It must open before it can
    // press, so its press never arrives late when the grab ends.
    if (g_grip_claimed && b == GripButton() && RawDown(b)) {
      g_button[b].need_up = true;
    }
    // A trigger that commands the Creature never reaches the game either.
    if (command_claim && b == kLeft && RawDown(b)) {
      g_button[b].need_up = true;
    }
    const bool held_by_hold = hold_on && b == kRight;
    const bool asks = held_by_hold ? hold.button : ButtonDown(b);
    // The click of an open grip lets go and picks nothing. It needs no fresh
    // press and no pixel in view, and a grab of the world does not hold it
    // back: a throw often ends with the hand out of view.
    const bool far_press = held_by_hold && FarGrabPressing();
    const bool click = held_by_hold && (hold.click || far_press);
    bool want = false;
    if (click) {
      want = usable && asks;
      quiet[b] = want && !far_press;
    } else if (usable && !g_button[b].need_up && asks) {
      // A press that starts with the Hand or the ray off view is dropped.
      // The button must go up before it can press again, so the press never
      // arrives later at the edge of the view. One that has started may end
      // anywhere.
      //
      // The cut scene window has no Hand and no ray, so no pixel is in view
      // there. A press is then a plain click at the place of the pointer,
      // which is all that a cut scene can use. The click that a text waits
      // for is such a click too: the game takes it at each place.
      want = text_click_math::PressMayStart(
          ((g_ctrl_mask >> b) & 1) != 0, InView(), CutsceneWindowFlat(),
          click_on && b == kLeft, click_waits);
      if (!want) {
        g_button[b].need_up = true;
        ++g_dropped;
        if (g_event_logs < kMaxEventLogs) {
          ++g_event_logs;
          ModLog("controllers  : the %s button press started off view, so "
                 "it is dropped, frame %llu", kButtonName[b], g_frame);
        }
      }
    }
    // During a grab, the trigger and any new left press stay away from the
    // game too, in the same way.
    if (want && !click && GrabHeld() && ((g_ctrl_mask >> b) & 1) == 0 &&
        (b == kLeft || b == TriggerButton())) {
      want = false;
      g_button[b].need_up = true;
      ++g_grab_trigger_held;
      if (GrabLogRoom()) {
        ModLog("grab         : the %s button is held back from the game during "
               "the grab, frame %llu", kButtonName[b], g_frame);
      }
    }
    if (b == kLeft && usable && g_jump_control) want = g_jump_left;
    if (want) next |= static_cast<uint8_t>(1u << b);
  }
  g_ctrl_mask = next;
  for (int b = 0; b < 2; ++b) ReconcileController(b, now, quiet[b]);
}

// The stick: a snap turn to the side, a wheel step forward and back.
//
// IN THE GOD VIEW (docs/god-view.md) the stick forward and back is one zoom
// step of the view for each push, not a wheel step. The other hand's stick
// moves the player over the land (PumpWalk). The turn goes the same way as
// with the plain stick, and comfort_camera.cpp gives it to the God view.
//
// THE SMOOTH ZOOM (god_zoom_smooth=1). The stick forward or back past
// kStickBack zooms the God view while it is held, and the pump asks the view
// again on each pass (TabletopGodZoomHold). The larger axis of the stick
// wins, as for a step and a turn, so one pass never zooms and turns. A hold
// starts with a fresh push only. A push that the view or a grab refused, and
// a hold that stopped because the stick went to the side, must come back
// below kStickBack before the stick zooms again.
void PumpStick(ULONGLONG now) {
  const bool was_holding = g_god_zoom_holding;
  g_god_zoom_holding = false;
  if (!Usable()) {
    g_wheel_tick = 0;
    return;
  }
  const bwvr::HandInput& hand = g_hands[PointingIndex()];
  const float ax = fabsf(hand.stick[0]);
  const float ay = fabsf(hand.stick[1]);
  const bool live = StickLive(hand);
  if (ax < kStickBack || !live) g_turn_armed = true;
  if (ay < kStickBack || !live) g_god_zoom_armed = true;
  const bool god = TabletopGodOwnsView();
  const bool smooth = god && TabletopGodZoomSmooth();
  // No snap turn and no zoom step while a grab holds the world. The two hands
  // turn and zoom then. Each push is refused once and counted. A push for a
  // step is past kStickPast, and a push for the smooth zoom is past
  // kStickBack.
  const bool pushed =
      live && ay > (smooth ? kStickBack : kStickPast) && ay > ax;
  if (!pushed) g_zoom_blocked = false;
  // THE TEMPLE (temple.cpp, temple_turn). The camera of a room is not the
  // camera of the land, so the turn and the zoom of the land do nothing good
  // there. A push of the stick to the side turns the player in the room by
  // one step, and a push forward or back does nothing.
  if (TempleTurnOn()) {
    if (live && g_turn_armed && ax > kStickPast && ax > ay) {
      g_turn_armed = false;
      if (TempleTurn(hand.stick[0] > 0.0f ? 1 : -1)) Pulse(PointingIndex());
    }
    g_wheel_tick = 0;
    return;
  }
  if (GrabHeld()) {
    const bool turn = live && g_turn_armed && ax > kStickPast && ax > ay;
    const bool zoom = pushed && !g_zoom_blocked;
    if (turn) g_turn_armed = false;
    // A push that the grab refused must come back before it zooms.
    if (zoom) g_zoom_blocked = true;
    if (pushed) g_god_zoom_armed = false;
    g_wheel_tick = 0;
    if (turn || zoom) {
      ++g_grab_stick_refused;
      if (GrabLogRoom()) {
        ModLog("grab         : the stick %s is refused during the grab, frame "
               "%llu", turn ? "turn" : "zoom", g_frame);
      }
    }
    return;
  }
  if (live && g_turn_armed && ax > kStickPast && ax > ay) {
    g_turn_armed = false;
    ++g_turns_asked;
    ComfortCameraRequestTurn(hand.stick[0] > 0.0f ? 1 : -1);
  }
  // THE DIRECTION OF THE ZOOM (god_zoom_forward_in). With 0 the stick forward
  // zooms out, so the world gets smaller, and the stick back zooms in. With
  // 1 the stick forward zooms in.
  const bool zoom_in = (hand.stick[1] > 0.0f) == TabletopGodZoomForwardIn();
  if (smooth) {
    // The rate follows the push.
    if (pushed && (was_holding || g_god_zoom_armed)) {
      g_god_zoom_armed = false;
      const float share = zoom_math::StickShare(ay, kStickBack, kStickFull);
      const bool in = zoom_in;
      g_god_zoom_holding = TabletopGodZoomHold(in ? share : -share);
      if (g_god_zoom_holding && !was_holding) {
        ++g_god_holds;
        Pulse(PointingIndex());
        if (g_event_logs < kMaxEventLogs) {
          ++g_event_logs;
          ModLog("controllers  : the stick starts a smooth zoom %s of the God "
                 "view, frame %llu", in ? "in" : "out", g_frame);
        }
      }
    }
    g_wheel_tick = 0;
    return;
  }
  if (god) {
    if (pushed && g_god_zoom_armed) {
      g_god_zoom_armed = false;
      if (TabletopGodStep(zoom_in ? 1 : -1, 0)) {
        ++g_god_zooms;
        Pulse(PointingIndex());
        if (g_event_logs < kMaxEventLogs) {
          ++g_event_logs;
          ModLog("controllers  : the stick asks the God view to zoom %s, frame "
                 "%llu", zoom_in ? "in" : "out", g_frame);
        }
      }
    }
    g_wheel_tick = 0;
    return;
  }
  if (pushed && !g_zoom_blocked) {
    // While the camera ignores its input, a click would only pile up and zoom
    // later, all at once.
    const uint8_t* mode = static_cast<const uint8_t*>(ComfortCameraLastMode());
    const bool camera_input =
        mode != nullptr && *reinterpret_cast<const int32_t*>(mode + 0x2EC) == 0;
    if (camera_input &&
        (g_wheel_tick == 0 || now - g_wheel_tick >= kWheelRepeatMs)) {
      g_wheel_tick = now;
      const int32_t counts = hand.stick[1] > 0.0f ? kWheelClick : -kWheelClick;
      *reinterpret_cast<int32_t*>(kWheelSum) += counts;
      ++g_wheel_sent;
      if (g_event_logs < kMaxEventLogs) {
        ++g_event_logs;
        ModLog("controllers  : the stick adds %d to the wheel, frame %llu",
               counts, g_frame);
      }
    }
  } else {
    g_wheel_tick = 0;
  }
}

// THE WALK WITH THE STICK (move_stick, docs/god-view.md, "The walk with the
// stick"). In the God view the stick of the hand that does not point moves
// the player over the land while it is pushed, and tabletop.cpp makes the
// move at the next main camera call (TabletopGodWalk). The pump asks the view
// again on each pass, so the walk stops in the frame in which the stick
// comes back. The push has the dead zone and the curve of the smooth zoom.
//
// The stick walks only while it does not drive the ring (button_layout=1 on
// a controller with face buttons, or radial_menu_stick=0). The walk is
// refused while the controllers cannot act, in a dialog box and in the game
// menu, while a grip holds the world, while the God view does not own the
// view (the Temple, a story, a cut scene), and while the camera is not the
// player's. A push that is refused, and a push that was there when the stick
// got this job, must come back to the middle before it walks. So the player
// never starts to move with no new push.
bool g_walk_blocked = false;
uint64_t g_walk_refused = 0;
int g_walk_logs = 0;
const int kMaxWalkLogs = 40;

// The stick of the hand that does not point has the job of the walk: the
// walk is on, and the stick does not belong to the ring.
bool StickWalks() {
  return TabletopGodMoveStick() != walk_math::kRuleOff &&
         !RadialMenuStickDrives();
}

// The first reason that a push of the stick does not move the player now, or
// null. The game's switch for the drag of the land is not in this list:
// TabletopGodWalk tests it.
const char* WalkRefusal() {
  if (!Usable()) return "the controllers cannot act now";
  if (MenuUp()) return "a dialog box is up";
  if (GrabHeld()) return "a grip holds the world";
  if (!TabletopGodOwnsView()) return "the God view does not own the view now";
  if (!WorldGrabCameraReady()) return "the camera is not the player's now";
  return nullptr;
}

void PumpWalk() {
  const int other = OtherIndex();
  const bwvr::HandInput& hand = g_hands[other];
  float push[2] = {0.0f, 0.0f};
  const bool pushed =
      hand.active && StickLive(hand) &&
      walk_math::Push(hand.stick[0], hand.stick[1], kStickBack, kStickFull,
                      push) > 0.0f;
  if (!pushed) {
    g_walk_blocked = false;
    return;
  }
  // The walk is off, or the stick belongs to the ring.
  if (!StickWalks()) {
    g_walk_blocked = true;
    return;
  }
  if (g_walk_blocked) return;
  const char* why = WalkRefusal();
  if (why == nullptr) {
    // The direction in which the controller of the stick points, flat on the
    // land, in the room.
    float rotation[9], translation[3], direction[3], aim[2];
    bool have_aim = false;
    if (hand.aim_valid &&
        HeadPoseToRoom(g_aim_p[other], g_aim_q[other], rotation, translation)) {
      controller_math::AimDirection(rotation, direction);
      have_aim = walk_math::AimForward(direction, walk_math::kAimLeast, aim);
    }
    if (!TabletopGodWalk(push, have_aim ? aim : nullptr)) {
      why = "the game does not allow a drag of the land now";
    }
  }
  if (why == nullptr) return;
  g_walk_blocked = true;
  ++g_walk_refused;
  if (g_walk_logs < kMaxWalkLogs) {
    ++g_walk_logs;
    ModLog("controllers  : the stick of the other hand does not move you: %s, "
           "frame %llu", why, g_frame);
  }
}

// THE TEST KEYS OF THE LAYOUT AND OF THE WALK (test_keys=1). The Menu key is
// the key beside the right Ctrl key that opens a context menu. With Ctrl it
// changes button_layout, and with Shift it changes move_stick. The Menu key
// alone is the key of one walk for both eyes (stereo.cpp). With Ctrl and
// Shift it is the key of land_light_eyes (land_light.cpp).
void PollLayoutKeys() {
  static int test_keys = -1;
  if (test_keys < 0) test_keys = IniInt("test_keys", 0) != 0 ? 1 : 0;
  if (test_keys == 0) return;
  static bool before[2] = {false, false};
  const bool menu = (GetAsyncKeyState(VK_APPS) & 0x8000) != 0;
  const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool down[2] = {menu && ctrl && !shift, menu && shift && !ctrl};
  const bool pressed[2] = {down[0] && !before[0], down[1] && !before[1]};
  before[0] = down[0];
  before[1] = down[1];
  if ((!pressed[0] && !pressed[1]) || !GameInFront()) return;
  if (pressed[0]) ControllersButtonLayoutToggle("Ctrl and the Menu key");
  if (pressed[1]) TabletopGodMoveStickCycle();
}

bool __cdecl h_AnyWindowMessages() {
  ControllersPump();
  return o_AnyWindowMessages();
}

// THE FOCUS TEST OF THE CAMERA. CameraModeNew3::Update starts the both button
// mode only when GetFocus() is the game window (0x0045ABDA). The call goes
// through the import slot 0x008A97F8, and no other code of the game that can
// run uses that slot. With the game window behind, the test fails, and the
// trigger and the grip together then do something different from the same
// press with the window in front. So the slot gets this function: while the
// controllers act with the window behind, the answer is the game window.
const uintptr_t kGetFocusSlot = 0x008A97F8;
uint64_t g_focus_asked = 0;   // calls of the game, this window
uint64_t g_focus_stood = 0;   // answers that the mod changed
// The script word "deactivate" (a test): the game window does not have the
// keyboard focus from then on, as after a click on a different program.
bool g_fake_unfocused = false;
HWND WINAPI h_GetFocus() {
  ++g_focus_asked;
  const HWND real = g_fake_unfocused ? nullptr : GetFocus();
  const HWND game = GameWindowHandle();
  if (game == nullptr || real == game || !ActsBehind()) return real;
  ++g_focus_stood;
  return game;
}

void PatchFocusTest() {
  void** slot = reinterpret_cast<void**>(kGetFocusSlot);
  const HMODULE user = GetModuleHandleA("user32.dll");
  void* real = user != nullptr
                   ? reinterpret_cast<void*>(GetProcAddress(user, "GetFocus"))
                   : nullptr;
  DWORD old = 0;
  // A slot that does not hold GetFocus is not the slot of this build, or a
  // different program changed it. Then the game keeps its own test.
  const bool ok = real != nullptr && !IsBadReadPtr(slot, sizeof(void*)) &&
                  *slot == real &&
                  VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
  if (ok) {
    *slot = reinterpret_cast<void*>(&h_GetFocus);
    VirtualProtect(slot, sizeof(void*), old, &old);
  }
  ModLog("hooks        : the focus test of the camera (the GetFocus import "
         "at 0x%08X) %s", kGetFocusSlot,
         ok ? "ok" : "FAILED, so the both button mode needs the game window "
                     "in front");
}

// The script word "deactivate" (a test). When the player clicks a different
// program, Windows sends WM_ACTIVATEAPP with 0 and WM_KILLFOCUS to the game
// window. The word gives the two messages to the window procedure of the
// game itself (GameWindowProc 0x007DBF30, stdcall). The game is then in that
// state: gWindowActive is 0, its mouse thread sleeps, and it lets go of the
// buttons and the keys. No window changes, and Windows does not know of it.
const uintptr_t kGameWindowProc = 0x007DBF30;
void FakeDeactivate() {
  g_fake_deactivate = false;
  const HWND window = GameWindowHandle();
  if (window == nullptr) return;
  typedef LRESULT(__stdcall* WindowProcFn)(HWND, UINT, WPARAM, LPARAM);
  const WindowProcFn proc = reinterpret_cast<WindowProcFn>(kGameWindowProc);
  const uint8_t held = g_game_mask;
  proc(window, WM_ACTIVATEAPP, FALSE, 0);
  proc(window, WM_KILLFOCUS, 0, 0);
  g_fake_unfocused = true;
  ModLog("controllers  : the script tells the game that a different program "
         "is active now (a test). gWindowActive is %d. The game held the "
         "buttons 0x%02X before and holds 0x%02X now, frame %llu",
         WindowActive() ? 1 : 0, held, g_game_mask, g_frame);
}

bool Hook(uintptr_t address, void* replacement, void** original,
          const char* name) {
  void* target = reinterpret_cast<void*>(address);
  MH_STATUS status = MH_CreateHook(target, replacement, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : %s at 0x%08X %s", name, address,
         status == MH_OK ? "ok" : "FAILED");
  return status == MH_OK;
}

// --- the aim --------------------------------------------------------------------

// THE AIM RAY (docs/controllers.md, "8. The aim ray"). The ray of a hand is
// the aim pose of the runtime with one turn in the frame of the controller,
// from a point that the preset can move. The turn has two parts.
//
// aim_preset: on a Touch controller the ray is one of three rays (aim_math.h,
// THE PRESETS): 0 the ray of the runtime, 1 the pointer of Meta's Interaction
// SDK, 2 the menu pointer of Half-Life: Alyx. The default is 2. The ray of the
// runtime sits too high and to the right of the ray of the Quest menu.
//
// aim_pitch_deg and aim_yaw_deg: a trim on top, for all controllers. A
// positive pitch is up, and a positive yaw is toward the other hand.
//
// With test_keys=1 the number pad changes the aim while the game runs, with
// Num Lock on: 8 and 2 turn the ray up and down, 4 and 6 turn it to the left
// and to the right as the pointing hand sees it, 1 degree a press. 5 goes to
// the next preset. The game binds no key of the number pad
// (ControlMap::LoadDefaults, 0x0046F890).
struct AimSettings {
  bool read;
  int preset;
  float trim_pitch;
  float trim_yaw;
  bool test_keys;
  uint64_t test_frame;  // aim_test_keys, 0 is off
  // aim_test_touch=N: from frame N the fake hand counts as a Touch
  // controller. A frame after the start dialog, because the clicks of the
  // dialog recipe aim the pose of the fake hand. 0 is off.
  uint64_t test_touch;
};
AimSettings g_aim = {false, aim_math::kPresetAlyx, 0.0f, 0.0f, false, 0, 0};
// What each hand last said in the log: -1 nothing, a preset for a Touch
// controller, kPresetCount for a controller that has no preset.
int g_aim_said[2] = {-1, -1};
aim_math::GripToAim g_grip_to_aim[2] = {};
bool g_have_grip_to_aim[2] = {false, false};
int g_grip_to_aim_logs = 0;
const int kMaxGripToAimLogs = 12;

// A number of the game folder's bwvr.ini that can have a fraction.
float IniFloat(const char* key, float fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return fallback;
  slash[1] = 0;
  strcat_s(path, "bwvr.ini");
  char text[32];
  GetPrivateProfileStringA("bwvr", key, "", text, sizeof(text), path);
  if (text[0] == 0) return fallback;
  return static_cast<float>(atof(text));
}

const char* AimProfile(int index) {
  if (g_fake.mode != 0) return "fake";
  return g_screen != nullptr ? g_screen->HandProfile(index) : "";
}

// The presets are for a Touch controller. The test key aim_test_touch lets
// the fake hand have them too.
bool AimTouch(int index) {
  if (g_fake.mode != 0) {
    return g_aim.test_touch > 0 && g_frame >= g_aim.test_touch;
  }
  return aim_math::TouchProfile(AimProfile(index));
}

void ReadAimSettings() {
  g_aim.read = true;
  g_aim.preset =
      aim_math::PresetOf(IniInt("aim_preset", aim_math::kPresetAlyx));
  g_aim.trim_pitch = aim_math::TrimStep(IniFloat("aim_pitch_deg", 0.0f), 0.0f);
  g_aim.trim_yaw = aim_math::TrimStep(IniFloat("aim_yaw_deg", 0.0f), 0.0f);
  g_aim.test_keys = IniInt("test_keys", 0) != 0;
  const int test_frame = IniInt("aim_test_keys", 0);
  g_aim.test_frame = g_aim.test_keys && test_frame > 0 ? test_frame : 0;
  const int test_touch = IniInt("aim_test_touch", 0);
  g_aim.test_touch = test_touch > 0 ? test_touch : 0;
  const aim_math::PresetPose& pose = aim_math::kPresets[g_aim.preset];
  ModLog("controllers  : aim_preset=%d: on a Touch controller the aim ray is "
         "%s, %.2f degrees toward the other hand and %.2f degrees %s from the "
         "aim pose of the runtime. The trim is pitch %.2f and yaw %.2f degrees "
         "(aim_pitch_deg, aim_yaw_deg): a positive pitch is up, and a positive "
         "yaw is toward the other hand", g_aim.preset, pose.name,
         pose.yaw_in_deg, fabsf(pose.pitch_up_deg),
         pose.pitch_up_deg < 0.0f ? "down" : "up", g_aim.trim_pitch,
         g_aim.trim_yaw);
  if (g_aim.test_keys) {
    ModLog("keys         : the number pad trims the aim ray (test_keys=1, Num "
           "Lock on). 8 and 2 turn it up and down, 4 and 6 to the left and to "
           "the right, 1 degree a press. 5 goes to the next preset: the ray "
           "of the runtime, the Meta pointer, the Half-Life: Alyx pointer");
  }
}

// The line of a trim key: the preset, the trim, and where the ray of the
// pointing hand is in this frame against the aim pose of the runtime. The
// angles and the move of the point come from the two poses of the frame, not
// from the settings.
void LogAimTrim(const char* key) {
  const int index = PointingIndex();
  aim_math::GripToAim seen;
  aim_math::GripToAimOf(g_hands[index].aim_position,
                        g_hands[index].aim_orientation, g_aim_p[index],
                        g_aim_q[index], &seen);
  ModLog("controllers  : %s: aim_preset=%d, %s%s. The trim is "
         "aim_pitch_deg=%.2f and aim_yaw_deg=%.2f. The ray of the %s hand is "
         "%.2f degrees %s and %.2f degrees to the %s of the aim pose of the "
         "runtime, and its point is at %.1f %.1f %.1f cm from the aim point "
         "(x right, y up, z back), frame %llu", key, g_aim.preset,
         aim_math::kPresets[g_aim.preset].name,
         AimTouch(index) ? "" : " (not in use: this is not a Touch controller)",
         g_aim.trim_pitch, g_aim.trim_yaw, index == 0 ? "left" : "right",
         fabsf(seen.pitch_up_deg), seen.pitch_up_deg < 0.0f ? "down" : "up",
         fabsf(seen.yaw_left_deg), seen.yaw_left_deg < 0.0f ? "right" : "left",
         seen.offset_m[0] * 100.0f, seen.offset_m[1] * 100.0f,
         seen.offset_m[2] * 100.0f, g_frame);
}

// One press of a trim key: 0 is up, 1 down, 2 to the left, 3 to the right,
// and 4 goes to the next preset. A short pulse of the pointing hand answers
// each press, and UpdateAim writes the log line when the new ray is known.
const char* g_aim_key = nullptr;
void AimKey(int key) {
  static const char* const names[5] = {"numpad 8", "numpad 2", "numpad 4",
                                       "numpad 6", "numpad 5"};
  // To the left is toward the other hand for a right hand only.
  const float to_left = PointingIndex() == 0 ? -1.0f : 1.0f;
  if (key == 0) g_aim.trim_pitch = aim_math::TrimStep(g_aim.trim_pitch, 1.0f);
  if (key == 1) g_aim.trim_pitch = aim_math::TrimStep(g_aim.trim_pitch, -1.0f);
  if (key == 2) g_aim.trim_yaw = aim_math::TrimStep(g_aim.trim_yaw, to_left);
  if (key == 3) g_aim.trim_yaw = aim_math::TrimStep(g_aim.trim_yaw, -to_left);
  if (key == 4) g_aim.preset = aim_math::NextPreset(g_aim.preset);
  PulseWith(PointingIndex(), 0.3f, 0.03f);
  g_aim_key = names[key];
}

// The trim keys. Read once a frame, and only while a window of the game is in
// front.
void PollAimKeys() {
  static bool down[5] = {false, false, false, false, false};
  const int keys[5] = {VK_NUMPAD8, VK_NUMPAD2, VK_NUMPAD4, VK_NUMPAD6,
                       VK_NUMPAD5};
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  const bool focus = process == GetCurrentProcessId();
  // Ctrl with a key of the number pad is a different test key (Ctrl with 5
  // is the rule of the Creature commands), so it does not change the aim.
  const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  for (int i = 0; i < 5; ++i) {
    const bool now = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
    if (now && !down[i] && focus && !ctrl) AimKey(i);
    down[i] = now;
  }
  // A test with no key press (aim_test_keys): from that frame, one key acts
  // each 30 frames, in the order 5, 5, 5, 8, 4, 4, 2, 2, 6. The first three
  // go through the presets and come back to the first one.
  // With fake_controller_play_frame, the frame counts as a step of the script
  // counts it.
  const ScriptStep as_step = {0, g_aim.test_frame, 0};
  const uint64_t t = StepFrame(as_step);
  if (g_aim.test_frame > 0 && t >= g_aim.test_frame) {
    const int order[9] = {4, 4, 4, 0, 2, 2, 1, 1, 3};
    const uint64_t since = t - g_aim.test_frame;
    if (since % 30 == 0 && since / 30 < 9) AimKey(order[since / 30]);
  }
}

// What the runtime gives: the aim pose seen from the grip pose, for each hand.
// One line at the first tracked frame, and one when it changes by more than 1
// degree or 5 mm. The next log of a headset then says what its runtime does.
void LogGripToAim(int index) {
  const bwvr::HandInput& hand = g_hands[index];
  if (!hand.active || !hand.aim_valid || !hand.grip_valid ||
      !hand.aim_tracked || !hand.grip_tracked) {
    return;
  }
  aim_math::GripToAim now;
  aim_math::GripToAimOf(hand.grip_position, hand.grip_orientation,
                        hand.aim_position, hand.aim_orientation, &now);
  if (g_have_grip_to_aim[index] &&
      !aim_math::GripToAimMoved(g_grip_to_aim[index], now, 1.0f, 0.005f)) {
    return;
  }
  const bool first = !g_have_grip_to_aim[index];
  g_grip_to_aim[index] = now;
  g_have_grip_to_aim[index] = true;
  if (g_grip_to_aim_logs >= kMaxGripToAimLogs) return;
  ++g_grip_to_aim_logs;
  ModLog("controllers  : %s hand \"%s\": the runtime %s the aim pose %.1f "
         "degrees %s, %.1f degrees to the %s and with a roll of %.1f degrees "
         "from the grip pose. The aim point is at %.1f %.1f %.1f cm in the "
         "frame of the grip (x right, y up, z back), frame %llu",
         index == 0 ? "left" : "right", AimProfile(index),
         first ? "puts" : "now puts", fabsf(now.pitch_up_deg),
         now.pitch_up_deg < 0.0f ? "down" : "up", fabsf(now.yaw_left_deg),
         now.yaw_left_deg < 0.0f ? "right" : "left", now.roll_deg,
         now.offset_m[0] * 100.0f, now.offset_m[1] * 100.0f,
         now.offset_m[2] * 100.0f, g_frame);
}

float FakeLeftAimFix() {
  if (!g_aim.read) ReadAimSettings();
  const aim_math::Turn t = aim_math::TotalTurn(g_aim.preset, AimTouch(0),
                                               g_aim.trim_yaw,
                                               g_aim.trim_pitch);
  // The right hand turns yaw_in to the left, the left hand yaw_in to the
  // right.
  return 2.0f * t.yaw_in_deg;
}

void AimTurnOfPointingHand(float* yaw_left_deg, float* pitch_up_deg) {
  if (!g_aim.read) ReadAimSettings();
  const int index = PointingIndex();
  const aim_math::Turn t = aim_math::TotalTurn(g_aim.preset, AimTouch(index),
                                               g_aim.trim_yaw,
                                               g_aim.trim_pitch);
  // The turn in goes to the left on the right hand and to the right on the
  // left hand.
  *yaw_left_deg = index == 0 ? -t.yaw_in_deg : t.yaw_in_deg;
  *pitch_up_deg = t.pitch_up_deg;
}

// Once a frame, after the hands are read and before Compute: the aim pose of
// each hand with its turn and its point.
void UpdateAim() {
  if (!g_aim.read) ReadAimSettings();
  if (g_aim.test_keys) PollAimKeys();
  for (int h = 0; h < 2; ++h) {
    const bwvr::HandInput& hand = g_hands[h];
    const bool touch = AimTouch(h);
    const aim_math::Turn t = aim_math::TotalTurn(g_aim.preset, touch,
                                                 g_aim.trim_yaw,
                                                 g_aim.trim_pitch);
    aim_math::Turned(hand.aim_orientation, t.yaw_in_deg, t.pitch_up_deg,
                     h == 0, g_aim_q[h]);
    aim_math::PresetPoint(g_aim.preset, touch, h == 0, hand.aim_position,
                          hand.aim_orientation, g_aim_p[h]);
    if (!hand.active || !hand.aim_valid) continue;
    LogGripToAim(h);
    // Which ray this hand has, when its profile is known and when the
    // profile or the preset changes.
    const int say = touch ? g_aim.preset : aim_math::kPresetCount;
    if (say == g_aim_said[h] || AimProfile(h)[0] == 0) continue;
    g_aim_said[h] = say;
    if (touch) {
      ModLog("controllers  : the aim ray of the %s hand is %s: it turns %.2f "
             "degrees to the %s and %.2f degrees %s from the aim pose of the "
             "runtime, with the trim", h == 0 ? "left" : "right",
             aim_math::kPresets[g_aim.preset].name, fabsf(t.yaw_in_deg),
             (t.yaw_in_deg < 0.0f) == (h == 0) ? "left" : "right",
             fabsf(t.pitch_up_deg), t.pitch_up_deg < 0.0f ? "down" : "up");
    } else {
      ModLog("controllers  : the %s hand \"%s\" is not a Touch controller, so "
             "its aim ray is the aim pose of the runtime with the trim only",
             h == 0 ? "left" : "right", AimProfile(h));
    }
  }
  if (g_aim_key != nullptr) {
    LogAimTrim(g_aim_key);
    g_aim_key = nullptr;
  }
}

}  // namespace

// Player keys. Off turns the whole controller path off.
void ControllersSetOptions(bool on, int left_hand, bool haptics,
                           bool swap_buttons) {
  g_on = on;
  g_left_hand_key = left_hand;
  g_haptics = haptics;
  g_swap_buttons = swap_buttons;
  ModLog("controllers  : %s, left_hand=%d (%s)", on ? "on" : "off", left_hand,
         left_hand < 0    ? "the left hand option of the game chooses the Hand"
         : left_hand == 0 ? "the right controller is the Hand"
                          : "the left controller is the Hand");
}

void ControllersSetFake(const FakeControllerSettings& fake) {
  g_fake = fake;
  g_fake.script[sizeof(g_fake.script) - 1] = 0;
  g_fake.points[sizeof(g_fake.points) - 1] = 0;
  g_fake.left_points[sizeof(g_fake.left_points) - 1] = 0;
  g_fake.gestures[sizeof(g_fake.gestures) - 1] = 0;
  if (g_fake.gesture_cm < 5) g_fake.gesture_cm = 5;
  if (g_fake.gesture_cm > 200) g_fake.gesture_cm = 200;
  g_sweep_turned = false;
  g_pose2_latched = false;
  g_script_leans = false;
  ParseScript(g_fake.script);
  ParsePoints(g_fake.points, g_points, &g_point_count);
  ParsePoints(g_fake.left_points, g_lpoints, &g_lpoint_count);
  g_point_index = -1;
  g_points_used = 0;
  g_lpoint_index = -1;
  g_lpoints_used = 0;
  // The test key fake_controller_rots: yaw,pitch,roll in degrees for the
  // word "rot", from the game folder's bwvr.ini. Not in the launcher.
  g_rot_count = 0;
  g_rot_index = -1;
  g_rots_used = 0;
  if (fake.mode != 0) {
    char path[MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash =
        length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
    if (slash != nullptr && strlen(path) + 9 < MAX_PATH) {
      slash[1] = 0;
      strcat_s(path, "bwvr.ini");
      char rots[512];
      GetPrivateProfileStringA("bwvr", "fake_controller_rots", "", rots,
                               sizeof(rots), path);
      ParsePoints(rots, g_rots, &g_rot_count);
      char turns[512];
      GetPrivateProfileStringA("bwvr", "fake_controller_turns", "", turns,
                               sizeof(turns), path);
      g_turns_used = 0;
      ParsePoints(turns, g_turns, &g_turn_count);
      if (g_rot_count > 0) {
        ModLog("controllers  : %d turns for the word \"rot\"", g_rot_count);
      }
    }
  }
  if (fake.mode != 0) LoadTraces(g_fake.gestures);
  if (fake.mode != 0) {
    ModLog("controllers  : fake %s, offset %d %d %d cm, yaw %d pitch %d roll "
           "%d degrees, %d script steps, %d points, %d left points%s",
           fake.mode >= 2 ? "hands" : "hand", fake.right_cm, fake.up_cm,
           fake.forward_cm, fake.yaw, fake.pitch, fake.roll, g_script_count,
           g_point_count, g_lpoint_count, fake.sweep ? ", sweeping" : "");
  }
}

// pointer_map.cpp: the controllers act while the game window is behind (the
// headset session stands for the front, or the test key
// fake_controller_front is on).
bool ControllersActBehind() { return ActsBehind(); }

// pointer_map.cpp: the real cursor stays where it is while a different
// window is in front (controller_window_behind=1, or the test key).
bool ControllersLeaveCursor() { return BehindOption() || FakeFront(); }

void ControllersSetScreen(bwvr::XrScreen* screen) { g_screen = screen; }

namespace {
void LatchFrameAim();

// The window in front, for the log: the name of its program and its class.
// The title is not in the log, because it can be private.
void DescribeFront(HWND front, char* out, size_t size) {
  if (front == nullptr) {
    strcpy_s(out, size, "no window is in front");
    return;
  }
  char window_class[64] = "?";
  GetClassNameA(front, window_class, sizeof(window_class));
  DWORD process = 0;
  GetWindowThreadProcessId(front, &process);
  if (process == GetCurrentProcessId()) {
    _snprintf_s(out, size, _TRUNCATE, "in front: a window of the game, class "
                "\"%s\", but the game is not the active program", window_class);
    return;
  }
  char path[MAX_PATH] = "";
  const char* name = "a program with no name to read";
  const HANDLE handle =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
  if (handle != nullptr) {
    DWORD length = sizeof(path);
    if (QueryFullProcessImageNameA(handle, 0, path, &length)) {
      const char* slash = strrchr(path, '\\');
      name = slash != nullptr ? slash + 1 : path;
    }
    CloseHandle(handle);
  }
  _snprintf_s(out, size, _TRUNCATE, "in front: %s, window class \"%s\"", name,
              window_class);
}

// THE GAME WINDOW BEHIND, once a frame: the latch of the session focus, and
// one log line for each change of the state. The line names the window in
// front, so a log from a headset session says what took the front and when.
int g_front_state = -1;  // 0 in front, 1 behind and the hands act, 2 behind
HWND g_front_window = nullptr;
char g_front_said[200] = "";
int g_front_logs = 0;
const int kMaxFrontLogs = 60;

void LatchFront() {
  g_session_focus = g_fake.mode != 0
                        ? !g_held[kNoFocus]
                        : g_screen != nullptr && g_screen->InputFocused();
  const bool option = BehindOption();
  const bool real = GameInFront();
  const bool acts = FakeFront() || (option && g_session_focus);
  const int state = real ? 0 : acts ? 1 : 2;
  const HWND front = real ? nullptr : GetForegroundWindow();
  if (state == g_front_state && front == g_front_window) return;
  char said[200] = "";
  if (!real) DescribeFront(front, said, sizeof(said));
  const int before = g_front_state;
  const bool same = state == before && strcmp(said, g_front_said) == 0;
  g_front_state = state;
  g_front_window = front;
  strcpy_s(g_front_said, said);
  // The game window in front from the start is the usual case: no line.
  if (same || (before < 0 && state == 0)) return;
  if (g_front_logs >= kMaxFrontLogs) return;
  ++g_front_logs;
  if (state == 0) {
    ModLog("controllers  : the game window is in front again, frame %llu",
           g_frame);
  } else if (state == 1) {
    ModLog("controllers  : the game window is behind (%s). The controllers "
           "act through %s. The mouse and the keyboard do not act in the "
           "game, frame %llu", said,
           FakeFront() ? "the test key fake_controller_front"
           : g_fake.mode != 0
                 ? "the fake controller, which stands for the headset session"
                 : "the headset session",
           g_frame);
  } else {
    ModLog("controllers  : the game window is behind (%s). The controllers "
           "do not act: %s, frame %llu", said,
           option ? "the headset session does not have the focus of the "
                    "runtime"
                  : "controller_window_behind=0",
           g_frame);
  }
}

// The both button mode of the camera, for the log: one line for each change.
// CameraModeNew3::Update sets 0x00C5B0B0 while the left and the right button
// are down together. A player who closes the whole hand gets it.
const uintptr_t kBothButtons = 0x00C5B0B0;
void LogBothButtons() {
  static int32_t before = 0;
  static int logs = 0;
  const int32_t now = *reinterpret_cast<const int32_t*>(kBothButtons);
  if ((now != 0) == (before != 0)) return;
  before = now;
  if (logs >= 20) return;
  ++logs;
  ModLog("controllers  : the both button mode of the camera is %s, frame "
         "%llu", now != 0 ? "on" : "off", g_frame);
}
}  // namespace

namespace {

// THE HAND SIDE. The game keeps its left hand option in the profile
// (HAND_ORIENTATION, bw_addresses.h). With left_hand=-1 that option chooses
// which controller is the Hand, so the player sets it in one place. Then each
// job of the two controllers changes sides: PointingIndex() gives the Hand,
// and OtherIndex() the other hand.
//
// The option can change in the options screen of the game while the player
// plays. A button that is down belongs to the hand that pressed it, so the
// change waits until each button of the two controllers is up. Then no press
// and no grab of the world goes to the wrong hand, and the pump sees no
// button go up that it did not see go down.
bool HandAnyDown(const bwvr::HandInput& h) {
  if (!h.active) return false;
  return h.trigger > 0.35f || h.squeeze > 0.35f || h.primary || h.secondary ||
         h.menu || h.stick_click;
}

// The game draws its Hand as a left hand or as a right hand. The mod makes it
// the hand of the controller that is the Hand, with the game's own
// CHand::ToggleLeftRight, as the game does for its option (packet 0x64). The
// game's own test (fn_0046E5F0) does nothing when the Hand already fits.
void MatchGameHand() {
  static int logs = 0;
  bool active = false;
  uint8_t* hand = static_cast<uint8_t*>(HandDirectHand(&active));
  if (hand == nullptr) return;
  const bool right =
      *reinterpret_cast<const int32_t*>(hand + bw::hand::kMirrorOffset) != 0;
  if (right != g_left_handed) return;
  // ToggleLeftRight turns the triangles of the mesh at CHand+0xB4. Not
  // before the game loaded the mesh.
  const uint8_t* mesh = *reinterpret_cast<uint8_t* const*>(hand + 0xB4);
  if (mesh == nullptr) return;
  const uint8_t* const* parts =
      *reinterpret_cast<const uint8_t* const* const*>(mesh + 0x10);
  if (parts == nullptr || *parts == nullptr) return;
  using Toggle = void(__thiscall*)(void*);
  reinterpret_cast<Toggle>(bw::hand::kToggleLeftRight)(hand);
  if (logs < 8) {
    ++logs;
    ModLog("controllers  : the game's Hand is a %s hand now, as the "
           "controller that is the Hand, frame %llu",
           g_left_handed ? "left" : "right", g_frame);
  }
}

void UpdateHandSide() {
  static int32_t game_before = -2;
  static bool said_wait = false;
  static bool decided = false;
  const int32_t game =
      *reinterpret_cast<const int32_t*>(bw::game::kHandOrientation);
  if (game != game_before) {
    game_before = game;
    ModLog("controllers  : the left hand option of the game "
           "(HAND_ORIENTATION) is %d, frame %llu", game, g_frame);
  }
  const bool want = g_left_hand_key >= 0 ? g_left_hand_key != 0 : game != 0;
  if (!decided || want != g_left_handed) {
    if (decided && (HandAnyDown(g_hands[0]) || HandAnyDown(g_hands[1]))) {
      if (!said_wait) {
        said_wait = true;
        ModLog("controllers  : the Hand goes to the %s controller when each "
               "button is up, frame %llu", want ? "left" : "right", g_frame);
      }
    } else {
      said_wait = false;
      decided = true;
      g_left_handed = want;
      // The hand that points now: a button that is down on it must go up
      // before it presses.
      g_hand_back = true;
      ModLog("controllers  : the %s controller is the Hand, and the %s "
             "controller is the other hand (left_hand=%d, the game's option "
             "%d), frame %llu", want ? "left" : "right",
             want ? "right" : "left", g_left_hand_key, game, g_frame);
    }
  }
  MatchGameHand();
}

// The test key mouse_owner_test_frame: from that frame the mouse owns the
// pointer (the owner mode "mouse only" of Ctrl F12), as when the controllers
// are not tracked. The game then places the Hand at the pointer, and its
// hints at the Hand draw in the eye pictures (hint_hand_depth).
void MouseOwnerTestFrame() {
  static int at = -1;
  if (at < 0) at = IniInt("mouse_owner_test_frame", 0);
  if (at <= 0 || g_owner_mode == 2) return;
  // As ControllersTestFrame counts it (fake_controller_play_frame).
  if (script_frame_math::Counted(g_frame, static_cast<uint64_t>(at),
                                g_script_play, g_script_shift_known,
                                g_script_shift) != static_cast<uint64_t>(at)) {
    return;
  }
  g_owner_mode = 2;
  ModLog("controllers  : test key mouse_owner_test_frame: the pointer owner "
         "is now %s, frame %llu", kOwnerModeName[g_owner_mode], g_frame);
}

}  // namespace

// Once for each frame the scene is drawn, after the head pose of the frame is
// known. The first call is frame 0 for the script.
void ControllersFrame() {
  if (!g_on) return;
  UpdateHandSide();
  if (g_fake.mode != 0) {
    UpdateScript();
    FakeHand(0, &g_hands[0]);
    FakeHand(1, &g_hands[1]);
  } else if (g_screen != nullptr) {
    g_hands[0] = g_screen->hand(0);
    g_hands[1] = g_screen->hand(1);
  } else {
    return;
  }
  LatchFront();
  LogBothButtons();
  UpdateAim();
  TempleFrame(g_frame);
  TextClickFrame(g_frame);
  MouseOwnerTestFrame();
  NoteDepthFrame(g_frame);
  Compute();
  ++g_frame;
  ++g_frames_in_window;
  LatchFrameAim();
  GrabSamples();
  // The next pulses of the patterns of the world (haptics.cpp).
  HapticsWorldFrame();

  // Latch what the pump needs. This runs once a frame, so an edge is seen
  // once. The pump may run more often and reads only the latches.
  g_frame_tick = GetTickCount64();
  // The camera ignores the wheel and the buttons while a story scene holds the
  // input off (mode+0x2EC). Log each change, so a test can tell why nothing
  // happened.
  {
    static int32_t off_before = -2;
    static int said = 0;
    const uint8_t* m = static_cast<const uint8_t*>(ComfortCameraLastMode());
    const int32_t off =
        m != nullptr ? *reinterpret_cast<const int32_t*>(m + 0x2EC) : -1;
    if (off != off_before && said < 8) {
      ++said;
      ModLog("controllers  : the camera input switch is %d, frame %llu", off,
             g_frame);
    }
    off_before = off;
  }
  const bwvr::HandInput& pointing = g_hands[PointingIndex()];
  // The hand is active again. A button that is already down on this frame
  // was pressed while the hand could not act, so it is not a new press.
  const bool back = pointing.active && !g_pointing_active;
  if (back) g_hand_back = true;
  g_pointing_active = pointing.active;
  const bool a_edge = pointing.primary && !g_a_before && !back;
  g_a_before = pointing.primary;
  g_tele_a_press = a_edge && pointing.active;
  g_tele_b_press = pointing.active && pointing.secondary && !g_tele_b_before &&
                   !back;
  g_tele_b_before = pointing.secondary;
  if (a_edge) {
    if (!TeleportOwnsA()) {
      g_a_event = true;
    } else if (Usable()) {
      // The A press gives the controller the pointer, as the jump did.
      g_button_event = true;
    }
  }
  const bool other[3] = {pointing.secondary, pointing.menu,
                         pointing.stick_click};
  for (int i = 0; i < 3; ++i) {
    if (other[i] && !g_other_before[i] && !back && Usable()) {
      g_button_event = true;
    }
    g_other_before[i] = other[i];
  }
  LatchGesture(back);
  PollLayoutKeys();
  LatchEsc();
  RadialMenuFrame();
}

namespace {
// The grip point in the room when the mouse last moved, for the motion test
// of the direct hand. A move of 2 cm counts.
float g_rest_grip[3];
bool g_have_rest_grip = false;
const float kGripMoveMeters = 0.02f;
}  // namespace

// For pointer_map.cpp: the render pixel of the pointing hand. With the pointer it is
// where the aim ray points. With the direct hand it is the Hand's own point,
// seen from the eye. False when there is none.
bool ControllerRenderPixel(float* x, float* y, bool* off_view) {
  if (!g_on) return false;
  // A test key of temple.cpp: the pixel of a scan of a room.
  if (TempleTestPixel(x, y)) {
    *off_view = false;
    return true;
  }
  if (DirectHandNow()) {
    if (!g_point.have_hand_pixel) return false;
    *x = g_point.hand_x;
    *y = g_point.hand_y;
    *off_view = g_point.hand_off_view;
    return true;
  }
  if (!g_point.have_pixel) return false;
  *x = g_point.x;
  *y = g_point.y;
  *off_view = g_point.off_view;
  return true;
}

// For intro_vr.cpp: the render pixel of the Hand's point of the pointing
// hand with the direct hand, also while a dialog box is up. The value of the
// active box is not 0 while the Lionhead logo plays, so ControllerRenderPixel
// gives the pixel of the aim ray there. False with controller_hand=0. With
// no pixel of the Hand, off_view is true.
bool ControllerHandPixel(float* x, float* y, bool* off_view) {
  if (!DirectMode()) return false;
  if (!g_point.have_hand_pixel) {
    *off_view = true;
    return true;
  }
  *x = g_point.hand_x;
  *y = g_point.hand_y;
  *off_view = g_point.hand_off_view;
  return true;
}

// For gesture_air.cpp: the draw button and the points of the last
// controllers frame. False with the controllers off, or before the first
// frame.
bool ControllersGestureInput(GestureInput* out) {
  memset(out, 0, sizeof(*out));
  if (!g_on || g_frame == 0) return false;
  out->held = g_gesture_held;
  out->button_down = g_gesture_down;
  out->why = g_gesture_why;
  // A MIRACLE THAT NEEDS A CIRCLE (a shield, a storm). The game reads the
  // circle from the path of the pointer while the Action button is held. So
  // while the grip is closed with such a miracle, the path of the hand goes
  // to the game as it does for the draw button (miracle.cpp).
  if (!out->held && MiracleDrawsCircle() && g_pointer_owned &&
      g_point.have_grip && g_point.have_world && Usable()) {
    out->held = true;
    out->why = "the grip is closed with a miracle that needs a circle";
  }
  for (int i = 0; i < 3; ++i) out->grip_room[i] = g_point.grip_room[i];
  for (int i = 0; i < 9; ++i) out->head[i] = g_point.head[i];
  out->scale = HeadPoseScale();
  float x = 0.0f, y = 0.0f;
  bool off = true;
  out->have_pixel = ControllerRenderPixel(&x, &y, &off) && !off;
  out->pixel_x = x;
  out->pixel_y = y;
  out->render_width = g_point.render_w;
  out->render_height = g_point.render_h;
  out->frame = g_frame;
  return true;
}

// True while the controller owns the pointer. Then the game's own samples of
// the pointer do not go in the gesture ring (gesture_air.cpp).
bool ControllersOwnPointer() { return g_on && g_installed && g_pointer_owned; }

// The state the hint words follow. The profile is empty when the
// runtime has not told it yet.
void ControllersHintState(ControllerHintState* out) {
  memset(out, 0, sizeof(*out));
  out->owns = ControllersOwnPointer();
  out->left_handed = g_left_handed;
  out->swap_buttons = g_swap_buttons;
  out->gesture_button = g_gesture.button;
  out->world_grab = g_grab.on && g_grab.hand >= 1;
  out->ring = RadialMenuHintMode();
  out->layout = ButtonLayout();
  out->walk = StickWalks();
  if (g_fake.mode != 0) {
    strcpy_s(out->profile, "fake");
  } else if (g_screen != nullptr) {
    strcpy_s(out->profile, g_screen->HandProfile(PointingIndex()));
  }
}

bool ControllersWalkPossible() {
  return StickWalks() && g_hands[OtherIndex()].active &&
         WalkRefusal() == nullptr && TabletopMoveAllowed();
}

// For hint_card.cpp (docs/controller-hints.md, "9. The hint card"): the grip
// pose of one hand and the head center, in the LOCAL space of OpenXR, in
// meters, from the last controllers frame. `which` is 0 for the hand that
// points and 1 for the other hand. `left` says which hand it is. False when
// the hand is not active or has no pose.
void HeadPoseFakeCenter(float out[3]);
bool ControllersCardPose(int which, float grip_position[3],
                         float grip_orientation[4], float head[3],
                         bool* left) {
  if (!g_on || g_frame == 0) return false;
  const int index = which == 0 ? PointingIndex() : 1 - PointingIndex();
  const bwvr::HandInput& hand = g_hands[index];
  if (!hand.active || (!hand.grip_valid && !hand.aim_valid)) return false;
  // A runtime that gives no grip pose gives the aim pose instead.
  const float* position =
      hand.grip_valid ? hand.grip_position : hand.aim_position;
  const float* orientation =
      hand.grip_valid ? hand.grip_orientation : hand.aim_orientation;
  for (int i = 0; i < 3; ++i) grip_position[i] = position[i];
  for (int i = 0; i < 4; ++i) grip_orientation[i] = orientation[i];
  if (g_fake.mode != 0) {
    HeadPoseFakeCenter(head);
  } else if (g_screen != nullptr && g_screen->eye(0).valid &&
             g_screen->eye(1).valid) {
    for (int i = 0; i < 3; ++i) {
      head[i] = 0.5f * (g_screen->eye(0).position[i] +
                        g_screen->eye(1).position[i]);
    }
  } else {
    return false;
  }
  *left = index == 0;
  return true;
}

// For radial_menu.cpp: the hand that does not point, with its grip point and
// the head center in the LOCAL space. False before the first frame. The ring
// may open while the pointing hand is not active, so this does not ask
// Usable.
bool ControllersRingHand(RingHand* out) {
  memset(out, 0, sizeof(*out));
  if (!g_on || !g_installed || g_frame == 0) return false;
  const bwvr::HandInput& hand = g_hands[OtherIndex()];
  float orientation[4];
  bool left = false;
  out->active =
      ControllersCardPose(1, out->position, orientation, out->head, &left);
  out->left = OtherIndex() == 0;
  out->usable = g_owner_mode != 2 && !IntroBlocksButtons() &&
                (GameInFront() || ActsBehind()) && !StereoIdle();
  out->dialog = MenuUp();
  out->grab_held = GrabHeld();
  // A Vive wand, a Windows Mixed Reality controller and the simple controller
  // have no face buttons.
  const char* profile = HandProfileOf(OtherIndex());
  out->face_buttons = strstr(profile, "vive") == nullptr &&
                      strstr(profile, "microsoft") == nullptr &&
                      strstr(profile, "simple") == nullptr;
  if (!hand.active) return true;
  // The face button of the ring: the second one with button_layout=1, and
  // the first one with 0.
  out->button = ButtonLayout() != 0 ? hand.secondary : hand.primary;
  out->stick_click = hand.stick_click;
  out->stick_live = StickLive(hand);
  out->trigger = hand.trigger;
  out->stick[0] = hand.stick[0];
  out->stick[1] = hand.stick[1];
  return true;
}

// A pulse on the pointing hand, for the draw.
void ControllersGesturePulse(float amplitude, float seconds) {
  if (g_on) PulseWith(PointingIndex(), amplitude, seconds);
}

int ControllersGestureSizeCm() { return g_gesture.size_cm; }

// Whether a hand points and its ray is known in the world. The ray can still
// be behind the head, and then there is no pixel.
bool ControllerPointing() {
  return g_on && g_point.have_hand && g_point.have_world;
}

// Whether the aim turned more than 1 degree from where it was when the mouse
// last moved, or when this last said yes. The aim that counts is the aim at rest.
// With the direct hand, a move of the grip point of 2 cm in the room counts
// too, because a hand can move without a turn.
//
// The aim is compared in the room, before the room basis L. So a turn of the
// game camera (the keys, the middle mouse, a fly to or a story camera) is not
// a turn of a controller that lies still on the desk. An untracked pose is
// not the player's motion, so the test waits and the rest stays.
bool ControllerMoved() {
  if (!ControllerPointing()) return false;
  if (!g_point.aim_tracked) return false;
  if (DirectMode() && !g_point.grip_tracked) return false;
  bool grip_moved = false;
  if (DirectMode() && g_point.have_grip) {
    const float scale = HeadPoseScale();
    if (g_have_rest_grip && scale > 0.0f &&
        controller_math::Distance(g_rest_grip, g_point.grip_room) / scale >
            kGripMoveMeters) {
      grip_moved = true;
    }
    if (!g_have_rest_grip || grip_moved) {
      for (int i = 0; i < 3; ++i) g_rest_grip[i] = g_point.grip_room[i];
      g_have_rest_grip = true;
    }
  }
  const float* aim = g_point.room_direction;
  if (!g_have_rest) {
    for (int i = 0; i < 3; ++i) g_rest_direction[i] = aim[i];
    g_have_rest = true;
    return grip_moved;
  }
  float along = 0.0f;
  for (int i = 0; i < 3; ++i) along += g_rest_direction[i] * aim[i];
  if (along > 1.0f) along = 1.0f;
  if (along < -1.0f) along = -1.0f;
  if (acosf(along) * 57.29578f <= 1.0f) return grip_moved;
  for (int i = 0; i < 3; ++i) g_rest_direction[i] = aim[i];
  return true;
}

// The mouse took the pointer, so the aim is at rest from now on.
void ControllerRestMotion() {
  if (!ControllerPointing()) return;
  for (int i = 0; i < 3; ++i) g_rest_direction[i] = g_point.room_direction[i];
  g_have_rest = true;
  if (g_point.have_grip) {
    for (int i = 0; i < 3; ++i) g_rest_grip[i] = g_point.grip_room[i];
    g_have_rest_grip = true;
  }
}

// pointer_map.cpp, once a frame: whether the owner rule gave the controller
// the pointer this frame.
void ControllersSetPointerOwned(bool owned) { g_pointer_owned = owned; }

// For pointer_map.cpp: the unit direction from the game camera to the direct
// Hand's reach point, for the rays of the camera code (the land drag). The
// point is fixed in the room, so a lean of the head does not turn the ray and
// the land does not slide. False when the direct hand does not own the
// pointer, and then the rays come from the pointer pixel.
bool ControllerCameraRay(float direction_out[3]) {
  if (!DirectHandNow() || !g_pointer_owned || !g_point.have_grip ||
      !g_point.have_camera_dir) {
    return false;
  }
  for (int i = 0; i < 3; ++i) direction_out[i] = g_point.camera_dir[i];
  return true;
}

// For hand_direct.cpp: the point where the direct hand goes this frame, and
// the reach factor. False when the mouse owns the Hand, with the pointer,
// or with no grip point. Then the game places the Hand.
bool ControllerDirectHand(float point_out[3], float* reach_factor,
                          float* real_distance) {
  if (!DirectHandNow() || !g_pointer_owned || !g_point.have_grip) return false;
  for (int i = 0; i < 3; ++i) point_out[i] = g_point.reach[i];
  *reach_factor = g_point.reach_factor;
  *real_distance = g_point.grip_distance;
  return true;
}

// For hand_direct.cpp: where the reach point and the grip are in
// the room this frame, and the room basis L. False with the same rule as
// ControllerDirectHand.
bool ControllerDirectMotion(float reach_room_out[3], float grip_room_out[3],
                            float room_out[9]) {
  if (!DirectHandNow() || !g_pointer_owned || !g_point.have_grip) return false;
  for (int i = 0; i < 3; ++i) {
    reach_room_out[i] = g_point.reach_room[i];
    grip_room_out[i] = g_point.grip_room[i];
  }
  for (int i = 0; i < 9; ++i) room_out[i] = g_point.room_basis[i];
  return true;
}

// For hand_exact.cpp (docs/hand.md, "7. The exact Hand"): the pointing grip's
// axes in the world of this frame, L * M_grip, and whether it is the left
// hand. False with the same rule as ControllerDirectHand.
bool ControllersExactRotation(float rotation[9], bool* left) {
  if (!DirectHandNow() || !g_pointer_owned || !g_point.have_grip ||
      !g_point.have_rotation) {
    return false;
  }
  for (int i = 0; i < 9; ++i) rotation[i] = g_point.grip_rotation[i];
  *left = PointingIndex() == 0;
  return true;
}

namespace {
// For the overlay: a hand's grip was reported as tracked at least once.
bool g_overlay_seen_tracked[2] = {false, false};
}  // namespace

// For overlay.cpp (docs/hand-model.md, "Overlay renderer"): the grip pose of
// each hand in the world, one to one, and its trigger and squeeze. It takes the
// head sample of this frame and the steps of Compute, on the table and off
// it, so the pointing grip lands on the point the direct hand uses when the
// reach gain is 0. The rotation is L * M_grip, as HandToWorld gives it. A
// hand that is not active, or whose pose is not tracked, is not valid. False
// with the controllers off, before the first frame, or with no head sample.
// overlay.cpp calls it once a frame, after ControllersFrame.
bool ControllersOverlayHands(OverlayHands* out) {
  memset(out, 0, sizeof(*out));
  if (!g_on || g_frame == 0) return false;
  float head[9], lean[3], room[9], game_position[3], focus[3], t_head[3];
  if (!HeadPoseCurrent(head, lean) || !HeadCameraStageBasis(room) ||
      !HeadCameraGameCamera(game_position, focus) ||
      !HeadPoseHeadInRoom(t_head)) {
    return false;
  }
  float table_origin[3], table_basis[9], table_center[3], gain = 1.0f;
  const bool table =
      TabletopRoom(table_origin, table_basis, table_center, &gain);
  if (table) {
    for (int i = 0; i < 9; ++i) room[i] = table_basis[i];
  } else {
    gain = 1.0f;
  }
  float lean_w[3], t_head_w[3], eye[3];
  controller_math::ScaleRoom(lean, gain, lean_w);
  controller_math::ScaleRoom(t_head, gain, t_head_w);
  controller_math::EyeFromLean(table ? table_origin : game_position, room,
                               lean_w, eye);
  out->units_per_meter = HeadPoseScale() * gain;
  out->pointing = PointingIndex();
  out->frame = g_frame;
  for (int index = 0; index < 2; ++index) {
    const bwvr::HandInput& hand = g_hands[index];
    OverlayHand& o = out->hand[index];
    // A runtime that gives no grip pose gives the aim pose instead.
    const bool grip_pose = hand.grip_valid;
    if (!hand.active || !(grip_pose || hand.aim_valid)) continue;
    if (!Tracked(grip_pose ? hand.grip_tracked : hand.aim_tracked,
                 &g_overlay_seen_tracked[index])) {
      continue;
    }
    float m_grip[9], t_grip[3], t_grip_w[3];
    if (!HeadPoseToRoom(grip_pose ? hand.grip_position : hand.aim_position,
                        grip_pose ? hand.grip_orientation
                                  : hand.aim_orientation,
                        m_grip, t_grip)) {
      continue;
    }
    controller_math::ScaleRoom(t_grip, gain, t_grip_w);
    controller_math::HandToWorld(eye, room, t_head_w, t_grip_w, m_grip,
                                 o.position, o.rotation);
    o.trigger = hand.trigger;
    o.squeeze = hand.squeeze;
    o.valid = true;
  }
  return true;
}

// THE AIM FOR THE TELEPORT, in the room and in the world. A point of the room
// in meters r goes to the world as
//
//   world = base + map * r,   base = E - L * gain * t_head,
//   map = L * (units per meter)
//
// which is HandToWorld for a point, so it is the map of the grip in every
// play mode: the God view and the table (Lt, P0, gain s / 7.5) and the
// immersive view (L, the game camera, gain 1).
//
// ONE AIM A FRAME. The room of the God view and of the table is known only
// inside the draw of a frame: TabletopRoom gives it from the main camera
// call to the end of GGame::ProcessGraphicsEngine. The pump of the buttons
// runs after that time: GGame::Loop calls GGame::ProcessGraphicsEngine at
// 0x0054D3BE and then GGame::FlipScreen at 0x0054D3F5, and LHScreen::Flip
// has the pump. So the pump gets the map of the immersive view, at 7.5 units
// a meter from the game camera. A press of a Creature command on the land
// would say "the arc meets no land" for that reason, and the tap would use
// the aim of the release. On the desktop the game camera is near the land,
// and the press meets the land near that camera. So ControllersFrame makes the
// aim once, inside the draw, and each caller gets that aim until the next
// frame.
namespace {
TeleportInput g_frame_aim = {};
}  // namespace

bool ControllersTeleportInput(TeleportInput* out) {
  memset(out, 0, sizeof(*out));
  if (!g_on || g_frame == 0) return false;
  *out = g_frame_aim;
  const int index = PointingIndex();
  const bwvr::HandInput& hand = g_hands[index];
  out->hand = hand.active;
  out->a_down = hand.active && hand.primary;
  out->a_pressed = g_tele_a_press;
  out->b_down = hand.active && hand.secondary;
  out->b_pressed = g_tele_b_press;
  out->usable = Usable();
  out->grab_held = GrabHeld();
  out->gesture_held = g_gesture_held;
  out->frame = g_frame;
  if (!hand.active || !hand.aim_valid) out->have_aim = false;
  return true;
}

namespace {
// The aim of this frame: have_aim, the aim in the room, and the map to the
// world. Called once a frame from ControllersFrame.
void LatchFrameAim() {
  TeleportInput* out = &g_frame_aim;
  memset(out, 0, sizeof(*out));
  const int index = PointingIndex();
  const bwvr::HandInput& hand = g_hands[index];
  if (!hand.active || !hand.aim_valid) return;
  if (!Tracked(hand.aim_tracked, &g_tele_seen_tracked)) return;
  float head[9], lean[3], room[9], game_position[3], focus[3], t_head[3];
  if (!HeadPoseCurrent(head, lean) || !HeadCameraStageBasis(room) ||
      !HeadCameraGameCamera(game_position, focus) ||
      !HeadPoseHeadInRoom(t_head)) {
    return;
  }
  float table_origin[3], table_basis[9], table_center[3], gain = 1.0f;
  const bool table =
      TabletopRoom(table_origin, table_basis, table_center, &gain);
  if (table) {
    for (int i = 0; i < 9; ++i) room[i] = table_basis[i];
  } else {
    gain = 1.0f;
  }
  const float life = HeadPoseScale();
  if (!(life > 0.0f)) return;
  float m_aim[9], t_aim[3];
  if (!HeadPoseToRoom(g_aim_p[index], g_aim_q[index], m_aim, t_aim)) {
    return;
  }
  float lean_w[3], t_head_w[3], eye[3];
  controller_math::ScaleRoom(lean, gain, lean_w);
  controller_math::ScaleRoom(t_head, gain, t_head_w);
  controller_math::EyeFromLean(table ? table_origin : game_position, room,
                               lean_w, eye);
  controller_math::AimDirection(m_aim, out->direction);
  const float length = sqrtf(out->direction[0] * out->direction[0] +
                             out->direction[1] * out->direction[1] +
                             out->direction[2] * out->direction[2]);
  if (!(length > 1e-6f)) return;
  for (int i = 0; i < 3; ++i) {
    out->direction[i] /= length;
    out->origin_m[i] = t_aim[i] / life;
    out->eye[i] = eye[i];
  }
  float shift[3];
  controller_math::Apply(room, false, t_head_w, shift);
  for (int i = 0; i < 3; ++i) out->base[i] = eye[i] - shift[i];
  out->units_per_meter = life * gain;
  for (int i = 0; i < 9; ++i) out->map[i] = room[i] * out->units_per_meter;
  out->have_aim = true;
}
}  // namespace

// THE RAY OF A FAR GRAB (docs/grab.md, "The grab from a distance"): the aim
// pose of the pointing hand in the world, as the teleport takes it. The origin
// is the aim point, the direction is a unit vector, and units is the world
// units in a meter of the room. eye is the eye in the world. False when the
// aim is not known.
bool ControllersFarRay(float origin[3], float direction[3], float* units,
                       float eye[3]) {
  TeleportInput in;
  if (!ControllersTeleportInput(&in) || !in.have_aim) return false;
  float world[3];
  for (int i = 0; i < 3; ++i) {
    origin[i] = in.base[i] + in.map[i * 3 + 0] * in.origin_m[0] +
                in.map[i * 3 + 1] * in.origin_m[1] +
                in.map[i * 3 + 2] * in.origin_m[2];
    world[i] = in.map[i * 3 + 0] * in.direction[0] +
               in.map[i * 3 + 1] * in.direction[1] +
               in.map[i * 3 + 2] * in.direction[2];
  }
  const float length = controller_math::Length(world);
  if (!(length > 1e-6f)) return false;
  for (int i = 0; i < 3; ++i) direction[i] = world[i] / length;
  *units = in.units_per_meter;
  for (int i = 0; i < 3; ++i) eye[i] = in.eye[i];
  return true;
}

// Whether the pointing hand may act on a far object now: it can act, the
// controller owns the pointer, no grab holds the world, no jump, gesture or
// teleport runs, and the trigger is up. why says the first reason for a no.
bool ControllersFarAllowed(const char** why) {
  *why = "";
  if (!g_on || !DirectHandNow()) {
    *why = "the direct hand is off";
    return false;
  }
  if (!Usable()) {
    *why = "the hand cannot act now";
    return false;
  }
  if (!g_pointer_owned) {
    *why = "the mouse owns the pointer";
    return false;
  }
  if (GrabHeld()) {
    *why = "a grip holds the world";
    return false;
  }
  if (g_jump_control || g_gesture_held || TeleportAiming()) {
    *why = "a jump, a gesture or a teleport runs";
    return false;
  }
  if (RawDown(kLeft) || ((g_game_mask | g_mouse_mask | g_ctrl_mask) & 1) != 0) {
    *why = "the trigger is down";
    return false;
  }
  return true;
}

// THE RAY OF THE MENU. While a dialog box is up and the controller owns the
// pointer, the pointing hand points like a mouse and the player needs to see
// the line. A room of the Temple has the same ray (temple.cpp). from and to are the ends of the ray in the world: the aim point
// and a point kMenuRayMeters of the room along the aim. units is the world
// units in a meter of the room. False when there is no ray to draw.
bool ControllersMenuRay(float from[3], float to[3], float* units) {
  if (!g_on || !g_pointer_owned || !(MenuUp() || TempleRayOn())) return false;
  TeleportInput in;
  if (!ControllersTeleportInput(&in) || !in.have_aim) return false;
  // The ray ends where the cursor is in both eyes: at the depth of the menu,
  // along the same ray that gave the pixel (Compute).
  if (g_point.have_world && g_point.menu_depth) {
    for (int i = 0; i < 3; ++i) {
      from[i] = g_point.origin[i];
      to[i] = g_point.origin[i] + g_point.direction[i] * g_point.target_distance;
    }
    *units = in.units_per_meter;
    return true;
  }
  const float kMenuRayMeters = 2.0f;
  float a[3], b[3];
  for (int i = 0; i < 3; ++i) {
    a[i] = in.origin_m[i];
    b[i] = in.origin_m[i] + in.direction[i] * kMenuRayMeters;
  }
  for (int i = 0; i < 3; ++i) {
    from[i] = in.base[i] + in.map[i * 3 + 0] * a[0] +
              in.map[i * 3 + 1] * a[1] + in.map[i * 3 + 2] * a[2];
    to[i] = in.base[i] + in.map[i * 3 + 0] * b[0] +
            in.map[i * 3 + 1] * b[1] + in.map[i * 3 + 2] * b[2];
  }
  *units = in.units_per_meter;
  return true;
}

void ControllersSetDirect(const DirectHandSettings& direct) {
  g_direct = direct;
  if (g_direct.throw_gain_pct < 0) g_direct.throw_gain_pct = 0;
  if (g_direct.throw_gain_pct > 1000) g_direct.throw_gain_pct = 1000;
  if (!(g_direct.creature_slap_mps >= 0.2f)) g_direct.creature_slap_mps = 0.2f;
  if (g_direct.creature_slap_mps > 10.0f) g_direct.creature_slap_mps = 10.0f;
  if (g_direct.creature_touch_cm < 0) g_direct.creature_touch_cm = 0;
  if (g_direct.creature_touch_cm > 50) g_direct.creature_touch_cm = 50;
  if (g_direct.reach_start_cm < 0) g_direct.reach_start_cm = 0;
  if (g_direct.reach_gain < 0) g_direct.reach_gain = 0;
  if (g_direct.hand_cm < 5) g_direct.hand_cm = 5;
  if (g_direct.hand_cm > 40) g_direct.hand_cm = 40;
  if (g_direct.scale_pct < 5) g_direct.scale_pct = 5;
  if (g_direct.scale_pct > 400) g_direct.scale_pct = 400;
  if (g_direct.reach_anchor_down_cm < 0) g_direct.reach_anchor_down_cm = 0;
  if (g_direct.reach_anchor_down_cm > 100) g_direct.reach_anchor_down_cm = 100;
  if (g_direct.mode == 1 && g_direct.reach_gain == 0) {
    ModLog("controllers  : the direct hand is on. The Hand is where the "
           "controller is, one to one, and %d cm long at %d%%.",
           g_direct.hand_cm, g_direct.scale_pct);
  } else if (g_direct.mode == 1) {
    const float start = g_direct.reach_start_cm * 0.01f;
    const float gain = static_cast<float>(g_direct.reach_gain);
    ModLog("controllers  : the direct hand is on. The reach anchor is %d cm "
           "below the head center at the last recenter. One to one up to %d "
           "cm from it, then a gain of %d per meter: 50 cm reaches %.2f m, "
           "65 cm %.2f m, 80 cm %.2f m. The Hand is %d cm long at %d%%.",
           g_direct.reach_anchor_down_cm, g_direct.reach_start_cm,
           g_direct.reach_gain,
           controller_math::ReachDistance(0.50f, start, gain),
           controller_math::ReachDistance(0.65f, start, gain),
           controller_math::ReachDistance(0.80f, start, gain),
           g_direct.hand_cm, g_direct.scale_pct);
  } else {
    ModLog("controllers  : the controller is a pointer (M2), not a direct "
           "hand");
  }
  if (g_direct.mode == 1) {
    ModLog("controllers  : the Creature by touch: a slap needs %.2f m/s of "
           "the real hand and a hard slap %.2f m/s. The body reaches %d cm "
           "past its radius. %s", g_direct.creature_slap_mps,
           1.8f * g_direct.creature_slap_mps, g_direct.creature_touch_cm,
           g_direct.creature_slap_wait
               ? "The game's wait of 1 s before a slap stays."
               : "A slap can count at once.");
  }
}

// The player keys of the draw.
void ControllersSetGesture(const GestureSettings& gesture) {
  g_gesture = gesture;
  if (g_gesture.button < 0 || g_gesture.button > 2) g_gesture.button = 1;
  if (g_gesture.size_cm < 5) g_gesture.size_cm = 5;
  if (g_gesture.size_cm > 200) g_gesture.size_cm = 200;
  static const char* const kButton[3] = {
      "off", "the second face button (B, or Y on the left hand)",
      "the stick click"};
  ModLog("gesture      : the draw button is %s. %d cm of hand motion is a "
         "third of the view height to the game.", kButton[g_gesture.button],
         g_gesture.size_cm);
}

// The player keys of the grab. world_grab.cpp gets the same.
void ControllersSetGrab(const GrabSettings& grab) {
  g_grab = grab;
  if (g_grab.press_pct < 5) g_grab.press_pct = 5;
  if (g_grab.press_pct > 100) g_grab.press_pct = 100;
  if (g_grab.release_pct < 0) g_grab.release_pct = 0;
  if (g_grab.release_pct >= g_grab.press_pct) {
    g_grab.release_pct = g_grab.press_pct - 5;
  }
  if (!g_grab.on || g_grab.hand <= 0) {
    ModLog("grab         : grabbing the world is off");
  } else {
    ModLog("grab         : the other hand's grip grabs the world%s%s. A grip "
           "grabs above %d%% and lets go below %d%%.",
           g_grab.hand >= 2 ? ", and so does the pointing grip when the Hand "
                              "is empty and over nothing it can hold"
                            : "",
           g_grab.two_hands ? ". Both grips turn and zoom" : "",
           g_grab.press_pct, g_grab.release_pct);
  }
}

// For world_grab.cpp: the grip points of both hands in the room, from the
// last frame, and which of them hold the world. 0 is the other hand and 1 the
// pointing hand. False before the first frame with a head.
bool ControllersGrabInput(GrabInput* out) {
  memset(out, 0, sizeof(*out));
  if (!g_on || !g_grab_have_head) return false;
  const bool held[2] = {g_off_engaged, g_grip_claimed};
  for (int slot = 0; slot < 2; ++slot) {
    out->hand[slot].held = held[slot] && g_grab_have[slot];
    out->hand[slot].tracked = g_grab_tracked[slot];
    for (int i = 0; i < 3; ++i) out->hand[slot].room[i] = g_grab_room[slot][i];
  }
  for (int i = 0; i < 3; ++i) out->head[i] = g_grab_head[i];
  out->scale = HeadPoseScale();
  out->frame = g_grab_frame;
  out->time = g_grab_time;
  return true;
}

// True while a grip holds the world. comfort_camera.cpp refuses snap turns
// and zoom steps then.
bool ControllersGrabHeld() { return g_on && GrabHeld(); }

// world_grab.cpp ends a grab: the camera left the player, or another part of
// the game moved it. A new press starts the next one.
void ControllersGrabEnd(const char* why) { LetGoOfWorld(why); }

// A short pulse on the hands of a grab: bit 0 the other hand, bit 1 the
// pointing hand.
void ControllersGrabPulse(int hands, float amplitude, float seconds) {
  if ((hands & 1) != 0) PulseWith(OtherIndex(), amplitude, seconds);
  if ((hands & 2) != 0) PulseWith(PointingIndex(), amplitude, seconds);
}

// How many frames the controllers have seen.
uint64_t ControllersFrameCount() { return g_frame; }

// For a test key with a frame of its own (village_test_site,
// miracle_test_seeds, text_click_test, near_test_flips and others): the number
// that the key compares with its frame, when the frame of now is `frame`. With
// fake_controller_play_frame, a key frame from the play frame on counts as a
// step of the script counts it. So such a key and the steps of one recipe
// keep their distance when the run starts in a test save. Without the key of
// the script, the number is `frame`.
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame) {
  return script_frame_math::Counted(frame, key_frame, g_script_play,
                                    g_script_shift_known, g_script_shift);
}

// For the land drag probe: the pointing grip and the head in the
// room, from the last frame. False when the controller does not own the Hand.
bool ControllersProbePoints(float grip_room[3], float head_room[3]) {
  if (!g_on || !g_pointer_owned || !g_grab_have[1] || !g_grab_have_head) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    grip_room[i] = g_grab_room[1][i];
    head_room[i] = g_grab_head[i];
  }
  return true;
}

int ControllersHandScalePct() { return g_direct.scale_pct; }
int ControllersHandCm() { return g_direct.hand_cm; }
int ControllersThrowGainPct() { return g_direct.throw_gain_pct; }
float ControllersCreatureSlapMps() { return g_direct.creature_slap_mps; }
int ControllersCreatureTouchCm() { return g_direct.creature_touch_cm; }
bool ControllersCreatureSlapWait() { return g_direct.creature_slap_wait; }

// A controller button changed since the owner rule last asked.
bool ControllersButtonEvent() {
  const bool event = g_button_event;
  g_button_event = false;
  return event;
}

// A real mouse button was pressed since the owner rule last asked.
bool ControllersMouseEvent() {
  const bool event = g_mouse_event;
  g_mouse_event = false;
  return event;
}

// 0 automatic, 1 controller only, 2 mouse only.
int ControllersOwnerMode() { return g_owner_mode; }

// The test key Ctrl+F12.
void ControllersCycleOwnerMode() {
  g_owner_mode = (g_owner_mode + 1) % 3;
  ModLog("controllers  : the pointer owner is now %s",
         kOwnerModeName[g_owner_mode]);
}

// The value of the switch of Ctrl+F12 on the debug page (debug_menu.h).
int ControllersOwnerModeValue() {
  return g_on && g_installed ? g_owner_mode : kDebugCannot;
}

int ControllersButtonLayout() { return ButtonLayout(); }

// The value of the switch of button_layout on the debug page.
int ControllersButtonLayoutValue() {
  return g_on ? ButtonLayout() : kDebugCannot;
}

// The test key of button_layout. A button that is down now is not a new
// press for its new job, a ring that is open closes with no choice, and a
// stick that is pushed must come back before it walks. The hand that does
// not point gives a long pulse for 1 and a short pulse for 0.
void ControllersButtonLayoutToggle(const char* by) {
  if (!g_on) return;
  g_layout = ButtonLayout() != 0 ? 0 : 1;
  g_esc_before = true;
  g_idle_before = true;
  g_walk_blocked = true;
  RadialMenuLayoutChanged();
  ControllersGrabPulse(1, 0.5f, g_layout != 0 ? 0.30f : 0.08f);
  LogLayout(by);
}

// Hook the two functions. Only with the controller option on, so with it off
// the game runs as the plain game.
void ControllersInstall() {
  if (g_installed || !g_on) return;
  const bool a = Hook(kSetButtons, reinterpret_cast<void*>(&h_SetButtons),
                      reinterpret_cast<void**>(&o_SetButtons),
                      "LHMouse::SetButtons");
  const bool b = Hook(kAnyWindowMessages,
                      reinterpret_cast<void*>(&h_AnyWindowMessages),
                      reinterpret_cast<void**>(&o_AnyWindowMessages),
                      "AnyWindowMessages");
  g_installed = a && b;
  if (!g_installed) {
    // A half installed pair would call a null function. Turn both off.
    if (a) MH_DisableHook(reinterpret_cast<void*>(kSetButtons));
    if (b) MH_DisableHook(reinterpret_cast<void*>(kAnyWindowMessages));
  }
  if (g_installed) PatchFocusTest();
  // The game's own haptics stand apart from the buttons. Without them the
  // mouse and the buttons still work.
  if (g_installed && g_haptics) {
    g_haptics_hooked =
        Hook(kStartImmersion, reinterpret_cast<void*>(&h_StartImmersion),
             reinterpret_cast<void**>(&o_StartImmersion),
             "GInterface::StartImmersion");
  }
}

// Once for each pass of the game's message pump, on the game thread, just
// before the window procedure runs. It sends the buttons of the controller
// where the real ones arrive, and the next frame reads both.
void ControllersPump() {
  if (!g_installed) return;
  const ULONGLONG now = GetTickCount64();
  g_last_pump = now;
  if (!g_on) return;
  if (g_fake_deactivate) FakeDeactivate();
  TextClickPump();
  NoteDepthPump();
  PumpButtons(now);
  PumpStick(now);
  PumpWalk();
  PumpEsc(now);
  RadialMenuPump(now);
}

// A snap turn that the stick asked for has happened.
void ControllersSnapTurned() {
  ++g_turns_done;
  if (g_event_logs < kMaxEventLogs) {
    ++g_event_logs;
    ModLog("controllers  : the stick turned the camera, frame %llu", g_frame);
  }
  Pulse(PointingIndex());
}

// Once a report window.
void ControllersReport() {
  if (!g_on) return;
  if (g_fake.mode != 0 && g_fake.sweep) {
    g_sweep_turned = !g_sweep_turned;
    ModLog("controllers  : the fake hand is now at its %s pose",
           g_sweep_turned ? "second" : "first");
  }
  if (g_installed && (g_presses_sent != 0 || g_releases_sent != 0 ||
                      g_wheel_sent != 0 || g_turns_asked != 0)) {
    ModLog("controllers  : sent %llu presses (%llu double clicks), %llu "
           "releases, %llu jumps, %llu wheel steps, %llu turns asked and %llu "
           "done, %llu pulses, %llu presses dropped off view, %llu forced "
           "releases", g_presses_sent, g_doubles_sent, g_releases_sent,
           g_jumps_started, g_wheel_sent, g_turns_asked, g_turns_done,
           g_pulses, g_dropped, g_forced_releases);
    g_presses_sent = g_doubles_sent = g_releases_sent = g_jumps_started = 0;
    g_wheel_sent = g_turns_asked = g_turns_done = g_pulses = 0;
    g_dropped = g_forced_releases = 0;
  }
  RadialMenuReport();
  TempleReport();
  TextClickReport();
  if (g_god_zooms != 0 || g_god_holds != 0) {
    ModLog("controllers  : the stick asked the God view for %llu zoom steps "
           "and %llu smooth zooms", g_god_zooms, g_god_holds);
    g_god_zooms = g_god_holds = 0;
  }
  if (g_grab_starts != 0 || g_grab_claims != 0 || g_grab_refused != 0 ||
      g_grab_picks != 0 || g_grab_trigger_held != 0 ||
      g_grab_stick_refused != 0) {
    ModLog("grab         : %llu grabs by the other hand, %llu by the pointing "
           "grip and %llu pointing grips left to the game, %llu grips refused, "
           "%llu presses held back from the game, %llu stick pushes refused",
           g_grab_starts, g_grab_claims, g_grab_picks, g_grab_refused,
           g_grab_trigger_held, g_grab_stick_refused);
    g_grab_starts = g_grab_claims = g_grab_refused = g_grab_picks = 0;
    g_grab_trigger_held = g_grab_stick_refused = 0;
  }
  if (g_hold_carries != 0 || g_hold_let_go != 0 || g_hold_late != 0 ||
      g_hold_clicks != 0 || g_hold_held_on != 0 || g_hold_short != 0) {
    ModLog("grip         : %llu carries, %llu openings to let go, %llu late "
           "objects let go, %llu clicks sent, %llu closes on the object again, "
           "%llu clicks that waited for the other button. On an object that "
           "moves, the mod held %llu presses for the game, and %llu presses "
           "were a click for the game", g_hold_carries,
           g_hold_let_go, g_hold_late, g_hold_clicks, g_hold_regrips,
           g_hold_waited, g_hold_held_on, g_hold_short);
    g_hold_carries = g_hold_let_go = g_hold_late = g_hold_clicks = 0;
    g_hold_regrips = g_hold_waited = 0;
    g_hold_held_on = g_hold_short = 0;
  }
  if (g_hold_take_opens != 0 || g_hold_take_ends != 0 || g_fast_opens != 0 ||
      g_slow_opens != 0) {
    ModLog("grip         : %llu openings on a fast fall and %llu below 35%%. "
           "%llu openings ended a bulk take, and the mod had to end %llu "
           "takes, this window", g_fast_opens, g_slow_opens, g_hold_take_opens,
           g_hold_take_ends);
    g_hold_take_opens = g_hold_take_ends = 0;
    g_fast_opens = g_slow_opens = 0;
  }
  // The focus test of the camera, in a window in which the mod changed its
  // answer.
  if (g_focus_stood != 0) {
    ModLog("controllers  : the camera asked for the keyboard focus %llu "
           "times. %llu times the game window did not have it, and the "
           "answer was the game window all the same, because the controllers "
           "act with the window behind", g_focus_asked, g_focus_stood);
  }
  g_focus_asked = g_focus_stood = 0;
  if (g_installed && g_on &&
      (g_not_usable[0] || g_not_usable[1] || g_not_usable[2] ||
       g_not_usable[3])) {
    ModLog("controllers  : the hand cannot act as a mouse now: game window "
           "behind with nothing that stands for it %d, no scene %d, no hand "
           "data %d, hand not bound %d",
           g_not_usable[0], g_not_usable[1], g_not_usable[2], g_not_usable[3]);
  }
  if (g_frames_in_window == 0) return;
  const Pointing& p = g_point;
  const int index = g_left_handed ? 0 : 1;
  const char* name = index == 0 ? "left" : "right";
  if (!p.have_hand) {
    ModLog("controllers  : no %s hand", name);
  } else if (!p.have_world) {
    if (!g_said_no_head) {
      g_said_no_head = true;
      ModLog("controllers  : the %s hand is here, but the head has no pose "
             "yet", name);
    }
  } else {
    g_said_no_head = false;
    ModLog("controllers  : %s hand \"%s\", aim from %.2f %.2f %.2f toward "
           "%.3f %.3f %.3f, %.2f units from the eye", name, p.profile,
           p.origin[0], p.origin[1], p.origin[2], p.direction[0],
           p.direction[1], p.direction[2], p.eye_distance);
    if (p.have_pixel) {
      ModLog("controllers  : it would give the render pixel %.1f %.1f%s, "
             "target %.1f units out", p.x, p.y,
             p.off_view ? " (off the render)" : "", p.target_distance);
    } else {
      ModLog("controllers  : it would give no pixel (behind the head, or the "
             "game camera is not known)");
    }
    if (DirectMode() && p.have_grip) {
      const float scale = HeadPoseScale();
      ModLog("controllers  : direct hand: grip %.1f cm from the anchor%s, "
             "reach %.2f units (x%.2f), pixel %.1f %.1f%s, %s", scale > 0.0f
                 ? p.grip_distance / scale * 100.0f : 0.0f,
             p.grip_frozen ? " (not tracked, held)" : "",
             p.grip_distance * p.reach_factor, p.reach_factor, p.hand_x,
             p.hand_y, !p.have_hand_pixel ? " (none)"
                       : p.hand_off_view ? " (off the render)" : "",
             g_pointer_owned ? "the controller owns the Hand"
                             : "the mouse owns the Hand");
      if (p.table) {
        ModLog("controllers  : on the table, with no reach curve: the grip is "
               "at %.1f %.1f %.1f in the world", p.grip[0], p.grip[1],
               p.grip[2]);
      }
    }
  }
  g_frames_in_window = 0;
}

// For the intro skip (intro_skip.cpp): the triggers and the face buttons
// that are down, as the last ControllersFrame read them. Bit 0 is the left
// trigger, bit 1 is X and bit 2 is Y. Bits 3 to 5 are the right trigger, A
// and B. `awake` gets bit 0 for an active left hand and bit 1 for the right
// hand. The grip is not in it.
unsigned ControllersButtonBits(unsigned* awake) {
  unsigned bits = 0;
  for (int i = 0; i < 2; ++i) {
    const bwvr::HandInput& hand = g_hands[i];
    if (!hand.active) continue;
    *awake |= 1u << i;
    if (hand.trigger >= 0.5f) bits |= 1u << (i * 3);
    if (hand.primary) bits |= 2u << (i * 3);
    if (hand.secondary) bits |= 4u << (i * 3);
  }
  return bits;
}
