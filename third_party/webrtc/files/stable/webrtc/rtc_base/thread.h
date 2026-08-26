// fork-local webrtc stub header. NOT real webrtc. Minimal webrtc::Thread used
// by the g3 fake-platform webrtc glue (never instantiated in the fork). Never
// vendor or build real webrtc.
#ifndef THIRD_PARTY_WEBRTC_STUB_RTC_BASE_THREAD_H_
#define THIRD_PARTY_WEBRTC_STUB_RTC_BASE_THREAD_H_

#include <memory>

namespace webrtc {

class Thread {
 public:
  static std::unique_ptr<Thread> Create() {
    return std::make_unique<Thread>();
  }
  void SetName(const char* /*name*/, const void* /*obj*/) {}
  bool Start() { return true; }
};

}  // namespace webrtc

#endif  // THIRD_PARTY_WEBRTC_STUB_RTC_BASE_THREAD_H_
