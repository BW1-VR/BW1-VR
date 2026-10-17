// THE LINES THAT THE PLAYER KNOWS (docs/controller-hints.md, "9.10 The lines
// that the player knows").
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <stdio.h>

#include "../../src/mod/hint_learn_math.h"

namespace learn_checks {

inline void LearnChecks() {
  using namespace hint_learn_math;
  printf("\nThe lines that the player knows\n");

  // THE COUNT AND THE TIME TO COME BACK.
  {
    Entry take;
    Check(!Known(take, 0.0, kUses, kComebackSeconds),
          "no use: the line shows", 0, 0);
    Done(&take, 10.0);
    Done(&take, 20.0);
    Check(!Known(take, 21.0, kUses, kComebackSeconds),
          "two uses: the line shows", take.count, 2);
    Done(&take, 30.0);
    Check(Known(take, 30.0, kUses, kComebackSeconds),
          "the third use takes the line away at once", take.count, 3);
    Check(Known(take, 30.0 + 599.0, kUses, kComebackSeconds),
          "599 s of play with no use: the line stays away", 599, 600);
    Check(!Known(take, 30.0 + 600.0, kUses, kComebackSeconds),
          "600 s of play with no use: the line comes back", 600, 600);
    Done(&take, 700.0);
    Check(Known(take, 700.0, kUses, kComebackSeconds) && take.last == 700.0,
          "one use after the line came back takes it away again", take.count,
          4);
    // Each use starts the time again, so a player who uses the action keeps
    // the line away.
    Entry walk;
    for (int i = 0; i < 40; ++i) Done(&walk, 300.0 * i);
    Check(Known(walk, 300.0 * 39 + 599.0, kUses, kComebackSeconds),
          "a use each 300 s: the line stays away", walk.count, 40);
    Entry most;
    most.count = kMostCount;
    Done(&most, 1.0);
    Check(most.count == kMostCount, "the count stops at its limit", most.count,
          kMostCount);
  }

  // WHEN A LINE SHOWS.
  {
    Entry entries[kSkills];
    for (int i = 0; i < kUses; ++i) Done(&entries[kSkillFetch], 50.0);
    Check(LineShows(0, kSkillFetch, entries, 60.0, false),
          "hint_learn=0: a known line shows", 1, 1);
    Check(!LineShows(1, kSkillFetch, entries, 60.0, false),
          "hint_learn=1: a known line does not show", 0, 0);
    Check(LineShows(1, kSkillFetch, entries, 60.0, true),
          "hint_learn=1 in a lesson of the tutorial: a known line shows", 1,
          1);
    Check(LineShows(1, kSkillTake, entries, 60.0, false),
          "a line of an action with no uses shows", 1, 1);
    Check(LineShows(1, kNoSkill, entries, 60.0, false),
          "a line that teaches no counted action shows", 1, 1);
    Check(LineShows(1, kSkills, entries, 60.0, false),
          "a line with a number past the actions shows", 1, 1);
  }

  // THE LESSONS OF THE TUTORIAL: the camera controls of each level that a
  // script uses (docs/god-view.md, "The interface levels").
  {
    Check(!LessonNow(0x1BF), "level 0, all controls: no lesson",
          LessonNow(0x1BF) ? 1 : 0, 0);
    const uint32_t lessons[] = {0x48, 0x8, 0x18, 0x0, 0xA, 0x1, 0x1A, 0x1B,
                                0x3F};
    int all = 0;
    for (uint32_t f : lessons) all += LessonNow(f) ? 1 : 0;
    Check(all == 9, "each other level that a script uses is a lesson", all,
          9);
  }

  // THE PLAY CLOCK.
  {
    double play = 0.0;
    for (int i = 0; i < 90 * 3600; ++i) play = PlayStep(play, 1.0 / 90.0);
    Near("an hour of frames at 90 a second is 3600 s", play, 3600.0, 0.01);
    Check(PlayStep(10.0, 30.0) == 10.25, "a frame of 30 s counts as 0.25 s",
          PlayStep(10.0, 30.0), 10.25);
    Check(PlayStep(10.0, -1.0) == 10.0, "a frame with no time adds nothing",
          PlayStep(10.0, -1.0), 10.0);
  }

  // THE TAKE: the close of the grip on the object, and the Hand holds it 9
  // frames later, with the grip still closed. The frames are 1/90 s.
  {
    const double dt = 1.0 / 90.0;
    TakeWatch w;
    int done = kNoSkill;
    TakeStep(&w, kSkillTake, false, false, 0.0, 1.0);
    TakeStep(&w, kSkillTake, true, false, dt, 1.0);  // the close
    for (int i = 2; i < 11; ++i) {
      // The pick up state: the Hand is on no target.
      const int d = TakeStep(&w, kNoSkill, true, false, i * dt, 1.0);
      if (d != kNoSkill) done = d;
    }
    Check(done == kNoSkill, "no take before the Hand holds the object", done,
          kNoSkill);
    done = TakeStep(&w, kNoSkill, true, true, 11 * dt, 1.0);
    Check(done == kSkillTake, "the Hand holds the object: one take", done,
          kSkillTake);
    done = TakeStep(&w, kNoSkill, true, true, 12 * dt, 1.0);
    Check(done == kNoSkill, "the next frames do not count it again", done,
          kNoSkill);

    // A close on a bulk source counts for the collect.
    TakeWatch c;
    TakeStep(&c, kSkillCollect, false, false, 0.0, 1.0);
    TakeStep(&c, kSkillCollect, true, false, dt, 1.0);
    done = TakeStep(&c, kNoSkill, true, true, 20 * dt, 1.0);
    Check(done == kSkillCollect, "a bulk source: one collect", done,
          kSkillCollect);

    // A close on nothing, a grip that opens first, and a game that did not
    // take the object in 1 s count nothing.
    TakeWatch n;
    TakeStep(&n, kNoSkill, false, false, 0.0, 1.0);
    TakeStep(&n, kNoSkill, true, false, dt, 1.0);
    done = TakeStep(&n, kNoSkill, true, true, 5 * dt, 1.0);
    Check(done == kNoSkill, "a close on no object to take is no take", done,
          kNoSkill);
    TakeWatch o;
    TakeStep(&o, kSkillTake, false, false, 0.0, 1.0);
    TakeStep(&o, kSkillTake, true, false, dt, 1.0);
    TakeStep(&o, kSkillTake, false, false, 2 * dt, 1.0);
    done = TakeStep(&o, kNoSkill, false, true, 3 * dt, 1.0);
    Check(done == kNoSkill, "a grip that opens before the take is no take",
          done, kNoSkill);
    TakeWatch l;
    TakeStep(&l, kSkillTake, false, false, 0.0, 1.0);
    TakeStep(&l, kSkillTake, true, false, dt, 1.0);
    TakeStep(&l, kNoSkill, true, false, 1.2, 1.0);
    done = TakeStep(&l, kNoSkill, true, true, 1.3, 1.0);
    Check(done == kNoSkill, "an object in the Hand 1.3 s after the close is "
          "no take", done, kNoSkill);
    // The Hand holds an object already: a new close is no take.
    TakeWatch h;
    TakeStep(&h, kSkillTake, false, true, 0.0, 1.0);
    TakeStep(&h, kSkillTake, true, true, dt, 1.0);
    done = TakeStep(&h, kSkillTake, true, true, 2 * dt, 1.0);
    Check(done == kNoSkill, "a close with an object in the Hand is no take",
          done, kNoSkill);
  }

  // THE DROP.
  {
    DropWatch d;
    DropStep(&d, true, true, true, true);
    DropStep(&d, true, true, true, false);  // the grip opens
    // A frame with no facts of the Hand changes nothing.
    Check(!DropStep(&d, false, false, false, false),
          "a frame with no facts is no drop", 0, 0);
    Check(DropStep(&d, true, false, false, false),
          "the Hand is empty with the grip open: one drop", 1, 1);
    Check(!DropStep(&d, true, false, false, false),
          "the next frames do not count it again", 0, 0);
    DropWatch g;
    DropStep(&g, true, true, true, true);
    Check(!DropStep(&g, true, false, false, true),
          "the object went with the grip closed: no drop", 0, 0);
    DropWatch m;
    DropStep(&m, true, true, false, true);  // a miracle in the Hand
    Check(!DropStep(&m, true, false, false, false),
          "a miracle that leaves the Hand is no drop", 0, 0);
  }

  // A START, THE GRAB AND THE WALK.
  {
    bool was = false;
    const bool a = Rose(&was, true);
    const bool b = Rose(&was, true);
    Rose(&was, false);
    const bool c = Rose(&was, true);
    Check(a && !b && c, "the ring opens: one start for each opening",
          (a ? 1 : 0) + (b ? 1 : 0) + (c ? 1 : 0), 2);
    Check(!GrabMovedLand(0.04f, 0.0f, 1.0f), "a grab of 4 cm moved nothing",
          0.04, 0.05);
    Check(GrabMovedLand(0.06f, 0.0f, 1.0f), "a grab of 6 cm moved the land",
          0.06, 0.05);
    Check(GrabMovedLand(0.0f, -45.0f, 1.0f), "a turn of 45 degrees moved it",
          -45, 3);
    Check(GrabMovedLand(0.0f, 0.0f, 1.05f) && GrabMovedLand(0.0f, 0.0f, 0.95f),
          "a zoom of 5 percent each way moved it", 5, 3);
    Check(!GrabMovedLand(0.01f, 1.0f, 1.01f),
          "a hand that shakes moved nothing", 0, 0);
    Check(!WalkMoved(0.05f) && WalkMoved(0.5f),
          "a walk of 5 cm is no walk, and a walk of 50 cm is one", 0.5, 0.10);
  }

  // THE FILE. Each read comes before its check, so the check shows what it
  // read.
  {
    Entry e;
    bool ok = ParseEntry("3,1200", 2000.0, &e);
    Check(ok && e.count == 3 && e.last == 1200.0,
          "\"3,1200\" reads as 3 uses, the last at 1200 s", e.count, 3);
    ok = ParseEntry("3,5000", 2000.0, &e);
    Check(ok && e.last == 2000.0,
          "a last use after the play clock is the play clock", e.last, 2000);
    ok = ParseEntry("-4,10", 2000.0, &e);
    Check(ok && e.count == 0, "a count less than 0 is 0", e.count, 0);
    ok = ParseEntry("99999,1", 2000.0, &e);
    Check(ok && e.count == kMostCount, "a count past the limit is the limit",
          e.count, kMostCount);
    Entry kept;
    kept.count = 2;
    kept.last = 7.0;
    const bool bad = ParseEntry("abc", 10.0, &kept) ||
                     ParseEntry("3", 10.0, &kept) ||
                     ParseEntry("", 10.0, &kept) ||
                     ParseEntry(nullptr, 10.0, &kept) ||
                     ParseEntry("3,", 10.0, &kept);
    Check(!bad && kept.count == 2 && kept.last == 7.0,
          "a value that does not read leaves the entry as it is", kept.count,
          2);
    Check(ParsePlay("1234") == 1234.0 && ParsePlay("") == 0.0 &&
              ParsePlay("-5") == 0.0 && ParsePlay("x") == 0.0,
          "the play clock reads, and a bad value is 0", ParsePlay("1234"),
          1234);
    // The values that SaveCounts writes read back as they were.
    Entry out;
    out.count = 5;
    out.last = 4321.0;
    char text[32];
    snprintf(text, sizeof(text), "%d,%.0f", out.count, out.last);
    Entry back;
    ok = ParseEntry(text, 5000.0, &back);
    Check(ok && back.count == 5 && back.last == 4321.0,
          "a written value reads back", back.count, 5);
  }
}

}  // namespace learn_checks
