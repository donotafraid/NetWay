// 临时探针：真实 server 的空 STRING 与全部标量类型往返。
#include "tests/include/util/ClientTestFixture.h"

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <array>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

struct AllTypeCase {
  const char *name;
  S7DataType type;
  ValueType value;
};

const std::array<AllTypeCase, 13> kAllTypeCases{{
    {"TestBool", S7DataType::BOOL, true},
    {"TestSint", S7DataType::SINT, int8_t{-7}},
    {"TestByte", S7DataType::BYTE, uint8_t{201}},
    {"TestInt2", S7DataType::INT, int16_t{-1234}},
    {"TestWord", S7DataType::WORD, uint16_t{54321}},
    {"TestDint", S7DataType::DINT, int32_t{-1234567}},
    {"TestUdint", S7DataType::UDINT, uint32_t{3000000000u}},
    {"TestDword", S7DataType::DWORD, uint32_t{4000000000u}},
    {"TestLint", S7DataType::LINT, int64_t{-1234567890123ll}},
    {"TestUlint", S7DataType::ULINT, uint64_t{18000000000000000000ull}},
    {"TestReal", S7DataType::REAL, 3.25f},
    {"TestLreal", S7DataType::LREAL, -9.5},
    {"TestStringAll", S7DataType::STRING, std::string{"all-types"}},
}};

class ClientAllTypesFixture : public ClientTest {
protected:
  void addTestNodes() override {
    ClientTest::addTestNodes();

    for (const auto &c : kAllTypeCases) {
      UA_VariableAttributes attr = UA_VariableAttributes_default;
      // setScalarCopy makes attr.value own a copy. setScalar would store a
      // pointer to a branch-local `v`; that pointer is dangling by the time
      // UA_Server_addVariableNode() deep-copies the attributes (caught by ASan
      // as stack-use-after-scope in T173/T174).
      auto setCopy = [&](const void *value, const UA_DataType *type) {
        const UA_StatusCode rc =
            UA_Variant_setScalarCopy(&attr.value, value, type);
        if (rc != UA_STATUSCODE_GOOD)
          throw std::runtime_error("UA_Variant_setScalarCopy failed");
      };
      if (c.type == S7DataType::STRING) {
        const std::string &s = std::get<std::string>(c.value);
        UA_String seed = UA_STRING_ALLOC(s.c_str());
        setCopy(&seed, &UA_TYPES[UA_TYPES_STRING]);
        UA_String_clear(&seed);
      } else if (c.type == S7DataType::BOOL) {
        UA_Boolean v = std::get<bool>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_BOOLEAN]);
      } else if (c.type == S7DataType::SINT) {
        int8_t v = std::get<int8_t>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_SBYTE]);
      } else if (c.type == S7DataType::BYTE) {
        uint8_t v = std::get<uint8_t>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_BYTE]);
      } else if (c.type == S7DataType::INT) {
        int16_t v = std::get<int16_t>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_INT16]);
      } else if (c.type == S7DataType::WORD) {
        uint16_t v = std::get<uint16_t>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_UINT16]);
      } else if (c.type == S7DataType::DINT) {
        int32_t v = std::get<int32_t>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_INT32]);
      } else if (c.type == S7DataType::UDINT || c.type == S7DataType::DWORD) {
        uint32_t v = std::get<uint32_t>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_UINT32]);
      } else if (c.type == S7DataType::LINT) {
        int64_t v = std::get<int64_t>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_INT64]);
      } else if (c.type == S7DataType::ULINT) {
        uint64_t v = std::get<uint64_t>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_UINT64]);
      } else if (c.type == S7DataType::REAL) {
        float v = std::get<float>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_FLOAT]);
      } else if (c.type == S7DataType::LREAL) {
        double v = std::get<double>(c.value);
        setCopy(&v, &UA_TYPES[UA_TYPES_DOUBLE]);
      }
      attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

      std::string name{c.name};
      UA_NodeId nodeId = UA_NODEID_STRING(1, name.data());
      UA_QualifiedName qn = UA_QUALIFIEDNAME(1, name.data());
      UA_NodeId parent = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
      UA_NodeId parentRef = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
      UA_Server_addVariableNode(
          server_, nodeId, parent, parentRef, qn,
          UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, nullptr,
          nullptr);
      UA_Variant_clear(&attr.value);
    }
  }
};

} // namespace

TEST_F(ClientAllTypesFixture, T173_RealSdk_AllScalarTypes_RoundTrip) {
  auto c = connectTo();
  ASSERT_NE(c, nullptr);

  std::vector<OPC_UA_Client::WriteValue> writes;
  writes.reserve(kAllTypeCases.size());
  for (const auto &c : kAllTypeCases)
    writes.push_back({{1, std::string(c.name), c.type}, c.value});

  auto wr = c->batchWrite(writes);
  ASSERT_TRUE(wr.is_success()) << (wr.is_fail() ? wr.get_error()->what() : "");
  const auto &wout = wr.value_or({});
  ASSERT_EQ(wout.size(), kAllTypeCases.size());
  for (size_t i = 0; i < wout.size(); ++i) {
    EXPECT_EQ(wout[i].status, OPC_UA_Client::WriteResult::Status::Good)
        << "write item " << i << " (" << kAllTypeCases[i].name << ")";
  }

  std::vector<OPC_UA_Client::ReadValue> reads;
  reads.reserve(kAllTypeCases.size());
  for (const auto &c : kAllTypeCases)
    reads.push_back({{1, std::string(c.name), c.type}});

  auto rr = c->batchRead(reads);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &rout = rr.value_or({});
  ASSERT_EQ(rout.size(), kAllTypeCases.size());
  for (size_t i = 0; i < rout.size(); ++i) {
    EXPECT_EQ(rout[i].rawStatus, UA_STATUSCODE_GOOD)
        << "read item " << i << " (" << kAllTypeCases[i].name << ")";
    ASSERT_TRUE(rout[i].value.has_value()) << "item " << i;
    EXPECT_EQ(rout[i].value->index(), kAllTypeCases[i].value.index())
        << "variant alternative " << i;
    EXPECT_EQ(*rout[i].value, kAllTypeCases[i].value) << "item " << i;
  }

  EXPECT_TRUE(c->shutdown().is_success());
}

TEST_F(ClientAllTypesFixture, T174_RealSdk_EmptyStringRoundTrip) {
  auto c = connectTo();
  ASSERT_NE(c, nullptr);

  OPC_UA_Client::WriteValue wv{{1, "TestStringAll", S7DataType::STRING},
                               std::string{}};
  auto wr = c->batchWrite(std::vector<OPC_UA_Client::WriteValue>{wv});
  ASSERT_TRUE(wr.is_success()) << (wr.is_fail() ? wr.get_error()->what() : "");
  ASSERT_EQ(wr.value_or({}).size(), 1u);
  EXPECT_EQ(wr.value_or({})[0].status,
            OPC_UA_Client::WriteResult::Status::Good);

  auto rr = c->batchRead({{1, "TestStringAll", S7DataType::STRING}});
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  ASSERT_TRUE(out[0].value.has_value())
      << "empty STRING returned by the real server was rejected; errorCode="
      << static_cast<int>(out[0].errorCode);
  ASSERT_TRUE(std::holds_alternative<std::string>(*out[0].value));
  EXPECT_EQ(std::get<std::string>(*out[0].value), "");

  EXPECT_TRUE(c->shutdown().is_success());
}
