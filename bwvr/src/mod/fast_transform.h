// The two vertex loops of the engine with SSE (fast_transform.cpp,
// docs/backend.md, "The vertex loops with SSE").

#pragma once

#include <stdint.h>

// fast_transform of bwvr.ini: 1 runs the loops with SSE, 0 on the x87 unit.
void FastTransformSetOptions(int on);
// fast_transform_check=1 (a test key): each loop runs on the x87 unit, and
// the mod compares a copy that the SSE loop makes with it.
void FastTransformSetCheck(int on);
void FastTransformInstall();
bool FastTransformOn();
void FastTransformSet(bool on);
// The test key: fast_transform goes between 1 and 0.
void FastTransformToggle(const char* keys);
void FastTransformReport(uint64_t frames);
