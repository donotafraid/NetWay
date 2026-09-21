#include "tests/util/ClientTestFixture.h"
#include "OPCUAPacking/ua.h"
#include <open62541/client.h>     // §6.4: 客户端测试不需要 server.h
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <thread>
#include <variant>
#include <vector>

TEST_F(ClientTest, T2_ConnectSuccess) {
    auto c = connectTo();
    ASSERT_NE(c, nullptr);

    auto connResult = c->checkConnected();
    ASSERT_TRUE(connResult.has_value()) << connResult.get_error()->what();
    EXPECT_EQ(connResult.value_or({}), ConnectionState::CONNECTED);
}

TEST_F(ClientTest, T3_ReadTestInt) {
    auto c = connectTo();

    std::vector<OPC_UA_Client::ReadValue> nodes;
    std::string varName{"TestInt"};
    uint16_t nameSpace = 1;
    nodes.push_back({{nameSpace, varName.data(), S7DataType::INT}});

    auto r = c->batchRead(nodes);
    ASSERT_TRUE(r.has_value()) << r.get_error()->what();
    const auto& results = r.value_or({});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].rawStatus, UA_STATUSCODE_GOOD);
    ASSERT_TRUE(results[0].value.has_value());
}



TEST_F(ClientTest, T5_ConcurrentReadWrite_BoundedFail) {
    auto c = connectTo();
    ASSERT_NE(c, nullptr);

    constexpr int kThreads    = 8;
    constexpr int kIterations = 50;
    constexpr int kTotal      = kThreads * kIterations;
    std::atomic<int> ok{0}, rejected{0};

    const uint16_t ns = 1;
    const std::string varName{"TestInt"};
    const S7DataType type = S7DataType::INT;
    OPC_UA_Client::NodeId node{ns, varName, type};

    {
        std::vector<OPC_UA_Client::ReadValue> rv{{node}};
        auto r = c->batchRead(rv);
        ASSERT_TRUE(r.is_success()) << r.get_error()->what();
        const auto& results = r.value_or({});
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0].rawStatus, UA_STATUSCODE_GOOD);
        ASSERT_TRUE(results[0].value.has_value());
    }

    // 写线程只用这些值；读线程校验读到的值必须 ∈ 该集合
    const std::array<int16_t, 4> kWrittenValues = {1, 3, 5, 7};


    // ★ 在并发开始前，先写一次，确保服务器上的值 ∈ kWrittenValues
    {
      OPC_UA_Client::WriteValue var{node, int16_t(1)};
      std::vector<OPC_UA_Client::WriteValue> wv{var};
      auto w = c->batchWrite(wv);
      ASSERT_TRUE(w.is_success()) << w.get_error()->what();
    }

    std::vector<std::thread> ts;
    for (int16_t i = 0; i < kThreads; ++i) {
        ts.emplace_back([&, i]() {
            for (int j = 0; j < kIterations; ++j) {
                if (i % 2 == 0) {
                    std::vector<OPC_UA_Client::ReadValue> rv{{node}};
                    auto rr = c->batchRead(rv);
                    if (!rr.is_success()) { ++rejected; continue; }
                    const auto& results = rr.value_or({});
                    const bool allGood =
                        results.size() == 1 &&
                        results[0].rawStatus == UA_STATUSCODE_GOOD &&
                        results[0].value.has_value();
                    bool inWrittenSet = false;
                    if (allGood) {
                        const auto& v = results[0].value.value();
                        // ★ 写的是 int16_t；先按类型检查再取，避免 bad_variant_access
                        if (std::holds_alternative<int16_t>(v)) {
                            const int16_t got = std::get<int16_t>(v);
                            inWrittenSet =
                                std::find(kWrittenValues.begin(),
                                          kWrittenValues.end(),
                                          got) != kWrittenValues.end();
                        }
                    }
                    (allGood && inWrittenSet) ? ++ok : ++rejected;
                } else {
                    const int16_t v = kWrittenValues[i / 2];
                    OPC_UA_Client::WriteValue var{node, v};
                    std::vector<OPC_UA_Client::WriteValue> wv{var};
                    auto w = c->batchWrite(wv);
                    if (w.is_success()) {
                        const auto& results = w.value_or({});
                        const bool allGood =
                            results.size() == 1 &&
                            results[0].status ==
                                OPC_UA_Client::WriteResult::Status::Good &&
                            results[0].rawStatus == UA_STATUSCODE_GOOD;
                        allGood ? ++ok : ++rejected;
                    } else {
                        ++rejected;
                    }
                }
            }
        });
    }
    for (auto& t : ts) t.join();

    EXPECT_EQ(ok.load() + rejected.load(), kTotal)
        << "ok=" << ok.load() << " rejected=" << rejected.load();
    EXPECT_GE(ok.load(), kTotal * 99 / 100)
        << "ok=" << ok.load() << " rejected=" << rejected.load();
}
