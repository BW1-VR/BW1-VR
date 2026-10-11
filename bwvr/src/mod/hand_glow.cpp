// The glow at the Hand's point. See docs/hand-model.md, "The glow at the Hand's
// point".
//
// WHY. With the direct hand the Hand is the player's real hand, one to one.
// The game acts from one point of it. The render pick looks through the
// point's pixel (scrolls, signposts, buildings), and the mod's pick takes the
// nearest holdable object within a grab radius of the same point
// (hand_direct.cpp). The Hand's mesh does not show where that point is. So a
// small disc shows it, and its color says what a press does there now.
//
// WHAT IT SHOWS. The look says what a press of the grip does at the point
// now. hand_direct.cpp gives it (HandDirectPressKind), from the game's own
// interface hand mode (GInterface+0x3AC, the returns of the hand mode chooser
// fn_005D7F20 [V]) and from the object of the pick record:
//
//   9                          take: the grip picks the object up
//   0xD                        take: the grip takes from a pile, a field or
//                              a fish farm (the bulk take, docs/grab.md)
//   0xF, 0x10, 0x13, 0x1C,     activate: the grip acts on something that the
//   0x20, and 0x12 over an     Hand cannot take (a scroll, a house, the
//   object that the game taps  Creature, a bubble, a leash)
//   or over the Creature
//   anything else              nothing: the land, the story, and an object
//                              where a press does nothing
//   the Hand holds an object   a smaller, dimmer disc
//
// THE COLOR IS THE BUTTON. The grip is gold on the hint card and the trigger
// is green (hint_card.cpp). The game activates a scroll with the Action
// button, which is the grip: the right press FUN_005D1330 sends the tap, and
// the left press fn_005D1AB0 only grabs the land [V]. The grip also starts
// the touch of the Creature (the game's hint there is "Interact" for the
// right button). So the trigger acts on nothing at the Hand's point. The
// activate look is a paler gold than the take look, and not green, the
// color of the trigger.
//
// Only while the controller places the Hand in the Normal or the Holding
// state. With the mouse, or with hand_glow=0, nothing changes.
//
// HOW. A round disc that faces the eye, a few centimeters across in the
// room. Each eye pass projects the same world point with its own camera, as
// LH3DTech::ProjectPoint does, so the two discs fuse at the Hand's point. The
// size is in centimeters of the room at the current scale (the table and the
// God view have their own), not in pixels. Two layers: a disc in the color
// of the look with the game's usual alpha blend, which shows on bright and on
// dark ground, and a core of half its radius that adds light, so the middle
// glows. Both fade to nothing at their edge.
//
// WHERE. At the end of the world in each eye pass: sky_detail.cpp calls
// HandGlowDraw after the sun glare, at 0x0082F4C9 in LH3DRender::FinishFrame.
// There the world and the sorted list are drawn and the depth buffer still
// holds them, and the interface is not drawn yet (docs/tabletop.md, "Not built: the ring and the depth key").
//
// THE CAMERA. An item of the sorted list can leave the camera without the eye
// offset (sky_detail.cpp, THE SUN GLARE). So the draw asks stereo.cpp for the
// camera of this eye (StereoReapplyEye), reads the world to camera matrix,
// and puts the four camera matrices back as they were. The rest of
// FinishFrame sees no change. overlay.cpp (OverlayEyeMatrix) does this for
// the glow and for the overlay's meshes.
//
// THE DEVICE. The disc goes to the game's Direct3D 7 device, with the vertex
// format of the game's own draws (0x1C4), so dxwrapper sees it as it sees the
// game. The draw reads each render state and texture stage state before it
// changes it, and puts the value back after. The game keeps its own copy of
// some states (0x00ECA1F0, 0x00EC81F0, and the depth test at 0x00ECA24C) and
// skips a call when its copy agrees, so a state left changed would stay
// wrong. A state that the device does not give is not changed. The texture
// stays bound, and stage 0 takes its color from the vertices only.
//
// THE DEPTH. rhw = near / depth and z = 1 - rhw, as the game writes them
// (Draw3DScreenTriangle, 0x0081C5DD). The depth test uses a point half a
// Hand's length nearer to the eye than the Hand's point. So the Hand's own
// mesh does not hide the disc, and land or an object further in front does.
// The disc writes no depth. The screen position stays at the true point.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "one_walk.h"
#include "../common/bw_addresses.h"
#include "controller_math.h"
#include "creature_command.h"
#include "far_grab.h"
#include "miracle.h"
#include "village.h"
#include "near_math.h"

extern void ModLog(const char* format, ...);

// hand_direct.cpp: the Hand's point and what a press does there, latched
// once a frame.
bool HandDirectGlowFacts(float point[3], float* length, int32_t* mode,
                         bool* holding, int* eye, uint64_t* frame);
// hand_direct.cpp: what a press of the grip does at the point, as a
// near_math::Kind. Latched with the facts above.
int HandDirectPressKind();
// hand_direct.cpp: the eye pass and the frame, while the controller places the
// Hand.
bool HandDirectEyeFrame(int* eye, uint64_t* frame);
// hand_direct.cpp, one target (docs/grab.md, "One target"): the choice of
// this frame is made now, in the first eye pass. The target of the grip at
// the Hand's point, for its halo. And hand_glow_idle: 1 draws the glow also
// with no target and while the Hand holds an object.
void HandDirectChooseNow();
bool HandDirectNearTarget(float center[3], float* radius, float* half_height,
                          bool* acts);
bool HandDirectGlowIdle();
// overlay.cpp: the world to camera matrix of this eye pass, with the camera
// matrices put back as they were.
void OverlayEyeMatrix(float m[12]);
// tabletop.cpp: units per meter while the table or the God view owns the
// view, and 0 otherwise.
float TabletopViewScale();
// head_pose.cpp: units per meter at the life size scale.
float HeadPoseScale();

namespace {

using controller_math::GlowDisc;
using controller_math::GlowProject;

bool g_on = true;

enum Look { kLookNothing, kLookTake, kLookTouch, kLookHolding, kLooks };

struct Style {
  const char* name;
  float radius_cm;       // in the room
  uint8_t r, g, b;
  float alpha;           // of the middle of the disc
  float core;            // the light that the core adds in its middle
};

// Pale blue and small over nothing, gold and larger over something to take,
// a paler gold over something to activate, and small and faint while
// holding. Gold is the color of the grip, the button that acts in both.
const Style kStyles[kLooks] = {
    {"nothing under the Hand", 1.0f, 120, 200, 255, 0.55f, 0.55f},
    {"something to take", 1.7f, 255, 190, 40, 0.85f, 0.85f},
    {"something to activate", 1.5f, 255, 224, 120, 0.80f, 0.90f},
    {"the Hand holds an object", 0.8f, 120, 200, 255, 0.35f, 0.30f},
};

// kind is a near_math::Kind: what a press of the grip does at the point.
Look LookFor(int kind, bool holding) {
  if (holding) return kLookHolding;
  switch (kind) {
    case near_math::kHold:
    case near_math::kTake:
      return kLookTake;
    case near_math::kTap:
    case near_math::kCreature:
      return kLookTouch;
    default:
      return kLookNothing;
  }
}

// THE HALO of a far target (far_grab.cpp). A disc of the same shape, gold, or
// red when the Hand cannot take the object. It is centered on the object and
// larger than the object by 35 percent and 1 cm of the room. Its depth test
// uses a point one radius behind the center, so the object hides the middle
// of the disc and the light shows around the outline of the object.
const Style kHaloValid = {"a far target", 0.0f, 255, 190, 40, 0.50f, 0.65f};
const Style kHaloInvalid = {"a far target outside the influence", 0.0f, 255,
                            70, 50, 0.50f, 0.55f};
// THE HALO of a Creature command (creature_command.cpp): green, the color of
// the trigger. It shows around the object that the leashed Creature goes to
// when the trigger is pressed. A far target has the gold halo, also when the
// trigger can send the Creature to it.
const Style kHaloCommand = {"a target of the Creature", 0.0f, 70, 255, 150,
                            0.50f, 0.65f};
// THE HALO of the target of a miracle (miracle.cpp): gold, the color of the
// grip, because a press of the grip casts the miracle on that body.
const Style kHaloMiracle = {"the target of a miracle", 0.0f, 255, 190, 40,
                            0.50f, 0.65f};
// THE HALO of the thing that takes the object in the Hand (village.cpp): the
// same gold, because the opening of the grip puts the object on it.
const Style kHaloHold = {"the thing that takes the object in the Hand", 0.0f,
                         255, 190, 40, 0.50f, 0.65f};
const float kHaloGrow = 1.35f;
const float kHaloPlusCm = 1.0f;
const float kHaloMinCm = 2.0f;
uint64_t g_halo_frame[2] = {0, 0};
uint64_t g_halo_drawn = 0;

// THE DISC. A middle, a ring at kMiddle of the radius, and the edge. Three
// triangles for each segment: one from the middle to the ring, and two from
// the ring to the edge. The core is the same shape at kCore of the radius.
const int kSegments = 20;
const int kVertices = kSegments * 9;
const float kMiddle = 0.45f;
const float kRingAlpha = 0.7f;  // of the middle's alpha
const float kCore = 0.5f;
const uint32_t kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR |
                      D3DFVF_TEX1;  // 0x1C4, as the game draws

struct Vertex {
  float x, y, z, rhw;
  uint32_t diffuse, specular;
  float u, v;
};
static_assert(sizeof(Vertex) == 32, "the game's vertex is 32 bytes");

Vertex g_vertices[kVertices];
float g_cos[kSegments + 1];
float g_sin[kSegments + 1];
bool g_have_circle = false;

// THE STATES the draw changes. The depth buffer stays on as the game has it.
// Only when it is off does the draw turn it on.
struct RenderSet {
  D3DRENDERSTATETYPE state;
  DWORD value;
};
const RenderSet kRender[] = {
    {D3DRENDERSTATE_ZENABLE, D3DZB_TRUE},
    {D3DRENDERSTATE_ZWRITEENABLE, FALSE},
    {D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL},
    {D3DRENDERSTATE_ALPHABLENDENABLE, TRUE},
    {D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA},
    {D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA},
    {D3DRENDERSTATE_ALPHATESTENABLE, FALSE},
    {D3DRENDERSTATE_FOGENABLE, FALSE},
    {D3DRENDERSTATE_CULLMODE, D3DCULL_NONE},
    {D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD},
    {D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID},
    {D3DRENDERSTATE_STENCILENABLE, FALSE},
    {D3DRENDERSTATE_COLORKEYENABLE, FALSE},
};
const int kRenderCount = sizeof(kRender) / sizeof(kRender[0]);

// The index of a state in kRender, or -1.
int RenderIndex(D3DRENDERSTATETYPE state) {
  for (int i = 0; i < kRenderCount; ++i) {
    if (kRender[i].state == state) return i;
  }
  return -1;
}

struct StageSet {
  DWORD stage;
  D3DTEXTURESTAGESTATETYPE state;
  DWORD value;
};
// Stage 1 off turns off every stage after stage 0.
const StageSet kStage[] = {
    {0, D3DTSS_COLOROP, D3DTOP_SELECTARG1},
    {0, D3DTSS_COLORARG1, D3DTA_DIFFUSE},
    {0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1},
    {0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE},
    {1, D3DTSS_COLOROP, D3DTOP_DISABLE},
};
const int kStageCount = sizeof(kStage) / sizeof(kStage[0]);

DWORD g_render_was[kRenderCount];
bool g_render_read[kRenderCount];
bool g_render_changed[kRenderCount];
DWORD g_stage_was[kStageCount];
bool g_stage_changed[kStageCount];
bool g_said_unread = false;
bool g_said_failed = false;

// Once in each eye pass of a frame. g_asked_frame is the frame of the last
// try in each eye, and g_drawn_frame the frame of the last draw.
uint64_t g_asked_frame[2] = {0, 0};
uint64_t g_drawn_frame[2] = {0, 0};
GlowDisc g_disc[2];
Look g_look[2] = {kLookNothing, kLookNothing};
// Whether the glow of the look was drawn in each eye: with hand_glow_idle=0
// the looks with no target are not drawn.
bool g_shown[2] = {false, false};
uint64_t g_hidden[kLooks] = {0, 0, 0, 0};

// THE HAND'S POINT IN EACH EYE, for the hint at the Hand (draw_hooks.cpp,
// TooltipAtHand). Set in each eye pass, with the glow on or off: the pixel of
// the point in the render of that eye, and its depth in meters of the room.
struct EyePoint {
  bool valid;
  uint64_t frame;
  float x, y, meters;
};
EyePoint g_eye_point[2] = {};

// The world to camera matrix of each eye pass and the half size of the
// render, from the last frame that drew: the desktop test word "picture"
// finds the place of a pixel pair with them (HandGlowPicturePoint).
float g_eye_matrix[2][12] = {};
uint64_t g_eye_matrix_frame[2] = {0, 0};
float g_eye_half[2] = {0.0f, 0.0f};

// For the log and the report.
bool g_said_first = false;
Look g_logged_look = kLooks;
uint64_t g_next_sample = 0;
int g_look_logs = 0;
const int kMaxLookLogs = 40;
const uint64_t kSampleFrames = 900;
uint64_t g_drawn[kLooks] = {0, 0, 0, 0};
uint64_t g_outside = 0;
uint64_t g_too_near = 0;
uint64_t g_failed = 0;

float UnitsPerMeter() {
  const float table = TabletopViewScale();
  if (table > 0.0f) return table;
  const float life = HeadPoseScale();
  return life > 0.1f ? life : 7.5f;
}

uint32_t Color(float alpha, float r, float g, float b) {
  const auto channel = [](float v) {
    if (!(v > 0.0f)) return 0u;
    if (v >= 255.0f) return 255u;
    return static_cast<uint32_t>(v + 0.5f);
  };
  return (channel(alpha * 255.0f) << 24) | (channel(r) << 16) |
         (channel(g) << 8) | channel(b);
}

void Circle() {
  if (g_have_circle) return;
  g_have_circle = true;
  for (int i = 0; i <= kSegments; ++i) {
    const float angle = 6.2831853f * static_cast<float>(i) / kSegments;
    g_cos[i] = cosf(angle);
    g_sin[i] = sinf(angle);
  }
}

// THE TILT of the disc on the screen: the direction of its ry axis, a unit
// vector. 0 and 1 is a disc with its axes along the screen. The halo of a
// tall target at the Hand is an ellipse along the axis of the body
// (DrawNearHalo), and each other disc has no tilt.
float g_tilt_x = 0.0f;
float g_tilt_y = 1.0f;

void Put(int* n, const GlowDisc& disc, float size, float share, int i,
         uint32_t color) {
  Vertex& v = g_vertices[(*n)++];
  const float across = size * share * disc.rx * g_cos[i];
  const float along = size * share * disc.ry * g_sin[i];
  v.x = disc.x + across * g_tilt_y + along * g_tilt_x;
  v.y = disc.y - across * g_tilt_x + along * g_tilt_y;
  v.z = disc.z;
  v.rhw = disc.rhw;
  v.diffuse = color;
  v.specular = 0xFF000000u;  // no fog, if the fog were on
  v.u = 0.0f;
  v.v = 0.0f;
}

// size is 1 for the disc and kCore for the core. The disc has the color of
// the look. The core is that color half way to white, and adds its light.
void Build(const GlowDisc& disc, const Style& style, bool core) {
  Circle();
  const float size = core ? kCore : 1.0f;
  const float alpha = core ? style.core : style.alpha;
  float r = style.r, g = style.g, b = style.b;
  if (core) {
    r = 0.5f * (r + 255.0f);
    g = 0.5f * (g + 255.0f);
    b = 0.5f * (b + 255.0f);
  }
  const uint32_t middle = Color(alpha, r, g, b);
  const uint32_t ring = Color(alpha * kRingAlpha, r, g, b);
  const uint32_t edge = Color(0.0f, r, g, b);
  int n = 0;
  for (int i = 0; i < kSegments; ++i) {
    Put(&n, disc, size, 0.0f, i, middle);
    Put(&n, disc, size, kMiddle, i, ring);
    Put(&n, disc, size, kMiddle, i + 1, ring);
    Put(&n, disc, size, kMiddle, i, ring);
    Put(&n, disc, size, 1.0f, i, edge);
    Put(&n, disc, size, 1.0f, i + 1, edge);
    Put(&n, disc, size, kMiddle, i, ring);
    Put(&n, disc, size, 1.0f, i + 1, edge);
    Put(&n, disc, size, kMiddle, i + 1, ring);
  }
}

void SetStates(IDirect3DDevice7* device) {
  int unread = 0;
  for (int i = 0; i < kRenderCount; ++i) {
    g_render_changed[i] = false;
    g_render_read[i] = false;
    DWORD was = 0;
    if (FAILED(device->GetRenderState(kRender[i].state, &was))) {
      ++unread;
      continue;
    }
    g_render_read[i] = true;
    g_render_was[i] = was;
    DWORD want = kRender[i].value;
    if (kRender[i].state == D3DRENDERSTATE_ZENABLE && was != D3DZB_FALSE) {
      want = was;
    }
    if (was == want) continue;
    if (SUCCEEDED(device->SetRenderState(kRender[i].state, want))) {
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
    ModLog("hand glow    : the device did not give %d states, so the glow "
           "leaves them as they are", unread);
  }
}

// One more change of a state that SetStates read. PutStatesBack puts back
// the value that SetStates read. False when that value is not known.
bool SetOne(IDirect3DDevice7* device, D3DRENDERSTATETYPE state,
            DWORD value) {
  const int i = RenderIndex(state);
  if (i < 0 || !g_render_read[i]) return false;
  if (FAILED(device->SetRenderState(kRender[i].state, value))) return false;
  g_render_changed[i] = true;
  return true;
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

// One halo: the disc around an object, in the pass of this eye.
void DrawOneHalo(IDirect3DDevice7* device, const FarDraw& d,
                 const Style& style, int eye, uint64_t frame) {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  const float near_plane =
      *reinterpret_cast<const float*>(bw::tech::kNearPlane);
  float m[12];
  OverlayEyeMatrix(m);
  const float units = d.units_per_meter;
  float radius = kHaloGrow * d.radius + kHaloPlusCm * 0.01f * units;
  if (radius < kHaloMinCm * 0.01f * units) radius = kHaloMinCm * 0.01f * units;
  // At least 1.2 percent of the distance from the Hand, so it shows far away.
  const float away = sqrtf((d.object[0] - d.hand[0]) * (d.object[0] - d.hand[0]) +
                           (d.object[1] - d.hand[1]) * (d.object[1] - d.hand[1]) +
                           (d.object[2] - d.hand[2]) * (d.object[2] - d.hand[2]));
  if (radius < 0.012f * away) radius = 0.012f * away;
  // The center is one radius over the object's base, as the object is round.
  float center[3] = {d.object[0], d.object[1] + d.radius, d.object[2]};
  GlowDisc disc = {};
  if (!GlowProject(m, info->half_res_x, info->half_res_y, near_plane, center,
                   radius, -d.radius, &disc)) {
    return;
  }
  if (disc.rx > info->half_res_x || disc.ry > info->half_res_y) return;
  Build(disc, style, false);
  SetStates(device);
  HRESULT result = device->DrawPrimitive(D3DPT_TRIANGLELIST, kFvf, g_vertices,
                                         kVertices, 0);
  if (SUCCEEDED(result) &&
      SetOne(device, D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE)) {
    Build(disc, style, true);
    result = device->DrawPrimitive(D3DPT_TRIANGLELIST, kFvf, g_vertices,
                                   kVertices, 0);
  }
  PutStatesBack(device);
  if (FAILED(result)) {
    ++g_failed;
    return;
  }
  ++g_halo_drawn;
  static float said_at[3] = {1e30f, 1e30f, 1e30f};
  static int said = 0;
  if (said < 20 && (fabsf(said_at[0] - d.object[0]) > 0.01f ||
                    fabsf(said_at[2] - d.object[2]) > 0.01f)) {
    ++said;
    for (int i = 0; i < 3; ++i) said_at[i] = d.object[i];
    ModLog("hand glow    : a halo of %s is drawn, frame "
           "%llu, eye %d, at %.0f %.0f, radius %.1f units, %.0f px, depth "
           "%.1f units", style.name, frame, eye, disc.x, disc.y, radius,
           disc.rx,
           disc.depth);
  }
}

// The halo of a far target, once in each eye pass of a frame.
//
// A RED TARGET THAT THE CREATURE CAN GO TO. A far target outside the
// influence is red: the Hand cannot take it. With the leash on the Hand, the
// trigger can still send the Creature to the target of the command. Then the
// halo is the green of the command, on the target of the command, because
// the red said that nothing can act. A gold target and a locked one keep
// their halo.
//
// TWO TARGETS. The grip and the trigger can have different targets: a far
// target of the grip in its wide cone, and a target of a command in its tight
// cone. Then each has its halo, gold for the grip and green for the trigger.
// One object that is both has the gold halo.
void DrawHalo() {
  FarDraw d, c, m;
  bool same = false;
  bool far_target = FarGrabDrawState(&d);
  // While the trigger holds a walk of the Creature, the aim is for the land:
  // the far target of the grip has no halo. A target that the grip holds
  // keeps its halo.
  if (far_target && !d.locked && CreatureCommandWalkHeld()) far_target = false;
  const bool command = CreatureCommandHalo(&c, &same);
  // THE TARGET OF A MIRACLE (miracle.cpp). While a miracle is in the Hand,
  // the body that a press casts it on has the halo: gold, or red when the
  // game does not let the miracle act on it.
  const bool miracle = MiracleHalo(&m);
  // THE THING THAT TAKES THE OBJECT IN THE HAND (village.cpp): the store, the
  // pile, the scaffold or the altar that the opening of the grip acts on.
  FarDraw v;
  const bool village = VillageHalo(&v);
  if (!far_target && !command && !miracle && !village) return;
  int eye = -1;
  uint64_t frame = 0;
  if (!HandDirectEyeFrame(&eye, &frame) || eye < 0 || eye > 1) return;
  if (g_halo_frame[eye] == frame) return;
  g_halo_frame[eye] = frame;
  IDirect3DDevice7* device =
      *reinterpret_cast<IDirect3DDevice7* const*>(
          bw::render::kDirect3DDevice7);
  if (device == nullptr) return;
  // The red halo of a far target gives way to the green halo of a command
  // on the same object.
  const bool red_gives_way = far_target && command && same && !d.valid &&
                             !d.locked;
  if (far_target && !red_gives_way) {
    DrawOneHalo(device, d, d.valid ? kHaloValid : kHaloInvalid, eye, frame);
  }
  if (command && (!far_target || !same || red_gives_way)) {
    DrawOneHalo(device, c, kHaloCommand, eye, frame);
  }
  if (miracle) {
    DrawOneHalo(device, m, m.valid ? kHaloMiracle : kHaloInvalid, eye, frame);
  }
  if (village) {
    DrawOneHalo(device, v, v.valid ? kHaloHold : kHaloInvalid, eye, frame);
  }
}

// THE HALO OF THE TARGET AT THE HAND (one target, docs/grab.md). The object
// that a press of the grip acts on has the halo of a far target: gold, or red
// when the influence stops the press. So near and far are one system: the
// object with the halo is the object of the grip. The halo is as large as the
// body of the target (hand_direct.cpp, Measure), and for a tall body it is an
// ellipse along the image of the upright axis. Its depth test uses a point
// one radius behind the middle of the body, so the object hides the middle of
// the disc.
uint64_t g_near_halo_frame[2] = {0, 0};
uint64_t g_near_halo_drawn = 0;

void DrawNearHalo(IDirect3DDevice7* device, int eye, uint64_t frame,
                  const float m[12], float half_x, float half_y,
                  float near_plane, float units) {
  if (eye < 0 || eye > 1 || g_near_halo_frame[eye] == frame) return;
  g_near_halo_frame[eye] = frame;
  float center[3], body = 0.0f, half_height = 0.0f;
  bool acts = false;
  if (!HandDirectNearTarget(center, &body, &half_height, &acts)) return;
  float radius = kHaloGrow * body + kHaloPlusCm * 0.01f * units;
  if (radius < kHaloMinCm * 0.01f * units) radius = kHaloMinCm * 0.01f * units;
  GlowDisc disc = {};
  if (!GlowProject(m, half_x, half_y, near_plane, center, radius, -body,
                   &disc)) {
    return;
  }
  // The image of the top of the body. A top behind the near plane gives a
  // round halo.
  near_math::HaloShape shape = {disc.rx, disc.ry, 0.0f, 1.0f};
  if (half_height > body) {
    const float top[3] = {center[0], center[1] + half_height, center[2]};
    GlowDisc top_disc = {};
    const float cz = top[0] * m[2] + top[1] * m[5] + top[2] * m[8] + m[11];
    if (cz > near_plane) {
      const float cx = top[0] * m[0] + top[1] * m[3] + top[2] * m[6] + m[9];
      const float cy = top[0] * m[1] + top[1] * m[4] + top[2] * m[7] + m[10];
      top_disc.x = (1.0f + cx / cz) * half_x;
      top_disc.y = (1.0f - cy / cz) * half_y;
      shape = near_math::HaloEllipse(top_disc.x - disc.x, top_disc.y - disc.y,
                                     disc.rx, kHaloGrow);
    }
  }
  if (shape.along > half_x || shape.along > half_y) return;
  disc.rx = shape.across;
  disc.ry = shape.along;
  g_tilt_x = shape.tilt_x;
  g_tilt_y = shape.tilt_y;
  const Style& style = acts ? kHaloValid : kHaloInvalid;
  Build(disc, style, false);
  SetStates(device);
  HRESULT result = device->DrawPrimitive(D3DPT_TRIANGLELIST, kFvf, g_vertices,
                                         kVertices, 0);
  if (SUCCEEDED(result) &&
      SetOne(device, D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE)) {
    Build(disc, style, true);
    result = device->DrawPrimitive(D3DPT_TRIANGLELIST, kFvf, g_vertices,
                                   kVertices, 0);
  }
  PutStatesBack(device);
  g_tilt_x = 0.0f;
  g_tilt_y = 1.0f;
  if (FAILED(result)) {
    ++g_failed;
    return;
  }
  ++g_near_halo_drawn;
  static float said_at[3] = {1e30f, 1e30f, 1e30f};
  static int said = 0;
  if (said < 30 && (fabsf(said_at[0] - center[0]) > 0.01f ||
                    fabsf(said_at[1] - center[1]) > 0.01f ||
                    fabsf(said_at[2] - center[2]) > 0.01f)) {
    ++said;
    for (int i = 0; i < 3; ++i) said_at[i] = center[i];
    ModLog("hand glow    : a halo of the target at the Hand is drawn (%s), "
           "frame %llu, eye %d, at %.0f %.0f, %.0f px across and %.0f px "
           "along the body, the axis %.2f %.2f on the screen. The body: "
           "radius %.2f, half height %.2f units, depth %.1f units",
           acts ? "gold" : "red: the influence stops the press", frame, eye,
           disc.x, disc.y, shape.across, shape.along, shape.tilt_x,
           shape.tilt_y, body, half_height, disc.depth);
  }
}

// After both eyes of a frame: each change of the look, and a sample now and
// then, so a test can find the disc in the pictures of both eyes.
void LogFrame(uint64_t frame, int32_t mode, float units_per_meter,
              float radius_cm) {
  const Look look = g_look[1];
  const bool changed = look != g_logged_look;
  const bool sample = frame >= g_next_sample;
  if (!g_shown[1]) {
    // No glow: one line for the change only.
    if (!changed || g_look_logs >= kMaxLookLogs) return;
    ++g_look_logs;
    g_logged_look = look;
    ModLog("hand glow    : no glow now: %s (hand mode 0x%X), frame %llu",
           look == kLookHolding ? "the Hand holds an object"
                                : "a press of the grip has no target at the "
                                  "Hand's point",
           mode, frame);
    return;
  }
  if (!changed && !sample) return;
  if (g_look_logs >= kMaxLookLogs) return;
  ++g_look_logs;
  g_logged_look = look;
  g_next_sample = frame + kSampleFrames;
  const GlowDisc& l = g_disc[0];
  const GlowDisc& r = g_disc[1];
  ModLog("hand glow    : %s (hand mode 0x%X), frame %llu. Left eye at %.1f "
         "%.1f, right eye at %.1f %.1f, %.1f px apart. Radius %.1f cm, "
         "%.1f px. Depth %.2f units, %.1f cm of the room",
         kStyles[look].name, mode, frame, l.x, l.y, r.x, r.y, l.x - r.x,
         radius_cm, l.rx, l.depth,
         units_per_meter > 0.0f ? l.depth / units_per_meter * 100.0f : 0.0f);
}

}  // namespace

void HandGlowSetEnabled(bool on) {
  g_on = on;
  ModLog("hand glow    : %s", on ? "a glow marks the Hand's point while the "
                                     "controller places the Hand"
                                   : "off");
}

// sky_detail.cpp, after the sun glare, once in each FinishFrame. Draws only
// inside an eye pass, and at most once in each eye pass of a frame.
void HandGlowDraw() {
  // ONE TARGET: the choice of this frame, before the halo and the glow of
  // this frame are drawn. The object with the halo is then the object of the
  // next press.
  HandDirectChooseNow();
  DrawHalo();
  float point[3], length = 0.0f;
  int32_t mode = 0;
  bool holding = false;
  int eye = -1;
  uint64_t frame = 0;
  if (!HandDirectGlowFacts(point, &length, &mode, &holding, &eye, &frame)) {
    return;
  }
  if (eye < 0 || eye > 1 || g_asked_frame[eye] == frame) return;
  g_asked_frame[eye] = frame;

  IDirect3DDevice7* device =
      *reinterpret_cast<IDirect3DDevice7* const*>(
          bw::render::kDirect3DDevice7);
  if (device == nullptr) return;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  const float near_plane =
      *reinterpret_cast<const float*>(bw::tech::kNearPlane);

  float m[12];
  OverlayEyeMatrix(m);
  for (int i = 0; i < 12; ++i) g_eye_matrix[eye][i] = m[i];
  g_eye_matrix_frame[eye] = frame;
  g_eye_half[0] = info->half_res_x;
  g_eye_half[1] = info->half_res_y;
  const float units_per_meter = UnitsPerMeter();
  {
    GlowDisc at = {};
    EyePoint& p = g_eye_point[eye];
    p.valid = GlowProject(m, info->half_res_x, info->half_res_y, near_plane,
                          point, 0.0f, 0.0f, &at) &&
              units_per_meter > 0.0f;
    p.frame = frame;
    p.x = at.x;
    p.y = at.y;
    p.meters = p.valid ? at.depth / units_per_meter : 0.0f;
    // ONE WALK (one_walk.cpp). The right eye has no pass of its own. Its
    // point and its camera are those of the left eye, moved by the disparity
    // of the depth (one_walk_math.h): x moves by per_rhw * near / depth, and
    // m[9] by per_rhw * near / half_res_x.
    if (eye == 0 && OneWalkActive()) {
      const float per_rhw = OneWalkShiftPerRhw();
      EyePoint& right = g_eye_point[1];
      right = p;
      if (right.valid && at.depth > 0.0f) {
        right.x += per_rhw * near_plane / at.depth;
      }
      for (int i = 0; i < 12; ++i) g_eye_matrix[1][i] = m[i];
      if (info->half_res_x > 0.0f) {
        g_eye_matrix[1][9] += per_rhw * near_plane / info->half_res_x;
      }
      g_eye_matrix_frame[1] = frame;
    }
  }
  DrawNearHalo(device, eye, frame, m, info->half_res_x, info->half_res_y,
               near_plane, units_per_meter);
  if (!g_on) return;
  const Look look = LookFor(HandDirectPressKind(), holding);
  // NO GLOW WITH NO TARGET (hand_glow_idle=0). The glow shows only while a
  // press of the grip at this place acts on something. With the value 1, the pale blue glow
  // is there on each frame, so it says nothing.
  if ((look == kLookNothing || look == kLookHolding) &&
      !HandDirectGlowIdle()) {
    ++g_hidden[look];
    g_look[eye] = look;
    g_shown[eye] = false;
    if (eye == 1) LogFrame(frame, mode, units_per_meter, 0.0f);
    return;
  }
  const Style& style = kStyles[look];
  const float radius = style.radius_cm * 0.01f * units_per_meter;
  GlowDisc disc = {};
  if (!GlowProject(m, info->half_res_x, info->half_res_y, near_plane, point,
                   radius, 0.5f * length, &disc)) {
    ++g_outside;
    return;
  }
  // A hand at the face: the disc would fill the view.
  if (disc.rx > info->half_res_x || disc.ry > info->half_res_y) {
    ++g_too_near;
    return;
  }
  Build(disc, style, false);
  SetStates(device);
  HRESULT result = device->DrawPrimitive(D3DPT_TRIANGLELIST, kFvf, g_vertices,
                                         kVertices, 0);
  // The core adds its light: source alpha and one.
  if (SUCCEEDED(result) && SetOne(device, D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE)) {
    Build(disc, style, true);
    result = device->DrawPrimitive(D3DPT_TRIANGLELIST, kFvf, g_vertices,
                                   kVertices, 0);
  }
  PutStatesBack(device);
  if (FAILED(result)) {
    ++g_failed;
    if (!g_said_failed) {
      g_said_failed = true;
      ModLog("hand glow    : the draw failed (0x%08X)",
             static_cast<unsigned>(result));
    }
    return;
  }
  ++g_drawn[look];
  g_drawn_frame[eye] = frame;
  g_disc[eye] = disc;
  g_look[eye] = look;
  g_shown[eye] = true;
  if (!g_said_first) {
    g_said_first = true;
    ModLog("hand glow    : the first glow is drawn, frame %llu, eye %d",
           frame, eye);
  }
  if (eye == 1 && g_drawn_frame[0] == frame) {
    LogFrame(frame, mode, units_per_meter, style.radius_cm);
  }
}

// For draw_hooks.cpp: the Hand's point in this eye, from this frame or the
// one before. The interface of an eye pass can draw before the end of the
// world, where the point is set. False while the controller does not place
// the Hand.
bool HandGlowEyePoint(int eye, float* x, float* y, float* meters) {
  if (eye < 0 || eye > 1) return false;
  float point[3], length = 0.0f;
  int32_t mode = 0;
  bool holding = false;
  int now_eye = -1;
  uint64_t frame = 0;
  if (!HandDirectGlowFacts(point, &length, &mode, &holding, &now_eye,
                           &frame)) {
    return false;
  }
  const EyePoint& p = g_eye_point[eye];
  if (!p.valid || p.frame + 1 < frame || p.frame > frame) return false;
  *x = p.x;
  *y = p.y;
  *meters = p.meters;
  return true;
}

// For the desktop test word "picture" (hand_direct.cpp, seek_plan): the place
// in the world of a point that shows at lx ly in the picture of the left eye
// and at rx ry in the picture of the right eye, in pixels of the render. It
// uses the cameras of the last frame that drew both eyes, and nothing that the
// mod knows about an object. False with no such frame, or when the two pixels
// give no place.
bool HandGlowPicturePoint(float lx, float ly, float rx, float ry,
                          float out[3], float* half_x, float* half_y) {
  if (g_eye_matrix_frame[0] == 0 ||
      g_eye_matrix_frame[0] != g_eye_matrix_frame[1]) {
    return false;
  }
  *half_x = g_eye_half[0];
  *half_y = g_eye_half[1];
  return near_math::PicturePoint(g_eye_matrix[0], g_eye_matrix[1],
                                 g_eye_half[0], g_eye_half[1], lx, ly, rx, ry,
                                 out);
}

// Once a report window.
void HandGlowReport() {
  uint64_t drawn = 0;
  for (int i = 0; i < kLooks; ++i) drawn += g_drawn[i];
  if (g_hidden[kLookNothing] != 0 || g_hidden[kLookHolding] != 0 ||
      g_near_halo_drawn != 0) {
    ModLog("hand glow    : no glow in %llu eye passes with no target and in "
           "%llu while holding (hand_glow_idle=0). The halo of the target at "
           "the Hand was drawn in %llu eye passes",
           g_hidden[kLookNothing], g_hidden[kLookHolding], g_near_halo_drawn);
    g_hidden[kLookNothing] = g_hidden[kLookHolding] = 0;
    g_near_halo_drawn = 0;
  }
  if (drawn == 0 && g_outside == 0 && g_too_near == 0 && g_failed == 0) {
    return;
  }
  ModLog("hand glow    : drawn in %llu eye passes: %llu over nothing, %llu "
         "over something to take, %llu over something to activate, %llu while "
         "holding. Not drawn: %llu outside the view, %llu too near the eye, "
         "%llu failed", drawn, g_drawn[kLookNothing], g_drawn[kLookTake],
         g_drawn[kLookTouch], g_drawn[kLookHolding], g_outside, g_too_near,
         g_failed);
  for (int i = 0; i < kLooks; ++i) g_drawn[i] = 0;
  g_outside = g_too_near = g_failed = 0;
}
