// tests/test40.cpp
//
// Lifecycle and tail-latency regressions:
//   T152  The watchdog's idle event-loop poll must not hold the wrapper's
//         m_sdkMutex across UA_Client_run_iterate's full timeout. With
//         timeout=50ms the poll blocks every service call for up to 50ms
//         every 100ms; the pre-fix measured 8-thread p99 is 67-147ms while
//         the 1-thread p99 is 0.3-0.9ms.
//   T153  recreateGiveUpClient() must keep m_recreating set until the old
//         Impl has finished shutdown(). Resetting it before old->shutdown()
//         lets the destructor return while the recreator is still blocked in
//         ISdk::clientDelete, the same class of lifetime hole T150 fixed for
//         the shutdown winner.
//
// Run: ctest --test-dir build-asan -R 'ClientLifecycle.T15[23]'

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
           bool enableWatchdog = true) {
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
// T152: post-create runIterate calls come from the watchdog loop only until
// the first decision (watchdogIntervalMs=1000). The first such call must be
// non-blocking; a 50ms idle poll is what turns the 8-thread p99 into
// 67-147ms while the 1-thread p99 stays below 1ms.
// ============================================================
TEST(ClientLifecycle, T152_WatchdogIdlePollMustBeNonBlocking) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg());
  ASSERT_NE(c, nullptr);

  sdk->clearRunIterateTimeouts();

  // The watchdog wakes every 100ms. Do not wait for watchdogIntervalMs, or the
  // first decision path (which legitimately uses a blocking iterate) could be
  // observed instead of the idle poll.
  ASSERT_TRUE(waitFor([&] { return !sdk->runIterateTimeouts().empty(); }, 500ms))
      << "看门狗未在 500ms 内驱动一次 run_iterate";

  const auto timeouts = sdk->runIterateTimeouts();
  ASSERT_FALSE(timeouts.empty());
  EXPECT_LE(timeouts.front(), 1u)
      << "看门狗空闲轮询把 timeout=" << timeouts.front()
      << "ms 传给 run_iterate；该调用期间持有 m_sdkMutex，会阻塞所有服务调用";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T153: keep the destructor waiting for the recreate tail.
//
// The recreator swaps pImpl, then calls old->shutdown(). The old client's
// ISdk::clientDelete is blocked. If m_recreating is cleared before that call,
// the destructor sees false and frees the OPC_UA_Client while the recreator is
// still inside its member function.
// ============================================================
TEST(ClientLifecycle, T153_DestructorWaitsForRecreateTail) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg());
  ASSERT_NE(c, nullptr);
  OPC_UA_Client *raw = c.get();

  ASSERT_TRUE(c->setGiveUpSignal().is_success());

  latch oldDeleteEntered{1};
  latch releaseOldDelete{1};
  std::atomic<int> deleteCount{0};

  // The first delete is the old client during recreate; subsequent deletes
  // (the replacement client, from ~OPC_UA_Client) must not block.
  sdk->setClientDeleteHook([&] {
    if (deleteCount.fetch_add(1, std::memory_order_acq_rel) == 0) {
      oldDeleteEntered.count_down();
      releaseOldDelete.wait();
    }
  });

  std::atomic<bool> recreateReturned{false};
  std::atomic<bool> recreateSucceeded{false};
  std::thread recreator([&] {
    auto r = raw->recreateGiveUpClient();
    recreateSucceeded.store(r.is_success(), std::memory_order_release);
    recreateReturned.store(true, std::memory_order_release);
  });

  ASSERT_TRUE(waitFor([&] { return oldDeleteEntered.try_wait(); }, 5s))
      << "recreate 未在 5s 内走到旧 clientDelete；前置条件不成立";

  std::atomic<bool> destroyed{false};
  std::thread destroyer([&] {
    c.reset();
    destroyed.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(destroyed.load())
      << "析构在 recreate 仍阻塞于旧 clientDelete 时返回；"
         "m_recreating 在 old->shutdown() 之前就被复位了";

  releaseOldDelete.count_down();
  ASSERT_TRUE(waitFor(
      [&] { return recreateReturned.load(std::memory_order_acquire); }, 5s))
      << "放行旧 clientDelete 后 recreate 未返回，疑似死锁";
  destroyer.join();
  recreator.join();

  EXPECT_TRUE(recreateSucceeded.load());
  EXPECT_TRUE(destroyed.load());
}

// ============================================================
// T154: the watchdog's health probe must also be non-blocking. The top-level
// loop already polled with timeout=0; doing a 50ms iterate again inside
// doWatchdogDecision() re-introduces a periodic 50ms hold every
// watchdogIntervalMs (1s), which is what keeps p99.9 in the hundreds of ms.
// No watchdog thread here: pump the decision synchronously so the first
// recorded runIterate is the decision itself.
// ============================================================
TEST(ClientLifecycle, T154_WatchdogDecisionHealthProbeMustBeNonBlocking) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg(), /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  sdk->clearRunIterateTimeouts();
  OPCUAPacking::test::ClientTestHooks::pumpWatchdogForTest(*c);

  const auto timeouts = sdk->runIterateTimeouts();
  ASSERT_FALSE(timeouts.empty())
      << "看门狗裁决没有驱动 run_iterate，健康状态不会刷新";
  EXPECT_LE(timeouts.front(), 1u)
      << "看门狗裁决的健康探针把 timeout=" << timeouts.front()
      << "ms 传给 run_iterate；这会每个 watchdogIntervalMs 阻塞服务调用一次";

  EXPECT_TRUE(c->shutdown().is_success());
}
