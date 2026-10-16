// A speech bubble of the game as one card (bubble_card.cpp, docs/ui-layer.md,
// "A speech bubble as one card").
#pragma once

#include <stdint.h>

// The settings: bubble_card, and the test keys bubble_card_test and
// bubble_card_test_frame.
void BubbleCardSetOptions(int on, int test, int test_frame);
void BubbleCardInstall();
// The test key (Shift and Delete in play, test_keys=1).
void BubbleCardToggle(const char* cause);
// stereo.cpp: the eye pass that runs, or -1 after the passes.
void BubbleCardPass(int eye);
void BubbleCardReport();

// For draw_hooks.cpp. True while the game draws a bubble in an eye pass.
bool BubbleCardInBubble();
// True when such a bubble is drawn as a card (bubble_card=1).
bool BubbleCardOn();
// A flat batch of the bubble (its text, its arrows, its icon), with the
// pixels that it has at its draw. `right` is true for the copy of the batch
// that one walk draws for the right eye.
void BubbleCardFlat(const void* verts, uint32_t fvf, uint32_t count,
                    bool right);
// A batch of LH3DRender::DrawTriangle while a bubble draws, and the code
// that asked for it. For the batch of the box, with the card on, each corner
// goes to its pixel and the call is true: then BubbleCardBoxOut puts the
// corners back after the draw.
bool BubbleCardBoxIn(void* verts, uint32_t count, uintptr_t caller);
void BubbleCardBoxOut(void* verts, uint32_t count);
// ONE WALK (one_walk.cpp, one_walk_marks=1). True when the card of this
// bubble is drawn for each eye by itself in the pass of the left eye. `move`
// then gets the pixels from the card of the left eye to the card of the right
// eye: the eye difference of the sign.
bool BubbleCardWalkMove(float* move);
// The corners of the box, at their pixels, go to the card of the right eye.
// BubbleCardBoxOut puts them back.
void BubbleCardBoxRight(void* verts, uint32_t count, float move);
