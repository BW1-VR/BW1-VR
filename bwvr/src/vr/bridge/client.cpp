#include "client.h"
#include <dxgi1_2.h>
#include <string>
#include <cstring>

namespace bwvr {
RemoteScreen::~RemoteScreen() {
  Shutdown();
  if (device) device->Release();
  if (packet) UnmapViewOfFile(packet);
  for (HANDLE h : {mapping_, request_, ready_, stop_}) if (h) CloseHandle(h);
}
bool RemoteScreen::Launch() {
  const std::wstring channel = L"Local\\bwvr-host-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
  mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                               sizeof(bridge::Packet), channel.c_str());
  if (!mapping_ || GetLastError() == ERROR_ALREADY_EXISTS) return false;
  request_ = CreateEventW(nullptr, FALSE, FALSE, (channel + L"-request").c_str());
  ready_ = CreateEventW(nullptr, FALSE, FALSE, (channel + L"-ready").c_str());
  stop_ = CreateEventW(nullptr, TRUE, FALSE, (channel + L"-stop").c_str());
  packet = static_cast<bridge::Packet*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(bridge::Packet)));
  if (!packet || !request_ || !ready_ || !stop_) return false;
  ZeroMemory(packet, sizeof(*packet));
  packet->magic = bridge::kMagic; packet->version = bridge::kVersion; packet->bytes = sizeof(*packet);
  wchar_t path[32768];
  const DWORD n = GetModuleFileNameW(nullptr, path, 32768);
  if (!n || n >= 32768) return false;
  wchar_t* name = wcsrchr(path, L'\\');
  if (!name || wcscpy_s(name + 1, 32768 - (name + 1 - path), L"bwvr-host\\bwvr-xr-host.exe") != 0) return false;
  std::wstring command = L"\"" + std::wstring(path) + L"\" \"" + channel + L"\" " + std::to_wstring(GetCurrentProcessId());
  STARTUPINFOW startup = {}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION child = {};
  if (!CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &child)) {
    log_("xr bridge    : host launch failed, Windows error %lu", GetLastError()); return false;
  }
  process_ = child.hProcess;
  job_ = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
      !AssignProcessToJobObject(job_, process_)) {
    log_("xr bridge    : process job unavailable. The host will watch the parent between requests");
    if (job_) CloseHandle(job_);
    job_ = nullptr;
  }
  if (ResumeThread(child.hThread) == static_cast<DWORD>(-1)) {
    CloseHandle(child.hThread); TerminateProcess(process_, 1); Shutdown(); return false;
  }
  CloseHandle(child.hThread);
  log_("xr bridge    : started x64 host PID %lu, protocol %u. See bwvr-host\\bwvr-host.log", child.dwProcessId, bridge::kVersion);
  return true;
}
bool RemoteScreen::Call(bridge::Command command) {
  if (!healthy_ || !packet || !process_) return false;
  packet->command = command; packet->sequence = ++sequence_; packet->result = 0;
  if (!SetEvent(request_)) { healthy_ = false; return false; }
  HANDLE waits[] = {process_, ready_};
  const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, command <= bridge::Session ? 30000 : 5000);
  if (wait != WAIT_OBJECT_0 + 1 || packet->response != sequence_ ||
      packet->magic != bridge::kMagic || packet->version != bridge::kVersion || packet->bytes != sizeof(*packet)) {
    log_("xr bridge    : command %u failed or timed out, wait %lu, sequence %u", command, wait, sequence_);
    healthy_ = false; Shutdown(); return false;
  }
  if (packet->result < 0) {
    log_("xr bridge    : host rejected command %u. See bwvr-host.log", command);
    healthy_ = false; Shutdown(); return false;
  }
  return true;
}
bool RemoteScreen::MakeDevice() {
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return false;
  IDXGIAdapter1* adapter = nullptr;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) == S_OK; ++i) {
    DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.AdapterLuid.LowPart == packet->state.adapter_low &&
        static_cast<uint32_t>(desc.AdapterLuid.HighPart) == packet->state.adapter_high) break;
    adapter->Release(); adapter = nullptr;
  }
  factory->Release();
  if (!adapter) { log_("xr bridge    : no matching local GPU adapter"); return false; }
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  const HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 2,
                                      D3D11_SDK_VERSION, &device, nullptr, nullptr);
  adapter->Release();
  if (FAILED(hr)) log_("xr bridge    : local D3D11 device failed, 0x%08lX", hr);
  return SUCCEEDED(hr);
}
bool RemoteScreen::Texture(void* texture, int index, bool panel) {
  if (!healthy_) return false;
  if (!texture) {
    if (!panel) return false;
    packet->shared_handle = 0;
    return Call(bridge::PanelTexture);
  }
  IDXGIResource* resource = nullptr;
  auto* source = static_cast<ID3D11Texture2D*>(texture);
  HANDLE handle = nullptr;
  HRESULT hr = source->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&resource));
  if (SUCCEEDED(hr)) { hr = resource->GetSharedHandle(&handle); resource->Release(); }
  if (FAILED(hr) || !handle) {
    log_("xr bridge    : texture has no shared handle, 0x%08lX", hr);
    healthy_ = false; return false;
  }
  packet->index = index; packet->shared_handle = reinterpret_cast<uintptr_t>(handle);
  return Call(panel ? bridge::PanelTexture : bridge::EyeTexture);
}
bool RemoteScreen::Pixels(int index, const void* data, int pitch) {
  if (!data || !healthy_) return false;
  const size_t row = static_cast<size_t>(packet->width) * 4;
  if (pitch < 0 || static_cast<size_t>(pitch) < row || row * packet->height > bridge::kPayload) {
    log_("xr bridge    : CPU image exceeds transport bounds"); healthy_ = false; return false;
  }
  for (uint32_t y = 0; y < packet->height; ++y)
    memcpy(packet->payload + y * row, static_cast<const uint8_t*>(data) + static_cast<size_t>(y) * pitch, row);
  packet->index = index; packet->payload_bytes = static_cast<uint32_t>(row * packet->height);
  return Call(bridge::EyePixels);
}
bool RemoteScreen::Card(int index, const CardLayer& card) {
  if (!healthy_) return false;
  auto& out = packet->card;
  out.visible = card.visible && card.pixels;
  out.texture_width = card.texture_width; out.texture_height = card.texture_height;
  memcpy(out.rect, card.rect, sizeof(out.rect)); memcpy(out.position, card.position, sizeof(out.position));
  memcpy(out.orientation, card.orientation, sizeof(out.orientation)); out.width = card.width; out.height = card.height;
  const uint64_t bytes = static_cast<uint64_t>(card.texture_width) * card.texture_height * 4;
  if (out.visible && bytes > bridge::kPayload) { healthy_ = false; log_("xr bridge    : card too large"); return false; }
  packet->payload_bytes = out.visible ? static_cast<uint32_t>(bytes) : 0;
  if (packet->payload_bytes) memcpy(packet->payload, card.pixels, packet->payload_bytes);
  packet->index = index; return Call(bridge::Card);
}
bool RemoteScreen::Shutdown() {
  if (!process_) return true;
  SetEvent(stop_);
  const bool exited = WaitForSingleObject(process_, 3000) == WAIT_OBJECT_0;
  if (!exited) {
    log_("xr bridge    : the host did not stop in 3 seconds. Ending the host started by this mod");
    TerminateProcess(process_, 1);
    WaitForSingleObject(process_, 1000);
  }
  DWORD code = 1;
  const bool clean = exited && GetExitCodeProcess(process_, &code) && code == 0;
  log_("xr bridge    : host shutdown %s", clean ? "ok" : "failed");
  CloseHandle(process_); process_ = nullptr;
  if (job_) { CloseHandle(job_); job_ = nullptr; }
  healthy_ = false;
  return clean;
}
}
