// Four things about the world at a distance: clouds that stay still when the
// head moves, a moon that stays fixed in the sky, a sun glare that stays on
// the sun, and models that keep their detail further out.
//
// CLOUDS THAT FACE THE PLAYER, NOT THE HEAD
//
// A cloud is a small dome, an LH3DMist (mist.l3d, 26 vertices, radius 20,
// base at y 0 and pole at y 20), scaled up to between 13 and 50 times its
// size. LH3DMist::Draw at 0x007FA300 turns each one to face the camera by
// copying a 3x3 basis from 0x00EA1C98 into it (0x007FA38C). The rows of that
// basis are the camera right, the camera forward reversed, and the camera up.
// LH3DTech::UpdateCamera builds it, from the camera it is given, which is the
// head camera.
//
// So every cloud would turn with the head. Its visible body sits 130 to 500
// units from its pivot, along that basis, so a nod would swing it up and down
// across the sky.
//
// The mod gives each cloud its own basis just before it is drawn: facing the
// camera of the frame with no head in it, with world up. In the full view
// that is the stored camera (0x00EA1DB8), which head_camera.cpp keeps free of
// the head. In a table frame (the table and the God view) the stored camera
// is the render head while the scene is drawn, so the clouds face the table
// camera: the place of the recenter pose in the world. The weather clouds
// and ground mist are LH3DMist too, and get the same.
//
// THE HEAD DOES NOT TURN A CLOUD
//
// A cloud that faces the render head turns with each move of the head. In
// the God view the clouds are near the head, so a small move of the head is
// a large turn of a cloud. A cloud that is below the head spins about its
// pole when the head goes over it. The body and the rim of each dome then
// move up, down and to the side. A cloud that faces the table camera keeps
// its place and its turn while the head moves. Only the steps of the player
// (a zoom, a grab, a snap turn) move the table camera.
//
// The engine also scales the rows 1 and 2 by A (below), from the stored
// camera. So the mod gives those rows the ratio of A from the table camera to
// A from the render head, and the shape of a cloud does not change with the
// head either. The key clouds_head_free=0 gives the clouds the render head
// in a table frame.
//
// A CLOUD MUST NOT REACH THE EYE
//
// That facing points the pole of each dome at the camera. The engine then
// scales the rows of the basis (0x007FA4B7 to 0x007FA539). Row 0 gets the
// scale at +0x88. Rows 1 and 2 get
//
//     A = scale / ((flatten - 1) * (1 - |dy| / D) + 1)
//
// with D and dy measured from the stored camera. A cloud seen from below is
// not flattened, so its pole is 20 * A = 260 to 1000 units long. The clouds
// hang only 300 to 500 units up. So the dense middle of the smoke would come
// down the eye line toward the player: gray shapes near the head, a gray
// wash, and dome bodies that cut through the land.
//
// The pole is at most a share of the distance to the camera, 25 percent
// by default. The pole lies on the eye line, so a shorter pole does not
// change the outline the player sees. It only keeps the dome away from the
// eye. The limit follows the game camera's distance, never the head.
//
// The facing stays per dome for a reason. The domes are one sided: the
// primitive flags of mist.l3d are 0, so fn_00856D40 removes back faces in
// software. One basis for all domes shows the base side of every dome behind
// the game camera, and those domes vanish when the player turns around.
//
// THE MOON
//
// LH3DSky::Draw (0x0086B010) puts the moon at the stored camera plus the
// moon position (0x00EDD378), so the moon keeps its direction from the eye
// at each place, as a thing at a very large distance does. fn_0086AC60 then turns it to the camera: it takes the
// direction d from the camera matrix (0x00EA1D28) to the moon, and the right
// of the moon is the camera up crossed with d. The camera matrix has the
// head, with its roll, so with that matrix the moon turns with the head. The
// glow quad (fn_0086A930) and the moon mesh (the object at 0x00FA2728) both
// take that turn. The reflection of the moon in the sea is the second call.
//
// The mod gives fn_0086AC60 a camera matrix at the same place with world up
// (sky_math.h, LevelCamera), and puts the camera matrix back after the call.
// The moon then turns only with its direction from the eye, so it stays
// fixed in the sky. The key moon_world_up=0 gives the moon the camera with
// the head.
//
// The sun disc (fn_0086C140) is a fixed quad and reads no camera turn. The
// dome (fn_0086B6B0) is a sphere about a fixed point.
//
// THE SUN GLARE
//
// The sun is drawn two times. The disc is drawn in the scene walk
// (fn_0086C140). The glare is drawn later, in LH3DRender::FinishFrame
// (fn_0086BB60, called at 0x0082F4C9). Before the glare, FinishFrame flushes
// the sorted draw list (0x0082F280). One item on that list, fn_0068B480, gives
// the engine a camera of its own. It then puts the stored camera back through
// UpdateCamera (fn_0068B7D0, called at 0x0068BF2F). That call goes through
// head_camera.cpp, which rebuilds the camera matrices with the head, but with
// no eye offset and no roll.
//
// So the glare is drawn from a different camera than the rest of the eye.
// With head roll, the glare turns about the middle of the view, away from
// the disc, and the sun seems to bob. There are three modes, and the ini key
// sun_glare picks one:
//
//     0  put the eye camera back (StereoReapplyEye), then draw the glare
//     1  draw the glare as the game does
//     2  do not draw the glare
//
// MODELS KEEP THEIR DETAIL FURTHER OUT
//
// LH3DObject::AddForDrawing (0x00815A70, and its twin at 0x00816AD0) picks a
// mesh by the depth of an object along the view axis, against four multiples
// of a size figure:
//
//     below 23.3   full detail          below 86.7   lowest detail
//     below 66.7   middle detail        below 173.3  lowest, fading out
//     beyond that, a stand-in sprite or nothing
//
// The four multiples live at 0x00C37E8C to 0x00C37E98, in writable data, with
// two readers each and no writer. Multiplying them moves every switch out by
// the same factor. With the game's own settings a villager goes at about 87
// units, a tree at about 350 and a large building at about 867.
//
// The registry's MaxObjectDistance does nothing: its value is read into a
// stack slot at 0x008238C4 and overwritten by the next read.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "sky_math.h"

extern void ModLog(const char* format, ...);
// head_camera.cpp: the camera of the table frame that is drawn, with no
// head. False outside a table frame.
bool HeadCameraTableCamera(float out[3]);
// controllers.cpp: the frame number that the scripted controller counts.
uint64_t ControllersFrameCount();
// head_camera.cpp: the room, which is world up and the game camera's
// heading. Columns are right, up and forward, in world space.
bool HeadCameraStageBasis(float basis_out[9]);
// stereo.cpp: give the engine the camera of the eye pass that is running.
// Outside an eye pass it does nothing.
void StereoReapplyEye();
// tabletop.cpp: true for a cloud past the edge of the table. Always false
// when the table does not own the frame.
bool TabletopHidesMist(const void* mist);
// hand_glow.cpp: the glow at the Hand's point, in each eye pass.
void HandGlowDraw();
// overlay.cpp: the mod's own meshes, in each eye pass.
void OverlayDraw();

namespace {

const uintptr_t kMistDraw = 0x007FA300;
const uintptr_t kMistBasis = 0x00EA1C98;
const uintptr_t kStoredPosition = 0x00EA1DB8;
const int kMistPosition = 0x38;
// LH3DMist fields that the scale at 0x007FA4B7 reads.
const int kMistFlags = 0x80;    // byte. The sky cloud branch tests bit 2.
const uint8_t kMistSkyCloud = 2;
const int kMistScale = 0x88;    // float, the size of the dome
const int kMistFlatten = 0x8C;  // float, how flat a cloud near the horizon is
const float kDomeHeight = 20.0f;  // mist.l3d: the pole is at y 20

// The moon. fn_0086AC60 is __cdecl with three arguments: the place of the
// moon, the turn that it makes (12 floats) and the matrix of the moon mesh.
// It reads the camera matrix at 0x00EA1D28. Two calls, both in LH3DSky::Draw
// (0x0086B649 and the reflection at 0x0086B685).
const uintptr_t kMoonTurn = 0x0086AC60;
const uintptr_t kCameraMatrix = 0x00EA1D28;

// The time of day of the game, in hours. UpdateTime moves the visual time
// (0x00BF3380) toward the time that GLandAlignement::SetTime (0x005E22E0,
// __thiscall, two floats, ret 8) sets at 0x00BF3384. SetTime also sets the
// flag of the night. LH3DAtmos::UpdateGame puts the moon at a height of
// 1100 * cos(15 * t) - 150, with t from the visual time (fn_0086A160), and
// LH3DSky::Draw draws it above 220.
const uintptr_t kVisualTime = 0x00BF3380;
const uintptr_t kSetTime = 0x005E22E0;

// The sun glare. No arguments and a plain ret. The only call is at
// 0x0082F4C9, in LH3DRender::FinishFrame.
const uintptr_t kSunGlare = 0x0086BB60;
// The disc draw sets this to 1 (0x0086C20D) and the glare sets it to 0
// (0x0086BB92). No other code reads or writes it.
const uintptr_t kSunGlareWanted = 0x00FA275C;

enum GlareMode {
  kGlareEyeCamera = 0,
  kGlareAsGame = 1,
  kGlareHidden = 2,
  kGlareModes = 3
};

const uintptr_t kLodDistances = 0x00C37E8C;
const int kLodCount = 4;
const float kLodExpected[kLodCount] = {23.333f, 66.667f, 86.667f, 173.333f};

typedef void(__fastcall* MistDrawFn)(void* self, void* unused);
MistDrawFn o_MistDraw = nullptr;
typedef void(__cdecl* SunGlareFn)();
SunGlareFn o_SunGlare = nullptr;
typedef void(__cdecl* MoonTurnFn)(const float* place, float* turn,
                                  float* mesh);
MoonTurnFn o_MoonTurn = nullptr;

bool g_head_free = true;
bool g_hooked = false;
// clouds_head_free: in a table frame the clouds face the table camera, not
// the render head.
bool g_clouds_head_free = true;
// moon_world_up: the moon turns with world up, not with the camera up.
bool g_moon_world_up = true;
// sky_test_frame: at this frame of the scripted controller both keys above
// change, for one run with each value. 0 is no change.
uint64_t g_test_frame = 0;
bool g_test_done = false;
// sky_test_hour: at the first frame of play the time of day of the game
// goes to this hour, so a run in a test save can show the moon. -1 is no
// change.
int g_test_hour = -1;
bool g_test_hour_done = false;
// sky_test_trace: the report has how far the clouds and the moon moved from
// one frame to the next.
bool g_trace = false;
// The longest pole a cloud may have, as a share of its distance to the
// camera. Zero means no limit.
float g_pole_share = 0.25f;
int g_glare_mode = kGlareEyeCamera;
float g_lod_saved[kLodCount];
bool g_lod_have_saved = false;
float g_lod_factor = 1.0f;

// For the report. Each eye pass draws every dome, so both passes count.
uint64_t g_dome_draws = 0;
uint64_t g_poles_limited = 0;
uint64_t g_glare_calls = 0;
uint64_t g_glare_wanted = 0;
uint64_t g_table_faced = 0;
uint64_t g_moon_turns = 0;
uint64_t g_moon_level = 0;

// THE TRACE (sky_test_trace=1). For each dome, the last frame in which it
// was drawn, its place and the three axes that the engine gives it (the
// basis rows with the engine's scale, times the dome size). A move of an
// axis tip from one frame to the next is a move of the body or the rim.
struct DomeTrace {
  const void* mist;
  uint64_t frame;
  float place[3];
  float axes[9];
};
const int kTraceSlots = 512;
DomeTrace g_traces[kTraceSlots];
float g_trace_shape_most = 0.0f;   // units, the largest move of an axis tip
double g_trace_shape_sum = 0.0;
float g_trace_place_most = 0.0f;   // units, the largest move of a pivot
uint64_t g_trace_pairs = 0;
float g_trace_eye_most = 0.0f;     // units, the largest move of the point
uint64_t g_trace_eye_frame = 0;    // the clouds face
float g_trace_eye[3] = {0.0f, 0.0f, 0.0f};
// The moon: the angle of its right from the horizontal, and the largest turn
// of its right from one frame to the next.
float g_moon_tilt_most = 0.0f;
float g_moon_step_most = 0.0f;
uint64_t g_moon_frame = 0;
float g_moon_right[3] = {0.0f, 0.0f, 0.0f};

void Normalize(float* v) {
  const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (length < 1e-6f) return;
  v[0] /= length;
  v[1] /= length;
  v[2] /= length;
}

// A cloud straight above or below the camera has no right from world up.
// The room gives it one: the game camera's right, which the head does not
// move. Without a room yet, the world x axis does the same job. The result
// is made square to the forward, so the basis stays a true rotation.
void OverheadRight(const float forward[3], float right[3]) {
  float stage[9];
  if (HeadCameraStageBasis(stage)) {
    right[0] = stage[0];
    right[1] = stage[3];
    right[2] = stage[6];
  } else {
    right[0] = 1.0f;
    right[1] = 0.0f;
    right[2] = 0.0f;
  }
  const float along = right[0] * forward[0] + right[1] * forward[1] +
                      right[2] * forward[2];
  for (int i = 0; i < 3; ++i) right[i] -= forward[i] * along;
  Normalize(right);
}

// Makes the pole of the dome at most g_pole_share of its distance. The
// engine copies the basis and then scales its rows, so a shorter row 1 gives
// a shorter pole. Rows 0 and 2 stay as they are, so the outline does not
// change. `a` is the scale that the engine gives the pole, seen from the
// point that the cloud faces, and `distance` is the distance to that point.
// True when the limit made the pole shorter.
bool LimitPole(float a, float distance, float basis[9]) {
  if (g_pole_share <= 0.0f) return false;
  // A scale of zero is a dome the game itself draws wrong. Leave it.
  if (a == 0.0f) return false;
  const float pole = kDomeHeight * fabsf(a);
  const float most = g_pole_share * distance;
  if (!(pole > most)) return false;
  const float k = most / pole;
  basis[3] *= k;
  basis[4] *= k;
  basis[5] *= k;
  return true;
}

// At the first frame of play, the time of day of sky_test_hour.
void TestHourCheck() {
  if (g_test_hour < 0 || g_test_hour_done) return;
  if (ControllersFrameCount() == 0) return;
  g_test_hour_done = true;
  const float hour = static_cast<float>(g_test_hour);
  typedef void(__fastcall * SetTimeFn)(void* self, void* unused, float time,
                                       float seconds);
  reinterpret_cast<SetTimeFn>(kSetTime)(nullptr, nullptr, hour, 0.0f);
  *reinterpret_cast<float*>(kVisualTime) = hour;
  ModLog("sky          : sky_test_hour: the time of day of the game is %d "
         "hours now, frame %llu", g_test_hour, ControllersFrameCount());
}

// At sky_test_frame both keys change one time.
void TestFrameCheck() {
  TestHourCheck();
  if (g_test_frame == 0 || g_test_done) return;
  const uint64_t frame = ControllersFrameCount();
  if (frame < g_test_frame) return;
  g_test_done = true;
  g_clouds_head_free = !g_clouds_head_free;
  g_moon_world_up = !g_moon_world_up;
  ModLog("sky          : sky_test_frame at frame %llu: clouds_head_free=%d "
         "and moon_world_up=%d now", frame, g_clouds_head_free ? 1 : 0,
         g_moon_world_up ? 1 : 0);
}

float Distance3(const float a[3], const float b[3]) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

DomeTrace* TraceSlot(const void* mist) {
  const uintptr_t key = reinterpret_cast<uintptr_t>(mist);
  int index = static_cast<int>((key >> 4) % kTraceSlots);
  for (int probe = 0; probe < kTraceSlots; ++probe) {
    DomeTrace* slot = &g_traces[index];
    if (slot->mist == mist) return slot;
    if (slot->mist == nullptr) {
      slot->mist = mist;
      slot->frame = 0;
      return slot;
    }
    index = (index + 1) % kTraceSlots;
  }
  return nullptr;
}

// One dome draw for the trace. The axes are the basis rows with the scale
// of the engine, times the dome size.
void TraceDome(const void* mist, const float place[3], const float basis[9],
               float scale, float a, const float eye[3]) {
  const uint64_t frame = ControllersFrameCount();
  if (frame != g_trace_eye_frame) {
    if (g_trace_eye_frame != 0 && frame == g_trace_eye_frame + 1) {
      const float moved = Distance3(eye, g_trace_eye);
      if (moved > g_trace_eye_most) g_trace_eye_most = moved;
    }
    g_trace_eye_frame = frame;
    memcpy(g_trace_eye, eye, sizeof(g_trace_eye));
  }
  DomeTrace* slot = TraceSlot(mist);
  if (slot == nullptr || slot->frame == frame) return;
  float axes[9];
  for (int i = 0; i < 3; ++i) {
    axes[i] = basis[i] * scale * kDomeHeight;
    axes[3 + i] = basis[3 + i] * a * kDomeHeight;
    axes[6 + i] = basis[6 + i] * a * kDomeHeight;
  }
  if (slot->frame != 0 && frame == slot->frame + 1) {
    float most = 0.0f;
    for (int row = 0; row < 3; ++row) {
      const float moved = Distance3(&axes[row * 3], &slot->axes[row * 3]);
      if (moved > most) most = moved;
    }
    if (most > g_trace_shape_most) g_trace_shape_most = most;
    g_trace_shape_sum += most;
    const float pivot = Distance3(place, slot->place);
    if (pivot > g_trace_place_most) g_trace_place_most = pivot;
    ++g_trace_pairs;
  }
  slot->frame = frame;
  memcpy(slot->place, place, sizeof(slot->place));
  memcpy(slot->axes, axes, sizeof(slot->axes));
}

void __fastcall h_MistDraw(void* self, void* unused) {
  if (TabletopHidesMist(self)) return;
  TestFrameCheck();
  if (g_head_free && self != nullptr) {
    const uint8_t* mist = static_cast<const uint8_t*>(self);
    const float* position =
        reinterpret_cast<const float*>(mist + kMistPosition);
    // The engine measures the flatten of a cloud from the stored camera.
    const float* stored = reinterpret_cast<const float*>(kStoredPosition);
    float table[3];
    const bool table_faced =
        g_clouds_head_free && HeadCameraTableCamera(table);
    const float* eye = table_faced ? table : stored;
    // Forward, from the camera to the cloud.
    float f[3] = {position[0] - eye[0], position[1] - eye[1],
                  position[2] - eye[2]};
    const float distance = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    // A cloud at the camera itself has no direction. It keeps the engine's
    // basis.
    if (distance > 1e-3f) {
      f[0] /= distance;
      f[1] /= distance;
      f[2] /= distance;
      // Right is world up crossed with forward, which is the engine's own
      // handedness.
      float r[3] = {f[2], 0.0f, -f[0]};
      if (r[0] * r[0] + r[2] * r[2] > 1e-6f) {
        Normalize(r);
      } else {
        OverheadRight(f, r);
      }
      const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2],
                          f[0] * r[1] - f[1] * r[0]};
      float basis[9] = {r[0], r[1], r[2], -f[0], -f[1], -f[2],
                        u[0], u[1], u[2]};
      const float scale = *reinterpret_cast<const float*>(mist + kMistScale);
      const bool sky_cloud = (mist[kMistFlags] & kMistSkyCloud) != 0;
      const float flatten =
          *reinterpret_cast<const float*>(mist + kMistFlatten);
      const float a_engine =
          sky_math::MistScale(scale, sky_cloud, flatten, position, stored);
      float a = a_engine;
      if (table_faced) {
        // The engine scales rows 1 and 2 by A from the stored camera, which
        // is the render head here. The ratio gives them A from the table
        // camera.
        const float a_table =
            sky_math::MistScale(scale, sky_cloud, flatten, position, eye);
        if (a_engine != 0.0f && a_table != 0.0f) {
          const float ratio = a_table / a_engine;
          for (int i = 3; i < 9; ++i) basis[i] *= ratio;
          a = a_table;
        }
        ++g_table_faced;
      }
      ++g_dome_draws;
      if (LimitPole(a, distance, basis)) ++g_poles_limited;
      memcpy(reinterpret_cast<void*>(kMistBasis), basis, sizeof(basis));
      if (g_trace) TraceDome(self, position, basis, scale, a_engine, eye);
    }
  }
  o_MistDraw(self, unused);
}

// The moon, in both of its calls. See THE MOON above.
void __cdecl h_MoonTurn(const float* place, float* turn, float* mesh) {
  TestFrameCheck();
  float* camera = reinterpret_cast<float*>(kCameraMatrix);
  float saved[12];
  float level[12];
  const bool swap =
      g_head_free && g_moon_world_up && sky_math::LevelCamera(camera, level);
  if (swap) {
    memcpy(saved, camera, sizeof(saved));
    memcpy(camera, level, sizeof(level));
    ++g_moon_level;
  }
  o_MoonTurn(place, turn, mesh);
  if (swap) memcpy(camera, saved, sizeof(saved));
  ++g_moon_turns;
  if (!g_trace || turn == nullptr) return;
  // The first call of a frame is the moon. The second is its reflection.
  const uint64_t frame = ControllersFrameCount();
  if (frame == g_moon_frame) return;
  float right[3] = {turn[0], turn[1], turn[2]};
  Normalize(right);
  const float tilt = asinf(fminf(1.0f, fabsf(right[1]))) * 57.29578f;
  if (tilt > g_moon_tilt_most) g_moon_tilt_most = tilt;
  if (g_moon_frame != 0 && frame == g_moon_frame + 1) {
    const float along = right[0] * g_moon_right[0] +
                        right[1] * g_moon_right[1] +
                        right[2] * g_moon_right[2];
    const float step = acosf(fmaxf(-1.0f, fminf(1.0f, along))) * 57.29578f;
    if (step > g_moon_step_most) g_moon_step_most = step;
  }
  g_moon_frame = frame;
  memcpy(g_moon_right, right, sizeof(g_moon_right));
}

// The glare runs once in each eye pass, as the first 3D draw after the
// sorted list. See THE SUN GLARE above.
//
// The overlay's meshes (overlay.cpp) and the glow at the Hand's point
// (hand_glow.cpp) come after it: the world is drawn, the depth buffer still
// holds it, and the interface is not drawn yet. The meshes go first, so the
// glow shows on the overlay's hand too.
void __cdecl h_SunGlare() {
  uint32_t* wanted = reinterpret_cast<uint32_t*>(kSunGlareWanted);
  ++g_glare_calls;
  if (*wanted != 0) ++g_glare_wanted;
  if (g_glare_mode == kGlareHidden) {
    // Clear the request as the glare does. Otherwise an old request draws a
    // glare later, in a pass that did not draw the disc.
    *wanted = 0;
  } else {
    if (g_glare_mode == kGlareEyeCamera) StereoReapplyEye();
    o_SunGlare();
  }
  OverlayDraw();
  HandGlowDraw();
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

const char* GlareModeText(int mode) {
  switch (mode) {
    case kGlareAsGame:
      return "drawn as the game draws it";
    case kGlareHidden:
      return "hidden";
    default:
      return "drawn through the eye camera";
  }
}

}  // namespace

void SkyDetailInstall() {
  if (g_hooked) return;
  g_hooked = true;
  Hook(kMistDraw, reinterpret_cast<void*>(&h_MistDraw),
       reinterpret_cast<void**>(&o_MistDraw), "LH3DMist::Draw");
  Hook(kSunGlare, reinterpret_cast<void*>(&h_SunGlare),
       reinterpret_cast<void**>(&o_SunGlare), "sun glare");
  Hook(kMoonTurn, reinterpret_cast<void*>(&h_MoonTurn),
       reinterpret_cast<void**>(&o_MoonTurn), "the turn of the moon");
}

// clouds_head_free, moon_world_up, sky_test_frame, sky_test_trace and
// sky_test_hour.
void SkySetHeadFree(bool clouds, bool moon, int test_frame, bool trace,
                    int test_hour) {
  g_test_hour = test_hour >= 0 && test_hour < 24 ? test_hour : -1;
  g_clouds_head_free = clouds;
  g_moon_world_up = moon;
  g_test_frame = test_frame > 0 ? static_cast<uint64_t>(test_frame) : 0;
  g_trace = trace;
  ModLog("sky          : clouds_head_free=%d. In the table and the God view "
         "the clouds face %s", clouds ? 1 : 0,
         clouds ? "the table camera, so a move of the head does not turn them"
                : "the render head, so they turn with each move of the head");
  ModLog("sky          : moon_world_up=%d. The moon turns with %s",
         moon ? 1 : 0,
         moon ? "world up, so it stays fixed in the sky"
              : "the camera up, so it turns with the head");
  if (g_test_frame != 0) {
    ModLog("sky          : sky_test_frame=%d changes both keys at that frame",
           test_frame);
  }
}

bool SkyHeadFree() { return g_head_free; }

// 0 draws the glare through the eye camera, 1 as the game draws it, and 2
// hides it. Other values give 0.
void SkySetGlareMode(int mode) {
  g_glare_mode = (mode >= 0 && mode < kGlareModes) ? mode : kGlareEyeCamera;
  ModLog("sky          : sun glare %s", GlareModeText(g_glare_mode));
}

// The longest pole a cloud may have, as a percent of its distance to the
// camera. 0 removes the limit. Other values are kept between 5 and 100.
void SkySetCloudPoleShare(int percent) {
  if (percent <= 0) {
    g_pole_share = 0.0f;
    ModLog("sky          : no limit on the pole of a cloud");
    return;
  }
  if (percent < 5) percent = 5;
  if (percent > 100) percent = 100;
  g_pole_share = static_cast<float>(percent) / 100.0f;
  ModLog("sky          : the pole of a cloud is at most %d percent of its "
         "distance", percent);
}

// Once per report window.
void SkyDetailReport() {
  if (!g_hooked) return;
  if (g_dome_draws > 0) {
    ModLog("sky          : the pole limit acted on %llu of %llu cloud and "
           "mist draws", g_poles_limited, g_dome_draws);
  }
  if (g_glare_wanted > 0) {
    ModLog("sky          : %llu of %llu passes asked for a sun glare, %s",
           g_glare_wanted, g_glare_calls, GlareModeText(g_glare_mode));
  }
  if (g_table_faced > 0) {
    ModLog("sky          : %llu of %llu cloud and mist draws faced the table "
           "camera", g_table_faced, g_dome_draws);
  }
  if (g_moon_turns > 0) {
    ModLog("sky          : the moon was turned %llu times, %llu of them with "
           "world up", g_moon_turns, g_moon_level);
  }
  if (g_trace && g_trace_pairs > 0) {
    ModLog("sky trace    : from one frame to the next an axis of a cloud "
           "moved at most %.2f units (mean %.3f), and a cloud at most %.2f "
           "units, in %llu pairs. The point that the clouds face moved at "
           "most %.2f units in a frame",
           g_trace_shape_most, g_trace_shape_sum / g_trace_pairs,
           g_trace_place_most, g_trace_pairs, g_trace_eye_most);
  }
  if (g_trace && g_moon_frame != 0) {
    ModLog("sky trace    : the right of the moon was at most %.2f degrees "
           "from level, and it turned at most %.3f degrees from one frame "
           "to the next", g_moon_tilt_most, g_moon_step_most);
  }
  g_dome_draws = 0;
  g_poles_limited = 0;
  g_glare_calls = 0;
  g_glare_wanted = 0;
  g_table_faced = 0;
  g_moon_turns = 0;
  g_moon_level = 0;
  g_trace_shape_most = 0.0f;
  g_trace_shape_sum = 0.0;
  g_trace_place_most = 0.0f;
  g_trace_pairs = 0;
  g_trace_eye_most = 0.0f;
  g_moon_tilt_most = 0.0f;
  g_moon_step_most = 0.0f;
}

// Moves every detail switch out by `factor`. 1 is the game's own distances.
void DetailDistanceSet(float factor) {
  float* distances = reinterpret_cast<float*>(kLodDistances);
  if (!g_lod_have_saved) {
    // Only values that look like the ones measured are touched. Anything else
    // means this is not the executable these addresses came from.
    for (int i = 0; i < kLodCount; ++i) {
      if (fabsf(distances[i] - kLodExpected[i]) > 0.01f) {
        ModLog("detail       : unexpected distances at 0x%08X (%.3f), left "
               "alone", kLodDistances + i * 4, distances[i]);
        return;
      }
      g_lod_saved[i] = distances[i];
    }
    g_lod_have_saved = true;
  }
  if (factor < 1.0f) factor = 1.0f;
  if (factor > 8.0f) factor = 8.0f;
  DWORD old = 0;
  if (!VirtualProtect(distances, sizeof(float) * kLodCount,
                      PAGE_READWRITE, &old)) {
    return;
  }
  for (int i = 0; i < kLodCount; ++i) distances[i] = g_lod_saved[i] * factor;
  VirtualProtect(distances, sizeof(float) * kLodCount, old, &old);
  g_lod_factor = factor;
  ModLog("detail       : models keep their detail %.0f times as far out",
         factor);
}

float DetailDistanceFactor() { return g_lod_factor; }

void SkyDetailRelease() {
  if (g_lod_have_saved) DetailDistanceSet(1.0f);
}
