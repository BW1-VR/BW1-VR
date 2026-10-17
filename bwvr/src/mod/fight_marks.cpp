// THE MARKS OF A FIGHT OF CREATURES (fight_mark_depth, fight_math.h, and
// docs/creature.md, "A fight of Creatures").
//
// A press of a fight puts a mark at the place that it hits: on the other
// Creature for an attack, on your Creature for a block, and on the land for a
// move. LH3DCreature::DrawFightSparkles 0x0048DD70 draws the marks of the
// queue of the fight in GGame::Process3dEngine (0x0054DF13), so in each eye
// pass. For each mark it finds the place, and then it moves the place half
// way to the stored camera (the factor 0.5 at 0x0048E118 to 0x0048E169). It
// gives the color, that place, the size of the Creature (LH3DCreature+0x90)
// and a frame of the animation to fn_008465A0. That function puts them in
// one LH3DSprite (0x00EF7534) and calls LH3DSprite::Draw, a billboard with a
// depth of its own. DrawFightSparkles is its only caller. [V]
//
// On a flat screen a mark at the middle shows on its place and in front of
// the body. In a headset each eye sees it at half the distance to the body,
// so it floats in front of each thing near the Creature. With
// fight_mark_depth=1 the hook puts each mark back on its place, in front of
// the body by one size of the mark, and makes it larger by the same factor
// that it moved away, so that it has the size on the screen that the game
// gives. With fight_mark_depth=0 the marks stay where the game puts them.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "../common/bw_addresses.h"
#include "MinHook.h"
#include "fight_math.h"

extern void ModLog(const char* format, ...);

namespace {

// fn_008465A0: cdecl (uint32_t color, const float* place, float size,
// uint32_t frame), a plain ret. The caller pops the four arguments
// (0x0048E184).
const uintptr_t kFightSprite = 0x008465A0;
const uint8_t kFightSpriteBytes[9] = {0xA1, 0x34, 0x75, 0xEF, 0x00,
                                      0x8B, 0x4C, 0x24, 0x04};
// The return address in DrawFightSparkles.
const uintptr_t kFightSpriteFrom = 0x0048E172;

typedef void(__cdecl* FightSpriteFn)(uint32_t color, const float* place,
                                     float size, uint32_t frame);
FightSpriteFn o_FightSprite = nullptr;

int g_depth = 1;  // fight_mark_depth
uint64_t g_moved = 0;
uint64_t g_kept = 0;
float g_last_moved = 0.0f;  // units, for the report
float g_last_distance = 0.0f;
int g_logs = 0;
const int kMaxLogs = 4;

void Load() {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash != nullptr && strlen(path) + 9 < MAX_PATH) {
    slash[1] = 0;
    strcat_s(path, MAX_PATH, "bwvr.ini");
    g_depth =
        GetPrivateProfileIntA("bwvr", "fight_mark_depth", 1, path) != 0 ? 1
                                                                        : 0;
  }
}

void __cdecl h_FightSprite(uint32_t color, const float* place, float size,
                           uint32_t frame) {
  const uintptr_t from = reinterpret_cast<uintptr_t>(_ReturnAddress());
  if (g_depth == 0 || from != kFightSpriteFrom || place == nullptr) {
    o_FightSprite(color, place, size, frame);
    return;
  }
  // DrawFightSparkles took the stored camera at its start, and nothing in it
  // changes the stored camera before this call.
  const float* camera = reinterpret_cast<const float*>(bw::tech::kCameraPos);
  float moved[3], moved_size = size;
  if (!fight_math::MarkPlace(camera, place, size, moved, &moved_size)) {
    ++g_kept;
    o_FightSprite(color, place, size, frame);
    return;
  }
  ++g_moved;
  float shift = 0.0f, distance = 0.0f;
  for (int i = 0; i < 3; ++i) {
    shift += (moved[i] - place[i]) * (moved[i] - place[i]);
    distance += (moved[i] - camera[i]) * (moved[i] - camera[i]);
  }
  g_last_moved = sqrtf(shift);
  g_last_distance = sqrtf(distance);
  if (g_logs < kMaxLogs) {
    ++g_logs;
    ModLog("fight marks  : a mark of the fight goes from %.2f %.2f %.2f, half "
           "way to the camera, to %.2f %.2f %.2f, %.2f units from the camera "
           "at its place on the body. Its size goes from %.3f to %.3f "
           "(fight_mark_depth=1)", place[0], place[1], place[2], moved[0],
           moved[1], moved[2], g_last_distance, size, moved_size);
  }
  o_FightSprite(color, moved, moved_size, frame);
}

}  // namespace

// From mod_main.cpp, with the other hooks of the stereo picture.
void FightMarksInstall() {
  Load();
  ModLog("fight marks  : fight_mark_depth=%d. %s", g_depth,
         g_depth != 0
             ? "The marks of a fight of Creatures have the depth of their "
               "place on the body, and the size on the screen that the game "
               "gives"
             : "The marks of a fight of Creatures are half way between the "
               "camera and their place, as the game puts them");
  void* target = reinterpret_cast<void*>(kFightSprite);
  if (IsBadReadPtr(target, sizeof(kFightSpriteBytes)) ||
      memcmp(target, kFightSpriteBytes, sizeof(kFightSpriteBytes)) != 0) {
    ModLog("fight marks  : the draw of a mark of a fight at 0x%08X does not "
           "have the bytes of the disassembly, so the marks stay where the "
           "game puts them", static_cast<unsigned>(kFightSprite));
    return;
  }
  const bool ok =
      MH_CreateHook(target, reinterpret_cast<void*>(&h_FightSprite),
                    reinterpret_cast<void**>(&o_FightSprite)) == MH_OK &&
      MH_EnableHook(target) == MH_OK;
  if (!ok) {
    ModLog("fight marks  : the draw of a mark of a fight at 0x%08X FAILED to "
           "hook. The marks stay where the game puts them",
           static_cast<unsigned>(kFightSprite));
  }
}

void FightMarksReport() {
  if (g_moved == 0 && g_kept == 0) return;
  ModLog("fight marks  : %llu draws of a mark of a fight went to the place of "
         "the mark and %llu stayed where the game puts them, this window. The "
         "last one moved %.2f units, to %.2f units from the camera",
         g_moved, g_kept, g_last_moved, g_last_distance);
  g_moved = g_kept = 0;
}
