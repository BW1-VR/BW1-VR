// THE BACKEND OF THE MOD ON VULKAN (backend=vulkan, vk_ddraw.h,
// docs/backend.md, "The backend of the mod on Vulkan").
//
// WHY. dxwrapper makes DirectDraw 7 and Direct3D 7 into Direct3D 9, and the
// mod then copies the pictures from Direct3D 9 to Direct3D 11 or Vulkan. Each
// layer is x86 code, and on a headset with an ARM CPU (Steam Frame) it runs
// through an emulator. This file makes DirectDraw 7 and Direct3D 7 itself:
// the surfaces keep their pixels in the memory of the CPU, and the device
// gives each scene to the Vulkan renderer (vk_scene.cpp) on the frame thread.
//
// WHAT THE GAME USES (ddraw_census=1 with backend=system, and the answers of
// dxwrapper at the first walk). The game runs in a window
// (SetCooperativeLevel DDSCL_NORMAL): a primary surface, an offscreen render
// target with a Z buffer, textures of 16 bits with mip levels, textures in
// DXT1 and DXT3 in the memory of the system that a Blt makes into textures
// of 16 bits, and one Blt a frame from the render target to the primary.
// It uses no Flip, no GetDC, no palette and no vertex buffer. Each method
// that this file does not make gives an error and writes one line of the
// log at its first call (Unmade), so a run shows what is missing.
//
// THE ANSWERS. The caps of the device, the texture formats, the formats of
// the Z buffer and the DirectDraw caps are the answers of dxwrapper (the
// log of ddraw_census=1), so the game makes the same choices as with
// dxwrapper. The identifier of the device has the GPU of Vulkan.
//
// THE PICTURE. Each BeginScene and EndScene of the device is a walk of the
// renderer (VkSceneWalkBegin, VkSceneWalkEnd). The Blt from the render
// target to the primary is the present: the picture of the last walk of the
// left eye goes to the window at the size of its client area
// (VkScenePresentPixels, then SetDIBitsToDevice).

#define WIN32_LEAN_AND_MEAN
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../proxy/ddraw_proxy.h"
#include "vk_core.h"
#include "vk_ddraw.h"
#include "vk_fallback.h"
#include "vk_scene.h"
#include "vk_window.h"
#include "vk_prim_math.h"
#include "vk_texture_math.h"

void ModLog(const char* format, ...);

namespace {

bool g_on = false;
// vk_lines of bwvr.ini: 1 (the default) a draw of points or lines becomes
// triangles (vk_prim_math.h). 0: it is not drawn, and the report counts it.
bool g_lines = true;

// THE METHODS THAT THIS FILE DOES NOT MAKE: a count for each name, and one
// line of the log at the first call.
const int kMaxUnmade = 128;
struct Unmade {
  const char* name;
  LONG count;
};
Unmade g_unmade[kMaxUnmade];
int g_unmade_count = 0;
SRWLOCK g_unmade_lock = SRWLOCK_INIT;

HRESULT NotMade(const char* name, HRESULT result = DDERR_UNSUPPORTED) {
  AcquireSRWLockExclusive(&g_unmade_lock);
  int i = 0;
  for (; i < g_unmade_count; ++i) {
    if (g_unmade[i].name == name) break;
  }
  bool first = false;
  if (i == g_unmade_count && g_unmade_count < kMaxUnmade) {
    g_unmade[i].name = name;
    g_unmade[i].count = 0;
    ++g_unmade_count;
    first = true;
  }
  if (i < g_unmade_count) ++g_unmade[i].count;
  ReleaseSRWLockExclusive(&g_unmade_lock);
  if (first) {
    ModLog("vk ddraw     : the game called %s, which the backend does not "
           "make (0x%08lX)",
           name, result);
  }
  return result;
}

void LogInterface(const char* who, REFIID id) {
  static LONG s_said = 0;
  if (InterlockedIncrement(&s_said) > 64) return;
  ModLog("vk ddraw     : QueryInterface of %s for "
         "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}: no interface",
         who, id.Data1, id.Data2, id.Data3, id.Data4[0], id.Data4[1],
         id.Data4[2], id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6],
         id.Data4[7]);
}

// A write of the CPU into the render target (a Lock for a write, a Blt from a
// surface). Its pixels are not the picture of the walks. The first 48 go
// into the log with the address of the caller in the game.
volatile LONG g_target_writes = 0;

void LogTargetWrite(const char* how, const RECT* rect, DWORD flags,
                    void* caller) {
  const LONG n = InterlockedIncrement(&g_target_writes);
  if (n > 48) return;
  RECT r = {};
  if (rect != nullptr) r = *rect;
  ModLog("vk ddraw     : write %ld of the CPU into the render target: %s, "
         "flags 0x%lX, rect %s%ld %ld %ld %ld, from %p",
         n, how, flags, rect != nullptr ? "" : "(all) ", r.left, r.top,
         r.right, r.bottom, caller);
}

// The counts of the objects, for the report.
volatile LONG g_surfaces = 0;
volatile LONG g_surfaces_made = 0;
volatile LONG g_presents = 0;
volatile LONG g_dxt_blts = 0;
volatile LONG g_copy_blts = 0;
volatile LONG g_fills = 0;
volatile LONG g_line_draws = 0;
volatile LONG g_line_prims = 0;

// THE FORMATS.
DDPIXELFORMAT Rgb(DWORD bits, DWORD r, DWORD g, DWORD b, DWORD a) {
  DDPIXELFORMAT p = {};
  p.dwSize = sizeof(p);
  p.dwFlags = DDPF_RGB | (a != 0 ? DDPF_ALPHAPIXELS : 0);
  p.dwRGBBitCount = bits;
  p.dwRBitMask = r;
  p.dwGBitMask = g;
  p.dwBBitMask = b;
  p.dwRGBAlphaBitMask = a;
  return p;
}

DDPIXELFORMAT FourCc(DWORD code) {
  DDPIXELFORMAT p = {};
  p.dwSize = sizeof(p);
  p.dwFlags = DDPF_FOURCC;
  p.dwFourCC = code;
  return p;
}

const DWORD kDxt1 = MAKEFOURCC('D', 'X', 'T', '1');
const DWORD kDxt2 = MAKEFOURCC('D', 'X', 'T', '2');
const DWORD kDxt3 = MAKEFOURCC('D', 'X', 'T', '3');
const DWORD kDxt4 = MAKEFOURCC('D', 'X', 'T', '4');
const DWORD kDxt5 = MAKEFOURCC('D', 'X', 'T', '5');

// The format of the display and of the render target: X8R8G8B8.
DDPIXELFORMAT DisplayFormat() {
  return Rgb(32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
}

// THE ANSWERS OF DXWRAPPER (ddraw_census=1, the first walk): the caps of
// its HAL device as the 59 words of D3DDEVICEDESC7.
const uint32_t kDeviceDesc[59] = {
    0x0008EEF1, 0x00000038, 0x00000072, 0x003B7D91, 0x000000FF, 0x00001FFF,
    0x00001FFF, 0x000000FF, 0x000C528A, 0x0000155F, 0x0703073F, 0x000000FF,
    0x0000001F, 0x00000000, 0x00000000, 0x00000038, 0x00000072, 0x003B7D91,
    0x000000FF, 0x00001FFF, 0x00001FFF, 0x000000FF, 0x000C528A, 0x0000155F,
    0x0703073F, 0x000000FF, 0x0000001F, 0x00000000, 0x00000000, 0x00000500,
    0x00000600, 0x00000001, 0x00000001, 0x00004000, 0x00004000, 0x00002000,
    0x00002000, 0x00000010, 0xC7000000, 0xC7000000, 0x47000000, 0x47000000,
    0x00000000, 0x000000FF, 0x00000000, 0x03FFFFFF, 0x00080008, 0x00000008,
    0x501502F9, 0x84E63DE0, 0x11CF46AA, 0x00006F81, 0x6E1520C0, 0x00040006,
    0x0000003F, 0x00000000, 0x00000000, 0x00000000, 0x00000000};
static_assert(sizeof(kDeviceDesc) == sizeof(D3DDEVICEDESC7),
              "the caps of the device are 59 words");

D3DDEVICEDESC7 DeviceDesc(REFCLSID id) {
  D3DDEVICEDESC7 d;
  memcpy(&d, kDeviceDesc, sizeof(d));
  d.deviceGUID = id;
  return d;
}

// The texture formats of dxwrapper, in its order: the formats of 16 and 32
// bits that the renderer reads, and DXT1 to DXT5.
DDPIXELFORMAT TextureFormat(int i) {
  switch (i) {
    case 0: return Rgb(16, 0xF800, 0x07E0, 0x001F, 0);
    case 1: return Rgb(16, 0x7C00, 0x03E0, 0x001F, 0);
    case 2: return Rgb(16, 0x7C00, 0x03E0, 0x001F, 0x8000);
    case 3: return Rgb(16, 0x0F00, 0x00F0, 0x000F, 0xF000);
    case 4: return Rgb(32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    case 5: return Rgb(32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    case 6: return FourCc(kDxt1);
    case 7: return FourCc(kDxt2);
    case 8: return FourCc(kDxt3);
    case 9: return FourCc(kDxt4);
    default: return FourCc(kDxt5);
  }
}
const int kTextureFormats = 11;

DDPIXELFORMAT ZFormat(int i) {
  DDPIXELFORMAT p = {};
  p.dwSize = sizeof(p);
  p.dwFlags = DDPF_ZBUFFER;
  if (i == 0) {
    p.dwZBufferBitDepth = 16;
    p.dwZBitMask = 0xFFFF;
  } else if (i == 1) {
    p.dwFlags |= DDPF_STENCILBUFFER;
    p.dwZBufferBitDepth = 32;
    p.dwStencilBitDepth = 8;
    p.dwZBitMask = 0x00FFFFFF;
    p.dwStencilBitMask = 0xFF000000;
  } else {
    p.dwZBufferBitDepth = 32;
    p.dwZBitMask = 0x00FFFFFF;
  }
  return p;
}
const int kZFormats = 3;

// The bytes of one pixel, or 0 for a format of blocks (DXT) and a Z buffer
// with no bits.
uint32_t PixelBytes(const DDPIXELFORMAT& p) {
  if (p.dwFlags & DDPF_FOURCC) return 0;
  if (p.dwFlags & DDPF_ZBUFFER) return p.dwZBufferBitDepth / 8;
  return p.dwRGBBitCount / 8;
}

uint32_t BlockBytes(DWORD fourcc) { return fourcc == kDxt1 ? 8 : 16; }

class Draw;
class Surface;

// The 16 cells of a matrix of Direct3D 7, row by row (_11 to _44).
float* Cells(D3DMATRIX* m) { return &m->_11; }

// ---------------------------------------------------------------------------
// THE CLIPPER: the window of the game.

class Clipper : public IDirectDrawClipper {
 public:
  STDMETHOD(QueryInterface)(REFIID id, LPVOID* out) override {
    if (out == nullptr) return E_POINTER;
    if (IsEqualGUID(id, IID_IUnknown) || IsEqualGUID(id, IID_IDirectDrawClipper)) {
      *out = this;
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    LogInterface("the clipper", id);
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    const LONG left = InterlockedDecrement(&refs_);
    if (left == 0) delete this;
    return left;
  }
  STDMETHOD(GetClipList)(LPRECT, LPRGNDATA, LPDWORD) override {
    return NotMade("IDirectDrawClipper::GetClipList");
  }
  STDMETHOD(GetHWnd)(HWND* out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = window_;
    return DD_OK;
  }
  STDMETHOD(Initialize)(LPDIRECTDRAW, DWORD) override {
    return DDERR_ALREADYINITIALIZED;
  }
  STDMETHOD(IsClipListChanged)(BOOL* out) override {
    if (out != nullptr) *out = FALSE;
    return DD_OK;
  }
  STDMETHOD(SetClipList)(LPRGNDATA, DWORD) override {
    return NotMade("IDirectDrawClipper::SetClipList");
  }
  STDMETHOD(SetHWnd)(DWORD, HWND window) override {
    window_ = window;
    return DD_OK;
  }
  HWND window() const { return window_; }

 private:
  LONG refs_ = 1;
  HWND window_ = nullptr;
};

// ---------------------------------------------------------------------------
// THE SURFACE: the desc, the pixels in the memory of the CPU, and the
// attached surfaces (the next mip level, the Z buffer).

// THE IDirect3DTexture2 OF A SURFACE. The texture loader of the game asks a
// texture for it and calls Load, which copies the pixels of each level of
// another texture (a texture in the memory of the system) into this one.
class TextureFace : public IDirect3DTexture2 {
 public:
  explicit TextureFace(Surface* owner) : owner_(owner) {}
  STDMETHOD(QueryInterface)(REFIID id, LPVOID* out) override;
  STDMETHOD_(ULONG, AddRef)() override;
  STDMETHOD_(ULONG, Release)() override;
  STDMETHOD(GetHandle)(LPDIRECT3DDEVICE2, LPD3DTEXTUREHANDLE handle) override {
    if (handle == nullptr) return DDERR_INVALIDPARAMS;
    *handle = static_cast<D3DTEXTUREHANDLE>(reinterpret_cast<uintptr_t>(owner_));
    return D3D_OK;
  }
  STDMETHOD(PaletteChanged)(DWORD, DWORD) override { return D3D_OK; }
  STDMETHOD(Load)(LPDIRECT3DTEXTURE2 from) override;
  Surface* owner() const { return owner_; }

 private:
  Surface* owner_;
};

class Surface : public IDirectDrawSurface7 {
 public:
  Surface(Draw* draw, const DDSURFACEDESC2& desc);
  ~Surface();

  STDMETHOD(QueryInterface)(REFIID id, LPVOID* out) override;
  STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    const LONG left = InterlockedDecrement(&refs_);
    if (left == 0) delete this;
    return left;
  }
  STDMETHOD(AddAttachedSurface)(LPDIRECTDRAWSURFACE7 s) override {
    if (s == nullptr) return DDERR_INVALIDPARAMS;
    Surface* other = static_cast<Surface*>(s);
    if (other->desc_.ddsCaps.dwCaps & DDSCAPS_ZBUFFER) {
      if (z_ != nullptr) z_->Release();
      z_ = other;
      z_->AddRef();
      return DD_OK;
    }
    return NotMade("IDirectDrawSurface7::AddAttachedSurface (no Z buffer)");
  }
  STDMETHOD(AddOverlayDirtyRect)(LPRECT) override {
    return NotMade("IDirectDrawSurface7::AddOverlayDirtyRect");
  }
  STDMETHOD(Blt)(LPRECT to, LPDIRECTDRAWSURFACE7 from, LPRECT from_rect,
                 DWORD flags, LPDDBLTFX fx) override;
  STDMETHOD(BltBatch)(LPDDBLTBATCH, DWORD, DWORD) override {
    return NotMade("IDirectDrawSurface7::BltBatch");
  }
  STDMETHOD(BltFast)(DWORD x, DWORD y, LPDIRECTDRAWSURFACE7 from,
                     LPRECT from_rect, DWORD flags) override;
  STDMETHOD(DeleteAttachedSurface)(DWORD, LPDIRECTDRAWSURFACE7 s) override {
    if (s != nullptr && s == z_) {
      z_->Release();
      z_ = nullptr;
      return DD_OK;
    }
    return NotMade("IDirectDrawSurface7::DeleteAttachedSurface");
  }
  STDMETHOD(EnumAttachedSurfaces)(LPVOID context,
                                  LPDDENUMSURFACESCALLBACK7 callback) override {
    if (callback == nullptr) return DDERR_INVALIDPARAMS;
    Surface* const list[2] = {next_, z_};
    for (Surface* s : list) {
      if (s == nullptr) continue;
      s->AddRef();
      DDSURFACEDESC2 d;
      s->FillDesc(&d);
      if (callback(s, &d, context) == DDENUMRET_CANCEL) break;
    }
    return DD_OK;
  }
  STDMETHOD(EnumOverlayZOrders)(DWORD, LPVOID,
                                LPDDENUMSURFACESCALLBACK7) override {
    return NotMade("IDirectDrawSurface7::EnumOverlayZOrders");
  }
  STDMETHOD(Flip)(LPDIRECTDRAWSURFACE7, DWORD) override {
    return NotMade("IDirectDrawSurface7::Flip");
  }
  STDMETHOD(GetAttachedSurface)(LPDDSCAPS2 caps,
                                LPDIRECTDRAWSURFACE7* out) override {
    if (caps == nullptr || out == nullptr) return DDERR_INVALIDPARAMS;
    *out = nullptr;
    if ((caps->dwCaps & DDSCAPS_ZBUFFER) && z_ != nullptr) {
      *out = z_;
    } else if ((caps->dwCaps & (DDSCAPS_MIPMAP | DDSCAPS_TEXTURE)) &&
               next_ != nullptr) {
      *out = next_;
    }
    if (*out == nullptr) return DDERR_NOTFOUND;
    (*out)->AddRef();
    return DD_OK;
  }
  STDMETHOD(GetBltStatus)(DWORD) override { return DD_OK; }
  STDMETHOD(GetCaps)(LPDDSCAPS2 caps) override {
    if (caps == nullptr) return DDERR_INVALIDPARAMS;
    *caps = desc_.ddsCaps;
    return DD_OK;
  }
  STDMETHOD(GetClipper)(LPDIRECTDRAWCLIPPER* out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = clipper_;
    if (clipper_ == nullptr) return DDERR_NOCLIPPERATTACHED;
    clipper_->AddRef();
    return DD_OK;
  }
  STDMETHOD(GetColorKey)(DWORD flags, LPDDCOLORKEY key) override {
    if (key == nullptr) return DDERR_INVALIDPARAMS;
    if ((flags & DDCKEY_SRCBLT) && has_key_) {
      *key = key_;
      return DD_OK;
    }
    return DDERR_NOCOLORKEY;
  }
  STDMETHOD(GetDC)(HDC*) override {
    return NotMade("IDirectDrawSurface7::GetDC");
  }
  STDMETHOD(GetFlipStatus)(DWORD) override { return DD_OK; }
  STDMETHOD(GetOverlayPosition)(LPLONG, LPLONG) override {
    return NotMade("IDirectDrawSurface7::GetOverlayPosition");
  }
  STDMETHOD(GetPalette)(LPDIRECTDRAWPALETTE*) override {
    return DDERR_NOPALETTEATTACHED;
  }
  STDMETHOD(GetPixelFormat)(LPDDPIXELFORMAT out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = desc_.ddpfPixelFormat;
    return DD_OK;
  }
  STDMETHOD(GetSurfaceDesc)(LPDDSURFACEDESC2 out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    FillDesc(out);
    return DD_OK;
  }
  STDMETHOD(Initialize)(LPDIRECTDRAW, LPDDSURFACEDESC2) override {
    return DDERR_ALREADYINITIALIZED;
  }
  STDMETHOD(IsLost)() override { return DD_OK; }
  STDMETHOD(Lock)(LPRECT rect, LPDDSURFACEDESC2 out, DWORD flags,
                  HANDLE) override;
  STDMETHOD(ReleaseDC)(HDC) override {
    return NotMade("IDirectDrawSurface7::ReleaseDC");
  }
  STDMETHOD(Restore)() override { return DD_OK; }
  STDMETHOD(SetClipper)(LPDIRECTDRAWCLIPPER c) override {
    if (clipper_ != nullptr) clipper_->Release();
    clipper_ = static_cast<Clipper*>(c);
    if (clipper_ != nullptr) clipper_->AddRef();
    return DD_OK;
  }
  STDMETHOD(SetColorKey)(DWORD flags, LPDDCOLORKEY key) override {
    if (flags & DDCKEY_SRCBLT) {
      has_key_ = key != nullptr;
      if (key != nullptr) key_ = *key;
    }
    return DD_OK;
  }
  STDMETHOD(SetOverlayPosition)(LONG, LONG) override {
    return NotMade("IDirectDrawSurface7::SetOverlayPosition");
  }
  STDMETHOD(SetPalette)(LPDIRECTDRAWPALETTE) override {
    return NotMade("IDirectDrawSurface7::SetPalette");
  }
  STDMETHOD(Unlock)(LPRECT) override;
  STDMETHOD(UpdateOverlay)(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD,
                           LPDDOVERLAYFX) override {
    return NotMade("IDirectDrawSurface7::UpdateOverlay");
  }
  STDMETHOD(UpdateOverlayDisplay)(DWORD) override {
    return NotMade("IDirectDrawSurface7::UpdateOverlayDisplay");
  }
  STDMETHOD(UpdateOverlayZOrder)(DWORD, LPDIRECTDRAWSURFACE7) override {
    return NotMade("IDirectDrawSurface7::UpdateOverlayZOrder");
  }
  STDMETHOD(GetDDInterface)(LPVOID* out) override;
  STDMETHOD(PageLock)(DWORD) override { return DD_OK; }
  STDMETHOD(PageUnlock)(DWORD) override { return DD_OK; }
  STDMETHOD(SetSurfaceDesc)(LPDDSURFACEDESC2, DWORD) override {
    return NotMade("IDirectDrawSurface7::SetSurfaceDesc");
  }
  STDMETHOD(SetPrivateData)(REFGUID, LPVOID, DWORD, DWORD) override {
    return NotMade("IDirectDrawSurface7::SetPrivateData");
  }
  STDMETHOD(GetPrivateData)(REFGUID, LPVOID, LPDWORD) override {
    return DDERR_NOTFOUND;
  }
  STDMETHOD(FreePrivateData)(REFGUID) override { return DDERR_NOTFOUND; }
  STDMETHOD(GetUniquenessValue)(LPDWORD out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = uniqueness_;
    return DD_OK;
  }
  STDMETHOD(ChangeUniquenessValue)() override {
    ++uniqueness_;
    return DD_OK;
  }
  STDMETHOD(SetPriority)(DWORD p) override {
    priority_ = p;
    return DD_OK;
  }
  STDMETHOD(GetPriority)(LPDWORD out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = priority_;
    return DD_OK;
  }
  STDMETHOD(SetLOD)(DWORD lod) override {
    lod_ = lod;
    return DD_OK;
  }
  STDMETHOD(GetLOD)(LPDWORD out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = lod_;
    return DD_OK;
  }

  void FillDesc(DDSURFACEDESC2* out) const {
    *out = desc_;
    out->dwSize = sizeof(*out);
    out->lpSurface = nullptr;
    if (PixelBytes(desc_.ddpfPixelFormat) != 0) {
      out->dwFlags |= DDSD_PITCH;
      out->lPitch = pitch_;
    } else if (desc_.ddpfPixelFormat.dwFlags & DDPF_FOURCC) {
      out->dwFlags |= DDSD_LINEARSIZE;
      out->dwLinearSize = bytes_;
    }
  }
  bool IsPrimary() const {
    return (desc_.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
  }
  const DDSURFACEDESC2& desc() const { return desc_; }
  void SetNext(Surface* next) { next_ = next; }  // takes the reference
  // The pixels, made at their first use.
  uint8_t* Pixels() {
    if (bits_ == nullptr && bytes_ > 0) {
      bits_ = static_cast<uint8_t*>(
          VirtualAlloc(nullptr, bytes_, MEM_COMMIT | MEM_RESERVE,
                       PAGE_READWRITE));
    }
    return bits_;
  }
  LONG pitch() const { return pitch_; }
  Clipper* clipper() const { return clipper_; }
  Surface* next() const { return next_; }
  TextureFace* face() { return &face_; }

 private:
  void Present();

  LONG refs_ = 1;
  TextureFace face_{this};
  Draw* draw_;
  DDSURFACEDESC2 desc_;
  LONG pitch_ = 0;
  DWORD bytes_ = 0;
  uint8_t* bits_ = nullptr;
  bool locked_write_ = false;
  Surface* next_ = nullptr;  // the next mip level
  Surface* z_ = nullptr;     // the attached Z buffer
  Clipper* clipper_ = nullptr;
  bool has_key_ = false;
  DDCOLORKEY key_ = {};
  DWORD uniqueness_ = 1;
  DWORD priority_ = 0;
  DWORD lod_ = 0;
};

// ---------------------------------------------------------------------------
// THE DEVICE: the state of Direct3D 7 for Get, and each call to the
// renderer.

class Direct3D;

class Device : public IDirect3DDevice7 {
 public:
  Device(Direct3D* d3d, Surface* target, REFCLSID id);
  ~Device();

  STDMETHOD(QueryInterface)(REFIID id, LPVOID* out) override {
    if (out == nullptr) return E_POINTER;
    if (IsEqualGUID(id, IID_IUnknown) || IsEqualGUID(id, IID_IDirect3DDevice7)) {
      *out = this;
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    LogInterface("the device", id);
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    const LONG left = InterlockedDecrement(&refs_);
    if (left == 0) delete this;
    return left;
  }
  STDMETHOD(GetCaps)(LPD3DDEVICEDESC7 out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = DeviceDesc(id_);
    return D3D_OK;
  }
  STDMETHOD(EnumTextureFormats)(LPD3DENUMPIXELFORMATSCALLBACK callback,
                                LPVOID context) override {
    if (callback == nullptr) return DDERR_INVALIDPARAMS;
    for (int i = 0; i < kTextureFormats; ++i) {
      DDPIXELFORMAT p = TextureFormat(i);
      if (callback(&p, context) == D3DENUMRET_CANCEL) break;
    }
    return D3D_OK;
  }
  STDMETHOD(BeginScene)() override {
    if (in_scene_) return D3DERR_SCENE_IN_SCENE;
    in_scene_ = true;
    VkSceneWalkBegin(this);
    return D3D_OK;
  }
  STDMETHOD(EndScene)() override {
    if (!in_scene_) return D3DERR_SCENE_NOT_IN_SCENE;
    in_scene_ = false;
    VkSceneWalkEnd();
    return D3D_OK;
  }
  STDMETHOD(GetDirect3D)(LPDIRECT3D7* out) override;
  STDMETHOD(SetRenderTarget)(LPDIRECTDRAWSURFACE7 s, DWORD) override {
    if (s == nullptr) return DDERR_INVALIDPARAMS;
    s->AddRef();
    if (target_ != nullptr) target_->Release();
    target_ = static_cast<Surface*>(s);
    VkSceneTarget(target_);
    return D3D_OK;
  }
  STDMETHOD(GetRenderTarget)(LPDIRECTDRAWSURFACE7* out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = target_;
    if (target_ != nullptr) target_->AddRef();
    return D3D_OK;
  }
  STDMETHOD(Clear)(DWORD count, LPD3DRECT rects, DWORD flags, D3DCOLOR color,
                   D3DVALUE z, DWORD) override {
    VkSceneClear(count, rects, flags, color, z);
    return D3D_OK;
  }
  STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX m) override {
    if (m == nullptr) return DDERR_INVALIDPARAMS;
    D3DMATRIX* slot = Transform(type);
    if (slot != nullptr) *slot = *m;
    return D3D_OK;
  }
  STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE type, LPD3DMATRIX m) override {
    if (m == nullptr) return DDERR_INVALIDPARAMS;
    D3DMATRIX* slot = Transform(type);
    if (slot == nullptr) return DDERR_INVALIDPARAMS;
    *m = *slot;
    return D3D_OK;
  }
  STDMETHOD(SetViewport)(LPD3DVIEWPORT7 v) override {
    if (v == nullptr) return DDERR_INVALIDPARAMS;
    viewport_ = *v;
    VkSceneViewport(v);
    return D3D_OK;
  }
  STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE type,
                               LPD3DMATRIX m) override {
    if (m == nullptr) return DDERR_INVALIDPARAMS;
    D3DMATRIX* slot = Transform(type);
    if (slot == nullptr) return DDERR_INVALIDPARAMS;
    D3DMATRIX r;
    const float* a = Cells(m);
    const float* b = Cells(slot);
    float* out = Cells(&r);
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) sum += a[i * 4 + k] * b[k * 4 + j];
        out[i * 4 + j] = sum;
      }
    }
    *slot = r;
    return D3D_OK;
  }
  STDMETHOD(GetViewport)(LPD3DVIEWPORT7 v) override {
    if (v == nullptr) return DDERR_INVALIDPARAMS;
    *v = viewport_;
    return D3D_OK;
  }
  STDMETHOD(SetMaterial)(LPD3DMATERIAL7 m) override {
    if (m == nullptr) return DDERR_INVALIDPARAMS;
    material_ = *m;
    return D3D_OK;
  }
  STDMETHOD(GetMaterial)(LPD3DMATERIAL7 m) override {
    if (m == nullptr) return DDERR_INVALIDPARAMS;
    *m = material_;
    return D3D_OK;
  }
  STDMETHOD(SetLight)(DWORD i, LPD3DLIGHT7 l) override {
    if (l == nullptr || i >= kLights) return DDERR_INVALIDPARAMS;
    lights_[i] = *l;
    return D3D_OK;
  }
  STDMETHOD(GetLight)(DWORD i, LPD3DLIGHT7 l) override {
    if (l == nullptr || i >= kLights) return DDERR_INVALIDPARAMS;
    *l = lights_[i];
    return D3D_OK;
  }
  STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE type, DWORD value) override {
    if (static_cast<uint32_t>(type) >= kStates) return DDERR_INVALIDPARAMS;
    if (recording_ != nullptr) {
      recording_->rs[type] = value;
      recording_->rs_set[type] = true;
    }
    rs_[type] = value;
    VkSceneRenderState(type, value);
    return D3D_OK;
  }
  STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE type, LPDWORD out) override {
    if (out == nullptr || static_cast<uint32_t>(type) >= kStates) {
      return DDERR_INVALIDPARAMS;
    }
    *out = rs_[type];
    return D3D_OK;
  }
  STDMETHOD(BeginStateBlock)() override {
    if (recording_ != nullptr) return D3DERR_INBEGINSTATEBLOCK;
    recording_ = new Block();
    memset(recording_, 0, sizeof(*recording_));
    return D3D_OK;
  }
  STDMETHOD(EndStateBlock)(LPDWORD out) override {
    if (recording_ == nullptr) return D3DERR_NOTINBEGINSTATEBLOCK;
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = AddBlock(recording_);
    recording_ = nullptr;
    return D3D_OK;
  }
  STDMETHOD(PreLoad)(LPDIRECTDRAWSURFACE7) override { return D3D_OK; }
  STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID vertices,
                           DWORD count, DWORD) override {
    DrawTriangles(type, fvf, vertices, count, nullptr, 0);
    return D3D_OK;
  }
  STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE type, DWORD fvf,
                                  LPVOID vertices, DWORD count, LPWORD indices,
                                  DWORD index_count, DWORD) override {
    DrawTriangles(type, fvf, vertices, count, indices, index_count);
    return D3D_OK;
  }
  STDMETHOD(SetClipStatus)(LPD3DCLIPSTATUS) override { return D3D_OK; }
  STDMETHOD(GetClipStatus)(LPD3DCLIPSTATUS s) override {
    if (s != nullptr) memset(s, 0, sizeof(*s));
    return D3D_OK;
  }
  STDMETHOD(DrawPrimitiveStrided)(D3DPRIMITIVETYPE, DWORD,
                                  LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD,
                                  DWORD) override {
    return NotMade("IDirect3DDevice7::DrawPrimitiveStrided");
  }
  STDMETHOD(DrawIndexedPrimitiveStrided)(D3DPRIMITIVETYPE, DWORD,
                                         LPD3DDRAWPRIMITIVESTRIDEDDATA, DWORD,
                                         LPWORD, DWORD, DWORD) override {
    return NotMade("IDirect3DDevice7::DrawIndexedPrimitiveStrided");
  }
  STDMETHOD(DrawPrimitiveVB)(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7, DWORD,
                             DWORD, DWORD) override {
    return NotMade("IDirect3DDevice7::DrawPrimitiveVB");
  }
  STDMETHOD(DrawIndexedPrimitiveVB)(D3DPRIMITIVETYPE, LPDIRECT3DVERTEXBUFFER7,
                                    DWORD, DWORD, LPWORD, DWORD,
                                    DWORD) override {
    return NotMade("IDirect3DDevice7::DrawIndexedPrimitiveVB");
  }
  STDMETHOD(ComputeSphereVisibility)(LPD3DVECTOR, LPD3DVALUE, DWORD, DWORD,
                                     LPDWORD) override {
    return NotMade("IDirect3DDevice7::ComputeSphereVisibility");
  }
  STDMETHOD(GetTexture)(DWORD stage, LPDIRECTDRAWSURFACE7* out) override {
    if (out == nullptr || stage >= kStages) return DDERR_INVALIDPARAMS;
    *out = textures_[stage];
    if (*out != nullptr) (*out)->AddRef();
    return D3D_OK;
  }
  STDMETHOD(SetTexture)(DWORD stage, LPDIRECTDRAWSURFACE7 s) override {
    if (stage >= kStages) return DDERR_INVALIDPARAMS;
    if (recording_ != nullptr) {
      recording_->texture[stage] = s;
      recording_->texture_set[stage] = true;
    }
    if (s != nullptr) s->AddRef();
    if (textures_[stage] != nullptr) textures_[stage]->Release();
    textures_[stage] = s;
    VkSceneTexture(stage, s);
    return D3D_OK;
  }
  STDMETHOD(GetTextureStageState)(DWORD stage, D3DTEXTURESTAGESTATETYPE type,
                                  LPDWORD out) override {
    if (out == nullptr || stage >= kStages ||
        static_cast<uint32_t>(type) >= kStageStates) {
      return DDERR_INVALIDPARAMS;
    }
    *out = ts_[stage][type];
    return D3D_OK;
  }
  STDMETHOD(SetTextureStageState)(DWORD stage, D3DTEXTURESTAGESTATETYPE type,
                                  DWORD value) override {
    if (stage >= kStages || static_cast<uint32_t>(type) >= kStageStates) {
      return DDERR_INVALIDPARAMS;
    }
    if (recording_ != nullptr) {
      recording_->ts[stage][type] = value;
      recording_->ts_set[stage][type] = true;
    }
    ts_[stage][type] = value;
    // In Direct3D 7 D3DTSS_ADDRESS sets the mode of u and of v.
    if (type == D3DTSS_ADDRESS) {
      ts_[stage][D3DTSS_ADDRESSU] = value;
      ts_[stage][D3DTSS_ADDRESSV] = value;
    }
    VkSceneStageState(stage, type, value);
    return D3D_OK;
  }
  STDMETHOD(ValidateDevice)(LPDWORD passes) override {
    if (passes != nullptr) *passes = 1;
    return D3D_OK;
  }
  STDMETHOD(ApplyStateBlock)(DWORD handle) override {
    Block* b = BlockOf(handle);
    if (b == nullptr) return D3DERR_INVALIDSTATEBLOCK;
    for (uint32_t i = 0; i < kStates; ++i) {
      if (b->rs_set[i]) {
        SetRenderState(static_cast<D3DRENDERSTATETYPE>(i), b->rs[i]);
      }
    }
    for (uint32_t s = 0; s < kStages; ++s) {
      for (uint32_t i = 0; i < kStageStates; ++i) {
        if (b->ts_set[s][i]) {
          SetTextureStageState(s, static_cast<D3DTEXTURESTAGESTATETYPE>(i),
                               b->ts[s][i]);
        }
      }
      if (b->texture_set[s]) SetTexture(s, b->texture[s]);
    }
    return D3D_OK;
  }
  STDMETHOD(CaptureStateBlock)(DWORD handle) override {
    Block* b = BlockOf(handle);
    if (b == nullptr) return D3DERR_INVALIDSTATEBLOCK;
    for (uint32_t i = 0; i < kStates; ++i) {
      if (b->rs_set[i]) b->rs[i] = rs_[i];
    }
    for (uint32_t s = 0; s < kStages; ++s) {
      for (uint32_t i = 0; i < kStageStates; ++i) {
        if (b->ts_set[s][i]) b->ts[s][i] = ts_[s][i];
      }
      if (b->texture_set[s]) b->texture[s] = textures_[s];
    }
    return D3D_OK;
  }
  STDMETHOD(DeleteStateBlock)(DWORD handle) override {
    Block* b = BlockOf(handle);
    if (b == nullptr) return D3DERR_INVALIDSTATEBLOCK;
    delete b;
    blocks_[handle - 1] = nullptr;
    return D3D_OK;
  }
  STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE, LPDWORD out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    Block* b = new Block();
    memset(b, 0, sizeof(*b));
    // A block of all the states: each render state and stage state.
    for (uint32_t i = 0; i < kStates; ++i) b->rs_set[i] = true;
    for (uint32_t s = 0; s < kStages; ++s) {
      for (uint32_t i = 0; i < kStageStates; ++i) b->ts_set[s][i] = true;
      b->texture_set[s] = true;
    }
    *out = AddBlock(b);
    return CaptureStateBlock(*out);
  }
  STDMETHOD(Load)(LPDIRECTDRAWSURFACE7 to, LPPOINT at,
                  LPDIRECTDRAWSURFACE7 from, LPRECT from_rect,
                  DWORD) override {
    if (to == nullptr || from == nullptr) return DDERR_INVALIDPARAMS;
    return to->BltFast(at != nullptr ? at->x : 0, at != nullptr ? at->y : 0,
                       from, from_rect, DDBLTFAST_WAIT);
  }
  STDMETHOD(LightEnable)(DWORD i, BOOL on) override {
    if (i >= kLights) return DDERR_INVALIDPARAMS;
    light_on_[i] = on != FALSE;
    return D3D_OK;
  }
  STDMETHOD(GetLightEnable)(DWORD i, BOOL* out) override {
    if (out == nullptr || i >= kLights) return DDERR_INVALIDPARAMS;
    *out = light_on_[i] ? TRUE : FALSE;
    return D3D_OK;
  }
  STDMETHOD(SetClipPlane)(DWORD i, D3DVALUE* p) override {
    if (p == nullptr || i >= 6) return DDERR_INVALIDPARAMS;
    memcpy(clip_[i], p, sizeof(clip_[i]));
    return D3D_OK;
  }
  STDMETHOD(GetClipPlane)(DWORD i, D3DVALUE* p) override {
    if (p == nullptr || i >= 6) return DDERR_INVALIDPARAMS;
    memcpy(p, clip_[i], sizeof(clip_[i]));
    return D3D_OK;
  }
  STDMETHOD(GetInfo)(DWORD, LPVOID, DWORD) override { return S_FALSE; }

 private:
  static const uint32_t kStates = 256;
  static const uint32_t kStages = 8;
  static const uint32_t kStageStates = 32;
  static const uint32_t kLights = 32;
  struct Block {
    DWORD rs[kStates];
    bool rs_set[kStates];
    DWORD ts[kStages][kStageStates];
    bool ts_set[kStages][kStageStates];
    IDirectDrawSurface7* texture[kStages];
    bool texture_set[kStages];
  };

  void DrawTriangles(D3DPRIMITIVETYPE type, DWORD fvf, const void* vertices,
            DWORD count, const WORD* indices, DWORD index_count) {
    // The renderer draws triangles. A draw of points or lines of XYZRHW
    // vertices becomes a list of triangles of 1 pixel across, with no cull,
    // because Direct3D 7 culls no point and no line.
    if (type != D3DPT_TRIANGLELIST && type != D3DPT_TRIANGLESTRIP &&
        type != D3DPT_TRIANGLEFAN) {
      const uint32_t stride = vk_prim_math::FvfStride(fvf);
      if (!g_lines || (fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW ||
          stride == 0 ||
          (!vk_prim_math::IsPoints(type) && !vk_prim_math::IsLines(type))) {
        NotMade("a draw of points or lines", D3D_OK);
        return;
      }
      const uint32_t prims = vk_prim_math::PrimCount(
          type, indices != nullptr ? index_count : count);
      if (prims == 0) return;
      const size_t bytes = static_cast<size_t>(prims) * 6 * stride;
      if (bytes > prim_room_) {
        delete[] prim_;
        prim_room_ = bytes * 2;
        prim_ = new uint8_t[prim_room_];
      }
      const uint32_t made = vk_prim_math::Expand(
          type, static_cast<const uint8_t*>(vertices), count, indices,
          index_count, stride, prim_, prims * 6);
      if (made == 0) return;
      InterlockedIncrement(&g_line_draws);
      InterlockedExchangeAdd(&g_line_prims, static_cast<LONG>(made / 6));
      const DWORD cull = rs_[D3DRENDERSTATE_CULLMODE];
      if (cull != D3DCULL_NONE) {
        VkSceneRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
      }
      VkSceneDraw(D3DPT_TRIANGLELIST, fvf, prim_, made, nullptr, 0);
      if (cull != D3DCULL_NONE) {
        VkSceneRenderState(D3DRENDERSTATE_CULLMODE, cull);
      }
      return;
    }
    VkSceneDraw(type, fvf, vertices, count, indices, index_count);
  }
  D3DMATRIX* Transform(D3DTRANSFORMSTATETYPE type) {
    const uint32_t t = static_cast<uint32_t>(type);
    if (t >= 1 && t <= 3) return &transforms_[t];  // world (old), view, proj
    if (t >= D3DTRANSFORMSTATE_WORLD1 && t <= D3DTRANSFORMSTATE_WORLD3) {
      return &world_[t - D3DTRANSFORMSTATE_WORLD1 + 1];
    }
    if (t >= D3DTRANSFORMSTATE_TEXTURE0 && t <= D3DTRANSFORMSTATE_TEXTURE7) {
      return &texture_m_[t - D3DTRANSFORMSTATE_TEXTURE0];
    }
    if (t == 256) return &world_[0];  // D3DTS_WORLDMATRIX(0)
    return nullptr;
  }
  DWORD AddBlock(Block* b) {
    for (int i = 0; i < kMaxBlocks; ++i) {
      if (blocks_[i] == nullptr) {
        blocks_[i] = b;
        return static_cast<DWORD>(i + 1);
      }
    }
    delete b;
    return 0;
  }
  Block* BlockOf(DWORD handle) {
    if (handle == 0 || handle > static_cast<DWORD>(kMaxBlocks)) return nullptr;
    return blocks_[handle - 1];
  }

  LONG refs_ = 1;
  Direct3D* d3d_;
  Surface* target_;
  GUID id_;
  bool in_scene_ = false;
  // The triangles of a draw of points or lines.
  uint8_t* prim_ = nullptr;
  size_t prim_room_ = 0;
  DWORD rs_[kStates] = {};
  DWORD ts_[kStages][kStageStates] = {};
  IDirectDrawSurface7* textures_[kStages] = {};
  D3DVIEWPORT7 viewport_ = {};
  D3DMATERIAL7 material_ = {};
  D3DLIGHT7 lights_[kLights] = {};
  bool light_on_[kLights] = {};
  D3DMATRIX transforms_[4] = {};
  D3DMATRIX world_[4] = {};
  D3DMATRIX texture_m_[8] = {};
  float clip_[6][4] = {};
  static const int kMaxBlocks = 64;
  Block* blocks_[kMaxBlocks] = {};
  Block* recording_ = nullptr;
};

// ---------------------------------------------------------------------------
// DIRECT3D 7.

class Direct3D : public IDirect3D7 {
 public:
  explicit Direct3D(Draw* draw) : draw_(draw) {}

  STDMETHOD(QueryInterface)(REFIID id, LPVOID* out) override;
  STDMETHOD_(ULONG, AddRef)() override;
  STDMETHOD_(ULONG, Release)() override;
  STDMETHOD(EnumDevices)(LPD3DENUMDEVICESCALLBACK7 callback,
                         LPVOID context) override {
    if (callback == nullptr) return DDERR_INVALIDPARAMS;
    struct One {
      const GUID* id;
      const char* description;
      const char* name;
      DWORD extra_caps;
    };
    const One devices[3] = {
        {&IID_IDirect3DRGBDevice, "Microsoft Direct3D RGB Software Emulation",
         "RGB Emulation", 0},
        {&IID_IDirect3DHALDevice,
         "Microsoft Direct3D Hardware acceleration through Direct3D HAL",
         "Direct3D HAL", 0},
        {&IID_IDirect3DTnLHalDevice,
         "Microsoft Direct3D Hardware Transform and Lighting acceleration "
         "capable device",
         "Direct3D T&L HAL", D3DDEVCAPS_HWTRANSFORMANDLIGHT}};
    for (const One& d : devices) {
      D3DDEVICEDESC7 desc = DeviceDesc(*d.id);
      desc.dwDevCaps |= d.extra_caps;
      if (d.id == &IID_IDirect3DRGBDevice) {
        desc.dwDevCaps &= ~static_cast<DWORD>(D3DDEVCAPS_HWRASTERIZATION);
      }
      char description[128], name[64];
      strcpy_s(description, d.description);
      strcpy_s(name, d.name);
      if (callback(description, name, &desc, context) == D3DENUMRET_CANCEL) {
        break;
      }
    }
    return D3D_OK;
  }
  STDMETHOD(CreateDevice)(REFCLSID id, LPDIRECTDRAWSURFACE7 target,
                          LPDIRECT3DDEVICE7* out) override {
    if (out == nullptr || target == nullptr) return DDERR_INVALIDPARAMS;
    *out = new Device(this, static_cast<Surface*>(target), id);
    ModLog("vk ddraw     : the game made its device {%08lX-...} on the render "
           "target",
           id.Data1);
    return D3D_OK;
  }
  STDMETHOD(CreateVertexBuffer)(LPD3DVERTEXBUFFERDESC,
                                LPDIRECT3DVERTEXBUFFER7*, DWORD) override {
    return NotMade("IDirect3D7::CreateVertexBuffer");
  }
  STDMETHOD(EnumZBufferFormats)(REFCLSID, LPD3DENUMPIXELFORMATSCALLBACK callback,
                                LPVOID context) override {
    if (callback == nullptr) return DDERR_INVALIDPARAMS;
    for (int i = 0; i < kZFormats; ++i) {
      DDPIXELFORMAT p = ZFormat(i);
      if (callback(&p, context) == D3DENUMRET_CANCEL) break;
    }
    return D3D_OK;
  }
  STDMETHOD(EvictManagedTextures)() override { return D3D_OK; }

 private:
  Draw* draw_;
};

// ---------------------------------------------------------------------------
// DIRECTDRAW 7.

// The IDirectDraw2 of the DirectDraw object (DrawTwo, below).
IDirectDraw2* TwoOf();

class Draw : public IDirectDraw7 {
 public:
  Draw() : d3d_(this) {}

  STDMETHOD(QueryInterface)(REFIID id, LPVOID* out) override {
    if (out == nullptr) return E_POINTER;
    if (IsEqualGUID(id, IID_IUnknown) || IsEqualGUID(id, IID_IDirectDraw7)) {
      *out = static_cast<IDirectDraw7*>(this);
      AddRef();
      return S_OK;
    }
    if (IsEqualGUID(id, IID_IDirect3D7)) {
      *out = &d3d_;
      AddRef();
      return S_OK;
    }
    if (IsEqualGUID(id, IID_IDirectDraw2)) {
      *out = TwoOf();
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    LogInterface("DirectDraw", id);
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    const LONG left = InterlockedDecrement(&refs_);
    // The object stays: its surfaces and its device keep a pointer to it,
    // and the game makes it one time.
    return left;
  }
  STDMETHOD(Compact)() override { return DD_OK; }
  STDMETHOD(CreateClipper)(DWORD, LPDIRECTDRAWCLIPPER* out,
                           IUnknown*) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = new Clipper();
    return DD_OK;
  }
  STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*,
                           IUnknown*) override {
    return NotMade("IDirectDraw7::CreatePalette");
  }
  STDMETHOD(CreateSurface)(LPDDSURFACEDESC2 desc, LPDIRECTDRAWSURFACE7* out,
                           IUnknown*) override;
  STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE7,
                              LPDIRECTDRAWSURFACE7*) override {
    return NotMade("IDirectDraw7::DuplicateSurface");
  }
  STDMETHOD(EnumDisplayModes)(DWORD, LPDDSURFACEDESC2, LPVOID context,
                              LPDDENUMMODESCALLBACK2 callback) override {
    if (callback == nullptr) return DDERR_INVALIDPARAMS;
    DEVMODEA mode = {};
    mode.dmSize = sizeof(mode);
    DWORD last_w = 0, last_h = 0, last_bits = 0;
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &mode); ++i) {
      if (mode.dmBitsPerPel != 32 && mode.dmBitsPerPel != 16) continue;
      if (mode.dmPelsWidth == last_w && mode.dmPelsHeight == last_h &&
          mode.dmBitsPerPel == last_bits) {
        continue;
      }
      last_w = mode.dmPelsWidth;
      last_h = mode.dmPelsHeight;
      last_bits = mode.dmBitsPerPel;
      DDSURFACEDESC2 d = {};
      d.dwSize = sizeof(d);
      d.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_PITCH |
                  DDSD_REFRESHRATE;
      d.dwWidth = mode.dmPelsWidth;
      d.dwHeight = mode.dmPelsHeight;
      d.dwRefreshRate = 0;
      d.ddpfPixelFormat = mode.dmBitsPerPel == 32
                              ? DisplayFormat()
                              : Rgb(16, 0xF800, 0x07E0, 0x001F, 0);
      d.lPitch = static_cast<LONG>(d.dwWidth * mode.dmBitsPerPel / 8);
      if (callback(&d, context) == DDENUMRET_CANCEL) break;
    }
    return DD_OK;
  }
  STDMETHOD(EnumSurfaces)(DWORD, LPDDSURFACEDESC2, LPVOID,
                          LPDDENUMSURFACESCALLBACK7) override {
    return NotMade("IDirectDraw7::EnumSurfaces");
  }
  STDMETHOD(FlipToGDISurface)() override { return DD_OK; }
  STDMETHOD(GetCaps)(LPDDCAPS driver, LPDDCAPS hel) override {
    // The caps of dxwrapper (ddraw_census=1).
    LPDDCAPS const both[2] = {driver, hel};
    for (LPDDCAPS c : both) {
      if (c == nullptr) continue;
      const DWORD size = c->dwSize;
      memset(c, 0, size);
      c->dwSize = size;
      c->dwCaps = 0xF4CBAF41;
      c->dwCaps2 = 0x20CA9981;
      c->dwVidMemTotal = 0x7EFFFFFF;
      c->dwVidMemFree = 0x7E7FFFFF;
      c->ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE |
                          DDSCAPS_ZBUFFER | DDSCAPS_MIPMAP |
                          DDSCAPS_OFFSCREENPLAIN | DDSCAPS_PRIMARYSURFACE |
                          DDSCAPS_VIDEOMEMORY | DDSCAPS_SYSTEMMEMORY;
    }
    return DD_OK;
  }
  STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC2 out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    DEVMODEA mode = {};
    mode.dmSize = sizeof(mode);
    EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode);
    memset(out, 0, sizeof(*out));
    out->dwSize = sizeof(*out);
    out->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_PITCH |
                   DDSD_REFRESHRATE;
    out->dwWidth = mode.dmPelsWidth;
    out->dwHeight = mode.dmPelsHeight;
    out->lPitch = static_cast<LONG>(out->dwWidth * 4);
    out->dwRefreshRate = mode.dmDisplayFrequency;
    out->ddpfPixelFormat = DisplayFormat();
    return DD_OK;
  }
  STDMETHOD(GetFourCCCodes)(LPDWORD count, LPDWORD codes) override {
    if (count == nullptr) return DDERR_INVALIDPARAMS;
    const DWORD all[5] = {kDxt1, kDxt2, kDxt3, kDxt4, kDxt5};
    if (codes != nullptr) {
      for (DWORD i = 0; i < *count && i < 5; ++i) codes[i] = all[i];
    }
    *count = 5;
    return DD_OK;
  }
  STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE7* out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    *out = primary_;
    if (primary_ == nullptr) return DDERR_NOTFOUND;
    primary_->AddRef();
    return DD_OK;
  }
  STDMETHOD(GetMonitorFrequency)(LPDWORD out) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    DEVMODEA mode = {};
    mode.dmSize = sizeof(mode);
    EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode);
    *out = mode.dmDisplayFrequency;
    return DD_OK;
  }
  STDMETHOD(GetScanLine)(LPDWORD out) override {
    if (out != nullptr) *out = 0;
    return DD_OK;
  }
  STDMETHOD(GetVerticalBlankStatus)(LPBOOL out) override {
    if (out != nullptr) *out = TRUE;
    return DD_OK;
  }
  STDMETHOD(Initialize)(GUID*) override { return DDERR_ALREADYINITIALIZED; }
  STDMETHOD(RestoreDisplayMode)() override { return DD_OK; }
  STDMETHOD(SetCooperativeLevel)(HWND window, DWORD flags) override {
    window_ = window;
    ModLog("vk ddraw     : SetCooperativeLevel 0x%lX", flags);
    return DD_OK;
  }
  STDMETHOD(SetDisplayMode)(DWORD w, DWORD h, DWORD bits, DWORD, DWORD) override {
    ModLog("vk ddraw     : SetDisplayMode %lux%lu, %lu bits: the game stays "
           "in its window",
           w, h, bits);
    return DD_OK;
  }
  STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE) override { return DD_OK; }
  STDMETHOD(GetAvailableVidMem)(LPDDSCAPS2, LPDWORD total,
                                LPDWORD free) override {
    if (total != nullptr) *total = 0x7EFFFFFF;
    if (free != nullptr) *free = 0x7E7FFFFF;
    return DD_OK;
  }
  STDMETHOD(GetSurfaceFromDC)(HDC, LPDIRECTDRAWSURFACE7*) override {
    return NotMade("IDirectDraw7::GetSurfaceFromDC");
  }
  STDMETHOD(RestoreAllSurfaces)() override { return DD_OK; }
  STDMETHOD(TestCooperativeLevel)() override { return DD_OK; }
  STDMETHOD(GetDeviceIdentifier)(LPDDDEVICEIDENTIFIER2 out, DWORD) override {
    if (out == nullptr) return DDERR_INVALIDPARAMS;
    memset(out, 0, sizeof(*out));
    // The game asks before Vulkan starts. The answer has the ids of the
    // answer of dxwrapper on the PC of the census, and the name of the GPU
    // of Vulkan when Vulkan has started.
    strcpy_s(out->szDriver, "vulkan-1.dll");
    strcpy_s(out->szDescription, "Black & White VR Mod on Vulkan");
    if (vk::Started()) strcpy_s(out->szDescription, vk::Get().gpu);
    out->dwVendorId = 0x1002;
    out->dwDeviceId = 0x744C;
    out->liDriverVersion.HighPart = 0x00200000;
    out->liDriverVersion.LowPart = 0x79400FEC;
    return DD_OK;
  }
  STDMETHOD(StartModeTest)(LPSIZE, DWORD, DWORD) override {
    return NotMade("IDirectDraw7::StartModeTest");
  }
  STDMETHOD(EvaluateMode)(DWORD, DWORD*) override {
    return NotMade("IDirectDraw7::EvaluateMode");
  }

  HWND window() const { return window_; }
  void SetPrimary(Surface* s) { primary_ = s; }
  void ForgetPrimary(Surface* s) {
    if (primary_ == s) primary_ = nullptr;
  }

 private:
  LONG refs_ = 1;
  Direct3D d3d_;
  HWND window_ = nullptr;
  Surface* primary_ = nullptr;  // no reference
};

Draw* g_draw = nullptr;

// THE OBJECT OF DirectDrawCreate (IDirectDraw of DirectX 1): the game asks
// it for IDirectDraw7 and releases it.
class DrawOld : public IDirectDraw {
 public:
  STDMETHOD(QueryInterface)(REFIID id, LPVOID* out) override {
    if (out == nullptr) return E_POINTER;
    if (IsEqualGUID(id, IID_IUnknown) || IsEqualGUID(id, IID_IDirectDraw)) {
      *out = this;
      AddRef();
      return S_OK;
    }
    if (IsEqualGUID(id, IID_IDirectDraw7)) {
      return g_draw->QueryInterface(id, out);
    }
    *out = nullptr;
    LogInterface("the IDirectDraw of DirectDrawCreate", id);
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    const LONG left = InterlockedDecrement(&refs_);
    if (left == 0) delete this;
    return left;
  }
  STDMETHOD(Compact)() override { return DD_OK; }
  STDMETHOD(CreateClipper)(DWORD, LPDIRECTDRAWCLIPPER*, IUnknown*) override {
    return NotMade("IDirectDraw::CreateClipper");
  }
  STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*,
                           IUnknown*) override {
    return NotMade("IDirectDraw::CreatePalette");
  }
  STDMETHOD(CreateSurface)(LPDDSURFACEDESC, LPDIRECTDRAWSURFACE*,
                           IUnknown*) override {
    return NotMade("IDirectDraw::CreateSurface");
  }
  STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE,
                              LPDIRECTDRAWSURFACE*) override {
    return NotMade("IDirectDraw::DuplicateSurface");
  }
  STDMETHOD(EnumDisplayModes)(DWORD, LPDDSURFACEDESC, LPVOID,
                              LPDDENUMMODESCALLBACK) override {
    return NotMade("IDirectDraw::EnumDisplayModes");
  }
  STDMETHOD(EnumSurfaces)(DWORD, LPDDSURFACEDESC, LPVOID,
                          LPDDENUMSURFACESCALLBACK) override {
    return NotMade("IDirectDraw::EnumSurfaces");
  }
  STDMETHOD(FlipToGDISurface)() override { return DD_OK; }
  STDMETHOD(GetCaps)(LPDDCAPS a, LPDDCAPS b) override {
    return g_draw->GetCaps(a, b);
  }
  STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC) override {
    return NotMade("IDirectDraw::GetDisplayMode");
  }
  STDMETHOD(GetFourCCCodes)(LPDWORD a, LPDWORD b) override {
    return g_draw->GetFourCCCodes(a, b);
  }
  STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE*) override {
    return NotMade("IDirectDraw::GetGDISurface");
  }
  STDMETHOD(GetMonitorFrequency)(LPDWORD out) override {
    return g_draw->GetMonitorFrequency(out);
  }
  STDMETHOD(GetScanLine)(LPDWORD out) override {
    return g_draw->GetScanLine(out);
  }
  STDMETHOD(GetVerticalBlankStatus)(LPBOOL out) override {
    return g_draw->GetVerticalBlankStatus(out);
  }
  STDMETHOD(Initialize)(GUID*) override { return DDERR_ALREADYINITIALIZED; }
  STDMETHOD(RestoreDisplayMode)() override { return DD_OK; }
  STDMETHOD(SetCooperativeLevel)(HWND window, DWORD flags) override {
    return g_draw->SetCooperativeLevel(window, flags);
  }
  STDMETHOD(SetDisplayMode)(DWORD w, DWORD h, DWORD bits) override {
    return g_draw->SetDisplayMode(w, h, bits, 0, 0);
  }
  STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE) override { return DD_OK; }

 private:
  LONG refs_ = 1;
};

// THE IDirectDraw2 OF THE DirectDraw OBJECT. LHScreen::GetAvailableVidMem
// asks for it and calls its GetAvailableVidMem, with no test of the result.
class DrawTwo : public IDirectDraw2 {
 public:
  STDMETHOD(QueryInterface)(REFIID id, LPVOID* out) override {
    return g_draw->QueryInterface(id, out);
  }
  STDMETHOD_(ULONG, AddRef)() override { return g_draw->AddRef(); }
  STDMETHOD_(ULONG, Release)() override { return g_draw->Release(); }
  STDMETHOD(Compact)() override { return DD_OK; }
  STDMETHOD(CreateClipper)(DWORD, LPDIRECTDRAWCLIPPER*, IUnknown*) override {
    return NotMade("IDirectDraw2::CreateClipper");
  }
  STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*,
                           IUnknown*) override {
    return NotMade("IDirectDraw2::CreatePalette");
  }
  STDMETHOD(CreateSurface)(LPDDSURFACEDESC, LPDIRECTDRAWSURFACE*,
                           IUnknown*) override {
    return NotMade("IDirectDraw2::CreateSurface");
  }
  STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE,
                              LPDIRECTDRAWSURFACE*) override {
    return NotMade("IDirectDraw2::DuplicateSurface");
  }
  STDMETHOD(EnumDisplayModes)(DWORD, LPDDSURFACEDESC, LPVOID,
                              LPDDENUMMODESCALLBACK) override {
    return NotMade("IDirectDraw2::EnumDisplayModes");
  }
  STDMETHOD(EnumSurfaces)(DWORD, LPDDSURFACEDESC, LPVOID,
                          LPDDENUMSURFACESCALLBACK) override {
    return NotMade("IDirectDraw2::EnumSurfaces");
  }
  STDMETHOD(FlipToGDISurface)() override { return DD_OK; }
  STDMETHOD(GetCaps)(LPDDCAPS a, LPDDCAPS b) override {
    return g_draw->GetCaps(a, b);
  }
  STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC) override {
    return NotMade("IDirectDraw2::GetDisplayMode");
  }
  STDMETHOD(GetFourCCCodes)(LPDWORD a, LPDWORD b) override {
    return g_draw->GetFourCCCodes(a, b);
  }
  STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE*) override {
    return NotMade("IDirectDraw2::GetGDISurface");
  }
  STDMETHOD(GetMonitorFrequency)(LPDWORD out) override {
    return g_draw->GetMonitorFrequency(out);
  }
  STDMETHOD(GetScanLine)(LPDWORD out) override {
    return g_draw->GetScanLine(out);
  }
  STDMETHOD(GetVerticalBlankStatus)(LPBOOL out) override {
    return g_draw->GetVerticalBlankStatus(out);
  }
  STDMETHOD(Initialize)(GUID*) override { return DDERR_ALREADYINITIALIZED; }
  STDMETHOD(RestoreDisplayMode)() override { return DD_OK; }
  STDMETHOD(SetCooperativeLevel)(HWND window, DWORD flags) override {
    return g_draw->SetCooperativeLevel(window, flags);
  }
  STDMETHOD(SetDisplayMode)(DWORD w, DWORD h, DWORD bits, DWORD rate,
                            DWORD flags) override {
    return g_draw->SetDisplayMode(w, h, bits, rate, flags);
  }
  STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE) override { return DD_OK; }
  STDMETHOD(GetAvailableVidMem)(LPDDSCAPS, LPDWORD total,
                                LPDWORD free) override {
    return g_draw->GetAvailableVidMem(nullptr, total, free);
  }
};
DrawTwo g_draw_two;
IDirectDraw2* TwoOf() { return &g_draw_two; }

// ---------------------------------------------------------------------------
// The methods of the surface.

Surface::Surface(Draw* draw, const DDSURFACEDESC2& desc)
    : draw_(draw), desc_(desc) {
  desc_.dwSize = sizeof(desc_);
  desc_.lpSurface = nullptr;
  if (!(desc_.dwFlags & DDSD_PIXELFORMAT) ||
      desc_.ddpfPixelFormat.dwSize == 0) {
    if (desc_.ddsCaps.dwCaps & DDSCAPS_ZBUFFER) {
      desc_.ddpfPixelFormat = ZFormat(2);
    } else {
      desc_.ddpfPixelFormat = DisplayFormat();
    }
    desc_.dwFlags |= DDSD_PIXELFORMAT;
  }
  // The primary has the size of the display.
  if (desc_.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) {
    DEVMODEA mode = {};
    mode.dmSize = sizeof(mode);
    EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode);
    desc_.dwWidth = mode.dmPelsWidth;
    desc_.dwHeight = mode.dmPelsHeight;
    desc_.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT;
    desc_.ddsCaps.dwCaps |= DDSCAPS_VISIBLE | DDSCAPS_FRONTBUFFER;
  }
  // The memory of the GPU is local memory, as dxwrapper says.
  if (desc_.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) {
    desc_.ddsCaps.dwCaps |= DDSCAPS_LOCALVIDMEM;
  }
  if (!(desc_.ddsCaps.dwCaps & (DDSCAPS_VIDEOMEMORY | DDSCAPS_SYSTEMMEMORY)) &&
      !(desc_.ddsCaps.dwCaps2 & (DDSCAPS2_TEXTUREMANAGE |
                                 DDSCAPS2_D3DTEXTUREMANAGE))) {
    desc_.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
  }
  const DDPIXELFORMAT& p = desc_.ddpfPixelFormat;
  const uint32_t bpp = PixelBytes(p);
  if (bpp != 0) {
    pitch_ = static_cast<LONG>((desc_.dwWidth * bpp + 3) & ~3u);
    bytes_ = static_cast<DWORD>(pitch_) * desc_.dwHeight;
  } else if (p.dwFlags & DDPF_FOURCC) {
    const DWORD bw = desc_.dwWidth > 4 ? (desc_.dwWidth + 3) / 4 : 1;
    const DWORD bh = desc_.dwHeight > 4 ? (desc_.dwHeight + 3) / 4 : 1;
    pitch_ = static_cast<LONG>(bw * BlockBytes(p.dwFourCC));
    bytes_ = static_cast<DWORD>(pitch_) * bh;
  }
  InterlockedIncrement(&g_surfaces);
  InterlockedIncrement(&g_surfaces_made);
}

Surface::~Surface() {
  if (IsPrimary() && draw_ != nullptr) draw_->ForgetPrimary(this);
  if (next_ != nullptr) next_->Release();
  if (z_ != nullptr) z_->Release();
  if (clipper_ != nullptr) clipper_->Release();
  if (bits_ != nullptr) VirtualFree(bits_, 0, MEM_RELEASE);
  InterlockedDecrement(&g_surfaces);
}

HRESULT Surface::QueryInterface(REFIID id, LPVOID* out) {
  if (out == nullptr) return E_POINTER;
  if (IsEqualGUID(id, IID_IUnknown) ||
      IsEqualGUID(id, IID_IDirectDrawSurface7)) {
    *out = this;
    AddRef();
    return S_OK;
  }
  if (IsEqualGUID(id, IID_IDirect3DTexture2)) {
    *out = &face_;
    AddRef();
    return S_OK;
  }
  *out = nullptr;
  LogInterface("a surface", id);
  return E_NOINTERFACE;
}

HRESULT TextureFace::QueryInterface(REFIID id, LPVOID* out) {
  return owner_->QueryInterface(id, out);
}
ULONG TextureFace::AddRef() { return owner_->AddRef(); }
ULONG TextureFace::Release() { return owner_->Release(); }

// Each level of the other texture into the level of this one with its size.
HRESULT TextureFace::Load(LPDIRECT3DTEXTURE2 from_face) {
  if (from_face == nullptr) return DDERR_INVALIDPARAMS;
  Surface* from = static_cast<TextureFace*>(from_face)->owner();
  Surface* to = owner_;
  while (to != nullptr && from != nullptr) {
    // The level of the source with the size of this level.
    Surface* src = from;
    while (src != nullptr && src->desc().dwWidth > to->desc().dwWidth) {
      src = src->next();
    }
    if (src == nullptr) break;
    const HRESULT hr = to->Blt(nullptr, src, nullptr, DDBLT_WAIT, nullptr);
    if (FAILED(hr)) return hr;
    from = src->next();
    to = to->next();
  }
  return D3D_OK;
}

HRESULT Surface::GetDDInterface(LPVOID* out) {
  if (out == nullptr) return DDERR_INVALIDPARAMS;
  *out = static_cast<IDirectDraw7*>(draw_);
  draw_->AddRef();
  return DD_OK;
}

HRESULT Surface::Lock(LPRECT rect, LPDDSURFACEDESC2 out, DWORD flags,
                      HANDLE) {
  if (out == nullptr) return DDERR_INVALIDPARAMS;
  uint8_t* bits = Pixels();
  if (bits == nullptr) return DDERR_OUTOFMEMORY;
  if ((desc_.ddsCaps.dwCaps & DDSCAPS_3DDEVICE) &&
      (flags & DDLOCK_READONLY) == 0) {
    LogTargetWrite("Lock", rect, flags, _ReturnAddress());
  }
  FillDesc(out);
  out->dwFlags |= DDSD_LPSURFACE;
  const uint32_t bpp = PixelBytes(desc_.ddpfPixelFormat);
  if (rect != nullptr && bpp != 0) {
    bits += rect->top * pitch_ + rect->left * static_cast<LONG>(bpp);
  }
  out->lpSurface = bits;
  locked_write_ = (flags & DDLOCK_READONLY) == 0;
  return DD_OK;
}

HRESULT Surface::Unlock(LPRECT) {
  // The renderer reads a texture again at its next use when the game wrote
  // it.
  if (locked_write_) VkSceneSurfaceWritten(this);
  locked_write_ = false;
  return DD_OK;
}

// THE DXT BLOCKS: a block of 4 by 4 pixels as RGBA8 (bytes R, G, B, A).
void Rgb565(uint16_t c, uint8_t out[4]) {
  const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  out[0] = static_cast<uint8_t>((r * 255 + 15) / 31);
  out[1] = static_cast<uint8_t>((g * 255 + 31) / 63);
  out[2] = static_cast<uint8_t>((b * 255 + 15) / 31);
  out[3] = 255;
}

void DecodeColorBlock(const uint8_t* block, bool dxt1, uint8_t out[16][4]) {
  const uint16_t c0 = static_cast<uint16_t>(block[0] | block[1] << 8);
  const uint16_t c1 = static_cast<uint16_t>(block[2] | block[3] << 8);
  uint8_t colors[4][4];
  Rgb565(c0, colors[0]);
  Rgb565(c1, colors[1]);
  if (!dxt1 || c0 > c1) {
    for (int k = 0; k < 3; ++k) {
      colors[2][k] = static_cast<uint8_t>((2 * colors[0][k] + colors[1][k] + 1) / 3);
      colors[3][k] = static_cast<uint8_t>((colors[0][k] + 2 * colors[1][k] + 1) / 3);
    }
    colors[2][3] = colors[3][3] = 255;
  } else {
    for (int k = 0; k < 3; ++k) {
      colors[2][k] = static_cast<uint8_t>((colors[0][k] + colors[1][k]) / 2);
      colors[3][k] = 0;
    }
    colors[2][3] = 255;
    colors[3][3] = 0;
  }
  const uint32_t bits =
      block[4] | block[5] << 8 | block[6] << 16 | static_cast<uint32_t>(block[7]) << 24;
  for (int i = 0; i < 16; ++i) {
    memcpy(out[i], colors[(bits >> (2 * i)) & 3], 4);
  }
}

// One block of a DXT format as 16 RGBA8 pixels, row by row.
void DecodeBlock(DWORD fourcc, const uint8_t* block, uint8_t out[16][4]) {
  if (fourcc == kDxt1) {
    DecodeColorBlock(block, true, out);
    return;
  }
  DecodeColorBlock(block + 8, false, out);
  if (fourcc == kDxt2 || fourcc == kDxt3) {
    for (int i = 0; i < 16; ++i) {
      const uint8_t a4 = (block[i / 2] >> ((i & 1) * 4)) & 15;
      out[i][3] = static_cast<uint8_t>(a4 * 17);
    }
    return;
  }
  // DXT4 and DXT5: two alphas and 3 bits for each pixel.
  uint8_t alphas[8];
  alphas[0] = block[0];
  alphas[1] = block[1];
  if (alphas[0] > alphas[1]) {
    for (int k = 1; k < 7; ++k) {
      alphas[k + 1] =
          static_cast<uint8_t>(((7 - k) * alphas[0] + k * alphas[1] + 3) / 7);
    }
  } else {
    for (int k = 1; k < 5; ++k) {
      alphas[k + 1] =
          static_cast<uint8_t>(((5 - k) * alphas[0] + k * alphas[1] + 2) / 5);
    }
    alphas[6] = 0;
    alphas[7] = 255;
  }
  uint64_t bits = 0;
  for (int k = 0; k < 6; ++k) bits |= static_cast<uint64_t>(block[2 + k]) << (8 * k);
  for (int i = 0; i < 16; ++i) out[i][3] = alphas[(bits >> (3 * i)) & 7];
}

// RGBA8 to a pixel of a format of 16 or 32 bits: for each channel the value
// of each of the 256 levels, made one time for each Blt.
struct Packer {
  uint32_t value[4][256];
  explicit Packer(const DDPIXELFORMAT& p) {
    const uint32_t masks[4] = {
        p.dwRBitMask, p.dwGBitMask, p.dwBBitMask,
        (p.dwFlags & DDPF_ALPHAPIXELS) ? p.dwRGBAlphaBitMask : 0u};
    for (int c = 0; c < 4; ++c) {
      int shift = 0, bits = 0;
      vk_texture_math::MaskShape(masks[c], &shift, &bits);
      const uint32_t max = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
      for (uint32_t v = 0; v < 256; ++v) {
        value[c][v] = masks[c] == 0 ? 0u : ((v * max + 127u) / 255u) << shift;
      }
    }
  }
  uint32_t Pack(const uint8_t rgba[4]) const {
    return value[0][rgba[0]] | value[1][rgba[1]] | value[2][rgba[2]] |
           value[3][rgba[3]];
  }
};

void Store(uint8_t* at, uint32_t bpp, uint32_t v) {
  if (bpp == 2) {
    const uint16_t s = static_cast<uint16_t>(v);
    memcpy(at, &s, 2);
  } else if (bpp == 4) {
    memcpy(at, &v, 4);
  } else if (bpp == 3) {
    memcpy(at, &v, 3);
  }
}

uint32_t LoadPixel(const uint8_t* at, uint32_t bpp) {
  uint32_t v = 0;
  memcpy(&v, at, bpp);
  return v;
}

HRESULT Surface::Blt(LPRECT to, LPDIRECTDRAWSURFACE7 from_s, LPRECT from_rect,
                     DWORD flags, LPDDBLTFX fx) {
  // THE COLOR FILL.
  if (flags & DDBLT_COLORFILL) {
    InterlockedIncrement(&g_fills);
    const uint32_t bpp = PixelBytes(desc_.ddpfPixelFormat);
    // The render target and the primary have their pixels on the GPU: the
    // game clears the render target with Clear in each walk.
    if (bpp == 0 || fx == nullptr || IsPrimary() ||
        (desc_.ddsCaps.dwCaps & DDSCAPS_3DDEVICE)) {
      return DD_OK;
    }
    uint8_t* bits = Pixels();
    if (bits == nullptr) return DDERR_OUTOFMEMORY;
    RECT r = {0, 0, static_cast<LONG>(desc_.dwWidth),
              static_cast<LONG>(desc_.dwHeight)};
    if (to != nullptr) r = *to;
    for (LONG y = r.top; y < r.bottom; ++y) {
      for (LONG x = r.left; x < r.right; ++x) {
        Store(bits + y * pitch_ + x * bpp, bpp, fx->dwFillColor);
      }
    }
    VkSceneSurfaceWritten(this);
    return DD_OK;
  }
  Surface* from = static_cast<Surface*>(from_s);
  if (from == nullptr) return NotMade("IDirectDrawSurface7::Blt (no source)");
  // THE PRESENT: the render target to the primary.
  if (IsPrimary()) {
    Present();
    return DD_OK;
  }
  if (desc_.ddsCaps.dwCaps & DDSCAPS_3DDEVICE) {
    LogTargetWrite("Blt", to, flags, _ReturnAddress());
  }
  // A COPY in the memory of the CPU, with a change of the format and a
  // stretch to the rect of the destination (the nearest pixel).
  const DDPIXELFORMAT& sp = from->desc_.ddpfPixelFormat;
  const DDPIXELFORMAT& dp = desc_.ddpfPixelFormat;
  const uint32_t dbpp = PixelBytes(dp);
  const uint32_t sbpp = PixelBytes(sp);
  if (dbpp == 0 && !((dp.dwFlags & DDPF_FOURCC) && (sp.dwFlags & DDPF_FOURCC))) {
    return NotMade("IDirectDrawSurface7::Blt (to a surface of blocks)");
  }
  const uint8_t* src = from->Pixels();
  uint8_t* dst = Pixels();
  if (src == nullptr || dst == nullptr) return DDERR_OUTOFMEMORY;
  RECT sr = {0, 0, static_cast<LONG>(from->desc_.dwWidth),
             static_cast<LONG>(from->desc_.dwHeight)};
  if (from_rect != nullptr) sr = *from_rect;
  RECT dr = {0, 0, static_cast<LONG>(desc_.dwWidth),
             static_cast<LONG>(desc_.dwHeight)};
  if (to != nullptr) dr = *to;
  const LONG dw = dr.right - dr.left, dh = dr.bottom - dr.top;
  const LONG sw = sr.right - sr.left, sh = sr.bottom - sr.top;
  if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return DD_OK;
  // Blocks to blocks of one format: a copy of the bytes.
  if (dbpp == 0) {
    if (sp.dwFourCC != dp.dwFourCC || bytes_ != from->bytes_) {
      return NotMade("IDirectDrawSurface7::Blt (blocks of two formats)");
    }
    memcpy(dst, src, bytes_);
    InterlockedIncrement(&g_copy_blts);
    VkSceneSurfaceWritten(this);
    return DD_OK;
  }
  const bool same = sbpp == dbpp && sp.dwRBitMask == dp.dwRBitMask &&
                    sp.dwGBitMask == dp.dwGBitMask &&
                    sp.dwBBitMask == dp.dwBBitMask &&
                    sp.dwRGBAlphaBitMask == dp.dwRGBAlphaBitMask &&
                    (sp.dwFlags & DDPF_FOURCC) == 0;
  if (same && dw == sw && dh == sh) {
    for (LONG y = 0; y < dh; ++y) {
      memcpy(dst + (dr.top + y) * pitch_ + dr.left * dbpp,
             src + (sr.top + y) * from->pitch_ + sr.left * sbpp,
             static_cast<size_t>(dw) * dbpp);
    }
    InterlockedIncrement(&g_copy_blts);
    VkSceneSurfaceWritten(this);
    return DD_OK;
  }
  const Packer packer(dp);
  const bool blocks = (sp.dwFlags & DDPF_FOURCC) != 0;
  // BLOCKS WITH NO STRETCH: each block is decoded one time, and its 16
  // pixels go to their places.
  if (blocks && dw == sw && dh == sh && (sr.left & 3) == 0 &&
      (sr.top & 3) == 0) {
    const uint32_t block_bytes = BlockBytes(sp.dwFourCC);
    for (LONG by = 0; by < sh; by += 4) {
      const uint8_t* row = src + ((sr.top + by) / 4) * from->pitch_;
      for (LONG bx = 0; bx < sw; bx += 4) {
        uint8_t block[16][4];
        DecodeBlock(sp.dwFourCC, row + ((sr.left + bx) / 4) * block_bytes,
                    block);
        for (LONG y = 0; y < 4 && by + y < dh; ++y) {
          uint8_t* out = dst + (dr.top + by + y) * pitch_ +
                         (dr.left + bx) * static_cast<LONG>(dbpp);
          for (LONG x = 0; x < 4 && bx + x < dw; ++x, out += dbpp) {
            Store(out, dbpp, packer.Pack(block[y * 4 + x]));
          }
        }
      }
    }
    InterlockedIncrement(&g_dxt_blts);
    VkSceneSurfaceWritten(this);
    return DD_OK;
  }
  // THE OTHER COPIES: the nearest pixel of the source, read with the
  // channels of its format (vk_texture_math.h).
  vk_texture_math::Channel cr, cg, cb, ca;
  const uint32_t alpha_mask =
      (sp.dwFlags & DDPF_ALPHAPIXELS) ? sp.dwRGBAlphaBitMask : 0u;
  if (!blocks) {
    vk_texture_math::BuildChannel(sp.dwRBitMask, 0, &cr);
    vk_texture_math::BuildChannel(sp.dwGBitMask, 0, &cg);
    vk_texture_math::BuildChannel(sp.dwBBitMask, 0, &cb);
    vk_texture_math::BuildChannel(alpha_mask, 255, &ca);
  }
  const uint8_t* last_block = nullptr;
  uint8_t block[16][4];
  for (LONG y = 0; y < dh; ++y) {
    const LONG sy = sr.top + y * sh / dh;
    for (LONG x = 0; x < dw; ++x) {
      const LONG sx = sr.left + x * sw / dw;
      uint8_t rgba[4];
      if (blocks) {
        const uint8_t* b = src + (sy / 4) * from->pitch_ +
                           (sx / 4) * BlockBytes(sp.dwFourCC);
        if (b != last_block) {
          DecodeBlock(sp.dwFourCC, b, block);
          last_block = b;
        }
        memcpy(rgba, block[(sy & 3) * 4 + (sx & 3)], 4);
      } else {
        const uint32_t p = LoadPixel(src + sy * from->pitch_ + sx * sbpp, sbpp);
        rgba[0] = static_cast<uint8_t>(
            vk_texture_math::ChannelValue(p, cr, sp.dwRBitMask));
        rgba[1] = static_cast<uint8_t>(
            vk_texture_math::ChannelValue(p, cg, sp.dwGBitMask));
        rgba[2] = static_cast<uint8_t>(
            vk_texture_math::ChannelValue(p, cb, sp.dwBBitMask));
        rgba[3] = static_cast<uint8_t>(
            vk_texture_math::ChannelValue(p, ca, alpha_mask));
      }
      Store(dst + (dr.top + y) * pitch_ + (dr.left + x) * dbpp, dbpp,
            packer.Pack(rgba));
    }
  }
  InterlockedIncrement(blocks ? &g_dxt_blts : &g_copy_blts);
  VkSceneSurfaceWritten(this);
  return DD_OK;
}

HRESULT Surface::BltFast(DWORD x, DWORD y, LPDIRECTDRAWSURFACE7 from,
                         LPRECT from_rect, DWORD) {
  if (from == nullptr) return DDERR_INVALIDPARAMS;
  const Surface* s = static_cast<Surface*>(from);
  RECT sr = {0, 0, static_cast<LONG>(s->desc_.dwWidth),
             static_cast<LONG>(s->desc_.dwHeight)};
  if (from_rect != nullptr) sr = *from_rect;
  RECT dr = {static_cast<LONG>(x), static_cast<LONG>(y),
             static_cast<LONG>(x) + (sr.right - sr.left),
             static_cast<LONG>(y) + (sr.bottom - sr.top)};
  return Blt(&dr, from, &sr, DDBLT_WAIT, nullptr);
}

void Surface::Present() {
  InterlockedIncrement(&g_presents);
  HWND window = clipper_ != nullptr ? clipper_->window() : nullptr;
  if (window == nullptr) window = draw_->window();
  if (window == nullptr) return;
  RECT client;
  if (!GetClientRect(window, &client)) return;
  const uint32_t w = static_cast<uint32_t>(client.right - client.left);
  const uint32_t h = static_cast<uint32_t>(client.bottom - client.top);
  if (w == 0 || h == 0) return;
  // A swapchain of Vulkan on the window (vk_window=1), else the way through
  // the CPU and GDI. GDI can draw into the window only when it has no
  // swapchain.
  if (VkWindowWanted()) {
    if (VkScenePresentWindow(window, w, h)) return;
  }
  VkWindowStop();
  int pitch = 0;
  const uint8_t* pixels = VkScenePresentPixels(w, h, &pitch);
  if (pixels == nullptr) return;
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = static_cast<LONG>(w);
  info.bmiHeader.biHeight = -static_cast<LONG>(h);  // top row first
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  HDC dc = ::GetDC(window);
  if (dc == nullptr) return;
  if (SetDIBitsToDevice(dc, 0, 0, w, h, 0, 0, 0, h, pixels, &info,
                        DIB_RGB_COLORS) != 0) {
    VkFallbackPicture("the window, through the CPU and GDI");
  }
  ::ReleaseDC(window, dc);
}

// ---------------------------------------------------------------------------
// The methods of DirectDraw and Direct3D that need the classes above.

HRESULT Draw::CreateSurface(LPDDSURFACEDESC2 desc, LPDIRECTDRAWSURFACE7* out,
                            IUnknown*) {
  if (desc == nullptr || out == nullptr) return DDERR_INVALIDPARAMS;
  *out = nullptr;
  DDSURFACEDESC2 d = *desc;
  // The levels of a texture with mip levels.
  DWORD levels = 1;
  if (d.ddsCaps.dwCaps & DDSCAPS_MIPMAP) {
    if (d.dwFlags & DDSD_MIPMAPCOUNT) {
      levels = d.dwMipMapCount;
    } else {
      DWORD size = d.dwWidth > d.dwHeight ? d.dwWidth : d.dwHeight;
      levels = 1;
      while (size > 1) {
        size >>= 1;
        ++levels;
      }
    }
    if (levels == 0) levels = 1;
    d.dwFlags |= DDSD_MIPMAPCOUNT;
    d.dwMipMapCount = levels;
  }
  Surface* top = new Surface(this, d);
  if (top->IsPrimary()) SetPrimary(top);
  Surface* last = top;
  for (DWORD i = 1; i < levels; ++i) {
    DDSURFACEDESC2 sub = d;
    sub.dwWidth = d.dwWidth >> i ? d.dwWidth >> i : 1;
    sub.dwHeight = d.dwHeight >> i ? d.dwHeight >> i : 1;
    sub.dwMipMapCount = levels - i;
    sub.ddsCaps.dwCaps2 |= DDSCAPS2_MIPMAPSUBLEVEL;
    Surface* next = new Surface(this, sub);
    last->SetNext(next);
    last = next;
  }
  *out = top;
  return DD_OK;
}

HRESULT Direct3D::QueryInterface(REFIID id, LPVOID* out) {
  if (out == nullptr) return E_POINTER;
  if (IsEqualGUID(id, IID_IUnknown) || IsEqualGUID(id, IID_IDirect3D7)) {
    *out = this;
    AddRef();
    return S_OK;
  }
  return draw_->QueryInterface(id, out);
}

ULONG Direct3D::AddRef() { return draw_->AddRef(); }
ULONG Direct3D::Release() { return draw_->Release(); }

Device::Device(Direct3D* d3d, Surface* target, REFCLSID id)
    : d3d_(d3d), target_(target), id_(id) {
  target_->AddRef();
  d3d_->AddRef();
  // The defaults of Direct3D 7.
  rs_[D3DRENDERSTATE_ZENABLE] = D3DZB_TRUE;
  rs_[D3DRENDERSTATE_FILLMODE] = D3DFILL_SOLID;
  rs_[D3DRENDERSTATE_SHADEMODE] = D3DSHADE_GOURAUD;
  rs_[D3DRENDERSTATE_ZWRITEENABLE] = TRUE;
  rs_[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_ONE;
  rs_[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_ZERO;
  rs_[D3DRENDERSTATE_CULLMODE] = D3DCULL_CCW;
  rs_[D3DRENDERSTATE_ZFUNC] = D3DCMP_LESSEQUAL;
  rs_[D3DRENDERSTATE_ALPHAFUNC] = D3DCMP_ALWAYS;
  rs_[D3DRENDERSTATE_DITHERENABLE] = FALSE;
  rs_[D3DRENDERSTATE_TEXTUREPERSPECTIVE] = TRUE;
  rs_[D3DRENDERSTATE_COLORVERTEX] = TRUE;
  rs_[D3DRENDERSTATE_LIGHTING] = TRUE;
  rs_[D3DRENDERSTATE_CLIPPING] = TRUE;
  for (uint32_t s = 0; s < kStages; ++s) {
    ts_[s][D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
    ts_[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
    ts_[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
    ts_[s][D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
    ts_[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
    ts_[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
    ts_[s][D3DTSS_TEXCOORDINDEX] = s;
    ts_[s][D3DTSS_ADDRESS] = D3DTADDRESS_WRAP;
    ts_[s][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
    ts_[s][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
    ts_[s][D3DTSS_MAGFILTER] = D3DTFG_POINT;
    ts_[s][D3DTSS_MINFILTER] = D3DTFN_POINT;
    ts_[s][D3DTSS_MIPFILTER] = D3DTFP_NONE;
    ts_[s][D3DTSS_MAXANISOTROPY] = 1;
  }
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 4; ++k) {
      Cells(&transforms_[i])[k * 4 + k] = 1.0f;
      Cells(&world_[i])[k * 4 + k] = 1.0f;
    }
  }
  for (int i = 0; i < 8; ++i) {
    for (int k = 0; k < 4; ++k) Cells(&texture_m_[i])[k * 4 + k] = 1.0f;
  }
  const DDSURFACEDESC2& t = target_->desc();
  viewport_.dwWidth = t.dwWidth;
  viewport_.dwHeight = t.dwHeight;
  viewport_.dvMaxZ = 1.0f;
}

Device::~Device() {
  for (uint32_t s = 0; s < kStages; ++s) {
    if (textures_[s] != nullptr) textures_[s]->Release();
  }
  for (int i = 0; i < kMaxBlocks; ++i) delete blocks_[i];
  delete recording_;
  delete[] prim_;
  target_->Release();
  d3d_->Release();
}

HRESULT Device::GetDirect3D(LPDIRECT3D7* out) {
  if (out == nullptr) return DDERR_INVALIDPARAMS;
  *out = d3d_;
  d3d_->AddRef();
  return D3D_OK;
}

// ---------------------------------------------------------------------------
// The two exports of the proxy.

HRESULT CreateEx(ProxyCreateExFn, GUID*, void** out, REFIID id, IUnknown*) {
  VkFallbackTestStop();
  if (out == nullptr) return DDERR_INVALIDPARAMS;
  if (!IsEqualGUID(id, IID_IDirectDraw7)) {
    *out = nullptr;
    LogInterface("DirectDrawCreateEx", id);
    return DDERR_INVALIDPARAMS;
  }
  if (g_draw == nullptr) {
    g_draw = new Draw();
    ModLog("vk ddraw     : the game made its DirectDraw object (backend=vulkan)");
  } else {
    g_draw->AddRef();
  }
  *out = static_cast<IDirectDraw7*>(g_draw);
  return DD_OK;
}

HRESULT Create(ProxyCreateFn, GUID*, void** out, IUnknown*) {
  VkFallbackTestStop();
  if (out == nullptr) return DDERR_INVALIDPARAMS;
  if (g_draw == nullptr) g_draw = new Draw();
  *out = new DrawOld();
  return DD_OK;
}

}  // namespace

void VkDdrawInstall(int lines) {
  g_on = true;
  g_lines = lines != 0;
  ProxyCreateHooks hooks = {};
  hooks.create_ex = &CreateEx;
  hooks.create = &Create;
  ProxySetCreateHooks(hooks);
  VkSceneSetDirect(true);
  ModLog("vk ddraw     : backend=vulkan. DirectDraw 7 and Direct3D 7 of the "
         "mod, with the Vulkan renderer, and no dxwrapper and no Direct3D 9");
}

bool VkDdrawOn() { return g_on; }

void VkDdrawReport(unsigned long long frames) {
  if (!g_on || frames == 0) return;
  ModLog("vk ddraw     : %ld surfaces (%ld made since the start). In the "
         "window: %ld presents, %ld color fills, %ld copies, %ld copies from "
         "DXT",
         g_surfaces, g_surfaces_made, InterlockedExchange(&g_presents, 0),
         InterlockedExchange(&g_fills, 0), InterlockedExchange(&g_copy_blts, 0),
         InterlockedExchange(&g_dxt_blts, 0));
  const LONG line_draws = InterlockedExchange(&g_line_draws, 0);
  if (line_draws != 0) {
    ModLog("vk ddraw     : %ld draws of points or lines in the window, as %ld "
           "rectangles of 1 pixel across",
           line_draws, InterlockedExchange(&g_line_prims, 0));
  }
  AcquireSRWLockExclusive(&g_unmade_lock);
  for (int i = 0; i < g_unmade_count; ++i) {
    if (g_unmade[i].count == 0) continue;
    ModLog("vk ddraw     : %ld calls of %s, which the backend does not make",
           g_unmade[i].count, g_unmade[i].name);
    g_unmade[i].count = 0;
  }
  ReleaseSRWLockExclusive(&g_unmade_lock);
}
