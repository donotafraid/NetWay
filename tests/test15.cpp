// tests/test13.cpp
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include "OPCUAPacking/ua.h"
#include "tests/MockSdk.h"

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    include <sanitizer/lsan_interface.h>
#    define OPCUA_LSAN_CHECK() __lsan_do_recoverable_leak_check()
#  else
#    define OPCUA_LSAN_CHECK() do {} while (0)
#  endif
#else
#  define OPCUA_LSAN_CHECK() do {} while (0)
#endif

TEST(Client, T28_MalformedReadResponse_NoLeak) {
    auto sdk = std::make_shared<MockSdk>();
    sdk->setMode(MockSdk::Mode::Connected);

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

    auto r = OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
    ASSERT_TRUE(r.has_value()) << r.get_error()->what();
    auto c = std::move(*r.get());

    std::vector<OPC_UA_Client::ReadValue> v{{{1, "TestInt", S7DataType::INT}}};

    // 出口 1：GOOD 但 resultsSize=0，results=null → 走 !response.results 分支
    {
        sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/0,
                                  /*allocResults=*/false);
        auto rr = c->batchRead(v);
        EXPECT_TRUE(rr.is_fail())
            << "GOOD 但 results=null 必须判失败，不能伪造成功";
        OPCUA_LSAN_CHECK();   // 就地裁决，不等进程退出
    }

    // 出口 2：GOOD 但 resultsSize=3 > 请求数 1 → 走 size 不匹配分支
    {
        sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/3,
                                  /*allocResults=*/true);
        auto rr = c->batchRead(v);
        EXPECT_TRUE(rr.is_fail())
            << "GOOD 但结果数量多于请求必须判失败";
        OPCUA_LSAN_CHECK();
    }

    // 出口 3：非可重试错误 + results=null → 走 else 分支
    {
        sdk->setReadResponseShape(UA_STATUSCODE_BADINTERNALERROR,
                                  /*resultsSize=*/0, /*allocResults=*/false);
        auto rr = c->batchRead(v);
        EXPECT_TRUE(rr.is_fail());
        OPCUA_LSAN_CHECK();
    }

    // 出口 4：非可重试错误 + results 已分配（Guard 必须同时清理 results）
    {
        sdk->setReadResponseShape(UA_STATUSCODE_BADINTERNALERROR,
                                  /*resultsSize=*/1, /*allocResults=*/true);
        auto rr = c->batchRead(v);
        EXPECT_TRUE(rr.is_fail());
        OPCUA_LSAN_CHECK();
    }

    sdk->resetResponseShape();
    EXPECT_TRUE(c->shutdown().is_success());
}


TEST(Client, T29_PerItemBadAndTypeMismatch_MapToNullopt) {
    auto sdk = std::make_shared<MockSdk>();
    sdk->setMode(MockSdk::Mode::Connected);

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

    auto r = OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
    ASSERT_TRUE(r.has_value()) << r.get_error()->what();
    auto c = std::move(*r.get());

    // 两节点：第 0 项 GOOD + INT16=7；第 1 项 BADNOTREADABLE
    sdk->setReadItemStatus(0, UA_STATUSCODE_GOOD);
    sdk->setReadItemInt16(0, 7);
    sdk->setReadItemStatus(1, UA_STATUSCODE_BADNOTREADABLE);
    sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/2,
                              /*allocResults=*/true);

    std::vector<OPC_UA_Client::ReadValue> v{
        {1, "A", S7DataType::INT},
        {1, "B", S7DataType::INT}};
    auto rr = c->batchRead(v);
    ASSERT_TRUE(rr.is_success()) << rr.get_error()->what();

    const auto& out = rr.value_or({});
    ASSERT_EQ(out.size(), 2u);

    // 第 0 项：GOOD + value 有值 + 值正确
    EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
    ASSERT_TRUE(out[0].value.has_value())
        << "GOOD + INT16 应解析出 value";
    EXPECT_EQ(std::get<int16_t>(*out[0].value), 7);

    // 第 1 项：BAD + value 必须为 nullopt（不得崩、不得伪造值）
    EXPECT_EQ(out[1].rawStatus, UA_STATUSCODE_BADNOTREADABLE);
    EXPECT_FALSE(out[1].value.has_value())
        << "按项 BAD 必须映射为 nullopt，而不是空 variant";

    // 类型不匹配：请求 UDINT，服务端返回 INT16
    // 期望：rawStatus 仍为 GOOD（服务层成功），但 value=nullopt（转换失败）
    sdk->setReadItemStatus(0, UA_STATUSCODE_GOOD);
    sdk->setReadItemInt16(0, 7);
    sdk->setReadResponseShape(UA_STATUSCODE_GOOD, 1, true);

    std::vector<OPC_UA_Client::ReadValue> v2{{1, "A", S7DataType::UDINT}};
    auto rr2 = c->batchRead(v2);
    ASSERT_TRUE(rr2.is_success()) << rr2.get_error()->what();
    const auto& out2 = rr2.value_or({});
    ASSERT_EQ(out2.size(), 1u);
    EXPECT_EQ(out2[0].rawStatus, UA_STATUSCODE_GOOD);
    EXPECT_FALSE(out2[0].value.has_value())
        << "类型不匹配必须映射为 nullopt，而不是 UB 或伪造值";

    OPCUA_LSAN_CHECK();   // 按项 UA_Variant 的分配必须被 clear

    sdk->resetResponseShape();
    EXPECT_TRUE(c->shutdown().is_success());
}


TEST(Client, T30_MalformedWriteResponse_Rejected) {
    auto sdk = std::make_shared<MockSdk>();
    sdk->setMode(MockSdk::Mode::Connected);

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

    auto r = OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
    ASSERT_TRUE(r.has_value()) << r.get_error()->what();
    auto c = std::move(*r.get());

    OPC_UA_Client::WriteValue wv{{1, "TestInt", S7DataType::INT}, int16_t(1)};
    std::vector<OPC_UA_Client::WriteValue> v{wv};

    // 出口 1：GOOD 但 resultsSize=0 → 必须判失败，不能伪造成功
    {
        sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/0);
        auto rr = c->batchWrite(v);
        EXPECT_TRUE(rr.is_fail())
            << "GOOD 但结果数量为 0 必须判失败，不能伪造成功";
        OPCUA_LSAN_CHECK();
    }

    // 出口 2：GOOD 但 resultsSize=3 > 请求数 1 → 必须判失败
    {
        sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/3);
        auto rr = c->batchWrite(v);
        EXPECT_TRUE(rr.is_fail())
            << "GOOD 但结果数量多于请求必须判失败";
        OPCUA_LSAN_CHECK();
    }

    // 出口 3：非 GOOD + results=0 → 正常错误路径，Guard 必须清理
    {
        sdk->setWriteResponseShape(UA_STATUSCODE_BADINTERNALERROR,
                                   /*resultsSize=*/0);
        auto rr = c->batchWrite(v);
        EXPECT_TRUE(rr.is_fail());
        OPCUA_LSAN_CHECK();
    }

    // 出口 4：非 GOOD + results 已分配 → Guard 必须清理 results 数组
    {
        sdk->setWriteResponseShape(UA_STATUSCODE_BADINTERNALERROR,
                                   /*resultsSize=*/1);
        auto rr = c->batchWrite(v);
        EXPECT_TRUE(rr.is_fail());
        OPCUA_LSAN_CHECK();
    }

    sdk->resetResponseShape();
    EXPECT_TRUE(c->shutdown().is_success());
}