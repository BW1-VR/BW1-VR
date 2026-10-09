// Better texture filtering than 2001 could afford.
//
// The game asks for point or bilinear filtering, which was the right choice on
// the hardware it shipped for. In a headset it is not. Ground textures seen at
// a shallow angle are exactly the case bilinear filtering handles worst, and
// the shimmer that produces is far more obvious in stereo than on a monitor,
// because the two eyes shimmer differently and the picture will not settle.
//
// Anisotropic filtering fixes that case specifically, and a modern GPU does it
// for free. The engine sets its filtering through
// IDirect3DDevice7::SetTextureStageState, so that is where this steps in.
//
// It is careful in two ways:
//
// 1. It asks the device what it supports before overriding anything, rather
//    than assuming. If anisotropic filtering is not available, or the maximum
//    ratio is 1, nothing changes.
// 2. If the override is rejected at draw time it passes the game's own value
//    through instead, so a refusal cannot leave a stage unset.
//
// Mip filtering is raised to linear as well, which is trilinear filtering, but
// only when the game asked for some mip filtering already. Turning it on where
// the game turned it off would sample mip levels that may not exist.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdint.h>

#include "MinHook.h"
#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);

namespace {

// IDirect3DDevice7 vtable slots.
const int kSlotGetCaps = 3;
const int kSlotSetTexture = 35;
const int kSlotSetTextureStageState = 37;

typedef HRESULT(WINAPI* SetTextureStageStateFn)(void*, DWORD,
                                                D3DTEXTURESTAGESTATETYPE,
                                                DWORD);
typedef HRESULT(WINAPI* GetCapsFn)(void*, LPD3DDEVICEDESC7);
typedef HRESULT(WINAPI* SetTextureFn)(void*, DWORD, LPDIRECTDRAWSURFACE7);

SetTextureStageStateFn o_SetTextureStageState = nullptr;
SetTextureFn o_SetTexture = nullptr;

// A survey, so the aliasing is diagnosed rather than guessed at.
//
// Three different reports need telling apart. Jagged edges on models are a
// lack of multisampling. Jagged edges on a fence or a tree are alpha testing
// with no multisampling. Repeated textures that crawl in the distance, roof
// tiles being the clearest case, are a lack of mip levels, and no amount of
// filtering can fix a texture that has none to filter between.
//
// So this counts what the game asks for and what its textures actually carry.
bool g_survey = false;
uint64_t g_asked_min[8] = {0};
uint64_t g_asked_mag[8] = {0};
uint64_t g_asked_mip[8] = {0};
uint64_t g_textures_seen = 0;
uint64_t g_textures_with_mips = 0;
uint64_t g_mip_levels_total = 0;
uint32_t g_widest = 0;
void* g_last_texture = nullptr;

bool g_enabled = false;
bool g_force_mips = false;
DWORD g_max_anisotropy = 1;
bool g_can_anisotropic_min = false;
bool g_can_anisotropic_mag = false;
uint64_t g_overrides = 0;
uint64_t g_mips_forced = 0;

// Counts the mip levels on each new texture the game binds. Only a change of
// texture is inspected, so a surface bound every frame is asked once.
HRESULT WINAPI h_SetTexture(void* self, DWORD stage,
                            LPDIRECTDRAWSURFACE7 texture) {
  if (g_survey && texture != nullptr && texture != g_last_texture) {
    g_last_texture = texture;
    DDSURFACEDESC2 desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (SUCCEEDED(texture->GetSurfaceDesc(&desc))) {
      ++g_textures_seen;
      // DDSCAPS_MIPMAP is set on these surfaces even when there is only one
      // level, so the flag means nothing on its own. The level COUNT is what
      // decides whether there is a chain to filter between.
      const DWORD levels =
          (desc.dwFlags & DDSD_MIPMAPCOUNT) ? desc.dwMipMapCount : 1;
      if (levels > 1) ++g_textures_with_mips;
      g_mip_levels_total += levels;
      if (desc.dwWidth > g_widest) g_widest = desc.dwWidth;
    }
  }
  return o_SetTexture(self, stage, texture);
}

HRESULT WINAPI h_SetTextureStageState(void* self, DWORD stage,
                                      D3DTEXTURESTAGESTATETYPE state,
                                      DWORD value) {
  if (g_survey && value < 8) {
    if (state == D3DTSS_MINFILTER) ++g_asked_min[value];
    else if (state == D3DTSS_MAGFILTER) ++g_asked_mag[value];
    else if (state == D3DTSS_MIPFILTER) ++g_asked_mip[value];
  }
  if (!g_enabled) {
    return o_SetTextureStageState(self, stage, state, value);
  }

  // THE GAME NEVER SETS MIP FILTERING.
  //
  // The survey is unambiguous: 228,748 MINFILTER calls, 228,748 MAGFILTER
  // calls, and not one MIPFILTER call in a whole session. So the mip filter
  // sits at the Direct3D default of NONE for the entire game, and any mip
  // levels the backend builds are never sampled. Waiting for the game to ask
  // and then upgrading the value, which is what the code below does, can
  // never fire.
  //
  // So it is set here instead, alongside a filter call the game does make.
  // This only makes sense with a backend that provides the levels, which is
  // what DdrawForceMipMapAutoGen in dxwrapper.ini is for. With no levels to
  // sample, a linear mip filter is harmless: there is one level and it gets
  // used.
  if (g_force_mips && state == D3DTSS_MINFILTER) {
    if (SUCCEEDED(o_SetTextureStageState(self, stage, D3DTSS_MIPFILTER,
                                         D3DTFP_LINEAR))) {
      ++g_mips_forced;
    }
  }

  DWORD wanted = value;
  bool changed = false;

  if (state == D3DTSS_MAGFILTER && g_can_anisotropic_mag &&
      value != D3DTFG_ANISOTROPIC) {
    // Point filtering is usually deliberate, for the interface and for masks,
    // so only smooth filtering is raised.
    if (value == D3DTFG_LINEAR) {
      wanted = D3DTFG_ANISOTROPIC;
      changed = true;
    }
  } else if (state == D3DTSS_MINFILTER && g_can_anisotropic_min &&
             value != D3DTFN_ANISOTROPIC) {
    if (value == D3DTFN_LINEAR) {
      wanted = D3DTFN_ANISOTROPIC;
      changed = true;
    }
  } else if (state == D3DTSS_MIPFILTER && value != D3DTFP_LINEAR) {
    // Choosing one mip level becomes blending two, which is trilinear
    // filtering. D3DTFP_NONE is raised as well when g_force_mips is set,
    // because a texture that HAS mip levels and is told not to use them
    // crawls, as roof tiles do.
    if (value == D3DTFP_POINT || (g_force_mips && value == D3DTFP_NONE)) {
      wanted = D3DTFP_LINEAR;
      changed = true;
    }
  }

  if (!changed) {
    return o_SetTextureStageState(self, stage, state, value);
  }

  // The ratio has to be set for anisotropic filtering to do anything.
  if (wanted == D3DTFG_ANISOTROPIC || wanted == D3DTFN_ANISOTROPIC) {
    o_SetTextureStageState(self, stage, D3DTSS_MAXANISOTROPY, g_max_anisotropy);
  }

  const HRESULT hr = o_SetTextureStageState(self, stage, state, wanted);
  if (FAILED(hr)) {
    // Refused. Put the game's own choice back rather than leave it unset.
    return o_SetTextureStageState(self, stage, state, value);
  }
  ++g_overrides;
  return hr;
}

}  // namespace

// Reads the device capabilities and hooks the filter calls. Returns the
// maximum anisotropy in use, or 0 when nothing was changed.
DWORD TextureFilterInstall(int requested_anisotropy) {
  if (requested_anisotropy <= 1) return 0;

  void** device_slot = reinterpret_cast<void**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(device_slot, sizeof(void*)) || *device_slot == nullptr) {
    return 0;
  }
  void* device = *device_slot;
  if (IsBadReadPtr(device, sizeof(void*))) return 0;
  void** vtable = *reinterpret_cast<void***>(device);
  if (IsBadReadPtr(vtable, sizeof(void*) * 49)) return 0;

  D3DDEVICEDESC7 caps;
  ZeroMemory(&caps, sizeof(caps));
  GetCapsFn get_caps = reinterpret_cast<GetCapsFn>(vtable[kSlotGetCaps]);
  if (FAILED(get_caps(device, &caps))) {
    ModLog("filtering    : the device would not report its capabilities");
    return 0;
  }

  g_can_anisotropic_min =
      (caps.dpcTriCaps.dwTextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC) != 0;
  g_can_anisotropic_mag =
      (caps.dpcTriCaps.dwTextureFilterCaps & D3DPTFILTERCAPS_MAGFANISOTROPIC) != 0;
  g_max_anisotropy = caps.dwMaxAnisotropy;

  if (!g_can_anisotropic_min && !g_can_anisotropic_mag) {
    ModLog("filtering    : the device does not offer anisotropic filtering");
    return 0;
  }
  if (g_max_anisotropy < 2) {
    ModLog("filtering    : the device reports a maximum anisotropy of %lu",
           g_max_anisotropy);
    return 0;
  }
  if (g_max_anisotropy > static_cast<DWORD>(requested_anisotropy)) {
    g_max_anisotropy = static_cast<DWORD>(requested_anisotropy);
  }

  void* target = vtable[kSlotSetTextureStageState];
  MH_STATUS status = MH_CreateHook(
      target, reinterpret_cast<void*>(&h_SetTextureStageState),
      reinterpret_cast<void**>(&o_SetTextureStageState));
  if (status == MH_OK) status = MH_EnableHook(target);
  if (status != MH_OK) {
    ModLog("filtering    : SetTextureStageState at 0x%08X FAILED",
           reinterpret_cast<uintptr_t>(target));
    return 0;
  }

  if (g_survey) {
    void* set_texture = vtable[kSlotSetTexture];
    MH_STATUS texture_status =
        MH_CreateHook(set_texture, reinterpret_cast<void*>(&h_SetTexture),
                      reinterpret_cast<void**>(&o_SetTexture));
    if (texture_status == MH_OK) texture_status = MH_EnableHook(set_texture);
    ModLog("filtering    : texture survey %s",
           texture_status == MH_OK ? "on" : "FAILED");
  }

  // What the hardware offers for edges, which is a different problem from
  // texture filtering and needs saying out loud.
  ModLog("filtering    : raster caps 0x%08lX, device antialias %s",
         caps.dpcTriCaps.dwRasterCaps,
         (caps.dwDevCaps & D3DDEVCAPS_DRAWPRIMITIVES2EX) ? "ex" : "plain");

  g_enabled = true;
  ModLog("filtering    : anisotropic %lux, min=%d mag=%d", g_max_anisotropy,
         g_can_anisotropic_min ? 1 : 0, g_can_anisotropic_mag ? 1 : 0);
  return g_max_anisotropy;
}

void TextureFilterSetOptions(bool survey, bool force_mips) {
  g_survey = survey;
  g_force_mips = force_mips;
}

void TextureFilterReport() {
  if (!g_enabled) return;
  ModLog("filtering    : raised the filter on %llu stage changes, set the mip "
         "filter %llu times", g_overrides, g_mips_forced);
  if (!g_survey) return;

  static const char* kMin[] = {"?", "POINT", "LINEAR", "ANISOTROPIC",
                               "FLATCUBIC", "GAUSSIANCUBIC", "?", "?"};
  static const char* kMip[] = {"NONE", "POINT", "LINEAR", "?",
                               "?", "?", "?", "?"};
  ModLog("filtering    : what the game asked for");
  for (int i = 0; i < 8; ++i) {
    if (g_asked_min[i] > 0) {
      ModLog("filtering    :   MINFILTER %-14s %llu", kMin[i], g_asked_min[i]);
    }
  }
  for (int i = 0; i < 8; ++i) {
    if (g_asked_mag[i] > 0) {
      ModLog("filtering    :   MAGFILTER %-14s %llu", kMin[i], g_asked_mag[i]);
    }
  }
  for (int i = 0; i < 8; ++i) {
    if (g_asked_mip[i] > 0) {
      ModLog("filtering    :   MIPFILTER %-14s %llu", kMip[i], g_asked_mip[i]);
    }
  }
  ModLog("filtering    : %llu texture binds, %llu had more than one mip "
         "level, %.2f levels each on average, widest %u",
         g_textures_seen, g_textures_with_mips,
         g_textures_seen > 0
             ? static_cast<double>(g_mip_levels_total) / g_textures_seen
             : 0.0,
         g_widest);
  if (g_textures_with_mips == 0) {
    ModLog("filtering    : no mip levels on the Direct3D 7 surfaces. Note that "
           "this reads the surface the GAME sees. A backend that builds mip "
           "chains underneath, in Direct3D 9, would not show up here, so this "
           "line cannot tell you whether DdrawForceMipMapAutoGen worked.");
  }
  if (g_asked_mip[0] == 0 && g_asked_mip[1] == 0 && g_asked_mip[2] == 0) {
    ModLog("filtering    : the game never set MIPFILTER once, so without the "
           "override above the mip filter stays at NONE all game");
  }
}
