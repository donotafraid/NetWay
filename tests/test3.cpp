#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include "tests/MockSdk.h"
#include "tests/util/wait_for.h"
#include "OPCUAPacking/detail/ClientTestHooks.h"

TEST(Client, T3_TransientDisconnectRecoversWithoutGiveUp_Mock) {
    auto sdk = std::make_shared<MockSdk>();
    sdk->setMode(MockSdk::Mode::Connected);
     sdk->setAutoAdvancePerIterateMs(50);   // ★ 关键：让虚拟时钟每次驱动 +50ms

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    cfg.timeoutMs = 200;
    cfg.watchdogIntervalMs = 1000;
    cfg.connectivityCheckIntervalMs = 1000;
    cfg.retryBackoffBaseMs = 50;
    cfg.retryMaxBackoffMs = 100;
    cfg.maxTotalWaitMs = 5000;
    cfg.giveUpThresholdMs = 10000;

    ASSERT_FALSE(cfg.check().has_value())
        << "cfg invalid: " << cfg.check().value();

    auto createResult = OPC_UA_Client::createWithSdk(
        sdk, "opc.tcp://192.0.2.1:4840", cfg, false, false);
    ASSERT_TRUE(createResult.has_value());
    auto client = std::move(*createResult.get());

    //确认连接状态正常
    {
        auto r = client->checkConnected();
        ASSERT_TRUE(r.has_value()) << r.get_error()->what();
        EXPECT_EQ(r.value_or(ConnectionState::OBJECT_ONLY),
                  ConnectionState::CONNECTED);
    }

    //设置断开连接
    sdk->setMode(MockSdk::Mode::Unreachable,false);

    // error 会被记录并通过 << lastError 显式暴露。
    // 判断LifeState状态为特定情况才返回，否则进行延时等待，直到超时
    std::string lastError;
    auto lifeStateIs = [&](LifeState target) {
        auto r = client->checkLifeState();
        if (!r.has_value()) {
            lastError = r.get_error() ? r.get_error()->what() : "unknown";
            return false;
        }
        return r.value_or(LifeState::GIVEN_UP) == target;
    };

    lastError.clear();
    ClientTestHooks::pumpWatchdogForTest(*client);
    bool recovering = waitFor([&]{ return lifeStateIs(LifeState::RECOVERING); },
                              std::chrono::seconds(3));
    EXPECT_TRUE(recovering) << "lastError: " << lastError;
    EXPECT_FALSE(lifeStateIs(LifeState::GIVEN_UP));

    sdk->setMode(MockSdk::Mode::Connected);

    lastError.clear();
    ClientTestHooks::pumpWatchdogForTest(*client);
    bool recovered = waitFor([&]{ return lifeStateIs(LifeState::RUNNING); },
                             std::chrono::seconds(5));
    EXPECT_TRUE(recovered) << "lastError: " << lastError;
    EXPECT_FALSE(lifeStateIs(LifeState::GIVEN_UP));
}