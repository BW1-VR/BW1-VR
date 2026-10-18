#include "protocol.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <cstring>
#include <cwchar>

namespace {
FILE* output = nullptr;
void Log(const char* format, ...) {
  va_list args; va_start(args, format);
  if (output) { vfprintf(output, format, args); fputc('\n', output); fflush(output); }
  va_end(args);
}
bool Drain(ID3D11DeviceContext* context, ID3D11Query* query) {
  context->End(query); context->Flush();
  const ULONGLONG start = GetTickCount64();
  for (;;) {
    BOOL done = FALSE;
    HRESULT hr = context->GetData(query, &done, sizeof(done), 0);
    if (hr == S_OK && done) return true;
    if (FAILED(hr) || GetTickCount64() - start > 2000) { Log("FAIL: GPU wait, 0x%08lX", hr); return false; }
    Sleep(1);
  }
}
}
int wmain(int argc, wchar_t** argv) {
  static_assert(sizeof(void*) == 8, "The XR host must be x64");
  if (argc != 3) return 1;
  wchar_t path[32768];
  DWORD size = GetModuleFileNameW(nullptr, path, 32768);
  if (!size || size >= 32768) return 1;
  wchar_t* file = wcsrchr(path, L'\\');
  if (!file || wcscpy_s(file + 1, 32768 - (file + 1 - path), L"bwvr-host.log") != 0 ||
      _wfopen_s(&output, path, L"w") != 0) return 1;
  SYSTEMTIME now; GetSystemTime(&now);
  Log("%04u-%02u-%02u %02u:%02u:%02u UTC, PID %lu", now.wYear, now.wMonth, now.wDay,
      now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
  Log("Black and White experimental x64 OpenXR host. Protocol %u, packet %zu bytes.", bwvr::bridge::kVersion, sizeof(bwvr::bridge::Packet));
  const std::wstring channel = argv[1];
  HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, channel.c_str());
  HANDLE request = OpenEventW(SYNCHRONIZE, FALSE, (channel + L"-request").c_str());
  HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, (channel + L"-ready").c_str());
  HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, (channel + L"-stop").c_str());
  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, wcstoul(argv[2], nullptr, 10));
  auto* p = mapping ? static_cast<bwvr::bridge::Packet*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(bwvr::bridge::Packet))) : nullptr;
  int exit_code = 1;
  if (p && request && ready && stop && parent) {
    bwvr::XrScreen screen;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Query* query = nullptr;
    ID3D11Texture2D* textures[3] = {};
    uint64_t handles[3] = {};
    std::vector<uint8_t> cards[bwvr::kCards];
    uint32_t sequence = 0;
    bool frame_open = false;
    for (;;) {
      HANDLE waits[] = {parent, stop, request};
      DWORD wait = WaitForMultipleObjects(3, waits, FALSE, 120000);
      if (wait == WAIT_OBJECT_0 || wait == WAIT_OBJECT_0 + 1) { exit_code = 0; break; }
      if (wait != WAIT_OBJECT_0 + 2) { Log("FAIL: host idle timeout or event failure"); break; }
      if (p->magic != bwvr::bridge::kMagic || p->version != bwvr::bridge::kVersion ||
          p->bytes != sizeof(*p) || p->sequence != ++sequence || p->payload_bytes > bwvr::bridge::kPayload) {
        Log("FAIL: protocol header or sequence"); break;
      }
      p->result = 1;
      using namespace bwvr::bridge;
      switch (p->command) {
        case Probe:
          p->state.eyes[0] = p->render_pose[1];
          p->state.time = static_cast<int64_t>(p->shared_handle);
          for (uint32_t i = 0; i < p->payload_bytes; ++i) p->payload[i] ^= 0x5a;
          p->result = 64;
          break;
        case Instance: p->result = screen.StartInstance(Log); break;
        case System:
          p->result = screen.StartSystem(); screen.LogSystem();
          screen.RecommendedSize(&p->state.recommended_width, &p->state.recommended_height); break;
        case Session: {
          if (!p->width || !p->height || p->width > 8192 || p->height > 8192 || (p->eyes != 1 && p->eyes != 2)) { p->result = -1; break; }
          screen.SetPlacement(p->distance, p->screen_width); screen.EnablePanel(p->panel_enabled != 0);
          if (!screen.StartSession(p->width, p->height, p->eyes)) { p->result = 0; break; }
          auto* device = static_cast<ID3D11Device*>(screen.D3D11Device());
          device->GetImmediateContext(&context);
          D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT, 0};
          if (FAILED(device->CreateQuery(&desc, &query))) { p->result = -1; break; }
          IDXGIDevice* dxgi = nullptr; IDXGIAdapter* adapter = nullptr; DXGI_ADAPTER_DESC ad;
          HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi));
          if (SUCCEEDED(hr)) { hr = dxgi->GetAdapter(&adapter); dxgi->Release(); }
          if (SUCCEEDED(hr)) { hr = adapter->GetDesc(&ad); adapter->Release(); }
          if (FAILED(hr)) { p->result = -1; break; }
          p->state.adapter_low = ad.AdapterLuid.LowPart; p->state.adapter_high = static_cast<uint32_t>(ad.AdapterLuid.HighPart);
          p->state.format = screen.SwapchainFormat(); p->state.panel_ready = screen.panel_ready();
          Log("Session ready: %ux%u, %u eyes, panel %u", p->width, p->height, p->eyes, p->state.panel_ready);
          break;
        }
        case Begin:
          if (frame_open) { p->result = -1; break; }
          p->result = screen.BeginFrame(); frame_open = p->result != 0;
          p->state.render = screen.should_render(); p->state.focus = screen.InputFocused();
          p->state.recenter = screen.TakeRecenter(); p->state.time = p->state.period = 0;
          screen.LastDisplayTime(&p->state.time, &p->state.period);
          for (int eye = 0; eye < 2; ++eye) {
            p->state.eyes[eye] = screen.eye(eye); p->state.hands[eye] = screen.hand(eye);
            strncpy_s(p->state.profiles[eye], screen.HandProfile(eye), _TRUNCATE);
          }
          break;
        case EyeTexture: case PanelTexture: {
          const int slot = p->command == PanelTexture ? 2 : static_cast<int>(p->index);
          if (p->command == PanelTexture && !p->shared_handle) {
            screen.SetPanelTexture(nullptr); break;
          }
          if (!frame_open || slot < 0 || slot > 2 || !context || !p->shared_handle) { p->result = -1; break; }
          if (handles[slot] != p->shared_handle) {
            if (textures[slot]) { textures[slot]->Release(); textures[slot] = nullptr; }
            auto* device = static_cast<ID3D11Device*>(screen.D3D11Device());
            HRESULT hr = device->OpenSharedResource(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(p->shared_handle)),
                __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&textures[slot]));
            if (FAILED(hr)) { Log("FAIL: open texture slot %d, 0x%08lX", slot, hr); p->result = -1; break; }
            D3D11_TEXTURE2D_DESC desc; textures[slot]->GetDesc(&desc);
            if (desc.Width != p->width || desc.Height != p->height || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
                desc.ArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1) { p->result = -1; break; }
            handles[slot] = p->shared_handle;
            Log("Opened shared texture slot %d", slot);
          }
          if (slot == 2) screen.SetPanelTexture(textures[slot]); else screen.SetEyeTexture(slot, textures[slot]);
          if (!Drain(context, query)) p->result = -1;
          break;
        }
        case EyePixels:
          if (!frame_open || p->index > 1 || static_cast<uint64_t>(p->width) * p->height * 4 != p->payload_bytes) p->result = -1;
          else screen.SetEyeImage(p->index, p->payload, p->width * 4);
          break;
        case Panels:
          screen.StagePanelQuad(p->panels[2]); screen.SetPanelQuads(p->panels); break;
        case Card: {
          if (p->index >= bwvr::kCards || (p->card.visible &&
              static_cast<uint64_t>(p->card.texture_width) * p->card.texture_height * 4 != p->payload_bytes)) { p->result = -1; break; }
          auto& bytes = cards[p->index]; bytes.assign(p->payload, p->payload + p->payload_bytes);
          bwvr::CardLayer layer = {};
          layer.visible = p->card.visible != 0; layer.pixels = bytes.empty() ? nullptr : bytes.data();
          layer.texture_width = p->card.texture_width; layer.texture_height = p->card.texture_height;
          memcpy(layer.rect, p->card.rect, sizeof(layer.rect)); memcpy(layer.position, p->card.position, sizeof(layer.position));
          memcpy(layer.orientation, p->card.orientation, sizeof(layer.orientation)); layer.width = p->card.width; layer.height = p->card.height;
          screen.StageCard(p->index, layer); break;
        }
        case Window: screen.StageWindow(p->window); break;
        case End:
          if (!frame_open) { p->result = -1; break; }
          screen.SetProjection(p->projection != 0); screen.SetRenderHalfAngle(p->half_angle);
          screen.SetPlacement(p->distance, p->screen_width);
          if (p->have_render_pose) for (int i = 0; i < 2; ++i)
            screen.SetRenderPose(i, p->render_pose[i].position, p->render_pose[i].orientation);
          screen.EndFrame(); frame_open = false; break;
        case Haptic: screen.Haptic(p->index, p->amplitude, p->duration); break;
        default: p->result = -1; break;
      }
      p->state.running = screen.running(); p->state.presented = screen.presented(); p->state.skipped = screen.skipped();
      for (int i = 0; i < bwvr::kCards; ++i) p->state.card_failed[i] = screen.CardFailed(i);
      p->response = sequence;
      const bool fatal = p->result < 0;
      const uint32_t command = p->command;
      SetEvent(ready);
      if (fatal) { Log("FAIL: command %u, sequence %u", command, sequence); break; }
    }
    if (frame_open) screen.EndFrame();
    if (context && query) Drain(context, query);
    for (auto* texture : textures) if (texture) texture->Release();
    if (query) query->Release(); if (context) context->Release();
    Log("Host frames: %llu presented, %llu skipped", screen.presented(), screen.skipped());
    screen.Stop(); Log("Host stopped, result %d", exit_code);
  }
  if (p) UnmapViewOfFile(p);
  for (HANDLE h : {mapping, request, ready, stop, parent}) if (h) CloseHandle(h);
  fclose(output);
  return exit_code;
}
