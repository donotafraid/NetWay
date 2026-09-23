// tests/test16.cpp
// 目标：把 checkConnected() 对“连接不健康”的语义固定下来，
//       并顺手覆盖 MockSdk::enqueueState / Mode::Disconnected 这两条此前未用过的接缝。

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include "OPCUAPacking/ua.h"
#include "tests/MockSdk.h"
#include "tests/util/wait_for.h"

#include "OPCUAPacking/internal/ClientTestHooks.h"


// ---------------------------------------------------------------------------
// T31：连接不健康时，checkConnected() 必须降级为 OBJECT_ONLY。
//      当前实现（P1-3）会把 success(false) 误判为 CONNECTED。
// ---------------------------------------------------------------------------
TEST(Client, T31_CheckConnected_MustNotReportConnectedWhenUnhealthy) {
    auto sdk = std::make_shared<MockSdk>();

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
    ASSERT_TRUE(r.has_value());
    auto c = std::move(*r.get());
    ASSERT_TRUE(c != nullptr);

    // 健康基线：CONNECTED
    {
        auto s = c->checkConnected();
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s.value_or(ConnectionState::OBJECT_ONLY),
                  ConnectionState::CONNECTED);
    }

    // 切到不可达：看门狗应把内部连接状态推进到不健康。
    sdk->setMode(MockSdk::Mode::Unreachable, /*allowAuto=*/false);

    std::string lastErr;
    bool becameObjectOnly = waitFor([&] {
        auto s = c->checkConnected();
        if (!s.has_value()) {
            lastErr = s.get_error() ? s.get_error()->what() : "<no error>";
            return false;
        }
        return s.value_or(ConnectionState::CONNECTED) ==
               ConnectionState::OBJECT_ONLY;
    }, std::chrono::milliseconds(3000));

    EXPECT_TRUE(becameObjectOnly)
        << "checkConnected 仍报 CONNECTED，last error=" << lastErr;

    EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T32：显式 disconnect() 之后，checkConnected() 必须返回 OBJECT_ONLY，
//      而不是 CONNECTED，也不是错误。
// ---------------------------------------------------------------------------
TEST(Client, T32_CheckConnected_AfterDisconnect_IsObjectOnly) {
    auto sdk = std::make_shared<MockSdk>();

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
    ASSERT_TRUE(r.has_value());
    auto c = std::move(*r.get());
    ASSERT_TRUE(c != nullptr);

    {
        auto s = c->checkConnected();
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s.value_or(ConnectionState::OBJECT_ONLY),
                  ConnectionState::CONNECTED);
    }

    ASSERT_TRUE(c->disconnect().is_success());

    auto s = c->checkConnected();
    ASSERT_TRUE(s.has_value())
        << (s.get_error() ? s.get_error()->what() : "<no error>");
    EXPECT_EQ(s.value_or(ConnectionState::CONNECTED),
              ConnectionState::OBJECT_ONLY);

    EXPECT_TRUE(c->shutdown().is_success());
}

// ---------------------------------------------------------------------------
// T33：通道已打开但会话未激活时，checkConnected() 必须返回 OBJECT_ONLY。
//      覆盖 MockSdk::enqueueState 与 Mode::ChannelOpenSessionClosed。
// ---------------------------------------------------------------------------
TEST(Client, T33_CheckConnected_ChannelOpenSessionClosed_IsObjectOnly) {
    auto sdk = std::make_shared<MockSdk>();

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
    ASSERT_TRUE(r.has_value());
    auto c = std::move(*r.get());
    ASSERT_TRUE(c != nullptr);

    {
        auto s = c->checkConnected();
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s.value_or(ConnectionState::OBJECT_ONLY),
                  ConnectionState::CONNECTED);
    }

    // 下一次 getState 返回：通道 OPEN，会话 CLOSED。
    sdk->enqueueState(UA_STATUSCODE_GOOD,
                      UA_SECURECHANNELSTATE_OPEN,
                      UA_SESSIONSTATE_CLOSED);

    std::string lastErr;
    bool becameObjectOnly = waitFor([&] {
        auto s = c->checkConnected();
        if (!s.has_value()) {
            lastErr = s.get_error() ? s.get_error()->what() : "<no error>";
            return false;
        }
        return s.value_or(ConnectionState::CONNECTED) ==
               ConnectionState::OBJECT_ONLY;
    }, std::chrono::milliseconds(3000));

    EXPECT_TRUE(becameObjectOnly)
        << "通道开、会话未激活时仍报 CONNECTED，last error=" << lastErr;

    EXPECT_TRUE(c->shutdown().is_success());
}