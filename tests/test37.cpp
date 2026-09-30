// tests/test37.cpp
//
// 补充用例：只覆盖此前没有任何用例触达的出口。
//
//   T131（红→绿）connect() 的失败出口必须让 LifeState 与 isHealthyCached() 保持一致。
//         公开 connect() 在缓存三元组已经不健康时超时/失败，lifeState 必须离开
//         RUNNING；否则 checkLifeState()==RUNNING 与 isHealthy()==false 同时成立，
//         破坏 Types.h 对 RUNNING 的定义（“三元组全就绪、可正常读写”）。
//         修复前 T131 会红：Impl::connect() 的 CONNECT_FAILED / TIMEOUT_NOTCONNECT
//         出口直接 return，不调用 TurnToRecovring()。
//
//   T132（红→绿）disconnect() 的超时出口同理。当 getState 报告通道处于
//         CONNECTING（既非 OPEN 也非 CLOSED）时，disconnect() 等不到 CLOSED 而超时，
//         此时缓存三元组不健康；修复前 lifeState 仍为 RUNNING，T132 会红。
//
//   T133 已进入 GIVEN_UP 后再次调用 setGiveUpSignal() 必须返回 GIVENUP_ING，
//         而不是把 GIVEN_UP “重新置位”成成功。
//
//   T134 公开工厂 OPC_UA_Client::create() 在 useDefault=false 且配置非法时，
//         必须在任何网络动作之前返回 INVALID_CONFIG。此前公开 create() 只被
//         跳过执行的真实服务器用例（T1）与真实 RealSdk 用例（T47）触及。
//
// 运行：
//   ctest --test-dir build
//   ./scripts/run_asan.sh
//   ./scripts/run_tsan.sh

#include "tests/include/util/ClientTestFixture.h"

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>
#include <open62541/client.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"

namespace {

std::unique_ptr<OPC_UA_Client>
makeMockClient(const std::shared_ptr<MockSdk> &sdk, ClientConfig cfg,
               bool watchdog = false) {
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false, watchdog);
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

// 该配置让 connect()/disconnect() 的虚拟时钟在第一次迭代就跨过 timeoutMs，
// 从而快速、确定性地走到超时出口（不依赖墙钟 sleep）。
ClientConfig fastTimeoutCfg() {
  ClientConfig cfg = localCfg();
  cfg.timeoutMs = 200;
  cfg.retryBackoffBaseMs = 50;
  cfg.retryMaxBackoffMs = 100;
  cfg.maxTotalWaitMs = 5000;
  cfg.giveUpThresholdMs = 60000;
  return cfg;
}

} // namespace

// ============================================================
// T131（红→绿）：connect() 失败出口不得让 RUNNING 与不健康三元组共存
// ============================================================
TEST(ClientBug, T131_ConnectFailure_MustNotLeaveRunningWithUnhealthyTriplet) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg = fastTimeoutCfg();
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto c = makeMockClient(sdk, cfg, /*watchdog=*/false);
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::GIVEN_UP),
            LifeState::RUNNING)
      << "前置条件：刚建连的客户端应为 RUNNING";

  // 链路静默失效：getState 报告 channel CLOSED / session CLOSED 且
  // connectStatus 仍为 GOOD；allowAuto=false 使每次 connectAsync 都返回 GOOD，
  // 但模式停在 Disconnected，于是 connect() 只能超时。
  sdk->setMode(MockSdk::Mode::Disconnected, /*allowAuto=*/false);
  sdk->setAutoAdvancePerIterateMs(1000);

  auto co = c->connect();
  ASSERT_TRUE(co.is_fail()) << "链路始终未恢复，connect() 竟然成功";
  EXPECT_EQ(co.get_error()->code(), RichError::ErrorCode::CONNECT_TIMEOUT)
      << "actual=" << co.get_error()->codeName();

  // 契约：RUNNING ⟺ 三元组全就绪。connect() 失败且缓存三元组已不健康时，
  // lifeState 必须离开 RUNNING。
  EXPECT_TRUE(c->isHealthy().is_fail())
      << "前置条件不成立：三元组本应不健康";
  auto ls = c->checkLifeState();
  ASSERT_TRUE(ls.has_value()) << ls.get_error()->what();
  EXPECT_NE(ls.value_or(LifeState::RUNNING), LifeState::RUNNING)
      << "connect() 超时且 isHealthy() 已失败，checkLifeState() 仍报 RUNNING："
         "connect() 的失败出口没有调用 TurnToRecovring()";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T132（红→绿）：disconnect() 超时出口不得让 RUNNING 与不健康三元组共存
// ============================================================
TEST(ClientBug, T132_DisconnectTimeout_MustNotLeaveRunningWithUnhealthyTriplet) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg = fastTimeoutCfg();
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto c = makeMockClient(sdk, cfg, /*watchdog=*/false);
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::GIVEN_UP),
            LifeState::RUNNING)
      << "前置条件：刚建连的客户端应为 RUNNING";

  // 通道停在 CONNECTING：既非 OPEN（isHealthy() 必失败）也非 CLOSED
  // （disconnect() 等不到结束条件）。两次 getState 各消费一条脚本。
  sdk->setMode(MockSdk::Mode::Connected, /*allowAuto=*/false);
  sdk->setAutoAdvancePerIterateMs(1000);
  sdk->enqueueState(UA_STATUSCODE_GOOD, UA_SECURECHANNELSTATE_CONNECTING,
                    UA_SESSIONSTATE_ACTIVATED);
  sdk->enqueueState(UA_STATUSCODE_GOOD, UA_SECURECHANNELSTATE_CONNECTING,
                    UA_SESSIONSTATE_ACTIVATED);

  auto dr = c->disconnect();
  ASSERT_TRUE(dr.is_fail()) << "通道始终未 CLOSED，disconnect() 竟然成功";
  EXPECT_EQ(dr.get_error()->code(), RichError::ErrorCode::DISCONNECT_TIMEOUT)
      << "actual=" << dr.get_error()->codeName();

  EXPECT_TRUE(c->isHealthy().is_fail())
      << "前置条件不成立：三元组本应不健康";
  auto ls = c->checkLifeState();
  ASSERT_TRUE(ls.has_value()) << ls.get_error()->what();
  EXPECT_NE(ls.value_or(LifeState::RUNNING), LifeState::RUNNING)
      << "disconnect() 超时且 isHealthy() 已失败，checkLifeState() 仍报 RUNNING："
         "disconnect() 的超时出口没有调用 TurnToRecovring()";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T133：重复 setGiveUpSignal() 必须被拒绝为 GIVENUP_ING
// ============================================================
TEST(Client, T133_SetGiveUpSignal_TwiceIsRejectedAsGivenUpIng) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeMockClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  ASSERT_TRUE(c->setGiveUpSignal().is_success());
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING), LifeState::GIVEN_UP);

  auto again = c->setGiveUpSignal();
  ASSERT_TRUE(again.is_fail()) << "重复 setGiveUpSignal() 竟然成功";
  EXPECT_EQ(again.get_error()->code(), RichError::ErrorCode::GIVENUP_ING)
      << "actual=" << again.get_error()->codeName();

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T134：公开 create() 的配置校验必须在触网之前失败
// ============================================================
TEST(Client, T134_PublicCreate_InvalidConfig_RejectedBeforeConnect) {
  // 默认 ClientConfig 的 maxTotalWaitMs == 0，check() 必然失败。
  ClientConfig invalid;
  ASSERT_TRUE(invalid.check().has_value())
      << "前置条件：默认配置应当非法";

  auto r = OPC_UA_Client::create("opc.tcp://127.0.0.1:1", invalid,
                                 /*useDefault=*/false);
  ASSERT_TRUE(r.is_fail()) << "非法配置竟然创建成功";
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::INVALID_CONFIG)
      << "actual=" << r.get_error()->codeName()
      << "；公开 create() 未在连接之前拒绝非法配置";
}
