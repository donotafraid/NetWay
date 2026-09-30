// tests/test33.cpp
//
// 补充用例：面向“工业现场多线程 + 真实 open62541 v1.4.14”的三条缺口。
//
//   T110  真实 SDK：服务器断开后再拉起，封装层必须能恢复。
//         这条路径直接对应官方头文件的实现事实：
//           __UA_Client_Service() 发现 !isFullyConnected() → connectSync()
//           → connectInternal()/initConnect() 主动补连（noReconnect 只约束
//           被动状态机，不阻止服务调用前的补连）。
//         现有 32 个文件里 stopServer()/restartServer() 从未被使用，
//         真实 socket 上的“断开-恢复”始终没有用例覆盖。
//
//   T111  真实 SDK：单请求大批量（1024 项）读写，验证 UA_Array_new /
//         UA_ReadRequest_clear / UA_ReadResponse_clear 在规模下不漏、不错位。
//
//   T112  两个真实客户端实例并存：关闭其中一个不得影响另一个。
//         当前所有用例都只在单实例上验证，实例间隔离（尤其共享的日志、
//         SDK 全局状态）没有回归。
//
//   T113  Mock：STRING 变体的 data==nullptr 必须映射到确定的 ErrorCode，
//         而不是落到 RichError 的 UNKNOWN 兜底。此用例在修复前会失败。
//
// 期望运行方式：
//   ctest --test-dir build
//   ./scripts/run_asan.sh      （T110/T111/T112 由 LSan/ASan 裁决）
//
// 注：T110 依赖真实 socket + 看门狗退避，单条最长约 10~30s，已在注释中标注。

#include "tests/include/util/ClientTestFixture.h"

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>
#include <open62541/client.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/types.h>

#include <atomic>
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

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    include <sanitizer/lsan_interface.h>
#    define OPCUA_LSAN_CHECK() __lsan_do_recoverable_leak_check()
#  else
#    define OPCUA_LSAN_CHECK() do {} while (0)
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  include <sanitizer/lsan_interface.h>
#  define OPCUA_LSAN_CHECK() __lsan_do_recoverable_leak_check()
#else
#  define OPCUA_LSAN_CHECK() do {} while (0)
#endif

using namespace std::chrono_literals;

namespace {

OPC_UA_Client::NodeId testIntNode() {
  return OPC_UA_Client::NodeId{1, "TestInt", S7DataType::INT};
}

// T110 用的短超时配置：失败与恢复都要在可控时间内完成。
ClientConfig reconnectTestConfig() {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 2000;
  cfg.sessionTimeoutMs = 600000;
  cfg.secureChannelLifeTimeMs = 900000;
  cfg.maxRetries = 1;
  cfg.retryBackoffBaseMs = 100;
  cfg.retryMaxBackoffMs = 500;
  cfg.watchdogIntervalMs = 1000;
  cfg.connectivityCheckIntervalMs = 1000;
  cfg.giveUpThresholdMs = 20000;
  cfg.maxTotalWaitMs = 60000;
  return cfg;
}

} // namespace

// ============================================================
// T110: 真实 SDK —— 服务器断开后再拉起，看门狗必须恢复连接，
//       且恢复后真实读必须成功。
// ============================================================
TEST_F(ClientTest, T110_RealSdk_RecoversAfterServerRestart) {
  ClientConfig cfg = reconnectTestConfig();
  auto cfgErr = cfg.check();
  ASSERT_FALSE(cfgErr.has_value()) << *cfgErr;

  auto c = connectTo(cfg);
  ASSERT_NE(c, nullptr);

  const OPC_UA_Client::NodeId node = testIntNode();

  {
    auto r = c->batchRead({{node}});
    ASSERT_TRUE(r.is_success()) << r.get_error()->what();
    ASSERT_EQ(r.value_or({}).size(), 1u);
    EXPECT_EQ(r.value_or({})[0].rawStatus, UA_STATUSCODE_GOOD);
  }

  // 服务器下线：后续读必须走失败出口，且不得崩溃/挂死。
  stopServerAndJoin();
  (void)c->batchRead({{node}});
  OPCUA_LSAN_CHECK();

  // 服务器重新上线：看门狗在 giveUpThresholdMs（20s）内应恢复。
  startServerThread();

  const bool recovered = waitFor(
      [&] { return c->isHealthy().is_success(); }, std::chrono::seconds(30),
      std::chrono::milliseconds(100));
  ASSERT_TRUE(recovered)
      << "服务器恢复后看门狗未能在 30s 内重新建立连接；"
         "lifestate="
      << static_cast<int>(c->checkLifeState().value_or(LifeState::GIVEN_UP));

  // 恢复后真实读必须成功（证明 connectSync 补连路径确实生效）。
  auto r2 = c->batchRead({{node}});
  ASSERT_TRUE(r2.is_success()) << r2.get_error()->what();
  const auto &out = r2.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_TRUE(out[0].value.has_value());

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T111: 真实 SDK —— 单请求 1024 项的批量读，规模下不漏、不错位。
// ============================================================
TEST_F(ClientTest, T111_RealSdk_LargeBatchRead_CoversEveryItem) {
  auto c = connectTo();
  ASSERT_NE(c, nullptr);

  constexpr size_t kN = 1024;
  const OPC_UA_Client::NodeId node = testIntNode();
  std::vector<OPC_UA_Client::ReadValue> nodes;
  nodes.reserve(kN);
  for (size_t i = 0; i < kN; ++i) {
    nodes.push_back(OPC_UA_Client::ReadValue{node});
  }

  auto r = c->batchRead(nodes);
  ASSERT_TRUE(r.is_success()) << r.get_error()->what();
  const auto &out = r.value_or({});
  ASSERT_EQ(out.size(), kN);
  for (size_t i = 0; i < kN; ++i) {
    ASSERT_EQ(out[i].rawStatus, UA_STATUSCODE_GOOD) << "item " << i;
    ASSERT_TRUE(out[i].value.has_value()) << "item " << i;
    ASSERT_TRUE(std::holds_alternative<int16_t>(*out[i].value)) << "item " << i;
  }
  OPCUA_LSAN_CHECK();

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T112: 同一服务器上的两个真实客户端实例相互隔离。
// ============================================================
TEST_F(ClientTest, T112_TwoRealClients_ShutdownOfOneDoesNotAffectOther) {
  auto a = connectTo();
  auto b = connectTo();
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);

  const OPC_UA_Client::NodeId node = testIntNode();
  ASSERT_TRUE(a->batchRead({{node}}).is_success());

  EXPECT_TRUE(b->shutdown().is_success());

  for (int i = 0; i < 5; ++i) {
    auto r = a->batchRead({{node}});
    ASSERT_TRUE(r.is_success()) << r.get_error()->what();
    ASSERT_EQ(r.value_or({}).size(), 1u);
    EXPECT_EQ(r.value_or({})[0].rawStatus, UA_STATUSCODE_GOOD);
  }

  EXPECT_TRUE(a->shutdown().is_success());
}

// ============================================================
// T113: 空 / 缺失 STRING（data==nullptr）必须映射到确定的错误码。
//       修复前 readTraits 走 RichError(string) → UNKNOWN；
//       修复后为 TYPE_MISMATCH。
// ============================================================
TEST(ClientStringShape, T113_NullStringVariant_MapsToDeterministicErrorCode) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg,
                                          /*usedefault=*/false,
                                          /*enableWatchDog=*/false);
  ASSERT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  auto c = std::move(*r.get());
  ASSERT_NE(c, nullptr);

  UA_String nullStr = UA_STRING_NULL; // data == nullptr, length == 0
  sdk->setReadItemScalar(0, &nullStr, &UA_TYPES[UA_TYPES_STRING]);
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);

  auto rr = c->batchRead({{1, "A", S7DataType::STRING}});
  ASSERT_TRUE(rr.is_success()) << rr.get_error()->what();
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_FALSE(out[0].value.has_value());
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::TYPE_MISMATCH)
      << "空 STRING 必须映射到确定的 ErrorCode，而不是 UNKNOWN 兜底";

  OPCUA_LSAN_CHECK();
  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}
