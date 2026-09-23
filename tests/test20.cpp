// tests/test20_known_defects.cpp
// 公共 API 封装签名锁 + createWithSdk 连接错误码透传回归用例。
//
// 签名锁：把封装完整性从“能否 include”提升到“签名长什么样”。
// T46：回归 F3（create/createWithSdk 连接失败必须原样透传 CONNECT_TIMEOUT，
//      不能降级为 UNKNOWN）。
// 从“能否 include”提升到“签名长什么样”。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>
#include "tests/MockSdk.h"
#include "OPCUAPacking/internal/ClientTestHooks.h"

// ---------------------------------------------------------------------------
// 编译期封装签名锁：公共方法只能使用领域类型。
// 若有人在公共签名里塞回 UA_* 类型，这些 static_assert 会直接编译失败。
// ---------------------------------------------------------------------------
static_assert(
    std::is_same_v<
        decltype(&OPC_UA_Client::batchRead),
        Result<std::vector<OPC_UA_Client::ReadResult>, RichError> (
            OPC_UA_Client::*)(const std::vector<OPC_UA_Client::ReadValue> &)>,
    "batchRead 公共签名不得暴露 UA_* 类型");

static_assert(
    std::is_same_v<
        decltype(&OPC_UA_Client::batchWrite),
        Result<std::vector<OPC_UA_Client::WriteResult>, RichError> (
            OPC_UA_Client::*)(const std::vector<OPC_UA_Client::WriteValue> &)>,
    "batchWrite 公共签名不得暴露 UA_* 类型");

static_assert(
    std::is_same_v<
        decltype(&OPC_UA_Client::create),
        Result<std::unique_ptr<OPC_UA_Client>, RichError> (*)(
            const std::string &, ClientConfig, bool)>,
    "create 公共签名不得暴露 UA_* 类型");

// ============================================================
// T46: createWithSdk 在 connect 失败时原样透传错误码（回归 F3）
//
// 修复前：RichError{connectResult.get_error()->what()} 只带 what，
//         错误码被降级为 UNKNOWN。
// 修复后：RichError{code, what} 原样透传 CONNECT_TIMEOUT。
// 本用例守护该行为，防止回退。
// ============================================================
TEST(Client, T46_CreateWithSdkFailurePropagatesConnectErrorCode) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Unreachable, /*allowAuto=*/false);
  sdk->setAutoAdvancePerIterateMs(50);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 200;
  cfg.retryBackoffBaseMs = 10;
  cfg.retryMaxBackoffMs = 50;
  cfg.maxTotalWaitMs = 5000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.is_fail());

  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::CONNECT_TIMEOUT)
      << "错误码被降级；actual=" << r.get_error()->codeName();
}


// ============================================================
// T47: create 在 connect 失败时原样透传错误码（回归 F3）
// ============================================================
TEST(Client, T47_CreateFailurePropagatesConnectErrorCode) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Unreachable, /*allowAuto=*/false);
  sdk->setAutoAdvancePerIterateMs(50);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 200;
  cfg.retryBackoffBaseMs = 10;
  cfg.retryMaxBackoffMs = 50;
  cfg.maxTotalWaitMs = 5000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  auto r = OPC_UA_Client::create("opc.tcp://192.0.2.1:4840", cfg,
                                 /*useDefault=*/false);
  ASSERT_TRUE(r.is_fail());
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::CONNECT_TIMEOUT)
      << "错误码被降级；actual=" << r.get_error()->codeName();
}