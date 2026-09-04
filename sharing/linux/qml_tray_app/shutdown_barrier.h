#ifndef SCOTTY_SHUTDOWN_BARRIER_H_
#define SCOTTY_SHUTDOWN_BARRIER_H_

#include <chrono>
#include <future>
#include <memory>
#include <utility>

namespace scotty {

// A slow shutdown is diagnostic, never permission to destroy resources still
// used by the engine. The completion owns its state even if the engine retains
// the callback after invoking it. Call from the owning thread, not the worker.
template <typename Start, typename OnSlow>
void AwaitShutdown(Start start, OnSlow on_slow,
                   std::chrono::milliseconds warning_after =
                       std::chrono::seconds(5)) {
  auto completion = std::make_shared<std::promise<void>>();
  auto done = completion->get_future();
  start([completion] { completion->set_value(); });
  if (done.wait_for(warning_after) != std::future_status::ready) {
    on_slow();
    done.wait();
  }
}

}  // namespace scotty
#endif
