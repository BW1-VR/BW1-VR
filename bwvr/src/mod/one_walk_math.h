// The right eye from the batches of the left eye (docs/backend.md, "One walk
// for both eyes"). Pure math with no game and no device, so that the checks in
// bwvr/test/controller_math/ can run it.
//
// THE RULE. The two eye passes differ in one number: the camera space x
// translation of the world to camera matrix (stereo-artifacts.md, "How the
// mod makes the two eyes"). For an eye offset d along the camera right axis,
// the scaled matrix gets m[9] = m9_mono - scale * d, where scale is
// inv_half_tan_fov_x. With head roll the offset is applied after the roll,
// on the rolled right axis, so the same holds (eye_camera.cpp,
// EyeCameraSetPose). So for a point at one world position:
//
//     cam_x(right) - cam_x(left) = -scale * separation
//     cam_y and cam_z do not change
//
// The engine projects screen_x = (1 + cam_x / cam_z) * half_res_x and writes
// rhw = near / cam_z. So y, z and rhw are the same in both eyes, and
//
//     x(right) = x(left) - scale * separation * half_res_x * rhw / near
//
// which is exact for each vertex of the world. A batch with no depth (the
// interface and the sky) has the same x in both eyes.
#pragma once

#include <stdint.h>
#include <string.h>

namespace one_walk_math {

// Pixels per unit of rhw that a vertex moves from the left eye to the right
// eye. Multiply by the rhw of the vertex. Negative: the right eye sees a near
// thing further to the left. 0 when the near plane is not usable.
inline float ShiftPerRhw(float inv_half_tan_fov_x, float separation,
                         float half_res_x, float near_plane) {
  if (!(near_plane > 0.0f)) return 0.0f;
  return -inv_half_tan_fov_x * separation * half_res_x / near_plane;
}

// How the right eye gets a batch.
enum BatchKind {
  kWorld = 0,  // each vertex moves by its own rhw
  kFlat = 1,   // every rhw is exactly 1: a screen space batch, no move
  kSky = 2,    // every z is 1 or more: at infinity, no move
};

// The vertex count that a draw of primitive_count primitives of a type reads,
// with the D3DPRIMITIVETYPE values: 1 point list, 2 line list, 3 line strip,
// 4 triangle list, 5 triangle strip, 6 triangle fan. 0 for an unknown type.
inline uint32_t VertexCount(int type, uint32_t primitive_count) {
  switch (type) {
    case 1: return primitive_count;
    case 2: return primitive_count * 2;
    case 3: return primitive_count + 1;
    case 4: return primitive_count * 3;
    case 5: return primitive_count + 2;
    case 6: return primitive_count + 2;
    default: return 0;
  }
}

// Copies count vertices of stride bytes, each starting with x, y, z, rhw as
// floats, from src to dst for the right eye. Every vertex moves by offset
// (the width of the left half of the target). A world batch also moves each
// vertex by per_rhw * rhw. Returns the kind that it found.
inline BatchKind CopyForRightEye(const uint8_t* src, uint8_t* dst,
                                 uint32_t count, uint32_t stride, float offset,
                                 float per_rhw) {
  if (count == 0 || stride < 16) return kFlat;
  memcpy(dst, src, static_cast<size_t>(count) * stride);
  bool all_rhw_one = true;
  bool all_far = true;
  for (uint32_t i = 0; i < count; ++i) {
    const float* v = reinterpret_cast<const float*>(src + i * stride);
    if (v[3] != 1.0f) all_rhw_one = false;
    if (v[2] < 1.0f) all_far = false;
  }
  const BatchKind kind = all_rhw_one ? kFlat : (all_far ? kSky : kWorld);
  for (uint32_t i = 0; i < count; ++i) {
    float* v = reinterpret_cast<float*>(dst + i * stride);
    const float rhw = reinterpret_cast<const float*>(src + i * stride)[3];
    v[0] += kind == kWorld ? offset + per_rhw * rhw : offset;
  }
  return kind;
}

// Copies count vertices and moves each by offset only. For a batch that
// already has the place of the right eye.
inline void CopyMoved(const uint8_t* src, uint8_t* dst, uint32_t count,
                      uint32_t stride, float offset) {
  if (count == 0 || stride < 4) return;
  memcpy(dst, src, static_cast<size_t>(count) * stride);
  for (uint32_t i = 0; i < count; ++i) {
    reinterpret_cast<float*>(dst + i * stride)[0] += offset;
  }
}

}  // namespace one_walk_math
