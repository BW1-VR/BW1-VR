// The sound that goes to the ears (docs/audio.md, "7. The mixer of the mod").
//
// QMixer mixes all its channels to one stream and gives each block of it to
// the wave device with waveOutWrite. This file puts its own function in the
// import table of QMixer.dll for waveOutWrite and waveOutOpen. So the mod
// sees each block just before the device gets it. That has two uses:
//
// - The mixer of the mod (audio_voices.cpp) adds its sound to the block. The
//   mod needs no sound device of its own, and its sound has the latency of
//   the game's sound.
// - A desktop run writes the blocks to bwvr-audio.wav in the game folder
//   (audio_capture=1). tools/audio_measure.py measures the file: the level
//   of each ear, the spectrum, and the difference between the ears. A desktop
//   run has no listener, and this is how a change of the sound gets a proof.
//
// Only QMixer.dll is patched, so no other sound of the process comes here
// (the videos of the game play through their own code). The Windows sound
// settings do not change.
//
// The mod changes a block only when it is 16 bit stereo PCM. With another
// format, or when QMixer does not use the wave device, the mixer of the mod
// stays off and the log gives the reason.
//
// The hook runs on the thread of QMixer that mixes. The frame count is the
// clock of the capture: a log line that gives it lets the script find the
// place of an event in the file.

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <intrin.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "MinHook.h"
#include "audio.h"

void ModLog(const char* format, ...);

namespace {

// IDirectSoundBuffer::Unlock. QMixer locks a part of its buffer, mixes into
// it and unlocks it. The two parts are the block.
typedef HRESULT(STDMETHODCALLTYPE* UnlockFn)(IDirectSoundBuffer*, void*,
                                             DWORD, void*, DWORD);
// QSWaveMixGetDirectSound(session, &object). stdcall, ret 8.
typedef DWORD(WINAPI* GetDirectSoundFn)(void*, IDirectSound**);
const int kUnlockSlot = 19;   // in the vtable of IDirectSoundBuffer
const int kLockSlot = 11;
UnlockFn o_Unlock[2] = {nullptr, nullptr};
void* g_unlock_code[2] = {nullptr, nullptr};
// IDirectSoundBuffer::Lock. Its offset says where QMixer writes, and the
// play cursor says what the device plays now. The distance between the two
// is the time by which the stream is ahead of the ears.
typedef HRESULT(STDMETHODCALLTYPE* LockFn)(IDirectSoundBuffer*, DWORD, DWORD,
                                           void**, DWORD*, void**, DWORD*,
                                           DWORD);
LockFn o_Lock[2] = {nullptr, nullptr};
void* g_lock_code[2] = {nullptr, nullptr};
DWORD g_lock_offset = 0;
bool g_lock_known = false;
DWORD g_buffer_bytes = 0;
double g_lead_sum_ms = 0.0;
float g_lead_most_ms = 0.0f;
float g_lead_least_ms = 1e9f;
LONG g_lead_count = 0;
LONG g_lead_blocks = 0;     // blocks measured since the stream started
volatile LONG g_passed = 0; // times the device passed the place of a write

// QSWaveMixInitEx(config). stdcall, ret 4. The config has 0x40 bytes: the
// size, the flags, the rate at +0x08, the channels at +0x14, the output at
// +0x18 and the latency in ms at +0x1C (LHaudiodllR.dll fills it at RVA
// 0x1589E to 0x15982, and 0 lets QMixer choose).
typedef void*(WINAPI* InitExFn)(void*);
InitExFn o_InitEx = nullptr;
int g_latency_ms = 0;   // audio_latency_ms, 0 leaves QMixer's own
const int kConfigLatency = 0x1C;
IDirectSoundBuffer* g_ds_buffer = nullptr;
bool g_ds_tried = false;
uintptr_t g_mixer_lo = 0, g_mixer_hi = 0;   // the image of QMixer.dll

typedef MMRESULT(WINAPI* WaveOutOpenFn)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX,
                                        DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT(WINAPI* WaveOutWriteFn)(HWAVEOUT, LPWAVEHDR, UINT);

typedef void(WINAPI* DebugStringFn)(LPCSTR);

WaveOutOpenFn o_Open = nullptr;
WaveOutWriteFn o_Write = nullptr;
DebugStringFn o_DebugString = nullptr;

// What QMixer says about its output, kept for the log. The hook can run on
// a thread of QMixer, so the game thread writes the lines.
const int kSaid = 8;
char g_said[kSaid][160];
volatile LONG g_said_count = 0;
LONG g_said_logged = 0;
// The last call of waveOutOpen, for the log.
volatile LONG g_open_calls = 0;
LONG g_open_logged = 0;
char g_open_text[200];
DWORD g_first_frame_tick = 0;
bool g_no_blocks_logged = false;

bool g_patched = false;
bool g_capture = false;         // audio_capture
bool g_report = false;          // a line each second (audio_trace)

// The format of the stream. From waveOutOpen, or a guess from the byte rate
// when QMixer opened the device before the patch.
volatile LONG g_rate = 0;
volatile LONG g_good_format = 0;   // 16 bit stereo PCM
bool g_format_from_open = false;
bool g_format_logged = false;

AudioRenderFn g_render = nullptr;

volatile LONG g_frames = 0;        // frames given to the device so far
volatile LONG g_blocks = 0;

// For the guess of the rate and for the report.
DWORD g_window_tick = 0;
LONG g_window_bytes = 0;
LONG g_window_blocks = 0;
double g_window_square[2] = {0.0, 0.0};
int g_window_peak[2] = {0, 0};
LONG g_window_frames = 0;
bool g_report_due = false;
char g_report_text[256];

// The capture file.
HANDLE g_file = INVALID_HANDLE_VALUE;
DWORD g_file_bytes = 0;
DWORD g_header_tick = 0;
const DWORD kMaxCaptureBytes = 400u * 1024u * 1024u;

void WriteHeader() {
  if (g_file == INVALID_HANDLE_VALUE) return;
  const DWORD rate = g_rate > 0 ? static_cast<DWORD>(g_rate) : 22050;
  uint8_t h[44];
  memcpy(h, "RIFF", 4);
  const DWORD riff = 36 + g_file_bytes;
  memcpy(h + 4, &riff, 4);
  memcpy(h + 8, "WAVEfmt ", 8);
  const DWORD fmt_size = 16;
  memcpy(h + 16, &fmt_size, 4);
  const WORD tag = 1, channels = 2, bits = 16, align = 4;
  const DWORD bytes_per_second = rate * 4;
  memcpy(h + 20, &tag, 2);
  memcpy(h + 22, &channels, 2);
  memcpy(h + 24, &rate, 4);
  memcpy(h + 28, &bytes_per_second, 4);
  memcpy(h + 32, &align, 2);
  memcpy(h + 34, &bits, 2);
  memcpy(h + 36, "data", 4);
  memcpy(h + 40, &g_file_bytes, 4);
  DWORD written = 0;
  SetFilePointer(g_file, 0, nullptr, FILE_BEGIN);
  WriteFile(g_file, h, sizeof(h), &written, nullptr);
  SetFilePointer(g_file, 0, nullptr, FILE_END);
}

void OpenCapture() {
  char path[MAX_PATH];
  const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
  char* slash =
      length > 0 && length < MAX_PATH ? strrchr(path, '\\') : nullptr;
  if (slash == nullptr || strlen(path) + 16 >= MAX_PATH) return;
  slash[1] = 0;
  strcat_s(path, "bwvr-audio.wav");
  g_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (g_file == INVALID_HANDLE_VALUE) {
    ModLog("audio out    : the capture file did not open (error %lu)",
           GetLastError());
    return;
  }
  WriteHeader();
  ModLog("audio out    : audio_capture=1, each block that QMixer gives to "
         "the wave device goes to %s", path);
}

// One block of the stream, on the mixer thread of QMixer.
void OnBlock(char* data, DWORD bytes) {
  const DWORD now = GetTickCount();
  if (g_window_tick == 0) g_window_tick = now;
  g_window_bytes += static_cast<LONG>(bytes);
  ++g_window_blocks;
  InterlockedIncrement(&g_blocks);

  if (g_good_format != 0 && (bytes & 3) == 0) {
    int16_t* samples = reinterpret_cast<int16_t*>(data);
    const int frames = static_cast<int>(bytes / 4);
    if (g_render != nullptr) g_render(samples, frames, g_rate);
    if (g_report) {
      for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < 2; ++c) {
          const int v = samples[i * 2 + c];
          g_window_square[c] += static_cast<double>(v) * v;
          const int a = v < 0 ? -v : v;
          if (a > g_window_peak[c]) g_window_peak[c] = a;
        }
      }
      g_window_frames += frames;
    }
    if (g_file != INVALID_HANDLE_VALUE &&
        g_file_bytes + bytes < kMaxCaptureBytes) {
      DWORD written = 0;
      WriteFile(g_file, data, bytes, &written, nullptr);
      g_file_bytes += written;
      if (now - g_header_tick >= 500) {
        g_header_tick = now;
        WriteHeader();
      }
    }
    InterlockedExchangeAdd(&g_frames, frames);
  }

  if (now - g_window_tick < 1000) return;
  const DWORD elapsed = now - g_window_tick;
  // QMixer opened the device before the patch: guess the rate from the
  // bytes of a second, for 16 bit stereo.
  if (g_rate == 0 && g_window_blocks > 10) {
    const double per_second = 1000.0 * g_window_bytes / elapsed / 4.0;
    static const LONG kRates[] = {11025, 22050, 44100, 48000};
    for (int i = 0; i < 4; ++i) {
      if (fabs(per_second - kRates[i]) < 0.08 * kRates[i]) {
        InterlockedExchange(&g_rate, kRates[i]);
        InterlockedExchange(&g_good_format, 1);
      }
    }
  }
  if (g_report && !g_report_due) {
    const double n = g_window_frames > 0 ? g_window_frames : 1;
    const double left = sqrt(g_window_square[0] / n) / 32768.0;
    const double right = sqrt(g_window_square[1] / n) / 32768.0;
    wsprintfA(g_report_text,
              "%lu ms, %ld blocks, %ld bytes, rms left %d right %d, peak %d "
              "%d (parts of 32768), capture frame %ld",
              elapsed, g_window_blocks, g_window_bytes,
              static_cast<int>(left * 32768.0),
              static_cast<int>(right * 32768.0), g_window_peak[0],
              g_window_peak[1], g_frames);
    g_report_due = true;
  }
  g_window_tick = now;
  g_window_bytes = 0;
  g_window_blocks = 0;
  g_window_square[0] = g_window_square[1] = 0.0;
  g_window_peak[0] = g_window_peak[1] = 0;
  g_window_frames = 0;
}

bool FromMixer(uintptr_t caller) {
  return caller >= g_mixer_lo && caller < g_mixer_hi;
}

// The parts that QMixer unlocks. The format comes from the buffer itself.
void DirectSoundBlocks(IDirectSoundBuffer* self, void* part1, DWORD bytes1,
                       void* part2, DWORD bytes2) {
  if (self != g_ds_buffer) {
    g_ds_buffer = self;
    WAVEFORMATEX f;
    memset(&f, 0, sizeof(f));
    DWORD written = 0;
    const HRESULT hr = self->GetFormat(&f, sizeof(f), &written);
    const bool good = SUCCEEDED(hr) && f.wFormatTag == WAVE_FORMAT_PCM &&
                      f.nChannels == 2 && f.wBitsPerSample == 16;
    InterlockedExchange(&g_rate, static_cast<LONG>(f.nSamplesPerSec));
    InterlockedExchange(&g_good_format, good ? 1 : 0);
    g_format_from_open = true;
    g_format_logged = false;
    wsprintfA(g_open_text,
              "QMixer unlocks the DirectSound buffer %p: tag %d, %d "
              "channels, %lu Hz, %d bit (GetFormat 0x%lX), first part %lu "
              "bytes",
              self, f.wFormatTag, f.nChannels, f.nSamplesPerSec,
              f.wBitsPerSample, hr, bytes1);
    InterlockedIncrement(&g_open_calls);
  }
  if (g_lock_known && g_rate > 0) {
    // How far ahead of the play cursor this block is.
    if (g_buffer_bytes == 0) {
      DSBCAPS caps;
      memset(&caps, 0, sizeof(caps));
      caps.dwSize = sizeof(caps);
      if (SUCCEEDED(self->GetCaps(&caps))) g_buffer_bytes = caps.dwBufferBytes;
    }
    DWORD play = 0, write = 0;
    if (g_buffer_bytes > 0 &&
        SUCCEEDED(self->GetCurrentPosition(&play, &write))) {
      const DWORD ahead =
          (g_lock_offset + g_buffer_bytes - play) % g_buffer_bytes;
      const float ms = 1000.0f * static_cast<float>(ahead / 4) /
                       static_cast<float>(g_rate);
      // The first blocks fill the buffer from the play cursor. After them,
      // a write that is more than half the buffer "ahead" is behind the
      // play cursor: the device passed it, and the player hears a gap.
      if (++g_lead_blocks > 20) {
        if (ahead > g_buffer_bytes / 2) {
          InterlockedIncrement(&g_passed);
        } else {
          g_lead_sum_ms += ms;
          if (ms > g_lead_most_ms) g_lead_most_ms = ms;
          if (ms < g_lead_least_ms) g_lead_least_ms = ms;
          ++g_lead_count;
        }
      }
    }
    g_lock_known = false;
  }
  if (part1 != nullptr && bytes1 >= 4) {
    OnBlock(static_cast<char*>(part1), bytes1);
  }
  if (part2 != nullptr && bytes2 >= 4) {
    OnBlock(static_cast<char*>(part2), bytes2);
  }
}

HRESULT STDMETHODCALLTYPE h_Unlock0(IDirectSoundBuffer* self, void* part1,
                                    DWORD bytes1, void* part2, DWORD bytes2) {
  if (FromMixer(reinterpret_cast<uintptr_t>(_ReturnAddress()))) {
    DirectSoundBlocks(self, part1, bytes1, part2, bytes2);
  }
  return o_Unlock[0](self, part1, bytes1, part2, bytes2);
}

HRESULT STDMETHODCALLTYPE h_Unlock1(IDirectSoundBuffer* self, void* part1,
                                    DWORD bytes1, void* part2, DWORD bytes2) {
  if (FromMixer(reinterpret_cast<uintptr_t>(_ReturnAddress()))) {
    DirectSoundBlocks(self, part1, bytes1, part2, bytes2);
  }
  return o_Unlock[1](self, part1, bytes1, part2, bytes2);
}

void* WINAPI h_InitEx(void* config) {
  uint32_t copy[16];
  memset(copy, 0, sizeof(copy));
  if (config == nullptr || IsBadReadPtr(config, 4)) return o_InitEx(config);
  DWORD size = *static_cast<const DWORD*>(config);
  if (size > sizeof(copy)) size = sizeof(copy);
  if (IsBadReadPtr(config, size)) return o_InitEx(config);
  memcpy(copy, config, size);
  const uint32_t asked = copy[kConfigLatency / 4];
  if (g_latency_ms > 0 && size > kConfigLatency) {
    copy[kConfigLatency / 4] = static_cast<uint32_t>(g_latency_ms);
  }
  void* session = o_InitEx(copy);
  ModLog("audio out    : QMixer starts: %lu Hz, %lu channels, output %lu, "
         "flags 0x%lX, latency %lu ms from the game (0 is QMixer's own "
         "choice)%s", copy[2], copy[5], copy[6], copy[1], asked,
         g_latency_ms > 0 ? ", and audio_latency_ms sets it" : "");
  return session;
}

void NoteLock(HRESULT hr, DWORD offset, DWORD flags, uintptr_t caller) {
  // With the flag 1 the lock starts at the write cursor, and the offset is
  // not used. QMixer gives its own offset.
  if (SUCCEEDED(hr) && FromMixer(caller) && (flags & 1) == 0) {
    g_lock_offset = offset;
    g_lock_known = true;
  }
}

HRESULT STDMETHODCALLTYPE h_Lock0(IDirectSoundBuffer* self, DWORD offset,
                                  DWORD bytes, void** part1, DWORD* bytes1,
                                  void** part2, DWORD* bytes2, DWORD flags) {
  const HRESULT hr =
      o_Lock[0](self, offset, bytes, part1, bytes1, part2, bytes2, flags);
  NoteLock(hr, offset, flags, reinterpret_cast<uintptr_t>(_ReturnAddress()));
  return hr;
}

HRESULT STDMETHODCALLTYPE h_Lock1(IDirectSoundBuffer* self, DWORD offset,
                                  DWORD bytes, void** part1, DWORD* bytes1,
                                  void** part2, DWORD* bytes2, DWORD flags) {
  const HRESULT hr =
      o_Lock[1](self, offset, bytes, part1, bytes1, part2, bytes2, flags);
  NoteLock(hr, offset, flags, reinterpret_cast<uintptr_t>(_ReturnAddress()));
  return hr;
}

// Hooks the code of Unlock for the class of this buffer.
void HookUnlock(IDirectSoundBuffer* buffer, const char* what) {
  void** vtable = *reinterpret_cast<void***>(buffer);
  void* code = vtable[kUnlockSlot];
  if (code == g_unlock_code[0] || code == g_unlock_code[1]) return;
  const int slot = g_unlock_code[0] == nullptr ? 0 : 1;
  if (g_unlock_code[slot] != nullptr) return;
  MH_STATUS s = MH_CreateHook(
      code, slot == 0 ? reinterpret_cast<void*>(&h_Unlock0)
                      : reinterpret_cast<void*>(&h_Unlock1),
      reinterpret_cast<void**>(&o_Unlock[slot]));
  if (s == MH_OK) s = MH_EnableHook(code);
  if (s == MH_OK) g_unlock_code[slot] = code;
  ModLog("audio out    : Unlock of a %s DirectSound buffer at %p %s", what,
         code, s == MH_OK ? "ok" : "FAILED");
  // The same for Lock, only to measure how far ahead the stream is.
  void* lock = vtable[kLockSlot];
  if (s == MH_OK && g_lock_code[slot] == nullptr) {
    MH_STATUS l = MH_CreateHook(
        lock, slot == 0 ? reinterpret_cast<void*>(&h_Lock0)
                        : reinterpret_cast<void*>(&h_Lock1),
        reinterpret_cast<void**>(&o_Lock[slot]));
    if (l == MH_OK) l = MH_EnableHook(lock);
    if (l == MH_OK) g_lock_code[slot] = lock;
  }
}

MMRESULT WINAPI h_Write(HWAVEOUT device, LPWAVEHDR header, UINT size) {
  if (header != nullptr && header->lpData != nullptr &&
      header->dwBufferLength >= 4) {
    OnBlock(header->lpData, header->dwBufferLength);
  }
  return o_Write(device, header, size);
}

void WINAPI h_DebugString(LPCSTR text) {
  if (text != nullptr && !IsBadReadPtr(text, 1)) {
    const LONG n = InterlockedIncrement(&g_said_count) - 1;
    if (n < kSaid) {
      strncpy_s(g_said[n], text, _TRUNCATE);
      for (char* c = g_said[n]; *c != 0; ++c) {
        if (*c == '\r' || *c == '\n') *c = ' ';
      }
    }
  }
  o_DebugString(text);
}

MMRESULT WINAPI h_Open(LPHWAVEOUT device, UINT id, LPCWAVEFORMATEX format,
                       DWORD_PTR callback, DWORD_PTR instance, DWORD flags) {
  const MMRESULT r = o_Open(device, id, format, callback, instance, flags);
  if (format != nullptr) {
    wsprintfA(g_open_text,
              "waveOutOpen: tag %d, %d channels, %lu Hz, %d bit, flags "
              "0x%lX, result %u",
              format->wFormatTag, format->nChannels, format->nSamplesPerSec,
              format->wBitsPerSample, flags, r);
    InterlockedIncrement(&g_open_calls);
  }
  // A query only asks whether the format works.
  if (r == MMSYSERR_NOERROR && format != nullptr &&
      (flags & WAVE_FORMAT_QUERY) == 0) {
    const bool good = format->wFormatTag == WAVE_FORMAT_PCM &&
                      format->nChannels == 2 && format->wBitsPerSample == 16;
    InterlockedExchange(&g_rate, static_cast<LONG>(format->nSamplesPerSec));
    InterlockedExchange(&g_good_format, good ? 1 : 0);
    g_format_from_open = true;
    g_format_logged = false;
  }
  return r;
}

// The slot of an import of a module, or null.
void** ImportSlot(HMODULE module, const char* dll, const char* function) {
  uint8_t* base = reinterpret_cast<uint8_t*>(module);
  if (base == nullptr || IsBadReadPtr(base, 0x400)) return nullptr;
  const IMAGE_DOS_HEADER* dos =
      reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
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

bool Patch(void** slot, void* hook, void** original) {
  if (slot == nullptr) return false;
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
    return false;
  }
  *original = *slot;
  *slot = hook;
  VirtualProtect(slot, sizeof(void*), old, &old);
  return true;
}

}  // namespace

bool AudioPatchImport(HMODULE module, const char* dll, const char* function,
                      void* hook, void** original) {
  return Patch(ImportSlot(module, dll, function), hook, original);
}

bool AudioCaptureInstall(bool capture, bool report) {
  g_capture = capture;
  g_report = report;
  HMODULE mixer = AudioMixerDll();
  void** write = ImportSlot(mixer, "WINMM.dll", "waveOutWrite");
  void** open = ImportSlot(mixer, "WINMM.dll", "waveOutOpen");
  if (write == nullptr || open == nullptr) {
    ModLog("audio out    : QMixer.dll has no import of waveOutWrite or "
           "waveOutOpen, so the mod does not see the stream");
    return false;
  }
  const bool a = Patch(open, reinterpret_cast<void*>(&h_Open),
                       reinterpret_cast<void**>(&o_Open));
  const bool b = a && Patch(write, reinterpret_cast<void*>(&h_Write),
                            reinterpret_cast<void**>(&o_Write));
  g_patched = b;
  g_latency_ms = AudioIni("audio_latency_ms", 0);
  if (g_latency_ms < 0 || g_latency_ms > 500) g_latency_ms = 0;
  void* init = mixer == nullptr ? nullptr
                                : GetProcAddress(mixer, "QSWaveMixInitEx");
  if (init != nullptr &&
      MH_CreateHook(init, reinterpret_cast<void*>(&h_InitEx),
                    reinterpret_cast<void**>(&o_InitEx)) == MH_OK) {
    MH_EnableHook(init);
  }
  Patch(ImportSlot(mixer, "KERNEL32.dll", "OutputDebugStringA"),
        reinterpret_cast<void*>(&h_DebugString),
        reinterpret_cast<void**>(&o_DebugString));
  ModLog("audio out    : the wave device calls of QMixer.dll go through the "
         "mod: %s", b ? "ok" : "FAILED");
  if (b && capture) OpenCapture();
  return b;
}

// QMixer mixes into a DirectSound buffer on this PC. The session gives its
// DirectSound object. Two buffers of the mod's own, a secondary one and the
// primary one, show where the code of Unlock is for each class. The hook on
// that code sees the blocks of QMixer: its caller is in QMixer.dll.
void AudioCaptureSession(void* session) {
  if (g_ds_tried || session == nullptr) return;
  g_ds_tried = true;
  HMODULE mixer = AudioMixerDll();
  const uint8_t* base = reinterpret_cast<const uint8_t*>(mixer);
  const IMAGE_DOS_HEADER* dos =
      reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const IMAGE_NT_HEADERS32* nt =
      reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  g_mixer_lo = reinterpret_cast<uintptr_t>(base);
  g_mixer_hi = g_mixer_lo + nt->OptionalHeader.SizeOfImage;
  GetDirectSoundFn get = reinterpret_cast<GetDirectSoundFn>(
      GetProcAddress(mixer, "QSWaveMixGetDirectSound"));
  IDirectSound* sound = nullptr;
  const DWORD r = get != nullptr ? get(session, &sound) : 0xFFFF;
  if (r != 0 || sound == nullptr || IsBadReadPtr(sound, 4)) {
    ModLog("audio out    : QMixer has no DirectSound object (result %lu)",
           r);
    return;
  }
  WAVEFORMATEX f;
  memset(&f, 0, sizeof(f));
  f.wFormatTag = WAVE_FORMAT_PCM;
  f.nChannels = 2;
  f.nSamplesPerSec = 22050;
  f.wBitsPerSample = 16;
  f.nBlockAlign = 4;
  f.nAvgBytesPerSec = 88200;
  DSBUFFERDESC d;
  memset(&d, 0, sizeof(d));
  d.dwSize = sizeof(d);
  d.dwBufferBytes = 8192;
  d.lpwfxFormat = &f;
  IDirectSoundBuffer* buffer = nullptr;
  HRESULT hr = sound->CreateSoundBuffer(&d, &buffer, nullptr);
  if (SUCCEEDED(hr) && buffer != nullptr) {
    HookUnlock(buffer, "secondary");
    buffer->Release();
  } else {
    ModLog("audio out    : no secondary DirectSound buffer for the mod "
           "(0x%lX)", hr);
  }
  memset(&d, 0, sizeof(d));
  d.dwSize = sizeof(d);
  d.dwFlags = DSBCAPS_PRIMARYBUFFER;
  buffer = nullptr;
  hr = sound->CreateSoundBuffer(&d, &buffer, nullptr);
  if (SUCCEEDED(hr) && buffer != nullptr) {
    HookUnlock(buffer, "primary");
    buffer->Release();
  }
}

void AudioCaptureSetRender(AudioRenderFn render) { g_render = render; }

long AudioCaptureFrames() { return g_frames; }

// How far ahead of the device the stream is, in ms: the mean and the most
// since the last call. False when no block was measured.
bool AudioCaptureLead(float* mean_ms, float* most_ms, float* least_ms,
                      long* passed) {
  *passed = g_passed;
  const LONG count = InterlockedExchange(&g_lead_count, 0);
  if (count <= 0) return false;
  *mean_ms = static_cast<float>(g_lead_sum_ms / count);
  *most_ms = g_lead_most_ms;
  *least_ms = g_lead_least_ms;
  g_lead_sum_ms = 0.0;
  g_lead_most_ms = 0.0f;
  g_lead_least_ms = 1e9f;
  return true;
}

int AudioOutRate() { return g_good_format != 0 ? static_cast<int>(g_rate) : 0; }

// Once per rendered frame, on the game thread: the log lines of the hook.
void AudioCaptureFrame() {
  if (!g_patched) return;
  while (g_said_logged < g_said_count && g_said_logged < kSaid) {
    ModLog("audio out    : QMixer says: %s", g_said[g_said_logged]);
    ++g_said_logged;
  }
  if (g_open_logged != g_open_calls && g_open_logged < 12) {
    g_open_logged = g_open_calls;
    ModLog("audio out    : %s", g_open_text);
  }
  const DWORD tick = GetTickCount();
  if (g_first_frame_tick == 0) g_first_frame_tick = tick;
  if (!g_no_blocks_logged && g_blocks == 0 &&
      tick - g_first_frame_tick > 15000) {
    g_no_blocks_logged = true;
    ModLog("audio out    : QMixer gave no block to the wave device in 15 s. "
           "It is not active, or it does not use the wave device");
  }
  if (!g_format_logged && (g_rate != 0 || g_blocks > 200)) {
    g_format_logged = true;
    if (g_rate == 0) {
      ModLog("audio out    : QMixer gives blocks to the wave device, but the "
             "format is not known, so the mixer of the mod stays off");
    } else {
      ModLog("audio out    : the stream of QMixer is %ld Hz, %s (%s)", g_rate,
             g_good_format != 0 ? "16 bit stereo PCM"
                                : "not 16 bit stereo PCM, so the mixer of "
                                  "the mod stays off",
             g_format_from_open ? "from waveOutOpen"
                                : "a guess from the bytes of a second");
    }
  }
  if (g_report_due) {
    ModLog("audio out    : %s", g_report_text);
    g_report_due = false;
  }
}
