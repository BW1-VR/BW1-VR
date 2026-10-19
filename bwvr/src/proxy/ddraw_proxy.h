// The pass-through ddraw.dll (ddraw_proxy.cpp).
#pragma once

#include <windows.h>
#include <unknwn.h>

// The two exports that make a DirectDraw object can go through a function of
// the mod (the count of ddraw_census, and later a backend of the mod). With
// no function set they go to the backend. The function gets the export of the
// backend as `real`, and it can call it.
typedef HRESULT(WINAPI* ProxyCreateExFn)(GUID* driver, void** out, REFIID id,
                                         IUnknown* outer);
typedef HRESULT(WINAPI* ProxyCreateFn)(GUID* driver, void** out,
                                       IUnknown* outer);
struct ProxyCreateHooks {
  HRESULT (*create_ex)(ProxyCreateExFn real, GUID* driver, void** out,
                       REFIID id, IUnknown* outer);
  HRESULT (*create)(ProxyCreateFn real, GUID* driver, void** out,
                    IUnknown* outer);
};
void ProxySetCreateHooks(const ProxyCreateHooks& hooks);

// An export of the backend that ProxyLoadRealDDraw loaded, or null.
void* ProxyRealExport(const char* name);

// backend=vulkan in bwvr.ini: the proxy loaded no backend DLL, and the mod
// makes DirectDraw itself (vk_ddraw.h).
bool ProxyBackendVulkan();
