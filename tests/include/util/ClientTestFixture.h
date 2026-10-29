#pragma once

#include <gtest/gtest.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <stdint.h>

struct UA_Server;   // 前向声明，不 include open62541/server.h
class OPC_UA_Client;
struct ClientConfig;

typedef uint32_t UA_StatusCode;

class ClientTest : public ::testing::Test {
protected:
  void SetUp() override; // ← 无参，gtest 调用
  void TearDown() override;

  std::unique_ptr<OPC_UA_Client> connectTo();
  // 用调用方给定的配置建连（用于“短超时 + 断开-恢复”等集成用例）。
  std::unique_ptr<OPC_UA_Client> connectTo(const ClientConfig &cfg);
  uint16_t port() const { return port_; }

  virtual void addTestNodes();
  void configureServerPort(uint16_t port);


  UA_Server *server_ = nullptr;
  uint16_t port_ = 0;
  std::thread serverThread_;
  std::atomic<bool> running_{true};
  // server_ 当前是否处于 STARTED（run_startup 之后、run_shutdown 之前）。
  bool serverStarted_ = false;

  static std::atomic<uint16_t> g_nextPort;

  // Fixture-defined numeric NodeId (ns=1;i=20001) for the numeric-NodeId
  // round-trip test. String NodeIds are ns=1;s=TestInt.
  static constexpr uint32_t kTestIntNumericId = 20001;

  // 使用 RAII 包装器
  struct ServerDeleter ;

protected:
  // 先停事件循环再 join 迭代线程，避免 stopServer() 与仍在 run_iterate 的
  // 线程并发访问同一个 server_。用于“服务器断开-恢复”集成用例。
  void stopServerAndJoin();
  // 用同一个 server_ 重新 run_startup 并拉起迭代线程（与 stopServerAndJoin 配对）。
  void startServerThread();
  UA_StatusCode serverIsAccepting();
};