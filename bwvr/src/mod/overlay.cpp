// The overlay renderer: the mod's own 3D meshes, drawn inside the game's
// frame. See docs/hand-model.md, "Overlay renderer".
//
// WHY. The glow (hand_glow.cpp) draws its own geometry in each eye pass, at
// the right depth, against the game's depth buffer. This file is a small
// renderer for meshes: hand models first, and later the controller models,
// rays and the rim of the table.
//
// THE MESHES. Once a frame, OverlayFrame builds one list of triangles in
// world space, with a lit color at each corner, and both eye passes draw that
// list. This spike has one mesh: a test hand at each controller's grip pose
// (overlay_math.h, BuildHand). It is the real hand's length in the room
// (hand_real_cm times hand_scale_pct) at the scale now, so it is one to one
// in place and in size. The index finger bends with the trigger, and the
// middle, ring and little fingers with the grip. The game's own Hand still
// draws. The log puts the two side by side.
//
// THE LIGHT. A fixed light from straight above and some ambient, from the
// world normals, once a frame on the CPU. The color does not depend on the
// eye, so both eyes see the same shading.
//
// EACH EYE. sky_detail.cpp calls OverlayDraw after the sun glare and before
// the glow, at 0x0082F4C9 in LH3DRender::FinishFrame. There the world and its
// depth are drawn, and the interface is not. Later FinishFrame resets the
// depth to 1.0 with a screen quad, before the interface draws
// (docs/tabletop.md, 4.4). So the depth that the hand writes does not reach
// the interface. The draw takes the camera of this eye (OverlayEyeMatrix),
// moves each corner into camera space on the CPU, cuts each triangle at the
// near plane, and projects it as LH3DTech::ProjectPoint does, with
// rhw = near / depth and z = 1 - rhw (overlay_math.h). So the depth test
// compares the hand with the game's own depth, and land in front hides it.
// The vertices have the game's format 0x1C4 (XYZRHW), so the draw needs no
// shader and no transform state.
//
// THE DEVICE. As the glow does: the game's Direct3D 7 device, and one
// DrawPrimitive for each eye. The draw reads each state before it changes it
// and puts back the value it read, in the reverse order. dxwrapper keeps its
// own copy of each state and does not compare it with the device, and the
// game skips a call when its own copy agrees, so a state left changed would
// stay wrong. The hand writes depth, so its fingers hide its palm. A state
// that the device does not give is not changed.
//
// ONCE. OverlayFrame runs once a frame, before the eye passes (stereo.cpp),
// and the draw runs at most once in each eye pass of a frame.
//
// WHEN. The hands draw only while the controller owns the pointer, and each
// hand only while its pose is tracked. With overlay=0, hand_exact=0 and
// nothing to show, the overlay does not draw and changes nothing.
//
// THE EXACT HAND (docs/hand.md, "The exact Hand"). With hand_exact=1 the
// game's own Hand is the pointing hand, so with overlay=0 the overlay draws
// the other hand only: the left hand, or the right hand for a left handed
// player. With overlay=1 both hands draw, so the two right hands can be
// compared.
//
// ONE PAIR OF HANDS (docs/hand.md, "The two real hands and
// which one is the Hand"). With hand_hide_game=1
// (the default) hand_exact.cpp hides the mesh of the game's Hand, and the
// overlay draws both hands with overlay=0 too. Each hand has a pose
// (overlay_math.h, THE POSES) that follows what the hand does:
//
//   hold     the pointing hand, while the game's Hand holds an object (the
//            Holding state, the Tug state, and a Miracle in the Hand)
//   point    the pointing hand, while the teleport aims, a far grab has a
//            target, or the ray of the menu shows. Another source asks for
//            it with OverlayHandPoints
//   flat     the pointing hand in the Creature state (a stroke), with the
//            grip open
//   fist     the land drag (the Camera state), and a grip that is closed on
//            nothing, with either hand
//   relaxed  each other time
//
// The pose is chosen once a frame from the state of the last frame. A change
// of pose takes 0.1 s, and the trigger and the grip move the fingers in each
// pose with no lag. The skin takes the color of the player's alignment
// (CHand+0x9C): pale for good, dark and red for evil.
//
// THE TELEPORT. The arc and the ring of the teleport (teleport.cpp) are a
// second source with its own switch: they draw while the player aims, with
// overlay=0 too. A tube of 6 sides, 6 mm of the room across, and a ring on
// the land that follow its height. Cyan means a valid target and red means a
// target that is not valid. They are opaque, with the depth test and the
// depth write on, as the hands are.
//
// THE MESH OF THE GAME (docs/hand-model.md, "The game's hand on both real hands").
// With hand_model=1 (the default) the hands are the game's own hand mesh,
// with its textures and its light (hand_model.cpp). This file still chooses
// the pose and the curl of each finger, and the low poly hand below is the
// hand of hand_model=0 and of a game folder with no hand files.
//
// THE FINGERS AND THE WORLD (docs/hand-model.md, "Hands that react to the world").
// After the curls of the pose are known, hand_feel.cpp changes them: a finger
// bends on the land and against a body, the pointing hand opens toward a
// target of the grip, and the hold pose closes on the body of the object in
// the Hand. With hand_feel=0 the curls of the pose stay.
//
// THE FINGERS FROM OUTSIDE. OverlaySetFinger gives one finger of one hand a
// curl and a spread for the frame, in place of the curl that the poses give.
// No file of the mod calls it.
//
// THE INTRO (docs/intro.md, "The hand models in the intro"). The Lionhead
// logo in stereo has no scene walk, so intro_vr.cpp calls OverlayIntroFrame
// in place of OverlayFrame, and OverlayPassEye around its eye passes. That
// frame builds the hands only, with no context of the game and no fingers
// that feel the world, and each hand has an opacity (HandModelSetOpacity).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "../common/bw_addresses.h"
#include "controllers.h"
#include "creature_command.h"
#include "far_grab.h"
#include "hand_exact.h"
#include "hand_feel.h"
#include "hand_model.h"
#include "overlay_math.h"
#include "teleport.h"

extern void ModLog(const char* format, ...);

// stereo.cpp: give the engine the camera of the eye pass that is running.
void StereoReapplyEye();
// controllers.cpp: the controller owns the pointer, and the real hand's
// length in cm (hand_real_cm) and the percent of it the Hand gets.
bool ControllersOwnPointer();
// controllers.cpp: the ray of the pointing hand while a dialog box is up.
bool ControllersMenuRay(float from[3], float to[3], float* units);
int ControllersHandCm();
int ControllersHandScalePct();
// hand_direct.cpp: the game's Hand position, and whether the controller
// places the Hand.
bool HandDirectHandPosition(float out[3], bool* by_controller);
// hand_direct.cpp: the hand of a stroke of the Creature (0 none, 1 the flat
// hand, 2 the index finger, 3 a fist while the grip is closed), and what the tickle
// adds to each finger's curl.
int HandDirectStrokeHand(float curl[5]);
// hand_exact.cpp: the game's Hand is the pointing hand.
bool HandExactOn();

namespace {

using overlay_math::ScreenVertex;
using overlay_math::Triangle;

bool g_on = false;

const int kHands = 2;
// THE INTRO (intro_vr.cpp, OverlayIntroFrame): the hands of the Lionhead
// logo in stereo. There is no world and no Hand of the game yet, so the
// build has no context of the game and no fingers that feel the world, and
// each hand has an opacity.
bool g_intro = false;
bool g_intro_built = false;
float g_intro_opacity[kHands] = {1.0f, 1.0f};
// The tether of a far grab is a tube of one segment. The room of a ring
// under a far target stays as spare room.
const int kFarMaxTriangles = overlay_math::kRingMaxTriangles +
                             overlay_math::kTubeSides * 2;
const int kMaxTriangles = kHands * overlay_math::kHandTriangles +
                          overlay_math::kTeleportMaxTriangles +
                          kFarMaxTriangles;
// The cut at the near plane makes at most two triangles of one.
const int kMaxVertices = kMaxTriangles * 2 * 3;
const uint32_t kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR |
                      D3DFVF_TEX1;  // 0x1C4, as the game draws

// A plain skin color, and the share of it that the ambient light gives.
const float kSkin[3] = {0.90f, 0.70f, 0.57f};
const float kAmbient = 0.35f;
// The teleport: cyan for a valid target, red for one that is not. The light
// has more ambient, so the arc stays bright from every side.
const float kArcValid[3] = {0.10f, 0.85f, 1.00f};
const float kArcInvalid[3] = {1.00f, 0.15f, 0.10f};
// The arc of a Creature command: green, the color of the trigger.
const float kCommandValid[3] = {0.27f, 1.00f, 0.59f};
const float kArcAmbient = 0.60f;
const float kTubeRadiusM = 0.006f;
const float kMenuRay[3] = {1.00f, 0.95f, 0.75f};
// The tether of the lock of a far grab: gold.
const float kFarGold[3] = {1.00f, 0.72f, 0.16f};
const float kFarTubeRadiusM = 0.0015f;  // 3 mm across
int g_far_triangles = 0;
const float kMenuRayRadiusM = 0.002f;
// The mark at the end of the ray: from its middle to an end it is 0.4 percent
// of the length of the ray, and 6 mm of the room at the least.
const float kMenuRayEndM = 0.006f;
const float kMenuRayEndShare = 0.004f;
int g_menu_ray_triangles = 0;
const float kRingInner = 0.75f;
const float kRingLiftM = 0.02f;

struct Vertex {
  float x, y, z, rhw;
  uint32_t diffuse, specular;
  float u, v;
};
static_assert(sizeof(Vertex) == 32, "the game's vertex is 32 bytes");

// THE WORLD MESH of this frame, with a color at each corner.
Triangle g_hand_local[overlay_math::kHandTriangles];
Triangle g_world[kMaxTriangles];
float g_color[kMaxTriangles][3][3];
int g_count = 0;
int g_teleport_triangles = 0;
Vertex g_vertices[kMaxVertices];

// THE STATES the draw changes. The depth buffer stays on as the game has it.
// Only when it is off does the draw turn it on.
struct RenderSet {
  D3DRENDERSTATETYPE state;
  DWORD value;
};
const RenderSet kRender[] = {
    {D3DRENDERSTATE_ZENABLE, D3DZB_TRUE},
    {D3DRENDERSTATE_ZWRITEENABLE, TRUE},
    {D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL},
    {D3DRENDERSTATE_ALPHABLENDENABLE, FALSE},
    {D3DRENDERSTATE_ALPHATESTENABLE, FALSE},
    {D3DRENDERSTATE_FOGENABLE, FALSE},
    {D3DRENDERSTATE_CULLMODE, D3DCULL_NONE},
    {D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD},
    {D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID},
    {D3DRENDERSTATE_STENCILENABLE, FALSE},
    {D3DRENDERSTATE_COLORKEYENABLE, FALSE},
};
const int kRenderCount = sizeof(kRender) / sizeof(kRender[0]);

struct StageSet {
  DWORD stage;
  D3DTEXTURESTAGESTATETYPE state;
  DWORD value;
};
// Stage 0 takes the color of the vertices only, and stage 1 off turns off
// every stage after stage 0. The texture stays bound.
const StageSet kStage[] = {
    {0, D3DTSS_COLOROP, D3DTOP_SELECTARG1},
    {0, D3DTSS_COLORARG1, D3DTA_DIFFUSE},
    {0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1},
    {0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE},
    {1, D3DTSS_COLOROP, D3DTOP_DISABLE},
};
const int kStageCount = sizeof(kStage) / sizeof(kStage[0]);

DWORD g_render_was[kRenderCount];
bool g_render_changed[kRenderCount];
DWORD g_stage_was[kStageCount];
bool g_stage_changed[kStageCount];
bool g_said_unread = false;
bool g_said_failed = false;

// The frame, the eye pass and the draws of each eye in this frame.
uint64_t g_frame = 0;
int g_eye = -1;
uint64_t g_asked_frame[2] = {0, 0};
uint64_t g_drawn_frame[2] = {0, 0};

// For the log: the hands of this frame.
bool g_hand_valid[kHands] = {false, false};
float g_grip[kHands][3];
float g_index_tip[kHands][3];
float g_trigger[kHands] = {0.0f, 0.0f};
float g_squeeze[kHands] = {0.0f, 0.0f};
int g_pointing = 1;
float g_units_per_meter = 0.0f;
float g_length = 0.0f;
// The index tip on the screen in each eye, for each hand.
bool g_tip_seen[2][kHands];
float g_tip_screen[2][kHands][2];
// Each draw's triangles, for the cost line.
int g_eye_triangles[2] = {0, 0};

// THE POSES. The weight of each pose for each hand, the pose that the hand
// goes to, and whether its grip counts as closed on nothing.
float g_pose_weight[kHands][overlay_math::kPoseCount] = {
    {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
int g_pose[kHands] = {overlay_math::kPoseRelaxed, overlay_math::kPoseRelaxed};
bool g_grip_closed[kHands] = {false, false};
// A grip counts as closed from kFistOn, until it opens to kFistOff.
const float kFistOn = 0.85f;
const float kFistOff = 0.60f;
// The radius of a held body with no known size, in hand lengths.
const float kHoldDefaultRadius = 0.20f;
// The game's hand states that the poses use (docs/hand.md, "The state machine").
const int kStateCamera = 2;
const int kStateTug = 3;
const int kStateHolding = 4;
const int kStateCreature = 7;
const int kStateGrain = 8;
// The frame of the last request for the point pose, for each hand.
uint64_t g_point_asked[kHands] = {0, 0};
bool g_have_point_ask[kHands] = {false, false};
int64_t g_pose_ticks = 0;
bool g_pointing_drawn = false;
int g_pose_logs = 0;
const int kMaxPoseLogs = 60;
const char* const kPoseName[overlay_math::kPoseCount] = {
    "relaxed", "hold", "point", "flat", "fist"};

// THE FINGERS FROM OUTSIDE (OverlaySetFinger): the curl, the spread and the
// weight of each finger of each hand, and the frame of the request.
struct FingerAsk {
  uint64_t frame = 0;
  bool have = false;
  float curl = 0.0f;
  float spread = 0.0f;
  float weight = 0.0f;
};
FingerAsk g_finger_ask[kHands][overlay_math::kFingers];
// The hands of this frame are the mesh of the game (hand_model.cpp).
bool g_mesh_hands = false;
bool g_said_tip = false;
// The place and the pose of each hand as it was built last, for
// OverlayFingerJoints and OverlayPoseCurls.
struct HandPlace {
  uint64_t frame = 0;
  bool have = false;
  bool mesh = false;
  float position[3];
  float rotation[9];
  float length = 0.0f;
  float curls[overlay_math::kFingers];
};
HandPlace g_hand_place[kHands];

bool g_said_first = false;
bool g_said_owner = false;
int g_owner_logs = 0;
const int kMaxOwnerLogs = 20;
int g_hand_logs = 0;
const int kMaxHandLogs = 40;
const uint64_t kSampleFrames = 900;
const uint64_t kGapFrames = 30;
uint64_t g_logged_frame = 0;
float g_logged_trigger = -1.0f;
float g_logged_squeeze = -1.0f;
float g_logged_tip[2] = {-1e9f, -1e9f};

// THE COST, on the CPU: the build of this frame and the draw of each eye.
LARGE_INTEGER g_frequency = {};
int64_t g_build_ticks = 0;
int64_t g_eye_ticks[2] = {0, 0};
int64_t g_cost_ticks = 0;
int64_t g_cost_build_ticks = 0;
int64_t g_worst_ticks = 0;
uint64_t g_cost_frames = 0;
uint64_t g_cost_triangles = 0;
bool g_said_cost = false;
const uint64_t kCostFrames = 300;

// For the report.
uint64_t g_drawn = 0;
uint64_t g_triangles = 0;
uint64_t g_cut = 0;
uint64_t g_off_screen = 0;
uint64_t g_nothing = 0;
uint64_t g_failed = 0;

int64_t Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

double Ms(int64_t ticks) {
  if (g_frequency.QuadPart == 0) return 0.0;
  return static_cast<double>(ticks) * 1000.0 /
         static_cast<double>(g_frequency.QuadPart);
}

uint32_t Color(const float rgb[3]) {
  uint32_t out = 0xFF000000u;
  for (int k = 0; k < 3; ++k) {
    float v = rgb[k] * 255.0f;
    if (!(v > 0.0f)) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    out |= static_cast<uint32_t>(v + 0.5f) << (16 - 8 * k);
  }
  return out;
}

void SetStates(IDirect3DDevice7* device) {
  int unread = 0;
  for (int i = 0; i < kRenderCount; ++i) {
    g_render_changed[i] = false;
    DWORD was = 0;
    if (FAILED(device->GetRenderState(kRender[i].state, &was))) {
      ++unread;
      continue;
    }
    DWORD want = kRender[i].value;
    if (kRender[i].state == D3DRENDERSTATE_ZENABLE && was != D3DZB_FALSE) {
      want = was;
    }
    if (was == want) continue;
    if (SUCCEEDED(device->SetRenderState(kRender[i].state, want))) {
      g_render_was[i] = was;
      g_render_changed[i] = true;
    }
  }
  for (int i = 0; i < kStageCount; ++i) {
    g_stage_changed[i] = false;
    DWORD was = 0;
    if (FAILED(device->GetTextureStageState(kStage[i].stage, kStage[i].state,
                                            &was))) {
      ++unread;
      continue;
    }
    if (was == kStage[i].value) continue;
    if (SUCCEEDED(device->SetTextureStageState(kStage[i].stage,
                                               kStage[i].state,
                                               kStage[i].value))) {
      g_stage_was[i] = was;
      g_stage_changed[i] = true;
    }
  }
  if (unread > 0 && !g_said_unread) {
    g_said_unread = true;
    ModLog("overlay      : the device did not give %d states, so the overlay "
           "leaves them as they are", unread);
  }
}

// In the reverse order.
void PutStatesBack(IDirect3DDevice7* device) {
  for (int i = kStageCount - 1; i >= 0; --i) {
    if (!g_stage_changed[i]) continue;
    device->SetTextureStageState(kStage[i].stage, kStage[i].state,
                                 g_stage_was[i]);
    g_stage_changed[i] = false;
  }
  for (int i = kRenderCount - 1; i >= 0; --i) {
    if (!g_render_changed[i]) continue;
    device->SetRenderState(kRender[i].state, g_render_was[i]);
    g_render_changed[i] = false;
  }
}

// THE POSE that a hand goes to in this frame. The pointing hand takes it from
// the game's Hand and from the mod's own aims. The other hand has only its
// grip, and a request of another source.
int ChoosePose(int h, bool pointing, float squeeze, bool have_context,
               const HandExactContext& context) {
  if (squeeze >= kFistOn) g_grip_closed[h] = true;
  if (squeeze <= kFistOff) g_grip_closed[h] = false;
  const bool asked = g_have_point_ask[h] && g_frame <= g_point_asked[h] + 1;
  if (pointing && have_context && context.placed) {
    const int state = context.state;
    if (context.holding && (state == kStateHolding || state == kStateTug ||
                            state == kStateGrain)) {
      return overlay_math::kPoseHold;
    }
    TeleportDraw teleport;
    FarDraw far_draw;
    float from[3], to[3], units = 0.0f;
    if (asked || (TeleportDrawState(&teleport) && teleport.active) ||
        (FarGrabDrawState(&far_draw) && far_draw.active) ||
        ControllersMenuRay(from, to, &units)) {
      return overlay_math::kPosePoint;
    }
    if (state == kStateCreature) {
      // THE OPEN HAND ON THE CREATURE. The grip holds the Creature state, so
      // a closed grip is no fist here: the player strokes with an open hand.
      // The flat hand for a Creature that is large against the hand, and the
      // index finger for a small one (hand_direct.cpp, TouchFrame).
      // creature_stroke_hand=3 is a fist while the grip is closed.
      float tickle[5];
      const int stroke = HandDirectStrokeHand(tickle);
      if (stroke == 3) {
        return g_grip_closed[h] ? overlay_math::kPoseFist
                                : overlay_math::kPoseFlat;
      }
      return stroke == 2 ? overlay_math::kPosePoint : overlay_math::kPoseFlat;
    }
    {
      // THE WAIT BEFORE THE CREATURE STATE: a press on the Creature that is
      // a stroke has the open hand already (hand_direct.cpp, StrokeHand).
      float wait[5];
      const int stroke = HandDirectStrokeHand(wait);
      if (stroke == 1) return overlay_math::kPoseFlat;
      if (stroke == 2) return overlay_math::kPosePoint;
    }
    if (state == kStateCamera) return overlay_math::kPoseFist;
  } else if (asked) {
    return overlay_math::kPosePoint;
  } else if (pointing) {
    // The game's Hand is not the controller's now, for example in a menu.
    float from[3], to[3], units = 0.0f;
    if (ControllersMenuRay(from, to, &units)) return overlay_math::kPosePoint;
  }
  return g_grip_closed[h] ? overlay_math::kPoseFist
                          : overlay_math::kPoseRelaxed;
}

// The hands of this frame: a mesh at each grip, lit. False when nothing is
// to be drawn.
bool BuildFrame() {
  for (int h = 0; h < kHands; ++h) g_hand_valid[h] = false;
  // The time since the last frame, for the blend of the poses. A long gap
  // counts as one short step.
  const int64_t ticks = Now();
  float dt = g_pose_ticks != 0 ? static_cast<float>(Ms(ticks - g_pose_ticks)) *
                                     0.001f
                               : 0.0f;
  g_pose_ticks = ticks;
  if (!(dt > 0.0f)) dt = 0.0f;
  if (dt > 0.05f) dt = 0.05f;
  if (!ControllersOwnPointer()) {
    if (g_said_owner) {
      g_said_owner = false;
      if (++g_owner_logs <= kMaxOwnerLogs) {
        ModLog("overlay      : the mouse owns the pointer, so no hand is "
               "drawn, frame %llu", g_frame);
      }
    }
    return false;
  }
  OverlayHands hands;
  if (!ControllersOverlayHands(&hands)) return false;
  if (!g_said_owner) {
    g_said_owner = true;
    if (++g_owner_logs <= kMaxOwnerLogs) {
      ModLog("overlay      : the controller owns the pointer, so the hands "
             "are drawn, frame %llu", g_frame);
    }
  }
  g_pointing = hands.pointing;
  g_units_per_meter = hands.units_per_meter;
  g_length = ControllersHandCm() * 0.01f * ControllersHandScalePct() * 0.01f *
             hands.units_per_meter;
  if (!(g_length > 0.0f)) return false;
  // With the exact Hand and overlay=0, only the other hand. With the mesh of
  // the game's Hand hidden, both hands.
  const bool other_only =
      !g_intro && !g_on && HandExactOn() && !HandExactHidesGame();
  HandExactContext context;
  memset(&context, 0, sizeof(context));
  const bool have_context = !g_intro && HandExactGetContext(&context);
  // The color of the skin: the player's alignment, or the plain skin.
  float skin[3] = {kSkin[0], kSkin[1], kSkin[2]};
  if (have_context && context.have_alignment && HandExactTint()) {
    overlay_math::SkinTint(context.alignment, skin);
  }
  // The mesh of the game: the look of this frame, and its test keys.
  HandModelFrame(g_frame, have_context && context.have_alignment,
                 have_context ? context.alignment : 0.0f);
  const bool mesh = HandModelActive();
  overlay_math::PoseRange hold = overlay_math::kPoseRanges[
      overlay_math::kPoseHold];
  if (have_context && context.holding) {
    const float radius = context.hold_radius > 0.0f
                             ? context.hold_radius / g_length
                             : kHoldDefaultRadius;
    // The game's own hold pose for the object and the contact with its body
    // (hand_feel.cpp). With hand_feel=0, each finger closes on the ball of
    // the object with its own joints, so the mesh and the low poly hand each
    // have their own contact.
    if (!HandFeelHoldRange(hands.pointing, mesh, g_length, radius * g_length,
                           &hold)) {
      if (mesh) {
        HandModelHoldRange(radius, &hold);
      } else {
        overlay_math::HoldRange(radius, &hold);
      }
    }
  }
  for (int h = 0; h < kHands; ++h) {
    const OverlayHand& hand = hands.hand[h];
    if (!hand.valid) continue;
    if (other_only && h == hands.pointing) continue;
    // A hand of the intro that does not show is not built. The low poly hand
    // has no alpha, so it shows only from half the opacity.
    if (g_intro && g_intro_opacity[h] < (mesh ? 0.01f : 0.5f)) continue;
    const bool pointing = h == hands.pointing;
    const int pose =
        ChoosePose(h, pointing, hand.squeeze, have_context, context);
    if (pose != g_pose[h]) {
      g_pose[h] = pose;
      if (g_pose_logs < kMaxPoseLogs) {
        ++g_pose_logs;
        ModLog("overlay      : the %s hand takes the pose %s, frame %llu. The "
               "game's Hand: state %d, animation %d, %s, a body of %.2f units. "
               "Trigger %.2f, grip %.2f, alignment %.2f",
               h == 0 ? "left" : "right", kPoseName[pose], g_frame,
               have_context ? context.state : -1,
               have_context ? context.animation : -1,
               have_context && context.holding ? "holds an object" : "empty",
               have_context ? context.hold_radius : 0.0f, hand.trigger,
               hand.squeeze, have_context ? context.alignment : 0.0f);
      }
    }
    overlay_math::StepWeights(g_pose_weight[h], pose, dt,
                              overlay_math::kPoseBlendSeconds);
    float curls[overlay_math::kFingers];
    overlay_math::PoseCurls(g_pose_weight[h], hold, hand.trigger,
                            hand.squeeze, curls);
    // THE TICKLE: while the pointing hand strokes the Creature, the fingers
    // move a little with the path of the real hand. A hand that is still has
    // still fingers.
    if (pointing && !g_intro && overlay_math::kFingers == 5) {
      float tickle[5];
      if (HandDirectStrokeHand(tickle) != 0) {
        for (int f = 0; f < 5; ++f) {
          curls[f] += tickle[f];
          if (curls[f] > 1.0f) curls[f] = 1.0f;
        }
      }
    }
    HandPlace& place = g_hand_place[h];
    place.frame = g_frame;
    place.have = true;
    place.mesh = false;
    place.length = g_length;
    for (int k = 0; k < 3; ++k) place.position[k] = hand.position[k];
    for (int k = 0; k < 9; ++k) place.rotation[k] = hand.rotation[k];
    for (int f = 0; f < overlay_math::kFingers; ++f) place.curls[f] = curls[f];
    // THE FINGERS AND THE WORLD: the land, a target of the grip and the
    // bodies near the hand change the curls of the pose (hand_feel.cpp).
    if (!g_intro) {
      HandFeelHand feel;
      feel.hand = h;
      feel.pointing = pointing;
      feel.placed = have_context && context.placed;
      float stroke_curl[5];
      feel.stroke = pointing && ((have_context &&
                                  context.state == kStateCreature) ||
                                 HandDirectStrokeHand(stroke_curl) != 0);
      feel.holding = pointing && have_context && context.holding;
      feel.relaxed = pose == overlay_math::kPoseRelaxed;
      feel.mesh = mesh;
      for (int k = 0; k < 3; ++k) feel.position[k] = hand.position[k];
      for (int k = 0; k < 9; ++k) feel.rotation[k] = hand.rotation[k];
      feel.length = g_length;
      feel.units_per_meter = hands.units_per_meter;
      feel.trigger = hand.trigger;
      feel.squeeze = hand.squeeze;
      feel.dt = dt;
      HandFeelCurls(feel, curls);
    }
    // THE FINGERS FROM OUTSIDE: a request of this frame or the frame before
    // takes its share of the curl of the finger, and gives its spread.
    hand_model::FingerPose fingers;
    hand_model::OpenPose(&fingers);
    for (int f = 0; f < overlay_math::kFingers; ++f) {
      const FingerAsk& ask = g_finger_ask[h][f];
      if (ask.have && g_frame <= ask.frame + 1) {
        curls[f] = overlay_math::Clamp01(curls[f] +
                                         ask.weight * (ask.curl - curls[f]));
        fingers.spread[f] = ask.weight * ask.spread;
      }
      fingers.curl[f] = curls[f];
    }
    const float index_curl = curls[1];
    const bool left = h == 0;
    float tip[3];
    overlay_math::FingerTip(1, index_curl, tip);
    overlay_math::PlacePoint(hand.position, hand.rotation, g_length, left, tip,
                             g_index_tip[h]);
    HandModelSetOpacity(h, g_intro ? g_intro_opacity[h] : 1.0f);
    if (mesh && HandModelBuild(h, hand.position, hand.rotation, g_length, left,
                               fingers)) {
      // The mesh of the hand model stands for the low poly hand. Its own
      // index tip is the tip for the log.
      g_mesh_hands = true;
      place.mesh = true;
      HandModelIndexTip(h, g_index_tip[h]);
      if (!g_said_tip && h == hands.pointing) {
        g_said_tip = true;
        float rest[3], rest_world[3];
        overlay_math::FingerTip(1, overlay_math::kRestCurl, rest);
        overlay_math::PlacePoint(hand.position, hand.rotation, g_length, left,
                                 rest, rest_world);
        const float* at = g_index_tip[h];
        const float d[3] = {at[0] - rest_world[0], at[1] - rest_world[1],
                            at[2] - rest_world[2]};
        ModLog("overlay      : the hands are a mesh of the hand model, frame "
               "%llu. The index tip of the mesh of the pointing hand is at "
               "%.2f %.2f %.2f with a curl of %.2f, %.2f cm of the room from "
               "the point of the Hand (the tip at the rest curl of %.2f)",
               g_frame, at[0], at[1], at[2], index_curl,
               g_units_per_meter > 0.0f
                   ? sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) /
                         g_units_per_meter * 100.0f
                   : 0.0f,
               overlay_math::kRestCurl);
      }
    } else {
      const int n = overlay_math::BuildHandPose(curls, g_hand_local);
      if (g_count + n > kMaxTriangles) break;
      overlay_math::PlaceTriangles(g_hand_local, n, hand.position,
                                   hand.rotation, g_length, left,
                                   &g_world[g_count]);
      for (int t = g_count; t < g_count + n; ++t) {
        for (int v = 0; v < 3; ++v) {
          const float level =
              overlay_math::LightLevel(g_world[t].n[v], kAmbient);
          for (int k = 0; k < 3; ++k) g_color[t][v][k] = skin[k] * level;
        }
      }
      g_count += n;
    }
    g_hand_valid[h] = true;
    g_trigger[h] = hand.trigger;
    g_squeeze[h] = hand.squeeze;
    for (int k = 0; k < 3; ++k) g_grip[h][k] = hand.position[k];
  }
  return g_count > 0 || g_mesh_hands;
}

// THE ARC AND THE RING of the teleport, after the hands. Nothing when the
// player does not aim.
void BuildTeleport() {
  g_teleport_triangles = 0;
  TeleportDraw d;
  if (!TeleportDrawState(&d)) return;
  const float units = d.units_per_meter;
  if (!(units > 0.0f)) return;
  const float* base = d.valid ? kArcValid : kArcInvalid;
  const int start = g_count;
  if (d.count >= 2) {
    g_count += overlay_math::BuildTube(d.points, d.count, kTubeRadiusM * units,
                                       &g_world[g_count],
                                       kMaxTriangles - g_count);
  }
  if (d.ring) {
    const float outer = d.ring_cm * 0.01f * units;
    g_count += overlay_math::BuildLandRing(
        d.target[0], d.target[2], outer * kRingInner, outer,
        kRingLiftM * units, d.valid,
        [](float x, float z) { return TeleportGround(x, z); },
        &g_world[g_count], kMaxTriangles - g_count);
  }
  for (int t = start; t < g_count; ++t) {
    for (int v = 0; v < 3; ++v) {
      const float level =
          overlay_math::LightLevel(g_world[t].n[v], kArcAmbient);
      for (int k = 0; k < 3; ++k) g_color[t][v][k] = base[k] * level;
    }
  }
  g_teleport_triangles = g_count - start;
}

// THE ARC AND THE RING OF A CREATURE COMMAND (creature_command.cpp). While
// the trigger is held with the leash on, the arc shows the land point where
// the Creature goes: green, the color of the trigger, or red for a point that
// is not land. It takes the room of the teleport's triangles, because the
// two never draw in the same frame. It draws with overlay=0 too.
void BuildCommand() {
  TeleportDraw d;
  if (g_teleport_triangles > 0 || !CreatureCommandDrawState(&d)) return;
  const float units = d.units_per_meter;
  if (!(units > 0.0f)) return;
  const float* base = d.valid ? kCommandValid : kArcInvalid;
  const int start = g_count;
  if (d.count >= 2) {
    g_count += overlay_math::BuildTube(d.points, d.count, kTubeRadiusM * units,
                                       &g_world[g_count],
                                       kMaxTriangles - g_count);
  }
  if (d.ring) {
    const float outer = d.ring_cm * 0.01f * units;
    g_count += overlay_math::BuildLandRing(
        d.target[0], d.target[2], outer * kRingInner, outer,
        kRingLiftM * units, d.valid,
        [](float x, float z) { return TeleportGround(x, z); },
        &g_world[g_count], kMaxTriangles - g_count);
  }
  for (int t = start; t < g_count; ++t) {
    for (int v = 0; v < 3; ++v) {
      const float level =
          overlay_math::LightLevel(g_world[t].n[v], kArcAmbient);
      for (int k = 0; k < 3; ++k) g_color[t][v][k] = base[k] * level;
    }
  }
  static bool said = false;
  if (!said && g_count > start) {
    said = true;
    ModLog("overlay      : the arc of a Creature command is drawn, %d "
           "triangles, frame %llu. The ring is at %.1f %.1f %.1f",
           g_count - start, g_frame, d.target[0], d.target[1], d.target[2]);
  }
}

// THE TETHER OF A FAR GRAB (far_grab.cpp). After the lock, and while the game
// takes from a far bulk source, a thin tube goes from the Hand's point to the
// object. It draws with overlay=0 too. Nothing is drawn on the land under the
// target: the halo of hand_glow.cpp shows the target, gold or red.
void BuildFar() {
  g_far_triangles = 0;
  FarDraw d;
  if (!FarGrabDrawState(&d)) return;
  if (!d.locked || !d.valid) return;
  const float units = d.units_per_meter;
  const int start = g_count;
  float line[2][3];
  for (int i = 0; i < 3; ++i) {
    line[0][i] = d.hand[i];
    line[1][i] = d.object[i];
  }
  g_count += overlay_math::BuildTube(line, 2, kFarTubeRadiusM * units,
                                     &g_world[g_count],
                                     kMaxTriangles - g_count);
  for (int t = start; t < g_count; ++t) {
    for (int v = 0; v < 3; ++v) {
      const float level =
          overlay_math::LightLevel(g_world[t].n[v], kArcAmbient);
      for (int k = 0; k < 3; ++k) g_color[t][v][k] = kFarGold[k] * level;
    }
  }
  g_far_triangles = g_count - start;
  static int said = 0;
  if (said < 5 && g_far_triangles > 0) {
    ++said;
    ModLog("overlay      : the tether of a far grab is drawn, %d triangles, "
           "frame %llu. The object is at %.1f %.1f %.1f. No ring is drawn on "
           "the land", g_far_triangles, g_frame, d.object[0], d.object[1],
           d.object[2]);
  }
}

// THE RAY OF THE MENU. While a dialog box is up, the pointing hand points like
// a mouse (controllers.cpp, ControllersMenuRay), and this thin line shows the
// player where it points. Warm white, 2 mm of the room across. It draws with
// overlay=0 too.
void BuildMenuRay() {
  g_menu_ray_triangles = 0;
  float line[2][3], units = 0.0f;
  if (!ControllersMenuRay(line[0], line[1], &units) || !(units > 0.0f)) return;
  const int start = g_count;
  g_count += overlay_math::BuildTube(line, 2, kMenuRayRadiusM * units,
                                     &g_world[g_count],
                                     kMaxTriangles - g_count);
  // A MARK AT THE END: a small cross across the ray. In a room of the Temple
  // the game's Hand does not show, so the end of the line is the cursor, and
  // a line of this width is hard to see on a wall some meters away. The cross
  // grows with the length of the ray, so it has the same size in the view at
  // each distance.
  {
    float d[3] = {line[1][0] - line[0][0], line[1][1] - line[0][1],
                  line[1][2] - line[0][2]};
    const float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length > 1e-3f * units) {
      for (int k = 0; k < 3; ++k) d[k] /= length;
      // Two directions across the ray: level, and up.
      float u[3] = {d[2], 0.0f, -d[0]};
      float ul = sqrtf(u[0] * u[0] + u[2] * u[2]);
      if (!(ul > 0.1f)) {
        u[0] = 1.0f;
        u[2] = 0.0f;
        ul = 1.0f;
      }
      for (int k = 0; k < 3; ++k) u[k] /= ul;
      const float w[3] = {d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2],
                          d[0] * u[1] - d[1] * u[0]};
      float size = kMenuRayEndShare * length;
      if (size < kMenuRayEndM * units) size = kMenuRayEndM * units;
      const float* across[2] = {u, w};
      for (int a = 0; a < 2; ++a) {
        float bar[2][3];
        for (int k = 0; k < 3; ++k) {
          bar[0][k] = line[1][k] - across[a][k] * size;
          bar[1][k] = line[1][k] + across[a][k] * size;
        }
        g_count += overlay_math::BuildTube(bar, 2, size / 3.0f,
                                           &g_world[g_count],
                                           kMaxTriangles - g_count);
      }
    }
  }
  for (int t = start; t < g_count; ++t) {
    for (int v = 0; v < 3; ++v) {
      const float level =
          overlay_math::LightLevel(g_world[t].n[v], kArcAmbient);
      for (int k = 0; k < 3; ++k) g_color[t][v][k] = kMenuRay[k] * level;
    }
  }
  g_menu_ray_triangles = g_count - start;
  static bool said = false;
  if (!said && g_menu_ray_triangles > 0) {
    said = true;
    ModLog("overlay      : the ray of the menu is drawn, %d triangles, frame "
           "%llu", g_menu_ray_triangles, g_frame);
  }
}

// The index tip of each hand on the screen of this eye, for the log.
void ProjectTips(int eye, const float m[12], float near_plane, float hx,
                 float hy) {
  const float white[3] = {1.0f, 1.0f, 1.0f};
  for (int h = 0; h < kHands; ++h) {
    g_tip_seen[eye][h] = false;
    if (!g_hand_valid[h]) continue;
    float c[3];
    overlay_math::ToCamera(m, g_index_tip[h], c);
    if (!(c[2] >= near_plane)) continue;
    ScreenVertex v;
    overlay_math::Project(c, white, near_plane, hx, hy, &v);
    g_tip_screen[eye][h][0] = v.x;
    g_tip_screen[eye][h][1] = v.y;
    g_tip_seen[eye][h] = true;
  }
}

// After both eyes of a frame: the overlay's hands beside the game's Hand. On
// the first draw, on a change of the pointing hand's trigger or grip, when its
// index tip moves on the screen, and every kSampleFrames frames.
void LogHands() {
  if (g_hand_logs >= kMaxHandLogs) return;
  const int p = g_pointing;
  const bool pointing = g_hand_valid[p];
  const bool first = g_logged_frame == 0;
  const bool pressed = pointing &&
                       (fabsf(g_trigger[p] - g_logged_trigger) > 0.05f ||
                        fabsf(g_squeeze[p] - g_logged_squeeze) > 0.05f);
  const bool moved =
      pointing && g_tip_seen[0][p] &&
      (fabsf(g_tip_screen[0][p][0] - g_logged_tip[0]) > 4.0f ||
       fabsf(g_tip_screen[0][p][1] - g_logged_tip[1]) > 4.0f);
  const bool sample = g_frame >= g_logged_frame + kSampleFrames;
  const bool due = g_frame >= g_logged_frame + kGapFrames;
  if (!first && !pressed && !(moved && due) && !sample) return;
  ++g_hand_logs;
  g_logged_frame = g_frame;
  if (pointing) {
    g_logged_trigger = g_trigger[p];
    g_logged_squeeze = g_squeeze[p];
    if (g_tip_seen[0][p]) {
      g_logged_tip[0] = g_tip_screen[0][p][0];
      g_logged_tip[1] = g_tip_screen[0][p][1];
    }
  }
  float game[3] = {0.0f, 0.0f, 0.0f};
  bool by_controller = false;
  const bool have_game = HandDirectHandPosition(game, &by_controller);
  for (int h = 0; h < kHands; ++h) {
    if (!g_hand_valid[h]) continue;
    const float* tip = g_index_tip[h];
    const float* grip = g_grip[h];
    const char* name = h == 0 ? "left" : "right";
    if (g_tip_seen[0][h] && g_tip_seen[1][h]) {
      ModLog("overlay      : frame %llu, %s hand, trigger %.2f, grip %.2f. "
             "Index tip on the left eye at %.1f %.1f, on the right eye at %.1f "
             "%.1f, %.1f px apart", g_frame, name, g_trigger[h], g_squeeze[h],
             g_tip_screen[0][h][0], g_tip_screen[0][h][1],
             g_tip_screen[1][h][0], g_tip_screen[1][h][1],
             g_tip_screen[0][h][0] - g_tip_screen[1][h][0]);
    } else {
      ModLog("overlay      : frame %llu, %s hand, trigger %.2f, grip %.2f. "
             "The index tip is not in front of both eyes", g_frame, name,
             g_trigger[h], g_squeeze[h]);
    }
    if (h == p && have_game) {
      const float dx = game[0] - tip[0], dy = game[1] - tip[1],
                  dz = game[2] - tip[2];
      const float apart = sqrtf(dx * dx + dy * dy + dz * dz);
      ModLog("overlay      : grip %.2f %.2f %.2f, index tip %.2f %.2f %.2f. "
             "The game's Hand (%s) at %.2f %.2f %.2f, %.1f cm of the room from "
             "the index tip. Hand %.2f units long, %.1f units a meter",
             grip[0], grip[1], grip[2], tip[0], tip[1], tip[2],
             by_controller ? "placed by the controller" : "placed by the game",
             game[0], game[1], game[2],
             g_units_per_meter > 0.0f ? apart / g_units_per_meter * 100.0f
                                      : 0.0f,
             g_length, g_units_per_meter);
    } else {
      ModLog("overlay      : grip %.2f %.2f %.2f, index tip %.2f %.2f %.2f",
             grip[0], grip[1], grip[2], tip[0], tip[1], tip[2]);
    }
  }
}

// After both eyes of a frame: the cost, once, as a mean over kCostFrames.
void CountCost() {
  const int64_t frame = g_build_ticks + g_eye_ticks[0] + g_eye_ticks[1];
  g_cost_ticks += frame;
  g_cost_build_ticks += g_build_ticks;
  if (frame > g_worst_ticks) g_worst_ticks = frame;
  g_cost_triangles +=
      static_cast<uint64_t>(g_eye_triangles[0] + g_eye_triangles[1]);
  ++g_cost_frames;
  if (g_said_cost || g_cost_frames < kCostFrames) return;
  g_said_cost = true;
  int hands = 0;
  for (int h = 0; h < kHands; ++h) hands += g_hand_valid[h] ? 1 : 0;
  const double frames = static_cast<double>(g_cost_frames);
  ModLog("overlay      : %d hands, %d triangles in the world, and %.0f drawn "
         "a frame in the two eyes. The overlay takes %.3f ms a frame on the "
         "CPU (the worst %.3f ms, %llu frames): %.3f ms to build and light "
         "the meshes, and %.3f ms to cut, project and draw both eyes",
         hands, g_count, static_cast<double>(g_cost_triangles) / frames,
         Ms(g_cost_ticks) / frames, Ms(g_worst_ticks), g_cost_frames,
         Ms(g_cost_build_ticks) / frames,
         Ms(g_cost_ticks - g_cost_build_ticks) / frames);
}

}  // namespace

// The world to camera matrix of this eye pass. The four camera matrices go
// back as they were, so nothing after the draw sees a change. An item of the
// sorted list can leave the camera without the eye offset (sky_detail.cpp,
// THE SUN GLARE), so this asks stereo.cpp for the camera of this eye first.
// hand_glow.cpp uses it too.
void OverlayEyeMatrix(float m[12]) {
  const size_t bytes = sizeof(float) * 12;
  float scaled[12], unscaled[12], copy[12], inverse[12];
  memcpy(scaled, reinterpret_cast<const void*>(bw::tech::kWorldToCamera),
         bytes);
  memcpy(unscaled,
         reinterpret_cast<const void*>(bw::tech::kWorldToCameraUnscaled),
         bytes);
  memcpy(copy, reinterpret_cast<const void*>(bw::tech::kWorldToCameraCopy),
         bytes);
  memcpy(inverse,
         reinterpret_cast<const void*>(bw::tech::kCameraToWorldInverse),
         bytes);
  StereoReapplyEye();
  memcpy(m, reinterpret_cast<const void*>(bw::tech::kWorldToCamera), bytes);
  memcpy(reinterpret_cast<void*>(bw::tech::kWorldToCamera), scaled, bytes);
  memcpy(reinterpret_cast<void*>(bw::tech::kWorldToCameraUnscaled), unscaled,
         bytes);
  memcpy(reinterpret_cast<void*>(bw::tech::kWorldToCameraCopy), copy, bytes);
  memcpy(reinterpret_cast<void*>(bw::tech::kCameraToWorldInverse), inverse,
         bytes);
}

void OverlaySetEnabled(bool on) {
  g_on = on;
  QueryPerformanceFrequency(&g_frequency);
  ModLog("overlay      : %s", on ? "the mod draws a test hand at each "
                                   "controller while the controller owns the "
                                   "pointer"
                                 : "off");
}

// stereo.cpp, once a frame, after the hands of the frame are known and
// before the eye passes.
void OverlayFrame() {
  g_intro_built = false;
  ++g_frame;
  g_count = 0;
  g_eye_triangles[0] = g_eye_triangles[1] = 0;
  g_eye_ticks[0] = g_eye_ticks[1] = 0;
  g_build_ticks = 0;
  g_teleport_triangles = 0;
  g_mesh_hands = false;
  for (int h = 0; h < kHands; ++h) g_hand_valid[h] = false;
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  const int64_t start = Now();
  HandFeelFrame();
  if (g_on || HandExactOn()) BuildFrame();
  g_pointing_drawn = g_hand_valid[g_pointing];
  BuildTeleport();
  BuildCommand();
  BuildMenuRay();
  BuildFar();
  g_build_ticks = Now() - start;
}

// intro_vr.cpp, once a frame of the Lionhead logo in stereo, before the eye
// passes: only the hands, each with its opacity (0 to 1). See g_intro.
void OverlayIntroFrame(const float opacity[2]) {
  ++g_frame;
  g_count = 0;
  g_eye_triangles[0] = g_eye_triangles[1] = 0;
  g_eye_ticks[0] = g_eye_ticks[1] = 0;
  g_teleport_triangles = 0;
  g_mesh_hands = false;
  for (int h = 0; h < kHands; ++h) {
    g_hand_valid[h] = false;
    g_intro_opacity[h] = opacity[h];
  }
  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  const int64_t start = Now();
  g_intro = true;
  BuildFrame();
  g_intro = false;
  g_intro_built = true;
  g_pointing_drawn = g_hand_valid[g_pointing];
  g_build_ticks = Now() - start;
}

// stereo.cpp, around each eye pass: the eye, and -1 after the pass.
void OverlayPassEye(int eye) { g_eye = eye; }

// For hand_exact.cpp: the overlay built the pointing hand in this frame, so
// the mesh of the game's Hand can stay hidden.
bool OverlayPointingHandDrawn() { return g_pointing_drawn; }

// For a source that aims with a hand (0 the left hand, 1 the right hand), for
// example a Creature command: the hand takes the point pose. Call it in each
// frame of the aim. The pose ends one frame after the last call.
void OverlayHandPoints(int hand) {
  if (hand < 0 || hand >= kHands) return;
  g_point_asked[hand] = g_frame;
  g_have_point_ask[hand] = true;
}

// For a feature that bends the fingers by what is near them: one finger of
// one hand (0 the left hand, 1 the right hand; finger 0 the thumb, 1 the
// index finger to 4 the little finger) takes this curl and this spread.
//
//   curl    0 an open finger, 1 a finger of a fist
//   spread  radians at the knuckle, plus toward the thumb side. Only the
//           mesh of the game shows it
//   weight  0 to 1: the share of this curl against the curl of the pose, so
//           a caller can fade its request in and out
//
// Call it in each frame of the request. It ends one frame after the last
// call, and the finger goes back to the curl of its pose.
void OverlaySetFinger(int hand, int finger, float curl, float spread,
                      float weight) {
  if (hand < 0 || hand >= kHands || finger < 0 ||
      finger >= overlay_math::kFingers) {
    return;
  }
  FingerAsk& ask = g_finger_ask[hand][finger];
  ask.frame = g_frame;
  ask.have = true;
  ask.curl = overlay_math::Clamp01(curl);
  ask.spread = spread;
  ask.weight = overlay_math::Clamp01(weight);
}

// For hand_feel.cpp, for the pictures of a desktop run: the pixel of the tip
// of the index finger of a hand in the picture of the left eye, as the last
// draw had it. False when the tip was not in front of that eye.
bool OverlayTipPixel(int hand, float out[2]) {
  if (hand < 0 || hand >= kHands || !g_tip_seen[0][hand]) return false;
  out[0] = g_tip_screen[0][hand][0];
  out[1] = g_tip_screen[0][hand][1];
  return true;
}

// For the same feature: the curl of each finger that the pose of a hand gave
// in the last frame, before a request of OverlaySetFinger. False when the
// hand was not drawn in this frame or the frame before.
bool OverlayPoseCurls(int hand, float curls[overlay_math::kFingers]) {
  if (hand < 0 || hand >= kHands) return false;
  const HandPlace& place = g_hand_place[hand];
  if (!place.have || g_frame > place.frame + 1) return false;
  for (int f = 0; f < overlay_math::kFingers; ++f) curls[f] = place.curls[f];
  return true;
}

// For the same feature: where one finger of a hand would be in the world with
// a curl and a spread, at the place where the hand was drawn last. The four
// points are the knuckle, the two joints and the tip (for the thumb: its
// base, its two joints and its tip). `radius` gets the radius of the finger
// in world units. A caller can try some curls and take the largest one at
// which the finger does not go into the land or into an object. False when
// the hand was not drawn in this frame or the frame before.
bool OverlayFingerJoints(int hand, int finger, float curl, float spread,
                         float joints[overlay_math::kJoints + 1][3],
                         float* radius) {
  if (hand < 0 || hand >= kHands || finger < 0 ||
      finger >= overlay_math::kFingers) {
    return false;
  }
  const HandPlace& place = g_hand_place[hand];
  if (!place.have || g_frame > place.frame + 1) return false;
  float local[overlay_math::kJoints + 1][3];
  if (!place.mesh || !HandModelFingerJoints(finger, curl, spread, local)) {
    overlay_math::FingerJoints(finger, curl, local);
  }
  for (int j = 0; j <= overlay_math::kJoints; ++j) {
    overlay_math::PlacePoint(place.position, place.rotation, place.length,
                             hand == 0, local[j], joints[j]);
  }
  if (radius != nullptr) {
    *radius = overlay_math::kShape[finger].radius * place.length;
  }
  return true;
}

// sky_detail.cpp, after the sun glare and before the glow, once in each
// FinishFrame. Draws only inside an eye pass, and at most once in each eye
// pass of a frame.
void OverlayDraw() {
  if (g_count == 0 && !g_mesh_hands) return;
  const int eye = g_eye;
  if (eye < 0 || eye > 1 || g_asked_frame[eye] == g_frame) return;
  g_asked_frame[eye] = g_frame;
  const int64_t start = Now();
  // The world of this eye pass is drawn, so the objects of the frame are
  // known: the bodies near each hand, for the fingers of the next frame.
  if (eye == 0 && !g_intro_built) HandFeelSnapshot();

  IDirect3DDevice7* device =
      *reinterpret_cast<IDirect3DDevice7* const*>(
          bw::render::kDirect3DDevice7);
  if (device == nullptr) return;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  const float near_plane =
      *reinterpret_cast<const float*>(bw::tech::kNearPlane);
  const float hx = info->half_res_x, hy = info->half_res_y;
  if (!(near_plane > 0.0f) || !(hx > 0.0f) || !(hy > 0.0f)) return;

  float m[12];
  OverlayEyeMatrix(m);
  int n = 0;
  int drawn = 0;
  for (int t = 0; t < g_count; ++t) {
    float c[3][3];
    bool cut = false;
    for (int v = 0; v < 3; ++v) {
      overlay_math::ToCamera(m, g_world[t].p[v], c[v]);
      if (c[v][2] < near_plane) cut = true;
    }
    ScreenVertex out[6];
    const int pieces = overlay_math::ClipTriangle(c, g_color[t], near_plane,
                                                  hx, hy, out);
    if (cut && pieces > 0) ++g_cut;
    for (int p = 0; p < pieces; ++p) {
      const ScreenVertex* corner = &out[p * 3];
      if (overlay_math::OffScreen(corner, hx, hy)) {
        ++g_off_screen;
        continue;
      }
      for (int v = 0; v < 3; ++v) {
        Vertex& d = g_vertices[n++];
        d.x = corner[v].x;
        d.y = corner[v].y;
        d.z = corner[v].z;
        d.rhw = corner[v].rhw;
        d.diffuse = Color(corner[v].color);
        d.specular = 0xFF000000u;  // no fog, if the fog were on
        d.u = 0.0f;
        d.v = 0.0f;
      }
      ++drawn;
    }
  }
  ProjectTips(eye, m, near_plane, hx, hy);
  HRESULT result = S_OK;
  if (n > 0) {
    SetStates(device);
    result = device->DrawPrimitive(D3DPT_TRIANGLELIST, kFvf, g_vertices,
                                   static_cast<DWORD>(n), 0);
    PutStatesBack(device);
  }
  if (g_teleport_triangles > 0) TeleportCountDraw(1, drawn);
  // The mesh of the game, after the opaque parts: its wrist fades, so what
  // is behind it must be there first.
  int mesh_drawn = 0;
  if (g_mesh_hands) {
    mesh_drawn = HandModelDraw(device, m, near_plane, hx, hy);
    if (mesh_drawn > 0) drawn += mesh_drawn;
  }
  g_eye_ticks[eye] = Now() - start;
  if (n == 0 && mesh_drawn <= 0) {
    ++g_nothing;
    return;
  }
  if (FAILED(result)) {
    ++g_failed;
    if (!g_said_failed) {
      g_said_failed = true;
      ModLog("overlay      : the draw failed (0x%08X)",
             static_cast<unsigned>(result));
    }
    return;
  }
  ++g_drawn;
  g_triangles += static_cast<uint64_t>(drawn);
  g_eye_triangles[eye] = drawn;
  g_drawn_frame[eye] = g_frame;
  if (!g_said_first && g_on) {
    g_said_first = true;
    ModLog("overlay      : the first hand is drawn, frame %llu, eye %d, %d "
           "triangles", g_frame, eye, drawn);
  }
  if (eye == 1 && g_drawn_frame[0] == g_frame) {
    CountCost();
    if (g_on) LogHands();
  }
}

// Once a report window.
void OverlayReport() {
  HandModelReport();
  HandFeelReport();
  if (g_drawn == 0 && g_nothing == 0 && g_failed == 0) return;
  ModLog("overlay      : drawn in %llu eye passes, %llu triangles. %llu "
         "triangles crossed the near plane, and %llu were off the screen. Not "
         "drawn: %llu passes with nothing in view, %llu failed",
         g_drawn, g_triangles, g_cut, g_off_screen, g_nothing, g_failed);
  g_drawn = g_triangles = g_cut = g_off_screen = g_nothing = g_failed = 0;
}
