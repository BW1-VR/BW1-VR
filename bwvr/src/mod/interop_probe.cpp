// Ask the backend's own objects what else they can be.
//
// This one question decides how much work removing the CPU readback is.
//
// d7vk is a proxy. Its IDirect3DDevice7 wraps one of DXVK's Direct3D 9
// devices, and DXVK publishes Vulkan interop interfaces on those:
// ID3D9VkInteropDevice hands over the VkDevice and the queue, and
// ID3D9VkInteropTexture hands over the VkImage behind a texture. If the proxy
// passes an interface id it does not recognize down to the object it wraps,
// then the image holding the finished frame is one QueryInterface away, and
// no fork of the backend is needed.
//
// dxwrapper is the same question with a different shape: its objects might
// hand back the IDirect3DDevice9 it renders with, which can make a shared
// texture that Direct3D 11 opens.
//
// If nothing answers, the backend hides its insides, and reaching the frame
// on the GPU means forking it.
//
// This file exists on its own because it needs the Direct3D 7 headers.
// d3dtypes.h and d3d9types.h declare the same enums with different values,
// so no file can include both.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

#include "../common/bw_addresses.h"

extern void ModLog(const char* format, ...);

namespace {

struct Candidate {
  const char* name;
  GUID id;
};

// From DXVK's d3d9_interfaces.h and from the Direct3D 9 headers.
const Candidate kCandidates[] = {
    {"ID3D9VkInteropDevice",
     {0x2eaa4b89, 0x0107, 0x4bdb,
      {0x87, 0xf7, 0x0f, 0x54, 0x1c, 0x49, 0x3c, 0xe0}}},
    {"ID3D9VkInteropTexture",
     {0xd56344f5, 0x8d35, 0x46fd,
      {0x80, 0x6d, 0x94, 0xc3, 0x51, 0xb4, 0x72, 0xc1}}},
    {"ID3D9VkInteropInterface",
     {0x3461a81b, 0xce41, 0x485b,
      {0xb6, 0xb5, 0xfc, 0xf0, 0x8b, 0xa6, 0xa6, 0xbd}}},
    {"ID3D9VkInteropInterface1",
     {0xd6589ed4, 0x7a37, 0x4096,
      {0xba, 0xc2, 0x22, 0x3b, 0x25, 0xae, 0x31, 0xd2}}},
    {"ID3D9VkExtSwapchain",
     {0x13776e93, 0x4aa9, 0x430a,
      {0xa4, 0xec, 0xfe, 0x9e, 0x28, 0x11, 0x81, 0xd5}}},
    {"IDirect3DDevice9",
     {0xd0223b96, 0xbf7a, 0x43fd,
      {0x92, 0xbd, 0xa4, 0x3b, 0x0d, 0x82, 0xb9, 0xeb}}},
    {"IDirect3DDevice9Ex",
     {0xb18b10ce, 0x2649, 0x405a,
      {0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a}}},
    {"IDirect3DSurface9",
     {0x0cfbaf3a, 0x9ff6, 0x429a,
      {0x99, 0xb3, 0xa2, 0x79, 0x6a, 0xf8, 0xb8, 0x9b}}},
    {"IDirect3DTexture9",
     {0x85c31227, 0x3de5, 0x4f00,
      {0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5}}},
};
const int kCount = sizeof(kCandidates) / sizeof(kCandidates[0]);

// Asks one object for every candidate interface and reports what sticks.
int AskAll(IUnknown* object, const char* what) {
  if (object == nullptr) return 0;
  int found = 0;
  for (int i = 0; i < kCount; ++i) {
    void* out = nullptr;
    if (SUCCEEDED(object->QueryInterface(kCandidates[i].id, &out)) &&
        out != nullptr) {
      ModLog("interop      : the %s IS a %s", what, kCandidates[i].name);
      reinterpret_cast<IUnknown*>(out)->Release();
      ++found;
    }
  }
  return found;
}

}  // namespace

void GpuBridgeProbeInterop() {
  IDirect3DDevice7** slot =
      reinterpret_cast<IDirect3DDevice7**>(bw::render::kDirect3DDevice7);
  if (IsBadReadPtr(slot, sizeof(void*)) || *slot == nullptr) {
    ModLog("interop      : no Direct3D 7 device yet");
    return;
  }
  IDirect3DDevice7* device = *slot;

  int found = AskAll(reinterpret_cast<IUnknown*>(device), "device");

  // The render target too, because a texture interface only ever appears on
  // a surface.
  IDirectDrawSurface7* target = nullptr;
  if (SUCCEEDED(device->GetRenderTarget(&target)) && target != nullptr) {
    found += AskAll(reinterpret_cast<IUnknown*>(target), "render target");
    target->Release();
  }

  if (found == 0) {
    ModLog("interop      : the backend hides everything underneath it, so "
           "reaching the frame on the GPU needs a fork of the backend");
  }
}
