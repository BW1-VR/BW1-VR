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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

void ModLog(const char* format, ...);
// controllers.cpp: how many frames the controllers have seen.
uint64_t ControllersFrameCount();
// exit_test.cpp: the writes to the registry that the guard dropped, and the
// writes that passed.
int ExitTestDropped(bool* guard, int* passed);

namespace {

const uintptr_t kGame = 0x00D0195C;  // GGame::g_game
const int kGameMode = 0x25017C;
const int kLandNumber = 0x205A08;

bool g_said = false;

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
