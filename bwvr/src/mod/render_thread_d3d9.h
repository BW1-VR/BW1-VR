// The calls of Direct3D 9 that one_walk.cpp changes, through the layer of the
// render thread (render_thread.cpp). render_thread.cpp has the hooks of the
// device. For these seven calls its hook calls the function of one_walk.cpp,
// and one_walk.cpp gives each call that it makes to the RenderThreadOut
// functions: they keep the call for the render thread, or give it to
// Direct3D 9.

#pragma once

#include <d3d9.h>

// render_thread.cpp: the hooks of the device. Without `all` only the seven
// calls of one walk, with `all` each call of the device and the locks of the
// resources too. False when a hook failed.
bool RenderThreadInstall(IDirect3DDevice9* device, bool all);

HRESULT RenderThreadOutSetRenderTarget(IDirect3DDevice9* self, DWORD index,
                                       IDirect3DSurface9* surface);
HRESULT RenderThreadOutSetDepthStencil(IDirect3DDevice9* self,
                                       IDirect3DSurface9* surface);
HRESULT RenderThreadOutClear(IDirect3DDevice9* self, DWORD count,
                             const D3DRECT* rects, DWORD flags, D3DCOLOR color,
                             float z, DWORD stencil);
HRESULT RenderThreadOutDrawPrimitive(IDirect3DDevice9* self,
                                     D3DPRIMITIVETYPE type, UINT start,
                                     UINT count);
HRESULT RenderThreadOutDrawIndexedPrimitive(IDirect3DDevice9* self,
                                            D3DPRIMITIVETYPE type, INT base,
                                            UINT min_index, UINT vertices,
                                            UINT start, UINT count);
HRESULT RenderThreadOutDrawPrimitiveUP(IDirect3DDevice9* self,
                                       D3DPRIMITIVETYPE type, UINT count,
                                       const void* vertices, UINT stride);
HRESULT RenderThreadOutDrawIndexedPrimitiveUP(
    IDirect3DDevice9* self, D3DPRIMITIVETYPE type, UINT min_index,
    UINT vertex_count, UINT primitive_count, const void* indices,
    D3DFORMAT index_format, const void* vertices, UINT stride);

// one_walk.cpp: the seven calls of one walk.
HRESULT OneWalkOnSetRenderTarget(IDirect3DDevice9* self, DWORD index,
                                 IDirect3DSurface9* surface);
HRESULT OneWalkOnSetDepthStencil(IDirect3DDevice9* self,
                                 IDirect3DSurface9* surface);
HRESULT OneWalkOnClear(IDirect3DDevice9* self, DWORD count,
                       const D3DRECT* rects, DWORD flags, D3DCOLOR color,
                       float z, DWORD stencil);
HRESULT OneWalkOnDrawPrimitive(IDirect3DDevice9* self, D3DPRIMITIVETYPE type,
                               UINT start, UINT count);
HRESULT OneWalkOnDrawIndexedPrimitive(IDirect3DDevice9* self,
                                      D3DPRIMITIVETYPE type, INT base,
                                      UINT min_index, UINT vertices,
                                      UINT start, UINT count);
HRESULT OneWalkOnDrawPrimitiveUP(IDirect3DDevice9* self, D3DPRIMITIVETYPE type,
                                 UINT count, const void* vertices,
                                 UINT stride);
HRESULT OneWalkOnDrawIndexedPrimitiveUP(IDirect3DDevice9* self,
                                        D3DPRIMITIVETYPE type, UINT min_index,
                                        UINT vertex_count,
                                        UINT primitive_count,
                                        const void* indices,
                                        D3DFORMAT index_format,
                                        const void* vertices, UINT stride);

// One draw for both eyes (one_walk_merge, docs/backend.md, "One draw for both
// eyes"). The device gets the viewport `both` over both eyes, and GetViewport
// still gives the viewport of the game. False when the walk does not go to
// the queue.
bool RenderThreadViewportBoth(IDirect3DDevice9* self,
                              const D3DVIEWPORT9* both);
// The device gets the viewport of the game back, if it has the one of both
// eyes.
void RenderThreadViewportPutBack(IDirect3DDevice9* self);
// One record of a triangle list with the left copy and then the right copy
// of the vertices, and the indices of the right copy moved by the count of a
// copy. False when the walk does not go to the queue, or a copy is too large
// for 16-bit indices.
bool RenderThreadOutDrawIndexedBoth(IDirect3DDevice9* self,
                                    D3DPRIMITIVETYPE type, UINT min_index,
                                    UINT vertex_count, UINT primitive_count,
                                    const void* indices, D3DFORMAT index_format,
                                    const void* left, const void* right,
                                    UINT stride);
