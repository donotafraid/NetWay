// tests/test21.cpp
//
// 第四轮补充：把 ua.cpp 里“只写在注释/文档里的契约”变成可证伪用例。
//   T48 batchWrite 对可重试 serviceResult 不做内部重试（写非幂等保护）
//   T49 batchWrite 值类型不匹配 → 失败且不下发 SDK，请求内存被释放
//   T50 batchRead 重试码集合表驱动（9 个可重试  1 个不可重试）
//   T51 重试退避期间 shutdown → 立刻唤醒，不被完整 backoff 阻塞
//   T52 ClientConfig 是值快照：调用方改配置不影响已建客户端
//   T53 recreate 使用“生效配置”（applyDefaultIfInvalid 之后），
//       而不是 RecreateSync 里保留的原始非法配置
//   T54 公共 isHealthy() 跟随 stateCallback 三元组
//   T55 领域值类型满足 Rule of Five（可拷贝/可移动）
//   T56 公共 API 签名锁（扩展 test20，覆盖 connect/disconnect/isHealthy 等）
//
// 资源类用例交给 ASan/LSan：已加入 scripts/run_asan.sh、run_lsan.sh 过滤。
// 并发用例 T51 已加入 scripts/run_tsan.sh 过滤。

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "tests/MockSdk.h"
#include "tests/util/Cxx17Compat.h"
#include "tests/util/wait_for.h"
#include "OPCUAPacking/internal/ClientTestHooks.h"

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

#include <open62541/client.h>

using namespace std::chrono_literals;

namespace {

std::unique_ptr<OPC_UA_Client>
makeClient(const std::shared_ptr<MockSdk> &sdk, bool enableWatchdog = false) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      enableWatchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

} // namespace

// ============================================================
// T48: 写操作非幂等 —— 可重试 serviceResult 也只能下发一次
// ============================================================
TEST(Client, T48_BatchWrite_NoAutoRetryOnRetryableServiceResult) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr) << "create failed";

  // BADCONNECTIONCLOSED 在 batchRead 里属于“可重试”。写路径必须忽略重试，
  // 否则网络瞬断后会在上层无感知的情况下重复执行非幂等写入。
  sdk->setWriteResponseShape(UA_STATUSCODE_BADCONNECTIONCLOSED,
                             /*resultsSize=*/1);

  OPC_UA_Client::WriteValue w{{1, "TestInt", S7DataType::INT}, int16_t(1)};
  std::vector<OPC_UA_Client::WriteValue> v{w};

  auto r = c->batchWrite(v);
  ASSERT_TRUE(r.is_fail());
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::SERVICE_FAILED)
      << "context=" << r.get_error()->what();
  EXPECT_EQ(sdk->serviceWriteCallCount(), 1u)
      << "batchWrite 对可重试错误进行了自动重试，违反“写非幂等”契约";

  sdk->resetResponseShape();
  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T49: 写值类型与声明的 S7DataType 不匹配 → 失败且不下发 SDK
// ============================================================
TEST(Client, T49_BatchWrite_TypeMismatch_FailsWithoutSdkCall) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr) << "create failed";

  // dataType=INT，但 variant 里是 std::string → writeConversion 的 get_if
  // 失败。WriteRequestGuard 必须把已初始化的请求项清理干净（LSan 裁决）。
  OPC_UA_Client::WriteValue w{{1, "TestInt", S7DataType::INT},
                              std::string("not-an-int")};
  std::vector<OPC_UA_Client::WriteValue> v{w};

  auto r = c->batchWrite(v);
  ASSERT_TRUE(r.is_fail()) << "类型不匹配竟然写成功";
  EXPECT_EQ(sdk->serviceWriteCallCount(), 0u)
      << "转换失败后不应下发 SDK 调用";

  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T50: batchRead 重试码集合表驱动
//
// ua.cpp 里用一长串 == 判断列出“可重试”的 StatusCode。该集合必须与
// open62541 的“断链类”错误一一对应：漏一个会把可恢复错误直接判死，
// 多一个会把不可恢复错误反复重试。用表把当前集合钉死。
// ============================================================
TEST(Client, T50_BatchRead_RetryableStatusCodeSet) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.maxRetries = 0;                      // attempts = 1
  cfg.maxTotalWaitMs = 2 * cfg.timeoutMs;  // 满足 2*timeout*(maxRetries1)
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r0 = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg,
                                         /*usedefault=*/false,
                                         /*enableWatchDog=*/false);
  ASSERT_TRUE(r0.has_value()) << r0.get_error()->what();
  auto c = std::move(*r0.get());

  const uint32_t retryable[] = {
      UA_STATUSCODE_BADCONNECTIONCLOSED,
      UA_STATUSCODE_BADCOMMUNICATIONERROR,
      UA_STATUSCODE_BADSESSIONIDINVALID,
      UA_STATUSCODE_BADSECURECHANNELIDINVALID,
      UA_STATUSCODE_BADNOTCONNECTED,
      UA_STATUSCODE_BADSECURECHANNELCLOSED,
      UA_STATUSCODE_BADSERVERNOTCONNECTED,
      UA_STATUSCODE_BADTIMEOUT,
      UA_STATUSCODE_BADSESSIONCLOSED};

  std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};

  for (uint32_t code : retryable) {
    const unsigned before = sdk->serviceReadCallCount();
    sdk->setReadResponseShape(code, /*resultsSize=*/1, /*allocResults=*/true);
    auto rr = c->batchRead(v);
    sdk->resetResponseShape();

    ASSERT_TRUE(rr.is_fail()) << "code=" << UA_StatusCode_name(code);
    EXPECT_EQ(rr.get_error()->code(),
              RichError::ErrorCode::SERVICE_RETRY_EXHAUSTED)
        << "code=" << UA_StatusCode_name(code)
        << " context=" << rr.get_error()->what();
    EXPECT_EQ(sdk->serviceReadCallCount() - before, 1u)
        << "code=" << UA_StatusCode_name(code)
        << "（maxRetries=0 时只允许一次下发）";
  }

  // 不可重试错误必须走 else 分支（SERVICE_FAILED），不得消耗重试预算。
  {
    const unsigned before = sdk->serviceReadCallCount();
    sdk->setReadResponseShape(UA_STATUSCODE_BADINTERNALERROR, 1, true);
    auto rr = c->batchRead(v);
    sdk->resetResponseShape();

    ASSERT_TRUE(rr.is_fail());
    EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::SERVICE_FAILED)
        << "context=" << rr.get_error()->what();
    EXPECT_EQ(sdk->serviceReadCallCount() - before, 1u);
  }

  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T51: 重试退避期间 shutdown → delayFunction 必须被立即唤醒
//
// ua.cpp 的 delayFunction 以 100ms 分段等待 m_retryCv，并在每段检查
// stop 信号。若漏掉 notify（或 wait 谓词不查 stop），shutdown 会被完整
// 的指数退避拖住，违反“shutdown 有界”契约。
// ============================================================
TEST(Client, T51_ShutdownDuringRetryBackoff_WakesPromptly) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.maxRetries = 2;
  cfg.retryBackoffBaseMs = 4000;
  cfg.retryMaxBackoffMs = 4000;   // 故意让第一次退避就是 4s
  cfg.maxTotalWaitMs = 30000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r0 = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg,
                                         /*usedefault=*/false,
                                         /*enableWatchDog=*/false);
  ASSERT_TRUE(r0.has_value()) << r0.get_error()->what();
  auto c = std::move(*r0.get());

  latch entered{1};
  sdk->setServiceReadHook([&] { entered.count_down(); });
  // 可重试错误：首次返回后进入 4s 退避。
  sdk->setReadResponseShape(UA_STATUSCODE_BADCONNECTIONCLOSED, 0, false);

  std::atomic<bool> readDone{false};
  std::thread reader([&] {
    std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
    (void)c->batchRead(v);
    readDone.store(true);
  });

  ASSERT_TRUE(entered.try_wait() ||
              waitFor([&] { return entered.try_wait(); }, 2s))
      << "reader 未进入 serviceRead";
  std::this_thread::sleep_for(100ms); // 让 reader 进入 delayFunction

  const auto t0 = std::chrono::steady_clock::now();
  ASSERT_TRUE(c->shutdown().is_success());
  const auto dt = std::chrono::steady_clock::now() - t0;

  EXPECT_LT(dt, 2500ms)
      << "shutdown 未在退避期间唤醒 delayFunction，被 4s backoff 阻塞";

  ASSERT_TRUE(waitFor([&] { return readDone.load(); }, 2s));
  reader.join();
}

// ============================================================
// T52: ClientConfig 是值快照，且 noReconnect/noNewSession 被强制为 true
// ============================================================
TEST(Client, T52_ConfigValueSnapshot_ImmuneToCallerMutation) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 4321;
  cfg.secureChannelLifeTimeMs = 654321;
  cfg.connectivityCheckIntervalMs = 7654;
  cfg.watchdogIntervalMs = 3000;
  cfg.giveUpThresholdMs = 120000;
  cfg.maxTotalWaitMs = 2 * cfg.timeoutMs * (cfg.maxRetries + 1);
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg,
                                        /*usedefault=*/false,
                                        /*enableWatchDog=*/false);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  UA_Client *raw = sdk->rawClient();
  ASSERT_NE(raw, nullptr);
  const UA_ClientConfig *live = UA_Client_getConfig(raw);
  ASSERT_NE(live, nullptr);

  EXPECT_EQ(live->timeout, 4321u);
  EXPECT_EQ(live->secureChannelLifeTime, 654321u);
  EXPECT_EQ(live->connectivityCheckInterval, 7654u);
  EXPECT_TRUE(live->noReconnect)
      << "封装必须禁止 SDK 后台自动重连（重连权威在看门狗）";
  EXPECT_TRUE(live->noNewSession)
      << "封装必须禁止 SDK 自动新建会话（会话权威在看门狗）";

  // 篡改调用方副本：封装层必须是值快照，不能读到调用方后续修改。
  cfg.timeoutMs = 1;
  cfg.secureChannelLifeTimeMs = 1;
  cfg.connectivityCheckIntervalMs = 1;

  EXPECT_EQ(UA_Client_getConfig(sdk->rawClient())->timeout, 4321u)
      << "调用方修改 ClientConfig 泄漏进了已建客户端";
  EXPECT_EQ(UA_Client_getConfig(sdk->rawClient())->secureChannelLifeTime,
            654321u);
  EXPECT_EQ(UA_Client_getConfig(sdk->rawClient())->connectivityCheckInterval,
            7654u);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T53: recreate 必须沿用“生效配置快照”，而不是回退到默认配置
//
// 破坏场景：recreate 若用 ClientConfig{} 或 SDK 默认值重建，新客户端会带着
// 另一套 timeout / secureChannelLifeTime 复活，业务侧超时语义静默改变。
// ============================================================
TEST(Client, T53_RecreateCarriesEffectiveConfigSnapshot) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 4321;                 // 非默认值，便于区分
  cfg.secureChannelLifeTimeMs = 654321;
  cfg.connectivityCheckIntervalMs = 7654;
  cfg.watchdogIntervalMs = 3000;
  cfg.giveUpThresholdMs = 120000;
  cfg.maxTotalWaitMs = 2 * cfg.timeoutMs * (cfg.maxRetries + 1);
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg,
                                        /*usedefault=*/false,
                                        /*enableWatchDog=*/false);
  ASSERT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  auto c = std::move(*r.get());

  ASSERT_NE(sdk->rawClient(), nullptr);
  ASSERT_EQ(UA_Client_getConfig(sdk->rawClient())->timeout, 4321u);

  // 篡改调用方副本：recreate 不得“顺手”读回调用方的原始配置。
  cfg.timeoutMs = 7;
  cfg.secureChannelLifeTimeMs = 7;

  c->setGiveUpSignal();
  auto rr = c->recreateGiveUpClient();
  ASSERT_TRUE(rr.is_success())
      << (rr.is_fail() ? rr.get_error()->what() : "");

  EXPECT_EQ(UA_Client_getConfig(sdk->rawClient())->timeout, 4321u)
      << "recreate 未沿用生效配置快照（疑似回退到默认/原始配置）";
  EXPECT_EQ(UA_Client_getConfig(sdk->rawClient())->secureChannelLifeTime,
            654321u);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T54: 公共 isHealthy() 必须跟随 stateCallback 推送的三元组
// ============================================================
TEST(Client, T54_PublicIsHealthy_TracksCallbackTriplet) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr) << "create failed";

  auto h0 = c->isHealthy();
  ASSERT_TRUE(h0.has_value()) << (h0.is_fail() ? h0.get_error()->what() : "");
  EXPECT_EQ(h0.value_or(ConnectionState::OBJECT_ONLY),
            ConnectionState::CONNECTED);

  // 通道 OPEN、会话 CLOSED → 三元组不全 → 公共 API 报 NOT_CONNECTED。
  sdk->fireStateCallback(UA_STATUSCODE_GOOD, UA_SECURECHANNELSTATE_OPEN,
                         UA_SESSIONSTATE_CLOSED);
  auto h1 = c->isHealthy();
  ASSERT_TRUE(h1.is_fail()) << "会话未激活时 isHealthy() 仍报健康";
  EXPECT_EQ(h1.get_error()->code(), RichError::ErrorCode::NOT_CONNECTED);

  // 恢复会话 → 重新健康。
  sdk->fireStateCallback(UA_STATUSCODE_GOOD, UA_SECURECHANNELSTATE_OPEN,
                         UA_SESSIONSTATE_ACTIVATED);
  auto h2 = c->isHealthy();
  ASSERT_TRUE(h2.has_value()) << (h2.is_fail() ? h2.get_error()->what() : "");
  EXPECT_EQ(h2.value_or(ConnectionState::OBJECT_ONLY),
            ConnectionState::CONNECTED);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T55: 领域值类型的 Rule of Five（可拷贝 / 可移动）
// ============================================================
TEST(Client, T55_DomainValueTypes_RuleOfFive) {
  using Node = OPC_UA_Client::NodeId;
  using RV = OPC_UA_Client::ReadValue;
  using WV = OPC_UA_Client::WriteValue;
  using RR = OPC_UA_Client::ReadResult;
  using WR = OPC_UA_Client::WriteResult;

  static_assert(std::is_copy_constructible_v<Node>);
  static_assert(std::is_move_constructible_v<Node>);
  static_assert(std::is_copy_constructible_v<RV>);
  static_assert(std::is_move_constructible_v<RV>);
  static_assert(std::is_copy_constructible_v<WV>);
  static_assert(std::is_move_constructible_v<WV>);
  static_assert(std::is_copy_constructible_v<RR>);
  static_assert(std::is_move_constructible_v<RR>);
  static_assert(std::is_copy_assignable_v<RR>);
  static_assert(std::is_move_assignable_v<RR>);
  static_assert(std::is_copy_constructible_v<WR>);
  static_assert(std::is_move_constructible_v<WR>);
  static_assert(std::is_copy_constructible_v<ValueType>);
  static_assert(std::is_move_constructible_v<ValueType>);

  OPC_UA_Client::ReadResult a{};
  a.rawStatus = 1;
  OPC_UA_Client::ReadResult b = a;               // copy
  OPC_UA_Client::ReadResult d = std::move(b);    // move
  std::vector<OPC_UA_Client::ReadResult> out{a, d};
  EXPECT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].rawStatus, 1u);
}

// ============================================================
// T56: 公共 API 签名锁（扩展 test20）
// 若有人把 UA_* 或内部错误枚举塞回公共签名，这里编译失败。
// ============================================================
static_assert(
    std::is_same_v<decltype(&OPC_UA_Client::connect),
                   Result<Unit, RichError> (OPC_UA_Client::*)()>,
    "connect 公共签名应为 Result<Unit, RichError>()");

static_assert(
    std::is_same_v<decltype(&OPC_UA_Client::disconnect),
                   Result<Unit, RichError> (OPC_UA_Client::*)()>,
    "disconnect 公共签名应为 Result<Unit, RichError>()");

static_assert(
    std::is_same_v<decltype(&OPC_UA_Client::shutdown),
                   Result<Unit, RichError> (OPC_UA_Client::*)()>,
    "shutdown 公共签名应为 Result<Unit, RichError>()");

static_assert(
    std::is_same_v<decltype(&OPC_UA_Client::recreateGiveUpClient),
                   Result<Unit, RichError> (OPC_UA_Client::*)()>,
    "recreateGiveUpClient 公共签名应为 Result<Unit, RichError>()");

static_assert(
    std::is_same_v<decltype(&OPC_UA_Client::checkConnected),
                   Result<ConnectionState, RichError> (OPC_UA_Client::*)()>,
    "checkConnected 公共签名应为 Result<ConnectionState, RichError>()");

static_assert(
    std::is_same_v<decltype(&OPC_UA_Client::isHealthy),
                   Result<ConnectionState, RichError> (OPC_UA_Client::*)()>,
    "isHealthy 公共签名应为 Result<ConnectionState, RichError>()");

static_assert(
    std::is_same_v<decltype(&OPC_UA_Client::checkLifeState),
                   Result<LifeState, RichError> (OPC_UA_Client::*)()>,
    "checkLifeState 公共签名应为 Result<LifeState, RichError>()");

static_assert(
    std::is_same_v<decltype(&ClientTestHooks::createWithSdk),
                   Result<std::unique_ptr<OPC_UA_Client>, RichError> (*)(
                       std::shared_ptr<ISdk>, const std::string &, ClientConfig,
                       bool, bool)>,
    "createWithSdk 公共签名不得暴露 UA_* 类型");
