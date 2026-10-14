// The calls of Direct3D 7 of a scene walk on a thread of their own
// (d3d7_thread=1, docs/backend.md, "The thread of Direct3D 7").
//
// WHY. In a walk the frame thread calls the Direct3D 7 device of dxwrapper
// about 4000 times a frame. dxwrapper makes each call into calls of
// Direct3D 9, and the hooks of the render thread (render_thread.cpp) and of
// one walk (one_walk.cpp) make records of those for the render thread. All
// of that is on the frame thread. This file puts the calls of the device
// into a queue, and a thread of its own (the D3D7 thread) gives them to
// dxwrapper. The D3D7 thread then makes the records for the render thread.
//
// WHAT GOES IN THE QUEUE. In a walk the game makes SetRenderState,
// DrawIndexedPrimitive, SetTexture, SetTextureStageState and SetLight, and
// the hand and the overlay of the mod make DrawPrimitive (d3d7_count=1
// shows it). A hook on the function of dxwrapper behind each of these six
// methods makes a record with a copy of the values that the call points to,
// and returns D3D_OK. The hooks are on the inner functions of dxwrapper, so
// the hooks of the mod on the methods (draw_probe.cpp, texture_filter.cpp,
// draw_merge.cpp, eye_clean.cpp) run first, on the frame thread, as before.
//
// THE STOPS. Each other way of the frame thread into dxwrapper in a walk
// first waits until the D3D7 thread did each record (a stop):
//   - dxwrapper makes a scoped lock of its own (0x100096E0) at the start of
//     most methods of the device and of the surfaces. A hook of it stops.
//   - Clear, BeginScene, EndScene, the Get methods and the other methods of
//     the device take no such lock at their start. A hook of each of their
//     inner functions stops, and so do SetPalette and Restore of a surface.
//   - Five methods of the device have no inner function (GetTexture,
//     EndStateBlock, PreLoad, Load, GetInfo). Their entries in the table of
//     methods go to a thunk that stops.
//   - A call of the frame thread to Direct3D 9 in a walk stops
//     (render_thread.cpp, Taking).
// A stop never waits while the frame thread holds the lock of dxwrapper: a
// call that comes from inside dxwrapper goes to dxwrapper at once, and the
// queue is empty then, because the way in stopped first. A Release of a
// surface takes the lock, so a texture in a record is never released early.
//
// THE ORDER. One walk reads its mode and the move of the right eye in the
// hooks of Direct3D 9, which the D3D7 thread makes. So a change of these on
// the frame thread goes into the queue as a record that calls a function
// (D3D7ThreadPost, one_walk.cpp).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d.h>
#include <intrin.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"
#include "d3d7_thread.h"
#include "render_thread_ring.h"
#include "thread_id.h"
#include "thread_wait.h"

void ModLog(const char* format, ...);
// controllers.cpp
void ControllersGesturePulse(float amplitude, float seconds);
// one_walk.cpp: the state of one walk for the D3D7 thread, at the start of
// a walk.
void OneWalkPostState();
// one_walk.cpp: before a draw goes into the queue.
void OneWalkBeforeQueuedDraw();

namespace {

// ---------------------------------------------------------------------------
// The places in dxwrapper.dll (offsets from its base). The start of each
// function is checked before a hook goes in.

struct Place {
  uint32_t offset;
  uint8_t start[6];
};

const Place kSetRenderState = {0xBDED0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}};
const Place kSetTexture = {0xC57C0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}};
const Place kSetStageState = {0xC42E0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}};
const Place kSetLight = {0xC5110, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}};
const Place kDrawIndexed = {0xC13F0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}};
const Place kDraw = {0xC0F10, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}};
const Place kLock = {0x096E0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}};
// The place in the method DrawIndexedPrimitive and DrawPrimitive of the
// device after its call of the inner function.
const uint32_t kDrawIndexedReturn = 0x137554;
const uint32_t kDrawReturn = 0x13750E;

// The inner functions that stop.
const Place kStopPlaces[] = {
    {0xC9A30, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // Clear
    {0xBB610, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // BeginScene
    {0xBB740, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // EndScene
    {0xC3BE0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // GetTextureStageState
    {0xBD7F0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // GetRenderState
    {0xC0910, {0x55, 0x8B, 0xEC, 0x80, 0x3D, 0x49}},  // SetTransform
    {0xC0A30, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}},  // GetTransform
    {0xC0BB0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}},  // MultiplyTransform
    {0xCAB40, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}},  // SetViewport
    {0xCAA10, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // GetViewport
    {0xCAE00, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // SetMaterial
    {0xCAC80, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // GetMaterial
    {0xC53A0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // GetLight
    {0xCA880, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // LightEnable
    {0xC73E0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // GetLightEnable
    {0xCA930, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // SetClipPlane
    {0xC7600, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}},  // GetClipPlane
    {0xBBE20, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // SetRenderTarget
    {0xBC430, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // GetRenderTarget
    {0xC1900, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // SetClipStatus
    {0xC1B00, {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08}},  // GetClipStatus
    {0xC1D50, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // DrawPrimitiveStrided
    {0xC22D0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // DrawIndexedPrimitiveStrided
    {0xC2880, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // DrawPrimitiveVB
    {0xC2DB0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // DrawIndexedPrimitiveVB
    {0xC3420, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // ComputeSphereVisibility
    {0xC4750, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // ValidateDevice
    {0xC5AD0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // ApplyStateBlock
    {0xC64C0, {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08}},  // CaptureStateBlock
    {0xC6AD0, {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08}},  // CreateStateBlock
    {0xC4B30, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // GetCaps
    {0xFA680, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68}},  // surface SetPalette
    {0xF9F30, {0x55, 0x8B, 0xEC, 0x80, 0x3D, 0x49}},  // surface Restore
};
const int kStopCount = sizeof(kStopPlaces) / sizeof(kStopPlaces[0]);
// The slots of the table of methods of the device with no inner function:
// GetTexture, EndStateBlock, PreLoad, Load, GetInfo.
const int kTableSlots[] = {34, 23, 24, 43, 48};
const int kTableCount = sizeof(kTableSlots) / sizeof(kTableSlots[0]);
// The table of methods of the device of dxwrapper.
const uint32_t kDeviceTable = 0x753ABC;

// ---------------------------------------------------------------------------
// The records.

enum Op : uint16_t {
  kOpWrap = 0,
  kOpSetRenderState,
  kOpSetTexture,
  kOpSetStageState,
  kOpSetLight,
  kOpDrawIndexed,
  kOpDraw,
  kOpCall,
  kOps
};

struct Header {
  uint16_t op;
  uint16_t unused;
  uint32_t bytes;  // of the whole record
};

struct ArgState { void* x; DWORD a; DWORD b; DWORD c; };
struct ArgLight { void* x; DWORD index; D3DLIGHT7 light; };
struct ArgDraw {
  void* x;
  DWORD type;
  DWORD fvf;
  DWORD vertex_count;
  DWORD index_count;
  DWORD flags;
  DWORD version;
  DWORD vertex_bytes;  // the vertices follow, then the indices
};
struct ArgCall { D3D7ThreadFn fn; DWORD bytes; };  // the bytes follow

typedef HRESULT(WINAPI* State2Fn)(void*, DWORD, DWORD);
typedef HRESULT(WINAPI* State3Fn)(void*, DWORD, DWORD, DWORD);
typedef HRESULT(WINAPI* SetTextureFn)(void*, DWORD, void*);
typedef HRESULT(WINAPI* SetLightFn)(void*, DWORD, D3DLIGHT7*);
typedef HRESULT(WINAPI* DrawIndexedFn)(void*, DWORD, DWORD, void*, DWORD,
                                       WORD*, DWORD, DWORD, DWORD);
typedef HRESULT(WINAPI* DrawFn)(void*, DWORD, DWORD, void*, DWORD, DWORD,
                                DWORD);
// A constructor of MSVC gives `this` back in eax.
typedef void*(__fastcall* LockFn)(void*, void*, bool);

State2Fn o_SetRenderState = nullptr;
SetTextureFn o_SetTexture = nullptr;
State3Fn o_SetStageState = nullptr;
SetLightFn o_SetLight = nullptr;
DrawIndexedFn o_DrawIndexed = nullptr;
DrawFn o_Draw = nullptr;
LockFn o_Lock = nullptr;

// ---------------------------------------------------------------------------
// The state.

bool g_ini = false;       // d3d7_thread
bool g_on = false;        // the value now (the test key changes it)
bool g_in = false;        // the hooks are in
bool g_failed = false;
volatile bool g_active = false;  // a walk with the queue
uintptr_t g_base = 0;     // of dxwrapper.dll
uintptr_t g_end = 0;
DWORD g_frame_thread = 0;
DWORD g_worker_id = 0;
HANDLE g_worker = nullptr;
HANDLE g_work_event = nullptr;
volatile LONG g_worker_waits = 0;
// The records since the last wake of a D3D7 thread that sleeps
// (thread_wait.cpp). The frame thread writes it.
uint32_t g_unwoken = 0;

const uint32_t kQueueBytes = 16u << 20;
uint8_t* g_queue = nullptr;
__declspec(align(64)) volatile LONG g_write = 0;
__declspec(align(64)) volatile LONG g_read = 0;

// The thunks of the stops: 34 inner functions and 5 slots of the table.
uint8_t* g_thunks = nullptr;
const int kThunkBytes = 16;
void* g_stop_original[kStopCount + kTableCount] = {nullptr};

// For the report.
uint64_t g_walks = 0;
uint64_t g_records[kOps] = {0};
uint64_t g_bytes = 0;
uint64_t g_stops = 0;
uint64_t g_stop_ticks = 0;
uint64_t g_table_stops = 0;  // a stop before a hook of the mod
uint64_t g_not_queued = 0;   // a draw with a vertex format the file does not know
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

bool InDxwrapper(const void* address) {
  const uintptr_t a = reinterpret_cast<uintptr_t>(address);
  return a >= g_base && a < g_end;
}

// The bytes of a vertex of Direct3D 7, or 0 for a format that this file does
// not know.
uint32_t VertexBytes(DWORD fvf) {
  if ((fvf & 0x0001u) != 0 || (fvf & 0xF000u) != 0) return 0;
  uint32_t size = 0;
  switch (fvf & 0x000Eu) {
    case 0x002: size = 12; break;  // XYZ
    case 0x004: size = 16; break;  // XYZRHW
    case 0x006: size = 16; break;  // XYZB1
    case 0x008: size = 20; break;
    case 0x00A: size = 24; break;
    case 0x00C: size = 28; break;
    case 0x00E: size = 32; break;  // XYZB5
    default: return 0;
  }
  if (fvf & 0x010u) size += 12;  // normal
  if (fvf & 0x020u) size += 4;   // reserved1
  if (fvf & 0x040u) size += 4;   // diffuse
  if (fvf & 0x080u) size += 4;   // specular
  const DWORD sets = (fvf >> 8) & 0xFu;
  if (sets > 8) return 0;
  for (DWORD i = 0; i < sets; ++i) {
    switch ((fvf >> (16 + 2 * i)) & 3u) {
      case 0: size += 8; break;
      case 1: size += 12; break;
      case 2: size += 16; break;
      default: size += 4; break;
    }
  }
  return size;
}

// ---------------------------------------------------------------------------
// The queue: the frame thread writes, the D3D7 thread reads.

uint32_t LoadIndex(volatile LONG* index) { return static_cast<uint32_t>(*index); }
void StoreIndex(volatile LONG* index, uint32_t value) {
  *index = static_cast<LONG>(value);
}

uint8_t* Reserve(Op op, uint32_t payload) {
  const uint32_t bytes = render_ring::RecordBytes(payload);
  render_ring::Plan plan;
  uint32_t write = LoadIndex(&g_write);
  if (!render_ring::PlanRecord(write, kQueueBytes, bytes, &plan)) return nullptr;
  while (!render_ring::HasRoom(write, LoadIndex(&g_read), kQueueBytes,
                               plan.need)) {
    if (g_worker_waits != 0) SetEvent(g_work_event);
    YieldProcessor();
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
  h->op = op;
  h->unused = 0;
  h->bytes = bytes;
  ++g_records[op];
  g_bytes += bytes;
  return reinterpret_cast<uint8_t*>(h + 1);
}

void Commit() {
  const uint32_t write = LoadIndex(&g_write);
  const Header* h =
      reinterpret_cast<const Header*>(g_queue + (write & (kQueueBytes - 1)));
  _ReadWriteBarrier();
  StoreIndex(&g_write, write + h->bytes);
  if (g_worker_waits != 0 && ++g_unwoken >= ThreadWaitWakeRecords()) {
    g_unwoken = 0;
    SetEvent(g_work_event);
  }
}

// Gives the bytes of the record.
uint32_t Play(uint8_t* at) {
  const Header* h = reinterpret_cast<const Header*>(at);
  uint8_t* p = at + sizeof(Header);
  HRESULT hr = S_OK;
  switch (h->op) {
    case kOpSetRenderState: {
      const ArgState* a = reinterpret_cast<const ArgState*>(p);
      hr = o_SetRenderState(a->x, a->a, a->b);
      break;
    }
    case kOpSetTexture: {
      const ArgState* a = reinterpret_cast<const ArgState*>(p);
      hr = o_SetTexture(a->x, a->a, reinterpret_cast<void*>(a->b));
      break;
    }
    case kOpSetStageState: {
      const ArgState* a = reinterpret_cast<const ArgState*>(p);
      hr = o_SetStageState(a->x, a->a, a->b, a->c);
      break;
    }
    case kOpSetLight: {
      ArgLight* a = reinterpret_cast<ArgLight*>(p);
      hr = o_SetLight(a->x, a->index, &a->light);
      break;
    }
    case kOpDrawIndexed: {
      ArgDraw* a = reinterpret_cast<ArgDraw*>(p);
      uint8_t* vertices = reinterpret_cast<uint8_t*>(a + 1);
      WORD* indices = reinterpret_cast<WORD*>(vertices + a->vertex_bytes);
      hr = o_DrawIndexed(a->x, a->type, a->fvf, vertices, a->vertex_count,
                         indices, a->index_count, a->flags, a->version);
      break;
    }
    case kOpDraw: {
      ArgDraw* a = reinterpret_cast<ArgDraw*>(p);
      hr = o_Draw(a->x, a->type, a->fvf, a + 1, a->vertex_count, a->flags,
                  a->version);
      break;
    }
    case kOpCall: {
      const ArgCall* a = reinterpret_cast<const ArgCall*>(p);
      a->fn(a + 1);
      break;
    }
    default:
      break;
  }
  if (FAILED(hr)) InterlockedIncrement(&g_failed_calls);
  return h->bytes;
}

// Spins until the writer passes `read`, for at most `budget` ticks. True when
// a record came.
bool SpinForWrite(uint32_t read, int64_t budget) {
  if (budget <= 0) return false;
  LARGE_INTEGER start, now;
  QueryPerformanceCounter(&start);
  for (int i = 0;; ++i) {
    YieldProcessor();
    if (LoadIndex(&g_write) != read) return true;
    if ((i & 63) == 63) {
      QueryPerformanceCounter(&now);
      if (now.QuadPart - start.QuadPart > budget) return false;
    }
  }
}

DWORD WINAPI Worker(LPVOID) {
  for (;;) {
    uint32_t read = LoadIndex(&g_read);
    uint32_t write = LoadIndex(&g_write);
    if (read == write) {
      bool more = false;
      if (ThreadWaitMode() == 0) {
        for (int i = 0; i < 2000 || g_active; ++i) {
          YieldProcessor();
          if (LoadIndex(&g_write) != read) {
            more = true;
            break;
          }
        }
      } else {
        more = SpinForWrite(read, ThreadWaitSpinTicks(g_active));
      }
      if (!more) {
        InterlockedExchange(&g_worker_waits, 1);
        if (LoadIndex(&g_write) == read) {
          WaitForSingleObject(g_work_event, INFINITE);
        }
        InterlockedExchange(&g_worker_waits, 0);
      }
      continue;
    }
    const uint64_t start = Ticks();
    while (read != write) {
      read += Play(g_queue + (read & (kQueueBytes - 1)));
      // Each record before the count is done, so a stop that sees the count
      // knows that dxwrapper has no call of the queue in it.
      StoreIndex(&g_read, read);
      if (read == write) write = LoadIndex(&g_write);
    }
    InterlockedExchangeAdd64(&g_busy_ticks,
                             static_cast<LONG64>(Ticks() - start));
  }
}

// A stop: each record before it is done.
void Drain() {
  const uint32_t write = LoadIndex(&g_write);
  if (LoadIndex(&g_read) == write) return;
  const uint64_t start = Ticks();
  g_unwoken = 0;
  if (g_worker_waits != 0) SetEvent(g_work_event);
  while (LoadIndex(&g_read) != write) YieldProcessor();
  ++g_stops;
  g_stop_ticks += Ticks() - start;
}

// True when a call of the device on this thread goes into the queue: the
// frame thread in a walk with the queue, from outside dxwrapper.
bool Queueing(const void* caller) {
  return g_active && ThisThreadId() == g_frame_thread && !InDxwrapper(caller);
}

// A call that does not go into the queue: on the frame thread in a walk with
// the queue, each record before it is done first.
void StopIfFrame() {
  if (g_active && ThisThreadId() == g_frame_thread) Drain();
}

extern "C" void __cdecl D3D7StopFromThunk() { StopIfFrame(); }

extern "C" void __cdecl D3D7StopFromTable() {
  if (g_active && ThisThreadId() == g_frame_thread &&
      LoadIndex(&g_read) != LoadIndex(&g_write)) {
    ++g_table_stops;
  }
  StopIfFrame();
}

// ---------------------------------------------------------------------------
// The hooks of the six calls of the queue.

HRESULT WINAPI q_SetRenderState(void* x, DWORD state, DWORD value) {
  if (Queueing(_ReturnAddress())) {
    uint8_t* p = Reserve(kOpSetRenderState, sizeof(ArgState));
    if (p != nullptr) {
      const ArgState a = {x, state, value, 0};
      memcpy(p, &a, sizeof(a));
      Commit();
      return D3D_OK;
    }
  }
  StopIfFrame();
  return o_SetRenderState(x, state, value);
}

HRESULT WINAPI q_SetTexture(void* x, DWORD stage, void* surface) {
  if (Queueing(_ReturnAddress())) {
    uint8_t* p = Reserve(kOpSetTexture, sizeof(ArgState));
    if (p != nullptr) {
      const ArgState a = {x, stage,
                          static_cast<DWORD>(reinterpret_cast<uintptr_t>(surface)),
                          0};
      memcpy(p, &a, sizeof(a));
      Commit();
      return D3D_OK;
    }
  }
  StopIfFrame();
  return o_SetTexture(x, stage, surface);
}

HRESULT WINAPI q_SetStageState(void* x, DWORD stage, DWORD type, DWORD value) {
  if (Queueing(_ReturnAddress())) {
    uint8_t* p = Reserve(kOpSetStageState, sizeof(ArgState));
    if (p != nullptr) {
      const ArgState a = {x, stage, type, value};
      memcpy(p, &a, sizeof(a));
      Commit();
      return D3D_OK;
    }
  }
  StopIfFrame();
  return o_SetStageState(x, stage, type, value);
}

HRESULT WINAPI q_SetLight(void* x, DWORD index, D3DLIGHT7* light) {
  if (light != nullptr && Queueing(_ReturnAddress())) {
    uint8_t* p = Reserve(kOpSetLight, sizeof(ArgLight));
    if (p != nullptr) {
      ArgLight* a = reinterpret_cast<ArgLight*>(p);
      a->x = x;
      a->index = index;
      memcpy(&a->light, light, sizeof(D3DLIGHT7));
      Commit();
      return D3D_OK;
    }
  }
  StopIfFrame();
  return o_SetLight(x, index, light);
}

// The method of the device calls the inner draw functions, so the return
// address is in dxwrapper. The caller of the method is two words above the
// arguments: the method saved ebp and has its own return address.
const void* MethodCaller(void** return_slot, int arguments) {
  return return_slot[1 + arguments + 1];
}

HRESULT WINAPI q_DrawIndexed(void* x, DWORD type, DWORD fvf, void* vertices,
                             DWORD vertex_count, WORD* indices,
                             DWORD index_count, DWORD flags, DWORD version) {
  void** slot = reinterpret_cast<void**>(_AddressOfReturnAddress());
  const bool from_method = reinterpret_cast<uintptr_t>(*slot) ==
                           g_base + kDrawIndexedReturn;
  if (from_method && Queueing(MethodCaller(slot, 9)) && vertices != nullptr &&
      indices != nullptr) {
    const uint32_t stride = VertexBytes(fvf);
    const uint32_t vertex_bytes = stride * vertex_count;
    const uint32_t index_bytes = 2u * index_count;
    if (stride != 0) {
      OneWalkBeforeQueuedDraw();
      uint8_t* p = Reserve(kOpDrawIndexed,
                           sizeof(ArgDraw) + vertex_bytes + index_bytes);
      if (p != nullptr) {
        ArgDraw* a = reinterpret_cast<ArgDraw*>(p);
        a->x = x;
        a->type = type;
        a->fvf = fvf;
        a->vertex_count = vertex_count;
        a->index_count = index_count;
        a->flags = flags;
        a->version = version;
        a->vertex_bytes = vertex_bytes;
        memcpy(a + 1, vertices, vertex_bytes);
        memcpy(reinterpret_cast<uint8_t*>(a + 1) + vertex_bytes, indices,
               index_bytes);
        Commit();
        return D3D_OK;
      }
    } else {
      ++g_not_queued;
    }
  }
  StopIfFrame();
  return o_DrawIndexed(x, type, fvf, vertices, vertex_count, indices,
                       index_count, flags, version);
}

HRESULT WINAPI q_Draw(void* x, DWORD type, DWORD fvf, void* vertices,
                      DWORD vertex_count, DWORD flags, DWORD version) {
  void** slot = reinterpret_cast<void**>(_AddressOfReturnAddress());
  const bool from_method =
      reinterpret_cast<uintptr_t>(*slot) == g_base + kDrawReturn;
  if (from_method && Queueing(MethodCaller(slot, 7)) && vertices != nullptr) {
    const uint32_t stride = VertexBytes(fvf);
    const uint32_t vertex_bytes = stride * vertex_count;
    if (stride != 0) {
      OneWalkBeforeQueuedDraw();
      uint8_t* p = Reserve(kOpDraw, sizeof(ArgDraw) + vertex_bytes);
      if (p != nullptr) {
        ArgDraw* a = reinterpret_cast<ArgDraw*>(p);
        a->x = x;
        a->type = type;
        a->fvf = fvf;
        a->vertex_count = vertex_count;
        a->index_count = 0;
        a->flags = flags;
        a->version = version;
        a->vertex_bytes = vertex_bytes;
        memcpy(a + 1, vertices, vertex_bytes);
        Commit();
        return D3D_OK;
      }
    } else {
      ++g_not_queued;
    }
  }
  StopIfFrame();
  return o_Draw(x, type, fvf, vertices, vertex_count, flags, version);
}

// The scoped lock of dxwrapper: the frame thread stops before it takes it.
void* __fastcall h_Lock(void* self, void* unused, bool lock) {
  StopIfFrame();
  return o_Lock(self, unused, lock);
}

// ---------------------------------------------------------------------------
// Install.

bool Expect(uintptr_t address, const uint8_t* bytes, size_t size) {
  const void* site = reinterpret_cast<const void*>(address);
  return !IsBadReadPtr(site, size) && memcmp(site, bytes, size) == 0;
}

bool HookPlace(const Place& place, void* hook, void** original) {
  const uintptr_t target = g_base + place.offset;
  if (!Expect(target, place.start, sizeof(place.start))) return false;
  MH_STATUS status =
      MH_CreateHook(reinterpret_cast<void*>(target), hook, original);
  if (status == MH_OK) status = MH_QueueEnableHook(reinterpret_cast<void*>(target));
  return status == MH_OK;
}

// pushad; call fn; popad; jmp dword ptr [original]
void MakeStopThunk(uint8_t* at, void (*fn)(), void** original) {
  at[0] = 0x60;
  at[1] = 0xE8;
  const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(fn) -
                                           reinterpret_cast<intptr_t>(at + 6));
  memcpy(at + 2, &rel, 4);
  at[6] = 0x61;
  at[7] = 0xFF;
  at[8] = 0x25;
  const uint32_t o =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(original));
  memcpy(at + 9, &o, 4);
  at[13] = at[14] = at[15] = 0xCC;
}

bool Install() {
  HMODULE dx = GetModuleHandleA("dxwrapper.dll");
  if (dx == nullptr) return false;
  g_base = reinterpret_cast<uintptr_t>(dx);
  const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(dx);
  const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
      g_base + dos->e_lfanew);
  g_end = g_base + nt->OptionalHeader.SizeOfImage;
  // The table of methods of the device of the game must be the one of this
  // build of dxwrapper.
  void** slot = reinterpret_cast<void**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) return false;
  void** table = *reinterpret_cast<void***>(*slot);
  if (reinterpret_cast<uintptr_t>(table) != g_base + kDeviceTable) {
    ModLog("d3d7 thread  : the table of methods of the device is at 0x%08X, "
           "not at the place of this build of dxwrapper, so the calls stay "
           "on the frame thread",
           static_cast<unsigned>(reinterpret_cast<uintptr_t>(table)));
    return false;
  }
  g_thunks = static_cast<uint8_t*>(
      VirtualAlloc(nullptr, (kStopCount + kTableCount) * kThunkBytes,
                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  g_queue = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, kQueueBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (g_thunks == nullptr || g_queue == nullptr) return false;
  bool ok = true;
  ok = ok && HookPlace(kSetRenderState, reinterpret_cast<void*>(&q_SetRenderState),
                       reinterpret_cast<void**>(&o_SetRenderState));
  ok = ok && HookPlace(kSetTexture, reinterpret_cast<void*>(&q_SetTexture),
                       reinterpret_cast<void**>(&o_SetTexture));
  ok = ok && HookPlace(kSetStageState, reinterpret_cast<void*>(&q_SetStageState),
                       reinterpret_cast<void**>(&o_SetStageState));
  ok = ok && HookPlace(kSetLight, reinterpret_cast<void*>(&q_SetLight),
                       reinterpret_cast<void**>(&o_SetLight));
  ok = ok && HookPlace(kDrawIndexed, reinterpret_cast<void*>(&q_DrawIndexed),
                       reinterpret_cast<void**>(&o_DrawIndexed));
  ok = ok && HookPlace(kDraw, reinterpret_cast<void*>(&q_Draw),
                       reinterpret_cast<void**>(&o_Draw));
  ok = ok && HookPlace(kLock, reinterpret_cast<void*>(&h_Lock),
                       reinterpret_cast<void**>(&o_Lock));
  for (int i = 0; ok && i < kStopCount; ++i) {
    uint8_t* thunk = g_thunks + i * kThunkBytes;
    MakeStopThunk(thunk, reinterpret_cast<void (*)()>(&D3D7StopFromThunk),
                  &g_stop_original[i]);
    ok = HookPlace(kStopPlaces[i], thunk, &g_stop_original[i]);
  }
  if (ok) {
    DWORD old = 0;
    ok = VirtualProtect(table, 49 * sizeof(void*), PAGE_READWRITE, &old) != 0;
    for (int i = 0; ok && i < kTableCount; ++i) {
      uint8_t* thunk = g_thunks + (kStopCount + i) * kThunkBytes;
      g_stop_original[kStopCount + i] = table[kTableSlots[i]];
      MakeStopThunk(thunk, reinterpret_cast<void (*)()>(&D3D7StopFromTable),
                    &g_stop_original[kStopCount + i]);
      table[kTableSlots[i]] = thunk;
    }
    if (ok) VirtualProtect(table, 49 * sizeof(void*), old, &old);
  }
  FlushInstructionCache(GetCurrentProcess(), g_thunks,
                        (kStopCount + kTableCount) * kThunkBytes);
  if (ok) ok = MH_ApplyQueued() == MH_OK;
  if (!ok) {
    ModLog("d3d7 thread  : a hook in dxwrapper could not go in, or the code "
           "is not the code of this build, so the calls stay on the frame "
           "thread");
    return false;
  }
  g_work_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
  g_worker = CreateThread(nullptr, 0, &Worker, nullptr, 0, &g_worker_id);
  if (g_worker == nullptr) return false;
  SetThreadPriority(g_worker, THREAD_PRIORITY_ABOVE_NORMAL);
  ThreadWaitTune(g_worker, L"bwvr D3D7 thread");
  ThreadLoadAdd("D3D7 thread", g_worker);
  QueryPerformanceFrequency(&g_frequency);
  ModLog("hooks        : the D3D7 thread: 6 calls of the queue, the lock of "
         "dxwrapper, %d stops ok", kStopCount + kTableCount);
  return true;
}

}  // namespace

void D3D7ThreadSetOptions(int on) {
  g_ini = on != 0;
  g_on = g_ini;
  ModLog("d3d7 thread  : d3d7_thread=%d. %s", g_on ? 1 : 0,
         g_on ? "With render_thread=1, the calls of the Direct3D 7 device "
                "of a walk go to a thread of their own, which gives them to "
                "dxwrapper"
              : "The frame thread gives the calls of Direct3D 7 to dxwrapper");
}

bool D3D7ThreadOn() { return g_on; }
void D3D7ThreadSet(bool on) { g_on = on; }

void D3D7ThreadToggle(const char* keys) {
  g_on = !g_on;
  ControllersGesturePulse(0.5f, g_on ? 0.30f : 0.08f);
  ModLog("test key     : %s, d3d7_thread is %d from the next walk", keys,
         g_on ? 1 : 0);
}

void D3D7ThreadWalk(bool begin, bool render_thread) {
  if (begin) {
    if (!g_on || !render_thread) return;
    // With fewer than 4 logical cores the D3D7 thread takes a core from the
    // frame thread and the render thread, and the frames are slower.
    if (ThreadWaitLogicalCores() < 4) {
      static bool s_said = false;
      if (!s_said) {
        s_said = true;
        ModLog("d3d7 thread  : the process can use %d logical cores, fewer "
               "than 4. So the frame thread gives the calls of Direct3D 7 "
               "to dxwrapper", ThreadWaitLogicalCores());
      }
      return;
    }
    if (!g_in) {
      if (g_failed) return;
      g_in = Install();
      g_failed = !g_in;
      if (!g_in) return;
    }
    g_frame_thread = ThisThreadId();
    g_active = true;
    ++g_walks;
    OneWalkPostState();
    return;
  }
  if (!g_active) return;
  Drain();
  g_active = false;
}

bool D3D7ThreadActive() { return g_active; }

bool D3D7ThreadOnWorker() {
  return g_worker_id != 0 && ThisThreadId() == g_worker_id;
}

unsigned long D3D7ThreadWorkerId() { return g_worker_id; }

void D3D7ThreadStop() { StopIfFrame(); }

bool D3D7ThreadPost(D3D7ThreadFn fn, const void* data, unsigned bytes) {
  if (!g_active || ThisThreadId() != g_frame_thread) return false;
  uint8_t* p = Reserve(kOpCall, sizeof(ArgCall) + bytes);
  if (p == nullptr) return false;
  ArgCall* a = reinterpret_cast<ArgCall*>(p);
  a->fn = fn;
  a->bytes = bytes;
  if (bytes > 0) memcpy(a + 1, data, bytes);
  Commit();
  return true;
}

void D3D7ThreadReport(uint64_t frames) {
  if (!g_in || frames == 0) return;
  const double per = static_cast<double>(frames);
  const LONG64 busy = InterlockedExchange64(&g_busy_ticks, 0);
  const LONG failed = InterlockedExchange(&g_failed_calls, 0);
  ModLog("d3d7 thread  : d3d7_thread=%d. %llu walks in %llu frames. A frame: "
         "%.0f records (%.0f SetRenderState, %.0f DrawIndexedPrimitive, %.0f "
         "SetTexture, %.0f SetTextureStageState, %.0f SetLight, %.0f "
         "DrawPrimitive, %.0f of one walk), %.0f KiB, %.1f stops (%.3f ms), "
         "the D3D7 thread busy %.3f ms. %llu stops before a hook of the mod, "
         "%llu draws with a vertex format that the queue does not know, %ld "
         "calls that failed",
         g_on ? 1 : 0, g_walks, frames,
         (g_records[kOpSetRenderState] + g_records[kOpDrawIndexed] +
          g_records[kOpSetTexture] + g_records[kOpSetStageState] +
          g_records[kOpSetLight] + g_records[kOpDraw] + g_records[kOpCall]) /
             per,
         g_records[kOpSetRenderState] / per, g_records[kOpDrawIndexed] / per,
         g_records[kOpSetTexture] / per, g_records[kOpSetStageState] / per,
         g_records[kOpSetLight] / per, g_records[kOpDraw] / per,
         g_records[kOpCall] / per, g_bytes / 1024.0 / per, g_stops / per,
         Ms(g_stop_ticks) / per, Ms(static_cast<uint64_t>(busy)) / per,
         g_table_stops, g_not_queued, failed);
  g_walks = 0;
  memset(g_records, 0, sizeof(g_records));
  g_bytes = g_stops = g_stop_ticks = g_table_stops = g_not_queued = 0;
}
