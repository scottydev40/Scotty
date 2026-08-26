// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// fork-local: minimal drop-in replacement for nisaba/port/thread_pool.h.
// The upstream g3 fake platform depends on Nisaba's ThreadPool, but the
// Nisaba OSS archive pulls its own unvendored bazel_rules web that does not
// resolve in this fork. The g3 thread pool is only a test primitive, so this
// header provides an equivalent, self-contained implementation (std::thread +
// absl synchronization) exposing the same API the fake platform uses:
//   ThreadPool(int max_parallelism)
//   StartWorkers()
//   Schedule(Runnable&&)
//   ScheduleAt(absl::Time, Runnable&&)

#ifndef PLATFORM_IMPL_G3_THREAD_POOL_H_
#define PLATFORM_IMPL_G3_THREAD_POOL_H_

#include <algorithm>
#include <atomic>
#include <queue>
#include <thread>  // NOLINT(build/c++11)
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace nearby {

// Header-only fixed-size thread pool with support for delayed scheduling.
// Tasks are any_invocable<void()> (Runnable is convertible to this).
class ThreadPool {
 public:
  using Task = absl::AnyInvocable<void()>;

  explicit ThreadPool(int max_parallelism)
      : max_parallelism_(std::max(1, max_parallelism)) {}

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  ~ThreadPool() { Stop(); }

  // Starts the worker threads plus the single timer thread.
  void StartWorkers() {
    absl::MutexLock lock(&mutex_);
    if (started_) return;
    started_ = true;
    workers_.reserve(max_parallelism_);
    for (int i = 0; i < max_parallelism_; ++i) {
      workers_.emplace_back([this] { WorkerLoop(); });
    }
    timer_thread_ = std::thread([this] { TimerLoop(); });
  }

  // Enqueues a task to run as soon as a worker is free.
  void Schedule(Task task) {
    absl::MutexLock lock(&mutex_);
    if (shutdown_) return;
    ready_.push(std::move(task));
  }

  // Enqueues a task to run at (or after) the given absolute time.
  void ScheduleAt(absl::Time when, Task task) {
    absl::MutexLock lock(&mutex_);
    if (shutdown_) return;
    delayed_.push(DelayedTask{when, ++seq_, std::move(task)});
  }

 private:
  struct DelayedTask {
    absl::Time when;
    uint64_t seq;
    mutable Task task;
    bool operator>(const DelayedTask& other) const {
      if (when != other.when) return when > other.when;
      return seq > other.seq;
    }
  };

  bool HasReadyOrShutdown() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return !ready_.empty() || shutdown_;
  }

  void WorkerLoop() {
    for (;;) {
      Task task;
      {
        absl::MutexLock lock(&mutex_);
        mutex_.Await(absl::Condition(this, &ThreadPool::HasReadyOrShutdown));
        if (shutdown_ && ready_.empty()) return;
        task = std::move(ready_.front());
        ready_.pop();
      }
      task();
    }
  }

  bool TimerReady() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return shutdown_ || !delayed_.empty();
  }

  void TimerLoop() {
    for (;;) {
      Task due_task;
      bool have_due = false;
      {
        absl::MutexLock lock(&mutex_);
        mutex_.Await(absl::Condition(this, &ThreadPool::TimerReady));
        if (shutdown_) return;
        absl::Time now = absl::Now();
        if (delayed_.top().when <= now) {
          due_task = std::move(delayed_.top().task);
          delayed_.pop();
          have_due = true;
        } else {
          // Wait until the earliest deadline (or a wakeup) then re-check.
          mutex_.AwaitWithDeadline(
              absl::Condition(this, &ThreadPool::TimerReady),
              delayed_.top().when);
        }
      }
      if (have_due) {
        absl::MutexLock lock(&mutex_);
        if (!shutdown_) ready_.push(std::move(due_task));
      }
    }
  }

  void Stop() {
    {
      absl::MutexLock lock(&mutex_);
      if (shutdown_) return;
      shutdown_ = true;
    }
    if (timer_thread_.joinable()) timer_thread_.join();
    for (auto& w : workers_) {
      if (w.joinable()) w.join();
    }
    workers_.clear();
  }

  const int max_parallelism_;
  mutable absl::Mutex mutex_;
  bool started_ ABSL_GUARDED_BY(mutex_) = false;
  bool shutdown_ ABSL_GUARDED_BY(mutex_) = false;
  uint64_t seq_ ABSL_GUARDED_BY(mutex_) = 0;
  std::queue<Task> ready_ ABSL_GUARDED_BY(mutex_);
  std::priority_queue<DelayedTask, std::vector<DelayedTask>,
                      std::greater<DelayedTask>>
      delayed_ ABSL_GUARDED_BY(mutex_);
  std::vector<std::thread> workers_;
  std::thread timer_thread_;
};

}  // namespace nearby

#endif  // PLATFORM_IMPL_G3_THREAD_POOL_H_
