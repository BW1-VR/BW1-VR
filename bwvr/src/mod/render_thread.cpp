// THE RENDER THREAD (render_thread, docs/backend.md, "The render thread").
//
// WHY. Over the whole island a scene walk is about 10 ms of the frame
// thread, and about 6 ms of it are Direct3D 9, the driver and the kernel
// (backend.md, "Where the time of the scene walk goes"). A second thread can
// do those calls while the frame thread walks the scene.
//
// HOW. This file has the hooks of the Direct3D 9 device of the backend. In a
// scene walk on the frame thread (RenderThreadWalk), the hook of a call that
// sets a state, binds a texture or a target, clears or draws does not give
// the call to Direct3D 9. It writes a record of it to a queue, with a copy of
// each array that the call points to, and returns D3D_OK. The render thread
// reads the queue in order and makes the calls. A resource in a record has a
// reference until the render thread made the call.
//
// THE STOPS. Each other call of the device, a lock of a resource of
// Direct3D 9, the issue of a query, a call of a DirectDraw surface that reads
// or writes pixels (RenderThreadSync from draw_merge.cpp and the other surface
// hooks), and the end of the walk wait until the render thread has made each
// call before it. So each call has the state and the place in the order that
// it has with no render thread. The device has the flag for two threads.
//
// THE READS. One walk reads the format, the viewport, the scissor rect and a
// render state at each draw, and the interface panel reads the target. A
// read would be a stop. So the hooks keep these values as the calls of the
// walk set them, and a read gives the kept value. A value that is not kept
// yet is read after a stop and kept.
//
// ONE WALK. One walk changes seven calls of the device (one_walk.cpp). Their
// hooks are here, and they give each call to one_walk.cpp, which gives its
// calls to the RenderThreadOut functions. So one walk runs on the frame
// thread, before the record.
//
//   render_thread=0           each call goes to Direct3D 9 as it comes
//   render_thread=1           the calls of a scene walk go to the render thread
//   render_thread=2           the calls of a scene walk are kept, and go to
//                             Direct3D 9 on the frame thread at each stop (a
//                             test of the record and of the stops)
//   render_thread_lock_free=1  the type of a texture comes with no lock of
//                             the device (see GETTYPE). 0: from Direct3D 9
//   render_thread_test_frame=N  frames N and N+1 with the mode of the ini,
//                             N+2 and N+3 with 0, each with a picture of each
//                             eye

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "one_walk_math.h"
#include "render_thread.h"
#include "render_thread_d3d9.h"
#include "render_thread_ring.h"

extern void ModLog(const char* format, ...);
// gpu_bridge.cpp
void* GpuBridgeDeviceObject();
// draw_merge.cpp: true when the hooks of the surface calls are in.
bool DrawMergeSurfaceHooked();
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);

namespace {

// ---------------------------------------------------------------------------
// The slots of IDirect3DDevice9 and IDirect3DDevice9Ex.

enum Slot {
  kQueryInterface = 0,
  kAddRef = 1,
  kRelease = 2,
  kCreateDepthStencilSurface = 29,
  kSetRenderTarget = 37,
  kGetRenderTarget = 38,
  kSetDepthStencilSurface = 39,
  kGetDepthStencilSurface = 40,
  kBeginScene = 41,
  kEndScene = 42,
  kClear = 43,
  kSetTransform = 44,
  kSetViewport = 47,
  kGetViewport = 48,
  kSetMaterial = 49,
  kSetLight = 51,
  kLightEnable = 53,
  kSetClipPlane = 55,
  kSetRenderState = 57,
  kGetRenderState = 58,
  kGetTexture = 64,
  kSetTexture = 65,
  kGetTextureStageState = 66,
  kSetTextureStageState = 67,
  kGetSamplerState = 68,
  kSetSamplerState = 69,
  kSetScissorRect = 75,
  kGetScissorRect = 76,
  kDrawPrimitive = 81,
  kDrawIndexedPrimitive = 82,
  kDrawPrimitiveUP = 83,
  kDrawIndexedPrimitiveUP = 84,
  kSetVertexDeclaration = 87,
  kSetFVF = 89,
  kGetFVF = 90,
  kSetVertexShader = 92,
  kSetVertexShaderConstantF = 94,
  kSetStreamSource = 100,
  kSetIndices = 104,
  kSetPixelShader = 107,
  kSetPixelShaderConstantF = 109,
  kDeviceSlots = 119,
  kDeviceSlotsEx = 134,
};

// The resources: their slot numbers, and the numbers of their stops in the
// report, after the slots of the device.
const int kSurfaceLockRect = 13;
const int kSurfaceUnlockRect = 14;
const int kSurfaceGetDC = 15;
const int kTextureLockRect = 19;
const int kTextureUnlockRect = 20;
const int kBufferLock = 11;
const int kQueryIssue = 6;
const int kStopLockRect = 140;
const int kStopUnlockRect = 141;
const int kStopGetDC = 142;
const int kStopBufferLock = 143;
const int kStopQueryIssue = 144;
const int kStopSurface = 145;  // a surface call of DirectDraw (RenderThreadSync)
const int kStops = 150;
const int kMaxThunks = 160;

// ---------------------------------------------------------------------------
// The records.

enum Op : uint16_t {
  kOpWrap,
  kOpSetRenderTarget,
  kOpSetDepthStencil,
  kOpBeginScene,
  kOpEndScene,
  kOpClear,
  kOpSetTransform,
  kOpSetViewport,
  kOpSetMaterial,
  kOpSetLight,
  kOpLightEnable,
  kOpSetClipPlane,
  kOpSetRenderState,
  kOpSetTexture,
  kOpSetTextureStageState,
  kOpSetSamplerState,
  kOpSetScissorRect,
  kOpDrawPrimitive,
  kOpDrawIndexedPrimitive,
  kOpDrawPrimitiveUP,
  kOpDrawIndexedPrimitiveUP,
  kOpSetVertexDeclaration,
  kOpSetFVF,
  kOpSetVertexShader,
  kOpSetVertexShaderConstantF,
  kOpSetStreamSource,
  kOpSetIndices,
  kOpSetPixelShader,
  kOpSetPixelShaderConstantF,
  kOpIssueQuery,
  kOps
};

struct Header {
  uint16_t op;
  uint16_t unused;
  uint32_t bytes;  // of the whole record
};

// The arguments of each record. An array follows them in the record.
struct ArgTarget { DWORD index; IDirect3DSurface9* surface; };
struct ArgSurface { IDirect3DSurface9* surface; };
struct ArgClear { DWORD count; DWORD flags; D3DCOLOR color; float z; DWORD stencil; };
struct ArgTransform { D3DTRANSFORMSTATETYPE state; D3DMATRIX matrix; };
struct ArgLight { DWORD index; D3DLIGHT9 light; };
struct ArgLightEnable { DWORD index; BOOL on; };
struct ArgClipPlane { DWORD index; float plane[4]; };
struct ArgState { DWORD a; DWORD b; DWORD c; };
struct ArgTexture { DWORD stage; IDirect3DBaseTexture9* texture; };
struct ArgDraw { D3DPRIMITIVETYPE type; INT base; UINT a; UINT b; UINT c; UINT d; };
struct ArgDrawUP { D3DPRIMITIVETYPE type; UINT count; UINT stride; UINT vertex_bytes; };
struct ArgDrawIndexedUP {
  D3DPRIMITIVETYPE type;
  UINT min_index;
  UINT vertex_count;
  UINT primitive_count;
  D3DFORMAT index_format;
  UINT stride;
  UINT vertex_bytes;
  UINT index_bytes;
};
struct ArgObject { IUnknown* object; };
struct ArgConstants { UINT start; UINT count; };
struct ArgStream { UINT stream; IDirect3DVertexBuffer9* buffer; UINT offset; UINT stride; };

// ---------------------------------------------------------------------------
// The originals of the hooks.

typedef HRESULT(STDMETHODCALLTYPE* SetRenderTargetFn)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
typedef HRESULT(STDMETHODCALLTYPE* GetRenderTargetFn)(IDirect3DDevice9*, DWORD, IDirect3DSurface9**);
typedef HRESULT(STDMETHODCALLTYPE* SetSurfaceFn)(IDirect3DDevice9*, IDirect3DSurface9*);
typedef HRESULT(STDMETHODCALLTYPE* GetSurfaceFn)(IDirect3DDevice9*, IDirect3DSurface9**);
typedef HRESULT(STDMETHODCALLTYPE* PlainFn)(IDirect3DDevice9*);
typedef HRESULT(STDMETHODCALLTYPE* ClearFn)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* SetTransformFn)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);
typedef HRESULT(STDMETHODCALLTYPE* SetViewportFn)(IDirect3DDevice9*, const D3DVIEWPORT9*);
typedef HRESULT(STDMETHODCALLTYPE* GetViewportFn)(IDirect3DDevice9*, D3DVIEWPORT9*);
typedef HRESULT(STDMETHODCALLTYPE* SetMaterialFn)(IDirect3DDevice9*, const D3DMATERIAL9*);
typedef HRESULT(STDMETHODCALLTYPE* SetLightFn)(IDirect3DDevice9*, DWORD, const D3DLIGHT9*);
typedef HRESULT(STDMETHODCALLTYPE* LightEnableFn)(IDirect3DDevice9*, DWORD, BOOL);
typedef HRESULT(STDMETHODCALLTYPE* SetClipPlaneFn)(IDirect3DDevice9*, DWORD, const float*);
typedef HRESULT(STDMETHODCALLTYPE* SetRenderStateFn)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* GetRenderStateFn)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD*);
typedef HRESULT(STDMETHODCALLTYPE* SetTextureFn)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
typedef HRESULT(STDMETHODCALLTYPE* GetTextureFn)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9**);
typedef HRESULT(STDMETHODCALLTYPE* SetStageStateFn)(IDirect3DDevice9*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* GetStageStateFn)(IDirect3DDevice9*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD*);
typedef HRESULT(STDMETHODCALLTYPE* SetSamplerStateFn)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* GetSamplerStateFn)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD*);
typedef HRESULT(STDMETHODCALLTYPE* SetScissorFn)(IDirect3DDevice9*, const RECT*);
typedef HRESULT(STDMETHODCALLTYPE* GetScissorFn)(IDirect3DDevice9*, RECT*);
typedef HRESULT(STDMETHODCALLTYPE* DrawPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawPrimitiveUPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveUPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* SetDeclarationFn)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
typedef HRESULT(STDMETHODCALLTYPE* SetFVFFn)(IDirect3DDevice9*, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* GetFVFFn)(IDirect3DDevice9*, DWORD*);
typedef HRESULT(STDMETHODCALLTYPE* SetVertexShaderFn)(IDirect3DDevice9*, IDirect3DVertexShader9*);
typedef HRESULT(STDMETHODCALLTYPE* SetConstantsFn)(IDirect3DDevice9*, UINT, const float*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* SetStreamSourceFn)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* SetIndicesFn)(IDirect3DDevice9*, IDirect3DIndexBuffer9*);
typedef HRESULT(STDMETHODCALLTYPE* SetPixelShaderFn)(IDirect3DDevice9*, IDirect3DPixelShader9*);

SetRenderTargetFn o_SetRenderTarget = nullptr;
GetRenderTargetFn o_GetRenderTarget = nullptr;
SetSurfaceFn o_SetDepthStencil = nullptr;
GetSurfaceFn o_GetDepthStencil = nullptr;
PlainFn o_BeginScene = nullptr;
PlainFn o_EndScene = nullptr;
ClearFn o_Clear = nullptr;
SetTransformFn o_SetTransform = nullptr;
SetViewportFn o_SetViewport = nullptr;
GetViewportFn o_GetViewport = nullptr;
SetMaterialFn o_SetMaterial = nullptr;
SetLightFn o_SetLight = nullptr;
LightEnableFn o_LightEnable = nullptr;
SetClipPlaneFn o_SetClipPlane = nullptr;
SetRenderStateFn o_SetRenderState = nullptr;
GetRenderStateFn o_GetRenderState = nullptr;
SetTextureFn o_SetTexture = nullptr;
GetTextureFn o_GetTexture = nullptr;
SetStageStateFn o_SetTextureStageState = nullptr;
GetStageStateFn o_GetTextureStageState = nullptr;
SetSamplerStateFn o_SetSamplerState = nullptr;
GetSamplerStateFn o_GetSamplerState = nullptr;
SetScissorFn o_SetScissorRect = nullptr;
GetScissorFn o_GetScissorRect = nullptr;
DrawPrimitiveFn o_DrawPrimitive = nullptr;
DrawIndexedPrimitiveFn o_DrawIndexedPrimitive = nullptr;
DrawPrimitiveUPFn o_DrawPrimitiveUP = nullptr;
DrawIndexedPrimitiveUPFn o_DrawIndexedPrimitiveUP = nullptr;
SetDeclarationFn o_SetVertexDeclaration = nullptr;
SetFVFFn o_SetFVF = nullptr;
GetFVFFn o_GetFVF = nullptr;
SetVertexShaderFn o_SetVertexShader = nullptr;
SetConstantsFn o_SetVertexShaderConstantF = nullptr;
SetStreamSourceFn o_SetStreamSource = nullptr;
SetIndicesFn o_SetIndices = nullptr;
SetPixelShaderFn o_SetPixelShader = nullptr;
SetConstantsFn o_SetPixelShaderConstantF = nullptr;

// ---------------------------------------------------------------------------
// The state.

int g_ini_mode = 0;          // render_thread
int g_test_frame = 0;        // render_thread_test_frame
bool g_lock_free = true;     // render_thread_lock_free
bool g_filter = true;        // render_thread_filter
// The calls of a walk that set the value that the device has already, by
// kind. With render_thread_filter=1 they do not go to Direct3D 9.
uint64_t g_same[kOps] = {0};
int g_wanted = 0;            // the mode for the next walk
int g_mode = 0;              // the mode of this walk
bool g_seven = false;        // the hooks of one walk are in
bool g_all = false;          // each hook is in
bool g_all_failed = false;
bool g_said_no_surface = false;
IDirect3DDevice9* g_device = nullptr;
volatile bool g_recording = false;  // in a walk with mode 1 or 2
DWORD g_frame_thread = 0;
DWORD g_worker_id = 0;
HANDLE g_worker = nullptr;
HANDLE g_work_event = nullptr;  // the writer wakes the render thread
HANDLE g_idle_event = nullptr;  // the render thread read the whole queue
volatile LONG g_worker_waits = 0;  // the render thread sleeps on g_work_event
// The render thread reads the queue only in mode 1. In mode 2 the frame
// thread makes the calls, and a second reader would make each call twice.
volatile LONG g_worker_on = 0;
// The frame thread sleeps on g_idle_event in a stop.
volatile LONG g_drain_waits = 0;

// The queue. The two counts are on cache lines of their own: the frame
// thread writes one and the render thread the other, and a shared line
// would go between the two cores at each record.
const uint32_t kQueueBytes = 64u << 20;
uint8_t* g_queue = nullptr;
__declspec(align(64)) volatile LONG g_write = 0;  // bytes written (mod 2^32)
__declspec(align(64)) volatile LONG g_read = 0;   // bytes read
__declspec(align(64)) volatile LONG g_line_end = 0;

// The values that the walk set (THE READS). The textures are raw pointers:
// the device or a record of the queue holds a reference to each of them.
const int kKeptStages = 16;
const int kKeptStageStates = 33;   // D3DTSS_CONSTANT is 32
const int kKeptSamplerStates = 14; // D3DSAMP_DMAPOFFSET is 13
const int kKeptTransforms = 14;

// The place of a matrix in Kept::transform, or -1 for a matrix that is not
// kept: view 0, projection 1, the texture matrices 2 to 9, the world
// matrices 0 to 3 at 10 to 13.
int TransformSlot(D3DTRANSFORMSTATETYPE state) {
  const int s = static_cast<int>(state);
  if (s == D3DTS_VIEW) return 0;
  if (s == D3DTS_PROJECTION) return 1;
  if (s >= D3DTS_TEXTURE0 && s <= D3DTS_TEXTURE7) return 2 + s - D3DTS_TEXTURE0;
  if (s >= 256 && s < 260) return 10 + s - 256;  // D3DTS_WORLDMATRIX(0..3)
  return -1;
}

struct Kept {
  bool fvf_ok;
  DWORD fvf;
  bool viewport_ok;
  D3DVIEWPORT9 viewport;
  bool scissor_ok;
  RECT scissor;
  bool target_ok[4];
  IDirect3DSurface9* target[4];
  bool depth_ok;
  IDirect3DSurface9* depth;
  uint8_t state_ok[256];
  DWORD state[256];
  uint8_t texture_ok[kKeptStages];
  IDirect3DBaseTexture9* texture[kKeptStages];
  uint8_t stage_ok[kKeptStages][kKeptStageStates];
  DWORD stage[kKeptStages][kKeptStageStates];
  uint8_t sampler_ok[kKeptStages][kKeptSamplerStates];
  DWORD sampler[kKeptStages][kKeptSamplerStates];
  // The matrices that the walk set: view, projection, the 8 texture
  // matrices and the first 4 world matrices (TransformSlot).
  uint8_t transform_ok[kKeptTransforms];
  D3DMATRIX transform[kKeptTransforms];
};
Kept g_kept;
// The viewport of one draw for both eyes (THE VIEWPORT OF BOTH EYES): the
// device has a viewport over both eyes, and g_vp_logical is the viewport of
// the game, which GetViewport gives.
bool g_vp_moved = false;
D3DVIEWPORT9 g_vp_logical = {};

// The thunks of the stops.
uint8_t* g_thunks = nullptr;
void* g_thunk_original[kMaxThunks] = {nullptr};

// For the report.
uint64_t g_walks = 0;
uint64_t g_records[kOps] = {0};
uint64_t g_bytes = 0;
uint64_t g_stops[kStops] = {0};
uint64_t g_stop_ticks = 0;
uint64_t g_end_ticks = 0;
uint64_t g_full_waits = 0;
uint64_t g_too_large = 0;
volatile LONG g_failed_calls = 0;
volatile LONG64 g_busy_ticks = 0;
LARGE_INTEGER g_frequency = {};

uint64_t Ticks() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return static_cast<uint64_t>(t.QuadPart);
}

double Ms(uint64_t ticks) {
  return g_frequency.QuadPart == 0
             ? 0.0
             : 1000.0 * static_cast<double>(ticks) /
                   static_cast<double>(g_frequency.QuadPart);
}

// A read of a count of the other thread. An aligned read of 32 bits is one
// read on x86, and a volatile read of MSVC has acquire order (/volatile:ms),
// so the bytes of a record are there before its count. A locked read would
// take the cache line from the other core at each call.
uint32_t LoadIndex(volatile LONG* index) {
  return static_cast<uint32_t>(*index);
}

// A write of a count: a volatile write of MSVC has release order, so the
// bytes of a record are there before the other thread sees the count.
void StoreIndex(volatile LONG* index, uint32_t value) {
  *index = static_cast<LONG>(value);
}

void ResetKept() {
  g_kept.fvf_ok = false;
  g_kept.viewport_ok = false;
  g_kept.scissor_ok = false;
  memset(g_kept.target_ok, 0, sizeof(g_kept.target_ok));
  g_kept.depth_ok = false;
  memset(g_kept.state_ok, 0, sizeof(g_kept.state_ok));
  memset(g_kept.texture_ok, 0, sizeof(g_kept.texture_ok));
  memset(g_kept.stage_ok, 0, sizeof(g_kept.stage_ok));
  memset(g_kept.sampler_ok, 0, sizeof(g_kept.sampler_ok));
  memset(g_kept.transform_ok, 0, sizeof(g_kept.transform_ok));
}

// At the start of a walk the queue is empty, so a read of Direct3D 9 gives
// the value that the walk starts with. Each value that the hooks keep is read
// here one time, so a read in the walk needs no stop.
void ReadAll(IDirect3DDevice9* d) {
  ResetKept();
  g_vp_moved = false;
  DWORD v = 0;
  if (SUCCEEDED(o_GetFVF(d, &v))) {
    g_kept.fvf = v;
    g_kept.fvf_ok = true;
  }
  if (SUCCEEDED(o_GetViewport(d, &g_kept.viewport))) g_kept.viewport_ok = true;
  if (SUCCEEDED(o_GetScissorRect(d, &g_kept.scissor))) g_kept.scissor_ok = true;
  IDirect3DSurface9* surface = nullptr;
  for (DWORD i = 0; i < 4; ++i) {
    surface = nullptr;
    g_kept.target_ok[i] = true;
    g_kept.target[i] = nullptr;
    if (SUCCEEDED(o_GetRenderTarget(d, i, &surface)) && surface != nullptr) {
      g_kept.target[i] = surface;
      surface->Release();
    }
  }
  surface = nullptr;
  g_kept.depth_ok = true;
  g_kept.depth = nullptr;
  if (SUCCEEDED(o_GetDepthStencil(d, &surface)) && surface != nullptr) {
    g_kept.depth = surface;
    surface->Release();
  }
  // The render states of Direct3D 9 are 7 to 209.
  for (DWORD s = 7; s < 210; ++s) {
    if (SUCCEEDED(o_GetRenderState(d, static_cast<D3DRENDERSTATETYPE>(s), &v))) {
      g_kept.state[s] = v;
      g_kept.state_ok[s] = 1;
    }
  }
  for (int t = 0; t < 8; ++t) {
    IDirect3DBaseTexture9* texture = nullptr;
    if (SUCCEEDED(o_GetTexture(d, t, &texture))) {
      g_kept.texture[t] = texture;
      g_kept.texture_ok[t] = 1;
      if (texture != nullptr) texture->Release();
    }
    for (int s = 1; s < kKeptStageStates; ++s) {
      if (SUCCEEDED(o_GetTextureStageState(
              d, t, static_cast<D3DTEXTURESTAGESTATETYPE>(s), &v))) {
        g_kept.stage[t][s] = v;
        g_kept.stage_ok[t][s] = 1;
      }
    }
    for (int s = 1; s < kKeptSamplerStates; ++s) {
      if (SUCCEEDED(o_GetSamplerState(d, t, static_cast<D3DSAMPLERSTATETYPE>(s),
                                      &v))) {
        g_kept.sampler[t][s] = v;
        g_kept.sampler_ok[t][s] = 1;
      }
    }
  }
}

// True when this call is a call of the walk that the queue takes.
bool Taking(IDirect3DDevice9* self) {
  return g_recording && self == g_device &&
         GetCurrentThreadId() == g_frame_thread;
}

bool OnWorker() { return g_worker_id != 0 && GetCurrentThreadId() == g_worker_id; }

// ---------------------------------------------------------------------------
// The calls of a record.

void Drop(IUnknown* object) {
  if (object != nullptr) object->Release();
}

// Makes the call of one record. Gives the bytes of the record.
// ---------------------------------------------------------------------------
// THE BUFFERS (render_thread_buffers=1). Each draw of a walk is a
// DrawIndexedPrimitiveUP. Direct3D 9 copies the vertices and the indices of
// each one to a buffer of its own, with a lock for each draw. The render
// thread can see the records that wait in the queue. So it copies the
// vertices and the 16-bit indices of all of them to a vertex buffer and an
// index buffer of its own, with one lock of each buffer, and draws each one
// with DrawIndexedPrimitive from there. The place of a vertex block is a
// multiple of its stride, so BaseVertexIndex finds it.
//
// After a DrawIndexedPrimitiveUP the device has no vertex buffer on stream 0
// and no index buffer. The render thread puts that state back before the
// queue is empty, and before a record that sets a stream or the indices or
// draws from a buffer. A Reset of the device needs each buffer of the
// default pool to be released, so a Reset releases them first.

const UINT kVertexBufferBytes = 16u << 20;
const UINT kIndexBufferBytes = 4u << 20;
const int kMaxPrepared = 8192;

struct Prepared {
  INT base_vertex;
  UINT start_index;
};
bool g_buffers = true;  // render_thread_buffers
IDirect3DVertexBuffer9* g_vb = nullptr;
IDirect3DIndexBuffer9* g_ib = nullptr;
IDirect3DDevice9* g_buffers_device = nullptr;
bool g_buffers_failed = false;
UINT g_vb_at = 0;  // the first free byte
UINT g_ib_at = 0;
bool g_bound = false;      // our buffers are on stream 0 and the indices
UINT g_bound_stride = 0;
Prepared g_prepared[kMaxPrepared];
// For the report, written by the render thread.
volatile LONG g_buffer_draws = 0;
volatile LONG g_up_draws = 0;
volatile LONG g_buffer_locks = 0;

void ReleaseBuffers() {
  if (g_vb != nullptr) g_vb->Release();
  if (g_ib != nullptr) g_ib->Release();
  g_vb = nullptr;
  g_ib = nullptr;
  g_buffers_device = nullptr;
  g_bound = false;
  g_vb_at = g_ib_at = 0;
}

bool MakeBuffers(IDirect3DDevice9* d) {
  if (g_buffers_device == d && g_vb != nullptr) return true;
  ReleaseBuffers();
  if (g_buffers_failed) return false;
  if (FAILED(d->CreateVertexBuffer(kVertexBufferBytes,
                                   D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                   D3DPOOL_DEFAULT, &g_vb, nullptr)) ||
      FAILED(d->CreateIndexBuffer(kIndexBufferBytes,
                                  D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                  D3DFMT_INDEX16, D3DPOOL_DEFAULT, &g_ib,
                                  nullptr))) {
    ReleaseBuffers();
    g_buffers_failed = true;
    ModLog("render thread: render_thread_buffers=1, but the buffers could not "
           "be made. Each draw goes to Direct3D 9 as it is");
    return false;
  }
  g_buffers_device = d;
  return true;
}

// The state that a DrawIndexedPrimitiveUP leaves: no buffer on stream 0, no
// indices.
void Unbind() {
  if (!g_bound) return;
  o_SetStreamSource(g_device, 0, nullptr, 0, 0);
  o_SetIndices(g_device, nullptr);
  g_bound = false;
}

// Copies the vertices and the indices of the draws in [read, write) to the
// buffers, and marks each of them in its header (unused is its place in
// g_prepared, plus 1). A draw that does not fit stays a
// DrawIndexedPrimitiveUP. The render thread owns these records.
void Prepare(uint32_t read, uint32_t write) {
  if (!g_buffers || g_mode != 1 || g_device == nullptr) return;
  if (!MakeBuffers(g_device)) return;
  // The bytes that the draws need, in the order of the queue.
  UINT vb_need = 0, ib_need = 0;
  int count = 0;
  for (uint32_t at = read; at != write && count < kMaxPrepared;) {
    Header* h = reinterpret_cast<Header*>(g_queue + (at & (kQueueBytes - 1)));
    if (h->op == kOpDrawIndexedPrimitiveUP) {
      const ArgDrawIndexedUP* a =
          reinterpret_cast<const ArgDrawIndexedUP*>(h + 1);
      if (a->index_format == D3DFMT_INDEX16 && a->stride != 0) {
        const UINT start = (vb_need + a->stride - 1) / a->stride * a->stride;
        vb_need = start + a->vertex_bytes;
        ib_need += (a->index_bytes + 3u) & ~3u;
        ++count;
      }
    }
    at += h->bytes;
  }
  // A lock for one or a few draws costs more than it saves.
  if (count < 8) return;
  if (vb_need > kVertexBufferBytes || ib_need > kIndexBufferBytes) return;
  // The copies go after the last ones, with no overwrite, or to the start of
  // the buffer with a discard. A change of the stride can add up to one
  // stride of room, so the room left must hold more than vb_need. The lock
  // is from the first free byte to the end of the buffer.
  DWORD vb_flags = D3DLOCK_NOOVERWRITE, ib_flags = D3DLOCK_NOOVERWRITE;
  UINT vb_base = g_vb_at;
  if (vb_base + vb_need + vb_need / 8u + 65536u > kVertexBufferBytes) {
    vb_base = 0;
    vb_flags = D3DLOCK_DISCARD;
  }
  UINT ib_base = (g_ib_at + 3u) & ~3u;
  if (ib_base + ib_need > kIndexBufferBytes) {
    ib_base = 0;
    ib_flags = D3DLOCK_DISCARD;
  }
  uint8_t* vb_data = nullptr;
  uint8_t* ib_data = nullptr;
  const UINT vb_lock = kVertexBufferBytes - vb_base;
  if (FAILED(g_vb->Lock(vb_base, vb_lock, reinterpret_cast<void**>(&vb_data),
                        vb_flags))) {
    return;
  }
  if (FAILED(g_ib->Lock(ib_base, ib_need, reinterpret_cast<void**>(&ib_data),
                        ib_flags))) {
    g_vb->Unlock();
    return;
  }
  InterlockedIncrement(&g_buffer_locks);
  UINT v = 0, i = 0;
  int n = 0;
  bool full = false;
  for (uint32_t at = read; at != write && n < count;) {
    Header* h = reinterpret_cast<Header*>(g_queue + (at & (kQueueBytes - 1)));
    if (h->op == kOpDrawIndexedPrimitiveUP) {
      const ArgDrawIndexedUP* a =
          reinterpret_cast<const ArgDrawIndexedUP*>(h + 1);
      if (a->index_format == D3DFMT_INDEX16 && a->stride != 0) {
        // The block starts at a multiple of the stride from the start of
        // the buffer.
        const UINT absolute = vb_base + v;
        const UINT start =
            (absolute + a->stride - 1) / a->stride * a->stride;
        if (start + a->vertex_bytes > vb_base + vb_lock) {
          full = true;
        }
        if (!full) {
          const uint8_t* vertices = reinterpret_cast<const uint8_t*>(a + 1);
          const uint8_t* indices = vertices + ((a->vertex_bytes + 7u) & ~7u);
          memcpy(vb_data + (start - vb_base), vertices, a->vertex_bytes);
          memcpy(ib_data + i, indices, a->index_bytes);
          g_prepared[n].base_vertex = static_cast<INT>(start / a->stride);
          g_prepared[n].start_index = (ib_base + i) / 2u;
          h->unused = static_cast<uint16_t>(n + 1);
          v = start + a->vertex_bytes - vb_base;
          i += (a->index_bytes + 3u) & ~3u;
        }
        ++n;
      }
    }
    at += h->bytes;
  }
  g_vb->Unlock();
  g_ib->Unlock();
  g_vb_at = vb_base + v;
  g_ib_at = ib_base + i;
}

// THE FLUSH (gpu_flush=N). The driver keeps the calls of a walk and gives
// them to the GPU late, mostly at the end of the walk, so the GPU starts the
// picture late and the frame waits for it after the walk. Each N draws the
// thread that makes the draws issues an event query and asks for its data
// with D3DGETDATA_FLUSH and no wait: the driver gives the GPU the calls so
// far, and the GPU draws while the walk goes on. With the render thread that
// is the render thread (Play), with no render thread the frame thread (the
// RenderThreadOut functions). One thread at a time makes the draws, so the
// count needs no lock.
int g_flush_every = 0;  // gpu_flush
int g_flush_ini = 0;
IDirect3DQuery9* g_flush_query = nullptr;
IDirect3DDevice9* g_flush_device = nullptr;
int g_since_flush = 0;
volatile LONG g_flushes = 0;

void CountDrawForFlush() {
  if (g_flush_every <= 0 || ++g_since_flush < g_flush_every) return;
  g_since_flush = 0;
  if (g_flush_device != g_device) {
    if (g_flush_query != nullptr) g_flush_query->Release();
    g_flush_query = nullptr;
    g_flush_device = g_device;
    if (FAILED(g_device->CreateQuery(D3DQUERYTYPE_EVENT, &g_flush_query))) {
      g_flush_query = nullptr;
    }
  }
  if (g_flush_query == nullptr) return;
  g_flush_query->Issue(D3DISSUE_END);
  g_flush_query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
  InterlockedIncrement(&g_flushes);
}

uint32_t Play(const uint8_t* at) {
  const Header* h = reinterpret_cast<const Header*>(at);
  const uint8_t* p = at + sizeof(Header);
  IDirect3DDevice9* d = g_device;
  HRESULT hr = S_OK;
  switch (h->op) {
    case kOpWrap:
      break;
    case kOpSetRenderTarget: {
      const ArgTarget* a = reinterpret_cast<const ArgTarget*>(p);
      hr = o_SetRenderTarget(d, a->index, a->surface);
      Drop(a->surface);
      break;
    }
    case kOpSetDepthStencil: {
      const ArgSurface* a = reinterpret_cast<const ArgSurface*>(p);
      hr = o_SetDepthStencil(d, a->surface);
      Drop(a->surface);
      break;
    }
    case kOpBeginScene:
      hr = o_BeginScene(d);
      break;
    case kOpEndScene:
      hr = o_EndScene(d);
      break;
    case kOpClear: {
      const ArgClear* a = reinterpret_cast<const ArgClear*>(p);
      const D3DRECT* rects =
          a->count > 0 ? reinterpret_cast<const D3DRECT*>(a + 1) : nullptr;
      hr = o_Clear(d, a->count, rects, a->flags, a->color, a->z, a->stencil);
      break;
    }
    case kOpSetTransform: {
      const ArgTransform* a = reinterpret_cast<const ArgTransform*>(p);
      hr = o_SetTransform(d, a->state, &a->matrix);
      break;
    }
    case kOpSetViewport:
      hr = o_SetViewport(d, reinterpret_cast<const D3DVIEWPORT9*>(p));
      break;
    case kOpSetMaterial:
      hr = o_SetMaterial(d, reinterpret_cast<const D3DMATERIAL9*>(p));
      break;
    case kOpSetLight: {
      const ArgLight* a = reinterpret_cast<const ArgLight*>(p);
      hr = o_SetLight(d, a->index, &a->light);
      break;
    }
    case kOpLightEnable: {
      const ArgLightEnable* a = reinterpret_cast<const ArgLightEnable*>(p);
      hr = o_LightEnable(d, a->index, a->on);
      break;
    }
    case kOpSetClipPlane: {
      const ArgClipPlane* a = reinterpret_cast<const ArgClipPlane*>(p);
      hr = o_SetClipPlane(d, a->index, a->plane);
      break;
    }
    case kOpSetRenderState: {
      const ArgState* a = reinterpret_cast<const ArgState*>(p);
      hr = o_SetRenderState(d, static_cast<D3DRENDERSTATETYPE>(a->a), a->b);
      break;
    }
    case kOpSetTexture: {
      const ArgTexture* a = reinterpret_cast<const ArgTexture*>(p);
      hr = o_SetTexture(d, a->stage, a->texture);
      Drop(a->texture);
      break;
    }
    case kOpSetTextureStageState: {
      const ArgState* a = reinterpret_cast<const ArgState*>(p);
      hr = o_SetTextureStageState(
          d, a->a, static_cast<D3DTEXTURESTAGESTATETYPE>(a->b), a->c);
      break;
    }
    case kOpSetSamplerState: {
      const ArgState* a = reinterpret_cast<const ArgState*>(p);
      hr = o_SetSamplerState(d, a->a, static_cast<D3DSAMPLERSTATETYPE>(a->b),
                             a->c);
      break;
    }
    case kOpSetScissorRect:
      hr = o_SetScissorRect(d, reinterpret_cast<const RECT*>(p));
      break;
    case kOpDrawPrimitive: {
      Unbind();
      const ArgDraw* a = reinterpret_cast<const ArgDraw*>(p);
      hr = o_DrawPrimitive(d, a->type, a->a, a->b);
      break;
    }
    case kOpDrawIndexedPrimitive: {
      Unbind();
      const ArgDraw* a = reinterpret_cast<const ArgDraw*>(p);
      hr = o_DrawIndexedPrimitive(d, a->type, a->base, a->a, a->b, a->c, a->d);
      break;
    }
    case kOpDrawPrimitiveUP: {
      Unbind();
      const ArgDrawUP* a = reinterpret_cast<const ArgDrawUP*>(p);
      hr = o_DrawPrimitiveUP(d, a->type, a->count, a + 1, a->stride);
      break;
    }
    case kOpDrawIndexedPrimitiveUP: {
      const ArgDrawIndexedUP* a = reinterpret_cast<const ArgDrawIndexedUP*>(p);
      if (h->unused != 0) {
        const Prepared& b = g_prepared[h->unused - 1];
        if (!g_bound || g_bound_stride != a->stride) {
          o_SetStreamSource(d, 0, g_vb, 0, a->stride);
          g_bound_stride = a->stride;
        }
        if (!g_bound) o_SetIndices(d, g_ib);
        g_bound = true;
        hr = o_DrawIndexedPrimitive(d, a->type, b.base_vertex, a->min_index,
                                    a->vertex_count, b.start_index,
                                    a->primitive_count);
        InterlockedIncrement(&g_buffer_draws);
        break;
      }
      Unbind();
      InterlockedIncrement(&g_up_draws);
      const uint8_t* vertices = reinterpret_cast<const uint8_t*>(a + 1);
      const uint8_t* indices = vertices + ((a->vertex_bytes + 7u) & ~7u);
      hr = o_DrawIndexedPrimitiveUP(d, a->type, a->min_index, a->vertex_count,
                                    a->primitive_count, indices,
                                    a->index_format, vertices, a->stride);
      break;
    }
    case kOpSetVertexDeclaration: {
      const ArgObject* a = reinterpret_cast<const ArgObject*>(p);
      hr = o_SetVertexDeclaration(
          d, static_cast<IDirect3DVertexDeclaration9*>(a->object));
      Drop(a->object);
      break;
    }
    case kOpSetFVF:
      hr = o_SetFVF(d, *reinterpret_cast<const DWORD*>(p));
      break;
    case kOpIssueQuery: {
      const ArgObject* a = reinterpret_cast<const ArgObject*>(p);
      hr = static_cast<IDirect3DQuery9*>(a->object)->Issue(D3DISSUE_END);
      break;
    }
    case kOpSetVertexShader: {
      const ArgObject* a = reinterpret_cast<const ArgObject*>(p);
      hr = o_SetVertexShader(d, static_cast<IDirect3DVertexShader9*>(a->object));
      Drop(a->object);
      break;
    }
    case kOpSetVertexShaderConstantF: {
      const ArgConstants* a = reinterpret_cast<const ArgConstants*>(p);
      hr = o_SetVertexShaderConstantF(
          d, a->start, reinterpret_cast<const float*>(a + 1), a->count);
      break;
    }
    case kOpSetStreamSource: {
      Unbind();
      const ArgStream* a = reinterpret_cast<const ArgStream*>(p);
      hr = o_SetStreamSource(d, a->stream, a->buffer, a->offset, a->stride);
      Drop(a->buffer);
      break;
    }
    case kOpSetIndices: {
      Unbind();
      const ArgObject* a = reinterpret_cast<const ArgObject*>(p);
      hr = o_SetIndices(d, static_cast<IDirect3DIndexBuffer9*>(a->object));
      Drop(a->object);
      break;
    }
    case kOpSetPixelShader: {
      const ArgObject* a = reinterpret_cast<const ArgObject*>(p);
      hr = o_SetPixelShader(d, static_cast<IDirect3DPixelShader9*>(a->object));
      Drop(a->object);
      break;
    }
    case kOpSetPixelShaderConstantF: {
      const ArgConstants* a = reinterpret_cast<const ArgConstants*>(p);
      hr = o_SetPixelShaderConstantF(
          d, a->start, reinterpret_cast<const float*>(a + 1), a->count);
      break;
    }
    default:
      break;
  }
  if (h->op == kOpDrawPrimitive || h->op == kOpDrawIndexedPrimitive ||
      h->op == kOpDrawPrimitiveUP || h->op == kOpDrawIndexedPrimitiveUP) {
    CountDrawForFlush();
  }
  if (FAILED(hr)) InterlockedIncrement(&g_failed_calls);
  return h->bytes;
}

// Makes the calls of each record up to `until` (mode 2, on the frame thread).
void PlayHere(uint32_t until) {
  uint32_t read = LoadIndex(&g_read);
  while (read != until) {
    read += Play(g_queue + (read & (kQueueBytes - 1)));
  }
  StoreIndex(&g_read, read);
}

// THE RENDER THREAD. It reads the queue in order. In a walk it spins while the
// queue is empty, because a wake from a sleep is a call of the kernel on the
// frame thread for each record. Outside the walks it sleeps.
DWORD WINAPI Worker(LPVOID) {
  for (;;) {
    uint32_t read = LoadIndex(&g_read);
    uint32_t write = LoadIndex(&g_write);
    if (read == write || g_worker_on == 0) {
      if (g_drain_waits != 0) {
        SetEvent(g_idle_event);
      }
      bool more = false;
      for (int i = 0; i < 2000 || g_recording; ++i) {
        YieldProcessor();
        if (LoadIndex(&g_write) != read &&
            g_worker_on != 0) {
          more = true;
          break;
        }
      }
      if (!more) {
        InterlockedExchange(&g_worker_waits, 1);
        if (LoadIndex(&g_write) == read ||
            g_worker_on == 0) {
          WaitForSingleObject(g_work_event, INFINITE);
        }
        InterlockedExchange(&g_worker_waits, 0);
      }
      continue;
    }
    // The count of the reader goes out after each 64 records and at the end
    // of what was written, and the count of the writer is read again only
    // then: each read of a count of the other core moves its cache line.
    const uint64_t start = Ticks();
    // The buffers take the draws of [read, write) before they play. A new
    // part of the queue is prepared only when the part before it has
    // played, because the marks of the records point into g_prepared.
    Prepare(read, write);
    while (read != write) {
      int played = 0;
      while (read != write && played < 64) {
        read += Play(g_queue + (read & (kQueueBytes - 1)));
        ++played;
      }
      if (read == write) {
        const uint32_t next = LoadIndex(&g_write);
        if (next == write) {
          // The queue is empty: the device gets the state that it has with
          // no buffers before a stop can see it.
          Unbind();
        } else {
          Prepare(write, next);
          write = next;
        }
      }
      StoreIndex(&g_read, read);
    }
    InterlockedExchangeAdd64(&g_busy_ticks,
                             static_cast<LONG64>(Ticks() - start));
  }
}

// ---------------------------------------------------------------------------
// The writer, on the frame thread.

// Waits until the queue is empty: the render thread made each call, or in
// mode 2 this thread made them.
void Drain() {
  const uint32_t write = LoadIndex(&g_write);
  if (LoadIndex(&g_read) == write) return;
  if (g_mode == 2) {
    PlayHere(write);
    return;
  }
  for (int i = 0; i < 4000; ++i) {
    if (LoadIndex(&g_read) == write) return;
    YieldProcessor();
  }
  InterlockedExchange(&g_drain_waits, 1);
  while (LoadIndex(&g_read) != write) {
    WaitForSingleObject(g_idle_event, 1);
  }
  InterlockedExchange(&g_drain_waits, 0);
}

// A stop: each call before it goes to Direct3D 9 now.
void Stop(int which) {
  const uint64_t start = Ticks();
  Drain();
  g_stop_ticks += Ticks() - start;
  if (which >= 0 && which < kStops) ++g_stops[which];
}

// Room for a record of `payload` bytes, or null when the record is too
// large for the queue: then the caller makes the call itself after a stop.
uint8_t* Reserve(Op op, uint32_t payload) {
  const uint32_t bytes = render_ring::RecordBytes(payload);
  render_ring::Plan plan;
  uint32_t write = LoadIndex(&g_write);
  if (!render_ring::PlanRecord(write, kQueueBytes, bytes, &plan)) {
    ++g_too_large;
    return nullptr;
  }
  // The count of the reader is read again only when the room that the last
  // read showed is not enough.
  static uint32_t s_read = 0;
  if (!render_ring::HasRoom(write, s_read, kQueueBytes, plan.need)) {
    s_read = LoadIndex(&g_read);
  }
  if (!render_ring::HasRoom(write, s_read, kQueueBytes, plan.need)) {
    ++g_full_waits;
    if (g_mode == 2) {
      PlayHere(write);
    } else {
      SetEvent(g_work_event);
      while (!render_ring::HasRoom(write, LoadIndex(&g_read), kQueueBytes,
                                   plan.need)) {
        YieldProcessor();
      }
    }
    s_read = LoadIndex(&g_read);
  }
  if (plan.wrap) {
    Header* w = reinterpret_cast<Header*>(g_queue + (write & (kQueueBytes - 1)));
    w->op = kOpWrap;
    w->unused = 0;
    w->bytes = plan.wrap_bytes;
    write += plan.wrap_bytes;
    _ReadWriteBarrier();
    StoreIndex(&g_write, write);
  }
  Header* h = reinterpret_cast<Header*>(g_queue + (write & (kQueueBytes - 1)));
  h->op = static_cast<uint16_t>(op);
  h->unused = 0;
  h->bytes = bytes;
  ++g_records[op];
  g_bytes += bytes;
  return reinterpret_cast<uint8_t*>(h + 1);
}

// The record that Reserve gave goes to the render thread.
void Commit() {
  const uint32_t write = LoadIndex(&g_write);
  const Header* h =
      reinterpret_cast<const Header*>(g_queue + (write & (kQueueBytes - 1)));
  _ReadWriteBarrier();
  StoreIndex(&g_write, write + h->bytes);
  if (g_mode == 1 && g_worker_waits != 0) {
    SetEvent(g_work_event);
  }
}

template <typename T>
bool Simple(Op op, const T& arg) {
  uint8_t* p = Reserve(op, sizeof(T));
  if (p == nullptr) return false;
  memcpy(p, &arg, sizeof(T));
  Commit();
  return true;
}

// THE VIEWPORT OF BOTH EYES (one_walk_merge, one_walk.cpp). One walk can
// draw both copies of a batch in one draw, with one viewport over both eyes.
// The device keeps that viewport while such draws follow each other. The
// walk keeps the viewport that the game set: GetViewport gives it, and a
// SetViewport of it changes nothing. A call that needs the viewport of the
// game puts it back first: a draw or a clear of one eye (one_walk.cpp), a
// stop, and the end of the walk. A new target sets the viewport itself.
void ViewportPutBack() {
  if (!g_vp_moved) return;
  g_vp_moved = false;
  if (!Simple(kOpSetViewport, g_vp_logical)) {
    Stop(kSetViewport);
    o_SetViewport(g_device, &g_vp_logical);
  }
  g_kept.viewport = g_vp_logical;
  g_kept.viewport_ok = true;
}

void Hold(IUnknown* object) {
  if (object != nullptr) object->AddRef();
}

// The vertices of a draw with no index array.
uint32_t IndexCount(D3DPRIMITIVETYPE type, UINT primitives) {
  switch (type) {
    case D3DPT_POINTLIST: return primitives;
    case D3DPT_LINELIST: return primitives * 2;
    case D3DPT_LINESTRIP: return primitives + 1;
    case D3DPT_TRIANGLELIST: return primitives * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return primitives + 2;
    default: return 0;
  }
}

// THE READS: a kept value, or the value of Direct3D 9 after a stop.
void KeepTarget(DWORD index, IDirect3DSurface9* surface) {
  if (index >= 4) return;
  g_kept.target_ok[index] = true;
  g_kept.target[index] = surface;
  if (index != 0) return;
  // SetRenderTarget of target 0 sets the viewport and the scissor rect to the
  // whole target.
  g_vp_moved = false;
  D3DSURFACE_DESC desc;
  if (surface != nullptr && SUCCEEDED(surface->GetDesc(&desc))) {
    const D3DVIEWPORT9 all = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    g_kept.viewport = all;
    g_kept.viewport_ok = true;
    const RECT whole = {0, 0, static_cast<LONG>(desc.Width),
                        static_cast<LONG>(desc.Height)};
    g_kept.scissor = whole;
    g_kept.scissor_ok = true;
  } else {
    g_kept.viewport_ok = false;
    g_kept.scissor_ok = false;
  }
}

// ---------------------------------------------------------------------------
// The hooks of the calls that the queue takes.

HRESULT STDMETHODCALLTYPE h_SetRenderTarget(IDirect3DDevice9* self, DWORD index,
                                            IDirect3DSurface9* surface) {
  if (OnWorker()) return o_SetRenderTarget(self, index, surface);
  return OneWalkOnSetRenderTarget(self, index, surface);
}

HRESULT STDMETHODCALLTYPE h_GetRenderTarget(IDirect3DDevice9* self, DWORD index,
                                            IDirect3DSurface9** out) {
  if (!Taking(self) || index >= 4 || out == nullptr) {
    if (Taking(self)) Stop(kGetRenderTarget);
    return o_GetRenderTarget(self, index, out);
  }
  if (!g_kept.target_ok[index]) {
    Stop(kGetRenderTarget);
    const HRESULT hr = o_GetRenderTarget(self, index, out);
    g_kept.target_ok[index] = true;
    g_kept.target[index] = SUCCEEDED(hr) ? *out : nullptr;
    return hr;
  }
  *out = g_kept.target[index];
  if (*out == nullptr) return D3DERR_NOTFOUND;
  (*out)->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE h_SetDepthStencil(IDirect3DDevice9* self,
                                            IDirect3DSurface9* surface) {
  if (OnWorker()) return o_SetDepthStencil(self, surface);
  return OneWalkOnSetDepthStencil(self, surface);
}

HRESULT STDMETHODCALLTYPE h_GetDepthStencil(IDirect3DDevice9* self,
                                            IDirect3DSurface9** out) {
  if (!Taking(self) || out == nullptr) {
    if (Taking(self)) Stop(kGetDepthStencilSurface);
    return o_GetDepthStencil(self, out);
  }
  if (!g_kept.depth_ok) {
    Stop(kGetDepthStencilSurface);
    const HRESULT hr = o_GetDepthStencil(self, out);
    g_kept.depth_ok = true;
    g_kept.depth = SUCCEEDED(hr) ? *out : nullptr;
    return hr;
  }
  *out = g_kept.depth;
  if (*out == nullptr) return D3DERR_NOTFOUND;
  (*out)->AddRef();
  return D3D_OK;
}

HRESULT STDMETHODCALLTYPE h_BeginScene(IDirect3DDevice9* self) {
  if (Taking(self)) {
    uint8_t* p = Reserve(kOpBeginScene, 0);
    if (p != nullptr) {
      Commit();
      return D3D_OK;
    }
  }
  return o_BeginScene(self);
}

HRESULT STDMETHODCALLTYPE h_EndScene(IDirect3DDevice9* self) {
  if (Taking(self)) {
    uint8_t* p = Reserve(kOpEndScene, 0);
    if (p != nullptr) {
      Commit();
      return D3D_OK;
    }
  }
  return o_EndScene(self);
}

HRESULT STDMETHODCALLTYPE h_Clear(IDirect3DDevice9* self, DWORD count,
                                  const D3DRECT* rects, DWORD flags,
                                  D3DCOLOR color, float z, DWORD stencil) {
  if (OnWorker()) return o_Clear(self, count, rects, flags, color, z, stencil);
  return OneWalkOnClear(self, count, rects, flags, color, z, stencil);
}

HRESULT STDMETHODCALLTYPE h_SetTransform(IDirect3DDevice9* self,
                                         D3DTRANSFORMSTATETYPE state,
                                         const D3DMATRIX* matrix) {
  if (Taking(self) && matrix != nullptr) {
    const int slot = TransformSlot(state);
    if (slot >= 0 && g_kept.transform_ok[slot] &&
        memcmp(&g_kept.transform[slot], matrix, sizeof(D3DMATRIX)) == 0) {
      ++g_same[kOpSetTransform];
      if (g_filter) return D3D_OK;
    }
    ArgTransform a;
    a.state = state;
    a.matrix = *matrix;
    if (Simple(kOpSetTransform, a)) {
      if (slot >= 0) {
        g_kept.transform[slot] = *matrix;
        g_kept.transform_ok[slot] = 1;
      }
      return D3D_OK;
    }
  }
  return o_SetTransform(self, state, matrix);
}

HRESULT STDMETHODCALLTYPE h_SetViewport(IDirect3DDevice9* self,
                                        const D3DVIEWPORT9* viewport) {
  if (Taking(self) && viewport != nullptr && g_vp_moved) {
    if (memcmp(&g_vp_logical, viewport, sizeof(D3DVIEWPORT9)) == 0) {
      ++g_same[kOpSetViewport];
      return D3D_OK;
    }
    // A new viewport of the game: the device gets it below.
    g_vp_moved = false;
  }
  if (Taking(self) && viewport != nullptr && g_kept.viewport_ok &&
      memcmp(&g_kept.viewport, viewport, sizeof(D3DVIEWPORT9)) == 0) {
    ++g_same[kOpSetViewport];
    if (g_filter) return D3D_OK;
  }
  if (Taking(self) && viewport != nullptr &&
      Simple(kOpSetViewport, *viewport)) {
    g_kept.viewport = *viewport;
    g_kept.viewport_ok = true;
    return D3D_OK;
  }
  return o_SetViewport(self, viewport);
}

HRESULT STDMETHODCALLTYPE h_GetViewport(IDirect3DDevice9* self,
                                        D3DVIEWPORT9* viewport) {
  if (Taking(self) && viewport != nullptr && g_vp_moved) {
    *viewport = g_vp_logical;
    return D3D_OK;
  }
  if (Taking(self) && viewport != nullptr) {
    if (g_kept.viewport_ok) {
      *viewport = g_kept.viewport;
      return D3D_OK;
    }
    Stop(kGetViewport);
    const HRESULT hr = o_GetViewport(self, viewport);
    if (SUCCEEDED(hr)) {
      g_kept.viewport = *viewport;
      g_kept.viewport_ok = true;
    }
    return hr;
  }
  return o_GetViewport(self, viewport);
}

HRESULT STDMETHODCALLTYPE h_SetMaterial(IDirect3DDevice9* self,
                                        const D3DMATERIAL9* material) {
  if (Taking(self) && material != nullptr &&
      Simple(kOpSetMaterial, *material)) {
    return D3D_OK;
  }
  return o_SetMaterial(self, material);
}

HRESULT STDMETHODCALLTYPE h_SetLight(IDirect3DDevice9* self, DWORD index,
                                     const D3DLIGHT9* light) {
  if (Taking(self) && light != nullptr) {
    ArgLight a;
    a.index = index;
    a.light = *light;
    if (Simple(kOpSetLight, a)) return D3D_OK;
  }
  return o_SetLight(self, index, light);
}

HRESULT STDMETHODCALLTYPE h_LightEnable(IDirect3DDevice9* self, DWORD index,
                                        BOOL on) {
  if (Taking(self)) {
    const ArgLightEnable a = {index, on};
    if (Simple(kOpLightEnable, a)) return D3D_OK;
  }
  return o_LightEnable(self, index, on);
}

HRESULT STDMETHODCALLTYPE h_SetClipPlane(IDirect3DDevice9* self, DWORD index,
                                         const float* plane) {
  if (Taking(self) && plane != nullptr) {
    ArgClipPlane a;
    a.index = index;
    memcpy(a.plane, plane, sizeof(a.plane));
    if (Simple(kOpSetClipPlane, a)) return D3D_OK;
  }
  return o_SetClipPlane(self, index, plane);
}

HRESULT STDMETHODCALLTYPE h_SetRenderState(IDirect3DDevice9* self,
                                           D3DRENDERSTATETYPE state,
                                           DWORD value) {
  if (Taking(self)) {
    if (static_cast<DWORD>(state) < 256 && g_kept.state_ok[state] &&
        g_kept.state[state] == value) {
      ++g_same[kOpSetRenderState];
      if (g_filter) return D3D_OK;
    }
    const ArgState a = {static_cast<DWORD>(state), value, 0};
    if (Simple(kOpSetRenderState, a)) {
      if (static_cast<DWORD>(state) < 256) {
        g_kept.state[state] = value;
        g_kept.state_ok[state] = 1;
      }
      return D3D_OK;
    }
  }
  return o_SetRenderState(self, state, value);
}

HRESULT STDMETHODCALLTYPE h_GetRenderState(IDirect3DDevice9* self,
                                           D3DRENDERSTATETYPE state,
                                           DWORD* value) {
  if (Taking(self) && value != nullptr) {
    const DWORD s = static_cast<DWORD>(state);
    if (s < 256 && g_kept.state_ok[s]) {
      *value = g_kept.state[s];
      return D3D_OK;
    }
    Stop(kGetRenderState);
    const HRESULT hr = o_GetRenderState(self, state, value);
    if (SUCCEEDED(hr) && s < 256) {
      g_kept.state[s] = *value;
      g_kept.state_ok[s] = 1;
    }
    return hr;
  }
  return o_GetRenderState(self, state, value);
}

HRESULT STDMETHODCALLTYPE h_SetTexture(IDirect3DDevice9* self, DWORD stage,
                                       IDirect3DBaseTexture9* texture) {
  if (Taking(self)) {
    if (stage < static_cast<DWORD>(kKeptStages) && g_kept.texture_ok[stage] &&
        g_kept.texture[stage] == texture) {
      ++g_same[kOpSetTexture];
      if (g_filter) return D3D_OK;
    }
    const ArgTexture a = {stage, texture};
    Hold(texture);
    if (Simple(kOpSetTexture, a)) {
      if (stage < static_cast<DWORD>(kKeptStages)) {
        g_kept.texture[stage] = texture;
        g_kept.texture_ok[stage] = 1;
      }
      return D3D_OK;
    }
    Drop(texture);
  }
  return o_SetTexture(self, stage, texture);
}

// The pair of GetTexture and Release of dxwrapper (see THE PAIRS).
bool IsKnownTexture(IUnknown* self);
void* g_outer_get_return = nullptr;
void* g_outer_release_return = nullptr;
IUnknown* volatile g_outer_object = nullptr;
volatile DWORD g_outer_thread = 0;

HRESULT STDMETHODCALLTYPE h_GetTexture(IDirect3DDevice9* self, DWORD stage,
                                       IDirect3DBaseTexture9** out) {
  if (Taking(self) && out != nullptr) {
    if (stage < static_cast<DWORD>(kKeptStages) && g_kept.texture_ok[stage]) {
      *out = g_kept.texture[stage];
      if (*out == nullptr) return D3D_OK;
      if (g_lock_free && g_outer_get_return != nullptr &&
          _ReturnAddress() == g_outer_get_return && g_outer_object == nullptr &&
          IsKnownTexture(*out)) {
        g_outer_object = *out;
        g_outer_thread = GetCurrentThreadId();
      } else {
        (*out)->AddRef();
      }
      return D3D_OK;
    }
    Stop(kGetTexture);
    const HRESULT hr = o_GetTexture(self, stage, out);
    if (SUCCEEDED(hr) && stage < static_cast<DWORD>(kKeptStages)) {
      g_kept.texture[stage] = *out;
      g_kept.texture_ok[stage] = 1;
    }
    return hr;
  }
  return o_GetTexture(self, stage, out);
}

HRESULT STDMETHODCALLTYPE h_SetTextureStageState(
    IDirect3DDevice9* self, DWORD stage, D3DTEXTURESTAGESTATETYPE type,
    DWORD value) {
  if (Taking(self)) {
    if (stage < static_cast<DWORD>(kKeptStages) &&
        static_cast<int>(type) < kKeptStageStates &&
        g_kept.stage_ok[stage][type] && g_kept.stage[stage][type] == value) {
      ++g_same[kOpSetTextureStageState];
      if (g_filter) return D3D_OK;
    }
    const ArgState a = {stage, static_cast<DWORD>(type), value};
    if (Simple(kOpSetTextureStageState, a)) {
      if (stage < static_cast<DWORD>(kKeptStages) &&
          static_cast<int>(type) < kKeptStageStates) {
        g_kept.stage[stage][type] = value;
        g_kept.stage_ok[stage][type] = 1;
      }
      return D3D_OK;
    }
  }
  return o_SetTextureStageState(self, stage, type, value);
}

HRESULT STDMETHODCALLTYPE h_GetTextureStageState(
    IDirect3DDevice9* self, DWORD stage, D3DTEXTURESTAGESTATETYPE type,
    DWORD* value) {
  if (Taking(self) && value != nullptr) {
    const bool kept_here = stage < static_cast<DWORD>(kKeptStages) &&
                           static_cast<int>(type) < kKeptStageStates;
    if (kept_here && g_kept.stage_ok[stage][type]) {
      *value = g_kept.stage[stage][type];
      return D3D_OK;
    }
    Stop(kGetTextureStageState);
    const HRESULT hr = o_GetTextureStageState(self, stage, type, value);
    if (SUCCEEDED(hr) && kept_here) {
      g_kept.stage[stage][type] = *value;
      g_kept.stage_ok[stage][type] = 1;
    }
    return hr;
  }
  return o_GetTextureStageState(self, stage, type, value);
}

HRESULT STDMETHODCALLTYPE h_SetSamplerState(IDirect3DDevice9* self,
                                            DWORD sampler,
                                            D3DSAMPLERSTATETYPE type,
                                            DWORD value) {
  if (Taking(self)) {
    if (sampler < static_cast<DWORD>(kKeptStages) &&
        static_cast<int>(type) < kKeptSamplerStates &&
        g_kept.sampler_ok[sampler][type] &&
        g_kept.sampler[sampler][type] == value) {
      ++g_same[kOpSetSamplerState];
      if (g_filter) return D3D_OK;
    }
    const ArgState a = {sampler, static_cast<DWORD>(type), value};
    if (Simple(kOpSetSamplerState, a)) {
      if (sampler < static_cast<DWORD>(kKeptStages) &&
          static_cast<int>(type) < kKeptSamplerStates) {
        g_kept.sampler[sampler][type] = value;
        g_kept.sampler_ok[sampler][type] = 1;
      }
      return D3D_OK;
    }
  }
  return o_SetSamplerState(self, sampler, type, value);
}

HRESULT STDMETHODCALLTYPE h_GetSamplerState(IDirect3DDevice9* self,
                                            DWORD sampler,
                                            D3DSAMPLERSTATETYPE type,
                                            DWORD* value) {
  if (Taking(self) && value != nullptr) {
    const bool kept_here = sampler < static_cast<DWORD>(kKeptStages) &&
                           static_cast<int>(type) < kKeptSamplerStates;
    if (kept_here && g_kept.sampler_ok[sampler][type]) {
      *value = g_kept.sampler[sampler][type];
      return D3D_OK;
    }
    Stop(kGetSamplerState);
    const HRESULT hr = o_GetSamplerState(self, sampler, type, value);
    if (SUCCEEDED(hr) && kept_here) {
      g_kept.sampler[sampler][type] = *value;
      g_kept.sampler_ok[sampler][type] = 1;
    }
    return hr;
  }
  return o_GetSamplerState(self, sampler, type, value);
}

HRESULT STDMETHODCALLTYPE h_SetScissorRect(IDirect3DDevice9* self,
                                           const RECT* rect) {
  if (Taking(self) && rect != nullptr && Simple(kOpSetScissorRect, *rect)) {
    g_kept.scissor = *rect;
    g_kept.scissor_ok = true;
    return D3D_OK;
  }
  return o_SetScissorRect(self, rect);
}

HRESULT STDMETHODCALLTYPE h_GetScissorRect(IDirect3DDevice9* self, RECT* rect) {
  if (Taking(self) && rect != nullptr) {
    if (g_kept.scissor_ok) {
      *rect = g_kept.scissor;
      return D3D_OK;
    }
    Stop(kGetScissorRect);
    const HRESULT hr = o_GetScissorRect(self, rect);
    if (SUCCEEDED(hr)) {
      g_kept.scissor = *rect;
      g_kept.scissor_ok = true;
    }
    return hr;
  }
  return o_GetScissorRect(self, rect);
}

HRESULT STDMETHODCALLTYPE h_DrawPrimitive(IDirect3DDevice9* self,
                                          D3DPRIMITIVETYPE type, UINT start,
                                          UINT count) {
  if (OnWorker()) return o_DrawPrimitive(self, type, start, count);
  return OneWalkOnDrawPrimitive(self, type, start, count);
}

HRESULT STDMETHODCALLTYPE h_DrawIndexedPrimitive(IDirect3DDevice9* self,
                                                 D3DPRIMITIVETYPE type,
                                                 INT base, UINT min_index,
                                                 UINT vertices, UINT start,
                                                 UINT count) {
  if (OnWorker()) {
    return o_DrawIndexedPrimitive(self, type, base, min_index, vertices, start,
                                  count);
  }
  return OneWalkOnDrawIndexedPrimitive(self, type, base, min_index, vertices,
                                       start, count);
}

HRESULT STDMETHODCALLTYPE h_DrawPrimitiveUP(IDirect3DDevice9* self,
                                            D3DPRIMITIVETYPE type, UINT count,
                                            const void* vertices, UINT stride) {
  if (OnWorker()) return o_DrawPrimitiveUP(self, type, count, vertices, stride);
  return OneWalkOnDrawPrimitiveUP(self, type, count, vertices, stride);
}

HRESULT STDMETHODCALLTYPE h_DrawIndexedPrimitiveUP(
    IDirect3DDevice9* self, D3DPRIMITIVETYPE type, UINT min_index,
    UINT vertex_count, UINT primitive_count, const void* indices,
    D3DFORMAT index_format, const void* vertices, UINT stride) {
  if (OnWorker()) {
    return o_DrawIndexedPrimitiveUP(self, type, min_index, vertex_count,
                                    primitive_count, indices, index_format,
                                    vertices, stride);
  }
  return OneWalkOnDrawIndexedPrimitiveUP(self, type, min_index, vertex_count,
                                         primitive_count, indices,
                                         index_format, vertices, stride);
}

HRESULT STDMETHODCALLTYPE h_SetVertexDeclaration(
    IDirect3DDevice9* self, IDirect3DVertexDeclaration9* declaration) {
  if (Taking(self)) {
    const ArgObject a = {declaration};
    Hold(declaration);
    if (Simple(kOpSetVertexDeclaration, a)) {
      // The format of a declaration is not kept here.
      g_kept.fvf_ok = false;
      return D3D_OK;
    }
    Drop(declaration);
  }
  return o_SetVertexDeclaration(self, declaration);
}

HRESULT STDMETHODCALLTYPE h_SetFVF(IDirect3DDevice9* self, DWORD fvf) {
  if (Taking(self) && g_kept.fvf_ok && g_kept.fvf == fvf) {
    ++g_same[kOpSetFVF];
    if (g_filter) return D3D_OK;
  }
  if (Taking(self) && Simple(kOpSetFVF, fvf)) {
    g_kept.fvf = fvf;
    g_kept.fvf_ok = true;
    return D3D_OK;
  }
  return o_SetFVF(self, fvf);
}

HRESULT STDMETHODCALLTYPE h_GetFVF(IDirect3DDevice9* self, DWORD* fvf) {
  if (Taking(self) && fvf != nullptr) {
    if (g_kept.fvf_ok) {
      *fvf = g_kept.fvf;
      return D3D_OK;
    }
    Stop(kGetFVF);
    const HRESULT hr = o_GetFVF(self, fvf);
    if (SUCCEEDED(hr)) {
      g_kept.fvf = *fvf;
      g_kept.fvf_ok = true;
    }
    return hr;
  }
  return o_GetFVF(self, fvf);
}

HRESULT STDMETHODCALLTYPE h_SetVertexShader(IDirect3DDevice9* self,
                                            IDirect3DVertexShader9* shader) {
  if (Taking(self)) {
    const ArgObject a = {shader};
    Hold(shader);
    if (Simple(kOpSetVertexShader, a)) return D3D_OK;
    Drop(shader);
  }
  return o_SetVertexShader(self, shader);
}

HRESULT Constants(Op op, SetConstantsFn original, IDirect3DDevice9* self,
                  UINT start, const float* data, UINT count) {
  if (Taking(self) && data != nullptr) {
    const uint32_t bytes = count * 4 * sizeof(float);
    uint8_t* p = Reserve(op, sizeof(ArgConstants) + bytes);
    if (p != nullptr) {
      const ArgConstants a = {start, count};
      memcpy(p, &a, sizeof(a));
      memcpy(p + sizeof(a), data, bytes);
      Commit();
      return D3D_OK;
    }
  }
  return original(self, start, data, count);
}

HRESULT STDMETHODCALLTYPE h_SetVertexShaderConstantF(IDirect3DDevice9* self,
                                                     UINT start,
                                                     const float* data,
                                                     UINT count) {
  return Constants(kOpSetVertexShaderConstantF, o_SetVertexShaderConstantF,
                   self, start, data, count);
}

HRESULT STDMETHODCALLTYPE h_SetPixelShaderConstantF(IDirect3DDevice9* self,
                                                    UINT start,
                                                    const float* data,
                                                    UINT count) {
  return Constants(kOpSetPixelShaderConstantF, o_SetPixelShaderConstantF, self,
                   start, data, count);
}

HRESULT STDMETHODCALLTYPE h_SetStreamSource(IDirect3DDevice9* self,
                                            UINT stream,
                                            IDirect3DVertexBuffer9* buffer,
                                            UINT offset, UINT stride) {
  if (Taking(self)) {
    const ArgStream a = {stream, buffer, offset, stride};
    Hold(buffer);
    if (Simple(kOpSetStreamSource, a)) return D3D_OK;
    Drop(buffer);
  }
  return o_SetStreamSource(self, stream, buffer, offset, stride);
}

HRESULT STDMETHODCALLTYPE h_SetIndices(IDirect3DDevice9* self,
                                       IDirect3DIndexBuffer9* buffer) {
  if (Taking(self)) {
    const ArgObject a = {buffer};
    Hold(buffer);
    if (Simple(kOpSetIndices, a)) return D3D_OK;
    Drop(buffer);
  }
  return o_SetIndices(self, buffer);
}

HRESULT STDMETHODCALLTYPE h_SetPixelShader(IDirect3DDevice9* self,
                                           IDirect3DPixelShader9* shader) {
  if (Taking(self)) {
    const ArgObject a = {shader};
    Hold(shader);
    if (Simple(kOpSetPixelShader, a)) return D3D_OK;
    Drop(shader);
  }
  return o_SetPixelShader(self, shader);
}

// ---------------------------------------------------------------------------
// THE STOPS of each other call. A thunk of machine code for each call:
//
//   pushad
//   push <the number of the stop>
//   mov eax, OtherCall
//   call eax
//   add esp, 4
//   popad
//   jmp dword ptr [<the original of the call>]
//
// The stack is as it was when the call came, so the original gets its
// arguments and returns to the caller.

// Each change of a state in the walk goes through the hooks above, so a
// stop keeps the kept values. Only a reset of the device and a state block
// change states with no call of a hook: then a read needs a stop again.
bool ChangesStates(int which) {
  return which == 16 ||                  // Reset
         (which >= 59 && which <= 61) ||  // the state blocks
         which == 132;                    // ResetEx
}

void __cdecl OtherCall(int which) {
  const DWORD thread = GetCurrentThreadId();
  if (g_recording && thread == g_frame_thread) {
    ViewportPutBack();
    Stop(which);
    if (ChangesStates(which)) ResetKept();
  }
  // A Reset needs each buffer of the default pool to be released. The render
  // thread is idle here: the queue is empty after the stop, or no walk runs.
  if ((which == 16 || which == 132) && thread != g_worker_id) {
    ReleaseBuffers();
  }
}

const int kThunkBytes = 32;

void* MakeThunk(int index, int which) {
  uint8_t* c = g_thunks + index * kThunkBytes;
  int i = 0;
  c[i++] = 0x60;  // pushad
  c[i++] = 0x68;  // push imm32
  *reinterpret_cast<int32_t*>(c + i) = which;
  i += 4;
  c[i++] = 0xB8;  // mov eax, imm32
  *reinterpret_cast<uint32_t*>(c + i) =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&OtherCall));
  i += 4;
  c[i++] = 0xFF;  // call eax
  c[i++] = 0xD0;
  c[i++] = 0x83;  // add esp, 4
  c[i++] = 0xC4;
  c[i++] = 0x04;
  c[i++] = 0x61;  // popad
  c[i++] = 0xFF;  // jmp dword ptr [imm32]
  c[i++] = 0x25;
  *reinterpret_cast<uint32_t*>(c + i) = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(&g_thunk_original[index]));
  return c;
}

int g_thunk_count = 0;

// A stop on the call at `target`. True when the hook is in, or when a hook
// of this file is on that code already.
bool HookStop(void* target, int which, bool* already) {
  *already = false;
  if (g_thunk_count >= kMaxThunks) return false;
  const int index = g_thunk_count;
  void* thunk = MakeThunk(index, which);
  MH_STATUS status = MH_CreateHook(target, thunk, &g_thunk_original[index]);
  if (status == MH_ERROR_ALREADY_CREATED) {
    *already = true;
    return true;
  }
  if (status == MH_OK) status = MH_QueueEnableHook(target);
  if (status != MH_OK) return false;
  ++g_thunk_count;
  return true;
}

bool HookTyped(void** vtable, int slot, void* hook, void** original) {
  MH_STATUS status = MH_CreateHook(vtable[slot], hook, original);
  if (status == MH_OK) status = MH_QueueEnableHook(vtable[slot]);
  return status == MH_OK;
}

// The seven calls of one walk.
bool InstallSeven(void** vtable) {
  bool ok = true;
  ok &= HookTyped(vtable, kSetRenderTarget,
                  reinterpret_cast<void*>(&h_SetRenderTarget),
                  reinterpret_cast<void**>(&o_SetRenderTarget));
  ok &= HookTyped(vtable, kSetDepthStencilSurface,
                  reinterpret_cast<void*>(&h_SetDepthStencil),
                  reinterpret_cast<void**>(&o_SetDepthStencil));
  ok &= HookTyped(vtable, kClear, reinterpret_cast<void*>(&h_Clear),
                  reinterpret_cast<void**>(&o_Clear));
  ok &= HookTyped(vtable, kDrawPrimitive,
                  reinterpret_cast<void*>(&h_DrawPrimitive),
                  reinterpret_cast<void**>(&o_DrawPrimitive));
  ok &= HookTyped(vtable, kDrawIndexedPrimitive,
                  reinterpret_cast<void*>(&h_DrawIndexedPrimitive),
                  reinterpret_cast<void**>(&o_DrawIndexedPrimitive));
  ok &= HookTyped(vtable, kDrawPrimitiveUP,
                  reinterpret_cast<void*>(&h_DrawPrimitiveUP),
                  reinterpret_cast<void**>(&o_DrawPrimitiveUP));
  ok &= HookTyped(vtable, kDrawIndexedPrimitiveUP,
                  reinterpret_cast<void*>(&h_DrawIndexedPrimitiveUP),
                  reinterpret_cast<void**>(&o_DrawIndexedPrimitiveUP));
  return ok;
}

struct Typed {
  int slot;
  void* hook;
  void** original;
};

// The slots that have a hook of their own, and are not a stop.
bool HasOwnHook(int slot) {
  switch (slot) {
    case kQueryInterface:
    case kAddRef:
    case kRelease:                   // gpu_bridge.cpp
    case kCreateDepthStencilSurface:  // gpu_bridge.cpp, which stops itself
    case kSetRenderTarget:
    case kSetDepthStencilSurface:
    case kClear:
    case kDrawPrimitive:
    case kDrawIndexedPrimitive:
    case kDrawPrimitiveUP:
    case kDrawIndexedPrimitiveUP:
      return true;
    default:
      return false;
  }
}

// Each other call: the typed hooks of the record, the stops of the device,
// and the stops of the locks of the resources.
bool InstallAll(IDirect3DDevice9* device) {
  void** vtable = *reinterpret_cast<void***>(device);
  const Typed typed[] = {
      {kGetRenderTarget, reinterpret_cast<void*>(&h_GetRenderTarget), reinterpret_cast<void**>(&o_GetRenderTarget)},
      {kGetDepthStencilSurface, reinterpret_cast<void*>(&h_GetDepthStencil), reinterpret_cast<void**>(&o_GetDepthStencil)},
      {kBeginScene, reinterpret_cast<void*>(&h_BeginScene), reinterpret_cast<void**>(&o_BeginScene)},
      {kEndScene, reinterpret_cast<void*>(&h_EndScene), reinterpret_cast<void**>(&o_EndScene)},
      {kSetTransform, reinterpret_cast<void*>(&h_SetTransform), reinterpret_cast<void**>(&o_SetTransform)},
      {kSetViewport, reinterpret_cast<void*>(&h_SetViewport), reinterpret_cast<void**>(&o_SetViewport)},
      {kGetViewport, reinterpret_cast<void*>(&h_GetViewport), reinterpret_cast<void**>(&o_GetViewport)},
      {kSetMaterial, reinterpret_cast<void*>(&h_SetMaterial), reinterpret_cast<void**>(&o_SetMaterial)},
      {kSetLight, reinterpret_cast<void*>(&h_SetLight), reinterpret_cast<void**>(&o_SetLight)},
      {kLightEnable, reinterpret_cast<void*>(&h_LightEnable), reinterpret_cast<void**>(&o_LightEnable)},
      {kSetClipPlane, reinterpret_cast<void*>(&h_SetClipPlane), reinterpret_cast<void**>(&o_SetClipPlane)},
      {kSetRenderState, reinterpret_cast<void*>(&h_SetRenderState), reinterpret_cast<void**>(&o_SetRenderState)},
      {kGetRenderState, reinterpret_cast<void*>(&h_GetRenderState), reinterpret_cast<void**>(&o_GetRenderState)},
      {kSetTexture, reinterpret_cast<void*>(&h_SetTexture), reinterpret_cast<void**>(&o_SetTexture)},
      {kGetTexture, reinterpret_cast<void*>(&h_GetTexture), reinterpret_cast<void**>(&o_GetTexture)},
      {kSetTextureStageState, reinterpret_cast<void*>(&h_SetTextureStageState), reinterpret_cast<void**>(&o_SetTextureStageState)},
      {kGetTextureStageState, reinterpret_cast<void*>(&h_GetTextureStageState), reinterpret_cast<void**>(&o_GetTextureStageState)},
      {kSetSamplerState, reinterpret_cast<void*>(&h_SetSamplerState), reinterpret_cast<void**>(&o_SetSamplerState)},
      {kGetSamplerState, reinterpret_cast<void*>(&h_GetSamplerState), reinterpret_cast<void**>(&o_GetSamplerState)},
      {kSetScissorRect, reinterpret_cast<void*>(&h_SetScissorRect), reinterpret_cast<void**>(&o_SetScissorRect)},
      {kGetScissorRect, reinterpret_cast<void*>(&h_GetScissorRect), reinterpret_cast<void**>(&o_GetScissorRect)},
      {kSetVertexDeclaration, reinterpret_cast<void*>(&h_SetVertexDeclaration), reinterpret_cast<void**>(&o_SetVertexDeclaration)},
      {kSetFVF, reinterpret_cast<void*>(&h_SetFVF), reinterpret_cast<void**>(&o_SetFVF)},
      {kGetFVF, reinterpret_cast<void*>(&h_GetFVF), reinterpret_cast<void**>(&o_GetFVF)},
      {kSetVertexShader, reinterpret_cast<void*>(&h_SetVertexShader), reinterpret_cast<void**>(&o_SetVertexShader)},
      {kSetVertexShaderConstantF, reinterpret_cast<void*>(&h_SetVertexShaderConstantF), reinterpret_cast<void**>(&o_SetVertexShaderConstantF)},
      {kSetStreamSource, reinterpret_cast<void*>(&h_SetStreamSource), reinterpret_cast<void**>(&o_SetStreamSource)},
      {kSetIndices, reinterpret_cast<void*>(&h_SetIndices), reinterpret_cast<void**>(&o_SetIndices)},
      {kSetPixelShader, reinterpret_cast<void*>(&h_SetPixelShader), reinterpret_cast<void**>(&o_SetPixelShader)},
      {kSetPixelShaderConstantF, reinterpret_cast<void*>(&h_SetPixelShaderConstantF), reinterpret_cast<void**>(&o_SetPixelShaderConstantF)},
  };
  bool is_typed[kDeviceSlotsEx] = {false};
  for (const Typed& t : typed) {
    if (!HookTyped(vtable, t.slot, t.hook, t.original)) {
      ModLog("render thread: the hook of slot %d of the device FAILED", t.slot);
      return false;
    }
    is_typed[t.slot] = true;
  }
  g_thunks = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, kMaxThunks * kThunkBytes, MEM_COMMIT | MEM_RESERVE,
      PAGE_EXECUTE_READWRITE));
  if (g_thunks == nullptr) return false;
  IDirect3DDevice9Ex* ex = nullptr;
  const bool is_ex = SUCCEEDED(device->QueryInterface(
      __uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&ex)));
  if (ex != nullptr) ex->Release();
  const int slots = is_ex ? kDeviceSlotsEx : kDeviceSlots;
  int stops = 0, shared = 0;
  for (int slot = 0; slot < slots; ++slot) {
    if (HasOwnHook(slot) || is_typed[slot]) continue;
    bool already = false;
    if (!HookStop(vtable[slot], slot, &already)) {
      ModLog("render thread: the stop of slot %d of the device FAILED", slot);
      return false;
    }
    if (already) {
      ++shared;
    } else {
      ++stops;
    }
  }

  // The locks of the resources. A resource of each kind shows the code of
  // its calls.
  int locks = 0;
  auto lock_hooks = [&](IUnknown* object, const int* slot_list, const int* which,
                        int count) {
    if (object == nullptr) return true;
    void** table = *reinterpret_cast<void***>(object);
    for (int i = 0; i < count; ++i) {
      bool already = false;
      if (!HookStop(table[slot_list[i]], which[i], &already)) return false;
      if (!already) ++locks;
    }
    return true;
  };
  bool ok = true;
  IDirect3DTexture9* texture = nullptr;
  if (SUCCEEDED(device->CreateTexture(4, 4, 1, D3DUSAGE_DYNAMIC,
                                      D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                      &texture, nullptr))) {
    const int s[2] = {kTextureLockRect, kTextureUnlockRect};
    const int w[2] = {kStopLockRect, kStopUnlockRect};
    ok &= lock_hooks(texture, s, w, 2);
    IDirect3DSurface9* level = nullptr;
    if (SUCCEEDED(texture->GetSurfaceLevel(0, &level))) {
      const int s2[3] = {kSurfaceLockRect, kSurfaceUnlockRect, kSurfaceGetDC};
      const int w2[3] = {kStopLockRect, kStopUnlockRect, kStopGetDC};
      ok &= lock_hooks(level, s2, w2, 3);
      level->Release();
    }
    texture->Release();
  }
  IDirect3DTexture9* system = nullptr;
  if (SUCCEEDED(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8,
                                      D3DPOOL_SYSTEMMEM, &system, nullptr))) {
    const int s[2] = {kTextureLockRect, kTextureUnlockRect};
    const int w[2] = {kStopLockRect, kStopUnlockRect};
    ok &= lock_hooks(system, s, w, 2);
    system->Release();
  }
  IDirect3DSurface9* plain = nullptr;
  if (SUCCEEDED(device->CreateOffscreenPlainSurface(
          4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &plain, nullptr))) {
    const int s[3] = {kSurfaceLockRect, kSurfaceUnlockRect, kSurfaceGetDC};
    const int w[3] = {kStopLockRect, kStopUnlockRect, kStopGetDC};
    ok &= lock_hooks(plain, s, w, 3);
    plain->Release();
  }
  IDirect3DSurface9* target = nullptr;
  if (SUCCEEDED(device->CreateRenderTarget(4, 4, D3DFMT_A8R8G8B8,
                                           D3DMULTISAMPLE_NONE, 0, TRUE,
                                           &target, nullptr))) {
    const int s[3] = {kSurfaceLockRect, kSurfaceUnlockRect, kSurfaceGetDC};
    const int w[3] = {kStopLockRect, kStopUnlockRect, kStopGetDC};
    ok &= lock_hooks(target, s, w, 3);
    target->Release();
  }
  IDirect3DVertexBuffer9* vb = nullptr;
  if (SUCCEEDED(device->CreateVertexBuffer(64, D3DUSAGE_DYNAMIC, 0,
                                           D3DPOOL_DEFAULT, &vb, nullptr))) {
    const int s[1] = {kBufferLock};
    const int w[1] = {kStopBufferLock};
    ok &= lock_hooks(vb, s, w, 1);
    vb->Release();
  }
  IDirect3DIndexBuffer9* ib = nullptr;
  if (SUCCEEDED(device->CreateIndexBuffer(64, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16,
                                          D3DPOOL_DEFAULT, &ib, nullptr))) {
    const int s[1] = {kBufferLock};
    const int w[1] = {kStopBufferLock};
    ok &= lock_hooks(ib, s, w, 1);
    ib->Release();
  }
  IDirect3DQuery9* query = nullptr;
  if (SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_EVENT, &query))) {
    const int s[1] = {kQueryIssue};
    const int w[1] = {kStopQueryIssue};
    ok &= lock_hooks(query, s, w, 1);
    query->Release();
  }
  if (!ok) {
    ModLog("render thread: a stop on a lock of a resource FAILED");
    return false;
  }
  ModLog("render thread: hooks in: %d calls of the device that the queue "
         "takes or reads, %d stops on the other calls (%d of them share their "
         "code with another call), %d stops on the locks of resources and the "
         "issue of a query", static_cast<int>(sizeof(typed) / sizeof(typed[0])),
         stops + shared, shared, locks);
  return true;
}

// ---------------------------------------------------------------------------
// GETTYPE. At each SetTexture dxwrapper asks the texture for its type
// (dxwrapper.dll+0x21367). With the flag for two threads, GetType of
// Direct3D 9 takes the lock of the device, and the render thread holds that
// lock in each draw. So the frame thread waits there for most of a draw.
// The type of a texture never changes, and each class of resource of
// Direct3D 9 has a vtable of its own. Some classes share one GetType, which
// reads the type from the object. So the hook knows the vtable of each class
// of texture from a texture that the mod makes, and gives the type of that
// class with no lock (render_thread_lock_free=1). An object with another
// vtable goes to Direct3D 9. One difference: the GetType of Direct3D 9 gives
// 0 when the device is gone, and the hook gives the type.

const int kResourceGetType = 10;  // the slot of IDirect3DResource9::GetType
const int kMaxTypeClasses = 8;
const int kMaxTypeHooks = 3;

typedef D3DRESOURCETYPE(STDMETHODCALLTYPE* GetTypeFn)(IDirect3DResource9*);

// The vtables of the classes of textures, and their types. Written before
// the hooks go in, and not changed after that.
const void* g_type_vtable[kMaxTypeClasses] = {nullptr};
D3DRESOURCETYPE g_type_of[kMaxTypeClasses];
int g_type_classes = 0;
GetTypeFn o_GetType[kMaxTypeHooks] = {nullptr};

template <int N>
D3DRESOURCETYPE STDMETHODCALLTYPE h_GetType(IDirect3DResource9* self) {
  const void* vtable = *reinterpret_cast<void* const*>(self);
  for (int i = 0; g_lock_free && i < g_type_classes; ++i) {
    if (g_type_vtable[i] == vtable) return g_type_of[i];
  }
  return o_GetType[N](self);
}

void* const kGetTypeHooks[kMaxTypeHooks] = {
    reinterpret_cast<void*>(&h_GetType<0>),
    reinterpret_cast<void*>(&h_GetType<1>),
    reinterpret_cast<void*>(&h_GetType<2>)};

// QUERYINTERFACE. dxwrapper asks a texture for its IUnknown at each
// SetTexture (dxwrapper.dll+0x29203), to find its own object for it. The
// QueryInterface of Direct3D 9 also takes the lock of the device. For a
// class of texture that the hook knows, the IUnknown is the object itself:
// InstallGetType checks that on each class. So the hook gives the object
// with one more reference, and takes no lock. Each other question goes to
// Direct3D 9.

typedef HRESULT(STDMETHODCALLTYPE* QueryInterfaceFn)(IUnknown*, REFIID,
                                                     void**);
typedef ULONG(STDMETHODCALLTYPE* AddRefFn)(IUnknown*);
QueryInterfaceFn o_QueryInterface[kMaxTypeHooks] = {nullptr};
AddRefFn o_Release[kMaxTypeHooks] = {nullptr};
bool g_query_known = false;  // each class gives itself as its IUnknown

// THE PAIRS. AddRef and Release of Direct3D 9 also take the lock. In the
// function of dxwrapper that finds its own object for a texture
// (dxwrapper.dll+0x291B0) there are two pairs of calls with nothing between
// them, on one object:
//
//   +0x29203 QueryInterface for the IUnknown, then +0x2921A its Release
//   +0x292AF AddRef of the object that dxwrapper keeps, then +0x292B5 the
//            Release of the object that it got. Both are the same object,
//            because dxwrapper finds its own object by that one.
//
// Around them, in the loop of dxwrapper.dll+0x23960 over the stages, is a
// third pair:
//
//   +0x2396B GetTexture of a stage, then +0x23A8E the Release of the
//            texture. Each way from the one call goes to the other. The
//            texture lives between them, because the object of dxwrapper
//            for it has a reference.
//
// At the first call of a pair the hook takes no reference, and the hook of
// Release lets the Release of the pair go, on the same thread and object.
// The count of references is the same as with Direct3D 9. When the Release
// of the second pair is of another object, the hook makes the AddRef first,
// then the Release. InstallPairs checks the bytes of dxwrapper at the pairs
// first.
struct PairCode {
  uintptr_t at;           // the first byte of `code`
  uint8_t code[28];
  int bytes;
  uintptr_t first_return;    // the return of QueryInterface or AddRef
  uintptr_t release_return;  // the return of the Release
};
const PairCode kPairs[2] = {
    {0x29201,
     {0x8B, 0x07, 0xFF, 0x10, 0x8B, 0x5D, 0xF0, 0x85, 0xC0, 0x78,
      0x13, 0x85, 0xDB, 0x74, 0x0F, 0xC7, 0x45, 0xFC, 0x01, 0x00,
      0x00, 0x00, 0x8B, 0x03, 0x53, 0xFF, 0x50, 0x08},
     28, 0x29205, 0x2921D},
    {0x292A9,
     {0x8B, 0x4E, 0x08, 0x51, 0x8B, 0x11, 0xFF, 0x52, 0x04, 0x8B, 0x0F,
      0x57, 0xFF, 0x51, 0x08},
     15, 0x292B2, 0x292B8},
};
// The code of the third pair, and the return of each of its calls.
const PairCode kOuterGet = {
    0x23963,
    {0x8B, 0x4B, 0x04, 0x56, 0x52, 0x51, 0x8B, 0x01, 0xFF, 0x90, 0x00, 0x01,
     0x00, 0x00},
    14, 0x23971, 0};
const PairCode kOuterRelease = {
    0x23A7D,
    {0x8B, 0x4D, 0xE8, 0xC7, 0x45, 0xFC, 0x0C, 0x00, 0x00, 0x00, 0x85, 0xC9,
     0x74, 0x06, 0x8B, 0x01, 0x51, 0xFF, 0x50, 0x08},
    20, 0x23A91, 0};
void* g_pair_first_return[2] = {nullptr, nullptr};
void* g_pair_release_return[2] = {nullptr, nullptr};
AddRefFn o_AddRef[kMaxTypeHooks] = {nullptr};
// The object of a pair that has its first call and not its Release yet, the
// thread, and the AddRef that the object did not get.
IUnknown* volatile g_pair_object = nullptr;
volatile DWORD g_pair_thread = 0;
AddRefFn g_pair_add_ref = nullptr;

bool IsKnownTexture(IUnknown* self) {
  const void* vtable = *reinterpret_cast<void* const*>(self);
  for (int i = 0; i < g_type_classes; ++i) {
    if (g_type_vtable[i] == vtable) return true;
  }
  return false;
}

void StartPair(IUnknown* self, AddRefFn add_ref) {
  g_pair_object = self;
  g_pair_thread = GetCurrentThreadId();
  g_pair_add_ref = add_ref;
}

template <int N>
HRESULT STDMETHODCALLTYPE h_QueryInterface(IUnknown* self, REFIID riid,
                                           void** out) {
  if (g_lock_free && g_query_known && out != nullptr &&
      riid == IID_IUnknown &&
      IsKnownTexture(self)) {
    AddRefFn add_ref =
        reinterpret_cast<AddRefFn>((*reinterpret_cast<void***>(self))[1]);
    if (g_pair_first_return[0] != nullptr &&
        _ReturnAddress() == g_pair_first_return[0]) {
      StartPair(self, add_ref);
    } else {
      add_ref(self);
    }
    *out = self;
    return S_OK;
  }
  return o_QueryInterface[N](self, riid, out);
}

template <int N>
ULONG STDMETHODCALLTYPE h_AddRef(IUnknown* self) {
  if (g_lock_free && g_pair_first_return[1] != nullptr &&
      _ReturnAddress() == g_pair_first_return[1] && IsKnownTexture(self)) {
    StartPair(self, o_AddRef[N]);
    return 2;  // dxwrapper does not read the count
  }
  return o_AddRef[N](self);
}

template <int N>
ULONG STDMETHODCALLTYPE h_Release(IUnknown* self) {
  void* from = _ReturnAddress();
  if (from == g_outer_release_return && g_outer_object != nullptr &&
      g_outer_thread == GetCurrentThreadId()) {
    IUnknown* object = g_outer_object;
    g_outer_object = nullptr;
    if (object == self) return 1;  // dxwrapper does not read the count
    object->AddRef();
  }
  if (g_pair_object != nullptr && g_pair_thread == GetCurrentThreadId() &&
      (from == g_pair_release_return[0] || from == g_pair_release_return[1])) {
    IUnknown* object = g_pair_object;
    g_pair_object = nullptr;
    if (object == self) return 1;  // dxwrapper does not read the count
    g_pair_add_ref(object);
  }
  return o_Release[N](self);
}

void* const kQueryInterfaceHooks[kMaxTypeHooks] = {
    reinterpret_cast<void*>(&h_QueryInterface<0>),
    reinterpret_cast<void*>(&h_QueryInterface<1>),
    reinterpret_cast<void*>(&h_QueryInterface<2>)};
void* const kAddRefHooks[kMaxTypeHooks] = {
    reinterpret_cast<void*>(&h_AddRef<0>),
    reinterpret_cast<void*>(&h_AddRef<1>),
    reinterpret_cast<void*>(&h_AddRef<2>)};
void* const kReleaseHooks[kMaxTypeHooks] = {
    reinterpret_cast<void*>(&h_Release<0>),
    reinterpret_cast<void*>(&h_Release<1>),
    reinterpret_cast<void*>(&h_Release<2>)};

// Hooks the function in `slot` of each class of texture, one hook for each
// function. False when a hook failed.
bool HookEachClass(IDirect3DResource9* const* textures, int count, int slot,
                   void* const* hooks, AddRefFn* originals) {
  int made = 0;
  for (int i = 0; i < count; ++i) {
    void* code = (*reinterpret_cast<void* const* const*>(textures[i]))[slot];
    bool first = true;
    for (int j = 0; j < i; ++j) {
      if ((*reinterpret_cast<void* const* const*>(textures[j]))[slot] == code) {
        first = false;
      }
    }
    if (!first) continue;
    if (made >= kMaxTypeHooks ||
        MH_CreateHook(code, hooks[made],
                      reinterpret_cast<void**>(&originals[made])) != MH_OK ||
        MH_QueueEnableHook(code) != MH_OK) {
      return false;
    }
    ++made;
  }
  return true;
}

// Hooks AddRef and Release of each class of texture when the code of
// dxwrapper at the pairs is the code that the pairs need. Gives the count of
// pairs that are in: 0, or 2.
int InstallPairs(IDirect3DResource9* const* textures, int count) {
  HMODULE dx = GetModuleHandleA("dxwrapper.dll");
  if (dx == nullptr) return 0;
  uint8_t* base = reinterpret_cast<uint8_t*>(dx);
  for (const PairCode& p : kPairs) {
    if (memcmp(base + p.at, p.code, p.bytes) != 0) return 0;
  }
  if (memcmp(base + kOuterGet.at, kOuterGet.code, kOuterGet.bytes) != 0 ||
      memcmp(base + kOuterRelease.at, kOuterRelease.code,
             kOuterRelease.bytes) != 0) {
    return 0;
  }
  if (!HookEachClass(textures, count, 2, kReleaseHooks, o_Release) ||
      !HookEachClass(textures, count, 1, kAddRefHooks, o_AddRef)) {
    return 0;
  }
  // The places of the Release go in first, so a pair starts only when the
  // hooks can end it.
  for (int k = 0; k < 2; ++k) {
    g_pair_release_return[k] = base + kPairs[k].release_return;
  }
  g_outer_release_return = base + kOuterRelease.first_return;
  for (int k = 0; k < 2; ++k) {
    g_pair_first_return[k] = base + kPairs[k].first_return;
  }
  g_outer_get_return = base + kOuterGet.first_return;
  return 3;
}

// A resource of each kind gives its vtable, its GetType and its type. Each
// GetType of a texture gets a hook.
void InstallGetType(IDirect3DDevice9* device) {
  const int kMaxProbes = 10;
  IDirect3DResource9* probes[kMaxProbes] = {nullptr};
  int count = 0;
  auto add = [&](IDirect3DResource9* r) {
    if (r != nullptr && count < kMaxProbes) probes[count++] = r;
  };
  IDirect3DTexture9* t = nullptr;
  if (SUCCEEDED(device->CreateTexture(4, 4, 1, D3DUSAGE_DYNAMIC,
                                      D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t,
                                      nullptr))) add(t);
  t = nullptr;
  if (SUCCEEDED(device->CreateTexture(4, 4, 1, D3DUSAGE_RENDERTARGET,
                                      D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t,
                                      nullptr))) add(t);
  t = nullptr;
  if (SUCCEEDED(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8,
                                      D3DPOOL_SYSTEMMEM, &t, nullptr))) add(t);
  IDirect3DCubeTexture9* c = nullptr;
  if (SUCCEEDED(device->CreateCubeTexture(4, 1, 0, D3DFMT_A8R8G8B8,
                                          D3DPOOL_DEFAULT, &c, nullptr))) add(c);
  IDirect3DVolumeTexture9* v = nullptr;
  if (SUCCEEDED(device->CreateVolumeTexture(4, 4, 4, 1, 0, D3DFMT_A8R8G8B8,
                                            D3DPOOL_SYSTEMMEM, &v, nullptr))) add(v);
  IDirect3DSurface9* s = nullptr;
  if (SUCCEEDED(device->CreateOffscreenPlainSurface(
          4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &s, nullptr))) add(s);
  IDirect3DVertexBuffer9* vb = nullptr;
  if (SUCCEEDED(device->CreateVertexBuffer(64, D3DUSAGE_DYNAMIC, 0,
                                           D3DPOOL_DEFAULT, &vb, nullptr))) add(vb);
  IDirect3DIndexBuffer9* ib = nullptr;
  if (SUCCEEDED(device->CreateIndexBuffer(64, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16,
                                          D3DPOOL_DEFAULT, &ib, nullptr))) add(ib);

  void* code[kMaxProbes];
  const void* vtable[kMaxProbes];
  D3DRESOURCETYPE type[kMaxProbes];
  for (int i = 0; i < count; ++i) {
    vtable[i] = *reinterpret_cast<void* const*>(probes[i]);
    code[i] = (*reinterpret_cast<void***>(probes[i]))[kResourceGetType];
    type[i] = probes[i]->GetType();
  }
  auto is_texture = [](D3DRESOURCETYPE t) {
    return t == D3DRTYPE_TEXTURE || t == D3DRTYPE_CUBETEXTURE ||
           t == D3DRTYPE_VOLUMETEXTURE;
  };
  // A vtable with two types is not a class of one type: then no hook.
  bool ok = true;
  for (int i = 0; i < count; ++i) {
    for (int j = 0; j < count; ++j) {
      if (vtable[i] == vtable[j] && type[i] != type[j]) ok = false;
    }
  }
  int hooked = 0;
  for (int i = 0; ok && i < count; ++i) {
    if (!is_texture(type[i])) continue;
    bool known = false;
    for (int k = 0; k < g_type_classes; ++k) {
      if (g_type_vtable[k] == vtable[i]) known = true;
    }
    if (!known && g_type_classes < kMaxTypeClasses) {
      g_type_vtable[g_type_classes] = vtable[i];
      g_type_of[g_type_classes] = type[i];
      ++g_type_classes;
    }
  }
  for (int i = 0; ok && i < count; ++i) {
    if (!is_texture(type[i])) continue;
    bool first = true;
    for (int j = 0; j < i; ++j) {
      if (code[j] == code[i]) first = false;
    }
    if (!first) continue;
    if (hooked >= kMaxTypeHooks ||
        MH_CreateHook(code[i], kGetTypeHooks[hooked],
                      reinterpret_cast<void**>(&o_GetType[hooked])) != MH_OK ||
        MH_QueueEnableHook(code[i]) != MH_OK) {
      ok = false;
      break;
    }
    ++hooked;
  }
  // The IUnknown of each texture must be the texture itself.
  bool identity = ok;
  for (int i = 0; identity && i < count; ++i) {
    if (!is_texture(type[i])) continue;
    IUnknown* unknown = nullptr;
    if (FAILED(probes[i]->QueryInterface(IID_IUnknown,
                                         reinterpret_cast<void**>(&unknown))) ||
        unknown != static_cast<IUnknown*>(probes[i])) {
      identity = false;
    }
    if (unknown != nullptr) unknown->Release();
  }
  int query_hooks = 0;
  for (int i = 0; identity && i < count; ++i) {
    if (!is_texture(type[i])) continue;
    void* query = (*reinterpret_cast<void***>(probes[i]))[0];
    bool first = true;
    for (int j = 0; j < i; ++j) {
      if (is_texture(type[j]) &&
          (*reinterpret_cast<void***>(probes[j]))[0] == query) {
        first = false;
      }
    }
    if (!first) continue;
    if (query_hooks >= kMaxTypeHooks ||
        MH_CreateHook(query, kQueryInterfaceHooks[query_hooks],
                      reinterpret_cast<void**>(
                          &o_QueryInterface[query_hooks])) != MH_OK ||
        MH_QueueEnableHook(query) != MH_OK) {
      identity = false;
      break;
    }
    ++query_hooks;
  }
  g_query_known = identity;
  int pair_hooks = -1;
  if (identity) {
    IDirect3DResource9* textures[kMaxProbes];
    int textures_count = 0;
    for (int i = 0; i < count; ++i) {
      if (is_texture(type[i])) textures[textures_count++] = probes[i];
    }
    pair_hooks = InstallPairs(textures, textures_count);
  }
  char line[512];
  int used = 0;
  line[0] = 0;
  for (int i = 0; i < count; ++i) {
    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCSTR>(code[i]), &module);
    used += sprintf_s(line + used, sizeof(line) - used, "%stype %d at +0x%X",
                      i == 0 ? "" : ", ", static_cast<int>(type[i]),
                      static_cast<unsigned>(static_cast<uint8_t*>(code[i]) -
                                            reinterpret_cast<uint8_t*>(module)));
    if (used >= static_cast<int>(sizeof(line)) - 40) break;
  }
  ModLog("render thread: the GetType of each resource that the mod made, in "
         "its module: %s", line);
  if (count > 0) {
    void** first_table = *reinterpret_cast<void***>(probes[0]);
    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCSTR>(first_table[0]), &module);
    const uint8_t* base = reinterpret_cast<const uint8_t*>(module);
    ModLog("render thread: the first texture: QueryInterface at +0x%X, "
           "AddRef at +0x%X, Release at +0x%X",
           static_cast<unsigned>(static_cast<uint8_t*>(first_table[0]) - base),
           static_cast<unsigned>(static_cast<uint8_t*>(first_table[1]) - base),
           static_cast<unsigned>(static_cast<uint8_t*>(first_table[2]) - base));
  }
  if (ok) {
    ModLog("render thread: render_thread_lock_free=%d. Hooks in: %d GetType "
           "functions for %d classes of textures and %d QueryInterface "
           "functions. While the value is 1 they answer with no lock of the "
           "device%s", g_lock_free ? 1 : 0, hooked, g_type_classes, query_hooks,
           identity ? "" : ", but a class is not its own IUnknown, or a hook "
                           "FAILED: each QueryInterface goes to Direct3D 9");
    if (pair_hooks > 0) {
      ModLog("render thread: hooks in for %d pairs of calls of "
             "dxwrapper.dll+0x291B0. While render_thread_lock_free is 1 they "
             "take no reference and no lock", pair_hooks);
    } else if (identity) {
      ModLog("render thread: the code of dxwrapper is not the code of the "
             "pairs, or a hook of AddRef or Release FAILED. The pairs go to "
             "Direct3D 9");
    }
  } else {
    ModLog("render thread: render_thread_lock_free, but a hook of GetType "
           "FAILED or a class has two types. %d hooks are in", hooked);
  }
  for (int i = 0; i < count; ++i) probes[i]->Release();
}

// ---------------------------------------------------------------------------
// THE GPU TIME (profile_gpu=1, a test key). A timestamp query at the start
// and at the end of each scene walk gives the time that the GPU took for
// the walk, from its first call to its last. With the render thread the
// queries go in the queue, so they have their place in the order of the
// calls. The mod reads a pair four walks later, with no flush and no wait.
// A pair that is not ready then is lost.

const int kStampSets = 4;
struct StampSet {
  IDirect3DQuery9* begin;
  IDirect3DQuery9* end;
  IDirect3DQuery9* frequency;
  bool issued;
};
int g_profile_gpu = 0;
bool g_stamps_failed = false;
StampSet g_stamps[kStampSets] = {};
int g_stamp_at = 0;
IDirect3DDevice9* g_stamp_device = nullptr;
double g_gpu_ms = 0.0;
uint64_t g_gpu_walks = 0;
uint64_t g_gpu_lost = 0;

void IssueStamp(IDirect3DQuery9* query) {
  if (g_recording) {
    const ArgObject a = {query};
    if (Simple(kOpIssueQuery, a)) return;
    Stop(kStopQueryIssue);
  }
  query->Issue(D3DISSUE_END);
}

// Reads the set of four walks before, and gives its queries to this walk.
// The queue is empty here.
void StampWalkBegin(IDirect3DDevice9* device) {
  if (g_profile_gpu == 0 || g_stamps_failed || device == nullptr) return;
  if (device != g_stamp_device) {
    for (StampSet& s : g_stamps) {
      if (s.begin != nullptr) s.begin->Release();
      if (s.end != nullptr) s.end->Release();
      if (s.frequency != nullptr) s.frequency->Release();
      s = StampSet();
    }
    g_stamp_device = device;
    for (StampSet& s : g_stamps) {
      if (FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &s.begin)) ||
          FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &s.end)) ||
          FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,
                                     &s.frequency))) {
        g_stamps_failed = true;
        ModLog("render thread: profile_gpu=1, but the device has no "
               "timestamp queries");
        return;
      }
    }
  }
  StampSet& s = g_stamps[g_stamp_at];
  if (s.issued) {
    UINT64 a = 0, b = 0, f = 0;
    if (s.begin->GetData(&a, sizeof(a), 0) == S_OK &&
        s.end->GetData(&b, sizeof(b), 0) == S_OK &&
        s.frequency->GetData(&f, sizeof(f), 0) == S_OK && f != 0 && b >= a) {
      g_gpu_ms += 1000.0 * static_cast<double>(b - a) / static_cast<double>(f);
      ++g_gpu_walks;
    } else {
      ++g_gpu_lost;
    }
  }
  s.issued = true;
  IssueStamp(s.frequency);
  IssueStamp(s.begin);
}

void StampWalkEnd() {
  if (g_profile_gpu == 0 || g_stamps_failed || g_stamp_device == nullptr) {
    return;
  }
  IssueStamp(g_stamps[g_stamp_at].end);
  g_stamp_at = (g_stamp_at + 1) % kStampSets;
}

// THE WAIT FOR THE GPU AFTER THE WALK (profile_gpu=1). In a headset the
// second thread of frame_send=2 waits for the GPU after the walk, and that
// wait is a part of the frame. A desktop run has no such thread. So after
// each walk, when each call of the walk went to Direct3D 9, the frame thread
// issues an event query with D3DGETDATA_FLUSH, and a thread of its own looks
// at it about each 50 microseconds until the GPU is done. The time from the
// issue to the end is the wait that a headset would have, less the copy of
// the eye pictures.
IDirect3DQuery9* g_done_query = nullptr;
IDirect3DDevice9* g_done_device = nullptr;
HANDLE g_done_event = nullptr;
volatile LONG g_done_pending = 0;
LARGE_INTEGER g_done_start = {};
double g_done_ms = 0.0;
volatile LONG g_done_count = 0;
volatile LONG g_done_skipped = 0;

DWORD WINAPI DoneThread(LPVOID) {
  for (;;) {
    WaitForSingleObject(g_done_event, INFINITE);
    LARGE_INTEGER now;
    for (;;) {
      if (g_done_query->GetData(nullptr, 0, 0) != S_FALSE) break;
      LARGE_INTEGER until;
      QueryPerformanceCounter(&until);
      const LONGLONG step = g_frequency.QuadPart / 20000;  // 50 microseconds
      do {
        YieldProcessor();
        QueryPerformanceCounter(&now);
      } while (now.QuadPart - until.QuadPart < step);
    }
    QueryPerformanceCounter(&now);
    g_done_ms += 1000.0 * static_cast<double>(now.QuadPart - g_done_start.QuadPart) /
                 static_cast<double>(g_frequency.QuadPart);
    InterlockedIncrement(&g_done_count);
    InterlockedExchange(&g_done_pending, 0);
  }
}

void MeasureDoneAfterWalk(IDirect3DDevice9* device) {
  if (g_profile_gpu == 0 || device == nullptr) return;
  if (g_done_pending != 0) {
    InterlockedIncrement(&g_done_skipped);
    return;
  }
  if (g_done_device != device) {
    if (g_done_query != nullptr) g_done_query->Release();
    g_done_query = nullptr;
    g_done_device = device;
    if (FAILED(device->CreateQuery(D3DQUERYTYPE_EVENT, &g_done_query))) {
      g_done_query = nullptr;
    }
  }
  if (g_done_query == nullptr) return;
  if (g_done_event == nullptr) {
    if (g_frequency.QuadPart == 0) QueryPerformanceFrequency(&g_frequency);
    g_done_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HANDLE thread =
        g_done_event != nullptr
            ? CreateThread(nullptr, 0, DoneThread, nullptr, 0, nullptr)
            : nullptr;
    if (thread == nullptr) {
      g_done_query->Release();
      g_done_query = nullptr;
      return;
    }
    CloseHandle(thread);
  }
  g_done_query->Issue(D3DISSUE_END);
  QueryPerformanceCounter(&g_done_start);
  g_done_query->GetData(nullptr, 0, D3DGETDATA_FLUSH);
  InterlockedExchange(&g_done_pending, 1);
  SetEvent(g_done_event);
}

bool StartWorker() {
  if (g_worker != nullptr) return true;
  g_queue = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, kQueueBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  g_work_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  g_idle_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  if (g_queue == nullptr || g_work_event == nullptr || g_idle_event == nullptr) {
    return false;
  }
  g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, &g_worker_id);
  if (g_worker == nullptr) {
    g_worker_id = 0;
    return false;
  }
  SetThreadPriority(g_worker, THREAD_PRIORITY_ABOVE_NORMAL);
  return true;
}

const char* ModeName(int mode) {
  return mode == 1   ? "the calls of each scene walk go to the render thread"
         : mode == 2 ? "the calls of each scene walk are kept, and go to "
                       "Direct3D 9 on the frame thread at each stop (a test)"
                     : "each call goes to Direct3D 9 as it comes";
}

}  // namespace

// ---------------------------------------------------------------------------
// The out functions of one walk.

HRESULT RenderThreadOutSetRenderTarget(IDirect3DDevice9* self, DWORD index,
                                       IDirect3DSurface9* surface) {
  if (Taking(self)) {
    const ArgTarget a = {index, surface};
    Hold(surface);
    if (Simple(kOpSetRenderTarget, a)) {
      KeepTarget(index, surface);
      return D3D_OK;
    }
    Drop(surface);
  }
  return o_SetRenderTarget(self, index, surface);
}

HRESULT RenderThreadOutSetDepthStencil(IDirect3DDevice9* self,
                                       IDirect3DSurface9* surface) {
  if (Taking(self)) {
    const ArgSurface a = {surface};
    Hold(surface);
    if (Simple(kOpSetDepthStencil, a)) {
      g_kept.depth = surface;
      g_kept.depth_ok = true;
      return D3D_OK;
    }
    Drop(surface);
  }
  return o_SetDepthStencil(self, surface);
}

HRESULT RenderThreadOutClear(IDirect3DDevice9* self, DWORD count,
                             const D3DRECT* rects, DWORD flags, D3DCOLOR color,
                             float z, DWORD stencil) {
  if (Taking(self)) {
    const DWORD n = rects != nullptr ? count : 0;
    uint8_t* p = Reserve(kOpClear, sizeof(ArgClear) + n * sizeof(D3DRECT));
    if (p != nullptr) {
      const ArgClear a = {n, flags, color, z, stencil};
      memcpy(p, &a, sizeof(a));
      if (n > 0) memcpy(p + sizeof(a), rects, n * sizeof(D3DRECT));
      Commit();
      return D3D_OK;
    }
  }
  return o_Clear(self, count, rects, flags, color, z, stencil);
}

HRESULT RenderThreadOutDrawPrimitive(IDirect3DDevice9* self,
                                     D3DPRIMITIVETYPE type, UINT start,
                                     UINT count) {
  if (Taking(self)) {
    const ArgDraw a = {type, 0, start, count, 0, 0};
    if (Simple(kOpDrawPrimitive, a)) return D3D_OK;
  }
  const HRESULT hr = o_DrawPrimitive(self, type, start, count);
  CountDrawForFlush();
  return hr;
}

HRESULT RenderThreadOutDrawIndexedPrimitive(IDirect3DDevice9* self,
                                            D3DPRIMITIVETYPE type, INT base,
                                            UINT min_index, UINT vertices,
                                            UINT start, UINT count) {
  if (Taking(self)) {
    const ArgDraw a = {type, base, min_index, vertices, start, count};
    if (Simple(kOpDrawIndexedPrimitive, a)) return D3D_OK;
  }
  const HRESULT hr = o_DrawIndexedPrimitive(self, type, base, min_index,
                                            vertices, start, count);
  CountDrawForFlush();
  return hr;
}

HRESULT RenderThreadOutDrawPrimitiveUP(IDirect3DDevice9* self,
                                       D3DPRIMITIVETYPE type, UINT count,
                                       const void* vertices, UINT stride) {
  if (Taking(self) && vertices != nullptr) {
    const uint32_t bytes = IndexCount(type, count) * stride;
    uint8_t* p = Reserve(kOpDrawPrimitiveUP, sizeof(ArgDrawUP) + bytes);
    if (p != nullptr) {
      const ArgDrawUP a = {type, count, stride, bytes};
      memcpy(p, &a, sizeof(a));
      memcpy(p + sizeof(a), vertices, bytes);
      Commit();
      return D3D_OK;
    }
    Stop(kDrawPrimitiveUP);
  }
  const HRESULT hr = o_DrawPrimitiveUP(self, type, count, vertices, stride);
  CountDrawForFlush();
  return hr;
}

HRESULT RenderThreadOutDrawIndexedPrimitiveUP(
    IDirect3DDevice9* self, D3DPRIMITIVETYPE type, UINT min_index,
    UINT vertex_count, UINT primitive_count, const void* indices,
    D3DFORMAT index_format, const void* vertices, UINT stride) {
  if (Taking(self) && vertices != nullptr && indices != nullptr) {
    // The indices count from the start of the array of vertices.
    const uint32_t vertex_bytes = (min_index + vertex_count) * stride;
    const uint32_t index_bytes =
        IndexCount(type, primitive_count) *
        (index_format == D3DFMT_INDEX32 ? 4u : 2u);
    const uint32_t padded = (vertex_bytes + 7u) & ~7u;
    uint8_t* p = Reserve(kOpDrawIndexedPrimitiveUP,
                         sizeof(ArgDrawIndexedUP) + padded + index_bytes);
    if (p != nullptr) {
      const ArgDrawIndexedUP a = {type,         min_index,  vertex_count,
                                  primitive_count, index_format, stride,
                                  vertex_bytes, index_bytes};
      memcpy(p, &a, sizeof(a));
      memcpy(p + sizeof(a), vertices, vertex_bytes);
      memcpy(p + sizeof(a) + padded, indices, index_bytes);
      Commit();
      return D3D_OK;
    }
    Stop(kDrawIndexedPrimitiveUP);
  }
  const HRESULT hr = o_DrawIndexedPrimitiveUP(
      self, type, min_index, vertex_count, primitive_count, indices,
      index_format, vertices, stride);
  CountDrawForFlush();
  return hr;
}

// ---------------------------------------------------------------------------

// The hooks are queued in MinHook and go in at the end with one
// MH_ApplyQueued. Each MH_EnableHook stops each thread of the process for a
// moment, and over a hundred of them take seconds when the window of the
// game is not in front. MH_ApplyQueued stops no thread when nothing is
// queued, so the call in each walk costs little.
static bool InstallQueued(IDirect3DDevice9* device, bool all);

bool RenderThreadInstall(IDirect3DDevice9* device, bool all) {
  const bool ok = InstallQueued(device, all);
  MH_ApplyQueued();
  return ok;
}

static bool InstallQueued(IDirect3DDevice9* device, bool all) {
  if (device == nullptr) return false;
  if (g_device != nullptr && device != g_device) {
    // A new device of the same class has the same code, so the hooks stay.
    g_device = device;
  }
  g_device = device;
  if (!g_seven) {
    QueryPerformanceFrequency(&g_frequency);
    void** vtable = *reinterpret_cast<void***>(device);
    if (!InstallSeven(vtable)) {
      ModLog("render thread: the hooks of the seven calls of one walk FAILED");
      return false;
    }
    g_seven = true;
  }
  if (!all || g_all) return true;
  if (g_all_failed) return false;
  if (!InstallAll(device) || !StartWorker()) {
    g_all_failed = true;
    ModLog("render thread: OFF. Each call goes to Direct3D 9 as it comes");
    return false;
  }
  g_all = true;
  // The hooks go in also with render_thread_lock_free=0, so the test key
  // can turn the answers on. Then they give each call to Direct3D 9.
  InstallGetType(device);
  return true;
}

void RenderThreadSetOptions(int mode, int test_frame, int lock_free,
                            int filter, int buffers, int flush,
                            int profile_gpu) {
  g_ini_mode = mode == 1 || mode == 2 ? mode : 0;
  g_wanted = g_ini_mode;
  g_test_frame = test_frame > 0 ? test_frame : 0;
  g_lock_free = lock_free != 0;
  g_filter = filter != 0;
  g_buffers = buffers != 0;
  g_flush_every = g_flush_ini = flush > 0 ? flush : 0;
  g_profile_gpu = profile_gpu != 0 ? 1 : 0;
  ModLog("render thread: render_thread=%d. %s", g_ini_mode,
         ModeName(g_ini_mode));
}

void RenderThreadFilterToggle(const char* keys) {
  g_filter = !g_filter;
  ControllersGesturePulse(0.5f, g_filter ? 0.30f : 0.08f);
  ModLog("test key     : %s, render_thread_filter is %d: %s", keys,
         g_filter ? 1 : 0,
         g_filter ? "a call of a walk that sets the value that the device has "
                    "already does not go to Direct3D 9"
                  : "each call of a walk goes to Direct3D 9");
}

void RenderThreadFlushToggle(const char* keys) {
  const int on = g_flush_ini > 0 ? g_flush_ini : 128;
  g_flush_every = g_flush_every > 0 ? 0 : on;
  ControllersGesturePulse(0.5f, g_flush_every > 0 ? 0.30f : 0.08f);
  ModLog("test key     : %s, gpu_flush is %d: %s", keys,
         g_flush_every,
         g_flush_every > 0 ? "the driver gives the calls to the GPU each N draws"
                           : "the driver gives the calls to the GPU when it "
                             "wants");
}

void RenderThreadBuffersToggle(const char* keys) {
  g_buffers = !g_buffers;
  ControllersGesturePulse(0.5f, g_buffers ? 0.30f : 0.08f);
  ModLog("test key     : %s, render_thread_buffers is %d: %s", keys,
         g_buffers ? 1 : 0,
         g_buffers ? "the render thread draws from buffers of its own"
                   : "the render thread gives each DrawIndexedPrimitiveUP to "
                     "Direct3D 9");
}

void RenderThreadLockFreeToggle(const char* keys) {
  // A pair that started goes on to its Release, because the hook of Release
  // ends a pair whatever the value is.
  g_lock_free = !g_lock_free;
  ControllersGesturePulse(0.5f, g_lock_free ? 0.30f : 0.08f);
  ModLog("test key     : %s, render_thread_lock_free is %d: %s", keys,
         g_lock_free ? 1 : 0,
         g_lock_free ? "the mod gives the answers of some calls of a texture "
                       "with no lock of the device"
                     : "each call of a texture goes to Direct3D 9");
}

void RenderThreadToggle(const char* keys) {
  const int on = g_ini_mode != 0 ? g_ini_mode : 1;
  g_wanted = g_wanted == 0 ? on : 0;
  ControllersGesturePulse(0.5f, g_wanted != 0 ? 0.30f : 0.08f);
  ModLog("test key     : %s, render_thread is %d from the next scene walk: %s",
         keys, g_wanted, ModeName(g_wanted));
}

bool RenderThreadFrame(uint64_t frame) {
  if (g_test_frame <= 0) return false;
  const uint64_t first = static_cast<uint64_t>(g_test_frame);
  if (frame < first || frame >= first + 4) return false;
  const int on = g_ini_mode != 0 ? g_ini_mode : 1;
  g_wanted = frame < first + 2 ? on : 0;
  ModLog("render thread: test key render_thread_test_frame: frame %llu has "
         "render_thread=%d, and a picture of each eye", frame, g_wanted);
  return true;
}

void RenderThreadWalk(bool begin) {
  if (!begin) {
    StampWalkEnd();
    if (g_recording) {
      ViewportPutBack();
      const uint64_t start = Ticks();
      Drain();
      g_end_ticks += Ticks() - start;
      g_recording = false;
    }
    MeasureDoneAfterWalk(
        static_cast<IDirect3DDevice9*>(GpuBridgeDeviceObject()));
    g_since_flush = 0;
    return;
  }
  g_mode = 0;
  IDirect3DDevice9* device =
      static_cast<IDirect3DDevice9*>(GpuBridgeDeviceObject());
  if (g_wanted == 0 || device == nullptr) {
    StampWalkBegin(device);
    return;
  }
  if (!DrawMergeSurfaceHooked()) {
    if (!g_said_no_surface) {
      g_said_no_surface = true;
      ModLog("render thread: waits for the hooks of the surface calls of "
             "draw_merge.cpp (draw_merge=1), because a write of a texture "
             "must stop the queue");
    }
    StampWalkBegin(device);
    return;
  }
  if (!RenderThreadInstall(device, true)) {
    StampWalkBegin(device);
    return;
  }
  g_mode = g_wanted;
  g_frame_thread = GetCurrentThreadId();
  ReadAll(device);
  ++g_walks;
  // The queue is empty here: the end of each walk waits for it.
  InterlockedExchange(&g_worker_on, g_mode == 1 ? 1 : 0);
  g_recording = true;
  StampWalkBegin(device);
}

void RenderThreadSync() {
  if (!g_recording || GetCurrentThreadId() != g_frame_thread) return;
  Stop(kStopSurface);
}

// gpu_flush_ab=1 (a test key): each window of the report has the other
// value of gpu_flush, the value of the ini (or 128) and 0, so a session in a
// headset compares the two with no key.
static bool g_flush_ab = false;

void RenderThreadSetFlushAb(bool on) {
  g_flush_ab = on;
  if (on) {
    ModLog("gpu flush ab : gpu_flush_ab=1. Each window of 450 frames has the "
           "other value of gpu_flush: %d and 0",
           g_flush_ini > 0 ? g_flush_ini : 128);
  }
}

void RenderThreadReport(uint64_t frames) {
  if (g_flush_ab && frames > 0) {
    const int was = g_flush_every;
    g_flush_every = was > 0 ? 0 : (g_flush_ini > 0 ? g_flush_ini : 128);
    ModLog("gpu flush ab : the window that ends now had gpu_flush=%d, the next "
           "one has %d", was, g_flush_every);
  }
  if (g_profile_gpu != 0 && frames > 0 && (g_gpu_walks > 0 || g_gpu_lost > 0)) {
    ModLog("gpu time     : the scene walks took %.3f ms in a frame on the GPU, "
           "from the first call of a walk to its last (%.3f ms for one walk). "
           "%llu walks measured, %llu not ready four walks later",
           g_gpu_ms / static_cast<double>(frames),
           g_gpu_walks > 0 ? g_gpu_ms / static_cast<double>(g_gpu_walks) : 0.0,
           g_gpu_walks, g_gpu_lost);
    g_gpu_ms = 0.0;
    g_gpu_walks = g_gpu_lost = 0;
  }
  {
    const LONG done = InterlockedExchange(&g_done_count, 0);
    const LONG skipped = InterlockedExchange(&g_done_skipped, 0);
    const LONG flushes = InterlockedExchange(&g_flushes, 0);
    if (g_profile_gpu != 0 && frames > 0 && done > 0) {
      ModLog("gpu time     : the GPU was done %.3f ms after the end of a walk "
             "(%ld walks, %ld not measured). gpu_flush=%d, %.1f "
             "flushes in a frame",
             g_done_ms / done, done, skipped, g_flush_every,
             static_cast<double>(flushes) / static_cast<double>(frames));
    }
    g_done_ms = 0.0;
  }
  if (g_walks == 0 || frames == 0) return;
  {
    // The calls by kind, and the calls that set the value that the device
    // has already.
    static const char* const kNames[kOps] = {
        "wrap", "SetRenderTarget", "SetDepthStencil", "BeginScene",
        "EndScene", "Clear", "SetTransform", "SetViewport", "SetMaterial",
        "SetLight", "LightEnable", "SetClipPlane", "SetRenderState",
        "SetTexture", "SetTextureStageState", "SetSamplerState",
        "SetScissorRect", "DrawPrimitive", "DrawIndexedPrimitive",
        "DrawPrimitiveUP", "DrawIndexedPrimitiveUP", "SetVertexDeclaration",
        "SetFVF", "SetVertexShader", "SetVertexShaderConstantF",
        "SetStreamSource", "SetIndices", "SetPixelShader",
        "SetPixelShaderConstantF", "IssueQuery"};
    const double per = static_cast<double>(frames);
    char line[1024];
    int used = 0;
    uint64_t shown[kOps] = {0};
    memcpy(shown, g_records, sizeof(shown));
    for (int k = 0; k < 12; ++k) {
      int best = -1;
      for (int i = 1; i < kOps; ++i) {
        if (shown[i] > 0 && (best < 0 || shown[i] > shown[best])) best = i;
      }
      if (best < 0) break;
      used += sprintf_s(line + used, sizeof(line) - used, "%s%s %.0f",
                        k == 0 ? "" : ", ", kNames[best], shown[best] / per);
      shown[best] = 0;
    }
    ModLog("render thread: the calls of a frame in the queue by kind: %s", line);
    uint64_t same = 0;
    for (int i = 0; i < kOps; ++i) same += g_same[i];
    ModLog("render thread: render_thread_filter=%d. %.0f calls of a frame set "
           "the value that the device had already (SetRenderState %.0f, "
           "SetTextureStageState %.0f, SetSamplerState %.0f, SetTexture %.0f, "
           "SetFVF %.0f, SetViewport %.0f, SetTransform %.0f)%s",
           g_filter ? 1 : 0, same / per, g_same[kOpSetRenderState] / per,
           g_same[kOpSetTextureStageState] / per,
           g_same[kOpSetSamplerState] / per, g_same[kOpSetTexture] / per,
           g_same[kOpSetFVF] / per, g_same[kOpSetViewport] / per,
           g_same[kOpSetTransform] / per,
           g_filter ? ". They did not go to Direct3D 9" : "");
    memset(g_same, 0, sizeof(g_same));
    const LONG buffer_draws = InterlockedExchange(&g_buffer_draws, 0);
    const LONG up_draws = InterlockedExchange(&g_up_draws, 0);
    const LONG locks = InterlockedExchange(&g_buffer_locks, 0);
    if (g_buffers || buffer_draws > 0) {
      ModLog("render thread: render_thread_buffers=%d. A frame: %.0f draws "
             "from the buffers of the render thread, %.0f "
             "DrawIndexedPrimitiveUP, %.1f locks of the buffers",
             g_buffers ? 1 : 0, buffer_draws / per, up_draws / per,
             locks / per);
    }
  }
  uint64_t records = 0;
  for (int i = 0; i < kOps; ++i) records += g_records[i];
  uint64_t stops = 0;
  for (int i = 0; i < kStops; ++i) stops += g_stops[i];
  const double per = static_cast<double>(frames);
  const LONG failed = InterlockedExchange(&g_failed_calls, 0);
  const LONG64 busy = InterlockedExchange64(&g_busy_ticks, 0);
  ModLog("render thread: render_thread=%d. %llu scene walks in %llu frames. A "
         "frame: %.0f records (%.0f draws), %.0f KiB, %.1f stops (%.3f ms), "
         "the wait at the end of the walks %.3f ms, the render thread busy "
         "%.3f ms. %llu waits for room in the queue, %llu calls too large "
         "for the queue, %ld calls that failed",
         g_mode, g_walks, frames, records / per,
         (g_records[kOpDrawPrimitiveUP] + g_records[kOpDrawIndexedPrimitiveUP] +
          g_records[kOpDrawPrimitive] + g_records[kOpDrawIndexedPrimitive]) /
             per,
         g_bytes / 1024.0 / per, stops / per, Ms(g_stop_ticks) / per,
         Ms(g_end_ticks) / per, Ms(static_cast<uint64_t>(busy)) / per,
         g_full_waits, g_too_large, failed);
  if (stops > 0) {
    char line[512];
    int used = 0;
    for (int k = 0; k < 8; ++k) {
      int best = -1;
      for (int i = 0; i < kStops; ++i) {
        if (g_stops[i] > 0 && (best < 0 || g_stops[i] > g_stops[best])) {
          best = i;
        }
      }
      if (best < 0) break;
      used += sprintf_s(line + used, sizeof(line) - used, "%s%d: %.1f",
                        k == 0 ? "" : ", ", best, g_stops[best] / per);
      g_stops[best] = 0;
    }
    ModLog("render thread: the stops in a frame by call (the slot of the "
           "device; 140 a LockRect, 141 an UnlockRect, 142 a GetDC, 143 a "
           "lock of a buffer, 144 the issue of a query, 145 a surface call of "
           "DirectDraw): %s", line);
  }
  g_walks = 0;
  memset(g_records, 0, sizeof(g_records));
  memset(g_stops, 0, sizeof(g_stops));
  g_bytes = g_stop_ticks = g_end_ticks = g_full_waits = g_too_large = 0;
}

unsigned long RenderThreadWorkerId() { return g_worker_id; }

bool RenderThreadViewportBoth(IDirect3DDevice9* self,
                              const D3DVIEWPORT9* both) {
  if (!Taking(self) || both == nullptr || !g_kept.viewport_ok) return false;
  if (!g_vp_moved) g_vp_logical = g_kept.viewport;
  if (memcmp(&g_kept.viewport, both, sizeof(D3DVIEWPORT9)) != 0) {
    if (!Simple(kOpSetViewport, *both)) return false;
    g_kept.viewport = *both;
  }
  g_vp_moved = memcmp(&g_vp_logical, both, sizeof(D3DVIEWPORT9)) != 0;
  return true;
}

void RenderThreadViewportPutBack(IDirect3DDevice9* self) {
  if (Taking(self)) ViewportPutBack();
}

bool RenderThreadOutDrawIndexedBoth(IDirect3DDevice9* self,
                                    D3DPRIMITIVETYPE type, UINT min_index,
                                    UINT vertex_count, UINT primitive_count,
                                    const void* indices, D3DFORMAT index_format,
                                    const void* left, const void* right,
                                    UINT stride) {
  if (!Taking(self) || left == nullptr || right == nullptr ||
      indices == nullptr || type != D3DPT_TRIANGLELIST) {
    return false;
  }
  // The indices count from the start of each copy, so the right copy starts
  // count vertices after the left one.
  const uint32_t count = min_index + vertex_count;
  const bool wide = index_format == D3DFMT_INDEX32;
  if (!wide && 2u * count > 65536u) return false;
  const uint32_t index_count = IndexCount(type, primitive_count);
  const uint32_t copy_bytes = count * stride;
  const uint32_t vertex_bytes = 2u * copy_bytes;
  const uint32_t index_bytes = 2u * index_count * (wide ? 4u : 2u);
  const uint32_t padded = (vertex_bytes + 7u) & ~7u;
  uint8_t* p = Reserve(kOpDrawIndexedPrimitiveUP,
                       sizeof(ArgDrawIndexedUP) + padded + index_bytes);
  if (p == nullptr) return false;
  const ArgDrawIndexedUP a = {type,
                              min_index,
                              count + vertex_count,
                              2u * primitive_count,
                              index_format,
                              stride,
                              vertex_bytes,
                              index_bytes};
  memcpy(p, &a, sizeof(a));
  uint8_t* v = p + sizeof(a);
  memcpy(v, left, copy_bytes);
  memcpy(v + copy_bytes, right, copy_bytes);
  uint8_t* out = v + padded;
  if (wide) {
    const uint32_t* in = static_cast<const uint32_t*>(indices);
    uint32_t* o = reinterpret_cast<uint32_t*>(out);
    memcpy(o, in, index_count * 4u);
    for (uint32_t i = 0; i < index_count; ++i) o[index_count + i] = in[i] + count;
  } else {
    const uint16_t* in = static_cast<const uint16_t*>(indices);
    uint16_t* o = reinterpret_cast<uint16_t*>(out);
    memcpy(o, in, index_count * 2u);
    for (uint32_t i = 0; i < index_count; ++i) {
      o[index_count + i] = static_cast<uint16_t>(in[i] + count);
    }
  }
  Commit();
  return true;
}
