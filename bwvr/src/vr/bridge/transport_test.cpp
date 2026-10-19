#include "client.h"
#include <stdarg.h>
#include <stdio.h>
#include <cstring>
void Log(const char* format, ...) {
  va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
int main() {
  static_assert(sizeof(void*) == 4, "Run the client test in x86");
  bwvr::RemoteScreen client(Log);
  if (!client.Launch()) return 1;
  auto& p = *client.packet;
  for (int frame = 0; frame < 120; ++frame) {
    p.shared_handle = 900000000000ULL + frame;
    p.render_pose[1].valid = true;
    p.render_pose[1].position[0] = frame * .25f;
    p.render_pose[1].orientation[3] = 1;
    p.payload_bytes = 4096;
    memset(p.payload, frame, p.payload_bytes);
    if (!client.Call(bwvr::bridge::Probe) || p.result != 64 ||
        p.state.time != static_cast<int64_t>(p.shared_handle) || !p.state.eyes[0].valid ||
        p.state.eyes[0].position[0] != frame * .25f) return 2;
    for (uint32_t i = 0; i < p.payload_bytes; ++i)
      if (p.payload[i] != (static_cast<uint8_t>(frame) ^ 0x5a)) return 3;
  }
  if (!client.Shutdown()) return 6;
  bwvr::RemoteScreen invalid(Log);
  if (!invalid.Launch()) return 4;
  Log("Expect a protocol rejection in the next request.");
  invalid.packet->version = bwvr::bridge::kVersion + 1;
  if (invalid.Call(bwvr::bridge::Probe)) return 5;
  invalid.Shutdown();
  Log("PASS: the host rejected an incompatible protocol version.");
  Log("PASS: 120 x86 to x64 host requests, poses, 64 bit values and payloads. No headset used.");
  return 0;
}
