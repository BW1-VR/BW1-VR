// THE FRAME BEFORE OF EACH EYE (docs/stereo-artifacts.md, "The frame before
// of each eye").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/eye_clean_math.h"

namespace eye_clean_checks {

inline bool Close(float a, float b, float tolerance) {
  const float d = a - b;
  return d < tolerance && d > -tolerance;
}

// The clean function of the game (fn_0082EE70), as its listing has it: the
// mode, and for mode 0 the timer with its stamp and its flag. It gives what
// the call cleans.
inline eye_clean::Cleaned GameClean(int mode, bool callbacks, int time,
                                    uint32_t* stamp, uint32_t* flag) {
  int now = mode;
  if (now == 0 && callbacks) {
    const int last = static_cast<int>(*stamp);
    if (time < last || last + 2000 < time) {
      now = 1;
      *stamp = static_cast<uint32_t>(time);
      *flag = 1;
    } else if (*flag != 0) {
      *flag = 0;
      now = 1;
    }
  }
  if (now == 1) return eye_clean::kCleanedAll;
  if (now == 2) return eye_clean::kCleanedDepth;
  return eye_clean::kCleanedNothing;
}

// One pixel of the render target through some frames. In each frame the game
// cleans (or not), and each eye pass draws its own level over the pixel with
// an alpha: 1 covers the pixel, less than 1 lets the frame before show, and 0
// does not draw there. `sky` says if the passes draw the sky.
struct Pixel {
  float target = 0.0f;
  float keep[2] = {0.0f, 0.0f};
  bool has[2] = {false, false};
  bool covered = true;
  float seen[2] = {0.0f, 0.0f};  // what each eye sees in the last frame
};

inline void Frame(Pixel* p, int key, int mode, eye_clean::Cleaned cleaned,
                  bool sky, float alpha, const float level[2]) {
  using namespace eye_clean;
  // The clean of the game, after the present of the frame before.
  if (cleaned == kCleanedAll) p->target = 0.0f;
  if (key != kOwn || !FrameBeforeStays(mode, p->covered)) {
    p->has[0] = p->has[1] = false;
  } else if (SeedEyeOne(key, mode, cleaned, p->covered, p->has[1])) {
    p->keep[1] = p->target;
    p->has[1] = true;
  }
  for (int eye = 0; eye < 2; ++eye) {
    const PassPlan plan = Plan(key, mode, cleaned, p->covered, eye);
    if (plan.give && p->has[eye]) p->target = p->keep[eye];
    if (plan.clear == kCleanedAll) p->target = 0.0f;
    if (sky && plan.clean_at_sky) p->target = 0.0f;
    p->target = alpha * level[eye] + (1.0f - alpha) * p->target;
    p->seen[eye] = p->target;
    if (plan.take) {
      p->keep[eye] = p->target;
      p->has[eye] = true;
    }
  }
  p->covered = sky;
}

inline void EyeCleanChecks() {
  using namespace eye_clean;
  printf("\nThe frame before of each eye\n");

  // WHAT THE CLEAN OF THE GAME DOES.
  Check(GameCleaned(kModeAll, true, 5, 5, 0, 0) == kCleanedAll,
        "mode 1 cleans the picture and the Z buffer",
        GameCleaned(kModeAll, true, 5, 5, 0, 0), kCleanedAll);
  Check(GameCleaned(kModeDepth, true, 5, 5, 0, 0) == kCleanedDepth,
        "mode 2 cleans the Z buffer only",
        GameCleaned(kModeDepth, true, 5, 5, 0, 0), kCleanedDepth);
  Check(GameCleaned(kModeNone, true, 5, 5, 0, 0) == kCleanedNothing,
        "mode 0 cleans nothing between two timed cleans",
        GameCleaned(kModeNone, true, 5, 5, 0, 0), kCleanedNothing);
  Check(GameCleaned(kModeNone, true, 5, 2100, 0, 1) == kCleanedAll,
        "mode 0 with a new stamp is the first timed clean",
        GameCleaned(kModeNone, true, 5, 2100, 0, 1), kCleanedAll);
  Check(GameCleaned(kModeNone, true, 2100, 2100, 1, 0) == kCleanedAll,
        "mode 0 with the flag put down is the second timed clean",
        GameCleaned(kModeNone, true, 2100, 2100, 1, 0), kCleanedAll);
  Check(GameCleaned(kModeNone, false, 5, 2100, 0, 1) == kCleanedNothing,
        "mode 0 with the callbacks off has no timed clean",
        GameCleaned(kModeNone, false, 5, 2100, 0, 1), kCleanedNothing);

  // The same answer as the clean function of the game, for 10 s of frames of
  // 11 ms in mode 0. The timer makes 2 cleans in each 2000 ms.
  {
    uint32_t stamp = 0, flag = 0;
    int wrong = 0, cleans = 0;
    for (int time = 100; time < 10100; time += 11) {
      const uint32_t stamp_before = stamp, flag_before = flag;
      const Cleaned did = GameClean(kModeNone, true, time, &stamp, &flag);
      const Cleaned said = GameCleaned(kModeNone, true, stamp_before, stamp,
                                       flag_before, flag);
      if (did != said) ++wrong;
      if (did == kCleanedAll) ++cleans;
    }
    Check(wrong == 0, "10 s of mode 0: the answer is the one of the game in "
          "each frame", wrong, 0);
    Check(cleans == 10, "and the timer of the game made 2 cleans in each 2 s",
          cleans, 10);
  }
  Check(More(kCleanedDepth, kCleanedAll) == kCleanedAll &&
            More(kCleanedNothing, kCleanedDepth) == kCleanedDepth,
        "the larger of two cleans counts", 0, 0);

  // THE PLAN OF AN EYE PASS.
  for (int mode = 0; mode < 3; ++mode) {
    for (int eye = 0; eye < 2; ++eye) {
      const PassPlan off = Plan(kShared, mode, kCleanedAll, false, eye);
      Check(off.clear == kCleanedNothing && !off.give && !off.take &&
                !off.clean_at_sky,
            "eye_clean=0 does nothing to an eye pass", mode, eye);
    }
  }
  {
    const PassPlan left = Plan(kOwn, kModeNone, kCleanedNothing, true, 0);
    const PassPlan right = Plan(kOwn, kModeNone, kCleanedNothing, true, 1);
    Check(left.clean_at_sky && right.clean_at_sky,
          "play (mode 0): each eye pass has a clean before its sky", 0, 0);
    Check(!left.give && !left.take && !right.give && !right.take &&
              left.clear == kCleanedNothing && right.clear == kCleanedNothing,
          "and no keep and no other clean while the passes draw the sky", 0,
          0);
  }
  {
    const PassPlan left = Plan(kOwn, kModeNone, kCleanedNothing, false, 0);
    const PassPlan right = Plan(kOwn, kModeNone, kCleanedNothing, false, 1);
    Check(left.give && left.take && right.give && right.take,
          "mode 0 with no sky in the frame before: each eye has its keep", 0,
          0);
    const PassPlan timed = Plan(kOwn, kModeNone, kCleanedAll, false, 1);
    Check(timed.clear == kCleanedAll && !timed.give && timed.take,
          "after a timed clean of the game eye pass 1 gets the same clean, "
          "and no keep goes into the render target", timed.clear, kCleanedAll);
  }
  {
    const PassPlan left = Plan(kOwn, kModeDepth, kCleanedDepth, true, 0);
    const PassPlan right = Plan(kOwn, kModeDepth, kCleanedDepth, true, 1);
    Check(left.give && left.take && left.clear == kCleanedNothing &&
              !left.clean_at_sky,
          "mode 2: eye pass 0 has the clean of the game and its keep", 0, 0);
    Check(right.give && right.take && right.clear == kCleanedDepth &&
              !right.clean_at_sky,
          "mode 2: eye pass 1 gets a clean of the Z buffer and its keep",
          right.clear, kCleanedDepth);
  }
  {
    const PassPlan left = Plan(kOwn, kModeAll, kCleanedAll, true, 0);
    const PassPlan right = Plan(kOwn, kModeAll, kCleanedAll, true, 1);
    Check(left.clear == kCleanedNothing && right.clear == kCleanedAll &&
              !left.take && !right.take && !right.clean_at_sky,
          "mode 1: eye pass 1 gets the clean of the picture and the Z "
          "buffer, and there is no keep", right.clear, kCleanedAll);
  }

  // ONE PIXEL THAT A THING COVERS FOR THE LEFT EYE ONLY, in the way of the
  // Lionhead logo: mode 2, and each pass draws with an alpha of 160 of 255.
  // The left eye draws the level 1 there, and the right eye the level 0.
  const float logo_alpha = 160.0f / 255.0f;
  const float thing[2] = {1.0f, 0.0f};
  {
    Pixel p;
    for (int i = 0; i < 60; ++i) {
      Frame(&p, kShared, kModeDepth, kCleanedDepth, true, logo_alpha, thing);
    }
    Check(p.seen[1] > 0.25f && p.seen[1] < 0.30f,
          "eye_clean=0: the right eye sees 27 percent of the thing of the "
          "left eye", p.seen[1], 0.2718);
    Check(p.seen[0] > 0.70f && p.seen[0] < 0.75f,
          "eye_clean=0: and the left eye sees its own thing at 73 percent",
          p.seen[0], 0.7288);
  }
  {
    Pixel p;
    for (int i = 0; i < 60; ++i) {
      Frame(&p, kOwn, kModeDepth, kCleanedDepth, true, logo_alpha, thing);
    }
    Check(p.seen[1] < 0.001f,
          "eye_clean=1: the right eye sees nothing of the thing of the left "
          "eye", p.seen[1], 0.0);
    Check(p.seen[0] > 0.999f, "eye_clean=1: the left eye sees its own thing",
          p.seen[0], 1.0);
  }
  // A thing that goes away has a trail in its own eye only.
  {
    Pixel p;
    for (int i = 0; i < 60; ++i) {
      Frame(&p, kOwn, kModeDepth, kCleanedDepth, true, logo_alpha, thing);
    }
    const float gone[2] = {0.0f, 0.0f};
    Frame(&p, kOwn, kModeDepth, kCleanedDepth, true, logo_alpha, gone);
    Check(Close(p.seen[0], 1.0f - logo_alpha, 0.001f) && p.seen[1] < 0.001f,
          "eye_clean=1: a thing that goes away leaves its trail in its own "
          "eye", p.seen[0], 1.0f - logo_alpha);
  }

  // A PIXEL THAT NO PASS DRAWS, in play (mode 0, with the sky). The frame
  // before has the level 0.8 there.
  const float none[2] = {0.5f, 0.5f};
  {
    Pixel p;
    p.target = 0.8f;
    Frame(&p, kShared, kModeNone, kCleanedNothing, true, 0.0f, none);
    Check(Close(p.seen[0], 0.8f, 0.001f) && Close(p.seen[1], 0.8f, 0.001f),
          "eye_clean=0: a pixel that the scene does not cover keeps the old "
          "picture", p.seen[0], 0.8);
  }
  {
    Pixel p;
    p.target = 0.8f;
    Frame(&p, kOwn, kModeNone, kCleanedNothing, true, 0.0f, none);
    Check(p.seen[0] == 0.0f && p.seen[1] == 0.0f,
          "eye_clean=1: such a pixel has the color of the clean in each eye",
          p.seen[0], 0.0);
  }

  // A FRAME OF PLAY THAT DRAWS NO SKY (the video of a falling spell): mode 0,
  // each pass draws with an alpha of 80 of 255 over the frame before. The
  // frames before it drew the sky.
  const float video_alpha = 80.0f / 255.0f;
  {
    Pixel p;
    p.target = 0.8f;
    // The first frame: the mod does not know yet. It is as with eye_clean=0.
    Frame(&p, kOwn, kModeNone, kCleanedNothing, false, video_alpha, thing);
    Check(!p.has[0] && !p.has[1],
          "the first frame with no sky has no keep yet", 0, 0);
    const float before = p.target;
    Frame(&p, kOwn, kModeNone, kCleanedNothing, false, video_alpha, thing);
    Check(p.has[0] && p.has[1], "the second frame has a keep for each eye",
          0, 0);
    Check(Close(p.seen[1], (1.0f - video_alpha) * before, 0.001f),
          "in it eye pass 1 starts from the picture that was in the render "
          "target before eye pass 0", p.seen[1],
          (1.0f - video_alpha) * before);
    for (int i = 0; i < 80; ++i) {
      Frame(&p, kOwn, kModeNone, kCleanedNothing, false, video_alpha, thing);
    }
    Check(p.seen[1] < 0.001f && p.seen[0] > 0.999f,
          "and then each eye has only its own picture", p.seen[1], 0.0);
    // The sky comes back: the keeps go.
    Frame(&p, kOwn, kModeNone, kCleanedNothing, true, 1.0f, thing);
    Frame(&p, kOwn, kModeNone, kCleanedNothing, true, 1.0f, thing);
    Check(!p.has[0] && !p.has[1],
          "when the passes draw the sky again, no keep stays", 0, 0);
  }
  {
    Pixel p;
    for (int i = 0; i < 80; ++i) {
      Frame(&p, kShared, kModeNone, kCleanedNothing, false, video_alpha,
            thing);
    }
    Check(p.seen[1] > 0.3f,
          "eye_clean=0: with no sky the right eye sees the thing of the left "
          "eye", p.seen[1], 0.4);
  }
}

}  // namespace eye_clean_checks
