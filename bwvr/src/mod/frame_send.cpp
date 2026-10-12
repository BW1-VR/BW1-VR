// The way of the eye pictures to the headset: frame_send.
//
// The game draws an eye picture with the Direct3D 9 device of the backend.
// The headset gets the picture from the Direct3D 11 device of the OpenXR
// session, through a shared texture. The two devices have no common queue,
// so the mod waits for the GPU before the Direct3D 11 device reads a shared
// texture (gpu_bridge.cpp, "WHY THE FLUSH IS NEEDED").
//
//   frame_send=0  The default. The mod waits after each eye pass, and then
//                 it copies the picture of that eye to the headset. The
//                 frame thread does no work in the two waits of a frame.
//   frame_send=1  The mod waits one time, after the second eye pass. The GPU
//                 draws the first eye while the game walks the scene for the
//                 second eye. Each eye has its own shared texture, so the
//                 second eye pass does not write the picture of the first.
//   frame_send=2  As 1, and a second thread does the wait, the two copies
//                 and xrEndFrame (XrScreen::EndFrameLate). The
//                 frame thread goes on with the game and calls xrWaitFrame
//                 for the next frame. Then it waits until the runtime has
//                 the frame.
//
// The pixels of a frame are the same with each value. The frame goes to the
// runtime at the same point of the frame or sooner, so the value adds no
// time between the pose and the picture.
//
// frame_send=2 needs a Direct3D 9 device that permits calls from two threads:
// the second thread reads the event query while the frame thread can draw.
// On a device for one thread the value acts as 1. With frame_send=2 or the
// test keys in the ini, the mod asks the backend for such a device
// (gpu_bridge.cpp, GpuBridgeWantTwoThreads).
//
// WHAT MUST NOT HAPPEN. While the second thread has a frame, it reads the
// two shared textures and the event query. So each code path that writes or
// frees them calls FrameSendJoin first: the capture of a menu frame, the end
// of the bridge, and a device that the backend destroys.
//
// The test key (Ctrl, Shift and the slash key of the number pad, with
// test_keys=1) steps the value: 0, 1, 2. A long pulse says 0, a pulse of
// middle length says 1, and a short pulse says 2.
//
// Only a session in a headset shows the frame rate of each value. A desktop
// run sends no frame to a runtime. The numbers of the headset sessions are in
// docs/backend.md, "One wait a frame, and a second thread".

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "../vr/xr_screen.h"

extern void ModLog(const char* format, ...);
// gpu_bridge.cpp
bool GpuBridgeReady();
bool GpuBridgeTwoThreads();
void GpuBridgeWaitHere();
void GpuBridgeWaitBegin();
void GpuBridgeWaitOnThread();
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
uint64_t ControllersFrameCount();

namespace {

const int kEachEye = 0;
const int kOneWait = 1;
const int kSecondThread = 2;

int g_key = kSecondThread;
bwvr::XrScreen* g_screen = nullptr;
// frame_send=2 on a device for one thread gets one line of the log.
bool g_said_one_thread = false;

const char* KeyText(int key) {
  if (key == kOneWait) {
    return "one wait for the GPU in a frame, after the second eye pass";
  }
  if (key == kSecondThread) {
    return "one wait for the GPU in a frame, on a second thread that also "
           "gives the frame to the runtime";
  }
  return "a wait for the GPU after each eye pass";
}

}  // namespace

void FrameSendSetOptions(int key) {
  g_key = key == kEachEye || key == kOneWait ? key : kSecondThread;
  ModLog("frame send   : frame_send=%d, %s", g_key, KeyText(g_key));
}

void FrameSendSetScreen(bwvr::XrScreen* screen) { g_screen = screen; }

// The caller reads the value one time for a frame, before the eye passes.
int FrameSendMode() { return g_key; }

void FrameSendCycle(const char* cause) {
  g_key = g_key == kSecondThread ? kEachEye : g_key + 1;
  ControllersGesturePulse(
      0.5f, g_key == kEachEye ? 0.30f : g_key == kOneWait ? 0.15f : 0.06f);
  ModLog("frame send   : %s: frame_send is %d now (%s), frame %llu", cause,
         g_key, KeyText(g_key), ControllersFrameCount());
}

// Ends a frame whose eye pictures are in the shared textures, with one wait
// for the GPU. `mode` is the value that the caller read before the eye
// passes. Returns the ticks that this thread waited for the GPU.
uint64_t FrameSendEnd(const bwvr::LateFrame& pictures, int mode) {
  if (g_screen == nullptr) return 0;
  // The bridge can stop in an eye pass. It freed the textures then, so the
  // frame goes out with the pictures that it has.
  if (!GpuBridgeReady()) {
    g_screen->EndFrame();
    return 0;
  }
  bwvr::LateFrame frame = pictures;
  bool on_thread = mode == kSecondThread;
  if (on_thread && !GpuBridgeTwoThreads()) {
    on_thread = false;
    if (!g_said_one_thread) {
      g_said_one_thread = true;
      ModLog("frame send   : the Direct3D 9 device does not permit calls "
             "from two threads, so the frame thread waits for the GPU, as "
             "with frame_send=1");
    }
  }
  if (on_thread) {
    // This thread puts the end of the frame in the queue of the GPU, and
    // the second thread waits for it.
    GpuBridgeWaitBegin();
    frame.wait = &GpuBridgeWaitOnThread;
  } else {
    frame.wait = &GpuBridgeWaitHere;
  }
  g_screen->EndFrameLate(frame, on_thread);
  return g_screen->LateWaitTicks();
}

// True while the second thread has a frame.
bool FrameSendBusy() { return g_screen != nullptr && g_screen->sending(); }

// Returns when the second thread has no frame.
void FrameSendJoin() {
  if (g_screen != nullptr) g_screen->Join();
}
