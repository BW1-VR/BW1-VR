// THE FRAME BEFORE OF EACH EYE. See docs/stereo-artifacts.md, "The frame
// before of each eye".
//
// THE GAME DRAWS EACH FRAME OVER THE FRAME BEFORE. Its render target keeps
// its picture from one frame to the next. After each present the game calls
// one clean function (fn_0082EE70, from LHScreen::Clear), and the cleaning
// mode LH3DRender::g_mode_cleaning (0x00C3871C) says what that clean does:
//
//   1  the picture and the Z buffer. The menus, the load screens and the
//      logo screens.
//   2  the Z buffer only. The Lionhead logo: its sky has an alpha, so the
//      frame before shows through it (intro_vr.cpp).
//   0  nothing. All of play: GLandscape::Draw writes 0 in each frame
//      (0x005E48A4), and so does the scene walk for the Temple (0x0054DE1F)
//      and for the video of a falling spell (0x0054DDB5). The sky dome is the
//      clean of such a frame: it draws first, with no blend, with ZFUNC
//      ALWAYS and with the Z write on. In mode 0 a timer of the clean
//      function also makes the clean of mode 1 for two frames in each 2
//      seconds.
//
// THE TWO EYE PASSES SHARE THAT RENDER TARGET, and the game cleans it one
// time for a frame. So eye pass 0 draws over the picture of eye pass 1 of the
// frame before, and eye pass 1 draws over the picture of eye pass 0 of the
// same frame. Each pixel that a pass does not cover with a draw of no blend
// keeps the picture of the other eye. For the left eye that picture is one
// frame old. For the right eye it is of the same moment.
//
// THE RULE (eye_clean=1). Each eye pass of the scene starts from its own
// frame before (eye_clean_math.h, Plan):
//
//   - Eye pass 1 gets the clean that the game made before eye pass 0. A hook
//     on the clean function records what each call did.
//   - In mode 0 the mod cleans the picture and the Z buffer of each eye pass
//     just before its sky (GLandAlignement::DrawSky 0x005E2160), with the
//     clean call and the color of the game. A pixel that the scene does not
//     cover then has that color in each eye, and no old picture.
//   - When the frame before stays in view (mode 2, or mode 0 in a frame that
//     draws no sky: the video of a falling spell), each eye has a keep of its
//     picture (gpu_bridge.cpp). The keep of an eye goes into the render
//     target before its pass, and the picture goes into the keep after it.
//     The mod knows that a frame draws no sky from the frame before it, so
//     the first frame of such an effect is drawn as with eye_clean=0. The
//     keeps are free again when the passes draw the sky.
//
// With eye_clean=0 the mod does none of this.
//
// The Lionhead logo draws its own pair of frames, each with its own present
// and its own clean of the game. Its keeps are in intro_vr.cpp
// (intro_eye_trails).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "eye_clean_math.h"

extern void ModLog(const char* format, ...);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);
// gpu_bridge.cpp: the keep of the picture of each eye.
bool GpuBridgeKeepTake(int eye);
bool GpuBridgeKeepGive(int eye);
bool GpuBridgeKeepHas(int eye);
void GpuBridgeKeepFree();

namespace {

using namespace eye_clean;

const uintptr_t kModeCleaning = 0x00C3871C;  // LH3DRender::g_mode_cleaning
// The clean function of the game: __cdecl, no argument, a plain ret.
const uintptr_t kCleanFunction = 0x0082EE70;
const uintptr_t kCleanStamp = 0x00ECA67C;    // the time of its last timed clean
const uintptr_t kCleanFlag = 0x00ECA680;     // its second timed clean is due
const uintptr_t kEnableCallbacks = 0x00C386D0;  // byte, g_enable_callbacks
const uintptr_t kClearColor = 0x00EC7F08;    // the color of each clean
// GLandAlignement::DrawSky: __cdecl, no argument. It ends in a jump to
// LH3DSky::Draw 0x0086B010. GLandscape::Draw calls it (0x005E48AE), and the
// scene walk calls it for the Temple (0x0054DE34).
const uintptr_t kDrawSky = 0x005E2160;
const uintptr_t kRenderFrame = 0x00ECA640;   // LH3DRender::g_frame
// GGame: the kind of scene that the walk draws (0 the land, 1 the Temple, 2
// the video of a falling spell), and the video that plays.
const uintptr_t kGameSceneKind = 0x205A28;
const uintptr_t kGameVideo = 0x250188;
// LH3DSky::Draw pushes the alpha of the first layer of the dome here:
// "push 0xFF" (68 FF 00 00 00), then the "jle" of the two layers.
const uintptr_t kSkyAlphaPush = 0x0086B5BA;

// IDirect3DDevice7::Clear, slot 10.
typedef HRESULT(__stdcall* ClearFn)(void* device, DWORD count, void* rects,
                                    DWORD flags, DWORD color, float z,
                                    DWORD stencil);
const int kClearSlot = 10;
const DWORD kClearPicture = 1;  // D3DCLEAR_TARGET
const DWORD kClearDepth = 2;    // D3DCLEAR_ZBUFFER

typedef void(__cdecl* VoidFn)();
VoidFn o_Clean = nullptr;
VoidFn o_DrawSky = nullptr;

// eye_clean_test
const int kTestOff = 0;
// The way of the Lionhead logo, in play: the game cleans only the Z buffer,
// and the sky dome has an alpha of 160 of 255.
const int kTestLogoWay = 1;
// The clean before the sky has the color FF00FF, so a picture of an eye
// shows each pixel that the scene does not cover.
const int kTestMarker = 2;
const DWORD kMarkerColor = 0x00FF00FF;
const uint8_t kTestSkyAlpha = 160;

int g_key = kOwn;         // eye_clean
int g_test = kTestOff;    // eye_clean_test
int g_test_frame = 0;     // eye_clean_test_frame
bool g_installed = false;
bool g_sky_hooked = false;

// THE CLEAN OF THE GAME, from the hook. The largest clean since the last
// pair of eye passes, and the mode of the last call.
Cleaned g_cleaned = kCleanedNothing;
int g_clean_mode = kModeAll;
bool g_have_clean = false;
bool g_pair_since_clean = false;

// THE PAIR THAT RUNS. The mode, the clean and the cover are latched before
// eye pass 0, so both passes have the same plan.
int g_eye = -1;
int g_mode = kModeAll;
Cleaned g_pair_cleaned = kCleanedNothing;
bool g_covered = true;       // each pass of the pair before drew the sky
bool g_sky_in_pass = false;
bool g_sky[2] = {false, false};
PassPlan g_plan;
uint32_t g_frame_after_pair = 0;
bool g_have_frame_after = false;

// What the log has.
int g_said_mode = -1;
bool g_said_covered = true;
bool g_said_no_keep = false;
bool g_said_clean_failed = false;

// For the report.
uint64_t g_pairs = 0;
uint64_t g_game_cleans[3] = {0, 0, 0};  // by Cleaned
uint64_t g_own_cleans[3] = {0, 0, 0};   // before eye pass 1, by Cleaned
uint64_t g_sky_cleans = 0;
uint64_t g_no_sky_passes = 0;
uint64_t g_gives = 0;
uint64_t g_takes = 0;

template <typename T>
T* At(uintptr_t address) {
  return reinterpret_cast<T*>(address);
}

const char* ModeText(int mode) {
  switch (mode) {
    case kModeNone: return "0, no clean: the sky is the clean of a frame";
    case kModeAll: return "1, the picture and the Z buffer";
    case kModeDepth: return "2, the Z buffer only: the frame before stays";
    default: return "not a known mode";
  }
}

const char* KeyText(int key) {
  return key == kOwn
             ? "each eye pass of the scene starts from its own frame before"
             : "the two eye passes share the render target, and the game "
               "cleans it one time for a frame";
}

// A field of GGame, or 0 when there is no game object.
uint32_t GameField(uintptr_t offset) {
  const uintptr_t* game = At<const uintptr_t>(bw::game::kGGame);
  if (IsBadReadPtr(game, sizeof(uintptr_t)) || *game == 0) return 0;
  const uint32_t* field = reinterpret_cast<const uint32_t*>(*game + offset);
  if (IsBadReadPtr(field, sizeof(uint32_t))) return 0;
  return *field;
}

// The clean call of the game, on its own device.
bool CleanNow(Cleaned what, DWORD color) {
  if (what == kCleanedNothing) return true;
  void* const* device = At<void* const>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(device, sizeof(void*)) || *device == nullptr) return false;
  void* const* slots = *reinterpret_cast<void* const* const*>(*device);
  const ClearFn clear = reinterpret_cast<ClearFn>(slots[kClearSlot]);
  const DWORD flags =
      what == kCleanedAll ? (kClearPicture | kClearDepth) : kClearDepth;
  const HRESULT result = clear(*device, 0, nullptr, flags, color, 1.0f, 0);
  if (FAILED(result) && !g_said_clean_failed) {
    g_said_clean_failed = true;
    ModLog("eye clean    : the clean call of the game failed (0x%08X), frame "
           "%llu", static_cast<unsigned>(result), ControllersFrameCount());
  }
  return SUCCEEDED(result);
}

DWORD GameColor() { return *At<const DWORD>(kClearColor); }

void FreeKeeps(const char* why) {
  if (!GpuBridgeKeepHas(0) && !GpuBridgeKeepHas(1)) return;
  GpuBridgeKeepFree();
  ModLog("eye clean    : %s, so the keeps of the eye pictures are free, "
         "frame %llu", why, ControllersFrameCount());
}

// The clean function of the game, after each present.
void __cdecl h_Clean() {
  int32_t* mode = At<int32_t>(kModeCleaning);
  // The test of the logo's way: the clean after a frame of the scene has
  // mode 2. GLandscape::Draw wrote 0 in that frame.
  if (g_test == kTestLogoWay && g_pair_since_clean) *mode = kModeDepth;
  const int now = *mode;
  const bool callbacks = *At<const uint8_t>(kEnableCallbacks) != 0;
  const uint32_t stamp = *At<const uint32_t>(kCleanStamp);
  const uint32_t flag = *At<const uint32_t>(kCleanFlag);
  o_Clean();
  const Cleaned did =
      GameCleaned(now, callbacks, stamp, *At<const uint32_t>(kCleanStamp),
                  flag, *At<const uint32_t>(kCleanFlag));
  g_cleaned = More(g_cleaned, did);
  g_clean_mode = now;
  g_have_clean = true;
  g_pair_since_clean = false;
  ++g_game_cleans[did];
}

// The sky of a frame. In an eye pass with the plan for it, the mod cleans
// the picture and the Z buffer first. Only the first sky of a pass has the
// clean: a second one must not take the scene away.
void __cdecl h_DrawSky() {
  if (g_eye >= 0) {
    if (!g_sky_in_pass && g_plan.clean_at_sky) {
      if (CleanNow(kCleanedAll,
                   g_test == kTestMarker ? kMarkerColor : GameColor())) {
        ++g_sky_cleans;
      }
    }
    g_sky_in_pass = true;
  }
  o_DrawSky();
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

// The test of the logo's way: the first layer of the sky dome gets an alpha
// of 160, as the sky of the Lionhead logo has. LH3DSky::Draw then draws it
// with the blend on, over the frame before.
bool PatchSkyAlpha() {
  static const uint8_t expected[7] = {0x68, 0xFF, 0x00, 0x00, 0x00, 0x7E, 0x1F};
  uint8_t* site = At<uint8_t>(kSkyAlphaPush);
  if (IsBadReadPtr(site, sizeof(expected)) ||
      memcmp(site, expected, sizeof(expected)) != 0) {
    return false;
  }
  DWORD old = 0;
  if (!VirtualProtect(site, sizeof(expected), PAGE_EXECUTE_READWRITE, &old)) {
    return false;
  }
  site[1] = kTestSkyAlpha;
  VirtualProtect(site, sizeof(expected), old, &old);
  FlushInstructionCache(GetCurrentProcess(), site, sizeof(expected));
  return true;
}

void Toggle(const char* cause) {
  g_key = g_key == kOwn ? kShared : kOwn;
  if (g_key == kShared) GpuBridgeKeepFree();
  ControllersGesturePulse(0.5f, g_key == kOwn ? 0.30f : 0.08f);
  ModLog("eye clean    : %s: eye_clean is %d now (%s), frame %llu", cause,
         g_key, KeyText(g_key), ControllersFrameCount());
}

}  // namespace

void EyeCleanSetOptions(int key, int test, int test_frame) {
  g_key = key != 0 ? kOwn : kShared;
  g_test = test == kTestLogoWay || test == kTestMarker ? test : kTestOff;
  g_test_frame = test_frame;
}

void EyeCleanInstall() {
  if (g_installed) return;
  if (IsBadWritePtr(At<int32_t>(kModeCleaning), sizeof(int32_t)) ||
      IsBadReadPtr(At<uint32_t>(kCleanStamp), sizeof(uint32_t) * 2) ||
      IsBadReadPtr(At<uint8_t>(kEnableCallbacks), 1) ||
      IsBadReadPtr(At<DWORD>(kClearColor), sizeof(DWORD)) ||
      IsBadReadPtr(At<uint32_t>(kRenderFrame), sizeof(uint32_t))) {
    ModLog("eye clean    : a game address is not readable, so eye_clean is "
           "off");
    return;
  }
  if (!Hook(kCleanFunction, reinterpret_cast<void*>(&h_Clean),
            reinterpret_cast<void**>(&o_Clean),
            "the clean of the game after a present")) {
    return;
  }
  g_sky_hooked = Hook(kDrawSky, reinterpret_cast<void*>(&h_DrawSky),
                      reinterpret_cast<void**>(&o_DrawSky),
                      "GLandAlignement::DrawSky");
  g_installed = true;
  ModLog("eye clean    : eye_clean=%d, %s%s", g_key, KeyText(g_key),
         g_sky_hooked ? "" : ". The sky has no hook: a frame of mode 0 has "
                             "no clean of the mod, and no keep");
  if (g_test == kTestLogoWay) {
    const bool patched = PatchSkyAlpha();
    ModLog("eye clean    : test key eye_clean_test=1: after each frame of "
           "the scene the game cleans only the Z buffer (mode 2), and the "
           "sky dome %s. Each frame is then drawn over the frame before, as "
           "the Lionhead logo is",
           patched ? "has an alpha of 160 of 255"
                   : "keeps its alpha: the code of LH3DSky::Draw is not as "
                     "expected");
  } else if (g_test == kTestMarker) {
    ModLog("eye clean    : test key eye_clean_test=2: the clean of an eye "
           "pass before its sky has the color FF00FF. A pixel of that color "
           "in a picture of an eye is a pixel that the scene does not cover");
  }
}

// The test key (Shift and Page Down in play, test_keys=1). A long pulse for
// 1 and a short pulse for 0.
void EyeCleanToggle(const char* cause) {
  if (!g_installed) return;
  Toggle(cause);
}

// Before eye pass 0 of a frame.
void EyeCleanBeginPair() {
  if (!g_installed) return;
  const uint64_t frame = ControllersFrameCount();
  // A test with no key press: the test key acts at one frame.
  if (g_test_frame > 0) {
    const uint64_t at = static_cast<uint64_t>(g_test_frame);
    static uint64_t done_frame = 0;
    if (frame != done_frame && ControllersTestFrame(at, frame) == at) {
      done_frame = frame;
      Toggle("test key eye_clean_test_frame");
    }
  }
  // The game counts each StartFrame. A count that moved since the last pair
  // is a frame that the game drew outside the eye passes: a menu, a load, or
  // a frame that the headset did not ask for. Its picture is the frame
  // before of both eyes now.
  const uint32_t drawn = *At<const uint32_t>(kRenderFrame);
  if (g_have_frame_after && drawn != g_frame_after_pair) {
    FreeKeeps("the game drew a frame outside the eye passes");
    g_covered = true;
  }
  g_mode = g_have_clean ? g_clean_mode : *At<const int32_t>(kModeCleaning);
  g_pair_cleaned = g_cleaned;
  g_cleaned = kCleanedNothing;
  g_sky[0] = g_sky[1] = false;
  if (g_mode != g_said_mode) {
    ModLog("eye clean    : the cleaning mode of the game is %s. It was %d, "
           "frame %llu", ModeText(g_mode), g_said_mode, frame);
    g_said_mode = g_mode;
  }
  if (g_key != kOwn || !FrameBeforeStays(g_mode, g_covered)) {
    FreeKeeps("the frame before does not stay in view now");
  } else if (SeedEyeOne(g_key, g_mode, g_pair_cleaned, g_covered,
                        GpuBridgeKeepHas(1))) {
    // The first frame with the keeps: the keep of eye 1 takes the render
    // target before eye pass 0 draws over it.
    if (GpuBridgeKeepTake(1)) ++g_takes;
  }
  ++g_pairs;
}

// Before the scene walk of one eye.
void EyeCleanBeforePass(int eye) {
  if (!g_installed) return;
  g_eye = eye;
  g_sky_in_pass = false;
  g_plan = Plan(g_key, g_mode, g_pair_cleaned, g_covered, eye);
  if (g_plan.give && GpuBridgeKeepHas(eye) && GpuBridgeKeepGive(eye)) {
    ++g_gives;
  }
  if (g_plan.clear != kCleanedNothing && CleanNow(g_plan.clear, GameColor())) {
    ++g_own_cleans[g_plan.clear];
  }
}

// After the scene walk of one eye.
void EyeCleanAfterPass(int eye) {
  if (!g_installed || g_eye < 0) return;
  if (g_plan.take) {
    if (GpuBridgeKeepTake(eye)) {
      ++g_takes;
    } else if (!g_said_no_keep) {
      g_said_no_keep = true;
      ModLog("eye clean    : there is no keep for the picture of each eye "
             "(the mod does not have the Direct3D 9 device of the backend, "
             "or a copy failed). So each eye pass draws over the picture of "
             "the other eye while the frame before stays in view, frame %llu",
             ControllersFrameCount());
    }
  }
  if (eye == 0 || eye == 1) {
    g_sky[eye] = g_sky_in_pass;
    if (!g_sky_in_pass) ++g_no_sky_passes;
  }
  g_plan = PassPlan();
  g_eye = -1;
}

// ONE WALK (one_walk.cpp, docs/backend.md, "One walk for both eyes"). One
// pass draws both eyes into a wide target of the mod. That target keeps the
// picture of each eye from one frame to the next, as a keep does, so the pass
// gives no keep and takes none. The clean of the game after the last present
// acted on the game's own target, not on the wide one, so the mod makes the
// same clean when the pass starts. Call it with the wide target bound: the
// clean then reaches both halves.
void EyeCleanOneWalkBefore() {
  if (!g_installed) return;
  g_eye = 0;
  g_sky_in_pass = false;
  g_plan = Plan(g_key, g_mode, g_pair_cleaned, g_covered, 0);
  g_plan.give = false;
  g_plan.take = false;
  if (g_pair_cleaned != kCleanedNothing &&
      CleanNow(g_pair_cleaned, GameColor())) {
    ++g_own_cleans[g_pair_cleaned];
  }
}

// After the pass of one walk. It drew both eyes, so both have its sky.
void EyeCleanOneWalkAfter() {
  if (!g_installed || g_eye < 0) return;
  g_sky[0] = g_sky[1] = g_sky_in_pass;
  if (!g_sky_in_pass) ++g_no_sky_passes;
  g_plan = PassPlan();
  g_eye = -1;
}

// After eye pass 1 of a frame.
void EyeCleanEndPair() {
  if (!g_installed) return;
  // With no hook on the sky the mod cannot know, and the sky is the rule.
  g_covered = !g_sky_hooked || (g_sky[0] && g_sky[1]);
  g_frame_after_pair = *At<const uint32_t>(kRenderFrame);
  g_have_frame_after = true;
  g_pair_since_clean = true;
  if (g_covered != g_said_covered) {
    g_said_covered = g_covered;
    if (g_covered) {
      ModLog("eye clean    : each eye pass draws the sky again, frame %llu",
             ControllersFrameCount());
    } else {
      ModLog("eye clean    : an eye pass drew no sky (eye pass 0: %s, eye "
             "pass 1: %s), so the game draws over the frame before. The "
             "scene of the walk is %u (0 the land, 1 the Temple, 2 the video "
             "of a falling spell), a video %s, the cleaning mode is %d. "
             "eye_clean=%d: %s, frame %llu",
             g_sky[0] ? "a sky" : "no sky", g_sky[1] ? "a sky" : "no sky",
             GameField(kGameSceneKind),
             GameField(kGameVideo) != 0 ? "plays" : "does not play", g_mode,
             g_key,
             g_key == kOwn ? "from the next frame each eye has a keep of its "
                             "picture"
                           : "each eye pass draws over the picture of the "
                             "other eye",
             ControllersFrameCount());
    }
  }
}

void EyeCleanReport() {
  if (!g_installed || g_pairs == 0) return;
  ModLog("eye clean    : eye_clean=%d, cleaning mode %d. In %llu frames the "
         "game cleaned the picture and the Z buffer %llu times and the Z "
         "buffer %llu times. The mod cleaned %llu eye passes before the sky, "
         "and before eye pass 1 the picture and the Z buffer %llu times and "
         "the Z buffer %llu times. %llu eye passes drew no sky. The keeps: "
         "%llu given, %llu taken",
         g_key, g_mode, g_pairs, g_game_cleans[kCleanedAll],
         g_game_cleans[kCleanedDepth], g_sky_cleans, g_own_cleans[kCleanedAll],
         g_own_cleans[kCleanedDepth], g_no_sky_passes, g_gives, g_takes);
  g_pairs = 0;
  g_game_cleans[0] = g_game_cleans[1] = g_game_cleans[2] = 0;
  g_own_cleans[0] = g_own_cleans[1] = g_own_cleans[2] = 0;
  g_sky_cleans = g_no_sky_passes = g_gives = g_takes = 0;
}
