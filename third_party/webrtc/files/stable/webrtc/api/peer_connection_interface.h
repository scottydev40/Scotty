// fork-local webrtc stub header. NOT real webrtc. Declares only the webrtc
// peer-connection types the g3 fake-platform webrtc glue names in its
// signatures/bodies, so it compiles when webrtc is disabled (the only
// configuration this fork builds). Never vendor or build real webrtc.
#ifndef THIRD_PARTY_WEBRTC_STUB_API_PEER_CONNECTION_INTERFACE_H_
#define THIRD_PARTY_WEBRTC_STUB_API_PEER_CONNECTION_INTERFACE_H_

#include <utility>

#include "third_party/webrtc/files/stable/webrtc/api/scoped_refptr.h"

namespace webrtc {

enum class SdpSemantics { kPlanB, kUnifiedPlan };

class PeerConnectionObserver {
 public:
  virtual ~PeerConnectionObserver() = default;
};

class PeerConnectionInterface {
 public:
  struct RTCConfiguration {
    SdpSemantics sdp_semantics = SdpSemantics::kUnifiedPlan;
  };
  virtual ~PeerConnectionInterface() = default;
};

// Minimal stand-in for webrtc::RTCErrorOr<T>.
template <typename T>
class RTCErrorOr {
 public:
  RTCErrorOr() = default;
  RTCErrorOr(T value) : value_(std::move(value)), ok_(true) {}  // NOLINT
  bool ok() const { return ok_; }
  T MoveValue() { return std::move(value_); }

 private:
  T value_{};
  bool ok_ = false;
};

class PeerConnectionDependencies {
 public:
  explicit PeerConnectionDependencies(PeerConnectionObserver* observer)
      : observer(observer) {}
  PeerConnectionObserver* observer = nullptr;
};

class Thread;  // from rtc_base/thread.h

class PeerConnectionFactoryDependencies {
 public:
  Thread* signaling_thread = nullptr;
};

class PeerConnectionFactoryInterface {
 public:
  struct Options {};
  virtual ~PeerConnectionFactoryInterface() = default;
  virtual void SetOptions(const Options&) {}
  virtual RTCErrorOr<scoped_refptr<PeerConnectionInterface>>
  CreatePeerConnectionOrError(
      const PeerConnectionInterface::RTCConfiguration&,
      PeerConnectionDependencies) {
    return RTCErrorOr<scoped_refptr<PeerConnectionInterface>>();
  }
};

}  // namespace webrtc

#endif  // THIRD_PARTY_WEBRTC_STUB_API_PEER_CONNECTION_INTERFACE_H_
