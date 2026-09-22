// tests/test18.cpp
//
// 生命周期 / 配置快照 / CAS 并发补充（对应审查报告 §6.2 / G4、G5、G6、G9）：
//   T39  endpoint 是值快照：recreate 不读取调用方字符串
//   T40  connect 与 disconnect 并发 → CAS 恰好一个赢，另一个 THREAD_BUSY
//   T41  两个 disconnect 并发 → 恰好一个赢，另一个 THREAD_BUSY
//   T42  recreate 等待在途 API 排空的超时路径 → IN_FLIGHT_TIMEOUT
//
// T40/T41 是确定性并发用例，已加入 TSan 过滤器。

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

#include "OPCUAPacking/detail/ClientTestHooks.h"

using namespace std::chrono_literals;

namespace {
std::unique_ptr<OPC_UA_Client>
makeClient(const std::shared_ptr<MockSdk> &sdk, bool enableWatchdog) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPC_UA_Client::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false,
      enableWatchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}
} // namespace

// ============================================================
// T39: endpoint 值快照 —— 调用方字符串被篡改后 recreate 仍用原值
// ============================================================
TEST(Client, T39_EndpointSnapshot_SurvivesCallerMutation) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  std::string url = "opc.tcp://127.0.0.1:4840";
  const std::string original = url;

  auto r = OPC_UA_Client::createWithSdk(sdk, url, cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(sdk->lastConnectUrl(), original);

  // 篡改调用方的字符串并让其析构语义失效：若 RecreateSync 只保存引用/指针，
  // recreate 会读到被改写的内容。
  url.assign("opc.tcp://192.0.2.1:9");
  { std::string scratch = url; scratch.clear(); }

  c->setGiveUpSignal();
  ASSERT_TRUE(waitFor(
      [&] {
        auto s = c->checkLifeState();
        return s.has_value() &&
               s.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
      },
      5s)) << "未进入 GIVEN_UP";

  sdk->clearLastConnectUrl();
  auto rr = c->recreateGiveUpClient();
  EXPECT_TRUE(rr.is_success())
      << (rr.is_fail() ? rr.get_error()->what() : "");
  EXPECT_EQ(sdk->lastConnectUrl(), original)
      << "recreate 使用了被篡改/悬垂的 endpoint，而不是构造时保存的快照";

  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T40: connect 与 disconnect 的 CAS 竞争
// ============================================================
TEST(Client, T40_ConcurrentConnectVsDisconnect_ExactlyOneWins) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  latch inConnect{1};
  latch releaseConnect{1};
  sdk->setConnectAsyncHook([&] {
    inConnect.count_down();
    releaseConnect.wait();
  });

  std::atomic<bool> connectDone{false};
  std::atomic<bool> connectOk{false};
  std::thread a([&] {
    connectOk.store(c->connect().is_success());
    connectDone.store(true);
  });

  ASSERT_TRUE(waitFor([&] { return inConnect.try_wait(); }, 2s))
      << "connect 未进入 connectAsync 钩子";

  // connect 已持 CAS；disconnect 必须立刻返回 THREAD_BUSY，而不是并行下发。
  auto dr = c->disconnect();
  ASSERT_TRUE(dr.is_fail()) << "connect 进行中 disconnect 竟然成功";
  EXPECT_EQ(dr.get_error()->code(), RichError::ErrorCode::THREAD_BUSY)
      << "context=" << dr.get_error()->what();

  releaseConnect.count_down();
  ASSERT_TRUE(waitFor([&] { return connectDone.load(); }, 5s));
  a.join();

  EXPECT_TRUE(connectOk.load()) << "被放行后的 connect 应成功";
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T41: 两个 disconnect 的 CAS 竞争
// ============================================================
TEST(Client, T41_ConcurrentDisconnect_ExactlyOneWins) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  latch inDisconnect{1};
  latch releaseDisconnect{1};
  sdk->setDisconnectAsyncHook([&] {
    inDisconnect.count_down();
    releaseDisconnect.wait();
  });

  std::atomic<int> ok{0};
  std::atomic<int> busy{0};
  std::atomic<int> other{0};

  auto classify = [&](const Result<Unit, RichError> &res) {
    if (res.is_success()) {
      ok.fetch_add(1, std::memory_order_relaxed);
    } else if (res.get_error()->code() == RichError::ErrorCode::THREAD_BUSY) {
      busy.fetch_add(1, std::memory_order_relaxed);
    } else {
      other.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::thread a([&] { classify(c->disconnect()); });
  ASSERT_TRUE(waitFor([&] { return inDisconnect.try_wait(); }, 2s))
      << "第一个 disconnect 未进入 disconnectAsync 钩子";

  classify(c->disconnect());

  releaseDisconnect.count_down();
  a.join();

  EXPECT_EQ(ok.load(), 1);
  EXPECT_EQ(busy.load(), 1);
  EXPECT_EQ(other.load(), 0);
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T42: recreate 等待在途 API 排空的超时路径
// ============================================================
TEST(Client, T42_RecreateInFlightTimeout) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeClient(sdk, /*enableWatchdog=*/false);
  ASSERT_NE(c, nullptr);

  latch inRead{1};
  latch releaseRead{1};
  sdk->setServiceReadHook([&] {
    inRead.count_down();
    releaseRead.wait();
  });
  // 放行后立刻以非可重试错误返回，避免 retry 退避拖慢收尾。
  sdk->setReadResponseShape(UA_STATUSCODE_BADINTERNALERROR, 0, false);

  std::atomic<bool> readDone{false};
  std::thread reader([&] {
    std::vector<OPC_UA_Client::ReadValue> v{{1, "TestInt", S7DataType::INT}};
    (void)c->batchRead(v);
    readDone.store(true);
  });
  ASSERT_TRUE(waitFor([&] { return inRead.try_wait(); }, 2s))
      << "reader 未进入 serviceRead";

  c->setGiveUpSignal();
  ASSERT_TRUE(waitFor(
      [&] {
        auto s = c->checkLifeState();
        return s.has_value() &&
               s.value_or(LifeState::RUNNING) == LifeState::GIVEN_UP;
      },
      3s)) << "未进入 GIVEN_UP";

  const auto t0 = std::chrono::steady_clock::now();
  auto rr = c->recreateGiveUpClient();
  const auto dt = std::chrono::steady_clock::now() - t0;

  ASSERT_TRUE(rr.is_fail()) << "在途 API 未排空时 recreate 竟成功";
  EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::IN_FLIGHT_TIMEOUT)
      << "context=" << rr.get_error()->what();
  EXPECT_FALSE(ClientTestHooks::getRecreatingStatus(*c))
      << "超时返回后 m_recreating 必须复位";
  EXPECT_GE(dt, 1900ms) << "未等到 2s 排空窗口就返回";
  EXPECT_LT(dt, 5s) << "排空超时后仍长时间阻塞";

  releaseRead.count_down();
  ASSERT_TRUE(waitFor([&] { return readDone.load(); }, 5s));
  reader.join();
  EXPECT_TRUE(c->shutdown().is_success());
}

