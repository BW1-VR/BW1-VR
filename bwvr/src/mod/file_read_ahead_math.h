// THE READ AHEAD OF A FILE OF THE GAME (file_read_ahead.cpp). Pure logic, so
// the checks in test/controller_math can run it against a file in memory.
//
// LHFile::ReadData of the game gives each read to ReadFile. The load of an
// animation of the Creature makes 1900 to 19,000 reads of some bytes each.
// Each call goes into the kernel and takes 1.2 to 1.5 microseconds when
// Windows has the file in memory, so one load holds the game turn for up to
// 34 ms. Here a read of less than kDirectBytes comes from a copy of the next
// kChunkBytes of the file, and the file gets one ReadFile for each chunk.
//
// The position of the file that the game sees is virtual (virt). The file
// pointer of Windows (real) is at the end of the last chunk. A seek with
// FILE_BEGIN or FILE_CURRENT only sets virt. Each other call first puts real
// at virt, so the game gets the same bytes, the same counts and the same
// results as from Windows. This is correct only for a file that nobody
// writes while it is open: file_read_ahead.cpp takes only a file that
// LHFile::Open opened with GENERIC_READ and FILE_SHARE_READ.
//
// The checks compare each answer with a file that has no read ahead.
#pragma once

#include <stdint.h>
#include <string.h>

namespace read_ahead {

const uint32_t kChunkBytes = 256 * 1024;
const uint32_t kDirectBytes = 64 * 1024;
const uint32_t kBegin = 0;    // FILE_BEGIN
const uint32_t kCurrent = 1;  // FILE_CURRENT
const uint32_t kEnd = 2;      // FILE_END
const uint32_t kBadPosition = 0xFFFFFFFFu;  // INVALID_SET_FILE_POINTER
const uint32_t kMaxPosition = 0x7FFFFFFFu;  // the most for a virtual position

// The calls into the real file. Read is ReadFile with no OVERLAPPED. Seek is
// SetFilePointer with no high part: the new position, or kBadPosition when
// it fails (with the last error of Windows).
struct Io {
  virtual bool Read(void* out, uint32_t bytes, uint32_t* got) = 0;
  virtual uint32_t Seek(int32_t distance, uint32_t method) = 0;
  virtual ~Io() {}
};

struct File {
  bool known;         // virt and real are known
  uint32_t virt;      // the position that the game sees
  uint32_t real;      // the file pointer of Windows
  uint8_t* chunk;     // kChunkBytes, or null
  uint32_t chunk_at;  // the position of chunk[0]
  uint32_t chunk_len; // the bytes in chunk, 0 for no chunk
  // For the report.
  uint32_t reads;     // reads that the game made
  uint32_t copies;    // of them, reads from the chunk
  uint32_t fills;     // ReadFile calls for a chunk
};

inline void Reset(File* f) {
  uint8_t* chunk = f->chunk;
  memset(f, 0, sizeof(*f));
  f->chunk = chunk;
}

// Puts the file pointer of Windows at virt. False when the seek fails.
inline bool Sync(File* f, Io* io) {
  if (!f->known || f->real == f->virt) return true;
  const uint32_t at = io->Seek(static_cast<int32_t>(f->virt), kBegin);
  if (at == kBadPosition) {
    f->known = false;
    f->chunk_len = 0;
    return false;
  }
  f->real = at;
  return true;
}

// Learns the position from Windows, at the first call or after a call that
// did not keep it.
inline bool Learn(File* f, Io* io) {
  if (f->known) return true;
  const uint32_t at = io->Seek(0, kCurrent);
  if (at == kBadPosition || at > kMaxPosition) return false;
  f->known = true;
  f->virt = at;
  f->real = at;
  f->chunk_len = 0;
  return true;
}

// Ends the read ahead of the file: the file pointer of Windows goes to the
// position that the game sees, and the next call learns it again. Call it
// before a call that goes to Windows with no read ahead.
inline void Leave(File* f, Io* io) {
  Sync(f, io);
  f->known = false;
  f->chunk_len = 0;
}

// ReadFile with no OVERLAPPED. chunk must have kChunkBytes when it is not
// null. With no chunk each read goes to Windows.
inline bool Read(File* f, Io* io, void* out, uint32_t bytes, uint32_t* got) {
  *got = 0;
  ++f->reads;
  if (!Learn(f, io) || f->chunk == nullptr || bytes >= kDirectBytes) {
    Leave(f, io);
    return io->Read(out, bytes, got);
  }
  const bool inside = f->chunk_len > 0 && f->virt >= f->chunk_at &&
                      f->virt - f->chunk_at <= f->chunk_len &&
                      f->chunk_len - (f->virt - f->chunk_at) >= bytes;
  if (!inside) {
    // The end of a chunk must be a position that Sync can give to Windows.
    if (!Sync(f, io) || f->virt > kMaxPosition - kChunkBytes) {
      Leave(f, io);
      return io->Read(out, bytes, got);
    }
    uint32_t filled = 0;
    if (!io->Read(f->chunk, kChunkBytes, &filled) || filled > kChunkBytes) {
      // The read of Windows for the game gives the error to the game.
      const uint32_t want = f->virt;
      f->known = false;
      f->chunk_len = 0;
      io->Seek(static_cast<int32_t>(want), kBegin);
      return io->Read(out, bytes, got);
    }
    ++f->fills;
    f->chunk_at = f->virt;
    f->chunk_len = filled;
    f->real = f->virt + filled;
  }
  // At the end of the file the chunk has fewer bytes, as ReadFile gives.
  const uint32_t offset = f->virt - f->chunk_at;
  const uint32_t left = f->chunk_len - offset;
  const uint32_t n = bytes < left ? bytes : left;
  memcpy(out, f->chunk + offset, n);
  f->virt += n;
  *got = n;
  ++f->copies;
  return true;
}

// SetFilePointer with no high part.
inline uint32_t Seek(File* f, Io* io, int32_t distance, uint32_t method) {
  if (Learn(f, io) && (method == kBegin || method == kCurrent)) {
    const int64_t base = method == kBegin ? 0 : static_cast<int64_t>(f->virt);
    const int64_t at = base + distance;
    // A position before 0 is an error of Windows. A position of 2 GB or more
    // goes to Windows too, because Sync gives a position as a signed
    // distance from the start.
    if (at >= 0 && at <= static_cast<int64_t>(kMaxPosition)) {
      f->virt = static_cast<uint32_t>(at);
      return f->virt;
    }
  }
  Leave(f, io);
  const uint32_t at = io->Seek(distance, method);
  if (at != kBadPosition && at <= kMaxPosition) {
    f->known = true;
    f->virt = at;
    f->real = at;
  }
  return at;
}

}  // namespace read_ahead
