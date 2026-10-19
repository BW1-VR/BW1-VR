// The read ahead of a file of the game, a test (file_read_ahead_math.h).
// main.cpp includes this file after Check and Near, just before main.
#pragma once

#include <vector>

#include "../../src/mod/file_read_ahead_math.h"

namespace read_ahead_checks {

// A file in memory with the answers of ReadFile and SetFilePointer.
struct MemoryFile : read_ahead::Io {
  std::vector<uint8_t> data;
  uint32_t at = 0;
  uint32_t read_calls = 0;
  uint32_t seek_calls = 0;
  bool fail_reads = false;
  bool Read(void* out, uint32_t bytes, uint32_t* got) override {
    ++read_calls;
    *got = 0;
    if (fail_reads) return false;
    const uint32_t size = static_cast<uint32_t>(data.size());
    const uint32_t left = at < size ? size - at : 0;
    const uint32_t n = bytes < left ? bytes : left;
    if (n > 0) memcpy(out, &data[at], n);
    at += n;
    *got = n;
    return true;
  }
  uint32_t Seek(int32_t distance, uint32_t method) override {
    ++seek_calls;
    const int64_t base = method == read_ahead::kBegin     ? 0
                         : method == read_ahead::kCurrent ? at
                                                          : data.size();
    const int64_t next = base + distance;
    if (method > read_ahead::kEnd || next < 0 || next >= 0xFFFFFFFFll) {
      return read_ahead::kBadPosition;
    }
    at = static_cast<uint32_t>(next);
    return at;
  }
};

inline uint32_t Next(uint32_t* seed) {
  *seed = *seed * 1664525u + 1013904223u;
  return *seed >> 8;
}

inline void ReadAheadChecks() {
  using namespace read_ahead;
  printf("\nThe read ahead of a file of the game\n");

  const uint32_t size = 600 * 1024 + 123;
  MemoryFile plain, ahead;
  plain.data.resize(size);
  for (uint32_t i = 0; i < size; ++i) {
    plain.data[i] = static_cast<uint8_t>((i * 31u) ^ (i >> 9));
  }
  ahead.data = plain.data;
  std::vector<uint8_t> chunk(kChunkBytes);
  File f = {};
  f.chunk = chunk.data();

  // Small reads from the start: one ReadFile for a chunk.
  {
    uint8_t a[16], b[16];
    uint32_t ga = 0, gb = 0;
    bool same = true;
    for (int i = 0; i < 1000; ++i) {
      const bool ra = plain.Read(a, 16, &ga);
      const bool rb = Read(&f, &ahead, b, 16, &gb);
      if (ra != rb || ga != gb || memcmp(a, b, ga) != 0) same = false;
    }
    Check(same, "1000 reads of 16 bytes give the bytes of the file", 0, 0);
    Near("1000 reads of 16 bytes are one ReadFile", ahead.read_calls, 1, 0);
    Near("the position that the game sees is 16000",
         Seek(&f, &ahead, 0, kCurrent), 16000, 0);
  }

  // A random row of calls against the file with no read ahead.
  uint32_t seed = 12345;
  int differ = 0;
  for (int i = 0; i < 20000; ++i) {
    const uint32_t kind = Next(&seed) % 10;
    if (kind < 7) {
      uint32_t bytes = Next(&seed) % 300;
      if (kind == 6) bytes = 60000 + Next(&seed) % 20000;  // some direct
      std::vector<uint8_t> a(bytes + 1), b(bytes + 1);
      uint32_t ga = 7, gb = 9;
      const bool ra = plain.Read(a.data(), bytes, &ga);
      const bool rb = Read(&f, &ahead, b.data(), bytes, &gb);
      if (ra != rb || ga != gb || memcmp(a.data(), b.data(), ga) != 0) {
        ++differ;
      }
    } else {
      const uint32_t method = Next(&seed) % 3;
      int32_t distance = static_cast<int32_t>(Next(&seed) % (size + 4000));
      if (method != kBegin) distance -= static_cast<int32_t>(size / 2);
      if (Next(&seed) % 50 == 0) distance = -5;  // an error of Windows
      const uint32_t pa = plain.Seek(distance, method);
      const uint32_t pb = Seek(&f, &ahead, distance, method);
      if (pa != pb) ++differ;
    }
  }
  Near("20000 random reads and seeks give the answers of the file with no "
       "read ahead", differ, 0, 0);
  Leave(&f, &ahead);
  Near("after Leave the file pointer of Windows is the position of the game",
       ahead.at, plain.at, 0);

  // The reads of a parse: small reads one after the other from the start,
  // and a short skip forward after some of them.
  {
    File g = {};
    g.chunk = chunk.data();
    MemoryFile parse, check;
    parse.data = plain.data;
    check.data = plain.data;
    uint32_t s = 777;
    int wrong = 0;
    uint32_t reads = 0;
    while (check.at < size) {
      uint8_t a[40], b[40];
      const uint32_t bytes = 1 + Next(&s) % 40;
      uint32_t ga = 0, gb = 0;
      const bool ra = check.Read(a, bytes, &ga);
      const bool rb = Read(&g, &parse, b, bytes, &gb);
      ++reads;
      if (ra != rb || ga != gb || memcmp(a, b, ga) != 0) ++wrong;
      if (Next(&s) % 20 == 0) {
        if (check.Seek(100, kCurrent) != Seek(&g, &parse, 100, kCurrent)) {
          ++wrong;
        }
      }
    }
    Near("a parse of the whole file gives the bytes of the file", wrong, 0,
         0);
    Check(reads > 20000, "the parse makes more than 20000 reads", reads,
          20000);
    Check(parse.read_calls <= 4,
          "and 4 calls of ReadFile at most (3 chunks and the end)",
          parse.read_calls, 4);
  }

  // A read at the end of the file gives the bytes that are left.
  {
    Seek(&f, &ahead, -10, kEnd);
    plain.Seek(-10, kEnd);
    uint8_t a[64], b[64];
    uint32_t ga = 0, gb = 0;
    plain.Read(a, 64, &ga);
    const bool rb = Read(&f, &ahead, b, 64, &gb);
    Check(rb && gb == 10 && ga == 10 && memcmp(a, b, 10) == 0,
          "a read over the end gives the last 10 bytes", gb, ga);
    Check(Read(&f, &ahead, b, 64, &gb) && gb == 0,
          "a read at the end gives 0 bytes and true", gb, 0);
  }

  // A seek before the start fails and does not move the position.
  {
    Seek(&f, &ahead, 100, kBegin);
    Near("a seek to -1 fails", Seek(&f, &ahead, -1, kBegin), kBadPosition, 0);
    Near("the position stays at 100", Seek(&f, &ahead, 0, kCurrent), 100, 0);
  }

  // A failed ReadFile of a chunk gives the failure to the game.
  {
    File g = {};
    g.chunk = chunk.data();
    MemoryFile bad;
    bad.data = plain.data;
    bad.fail_reads = true;
    uint8_t b[8];
    uint32_t gb = 5;
    Check(!Read(&g, &bad, b, 8, &gb) && gb == 0,
          "a failed read gives false and 0 bytes", gb, 0);
  }

  // With no chunk each read goes to Windows.
  {
    File g = {};
    MemoryFile m;
    m.data = plain.data;
    uint8_t b[8];
    uint32_t gb = 0;
    for (int i = 0; i < 10; ++i) Read(&g, &m, b, 8, &gb);
    Near("with no chunk 10 reads are 10 ReadFile calls", m.read_calls, 10, 0);
    Near("and the file is at 80", m.at, 80, 0);
  }
}

}  // namespace read_ahead_checks
