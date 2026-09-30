// tests/test36.cpp
//
// Fourth review round. These cases target three defects that the existing
// 128 cases cannot see, and every one of them is a red -> green regression
// against the production fix shipped in the same patch:
//
//   T128  The give-up timer is only armed when the SDK reports a non-GOOD
//         connect status (Impl::isConnected). A connection that is lost while
//         the SDK still reports connectStatus == GOOD (session CLOSED, or the
//         inactivity callback fired) never arms m_lastBadStatusMs, so
//         shouldTriggerGiveUp() stays false forever and lifeState can never
//         reach GIVEN_UP. The watchdog loops disconnect/connect indefinitely.
//
//   T129  Impl::disconnect() moves RUNNING -> RECOVERING through
//         TurnToRecovring(), whose condition omits the secure-channel state.
//         When the SDK reports (connectStatus=GOOD, channel=CLOSED,
//         session=ACTIVATED), disconnect() succeeds but lifeState stays
//         RUNNING while isHealthy() already fails, breaking the documented
//         RUNNING contract (Types.h).
//
//   T130  ReadResponseGuard / WriteResponseGuard neutralise a malformed
//         `results == nullptr && resultsSize > 0` array but not the symmetric
//         `diagnosticInfos == nullptr && diagnosticInfosSize > 0` array.
//         open62541 v1.4.14 UA_ReadResponse_clear -> UA_clear ->
//         clearStructure() calls UA_Array_delete(diagnosticInfos, size,
//         &UA_TYPES[UA_TYPES_DIAGNOSTICINFO]) unconditionally; a null pointer
//         with size > 0 dereferences null. The case is run under ASan.
//
// Build/run:
//   ctest --test-dir build-asan
//   ./scripts/run_asan.sh

#include "tests/include/util/ClientTestFixture.h"

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>
#include <open62541/client.h>

#include <chrono>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    include <sanitizer/lsan_interface.h>
#    define OPCUA_LSAN_CHECK() __lsan_do_recoverable_leak_check()
#  else
#    define OPCUA_LSAN_CHECK() 0
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  include <sanitizer/lsan_interface.h>
#  define OPCUA_LSAN_CHECK() __lsan_do_recoverable_leak_check()
#else
#  define OPCUA_LSAN_CHECK() 0
#endif

namespace {

std::unique_ptr<OPC_UA_Client>
makeMockClient(const std::shared_ptr<MockSdk> &sdk, ClientConfig cfg,
               bool watchdog = false) {
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false, watchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

ClientConfig localCfg() {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  return cfg;
}

} // namespace

// ============================================================
// T128 (red -> green): give-up timer must arm when session is lost even if the
// SDK still reports connectStatus == GOOD.
// ============================================================
TEST(ClientBug, T128_GiveUpTimer_ArmsWithGoodStatusButLostSession) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg = localCfg();
  cfg.watchdogIntervalMs = 1000;
  cfg.connectivityCheckIntervalMs = 1000;
  cfg.giveUpThresholdMs = 3000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto c = makeMockClient(sdk, cfg, /*watchdog=*/false);
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::GIVEN_UP),
            LifeState::RUNNING);

  // Session lost, but getState() keeps reporting connectStatus == GOOD.
  // connectAsync therefore always fails, so the watchdog can never self-heal.
  sdk->setMode(MockSdk::Mode::Disconnected, /*allowAuto=*/false);
  sdk->setConnectAsyncResult(UA_STATUSCODE_BADCONNECTIONCLOSED);
  sdk->setAutoAdvancePerIterateMs(1000);

  bool reachedGivenUp = false;
  for (int i = 0; i < 50 && !reachedGivenUp; ++i) {
    OPCUAPacking::test::ClientTestHooks::pumpWatchdogForTest(*c);
    auto ls = c->checkLifeState();
    reachedGivenUp =
        ls.has_value() && ls.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
  }

  EXPECT_TRUE(reachedGivenUp)
      << "watchdog kept looping disconnect/connect without ever arming the "
         "give-up timer; GIVEN_UP is unreachable when the SDK reports GOOD "
         "connectStatus with a lost session";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T129 (red -> green): successful disconnect() must not leave RUNNING while
// the channel is CLOSED and isHealthy() already fails.
// ============================================================
TEST(ClientBug, T129_Disconnect_ChannelClosedSessionActivated_MustNotStayRunning) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeMockClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::GIVEN_UP),
            LifeState::RUNNING);

  // The SDK reports a closed channel while the session still reads ACTIVATED.
  // This is exactly the triplet T122 treats as unhealthy in the watchdog.
  sdk->enqueueState(UA_STATUSCODE_GOOD, UA_SECURECHANNELSTATE_CLOSED,
                    UA_SESSIONSTATE_ACTIVATED);

  ASSERT_TRUE(c->disconnect().is_success());
  EXPECT_TRUE(c->isHealthy().is_fail());

  auto ls = c->checkLifeState();
  ASSERT_TRUE(ls.has_value()) << ls.get_error()->what();
  EXPECT_NE(ls.value_or(LifeState::RUNNING), LifeState::RUNNING)
      << "disconnect() succeeded and isHealthy() fails, but checkLifeState() "
         "still reports RUNNING: TurnToRecovring() ignores the channel state";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T130 (red -> green, ASan): malformed diagnosticInfos must not dereference
// null; the response guards must release both arrays.
// ============================================================
TEST(ClientBug, T130_MalformedDiagnosticInfos_NoNullDerefNoLeak) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeMockClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  // ---- read: valid results + diagnosticInfosSize>0 with nullptr ----
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16(0, 7);
  sdk->setReadResponseNullDiagnosticInfos(1);
  std::vector<OPC_UA_Client::ReadValue> rv{{1, "DiagRead", S7DataType::INT}};
  auto rr = c->batchRead(rv);
  ASSERT_TRUE(rr.is_success()) << rr.get_error()->what();
  const auto &rout = rr.value_or({});
  ASSERT_EQ(rout.size(), 1u);
  ASSERT_TRUE(rout[0].value.has_value());
  EXPECT_EQ(std::get<int16_t>(*rout[0].value), 7);

  // ---- write: valid per-item results + diagnosticInfosSize>0 with nullptr ----
  sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1);
  sdk->setWriteResponseNullDiagnosticInfos(1);
  std::vector<OPC_UA_Client::WriteValue> wv{
      {{1, "DiagWrite", S7DataType::INT}, int16_t(9)}};
  auto wr = c->batchWrite(wv);
  ASSERT_TRUE(wr.is_success()) << wr.get_error()->what();
  const auto &wout = wr.value_or({});
  ASSERT_EQ(wout.size(), 1u);
  EXPECT_EQ(wout[0].status, OPC_UA_Client::WriteResult::Status::Good);

  // Both malformed responses must have been released by the guards.
  EXPECT_EQ(OPCUA_LSAN_CHECK(), 0);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}
