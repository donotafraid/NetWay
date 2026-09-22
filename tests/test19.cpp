// tests/test19.cpp
//
// 回调驱动路径与关闭后契约补充（对应审查报告 §6.3 / G10、G11、G12）：
//   T43  真实 SDK 会通过 stateCallback 推送三元组；封装必须消费它
//   T44  inactivityCallback 置 connectionLost，看门狗走
//        “disconnect  connect” 恢复分支（此前完全未覆盖）
//   T45  关闭后的公共 API 错误码（当前实现统一 NOT_INITIALIZED，
//        recreate 却是 ALREADY_TERMINATED —— 用测试把语义钉住）
//
// 说明：MockSdk 新增 fireStateCallback / fireInactivityCallback，
// 它们调用的是 UA_ClientConfig 里封装注册的真实回调，因此覆盖的是
// 封装层的回调消费逻辑，而不是 Mock 自定义的 getState 分支。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "tests/MockSdk.h"
#include "tests/util/wait_for.h"
#include "OPCUAPacking/detail/ClientTestHooks.h"

namespace {
std::unique_ptr<OPC_UA_Client>
makeClient(const std::shared_ptr<MockSdk> &sdk, bool enableWatchdog) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPC_UA_Client::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      enableWatchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}
} // namespace

// ============================================================
// T43: stateCallback 推送的三元组必须参与健康判定
// ============================================================
TEST(Client, T43_StateCallbackDrivesHealthGate) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  // 让读请求有一个可成功的响应形状。
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, 1, true);
  std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
  ASSERT_TRUE(c->batchRead(v).is_success());

  // 回调报告：通道 OPEN、会话 CLOSED → isHealthy() 必须为 false。
  sdk->fireStateCallback(UA_STATUSCODE_GOOD, UA_SECURECHANNELSTATE_OPEN,
                         UA_SESSIONSTATE_CLOSED);
  const unsigned before = sdk->serviceReadCallCount();
  auto r2 = c->batchRead(v);
  ASSERT_TRUE(r2.is_fail());
  EXPECT_EQ(r2.get_error()->code(), RichError::ErrorCode::NOT_CONNECTED);
  EXPECT_EQ(sdk->serviceReadCallCount(), before)
      << "健康判定为 false 时不应发起 SDK 调用";

  // 会话重新激活 → 恢复可读。
  sdk->fireStateCallback(UA_STATUSCODE_GOOD, UA_SECURECHANNELSTATE_OPEN,
                         UA_SESSIONSTATE_ACTIVATED);
  EXPECT_TRUE(c->batchRead(v).is_success());

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T44: inactivityCallback → connectionLost → 看门狗重连分支
// ============================================================
TEST(Client, T44_InactivityCallbackTriggersWatchdogReconnect) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);

  auto c = makeClient(sdk, /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  const unsigned connectBefore = sdk->connectAsyncCallCount();
  const unsigned disconnectBefore = sdk->disconnectAsyncCallCount();

  // 只置 connectionLost，不改 mode：走 else-if(connectionLost) 分支。
  sdk->fireInactivityCallback();
  ClientTestHooks::pumpWatchdogForTest(*c);


  EXPECT_GT(sdk->disconnectAsyncCallCount(), disconnectBefore)
      << "connectionLost 分支未先断开旧连接";
  EXPECT_GT(sdk->connectAsyncCallCount(), connectBefore)
      << "connectionLost 分支未发起重连";

  auto s = c->checkLifeState();
  ASSERT_TRUE(s.has_value()) << (s.is_fail() ? s.get_error()->what() : "");
  EXPECT_EQ(s.value_or(LifeState::GIVEN_UP), LifeState::RUNNING);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T45: 关闭后公共 API 的错误码
// ============================================================
TEST(Client, T45_PostShutdownApiCodes) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  ASSERT_TRUE(c->shutdown().is_success());
  EXPECT_TRUE(c->shutdown().is_success()) << "shutdown 必须幂等";

  std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
  auto br = c->batchRead(v);
  auto cl = c->checkLifeState();
  auto cc = c->checkConnected();
  auto co = c->connect();

  ASSERT_TRUE(br.is_fail());
  ASSERT_TRUE(cl.is_fail());
  ASSERT_TRUE(cc.is_fail());
  ASSERT_TRUE(co.is_fail());

  // F9 修复后：shutdown 后所有公共入口统一返回 ALREADY_TERMINATED，
  // 调用方可以用一个错误码处理“对象已终结”。
  EXPECT_EQ(br.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);
  EXPECT_EQ(cl.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);
  EXPECT_EQ(cc.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);
  EXPECT_EQ(co.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);

  // recreate 也走 m_terminated 分支，返回同一个错误码：语义一致。
  auto rec = c->recreateGiveUpClient();
  ASSERT_TRUE(rec.is_fail());
  EXPECT_EQ(rec.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);
}

