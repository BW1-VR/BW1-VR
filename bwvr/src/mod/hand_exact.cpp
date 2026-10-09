// The exact Hand: the game's own Hand mesh on the player's real hand. See
// docs/hand.md, "The exact Hand".
//
// WHY. The pose builder fn_0046E160 turns the Hand by the yaw of the screen
// ray and a gliding up vector, never by the controller. It puts the Hand's
// point at the target, and the mesh's origin is the tip of the index finger,
// so the whole Hand would sit about one hand length in front of the real hand.
// A blend after each change of state lets the Hand glide for 0.13 s.
//
// THE SEAM. Every Hand state builds a root matrix and then composes the
// bones of its animation with it: fn_00839F10, cdecl (LHMatrix* pose, anim,
// const LHMatrix* root), plain ret [V, the calls at 0x005B7443 (Normal),
// 0x005B480E (Holding), 0x005B8DEE (Tug), 0x005B2A49 (Creature) and more].
// The symbol file calls it LH3DTech::GetCameraPosition. Each bone's matrix
// becomes bone * parent, and a root bone takes the root. The pose array is
// CHand+0x47F0, so a hook here knows the Hand's compose by its pose array.
// While the controller places the Hand, the hook gives the compose the exact
// root in place of the game's. The state's own root and its logic stay as the
// game made them, and only the drawn bones change. The Creature state does not
// call the pose builder, so this seam reaches it too.
//
// THE ROOT (hand_exact_math.h, ExactRoot). The fingers go along the
// controller's finger axis, the palm faces away from the back of the real
// hand, and the origin (the index tip of the mesh) is the target, which
// controllers.cpp puts at the real index tip. The size is the real hand's
// length over the mesh's length. The mirror of CHand+0x484C stays, so the
// game's right hand stays a right hand. The rest turn (hand_mesh_yaw_deg,
// hand_mesh_pitch_deg, hand_mesh_roll_deg) lines the mesh up with the hand.
//
// NO LAG. CHand::PrepareForDrawing mixes the world matrices of the old pose
// (CHand+0x49AC) and the new pose for 0.13 s after a change of state
// (0x0046C932 to 0x0046CA28). The hook keeps the root of each compose of
// each pose array. While a blend runs, it moves the old pose to the new root
// (Rebase), so the blend mixes only the fingers. In the Normal and the Holding
// states it also sets 0x00C5E670 to 0x00C5E680, so the held object does not
// lag.
//
// THE STATES. Normal (1) and Holding (4): the hooks of hand_direct.cpp put
// the Hand's position at the target, and this gives the drawn pose.
// Creature (7): the depth hook puts the position at the target. Camera (2),
// Tug (3), Grain (8) and PlayAnim (9): the game keeps its own position, which
// its logic uses, and only the drawn Hand is at the controller. Invisible,
// Totem, MultiPickUp and Citadel stay as the game draws them.
//
// ONCE A FRAME. The compose runs in each eye pass. The target, the size and
// the controller's rotation are latched once a frame by hand_direct.cpp and
// controllers.cpp, so both passes give the same root.
//
// AFTER THE DRAW SETUP. pass_guard.cpp calls HandExactAfterPrepare after
// CHand::PrepareForDrawing (its hook of 0x0046C550). The log of the drawn
// bones runs there, and a later feature (the glide of a far grab) can add its
// own call with HandExactAddPrepareHook.
//
// ONE PAIR OF HANDS (docs/hand.md, "The two real hands and
// which one is the Hand"). Two right hands
// that disagree would confuse the player, so the mod's hand is the hand, and
// the game's Hand gives only its bones:
//
//   THE WRIST LOCK. The Holding animation moves the wrist bone itself (79
//   degrees about the fingers, 14 cm), so the Hand turned away from the real
//   hand with an object in it. The compose gets a root that keeps the
//   wrist bone at the file's wrist under the exact root, in each animation
//   (hand_exact_math.h, WristLockRoot). A spell on a bone follows the real
//   hand in each state.
//
//   THE MESH IS HIDDEN. CHand::Draw 0x0046D210 draws the mesh with a call of
//   its vtable +0x108 at 0x0046D27C (and at 0x0046D258 when CHand+0x47BC is
//   set), then the ring of the influence, the effects and the spell in the
//   Hand [V]. The mesh object has the vtable 0x009A3068, and its +0x108 is
//   fn_00813BA0, thiscall with no arguments [V]. A hook there skips the draw
//   for the Hand's mesh when the call comes from CHand::Draw, while the
//   controller places the Hand and the overlay drew the pointing hand in this
//   frame. Each other part of CHand::Draw still runs. The mouse player sees
//   the game's Hand as the game draws it.
//
//   THE HELD OBJECT IN THE PALM. In the Holding state PrepareForDrawing puts
//   the object's mesh at the Hand's point plus the hang offset (0x0046CA50 to
//   0x0046CA97) [V]. After it, the mod puts the object in the palm of the
//   mod's hand, and turns it with the hand from the pose it had when it came
//   into the hand (hand_exact_math.h, HoldOrigin). The glide of a far grab
//   and the first frames of a throw run after this and start from this place.
//
//   THE ANIMATION. Each hand state asks Morphable::GetAnim 0x00619650 for its
//   animation by number [V]. A hook keeps the last number that the Hand
//   asked for, for the log and for the pose of the mod's hand.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"
#include "hand_exact.h"
#include "hand_exact_math.h"
#include "miracle.h"

extern void ModLog(const char* format, ...);

// overlay.cpp: the overlay drew the pointing hand in this frame.
bool OverlayPointingHandDrawn();
// controllers.cpp: the frame count, and a pulse on the pointing hand.
uint64_t ControllersFrameCount();
void ControllersGesturePulse(float amplitude, float seconds);

// hand_direct.cpp: the CHand, and whether the controller places it now.
void* HandDirectHand(bool* active);
// hand_direct.cpp: the target (after the land test) and the size of this
// frame. False when the controller does not place the Hand.
bool HandDirectExactFacts(float target[3], float* size, uint64_t* frame);
// controllers.cpp: the pointing grip's axes in the world, L * M_grip, of this
// frame, and whether it is the left hand.
bool ControllersExactRotation(float rotation[9], bool* left);
// tabletop.cpp and head_pose.cpp: world units in a meter of the room, for
// the log. The table's scale while the table or the God view owns the view,
// and 0 otherwise.
float TabletopViewScale();
float HeadPoseScale();

namespace {

namespace hx = hand_exact_math;

// fn_00839F10, the compose of a pose with its root.
const uintptr_t kCompose = 0x00839F10;
typedef void(__cdecl* ComposeFn)(float* pose, void* anim, const float* root);
ComposeFn o_Compose = nullptr;

// CHand fields. See docs/hand.md, "CHand fields" and "The exact Hand".
const int kHandPosition = 0x78;
const int kHandBoneSpan = 0x8C;   // the span of the bones in y [V 0x00618602]
const int kHandSize1 = 0x90;
const int kHandK = 0x94;          // the game's mesh scale [V fn_0046D080]
const int kHandBones = 0x47B8;    // the bone count, 12 floats each
const int kHandPose = 0x47F0;     // the new pose array (LHMatrix*)
const int kHandScale = 0x4834;
const int kHandMirror = 0x484C;   // 1: the stored mesh is drawn mirrored
const int kHandState = 0x4878;
const int kHandOldPose = 0x49AC;  // the old pose array of the blend
const int kHandBlendTime = 0x49B0;
const float kBlendSeconds = 0.13f;  // 0x008CBE9C
const int kMaxBones = 64;
// The Hand's length at size 1 (fn_0046C040).
const float kHandLength = 3.2f;
// Written by the pose builder: the Hand's position, and the place the blend
// of the held object starts from.
const uintptr_t kHandWorld = 0x00C5E680;
const uintptr_t kHandWorldBefore = 0x00C5E670;

const int32_t kStateNormal = 1;
const int32_t kStateCamera = 2;
const int32_t kStateTug = 3;
const int32_t kStateHolding = 4;
const int32_t kStateCreature = 7;
const int32_t kStateGrain = 8;
const int32_t kStatePlayAnim = 9;

// ONE PAIR OF HANDS. The draw of the Hand's mesh: fn_00813BA0, thiscall with
// no arguments, at +0x108 of the vtable 0x009A3068 [V]. CHand::Draw calls it
// at 0x0046D258 and at 0x0046D27C, so it returns to these two addresses.
const uintptr_t kMeshDraw = 0x00813BA0;
const uintptr_t kMeshVtable = 0x009A3068;
const int kMeshDrawSlot = 0x108;
const uintptr_t kDrawCallA = 0x0046D258;
const uintptr_t kDrawCallB = 0x0046D27C;
const uintptr_t kDrawReturnA = 0x0046D25E;
const uintptr_t kDrawReturnB = 0x0046D282;
typedef void(__fastcall* MeshDrawFn)(void* mesh, void* unused);
MeshDrawFn o_MeshDraw = nullptr;
// Morphable::GetAnim, thiscall (int animation, int), ret 8 [V].
const uintptr_t kGetAnim = 0x00619650;
typedef void*(__fastcall* GetAnimFn)(void* self, void* unused, int animation,
                                     int second);
GetAnimFn o_GetAnim = nullptr;
const int kHandMorph = 0x9C;      // the alignment, -1 to 1 [V 0x0046C66D]
const int kHandMesh = 0x482C;     // the mesh object
const int kHandHeld = 0x4904;     // the held Object*
// The offset from the Hand to the held object [V 0x0046CA5A].
const uintptr_t kHangOffset = 0x00D13F20;
// The release physics owns the held object's place [V 0x0046CA3A].
const uintptr_t kPredictionActive = 0x00D47824;
const uintptr_t kPredictionObject = 0x00D47828;
// Object: the LH3DObject at +0x40, its matrix at +0x14 (4 rows of 3), and
// Get2DRadius at +0x64 of the vtable (hand_direct.cpp uses the same).
const int kObjectMesh = 0x40;
const int kMeshMatrix = 0x14;
const int kGet2DRadius = 0x64;

struct Settings {
  bool exact = true;
  float yaw = hx::kRestYawDeg;
  float pitch = hx::kRestPitchDeg;
  float roll = hx::kRestRollDeg;
  bool left_mesh = false;
  bool hide = true;        // hand_hide_game
  bool wrist_lock = true;  // hand_wrist_lock
  bool hold_palm = true;   // hand_hold_palm
  bool hold_turn = true;   // hand_hold_turn
  bool tree_trunk = true;  // hand_hold_trunk
  bool tint = true;        // hand_tint
  bool test_keys = false;
};
Settings g_set;
float g_rest[9];
bool g_installed = false;
bool g_mesh_hooked = false;
bool g_anim_hooked = false;
// The mesh is hidden now: hand_hide_game, and the test key.
bool g_hide_live = false;

// The roots of the last compose of each pose array, for the rebase.
struct ArrayRoot {
  const float* pose;
  float root[12];
  bool valid;
  bool exact;  // the root was the exact root, not the game's
};
const int kArrays = 4;
ArrayRoot g_arrays[kArrays];
int g_next_array = 0;

// The root of the last exact compose, for the log after the draw setup.
bool g_have_root = false;
float g_root[12];
uint64_t g_root_frame = 0;
int32_t g_root_state = -1;
bool g_root_left = false;
float g_root_size = 0.0f;
float g_rotation[9];

// For the log.
bool g_said_first = false;
bool g_said_side = false;
bool g_said_no_record = false;
// The measurement of the drawn bones, once for each state, after a blend.
const int kMeasuredStates = 11;
bool g_measured[kMeasuredStates] = {false};
int g_steady_passes = 0;
uint64_t g_exact_composes = 0;
uint64_t g_game_composes = 0;
uint64_t g_rebases = 0;
uint64_t g_blends_ended = 0;
int32_t g_logged_state = -1;
int g_state_logs = 0;
const int kMaxStateLogs = 60;
int g_check_logs = 0;
const int kMaxCheckLogs = 60;
uint64_t g_last_check_frame = 0;
const uint64_t kCheckFrames = 600;
// The wrist bone in the Hand's own frame on the last frame, for the drift.
bool g_have_wrist = false;
float g_wrist_model[3];
float g_worst_drift_cm = 0.0f;
uint64_t g_drift_frames = 0;
uint64_t g_checked_frames = 0;
int g_after_state_change = 0;

// The animation number that the Hand asked for last, and the one of the last
// compose of the Hand's pose.
void* g_anim_hand = nullptr;
int g_anim_asked = -1;
int g_anim = -1;
// The wrist of the animation against the file's, once for each state.
bool g_wrist_said[kMeasuredStates] = {false};
// The lock works only after the Normal animation showed the wrist of the
// file in the first matrix of the pose.
bool g_lock_ok = false;
uint64_t g_locked_composes = 0;
uint64_t g_hidden_draws = 0;
bool g_said_hidden = false;
bool g_said_vtable = false;
bool g_key_down = false;
uint64_t g_key_frame = 0;

// THE HELD OBJECT IN THE PALM. The pose of the hand and of the object when
// the object came into the hand, and the matrix of this frame.
struct Hold {
  void* object = nullptr;
  float radius_2d = 0.0f;
  float rotation0[9];      // the grip's axes then
  float base[9];           // the object's axes then
  float wrote[9];          // the axes that the mod wrote last
  bool have_wrote = false;
  float offset_local[3];   // the game's place less the palm place, hand frame
  int64_t start = 0;       // QueryPerformanceCounter
  uint64_t frame = 0;      // the frame of `out`
  bool have_out = false;
  float out[12];
  float radius = 0.0f;     // of the body, world units
};
Hold g_hold;
LARGE_INTEGER g_frequency = {};
uint64_t g_holds_placed = 0;
int g_hold_logs = 0;
const int kMaxHoldLogs = 40;
// The radius of the body of the object in the Hand, for the pose, in each
// state that holds one.
void* g_radius_object = nullptr;
float g_radius_2d = 0.0f;

// Later features: a call after CHand::PrepareForDrawing.
typedef void (*PrepareHook)(void* hand);
const int kMaxPrepareHooks = 6;
PrepareHook g_prepare_hooks[kMaxPrepareHooks] = {nullptr};
int g_prepare_hook_count = 0;

// The game folder's bwvr.ini.
bool IniPath(char* path, size_t size) {
  const DWORD length = GetModuleFileNameA(nullptr, path,
                                          static_cast<DWORD>(size));
  if (length == 0 || length >= size) return false;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return false;
  slash[1] = 0;
  if (strlen(path) + 9 >= size) return false;
  strcat_s(path, size, "bwvr.ini");
  return true;
}

float IniFloat(const char* path, const char* key, float fallback) {
  char text[32];
  GetPrivateProfileStringA("bwvr", key, "", text, sizeof(text), path);
  if (text[0] == 0) return fallback;
  char* end = nullptr;
  const double value = strtod(text, &end);
  if (end == text || !(value > -360.0 && value < 360.0)) return fallback;
  return static_cast<float>(value);
}

void LoadSettings() {
  char path[MAX_PATH];
  if (IniPath(path, sizeof(path))) {
    g_set.exact = GetPrivateProfileIntA("bwvr", "hand_exact", 1, path) != 0;
    g_set.yaw = IniFloat(path, "hand_mesh_yaw_deg", hx::kRestYawDeg);
    g_set.pitch = IniFloat(path, "hand_mesh_pitch_deg", hx::kRestPitchDeg);
    g_set.roll = IniFloat(path, "hand_mesh_roll_deg", hx::kRestRollDeg);
    g_set.left_mesh =
        GetPrivateProfileIntA("bwvr", "hand_left_mesh", 0, path) != 0;
    g_set.hide = GetPrivateProfileIntA("bwvr", "hand_hide_game", 1, path) != 0;
    g_set.wrist_lock =
        GetPrivateProfileIntA("bwvr", "hand_wrist_lock", 1, path) != 0;
    g_set.hold_palm =
        GetPrivateProfileIntA("bwvr", "hand_hold_palm", 1, path) != 0;
    g_set.hold_turn =
        GetPrivateProfileIntA("bwvr", "hand_hold_turn", 1, path) != 0;
    g_set.tree_trunk =
        GetPrivateProfileIntA("bwvr", "hand_hold_trunk", 1, path) != 0;
    g_set.tint = GetPrivateProfileIntA("bwvr", "hand_tint", 1, path) != 0;
    g_set.test_keys = GetPrivateProfileIntA("bwvr", "test_keys", 0, path) != 0;
  }
  hx::RestTurn(g_set.yaw, g_set.pitch, g_set.roll, g_rest);
}

// The states where the drawn Hand is exact.
bool ExactState(int32_t state) {
  return state == kStateNormal || state == kStateCamera ||
         state == kStateTug || state == kStateHolding ||
         state == kStateCreature || state == kStateGrain ||
         state == kStatePlayAnim;
}

// The states where hand_direct.cpp puts the Hand's position at the target.
bool PlacedState(int32_t state) {
  return state == kStateNormal || state == kStateHolding ||
         state == kStateCreature ||
         (state == kStateGrain && MiraclePlacesHand());
}

ArrayRoot* FindArray(const float* pose) {
  for (int i = 0; i < kArrays; ++i) {
    if (g_arrays[i].valid && g_arrays[i].pose == pose) return &g_arrays[i];
  }
  return nullptr;
}

void Remember(const float* pose, const float root[12], bool exact) {
  ArrayRoot* slot = FindArray(pose);
  if (slot == nullptr) {
    slot = &g_arrays[g_next_array];
    g_next_array = (g_next_array + 1) % kArrays;
  }
  slot->pose = pose;
  memcpy(slot->root, root, sizeof(slot->root));
  slot->valid = true;
  slot->exact = exact;
}

int BoneCount(const uint8_t* hand) {
  const int32_t n = *reinterpret_cast<const int32_t*>(hand + kHandBones);
  return (n >= 1 && n <= kMaxBones) ? n : 0;
}

// NO LAG. While the blend of a change of state runs, the old pose goes to
// the root of this compose. Its bones stay where they are in the Hand's own
// frame, so the blend mixes only the fingers. An old pose with no known root
// (the hook never saw its compose, for example at the start or after a story
// that hid the Hand) cannot move. Then the blend ends at once, as
// PrepareForDrawing does at 0.13 s, and the Hand does not glide from a stale
// place. The same for an old pose that the game placed with its own root:
// after a dialog box, such a pose can be 27 cm from the real hand for one
// frame.
//
// The records hold the exact roots, not the roots of the wrist lock. The
// lock puts a factor in front of the exact root that is the same for the old
// pose then and now, so m * inverse(exact then) * (exact now) moves a locked
// pose too.
void RebaseOld(uint8_t* hand, const float* pose, const float root[12]) {
  float* old = *reinterpret_cast<float* const*>(hand + kHandOldPose);
  if (old == nullptr || old == pose) return;
  float* timer = reinterpret_cast<float*>(hand + kHandBlendTime);
  if (!(*timer < kBlendSeconds)) return;
  ArrayRoot* record = FindArray(old);
  if (record == nullptr || !record->exact) {
    *timer = kBlendSeconds;
    ++g_blends_ended;
    if (!g_said_no_record) {
      g_said_no_record = true;
      ModLog("hand exact   : the old pose of a blend has no known exact root, "
             "so the blend ends at once");
    }
    return;
  }
  if (memcmp(record->root, root, sizeof(record->root)) == 0) return;
  const int n = BoneCount(hand);
  if (n == 0) return;
  for (int i = 0; i < n; ++i) {
    float* bone = old + i * 12;
    float moved[12];
    if (!hx::Rebase(bone, record->root, root, moved)) return;
    memcpy(bone, moved, sizeof(moved));
  }
  memcpy(record->root, root, sizeof(record->root));
  ++g_rebases;
}

// The exact root of this frame. False when the controller does not place the
// Hand or a fact is missing.
bool BuildRoot(const uint8_t* hand, int32_t state, float out[12]) {
  float target[3], size = 0.0f, rotation[9];
  uint64_t frame = 0;
  bool left = false;
  if (!HandDirectExactFacts(target, &size, &frame)) return false;
  if (!ControllersExactRotation(rotation, &left)) return false;
  if (!(size > 0.0f)) return false;
  float f[3], b[3];
  hx::HandAxes(rotation, left, f, b);
  const bool mirrored =
      *reinterpret_cast<const int32_t*>(hand + kHandMirror) != 0;
  const float k = kHandLength * size / hx::kMeshLength;
  if (!hx::ExactRoot(f, b, g_rest, k, mirrored, target, out)) return false;
  // The game's Hand is a right hand while 0x484C is set. On the other
  // controller its thumb is on the wrong side.
  if (left == mirrored && !g_said_side) {
    g_said_side = true;
    ModLog("hand exact   : the game's Hand is a %s hand, and the %s "
           "controller places it. Its thumb is on the other side. The game's "
           "own left handed option changes the Hand",
           mirrored ? "right" : "left", left ? "left" : "right");
  }
  memcpy(g_root, out, sizeof(g_root));
  memcpy(g_rotation, rotation, sizeof(g_rotation));
  g_have_root = true;
  g_root_frame = frame;
  g_root_state = state;
  g_root_left = left;
  g_root_size = size;
  return true;
}

void __cdecl h_Compose(float* pose, void* anim, const float* root) {
  bool active = false;
  uint8_t* hand = static_cast<uint8_t*>(HandDirectHand(&active));
  if (hand == nullptr || pose == nullptr || root == nullptr ||
      pose != *reinterpret_cast<float* const*>(hand + kHandPose)) {
    o_Compose(pose, anim, root);
    return;
  }
  // The Hand's own pose.
  const int32_t state = *reinterpret_cast<const int32_t*>(hand + kHandState);
  float exact[12];
  const float* use = root;
  if (active && g_set.exact && ExactState(state) &&
      BuildRoot(hand, state, exact)) {
    use = exact;
    ++g_exact_composes;
  } else {
    ++g_game_composes;
  }
  // Only an exact compose moves the old pose. With the mouse the blend stays
  // as the game makes it.
  if (use == exact) RebaseOld(hand, pose, use);
  Remember(pose, use, use == exact);
  g_anim_hand = hand;
  g_anim = g_anim_asked;
  // THE WRIST LOCK. Before the compose, the first matrix of the pose is the
  // wrist bone of the animation in the frame of the root.
  float locked[12];
  if (use == exact) {
    if (state >= 0 && state < kMeasuredStates && !g_wrist_said[state]) {
      g_wrist_said[state] = true;
      float degrees = 0.0f, units = 0.0f;
      hx::WristOff(pose, &degrees, &units);
      ModLog("hand exact   : state %d, animation %d: the wrist bone of the "
             "animation is %.1f degrees and %.1f mesh units (%.1f cm of a "
             "hand of 19 cm) from the wrist bone of the file. The wrist lock "
             "is %s", state, g_anim, degrees, units,
             units * 19.0f / hx::kMeshLength, g_set.wrist_lock ? "on" : "off");
      // A CHECK OF THE LOCK. The Normal animation keeps the wrist of the
      // file (measured: the drawn wrist is at -229.3 -555.3 -5.0). If the
      // first matrix of the pose is not that wrist there, the mod does not
      // know the pose, and the lock stays off.
      if (state == kStateNormal && g_set.wrist_lock) {
        g_lock_ok = degrees <= 3.0f && units <= 20.0f;
        if (!g_lock_ok) {
          ModLog("hand exact   : the Normal animation does not have the wrist "
                 "of the file, so the wrist lock stays OFF");
        }
      }
    }
    if (g_set.wrist_lock && g_lock_ok &&
        hx::WristLockRoot(pose, exact, locked)) {
      use = locked;
      ++g_locked_composes;
    }
  }
  const bool is_exact = use == exact || use == locked;
  o_Compose(pose, anim, use);
  use = is_exact ? exact : root;
  if (use == exact &&
      (state == kStateNormal || state == kStateHolding ||
       (state == kStateGrain && MiraclePlacesHand()))) {
    // The held object hangs from 0x00C5E660, a blend from 0x00C5E670 to
    // 0x00C5E680. The position is the target here, so it does not lag. A
    // miracle in the Hand hangs from the same point.
    memcpy(reinterpret_cast<void*>(kHandWorldBefore),
           reinterpret_cast<const void*>(kHandWorld), sizeof(float) * 3);
  }
  if (use == exact && !g_said_first) {
    g_said_first = true;
    ModLog("hand exact   : the first exact Hand, state %d. The game's size k "
           "is %.6f, the exact k %.6f. CHand+0x8C %.2f (the stored bones "
           "give %.2f), +0x90 %.3f, +0x4834 %.4f, mirror %d, %d bones",
           state, *reinterpret_cast<const float*>(hand + kHandK),
           kHandLength * g_root_size / hx::kMeshLength,
           *reinterpret_cast<const float*>(hand + kHandBoneSpan),
           hx::kMeshBoneSpan,
           *reinterpret_cast<const float*>(hand + kHandSize1),
           *reinterpret_cast<const float*>(hand + kHandScale),
           *reinterpret_cast<const int32_t*>(hand + kHandMirror),
           BoneCount(hand));
  }
}

// The overlay's test hand in the world (overlay_math.h): a point of its hand
// frame, for the log.
void OverlayPoint(const float p[3], float out[3]) {
  const float length = kHandLength * g_root_size;
  const float* origin = &g_root[9];
  // The grip is the target less the tip offset.
  float offset[3];
  hx::TipOffset(g_rotation, g_root_left, length, offset);
  const float grip[3] = {origin[0] - offset[0], origin[1] - offset[1],
                         origin[2] - offset[2]};
  overlay_math::PlacePoint(grip, g_rotation, length, g_root_left, p, out);
}

// After CHand::PrepareForDrawing: the drawn bones against the real hand.
void CheckDrawn(const uint8_t* hand) {
  if (!g_have_root) return;
  float target[3], size = 0.0f;
  uint64_t frame = 0;
  if (!HandDirectExactFacts(target, &size, &frame) || frame != g_root_frame) {
    g_have_wrist = false;
    return;
  }
  const int32_t state = *reinterpret_cast<const int32_t*>(hand + kHandState);
  if (state != g_root_state) {
    g_have_wrist = false;
    return;
  }
  const float* pose = *reinterpret_cast<float* const*>(hand + kHandPose);
  const int n = BoneCount(hand);
  if (pose == nullptr || n <= hx::kBoneIndexLast) return;
  ++g_checked_frames;
  const float* wrist = pose + hx::kBoneWrist * 12 + 9;
  const float* middle = pose + hx::kBoneMiddleLast * 12 + 9;
  const float* index = pose + hx::kBoneIndexLast * 12 + 9;
  // THE DRIFT. The wrist bone in the Hand's own frame barely moves from one
  // frame to the next. A blend that drags the Hand moves it there.
  float wrist_model[3];
  if (!hx::WorldToModel(g_root, wrist, wrist_model)) return;
  const float table = TabletopViewScale();
  const float units_per_cm = (table > 0.0f ? table : HeadPoseScale()) * 0.01f;
  float drift_cm = 0.0f;
  if (g_have_wrist && units_per_cm > 0.0f) {
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = wrist_model[i] - g_wrist_model[i];
    const float k = kHandLength * g_root_size / hx::kMeshLength;
    drift_cm = hx::Length(d) * k / units_per_cm;
    if (drift_cm > g_worst_drift_cm) g_worst_drift_cm = drift_cm;
    if (drift_cm > 0.5f) ++g_drift_frames;
  }
  memcpy(g_wrist_model, wrist_model, sizeof(g_wrist_model));
  g_have_wrist = true;

  // THE MEASUREMENT of the animation's rest pose, once for each state, 30
  // passes after the last blend: the bones in the stored mesh frame, beside
  // the bind pose of the file.
  const float blend = *reinterpret_cast<const float*>(hand + kHandBlendTime);
  g_steady_passes = blend < kBlendSeconds ? 0 : g_steady_passes + 1;
  if (state >= 0 && state < kMeasuredStates && !g_measured[state] &&
      g_steady_passes >= 30) {
    g_measured[state] = true;
    float m0[3], m10[3], m19[3], m17[3], m11[3];
    hx::WorldToModel(g_root, wrist, m0);
    hx::WorldToModel(g_root, middle, m10);
    hx::WorldToModel(g_root, index, m19);
    hx::WorldToModel(g_root, pose + 17 * 12 + 9, m17);
    hx::WorldToModel(g_root, pose + 11 * 12 + 9, m11);
    const float line[3] = {m10[0] - m0[0], m10[1] - m0[1], m10[2] - m0[2]};
    const float lean_thumb =
        atan2f(line[0], line[1]) * 57.29578f;
    const float lean_palm =
        atan2f(line[2], sqrtf(line[0] * line[0] + line[1] * line[1])) *
        57.29578f;
    const float knuckles[3] = {m17[0] - m11[0], m17[1] - m11[1],
                               m17[2] - m11[2]};
    const float knuckle_tilt =
        atan2f(knuckles[2], knuckles[0]) * 57.29578f;
    ModLog("hand exact   : the drawn bones in the stored mesh frame, state %d: "
           "wrist %.1f %.1f %.1f (stored -229.3 -555.3 -4.9), middle last "
           "joint %.1f %.1f %.1f (stored -123.7 -18.6 -19.0), index last "
           "joint %.1f %.1f %.1f (stored -29.5 -61.0 -18.2)",
           state, m0[0], m0[1], m0[2], m10[0], m10[1], m10[2], m19[0], m19[1],
           m19[2]);
    ModLog("hand exact   : the wrist to the middle finger's last joint leans "
           "%.1f degrees toward the thumb (stored 11.1) and %.1f toward the "
           "palm (stored -1.5). The knuckle line from the little finger to "
           "the index finger tilts %.1f degrees toward the palm (stored 0.1)",
           lean_thumb, lean_palm, knuckle_tilt);
  }

  // THE CHECK against the overlay's real hand, now and then: the wrist and
  // the index finger's last joint, and the angle of the hand's line.
  const bool changed = state != g_logged_state;
  if (changed) {
    g_logged_state = state;
    g_after_state_change = 3;
  }
  const bool sample = g_root_frame >= g_last_check_frame + kCheckFrames;
  if (g_after_state_change > 0) --g_after_state_change;
  if (!changed && !sample && g_after_state_change == 0) return;
  if (g_check_logs >= kMaxCheckLogs) return;
  ++g_check_logs;
  g_last_check_frame = g_root_frame;
  float joints[overlay_math::kJoints + 1][3];
  overlay_math::FingerJoints(1, overlay_math::kRestCurl, joints);
  float middle_joints[overlay_math::kJoints + 1][3];
  overlay_math::FingerJoints(2, overlay_math::kRestCurl, middle_joints);
  const float real_wrist_local[3] = {overlay_math::kPalmX,
                                     overlay_math::kKnuckleY +
                                         overlay_math::kPalmLength,
                                     0.0f};
  float real_wrist[3], real_index[3], real_middle[3];
  OverlayPoint(real_wrist_local, real_wrist);
  OverlayPoint(joints[2], real_index);
  OverlayPoint(middle_joints[2], real_middle);
  const float drawn_line[3] = {middle[0] - wrist[0], middle[1] - wrist[1],
                               middle[2] - wrist[2]};
  const float real_line[3] = {real_middle[0] - real_wrist[0],
                              real_middle[1] - real_wrist[1],
                              real_middle[2] - real_wrist[2]};
  float wrist_gap[3], index_gap[3];
  for (int i = 0; i < 3; ++i) {
    wrist_gap[i] = wrist[i] - real_wrist[i];
    index_gap[i] = index[i] - real_index[i];
  }
  const float timer = *reinterpret_cast<const float*>(hand + kHandBlendTime);
  ModLog("hand exact   : state %d%s, frame %llu. The drawn wrist is %.1f cm "
         "from the overlay's wrist, the index finger's last joint %.1f cm from "
         "the overlay's. The line from the wrist to the middle finger is "
         "%.1f degrees off the overlay's. The wrist moved %.2f cm in the "
         "Hand's frame since the last frame (the worst %.2f). Blend %.3f s",
         state, changed ? " (new)" : "", g_root_frame,
         units_per_cm > 0.0f ? hx::Length(wrist_gap) / units_per_cm : 0.0f,
         units_per_cm > 0.0f ? hx::Length(index_gap) / units_per_cm : 0.0f,
         hx::AngleDeg(drawn_line, real_line), drift_cm, g_worst_drift_cm,
         timer < kBlendSeconds ? timer : kBlendSeconds);
}

// THE MESH IS HIDDEN. True for the Hand's own mesh while the controller places
// the Hand in a state where the mod's hand stands for it, and the overlay drew
// the pointing hand in this frame. So one hand is always there.
bool HideNow(void* mesh) {
  bool active = false;
  const uint8_t* hand = static_cast<const uint8_t*>(HandDirectHand(&active));
  if (!active || hand == nullptr) return false;
  if (*reinterpret_cast<void* const*>(hand + kHandMesh) != mesh) return false;
  const int32_t state = *reinterpret_cast<const int32_t*>(hand + kHandState);
  return ExactState(state) && OverlayPointingHandDrawn();
}

// The draw of a mesh of the Hand's class. It runs in each eye pass, and it
// reads only values that are the same in both passes of a frame.
void __fastcall h_MeshDraw(void* mesh, void* unused) {
  if (g_hide_live) {
    const uintptr_t back = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if ((back == kDrawReturnA || back == kDrawReturnB) && HideNow(mesh)) {
      ++g_hidden_draws;
      if (!g_said_hidden) {
        g_said_hidden = true;
        ModLog("hand exact   : the mesh of the game's Hand is not drawn (the "
               "call from 0x%08X), and the mod's hand stands for it",
               static_cast<unsigned>(back - 6));
      }
      return;
    }
  }
  o_MeshDraw(mesh, unused);
}

// The animation that a Morphable asks for. Only the Hand's number is kept.
void* __fastcall h_GetAnim(void* self, void* unused, int animation,
                           int second) {
  if (self == g_anim_hand) g_anim_asked = animation;
  return o_GetAnim(self, unused, animation, second);
}

// The Get2DRadius of an object, or 0 when it gives no sane value. Asked once
// for each object.
float Radius2D(void* object) {
  if (object == g_radius_object) return g_radius_2d;
  g_radius_object = object;
  g_radius_2d = 0.0f;
  void** const* vtable = reinterpret_cast<void** const*>(object);
  if (IsBadReadPtr(vtable, sizeof(void*)) ||
      IsBadReadPtr(*vtable, kGet2DRadius + sizeof(void*))) {
    return 0.0f;
  }
  typedef float(__fastcall* RadiusFn)(void* self, void* unused);
  const float radius = reinterpret_cast<RadiusFn>(
      (*vtable)[kGet2DRadius / 4])(object, nullptr);
  if (radius > 0.0f && radius < 1000.0f) g_radius_2d = radius;
  return g_radius_2d;
}

// THE RADIUS OF THE BODY AT THE PALM. The 2D radius of a tree is the radius
// of its crown, and the Hand holds a tree near its base. With that radius
// the trunk of a large tree is 3 to 6 cm in front of the palm, and the
// fingers make a cup under it (docs/hand-model.md, "Hands that react to the
// world"). So a tree has the game's own hold radius, Object::GetHoldRadius
// (slot 0x590): 0.2 times the 2D radius, the trunk [V Tree::GetHoldRadius
// 0x0074B610]. Object::GetHoldType (slot 0x58C) gives 5 for a tree and a
// dead tree. Each other object keeps its 2D radius. With hand_hold_trunk=0
// a tree keeps it too.
const int kGetHoldType = 0x58C;
const int kGetHoldRadius = 0x590;
const int kHoldTypeTree = 5;
float HeldRadius(void* held) {
  const float radius = Radius2D(held);
  if (!g_set.tree_trunk) return radius;
  void*** vtable = static_cast<void***>(held);
  if (IsBadReadPtr(vtable, sizeof(void*)) ||
      IsBadReadPtr(*vtable, kGetHoldRadius + sizeof(void*))) {
    return radius;
  }
  typedef int(__fastcall* TypeFn)(void* self, void* unused);
  typedef float(__fastcall* RadiusFn)(void* self, void* unused);
  if (reinterpret_cast<TypeFn>((*vtable)[kGetHoldType / 4])(held, nullptr) !=
      kHoldTypeTree) {
    return radius;
  }
  const float trunk =
      reinterpret_cast<RadiusFn>((*vtable)[kGetHoldRadius / 4])(held, nullptr);
  return trunk > 0.0f && trunk < radius ? trunk : radius;
}

// The matrix of the held object's mesh (4 rows of 3), or null.
float* HeldMatrix(void* held) {
  uint8_t* const* link = reinterpret_cast<uint8_t* const*>(
      static_cast<uint8_t*>(held) + kObjectMesh);
  if (IsBadReadPtr(link, sizeof(void*)) || *link == nullptr) return nullptr;
  float* matrix = reinterpret_cast<float*>(*link + kMeshMatrix);
  return IsBadWritePtr(matrix, 12 * sizeof(float)) ? nullptr : matrix;
}

// THE HELD OBJECT IN THE PALM, after CHand::PrepareForDrawing in each eye
// pass. The place is made once a frame, and both passes write the same
// matrix. The game writes the translation again in each pass, and it leaves
// the axes as they are, so the mod keeps the axes that it wrote last and
// knows when the game changed them.
void PlaceHeld(uint8_t* hand) {
  const int32_t state = *reinterpret_cast<const int32_t*>(hand + kHandState);
  void* held = *reinterpret_cast<void* const*>(hand + kHandHeld);
  if (!g_set.hold_palm || state != kStateHolding || held == nullptr) {
    g_hold.object = nullptr;
    return;
  }
  // The release physics has the object: the game does not place it either.
  if (*reinterpret_cast<const int32_t*>(kPredictionActive) != 0 &&
      *reinterpret_cast<void* const*>(kPredictionObject) == held) {
    return;
  }
  float target[3], size = 0.0f, rotation[9];
  uint64_t frame = 0;
  bool left = false;
  if (!HandDirectExactFacts(target, &size, &frame) ||
      !ControllersExactRotation(rotation, &left) || !(size > 0.0f)) {
    g_hold.object = nullptr;
    return;
  }
  float* matrix = HeldMatrix(held);
  if (matrix == nullptr) return;
  const bool fresh = held != g_hold.object;
  if (fresh) {
    g_hold = Hold();
    g_hold.object = held;
    g_hold.radius_2d = HeldRadius(held);
    memcpy(g_hold.rotation0, rotation, sizeof(g_hold.rotation0));
    memcpy(g_hold.base, matrix, sizeof(g_hold.base));
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    g_hold.start = start.QuadPart;
  } else if (g_hold.have_wrote &&
             memcmp(matrix, g_hold.wrote, sizeof(g_hold.wrote)) != 0) {
    // The game gave the object new axes. They are the axes to turn now, by
    // the turn of the hand since the object came into it.
    memcpy(g_hold.base, matrix, sizeof(g_hold.base));
  }
  if (!g_hold.have_out || g_hold.frame != frame) {
    const float length = kHandLength * size;
    const float* hang = reinterpret_cast<const float*>(kHangOffset);
    g_hold.radius = hx::BodyRadius(hang, g_hold.radius_2d);
    float offset[3], grip[3], d[9];
    hx::TipOffset(rotation, left, length, offset);
    for (int i = 0; i < 3; ++i) grip[i] = target[i] - offset[i];
    const float same[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                           0.0f, 0.0f, 0.0f, 1.0f};
    if (g_set.hold_turn) {
      hx::TurnSince(rotation, g_hold.rotation0, d);
    } else {
      memcpy(d, same, sizeof(d));
    }
    float origin[3];
    hx::HoldOrigin(grip, rotation, length, left, g_hold.radius, hang, d,
                   origin);
    // THE SETTLE: from the place that the game gave on the first frame.
    if (fresh) {
      const float gap[3] = {matrix[9] - origin[0], matrix[10] - origin[1],
                            matrix[11] - origin[2]};
      for (int c = 0; c < 3; ++c) {
        g_hold.offset_local[c] = rotation[0 * 3 + c] * gap[0] +
                                 rotation[1 * 3 + c] * gap[1] +
                                 rotation[2 * 3 + c] * gap[2];
      }
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const float t = g_frequency.QuadPart > 0
                        ? static_cast<float>(
                              static_cast<double>(now.QuadPart - g_hold.start) /
                              static_cast<double>(g_frequency.QuadPart))
                        : hx::kSettleSeconds;
    const float share = hx::SettleShare(t);
    float left_over[3];
    hx::Turn(rotation, g_hold.offset_local, left_over);
    for (int r = 0; r < 3; ++r) {
      hx::Turn(d, &g_hold.base[r * 3], &g_hold.out[r * 3]);
      g_hold.out[9 + r] = origin[r] + share * left_over[r];
    }
    g_hold.frame = frame;
    g_hold.have_out = true;
    ++g_holds_placed;
    if (fresh && g_hold_logs < kMaxHoldLogs) {
      ++g_hold_logs;
      const float table = TabletopViewScale();
      const float units_per_cm =
          (table > 0.0f ? table : HeadPoseScale()) * 0.01f;
      const float gap[3] = {matrix[9] - origin[0], matrix[10] - origin[1],
                            matrix[11] - origin[2]};
      ModLog("hand exact   : the Hand holds %p, frame %llu. The body is a ball "
             "of %.2f units (2D radius or trunk %.2f, hang %.2f %.2f %.2f), "
             "%.1f cm of the room. The game put the object at %.1f %.1f %.1f, "
             "and its "
             "place in the palm is %.1f %.1f %.1f, %.1f cm away. It moves "
             "there in %.2f s and turns with the hand: %s",
             held, frame, g_hold.radius, g_hold.radius_2d, hang[0], hang[1],
             hang[2],
             units_per_cm > 0.0f ? g_hold.radius / units_per_cm : 0.0f,
             matrix[9], matrix[10], matrix[11], origin[0], origin[1],
             origin[2],
             units_per_cm > 0.0f ? hx::Length(gap) / units_per_cm : 0.0f,
             hx::kSettleSeconds, g_set.hold_turn ? "yes" : "no");
    }
  }
  if (g_set.hold_turn) {
    memcpy(matrix, g_hold.out, 9 * sizeof(float));
    memcpy(g_hold.wrote, g_hold.out, sizeof(g_hold.wrote));
    g_hold.have_wrote = true;
  }
  for (int i = 0; i < 3; ++i) matrix[9 + i] = g_hold.out[9 + i];
}

// AN A/B IN THE HEADSET. With test_keys=1, the minus key of the number pad
// shows and hides the mesh of the game's Hand while the game runs, so the
// player can put the game's Hand beside the mod's hand. The pointing
// controller gives a long pulse for hidden and a short pulse for shown. The
// key is read once a frame, only while a window of the game is in front, and
// the mod does not take it from the game. The minus key with Ctrl or Shift is
// a key of hand_model.cpp, so it does nothing here.
void PollHideKey() {
  if (!g_set.test_keys || !g_mesh_hooked) return;
  const uint64_t frame = ControllersFrameCount();
  if (frame == g_key_frame) return;
  g_key_frame = frame;
  const bool down = (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0 &&
                    (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0 &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0;
  const bool pressed = down && !g_key_down;
  g_key_down = down;
  if (!pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  g_hide_live = !g_hide_live;
  ControllersGesturePulse(0.5f, g_hide_live ? 0.30f : 0.08f);
  ModLog("hand exact   : the minus key: the mesh of the game's Hand is %s, "
         "frame %llu", g_hide_live ? "HIDDEN" : "SHOWN", frame);
}

bool SameBytes(uintptr_t address, const uint8_t* want, int count) {
  const void* at = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(at, count) && memcmp(at, want, count) == 0;
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

// ONE PAIR OF HANDS: the hooks. Each one only after the bytes that the
// disassembly of runblack.exe gave are there.
void InstallOnePair() {
  const uint8_t draw[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56};
  const uint8_t call_a[] = {0xFF, 0x90, 0x08, 0x01, 0x00, 0x00};
  const uint8_t call_b[] = {0xFF, 0x92, 0x08, 0x01, 0x00, 0x00};
  const uint8_t get_anim[] = {0x8B, 0x81, 0x98, 0x00, 0x00, 0x00, 0x83, 0xF8};
  const uintptr_t* slot =
      reinterpret_cast<const uintptr_t*>(kMeshVtable + kMeshDrawSlot);
  if (g_set.hide) {
    const bool same = SameBytes(kMeshDraw, draw, sizeof(draw)) &&
                      SameBytes(kDrawCallA, call_a, sizeof(call_a)) &&
                      SameBytes(kDrawCallB, call_b, sizeof(call_b)) &&
                      !IsBadReadPtr(slot, sizeof(*slot)) && *slot == kMeshDraw;
    if (!same) {
      ModLog("hand exact   : the draw of the Hand's mesh is not as the mod "
             "knows it, so the game's Hand stays visible");
    } else {
      g_mesh_hooked = Hook(kMeshDraw, reinterpret_cast<void*>(&h_MeshDraw),
                           reinterpret_cast<void**>(&o_MeshDraw),
                           "the draw of the Hand's mesh (hidden for the "
                           "controller)");
    }
  }
  g_hide_live = g_mesh_hooked;
  if (SameBytes(kGetAnim, get_anim, sizeof(get_anim))) {
    g_anim_hooked = Hook(kGetAnim, reinterpret_cast<void*>(&h_GetAnim),
                         reinterpret_cast<void**>(&o_GetAnim),
                         "Morphable::GetAnim (the Hand's animation number)");
  }
  ModLog("hand exact   : one pair of hands. hand_hide_game=%d: the mesh of "
         "the game's Hand is %s while the controller places the Hand. "
         "hand_wrist_lock=%d, hand_hold_palm=%d, hand_hold_turn=%d, "
         "hand_tint=%d%s",
         g_set.hide ? 1 : 0, g_hide_live ? "hidden" : "drawn",
         g_set.wrist_lock ? 1 : 0, g_set.hold_palm ? 1 : 0,
         g_set.hold_turn ? 1 : 0, g_set.tint ? 1 : 0,
         g_set.test_keys && g_mesh_hooked
             ? ". test_keys=1: the minus key of the number pad shows and "
               "hides the mesh"
             : "");
}

}  // namespace

// hand_direct.cpp, after its own hooks. Reads the keys and hooks the compose.
// With hand_exact=0 nothing is hooked.
void HandExactInstall() {
  static bool tried = false;
  if (tried) return;
  tried = true;
  LoadSettings();
  if (!g_set.exact) {
    ModLog("hand exact   : hand_exact=0, the game's Hand turns with the screen "
           "ray as before");
    return;
  }
  g_installed = Hook(kCompose, reinterpret_cast<void*>(&h_Compose),
                     reinterpret_cast<void**>(&o_Compose),
                     "the Hand pose compose (the exact Hand)");
  if (!g_installed) return;
  QueryPerformanceFrequency(&g_frequency);
  InstallOnePair();
  ModLog("hand exact   : on. The game's Hand is the real hand: its index tip "
         "at the real index tip, turned with the controller. The rest turn "
         "of the mesh is yaw %.2f, pitch %.2f, roll %.2f degrees",
         g_set.yaw, g_set.pitch, g_set.roll);
  if (g_set.left_mesh) {
    ModLog("hand exact   : hand_left_mesh=1 does nothing. With hand_model=1 "
           "the mod draws both hands with the mesh of the game");
  }
}

// For controllers.cpp and overlay.cpp: the exact Hand is on. The point of
// the Hand is then the real index tip, and the overlay draws the other hand.
bool HandExactOn() { return g_installed && g_set.exact; }

// pass_guard.cpp, after CHand::PrepareForDrawing in each pass.
void HandExactAfterPrepare(void* hand) {
  if (hand == nullptr) return;
  bool active = false;
  void* ours = HandDirectHand(&active);
  if (g_installed) PollHideKey();
  if (g_installed && active && hand == ours) {
    CheckDrawn(static_cast<const uint8_t*>(hand));
    // Before the hooks: the glide of a far grab and the first frames of a
    // throw start from the place in the palm.
    PlaceHeld(static_cast<uint8_t*>(hand));
  } else {
    g_hold.object = nullptr;
  }
  for (int i = 0; i < g_prepare_hook_count; ++i) g_prepare_hooks[i](hand);
}

// For overlay.cpp: the game's Hand of this frame, for the pose and the color
// of the mod's hand. Called once a frame, before the eye passes, so the state
// is the one of the last frame.
bool HandExactGetContext(HandExactContext* out) {
  memset(out, 0, sizeof(*out));
  out->state = -1;
  out->animation = -1;
  bool active = false;
  const uint8_t* hand = static_cast<const uint8_t*>(HandDirectHand(&active));
  if (hand == nullptr) return false;
  out->placed = active;
  out->state = *reinterpret_cast<const int32_t*>(hand + kHandState);
  out->animation = g_anim_hooked ? g_anim : -1;
  void* held = *reinterpret_cast<void* const*>(hand + kHandHeld);
  out->holding = held != nullptr;
  if (held != nullptr) {
    out->hold_radius =
        held == g_hold.object && g_hold.radius > 0.0f
            ? g_hold.radius
            : hx::BodyRadius(reinterpret_cast<const float*>(kHangOffset),
                             HeldRadius(held));
  }
  const float alignment = *reinterpret_cast<const float*>(hand + kHandMorph);
  out->have_alignment = alignment >= -1.0f && alignment <= 1.0f;
  out->alignment = out->have_alignment ? alignment : 0.0f;
  return true;
}

bool HandExactHidesGame() { return g_installed && g_hide_live; }

// For the leash (hand_direct.cpp, docs/grab.md, "The leash at the mod's
// hand"): the middle of the palm of the mod's hand in the world, from the
// exact root of the last compose, with the frame and the Hand state of that
// compose. It is the point where the ball of a held object touches the palm
// (overlay_math::HoldCenter). False until an exact compose ran, and while the
// mesh of the game's Hand shows: the game's own Hand holds the rope then.
bool HandExactPalm(float out[3], uint64_t* frame, int32_t* state) {
  if (!g_installed || !g_set.exact || !g_have_root || !g_hide_live ||
      !ExactState(g_root_state) || !OverlayPointingHandDrawn()) {
    return false;
  }
  float palm[3];
  overlay_math::HoldCenter(0.0f, palm);
  OverlayPoint(palm, out);
  *frame = g_root_frame;
  *state = g_root_state;
  return true;
}

bool HandExactTint() { return g_set.tint; }

// For hand_feel.cpp (docs/hand-model.md, "Hands that react to the world"): the
// object in the Hand as the mod placed it in the palm last (PlaceHeld): the
// object, its origin in the world and the direction of its own up axis.
// False when the mod does not place an object.
bool HandExactHeld(void** object, float origin[3], float up[3]) {
  if (g_hold.object == nullptr || !g_hold.have_out) return false;
  *object = g_hold.object;
  for (int i = 0; i < 3; ++i) {
    origin[i] = g_hold.out[9 + i];
    up[i] = g_hold.out[3 + i];
  }
  return hx::Normalize(up);
}

// For a later feature, for example the glide of a far grab: a call after
// CHand::PrepareForDrawing, where the drawn bones and the held object's
// translation are set. False when the list is full.
bool HandExactAddPrepareHook(void (*hook)(void* hand)) {
  if (hook == nullptr || g_prepare_hook_count >= kMaxPrepareHooks) return false;
  g_prepare_hooks[g_prepare_hook_count++] = hook;
  return true;
}

// Once a report window (hand_direct.cpp calls it from its report).
void HandExactReport() {
  if (!g_installed) return;
  if (g_exact_composes == 0 && g_game_composes == 0) return;
  ModLog("hand exact   : %llu exact composes, %llu as the game made them, %llu "
         "old poses moved to the new root, %llu blends ended with no known "
         "root. %llu passes checked, %llu with the wrist moved more than 0.5 "
         "cm in the Hand's frame (the worst %.2f cm)",
         g_exact_composes, g_game_composes, g_rebases, g_blends_ended,
         g_checked_frames, g_drift_frames, g_worst_drift_cm);
  ModLog("hand exact   : %llu composes with the wrist lock, %llu draws of the "
         "Hand's mesh hidden, %llu frames with the held object in the palm. "
         "The last animation of the Hand is %d",
         g_locked_composes, g_hidden_draws, g_holds_placed, g_anim);
  g_locked_composes = g_hidden_draws = g_holds_placed = 0;
  g_exact_composes = g_game_composes = g_rebases = g_blends_ended = 0;
  g_checked_frames = g_drift_frames = 0;
  g_worst_drift_cm = 0.0f;
}
