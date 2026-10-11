// THE FRAME TIME OF THE GAME AFTER THE TWO EYE PASSES (frame_time.cpp, and
// docs/backend.md, "Time, bars and the second eye pass"). Pure math: no
// Windows, no game. The checks are in
// test/controller_math/frame_time_checks.h.
//
// LH3DRender::StartFrame (0x0082F0E0) keeps one sample of its clock, in ms.
// At each call it takes a new sample. The frame time of the game is the new
// sample less the sample of the call before, and 1 when that is not more
// than 0 (0x0082F184 to 0x0082F195). The number has no upper limit.

#pragma once

#include <stdint.h>

namespace frame_time_math {

// The frame time that LH3DRender::StartFrame makes from two samples of its
// clock. The clock is a count of 32 bits, so the difference is right when
// the count goes round.
inline uint32_t FromClock(int32_t before, int32_t now) {
  const int32_t passed = static_cast<int32_t>(static_cast<uint32_t>(now) -
                                              static_cast<uint32_t>(before));
  return passed > 0 ? static_cast<uint32_t>(passed) : 1u;
}

// frame_time_real. kReal: after the two eye passes the game has the real time
// of the frame. kKept: it has the number from before the passes.
const int kKept = 0;
const int kReal = 1;

// THE NUMBER THAT kKept GIVES. With kKept the game has, after each pair of
// passes, the number that it had before the first pair: no code of the game
// writes the number outside the passes in play. A screen with no scene (a
// dialog box of the start, a load) calls StartFrame outside the passes, and
// the number that it leaves is then the one that stays.
//
// The mod follows that number with each value of the key, so the test key
// gives it in the middle of a session. `kept` is the number so far. `wrote`
// and `clock_wrote` are what the mod wrote after the passes of the frame
// before, and the sample of StartFrame at that moment. `before_passes` and
// `clock_before` are the number and the sample that this frame finds. When
// one of the two is different, StartFrame ran between the two frames, and its
// number stays.
inline uint32_t Kept(bool have, uint32_t kept, uint32_t wrote,
                     int32_t clock_wrote, uint32_t before_passes,
                     int32_t clock_before) {
  if (!have || before_passes != wrote || clock_before != clock_wrote) {
    return before_passes;
  }
  return kept;
}

// What the game has as its frame time after the two eye passes. `kept` is
// the number of kKept. The two samples are the sample of StartFrame before
// the passes and its sample after them: one whole frame is between them,
// with both passes in it.
inline uint32_t AfterPasses(int rule, uint32_t kept, int32_t clock_before,
                            int32_t clock_now) {
  if (rule == kKept) return kept;
  return FromClock(clock_before, clock_now);
}

// THE NUMBERS OF THE LOG. The sums of a count of frames: what the game had
// after the passes, the real time, and what StartFrame gave each eye pass.
struct Window {
  uint64_t frames;
  uint64_t given;
  uint64_t real;
  uint64_t pass[2];
  uint32_t least;  // of what the game had after the passes
  uint32_t most;
};

inline void Add(Window* w, uint32_t given, uint32_t real, uint32_t pass0,
                uint32_t pass1) {
  if (w->frames == 0 || given < w->least) w->least = given;
  if (w->frames == 0 || given > w->most) w->most = given;
  ++w->frames;
  w->given += given;
  w->real += real;
  w->pass[0] += pass0;
  w->pass[1] += pass1;
}

inline double Mean(uint64_t sum, uint64_t frames) {
  return frames == 0 ? 0.0
                     : static_cast<double>(sum) / static_cast<double>(frames);
}

// How many times as long as its own time a move of the game takes, when the
// game counts `given` for a real time of `real`. 1 is the speed of the game.
// 0 when the game counted no time.
inline double TimesAsLong(const Window& w) {
  return w.given == 0 ? 0.0
                      : static_cast<double>(w.real) /
                            static_cast<double>(w.given);
}

// THE READERS (the test key frame_time_test_readers). Each instruction of
// the game that reads or writes the frame time, with a count for each part
// of a frame and the least and the most value that it found.
enum Part { kOutside, kHook, kPass0, kPass1, kParts };
const int kMaxReaders = 96;

struct Reader {
  uint32_t after;  // the address after the instruction
  uint32_t count[kParts];
  uint32_t least[kParts];
  uint32_t most[kParts];
};

struct Readers {
  Reader list[kMaxReaders];
  int used;
  uint32_t dropped;  // reads with no room in the list
};

inline void Note(Readers* r, uint32_t after, int part, uint32_t value) {
  if (part < 0 || part >= kParts) return;
  Reader* reader = nullptr;
  for (int i = 0; i < r->used; ++i) {
    if (r->list[i].after == after) {
      reader = &r->list[i];
      break;
    }
  }
  if (reader == nullptr) {
    if (r->used >= kMaxReaders) {
      ++r->dropped;
      return;
    }
    reader = &r->list[r->used++];
    *reader = Reader();
    reader->after = after;
  }
  if (reader->count[part] == 0 || value < reader->least[part]) {
    reader->least[part] = value;
  }
  if (reader->count[part] == 0 || value > reader->most[part]) {
    reader->most[part] = value;
  }
  ++reader->count[part];
}

// The readers in the order of their addresses, for the log.
inline void Sort(Readers* r) {
  for (int i = 1; i < r->used; ++i) {
    const Reader moved = r->list[i];
    int at = i;
    while (at > 0 && r->list[at - 1].after > moved.after) {
      r->list[at] = r->list[at - 1];
      --at;
    }
    r->list[at] = moved;
  }
}

}  // namespace frame_time_math
