// THE BRIGHTNESS OF THE TWO EYE PICTURES, a test (eye_luma_math.h, and
// docs/backend.md, "Testing without a headset").
//
// eye_test_luma=1: after each eye pass the mod reads the mean brightness of
// the picture of that eye, on a grid of points. The log then has one line
// for each run of frames in which one eye is much darker than the other eye,
// one line for each run of frames in which both eyes are black, and the sums
// of each window. A fade that only one eye pass draws is such a run, and a
// frame with no picture between two frames with a picture is a black flash.
//
// The read locks the render target, which costs frame time (about 1 ms for
// each eye at 1440 lines, and more at the size of a headset). So the key is
// a test key, and it is off for a player.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "eye_luma_math.h"

extern void ModLog(const char* format, ...);
// temple.cpp: the fade of the Temple now, and whether the player is there.
float TempleFadeNow();
bool TempleInside();

namespace {

const int kCells = 24;
const int kMaxRunLogs = 300;

bool g_loaded = false;
bool g_on = false;
float g_mean[2] = {0.0f, 0.0f};
bool g_have[2] = {false, false};
eye_luma::Track g_track = {};
float g_fade_at_start = 0.0f;
bool g_temple_at_start = false;
int g_run_logs = 0;
uint64_t g_missed = 0;  // frames with no picture of one eye

void Load() {
  if (g_loaded) return;
  g_loaded = true;
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 9 >= MAX_PATH) return;
  slash[1] = 0;
  strcat_s(path, MAX_PATH, "bwvr.ini");
  g_on = GetPrivateProfileIntA("bwvr", "eye_test_luma", 0, path) != 0;
  if (g_on) {
    ModLog("eye test     : eye_test_luma=1. After each eye pass the mod reads "
           "the mean brightness of the eye picture, 0 to 255, on a grid of %d "
           "by %d points. The log names each run of frames with one dark eye "
           "and each run of black frames. The read costs frame time", kCells,
           kCells);
  }
}

const char* Where(bool temple) { return temple ? "in the Temple" : "on the land"; }

void LogRun(const eye_luma::Run& run) {
  if (run.kind == eye_luma::kBright) return;
  if (g_run_logs >= kMaxRunLogs) return;
  ++g_run_logs;
  const uint64_t last = run.first + run.frames - 1;
  if (run.kind == eye_luma::kOneEye) {
    ModLog("eye test     : from frame %llu to frame %llu (%llu frames) one "
           "eye was much darker than the other eye. At the frame with the "
           "largest difference the left eye had %.1f and the right eye had "
           "%.1f. The fade of the Temple was %.2f at the start of the run "
           "and %.2f after it, %s", run.first, last, run.frames, run.left,
           run.right, g_fade_at_start, TempleFadeNow(),
           Where(g_temple_at_start));
  } else {
    ModLog("eye test     : from frame %llu to frame %llu (%llu frames) both "
           "eyes were black. %s. The fade of the Temple was %.2f at the start "
           "of the run and %.2f after it, %s", run.first, last, run.frames,
           run.flash ? "That is a black flash: the frame before it and the "
                       "frame after it had a picture"
                     : "That is not a flash: the run is long, or no picture "
                       "came before it",
           g_fade_at_start, TempleFadeNow(), Where(g_temple_at_start));
  }
}

}  // namespace

bool EyeLumaOn() {
  Load();
  return g_on;
}

// The picture of one eye, after its pass. The pixels have 4 bytes each.
void EyeLumaEye(int eye, const void* pixels, int pitch, uint32_t width,
                uint32_t height) {
  if (!g_on || eye < 0 || eye > 1 || pixels == nullptr) return;
  g_mean[eye] = eye_luma::MeanLuma(static_cast<const uint8_t*>(pixels), pitch,
                                   static_cast<int>(width),
                                   static_cast<int>(height), kCells);
  g_have[eye] = true;
}

// After the two eye passes of a frame.
void EyeLumaFrame(uint64_t frame) {
  if (!g_on) return;
  const bool both = g_have[0] && g_have[1];
  g_have[0] = g_have[1] = false;
  if (!both) {
    ++g_missed;
    return;
  }
  const bool open_before = g_track.have;
  const int kind_before = g_track.kind;
  eye_luma::Run ended;
  if (eye_luma::Step(&g_track, frame, g_mean[0], g_mean[1], &ended)) {
    LogRun(ended);
  }
  // The state of the Temple at the first frame of a run.
  if (!open_before || kind_before != g_track.kind) {
    g_fade_at_start = TempleFadeNow();
    g_temple_at_start = TempleInside();
  }
}

void EyeLumaReport() {
  if (!g_on || (g_track.all == 0 && g_missed == 0)) return;
  ModLog("eye test     : %llu frames this window: %llu with one dark eye, "
         "%llu with both eyes black, and %llu black flashes. The largest "
         "difference between the eyes was %.1f (left %.1f, right %.1f) at "
         "frame %llu. %llu frames had no picture to read",
         g_track.all, g_track.one_eye, g_track.black, g_track.flashes,
         g_track.most_gap, g_track.gap_left, g_track.gap_right,
         g_track.gap_frame, g_missed);
  eye_luma::NewWindow(&g_track);
  g_missed = 0;
}
