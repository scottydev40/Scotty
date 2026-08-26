// fork-local webrtc stub header. NOT real webrtc. Inline no-op factory so the
// g3 fake-platform webrtc glue links (alwayslink) without real webrtc. Never
// vendor or build real webrtc.
#ifndef THIRD_PARTY_WEBRTC_STUB_API_CREATE_MODULAR_PEER_CONNECTION_FACTORY_H_
#define THIRD_PARTY_WEBRTC_STUB_API_CREATE_MODULAR_PEER_CONNECTION_FACTORY_H_

#include "third_party/webrtc/files/stable/webrtc/api/peer_connection_interface.h"
#include "third_party/webrtc/files/stable/webrtc/api/scoped_refptr.h"

namespace webrtc {

inline scoped_refptr<PeerConnectionFactoryInterface>
CreateModularPeerConnectionFactory(PeerConnectionFactoryDependencies) {
  return scoped_refptr<PeerConnectionFactoryInterface>();
}

}  // namespace webrtc

#endif  // THIRD_PARTY_WEBRTC_STUB_API_CREATE_MODULAR_PEER_CONNECTION_FACTORY_H_
