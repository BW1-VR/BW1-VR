// The id of the calling thread with no call of the system.

#pragma once

#include <intrin.h>

// GetCurrentThreadId reads the same field of the thread block: on x86 the
// thread block is at fs:0, and its ClientId.UniqueThread at 0x24.
inline unsigned long ThisThreadId() { return __readfsdword(0x24); }
