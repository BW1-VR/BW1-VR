// The lines of the hint cards that go away when the player knows them. See
// docs/controller-hints.md, "9.10 The lines that the player knows". No
// Windows and no game here, so the math test compiles this file.
//
// THE RULE (hint_learn=1). Some lines of a card teach one action, for
// example "Grip Take" or "A Teleport". The mod counts the times that the
// player did that action and it worked. After kUses of them the line does
// not show. When the player does not use the action for kComebackSeconds of
// play, the line shows again, and the next use takes it away again. While a
// lesson of the tutorial runs, each line shows. hint_learn=0 shows each line
// always.

#pragma once

#include <stdint.h>
#include <stdlib.h>

namespace hint_learn_math {

// THE ACTIONS that a line of a card teaches.
enum Skill {
  kNoSkill = -1,
  kSkillTake = 0,  // the grip takes an object at the Hand: "Take"
  kSkillCollect,   // the grip takes from a pile, a field or a fish farm:
                   // "Hold to collect"
  kSkillFetch,     // the grip fetches a far object: "Pull to fetch"
  kSkillDrop,      // the grip opens, and the object leaves the Hand: "Open
                   // to drop"
  kSkillWorld,     // a grip moves the land: "Move the land"
  kSkillWalk,      // the stick moves the player: "Move"
  kSkillTeleport,  // A moves the player: "Teleport", "Release to go"
  kSkillRing,      // the ring opens: "Ring menu"
  kSkillSend,      // the trigger sends the Creature: "Send the Creature",
                   // "Release to send"
  kSkills
};

// The names of the actions in the file of the counts and in the log.
const char* const kSkillKey[kSkills] = {"take", "collect",  "fetch",
                                        "drop", "world",    "walk",
                                        "teleport", "ring", "send"};

// A line goes away after this many uses that worked.
const int kUses = 3;
// It comes back after this many seconds of play with no use.
const double kComebackSeconds = 600.0;
// The count stops here, so the file stays small.
const int kMostCount = 9999;

struct Entry {
  int count = 0;
  double last = 0.0;  // the play clock at the last use, in seconds
};

// The player did the action, and it worked.
inline void Done(Entry* e, double play) {
  if (e->count < kMostCount) ++e->count;
  e->last = play;
}

// True when the player knows the action: kUses uses or more, and the last
// one less than `comeback` seconds of play ago.
inline bool Known(const Entry& e, double play, int uses, double comeback) {
  return uses > 0 && e.count >= uses && play - e.last < comeback;
}

// True when a line of this action shows. mode is hint_learn: 0 always, 1
// until learned. lesson: a lesson of the tutorial runs now.
inline bool LineShows(int mode, int skill, const Entry* entries, double play,
                      bool lesson) {
  if (mode == 0 || lesson) return true;
  if (skill < 0 || skill >= kSkills) return true;
  return !Known(entries[skill], play, kUses, kComebackSeconds);
}

// THE LESSONS OF THE TUTORIAL. A step of the tutorial lets the player use
// only the camera controls that it teaches: a script sets the bits of
// CameraHelp::EnabledFeatures through the interface levels of the game
// (docs/god-view.md, "The interface levels"). Level 0, which each script
// sets at its end, is 0x1BF. Any other value is a lesson.
const uint32_t kAllCameraControls = 0x1BF;
inline bool LessonNow(uint32_t features) {
  return features != kAllCameraControls;
}

// THE PLAY CLOCK: the seconds in which a controller points in the game. A
// long frame (a load, a pause of the process) counts as 0.25 s.
inline double PlayStep(double play, double dt) {
  if (!(dt > 0.0)) return play;
  return play + (dt > 0.25 ? 0.25 : dt);
}

// THE TAKE. The grip closes while the Hand is on an object to take (or on a
// bulk source), and some frames later the Hand holds the object. `on` is the
// action of the place of the Hand in this frame (kSkillTake, kSkillCollect
// or kNoSkill). The answer is the action that worked in this frame, or
// kNoSkill. The watch ends when the grip opens, or `limit` seconds after the
// close.
struct TakeWatch {
  bool grip = false;  // the grip in the last frame
  int skill = kNoSkill;
  double since = 0.0;  // the close of the grip
};

inline int TakeStep(TakeWatch* w, int on, bool grip, bool holding, double now,
                    double limit) {
  const bool closed = grip && !w->grip;
  w->grip = grip;
  int done = kNoSkill;
  if (w->skill != kNoSkill) {
    if (holding && grip) {
      done = w->skill;
      w->skill = kNoSkill;
    } else if (!grip || now - w->since > limit) {
      w->skill = kNoSkill;
    }
  }
  if (closed && on != kNoSkill && !holding) {
    w->skill = on;
    w->since = now;
  }
  return done;
}

// THE DROP. The Hand held an object (and not a miracle), and then it holds
// nothing with the grip open: the opening of the grip let the object go.
// known: the mod has the facts of the Hand in this frame. A frame with no
// facts changes nothing.
struct DropWatch {
  bool held = false;
};

inline bool DropStep(DropWatch* w, bool known, bool holding, bool object,
                     bool grip) {
  if (!known) return false;
  if (holding) {
    w->held = object;
    return false;
  }
  const bool done = w->held && !grip;
  w->held = false;
  return done;
}

// A start: true in the first frame of a state.
inline bool Rose(bool* was, bool now) {
  const bool rose = now && !*was;
  *was = now;
  return rose;
}

// THE GRAB OF THE LAND worked when the land moved 5 cm or more in the room,
// turned 3 degrees or more, or changed its scale by 3 percent or more,
// from the press to the release.
inline bool GrabMovedLand(float moved_m, float turned_deg, float scale_ratio) {
  const float zoom = scale_ratio > 1.0f ? scale_ratio - 1.0f
                                        : 1.0f - scale_ratio;
  return moved_m >= 0.05f || turned_deg >= 3.0f || turned_deg <= -3.0f ||
         zoom >= 0.03f;
}

// THE WALK worked when the player went 10 cm or more in the room.
inline bool WalkMoved(float room_m) { return room_m >= 0.10f; }

// THE FILE. One value of an action is "count,last", for example "3,1200".
// A value that does not read gives false and leaves the entry as it is. The
// count stays in 0 to kMostCount, and the last use is not after `play`.
inline bool ParseEntry(const char* text, double play, Entry* out) {
  if (text == nullptr || text[0] == 0) return false;
  char* end = nullptr;
  const long count = strtol(text, &end, 10);
  if (end == text || *end != ',') return false;
  const char* rest = end + 1;
  const double last = strtod(rest, &end);
  if (end == rest) return false;
  out->count = count < 0 ? 0 : (count > kMostCount ? kMostCount
                                                    : static_cast<int>(count));
  out->last = last < 0.0 ? 0.0 : (last > play ? play : last);
  return true;
}

// The play clock in the file. A value that does not read, or a value less
// than 0, gives 0.
inline double ParsePlay(const char* text) {
  if (text == nullptr || text[0] == 0) return 0.0;
  char* end = nullptr;
  const double play = strtod(text, &end);
  if (end == text || !(play > 0.0)) return 0.0;
  return play;
}

}  // namespace hint_learn_math
