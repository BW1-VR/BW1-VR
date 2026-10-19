// Where the time of the scene walk goes (profile_sample, a test key). See
// scene_sample.cpp and docs/backend.md, "Where the time of the scene walk
// goes".

#pragma once

#include <stdint.h>

// stereo.cpp, on the frame thread: a scene walk starts (true) or ends
// (false). The first start reads the key and starts the thread of the
// samples.
void SceneSampleWalk(bool in);
// stereo.cpp, each 450 frames, outside the scene walks: the summary of the
// samples since the last one. scene_ms is the time of the scene walks in a
// frame, for the ms of each module.
void SceneSampleReport(uint64_t frames, double scene_ms);
