// The brightness of the two eye pictures, a test (eye_luma_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <string.h>

#include "../../src/mod/eye_luma_math.h"

namespace eye_luma_checks {

inline void EyeLumaChecks() {
  using namespace eye_luma;
  printf("\nThe brightness of the two eye pictures\n");

  Near("a white pixel has the brightness 255", Luma(255, 255, 255), 255, 0);
  Near("a black pixel has the brightness 0", Luma(0, 0, 0), 0, 0);
  Check(Luma(0, 255, 0) > Luma(0, 0, 255) && Luma(0, 0, 255) > Luma(255, 0, 0),
        "green counts most, then red, then blue", Luma(0, 255, 0), 150);

  // A picture of 64 by 48 pixels in rows of 300 bytes: gray 100 in the left
  // half and black in the right half.
  {
    static uint8_t picture[48 * 300];
    memset(picture, 0, sizeof(picture));
    for (int y = 0; y < 48; ++y) {
      for (int x = 0; x < 32; ++x) {
        uint8_t* p = picture + y * 300 + x * 4;
        p[0] = p[1] = p[2] = 100;
      }
    }
    Near("a picture that is gray 100 in one half has the mean 50",
         MeanLuma(picture, 300, 64, 48, 24), 50.0, 0.5);
    Near("a picture with no size has the mean 0",
         MeanLuma(picture, 300, 0, 48, 24), 0.0, 0);
    Near("rows that are too short for the width give 0",
         MeanLuma(picture, 200, 64, 48, 24), 0.0, 0);
  }

  // The kind of a frame.
  Check(KindOf(96.0f, 98.0f) == kBright,
        "two eyes with the means 96 and 98 are one scene", 0, 0);
  Check(KindOf(1.0f, 96.0f) == kOneEye,
        "a left eye at 1 and a right eye at 96: one eye is dark", 0, 0);
  Check(KindOf(96.0f, 1.0f) == kOneEye, "and the same the other way round", 0,
        0);
  Check(KindOf(60.0f, 96.0f) == kOneEye,
        "a left eye at 60 under a fade and a right eye at 96: one eye is "
        "darker", 0, 0);
  Check(KindOf(0.5f, 1.5f) == kBlack, "two eyes below 4 are black", 0, 0);
  Check(KindOf(8.0f, 3.0f) == kBright,
        "a dark scene with 8 and 3 is not a dark eye: the difference is small",
        0, 0);

  // The runs of a guide: 100 frames of a picture, 1 black frame, 100 frames
  // of a picture. That black frame is a flash.
  {
    Track track = {};
    Run run = {};
    int runs = 0, flashes = 0;
    uint64_t flash_at = 0;
    for (uint64_t f = 1000; f < 1201; ++f) {
      const bool black = f == 1100;
      if (Step(&track, f, black ? 0.0f : 96.0f, black ? 0.0f : 97.0f, &run)) {
        ++runs;
        if (run.flash) {
          ++flashes;
          flash_at = run.first;
          Near("the flash is one frame long", static_cast<double>(run.frames),
               1, 0);
        }
      }
    }
    Near("a black frame between two pictures ends two runs", runs, 2, 0);
    Near("one of them is a black flash", flashes, 1, 0);
    Near("at the frame of the black frame", static_cast<double>(flash_at),
         1100, 0);
    Near("the window counts 201 frames", static_cast<double>(track.all), 201,
         0);
    Near("1 black frame", static_cast<double>(track.black), 1, 0);
    Near("and 1 flash", static_cast<double>(track.flashes), 1, 0);
  }

  // The entry of the Temple with the fade in one eye: 70 frames with a dark
  // left eye, and then the picture in both eyes. That is one run, and it is
  // no flash.
  {
    Track track = {};
    Run run = {};
    int runs = 0;
    Run one_eye = {};
    for (uint64_t f = 0; f < 200; ++f) {
      float left = 96.0f, right = 97.0f;
      if (f >= 50 && f < 120) left = 96.0f * static_cast<float>(f - 50) / 70.0f;
      if (Step(&track, f, left, right, &run)) {
        ++runs;
        if (run.kind == kOneEye) one_eye = run;
      }
    }
    Check(one_eye.kind == kOneEye, "a fade in the left eye only is a run with "
          "one dark eye", one_eye.kind, kOneEye);
    Near("it starts at the first frame of the fade",
         static_cast<double>(one_eye.first), 50, 0);
    Check(one_eye.frames > 40 && one_eye.frames <= 70,
          "and it ends when the left eye is near the right eye again",
          static_cast<double>(one_eye.frames), 54);
    Near("the run has the frame with the largest difference: the left eye at "
         "0", one_eye.left, 0.0, 0.01);
    Near("and the right eye at 97", one_eye.right, 97.0, 0.01);
    Check(!one_eye.flash, "it is no black flash", one_eye.flash ? 1 : 0, 0);
    Near("the window has no black frame", static_cast<double>(track.black), 0,
         0);
    Near("the largest difference of the window is 97", track.most_gap, 97.0,
         0.01);
    Near("at frame 50", static_cast<double>(track.gap_frame), 50, 0);
    Check(runs == 2, "the picture before and the dark eye are two runs that "
          "ended", runs, 2);
  }

  // A fade of the game to black and back, in both eyes: 30 black frames.
  // That is no flash. A black start of a run is no flash too.
  {
    Track track = {};
    Run run = {};
    int flashes = 0, black_runs = 0;
    for (uint64_t f = 0; f < 300; ++f) {
      float mean = 96.0f;
      if (f < 5) mean = 0.0f;                  // black before the first picture
      if (f >= 100 && f < 130) mean = 0.0f;    // a long fade
      if (f >= 200 && f < 208) mean = 0.0f;    // 8 frames: the limit
      if (Step(&track, f, mean, mean, &run) && run.kind == kBlack) {
        ++black_runs;
        if (run.flash) ++flashes;
      }
    }
    Near("three runs of black frames ended", black_runs, 3, 0);
    Near("only the run of 8 frames between two pictures is a flash", flashes,
         1, 0);
    NewWindow(&track);
    Near("a new window starts its counts at 0",
         static_cast<double>(track.all + track.black + track.flashes), 0, 0);
    Check(track.have, "and the open run stays", track.have ? 1 : 0, 1);
  }

  // A flash of light. In the left eye only it is a run with one eye that is
  // much darker than the other eye. In both eyes it is no such run, and the
  // brightest frame of the window has the flash in each eye.
  {
    Track one = {}, both = {};
    Run run = {};
    int one_eye_runs = 0, both_runs = 0;
    for (uint64_t f = 0; f < 300; ++f) {
      const bool flash = f >= 100 && f < 150;
      const float lit = flash ? 161.0f : 73.0f;
      if (Step(&one, f, lit, 73.5f, &run) && run.kind == kOneEye) {
        ++one_eye_runs;
        Near("the run of the flash in the left eye has 50 frames",
             static_cast<double>(run.frames), 50, 0);
        Check(run.left > run.right, "and the left eye is the bright one",
              run.left, run.right);
      }
      if (Step(&both, f, lit, lit + 0.5f, &run) && run.kind == kOneEye) {
        ++both_runs;
      }
    }
    Near("a flash in the left eye only is one run", one_eye_runs, 1, 0);
    Near("a flash in both eyes is no run with one dark eye", both_runs, 0, 0);
    Near("the brightest frame of that window has the flash in the left eye",
         both.bright_left, 161.0, 0.01);
    Near("and in the right eye", both.bright_right, 161.5, 0.01);
    Near("it is the first frame of the flash",
         static_cast<double>(both.bright_frame), 100, 0);
    NewWindow(&both);
    Near("a new window has no brightest frame", both.bright_left, 0.0, 0);
  }
}

}  // namespace eye_luma_checks
