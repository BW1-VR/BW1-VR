// THE INTRO SKIP (docs/intro.md, "The skip").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/intro_skip_math.h"

namespace intro_skip_checks {

inline void IntroSkipChecks() {
  using namespace intro_skip;
  printf("\nThe intro skip\n");

  // THE PARTS, by the address that called FinishFrame.
  Check(PartOf(0x00642A4A) == kLogoScreens,
        "a FinishFrame of PlayLogoScreens is a logo screen",
        PartOf(0x00642A4A), kLogoScreens);
  Check(PartOf(0x00642D14) == kLogoScreens,
        "and its last FinishFrame too", PartOf(0x00642D14), kLogoScreens);
  Check(PartOf(0x006428AE) == kVideo,
        "the FinishFrame of PlayPreIntroVideo is the video",
        PartOf(0x006428AE), kVideo);
  Check(PartOf(0x005F9A00) == kLionheadLogo,
        "a FinishFrame of RunAlexFX is the Lionhead logo", PartOf(0x005F9A00),
        kLionheadLogo);
  Check(PartOf(0x005FA0C7) == kLionheadLogo,
        "the FinishFrame of DoLogo is the Lionhead logo too",
        PartOf(0x005FA0C7), kLionheadLogo);
  Check(PartOf(0x0054D100) == kNone && PartOf(0x0082F460) == kNone &&
            PartOf(0) == kNone,
        "a frame of the game loop is no part of the intro", 0, 0);

  const float dt = 1.0f / 60.0f;
  const unsigned kRightTrigger = 8u, kA = 16u, kLeftTrigger = 1u;
  const unsigned kBoth = 3u, kRightOnly = 2u;
  unsigned pressed = 0;

  // A button that is down when the intro starts does not skip.
  {
    State s;
    bool skipped = false;
    for (int i = 0; i < 120; ++i) {
      skipped |= Step(&s, kLogoScreens, kRightTrigger, kBoth, dt, kSkipAll,
                      &pressed);
    }
    Check(!skipped, "a button that is down when the intro starts does not "
          "skip", 0, 0);
    Step(&s, kLogoScreens, 0u, kBoth, dt, kSkipAll, &pressed);
    Check(Step(&s, kLogoScreens, kRightTrigger, kBoth, dt, kSkipAll,
               &pressed),
          "it goes up, and the next press skips", 1, 1);
    Check(pressed == kRightTrigger, "the press is the right trigger", pressed,
          kRightTrigger);
  }

  // A controller that wakes with a button down.
  {
    State s;
    Step(&s, kLogoScreens, 0u, kRightOnly, dt, kSkipAll, &pressed);
    Check(!Step(&s, kLogoScreens, kLeftTrigger, kBoth, dt, kSkipAll,
                &pressed),
          "a controller that wakes with its trigger down does not skip", 0,
          0);
    Step(&s, kLogoScreens, 0u, kBoth, dt, kSkipAll, &pressed);
    Check(Step(&s, kLogoScreens, kLeftTrigger, kBoth, dt, kSkipAll, &pressed),
          "its next press skips", 1, 1);
  }

  // One press skips all that is left (intro_skip=1).
  {
    State s;
    Step(&s, kLogoScreens, 0u, kBoth, dt, kSkipAll, &pressed);
    Check(Step(&s, kLogoScreens, kA, kBoth, dt, kSkipAll, &pressed),
          "a press of A in a logo screen skips", 1, 1);
    bool all = true;
    for (int i = 0; i < 30; ++i) {
      all &= Step(&s, kLogoScreens, i < 5 ? kA : 0u, kBoth, dt, kSkipAll,
                  &pressed);
    }
    Check(all, "each later frame of the logo screens gets the skip, with "
          "the button up too", 1, 1);
    // The game loads: frames that are no part of the intro.
    bool none = false;
    for (int i = 0; i < 200; ++i) {
      none |= Step(&s, kNone, 0u, kBoth, dt, kSkipAll, &pressed);
    }
    Check(!none, "a frame that is no part of the intro gets no skip", 0, 0);
    Check(s.latched, "the press still holds while the game loads", 1, 1);
    Check(Step(&s, kLionheadLogo, 0u, kBoth, dt, kSkipAll, &pressed),
          "the Lionhead logo gets the skip in its first frame", 1, 1);
    Step(&s, kNone, 0u, kBoth, dt, kSkipAll, &pressed);
    Check(!s.latched, "after the Lionhead logo the press holds no more", 0,
          0);
    Check(!Step(&s, kLionheadLogo, 0u, kBoth, dt, kSkipAll, &pressed),
          "a Lionhead logo that plays later needs a press of its own", 0, 0);
  }

  // A press that never meets the Lionhead logo lets go after 2 minutes.
  {
    State s;
    Step(&s, kLogoScreens, 0u, kBoth, dt, kSkipAll, &pressed);
    Step(&s, kLogoScreens, kA, kBoth, dt, kSkipAll, &pressed);
    for (int i = 0; i < 119; ++i) {
      Step(&s, kNone, 0u, kBoth, 1.0f, kSkipAll, &pressed);
    }
    Check(s.latched, "the press holds for 119 s", 1, 1);
    for (int i = 0; i < 3; ++i) {
      Step(&s, kNone, 0u, kBoth, 1.0f, kSkipAll, &pressed);
    }
    Check(!s.latched, "and it lets go after 120 s", 0, 0);
  }

  // One press is one Esc (intro_skip=2), and 0 is off.
  {
    State s;
    Step(&s, kLogoScreens, 0u, kBoth, dt, kSkipPart, &pressed);
    Check(Step(&s, kLogoScreens, kA, kBoth, dt, kSkipPart, &pressed),
          "with intro_skip=2 a press skips in its frame", 1, 1);
    Check(!Step(&s, kLogoScreens, kA, kBoth, dt, kSkipPart, &pressed),
          "and not in the next frame", 0, 0);
    Check(!s.latched, "it does not hold", 0, 0);
    State off;
    Step(&off, kLogoScreens, 0u, kBoth, dt, kOff, &pressed);
    Check(!Step(&off, kLogoScreens, kA, kBoth, dt, kOff, &pressed),
          "with intro_skip=0 a press does not skip", 0, 0);
  }

  // Outside the intro a press does nothing.
  {
    State s;
    Step(&s, kNone, 0u, kBoth, dt, kSkipAll, &pressed);
    Check(!Step(&s, kNone, kRightTrigger, kBoth, dt, kSkipAll, &pressed),
          "a press outside the intro does not skip", 0, 0);
    Check(!s.latched, "and it holds nothing", 0, 0);
  }
}

}  // namespace intro_skip_checks
