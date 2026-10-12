// One walk of the scene for both eyes (one_walk.cpp, docs/backend.md, "One
// walk for both eyes"). No Direct3D types here, so that files with the
// Direct3D 7 headers can include it.
#pragma once

#include <stdint.h>

// Where a draw of the pass goes.
enum OneWalkMode {
  kOneWalkBoth = 0,       // the left half as asked, and a copy for the right
  kOneWalkLeftOnly = 1,   // the left half only
  kOneWalkRightOnly = 2,  // the right half only: the batch is for the right eye
};

void OneWalkSetWanted(bool on);
// one_walk_check: a frame that saves pictures also walks the right eye.
void OneWalkSetCheck(bool on);
bool OneWalkCheck();
// True when one walk is asked for and nothing has failed.
bool OneWalkWanted();
void OneWalkToggle();

// The pass of the left eye starts. separation is the distance of the eyes in
// world units: the left eye is at -separation / 2 and the right eye at
// +separation / 2 on the camera right axis. False when one walk cannot run:
// then the caller draws two walks.
bool OneWalkBeginPass(float separation);
void OneWalkEndPass();
bool OneWalkActive();
// The move of a vertex from the left eye to the right eye per unit of its rhw,
// in pixels (one_walk_math::ShiftPerRhw), now in the pass, or at the last draw.
float OneWalkShiftPerRhw();
// draw_merge.cpp holds the move of its batch while the batch goes out, because
// the engine can have a new camera by then.
void OneWalkHoldShift(bool hold, float shift);
void OneWalkSetMode(int mode);

// THE CARDS AND THE MARKS OF THE MOD (one_walk_marks). A speech bubble as a
// card (bubble_card.cpp) and a disc of hand_glow.cpp do not have the rhw of
// their place in their vertices. With 1 each of them draws its batch for the
// left eye only, and then for the right eye only at the place that the right
// eye of its own walk gives it (one_walk_math::ShiftAtDepth). With 0 the right
// eye gets them as each other batch.
void OneWalkSetMarks(bool on, int test_frame);
bool OneWalkMarks();
// The test key (Ctrl, Shift and the right square bracket key, test_keys=1).
void OneWalkMarksToggle(const char* cause);

// The wide target (an IDirect3DSurface9) and the part of it that one eye
// holds, as left, top, right, bottom.
void* OneWalkTarget();
bool OneWalkEyeRect(int eye, long rect[4]);

// The picture of one eye of the last pass, for a file, and its end.
const void* OneWalkReadEye(int eye, int* pitch, uint32_t* width,
                           uint32_t* height);
void OneWalkReadDone();

void OneWalkReport(uint64_t frames);
