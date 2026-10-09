// Put the head on the camera the ENGINE builds, not on the matrices afterwards.
//
// A change of the world to camera matrix family inside the scene walk, once
// per eye, is not enough. Terrain, objects and shadows follow the head
// correctly, because they are projected through those matrices. The sea does
// not: turning the head makes the horizon swing and the water arc away. An
// invented head shows this on the desktop with no headset.
//
// The reason is where such a change happens. GGame::ProcessGraphicsEngine runs
// camera->Update() BEFORE GGame::Process3dEngine, and LH3DTech::UpdateCamera
// builds the whole camera family, the culling cone, and whatever the water
// grid is anchored to, from one position and one focus point. A poke inside
// the scene walk arrives after all of that, so anything built earlier is still
// looking the old way.
//
// So the head is applied at the source. LH3DTech::UpdateCamera is
// hooked, and the position and focus it is given are moved before it runs.
// Everything the engine derives is then derived from a camera that is already
// looking where the player is looking, and nothing downstream has to be
// corrected.
//
// LH3DTech::UpdateCamera 0x00819920 is __FASTCALL. Two pointer arguments,
// position and focus, each an LHPoint of three floats, arriving in ecx and
// edx.
//
// A wrong calling convention here crashes the game. The rule "C3 means the
// caller cleans, so __cdecl" is true and incomplete: a function whose arguments all arrive in registers also has
// nothing to clean, and also ends C3. The distinguishing evidence is at the
// top of the function, not the bottom:
//
//     +0x0C  8B F2   mov esi, edx     <- second argument
//     +0x0E  8B F9   mov edi, ecx     <- first argument
//     +0x48  8B 07   mov eax, [edi]   <- faulted with a stack-read pointer
//
// So: read the ret to learn who cleans, and read the first few instructions to
// learn where the arguments come from. Both, every time.
//
// FIVE CALLS A FRAME, NOT ONE. The engine does not call UpdateCamera once a
// frame. The log counts it: 2250 camera updates over 450 frames. One call
// places the main camera before the scene walk. The other four happen INSIDE
// the walk, which this mod runs twice, once per eye. If each of them rewrote
// the frame's anchor and the frame's leftover roll, eye 0 would read the
// roll, draw, and then the walk would change the roll before eye 1 read it.
// The two eyes of one frame would draw from different cameras, the roll of
// the right eye would climb past 90 degrees, and looking up would invert and
// spin that eye. The same state carries the Hand, so the Hand would land in
// two places in one frame.
//
// Every call still moves its camera, because the sub cameras need the head
// too. Only the FIRST call of each frame records the frame's state. See
// RecordThisCall.
//
// WHAT THIS CANNOT DO. A camera described by a position and a focus point has
// no roll: there is nowhere to put it. So this carries yaw, pitch and the
// player leaning about, and roll is left over for the per eye matrix. The
// leftover is not measured by comparing vectors, which has a singularity at
// the zenith. It is computed from the rotation this file composed, against
// the rotation the engine is about to build. See RecordThisCall.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);
// script_camera.cpp: the shot the story camera holds, in place of the pose
// the caller asked for. True when there is one.
bool ScriptCameraRenderPose(uintptr_t caller, const float* position,
                            const float* focus, float* out_position,
                            float* out_focus);
// intro_vr.cpp: the Lionhead intro draws its own frames. While it runs two
// eye passes, it holds the camera still and puts the head on it. True when
// this call is the intro's camera call, and then out_position and out_focus
// are the still camera. IntroCameraBuilt runs after the engine built the
// camera, and moves it half an eye separation.
bool IntroCameraPose(uintptr_t caller, const float* position, const float* focus,
                     float* out_position, float* out_focus);
void IntroCameraBuilt();
// sky_detail.cpp: clouds and far billboards are kept free of the head.
bool SkyHeadFree();
// The heading matrix for far billboards, its builder, and the angle added.
const uintptr_t kBillboardTurn = 0x00EA1D88;
const uintptr_t kBillboardHeading = 0x00841260;
const uintptr_t kBillboardHeadingOffset = 0x008C78D8;

// head_pose.cpp
bool HeadPoseCurrent(float matrix_out[9], float translation_out[3]);
// tabletop.cpp: the table takes the main call of a frame. It gives the camera
// to put the head on, and the gain for the lean. See docs/tabletop.md, "The table camera".
bool TabletopCamera(const float asked_position[3], const float asked_focus[3],
                    float position_out[3], float focus_out[3],
                    float* lean_gain);
// cutscene_window.cpp: this frame draws as the stereoscopic window. The game
// camera then goes through with no head on it, as in play mode 1.
bool CutsceneWindowFlat();

namespace {

const uintptr_t kUpdateCamera = 0x00819920;

typedef void(__fastcall* UpdateCameraFn)(const float* position,
                                         const float* focus);
UpdateCameraFn o_UpdateCamera = nullptr;

bool g_installed = false;
bool g_enabled = false;
bool g_world_is_up = false;
bool g_head_roll = true;

// Rotations are 3x3, stored row major, and read in the COLUMN convention:
// m[r * 3 + c] is row r column c, and a column is a basis vector in the frame
// the matrix maps to. So m * v rotates v, and the columns of a camera basis
// are its right, its up and its forward, in world space.
void MatMul(const float a[9], const float b[9], float out[9]) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] +
                       a[r * 3 + 1] * b[1 * 3 + c] +
                       a[r * 3 + 2] * b[2 * 3 + c];
    }
  }
}

void MatTranspose(const float a[9], float out[9]) {
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) out[r * 3 + c] = a[c * 3 + r];
  }
}

void Cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

float Dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float Length(const float a[3]) { return sqrtf(Dot(a, a)); }

// THE ROOM: world up, and the game camera's heading. Columns are its right,
// its up and its forward, in world space. Anything that should stay put while
// the player looks around, the Hand and the subtitles, stays put in this.
float g_stage_basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
bool g_have_stage = false;
// Whether the Hand stays where it is in the room when the player turns their
// head, rather than sweeping across the world with the view. pointer_map.cpp
// does the work, by moving the cursor. See HeadCameraSetHandAnchored.
bool g_hand_anchored = true;

// The roll the position-and-focus camera could not express, in radians.
float g_roll = 0.0f;

// The rotation and position the scene is actually being drawn from. The
// projection layer has to declare these, or the runtime reprojects the picture
// from a pose it never came from and the world swims.
float g_render_basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
float g_render_position[3] = {0.0f, 0.0f, 0.0f};
bool g_have_render = false;

// The game's own camera, as the engine would build it from what the game
// asked for: no head and no roll. The game's camera code works in this one,
// so pointer_map.cpp needs it to give that code a pointer it understands.
// Columns are right, up and forward, in world space.
float g_game_basis[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
bool g_have_game = false;

// The camera the game asked for on the main call, and a count of main calls.
// pointer_map.cpp shows it to the game's camera code, and uses the count to
// see whether a frame made its main call at all. While script_camera.cpp
// holds a shot, this is the held shot, which is the one on screen.
float g_game_position[3] = {0.0f, 0.0f, 0.0f};
float g_game_focus[3] = {0.0f, 0.0f, 0.0f};
bool g_have_game_camera = false;
uint64_t g_main_serial = 0;

float* UnscaledRotation() {
  return reinterpret_cast<float*>(bw::tech::kWorldToCameraUnscaled);
}
float* CameraPosition() {
  return reinterpret_cast<float*>(bw::tech::kCameraPos);
}
uint64_t g_calls = 0;
uint64_t g_moved = 0;
uint64_t g_records = 0;
uint64_t g_restored = 0;
uint64_t g_frames = 0;
int g_logged = 0;

// ONE FRAME, ONE HEAD.
//
// The engine places the main camera BEFORE it walks the scene, and every other
// camera DURING the walk. So a call made outside the walk owns the frame, and
// a call made inside it is a sub camera that must leave the frame's state
// alone. The scene flag is set around the whole of GGame::Process3dEngine,
// including the paths where it returns early, so a frame that draws mono is
// counted exactly like a frame that draws two eyes.
//
// The serial is the second half of the rule. Without it, a frame with no sub
// cameras at all would let a second outside call overwrite the first.
bool g_in_scene = false;
uint64_t g_frame = 0;
uint64_t g_recorded_frame = 0xFFFFFFFFFFFFFFFFull;

bool RecordThisCall() {
  if (g_in_scene) return false;
  if (g_frame == g_recorded_frame) return false;
  g_recorded_frame = g_frame;
  return true;
}

// THE TABLE OF THIS FRAME. When tabletop.cpp takes the main call, the render
// head and its focus are kept here, with the camera the table gave. The
// stored camera then holds the render head until the end of
// GGame::ProcessGraphicsEngine.
uint64_t g_table_frame = 0xFFFFFFFFFFFFFFFFull;
float g_table_eye[3] = {0.0f, 0.0f, 0.0f};
float g_table_camera[3] = {0.0f, 0.0f, 0.0f};
float g_table_camera_focus[3] = {0.0f, 0.0f, 0.0f};
uint64_t g_table_records = 0;
uint64_t g_table_restores = 0;
// Main calls of flat frames of the cut scene window, for the report.
uint64_t g_flat_records = 0;

bool SameTableEye(const float position[3]) {
  for (int i = 0; i < 3; ++i) {
    const float d = position[i] - g_table_eye[i];
    if (d > 0.01f || d < -0.01f) return false;
  }
  return true;
}

// THE HEADING OF FAR BILLBOARDS.
//
// UpdateCamera also builds a turn about world up at 0x00EA1D88, from the
// stored camera before it is given back (0x00819E3E to 0x00819EF2). The draw
// at 0x0080DB30 turns meshes with the billboard flag by it at the lowest
// detail, so far trees turned with the head's yaw. It is built again here the
// way the engine builds it, from a camera with no head: the heading from
// fn_00841260, plus the constant at 0x008C78D8. Only the four entries the
// engine writes are touched. The rest is the identity.
void BillboardTurn(const float* from, const float* to) {
  if (!SkyHeadFree()) return;
  typedef float(__cdecl * HeadingFn)(const float* from, const float* to);
  const float angle =
      reinterpret_cast<HeadingFn>(kBillboardHeading)(from, to) +
      *reinterpret_cast<const float*>(kBillboardHeadingOffset);
  float* turn = reinterpret_cast<float*>(kBillboardTurn);
  turn[0] = cosf(angle);
  turn[2] = sinf(angle);
  turn[6] = -sinf(angle);
  turn[8] = cosf(angle);
}

// Half a degree from vertical. The engine builds its camera basis by crossing
// world up with the forward, so a forward that is straight up has no basis at
// all, and neither the engine nor this file can build one. Looking a fraction
// short of vertical is far better than a view that spins.
const float kMinHorizontal = 0.0087f;

// One camera call. intro is true for the call the Lionhead intro makes once
// for each eye pass. The intro has no scene walk, so the world is never up
// and the table never owns it, but the head goes on it like any camera.
void PlaceCamera(const float* position, const float* focus, bool intro,
                 uintptr_t caller) {
  ++g_calls;
  // THE STORY CAMERA HOLDS ITS SHOT HERE, BEFORE ANYTHING READS THE POSE.
  //
  // script_camera.cpp can put a held shot in place of the pose the caller
  // asked for. It does this only for the main call in GCamera::Update and
  // the hand demo's own call, and it knows them by the return address. From
  // here on the held shot is the camera the game asked for. The head goes on
  // it, RecordThisCall records it, and the stored camera and the billboard
  // heading get it. The early returns below pass it on too, so the hold also
  // works with head tracking off.
  float shown_position[3], shown_focus[3];
  if (!intro && ScriptCameraRenderPose(caller, position, focus, shown_position,
                                       shown_focus)) {
    position = shown_position;
    focus = shown_focus;
  }
  // THE CUT SCENE WINDOW (cutscene_window.cpp, docs/cutscene-window.md).
  //
  // In a flat frame the picture goes to a screen in the room, so the camera
  // is the game's own: its pitch, its moves and its cuts, with no head and no
  // lean. Every call of the frame goes through as it came, the sub cameras
  // too. That is what play mode 1 does with head tracking off.
  //
  // The main call still tells tabletop.cpp that the frame is not the God
  // view's, so the scale, the fog and the Hand of the table let go. It also
  // keeps the camera that the game asked for. The first full view frame
  // after the window then starts from a camera that is not old.
  if (!intro && g_enabled && CutsceneWindowFlat()) {
    if (RecordThisCall() && g_world_is_up && position != nullptr &&
        focus != nullptr && !IsBadReadPtr(position, sizeof(float) * 3) &&
        !IsBadReadPtr(focus, sizeof(float) * 3)) {
      ++g_flat_records;
      ++g_main_serial;
      float unused_position[3], unused_focus[3], unused_gain = 1.0f;
      TabletopCamera(position, focus, unused_position, unused_focus,
                     &unused_gain);
      memcpy(g_game_position, position, sizeof(g_game_position));
      memcpy(g_game_focus, focus, sizeof(g_game_focus));
      g_have_game_camera = true;
    }
    o_UpdateCamera(position, focus);
    return;
  }
  float h[9];
  float lean[3];
  // Not until the world is up. UpdateCamera runs during loading and in menus
  // too, and those cameras are not ours to move. The intro is the one
  // exception, and it asks by name.
  if (!(g_world_is_up || intro) || !g_enabled || position == nullptr ||
      focus == nullptr ||
      !HeadPoseCurrent(h, lean)) {
    o_UpdateCamera(position, focus);
    return;
  }
  if (IsBadReadPtr(position, sizeof(float) * 3) ||
      IsBadReadPtr(focus, sizeof(float) * 3)) {
    o_UpdateCamera(position, focus);
    return;
  }

  // The camera the caller asked for, copied before the engine can overwrite
  // it. A caller may pass the engine's own stored camera, and the call below
  // rewrites that.
  const float asked_position[3] = {position[0], position[1], position[2]};
  const float asked_focus[3] = {focus[0], focus[1], focus[2]};

  // THE TABLE TAKES THE MAIN CALL.
  //
  // In tabletop mode the head goes on a camera at the recenter pose over the
  // table, not on the camera the game asked for, and the lean is s units per
  // meter, not the life size scale. Everything below is the same formula.
  // The game still records its own camera, so the game logic keeps it.
  float table_position[3], table_focus[3];
  float lean_gain = 1.0f;
  const bool main_call = !g_in_scene && g_frame != g_recorded_frame;
  const bool table =
      main_call && !intro &&
      TabletopCamera(asked_position, asked_focus, table_position, table_focus,
                     &lean_gain);
  if (table) {
    position = table_position;
    focus = table_focus;
    for (int i = 0; i < 3; ++i) lean[i] *= lean_gain;
  } else if (g_in_scene && g_table_frame == g_frame && SameTableEye(position)) {
    // THE STORED CAMERA COMES BACK. fn_0068B7D0 gives the engine the stored
    // camera again after a camera of its own. In a table frame the stored
    // camera is the render head, so the head is already on it. It goes
    // through as it is, and the far billboards keep the table's heading.
    ++g_table_restores;
    o_UpdateCamera(position, focus);
    BillboardTurn(g_table_camera, g_table_camera_focus);
    return;
  }

  // THE BASIS COMES FROM THE ARGUMENTS, NOT FROM THE MATRIX.
  //
  // The world to camera matrix is built from the PREVIOUS frame's already
  // rotated camera. A basis read from it would apply the head rotation on top
  // of itself every frame, and the camera would spin.
  //
  // The arguments are the game's own camera, untouched, so a basis built from
  // them cannot accumulate.
  const float dx = focus[0] - position[0];
  const float dy = focus[1] - position[1];
  const float dz = focus[2] - position[2];
  const float distance = sqrtf(dx * dx + dy * dy + dz * dz);
  // A camera looking at its own position is not a camera. Invent nothing:
  // hand it straight through.
  if (distance < 1e-3f) {
    o_UpdateCamera(position, focus);
    return;
  }
  const float axis_z[3] = {dx / distance, dy / distance, dz / distance};
  const float up_world[3] = {0.0f, 1.0f, 0.0f};

  // THE LEVELLED BASIS L, which is the room the player stands in. Its up is
  // world up, so gravity in the game points the same way as gravity in the
  // room. Its forward is the camera's heading, flattened.
  float level_forward[3] = {axis_z[0], 0.0f, axis_z[2]};
  const float level_length =
      sqrtf(level_forward[0] * level_forward[0] +
            level_forward[2] * level_forward[2]);
  // Looking straight up or straight down leaves no heading to level. This
  // game never does, so hand it through rather than invent a heading.
  if (level_length < 1e-3f) {
    o_UpdateCamera(position, focus);
    return;
  }
  level_forward[0] /= level_length;
  level_forward[2] /= level_length;
  float level_right[3];
  Cross(up_world, level_forward, level_right);

  float basis_level[9];
  for (int i = 0; i < 3; ++i) {
    basis_level[i * 3 + 0] = level_right[i];
    basis_level[i * 3 + 1] = up_world[i];
    basis_level[i * 3 + 2] = level_forward[i];
  }

  // THE HEAD TURNS INSIDE THE ROOM, WHOLE.
  //
  //     R = L * H
  //
  // L is the room: world up, and the game camera's heading. H is the player's
  // head, exactly as the runtime reports it in its gravity aligned space. The
  // game's own downward pitch is not in the view at all.
  //
  // That is the only composition under which the world stands still. Turning
  // the head from A to B then turns the view by exactly the rotation the head
  // made, for every A and B, so nothing in the world moves when the player
  // looks around. The horizon is always level, because the room's up is the
  // world's up.
  //
  // Two other orders move the world. L * H * P puts the head outside the
  // game's pitch P, so a head tilt rotates about the level forward axis while
  // the camera looks 13 degrees below it, and the look direction sweeps
  // sideways by 4.51 degrees for a 20 degree tilt. Splitting H, so its yaw
  // goes outside P and its pitch and roll go inside, fixes the look direction
  // but leaves the same 4.51 degrees in the roll axis, which feels like
  // sliding.
  //
  // The cost is that the player's straight ahead view is the horizon rather
  // than the island, and they look down at the land with their head, as they
  // would over a real table. The game's pitch control still moves where the
  // camera sits.
  float render[9];
  MatMul(basis_level, h, render);

  float forward[3] = {render[2], render[5], render[8]};
  float head_up[3] = {render[1], render[4], render[7]};

  // Hold the view a fraction short of vertical, so the basis the engine is
  // about to build from this forward is always well conditioned.
  const float horizontal =
      sqrtf(forward[0] * forward[0] + forward[2] * forward[2]);
  if (horizontal < kMinHorizontal) {
    float hx = level_forward[0];
    float hz = level_forward[2];
    if (horizontal > 1e-6f) {
      hx = forward[0] / horizontal;
      hz = forward[2] / horizontal;
    }
    const float sign = forward[1] >= 0.0f ? 1.0f : -1.0f;
    forward[0] = hx * kMinHorizontal;
    forward[2] = hz * kMinHorizontal;
    forward[1] = sign * sqrtf(1.0f - kMinHorizontal * kMinHorizontal);
    // Keep the up vector square to the forward that was actually used.
    const float along = Dot(head_up, forward);
    for (int i = 0; i < 3; ++i) head_up[i] -= forward[i] * along;
    const float hu = Length(head_up);
    if (hu > 1e-4f) {
      for (int i = 0; i < 3; ++i) head_up[i] /= hu;
    }
  }

  // LEANING MOVES THE CAMERA, IN THE ROOM'S AXES.
  //
  // The lean arrives from head_pose.cpp already written in the levelled frame:
  // it is the head's offset from the recentre origin, turned by the inverse of
  // the origin yaw. So it needs the levelled basis and nothing else. The head
  // rotation must not turn it: leaning sideways while turned 90 degrees would
  // move the camera forward.
  float moved_position[3];
  for (int i = 0; i < 3; ++i) {
    moved_position[i] = position[i] + basis_level[i * 3 + 0] * lean[0] +
                        basis_level[i * 3 + 1] * lean[1] +
                        basis_level[i * 3 + 2] * lean[2];
  }

  const float new_focus[3] = {moved_position[0] + forward[0] * distance,
                              moved_position[1] + forward[1] * distance,
                              moved_position[2] + forward[2] * distance};

  if (RecordThisCall()) {
    ++g_records;
    ++g_main_serial;
    memcpy(g_stage_basis, basis_level, sizeof(g_stage_basis));
    g_have_stage = true;
    memcpy(g_game_position, asked_position, sizeof(g_game_position));
    memcpy(g_game_focus, asked_focus, sizeof(g_game_focus));
    g_have_game_camera = true;

    // The game's own basis comes from the camera the game asked for. Only
    // the table makes that a different camera from the one used here.
    float game_axis[3] = {axis_z[0], axis_z[1], axis_z[2]};
    if (table) {
      const float g[3] = {asked_focus[0] - asked_position[0],
                          asked_focus[1] - asked_position[1],
                          asked_focus[2] - asked_position[2]};
      const float gd = Length(g);
      if (gd > 1e-3f) {
        for (int i = 0; i < 3; ++i) game_axis[i] = g[i] / gd;
      }
      g_table_frame = g_frame;
      ++g_table_records;
      memcpy(g_table_eye, moved_position, sizeof(g_table_eye));
      memcpy(g_table_camera, position, sizeof(g_table_camera));
      memcpy(g_table_camera_focus, focus, sizeof(g_table_camera_focus));
    }
    float game_right[3];
    Cross(up_world, game_axis, game_right);
    const float gr = Length(game_right);
    if (gr > 1e-4f) {
      for (int i = 0; i < 3; ++i) game_right[i] /= gr;
      float game_up[3];
      Cross(game_axis, game_right, game_up);
      for (int i = 0; i < 3; ++i) {
        g_game_basis[i * 3 + 0] = game_right[i];
        g_game_basis[i * 3 + 1] = game_up[i];
        g_game_basis[i * 3 + 2] = game_axis[i];
      }
      g_have_game = true;
    }

    // THE ROLL THIS CANNOT CARRY, computed rather than measured.
    //
    // The engine is about to build a roll free basis from the forward above
    // and world up. The rotation this file composed has a definite up vector.
    // The angle between the two, about the forward, is exactly the roll the
    // per eye matrix has to add, and reading it off two dot products has no
    // singularity that the forward clamp above has not already removed.
    float engine_right[3];
    Cross(up_world, forward, engine_right);
    const float er = Length(engine_right);
    if (er > 1e-4f) {
      for (int i = 0; i < 3; ++i) engine_right[i] /= er;
      float engine_up[3];
      Cross(forward, engine_right, engine_up);
      g_roll = atan2f(Dot(head_up, engine_right), Dot(head_up, engine_up));
    }

    // What the scene is about to be drawn from, for the projection layer to
    // declare. The basis is rebuilt from the forward and the up that were
    // actually used, so it matches the picture even after the clamp.
    float render_right[3];
    Cross(head_up, forward, render_right);
    const float rr = Length(render_right);
    if (rr > 1e-4f) {
      for (int i = 0; i < 3; ++i) render_right[i] /= rr;
      for (int i = 0; i < 3; ++i) {
        g_render_basis[i * 3 + 0] = render_right[i];
        g_render_basis[i * 3 + 1] = head_up[i];
        g_render_basis[i * 3 + 2] = forward[i];
      }
      for (int i = 0; i < 3; ++i) g_render_position[i] = moved_position[i];
      g_have_render = true;
    }

    if (g_logged < 2) {
      ++g_logged;
      ModLog("head camera  : looking %.3f %.3f %.3f, was %.3f %.3f %.3f, "
             "roll left over %.1f degrees", forward[0], forward[1], forward[2],
             axis_z[0], axis_z[1], axis_z[2], g_roll * 57.29578f);
    }
  }

  ++g_moved;
  o_UpdateCamera(moved_position, new_focus);

  // GIVE THE GAME BACK THE CAMERA IT ASKED FOR.
  //
  // UpdateCamera does two things. It builds everything the renderer needs,
  // the matrices, the frustum and the culling cone, from the camera it is
  // given, and those must carry the head. It also STORES that camera at
  // 0x00EA1DB8, the position, and 0x00EA1DC4, the focus, where 38 other
  // places read it afterwards. Those readers are game logic, and they must
  // not carry the head.
  //
  //   HandStateHolding::Update  works out what a held object can do from the
  //                             vector between the Hand and the focus, and
  //                             sets the CameraHelp hint from it.
  //   HandStateCamera::Update   drags the land relative to the focus.
  //   GCamera::Update           in some modes takes the stored camera back as
  //                             its own starting point, so the head would
  //                             feed into the game camera and go on again the
  //                             next frame.
  //   GScript::StoreCameraDetails, GScript::GetCameraFocus, Tree::PreDraw,
  //   and the sky and cloud module at 0x005E5CD0, which recomputes whenever
  //   the stored camera moves.
  //
  // Under gravity lock a stored focus with the head is a point in the air
  // toward the horizon, so the hint for a held object, the land drag and the
  // cloud layout would be worked out from the wrong place. The pointer that
  // the interface gets is in window pixels. See pointer_map.cpp.
  //
  // The renderer has already taken what it needs by the time this runs, so
  // the stored camera goes back to the one the game asked for.
  //
  // Not in a table frame. There the eye is thousands of units from the game
  // camera, and the Hand casts its ray from the stored camera. So the stored
  // camera keeps the render head, and tabletop.cpp gives the game its camera
  // back at the end of GGame::ProcessGraphicsEngine.
  float* stored = CameraPosition();
  if (!table && !IsBadWritePtr(stored, sizeof(float) * 6)) {
    for (int i = 0; i < 3; ++i) {
      stored[i] = asked_position[i];
      stored[3 + i] = asked_focus[i];
    }
    ++g_restored;
  }

  // The far billboards face the camera the game asked for, or the table.
  BillboardTurn(table ? position : asked_position,
                table ? focus : asked_focus);
}

void __fastcall h_UpdateCamera(const float* position, const float* focus) {
  const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
  float still_position[3], still_focus[3];
  if (IntroCameraPose(caller, position, focus, still_position, still_focus)) {
    PlaceCamera(still_position, still_focus, true, caller);
    IntroCameraBuilt();
    return;
  }
  PlaceCamera(position, focus, false, caller);
}

}  // namespace

// Told by the stereo hook once the scene has actually been walked, so the
// camera is left alone during loading screens and menus.
void HeadCameraWorldIsUp() { g_world_is_up = true; }

// Told by the stereo hook around the whole scene walk, whether that walk draws
// one eye or two. Every camera the engine builds between these two is a sub
// camera, and none of them owns the frame's state.
void HeadCameraBeginScene() { g_in_scene = true; }

// The intro has no scene walk to end, so it ends its own frame here. One frame
// gives one head to both of its eye passes. See RecordThisCall.
void HeadCameraIntroFrame() {
  ++g_frame;
  ++g_frames;
}

void HeadCameraEndScene() {
  g_in_scene = false;
  ++g_frame;
  ++g_frames;
}

void HeadCameraSetEnabled(bool on) {
  g_enabled = on;
  ModLog("head camera  : the engine's own camera %s carry the head",
         on ? "will" : "will not");
}

// False in a flat frame of the cut scene window too. The pointer, the
// spirits, the ears, the hands and the teleport all ask this, and in a flat
// frame each does what it does in play mode 1.
bool HeadCameraEnabled() { return g_enabled && !CutsceneWindowFlat(); }

// The render head of the last table frame, in world units. False when no
// table frame was drawn yet.
bool HeadCameraTableEye(float out[3]) {
  if (g_table_frame == 0xFFFFFFFFFFFFFFFFull) return false;
  for (int i = 0; i < 3; ++i) out[i] = g_table_eye[i];
  return true;
}

// The setting alone: the head goes on the camera in the full view.
bool HeadCameraOn() { return g_installed && g_enabled; }


void HeadCameraInstall() {
  if (g_installed) return;
  void* target = reinterpret_cast<void*>(kUpdateCamera);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_UpdateCamera),
                    reinterpret_cast<void**>(&o_UpdateCamera));
  if (status == MH_OK) status = MH_EnableHook(target);
  g_installed = status == MH_OK;
  ModLog("hooks        : LH3DTech::UpdateCamera at 0x%08X %s", kUpdateCamera,
         g_installed ? "ok" : "FAILED");
}

// ONE CAMERA FOR THE HAND AND FOR THE GAME.
//
// The game's own camera is not swapped in around CHand::PrepareForDrawing. A
// swap would cast the cursor's ray from where the GAME looks. That would place
// the Hand through one camera while everything else the game does with the
// cursor used another: what the Hand is over, whether a drop is allowed,
// where a held object goes. CHand::UpdateHeldObject is never inside such a
// swap at all.
//
// Under gravity lock the two cameras differ by the game's whole downward
// pitch (12.9 degrees in one measure), so the disagreement is permanent. It
// shows as a red cross beside the Hand, ground that spans under the Hand
// instead of dragging, and objects that drop only near the bottom edge of the
// screen.
//
// So there is one camera, and the CURSOR moves instead. pointer_map.cpp turns
// the cursor by the inverse of the head's rotation each frame, so the Hand
// stays put in the room while every part of the game sees one camera and one
// cursor that agree with each other. The interface gets the pointer in render
// pixels, not window pixels. pointer_map.cpp describes it.

void HeadCameraSetHandAnchored(bool on) {
  g_hand_anchored = on;
  ModLog("head camera  : the Hand %s",
         on ? "stays put in the room when you turn your head"
            : "sweeps the world with your view");
}

bool HeadCameraHandAnchored() { return g_hand_anchored; }

// How much roll is left for the per eye matrix to add.
//
// ROLL AND THE SEA ARE ONE CHOICE, NOT TWO. The scanline sea is drawn as
// full width horizontal strips and cannot tilt, so with roll applied the land
// rolls and the horizon does not: at 20 degrees of roll the land tilts
// correctly while the sea keeps a horizontal horizon.
// So turning roll on turns the plainer sea on with it, which mod_main.cpp
// does when it reads the settings.
//
// Roll is on by default. Without it the world tilts with the player's head
// instead of staying level, and that is worse than a plainer sea.
float HeadCameraRoll() {
  if (!HeadCameraEnabled() || !g_head_roll) return 0.0f;
  return g_roll;
}

void HeadCameraSetRoll(bool on) {
  g_head_roll = on;
  ModLog("head camera  : head roll %s",
         on ? "is carried by the per eye matrix" : "is dropped");
}

bool HeadCameraRollEnabled() { return g_head_roll; }

// The rotation and the position the scene was drawn from, for the projection
// layer to declare. Columns are right, up and forward, in world space.
//
// The four reads below give nothing in a flat frame of the cut scene window.
// The values they hold are from the last full view frame, and a flat frame
// has no room and no head in the world. Each caller already has a path for
// "nothing yet", the one it uses before the first main call.
bool HeadCameraRenderBasis(float basis_out[9], float position_out[3]) {
  if (!g_have_render || CutsceneWindowFlat()) return false;
  for (int i = 0; i < 9; ++i) basis_out[i] = g_render_basis[i];
  for (int i = 0; i < 3; ++i) position_out[i] = g_render_position[i];
  return true;
}

// The room the head turns inside: world up and the game camera's heading.
// Columns are right, up and forward, in world space.
bool HeadCameraStageBasis(float basis_out[9]) {
  if (!g_have_stage || CutsceneWindowFlat()) return false;
  for (int i = 0; i < 9; ++i) basis_out[i] = g_stage_basis[i];
  return true;
}

// The game's own camera, with no head in it. Columns are right, up and
// forward, in world space.
bool HeadCameraGameBasis(float basis_out[9]) {
  if (!g_have_game || CutsceneWindowFlat()) return false;
  for (int i = 0; i < 9; ++i) basis_out[i] = g_game_basis[i];
  return true;
}

// The camera the game asked for on the last main call, with no head in it.
// While script_camera.cpp holds a shot, this is the held shot. False before
// the first main call with the head on.
bool HeadCameraGameCamera(float position[3], float focus[3]) {
  if (!g_have_game_camera || CutsceneWindowFlat()) return false;
  for (int i = 0; i < 3; ++i) {
    position[i] = g_game_position[i];
    focus[i] = g_game_focus[i];
  }
  return true;
}

// Point the engine at the game's own camera from the last main call, with no
// head in it. The matrices, the frustum and the stored camera then all
// describe the camera the game's camera code thinks it is using. See
// h_GCameraUpdate in pointer_map.cpp. False when there is nothing to use.
//
// While script_camera.cpp holds a shot, the recorded camera is the held
// shot. That is also the camera g_game_basis describes, so the pointer that
// pointer_map.cpp turns onto the game's screen agrees with these matrices.
// HeadCameraReapply puts the head back on the same camera.
bool HeadCameraUseGameCamera() {
  if (!g_installed || !HeadCameraEnabled() || !g_world_is_up ||
      !g_have_game_camera) {
    return false;
  }
  const float position[3] = {g_game_position[0], g_game_position[1],
                             g_game_position[2]};
  const float focus[3] = {g_game_focus[0], g_game_focus[1], g_game_focus[2]};
  o_UpdateCamera(position, focus);
  return true;
}

// Put the head back on the engine's camera, for a frame where the game did
// not make its main call after HeadCameraUseGameCamera. Without this the
// frame would be drawn with no head at all.
void HeadCameraReapply() {
  if (!g_have_game_camera) return;
  const float position[3] = {g_game_position[0], g_game_position[1],
                             g_game_position[2]};
  const float focus[3] = {g_game_focus[0], g_game_focus[1], g_game_focus[2]};
  h_UpdateCamera(position, focus);
}

// Goes up by one on every main call.
uint64_t HeadCameraMainSerial() { return g_main_serial; }

void HeadCameraReport() {
  if (!g_installed || g_calls == 0) return;
  // The record count is the one that matters. It must be one per frame. If it
  // climbs above that, a sub camera has taken the frame's state again and both
  // eyes will draw from different cameras.
  ModLog("head camera  : %llu camera updates, %llu carried the head, %llu "
         "recorded over %llu frames, %llu handed back to the game", g_calls,
         g_moved, g_records, g_frames, g_restored);
  if (g_table_records > 0) {
    ModLog("head camera  : %llu main calls were the table's, and the stored "
           "camera came back %llu times", g_table_records, g_table_restores);
  }
  if (g_flat_records > 0) {
    ModLog("head camera  : %llu main calls went through with no head, for "
           "the cut scene window", g_flat_records);
  }
  g_flat_records = 0;
  g_table_records = 0;
  g_table_restores = 0;
  g_calls = 0;
  g_moved = 0;
  g_records = 0;
  g_frames = 0;
  g_restored = 0;
}
