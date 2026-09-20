#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include <thread>
#include "tests/util/ClientTestFixture.h"
#include "tests/MockSdk.h"
#include "tests/util/Cxx17Compat.h"   // §6.4: latch 由它提供
#include <future>

// ==================== TA2: 并发 connect 确定性单飞 ====================


// ==================== TC: shutdown 并发 + 幂等 + 副作用 ====================
TEST(Client, TC_ConcurrentShutdownIdempotent) {
    auto sdk = std::make_shared<MockSdk>();
    sdk->setMode(MockSdk::Mode::Connected);

    ClientConfig cfg;
    cfg.applyProfile(ClientConfig::PROFILE_LOCAL);

    auto createResult = OPC_UA_Client::createWithSdk(
        sdk, "opc.tcp://192.0.2.1:4840", cfg);
    ASSERT_TRUE(createResult.has_value());
    auto client = std::move(*createResult.get());

    // P2: 记录 shutdown 前的线程基线
    std::atomic<int> success{0}, error{0};
    std::thread a([&]{ client->shutdown().is_success() ? ++success : ++error; });
    std::thread b([&]{ client->shutdown().is_success() ? ++success : ++error; });
    a.join(); b.join();

    EXPECT_EQ(success.load(), 2);
    EXPECT_EQ(error.load(), 0);

    // P2: 副作用——第三次 shutdown 仍幂等成功
    EXPECT_TRUE(client->shutdown().is_success())
        << "第三次 shutdown 应幂等成功";

    // P2: 副作用——shutdown 后 checkConnected 必须返回失败或非 CONNECTED
    auto conn = client->checkConnected();
    if (conn.has_value()) {
        EXPECT_NE(conn.value_or(ConnectionState::OBJECT_ONLY),
                  ConnectionState::CONNECTED)
            << "shutdown 后不应仍报 CONNECTED";
    }
}

// ==================== TD: create+shutdown 生命周期（改名） ====================
// 报告 §3.11 TA-4/TA-5：原名 TD_RecreateLifecycleNoLeak 名实不符，
// 循环体是 create→shutdown→destroy，从未调用 recreateGiveUpClient()。
// 此处按报告"或改名"的选项，改为 TD_CreateShutdownLifecycleNoLeak。
// 泄漏 oracle 由 CI 的 LSan 作业裁决；本测试只做趋势护栏。
TEST_F(ClientTest, TD_CreateShutdownLifecycleNoLeak) {
    constexpr int kIters = 20;
    for (int i = 0; i < kIters; ++i) {
        auto c = connectTo();
        ASSERT_NE(c, nullptr) << "iteration " << i;
        ASSERT_TRUE(c->shutdown().is_success()) << "iteration " << i;
    }
}

namespace {
inline size_t tid_hash() {
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
}
#define TA2_LOG(fmt, ...)                                              \
  do {                                                                 \
    std::fprintf(stderr, "[TA2][tid=%zu] " fmt "\n",                   \
                 ::tid_hash(), ##__VA_ARGS__);                         \
    std::fflush(stderr);                                               \
  } while (0)
}  // namespace

TEST(Client, TA2_ConcurrentConnect_ExactlyOneWins) {
  TA2_LOG("test begin");

  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  sdk->setAutoAdvancePerIterateMs(50);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  ASSERT_FALSE(cfg.check().has_value()) << *cfg.check();

  TA2_LOG("-> createWithSdk");
  auto r = OPC_UA_Client::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());
  TA2_LOG("<- createWithSdk ok, client=%p", (void *)c.get());

  TA2_LOG("-> c->disconnect()");
  auto dres = c->disconnect();
  TA2_LOG("<- c->disconnect() done, success=%d", (int)dres.is_success());
  ASSERT_TRUE(dres.is_success());
  TA2_LOG("<- initial disconnect ok");

  latch inConnect{1};
  latch releaseConnect{1};

  std::atomic<int> hookCalls{0};
  sdk->setConnectAsyncHook([&] {
    // 关键诊断：hook 到底有没有被调用、被调用了几次、由哪个 tid 调用。
    int n = hookCalls.fetch_add(1) + 1;
    TA2_LOG("HOOK called, hookCalls=%d", n);

    if (n == 1) {
      inConnect.count_down();
      TA2_LOG("HOOK count_down(inConnect) done");
    } else {
      // 第二个进入 hook 的线程：不要二次 count_down，否则 latch UB。
      TA2_LOG("HOOK entry #%d, skipping inConnect.count_down", n);
    }

    TA2_LOG("HOOK -> entering releaseConnect.wait()");
    releaseConnect.wait();
    TA2_LOG("HOOK <- releaseConnect.wait() returned");
  });

  std::atomic<int> ok{0}, fail{0};
  std::atomic<int> busy{0}, other{0};

  auto classify = [&](const auto& res, const char* who) {
    if (res.is_success()) {
      ++ok;
      TA2_LOG("classify[%s] = success", who);
    } else {
      ++fail;
      auto code = res.get_error()->code();
      if (code == RichError::ErrorCode::THREAD_BUSY) {
        ++busy;
        TA2_LOG("classify[%s] = THREADBUSY", who);
      } else {
        ++other;
        TA2_LOG("classify[%s] = other(code=%d)", who, (int)code);
      }
    }
  };

  std::promise<void> aDone, bDone;
  auto aDoneFut = aDone.get_future();
  auto bDoneFut = bDone.get_future();

  std::thread a([&] {
    TA2_LOG("thread a: -> c->connect()");
    try {
      classify(c->connect(), "a");
    } catch (...) {
      TA2_LOG("thread a: caught exception");
    }
    TA2_LOG("thread a: <- c->connect(), set aDone");
    aDone.set_value();
  });

  // 等第一个 connect 真正进入异步连接 hook。
  TA2_LOG("main: -> inConnect.wait()");
  inConnect.wait();
  TA2_LOG("main: <- inConnect.wait() returned, hookCalls=%d",
          hookCalls.load());

  std::thread b([&] {
    TA2_LOG("thread b: -> c->connect()");
    try {
      classify(c->connect(), "b");
    } catch (...) {
      TA2_LOG("thread b: caught exception");
    }
    TA2_LOG("thread b: <- c->connect(), set bDone");
    bDone.set_value();
  });

  using namespace std::chrono_literals;

  TA2_LOG("main: -> bDoneFut.wait_for(500ms)");
  auto bStatus = bDoneFut.wait_for(500ms);
  TA2_LOG("main: <- bDoneFut.wait_for, ready=%d, hookCalls=%d",
          (int)(bStatus == std::future_status::ready), hookCalls.load());

  if (bStatus != std::future_status::ready) {
    TA2_LOG("main: b not ready in 500ms, releasing hook");
    releaseConnect.count_down();

    auto aStatus2 = aDoneFut.wait_for(2s);
    auto bStatus2 = bDoneFut.wait_for(2s);
    TA2_LOG("main: grace wait: aReady=%d bReady=%d",
            (int)(aStatus2 == std::future_status::ready),
            (int)(bStatus2 == std::future_status::ready));

    if (a.joinable()) a.detach();
    if (b.joinable()) b.detach();

    if (aStatus2 != std::future_status::ready) {
      ADD_FAILURE() << "线程 a 在放行 hook 后 2s 仍未返回";
    }
    if (bStatus2 != std::future_status::ready) {
      ADD_FAILURE() << "线程 b 在放行 hook 后 2s 仍未返回";
    }

    FAIL() << "第二个 connect 没有在 500ms 内立即返回 THREAD_BUSY；"
              "hookCalls=" << hookCalls.load();
  }

  TA2_LOG("main: b ready, releasing hook");
  releaseConnect.count_down();

  TA2_LOG("main: -> aDoneFut.wait_for(2s)");
  auto aStatus = aDoneFut.wait_for(2s);
  TA2_LOG("main: <- aDoneFut.wait_for, ready=%d",
          (int)(aStatus == std::future_status::ready));
  if (aStatus != std::future_status::ready) {
    if (a.joinable()) a.detach();
    if (b.joinable()) b.detach();
    FAIL() << "线程 a 未能在 hook 放行后 2s 内返回";
  }

  TA2_LOG("main: a and b futures ready, joining threads");
  a.join();
  b.join();
  TA2_LOG("main: joined, ok=%d fail=%d busy=%d other=%d",
          ok.load(), fail.load(), busy.load(), other.load());

  EXPECT_EQ(ok.load(), 1);
  EXPECT_EQ(fail.load(), 1);
  EXPECT_EQ(busy.load(), 1) << "败者应因 CAS 竞争返回 THREADBUSY";
  EXPECT_EQ(other.load(), 0);
}