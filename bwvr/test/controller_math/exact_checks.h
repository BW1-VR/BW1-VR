// THE EXACT HAND (docs/hand.md, "The exact Hand"). main.cpp includes this file
// after Check and Near, just before main.
#pragma once

#include "../../src/mod/hand_exact_math.h"

namespace exact_checks {

// A grip rotation in the game's frame from a fake pose, as the fake
// controller and head_pose.cpp make it: the quaternion of FakeOrientation,
// then the flip of z on both sides (diag(1, 1, -1) R diag(1, 1, -1)).
inline void GripRotation(float yaw, float pitch, float roll, float out[9]) {
  float q[4];
  controller_math::FakeOrientation(yaw, pitch, roll, q);
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),
                      2 * (x * z + y * w),     2 * (x * y + z * w),
                      1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                      2 * (x * z - y * w),     2 * (y * z + x * w),
                      1 - 2 * (x * x + y * y)};
  const float flip[3] = {1.0f, 1.0f, -1.0f};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) out[i * 3 + j] = flip[i] * r[i * 3 + j] * flip[j];
  }
}

// The worst error of (rows / k) from an orthonormal set.
inline double OrthoError(const float m[12], float k) {
  double worst = 0.0;
  for (int a = 0; a < 3; ++a) {
    for (int b = 0; b < 3; ++b) {
      double dot = 0.0;
      for (int i = 0; i < 3; ++i) {
        dot += static_cast<double>(m[a * 3 + i]) * m[b * 3 + i];
      }
      dot /= static_cast<double>(k) * k;
      const double want = a == b ? 1.0 : 0.0;
      if (fabs(dot - want) > worst) worst = fabs(dot - want);
    }
  }
  return worst;
}

}  // namespace exact_checks

void ExactChecks() {
  namespace hx = hand_exact_math;
  using exact_checks::GripRotation;
  using exact_checks::OrthoError;
  printf("\nThe exact Hand: the root matrix\n");

  // The mesh constants agree with each other.
  {
    float d[3];
    for (int i = 0; i < 3; ++i) d[i] = hx::kMeshMiddleTip[i] - hx::kMeshWrist[i];
    Near("the mesh: the wrist to the middle tip is kMeshLength",
         hx::Length(d), hx::kMeshLength, 0.1);
    Near("the mesh: the rest yaw puts that line on +y (degrees)",
         atan2f(d[0], d[1]) * 57.29578f, hx::kRestYawDeg, 0.05);
  }

  float q[9];
  hx::RestTurn(hx::kRestYawDeg, hx::kRestPitchDeg, hx::kRestRollDeg, q);
  const float k = 0.0123f;
  const float tip[3] = {12.0f, -3.0f, 40.0f};
  const float poses[6][3] = {{0, 0, 0},      {0, -30, 0},  {45, -20, 30},
                             {-90, 60, -45}, {170, 10, 80}, {30, -85, 120}};
  double worst_ortho = 0.0, worst_axis = 0.0, worst_palm = 0.0;
  double worst_thumb_right = 0.0, worst_thumb_left = 0.0;
  double worst_length = 0.0, worst_tip = 0.0;
  bool det_right = true, det_left = true, det_game = true;
  for (int p = 0; p < 6; ++p) {
    float rotation[9];
    GripRotation(poses[p][0], poses[p][1], poses[p][2], rotation);
    for (int side = 0; side < 2; ++side) {
      const bool left = side == 0;
      float f[3], b[3];
      hx::HandAxes(rotation, left, f, b);
      // The game's Hand is a right hand (CHand+0x484C set) on the right
      // controller, and the stored left hand (0x484C clear) on the left one.
      const bool mirrored = !left;
      float m[12];
      if (!hx::ExactRoot(f, b, q, k, mirrored, tip, m)) {
        Check(false, "ExactRoot builds a root", 0, 1);
        continue;
      }
      const double ortho = OrthoError(m, k);
      if (ortho > worst_ortho) worst_ortho = ortho;
      const float det = hx::Det(m);
      if (mirrored && !(det < 0.0f)) det_right = false;
      if (!mirrored && !(det > 0.0f)) det_left = false;
      // The game's own root for the same mirror has the same sign.
      const float up[3] = {0.0f, 1.0f, 0.0f};
      float game[12];
      hx::GameRoot(0.7f * p, up, k, mirrored, tip, game);
      if ((hx::Det(game) < 0.0f) != (det < 0.0f)) det_game = false;
      // The mesh's wrist to middle tip goes along f, with the length of the
      // real hand, k * kMeshLength.
      float wrist[3], middle[3], axis[3];
      hx::ModelToWorld(m, hx::kMeshWrist, wrist);
      hx::ModelToWorld(m, hx::kMeshMiddleTip, middle);
      for (int i = 0; i < 3; ++i) axis[i] = middle[i] - wrist[i];
      const double axis_error = hx::AngleDeg(axis, f);
      if (axis_error > worst_axis) worst_axis = axis_error;
      const double length_error =
          fabs(hx::Length(axis) / (k * hx::kMeshLength) - 1.0);
      if (length_error > worst_length) worst_length = length_error;
      // The palm (+z of the mesh, turned by Q) faces away from b.
      const float zero[3] = {0.0f, 0.0f, 0.0f};
      float origin[3], palm_point[3], palm[3];
      // The palm normal of the mesh, Q^-1 * e_z (row 2 of Q, as Q is a turn).
      const float z_rest[3] = {q[6], q[7], q[8]};
      hx::ModelToWorld(m, zero, origin);
      hx::ModelToWorld(m, z_rest, palm_point);
      for (int i = 0; i < 3; ++i) palm[i] = palm_point[i] - origin[i];
      const float away[3] = {-b[0], -b[1], -b[2]};
      const double palm_error = hx::AngleDeg(palm, away);
      if (palm_error > worst_palm) worst_palm = palm_error;
      // The thumb (+x of the stored mesh) goes to the thumb side of the real
      // hand: +z of the grip for both hands (overlay_math.h).
      const float x_rest[3] = {q[0], q[1], q[2]};  // Q^-1 * e_x
      float thumb_point[3], thumb[3];
      hx::ModelToWorld(m, x_rest, thumb_point);
      for (int i = 0; i < 3; ++i) thumb[i] = thumb_point[i] - origin[i];
      const float side_axis[3] = {rotation[2], rotation[5], rotation[8]};
      const double thumb_error = hx::AngleDeg(thumb, side_axis);
      if (left) {
        if (thumb_error > worst_thumb_left) worst_thumb_left = thumb_error;
      } else if (thumb_error > worst_thumb_right) {
        worst_thumb_right = thumb_error;
      }
      // The origin, the tip of the mesh's index finger, is the point.
      const double tip_error = controller_math::Distance(origin, tip);
      if (tip_error > worst_tip) worst_tip = tip_error;
    }
  }
  Near("(A, B, C) / k is orthonormal, 6 poses, both hands", worst_ortho, 0.0,
       1e-5);
  Check(det_right, "the right Hand (0x484C set) has det below 0", det_right,
        1);
  Check(det_left, "the left Hand (0x484C clear) has det above 0", det_left, 1);
  Check(det_game, "det has the sign of the game's own root for each mirror",
        det_game, 1);
  Near("the mesh's wrist to middle tip is along f (degrees)", worst_axis, 0.0,
       0.1);
  Near("and as long as the real hand (share)", worst_length, 0.0, 1e-4);
  Near("the mesh's palm faces away from the back of the hand (degrees)",
       worst_palm, 0.0, 0.1);
  Near("the right Hand's thumb is on the right hand's thumb side (degrees)",
       worst_thumb_right, 0.0, 0.1);
  Near("the left Hand's thumb is on the left hand's thumb side (degrees)",
       worst_thumb_left, 0.0, 0.1);
  Near("the mesh's origin is the point (units)", worst_tip, 0.0, 1e-5);

  printf("The exact Hand: the rest turn\n");
  {
    float q0[9];
    hx::RestTurn(0.0f, 0.0f, 0.0f, q0);
    double worst = 0.0;
    for (int i = 0; i < 9; ++i) {
      const double want = (i % 4 == 0) ? 1.0 : 0.0;
      if (fabs(q0[i] - want) > worst) worst = fabs(q0[i] - want);
    }
    Near("no turn is the identity", worst, 0.0, 1e-7);
    float q1[9];
    hx::RestTurn(0.0f, 20.0f, 0.0f, q1);
    // +y goes toward +z (the palm) with a plus pitch.
    Near("a pitch of 20 turns the fingers toward the palm", q1[2 * 3 + 1],
         sinf(20.0f / 57.29578f), 1e-6);
    hx::RestTurn(0.0f, 0.0f, 30.0f, q1);
    // +z goes toward +x (the thumb) with a plus roll.
    Near("a roll of 30 turns the palm toward the thumb", q1[0 * 3 + 2],
         sinf(30.0f / 57.29578f), 1e-6);
    hx::RestTurn(25.0f, 0.0f, 0.0f, q1);
    Near("a yaw of 25 turns the fingers toward the little finger",
         q1[0 * 3 + 1], -sinf(25.0f / 57.29578f), 1e-6);
    // 8 more of pitch moves the mapped axis 8 degrees off f.
    float rotation[9], f[3], b[3], m[12];
    GripRotation(10.0f, -30.0f, 5.0f, rotation);
    hx::HandAxes(rotation, false, f, b);
    hx::RestTurn(hx::kRestYawDeg, hx::kRestPitchDeg + 8.0f, 0.0f, q1);
    hx::ExactRoot(f, b, q1, k, true, tip, m);
    float wrist[3], middle[3], axis[3];
    hx::ModelToWorld(m, hx::kMeshWrist, wrist);
    hx::ModelToWorld(m, hx::kMeshMiddleTip, middle);
    for (int i = 0; i < 3; ++i) axis[i] = middle[i] - wrist[i];
    Near("8 more of pitch puts the mesh 8 degrees off the finger axis",
         hx::AngleDeg(axis, f), 8.0, 0.1);
  }

  printf("The exact Hand: the point at the index tip\n");
  {
    float rotation[9];
    GripRotation(-20.0f, -40.0f, 15.0f, rotation);
    const float length = 0.19f * 100.0f;  // 19 cm at 100 units a meter
    for (int side = 0; side < 2; ++side) {
      const bool left = side == 0;
      float off[3];
      hx::TipOffset(rotation, left, length, off);
      float tip_local[3], want[3];
      overlay_math::FingerTip(1, overlay_math::kRestCurl, tip_local);
      const float grip[3] = {5.0f, 6.0f, 7.0f};
      overlay_math::PlacePoint(grip, rotation, length, left, tip_local, want);
      const float got[3] = {grip[0] + off[0], grip[1] + off[1],
                            grip[2] + off[2]};
      Near(left ? "the left tip is the overlay's left index tip (units)"
                : "the right tip is the overlay's right index tip (units)",
           controller_math::Distance(got, want), 0.0, 1e-4);
    }
    float off[3];
    hx::TipOffset(rotation, false, length, off);
    // The tip is 8 to 10 cm of the room from the grip point.
    Near("the index tip is 8 to 10 cm from the grip (cm)",
         hx::Length(off) / 100.0f * 100.0f, 9.0, 1.0);
    // The trigger does not move the point: the rest curl is fixed.
    float a[3], b2[3];
    overlay_math::FingerTip(1, overlay_math::kRestCurl, a);
    overlay_math::FingerTip(1, overlay_math::CurlOf(0.0f), b2);
    Near("the point is the index tip of an open trigger",
         controller_math::Distance(a, b2), 0.0, 1e-7);
  }

  printf("The exact Hand: no lag at a change of state\n");
  {
    // A bone in the model frame, the old root and the new root.
    float rotation[9], f[3], b[3];
    GripRotation(30.0f, -10.0f, 0.0f, rotation);
    hx::HandAxes(rotation, false, f, b);
    float old_root[12], new_root[12];
    const float old_tip[3] = {100.0f, 20.0f, 300.0f};
    const float new_tip[3] = {104.0f, 19.0f, 296.0f};
    hx::ExactRoot(f, b, q, k, true, old_tip, old_root);
    GripRotation(50.0f, 20.0f, 40.0f, rotation);
    hx::HandAxes(rotation, false, f, b);
    hx::ExactRoot(f, b, q, k, true, new_tip, new_root);
    // A bone: a turn and a point of the model frame, then composed with a
    // root as fn_00839F10 does (bone * root).
    const float bone[12] = {0.8f, 0.6f, 0.0f, -0.6f, 0.8f, 0.0f,
                            0.0f, 0.0f,  1.0f, -89.0f, -206.0f, -32.0f};
    auto compose = [](const float a[12], const float root[12], float out[12]) {
      for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
          out[r * 3 + c] = a[r * 3 + 0] * root[0 + c] +
                           a[r * 3 + 1] * root[3 + c] +
                           a[r * 3 + 2] * root[6 + c];
        }
      }
      for (int c = 0; c < 3; ++c) {
        out[9 + c] = a[9] * root[0 + c] + a[10] * root[3 + c] +
                     a[11] * root[6 + c] + root[9 + c];
      }
    };
    float at_old[12], at_new[12], moved[12];
    compose(bone, old_root, at_old);
    compose(bone, new_root, at_new);
    const bool ok = hx::Rebase(at_old, old_root, new_root, moved);
    double worst = 0.0;
    for (int i = 0; i < 12; ++i) {
      const double scale = i >= 9 ? 1.0 : k;
      const double e = fabs(moved[i] - at_new[i]) / scale;
      if (e > worst) worst = e;
    }
    Check(ok, "the rebase has an inverse", ok, 1);
    Near("an old bone rebased to the new root is the new bone", worst, 0.0,
         2e-4);
    // The blend of the rebased old pose and the new pose is the new pose at
    // every time of the blend, so the Hand does not glide.
    double drift = 0.0;
    for (int step = 0; step <= 4; ++step) {
      const float t = 0.25f * step;
      for (int i = 9; i < 12; ++i) {
        const double mixed = (at_new[i] - moved[i]) * t + moved[i];
        if (fabs(mixed - at_new[i]) > drift) drift = fabs(mixed - at_new[i]);
      }
    }
    Near("the blend then does not move the bone (units)", drift, 0.0, 2e-3);
    // Without the rebase the blend starts at the old place.
    Check(controller_math::Distance(&at_old[9], &at_new[9]) > 1.0f,
          "without the rebase the bone starts away from its new place",
          controller_math::Distance(&at_old[9], &at_new[9]), 1.0);
    // A world point goes into the model frame and back.
    float model[3], world[3];
    const float point[3] = {7.0f, 8.0f, 9.0f};
    hx::WorldToModel(new_root, point, model);
    hx::ModelToWorld(new_root, model, world);
    Near("a point to the model frame and back (units)",
         controller_math::Distance(point, world), 0.0, 1e-4);
  }
}
