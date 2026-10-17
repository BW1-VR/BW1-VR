// The test of a body in the way of a tree in the Hand (docs/grab.md, "The
// test of a body in the way"). The release of the game raises a tree over a
// body that is in its way, and then the tree falls. This test gives the card
// the same answer before the release. It reads the game and changes nothing.

#pragma once

// The answer for one frame.
struct TreeBodyAnswer {
  bool in_way;     // the release raises the tree: it does not plant it
  void* object;    // the first body in the way, or null
  float depth;     // how deep that body is, in units
  int compared;    // bodies whose sphere meets the sphere of the tree
  int unknown;     // near objects whose body the mod does not make
  float radius;    // the radius of the body of the tree
  float ms;        // the time of the test
};

// hand_direct.cpp, once a frame while the Hand holds a tree and the other
// tests of the release let the game plant it. False when the mod cannot read
// the tree.
bool TreeBodyTest(void* tree, TreeBodyAnswer* out);

// hand_direct.cpp, on the first frame of the tree in the physics after the
// release: the log compares the bodies of the last test with the bodies that
// the game made.
void TreeBodyLogAfterRelease(void* tree);
