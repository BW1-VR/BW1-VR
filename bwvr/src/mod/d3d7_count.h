// A count of the calls of Direct3D 7 in the scene walks (d3d7_count.cpp).

#pragma once

#include <stdint.h>

// d3d7_count=1 (a test key) counts each call.
void D3D7CountSetOptions(int on);
// stereo.cpp: a scene walk starts (true) or ends (false).
void D3D7CountWalk(bool begin);
void D3D7CountReport(uint64_t frames);
