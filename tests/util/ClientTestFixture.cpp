#include "tests/util/ClientTestFixture.h"

// open62541 server 头文件只在这里出现
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

// 你的客户端头文件
#include <stdexcept>
#include "OPCUAPacking/ua.h"

std::atomic<uint16_t> ClientTest::g_nextPort{4841};

void ClientTest::SetUp() {
    port_ = g_nextPort.fetch_add(1, std::memory_order_relaxed);

    server_ = UA_Server_new();
    if (!server_) throw std::runtime_error("UA_Server_new failed");

    UA_ServerConfig* config = UA_Server_getConfig(server_);
    UA_ServerConfig_setDefault(config);

    configureServerPort(port_);   // ← startup 之前

    addTestNodes();

    UA_StatusCode retval = UA_Server_run_startup(server_);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server_);
        server_ = nullptr;
        throw std::runtime_error("UA_Server_run_startup failed");
    }

    running_.store(true, std::memory_order_release);
    serverThread_ = std::thread([this] {
        while (running_.load(std::memory_order_acquire)) {
            UA_Server_run_iterate(server_, true);
        }
    });
}

void ClientTest::TearDown() {
    running_.store(false, std::memory_order_release);
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    if (server_) {
        UA_Server_run_shutdown(server_);
        UA_Server_delete(server_);
        server_ = nullptr;
    }
}

void ClientTest::configureServerPort(uint16_t port) {
    UA_ServerConfig* config = UA_Server_getConfig(server_);

    if (config->serverUrls && config->serverUrlsSize > 0) {
        UA_Array_delete(config->serverUrls, config->serverUrlsSize, &UA_TYPES[UA_TYPES_STRING]);
    }
    config->serverUrls = nullptr;
    config->serverUrlsSize = 0;

    std::string urlStr = "opc.tcp://127.0.0.1:" + std::to_string(port);
    UA_String url = UA_STRING_ALLOC(urlStr.c_str());

    UA_StatusCode retval = UA_Array_copy(&url, 1, (void**)&config->serverUrls, &UA_TYPES[UA_TYPES_STRING]);
    UA_String_clear(&url);
    if (retval != UA_STATUSCODE_GOOD) {
        throw std::runtime_error("Failed to set server URLs");
    }
    config->serverUrlsSize = 1;
}

void ClientTest::addTestNodes() {
    UA_Int16 value = 0;
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_INT16]);
    attr.accessLevel =
        UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE; // ← 加这行

    // 用字符串字面量，静态存储期，安全
    std::string varName{"TestInt"};
    int nameSpace = 1;
    UA_NodeId nodeId = UA_NODEID_STRING(nameSpace,varName.data() );
    UA_QualifiedName name = UA_QUALIFIEDNAME(nameSpace, varName.data());
    UA_NodeId parent = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parentRef = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);

    // SDK 深拷贝 attr
    UA_Server_addVariableNode(server_, nodeId, parent, parentRef,
                              name, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                              attr, nullptr, nullptr);
}

std::unique_ptr<OPC_UA_Client> ClientTest::connectTo() {
  ClientConfig cfg;
  // 用长阈值配置，匹配TSAN的长时间连接
  {
    cfg.timeoutMs = 60000;
    cfg.sessionTimeoutMs = 600000;

    cfg.secureChannelLifeTimeMs = 900000;

    cfg.maxRetries = 3;
    cfg.retryBackoffBaseMs = 2000;
    cfg.retryMaxBackoffMs = 30000;

    cfg.watchdogIntervalMs = 10000;
    cfg.connectivityCheckIntervalMs = 15000;
    cfg.giveUpThresholdMs = 120000;

    cfg.maxTotalWaitMs = 500000;
  }

  // 用短阈值配置，让 GIVEN_UP 快速到达
  //   {
  //       cfg.applyProfile(ClientConfig::PROFILE_LOCAL);
  //       cfg.watchdogIntervalMs = 1000;
  //       cfg.giveUpThresholdMs = 3000; // 3 秒进 GIVEN_UP
  //   }

  std::string url = "opc.tcp://127.0.0.1:" + std::to_string(port_);
  auto r = OPC_UA_Client::create(url, cfg, true);
  if (!r.has_value()) {
    throw std::runtime_error("OPC_UA_Client::create failed: " +
                             std::string(r.get_error()->what()));
  }

  auto up = std::move(*r.get()); // 先移出，r 内部变 nullptr
  return up; // 再返回，r 析构时内部是 nullptr，安全
}

void ClientTest::stopServer() { UA_Server_run_shutdown(server_); } // 停服务器

void ClientTest::restartServer() { UA_Server_run_startup(server_); }

UA_StatusCode ClientTest::serverIsAccepting() {
    // 每次调用建一个临时连接，探测完立即关闭
    UA_Client *probe = UA_Client_new();
    UA_StatusCode rc = UA_ClientConfig_setDefault(UA_Client_getConfig(probe));
    if (rc != UA_STATUSCODE_GOOD) {
      return UA_STATUSCODE_BAD; // 
    }

    // 用很短超时即可，只看 TCP 能否建连
    std::string url = "opc.tcp://127.0.0.1:" + std::to_string(port_);
    UA_StatusCode retval = UA_Client_connect(probe, url.c_str());

    UA_Client_delete(probe);
    return retval == UA_STATUSCODE_GOOD;
}