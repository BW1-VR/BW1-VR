// The glow on the mesh of a target of the Hand. See docs/hand-model.md, "The
// glow on the mesh of a target".
//
// WHY. A halo (hand_glow.cpp) is a flat disc around a point of the object.
// The land cuts it, and the point is the base of the object and not the
// middle of its mesh. Light on the mesh itself has the outline of the object
// and its depth.
//
// HOW. Each 3D object of the game has a color that Direct3D adds over the
// texture: the specular color at +0x50 (the diffuse color is at +0x4C). The
// game uses it for haze, poison and fire, in the form 0xFF000000 and a
// saturating add. The vertex builders copy it into each vertex of the mesh
// (fn_0084A6D0 at 0x0084A77A, and the lit path fn_0084BA90 at 0x0084BEEC).
//
//  1. Once a frame, before the eye passes, TargetGlowFrame takes the targets
//     of the frame from hand_glow.cpp, with the color of the halo of each.
//     It takes the time, and it makes the fade and the pulse of each target.
//     Both eye passes use these numbers, so the eyes agree.
//  2. Each draw of an object of the game goes through
//     Game3DObject::AddForDrawing (0x0063B5D0, a fastcall with the 3D object
//     in ecx and the object of the game in edx). For a target, the hook
//     writes the mark of the target into +0x50 before the game draws: the
//     glow with no wave, with the number of the slot in the low bits of blue
//     (target_glow_math.h).
//  3. The vertices come to LH3DRender::DrawTriangle with that color
//     (TargetGlowBatch). A batch inside the call of a target is a batch of
//     that target. After the call (a mesh of the sorted list), a batch whose
//     first vertex has a mark is a batch of that target. Each vertex that a
//     triangle uses and that has the mark gets the wave: more or less of the
//     glow, from the height of the vertex in the world. The right eye of one
//     walk is a copy of these vertices with their colors.
//  4. After the scene walk of the eye pass, TargetGlowPassDone puts the
//     color of each 3D object back. An object of the sorted list draws after
//     its AddForDrawing, so the color stays to the end of the walk.
//
// A target whose mesh did not come to the draw in the frame keeps its halo:
// a class that does not go through the hook (the Creature, a building in
// work) and an object that the game draws as a picture far away.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "MinHook.h"

#include "../common/bw_addresses.h"
#include "target_glow.h"
#include "target_glow_math.h"

extern void ModLog(const char* format, ...);

// stereo.cpp: the eye of the pass now, or -1 outside an eye pass.
int StereoPassEye();
// controllers.cpp: the frame, the frame of a test key, and a pulse of the
// pointing controller.
uint64_t ControllersFrameCount();
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);
void ControllersGesturePulse(float amplitude, float seconds);

namespace {

using namespace target_glow_math;

const uintptr_t kAddForDrawing = 0x0063B5D0;
// push ebx, push ebp, push esi, mov esi, ecx.
const uint8_t kAddForDrawingBytes[5] = {0x53, 0x55, 0x56, 0x8B, 0xF1};
const size_t kModelSpecular = 0x50;

// The function ends with a plain ret. The hook gives eax back as it came.
typedef uintptr_t(__fastcall* AddForDrawingFn)(void* model, void* object);
AddForDrawingFn o_AddForDrawing = nullptr;

// A vertex of the game's draws (FVF 0x1C4).
struct Vertex {
  float x, y, z, rhw;
  uint32_t diffuse, specular;
  float u, v;
};
static_assert(sizeof(Vertex) == 32, "the game's vertex is 32 bytes");

bool g_installed = false;
int g_mode = kModeMesh;
int g_pct = kPctDefault;
int g_anim = kAnimBoth;
bool g_test_keys = false;
bool g_key_down = false;

// The test keys of bwvr.ini for the desktop runs. target_glow_test_keys: up
// to 8 frames at which the test key acts. target_glow_log=N: one line for
// each target each N frames.
const int kMaxTestFrames = 8;
uint64_t g_test_frames[kMaxTestFrames] = {};
int g_test_frame_count = 0;
int g_log_every = 0;

// A TARGET. object is the object of the game, and null for a free slot.
struct Slot {
  void* object;
  const char* name;
  bool wanted;       // a target at the start of this frame
  float color[3];    // the color now, 0 to 255
  float want[3];     // the color of the halo
  float level;       // the fade, 0 to 1
  float strength;    // the key, the fade and the pulse of this frame
  int add[3];        // the glow of this frame with no wave
  float base_y;      // the mesh in the frame before: its lowest vertex in the
  float height;      // world and its height, for the wave
  // What this frame saw.
  bool seen;         // the hook saw the object
  uint32_t models;   // 3D objects that got the mark
  uint32_t vertices; // vertices with the mark that went to the draw
  uint32_t clipped;  // and those that went through the clip of the engine
  float min_y, max_y;
  float box[4];      // on the screen of the left eye: left, top, right, bottom
  uint32_t dark;     // vertices that the device drew with the specular
                     // color off
  // For the log.
  uint64_t frames;   // frames as a target
  int said;          // 0 nothing, 1 "glows", 2 "keeps its halo"
  int batch_lines;   // lines of the test key about its batches
};
Slot g_slots[kSlots] = {};
int g_live = 0;

// A 3D object that has the mark now, in this eye pass.
struct Written {
  uint8_t* model;
  uint32_t base;   // the color of the game
  uint32_t mark;
  int slot;
};
const int kMaxWritten = 24;
Written g_written[kMaxWritten];
int g_written_count = 0;

// The time, one value for the frame.
LARGE_INTEGER g_frequency = {};
LARGE_INTEGER g_start = {};
bool g_have_time = false;
double g_now = 0.0;
uint64_t g_frame = 0;

// For the log and the report.
int g_log_lines = 0;
const int kMaxLogLines = 120;
uint64_t g_frames_glow = 0;
uint64_t g_frames_halo = 0;
uint64_t g_vertices = 0;
uint64_t g_clipped = 0;
uint64_t g_targets = 0;
uint64_t g_late = 0;
uint64_t g_full = 0;
uint64_t g_outside = 0;

bool Log() {
  if (g_log_every > 0) return true;
  if (g_log_lines >= kMaxLogLines) return false;
  ++g_log_lines;
  return true;
}

int IniKey(const char* key, int fallback, char* text, size_t size) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return fallback;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return fallback;
  slash[1] = 0;
  if (strlen(path) + 9 >= MAX_PATH) return fallback;
  strcat_s(path, "bwvr.ini");
  if (text != nullptr) {
    GetPrivateProfileStringA("bwvr", key, "", text, static_cast<DWORD>(size),
                             path);
    return 0;
  }
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

const char* ModeWords(int mode) {
  switch (mode) {
    case kModeHalo:
      return "a halo marks each target";
    case kModeBoth:
      return "the mesh of a target glows, and the halo shows too";
    default:
      return "the mesh of a target glows, and a halo shows only for a target "
             "whose mesh does not glow";
  }
}

const char* AnimWords(int anim) {
  switch (anim) {
    case kAnimSteady:
      return "steady";
    case kAnimPulse:
      return "a pulse";
    case kAnimWave:
      return "a band of light that goes up the mesh";
    default:
      return "a pulse and a band of light that goes up the mesh";
  }
}

Slot* Find(const void* object) {
  if (object == nullptr) return nullptr;
  for (int i = 0; i < kSlots; ++i) {
    if (g_slots[i].object == object) return &g_slots[i];
  }
  return nullptr;
}

void NoBox(Slot* s) {
  s->min_y = 1e30f;
  s->max_y = -1e30f;
  s->box[0] = s->box[1] = 1e30f;
  s->box[2] = s->box[3] = -1e30f;
}

void Clear(Slot* s) {
  memset(s, 0, sizeof(*s));
  NoBox(s);
}

// A free slot, or the slot of the object that fades out with the least
// light.
Slot* Free() {
  Slot* least = nullptr;
  for (int i = 0; i < kSlots; ++i) {
    Slot& s = g_slots[i];
    if (s.object == nullptr) return &s;
    if (!s.wanted && (least == nullptr || s.level < least->level)) least = &s;
  }
  return least;
}

// The game can change the color in its own draw. Then its value stays.
void PutBack(const Written& w) {
  uint32_t* color = reinterpret_cast<uint32_t*>(w.model + kModelSpecular);
  if (*color == w.mark) *color = w.base;
}

// The mark of the slot goes into the 3D object before the game draws it.
// The answer is the place of the 3D object in g_written, or -1 when it has
// no mark.
int Write(int index, void* model) {
  Slot& s = g_slots[index];
  s.seen = true;
  // Only an eye pass puts the color back.
  if (StereoPassEye() < 0) {
    ++g_outside;
    return -1;
  }
  if ((s.add[0] | s.add[1] | s.add[2]) == 0) return -1;
  uint8_t* bytes = static_cast<uint8_t*>(model);
  for (int i = 0; i < g_written_count; ++i) {
    if (g_written[i].model == bytes) return i;
  }
  if (g_written_count >= kMaxWritten) {
    ++g_full;
    return -1;
  }
  uint32_t* color = reinterpret_cast<uint32_t*>(bytes + kModelSpecular);
  const uint32_t base = *color;
  const uint32_t mark =
      Mark(AddColor(base, s.add[0], s.add[1], s.add[2]), index);
  if (SameRgb(mark, base)) return -1;
  *color = mark;
  Written& w = g_written[g_written_count];
  w.model = bytes;
  w.base = base;
  w.mark = mark;
  w.slot = index;
  ++s.models;
  return g_written_count++;
}

// THE CALL OF A TARGET. A mesh that the game draws at once goes to the draw
// inside this call, so each batch in the call is a batch of this 3D object.
// g_call is its place in g_written, and -1 outside such a call.
int g_call = -1;

uintptr_t __fastcall h_AddForDrawing(void* model, void* object) {
  if (g_live == 0 || object == nullptr || model == nullptr) {
    return o_AddForDrawing(model, object);
  }
  int call = -1;
  for (int i = 0; i < kSlots; ++i) {
    if (g_slots[i].object == object) {
      call = Write(i, model);
      break;
    }
  }
  const int outer = g_call;
  g_call = call;
  const uintptr_t result = o_AddForDrawing(model, object);
  g_call = outer;
  return result;
}

// What a vertex of a batch is: 0 is not looked at yet.
unsigned char g_vertex_done[65536];

// The vertices of one target in a batch. Each one gets the wave from its
// height in the world.
//
// ONLY A VERTEX THAT A TRIANGLE USES COUNTS. The game keeps vertices in the
// array that it did not project: such a vertex has its camera space values,
// with its depth in the place of rhw, and no triangle of the batch uses it
// (cutscene_window_math.h, GridBatch). It has the color of the object too.
void Paint(const Written& w, Vertex* v, uint32_t count, const uint16_t* idx,
           uint32_t indices, bool clipped, uint32_t caller) {
  Slot& s = g_slots[w.slot];
  const bw::tech::InfoTransform* info =
      reinterpret_cast<const bw::tech::InfoTransform*>(
          bw::tech::kInfoTransform);
  const HeightMap map = HeightFromScreen(
      reinterpret_cast<const float*>(bw::tech::kWorldToCamera),
      info->half_res_x, info->half_res_y,
      *reinterpret_cast<const float*>(bw::tech::kNearPlane));
  const bool wave = HasWave(g_anim) && s.height > 0.0f;
  if (count > 65536) count = 65536;
  memset(g_vertex_done, 0, count);
  uint32_t n = 0, faded = 0;
  float low = s.min_y, high = s.max_y;
  // The place on the screen is for the log: a test finds the target in the
  // picture of the left eye with it.
  const bool box = StereoPassEye() == 0;
  for (uint32_t j = 0; j < indices; ++j) {
    const uint32_t i = idx[j];
    if (i >= count || g_vertex_done[i] != 0) continue;
    g_vertex_done[i] = 1;
    const uint32_t was = v[i].specular;
    if (!HasMark(was, w.mark)) continue;
    ++n;
    const uint32_t alpha = was >> 24;
    if (alpha != 0xFFu) ++faded;
    if (box) {
      if (v[i].x < s.box[0]) s.box[0] = v[i].x;
      if (v[i].y < s.box[1]) s.box[1] = v[i].y;
      if (v[i].x > s.box[2]) s.box[2] = v[i].x;
      if (v[i].y > s.box[3]) s.box[3] = v[i].y;
    }
    float y = 0.0f;
    // A vertex with no height keeps the mark: the glow with no wave.
    if (!HeightAt(map, v[i].x, v[i].y, v[i].rhw, &y)) continue;
    if (y < low) low = y;
    if (y > high) high = y;
    if (!wave) continue;
    const float f = Wave(Share(y, s.base_y, s.height), g_now);
    const uint32_t color =
        AddColor(w.base, Part(s.color[0], s.strength, f),
                 Part(s.color[1], s.strength, f),
                 Part(s.color[2], s.strength, f));
    // The game's own fade of the vertex stays on the new color.
    v[i].specular = alpha == 0xFFu ? color : Faded(color, alpha);
  }
  s.min_y = low;
  s.max_y = high;
  if (n == 0) return;
  // The test key target_glow_log: the first batches of each target, with
  // the code of the game that drew them.
  if (g_log_every > 0 && s.batch_lines < 6) {
    ++s.batch_lines;
    const Vertex& a = v[idx[0] < count ? idx[0] : 0];
    ModLog("target glow  : test: a batch of %s %p, frame %llu, eye %d, %s "
           "the call of the object: %u vertices in triangles have the mark "
           "%06X, %u of them with the fade of the game, of %u vertices and "
           "%u indices. The caller is 0x%08X%s. The first vertex of a "
           "triangle: %.1f %.1f, z %.5f, rhw %.6f, specular %08X now. "
           "Heights so far %.2f to %.2f",
           s.name, s.object, g_frame, StereoPassEye(),
           g_call >= 0 ? "in" : "after", n, w.mark & 0x00FFFFFFu, faded,
           count, indices, caller, clipped ? ", through the clip" : "", a.x,
           a.y, a.z, a.rhw, a.specular, low, high);
  }
  // THE STATE OF THE DEVICE. Direct3D adds the specular color of a vertex
  // only while SPECULARENABLE is on. The game turns it on for a mesh that
  // has a color at +0x50. A batch that comes with the state off does not
  // show its glow, so its vertices do not count, and the target keeps its
  // halo. The mod does not change the state.
  IDirect3DDevice7* device =
      *reinterpret_cast<IDirect3DDevice7* const*>(
          bw::render::kDirect3DDevice7);
  DWORD on = TRUE;
  if (device != nullptr &&
      SUCCEEDED(device->GetRenderState(D3DRENDERSTATE_SPECULARENABLE, &on)) &&
      on == FALSE) {
    s.dark += n;
    return;
  }
  if (clipped) {
    s.clipped += n;
  } else {
    s.vertices += n;
  }
}

// The end of a frame of a slot: the mesh of the next frame, and one line of
// the log when the mark of the target changes between the glow and the halo.
void CloseFrame(Slot* s, int index) {
  const uint32_t all = s->vertices + s->clipped;
  if (all > 0) {
    const MeshExtent known = {s->base_y, s->base_y + s->height};
    const MeshExtent next = Follow(known, s->min_y, s->max_y);
    s->base_y = next.base;
    s->height = next.top > next.base ? next.top - next.base : 0.0f;
  }
  // The test key target_glow_log: a target of before that fades out.
  if (!s->wanted && g_log_every > 0 && all > 0) {
    ModLog("target glow  : test: the glow of %p fades out, slot %d, frame "
           "%llu: level %.2f, the glow with no wave is %d %d %d of 255, %u "
           "vertices",
           s->object, index, g_frame, s->level, s->add[0], s->add[1],
           s->add[2], all);
  }
  if (s->wanted && s->frames > 0) {
    if (all > 0) {
      ++g_frames_glow;
    } else {
      ++g_frames_halo;
    }
    const int say = all > 0 ? 1 : 2;
    const bool sample = g_log_every > 0 &&
                        g_frame % static_cast<uint64_t>(g_log_every) == 0;
    if ((say != s->said || sample) && Log()) {
      s->said = say;
      if (all > 0) {
        ModLog("target glow  : the mesh of %s %p glows, slot %d, frame %llu: "
               "%u vertices in %u 3D objects, %u of them through the clip. "
               "The glow with no wave is %d %d %d of 255 (level %.2f, "
               "strength %.2f), the mesh is from %.2f to %.2f units high in "
               "the world, and in the left eye from %.0f %.0f to %.0f %.0f. "
               "The device drew %u more vertices with the specular color off",
               s->name, s->object, index, g_frame, all, s->models, s->clipped,
               s->add[0], s->add[1], s->add[2], s->level, s->strength,
               s->min_y, s->max_y, s->box[0], s->box[1], s->box[2], s->box[3],
               s->dark);
      } else {
        ModLog("target glow  : %s %p has no mesh with the glow in the draw, "
               "slot %d, frame %llu, so it keeps its halo: %s",
               s->name, s->object, index, g_frame,
               !s->seen ? "the game did not draw it through "
                          "Game3DObject::AddForDrawing in this frame"
               : s->models == 0 ? "no 3D object got the color"
               : s->dark != 0 ? "the device drew its vertices with the "
                                "specular color off"
                              : "no vertex with the color came to the draw");
      }
    }
  }
  g_vertices += s->vertices;
  g_clipped += s->clipped;
  s->seen = false;
  s->models = 0;
  s->vertices = 0;
  s->clipped = 0;
  s->dark = 0;
  NoBox(s);
}

void SetMode(int mode, const char* cause) {
  g_mode = ClampMode(mode);
  ModLog("target glow  : %s: target_glow is %d now (%s), frame %llu", cause,
         g_mode, ModeWords(g_mode), ControllersFrameCount());
}

// The test key: Ctrl, Shift and the left square bracket key. One short pulse
// of the pointing controller for 0, one long pulse for 1, and a longer one
// for 2. Only while a window of the game is in front.
void PollKeys(uint64_t frame) {
  for (int i = 0; i < g_test_frame_count; ++i) {
    static uint64_t done_frame = 0;
    if (frame != done_frame &&
        ControllersTestFrame(g_test_frames[i], frame) == g_test_frames[i]) {
      done_frame = frame;
      SetMode(NextMode(g_mode), "test key target_glow_test_keys");
    }
  }
  if (!g_test_keys) return;
  const bool down = (GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool pressed = down && !g_key_down;
  g_key_down = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  SetMode(NextMode(g_mode),
          "test key: Ctrl, Shift and the left square bracket key");
  ControllersGesturePulse(0.5f, g_mode == kModeHalo   ? 0.08f
                                : g_mode == kModeMesh ? 0.24f
                                                      : 0.45f);
}

}  // namespace

void TargetGlowSetOptions(const TargetGlowSettings& settings) {
  g_mode = ClampMode(settings.mode);
  g_pct = ClampPct(settings.pct);
  g_anim = ClampAnim(settings.anim);
  g_test_keys = settings.test_keys;
}

void TargetGlowInstall() {
  if (g_installed) return;
  for (int i = 0; i < kSlots; ++i) Clear(&g_slots[i]);
  ModLog("target glow  : target_glow=%d (%s), target_glow_pct=%d, "
         "target_glow_anim=%d (%s)", g_mode, ModeWords(g_mode), g_pct, g_anim,
         AnimWords(g_anim));
  const uint8_t* code = reinterpret_cast<const uint8_t*>(kAddForDrawing);
  if (IsBadReadPtr(code, sizeof(kAddForDrawingBytes)) ||
      memcmp(code, kAddForDrawingBytes, sizeof(kAddForDrawingBytes)) != 0) {
    ModLog("target glow  : the code at 0x%08X is not "
           "Game3DObject::AddForDrawing, so no mesh glows and each target "
           "has its halo", static_cast<unsigned>(kAddForDrawing));
    return;
  }
  void* target = reinterpret_cast<void*>(kAddForDrawing);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_AddForDrawing),
                    reinterpret_cast<void**>(&o_AddForDrawing));
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : Game3DObject::AddForDrawing at 0x%08X %s",
         static_cast<unsigned>(kAddForDrawing),
         status == MH_OK ? "ok" : "FAILED");
  if (status != MH_OK) return;
  QueryPerformanceFrequency(&g_frequency);
  g_installed = true;

  char text[128] = "";
  IniKey("target_glow_test_keys", 0, text, sizeof(text));
  for (const char* p = text; *p != 0 && g_test_frame_count < kMaxTestFrames;) {
    char* end = nullptr;
    const unsigned long at = strtoul(p, &end, 10);
    if (end == p) break;
    if (at > 0) g_test_frames[g_test_frame_count++] = at;
    p = end;
    while (*p == ',' || *p == ' ') ++p;
  }
  g_log_every = IniKey("target_glow_log", 0, nullptr, 0);
  if (g_log_every < 0) g_log_every = 0;
  if (g_test_frame_count > 0 || g_log_every > 0) {
    ModLog("target glow  : test keys: target_glow_test_keys has %d frames at "
           "which the test key acts, target_glow_log=%d",
           g_test_frame_count, g_log_every);
  }
}

void TargetGlowFrame() {
  if (!g_installed) return;
  const uint64_t frame = ControllersFrameCount();
  PollKeys(frame);

  // A pass that did not end puts its colors back now.
  if (g_written_count != 0) {
    for (int i = 0; i < g_written_count; ++i) {
      const Written& w = g_written[i];
      if (!IsBadWritePtr(w.model + kModelSpecular, sizeof(uint32_t))) {
        PutBack(w);
      }
      ++g_late;
    }
    g_written_count = 0;
  }

  // The time of this frame, and the time since the frame before.
  LARGE_INTEGER ticks;
  QueryPerformanceCounter(&ticks);
  if (!g_have_time) {
    g_have_time = true;
    g_start = ticks;
  }
  const double now =
      g_frequency.QuadPart > 0
          ? static_cast<double>(ticks.QuadPart - g_start.QuadPart) /
                static_cast<double>(g_frequency.QuadPart)
          : 0.0;
  float seconds = static_cast<float>(now - g_now);
  if (seconds < 0.0f) seconds = 0.0f;
  if (seconds > 0.1f) seconds = 0.1f;
  g_now = now;

  for (int i = 0; i < kSlots; ++i) {
    if (g_slots[i].object != nullptr) CloseFrame(&g_slots[i], i);
  }
  g_frame = frame;

  // The targets of this frame.
  const int kMaxMarks = 6;
  TargetGlowMark marks[kMaxMarks];
  const int count =
      g_mode != kModeHalo ? HandGlowTargets(marks, kMaxMarks) : 0;
  for (int i = 0; i < kSlots; ++i) g_slots[i].wanted = false;
  for (int m = 0; m < count; ++m) {
    const TargetGlowMark& mark = marks[m];
    if (mark.object == nullptr) continue;
    Slot* s = Find(mark.object);
    if (s == nullptr) {
      s = Free();
      if (s == nullptr) continue;
      Clear(s);
      s->object = mark.object;
      s->color[0] = mark.r;
      s->color[1] = mark.g;
      s->color[2] = mark.b;
      ++g_targets;
    } else if (s->wanted) {
      // The object has two halos. The first one gives the color.
      continue;
    }
    s->wanted = true;
    s->name = mark.name;
    s->want[0] = mark.r;
    s->want[1] = mark.g;
    s->want[2] = mark.b;
  }

  g_live = 0;
  for (int i = 0; i < kSlots; ++i) {
    Slot& s = g_slots[i];
    if (s.object == nullptr) continue;
    s.level = Fade(s.level, s.wanted, seconds);
    for (int c = 0; c < 3; ++c) {
      s.color[c] = Approach(s.color[c], s.want[c], seconds);
    }
    if (!s.wanted && s.level <= 0.0f) {
      Clear(&s);
      continue;
    }
    if (s.wanted) ++s.frames;
    s.strength = Strength(g_pct, s.height, s.level, g_anim, g_now);
    for (int c = 0; c < 3; ++c) s.add[c] = Part(s.color[c], s.strength, 1.0f);
    ++g_live;
  }
}

void TargetGlowPassDone() {
  for (int i = 0; i < g_written_count; ++i) PutBack(g_written[i]);
  g_written_count = 0;
}

void TargetGlowBatch(void* verts, uint32_t count, const uint16_t* idx,
                     uint32_t indices, bool clipped, uint32_t caller) {
  if (g_written_count == 0 || verts == nullptr || count == 0 ||
      idx == nullptr || indices < 3) {
    return;
  }
  Vertex* v = static_cast<Vertex*>(verts);
  // In the call of a target, each batch is a batch of that target.
  if (g_call >= 0 && g_call < g_written_count) {
    Paint(g_written[g_call], v, count, idx, indices, clipped, caller);
    return;
  }
  // After the call (a mesh of the sorted list), the color of the first
  // vertex names the target. The builders write the color of the object
  // into each vertex of a mesh, also into a vertex that they do not project.
  const uint32_t first = v[0].specular;
  if ((first & 0x00FFFFFFu) == 0) return;
  for (int i = 0; i < g_written_count; ++i) {
    const uint32_t mark = g_written[i].mark;
    if (!SameRgb(first, mark) && !(StrongMark(mark) && HasMark(first, mark))) {
      continue;
    }
    Paint(g_written[i], v, count, idx, indices, clipped, caller);
    return;
  }
}

bool TargetGlowHaloShows(const void* object) {
  if (!g_installed || g_mode != kModeMesh || object == nullptr) return true;
  const Slot* s = Find(object);
  const bool known = s != nullptr && s->wanted;
  return HaloShows(g_mode, known,
                   known && s->vertices + s->clipped > 0);
}

void TargetGlowReport() {
  if (!g_installed) return;
  if (g_targets == 0 && g_frames_glow == 0 && g_frames_halo == 0 &&
      g_late == 0 && g_full == 0 && g_outside == 0) {
    return;
  }
  ModLog("target glow  : %llu new targets. A target had the glow on its mesh "
         "in %llu frames and kept its halo in %llu frames. %llu vertices got "
         "the glow, %llu of them through the clip. Not as wanted: %llu "
         "colors were put back after their pass, %llu 3D objects had no "
         "place in the list, %llu draws of a target were outside an eye "
         "pass (target_glow=%d)",
         g_targets, g_frames_glow, g_frames_halo, g_vertices + g_clipped,
         g_clipped, g_late, g_full, g_outside, g_mode);
  g_targets = g_frames_glow = g_frames_halo = 0;
  g_vertices = g_clipped = 0;
  g_late = g_full = g_outside = 0;
}
