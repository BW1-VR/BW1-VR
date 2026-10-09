// Move the camera for one eye, and keep everything derived from it in step.
//
// WHY ONE MATRIX IS NOT ENOUGH
//
// The world to camera matrix at 0x00EA9E40 is the one that
// LH3DTech::ProjectPoint reads. It is not the whole camera.
// LH3DTech::UpdateCamera builds a family of values from one camera position,
// and different parts of the engine read different members of that family.
// Moving one member and leaving the rest at the mono camera makes those parts
// draw their geometry in the wrong place, differently for each eye. That is
// what made villager shadows change shape between the eyes, and what made
// smoke sit at no depth at all.
//
// THE FAMILY, all written by LH3DTech::UpdateCamera at 0x00819920, all read
// out of the disassembly rather than assumed:
//
//   0x00EA1D28  world to camera, UNSCALED.
//               Built first, at 0x00819A58, by LH3DTech::UpdateWorldToCamera.
//               LH3DSprite::Draw reads this one and nothing else. It loads
//               0x00EA1D4C, which is m[9], at 0x008405B4. Because the mod
//               never touched it, every sprite landed on the same screen
//               position in both eyes. Identical pixels are zero disparity,
//               and zero disparity reads as infinitely far away. That is the
//               smoke.
//
//   0x00EA9E40  the same matrix with its columns scaled by the field of view
//               terms. LH3DTech::ProjectPoint reads this. The relation is
//               exact, at 0x00819B40:
//                   fld [0xea1d4c] / fmul st(1) / fstp [0xea9e64]
//               so scaled m[9] = unscaled m[9] * inv_half_tan_fov_x. An offset
//               of d world units is therefore d on the unscaled matrix and
//               d * inv_half_tan_fov_x on the scaled one.
//
//   0x00EA1D58  a verbatim copy of the scaled matrix, made at 0x00819BF6 and
//               0x00819C50. GLandscape::Draw projects through this copy.
//
//   0x00EA9DE0  the INVERSE of the scaled matrix, built at 0x00819BB0:
//                   mov edx, 0xea9e40 / mov ecx, 0xea9de0
//                   call LHMatrix::SetInverse
//               Seventeen places read it.
//
// THE INVERSE CAUSED THE WORST ARTIFACT, and it is worth writing down because
// it is not obvious. A shadow is a projected texture. fn_008801D0 builds the
// shadow's texture matrix by copying the camera to world matrix at 0x00EA9DE0
// and scaling it by the caster bounding box. The receiver vertices are then
// transformed to camera space by the world to camera matrix. Camera to world
// followed by world to camera cancels, so the lookup lands in the right place,
// as long as the two agree.
//
// They stopped agreeing. UpdateCamera is not called from Process3dEngine, so
// poking 0x00EA9E40 between the passes left 0x00EA9DE0 at the mono camera.
// What remained after the two matrices failed to cancel was a translation
// along the eye offset, and the per vertex perspective divide turned that
// translation into a skew. So the shadow slides across the ground AND changes
// shape, in opposite directions for the two eyes.
//
// To reuse the first pass geometry is not the answer. The shadow map, the light
// position and the bounding box are already identical in both passes. The
// projection has to differ per eye, and it is correct as soon as the inverse
// is the true inverse of the matrix being used. So this calls the game's own
// LHMatrix::SetInverse after each move.
//
// THE RANDOM NUMBER GENERATOR, which is a separate bug with the same symptom.
//
// ParticleChainJoint::DrawAt at 0x00679E80 and ParticleLightMap::DrawAt at
// 0x0067B220 each call GRand::LocalFloatRand three times and add the results
// to a world position, as a side effect of drawing. GRand::LocalFloatRand at
// 0x006DE590 does
//     mov eax, [0xd0195c]      ; GGame::g_game
//     add eax, 0x205a3c        ; the seed
// and _LHRand__FlRU writes the seed back on every call. Drawing twice
// therefore gives the second eye different jitter from the first, so those
// particles are in genuinely different places in the two pictures. Freezing
// the animation clock does not help, because the generator is not driven by
// time.
//
// Putting the seed back before the second pass looks like the obvious answer.
// IT IS NOT: the draw probe counts 24760 wrong batches at the mesh transform
// fn_00856D40 with the seed replayed, and 150 in 150 frames without. The
// generator feeds more than jitter, and something downstream decides WHAT to
// draw from it, so replaying the sequence changes the scene.
// That shows up as a large "nomatch" count, which means the two passes did not
// even draw the same number of things.
//
// So it stays off, and the particle jitter is open. A fix that works has
// to leave the sequence alone and make the particles ask for the same numbers,
// which means hooking the two DrawAt functions rather than the generator.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);

namespace {

const size_t kMatrixFloats = 12;
const size_t kMatrixBytes = sizeof(float) * kMatrixFloats;
const int kTranslationX = 9;  // m[9], the camera space x translation

// LHMatrix::SetInverse, __fastcall(this = destination in ecx,
// const LHMatrix& source in edx). The mangled name says QAI, which is
// __fastcall, and the call site passes both in registers and pushes nothing.
typedef void(__fastcall* SetInverseFn)(void* destination, const void* source);

float* Scaled() {
  return reinterpret_cast<float*>(bw::tech::kWorldToCamera);
}
float* Unscaled() {
  return reinterpret_cast<float*>(bw::tech::kWorldToCameraUnscaled);
}
float* ScaledCopy() {
  return reinterpret_cast<float*>(bw::tech::kWorldToCameraCopy);
}
void* Inverse() {
  return reinterpret_cast<void*>(bw::tech::kCameraToWorldInverse);
}

const bw::tech::InfoTransform* Info() {
  return reinterpret_cast<const bw::tech::InfoTransform*>(
      bw::tech::kInfoTransform);
}

// The seed GRand::LocalFloatRand steps, reached through GGame::g_game.
uint32_t* RandomSeed() {
  const uintptr_t* game = reinterpret_cast<const uintptr_t*>(bw::game::kGGame);
  if (IsBadReadPtr(game, sizeof(uintptr_t)) || *game == 0) return nullptr;
  uint32_t* seed =
      reinterpret_cast<uint32_t*>(*game + bw::game::kRandomSeedOffset);
  if (IsBadWritePtr(seed, sizeof(uint32_t))) return nullptr;
  return seed;
}

struct Saved {
  float scaled[kMatrixFloats];
  float unscaled[kMatrixFloats];
  float scaled_copy[kMatrixFloats];
  float inverse[kMatrixFloats];
  uint32_t random_seed;
  bool have_scaled, have_unscaled, have_copy, have_inverse, have_seed;
};

Saved g_saved = {};
bool g_captured = false;

// Every fix is a switch, so a report of "this still looks wrong" can be
// narrowed to one change without a rebuild, and so the draw probe can measure
// each one on its own.
bool g_fix_unscaled = true;
bool g_fix_copy = true;
bool g_fix_inverse = true;
// Measured OFF. Putting the seed back sounded obviously right and it is not.
// With it on, and nothing else changed, the mesh transform at fn_00856D40 went
// from 150 rebuilt batches in 150 frames to 24760, and SetupThing::DrawBox
// reported batches with disparity, which a screen space menu cannot have. The
// high "nomatch" count says what really happened: the two passes stopped
// drawing the same NUMBER of things. Something decides what to draw from a
// random number, so replaying the sequence changes the scene rather than
// steadying it. Left in as a switch, and left off.
bool g_fix_random = false;

// Checked once, not once per frame. These are fixed addresses in the game's
// own .data, so whether they are writable cannot change while it runs, and
// IsBadWritePtr is far too expensive to call on every frame for no new
// information.
bool Readable(const void* p) {
  return !IsBadWritePtr(const_cast<void*>(p), kMatrixBytes);
}

struct Checked {
  bool done;
  bool scaled, unscaled, copy, inverse;
};
Checked g_checked = {false, false, false, false, false};

void CheckOnce() {
  if (g_checked.done) return;
  g_checked.done = true;
  g_checked.scaled = Readable(Scaled());
  g_checked.unscaled = Readable(Unscaled());
  g_checked.copy = Readable(ScaledCopy());
  g_checked.inverse = Readable(Inverse());
}

// WHAT THE TWELVE FLOATS ARE.
//
// Phase 3 has to turn the camera, not just slide it sideways, and a rotation
// cannot be applied without knowing which floats are which. Two things are
// known already and neither is enough: m[9] is the camera space x translation,
// and scaled m[9] is unscaled m[9] times inv_half_tan_fov_x.
//
// So the answer is read out of the running game rather than assumed. The
// element by element ratio between the scaled and unscaled matrices says which
// elements the field of view terms touch:
//
//   elements 0,1,2 share a factor  ->  rows are the camera axes
//   elements 0,3,6 share a factor  ->  columns are the camera axes
//
// and the row norms say whether the unscaled matrix really is rigid, which the
// rotation maths depends on.
bool g_dump = false;
bool g_dumped = false;

float Norm3(const float* v) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

void DumpOnce() {
  if (!g_dump || g_dumped) return;
  if (!g_saved.have_scaled || !g_saved.have_unscaled) return;
  g_dumped = true;
  const float* u = g_saved.unscaled;
  const float* c = g_saved.scaled;
  ModLog("camera dump  : unscaled %7.4f %7.4f %7.4f | %7.4f %7.4f %7.4f | "
         "%7.4f %7.4f %7.4f | t %9.2f %9.2f %9.2f",
         u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10],
         u[11]);
  ModLog("camera dump  : scaled   %7.4f %7.4f %7.4f | %7.4f %7.4f %7.4f | "
         "%7.4f %7.4f %7.4f | t %9.2f %9.2f %9.2f",
         c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9], c[10],
         c[11]);
  float ratio[12];
  for (int i = 0; i < 12; ++i) {
    ratio[i] = (u[i] > 1e-6f || u[i] < -1e-6f) ? (c[i] / u[i]) : 0.0f;
  }
  ModLog("camera dump  : ratio    %7.4f %7.4f %7.4f | %7.4f %7.4f %7.4f | "
         "%7.4f %7.4f %7.4f | t %7.4f %7.4f %7.4f",
         ratio[0], ratio[1], ratio[2], ratio[3], ratio[4], ratio[5], ratio[6],
         ratio[7], ratio[8], ratio[9], ratio[10], ratio[11]);
  ModLog("camera dump  : unscaled row norms %.4f %.4f %.4f, "
         "column norms %.4f %.4f %.4f",
         Norm3(u), Norm3(u + 3), Norm3(u + 6),
         sqrtf(u[0]*u[0] + u[3]*u[3] + u[6]*u[6]),
         sqrtf(u[1]*u[1] + u[4]*u[4] + u[7]*u[7]),
         sqrtf(u[2]*u[2] + u[5]*u[5] + u[8]*u[8]));
  const bw::tech::InfoTransform* info = Info();
  if (!IsBadReadPtr(info, sizeof(*info))) {
    ModLog("camera dump  : inv_half_tan_fov_x %.4f", info->inv_half_tan_fov_x);
  }

  // ANSWERED: 0x00EA9E80 is the light in camera space, not a camera. It is
  // printed beside the rigid inverse of the unscaled matrix, which is what
  // showed they are nothing like each other. Kept because the comparison is
  // the evidence.
  const float* c2w =
      reinterpret_cast<const float*>(bw::tech::kLightInCameraSpace);
  if (!IsBadReadPtr(c2w, kMatrixBytes)) {
    ModLog("camera dump  : c2w      %7.4f %7.4f %7.4f | %7.4f %7.4f %7.4f | "
           "%7.4f %7.4f %7.4f | t %9.2f %9.2f %9.2f",
           c2w[0], c2w[1], c2w[2], c2w[3], c2w[4], c2w[5], c2w[6], c2w[7],
           c2w[8], c2w[9], c2w[10], c2w[11]);
    // The rigid inverse of the unscaled matrix: transpose the rotation and
    // carry the translation back through it.
    float inv[12];
    for (int r = 0; r < 3; ++r) {
      for (int col = 0; col < 3; ++col) inv[r * 3 + col] = u[col * 3 + r];
    }
    for (int col = 0; col < 3; ++col) {
      inv[9 + col] = -(u[9] * u[col * 3 + 0] + u[10] * u[col * 3 + 1] +
                       u[11] * u[col * 3 + 2]);
    }
    ModLog("camera dump  : rigid^-1 %7.4f %7.4f %7.4f | %7.4f %7.4f %7.4f | "
           "%7.4f %7.4f %7.4f | t %9.2f %9.2f %9.2f",
           inv[0], inv[1], inv[2], inv[3], inv[4], inv[5], inv[6], inv[7],
           inv[8], inv[9], inv[10], inv[11]);
  }
  const float* pos = reinterpret_cast<const float*>(bw::tech::kCameraPos);
  if (!IsBadReadPtr(pos, sizeof(float) * 3)) {
    ModLog("camera dump  : camera pos %9.2f %9.2f %9.2f", pos[0], pos[1],
           pos[2]);
  }
}

}  // namespace

void EyeCameraDump(bool on) { g_dump = on; }

// Ask for the next few head poses to be written out. Called once a window, so
// the log shows whether the pose is actually moving.
int g_pose_log_budget = 2;
void EyeCameraLogPoses(int count) { g_pose_log_budget += count; }

// Save the camera the game chose. Called once per frame, before either pass.
bool EyeCameraCapture() {
  CheckOnce();
  if (!g_checked.scaled) return false;

  memcpy(g_saved.scaled, Scaled(), kMatrixBytes);
  g_saved.have_scaled = true;

  g_saved.have_unscaled = g_fix_unscaled && g_checked.unscaled;
  if (g_saved.have_unscaled) {
    memcpy(g_saved.unscaled, Unscaled(), kMatrixBytes);
  }

  g_saved.have_copy = g_fix_copy && g_checked.copy;
  if (g_saved.have_copy) {
    memcpy(g_saved.scaled_copy, ScaledCopy(), kMatrixBytes);
  }

  g_saved.have_inverse = g_fix_inverse && g_checked.inverse;
  if (g_saved.have_inverse) {
    memcpy(g_saved.inverse, Inverse(), kMatrixBytes);
  }

  g_saved.have_seed = false;
  if (g_fix_random) {
    uint32_t* seed = RandomSeed();
    if (seed != nullptr) {
      g_saved.random_seed = *seed;
      g_saved.have_seed = true;
    }
  }

  g_captured = true;
  DumpOnce();
  return true;
}

// Slide the eye d world units along the camera right axis. Positive is right.
void EyeCameraSetEye(float d) {
  if (!g_captured || !g_saved.have_scaled) return;

  // Camera space x is pre-multiplied by the horizontal field of view term, so
  // an offset in world units has to be scaled to match on the scaled matrices
  // and left alone on the unscaled one.
  float scale = 1.0f;
  const bw::tech::InfoTransform* info = Info();
  if (!IsBadReadPtr(info, sizeof(*info)) && info->inv_half_tan_fov_x > 0.0f) {
    scale = info->inv_half_tan_fov_x;
  }

  float* scaled = Scaled();
  memcpy(scaled, g_saved.scaled, kMatrixBytes);
  scaled[kTranslationX] -= scale * d;

  if (g_saved.have_unscaled) {
    float* unscaled = Unscaled();
    memcpy(unscaled, g_saved.unscaled, kMatrixBytes);
    unscaled[kTranslationX] -= d;
  }

  if (g_saved.have_copy) {
    float* copy = ScaledCopy();
    memcpy(copy, g_saved.scaled_copy, kMatrixBytes);
    copy[kTranslationX] -= scale * d;
  }

  // Rebuild the inverse with the game's own routine, rather than editing the
  // translation row by hand. It is one call per eye and it cannot drift.
  if (g_saved.have_inverse) {
    SetInverseFn set_inverse =
        reinterpret_cast<SetInverseFn>(bw::tech::kSetInverse);
    set_inverse(Inverse(), scaled);
  }

  // Both passes must draw the same particles, so the second pass starts the
  // generator where the first one started it, not where it left off.
  if (g_saved.have_seed) {
    uint32_t* seed = RandomSeed();
    if (seed != nullptr) *seed = g_saved.random_seed;
  }
}

// Move the camera by a full rigid transform, not just sideways.
//
// The matrix is 4 rows of 3 floats in the row vector convention, measured out
// of the running game rather than assumed:
//
//     p_camera = p_world * R + t      R is rows 0..2, t is row 3
//
// and the COLUMNS of R are the camera axes in world space, which is why the
// field of view terms scale elements 0,3,6,9 together and 1,4,7,10 together.
//
// Applying a head pose (H, d) expressed in camera space:
//
//     p_newcam = (p_camera - d) * H = p_world * (R * H) + (t - d) * H
//
// so R' = R * H and t' = (t - d) * H. The old sideways only move is the same
// thing with H as the identity, which is how this was checked.
//
// The scaled matrix is NOT transformed directly, because its columns carry the
// field of view terms and a rotation would mix them. It is rebuilt from the
// unscaled one, column by column, exactly as the engine's own UpdateCamera
// does it.
void EyeCameraSetPose(const float h[9], const float d[3], bool log) {
  if (!g_captured || !g_saved.have_scaled) return;
  if (!g_saved.have_unscaled) {
    // Without the unscaled matrix there is nothing rigid to rotate, and
    // silently doing nothing is how a feature looks broken rather than off.
    static bool said = false;
    if (!said) {
      said = true;
      ModLog("head         : no unscaled camera matrix, so the head cannot be "
             "applied. Set fix_unscaled=1.");
    }
    return;
  }
  extern int g_pose_log_budget;
  static int logged = 0;
  if (log && logged < g_pose_log_budget) {
    ++logged;
    ModLog("head         : pose %6.3f %6.3f %6.3f | %6.3f %6.3f %6.3f | "
           "%6.3f %6.3f %6.3f  move %.2f %.2f %.2f",
           h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], d[0], d[1],
           d[2]);
  }

  const float* u = g_saved.unscaled;
  float out[kMatrixFloats];

  // Rows 0..2: R' = R * H.
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      out[row * 3 + col] = u[row * 3 + 0] * h[0 * 3 + col] +
                           u[row * 3 + 1] * h[1 * 3 + col] +
                           u[row * 3 + 2] * h[2 * 3 + col];
    }
  }
  // Row 3: t' = t * H - d.
  //
  // THE EYE OFFSET COMES AFTER THE ROTATION, NOT BEFORE.
  //
  // The eyes sit on the head's own right axis, and after a roll that axis is
  // the rolled one. Writing (t - d) * H instead puts the offset on the axis
  // the camera had BEFORE it rolled, so the two eyes separate along the level
  // axis while the picture is tilted. The error is dx * (cos(roll) - 1,
  // sin(roll), 0), and the sin term is the one that hurts: the eyes gain
  // separation * sin(roll) of VERTICAL disparity, about 23 mm at a 20 degree
  // tilt, which no pair of eyes can fuse.
  //
  // Derivation, in the row vector convention this matrix uses. With the eye at
  // world position e and basis B, t = -e * B. After the roll, B1 = B0 * H, and
  // the eye moves by dx along B1's right column, so
  //     t1 = -(e0 + dx * B1_right) * B1 = t0 * H - d.
  for (int col = 0; col < 3; ++col) {
    out[9 + col] = u[9] * h[0 * 3 + col] + u[10] * h[1 * 3 + col] +
                   u[11] * h[2 * 3 + col] - d[col];
  }

  float* unscaled = Unscaled();
  if (g_saved.have_unscaled) memcpy(unscaled, out, kMatrixBytes);

  // Rebuild the scaled matrix by scaling each column.
  float factor[3] = {1.0f, 1.0f, 1.0f};
  const bw::tech::InfoTransform* info = Info();
  if (!IsBadReadPtr(info, sizeof(*info))) {
    if (info->inv_half_tan_fov_x > 0.0f) factor[0] = info->inv_half_tan_fov_x;
    if (info->inv_half_tan_fov_y > 0.0f) factor[1] = info->inv_half_tan_fov_y;
  }
  float scaled_out[kMatrixFloats];
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 3; ++col) {
      scaled_out[row * 3 + col] = out[row * 3 + col] * factor[col];
    }
  }
  memcpy(Scaled(), scaled_out, kMatrixBytes);
  if (g_saved.have_copy) memcpy(ScaledCopy(), scaled_out, kMatrixBytes);
  if (g_saved.have_inverse) {
    SetInverseFn set_inverse =
        reinterpret_cast<SetInverseFn>(bw::tech::kSetInverse);
    set_inverse(Inverse(), Scaled());
  }
  if (g_saved.have_seed) {
    uint32_t* seed = RandomSeed();
    if (seed != nullptr) *seed = g_saved.random_seed;
  }
}

// Put the mono camera back. Everything outside the scene walk reads these.
void EyeCameraRestore() {
  if (!g_captured) return;
  if (g_saved.have_scaled) memcpy(Scaled(), g_saved.scaled, kMatrixBytes);
  if (g_saved.have_unscaled) {
    memcpy(Unscaled(), g_saved.unscaled, kMatrixBytes);
  }
  if (g_saved.have_copy) {
    memcpy(ScaledCopy(), g_saved.scaled_copy, kMatrixBytes);
  }
  if (g_saved.have_inverse) memcpy(Inverse(), g_saved.inverse, kMatrixBytes);
  // The seed is deliberately NOT restored. The world should move on from one
  // draw of the particles, not from two.
  g_captured = false;
}

void EyeCameraSetFixes(bool unscaled, bool copy, bool inverse, bool random) {
  g_fix_unscaled = unscaled;
  g_fix_copy = copy;
  g_fix_inverse = inverse;
  g_fix_random = random;
}

void EyeCameraReport() {
  ModLog("eye camera   : unscaled=%d copy=%d inverse=%d random=%d",
         g_fix_unscaled ? 1 : 0, g_fix_copy ? 1 : 0, g_fix_inverse ? 1 : 0,
         g_fix_random ? 1 : 0);
}
