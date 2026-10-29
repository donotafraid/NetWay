// T87：把 test20 已有的 3 条签名锁扩展到全部公开入口。
// 目标：公共签名一旦回退到 UA_* 类型，本文件立即编译失败。
// 与 tests/compile/EncapsulationProbe.cpp 互补：
//   - Probe 保证 ua.h 不 include open62541；
//   - 本用例保证所有公共签名只用领域类型。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

TEST(ClientEncapsulation, T87_PublicApiSignaturesUseDomainTypesOnly) {
  using C = OPC_UA_Client;

  static_assert(
      std::is_same_v<decltype(&C::create),
                     Result<std::unique_ptr<C>, RichError> (*)(
                         const std::string &, ClientConfig, bool) noexcept>,
      "create 不得暴露 UA_*");
  static_assert(std::is_same_v<decltype(&C::connect),
                               Result<Unit, RichError> (C::*)() noexcept>,
                "connect 不得暴露 UA_*");
  static_assert(std::is_same_v<decltype(&C::disconnect),
                               Result<Unit, RichError> (C::*)() noexcept>,
                "disconnect 不得暴露 UA_*");
  static_assert(std::is_same_v<decltype(&C::checkConnected),
                               Result<Unit, RichError> (C::*)() noexcept>,
                "checkConnected 不得暴露 UA_*");
  static_assert(std::is_same_v<decltype(&C::isHealthy),
                               Result<Unit, RichError> (C::*)() noexcept>,
                "isHealthy 不得暴露 UA_*");
  static_assert(std::is_same_v<decltype(&C::checkLifeState),
                               Result<LifeState, RichError> (C::*)() noexcept>,
                "checkLifeState 不得暴露 UA_*");
  static_assert(std::is_same_v<decltype(&C::recreateGiveUpClient),
                               Result<Unit, RichError> (C::*)() noexcept>,
                "recreateGiveUpClient 不得暴露 UA_*");
  static_assert(std::is_same_v<decltype(&C::shutdown),
                               Result<Unit, RichError> (C::*)() noexcept>,
                "shutdown 不得暴露 UA_*");
  static_assert(std::is_same_v<decltype(&C::setGiveUpSignal), Result<Unit, RichError> (C::*)() noexcept>,
                "setGiveUpSignal 不得暴露 UA_*,且必须返回 Result 以便区分拒绝原因");
  static_assert(
      std::is_same_v<decltype(&C::batchRead),
                     Result<std::vector<C::ReadResult>, RichError> (C::*)(
                         const std::vector<C::ReadValue> &) noexcept>,
      "batchRead 不得暴露 UA_*");
  static_assert(
      std::is_same_v<decltype(&C::batchWrite),
                     Result<std::vector<C::WriteResult>, RichError> (C::*)(
                         const std::vector<C::WriteValue> &) noexcept>,
      "batchWrite 不得暴露 UA_*");

  SUCCEED();
}

// ============================================================
// T101: 成员级封装锁 —— 公开值类型不得出现 UA_* 成员。
// 仅锁方法签名（T87）无法阻止在 NodeId/ReadResult 里加一个 UA_StatusCode。
// ============================================================
namespace {
using C = OPC_UA_Client;
static_assert(std::is_same_v<decltype(C::NodeId::ns), uint16_t>, "NodeId::ns");
static_assert(std::is_same_v<decltype(C::NodeId::id), std::variant<std::string,uint32_t>>, "NodeId::id");
static_assert(std::is_same_v<decltype(C::NodeId::dataType), S7DataType>, "NodeId::dataType");
static_assert(std::is_same_v<decltype(C::ReadValue::node), C::NodeId>, "ReadValue::node");
static_assert(std::is_same_v<decltype(C::WriteValue::node), C::NodeId>, "WriteValue::node");
static_assert(std::is_same_v<decltype(C::WriteValue::value), ValueType>, "WriteValue::value");
static_assert(std::is_same_v<decltype(C::ReadResult::rawStatus), uint32_t>, "ReadResult::rawStatus");
static_assert(std::is_same_v<decltype(C::ReadResult::errorCode), RichError::ErrorCode>, "ReadResult::errorCode");
static_assert(std::is_same_v<decltype(C::ReadResult::value), std::optional<ValueType>>, "ReadResult::value");
static_assert(std::is_same_v<decltype(C::WriteResult::rawStatus), uint32_t>, "WriteResult::rawStatus");
} // namespace

TEST(ClientEncapsulation, T101_PublicValueTypeMembersAreUaFree) { SUCCEED(); }

