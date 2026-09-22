#include "tests/util/ClientTestFixture.h"
#include "OPCUAPacking/ua.h"
#include <open62541/client.h>
#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include "tests/MockSdk.h"
#include "tests/util/Cxx17Compat.h"   
#include "tests/util/wait_for.h"

#include "OPCUAPacking/detail/ClientTestHooks.h"

// T8-B: 验证"非 GIVEN_UP 状态下 recreate 必须返回错误"
TEST(Client, T8B_RecreateRequiresGivenUpState_Mock) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  std::string endpointUrl = "opc.tcp://192.0.2.1:4841";
  auto createResult = OPC_UA_Client::createWithSdk(sdk, endpointUrl, cfg);
  ASSERT_TRUE(createResult.has_value());
  auto c = std::move(*createResult.get());

  auto r = c->recreateGiveUpClient();
  ASSERT_TRUE(r.is_fail());              
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::NOT_GIVEN_UP)
      << "context=" << r.get_error()->what();
}

namespace {
using namespace std::chrono_literals;
constexpr auto kDestroyBlockedWindow = 300ms;
constexpr auto kRecreateReleaseWait  = 5s;
constexpr auto kDestroyAfterRelease  = 5s;
}  // namespace

TEST(Client, T8C1_ShutdownRejectedWhileRecreating) {
  using namespace std::chrono_literals;

  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto createResult =
      OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(createResult.has_value())
      << "create failed: " << createResult.get_error()->what();
  auto client = std::move(*createResult.get());
  ASSERT_NE(client, nullptr);

  // 1) 进入 GIVEN_UP
  client->setGiveUpSignal();
  ASSERT_TRUE(waitFor([&] {
    auto s = client->checkLifeState();
    return s.has_value() &&
           s.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
  }, 5s)) << "未进入 GIVEN_UP, 无法调用 recreateGiveUpClient()";

  // 2) 卡住 recreate 的 connectAsync，确定性地持住 m_recreating
  latch inRecreate{1};
  latch release{1};
  sdk->setConnectAsyncHook([&] {
    inRecreate.count_down();
    release.wait();
  });

  std::atomic<bool> recreateReturned{false};
  std::atomic<bool> recreateSucceeded{false};
  std::thread recreator([&] {
    auto r = client->recreateGiveUpClient();
    recreateSucceeded.store(r.is_success(), std::memory_order_release);
    recreateReturned.store(true, std::memory_order_release);
  });

  ASSERT_TRUE(waitFor([&] { return inRecreate.try_wait(); }, 2s))
      << "recreator 未在 2s 内进入 connectAsync 钩子，前置条件不成立";
  EXPECT_FALSE(ClientTestHooks::getRecreatingStatus(*client))
      << "进入 connectAsync 后 m_recreating 仍为 false";

  // 3) 核心契约：recreate 期间 shutdown() 必须返回错误
  auto srDuring = client->shutdown();
  ASSERT_TRUE(srDuring.is_fail())
      << "RECREATING 期间 shutdown 竟然成功，与 m_recreating 互斥契约不符";
  EXPECT_EQ(srDuring.get_error()->code(), RichError::ErrorCode::RECREATING);

  // 4) 关键前提：shutdown 被拒绝后，不应该置 m_terminated
  //    否则 recreate 会被迫失败
  EXPECT_FALSE(ClientTestHooks::getRecreatingStatus(*client))
      << "shutdown 被拒绝后 m_recreating 被误复位，违反拒绝语义";

  // 5) 放行 recreate，验证它仍能成功
  release.count_down();
  ASSERT_TRUE(waitFor(
      [&] { return recreateReturned.load(std::memory_order_acquire); },
      5s)) << "recreator 在放行后未返回，疑似死锁";
  recreator.join();

  EXPECT_TRUE(recreateSucceeded.load(std::memory_order_acquire))
      << "recreate 在 shutdown 被拒后应仍成功，方案 B 契约不成立";
  EXPECT_FALSE(ClientTestHooks::getRecreatingStatus(*client))
      << "recreate 完成后 m_recreating 未复位";

  // 6) recreate 完成后 shutdown 应成功（幂等）
  EXPECT_TRUE(client->shutdown().is_success())
      << "recreate 完成后 shutdown 仍失败，生命周期状态机异常";
}

TEST(Client, T8C2_DestructorAfterRecreate_CompletesPromptly) {
  using namespace std::chrono_literals;

  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto createResult =
      OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(createResult.has_value())
      << "create failed: " << createResult.get_error()->what();
  auto client = std::move(*createResult.get());
  ASSERT_NE(client, nullptr);

  // 1) 进入 GIVEN_UP
  client->setGiveUpSignal();
  ASSERT_TRUE(waitFor(
      [&] {
        auto s = client->checkLifeState();
        return s.has_value() &&
               s.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
      },
      5s))
      << "未进入 GIVEN_UP";

  // 2) 顺序执行 recreate，等它完成
  auto rr = client->recreateGiveUpClient();
  ASSERT_TRUE(rr.is_success()) << "recreate 失败: " << rr.get_error()->what();

  // 3) 关键前置：recreate 完成后 m_recreating 必须已复位
  EXPECT_FALSE(ClientTestHooks::getRecreatingStatus(*client))
      << "recreate 完成后 m_recreating 未复位，析构会卡到 10s 超时";

  // 4) 契约断言：析构必须迅速完成
  const auto t0 = std::chrono::steady_clock::now();
  client.reset();
  const auto dt = std::chrono::steady_clock::now() - t0;

  EXPECT_LT(dt, 2s)
      << "recreate 完成后析构耗时 "
      << std::chrono::duration_cast<std::chrono::milliseconds>(dt).count()
      << "ms，疑似 m_recreating 未复位或析构无界等待";
}