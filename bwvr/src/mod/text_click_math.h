// A text of the game that waits for a click: the rules of the mod for it.
// See docs/controller-hints.md, "A text that waits for a click". No Windows
// and no game here, so the math test compiles this file.

#pragma once

#include <stdint.h>

namespace text_click_math {

// THE WAIT. The game sets a flag when a script says a text "with
// interaction", and it clears the flag when the left button goes up. This
// follows the flag from frame to frame, for the log.
struct Wait {
  bool waits = false;   // the flag in the last frame
  uint64_t since = 0;   // the frame in which the flag came
  uint64_t clicks = 0;  // left presses of the controller since then
};

enum Change { kNoChange = 0, kStarted = 1, kEnded = 2 };

// One step for a frame. `now` is the flag of the game in this frame. For
// kEnded, *frames is the length of the wait and *clicks is the count of the
// left presses that the controller sent in it.
inline int WaitStep(Wait* wait, bool now, uint64_t frame, uint64_t* frames,
                    uint64_t* clicks) {
  if (now == wait->waits) return kNoChange;
  wait->waits = now;
  if (now) {
    wait->since = frame;
    wait->clicks = 0;
    return kStarted;
  }
  if (frames != nullptr) *frames = frame - wait->since;
  if (clicks != nullptr) *clicks = wait->clicks;
  return kEnded;
}

// The controller sent a left press to the game.
inline void WaitClick(Wait* wait) {
  if (wait->waits) ++wait->clicks;
}

// THE TRIGGER IS THE CLICK. With the leash on the Hand or a seed in the Hand,
// the trigger has a job of the mod and its press does not reach the game.
// While a text waits for a click, the press must reach the game, or the wait
// has no end. on: text_click. waits: the flag of the game in this frame.
inline bool TriggerMayCommand(bool on, bool waits) { return !(on && waits); }

// A PRESS THAT STARTS OFF THE VIEW. The pump drops a press that starts with
// the Hand's pixel off the view, because the game's pointer cannot be there.
// The click that a text waits for needs no place: the game takes it at each
// place of the pointer. So that press starts at each place of the hand, as a
// press in the cut scene window does. held: the game has the button down
// from this control. flat_window: the frame is a flat frame of the cut scene
// window.
inline bool PressMayStart(bool held, bool in_view, bool flat_window, bool on,
                          bool waits) {
  return held || in_view || flat_window || (on && waits);
}

// THE TEST KEY text_click_test: "frame" or "frame,frame", up to 4 frames. At
// each frame the mod makes the game start a wait. Returns the count.
inline int ParseFrames(const char* text, uint64_t* frames, int most) {
  int count = 0;
  if (text == nullptr) return 0;
  const char* p = text;
  while (*p != 0 && count < most) {
    while (*p == ' ' || *p == ',') ++p;
    if (*p < '0' || *p > '9') break;
    uint64_t value = 0;
    while (*p >= '0' && *p <= '9') {
      value = value * 10 + static_cast<uint64_t>(*p - '0');
      ++p;
    }
    if (value > 0) frames[count++] = value;
  }
  return count;
}

}  // namespace text_click_math
