// The start of the game, for the log of a desktop run (docs/backend.md, "A
// test save").
//
// One line at the first frame of the scene says how the game started and how
// long the start took: a new game, or a saved game that the game loaded. The
// run scripts read the frame and the seconds from it.
//
// HOW THE GAME STARTS. pc_main (0x00641C60) gives GGame its mode before
// GGame::StartGame (0x0054C190), and GGame::Init reads the mode in its switch
// at 0x0054F6EF:
//   0  The start with no word on the command line. GameOSFile::AutoLoad
//      (0x00563FF0) loads the saved game of slot 20, which the game writes
//      at the end of GGame::Loop (0x0054D5DD). When that load fails, Init
//      sets the mode 1. With the mode 0, GGame::Loop opens the Main Menu
//      over the loaded game before its first frame (0x0054CF58 and
//      0x0054D086), and "Continue Game" in that menu starts the play.
//   1  A new game: the word -NEWGAME, or no saved game in slot 20.
//   2  A saved game of the Temple or the quick save. The game is in this
//      mode only after a game of the same process ended (GGame::Close).
// So the mode at the first frame says whether the load of slot 20 was good.
//
// THE MAIN MENU OF THE START (the test key menu_skip_test_frame). With the
// mode 0 the game waits in its Main Menu, which it opened over the loaded
// game, for the button "Continue Game". The procedure of that menu
// (0x00540041) does this for the button, at 0x005400D1:
//   PauseGame(0)                       0x0054AE20, the game time runs again
//   DialogBoxBase::Hide()              0x00513640, the menu closes
//   FrontEnd::StartupMenuActive = 0    0x00CD0B03
//   FrontEnd::SetCursorOff()           0x0053C470
// With the key at N, the mod does the same at frame N of the scene, when the
// mode is 0 and the flag of the menu is set. So a desktop run in a test save
// needs no click, and no place of a button on the screen. The key is a test
// key: 0 is off, and a player gets the menu.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

void ModLog(const char* format, ...);
// controllers.cpp: how many frames the controllers have seen.
uint64_t ControllersFrameCount();
// exit_test.cpp: the writes to the registry that the guard dropped, and the
// writes that passed.
int ExitTestDropped(bool* guard, int* passed);
// The seconds from the start of the game process to now.
double SecondsSinceProcessStart();

namespace {

const uintptr_t kGame = 0x00D0195C;  // GGame::g_game
const int kGameMode = 0x25017C;
const int kLandNumber = 0x205A08;

const uintptr_t kStartupMenuActive = 0x00CD0B03;  // FrontEnd::StartupMenuActive
const uintptr_t kPauseGame = 0x0054AE20;          // PauseGame(int)
const uintptr_t kDialogHide = 0x00513640;         // DialogBoxBase::Hide()
const uintptr_t kSetCursorOff = 0x0053C470;       // FrontEnd::SetCursorOff()
typedef void(__cdecl* PauseGameFn)(int pause);
typedef void(__cdecl* NoArgFn)();

bool g_said = false;
int g_menu_skip_frame = -1;  // menu_skip_test_frame, -1 until it is read
bool g_menu_skip_done = false;

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

// The test key menu_skip_test_frame: one time, at its frame.
void SkipMainMenu() {
  if (g_menu_skip_frame < 0) {
    g_menu_skip_frame = IniInt("menu_skip_test_frame", 0);
    if (g_menu_skip_frame < 0) g_menu_skip_frame = 0;
  }
  if (g_menu_skip_frame == 0 || g_menu_skip_done) return;
  const uint64_t frame = ControllersFrameCount();
  if (frame < static_cast<uint64_t>(g_menu_skip_frame)) return;
  g_menu_skip_done = true;
  const uintptr_t game = *reinterpret_cast<const uintptr_t*>(kGame);
  const int mode =
      game != 0 ? *reinterpret_cast<const int32_t*>(game + kGameMode) : -1;
  uint8_t* const active = reinterpret_cast<uint8_t*>(kStartupMenuActive);
  if (mode != 0 || *active == 0) {
    ModLog("game start   : test key menu_skip_test_frame: the Main Menu of "
           "the start is not up (game mode %d, the flag of the menu is %d), "
           "so the mod does nothing, frame %llu", mode, *active, frame);
    return;
  }
  reinterpret_cast<PauseGameFn>(kPauseGame)(0);
  reinterpret_cast<NoArgFn>(kDialogHide)();
  *active = 0;
  reinterpret_cast<NoArgFn>(kSetCursorOff)();
  ModLog("game start   : test key menu_skip_test_frame: the mod does what "
         "the button \"Continue Game\" of the Main Menu does (PauseGame(0), "
         "DialogBoxBase::Hide, FrontEnd::SetCursorOff), so the loaded game "
         "plays with no click, frame %llu, %.1f s after the start of the "
         "game process", frame, SecondsSinceProcessStart());
}

}  // namespace

// The seconds from the start of the game process to now.
double SecondsSinceProcessStart() {
  FILETIME made, gone, kernel, user, now;
  if (!GetProcessTimes(GetCurrentProcess(), &made, &gone, &kernel, &user)) {
    return 0.0;
  }
  GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER a, b;
  a.LowPart = made.dwLowDateTime;
  a.HighPart = made.dwHighDateTime;
  b.LowPart = now.dwLowDateTime;
  b.HighPart = now.dwHighDateTime;
  return b.QuadPart > a.QuadPart
             ? static_cast<double>(b.QuadPart - a.QuadPart) / 1.0e7
             : 0.0;
}

// From the stereo loop, one time for each frame of the scene, before the
// controllers count the frame.
void GameStartFrame() {
  SkipMainMenu();
  if (g_said) return;
  g_said = true;
  const uintptr_t game = *reinterpret_cast<const uintptr_t*>(kGame);
  if (game == 0) return;
  const int mode = *reinterpret_cast<const int32_t*>(game + kGameMode);
  const int land = *reinterpret_cast<const int32_t*>(game + kLandNumber);
  const double seconds = SecondsSinceProcessStart();
  const uint64_t frame = ControllersFrameCount();
  bool guard = false;
  int passed = 0;
  const int dropped = ExitTestDropped(&guard, &passed);
  char registry[200] = "";
  if (guard) {
    wsprintfA(registry, ". In the start the mod dropped %d writes of the "
              "game to its keys of the registry, and %d writes of the "
              "process to other keys passed", dropped, passed);
  }
  if (mode == 0 || mode == 2) {
    ModLog("game start   : the saved game is loaded, frame %llu. %s (game "
           "mode %d, land %d), %.1f s after the start of the game process%s",
           frame,
           mode == 0 ? "The game loaded the saved game of slot 20, and it "
                       "opens its Main Menu over it"
                     : "The game loaded a saved game of the Temple or the "
                       "quick save",
           mode, land, seconds, registry);
  } else {
    ModLog("game start   : a new game starts, frame %llu (game mode %d, land "
           "%d), %.1f s after the start of the game process%s",
           frame, mode, land, seconds, registry);
  }
}
