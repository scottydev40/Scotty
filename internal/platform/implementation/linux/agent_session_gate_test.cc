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

TEST(AgentSessionGateTest, RearmsAfterFullDrainCycle) {
  int arms = 0, disarms = 0;
  AgentSessionGate gate(absl::Seconds(30));
  gate.SetArmCallbacks({[&] { arms++; }, [&] { disarms++; }});

  gate.BeginSession(Mac("AA:BB:CC:DD:EE:01"));
  EXPECT_EQ(arms, 1);
  EXPECT_EQ(disarms, 0);

  gate.EndSession(Mac("AA:BB:CC:DD:EE:01"));
  EXPECT_EQ(disarms, 1);

  // Set fully drained; a new session should arm again (state machine
  // cycles rather than latching disarmed forever).
  gate.BeginSession(Mac("AA:BB:CC:DD:EE:02"));
  EXPECT_EQ(arms, 2);
  EXPECT_EQ(disarms, 1);

  gate.EndSession(Mac("AA:BB:CC:DD:EE:02"));
  EXPECT_EQ(disarms, 2);
}

TEST(AgentSessionGateTest, IsAllowedAfterExpiryDoesNotDisarm) {
  absl::Time now = absl::UnixEpoch();
  int disarms = 0;
  AgentSessionGate gate(absl::Seconds(30), [&] { return now; });
  gate.SetArmCallbacks({[] {}, [&] { disarms++; }});

  gate.BeginSession(Mac("AA:BB:CC:DD:EE:01"));
  now += absl::Seconds(31);

  // Simulates a BlueZ agent callback (RequestAuthorization/RequestConfirmation)
  // arriving after the peer's TTL has expired. IsAllowed must purge the
  // expired entry and report false, but must NEVER fire on_disarm itself:
  // on_disarm runs AgentManager::Disarm(), which destroys the Agent whose
  // method would still be on the stack in the real BlueZ callback path.
  EXPECT_FALSE(gate.IsAllowed(Mac("AA:BB:CC:DD:EE:01")));
  EXPECT_EQ(disarms, 0);

  // Disarm is deferred to a caller off the agent-callback thread.
  gate.SweepExpired();
  EXPECT_EQ(disarms, 1);
}

}  // namespace
}  // namespace linux
}  // namespace nearby
