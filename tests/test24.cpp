// tests/test24.cpp
//
// 补充 ua.cpp 中尚未覆盖的出口与“公开 connect 成功后的 lifeState”
// 契约钉死。T73/T80 为缺陷复现（修复前应失败），其余修复前应通过。
//
//   T73  ClientBug    公开 connect() 成功必须把 lifeState 复位为 RUNNING
//   T74  Client       connect 超时出口必须调用 UA_Client_disconnect 收尾
//   T75  Client       STRING 读取对含内嵌 NUL 的值必须长度保真
//   T76  Client       checkConnected 必须主动驱动一次 event loop
//   T77  Client       并发 shutdown：第二个调用者必须立即返回 success
//   T78  Client       recreate 成功必须 delete 旧 UA_Client 并换上新对象
//   T79  ClientConfig  check() 预算溢出分支必须被报告
//   T80  ClientBug    batchRead 内嵌 NUL 的错误文案不得写 writeTraits

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>
#include <open62541/client.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "tests/include/MockSdk.h"
#include "tests/include/util/Cxx17Compat.h"
#include "tests/include/util/wait_for.h"
#include "tests/include/ClientTestHooks.h"

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
std::unique_ptr<OPC_UA_Client>
makeClient(const std::shared_ptr<MockSdk> &sdk, ClientConfig cfg,
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
} // namespace

// ============================================================
// T73: 公开 connect() 成功后的 LifeState 契约
//
// LifeState::RUNNING 的定义是“三元组全部就绪”。既然公开 connect() 已返回成功、
// 且 getState 报告三元组健康，lifeState 就必须回到 RUNNING，读写必须立刻可用，
// 而不是要等下一次看门狗 tick（默认 5-10s）。
//
// 现状：Impl::connect() 成功后不调用 TurnToRunning()，只有看门狗会置 RUNNING，
// ============================================================
TEST(ClientBug, T73_PublicConnectSuccess_MustRestoreRunning) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  // 1) 确定性地进入 RECOVERING：健康为假 + 下一次 connect 失败。
  sdk->setMode(MockSdk::Mode::Unreachable, /*allowAuto=*/false);
  sdk->setConnectAsyncResult(UA_STATUSCODE_BADINTERNALERROR);
  OPCUAPacking::test::ClientTestHooks::pumpWatchdogForTest(*c);
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::RECOVERING)
      << "前置条件不成立：未进入 RECOVERING";

  // 2) 公开 connect() 成功，且三元组恢复健康。
  sdk->setConnectAsyncResult(UA_STATUSCODE_GOOD);
  sdk->setMode(MockSdk::Mode::Connected, /*allowAuto=*/true);
  auto co = c->connect();
  ASSERT_TRUE(co.is_success())
      << "connect 失败: " << (co.is_fail() ? co.get_error()->what() : "");

  // 3) 契约：连接健康 => lifeState 必须 RUNNING。
  auto ls = c->checkLifeState();
  ASSERT_TRUE(ls.has_value()) << ls.get_error()->what();
  EXPECT_EQ(ls.value_or(LifeState::RUNNING), LifeState::RUNNING)
      << "公开 connect() 已成功但 lifeState 仍是 RECOVERING；"
         "在下一次看门狗 tick 之前读写都会被拒";

  // 4) 同一契约的可观察后果：读写立即可用。
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
  EXPECT_TRUE(c->batchRead(v).is_success())
      << "连接已健康，却因 lifeState==RECOVERING 拒绝读";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T74: connect 超时出口的收尾动作
// ua.cpp:1100-1104 在 TIMEOUT_NOTCONNECT 分支调用 m_sdk->disconnect(client)。
// 该出口此前无测试；用 MockSdk 的 syncDisconnect 计数器钉死。
// ============================================================
TEST(Client, T74_ConnectTimeout_CallsDisconnectCleanup) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg = localCfg();
  cfg.timeoutMs = 200;
  cfg.retryBackoffBaseMs = 10;
  cfg.retryMaxBackoffMs = 50;
  cfg.maxTotalWaitMs = 5000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto c = makeClient(sdk, cfg, /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  // 保持通道关闭，并让虚拟时钟推进到超时。
  sdk->setMode(MockSdk::Mode::Unreachable, /*allowAuto=*/false);
  sdk->setAutoAdvancePerIterateMs(50);

  const unsigned before = sdk->disconnectAsyncCallCount();
  auto co = c->connect();
  ASSERT_TRUE(co.is_fail());
  EXPECT_EQ(co.get_error()->code(), RichError::ErrorCode::CONNECT_TIMEOUT)
      << "actual=" << co.get_error()->codeName();
  EXPECT_EQ(sdk->disconnectAsyncCallCount(), before + 1)
      << "connect 超时后未调用 UA_Client_Asyncdisconnect 收尾，半开通道残留";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T75: STRING 读取的长度保真（含内嵌 NUL）
// UATypeTraits<std::string>::convert 用 UA_String.length 构造 std::string
// （TypeTraits.h:52-57）。若有人改成 c_str()/strlen，本用例会失败。
// ============================================================
TEST(Client, T75_BatchRead_StringWithEmbeddedNul_PreservesLength) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  const std::string payload("A\0B\0C", 5);
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, 1, true);
  sdk->setReadItemString(0, payload);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "S", S7DataType::STRING}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_success()) << rr.get_error()->what();

  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  ASSERT_TRUE(out[0].value.has_value());
  ASSERT_TRUE(std::holds_alternative<std::string>(*out[0].value));
  const std::string got = std::get<std::string>(*out[0].value);
  EXPECT_EQ(got.size(), payload.size())
      << "STRING 读取被 strlen 截断（未使用 UA_String.length）";
  EXPECT_EQ(got, payload);

  sdk->resetResponseShape();
  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T76: checkConnected 必须主动驱动一次事件循环
// ua.h:61 明确写了“checkConnected 会主动驱动一次事件循环以刷新状态”。
// ============================================================
TEST(Client, T76_CheckConnected_DrivesEventLoopAtLeastOnce) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  const unsigned before = sdk->runIterateCallCount();
  (void)c->checkConnected();
  EXPECT_GT(sdk->runIterateCallCount(), before)
      << "checkConnected() 没有驱动 run_iterate，缓存三元组无法刷新";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T77: 并发 shutdown 的“第二个调用者立即返回”契约
// ua.h:90 写明：两个线程同时调用，仅一个执行关闭，另一个立即返回 success
// （不等待关闭完成）。T-C 只验证两者都 success，没有验证时序。
// ============================================================
TEST(Client, T77_Shutdown_SecondCaller_ReturnsBeforeDrainCompletes) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  latch inRead{1}, releaseRead{1};
  sdk->setServiceReadHook([&] {
    inRead.count_down();
    releaseRead.wait(); // 持住 m_taskCounter，让 shutdown 排空阻塞
  });

  std::atomic<bool> readDone{false};
  std::thread reader([&] {
    std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
    (void)c->batchRead(v);
    readDone.store(true, std::memory_order_release);
  });
  ASSERT_TRUE(waitFor([&] { return inRead.try_wait(); }, 2s))
      << "reader 未进入 serviceRead";

  std::atomic<bool> firstDone{false};
  std::thread first([&] {
    (void)c->shutdown();
    firstDone.store(true, std::memory_order_release);
  });

  // 给第一个 shutdown 足够时间置位 m_terminated 并阻塞在排空上。
  std::this_thread::sleep_for(100ms);
  ASSERT_FALSE(firstDone.load())
      << "第一个 shutdown 不应在在途读未释放时返回";

  const auto t0 = std::chrono::steady_clock::now();
  auto second = c->shutdown();
  const auto dt = std::chrono::steady_clock::now() - t0;

  EXPECT_TRUE(second.is_success()) << "幂等 shutdown 应返回 success";
  EXPECT_LT(dt, 1s) << "第二个 shutdown 等待了排空，违反‘立即返回’契约";
  EXPECT_FALSE(readDone.load())
      << "第二个 shutdown 返回时在途读却已结束，未测到并发场景";

  releaseRead.count_down();
  ASSERT_TRUE(waitFor([&] { return readDone.load(); }, 5s));
  reader.join();
  first.join();
}

// ============================================================
// T78: recreate 成功必须释放旧 UA_Client 并安装新对象
// T53/T18 只验证配置沿用与新对象可用，没有证明旧对象真的被 delete。
// ============================================================
TEST(Client, T78_RecreateGiveUpClient_DeletesOldSdkClientAndInstallsNew) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  UA_Client *old = sdk->rawClient();
  ASSERT_NE(old, nullptr);
  const unsigned deletedBefore = sdk->clientDeleteCallCount();

  c->setGiveUpSignal();
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::GIVEN_UP);

  auto rr = c->recreateGiveUpClient();
  ASSERT_TRUE(rr.is_success())
      << (rr.is_fail() ? rr.get_error()->what() : "");

  UA_Client *now = sdk->rawClient();
  ASSERT_NE(now, nullptr);
  EXPECT_NE(now, old) << "recreate 未安装新的 UA_Client";
  EXPECT_GT(sdk->clientDeleteCallCount(), deletedBefore)
      << "recreate 成功后旧 UA_Client 未被 delete（deleter 未执行）";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T79: ClientConfig::check() 预算溢出分支
// Types.h:231-241 的 requiredBudget > uint32_t::max() 分支，T24 未覆盖。
// ============================================================
TEST(ClientConfigTest, Check_OverflowBudget_Reported) {
  ClientConfig c;
  c.applyProfile(ClientConfig::PROFILE_LOCAL);
  c.timeoutMs = 400000000;             // < sessionTimeoutMs
  c.sessionTimeoutMs = 500000000;
  c.secureChannelLifeTimeMs = 500000000;
  c.maxRetries = 10;
  c.retryBackoffBaseMs = 1000;
  c.retryMaxBackoffMs = 2000;          // < timeoutMs
  c.watchdogIntervalMs = 1000;         // [1000, 30000]
  c.connectivityCheckIntervalMs = 1000;
  c.giveUpThresholdMs = 60000;         // >= 2*watchdog, < sessionTimeout
  c.maxTotalWaitMs = 450000000;        // > timeoutMs, < sessionTimeoutMs

  // requiredBudget = 2 * 4e8 * 11 = 8.8e9 > UINT32_MAX(4.29e9)
  const auto err = c.check();
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->find("overflow"), std::string::npos)
      << "预算溢出未被报告，err=" << *err;
}

// ============================================================
// T80: batchRead 的内嵌 NUL 错误文案
// ============================================================
TEST(ClientBug, T80_BatchRead_EmbeddedNul_ErrorTextShouldNotSayWriteTraits) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, localCfg(), /*watchdog=*/false);
  ASSERT_NE(c, nullptr);

  const std::string id("A\0B", 3);
  std::vector<OPC_UA_Client::ReadValue> v{{1, id, S7DataType::INT}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_fail());
  EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);
  EXPECT_EQ(std::string(rr.get_error()->what()).find("writeTraits"),
            std::string::npos)
      << "batchRead 复用了写路径前缀: " << rr.get_error()->what();

  EXPECT_TRUE(c->shutdown().is_success());
}
