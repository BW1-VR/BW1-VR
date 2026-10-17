// THE TRIES TO START THE HEADSET SESSION (vr_start, vr_start.cpp, and
// docs/backend.md, "The start of the headset session"). Pure math: no
// Windows, no game. The checks are in test/controller_math/vr_start_checks.h.
//
// With no headset, the mod tries to start a session each few seconds for a
// time. These functions say which try writes to the log, when the next try
// starts, when the tries are over, which answer of the runtime keeps the
// instance, and when a call into the runtime counts as slow. They also keep
// the numbers of the frame watch: the frames of the frame thread while the
// tries run.

#pragma once

#include <stdint.h>

namespace vr_start_math {

// The values of vr_start.
const int kOnFrameThread = 0;  // each try on the frame thread, a new instance
const int kOneInstance = 1;    // a thread of the mod, one instance for all
const int kNewInstance = 2;    // a thread of the mod, a new instance each try

// The answers of XrScreen::StartSystem.
const int kSystemReady = 1;
const int kSystemNotNow = 0;   // XR_ERROR_FORM_FACTOR_UNAVAILABLE
const int kSystemFault = -1;

// True when try `number` (1 is the first) writes its lines to the log: the
// first try and each tenth try after it. A line each few seconds for two
// minutes buries the rest of the log.
inline bool LoudTry(int number) {
  return number >= 1 && (number - 1) % 10 == 0;
}

// True when the instance stays for the next try, after the answer `system`
// of StartSystem. Only "not now" keeps it, and only with one instance for
// all tries: the spec lets a later xrGetSystem on that instance find the
// headset. A different fault can be a lost instance, and only a new
// instance can help then.
inline bool KeepInstance(int mode, int system) {
  return mode == kOneInstance && system == kSystemNotNow;
}

// The milliseconds of a tick count from `from` to `to`. The difference of
// two unsigned values is right across the wrap of GetTickCount.
inline uint32_t TicksSince(uint32_t from, uint32_t to) { return to - from; }

// How long the thread waits after a try that started at `started`, at the
// time `now`, so that the next try starts `retry_ms` after the start of this
// one. 0 when that time is over.
inline uint32_t WaitBeforeNext(uint32_t started, uint32_t now,
                               uint32_t retry_ms) {
  const uint32_t spent = TicksSince(started, now);
  return spent >= retry_ms ? 0 : retry_ms - spent;
}

// True when no next try starts: `tries` tries are done, or the time of all
// the tries (max_tries times retry_ms from the start of the first try) is
// over. A try that did not come back for a long time uses up the time of the
// tries that it held up.
inline bool TriesOver(int tries, uint32_t since_first_ms, int max_tries,
                      uint32_t retry_ms) {
  if (tries >= max_tries) return true;
  return since_first_ms >= static_cast<uint32_t>(max_tries) * retry_ms;
}

// True when a call into the runtime that started at `started` is slow at the
// time `now`: it has run for `slow_ms` or more.
inline bool CallSlow(uint32_t started, uint32_t now, uint32_t slow_ms) {
  return TicksSince(started, now) >= slow_ms;
}

// THE FALL BACK. The thread of the tries found the headset, and the frame
// thread ran the session part. True when the mod then falls back to the
// tries of vr_start=0 for the rest of the run: the session part failed with
// vr_start 1 or 2. After the fall back vr_start is 0, so it comes one time.
inline bool FallBack(int mode, bool session_started) {
  return (mode == kOneInstance || mode == kNewInstance) && !session_started;
}

// The count of tries that the frame thread starts from after the fall back.
// The tries of the thread count, so the tries keep their 2 minutes. At least
// one try of vr_start=0 comes after the fall back.
inline int TriesAfterFallBack(int thread_tries, int max_tries) {
  if (thread_tries < 0) return 0;
  return thread_tries < max_tries ? thread_tries : max_tries - 1;
}

// True when the test key vr_start_test_fail_session makes the session part
// fail: the key is on, and the thread found the headset. With no headset no
// system comes, and the key does nothing.
inline bool TestFailSession(int key, bool system_found) {
  return key != 0 && system_found;
}

// THE FRAME WATCH. The frames of the frame thread while the tries run, and
// the time of the start code of the mod in those frames.
struct FrameWatch {
  uint32_t frames;
  double frame_total_ms;
  double frame_longest_ms;
  double code_total_ms;
  double code_longest_ms;
};

inline void WatchClear(FrameWatch* watch) {
  watch->frames = 0;
  watch->frame_total_ms = 0.0;
  watch->frame_longest_ms = 0.0;
  watch->code_total_ms = 0.0;
  watch->code_longest_ms = 0.0;
}

// One frame: `frame_ms` from the start of the frame before, and `code_ms` of
// the start code in this frame. A frame_ms below 0 counts only the code: the
// first frame of the watch has no frame before it.
inline void WatchFrame(FrameWatch* watch, double frame_ms, double code_ms) {
  if (frame_ms >= 0.0) {
    ++watch->frames;
    watch->frame_total_ms += frame_ms;
    if (frame_ms > watch->frame_longest_ms) watch->frame_longest_ms = frame_ms;
  }
  if (code_ms > 0.0) {
    watch->code_total_ms += code_ms;
    if (code_ms > watch->code_longest_ms) watch->code_longest_ms = code_ms;
  }
}

inline double WatchMeanFrame(const FrameWatch& watch) {
  return watch.frames > 0 ? watch.frame_total_ms / watch.frames : 0.0;
}

}  // namespace vr_start_math
