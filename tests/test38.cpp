// tests/test38.cpp
//
// 异常安全补充用例。
//
// 现有用例大多只让 ISdk 返回错误码，缺少让 SDK 接缝**抛异常**的覆盖。
// 公共 batchRead/batchWrite/create/createWithSdk 声明为返回 Result（不抛），
// 因此异常必须由 RAII 守卫清干净：
//   · UA_ReadRequest / UA_WriteRequest 不能泄漏；
//   · UA_ReadResponse / UA_WriteResponse 不能泄漏；
//   · InFlightGuard 的 m_taskCounter 必须归零，否则 shutdown() 要等到
//     max(timeoutMs, 2*watchdogIntervalMs) 才返回。
// T135/T136 用"抛异常的钩子"验证上述三条；T137 验证 connect() 在 SDK 抛异常
// 后 CAS 复位、后续 connect() 仍可用；T138 钉死"形状错误的响应是终态失败，
// 不得被当成可重试错误重试"。
//
// 依据：src/OPCUAPacking/ua.cpp 的 ReadResponseGuard / WriteResponseGuard /
// InFlightGuard / CASGuard 析构逻辑，以及 batchRead 的可重试码集合
// （BADCONNECTIONCLOSED/BADCOMMUNICATIONERROR/.../BADSESSIONCLOSED）。

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"

namespace {

// timeoutMs 故意放大：若 m_taskCounter 没归零，shutdown() 会等满 20s，
// 用例用 <2s 的上界把它抓成失败。
std::unique_ptr<OPC_UA_Client> makeClient(const std::shared_ptr<MockSdk> &sdk) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 20000;
  cfg.watchdogIntervalMs = 10000;
  cfg.giveUpThresholdMs = 60000;
  cfg.maxTotalWaitMs = 300000;
  EXPECT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      /*enableWatchDog=*/false);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

long shutdownMillis(OPC_UA_Client &c, bool *ok) {
  const auto t0 = std::chrono::steady_clock::now();
  *ok = c.shutdown().is_success();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - t0)
      .count();
}

} // namespace

// ASAN 在动态 libc++ 场景下会误报 alloc-dealloc-mismatch：
//   异常对象的 __libcpp_refstring 在 ASAN 插桩的 operator new 里分配，
//   却在未插桩的 libc++abi.so 的 __cxa_end_catch 里被 free()。
// 这是 ASAN + 未插桩动态 C++ 运行时的已知限制，不是内存错误。
// 参考：https://github.com/google/sanitizers/issues/1440
extern "C" const char* __asan_default_options() {
    return "alloc_dealloc_mismatch=0";
}

// ============================================================
// T135: serviceRead 抛异常 —— batchRead 返回错误、守卫配平、shutdown 快
// ============================================================
TEST(ClientExceptionSafety, T135_ThrowingReadHook_GuardsBalanceAndShutdownIsFast) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setServiceReadHook([] { throw std::runtime_error("boom-read"); });

  std::vector<OPC_UA_Client::ReadValue> reads{{1, "A", S7DataType::INT}};
  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_fail()) << "SDK 抛异常时 batchRead 没有返回 Result 错误";
  EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::INTERNAL_ERROR)
      << rr.get_error()->codeName();
  EXPECT_EQ(sdk->inServiceRead(), 0u)
      << "MockSdk 的 inServiceRead 计数未在异常路径归零（守卫泄漏）";

  bool ok = false;
  const long ms = shutdownMillis(*c, &ok);
  EXPECT_TRUE(ok);
  EXPECT_LT(ms, 2000)
      << "InFlightGuard 未在异常路径归零，shutdown 等满了超时（" << ms
      << " ms）";
  EXPECT_EQ(sdk->clientNewCallCount(), sdk->clientDeleteCallCount())
      << "异常路径下 UA_Client 未配平";
}

// ============================================================
// T136: serviceWrite 抛异常 —— batchWrite 返回错误、请求/响应释放、shutdown 快
// ============================================================
TEST(ClientExceptionSafety,
     T136_ThrowingWriteHook_RequestFreedAndRuleHolds) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setServiceWriteHook([] { throw std::runtime_error("boom-write"); });

  std::vector<OPC_UA_Client::WriteValue> writes{
      {{1, "A", S7DataType::INT}, int16_t(7)}};
  auto wr = c->batchWrite(writes);
  ASSERT_TRUE(wr.is_fail()) << "SDK 抛异常时 batchWrite 没有返回 Result 错误";
  EXPECT_EQ(wr.get_error()->code(), RichError::ErrorCode::INTERNAL_ERROR)
      << wr.get_error()->codeName();
  EXPECT_EQ(sdk->inServiceWrite(), 0u)
      << "MockSdk 的 inServiceWrite 计数未在异常路径归零（守卫泄漏）";

  bool ok = false;
  const long ms = shutdownMillis(*c, &ok);
  EXPECT_TRUE(ok);
  EXPECT_LT(ms, 2000)
      << "InFlightGuard 未在异常路径归零，shutdown 等满了超时（" << ms
      << " ms）";
  EXPECT_EQ(sdk->clientNewCallCount(), sdk->clientDeleteCallCount());
}

// ============================================================
// T137: connectAsync 抛异常 —— CASGuard 必须复位，后续 connect() 仍可用
//
// 说明：
// "即使异常逃出，状态机不能卡在 CONNECTING"。
// ============================================================
TEST(ClientExceptionSafety,
     T137_ThrowingConnectHook_ResetsCasAndLaterConnectSucceeds) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  auto d = c->disconnect();
  ASSERT_TRUE(d.is_success()) << (d.is_fail() ? d.get_error()->what() : "");

  // The hook must be live during this call. The previous version cleared it
  // on the next line, so no exception ever crossed connect() and the test
  // proved nothing about CASGuard.
  sdk->setConnectAsyncHook([] { throw std::runtime_error("boom-connect"); });
  auto failed = c->connect();
  ASSERT_TRUE(failed.is_fail()) << "connectAsync hook 抛异常后 connect() 未返回错误";
  EXPECT_EQ(failed.get_error()->code(), RichError::ErrorCode::INTERNAL_ERROR)
      << "actual=" << failed.get_error()->codeName();

  // CASGuard 若未在异常路径复位，这里会拿到 THREAD_BUSY。
  sdk->setConnectAsyncHook(nullptr);
  auto again = c->connect();
  ASSERT_TRUE(again.is_success())
      << (again.is_fail() ? again.get_error()->codeName() : "");
  EXPECT_EQ(c->checkLifeState().value_or(LifeState::GIVEN_UP),
            LifeState::RUNNING);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T138: serviceResult==GOOD 但 resultsSize 与请求数不符 —— 终态失败，不重试
//
// 形状错误不是可重试码；若被重试，serviceReadCallCount() 会 > 1。
// ============================================================
TEST(ClientExceptionSafety, T138_MalformedReadResponse_IsNotRetried) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/0,
                            /*allocResults=*/false);
  std::vector<OPC_UA_Client::ReadValue> reads{{1, "A", S7DataType::INT}};
  auto rr = c->batchRead(reads);
  EXPECT_TRUE(rr.is_fail());
  EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::SERVICE_FAILED);
  EXPECT_EQ(sdk->serviceReadCallCount(), 1u)
      << "形状错误的响应被当成可重试错误重试了";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T139: 写路径的内嵌 NUL NodeId 必须在进入 SDK 前被拒绝
//
// 覆盖 detail/TypeTraits.h 写特化的 `containsEmbeddedNul(nodeID)` 分支
// （读路径由 B2 覆盖，字符串值的 NUL 由 T81 覆盖；标量写的 NodeId NUL
// 此前无任何用例）。
// ============================================================
TEST(ClientExceptionSafety,
     T139_BatchWrite_EmbeddedNulNodeId_RejectedBeforeSdkCall) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  const std::string badId("A\0B", 3);
  std::vector<OPC_UA_Client::WriteValue> writes{
      {{1, badId, S7DataType::INT}, int16_t(1)}};
  auto wr = c->batchWrite(writes);
  ASSERT_TRUE(wr.is_fail()) << "内嵌 NUL 的 NodeId 未被拒绝";
  EXPECT_EQ(wr.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT)
      << wr.get_error()->codeName();
  EXPECT_EQ(sdk->serviceWriteCallCount(), 0u)
      << "坏 NodeId 在进入 SDK 前就应被拒绝（writeTraits::containsEmbeddedNul）";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T140: STRING 请求被非 STRING 变体回答 —— nullopt + TYPE_MISMATCH
//
// 覆盖 readTraits::UATypeTraits<std::string> 的外层 `var.type != uaType()`。
// ============================================================
TEST(ClientExceptionSafety,
     T140_BatchRead_StringRequestNonStringVariant_MapsToNullopt) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16(0, 7);

  std::vector<OPC_UA_Client::ReadValue> reads{{1, "S", S7DataType::STRING}};
  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_FALSE(out[0].value.has_value())
      << "STRING 请求到了 INT16 变体，却读出了值（类型混淆）";
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::TYPE_MISMATCH);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T142: STRING 请求无值（hasValue=false）—— nullopt + TYPE_MISMATCH
//
// 覆盖 readTraits::UATypeTraits<std::string> 的外层 `!var.data`。
// ============================================================
TEST(ClientExceptionSafety,
     T142_BatchRead_StringRequestWithoutValue_MapsToNullopt) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true); // 不设 item -> hasValue=false

  std::vector<OPC_UA_Client::ReadValue> reads{{1, "S", S7DataType::STRING}};
  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_FALSE(out[0].value.has_value());
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::TYPE_MISMATCH);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T143: setGiveUpSignal() 在 shutdown 之后必须返回 ALREADY_TERMINATED
//       （T72 调用后丢弃了 Result，T45 没调用；错误码此前未被断言）
// ============================================================
TEST(ClientExceptionSafety,
     T143_SetGiveUpSignal_AfterShutdown_ReturnsAlreadyTerminated) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  ASSERT_TRUE(c->shutdown().is_success());
  auto r = c->setGiveUpSignal();
  ASSERT_TRUE(r.is_fail());
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED)
      << r.get_error()->codeName();
}

// ============================================================
// T147: 写值类型与声明的 S7DataType 不匹配 —— 错误码必须是 TYPE_MISMATCH，
//       而不是 RichError(string) 兜底的 UNKNOWN。
//
// T49 只断言 is_fail()，没有钉死 code。读路径同种错误给出 TYPE_MISMATCH
// （T29/T140），写路径却给 UNKNOWN：调用方无法用 code() 区分
// “值类型写错了”和“内部未知错误”，与 README“ErrorCode 是稳定枚举、
// what() 不参与分支判断”的契约相矛盾。
// ============================================================
TEST(ClientExceptionSafety,
     T147_BatchWrite_TypeMismatch_ReportsTypeMismatch) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  OPC_UA_Client::WriteValue w{{1, "TestInt", S7DataType::INT},
                              std::string("not-an-int")};
  std::vector<OPC_UA_Client::WriteValue> v{w};
  auto wr = c->batchWrite(v);
  ASSERT_TRUE(wr.is_fail()) << "类型不匹配竟然写成功";
  EXPECT_EQ(wr.get_error()->code(), RichError::ErrorCode::TYPE_MISMATCH)
      << "actual=" << wr.get_error()->codeName();
  EXPECT_EQ(sdk->serviceWriteCallCount(), 0u);

  EXPECT_TRUE(c->shutdown().is_success());
}
