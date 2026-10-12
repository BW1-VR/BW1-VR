// The stream of a take at a distance (docs/grab.md, "The stream of a take at
// a distance").
//
// WHAT THE GAME DOES. While the Hand takes wood or grain from a bulk source,
// one particle system shows pieces that go up into the Hand. Its one rule,
// ER_MultiPickup::ModifyAtomCollection (0x006A77C0), runs in each walk of the
// scene (PSysGlobal::DrawLoop 0x0068F5E0, fn_006714F0). It makes 8 pieces a
// second at the land under the Hand. On each call it writes the place of
// each piece: a point on the straight line from the start of the piece to
// the Hand, by the age of the piece over its life of 1 s. With the mouse the
// game holds the Hand over the source, so the pieces come out of the source.
//
// THE PROBLEM. In a take at a distance the Hand is the real hand, and it is
// not over the source. The pieces then come out of the land under the Hand.
//
// WHAT THIS FILE DOES. A hook on that rule. After the rule of the game,
// while the far grab has the source of a take at a distance, the mod writes
// the place of each piece again from its age: a point on a curve from the
// top of the source to the Hand (take_stream_math.h). It writes the velocity
// of the curve too, because the tumble rule of the piece of wood reads it
// (AppearanceRuleTumble::ModifyAtomCore 0x006A6200). The game makes and
// deletes the pieces as before. With the Hand at the source the far grab has
// no take, and the line of the game stays.
//
// THE TOP OF A SOURCE is the point of its drawn mesh, which is the base of
// the mesh, plus the height at which the game holds the Hand over the source
// in a take (slot 0x64C, the call at 0x005B3F49). For a pile and a field
// that is Object::GetHeight (0x00638120): the height of the box of the mesh
// times the scale. For a fish farm it is 5 units (0x0052C840). The game
// draws a pile that is not full lower in the land (PileWood::Draw
// 0x0051BC40). The far grab has the point of the press, so the hook reads
// the mesh translation of the source on each frame while the source lives.
// The top then goes down with the pile.
//
// ONCE A FRAME. The rule runs one time in each walk of the scene: one time a
// frame with one walk for both eyes, and two times with two walks. So the
// hook latches the two ends of the curve in its first call of a frame, and
// each call of that frame uses the same curve. The place of a piece then
// depends only on its age.
//
// THE LOG. Tag "take stream": one line when the stream of a take at a
// distance starts, with the source, its top, the Hand and the length of the
// way, and one line at its end with the count of the pieces that the mod
// placed. With take_stream=0 the same two lines say that the line of the game
// stays.
//
// Ini: take_stream (default 1), with the test key of Ctrl, Shift and the
// minus key of the row of digits.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "far_grab.h"
#include "take_stream.h"
#include "take_stream_math.h"

void ModLog(const char* format, ...);
void ControllersGesturePulse(float amplitude, float seconds);

namespace {

// ER_MultiPickup::ModifyAtomCollection(AtomCollection*). thiscall on the
// rule, ret 4, a bool in al.
const uintptr_t kRule = 0x006A77C0;
// AtomCore::GetAtomAge(). thiscall, no arguments, seconds in st(0).
const uintptr_t kAtomAge = 0x00673CC0;
// PSysManager::GetCurrentGesturePosn(LHPoint*). thiscall, ret 4. The game
// gives the particle system the place of the Hand of the picture
// (CHand+0x78) before each process (fn_006714F0).
const uintptr_t kGesturePosn = 0x00673600;
// The rule: RaiseTime at +0x2C (the life of a piece, in seconds) and EmitRate
// at +0x30 (pieces a second), from the file of the stream.
const int kRuleLife = 0x2C;
const int kRuleRate = 0x30;
// The AtomCollection: the PSysManager at +0x30, the first piece at +0x40.
const int kCollectionManager = 0x30;
const int kCollectionFirst = 0x40;
// The AtomCore: the next piece at +0x18, the velocity at +0x34 and the place
// at +0x80, 3 floats each, in world units.
const int kAtomNext = 0x18;
const int kAtomVelocity = 0x34;
const int kAtomPlace = 0x80;
// Object: the LH3DObject at +0x40, and its translation at +0x38. Slot 0x64C
// (GetHeightForHandAboveInteractObject, thiscall, no arguments, a float) is
// the height at which the game holds the Hand over the source of a take.
const int kObjectMesh = 0x40;
const int kMeshTranslation = 0x38;
const int kHandHeightSlot = 0x64C;
const float kMaxHeight = 2000.0f;
// A stream has about 8 pieces. The walk of the list stops here.
const int kMaxPieces = 256;
const int kSeen = 32;
// Frames with no call of the rule after which a stream is over while the far
// grab still has its source: the game does not walk the scene in a menu.
const uint64_t kQuietFrames = 30;

typedef bool(__fastcall* RuleFn)(void* rule, void* unused, void* collection);
typedef float(__fastcall* AgeFn)(void* atom, void* unused);
typedef void(__fastcall* PosnFn)(void* manager, void* unused, float* out);
typedef float(__fastcall* HeightFn)(void* object, void* unused);

RuleFn o_Rule = nullptr;
const AgeFn f_Age = reinterpret_cast<AgeFn>(kAtomAge);
const PosnFn f_Posn = reinterpret_cast<PosnFn>(kGesturePosn);

bool g_on = true;           // take_stream
bool g_keys = false;        // test_keys
bool g_key_down = false;
bool g_hooked = false;
uint64_t g_frame = 0;       // from TakeStreamFrame

// What the first call of the rule in a frame latched.
struct Latch {
  uint64_t frame;
  bool place;  // the mod places the pieces in this frame
  float life;  // seconds
  take_stream_math::Curve curve;
};
Latch g_latch = {~0ull, false, 0.0f, {}};

// A piece of the last call, to count each piece one time.
struct Seen {
  const void* atom;
  float age;
};

// The stream of one take at a distance.
struct Stream {
  bool runs;
  bool placing;      // the mod places its pieces
  void* source;
  float point[3];    // the last point of the mesh of the source
  float height;      // the last height that the game gave for the source
  float first_top;   // the top of the source at the start of the stream
  float top;         // and now
  uint64_t last;     // the frame of the last call of the rule
  uint64_t frames;   // frames with a call
  uint64_t calls;    // calls of the rule
  uint64_t second;   // calls after the first one of a frame
  uint64_t pieces;   // pieces that the mod placed
  float longest;     // the longest way, in world units
  Seen seen[kSeen];
  int seen_count;
};
Stream g_stream = {};

bool Readable(const void* p, size_t n) {
  return p != nullptr && !IsBadReadPtr(p, n);
}

uint8_t* Ptr(const uint8_t* p) {
  return *reinterpret_cast<uint8_t* const*>(p);
}

float Float(const uint8_t* p) { return *reinterpret_cast<const float*>(p); }

// The object lives: the first word is the table of its class, as at the
// selection (hand_direct.cpp, ObjectLives).
bool Lives(void* object, void* vtable) {
  if (object == nullptr || vtable == nullptr) return false;
  if (!Readable(object, kObjectMesh + sizeof(void*))) return false;
  return *static_cast<void* const*>(object) == vtable;
}

void End(const char* why) {
  if (!g_stream.runs) return;
  g_stream.runs = false;
  if (g_stream.placing) {
    ModLog("take stream  : the stream of the take at a distance is over "
           "(%s). The mod placed %llu pieces in %llu frames. The rule of the "
           "game ran %llu times, %llu of them a second time in a frame. The "
           "longest way was %.1f units, and the top of the source went from "
           "%.1f to %.1f, frame %llu", why, g_stream.pieces, g_stream.frames,
           g_stream.calls, g_stream.second, g_stream.longest,
           g_stream.first_top, g_stream.top, g_frame);
  } else {
    ModLog("take stream  : the stream of the take at a distance is over "
           "(%s). The game placed the pieces for %llu frames. Its rule ran "
           "%llu times, %llu of them a second time in a frame, frame %llu",
           why, g_stream.frames, g_stream.calls, g_stream.second, g_frame);
  }
}

// The first call of the rule in a frame: the two ends of the curve, and
// whether the mod places the pieces.
void LatchFrame(const uint8_t* rule, const uint8_t* collection) {
  g_latch.frame = g_frame;
  g_latch.place = false;
  FarTakeSource s = {};
  if (!FarGrabTakeSource(&s)) {
    End("the far grab has no take now");
    return;
  }
  // The top of the source: the point of its mesh now, and the height of the
  // game. A source that the game deleted keeps its last point and height,
  // and nothing reads the object.
  const bool known = g_stream.runs && g_stream.source == s.object;
  float point[3];
  for (int i = 0; i < 3; ++i) {
    point[i] = known ? g_stream.point[i] : s.position[i];
  }
  float height = known ? g_stream.height : 0.0f;
  if (Lives(s.object, s.vtable)) {
    void** table = *static_cast<void***>(s.object);
    height = reinterpret_cast<HeightFn>(table[kHandHeightSlot / 4])(s.object,
                                                                   nullptr);
    const uint8_t* mesh = Ptr(static_cast<const uint8_t*>(s.object) +
                              kObjectMesh);
    if (Readable(mesh, kMeshTranslation + 3 * sizeof(float))) {
      float now[3];
      memcpy(now, mesh + kMeshTranslation, sizeof(now));
      if (take_stream_math::Finite3(now)) memcpy(point, now, sizeof(point));
    }
  }
  const float top[3] = {
      point[0], take_stream_math::Top(point[1], height, kMaxHeight), point[2]};
  // The Hand, as the rule of the game has it for this process.
  float hand[3] = {0.0f, 0.0f, 0.0f};
  uint8_t* manager = Readable(collection, kCollectionFirst + 4)
                         ? Ptr(collection + kCollectionManager) : nullptr;
  const bool have_hand = Readable(manager, 0x40);
  if (have_hand) f_Posn(manager, nullptr, hand);
  const float life = Float(rule + kRuleLife);
  const bool ends_ok = have_hand && take_stream_math::Finite3(top) &&
                       take_stream_math::Finite3(hand) && life > 0.0f;
  const bool place = take_stream_math::Places(g_on, true, ends_ok);
  g_latch.life = life;
  g_latch.curve = take_stream_math::Make(top, hand);
  g_latch.place = place;

  if (g_stream.runs && g_stream.source != s.object) {
    End("the far grab has a different source");
  }
  if (g_stream.runs && g_stream.placing != place) {
    End(place ? "the mod places the pieces now"
              : "the line of the game is back");
  }
  const float way = place ? take_stream_math::Length(g_latch.curve, 16) : 0.0f;
  if (g_stream.runs) {
    memcpy(g_stream.point, point, sizeof(point));
    g_stream.height = height;
    g_stream.top = top[1];
    if (way > g_stream.longest) g_stream.longest = way;
    return;
  }
  g_stream = {};
  g_stream.runs = true;
  g_stream.placing = place;
  g_stream.source = s.object;
  memcpy(g_stream.point, point, sizeof(point));
  g_stream.height = height;
  g_stream.first_top = top[1];
  g_stream.top = top[1];
  g_stream.longest = way;
  const char* name = s.name != nullptr ? s.name : "unknown";
  if (!place) {
    ModLog("take stream  : the stream of a take at a distance starts. The "
           "source %p (%s) has its point at %.1f %.1f %.1f and its top at "
           "%.1f, and the Hand is at %.1f %.1f %.1f. The pieces are on the "
           "line of the game, from the land under the Hand, frame %llu "
           "(take_stream=%d%s)", s.object, name, point[0], point[1], point[2],
           top[1], hand[0], hand[1], hand[2], g_frame,
           g_on ? 1 : 0, g_on ? ", but an end of the curve is no number" : "");
    return;
  }
  const float dx = hand[0] - top[0];
  const float dy = hand[1] - top[1];
  const float dz = hand[2] - top[2];
  const float straight = sqrtf(dx * dx + dy * dy + dz * dz);
  ModLog("take stream  : the stream of a take at a distance starts. The "
         "source %p (%s) has its point at %.1f %.1f %.1f and its top at %.1f "
         "(the game gives a height of %.2f), and the Hand is at %.1f %.1f "
         "%.1f. The way is %.1f units long (%.2f m of the room, %.1f units "
         "straight), and its top is %.1f units over the higher end. A piece "
         "lives %.2f s, and %.0f pieces start in a second. The mod places "
         "the pieces on the curve, frame %llu (take_stream=1)", s.object,
         name, point[0], point[1], point[2], top[1], height, hand[0], hand[1],
         hand[2], way,
         s.units_per_meter > 0.0f ? way / s.units_per_meter : 0.0f, straight,
         take_stream_math::Rise(g_latch.curve, 16), life,
         Float(rule + kRuleRate), g_frame);
}

// A piece that the last call did not place, or a new piece at the address of
// a piece that the game deleted.
bool NewPiece(const void* atom, float age) {
  for (int i = 0; i < g_stream.seen_count; ++i) {
    if (g_stream.seen[i].atom == atom) return age < g_stream.seen[i].age;
  }
  return true;
}

// The place and the velocity of each piece, from its age and the curve of
// this frame.
void Place(const uint8_t* collection) {
  Seen seen[kSeen];
  int count = 0;
  uint8_t* atom = Ptr(collection + kCollectionFirst);
  for (int n = 0; atom != nullptr && n < kMaxPieces; ++n) {
    uint8_t* next = Ptr(atom + kAtomNext);
    const float age = f_Age(atom, nullptr);
    const float t = take_stream_math::Share(age, g_latch.life);
    float place[3];
    float velocity[3];
    take_stream_math::Point(g_latch.curve, t, place);
    take_stream_math::Velocity(g_latch.curve, t, g_latch.life, velocity);
    memcpy(atom + kAtomPlace, place, sizeof(place));
    memcpy(atom + kAtomVelocity, velocity, sizeof(velocity));
    if (NewPiece(atom, age)) ++g_stream.pieces;
    if (count < kSeen) {
      seen[count].atom = atom;
      seen[count].age = age;
      ++count;
    }
    atom = next;
  }
  memcpy(g_stream.seen, seen, sizeof(Seen) * count);
  g_stream.seen_count = count;
}

bool __fastcall h_Rule(void* rule, void* unused, void* collection) {
  const bool r = o_Rule(rule, unused, collection);
  if (rule == nullptr || collection == nullptr) return r;
  const bool first = g_frame != g_latch.frame;
  if (first) {
    LatchFrame(static_cast<const uint8_t*>(rule),
               static_cast<const uint8_t*>(collection));
  }
  if (!g_stream.runs) return r;
  g_stream.last = g_frame;
  ++g_stream.calls;
  if (first) {
    ++g_stream.frames;
  } else {
    ++g_stream.second;
  }
  if (g_latch.place) Place(static_cast<const uint8_t*>(collection));
  return r;
}

const char* Words(bool on) {
  return on ? "The pieces of the stream of a take at a distance go on a "
              "curve from the top of the source to the Hand"
            : "The pieces of the stream of a take at a distance are on the "
              "line of the game, from the land under the Hand";
}

// THE A/B KEY (test_keys=1): Ctrl and Shift with the minus key of the row of
// digits change take_stream. The pointing controller gives a long pulse for 1
// and a short pulse for 0. Only while a window of the game is in front.
void PollKey() {
  if (!g_keys) return;
  const bool down = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool pressed = down && !g_key_down;
  g_key_down = down;
  if (!pressed) return;
  DWORD process = 0;
  const HWND window = GetForegroundWindow();
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_on = !g_on;
  ControllersGesturePulse(0.5f, g_on ? 0.30f : 0.08f);
  ModLog("take stream  : Ctrl, Shift and the minus key: take_stream is %d "
         "now, frame %llu. %s", g_on ? 1 : 0, g_frame, Words(g_on));
}

bool SameBytes(uintptr_t address, const uint8_t* bytes, size_t count) {
  const void* target = reinterpret_cast<const void*>(address);
  return Readable(target, count) && memcmp(target, bytes, count) == 0;
}

}  // namespace

void TakeStreamSetOptions(int take_stream, bool test_keys) {
  g_on = take_stream != 0;
  g_keys = test_keys;
}

void TakeStreamInstall() {
  if (g_hooked) return;
  static const uint8_t kRuleBytes[] = {0x83, 0xEC, 0x50, 0x57, 0x8B, 0xF9};
  static const uint8_t kAgeBytes[] = {0x8B, 0x81, 0x20, 0x01, 0x00, 0x00};
  static const uint8_t kPosnBytes[] = {0x8B, 0x44, 0x24, 0x04, 0x83, 0xC1,
                                       0x30};
  if (!SameBytes(kRule, kRuleBytes, sizeof(kRuleBytes)) ||
      !SameBytes(kAtomAge, kAgeBytes, sizeof(kAgeBytes)) ||
      !SameBytes(kGesturePosn, kPosnBytes, sizeof(kPosnBytes))) {
    ModLog("hooks        : the stream of a take at a distance: the rule at "
           "0x%08X does not have the code of game version 1.42, so the line "
           "of the game stays", static_cast<unsigned>(kRule));
    return;
  }
  void* target = reinterpret_cast<void*>(kRule);
  MH_STATUS s = MH_CreateHook(target, reinterpret_cast<void*>(&h_Rule),
                              reinterpret_cast<void**>(&o_Rule));
  if (s == MH_OK) s = MH_EnableHook(target);
  g_hooked = s == MH_OK;
  ModLog("hooks        : the stream of a take at a distance, "
         "ER_MultiPickup::ModifyAtomCollection at 0x%08X %s",
         static_cast<unsigned>(kRule), g_hooked ? "ok" : "FAILED");
  ModLog("take stream  : take_stream=%d. %s", g_on ? 1 : 0,
         g_hooked ? Words(g_on)
                  : "The hook is missing, so the pieces are on the line of "
                    "the game");
}

void TakeStreamFrame(uint64_t frame) {
  g_frame = frame;
  if (!g_hooked) return;
  PollKey();
  if (!g_stream.runs) return;
  // The game deletes the particle system at the end of a take, so its rule
  // does not run again.
  FarTakeSource s = {};
  if (!FarGrabTakeSource(&s)) {
    End("the take is over");
  } else if (frame > g_stream.last + kQuietFrames) {
    End("the rule of the game did not run for 30 frames");
  }
}
