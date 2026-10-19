#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include "protocol.h"

namespace bwvr {
class RemoteScreen {
 public:
  explicit RemoteScreen(LogFn log) : log_(log) {}
  ~RemoteScreen();
  bool Launch();
  bool Call(bridge::Command command);
  bool MakeDevice();
  bool Texture(void* texture, int index, bool panel);
  bool Pixels(int index, const void* data, int pitch);
  bool Card(int index, const CardLayer& card);
  bool Shutdown();
  void SetLog(LogFn log) { log_ = log; }
  bool healthy() const { return healthy_; }
  bool instance_ready = false;
  bridge::Packet* packet = nullptr;
  ID3D11Device* device = nullptr;
 private:
  LogFn log_;
  HANDLE mapping_ = nullptr, request_ = nullptr, ready_ = nullptr, stop_ = nullptr, process_ = nullptr, job_ = nullptr;
  uint32_t sequence_ = 0;
  bool healthy_ = true;
};
}
