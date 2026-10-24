// Hands that react to the world: the fingers bend on the land and against a
// body, open toward a target of the grip, and close on the object in the
// Hand. See hand_feel.cpp and docs/hand-model.md, "Hands that react to the world".

#pragma once

#include <stdint.h>

#include "overlay_math.h"

// What overlay.cpp knows of one hand in this frame.
struct HandFeelHand {
  int hand;        // 0 the left hand, 1 the right hand
  bool pointing;   // the hand that the game's Hand follows
  bool placed;     // the controller places the game's Hand now
  bool stroke;     // the pointing hand strokes the Creature
  bool holding;    // the pointing hand has the hold pose
  bool relaxed;    // the hand has the relaxed pose
  bool mesh;       // the hand is the mesh of the hand model
  float position[3];
  float rotation[9];
  float length;    // of the hand, in world units
  float units_per_meter;
  float trigger;
  float squeeze;
  float dt;        // seconds since the last frame
};

// What hand feel gives a hand of the mesh beside the curls
// (hand_model::FingerPose): the outer bend and the spread of each finger,
// and the turn of the thumb. 0 in each is the pose of the curls only.
struct HandFeelShape {
  float outer[overlay_math::kFingers];
  float spread[overlay_math::kFingers];
  float thumb_turn;
};

// overlay.cpp, once a frame before the hands are built: the test keys.
void HandFeelFrame();

// overlay.cpp, for each hand, after the curls of the pose are known: the
// curls with what the world adds, and the shape. With hand_feel=0 the curls
// do not change and the shape is 0.
void HandFeelCurls(const HandFeelHand& hand,
                   float curls[overlay_math::kFingers], HandFeelShape* shape);

// overlay.cpp, while the game's Hand holds an object: the hold pose from the
// game's own rule and the contact of each finger with the body in the hand.
// `hand` is the pointing hand, and hold_radius is the radius of the ball of
// the held object in world units. False when hand_feel does not make the
// pose. The caller then makes its own.
bool HandFeelHoldRange(int hand, bool mesh, float length, float hold_radius,
                       overlay_math::PoseRange* out);

// hand_exact.cpp, once a frame when it places the object in the Hand: the
// weight of the pinch for a small object (0 the fist, 1 the pinch), and the
// point between the tips of the thumb and of the index finger in the hand
// frame, in hand lengths. body_radius is the radius of the ball of the body
// in world units, and units_per_cm gives the size of the room.
float HandFeelPinch(void* held, uint64_t frame, float body_radius,
                    float hand_length, float units_per_cm, float point[3]);

// overlay.cpp, at the end of the world of the first eye pass, before the
// hands draw: the bodies near each hand, from the objects that the game drew
// in this pass. With hand_feel_bodies_now=1 the fingers of a hand of the mesh
// bend against them in this frame, and the hand is built again
// (OverlayRebuildHand). With 0, or with the low poly hands, the next frame
// uses them.
void HandFeelSnapshot();

// Once a report window.
void HandFeelReport();
