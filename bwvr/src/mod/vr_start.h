// THE TRIES TO START THE HEADSET SESSION on a thread of the mod (vr_start=1
// and 2), and the frame watch of each value of vr_start. See
// docs/backend.md, "The start of the headset session".
//
// The thread makes the instance and asks for the head mounted display. These
// calls only fail or wait for the runtime when no headset is there, so the
// frame thread does not wait for them. When the runtime has a headset, the
// thread gives the screen to the frame thread, and the frame thread starts
// the session (XrScreen::StartSession).

#pragma once

#include <stdint.h>

namespace bwvr {
class XrScreen;
}

struct VrStartOptions {
  int mode;           // vr_start, 1 or 2 (vr_start_math.h)
  int retry_ms;       // from the start of one try to the start of the next
  int max_tries;
  int test_hang_try;  // vr_start_test_hang_try: 0 is off
  int test_hang_ms;   // vr_start_test_hang_ms
};

// On the frame thread, one time: starts the thread. False: no thread, and
// the log says why.
bool VrStartBegin(const VrStartOptions& options);

// On the frame thread, each frame while the tries run. It does not wait for
// the thread. It gives a screen with an instance and a system one time, and
// the caller then owns it. Null: no screen in this frame. *over is true when
// the tries are over and no screen comes. The log says when a call into the
// runtime does not come back.
bwvr::XrScreen* VrStartTake(bool* over);

// The session of the screen from VrStartTake did not start. The thread ends
// the screen and its tries, and then VrStartTake gives *over. The frame
// thread falls back to the tries of vr_start=0 (vr_start_math.h, FallBack).
void VrStartGiveBack(bwvr::XrScreen* screen);

// The count of tries that the thread started.
int VrStartTries();

// The session of the screen from VrStartTake runs. The thread that made the
// instance stays until the shutdown, and it does nothing more.
void VrStartSessionRuns();

// On the frame thread: the tries stop. The thread ends its instance.
void VrStartCancel();

// At the shutdown, on the frame thread. The thread ends its instance, and
// the shutdown waits at most wait_ms for that.
void VrStartShutdown(uint32_t wait_ms);

// THE FRAME WATCH, on the frame thread, for each value of vr_start. One call
// each frame while the tries run, with the time of the start code in that
// frame. One line of the log each 30 s, and one line at the end.
void VrWatchFrame(double code_ms, int mode);
void VrWatchEnd(const char* how, int mode);
