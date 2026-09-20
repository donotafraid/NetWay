// tests/test13.cpp（同一文件）
#include <atomic>
#include <thread>
#include "tests/util/Cxx17Compat.h"
#include "tests/MockSdk.h"
#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include "tests/util/wait_for.h"

TEST(Client, T27_ApiRejectedWhileRecreating) {
  using namespace std::chrono_literals;

  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPC_UA_Client::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value());
  auto c = std::move(*r.get());

  // 1) 进入 GIVEN_UP
  c->setGiveUpSignal();
  ASSERT_TRUE(waitFor([&] {
    auto s = c->checkLifeState();
    return s.has_value() &&
           s.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
  }, 5s)) << "未能进入 GIVEN_UP，前置条件不成立";

  // 2) 让 recreate 卡在 SDK connectAsync 内 → 确定性地持住 m_recreating
  latch inRecreate{1}, release{1};
  std::atomic<bool> inRecreateFlag{false};
  sdk->setConnectAsyncHook([&] {
    inRecreateFlag.store(true);
    inRecreate.count_down();
    release.wait();          // 阻塞，直到主线程放行
  });

  std::atomic<bool> recreateDone{false};
  std::atomic<bool> recreateOk{false};
  std::thread recreator([&] {
    recreateOk.store(c->recreateGiveUpClient().is_success());
    recreateDone.store(true);
  });

  // 3) 等 recreate 真正进入临界区（m_recreating == true）
  ASSERT_TRUE(waitFor([&] { return inRecreateFlag.load(); }, 2s))
      << "recreate 未进入 connectAsync hook，前置条件不成立";

  // 4) 此刻 m_recreating 必为 true
  EXPECT_TRUE(c->getRecreatingStatus())
      << "进入 connectAsync 后 m_recreating 仍为 false，ApiLease 契约不成立";

  // 5) 契约断言：public API 必须快速失败
  std::vector<OPC_UA_Client::ReadValue> rv{
      {1, "TestInt", S7DataType::INT}};
  auto br = c->batchRead(rv);
  ASSERT_TRUE(br.is_fail())
      << "RECREATING 期间 batchRead 竟然成功，ApiLease 未拒绝";
  EXPECT_EQ(br.get_error()->code(), RichError::ErrorCode::RECREATING)
      << "context=" << br.get_error()->what();

  // 6) checkLifeState 同样应被 ApiLease 拒绝（走 allowUseAPI 分支）
  auto cl = c->checkLifeState();
  EXPECT_TRUE(cl.is_fail())
      << "RECREATING 期间 checkLifeState 未快速失败";

  // 7) 放行 recreate，验证状态复位
  release.count_down();
  ASSERT_TRUE(waitFor([&] { return recreateDone.load(); }, 5s))
      << "recreate 未在放行后返回（疑似死等）";
  recreator.join();

  EXPECT_TRUE(recreateOk.load()) << "recreate 本身应该成功";
  EXPECT_FALSE(c->getRecreatingStatus())
      << "recreate 完成后 m_recreating 未复位";
}