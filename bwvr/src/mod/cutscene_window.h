// The cut scene window (docs/cutscene-window.md). See cutscene_window.cpp.
#pragma once

#include <stdint.h>

// True while the frame draws as the stereoscopic window of play mode 1: the
// game camera with no head on it, the view angle of the game, and the eye
// pictures on a screen in the room. It changes only between two frames, in
// CutsceneWindowEndFrame, so both eye passes of a frame read the same value.
bool CutsceneWindowFlat();

// True while the cinema bars of the game are up at this moment, read from
// the game. It can change in a game turn, between two frames.
bool CutsceneWindowBarsUp();

// stereo.cpp gives the head of each frame, in the LOCAL space of the runtime:
// the middle of the two eyes and the orientation. The screen goes ahead of
// this head when a cut scene starts.
void CutsceneWindowHead(const float center[3], const float orientation[4]);

// stereo.cpp calls this once in a flat frame, before the eye passes. `ipd` is
// the distance between the eyes in meters. True when the depth limit is on
// for the frame. Then `separation` is the distance between the two eye
// cameras in world units, and `ui_shift` is the shift of the flat interface
// in pixels for each eye, which puts it on the screen. False gives the
// stereo as before.
bool CutsceneWindowBeginFlat(float ipd, uint64_t frame, float* separation,
                             float* ui_shift);

// THE NEAREST THING IN VIEW. draw_hooks.cpp calls ProbeStart at the start of
// each eye pass (0 is the left eye). True when the frame wants the world
// batches of that pass, and then each one goes to ProbeBatch: the vertices
// and the indices of LH3DRender::DrawTriangle, and the address that called
// it.
bool CutsceneWindowProbeStart(int eye);
void CutsceneWindowProbeBatch(const void* verts, uint32_t count,
                              const uint16_t* idx, uint32_t n,
                              uint32_t caller);

// THE SUBTITLES. True in a flat frame whose subtitles go to the subtitle card
// (ui_panel.cpp). SubtitleQuad gives the place of the card for the part of
// the render that the game drew its text in: `rect` is x, y, width and height
// in pixels. The card is in the plane of the screen, under the scene.
bool CutsceneWindowSubtitles();
bool CutsceneWindowSubtitleQuad(uint32_t render_w, uint32_t render_h,
                                const int rect[4], float position[3],
                                float orientation[4], float* width,
                                float* height);

// stereo.cpp calls this once a frame, before the frame goes to the headset.
// It gives the screen or the walls of this frame to the runtime, and logs
// each frame of a change. `ipd` is the distance between the eyes in meters.
void CutsceneWindowStage(bool to_headset, uint64_t frame, float ipd);

// stereo.cpp calls this once a frame, after the frame went out. It reads the
// cinema state of the game and sets the state of the next frame. It returns
// the frame to save the eye pictures of, for a test, or 0.
uint64_t CutsceneWindowEndFrame(uint64_t frame);

// The test keys (with test_keys=1). Numpad 0: the window off and on. Numpad
// 1: the depth limit off and on.
void CutsceneWindowToggle();
void CutsceneWindowDepthToggle();

void CutsceneWindowReport();
