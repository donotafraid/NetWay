#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include "tests/MockSdk.h"
#include <chrono>
#include "OPCUAPacking/internal/ClientTestHooks.h"

TEST(Client, T2_ConnectFailureDrivesRunIterate) {
    auto sdk = std::make_shared<MockSdk>();
    sdk->setMode(MockSdk::Mode::Unreachable,false);
    sdk->setAutoAdvancePerIterateMs(50);   // 每次驱动 +50ms

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    cfg.timeoutMs          = 200;
    cfg.retryBackoffBaseMs = 50;
    cfg.retryMaxBackoffMs  = 100;
    cfg.maxTotalWaitMs     = 5000;
    ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

    const int64_t  t0     = sdk->nowMs();
    const unsigned before = sdk->runIterateCallCount();

    auto wall0 = std::chrono::steady_clock::now();
    auto createResult =
        ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
    auto wall1 = std::chrono::steady_clock::now();

    const int64_t  dt    = sdk->nowMs() - t0;
    const unsigned calls = sdk->runIterateCallCount() - before;

    EXPECT_FALSE(createResult.has_value());

    // 判据 1（虚拟时钟，确定性）：推进量 ≈ timeoutMs
    EXPECT_GE(dt, cfg.timeoutMs);
    EXPECT_LE(dt, cfg.timeoutMs + 200);   // 单步粒度容差

    // 判据 2（契约）：循环次数 ≈ 推进量 / 单步
    EXPECT_GT(calls, 0u) << "首连没有驱动事件循环";
    EXPECT_NEAR(static_cast<int64_t>(calls) * sdk->autoAdvanceMs(),
                dt, sdk->autoAdvanceMs())
        << "循环次数与虚拟时间推进量不成比例，疑似忙等";

    // 判据 3（弱护栏）：墙钟只防 CI 挂死，不作逻辑判据
    auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      wall1 - wall0).count();
    EXPECT_LT(wallMs, 5000) << "墙钟超过 5s，疑似 CI 挂死";
}