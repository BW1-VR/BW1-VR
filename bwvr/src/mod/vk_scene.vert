#version 450
// The vertex shader of the Vulkan renderer of the scene (vk_scene.cpp,
// docs/backend.md, "The Vulkan renderer"). The game gives each vertex with
// its place on the screen: x and y in pixels of the target, z in 0 to 1 and
// rhw = 1 / w (D3DFVF_XYZRHW). The shader gives the clip position of that
// pixel in the viewport of the draw, times w, so the device makes the
// perspective of the texture as with the vertices of the game. A pixel of
// Direct3D 9 has its centre at the whole number, and a pixel of Vulkan at
// the half, so x and y move by half a pixel.
//
// Compile with:
//   glslang -V --vn kVkSceneVert -o vk_scene_vert.h vk_scene.vert

layout(location = 0) in vec4 position;  // x, y, z, rhw
layout(location = 1) in vec4 diffuse;   // B8G8R8A8: the format gives RGBA
layout(location = 2) in vec4 specular;
layout(location = 3) in vec2 uv;

layout(push_constant) uniform Draw {
  vec4 viewport;  // X, Y, 2 / Width, 2 / Height
  vec4 depth;     // MinZ, 1 / (MaxZ - MinZ), 0, 0
  uvec4 ops;      // see vk_scene.frag
  vec4 alpha;     // the reference of the alpha test, the fog color
  vec4 fog_color;
} draw;

layout(location = 0) out vec4 out_diffuse;
layout(location = 1) out vec4 out_specular;
layout(location = 2) out vec2 out_uv;

void main() {
  const float w = position.w > 0.0 ? 1.0 / position.w : 1.0;
  const float x = (position.x + 0.5 - draw.viewport.x) * draw.viewport.z - 1.0;
  const float y = (position.y + 0.5 - draw.viewport.y) * draw.viewport.w - 1.0;
  const float z = (position.z - draw.depth.x) * draw.depth.y;
  gl_Position = vec4(x * w, y * w, z * w, w);
  out_diffuse = diffuse;
  out_specular = specular;
  out_uv = uv;
}
