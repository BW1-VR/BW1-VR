// What hand_exact.cpp gives to the overlay. See docs/hand.md, "One pair of
// hands".

#pragma once

// The game's Hand in this frame, for the pose of the mod's hand.
struct HandExactContext {
  bool placed;         // the controller places the game's Hand now
  int state;           // the hand state, CHand+0x4878, or -1
  int animation;       // the animation number of the last compose, or -1
  bool holding;        // an object is in the Hand (CHand+0x4904)
  float hold_radius;   // the radius of its body, in world units
  bool have_alignment;
  float alignment;     // the player's alignment, -1 (evil) to 1 (good)
};

// False with no CHand.
bool HandExactGetContext(HandExactContext* out);

// The mesh of the game's Hand is hidden while the controller places the Hand
// (hand_hide_game=1, and the test key did not turn it off).
bool HandExactHidesGame();

// hand_tint=1: the mod's hands take the color of the player's alignment.
bool HandExactTint();
