// SPRITES WITH WORLD UP (sprite_world_up, sprite_math.h, docs/backend.md,
// "Sprites with world up").
//
// LH3DSprite::Draw (0x00840530) draws each billboard of the engine: smoke,
// fire, sparks, the glow of a miracle, the notes over a thing, the marks of a
// fight and the orbs of the logo. A particle system of the game draws its
// sprites through Particle3DSprite::DrawAt (0x0067AE80), which calls it at
// once or puts it in the sorted list (LH3DSprite::AddDrawing 0x00840C70,
// which gives the sorted list the address of the draw). When the flag 0x40 of
// the sprite (+0x28) is clear, the draw puts the four corners in the camera
// plane of the matrix at 0x00EA1D28, turned by the angle of the sprite
// (+0x14). That matrix has the head of the player with its roll, so each
// sprite rolls with the head. With the SSE path (0x00EA9EB4 not zero) the
// draw goes to fn_007A8DB0, which does the same. A sprite with the flag 0x40
// is a flat quad in the world, not a billboard.
//
// The hook adds the turn of sprite_math::SpriteTurn to the angle for the one
// call and puts the angle back after it. The sprite then keeps world up as it
// shows in the camera plane, it still faces the eye, and the game keeps its
// place and size. The two eyes differ only in the camera x translation, so
// they get one turn. With one walk (one_walk=1) the right eye is a copy of
// the draws of the left eye.
//
// SPRITES THAT FOLLOW THEIR SPEED. UR_OrientSpriteWithVelocity::ModifyAtomCore
// (0x0069A790) and fn_006840E0 (from UR_Flocking::ModifyAtomCollection)
// measure the direction of the speed of a particle in the camera plane of the
// same matrix, and give it to the sprite as its angle. They run in the scene
// walk, so that matrix has the roll of the head. Then the angle has the roll,
// and the turn of the draw adds it a second time. So the hook gives these
// two calls the camera with no roll (sprite_math::DerollCamera) and puts the
// matrix back after the call.
//
// The key sprite_world_up=0 keeps the sprites of the game: they roll with the
// head.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "sprite_math.h"

extern void ModLog(const char* format, ...);
// controllers.cpp: the frame number that the scripted controller counts.
uint64_t ControllersFrameCount();

namespace {

// LH3DSprite::Draw: __thiscall, no arguments, a plain ret.
const uintptr_t kSpriteDraw = 0x00840530;
const int kSpriteAngle = 0x14;      // float, radians
const int kSpriteFlags = 0x28;      // uint32_t
const uint32_t kSpriteFlat = 0x40;  // a flat quad in the world
// UR_OrientSpriteWithVelocity::ModifyAtomCore: __thiscall, one argument (the
// AtomCore), ret 4.
const uintptr_t kOrientSpeed = 0x0069A790;
// fn_006840E0: __stdcall (AtomCore*, const float* speed), ret 8.
const uintptr_t kFlockTurn = 0x006840E0;
// The camera matrix that the draw reads (12 floats).
const uintptr_t kCameraMatrix = 0x00EA1D28;
// For the trace: the model view that the draw builds (12 floats), and the
// near distance under which the draw stops before it builds it.
const uintptr_t kSpriteModelView = 0x00EA9E10;
const uintptr_t kNearZ = 0x00E839E0;

typedef void(__fastcall* SpriteDrawFn)(void* self, void* unused);
typedef void(__fastcall* OrientSpeedFn)(void* self, void* unused, void* atom);
typedef void(__stdcall* FlockTurnFn)(void* atom, const float* speed);
SpriteDrawFn o_SpriteDraw = nullptr;
OrientSpeedFn o_OrientSpeed = nullptr;
FlockTurnFn o_FlockTurn = nullptr;

bool g_hooked = false;
// sprite_world_up: the sprites keep world up.
bool g_on = true;
// sprite_test_frame: at this frame of the scripted controller the key above
// changes, for one run with each value. 0 is no change.
uint64_t g_test_frame = 0;
bool g_test_done = false;
// sprite_test_trace: the report has the angle of the drawn sprites from
// world up.
bool g_trace = false;

// The turn of the last camera matrix. The draw runs for each sprite, and the
// matrix changes only with the eye pass and a camera of a room.
float g_cache_up[3] = {0.0f, 0.0f, 0.0f};
float g_cache_turn = 0.0f;

uint64_t g_billboards = 0;
uint64_t g_turned = 0;
float g_turn_most = 0.0f;  // degrees
uint64_t g_speed_calls = 0;
uint64_t g_speed_unrolled = 0;

// THE TRACE.
uint64_t g_trace_sprites = 0;
float g_trace_off_most = 0.0f;  // degrees, drawn up from world up
double g_trace_off_sum = 0.0;
float g_trace_roll_most = 0.0f;  // degrees, the roll of the camera
// The different turns of the sprites in one frame. A camera of a room (the
// advisors) can have no roll while the eye camera has one.
const int kTurnSlots = 4;
uint64_t g_trace_frame = 0;
float g_trace_frame_turns[kTurnSlots];
int g_trace_frame_count = 0;
float g_trace_most_turns[kTurnSlots];
int g_trace_most_count = 0;
float g_trace_speed_roll_most = 0.0f;  // degrees, at a speed measure

const float kToDegrees = 57.29578f;

float TurnNow() {
  const float* m = reinterpret_cast<const float*>(kCameraMatrix);
  if (m[3] == g_cache_up[0] && m[4] == g_cache_up[1] &&
      m[5] == g_cache_up[2]) {
    return g_cache_turn;
  }
  g_cache_up[0] = m[3];
  g_cache_up[1] = m[4];
  g_cache_up[2] = m[5];
  g_cache_turn = sprite_math::SpriteTurn(m);
  return g_cache_turn;
}

// The angle of world up in the camera plane, with no fade. Degrees.
float RollNow() {
  const float* m = reinterpret_cast<const float*>(kCameraMatrix);
  if (m[3] == 0.0f && m[4] == 0.0f) return 0.0f;
  return atan2f(m[3], m[4]) * kToDegrees;
}

void TestFrameCheck() {
  if (g_test_frame == 0 || g_test_done) return;
  const uint64_t frame = ControllersFrameCount();
  if (frame < g_test_frame) return;
  g_test_done = true;
  g_on = !g_on;
  ModLog("sprites      : sprite_test_frame at frame %llu: sprite_world_up=%d "
         "now", frame, g_on ? 1 : 0);
}

// One sprite for the trace, after the draw built its model view. The angle
// of the drawn up comes from the model view of the engine: its rows 0 and 1
// are (cos a, -sin a) and (sin a, cos a), times the scale of the screen.
void TraceSprite(float angle, float turn) {
  const float* mv = reinterpret_cast<const float*>(kSpriteModelView);
  const float drawn = atan2f(mv[3], mv[0]);
  const float roll = RollNow();
  const float off = fabsf(sprite_math::WrapAngle(
                        drawn - angle - roll / kToDegrees)) *
                    kToDegrees;
  ++g_trace_sprites;
  if (off > g_trace_off_most) g_trace_off_most = off;
  g_trace_off_sum += off;
  if (fabsf(roll) > g_trace_roll_most) g_trace_roll_most = fabsf(roll);
  const uint64_t frame = ControllersFrameCount();
  const float t = turn * kToDegrees;
  if (frame != g_trace_frame) {
    g_trace_frame = frame;
    g_trace_frame_count = 0;
  }
  for (int i = 0; i < g_trace_frame_count; ++i) {
    if (fabsf(g_trace_frame_turns[i] - t) < 0.01f) return;
  }
  if (g_trace_frame_count == kTurnSlots) return;
  g_trace_frame_turns[g_trace_frame_count++] = t;
  if (g_trace_frame_count > g_trace_most_count) {
    g_trace_most_count = g_trace_frame_count;
    memcpy(g_trace_most_turns, g_trace_frame_turns,
           sizeof(g_trace_most_turns));
  }
}

void __fastcall h_SpriteDraw(void* self, void* unused) {
  TestFrameCheck();
  if (self == nullptr) {
    o_SpriteDraw(self, unused);
    return;
  }
  uint8_t* sprite = static_cast<uint8_t*>(self);
  if ((*reinterpret_cast<const uint32_t*>(sprite + kSpriteFlags) &
       kSpriteFlat) != 0) {
    o_SpriteDraw(self, unused);
    return;
  }
  ++g_billboards;
  float* angle = reinterpret_cast<float*>(sprite + kSpriteAngle);
  const float old = *angle;
  const float turn = g_on ? TurnNow() : 0.0f;
  // The draw stops before it builds the model view when the sprite is at
  // the eye or behind it. The trace reads only a model view that it built.
  bool built = false;
  if (g_trace) {
    const float* m = reinterpret_cast<const float*>(kCameraMatrix);
    const float* p = reinterpret_cast<const float*>(sprite);
    const float z = p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11];
    built = z > *reinterpret_cast<const float*>(kNearZ);
  }
  if (turn != 0.0f) {
    *angle = old + turn;
    ++g_turned;
    const float degrees = fabsf(turn) * kToDegrees;
    if (degrees > g_turn_most) g_turn_most = degrees;
  }
  o_SpriteDraw(self, unused);
  if (turn != 0.0f) *angle = old;
  if (built) TraceSprite(old, turn);
}

// Gives `call` the camera with no roll, and puts the matrix back after it.
template <typename Call>
void WithNoRoll(Call call) {
  ++g_speed_calls;
  if (g_trace) {
    const float roll = fabsf(RollNow());
    if (roll > g_trace_speed_roll_most) g_trace_speed_roll_most = roll;
  }
  float* m = reinterpret_cast<float*>(kCameraMatrix);
  const float turn = g_on ? sprite_math::SpriteTurn(m) : 0.0f;
  if (turn == 0.0f) {
    call();
    return;
  }
  float saved[12];
  memcpy(saved, m, sizeof(saved));
  sprite_math::DerollCamera(saved, turn, m);
  ++g_speed_unrolled;
  call();
  memcpy(m, saved, sizeof(saved));
}

void __fastcall h_OrientSpeed(void* self, void* unused, void* atom) {
  WithNoRoll([&] { o_OrientSpeed(self, unused, atom); });
}

void __stdcall h_FlockTurn(void* atom, const float* speed) {
  WithNoRoll([&] { o_FlockTurn(atom, speed); });
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

// sprite_world_up, sprite_test_frame and sprite_test_trace.
void SpriteUpSet(bool on, int test_frame, bool trace) {
  g_on = on;
  g_test_frame = test_frame > 0 ? static_cast<uint64_t>(test_frame) : 0;
  g_trace = trace;
  ModLog("sprites      : sprite_world_up=%d. Smoke, fire and the other "
         "sprites keep %s", on ? 1 : 0,
         on ? "world up, so they do not roll with the head"
            : "the camera up, so they roll with the head");
  if (g_test_frame != 0) {
    ModLog("sprites      : sprite_test_frame=%d changes the key at that frame",
           test_frame);
  }
}

void SpriteUpInstall() {
  if (g_hooked) return;
  g_hooked = true;
  Hook(kSpriteDraw, reinterpret_cast<void*>(&h_SpriteDraw),
       reinterpret_cast<void**>(&o_SpriteDraw), "LH3DSprite::Draw");
  Hook(kOrientSpeed, reinterpret_cast<void*>(&h_OrientSpeed),
       reinterpret_cast<void**>(&o_OrientSpeed),
       "UR_OrientSpriteWithVelocity::ModifyAtomCore");
  Hook(kFlockTurn, reinterpret_cast<void*>(&h_FlockTurn),
       reinterpret_cast<void**>(&o_FlockTurn), "the turn of a flock sprite");
}

void SpriteUpReport() {
  if (g_billboards > 0) {
    ModLog("sprites      : %llu billboard draws, %llu turned to world up, the "
           "largest turn %.2f degrees", g_billboards, g_turned, g_turn_most);
  }
  if (g_speed_calls > 0) {
    ModLog("sprites      : %llu turns from a speed, %llu measured with no "
           "roll", g_speed_calls, g_speed_unrolled);
  }
  if (g_trace && g_trace_sprites > 0) {
    char turns[64] = "";
    int at = 0;
    for (int i = 0; i < g_trace_most_count && at < 56; ++i) {
      at += _snprintf_s(turns + at, sizeof(turns) - at, _TRUNCATE, "%s%.2f",
                        i == 0 ? "" : ", ", g_trace_most_turns[i]);
    }
    ModLog("sprite trace : the up of a drawn sprite was at most %.2f degrees "
           "from world up (mean %.3f), in %llu sprites. The roll of the "
           "camera was at most %.2f degrees. One frame had at most %d "
           "different turns (%s degrees)",
           g_trace_off_most, g_trace_off_sum / g_trace_sprites,
           g_trace_sprites, g_trace_roll_most, g_trace_most_count, turns);
  }
  if (g_trace && g_speed_calls > 0) {
    ModLog("sprite trace : at a turn from a speed the roll of the camera was "
           "at most %.2f degrees", g_trace_speed_roll_most);
  }
  g_billboards = 0;
  g_turned = 0;
  g_turn_most = 0.0f;
  g_speed_calls = 0;
  g_speed_unrolled = 0;
  g_trace_sprites = 0;
  g_trace_off_most = 0.0f;
  g_trace_off_sum = 0.0;
  g_trace_roll_most = 0.0f;
  g_trace_most_count = 0;
  g_trace_speed_roll_most = 0.0f;
}
