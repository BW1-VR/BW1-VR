// Tells if runblack.exe is the one build that the mod supports.
//
// Every address in bw_addresses.h is correct for one file only: runblack.exe
// from the unofficial v1.42 patch. The image base cannot tell the builds
// apart, because every Black & White executable loads at 0x00400000. So this
// reads the file from the disk and compares its SHA-1 with the known value.
//
// The mod and the launcher both use this file, so they always agree. The
// installer compares the same value (see installer/bwvr.iss).
//
// No DLL is loaded here, so the mod can call this from DllMain.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <string.h>

#include "bw_addresses.h"

namespace bwvr {

// One source for the value: bw_addresses.h. The installer has a copy.
constexpr uint64_t kGameExeSize = bw::kExpectedSize;
constexpr const char* kGameExeSha1 = bw::kExpectedSha1;

// Builds that are known and not supported, so that a message can name them.
struct KnownBuild {
  const char* sha1;
  const char* name;
};
constexpr KnownBuild kKnownBuilds[] = {
    {"85dc3180803153162a46d43433f8dcaa883af02c", "version 1.00"},
    {"800254f0c91684024cc51bc3f479192d7fec1068", "a changed version 1.00"},
    {"43ad8e54f51e0de9f97fcb884b37bf7d315c19d6", "a changed version 1.00"},
    {"3c7297dae357834860ff46a89ca5d1a552747100", "version 1.10"},
    {"c9bba68e17f5ec162ff235a496779f43090508db", "version 1.20"},
    {"a0fd87fdb1352fef65ba728ab732bcb1671f67e9", "version 1.30"},
};

enum class ExeVerdict {
  kSupported,   // the v1.42 file, byte for byte
  kOtherBuild,  // a different file: do not install hooks
  kUnreadable,  // the file cannot be read: do not install hooks
};

struct ExeReport {
  ExeVerdict verdict = ExeVerdict::kUnreadable;
  uint64_t size = 0;
  char sha1[41] = {0};         // lower case hex, empty if unreadable
  const char* build = nullptr;  // a name from kKnownBuilds, or null
  DWORD error = 0;              // GetLastError if unreadable
};

namespace detail {

// SHA-1 as in FIPS 180-4. Small and slow is good enough: it runs once, on
// 8.6 MB, and it needs nothing but the CPU.
struct Sha1 {
  uint32_t h[5];
  uint64_t bytes;
  uint8_t block[64];
  uint32_t used;
};

inline uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

inline void Sha1Block(uint32_t h[5], const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) |
           (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
           (static_cast<uint32_t>(p[4 * i + 2]) << 8) |
           static_cast<uint32_t>(p[4 * i + 3]);
  }
  for (int i = 16; i < 80; ++i) {
    w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    const uint32_t t = Rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = Rol(b, 30);
    b = a;
    a = t;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
}

inline void Sha1Start(Sha1* s) {
  s->h[0] = 0x67452301u;
  s->h[1] = 0xEFCDAB89u;
  s->h[2] = 0x98BADCFEu;
  s->h[3] = 0x10325476u;
  s->h[4] = 0xC3D2E1F0u;
  s->bytes = 0;
  s->used = 0;
}

inline void Sha1Add(Sha1* s, const uint8_t* data, uint32_t n) {
  s->bytes += n;
  while (n > 0) {
    if (s->used == 0 && n >= 64) {
      Sha1Block(s->h, data);
      data += 64;
      n -= 64;
      continue;
    }
    uint32_t take = 64 - s->used;
    if (take > n) take = n;
    memcpy(s->block + s->used, data, take);
    s->used += take;
    data += take;
    n -= take;
    if (s->used == 64) {
      Sha1Block(s->h, s->block);
      s->used = 0;
    }
  }
}

inline void Sha1Finish(Sha1* s, char hex[41]) {
  const uint64_t bits = s->bytes * 8;
  const uint8_t one = 0x80;
  const uint8_t zero = 0;
  Sha1Add(s, &one, 1);
  while (s->used != 56) Sha1Add(s, &zero, 1);
  uint8_t length[8];
  for (int i = 0; i < 8; ++i) {
    length[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
  }
  Sha1Add(s, length, 8);
  static const char kDigits[] = "0123456789abcdef";
  for (int i = 0; i < 20; ++i) {
    const uint8_t byte = static_cast<uint8_t>(s->h[i / 4] >> (24 - 8 * (i % 4)));
    hex[2 * i] = kDigits[byte >> 4];
    hex[2 * i + 1] = kDigits[byte & 15];
  }
  hex[40] = 0;
}

}  // namespace detail

// Reads the whole file once. About 10 to 40 ms for 8.6 MB.
inline ExeReport CheckGameExe(const wchar_t* path) {
  ExeReport report;
  HANDLE file = CreateFileW(
      path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    report.error = GetLastError();
    return report;
  }
  LARGE_INTEGER size = {};
  GetFileSizeEx(file, &size);
  report.size = static_cast<uint64_t>(size.QuadPart);

  const DWORD kChunk = 64 * 1024;
  uint8_t* buffer =
      static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, kChunk));
  if (buffer == nullptr) {
    report.error = ERROR_NOT_ENOUGH_MEMORY;
    CloseHandle(file);
    return report;
  }
  detail::Sha1 sha;
  detail::Sha1Start(&sha);
  bool ok = true;
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(file, buffer, kChunk, &got, nullptr)) {
      report.error = GetLastError();
      ok = false;
      break;
    }
    if (got == 0) break;
    detail::Sha1Add(&sha, buffer, got);
  }
  HeapFree(GetProcessHeap(), 0, buffer);
  CloseHandle(file);
  if (!ok) return report;

  detail::Sha1Finish(&sha, report.sha1);
  for (const KnownBuild& known : kKnownBuilds) {
    if (strcmp(report.sha1, known.sha1) == 0) report.build = known.name;
  }
  report.verdict = (report.size == kGameExeSize &&
                    strcmp(report.sha1, kGameExeSha1) == 0)
                       ? ExeVerdict::kSupported
                       : ExeVerdict::kOtherBuild;
  return report;
}

}  // namespace bwvr
