// Three things about the world at a distance: clouds that stay still when the
// head moves, a sun glare that stays on the sun, and models that keep their
// detail further out.
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
// game camera's position (0x00EA1DB8, which head_camera.cpp keeps free of the
// head), with world up. Nothing about it depends on where the head points.
// The weather clouds and ground mist are LH3DMist too, and get the same.
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

extern void ModLog(const char* format, ...);
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

bool g_head_free = true;
bool g_hooked = false;
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
// change. `rise` is the height of the cloud above the camera. True when the
// limit made the pole shorter.
bool LimitPole(const uint8_t* mist, float rise, float distance,
               float basis[9]) {
  if (g_pole_share <= 0.0f) return false;
  // The length the engine is about to give the pole, worked out as it does.
  const float scale = *reinterpret_cast<const float*>(mist + kMistScale);
  float a = scale;
  if ((mist[kMistFlags] & kMistSkyCloud) != 0) {
    const float flatten = *reinterpret_cast<const float*>(mist + kMistFlatten);
    const float divisor =
        (flatten - 1.0f) * (1.0f - fabsf(rise) / distance) + 1.0f;
    // A divisor near zero is a dome the game itself draws wrong. Leave it.
    if (!(divisor > 1e-6f)) return false;
    a = scale / divisor;
  }
  const float pole = kDomeHeight * fabsf(a);
  const float most = g_pole_share * distance;
  if (!(pole > most)) return false;
  const float k = most / pole;
  basis[3] *= k;
  basis[4] *= k;
  basis[5] *= k;
  return true;
}

void __fastcall h_MistDraw(void* self, void* unused) {
  if (TabletopHidesMist(self)) return;
  if (g_head_free && self != nullptr) {
    const uint8_t* mist = static_cast<const uint8_t*>(self);
    const float* position =
        reinterpret_cast<const float*>(mist + kMistPosition);
    const float* eye = reinterpret_cast<const float*>(kStoredPosition);
    // Forward, from the camera to the cloud.
    float f[3] = {position[0] - eye[0], position[1] - eye[1],
                  position[2] - eye[2]};
    const float distance = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    // A cloud at the camera itself has no direction. It keeps the engine's
    // basis.
    if (distance > 1e-3f) {
      const float rise = f[1];
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
      ++g_dome_draws;
      if (LimitPole(mist, rise, distance, basis)) ++g_poles_limited;
      memcpy(reinterpret_cast<void*>(kMistBasis), basis, sizeof(basis));
    }
  }
  o_MistDraw(self, unused);
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
  g_dome_draws = 0;
  g_poles_limited = 0;
  g_glare_calls = 0;
  g_glare_wanted = 0;
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
