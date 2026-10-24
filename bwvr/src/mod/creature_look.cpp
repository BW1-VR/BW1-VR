// The Creature looks at the player. See docs/creature.md, "The Creature looks
// at the player".
//
// WHAT THE GAME DOES [V, runblack.exe 1.42]. A step of a plan of the Creature
// can have a look function (a point of the world for the head). The plans
// that act for the player use Creature::LookAtCamera (0x004D1A30): the
// follow of the player, a wave, a prayer, a howl, a show of an object, the
// notice of an action of the player, and more (CreatureAgenda::
// ConstructSubActionsFor...). Creature::LookAtPlayer (0x004D1420) calls it or
// Creature::LookAtHand. On each game turn Creature::ProcessState calls
// fn_004D03B0, which calls the look function of the step and gives the point
// to fn_00482C90. LH3DCreature::UpdateLook turns the head to that point with
// the speed and the limits of the Creature. Creature::LookAtCamera takes the
// place of the camera of the nearest player
// (Creature::GetNearestCameraInterfaceStatus, GInterfaceStatus+0xB0).
//
// WHY THE MOD CHANGES THE POINT. In VR the picture does not come from the
// game camera. In the God view the game camera is 400 units from the land
// under the head and 50 degrees above it (docs/god-view.md, section 7). So a
// Creature that looks at the camera looks at the sky above the player. The
// player's head is a real place in the world. With creature_look_head=1 the
// hook gives Creature::LookAtCamera the point of the head, in the same words
// as the game. The game's own look then turns the head of the Creature, so
// the turn, its speed and its limits are those of the game.
//
// The hook changes the point only for the camera of the local player, and
// only while the head is known (HeadCameraRenderBasis: no flat frame of the
// cut scene window). It does not add a look: each look comes from a plan of
// the Creature.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "creature_look.h"
#include "creature_look_math.h"

extern void ModLog(const char* format, ...);

// head_camera.cpp: the place that the scene is drawn from (the head), in
// world units. False before the first frame with the head and in a flat
// frame of the cut scene window.
bool HeadCameraRenderBasis(float basis_out[9], float position_out[3]);
// hand_direct.cpp: whether an object from an earlier frame still lives.
bool HandDirectObjectLives(void* object, void* table);

namespace {

// --- the game ---------------------------------------------------------------

const uintptr_t kGame = 0x00D0195C;
// Creature::LookAtCamera, thiscall on the Creature (the three words of the
// point), ret 4. It gives 1 when it wrote the point.
const uintptr_t kLookAtCamera = 0x004D1A30;
// Creature::GetNearestCameraInterfaceStatus, thiscall, no arguments.
const uintptr_t kNearestCamera = 0x0047D640;
// GGame::MyInterfaceStatus, thiscall, no arguments.
const uintptr_t kMyStatus = 0x00555880;
// LH3DIsland::GetAltitude, thiscall on three words (x and z in map units).
const uintptr_t kGetAltitude = 0x00803090;
// GInterfaceStatus: the place of the camera.
const int kStatusCamera = 0xB0;
// Creature.
const int kCreatureLink = 0x160;  // [[+0x160]+0x58] is the LH3DCreature
const int kCreatureMind = 0x164;
const int kMindDesire = 0xF50;
const int kMindAction = 0xF60;
const int kMindStep = 0xFB4;
// LH3DCreature.
const int kModelLink = 0x58;
const int kModelPlace = 0x78;     // the base of the body, in the world
const int kModelHeading = 0x84;   // the yaw of the body
const int kModelScale = 0x90;
const int kModelHeadYaw = 0x4864;
const int kModelHeadPitch = 0x486C;
const int kModelLook = 0x48C0;    // the look point
const int kModelLookSet = 0x49A0; // 0x26 takes the second pair of look anims
// Morphable::GetAnim, thiscall on the LH3DCreature (the number of the
// animation, 0), ret 8. LH3DCreature::UpdateBuffers (0x004ECB2E, 0x004ECC06)
// shows the pitch of the head with animation 0x4C (0x4E) and the yaw with
// 0x4B (0x4D). The first word of an animation is its number of frames.
const uintptr_t kGetAnim = 0x00619650;

typedef int(__fastcall* LookFn)(void* creature, void* unused, int32_t* out);
typedef void*(__fastcall* ThisFn)(void* self, void* unused);
typedef float(__fastcall* AltitudeFn)(const int32_t* coords, void* unused);
typedef const int32_t*(__fastcall* GetAnimFn)(void* model, void* unused,
                                              int32_t number, int32_t flag);

LookFn o_LookAtCamera = nullptr;

// --- the key ----------------------------------------------------------------

bool g_ready = false;
bool g_hooked = false;
int g_mode = look_math::kModeHead;  // creature_look_head

int IniInt(const char* key, int fallback) {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return fallback;
  slash[1] = 0;
  strcat_s(path, MAX_PATH, "bwvr.ini");
  return static_cast<int>(GetPrivateProfileIntA("bwvr", key, fallback, path));
}

// --- the frame --------------------------------------------------------------

uint64_t g_frame = 0;
double g_now = 0.0;
// The head of this frame, latched once a frame. The look function runs many
// times in a frame (each Creature, each game turn), and each call gives the
// same point.
bool g_have_head = false;
float g_head[3] = {0.0f, 0.0f, 0.0f};

// --- the log ----------------------------------------------------------------

const int kMaxLogs = 200;
int g_logs = 0;
uint64_t g_calls = 0;  // the look functions that asked for the camera
uint64_t g_heads = 0;  // and that got the head

bool Log() {
  if (g_logs >= kMaxLogs) return false;
  ++g_logs;
  return true;
}

// One look of one Creature at the player, from the first call of the look
// function to the last.
struct Look {
  void* creature;
  void* table;         // the first word of the Creature at the start
  uint8_t* model;
  bool head;           // the last call gave the head
  double start;        // the time of the first call
  double last;         // and of the last call
  uint64_t start_frame;
  float camera[3];     // the camera of the player at the last call
  float target[3];     // the point that the look function gave
  float logged_head[3];  // the head at the last line of the log
  bool on;             // the eyes are on the target now
  bool moved;          // the head moved, and a line waits for the eyes
  double moved_at;
  float least_deg;     // the least angle between the eyes and the head
  int32_t plan[3];     // desire, action and step at the start
};
const int kMaxLooks = 4;
Look g_looks[kMaxLooks] = {};

uint8_t* ModelOf(void* creature) {
  if (creature == nullptr || IsBadReadPtr(creature, kCreatureMind + 4)) {
    return nullptr;
  }
  const uint8_t* c = static_cast<const uint8_t*>(creature);
  const uint8_t* link = *reinterpret_cast<uint8_t* const*>(c + kCreatureLink);
  if (link == nullptr || IsBadReadPtr(link + kModelLink, sizeof(void*))) {
    return nullptr;
  }
  uint8_t* model = *reinterpret_cast<uint8_t* const*>(link + kModelLink);
  if (model == nullptr || IsBadReadPtr(model, kModelLook + 12)) return nullptr;
  return model;
}

void ReadPlan(void* creature, int32_t plan[3]) {
  plan[0] = plan[1] = plan[2] = -1;
  const uint8_t* c = static_cast<const uint8_t*>(creature);
  const uint8_t* mind = *reinterpret_cast<uint8_t* const*>(c + kCreatureMind);
  if (mind == nullptr || IsBadReadPtr(mind + kMindDesire, 0x70)) return;
  plan[0] = *reinterpret_cast<const int32_t*>(mind + kMindDesire);
  plan[1] = *reinterpret_cast<const int32_t*>(mind + kMindAction);
  plan[2] = *reinterpret_cast<const int32_t*>(mind + kMindStep);
}

// The angle between the eyes of the Creature and a point, from the facts of
// the LH3DCreature now.
float EyesToDeg(const uint8_t* model, const float to[3]) {
  const float* base = reinterpret_cast<const float*>(model + kModelPlace);
  const float heading = *reinterpret_cast<const float*>(model + kModelHeading);
  const float scale = *reinterpret_cast<const float*>(model + kModelScale);
  const float yaw = *reinterpret_cast<const float*>(model + kModelHeadYaw);
  const float pitch = *reinterpret_cast<const float*>(model + kModelHeadPitch);
  return look_math::EyesToDeg(base, scale, heading, yaw, pitch, to);
}

void EyesOf(const uint8_t* model, float out[3]) {
  const float* base = reinterpret_cast<const float*>(model + kModelPlace);
  const float scale = *reinterpret_cast<const float*>(model + kModelScale);
  look_math::EyePoint(base, scale, out);
}

Look* Find(void* creature, bool make) {
  Look* free_slot = nullptr;
  for (int i = 0; i < kMaxLooks; ++i) {
    if (g_looks[i].creature == creature) return &g_looks[i];
    if (g_looks[i].creature == nullptr && free_slot == nullptr) {
      free_slot = &g_looks[i];
    }
  }
  return make ? free_slot : nullptr;
}

// The animations that show the turn of the head, and their frames. -1 when
// the Creature has no such animation: then the head does not show the look.
void LookAnims(uint8_t* model, int32_t frames[2]) {
  const bool second =
      *reinterpret_cast<const int32_t*>(model + kModelLookSet) == 0x26;
  const int32_t numbers[2] = {second ? 0x4D : 0x4B, second ? 0x4E : 0x4C};
  for (int i = 0; i < 2; ++i) {
    const int32_t* anim = reinterpret_cast<GetAnimFn>(kGetAnim)(
        model, nullptr, numbers[i], 0);
    frames[i] = anim != nullptr && !IsBadReadPtr(anim, 4) ? anim[0] : -1;
  }
}

// THE START OF A LOOK: one line with the two points and the angle.
void LogStart(const Look& l) {
  if (!Log()) return;
  int32_t frames[2];
  LookAnims(l.model, frames);
  float eyes[3];
  EyesOf(l.model, eyes);
  const float to_head = EyesToDeg(l.model, g_head);
  const float to_target = EyesToDeg(l.model, l.target);
  ModLog("creature look: the Creature %p looks at the player now (its plan: "
         "desire %d, action %d, step %d). The game asks for the place of the "
         "camera %.1f %.1f %.1f. creature_look_head=%d: %s %.1f %.1f %.1f. "
         "The head is at %.1f %.1f %.1f, %.1f units from the eyes of the "
         "Creature and %.1f above them. The eyes are %.1f degrees from the "
         "target and %.1f degrees from the head. The animations of the turn "
         "of the head have %d frames (yaw) and %d frames (pitch), frame %llu",
         l.creature, l.plan[0], l.plan[1], l.plan[2], l.camera[0],
         l.camera[1], l.camera[2], g_mode,
         l.head ? "the mod gives it the point of your head"
                : "the game keeps the point of its camera",
         l.target[0], l.target[1], l.target[2], g_head[0], g_head[1],
         g_head[2], look_math::Distance(eyes, g_head), g_head[1] - eyes[1],
         to_target, to_head, frames[0], frames[1], g_frame);
}

// --- the hook ---------------------------------------------------------------

void* LocalStatus() {
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return nullptr;
  return reinterpret_cast<ThisFn>(kMyStatus)(*slot, nullptr);
}

// THE LOOK AT THE CAMERA. The game writes the place of the camera. With
// creature_look_head=1, for the camera of the local player and a known head,
// the hook writes the point of the head in its place.
int __fastcall h_LookAtCamera(void* creature, void* unused, int32_t* out) {
  const int wrote = o_LookAtCamera(creature, unused, out);
  if (wrote == 0 || out == nullptr || !g_have_head) return wrote;
  uint8_t* model = ModelOf(creature);
  if (model == nullptr) return wrote;
  void* status = reinterpret_cast<ThisFn>(kNearestCamera)(creature, nullptr);
  if (status == nullptr || status != LocalStatus()) return wrote;
  ++g_calls;
  const float* camera = reinterpret_cast<const float*>(
      static_cast<const uint8_t*>(status) + kStatusCamera);
  const AltitudeFn altitude = reinterpret_cast<AltitudeFn>(kGetAltitude);
  const bool head = g_mode == look_math::kModeHead;
  if (head) {
    const int32_t coords[3] = {look_math::ToMap(g_head[0]),
                               look_math::ToMap(g_head[2]), 0};
    look_math::HeadWords(g_head, altitude(coords, nullptr), out);
    ++g_heads;
  }
  // FOR THE LOG: the point that fn_004D03B0 makes of the words.
  const int32_t coords[3] = {out[0], out[1], 0};
  float target[3];
  look_math::PointOfWords(out, altitude(coords, nullptr), target);
  Look* l = Find(creature, true);
  if (l == nullptr) return wrote;
  const bool fresh = l->creature != creature;
  if (fresh) {
    memset(l, 0, sizeof(*l));
    l->creature = creature;
    l->table = *static_cast<void**>(creature);
    l->start = g_now;
    l->start_frame = g_frame;
    l->least_deg = 360.0f;
    memcpy(l->logged_head, g_head, sizeof(g_head));
    ReadPlan(creature, l->plan);
  }
  l->model = model;
  l->head = head;
  l->last = g_now;
  memcpy(l->camera, camera, sizeof(l->camera));
  memcpy(l->target, target, sizeof(l->target));
  if (fresh) LogStart(*l);
  return wrote;
}

// --- each frame -------------------------------------------------------------

void Step(Look* l) {
  if (!HandDirectObjectLives(l->creature, l->table)) {
    memset(l, 0, sizeof(*l));
    return;
  }
  const double ms = (g_now - l->start) * 1000.0;
  // THE END: no look function asked for the camera for a while. The game
  // turn is about 100 ms, so the wait is longer than a few turns.
  if (g_now - l->last > look_math::kEndSeconds) {
    if (Log()) {
      ModLog("creature look: the look of the Creature %p at the player ends "
             "after %.0f ms: no step of its plan asks for the camera now. "
             "The eyes came to %.1f degrees from the head at the least. Now "
             "they are %.1f degrees from the head, frame %llu",
             l->creature, (l->last - l->start) * 1000.0, l->least_deg,
             ModelOf(l->creature) != nullptr ? EyesToDeg(l->model, g_head)
                                             : -1.0f,
             g_frame);
    }
    memset(l, 0, sizeof(*l));
    return;
  }
  if (ModelOf(l->creature) != l->model) {
    memset(l, 0, sizeof(*l));
    return;
  }
  const float to_head = EyesToDeg(l->model, g_head);
  const float to_target = EyesToDeg(l->model, l->target);
  if (to_head < l->least_deg) l->least_deg = to_head;
  const float* look = reinterpret_cast<const float*>(l->model + kModelLook);
  // THE EYES COME TO THE TARGET.
  if (!l->on && to_target < look_math::kOnDeg) {
    l->on = true;
    if (Log()) {
      ModLog("creature look: the eyes of the Creature %p are on the target "
             "%.0f ms after the start of the look: %.1f degrees from the "
             "target and %.1f degrees from the head. The look point of the "
             "game is %.1f %.1f %.1f. The head of the Creature turns %.1f "
             "degrees (yaw) and %.1f degrees (pitch), frame %llu",
             l->creature, ms, to_target, to_head, look[0], look[1], look[2],
             *reinterpret_cast<const float*>(l->model + kModelHeadYaw) *
                 57.29578f,
             *reinterpret_cast<const float*>(l->model + kModelHeadPitch) *
                 57.29578f,
             g_frame);
    }
  } else if (l->on && to_target > 2.0f * look_math::kOnDeg && !l->moved) {
    l->on = false;
  }
  // THE HEAD MOVES: one line when the eyes have followed it, or after 2 s.
  if (!l->moved &&
      look_math::Distance(g_head, l->logged_head) > look_math::kMovedUnits) {
    l->moved = true;
    l->moved_at = g_now;
  }
  if (l->moved &&
      (to_target < look_math::kOnDeg || g_now - l->moved_at > 2.0)) {
    l->moved = false;
    l->on = to_target < look_math::kOnDeg;
    if (Log()) {
      ModLog("creature look: the head moved %.1f units to %.1f %.1f %.1f. "
             "%.0f ms later the eyes of the Creature %p are %.1f degrees "
             "from the head and %.1f degrees from the target %.1f %.1f %.1f "
             "(creature_look_head=%d), frame %llu",
             look_math::Distance(g_head, l->logged_head), g_head[0],
             g_head[1], g_head[2], (g_now - l->moved_at) * 1000.0, l->creature,
             to_head, to_target, l->target[0], l->target[1], l->target[2],
             g_mode, g_frame);
    }
    memcpy(l->logged_head, g_head, sizeof(g_head));
  }
}

}  // namespace

void CreatureLookInstall() {
  if (g_ready) return;
  g_ready = true;
  g_mode = look_math::ClampMode(IniInt("creature_look_head", 1));
  ModLog("creature look: creature_look_head=%d. %s", g_mode,
         g_mode == look_math::kModeHead
             ? "Where a plan of the Creature looks at the camera of the "
               "player, the Creature looks at your head: the mod gives the "
               "point of the head to the look of the game"
             : "Where a plan of the Creature looks at the camera of the "
               "player, the Creature looks at the game camera");
  // The hook is also on with 0: it then changes nothing and gives the lines
  // of the log for a comparison.
  void* target = reinterpret_cast<void*>(kLookAtCamera);
  MH_STATUS status =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_LookAtCamera),
                    reinterpret_cast<void**>(&o_LookAtCamera));
  if (status == MH_OK) status = MH_EnableHook(target);
  g_hooked = status == MH_OK;
  ModLog("hooks        : Creature::LookAtCamera at 0x%08X %s",
         static_cast<unsigned>(kLookAtCamera), g_hooked ? "ok" : "FAILED");
}

void CreatureLookFrame(double now, uint64_t frame) {
  g_now = now;
  g_frame = frame;
  float basis[9];
  g_have_head = HeadCameraRenderBasis(basis, g_head);
  if (!g_have_head) return;
  for (int i = 0; i < kMaxLooks; ++i) {
    if (g_looks[i].creature != nullptr) Step(&g_looks[i]);
  }
}

void CreatureLookReport() {
  if (g_calls == 0) return;
  ModLog("creature look: %llu calls of the look at the camera of the player, "
         "%llu of them got the point of the head, this window", g_calls,
         g_heads);
  g_calls = g_heads = 0;
}
