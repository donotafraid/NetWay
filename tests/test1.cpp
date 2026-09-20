#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <variant>
#include <vector>
#include "tests/MockSdk.h"

// T1 是集成测试，依赖真实设备。CI 默认跳过，除非设 OPCUA_RUN_INTEGRATION=1。
TEST(Client, T1_ConnectSuccess) {
    const char* gate = std::getenv("OPCUA_RUN_INTEGRATION");
    if (!gate || std::string(gate) != "1") {
        GTEST_SKIP() << "T1 是集成测试，需 OPCUA_RUN_INTEGRATION=1 且真实设备可达";
    }

    const char* urlEnv = std::getenv("OPCUA_ENDPOINT_URL");
    std::string endpointUrl =
        urlEnv ? urlEnv : "opc.tcp://192.0.2.1:4840";   // P3-1: RFC 5737 文档地址

    ClientConfig cfg;
    auto createResult = OPC_UA_Client::create(endpointUrl, cfg, true);
    ASSERT_TRUE(createResult.has_value())
        << "connect failed: " << createResult.get_error()->what();   // P2-1: 去掉 *
    std::unique_ptr<OPC_UA_Client> c = std::move(*createResult.get());

    auto connResult = c->checkConnected();
    ASSERT_TRUE(connResult.has_value())
        << "checkConnected failed: " << connResult.get_error()->what();
    EXPECT_EQ(connResult.value_or(ConnectionState::OBJECT_ONLY),
              ConnectionState::CONNECTED);

    uint16_t ns = 3;
    std::string id = "\"DB111_EdgeGatewayTest\".\"Uint32_t_UDInt\"";
    S7DataType type = S7DataType::UDINT;
    uint32_t value = 42;
    std::vector<OPC_UA_Client::ReadValue> nodes{{{ns, id, type}}};
    std::vector<OPC_UA_Client::WriteValue> writeNodes{{ns, id, type, value}};

    auto writeResult = c->batchWrite(writeNodes);
    ASSERT_TRUE(writeResult.has_value())
        << "write fail: " << writeResult.get_error()->what();

    auto readResult = c->batchRead(nodes);
    ASSERT_TRUE(readResult.has_value())
        << "read fail: " << readResult.get_error()->what();

    const auto& results = readResult.value_or({});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].rawStatus, UA_STATUSCODE_GOOD);
    ASSERT_TRUE(results[0].value.has_value());

    const auto& v = results[0].value.value();
    ASSERT_TRUE(std::holds_alternative<uint32_t>(v))
        << "期望 uint32_t，实际类型索引: " << v.index();
    EXPECT_EQ(std::get<uint32_t>(v), value);
}