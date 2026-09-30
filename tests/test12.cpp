#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "tests/include/MockSdk.h"

#include "tests/include/ClientTestHooks.h"

TEST(Client, T25_ConcurrentLifecycle_TSan) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = OPCUAPacking::test::ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
  ASSERT_TRUE(r.has_value()) << r.get_error()->what();
  auto c = std::move(*r.get());

  std::atomic<bool> stop{false};

  auto reader = [&]{
    while (!stop) {
      std::vector<OPC_UA_Client::ReadValue> v{{{1, "TestInt", S7DataType::INT}}};
      (void)c->batchRead(v);
    }
  };
  auto writer = [&]{
    while (!stop) {
      std::vector<OPC_UA_Client::WriteValue> v{
          {{1, "TestInt", S7DataType::INT}, int16_t(1)}};
      (void)c->batchWrite(v);
    }
  };
  auto churn = [&]{
    while (!stop) {
      (void)c->checkConnected();
      (void)c->checkLifeState();
    }
  };

  std::vector<std::thread> ts;
  for (int i = 0; i < 2; ++i) ts.emplace_back(reader);
  for (int i = 0; i < 2; ++i) ts.emplace_back(writer);
  ts.emplace_back(churn);

  std::this_thread::sleep_for(std::chrono::seconds(2));
  stop = true;
  for (auto& t : ts) t.join();

  // TSan 只证明“没有数据竞争”，不证明“功能仍然正确”。这里补一条确定性断言：
  // 2 秒并发 churn 之后对象必须仍然可用；并且 shutdown() 之后 SDK 的
  // clientNew/clientDelete 必须配平（否则 TSan 干净但资源仍在泄漏）。
  // MockSdk 默认的 serviceRead 返回 BADNOTCONNECTED，所以显式给一个成功形状。
  sdk->setReadResponseShape(UA_STATUSCODE_GOOD, /*resultsSize=*/1,
                            /*allocResults=*/true);
  std::vector<OPC_UA_Client::ReadValue> one{{{1, "TestInt", S7DataType::INT}}};
  auto after = c->batchRead(one);
  sdk->resetResponseShape();
  EXPECT_TRUE(after.is_success())
      << "并发 churn 之后 batchRead 失败："
      << (after.is_fail() ? after.get_error()->codeName() : "");

  EXPECT_TRUE(c->shutdown().is_success());
  EXPECT_EQ(sdk->clientNewCallCount(), sdk->clientDeleteCallCount())
      << "shutdown() 之后 SDK client 的 new/delete 未配平";
}