// T84–T86 的落地补测：
//   T84  batchRead maxRetries==0：只尝试一次，可重试错误直接
//        SERVICE_RETRY_EXHAUSTED，且不进入退避等待；
//   T85  并发只读查询(checkConnected/isHealthy) × shutdown：ApiLease 排空、
//        无 UAF（须由 run_tsan.sh 执行才有竞态检测价值）；
//   T86  shutdown 之后 recreateGiveUpClient 必须被拒绝，不得把对象“复活”。
//
// 这些用例补的是现有 T20/T25/T66 未覆盖的入口组合。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"
#include "tests/include/util/Cxx17Compat.h"

using namespace std::chrono_literals;

namespace {
std::unique_ptr<OPC_UA_Client>
makeClient(const std::shared_ptr<MockSdk> &sdk, bool watchdog = false) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false, watchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}
} // namespace

// ============================================================
// T84: maxRetries==0 —— 单次尝试，不做退避
// ============================================================
TEST(Client, T84_BatchRead_MaxRetriesZero_SingleAttempt) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.maxRetries = 0;
  cfg.timeoutMs = 500;
  cfg.retryBackoffBaseMs = 50;
  cfg.retryMaxBackoffMs = 100;
  cfg.maxTotalWaitMs = 5000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      /*enableWatchDog=*/false);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  sdk->setReadResponseShape(UA_STATUSCODE_BADCONNECTIONCLOSED, 0, false);

  const auto t0 = std::chrono::steady_clock::now();
  std::vector<OPC_UA_Client::ReadValue> v{{1, "N", S7DataType::INT}};
  auto rr = c->batchRead(v);
  const auto dt = std::chrono::steady_clock::now() - t0;

  ASSERT_TRUE(rr.is_fail());
  EXPECT_EQ(rr.get_error()->code(),
            RichError::ErrorCode::SERVICE_RETRY_EXHAUSTED);
  EXPECT_EQ(sdk->serviceReadCallCount(), 1u) << "maxRetries=0 时不应额外尝试";
  EXPECT_LT(dt, 400ms) << "maxRetries=0 时不应进入退避等待";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T85: 并发只读查询 × shutdown —— ApiLease 排空，无 UAF
// ============================================================
TEST(Client, T85_ConcurrentReadOnlyQueriesVsShutdown_NoUaf) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  std::atomic<bool> stop{false};
  std::atomic<unsigned> success{0};
  std::atomic<unsigned> rejected{0};

  std::vector<JThread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&] {
      while (!stop.load(std::memory_order_acquire)) {
        if (c->checkConnected().is_success())
          success.fetch_add(1, std::memory_order_relaxed);
        else
          rejected.fetch_add(1, std::memory_order_relaxed);
        if (c->isHealthy().is_success())
          success.fetch_add(1, std::memory_order_relaxed);
        else
          rejected.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  std::this_thread::sleep_for(50ms);
  EXPECT_TRUE(c->shutdown().is_success());
  stop.store(true, std::memory_order_release);
  for (auto &th : threads)
    th.join();

  EXPECT_GT(success.load() + rejected.load(), 0u);
}

// ============================================================
// T86: shutdown 之后不得通过 recreateGiveUpClient() 复活对象
// ua.h 私有注释写明：shutdown() 代表整个对象终结，禁止 pImpl 由空->非空。
// ============================================================
TEST(Client, T86_RecreateAfterShutdown_IsRejected_NoResurrection) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  ASSERT_TRUE(c->shutdown().is_success());

  auto rr = c->recreateGiveUpClient();
  ASSERT_TRUE(rr.is_fail());
  EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED)
      << "actual=" << rr.get_error()->codeName();

  // 不得安装新 Impl：所有入口仍应快速失败。
  EXPECT_TRUE(c->batchRead({{1, "N", S7DataType::INT}}).is_fail());
  EXPECT_TRUE(
      c->batchWrite({{{1, "N", S7DataType::INT}, int16_t(1)}}).is_fail());
  EXPECT_TRUE(c->connect().is_fail());
}
