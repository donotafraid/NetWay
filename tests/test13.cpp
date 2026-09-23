// tests/test13.cpp
#include <gtest/gtest.h>
#include "OPCUAPacking/ua.h"
#include "tests/MockSdk.h"
#include "tests/util/wait_for.h"

#include "OPCUAPacking/internal/ClientTestHooks.h"

TEST(Client, T26_RecreateFailureKeepsOldImpl) {
  using namespace std::chrono_literals;

  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);

  // ★ 新增：记录 connectAsync 被调用的次数
  std::atomic<int> connectAsyncCalls{0};
  sdk->setConnectAsyncHook([&] { ++connectAsyncCalls; });

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs          = 200;   // recreate 的 connect 快速超时
  cfg.retryBackoffBaseMs = 10;
  cfg.retryMaxBackoffMs  = 50;    // check() 要求 retryMaxBackoffMs < timeoutMs
  cfg.maxTotalWaitMs     = 5000;  // >= 2*timeout*(maxRetries+1)
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r = ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  // 初始 create 的 connect 也会走 hook；记下基线
  const int baseline = connectAsyncCalls.load();
  ASSERT_GE(baseline, 1) << "初始 create 未走 connectAsync，MockSdk 配置有问题";

  // 1) 进入 GIVEN_UP（setGiveUpSignal 是业务主动放弃的旁路）
  c->setGiveUpSignal();
  ASSERT_TRUE(waitFor([&] {
    auto s = c->checkLifeState();
    return s.has_value() &&
           s.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
  }, 5s)) << "未能进入 GIVEN_UP，前置条件不成立";

  // 2) 让 SDK 保持 Unreachable 且不允许自动恢复
  //    → recreate 内部新建 Impl 后的 connect() 必然失败
  sdk->setMode(MockSdk::Mode::Unreachable, /*allowAuto=*/false);

  // 3) 触发 recreate，期望失败
  auto rr = c->recreateGiveUpClient();
  ASSERT_TRUE(rr.is_fail()) << "重建 connect 应失败，但 recreate 返回了 success";

  // ★ 新增断言 0：recreate 必须真的走到了新 Impl 的 connect
  const int after = connectAsyncCalls.load();
  EXPECT_GT(after, baseline)
      << "recreate 没有调用 connectAsync（baseline=" << baseline
      << ", after=" << after << "），"
      << "说明它失败在更早的检查点，T26 未验证到'新 Impl connect "
         "失败后回滚'的契约";

  // 4) 契约断言：旧 pImpl 必须保持不变
  auto s2 = c->checkLifeState();
  ASSERT_TRUE(s2.has_value())
      << "checkLifeState 在 recreate 失败后返回错误，说明旧 Impl 已被破坏: "
      << (s2.get_error() ? s2.get_error()->what() : "");
  EXPECT_EQ(s2.value_or(LifeState::RUNNING), LifeState::GIVEN_UP)
      << "重建失败后 lifeState 变了，违反 ua.h:362 契约"
      << "（当前值=" << static_cast<int>(s2.value_or({})) << "）";

  // 5) 旧实现仍应可被幂等关闭（证明没有被半替换破坏）
  auto sr = c->shutdown();
  EXPECT_TRUE(sr.is_success())
      << "重建失败后旧实现无法 shutdown: "
      << (sr.get_error() ? sr.get_error()->what() : "");
  std::cout << "[T26] baseline=" << baseline << " after=" << after << std::endl;
}