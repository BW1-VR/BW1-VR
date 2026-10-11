// The math of the pointer and of the camera in a room of the Temple. See
// temple.cpp and docs/controllers.md, "The Temple".
//
// Pure functions: no game memory and no Windows. The checks are in
// test/controller_math/temple_checks.h.

#pragma once

#include <math.h>

namespace temple_math {

inline float Dot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline float Distance(const float a[3], const float b[3]) {
  const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  return sqrtf(Dot(d, d));
}

// THE DEPTH OF THE RAY. The game finds the thing under its pointer with a
// line from the eye through a pixel, and it gives the point where that line
// meets the room (hit). The ray of the hand does not start at the eye. So the
// mod needs the point of the ray that the eye sees on the thing.
//
// This is the distance along the ray (origin, unit direction) to the plane
// through hit that faces the eye. When the hit is on the ray, the result is
// the distance to the hit. So a pointer that is at rest comes to the pixel
// where the ray and the line of the eye meet on the thing.
//
// False when the eye is at the hit, when the ray goes along the plane or away
// from it, or when the plane is behind the origin.
inline bool RayToHitPlane(const float origin[3], const float direction[3],
                          const float eye[3], const float hit[3],
                          float* along) {
  float normal[3] = {hit[0] - eye[0], hit[1] - eye[1], hit[2] - eye[2]};
  const float length = sqrtf(Dot(normal, normal));
  if (!(length > 1e-4f)) return false;
  for (int i = 0; i < 3; ++i) normal[i] /= length;
  const float toward = Dot(direction, normal);
  if (!(toward > 0.05f)) return false;
  const float to_hit[3] = {hit[0] - origin[0], hit[1] - origin[1],
                           hit[2] - origin[2]};
  const float t = Dot(to_hit, normal) / toward;
  if (!(t > 0.0f)) return false;
  *along = t;
  return true;
}

// The distance from a point to the ray (origin, unit direction). A point
// behind the origin measures to the origin.
inline float PointToRay(const float origin[3], const float direction[3],
                        const float point[3]) {
  const float to[3] = {point[0] - origin[0], point[1] - origin[1],
                       point[2] - origin[2]};
  float t = Dot(to, direction);
  if (t < 0.0f) t = 0.0f;
  const float foot[3] = {origin[0] + direction[0] * t,
                         origin[1] + direction[1] * t,
                         origin[2] + direction[2] * t};
  return Distance(point, foot);
}

// The angle at the eye between the points a and b, in degrees. 180 when a
// point is at the eye.
inline float AngleAtEye(const float eye[3], const float a[3],
                        const float b[3]) {
  const float u[3] = {a[0] - eye[0], a[1] - eye[1], a[2] - eye[2]};
  const float v[3] = {b[0] - eye[0], b[1] - eye[1], b[2] - eye[2]};
  const float lu = sqrtf(Dot(u, u)), lv = sqrtf(Dot(v, v));
  if (!(lu > 1e-4f) || !(lv > 1e-4f)) return 180.0f;
  float c = Dot(u, v) / (lu * lv);
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 57.29578f;
}

// Where the depth of the ray came from on a frame.
enum DepthSource {
  kDepthDefault = 0,  // no hit yet: the depth of the settings
  kDepthHit = 1,      // the hit of the game under the pointer
  kDepthKept = 2      // no good hit on this frame: the depth of before
};

struct Depth {
  bool have;     // along is set
  float along;   // the distance along the ray, in world units
  int source;    // DepthSource
};

// Half of the way to a new depth on each frame. At the edge of a near thing
// in front of a far wall, the pixel of the near depth can be on the wall, and
// the pixel of the far depth on the near thing. With the full step the depth
// then changes on each frame. With half a step it comes to rest between the
// two. A depth less than 0.5 percent from its target takes the target.
const float kDepthBlend = 0.5f;
const float kDepthSnap = 0.005f;

// One frame of the depth. hit_ok says that the game gave a good hit, and
// hit_along is RayToHitPlane for it. fallback is the depth of the settings.
// least and most are the limits of a depth. Returns the depth of this frame.
inline float DepthStep(Depth* depth, bool hit_ok, float hit_along,
                       float fallback, float least, float most) {
  if (hit_ok) {
    float target = hit_along;
    if (target < least) target = least;
    if (target > most) target = most;
    if (!depth->have || depth->source == kDepthDefault) {
      depth->along = target;
    } else {
      depth->along += (target - depth->along) * kDepthBlend;
      if (fabsf(target - depth->along) <= kDepthSnap * target) {
        depth->along = target;
      }
    }
    depth->have = true;
    depth->source = kDepthHit;
  } else if (depth->have) {
    if (depth->source != kDepthDefault) depth->source = kDepthKept;
  } else {
    depth->along = fallback;
    if (depth->along < least) depth->along = least;
    if (depth->along > most) depth->along = most;
    depth->have = true;
    depth->source = kDepthDefault;
  }
  return depth->along;
}

// THE KINDS OF A CONTROL. A room holds its controls as SubOptionEntry
// objects. The second function of the vtable is UpdateMouse, and its address
// names the class (runblack.exe v1.42).
enum ControlKind {
  kControlNone = 0,
  kControlButton,
  kControlCheckOff,
  kControlCheckOn,
  kControlSlider,
  kControlPicture,
  kControlScroll,
  kControlText,
  kControlList,
  kControlFrame,
  kControlOther
};

inline int ControlKindOf(unsigned update_mouse) {
  switch (update_mouse) {
    case 0x0078DC40: return kControlButton;
    case 0x0078DDD0: return kControlCheckOff;
    case 0x0078DF60: return kControlCheckOn;
    case 0x0078E060: return kControlSlider;
    case 0x0078E190: return kControlPicture;
    case 0x0078E320: return kControlScroll;
    case 0x0078E4E0: return kControlText;
    case 0x0078E6A0: return kControlList;
    case 0x0078E850: return kControlFrame;
    default: return update_mouse != 0 ? kControlOther : kControlNone;
  }
}

inline const char* ControlKindName(int kind) {
  switch (kind) {
    case kControlButton: return "a button";
    case kControlCheckOff: return "a check box that is off";
    case kControlCheckOn: return "a check box that is on";
    case kControlSlider: return "a slider";
    case kControlPicture: return "a picture";
    case kControlScroll: return "a scroll";
    case kControlText: return "a text line";
    case kControlList: return "a list";
    case kControlFrame: return "a frame";
    case kControlOther: return "a control of a kind with no name";
    default: return "no control";
  }
}

// THE BUTTONS IN A ROOM (temple_click). The game takes a press of each of
// its two buttons on a control. The Action button opens a door, and the Move
// button on empty space turns the camera of the room. So with
// temple_click=1 the trigger and the grip are both the Action button, and
// the game gets no Move button. 0 gives the plain buttons.
//
// button is 0 for the Move button of the game and 1 for its Action button.
inline bool ButtonAsked(int click, int button, bool trigger, bool grip,
                        bool swap) {
  if (click == 0) {
    if (button == 0) return swap ? grip : trigger;
    return swap ? trigger : grip;
  }
  return button == 1 && (trigger || grip);
}

// THE FLIGHT OF THE CAMERA OF A ROOM (temple_camera_flight). When the player
// comes into a room, the camera of the room has the state 0: it goes along a
// path of the room to its rest pose (InnerCamera::UpdateMain, runblack.exe
// v1.42). Two clocks count the seconds of the flight:
//  - The flight clock gives the place on the path. In the last second of
//    the path the camera goes from the path to the rest pose of the room.
//    When the clock is more than the length of the path and 0.5 s, the game
//    asks for the state 1, the state of the room.
//  - The blend clock mixes the pose of the entry (the camera of the land, or
//    of the room before) into the path for its first 2 s.
// The path of the hall is 5 s long, and it goes one time round the hall.
//
// In VR the head is on that camera, so the flight moves the room about the
// player. With temple_camera_flight=0 the mod sets the two clocks to the end
// of the flight. The game then ends the flight by its own test, and the mod
// puts the camera at the pose that the game asked for.
//
// THE REST POSE. In the state of the room a camera goes on to its rest
// pose. The camera of a room with pictures (the Challenge Room, the Save
// Game Room) has a number for its distance from the middle of the room, and
// that number goes to its target by the part 1 - exp(-seconds) in each
// update (ChallengeRoomCamera::UpdateMain, 0x007859D0). In a flight that
// number has the 5.5 s of the flight for its way. Temple::GoToRoom updates a
// camera two times with 1 s for this. The mod does the same 10 times, so
// that nothing is left for the frames after the cut.
const int kCameraFlight = 0;  // the state of the flight
const int kCameraRoom = 1;    // the state of the room
const float kFlightHoldSeconds = 0.5f;
const float kFlightBlendSeconds = 2.0f;
// How far past the end the mod sets the flight clock.
const float kFlightPastSeconds = 0.05f;
// The updates that bring a camera to its rest pose, and the seconds of each.
const int kFlightSettleUpdates = 10;
const float kFlightSettleSeconds = 1.0f;

// The part of the way to the rest pose that is left after some updates of
// the settle: 1 with no update, 0.135 after the two updates of the game.
inline float FlightSettleLeft(int updates) {
  return expf(-kFlightSettleSeconds * static_cast<float>(updates));
}

// The value of the flight clock after which the game ends a flight.
inline float FlightEndSeconds(unsigned path_ms) {
  return path_ms * 0.001f + kFlightHoldSeconds;
}

// The game ends the flight in an update with this flight clock.
inline bool FlightIsOver(float clock, unsigned path_ms) {
  return clock > FlightEndSeconds(path_ms);
}

// How much of the rest pose the game mixes into the pose of the path, 0 to
// 1: nothing before the last second of the path, all of it at its end.
inline float FlightRestWeight(float clock, unsigned path_ms) {
  const float w = clock - (path_ms * 0.001f - 1.0f);
  return w < 0.0f ? 0.0f : w > 1.0f ? 1.0f : w;
}

// How much of the pose of the entry the game mixes in, 0 to 1: all of it at
// the start, nothing from 2 s of the blend clock.
inline float FlightEntryWeight(float blend_clock) {
  const float w = (kFlightBlendSeconds - blend_clock) * 0.5f;
  return w < 0.0f ? 0.0f : w > 1.0f ? 1.0f : w;
}

// The mod ends the flight of a camera: the rule is 0, the camera has the
// state of the flight, and its path is known.
inline bool FlightCut(int rule, int state, bool have_path) {
  return rule == 0 && state == kCameraFlight && have_path;
}

// The mod brings a camera to its rest pose after the game changed the room
// with Temple::GoToRoom (the ring, Esc in a room). The game ends the flight
// itself there, and its two updates of 1 s leave a part of the way. The rule
// is 0, the update is the first one of this camera after a different
// camera, the mod did not end a flight in it, and the camera has the state
// of the room.
inline bool SettleAfterRoomChange(int rule, bool new_camera, bool cut,
                                  int state) {
  return rule == 0 && new_camera && !cut && state == kCameraRoom;
}

// The two clocks that end a flight. The blend clock keeps a value that is
// past its end.
struct FlightClocks {
  float flight;
  float blend;
};

inline FlightClocks FlightClocksAtEnd(unsigned path_ms, float blend_now) {
  FlightClocks clocks;
  clocks.flight = FlightEndSeconds(path_ms) + kFlightPastSeconds;
  clocks.blend =
      blend_now > kFlightBlendSeconds ? blend_now : kFlightBlendSeconds;
  return clocks;
}

// THE LINE OF SIGHT OF A CAMERA, in degrees. The heading is 0 along +z and
// 90 along +x. The pitch is positive up. A camera that looks at its own
// place gives 0 and 0.
inline void HeadingPitch(const float position[3], const float focus[3],
                         float* heading, float* pitch) {
  const float d[3] = {focus[0] - position[0], focus[1] - position[1],
                      focus[2] - position[2]};
  const float flat = sqrtf(d[0] * d[0] + d[2] * d[2]);
  *heading = flat > 1e-6f ? atan2f(d[0], d[2]) * 57.29578f : 0.0f;
  *pitch = flat > 1e-6f || fabsf(d[1]) > 1e-6f
               ? atan2f(d[1], flat) * 57.29578f
               : 0.0f;
}

inline float WrapDegrees(float d) {
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}

// The angle between two lines of sight, in degrees.
inline float SightAngle(float heading_a, float pitch_a, float heading_b,
                        float pitch_b) {
  const float k = 0.0174533f;
  const float a[3] = {sinf(heading_a * k) * cosf(pitch_a * k), sinf(pitch_a * k),
                      cosf(heading_a * k) * cosf(pitch_a * k)};
  const float b[3] = {sinf(heading_b * k) * cosf(pitch_b * k), sinf(pitch_b * k),
                      cosf(heading_b * k) * cosf(pitch_b * k)};
  float c = Dot(a, b);
  if (c > 1.0f) c = 1.0f;
  if (c < -1.0f) c = -1.0f;
  return acosf(c) * 57.29578f;
}

// THE MOVE OF A CAMERA, for the log. The first three numbers compare the
// end with the start. A camera that goes round a room ends near its start,
// so the last two numbers are sums along the path.
struct CameraMove {
  float heading, pitch;  // the line of sight at the start
  float position[3];     // the place at the start
  float last_heading, last_pitch;
  float last_position[3];
  float turned;          // the sum of the turns, in degrees
  float path;            // the sum of the steps of the place
};

inline void MoveStart(CameraMove* move, float heading, float pitch,
                      const float position[3]) {
  move->heading = move->last_heading = heading;
  move->pitch = move->last_pitch = pitch;
  for (int i = 0; i < 3; ++i) {
    move->position[i] = move->last_position[i] = position[i];
  }
  move->turned = 0.0f;
  move->path = 0.0f;
}

inline void MoveStep(CameraMove* move, float heading, float pitch,
                     const float position[3]) {
  move->turned +=
      SightAngle(move->last_heading, move->last_pitch, heading, pitch);
  move->path += Distance(move->last_position, position);
  move->last_heading = heading;
  move->last_pitch = pitch;
  for (int i = 0; i < 3; ++i) move->last_position[i] = position[i];
}

// The end less the start: the turn to the side, the turn up, and the
// distance.
inline void MoveNet(const CameraMove& move, float* side, float* up,
                    float* went) {
  *side = WrapDegrees(move.last_heading - move.heading);
  *up = move.last_pitch - move.pitch;
  *went = Distance(move.last_position, move.position);
}

}  // namespace temple_math
