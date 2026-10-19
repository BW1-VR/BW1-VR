#version 450
#extension GL_EXT_multiview : require
// The fragment shader of the Vulkan renderer of the scene (vk_scene.cpp,
// docs/backend.md, "The Vulkan renderer"). It makes texture stage 0 of
// Direct3D 7 for the operations and inputs that the game uses, texture
// stage 1 when the draw has it (vk_stage1), the alpha test, the specular
// color and the fog of the vertices.
//
//   ops.x  the color of stage 0: operation | arg1 << 8 | arg2 << 16
//   ops.y  the alpha of stage 0, in the same form
//   ops.z  bit 0 a texture, bit 1 the alpha test, bits 4 to 7 its compare
//          function, bit 8 the specular color, bit 9 the fog
//   alpha  x: the reference of the alpha test (0 to 1)
//   fog_color  the color of the fog
//   ops1.x  the color of stage 1: operation | arg1 << 8 | arg2 << 16, or 0
//           when the draw has no stage 1
//   ops1.y  the alpha of stage 1, in the same form
//
// Compile with:
//   glslang -V --target-env vulkan1.3 --vn kVkSceneFrag -o vk_scene_frag.h
//       vk_scene.frag

layout(location = 0) in vec4 in_diffuse;
layout(location = 1) in vec4 in_specular;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec2 in_uv1;

layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D tex1;

layout(push_constant) uniform Draw {
  vec4 viewport;
  vec4 depth;
  uvec4 ops;
  vec4 alpha;
  vec4 fog_color;
  uvec4 ops1;
} draw;

layout(location = 0) out vec4 out_color;

// D3DTA_*: 0 diffuse, 1 current, 2 texture, 3 the texture factor (not used
// by the game), 4 specular. 0x10 the complement, 0x20 the alpha in each
// channel.
vec4 Arg(uint arg, vec4 texel, vec4 current) {
  vec4 v;
  const uint which = arg & 15u;
  if (which == 2u) {
    v = texel;
  } else if (which == 1u) {
    v = current;
  } else if (which == 4u) {
    v = in_specular;
  } else {
    v = in_diffuse;
  }
  if ((arg & 0x20u) != 0u) v = vec4(v.a);
  if ((arg & 0x10u) != 0u) v = vec4(1.0) - v;
  return v;
}

// D3DTOP_*: 2 arg1, 3 arg2, 4 the product, 5 the product times 2, 6 times
// 4, 7 the sum, 8 the sum less 0.5, 10 the difference. Else arg1.
vec4 Op(uint op, vec4 a, vec4 b) {
  if (op == 3u) return b;
  if (op == 4u) return a * b;
  if (op == 5u) return clamp(a * b * 2.0, 0.0, 1.0);
  if (op == 6u) return clamp(a * b * 4.0, 0.0, 1.0);
  if (op == 7u) return clamp(a + b, 0.0, 1.0);
  if (op == 8u) return clamp(a + b - 0.5, 0.0, 1.0);
  if (op == 10u) return clamp(a - b, 0.0, 1.0);
  return a;
}

// D3DCMP_*: 1 never, 2 less, 3 equal, 4 less or equal, 5 greater, 6 not
// equal, 7 greater or equal, 8 always.
bool Passes(uint func, float value, float reference) {
  if (func == 1u) return false;
  if (func == 2u) return value < reference;
  if (func == 3u) return value == reference;
  if (func == 4u) return value <= reference;
  if (func == 5u) return value > reference;
  if (func == 6u) return value != reference;
  if (func == 7u) return value >= reference;
  return true;
}

void main() {
  const uint flags = draw.ops.z;
  const vec4 texel = (flags & 1u) != 0u ? texture(tex, in_uv) : vec4(1.0);
  const uint color_op = draw.ops.x & 255u;
  const uint alpha_op = draw.ops.y & 255u;
  vec4 color = in_diffuse;
  if (color_op > 1u) {
    color.rgb = Op(color_op, Arg((draw.ops.x >> 8) & 255u, texel, in_diffuse),
                   Arg((draw.ops.x >> 16) & 255u, texel, in_diffuse)).rgb;
  }
  if (alpha_op > 1u) {
    color.a = Op(alpha_op, Arg((draw.ops.y >> 8) & 255u, texel, in_diffuse),
                 Arg((draw.ops.y >> 16) & 255u, texel, in_diffuse)).a;
  }
  // Stage 1 takes the result of stage 0 as its current color, for example
  // the light map of a room of the Temple (D3DTOP_MODULATE2X or D3DTOP_ADD
  // of the texture and the current color). An alpha operation of 1 keeps the alpha of stage 0.
  const uint color_op1 = draw.ops1.x & 255u;
  if (color_op1 > 1u) {
    const vec4 texel1 = texture(tex1, in_uv1);
    const vec4 current = color;
    color.rgb = Op(color_op1, Arg((draw.ops1.x >> 8) & 255u, texel1, current),
                   Arg((draw.ops1.x >> 16) & 255u, texel1, current)).rgb;
    const uint alpha_op1 = draw.ops1.y & 255u;
    if (alpha_op1 > 1u) {
      color.a = Op(alpha_op1, Arg((draw.ops1.y >> 8) & 255u, texel1, current),
                   Arg((draw.ops1.y >> 16) & 255u, texel1, current)).a;
    }
  }
  if ((flags & 2u) != 0u &&
      !Passes((flags >> 4) & 15u, floor(color.a * 255.0 + 0.5),
              floor(draw.alpha.x * 255.0 + 0.5))) {
    discard;
  }
  if ((flags & 0x100u) != 0u) {
    color.rgb = clamp(color.rgb + in_specular.rgb, 0.0, 1.0);
  }
  if ((flags & 0x200u) != 0u) {
    color.rgb = mix(draw.fog_color.rgb, color.rgb, in_specular.a);
  }
  out_color = color;
}
