// T81–T83 的落地补测（全部基于 MockSdk，确定性、无网络）：
//   T81  写入 STRING 含内嵌 '\0' 时不得被 UA_STRING(strlen) 静默截断；
//   T82  batchWrite 对全部 12 个 S7DataType 的 UA 类型映射正确；
//   T83  batchRead  对全部 12 个 S7DataType 的解析正确。
//
// 依赖 MockSdk 的两处改动（见 01-MockSdk-seams-and-leak-fix.diff）：
//   - setReadItemScalar(i, ptr, uaType)          通用读取标量 seam
//   - WriteObservation lastWriteObservation()    写入观察点
//   - buildShapedReadResponse 的 STRING 分支已修复为单次 setScalarCopy
//
// 缺陷提示：T81 的“不得截断”、T82/T83 的 DWORD 分支，在修复前会失败。

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <string>
#include <variant>
#include <vector>
#include "src/OPCUAPacking//detail/TypeTraits.h" // 仅测试用于取 UATypeTraits<T>::uaType()
#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"

namespace {
std::unique_ptr<OPC_UA_Client>
makeClientNoWatchdog(const std::shared_ptr<MockSdk> &sdk) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      /*enableWatchDog=*/false);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

int uaTypeIndexOf(const UA_DataType *t) {
  if (!t)
    return -1;
  for (int i = 0; i < UA_TYPES_COUNT; ++i)
    if (&UA_TYPES[i] == t)
      return i;
  return -1;
}
} // namespace

// ============================================================
// T81: 写入 STRING 含内嵌 '\0' —— 必须长度保真，或显式拒绝。
//     现状：writeTraits<std::string> 用 UA_STRING(value.c_str())，
//     而 UA_STRING 内部走 strlen，值被静默截断（读取侧 T75 已保真）。
// ============================================================
TEST(Client, T81_BatchWrite_StringWithEmbeddedNul_MustNotBeTruncated) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);

  const std::string payload("A\0B\0C", 5);
  std::vector<OPC_UA_Client::WriteValue> v{
      {{1, "S", S7DataType::STRING}, payload}};

  auto rr = c->batchWrite(v);
  if (rr.is_fail()) {
    // 显式拒绝是可接受的设计；静默截断不可接受。
    EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT)
        << "actual=" << rr.get_error()->codeName();
    EXPECT_TRUE(c->shutdown().is_success());
    return;
  }

  const auto obs = sdk->lastWriteObservation();
  ASSERT_EQ(obs.valueStrings.size(), 1u);
  EXPECT_EQ(obs.valueStrings[0].size(), payload.size())
      << "写入 STRING 被 UA_STRING(strlen) 截断：SDK 收到 "
      << obs.valueStrings[0].size() << " 字节，调用方传入 " << payload.size();
  EXPECT_EQ(obs.valueStrings[0], payload);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T82: batchWrite 全类型映射（S7DataType -> UA_TYPES 下标）
// ============================================================
TEST(Client, T82_BatchWrite_AllS7Types_MapToCorrectUaType) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);

  struct Case {
    S7DataType s7;
    ValueType value;
  };
  const std::vector<Case> cases{
      {S7DataType::BOOL, ValueType{true}},
      {S7DataType::SINT, ValueType{int8_t(-1)}},
      {S7DataType::BYTE, ValueType{uint8_t(255)}},
      {S7DataType::INT, ValueType{int16_t(-2)}},
      {S7DataType::WORD, ValueType{uint16_t(65535)}},
      {S7DataType::DINT, ValueType{int32_t(-3)}},
      {S7DataType::UDINT, ValueType{uint32_t(4294967295u)}},
      {S7DataType::DWORD, ValueType{uint32_t(1u)}}, // 修复前 dispatch 不支持
      {S7DataType::LINT, ValueType{int64_t(-4)}},
      {S7DataType::ULINT, ValueType{uint64_t(1)}},
      {S7DataType::REAL, ValueType{float(1.5f)}},
      {S7DataType::LREAL, ValueType{double(2.5)}},
      {S7DataType::STRING, ValueType{std::string("ok")}},
  };

  for (const auto &tc : cases) {
    std::vector<OPC_UA_Client::WriteValue> v{{{1, "N", tc.s7}, tc.value}};

    auto rr = c->batchWrite(v);
    ASSERT_TRUE(rr.is_success())
        << "S7DataType=" << static_cast<int>(tc.s7) << " batchWrite 失败: "
        << (rr.is_fail() ? rr.get_error()->what() : "");

    const auto obs = sdk->lastWriteObservation();
    ASSERT_EQ(obs.typeIndices.size(), 1u)
        << "S7DataType=" << static_cast<int>(tc.s7);

    const int expected = std::visit(
        [](const auto &val) {
          using T = std::decay_t<decltype(val)>;
          return uaTypeIndexOf(
              opcua::writeTraits::UATypeTraits<T>::uaType());
        },
        tc.value);

    EXPECT_EQ(obs.typeIndices[0], expected)
        << "S7DataType=" << static_cast<int>(tc.s7) << " 写成了 UA_TYPES["
        << obs.typeIndices[0] << "]，期望 " << expected;
    ASSERT_EQ(obs.nodeIds.size(), 1u);
    EXPECT_EQ(obs.nodeIds[0], "N");
  }

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T83: batchRead 全类型解析（UA_TYPES -> S7DataType -> variant）
// ============================================================
TEST(Client, T83_BatchRead_AllS7Types_ParseToCorrectAlternative) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);

  struct Case {
    S7DataType s7;
    ValueType value;
  };
  const std::vector<Case> cases{
      {S7DataType::BOOL, ValueType{true}},
      {S7DataType::SINT, ValueType{int8_t(-1)}},
      {S7DataType::BYTE, ValueType{uint8_t(255)}},
      {S7DataType::INT, ValueType{int16_t(-2)}},
      {S7DataType::WORD, ValueType{uint16_t(65535)}},
      {S7DataType::DINT, ValueType{int32_t(-3)}},
      {S7DataType::UDINT, ValueType{uint32_t(4294967295u)}},
      {S7DataType::DWORD, ValueType{uint32_t(1u)}}, // 修复前 dispatch 不支持
      {S7DataType::LINT, ValueType{int64_t(-4)}},
      {S7DataType::ULINT, ValueType{uint64_t(1)}},
      {S7DataType::REAL, ValueType{float(1.5f)}},
      {S7DataType::LREAL, ValueType{double(2.5)}},
      {S7DataType::STRING, ValueType{std::string("ok")}},
  };

  for (const auto &tc : cases) {
    sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                              /*allocResults=*/true);

    if (tc.s7 == S7DataType::STRING) {
      sdk->setReadItemString(0, std::get<std::string>(tc.value));
    } else {
      std::visit(
          [&](const auto &val) {
            using T = std::decay_t<decltype(val)>;
            sdk->setReadItemScalar(
                0, &val, opcua::readTraits::UATypeTraits<T>::uaType());
          },
          tc.value);
    }

    std::vector<OPC_UA_Client::ReadValue> v{{1, "N", tc.s7}};
    auto rr = c->batchRead(v);
    ASSERT_TRUE(rr.is_success())
        << "S7DataType=" << static_cast<int>(tc.s7) << " batchRead 失败: "
        << (rr.is_fail() ? rr.get_error()->what() : "");

    const auto &out = rr.value_or({});
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
    ASSERT_TRUE(out[0].value.has_value())
        << "S7DataType=" << static_cast<int>(tc.s7)
        << " 解析为 nullopt（TypeTraits 缺特化或 UA 类型写错）";
    EXPECT_TRUE(*out[0].value == tc.value)
        << "S7DataType=" << static_cast<int>(tc.s7) << " 值/类型不一致";

    sdk->resetResponseShape();
  }

  EXPECT_TRUE(c->shutdown().is_success());
}
