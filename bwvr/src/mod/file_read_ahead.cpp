// THE READ AHEAD OF THE FILES OF THE GAME (file_read_ahead, 1 is the
// default, 0 is the old behavior. A test key with test_keys=1: Shift Q).
//
// LHFile is the file class of the engine. LHFile::ReadData (0x007BE120)
// gives each read to ReadFile, with no buffer. The load of an animation of
// the Creature (CAnim::ReadBinary, 0x00860860) makes 1900 to 19,000 reads of
// some bytes each in one game turn, and each read is a call into the kernel.
// The game turn then takes 4 to 34 ms, also when Windows has the file in
// memory (backend.md, "The slow frames").
//
// The mod puts its own functions in five calls of LHFile to Windows. The
// instructions read the address of the function from the import table of the
// game. The mod changes only the address in each instruction to a slot of
// its own, so no other code of the game changes:
//
//   0x007BD5BD  LHFile::Open            mov edi, [CreateFileA]
//   0x007BE187  LHFile::ReadData        call [ReadFile]
//   0x007BDD38  LHFile::SetLHFilePointer  call [SetFilePointer]
//   0x007BD804  LHFile::VerifyFile      mov ebp, [SetFilePointer]
//   0x007BDBEE  LHFile::Close           call [CloseHandle]
//
// A file that LHFile::Open opens with GENERIC_READ and FILE_SHARE_READ (the
// read mode of LHFile, 2) gets a read ahead: a read of less than 64 KB comes
// from a copy of the next 256 KB of the file (file_read_ahead_math.h). The
// share mode lets nobody write the file while it is open, so the copy stays
// true. A file that LHFile opens to write (the modes 0 and 3, the saved
// games and the profile) gets no read ahead, and its calls go to Windows as
// they come. LHFile::WriteData is not changed.
//
// The parse of the file stays on the thread of the game, as before. Only the
// count of the calls into the kernel changes: one call of ReadFile for each
// 256 KB in place of one for each read of the game.
//
// Each function calls Windows through the import table of the game, so a
// hook of another module on that table stays in the chain.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "file_read_ahead_math.h"

void ModLog(const char* format, ...);

namespace {

// The import table of runblack.exe.
const uintptr_t kImportCreateFileA = 0x008A921C;
const uintptr_t kImportReadFile = 0x008A9218;
const uintptr_t kImportSetFilePointer = 0x008A9220;
const uintptr_t kImportCloseHandle = 0x008A92C4;

struct Site {
  uintptr_t at;       // the instruction
  uint8_t code[2];    // its opcode and its ModRM byte
  uintptr_t import;   // the address that it reads
  void** slot;        // the slot of the mod that it reads after the change
};

typedef HANDLE(WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD,
                                      LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef BOOL(WINAPI* ReadFileFn)(HANDLE, LPVOID, DWORD, LPDWORD,
                                 LPOVERLAPPED);
typedef DWORD(WINAPI* SetFilePointerFn)(HANDLE, LONG, PLONG, DWORD);
typedef BOOL(WINAPI* CloseHandleFn)(HANDLE);

CreateFileAFn RealCreateFileA() {
  return *reinterpret_cast<CreateFileAFn*>(kImportCreateFileA);
}
ReadFileFn RealReadFile() {
  return *reinterpret_cast<ReadFileFn*>(kImportReadFile);
}
SetFilePointerFn RealSetFilePointer() {
  return *reinterpret_cast<SetFilePointerFn*>(kImportSetFilePointer);
}
CloseHandleFn RealCloseHandle() {
  return *reinterpret_cast<CloseHandleFn*>(kImportCloseHandle);
}

// The calls into the real file, for file_read_ahead_math.h.
struct WindowsIo : read_ahead::Io {
  HANDLE file;
  explicit WindowsIo(HANDLE h) : file(h) {}
  bool Read(void* out, uint32_t bytes, uint32_t* got) override {
    DWORD n = 0;
    const BOOL ok = RealReadFile()(file, out, bytes, &n, nullptr);
    *got = n;
    return ok != FALSE;
  }
  uint32_t Seek(int32_t distance, uint32_t method) override {
    return RealSetFilePointer()(file, distance, nullptr, method);
  }
};

// The open files with a read ahead. A file that does not fit in the table
// goes to Windows as it comes.
const int kFiles = 64;
struct Entry {
  HANDLE file;  // null for a free entry
  read_ahead::File state;
};
Entry g_files[kFiles] = {};
SRWLOCK g_lock = SRWLOCK_INIT;

bool g_installed = false;
volatile LONG g_on = 0;  // the read ahead acts now
// For the report. Only the hooks write them, under g_lock.
uint32_t g_opened = 0;   // files that got an entry
uint32_t g_reads = 0;    // reads of the game on those files
uint32_t g_copies = 0;   // of them, reads from a copy
uint32_t g_fills = 0;    // ReadFile calls for a copy
uint32_t g_reported_reads = 0;
uint32_t g_reported_copies = 0;
uint32_t g_reported_fills = 0;

Entry* Find(HANDLE file) {
  for (int i = 0; i < kFiles; ++i) {
    if (g_files[i].file == file) return &g_files[i];
  }
  return nullptr;
}

// Takes the counts of one call into the totals of the report.
void Count(const read_ahead::File& before, const read_ahead::File& after) {
  g_reads += after.reads - before.reads;
  g_copies += after.copies - before.copies;
  g_fills += after.fills - before.fills;
}

HANDLE WINAPI h_CreateFileA(LPCSTR name, DWORD access, DWORD share,
                            LPSECURITY_ATTRIBUTES security, DWORD creation,
                            DWORD flags, HANDLE model) {
  const HANDLE file =
      RealCreateFileA()(name, access, share, security, creation, flags, model);
  if (file == INVALID_HANDLE_VALUE || access != GENERIC_READ ||
      share != FILE_SHARE_READ || (flags & FILE_FLAG_OVERLAPPED) != 0) {
    return file;
  }
  AcquireSRWLockExclusive(&g_lock);
  // A handle that Windows gives again after a close of the mod is old.
  Entry* e = Find(file);
  if (e == nullptr) e = Find(nullptr);
  if (e != nullptr) {
    uint8_t* chunk = e->state.chunk;
    memset(e, 0, sizeof(*e));
    e->file = file;
    e->state.chunk = chunk;
    ++g_opened;
  }
  ReleaseSRWLockExclusive(&g_lock);
  return file;
}

BOOL WINAPI h_ReadFile(HANDLE file, LPVOID out, DWORD bytes, LPDWORD got,
                       LPOVERLAPPED overlapped) {
  AcquireSRWLockExclusive(&g_lock);
  Entry* e = Find(file);
  if (e == nullptr) {
    ReleaseSRWLockExclusive(&g_lock);
    return RealReadFile()(file, out, bytes, got, overlapped);
  }
  WindowsIo io(file);
  if (g_on == 0 || overlapped != nullptr || got == nullptr) {
    read_ahead::Leave(&e->state, &io);
    ReleaseSRWLockExclusive(&g_lock);
    return RealReadFile()(file, out, bytes, got, overlapped);
  }
  if (e->state.chunk == nullptr) {
    e->state.chunk = static_cast<uint8_t*>(malloc(read_ahead::kChunkBytes));
  }
  const read_ahead::File before = e->state;
  uint32_t n = 0;
  const bool ok = read_ahead::Read(&e->state, &io, out, bytes, &n);
  *got = n;
  Count(before, e->state);
  ReleaseSRWLockExclusive(&g_lock);
  return ok ? TRUE : FALSE;
}

DWORD WINAPI h_SetFilePointer(HANDLE file, LONG distance, PLONG high,
                              DWORD method) {
  AcquireSRWLockExclusive(&g_lock);
  Entry* e = Find(file);
  if (e == nullptr) {
    ReleaseSRWLockExclusive(&g_lock);
    return RealSetFilePointer()(file, distance, high, method);
  }
  WindowsIo io(file);
  if (g_on == 0 || high != nullptr) {
    read_ahead::Leave(&e->state, &io);
    ReleaseSRWLockExclusive(&g_lock);
    return RealSetFilePointer()(file, distance, high, method);
  }
  const DWORD at = read_ahead::Seek(&e->state, &io, distance, method);
  ReleaseSRWLockExclusive(&g_lock);
  return at;
}

BOOL WINAPI h_CloseHandle(HANDLE file) {
  AcquireSRWLockExclusive(&g_lock);
  Entry* e = Find(file);
  if (e != nullptr) {
    free(e->state.chunk);
    memset(e, 0, sizeof(*e));
  }
  ReleaseSRWLockExclusive(&g_lock);
  return RealCloseHandle()(file);
}

void* g_slot_create = reinterpret_cast<void*>(&h_CreateFileA);
void* g_slot_read = reinterpret_cast<void*>(&h_ReadFile);
void* g_slot_seek = reinterpret_cast<void*>(&h_SetFilePointer);
void* g_slot_close = reinterpret_cast<void*>(&h_CloseHandle);

const Site kSites[] = {
    {0x007BD5BD, {0x8B, 0x3D}, kImportCreateFileA, &g_slot_create},
    {0x007BE187, {0xFF, 0x15}, kImportReadFile, &g_slot_read},
    {0x007BDD38, {0xFF, 0x15}, kImportSetFilePointer, &g_slot_seek},
    {0x007BD804, {0x8B, 0x2D}, kImportSetFilePointer, &g_slot_seek},
    {0x007BDBEE, {0xFF, 0x15}, kImportCloseHandle, &g_slot_close},
};
const int kSiteCount = sizeof(kSites) / sizeof(kSites[0]);

// True when the instruction reads the address `import`.
bool SiteIs(const Site& s, uintptr_t import) {
  const uint8_t* code = reinterpret_cast<const uint8_t*>(s.at);
  uint32_t address = 0;
  memcpy(&address, code + 2, 4);
  return code[0] == s.code[0] && code[1] == s.code[1] && address == import;
}

void WriteAddress(const Site& s, uintptr_t address) {
  uint8_t* code = reinterpret_cast<uint8_t*>(s.at);
  DWORD old = 0;
  VirtualProtect(code + 2, 4, PAGE_EXECUTE_READWRITE, &old);
  const uint32_t value = static_cast<uint32_t>(address);
  memcpy(code + 2, &value, 4);
  VirtualProtect(code + 2, 4, old, &old);
  FlushInstructionCache(GetCurrentProcess(), code, 6);
}

}  // namespace

// Puts the functions of the mod in the five calls of LHFile. on is the value
// of file_read_ahead. With test_keys=1 they go in also with the key at 0, so
// the test key can turn the read ahead on. Call it before the game opens a
// file, from the start of the DLL.
void FileReadAheadInstall(bool on, bool test_keys) {
  g_on = on ? 1 : 0;
  if (!on && !test_keys) {
    ModLog("read ahead   : file_read_ahead=0. Each read of LHFile goes to "
           "ReadFile, as the game makes it");
    return;
  }
  for (int i = 0; i < kSiteCount; ++i) {
    if (!SiteIs(kSites[i], kSites[i].import)) {
      ModLog("read ahead   : the code of LHFile at 0x%08X is not the code "
             "that the mod knows. No read ahead",
             static_cast<unsigned>(kSites[i].at));
      return;
    }
  }
  for (int i = 0; i < kSiteCount; ++i) {
    WriteAddress(kSites[i], reinterpret_cast<uintptr_t>(kSites[i].slot));
  }
  g_installed = true;
  ModLog("read ahead   : file_read_ahead=%d. A read of less than 64 KB from a "
         "file that LHFile opens to read comes from a copy of the next 256 KB "
         "of the file%s",
         on ? 1 : 0,
         on ? "" : ". The read ahead is off until the test key (Shift Q)");
}

// The test key (Shift Q, test_keys=1). Each open file gives its position
// back to Windows at its next call.
void FileReadAheadToggle(const char* keys) {
  if (!g_installed) {
    ModLog("read ahead   : test key (%s): the read ahead is not in, so the "
           "key does nothing", keys);
    return;
  }
  const LONG now = InterlockedXor(&g_on, 1) ^ 1;
  ModLog("read ahead   : test key (%s), file_read_ahead is %d now", keys,
         static_cast<int>(now));
}

// For the report of each 450 frames. A line only when the game read a file
// of LHFile in the window.
void FileReadAheadReport() {
  if (!g_installed) return;
  AcquireSRWLockExclusive(&g_lock);
  const uint32_t reads = g_reads - g_reported_reads;
  const uint32_t copies = g_copies - g_reported_copies;
  const uint32_t fills = g_fills - g_reported_fills;
  g_reported_reads = g_reads;
  g_reported_copies = g_copies;
  g_reported_fills = g_fills;
  const uint32_t opened = g_opened;
  ReleaseSRWLockExclusive(&g_lock);
  if (reads == 0) return;
  ModLog("read ahead   : file_read_ahead=%d. In the window the game read %u "
         "times from files of LHFile. %u reads came from a copy, with %u "
         "calls of ReadFile for the copies. %u files opened since the start",
         static_cast<int>(g_on), reads, copies, fills, opened);
}
