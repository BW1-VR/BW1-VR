// A count of each call of DirectDraw and Direct3D 7 in a whole session
// (ddraw_census.cpp).

#pragma once

#include <stdint.h>

// ddraw_census=1 (a test key): call it after MH_Initialize and before the
// game makes its DirectDraw object.
void DdrawCensusInstall(int on);
// stereo.cpp: a scene walk starts (true) or ends (false).
void DdrawCensusWalk(bool begin);
void DdrawCensusReport(uint64_t frames);
// d3d7_thread.cpp, on the D3D7 thread: a render state (kind 0) or a state of
// a texture stage (kind 1, stage * 256 + type) gets a value in a walk.
void DdrawCensusState(int kind, uint32_t type, uint32_t value);
