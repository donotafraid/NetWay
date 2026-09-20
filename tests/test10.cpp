// tests/test10_lifecycle_contracts.cpp
//
// 报告 §6.2：补齐"代码自己写下的契约"——在途读写 × shutdown / recreate、
// giveUpThresholdMs 的阈值路径。全部基于 MockSdk 的 4 个接缝，确定性可复现。
//
// T16: shutdown 与在途读的排空契约（正常路径）
// T17: shutdown 超时后强制清理（在途被阻塞）
// T18: recreate 与在途读并发（旧 Impl 优雅失败、新 Impl 可用）
// T19: giveUpThresholdMs 的确定性边界（虚拟时钟）

#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include "tests/MockSdk.h"
#include "tests/util/Cxx17Compat.h"
#include "tests/util/wait_for.h"

using namespace std::chrono_literals;

// ============================================================
// T16: shutdown 必须等在途读返回（正常路径）
// ============================================================
TEST(Client, T16_ShutdownWaitsForInFlightRead_NoUAF) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50); // ★ 关键：让虚拟时钟每次驱动 +50ms
  sdk->setReadDelayMs(300);                 // serviceRead 慢 300ms 返回 GOOD

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  latch entered{1};
  sdk->setServiceReadHook([&] { entered.count_down(); });
  // 不设 release，让 serviceRead 自己 sleep 完

  std::atomic<bool> readDone{false};
  std::thread reader([&]{
    std::vector<OPC_UA_Client::ReadValue> v{{{1, "TestInt", S7DataType::INT}}};
    (void)c->batchRead(v);                  // 结果可为 error（stop 信号）
    readDone = true;
  });
  entered.wait();                          // ← 精确等到 reader 进入 serviceRead
  auto t0 = std::chrono::steady_clock::now();
  ASSERT_TRUE(c->shutdown().is_success());
  
  auto dt = std::chrono::steady_clock::now() - t0;
  EXPECT_GE(dt, 250ms);                     // 确实等了在途读
  EXPECT_TRUE(readDone.load());
  reader.join();
}

// ============================================================
// T17: shutdown 超时后强制清理（在途被永久阻塞）
// ============================================================
TEST(Client, T17_ShutdownTimeout_ForceCleanup_NoUAF) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 2000;          // >= retryMaxBackoffMs
  cfg.watchdogIntervalMs = 1000; // [1000, 30000]
  cfg.retryBackoffBaseMs = 50;
  cfg.retryMaxBackoffMs = 100; // < timeoutMs

  auto r = OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  latch entered{1}, release{1};
  sdk->setServiceReadHook([&]{ entered.count_down(); release.wait(); });

  std::atomic<bool> readReturned{false};
  std::thread reader([&]{
    std::vector<OPC_UA_Client::ReadValue> v{{{1, "TestInt", S7DataType::INT}}};
    (void)c->batchRead(v);
    readReturned = true;
  });
  entered.wait();

  auto t0 = std::chrono::steady_clock::now();
  ASSERT_TRUE(c->shutdown().is_success());
  auto dt = std::chrono::steady_clock::now() - t0;

  // ★ cfg.timeoutMs 是 uint32_t（毫秒数值），显式转成 milliseconds
  const auto timeout = std::chrono::milliseconds(cfg.timeoutMs);

  // 关键断言：至少等到了超时，且没有无限等
  EXPECT_GE(dt, timeout) // 确实等到了超时
      << "shutdown 未等到超时上限就返回，可能提前放弃";
  EXPECT_LT(dt, timeout + 2s) // 有界：超时后 2s 内必须返回
      << "shutdown 超时后仍未返回，疑似无界等待";

  release.count_down();
  reader.join();
}

// ============================================================
// T18: recreate 与在途读并发
// ============================================================
TEST(Client, T18_RecreateVsInFlightRead_OldFailsNewUsable) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  latch inRead{1}, releaseRead{1};
  latch recreatorEnteredWait{1};   // 新增

  sdk->setServiceReadHook([&] {
    inRead.count_down(); // ① 通知主线程：reader 已进入
    releaseRead.wait();  // ② 阻塞，直到主线程 count_down
  });

  std::atomic<bool> oldReadFailed{false};
  JThread reader([&] {
    std::vector<OPC_UA_Client::ReadValue> v{{{1, "TestInt", S7DataType::INT}}};
    oldReadFailed = c->batchRead(v).is_fail();
  });
  inRead.wait();

  c->setGiveUpSignal();
  ASSERT_TRUE(waitFor(
      [&] {
        auto s = c->checkLifeState();
        return s.has_value() &&
               s.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
      },
      5s))
      << "未进入 GIVEN_UP，无法调用 recreateGiveUpClient()";

  std::atomic<bool> recreateOk{false};
  std::atomic<bool> recreateReturned{false};
  JThread recreator([&] {
    auto rr = c->recreateGiveUpClient();
    recreateOk.store(rr.is_success(), std::memory_order_release);
     recreateReturned.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // ★ 关键断言：如果 recreator 已经返回，说明它没等在途 reader，
  //   那就不是"并发"，测试语义失效。
  ASSERT_FALSE(recreateReturned.load(std::memory_order_acquire))
      << "recreator 在放行 reader 前就返回了——它没真正等在途读，"
         "T18 未测到并发场景";

  releaseRead.count_down();

  recreator.join();
  reader.join();

  ASSERT_TRUE(recreateOk.load()) << "recreate 失败";
  EXPECT_TRUE(oldReadFailed.load()) << "旧读应优雅失败";

  // ---------- 关键两步 ----------
  sdk->setServiceReadHook(nullptr);                  // ① 清 hook
  sdk->enqueueReadServiceResult(UA_STATUSCODE_GOOD); // ② 让下一次读成功

  std::vector<OPC_UA_Client::ReadValue> v2{{{1, "TestInt", S7DataType::INT}}};
  auto rr2 = c->batchRead(v2);
  EXPECT_TRUE(rr2.is_success())
      << (rr2.is_fail() ? rr2.get_error()->what() : "");
}



// ============================================================
// T19: giveUpThresholdMs 的确定性边界（虚拟时钟）
// ============================================================
TEST(Client, T19_GiveUp_ThresholdBoundary_VirtualClock) {
  auto sdk = std::make_shared<MockSdk>();

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  cfg.timeoutMs = 500;
  cfg.watchdogIntervalMs = 1000;
  cfg.connectivityCheckIntervalMs = 1000;
  cfg.retryBackoffBaseMs = 100;
  cfg.retryMaxBackoffMs = 400;
  cfg.maxTotalWaitMs = 30000;
  cfg.giveUpThresholdMs = 5000;
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  sdk->setMode(MockSdk::Mode::Connected);
  auto r = OPC_UA_Client::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  // ① 让看门狗稳定在 RUNNING（此时时钟未冻结）
  ASSERT_TRUE(waitFor([&]{
    auto s = c->checkLifeState();
    return s.has_value() &&
           s.value_or(LifeState::GIVEN_UP) == LifeState::RUNNING;
  }, 3s)) << "看门狗未在 3s 内进入 RUNNING";

  // ② 让看门狗能自行推进时钟
  sdk->setAutoAdvancePerIterateMs(50);

  // ③ 切到不健康，等看门狗进入 RECOVERING
  sdk->setMode(MockSdk::Mode::Unreachable,false);
  ASSERT_TRUE(waitFor([&]{
    auto s = c->checkLifeState();
    return s.has_value() &&
           s.value_or(LifeState::RUNNING) == LifeState::RECOVERING;
  }, 3s)) << "看门狗未在 3s 内进入 RECOVERING";

  // ④ 基准
  const int64_t t0 = sdk->nowMs();

  // ⑤ 冻结时钟，避免看门狗在下面检查窗口内继续推进
  sdk->setAutoAdvancePerIterateMs(0);

  // ⑥ slack 必须 > 一个 runIterate 步长（50ms）+ connect 时间，
  //    取 1000ms 足够覆盖所有调度抖动
  constexpr int64_t kSlack = 1000;

  // ---- 边界前：安全地落在阈值之前 ----
  sdk->setNowMs(t0 + cfg.giveUpThresholdMs - kSlack);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  auto s1 = c->checkLifeState();
  ASSERT_TRUE(s1.has_value()) << s1.get_error()->what();
  EXPECT_NE(s1.value_or(LifeState::GIVEN_UP), LifeState::GIVEN_UP)
      << "nowMs = t0+" << (cfg.giveUpThresholdMs - kSlack)
      << " 时不应 GIVEN_UP";

  // ---- 边界后：安全地越过阈值 ----
  sdk->setNowMs(t0 + cfg.giveUpThresholdMs + kSlack);
  ASSERT_TRUE(waitFor([&]{
    auto s = c->checkLifeState();
    return s.has_value() &&
           s.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
  }, 5s)) << "nowMs = t0+" << (cfg.giveUpThresholdMs + kSlack)
          << " 后仍未 GIVEN_UP";
}
