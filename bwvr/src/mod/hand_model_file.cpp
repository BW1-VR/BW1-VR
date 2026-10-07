// A hand that the player edited: the mesh of bwvr-hand.glb beside the mod.
// See docs/hand.md, "A hand that the player edited".
//
// WHY. The owner wants to add polygons to the hand of the game. The export
// tool (tools/hand_export) gives him the hand as a Blender file, an FBX and a
// GLB. He edits the mesh, keeps the 22 bones, and exports a glTF binary. This
// file reads it and gives the mesh to hand_model.cpp in place of the mesh of
// the game.
//
// WHAT IT READS. The first mesh with a skin. From each of its triangle
// primitives: the positions, the normals, the first texture points, the
// first set of four bones and weights, and the shape keys with the names
// `evil` and `good`. The textures of the file are not read: the hands keep
// the textures of the game, so the texture points of the file must fit them.
//
// THE BONES. The mod finds each bone by its name: `b00_wrist` to
// `b21_thumb3`, where the number is the bone of the game. The file must have
// each of the 22. A vertex can have up to 4 bones.
//
// THE FIT. The mod does not trust the units and the axes of the file. It
// finds the map that puts the bone joints of the file on the bone joints of
// the game (hand_model_math.h, FitJoints), and it puts the vertices through
// that map. So the size, the up axis and a mirror of the export do not
// matter. A file whose bones are not at the places of the bones of the game
// is not used.
//
// A FILE THAT DOES NOT FIT gives false with the reason, and hand_model.cpp
// writes the reason to the log and uses the mesh of the game.
//
// The reader is cgltf by Johannes Kuhlmann (MIT license), a clone in
// third_party/cgltf. A build with no clone has no reader, and says so.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hand_model.h"

#ifdef BWVR_HAVE_CGLTF

#define CGLTF_IMPLEMENTATION
#pragma warning(push, 0)
#include "cgltf.h"
#pragma warning(pop)

namespace {

namespace hm = hand_model;
namespace hx = hand_exact_math;

// A 4x4 matrix of glTF: 16 floats, a column after a column, and a point is a
// column.
void Multiply4(const float a[16], const float b[16], float out[16]) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += a[k * 4 + r] * b[c * 4 + k];
      out[c * 4 + r] = sum;
    }
  }
}

void Point4(const float m[16], const float p[3], float out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = m[0 * 4 + r] * p[0] + m[1 * 4 + r] * p[1] + m[2 * 4 + r] * p[2] +
             m[3 * 4 + r];
  }
}

void Vector4(const float m[16], const float v[3], float out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = m[0 * 4 + r] * v[0] + m[1 * 4 + r] * v[1] + m[2 * 4 + r] * v[2];
  }
}

float Det4(const float m[16]) {
  return m[0] * (m[5] * m[10] - m[9] * m[6]) -
         m[4] * (m[1] * m[10] - m[9] * m[2]) +
         m[8] * (m[1] * m[6] - m[5] * m[2]);
}

// The name of a bone without the prefix that an editor can put before it.
const char* PlainName(const char* name) {
  if (name == nullptr) return "";
  const char* plain = name;
  for (const char* p = name; *p != 0; ++p) {
    if (*p == ':' || *p == '|') plain = p + 1;
  }
  return plain;
}

struct Parts {
  const cgltf_accessor* position = nullptr;
  const cgltf_accessor* normal = nullptr;
  const cgltf_accessor* uv = nullptr;
  const cgltf_accessor* joints = nullptr;
  const cgltf_accessor* weights = nullptr;
};

void FindParts(const cgltf_attribute* attributes, cgltf_size count,
               Parts* out) {
  for (cgltf_size i = 0; i < count; ++i) {
    const cgltf_attribute& a = attributes[i];
    // Only the first set of texture points, bones and weights.
    const bool first = a.index == 0;
    if (a.type == cgltf_attribute_type_position) out->position = a.data;
    if (a.type == cgltf_attribute_type_normal) out->normal = a.data;
    if (a.type == cgltf_attribute_type_texcoord && first) out->uv = a.data;
    if (a.type == cgltf_attribute_type_joints && first) out->joints = a.data;
    if (a.type == cgltf_attribute_type_weights && first) out->weights = a.data;
  }
}

// All the values of an accessor as floats, `width` for each element, or null.
// The file can store a shape key as a sparse accessor, and only the unpack
// of cgltf reads that kind. The caller frees the result.
float* Unpack(const cgltf_accessor* accessor, int width) {
  if (accessor == nullptr ||
      cgltf_num_components(accessor->type) != static_cast<cgltf_size>(width)) {
    return nullptr;
  }
  const cgltf_size count = accessor->count * static_cast<cgltf_size>(width);
  float* out = static_cast<float*>(malloc(count * sizeof(float) + 1));
  if (out == nullptr) return nullptr;
  if (cgltf_accessor_unpack_floats(accessor, out, count) != count) {
    free(out);
    return nullptr;
  }
  return out;
}

// The index of the shape key with this name, or -1.
int TargetNamed(const cgltf_mesh* mesh, const char* name) {
  for (cgltf_size i = 0; i < mesh->target_names_count; ++i) {
    if (mesh->target_names[i] != nullptr &&
        _stricmp(mesh->target_names[i], name) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Writes the reason, or the summary, and gives false.
bool Say(char* why, int why_size, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf_s(why, static_cast<size_t>(why_size), _TRUNCATE, format, args);
  va_end(args);
  return false;
}

bool Read(const cgltf_data* data, const hm::Skeleton& skeleton, HandMesh* out,
          char* why, int why_size) {
  const cgltf_node* node = nullptr;
  for (cgltf_size i = 0; i < data->nodes_count && node == nullptr; ++i) {
    if (data->nodes[i].mesh != nullptr && data->nodes[i].skin != nullptr) {
      node = &data->nodes[i];
    }
  }
  if (node == nullptr) {
    return Say(why, why_size, "The file has no mesh with a skin");
  }
  const cgltf_skin* skin = node->skin;
  const cgltf_mesh* mesh = node->mesh;
  const int joints = static_cast<int>(skin->joints_count);
  if (joints < hm::kBones || joints > 256) {
    return Say(why, why_size, "The skin of the file has %d bones, and the "
                "hand of the game has %d", joints, hm::kBones);
  }

  // THE BONES by their names, the place of each joint, and the matrix that
  // takes a vertex of the file to its place with the bones at rest.
  int bone_of_joint[256];
  int joint_of_bone[hm::kBones];
  for (int b = 0; b < hm::kBones; ++b) joint_of_bone[b] = -1;
  float (*bind)[16] = static_cast<float (*)[16]>(
      malloc(static_cast<size_t>(joints) * sizeof(float[16])));
  if (bind == nullptr) return Say(why, why_size, "No memory for the bones");
  float file_joint[hm::kBones][3], game_joint[hm::kBones][3];
  for (int j = 0; j < joints; ++j) {
    const int bone = hm::BoneOfName(PlainName(skin->joints[j]->name));
    bone_of_joint[j] = -1;
    float world[16];
    cgltf_node_transform_world(skin->joints[j], world);
    float inverse[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (skin->inverse_bind_matrices != nullptr) {
      cgltf_accessor_read_float(skin->inverse_bind_matrices,
                                static_cast<cgltf_size>(j), inverse, 16);
    }
    Multiply4(world, inverse, bind[j]);
    if (bone >= 0 && joint_of_bone[bone] < 0) {
      joint_of_bone[bone] = j;
      bone_of_joint[j] = bone;
      for (int k = 0; k < 3; ++k) file_joint[bone][k] = world[12 + k];
    }
  }
  for (int b = 0; b < hm::kBones; ++b) {
    if (joint_of_bone[b] < 0) {
      free(bind);
      return Say(why, why_size, "The file has no bone with a name that "
                  "starts with b%02d. Keep the 22 bones of the export and "
                  "their names", b);
    }
    for (int k = 0; k < 3; ++k) game_joint[b][k] = skeleton.rest[b][9 + k];
  }
  hm::Fit fit;
  if (!hm::FitJoints(file_joint, game_joint, &fit)) {
    free(bind);
    return Say(why, why_size, "The bones of the file are on one line, so "
                "the mod cannot find the place of the hand");
  }
  const float worst_mm = fit.worst / hx::kMeshLength * 190.0f;
  if (fit.worst > hm::kFitTolerance * hx::kMeshLength) {
    free(bind);
    return Say(why, why_size, "The bones of the file are not at the places "
                "of the bones of the game: the bone %d is %.1f mm from its "
                "place on a hand of 19 cm. Do not move a bone",
                fit.worst_bone, worst_mm);
  }

  // THE SIZE of the mesh: each triangle primitive with all its parts.
  int vertices = 0, triangles = 0;
  for (cgltf_size p = 0; p < mesh->primitives_count; ++p) {
    const cgltf_primitive& prim = mesh->primitives[p];
    if (prim.type != cgltf_primitive_type_triangles) continue;
    Parts parts;
    FindParts(prim.attributes, prim.attributes_count, &parts);
    if (parts.position == nullptr || parts.uv == nullptr ||
        parts.joints == nullptr || parts.weights == nullptr) {
      free(bind);
      return Say(why, why_size, "A part of the mesh has no %s. Export the "
                  "mesh with its texture points and its skin",
                  parts.position == nullptr ? "positions"
                  : parts.uv == nullptr ? "texture points"
                                        : "bones and weights");
    }
    vertices += static_cast<int>(parts.position->count);
    const cgltf_size corners = prim.indices != nullptr ? prim.indices->count
                                                       : parts.position->count;
    triangles += static_cast<int>(corners / 3);
  }
  const int evil = TargetNamed(mesh, "evil"), good = TargetNamed(mesh, "good");
  if (!HandMeshAllocate(out, vertices, triangles, evil >= 0, good >= 0)) {
    free(bind);
    return Say(why, why_size, "The mesh of the file has %d vertices and %d "
                "triangles. The mod takes 20000 vertices and 20000 triangles "
                "at most", vertices, triangles);
  }

  bool ok = true;
  bool any_normal_missing = false;
  int v0 = 0, t0 = 0;
  float turn_sign = 0.0f;
  for (cgltf_size p = 0; p < mesh->primitives_count && ok; ++p) {
    const cgltf_primitive& prim = mesh->primitives[p];
    if (prim.type != cgltf_primitive_type_triangles) continue;
    Parts parts;
    FindParts(prim.attributes, prim.attributes_count, &parts);
    Parts shape[2];  // evil, good
    const int target[2] = {evil, good};
    for (int s = 0; s < 2; ++s) {
      if (target[s] >= 0 &&
          static_cast<cgltf_size>(target[s]) < prim.targets_count) {
        FindParts(prim.targets[target[s]].attributes,
                  prim.targets[target[s]].attributes_count, &shape[s]);
      }
    }
    const int count = static_cast<int>(parts.position->count);
    float* positions = Unpack(parts.position, 3);
    float* normals = Unpack(parts.normal, 3);
    float* uvs = Unpack(parts.uv, 2);
    float* weights = Unpack(parts.weights, 4);
    float* steps[2] = {Unpack(shape[0].position, 3),
                       Unpack(shape[1].position, 3)};
    float* normal_steps[2] = {Unpack(shape[0].normal, 3),
                              Unpack(shape[1].normal, 3)};
    if (normals == nullptr) any_normal_missing = true;
    if (positions == nullptr || uvs == nullptr || weights == nullptr ||
        parts.joints->is_sparse || parts.uv->count != parts.position->count ||
        parts.weights->count != parts.position->count ||
        parts.joints->count != parts.position->count) {
      ok = Say(why, why_size, "A part of the mesh has data that the mod "
               "cannot read. Export the mesh again with the settings of the "
               "README");
    }
    for (int i = 0; i < count && ok; ++i) {
      const cgltf_size at = static_cast<cgltf_size>(i);
      const int v = v0 + i;
      float position[3], normal[3] = {0, 0, 1}, uv[2];
      float weight[4];
      cgltf_uint joint[4] = {0, 0, 0, 0};
      memcpy(position, positions + i * 3, sizeof(position));
      if (normals != nullptr) memcpy(normal, normals + i * 3, sizeof(normal));
      memcpy(uv, uvs + i * 2, sizeof(uv));
      memcpy(weight, weights + i * 4, sizeof(weight));
      cgltf_accessor_read_uint(parts.joints, at, joint, 4);
      // The bones of the vertex, and the mix of their rest matrices.
      float total = 0.0f;
      float rest[16] = {0};
      for (int k = 0; k < 4; ++k) {
        if (!(weight[k] > 0.0f)) {
          weight[k] = 0.0f;
          continue;
        }
        if (joint[k] >= static_cast<cgltf_uint>(joints) ||
            bone_of_joint[joint[k]] < 0) {
          if (weight[k] > 0.01f) {
            ok = Say(why, why_size, "A vertex of the file has a bone that "
                      "is not a bone of the game (%s). Use only the 22 bones "
                      "of the export",
                      joint[k] < static_cast<cgltf_uint>(joints)
                          ? PlainName(skin->joints[joint[k]]->name)
                          : "a bone that is not in the skin");
          }
          weight[k] = 0.0f;
          continue;
        }
        total += weight[k];
      }
      if (!ok) break;
      if (!(total > 1e-4f)) {
        ok = Say(why, why_size, "A vertex of the file has no bone. Give "
                  "each vertex a bone of the hand");
        break;
      }
      for (int k = 0; k < 4; ++k) {
        weight[k] /= total;
        out->weight[v * 4 + k] = weight[k];
        out->bone[v * 4 + k] = weight[k] > 0.0f
            ? static_cast<uint8_t>(bone_of_joint[joint[k]]) : 0;
        if (weight[k] > 0.0f) {
          for (int e = 0; e < 16; ++e) rest[e] += weight[k] * bind[joint[k]][e];
        }
      }
      if (turn_sign == 0.0f) turn_sign = Det4(rest) < 0.0f ? -1.0f : 1.0f;
      float world[3], world_normal[3];
      Point4(rest, position, world);
      Vector4(rest, normal, world_normal);
      hm::FitPoint(fit, world, out->base + v * 3);
      hm::FitNormal(fit, world_normal, out->base_normal + v * 3);
      out->uv[v * 2 + 0] = uv[0];
      out->uv[v * 2 + 1] = uv[1];
      // The shape keys: a step of the position, and of the normal when the
      // file has it.
      float* form[2] = {out->evil, out->good};
      float* form_normal[2] = {out->evil_normal, out->good_normal};
      for (int s = 0; s < 2; ++s) {
        if (form[s] == nullptr) continue;
        float step[3] = {0, 0, 0}, normal_step[3] = {0, 0, 0};
        if (steps[s] != nullptr) memcpy(step, steps[s] + i * 3, sizeof(step));
        if (normal_steps[s] != nullptr) {
          memcpy(normal_step, normal_steps[s] + i * 3, sizeof(normal_step));
        }
        float world_step[3], mesh_step[3];
        Vector4(rest, step, world_step);
        hm::FitStep(fit, world_step, mesh_step);
        const float moved[3] = {normal[0] + normal_step[0],
                                normal[1] + normal_step[1],
                                normal[2] + normal_step[2]};
        float moved_world[3];
        Vector4(rest, moved, moved_world);
        hm::FitNormal(fit, moved_world, form_normal[s] + v * 3);
        for (int k = 0; k < 3; ++k) {
          form[s][v * 3 + k] = out->base[v * 3 + k] + mesh_step[k];
        }
      }
    }
    free(positions);
    free(normals);
    free(uvs);
    free(weights);
    for (int s = 0; s < 2; ++s) {
      free(steps[s]);
      free(normal_steps[s]);
    }
    if (!ok) break;
    const cgltf_size corners = prim.indices != nullptr ? prim.indices->count
                                                       : parts.position->count;
    for (cgltf_size c = 0; c + 2 < corners; c += 3) {
      uint32_t index[3];
      for (int k = 0; k < 3; ++k) {
        const cgltf_size i = prim.indices != nullptr
            ? cgltf_accessor_read_index(prim.indices, c + k) : c + k;
        if (i >= parts.position->count) {
          ok = Say(why, why_size, "A triangle of the file uses a vertex "
                    "that is not there");
          break;
        }
        index[k] = static_cast<uint32_t>(v0) + static_cast<uint32_t>(i);
      }
      if (!ok) break;
      memcpy(out->index + t0 * 3, index, sizeof(index));
      ++t0;
    }
    v0 += count;
  }
  free(bind);
  if (!ok) {
    HandMeshFree(out);
    return false;
  }
  out->triangles = t0;
  // THE TURN OF A TRIANGLE. A glTF file has the outside of a triangle at
  // (v1 - v0) x (v2 - v0), as the stored mesh of the game does. A mirror on
  // the way to the frame of the mesh turns that, so two corners change
  // places.
  const bool mirror = fit.mirror != (turn_sign < 0.0f);
  if (mirror) {
    for (int t = 0; t < out->triangles; ++t) {
      const uint32_t keep = out->index[t * 3 + 1];
      out->index[t * 3 + 1] = out->index[t * 3 + 2];
      out->index[t * 3 + 2] = keep;
    }
  }
  // A file with no normals: the sum of the triangles at each vertex.
  if (any_normal_missing) {
    memset(out->base_normal, 0,
           static_cast<size_t>(out->vertices) * 3 * sizeof(float));
    for (int t = 0; t < out->triangles; ++t) {
      const uint32_t* index = out->index + t * 3;
      const float* a = out->base + index[0] * 3;
      const float* b = out->base + index[1] * 3;
      const float* c = out->base + index[2] * 3;
      const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
      const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
      float n[3];
      hx::Cross(e1, e2, n);
      for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < 3; ++i) out->base_normal[index[k] * 3 + i] += n[i];
      }
    }
    for (int v = 0; v < out->vertices; ++v) {
      hx::Normalize(out->base_normal + v * 3);
    }
    const size_t bytes =
        static_cast<size_t>(out->vertices) * 3 * sizeof(float);
    if (out->evil_normal != nullptr) {
      memcpy(out->evil_normal, out->base_normal, bytes);
    }
    if (out->good_normal != nullptr) {
      memcpy(out->good_normal, out->base_normal, bytes);
    }
  }
  Say(why, why_size, "The map from the file to the hand of the game: one "
       "unit of the file is %.1f mesh units, %s, and the worst bone joint "
       "(bone %d) is %.2f mm from its place on a hand of 19 cm. Shape keys: "
       "evil %s, good %s%s",
       fit.scale, mirror ? "with a mirror" : "with no mirror", fit.worst_bone,
       worst_mm, evil >= 0 ? "yes" : "no", good >= 0 ? "yes" : "no",
       any_normal_missing ? ". The file has no normals, so the mod made them"
                          : "");
  return true;
}

}  // namespace

bool HandModelFileLoad(const char* path, const hand_model::Skeleton& skeleton,
                       HandMesh* out, char* why, int why_size) {
  memset(out, 0, sizeof(*out));
  cgltf_options options;
  memset(&options, 0, sizeof(options));
  cgltf_data* data = nullptr;
  cgltf_result result = cgltf_parse_file(&options, path, &data);
  if (result != cgltf_result_success || data == nullptr) {
    return Say(why, why_size, "The file is not a glTF file that the mod can "
                "read (code %d). Export a glTF binary (.glb)",
                static_cast<int>(result));
  }
  result = cgltf_load_buffers(&options, data, path);
  if (result == cgltf_result_success) result = cgltf_validate(data);
  bool ok = false;
  if (result != cgltf_result_success) {
    Say(why, why_size, "The data of the file is not complete (code %d). "
         "Export a glTF binary (.glb) with all data in one file",
         static_cast<int>(result));
  } else {
    ok = Read(data, skeleton, out, why, why_size);
  }
  cgltf_free(data);
  return ok;
}

#else  // BWVR_HAVE_CGLTF

bool HandModelFileLoad(const char*, const hand_model::Skeleton&, HandMesh* out,
                       char* why, int why_size) {
  memset(out, 0, sizeof(*out));
  sprintf_s(why, static_cast<size_t>(why_size),
            "This build of the mod has no reader for the file: "
            "third_party/cgltf was not there at the build");
  return false;
}

#endif  // BWVR_HAVE_CGLTF
