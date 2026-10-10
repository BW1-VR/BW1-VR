// The frames of the scripted controller and of the test keys with a frame
// (script_frame_math.h). main.cpp includes this file after Check and Near,
// just before main.
#pragma once

#include "../../src/mod/script_frame_math.h"

namespace script_frame_checks {

inline void ScriptFrameChecks() {
  using namespace script_frame_math;
  // A new game with the first story: the Hand comes at frame 3600.
  const int64_t story = Shift(3600, 2750, 1000);
  Check(story == 850, "the Hand at frame 3600 is 850 frames later than 2750",
        static_cast<double>(story), 850);
  Check(Shift(2000, 2750, 1000) == 0,
        "a Hand before the play frame gives no shift in a new game", 0, 0);
  // A test save: the Hand comes at frame 70.
  const int64_t save = Shift(70, 2750, 60);
  Check(save == -2680, "the Hand at frame 70 is 2680 frames sooner than 2750",
        static_cast<double>(save), -2680);
  Check(Shift(70, 800, 60) == -730, "the play frame 800 in a test save", 0, 0);

  // No play frame: each step and each key has the frame of the game.
  Check(Counted(930, 930, 0, false, 0) == 930, "the key is off", 0, 0);
  Check(Counted(930, 930, -1, true, -730) == 930,
        "a play frame that is not read yet is off", 0, 0);
  // A frame before the play frame keeps its number, with each shift.
  Check(Counted(440, 440, 2750, true, 850) == 440,
        "a click of the start dialog keeps its frame", 0, 0);
  Check(Counted(110, 700, 2750, true, -2680) == 110,
        "a key before the play frame has the frame of the game", 0, 0);
  // From the play frame on, a step waits for the Hand.
  Check(Counted(3000, 2990, 2750, false, 0) == 0,
        "a step after the play frame waits while the shift is not known", 0,
        0);
  // The frame of the Hand is the play frame.
  Check(Counted(3600, 2750, 2750, true, story) == 2750,
        "the frame of the Hand counts as the play frame, a new game", 0, 0);
  Check(Counted(70, 800, 800, true, -730) == 800,
        "the frame of the Hand counts as the play frame, a test save", 0, 0);
  // A step and a test key of one recipe keep their distance.
  const uint64_t seed = 930, seek = 960;
  uint64_t seed_at = 0, seek_at = 0;
  for (uint64_t frame = 60; frame < 400; ++frame) {
    if (Counted(frame, seed, 800, true, -730) == seed) seed_at = frame;
    if (Counted(frame, seek, 800, true, -730) == seek) seek_at = frame;
  }
  Check(seed_at == 200, "the seed of frame 930 comes at frame 200",
        static_cast<double>(seed_at), 200);
  Check(seek_at - seed_at == seek - seed,
        "the step comes 30 frames after the seed, as in a new game",
        static_cast<double>(seek_at - seed_at), 30);
  // A frame before the shift gives no number below 0.
  Check(Counted(10, 2750, 2750, true, 850) == 0,
        "a frame before the Hand counts as 0", 0, 0);
}

}  // namespace script_frame_checks
