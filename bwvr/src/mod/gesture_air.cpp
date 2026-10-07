// Gestures in the air (step M6): hold the draw button and draw a miracle's
// gesture with the pointing hand. See docs/hand.md, section 5 and "M6 as
// built".
//
// HOW THE GAME READS A GESTURE. The game keeps a ring of the last 80 pointer
// samples in the GestureSystem (GGame+0x25006C). Every move of the pointer
// adds one, through fn_005CEAD0: the pixel, and the land under it. No button
// starts or ends a gesture. A still pointer adds the same pixel again, and 70
// of those clear the ring. So does any move of the game camera. Once a frame,
// GInterface::ProcessPowerUpSystem asks for the gestures that fit the moment
// (a miracle at one of the player's Worship Sites, the scribble that drops a
// miracle, the gestures of a miracle in the Hand) and matches the ring
// against the templates of Data\Gestures.jty. The match reads the turns and
// the directions of the path's corners, not its size. A match clears the
// ring, stops the samples for 0.4 s and starts Immersion effect 3, which
// controllers.cpp already turns into a pulse. The game then draws the
// gesture on the land from the samples' land points.
//
// WHAT THE MOD DOES. While the draw button is held, the grip point goes on a
// plane that the head faced when the draw started (controller_math.h,
// GesturePlane). The plane becomes pixels around the controller's pixel of
// that moment, at gesture_size_cm for a third of the render height. A new
// sample goes in when the pixel has moved a fortieth of the render height,
// about what a mouse gives in 28 ms, and a sharp corner between two samples
// goes in first (GestureCornerIndex). The samples go in through the game's
// own sampler, so the pause, the cooldown after a match and the land under
// the pixel all work as they do for the mouse. Over the sky, where the game
// would drop the first sample, the Hand's point stands in for the land. The
// game matches the ring itself, every frame, as with the mouse.
//
// While the controller owns the pointer, the game's own samples of the
// pointer are dropped. The Hand's pixel moves with the head and with the
// reach, and a wave of the hand must not cast a miracle. With the mouse as
// the owner, the game samples the pointer exactly as before.
//
// At the release the mod logs the path and the templates it fits, and clears
// the ring, so that an old path cannot match later.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "controller_math.h"
#include "controllers.h"
#include "haptics.h"

extern void ModLog(const char* format, ...);

// controllers.cpp
bool ControllersGestureInput(GestureInput* out);
bool ControllersOwnPointer();
void ControllersGesturePulse(float amplitude, float seconds);
int ControllersGestureSizeCm();

namespace {

const uintptr_t kGame = 0x00D0195C;
const uintptr_t kMyInterface = 0x00555850;
const int kGamePaused = 0x14;            // a byte: bit 2 while paused
const int kGameCamera = 0x2502C0;        // GCamera*, see GGame::GetCamera
const int kGameGestureList = 0x250064;   // GestureSystemDataList*
const int kGameGestureRing = 0x25006C;   // GestureSystem*
const int kCameraMoving = 0x74;          // GCamera::IsMoving reads it

// The GestureSystem: a ring of 80 samples of 0x28 bytes from +0x08. A sample
// holds the screen x at +0x00, the screen y at +0x08, the land point at
// +0x14 and the flags at +0x20 (1 start, 2 corner, 4 and 8 the end).
const int kRingSamples = 0x08;
const int kRingSampleSize = 0x28;
const int kRingSize = 80;
const int kRingCount = 0xC88;    // a byte
const int kRingRepeats = 0xC8C;  // an int
const int kRingHead = 0xC90;     // a byte
const int kSampleFlags = 0x20;

// The GestureSystemDataList: the templates at +0x08, their count at +0x0C.
// A template is 0x65C bytes, with the corner count at +0x648 and the gesture
// type at +0x649.
const int kListTemplates = 0x08;
const int kListCount = 0x0C;
const int kTemplateSize = 0x65C;
const int kTemplateCorners = 0x648;
const int kTemplateType = 0x649;
// A GestureSystemData is a template with a vtable. The mod builds its own for
// the test at the release, so the game's own stays as it was.
const uintptr_t kPathVTable = 0x008DF7E0;
// The GestureSystemResult: the type at +0x00, the template at +0x0A.
const int kResultTemplate = 0x0A;

// GInterface: no samples while this timer (seconds) is above 0.
const int kFaceCooldown = 0x378;
const int kFaceHand = 0x3A0;      // the CHand
const int kHandPosition = 0x78;   // its world position

// fn_005CEAD0, thiscall on GInterface (const int32_t* pixel), ret 4: one
// sample of a pixel, with the land under it.
const uintptr_t kSampler = 0x005CEAD0;
// GInterface::ProcessPowerUpSystem, thiscall, no arguments, plain ret.
const uintptr_t kPowerUpSystem = 0x005CF300;
// fn_00579F10, thiscall on the list (unsigned type, path, result), ret 0xC:
// 1 when a template of that type fits the path.
const uintptr_t kMatchType = 0x00579F10;
// 0x00579F70, thiscall on the list (int index, path, result), ret 0xC: 1
// when that one template fits the path.
const uintptr_t kMatchIndex = 0x00579F70;
// fn_00578C20, thiscall on a GestureSystemData (GestureSystem*), ret 4: the
// corners of the ring into the path.
const uintptr_t kBuildPath = 0x00578C20;
// fn_0057BBC0, thiscall on the ring (const float* land, const int32_t*
// pixel), ret 8: one sample with its land point.
const uintptr_t kRingAdd = 0x0057BBC0;

typedef void(__fastcall* SamplerFn)(void* face, void* unused,
                                    const int32_t* pixel);
typedef void(__fastcall* PowerUpFn)(void* face, void* unused);
typedef int(__fastcall* MatchTypeFn)(void* list, void* unused, uint32_t type,
                                     void* path, uint8_t* result);
typedef int(__fastcall* MatchIndexFn)(void* list, void* unused, int index,
                                      void* path, uint8_t* result);
typedef void(__fastcall* BuildPathFn)(void* path, void* unused, void* ring);
typedef void(__fastcall* RingAddFn)(void* ring, void* unused, const float* land,
                                    const int32_t* pixel);
typedef void*(__fastcall* MyInterfaceFn)(void* game, void* unused);

SamplerFn o_Sampler = nullptr;
PowerUpFn o_PowerUpSystem = nullptr;
MatchTypeFn o_MatchType = nullptr;

// The names of the gesture types, as the game's own table at 0x00BEF308.
const int kTypeCount = 24;
const char* const kTypeName[kTypeCount] = {
    "none",       "spiral",        "inverse spiral", "S shape",
    "circle",     "scribble",      "three",          "vertical",
    "star",       "fork right",    "fork up",        "fork left",
    "fork down",  "heart",         "R shape",        "square spiral",
    "Cyrillic L", "E shape",       "reverse S",      "infinity",
    "W shape",    "house",         "inverse square spiral",
    "square wave"};

const char* TypeName(int type) {
  return type >= 0 && type < kTypeCount ? kTypeName[type] : "unknown";
}

bool g_installed = false;
bool g_feeding = false;  // the mod's own call of the sampler

// THE STROKE. Begins on the first frame the button may draw, and ends on the
// first frame it may not.
bool g_active = false;
controller_math::GesturePlane g_plane;
float g_start_x = 0.0f, g_start_y = 0.0f;
float g_pixels_per_unit = 0.0f;
float g_spacing = 0.0f;
float g_scale = 7.5f;          // game units per meter
bool g_have_last = false;
float g_last_x = 0.0f, g_last_y = 0.0f;
// The pixels of the frames since the last sample, oldest first, so that a
// corner between two samples is not cut (GestureCornerIndex).
const int kMaxPending = 96;
float g_pending[kMaxPending][2];
int g_pending_count = 0;
float g_tolerance = 9.0f;      // pixels off the line that make a corner
int g_corners_kept = 0;
int g_skyward = 0;             // samples with no land, given the Hand's point
int g_expected = 0;            // the ring count after the mod's last sample
uint64_t g_frame = ~0ull;      // the controllers frame of the last step
uint64_t g_begin_frame = 0;
int g_fed = 0;                 // samples the game took
int g_refused = 0;             // samples the game did not take
float g_path_px = 0.0f;
int g_cleared = 0;             // the game cleared the ring during the stroke
int g_cleared_moving = 0;      // and the camera was moving then
uint32_t g_tested = 0;         // gesture types the game asked for
int g_game_matches = 0;
int g_after = 0;               // frames after the end that still log matches

// The report window.
uint64_t g_strokes = 0;
uint64_t g_samples = 0;
uint64_t g_dropped = 0;        // the game's samples of the pointer, dropped
uint64_t g_matches = 0;
int g_logs = 0;
const int kMaxLogs = 120;

bool LogRoom() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

uint8_t* Game() {
  uint8_t* const* game = reinterpret_cast<uint8_t* const*>(kGame);
  if (IsBadReadPtr(game, sizeof(void*))) return nullptr;
  return *game;
}

uint8_t* Pointer(uint8_t* base, int offset) {
  if (base == nullptr) return nullptr;
  uint8_t* const* at = reinterpret_cast<uint8_t* const*>(base + offset);
  if (IsBadReadPtr(at, sizeof(void*))) return nullptr;
  return *at;
}

uint8_t* Interface() {
  uint8_t* game = Game();
  if (game == nullptr) return nullptr;
  const MyInterfaceFn my_interface =
      reinterpret_cast<MyInterfaceFn>(kMyInterface);
  return static_cast<uint8_t*>(my_interface(game, nullptr));
}

uint8_t* Ring() {
  uint8_t* ring = Pointer(Game(), kGameGestureRing);
  if (ring == nullptr || IsBadReadPtr(ring, kRingHead + 1)) return nullptr;
  return ring;
}

int RingCount(const uint8_t* ring) { return ring[kRingCount]; }

// The game's own clear of the ring (fn_005CE420 and others).
void ClearRing(uint8_t* ring) {
  ring[kRingCount] = 0;
  ring[kRingHead] = 0;
  *reinterpret_cast<int32_t*>(ring + kRingRepeats) = 0;
  memset(ring + kRingSamples, 0, kRingSize * kRingSampleSize);
}

// The samples the game marked as corners, the start and the end: the ones
// the match reads.
int RingCorners(const uint8_t* ring) {
  int corners = 0;
  for (int i = 0; i < kRingSize; ++i) {
    const int32_t flags = *reinterpret_cast<const int32_t*>(
        ring + kRingSamples + i * kRingSampleSize + kSampleFlags);
    if ((flags & 0xB) != 0) ++corners;
  }
  return corners;
}

bool Paused() {
  const uint8_t* game = Game();
  return game != nullptr && (game[kGamePaused] & 4) != 0;
}

// The Hand's world position, when it is not zero.
const float* HandPoint(uint8_t* face) {
  const uint8_t* hand = Pointer(face, kFaceHand);
  if (hand == nullptr || IsBadReadPtr(hand + kHandPosition, 12)) {
    return nullptr;
  }
  const float* at = reinterpret_cast<const float*>(hand + kHandPosition);
  if (!(fabsf(at[0]) > 1e-3f || fabsf(at[2]) > 1e-3f)) return nullptr;
  return at;
}

bool CameraMoving() {
  const uint8_t* camera = Pointer(Game(), kGameCamera);
  if (camera == nullptr || IsBadReadPtr(camera, kCameraMoving + 4)) {
    return false;
  }
  return *reinterpret_cast<const int32_t*>(camera + kCameraMoving) != 0;
}

// Drop the game's own samples of the pointer while the controller owns it.
// The mod's samples pass.
void __fastcall h_Sampler(void* face, void* unused, const int32_t* pixel) {
  if (!g_feeding && ControllersOwnPointer()) {
    ++g_dropped;
    return;
  }
  o_Sampler(face, unused, pixel);
}

// THE TEST AT THE RELEASE. The game's own match, one template at a time, on
// a copy of the path. The game only asks for the gestures that fit the
// moment, so this says what the path looks like to the game even when no
// miracle can be cast. It writes only to the mod's own path and result.
struct Fit {
  int templates;     // templates in the list
  int path_corners;  // corners the game reads in the ring
  int fits;          // templates that fit
  uint32_t types;    // their gesture types, one bit each
  int best;          // the fitting template with the most corners
  int best_type;
  char corners[640]; // the path's corners: x, y, turn and direction
};

bool Test(uint8_t* ring, Fit* fit) {
  memset(fit, 0, sizeof(*fit));
  fit->best = -1;
  uint8_t* list = Pointer(Game(), kGameGestureList);
  if (list == nullptr || IsBadReadPtr(list, kListCount + 4)) return false;
  const int count = *reinterpret_cast<const int32_t*>(list + kListCount);
  uint8_t* templates = Pointer(list, kListTemplates);
  if (templates == nullptr || count <= 0 || count > 1000 ||
      IsBadReadPtr(templates, count * kTemplateSize)) {
    return false;
  }
  static uint32_t path_words[(kTemplateSize + 3) / 4];
  uint8_t* path = reinterpret_cast<uint8_t*>(path_words);
  memset(path, 0, kTemplateSize);
  *reinterpret_cast<uintptr_t*>(path) = kPathVTable;
  uint8_t result[16] = {0};
  reinterpret_cast<BuildPathFn>(kBuildPath)(path, nullptr, ring);
  fit->templates = count;
  fit->path_corners = path[kTemplateCorners];
  // The corners as the game reads them: a sample is five floats from +0x08,
  // x, a pad, y, the turn and the direction (an int).
  size_t used = 0;
  for (int i = 0; i < fit->path_corners && i < 24; ++i) {
    const float* c = reinterpret_cast<const float*>(path + 8 + i * 20);
    const int n = _snprintf_s(
        fit->corners + used, sizeof(fit->corners) - used, _TRUNCATE,
        "%s%.0f,%.0f %.2f %d", i > 0 ? " | " : "", c[0], c[2], c[3],
        *reinterpret_cast<const int32_t*>(c + 4));
    if (n < 0) break;
    used += static_cast<size_t>(n);
  }
  const MatchIndexFn match = reinterpret_cast<MatchIndexFn>(kMatchIndex);
  int best_corners = -1;
  for (int i = 0; i < count; ++i) {
    if (match(list, nullptr, i, path, result) == 0) continue;
    const uint8_t* t = templates + i * kTemplateSize;
    const int type = t[kTemplateType];
    ++fit->fits;
    if (type < 32) fit->types |= 1u << type;
    if (t[kTemplateCorners] > best_corners) {
      best_corners = t[kTemplateCorners];
      fit->best = i;
      fit->best_type = type;
    }
  }
  return true;
}

void TypeList(uint32_t types, char* out, size_t size) {
  out[0] = 0;
  size_t used = 0;
  for (int type = 0; type < 32; ++type) {
    if ((types & (1u << type)) == 0) continue;
    const int n = _snprintf_s(out + used, size - used, _TRUNCATE, "%s%s",
                              used > 0 ? ", " : "", TypeName(type));
    if (n < 0) break;
    used += static_cast<size_t>(n);
  }
  if (used == 0) _snprintf_s(out, size, _TRUNCATE, "none");
}

// One sample through the game's own sampler. False when the game did not
// take it.
bool Add(uint8_t* face, uint8_t* ring, float px, float py) {
  const int32_t pixel[2] = {static_cast<int32_t>(px),
                            static_cast<int32_t>(py)};
  const uint8_t head = ring[kRingHead];
  const uint8_t count = ring[kRingCount];
  g_feeding = true;
  o_Sampler(face, nullptr, pixel);
  g_feeding = false;
  if (ring[kRingHead] == head && ring[kRingCount] == count &&
      !Paused() && *reinterpret_cast<const float*>(face + kFaceCooldown) <= 0.0f) {
    // The ring is empty and the pixel has no land under it, for example over
    // the sky. The game would drop it, and the stroke would lose its start.
    // The land point only places the game's picture of a match and a sound,
    // so the Hand's own point stands in for it.
    const float* hand = HandPoint(face);
    if (hand != nullptr) {
      reinterpret_cast<RingAddFn>(kRingAdd)(ring, nullptr, hand, pixel);
      ++g_skyward;
    }
  }
  g_expected = RingCount(ring);
  if (ring[kRingHead] == head && ring[kRingCount] == count) {
    // The game is paused, or it matched a gesture less than 0.4 s ago.
    ++g_refused;
    return false;
  }
  if (g_have_last) {
    const float dx = px - g_last_x, dy = py - g_last_y;
    g_path_px += sqrtf(dx * dx + dy * dy);
  }
  g_last_x = px;
  g_last_y = py;
  g_have_last = true;
  ++g_fed;
  ++g_samples;
  return true;
}

// The pixel of this frame. A sample goes in when it is a spacing from the
// last one, and a corner between the two goes in first.
void Feed(uint8_t* face, uint8_t* ring, const GestureInput& in) {
  float x, y;
  controller_math::GesturePixel(g_plane, in.grip_room, g_start_x, g_start_y,
                                g_pixels_per_unit, &x, &y);
  // A grip far off the plane's scale is no gesture. It must not wrap the
  // game's integer pixel.
  if (!(fabsf(x) < 100000.0f && fabsf(y) < 100000.0f)) return;
  const float px = floorf(x + 0.5f);
  const float py = floorf(y + 0.5f);
  if (!g_have_last) {
    g_pending_count = 0;
    Add(face, ring, px, py);
    return;
  }
  if (g_pending_count == kMaxPending) {
    // A slow hand: keep every second pixel.
    for (int i = 0; i < kMaxPending / 2; ++i) {
      g_pending[i][0] = g_pending[i * 2 + 1][0];
      g_pending[i][1] = g_pending[i * 2 + 1][1];
    }
    g_pending_count = kMaxPending / 2;
  }
  g_pending[g_pending_count][0] = px;
  g_pending[g_pending_count][1] = py;
  ++g_pending_count;
  for (int round = 0; round < 8; ++round) {
    if (!controller_math::GestureStepFar(g_last_x, g_last_y, px, py,
                                         g_spacing)) {
      return;
    }
    // The pixels before this frame's, and the farthest of them from the
    // line.
    const int corner = controller_math::GestureCornerIndex(
        g_pending, g_pending_count - 1, g_last_x, g_last_y, px, py,
        g_tolerance);
    if (corner < 0) {
      if (Add(face, ring, px, py)) g_pending_count = 0;
      return;
    }
    if (!Add(face, ring, g_pending[corner][0], g_pending[corner][1])) return;
    ++g_corners_kept;
    const int rest = g_pending_count - (corner + 1);
    memmove(g_pending, g_pending + corner + 1, sizeof(g_pending[0]) * rest);
    g_pending_count = rest;
  }
}

void Begin(uint8_t* face, uint8_t* ring, const GestureInput& in) {
  controller_math::GesturePlaneFromHead(in.head, in.grip_room, &g_plane);
  g_scale = in.scale > 0.0f ? in.scale : 7.5f;
  g_pixels_per_unit = controller_math::GesturePixelsPerUnit(
      in.render_height, ControllersGestureSizeCm() * 0.01f, g_scale);
  g_spacing = in.render_height / 40.0f;
  if (g_spacing < 6.0f) g_spacing = 6.0f;
  g_tolerance = g_spacing / 4.0f;
  g_pending_count = 0;
  g_corners_kept = 0;
  g_skyward = 0;
  g_start_x = in.have_pixel ? in.pixel_x : in.render_width * 0.5f;
  g_start_y = in.have_pixel ? in.pixel_y : in.render_height * 0.5f;
  g_active = true;
  g_begin_frame = in.frame;
  g_have_last = false;
  g_fed = g_refused = g_cleared = g_cleared_moving = g_game_matches = 0;
  g_path_px = 0.0f;
  g_tested = 0;
  g_after = 0;
  ++g_strokes;
  // A new path: nothing before the press takes part in it.
  ClearRing(ring);
  g_expected = 0;
  HapticsPulse(haptics_math::kTick);
  if (LogRoom()) {
    ModLog("gesture      : the draw starts at frame %llu, at pixel %.0f %.0f "
           "of %.0f x %.0f. The plane faces the head: right %.2f %.2f %.2f, "
           "up %.2f %.2f %.2f. %.1f pixels per cm of hand, a sample every "
           "%.0f pixels. The camera is %s.", in.frame, g_start_x, g_start_y,
           in.render_width, in.render_height, g_plane.right[0],
           g_plane.right[1], g_plane.right[2], g_plane.up[0], g_plane.up[1],
           g_plane.up[2], g_pixels_per_unit * g_scale * 0.01f, g_spacing,
           CameraMoving() ? "moving, so the game clears the ring" : "still");
  }
  Feed(face, ring, in);
}

void End(uint8_t* ring, const GestureInput& in) {
  g_active = false;
  g_after = 30;
  Fit fit;
  const bool tested = Test(ring, &fit);
  const float px_per_cm = g_pixels_per_unit * g_scale * 0.01f;
  if (LogRoom()) {
    char asked[256], fits[512];
    TypeList(g_tested, asked, sizeof(asked));
    TypeList(fit.types, fits, sizeof(fits));
    ModLog("gesture      : the draw ends at frame %llu (%s) after %llu "
           "frames: %d samples fed (%d kept a corner, %d with no land), %d "
           "refused, a path of %.0f pixels (%.1f cm of hand). The ring holds "
           "%d samples, %d of them corners.",
           g_frame, in.why != nullptr ? in.why : "no controller data",
           g_frame - g_begin_frame, g_fed, g_corners_kept, g_skyward,
           g_refused, g_path_px,
           px_per_cm > 0.0f ? g_path_px / px_per_cm : 0.0f, RingCount(ring),
           RingCorners(ring));
    if (g_fed > kRingSize) {
      ModLog("gesture      : the path had more than %d samples, so the ring "
             "lost its start. Draw it smaller, or set a larger "
             "gesture_size_cm.", kRingSize);
    }
    ModLog("gesture      : the game asked for: %s. It matched %d time%s. It "
           "cleared the ring %d time%s during the draw (%d with the camera "
           "moving).", asked, g_game_matches, g_game_matches == 1 ? "" : "s",
           g_cleared, g_cleared == 1 ? "" : "s", g_cleared_moving);
    if (tested && fit.path_corners > 0) {
      ModLog("gesture      : its corners (x,y turn direction): %s",
             fit.corners);
    }
    if (!tested) {
      ModLog("gesture      : the templates are not loaded, so the path was "
             "not tested");
    } else if (fit.fits == 0) {
      ModLog("gesture      : the path (%d corners) fits none of the %d "
             "templates", fit.path_corners, fit.templates);
    } else {
      ModLog("gesture      : the path (%d corners) fits %d of the %d "
             "templates: %s. The best is template %d, %s.", fit.path_corners,
             fit.fits, fit.templates, fits, fit.best, TypeName(fit.best_type));
    }
  }
  // An old path must not match later, when a miracle becomes possible.
  ClearRing(ring);
  g_expected = 0;
}

// Once a controllers frame, before the game matches the ring.
void Step(uint8_t* face) {
  if (face == nullptr || face != Interface()) return;
  GestureInput in;
  const bool have = ControllersGestureInput(&in);
  if (have && in.frame == g_frame) return;
  if (have) g_frame = in.frame;
  if (g_after > 0) --g_after;
  uint8_t* ring = Ring();
  if (ring == nullptr) return;
  if (g_active && RingCount(ring) < g_expected) {
    // The game cleared the ring since the last sample: a match, a camera
    // move, a pick up or a drop.
    ++g_cleared;
    if (CameraMoving()) ++g_cleared_moving;
    g_have_last = false;
    g_pending_count = 0;
    g_expected = RingCount(ring);
  }
  const bool held = have && in.held && in.scale > 0.0f &&
                    in.render_height > 0.0f;
  if (!g_active && held) {
    Begin(face, ring, in);
  } else if (g_active && !held) {
    End(ring, in);
  } else if (g_active) {
    Feed(face, ring, in);
  }
}

void __fastcall h_PowerUpSystem(void* face, void* unused) {
  Step(static_cast<uint8_t*>(face));
  o_PowerUpSystem(face, unused);
}

// Every match the game asks for. Logs the ones of a draw.
int __fastcall h_MatchType(void* list, void* unused, uint32_t type,
                           void* path, uint8_t* result) {
  const int matched = o_MatchType(list, unused, type, path, result);
  if (!g_active && g_after <= 0) return matched;
  if (type < 32) g_tested |= 1u << type;
  if (matched != 0) {
    ++g_game_matches;
    ++g_matches;
    if (LogRoom()) {
      ModLog("gesture      : the game matched a %s (template %d) at frame "
             "%llu", TypeName(static_cast<int>(type)),
             result != nullptr ? result[kResultTemplate] : -1, g_frame);
    }
  }
  return matched;
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

// Only with the controllers on, so with them off the game runs exactly as
// before.
void GestureAirInstall() {
  if (g_installed) return;
  const bool a = Hook(kSampler, reinterpret_cast<void*>(&h_Sampler),
                      reinterpret_cast<void**>(&o_Sampler),
                      "the gesture sampler fn_005CEAD0");
  const bool b = Hook(kPowerUpSystem, reinterpret_cast<void*>(&h_PowerUpSystem),
                      reinterpret_cast<void**>(&o_PowerUpSystem),
                      "GInterface::ProcessPowerUpSystem");
  g_installed = a && b;
  if (!g_installed) {
    // Without both, the draw cannot feed the ring. Leave the game's own.
    if (a) MH_DisableHook(reinterpret_cast<void*>(kSampler));
    if (b) MH_DisableHook(reinterpret_cast<void*>(kPowerUpSystem));
    return;
  }
  // The log of the game's matches stands apart. The draw works without it.
  Hook(kMatchType, reinterpret_cast<void*>(&h_MatchType),
       reinterpret_cast<void**>(&o_MatchType), "the gesture match fn_00579F10");
}

// Once a report window.
void GestureAirReport() {
  if (!g_installed) return;
  if (g_strokes == 0 && g_samples == 0 && g_dropped == 0 && g_matches == 0) {
    return;
  }
  ModLog("gesture      : %llu draws, %llu samples fed, %llu game matches, "
         "%llu samples of the pointer dropped while the controller owned it",
         g_strokes, g_samples, g_matches, g_dropped);
  g_strokes = g_samples = g_matches = g_dropped = 0;
}
