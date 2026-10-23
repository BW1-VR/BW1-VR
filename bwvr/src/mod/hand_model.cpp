// The hand model: the game's own hand mesh on both of the player's hands,
// with the game's textures and the game's light. See docs/hand-model.md, "The
// game's hand on both real hands".
//
// WHY. The hand of Black & White is a large part of the charm of the game, so
// the mod draws both hands with the mesh of the game's own Hand.
//
// THE FILES. Nothing of the game is in the mod. At the first frame with a
// controller hand, this reads the three hand meshes from the game folder of
// the player: Data\CreatureMesh\Hand_Boned_Base2.l3d, Good2 and Evil2. Each
// has 141 vertices, 169 triangles, 22 bones and one texture of 256 by 256
// (docs/hand-model.md, "The files of the Hand"). A file that is missing or not as
// the mod knows it gives a log line with the reason, and the low poly hands
// stay.
//
// THE POSE. overlay.cpp gives a curl and a spread for each finger
// (hand_model_math.h, FingerPose), from the same poses and the same trigger
// and grip as the low poly hands. A curl bends the bones of a finger about
// their own x, by the angles of the game's own poses. The mesh is skinned on
// the CPU once a frame.
//
// THE PLACE. The tip of the index finger of the mesh, at the rest curl, is
// the point of the Hand (hand_model_math.h, MakePlacement). The right hand is
// the mirror image of the stored mesh, as in the game, and the left hand is
// the stored mesh.
//
// THE LOOK. The game mixes the base mesh and texture with the good or the
// evil ones by the player's alignment [V Morphable::MorphVertices 0x00618D10,
// Morphable::MorphTexture 0x00619500]. This does the same mix, in steps of
// 0.03 of the alignment as the game does [V Morphable::UpdateMorphing].
//
// THE LIGHT. The game lights a vertex of a boned mesh with
// min(1, 0.4 + 1.3 * max(0, n.l)) times the color of the object, where l
// goes to the light position 0x00EA9E90 [V fn_0084C3C0]. This reads that
// position and the color of the game's own Hand mesh (CHand+0x482C, +0x4C) in
// each frame, so the hands have the light of the game at each time of day.
//
// A HAND OF THE PLAYER. When bwvr-hand.glb is beside the mod, its mesh is the
// mesh of the hands (hand_model_file.cpp, docs/hand-model.md, "A hand that the
// player edited"). It keeps the bones and the textures of the game. A file
// that does not fit gives a log line with the reason, and the mesh of the
// game stays.
//
// THE DRAW. As the overlay: the game's Direct3D 7 device, vertices of the
// format 0x1C4 that are projected on the CPU with the camera of the eye, and
// each state put back after the draw. The texture is a surface of the mod.
// Stage 0 multiplies it by the lit color, and the alpha of the texture makes
// the wrist fade, as in the game. A triangle that faces away is not drawn.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/bw_addresses.h"
#include "debug_menu.h"
#include "hand_model.h"

extern void ModLog(const char* format, ...);

// hand_direct.cpp: the CHand, and whether the controller places it now.
void* HandDirectHand(bool* active);
// controllers.cpp: a pulse on the pointing hand, and the frame count that the
// script of the fake controller uses.
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();
// This file: the switches of the test keys (below the namespace).
void HandModelSwapChange(const char* by);
void HandModelLookChange(const char* by);

namespace {

namespace hm = hand_model;
namespace hx = hand_exact_math;

const uint32_t kFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR |
                      D3DFVF_TEX1;  // 0x1C4, as the game draws
struct Vertex {
  float x, y, z, rhw;
  uint32_t diffuse, specular;
  float u, v;
};
static_assert(sizeof(Vertex) == 32, "the game's vertex is 32 bytes");

// The light position of the game, 3 floats [V fn_0081E1F0, fn_0084C3C0].
const uintptr_t kLightPosition = 0x00EA9E90;
// CHand: the mesh object, and its color [V fn_00813BA0: +0x4C goes to the
// color that fn_0084C3C0 multiplies by the light].
const int kHandMesh = 0x482C;
const int kMeshColor = 0x4C;

const int kTexels = 256 * 256;
// The most that a mesh can have. The mod skins each vertex of both hands on
// the CPU in each frame, so the limit keeps that time small.
const int kMaxVertices = 20000;
const int kMaxTriangles = 20000;
// Direct3D 7 takes 65535 vertices in one draw at most. A large mesh is drawn
// in parts of this many vertices, a whole count of triangles.
const int kDrawVertices = 63000;
// The alignment must move this far before the look changes
// [V Morphable::UpdateMorphing 0x00618C40].
const float kLookStep = 0.03f;

struct Settings {
  bool on = true;          // hand_model
  bool file = true;        // hand_model_file
  // A test: the full path of a file to read in place of bwvr-hand.glb, so a
  // desktop run needs no file in the game folder.
  char file_path[MAX_PATH] = "";
  bool test_keys = false;
  float yaw = hx::kRestYawDeg;
  float pitch = hx::kRestPitchDeg;
  float roll = hx::kRestRollDeg;
  // Tests with no key press: the frames at which the look steps and at which
  // the model goes off and on.
  uint64_t look_frames[8] = {0};
  int look_count = 0;
  uint64_t swap_frames[8] = {0};
  int swap_count = 0;
};
Settings g_set;
bool g_tried = false;
bool g_ready = false;
uint64_t g_frame = 0;

// WHICH HANDS draw: the low poly hands of overlay.cpp, the mesh of the game,
// or the mesh of the file of the player. The test key goes through them.
const int kModeLowPoly = 0;
const int kModeGame = 1;
const int kModeFile = 2;
int g_mode = kModeGame;
const char* const kModeName[3] = {"THE LOW POLY HANDS", "THE MESH OF THE GAME",
                                  "THE MESH OF bwvr-hand.glb"};

hm::Skeleton g_skeleton;
hm::Placement g_place;
// The mesh in use. It has the pointers of the mesh of the game or of the
// mesh of the file, which own the memory.
HandMesh g_mesh = {};
HandMesh g_game_mesh = {};
HandMesh g_file_mesh = {};
uint16_t* g_texels[3] = {nullptr, nullptr, nullptr};  // base, good, evil

// THE LOOK of this frame.
float* g_now = nullptr;
float* g_now_normal = nullptr;
uint32_t* g_texture_now = nullptr;
bool g_have_look = false;
float g_look_alignment = 0.0f;
// 0 the alignment of the game, then evil, neutral and good.
int g_look_step = 0;
const char* const kLookName[4] = {"the alignment of the player", "evil",
                                  "neutral", "good"};
int g_look_logs = 0;
const int kMaxLookLogs = 40;

// THE HANDS of this frame, in the world.
struct Built {
  bool valid = false;
  float* world = nullptr;    // 3 floats a vertex
  uint32_t* color = nullptr;
  float tip[3];
  float mirror = 1.0f;       // the sign of the map from the mesh to the world
};
Built g_built[2];
// The opacity of each hand (HandModelSetOpacity). 1 is the opacity of the
// texture alone.
float g_opacity[2] = {1.0f, 1.0f};
// A hand writes depth from this opacity on.
const float kDepthWriteOpacity = 0.9f;
float* g_camera = nullptr;   // 3 floats a vertex, for the eye pass
Vertex* g_vertices = nullptr;
int g_vertex_room = 0;

// THE TEXTURE on the game's device.
IDirectDrawSurface7* g_texture = nullptr;
IDirect3DDevice7* g_texture_device = nullptr;
bool g_texture_dirty = true;
int g_texture_bits = 0;

// The light and the color of the game, as read in this frame.
float g_light[3] = {0.0f, 1.0f, 0.0f};
bool g_light_is_position = false;
uint32_t g_object_color = 0xFFFFFFFFu;
uint32_t g_logged_color = 0;
int g_color_logs = 0;
bool g_said_light = false;

// For the report and the cost.
LARGE_INTEGER g_frequency = {};
uint64_t g_draws = 0;
uint64_t g_triangles_drawn = 0;
uint64_t g_culled = 0;
uint64_t g_failed = 0;
int64_t g_build_ticks = 0;
int64_t g_draw_ticks = 0;
uint64_t g_cost_frames = 0;
bool g_said_first = false;
bool g_said_failed = false;
bool g_said_unread = false;
bool g_key_swap_down = false;
bool g_key_look_down = false;

int64_t Now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

double Ms(int64_t ticks) {
  if (g_frequency.QuadPart == 0) return 0.0;
  return static_cast<double>(ticks) * 1000.0 /
         static_cast<double>(g_frequency.QuadPart);
}

// The folder of runblack.exe, with the last backslash.
bool GameFolder(char* path, size_t size) {
  const DWORD length = GetModuleFileNameA(nullptr, path,
                                          static_cast<DWORD>(size));
  if (length == 0 || length >= size) return false;
  char* slash = strrchr(path, '\\');
  if (slash == nullptr) return false;
  slash[1] = 0;
  return true;
}

float IniFloat(const char* path, const char* key, float fallback) {
  char text[32];
  GetPrivateProfileStringA("bwvr", key, "", text, sizeof(text), path);
  if (text[0] == 0) return fallback;
  char* end = nullptr;
  const double value = strtod(text, &end);
  if (end == text || !(value > -360.0 && value < 360.0)) return fallback;
  return static_cast<float>(value);
}

// A list of frame numbers, separated by commas or spaces.
int IniFrames(const char* path, const char* key, uint64_t* out, int most) {
  char text[128];
  GetPrivateProfileStringA("bwvr", key, "", text, sizeof(text), path);
  int count = 0;
  const char* p = text;
  while (*p != 0 && count < most) {
    while (*p == ' ' || *p == ',') ++p;
    if (*p == 0) break;
    char* end = nullptr;
    const unsigned long value = strtoul(p, &end, 10);
    if (end == p) break;
    if (value > 0) out[count++] = value;
    p = end;
  }
  return count;
}

void LoadSettings() {
  char path[MAX_PATH];
  if (!GameFolder(path, sizeof(path)) ||
      strlen(path) + 9 >= sizeof(path)) {
    return;
  }
  strcat_s(path, sizeof(path), "bwvr.ini");
  g_set.on = GetPrivateProfileIntA("bwvr", "hand_model", 1, path) != 0;
  g_set.file = GetPrivateProfileIntA("bwvr", "hand_model_file", 1, path) != 0;
  GetPrivateProfileStringA("bwvr", "hand_model_file_path", "", g_set.file_path,
                           sizeof(g_set.file_path), path);
  g_set.test_keys = GetPrivateProfileIntA("bwvr", "test_keys", 0, path) != 0;
  g_set.yaw = IniFloat(path, "hand_mesh_yaw_deg", hx::kRestYawDeg);
  g_set.pitch = IniFloat(path, "hand_mesh_pitch_deg", hx::kRestPitchDeg);
  g_set.roll = IniFloat(path, "hand_mesh_roll_deg", hx::kRestRollDeg);
  g_set.look_count = IniFrames(path, "hand_model_test_look",
                               g_set.look_frames, 8);
  g_set.swap_count = IniFrames(path, "hand_model_test_swap",
                               g_set.swap_frames, 8);
}

// --- the L3D file ------------------------------------------------------------

// One hand file, as pointers into its bytes. See docs/hand-model.md, "The files of
// the Hand".
struct L3D {
  uint8_t* data = nullptr;
  uint32_t size = 0;
  uint32_t vertices = 0, triangles = 0, bones = 0, groups = 0;
  const float* vertex = nullptr;     // 8 floats a vertex
  const uint16_t* index = nullptr;   // 3 a triangle
  const uint16_t* group = nullptr;   // a count and a bone
  const uint8_t* bone = nullptr;     // 60 bytes a bone
  const uint16_t* texels = nullptr;  // 256 by 256
};

void FreeL3D(L3D* file) {
  free(file->data);
  *file = L3D();
}

bool Fits(const L3D& file, uint32_t offset, uint64_t bytes) {
  return offset <= file.size && bytes <= file.size - offset;
}

uint32_t Word(const L3D& file, uint32_t offset) {
  uint32_t value = 0;
  memcpy(&value, file.data + offset, sizeof(value));
  return value;
}

// Reads a hand file and checks each offset and each count. False with the
// reason when the file is not as the mod knows it.
bool ReadL3D(const char* path, L3D* out, char* why, int why_size) {
  *out = L3D();
  FILE* handle = nullptr;
  if (fopen_s(&handle, path, "rb") != 0 || handle == nullptr) {
    sprintf_s(why, why_size, "the file is not there");
    return false;
  }
  fseek(handle, 0, SEEK_END);
  const long size = ftell(handle);
  fseek(handle, 0, SEEK_SET);
  if (size < 0x100 || size > 8 * 1024 * 1024) {
    fclose(handle);
    sprintf_s(why, why_size, "the file has %ld bytes", size);
    return false;
  }
  out->data = static_cast<uint8_t*>(malloc(static_cast<size_t>(size)));
  out->size = static_cast<uint32_t>(size);
  const size_t read = out->data != nullptr
                          ? fread(out->data, 1, static_cast<size_t>(size),
                                  handle)
                          : 0;
  fclose(handle);
  const L3D& f = *out;
  if (read != static_cast<size_t>(size) || memcmp(f.data, "L3D0", 4) != 0) {
    sprintf_s(why, why_size, "the file does not start with L3D0");
    return false;
  }
  // The header: the count of submeshes and their table, then after 32 bytes
  // of 0 the count of textures and their table.
  const uint32_t submeshes = Word(f, 0x0C), submesh_table = Word(f, 0x10);
  const uint32_t skins = Word(f, 0x38), skin_table = Word(f, 0x3C);
  if (submeshes != 1 || skins < 1 || !Fits(f, submesh_table, 4) ||
      !Fits(f, skin_table, 4)) {
    sprintf_s(why, why_size, "the file has %u submeshes and %u textures",
              submeshes, skins);
    return false;
  }
  const uint32_t submesh = Word(f, submesh_table);
  if (!Fits(f, submesh, 20)) {
    sprintf_s(why, why_size, "the submesh is not in the file");
    return false;
  }
  const uint32_t primitives = Word(f, submesh + 4);
  const uint32_t primitive_table = Word(f, submesh + 8);
  out->bones = Word(f, submesh + 12);
  const uint32_t bone_offset = Word(f, submesh + 16);
  if (primitives != 1 || !Fits(f, primitive_table, 4) ||
      out->bones != static_cast<uint32_t>(hm::kBones) ||
      !Fits(f, bone_offset, 60ull * out->bones)) {
    sprintf_s(why, why_size, "the file has %u primitives and %u bones",
              primitives, out->bones);
    return false;
  }
  const uint32_t primitive = Word(f, primitive_table);
  if (!Fits(f, primitive, 48)) {
    sprintf_s(why, why_size, "the primitive is not in the file");
    return false;
  }
  out->vertices = Word(f, primitive + 16);
  const uint32_t vertex_offset = Word(f, primitive + 20);
  out->triangles = Word(f, primitive + 24);
  const uint32_t triangle_offset = Word(f, primitive + 28);
  out->groups = Word(f, primitive + 32);
  const uint32_t group_offset = Word(f, primitive + 36);
  if (out->vertices < 3 || out->vertices > 65535 || out->triangles < 1 ||
      out->triangles > static_cast<uint32_t>(kMaxTriangles) ||
      !Fits(f, vertex_offset, 32ull * out->vertices) ||
      !Fits(f, triangle_offset, 6ull * out->triangles) ||
      !Fits(f, group_offset, 4ull * out->groups)) {
    sprintf_s(why, why_size, "the file has %u vertices, %u triangles and %u "
              "bone groups", out->vertices, out->triangles, out->groups);
    return false;
  }
  const uint32_t skin = Word(f, skin_table);
  if (!Fits(f, skin, 4 + 2ull * kTexels)) {
    sprintf_s(why, why_size, "the texture is not in the file");
    return false;
  }
  out->vertex = reinterpret_cast<const float*>(f.data + vertex_offset);
  out->index = reinterpret_cast<const uint16_t*>(f.data + triangle_offset);
  out->group = reinterpret_cast<const uint16_t*>(f.data + group_offset);
  out->bone = f.data + bone_offset;
  out->texels = reinterpret_cast<const uint16_t*>(f.data + skin + 4);
  // The groups must cover the vertices, each with a bone of the file, and
  // each triangle must use vertices of the file.
  uint32_t covered = 0;
  for (uint32_t g = 0; g < out->groups; ++g) {
    covered += out->group[g * 2];
    if (out->group[g * 2 + 1] >= out->bones) covered = 0xFFFFFFFFu;
  }
  if (covered != out->vertices) {
    sprintf_s(why, why_size, "the bone groups do not cover the vertices");
    return false;
  }
  for (uint32_t i = 0; i < out->triangles * 3; ++i) {
    if (out->index[i] >= out->vertices) {
      sprintf_s(why, why_size, "a triangle uses a vertex that is not there");
      return false;
    }
  }
  return true;
}

// The bones of a file: the parent and the matrix in the frame of the parent.
void ReadBones(const L3D& file, hm::Skeleton* out) {
  for (int b = 0; b < hm::kBones; ++b) {
    const uint8_t* at = file.bone + 60 * b;
    int32_t parent = -1;
    memcpy(&parent, at, sizeof(parent));
    out->parent[b] = parent;
    memcpy(out->local[b], at + 12, 12 * sizeof(float));
  }
}

// The vertices of a file in the frame of the mesh, with the bones at rest.
void RestMesh(const L3D& file, const hm::Skeleton& skeleton, float* positions,
              float* normals) {
  uint32_t v = 0;
  for (uint32_t g = 0; g < file.groups; ++g) {
    const int bone = file.group[g * 2 + 1];
    for (uint32_t i = 0; i < file.group[g * 2]; ++i, ++v) {
      const float* in = file.vertex + v * 8;
      hx::ModelToWorld(skeleton.rest[bone], in, positions + v * 3);
      float n[3];
      hm::SkinVector(skeleton.rest[bone], in + 5, n);
      hx::Normalize(n);
      memcpy(normals + v * 3, n, sizeof(n));
    }
  }
}

}  // namespace

void HandMeshFree(HandMesh* mesh) {
  free(mesh->base);
  free(mesh->base_normal);
  free(mesh->evil);
  free(mesh->evil_normal);
  free(mesh->good);
  free(mesh->good_normal);
  free(mesh->uv);
  free(mesh->index);
  free(mesh->bone);
  free(mesh->weight);
  memset(mesh, 0, sizeof(*mesh));
}

bool HandMeshAllocate(HandMesh* mesh, int vertices, int triangles, bool evil,
                      bool good) {
  memset(mesh, 0, sizeof(*mesh));
  if (vertices < 3 || vertices > kMaxVertices || triangles < 1 ||
      triangles > kMaxTriangles) {
    return false;
  }
  const size_t v3 = static_cast<size_t>(vertices) * 3 * sizeof(float);
  mesh->vertices = vertices;
  mesh->triangles = triangles;
  mesh->base = static_cast<float*>(calloc(1, v3));
  mesh->base_normal = static_cast<float*>(calloc(1, v3));
  if (evil) {
    mesh->evil = static_cast<float*>(calloc(1, v3));
    mesh->evil_normal = static_cast<float*>(calloc(1, v3));
  }
  if (good) {
    mesh->good = static_cast<float*>(calloc(1, v3));
    mesh->good_normal = static_cast<float*>(calloc(1, v3));
  }
  mesh->uv = static_cast<float*>(
      calloc(1, static_cast<size_t>(vertices) * 2 * sizeof(float)));
  mesh->index = static_cast<uint32_t*>(
      calloc(1, static_cast<size_t>(triangles) * 3 * sizeof(uint32_t)));
  mesh->bone = static_cast<uint8_t*>(
      calloc(1, static_cast<size_t>(vertices) * 4));
  mesh->weight = static_cast<float*>(
      calloc(1, static_cast<size_t>(vertices) * 4 * sizeof(float)));
  const bool ok = mesh->base && mesh->base_normal && mesh->uv && mesh->index &&
                  mesh->bone && mesh->weight &&
                  (!evil || (mesh->evil && mesh->evil_normal)) &&
                  (!good || (mesh->good && mesh->good_normal));
  if (!ok) HandMeshFree(mesh);
  return ok;
}

namespace {

// The buffers that depend on the count of vertices and triangles of the mesh
// in use.
bool AllocateFrameBuffers() {
  const int vertices = g_game_mesh.vertices > g_file_mesh.vertices
                           ? g_game_mesh.vertices
                           : g_file_mesh.vertices;
  const int triangles = g_game_mesh.triangles > g_file_mesh.triangles
                            ? g_game_mesh.triangles
                            : g_file_mesh.triangles;
  const size_t v3 = static_cast<size_t>(vertices) * 3 * sizeof(float);
  free(g_now);
  free(g_now_normal);
  free(g_camera);
  free(g_vertices);
  g_now = static_cast<float*>(malloc(v3));
  g_now_normal = static_cast<float*>(malloc(v3));
  g_camera = static_cast<float*>(malloc(v3));
  bool ok = g_now && g_now_normal && g_camera;
  for (int h = 0; h < 2; ++h) {
    free(g_built[h].world);
    free(g_built[h].color);
    g_built[h].world = static_cast<float*>(malloc(v3));
    g_built[h].color = static_cast<uint32_t*>(
        malloc(static_cast<size_t>(vertices) * sizeof(uint32_t)));
    g_built[h].valid = false;
    ok = ok && g_built[h].world && g_built[h].color;
  }
  // The cut at the near plane makes at most two triangles of one.
  g_vertex_room = triangles * 2 * 3 * 2;
  g_vertices = static_cast<Vertex*>(
      malloc(static_cast<size_t>(g_vertex_room) * sizeof(Vertex)));
  g_have_look = false;
  return ok && g_vertices != nullptr;
}

// Reads the three hand files of the game and makes the mesh, the bones and
// the textures. False with the reason.
bool LoadGameMesh(char* why, int why_size) {
  char folder[MAX_PATH];
  if (!GameFolder(folder, sizeof(folder))) {
    sprintf_s(why, why_size, "the game folder is not known");
    return false;
  }
  const char* const names[3] = {"Hand_Boned_Base2.l3d", "Hand_Boned_Good2.l3d",
                                "Hand_Boned_Evil2.l3d"};
  L3D files[3];
  bool ok = true;
  for (int i = 0; i < 3 && ok; ++i) {
    char path[MAX_PATH], reason[160];
    sprintf_s(path, sizeof(path), "%sData\\CreatureMesh\\%s", folder,
              names[i]);
    if (!ReadL3D(path, &files[i], reason, sizeof(reason))) {
      sprintf_s(why, why_size, "Data\\CreatureMesh\\%s: %s", names[i], reason);
      ok = false;
    } else if (i > 0 && (files[i].vertices != files[0].vertices ||
                         files[i].triangles != files[0].triangles ||
                         files[i].groups != files[0].groups ||
                         memcmp(files[i].index, files[0].index,
                                6ull * files[0].triangles) != 0 ||
                         memcmp(files[i].group, files[0].group,
                                4ull * files[0].groups) != 0)) {
      sprintf_s(why, why_size, "Data\\CreatureMesh\\%s does not have the "
                "vertices, the triangles and the bone groups of the base file",
                names[i]);
      ok = false;
    }
  }
  if (ok) {
    ReadBones(files[0], &g_skeleton);
    if (!hm::PrepareSkeleton(&g_skeleton)) {
      sprintf_s(why, why_size, "the bones of the base file are not a tree");
      ok = false;
    }
  }
  const L3D& base = files[0];
  // The good file has the vertices of the base file in the game as shipped.
  // Then the good form needs no memory.
  const bool good_differs =
      ok && memcmp(files[1].vertex, base.vertex, 32ull * base.vertices) != 0;
  if (ok && !HandMeshAllocate(&g_game_mesh, static_cast<int>(base.vertices),
                              static_cast<int>(base.triangles), true,
                              good_differs)) {
    sprintf_s(why, why_size, "no memory for the mesh");
    ok = false;
  }
  if (ok) {
    RestMesh(base, g_skeleton, g_game_mesh.base, g_game_mesh.base_normal);
    RestMesh(files[2], g_skeleton, g_game_mesh.evil, g_game_mesh.evil_normal);
    if (good_differs) {
      RestMesh(files[1], g_skeleton, g_game_mesh.good, g_game_mesh.good_normal);
    }
    uint32_t v = 0;
    float bone_end[hm::kBones];
    for (int b = 0; b < hm::kBones; ++b) bone_end[b] = 0.0f;
    for (uint32_t g = 0; g < base.groups; ++g) {
      const uint16_t bone = base.group[g * 2 + 1];
      for (uint32_t i = 0; i < base.group[g * 2]; ++i, ++v) {
        const float* in = base.vertex + v * 8;
        g_game_mesh.uv[v * 2 + 0] = in[3];
        g_game_mesh.uv[v * 2 + 1] = in[4];
        g_game_mesh.bone[v * 4] = static_cast<uint8_t>(bone);
        g_game_mesh.weight[v * 4] = 1.0f;
        // The length of a bone: the largest z of its own vertices.
        if (in[2] > bone_end[bone]) bone_end[bone] = in[2];
      }
    }
    for (uint32_t i = 0; i < base.triangles * 3; ++i) {
      g_game_mesh.index[i] = base.index[i];
    }
    for (int f = 0; f < hm::kFingers; ++f) {
      int last = -1;
      for (int j = 0; j < hm::kChain; ++j) {
        if (hm::kFingerBones[f][j] >= 0) last = hm::kFingerBones[f][j];
      }
      g_skeleton.tip[f] = bone_end[last];
    }
    for (int i = 0; i < 3; ++i) {
      g_texels[i] = static_cast<uint16_t*>(malloc(kTexels * sizeof(uint16_t)));
      if (g_texels[i] == nullptr) {
        sprintf_s(why, why_size, "no memory for the textures");
        ok = false;
        break;
      }
      memcpy(g_texels[i], files[i].texels, kTexels * sizeof(uint16_t));
    }
  }
  for (int i = 0; i < 3; ++i) FreeL3D(&files[i]);
  return ok;
}

void Load() {
  if (g_tried) return;
  g_tried = true;
  QueryPerformanceFrequency(&g_frequency);
  LoadSettings();
  if (!g_set.on) {
    ModLog("hand model   : hand_model=0, the mod draws the low poly hands");
    return;
  }
  char why[256];
  if (!LoadGameMesh(why, sizeof(why))) {
    ModLog("hand model   : the hand of the game was not read, so the mod "
           "draws the low poly hands. %s", why);
    return;
  }
  g_mesh = g_game_mesh;
  g_mode = kModeGame;
  // A HAND OF THE PLAYER: bwvr-hand.glb beside the mod.
  char file_path[MAX_PATH];
  char file_note[512] = "";
  if (!g_set.file) {
    sprintf_s(file_note, sizeof(file_note), "hand_model_file=0, so the mod "
              "does not look for bwvr-hand.glb");
  } else if (!GameFolder(file_path, sizeof(file_path)) ||
             strlen(file_path) + 14 >= sizeof(file_path)) {
    sprintf_s(file_note, sizeof(file_note), "the game folder is not known");
  } else {
    strcat_s(file_path, sizeof(file_path), "bwvr-hand.glb");
    if (g_set.file_path[0] != 0) {
      strcpy_s(file_path, sizeof(file_path), g_set.file_path);
      ModLog("hand model   : hand_model_file_path is set, so the file of the "
             "hand is %s", file_path);
    }
    if (GetFileAttributesA(file_path) == INVALID_FILE_ATTRIBUTES) {
      sprintf_s(file_note, sizeof(file_note), "there is no bwvr-hand.glb "
                "in the game folder, so the hands have the mesh of the game");
    } else {
      char reason[400];
      if (HandModelFileLoad(file_path, g_skeleton, &g_file_mesh, reason,
                            sizeof(reason))) {
        g_mesh = g_file_mesh;
        g_mode = kModeFile;
        sprintf_s(file_note, sizeof(file_note), "bwvr-hand.glb is the mesh "
                  "of the hands: %d vertices and %d triangles. %s",
                  g_file_mesh.vertices, g_file_mesh.triangles, reason);
      } else {
        sprintf_s(file_note, sizeof(file_note), "bwvr-hand.glb is NOT USED, "
                  "so the hands have the mesh of the game. %s", reason);
      }
    }
  }
  g_texture_now = static_cast<uint32_t*>(malloc(kTexels * sizeof(uint32_t)));
  if (g_texture_now == nullptr || !AllocateFrameBuffers()) {
    ModLog("hand model   : no memory for the hands, so the mod draws the low "
           "poly hands");
    return;
  }
  float rest_turn[9];
  hx::RestTurn(g_set.yaw, g_set.pitch, g_set.roll, rest_turn);
  hm::MakePlacement(g_skeleton, rest_turn, hx::kMeshLength, &g_place);
  // Where the mesh is in the hand frame, for the log: the wrist joint, the
  // knuckle of the index finger and the tip of the middle finger at rest.
  float wrist[3], knuckle[3];
  hm::MeshToHand(g_place, &g_skeleton.rest[0][9], wrist);
  hm::MeshToHand(g_place, &g_skeleton.rest[hm::kFingerBones[1][1]][9],
                 knuckle);
  // THE PALM. The code puts a held object, the leash and the palm search at
  // overlay_math::HoldCenter, on the palm of the low poly hand. This finds
  // the palm of the mesh on the line through that point along x: the
  // triangle of the rest mesh that the line goes through with the least x.
  float palm_code[3];
  overlay_math::HoldCenter(0.0f, palm_code);
  float palm_mesh = 1e9f;
  for (int t = 0; t < g_game_mesh.triangles; ++t) {
    float corner[3][3];
    for (int k = 0; k < 3; ++k) {
      const uint32_t vertex = g_game_mesh.index[t * 3 + k];
      hm::MeshToHand(g_place, g_game_mesh.base + vertex * 3, corner[k]);
    }
    // The point (y, z) of the line in the triangle, as shares u and v.
    const float e1y = corner[1][1] - corner[0][1];
    const float e1z = corner[1][2] - corner[0][2];
    const float e2y = corner[2][1] - corner[0][1];
    const float e2z = corner[2][2] - corner[0][2];
    const float det = e1y * e2z - e1z * e2y;
    if (fabsf(det) < 1e-12f) continue;
    const float py = palm_code[1] - corner[0][1];
    const float pz = palm_code[2] - corner[0][2];
    const float u = (py * e2z - pz * e2y) / det;
    const float v = (e1y * pz - e1z * py) / det;
    if (u < 0.0f || v < 0.0f || u + v > 1.0f) continue;
    const float x = corner[0][0] + u * (corner[1][0] - corner[0][0]) +
                    v * (corner[2][0] - corner[0][0]);
    if (x < palm_mesh) palm_mesh = x;
  }
  float measured = 0.0f;
  for (int v = 0; v < g_game_mesh.vertices; ++v) {
    if (g_game_mesh.bone[v * 4] != hm::kFingerBones[2][hm::kChain - 1]) {
      continue;
    }
    const float d[3] = {g_game_mesh.base[v * 3] - g_skeleton.rest[0][9],
                        g_game_mesh.base[v * 3 + 1] - g_skeleton.rest[0][10],
                        g_game_mesh.base[v * 3 + 2] - g_skeleton.rest[0][11]};
    if (hx::Length(d) > measured) measured = hx::Length(d);
  }
  g_ready = true;
  ModLog("hand model   : hand_model=1. The hand of the game was read from "
         "Data\\CreatureMesh: %d vertices, %d triangles, %d bones, 3 textures "
         "of 256 by 256. The wrist joint to the end of the middle finger is "
         "%.1f mesh units (the mod uses %.1f). The good form is %s",
         g_game_mesh.vertices, g_game_mesh.triangles, hm::kBones, measured,
         hx::kMeshLength,
         g_game_mesh.good != nullptr ? "its own mesh" : "the base mesh");
  ModLog("hand model   : %s", file_note);
  ModLog("hand model   : the tip of the index finger at the rest curl is the "
         "point of the Hand. In the hand frame the wrist joint is at %.3f "
         "%.3f %.3f (the low poly hand: %.3f %.3f %.3f) and the knuckle of "
         "the index finger at %.3f %.3f %.3f, in hand lengths",
         wrist[0], wrist[1], wrist[2], overlay_math::kPalmX,
         overlay_math::kKnuckleY + overlay_math::kPalmLength, 0.0f, knuckle[0],
         knuckle[1], knuckle[2]);
  ModLog("hand model   : the palm of the mesh is %.1f mm in front of the "
         "palm that the code uses for a held object and the leash, on a hand "
         "of 19 cm%s",
         palm_mesh < 1e8f ? (palm_code[0] - palm_mesh) * 190.0f : 0.0f,
         g_set.test_keys ? ". test_keys=1: Ctrl and the minus key of the "
                           "number pad go through the hands (the mesh of the "
                           "file, the mesh of the game, the low poly hands), "
                           "and Shift and that key step the look"
                         : "");
}

// THE LOOK for an alignment: the vertices, the normals and the texture.
void ApplyLook(float alignment) {
  hm::Look look;
  hm::LookOf(alignment, &look);
  const float* other = look.evil ? g_mesh.evil : g_mesh.good;
  const float* other_normal = look.evil ? g_mesh.evil_normal
                                        : g_mesh.good_normal;
  const int count = g_mesh.vertices * 3;
  if (other == nullptr || !(look.weight > 0.0f)) {
    memcpy(g_now, g_mesh.base, count * sizeof(float));
    memcpy(g_now_normal, g_mesh.base_normal, count * sizeof(float));
  } else {
    for (int i = 0; i < count; ++i) {
      g_now[i] = g_mesh.base[i] + (other[i] - g_mesh.base[i]) * look.weight;
      g_now_normal[i] = g_mesh.base_normal[i] +
                        (other_normal[i] - g_mesh.base_normal[i]) * look.weight;
    }
    for (int v = 0; v < g_mesh.vertices; ++v) {
      hx::Normalize(g_now_normal + v * 3);
    }
  }
  const uint16_t* base = g_texels[0];
  const uint16_t* mix = g_texels[look.evil ? 2 : 1];
  for (int i = 0; i < kTexels; ++i) {
    g_texture_now[i] = hm::Texel32(hm::MixTexel(base[i], mix[i], look.texture));
  }
  g_texture_dirty = true;
  g_have_look = true;
  g_look_alignment = alignment;
  if (g_look_logs < kMaxLookLogs) {
    ++g_look_logs;
    ModLog("hand model   : the look is for an alignment of %.2f (%s), frame "
           "%llu: the %s mesh with the weight %.2f, and the %s texture with "
           "the weight %d of 255", alignment, kLookName[g_look_step], g_frame,
           look.evil ? "evil" : "good", look.weight,
           look.evil ? "evil" : "good", look.texture);
  }
}

// The next hands: the mesh of the file when there is one, the mesh of the
// game, the low poly hands, and round again.
void SwapModel(const char* how) {
  const bool have_file = g_file_mesh.vertices > 0;
  if (g_mode == kModeFile) {
    g_mode = kModeGame;
  } else if (g_mode == kModeGame) {
    g_mode = kModeLowPoly;
  } else {
    g_mode = have_file ? kModeFile : kModeGame;
  }
  if (g_mode != kModeLowPoly) {
    g_mesh = g_mode == kModeFile ? g_file_mesh : g_game_mesh;
    g_have_look = false;
  }
  ControllersGesturePulse(0.5f, g_mode != kModeLowPoly ? 0.30f : 0.08f);
  ModLog("hand model   : %s: the hands are %s, frame %llu", how,
         kModeName[g_mode], g_frame);
}

void StepLook(const char* how) {
  g_look_step = (g_look_step + 1) % 4;
  g_have_look = false;
  ControllersGesturePulse(0.5f, 0.08f);
  ModLog("hand model   : %s: the look is now %s, frame %llu", how,
         kLookName[g_look_step], g_frame);
}

// AN A/B IN THE HEADSET. With test_keys=1, Ctrl and the minus key of the
// number pad go through the hands: the mesh of the file of the player, the
// mesh of the game and the low poly hands. A long pulse on the pointing
// controller means a mesh and a short pulse the low poly hands. Shift and
// that key step the look: the alignment of the player, evil,
// neutral, good. The keys are read once a frame, only while a window of the
// game is in front. The minus key alone is the key of hand_exact.cpp.
void PollKeys() {
  for (int i = 0; i < g_set.look_count; ++i) {
    if (g_set.look_frames[i] == g_frame) StepLook("hand_model_test_look");
  }
  for (int i = 0; i < g_set.swap_count; ++i) {
    if (g_set.swap_frames[i] == g_frame) SwapModel("hand_model_test_swap");
  }
  if (!g_set.test_keys) return;
  const bool minus = (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0;
  const bool control = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool swap = minus && control && !shift;
  const bool look = minus && shift && !control;
  const bool swap_pressed = swap && !g_key_swap_down;
  const bool look_pressed = look && !g_key_look_down;
  g_key_swap_down = swap;
  g_key_look_down = look;
  if (!swap_pressed && !look_pressed) return;
  HWND window = GetForegroundWindow();
  DWORD process = 0;
  if (window != nullptr) GetWindowThreadProcessId(window, &process);
  if (process != GetCurrentProcessId()) return;
  if (swap_pressed) HandModelSwapChange("Ctrl and the minus key");
  if (look_pressed) HandModelLookChange("Shift and the minus key");
}

// The light position and the color of the Hand of the game, for this frame.
void ReadGameLight() {
  const float* light = reinterpret_cast<const float*>(kLightPosition);
  const float size = hx::Length(light);
  g_light_is_position = size > 1.0f && size < 1e9f;
  if (g_light_is_position) {
    memcpy(g_light, light, sizeof(g_light));
  } else {
    g_light[0] = 0.0f;
    g_light[1] = 1.0f;
    g_light[2] = 0.0f;
  }
  g_object_color = 0xFFFFFFFFu;
  bool active = false;
  const uint8_t* hand = static_cast<const uint8_t*>(HandDirectHand(&active));
  if (hand != nullptr) {
    const uint8_t* mesh =
        *reinterpret_cast<const uint8_t* const*>(hand + kHandMesh);
    if (mesh != nullptr && !IsBadReadPtr(mesh + kMeshColor, 4)) {
      g_object_color = *reinterpret_cast<const uint32_t*>(mesh + kMeshColor);
      // The alpha of the texture makes the wrist fade. A color with no alpha
      // would hide the hands.
      if ((g_object_color >> 24) == 0) g_object_color |= 0xFF000000u;
    }
  }
  // The game changes the color of its Hand with the time of day, a little in
  // each frame at sunset. The log has the first color, and then each color
  // that is far from the last one in the log.
  int far_channel = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    const int now = static_cast<int>((g_object_color >> shift) & 0xFFu);
    const int then = static_cast<int>((g_logged_color >> shift) & 0xFFu);
    if (abs(now - then) > far_channel) far_channel = abs(now - then);
  }
  if (!g_said_light || (far_channel >= 32 && g_color_logs < 20)) {
    if (g_said_light) ++g_color_logs;
    g_said_light = true;
    g_logged_color = g_object_color;
    ModLog("hand model   : the light of the game is at %.0f %.0f %.0f (%s), "
           "and the color of the Hand of the game is 0x%08X, frame %llu. The "
           "hands take this light and this color",
           light[0], light[1], light[2],
           g_light_is_position ? "used" : "not usable, so the light is from "
                                          "above",
           g_object_color, g_frame);
  }
}

// --- the texture -------------------------------------------------------------

const GUID kIidDirectDraw7 = {0x15e65ec0, 0x3b9c, 0x11d2,
                              {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};

IDirectDrawSurface7* CreateTexture(IDirectDraw7* draw, int bits) {
  DDSURFACEDESC2 desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.dwSize = sizeof(desc);
  desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
  desc.dwWidth = 256;
  desc.dwHeight = 256;
  desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
  desc.ddsCaps.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
  desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
  desc.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
  desc.ddpfPixelFormat.dwRGBBitCount = static_cast<DWORD>(bits);
  if (bits == 32) {
    desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000u;
    desc.ddpfPixelFormat.dwRBitMask = 0x00FF0000u;
    desc.ddpfPixelFormat.dwGBitMask = 0x0000FF00u;
    desc.ddpfPixelFormat.dwBBitMask = 0x000000FFu;
  } else {
    desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0xF000u;
    desc.ddpfPixelFormat.dwRBitMask = 0x0F00u;
    desc.ddpfPixelFormat.dwGBitMask = 0x00F0u;
    desc.ddpfPixelFormat.dwBBitMask = 0x000Fu;
  }
  IDirectDrawSurface7* surface = nullptr;
  if (FAILED(draw->CreateSurface(&desc, &surface, nullptr))) return nullptr;
  return surface;
}

bool UploadTexture() {
  DDSURFACEDESC2 desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.dwSize = sizeof(desc);
  if (FAILED(g_texture->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_WRITEONLY,
                             nullptr)) ||
      desc.lpSurface == nullptr) {
    return false;
  }
  uint8_t* row = static_cast<uint8_t*>(desc.lpSurface);
  for (int y = 0; y < 256; ++y, row += desc.lPitch) {
    const uint32_t* in = g_texture_now + y * 256;
    if (g_texture_bits == 32) {
      memcpy(row, in, 256 * sizeof(uint32_t));
    } else {
      uint16_t* out = reinterpret_cast<uint16_t*>(row);
      for (int x = 0; x < 256; ++x) {
        const uint32_t c = in[x];
        out[x] = static_cast<uint16_t>(((c >> 28) & 15u) << 12 |
                                       ((c >> 20) & 15u) << 8 |
                                       ((c >> 12) & 15u) << 4 |
                                       ((c >> 4) & 15u));
      }
    }
  }
  g_texture->Unlock(nullptr);
  return true;
}

// The texture of the mod on the device of the game, with the look of this
// frame in it. False with one log line when the device does not give one.
bool EnsureTexture(IDirect3DDevice7* device) {
  if (g_texture != nullptr && g_texture_device != device) {
    // The game made a new device. The old surface belongs to a device that
    // is gone, and a release there can touch memory that is free. So the mod
    // lets the old surface go, and makes a new one.
    g_texture = nullptr;
  }
  if (g_texture != nullptr && g_texture->IsLost() == DDERR_SURFACELOST) {
    g_texture->Restore();
    g_texture_dirty = true;
  }
  if (g_texture == nullptr) {
    IDirect3D7* d3d = nullptr;
    IDirectDraw7* draw = nullptr;
    HRESULT result = device->GetDirect3D(&d3d);
    if (SUCCEEDED(result) && d3d != nullptr) {
      result = d3d->QueryInterface(kIidDirectDraw7,
                                   reinterpret_cast<void**>(&draw));
      d3d->Release();
    }
    if (FAILED(result) || draw == nullptr) {
      ModLog("hand model   : the device did not give its DirectDraw (0x%08X), "
             "so the mod draws the low poly hands",
             static_cast<unsigned>(result));
      return false;
    }
    g_texture_bits = 32;
    g_texture = CreateTexture(draw, 32);
    if (g_texture == nullptr) {
      g_texture_bits = 16;
      g_texture = CreateTexture(draw, 16);
    }
    draw->Release();
    if (g_texture == nullptr) {
      ModLog("hand model   : the device did not make a texture of 256 by 256 "
             "with alpha, so the mod draws the low poly hands");
      return false;
    }
    g_texture_device = device;
    g_texture_dirty = true;
    ModLog("hand model   : the texture of the hands is a surface of %d bits "
           "on the device of the game", g_texture_bits);
  }
  if (g_texture_dirty) {
    if (!UploadTexture()) {
      ModLog("hand model   : the texture of the hands did not lock, so the "
             "mod draws the low poly hands");
      return false;
    }
    g_texture_dirty = false;
  }
  return true;
}

// --- the states --------------------------------------------------------------

struct RenderSet {
  D3DRENDERSTATETYPE state;
  DWORD value;
};
// The hand writes depth, so its fingers hide its palm. The blend is the
// plain alpha blend, and a texel with almost no alpha writes nothing.
const RenderSet kRender[] = {
    {D3DRENDERSTATE_ZENABLE, D3DZB_TRUE},
    {D3DRENDERSTATE_ZWRITEENABLE, TRUE},
    {D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL},
    {D3DRENDERSTATE_ALPHABLENDENABLE, TRUE},
    {D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA},
    {D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA},
    {D3DRENDERSTATE_ALPHATESTENABLE, TRUE},
    {D3DRENDERSTATE_ALPHAREF, 8},
    {D3DRENDERSTATE_ALPHAFUNC, D3DCMP_GREATEREQUAL},
    {D3DRENDERSTATE_FOGENABLE, FALSE},
    {D3DRENDERSTATE_CULLMODE, D3DCULL_NONE},
    {D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD},
    {D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID},
    {D3DRENDERSTATE_STENCILENABLE, FALSE},
    {D3DRENDERSTATE_COLORKEYENABLE, FALSE},
    {D3DRENDERSTATE_SPECULARENABLE, FALSE},
    {D3DRENDERSTATE_TEXTUREPERSPECTIVE, TRUE},
};
const int kRenderCount = sizeof(kRender) / sizeof(kRender[0]);

struct StageSet {
  DWORD stage;
  D3DTEXTURESTAGESTATETYPE state;
  DWORD value;
};
// Stage 0 multiplies the texture by the lit color of the vertex, and the
// alpha of the texture by the alpha of the vertex. Stage 1 off turns off each
// stage after stage 0.
const StageSet kStage[] = {
    {0, D3DTSS_COLOROP, D3DTOP_MODULATE},
    {0, D3DTSS_COLORARG1, D3DTA_TEXTURE},
    {0, D3DTSS_COLORARG2, D3DTA_DIFFUSE},
    {0, D3DTSS_ALPHAOP, D3DTOP_MODULATE},
    {0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE},
    {0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE},
    {0, D3DTSS_MAGFILTER, D3DTFG_LINEAR},
    {0, D3DTSS_MINFILTER, D3DTFN_LINEAR},
    {0, D3DTSS_MIPFILTER, D3DTFP_NONE},
    {0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP},
    {0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP},
    {0, D3DTSS_TEXCOORDINDEX, 0},
    {0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE},
    {1, D3DTSS_COLOROP, D3DTOP_DISABLE},
    {1, D3DTSS_ALPHAOP, D3DTOP_DISABLE},
};
const int kStageCount = sizeof(kStage) / sizeof(kStage[0]);

DWORD g_render_was[kRenderCount];
bool g_render_changed[kRenderCount];
DWORD g_stage_was[kStageCount];
bool g_stage_changed[kStageCount];

// As overlay.cpp: read each state before the change, and keep what was read.
void SetStates(IDirect3DDevice7* device) {
  int unread = 0;
  for (int i = 0; i < kRenderCount; ++i) {
    g_render_changed[i] = false;
    DWORD was = 0;
    if (FAILED(device->GetRenderState(kRender[i].state, &was))) {
      ++unread;
      continue;
    }
    DWORD want = kRender[i].value;
    if (kRender[i].state == D3DRENDERSTATE_ZENABLE && was != D3DZB_FALSE) {
      want = was;
    }
    if (was == want) continue;
    if (SUCCEEDED(device->SetRenderState(kRender[i].state, want))) {
      g_render_was[i] = was;
      g_render_changed[i] = true;
    }
  }
  for (int i = 0; i < kStageCount; ++i) {
    g_stage_changed[i] = false;
    DWORD was = 0;
    if (FAILED(device->GetTextureStageState(kStage[i].stage, kStage[i].state,
                                            &was))) {
      ++unread;
      continue;
    }
    if (was == kStage[i].value) continue;
    if (SUCCEEDED(device->SetTextureStageState(kStage[i].stage,
                                               kStage[i].state,
                                               kStage[i].value))) {
      g_stage_was[i] = was;
      g_stage_changed[i] = true;
    }
  }
  if (unread > 0 && !g_said_unread) {
    g_said_unread = true;
    ModLog("hand model   : the device did not give %d states, so the hands "
           "leave them as they are", unread);
  }
}

// In the reverse order.
void PutStatesBack(IDirect3DDevice7* device) {
  for (int i = kStageCount - 1; i >= 0; --i) {
    if (!g_stage_changed[i]) continue;
    device->SetTextureStageState(kStage[i].stage, kStage[i].state,
                                 g_stage_was[i]);
    g_stage_changed[i] = false;
  }
  for (int i = kRenderCount - 1; i >= 0; --i) {
    if (!g_render_changed[i]) continue;
    device->SetRenderState(kRender[i].state, g_render_was[i]);
    g_render_changed[i] = false;
  }
}

// --- the draw of one eye -----------------------------------------------------

// A corner in camera space with its color and its texture point.
struct Corner {
  float c[3];
  float color[4];  // blue, green, red, alpha, 0 to 255
  float uv[2];
};

// The opacity of the hand that FillHand fills now (HandModelSetOpacity).
float g_fill_opacity = 1.0f;

void CornerOf(const Built& built, int v, Corner* out) {
  memcpy(out->c, g_camera + v * 3, sizeof(out->c));
  const uint32_t color = built.color[v];
  for (int k = 0; k < 4; ++k) {
    out->color[k] = static_cast<float>((color >> (8 * k)) & 0xFFu);
  }
  out->color[3] *= g_fill_opacity;
  out->uv[0] = g_mesh.uv[v * 2 + 0];
  out->uv[1] = g_mesh.uv[v * 2 + 1];
}

void Emit(const Corner& corner, float near_plane, float half_x, float half_y,
          Vertex* out) {
  out->x = (1.0f + corner.c[0] / corner.c[2]) * half_x;
  out->y = (1.0f - corner.c[1] / corner.c[2]) * half_y;
  out->rhw = near_plane / corner.c[2];
  out->z = 1.0f - out->rhw;
  uint32_t color = 0;
  for (int k = 0; k < 4; ++k) {
    float value = corner.color[k];
    if (!(value > 0.0f)) value = 0.0f;
    if (value > 255.0f) value = 255.0f;
    color |= static_cast<uint32_t>(value + 0.5f) << (8 * k);
  }
  out->diffuse = color;
  out->specular = 0xFF000000u;  // no fog, if the fog were on
  out->u = corner.uv[0];
  out->v = corner.uv[1];
}

// The part of a triangle in front of the near plane, as 3 or 4 corners. A cut
// point takes the color and the texture point at the same share of the edge.
int ClipNear(const Corner in[3], float near_plane, Corner out[4]) {
  int n = 0;
  for (int i = 0; i < 3; ++i) {
    const int j = (i + 1) % 3;
    const bool i_in = in[i].c[2] >= near_plane;
    const bool j_in = in[j].c[2] >= near_plane;
    if (i_in) out[n++] = in[i];
    if (i_in != j_in) {
      const float t = (near_plane - in[i].c[2]) / (in[j].c[2] - in[i].c[2]);
      Corner& cut = out[n++];
      for (int k = 0; k < 3; ++k) {
        cut.c[k] = in[i].c[k] + t * (in[j].c[k] - in[i].c[k]);
      }
      cut.c[2] = near_plane;
      for (int k = 0; k < 4; ++k) {
        cut.color[k] = in[i].color[k] + t * (in[j].color[k] - in[i].color[k]);
      }
      for (int k = 0; k < 2; ++k) {
        cut.uv[k] = in[i].uv[k] + t * (in[j].uv[k] - in[i].uv[k]);
      }
    }
  }
  return n;
}

bool OffScreen(const Vertex* v, float half_x, float half_y) {
  bool left = true, right = true, top = true, bottom = true;
  for (int i = 0; i < 3; ++i) {
    left = left && v[i].x < 0.0f;
    right = right && v[i].x > 2.0f * half_x;
    top = top && v[i].y < 0.0f;
    bottom = bottom && v[i].y > 2.0f * half_y;
  }
  return left || right || top || bottom;
}

// The vertices of one hand for one eye, after the ones that are there.
int FillHand(const Built& built, const float camera[12], float near_plane,
             float half_x, float half_y, int n) {
  for (int v = 0; v < g_mesh.vertices; ++v) {
    overlay_math::ToCamera(camera, built.world + v * 3, g_camera + v * 3);
  }
  // A mirror in the camera matrix turns the triangles once more.
  const float camera3[9] = {camera[0], camera[1], camera[2], camera[3],
                            camera[4], camera[5], camera[6], camera[7],
                            camera[8]};
  const float mirror = hm::Det3(camera3) < 0.0f ? -built.mirror : built.mirror;
  for (int t = 0; t < g_mesh.triangles; ++t) {
    const uint32_t* index = g_mesh.index + t * 3;
    const float* c0 = g_camera + index[0] * 3;
    const float* c1 = g_camera + index[1] * 3;
    const float* c2 = g_camera + index[2] * 3;
    const int behind = (c0[2] < near_plane ? 1 : 0) +
                       (c1[2] < near_plane ? 1 : 0) +
                       (c2[2] < near_plane ? 1 : 0);
    if (behind == 3) continue;
    if (!hm::FacesCamera(c0, c1, c2, mirror)) {
      ++g_culled;
      continue;
    }
    Corner in[3], cut[4];
    for (int k = 0; k < 3; ++k) {
      CornerOf(built, static_cast<int>(index[k]), &in[k]);
    }
    const int corners = behind == 0 ? 3 : ClipNear(in, near_plane, cut);
    const Corner* use = behind == 0 ? in : cut;
    if (corners < 3 || n + (corners - 2) * 3 > g_vertex_room) continue;
    for (int piece = 0; piece + 2 < corners; ++piece) {
      Vertex* out = g_vertices + n;
      Emit(use[0], near_plane, half_x, half_y, &out[0]);
      Emit(use[piece + 1], near_plane, half_x, half_y, &out[1]);
      Emit(use[piece + 2], near_plane, half_x, half_y, &out[2]);
      if (!OffScreen(out, half_x, half_y)) n += 3;
    }
  }
  return n;
}

}  // namespace

// THE SWITCHES OF THE TEST KEYS (debug_menu.h). Ctrl and the minus key of the
// number pad: the next hands. The value is 0 for the low poly hands, 1 for
// the mesh of the game and 2 for the mesh of the file. Without the mesh of
// the game nothing changes.
void HandModelSwapChange(const char* by) {
  Load();
  if (!g_ready) return;
  SwapModel(by);
}
int HandModelSwapValue() {
  Load();
  return g_ready ? g_mode : kDebugCannot;
}

// Shift and the minus key of the number pad: the next look. The value is 0
// for the alignment of the player, then 1 evil, 2 neutral and 3 good. With
// the low poly hands the look shows nothing.
void HandModelLookChange(const char* by) {
  Load();
  if (!g_ready) return;
  StepLook(by);
}
int HandModelLookValue() {
  Load();
  return g_ready && g_mode != kModeLowPoly ? g_look_step : kDebugCannot;
}

bool HandModelActive() {
  Load();
  return g_ready && g_mode != kModeLowPoly;
}

void HandModelFrame(uint64_t, bool have_alignment, float alignment) {
  Load();
  // The frame of the controllers, so the test frames of the ini file and the
  // log lines agree with the script of the fake controller.
  g_frame = ControllersFrameCount();
  for (int h = 0; h < 2; ++h) g_built[h].valid = false;
  if (!g_ready) return;
  PollKeys();
  if (g_mode == kModeLowPoly) return;
  const float step[4] = {have_alignment ? alignment : 0.0f, -1.0f, 0.0f, 1.0f};
  const float want = step[g_look_step];
  if (!g_have_look || fabsf(want - g_look_alignment) >= kLookStep) {
    ApplyLook(want);
  }
  ReadGameLight();
}

void HandModelHoldRange(float radius, overlay_math::PoseRange* out) {
  hm::MeshHoldRange(g_skeleton, g_place, radius, out);
}

bool HandModelBuild(int hand, const float position[3], const float rotation[9],
                    float length, bool left,
                    const hm::FingerPose& pose) {
  if (hand < 0 || hand > 1 || !g_ready || g_mode == kModeLowPoly ||
      !g_have_look) {
    return false;
  }
  const int64_t start = Now();
  Built& built = g_built[hand];
  float world[hm::kBones][12], skin[hm::kBones][12];
  hm::PoseBones(g_skeleton, pose, world);
  hm::SkinMatrices(g_skeleton, world, skin);
  // For each bone: world = m * p + o, with p a point of the rest mesh.
  float m[hm::kBones][9], o[hm::kBones][3];
  hm::WorldMaps(g_place, position, rotation, length, left, skin, m, o,
                &built.mirror);
  // The direction to the light of the game, from the hand.
  float light[3] = {g_light[0], g_light[1], g_light[2]};
  if (g_light_is_position) {
    for (int i = 0; i < 3; ++i) light[i] -= position[i];
  }
  if (!hx::Normalize(light)) {
    light[0] = 0.0f;
    light[1] = 1.0f;
    light[2] = 0.0f;
  }
  const uint32_t red = (g_object_color >> 16) & 0xFFu;
  const uint32_t green = (g_object_color >> 8) & 0xFFu;
  const uint32_t blue = g_object_color & 0xFFu;
  const uint32_t alpha = g_object_color & 0xFF000000u;
  for (int v = 0; v < g_mesh.vertices; ++v) {
    const float* p = g_now + v * 3;
    const float* n = g_now_normal + v * 3;
    float point[3] = {0.0f, 0.0f, 0.0f}, normal[3] = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < 4; ++k) {
      const float w = g_mesh.weight[v * 4 + k];
      if (!(w > 0.0f)) continue;
      const int b = g_mesh.bone[v * 4 + k];
      for (int r = 0; r < 3; ++r) {
        point[r] += w * (m[b][r * 3 + 0] * p[0] + m[b][r * 3 + 1] * p[1] +
                         m[b][r * 3 + 2] * p[2] + o[b][r]);
        normal[r] += w * (m[b][r * 3 + 0] * n[0] + m[b][r * 3 + 1] * n[1] +
                          m[b][r * 3 + 2] * n[2]);
      }
    }
    memcpy(built.world + v * 3, point, sizeof(point));
    hx::Normalize(normal);
    // The light of the game, as an integer of 0 to 255 times each channel
    // [V 0x0084C677 to 0x0084C6B6].
    const uint32_t level = static_cast<uint32_t>(
        hm::GameLightLevel(hx::Dot(normal, light)) * 255.0f);
    built.color[v] = alpha | ((red * level) >> 8) << 16 |
                     ((green * level) >> 8) << 8 | ((blue * level) >> 8);
  }
  // The tip of the index finger: the origin of the rest mesh, on its last bone.
  memcpy(built.tip, o[hm::kFingerBones[1][hm::kChain - 1]], sizeof(built.tip));
  built.valid = true;
  g_build_ticks += Now() - start;
  return true;
}

bool HandModelFingerJoints(int finger, float curl, float spread,
                           float joints[overlay_math::kJoints + 1][3]) {
  if (finger < 0 || finger >= hm::kFingers || !HandModelActive()) return false;
  hm::MeshFingerJointsSpread(g_skeleton, g_place, finger, curl, spread, joints);
  return true;
}

bool HandModelIndexTip(int hand, float out[3]) {
  if (hand < 0 || hand > 1 || !g_built[hand].valid) return false;
  memcpy(out, g_built[hand].tip, sizeof(g_built[hand].tip));
  return true;
}

bool HandModelBuilt() { return g_built[0].valid || g_built[1].valid; }

void HandModelSetOpacity(int hand, float opacity) {
  if (hand < 0 || hand > 1) return;
  g_opacity[hand] = opacity < 0.0f ? 0.0f : opacity > 1.0f ? 1.0f : opacity;
}

int HandModelDraw(IDirect3DDevice7* device, const float camera[12],
                  float near_plane, float half_x, float half_y) {
  if (!HandModelBuilt() || device == nullptr) return 0;
  const int64_t start = Now();
  if (!EnsureTexture(device)) {
    // One log line was written. The low poly hands draw from the next frame.
    g_ready = false;
    return -1;
  }
  int n = 0;
  int end[2] = {0, 0};  // the end of the vertices of each hand
  for (int h = 0; h < 2; ++h) {
    if (g_built[h].valid) {
      g_fill_opacity = g_opacity[h];
      n = FillHand(g_built[h], camera, near_plane, half_x, half_y, n);
      g_fill_opacity = 1.0f;
    }
    end[h] = n;
  }
  if (n == 0) {
    g_draw_ticks += Now() - start;
    return 0;
  }
  SetStates(device);
  // The game keeps its own copy of the texture of each stage, and it does not
  // set a texture that its copy has already. So the texture that was there
  // goes back after the draw.
  IDirectDrawSurface7* before = nullptr;
  const bool have_before = SUCCEEDED(device->GetTexture(0, &before));
  device->SetTexture(0, g_texture);
  HRESULT result = S_OK;
  for (int h = 0; h < 2 && SUCCEEDED(result); ++h) {
    const int from = h == 0 ? 0 : end[0];
    if (end[h] <= from) continue;
    // A hand that fades writes no depth, so it does not hide what draws
    // behind it after it. SetStates turned the depth write on, so it goes
    // back on before PutStatesBack.
    const bool write = g_opacity[h] >= kDepthWriteOpacity;
    if (!write) device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
    for (int first = from; first < end[h] && SUCCEEDED(result);
         first += kDrawVertices) {
      const int count =
          end[h] - first < kDrawVertices ? end[h] - first : kDrawVertices;
      result = device->DrawPrimitive(D3DPT_TRIANGLELIST, kFvf,
                                     g_vertices + first,
                                     static_cast<DWORD>(count), 0);
    }
    if (!write) device->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
  }
  if (have_before) {
    device->SetTexture(0, before);
    if (before != nullptr) before->Release();
  }
  PutStatesBack(device);
  g_draw_ticks += Now() - start;
  if (FAILED(result)) {
    ++g_failed;
    if (!g_said_failed) {
      g_said_failed = true;
      ModLog("hand model   : the draw failed (0x%08X)",
             static_cast<unsigned>(result));
    }
    return -1;
  }
  ++g_draws;
  g_triangles_drawn += static_cast<uint64_t>(n / 3);
  if (!g_said_first) {
    g_said_first = true;
    ModLog("hand model   : the first hands are drawn with %s, frame %llu, "
           "%d triangles in this eye. The texture that was on stage 0 %s",
           g_mode == kModeFile ? "the mesh of bwvr-hand.glb"
                               : "the mesh of the game",
           g_frame, n / 3,
           have_before ? "went back after the draw" : "was not read");
  }
  return n / 3;
}

void HandModelReport() {
  if (g_draws == 0 && g_failed == 0) return;
  const double passes = g_draws > 0 ? static_cast<double>(g_draws) : 1.0;
  ModLog("hand model   : drawn in %llu eye passes, %.0f triangles a pass, "
         "%.0f that face away not drawn, %llu passes failed. On the CPU: "
         "%.3f ms a pass to project and draw, and %.3f ms a pass to skin and "
         "light",
         g_draws, static_cast<double>(g_triangles_drawn) / passes,
         static_cast<double>(g_culled) / passes, g_failed,
         Ms(g_draw_ticks) / passes, Ms(g_build_ticks) / passes);
  g_draws = g_triangles_drawn = g_culled = g_failed = 0;
  g_draw_ticks = g_build_ticks = 0;
  ++g_cost_frames;
}
