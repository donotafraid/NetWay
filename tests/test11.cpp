
// tests/test11_error_paths.cpp
//
// 报告 §6.3：补齐"错误出口也有测试"。
// T20: 所有 batchRead/batchWrite 错误出口 + LSan
// T21: 可重试状态 → 成功（覆盖重试循环 + delayFunction + 双 Guard）
// T22: maxTotalWaitMs 预算超限
// T23: create 的 useDefault 语义
// T24: ClientConfig::check() 边界表驱动
//
// 说明：T23 需要 createWithSdk 提供 useDefault 形参（报告 §6.3.4 建议）。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <atomic>
#include <functional>
#include <string>
#include <variant>
#include <vector>
#include "tests/MockSdk.h"
#include "tests/util/Cxx17Compat.h"
#include "tests/util/wait_for.h"

#include "OPCUAPacking/internal/ClientTestHooks.h"
// ============================================================
// T20: batchRead/batchWrite 所有错误出口
// ============================================================
TEST(Client, T20_AllReadWriteErrorPaths_NoLeak) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  const std::vector<OPC_UA_Client::ReadValue>  empty;
  const std::vector<OPC_UA_Client::ReadValue>  badId{{ {1, "", S7DataType::INT} }};
  const std::vector<OPC_UA_Client::WriteValue> wEmpty;
  const std::vector<OPC_UA_Client::WriteValue> wBad{{ {1, "", S7DataType::INT}, int16_t(0) }};

  {
    auto r1 = c->batchRead(empty);
    ASSERT_TRUE(r1.is_fail());
    EXPECT_EQ(r1.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);

    auto r2 = c->batchRead(badId);
    ASSERT_TRUE(r2.is_fail());
    EXPECT_EQ(r2.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);

    auto r3 = c->batchWrite(wEmpty);
    ASSERT_TRUE(r3.is_fail());
    EXPECT_EQ(r3.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);

    auto r4 = c->batchWrite(wBad);
    ASSERT_TRUE(r4.is_fail());
    EXPECT_EQ(r4.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);

    c->setGiveUpSignal();
    std::vector<OPC_UA_Client::ReadValue> good{
        {{1, "TestInt", S7DataType::INT}}};
    auto r5 = c->batchRead(good);
    ASSERT_TRUE(r5.is_fail());
    // 注意：GIVEN_UP 时 batchRead 在 Impl 内会先命中 getEffectiveStopSignal
    // → ALREADY_TERMINATED，而不是 NOT_CONNECTED。以实际为准。
    EXPECT_TRUE(r5.get_error()->code() == RichError::ErrorCode::NOT_CONNECTED ||
                r5.get_error()->code() ==
                    RichError::ErrorCode::ALREADY_TERMINATED)
        << "actual code=" << r5.get_error()->codeName();
  }

  c->setGiveUpSignal();
  std::vector<OPC_UA_Client::ReadValue> good{{ {1, "TestInt", S7DataType::INT} }};
  EXPECT_TRUE(c->batchRead(good).is_fail());   // GIVEN_UP 快速失败
  // 每个出口都应在 ASan/LSan 下不泄漏
}

// ============================================================
// T21: 可重试状态 → 成功
// ============================================================
TEST(Client, T21_BatchRead_RetriesTransientThenSucceeds) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->enqueueReadServiceResult(UA_STATUSCODE_BADCONNECTIONCLOSED);
  sdk->enqueueReadServiceResult(UA_STATUSCODE_GOOD);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.retryBackoffBaseMs = 10;
  cfg.retryMaxBackoffMs  = 20;
  auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  std::vector<OPC_UA_Client::ReadValue> v{{{1, "TestInt", S7DataType::INT}}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_success()) << rr.get_error()->what();
  EXPECT_EQ(sdk->serviceReadCallCount(), 2u);
  EXPECT_EQ(rr.value_or({})[0].rawStatus, UA_STATUSCODE_GOOD);
}

// ============================================================
// T22: maxTotalWaitMs 预算超限（虚拟时钟驱动）
//
// 前提：实现层的预算检查读 sdk->nowMs()（而非 steady_clock），
//       且 MockSdk 的 serviceRead 也推进虚拟时钟。
// 收益：测试从 ~500ms 真实时间降到 <10ms 虚拟时间，且完全确定。
// ============================================================
TEST(Client, T22_BatchRead_BudgetExceeded_VirtualClock) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 20;
  cfg.maxRetries = 10;
  cfg.retryBackoffBaseMs = 5;
  cfg.retryMaxBackoffMs = 10;
  // 满足 check() 约束：2 * 20 * 11 = 440
  cfg.maxTotalWaitMs = 2 * cfg.timeoutMs * (cfg.maxRetries + 1);
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  // ★ 关键：每次 serviceRead / runIterate 推进 50ms 虚拟时间
  //    预算 440ms ÷ 50ms ≈ 9 次调用后触发
  sdk->setAutoAdvancePerIterateMs(50);

  for (int i = 0; i < 200; ++i) {
    sdk->enqueueReadServiceResult(UA_STATUSCODE_BADCONNECTIONCLOSED);
  }

  auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  const int64_t t0 = sdk->nowMs();
  std::vector<OPC_UA_Client::ReadValue> v{{{1, "TestInt", S7DataType::INT}}};
  auto rr = c->batchRead(v);
  const int64_t t1 = sdk->nowMs();

  const auto calls = sdk->serviceReadCallCount();
  const std::string err = rr.get_error()->what();

  std::cerr << "[T22] error           = " << err << "\n";
  std::cerr << "[T22] calls           = " << calls << "\n";
  std::cerr << "[T22] virtual elapsed = " << (t1 - t0) << "\n";
  std::cerr << "[T22] budget          = " << cfg.maxTotalWaitMs << "\n";

  ASSERT_TRUE(rr.is_fail());

  // ① 预算必须比“重试次数用尽”先触发
  EXPECT_LT(calls, static_cast<unsigned>(cfg.maxRetries) + 1)
      << "预算未先于重试次数触发（calls=" << calls
      << "，maxRetries+1=" << (cfg.maxRetries + 1) << "）";

  // ② 错误类别是预算超限，不是次数用尽
  EXPECT_EQ(err.find("max retries"), std::string::npos)
      << "错误仍是‘重试次数用尽’，预算逻辑未生效: " << err;
  EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::BUDGET_EXCEEDED)
      << "context=" << rr.get_error()->what();

  // ③ 虚拟时钟确实被推进到 ≥ 预算值
  //    这是“虚拟时钟真正可用”的直接证据
  EXPECT_GE(t1 - t0, cfg.maxTotalWaitMs)
      << "虚拟时钟推进不足预算，预算检查可能仍读真实时间";
  EXPECT_LT(t1 - t0, cfg.maxTotalWaitMs + 2 * 50) // 容两步
      << "虚拟时钟推进远超预算，说明不是预算先触发的";
}

// ============================================================
// T23: create 的 useDefault 语义
// 需要 createWithSdk(..., useDefault) 形参；见报告 §6.3.4
// ============================================================
TEST(Client, T23_CreateWithInvalidConfig_useDefaultFalse_Rejected) {
  ClientConfig bad;                        // 全默认：maxTotalWaitMs==0
  ASSERT_TRUE(bad.check().has_value());
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto r = ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", bad);
  EXPECT_TRUE(r.is_fail());
}

// ============================================================
// T24: ClientConfig::check() 边界表驱动
// ============================================================
struct CheckCase {
  const char* name;
  std::function<void(ClientConfig&)> mut;
  bool ok;
};

TEST(ClientConfigTest, Check_Boundaries) {
  const std::vector<CheckCase> cases = {
    {"baseline",                 [](ClientConfig&){},                          true },
    {"timeout=0",                [](ClientConfig& c){ c.timeoutMs = 0; },      false},
    {"sessionTimeout=0",         [](ClientConfig& c){ c.sessionTimeoutMs = 0; },false},
    {"secureChannel<session",    [](ClientConfig& c){ c.secureChannelLifeTimeMs = c.sessionTimeoutMs - 1; }, false},
    {"maxRetries=11",            [](ClientConfig& c){ c.maxRetries = 11; },    false},
    {"watchdog=999",             [](ClientConfig& c){ c.watchdogIntervalMs = 999; },  false},
    {"watchdog=30001",           [](ClientConfig& c){ c.watchdogIntervalMs = 30001; },false},
    {"retryMax<base",            [](ClientConfig& c){ c.retryMaxBackoffMs = c.retryBackoffBaseMs - 1; }, false},
    {"connectivity<1000",        [](ClientConfig& c){ c.connectivityCheckIntervalMs = 999; }, false},
    {"giveUp<2*watchdog",        [](ClientConfig& c){ c.giveUpThresholdMs = 2LL * c.watchdogIntervalMs - 1; }, false},
    {"maxTotalWaitMs=0",         [](ClientConfig& c){ c.maxTotalWaitMs = 0; }, false},
    {"maxTotalWaitMs<=timeout",  [](ClientConfig& c){ c.maxTotalWaitMs = c.timeoutMs; }, false},
  };
  for (const auto& t : cases) {
    ClientConfig c;
    c.applyProfile(ClientConfig::PROFILE_LOCAL);
    t.mut(c);
    EXPECT_EQ(!c.check().has_value(), t.ok) << t.name
        << " err=" << (c.check() ? *c.check() : "ok");
  }
}