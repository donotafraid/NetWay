// tests/test23.cpp —— 补测（应全部通过）
// 本文件把两者合并以便一次性编译；正式落地时请拆成两个 TU。

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "tests/include/MockSdk.h"
#include "tests/include/util/Cxx17Compat.h"
#include "tests/include/util/wait_for.h"
#include "tests/include/ClientTestHooks.h"

using namespace std::chrono_literals;

namespace {

std::unique_ptr<OPC_UA_Client>
makeExtraClient(const std::shared_ptr<MockSdk> &sdk, bool watchdog = false) {
  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(
      sdk, "opc.tcp://192.0.2.1:4840", cfg, /*usedefault=*/false, watchdog);
  EXPECT_TRUE(r.has_value()) << (r.is_fail() ? r.get_error()->what() : "");
  if (r.is_fail())
    return nullptr;
  return std::move(*r.get());
}

} // namespace

// ============================================================
// T67: shutdown() 必须在返回前真正释放 UA_Client（deleter 已执行）
//
// 现有 T16/T17 只验证 shutdown 的时序上界；没有任何用例证明
// `m_impl.reset() -> UA_ClientDeleter -> clientDelete` 这条链真的走到了。
// ============================================================
TEST(ClientExtra, T67_ShutdownReleasesUnderlyingSdkClient) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeExtraClient(sdk);
  ASSERT_NE(c, nullptr);
  ASSERT_NE(sdk->rawClient(), nullptr) << "构造后 UA_Client 应存在";

  ASSERT_TRUE(c->shutdown().is_success());

  EXPECT_EQ(sdk->rawClient(), nullptr)
      << "shutdown() 返回后仍持有 UA_Client：deleter 未执行或清理被推迟";
}

// ============================================================
// T68: batchWrite 按项 status 映射（当前写入路径完全未覆盖 BAD 项）
// ============================================================
TEST(ClientExtra, T68_BatchWrite_PerItemStatus_MapsToPerItemResult) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeExtraClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/2);
  sdk->setWriteItemStatus(0, UA_STATUSCODE_GOOD);
  sdk->setWriteItemStatus(1, UA_STATUSCODE_BADNOTWRITABLE);

  std::vector<OPC_UA_Client::WriteValue> v{
      {{1, "A", S7DataType::INT}, int16_t(1)},
      {{1, "B", S7DataType::INT}, int16_t(2)}};

  auto rr = c->batchWrite(v);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");

  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].status, OPC_UA_Client::WriteResult::Status::Good);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  EXPECT_EQ(out[1].status, OPC_UA_Client::WriteResult::Status::Bad)
      << "按项 BAD 被误报为 Good";
  EXPECT_EQ(out[1].rawStatus, UA_STATUSCODE_BADNOTWRITABLE);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T69: shutdown() 与在途 connect() 的排空/唤醒契约
//
// 现有用例覆盖 “shutdown × 在途读”(T16/T17) 与 “connect × disconnect”
// (T40)，但没有 “shutdown × 在途 connect”。connect() 持有 InFlightGuard，
// shutdown() 必须等它退出，且 stop 信号必须让 connect 以失败收尾。
// ============================================================
TEST(ClientExtra, T69_ShutdownWhileConnectInFlight_DrainsAndFailsConnect) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeExtraClient(sdk);
  ASSERT_NE(c, nullptr);

  latch inConnect{1};
  latch releaseConnect{1};
  sdk->setConnectAsyncHook([&] {
    inConnect.count_down();
    releaseConnect.wait();
  });

  std::atomic<bool> connectReturned{false};
  std::atomic<bool> connectOk{true};
  std::thread connector([&] {
    connectOk.store(c->connect().is_success());
    connectReturned.store(true, std::memory_order_release);
  });

  ASSERT_TRUE(waitFor([&] { return inConnect.try_wait(); }, 2s))
      << "connect 未进入 connectAsync 钩子";

  // 200ms 后放行 connectAsync，让 connect() 有机会观察 stop 信号。
  std::thread releaser([&] {
    std::this_thread::sleep_for(200ms);
    releaseConnect.count_down();
  });

  const auto t0 = std::chrono::steady_clock::now();
  auto sr = c->shutdown();
  const auto dt = std::chrono::steady_clock::now() - t0;

  releaser.join();
  ASSERT_TRUE(waitFor([&] { return connectReturned.load(); }, 5s));
  connector.join();

  EXPECT_TRUE(sr.is_success()) << (sr.is_fail() ? sr.get_error()->what() : "");
  EXPECT_FALSE(connectOk.load()) << "stop 信号置位后 connect 仍报成功";
  EXPECT_LT(dt, 5s) << "shutdown 被在途 connect 长时间阻塞";
}

// ============================================================
// T72: shutdown() 之后调用 setGiveUpSignal()/测试钩子不得崩溃
// （pImpl 已被移走，这些入口没有 ApiLease 保护）
// ============================================================
TEST(ClientExtra, T72_SetGiveUpSignalAfterShutdown_IsSafe) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeExtraClient(sdk);
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->shutdown().is_success());

  c->setGiveUpSignal();          // pImpl == nullptr
  OPCUAPacking::test::ClientTestHooks::pumpWatchdogForTest(*c);
  EXPECT_FALSE(OPCUAPacking::test::ClientTestHooks::getRecreatingStatus(*c));
  SUCCEED();
}

// ============================================================
// 以下为缺陷复现：当前实现会失败，修复后应转绿。
// ============================================================

// B1: GIVEN_UP 状态下，checkConnected() 与 isHealthy() 描述同一事实
//     却返回不同错误码（ALREADY_TERMINATED vs NOT_CONNECTED）。
TEST(ClientBug, B1_CheckConnected_And_IsHealthy_AgreeOnSameState) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeExtraClient(sdk);
  ASSERT_NE(c, nullptr);

  c->setGiveUpSignal();
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::GIVEN_UP);

  auto cc = c->checkConnected();
  auto hi = c->isHealthy();
  ASSERT_TRUE(cc.is_fail());
  ASSERT_TRUE(hi.is_fail());

  EXPECT_EQ(cc.get_error()->code(), hi.get_error()->code())
      << "checkConnected=" << cc.get_error()->codeName()
      << " isHealthy=" << hi.get_error()->codeName()
      << "（同一 GIVEN_UP 状态，错误类别必须一致）";

  EXPECT_TRUE(c->shutdown().is_success());
}

// B2: NodeId 含内嵌 '\0' 时，封装层用 UA_NODEID_STRING_ALLOC(strlen)
//     静默截断：std::string("A\0B", 3) 只把 "A" 发给服务端。
TEST(ClientBug, B2_NodeIdEmbeddedNul_MustNotBeSilentlyTruncated) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeExtraClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, 1, true);

  const std::string id("A\0B", 3); // size()==3，非空
  ASSERT_EQ(id.size(), 3u);
  std::vector<OPC_UA_Client::ReadValue> v{{1, id, S7DataType::INT}};

  auto rr = c->batchRead(v);
  if (rr.is_success()) {
    auto lens = sdk->lastReadNodeIdLengths();
    ASSERT_EQ(lens.size(), 1u);
    EXPECT_EQ(lens[0], id.size())
        << "NodeId 被 strlen 截断：SDK 收到长度 " << lens[0]
        << "，调用方传入 " << id.size();
  } else {
    // 也可选择显式拒绝；但不能既不拒绝又静默截断。
    EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);
  }

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// B3: isHealthy() 的“不健康”错误文案复用了 checkConnected。
TEST(ClientBug, B3_IsHealthy_ErrorTextMustNotMentionCheckConnected) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  auto c = makeExtraClient(sdk);
  ASSERT_NE(c, nullptr);

  // 通道 OPEN、会话 CLOSED：isHealthy() 必须失败（三元组不全）。
  sdk->fireStateCallback(UA_STATUSCODE_GOOD, UA_SECURECHANNELSTATE_OPEN,
                         UA_SESSIONSTATE_CLOSED);
  auto hi = c->isHealthy();
  ASSERT_TRUE(hi.is_fail()) << "会话未激活时 isHealthy() 仍报健康";

  EXPECT_EQ(std::string(hi.get_error()->what()).find("checkConnected"),
            std::string::npos)
      << "isHealthy() 复用了 checkConnected 的文案: "
      << hi.get_error()->what();

  EXPECT_TRUE(c->shutdown().is_success());
}
