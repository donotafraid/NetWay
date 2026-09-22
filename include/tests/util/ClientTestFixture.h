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
  uint16_t port() const { return port_; }

  virtual void addTestNodes();
  void configureServerPort(uint16_t port);


  UA_Server *server_ = nullptr;
  uint16_t port_ = 0;
  std::thread serverThread_;
  std::atomic<bool> running_{true};

  static std::atomic<uint16_t> g_nextPort;

  // 使用 RAII 包装器
  struct ServerDeleter ;

protected:
  void stopServer() ; // 停服务器
  void restartServer(); // 重启服务器
  UA_StatusCode serverIsAccepting();
};