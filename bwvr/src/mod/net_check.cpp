// The game's check for an internet connection (docs/intro.md, "The game
// stops after the logos: the check for an internet connection").
//
// LHMultiplayerR.dll has the function LHCheckForInternetConnection. The game
// calls it on its main thread when the logo screens are over. The function
// loads rasapi32.dll and calls RasEnumConnectionsA and RasEnumDevicesA. They
// ask the Remote Access Connection Manager of Windows (the service RasMan)
// for the dial-up and VPN connections of the PC and for its modems. Each
// call is an RPC with no time limit.
// When the service does not answer, the game waits forever: its window keeps
// the last logo, and Windows shows "not responding". This was seen on
// 2026-10-07. The stack of the main thread was
//
//     LHMultiplayerR!LHCheckForInternetConnection
//     rasapi32!RasEnumConnectionsA
//     RASMAN!RasConnectionEnum
//     RPCRT4!NdrClientCall2 ... ntdll!NtAlpcSendWaitReceivePort
//
// and rasdial.exe of Windows waited in the same way, with no game.
//
// The game needs the answer only for its internet features of 2001 (the
// weather of the real world, its servers). Those have no server now. So the
// mod gives the answers itself: no connection and no modem. That is what
// Windows answers on a PC with no dial-up connection and no VPN, which is
// the usual PC.
//
//     net_check=0   the mod answers "no connection" at once (the default)
//     net_check=1   the call of the game goes to Windows, as in the plain game
//
// HOW. LHMultiplayerR.dll does not import rasapi32.dll. It loads the DLL and
// asks for the function by name. So the mod puts its own function in the
// import slot of GetProcAddress of LHMultiplayerR.dll only. The DLL asks
// for four names: RasEnumConnections, RasEnumConnectionsA, RasEnumDevices and
// RasEnumDevicesA. For each of them it gets an answer function of the mod.
// Each other name goes to the real GetProcAddress. This writes one pointer,
// loads no DLL, and is safe before the game starts.

#include <windows.h>
#include <stdint.h>
#include <string.h>

extern void ModLog(const char* format, ...);

namespace {

typedef FARPROC(WINAPI* GetProcAddressFn)(HMODULE, LPCSTR);

GetProcAddressFn g_real_get_proc = nullptr;
bool g_installed = false;
bool g_missing_logged = false;
volatile LONG g_connection_answers = 0;
volatile LONG g_device_answers = 0;

// RasEnumConnectionsA: no connection. The caller gives the size of its buffer
// in *bytes and gets the size that is needed, which is 0.
DWORD WINAPI NoConnections(void* /*connections*/, DWORD* bytes, DWORD* count) {
  if (bytes != nullptr) *bytes = 0;
  if (count != nullptr) *count = 0;
  if (InterlockedIncrement(&g_connection_answers) == 1) {
    ModLog("net check    : the game asked Windows for its dial-up and VPN "
           "connections (RasEnumConnections). The mod answered: none "
           "(net_check=0)");
  }
  return ERROR_SUCCESS;
}

// RasEnumDevicesA: no modem and no other device. The same form of call.
DWORD WINAPI NoDevices(void* /*devices*/, DWORD* bytes, DWORD* count) {
  if (bytes != nullptr) *bytes = 0;
  if (count != nullptr) *count = 0;
  if (InterlockedIncrement(&g_device_answers) == 1) {
    ModLog("net check    : the game asked Windows for its modems "
           "(RasEnumDevices). The mod answered: none (net_check=0)");
  }
  return ERROR_SUCCESS;
}

FARPROC WINAPI h_GetProcAddress(HMODULE module, LPCSTR name) {
  // A name below 0x10000 is an ordinal, not a string.
  if (reinterpret_cast<uintptr_t>(name) > 0xFFFF) {
    if (strcmp(name, "RasEnumConnectionsA") == 0 ||
        strcmp(name, "RasEnumConnections") == 0) {
      return reinterpret_cast<FARPROC>(&NoConnections);
    }
    if (strcmp(name, "RasEnumDevicesA") == 0 ||
        strcmp(name, "RasEnumDevices") == 0) {
      return reinterpret_cast<FARPROC>(&NoDevices);
    }
  }
  return g_real_get_proc(module, name);
}

// The slot of an import by name of a module, or null.
void** ImportSlotOf(HMODULE module, const char* dll, const char* function) {
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
      if (names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG32) continue;
      const IMAGE_IMPORT_BY_NAME* by_name =
          reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
              base + names[i].u1.AddressOfData);
      if (strcmp(reinterpret_cast<const char*>(by_name->Name), function) ==
          0) {
        return &slots[i];
      }
    }
  }
  return nullptr;
}

}  // namespace

// Puts the answer of the mod in place. own_answer false leaves the game's
// call as it is. This can run more than one time: the first time is before
// the game starts, and a later call is a second try if the DLL was not there.
void NetCheckInstall(bool own_answer) {
  if (g_installed) return;
  if (!own_answer) {
    g_installed = true;
    ModLog("net check    : net_check=1. The game asks Windows for an internet "
           "connection itself. If the game stops after the logos, set "
           "net_check=0");
    return;
  }
  const HMODULE multiplayer = GetModuleHandleA("LHMultiplayerR.dll");
  void** slot = multiplayer != nullptr
                    ? ImportSlotOf(multiplayer, "KERNEL32.dll",
                                   "GetProcAddress")
                    : nullptr;
  if (slot == nullptr) {
    if (!g_missing_logged) {
      g_missing_logged = true;
      ModLog("net check    : %s, so the check of the game for an internet "
             "connection stays as it is for now",
             multiplayer == nullptr
                 ? "LHMultiplayerR.dll is not loaded yet"
                 : "LHMultiplayerR.dll has no import of GetProcAddress");
    }
    return;
  }
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    ModLog("net check    : the import slot cannot be written (error %lu), so "
           "the check of the game stays as it is",
           GetLastError());
    g_installed = true;
    return;
  }
  g_real_get_proc = reinterpret_cast<GetProcAddressFn>(*slot);
  *slot = reinterpret_cast<void*>(&h_GetProcAddress);
  VirtualProtect(slot, sizeof(void*), old, &old);
  g_installed = true;
  ModLog("net check    : the mod answers the game's check for an internet "
         "connection itself: no connection and no modem (net_check=0). The "
         "game cannot wait for the Remote Access service of Windows");
}
