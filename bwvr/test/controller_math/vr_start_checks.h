// The tries to start the headset session (vr_start_math.h). main.cpp
// includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/vr_start_math.h"

namespace vr_start_checks {

inline void VrStartChecks() {
  using namespace vr_start_math;
  printf("The tries to start the headset session\n");

  // The first try and each tenth try after it write to the log.
  Check(LoudTry(1), "try 1 writes to the log", 1, 1);
  Check(!LoudTry(2), "try 2 is quiet", 0, 0);
  Check(!LoudTry(10), "try 10 is quiet", 0, 0);
  Check(LoudTry(11), "try 11 writes to the log", 1, 1);
  Check(LoudTry(31), "try 31 writes to the log", 1, 1);
  Check(!LoudTry(0), "there is no try 0", 0, 0);
  int loud = 0;
  for (int t = 1; t <= 40; ++t) loud += LoudTry(t) ? 1 : 0;
  Check(loud == 4, "4 of 40 tries write to the log", loud, 4);

  // Only "not now" with one instance for all tries keeps the instance.
  Check(KeepInstance(kOneInstance, kSystemNotNow),
        "vr_start=1 keeps the instance when the headset is not there", 1, 1);
  Check(!KeepInstance(kOneInstance, kSystemFault),
        "vr_start=1 ends the instance after a different fault", 0, 0);
  Check(!KeepInstance(kNewInstance, kSystemNotNow),
        "vr_start=2 ends the instance after each try", 0, 0);
  Check(!KeepInstance(kOnFrameThread, kSystemNotNow),
        "vr_start=0 keeps no instance", 0, 0);

  // The wait to the next try, also across the wrap of the tick count.
  Check(WaitBeforeNext(1000, 1200, 3000) == 2800,
        "a try of 200 ms waits 2800 ms", WaitBeforeNext(1000, 1200, 3000),
        2800);
  Check(WaitBeforeNext(1000, 5000, 3000) == 0,
        "a try of 4 s does not wait", WaitBeforeNext(1000, 5000, 3000), 0);
  Check(WaitBeforeNext(1000, 4000, 3000) == 0,
        "a try of 3 s does not wait", WaitBeforeNext(1000, 4000, 3000), 0);
  const uint32_t near_wrap = 0xFFFFFF00u;
  Check(WaitBeforeNext(near_wrap, near_wrap + 1000u, 3000) == 2000,
        "the wait is right across the wrap of the tick count",
        WaitBeforeNext(near_wrap, near_wrap + 1000u, 3000), 2000);
  Check(TicksSince(near_wrap, 0x100u) == 0x200u,
        "the time across the wrap", TicksSince(near_wrap, 0x100u), 0x200);

  // 40 tries of 3 s: the tries end after try 40, or after 120 s.
  Check(!TriesOver(0, 0, 40, 3000), "the first try starts", 0, 0);
  Check(!TriesOver(39, 117000, 40, 3000), "try 40 starts at 117 s", 0, 0);
  Check(TriesOver(40, 117100, 40, 3000), "no try 41", 1, 1);
  Check(TriesOver(5, 120000, 40, 3000),
        "a try that held the thread for a long time uses up the time", 1, 1);
  Check(!TriesOver(5, 119999, 40, 3000),
        "before 120 s a next try starts", 0, 0);

  // A call is slow after 10 s.
  Check(!CallSlow(5000, 14999, 10000), "a call of 9.999 s is not slow", 0, 0);
  Check(CallSlow(5000, 15000, 10000), "a call of 10 s is slow", 1, 1);
  Check(CallSlow(near_wrap, near_wrap + 20000u, 10000),
        "a call of 20 s across the wrap is slow", 1, 1);

  // The fall back after a session part that failed.
  Check(FallBack(kOneInstance, false),
        "vr_start=1 falls back when the session part fails", 1, 1);
  Check(FallBack(kNewInstance, false),
        "vr_start=2 falls back when the session part fails", 1, 1);
  Check(!FallBack(kOneInstance, true),
        "a session that runs needs no fall back", 0, 0);
  Check(!FallBack(kOnFrameThread, false),
        "vr_start=0 has no fall back, so the fall back comes one time", 0, 0);
  Check(TriesAfterFallBack(7, 40) == 7,
        "the frame thread counts on from try 7 of the thread",
        TriesAfterFallBack(7, 40), 7);
  Check(TriesAfterFallBack(40, 40) == 39,
        "after a fall back at try 40, one try of vr_start=0 comes",
        TriesAfterFallBack(40, 40), 39);
  Check(TriesAfterFallBack(-1, 40) == 0, "no tries give 0",
        TriesAfterFallBack(-1, 40), 0);
  Check(TestFailSession(1, true),
        "the test key fails the session part of a headset that is there", 1,
        1);
  Check(!TestFailSession(1, false),
        "with no headset the test key does nothing", 0, 0);
  Check(!TestFailSession(0, true), "the test key is off at 0", 0, 0);

  // The frame watch.
  FrameWatch watch;
  WatchClear(&watch);
  WatchFrame(&watch, -1.0, 0.5);
  Check(watch.frames == 0, "the first frame of the watch has no frame time",
        watch.frames, 0);
  Near("but its start code counts", watch.code_total_ms, 0.5, 1e-9);
  WatchFrame(&watch, 16.0, 0.0);
  WatchFrame(&watch, 40.0, 0.25);
  WatchFrame(&watch, 16.0, 0.0);
  Check(watch.frames == 3, "three frames", watch.frames, 3);
  Near("the mean frame", WatchMeanFrame(watch), 24.0, 1e-9);
  Near("the longest frame", watch.frame_longest_ms, 40.0, 1e-9);
  Near("the longest start code", watch.code_longest_ms, 0.5, 1e-9);
  Near("the start code in all", watch.code_total_ms, 0.75, 1e-9);
  WatchClear(&watch);
  Near("a clear watch has a mean of 0", WatchMeanFrame(watch), 0.0, 1e-9);
}

}  // namespace vr_start_checks
