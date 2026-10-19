// A note of the game over a thing in the world, at the depth of its place
// (note_depth.cpp, docs/ui-layer.md, section 19).
#pragma once

#include <stdint.h>

// The settings: note_depth, and the test keys note_test (a list of frames)
// and note_depth_test_frame.
void NoteDepthSetOptions(int on, const char* test_frames, int test_frame);
void NoteDepthInstall();
// The test key (Ctrl, Shift and the apostrophe key in play, test_keys=1).
void NoteDepthToggle(const char* cause);
// stereo.cpp: the eye pass that runs, or -1 after the passes.
void NoteDepthPass(int eye);
// controllers.cpp: once a frame, and once for each pass of the message pump
// of the game. The test key note_test acts in the pump.
void NoteDepthFrame(uint64_t frame);
void NoteDepthPump();
void NoteDepthReport();

// For draw_hooks.cpp. True while the game draws its notes in an eye pass.
bool NoteDepthInNote();
// True when a note has the depth of its place (note_depth=1).
bool NoteDepthOn();
// A batch of a note. `walk` is true in the pass of one walk, where the right
// eye gets a copy of the batch. `ui_shift` is the move of the interface depth
// in this eye, for the log. `placed` is true when the batch has the depth of
// its place, and false when it gets the steps of the interface.
void NoteDepthBatch(const void* verts, uint32_t fvf, uint32_t count, int eye,
                    bool walk, float ui_shift, bool placed);
