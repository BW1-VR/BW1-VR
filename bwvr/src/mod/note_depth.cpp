// A NOTE OF THE GAME OVER A THING, AT THE DEPTH OF ITS PLACE
// (docs/ui-layer.md, section 19).
//
// WHAT THE GAME DOES. The game shows a short yellow text over a thing in the
// world for 6 s. fn_00827A50 puts such a note in a list (the head is at
// 0x00EB9A74). Each frame fn_00827B90 draws the list in the eye pass, and
// fn_00827BD0 draws one note: it projects the point over the thing with the
// camera of the pass (fn_00819490), and draws the text and its shadow with
// GatheringText::DrawTextRaw at that pixel, with the camera depth of the
// point. So each vertex of the text has the rhw of the place of the note. The
// one note of the game is the note of a Creature that learns: fn_0047CDA0
// shows the percent of a lesson over the head of the Creature
// (CreatureMental::ConsiderLearningAction).
//
// WHY THE MOD CHANGES IT. The text comes through
// LH3DTech::Draw3DScreenTriangle, which draw_hooks.cpp takes as interface.
// The interface depth puts a batch at the depth of the nearest world, and in
// one walk the right eye gets the pixel of the left eye with that depth. Both
// are not the depth of the thing, so the eyes see the note two times when
// they look at the thing.
//
// WHAT THE MOD DOES (note_depth=1). A batch of a note gets none of the steps
// of the interface. Each eye pass draws it where the game put it for that
// eye, and in one walk the right eye gets the move of its rhw, as a batch of
// the world does (one_walk_math::CopyForRightEye). So the note has the depth
// of its place in both eyes. note_depth=0 gives it the steps of the
// interface.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "note_depth.h"
#include "one_walk.h"
#include "text_click_math.h"

extern void ModLog(const char* format, ...);
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();
uint64_t ControllersTestFrame(uint64_t key_frame, uint64_t frame);
void EyeCaptureArmNow(uint64_t stereo_frame);

namespace {

// fn_00827B90: draws the list of the notes. No arguments, a plain ret.
const uintptr_t kNotesDraw = 0x00827B90;
// The first note of the list, or null.
const uintptr_t kNoteList = 0x00EB9A74;
// fn_0047CDA0: the note of a Creature that learns. thiscall on the Creature,
// one float (the part of the lesson that is learned, 0 to 1), ret 4.
const uintptr_t kCreatureLearnNote = 0x0047CDA0;

// A note (0x98 bytes, fn_00827A50).
const int kNoteNext = 0x00;    // the next note
const int kNoteSprite = 0x04;  // the LH3DSprite: its place is 3 floats at +0
const int kNoteAge = 0x08;     // ms since the note started
const int kNoteColour = 0x14;  // the colour of the text, 0xAARRGGBB
const int kNoteText = 0x18;    // the text, 64 wide characters
const int kNoteSize = 0x98;

// The player's Creature, as creature_command.cpp reads it.
const uintptr_t kGame = 0x00D0195C;
const int kGamePlayers = 0x18;
const int kGamePlayerSize = 0xA60;
const int kGameLocalPlayer = 0x205A59;
const int kGameTurn = 0x205A40;
const int kPlayerCreature = 0xA4C;
const int kCreatureLastNote = 0x1124;  // the game turn of its last note

const uint32_t kFvf = 0x1C4;  // XYZRHW, two colours, one texture: 32 bytes
const int kStride = 32;

typedef void(__cdecl* NotesDrawFn)();
typedef void(__fastcall* LearnNoteFn)(void* creature, void* unused,
                                      float part);
NotesDrawFn o_NotesDraw = nullptr;

bool g_on = true;  // note_depth
bool g_installed = false;
int g_eye = -1;
int g_scope = 0;

// The test keys.
const int kMaxTestFrames = 4;
uint64_t g_test_frames[kMaxTestFrames] = {0};
int g_test_count = 0;
int g_test_frame = 0;  // note_depth_test_frame
bool g_test_pending = false;
uint64_t g_frame = 0;

// The notes of the last pass of the left eye, to know a new note.
const int kMaxSeen = 16;
const void* g_seen[kMaxSeen];
int g_seen_count = 0;

// The log: a line for each new note, and a sample of the first batch of a
// note after it starts and then each 150 frames.
int g_logs = 0;
const int kMaxLogs = 80;
uint64_t g_sample_next = 0;
bool g_sample_now = false;
uint64_t g_sample_frame = ~0ull;
bool g_sampled_eye[2] = {false, false};
float g_left_x = 0.0f;  // two walks: the left x of the sample in eye 0

// The report.
uint64_t g_placed = 0;     // batches at the depth of their place
uint64_t g_interface = 0;  // batches with the steps of the interface
uint64_t g_started = 0;    // new notes
uint64_t g_tests = 0;

bool Hook(uintptr_t address, void* replacement, void** original,
          const char* name) {
  void* target = reinterpret_cast<void*>(address);
  MH_STATUS status = MH_CreateHook(target, replacement, original);
  if (status == MH_OK) status = MH_EnableHook(target);
  ModLog("hooks        : %s at 0x%08X %s", name, address,
         status == MH_OK ? "ok" : "FAILED");
  return status == MH_OK;
}

const char* RuleText(bool on) {
  return on ? "a note of the game over a thing (the percent of a lesson over "
              "the Creature) has the depth of its place in both eyes"
            : "a note of the game over a thing gets the interface depth, as "
              "the other flat interface does";
}

void Toggle(const char* cause) {
  g_on = !g_on;
  ControllersGesturePulse(0.5f, g_on ? 0.30f : 0.08f);
  ModLog("note depth   : %s: note_depth is %d now (%s), frame %llu", cause,
         g_on ? 1 : 0, RuleText(g_on), ControllersFrameCount());
}

void Narrow(const wchar_t* text, char* out, int size) {
  int n = 0;
  for (; n < size - 1 && n < 63 && text[n] != 0; ++n) {
    out[n] = (text[n] >= 32 && text[n] < 127) ? static_cast<char>(text[n])
                                              : '?';
  }
  out[n] = 0;
}

// In the pass of the left eye: a line for each note that was not in the list
// of the pass before.
void NoteNewNotes() {
  const uint8_t* const* head = reinterpret_cast<const uint8_t* const*>(kNoteList);
  if (IsBadReadPtr(head, sizeof(void*))) return;
  const void* now[kMaxSeen];
  int count = 0;
  for (const uint8_t* note = *head; note != nullptr && count < kMaxSeen;
       note = *reinterpret_cast<const uint8_t* const*>(note + kNoteNext)) {
    if (IsBadReadPtr(note, kNoteSize)) break;
    now[count++] = note;
    bool seen = false;
    for (int i = 0; i < g_seen_count; ++i) seen |= g_seen[i] == note;
    if (seen) continue;
    ++g_started;
    g_sample_next = 0;
    if (g_logs >= kMaxLogs) continue;
    ++g_logs;
    char text[64];
    Narrow(reinterpret_cast<const wchar_t*>(note + kNoteText), text,
           sizeof(text));
    const float* place = nullptr;
    const uint8_t* sprite =
        *reinterpret_cast<const uint8_t* const*>(note + kNoteSprite);
    if (sprite != nullptr && !IsBadReadPtr(sprite, sizeof(float) * 3)) {
      place = reinterpret_cast<const float*>(sprite);
    }
    ModLog("note depth   : frame %llu, the game shows a note over a thing: "
           "\"%s\", colour 0x%08X, %d ms old, at %.1f %.1f %.1f. "
           "note_depth=%d", ControllersFrameCount(), text,
           *reinterpret_cast<const uint32_t*>(note + kNoteColour),
           *reinterpret_cast<const int32_t*>(note + kNoteAge),
           place ? place[0] : 0.0f, place ? place[1] : 0.0f,
           place ? place[2] : 0.0f, g_on ? 1 : 0);
  }
  for (int i = 0; i < count; ++i) g_seen[i] = now[i];
  g_seen_count = count;
}

void __cdecl h_NotesDraw() {
  if (g_eye < 0 || g_eye > 1) {
    o_NotesDraw();
    return;
  }
  if (g_eye == 0) {
    NoteNewNotes();
    const uint64_t frame = ControllersFrameCount();
    const uint8_t* const* head =
        reinterpret_cast<const uint8_t* const*>(kNoteList);
    const bool any = !IsBadReadPtr(head, sizeof(void*)) && *head != nullptr;
    g_sample_now = false;
    if (any && g_logs < kMaxLogs && frame >= g_sample_next) {
      g_sample_now = true;
      g_sample_frame = frame;
      g_sample_next = frame + 150;
      g_sampled_eye[0] = g_sampled_eye[1] = false;
    }
  }
  ++g_scope;
  o_NotesDraw();
  --g_scope;
}

// The test key note_test: the game shows the note of a Creature that learns
// over the player's Creature, with 37 percent.
void TestNote() {
  ++g_tests;
  void* const* slot = reinterpret_cast<void* const*>(kGame);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) {
    ModLog("note depth   : test key note_test: there is no game now, so "
           "nothing is done, frame %llu", g_frame);
    return;
  }
  uint8_t* const game = static_cast<uint8_t*>(*slot);
  if (IsBadReadPtr(game + kGameLocalPlayer, 1) ||
      IsBadReadPtr(game + kGameTurn, sizeof(int32_t))) {
    return;
  }
  const uint8_t* player =
      game + kGamePlayers + kGamePlayerSize * game[kGameLocalPlayer];
  if (IsBadReadPtr(player + kPlayerCreature, sizeof(void*))) return;
  void* creature = *reinterpret_cast<void* const*>(player + kPlayerCreature);
  if (creature == nullptr ||
      IsBadReadPtr(creature, kCreatureLastNote + sizeof(int32_t))) {
    ModLog("note depth   : test key note_test: the player has no Creature, "
           "so nothing is done, frame %llu", g_frame);
    return;
  }
  // The game shows a note of a Creature at most each 100 game turns, and a
  // test save starts a few game turns after its own start. So the test puts
  // the turn of the last note of the Creature 101 game turns back.
  const uint32_t turn = *reinterpret_cast<const uint32_t*>(game + kGameTurn);
  uint32_t* last = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(creature) +
                                               kCreatureLastNote);
  if (turn - *last <= 100u && !IsBadWritePtr(last, sizeof(uint32_t))) {
    *last = turn - 101u;
  }
  const uint8_t* const* head =
      reinterpret_cast<const uint8_t* const*>(kNoteList);
  const void* before = *head;
  reinterpret_cast<LearnNoteFn>(kCreatureLearnNote)(creature, nullptr, 0.37f);
  const bool shown = *head != before;
  ModLog("note depth   : test key note_test: the mod asks the game for the "
         "note of a Creature that learns over the player's Creature, 37 "
         "percent (fn_0047CDA0), frame %llu. %s", g_frame,
         shown ? "The game shows it. The eye pictures of a frame 20 frames "
                 "later are saved"
               : "The game shows none");
  if (shown) EyeCaptureArmNow(ControllersFrameCount() + 20);
}

}  // namespace

void NoteDepthSetOptions(int on, const char* test_frames, int test_frame) {
  g_on = on != 0;
  g_test_count =
      text_click_math::ParseFrames(test_frames, g_test_frames, kMaxTestFrames);
  g_test_frame = test_frame;
}

void NoteDepthInstall() {
  if (g_installed) return;
  // fn_00827B90 starts as the listing has it: "push esi" and
  // "mov esi, dword ptr [0xeb9a74]".
  static const uint8_t start[7] = {0x56, 0x8B, 0x35, 0x74, 0x9A, 0xEB, 0x00};
  const uint8_t* code = reinterpret_cast<const uint8_t*>(kNotesDraw);
  if (IsBadReadPtr(code, sizeof(start)) ||
      memcmp(code, start, sizeof(start)) != 0) {
    ModLog("note depth   : the code of the notes of the game is not as "
           "expected, so a note is drawn as before");
    return;
  }
  if (!Hook(kNotesDraw, reinterpret_cast<void*>(&h_NotesDraw),
            reinterpret_cast<void**>(&o_NotesDraw),
            "the draw of the notes over a thing")) {
    return;
  }
  g_installed = true;
  ModLog("note depth   : note_depth=%d, %s", g_on ? 1 : 0, RuleText(g_on));
  if (g_test_count > 0) {
    ModLog("note depth   : test key note_test: at %d frames, the first is "
           "%llu, the game shows the note of a Creature that learns over the "
           "player's Creature", g_test_count, g_test_frames[0]);
  }
  if (g_test_frame > 0) {
    ModLog("note depth   : test key note_depth_test_frame: note_depth "
           "changes at frame %d", g_test_frame);
  }
}

void NoteDepthToggle(const char* cause) {
  if (!g_installed) return;
  Toggle(cause);
}

void NoteDepthPass(int eye) { g_eye = g_installed ? eye : -1; }

void NoteDepthFrame(uint64_t frame) {
  if (!g_installed) return;
  g_frame = frame;
  for (int i = 0; i < g_test_count; ++i) {
    // The frame of the key counts as a step of the script counts it
    // (fake_controller_play_frame).
    if (ControllersTestFrame(g_test_frames[i], frame) == g_test_frames[i]) {
      g_test_pending = true;
    }
  }
  if (g_test_frame > 0) {
    const uint64_t at = static_cast<uint64_t>(g_test_frame);
    static uint64_t done_frame = 0;
    if (frame != done_frame && ControllersTestFrame(at, frame) == at) {
      done_frame = frame;
      Toggle("test key note_depth_test_frame");
    }
  }
}

void NoteDepthPump() {
  if (!g_test_pending) return;
  g_test_pending = false;
  TestNote();
}

bool NoteDepthInNote() { return g_scope > 0; }

bool NoteDepthOn() { return g_on; }

void NoteDepthBatch(const void* verts, uint32_t fvf, uint32_t count, int eye,
                    bool walk, float ui_shift, bool placed) {
  if (placed) {
    ++g_placed;
  } else {
    ++g_interface;
  }
  if (!g_sample_now || eye < 0 || eye > 1 || g_sampled_eye[eye]) return;
  if (verts == nullptr || fvf != kFvf || count == 0) return;
  g_sampled_eye[eye] = true;
  float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
  const uint8_t* vertex = static_cast<const uint8_t*>(verts);
  for (uint32_t i = 0; i < count; ++i, vertex += kStride) {
    const float* p = reinterpret_cast<const float*>(vertex);
    if (p[0] < x0) x0 = p[0];
    if (p[0] > x1) x1 = p[0];
    if (p[1] < y0) y0 = p[1];
    if (p[1] > y1) y1 = p[1];
  }
  const float rhw = reinterpret_cast<const float*>(verts)[3];
  const float near_plane =
      *reinterpret_cast<const float*>(bw::tech::kNearPlane);
  const float depth = rhw > 0.0f ? near_plane / rhw : 0.0f;
  if (g_logs >= kMaxLogs) return;
  ++g_logs;
  if (walk) {
    // The right eye gets the move of its rhw (with note_depth=1), or the
    // pixel of the left eye and the other side of the interface depth.
    const float move = placed ? OneWalkShiftPerRhw() * rhw : -2.0f * ui_shift;
    ModLog("note depth   : frame %llu, a batch of a note in one walk: x "
           "%.0f..%.0f y %.0f..%.0f in the left eye as the game put it, rhw "
           "%.5f (%.1f units from the eye). The right eye has it %.1f px "
           "from the left eye: %s. The eye difference of its place is %.1f "
           "px", g_sample_frame, x0, x1, y0, y1, rhw, depth, move,
           placed ? "the eye difference of its place"
                  : "the interface depth",
           OneWalkShiftPerRhw() * rhw);
    return;
  }
  // Two walks: each eye pass places the note with its own camera.
  if (eye == 0) g_left_x = x0;
  ModLog("note depth   : frame %llu, a batch of a note in eye pass %d: x "
         "%.0f..%.0f y %.0f..%.0f as the game put it, rhw %.5f (%.1f units "
         "from the eye)%s%s", g_sample_frame, eye, x0, x1, y0, y1, rhw, depth,
         placed ? "" : ". The interface depth moves it ",
         placed ? "" : (ui_shift >= 0.0f ? "to the right" : "to the left"));
  if (eye == 1) {
    ModLog("note depth   : frame %llu, the right eye has the note %.1f px "
           "from the left eye before the steps of the interface",
           g_sample_frame, x0 - g_left_x);
  }
}

void NoteDepthReport() {
  if (!g_installed) return;
  if (g_placed == 0 && g_interface == 0 && g_tests == 0) return;
  ModLog("note depth   : %llu notes of the game over a thing started. %llu "
         "batches of a note had the depth of their place, and %llu had the "
         "interface depth (note_depth=%d now)", g_started, g_placed,
         g_interface, g_on ? 1 : 0);
  g_started = 0;
  g_placed = 0;
  g_interface = 0;
  g_tests = 0;
}
