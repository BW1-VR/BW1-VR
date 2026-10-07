// The math of the pointer in a room of the Temple. See temple.cpp and
// docs/controllers.md, "The Temple with the controllers".
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

}  // namespace temple_math
