// The calls of Direct3D 9 that one_walk.cpp changes, through the layer of the
// render thread (render_thread.cpp). render_thread.cpp has the hooks of the
// device. For these seven calls its hook calls the function of one_walk.cpp,
// and one_walk.cpp gives each call that it makes to the RenderThreadOut
// functions: they keep the call for the render thread, or give it to
// Direct3D 9.

#pragma once

#include <d3d9.h>
#include <stdint.h>

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
// THE RIGHT EYE ON THE RENDER THREAD (render_thread_right_eye=1). One record
// of a triangle list with the left copy only, and room for the right copy.
// The render thread makes the right copy: each x moves by eye_width, and the
// x of a world batch also by per_rhw times its rhw (one_walk_math.h,
// CopyForRightEye). When each copy is in its own half of `game` and the
// viewport of the right eye, the record is one draw with the viewport over
// both eyes. Else the render thread draws each copy with the viewport of its
// eye and puts the viewport over both eyes back. The caller first sets the
// viewport over both eyes (RenderThreadViewportBoth). False when the walk
// does not go to the queue, render_thread_right_eye is 0, or a copy is too
// large for 16-bit indices.
bool RenderThreadOutDrawBothLater(IDirect3DDevice9* self,
                                  D3DPRIMITIVETYPE type, UINT min_index,
                                  UINT vertex_count, UINT primitive_count,
                                  const void* indices, D3DFORMAT index_format,
                                  const void* left, UINT stride,
                                  const D3DVIEWPORT9& game, UINT eye_width,
                                  float per_rhw);
// The counts of the right copies that the render thread made since the last
// call. The render thread writes them, so a count can be one batch late.
struct RenderThreadRightEyeCounts {
  uint32_t world;
  uint32_t flat;
  uint32_t sky;
  uint32_t vertices;
  uint32_t own_halves;  // one draw for both eyes
};
void RenderThreadTakeRightEyeCounts(RenderThreadRightEyeCounts* out);
