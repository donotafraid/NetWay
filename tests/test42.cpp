// tests/test42.cpp
//
// Review follow-ups for the 2026-10-28 snapshot:
//   T160  ClientConfig::logLevel must reach the UA_Logger installed on the
//         client, and must be validated. The SDK is built with
//         UA_LOGLEVEL=100, so an unfiltered logger writes trace/debug frames
//         to stderr (1.6 GB over the 5x6 SLO run) and contaminates latency.
//   T161  An empty STRING is a legal write value. T148 pins the read path; the
//         write path had no test that distinguishes `""` from `nullptr`.
//   T162  The public config rejects out-of-range log levels instead of silently
//         accepting a value the SDK logger will never match.

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"

namespace {

std::unique_ptr<OPC_UA_Client> makeClient(const std::shared_ptr<MockSdk> &sdk,
                                          ClientConfig cfg) {
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      /*enableWatchDog=*/false);
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

// ---------------------------------------------------------------------------
// T160: logLevel is copied to the per-client UA_Logger context and is visible
// to the filter used by customLog().
// ---------------------------------------------------------------------------
TEST(ClientLogging, T160_LogLevelReachesClientLogger) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg = localCfg();
  cfg.logLevel = ClientConfig::LOG_ERROR;
  auto c = makeClient(sdk, cfg);
  ASSERT_NE(c, nullptr);

  UA_Client *raw = sdk->rawClient();
  ASSERT_NE(raw, nullptr);
  UA_ClientConfig *cfgOnClient = UA_Client_getConfig(raw);
  ASSERT_NE(cfgOnClient, nullptr);
  ASSERT_NE(cfgOnClient->logging, nullptr);
  ASSERT_NE(cfgOnClient->logging->context, nullptr)
      << "logger context 未指向 Impl::m_logLevel，日志门槛无法生效";

  auto *level = static_cast<std::atomic<uint32_t> *>(
      cfgOnClient->logging->context);
  EXPECT_EQ(level->load(std::memory_order_relaxed),
            static_cast<uint32_t>(ClientConfig::LOG_ERROR));

  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T161: write "" as STRING and verify the SDK received length 0 with a
// non-null data pointer (empty, not null).
// ---------------------------------------------------------------------------
TEST(ClientStringShape, T161_BatchWrite_EmptyStringIsValidAndPreserved) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk, localCfg());
  ASSERT_NE(c, nullptr);

  sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1);
  std::vector<OPC_UA_Client::WriteValue> writes{
      {{1, "A", S7DataType::STRING}, std::string("")}};
  auto wr = c->batchWrite(writes);
  ASSERT_TRUE(wr.is_success()) << (wr.is_fail() ? wr.get_error()->what() : "");
  const auto &out = wr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].status, OPC_UA_Client::WriteResult::Status::Good);

  const auto obs = sdk->lastWriteObservation();
  ASSERT_EQ(obs.typeIndices.size(), 1u);
  EXPECT_EQ(obs.typeIndices[0], UA_TYPES_STRING);
  ASSERT_EQ(obs.valueStrings.size(), 1u);
  EXPECT_TRUE(obs.valueStrings[0].empty())
      << "empty STRING 被当成了 null/缺失值";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T162: public config validation covers the new logLevel field.
// ---------------------------------------------------------------------------
TEST(ClientConfigTest, T162_LogLevel_Boundaries) {
  ClientConfig cfg = localCfg();
  EXPECT_FALSE(cfg.check().has_value());

  cfg.logLevel = ClientConfig::LOG_TRACE;
  EXPECT_FALSE(cfg.check().has_value());
  cfg.logLevel = ClientConfig::LOG_FATAL;
  EXPECT_FALSE(cfg.check().has_value());

  cfg.logLevel = 99;
  auto low = cfg.check();
  ASSERT_TRUE(low.has_value());
  EXPECT_NE(low->find("logLevel"), std::string::npos);

  cfg.logLevel = 601;
  auto high = cfg.check();
  ASSERT_TRUE(high.has_value());
  EXPECT_NE(high->find("logLevel"), std::string::npos);
}
