// A CATCH AT ONCE (docs/grab.md, "A catch at once"): the press that is old
// enough for the game, the picture of the object from the press, and the
// numbers of a catch that the log has.
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/catch_math.h"
#include "../../src/mod/controller_math.h"
#include "../../src/mod/hand_exact_math.h"

namespace catch_checks {

// One catch at a frame rate, in cm of the room and ms. The object falls with
// the gravity of the room and has 100 cm/s at the press. The hand is still,
// 12 cm from the picture of the object. The game counts the age of the press
// as in controller_math::GamePressAge, with a game turn of 100 ms. offset_ms
// is the time of the press in its game turn.
//   at_once  The step of the first frame after the press gets a press that
//            is old enough, and the mod draws the object in the Hand from the
//            frame of the press.
//   no       The game sends its pick up when the press is 225 ms old, and it
//            takes the object 2 game turns later. The picture is the object
//            itself until then, and the place in the Hand after that.
struct Run {
  catch_math::Track track;
  double sent_ms;   // the step that sent the pick up
  double taken_ms;  // the game took the object out of its physics
  bool click;       // the button went up while a release was a click
};

inline Run Catch(double fps, double offset_ms, bool at_once,
                 double open_ms) {
  using namespace catch_math;
  const float from[3] = {0.0f, 100.0f, 0.0f};
  const float place[3] = {4.0f, 111.0f, 2.0f};
  const double frame_ms = 1000.0 / fps;
  const uint32_t press_clock = 5000u;
  const uint32_t press_turn = 50u;
  Run run = {};
  TrackReset(&run.track, from);
  run.sent_ms = -1.0;
  run.taken_ms = -1.0;
  for (int k = 0; k < 400; ++k) {
    const double ms = k * frame_ms;
    const uint32_t clock = press_clock + static_cast<uint32_t>(ms);
    const uint32_t turn =
        press_turn + static_cast<uint32_t>((offset_ms + ms) / 100.0);
    // The step of the pick up action runs from the frame after the press.
    if (k >= 1 && run.sent_ms < 0.0) {
      uint32_t step_clock = press_clock, step_turn = press_turn;
      if (at_once) {
        OldPress(clock, turn, static_cast<uint32_t>(controller_math::kGripClickMs),
                 &step_clock, &step_turn);
      }
      const uint32_t age =
          controller_math::GamePressAge(clock, step_clock, turn, step_turn);
      if (age >= controller_math::kGripClickMs) {
        run.sent_ms = ms;
        // The game takes the object 2 game turns after the turn of the step.
        run.taken_ms =
            (floor((offset_ms + ms) / 100.0) + 2.0) * 100.0 - offset_ms;
      } else if (ms >= open_ms) {
        run.click = true;
        break;
      }
    }
    const bool taken = run.taken_ms >= 0.0 && ms >= run.taken_ms;
    float p[3];
    bool hold = false;
    if (at_once) {
      Picture(from, place,
              hand_exact_math::SettleShare(static_cast<float>(ms * 0.001)), p);
      hold = true;
    } else if (taken) {
      // The settle of the pick up of the game starts at the place of the
      // Hand, which is the place in the palm here.
      for (int i = 0; i < 3; ++i) p[i] = place[i];
      hold = true;
    } else {
      const double t = ms * 0.001;
      p[0] = from[0];
      p[1] = static_cast<float>(from[1] - 100.0 * t - 0.5 * 981.0 * t * t);
      p[2] = from[2];
    }
    TrackAdd(&run.track, p, hold, ms);
    if (taken && ms - run.taken_ms > 200.0) break;
  }
  return run;
}

inline void CatchChecks() {
  using namespace catch_math;
  printf("\nA catch at once\n");

  // THE PRESS IS OLD ENOUGH for the game on both of its ages.
  {
    const uint32_t clocks[5] = {0u, 100u, 224u, 5000u, 0xFFFFFF00u};
    const uint32_t turns[5] = {0u, 1u, 2u, 3u, 70000u};
    bool old = true, exact = true;
    for (int i = 0; i < 5; ++i) {
      for (int j = 0; j < 5; ++j) {
        uint32_t press = 0, press_turn = 0;
        OldPress(clocks[i], turns[j], 225u, &press, &press_turn);
        const uint32_t age = controller_math::GamePressAge(
            clocks[i], press, turns[j], press_turn);
        if (age < 225u) old = false;
        if (age != 225u) exact = false;
      }
    }
    Check(old, "a press that the mod made old is 225 ms old for the game, "
               "with a clock and a game turn that wrap", old, 1);
    Check(exact, "its age is the real time of 225 ms, and the game turns "
                 "give more", exact, 1);
    Check(controller_math::GamePressAge(5011u, 5000u, 50u, 50u) < 225u,
          "the real press is 11 ms old on the first step at 90 frames a "
          "second", controller_math::GamePressAge(5011u, 5000u, 50u, 50u), 11);
  }

  // THE PICTURE starts where it was, and ends at its place in the Hand.
  {
    const float from[3] = {1.0f, 2.0f, 3.0f}, place[3] = {4.0f, 6.0f, 3.0f};
    float p[3];
    Picture(from, place, hand_exact_math::SettleShare(0.0f), p);
    Check(p[0] == from[0] && p[1] == from[1] && p[2] == from[2],
          "at the press the picture is where it was: no jump", p[1], from[1]);
    Picture(from, place,
            hand_exact_math::SettleShare(hand_exact_math::kSettleSeconds), p);
    Check(p[0] == place[0] && p[1] == place[1] && p[2] == place[2],
          "after the settle the picture is at its place in the Hand", p[1],
          place[1]);
    Picture(from, place, 0.5f, p);
    Near("half of the way is the middle", p[1], 4.0, 1e-5);
  }

  // ONE CATCH AT EACH FRAME RATE, at 20 times in a game turn. The gap from
  // the picture to its place in the Hand is 12.2 cm.
  const double rates[4] = {60.0, 72.0, 90.0, 120.0};
  const float gap = sqrtf(4.0f * 4.0f + 11.0f * 11.0f + 2.0f * 2.0f);
  for (int r = 0; r < 4; ++r) {
    const double fps = rates[r];
    const double frame_ms = 1000.0 / fps;
    bool now_first = true, now_still = true, now_smooth = true;
    bool now_sent = true, now_no_click = true;
    bool old_late = true, old_falls = true, old_jumps = true;
    bool old_click = true;
    double worst_step = 0.0, least_fall = 1e9, least_jump = 1e9;
    double latest_sent = 0.0, soonest_old = 1e9;
    for (int i = 0; i < 20; ++i) {
      const double offset = i * 5.0;
      const Run now = Catch(fps, offset, true, 1e9);
      const Run old = Catch(fps, offset, false, 1e9);
      if (now.track.first_hold_frame != 0) now_first = false;
      if (now.track.own != 0.0f || now.track.fell != 0.0f) now_still = false;
      // The settle is a smooth step: its fastest frame is 1.5 frames of the
      // settle time.
      const double most =
          gap * 1.5 * frame_ms / (hand_exact_math::kSettleSeconds * 1000.0);
      if (now.track.jump > most + 0.01) now_smooth = false;
      if (now.track.jump > worst_step) worst_step = now.track.jump;
      if (now.sent_ms < 0.0 || now.sent_ms > frame_ms + 0.01) now_sent = false;
      if (now.sent_ms > latest_sent) latest_sent = now.sent_ms;
      if (old.sent_ms < 225.0) old_late = false;
      if (old.sent_ms < soonest_old) soonest_old = old.sent_ms;
      if (old.track.first_hold_ms < 225.0) old_late = false;
      if (old.track.fell < least_fall) least_fall = old.track.fell;
      if (old.track.jump < least_jump) least_jump = old.track.jump;
      if (old.track.fell < 47.0f) old_falls = false;
      if (old.track.jump < 10.0f * now.track.jump) old_jumps = false;
      // A press of 50 ms: the game sent its pick up before the grip opened,
      // so the release is no click. With the pick up of the game it is one.
      if (Catch(fps, offset, true, 50.0).click) now_no_click = false;
      if (!Catch(fps, offset, false, 50.0).click) old_click = false;
    }
    char what[160];
    snprintf(what, sizeof(what), "%.0f frames a second: the mod draws the "
             "object in the Hand from the frame of the press", fps);
    Check(now_first, what, now_first, 1);
    snprintf(what, sizeof(what), "%.0f frames a second: the object does not "
             "move by itself and does not fall after the press", fps);
    Check(now_still, what, now_still, 1);
    snprintf(what, sizeof(what), "%.0f frames a second: the largest move of "
             "the picture in a frame is the settle (cm)", fps);
    Check(now_smooth, what, worst_step,
          gap * 1.5 * frame_ms / (hand_exact_math::kSettleSeconds * 1000.0));
    snprintf(what, sizeof(what), "%.0f frames a second: the game sends its "
             "pick up on the first step after the press (ms)", fps);
    Check(now_sent, what, latest_sent, frame_ms);
    snprintf(what, sizeof(what), "%.0f frames a second: a press of 50 ms on "
             "an object that moves is no click", fps);
    Check(now_no_click, what, now_no_click, 1);
    snprintf(what, sizeof(what), "%.0f frames a second, catch_at_once=0: the "
             "game sends its pick up 225 ms after the press or later (ms)",
             fps);
    Check(old_late, what, soonest_old, 225.0);
    snprintf(what, sizeof(what), "%.0f frames a second, catch_at_once=0: the "
             "object falls 47 cm or more before the game takes it (cm)", fps);
    Check(old_falls, what, least_fall, 47.0);
    snprintf(what, sizeof(what), "%.0f frames a second, catch_at_once=0: the "
             "picture then jumps to the Hand, 10 times the settle or more "
             "(cm)", fps);
    Check(old_jumps, what, least_jump, 10.0 * worst_step);
    snprintf(what, sizeof(what), "%.0f frames a second, catch_at_once=0: a "
             "press of 50 ms is a click for the game", fps);
    Check(old_click, what, old_click, 1);
  }

  // THE NUMBERS OF A CATCH do not depend on the frame rate: the same picture
  // at 60 and at 120 frames a second gives the same fall within one frame.
  {
    const Run a = Catch(60.0, 35.0, false, 1e9);
    const Run b = Catch(120.0, 35.0, false, 1e9);
    Near("the fall with catch_at_once=0 is the same at 60 and at 120 frames "
         "a second, within the fall of one frame (cm)",
         a.track.fell, b.track.fell, 8.0);
  }
}

}  // namespace catch_checks
