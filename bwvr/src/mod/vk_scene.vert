#version 450
#extension GL_EXT_multiview : require
// The vertex shader of the Vulkan renderer of the scene (vk_scene.cpp,
// docs/backend.md, "The Vulkan renderer"). The game gives each vertex with
// its place on the screen: x and y in pixels of the target, z in 0 to 1 and
// rhw = 1 / w (D3DFVF_XYZRHW). The shader gives the clip position of that
// pixel in the viewport of the draw, times w, so the device makes the
// perspective of the texture as with the vertices of the game. A pixel of
// Direct3D 9 has its centre at the whole number, and a pixel of Vulkan at
// the half, so x and y move by half a pixel.
//
// MULTIVIEW. The target has a layer for each eye, and each draw goes to
// both (gl_ViewIndex 0 the left eye, 1 the right eye). The game draws the
// left eye. For the right eye a vertex moves by per_rhw * rhw, as one walk
// moves it (one_walk_math.h), unless the batch is flat or at infinity
// (ops.w bit 0). A batch of the interface for one eye only goes to the
// other eye outside the clip space (ops.w bits 8 and 9: the eyes).
//
// Compile with:
//   glslang -V --target-env vulkan1.3 --vn kVkSceneVert -o vk_scene_vert.h
//       vk_scene.vert

layout(location = 0) in vec4 position;  // x, y, z, rhw
layout(location = 1) in vec4 diffuse;   // B8G8R8A8: the format gives RGBA
layout(location = 2) in vec4 specular;
layout(location = 3) in vec2 uv;

layout(push_constant) uniform Draw {
  vec4 viewport;  // X, Y, 2 / Width, 2 / Height
  vec4 depth;     // MinZ, 1 / (MaxZ - MinZ), per_rhw, 0
  uvec4 ops;      // see vk_scene.frag
  vec4 alpha;     // the reference of the alpha test, the fog color
  vec4 fog_color;
} draw;

layout(location = 0) out vec4 out_diffuse;
layout(location = 1) out vec4 out_specular;
layout(location = 2) out vec2 out_uv;

void main() {
  const uint eyes = (draw.ops.w >> 8) & 3u;
  if (((eyes >> gl_ViewIndex) & 1u) == 0u) {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    out_diffuse = vec4(0.0);
    out_specular = vec4(0.0);
    out_uv = vec2(0.0);
    return;
  }
  const float w = position.w > 0.0 ? 1.0 / position.w : 1.0;
  float sx = position.x;
  if (gl_ViewIndex == 1 && (draw.ops.w & 1u) == 0u) {
    sx += draw.depth.z * position.w;
  }
  const float x = (sx + 0.5 - draw.viewport.x) * draw.viewport.z - 1.0;
  const float y = (position.y + 0.5 - draw.viewport.y) * draw.viewport.w - 1.0;
  const float z = (position.z - draw.depth.x) * draw.depth.y;
  gl_Position = vec4(x * w, y * w, z * w, w);
  out_diffuse = diffuse;
  out_specular = specular;
  out_uv = uv;
}
