// 补充用例：
//   T88  readTraits 必须拒绝 arrayLength>0 的“标量类型数组”UA_Variant；
//   T89  同一规则必须作用于 STRING 特化；
//   T90  batchRead 端到端：数组值必须映射为 nullopt，不得静默返回首元素；
//   T92  batchRead：resultsSize < 请求数必须失败（T28 只覆盖了 >）。
//
// 依据：open62541 v1.4.14 include/open62541/types.h 对 UA_Variant 的不变量
//   arrayLength == 0 && data > UA_EMPTY_ARRAY_SENTINEL : scalar value
//   arrayLength > 0                                     : array of given length
// 数组的 type 仍等于元素类型，所以只比较 type 的转换器会误读数组。

#include "OPCUAPacking/ua.h"
#include "src/OPCUAPacking//detail/TypeTraits.h" // 直接测 readTraits

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <open62541/types.h>

#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"

namespace {
std::unique_ptr<OPC_UA_Client>
makeClient(const std::shared_ptr<MockSdk> &sdk) {
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
} // namespace

// ------------------------------------------------------------
// T88: 标量类型数组不得被当成标量
// ------------------------------------------------------------
TEST(ClientVariantShape, T88_ReadTraits_RejectsScalarTypeArrayVariant) {
  int16_t data[3] = {10, 20, 30};
  UA_Variant v;
  UA_Variant_init(&v);
  ASSERT_EQ(UA_Variant_setArrayCopy(&v, data, 3, &UA_TYPES[UA_TYPES_INT16]),
            UA_STATUSCODE_GOOD);

  auto r = opcua::readTraits::UATypeTraits<int16_t>::convert(v);
  ASSERT_TRUE(r.is_fail())
      << "arrayLength>0 被当作标量读取，静默返回首元素 "
      << r.value_or(int16_t(-999));
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::TYPE_MISMATCH);

  UA_Variant_clear(&v);
}

// ------------------------------------------------------------
// T89: STRING 数组同理
// ------------------------------------------------------------
TEST(ClientVariantShape, T89_ReadTraits_RejectsStringArrayVariant) {
  static UA_Byte sa[] = {'a'};
  static UA_Byte sb[] = {'b'};
  UA_String arr[2] = {{1, sa}, {1, sb}};
  UA_Variant v;
  UA_Variant_init(&v);
  ASSERT_EQ(UA_Variant_setArrayCopy(&v, arr, 2, &UA_TYPES[UA_TYPES_STRING]),
            UA_STATUSCODE_GOOD);

  auto r = opcua::readTraits::UATypeTraits<std::string>::convert(v);
  ASSERT_TRUE(r.is_fail())
      << "字符串数组被当作标量读取，静默返回首元素 \""
      << r.value_or(std::string("?")) << "\"";
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::TYPE_MISMATCH);

  UA_Variant_clear(&v);
}

// ------------------------------------------------------------
// T90: batchRead 端到端 —— 数组值必须 nullopt
// ------------------------------------------------------------
TEST(ClientVariantShape, T90_BatchRead_ArrayValue_MustMapToNullopt) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16Array(0, {10, 20, 30});

  std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");

  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_FALSE(out[0].value.has_value())
      << "数组值被当成标量，读到首元素；READ 结果静默损坏";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ------------------------------------------------------------
// T92: resultsSize < 请求数
// ------------------------------------------------------------
TEST(ClientVariantShape, T92_BatchRead_FewerResultsThanRequested_Rejected) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT},
                                          {1, "B", S7DataType::INT}};

  auto rr = c->batchRead(v);
  EXPECT_TRUE(rr.is_fail()) << "resultsSize(1) < 请求数(2) 竟被接受";

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ------------------------------------------------------------
// T144: 空数组哨兵（arrayLength==0 && data==UA_EMPTY_ARRAY_SENTINEL）
//       必须被 readTraits 拒绝。旧判据 (!data || arrayLength>0) 会放行它，
//       随后解引用地址 0x01（段错误）。
// 依据：open62541 v1.4.14 include/open62541/types.h:715-721、:762-765。
// ------------------------------------------------------------
TEST(ClientVariantShape, T144_ReadTraits_RejectsEmptyArraySentinel) {
  UA_Variant v;
  UA_Variant_init(&v);
  v.type = &UA_TYPES[UA_TYPES_INT16];
  v.storageType = UA_VARIANT_DATA;
  v.arrayLength = 0;
  v.data = UA_EMPTY_ARRAY_SENTINEL;

  ASSERT_FALSE(UA_Variant_isScalar(&v))
      << "前提错误：哨兵变体不应被视为 scalar";

  auto r = opcua::readTraits::UATypeTraits<int16_t>::convert(v);
  EXPECT_TRUE(r.is_fail()) << "空数组哨兵被当成标量，解引用 0x01";
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::TYPE_MISMATCH);

  UA_Variant_clear(&v); // 哨兵地址不会被 free（Variant_clear 判 > 哨兵）
}

// ------------------------------------------------------------
// T145: batchRead 端到端 —— 空 INT 数组必须 nullopt + TYPE_MISMATCH
// ------------------------------------------------------------
TEST(ClientVariantShape, T145_BatchRead_EmptyIntArray_MapsToNullopt) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemEmptyArray(0, &UA_TYPES[UA_TYPES_INT16]);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_FALSE(out[0].value.has_value()) << "空数组被当成标量读取";
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::TYPE_MISMATCH);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ------------------------------------------------------------
// T146: 空 STRING 数组同理（走 UATypeTraits<std::string> 特化）
// ------------------------------------------------------------
TEST(ClientVariantShape, T146_BatchRead_EmptyStringArray_MapsToNullopt) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemEmptyArray(0, &UA_TYPES[UA_TYPES_STRING]);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "S", S7DataType::STRING}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_FALSE(out[0].value.has_value());
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::TYPE_MISMATCH);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ------------------------------------------------------------
// T149: hasValue == true 但 variant 为空（type == nullptr）。
//       这是“服务器在 DataValue 里声明了值、却给了 empty variant”的真实形态，
//       与 T142 的 hasValue == false 是两条不同的服务器行为。读转换只允许看
//       variant：empty variant 的 type == nullptr，必须映射为 TYPE_MISMATCH，
//       绝不能因为 hasValue==true 就把它当成标量。
// 依据：open62541 v1.4.14 include/open62541/types.h:723-724（empty variant 的
//       type == NULL；UA_Variant_isScalar 为 false）。
// ------------------------------------------------------------
TEST(ClientVariantShape,
     T149_BatchRead_HasValueButEmptyVariant_MapsToTypeMismatch) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemHasValueWithoutVariant(0);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_FALSE(out[0].value.has_value())
      << "hasValue==true 且 variant.type==nullptr 竟被当成标量读取";
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::TYPE_MISMATCH);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}