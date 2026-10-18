#pragma once
#include "../xr_screen.h"
#include <stddef.h>

namespace bwvr::bridge {
constexpr uint32_t kMagic = 0x42575242, kVersion = 1;
constexpr uint32_t kPayload = 32 * 1024 * 1024;
enum Command : uint32_t { Instance = 1, System, Session, Begin, EyeTexture,
  EyePixels, PanelTexture, Panels, Card, Window, End, Haptic, Stop, Probe };
// These pointer-free public types have the same MSVC layout in x86 and x64.
static_assert(sizeof(EyeView) == 48 && sizeof(HandInput) == 96, "Input ABI");
static_assert(sizeof(PanelQuad) == 56 && sizeof(WindowLayer) == 132, "Layer ABI");
#pragma pack(push, 8)
struct CardInfo {
  uint32_t visible, texture_width, texture_height;
  int32_t rect[4];
  float position[3], orientation[4], width, height;
};
struct State {
  uint32_t running, render, focus, panel_ready, recenter;
  uint32_t recommended_width, recommended_height, format;
  uint32_t adapter_low, adapter_high;
  uint32_t card_failed[kCards];
  EyeView eyes[2];
  HandInput hands[2];
  char profiles[2][128];
  int64_t time, period;
  uint64_t presented, skipped;
};
struct Packet {
  uint32_t magic, version, bytes, command, sequence, response;
  int32_t result;
  uint32_t width, height, eyes, index, panel_enabled, payload_bytes;
  uint64_t shared_handle;
  float distance, screen_width, half_angle, amplitude, duration;
  uint32_t projection, have_render_pose;
  EyeView render_pose[2];
  PanelQuad panels[kPanelQuads];
  CardInfo card;
  WindowLayer window;
  State state;
  uint8_t payload[kPayload];
};
#pragma pack(pop)
static_assert(offsetof(Packet, shared_handle) == 56, "Handle alignment");
}
