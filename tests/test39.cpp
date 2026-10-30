// tests/test39.cpp
//
// Lifecycle and configuration-snapshot regressions. Both cases are
// deterministic and run without a real server.
//
//   T150  A second shutdown() may return success immediately, but the owner
//         must not be able to destroy the client while the winning shutdown()
//         is still inside shutdownInternal(). Before the fix, ~OPC_UA_Client()
//         only waited for recreateGiveUpClient(), not for the shutdown winner,
//         so the object could be freed under it.
//   T151  ClientConfig is a value snapshot taken at create(). Mutating the
//         caller's object afterwards must not change retry policy.
//
// Run: ctest --test-dir build-asan -R ClientLifecycle

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
#include "tests/include/util/wait_for.h"

using namespace std::chrono_literals;

namespace {

std::unique_ptr<OPC_UA_Client>
makeClient(const std::shared_ptr<MockSdk> &sdk, ClientConfig cfg,
           bool enableWatchdog = false) {
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      enableWatchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

ClientConfig localCfg() {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 2000;
  cfg.watchdogIntervalMs = 1000;
  cfg.retryBackoffBaseMs = 10;
  cfg.retryMaxBackoffMs = 20;
  return cfg;
}

} // namespace

// ============================================================
// T150 (regression): destructor must wait for the winning shutdown().
//
// The second shutdown() is allowed to return immediately (existing T77
// contract). What is not allowed is freeing the object while the first
// shutdown() is still blocked in shutdownInternal() -> doomed.reset() ->
// ISdk::clientDelete(). T150 keeps the deleter blocked, destroys from another
// thread, and asserts the destructor does not return until the deleter is
// released.
// ============================================================
TEST(ClientLifecycle, T150_DestructorWaitsForInFlightShutdown) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg());
  ASSERT_NE(c, nullptr);
  OPC_UA_Client *raw = c.get();

  latch deleteEntered{1};
  latch releaseDelete{1};
  sdk->setClientDeleteHook([&] {
    deleteEntered.count_down();
    releaseDelete.wait();
  });

  std::atomic<bool> firstDone{false};
  std::thread first([&] {
    (void)raw->shutdown();
    firstDone.store(true, std::memory_order_release);
  });

  ASSERT_TRUE(waitFor([&] { return deleteEntered.try_wait(); }, 2s))
      << "第一个 shutdown 未走到 clientDelete；测试前提不成立";

  // Contract kept from T77: the loser returns without waiting for the drain.
  auto second = raw->shutdown();
  ASSERT_TRUE(second.is_success());

  // The owner now destroys the client. With the bug, ~OPC_UA_Client() finds
  // pImpl == nullptr (moved by the winner), returns immediately, and frees the
  // object while the winner is still in shutdownInternal().
  std::atomic<bool> destroyed{false};
  std::thread destroyer([&] {
    c.reset();
    destroyed.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(destroyed.load())
      << "析构在第一个 shutdown 仍在 clientDelete 中时返回；"
         "m_shutdownInFlight 没有参与析构等待";

  releaseDelete.count_down();
  first.join();
  destroyer.join();

  EXPECT_TRUE(firstDone.load());
  EXPECT_TRUE(destroyed.load());
}

// ============================================================
// T151: create() must snapshot ClientConfig, not keep a reference to the
// caller's object. maxRetries is the observable knob: the first read returns
// BADCONNECTIONCLOSED, the second returns GOOD.
// ============================================================
TEST(ClientLifecycle, T151_ConfigSnapshot_SurvivesCallerMutation) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg = localCfg();
  cfg.maxRetries = 1;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto c = makeClient(sdk, cfg);
  ASSERT_NE(c, nullptr);

  // If Impl kept a pointer/reference to cfg, the read below would see
  // maxRetries == 0 and fail after the first retryable status.
  cfg.maxRetries = 0;
  cfg.timeoutMs = 1;

  sdk->enqueueReadServiceResult(UA_STATUSCODE_BADCONNECTIONCLOSED);
  sdk->enqueueReadServiceResult(UA_STATUSCODE_GOOD);

  std::vector<OPC_UA_Client::ReadValue> reads{{1, "TestInt", S7DataType::INT}};
  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  EXPECT_EQ(sdk->serviceReadCallCount(), 2u)
      << "配置快照丢失：调用方把 maxRetries 改成 0 后封装层没有重试";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}
