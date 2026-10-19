// The depth of a hint of the game at the game's Hand (hint_hand_depth,
// docs/ui-layer.md, section 15.6). No Windows and no game here, so the math
// test compiles this file.
//
// THE RULE. The game draws a tooltip or a hint at the Hand at the pixel of a
// point of the world: CameraHelp::DrawKeyOrMouse projects the point of the
// Hand mesh with the camera of the pass. The text has no depth of its own. The
// right eye gets the eye difference of that point, so the hint has the depth
// of the Hand:
//
//     one walk:  move = per_rhw * near / depth (one_walk_math::ShiftAtDepth)
//     two walks: move = x of the point in the right eye - x in the left eye
//
// The matrix is the world to camera matrix of the engine (4 rows of 3, the
// translation in the last row, the field of view in the first two columns).
#pragma once

namespace hint_depth_math {

// The camera x and the depth of a point of the world. False when the point is
// not in front of the near plane.
inline bool CameraPoint(const float m[12], const float p[3], float near_plane,
                        float* cx, float* depth) {
  const float x = p[0] * m[0] + p[1] * m[3] + p[2] * m[6] + m[9];
  const float z = p[0] * m[2] + p[1] * m[5] + p[2] * m[8] + m[11];
  if (!(near_plane > 0.0f) || !(z >= near_plane)) return false;
  *cx = x;
  *depth = z;
  return true;
}

// The pixel across the render of a camera point, as LH3DTech::ProjectPoint
// makes it.
inline float PixelX(float cx, float depth, float half_res_x) {
  return (1.0f + cx / depth) * half_res_x;
}

// One walk: the move from the left eye to the right eye of a thing at this
// depth. per_rhw is one_walk_math::ShiftPerRhw.
inline float WalkMove(float per_rhw, float near_plane, float depth) {
  if (!(depth > 0.0f) || !(near_plane > 0.0f)) return 0.0f;
  return per_rhw * near_plane / depth;
}

}  // namespace hint_depth_math
