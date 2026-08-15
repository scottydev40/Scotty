#include "internal/platform/implementation/linux/agent_session_gate.h"

#include "gtest/gtest.h"
#include "absl/time/time.h"
#include "internal/platform/mac_address.h"

namespace nearby {
namespace linux {
namespace {

MacAddress Mac(absl::string_view s) {
  MacAddress m;
  EXPECT_TRUE(MacAddress::FromString(s, m));
  return m;
}

TEST(AgentSessionGateTest, ArmsOnFirstSessionDisarmsOnLast) {
  int arms = 0, disarms = 0;
  AgentSessionGate gate(absl::Seconds(30));
  gate.SetArmCallbacks({[&] { arms++; }, [&] { disarms++; }});

  gate.BeginSession(Mac("AA:BB:CC:DD:EE:01"));
  EXPECT_EQ(arms, 1);
  EXPECT_EQ(disarms, 0);

  // Second peer: already armed, no re-arm.
  gate.BeginSession(Mac("AA:BB:CC:DD:EE:02"));
  EXPECT_EQ(arms, 1);

  // First peer ends: still one active, no disarm.
  gate.EndSession(Mac("AA:BB:CC:DD:EE:01"));
  EXPECT_EQ(disarms, 0);

  // Last peer ends: disarm.
  gate.EndSession(Mac("AA:BB:CC:DD:EE:02"));
  EXPECT_EQ(disarms, 1);
}

TEST(AgentSessionGateTest, IsAllowedOnlyForActivePeers) {
  AgentSessionGate gate(absl::Seconds(30));
  gate.BeginSession(Mac("AA:BB:CC:DD:EE:01"));
  EXPECT_TRUE(gate.IsAllowed(Mac("AA:BB:CC:DD:EE:01")));
  EXPECT_FALSE(gate.IsAllowed(Mac("AA:BB:CC:DD:EE:99")));
}

TEST(AgentSessionGateTest, ExpiryRemovesPeerAndDisarms) {
  absl::Time now = absl::UnixEpoch();
  int disarms = 0;
  AgentSessionGate gate(absl::Seconds(30), [&] { return now; });
  gate.SetArmCallbacks({[] {}, [&] { disarms++; }});

  gate.BeginSession(Mac("AA:BB:CC:DD:EE:01"));
  now += absl::Seconds(31);
  EXPECT_FALSE(gate.IsAllowed(Mac("AA:BB:CC:DD:EE:01")));

  gate.SweepExpired();
  EXPECT_EQ(disarms, 1);
}

TEST(AgentSessionGateTest, SharedGateIsPerAdapterSingleton) {
  auto a = GetSharedAgentSessionGate("/org/bluez/hci0", absl::Seconds(30));
  auto b = GetSharedAgentSessionGate("/org/bluez/hci0", absl::Seconds(30));
  EXPECT_EQ(a.get(), b.get());
}

}  // namespace
}  // namespace linux
}  // namespace nearby
