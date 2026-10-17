// THE TEST OF A BODY IN THE WAY OF A TREE IN THE HAND (docs/grab.md, "The
// test of a body in the way").
//
// WHAT THE GAME DOES [V]. The release of the Hand
// (Object::InitialisePhysicsFromHand 0x00636F00) gives the tree a physics body
// (PhysicsObject::AddObject 0x006443A0, Tree::SetUpPhysOb 0x0074B650), puts
// its lowest point on the land (PhysOb::AdjustToGroundLevel 0x007FCB80),
// places its points there (PhysOb::ZeroForces 0x007FD200) and calls
// PhysicsObject::RaiseUntilNotIntersecting 0x00644800. That call:
//   1. reads each object in the map cells of the square of the center of the
//      tree plus and minus its radius. Each object that says yes to slot 0x7A4
//      (ShouldPhysicsRaiseObjectUntilNotIntersectingThis), has a mesh and is
//      not in the physics goes into the physics (fn_00644DF0), with the body
//      of its class (slot 0x78C) at the place of its mesh.
//   2. compares the tree with each body in the physics whose sphere meets the
//      sphere of the tree (tree_body_math.h). A body deeper than 0.001 units
//      raises the tree, and the release does not plant a raised tree.
//
// WHAT THE MOD DOES (tree_plant_bodies=1). The same two steps, once a frame,
// on bodies in memory of the mod: the body of the tree at the place of the
// release, the body of each near object that the release puts in the
// physics, and the bodies that are in the physics now. The mod only reads the
// game, and it calls only slots that return a value: GetMapChild, the yes of
// slot 0x7A4, IsARootedObject, the height, the 2D radius and the scale. The
// bodies that the mod makes: a tree and a dead tree, a villager, an animal,
// the hull of a mesh (each class with Object::SetUpPhysOb or
// Abode::SetUpPhysOb, for example a rock, a scaffold or a building) and a
// Creature. The test does not count a near object of a different class. The
// log names that class.
//
// The release gives the tree the speed of the hand, and ZeroForces moves its
// points by 0.06 of that speed. The test takes the tree as still. The game
// sets a tree down only at 2 units a turn or less, so the points move 0.12
// units at most.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "tree_body.h"
#include "tree_body_math.h"

extern void ModLog(const char* format, ...);
const char* HandDirectClass(void* object);  // hand_direct.cpp

namespace {

using namespace tree_body_math;

// The physics of the game: an array of entries of 0x1DC bytes. An entry has
// its Object at +0x18 and its body (a PhysOb) at +0x28.
const uintptr_t kPhysicsArray = 0x00D47814;
const uintptr_t kPhysicsCount = 0x00D47818;
const int kEntrySize = 0x1DC;
const int kEntryObject = 0x18;
const int kEntryBody = 0x28;
// A PhysOb.
const int kBodyCenter = 0xA0;
const int kBodyRadius = 0x150;
const int kBodyPointCount = 0x154;
const int kBodyPoints = 0x15C;      // records of 0x50 bytes
const int kBodyFaceCount = 0x160;
const int kBodyFaces = 0x164;       // records of 0x24 bytes, 3 indices first
const int kBodyEllipsoids = 0x168;  // a Creature: its ellipsoids, else null
const int kBodyCollision = 0x16C;   // a Creature: its collision object
const int kPointSize = 0x50;
const int kPointLocal = 0x10;
const int kPointWorld = 0x34;
const int kFaceSize = 0x24;
// The map: GGame::g_game, its counts of cells, and its cells of 8 bytes
// (two lists of objects, +4 first). JustMapXZ::ToMap 0x005E1950.
const uintptr_t kGame = 0x00D0195C;
const int kCellsX = 0x59C8;
const int kCellsZ = 0x59C4;
const int kCells = 0x59FC;
// An Object.
const int kObjectMesh = 0x40;
const int kSlotMapChild = 0x53C;   // GetMapChild: the next object of a cell
const int kSlotRaiseOver = 0x7A4;  // ShouldPhysicsRaiseObjectUntilNotIntersectingThis
const int kSlotSetUpBody = 0x78C;  // SetUpPhysOb
const int kSlotRooted = 0x7D0;     // IsARootedObject
const int kSlotHeight = 0x42C;     // GetHeight
const int kSlotRadius = 0x64;      // Get2DRadius
const int kSlotScale = 0x120;      // GetScale
// An LH3DObject: its frame, its mesh (slot 0xF8), and a lift of y for each
// vertex at +0x80 when slot 0x1F4 gives 1.
const int kMeshFrame = 0x14;
const int kSlotMeshData = 0xF8;
const int kSlotHasLift = 0x1F4;
const int kMeshLift = 0x80;
// An LH3DMesh: groups of parts. A group has its flags, its count of parts and
// its parts. A part has its vertices (0x20 bytes, x y z first) and its faces
// (3 shorts each).
const int kGroupCount = 0x0C;
const int kGroups = 0x10;
const uint32_t kGroupBody = 1u << 13;   // the parts of the body, if a group has it
const uint32_t kGroupShape = 1u << 29;  // else the parts with this flag
const int kPartVertexCount = 0x10;
const int kPartVertices = 0x14;
const int kPartFaceCount = 0x18;
const int kPartFaces = 0x1C;
const int kVertexSize = 0x20;
// The mesh pack: a count, then the meshes. A dead tree with the mesh 0x196
// has the hull of its mesh (DeadTree::SetUpPhysOb).
const uintptr_t kMeshPack = 0x00E9FE34;
const int kDeadTreeHullMesh = 0x196;
// The SetUpPhysOb of each class that the mod makes the body of.
const uintptr_t kSetUpObject = 0x006376B0;
const uintptr_t kSetUpAbode = 0x00402DD0;
const uintptr_t kSetUpTree = 0x0074B650;
const uintptr_t kSetUpDeadTree = 0x00511170;
const uintptr_t kSetUpVillager = 0x005EFF40;
const uintptr_t kSetUpAnimal = 0x005F04E0;
const uintptr_t kSetUpCreature = 0x00479B90;
// A Creature: its LH3DCreature is at *(+0x160) + 0x58. The frames of its
// bones (12 floats each) are in its safe buffer (LH3DCreature::GetSafeBuffer
// 0x004842B0), its ellipsoids at +0x57A4, and the collision object of its
// anim gives their count (fn_00867FE0).
const int kCreatureHolder = 0x160;
const int kHolderModel = 0x58;
const int kModelBoneCount = 0x47B8;
const int kModelSafeFlag = 0x5270;
const int kModelSafeOn = 0x5180;
const int kModelSafeOff = 0x5178;
const int kModelEllipsoids = 0x57A4;
const int kModelAnim = 0x98;
const int kModelCollision = 0xB4;
const int kEllipsoidSize = 0x78;
const int kBoneSize = 0x30;
const uintptr_t kGetAltitude = 0x00803090;  // LH3DIsland::GetAltitude

typedef void*(__fastcall* MapChildFn)(void* self, void* unused, void* cell);
typedef bool(__fastcall* RaiseOverFn)(void* self, void* unused, void* other);
typedef int(__fastcall* IntSlotFn)(void* self, void* unused);
typedef float(__fastcall* FloatSlotFn)(void* self, void* unused);
typedef void*(__fastcall* PointerSlotFn)(void* self, void* unused);
typedef float(__fastcall* GetAltitudeFn)(const int32_t* coords);

const int kMaxPoints = 2048;
const int kMaxFaces = 4096;
const int kMaxEllipsoids = 64;
const int kMaxWalk = 1024;  // objects of the map cells, at most
const int kMaxEntries = 4096;
const int kMaxSeen = 8;

bool Readable(const void* p, size_t size) {
  return p != nullptr && !IsBadReadPtr(p, size);
}

void* Slot(void* object, int slot) {
  return (*static_cast<void***>(object))[slot / 4];
}

int IntSlot(void* object, int slot) {
  return reinterpret_cast<IntSlotFn>(Slot(object, slot))(object, nullptr);
}

float FloatSlot(void* object, int slot) {
  return reinterpret_cast<FloatSlotFn>(Slot(object, slot))(object, nullptr);
}

template <typename T>
T Read(const void* base, int offset) {
  return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) +
                                     offset);
}

// One body of the mod, with room for its points, faces and ellipsoids.
struct Store {
  float local[kMaxPoints][3];
  float points[kMaxPoints][3];
  int faces[kMaxFaces][3];
  Ellipsoid ellipsoids[kMaxEllipsoids];
  Body body;
  const char* kind;  // the shape, for the log
};
Store g_held;   // the tree in the Hand
Store g_other;  // a near object
float g_raw[kMaxPoints][3];
float g_lifted[kMaxPoints][3];
float g_land[kMaxPoints];
void* g_in_physics[kMaxEntries];

// What the last test compared, for the log after the release.
struct Seen {
  void* object;
  float center[3];
  float radius;
  int points;
  int faces;
  int ellipsoids;
  float depth;
  bool from_physics;
};
Seen g_seen[kMaxSeen];
int g_seen_count = 0;

// The log: each change of the answer, and each class with no body of the mod.
void* g_said_object = nullptr;
int g_said_way = -1;
int g_logs = 0;
const int kMaxLogs = 80;
uintptr_t g_said_unknown[16];
int g_said_unknown_count = 0;
float g_most_ms = 0.0f;

enum Made { kMade, kNoModel, kNoRead };

bool MeshFrame(void* object, float frame[12]) {
  uint8_t* mesh = Read<uint8_t*>(object, kObjectMesh);
  if (!Readable(mesh, kMeshFrame + 48)) return false;
  memcpy(frame, mesh + kMeshFrame, 48);
  return true;
}

// Object::SetUpPhysObAsATree, from the slots of the object.
Made TreeBody(void* object, const float frame[12], Store* s) {
  const float height = FloatSlot(object, kSlotHeight);
  const float radius = FloatSlot(object, kSlotRadius);
  const float scale = FloatSlot(object, kSlotScale);
  const bool rooted = IntSlot(object, kSlotRooted) != 0;
  float middle[3], body_radius = 0.0f;
  if (!TreeShape(height, radius, rooted, scale, s->local, middle,
                 &body_radius)) {
    return kNoRead;
  }
  s->body = Body{s->points, kTreePoints, TreeFaces(), kTreeFaces, nullptr, 0,
                 {0, 0, 0}, body_radius};
  PlaceBody(frame, middle, s->local, kTreePoints, s->points, s->body.center);
  s->kind = rooted ? "tree" : "dead tree";
  return kMade;
}

// Villager::SetUpPhysOb and Animal::SetUpPhysOb.
Made LivingBody(void* object, const float frame[12], bool animal, Store* s) {
  const float height = FloatSlot(object, kSlotHeight);
  const float radius = FloatSlot(object, kSlotRadius);
  const float scale = FloatSlot(object, kSlotScale);
  float middle[3], body_radius = 0.0f;
  if (!LivingShape(height, radius, animal, scale, s->local, middle,
                   &body_radius)) {
    return kNoRead;
  }
  s->body = Body{s->points, kLivingPoints, LivingFaces(), kLivingFaces,
                 nullptr, 0, {0, 0, 0}, body_radius};
  PlaceBody(frame, middle, s->local, kLivingPoints, s->points,
            s->body.center);
  s->kind = animal ? "animal" : "villager";
  return kMade;
}

// fn_007FBAE0: the hull of the parts of the mesh with the body flag, or with
// the shape flag when no group has the body flag.
Made HullBody(void* object, const float frame[12], Store* s) {
  void* mesh = Read<void*>(object, kObjectMesh);
  if (!Readable(mesh, kMeshLift + 4)) return kNoRead;
  const uint8_t* data = static_cast<const uint8_t*>(
      reinterpret_cast<PointerSlotFn>(Slot(mesh, kSlotMeshData))(mesh,
                                                                 nullptr));
  if (!Readable(data, kGroups + 4)) return kNoRead;
  const int groups = Read<int32_t>(data, kGroupCount);
  const uint32_t* const* group = Read<const uint32_t* const*>(data, kGroups);
  if (groups <= 0 || groups > 256 || !Readable(group, groups * 4)) {
    return kNoRead;
  }
  bool body_flag = false;
  for (int g = 0; g < groups; ++g) {
    if (!Readable(group[g], 12)) return kNoRead;
    if ((group[g][0] & kGroupBody) != 0) body_flag = true;
  }
  const uint32_t want = body_flag ? kGroupBody : kGroupShape;
  const bool has_lift = IntSlot(mesh, kSlotHasLift) != 0;
  const float* lift = has_lift ? Read<const float*>(mesh, kMeshLift) : nullptr;
  int count = 0, faces = 0, lifted = 0;
  for (int g = 0; g < groups; ++g) {
    const uint32_t* grp = group[g];
    const int parts = static_cast<int>(grp[1]);
    const uint8_t* const* part = reinterpret_cast<const uint8_t* const*>(
        static_cast<uintptr_t>(grp[2]));
    if (parts < 0 || parts > 256 || (parts > 0 && !Readable(part, parts * 4))) {
      return kNoRead;
    }
    for (int k = 0; k < parts; ++k) {
      if (!Readable(part[k], kPartFaces + 4)) return kNoRead;
      const int vertices = Read<int32_t>(part[k], kPartVertexCount);
      if (vertices < 0) return kNoRead;
      if ((grp[0] & want) == 0) {
        lifted += vertices;  // the lifts of a part that the hull skips
        continue;
      }
      const uint8_t* v = Read<const uint8_t*>(part[k], kPartVertices);
      const int part_faces = Read<int32_t>(part[k], kPartFaceCount);
      const uint16_t* f = Read<const uint16_t*>(part[k], kPartFaces);
      if (count + vertices > kMaxPoints || part_faces < 0 ||
          faces + part_faces > kMaxFaces ||
          (vertices > 0 && !Readable(v, vertices * kVertexSize)) ||
          (part_faces > 0 && !Readable(f, part_faces * 6)) ||
          (lift != nullptr && !Readable(lift + lifted, vertices * 4))) {
        return kNoRead;
      }
      for (int i = 0; i < part_faces; ++i) {
        for (int c = 0; c < 3; ++c) {
          s->faces[faces + i][c] = static_cast<int>(f[i * 3 + c]) + count;
        }
      }
      for (int i = 0; i < vertices; ++i) {
        const float* xyz = reinterpret_cast<const float*>(v + i * kVertexSize);
        for (int c = 0; c < 3; ++c) {
          g_raw[count + i][c] = xyz[c];
          g_lifted[count + i][c] = xyz[c];
        }
        if (lift != nullptr) g_lifted[count + i][1] = xyz[1] + lift[lifted + i];
      }
      count += vertices;
      faces += part_faces;
      lifted += vertices;
    }
  }
  for (int i = 0; i < faces; ++i) {
    for (int c = 0; c < 3; ++c) {
      if (s->faces[i][c] >= count) return kNoRead;
    }
  }
  const float scale = FloatSlot(object, kSlotScale);
  float middle[3], body_radius = 0.0f;
  if (!HullShape(g_raw, g_lifted, count, scale, s->local, middle,
                 &body_radius)) {
    return kNoRead;
  }
  s->body = Body{s->points, count, s->faces, faces, nullptr, 0, {0, 0, 0},
                 body_radius};
  PlaceBody(frame, middle, s->local, count, s->points, s->body.center);
  s->kind = "hull of its mesh";
  return kMade;
}

// Creature::SetUpPhysOb: a point at the middle of each bone, an ellipsoid in
// the frame of each bone, and the sphere of the bones
// (LH3DCreature::GetBoundingSphere 0x0047F8D0).
Made CreatureBody(void* object, Store* s) {
  const uint8_t* holder = Read<const uint8_t*>(object, kCreatureHolder);
  if (!Readable(holder, kHolderModel + 4)) return kNoRead;
  const uint8_t* model = Read<const uint8_t*>(holder, kHolderModel);
  if (!Readable(model, kModelEllipsoids + 4)) return kNoRead;
  const int bones = Read<int32_t>(model, kModelBoneCount);
  const uint8_t* safe = Read<int32_t>(model, kModelSafeFlag) != 0
                            ? Read<const uint8_t*>(model, kModelSafeOn)
                            : Read<const uint8_t*>(model, kModelSafeOff);
  const int anim = Read<int32_t>(model, kModelAnim);
  if (anim < 0 || anim > 64 || !Readable(model + kModelCollision + anim * 4, 4)) {
    return kNoRead;
  }
  const uint8_t* collision =
      Read<const uint8_t*>(model, kModelCollision + anim * 4);
  if (!Readable(collision, 8)) return kNoRead;
  const uint32_t flags = Read<uint32_t>(collision, 4);
  const int ellipsoids = (flags & 0x100) != 0 ? static_cast<int>(flags & 0xFF)
                                              : 1;
  const uint8_t* records = Read<const uint8_t*>(model, kModelEllipsoids);
  const int frames = bones > ellipsoids ? bones : ellipsoids;
  if (bones <= 0 || bones > kMaxPoints || ellipsoids > kMaxEllipsoids ||
      !Readable(safe, frames * kBoneSize) ||
      !Readable(records, ellipsoids * kEllipsoidSize)) {
    return kNoRead;
  }
  float sum[3] = {0, 0, 0};
  for (int i = 0; i < bones; ++i) {
    const float* bone = reinterpret_cast<const float*>(safe + i * kBoneSize);
    for (int c = 0; c < 3; ++c) {
      s->points[i][c] = bone[9 + c];
      sum[c] = bone[9 + c] + sum[c];
    }
  }
  const float inverse = 1.0f / static_cast<float>(bones);
  float center[3];
  for (int c = 0; c < 3; ++c) center[c] = inverse * sum[c];
  float most = 0.0f;
  for (int i = 0; i < bones; ++i) {
    float d[3];
    Sub(s->points[i], center, d);
    const float length = sqrtf(Dot(d, d));
    if (most < length) most = length;
  }
  for (int i = 0; i < ellipsoids; ++i) {
    Ellipsoid& e = s->ellipsoids[i];
    memcpy(e.box_min, records + i * kEllipsoidSize, 24);
    memcpy(e.frame, safe + i * kBoneSize, 48);
    Invert(e.frame, e.inverse);
  }
  s->body = Body{s->points, bones, nullptr, 0, s->ellipsoids, ellipsoids,
                 {center[0], center[1], center[2]}, most};
  s->kind = "bones of the Creature";
  return kMade;
}

// The body that the release gives the object, by the SetUpPhysOb of its class.
Made MakeBody(void* object, Store* s, uintptr_t* set_up) {
  *set_up = reinterpret_cast<uintptr_t>(Slot(object, kSlotSetUpBody));
  if (*set_up == kSetUpCreature) return CreatureBody(object, s);
  float frame[12];
  if (!MeshFrame(object, frame)) return kNoRead;
  switch (*set_up) {
    case kSetUpTree:
      return TreeBody(object, frame, s);
    case kSetUpDeadTree: {
      const uint8_t* pack = *reinterpret_cast<const uint8_t* const*>(kMeshPack);
      if (!Readable(pack, 4)) return kNoRead;
      const int at = Read<int32_t>(pack, 0) > kDeadTreeHullMesh
                         ? kDeadTreeHullMesh
                         : 0;
      if (!Readable(pack + 4 + at * 4, 4)) return kNoRead;
      void* mesh = Read<void*>(object, kObjectMesh);
      const void* data =
          reinterpret_cast<PointerSlotFn>(Slot(mesh, kSlotMeshData))(mesh,
                                                                     nullptr);
      if (data == Read<const void*>(pack, 4 + at * 4)) {
        return HullBody(object, frame, s);
      }
      return TreeBody(object, frame, s);
    }
    case kSetUpVillager:
      return LivingBody(object, frame, false, s);
    case kSetUpAnimal:
      return LivingBody(object, frame, true, s);
    case kSetUpObject:
    case kSetUpAbode:
      return HullBody(object, frame, s);
    default:
      return kNoModel;
  }
}

// The body of an entry of the physics, as it is.
bool EntryBody(const uint8_t* entry, Store* s) {
  const uint8_t* body = entry + kEntryBody;
  const int points = Read<int32_t>(body, kBodyPointCount);
  const int faces = Read<int32_t>(body, kBodyFaceCount);
  const uint8_t* point = Read<const uint8_t*>(body, kBodyPoints);
  const uint8_t* face = Read<const uint8_t*>(body, kBodyFaces);
  const uint8_t* ellipsoid = Read<const uint8_t*>(body, kBodyEllipsoids);
  if (points < 0 || points > kMaxPoints || faces < 0 || faces > kMaxFaces ||
      (points > 0 && !Readable(point, points * kPointSize)) ||
      (faces > 0 && !Readable(face, faces * kFaceSize))) {
    return false;
  }
  for (int i = 0; i < points; ++i) {
    memcpy(s->points[i], point + i * kPointSize + kPointWorld, 12);
  }
  int kept = 0;
  for (int i = 0; i < faces; ++i) {
    const int32_t* index =
        reinterpret_cast<const int32_t*>(face + i * kFaceSize);
    if (index[0] < 0 || index[0] >= points || index[1] < 0 ||
        index[1] >= points || index[2] < 0 || index[2] >= points) {
      continue;
    }
    for (int c = 0; c < 3; ++c) s->faces[kept][c] = index[c];
    ++kept;
  }
  int ellipsoids = 0;
  if (ellipsoid != nullptr) {
    const uint8_t* collision = Read<const uint8_t*>(body, kBodyCollision);
    if (!Readable(collision, 8)) return false;
    const uint32_t flags = Read<uint32_t>(collision, 4);
    ellipsoids = (flags & 0x100) != 0 ? static_cast<int>(flags & 0xFF) : 1;
    if (ellipsoids > kMaxEllipsoids ||
        !Readable(ellipsoid, ellipsoids * kEllipsoidSize)) {
      return false;
    }
    memcpy(s->ellipsoids, ellipsoid, ellipsoids * sizeof(Ellipsoid));
  }
  s->body = Body{s->points, points, s->faces, kept,
                 ellipsoid != nullptr ? s->ellipsoids : nullptr, ellipsoids,
                 {Read<float>(body, kBodyCenter), Read<float>(body, kBodyCenter + 4),
                  Read<float>(body, kBodyCenter + 8)},
                 Read<float>(body, kBodyRadius)};
  s->kind = "body in the physics";
  return true;
}

void Remember(void* object, const Store& s, float depth, bool from_physics) {
  if (g_seen_count >= kMaxSeen) return;
  Seen& seen = g_seen[g_seen_count++];
  seen.object = object;
  for (int c = 0; c < 3; ++c) seen.center[c] = s.body.center[c];
  seen.radius = s.body.radius;
  seen.points = s.body.point_count;
  seen.faces = s.body.face_count;
  seen.ellipsoids = s.body.ellipsoid_count;
  seen.depth = depth;
  seen.from_physics = from_physics;
}

void SayUnknown(void* object, uintptr_t set_up) {
  for (int i = 0; i < g_said_unknown_count; ++i) {
    if (g_said_unknown[i] == set_up) return;
  }
  if (g_said_unknown_count < 16) g_said_unknown[g_said_unknown_count++] = set_up;
  ModLog("tree body    : the mod does not make the body of %p (%s, SetUpPhysOb "
         "0x%08X). The card does not count it", object,
         HandDirectClass(object), static_cast<unsigned>(set_up));
}

double NowMs() {
  static LARGE_INTEGER frequency = {};
  if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
  LARGE_INTEGER count;
  QueryPerformanceCounter(&count);
  return static_cast<double>(count.QuadPart) * 1000.0 /
         static_cast<double>(frequency.QuadPart);
}

// The test without the time and the log.
bool Test(void* tree, TreeBodyAnswer* out) {
  g_seen_count = 0;
  // The body of the tree at the place of the release: the frame of its mesh
  // now, with its lowest point on the land.
  uintptr_t set_up = 0;
  if (MakeBody(tree, &g_held, &set_up) != kMade) return false;
  Body& tree_body = g_held.body;
  const GetAltitudeFn altitude = reinterpret_cast<GetAltitudeFn>(kGetAltitude);
  for (int i = 0; i < tree_body.point_count; ++i) {
    const int32_t coords[3] = {LandInt(g_held.points[i][0]),
                               LandInt(g_held.points[i][2]), 0};
    g_land[i] = altitude(coords);
  }
  const float gap = LandGap(g_held.points, g_land, tree_body.point_count);
  for (int i = 0; i < tree_body.point_count; ++i) g_held.points[i][1] -= gap;
  tree_body.center[1] -= gap;
  out->radius = tree_body.radius;

  // The bodies in the physics now.
  const uint8_t* array = *reinterpret_cast<const uint8_t* const*>(kPhysicsArray);
  int entries = *reinterpret_cast<const int32_t*>(kPhysicsCount);
  if (entries < 0 || entries > kMaxEntries ||
      (entries > 0 && !Readable(array, entries * kEntrySize))) {
    entries = 0;
  }
  for (int i = 0; i < entries; ++i) {
    const uint8_t* entry = array + i * kEntrySize;
    void* object = Read<void*>(entry, kEntryObject);
    g_in_physics[i] = object;
    if (object == tree || out->in_way) continue;
    const uint8_t* body = entry + kEntryBody;
    float gap_to[3];
    const float center[3] = {Read<float>(body, kBodyCenter),
                             Read<float>(body, kBodyCenter + 4),
                             Read<float>(body, kBodyCenter + 8)};
    Sub(tree_body.center, center, gap_to);
    const float reach = Read<float>(body, kBodyRadius) + tree_body.radius;
    if (!(Dot(gap_to, gap_to) < reach * reach)) continue;
    if (!EntryBody(entry, &g_other)) continue;
    ++out->compared;
    float depth = 0.0f;
    const bool way = InTheWay(tree_body, g_other.body, &depth);
    Remember(object, g_other, depth, true);
    if (way) {
      out->in_way = true;
      out->object = object;
      out->depth = depth;
    }
  }
  if (out->in_way) return true;

  // The near objects that the release puts in the physics.
  const uint8_t* game = *reinterpret_cast<const uint8_t* const*>(kGame);
  if (!Readable(game, kCells)) return true;
  const int cells_x = Read<int32_t>(game, kCellsX);
  const int cells_z = Read<int32_t>(game, kCellsZ);
  if (cells_x <= 0 || cells_z <= 0) return true;
  int x0, x1, z0, z1;
  CellSquare(tree_body.center[0], tree_body.center[2], tree_body.radius,
             cells_x, cells_z, &x0, &x1, &z0, &z1);
  int walked = 0;
  for (int cx = x0; cx <= x1; ++cx) {
    for (int cz = z0; cz <= z1; ++cz) {
      uint8_t* cell = const_cast<uint8_t*>(game) + kCells +
                      (static_cast<size_t>(cx) * cells_z + cz) * 8;
      if (!Readable(cell, 8)) continue;
      for (int list = 0; list < 2; ++list) {
        // The game reads the list at +4 first, then the one at +0.
        void* object = Read<void*>(cell, list == 0 ? 4 : 0);
        while (object != nullptr && walked < kMaxWalk) {
          ++walked;
          if (!Readable(object, kObjectMesh + 4)) break;
          void* next = reinterpret_cast<MapChildFn>(
              Slot(object, kSlotMapChild))(object, nullptr, cell);
          bool take = object != tree &&
                      reinterpret_cast<RaiseOverFn>(Slot(
                          object, kSlotRaiseOver))(object, nullptr, tree) &&
                      Read<void*>(object, kObjectMesh) != nullptr;
          for (int i = 0; take && i < entries; ++i) {
            if (g_in_physics[i] == object) take = false;
          }
          if (take) {
            const Made made = MakeBody(object, &g_other, &set_up);
            if (made == kNoModel) {
              ++out->unknown;
              SayUnknown(object, set_up);
            } else if (made == kMade) {
              float depth = 0.0f;
              const bool way = InTheWay(tree_body, g_other.body, &depth);
              float gap_to[3];
              Sub(tree_body.center, g_other.body.center, gap_to);
              const float reach = g_other.body.radius + tree_body.radius;
              if (Dot(gap_to, gap_to) < reach * reach) {
                ++out->compared;
                Remember(object, g_other, depth, false);
              }
              if (way) {
                out->in_way = true;
                out->object = object;
                out->depth = depth;
                return true;
              }
            }
          }
          object = next;
        }
      }
    }
  }
  return true;
}

}  // namespace

bool TreeBodyTest(void* tree, TreeBodyAnswer* out) {
  *out = TreeBodyAnswer{};
  if (tree == nullptr) return false;
  const double start = NowMs();
  const bool read = Test(tree, out);
  out->ms = static_cast<float>(NowMs() - start);
  if (out->ms > g_most_ms) g_most_ms = out->ms;
  if (!read) return false;
  const int way = out->in_way ? 1 : 0;
  if ((way != g_said_way || out->object != g_said_object) &&
      g_logs < kMaxLogs) {
    ++g_logs;
    const Body& t = g_held.body;
    if (out->in_way) {
      ModLog("tree body    : the release now raises the tree %p over %p (%s), "
             "%.3f units deep, so the tree falls. The body of the tree (%s) "
             "has its middle at %.2f %.2f %.2f and a radius of %.2f. %d "
             "bodies met its sphere, %d near objects have no body of the "
             "mod, the test took %.2f ms (%.2f ms at most)",
             tree, out->object, HandDirectClass(out->object), out->depth,
             g_held.kind, t.center[0], t.center[1], t.center[2], t.radius,
             out->compared, out->unknown, out->ms, g_most_ms);
    } else {
      ModLog("tree body    : no body is in the way of the tree %p now. The "
             "body of the tree (%s) has its middle at %.2f %.2f %.2f and a "
             "radius of %.2f. %d bodies met its sphere, %d near objects have "
             "no body of the mod, the test took %.2f ms (%.2f ms at most)",
             tree, g_held.kind, t.center[0], t.center[1], t.center[2],
             t.radius, out->compared, out->unknown, out->ms, g_most_ms);
    }
    for (int i = 0; i < g_seen_count && g_logs < kMaxLogs; ++i) {
      const Seen& s = g_seen[i];
      ++g_logs;
      ModLog("tree body    :   %p (%s), %s: middle %.2f %.2f %.2f, radius "
             "%.2f, %d points, %d faces, %d ellipsoids, %.3f units deep",
             s.object, HandDirectClass(s.object),
             s.from_physics ? "in the physics now"
                            : "the release puts it in the physics",
             s.center[0], s.center[1], s.center[2], s.radius, s.points,
             s.faces, s.ellipsoids, s.depth);
    }
  }
  g_said_way = way;
  g_said_object = out->object;
  return true;
}

void TreeBodyLogAfterRelease(void* tree) {
  const uint8_t* array = *reinterpret_cast<const uint8_t* const*>(kPhysicsArray);
  const int entries = *reinterpret_cast<const int32_t*>(kPhysicsCount);
  if (entries <= 0 || entries > kMaxEntries ||
      !Readable(array, entries * kEntrySize)) {
    return;
  }
  g_said_way = -1;
  g_said_object = nullptr;
  for (int i = 0; i < entries; ++i) {
    const uint8_t* entry = array + i * kEntrySize;
    void* object = Read<void*>(entry, kEntryObject);
    const uint8_t* body = entry + kEntryBody;
    if (object == tree) {
      // The local points do not change with the place of the body.
      const int points = Read<int32_t>(body, kBodyPointCount);
      const uint8_t* point = Read<const uint8_t*>(body, kBodyPoints);
      float most = -1.0f;
      if (points == g_held.body.point_count &&
          Readable(point, points * kPointSize)) {
        most = 0.0f;
        for (int k = 0; k < points; ++k) {
          const float* game =
              reinterpret_cast<const float*>(point + k * kPointSize + kPointLocal);
          for (int c = 0; c < 3; ++c) {
            const float d = fabsf(game[c] - g_held.local[k][c]);
            if (d > most) most = d;
          }
        }
      }
      if (g_logs < kMaxLogs) {
        ++g_logs;
        ModLog("tree body    : the game made the body of the tree %p with %d "
               "points (the mod %d), a radius of %.3f (the mod %.3f) and its "
               "middle now at %.2f %.2f %.2f (the mod %.2f %.2f %.2f before "
               "the release). The points of the mod are %.4f units from the "
               "points of the game at most (-1: the counts are not the same)",
               tree, points, g_held.body.point_count,
               Read<float>(body, kBodyRadius), g_held.body.radius,
               Read<float>(body, kBodyCenter), Read<float>(body, kBodyCenter + 4),
               Read<float>(body, kBodyCenter + 8), g_held.body.center[0],
               g_held.body.center[1], g_held.body.center[2], most);
      }
      continue;
    }
    for (int k = 0; k < g_seen_count; ++k) {
      const Seen& s = g_seen[k];
      if (s.object != object || g_logs >= kMaxLogs) continue;
      ++g_logs;
      ModLog("tree body    : the game has %p (%s) in the physics with its "
             "middle at %.2f %.2f %.2f (the mod %.2f %.2f %.2f), a radius of "
             "%.3f (the mod %.3f), %d points (the mod %d) and %d faces (the "
             "mod %d). The mod gave it a depth of %.3f units",
             object, HandDirectClass(object), Read<float>(body, kBodyCenter),
             Read<float>(body, kBodyCenter + 4),
             Read<float>(body, kBodyCenter + 8), s.center[0], s.center[1],
             s.center[2], Read<float>(body, kBodyRadius), s.radius,
             Read<int32_t>(body, kBodyPointCount), s.points,
             Read<int32_t>(body, kBodyFaceCount), s.faces, s.depth);
    }
  }
}
