// The render thread (render_thread.cpp, docs/backend.md, "The render
// thread"). No Direct3D types here, so that files with the Direct3D 7
// headers can include it.

#pragma once

#include <stdint.h>

// render_thread of bwvr.ini: 0 gives each call of dxwrapper to Direct3D 9 as
// it comes, 1 gives the calls of a scene walk to a thread of their own, 2
// keeps them and gives them to Direct3D 9 on the frame thread at each stop
// (a test of the record). test: render_thread_test_frame. The ini value of 1
// or 2 also asks for a device that permits calls from two threads.
// render_thread_lock_free: 1 gives the type of a texture with no lock of the
// device, 0 gives each GetType to Direct3D 9.
// render_thread_filter: 1 drops a call of a walk that sets the value that the
// device has already, 0 gives it to Direct3D 9. profile_gpu=1 (a test key)
// measures the time of each scene walk on the GPU.
// render_thread_buffers: 1 draws the DrawIndexedPrimitiveUP of a walk from
// a vertex buffer and an index buffer of the render thread.
// gpu_flush=N: each N draws of a walk the driver gives the calls
// so far to the GPU. 0: when the driver wants.
void RenderThreadSetOptions(int mode, int test_frame, int lock_free,
                            int filter, int buffers, int flush,
                            int profile_gpu);
// The test key: gpu_flush goes between 0 and the value of the ini
// (or 128).
void RenderThreadFlushToggle(const char* keys);
// The test key: render_thread_buffers goes between 1 and 0.
void RenderThreadBuffersToggle(const char* keys);
// The test key: render_thread_filter goes between 1 and 0.
void RenderThreadFilterToggle(const char* keys);
// The test key: the mode goes between 0 and the value of the ini (or 1).
void RenderThreadToggle(const char* keys);
// The test key: render_thread_lock_free goes between 1 and 0.
void RenderThreadLockFreeToggle(const char* keys);
// Each frame of the scene, before the eye passes. `frame` is the frame of the
// controllers. True when the test of the pictures wants a picture of each eye
// of this frame.
bool RenderThreadFrame(uint64_t frame);
// stereo.cpp: a scene walk starts (true) or ends (false). The end waits until
// each call of the walk went to Direct3D 9.
void RenderThreadWalk(bool begin);
// A call that reads or writes the pixels of a surface, or that changes a
// texture, on the frame thread: each call of the walk before it goes to
// Direct3D 9 first (draw_merge.cpp, frame_profile.cpp, present_watch.cpp).
void RenderThreadSync();
void RenderThreadReport(uint64_t frames);
// The id of the render thread, or 0 before it starts (scene_sample.cpp).
unsigned long RenderThreadWorkerId();
// True in a walk whose calls go to the render thread (render_thread=1).
bool RenderThreadQueueing();
// render_thread_late_end: 1 (the default) does not wait at the end of a walk
// until the render thread made each call of it. The calls after the walk go
// into the queue, and the start of the next walk waits for it. 0: the end of
// the walk waits.
void RenderThreadSetLateEnd(bool on);
bool RenderThreadLateEnd();
// The test key: render_thread_late_end goes between 1 and 0.
void RenderThreadLateEndToggle(const char* keys);
// gpu_bridge.cpp, frame_send=2: the event query of the end of a frame goes
// into the open queue after a walk, and the render thread issues it with
// D3DGETDATA_FLUSH. Gives its number, or 0 when the frame thread must issue
// it itself (no open queue).
unsigned long RenderThreadQueueFlush(void* query);
// The second thread of frame_send: waits until the render thread issued the
// query of that number. False after limit_ms.
bool RenderThreadWaitFlushed(unsigned long number, unsigned limit_ms);
// ab_test=<key> (a test key): each window of the report goes to the other
// value of the key: gpu_flush or render_thread_right_eye.
void RenderThreadSetAbTest(const char* key);
// render_thread_right_eye: 1 makes the copy of a batch for the right eye in
// one walk on the render thread, 0 on the frame thread.
void RenderThreadSetRightEye(bool on);
// The test key: render_thread_right_eye goes between 1 and 0.
void RenderThreadRightEyeToggle(const char* keys);
