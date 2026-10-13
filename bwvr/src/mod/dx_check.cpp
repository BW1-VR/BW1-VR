// The game's check for DirectX 7 (docs/intro.md, "The DirectX check").
//
// At its start the game asks DSETUP.dll of its folder for the version of
// DirectX (DirectXSetupGetVersion, ordinal 11) and stops with "You need
// DirectX 7 or greater" when the answer is below 4.07 (runblack.exe
// 0x00641CD8). DSETUP.dll opens the key HKLM\SOFTWARE\Microsoft\DirectX with
// all access to read the version there. With no administrator rights
// Windows refuses that access, also with the virtual store of UAC. DSETUP.dll
// then takes the version of the file ddraw.dll of Windows, which is 4.06. So
// the game stops when it does not run as administrator.
//
// Each Windows that runs the mod has DirectX 9, because dxwrapper needs it.
// So the mod gives the answer that Windows gives with administrator rights:
// 4.09.00.0904.
//
//     dx_check=0   the mod answers the check (the default)
//     dx_check=1   the check goes to DSETUP.dll, as in the plain game
//
// HOW. runblack.exe imports the function from DSETUP.dll by ordinal. The mod
// puts its own function in that import slot of runblack.exe only. It writes
// one pointer and loads no DLL. The slot must already point into
// DSETUP.dll, so the loader has filled it. If it does not yet, Startup
// tries again.

#include <windows.h>
#include <stdint.h>
#include <string.h>

extern void ModLog(const char* format, ...);

namespace {

const WORD kGetVersionOrdinal = 11;  // DirectXSetupGetVersion
const DWORD kVersion = 0x00040009;    // 4.09
const DWORD kRevision = 0x00000384;   // 00.0904

bool g_installed = false;
bool g_not_ready_logged = false;
volatile LONG g_answers = 0;

INT WINAPI OwnVersion(DWORD* version, DWORD* revision) {
  if (version != nullptr) *version = kVersion;
  if (revision != nullptr) *revision = kRevision;
  if (InterlockedIncrement(&g_answers) == 1) {
    ModLog("dx check     : the game asked DSETUP.dll for the version of "
           "DirectX. The mod answered 4.09.00.0904 (dx_check=0)");
  }
  return TRUE;
}

// The slot of an import by ordinal of a module, or null.
void** ImportSlotByOrdinal(HMODULE module, const char* dll, WORD ordinal) {
  BYTE* base = reinterpret_cast<BYTE*>(module);
  const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
  const IMAGE_NT_HEADERS32* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
  const IMAGE_DATA_DIRECTORY& dir =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (dir.VirtualAddress == 0) return nullptr;
  const IMAGE_IMPORT_DESCRIPTOR* d =
      reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base +
                                                       dir.VirtualAddress);
  for (; d->Name != 0; ++d) {
    if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0) {
      continue;
    }
    const DWORD names_rva =
        d->OriginalFirstThunk != 0 ? d->OriginalFirstThunk : d->FirstThunk;
    const IMAGE_THUNK_DATA32* names =
        reinterpret_cast<const IMAGE_THUNK_DATA32*>(base + names_rva);
    void** slots = reinterpret_cast<void**>(base + d->FirstThunk);
    for (int i = 0; names[i].u1.AddressOfData != 0; ++i) {
      if ((names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG32) != 0 &&
          IMAGE_ORDINAL32(names[i].u1.Ordinal) == ordinal) {
        return &slots[i];
      }
    }
  }
  return nullptr;
}

}  // namespace

void DxCheckInstall(bool own_answer) {
  if (g_installed) return;
  if (!own_answer) {
    g_installed = true;
    ModLog("dx check     : dx_check=1. DSETUP.dll gives the game the version "
           "of DirectX. With no administrator rights it gives 4.06, and the "
           "game stops with \"You need DirectX 7 or greater\"");
    return;
  }
  const HMODULE dsetup = GetModuleHandleA("DSETUP.dll");
  void** slot = ImportSlotByOrdinal(GetModuleHandleA(nullptr), "DSETUP.dll",
                                    kGetVersionOrdinal);
  // The loader has filled the slot when it points into DSETUP.dll.
  const FARPROC real =
      dsetup != nullptr
          ? GetProcAddress(dsetup, MAKEINTRESOURCEA(kGetVersionOrdinal))
          : nullptr;
  if (slot == nullptr || real == nullptr ||
      *slot != reinterpret_cast<void*>(real)) {
    if (!g_not_ready_logged) {
      g_not_ready_logged = true;
      ModLog("dx check     : %s, so the check of the game for DirectX stays "
             "as it is for now",
             slot == nullptr    ? "runblack.exe has no import of ordinal 11 "
                                  "of DSETUP.dll"
             : dsetup == nullptr ? "DSETUP.dll is not loaded yet"
                                 : "the import slot is not filled yet");
    }
    return;
  }
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    ModLog("dx check     : the import slot cannot be written (error %lu), so "
           "the check of the game stays as it is",
           GetLastError());
    g_installed = true;
    return;
  }
  *slot = reinterpret_cast<void*>(&OwnVersion);
  VirtualProtect(slot, sizeof(void*), old, &old);
  g_installed = true;
  ModLog("dx check     : the mod answers the game's check for DirectX itself "
         "(dx_check=0), so the game starts with no administrator rights too");
}
