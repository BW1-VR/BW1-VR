// The throw of a real hand: pure math, no game state. See docs/throw.md, "The
// throw physics". throw_physics.cpp uses it, and the math test checks it.
//
// THE SPEED OF A THROW. A hand slows before the grip reads as open: the
// fingers open at the top speed of the swing, and the grip value falls
// through its threshold some frames later. The mean of the 80 ms before the
// opening then mixes the end of the swing with the start of the stop. So the
// throw takes the top speed of the last 120 ms, as the mean of about 40 ms
// around that moment. The direction is from the same samples.
//
// THE REAL FLIGHT. A meter of the room is s world units. A real throw at v
// m/s is s v units per second in the world, under a gravity of s g. The game
// has one gravity, g units per second squared. But a flight with the velocity
// sqrt(s) v under g is the same curve in a time that is sqrt(s) times longer:
//
//   x(t) = X(sqrt(s) t), with X the flight in the game:
//   dx/dt = sqrt(s) X' = s v, and d2x/dt2 = s X'' = -s g.
//
// So the mod gives the object the velocity sqrt(s) v, and runs the game's own
// step for that one object sqrt(s) times as often. The object then flies in
// the room as a real object flies: the same arc, the same time. Each rule of
// the game (the land, the bounce, the damage) sees the speeds of its own
// physics.

#pragma once

#include <math.h>

#include "controller_math.h"

namespace throw_math {

// The game's gravity in units per second squared (the float at 0x008CF02C),
// and the real one in meters per second squared.
const float kGravity = 9.81f;
// One substep of the game's physics, in seconds (the float at 0x008C7674). A
// game turn is 20 of them.
const float kSubstep = 0.005f;
const int kSubstepsInTurn = 20;

// The top speed is searched in this time before the opening of the grip.
const double kPeakWindow = 0.120;
// The velocity at a sample is the mean from this time before it to this time
// after it.
const double kPeakHalfSpan = 0.024;
// A top speed at or above this is a swing, in m/s. A still hand after a swing
// throws.
const float kSwingMps = 1.0f;
// The release sends at least this speed across the land, in units per second.
// The game sets an object down at 2 or less (InitialisePhysicsFromHand).
const float kSendFlat = 2.5f;

// THE TOP SPEED OF A SWING. The velocity at each sample from the time `from`
// to the time `to` is the mean over kPeakHalfSpan each side of it. The answer
// is the largest of them, in units of the point per second, and the time of
// its sample. With a slow frame rate the mean is over the step before the
// sample. False with no pair of samples 4 ms or more apart.
inline bool PeakVelocity(const controller_math::MotionSample* samples,
                         int count, double from, double to, double half_span,
                         float out[3], double* at_out) {
  bool found = false;
  double best = -1.0;
  for (int k = 0; k < count; ++k) {
    const double t = samples[k].time;
    if (t < from - 1e-6 || t > to + 1e-6) continue;
    int a = k, b = k;
    while (a > 0 && t - samples[a - 1].time <= half_span + 1e-6) --a;
    while (b < count - 1 && samples[b + 1].time - t <= half_span + 1e-6 &&
           samples[b + 1].time <= to + 1e-6) {
      ++b;
    }
    if (a == b) {
      if (k == 0) continue;
      a = k - 1;
    }
    const double span = samples[b].time - samples[a].time;
    if (!(span >= 0.004)) continue;
    double v[3], speed2 = 0.0;
    for (int i = 0; i < 3; ++i) {
      v[i] = (samples[b].point[i] - samples[a].point[i]) / span;
      speed2 += v[i] * v[i];
    }
    if (speed2 > best) {
      best = speed2;
      for (int i = 0; i < 3; ++i) out[i] = static_cast<float>(v[i]);
      if (at_out != nullptr) *at_out = t;
      found = true;
    }
  }
  return found;
}

// A SET DOWN. A release sets the object down when the hand was still at the
// opening and no swing came before it. "Still" is the rule of
// TableThrowVelocity on the mean of the last 80 ms: the speed across the
// land at the life size scale, times the gain, is drop_speed or less. v_end
// and v_peak are in units at the life size scale (life units in a meter).
inline bool SetDown(const float v_end_room[3], const float v_peak_room[3],
                    float gain, float life, float drop_speed,
                    float swing_mps) {
  const float flat = sqrtf(v_end_room[0] * v_end_room[0] +
                           v_end_room[2] * v_end_room[2]) * gain;
  const float peak_mps =
      life > 0.0f ? controller_math::Length(v_peak_room) / life : 0.0f;
  return !(flat > drop_speed) && !(peak_mps >= swing_mps);
}

// THE HEIGHT RULE. A hand that is still at the opening of the grip sets the
// object down only when it is near the land. A still hand higher up than the
// limit lets the object fall. height_m is the height of the Hand's point over
// the land (or over the sea) in meters of the room, at the scale now.
// limit_m of 0 or less is the set down at each height.
inline bool FallsFromHeight(float height_m, float limit_m) {
  return limit_m > 0.0f && height_m > limit_m;
}

// THE TIME FACTOR k. The real flight can look too fast in a small world. So
// the flight can run k times slower than real, with the same arc: the same
// landing point, and k times the flight time. In the room the speed is v / k
// and the gravity is g / k^2. In the world that is s v / k under s g / k^2.
// The game's gravity g and its velocity sqrt(s) v do not change. Only the
// number of game steps in a substep is sqrt(s) / k. So k = 1 is the real
// flight, and k = sqrt(s) is the square root law (one step a substep). The
// game cannot run less than one step a substep, so k is cut to sqrt(s).
// *rate is the steps in a substep, and the answer is the k that is in use.
inline float SlowFactor(float k, float scale, float* rate) {
  if (!(k > 1.0f)) k = 1.0f;
  if (k > scale) k = scale;
  *rate = scale / k;
  if (*rate < 1.0f) *rate = 1.0f;
  return k;
}

// What a release with the real flight does.
struct RealThrow {
  bool drop;        // set the object down
  bool fall;        // a still hand over the height limit: the object falls
                    // with the velocity of the hand at the opening
  float real_mps;   // the speed of the hand, meters per second
  float scale;      // sqrt(s): the game's velocity is the real one over this
  float rate;       // sqrt(s) / k: the game's step runs this many times as
                    // often in a substep
  float slow;       // the k that is in use (1 or more, sqrt(s) at most)
  float gravity;    // s / k^2: the gravity of the flight, in s g units
  float world[3];   // the real flight: s v, world units per second
  float flight[3];  // the flight that is drawn: s v / k
  float game[3];    // the same in the game's time: sqrt(s) v
  float sent[3];    // what the release action sends
};

// THE PLAN OF A RELEASE. v_peak_room is the velocity of the grip in the
// room's axes, in units at the life size scale (life units in a meter), and
// room is the room basis L. s is the world units in a meter of the room.
//
// The velocity that the release sends only starts the game's physics. It is
// the velocity in the game's time, cut to the cap, and with a speed across
// the land of kSendFlat or more, so the game throws the object. The mod then
// writes game[] to the physics object, with no cap.
//
// With fall set, a release that would be a set down is a fall: the object
// leaves with the mean velocity of the hand at the opening (v_end_room), which
// is near zero across the land. The game still gets kSendFlat across the
// land, because it sets down each object that is slower than that.
inline void RealThrowPlan(const float v_end_room[3],
                          const float v_peak_room[3], const float room[9],
                          float gain, float life, float s, float cap,
                          float drop_speed, RealThrow* out,
                          float slow = 1.0f, bool fall = false) {
  *out = RealThrow{};
  out->scale = s > 0.0f ? sqrtf(s) : 1.0f;
  if (out->scale < 1.0f) out->scale = 1.0f;
  out->slow = SlowFactor(slow, out->scale, &out->rate);
  out->gravity = s > 0.0f ? s / (out->slow * out->slow) : 1.0f;
  if (!(life > 0.0f) || !(s > 0.0f)) {
    out->drop = true;
    return;
  }
  out->real_mps = controller_math::Length(v_peak_room) / life;
  const bool still = SetDown(v_end_room, v_peak_room, gain, life, drop_speed,
                             kSwingMps) ||
                     !(out->real_mps > 1e-4f);
  out->fall = fall && still;
  out->drop = still && !fall;
  if (out->drop) return;
  const float* v_room = out->fall ? v_end_room : v_peak_room;
  if (out->fall) out->real_mps = controller_math::Length(v_room) / life;
  float along[3];
  controller_math::Apply(room, false, v_room, along);
  for (int i = 0; i < 3; ++i) {
    out->world[i] = along[i] * (s / life);
    out->flight[i] = out->world[i] / out->slow;
    out->game[i] = out->world[i] / out->scale;
    out->sent[i] = out->game[i];
  }
  const float speed = controller_math::Length(out->sent);
  if (cap > 0.0f && speed > cap) {
    for (int i = 0; i < 3; ++i) out->sent[i] *= cap / speed;
  }
  const float flat = sqrtf(out->sent[0] * out->sent[0] +
                           out->sent[2] * out->sent[2]);
  if (flat < kSendFlat) {
    if (flat > 1e-4f) {
      out->sent[0] *= kSendFlat / flat;
      out->sent[2] *= kSendFlat / flat;
    } else {
      out->sent[0] = kSendFlat;
      out->sent[2] = 0.0f;
    }
  }
}

// How many times the game's step runs for the object in one substep, so that
// over time it runs `scale` times as often (the rate of the time factor). *remainder keeps the part of a
// step that is left for the next substep.
inline int MiniSteps(float scale, float* remainder) {
  if (!(scale > 1.0f)) {
    *remainder = 0.0f;
    return 1;
  }
  *remainder += scale;
  int n = static_cast<int>(floorf(*remainder));
  *remainder -= static_cast<float>(n);
  if (n < 1) n = 1;
  return n;
}

// THE REAL ARC in the world: p0 + v0 t - (s g / 2) t^2 along world up, t
// seconds after the release. v0 is world units per second. With the time
// factor k, pass v0 / k and s / k^2: the same arc, k times slower.
inline void RealArc(const float p0[3], const float v0[3], float s, double t,
                    float p[3], float v[3]) {
  const double g = static_cast<double>(s) * kGravity;
  for (int i = 0; i < 3; ++i) {
    p[i] = static_cast<float>(p0[i] + v0[i] * t);
    v[i] = v0[i];
  }
  p[1] = static_cast<float>(p0[1] + v0[1] * t - 0.5 * g * t * t);
  v[1] = static_cast<float>(v0[1] - g * t);
}

// One step of the game's integrator in a free flight with no drag
// (fn_007FD4D0 adds the weight, and fn_007FE260 adds the force to the
// velocity and then the velocity to the position). The math test flies an
// object with it.
inline void GameFreeStep(float p[3], float v[3]) {
  v[1] -= kGravity * kSubstep;
  for (int i = 0; i < 3; ++i) p[i] += v[i] * kSubstep;
}

// THE FLIGHT THAT THE MOD DRAWS. The game runs its physics once a game turn,
// 10 times a second, and it draws each object where the turn left it. A turn
// of the real flight moves an object 50 cm of the room at 5 m/s. So
// throw_physics.cpp keeps the object's matrix after each substep with its
// time in the picture, and it draws the object where the flight is at the
// time of the frame.
struct FlightSample {
  double time;      // seconds of the picture
  float matrix[12]; // the mesh matrix: 3 rows of the axes, then the position
  float velocity[3];  // the speed of the picture, world units per second
  bool contact;     // the object touched the land or an object in this step
  float center[3];  // the center of the object (the mesh can be off center)
};

// THE TIME OF A SUBSTEP IN THE PICTURE (throw_draw). The game runs 20
// substeps in a turn, and a turn is due each 100 ms. So a substep is 5 ms of
// the real time, with each count of steps in it. In a substep of the flight
// the steps are `rate` of them, and their time of the flight is 5 ms too. In
// a substep where another object touches the thrown object, one step runs,
// and its time of the flight is 5 ms / rate. With that short time in the
// picture (the rule 0), a turn of 20 such substeps is 30 ms of the picture at
// a rate of 3.33, while the real time goes on by 100 ms: the picture comes to
// the newest step, stands there, and goes on from a new place after the next
// turn.
//
// So with the rule 1 a substep with a touch of another object is 5 ms of the
// picture, and the object moves through it at the speed of the game's own
// time. `span` is the time of the flight of the substep.
const int kDrawFlight = 0;
const int kDrawReal = 1;

inline double PictureSpan(double span, bool hit, int rule) {
  return rule == kDrawReal && hit ? static_cast<double>(kSubstep) : span;
}

// The speed of the picture for one unit of the speed of the game: the steps
// that ran in the substep, over the time that the substep has in the picture.
inline float PictureRate(int steps, double picture_span) {
  if (steps < 1 || !(picture_span > 1e-9)) return 1.0f;
  return static_cast<float>(steps * static_cast<double>(kSubstep) /
                            picture_span);
}

// After the newest sample the flight goes on for this time at most. A game
// turn is due each 100 ms, and it comes one frame early or late.
const double kFlightAhead = 0.050;

// Where the object is at the time `clock`. False before the first sample.
// Between two samples the position is on the line between them, and the axes
// are those of the nearer sample. After the newest sample a free flight goes
// on with the gravity of the real flight (s g), and an object in contact goes
// on along its velocity across the land. Both stop kFlightAhead after the
// newest sample. *late_out is the time of `clock` after the newest sample, 0
// when it is not after it. velocity_out gets the speed of the picture at that
// time, in world units per second.
inline bool FlightAt(const FlightSample* samples, int count, double clock,
                     float s, float matrix_out[12], double* late_out,
                     float* velocity_out = nullptr) {
  if (late_out != nullptr) *late_out = 0.0;
  if (count < 1 || clock < samples[0].time) return false;
  const FlightSample& last = samples[count - 1];
  if (clock >= last.time) {
    double ahead = clock - last.time;
    if (late_out != nullptr) *late_out = ahead;
    if (ahead > kFlightAhead) ahead = kFlightAhead;
    for (int i = 0; i < 12; ++i) matrix_out[i] = last.matrix[i];
    for (int i = 0; i < 3; ++i) {
      if (velocity_out != nullptr) velocity_out[i] = last.velocity[i];
      if (last.contact && i == 1) continue;
      matrix_out[9 + i] = static_cast<float>(last.matrix[9 + i] +
                                             last.velocity[i] * ahead);
    }
    if (!last.contact) {
      matrix_out[10] -= static_cast<float>(
          0.5 * static_cast<double>(s) * kGravity * ahead * ahead);
    }
    return true;
  }
  int hi = count - 1;
  while (hi > 1 && samples[hi - 1].time > clock) --hi;
  const FlightSample& a = samples[hi - 1];
  const FlightSample& b = samples[hi];
  const double span = b.time - a.time;
  const double u = span > 1e-9 ? (clock - a.time) / span : 1.0;
  const FlightSample& axes = u < 0.5 ? a : b;
  for (int i = 0; i < 9; ++i) matrix_out[i] = axes.matrix[i];
  for (int i = 0; i < 3; ++i) {
    matrix_out[9 + i] = static_cast<float>(
        a.matrix[9 + i] + (b.matrix[9 + i] - a.matrix[9 + i]) * u);
    if (velocity_out != nullptr) {
      velocity_out[i] = span > 1e-9
                            ? static_cast<float>(
                                  (b.matrix[9 + i] - a.matrix[9 + i]) / span)
                            : b.velocity[i];
    }
  }
  return true;
}

// THE PICTURE DOES NOT GO BACK (throw_draw=1). A frame that the mod draws
// after the newest step is a guess: the object goes on from that step with
// its own speed. When the next steps come, the object can be behind the
// guess, because a contact took a part of its speed. The picture then stays
// at the place of the guess until the object comes to it. It does not go
// back along the path.
//
// guess is the place of the guess, and dir the direction that the guess
// moved in (it needs no unit length). place and velocity are the object at
// the time of this frame, from the steps or from a new guess. The picture
// stays while the object is behind the plane through `guess` across `dir`
// and still moves to it. When the object turned in a contact, it does not
// come to that place: the picture goes with the object at once. `held` is
// the time that the picture stayed, and it never stays longer than `most`.
inline bool GuessHolds(const float place[3], const float velocity[3],
                       const float guess[3], const float dir[3], double held,
                       double most) {
  if (!(held < most)) return false;
  const float behind = (place[0] - guess[0]) * dir[0] +
                       (place[1] - guess[1]) * dir[1] +
                       (place[2] - guess[2]) * dir[2];
  if (!(behind < 0.0f)) return false;
  return velocity[0] * dir[0] + velocity[1] * dir[1] + velocity[2] * dir[2] >
         0.0f;
}

// THE CLOCK FALLS BACK (throw_draw=1). When the steps are more than
// kFlightAhead behind the clock, the clock of the flight waits. It is then
// kFlightAhead in front of the newest step, and with a turn each 100 ms each
// second frame is a guess from then on. So the clock runs kFallBackShare
// slower until it has given that time back. *debt is the time that is left,
// and the answer is the time that this frame gives back.
const double kFallBackShare = 0.2;

inline double ClockFallBack(double* debt, double frame_seconds) {
  if (!(*debt > 0.0) || !(frame_seconds > 0.0)) return 0.0;
  double pay = kFallBackShare * frame_seconds;
  if (pay > *debt) pay = *debt;
  *debt -= pay;
  return pay;
}

// THE TURN BETWEEN TWO MATRICES of the object, in radians. Each row is an
// axis of the object times a scale, and the scales of the three rows can be
// different. For a turn by the angle t about the unit vector n, the sum of
// the dot products of the unit axes is 1 + 2 cos t, and the sum of their
// cross products is 2 sin t n. The sine keeps a small angle exact. 0 for a
// matrix with no size.
inline float TurnBetween(const float a[12], const float b[12]) {
  float dot = 0.0f, cross[3] = {0.0f, 0.0f, 0.0f};
  for (int r = 0; r < 3; ++r) {
    const float* p = a + 3 * r;
    const float* q = b + 3 * r;
    const float size = controller_math::Length(p) * controller_math::Length(q);
    if (!(size > 1e-12f)) return 0.0f;
    dot += (p[0] * q[0] + p[1] * q[1] + p[2] * q[2]) / size;
    cross[0] += (p[1] * q[2] - p[2] * q[1]) / size;
    cross[1] += (p[2] * q[0] - p[0] * q[2]) / size;
    cross[2] += (p[0] * q[1] - p[1] * q[0]) / size;
  }
  return atan2f(0.5f * controller_math::Length(cross), 0.5f * (dot - 1.0f));
}

// WHAT THE OBJECT DOES AFTER ITS FIRST CONTACT, in numbers for the log. The
// caller adds the state of the object after each substep, from the first
// contact on. All lengths are world units, and the times are seconds of the
// flight (the time of the picture).
//
// A hop is a time with no point in contact that lasts kHopLeast or more. Its
// height is the rise of the center over the land from the moment that the
// object left the land. A bob is a change of the vertical direction of the
// center after a move of `dead_band` or more: each hop gives two, and an
// object that shakes on the land gives many small ones.
//
// A bob is from the height of the center in the world, and not from its
// height over the land. LH3DIsland::GetAltitude 0x00803090 takes only the
// top 8 bits of the place in a map cell (0x008032F9, 0x00803318), so the
// land of the game is flat in squares of 10 / 256 units. On a slope the
// height over the land of an object that moves has a step at the edge of
// each square, and each step would count as a bob.
const double kHopLeast = 0.030;

struct Landing {
  int samples;
  double first;        // the time of the first contact
  double last;
  float at[3];         // the center at the newest sample
  float over;          // its height over the land at the newest sample
  float path;          // the way of the center across the land
  float top_speed;     // the largest speed of the picture, units per second
  // The hops.
  bool air;
  double air_from;
  float air_base;
  float air_top;
  int hops;
  float top_hop;       // the highest hop
  double long_hop;     // the longest hop
  double air_time;     // the sum of the hops
  // The bobs of the height of the center.
  int dir;             // 1 up, -1 down, 0 not known yet
  float extreme;       // the top or the bottom that the height goes to now
  float turn_at;       // the top or the bottom of the bob before
  int bobs;
  float bob_sum;       // the sum of the moves between a top and a bottom
  // The turn of the object.
  float turned;        // radians in all
  float top_turn;      // radians per second
};

inline void LandingEndHop(Landing* l, double time) {
  if (!l->air) return;
  l->air = false;
  const double span = time - l->air_from;
  if (span < kHopLeast) return;
  ++l->hops;
  l->air_time += span;
  if (span > l->long_hop) l->long_hop = span;
  const float rise = l->air_top - l->air_base;
  if (rise > l->top_hop) l->top_hop = rise;
}

// `center` is the center in the world, `over` is its height over the land,
// `touch` is true while a point of the object is in contact, and `turn` is
// the turn since the sample before, in radians.
inline void LandingAdd(Landing* l, double time, const float center[3],
                       float over, bool touch, float turn, float dead_band) {
  const float height = center[1];
  if (l->samples == 0) {
    l->first = time;
    l->extreme = height;
  } else {
    const double dt = time - l->last;
    const float dx = center[0] - l->at[0];
    const float dy = center[1] - l->at[1];
    const float dz = center[2] - l->at[2];
    l->path += sqrtf(dx * dx + dz * dz);
    if (dt > 1e-6) {
      const float speed =
          sqrtf(dx * dx + dy * dy + dz * dz) / static_cast<float>(dt);
      if (speed > l->top_speed) l->top_speed = speed;
      const float rate = turn / static_cast<float>(dt);
      if (rate > l->top_turn) l->top_turn = rate;
    }
    l->turned += turn;
  }
  // The hops. The height of a hop is from the last sample on the land.
  if (touch) {
    LandingEndHop(l, time);
  } else if (!l->air) {
    l->air = true;
    l->air_from = time;
    l->air_base = l->samples > 0 ? l->over : over;
    l->air_top = over > l->air_base ? over : l->air_base;
  } else if (over > l->air_top) {
    l->air_top = over;
  }
  ++l->samples;
  l->last = time;
  l->over = over;
  for (int i = 0; i < 3; ++i) l->at[i] = center[i];
  // The bobs. `extreme` follows the height while it goes on in the
  // direction `dir`. A move of dead_band against it makes a bob.
  if (l->dir == 0) {
    if (fabsf(height - l->extreme) >= dead_band) {
      l->dir = height > l->extreme ? 1 : -1;
      l->turn_at = l->extreme;
      l->extreme = height;
    }
  } else if ((height - l->extreme) * static_cast<float>(l->dir) > 0.0f) {
    l->extreme = height;
  } else if ((l->extreme - height) * static_cast<float>(l->dir) >=
             dead_band) {
    ++l->bobs;
    l->bob_sum += fabsf(l->extreme - l->turn_at);
    l->turn_at = l->extreme;
    l->dir = -l->dir;
    l->extreme = height;
  }
}

// THE PICTURE FROM FRAME TO FRAME. The caller adds the place of the object on
// each frame after the first contact. A frame is late when the mod drew it
// after the newest step of the physics. The picture turns back when the move
// of a frame is against the move of the frame before, and each of the two is
// `dead_band` or longer: a bounce does that, and so does a wrong guess of
// the draw. back_late counts the turns back that came in the frame after a
// late frame: those are from a guess.
//
// The picture stands and jumps when it does not move in a frame (less than
// `dead_band`), after a frame in which it moved, and the first move after
// that is kJumpBands of `dead_band` or longer. top_jump is the longest of
// those first moves, and top_step is the longest move of one frame.
const float kJumpBands = 4.0f;

struct DrawTrack {
  int frames;
  int late;
  double top_late;     // seconds
  int back;
  int back_late;
  float top_back;      // the longest move against the move before
  int jumps;
  float top_jump;
  float top_step;
  bool has_last;
  bool has_step;
  bool was_late;
  bool moved;          // the picture moved in a frame before the stand
  bool stands;         // it does not move now
  float last[3];
  float step[3];
};

inline void DrawTrackAdd(DrawTrack* d, const float p[3], double late,
                         float dead_band) {
  ++d->frames;
  if (late > 0.0) {
    ++d->late;
    if (late > d->top_late) d->top_late = late;
  }
  if (d->has_last) {
    float step[3];
    for (int i = 0; i < 3; ++i) step[i] = p[i] - d->last[i];
    const float length = controller_math::Length(step);
    if (d->has_step && length >= dead_band &&
        controller_math::Length(d->step) >= dead_band &&
        step[0] * d->step[0] + step[1] * d->step[1] + step[2] * d->step[2] <
            0.0f) {
      ++d->back;
      if (d->was_late) ++d->back_late;
      if (length > d->top_back) d->top_back = length;
    }
    if (length > d->top_step) d->top_step = length;
    if (length < dead_band) {
      if (d->moved) d->stands = true;
    } else {
      if (d->stands && length >= kJumpBands * dead_band) {
        ++d->jumps;
        if (length > d->top_jump) d->top_jump = length;
      }
      d->stands = false;
      d->moved = true;
    }
    for (int i = 0; i < 3; ++i) d->step[i] = step[i];
    d->has_step = true;
  }
  d->was_late = late > 0.0;
  for (int i = 0; i < 3; ++i) d->last[i] = p[i];
  d->has_last = true;
}

// THE TIME AFTER THE LANDING. In the flight the game's step runs sqrt(s) / k
// times a substep, so that the arc is that of the real throw. The contact of
// the game is made for one step a substep: its springs are soft, and the turn
// of an object has a cap of 1.5 turns a second. At 5 steps a substep a rock
// turns 7.5 times a second and its center bobs 7 times a second. So after the
// first contact the rate goes from the rate of the flight to the rate of the
// land (1 is the game's own time).
//
// The rate changes only while the object touches, with the time constant
// kLandEase of the time of the picture. `touch` is the sum of that time. A hop
// then keeps one rate from the land to the land, so its arc is a true arc.
// The rate has no step, so the speed of the picture has no step.
const double kLandEase = 0.10;

inline float LandRate(float rate_flight, float rate_land, double touch,
                      double ease) {
  if (!(rate_land < rate_flight)) return rate_flight;
  if (!(ease > 0.0)) return rate_land;
  return rate_land + (rate_flight - rate_land) *
                         static_cast<float>(exp(-touch / ease));
}

// THE LOW BOUNCE. The contact of the game is a spring, and a spring gives
// back the energy that it took. The mod cuts the force of the contact while
// that force pushes the object along its own motion: that is the way back of
// the spring. `keep` is the part of the force that stays, from 0 to 1. A
// bounce that keeps the share e of its speed keeps e^2 of its energy, so
// keep is e^2 (ReboundKeep).
//
// A speed along the force of kReboundSoft or more gets the full cut. A slower
// one gets a part of the cut in proportion, so an object that lies on the
// land has the full force and does not sink into it. The answer is the factor
// for the force of the contact in this step.
const float kReboundSoft = 0.5f;

inline float ReboundKeep(float share) {
  if (!(share > 0.0f)) return 0.0f;
  if (share > 1.0f) return 1.0f;
  return share * share;
}

inline float ReboundFactor(const float force[3], const float velocity[3],
                           float keep, float soft) {
  const float size = controller_math::Length(force);
  if (!(size > 1e-6f) || !(keep < 1.0f)) return 1.0f;
  if (keep < 0.0f) keep = 0.0f;
  const float along = (force[0] * velocity[0] + force[1] * velocity[1] +
                       force[2] * velocity[2]) / size;
  if (!(along > 0.0f)) return 1.0f;
  float part = soft > 0.0f ? along / soft : 1.0f;
  if (part > 1.0f) part = 1.0f;
  return 1.0f - (1.0f - keep) * part;
}

// THE BRAKE OF THE LAND. A real object that lands gives its speed to the
// land, and it does not roll far. The contact of the game has friction only
// at a point that slides. A rock that turns over its points keeps its speed,
// and it rolls for 10 s of the game's time. So the mod adds a force against
// the velocity of the center while the object touches the land. Its size is
// `friction` times the force of the contact, the law of dry friction. It is
// never more than the force that takes the velocity to 0 in this step, so it
// stops the object and does not push it back. On a slope the object stays
// when the tangent of the slope is less than `friction`. `out` is the force
// to add.
//
// The turn has its own brake: the game keeps a part of the turn of an object
// in each second (PhysOb+0x148), and for a thrown object after its first
// contact that part is kLandTurnKeep at most.
const float kLandTurnKeep = 0.25f;

// THE SETTLE OF A TREE. A tree that falls hits the land on one corner. The
// contact gives it an angular momentum that is many times the cap of 1.5
// turns a second, and the cap hides it: the tree turns at the cap, with its
// center still, until the keep factor above has worn the momentum down. The
// tree then needs 3 to 5 s to come to rest, and its mesh point goes round and
// up and down all that time. So a tree that touches the land and has a center
// speed of kSettleSpeed units a second or less (the game's time) keeps only
// kSettleTurnKeep of its turn in each second. The speed test leaves the
// first bounce and a real roll alone.
const float kSettleSpeed = 6.0f;
const float kSettleTurnKeep = 0.001f;

// THE REST AFTER A LANDING (throw_land_rest). Three things keep a thrown
// object from its rest after its center is slow.
//
// A TURN IN PLACE. An object that rolls turns at its speed over its radius.
// The contact of the game gives an object more turn than that, as it gives a
// tree: the object lies with its center still and turns over its points, at
// the cap of 1.5 turns a second at first, for 2 to 5 s. The rest test of the
// game needs a turn below 0.5 radians a second. So an object that touches
// the land and turns more than kSpinOverRoll times as fast as a roll at its
// speed keeps only kSettleTurnKeep of its turn in each second (TurnsInPlace).
// A roll keeps the turn of the brake. turn_rate is in radians a second,
// radius in world units, and speed in units a second, all in the game's time.
//
// STEEP LAND. The brake holds an object on a slope up to its friction angle
// (the tangent of the slope is the friction of the brake). On steeper land
// the object must go down until the land is flat. But there the brake still
// acts against the motion, on top of the friction of the game, and the turn
// of the object keeps only a part of itself. The object then goes down in
// small moves: 0.6 to 1.3 units a second, with stops, for 10 to 30 s on a
// slope of 42 to 75 degrees. So the brake is off on land that it can not
// hold the object on (SteepBrakeFactor): it has its full size up to its
// friction angle, and it goes to 0 where the tangent of the slope is
// kSteepFade times its friction. There the object goes down with the
// friction of the game, as in the plain game. normal_up is the up part of the
// unit normal of the land, and friction is the friction of the brake.
//
// AN OBJECT THAT LIES STILL. The rest test of the game (fn_007FE260) has
// three limits: the speed below 1 unit a second, the turn below 0.5 radians a
// second, and the torque over m r^2 below 1. A small object has a small
// r^2, so the springs of its points keep its torque over the limit while it
// lies still: an object with a radius of 0.5 units did not move for 5 s and
// was not at rest.
// The game makes its limits wider only after 15 s of life. So an object
// lies still when a point is in contact, its speed is below kStillSpeed and
// its turn is below kStillTurn (LiesStill). After kStillSeconds of that with
// no break, the mod gives the game the answer of an object that came to rest
// (StillTime gives the time so far).
const int kRestOff = 0;    // the turn of the brake, and the brake on each land
const int kRestAll = 1;
const int kRestSpin = 2;   // the turn in place only
const int kRestSteep = 3;  // steep land only
const int kRestStill = 4;  // an object that lies still only
const float kSpinOverRoll = 2.0f;
const float kSteepFade = 1.15f;
const float kStillSpeed = 0.1f;   // units a second of the game's time
const float kStillTurn = 0.5f;    // radians a second, the limit of the game
const double kStillSeconds = 0.5;

inline bool RestSpin(int rule) {
  return rule == kRestAll || rule == kRestSpin;
}
inline bool RestSteep(int rule) {
  return rule == kRestAll || rule == kRestSteep;
}
inline bool RestStill(int rule) {
  return rule == kRestAll || rule == kRestStill;
}

inline bool LiesStill(bool touching, float speed, float turn_rate) {
  return touching && speed < kStillSpeed && turn_rate < kStillTurn;
}

// The time that the object lay still, after a substep of `span` seconds of
// the game's time. `still` is the time before the substep.
inline double StillTime(double still, bool touching, float speed,
                        float turn_rate, double span) {
  return LiesStill(touching, speed, turn_rate) ? still + span : 0.0;
}

inline bool TurnsInPlace(float turn_rate, float radius, float speed) {
  if (!(turn_rate > 0.0f) || !(radius > 0.0f)) return false;
  return turn_rate * radius > kSpinOverRoll * speed;
}

inline float SteepBrakeFactor(float normal_up, float friction) {
  if (!(friction > 0.0f) || !(normal_up < 1.0f)) return 1.0f;
  if (!(normal_up > 1e-4f)) return 0.0f;
  const float tangent = sqrtf(1.0f - normal_up * normal_up) / normal_up;
  const float steep = tangent / friction;
  if (!(steep > 1.0f)) return 1.0f;
  if (!(steep < kSteepFade)) return 0.0f;
  return (kSteepFade - steep) / (kSteepFade - 1.0f);
}

// The turn factor of one step for an object after its first contact.
// `game_keep` is the game's factor of the object. `brake_on` gives the land
// keep, and `settle` the keep of the settle while the object touches and is
// slow. `in_place` gives the same keep while the object touches and turns in
// place. The answer is never more than `game_keep`.
inline float TurnKeepAfterLanding(float game_keep, bool brake_on, bool settle,
                                  bool touching, float speed,
                                  bool in_place = false) {
  float keep = game_keep;
  if (brake_on) {
    const float most = powf(kLandTurnKeep, kSubstep);
    if (keep > most) keep = most;
  }
  if (touching && ((settle && speed <= kSettleSpeed) || in_place)) {
    const float least = powf(kSettleTurnKeep, kSubstep);
    if (keep > least) keep = least;
  }
  return keep;
}

inline void LandBrake(const float push[3], const float velocity[3],
                      float mass, float friction, float out[3]) {
  out[0] = out[1] = out[2] = 0.0f;
  const float normal = controller_math::Length(push);
  const float speed = controller_math::Length(velocity);
  if (!(friction > 0.0f) || !(normal > 1e-6f) || !(speed > 1e-6f) ||
      !(mass > 0.0f)) {
    return;
  }
  float size = friction * normal;
  const float stop = mass * speed / kSubstep;
  if (size > stop) size = stop;
  for (int i = 0; i < 3; ++i) out[i] = -size * velocity[i] / speed;
}

// A RELEASE IN THE GLIDE OF A FAR FETCH. The flight starts at the place of
// the object in the Hand. The picture of the object was at the point of the
// glide. So the picture starts there and comes to the flight in kCatchUp
// seconds: the answer is the part of that distance that is left at the time
// t of the flight, from 1 to 0.
const double kCatchUp = 0.15;

inline float CatchUpLeft(double t, double seconds) {
  if (!(seconds > 0.0) || t >= seconds) return 0.0f;
  if (t <= 0.0) return 1.0f;
  const double u = 1.0 - t / seconds;
  return static_cast<float>(u * u * u);
}

}  // namespace throw_math
