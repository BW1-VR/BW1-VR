// How the threads of the mod wait for work, and the time that each thread
// uses (thread_wait.cpp, docs/backend.md, "How the threads wait").

#pragma once

#include <stdint.h>

// thread_wait of bwvr.ini. 1 (the default): a thread that waits for work
// spins for a time that comes from the count of the cores, and then it
// sleeps. The second thread of frame_send sleeps until the GPU is nearly
// done. 0: the render thread and the D3D7 thread spin for the whole walk,
// and the second thread asks the GPU each 50 microseconds.
// test_spin_walk_us, test_spin_idle_us and test_wake_records (test aids,
// -1 is the default) take the place of the values of the count of the cores.
void ThreadWaitSetOptions(int mode, int walk_us, int idle_us, int wake);
int ThreadWaitMode();
// The logical cores that the process can use.
int ThreadWaitLogicalCores();
// The test key: thread_wait goes between 1 and 0.
void ThreadWaitToggle(const char* keys);
void ThreadWaitSet(int mode);

// The ticks of QueryPerformanceCounter that a reader spins on an empty
// queue before it sleeps. in_walk: the writer is in a scene walk.
int64_t ThreadWaitSpinTicks(bool in_walk);
// A writer wakes a reader that sleeps when this count of records waits for
// it, or at the end of a walk and at a stop.
uint32_t ThreadWaitWakeRecords();

// The QueryPerformanceCounter frequency.
int64_t ThreadWaitFrequency();

// Sleeps for about `ticks` of QueryPerformanceCounter with a timer of high
// resolution. False when Windows has no such timer: then the caller waits
// another way.
bool ThreadWaitSleep(int64_t ticks);

// Gives a thread of the mod a name for the debugger, and tells Windows that
// the thread is not background work (no EcoQoS on the cores for low power).
void ThreadWaitTune(void* thread, const wchar_t* name);

// The CPU time of the threads of the mod and of the frame thread. Each
// thread is added one time. ThreadLoadAddThis adds the thread that calls.
void ThreadLoadAdd(const char* name, void* thread);
void ThreadLoadAddThis(const char* name);
void ThreadLoadReport(uint64_t frames);

// test_cores and test_smt of bwvr.ini (test aids, 0 is the default): the
// process runs on `cores` physical cores, with one logical core of each
// (smt 0) or with each of their logical cores (smt 1). So a desktop run
// shows a PC with fewer cores. Call it before ThreadWaitSetOptions.
void ThreadWaitLimitCores(int cores, int smt);
