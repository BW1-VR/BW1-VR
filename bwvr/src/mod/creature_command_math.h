// The math of the Creature commands. Pure functions with no game state.
// creature_command.cpp calls them, and test/controller_math checks them
// (command_checks.h). See docs/creature.md, "The commands".
#pragma once

#include <math.h>

namespace command_math {

// THE SHAKE OF THE REAL HAND. A shake is a row of straight strokes, each one
// back along the stroke before it. The detector follows the grip in the room,
// in meters, and finds the turning points:
//
//   - The anchor is the last turning point. The far point is the sample that
//     is farthest from the anchor since then.
//   - When the hand comes back kShakeBackM from the far point, the far point
//     is a turning point, and the stroke from the anchor to it is done.
//   - A stroke counts when it is kShakeStrokeM long or more, its mean speed
//     is the threshold or more, and its path is almost a line (the length
//     over the path is kShakeStraight or more, so a circle does not count).
//   - A stroke that counts adds to the row only when it goes back along the
//     stroke before it (the dot of the two unit directions is kShakeBackDot
//     or less). If not, it starts a new row.
//   - kShakeStrokes strokes in a row, from the start of the first to the end
//     of the last in kShakeWindowS or less, are a shake.
//
// A hand that stays in a ball of kShakeStillM around the anchor has not
// started a stroke, so the start time of the stroke moves with the clock. A
// stroke that is not done after kShakeStrokeMostS ends the row. So does a
// gap of kShakeGapS between two samples (the tracking was lost).
const float kShakeStrokeM = 0.06f;
const float kShakeBackM = 0.02f;
const float kShakeStillM = 0.015f;
const float kShakeStraight = 0.75f;
const float kShakeBackDot = -0.5f;
const int kShakeStrokes = 4;
const double kShakeWindowS = 0.9;
const double kShakeStrokeMostS = 0.6;
const double kShakeGapS = 0.1;
const double kShakeRestS = 1.0;  // no second shake before this time is over

struct ShakeState {
  bool have;
  double last_time;
  float last[3];
  float anchor[3];
  double anchor_time;
  float far_point[3];
  float far_distance;
  double far_time;
  float path;           // the length of the path since the anchor
  float far_path;       // the path when the hand was at the far point
  int count;            // the strokes of the row
  double start[kShakeStrokes];  // the start time of each stroke of the row
  float direction[3];   // the unit direction of the last stroke that counted
  float slowest;        // the slowest stroke of the row, in m/s
  double quiet_until;
};

// What a shake was, for the log.
struct ShakeResult {
  int strokes;
  float seconds;   // from the start of the first stroke to the end of the last
  float slowest;   // the mean speed of the slowest stroke, in m/s
  float length;    // the length of the last stroke, in meters
};

inline void ShakeReset(ShakeState* s) {
  s->have = false;
  s->count = 0;
  s->quiet_until = 0.0;
}

inline void ShakeAnchor(ShakeState* s, const float p[3], double t) {
  for (int i = 0; i < 3; ++i) {
    s->anchor[i] = p[i];
    s->far_point[i] = p[i];
  }
  s->anchor_time = t;
  s->far_distance = 0.0f;
  s->far_time = t;
  s->path = 0.0f;
  s->far_path = 0.0f;
}

// One sample: the grip in the room, in meters, and its time in seconds. mps
// is the threshold: the mean speed that each stroke needs. True on the sample
// that completes a shake, and then out says what it was.
inline bool ShakeStep(ShakeState* s, float mps, const float p[3], double t,
                      ShakeResult* out) {
  if (!s->have || t < s->last_time || t - s->last_time > kShakeGapS) {
    const double quiet = s->have ? s->quiet_until : 0.0;
    s->have = true;
    s->count = 0;
    s->quiet_until = quiet;
    s->last_time = t;
    for (int i = 0; i < 3; ++i) s->last[i] = p[i];
    ShakeAnchor(s, p, t);
    return false;
  }
  {
    const float dx = p[0] - s->last[0], dy = p[1] - s->last[1],
                dz = p[2] - s->last[2];
    s->path += sqrtf(dx * dx + dy * dy + dz * dz);
  }
  s->last_time = t;
  for (int i = 0; i < 3; ++i) s->last[i] = p[i];

  const float ax = p[0] - s->anchor[0], ay = p[1] - s->anchor[1],
              az = p[2] - s->anchor[2];
  const float d = sqrtf(ax * ax + ay * ay + az * az);
  if (s->far_distance < kShakeStillM && d < kShakeStillM) {
    // The hand has not left the anchor: no stroke has started. This is only
    // after a new anchor, where the row is empty.
    s->anchor_time = t;
    s->far_time = t;
    s->path = 0.0f;
    s->far_path = 0.0f;
    return false;
  }
  if (d > s->far_distance) {
    s->far_distance = d;
    s->far_time = t;
    s->far_path = s->path;
    for (int i = 0; i < 3; ++i) s->far_point[i] = p[i];
    if (t - s->anchor_time > kShakeStrokeMostS) {
      // A slow move away: not a stroke of a shake.
      s->count = 0;
      ShakeAnchor(s, p, t);
    }
    return false;
  }
  if (s->far_distance - d < kShakeBackM) {
    if (t - s->anchor_time > kShakeStrokeMostS) {
      s->count = 0;
      ShakeAnchor(s, p, t);
    }
    return false;
  }

  // The far point is a turning point. The stroke from the anchor to it is
  // done.
  const float length = s->far_distance;
  const double seconds = s->far_time - s->anchor_time;
  const float speed =
      static_cast<float>(length / (seconds > 1e-3 ? seconds : 1e-3));
  const float straight = s->far_path > 1e-6f ? length / s->far_path : 1.0f;
  float direction[3] = {0.0f, 0.0f, 0.0f};
  if (length > 1e-6f) {
    for (int i = 0; i < 3; ++i) {
      direction[i] = (s->far_point[i] - s->anchor[i]) / length;
    }
  }
  const bool counts =
      length >= kShakeStrokeM && speed >= mps && straight >= kShakeStraight;
  const double stroke_start = s->anchor_time;
  const double stroke_end = s->far_time;
  if (!counts) {
    s->count = 0;
  } else {
    const float along = direction[0] * s->direction[0] +
                        direction[1] * s->direction[1] +
                        direction[2] * s->direction[2];
    if (s->count > 0 && along <= kShakeBackDot) {
      if (s->count == kShakeStrokes) {
        for (int i = 1; i < kShakeStrokes; ++i) s->start[i - 1] = s->start[i];
        --s->count;
      }
      s->start[s->count++] = stroke_start;
      if (speed < s->slowest) s->slowest = speed;
    } else {
      s->count = 1;
      s->start[0] = stroke_start;
      s->slowest = speed;
    }
    for (int i = 0; i < 3; ++i) s->direction[i] = direction[i];
  }

  // The turning point is the new anchor, and the way back from it is the
  // start of the next stroke.
  const float turn[3] = {s->far_point[0], s->far_point[1], s->far_point[2]};
  const float back_path = s->path - s->far_path;
  ShakeAnchor(s, turn, stroke_end);
  s->path = back_path;
  {
    const float bx = p[0] - turn[0], by = p[1] - turn[1], bz = p[2] - turn[2];
    s->far_distance = sqrtf(bx * bx + by * by + bz * bz);
    s->far_time = t;
    s->far_path = back_path;
    for (int i = 0; i < 3; ++i) s->far_point[i] = p[i];
  }

  if (!counts || s->count < kShakeStrokes || t < s->quiet_until) return false;
  const double whole = stroke_end - s->start[0];
  if (whole > kShakeWindowS) return false;
  if (out != nullptr) {
    out->strokes = s->count;
    out->seconds = static_cast<float>(whole);
    out->slowest = s->slowest;
    out->length = length;
  }
  s->count = 0;
  s->quiet_until = t + kShakeRestS;
  return true;
}

// WHAT A RELEASE OF THE TRIGGER DOES. The press chose its kind when it
// started: an object under the aim, the player's own Creature under the aim,
// or the land. A press on an object or on the Creature acts only when the
// aim is still on it at the release. A press on the land acts when its arc
// meets land that the Creature can go to.
enum PressKind { kPressNone, kPressObject, kPressCreature, kPressLand };
enum Command { kCommandNone, kCommandObject, kCommandLeash, kCommandLand };

// The longest press on the Creature that is a leash click, as the game's own
// limit of 450 ms for a press on the Creature.
const double kLeashClickS = 0.45;

// WHICH AIM A PRESS ON THE LAND USES. A press shorter than hold_s is a tap:
// the player pointed, and then clicked. The pull of the trigger and its
// release move the hand, so a tap uses the aim of the press, the first aim
// after the trigger closed. A longer press is a hold: the player moved the
// arc to a place, so a hold uses the aim of the release. A tap whose first
// aim met no land uses the aim of the release, if that one is valid.
enum LandAim { kLandAimNone, kLandAimPress, kLandAimRelease };

inline LandAim ReleaseLandAim(double held_s, double hold_s, bool press_valid,
                              bool release_valid) {
  if (held_s < hold_s && press_valid) return kLandAimPress;
  return release_valid ? kLandAimRelease : kLandAimNone;
}

// land_valid: the aim that ReleaseLandAim chose is valid.
inline Command ReleaseCommand(PressKind kind, double held_s, bool still_on,
                              bool land_valid) {
  switch (kind) {
    case kPressObject:
      return still_on ? kCommandObject : kCommandNone;
    case kPressCreature:
      return still_on && held_s < kLeashClickS ? kCommandLeash : kCommandNone;
    case kPressLand:
      return land_valid ? kCommandLand : kCommandNone;
    default:
      return kCommandNone;
  }
}

// THE MARK OF A COMMAND. After a send to the land, the ring stays on the
// place for kSentMarkS, so a tap shows where the Creature goes. The ring does
// not move.
const double kSentMarkS = 0.6;

inline bool SentMarkShows(double sent_at, double now) {
  return sent_at > 0.0 && now >= sent_at && now - sent_at < kSentMarkS;
}

// THE TARGET OF A COMMAND IN THE AIM (docs/creature.md, "The target of an
// object"). With the leash on, the land is the first target of the trigger:
// a press with no object in the aim sends the Creature to the land. So an
// object must not take the aim easily. Three rules, each with a band so a
// target at the border does not go on and off.
//
// THE CONE. The angle from the aim ray to the body of the object is at most
// select degrees for a new target, and keep degrees for the target of the
// last frame. The body is upright: the line from the base to the top, with
// the 2D radius around it. So the aim on the crown of a tree is on the tree.
//
// THE RANGE. The object is at most max_m meters of the room from the head,
// along the floor, and kAimRangeKeepM more for the target of the last frame.
// 0 gives no limit.
//
// THE LAND IN FRONT WINS. When the aim ray meets the land before it comes to
// the object, the player points at that land. The object is out when it is
// more than behind_m meters, plus kAimBehindShare of the distance of the land
// point, behind the land point. The target of the last frame stays to
// kAimBehindKeep times that.
struct AimRule {
  float select_deg;
  float keep_deg;
  float max_m;      // 0: no limit
  float behind_m;   // less than 0: the rule is off
};

const float kAimRangeKeepM = 0.15f;
const float kAimBehindShare = 0.2f;
const float kAimBehindKeep = 1.5f;
// Another target takes the aim only when its score is this much lower.
const float kAimSwitchDeg = 1.0f;

// The aim against an upright body: the line from base to base + height along
// +y, with radius around it. te is the angle from the ray to the nearest
// point of that line, less the angle that the radius covers there, in
// degrees, and 0 when the ray goes through the body. along is how far that
// point is along the ray. Not valid when that point is behind the origin.
struct BodyAim {
  bool valid;
  float te;
  float along;
  float distance;  // from the origin to that point
};

inline BodyAim AimAtBody(const float origin[3], const float direction[3],
                         const float base[3], float height, float radius) {
  BodyAim out = {false, 0.0f, 0.0f, 0.0f};
  if (!(height > 0.0f)) height = 0.0f;
  // The angle to the point at the share t of the height.
  const auto angle = [&](float t, float* along, float* distance) {
    const float to[3] = {base[0] - origin[0],
                         base[1] + t * height - origin[1],
                         base[2] - origin[2]};
    const float d = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    const float a = to[0] * direction[0] + to[1] * direction[1] +
                    to[2] * direction[2];
    *along = a;
    *distance = d;
    if (!(d > 1e-4f)) return 0.0f;
    float c = a / d;
    if (c > 1.0f) c = 1.0f;
    if (c < -1.0f) c = -1.0f;
    return acosf(c) * 57.29578f;
  };
  // The angle has one least value on the line, so the search halves the
  // part of the line that holds it.
  float low = 0.0f, high = 1.0f, along = 0.0f, distance = 0.0f;
  for (int i = 0; i < 24 && height > 0.0f; ++i) {
    const float a = low + (high - low) / 3.0f;
    const float b = high - (high - low) / 3.0f;
    if (angle(a, &along, &distance) < angle(b, &along, &distance)) {
      high = b;
    } else {
      low = a;
    }
  }
  const float t = angle(0.5f * (low + high), &along, &distance);
  if (!(distance > 1e-4f) || !(along > 0.0f)) return out;
  const float covered =
      atanf((radius > 0.0f ? radius : 0.0f) / distance) * 57.29578f;
  out.valid = true;
  out.te = t > covered ? t - covered : 0.0f;
  out.along = along;
  out.distance = distance;
  return out;
}

// How far along a ray the land is. ground(x, z) is the land height at a
// world point, with the sea at 0. The ray goes in steps of step to most, and
// then the last step is halved 10 times. 0 when the origin is under the
// land. Less than 0 when the ray meets no land in that distance.
template <typename Ground>
inline float RayLandDistance(const float origin[3], const float direction[3],
                             float step, float most, Ground ground) {
  if (!(step > 0.0f) || !(most > 0.0f)) return -1.0f;
  const auto over = [&](float t) {
    return origin[1] + t * direction[1] -
           ground(origin[0] + t * direction[0], origin[2] + t * direction[2]);
  };
  if (!(over(0.0f) > 0.0f)) return 0.0f;
  float before = 0.0f;
  for (float t = step; t <= most + 0.5f * step; t += step) {
    if (over(t) > 0.0f) {
      before = t;
      continue;
    }
    float low = before, high = t;
    for (int i = 0; i < 10; ++i) {
      const float middle = 0.5f * (low + high);
      if (over(middle) > 0.0f) {
        low = middle;
      } else {
        high = middle;
      }
    }
    return high;
  }
  return -1.0f;
}

// Whether the land in front of an object wins. along is how far the body is
// along the ray, less its radius. land is how far the land point of the ray
// is, and less than 0 when the ray meets no land. All in world units, and
// units is the world units in a meter of the room.
inline bool AimLandWins(const AimRule& rule, float along, float land,
                        float units, bool current) {
  if (rule.behind_m < 0.0f || land < 0.0f) return false;
  float allow = rule.behind_m * units + kAimBehindShare * land;
  if (current) allow *= kAimBehindKeep;
  return along > land + allow;
}

// Whether an object is in the range. floor_m is its distance from the head
// along the floor, in meters of the room.
inline bool AimInRange(const AimRule& rule, float floor_m, bool current) {
  if (!(rule.max_m > 0.0f)) return true;
  return floor_m <= rule.max_m + (current ? kAimRangeKeepM : 0.0f);
}

// THE TARGET WHILE THE TRIGGER IS HELD. The player sees one of two things:
// the halo of an object, or the arc and the ring on the land. What he sees
// is what the release acts on. candidate is what the aim is on in this
// frame: an object, the Creature, or nothing.
//
//   - No object in the aim: the land shows at once.
//   - The land shows and an object comes into the aim: the object must stay
//     in the aim for kHeldCatchS before it shows. So a ring that the player
//     moves across the land does not jump to each object that it passes.
//   - From one object to a different one: at once. The cone has its own band
//     (AimRule), and a different object must have a lower score.
//   - A press that began on the land can keep the land (land_stays): then no
//     object shows for as long as the trigger is held, and the aim only moves
//     the land point.
enum ShownKind { kShownNone, kShownLand, kShownObject, kShownCreature };

struct Shown {
  int kind;
  const void* object;  // null for the land
};

const double kHeldCatchS = 0.10;

struct HeldState {
  Shown shown;
  const void* waiting;    // an object that is in the aim, and does not show yet
  double waiting_since;
  bool land_stays;        // the press began on the land, and it stays a walk
};

// land_stays is wanted only for a press that began on the land.
inline void HeldStart(HeldState* s, Shown shown, bool land_stays = false) {
  s->land_stays = land_stays && shown.kind == kShownLand;
  s->shown = shown;
  s->waiting = nullptr;
  s->waiting_since = 0.0;
}

// One frame. The answer is what shows now.
inline Shown HeldStep(HeldState* s, Shown candidate, double now) {
  if (s->land_stays) {
    s->waiting = nullptr;
    s->shown.kind = kShownLand;
    s->shown.object = nullptr;
    return s->shown;
  }
  const bool object =
      candidate.kind == kShownObject || candidate.kind == kShownCreature;
  if (!object || candidate.object == nullptr) {
    s->waiting = nullptr;
    s->shown.kind = kShownLand;
    s->shown.object = nullptr;
    return s->shown;
  }
  if (s->shown.kind != kShownLand) {
    s->waiting = nullptr;
    s->shown = candidate;
    return s->shown;
  }
  if (s->waiting != candidate.object) {
    s->waiting = candidate.object;
    s->waiting_since = now;
  }
  if (now - s->waiting_since >= kHeldCatchS) {
    s->waiting = nullptr;
    s->shown = candidate;
  }
  return s->shown;
}

// WHAT A RELEASE DOES WITH THE LIVE TARGET. A press shorter than hold_s is a
// tap: the player pointed, and then clicked, so the tap acts on what showed
// at the press. A longer press is a hold, and it acts on what shows at the
// release. The leash click needs a press that started on the Creature and
// that is shorter than kLeashClickS. press_land_valid and land_valid say
// that the land aim of the press, and of the release, is land that the
// Creature can go to.
struct LiveRelease {
  Command command;
  bool from_press;  // the target is the target of the press
};

inline LiveRelease ReleaseLive(double held_s, double hold_s, int press,
                               int shown, bool press_land_valid,
                               bool land_valid) {
  LiveRelease out = {kCommandNone, false};
  if (held_s < hold_s) {
    out.from_press = true;
    switch (press) {
      case kShownObject:
        out.command = kCommandObject;
        return out;
      case kShownCreature:
        out.command = kCommandLeash;
        return out;
      case kShownLand:
        if (press_land_valid) {
          out.command = kCommandLand;
          return out;
        }
        // A tap whose first aim met no land uses the aim of the release.
        out.from_press = false;
        out.command = land_valid && shown == kShownLand ? kCommandLand
                                                        : kCommandNone;
        return out;
      default:
        out.from_press = false;
        return out;
    }
  }
  switch (shown) {
    case kShownObject:
      out.command = kCommandObject;
      break;
    case kShownCreature:
      out.command = press == kShownCreature && held_s < kLeashClickS
                        ? kCommandLeash
                        : kCommandNone;
      break;
    case kShownLand:
      out.command = land_valid ? kCommandLand : kCommandNone;
      break;
    default:
      break;
  }
  return out;
}

// THE DOUBLE PRESS. In the game a click on an object with the leash on the
// Hand sends the Creature to it, and a double click ties the leash to it
// (fn_005D5190: the double click runs fn_005D4210 and no click). So the
// first tap of the trigger sends the Creature at once, and a second press on
// the same object ties the leash. The second press must start window_s or
// less after the first press started, as the double click of Windows. The
// first press must be a tap.
inline bool DoublePress(double press_s, double first_press_s, double window_s,
                        const void* object, const void* first_object) {
  if (object == nullptr || object != first_object) return false;
  if (!(window_s > 0.0)) return false;
  return press_s >= first_press_s && press_s - first_press_s <= window_s;
}

}  // namespace command_math
