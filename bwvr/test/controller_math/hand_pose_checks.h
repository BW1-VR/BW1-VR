// ONE PAIR OF HANDS (docs/hand.md, "The two real hands and which one is the
// Hand", and docs/hand-model.md): the poses of the mod's hands, the held
// object in the palm, the wrist lock and the tint.
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include "../../src/mod/hand_exact_math.h"

namespace hand_pose_checks {

namespace om = overlay_math;
namespace hx = hand_exact_math;

inline double Gap(const float a[3], const float b[3]) {
  double sum = 0.0;
  for (int i = 0; i < 3; ++i) {
    sum += (static_cast<double>(a[i]) - b[i]) * (static_cast<double>(a[i]) - b[i]);
  }
  return sqrt(sum);
}

// The curls of one pose alone.
inline void OnePose(int pose, const om::PoseRange& hold, float trigger,
                    float squeeze, float curls[om::kFingers]) {
  float weights[om::kPoseCount] = {0.0f};
  weights[pose] = 1.0f;
  om::PoseCurls(weights, hold, trigger, squeeze, curls);
}

// The worst error of a finger's segment lengths, and the largest distance of
// one of its joints from the middle of the palm.
inline void ChainFacts(const float curls[om::kFingers], double* worst_length,
                       double* farthest) {
  const float palm[3] = {om::kPalmX, om::kKnuckleY + 0.5f * om::kPalmLength,
                         0.0f};
  for (int f = 0; f < om::kFingers; ++f) {
    float joints[om::kJoints + 1][3];
    om::FingerJoints(f, curls[f], joints);
    for (int j = 0; j < om::kJoints; ++j) {
      const double error =
          fabs(Gap(joints[j], joints[j + 1]) - om::kShape[f].length[j]);
      if (error > *worst_length) *worst_length = error;
      const double from_palm = Gap(joints[j + 1], palm);
      if (from_palm > *farthest) *farthest = from_palm;
    }
  }
}

// The smallest distance from a joint, or the middle of a segment, of a finger
// to a point, less the finger's radius.
inline double Clearance(int finger, float curl, const float center[3]) {
  float joints[om::kJoints + 1][3];
  om::FingerJoints(finger, curl, joints);
  double least = 1e9;
  for (int j = 1; j <= om::kJoints; ++j) {
    float middle[3];
    for (int k = 0; k < 3; ++k) {
      middle[k] = 0.5f * (joints[j][k] + joints[j - 1][k]);
    }
    const double a = Gap(joints[j], center), b = Gap(middle, center);
    if (a < least) least = a;
    if (b < least) least = b;
  }
  return least - om::kShape[finger].radius;
}

inline void PoseChecks() {
  printf("\nOne pair of hands: the poses\n");
  const om::PoseRange cup = om::kPoseRanges[om::kPoseHold];

  // THE THUMB.
  float rest[3], closed[3];
  om::FingerTip(0, 0.0f, rest);
  om::FingerTip(0, 1.0f, closed);
  const float old_rest[3] = {-0.086f, -0.011f, 0.320f};
  Near("the thumb at a curl of 0 is the thumb of the first test hand",
       Gap(rest, old_rest), 0.0, 0.002);
  Check(Gap(rest, closed) > 0.25, "the thumb moves with its curl",
        Gap(rest, closed), 0.25);
  Check(closed[0] < om::kPalmX - om::kPalmHalfThick - 0.10f,
        "the closed thumb is in front of the palm, over the closed fingers",
        closed[0], om::kPalmX - om::kPalmHalfThick - 0.10f);
  Check(closed[2] < 0.10f && closed[2] > -0.10f,
        "the closed thumb is across the index and the middle finger",
        closed[2], 0.0);
  double worst_step = 0.0;
  for (int i = 0; i < 20; ++i) {
    float a[3], b[3];
    om::FingerTip(0, i / 20.0f, a);
    om::FingerTip(0, (i + 1) / 20.0f, b);
    if (Gap(a, b) > worst_step) worst_step = Gap(a, b);
  }
  Check(worst_step < 0.03, "a step of 1/20 of curl moves the thumb's tip "
        "less than 3% of the hand", worst_step, 0.03);

  // THE VALUES.
  float values[om::kFingers];
  om::FingerValues(0.3f, 0.8f, values);
  Near("the thumb takes the larger of the trigger and the grip", values[0],
       0.8, 1e-6);
  Near("the index finger takes the trigger", values[1], 0.3, 1e-6);
  Near("the little finger takes the grip", values[4], 0.8, 1e-6);

  // THE RELAXED POSE is the first test hand.
  float curls[om::kFingers];
  OnePose(om::kPoseRelaxed, cup, 0.4f, 0.7f, curls);
  Near("relaxed: the index curl is the curl of the trigger", curls[1],
       om::CurlOf(0.4f), 1e-6);
  Near("relaxed: the ring curl is the curl of the grip", curls[3],
       om::CurlOf(0.7f), 1e-6);
  OnePose(om::kPoseRelaxed, cup, 0.0f, 0.0f, curls);
  Near("relaxed: the thumb is at rest with open fingers", curls[0], 0.0, 1e-6);
  OnePose(om::kPoseRelaxed, cup, 0.0f, 1.0f, curls);
  Check(curls[0] > 0.8f, "relaxed: the thumb closes with the grip", curls[0],
        0.8);

  // THE POINT, THE FLAT HAND AND THE FIST, at each value of the fingers.
  double point_index = 0.0, point_other = 1.0, flat_most = 0.0, fist_least = 1.0;
  double worst_length = 0.0, farthest = 0.0;
  const float steps[3] = {0.0f, 0.5f, 1.0f};
  for (int pose = 0; pose < om::kPoseCount; ++pose) {
    for (int t = 0; t < 3; ++t) {
      for (int s = 0; s < 3; ++s) {
        OnePose(pose, cup, steps[t], steps[s], curls);
        ChainFacts(curls, &worst_length, &farthest);
        for (int f = 0; f < om::kFingers; ++f) {
          if (pose == om::kPosePoint && f == 1 && curls[f] > point_index) {
            point_index = curls[f];
          }
          if (pose == om::kPosePoint && f != 1 && curls[f] < point_other) {
            point_other = curls[f];
          }
          if (pose == om::kPoseFlat && curls[f] > flat_most) {
            flat_most = curls[f];
          }
          if (pose == om::kPoseFist && curls[f] < fist_least) {
            fist_least = curls[f];
          }
        }
      }
    }
  }
  Check(point_index <= 0.18 + 1e-6, "point: the index finger stays out at "
        "each trigger value", point_index, 0.18);
  Check(point_other >= 0.85 - 1e-6, "point: the thumb and the other fingers "
        "stay closed", point_other, 0.85);
  Check(flat_most <= 0.20 + 1e-6, "flat: each finger stays open", flat_most,
        0.20);
  Check(fist_least >= 0.82 - 1e-6, "fist: each finger stays closed",
        fist_least, 0.82);
  Near("each segment keeps its length in each pose", worst_length, 0.0, 1e-5);
  Check(farthest < 0.80, "each joint and tip stays on the hand, less than 80% "
        "of its length from the palm", farthest, 0.80);
  float point_tip[3], fist_tip[3];
  OnePose(om::kPosePoint, cup, 1.0f, 1.0f, curls);
  om::FingerTip(1, curls[1], point_tip);
  OnePose(om::kPoseFist, cup, 1.0f, 1.0f, curls);
  om::FingerTip(1, curls[1], fist_tip);
  Check(point_tip[1] < fist_tip[1] - 0.25, "point: the index tip is far out "
        "along the fingers, with the trigger pressed too", point_tip[1],
        fist_tip[1] - 0.25);

  // THE HOLD POSE closes on the ball, and a large ball opens the hand.
  const float radii[4] = {0.10f, 0.30f, 0.60f, 1.00f};
  double last[om::kFingers] = {1.0, 1.0, 1.0, 1.0, 1.0};
  bool wider = true;
  double least_clearance = 1e9, most_clearance = -1e9;
  int contacts = 0;
  for (int r = 0; r < 4; ++r) {
    om::PoseRange range;
    om::HoldRange(radii[r], &range);
    float center[3];
    om::HoldCenter(radii[r], center);
    for (int f = 0; f < om::kFingers; ++f) {
      if (range.hi[f] > last[f] + 1e-6) wider = false;
      last[f] = range.hi[f];
      const double clear = Clearance(f, range.hi[f], center) - radii[r];
      if (range.hi[f] > 0.0f && clear < least_clearance) least_clearance = clear;
      // One step more of curl is in the ball, unless the finger is a fist.
      if (range.hi[f] > 0.0f && range.hi[f] < 1.0f) {
        const double next =
            Clearance(f, range.hi[f] + 1.0f / 40.0f, center) - radii[r];
        if (next > most_clearance) most_clearance = next;
        ++contacts;
      }
    }
    if (r == 1) {
      OnePose(om::kPoseHold, range, 0.0f, 1.0f, curls);
      Near("hold: with the grip closed the middle finger is at the contact",
           curls[2], range.hi[2], 1e-6);
      Check(curls[1] >= om::kHoldLoose * range.hi[1] - 1e-6f &&
                curls[1] <= range.hi[1] + 1e-6f,
            "hold: with the trigger open the index finger stays near the "
            "contact", curls[1], range.hi[1]);
      OnePose(om::kPoseHold, range, 0.0f, 0.0f, curls);
      Check(curls[2] <= om::kRestCurl + 1e-6f, "hold: with the grip open the "
            "middle finger opens", curls[2], om::kRestCurl);
    }
  }
  Check(wider, "hold: each finger opens more for a larger ball", wider, 1);
  Check(least_clearance >= -1e-5, "hold: at the contact no finger is in the "
        "ball", least_clearance, 0.0);
  Check(contacts >= 8, "hold: most fingers stop at a contact with the ball",
        contacts, 8);
  Check(most_clearance < 0.0, "hold: one step more of curl is in the ball",
        most_clearance, 0.0);
  om::PoseRange small_range, least_range, large_range;
  om::HoldRange(0.02f, &small_range);
  om::HoldRange(om::kHoldMinRadius, &least_range);
  om::HoldRange(1.0f, &large_range);
  Near("hold: a small object counts as the least radius, so the hand stays a "
       "cup", small_range.hi[2], least_range.hi[2], 1e-6);
  Check(large_range.hi[2] < least_range.hi[2] - 0.2, "hold: the middle "
        "finger is much more open on a ball of one hand length",
        large_range.hi[2], least_range.hi[2] - 0.2);

  // THE BLEND of a change of pose: smooth, done in 0.1 s, and a sum of 1.
  float weights[om::kPoseCount] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  float before[om::kFingers];
  om::PoseCurls(weights, cup, 0.0f, 0.0f, before);
  const float dt = 1.0f / 90.0f;
  double worst_jump = 0.0, worst_sum = 0.0;
  int frames_to_fist = 0;
  for (int i = 0; i < 20; ++i) {
    // The target changes in the middle of the blend.
    const int target = i < 4 ? om::kPosePoint : om::kPoseFist;
    om::StepWeights(weights, target, dt, om::kPoseBlendSeconds);
    float sum = 0.0f;
    for (int p = 0; p < om::kPoseCount; ++p) sum += weights[p];
    if (fabs(sum - 1.0) > worst_sum) worst_sum = fabs(sum - 1.0);
    om::PoseCurls(weights, cup, 0.0f, 0.0f, curls);
    for (int f = 0; f < om::kFingers; ++f) {
      const double jump = fabs(static_cast<double>(curls[f]) - before[f]);
      if (jump > worst_jump) worst_jump = jump;
      before[f] = curls[f];
    }
    if (weights[om::kPoseFist] < 0.9999f) frames_to_fist = i + 1;
  }
  Near("the weights of the poses sum to 1 in each step", worst_sum, 0.0, 1e-5);
  Check(worst_jump <= dt / om::kPoseBlendSeconds + 1e-4, "a frame of 1/90 s "
        "moves no curl more than its share of the blend", worst_jump,
        dt / om::kPoseBlendSeconds);
  Check(frames_to_fist >= 4 + 8 && frames_to_fist <= 4 + 10, "the blend to "
        "the fist takes about 0.1 s (9 frames at 90 Hz)", frames_to_fist, 13);
  // The value of a finger has no lag: in the middle of a blend the trigger
  // moves the index finger at once.
  float mid[om::kPoseCount] = {0.5f, 0.0f, 0.0f, 0.0f, 0.5f};
  float open_trigger[om::kFingers], pressed_trigger[om::kFingers];
  om::PoseCurls(mid, cup, 0.0f, 0.0f, open_trigger);
  om::PoseCurls(mid, cup, 1.0f, 0.0f, pressed_trigger);
  Check(pressed_trigger[1] > open_trigger[1] + 0.3, "the trigger moves the "
        "index finger in the middle of a blend", pressed_trigger[1],
        open_trigger[1] + 0.3);

  // THE MESH.
  static om::Triangle mesh_a[om::kHandTriangles], mesh_b[om::kHandTriangles];
  const float fist[om::kFingers] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  Near("a hand with a pose has the triangles of the first test hand",
       om::BuildHandPose(fist, mesh_a), om::kHandTriangles, 0);
  const float two[om::kFingers] = {0.0f, 0.3f, 0.7f, 0.7f, 0.7f};
  om::BuildHandPose(two, mesh_a);
  om::BuildHand(0.3f, 0.7f, mesh_b);
  Near("the first test hand is the hand with the thumb at rest",
       memcmp(mesh_a, mesh_b, sizeof(mesh_a)), 0, 0);

  // THE TINT.
  float skin[3];
  om::SkinTint(0.0f, skin);
  Near("a neutral player has the plain skin", Gap(skin, om::kSkinNeutral), 0.0,
       1e-6);
  om::SkinTint(1.0f, skin);
  Near("a good player has the pale skin", Gap(skin, om::kSkinGood), 0.0, 1e-6);
  Check(skin[0] + skin[1] + skin[2] >
            om::kSkinNeutral[0] + om::kSkinNeutral[1] + om::kSkinNeutral[2],
        "the good skin is lighter than the plain skin",
        skin[0] + skin[1] + skin[2], 2.17);
  om::SkinTint(-1.0f, skin);
  Near("an evil player has the dark skin", Gap(skin, om::kSkinEvil), 0.0, 1e-6);
  Check(skin[0] / skin[1] > 2.0f * om::kSkinNeutral[0] / om::kSkinNeutral[1] &&
            skin[0] < 0.5f * om::kSkinNeutral[0] + 0.01f,
        "the evil skin is darker and more red", skin[0] / skin[1], 2.57);
  om::SkinTint(-5.0f, skin);
  Near("an alignment past -1 counts as -1", Gap(skin, om::kSkinEvil), 0.0,
       1e-6);
  om::SkinTint(0.5f, skin);
  Near("half good is half of the way", skin[2],
       0.5 * (om::kSkinNeutral[2] + om::kSkinGood[2]), 1e-6);
}

inline void HoldChecks() {
  printf("\nOne pair of hands: the held object and the wrist lock\n");
  // A grip that is turned, and a hand of 19 units.
  float rotation[9];
  exact_checks::GripRotation(30.0f, 45.0f, -60.0f, rotation);
  const float grip[3] = {100.0f, 50.0f, -20.0f};
  const float length = 19.0f;
  const float same[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  const float no_hang[3] = {0.0f, 0.0f, 0.0f};
  for (int side = 0; side < 2; ++side) {
    const bool left = side == 1;
    const float radius = 3.0f;
    float origin[3];
    hx::HoldOrigin(grip, rotation, length, left, radius, no_hang, same, origin);
    // Back into the hand frame: transpose(R) * (origin - grip) / length.
    float local[3];
    for (int c = 0; c < 3; ++c) {
      local[c] = 0.0f;
      for (int r = 0; r < 3; ++r) {
        local[c] += rotation[r * 3 + c] * (origin[r] - grip[r]);
      }
      local[c] /= length;
    }
    const float palm_x = om::kPalmX - om::kPalmHalfThick;
    const float sign = left ? -1.0f : 1.0f;
    Near(left ? "left hand: the ball touches the palm"
              : "right hand: the ball touches the palm",
         (palm_x - sign * local[0]) * length, radius, 1e-3);
    Near(left ? "left hand: the ball is behind the knuckles, on the palm"
              : "right hand: the ball is behind the knuckles, on the palm",
         local[1], om::kHoldY, 1e-5);
    Check(local[1] > om::kKnuckleY && local[1] < om::kKnuckleY + om::kPalmLength,
          "the ball is between the knuckles and the wrist", local[1],
          om::kHoldY);
    Near("the ball is in the middle of the palm's width", local[2], 0.0, 1e-5);
  }
  // The body of the object: half the hang, or the 2D radius.
  const float hang[3] = {0.0f, -2.0f, 0.0f};
  Near("the body of a flat object takes its 2D radius",
       hx::BodyRadius(hang, 1.7f), 1.7, 1e-6);
  Near("the body of a tall object takes half its hang",
       hx::BodyRadius(hang, 0.3f), 1.0, 1e-6);
  float middle[3], origin[3];
  hx::HoldOrigin(grip, rotation, length, false, 1.0f, no_hang, same, middle);
  hx::HoldOrigin(grip, rotation, length, false, 1.0f, hang, same, origin);
  Near("the origin is half the hang from the middle of the body",
       origin[1] - middle[1], -1.0, 1e-4);

  // THE TURN with the hand.
  float then[9], now[9], d[9], back[9];
  exact_checks::GripRotation(90.0f, 0.0f, -90.0f, then);
  exact_checks::GripRotation(-90.0f, 0.0f, 90.0f, now);
  hx::TurnSince(now, then, d);
  hx::Multiply3(d, then, back);
  double worst = 0.0;
  for (int i = 0; i < 9; ++i) {
    if (fabs(back[i] - now[i]) > worst) worst = fabs(back[i] - now[i]);
  }
  Near("the turn since then, after the pose then, is the pose now", worst, 0.0,
       1e-5);
  hx::TurnSince(then, then, d);
  const float row[3] = {0.3f, -0.5f, 0.8f};
  float turned[3];
  hx::Turn(d, row, turned);
  Near("with no turn of the hand the object keeps its axes", Gap(row, turned),
       0.0, 1e-6);
  // An axis of the object that lay along the fingers stays along the fingers.
  float f_then[3], b_then[3], f_now[3], b_now[3];
  hx::HandAxes(then, false, f_then, b_then);
  hx::HandAxes(now, false, f_now, b_now);
  hx::TurnSince(now, then, d);
  hx::Turn(d, f_then, turned);
  Near("an axis of the object along the fingers stays along the fingers",
       Gap(turned, f_now), 0.0, 1e-5);
  hx::HoldOrigin(grip, now, length, false, 1.0f, hang, d, origin);
  hx::HoldOrigin(grip, now, length, false, 1.0f, no_hang, d, middle);
  float half[3] = {0.0f, -1.0f, 0.0f}, half_now[3];
  hx::Turn(d, half, half_now);
  const float want[3] = {middle[0] + half_now[0], middle[1] + half_now[1],
                         middle[2] + half_now[2]};
  Near("the origin turns about the middle of the body with the hand",
       Gap(origin, want), 0.0, 1e-4);

  // THE SETTLE: no jump at the change of state.
  Near("on the first frame the object is where the game put it",
       hx::SettleShare(0.0f), 1.0, 1e-6);
  Near("after the settle the object is in the palm",
       hx::SettleShare(hx::kSettleSeconds), 0.0, 1e-6);
  double worst_move = 0.0;
  bool falls = true;
  for (int i = 0; i < 20; ++i) {
    const double a = hx::SettleShare(i / 90.0f);
    const double b = hx::SettleShare((i + 1) / 90.0f);
    if (b > a + 1e-6) falls = false;
    if (a - b > worst_move) worst_move = a - b;
  }
  Check(falls, "the offset only gets smaller", falls, 1);
  Check(worst_move < 0.15, "a frame of 1/90 s takes less than 15% of the "
        "offset", worst_move, 0.15);

  // THE WRIST LOCK. An animation whose wrist is turned 79 degrees about the
  // fingers and moved, as the Holding animation is.
  const float c = cosf(79.0f * 3.14159265f / 180.0f);
  const float s = sinf(79.0f * 3.14159265f / 180.0f);
  const float roll[12] = {c, 0.0f, -s, 0.0f, 1.0f, 0.0f, s, 0.0f, c,
                          0.0f, 0.0f, 0.0f};
  float wrist[12];
  hx::MultiplyGame(hx::kBindWrist, roll, wrist);
  wrist[9] = 69.9f;
  wrist[10] = -285.4f;
  wrist[11] = 178.4f;
  float degrees = 0.0f, units = 0.0f;
  hx::WristOff(hx::kBindWrist, &degrees, &units);
  Near("the wrist of the file is 0 degrees from itself", degrees, 0.0, 0.05);
  hx::WristOff(wrist, &degrees, &units);
  Near("the turned wrist is 79 degrees from the file's", degrees, 79.0, 0.05);
  Check(units > 400.0f, "and it is far from the file's wrist", units, 400.0);
  float f[3], b[3], rest[9], exact[12], locked[12];
  hx::HandAxes(rotation, false, f, b);
  hx::RestTurn(hx::kRestYawDeg, hx::kRestPitchDeg, hx::kRestRollDeg, rest);
  const float k = 0.030468f;
  const float tip[3] = {1500.0f, 50.0f, 2100.0f};
  hx::ExactRoot(f, b, rest, k, true, tip, exact);
  Check(hx::WristLockRoot(wrist, exact, locked), "the wrist lock has a root",
        1, 1);
  float got[12], want_wrist[12];
  hx::MultiplyGame(wrist, locked, got);
  hx::MultiplyGame(hx::kBindWrist, exact, want_wrist);
  worst = 0.0;
  for (int i = 0; i < 12; ++i) {
    const double scale = i < 9 ? k : 1.0;
    if (fabs(got[i] - want_wrist[i]) / scale > worst) {
      worst = fabs(got[i] - want_wrist[i]) / scale;
    }
  }
  Check(worst < 0.01, "with the lock, the turned wrist is the file's wrist "
        "under the exact root", worst, 0.0);
  float file_locked[12];
  hx::WristLockRoot(hx::kBindWrist, exact, file_locked);
  worst = 0.0;
  for (int i = 0; i < 12; ++i) {
    const double scale = i < 9 ? k : 1.0;
    if (fabs(file_locked[i] - exact[i]) / scale > worst) {
      worst = fabs(file_locked[i] - exact[i]) / scale;
    }
  }
  Check(worst < 0.01, "an animation that keeps the file's wrist keeps the "
        "exact root", worst, 0.0);
  // The move of an old pose: a bone of the old pose, composed with the lock
  // then and moved by the exact roots, is the bone composed with the lock
  // now.
  float rotation2[9], f2[3], b2[3], exact2[12], locked2[12];
  exact_checks::GripRotation(-90.0f, 0.0f, 90.0f, rotation2);
  hx::HandAxes(rotation2, false, f2, b2);
  const float tip2[3] = {1510.0f, 58.0f, 2093.0f};
  hx::ExactRoot(f2, b2, rest, k, true, tip2, exact2);
  hx::WristLockRoot(wrist, exact2, locked2);
  const float bone[12] = {0.8f, 0.6f, 0.0f, -0.6f, 0.8f, 0.0f, 0.0f, 0.0f, 1.0f,
                          40.0f, 120.0f, -15.0f};
  float in_wrist[12], old_world[12], new_world[12], moved[12];
  hx::MultiplyGame(bone, wrist, in_wrist);
  hx::MultiplyGame(in_wrist, locked, old_world);
  hx::MultiplyGame(in_wrist, locked2, new_world);
  hx::Rebase(old_world, exact, exact2, moved);
  Check(Gap(&moved[9], &new_world[9]) < 0.01, "an old pose with the lock, "
        "moved by the exact roots, is on the new hand",
        Gap(&moved[9], &new_world[9]), 0.0);
}

inline void HandPoseChecks() {
  PoseChecks();
  HoldChecks();
}

}  // namespace hand_pose_checks
