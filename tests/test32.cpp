
// tests/test32.cpp
//
// 补充用例：把 test1~test31 仍未钉死的 7 条契约补上。
//   T103 isHealthy() 必须是只读缓存（0 次 SDK 调用）；checkConnected() 必须驱动事件循环。
//   T104 GIVEN_UP 后所有入口快速失败，且 0 次 SDK 调用（含 connect/disconnect）。
//   T105 RECOVERING 期间 batchRead/batchWrite 快速失败，且 0 次 service 调用。
//   T106 写响应 resultsSize>0 且 results==nullptr：拒绝、不崩、不漏。
//   T107 ReadResult::errorCode 对 GOOD / 按项 BAD / 类型不匹配三种出口都确定。
//   T108 并发健康查询 × 读写（TSan 过滤器已登记）。
//   T109 ClientConfig::check() 的 10 条未覆盖分支：必须被拒且 applyDefaultIfInvalid() 可修复。
//
// 期望的运行方式：
//   ctest --test-dir build            （功能）
//   ./scripts/run_asan.sh             （T106/T107/T109 由 LSan/ASan 裁决）
//   ./scripts/run_tsan.sh             （T108 由 TSan 裁决）

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "src/OPCUAPacking/detail/RealSdk.h"
#include "tests/include/ClientTestHooks.h"
#include "tests/include/MockSdk.h"
#include "tests/include/util/Cxx17Compat.h"
#include "tests/include/util/wait_for.h"

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    include <sanitizer/lsan_interface.h>
#    define OPCUA_LSAN_CHECK() __lsan_do_recoverable_leak_check()
#  else
#    define OPCUA_LSAN_CHECK() do {} while (0)
#  endif
#elif defined(__SANITIZE_ADDRESS__)
#  include <sanitizer/lsan_interface.h>
#  define OPCUA_LSAN_CHECK() __lsan_do_recoverable_leak_check()
#else
#  define OPCUA_LSAN_CHECK() do {} while (0)
#endif

using namespace std::chrono_literals;

namespace {

// 关掉看门狗，让 runIterate/service 计数完全由被测调用驱动，断言才确定。
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

} // namespace

// ============================================================
// T103: isHealthy() 是只读缓存，不得驱动事件循环 / 不得发起服务调用；
//       checkConnected() 按契约必须主动驱动一次事件循环。
// ua.h:61-66 明确把 isHealthy() 定位成高频只读健康检查。
// ============================================================
TEST(ClientExtra, T103_IsHealthy_IsCached_DoesNotDriveSdk) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);

  const unsigned iter0 = sdk->runIterateCallCount();
  const unsigned read0 = sdk->serviceReadCallCount();
  const unsigned write0 = sdk->serviceWriteCallCount();

  for (int i = 0; i < 5; ++i) {
    auto r = c->isHealthy();
    ASSERT_TRUE(r.is_success()) << "健康三元组必须报 success";
  }
  EXPECT_EQ(sdk->runIterateCallCount(), iter0)
      << "isHealthy() 是只读缓存，不得驱动 UA_Client_run_iterate";
  EXPECT_EQ(sdk->serviceReadCallCount(), read0);
  EXPECT_EQ(sdk->serviceWriteCallCount(), write0);

  auto cc = c->checkConnected();
  ASSERT_TRUE(cc.is_success());
  EXPECT_GE(sdk->runIterateCallCount(), iter0 + 1u)
      << "checkConnected() 的契约要求主动驱动一次事件循环";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T104: GIVEN_UP 之后所有公共入口必须快速失败，且不得触达 SDK。
// 覆盖 T20/T57 未覆盖的 connect()/disconnect() 两个入口与“零 SDK 调用”断言。
// ============================================================
TEST(ClientExtra, T104_GiveUp_FastFailsAllEntrypoints_WithoutSdkCalls) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);

  ASSERT_TRUE(c->setGiveUpSignal().is_success());
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::GIVEN_UP);

  const unsigned ca0 = sdk->connectAsyncCallCount();
  const unsigned da0 = sdk->disconnectAsyncCallCount();
  const unsigned sd0 = sdk->syncDisconnectCallCount();
  const unsigned r0 = sdk->serviceReadCallCount();
  const unsigned w0 = sdk->serviceWriteCallCount();

  auto br = c->batchRead({{1, "N", S7DataType::INT}});
  auto bw = c->batchWrite({{{1, "N", S7DataType::INT}, int16_t(1)}});
  auto cc = c->checkConnected();
  auto hi = c->isHealthy();
  auto co = c->connect();
  auto dc = c->disconnect();

  ASSERT_TRUE(br.is_fail());
  ASSERT_TRUE(bw.is_fail());
  ASSERT_TRUE(cc.is_fail());
  ASSERT_TRUE(hi.is_fail());
  ASSERT_TRUE(co.is_fail());
  ASSERT_TRUE(dc.is_fail());

  EXPECT_EQ(br.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);
  EXPECT_EQ(bw.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);
  EXPECT_EQ(cc.get_error()->code(), RichError::ErrorCode::NOT_CONNECTED);
  EXPECT_EQ(hi.get_error()->code(), RichError::ErrorCode::NOT_CONNECTED);
  EXPECT_EQ(co.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);
  EXPECT_EQ(dc.get_error()->code(), RichError::ErrorCode::ALREADY_TERMINATED);

  EXPECT_EQ(sdk->connectAsyncCallCount(), ca0);
  EXPECT_EQ(sdk->disconnectAsyncCallCount(), da0);
  EXPECT_EQ(sdk->syncDisconnectCallCount(), sd0);
  EXPECT_EQ(sdk->serviceReadCallCount(), r0);
  EXPECT_EQ(sdk->serviceWriteCallCount(), w0);

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T105: RECOVERING 期间 batchRead/batchWrite 必须快速失败且不触达 service。
// 通过“通道 OPEN  会话 CLOSED”让 doWatchdogDecision 走 !isChannelClosed()
// 分支，确定性地把 lifeState 推入 RECOVERING（无需睡眠）。
// ============================================================
TEST(ClientExtra, T105_Recovering_FastFailsReadWrite_WithoutServiceCalls) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(1000); // 加这一行
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->isHealthy().is_success());

  // 通道 OPEN、会话 CLOSED：不是“通道关闭”，看门狗直接判定 RECOVERING。
  sdk->setMode(MockSdk::Mode::ChannelOpenSessionClosed, /*allowAuto=*/false);
  OPCUAPacking::test::ClientTestHooks::pumpWatchdogForTest(*c);

  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::RECOVERING)
      << "未进入 RECOVERING，无法验证快速失败契约";

  const unsigned r0 = sdk->serviceReadCallCount();
  const unsigned w0 = sdk->serviceWriteCallCount();

  auto br = c->batchRead({{1, "N", S7DataType::INT}});
  auto bw = c->batchWrite({{{1, "N", S7DataType::INT}, int16_t(1)}});

  ASSERT_TRUE(br.is_fail());
  ASSERT_TRUE(bw.is_fail());
  EXPECT_EQ(br.get_error()->code(), RichError::ErrorCode::NOT_CONNECTED);
  EXPECT_EQ(bw.get_error()->code(), RichError::ErrorCode::NOT_CONNECTED);
  EXPECT_EQ(sdk->serviceReadCallCount(), r0)
      << "RECOVERING 期间读必须快速失败，不得下发 SDK";
  EXPECT_EQ(sdk->serviceWriteCallCount(), w0)
      << "RECOVERING 期间写必须快速失败，不得下发 SDK";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T106: 写响应的畸形出口 —— resultsSize>0 且 results==nullptr。
// 与读路径 T97 对称；同时钉死“WriteResponseGuard 不会空指针解引用”。
// ============================================================
TEST(ClientExtra, T106_WriteResponse_NullResultsWithMatchingSize_Rejected_NoLeak) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setWriteResponseNullResults(/*resultsSize=*/1);
  auto r = c->batchWrite({{{1, "N", S7DataType::INT}, int16_t(1)}});
  ASSERT_TRUE(r.is_fail());
  EXPECT_EQ(r.get_error()->code(), RichError::ErrorCode::SERVICE_FAILED)
      << "resultsSize>0 且 results==nullptr 必须走独立失败出口";
  OPCUA_LSAN_CHECK();

  // Guard 不得破坏后续正常写入。
  sdk->resetResponseShape();
  auto ok = c->batchWrite({{{1, "N", S7DataType::INT}, int16_t(1)}});
  EXPECT_TRUE(ok.is_success());

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T107: ReadResult::errorCode 对三种逐项出口都确定。
// ============================================================
TEST(ClientExtra, T107_ReadResult_ErrorCode_IsDeterministicPerOutcome) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);

  // 两项：0 = GOOD  INT16=7；1 = 按项 BAD。
  sdk->setReadItemStatus(0, UA_STATUSCODE_GOOD);
  sdk->setReadItemInt16(0, 7);
  sdk->setReadItemStatus(1, UA_STATUSCODE_BADNOTREADABLE);
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/2,
                            /*allocResults=*/true);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT},
                                          {1, "B", S7DataType::INT}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_success()) << rr.get_error()->what();
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::OK);
  EXPECT_EQ(out[1].errorCode, RichError::ErrorCode::SDK_ERROR);
  EXPECT_FALSE(out[1].value.has_value());

  // 类型不匹配：请求 UDINT，服务端返回 INT16（服务层 GOOD）。
  sdk->resetResponseShape();
  sdk->setReadItemStatus(0, UA_STATUSCODE_GOOD);
  sdk->setReadItemInt16(0, 7);
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  auto rr2 = c->batchRead({{1, "A", S7DataType::UDINT}});
  ASSERT_TRUE(rr2.is_success()) << rr2.get_error()->what();
  const auto &out2 = rr2.value_or({});
  ASSERT_EQ(out2.size(), 1u);
  EXPECT_EQ(out2[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_EQ(out2[0].errorCode, RichError::ErrorCode::TYPE_MISMATCH);
  EXPECT_FALSE(out2[0].value.has_value());

  OPCUA_LSAN_CHECK();
  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T108: isHealthy()/checkLifeState() 高频并发查询 × batchRead/batchWrite。
// 价值由 TSan 提供（scripts/run_tsan.sh 已登记 ClientExtra.T108_*）。
// ============================================================
TEST(ClientExtra, T108_ConcurrentHealthQueries_DuringReadWrite_TSan) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClientNoWatchdog(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16(0, 42);

  std::atomic<bool> stop{false};
  std::atomic<unsigned> healthyOk{0}, readOk{0}, writeOk{0};

  std::vector<JThread> threads;
  for (int t = 0; t < 2; ++t) {
    threads.emplace_back([&] {
      while (!stop.load(std::memory_order_acquire)) {
        if (c->isHealthy().is_success())
          healthyOk.fetch_add(1, std::memory_order_relaxed);
        (void)c->checkLifeState();
      }
    });
  }
  for (int t = 0; t < 2; ++t) {
    threads.emplace_back([&] {
      while (!stop.load(std::memory_order_acquire)) {
        std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT}};
        auto rr = c->batchRead(v);
        const auto &o = rr.value_or({});
        if (rr.is_success() && o.size() == 1 && o[0].value.has_value() &&
            std::holds_alternative<int16_t>(*o[0].value) &&
            std::get<int16_t>(*o[0].value) == 42)
          readOk.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  threads.emplace_back([&] {
    while (!stop.load(std::memory_order_acquire)) {
      std::vector<OPC_UA_Client::WriteValue> v{
          {{1, "A", S7DataType::INT}, int16_t(42)}};
      if (c->batchWrite(v).is_success())
        writeOk.fetch_add(1, std::memory_order_relaxed);
    }
  });

  std::this_thread::sleep_for(80ms);
  stop.store(true, std::memory_order_release);
  for (auto &th : threads)
    th.join();

  EXPECT_GT(healthyOk.load(), 0u);
  EXPECT_GT(readOk.load(), 0u);
  EXPECT_GT(writeOk.load(), 0u);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T109: ClientConfig::check() 的 10 条未覆盖分支。
// 每条非法配置都必须被拒；applyDefaultIfInvalid() 必须能修好。
// ============================================================
TEST(ClientConfigTest, T109_Check_CoversRemainingBranches_AndRepairable) {
  using Mutator = std::function<void(ClientConfig &)>;
  const std::vector<Mutator> bad = {
      [](ClientConfig &c) { c.watchdogIntervalMs = c.sessionTimeoutMs; },
      [](ClientConfig &c) { c.retryBackoffBaseMs = 0; },
      [](ClientConfig &c) { c.connectivityCheckIntervalMs = c.sessionTimeoutMs; },
      [](ClientConfig &c) { c.retryBackoffBaseMs = c.sessionTimeoutMs; },
      [](ClientConfig &c) { c.timeoutMs = c.sessionTimeoutMs; },
      [](ClientConfig &c) { c.retryMaxBackoffMs = c.timeoutMs; },
      [](ClientConfig &c) { c.giveUpThresholdMs = 0; },
      [](ClientConfig &c) { c.giveUpThresholdMs = c.sessionTimeoutMs; },
      [](ClientConfig &c) { c.maxTotalWaitMs = c.sessionTimeoutMs; },
      [](ClientConfig &c) {
        c.maxTotalWaitMs = c.timeoutMs + c.retryMaxBackoffMs - 1;
      },
      [](ClientConfig &c) { c.maxTotalWaitMs = 0; },
  };

  for (size_t i = 0; i < bad.size(); ++i) {
    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
    bad[i](cfg);

    ASSERT_TRUE(cfg.check().has_value()) << "case " << i << " 未被拒绝";

    auto repaired = cfg.applyDefaultIfInvalid();
    ASSERT_TRUE(repaired.is_success()) << "case " << i;
    EXPECT_TRUE(repaired.value_or(false)) << "case " << i << " 应报告发生了修复";
    EXPECT_FALSE(cfg.check().has_value())
        << "case " << i << " applyDefaultIfInvalid() 后仍非法";
  }
}
