// 补充用例：T66 覆盖了“recreate 进行中
// 其它 API 被拒”，但没有覆盖“两个线程同时 recreate”。本用例用 MockSdk 的
// connectAsync 阻塞钩子把赢家确定性地卡在临界区内，验证 RecreateGuard/ApiLease
// 的互斥语义：恰好一个成功，另一个以 RECREATING 快速失败。
//
// 已登记到 scripts/run_tsan.sh 的 FILTER_CONCURRENCY（Client.T91_*）。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"
#include "tests/include/util/Cxx17Compat.h" // latch / JThread
#include "tests/include/util/wait_for.h"

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
// T91: 两个线程并发 recreateGiveUpClient()
// ============================================================
TEST(Client, T91_ConcurrentRecreate_ExactlyOneWins) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  c->setGiveUpSignal();
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::GIVEN_UP);

  latch inRecreate{1}, release{1};
  sdk->setConnectAsyncHook([&] {
    inRecreate.count_down();
    release.wait(); // 持住 m_recreating，直到主线程放行
  });

  std::atomic<int> ok{0}, recreating{0}, other{0};
  auto attempt = [&] {
    auto rr = c->recreateGiveUpClient();
    if (rr.is_success())
      ok.fetch_add(1, std::memory_order_relaxed);
    else if (rr.get_error()->code() == RichError::ErrorCode::RECREATING)
      recreating.fetch_add(1, std::memory_order_relaxed);
    else
      other.fetch_add(1, std::memory_order_relaxed);
  };

  JThread winner(attempt);
  ASSERT_TRUE(waitFor([&] { return inRecreate.try_wait(); }, 2s))
      << "赢家未进入 connectAsync，前置条件不成立";

  {
    JThread loser(attempt); // m_recreating == true，必须立即被拒
    loser.join();
  }

  release.count_down();
  winner.join();

  EXPECT_EQ(ok.load(), 1) << "并发 recreate 应恰好一个成功";
  EXPECT_EQ(recreating.load(), 1) << "输家应以 RECREATING 快速失败";
  EXPECT_EQ(other.load(), 0);
  EXPECT_TRUE(c->shutdown().is_success());
}