// tests/test31.cpp
//
// 补充用例：把现有 30 个测试文件仍未覆盖的、最贴近“工业现场多线程 + 真实 SDK”
// 的四条风险钉死：
//   T94  真实 open62541 服务器上的 STRING 读写往返：验证 UA_ReadResponse 中
//        堆分配的 UA_String 被完整复制后被 Guard 清理（LSan 裁决），
//        而不是只在 MockSdk 上验证。
//   T95  真实服务器对不存在 NodeId 返回“服务层 GOOD + 按项 BadNodeIdUnknown”，
//        封装层必须按项映射为 rawStatus != GOOD 且 value == nullopt。
//   T96  batchWrite 在“前一项 STRING 已分配 NodeId + Variant”之后中途参数
//        校验失败：WriteRequestGuard 必须释放前一项（T35 只覆盖 INT）。
//   T97  batchRead 遇到 resultsSize == 请求数 但 results == nullptr 的畸形响应：
//        必须走 `!response.results` 独立出口并失败（现有 seam 到不了该分支）。
//   T98  recreateGiveUpClient 与“在途 batchWrite”并发：必须 IN_FLIGHT_TIMEOUT；
//        T18/T42 只覆盖了在途读，写路径是不对称的缺口。
//   T99  serviceRead 与 serviceWrite 在同一客户端上绝不在 SDK 层重叠，
//        且在并发下读写结果仍然正确（T25 已有 churn 后的功能断言与 new/delete 配平断言）。
//   T100 用户 setGiveUpSignal() 置位的 GIVEN_UP 对看门狗裁决具有粘性，
//        不会被 TurnToRunning() 的 CAS 丢失更新覆盖。
//
// 其中 T96/T97/T98/T99 依赖 tests/include/MockSdk.h 的三处 seam 扩展
// （setServiceWriteHook / inServiceWrite / setReadResponseNullResults），
// 对应补丁见报告末尾。

#include "tests/include/util/ClientTestFixture.h"

#include "OPCUAPacking/ua.h"

#include <gtest/gtest.h>
#include <open62541/client.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

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
std::unique_ptr<OPC_UA_Client>
makeClient(const std::shared_ptr<MockSdk> &sdk, bool watchdog = false) {
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
// 真实服务器 fixture：在 TestInt 之外再挂一个可读写 STRING 变量。
// ============================================================
class ClientStringFixture : public ClientTest {
protected:
  void addTestNodes() override {
    ClientTest::addTestNodes(); // ns=1 "TestInt"

    UA_String seed = UA_STRING_ALLOC("seed-string");
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, &seed, &UA_TYPES[UA_TYPES_STRING]);
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    const int ns = 1;
    std::string name{"TestStr"};
    UA_NodeId nodeId = UA_NODEID_STRING(ns, name.data());
    UA_QualifiedName qn = UA_QUALIFIEDNAME(ns, name.data());
    UA_NodeId parent = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parentRef = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_Server_addVariableNode(server_, nodeId, parent, parentRef, qn,
                              UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                              attr, nullptr, nullptr);
    UA_String_clear(&seed); // 服务器已深拷贝 attr
  }
};

// ============================================================
// T94: 真实 SDK —— STRING 读写往返，UA_ReadResponse 出口不得泄漏/悬垂
// ============================================================
TEST_F(ClientStringFixture, T94_RealSdk_StringRoundTrip_NoLeak) {
  auto c = connectTo();
  ASSERT_NE(c, nullptr);

  const std::string payload(64, 'A'); // 足够长，服务端响应里必然是堆分配的 UA_String
  const std::string nodeName{"TestStr"};

  OPC_UA_Client::WriteValue wv{{1, nodeName, S7DataType::STRING}, payload};
  auto wr = c->batchWrite(std::vector<OPC_UA_Client::WriteValue>{wv});
  ASSERT_TRUE(wr.is_success()) << (wr.is_fail() ? wr.get_error()->what() : "");
  const auto &wout = wr.value_or({});
  ASSERT_EQ(wout.size(), 1u);
  EXPECT_EQ(wout[0].status, OPC_UA_Client::WriteResult::Status::Good);

  std::vector<OPC_UA_Client::ReadValue> rv{{1, nodeName, S7DataType::STRING}};
  auto rr = c->batchRead(rv);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");

  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].rawStatus, UA_STATUSCODE_GOOD);
  ASSERT_TRUE(out[0].value.has_value());
  ASSERT_TRUE(std::holds_alternative<std::string>(*out[0].value));
  EXPECT_EQ(std::get<std::string>(*out[0].value), payload)
      << "真实 UA_ReadResponse 中的 UA_String 未被长度保真地复制/已悬垂";

  OPCUA_LSAN_CHECK(); // 响应必须已在 batchRead 返回前被 ReadResponseGuard 清理
}

// ============================================================
// T95: 真实 SDK —— 不存在 NodeId 的“服务层 GOOD + 按项 BAD”
// ============================================================
TEST_F(ClientTest, T95_RealSdk_UnknownNode_PerItemBad_MapsToNullopt) {
  auto c = connectTo();
  ASSERT_NE(c, nullptr);

  std::vector<OPC_UA_Client::ReadValue> rv{
      {1, "NoSuchNode_Review_XYZ", S7DataType::INT}};
  auto rr = c->batchRead(rv);
  // OPC UA Read 对“节点不存在”返回服务层 GOOD、按项 BadNodeIdUnknown。
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");

  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  EXPECT_NE(out[0].rawStatus, UA_STATUSCODE_GOOD)
      << "服务层 GOOD 时按项错误必须保留在 rawStatus";
  EXPECT_FALSE(out[0].value.has_value())
      << "按项 BAD 必须映射为 nullopt，不得伪造值";

  OPCUA_LSAN_CHECK();
}

// ============================================================
// T96: batchWrite 前一项 STRING 已分配，后一项中途失败 → 全部释放
// ============================================================
TEST(Client, T96_BatchWrite_PartialFailureAfterStringItem_FreesAll) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  std::vector<OPC_UA_Client::WriteValue> v{
      {{1, "GoodString", S7DataType::STRING}, std::string("x")},
      {{1, "", S7DataType::STRING}, std::string("y")}};

  auto wr = c->batchWrite(v);
  ASSERT_TRUE(wr.is_fail());
  EXPECT_EQ(wr.get_error()->code(), RichError::ErrorCode::INVALID_ARGUMENT);
  EXPECT_EQ(sdk->serviceWriteCallCount(), 0u)
      << "参数校验失败时不应下发 SDK";

  OPCUA_LSAN_CHECK();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T97: resultsSize == 请求数 但 results == nullptr → 独立出口必须失败
// ============================================================
TEST(Client, T97_BatchRead_ResultsNullWithMatchingSize_Rejected) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseNullResults(/*resultsSize=*/1);
  std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT}};
  auto rr = c->batchRead(v);
  EXPECT_TRUE(rr.is_fail())
      << "resultsSize 与请求数一致但 results==nullptr 竟被接受";

  OPCUA_LSAN_CHECK();
  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T98: recreateGiveUpClient 与在途 batchWrite 并发 → IN_FLIGHT_TIMEOUT
// ============================================================
TEST(Client, T98_RecreateInFlightWrite_Timeout) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  latch inWrite{1}, releaseWrite{1};
  sdk->setServiceWriteHook([&] {
    inWrite.count_down();
    releaseWrite.wait();
  });
  sdk->setWriteResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1);

  std::atomic<bool> writeDone{false};
  JThread writer([&] {
    std::vector<OPC_UA_Client::WriteValue> v{
        {{1, "N", S7DataType::INT}, int16_t(1)}};
    (void)c->batchWrite(v);
    writeDone.store(true, std::memory_order_release);
  });
  ASSERT_TRUE(waitFor([&] { return inWrite.try_wait(); }, 2s))
      << "writer 未进入 serviceWrite";

  ASSERT_TRUE(c->setGiveUpSignal().is_success());
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::GIVEN_UP);

  auto rr = c->recreateGiveUpClient();
  ASSERT_TRUE(rr.is_fail()) << "在途写未排空时 recreate 竟成功";
  EXPECT_EQ(rr.get_error()->code(), RichError::ErrorCode::IN_FLIGHT_TIMEOUT)
      << "context=" << rr.get_error()->what();

  releaseWrite.count_down();
  ASSERT_TRUE(waitFor([&] { return writeDone.load(); }, 5s));
  writer.join();

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T99: serviceRead 与 serviceWrite 绝不重叠 + 并发结果正确
// ============================================================
TEST(Client, T99_ConcurrentReadWrite_NoOverlap_Functional) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk, /*watchdog=*/true);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16(0, 42);

  std::atomic<bool> overlap{false};
  sdk->setServiceReadHook([&] {
    if (sdk->inServiceWrite() > 0)
      overlap.store(true, std::memory_order_relaxed);
  });
  sdk->setServiceWriteHook([&] {
    if (sdk->inServiceRead() > 0)
      overlap.store(true, std::memory_order_relaxed);
  });

  constexpr int kPerThread = 150;
  std::atomic<int> readOk{0}, writeOk{0}, other{0};

  std::vector<JThread> threads;
  for (int t = 0; t < 2; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < kPerThread; ++i) {
        std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT}};
        auto rr = c->batchRead(v);
        const auto &out = rr.value_or({});
        if (rr.is_success() && out.size() == 1 && out[0].value.has_value() &&
            std::holds_alternative<int16_t>(*out[0].value) &&
            std::get<int16_t>(*out[0].value) == 42) {
          readOk.fetch_add(1, std::memory_order_relaxed);
        } else {
          other.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
    threads.emplace_back([&] {
      for (int i = 0; i < kPerThread; ++i) {
        std::vector<OPC_UA_Client::WriteValue> v{
            {{1, "A", S7DataType::INT}, int16_t(42)}};
        if (c->batchWrite(v).is_success())
          writeOk.fetch_add(1, std::memory_order_relaxed);
        else
          other.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  for (auto &th : threads)
    th.join();

  EXPECT_FALSE(overlap.load()) << "serviceRead 与 serviceWrite 在 SDK 层重叠";
  EXPECT_EQ(other.load(), 0);
  EXPECT_EQ(readOk.load(), 2 * kPerThread);
  EXPECT_EQ(writeOk.load(), 2 * kPerThread);

  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T100: GIVEN_UP 对看门狗裁决具有粘性（CAS 不得丢失用户意图）
// ============================================================
TEST(Client, T100_GiveUp_IsStickyUnderWatchdogPump) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  ASSERT_TRUE(c->setGiveUpSignal().is_success());
  ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
            LifeState::GIVEN_UP);

  for (int i = 0; i < 8; ++i) {
    OPCUAPacking::test::ClientTestHooks::pumpWatchdogForTest(*c);
    ASSERT_EQ(c->checkLifeState().value_or(LifeState::RUNNING),
              LifeState::GIVEN_UP)
        << "看门狗裁决把用户主动置位的 GIVEN_UP 覆盖回 RUNNING (i=" << i << ")";
  }
  EXPECT_TRUE(c->shutdown().is_success());
}

// ============================================================
// T102（缺陷复现）: batchRead 成功项也必须给出确定的 errorCode
// ua.cpp 中 `ReadResult var;` 只在失败分支写 errorCode；成功分支
// （status==GOOD 且转换成功）从不赋值，errorCode 保持 indeterminate。
// ============================================================
TEST(Client, T102_BatchRead_Success_ResultErrorCodeIsDeterministic) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);
  auto c = makeClient(sdk);
  ASSERT_NE(c, nullptr);

  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  sdk->setReadItemInt16(0, 7);

  std::vector<OPC_UA_Client::ReadValue> v{{1, "A", S7DataType::INT}};
  auto rr = c->batchRead(v);
  ASSERT_TRUE(rr.is_success()) << (rr.is_fail() ? rr.get_error()->what() : "");
  const auto &out = rr.value_or({});
  ASSERT_EQ(out.size(), 1u);
  ASSERT_TRUE(out[0].value.has_value());
  EXPECT_EQ(out[0].errorCode, RichError::ErrorCode::OK)
      << "成功读取的 ReadResult::errorCode 未被赋值，调用方读到不确定值";

  OPCUA_LSAN_CHECK();
  sdk->resetResponseShape();
  EXPECT_TRUE(c->shutdown().is_success());
}
