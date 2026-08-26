// fork-local gloop stub header. NOT real gloop. Provides just the live-thread
// TID helpers the g3 fake platform uses in GetCurrentTid(), backed by the
// Linux gettid syscall. Never vendor or build real gloop.
#ifndef THIRD_PARTY_GLOOP_STUB_THREAD_THREAD_H_
#define THIRD_PARTY_GLOOP_STUB_THREAD_THREAD_H_

#include <sys/syscall.h>
#include <unistd.h>

struct LiveThread {};

inline const LiveThread* Thread_GetMyLiveThread() {
  static thread_local LiveThread self;
  return &self;
}

inline int LiveThread_Pthread_TID(const LiveThread* /*thread*/) {
  return static_cast<int>(::syscall(SYS_gettid));
}

#endif  // THIRD_PARTY_GLOOP_STUB_THREAD_THREAD_H_
