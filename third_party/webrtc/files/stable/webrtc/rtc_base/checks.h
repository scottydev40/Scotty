// fork-local webrtc stub header. NOT real webrtc. Minimal RTC_CHECK that
// supports the `RTC_CHECK(cond) << "msg"` streaming form used by the g3
// fake-platform webrtc glue. Never vendor or build real webrtc.
#ifndef THIRD_PARTY_WEBRTC_STUB_RTC_BASE_CHECKS_H_
#define THIRD_PARTY_WEBRTC_STUB_RTC_BASE_CHECKS_H_

#include <ostream>
#include <sstream>

namespace webrtc {
namespace rtc_stub {
// Swallows anything streamed after RTC_CHECK(...). Never actually reached in
// the fork (the webrtc path is disabled), so it does not need to abort.
class FatalMessage {
 public:
  template <typename T>
  FatalMessage& operator<<(const T&) {
    return *this;
  }
};
}  // namespace rtc_stub
}  // namespace webrtc

#define RTC_CHECK(condition) \
  ::webrtc::rtc_stub::FatalMessage()

#endif  // THIRD_PARTY_WEBRTC_STUB_RTC_BASE_CHECKS_H_
