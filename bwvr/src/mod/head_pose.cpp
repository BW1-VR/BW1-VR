// Put the player's head on the game's camera.
//
// The runtime gives a pose per eye in its own reference space. The game wants
// a rigid transform in camera space. Three things stand between them.
//
// HANDEDNESS. OpenXR is right handed: +X right, +Y up, -Z forward. The game's
// camera space is left handed: +X right, +Y up, +Z forward, which is why the
// projection divides by camera z. The change of basis is B = diag(1, 1, -1),
// and a rotation carries across as B * M * B, which negates exactly the four
// elements with one index equal to 2. Doing it this way rather than by
// guessing signs on the quaternion is the difference between one attempt and
// eight.
//
// WHERE ZERO IS. The reference space origin is wherever the player happened to
// be when the session started, so the head pose is measured from a meaningless
// place. Everything here is relative to a captured origin pose, and recentring
// just captures it again.
//
// SCALE. The head moves in metres and the island is in game units. The scale
// is taken from the stereo separation already in use: the runtime reports the
// player's eye separation in metres, the mod renders with eye_separation world
// units, and the ratio of the two is how many units a metre is worth. That
// keeps a head movement consistent with the depth the eyes already report, so
// the world cannot feel one size to the eyes and another to the neck.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>

extern void ModLog(const char* format, ...);
// spirit_anchor.cpp: tell the advisors that the view changed at once.
void SpiritAnchorCameraCut();

namespace {

struct Quat {
  float x, y, z, w;
};

Quat Conjugate(const Quat& q) {
  Quat out = {-q.x, -q.y, -q.z, q.w};
  return out;
}

// a then b, in the usual order: the result applies a first.
Quat Multiply(const Quat& b, const Quat& a) {
  Quat out;
  out.w = b.w * a.w - b.x * a.x - b.y * a.y - b.z * a.z;
  out.x = b.w * a.x + b.x * a.w + b.y * a.z - b.z * a.y;
  out.y = b.w * a.y - b.x * a.z + b.y * a.w + b.z * a.x;
  out.z = b.w * a.z + b.x * a.y - b.y * a.x + b.z * a.w;
  return out;
}

void Rotate(const Quat& q, const float v[3], float out[3]) {
  // v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
  const float tx = 2.0f * (q.y * v[2] - q.z * v[1]);
  const float ty = 2.0f * (q.z * v[0] - q.x * v[2]);
  const float tz = 2.0f * (q.x * v[1] - q.y * v[0]);
  out[0] = v[0] + q.w * tx + (q.y * tz - q.z * ty);
  out[1] = v[1] + q.w * ty + (q.z * tx - q.x * tz);
  out[2] = v[2] + q.w * tz + (q.x * ty - q.y * tx);
}

// Row major 3x3, in the row vector convention the camera matrix uses.
void ToMatrix(const Quat& q, float m[9]) {
  const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  m[0] = 1.0f - 2.0f * (yy + zz);
  m[1] = 2.0f * (xy - wz);
  m[2] = 2.0f * (xz + wy);
  m[3] = 2.0f * (xy + wz);
  m[4] = 1.0f - 2.0f * (xx + zz);
  m[5] = 2.0f * (yz - wx);
  m[6] = 2.0f * (xz - wy);
  m[7] = 2.0f * (yz + wx);
  m[8] = 1.0f - 2.0f * (xx + yy);
}

// B * M * B with B = diag(1, 1, -1): negate every element with exactly one
// index equal to 2.
void FlipHandedness(float m[9]) {
  m[2] = -m[2];
  m[5] = -m[5];
  m[6] = -m[6];
  m[7] = -m[7];
}

Quat g_origin_rotation = {0.0f, 0.0f, 0.0f, 1.0f};
float g_origin_position[3] = {0.0f, 0.0f, 0.0f};
bool g_have_origin = false;

// The middle of the head, in the reference space.
float g_head_center[3] = {0.0f, 0.0f, 0.0f};
bool g_have_head_center = false;

bool g_enabled = true;
bool g_rotation_only = false;
float g_units_per_meter = 7.5f;

// A position in the room: relative to the captured origin, turned by the
// inverse of the origin yaw, scaled into game units, and flipped to the left
// handed frame. The head and the hands both use this, so they share one room.
// Needs g_have_origin.
void RoomTranslation(const float position[3], float out[3]) {
  const float offset[3] = {position[0] - g_origin_position[0],
                           position[1] - g_origin_position[1],
                           position[2] - g_origin_position[2]};
  float local[3];
  Rotate(Conjugate(g_origin_rotation), offset, local);
  out[0] = local[0] * g_units_per_meter;
  out[1] = local[1] * g_units_per_meter;
  // Right handed to left handed.
  out[2] = -local[2] * g_units_per_meter;
}

// The head center of this frame in the room, before rotation only zeroes the
// lean. Set by HeadPoseUpdate.
float g_head_in_room[3] = {0.0f, 0.0f, 0.0f};
bool g_have_head_in_room = false;

// Goes up by one each time the origin changes, so a grab of the world can
// start a new anchor when the room moves under it.
uint64_t g_origin_serial = 0;

}  // namespace

uint64_t HeadPoseOriginSerial() { return g_origin_serial; }

// The recenter pose in the runtime's LOCAL space: the head center and the yaw
// at the last recenter, as a quaternion x y z w. The interface panels hang
// from it (docs/ui-layer.md, "Recenter"). False until the head has an origin.
bool HeadPoseOrigin(float position_out[3], float rotation_out[4]) {
  if (!g_have_origin) return false;
  for (int i = 0; i < 3; ++i) position_out[i] = g_origin_position[i];
  rotation_out[0] = g_origin_rotation.x;
  rotation_out[1] = g_origin_rotation.y;
  rotation_out[2] = g_origin_rotation.z;
  rotation_out[3] = g_origin_rotation.w;
  return true;
}

// Told once a frame, before either eye asks, so a recentre puts its origin in
// the middle of the head rather than on whichever eye called first.
void HeadPoseSetHead(const float center[3]) {
  g_head_center[0] = center[0];
  g_head_center[1] = center[1];
  g_head_center[2] = center[2];
  g_have_head_center = true;
}

void HeadPoseSetEnabled(bool on) {
  g_enabled = on;
  ModLog("head         : tracking %s", on ? "on" : "off");
}

bool HeadPoseEnabled() { return g_enabled; }

void HeadPoseSetRotationOnly(bool on) {
  g_rotation_only = on;
  ModLog("head         : %s", on ? "rotation only, the head cannot lean"
                                 : "full six degrees of freedom");
}

bool HeadPoseRotationOnly() { return g_rotation_only; }

// How many game units a metre is worth. Taken from the separation already in
// use, so the neck and the eyes agree about how big the world is.
void HeadPoseSetScale(float units_per_meter) {
  if (units_per_meter > 0.0f) g_units_per_meter = units_per_meter;
}

float HeadPoseScale() { return g_units_per_meter; }

// Forget where the player was, and call wherever they are now straight ahead.
void HeadPoseRecenter() {
  g_have_origin = false;
  g_have_head_in_room = false;
  // A recenter turns the room, so it is a cut. The advisors land again around
  // the new straight ahead, not at their old place in the old room.
  SpiritAnchorCameraCut();
  ModLog("head         : recentered on the next frame");
}

// A TURN OF THE ROOM ABOUT THE HEAD (temple.cpp: the stick in a room of the
// Temple). The origin turns about the up axis through the middle of the
// head. So the head keeps its place in the room, and the view turns by the
// angle, to the right for a positive angle. It is one step, as a snap turn
// is. False before the head has an origin.
bool HeadPoseTurn(float degrees) {
  if (!g_have_origin || !g_have_head_center) return false;
  // The head in the room now, before the turn.
  const float offset[3] = {g_head_center[0] - g_origin_position[0],
                           g_head_center[1] - g_origin_position[1],
                           g_head_center[2] - g_origin_position[2]};
  float in_room[3];
  Rotate(Conjugate(g_origin_rotation), offset, in_room);
  // LOCAL has y up and is right handed. A view that turns to the right is an
  // origin that turns to the left: a positive angle about y.
  const float half = 0.5f * degrees / 57.29578f;
  const Quat turn = {0.0f, sinf(half), 0.0f, cosf(half)};
  g_origin_rotation = Multiply(g_origin_rotation, turn);
  float back[3];
  Rotate(g_origin_rotation, in_room, back);
  for (int i = 0; i < 3; ++i) {
    g_origin_position[i] = g_head_center[i] - back[i];
  }
  ++g_origin_serial;
  // The room turns, so it is a cut for the advisors, as a recenter is.
  SpiritAnchorCameraCut();
  return true;
}

// A HEAD WITHOUT A HEADSET.
//
// Most of what goes wrong with head tracking can be seen in a still picture:
// the sea in the wrong place, the interface riding the view, geometry culled
// against the wrong frustum. None of that needs a person wearing a headset,
// and asking one to put it on for every experiment is slow and unkind.
//
// So the mod can invent a head. Given a yaw and a pitch it builds the pose the
// runtime would have reported, and the rest of the path is identical: the same
// quaternion, the same change of basis, the same camera transform. What comes
// out can be captured to disk and looked at.
float g_fake_yaw = 0.0f;
float g_fake_pitch = 0.0f;
float g_fake_roll = 0.0f;
bool g_fake_on = false;
float g_fake_separation = 0.0668f;  // metres, a human eye separation

void HeadPoseSetFake(bool on, float yaw_degrees, float pitch_degrees,
                     float roll_degrees) {
  g_fake_on = on;
  g_fake_yaw = yaw_degrees / 57.29578f;
  g_fake_pitch = pitch_degrees / 57.29578f;
  g_fake_roll = roll_degrees / 57.29578f;
  if (on) {
    // An invented head is absolute, not recentered. Recentering takes the
    // first pose it sees as straight ahead, so a fixed fake yaw would cancel
    // itself out exactly and every picture would look like yaw zero.
    g_origin_rotation.x = 0.0f;
    g_origin_rotation.y = 0.0f;
    g_origin_rotation.z = 0.0f;
    g_origin_rotation.w = 1.0f;
    g_origin_position[0] = 0.0f;
    g_origin_position[1] = 0.0f;
    g_origin_position[2] = 0.0f;
    g_have_origin = true;
    ++g_origin_serial;
    ModLog("head         : invented head, yaw %.1f pitch %.1f roll %.1f "
           "degrees, measured from straight ahead rather than recentered",
           yaw_degrees, pitch_degrees, roll_degrees);
  }
}

bool HeadPoseFakeOn() { return g_fake_on; }

// ALTERNATE THE INVENTED HEAD, so one run captures both states.
//
// Comparing a fixed head against a turned one otherwise needs two runs, and
// each run has to be driven through the menus by hand. Alternating every
// report window makes successive captures differ only by the head.
bool g_sweep = false;
bool g_sweep_turned = false;
float g_sweep_yaw = 0.0f;
float g_sweep_pitch = 0.0f;
float g_sweep_roll = 0.0f;

bool HeadPoseFakeSweeping() { return g_sweep; }

void HeadPoseFakeSweep(bool on) {
  g_sweep = on;
  if (on) {
    g_sweep_yaw = g_fake_yaw;
    g_sweep_pitch = g_fake_pitch;
    g_sweep_roll = g_fake_roll;
  }
}

// Called once a report window.
void HeadPoseFakeStep() {
  if (!g_sweep) return;
  // The whole pose alternates with a level head looking straight ahead, so a
  // pitch can be tested the same way as a turn.
  g_sweep_turned = !g_sweep_turned;
  g_fake_yaw = g_sweep_turned ? g_sweep_yaw : 0.0f;
  g_fake_pitch = g_sweep_turned ? g_sweep_pitch : 0.0f;
  g_fake_roll = g_sweep_turned ? g_sweep_roll : 0.0f;
  ModLog("head         : invented head now at yaw %.1f pitch %.1f roll %.1f "
         "degrees", g_fake_yaw * 57.29578f, g_fake_pitch * 57.29578f,
         g_fake_roll * 57.29578f);
}

// A STILL LEAN AND A LOOK FOR THE INVENTED HEAD, for the grab tests.
//
// The lean moves the head center by fake_head_right_cm, fake_head_up_cm and
// fake_head_forward_cm. The look turns the head 30 degrees left and 20
// degrees down, on top of its yaw and pitch. Both are steps, never a motion:
// the fake controller's script words "lean" and "look" hold them. With no
// "lean" word in the script, a lean that is set holds for the whole run.
float g_fake_lean[3] = {0.0f, 0.0f, 0.0f};  // meters, right, up, forward
bool g_fake_lean_on = true;
bool g_fake_look_on = false;
const float kLookYaw = 30.0f / 57.29578f;
const float kLookPitch = -20.0f / 57.29578f;

void HeadPoseSetFakeLean(int right_cm, int up_cm, int forward_cm) {
  g_fake_lean[0] = right_cm * 0.01f;
  g_fake_lean[1] = up_cm * 0.01f;
  g_fake_lean[2] = forward_cm * 0.01f;
  if (right_cm != 0 || up_cm != 0 || forward_cm != 0) {
    ModLog("head         : the invented head leans %d cm right, %d cm up and "
           "%d cm forward", right_cm, up_cm, forward_cm);
  }
}

void HeadPoseFakeLeanStep(bool on) { g_fake_lean_on = on; }
void HeadPoseFakeLookStep(bool on) { g_fake_look_on = on; }

// The center of the invented head in the runtime's space, where forward is
// -Z.
void HeadPoseFakeCenter(float out[3]) {
  const float k = g_fake_lean_on ? 1.0f : 0.0f;
  out[0] = g_fake_lean[0] * k;
  out[1] = g_fake_lean[1] * k;
  out[2] = -g_fake_lean[2] * k;
}

// The pose the runtime would have reported for one eye.
void HeadPoseFakeEye(int eye, float position[3], float orientation[4]) {
  // Yaw about up, then pitch about right, then roll about forward, which is
  // the order a neck works in. Forward is -Z in the runtime's right handed
  // frame, so a roll is a rotation about -Z.
  const float turn = g_fake_yaw + (g_fake_look_on ? kLookYaw : 0.0f);
  const float tilt = g_fake_pitch + (g_fake_look_on ? kLookPitch : 0.0f);
  const float cy = cosf(turn * 0.5f), sy = sinf(turn * 0.5f);
  const float cp = cosf(tilt * 0.5f), sp = sinf(tilt * 0.5f);
  const float cr = cosf(g_fake_roll * 0.5f), sr = sinf(g_fake_roll * 0.5f);
  Quat yaw = {0.0f, sy, 0.0f, cy};
  Quat pitch = {sp, 0.0f, 0.0f, cp};
  Quat roll = {0.0f, 0.0f, -sr, cr};
  const Quat head = Multiply(Multiply(yaw, pitch), roll);
  orientation[0] = head.x;
  orientation[1] = head.y;
  orientation[2] = head.z;
  orientation[3] = head.w;

  // The eyes sit either side of the head, carried round by its rotation.
  const float half = (eye == 0 ? -0.5f : 0.5f) * g_fake_separation;
  const float local[3] = {half, 0.0f, 0.0f};
  Rotate(head, local, position);
  float center[3];
  HeadPoseFakeCenter(center);
  for (int i = 0; i < 3; ++i) position[i] += center[i];
}

// Turn one eye's pose into a rigid transform in the game's camera space.
// Returns false when there is nothing to apply, and then the caller should
// fall back to the fixed eye offset.
bool HeadPoseForEye(const float position[3], const float orientation[4],
                    float matrix_out[9], float translation_out[3]);

// The last head pose worked out, without the eye offset in it. The camera
// hook runs once a frame, before either eye, so it asks for this rather than
// for an eye.
float g_last_matrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
float g_last_translation[3] = {0, 0, 0};
bool g_have_last = false;

bool HeadPoseCurrent(float matrix_out[9], float translation_out[3]) {
  if (!g_enabled || !g_have_last) return false;
  for (int i = 0; i < 9; ++i) matrix_out[i] = g_last_matrix[i];
  for (int i = 0; i < 3; ++i) translation_out[i] = g_last_translation[i];
  return true;
}

// Work out the head pose for the frame, with no eye offset. Called once a
// frame, before the eyes.
bool HeadPoseUpdate(const float position[3], const float orientation[4]) {
  float h[9];
  float d[3];
  if (!HeadPoseForEye(position, orientation, h, d)) return false;
  for (int i = 0; i < 9; ++i) g_last_matrix[i] = h[i];
  for (int i = 0; i < 3; ++i) g_last_translation[i] = d[i];
  g_have_last = true;
  RoomTranslation(position, g_head_in_room);
  g_have_head_in_room = true;
  return true;
}

// A pose in the room, with the same origin and scale as the head, and no
// origin capture. Only the head captures the origin, so a controller can never
// become straight ahead. False until the head has captured one. The
// translation ignores rotation only mode, because a hand still has a place.
bool HeadPoseToRoom(const float position[3], const float orientation[4],
                    float rotation_out[9], float translation_out[3]) {
  if (!g_enabled || !g_have_origin) return false;
  const Quat q = {orientation[0], orientation[1], orientation[2],
                  orientation[3]};
  const Quat relative = Multiply(Conjugate(g_origin_rotation), q);
  ToMatrix(relative, rotation_out);
  FlipHandedness(rotation_out);
  RoomTranslation(position, translation_out);
  return true;
}

// The head center of this frame in the same frame, before rotation only.
// With rotation only on, HeadPoseCurrent gives no lean, but a hand is still
// placed against the head where it really is.
bool HeadPoseHeadInRoom(float translation_out[3]) {
  if (!g_enabled || !g_have_head_in_room) return false;
  for (int i = 0; i < 3; ++i) translation_out[i] = g_head_in_room[i];
  return true;
}

bool HeadPoseForEye(const float position[3], const float orientation[4],
                    float matrix_out[9], float translation_out[3]) {
  if (!g_enabled) return false;

  Quat head = {orientation[0], orientation[1], orientation[2], orientation[3]};

  if (!g_have_origin) {
    // The origin keeps only the yaw of the first pose, not its pitch or roll.
    // Recentring on a tilted head would tilt the horizon and leave it tilted,
    // and a tilted horizon in VR is unpleasant in a way a wrong heading is
    // not.
    const float yaw = atan2f(2.0f * (head.w * head.y + head.x * head.z),
                             1.0f - 2.0f * (head.y * head.y + head.x * head.x));
    g_origin_rotation.x = 0.0f;
    g_origin_rotation.y = sinf(yaw * 0.5f);
    g_origin_rotation.z = 0.0f;
    g_origin_rotation.w = cosf(yaw * 0.5f);
    const float* from = g_have_head_center ? g_head_center : position;
    g_origin_position[0] = from[0];
    g_origin_position[1] = from[1];
    g_origin_position[2] = from[2];
    g_have_origin = true;
    ++g_origin_serial;
  }

  const Quat inverse_origin = Conjugate(g_origin_rotation);
  const Quat relative = Multiply(inverse_origin, head);

  ToMatrix(relative, matrix_out);
  FlipHandedness(matrix_out);

  if (g_rotation_only) {
    translation_out[0] = 0.0f;
    translation_out[1] = 0.0f;
    translation_out[2] = 0.0f;
    return true;
  }

  RoomTranslation(position, translation_out);
  return true;
}

// DECLARE A POSE THE PICTURE COULD HAVE COME FROM.
//
// The projection layer tells the runtime "this image is what a head at this
// pose sees". The runtime then warps it from that pose to the pose at display
// time. The warp is only right if the claim is right.
//
// When head roll is switched off the picture is drawn level, whatever the
// player's head is doing, but the runtime's own pose still carries the roll.
// The layer then claims a rolled camera for a level picture, so the runtime
// shows the level picture inside a rolled frustum and the world tilts WITH the
// player's head instead of staying put. That is a real tilt of the whole
// world, and it is exactly the kind of mismatch that makes people ill.
//
// So strip the roll from the declared orientation whenever the render did not
// carry it. Gravity is +Y and the camera looks down -Z, both in the runtime's
// own right handed convention, so this rebuilds the basis from the heading and
// world up and throws the roll away.
void HeadPoseStripRoll(const float in[4], float out[4]) {
  const Quat q = {in[0], in[1], in[2], in[3]};
  const float back_local[3] = {0.0f, 0.0f, 1.0f};
  float axis_z[3];
  Rotate(q, back_local, axis_z);

  // right = world up crossed with the camera's backward axis.
  float right[3] = {axis_z[2], 0.0f, -axis_z[0]};
  const float length = sqrtf(right[0] * right[0] + right[2] * right[2]);
  // Looking straight up or straight down leaves no heading, and no roll to
  // remove that means anything. Hand the pose back untouched.
  if (length < 0.02f) {
    for (int i = 0; i < 4; ++i) out[i] = in[i];
    return;
  }
  right[0] /= length;
  right[2] /= length;
  const float up[3] = {axis_z[1] * right[2] - axis_z[2] * right[1],
                       axis_z[2] * right[0] - axis_z[0] * right[2],
                       axis_z[0] * right[1] - axis_z[1] * right[0]};

  // Columns are right, up and backward, which is the matrix ToMatrix builds.
  float m[9];
  for (int i = 0; i < 3; ++i) {
    m[i * 3 + 0] = right[i];
    m[i * 3 + 1] = up[i];
    m[i * 3 + 2] = axis_z[i];
  }

  // Matrix back to a quaternion, taking the largest diagonal term so the
  // division is never by a small number.
  const float trace = m[0] + m[4] + m[8];
  float x, y, z, w;
  if (trace > 0.0f) {
    const float s = sqrtf(trace + 1.0f) * 2.0f;
    w = 0.25f * s;
    x = (m[7] - m[5]) / s;
    y = (m[2] - m[6]) / s;
    z = (m[3] - m[1]) / s;
  } else if (m[0] > m[4] && m[0] > m[8]) {
    const float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2.0f;
    w = (m[7] - m[5]) / s;
    x = 0.25f * s;
    y = (m[1] + m[3]) / s;
    z = (m[2] + m[6]) / s;
  } else if (m[4] > m[8]) {
    const float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2.0f;
    w = (m[2] - m[6]) / s;
    x = (m[1] + m[3]) / s;
    y = 0.25f * s;
    z = (m[5] + m[7]) / s;
  } else {
    const float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2.0f;
    w = (m[3] - m[1]) / s;
    x = (m[2] + m[6]) / s;
    y = (m[5] + m[7]) / s;
    z = 0.25f * s;
  }
  const float norm = sqrtf(x * x + y * y + z * z + w * w);
  if (norm < 1e-6f) {
    for (int i = 0; i < 4; ++i) out[i] = in[i];
    return;
  }
  out[0] = x / norm;
  out[1] = y / norm;
  out[2] = z / norm;
  out[3] = w / norm;
}
