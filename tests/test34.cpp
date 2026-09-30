// tests/test34.cpp
//
// 补充用例：（在 T1~T113 之后）。
//
// 本轮刻意不复述已覆盖的“控制流正确性”，只补四类此前没有任何用例触达的
// 契约/风险；其中 T114、T120 是“修复前必失败、修复后通过”的红→绿回归，
// 对应的生产修复在同一份补丁里（ua.cpp）。
//
//   T114  checkLifeState() 在显式 disconnect() 之后不得仍报 RUNNING。
//         LifeState::RUNNING 的文档语义是“三元组全就绪、可正常读写”，
//         而 Impl::disconnect() 从不写 m_lifeState，导致 isHealthy() 已失败
//         时 checkLifeState() 仍报告 RUNNING（公共契约自相矛盾）。
//
//   T115  NodeId.ns 必须原样下发到 SDK。现有 MockSdk 只记录 NodeId 字符串
//         长度，从不记录 namespaceIndex；若有人把 ns 写死为 0，T1~T113 全部
//         仍然通过。本用例补上这个观察点。
//
//   T116  真实 open62541 服务器上“显式 disconnect → connect → 读”往返。
//         T32 只验证 disconnect 之后 checkConnected() 失败，从未验证还能
//         重新连上；这是计划检修后恢复采集的主路径。
//
//   T117  真实服务器上数值写回读，以及“同一次批量读里混合有效/无效 NodeId”
//         的逐项对齐（T94 只做 STRING，T95 只做单个无效项）。
//
//   T118  四个 NetworkProfile 预设都必须能通过 ClientConfig::check()；
//         以及预算边界 maxTotalWaitMs == 2*timeoutMs*(maxRetries+1) 必须接受，
//         少 1ms 必须拒绝。
//
//   T119  看门狗在“通道 OPEN 但会话未激活”时不做任何恢复动作的特征化用例。
//         当前实现只在 isChannelClosed() 为真时才调用 connect()，因此该状态
//         只会从 RECOVERING 一路走到 GIVEN_UP，connectAsync 一次都不会被调用。
//         用例把这一现状钉死并注明期望的修复方向，防止行为被悄悄改动。
//
//   T120  connect() 的阻塞必须有界，即使 ISdk::runIterate 立即返回且不推进
//         虚拟时钟（MockSdk 默认就是如此）。修复前该轮询会无界忙等，T2 因为
//         显式 setAutoAdvancePerIterateMs(50) 才没有暴露。
//
// 运行：
//   ctest --test-dir build
//   ./scripts/run_asan.sh

#include "tests/include/util/ClientTestFixture.h"

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>
#include <open62541/client.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"
#include "tests/include/util/Cxx17Compat.h"
#include "tests/include/util/wait_for.h"

using namespace std::chrono_literals;

namespace {

std::unique_ptr<OPC_UA_Client>
makeMockClient(const std::shared_ptr<MockSdk> &sdk, ClientConfig cfg,
               bool watchdog = false) {
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg,
                                          /*usedefault=*/false, watchdog);
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

OPC_UA_Client::NodeId testIntNode() {
  return OPC_UA_Client::NodeId{1, "TestInt", S7DataType::INT};
}

} // namespace

// ============================================================
// T114（红→绿）: 显式 disconnect() 之后 checkLifeState() 不得仍报 RUNNING
// ============================================================
TEST(ClientBug, T114_CheckLifeState_AfterDisconnect_MustNotReportRunning) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeMockClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  ASSERT_EQ(c->checkLifeState().value_or(LifeState::GIVEN_UP),
            LifeState::RUNNING)
      << "前置条件：刚建连的客户端应为 RUNNING";

  ASSERT_TRUE(c->disconnect().is_success());

  // 同一对象上 isHealthy() 已明确失败，checkLifeState() 不能自相矛盾。
  EXPECT_TRUE(c->isHealthy().is_fail());
  EXPECT_TRUE(c->checkConnected().is_fail());

  auto ls = c->checkLifeState();
  ASSERT_TRUE(ls.has_value()) << ls.get_error()->what();
  EXPECT_NE(ls.value_or(LifeState::RUNNING), LifeState::RUNNING)
      << "disconnect() 之后 checkLifeState() 仍报 RUNNING；"
         "LifeState::RUNNING 的文档语义是“三元组全就绪、可正常读写”，"
         "此时 isHealthy() 已失败，公共契约自相矛盾";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T115: NodeId.ns 必须原样下发到 SDK（读 + 写）
// ============================================================
TEST(Client, T115_NodeIdNamespace_PassedThroughToSdk) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeMockClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  // --- 读：ns=7 ---
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16(0, 5);
  std::vector<OPC_UA_Client::ReadValue> rv{{7, "ReadNs", S7DataType::INT}};
  auto rr = c->batchRead(rv);
  ASSERT_TRUE(rr.is_success()) << rr.get_error()->what();
  ASSERT_EQ(sdk->lastReadNamespaces().size(), 1u);
  EXPECT_EQ(sdk->lastReadNamespaces()[0], 7u)
      << "封装层没有把 NodeId.ns 下发到 UA_ReadValueId.nodeId.namespaceIndex";

  // --- 写：ns=9 ---
  sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1);
  std::vector<OPC_UA_Client::WriteValue> wv{
      {{9, "WriteNs", S7DataType::INT}, int16_t(1)}};
  auto wr = c->batchWrite(wv);
  ASSERT_TRUE(wr.is_success()) << wr.get_error()->what();
  ASSERT_EQ(sdk->lastWriteNamespaces().size(), 1u);
  EXPECT_EQ(sdk->lastWriteNamespaces()[0], 9u)
      << "封装层没有把 NodeId.ns 下发到 UA_WriteValue.nodeId.namespaceIndex";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T116: 真实 SDK —— 显式 disconnect 后必须能 connect 回来并真实读到
// ============================================================
TEST_F(ClientTest, T116_RealSdk_DisconnectThenConnect_RoundTrip) {
  // watchdogIntervalMs 拉长，避免看门狗在用例的短窗口内抢先重连，
  // 使“谁把连接带回来”这件事保持确定性（仍满足 cfg.check()）。
  ClientConfig cfg = localCfg();
  cfg.watchdogIntervalMs = 30000;
  cfg.giveUpThresholdMs = 120000;
  cfg.connectivityCheckIntervalMs = 30000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto c = connectTo(cfg);
  ASSERT_NE(c, nullptr);

  const OPC_UA_Client::NodeId node = testIntNode();
  {
    auto r = c->batchRead({{node}});
    ASSERT_TRUE(r.is_success()) << r.get_error()->what();
    ASSERT_EQ(r.value_or({}).size(), 1u);
    EXPECT_EQ(r.value_or({})[0].rawStatus, UA_STATUSCODE_GOOD);
  }

  ASSERT_TRUE(c->disconnect().is_success());
  EXPECT_TRUE(c->isHealthy().is_fail());

  // 断开态下读写必须快速失败，且语义是 NOT_CONNECTED。
  auto fast = c->batchRead({{node}});
  ASSERT_TRUE(fast.is_fail());
  EXPECT_EQ(fast.get_error()->code(), RichError::ErrorCode::NOT_CONNECTED)
      << "context=" << fast.get_error()->what();

  // 重新连回来：connect() 成功后读写必须立即恢复可用。
  ASSERT_TRUE(c->connect().is_success());
  auto r2 = c->batchRead({{node}});
  ASSERT_TRUE(r2.is_success()) << r2.get_error()->what();
  const auto &out = r2.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_TRUE(out[0].value.has_value());

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T117: 真实 SDK —— 数值写回读 + 混合有效/无效 NodeId 的逐项对齐
// ============================================================
TEST_F(ClientTest, T117_RealSdk_WriteReadBack_And_MixedBatch) {
  auto c = connectTo();
  ASSERT_NE(c, nullptr);

  const OPC_UA_Client::NodeId node = testIntNode();

  std::vector<OPC_UA_Client::WriteValue> wv{{node, int16_t(1234)}};
  auto wr = c->batchWrite(wv);
  ASSERT_TRUE(wr.is_success()) << wr.get_error()->what();
  ASSERT_EQ(wr.value_or({}).size(), 1u);
  EXPECT_EQ(wr.value_or({})[0].status,
            OPC_UA_Client::WriteResult::Status::Good);

  // 同一次请求里同时包含有效节点与不存在的节点：逐项结果必须按索引对齐。
  std::vector<OPC_UA_Client::ReadValue> rv{
      {node}, {1, "NoSuchNode_Review_XYZ", S7DataType::INT}};
  auto rr = c->batchRead(rv);
  ASSERT_TRUE(rr.is_success()) << rr.get_error()->what();

  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  ASSERT_TRUE(out[0].value.has_value());
  ASSERT_TRUE(std::holds_alternative<int16_t>(*out[0].value));
  EXPECT_EQ(std::get<int16_t>(*out[0].value), 1234)
      << "数值写回读的值不一致（类型转换或索引错位）";

  EXPECT_NE(out[1].rawStatus, UA_STATUSCODE_GOOD)
      << "不存在节点的按项状态必须保留为 Bad";
  EXPECT_FALSE(out[1].value.has_value());

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T118: 四个网络预设都必须自洽；预算边界必须精确
// ============================================================
TEST(ClientConfigTest, T118_AllProfilesValid_And_BudgetBoundaryExact) {
  const ClientConfig::NetworkProfile profiles[] = {
      ClientConfig::PROFILE_LOCAL, ClientConfig::PROFILE_REMOTE,
      ClientConfig::PROFILE_MOBILE, ClientConfig::PROFILE_UNSTABLE};
  for (auto p : profiles) {
    ClientConfig c;
    c.applyProfile(p);
    auto err = c.check();
    EXPECT_FALSE(err.has_value())
        << "profile=" << static_cast<int>(p) << " 的预设竟然不通过 check(): "
        << (err.has_value() ? *err : "");
  }

  // PROFILE_LOCAL: timeoutMs=5000, maxRetries=2 →
  // requiredBudget = 2*5000*3 = 30000 == maxTotalWaitMs，必须接受。
  ClientConfig exact;
  exact.applyProfile(ClientConfig::PROFILE_LOCAL);
  EXPECT_FALSE(exact.check().has_value())
      << "maxTotalWaitMs 恰等于 2*timeoutMs*(maxRetries+1) 时应为合法";

  // 少 1ms 必须被预算分支拒绝（其余约束仍满足）。
  ClientConfig one_less = exact;
  one_less.maxTotalWaitMs = 29999;
  auto err = one_less.check();
  ASSERT_TRUE(err.has_value()) << "少 1ms 的预算竟被接受";
  EXPECT_NE(err->find("2*timeoutMs*(maxRetries+1)"), std::string::npos)
      << "错误信息没有指向预算下界: " << *err;
}

// ============================================================
// T119（特征化）: 看门狗修复“通道 OPEN + 会话未激活”
// 期望：通道 OPEN 但会话长期未激活时，先 disconnect() 再 connect()，
// 让通道回到 CLOSED 以便 initConnect() 真正重建会话。
// ============================================================
TEST(Client, T119_Watchdog_Repairs_ChannelOpenSessionClosed) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg = localCfg();
  cfg.giveUpThresholdMs = 60000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto c = makeMockClient(sdk, cfg, /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  // 进入故障：通道 OPEN、会话 CLOSED；虚拟时钟每次驱动 +1000ms。
  sdk->setMode(MockSdk::Mode::ChannelOpenSessionClosed);
  sdk->setAutoAdvancePerIterateMs(1000);

  ASSERT_EQ(c->checkLifeState().value_or(LifeState::GIVEN_UP), LifeState::RUNNING);

  bool reachedGivenUp = false;
  for (int i = 0; i < 200 && !reachedGivenUp; ++i) {
    OPCUAPacking::test::ClientTestHooks::pumpWatchdogForTest(*c);
    reachedGivenUp = (c->checkLifeState().value_or(LifeState::RUNNING) ==
                      LifeState::GIVEN_UP);
  }

  // 修复后：看门狗应自愈，不应进入 GIVEN_UP
  ASSERT_FALSE(reachedGivenUp);
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::GIVEN_UP),
            LifeState::RUNNING);
  auto result = c->checkConnected();
  ASSERT_TRUE(result.has_value());
}

// ============================================================
// T120（红→绿）: connect() 在虚拟时钟冻结时也必须有限返回
//
// MockSdk 默认不推进 nowMs（autoAdvance=0）。修复前 Impl::connect() 只比较
// ISdk::nowMs() 的 elapsed，于是 runIterate 立即返回时形成无界忙等；
// T2 因为显式 setAutoAdvancePerIterateMs(50) 而没有暴露。
// ============================================================
TEST(ClientBug, T120_Connect_BoundedWhenSdkClockFrozen) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Unreachable, /*allowAuto=*/false);
  sdk->setAutoAdvancePerIterateMs(0); // 时钟冻结

  ClientConfig cfg = localCfg();
  cfg.timeoutMs = 200;
  cfg.retryBackoffBaseMs = 50;
  cfg.retryMaxBackoffMs = 100;
  cfg.maxTotalWaitMs = 5000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  const auto t0 = std::chrono::steady_clock::now();
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg,
                                          /*usedefault=*/false,
                                          /*enableWatchDog=*/false);
  const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();

  ASSERT_TRUE(r.is_fail()) << "不可达服务器竟然建连成功";
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::CONNECT_TIMEOUT)
      << "actual=" << r.get_error()->codeName();
  // 墙钟上界：2*timeoutMs + 500ms + 余量，绝不允许无界忙等。
  EXPECT_LT(wallMs, 3000)
      << "connect() 在 SDK 时钟冻结时超过 3s 仍未返回，疑为无界忙等";
}
