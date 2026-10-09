// The hand model: the game's own hand mesh on both of the player's hands. See
// docs/hand-model.md, "The game's hand on both real hands".

#pragma once

#include <stdint.h>

#include "hand_model_math.h"

struct IDirect3DDevice7;

// THE MESH that the hands draw, in the frame of the stored mesh with the
// bones at rest. hand_model.cpp fills it from the game's L3D files, and
// hand_model_file.cpp from a file of the player.
struct HandMesh {
  int vertices;
  int triangles;
  float* base;        // 3 floats a vertex
  float* base_normal;
  float* evil;        // the evil form, or null for none
  float* evil_normal;
  float* good;        // the good form, or null for the base form
  float* good_normal;
  float* uv;          // 2 floats a vertex, v down as the game has it
  uint32_t* index;    // 3 a triangle, the turn of the stored mesh
  uint8_t* bone;      // 4 a vertex
  float* weight;      // 4 a vertex, the sum is 1
};

void HandMeshFree(HandMesh* mesh);
bool HandMeshAllocate(HandMesh* mesh, int vertices, int triangles, bool evil,
                      bool good);

// hand_model_file.cpp: the mesh of bwvr-hand.glb beside the mod, fitted to
// the bones of the game. False with the reason in `why` when there is no file
// or it does not fit.
bool HandModelFileLoad(const char* path, const hand_model::Skeleton& skeleton,
                       HandMesh* out, char* why, int why_size);

// True while the hands draw with the mesh: hand_model=1, the files were read,
// and the test key did not turn it off.
bool HandModelActive();

// Once a frame, before the hands are built: the test keys, and the look from
// the player's alignment.
void HandModelFrame(uint64_t frame, bool have_alignment, float alignment);

// The hold pose with the fingers of the mesh (hand_model_math.h).
void HandModelHoldRange(float radius, overlay_math::PoseRange* out);

// The mesh of one hand (0 the left hand, 1 the right hand) for this frame,
// in the world. False when the model is not in use.
bool HandModelBuild(int hand, const float position[3], const float rotation[9],
                    float length, bool left,
                    const hand_model::FingerPose& pose);

// The joints of one finger of the mesh in the hand frame of the right hand,
// for a curl and a spread: the knuckle, the two joints and the tip. False
// when the model is not in use.
bool HandModelFingerJoints(int finger, float curl, float spread,
                           float joints[overlay_math::kJoints + 1][3]);

// The tip of the index finger of the mesh of a hand in the world, as it was
// built in this frame. False when the hand was not built.
bool HandModelIndexTip(int hand, float out[3]);

// True when a hand was built in this frame.
bool HandModelBuilt();

// In an eye pass: draws the hands of this frame with the camera of the eye.
// Returns the count of triangles drawn, or -1 when the draw failed.
int HandModelDraw(IDirect3DDevice7* device, const float camera[12],
                  float near_plane, float half_x, float half_y);

// Once a report window.
void HandModelReport();
