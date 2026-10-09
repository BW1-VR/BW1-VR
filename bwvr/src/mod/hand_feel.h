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

// overlay.cpp, once a frame before the hands are built: the test keys.
void HandFeelFrame();

// overlay.cpp, for each hand, after the curls of the pose are known: the
// curls with what the world adds. With hand_feel=0 the curls do not change.
void HandFeelCurls(const HandFeelHand& hand,
                   float curls[overlay_math::kFingers]);

// overlay.cpp, while the game's Hand holds an object: the hold pose from the
// game's own rule and the contact of each finger with the body in the hand.
// `hand` is the pointing hand, and hold_radius is the radius of the ball of
// the held object in world units. False when hand_feel does not make the
// pose. The caller then makes its own.
bool HandFeelHoldRange(int hand, bool mesh, float length, float hold_radius,
                       overlay_math::PoseRange* out);

// overlay.cpp, at the end of the world of the first eye pass: the bodies near
// each hand, from the objects that the game drew. The next frame uses them.
void HandFeelSnapshot();

// Once a report window.
void HandFeelReport();
