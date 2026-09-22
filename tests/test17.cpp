// tests/test17.cpp
//
// 资源生命周期补充（对应审查报告 §6.1 / G1、G2、G3、G7、G8）：
//   T34  batchRead  中途参数校验失败 → ReadRequestGuard 必须释放已分配 NodeId
//   T35  batchWrite 中途参数校验失败 → WriteRequestGuard 必须释放已分配 NodeId/Variant
//   T36  STRING 读取：ReadResult.value 必须是独立 std::string，不借用 UA_String
//   T37  service GOOD 但 hasValue=false → value 必须为 nullopt（不能伪造值）
//   T38  可重试错误且 results 已分配 → 每次重试都要被 Guard 清理
//
// 这些用例本身不直接断言“内存”，而是把内存错误交给 ASan/LSan：
// scripts/run_asan.sh 与 run_lsan.sh 已把本套件加入过滤列表。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "tests/MockSdk.h"

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

namespace {
std::unique_ptr<OPC_UA_Client>
makeClientNoWatchdog(const std::shared_ptr<MockSdk> &sdk, ClientConfig cfg) {
  auto r = OPC_UA_Client::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      /*enableWatchDog=*/false);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}
} // namespace

// ============================================================
// T34: batchRead 部分分配后失败，必须释放已分配的请求项
// ============================================================
TEST(Client, T34_BatchRead_PartialAllocationFailure_FreesRequest) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);

  auto c = makeClientNoWatchdog(sdk, cfg);
  ASSERT_NE(c, nullptr);

  // 第 0 项合法（UA_NODEID_STRING_ALLOC 已分配），第 1 项 id 为空 →
  // batchRead 在循环中途返回 INVALID_ARGUMENT；ReadRequestGuard 必须
  // 清掉第 0 项以及未走到的尾部元素。
  std::vector<OPC_UA_Client::ReadValue> v{
      {1, "Good", S7DataType::INT},
      {1, "", S7DataType::INT}};

  auto r = c->batchRead(v);
  ASSERT_TRUE(r.is_fail());
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);
  EXPECT_EQ(sdk->serviceReadCallCount(), 0u)
      << "参数校验失败时不应发起 SDK 调用";

  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T35: batchWrite 部分分配后失败，必须释放已分配的请求项
// ============================================================
TEST(Client, T35_BatchWrite_PartialAllocationFailure_FreesRequest) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);

  auto c = makeClientNoWatchdog(sdk, cfg);
  ASSERT_NE(c, nullptr);

  // 第 0 项合法：writeConversion::dispatch 会分配 NodeId  Variant；
  // 第 1 项 id 为空 → 中途返回 INVALID_ARGUMENT。
  std::vector<OPC_UA_Client::WriteValue> v{
      {{1, "Good", S7DataType::INT}, int16_t(7)},
      {{1, "", S7DataType::INT}, int16_t(8)}};

  auto r = c->batchWrite(v);
  ASSERT_TRUE(r.is_fail());
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);
  EXPECT_EQ(sdk->serviceWriteCallCount(), 0u)
      << "参数校验失败时不应发起 SDK 调用";

  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T36: STRING 读取结果的所有权
// ============================================================
TEST(Client, T36_BatchRead_StringValueLifetime) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);

  auto c = makeClientNoWatchdog(sdk, cfg);
  ASSERT_NE(c, nullptr);

  const std::string payload = "OPC-UA-string-lifecycle";
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemString(0, payload);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "S", S7DataType::STRING}};
  auto r = c->batchRead(v);
  ASSERT_TRUE(r.is_success()) << (r.is_fail() ? r.get_error()->what() : "");

  const auto &out = r.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  ASSERT_TRUE(out[0].value.has_value());
  ASSERT_TRUE(std::holds_alternative<std::string>(*out[0].value));
  EXPECT_EQ(std::get<std::string>(*out[0].value), payload);

  // 此刻 UA_ReadResponse 已被 ReadResponseGuard 清理。若 ReadResult 借用
  // UA_String 的 data 指针，本次检查会以 use-after-free / leak 报错。
  OPCUA_LSAN_CHECK();
  EXPECT_EQ(std::get<std::string>(*out[0].value), payload)
      << "ReadResult 借用了已释放的 UA_String 内存";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T37: GOOD 状态但没有值 → nullopt
// ============================================================
TEST(Client, T37_BatchRead_GoodStatusWithoutValue_MapsToNullopt) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);

  auto c = makeClientNoWatchdog(sdk, cfg);
  ASSERT_NE(c, nullptr);

  // 分配 results，但每项 hasValue=false（状态仍 GOOD）。
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, 1, true);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
  auto r = c->batchRead(v);
  ASSERT_TRUE(r.is_success()) << (r.is_fail() ? r.get_error()->what() : "");

  const auto &out = r.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_FALSE(out[0].value.has_value())
      << "GOOD 但 hasValue=false 必须映射为 nullopt，不能伪造值";

  sdk->resetResponseShape();
  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T38: 可重试错误  已分配 results → 每次重试都要清理
// ============================================================
TEST(Client, T38_BatchRead_RetryableWithResults_NoLeakAcrossRetries) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.maxRetries = 3;
  cfg.retryBackoffBaseMs = 1;
  cfg.retryMaxBackoffMs = 2;
  cfg.maxTotalWaitMs = 2 * cfg.timeoutMs * (cfg.maxRetries + 1); // 40000
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  // 每次 serviceRead 都返回可重试错误，并且 results 已分配。
  // 若 ReadResponseGuard 只在最终出口清理，前几次的 results 会泄漏。
  sdk->setReadResponseShape(UA_STATUSCODE_BADCONNECTIONCLOSED, 1, true);

  auto r0 = OPC_UA_Client::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      /*enableWatchDog=*/false);
  ASSERT_TRUE(r0.has_value()) << r0.get_error()->what();
  auto c = std::move(*r0.get());

  std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
  auto r = c->batchRead(v);
  ASSERT_TRUE(r.is_fail());
  EXPECT_EQ(r.get_error()->code(),
            RichError::ErrorCode::SERVICE_RETRY_EXHAUSTED);
  EXPECT_EQ(sdk->serviceReadCallCount(),
            static_cast<unsigned>(cfg.maxRetries) + 1u);

  sdk->resetResponseShape();
  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

