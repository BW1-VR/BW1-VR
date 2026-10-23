// The screen interface on a surface of its own, shown in the headset as quad
// layers fixed in the room. See docs/ui-layer.md.
//
// Three files share the work:
//
//   draw_hooks.cpp   which batches go to the panel, and the census
//   gpu_bridge.cpp   the Direct3D 9 panel target, the switch, the share
//   ui_panel.cpp     the settings, the frame, the captures, the placement
//
// This header has no Direct3D in it, so the Direct3D 7 files can include it.

#pragma once

#include <stdint.h>

namespace bwvr {
struct PanelQuad;
}

// ui_panel from bwvr.ini.
const int kUiPanelOff = 0;    // the interface in the eye pictures
const int kUiPanelLayer = 1;  // the interface on its own layer
const int kUiPanelTest = 2;   // the test: also drawn to the panel, desktop only
// Not a value of the ini key. With ui_panel=0, the route of the subtitle
// card: only the help text goes to the panel target, in eye pass 0, and it
// does not draw in the eye pictures. The menus stay in the eye pictures.
const int kUiPanelSubtitles = 3;

// The quads cut from the one panel target (docs/ui-layer.md). The
// Creature panel draws in the help scope, so draw_hooks.cpp counts it as
// help. ui_panel.cpp cuts it out as a quad of its own.
const int kPanelMenu = 0;      // the Setup dialogs
const int kPanelHelp = 1;      // the subtitles and the help text
const int kPanelCreature = 2;  // the Creature panel

// creature_panel from bwvr.ini (docs/ui-layer.md, "The Creature panel").
const int kCreaturePanelOff = 0;    // no quad: the panel does not show
const int kCreaturePanelFixed = 1;  // fixed in the room, to the left
const int kCreaturePanelNear = 2;   // near the Creature

struct UiPanelSettings {
  int mode = kUiPanelOff;       // ui_panel
  int menu_distance_cm = 140;   // ui_menu_distance_cm
  int menu_pitch_deg = -10;     // ui_menu_pitch_deg, below eye level is < 0
  int menu_px_per_deg = 16;     // ui_menu_px_per_deg
  int menu_rect[4] = {0, 0, 0, 0};  // ui_menu_rect, render pixels, 0 is auto
  int help_distance_cm = 120;   // ui_help_distance_cm
  int help_pitch_deg = -30;     // ui_help_pitch_deg
  int help_px_per_deg = 44;     // ui_help_px_per_deg, at a 2880 px render
  int help_rect[4] = {0, 0, 0, 0};  // ui_help_rect
  int creature_panel = kCreaturePanelNear;  // creature_panel
};

// The Direct3D 9 render states of a batch, read after its draw.
struct UiPanelStates {
  uint32_t blend_on;
  uint32_t src;
  uint32_t dest;
  uint32_t z_on;
  uint32_t z_write;
  uint32_t stencil_on;
  uint32_t alpha_test;
};

// --- ui_panel.cpp ------------------------------------------------------------

void UiPanelSetOptions(const UiPanelSettings& settings);
const UiPanelSettings& UiPanelOptions();
// The ui_panel value in use now. The test key switches it between the ini
// value and 0.
int UiPanelMode();
void UiPanelToggle();
// The switch of the debug page for UiPanelToggle: UiPanelMode, or with
// ui_panel=0 1 while the subtitle card is on. kDebugCannot when there is
// nothing to switch.
int UiPanelValue();
// The test key of subtitle_read_ahead. keys names the keys for the log.
void UiPanelSubtitleReadAheadToggle(const char* keys);
int UiPanelSubtitleReadAheadValue();
// Once a stereo frame, after UiDepthNewFrame. It latches the route for both
// eye passes.
void UiPanelBeginFrame(bool to_headset, bool projection, uint64_t frame,
                       bool capture);
// After eye pass 0 returns: the capture.
void UiPanelAfterFirstEye(uint64_t frame, bool capture);
// The menu and the help quads of this frame, for XrScreen::SetPanelQuads.
// The Creature panel quad goes to XrScreen::StagePanelQuad. False when no
// quad has content.
bool UiPanelQuads(uint32_t render_w, uint32_t render_h, bwvr::PanelQuad out[2]);
// Around LH3DRender::FinishFrame, outside the eye passes.
void UiPanelBeforeFinish(bool in_stereo_pass);
void UiPanelAfterFinish(bool in_stereo_pass, uint64_t frame);
void UiPanelReport();
// After both eye passes, from UiDepthSetEye(-1): the subtitle card of the
// frame goes to the headset.
void UiPanelEndPasses();
// stereo.cpp, once a frame, before UiPanelBeginFrame: the head center and the
// orientation of the head in the LOCAL space.
void UiPanelHead(const float center[3], const float orientation[4]);

// --- draw_hooks.cpp ----------------------------------------------------------

bool UiPanelInstallHooks();
void UiPanelRouteFrame(int mode, bool on, bool check);
void UiPanelRouteOutside(int mode, bool on, bool check);
bool UiPanelDrew(int kind);
bool UiPanelFrameFailed();
void UiPanelRelease();
void UiPanelReportRoutes();
// The bounds of the help text that went to the panel target in this frame
// (x0, y0, x1, y1 in pixels of the render), and the size of the render.
bool UiPanelHelpBounds(float bounds[4], uint32_t* render_w,
                       uint32_t* render_h);
// The fallback of the subtitle card: the help text draws in the eye pictures
// at a fixed depth in meters. 0 gives it the interface depth.
void UiDepthSetHelpDepth(float meters);
uint64_t UiDepthTakeHelpFixed();
void UiDepthSetSubtitleSelfTest(bool on);

// --- gpu_bridge.cpp ----------------------------------------------------------

// target: the panel target. share: its copy to OpenXR too.
void GpuBridgePanelWant(bool target, bool share);
bool GpuBridgePanelUsable();
bool GpuBridgePanelShared();
void GpuBridgePanelNewFrame(bool check);
// which: 0 the panel target, 1 the check target of the composite check.
bool GpuBridgePanelBind(int which);
void GpuBridgePanelUnbind();
void GpuBridgePanelStates(UiPanelStates* out);
void* GpuBridgeCaptureUi();
// which: 0 the panel, 1 the check target, 2 the eye before the first panel
// batch. The pixels stay valid until GpuBridgePanelReadDone.
const void* GpuBridgePanelRead(int which, int* pitch, uint32_t* width,
                               uint32_t* height);
void GpuBridgePanelReadDone();
// The subtitle card: a part of the panel of this frame at half the size.
// rect is x, y, width and height in panel pixels. The pixels stay valid
// until GpuBridgePanelReadDone.
const void* GpuBridgePanelReadHalf(const int rect[4], int* pitch,
                                   uint32_t* width, uint32_t* height);
// The read ahead of the subtitle card (subtitle_read_ahead=1). After the eye
// passes, QueueHalf puts the half size copy of rect in the queue, with no
// wait. At the start of a later frame, FetchHalf reads a copy that the GPU
// has done. After the eye passes, ReadyHalf gives the newest pixels that were
// read, of a copy from the frame `since` or later, and the rect of the panel
// that they hold. The pixels stay valid until GpuBridgePanelReadDone.
bool GpuBridgePanelQueueHalf(const int rect[4], uint64_t frame);
void GpuBridgePanelFetchHalf();
const void* GpuBridgePanelReadyHalf(uint64_t since, int rect[4], int* pitch,
                                    uint32_t* width, uint32_t* height);
void GpuBridgePanelReport();
// True when `surface` (an IDirect3DSurface9) is the panel target. The Vulkan
// renderer draws the draws for it into its own panel (vk_scene.h, vk_panel).
bool GpuBridgeIsPanel(const void* surface);
