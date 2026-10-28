#include "tests/include/util/ClientTestFixture.h"

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

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {
unsigned processId() {
#if defined(_WIN32)
  return static_cast<unsigned>(_getpid());
#else
  return static_cast<unsigned>(getpid());
#endif
}
} // namespace

// gtest_discover_tests 让每个用例在独立进程里运行；固定从 4841 起会让
// `ctest -j` 的多个进程撞同一端口。按 PID 选一个 20000..39999 的基址，
// 同一进程内再顺序递增，串行与并行都稳定。
std::atomic<uint16_t> ClientTest::g_nextPort{
    static_cast<uint16_t>(20000 + (processId() % 20000))};

void ClientTest::SetUp() {
    port_ = g_nextPort.fetch_add(1, std::memory_order_relaxed);

    server_ = UA_Server_new();
    if (!server_) throw std::runtime_error("UA_Server_new failed");

    UA_ServerConfig* config = UA_Server_getConfig(server_);
    UA_ServerConfig_setDefault(config);

    // 显式等待一小段时间，确保 server 内部初始化（含 Timer 注册）完全稳定
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    configureServerPort(port_);   // ← startup 之前

    addTestNodes();

    UA_StatusCode retval = UA_Server_run_startup(server_);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_Server_delete(server_);
        server_ = nullptr;
        throw std::runtime_error("UA_Server_run_startup failed");
    }

    running_.store(true, std::memory_order_release);
    serverStarted_ = true;
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
        if (serverStarted_) {
            UA_Server_run_shutdown(server_);
            serverStarted_ = false;
        }
        UA_Server_delete(server_);
        server_ = nullptr;
    }
}

void ClientTest::stopServerAndJoin() {
    if (!server_ || !serverStarted_)
        return;
    // 迭代线程用 run_iterate(server_, true)（事件驱动阻塞，housekeeping 每
    // 1s 兜底唤醒），所以先置停止标志并 join，再 run_shutdown：避免
    // run_shutdown 与仍在 run_iterate 的线程并发访问同一个 server_。
    // 顺序与 TearDown() 保持一致。
    running_.store(false, std::memory_order_release);
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    UA_Server_run_shutdown(server_);
    serverStarted_ = false;
}

void ClientTest::startServerThread() {
    if (!server_ || serverStarted_)
        return;
    UA_StatusCode rc = UA_Server_run_startup(server_);
    if (rc != UA_STATUSCODE_GOOD) {
        throw std::runtime_error("UA_Server_run_startup (restart) failed");
    }
    serverStarted_ = true;
    running_.store(true, std::memory_order_release);
    serverThread_ = std::thread([this] {
        while (running_.load(std::memory_order_acquire)) {
            UA_Server_run_iterate(server_, true);
        }
    });
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
    if (!url.data) {
      throw std::runtime_error("Failed to allocate server URL string");
    }

    UA_StatusCode retval = UA_Array_copy(&url, 1, (void**)&config->serverUrls, &UA_TYPES[UA_TYPES_STRING]);
    UA_String_clear(&url);
    if (retval != UA_STATUSCODE_GOOD) {
      config->serverUrls = nullptr; // 防御：无论 UA_Array_copy 是否改了 *dst
      config->serverUrlsSize = 0;
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

std::unique_ptr<OPC_UA_Client> ClientTest::connectTo(const ClientConfig &cfg) {
  std::string url = "opc.tcp://127.0.0.1:" + std::to_string(port_);
  // useDefault=false：调用方已经显式给了配置，非法配置应当立即被拒，
  // 而不是被 applyDefaultIfInvalid() 静默修复。
  auto r = OPC_UA_Client::create(url, cfg, false);
  if (!r.has_value()) {
    throw std::runtime_error("OPC_UA_Client::create(cfg) failed: " +
                             std::string(r.get_error()->what()));
  }
  auto up = std::move(*r.get());
  return up;
}

UA_StatusCode ClientTest::serverIsAccepting() {
    // 每次调用建一个临时连接，探测完立即关闭
    UA_Client *probe = UA_Client_new();
    UA_StatusCode rc = UA_ClientConfig_setDefault(UA_Client_getConfig(probe));
    if (rc != UA_STATUSCODE_GOOD) {
        UA_Client_delete(probe);
      return UA_STATUSCODE_BAD; // 
    }

    // 用很短超时即可，只看 TCP 能否建连
    std::string url = "opc.tcp://127.0.0.1:" + std::to_string(port_);
    UA_StatusCode retval = UA_Client_connect(probe, url.c_str());

    UA_Client_delete(probe);
    return retval;
}