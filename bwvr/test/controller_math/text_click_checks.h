// A TEXT THAT WAITS FOR A CLICK (docs/controller-hints.md, "A text that
// waits for a click").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/text_click_math.h"

namespace text_click_checks {

inline void TextClickChecks() {
  using namespace text_click_math;
  printf("\nA text that waits for a click\n");

  // THE WAIT, from frame to frame.
  {
    Wait wait;
    uint64_t frames = 99, clicks = 99;
    Check(WaitStep(&wait, false, 10, &frames, &clicks) == kNoChange,
          "no flag: no change", 0, 0);
    Check(WaitStep(&wait, true, 30738, &frames, &clicks) == kStarted &&
              wait.since == 30738 && wait.clicks == 0,
          "the flag comes: the wait starts at that frame",
          static_cast<double>(wait.since), 30738);
    Check(WaitStep(&wait, true, 30739, &frames, &clicks) == kNoChange,
          "the flag stays: no change", 0, 0);
    // A press of the grip is no click of the left button: nothing counts it.
    Check(WaitStep(&wait, true, 42291, &frames, &clicks) == kNoChange &&
              wait.clicks == 0,
          "a wait of 11553 frames with no left press has 0 clicks",
          static_cast<double>(wait.clicks), 0);
    WaitClick(&wait);
    Check(WaitStep(&wait, false, 42300, &frames, &clicks) == kEnded &&
              frames == 42300 - 30738 && clicks == 1,
          "the flag goes: the wait is over, with its length and its clicks",
          static_cast<double>(frames), 42300 - 30738);
    // A left press with no wait does not count for the next wait.
    WaitClick(&wait);
    WaitStep(&wait, true, 50000, &frames, &clicks);
    Check(wait.clicks == 0, "a press before the wait is not a click of it",
          static_cast<double>(wait.clicks), 0);
    Check(WaitStep(&wait, false, 50090, nullptr, nullptr) == kEnded,
          "the end needs no place for its numbers", 0, 0);
  }

  // THE TRIGGER IS THE CLICK.
  {
    Check(TriggerMayCommand(true, false),
          "no text waits: the trigger commands the Creature", 1, 1);
    Check(!TriggerMayCommand(true, true),
          "a text waits: the press of the trigger goes to the game", 0, 0);
    Check(TriggerMayCommand(false, true) && TriggerMayCommand(false, false),
          "text_click=0: the trigger keeps its other jobs", 1, 1);
  }

  // A PRESS THAT STARTS OFF THE VIEW.
  {
    Check(PressMayStart(false, true, false, true, false),
          "the Hand in view: a press starts", 1, 1);
    Check(!PressMayStart(false, false, false, true, false),
          "the Hand off the view and no text waits: the press is dropped", 0,
          0);
    Check(PressMayStart(false, false, false, true, true),
          "the Hand off the view and a text waits: the press starts", 1, 1);
    Check(!PressMayStart(false, false, false, false, true),
          "text_click=0: that press is dropped", 0, 0);
    Check(PressMayStart(true, false, false, false, false),
          "a press that the game has may end at each place", 1, 1);
    Check(PressMayStart(false, false, true, false, false),
          "a flat frame of the cut scene window: a press starts", 1, 1);
  }

  // THE TEST KEY text_click_test.
  {
    uint64_t frames[4] = {0, 0, 0, 0};
    Check(ParseFrames("2600", frames, 4) == 1 && frames[0] == 2600,
          "one frame", static_cast<double>(frames[0]), 2600);
    Check(ParseFrames("2600,3400", frames, 4) == 2 && frames[1] == 3400,
          "two frames with a comma", static_cast<double>(frames[1]), 3400);
    Check(ParseFrames("2600 3400 4000 4500 5000", frames, 4) == 4,
          "no more than the room", 4, 4);
    Check(ParseFrames("", frames, 4) == 0 && ParseFrames(nullptr, frames, 4) == 0 &&
              ParseFrames("x", frames, 4) == 0 && ParseFrames("0", frames, 4) == 0,
          "an empty key, a word and frame 0 give no frame", 0, 0);
  }
}

}  // namespace text_click_checks
