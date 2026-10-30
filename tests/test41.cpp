// tests/test41.cpp
//
// Additional coverage for three API-contract gaps:
//   T155  default_value_for() must be available to every consumer of the
//         public S7TypeStruct.h and must cover every scalar PLC type. The
//         previous definition lived in the PLC library (undefined reference
//         for an OPCUAPacking-only consumer) and missed SINT/LINT/ULINT/LREAL/
//         DWORD.
//   T156  The public S7DataType <-> name tables must contain every enum value
//         and round-trip without loss.
//   T157  A GoodInfo item status (GOOD_CLAMPED) is still a value: batchRead
//         must return it instead of dropping it as an error.
//   T158  The same severity rule applies to batchWrite per-item statuses.
//   T159  A stateCallback that publishes a newer triplet while getState() is
//         in flight must not be overwritten by the stale getState snapshot.

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"
#include "tests/include/util/Cxx17Compat.h"
#include "tests/include/util/wait_for.h"

namespace {

std::unique_ptr<OPC_UA_Client> makeClient(const std::shared_ptr<MockSdk> &sdk,
                                          bool enableWatchdog = false) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      enableWatchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

} // namespace

// ---------------------------------------------------------------------------
// T155: every scalar type has a zero default; aggregate/unknown types throw
// instead of silently returning a mismatched alternative.
// ---------------------------------------------------------------------------
TEST(S7TypeStruct, T155_DefaultValueFor_AllScalarTypes) {
  EXPECT_EQ(std::get<bool>(default_value_for(S7DataType::BOOL)), false);
  EXPECT_EQ(std::get<int8_t>(default_value_for(S7DataType::SINT)), 0);
  EXPECT_EQ(std::get<uint8_t>(default_value_for(S7DataType::BYTE)), 0u);
  EXPECT_EQ(std::get<int16_t>(default_value_for(S7DataType::INT)), 0);
  EXPECT_EQ(std::get<uint16_t>(default_value_for(S7DataType::WORD)), 0u);
  EXPECT_EQ(std::get<int32_t>(default_value_for(S7DataType::DINT)), 0);
  EXPECT_EQ(std::get<uint32_t>(default_value_for(S7DataType::UDINT)), 0u);
  EXPECT_EQ(std::get<uint32_t>(default_value_for(S7DataType::DWORD)), 0u);
  EXPECT_EQ(std::get<int64_t>(default_value_for(S7DataType::LINT)), 0);
  EXPECT_EQ(std::get<uint64_t>(default_value_for(S7DataType::ULINT)), 0u);
  EXPECT_EQ(std::get<float>(default_value_for(S7DataType::REAL)), 0.0f);
  EXPECT_EQ(std::get<double>(default_value_for(S7DataType::LREAL)), 0.0);
  EXPECT_EQ(std::get<std::string>(default_value_for(S7DataType::STRING)), "");

  EXPECT_THROW(default_value_for(S7DataType::ARRAY), std::invalid_argument);
  EXPECT_THROW(default_value_for(S7DataType::STRUCT), std::invalid_argument);
  EXPECT_THROW(default_value_for(S7DataType::UNKNOWN), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// T156: both public conversion tables cover all 16 enum values and agree.
// ---------------------------------------------------------------------------
TEST(S7TypeStruct, T156_TypeNameTables_CompleteAndRoundTrip) {
  ASSERT_EQ(S7DataTypeToString.size(), 16u);
  ASSERT_EQ(typeMap.size(), 16u);
  for (const auto &[type, name] : S7DataTypeToString) {
    const auto it = typeMap.find(name);
    ASSERT_NE(it, typeMap.end()) << "typeMap misses " << name;
    EXPECT_EQ(it->second, type) << "round-trip mismatch for " << name;
  }
}

// ---------------------------------------------------------------------------
// T157: GOOD_CLAMPED is Good severity, so the value must survive.
// ---------------------------------------------------------------------------
TEST(ClientStatusSeverity, T157_BatchRead_GoodInfoStatus_ReturnsValue) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemStatus(0, UA_STATUSCODE_GOODCLAMPED);
  sdk->setReadItemInt16(0, 42);

  std::vector<OPC_UA_Client::ReadValue> reads{{1, "A", S7DataType::INT}};
  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOODCLAMPED);
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::OK);
  ASSERT_TRUE(out[0].value.has_value())
      << "GOOD_CLAMPED 被当成错误丢掉了值";
  EXPECT_EQ(std::get<int16_t>(*out[0].value), 42);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T158: the same severity rule on the write path.
// ---------------------------------------------------------------------------
TEST(ClientStatusSeverity, T158_BatchWrite_GoodInfoStatus_IsGood) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1);
  sdk->setWriteItemStatus(0, UA_STATUSCODE_GOODCLAMPED);

  std::vector<OPC_UA_Client::WriteValue> writes{
      {{1, "A", S7DataType::INT}, int16_t(7)}};
  auto wr = c->batchWrite(writes);
  ASSERT_TRUE(wr.is_success()) << (wr.is_fail() ? wr.get_error()->what() : "");
  const auto &out = wr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].status, OPC_UA_Client::WriteResult::Status::Good)
      << "GOOD_CLAMPED 被映射成 Bad";
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOODCLAMPED);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T159: a callback that lands while getState() is in flight is newer than the
// snapshot; publishing the snapshot afterwards loses the disconnect event and
// makes isHealthy() report a healthy connection.
// ---------------------------------------------------------------------------
TEST(ClientStateRace, T159_GetStateMustNotOverwriteNewerStateCallback) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  latch getStateEntered{1};
  latch releaseGetState{1};
  sdk->setGetStateHook([&] {
    getStateEntered.count_down();
    releaseGetState.wait();
  });

  std::thread checker([&] { (void)c->checkConnected(); });
  ASSERT_TRUE(waitFor([&] { return getStateEntered.try_wait(); },
                      std::chrono::seconds(1)))
      << "checkConnected 未进入 getState";

  // getState has already snapshotted GOOD/OPEN/ACTIVATED. Publish a newer bad
  // state through the same callback the SDK uses, then let getState return.
  sdk->fireStateCallback(UA_STATUSCODE_BADCONNECTIONCLOSED,
                         UA_SECURECHANNELSTATE_CLOSED,
                         UA_SESSIONSTATE_CLOSED);
  releaseGetState.count_down();
  checker.join();

  auto health = c->isHealthy();
  EXPECT_TRUE(health.is_fail())
      << "getState 的旧快照覆盖了更新的 stateCallback，isHealthy 仍报健康";
  if (health.is_fail())
    EXPECT_EQ(health.get_error()->code(), RichError::ErrorCode::NOT_CONNECTED);

  sdk->setGetStateHook(nullptr);
  EXPECT_TRUE(c->shutdown().is_success());
}
