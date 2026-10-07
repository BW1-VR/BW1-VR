// The intro skip: a press of a trigger or of a face button skips the logos
// at the start of the game (docs/intro.md, "The skip").
//
// WHAT THE GAME ACCEPTS. The start of the game has three parts, and each
// draws its own frames:
//
//   PlayLogoScreens 0x00642950    Three logo screens. The test at 0x00642930
//                                 skips one screen when the last key is Esc
//                                 or a mouse button is down. Then InputReset
//                                 0x005FA000 clears both, so one press skips
//                                 one screen.
//   PlayPreIntroVideo 0x006426F0  A video, only on the first start. It ends
//                                 when the last key is Esc (0x006428C5).
//   DoLogo 0x005FA070             The Lionhead logo of RunAlexFX. It ends on
//                                 a mouse button, on each key (0x005FA09F),
//                                 or when the effect is done.
//
// "The last key" is the scan code that LHKeyboard::ProcessKeyboard
// 0x007DCAF0 writes at 0x007DCBD3 for each key that goes down: the field at
// LHKeyboard + 0x108, which is 0x00E85474. Esc is 1. So a 1 in that field is
// what an Esc press gives each of the three parts.
//
// WHAT THE MOD DOES. mod_main.cpp calls IntroSkipFrame after each
// FinishFrame, with the address that called FinishFrame. That address says
// which part plays (intro_skip::PartOf), and the mod hooks nothing more. A
// new press of a trigger, A, B, X or Y writes 1 to the last key. With
// intro_skip=1 the mod writes it in each frame of a part from then on, so
// one press skips all that is left of the intro. With intro_skip=2 one
// press is one Esc, as on a monitor. The grip does not skip: a player holds
// it with no intent.
//
// THE CONTROLLERS. The mono path of mod_main.cpp sends each frame of the
// intro to the headset, and XrScreen::BeginFrame reads the controllers for
// each frame. So the hands are known as soon as the session has the focus,
// which is in the first logo screen or soon after it. Before that no press
// can skip. The fake controller of a desktop run comes from controllers.cpp,
// which runs in the intro only with intro_vr=1.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "../vr/xr_screen.h"
#include "intro_skip_math.h"

extern void ModLog(const char* format, ...);
// controllers.cpp: the triggers and the face buttons that are down, as the
// last ControllersFrame read them, with the bits of intro_skip_math.h.
unsigned ControllersButtonBits(unsigned* awake);

namespace {

const uintptr_t kLastKey = 0x00E85474;  // LHKeyboard + 0x108, a scan code
const uint32_t kEscScanCode = 1;

bool g_read = false;
int g_mode = intro_skip::kSkipAll;  // intro_skip
// A test with no controller: at this frame of the intro a press comes that
// no hand made. 0 is no test.
uint64_t g_test_frame = 0;          // intro_skip_test_frame

intro_skip::State g_state;
int g_part = intro_skip::kNone;
uint64_t g_intro_frames = 0;  // frames of a part this session
uint64_t g_part_frames = 0;   // frames of the part that plays now
uint64_t g_skip_frames = 0;   // frames that got the skip key
bool g_was_latched = false;
LARGE_INTEGER g_frequency = {0};
LARGE_INTEGER g_last = {0};

const char* const kPartName[4] = {"no part", "the logo screens",
                                  "the video before the intro",
                                  "the Lionhead logo"};

void ReadSettings() {
  if (g_read) return;
  g_read = true;
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash != nullptr && strlen(path) + 9 < MAX_PATH) {
    slash[1] = 0;
    strcat_s(path, MAX_PATH, "bwvr.ini");
    const int mode = static_cast<int>(
        GetPrivateProfileIntA("bwvr", "intro_skip", intro_skip::kSkipAll,
                              path));
    g_mode = mode < intro_skip::kOff || mode > intro_skip::kSkipPart
                 ? intro_skip::kSkipAll
                 : mode;
    g_test_frame = static_cast<uint64_t>(
        GetPrivateProfileIntA("bwvr", "intro_skip_test_frame", 0, path));
  }
  if (IsBadWritePtr(reinterpret_cast<void*>(kLastKey), sizeof(uint32_t))) {
    g_mode = intro_skip::kOff;
    ModLog("intro skip   : off, because the mod cannot write the last key of "
           "the game");
    return;
  }
  ModLog("intro skip   : intro_skip=%d. %s", g_mode,
         g_mode == intro_skip::kSkipAll
             ? "A press of a trigger or of A, B, X or Y skips the logos at "
               "the start of the game, all that is left of them"
             : g_mode == intro_skip::kSkipPart
                   ? "A press of a trigger or of A, B, X or Y skips one logo "
                     "at the start of the game, as Esc does"
                   : "No controller button skips the logos at the start of "
                     "the game");
}

// The buttons of one hand of the runtime, at the bits of that hand.
unsigned HandBits(const bwvr::HandInput& hand, int index, unsigned* awake) {
  if (!hand.active) return 0;
  *awake |= 1u << index;
  unsigned bits = 0;
  if (hand.trigger >= 0.5f) bits |= 1u << (index * 3);
  if (hand.primary) bits |= 2u << (index * 3);
  if (hand.secondary) bits |= 4u << (index * 3);
  return bits;
}

const char* ButtonName(unsigned pressed) {
  static const char* const kNames[6] = {
      "the left trigger",  "the X button", "the Y button",
      "the right trigger", "the A button", "the B button"};
  for (int i = 0; i < 6; ++i) {
    if ((pressed >> i) & 1u) return kNames[i];
  }
  return "a button";
}

}  // namespace

void IntroSkipFrame(uintptr_t caller, bwvr::XrScreen* screen) {
  ReadSettings();
  if (g_mode == intro_skip::kOff) return;
  const int part = intro_skip::PartOf(caller);
  // After the intro, a frame costs one test of an address.
  if (part == intro_skip::kNone && g_part == intro_skip::kNone &&
      !g_state.latched) {
    return;
  }

  if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  float dt = 0.0f;
  if (g_last.QuadPart != 0 && g_frequency.QuadPart != 0) {
    dt = static_cast<float>(now.QuadPart - g_last.QuadPart) /
         static_cast<float>(g_frequency.QuadPart);
  }
  g_last = now;

  if (part != g_part) {
    if (g_part != intro_skip::kNone) {
      ModLog("intro skip   : %s %s over after %llu frames", kPartName[g_part],
             g_part == intro_skip::kLogoScreens ? "are" : "is",
             g_part_frames);
    }
    g_part = part;
    g_part_frames = 0;
    if (part != intro_skip::kNone) {
      ModLog("intro skip   : %s play%s now", kPartName[part],
             part == intro_skip::kLogoScreens ? "" : "s");
    }
  }
  if (part != intro_skip::kNone) {
    ++g_intro_frames;
    ++g_part_frames;
  }

  // The hands of the headset, and the hands of controllers.cpp, which has
  // the fake controller of a desktop run.
  unsigned awake = 0;
  unsigned down = ControllersButtonBits(&awake);
  if (screen != nullptr && screen->running()) {
    down |= HandBits(screen->hand(0), 0, &awake);
    down |= HandBits(screen->hand(1), 1, &awake);
  }
  const bool test = part != intro_skip::kNone && g_test_frame != 0 &&
                    g_intro_frames == g_test_frame;
  if (test) {
    // A press that no hand made: the frame before had an awake right hand
    // with its trigger up.
    g_state.have = true;
    g_state.awake |= 2u;
    g_state.down &= ~8u;
    awake |= 2u;
    down |= 8u;
  }

  unsigned pressed = 0;
  const bool skip =
      intro_skip::Step(&g_state, part, down, awake, dt, g_mode, &pressed);
  if (pressed != 0) {
    ModLog("intro skip   : a press of %s%s skips %s, at frame %llu of the "
           "intro. %s", ButtonName(pressed), test ? " (a test)" : "",
           kPartName[part], g_intro_frames,
           g_mode == intro_skip::kSkipAll
               ? "The mod gives the game Esc until the intro is over"
               : "The mod gives the game one Esc");
  }
  if (skip) {
    *reinterpret_cast<uint32_t*>(kLastKey) = kEscScanCode;
    ++g_skip_frames;
  }
  if (g_was_latched && !g_state.latched) {
    ModLog("intro skip   : the intro is over. %llu frames got Esc",
           g_skip_frames);
  }
  g_was_latched = g_state.latched;
}
