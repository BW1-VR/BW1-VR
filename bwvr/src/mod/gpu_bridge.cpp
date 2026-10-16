// The finished frame, handed to OpenXR without the CPU touching a pixel.
//
// WHAT THIS REPLACES
//
// System DirectDraw keeps the render target where only the CPU can read it.
// To reach the headset the mod had to lock that surface, which drags the
// whole picture out of graphics memory, then write it into an upload texture,
// which pushes the whole picture back in again. Two crossings of the bus per
// eye per frame, and the cost grows with every pixel:
//
//     1280x720   0.41 ms          1920x1080  0.83 ms
//     1600x900   0.58 ms          2560x1440  1.34 ms       (the lock alone)
//
// The game's own drawing does not grow with resolution at all. It stayed
// between 4.33 and 6.52 ms at every resolution measured, because it transforms
// vertices, not pixels. So the round trip is the only thing standing between
// this mod and a headset native resolution.
//
// HOW IT IS REMOVED
//
// A Direct3D 7 backend that runs on Direct3D 9 keeps the frame in a real
// Direct3D 9 surface. A Direct3D 9Ex device can create a texture with a
// shared handle, and Direct3D 11 can open that handle and read the same
// memory. So the frame never leaves the graphics card:
//
//     game draws  ->  Direct3D 9 render target
//                     StretchRect       (graphics memory to graphics memory)
//                 ->  shared texture
//                     CopyResource      (graphics memory to graphics memory)
//                 ->  OpenXR swapchain image
//
// Both copies are queued on the GPU. The CPU only issues them.
//
// WHY THE FLUSH IS NEEDED
//
// The two devices have separate command queues and a shared texture carries
// no automatic synchronization. Without a fence the Direct3D 11 copy could
// run before the Direct3D 9 one finished, and the headset would show the
// previous frame torn into the current one. An event query after the
// StretchRect makes the CPU wait until the GPU has finished the Direct3D 9
// work. That costs latency, not bandwidth, and the profile says the GPU sits
// idle in this game, so the wait should be short. It is measured, not assumed.
//
// EVERYTHING HERE FAILS SAFE
//
// If the backend is not on Direct3D 9, if the device is not 9Ex, if the
// shared texture cannot be made or opened, or if the swapchain format does
// not match, GpuBridgeReady stays false and the caller keeps using the CPU
// path. A slow picture is better than no picture.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Only the modern headers here. Direct3D 7 and Direct3D 9 declare the same
// enums with different values and cannot share a file, so anything that
// needs the old headers lives in interop_probe.cpp.
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "render_thread.h"
// d3d9_backend.cpp: true when dxwrapper draws with DXVK.
bool D3D9BackendDxvk();
#include "thread_wait.h"
#include "ui_panel.h"
#include "vk_scene.h"

extern void ModLog(const char* format, ...);
// comfort_camera.cpp: true for a moment after a snap turn or a jump.
bool ComfortBlinkActive();
// frame_send.cpp: the second thread that gives a frame to the runtime. While
// it has a frame, it reads the event query and the shared textures.
bool FrameSendBusy();
void FrameSendJoin();
// draw_merge.cpp: the batch of the merge of draw calls. The calls of this
// file on the Direct3D 9 device do not go through the hooks of the merge, so
// the batch goes to Direct3D before each of them.
void DrawMergeFlush();
// one_walk.cpp: the game's own target in place of the wide target of one walk,
// and the end of the wide target when the device is gone.
void* OneWalkGameSurface(void* surface);
void OneWalkForgetDevice();

namespace {

const int kEyes = 2;

bool g_installed = false;
bool g_enabled = false;   // the mod asked for the GPU path
bool g_ready = false;     // and everything needed for it exists

// Captured from the backend as it builds its device.
IDirect3DDevice9* g_device = nullptr;
IDirect3DDevice9Ex* g_device_ex = nullptr;
// The newest device of the backend is a full screen one.
bool g_full_screen = false;

// One shared texture per eye. Two, not one, because the Direct3D 11 copy of
// the left eye is still queued when the right eye is captured, and a single
// texture would be overwritten underneath it.
IDirect3DTexture9* g_shared[kEyes] = {nullptr, nullptr};
IDirect3DSurface9* g_shared_surface[kEyes] = {nullptr, nullptr};
ID3D11Texture2D* g_opened[kEyes] = {nullptr, nullptr};
IDirect3DQuery9* g_flush_query = nullptr;
// g_flush_query was issued at least one time on this device, so its state
// says if the GPU did the last frame end.
bool g_flush_issued = false;
// The device permits calls from two threads (D3DCREATE_MULTITHREADED).
bool g_two_threads = false;
// The mod asks the backend for such a device (GpuBridgeWantTwoThreads).
bool g_two_threads_wanted = false;
// The wait of the second thread for the GPU (GpuBridgeWaitOnThread). The
// second thread writes these. The frame thread reads them in the next frame,
// when the second thread has no frame.
bool g_thread_waited = false;
// With the late end of the render thread (render_thread.cpp) the render
// thread issues the event query of the frame, in the order of its queue.
// This is its number, or 0 when the frame thread issued it.
unsigned long g_flush_number = 0;
bool g_thread_gave_up = false;
uint64_t g_thread_wait_ticks = 0;
uint32_t g_thread_polls = 0;
// thread_wait=1: the time from the issue of the query to the end of the GPU
// in the last frames, and the sleep before the polls.
const int kRecentWaits = 8;
LONGLONG g_recent_waits[kRecentWaits] = {0};
int g_recent_at = 0;
uint64_t g_thread_sleep_ticks = 0;
int g_gave_up_lines = 0;

uint32_t g_width = 0;
uint32_t g_height = 0;

// Timing, because the point of this file is a number going down.
LARGE_INTEGER g_frequency = {0};
uint64_t g_blit_ticks = 0;
uint64_t g_flush_ticks = 0;
uint64_t g_captures = 0;
uint64_t g_flush_spins = 0;
uint64_t g_failures = 0;

typedef HRESULT(WINAPI* Direct3DCreate9ExFn)(UINT, IDirect3D9Ex**);
typedef IDirect3D9*(WINAPI* Direct3DCreate9Fn)(UINT);
typedef HRESULT(STDMETHODCALLTYPE* CreateDeviceFn)(IDirect3D9*, UINT,
                                                   D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*,
                                                   IDirect3DDevice9**);
typedef HRESULT(STDMETHODCALLTYPE* CreateDeviceExFn)(IDirect3D9Ex*, UINT,
                                                     D3DDEVTYPE, HWND, DWORD,
                                                     D3DPRESENT_PARAMETERS*,
                                                     D3DDISPLAYMODEEX*,
                                                     IDirect3DDevice9Ex**);

Direct3DCreate9ExFn o_Direct3DCreate9Ex = nullptr;
Direct3DCreate9Fn o_Direct3DCreate9 = nullptr;
CreateDeviceFn o_CreateDevice = nullptr;
CreateDeviceExFn o_CreateDeviceEx = nullptr;

bool g_device_hooked = false;

// THE BACKEND DESTROYS ITS DEVICE, IT DOES NOT JUST RELEASE IT
//
// dxwrapper makes a new Direct3D 9 device when the window mode or the window
// changes. The game does this when it starts full screen. To free the old
// device, dxwrapper calls Release until the count is zero (ReleaseD9Device in
// IDirectDrawX.cpp). That removes our references too, so a reference does not
// keep the device alive. A call on the old device after a rebuild crashes the
// game.
//
// So a hook on the device's Release tells the bridge when the device is gone.
// The bridge then drops everything it made on that device and calls none of
// it. The creation hooks give it the new device, and GpuBridgeResume starts
// the shared path again on that device.
typedef ULONG(STDMETHODCALLTYPE* DeviceReleaseFn)(IDirect3DDevice9*);
DeviceReleaseFn o_DeviceRelease = nullptr;
void* g_hooked_release = nullptr;

// THE 24 BIT DEPTH BUFFER (docs/backend.md, "The depth buffer").
//
// The game asks the backend for the Z buffer formats it can make, keeps the
// ones with no stencil, and tries them in the order they came. The backend
// lists 16 bit first, so the game gets 16 bit. With depth24=1 the callback
// that the game gives to that list moves the 24 bit entry (D24X8) to the
// front. The game then asks for 24 bit by itself, and the backend stays in
// step with what it made.
//
// The game cannot fall back. Its loop only makes the DirectDraw surface, and
// dxwrapper makes the Direct3D 9 surface later, at the first use. So the hook
// on CreateDepthStencilSurface checks the adapter, and if the adapter cannot
// make D24X8, it makes D16 in its place. It also keeps the depth that the
// game got, for the near plane (near_plane.cpp), and logs it.
const uintptr_t kZFormatCallback = 0x0082B370;  // the game's enumeration callback
const uintptr_t kZFormatTable = 0x00EC7E88;     // DDPIXELFORMAT entries
const uintptr_t kZFormatCount = 0x00ECA674;     // how many entries, at most 4
const int kZFormatBytes = 32;
// Fields of DDPIXELFORMAT. 0x10 is dwStencilBitDepth, not the mask. The first
// build read the mask there, found 0, and never moved the entry.
const int kFormatFlags = 0x04;  // dwFlags
const int kFormatBits = 0x0C;   // dwZBufferBitDepth
const int kFormatMask = 0x14;   // dwZBitMask
const DWORD kZBufferOnly = 0x400;               // DDPF_ZBUFFER
typedef HRESULT(WINAPI* ZFormatCallbackFn)(unsigned char*, void*);
ZFormatCallbackFn o_ZFormatCallback = nullptr;
bool g_depth24 = false;         // depth24=1 in bwvr.ini
bool g_depth24_hooked = false;  // the hook on the game's callback is in
bool g_said_moved = false;
int g_depth_logs = 0;
// The depth of the game's Z buffer in bits, from the last surface made for
// the game. 0 until the first one.
int g_game_depth_bits = 0;
// The last create that was logged, so a log line comes only for a change.
D3DFORMAT g_logged_asked = D3DFMT_UNKNOWN;
D3DFORMAT g_logged_made = D3DFMT_UNKNOWN;
UINT g_logged_width = 0;
UINT g_logged_height = 0;
// True while the mod makes the depth surface of its own interface panel.
// That surface is not the game's.
bool g_making_own_depth = false;
typedef HRESULT(STDMETHODCALLTYPE* CreateDepthFn)(
    IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
    IDirect3DSurface9**, HANDLE*);
CreateDepthFn o_CreateDepth = nullptr;
void* g_hooked_create_depth = nullptr;

// What GpuBridgeStart was given, so that the bridge can start again on a new
// device.
void* g_start_d3d11 = nullptr;
uint32_t g_start_format = 0;
bool g_resume_wanted = false;
uint32_t g_devices_lost = 0;

// THE INTERFACE PANEL (docs/ui-layer.md, "Drawing the interface to its own
// target").
//
// The screen interface draws into a render target of its own. The mod
// switches the Direct3D 9 target around those draws. Dd7to9 keeps its own
// copy of the target, the viewport and the render states, and it does not
// compare them with the device. So the switch saves what it changes and puts
// back exactly that: target 0, the depth surface, the viewport, the scissor
// rect and the three separate alpha states.
bool g_panel_wanted = false;  // ui_panel is not 0
bool g_panel_share = false;   // ui_panel is 1, so the panel goes to OpenXR
bool g_panel_failed = false;  // a create failed, so the panel stays off
IDirect3DSurface9* g_panel = nullptr;        // A8R8G8B8, the render size
IDirect3DSurface9* g_panel_depth = nullptr;  // its own depth, no multisampling
bool g_panel_stencil = false;                // the depth format has a stencil
uint32_t g_panel_width = 0;
uint32_t g_panel_height = 0;
// The share to OpenXR, made in GpuBridgeStart like the eyes.
IDirect3DTexture9* g_panel_shared = nullptr;
IDirect3DSurface9* g_panel_shared_surface = nullptr;
ID3D11Texture2D* g_panel_opened = nullptr;
// The composite check: the eye before the first panel batch (spare),
// and the same picture with the panel batches drawn the game's way (check).
IDirect3DSurface9* g_spare = nullptr;
IDirect3DSurface9* g_check = nullptr;
IDirect3DSurface9* g_check_depth = nullptr;
IDirect3DSurface9* g_readback = nullptr;  // system memory, for a BMP
bool g_read_locked = false;
// The subtitle card (docs/ui-layer.md, "The subtitle card"): a part of the
// panel at half the size, for the CPU. GetRenderTargetData copies a whole
// surface, so the copy goes through a target that is only as large as the
// part, on a grid of kHalfGrid pixels. A few sizes are kept: the subtitle
// strip, the Creature panel, and both.
const uint32_t kHalfGrid = 128;
const int kHalfTargets = 6;
struct HalfTarget {
  IDirect3DSurface9* target = nullptr;
  IDirect3DSurface9* read = nullptr;  // system memory
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t used = 0;                  // the clock of its last copy
};
HalfTarget g_half[kHalfTargets];
uint64_t g_half_clock = 0;
IDirect3DSurface9* g_half_locked = nullptr;
uint64_t g_half_reads = 0;
uint64_t g_half_made = 0;
uint64_t g_half_bytes = 0;
uint64_t g_half_failures = 0;
// THE READ AHEAD OF THE SUBTITLE CARD (subtitle_read_ahead=1). The GPU does
// the calls of the device in the order of its queue. So GetRenderTargetData
// waits until the GPU has done each call before it, and a read after the eye
// passes waits for both eye passes. With frame_send 1 and 2 the frame thread
// does not wait for the GPU at all, so that read was the only wait of the
// frame thread. So the copy of a frame goes into a slot with an event query
// after it (GpuBridgePanelQueueHalf). At the start of a later frame, the read
// of the slot (GpuBridgePanelFetchHalf) is made only when its query and the
// query of the last frame end are done: then the GPU has no work left, and
// the read is only the copy itself. The card shows the help text of one or
// two frames before.
const int kAheadSlots = 3;
const int kSlotFree = 0;
const int kSlotQueued = 1;  // the copy and the query are in the queue
const int kSlotRead = 2;    // the pixels are in system memory
struct AheadSlot {
  IDirect3DSurface9* target = nullptr;
  IDirect3DSurface9* read = nullptr;  // system memory
  IDirect3DQuery9* query = nullptr;
  uint32_t width = 0;   // of the two surfaces
  uint32_t height = 0;
  uint32_t out_w = 0;   // of the copy in them
  uint32_t out_h = 0;
  int rect[4] = {0, 0, 0, 0};  // the rect of the panel that the copy holds
  uint64_t frame = 0;          // the frame of the copy
  uint64_t order = 0;          // the clock of the copy
  int state = kSlotFree;
};
AheadSlot g_ahead[kAheadSlots];
uint64_t g_ahead_clock = 0;
uint64_t g_ahead_queued = 0;
uint64_t g_ahead_reads = 0;
uint64_t g_ahead_waits = 0;    // frame starts with a copy not done yet
uint64_t g_ahead_dropped = 0;  // copies that were never read
uint64_t g_ahead_failures = 0;

// The state of the switch. g_bound is -1 when the game's target is bound.
int g_bound = -1;
// backend=vulkan: the panel of the Vulkan renderer is bound (VkScenePanelBind).
bool g_vk_bound = false;
IDirect3DSurface9* g_saved_target = nullptr;
IDirect3DSurface9* g_saved_depth = nullptr;
D3DVIEWPORT9 g_saved_viewport;
RECT g_saved_scissor;
DWORD g_saved_alpha[3] = {0, 0, 0};
const D3DRENDERSTATETYPE kAlphaStates[3] = {D3DRS_SEPARATEALPHABLENDENABLE,
                                            D3DRS_SRCBLENDALPHA,
                                            D3DRS_DESTBLENDALPHA};
// OpenXR reads the panel color as premultiplied. The game's usual blend gives
// that for the color. These three give "over" for the alpha too.
const DWORD kPanelAlpha[3] = {TRUE, D3DBLEND_ONE, D3DBLEND_INVSRCALPHA};

// Per frame: the first bind of each target clears it.
bool g_panel_cleared = false;
bool g_check_cleared = false;
bool g_check_wanted = false;
bool g_check_ready = false;  // the spare and the check hold this frame's eye

// For the log.
uint64_t g_panel_binds = 0;
uint64_t g_panel_rebinds = 0;
uint64_t g_panel_clears = 0;
uint64_t g_panel_copies = 0;
uint64_t g_panel_bind_failures = 0;
bool g_panel_logged_target = false;

// THE PICTURE OF EACH EYE, KEPT FOR ITS NEXT FRAME (intro_vr.cpp, docs/
// intro.md "The trails", and eye_clean.cpp, docs/stereo-artifacts.md "The
// frame before of each eye"). The Lionhead logo does not clear its picture:
// it draws each frame over the frame before, so each thing has a trail. A
// frame of the scene that draws no sky does the same. The two eye passes
// share one render target, so an eye pass would draw over the picture of the
// other eye. A keep holds the last picture of one eye, with the size, the
// format and the multisampling of the game's render target, so a copy
// changes no sample.
IDirect3DSurface9* g_keep[kEyes] = {nullptr, nullptr};
bool g_keep_filled[kEyes] = {false, false};
// The render target that the keeps came from. The mod holds a reference.
IDirect3DSurface9* g_keep_target = nullptr;
D3DSURFACE_DESC g_keep_desc = {};
bool g_keep_failed = false;
uint64_t g_keep_copies = 0;

// The device is gone, so its objects are dropped, not released.
void ForgetPanel() {
  for (int eye = 0; eye < kEyes; ++eye) {
    g_keep[eye] = nullptr;
    g_keep_filled[eye] = false;
  }
  g_keep_target = nullptr;
  g_panel = nullptr;
  g_panel_depth = nullptr;
  g_panel_shared = nullptr;
  g_panel_shared_surface = nullptr;
  g_spare = nullptr;
  g_check = nullptr;
  g_check_depth = nullptr;
  g_readback = nullptr;
  g_read_locked = false;
  for (int i = 0; i < kHalfTargets; ++i) g_half[i] = HalfTarget();
  for (int i = 0; i < kAheadSlots; ++i) g_ahead[i] = AheadSlot();
  g_half_locked = nullptr;
  g_saved_target = nullptr;
  g_saved_depth = nullptr;
  g_bound = -1;
  g_panel_width = 0;
  g_panel_height = 0;
  if (g_panel_opened != nullptr) {
    g_panel_opened->Release();
    g_panel_opened = nullptr;
  }
}

// Forgets the device and everything made on it. The Direct3D 9 objects died
// with the device, so they are dropped, not released. The Direct3D 11 objects
// belong to the headset's device and are still safe to release.
void ForgetDevice() {
  ForgetPanel();
  OneWalkForgetDevice();
  if (g_ready) g_resume_wanted = true;
  g_ready = false;
  g_flush_query = nullptr;
  g_flush_issued = false;
  for (int eye = 0; eye < kEyes; ++eye) {
    g_shared_surface[eye] = nullptr;
    g_shared[eye] = nullptr;
    if (g_opened[eye] != nullptr) {
      g_opened[eye]->Release();
      g_opened[eye] = nullptr;
    }
  }
  g_device = nullptr;
  g_device_ex = nullptr;
}

ULONG STDMETHODCALLTYPE h_DeviceRelease(IDirect3DDevice9* self) {
  // The second thread makes calls on the device while it waits for the GPU.
  // When this count is the last one of the device, the second thread ends
  // its frame first. The hook does not wait at each other count: Direct3D 9
  // can have the lock of the device at this call, and the second thread
  // needs that lock to end its frame.
  if (self == g_device && FrameSendBusy()) {
    self->AddRef();
    if (o_DeviceRelease(self) <= 1) FrameSendJoin();
  }
  const ULONG left = o_DeviceRelease(self);
  if (left == 0 && self != nullptr && self == g_device) {
    ForgetDevice();
    ++g_devices_lost;
    ModLog("gpu bridge   : the backend destroyed its Direct3D 9 device");
  }
  return left;
}

// IUnknown slot 2 is Release. All devices of one class share the function, so
// one hook covers every device the backend makes.
void HookDeviceRelease(IDirect3DDevice9* device) {
  void* release = (*reinterpret_cast<void***>(device))[2];
  if (release == g_hooked_release) return;
  if (g_hooked_release != nullptr) {
    ModLog("gpu bridge   : a device of a different class, so the bridge "
           "cannot see when it is destroyed");
    return;
  }
  if (MH_CreateHook(release, reinterpret_cast<void*>(&h_DeviceRelease),
                    reinterpret_cast<void**>(&o_DeviceRelease)) == MH_OK &&
      MH_EnableHook(release) == MH_OK) {
    g_hooked_release = release;
  } else {
    ModLog("gpu bridge   : the device's Release could not be hooked");
  }
}

const char* DepthName(D3DFORMAT format) {
  switch (format) {
    case D3DFMT_D16: return "D16";
    case D3DFMT_D16_LOCKABLE: return "D16_LOCKABLE";
    case D3DFMT_D24X8: return "D24X8";
    case D3DFMT_D24S8: return "D24S8";
    case D3DFMT_D32: return "D32";
    default: return "other";
  }
}

// Asks the adapter if it can make this 24 bit format with the multisample
// type of the request, next to the current render target. Returns null when
// it can, or the reason when it cannot.
const char* CheckDepth24(IDirect3DDevice9* device, D3DFORMAT format,
                         D3DMULTISAMPLE_TYPE multisample) {
  IDirect3D9* d3d = nullptr;
  if (FAILED(device->GetDirect3D(&d3d)) || d3d == nullptr) return nullptr;
  D3DDEVICE_CREATION_PARAMETERS creation;
  D3DDISPLAYMODE mode;
  if (FAILED(device->GetCreationParameters(&creation)) ||
      FAILED(device->GetDisplayMode(0, &mode))) {
    d3d->Release();
    return nullptr;  // cannot ask, so the backend tries
  }
  D3DFORMAT target = D3DFMT_X8R8G8B8;
  IDirect3DSurface9* surface = nullptr;
  if (SUCCEEDED(device->GetRenderTarget(0, &surface)) && surface != nullptr) {
    D3DSURFACE_DESC desc;
    if (SUCCEEDED(surface->GetDesc(&desc))) target = desc.Format;
    surface->Release();
  }
  BOOL windowed = TRUE;
  IDirect3DSwapChain9* chain = nullptr;
  if (SUCCEEDED(device->GetSwapChain(0, &chain)) && chain != nullptr) {
    D3DPRESENT_PARAMETERS present;
    if (SUCCEEDED(chain->GetPresentParameters(&present))) {
      windowed = present.Windowed;
    }
    chain->Release();
  }
  const char* why = nullptr;
  if (FAILED(d3d->CheckDeviceFormat(creation.AdapterOrdinal,
                                    creation.DeviceType, mode.Format,
                                    D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE,
                                    format))) {
    why = "the adapter does not offer this depth format";
  } else if (FAILED(d3d->CheckDepthStencilMatch(
                 creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                 target, format))) {
    why = "the depth format does not match the render target format";
  } else if (multisample != D3DMULTISAMPLE_NONE &&
             FAILED(d3d->CheckDeviceMultiSampleType(
                 creation.AdapterOrdinal, creation.DeviceType, format, windowed,
                 multisample, nullptr))) {
    why = "the depth format has no multisample support at this type";
  }
  d3d->Release();
  return why;
}

int DepthBits(D3DFORMAT format) {
  switch (format) {
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
    case D3DFMT_D15S1: return 16;
    case D3DFMT_D24X8:
    case D3DFMT_D24S8:
    case D3DFMT_D24X4S4: return 24;
    case D3DFMT_D32: return 32;
    default: return 0;
  }
}

// Every depth surface of the device comes here. For the game's Z buffer, a
// D24X8 request that the adapter cannot make becomes D16, because the game
// cannot try its next format at this point. The interface panel makes its
// own surface and tries its own formats, so its creates go through as asked.
HRESULT STDMETHODCALLTYPE h_CreateDepth(IDirect3DDevice9* self, UINT width,
                                        UINT height, D3DFORMAT format,
                                        D3DMULTISAMPLE_TYPE multisample,
                                        DWORD quality, BOOL discard,
                                        IDirect3DSurface9** out,
                                        HANDLE* shared) {
  // A new surface in a scene walk: each call of the walk goes to Direct3D 9
  // first (render_thread.cpp).
  RenderThreadSync();
  if (g_making_own_depth) {
    return o_CreateDepth(self, width, height, format, multisample, quality,
                         discard, out, shared);
  }
  D3DFORMAT made = format;
  const char* why = nullptr;
  if (format == D3DFMT_D24X8) {
    why = CheckDepth24(self, format, multisample);
    if (why != nullptr) made = D3DFMT_D16;
  }
  const HRESULT hr = o_CreateDepth(self, width, height, made, multisample,
                                   quality, discard, out, shared);
  if (SUCCEEDED(hr)) g_game_depth_bits = DepthBits(made);
  const bool changed = format != g_logged_asked || made != g_logged_made ||
                       width != g_logged_width || height != g_logged_height;
  if ((changed || FAILED(hr)) && g_depth_logs < 8) {
    ++g_depth_logs;
    g_logged_asked = format;
    g_logged_made = made;
    g_logged_width = width;
    g_logged_height = height;
    if (FAILED(hr)) {
      ModLog("depth24      : the game asked for %s, %ux%u, multisample type "
             "%d. The create of %s failed (0x%08lX)", DepthName(format), width,
             height, static_cast<int>(multisample), DepthName(made),
             static_cast<unsigned long>(hr));
    } else {
      ModLog("depth24      : the game asked for %s, %ux%u, multisample type "
             "%d. The Z buffer is %s, %d bit%s%s", DepthName(format), width,
             height, static_cast<int>(multisample), DepthName(made),
             DepthBits(made), why != nullptr ? ". Not 24 bit: " : "",
             why != nullptr ? why : "");
    }
  }
  return hr;
}

// CreateDepthStencilSurface is slot 29 of IDirect3DDevice9. One hook covers
// every device of the class, like the Release hook. It is in with depth24=0
// too, because the near plane needs the depth of the Z buffer.
void HookCreateDepth(IDirect3DDevice9* device) {
  if (g_hooked_create_depth != nullptr) return;
  void* create = (*reinterpret_cast<void***>(device))[29];
  if (MH_CreateHook(create, reinterpret_cast<void*>(&h_CreateDepth),
                    reinterpret_cast<void**>(&o_CreateDepth)) == MH_OK &&
      MH_EnableHook(create) == MH_OK) {
    g_hooked_create_depth = create;
  } else {
    ModLog("depth24      : CreateDepthStencilSurface could not be hooked. The "
           "depth of the Z buffer is not known, and the near plane uses the "
           "16 bit value");
  }
}

// The game's callback for one Z buffer format. The game keeps the formats with
// no stencil. This moves the 24 bit one (D24X8) to the front of its table.
HRESULT WINAPI h_ZFormatCallback(unsigned char* format, void* context) {
  volatile int* count = reinterpret_cast<volatile int*>(kZFormatCount);
  const int before = *count;
  const HRESULT hr = o_ZFormatCallback(format, context);
  const int after = *count;
  if (after == before + 1 && before > 0 && before < 4) {
    unsigned char* table = reinterpret_cast<unsigned char*>(kZFormatTable);
    unsigned char* entry = table + before * kZFormatBytes;
    const DWORD flags = *reinterpret_cast<DWORD*>(entry + kFormatFlags);
    const DWORD bits = *reinterpret_cast<DWORD*>(entry + kFormatBits);
    const DWORD mask = *reinterpret_cast<DWORD*>(entry + kFormatMask);
    if (flags == kZBufferOnly && bits == 32 && mask == 0x00FFFFFF) {
      unsigned char saved[kZFormatBytes];
      memcpy(saved, entry, kZFormatBytes);
      memmove(table + kZFormatBytes, table, before * kZFormatBytes);
      memcpy(table, saved, kZFormatBytes);
      if (!g_said_moved) {
        g_said_moved = true;
        ModLog("depth24      : the game lists D24X8 as entry %d of its Z "
               "formats. It now tries D24X8 first", before);
      }
    }
  }
  return hr;
}

void TakeDevice(IDirect3DDevice9* device,
                const D3DPRESENT_PARAMETERS* present) {
  if (device == nullptr) return;
  if (present != nullptr) {
    g_full_screen = !present->Windowed;
    ModLog("gpu bridge   : the backend made a device, %ux%u %s",
           present->BackBufferWidth, present->BackBufferHeight,
           present->Windowed ? "in a window"
                             : "full screen. Start the game from the "
                               "launcher, which sets a window.");
  }
  // The newest device is the one the backend draws with. If the Release hook
  // did not see the old device go, the old device can already be freed, so
  // the bridge makes no call on it.
  if (g_device != nullptr) {
    FrameSendJoin();
    ForgetDevice();
  }
  g_device = device;
  g_device->AddRef();
  HookDeviceRelease(device);
  HookCreateDepth(device);
  D3DDEVICE_CREATION_PARAMETERS made = {};
  g_two_threads = SUCCEEDED(g_device->GetCreationParameters(&made)) &&
                  (made.BehaviorFlags & D3DCREATE_MULTITHREADED) != 0;
  ModLog("gpu bridge   : the device %s calls from two threads (behavior "
         "flags 0x%lX)", g_two_threads ? "permits" : "does not permit",
         made.BehaviorFlags);
  if (SUCCEEDED(g_device->QueryInterface(__uuidof(IDirect3DDevice9Ex),
                                         reinterpret_cast<void**>(&g_device_ex)))) {
    ModLog("gpu bridge   : captured a Direct3D 9Ex device");
  } else {
    g_device_ex = nullptr;
    ModLog("gpu bridge   : the backend made a plain Direct3D 9 device, which "
           "cannot share a texture. Set D3d9to9Ex=1 in dxwrapper.ini.");
  }
}

// With frame_send=2 a second thread makes calls on the device, so the device
// must permit calls from two threads.
DWORD TwoThreadFlags(DWORD flags) {
  if (!g_two_threads_wanted || (flags & D3DCREATE_MULTITHREADED) != 0) {
    return flags;
  }
  ModLog("gpu bridge   : the backend asked for a device for one thread. The "
         "mod asks for a device that permits calls from two threads "
         "(frame_send)");
  return flags | D3DCREATE_MULTITHREADED;
}

HRESULT STDMETHODCALLTYPE h_CreateDevice(IDirect3D9* self, UINT adapter,
                                         D3DDEVTYPE type, HWND focus,
                                         DWORD flags,
                                         D3DPRESENT_PARAMETERS* present,
                                         IDirect3DDevice9** out) {
  const HRESULT hr =
      o_CreateDevice(self, adapter, type, focus, TwoThreadFlags(flags),
                     present, out);
  if (SUCCEEDED(hr) && out != nullptr) TakeDevice(*out, present);
  return hr;
}

HRESULT STDMETHODCALLTYPE h_CreateDeviceEx(IDirect3D9Ex* self, UINT adapter,
                                           D3DDEVTYPE type, HWND focus,
                                           DWORD flags,
                                           D3DPRESENT_PARAMETERS* present,
                                           D3DDISPLAYMODEEX* mode,
                                           IDirect3DDevice9Ex** out) {
  const HRESULT hr =
      o_CreateDeviceEx(self, adapter, type, focus, TwoThreadFlags(flags),
                       present, mode, out);
  if (SUCCEEDED(hr) && out != nullptr) TakeDevice(*out, present);
  return hr;
}

// Hooks CreateDevice and CreateDeviceEx on the interface the backend just
// made. The hooks go on the resolved function addresses, not on the object,
// so they survive whatever wrapping happens later.
void HookDeviceCreation(IDirect3D9* d3d, bool is_ex) {
  if (d3d == nullptr || g_device_hooked) return;
  void** vtable = *reinterpret_cast<void***>(d3d);
  if (IsBadReadPtr(vtable, sizeof(void*) * 21)) return;

  // IDirect3D9 slot 16 is CreateDevice. IDirect3D9Ex adds CreateDeviceEx at
  // slot 20.
  bool any = false;
  void* create = vtable[16];
  if (MH_CreateHook(create, reinterpret_cast<void*>(&h_CreateDevice),
                    reinterpret_cast<void**>(&o_CreateDevice)) == MH_OK &&
      MH_EnableHook(create) == MH_OK) {
    any = true;
  }
  if (is_ex) {
    void* create_ex = vtable[20];
    if (MH_CreateHook(create_ex, reinterpret_cast<void*>(&h_CreateDeviceEx),
                      reinterpret_cast<void**>(&o_CreateDeviceEx)) == MH_OK &&
        MH_EnableHook(create_ex) == MH_OK) {
      any = true;
    }
  }
  g_device_hooked = any;
  ModLog("gpu bridge   : device creation %s", any ? "watched" : "NOT watched");
}

HRESULT WINAPI h_Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
  const HRESULT hr = o_Direct3DCreate9Ex(sdk, out);
  if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
    HookDeviceCreation(*out, true);
  }
  return hr;
}

IDirect3D9* WINAPI h_Direct3DCreate9(UINT sdk) {
  IDirect3D9* d3d = o_Direct3DCreate9(sdk);
  if (d3d != nullptr) HookDeviceCreation(d3d, false);
  return d3d;
}

template <typename T>
void ReleaseOne(T*& object) {
  if (object != nullptr) {
    object->Release();
    object = nullptr;
  }
}

void ReleasePanelShared() {
  ReleaseOne(g_panel_opened);
  ReleaseOne(g_panel_shared_surface);
  ReleaseOne(g_panel_shared);
}

void ReleaseShared() {
  ReleasePanelShared();
  for (int eye = 0; eye < kEyes; ++eye) {
    if (g_opened[eye] != nullptr) {
      g_opened[eye]->Release();
      g_opened[eye] = nullptr;
    }
    if (g_shared_surface[eye] != nullptr) {
      g_shared_surface[eye]->Release();
      g_shared_surface[eye] = nullptr;
    }
    if (g_shared[eye] != nullptr) {
      g_shared[eye]->Release();
      g_shared[eye] = nullptr;
    }
  }
}

}  // namespace

void GpuBridgeSetEnabled(bool on) { g_enabled = on; }

// depth24=1. Called early, before the backend makes its device.
void Depth24SetWanted(bool on) { g_depth24 = on; }

// The depth of the game's Z buffer in bits: 16, 24 or 32. 0 until the backend
// makes the first one, or when the hook on CreateDepthStencilSurface is not
// in (gpu_bridge=0).
int GpuBridgeDepthBits() { return g_game_depth_bits; }

// Puts the hook on the game's Z format callback. Needs MinHook started and the
// target checked. Call it before the game opens Direct3D.
void Depth24Install() {
  if (g_depth24_hooked) return;
  if (!g_depth24) {
    ModLog("depth24      : OFF. The game keeps its 16 bit Z buffer");
    return;
  }
  void* target = reinterpret_cast<void*>(kZFormatCallback);
  if (MH_CreateHook(target, reinterpret_cast<void*>(&h_ZFormatCallback),
                    reinterpret_cast<void**>(&o_ZFormatCallback)) == MH_OK &&
      MH_EnableHook(target) == MH_OK) {
    g_depth24_hooked = true;
    ModLog("depth24      : ON. The game asks for a 24 bit Z buffer (D24X8). "
           "If the adapter cannot make it, the mod makes a 16 bit one");
  } else {
    ModLog("depth24      : the game's Z format callback could not be hooked, "
           "the Z buffer stays 16 bit");
  }
}

// Watches for the backend's Direct3D 9 device. Must run before the backend
// creates one, so the caller does this as early as it can.
void GpuBridgeInstall() {
  if (!g_enabled || g_installed) return;

  // Load Direct3D 9 ourselves so the entry points exist to hook. The backend
  // will get the same module.
  HMODULE d3d9 = LoadLibraryA("d3d9.dll");
  if (d3d9 == nullptr) {
    ModLog("gpu bridge   : d3d9.dll is not available, staying on the CPU path");
    return;
  }

  void* create_ex =
      reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9Ex"));
  void* create = reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9"));

  int hooked = 0;
  if (create_ex != nullptr &&
      MH_CreateHook(create_ex, reinterpret_cast<void*>(&h_Direct3DCreate9Ex),
                    reinterpret_cast<void**>(&o_Direct3DCreate9Ex)) == MH_OK &&
      MH_EnableHook(create_ex) == MH_OK) {
    ++hooked;
  }
  if (create != nullptr &&
      MH_CreateHook(create, reinterpret_cast<void*>(&h_Direct3DCreate9),
                    reinterpret_cast<void**>(&o_Direct3DCreate9)) == MH_OK &&
      MH_EnableHook(create) == MH_OK) {
    ++hooked;
  }

  g_installed = hooked > 0;
  ModLog("gpu bridge   : %d of 2 Direct3D 9 entry points hooked", hooked);
}

// True once a 9Ex device has been captured, so the shared path is possible.
bool GpuBridgeHasDevice() { return g_device_ex != nullptr; }

// True when the newest device of the backend is a full screen one.
bool GpuBridgeFullScreen() { return g_full_screen; }

bool GpuBridgeReady() { return g_ready; }

// THE WAYS OF THE SHARE. The Direct3D 9 of Windows makes a shared texture,
// and Direct3D 11 opens it (kShareFrom9). DXVK cannot give such a texture to
// the Direct3D 11 of Windows: the game stops in the open. So with DXVK
// (d3d9_backend.cpp), and when the first way fails, Direct3D 11 makes the
// texture and Direct3D 9 opens it: with a handle of the kernel
// (kShareFrom11), and else with an NT handle (kShareFrom11Nt). The first way
// that works stays for the session.
enum ShareWay { kShareNone = 0, kShareFrom9, kShareFrom11, kShareFrom11Nt };
int g_share_way = kShareNone;
bool g_said_share_way = false;

const char* ShareWayName(int way) {
  return way == kShareFrom9    ? "Direct3D 9 makes each shared texture"
         : way == kShareFrom11 ? "Direct3D 11 makes each shared texture, and "
                                 "Direct3D 9 opens it with a handle of the "
                                 "kernel"
                               : "Direct3D 11 makes each shared texture, and "
                                 "Direct3D 9 opens it with an NT handle";
}

uint32_t g_share_seen = 0;  // the color that the last check read

// True when a color that Direct3D 9 copies into the texture comes out of
// the texture of Direct3D 11. A backend can give D3D_OK for a share that it
// could not make, so the mod checks each way once. The color goes in as the
// eye pictures do, with StretchRect from a target that has it: DXVK keeps a
// ColorFill until it uses the texture itself, so Direct3D 11 would read the
// texture with no color.
bool ShareCarries(ID3D11Device* d11, IDirect3DTexture9* texture9,
                  ID3D11Texture2D* texture11) {
  IDirect3DSurface9* surface = nullptr;
  if (FAILED(texture9->GetSurfaceLevel(0, &surface))) return false;
  IDirect3DSurface9* source = nullptr;
  if (FAILED(g_device_ex->CreateRenderTarget(16, 16, D3DFMT_A8R8G8B8,
                                             D3DMULTISAMPLE_NONE, 0, FALSE,
                                             &source, nullptr))) {
    surface->Release();
    return false;
  }
  const D3DCOLOR mark = D3DCOLOR_ARGB(0xFF, 0x12, 0x9A, 0x56);
  HRESULT hr = g_device_ex->ColorFill(source, nullptr, mark);
  const RECT corner = {0, 0, 16, 16};
  if (SUCCEEDED(hr)) {
    hr = g_device_ex->StretchRect(source, nullptr, surface, &corner,
                                  D3DTEXF_NONE);
  }
  source->Release();
  surface->Release();
  if (FAILED(hr)) return false;
  // In a walk the fill can wait in the queue of the render thread. The query
  // comes after it only when the queue is empty.
  RenderThreadSync();
  IDirect3DQuery9* query = nullptr;
  if (SUCCEEDED(g_device_ex->CreateQuery(D3DQUERYTYPE_EVENT, &query))) {
    query->Issue(D3DISSUE_END);
    const DWORD start = GetTickCount();
    while (query->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE &&
           GetTickCount() - start < 500) {
      Sleep(0);
    }
    query->Release();
  }
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = 1;
  desc.Height = 1;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D* staging = nullptr;
  if (FAILED(d11->CreateTexture2D(&desc, nullptr, &staging))) return false;
  ID3D11DeviceContext* context = nullptr;
  d11->GetImmediateContext(&context);
  bool carries = false;
  uint32_t seen = 0;
  // The other device can see the color a moment later: 20 reads, 5 ms apart.
  for (int i = 0; i < 20 && context != nullptr && !carries; ++i) {
    if (i > 0) Sleep(5);
    const D3D11_BOX box = {0, 0, 0, 1, 1, 1};
    context->CopySubresourceRegion(staging, 0, 0, 0, 0, texture11, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
      seen = *static_cast<const uint32_t*>(mapped.pData);
      carries = seen == mark;
      context->Unmap(staging, 0);
    }
  }
  if (context != nullptr) context->Release();
  staging->Release();
  g_share_seen = seen;
  return carries;
}

// One shared texture of width by height in one way. False with nothing kept
// when the way does not work.
bool ShareOneWay(ID3D11Device* d11, int way, UINT width, UINT height,
                 IDirect3DTexture9** texture9, ID3D11Texture2D** texture11,
                 HRESULT* result) {
  *texture9 = nullptr;
  *texture11 = nullptr;
  HANDLE shared = nullptr;
  HRESULT hr = E_FAIL;
  if (way == kShareFrom9) {
    hr = g_device_ex->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
                                    D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, texture9,
                                    &shared);
    if (SUCCEEDED(hr) && shared != nullptr) {
      hr = d11->OpenSharedResource(shared, __uuidof(ID3D11Texture2D),
                                   reinterpret_cast<void**>(texture11));
    } else if (SUCCEEDED(hr)) {
      hr = E_FAIL;
    }
  } else {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = way == kShareFrom11
                         ? D3D11_RESOURCE_MISC_SHARED
                         : D3D11_RESOURCE_MISC_SHARED |
                               D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    hr = d11->CreateTexture2D(&desc, nullptr, texture11);
    bool close = false;
    if (SUCCEEDED(hr) && way == kShareFrom11) {
      IDXGIResource* resource = nullptr;
      hr = (*texture11)->QueryInterface(__uuidof(IDXGIResource),
                                        reinterpret_cast<void**>(&resource));
      if (SUCCEEDED(hr)) {
        hr = resource->GetSharedHandle(&shared);
        resource->Release();
      }
    } else if (SUCCEEDED(hr)) {
      IDXGIResource1* resource = nullptr;
      hr = (*texture11)->QueryInterface(__uuidof(IDXGIResource1),
                                        reinterpret_cast<void**>(&resource));
      if (SUCCEEDED(hr)) {
        hr = resource->CreateSharedHandle(
            nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            nullptr, &shared);
        resource->Release();
        close = SUCCEEDED(hr);
      }
    }
    if (SUCCEEDED(hr) && shared != nullptr) {
      hr = g_device_ex->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
                                      D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                      texture9, &shared);
    } else if (SUCCEEDED(hr)) {
      hr = E_FAIL;
    }
    if (close) CloseHandle(shared);
  }
  *result = hr;
  if (SUCCEEDED(hr) && *texture9 != nullptr && *texture11 != nullptr) {
    return true;
  }
  if (*texture9 != nullptr) (*texture9)->Release();
  if (*texture11 != nullptr) (*texture11)->Release();
  *texture9 = nullptr;
  *texture11 = nullptr;
  return false;
}

// One shared texture: the way of the session, or the first way that works
// and carries a color (ShareCarries).
bool ShareOne(ID3D11Device* d11, UINT width, UINT height,
              IDirect3DTexture9** texture9, ID3D11Texture2D** texture11,
              HRESULT* result) {
  if (g_share_way != kShareNone) {
    return ShareOneWay(d11, g_share_way, width, height, texture9, texture11,
                       result);
  }
  for (int way = D3D9BackendDxvk() ? kShareFrom11 : kShareFrom9;
       way <= kShareFrom11Nt; ++way) {
    if (ShareOneWay(d11, way, width, height, texture9, texture11, result) &&
        !ShareCarries(d11, *texture9, *texture11)) {
      ModLog("gpu bridge   : the share in the way \"%s\" gave no error, but "
             "a color of Direct3D 9 did not come out in Direct3D 11 (it "
             "read %08X)",
             ShareWayName(way), g_share_seen);
      (*texture9)->Release();
      (*texture11)->Release();
      *texture9 = nullptr;
      *texture11 = nullptr;
      *result = E_FAIL;
      continue;
    }
    if (*texture9 != nullptr) {
      g_share_way = way;
      if (!g_said_share_way) {
        g_said_share_way = true;
        ModLog("gpu bridge   : %s", ShareWayName(way));
      }
      return true;
    }
    ModLog("gpu bridge   : the share in the way \"%s\" failed (0x%08lX)",
           ShareWayName(way), *result);
  }
  return false;
}

// Builds the shared pair. d3d11_device is the device the OpenXR session was
// made with, and format is its swapchain format. Returns false, with a
// reason in the log, if the GPU path cannot be used.
bool GpuBridgeStart(void* d3d11_device, uint32_t width, uint32_t height,
                    uint32_t swapchain_format) {
  if (!g_enabled || g_ready) return g_ready;
  if (g_device_ex == nullptr || d3d11_device == nullptr) return false;

  // CopyResource only copies between formats in the same typeless family. A
  // Direct3D 9 A8R8G8B8 texture opens as B8G8R8A8_UNORM, which is in the same
  // family as B8G8R8A8_UNORM_SRGB but not as either R8G8B8A8 format.
  const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(swapchain_format);
  if (format != DXGI_FORMAT_B8G8R8A8_UNORM &&
      format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
      format != DXGI_FORMAT_B8G8R8A8_TYPELESS) {
    ModLog("gpu bridge   : the swapchain format %u is not BGRA, so a direct "
           "copy is not allowed. Staying on the CPU path.", swapchain_format);
    return false;
  }

  ID3D11Device* d11 = static_cast<ID3D11Device*>(d3d11_device);
  g_width = width;
  g_height = height;
  g_start_d3d11 = d3d11_device;
  g_start_format = swapchain_format;
  g_resume_wanted = false;

  for (int eye = 0; eye < kEyes; ++eye) {
    HRESULT hr = E_FAIL;
    if (!ShareOne(d11, width, height, &g_shared[eye], &g_opened[eye], &hr)) {
      ModLog("gpu bridge   : the shared texture of eye %d failed (0x%08lX)",
             eye, hr);
      ReleaseShared();
      return false;
    }
    hr = g_shared[eye]->GetSurfaceLevel(0, &g_shared_surface[eye]);
    if (FAILED(hr)) {
      ModLog("gpu bridge   : GetSurfaceLevel failed for eye %d (0x%08lX)", eye,
             hr);
      ReleaseShared();
      return false;
    }
  }

  g_flush_issued = false;
  if (FAILED(g_device_ex->CreateQuery(D3DQUERYTYPE_EVENT, &g_flush_query))) {
    ModLog("gpu bridge   : the device offers no event query, so the two "
           "devices cannot be kept in step. Staying on the CPU path.");
    ReleaseShared();
    return false;
  }

  // The interface panel goes the same way, through a third shared texture.
  // A failure here keeps the interface in the eyes and the eyes on the GPU.
  if (g_panel_wanted && g_panel_share) {
    HRESULT hr = E_FAIL;
    if (ShareOne(d11, width, height, &g_panel_shared, &g_panel_opened, &hr)) {
      hr = g_panel_shared->GetSurfaceLevel(0, &g_panel_shared_surface);
    }
    if (FAILED(hr) || g_panel_opened == nullptr) {
      ModLog("ui panel     : the panel texture could not be shared (0x%08lX). "
             "The interface stays in the eye pictures.", hr);
      ReleasePanelShared();
    } else {
      ModLog("ui panel     : the panel texture is shared to Direct3D 11, "
             "%ux%u", width, height);
    }
  }

  QueryPerformanceFrequency(&g_frequency);
  g_ready = true;
  ModLog("gpu bridge   : ON. %ux%u shared both ways, no readback", width,
         height);
  return true;
}

// Copies the eye the game just finished into that eye's shared texture and
// waits for the GPU to catch up. Returns the Direct3D 11 texture to hand to
// the VR layer, or null if the caller should use the CPU path this frame.
// How many captures in a row may fail before the GPU path is abandoned.
// A single failure can be a transient device state. A run of them means the
// device the shared textures were made on no longer exists.
const int kFailuresBeforeGivingUp = 60;
int g_consecutive_failures = 0;

// Stops using the GPU path and frees what it held. The caller falls back to
// reading the frame back through the CPU, which is slower but always works.
void GiveUpOnSharing(const char* why) {
  // The second thread can read the textures that this frees.
  FrameSendJoin();
  g_ready = false;
  ReleaseShared();
  ModLog("gpu bridge   : OFF, %s. The frame goes back through a readback.",
         why);
}

// Starts the shared path again after the backend replaced the device it ran
// on. The caller runs this once a frame. It does nothing unless that
// happened. Until it succeeds, the frame goes through the readback.
void GpuBridgeResume() {
  if (!g_resume_wanted || g_device_ex == nullptr || g_start_d3d11 == nullptr) {
    return;
  }
  g_resume_wanted = false;
  g_consecutive_failures = 0;
  ModLog("gpu bridge   : starting again on the new device (%u lost so far)",
         g_devices_lost);
  GpuBridgeStart(g_start_d3d11, g_width, g_height, g_start_format);
}

namespace {

// Puts the copy of the picture that the game finished in the queue of the
// GPU: from the render target to the shared texture of one eye. It does not
// wait for the GPU. Returns the Direct3D 11 texture of the eye, or null when
// the caller must use the CPU path in this frame.
//
// source is the surface to copy, or null for the render target that is bound.
// rect is the part of it to copy (left, top, right, bottom), or null for all
// of it: one walk copies each eye from its half of the wide target.
void* CopyEye(int eye, void* source_surface = nullptr,
              const long rect[4] = nullptr) {
  DrawMergeFlush();
  if (!g_ready || eye < 0 || eye >= kEyes) return nullptr;

  // A device reset throws away everything in the default pool, which is both
  // shared textures. Using them afterwards is undefined, and the crash that
  // follows lands in the game rather than here. Asking first is cheap.
  const HRESULT state = g_device_ex->CheckDeviceState(nullptr);
  if (state != S_OK && state != S_PRESENT_OCCLUDED) {
    GiveUpOnSharing("the Direct3D 9 device changed state");
    return nullptr;
  }

  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);

  IDirect3DSurface9* source = static_cast<IDirect3DSurface9*>(source_surface);
  if (source != nullptr) {
    source->AddRef();
  } else if (FAILED(g_device->GetRenderTarget(0, &source)) ||
             source == nullptr) {
    ++g_failures;
    if (++g_consecutive_failures >= kFailuresBeforeGivingUp) {
      GiveUpOnSharing("the backend stopped handing over a render target");
    }
    return nullptr;
  }
  RECT part;
  if (rect != nullptr) {
    part.left = rect[0];
    part.top = rect[1];
    part.right = rect[2];
    part.bottom = rect[3];
  }
  const HRESULT hr = g_device->StretchRect(source,
                                           rect != nullptr ? &part : nullptr,
                                           g_shared_surface[eye], nullptr,
                                           D3DTEXF_NONE);
  source->Release();
  if (FAILED(hr)) {
    ++g_failures;
    if (++g_consecutive_failures >= kFailuresBeforeGivingUp) {
      GiveUpOnSharing("the copy into the shared texture kept failing");
    }
    return nullptr;
  }
  g_consecutive_failures = 0;
  // A moment of black after a snap turn or a jump, when the player asked for
  // one. See comfort_camera.cpp.
  if (ComfortBlinkActive()) {
    g_device->ColorFill(g_shared_surface[eye], nullptr,
                        D3DCOLOR_ARGB(255, 0, 0, 0));
  }
  QueryPerformanceCounter(&t1);
  g_blit_ticks += t1.QuadPart - t0.QuadPart;
  ++g_captures;
  return g_opened[eye];
}

// Waits on this thread for the GPU, before the other device reads a shared
// texture.
//
// Note what this really waits for. D3DQUERYTYPE_EVENT with D3DGETDATA_FLUSH
// drains the whole Direct3D 9 command buffer to idle, so it waits for
// everything the game queued before it, not for the copy alone. The
// number it produces is a GPU drain, not the price of the copy, and it
// moves when anything else on the GPU moves. It dropped from about 0.9 ms
// to about 0.34 ms when the copy to the monitor stopped, and nothing in
// this file changed.
//
// It cannot simply be removed. Microsoft's contract for a Direct3D 9Ex
// texture shared into Direct3D 11 requires the producer to synchronize. A
// keyed mutex would be the usual answer and is not available here, because
// Direct3D 9Ex cannot create or open a keyed mutex shared resource.
// The loop is bounded: if the query never completes, the frame goes out
// slightly torn rather than the game locking up.
void WaitForGpu() {
  if (g_flush_query == nullptr) return;
  LARGE_INTEGER t1, t2;
  QueryPerformanceCounter(&t1);
  g_flush_query->Issue(D3DISSUE_END);
  g_flush_issued = true;
  int spins = 0;
  while (g_flush_query->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
    if (++spins > 2000000) break;
  }
  if (spins > 0) ++g_flush_spins;
  QueryPerformanceCounter(&t2);
  g_flush_ticks += t2.QuadPart - t1.QuadPart;
}

// Takes the wait of the second thread in the frame before, for the report.
// Call it only when the second thread has no frame.
void TakeThreadWait() {
  if (!g_thread_waited) return;
  g_thread_waited = false;
  g_flush_ticks += g_thread_wait_ticks;
  if (g_thread_polls > 0) ++g_flush_spins;
  if (g_thread_gave_up && g_gave_up_lines < 5) {
    ++g_gave_up_lines;
    ModLog("gpu bridge   : the GPU did not finish a frame in 100 ms. The "
           "second thread sent the frame as it was");
  }
}

}  // namespace

// The copy of one eye picture, and then the wait for the GPU.
void* GpuBridgeCaptureEye(int eye) {
  // The second thread can read the shared textures of the frame before.
  FrameSendJoin();
  void* texture = CopyEye(eye);
  if (texture != nullptr) WaitForGpu();
  return texture;
}

// A FRAME WITH ONE WAIT FOR THE GPU (frame_send.cpp). GpuBridgeCopyEye puts
// the copy of an eye picture in the queue and does not wait. After the
// second eye pass one wait covers the two copies, and the copy of the
// interface panel: the work of one device is done in the order of its queue.
// Until that wait is over, the Direct3D 11 device must not read a texture
// that GpuBridgeCopyEye returned.
void* GpuBridgeCopyEye(int eye) {
  TakeThreadWait();
  return CopyEye(eye);
}

// ONE WALK (one_walk.cpp). The two copies of a frame of one walk, from the
// halves of the wide target. GpuBridgeCopyEyeFrom is the copy alone, as
// GpuBridgeCopyEye. GpuBridgeCaptureEyeFrom is the copy and, with flush, the
// wait for the GPU, as GpuBridgeCaptureEye: the first copy of a frame goes
// with no wait, the second with the wait, so the two copies cost one wait.
void* GpuBridgeCopyEyeFrom(int eye, void* source, const long rect[4]) {
  TakeThreadWait();
  return CopyEye(eye, source, rect);
}

void* GpuBridgeCaptureEyeFrom(int eye, void* source, const long rect[4],
                              bool flush) {
  FrameSendJoin();
  void* texture = CopyEye(eye, source, rect);
  if (texture != nullptr && flush) WaitForGpu();
  return texture;
}

// The one wait, on the frame thread.
void GpuBridgeWaitHere() { WaitForGpu(); }

// The one wait on a second thread has two parts. The frame thread puts the
// end of the frame in the queue and sends the queue to the GPU.
void GpuBridgeWaitBegin() {
  if (g_flush_query == nullptr) return;
  g_flush_number = RenderThreadQueueFlush(g_flush_query);
  if (g_flush_number != 0) return;
  g_flush_query->Issue(D3DISSUE_END);
  g_flush_issued = true;
  g_flush_query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
}

// Then the second thread waits for that end. The device takes a lock for
// each call, and the frame thread can draw the next frame at the same time.
// With thread_wait=0 this loop asks about 20,000 times a second and spins
// between the questions. With thread_wait=1 the thread sleeps with a timer of
// high resolution until 0.3 ms before the earliest end of the last 8 frames,
// asks each 50 microseconds for the next 0.5 ms, and then each 100
// microseconds with a sleep between. Each call has D3DGETDATA_FLUSH, as the
// wait on the frame thread has. The loop has a limit of 100 ms: the frame then
// goes out as it is.
void GpuBridgeWaitOnThread() {
  IDirect3DQuery9* query = g_flush_query;
  if (query == nullptr || g_frequency.QuadPart == 0) return;
  static bool s_added = false;
  if (!s_added) {
    s_added = true;
    ThreadWaitTune(GetCurrentThread(), L"bwvr frame_send thread");
    ThreadLoadAddThis("frame_send thread");
  }
  const LONGLONG pause = g_frequency.QuadPart / 20000;
  const LONGLONG limit = g_frequency.QuadPart / 10;
  const bool sleeps = ThreadWaitMode() != 0;
  LARGE_INTEGER start, now;
  QueryPerformanceCounter(&start);
  uint32_t polls = 0;
  bool gave_up = false;
  // Before the render thread issued the query, the query has the end of the
  // frame before.
  if (g_flush_number != 0 && !RenderThreadWaitFlushed(g_flush_number, 100)) {
    gave_up = true;
  }
  LARGE_INTEGER issued;
  QueryPerformanceCounter(&issued);
  LONGLONG slept = 0;
  if (sleeps && !gave_up) {
    LONGLONG earliest = 0;
    for (int i = 0; i < kRecentWaits; ++i) {
      if (g_recent_waits[i] <= 0) {
        earliest = 0;
        break;
      }
      if (earliest == 0 || g_recent_waits[i] < earliest) {
        earliest = g_recent_waits[i];
      }
    }
    const LONGLONG margin = g_frequency.QuadPart * 3 / 10000;  // 0.3 ms
    if (earliest > 2 * margin &&
        query->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
      ThreadWaitSleep(earliest - margin);
      QueryPerformanceCounter(&now);
      slept = now.QuadPart - issued.QuadPart;
    }
  }
  const LONGLONG spin_until = g_frequency.QuadPart / 2000;  // 0.5 ms
  LARGE_INTEGER polls_from;
  QueryPerformanceCounter(&polls_from);
  for (; !gave_up;) {
    QueryPerformanceCounter(&now);
    const LONGLONG waited = now.QuadPart - start.QuadPart;
    if (query->GetData(nullptr, 0, D3DGETDATA_FLUSH) != S_FALSE) break;
    ++polls;
    if (waited > limit) {
      gave_up = true;
      break;
    }
    if (sleeps && now.QuadPart - polls_from.QuadPart > spin_until &&
        ThreadWaitSleep(2 * pause)) {
      continue;
    }
    LARGE_INTEGER then;
    do {
      YieldProcessor();
      QueryPerformanceCounter(&then);
    } while (then.QuadPart - now.QuadPart < pause);
  }
  QueryPerformanceCounter(&now);
  if (!gave_up) {
    g_recent_waits[g_recent_at] = now.QuadPart - issued.QuadPart;
    g_recent_at = (g_recent_at + 1) % kRecentWaits;
  }
  g_thread_wait_ticks = static_cast<uint64_t>(now.QuadPart - start.QuadPart);
  g_thread_sleep_ticks += static_cast<uint64_t>(slept);
  g_thread_polls = polls;
  g_thread_gave_up = gave_up;
  g_thread_waited = true;
}

// True when a second thread can wait for the GPU on this device.
bool GpuBridgeTwoThreads() { return g_ready && g_two_threads; }

// Call it before the backend makes its device.
void GpuBridgeWantTwoThreads(bool on) { g_two_threads_wanted = on; }

// Frees the keeps of the eye pictures. See the note at g_keep.
void GpuBridgeKeepFree() {
  for (int eye = 0; eye < kEyes; ++eye) {
    if (g_keep[eye] != nullptr) g_keep[eye]->Release();
    g_keep[eye] = nullptr;
    g_keep_filled[eye] = false;
  }
  if (g_keep_target != nullptr) g_keep_target->Release();
  g_keep_target = nullptr;
}

// Copies the game's render target to the keep of one eye. Call it when the
// picture of that eye is finished. False when there is no keep: then the
// caller must not count on GpuBridgeKeepGive.
bool GpuBridgeKeepTake(int eye) {
  DrawMergeFlush();
  if (g_device == nullptr || g_keep_failed || eye < 0 || eye >= kEyes) {
    return false;
  }
  IDirect3DSurface9* target = nullptr;
  if (FAILED(g_device->GetRenderTarget(0, &target)) || target == nullptr) {
    return false;
  }
  D3DSURFACE_DESC desc;
  if (FAILED(target->GetDesc(&desc))) {
    target->Release();
    return false;
  }
  if (g_keep[0] == nullptr || desc.Width != g_keep_desc.Width ||
      desc.Height != g_keep_desc.Height || desc.Format != g_keep_desc.Format ||
      desc.MultiSampleType != g_keep_desc.MultiSampleType ||
      desc.MultiSampleQuality != g_keep_desc.MultiSampleQuality) {
    GpuBridgeKeepFree();
    for (int e = 0; e < kEyes; ++e) {
      if (FAILED(g_device->CreateRenderTarget(
              desc.Width, desc.Height, desc.Format, desc.MultiSampleType,
              desc.MultiSampleQuality, FALSE, &g_keep[e], nullptr)) ||
          g_keep[e] == nullptr) {
        ModLog("gpu bridge   : a keep for the picture of an eye could not be "
               "made (%ux%u, format %d, multisample type %d). Each eye pass "
               "draws over the last picture in the render target",
               desc.Width, desc.Height,
               static_cast<int>(desc.Format),
               static_cast<int>(desc.MultiSampleType));
        GpuBridgeKeepFree();
        g_keep_failed = true;
        target->Release();
        return false;
      }
    }
    g_keep_desc = desc;
    ModLog("gpu bridge   : made a keep for the picture of each eye, %ux%u, "
           "format %d, multisample type %d", desc.Width, desc.Height,
           static_cast<int>(desc.Format),
           static_cast<int>(desc.MultiSampleType));
  }
  const HRESULT hr =
      g_device->StretchRect(target, nullptr, g_keep[eye], nullptr, D3DTEXF_NONE);
  if (FAILED(hr)) {
    ModLog("gpu bridge   : the copy of the picture of an eye to its keep "
           "failed (0x%08lX). Each eye pass draws over the last picture in "
           "the render target", hr);
    GpuBridgeKeepFree();
    g_keep_failed = true;
    target->Release();
    return false;
  }
  g_keep_filled[eye] = true;
  if (g_keep_target != target) {
    if (g_keep_target != nullptr) g_keep_target->Release();
    g_keep_target = target;
  } else {
    target->Release();
  }
  ++g_keep_copies;
  return true;
}

// Copies the keep of one eye back to the render target that it came from.
// Call it before the eye pass draws. False when the keep holds no picture.
bool GpuBridgeKeepGive(int eye) {
  DrawMergeFlush();
  if (g_device == nullptr || g_keep_failed || eye < 0 || eye >= kEyes ||
      g_keep[eye] == nullptr || !g_keep_filled[eye] ||
      g_keep_target == nullptr) {
    return false;
  }
  const HRESULT hr = g_device->StretchRect(g_keep[eye], nullptr, g_keep_target,
                                           nullptr, D3DTEXF_NONE);
  if (FAILED(hr)) {
    ModLog("gpu bridge   : the copy of the keep of an eye to the render "
           "target failed (0x%08lX). Each eye pass draws over the last "
           "picture in the render target", hr);
    GpuBridgeKeepFree();
    g_keep_failed = true;
    return false;
  }
  ++g_keep_copies;
  return true;
}

// How many copies the keeps made, for the log.
uint64_t GpuBridgeKeepCopies() { return g_keep_copies; }

// True when the keep of one eye holds a picture.
bool GpuBridgeKeepHas(int eye) {
  return eye >= 0 && eye < kEyes && g_keep[eye] != nullptr &&
         g_keep_filled[eye] && g_keep_target != nullptr;
}

// The Direct3D 9 device of the backend, for one_walk.cpp.
void* GpuBridgeDeviceObject() { return g_device; }

// A depth surface that the mod makes for itself is not the game's Z buffer.
// While this is on, the depth24 rule and its count do not touch a create.
void GpuBridgeOwnDepth(bool on) { g_making_own_depth = on; }

void GpuBridgeReport(uint64_t frames) {
  if (!g_ready || g_captures == 0 || frames == 0 ||
      g_frequency.QuadPart == 0) {
    return;
  }
  const double to_ms = 1000.0 / static_cast<double>(g_frequency.QuadPart);
  const double per_frame = static_cast<double>(frames);
  ModLog("gpu bridge   : blit %.2f ms + GPU drain %.2f ms per frame (%.2f ms "
         "of it a sleep of the second thread), %llu captures, %llu waited, "
         "%llu failed",
         (static_cast<double>(g_blit_ticks) / per_frame) * to_ms,
         (static_cast<double>(g_flush_ticks) / per_frame) * to_ms,
         (static_cast<double>(g_thread_sleep_ticks) / per_frame) * to_ms,
         g_captures, g_flush_spins, g_failures);
  g_thread_sleep_ticks = 0;
  g_blit_ticks = 0;
  g_flush_ticks = 0;
  g_captures = 0;
  g_flush_spins = 0;
  g_failures = 0;
}

// ---------------------------------------------------------------------------
// The interface panel. See the note at g_panel_wanted, and docs/ui-layer.md.

namespace {

// A render target of the given size, with no multisampling.
bool MakeTarget(uint32_t width, uint32_t height, IDirect3DSurface9** out) {
  return SUCCEEDED(g_device->CreateRenderTarget(
             width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
             out, nullptr)) &&
         *out != nullptr;
}

// A depth surface of the given size. A format with a stencil comes first,
// in case a panel batch tests the stencil.
bool MakeDepth(uint32_t width, uint32_t height, IDirect3DSurface9** out,
               bool* stencil) {
  const D3DFORMAT formats[3] = {D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D16};
  bool made = false;
  g_making_own_depth = true;
  for (int i = 0; i < 3 && !made; ++i) {
    if (SUCCEEDED(g_device->CreateDepthStencilSurface(
            width, height, formats[i], D3DMULTISAMPLE_NONE, 0, FALSE, out,
            nullptr)) &&
        *out != nullptr) {
      *stencil = formats[i] == D3DFMT_D24S8;
      made = true;
    }
  }
  g_making_own_depth = false;
  return made;
}

void ReleasePanelTargets() {
  ReleaseOne(g_panel);
  ReleaseOne(g_panel_depth);
  ReleaseOne(g_spare);
  ReleaseOne(g_check);
  ReleaseOne(g_check_depth);
  ReleaseOne(g_readback);
  for (int i = 0; i < kHalfTargets; ++i) {
    ReleaseOne(g_half[i].target);
    ReleaseOne(g_half[i].read);
    g_half[i] = HalfTarget();
  }
  for (int i = 0; i < kAheadSlots; ++i) {
    ReleaseOne(g_ahead[i].target);
    ReleaseOne(g_ahead[i].read);
    ReleaseOne(g_ahead[i].query);
    g_ahead[i] = AheadSlot();
  }
  g_half_locked = nullptr;
  g_panel_width = 0;
  g_panel_height = 0;
}

// The panel pair, the size of the game's render target (rule 1 and 2 of
// docs/ui-layer.md, "Rules for the switch"). It is made at the first bind, and
// again when the render target changes size.
bool EnsurePanel(IDirect3DSurface9* game_target) {
  D3DSURFACE_DESC desc;
  // In one walk the bound target is two eyes wide. The panel has the size of
  // one eye, which is the size of the game's own target.
  game_target = static_cast<IDirect3DSurface9*>(OneWalkGameSurface(game_target));
  if (game_target == nullptr || FAILED(game_target->GetDesc(&desc))) {
    return false;
  }
  if (!g_panel_logged_target) {
    g_panel_logged_target = true;
    ModLog("ui panel     : the game's render target is %ux%u, format %d, "
           "multisample type %d, quality %lu", desc.Width, desc.Height,
           static_cast<int>(desc.Format), static_cast<int>(desc.MultiSampleType),
           desc.MultiSampleQuality);
  }
  if (g_panel != nullptr && desc.Width == g_panel_width &&
      desc.Height == g_panel_height) {
    return true;
  }
  ReleasePanelTargets();
  D3DCAPS9 caps;
  if (FAILED(g_device->GetDeviceCaps(&caps)) ||
      (caps.PrimitiveMiscCaps & D3DPMISCCAPS_SEPARATEALPHABLEND) == 0) {
    ModLog("ui panel     : the device has no separate alpha blend. The "
           "interface stays in the eye pictures.");
    g_panel_failed = true;
    return false;
  }
  if (!MakeTarget(desc.Width, desc.Height, &g_panel) ||
      !MakeDepth(desc.Width, desc.Height, &g_panel_depth, &g_panel_stencil)) {
    ModLog("ui panel     : the panel target could not be made. The interface "
           "stays in the eye pictures.");
    ReleasePanelTargets();
    g_panel_failed = true;
    return false;
  }
  g_panel_width = desc.Width;
  g_panel_height = desc.Height;
  ModLog("ui panel     : made the panel target, %ux%u, its depth %s a stencil",
         desc.Width, desc.Height, g_panel_stencil ? "with" : "without");
  return true;
}

bool SaveGameState() {
  g_saved_target = nullptr;
  g_saved_depth = nullptr;
  if (FAILED(g_device->GetRenderTarget(0, &g_saved_target)) ||
      g_saved_target == nullptr) {
    return false;
  }
  // A device with no depth surface is a valid state, and then this fails.
  if (FAILED(g_device->GetDepthStencilSurface(&g_saved_depth))) {
    g_saved_depth = nullptr;
  }
  g_device->GetViewport(&g_saved_viewport);
  g_device->GetScissorRect(&g_saved_scissor);
  for (int i = 0; i < 3; ++i) {
    g_device->GetRenderState(kAlphaStates[i], &g_saved_alpha[i]);
  }
  return true;
}

void RestoreGameState() {
  g_device->SetRenderTarget(0, g_saved_target);
  g_device->SetDepthStencilSurface(g_saved_depth);
  // SetRenderTarget sets the viewport and the scissor rect to the whole
  // target. Dd7to9 sets its viewport again at its next draw, but a path that
  // does not would read the wrong one.
  g_device->SetViewport(&g_saved_viewport);
  g_device->SetScissorRect(&g_saved_scissor);
  for (int i = 0; i < 3; ++i) {
    g_device->SetRenderState(kAlphaStates[i], g_saved_alpha[i]);
  }
  ReleaseOne(g_saved_target);
  ReleaseOne(g_saved_depth);
  g_bound = -1;
}

// The composite check, at the first panel bind of a check frame: two copies
// of the eye as it is before the first panel batch.
void PrepareCheck() {
  static bool said = false;
  bool stencil = false;
  if ((g_spare == nullptr && !MakeTarget(g_panel_width, g_panel_height,
                                         &g_spare)) ||
      (g_check == nullptr && !MakeTarget(g_panel_width, g_panel_height,
                                         &g_check)) ||
      (g_check_depth == nullptr &&
       !MakeDepth(g_panel_width, g_panel_height, &g_check_depth, &stencil))) {
    if (!said) ModLog("ui panel     : the check targets could not be made");
    said = true;
    return;
  }
  const HRESULT a = g_device->StretchRect(g_saved_target, nullptr, g_spare,
                                          nullptr, D3DTEXF_NONE);
  const HRESULT b = g_device->StretchRect(g_saved_target, nullptr, g_check,
                                          nullptr, D3DTEXF_NONE);
  g_check_ready = SUCCEEDED(a) && SUCCEEDED(b);
  if (!g_check_ready && !said) {
    ModLog("ui panel     : the copy of the eye for the check failed "
           "(0x%08lX, 0x%08lX)", a, b);
    said = true;
  }
}

bool SameSurface(IDirect3DSurface9* now, IDirect3DSurface9* want) {
  const bool same = now == want;
  if (now != nullptr) now->Release();
  return same;
}

}  // namespace

void GpuBridgePanelWant(bool target, bool share) {
  g_panel_wanted = target;
  g_panel_share = target && share;
}

// With backend=vulkan there is no Direct3D 9: the panel is the panel of the
// Vulkan renderer (VkScenePanelBind), and a session with Vulkan takes it.
bool GpuBridgePanelUsable() {
  if (VkSceneDirect()) return g_panel_wanted && VkScenePanelOn();
  return g_panel_wanted && !g_panel_failed && g_device != nullptr;
}

bool GpuBridgePanelShared() {
  if (VkSceneDirect()) return VkScenePanelOn();
  return g_ready && g_panel_opened != nullptr;
}

bool GpuBridgeIsPanel(const void* surface) {
  return surface != nullptr && surface == g_panel;
}

void GpuBridgePanelNewFrame(bool check) {
  if (g_vk_bound) {
    VkScenePanelBind(false);
    g_vk_bound = false;
  }
  if (g_bound >= 0 && g_device != nullptr) RestoreGameState();
  g_panel_cleared = false;
  g_check_cleared = false;
  g_check_wanted = check;
  g_check_ready = false;
}

// Binds the panel target (0) or the check target (1) for the next draw.
// False when the batch must draw into the eye as before.
bool GpuBridgePanelBind(int which) {
  DrawMergeFlush();
  if (VkSceneDirect()) {
    // The renderer clears the panel at its first draw in a walk, and draws
    // it with the blend of the panel. There is no check target.
    if (which != 0 || !GpuBridgePanelUsable()) return false;
    // A bind of render target 0 of the game can come between two batches,
    // so each batch binds the panel again.
    if (!g_vk_bound) ++g_panel_binds;
    VkScenePanelBind(true);
    g_vk_bound = true;
    return true;
  }
  if (!GpuBridgePanelUsable() || g_read_locked) return false;
  which = which == 1 ? 1 : 0;
  // The check target only follows a panel draw of the same batch.
  if (which == 1 && (g_bound < 0 || !g_check_ready)) return false;
  if (g_bound < 0) {
    if (!SaveGameState()) {
      ReleaseOne(g_saved_target);
      ++g_panel_bind_failures;
      return false;
    }
    if (!EnsurePanel(g_saved_target)) {
      ReleaseOne(g_saved_target);
      ReleaseOne(g_saved_depth);
      ++g_panel_bind_failures;
      return false;
    }
    if (g_check_wanted && !g_panel_cleared) PrepareCheck();
    ++g_panel_binds;
  }
  IDirect3DSurface9* target = which == 1 ? g_check : g_panel;
  IDirect3DSurface9* depth = which == 1 ? g_check_depth : g_panel_depth;

  if (g_bound == which) {
    // Rule 6: a Dd7to9 path may bind the game's target in the middle of a
    // span. Look before each draw, and bind again if it did.
    IDirect3DSurface9* now = nullptr;
    IDirect3DSurface9* now_depth = nullptr;
    g_device->GetRenderTarget(0, &now);
    g_device->GetDepthStencilSurface(&now_depth);
    const bool target_ok = SameSurface(now, target);
    const bool depth_ok = SameSurface(now_depth, depth);
    if (target_ok && depth_ok) return true;
    ++g_panel_rebinds;
  }
  g_device->SetRenderTarget(0, target);
  g_device->SetDepthStencilSurface(depth);
  for (int i = 0; i < 3; ++i) {
    g_device->SetRenderState(kAlphaStates[i],
                             which == 0 ? kPanelAlpha[i] : g_saved_alpha[i]);
  }
  // Rule 4: the first bind of a frame clears the panel to 0 in all four
  // channels and its depth to 1.0, the depth the screen quad of FinishFrame
  // leaves in the game's depth. The check target keeps its copy of the eye.
  if (which == 0 && !g_panel_cleared) {
    g_device->ColorFill(g_panel, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
    g_device->Clear(0, nullptr,
                    D3DCLEAR_ZBUFFER | (g_panel_stencil ? D3DCLEAR_STENCIL : 0),
                    0, 1.0f, 0);
    g_panel_cleared = true;
    ++g_panel_clears;
  } else if (which == 1 && !g_check_cleared) {
    g_device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
    g_check_cleared = true;
  }
  g_bound = which;
  return true;
}

// Puts the game's target, depth, viewport, scissor rect and alpha states
// back. Does nothing when the game's target is bound.
void GpuBridgePanelUnbind() {
  DrawMergeFlush();
  if (g_vk_bound) {
    VkScenePanelBind(false);
    g_vk_bound = false;
    return;
  }
  if (g_bound < 0) return;
  if (g_device == nullptr) {
    g_bound = -1;
    return;
  }
  RestoreGameState();
}

// The render states the last draw used. Dd7to9 sends its states at the draw,
// so after the draw the device holds the ones that batch drew with.
void GpuBridgePanelStates(UiPanelStates* out) {
  ZeroMemory(out, sizeof(*out));
  if (g_device == nullptr) return;
  DWORD v = 0;
  g_device->GetRenderState(D3DRS_ALPHABLENDENABLE, &v);
  out->blend_on = v;
  g_device->GetRenderState(D3DRS_SRCBLEND, &v);
  out->src = v;
  g_device->GetRenderState(D3DRS_DESTBLEND, &v);
  out->dest = v;
  g_device->GetRenderState(D3DRS_ZENABLE, &v);
  out->z_on = v;
  g_device->GetRenderState(D3DRS_ZWRITEENABLE, &v);
  out->z_write = v;
  g_device->GetRenderState(D3DRS_STENCILENABLE, &v);
  out->stencil_on = v;
  g_device->GetRenderState(D3DRS_ALPHATESTENABLE, &v);
  out->alpha_test = v;
}

// Copies the panel into its shared texture, with no flush. The flush of the
// eye 0 capture, which the caller runs next, drains this copy too. Returns
// the Direct3D 11 texture, or null when there is nothing to send.
void* GpuBridgeCaptureUi() {
  DrawMergeFlush();
  if (!g_ready || g_panel_opened == nullptr || g_panel == nullptr ||
      !g_panel_cleared || g_bound >= 0) {
    return nullptr;
  }
  if (g_panel_width != g_width || g_panel_height != g_height) {
    static bool said = false;
    if (!said) {
      ModLog("ui panel     : the panel is %ux%u and the headset texture "
             "%ux%u, so it is not sent", g_panel_width, g_panel_height,
             g_width, g_height);
    }
    said = true;
    return nullptr;
  }
  if (FAILED(g_device->StretchRect(g_panel, nullptr, g_panel_shared_surface,
                                   nullptr, D3DTEXF_NONE))) {
    return nullptr;
  }
  ++g_panel_copies;
  return g_panel_opened;
}

const void* GpuBridgePanelRead(int which, int* pitch, uint32_t* width,
                               uint32_t* height) {
  DrawMergeFlush();
  if (g_device == nullptr || g_read_locked || g_bound >= 0) return nullptr;
  IDirect3DSurface9* source = nullptr;
  if (which == 0) {
    source = g_panel_cleared ? g_panel : nullptr;
  } else {
    source = g_check_ready ? (which == 1 ? g_check : g_spare) : nullptr;
  }
  if (source == nullptr) return nullptr;
  if (g_readback == nullptr &&
      FAILED(g_device->CreateOffscreenPlainSurface(
          g_panel_width, g_panel_height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
          &g_readback, nullptr))) {
    g_readback = nullptr;
    return nullptr;
  }
  if (FAILED(g_device->GetRenderTargetData(source, g_readback))) return nullptr;
  D3DLOCKED_RECT locked;
  if (FAILED(g_readback->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
    return nullptr;
  }
  g_read_locked = true;
  *pitch = locked.Pitch;
  *width = g_panel_width;
  *height = g_panel_height;
  return locked.pBits;
}

void GpuBridgePanelReadDone() {
  if (g_read_locked && g_half_locked != nullptr) {
    g_half_locked->UnlockRect();
  } else if (g_read_locked && g_readback != nullptr) {
    g_readback->UnlockRect();
  }
  g_half_locked = nullptr;
  g_read_locked = false;
}

// THE SUBTITLE CARD. A part of the panel of this frame at half the size, as
// pixels for the CPU: BGRA, the color multiplied by the alpha. rect is x, y,
// width and height in panel pixels. The copy is one StretchRect with a linear
// filter (each pixel is the mean of four) into a small target, and one
// GetRenderTargetData. The target is as large as the part, on a grid of 128
// pixels, so a subtitle costs a copy of about 1 MB at a 2880 pixel render,
// and not of the whole panel. The pixels stay valid until
// GpuBridgePanelReadDone. Null when the panel did not draw in this frame, or
// when a step failed.
namespace {

// The rect of the panel for a half size copy: even numbers, inside the
// panel. out_w and out_h are the size of the copy, need_w and need_h the size
// of its target, on the grid of kHalfGrid pixels. False when the rect is
// empty.
bool HalfRect(const int rect[4], RECT* from, uint32_t* out_w, uint32_t* out_h,
              uint32_t* need_w, uint32_t* need_h) {
  int x0 = rect[0] & ~1, y0 = rect[1] & ~1;
  int x1 = (rect[0] + rect[2] + 1) & ~1, y1 = (rect[1] + rect[3] + 1) & ~1;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > static_cast<int>(g_panel_width & ~1u)) {
    x1 = static_cast<int>(g_panel_width & ~1u);
  }
  if (y1 > static_cast<int>(g_panel_height & ~1u)) {
    y1 = static_cast<int>(g_panel_height & ~1u);
  }
  if (x1 - x0 < 2 || y1 - y0 < 2) return false;
  from->left = x0;
  from->top = y0;
  from->right = x1;
  from->bottom = y1;
  *out_w = static_cast<uint32_t>(x1 - x0) / 2;
  *out_h = static_cast<uint32_t>(y1 - y0) / 2;
  *need_w = (*out_w + kHalfGrid - 1) / kHalfGrid * kHalfGrid;
  *need_h = (*out_h + kHalfGrid - 1) / kHalfGrid * kHalfGrid;
  if (*need_w > g_panel_width / 2) *need_w = g_panel_width / 2;
  if (*need_h > g_panel_height / 2) *need_h = g_panel_height / 2;
  return true;
}

// The slot with the newest pixels in system memory, or null.
AheadSlot* NewestRead() {
  AheadSlot* newest = nullptr;
  for (int i = 0; i < kAheadSlots; ++i) {
    if (g_ahead[i].state == kSlotRead &&
        (newest == nullptr || g_ahead[i].order > newest->order)) {
      newest = &g_ahead[i];
    }
  }
  return newest;
}

}  // namespace

const void* GpuBridgePanelReadHalf(const int rect[4], int* pitch,
                                   uint32_t* width, uint32_t* height) {
  DrawMergeFlush();
  if (g_device == nullptr || g_read_locked || g_bound >= 0 ||
      g_panel == nullptr || !g_panel_cleared) {
    return nullptr;
  }
  RECT from_rect;
  uint32_t out_w = 0, out_h = 0, need_w = 0, need_h = 0;
  if (!HalfRect(rect, &from_rect, &out_w, &out_h, &need_w, &need_h)) {
    return nullptr;
  }

  // The target for a part of this size.
  HalfTarget* half = nullptr;
  HalfTarget* oldest = &g_half[0];
  for (int i = 0; i < kHalfTargets; ++i) {
    if (g_half[i].target != nullptr && g_half[i].width == need_w &&
        g_half[i].height == need_h) {
      half = &g_half[i];
      break;
    }
    if (g_half[i].used < oldest->used) oldest = &g_half[i];
  }
  if (half == nullptr) {
    half = oldest;
    ReleaseOne(half->target);
    ReleaseOne(half->read);
    *half = HalfTarget();
    if (!MakeTarget(need_w, need_h, &half->target) ||
        FAILED(g_device->CreateOffscreenPlainSurface(
            need_w, need_h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &half->read,
            nullptr)) ||
        half->read == nullptr) {
      ReleaseOne(half->target);
      ReleaseOne(half->read);
      *half = HalfTarget();
      ++g_half_failures;
      return nullptr;
    }
    half->width = need_w;
    half->height = need_h;
    ++g_half_made;
    if (g_half_made <= 12) {
      ModLog("ui panel     : made a half size copy target for the subtitle "
             "card, %ux%u", need_w, need_h);
    }
  }
  half->used = ++g_half_clock;
  const RECT from = from_rect;
  const RECT to = {0, 0, static_cast<LONG>(out_w), static_cast<LONG>(out_h)};
  if (FAILED(g_device->StretchRect(g_panel, &from, half->target, &to,
                                   D3DTEXF_LINEAR)) ||
      FAILED(g_device->GetRenderTargetData(half->target, half->read))) {
    ++g_half_failures;
    return nullptr;
  }
  D3DLOCKED_RECT locked;
  if (FAILED(half->read->LockRect(&locked, &to, D3DLOCK_READONLY))) {
    ++g_half_failures;
    return nullptr;
  }
  g_read_locked = true;
  g_half_locked = half->read;
  ++g_half_reads;
  g_half_bytes += static_cast<uint64_t>(need_w) * need_h * 4;
  *pitch = locked.Pitch;
  *width = out_w;
  *height = out_h;
  return locked.pBits;
}

// THE READ AHEAD, step 1, after the eye passes: the half size copy of rect
// into a slot, and an event query after it. Nothing waits. The slot is never
// the one with the newest pixels in system memory, because the card can
// still need them.
bool GpuBridgePanelQueueHalf(const int rect[4], uint64_t frame) {
  DrawMergeFlush();
  if (g_device == nullptr || g_read_locked || g_bound >= 0 ||
      g_panel == nullptr || !g_panel_cleared) {
    return false;
  }
  RECT from;
  uint32_t out_w = 0, out_h = 0, need_w = 0, need_h = 0;
  if (!HalfRect(rect, &from, &out_w, &out_h, &need_w, &need_h)) return false;
  const AheadSlot* keep = NewestRead();
  AheadSlot* slot = nullptr;
  for (int i = 0; i < kAheadSlots; ++i) {
    AheadSlot* s = &g_ahead[i];
    if (s == keep) continue;
    if (s->state == kSlotFree) {
      slot = s;
      break;
    }
    if (slot == nullptr || s->order < slot->order) slot = s;
  }
  if (slot == nullptr) return false;
  if (slot->state == kSlotQueued) ++g_ahead_dropped;
  slot->state = kSlotFree;
  if (slot->target == nullptr || slot->width != need_w ||
      slot->height != need_h) {
    ReleaseOne(slot->target);
    ReleaseOne(slot->read);
    if (!MakeTarget(need_w, need_h, &slot->target) ||
        FAILED(g_device->CreateOffscreenPlainSurface(
            need_w, need_h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &slot->read,
            nullptr)) ||
        slot->read == nullptr) {
      ReleaseOne(slot->target);
      ReleaseOne(slot->read);
      slot->width = slot->height = 0;
      ++g_ahead_failures;
      return false;
    }
    slot->width = need_w;
    slot->height = need_h;
    ++g_half_made;
    if (g_half_made <= 12) {
      ModLog("ui panel     : made a half size copy target for the read ahead "
             "of the subtitle card, %ux%u", need_w, need_h);
    }
  }
  if (slot->query == nullptr &&
      (FAILED(g_device->CreateQuery(D3DQUERYTYPE_EVENT, &slot->query)) ||
       slot->query == nullptr)) {
    slot->query = nullptr;
    ++g_ahead_failures;
    return false;
  }
  const RECT to = {0, 0, static_cast<LONG>(out_w), static_cast<LONG>(out_h)};
  if (FAILED(g_device->StretchRect(g_panel, &from, slot->target, &to,
                                   D3DTEXF_LINEAR)) ||
      FAILED(slot->query->Issue(D3DISSUE_END))) {
    ++g_ahead_failures;
    return false;
  }
  slot->out_w = out_w;
  slot->out_h = out_h;
  slot->rect[0] = static_cast<int>(from.left);
  slot->rect[1] = static_cast<int>(from.top);
  slot->rect[2] = static_cast<int>(from.right - from.left);
  slot->rect[3] = static_cast<int>(from.bottom - from.top);
  slot->frame = frame;
  slot->order = ++g_ahead_clock;
  slot->state = kSlotQueued;
  ++g_ahead_queued;
  return true;
}

// THE READ AHEAD, step 2, at the start of a frame, before the eye passes: the
// read of the newest slot whose copy is done. GetData asks once and does not
// wait. D3DGETDATA_FLUSH only sends the calls in the queue to the GPU, so a
// query that no other flush sent does not stay open. The read is made only
// when the query of the last frame end is done too: then the GPU has no work
// left before it. Older slots are free again.
void GpuBridgePanelFetchHalf() {
  if (g_device == nullptr || g_read_locked || g_bound >= 0) return;
  AheadSlot* done = nullptr;
  bool waiting = false;
  for (int i = 0; i < kAheadSlots; ++i) {
    AheadSlot* s = &g_ahead[i];
    if (s->state != kSlotQueued || s->query == nullptr) continue;
    if (s->query->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_OK) {
      if (done == nullptr || s->order > done->order) done = s;
    } else {
      waiting = true;
    }
  }
  if (done == nullptr) {
    if (waiting) ++g_ahead_waits;
    return;
  }
  if (g_flush_query != nullptr && g_flush_issued &&
      g_flush_query->GetData(nullptr, 0, D3DGETDATA_FLUSH) != S_OK) {
    ++g_ahead_waits;
    return;
  }
  if (FAILED(g_device->GetRenderTargetData(done->target, done->read))) {
    done->state = kSlotFree;
    ++g_ahead_failures;
    return;
  }
  done->state = kSlotRead;
  ++g_ahead_reads;
  for (int i = 0; i < kAheadSlots; ++i) {
    AheadSlot* s = &g_ahead[i];
    if (s == done || s->state == kSlotFree || s->order > done->order) {
      continue;
    }
    if (s->state == kSlotQueued) ++g_ahead_dropped;
    s->state = kSlotFree;
  }
}

// THE READ AHEAD, step 3, after the eye passes: the newest pixels in system
// memory, of a copy from the frame `since` or later. rect gets the rect of
// the panel that they hold. The pixels stay valid until
// GpuBridgePanelReadDone. Null when there are none yet.
const void* GpuBridgePanelReadyHalf(uint64_t since, int rect[4], int* pitch,
                                    uint32_t* width, uint32_t* height) {
  if (g_device == nullptr || g_read_locked) return nullptr;
  AheadSlot* slot = NewestRead();
  if (slot == nullptr || slot->frame < since) return nullptr;
  const RECT area = {0, 0, static_cast<LONG>(slot->out_w),
                     static_cast<LONG>(slot->out_h)};
  D3DLOCKED_RECT locked;
  if (FAILED(slot->read->LockRect(&locked, &area, D3DLOCK_READONLY))) {
    ++g_ahead_failures;
    return nullptr;
  }
  g_read_locked = true;
  g_half_locked = slot->read;
  for (int i = 0; i < 4; ++i) rect[i] = slot->rect[i];
  *pitch = locked.Pitch;
  *width = slot->out_w;
  *height = slot->out_h;
  return locked.pBits;
}

void GpuBridgePanelReport() {
  if (!g_panel_wanted) return;
  if (g_panel_binds == 0 && g_panel_bind_failures == 0) return;
  ModLog("ui panel     : %llu binds, %llu times the target was found changed, "
         "%llu clears, %llu copies to the headset, %llu binds failed",
         g_panel_binds, g_panel_rebinds, g_panel_clears, g_panel_copies,
         g_panel_bind_failures);
  if (g_half_reads + g_half_failures > 0) {
    ModLog("ui panel     : %llu half size copies for the subtitle card, %.0f "
           "KiB each, through %llu targets made so far. %llu failed",
           g_half_reads,
           g_half_reads ? static_cast<double>(g_half_bytes) / 1024.0 /
                              static_cast<double>(g_half_reads)
                        : 0.0,
           g_half_made, g_half_failures);
    g_half_reads = g_half_bytes = g_half_failures = 0;
  }
  if (g_ahead_queued + g_ahead_failures > 0) {
    ModLog("ui panel     : the read ahead of the subtitle card: %llu copies in "
           "the queue, %llu reads, %llu frame starts with a copy not done "
           "yet, %llu copies never read, %llu failed",
           g_ahead_queued, g_ahead_reads, g_ahead_waits, g_ahead_dropped,
           g_ahead_failures);
    g_ahead_queued = g_ahead_reads = g_ahead_waits = 0;
    g_ahead_dropped = g_ahead_failures = 0;
  }
  g_panel_binds = 0;
  g_panel_rebinds = 0;
  g_panel_clears = 0;
  g_panel_copies = 0;
  g_panel_bind_failures = 0;
}

// ---------------------------------------------------------------------------
// The self test
//
// The real path needs a headset, because the Direct3D 11 device it copies
// into belongs to the OpenXR session. That makes the most important piece of
// this mod the hardest piece to check.
//
// So the self test builds its own Direct3D 11 device, runs the same capture,
// and copies the result back to the CPU once, only so it can be written to a
// file. If that file shows the game, then the whole chain works: the backend
// render target, the shared texture, the handle, the open on the other
// device, and the copy. The readback here exists to prove the path, not to
// be part of it.

ID3D11Device* g_test_device = nullptr;
ID3D11DeviceContext* g_test_context = nullptr;
ID3D11Texture2D* g_test_staging = nullptr;

bool GpuBridgeSelfTestStart(uint32_t width, uint32_t height) {
  if (!g_enabled || g_ready) return g_ready;
  if (g_device_ex == nullptr) return false;

  // Made once. A later call, after a failed start, uses the same device.
  if (g_test_device == nullptr) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
        sizeof(levels) / sizeof(levels[0]), D3D11_SDK_VERSION, &g_test_device,
        nullptr, &g_test_context);
    if (FAILED(hr)) {
      ModLog("gpu bridge   : the self test could not make a Direct3D 11 "
             "device (0x%08lX)", hr);
      return false;
    }
  }

  if (!GpuBridgeStart(g_test_device, width, height,
                      static_cast<uint32_t>(DXGI_FORMAT_B8G8R8A8_UNORM))) {
    return false;
  }
  if (g_test_staging != nullptr) return true;

  D3D11_TEXTURE2D_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(g_test_device->CreateTexture2D(&desc, nullptr, &g_test_staging))) {
    ModLog("gpu bridge   : the self test could not make a staging texture");
    return false;
  }
  ModLog("gpu bridge   : self test ready");
  return true;
}

// Captures one eye through the shared texture and brings it back so it can be
// written to a file. Returns null if any step failed.
const void* GpuBridgeSelfTestReadFrom(int eye, void* source,
                                      const long rect[4], int* pitch) {
  DrawMergeFlush();
  if (g_test_staging == nullptr) return nullptr;
  void* texture = GpuBridgeCaptureEyeFrom(eye, source, rect, true);
  if (texture == nullptr) return nullptr;

  g_test_context->CopyResource(g_test_staging,
                               static_cast<ID3D11Texture2D*>(texture));
  D3D11_MAPPED_SUBRESOURCE mapped;
  if (FAILED(g_test_context->Map(g_test_staging, 0, D3D11_MAP_READ, 0,
                                 &mapped))) {
    return nullptr;
  }
  *pitch = static_cast<int>(mapped.RowPitch);
  return mapped.pData;
}

const void* GpuBridgeSelfTestRead(int eye, int* pitch) {
  return GpuBridgeSelfTestReadFrom(eye, nullptr, nullptr, pitch);
}

void GpuBridgeSelfTestDone() {
  if (g_test_staging != nullptr) g_test_context->Unmap(g_test_staging, 0);
}

void GpuBridgeShutdown() {
  FrameSendJoin();
  if (g_test_staging != nullptr) {
    g_test_staging->Release();
    g_test_staging = nullptr;
  }
  if (g_test_context != nullptr) {
    g_test_context->Release();
    g_test_context = nullptr;
  }
  if (g_test_device != nullptr) {
    g_test_device->Release();
    g_test_device = nullptr;
  }
  g_ready = false;
  if (g_flush_query != nullptr) {
    g_flush_query->Release();
    g_flush_query = nullptr;
  }
  g_flush_issued = false;
  if (g_device != nullptr) {
    GpuBridgePanelReadDone();
    GpuBridgePanelUnbind();
    ReleasePanelTargets();
  }
  ReleaseShared();
  // Cleared before the release, so the Release hook does not report the
  // bridge's own release as the backend destroying the device.
  IDirect3DDevice9Ex* device_ex = g_device_ex;
  IDirect3DDevice9* device = g_device;
  g_device_ex = nullptr;
  g_device = nullptr;
  if (device_ex != nullptr) device_ex->Release();
  if (device != nullptr) device->Release();
}
