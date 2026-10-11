// The frame time of the game after the two eye passes (frame_time_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/frame_time_math.h"

namespace frame_time_checks {

inline void FrameTimeChecks() {
  using namespace frame_time_math;
  printf("The frame time of the game after the two eye passes\n");

  // What LH3DRender::StartFrame makes from two samples of its clock.
  Near("16 ms between two samples is a frame time of 16", FromClock(1000, 1016),
       16, 0);
  Near("two samples at the same ms give 1, as StartFrame does",
       FromClock(1000, 1000), 1, 0);
  Near("a clock that went back gives 1", FromClock(1000, 990), 1, 0);
  Near("a long frame has no upper limit", FromClock(1000, 6000), 5000, 0);
  Near("the difference is right when the count goes round",
       FromClock(0x7FFFFFF8, static_cast<int32_t>(0x80000008u)), 16, 0);
  Near("and when it goes from the last number to 0",
       FromClock(-8, 8), 16, 0);

  // The number that frame_time_real=0 gives.
  Near("the first frame takes the number that it finds",
       Kept(false, 0, 0, 0, 1, 5000), 1, 0);
  Near("with the value 0 the number stays: only the mod wrote it",
       Kept(true, 1, 1, 5016, 1, 5016), 1, 0);
  Near("with the value 1 the mod wrote the real time, and 1 ms still stays",
       Kept(true, 1, 16, 5016, 16, 5016), 1, 0);
  Near("StartFrame of a screen with no scene wrote 31: that number stays now",
       Kept(true, 1, 16, 5016, 31, 5047), 31, 0);
  Near("it wrote the number that the mod wrote, at a later sample: it stays",
       Kept(true, 1, 16, 5016, 16, 5032), 16, 0);
  Near("it wrote a different number at the same sample: it stays",
       Kept(true, 16, 16, 5016, 1, 5016), 1, 0);
  // A session from a saved game: 1 ms before the first pair of passes. The
  // test key gives that number after 100 frames with the real time.
  {
    uint32_t kept = 0, wrote = 0, game = 1;
    int32_t clock = 9000, clock_wrote = 0;
    bool have = false;
    for (int i = 0; i < 100; ++i) {
      const int32_t before = clock;
      clock += 16;
      kept = Kept(have, kept, wrote, clock_wrote, game, before);
      wrote = AfterPasses(kReal, kept, before, clock);
      clock_wrote = clock;
      have = true;
      game = wrote;
    }
    Near("after 100 frames with the value 1 the game has the real time", game,
         16, 0);
    kept = Kept(have, kept, wrote, clock_wrote, game, clock);
    Near("and the value 0 gives 1 ms again",
         AfterPasses(kKept, kept, clock, clock + 16), 1, 0);
  }

  // The rule of frame_time_real.
  Near("frame_time_real=1: the real time of the frame, not the old number",
       AfterPasses(kReal, 1, 5000, 5016), 16, 0);
  Near("frame_time_real=1 with an old number of 31 ms",
       AfterPasses(kReal, 31, 5000, 5016), 16, 0);
  Near("frame_time_real=0: the number from before the passes, 1 ms",
       AfterPasses(kKept, 1, 5000, 5016), 1, 0);
  Near("frame_time_real=0: the number from before the passes, 31 ms",
       AfterPasses(kKept, 31, 5000, 5016), 31, 0);
  Near("frame_time_real=1 with no tick of the clock in the frame gives 1",
       AfterPasses(kReal, 31, 5000, 5000), 1, 0);

  // The two eye passes of a frame share the real time. StartFrame gives the
  // first pass the time since the second pass of the frame before, and the
  // second pass the time since the first. Their sum is the frame, when no
  // pass has the limit of 1.
  {
    const int32_t last = 2000, first = 2011, second = 2016;
    const uint32_t pass0 = FromClock(last, first);
    const uint32_t pass1 = FromClock(first, second);
    Near("the two passes of a frame of 16 ms get 11 and 5 ms",
         pass0 * 100 + pass1, 1105, 0);
    Near("and the frame has their sum",
         AfterPasses(kReal, 1, last, second), pass0 + pass1, 0);
  }

  // The numbers of the log: 60 frames of 16 ms, with the old number of 1 ms
  // and with the real time.
  {
    Window kept = {};
    Window real = {};
    int32_t clock = 100000;
    for (int i = 0; i < 60; ++i) {
      const int32_t before = clock;
      clock += 16;
      Add(&kept, AfterPasses(kKept, 1, before, clock), FromClock(before, clock),
          15, 1);
      Add(&real, AfterPasses(kReal, 1, before, clock), FromClock(before, clock),
          15, 1);
    }
    Near("60 frames are in the window", static_cast<double>(kept.frames), 60,
         0);
    Near("with the old number the game counts 1 ms for each frame",
         Mean(kept.given, kept.frames), 1, 0.0001);
    Near("the real time of a frame is 16 ms", Mean(kept.real, kept.frames), 16,
         0.0001);
    Near("so a move of 5 s takes 16 times as long: 80 s",
         5.0 * TimesAsLong(kept), 80, 0.0001);
    Near("with the real time a move takes its own time", TimesAsLong(real), 1,
         0.0001);
    Near("the first pass got 15 ms as a mean", Mean(real.pass[0], real.frames),
         15, 0.0001);
    Near("the second pass got 1 ms as a mean", Mean(real.pass[1], real.frames),
         1, 0.0001);
    Near("the least number of the window", real.least, 16, 0);
    Near("the most number of the window", real.most, 16, 0);
  }
  // An old number of 31 ms at 16 ms a frame: a move takes about half its time.
  {
    Window fast = {};
    for (int i = 0; i < 10; ++i) Add(&fast, 31, 16, 15, 1);
    Near("an old number of 31 ms makes a move take 0.52 of its time",
         TimesAsLong(fast), 16.0 / 31.0, 0.0001);
    Window none = {};
    Near("a window with no frames has a mean of 0",
         Mean(none.given, none.frames), 0, 0);
    Near("and no factor", TimesAsLong(none), 0, 0);
  }
  // The clock of the game steps 15 or 16 ms, so a frame of 11 ms gets 16 ms
  // or the limit of 1 ms. The sum over many frames is the real time and 1 ms
  // for each frame with no step.
  {
    Window w = {};
    const int32_t samples[8] = {0, 16, 16, 31, 47, 47, 62, 78};
    for (int i = 1; i < 8; ++i) {
      Add(&w, AfterPasses(kReal, 1, samples[i - 1], samples[i]),
          FromClock(samples[i - 1], samples[i]), 0, 0);
    }
    Near("7 frames in 78 ms of the clock count 80 ms: two frames had the limit",
         static_cast<double>(w.given), 80, 0);
    Near("the least number is the limit of 1 ms", w.least, 1, 0);
    Near("the most number is one step of the clock", w.most, 16, 0);
  }

  // The list of the readers.
  {
    static Readers r = {};
    Note(&r, 0x0079A50D, kOutside, 1);
    Note(&r, 0x0079A50D, kOutside, 16);
    Note(&r, 0x0082F193, kPass0, 15);
    Note(&r, 0x0082F193, kPass1, 1);
    Note(&r, 0x00555849, kOutside, 16);
    Note(&r, 0x00555849, kPass0, 15);
    Near("three instructions are in the list", r.used, 3, 0);
    Sort(&r);
    Check(r.list[0].after == 0x00555849 && r.list[1].after == 0x0079A50D &&
              r.list[2].after == 0x0082F193,
          "the list is in the order of the addresses", 0, 0);
    Near("a reader outside the passes has its count", r.list[1].count[kOutside],
         2, 0);
    Near("with the least value", r.list[1].least[kOutside], 1, 0);
    Near("and the most value", r.list[1].most[kOutside], 16, 0);
    Near("it has no read in a pass",
         r.list[1].count[kPass0] + r.list[1].count[kPass1], 0, 0);
    Near("a reader in both parts has a count for each",
         r.list[0].count[kOutside] + r.list[0].count[kPass0], 2, 0);
    Note(&r, 0x00401000, kParts, 5);
    Near("a part that is not known is not counted", r.used, 3, 0);
    for (int i = 0; i < kMaxReaders + 4; ++i) {
      Note(&r, 0x00500000u + static_cast<uint32_t>(i), kHook, 3);
    }
    Near("the list keeps its size", r.used, kMaxReaders, 0);
    Near("and it counts the reads with no room", r.dropped, 7, 0);
  }
}

}  // namespace frame_time_checks
