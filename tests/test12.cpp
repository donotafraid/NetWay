#include "OPCUAPacking/ua.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "tests/MockSdk.h"

#include "OPCUAPacking/internal/ClientTestHooks.h"

TEST(Client, T25_ConcurrentLifecycle_TSan) {
  auto sdk = std::make_shared<MockSdk>();
  sdk->setMode(MockSdk::Mode::Connected);

  ClientConfig cfg;
  cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  auto r = ClientTestHooks::createWithSdk(sdk, "opc.tcp://192.0.2.1:4840", cfg);
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

  // 只依赖 TSan 报告，不做功能断言（功能正确性由其它用例负责）
}