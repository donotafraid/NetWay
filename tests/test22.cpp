// tests/test22.cpp
//
// 补充用例：把 ua.cpp 中仍无确定性用例的出口钉死。
//   T57 isHealthy() 必须与 checkLifeState() 一致：GIVEN_UP 后不得再报 CONNECTED
//   T58 connectAsync 失败 → SERVICE_FAILED，且 CASGuard 复位后再次 connect 可用
//   T59 disconnectAsync 失败 → SERVICE_FAILED（DISCONNECT_FAIL 出口）
//   T60 disconnect 超时 → CONNECT_TIMEOUT（TIMEOUT_NOTDISCONNECT 出口），CAS 复位
//   T61 createWithSdk(useDefault=true) 用 PROFILE_LOCAL 修复非法配置
//   T62 不支持的 S7DataType：读映射 nullopt；写失败且不下发 SDK
//   T63 预算超限出口携带已分配 results 时不得泄漏（Guard 每轮清理）
//   T64 在途 serviceRead 与 connect 必须被 m_sdkMutex 串行化
//   T65 RealSdk::clientNew 必须保留 UA_Client_delete 需要的 logger clear
//   T66 RECREATING 窗口内所有公共入口统一快速失败
//
// 资源类用例交给 ASan/LSan（T63/T65 已加入 scripts/run_asan.sh、run_lsan.sh）。
// T64/T66 为并发用例，已加入 scripts/run_tsan.sh。
//
// 注意：T57 记录的是一处实现缺陷。它需要配套的最小修复：
//   OPC_UA_Client::isHealthy() 必须先看 getEffectiveStopSignal()/lifeState，
//   再读缓存三元组；否则 setGiveUpSignal() 之后 isHealthy() 仍返回 CONNECTED，
//   与同对象的 checkConnected()/batchRead() 拒绝语义矛盾。

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "tests/include/MockSdk.h"
#include "tests/include/util/Cxx17Compat.h"
#include "tests/include/util/wait_for.h"
#include "tests/include/ClientTestHooks.h"
#include "src/OPCUAPacking/detail/RealSdk.h"

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
makeClient(const std::shared_ptr<MockSdk> &sdk,
           ClientConfig cfg = ClientConfig{},
           bool useDefault = false, bool enableWatchdog = false) {
  if (cfg.maxTotalWaitMs == 0 && !useDefault)
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, useDefault, enableWatchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

void expectRecreating(const RichError *e, const char *what) {
  if (!e) {
    ADD_FAILURE() << what << ": 期望失败但返回了成功";
    return;
  }
  EXPECT_EQ(e->code(), RichError::ErrorCode::RECREATING)
      << what << ": actual=" << e->codeName() << " context=" << e->what();
}

} // namespace

// ============================================================
// T57: GIVEN_UP 之后 isHealthy()/checkConnected() 必须一致
// ============================================================
TEST(Client, T57_IsHealthy_MustNotReportConnectedAfterGiveUp) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  auto h0 = c->isHealthy();
  ASSERT_TRUE(h0.has_value()) << (h0.is_fail() ? h0.get_error()->what() : "");

  // 业务主动放弃：此后 batchRead / batchWrite 都会快速失败。
  c->setGiveUpSignal();
  auto ls = c->checkLifeState();
  ASSERT_TRUE(ls.has_value()) << (ls.is_fail() ? ls.get_error()->what() : "");
  ASSERT_EQ(ls.value_or(LifeState::RUNNING), LifeState::GIVEN_UP);

  // 契约：既然已 GIVEN_UP，健康查询不得再报 CONNECTED。
  auto h1 = c->isHealthy();
  EXPECT_TRUE(h1.is_fail())
      << "GIVEN_UP 后 isHealthy() 仍报 CONNECTED；"
         "缓存三元组必须随 lifeState 一起失效";

  // 同一对象上的 checkConnected() 已是拒绝语义，两者必须一致。
  auto cc = c->checkConnected();
  EXPECT_TRUE(cc.is_fail())
      << "GIVEN_UP 后 checkConnected() 竟然成功，与 isHealthy 不一致";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T58: connectAsync 返回非 GOOD → CONNECT_FAILED → SERVICE_FAILED
//     并且 CASGuard 必须复位 m_connState，第二次 connect 不能是 THREAD_BUSY
// ============================================================
TEST(Client, T58_ConnectAsyncFailure_MapsToServiceFailed_ThenCasResets) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setConnectAsyncResult(UA_STATUSCODE_BADINTERNALERROR);
  auto bad = c->connect();
  ASSERT_TRUE(bad.is_fail());
  EXPECT_EQ(bad.get_error()->code(), RichError::ErrorCode::SERVICE_FAILED)
      << "CONNECT_FAILED 未映射为 SERVICE_FAILED，actual="
      << bad.get_error()->codeName();

  // 失败出口必须把 m_connState 复位为 IDLE。
  sdk->setConnectAsyncResult(UA_STATUSCODE_GOOD);
  auto good = c->connect();
  EXPECT_TRUE(good.is_success())
      << "失败 connect 未复位 CAS，第二次 connect 被误判: "
      << (good.is_fail() ? good.get_error()->what() : "");

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T59: disconnectAsync 返回非 GOOD → DISCONNECT_FAIL → SERVICE_FAILED
// ============================================================
TEST(Client, T59_DisconnectAsyncFailure_MapsToServiceFailed) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setDisconnectAsyncResult(UA_STATUSCODE_BADINTERNALERROR);
  auto bad = c->disconnect();
  ASSERT_TRUE(bad.is_fail());
  EXPECT_EQ(bad.get_error()->code(), RichError::ErrorCode::SERVICE_FAILED)
      << "DISCONNECT_FAIL 未映射为 SERVICE_FAILED，actual="
      << bad.get_error()->codeName();

  // 失败后 CAS 必须复位；恢复 GOOD 后应可正常断开。
  sdk->setDisconnectAsyncResult(UA_STATUSCODE_GOOD);
  auto good = c->disconnect();
  EXPECT_TRUE(good.is_success())
      << "disconnectAsync 恢复后仍无法断开: "
      << (good.is_fail() ? good.get_error()->what() : "");

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T60: disconnect 等待通道 CLOSED 超时 → TIMEOUT_NOTDISCONNECT → CONNECT_TIMEOUT
//     用虚拟时钟  allowAuto=false 让 disconnectAsync 返回 GOOD 但不改 mode。
// ============================================================
TEST(Client, T60_DisconnectTimeout_MapsToConnectTimeout_ThenCasResets) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 100;
  cfg.retryBackoffBaseMs = 10;
  cfg.retryMaxBackoffMs = 20; // < timeoutMs
  cfg.maxTotalWaitMs = 2 * cfg.timeoutMs * (cfg.maxRetries +1);
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto c = makeClient(sdk, cfg);
  ASSERT_NE(c, nullptr);

  sdk->setAutoAdvancePerIterateMs(50);
  // 关键：disconnectAsync 返回 GOOD 但不做隐式状态转换 → 永远等不到 CLOSED。
  sdk->setMode(MockSdk::Mode::Connected, /*allowAuto=*/false);

  auto dr = c->disconnect();
  ASSERT_TRUE(dr.is_fail());
  EXPECT_EQ(dr.get_error()->code(), RichError::ErrorCode::DISCONNECT_TIMEOUT)
      << "TIMEOUT_NOTDISCONNECT 未映射为 DISCONNECT_TIMEOUT，actual="
      << dr.get_error()->codeName();

  // CAS 已复位：恢复可连接后再次 connect 必须成功。
  sdk->setMode(MockSdk::Mode::Connected, /*allowAuto=*/true);
  auto co = c->connect();
  EXPECT_TRUE(co.is_success())
      << "超时 disconnect 未复位 CAS: "
      << (co.is_fail() ? co.get_error()->what() : "");

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T61: useDefault=true 必须用 PROFILE_LOCAL 修复非法配置
// ============================================================
TEST(Client, T61_CreateWithInvalidConfig_useDefaultTrue_RepairsToProfileLocal) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig bad; // maxTotalWaitMs == 0 → check() 失败
  ASSERT_TRUE(bad.check().has_value());

  auto c = makeClient(sdk, bad, /*useDefault=*/true);
  ASSERT_NE(c, nullptr) << "useDefault=true 应修复配置并创建成功";

  UA_Client *raw = sdk->rawClient();
  ASSERT_NE(raw, nullptr);
  const UA_ClientConfig *live = UA_Client_getConfig(raw);
  ASSERT_NE(live, nullptr);

  // applyDefaultIfInvalid() → applyProfile(PROFILE_LOCAL)
  EXPECT_EQ(live->timeout, 5000u);
  EXPECT_EQ(live->connectivityCheckInterval, 30000u);
  EXPECT_EQ(live->secureChannelLifeTime, 600000u);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T62: 不支持的 S7DataType 的处理
// ============================================================
TEST(Client, T62_UnsupportedDataType_ReadNullopt_WriteRejectedWithoutSdkCall) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  // 读：转换器表里没有 UNKNOWN，readConversion::dispatch 返回错误被忽略，
  //     但 value 必须保持 nullopt，不能伪造值。
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  std::vector<OPC_UA_Client::ReadValue> rv{{1, "X", S7DataType::UNKNOWN}};
  auto rr = c->batchRead(rv);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  ASSERT_EQ(rr.value_or({}).size(), 1u);
  EXPECT_FALSE(rr.value_or({})[0].value.has_value())
      << "不支持的 S7DataType 不得产生 value";
  sdk->resetResponseShape();

  // 写：不支持的类型必须失败且不下发 SDK。
  const unsigned writesBefore = sdk->serviceWriteCallCount();
  std::vector<OPC_UA_Client::WriteValue> wv{
      {{1, "X", S7DataType::UNKNOWN}, int16_t(1)}};
  auto wr = c->batchWrite(wv);
  ASSERT_TRUE(wr.is_fail());
  EXPECT_EQ(sdk->serviceWriteCallCount(), writesBefore)
      << "转换失败后不应下发 SDK 调用";
  EXPECT_EQ(wr.get_error()->code(), RichError::ErrorCode::TYPE_NOTSUPPORT)
      << "actual=" << wr.get_error()->codeName();

  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T63: 预算超限出口携带已分配 results 时不得泄漏
//     每轮 serviceRead 都分配 results 并返回可重试错误，直到预算先触发。
// ============================================================
TEST(Client, T63_BudgetExceededAfterAllocatedRetries_NoLeak) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 20;
  cfg.maxRetries = 10;
  cfg.retryBackoffBaseMs = 1;
  cfg.retryMaxBackoffMs = 1; // < timeoutMs
  cfg.maxTotalWaitMs = 2 * cfg.timeoutMs * (cfg.maxRetries + 1); // 440
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  sdk->setAutoAdvancePerIterateMs(50);
  sdk->setReadResponseShape(UA_STATUSCODE_BADCONNECTIONCLOSED,
                            /*resultsSize=*/1, /*allocResults=*/true);

  auto c = makeClient(sdk, cfg);
  ASSERT_NE(c, nullptr);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_fail());
  EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::BUDGET_EXCEEDED)
      << "actual=" << rr.get_error()->codeName()
      << " context=" << rr.get_error()->what();
  EXPECT_LT(sdk->serviceReadCallCount(),
            static_cast<unsigned>(cfg.maxRetries)  + 1u)
      << "预算未先于重试次数触发";

  sdk->resetResponseShape();
  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T64: 在途 serviceRead 与 connect 必须被 m_sdkMutex 串行化
// ============================================================
TEST(Client, T64_InFlightServiceRead_SerializesConcurrentConnect) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, 1, true);

  latch inRead{1};
  latch releaseRead{1};
  sdk->setServiceReadHook([&] {
    inRead.count_down();
    releaseRead.wait(); // 持住 m_sdkMutex，模拟慢服务/内部 connectSync
  });

  std::atomic<bool> readDone{false};
  std::thread reader([&] {
    std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
    (void)c->batchRead(v);
    readDone.store(true);
  });
  ASSERT_TRUE(waitFor([&] { return inRead.try_wait(); }, 2s))
      << "reader 未进入 serviceRead";

  const unsigned connectsBefore = sdk->connectAsyncCallCount();
  std::atomic<bool> connectDone{false};
  std::thread connecter([&] {
    (void)c->connect();
    connectDone.store(true);
  });

  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(connectDone.load())
      << "connect 与在途 serviceRead 未串行化（并发进入 SDK）";
  EXPECT_EQ(sdk->connectAsyncCallCount(), connectsBefore)
      << "connectAsync 与 serviceRead 交叠，违反单实例串行化契约";

  releaseRead.count_down();
  ASSERT_TRUE(waitFor([&] { return connectDone.load(); }, 5s));
  ASSERT_TRUE(waitFor([&] { return readDone.load(); }, 5s));
  reader.join();
  connecter.join();
  EXPECT_GT(sdk->connectAsyncCallCount(), connectsBefore);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T65: RealSdk::clientNew 的 logger 所有权
//   setDefault 分配的 logger 必须保留 clear，否则 UA_Client_delete 泄漏；
//   自定义 log 函数必须被安装。delete 由 ASan/LSan 裁决无泄漏/无二次释放。
// ============================================================
TEST(Client, T65_RealSdk_ClientNew_PreservesLoggerClearForDelete) {
  RealSdk sdk;
  UA_Client *raw = sdk.clientNew(nullptr);
  ASSERT_NE(raw, nullptr);

  UA_ClientConfig *cfg = UA_Client_getConfig(raw);
  ASSERT_NE(cfg, nullptr);
  ASSERT_NE(cfg->logging, nullptr);

  EXPECT_NE(cfg->logging->log, nullptr);
  EXPECT_NE(cfg->logging->clear, nullptr)
      << "logger.clear 丢失：UA_Client_delete 无法释放 setDefault 分配的 logger";
  EXPECT_EQ(cfg->logging->context, nullptr)
      << "自定义 logger 的 context 应为 nullptr";

  sdk.clientDelete(raw);
  OPCUA_LSAN_CHECK();
}

// ============================================================
// T66: RECREATING 窗口内所有公共入口统一快速失败
// ============================================================
TEST(Client, T66_AllPublicEntrypoints_RejectedWhileRecreating) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  c->setGiveUpSignal(); // GIVEN_UP 是 recreate 的前置条件

  latch inRecreate{1};
  latch release{1};
  sdk->setConnectAsyncHook([&] {
    inRecreate.count_down();
    release.wait(); // 卡住 recreate，确定性地持住 m_recreating
  });

  std::atomic<bool> recreateDone{false};
  std::atomic<bool> recreateOk{false};
  std::thread recreator([&] {
    recreateOk.store(c->recreateGiveUpClient().is_success());
    recreateDone.store(true);
  });

  ASSERT_TRUE(waitFor([&] { return inRecreate.try_wait(); }, 2s))
      << "recreate 未进入 connectAsync";

  std::vector<OPC_UA_Client::ReadValue> rv{{1, "TestInt", S7DataType::INT}};
  std::vector<OPC_UA_Client::WriteValue> wv{
      {{1, "TestInt", S7DataType::INT}, int16_t(1)}};

  auto br = c->batchRead(rv);
  expectRecreating(br.is_fail() ? br.get_error() : nullptr, "batchRead");

  auto bw = c->batchWrite(wv);
  expectRecreating(bw.is_fail() ? bw.get_error() : nullptr, "batchWrite");

  auto cc = c->checkConnected();
  expectRecreating(cc.is_fail() ? cc.get_error() : nullptr, "checkConnected");

  auto hi = c->isHealthy();
  expectRecreating(hi.is_fail() ? hi.get_error() : nullptr, "isHealthy");

  auto ls = c->checkLifeState();
  expectRecreating(ls.is_fail() ? ls.get_error() : nullptr, "checkLifeState");

  auto co = c->connect();
  expectRecreating(co.is_fail() ? co.get_error() : nullptr, "connect");

  auto dc = c->disconnect();
  expectRecreating(dc.is_fail() ? dc.get_error() : nullptr, "disconnect");

  auto sd = c->shutdown();
  expectRecreating(sd.is_fail() ? sd.get_error() : nullptr, "shutdown");

  auto rc = c->recreateGiveUpClient();
  expectRecreating(rc.is_fail() ? rc.get_error() : nullptr,
                   "recreateGiveUpClient(第二次)");

  release.count_down();
  ASSERT_TRUE(waitFor([&] { return recreateDone.load(); }, 5s));
  recreator.join();
  EXPECT_TRUE(recreateOk.load()) << "recreate 被拒绝路径打断";

  sdk->setConnectAsyncHook(nullptr);
  EXPECT_TRUE(c->shutdown().is_success());
}