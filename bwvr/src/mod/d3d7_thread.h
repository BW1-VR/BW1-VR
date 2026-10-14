// The calls of Direct3D 7 of a scene walk on a thread of their own
// (d3d7_thread.cpp, docs/backend.md, "The thread of Direct3D 7").

#pragma once

#include <stdint.h>

// d3d7_thread of bwvr.ini: 1 gives the calls of the device of a walk to the
// D3D7 thread, with render_thread=1. 0: the frame thread gives them to
// dxwrapper.
void D3D7ThreadSetOptions(int on);
bool D3D7ThreadOn();
void D3D7ThreadSet(bool on);
// The test key: d3d7_thread goes between 1 and 0.
void D3D7ThreadToggle(const char* keys);
// stereo.cpp: a walk starts (true, after RenderThreadWalk(true)) or ends
// (false, before RenderThreadWalk(false)). The end waits until the D3D7
// thread did each call.
void D3D7ThreadWalk(bool begin, bool render_thread);
// True in a walk with the queue.
bool D3D7ThreadActive();
// True on the D3D7 thread.
bool D3D7ThreadOnWorker();
unsigned long D3D7ThreadWorkerId();
// On the frame thread in a walk with the queue: waits until the D3D7 thread
// did each call before this one.
void D3D7ThreadStop();
// A record that calls fn(copy of data) on the D3D7 thread, in the order of
// the calls. False when no walk with the queue runs on this thread: then the
// caller calls fn itself.
typedef void (*D3D7ThreadFn)(const void* data);
bool D3D7ThreadPost(D3D7ThreadFn fn, const void* data, unsigned bytes);
void D3D7ThreadReport(uint64_t frames);
