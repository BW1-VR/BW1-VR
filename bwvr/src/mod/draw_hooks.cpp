// The three engine draw entry points, in one place.
//
// This does two jobs that both need the same hooks.
//
// 1. IT NAMES WHAT DREW A BATCH.
//
// Every triangle in the frame reaches Direct3D through exactly four call
// sites, and the draw probe measured which:
//
//     0x0082F919  LH3DRender::DrawTriangle   19951 batches, world geometry
//     0x0082A81B  DrawAndClip2D               3900 batches, flat interface
//     0x0082A5A9  DrawAndClip                 3000 batches, flat interface
//     0x0082F617  LH3DRender::FinishFrame      149 batches, the sky
//
// Those four names are not enough on their own, because everything in the
// world arrives through DrawTriangle. So each hook here records the address it
// was called from, which is one level further up, inside the subsystem that
// actually built the geometry. That address resolves against the symbol file:
//
//     python bwvr/tools/disasm.py <address>
//
// 2. IT PUTS THE FLAT INTERFACE IN FRONT OF THE WORLD.
//
// The interface is drawn in screen space, so both eyes get identical pixels.
// Identical pixels mean no disparity, and no disparity puts the interface
// exactly on the virtual screen at 2 metres. The mountains behind it are drawn
// with real disparity and can sit nearer than that. So the interface covers
// them while stereo says it is further away, the two cues disagree, and the
// result is unpleasant to look at.
//
// Shifting the interface sideways, in opposite directions for the two eyes,
// moves it off the screen plane. Left eye right and right eye left is crossed
// disparity, which reads as nearer.
//
// AN EARLIER ATTEMPT AT THIS SPLIT A CHECKBOX IN HALF, and the measurement
// above says why. It hooked only DrawAndClip2D. DrawAndClip draws flat
// interface as well, 3000 batches in the same 150 frames, and none of it
// moved. A checkbox ended up with its border at one depth and its fill at
// another. Both paths are hooked here.
//
// WHAT COUNTS AS INTERFACE, and a mistake worth recording.
//
// The first version tested the depth value. The probe had measured world
// geometry at z 0.975 to 0.9998 and the interface at z 0.0 to 0.333, which
// look like two different worlds, so "z below 0.9 is screen space" seemed
// safe.
//
// It is not a depth test at all. Every path writes the same thing, and
// LH3DTech::Draw3DScreenTriangle at 0x0081C5DD shows it plainly:
//
//     rhw = near / camera_z        near at 0x00E839E0
//     z   = 1.0 - rhw
//
// so z below 0.9 means rhw above 0.1, which means camera_z below 35 world
// units at the game's near plane of 3.5. (near_plane.cpp lowers the near
// plane in play modes 0, 3 and 4. Then the same z is nearer still.) It does
// not say "interface". It says "near the camera". The measured
// 0.975 for the world was a zoomed out camera and nothing more. Zoom in and
// the water and the terrain cross that line and get dragged forward with the
// menu, which is exactly what happened.
//
// So the test is the CALLER. A function that draws the menu draws only the
// menu, whatever the camera is doing. These ranges come from the symbol file
// and each is one contiguous block.
//
// HOW MUCH SHIFT
//
// For eyes e apart, a screen plane at distance S and a wanted distance D, the
// two images must differ on the screen by
//
//     e * (S/D - 1)
//
// At 1280 pixels across 2.60 m the screen is 2.03 mm per pixel, so with
// e = 64 mm and S = 2.0 m:
//
//     ui_shift_px = 4    the interface sits about 1.6 m away
//     ui_shift_px = 8    about 1.3 m
//     ui_shift_px = 12   about 1.15 m
//
// ui_shift_px is half the total, because each eye moves by that much in
// opposite directions.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d.h>
#include <stdint.h>
#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "one_walk.h"
#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "bubble_card.h"
#include "controller_hints.h"
#include "controller_math.h"
#include "cutscene_window_math.h"
#include "hint_card.h"
#include "subtitle_card_math.h"
#include "ui_panel.h"

extern void ModLog(const char* format, ...);
// cutscene_window.cpp: this frame draws on the screen of the cut scene
// window. There the help layer, the subtitles and the cinema bars draw as the
// game made them for a flat screen.
bool CutsceneWindowFlat();
// A batch of the video player of the game. True when it goes on the screen
// plane of the window, and then `move` is its move for that eye.
bool CutsceneWindowVideoBatch(int eye, float* move);
// The measure of the nearest thing in view, for the depth limit of the cut
// scene window: each eye pass of a flat frame gives its world batches.
bool CutsceneWindowProbeStart(int eye);
void CutsceneWindowProbeBatch(const void* verts, uint32_t count,
                              const uint16_t* idx, uint32_t n,
                              uint32_t caller);
// The advisors in the window (cutscene_advisors). True for a batch of an
// advisor that gets its place in front of the screen. Then the batch is not
// in the measure, and `shift` gives the move of each vertex.
bool CutsceneWindowAdvisorBatch(int eye, const void* verts, uint32_t count,
                                const uint16_t* idx, uint32_t n,
                                window_math::AdvisorShift* shift);

// hand_glow.cpp: the Hand's point in this eye pass, in pixels of the render,
// and its depth in meters of the room. False while the controller does not
// place the Hand.
bool HandGlowEyePoint(int eye, float* x, float* y, float* meters);

// target_glow.cpp: a batch of the world with the vertex format 0x1C4. The
// vertices of a target of the Hand get their glow. caller is for the log.
void TargetGlowBatch(void* verts, uint32_t count, const uint16_t* idx,
                     uint32_t indices, bool clipped, uint32_t caller);

// intro_vr.cpp: the flat text and the boxes of the Lionhead logo in stereo.
// IntroFlatOn is true when a flat batch from `caller` comes in an eye pass
// of the intro and must get a place in the scene. IntroFlatVertex gives one
// corner its pixel, and it is false when the place is not in front of the
// camera. IntroFlatDone counts the batch.
bool IntroFlatOn(uint32_t caller);
bool IntroFlatVertex(float* x, float* y);
void IntroFlatDone(bool drawn);

namespace {

// The log of where the help lands (below, after the tooltips).
void HintLog(const char* what, const wchar_t* text, float x0, float x1,
             float y0, float y1);
// Makes the next frame a sample frame of that log.
void HintSampleSoon();

// True while the interface panel target is bound on the device. The routing
// is further down, above DrawInterface. The flag is here, because the scope
// hooks and DrawTriangle put the game's target back with PanelRelease.
bool g_panel_bound = false;
uint64_t g_panel_left_bound = 0;  // still bound when FinishFrame returned

// THE SUBTITLE CARD (docs/ui-layer.md, "The subtitle card"). True while the
// help text of this frame goes to the panel target and not to the eye
// pictures. UiPanelRouteFrame latches it before the eye passes.
bool g_card_takes_help = false;
// The bounds of what the card took in this frame, in pixels of the render:
// x0, y0, x1, y1.
float g_card_bounds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
bool g_have_card_bounds = false;
// THE FALLBACK. When the card is wanted and cannot go out, the help text
// draws in the eye pictures at this fixed depth in meters, and not at the
// interface depth, which changes with the world. 0 is the interface depth.
float g_help_depth_m = 0.0f;
uint64_t g_help_fixed = 0;  // batches drawn at the fixed depth
// The two help scopes, defined with the interface size further down.
extern int g_helptext_depth;
extern int g_draw3d_depth;

void PanelRelease() {
  if (!g_panel_bound) return;
  GpuBridgePanelUnbind();
  g_panel_bound = false;
}

// Both clip functions take every argument on the stack and end in a plain ret,
// so they really are __cdecl. DrawTriangle ends in ret 8, so it is __fastcall
// with the first two arguments in ecx and edx. The mangled names claim __cdecl
// for all three and are not to be trusted. The epilogue is.
typedef void(__cdecl* DrawAndClipFn)(D3DPRIMITIVETYPE type, uint32_t fvf,
                                     void* verts, uint32_t count,
                                     uint16_t* idx, uint32_t n);
typedef void(__fastcall* DrawTriangleFn)(void* verts, uint32_t count,
                                         uint16_t* idx, uint32_t n);

DrawAndClipFn o_DrawAndClip = nullptr;
DrawAndClipFn o_DrawAndClip2D = nullptr;
DrawTriangleFn o_DrawTriangle = nullptr;

// FVF 0x1C4 is XYZRHW plus diffuse, specular and one texture coordinate set:
// 32 bytes, with x first.
const uint32_t kExpectedFvf = 0x1C4;
const size_t kVertexStride = 32;

// The functions that draw the flat interface, by address range.
struct UiCaller {
  uintptr_t begin;
  uintptr_t end;
  const char* name;
};

const UiCaller kUiCallers[] = {
    // The whole SetupThing widget toolkit: GetTextHeight at 0x00411690
    // through DrawBevBox, which ends at 0x00413FF0. Menus, boxes, tabs,
    // buttons, text and the lines between them.
    {0x00411690, 0x00413FF0, "SetupThing widgets"},
    // LH3DTech::Draw3DScreenTriangle, 0x0081C5C0 size 0x1C0. The generic
    // screen space triangle submitter, which is what draws text.
    {0x0081C5C0, 0x0081C780, "Draw3DScreenTriangle"},
    // fn_00447450, size 0x3C5. Called by CameraHelp::DrawKeyOrMouse, by
    // fn_00447850 and from the key binding dialog, so it only ever draws
    // interface. These are the hints that appear at the screen edges.
    {0x00447450, 0x00447815, "CameraHelp quads"},
    // The unnamed function from 0x0081E3C0 to its ret at 0x0081E58B. It draws
    // the rect queue, and its only caller is the FinishFrame callback
    // 0x0081E7D0. HelpText fn_005CCE60 is the only code that fills the queue
    // (through fn_0081E360). It only queues the subtitle bar and does not draw
    // it. So the bar is drawn here, after the help scopes have closed.
    {0x0081E3C0, 0x0081E58C, "HelpSystem quads"},
};

// THE VIDEO PLAYER OF THE GAME. LHVideoPlayer::thedraw 0x00844E30, a late
// FinishFrame callback, calls fn_00845740 (0x3F0 bytes). It draws each frame
// of a Bink video as tiles of 4 vertices, each through DrawAndClip2D at
// 0x00845A7D, from the vertex array at 0x00C391B0. It is not interface: the
// chain of the interface does not move it.
const uintptr_t kVideoBegin = 0x00845740;
const uintptr_t kVideoEnd = 0x00845B30;

bool IsVideoCaller(uint32_t address) {
  return address >= kVideoBegin && address < kVideoEnd;
}

// The subtitle bar, which defines the band its letters are found in.
// DrawAndClip2D returns to 0x0081E516 when it draws the bar.
const uintptr_t kSubtitleBegin = 0x0081E3C0;
const uintptr_t kSubtitleEnd = 0x0081E58C;

// NOT in the list, and deliberately so: fn_00879930, reached from
// GLandscape::Draw. That is the water surface, and it is drawn in screen space
// at a constant depth of exactly the near plane, which is why it looked like a
// screen space effect, and why the interface shift must not move it.

bool IsUiCaller(uint32_t address) {
  for (int i = 0; i < sizeof(kUiCallers) / sizeof(kUiCallers[0]); ++i) {
    if (address >= kUiCallers[i].begin && address < kUiCallers[i].end) {
      return true;
    }
  }
  return false;
}

// Anything nearer than this is worth reporting when it is NOT an interface
// caller, because it may be an interface path nobody has found yet. It is a
// reporting threshold and nothing depends on it. It is camera_z below ten
// times the near plane, so a lower near plane reports less of the world.
const float kNearDepth = 0.9f;

// Per caller accounting, so "what am I moving" stops being a guess. The log
// names every function that submitted a screen space looking batch, whether
// or not it was shifted.
struct CallerStat {
  uint32_t address;
  uint64_t batches;
  uint64_t shifted;
  float z_min;
  float z_max;
  float spread_max;  // the largest z range inside one batch
  // Batches submitted during each eye pass. The two passes draw the same
  // scene from two places, so for almost every caller these must match. A
  // caller that draws more in the second pass than the first is adding to
  // something instead of rebuilding it, and our second call to
  // Process3dEngine makes it add twice. That is the shape of the doubled
  // hand tooltip, so this is how to find which caller does it.
  uint64_t batches_in_pass[2];
};

// Which eye pass is running: 0, 1, or -1 outside the passes.
int g_eye = -1;
// True in an eye pass of a frame that measures the nearest thing in view.
bool g_window_probe = false;

// DUMPING ONE FRAME OF INTERFACE GEOMETRY.
//
// The hand widget appears twice, and the gap between the copies grows with
// both the eye separation and the interface shift. That means one copy
// carries those transforms and the other does not. Reading it off pictures
// has gone as far as it can: what is needed is where each batch actually
// landed, per pass.
//
// So one frame writes out every interface batch with its horizontal extent.
// Two clusters at different x, in the same pass, is the widget drawn twice.
// A cluster whose x is the same in both passes is the copy that is missing
// the eye transform, and that is the one to fix.
bool g_dump = false;
int g_dumped = 0;
const int kMaxDump = 200;

const int kMaxStats = 64;
CallerStat g_stats[kMaxStats];
int g_stat_count = 0;

CallerStat* Stat(uint32_t address) {
  for (int i = 0; i < g_stat_count; ++i) {
    if (g_stats[i].address == address) return &g_stats[i];
  }
  if (g_stat_count >= kMaxStats) return nullptr;
  CallerStat* fresh = &g_stats[g_stat_count++];
  fresh->address = address;
  fresh->batches = 0;
  fresh->shifted = 0;
  fresh->z_min = 1e30f;
  fresh->z_max = -1e30f;
  fresh->spread_max = 0.0f;
  fresh->batches_in_pass[0] = 0;
  fresh->batches_in_pass[1] = 0;
  return fresh;
}

// Shifting in place and adding the offset back afterwards would not restore
// the value exactly, because float addition is not reversible. Saving the old
// x values and writing them back is exact, and it costs one small array.
const uint32_t kMaxShiftVertices = 16384;
float g_saved_x[kMaxShiftVertices];

float g_shift = 0.0f;  // pixels, at the current eye
bool g_accounting = false;

// How near the world gets in a frame, as an rhw. rhw is near/camera_z, so a
// LARGER rhw is nearer. The interface has to be put in front of this, and how
// much disparity that needs changes as the camera zooms.
//
// The MAXIMUM is the wrong number to use, and measuring said so plainly: it
// read 6077, which is a vertex three thousandths of a unit from the camera.
// Every frame has a few of those, from the Hand and from geometry the clipper
// has cut, and one of them would pin the interface at its nearest limit for
// the whole session.
//
// So this keeps a histogram and takes a high percentile instead.
//
// The histogram does not count the rhw of the frame. It counts the rhw that
// the vertex would have at the game's own near plane over high land, 3.5
// units: kHistogramNear / camera_z. near_plane.cpp can make the near plane
// ten times smaller, and then a raw rhw would put the whole world in the
// lowest bucket. A value above 1.0 is nearer than 3.5 units, so it is left
// out, as it was before the near plane moved: the real hand and the Hand are
// there, and they must not pull the interface to the eyes.
const int kRhwBuckets = 64;
const float kHistogramNear = 3.5f;
uint32_t g_rhw_histogram[kRhwBuckets];
uint32_t g_rhw_samples = 0;
float g_world_inverse_z = 0.0f;  // 1 / camera_z of the nearest world

// Time spent inside DrawTriangle, which is where the engine clips and submits
// already transformed vertices. Subtracting this from the whole scene walk
// leaves traversal, culling, the software transform itself and whatever
// simulation Process3dEngine mixes in.
bool g_time_submit = false;
LARGE_INTEGER g_submit_frequency = {0};
uint64_t g_submit_ticks = 0;
uint64_t g_submit_calls = 0;
uint64_t g_shifted = 0;
uint64_t g_refused = 0;  // asked to shift, but the batch was not flat
uint64_t g_too_big = 0;

// The address inside the subsystem that asked for this draw.
uint32_t g_caller = 0;

// Record the depth range of a batch, for the report. This is diagnosis only.
void Note(uint32_t caller, const void* verts, uint32_t count, bool shifted) {
  CallerStat* stat = Stat(caller);
  if (stat == nullptr) return;
  ++stat->batches;
  if (shifted) ++stat->shifted;
  if (g_eye == 0 || g_eye == 1) ++stat->batches_in_pass[g_eye];

  float low = 1e30f;
  float high = -1e30f;
  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    const float z = reinterpret_cast<const float*>(vertex)[2];
    if (z < low) low = z;
    if (z > high) high = z;
    vertex += kVertexStride;
  }
  if (low < stat->z_min) stat->z_min = low;
  if (high > stat->z_max) stat->z_max = high;
  const float spread = high - low;
  if (spread > stat->spread_max) stat->spread_max = spread;
}

// THE DOUBLED HAND WIDGET.
//
// The camera help code draws its hint TWICE in every pass. Measured, with the
// positions taken before any shift of ours touches them:
//
//     pass 0   x 1429     pass 0   x 1340      <- 1340 in both passes
//     pass 1   x 1251     pass 1   x 1340
//
// The first draw is projected through the camera, so it moves with the eye.
// The second is pinned: the same x in both passes, and exactly the midpoint of
// the two moving ones. The likely reason is that the game puts the hint both
// at the Hand and at the mouse pointer, which are the same point on a flat
// screen. In mono the two land on top of each other and look like one hint.
// The moment the camera moves per eye they separate, by between 2 and 89 px,
// and the gap grows as the Hand comes nearer.
//
// THE COPY TO KEEP IS THE PINNED ONE. It sits in the same place in both eyes,
// so the hint reads flat, like a label on the glass, which is what was asked
// for. Keeping the projected copy instead gives the hint the Hand's own depth.
// At 89 px of separation that is a hand's breadth from the face: single, and
// painful to read.
//
// Keeping the pinned copy costs one thing. The pinned copy is drawn SECOND, so
// by the time it arrives the projected one is already on the screen. So the
// pair is learned instead of seen. A widget image that arrived twice in the
// last pass gets its first copy dropped in this one. The image is recognised
// by its size and by the part of the texture it reads, never by where it sits,
// so the rule does not care how fast the hint moves.
//
// If the partner does not arrive, the image is forgotten and the next pass
// draws it normally. The price of a wrong guess is one pass without that hint,
// and the rule repairs itself.
const uintptr_t kCameraHelpBegin = 0x00447450;
const uintptr_t kCameraHelpEnd = 0x00447815;

// WHAT TO DO WITH THE PAIR.
//
//   kHintLeaveAlone   draw both copies, as the game does. Doubled in stereo.
//   kHintKeepProjected  keep the copy that moves with the eye, so the hint
//                     carries the Hand's depth, and shift it with the rest of
//                     the interface.
//   kHintKeepPinned   keep the copy that does not move, so both eyes put the
//                     hint in the same place, and leave it out of the shift.
//
// All three are reachable from one key, because the difference between them is
// a thing to feel rather than a thing to argue about.
const int kHintLeaveAlone = 0;
const int kHintKeepProjected = 1;
const int kHintKeepPinned = 2;
int g_hint_mode = kHintKeepPinned;
bool g_in_widget_band = false;

// Everything measured in pixels has to scale with the render, because a
// widget and the gap between its two copies both shrink when the render does.
// These numbers were tuned at 2560 wide, and Phase 3 renders square and
// smaller, which made the rule miss its own duplicates.
float g_pixel_scale = 1.0f;
uint64_t g_widgets_dropped = 0;

// Which picture a batch draws, with no reference to where it draws it.
//
// The part of the texture is what tells a duplicate from a neighbour. Two
// copies of one hint read the IDENTICAL part. Tiles that make up one image
// read DIFFERENT parts: the left half reads u 0 to 0.5 and the right half
// reads 0.5 to 1. Without this the glow behind the icon lost its right hand
// side, because its tiles are the same size and sit on the same row.
struct WidgetImage {
  float width;
  float height;
  float u_min;
  float u_max;
  float v_min;
  float v_max;
};

struct DrawnQuad {
  WidgetImage image;
  float y_min;
  float x_min;
  bool dropped;   // suppressed as the projected copy, waiting for its partner
  bool paired;    // the partner arrived, so this really is a pair
};

const int kMaxHelpQuads = 64;
DrawnQuad g_help_quads[kMaxHelpQuads];
int g_help_quad_count = 0;

// The images that came in pairs last pass. This is all that carries over.
WidgetImage g_paired[kMaxHelpQuads];
int g_paired_count = 0;

// WHAT THIS BATCH IS, decided once by IsDuplicateWidget and read by everything
// after it in the chain.
//
// One flag for each job: the hint size, the place without the interface
// depth, and the copy that is kept.
bool g_widget_scale = false;  // a hint, so the hint size applies
bool g_widget_flat = false;   // a hint, so the interface depth does not
bool g_widget_hint = false;   // a hint copy that is being kept
WidgetImage g_widget_image;
float g_widget_x = 0.0f;

// THE HINT SITS AT THE HAND, AND IT SITS FLAT.
//
// The copy kept in kHintKeepProjected carries the Hand's own disparity, which
// puts a page of text a hand's breadth from the player's face. Reading it
// hurts. The copy pinned to the cursor is flat and comfortable, but with head
// tracking it rides the look direction instead of staying on the Hand.
//
// Neither copy is what is wanted, so make one. Keep the projected copy, so the
// hint stays on the Hand, and record where it lands in the first pass. In the
// second pass move it to the same place. Both eyes then draw it at one screen
// position, which is what "flat" means, and it is still anchored to the Hand.
struct FlatAnchor {
  WidgetImage image;
  float x;
};
FlatAnchor g_flat[kMaxHelpQuads];
int g_flat_count = 0;
bool g_hint_flat = true;

// SUBTITLES YOU CAN ACTUALLY READ.
//
// The subtitle bar sits at the bottom of the render. On a monitor that is a
// few degrees below the middle of a 60 degree view. In a headset it is about
// 55 degrees below the middle of a 110 degree one, which is past the bottom of
// comfortable vision: the player has to look down at their own chin to read
// it, and by then it has scrolled away.
//
// The bar is drawn by one caller and is easy to find. The LETTERS on it are
// drawn by LH3DTech::Draw3DScreenTriangle, the generic text submitter that
// menus and buttons also use, so the caller cannot tell them apart. The band
// can. The letters sit in the same rows of screen as the bar they are written
// on, so remembering where the bar landed identifies its text as well, which
// is the same trick the hand hint already uses.
//
// The band is kept from the previous frame too, because within one pass the
// text may be drawn before the bar that defines the band.
float g_sub_top = 0.0f;
float g_sub_bottom = 0.0f;
bool g_have_sub = false;
float g_prev_sub_top = 0.0f;
float g_prev_sub_bottom = 0.0f;
bool g_have_prev_sub = false;
// How far toward the middle of the view the subtitles are moved, as a
// percentage of the way there. 0 leaves them where the game put them.
float g_subtitle_lift = 0.0f;
float g_saved_sub_y[kMaxShiftVertices];

// SUBTITLES AT A SET ANGLE BELOW THE MIDDLE OF THE VIEW.
//
// The interface size scales the subtitles toward the middle of the view. At
// 25 percent that put them about 16 degrees below the middle, which read as
// too high, in the middle of the work area. So after the size, the box and
// its letters move down together, until the middle of the band is this many
// degrees below the middle of the view. 0 leaves them where the size puts
// them. The angle holds at any render size and field of view.
float g_subtitle_degrees = 35.0f;
// The batch in the draw now is part of the subtitles, and the middle of their
// band, in pixels, after any lift. Set by SubtitleLiftIn for each batch.
bool g_sub_member = false;
float g_sub_center = 0.0f;
float g_saved_place_y[kMaxShiftVertices];
uint64_t g_sub_placed = 0;
// Subtitle batches that sat in the band of the Hand's hint, and were kept
// out of the hint rule. See IsDuplicateWidget.
uint64_t g_sub_kept_from_hint = 0;

bool InSubtitleBand(float y_low, float y_high) {
  const float margin = 24.0f * g_pixel_scale;
  if (g_have_sub && y_low <= g_sub_bottom + margin &&
      y_high >= g_sub_top - margin) {
    return true;
  }
  return g_have_prev_sub && y_low <= g_prev_sub_bottom + margin &&
         y_high >= g_prev_sub_top - margin;
}

// The rows of screen the widget occupies in this pass.
//
// The text and the glow behind the icon are drawn by different code from the
// icon itself, and they are doubled the same way, so they need the same
// treatment. A rule that applied everywhere would be dangerous: a dialog box
// has four identical corner quads on the same row, and a row of tabs is
// identical quads 156 px apart, and both would be eaten.
//
// So the rule only applies near a widget the camera help code actually drew.
// Outside that band the interface is left completely alone.
// The bands are carried over as well as collected. Within a pass the text may
// be drawn BEFORE the icon that defines the band, and then the band would not
// exist yet. The widget barely moves between one pass and the next, so last
// frame's band is a good enough guide for this one.
const int kMaxBands = 8;
float g_band_top[kMaxBands];
float g_band_bottom[kMaxBands];
// Where the hint sits, so the whole thing can be scaled about itself rather
// than about the middle of the screen, which would slide it sideways.
float g_band_x[kMaxBands];
float g_band_y[kMaxBands];
int g_band_count = 0;
float g_prev_band_top[kMaxBands];
float g_prev_band_bottom[kMaxBands];
float g_prev_band_x[kMaxBands];
float g_prev_band_y[kMaxBands];
int g_prev_band_count = 0;

// How big the hint is drawn, against what the game asks for. The game sizes
// its interface in pixels, which on a monitor is a small part of a 60 degree
// view and in a headset is a large part of a 110 degree one. So a hint keeps
// its size on screen while the Hand it belongs to shrinks into the distance.
float g_hint_scale = 1.0f;
const float kBandMargin = 48.0f;

void RememberBand(float y_low, float y_high, float x_mid, float y_mid) {
  for (int i = 0; i < g_band_count; ++i) {
    if (y_low <= g_band_bottom[i] && y_high >= g_band_top[i]) {
      if (y_low < g_band_top[i]) g_band_top[i] = y_low;
      if (y_high > g_band_bottom[i]) g_band_bottom[i] = y_high;
      return;
    }
  }
  if (g_band_count >= kMaxBands) return;
  g_band_top[g_band_count] = y_low;
  g_band_bottom[g_band_count] = y_high;
  g_band_x[g_band_count] = x_mid;
  g_band_y[g_band_count] = y_mid;
  ++g_band_count;
}

// The anchor of whichever band this batch belongs to. False when it belongs to
// none, and then it is left alone.
bool WidgetAnchor(float y_low, float y_high, float* ax, float* ay) {
  const float margin = kBandMargin * g_pixel_scale;
  for (int i = 0; i < g_band_count; ++i) {
    if (y_low <= g_band_bottom[i] + margin && y_high >= g_band_top[i] - margin) {
      *ax = g_band_x[i];
      *ay = g_band_y[i];
      return true;
    }
  }
  for (int i = 0; i < g_prev_band_count; ++i) {
    if (y_low <= g_prev_band_bottom[i] + margin &&
        y_high >= g_prev_band_top[i] - margin) {
      *ax = g_prev_band_x[i];
      *ay = g_prev_band_y[i];
      return true;
    }
  }
  return false;
}

bool InWidgetBand(float y_low, float y_high) {
  for (int i = 0; i < g_band_count; ++i) {
    if (y_low <= g_band_bottom[i] + kBandMargin * g_pixel_scale &&
        y_high >= g_band_top[i] - kBandMargin * g_pixel_scale) {
      return true;
    }
  }
  for (int i = 0; i < g_prev_band_count; ++i) {
    if (y_low <= g_prev_band_bottom[i] + kBandMargin * g_pixel_scale &&
        y_high >= g_prev_band_top[i] - kBandMargin * g_pixel_scale) {
      return true;
    }
  }
  return false;
}

// At the end of a frame the bands found become the guide for the next one.
void RollBands() {
  for (int i = 0; i < g_band_count; ++i) {
    g_prev_band_top[i] = g_band_top[i];
    g_prev_band_bottom[i] = g_band_bottom[i];
    g_prev_band_x[i] = g_band_x[i];
    g_prev_band_y[i] = g_band_y[i];
  }
  g_prev_band_count = g_band_count;
  g_band_count = 0;
}

// Texture coordinates run 0 to 1, so they need a much finer test than pixels.
bool SameUv(float a, float b) {
  const float d = a - b;
  return d < 0.0005f && d > -0.0005f;
}

void UpdatePixelScale() {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return;
  if (info->res_x <= 0) return;
  g_pixel_scale = static_cast<float>(info->res_x) / 2560.0f;
  if (g_pixel_scale < 0.25f) g_pixel_scale = 0.25f;
  if (g_pixel_scale > 2.0f) g_pixel_scale = 2.0f;
}

bool Near(float a, float b) {
  const float d = a - b;
  const float tolerance = 0.75f * g_pixel_scale;
  return d < tolerance && d > -tolerance;
}

bool SameImage(const WidgetImage& a, const WidgetImage& b) {
  return Near(a.width, b.width) && Near(a.height, b.height) &&
         SameUv(a.u_min, b.u_min) && SameUv(a.u_max, b.u_max) &&
         SameUv(a.v_min, b.v_min) && SameUv(a.v_max, b.v_max);
}

bool KnownPaired(const WidgetImage& image) {
  for (int i = 0; i < g_paired_count; ++i) {
    if (SameImage(g_paired[i], image)) return true;
  }
  return false;
}

void RememberPaired(const WidgetImage& image) {
  if (KnownPaired(image)) return;
  if (g_paired_count >= kMaxHelpQuads) return;
  g_paired[g_paired_count] = image;
  ++g_paired_count;
}

void ForgetPaired(const WidgetImage& image) {
  for (int i = 0; i < g_paired_count; ++i) {
    if (!SameImage(g_paired[i], image)) continue;
    g_paired[i] = g_paired[g_paired_count - 1];
    --g_paired_count;
    return;
  }
}

// Called when a pass ends, before the list of quads is cleared.
//
// Anything dropped whose partner never came was not a pair, so the image is
// forgotten and draws normally next pass. One pairing is enough to keep the
// image: two hints can share one picture, and only one of them has to be
// doubled for the rule to be right about the picture.
void EndWidgetPass() {
  for (int i = 0; i < g_help_quad_count; ++i) {
    if (!g_help_quads[i].dropped || g_help_quads[i].paired) continue;
    bool any = false;
    for (int j = 0; j < g_help_quad_count; ++j) {
      if (g_help_quads[j].paired &&
          SameImage(g_help_quads[j].image, g_help_quads[i].image)) {
        any = true;
        break;
      }
    }
    if (!any) ForgetPaired(g_help_quads[i].image);
  }
  g_help_quad_count = 0;
}

// ONE WALK (one_walk.cpp). The right eye of a batch of the interface is the
// chain below run again with the right eye, for the same batch, just after the
// left eye. The decision about a copy of a hint is made one time, by the left
// eye, and the right eye takes it. A second decision would find the left copy
// of the same batch in the list of the pass and pair the batch with itself.
bool g_replay_right = false;
// The batch that draws now is a batch of the video on the screen plane of the
// cut scene window, and the move of its left eye in pixels. DrawInterface
// sets both for one batch.
bool g_video = false;
float g_video_move = 0.0f;
bool g_left_dropped = false;

bool IsDuplicateWidgetDecide(void* verts, uint32_t fvf, uint32_t count);

// True when this batch is the copy of the hint that moves with the eye.
bool IsDuplicateWidget(void* verts, uint32_t fvf, uint32_t count) {
  if (g_replay_right) return g_left_dropped;
  g_left_dropped = IsDuplicateWidgetDecide(verts, fvf, count);
  return g_left_dropped;
}

bool IsDuplicateWidgetDecide(void* verts, uint32_t fvf, uint32_t count) {
  g_in_widget_band = false;
  g_widget_scale = false;
  g_widget_flat = false;
  g_widget_hint = false;
  if (verts == nullptr || fvf != kExpectedFvf || count == 0) return false;
  if (!IsUiCaller(g_caller)) return false;
  const bool from_camera_help =
      g_caller >= kCameraHelpBegin && g_caller < kCameraHelpEnd;

  float x_low = 1e30f, x_high = -1e30f, y_low = 1e30f, y_high = -1e30f;
  float u_low = 1e30f, u_high = -1e30f, v_low = 1e30f, v_high = -1e30f;
  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    const float* f = reinterpret_cast<const float*>(vertex);
    if (f[0] < x_low) x_low = f[0];
    if (f[0] > x_high) x_high = f[0];
    if (f[1] < y_low) y_low = f[1];
    if (f[1] > y_high) y_high = f[1];
    // XYZRHW, diffuse, specular, then one texture coordinate pair, so u and v
    // are the seventh and eighth floats of a 32 byte vertex.
    const float u = f[6];
    const float tv = f[7];
    if (u < u_low) u_low = u;
    if (u > u_high) u_high = u;
    if (tv < v_low) v_low = tv;
    if (tv > v_high) v_high = tv;
    vertex += kVertexStride;
  }

  // SUBTITLES ARE NEVER PART OF A HINT. With the head up, the Hand sits low
  // in the picture, in the rows where the game draws its subtitle box before
  // this file moves it. The box and its letters then fell in the hint's band.
  // They got the hint size about the Hand, and letters that looked like a copy
  // of each other were dropped. So the box went away and the letters came and
  // went. The rows are tested before any transform, so the test is exact.
  // With the subtitle card, a picture of the camera help code inside the
  // subtitle rows (the icon of a button in a line of text) is a part of the
  // subtitle too, so the whole line goes to the card.
  const bool card_takes = g_card_takes_help && g_eye >= 0 &&
                          g_helptext_depth > 0 && g_draw3d_depth == 0;
  if ((!from_camera_help || card_takes) &&
      ((g_caller >= kSubtitleBegin && g_caller < kSubtitleEnd) ||
       InSubtitleBand(y_low, y_high))) {
    if (InWidgetBand(y_low, y_high)) ++g_sub_kept_from_hint;
    return false;
  }

  // The camera help code defines where the widget is. Anything else is only
  // considered when it sits in the same band of screen.
  if (from_camera_help) {
    RememberBand(y_low, y_high, (x_low + x_high) * 0.5f,
                 (y_low + y_high) * 0.5f);
    HintLog("hand hint picture", nullptr, x_low, x_high, y_low, y_high);
  } else if (!InWidgetBand(y_low, y_high)) {
    return false;
  }
  if (g_hint_mode == kHintLeaveAlone) return false;
  // A hint is a hint in every mode. It gets the hint size, and it is left out
  // of the interface depth, because it already has a depth of its own: either
  // the Hand's, or none at all once it is flattened.
  g_in_widget_band = true;
  g_widget_scale = true;
  g_widget_flat = true;

  WidgetImage image;
  image.width = x_high - x_low;
  image.height = y_high - y_low;
  image.u_min = u_low;
  image.u_max = u_high;
  image.v_min = v_low;
  image.v_max = v_high;

  // PAIR THE TWO COPIES BY ORDER, NOT BY DISTANCE.
  //
  // The game draws each hint twice in each pass: the projected copy first, the
  // pinned copy second. That order is a fact about the engine and it does not
  // care where the player is looking.
  //
  // Matching them by screen distance does care. The old rule paired anything
  // within 120 * g_pixel_scale, which is 75 px at a 1600 by 1600 render, and
  // head tracking pulls the pair much further apart than that. Worse, it pulls
  // them apart by DIFFERENT amounts in each eye, because the projected copy
  // moves with the eye and the pinned one does not. So one pass recognised the
  // pair and dropped a copy while the other did not, and the player saw one
  // hint in the left eye and two in the right.
  //
  // So pair by picture first. Distance still matters, but only as a guard: a
  // dialog box has four identical corner tiles on one row, and a row of tabs
  // is identical tiles 156 px apart, and a rule on picture alone would eat
  // them wherever they crossed the hint's band.
  //
  // The guard works again now because the cursor is turned with the head
  // (pointer_map.cpp), so the Hand is placed under the cursor through the same
  // camera that draws it. The two copies then differ only by the eye's own
  // disparity, which is what the 120 px limit was built for. It is loose in y
  // as well as x, because head roll turns part of that disparity vertical.
  const float guard = 120.0f * g_pixel_scale;
  for (int i = 0; i < g_help_quad_count; ++i) {
    if (!SameImage(g_help_quads[i].image, image)) continue;
    const float dx = g_help_quads[i].x_min - x_low;
    const float dy = g_help_quads[i].y_min - y_low;
    if (dx >= guard || dx <= -guard || dy >= guard || dy <= -guard) continue;
    // The partner arrived. The first copy of a pair is the projected one.
    g_help_quads[i].paired = true;
    RememberPaired(image);
    // The first copy was already dropped, so this one is the keeper.
    if (g_help_quads[i].dropped) {
      g_widget_hint = true;
      g_widget_image = image;
      g_widget_x = x_low;
      return false;
    }
    // Otherwise the first copy was drawn, so this one goes. In
    // kHintKeepProjected that is the whole rule. In kHintKeepPinned it only
    // happens until the pair is learned, and the next pass drops the other
    // one.
    ++g_widgets_dropped;
    return true;
  }

  const bool drop = g_hint_mode == kHintKeepPinned && KnownPaired(image);
  if (g_help_quad_count < kMaxHelpQuads) {
    g_help_quads[g_help_quad_count].image = image;
    g_help_quads[g_help_quad_count].y_min = y_low;
    g_help_quads[g_help_quad_count].x_min = x_low;
    g_help_quads[g_help_quad_count].dropped = drop;
    g_help_quads[g_help_quad_count].paired = false;
    ++g_help_quad_count;
  }
  if (drop) {
    ++g_widgets_dropped;
    return true;
  }
  g_widget_hint = true;
  g_widget_image = image;
  g_widget_x = x_low;
  return false;
}

// Writes out where one batch landed, before the shift moves it.
//
// This sits outside the shift path, so that a shift of zero, the case worth
// inspecting, does not switch off the instrument.
void DumpBatch(void* verts, uint32_t fvf, uint32_t count) {
  if (!g_dump || g_dumped >= kMaxDump) return;
  if (verts == nullptr || count == 0 || fvf != kExpectedFvf) return;
  if (!IsUiCaller(g_caller)) return;
  float x_low = 1e30f, x_high = -1e30f, y_low = 1e30f, y_high = -1e30f;
  const uint8_t* v = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    const float* f = reinterpret_cast<const float*>(v);
    if (f[0] < x_low) x_low = f[0];
    if (f[0] > x_high) x_high = f[0];
    if (f[1] < y_low) y_low = f[1];
    if (f[1] > y_high) y_high = f[1];
    v += kVertexStride;
  }
  ModLog("ui dump      : pass %d caller 0x%08X  x %7.1f..%7.1f  y %7.1f..%7.1f"
         "  verts %u", g_eye, g_caller, x_low, x_high, y_low, y_high, count);
  ++g_dumped;
}

// Returns true when the caller must undo the shift after the draw.
//
// There is no IsBadWritePtr here on purpose. It is an expensive call that
// touches every page of the range, and this runs for every interface batch in
// both passes, which is about ninety times a frame. The pointer comes from the
// engine, which is about to read the same bytes itself, so checking it buys
// nothing and costs measurably.
// `fixed` is a shift for this batch in place of the interface depth, or 0.
bool ShiftIn(void* verts, uint32_t fvf, uint32_t count, float fixed = 0.0f) {
  const float shift = fixed != 0.0f ? fixed : g_shift;
  if (shift == 0.0f || fvf != kExpectedFvf || verts == nullptr) return false;
  if (count == 0) return false;
  if (count > kMaxShiftVertices) {
    ++g_too_big;
    return false;
  }
  // The hint stays out of the interface depth. It has a depth of its own.
  if (g_widget_flat) return false;
  if (!IsUiCaller(g_caller)) {
    ++g_refused;
    if (g_accounting) Note(g_caller, verts, count, false);
    return false;
  }
  if (g_accounting) Note(g_caller, verts, count, true);
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* x = reinterpret_cast<float*>(vertex);
    g_saved_x[i] = *x;
    *x += shift;
    vertex += kVertexStride;
  }
  ++g_shifted;
  if (fixed != 0.0f) ++g_help_fixed;
  return true;
}

// Shrink a hint about its own anchor, so it keeps its place while losing its
// size. Both x and y are saved, because unlike the shift this moves rows too.
float g_saved_scale_x[kMaxShiftVertices];
float g_saved_scale_y[kMaxShiftVertices];

bool ScaleIn(void* verts, uint32_t fvf, uint32_t count) {
  if (g_hint_scale == 1.0f || !g_widget_scale) return false;
  if (verts == nullptr || fvf != kExpectedFvf || count == 0) return false;
  if (count > kMaxShiftVertices) return false;

  float y_low = 1e30f, y_high = -1e30f;
  const uint8_t* read = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    const float y = reinterpret_cast<const float*>(read)[1];
    if (y < y_low) y_low = y;
    if (y > y_high) y_high = y;
    read += kVertexStride;
  }
  float ax = 0.0f, ay = 0.0f;
  if (!WidgetAnchor(y_low, y_high, &ax, &ay)) return false;

  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    g_saved_scale_x[i] = p[0];
    g_saved_scale_y[i] = p[1];
    p[0] = ax + (p[0] - ax) * g_hint_scale;
    p[1] = ay + (p[1] - ay) * g_hint_scale;
    vertex += kVertexStride;
  }
  return true;
}

// MAKE THE KEPT HINT FLAT, WITHOUT MOVING IT OFF THE HAND.
//
// Runs last in the chain, so what it records and matches is the final screen
// position, whatever the hint size and the interface depth did before it.
// The first pass records where the hint landed. The second pass moves it to
// the same place, so both eyes draw it at one screen position and there is no
// disparity left to strain against.
float g_saved_flat_x[kMaxShiftVertices];

bool FindFlat(const WidgetImage& image, float* x) {
  for (int i = 0; i < g_flat_count; ++i) {
    if (!SameImage(g_flat[i].image, image)) continue;
    *x = g_flat[i].x;
    return true;
  }
  return false;
}

bool FlattenIn(void* verts, uint32_t fvf, uint32_t count) {
  if (!g_hint_flat || !g_widget_hint) return false;
  if (verts == nullptr || fvf != kExpectedFvf || count == 0) return false;
  if (count > kMaxShiftVertices) return false;

  float x_low = 1e30f;
  const uint8_t* read = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    const float x = reinterpret_cast<const float*>(read)[0];
    if (x < x_low) x_low = x;
    read += kVertexStride;
  }

  if (g_eye == 0) {
    if (!FindFlat(g_widget_image, &x_low) && g_flat_count < kMaxHelpQuads) {
      g_flat[g_flat_count].image = g_widget_image;
      g_flat[g_flat_count].x = x_low;
      ++g_flat_count;
    }
    return false;
  }
  if (g_eye != 1) return false;

  float target = 0.0f;
  // Nothing recorded means the first pass did not draw this hint, and moving
  // it to a place nobody measured would be worse than leaving it alone.
  if (!FindFlat(g_widget_image, &target)) return false;
  const float dx = target - x_low;
  if (dx == 0.0f) return false;

  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    g_saved_flat_x[i] = p[0];
    p[0] += dx;
    vertex += kVertexStride;
  }
  return true;
}

void FlattenOut(void* verts, uint32_t count) {
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    *reinterpret_cast<float*>(vertex) = g_saved_flat_x[i];
    vertex += kVertexStride;
  }
}

// Move the subtitles up toward the middle of the view. Runs before the
// interface scale, so the scale shrinks the lifted position.
bool SubtitleLiftIn(void* verts, uint32_t fvf, uint32_t count) {
  g_sub_member = false;
  if (verts == nullptr || fvf != kExpectedFvf || count == 0) return false;
  if (count > kMaxShiftVertices) return false;
  if (!IsUiCaller(g_caller)) return false;

  const bool from_help =
      g_caller >= kSubtitleBegin && g_caller < kSubtitleEnd;

  float y_low = 1e30f, y_high = -1e30f;
  const uint8_t* read = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    const float y = reinterpret_cast<const float*>(read)[1];
    if (y < y_low) y_low = y;
    if (y > y_high) y_high = y;
    read += kVertexStride;
  }

  // The bar defines the band. Everything else has to be inside one already.
  if (from_help) {
    if (!g_have_sub) {
      g_have_sub = true;
      g_sub_top = y_low;
      g_sub_bottom = y_high;
    } else {
      if (y_low < g_sub_top) g_sub_top = y_low;
      if (y_high > g_sub_bottom) g_sub_bottom = y_high;
    }
  } else if (!InSubtitleBand(y_low, y_high)) {
    return false;
  }

  // Measured from the band, not from each batch, so the bar and its letters
  // move by the same amount and stay together. Last frame's band serves when
  // this frame has none yet.
  float band = (y_low + y_high) * 0.5f;
  if (g_have_sub) {
    band = (g_sub_top + g_sub_bottom) * 0.5f;
  } else if (g_have_prev_sub) {
    band = (g_prev_sub_top + g_prev_sub_bottom) * 0.5f;
  }
  g_sub_member = true;
  g_sub_center = band;
  // With a set angle, SubtitlePlaceIn places them after the size instead.
  if (g_subtitle_degrees > 0.0f) return false;
  if (g_subtitle_lift <= 0.0f) return false;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info)) || info->half_res_y <= 0.0f) {
    return false;
  }
  const float dy = (band - info->half_res_y) * g_subtitle_lift;
  if (dy <= 0.0f) return false;
  g_sub_center = band - dy;

  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    g_saved_sub_y[i] = p[1];
    p[1] -= dy;
    vertex += kVertexStride;
  }
  return true;
}

void SubtitleLiftOut(void* verts, uint32_t count) {
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    reinterpret_cast<float*>(vertex)[1] = g_saved_sub_y[i];
    vertex += kVertexStride;
  }
}

// THE INTERFACE AT A SIZE A HEADSET CAN USE.
//
// The game sizes its on screen help from the height of the render. A tooltip
// is a 25th of the screen height and its text is two thirds of that, so at a
// 2880 pixel render the text is about 77 pixels tall. On a monitor that fills
// a small part of a 60 degree view. In a headset it fills a large part of a
// 110 degree one, and a subtitle runs from one edge of the view to the other.
//
// So the help layer is scaled toward the middle of the view, which makes it
// smaller and brings it nearer the center in one step. It is chosen by which
// game function is drawing, not by caller address alone, because the text
// submitter and the widget toolkit also draw loading screens, the front end
// and menus:
//
//   HelpText's FinishFrame callback 0x005CD020: subtitles, help text, the
//     hint beside the Hand, and the Creature panel (0x005178D0).
//   HelpSystem::Draw3D 0x005C59A0: the tooltip list.
//
// The subtitle bar is the one exception to the scopes. HelpText queues it
// inside the help layer, but the rect queue callback 0x0081E7D0 draws it
// later, outside both scopes. Its letters get the size, so the bar must get
// it too. Otherwise the bar stays full width while its letters shrink away
// from it. So the caller range of the bar also counts as the help layer.
//
// Menus and dialogs keep their size. Their text is a fixed 20 to 35 pixels at
// any render size, which is already small in a headset. They are also the only
// interface anyone clicks, so a move would take them away from their hit
// test. The menu scope below (h_SetupFinish) makes sure of this.
float g_ui_scale = 1.0f;
int g_help_depth = 0;  // inside the game's help drawing
// The same two scopes one at a time, for the interface panel. Only the
// HelpText scope goes to the panel. HelpSystem::Draw3D draws in the walk.
int g_helptext_depth = 0;
int g_draw3d_depth = 0;
float g_saved_ui_x[kMaxShiftVertices];
float g_saved_ui_y[kMaxShiftVertices];
uint64_t g_ui_scaled = 0;

bool InHelpLayer() {
  return g_help_depth > 0 ||
         (g_caller >= kSubtitleBegin && g_caller < kSubtitleEnd);
}

bool UiScaleIn(void* verts, uint32_t fvf, uint32_t count) {
  if (g_ui_scale == 1.0f || !InHelpLayer()) return false;
  // On the screen of the cut scene window the help layer has the game's own
  // size. The game made it for a screen of that kind.
  if (CutsceneWindowFlat()) return false;
  // A hint has a size of its own, set by hint_scale_pct.
  if (g_widget_scale) return false;
  if (verts == nullptr || fvf != kExpectedFvf || count == 0) return false;
  if (count > kMaxShiftVertices) return false;
  if (!IsUiCaller(g_caller)) return false;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info)) || info->half_res_x <= 0.0f ||
      info->half_res_y <= 0.0f) {
    return false;
  }
  const float cx = info->half_res_x;
  const float cy = info->half_res_y;
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    g_saved_ui_x[i] = p[0];
    g_saved_ui_y[i] = p[1];
    p[0] = cx + (p[0] - cx) * g_ui_scale;
    p[1] = cy + (p[1] - cy) * g_ui_scale;
    vertex += kVertexStride;
  }
  ++g_ui_scaled;
  return true;
}

void UiScaleOut(void* verts, uint32_t count) {
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    p[0] = g_saved_ui_x[i];
    p[1] = g_saved_ui_y[i];
    vertex += kVertexStride;
  }
}

// Move a subtitle batch down, after the interface size, so the middle of the
// band lands g_subtitle_degrees below the middle of the view. `sized` says
// whether UiScaleIn scaled this batch, so each batch is placed from where it
// really is. The box and its letters get the same move.
bool SubtitlePlaceIn(void* verts, uint32_t fvf, uint32_t count, bool sized) {
  if (!g_sub_member || g_subtitle_degrees <= 0.0f) return false;
  // On the screen of the cut scene window the subtitles stay where the game
  // puts them: at the bottom of the picture, above the bottom bar.
  if (CutsceneWindowFlat()) return false;
  if (verts == nullptr || fvf != kExpectedFvf || count == 0) return false;
  if (count > kMaxShiftVertices) return false;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info)) || info->half_res_y <= 0.0f ||
      !(info->inv_half_tan_fov_y > 0.0f)) {
    return false;
  }
  const float cy = info->half_res_y;
  const float scale = sized ? g_ui_scale : 1.0f;
  const float now = cy + (g_sub_center - cy) * scale;
  // The vertical term of the projection: pixels per unit of tan(angle).
  const float focal = cy * info->inv_half_tan_fov_y;
  const float target =
      cy + tanf(g_subtitle_degrees * 0.01745329f) * focal;
  const float dy = target - now;
  if (g_caller >= kSubtitleBegin && g_caller < kSubtitleEnd) {
    static bool seen = false;
    if (!seen && g_eye == 0) {
      seen = true;
      HintSampleSoon();
    }
    HintLog("subtitle bar", nullptr, 0.0f, 0.0f,
            target - 0.5f * (g_sub_bottom - g_sub_top) * scale,
            target + 0.5f * (g_sub_bottom - g_sub_top) * scale);
  }
  if (dy < 0.5f && dy > -0.5f) return false;
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    g_saved_place_y[i] = p[1];
    p[1] += dy;
    vertex += kVertexStride;
  }
  ++g_sub_placed;
  return true;
}

void SubtitlePlaceOut(void* verts, uint32_t count) {
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    reinterpret_cast<float*>(vertex)[1] = g_saved_place_y[i];
    vertex += kVertexStride;
  }
}

// THE MENUS GET THE INTERFACE DEPTH AND NOTHING ELSE.
//
// Every SetupBox dialog, in the game and in the front end, is drawn by one
// FinishFrame callback, 0x006424E0. It holds the only two calls to
// SetupBox::Draw 0x00408340 in the exe. It is a sibling of the HelpText
// callback, so the two never nest.
//
// Without this scope the hint rule would act on the Options > Controls page.
// Each row of that page draws its key icon through the CameraHelp quads
// (return address 0x004477C0), the same code that draws the hint beside the
// Hand. So every row would be a hint band, the rows and the list box around
// them would get the hint size about the icons, and the list would fall into
// small clusters. The subtitle lift could also move a menu row away from its
// hit test.
//
// Inside this scope a batch gets only the interface depth: no hint rule, no
// hint size, no interface size and no subtitle lift.
typedef void(__stdcall* SetupFinishFn)(void* arg);
SetupFinishFn o_SetupFinish = nullptr;
const uintptr_t kSetupFinish = 0x006424E0;
int g_menu_depth = 0;  // inside the menu drawing
uint64_t g_menu_batches = 0;

void __stdcall h_SetupFinish(void* arg) {
  ++g_menu_depth;
  o_SetupFinish(arg);
  // Rule 5 of docs/ui-layer.md, "Rules for the switch": the game's target is
  // back at the end of each callback scope.
  PanelRelease();
  --g_menu_depth;
}

// ONE TOOLTIP, NOT TWO.
//
// The belief gained notice, the Storehouse readout and the Temple readout are
// HelpSystem tooltips. The game draws its tooltip list twice in every eye
// pass: once from HelpSystem::Draw3D (fn_00447850, returning to 0x005C5B36)
// and once from the HelpText FinishFrame callback (returning to 0x005CCE46).
// On a monitor the two copies land on the same pixels. In stereo they do not,
// so the player saw each tooltip twice in each eye.
//
// The dedup for the hint beside the Hand could not catch these. It builds its
// band around the mouse icon quad, and a tooltip has no icon (icon type 3).
//
// Every tooltip ends in one call to CameraHelp::DrawKeyOrMouse, so that call
// is the unit. A call is known by its text, its icon and its icon type, never
// by where it lands. The first copy in a pass is drawn and a second with the
// same key is skipped. Only the draw is skipped: the fade is advanced by the
// caller, so it runs exactly as before.
//
// The copy kept is the one at the Hand. Like the hint, it is drawn flat: the
// first pass records where it lands and the second pass moves it there, so
// both eyes draw it at one screen position. It gets the interface size about
// its own center, so it stays beside the Hand.
typedef void(__cdecl* DrawKeyOrMouseFn)(uint32_t blink, uint32_t button,
                                        uint32_t row_or_label,
                                        const wchar_t* text, uint32_t x,
                                        uint32_t y, uint32_t size,
                                        uint32_t flags, uint32_t colour_a,
                                        uint32_t colour_b, uint32_t alpha);
DrawKeyOrMouseFn o_DrawKeyOrMouse = nullptr;
const uintptr_t kDrawKeyOrMouse = 0x00447EA0;
// The icon inside a speech bubble. A bubble can show the same key twice.
const uintptr_t kFromBubble = 0x005763D1;
const uint32_t kIconTypeNone = 3;

const int kMaxTooltips = 32;
uint32_t g_tooltip_keys[kMaxTooltips];  // drawn in this pass
int g_tooltip_count = 0;
struct TooltipFlat {
  uint32_t key;
  float x;
};
TooltipFlat g_tooltip_flat[kMaxTooltips];  // where each landed in pass 0
int g_tooltip_flat_count = 0;

// THE TOOLTIP AT THE HAND (docs/ui-layer.md, "The tooltip at the Hand"). While
// the controller places the Hand, the Hand is the real hand, 40 to 80 cm from
// the eyes. The flat copy above has no disparity, so it shows at the far end of
// the view and is double while the player looks at the hand, and at
// ui_scale_pct 25 its text is a few pixels high. So with the direct hand a
// tooltip goes to the Hand in each eye (HintAtHandPoint): the left eye scales
// it about the Hand's point, and the right eye puts it at the same place from
// its own Hand's point. It then has the Hand's disparity and shows at the
// Hand's depth, at the game's layout. Its box is kHintHandCm of the room high
// at that depth, so it reads like a label on the hand. The game draws its Hand
// after the help, so the mesh hides a label on it. The point is at the tip of
// the index finger, and the mesh lies from there back toward the wrist, mostly
// up or down in the view (runs bk7 and bk10). So the label moves
// kHintHandSideCm to the right of the game's place. ui_scale_pct,
// hint_scale_pct and hint_flat do not change it. With the mouse, nothing of
// this runs.
const float kHintHandCm = 2.5f;
const float kHintHandSideCm = 6.0f;
struct TooltipHand {
  uint32_t key;
  float offset[2];  // the top left of the first batch less the Hand's point
};
TooltipHand g_tooltip_hand[kMaxTooltips];  // from pass 0
int g_tooltip_hand_count = 0;
bool g_tooltip_at_hand = false;
float g_tooltip_hand_xy[2] = {0.0f, 0.0f};
float g_tooltip_first[2] = {0.0f, 0.0f};
float g_tooltip_offset[2] = {0.0f, 0.0f};
float g_tooltip_hand_scale = 1.0f;
uint64_t g_tooltips_at_hand = 0;

// THE HINT CARD (hint_card.cpp, docs/controller-hints.md, "The hint card").
// While a controller owns the pointer, a tooltip does not draw in an eye
// picture at all. Its words go to the card, which the headset shows as one
// quad beside the wrist. One picture in the room cannot be double. The route is
// latched at the start of the left eye pass, so both eye passes agree.
bool g_card_route = false;
uint64_t g_tooltips_to_card = 0;

bool g_in_tooltip = false;
uint32_t g_tooltip_key = 0;
// For the log of where a tooltip lands (HintLog): the call of the game.
const wchar_t* g_tooltip_text = nullptr;
uint32_t g_tooltip_call_x = 0;
uint32_t g_tooltip_call_y = 0;
uint32_t g_tooltip_call_size = 0;
uint64_t g_ui_frame = 0;
int g_hint_logs = 0;
const int kMaxHintLogs = 160;
uint64_t g_hint_log_next = 0;
bool g_tooltip_anchored = false;
float g_tooltip_ax = 0.0f;
float g_tooltip_ay = 0.0f;
float g_tooltip_dx = 0.0f;

// ONE WALK. The batches of one tooltip come in the pass of the left eye, and
// each goes to the right eye just after (DrawInterface). The right eye anchors
// the tooltip from its own Hand's point, as its own pass did. So each eye has
// its own copy of the anchor, and the copy of the eye that draws is loaded.
struct TooltipState {
  bool anchored, at_hand;
  float ax, ay, dx, scale;
  float hand_xy[2], first[2], offset[2];
};
TooltipState g_tooltip_right = {};

void SaveTooltip(TooltipState* s) {
  s->anchored = g_tooltip_anchored;
  s->at_hand = g_tooltip_at_hand;
  s->ax = g_tooltip_ax;
  s->ay = g_tooltip_ay;
  s->dx = g_tooltip_dx;
  s->scale = g_tooltip_hand_scale;
  for (int i = 0; i < 2; ++i) {
    s->hand_xy[i] = g_tooltip_hand_xy[i];
    s->first[i] = g_tooltip_first[i];
    s->offset[i] = g_tooltip_offset[i];
  }
}

void LoadTooltip(const TooltipState& s) {
  g_tooltip_anchored = s.anchored;
  g_tooltip_at_hand = s.at_hand;
  g_tooltip_ax = s.ax;
  g_tooltip_ay = s.ay;
  g_tooltip_dx = s.dx;
  g_tooltip_hand_scale = s.scale;
  for (int i = 0; i < 2; ++i) {
    g_tooltip_hand_xy[i] = s.hand_xy[i];
    g_tooltip_first[i] = s.first[i];
    g_tooltip_offset[i] = s.offset[i];
  }
}
uint64_t g_tooltips_kept = 0;
uint64_t g_tooltips_dropped = 0;
float g_saved_tip_x[kMaxShiftVertices];
float g_saved_tip_y[kMaxShiftVertices];

uint32_t TooltipKey(uint32_t icon, uint32_t icon_type, const wchar_t* text) {
  uint32_t h = 2166136261u;  // FNV-1a
  if (text != nullptr) {
    // The game runs wcslen on this same pointer, so reading it is safe.
    for (int i = 0; i < 256 && text[i] != 0; ++i) {
      h ^= static_cast<uint32_t>(text[i]);
      h *= 16777619u;
    }
  }
  h ^= icon * 0x9E3779B1u;
  h ^= icon_type << 24;
  return h;
}

bool FindTooltipFlat(uint32_t key, float* x) {
  for (int i = 0; i < g_tooltip_flat_count; ++i) {
    if (g_tooltip_flat[i].key != key) continue;
    *x = g_tooltip_flat[i].x;
    return true;
  }
  return false;
}

// THE LOG OF WHERE THE HELP LANDS, for the tests of the hint at the Hand. On
// a sample frame each tooltip, hand hint and subtitle band goes in the log
// with the eye, its box as the game drew it, the pointer and the Hand's point
// in this eye. A sample frame comes every 150 frames, and when a tooltip with
// a new text first shows.
uint64_t g_hint_sample_frame = ~0ull;
const int kSeenTips = 16;
uint32_t g_seen_tips[kSeenTips];
int g_seen_tip_count = 0;

bool HintSample() {
  if (g_hint_logs >= kMaxHintLogs) return false;
  if (g_eye == 0 && g_ui_frame >= g_hint_log_next &&
      g_hint_sample_frame != g_ui_frame) {
    g_hint_sample_frame = g_ui_frame;
    g_hint_log_next = g_ui_frame + 150;
  }
  return g_hint_sample_frame == g_ui_frame;
}

void HintSampleSoon() { g_hint_log_next = 0; }

void HintLog(const char* what, const wchar_t* text, float x0, float x1,
             float y0, float y1) {
  if (g_eye < 0 || !HintSample()) return;
  ++g_hint_logs;
  char narrow[48] = {0};
  if (text != nullptr) {
    int n = 0;
    for (; n < 46 && text[n] != 0; ++n) {
      narrow[n] = (text[n] >= 32 && text[n] < 127) ? static_cast<char>(text[n])
                                                   : '?';
    }
    narrow[n] = 0;
  }
  const int32_t* pointer = reinterpret_cast<const int32_t*>(0x00E852C0);
  float hx = 0.0f, hy = 0.0f, meters = 0.0f;
  const bool hand = HandGlowEyePoint(g_eye, &hx, &hy, &meters);
  char at[96] = "the controller does not place the Hand";
  if (hand) {
    _snprintf_s(at, _TRUNCATE, "the Hand's point at %.0f %.0f, %.2f m", hx, hy,
                meters);
  }
  ModLog("ui hint      : frame %llu eye %d, %s \"%s\": x %.0f..%.0f y "
         "%.0f..%.0f (%.0f px high, the call asked %u), the pointer at %d %d, "
         "%s", g_ui_frame, g_eye, what, narrow, x0, x1, y0, y1, y1 - y0,
         g_tooltip_call_size, pointer[0], pointer[1], at);
}

// The first batch of a tooltip, with the controller placing the Hand: the
// scale and the place at the Hand for this tooltip in this eye. x and y are
// the top left of the batch, and height its height in pixels. False with the
// mouse, outside the eye passes, or when a value is not usable.
bool TooltipAtHand(float x, float y, float height) {
  if (g_eye < 0 || g_eye > 1) return false;
  float hx = 0.0f, hy = 0.0f, meters = 0.0f;
  if (!HandGlowEyePoint(g_eye, &hx, &hy, &meters)) return false;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info))) return false;
  const float focal = info->half_res_y * info->inv_half_tan_fov_y;
  // The size that the game asked for, or the first batch when that is not
  // usable.
  float box = static_cast<float>(g_tooltip_call_size);
  if (!(box >= 8.0f && box <= 400.0f)) box = height;
  const float s =
      controller_math::HintAtHandScale(focal, meters, box, kHintHandCm);
  if (s <= 0.0f) return false;
  g_tooltip_hand_scale = s;
  g_tooltip_hand_xy[0] = hx;
  g_tooltip_hand_xy[1] = hy;
  g_tooltip_first[0] = x;
  g_tooltip_first[1] = y;
  // The move to the side is in the room, so it is not scaled with the box.
  const float room_px = focal * 0.01f / meters / s;  // one cm, before s
  g_tooltip_offset[0] = x - hx + kHintHandSideCm * room_px;
  g_tooltip_offset[1] = y - hy;
  int found = -1;
  for (int i = 0; i < g_tooltip_hand_count; ++i) {
    if (g_tooltip_hand[i].key == g_tooltip_key) found = i;
  }
  if (g_eye == 0) {
    if (found < 0 && g_tooltip_hand_count < kMaxTooltips) {
      found = g_tooltip_hand_count++;
      g_tooltip_hand[found].key = g_tooltip_key;
    }
    if (found >= 0) {
      g_tooltip_hand[found].offset[0] = g_tooltip_offset[0];
      g_tooltip_hand[found].offset[1] = g_tooltip_offset[1];
    }
  } else if (found >= 0) {
    // The right eye: the place of the left eye, from its own Hand's point.
    g_tooltip_offset[0] = g_tooltip_hand[found].offset[0];
    g_tooltip_offset[1] = g_tooltip_hand[found].offset[1];
  }
  ++g_tooltips_at_hand;
  if (HintSample()) {
    ModLog("ui hint      : frame %llu eye %d, the tooltip goes to the Hand: "
           "scale %.3f (box %.0f px, %.1f cm at %.2f m), the top left at "
           "%.0f %.0f", g_ui_frame, g_eye, s, box, kHintHandCm, meters,
           hx + s * g_tooltip_offset[0], hy + s * g_tooltip_offset[1]);
  }
  return true;
}

// One batch of the tooltip being drawn. The first batch is its frame, whose
// center is the anchor for the size and whose left edge is what the flat copy
// is measured by. Every later batch moves by the same amounts.
bool TooltipIn(void* verts, uint32_t fvf, uint32_t count) {
  if (verts == nullptr || fvf != kExpectedFvf || count == 0) return false;
  if (count > kMaxShiftVertices) return false;
  if (!g_tooltip_anchored) {
    float x_low = 1e30f, x_high = -1e30f, y_low = 1e30f, y_high = -1e30f;
    const uint8_t* read = static_cast<const uint8_t*>(verts);
    for (uint32_t i = 0; i < count; ++i) {
      const float* f = reinterpret_cast<const float*>(read);
      if (f[0] < x_low) x_low = f[0];
      if (f[0] > x_high) x_high = f[0];
      if (f[1] < y_low) y_low = f[1];
      if (f[1] > y_high) y_high = f[1];
      read += kVertexStride;
    }
    g_tooltip_anchored = true;
    HintLog("tooltip", g_tooltip_text, x_low, x_high, y_low, y_high);
    g_tooltip_ax = (x_low + x_high) * 0.5f;
    g_tooltip_ay = (y_low + y_high) * 0.5f;
    g_tooltip_dx = 0.0f;
    g_tooltip_at_hand = TooltipAtHand(x_low, y_low, y_high - y_low);
    const float left = g_tooltip_ax + (x_low - g_tooltip_ax) * g_ui_scale;
    if (g_hint_flat && g_eye == 0) {
      float known = 0.0f;
      if (!FindTooltipFlat(g_tooltip_key, &known) &&
          g_tooltip_flat_count < kMaxTooltips) {
        g_tooltip_flat[g_tooltip_flat_count].key = g_tooltip_key;
        g_tooltip_flat[g_tooltip_flat_count].x = left;
        ++g_tooltip_flat_count;
      }
    } else if (g_hint_flat && g_eye == 1) {
      float target = 0.0f;
      if (FindTooltipFlat(g_tooltip_key, &target)) g_tooltip_dx = target - left;
    }
  }
  if (g_tooltip_at_hand) {
    uint8_t* vertex = static_cast<uint8_t*>(verts);
    for (uint32_t i = 0; i < count; ++i) {
      float* p = reinterpret_cast<float*>(vertex);
      g_saved_tip_x[i] = p[0];
      g_saved_tip_y[i] = p[1];
      controller_math::HintAtHandPoint(g_tooltip_hand_xy, g_tooltip_first,
                                       g_tooltip_offset, g_tooltip_hand_scale,
                                       p);
      vertex += kVertexStride;
    }
    return true;
  }
  if (g_ui_scale == 1.0f && g_tooltip_dx == 0.0f) return false;
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    g_saved_tip_x[i] = p[0];
    g_saved_tip_y[i] = p[1];
    p[0] = g_tooltip_ax + (p[0] - g_tooltip_ax) * g_ui_scale + g_tooltip_dx;
    p[1] = g_tooltip_ay + (p[1] - g_tooltip_ay) * g_ui_scale;
    vertex += kVertexStride;
  }
  return true;
}

void TooltipOut(void* verts, uint32_t count) {
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    p[0] = g_saved_tip_x[i];
    p[1] = g_saved_tip_y[i];
    vertex += kVertexStride;
  }
}

// THE ARGUMENTS. blink is -1 for a key cap, else the blink style. button is
// the mouse button code (1 left, 2 right, 4 middle, 8 and 0x10 the wheel), or
// 0 for a key cap. row_or_label is the row of the mouse picture (0 to 2), 3 for
// no picture, or for a key cap a pointer to the wide string in the cap.
//
// CONTROLLER WORDS. While a controller owns the pointer, a hint with
// a mouse picture becomes a key cap with the controller's word. The call is
// changed here and then goes down the tooltip path below, so it is a single
// unit like any tooltip: kept once in a pass, moved as a whole, drawn flat.
// The key cap draws through DrawBox and not through the picture quads, so the
// picture pairing rule never sees it. A hint for an input the controller does
// not have is not drawn, and a key cap for a keyboard key is not drawn. The
// callers still run their fades. ControllerHintsActive is latched at the start
// of the left eye, so both eyes decide the same way.
void __cdecl h_DrawKeyOrMouse(uint32_t blink, uint32_t button,
                              uint32_t row_or_label, const wchar_t* text,
                              uint32_t x, uint32_t y, uint32_t size,
                              uint32_t flags, uint32_t colour_a,
                              uint32_t colour_b, uint32_t alpha) {
  const uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress());
  ControllerHintsNoteDraw(static_cast<uint32_t>(from), blink, button,
                          row_or_label, text);
  const bool key_cap = blink == 0xFFFFFFFFu;
  bool converted = false;
  const uint32_t game_button = button;
  if (g_eye >= 0 && !g_in_tooltip && g_menu_depth == 0 &&
      g_hint_mode != kHintLeaveAlone && from != kFromBubble &&
      ControllerHintsActive()) {
    if (key_cap) {
      ControllerHintsCountHidden();
      return;
    }
    if (row_or_label != kIconTypeNone) {
      const wchar_t* label = ControllerHintLabel(button);
      if (label == nullptr) {
        ControllerHintsCountHidden();
        return;
      }
      blink = 0xFFFFFFFFu;
      button = 0;
      row_or_label = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(label));
      converted = true;
      ControllerHintsCountConverted();
    }
  }
  // Outside the eye passes the two copies land on one another, and the hint
  // with an icon already has its own dedup. Inside a menu the game draws it
  // as it is. No dialog calls this today, but the menu scope must win if one
  // does.
  if (g_eye < 0 || g_in_tooltip || g_menu_depth > 0 ||
      g_hint_mode == kHintLeaveAlone ||
      (row_or_label != kIconTypeNone && !converted) || from == kFromBubble) {
    o_DrawKeyOrMouse(blink, button, row_or_label, text, x, y, size, flags,
                     colour_a, colour_b, alpha);
    return;
  }
  // With the hint card, this hint draws in no eye picture. The caller still
  // runs its fade.
  if (g_card_route) {
    HintCardGameHint(
        g_eye,
        converted ? reinterpret_cast<const wchar_t*>(
                        static_cast<uintptr_t>(row_or_label))
                  : nullptr,
        converted ? game_button : 0, text);
    ++g_tooltips_to_card;
    return;
  }
  const uint32_t key = TooltipKey(blink, row_or_label, text);
  for (int i = 0; i < g_tooltip_count; ++i) {
    if (g_tooltip_keys[i] == key) {
      ++g_tooltips_dropped;
      return;
    }
  }
  if (g_tooltip_count < kMaxTooltips) g_tooltip_keys[g_tooltip_count++] = key;
  ++g_tooltips_kept;
  // A text not seen before makes this frame a sample frame for the log.
  if (g_eye == 0) {
    bool seen = false;
    for (int i = 0; i < g_seen_tip_count; ++i) seen |= g_seen_tips[i] == key;
    if (!seen && g_seen_tip_count < kSeenTips) {
      g_seen_tips[g_seen_tip_count++] = key;
      g_hint_log_next = 0;
    }
  }
  g_in_tooltip = true;
  g_tooltip_key = key;
  g_tooltip_anchored = false;
  g_tooltip_right.anchored = false;
  g_tooltip_text = text;
  g_tooltip_call_x = x;
  g_tooltip_call_y = y;
  g_tooltip_call_size = size;
  o_DrawKeyOrMouse(blink, button, row_or_label, text, x, y, size, flags,
                   colour_a, colour_b, alpha);
  g_in_tooltip = false;
}

// The scopes the interface scale applies in. Both only mark that the help
// layer is drawing.
typedef void(__stdcall* HelpTextFinishFn)(void* arg);
typedef void(__fastcall* HelpDraw3DFn)(void* self, void* unused);
HelpTextFinishFn o_HelpTextFinish = nullptr;
HelpDraw3DFn o_HelpDraw3D = nullptr;
const uintptr_t kHelpTextFinish = 0x005CD020;
const uintptr_t kHelpDraw3D = 0x005C59A0;

// A TEST KEY FOR THE CONTROLLER HINTS (hint_selftest=1). A desktop run seldom
// reaches a real hint, so this draws five of its own after the help layer, two
// copies of each, as the game does. A left button, a right button, a middle
// button (no controller input, so hidden while a controller owns the pointer)
// and a wheel hint. With the mouse as the owner they show as the game draws
// them, and with a controller as the owner they show as key caps, once in each
// eye. A fifth call is a key cap for a keyboard key, which is hidden then.
bool g_hint_selftest = false;

void HintSelfTest() {
  struct Case {
    uint32_t blink;
    uint32_t button;
    uint32_t row;
    const wchar_t* text;
    uint32_t y;
    uint32_t size;
  };
  static const Case cases[] = {
      {0, 1, 1, L"Test left button", 400, 24},
      {0, 2, 1, L"Test right button", 540, 32},
      {0, 4, 1, L"Test middle button", 680, 24},
      {0, 0x10, 2, L"Test wheel", 820, 24},
      // A key cap of the game, for a keyboard key: hidden with a controller.
      {0xFFFFFFFFu, 0, reinterpret_cast<uint32_t>(L"Space"), L"Test key", 960,
       24},
  };
  static uint32_t colour_text = 0xFFFFFFFFu;
  static uint32_t colour_box = 0xFF202020u;
  for (const Case& c : cases) {
    for (int copy = 0; copy < 2; ++copy) {
      h_DrawKeyOrMouse(c.blink, c.button, c.row, c.text, 900, c.y, c.size, 0,
                       reinterpret_cast<uint32_t>(&colour_text),
                       reinterpret_cast<uint32_t>(&colour_box), 255);
    }
  }
}

// A TEST KEY FOR THE SUBTITLE CARD (subtitle_selftest=1). A desktop run shows
// no subtitle, so this draws two lines of help text of its own inside the
// HelpText scope, at the bottom of the render where the game draws its
// subtitles. The game's own text code draws them, so they are real batches of
// that scope: with the card they go to the panel target, and without it they
// draw in the eye pictures as the help text does.
bool g_subtitle_selftest = false;

void SubtitleSelfTest() {
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info)) || info->res_x <= 0 ||
      info->res_y <= 0) {
    return;
  }
  const uint32_t w = static_cast<uint32_t>(info->res_x);
  const uint32_t h = static_cast<uint32_t>(info->res_y);
  static uint32_t colour_text = 0xFFFFFFFFu;
  static uint32_t colour_box = 0xFF202020u;
  const wchar_t* const lines[2] = {
      L"A test line of help text for the subtitle card",
      L"The second line of the test"};
  for (int i = 0; i < 2; ++i) {
    o_DrawKeyOrMouse(0xFFFFFFFFu, 0, reinterpret_cast<uint32_t>(L"Test"),
                     lines[i], w / 5, h * (88 + 5 * i) / 100, h / 25, 0,
                     reinterpret_cast<uint32_t>(&colour_text),
                     reinterpret_cast<uint32_t>(&colour_box), 255);
  }
}

void __stdcall h_HelpTextFinish(void* arg) {
  ++g_help_depth;
  ++g_helptext_depth;
  o_HelpTextFinish(arg);
  if (g_hint_selftest && g_eye >= 0) HintSelfTest();
  if (g_subtitle_selftest && g_eye >= 0) SubtitleSelfTest();
  // The world of this eye pass is drawn, so the Hand's facts are ready.
  if (g_eye == 0) HintCardLatchContext(g_eye);
  PanelRelease();
  --g_helptext_depth;
  --g_help_depth;
}

void __fastcall h_HelpDraw3D(void* self, void* unused) {
  ++g_help_depth;
  ++g_draw3d_depth;
  o_HelpDraw3D(self, unused);
  PanelRelease();
  --g_draw3d_depth;
  --g_help_depth;
}

// The rect queue callback, which draws the subtitle box. Hooked only while
// ui_panel is on, to put the game's target back at the end of its scope.
typedef void(__stdcall* RectQueueFinishFn)(void* arg);
RectQueueFinishFn o_RectQueueFinish = nullptr;
const uintptr_t kRectQueueFinish = 0x0081E7D0;

void __stdcall h_RectQueueFinish(void* arg) {
  o_RectQueueFinish(arg);
  PanelRelease();
}

// THE CINEMA BARS, HIDDEN WITHOUT A CHANGE TO THE GAME'S STATE.
//
// In a cut scene the game paints two opaque black bars over the scene. On a
// monitor they make a wide picture. In a headset they are two black slabs
// fixed to the face. At full width they cover 44 percent of a square render.
//
// The flag at HelpSystem+0x45E8 has 69 readers (audio, the Creature, the
// interface, the camera and more), so it must stay as the game sets it. The
// bars read only the render copy of the transition, the float at 0x00EB9950
// (0 to 1). Only the tick at 0x005C6C00 writes a value other than 0 to it.
// LH3DRender::FinishFrame reads it at 0x0082F652, and the screen fade reads
// it at 0x00870130. At 0 both skip their bars, and the fade then covers the
// whole view.
//
// So UiBarsBegin sets it to 0 while FinishFrame runs and UiBarsEnd puts the
// game's value back. Nothing inside FinishFrame writes it, so the restore is
// exact. The game logic reads the HelpSystem fields, not this copy, so scripts
// see the transition end at the same time as before.
//
// fn_005C5780 gives the bar height to the HelpText region, and the subtitles
// sit above the bottom bar. While the bars are hidden, h_HelpBarHeight returns
// 0, so the subtitles stay where they are in normal play. Hidden bars need no
// room.
// Its only caller is 0x005C57D4, with no arguments and no stack cleanup.
const uintptr_t kWideScreenShown = 0x00EB9950;  // float 0..1
const uintptr_t kHelpBarHeight = 0x005C5780;    // int __cdecl(void)
typedef int(__cdecl* BarHeightFn)();
BarHeightFn o_HelpBarHeight = nullptr;
bool g_cinema_bars = false;  // false hides the bars, the default
bool g_bars_zeroed = false;  // UiBarsBegin wrote 0, so UiBarsEnd must restore
uint64_t g_bars_hidden_frames = 0;

int __cdecl h_HelpBarHeight() {
  return (g_cinema_bars || CutsceneWindowFlat()) ? o_HelpBarHeight() : 0;
}

void ScaleOut(void* verts, uint32_t count) {
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* p = reinterpret_cast<float*>(vertex);
    p[0] = g_saved_scale_x[i];
    p[1] = g_saved_scale_y[i];
    vertex += kVertexStride;
  }
}

void ShiftOut(void* verts, uint32_t count) {
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    *reinterpret_cast<float*>(vertex) = g_saved_x[i];
    vertex += kVertexStride;
  }
}

// Moves each x of a batch by `move` pixels, with none of the tests of the
// interface depth. True when ShiftOut must put the batch back after the draw.
// A batch of the video as it came. DrawAndClip2D clips a batch in place: a
// vertex past an edge of the render goes to the edge and gets the texture
// place of its new point. In one walk the right eye draws the same vertices
// after the left eye, so the whole batch goes back after each draw, not only
// its x.
const uint32_t kMaxVideoVertices = 64;
uint8_t g_saved_video[kMaxVideoVertices * kVertexStride];

// Moves each x of a batch of the video by `move` pixels. True when VideoOut
// must put the batch back after the draw.
bool VideoIn(void* verts, uint32_t fvf, uint32_t count, float move) {
  if (fvf != kExpectedFvf || verts == nullptr || count == 0 ||
      count > kMaxVideoVertices) {
    return false;
  }
  memcpy(g_saved_video, verts, count * kVertexStride);
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kVertexStride) {
    *reinterpret_cast<float*>(vertex) += move;
  }
  return true;
}

void VideoOut(void* verts, uint32_t count) {
  memcpy(verts, g_saved_video, count * kVertexStride);
}

bool MoveIn(void* verts, uint32_t fvf, uint32_t count, float move) {
  if (move == 0.0f || fvf != kExpectedFvf || verts == nullptr || count == 0 ||
      count > kMaxShiftVertices) {
    return false;
  }
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    float* x = reinterpret_cast<float*>(vertex);
    g_saved_x[i] = *x;
    *x += move;
    vertex += kVertexStride;
  }
  return true;
}

// THE INTERFACE ON ITS OWN SURFACE (docs/ui-layer.md, "Which batches go to the
// panel").
//
// A panel batch is one of these, and nothing else:
//
//   inside the Setup callback 0x006424E0            the menus
//   inside the HelpText callback 0x005CD020 only,   the help text, the
//     not a tooltip and not a kept hint copy        subtitle letters, the
//                                                   Creature panel
//   the rect queue caller 0x0081E3C0 to 0x0081E58C  the subtitle box
//
// A panel batch gets none of the steps above: no interface depth, no
// interface size, no subtitle lift and no subtitle placement. It draws at the
// pixels the game gave it, and those are the pixels its hit test reads.
//
// ui_panel=1 draws a panel batch into the panel in eye pass 0, and not at all
// in eye pass 1. ui_panel=2, the test mode, draws it into the panel and then
// into the eye. Outside the eye passes only the test draws a panel. A panel
// for screens with no scene is not built (docs/ui-layer.md).
enum PanelScopeKind { kScopeMenu, kScopeHelp, kScopeBox, kScopeKinds };
// For the census only: HelpSystem::Draw3D, which draws in the walk and stays
// in the eyes.
const int kScopeDraw3D = kScopeKinds;
const char* const kScopeName[kScopeKinds + 1] = {"menu", "help",
                                                 "subtitle box", "Draw3D"};
const int kEyeHint = 0;
const int kEyeTooltip = 1;
const int kEyeOther = 2;

int g_panel_mode = kUiPanelOff;  // latched with the frame
bool g_panel_frame = false;      // the eye passes of this frame route
bool g_panel_outside = false;    // a FinishFrame outside the passes routes
bool g_panel_check = false;      // this frame runs the composite check
bool g_panel_failed = false;     // a bind failed in this frame
bool g_panel_drew[2] = {false, false};  // kPanelMenu, kPanelHelp

// The census for the log. Pass 0 and outside the passes only, so each batch
// counts once a frame.
uint64_t g_census_panel[kScopeKinds] = {0, 0, 0};
uint64_t g_census_hint = 0;     // kept in the eyes: the hint beside the Hand
uint64_t g_census_tooltip = 0;  // kept in the eyes: a tooltip
uint64_t g_census_draw3d = 0;   // kept in the eyes: the rest of Draw3D
uint64_t g_census_skipped = 0;  // pass 1 draws not made
float g_bounds[kScopeKinds][4];  // x0, y0, x1, y1 before any clip
bool g_have_bounds[kScopeKinds] = {false, false, false};
uint64_t g_blend_alpha = 0;  // source alpha, inverse source alpha
uint64_t g_blend_off = 0;
uint64_t g_blend_add = 0;    // the destination blend is one
uint64_t g_blend_other = 0;
uint32_t g_blend_other_pair[2] = {0, 0};
uint64_t g_state_z = 0;
uint64_t g_state_z_write = 0;
uint64_t g_state_stencil = 0;
uint64_t g_state_alpha_test = 0;

// Which draws went where, by caller.
struct RouteStat {
  uint32_t caller;
  int scope;
  uint64_t panel;
  uint64_t eye[3];  // kEyeHint, kEyeTooltip, kEyeOther
};
const int kMaxRoutes = 48;
RouteStat g_routes[kMaxRoutes];
int g_route_count = 0;

bool PanelHere() {
  if (g_panel_mode == kUiPanelOff || g_panel_failed) return false;
  return g_eye >= 0 ? g_panel_frame : g_panel_outside;
}

// The panel scope of this batch, or -1.
int PanelScope() {
  if (g_menu_depth > 0) return kScopeMenu;
  if (g_caller >= kSubtitleBegin && g_caller < kSubtitleEnd) return kScopeBox;
  if (g_helptext_depth > 0 && g_draw3d_depth == 0) return kScopeHelp;
  return -1;
}

RouteStat* Route(int scope) {
  for (int i = 0; i < g_route_count; ++i) {
    if (g_routes[i].caller == g_caller && g_routes[i].scope == scope) {
      return &g_routes[i];
    }
  }
  if (g_route_count >= kMaxRoutes) return nullptr;
  RouteStat* fresh = &g_routes[g_route_count++];
  fresh->caller = g_caller;
  fresh->scope = scope;
  fresh->panel = 0;
  fresh->eye[0] = fresh->eye[1] = fresh->eye[2] = 0;
  return fresh;
}

// An eye batch inside a panel scope or HelpSystem::Draw3D, for the census.
void NoteEyeBatch(int why) {
  if (!PanelHere() || g_eye == 1) return;
  int scope = PanelScope();
  if (scope < 0 && g_draw3d_depth > 0) scope = kScopeDraw3D;
  if (scope < 0) return;
  if (scope == kScopeDraw3D) {
    ++g_census_draw3d;
  } else if (why == kEyeTooltip) {
    ++g_census_tooltip;
  } else {
    ++g_census_hint;
  }
  RouteStat* route = Route(scope);
  if (route != nullptr) ++route->eye[why];
}

void NoteBounds(int scope, const void* verts, uint32_t fvf, uint32_t count) {
  if (verts == nullptr || fvf != kExpectedFvf || count == 0) return;
  float* b = g_bounds[scope];
  if (!g_have_bounds[scope]) {
    b[0] = b[1] = 1e30f;
    b[2] = b[3] = -1e30f;
    g_have_bounds[scope] = true;
  }
  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i) {
    const float* f = reinterpret_cast<const float*>(vertex);
    if (f[0] < b[0]) b[0] = f[0];
    if (f[1] < b[1]) b[1] = f[1];
    if (f[0] > b[2]) b[2] = f[0];
    if (f[1] > b[3]) b[3] = f[1];
    vertex += kVertexStride;
  }
}

// The blend and the tests of the batch just drawn (docs/ui-layer.md, "A clear
// background and the alpha").
void NoteStates() {
  UiPanelStates s;
  GpuBridgePanelStates(&s);
  if (s.blend_on == 0) {
    ++g_blend_off;
  } else if (s.src == static_cast<uint32_t>(D3DBLEND_SRCALPHA) &&
             s.dest == static_cast<uint32_t>(D3DBLEND_INVSRCALPHA)) {
    ++g_blend_alpha;
  } else if (s.dest == static_cast<uint32_t>(D3DBLEND_ONE)) {
    ++g_blend_add;
  } else {
    if (g_blend_other == 0) {
      g_blend_other_pair[0] = s.src;
      g_blend_other_pair[1] = s.dest;
    }
    ++g_blend_other;
  }
  if (s.z_on != 0) ++g_state_z;
  if (s.z_write != 0) ++g_state_z_write;
  if (s.stencil_on != 0) ++g_state_stencil;
  if (s.alpha_test != 0) ++g_state_alpha_test;
}

// Draws one panel batch. Returns true when the eye must not draw it.
bool PanelTake(int scope, DrawAndClipFn original, D3DPRIMITIVETYPE type,
               uint32_t fvf, void* verts, uint32_t count, uint16_t* idx,
               uint32_t n) {
  if (!PanelHere()) return false;
  // The subtitle card takes the help text only. The menus stay in the eyes.
  if (g_panel_mode == kUiPanelSubtitles && scope == kScopeMenu) return false;
  if (g_eye == 1) {
    if (g_panel_mode == kUiPanelTest) return false;
    ++g_census_skipped;
    return true;
  }
  NoteBounds(scope, verts, fvf, count);
  if (!GpuBridgePanelBind(0)) {
    // Rule 8, fail safe: the rest of this frame draws into the eyes, both
    // passes, and no panel goes out.
    g_panel_failed = true;
    return false;
  }
  g_panel_bound = true;
  original(type, fvf, verts, count, idx, n);
  NoteStates();
  // The composite check draws the same batch again, the game's way, over a
  // copy of the eye. DrawAndClip2D clamps the vertices in place, and a
  // second clamp of clamped vertices changes nothing.
  if (g_panel_check && GpuBridgePanelBind(1)) {
    original(type, fvf, verts, count, idx, n);
  }
  ++g_census_panel[scope];
  RouteStat* route = Route(scope);
  if (route != nullptr) ++route->panel;
  g_panel_drew[scope == kScopeMenu ? kPanelMenu : kPanelHelp] = true;
  // The bounds of the help text of this frame, for the rect of the subtitle
  // card.
  if (scope != kScopeMenu && verts != nullptr && fvf == kExpectedFvf) {
    if (!g_have_card_bounds) {
      g_card_bounds[0] = g_card_bounds[1] = 1e30f;
      g_card_bounds[2] = g_card_bounds[3] = -1e30f;
      g_have_card_bounds = true;
    }
    const uint8_t* vertex = static_cast<const uint8_t*>(verts);
    for (uint32_t i = 0; i < count; ++i) {
      const float* f = reinterpret_cast<const float*>(vertex);
      if (f[0] < g_card_bounds[0]) g_card_bounds[0] = f[0];
      if (f[1] < g_card_bounds[1]) g_card_bounds[1] = f[1];
      if (f[0] > g_card_bounds[2]) g_card_bounds[2] = f[0];
      if (f[1] > g_card_bounds[3]) g_card_bounds[3] = f[1];
      vertex += kVertexStride;
    }
  }
  if (g_panel_mode == kUiPanelTest) {
    PanelRelease();
    return false;
  }
  return true;
}

// THE FALLBACK OF THE SUBTITLE CARD. The shift in this eye pass for a batch
// of the help text (the HelpText scope and the subtitle box), or 0 for the
// interface depth. The interface depth follows the nearest world, so it
// changes from 8 to 24 pixels while a subtitle shows. This shift does not
// change: it puts the help text at g_help_depth_m.
float HelpShift(int scope) {
  if (!(g_help_depth_m > 0.0f) || g_eye < 0) return 0.0f;
  if (scope != kScopeHelp && scope != kScopeBox) return 0.0f;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info)) || !(info->half_res_x > 0.0f) ||
      !(info->inv_half_tan_fov_x > 0.0f)) {
    return 0.0f;
  }
  const float focal = info->half_res_x * info->inv_half_tan_fov_x;
  const float shift = subtitle_math::ShiftOfDepth(focal, 0.064f, g_help_depth_m);
  return g_eye == 0 ? shift : -shift;
}

// The corners of a flat batch of the Lionhead logo in stereo, as they came.
float g_saved_intro_x[kMaxShiftVertices];
float g_saved_intro_y[kMaxShiftVertices];

// Both clip hooks run the same chain. Each step saves what it changes and is
// undone in reverse order after the draw, because the engine may use the
// vertices again.
void DrawInterfaceEye(DrawAndClipFn original, D3DPRIMITIVETYPE type,
                      uint32_t fvf, void* verts, uint32_t count, uint16_t* idx,
                      uint32_t n) {
  DumpBatch(verts, fvf, count);
  // A VIDEO OF THE GAME IN THE CUT SCENE WINDOW (cutscene_video,
  // docs/cutscene-window.md, section 7). Each eye moves the tiles of the
  // video half of the crop toward the nose, so the video is on the screen
  // plane. g_video_move is the move of the left eye.
  if (g_video) {
    PanelRelease();
    const float move = g_eye == 0 ? g_video_move : -g_video_move;
    const bool moved = VideoIn(verts, fvf, count, move);
    original(type, fvf, verts, count, idx, n);
    if (moved) VideoOut(verts, count);
    return;
  }
  // THE LIONHEAD LOGO IN STEREO (intro_vr.cpp, docs/intro.md). The effect
  // draws a text and boxes flat, after its orbs. Each corner gets the pixel
  // of its place on the plane of the logo for the camera of this eye pass,
  // so the batch has the depth of the logo and stays beside it. A batch
  // with a corner that is not in front of the camera is not drawn.
  if (verts != nullptr && fvf == kExpectedFvf && count > 0 &&
      count <= kMaxShiftVertices && IsUiCaller(g_caller) &&
      IntroFlatOn(g_caller)) {
    bool in_front = true;
    uint8_t* vertex = static_cast<uint8_t*>(verts);
    for (uint32_t i = 0; i < count; ++i, vertex += kVertexStride) {
      float* p = reinterpret_cast<float*>(vertex);
      g_saved_intro_x[i] = p[0];
      g_saved_intro_y[i] = p[1];
      if (in_front && !IntroFlatVertex(&p[0], &p[1])) in_front = false;
    }
    if (in_front) original(type, fvf, verts, count, idx, n);
    vertex = static_cast<uint8_t*>(verts);
    for (uint32_t i = 0; i < count; ++i, vertex += kVertexStride) {
      float* p = reinterpret_cast<float*>(vertex);
      p[0] = g_saved_intro_x[i];
      p[1] = g_saved_intro_y[i];
    }
    IntroFlatDone(in_front);
    return;
  }
  // A SPEECH BUBBLE AS ONE CARD (bubble_card.cpp, docs/ui-layer.md, "A
  // speech bubble as one card"). The flat batches of a bubble (its text, its
  // arrows, its icon) have the pixels of its box. The box has the depth of
  // its sign, so they get none of the steps below and stay on it. In one walk
  // the copy for the right eye moves with the card, by the eye difference of
  // the sign (one_walk_marks=1). The name of a villager is such a card at the
  // depth of the villager (name_card=1).
  if (BubbleCardInBubble() && BubbleCardOn()) {
    PanelRelease();
    float move = 0.0f;
    const bool moved = g_replay_right && BubbleCardWalkMove(&move) &&
                       MoveIn(verts, fvf, count, move);
    BubbleCardFlat(verts, fvf, count, g_replay_right);
    original(type, fvf, verts, count, idx, n);
    if (moved) ShiftOut(verts, count);
    return;
  }
  // A menu gets only the interface depth. See h_SetupFinish. The flags are
  // cleared here because IsDuplicateWidget does not run, and the last batch
  // may have set them.
  if (g_menu_depth > 0) {
    g_in_widget_band = false;
    g_widget_scale = false;
    g_widget_flat = false;
    g_widget_hint = false;
    ++g_menu_batches;
    if (PanelTake(kScopeMenu, original, type, fvf, verts, count, idx, n)) {
      return;
    }
    PanelRelease();
    const bool shifted = ShiftIn(verts, fvf, count);
    original(type, fvf, verts, count, idx, n);
    if (shifted) ShiftOut(verts, count);
    return;
  }
  // A tooltip is one unit, moved as a whole. See h_DrawKeyOrMouse.
  if (g_in_tooltip) {
    NoteEyeBatch(kEyeTooltip);
    PanelRelease();
    const bool moved = TooltipIn(verts, fvf, count);
    original(type, fvf, verts, count, idx, n);
    if (moved) TooltipOut(verts, count);
    return;
  }
  if (IsDuplicateWidget(verts, fvf, count)) return;
  const int scope = PanelScope();
  if (scope >= 0 && PanelHere()) {
    if (g_widget_scale) {
      NoteEyeBatch(kEyeHint);
    } else {
      // The chain below records the subtitle band that the hint rule reads.
      // A panel batch skips the chain, so it records the band here.
      if (SubtitleLiftIn(verts, fvf, count)) SubtitleLiftOut(verts, count);
      if (PanelTake(scope, original, type, fvf, verts, count, idx, n)) return;
    }
  }
  if (scope < 0 && g_draw3d_depth > 0) NoteEyeBatch(kEyeOther);
  PanelRelease();
  const bool lifted = SubtitleLiftIn(verts, fvf, count);
  const bool sized = UiScaleIn(verts, fvf, count);
  const bool placed = SubtitlePlaceIn(verts, fvf, count, sized);
  const bool scaled = ScaleIn(verts, fvf, count);
  const bool shifted = ShiftIn(verts, fvf, count, HelpShift(scope));
  const bool flattened = FlattenIn(verts, fvf, count);
  // With bubble_card=0 a flat batch of a bubble comes here. Its pixels go to
  // the log of the bubble.
  if (BubbleCardInBubble()) {
    BubbleCardFlat(verts, fvf, count, g_replay_right);
  }
  original(type, fvf, verts, count, idx, n);
  if (flattened) FlattenOut(verts, count);
  if (shifted) ShiftOut(verts, count);
  if (scaled) ScaleOut(verts, count);
  if (placed) SubtitlePlaceOut(verts, count);
  if (sized) UiScaleOut(verts, count);
  if (lifted) SubtitleLiftOut(verts, count);
}

// In one walk a batch of the interface goes to the left half with the chain
// of the left eye, and then to the right half with the chain of the right eye.
// A batch of another caller is world geometry that the engine clipped.
// one_walk.cpp gives it to the right eye with the disparity of its rhw. A
// batch of a speech bubble as a card is a batch of the interface for each
// caller: the card has the place of its sign, and no batch of it has an rhw
// of that place (one_walk_marks=1).
void DrawInterface(DrawAndClipFn original, D3DPRIMITIVETYPE type,
                   uint32_t fvf, void* verts, uint32_t count, uint16_t* idx,
                   uint32_t n) {
  const bool card =
      BubbleCardInBubble() && BubbleCardOn() && OneWalkMarks();
  // A batch of the video on the screen plane goes to each eye as a batch of
  // the interface does. The move is for the left eye.
  float move = 0.0f;
  g_video = IsVideoCaller(g_caller) &&
            CutsceneWindowVideoBatch(g_eye < 0 ? -1 : 0, &move);
  g_video_move = move;
  if (!OneWalkActive() || g_eye != 0 ||
      (!IsUiCaller(g_caller) && !card && !g_video)) {
    DrawInterfaceEye(original, type, fvf, verts, count, idx, n);
    g_video = false;
    return;
  }
  OneWalkSetMode(kOneWalkLeftOnly);
  DrawInterfaceEye(original, type, fvf, verts, count, idx, n);
  const float shift = g_shift;
  TooltipState left_tooltip;
  if (g_in_tooltip) {
    SaveTooltip(&left_tooltip);
    LoadTooltip(g_tooltip_right);
  }
  g_eye = 1;
  g_shift = -shift;
  g_replay_right = true;
  OneWalkSetMode(kOneWalkRightOnly);
  DrawInterfaceEye(original, type, fvf, verts, count, idx, n);
  OneWalkSetMode(kOneWalkBoth);
  g_replay_right = false;
  g_shift = shift;
  g_eye = 0;
  g_video = false;
  if (g_in_tooltip) {
    SaveTooltip(&g_tooltip_right);
    LoadTooltip(left_tooltip);
  }
}

// THE ADVISORS IN THE CUT SCENE WINDOW (cutscene_advisors=1). A batch of an
// advisor gets the eye difference of its place in front of the screen
// (window_math::AdvisorX). In two walks each eye pass moves its own batch.
// In one walk the batch goes to the left half only, and then to the right
// half only, made from the vertices of the left eye. The engine changes the
// vertices that it gets, so the draw for the right eye starts from a copy,
// and the x of each vertex goes back after the last draw.
const uint32_t kAdvisorStride = 32;
const uint32_t kAdvisorMost = 65536;
uint8_t g_advisor_copy[kAdvisorMost * kAdvisorStride];

void MoveAdvisor(void* verts, uint32_t count,
                 const window_math::AdvisorShift& shift, int eye,
                 bool one_walk) {
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kAdvisorStride) {
    float* p = reinterpret_cast<float*>(vertex);
    if (p[3] > 0.0f) {
      p[0] = window_math::AdvisorX(shift, eye, p[0], p[3], one_walk);
    }
  }
}

template <typename Draw>
void DrawAdvisor(void* verts, uint32_t count,
                 const window_math::AdvisorShift& shift, Draw draw) {
  if (verts == nullptr || count == 0 || count > kAdvisorMost) {
    draw();
    return;
  }
  const size_t bytes = static_cast<size_t>(count) * kAdvisorStride;
  memcpy(g_advisor_copy, verts, bytes);
  const bool walk = OneWalkActive() && g_eye == 0;
  MoveAdvisor(verts, count, shift, g_eye == 1 ? 1 : 0, walk);
  if (walk) OneWalkSetMode(kOneWalkLeftOnly);
  draw();
  if (walk) {
    memcpy(verts, g_advisor_copy, bytes);
    MoveAdvisor(verts, count, shift, 1, true);
    OneWalkSetMode(kOneWalkRightOnly);
    draw();
    OneWalkSetMode(kOneWalkBoth);
  }
  uint8_t* vertex = static_cast<uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kAdvisorStride) {
    reinterpret_cast<float*>(vertex)[0] = reinterpret_cast<const float*>(
        g_advisor_copy + static_cast<size_t>(i) * kAdvisorStride)[0];
  }
}

void __cdecl h_DrawAndClip(D3DPRIMITIVETYPE type, uint32_t fvf, void* verts,
                           uint32_t count, uint16_t* idx, uint32_t n) {
  const uint32_t outer = g_caller;
  g_caller = reinterpret_cast<uint32_t>(_ReturnAddress());
  // A mesh that the engine clips comes this way. The mesh of a target of the
  // Hand gets its glow here too (target_glow.cpp).
  if (fvf == kExpectedFvf && type == D3DPT_TRIANGLELIST &&
      !IsUiCaller(g_caller)) {
    TargetGlowBatch(verts, count, idx, n, true, g_caller);
  }
  // A mesh of an advisor that the engine clips.
  window_math::AdvisorShift advisor = {};
  if (fvf == kExpectedFvf && type == D3DPT_TRIANGLELIST && g_eye >= 0 &&
      !IsUiCaller(g_caller) &&
      CutsceneWindowAdvisorBatch(g_eye, verts, count, idx, n, &advisor)) {
    DrawAdvisor(verts, count, advisor, [&]() {
      DrawInterface(o_DrawAndClip, type, fvf, verts, count, idx, n);
    });
  } else {
    DrawInterface(o_DrawAndClip, type, fvf, verts, count, idx, n);
  }
  g_caller = outer;
}

void __cdecl h_DrawAndClip2D(D3DPRIMITIVETYPE type, uint32_t fvf, void* verts,
                             uint32_t count, uint16_t* idx, uint32_t n) {
  const uint32_t outer = g_caller;
  g_caller = reinterpret_cast<uint32_t>(_ReturnAddress());
  DrawInterface(o_DrawAndClip2D, type, fvf, verts, count, idx, n);
  g_caller = outer;
}

// DrawTriangle carries the world, so it is never shifted. It is hooked for two
// cheap readings: the caller, when something is measuring, and how near the
// closest world geometry is, which the interface depth needs.
//
// Only the first vertex of each batch is read. That is one float for about a
// hundred and thirty batches a frame, and over a whole frame the largest of
// those first vertices tracks the nearest geometry closely enough to aim at.
void __fastcall h_DrawTriangle(void* verts, uint32_t count, uint16_t* idx,
                               uint32_t n) {
  // World geometry never goes to the interface panel.
  PanelRelease();
  // The mesh of a target of the Hand gets its glow (target_glow.cpp).
  TargetGlowBatch(verts, count, idx, n, false,
                  reinterpret_cast<uint32_t>(_ReturnAddress()));
  // A batch of an advisor with its place in front of the screen is not in
  // the measure of the nearest thing.
  window_math::AdvisorShift advisor_shift = {};
  const bool advisor =
      g_eye >= 0 &&
      CutsceneWindowAdvisorBatch(g_eye, verts, count, idx, n, &advisor_shift);
  if (g_window_probe && !advisor) {
    CutsceneWindowProbeBatch(verts, count, idx, n,
                             reinterpret_cast<uint32_t>(_ReturnAddress()));
  }
  const float near_plane =
      *reinterpret_cast<const float*>(bw::tech::kNearPlane);
  if (verts != nullptr && count > 0 && near_plane > 0.0f) {
    const float rhw =
        reinterpret_cast<const float*>(verts)[3] * (kHistogramNear / near_plane);
    if (rhw > 0.0f && rhw <= 1.0f) {
      int bucket = static_cast<int>(rhw * kRhwBuckets);
      if (bucket >= kRhwBuckets) bucket = kRhwBuckets - 1;
      ++g_rhw_histogram[bucket];
      ++g_rhw_samples;
    }
  }
  const uint32_t outer = g_caller;
  g_caller = reinterpret_cast<uint32_t>(_ReturnAddress());
  // The box of a speech bubble is the one batch of the interface that comes
  // this way: its corners are points of the world. Each corner goes to its
  // pixel (bubble_card.cpp).
  const bool box = BubbleCardInBubble() &&
                   BubbleCardBoxIn(verts, count, g_caller);
  // ONE WALK (one_walk_marks=1): the box for the left eye only, and after it
  // the box for the right eye only, moved by the eye difference of the sign.
  // The rhw of a corner is the depth of the bubble, not the depth of the
  // sign, so the copy that one walk makes of a batch is not that box.
  float box_move = 0.0f;
  const bool box_eyes = box && BubbleCardWalkMove(&box_move);
  if (box_eyes) OneWalkSetMode(kOneWalkLeftOnly);
  if (advisor && !box_eyes) {
    DrawAdvisor(verts, count, advisor_shift,
                [&]() { o_DrawTriangle(verts, count, idx, n); });
  } else if (g_time_submit) {
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    o_DrawTriangle(verts, count, idx, n);
    QueryPerformanceCounter(&b);
    g_submit_ticks += b.QuadPart - a.QuadPart;
    ++g_submit_calls;
  } else {
    o_DrawTriangle(verts, count, idx, n);
  }
  if (box_eyes) {
    BubbleCardBoxRight(verts, count, box_move);
    OneWalkSetMode(kOneWalkRightOnly);
    o_DrawTriangle(verts, count, idx, n);
    OneWalkSetMode(kOneWalkBoth);
  }
  if (box) BubbleCardBoxOut(verts, count);
  g_caller = outer;
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

}  // namespace

// The address inside the subsystem that asked for the draw now in progress.
uint32_t DrawHooksCaller() { return g_caller; }

// Called before each eye pass. eye is 0 for left, 1 for right, and -1 to turn
// the shift off for anything drawn outside the stereo passes.
// Asks for the next frame's interface batches to be written out.
void UiDepthDumpNextFrame() {
  g_dump = true;
  g_dumped = 0;
}

void UiDepthSetEye(int eye, float shift_pixels) {
  // The list of quads is per pass. The bands live for a whole frame, so they
  // only roll over when both passes are done.
  EndWidgetPass();
  g_tooltip_count = 0;
  if (eye < 0) RollBands();
  // The dump covers one frame. Turning it off when the passes end keeps it to
  // that frame rather than to a fixed number of batches.
  if (eye < 0) g_dump = false;
  g_eye = eye;
  g_window_probe = eye >= 0 && CutsceneWindowProbeStart(eye);
  // The left eye latches what the controller hints show for the frame.
  ControllerHintsPass(eye);
  // And whether the hints go to the hint card. After both passes the card of
  // the frame goes to the headset.
  if (eye == 0) g_card_route = HintCardRoute();
  if (eye < 0) {
    HintCardEndFrame();
    g_card_route = false;
    // The subtitle card of the frame. Both eye captures are done, so the
    // GPU has no work left, and the copy of the panel does not wait.
    PanelRelease();
    UiPanelEndPasses();
  }
  if (eye < 0) {
    g_shift = 0.0f;
  } else {
    g_shift = (eye == 0) ? shift_pixels : -shift_pixels;
  }
}

bool UiDepthInstall(bool with_attribution) {
  bool ok = true;
  ok &= Hook(bw::render::kDrawAndClip, reinterpret_cast<void*>(&h_DrawAndClip),
             reinterpret_cast<void**>(&o_DrawAndClip), "DrawAndClip");
  ok &= Hook(bw::render::kDrawAndClip2D,
             reinterpret_cast<void*>(&h_DrawAndClip2D),
             reinterpret_cast<void**>(&o_DrawAndClip2D), "DrawAndClip2D");
  // with_attribution does not decide this. The hook also samples how near the
  // world is, which the adaptive interface depth needs, so it goes in either
  // way.
  (void)with_attribution;
  ok &= Hook(bw::render::kDrawTriangle,
             reinterpret_cast<void*>(&h_DrawTriangle),
             reinterpret_cast<void**>(&o_DrawTriangle), "DrawTriangle");
  ok &= Hook(kDrawKeyOrMouse, reinterpret_cast<void*>(&h_DrawKeyOrMouse),
             reinterpret_cast<void**>(&o_DrawKeyOrMouse),
             "CameraHelp::DrawKeyOrMouse");
  ok &= Hook(kHelpTextFinish, reinterpret_cast<void*>(&h_HelpTextFinish),
             reinterpret_cast<void**>(&o_HelpTextFinish),
             "HelpText FinishFrame callback");
  ok &= Hook(kHelpDraw3D, reinterpret_cast<void*>(&h_HelpDraw3D),
             reinterpret_cast<void**>(&o_HelpDraw3D), "HelpSystem::Draw3D");
  ok &= Hook(kSetupFinish, reinterpret_cast<void*>(&h_SetupFinish),
             reinterpret_cast<void**>(&o_SetupFinish),
             "Setup FinishFrame callback, the menus");
  ok &= Hook(kHelpBarHeight, reinterpret_cast<void*>(&h_HelpBarHeight),
             reinterpret_cast<void**>(&o_HelpBarHeight),
             "HelpText cinema bar height");
  return ok;
}

// THE INTERFACE PANEL: the route of a frame, latched before its first draw.
// See the note above PanelTake.
bool UiPanelInstallHooks() {
  return Hook(kRectQueueFinish, reinterpret_cast<void*>(&h_RectQueueFinish),
              reinterpret_cast<void**>(&o_RectQueueFinish),
              "the rect queue callback, the subtitle box");
}

void UiPanelRouteFrame(int mode, bool on, bool check) {
  PanelRelease();
  g_panel_mode = mode;
  g_panel_frame = on && mode != kUiPanelOff;
  g_panel_outside = false;
  g_panel_check = check && g_panel_frame;
  g_panel_failed = false;
  g_panel_drew[0] = g_panel_drew[1] = false;
  g_card_takes_help = g_panel_frame && mode == kUiPanelSubtitles;
  g_have_card_bounds = false;
}

// The bounds of the help text that went to the panel target in this frame
// (x0, y0, x1, y1 in pixels of the render), and the size of the render.
// False when none drew.
bool UiPanelHelpBounds(float bounds[4], uint32_t* render_w,
                       uint32_t* render_h) {
  if (!g_have_card_bounds || g_panel_failed) return false;
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(bw::tech::kInfoTransform);
  if (IsBadReadPtr(info, sizeof(*info)) || info->res_x <= 0 ||
      info->res_y <= 0) {
    return false;
  }
  for (int i = 0; i < 4; ++i) bounds[i] = g_card_bounds[i];
  *render_w = static_cast<uint32_t>(info->res_x);
  *render_h = static_cast<uint32_t>(info->res_y);
  return true;
}

// The fallback of the subtitle card: the help text draws in the eye pictures
// at a fixed depth in meters. 0 gives it the interface depth.
void UiDepthSetHelpDepth(float meters) { g_help_depth_m = meters; }

uint64_t UiDepthTakeHelpFixed() {
  const uint64_t n = g_help_fixed;
  g_help_fixed = 0;
  return n;
}

void UiDepthSetSubtitleSelfTest(bool on) {
  g_subtitle_selftest = on;
  if (on) {
    ModLog("ui depth     : subtitle_selftest=1, two test lines of help text "
           "draw in each eye pass");
  }
}

void UiPanelRouteOutside(int mode, bool on, bool check) {
  PanelRelease();
  g_card_takes_help = false;
  g_have_card_bounds = false;
  g_panel_mode = mode;
  g_panel_frame = false;
  g_panel_outside = on && mode != kUiPanelOff;
  g_panel_check = check && g_panel_outside;
  g_panel_failed = false;
  g_panel_drew[0] = g_panel_drew[1] = false;
}

bool UiPanelDrew(int kind) {
  return g_panel_drew[kind == kPanelMenu ? kPanelMenu : kPanelHelp];
}

bool UiPanelFrameFailed() { return g_panel_failed; }

// Called after LH3DRender::FinishFrame. The scope hooks have put the game's
// target back already, so this only counts a miss.
void UiPanelRelease() {
  if (!g_panel_bound) return;
  ++g_panel_left_bound;
  PanelRelease();
}

void UiPanelReportRoutes() {
  const uint64_t panel =
      g_census_panel[kScopeMenu] + g_census_panel[kScopeHelp] +
      g_census_panel[kScopeBox];
  if (panel == 0 && g_census_hint == 0 && g_census_tooltip == 0 &&
      g_census_draw3d == 0 && g_panel_left_bound == 0) {
    return;
  }
  ModLog("ui panel     : %llu panel batches (%llu menu, %llu help, %llu "
         "subtitle box), %llu kept in the eyes (%llu hint, %llu tooltip), "
         "%llu not drawn in eye pass 1",
         panel, g_census_panel[kScopeMenu], g_census_panel[kScopeHelp],
         g_census_panel[kScopeBox], g_census_hint + g_census_tooltip,
         g_census_hint, g_census_tooltip, g_census_skipped);
  if (g_census_draw3d > 0) {
    ModLog("ui panel     : %llu more batches of HelpSystem::Draw3D, in the "
           "walk, stay in the eyes", g_census_draw3d);
  }
  if (panel > 0) {
    ModLog("ui panel     : blend modes: %llu source alpha, %llu off, %llu "
           "additive, %llu other", g_blend_alpha, g_blend_off, g_blend_add,
           g_blend_other);
    if (g_blend_other > 0) {
      ModLog("ui panel     : the first other blend is source %u, destination "
             "%u", g_blend_other_pair[0], g_blend_other_pair[1]);
    }
    ModLog("ui panel     : depth test on %llu, depth write on %llu, stencil on "
           "%llu, alpha test on %llu", g_state_z, g_state_z_write,
           g_state_stencil, g_state_alpha_test);
  }
  for (int k = 0; k < kScopeKinds; ++k) {
    if (!g_have_bounds[k]) continue;
    const float* b = g_bounds[k];
    ModLog("ui panel     : %s rect %.0f %.0f %.0f %.0f (x y w h, render "
           "pixels, before the clip)", kScopeName[k], b[0], b[1], b[2] - b[0],
           b[3] - b[1]);
    g_have_bounds[k] = false;
  }
  if (g_panel_left_bound > 0) {
    ModLog("ui panel     : %llu times the panel was still bound after "
           "FinishFrame", g_panel_left_bound);
  }
  // Which draws went where, in the test mode.
  if (g_panel_mode == kUiPanelTest) {
    for (int i = 0; i < g_route_count; ++i) {
      const RouteStat& r = g_routes[i];
      ModLog("ui panel     :   caller 0x%08X %-12s %7llu to the panel, in the "
             "eyes %6llu hint, %6llu tooltip, %6llu other", r.caller,
             kScopeName[r.scope], r.panel, r.eye[kEyeHint],
             r.eye[kEyeTooltip], r.eye[kEyeOther]);
    }
  }
  g_route_count = 0;
  for (int k = 0; k < kScopeKinds; ++k) g_census_panel[k] = 0;
  g_census_hint = g_census_tooltip = g_census_skipped = 0;
  g_census_draw3d = 0;
  g_blend_alpha = g_blend_off = g_blend_add = g_blend_other = 0;
  g_state_z = g_state_z_write = g_state_stencil = g_state_alpha_test = 0;
  g_panel_left_bound = 0;
}

void UiDepthTimeSubmit(bool on) {
  g_time_submit = on;
  if (on && g_submit_frequency.QuadPart == 0) {
    QueryPerformanceFrequency(&g_submit_frequency);
  }
}

// Milliseconds per frame spent inside DrawTriangle, and the call count.
void UiDepthTakeSubmit(double* ms, uint64_t* calls, uint64_t frames) {
  *ms = 0.0;
  *calls = frames > 0 ? g_submit_calls / frames : g_submit_calls;
  if (g_submit_frequency.QuadPart != 0 && frames > 0) {
    *ms = (static_cast<double>(g_submit_ticks) /
           static_cast<double>(g_submit_frequency.QuadPart)) *
          1000.0 / static_cast<double>(frames);
  }
  g_submit_ticks = 0;
  g_submit_calls = 0;
}

// The nearest world geometry drawn in the last finished frame, as 1 / camera_z,
// without the nearest two percent. 0 when there was none. Those few batches
// will still poke through the interface, and that is the right trade: chasing
// them would drag the whole interface to the eyes for the sake of a handful
// of triangles.
float UiDepthWorldInverseZ() { return g_world_inverse_z; }

// Called once per frame, before the passes, to start a fresh reading.
void UiDepthNewFrame() {
  ++g_ui_frame;
  UpdatePixelScale();
  if (g_rhw_samples > 0) {
    const uint32_t target = (g_rhw_samples * 98) / 100;
    uint32_t running = 0;
    int bucket = 0;
    for (; bucket < kRhwBuckets; ++bucket) {
      running += g_rhw_histogram[bucket];
      if (running >= target) break;
    }
    if (bucket >= kRhwBuckets) bucket = kRhwBuckets - 1;
    // The top of the bucket, so the answer is never an underestimate.
    g_world_inverse_z =
        static_cast<float>(bucket + 1) / (kRhwBuckets * kHistogramNear);
  } else {
    g_world_inverse_z = 0.0f;
  }
  for (int i = 0; i < kRhwBuckets; ++i) g_rhw_histogram[i] = 0;
  g_rhw_samples = 0;
  // Where the hints landed belongs to one frame. Carrying it into the next
  // would pin a hint to where it was rather than to where the Hand is.
  g_flat_count = 0;
  g_tooltip_flat_count = 0;
  g_tooltip_hand_count = 0;
  // The subtitle band is kept one frame, because the letters can be drawn
  // before the bar that defines the band.
  g_have_prev_sub = g_have_sub;
  g_prev_sub_top = g_sub_top;
  g_prev_sub_bottom = g_sub_bottom;
  g_have_sub = false;
}

void UiDepthSetAccounting(bool on) { g_accounting = on; }

const char* HintModeName(int mode) {
  if (mode == kHintLeaveAlone) return "left alone, so it doubles in stereo";
  if (mode == kHintKeepProjected) {
    return g_hint_flat ? "the copy at the Hand, drawn flat"
                       : "the copy at the Hand, with the Hand's depth";
  }
  return "the copy at the pointer, which rides the head";
}

void UiDepthSetHintMode(int mode) {
  if (mode < kHintLeaveAlone || mode > kHintKeepPinned) return;
  g_hint_mode = mode;
  // A mode change must not inherit what the last mode learned, because the
  // two rules drop opposite halves of the pair.
  g_paired_count = 0;
  g_help_quad_count = 0;
  g_flat_count = 0;
  ModLog("ui depth     : hint mode %d, %s", mode, HintModeName(mode));
}

int UiDepthHintMode() { return g_hint_mode; }
void UiDepthSetHintSelfTest(bool on) { g_hint_selftest = on; }

void UiDepthSetHintScale(float scale) {
  if (scale < 0.15f) scale = 0.15f;
  if (scale > 2.0f) scale = 2.0f;
  g_hint_scale = scale;
  ModLog("ui depth     : the hand hint is drawn at %.0f%% of its own size",
         scale * 100.0f);
}

float UiDepthHintScale() { return g_hint_scale; }

// A hint drawn at the Hand carries the Hand's own disparity, which puts text a
// hand's breadth from the player's face. Flattening keeps it on the Hand and
// takes the depth out, which is what reading it comfortably needs.
void UiDepthSetHintFlat(bool on) {
  g_hint_flat = on;
  g_flat_count = 0;
  ModLog("ui depth     : the hand hint is drawn %s",
         on ? "flat, at the Hand" : "at the Hand's own depth");
}

bool UiDepthHintFlat() { return g_hint_flat; }

// How big the help layer is drawn, as a percentage of what the game asks for.
// The range is 20 to 100.
//
// What the font cache holds, and what that means below 40:
// CachePage::RenderChar 0x00830C10 stores each glyph twice, at 40 rows and at
// 20 rows. Each copy is a 2x2 box filter of the size above it. DrawTextRaw
// 0x00832C60 uses the 20 row copy only when the requested height is below
// 26 px. The mod scales the vertices after that choice. So tooltip text asked
// at 77 px always samples the 40 row copy. That is a 1.3 to 1 minification at
// 40 percent and 2.1 to 1 at 25 percent. Without mip levels, text at 2 to 1
// can crawl when it moves. texture_filter.cpp asks for mip filtering, but it
// is not verified that the font cache pages have mip levels.
void UiDepthSetUiScale(int percent) {
  if (percent < 20) percent = 20;
  if (percent > 100) percent = 100;
  g_ui_scale = percent / 100.0f;
  ModLog("ui depth     : help, subtitles and tooltips are drawn at %d%% of their "
         "size, toward the middle of the view", percent);
}

int UiDepthUiScalePercent() {
  return static_cast<int>(g_ui_scale * 100.0f + 0.5f);
}

// Show or hide the cinema bars. Hidden is the default. See h_HelpBarHeight.
void UiDepthSetCinemaBars(bool show) {
  g_cinema_bars = show;
  ModLog("ui depth     : cinema bars %s",
         show ? "shown, as the game draws them"
              : "hidden, so the scene fills the view in cut scenes");
}

bool UiDepthCinemaBars() { return g_cinema_bars; }

// Called before LH3DRender::FinishFrame. It returns the game's value, which
// UiBarsEnd must get back. FinishFrame runs once in each eye pass. The bars
// are hidden only once the bar height hook is in, so the subtitles never sit
// above a bar that is not drawn. Before UiDepthInstall, and in a mono run that
// never installs it, the game draws its bars.
float UiBarsBegin() {
  float* shown = reinterpret_cast<float*>(kWideScreenShown);
  const float saved = *shown;
  g_bars_zeroed = false;
  // The cut scene window shows the bars: they are part of the picture that
  // the game made, and on a screen in the room they are not on the face.
  if (!g_cinema_bars && !CutsceneWindowFlat() && o_HelpBarHeight != nullptr &&
      saved != 0.0f) {
    *shown = 0.0f;
    g_bars_zeroed = true;
    // Count each frame once: the second eye pass draws the same frame.
    if (g_eye != 1) ++g_bars_hidden_frames;
  }
  return saved;
}

// Called after LH3DRender::FinishFrame. The logic and the interface playback
// read the game's value again from here on.
void UiBarsEnd(float saved) {
  if (!g_bars_zeroed) return;
  *reinterpret_cast<float*>(kWideScreenShown) = saved;
  g_bars_zeroed = false;
}

// How far toward the middle of the view the subtitles move, as a percentage of
// the way there. The bottom of a 110 degree view is 55 degrees below the
// middle, which is below comfortable reading.
void UiDepthSetSubtitleLift(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 90) percent = 90;
  g_subtitle_lift = percent / 100.0f;
  ModLog("ui depth     : subtitles move %d%% of the way to the middle of the "
         "view", percent);
}

// How far below the middle of the view the subtitles sit, in degrees. 0
// leaves them where the interface size puts them.
void UiDepthSetSubtitleDegrees(int degrees) {
  if (degrees < 0) degrees = 0;
  if (degrees > 50) degrees = 50;
  g_subtitle_degrees = static_cast<float>(degrees);
  if (degrees > 0) {
    ModLog("ui depth     : subtitles sit %d degrees below the middle of the "
           "view", degrees);
  } else {
    ModLog("ui depth     : subtitles sit where the interface size puts them");
  }
}

// The Shift F12 test key: 25, 30, 35, 40 and 45 degrees, then 25 again.
void UiDepthCycleSubtitleDegrees() {
  const int steps[] = {25, 30, 35, 40, 45};
  const int now = static_cast<int>(g_subtitle_degrees + 0.5f);
  int next = steps[0];
  for (int i = 0; i < 5; ++i) {
    if (steps[i] > now) {
      next = steps[i];
      break;
    }
  }
  g_subtitle_degrees = static_cast<float>(next);
  ModLog("test key     : subtitles sit %d degrees below the middle of the view",
         next);
}

// Names every caller that draws a different number of batches in the second
// eye pass than in the first. For a correct stereo frame that list should be
// empty: both passes draw the same scene.
void UiDepthReportPassMismatch() {
  int found = 0;
  for (int i = 0; i < g_stat_count; ++i) {
    const CallerStat& c = g_stats[i];
    const uint64_t first = c.batches_in_pass[0];
    const uint64_t second = c.batches_in_pass[1];
    if (first == 0 && second == 0) continue;
    // Ignore a difference of one batch, which is ordinary jitter when a
    // report lands in the middle of a frame.
    const uint64_t gap = first > second ? first - second : second - first;
    if (gap <= 1) continue;
    if (found == 0) {
      ModLog("pass mismatch: callers that draw a different amount per eye");
      ModLog("pass mismatch:   caller       pass 1    pass 2   ratio");
    }
    ModLog("pass mismatch:   0x%08X %9llu %9llu   %.2fx%s", c.address, first,
           second, first > 0 ? static_cast<double>(second) / first : 0.0,
           IsUiCaller(c.address) ? "  <- interface" : "");
    ++found;
  }
  if (found == 0) {
    ModLog("pass mismatch: none, both eye passes drew the same batches");
  }
}

// The tooltip dedup and the interface size, in every report window, so a
// headset session's log shows them without the accounting switched on.
void UiDepthReportHelp() {
  if (g_sub_placed > 0 || g_sub_kept_from_hint > 0) {
    ModLog("ui depth     : %llu subtitle batches placed at %.0f degrees, %llu "
           "kept out of the Hand's hint", g_sub_placed, g_subtitle_degrees,
           g_sub_kept_from_hint);
    g_sub_placed = 0;
    g_sub_kept_from_hint = 0;
  }
  if (g_tooltips_kept > 0 || g_tooltips_dropped > 0) {
    ModLog("ui depth     : %llu tooltips drawn, %llu second copies skipped, "
           "%llu at the Hand of the controller", g_tooltips_kept,
           g_tooltips_dropped, g_tooltips_at_hand);
    g_tooltips_kept = 0;
    g_tooltips_dropped = 0;
    g_tooltips_at_hand = 0;
  }
  if (g_tooltips_to_card > 0) {
    ModLog("ui depth     : %llu tooltip calls went to the hint card and did "
           "not draw in an eye picture", g_tooltips_to_card);
    g_tooltips_to_card = 0;
  }
  if (g_ui_scaled > 0) {
    ModLog("ui depth     : %llu help batches drawn at the interface size",
           g_ui_scaled);
    g_ui_scaled = 0;
  }
  if (g_menu_batches > 0) {
    ModLog("ui depth     : %llu menu batches drawn with the interface depth "
           "only", g_menu_batches);
    g_menu_batches = 0;
  }
  if (g_bars_hidden_frames > 0) {
    ModLog("ui depth     : %llu frames with the cinema bars hidden",
           g_bars_hidden_frames);
    g_bars_hidden_frames = 0;
  }
}

void UiDepthReport() {
  UiDepthReportPassMismatch();
  if (g_shifted == 0 && g_refused == 0) return;
  if (g_widgets_dropped > 0) {
    ModLog("ui depth     : dropped %llu duplicate hand widgets",
           g_widgets_dropped);
    g_widgets_dropped = 0;
  }

  ModLog("ui depth     : shifted %llu interface batches, refused %llu from "
         "other callers, %llu too large", g_shifted, g_refused, g_too_big);
  if (!g_accounting || g_stat_count == 0) return;

  ModLog("ui depth     : who submitted what");
  ModLog("ui depth     :   caller     batches  shifted        z range   worst "
         "spread");
  for (int i = 0; i < g_stat_count; ++i) {
    const CallerStat& c = g_stats[i];
    // A batch entirely far from the camera cannot be interface, and listing
    // every terrain function would bury the useful lines.
    if (c.shifted == 0 && c.z_min >= kNearDepth) continue;
    ModLog("ui depth     :   0x%08X %8llu %8llu  %6.3f..%6.3f %8.4f%s",
           c.address, c.batches, c.shifted, c.z_min, c.z_max, c.spread_max,
           IsUiCaller(c.address) ? "  <- interface" : "");
  }
}
