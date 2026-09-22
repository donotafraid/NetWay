#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <sstream>

#include <string>
#include "Rust_error_deal/error_deal.h"
#include "PLC/WriteRequestAddres.h"

class ISdk;
class UA_ClientPtr;
class RecreateSync;

// 连接状态的枚举
enum class ConnectionState {
  OBJECT_ONLY,   // 对象存在但未连接
  CONNECTED     // 已连接
};

enum class ConnState : uint8_t {
    IDLE,       ///< 空闲，允许发起连接操作
    CONNECTING  ///< 正在执行连接操作（已持有互斥锁）
};

enum class LifeState : uint8_t {
  /// 三元组全部就绪：
  ///   sdkConnectStatus == GOOD
  ///   sdkChannelState  == OPEN
  ///   sdkSessionState  == ACTIVATED
  /// 由 TurnToRunning() 置位（看门狗判定健康时）。
  RUNNING,

  /// 已检测到连接不健康，但仍处于"自愈窗口"内：
  ///   now - m_lastBadStatusMs < giveUpThresholdMs
  /// 看门狗会继续尝试 connect()；若 connect 返回非
  /// SHUTDOWN/NULLPTR 的错误，则进入本状态。
  ///
  /// 注意：本状态期间 batchRead / batchWrite **均快速失败**
  /// （不是"只读"，而是读写都拒绝），避免业务线程在不健康连接上
  /// 长时间阻塞。恢复动作完全由看门狗承担。
  /// 由 TurnToRecovring() 置位。
  RECOVERING,

  /// 自愈窗口耗尽，或连接进入不可恢复状态：
  ///   - 主路径：now - m_lastBadStatusMs >= giveUpThresholdMs
  ///   - 辅路径：connect() 返回 SHUTDOWN 或 NULLPTR
  ///   - 旁路  ：业务主动调用 setGiveUpSignal()
  ///
  /// 进入本状态后，看门狗不再主动重连；需业务显式调用
  /// recreateGiveUpClient() 重建客户端。
  /// 由 TurnToGiveup() 或 setGiveUpSignal() 置位。
  GIVEN_UP,
};

enum class DisconnectErrorState {
  SHUTDOWN,              // 检测到shutdown =true
  NULLPTR,               // 检测到client=nullptr
  THREADBUSY,            // 检测到CAS状态竞争
  TIMEOUT_NOTDISCONNECT, // TIMEOUT分为两种情况：1.发起超时：在超时分支收尾处没有检测到disconnect成功;2.在途被放弃：在超时分支收尾处检测到disconnect成功
  DISCONNECT_FAIL, // 检测到异步断连失败
  UNKNOWN          // 检测到CAS状态机的未知错误
};

// 首先定义连接错误枚举
enum class ConnectErrorState {
  SHUTDOWN,
  NULLPTR,
  THREADBUSY,
  UNKNOWN,
  CONNECT_FAILED,
  TIMEOUT_NOTCONNECT // TIMEOUT分为两种情况：1.发起超时：在超时分支收尾处没有检测到connect连接成功；2.在途被放弃：在超时分支收尾处检测到connect连接成功
};

struct ClientConfig {
  // ==================== 超时配置 ====================
  uint32_t timeoutMs = 10000;
  uint32_t sessionTimeoutMs = 600000;
  uint32_t secureChannelLifeTimeMs = 600000;
  uint32_t maxTotalWaitMs = 0;
  int64_t giveUpThresholdMs =
      60000; // 用于看门狗后台重连，超出该时间 LifeState 未恢复至 Running，则置为 GIVE-UP
             // 约束：>= 2 * watchdogIntervalMs，建议 >= maxTotalWaitMs 且 <
             // sessionTimeoutMs

  // ==================== 重试配置 ====================
  uint32_t maxRetries = 3;
  uint32_t retryBackoffBaseMs = 1000;
  uint32_t retryMaxBackoffMs = 8000;

  // ==================== 心跳 & 保活配置 ====================
  uint32_t connectivityCheckIntervalMs = 10000;
  uint32_t watchdogIntervalMs = 5000;

  // ==================== 网络环境预设 ====================
  enum NetworkProfile {
    PROFILE_LOCAL,
    PROFILE_REMOTE,
    PROFILE_MOBILE,
    PROFILE_UNSTABLE
  };

  void applyProfile(NetworkProfile profile) {
    switch (profile) {
    case PROFILE_LOCAL:
      timeoutMs = 5000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 2;
      retryBackoffBaseMs = 500;
      retryMaxBackoffMs = 4000;

      maxTotalWaitMs = 30000;

      connectivityCheckIntervalMs = 30000;
      watchdogIntervalMs = 10000;

      // giveUpThresholdMs 约束：
      // 1. >= 2 * watchdogIntervalMs (2 * 10000 = 20000)
      // 2. >= maxTotalWaitMs (30000)
      // 3. < sessionTimeoutMs (600000)
      // 取 60000：给一次会话内的重连/恢复留出充分余量
      giveUpThresholdMs = 60000; // 60秒
      break;

    case PROFILE_REMOTE:
      timeoutMs = 15000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 3;
      retryBackoffBaseMs = 1000;
      retryMaxBackoffMs = 10000;

      maxTotalWaitMs = 120000;

      connectivityCheckIntervalMs = 15000;
      watchdogIntervalMs = 8000;

      // giveUpThresholdMs 约束：
      // 1. >= 2 * watchdogIntervalMs (2 * 8000 = 16000)
      // 2. >= maxTotalWaitMs (120000)
      // 3. < sessionTimeoutMs (600000)
      giveUpThresholdMs = 180000; // 180秒（3分钟）
      break;

    case PROFILE_MOBILE:
      timeoutMs = 30000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 4;
      retryBackoffBaseMs = 2000;
      retryMaxBackoffMs = 20000;

      maxTotalWaitMs = 300000;

      connectivityCheckIntervalMs = 10000;
      watchdogIntervalMs = 5000;

      // giveUpThresholdMs 约束：
      // 1. >= 2 * watchdogIntervalMs (2 * 5000 = 10000)
      // 2. >= maxTotalWaitMs (300000)
      // 3. < sessionTimeoutMs (600000)
      giveUpThresholdMs = 360000; // 360秒（6分钟）
      break;

    case PROFILE_UNSTABLE:
      timeoutMs = 20000;
      sessionTimeoutMs = 300000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 5;
      retryBackoffBaseMs = 1000;
      retryMaxBackoffMs = 15000;

      maxTotalWaitMs = 240000;

      connectivityCheckIntervalMs = 5000;
      watchdogIntervalMs = 3000;

      // giveUpThresholdMs 约束：
      // 1. >= 2 * watchdogIntervalMs (2 * 3000 = 6000)
      // 2. >= maxTotalWaitMs (240000)
      // 3. < sessionTimeoutMs (300000)
      // 为避免等于 maxTotalWaitMs 时无余量，建议略大；这里取 270000
      giveUpThresholdMs = 270000; // 270秒（4.5分钟）
      break;
    }
  }

  // ==================== 只读校验（不修改任何成员） ====================
  std::optional<std::string> check() const {
    std::vector<std::string> errors;

    auto add_error = [&](const std::string &msg) { errors.push_back(msg); };

    if (timeoutMs == 0) {
      add_error("timeoutMs must be > 0");
    }
    if (sessionTimeoutMs == 0) {
      add_error("sessionTimeoutMs must be > 0");
    }
    if (secureChannelLifeTimeMs < sessionTimeoutMs) {
      add_error("secureChannelLifeTimeMs (" +
                std::to_string(secureChannelLifeTimeMs) +
                ") must be >= sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (maxRetries > 10) {
      add_error("maxRetries (" + std::to_string(maxRetries) +
                ") must be <= 10");
    }
    if (watchdogIntervalMs < 1000 || watchdogIntervalMs > 30000) {
      add_error("watchdogIntervalMs (" + std::to_string(watchdogIntervalMs) +
                ") must be in [1000, 30000]");
    }
    if (watchdogIntervalMs >= sessionTimeoutMs && sessionTimeoutMs > 0) {
      add_error("watchdogIntervalMs (" + std::to_string(watchdogIntervalMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (retryBackoffBaseMs == 0) {
      add_error("retryBackoffBaseMs must be > 0");
    }
    if (retryMaxBackoffMs < retryBackoffBaseMs) {
      add_error("retryMaxBackoffMs (" + std::to_string(retryMaxBackoffMs) +
                ") must be >= retryBackoffBaseMs (" +
                std::to_string(retryBackoffBaseMs) + ")");
    }
    if (connectivityCheckIntervalMs < 1000) {
      add_error("connectivityCheckIntervalMs (" +
                std::to_string(connectivityCheckIntervalMs) +
                ") must be >= 1000");
    }
    if (connectivityCheckIntervalMs >= sessionTimeoutMs &&
        sessionTimeoutMs > 0) {
      add_error("connectivityCheckIntervalMs (" +
                std::to_string(connectivityCheckIntervalMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (retryBackoffBaseMs >= sessionTimeoutMs && sessionTimeoutMs > 0) {
      add_error("retryBackoffBaseMs (" + std::to_string(retryBackoffBaseMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (timeoutMs >= sessionTimeoutMs && sessionTimeoutMs > 0) {
      add_error("timeoutMs (" + std::to_string(timeoutMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }
    if (retryMaxBackoffMs >= timeoutMs && timeoutMs > 0) {
      add_error("retryMaxBackoffMs (" + std::to_string(retryMaxBackoffMs) +
                ") must be < timeoutMs (" + std::to_string(timeoutMs) + ")");
    }

    // ==================== giveUpThresholdMs 校验 ====================
    if (giveUpThresholdMs < static_cast<int64_t>(watchdogIntervalMs) * 2) {
      add_error("giveUpThresholdMs (" + std::to_string(giveUpThresholdMs) +
                ") must be >= 2 * watchdogIntervalMs (" +
                std::to_string(static_cast<int64_t>(watchdogIntervalMs) * 2) +
                ")");
    }
    if (giveUpThresholdMs <= 0) {
      add_error("giveUpThresholdMs must be > 0");
    }
    if (sessionTimeoutMs > 0 &&
        giveUpThresholdMs >= static_cast<int64_t>(sessionTimeoutMs)) {
      add_error("giveUpThresholdMs (" + std::to_string(giveUpThresholdMs) +
                ") must be < sessionTimeoutMs (" +
                std::to_string(sessionTimeoutMs) + ")");
    }

    // 验证 maxTotalWaitMs
    if (maxTotalWaitMs > 0) {
      if (maxTotalWaitMs <= timeoutMs) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be > timeoutMs (" + std::to_string(timeoutMs) + ")");
      }
      if (maxTotalWaitMs >= sessionTimeoutMs && sessionTimeoutMs > 0) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be < sessionTimeoutMs (" +
                  std::to_string(sessionTimeoutMs) + ")");
      }
      if (maxRetries > 0 && maxTotalWaitMs < timeoutMs + retryMaxBackoffMs) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be >= timeoutMs + retryMaxBackoffMs (" +
                  std::to_string(timeoutMs + retryMaxBackoffMs) +
                  ") when maxRetries > 0");
      }

      uint64_t requiredBudget =
          2ULL * timeoutMs * (static_cast<uint64_t>(maxRetries) + 1);
      if (requiredBudget > std::numeric_limits<uint32_t>::max()) {
        add_error("maxTotalWaitMs cannot satisfy required budget (overflow)");
      } else if (maxTotalWaitMs < static_cast<uint32_t>(requiredBudget)) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be >= 2*timeoutMs*(maxRetries+1) = " +
                  std::to_string(requiredBudget) +
                  " (timeoutMs=" + std::to_string(timeoutMs) +
                  ", maxRetries=" + std::to_string(maxRetries) + ")");
      }
    } else {
      add_error("maxTotalWaitMs must be > 0; "
                "call applyProfile() or applyDefaultIfInvalid()");
    }

    if (errors.empty()) {
      return std::nullopt;
    }

    std::string combined;
    for (size_t i = 0; i < errors.size(); ++i) {
      if (i > 0)
        combined += "; ";
      combined += errors[i];
    }
    return combined;
  }

  // ==================== 显式修正（调用方主动调用） ====================
  Result<bool, RichError> applyDefaultIfInvalid() {
    auto error = check();
    if (!error.has_value()) {
      return Result<bool, RichError>::success(false);
    }

    applyProfile(PROFILE_LOCAL);

    auto errorAfterFix = check();
    if (errorAfterFix.has_value()) {
      return Result<bool, RichError>::error(
          RichError{
          RichError::ErrorCode::INVALID_CONFIG,
          "ClientConfig: default config still invalid after applyProfile"});
    }

    return Result<bool, RichError>::success(true);
  }
};

class OPC_UA_Client final  {
public:
  // 使用现代 C++ 领域类型，彻底取代 UA_* 类型 
  struct NodeId {
    uint16_t ns;
    std::string id;
    S7DataType dataType;
  };
  struct ReadValue {
    NodeId node;
  };
  struct WriteValue {
    NodeId node;
    ValueType value;
  };
  struct ReadResult {
    uint32_t rawStatus; // 仅日志/调试用，业务层可忽略
    std::optional<ValueType> value;
  };
  struct WriteResult {
    enum class Status { Good, Bad } status;
    uint32_t rawStatus; // 仅日志/调试用，业务层可忽略
  };

  static Result<std::unique_ptr<OPC_UA_Client>, RichError>
  create(const std::string &endpointUrl, ClientConfig config, bool useDefault);

  static Result<std::unique_ptr<OPC_UA_Client>, RichError>
  createWithSdk(std::shared_ptr<ISdk> sdk, const std::string &endpointUrl,
                ClientConfig config, bool usedefault = false,bool enableWatchDog=true);

  OPC_UA_Client(const OPC_UA_Client &) = delete;
  OPC_UA_Client &operator=(const OPC_UA_Client &) = delete;
  OPC_UA_Client(OPC_UA_Client &&other) = delete;
  OPC_UA_Client &operator=(OPC_UA_Client &&other) = delete;

  ~OPC_UA_Client() ;
  // “调用期间禁止修改/析构入参 vector”
  Result<std::vector<OPC_UA_Client::ReadResult>, RichError>
  batchRead(const std::vector<ReadValue> &batchReadNodes);
  // “调用期间禁止修改/析构入参 vector”
  Result<std::vector<OPC_UA_Client::WriteResult>, RichError>
  batchWrite(const std::vector<WriteValue> &batchWriteNodes);

  // 需要业务线程在定期巡检（例如每 1 秒）中调用 checkConnected()
  Result<ConnectionState, RichError> checkConnected() ;
  // 重建失败时旧 pImpl 保持不变，上层可再次调用重试
  // recreate/shutdown 替换前等 m_inFlight == 0
  // 契约：进入 API 后要么被明确拒绝，要么完整执行完
  // 代价：recreate/shutdown 会阻塞在长操作上（用有界等待 + 超时兜底
  Result<Unit, RichError> recreateGiveUpClient() ;
  // 业务可以通过这里获取客户端的存活状态
  // 语义：
  //   RUNNING     —— 三元组全就绪，可正常读写
  //   RECOVERING  —— 自愈窗口内，看门狗正在尝试恢复；此时读写均快速失败
  //   GIVEN_UP    —— 自愈窗口耗尽或不可恢复；需调用 recreateGiveUpClient()
  // 注意：本 API 走 ApiLease 入口，m_recreating / m_terminated 期间会返回错误。
  Result<LifeState, RichError> checkLifeState() ;
  //  业务主动设置Client状态为Give-Up
  void setGiveUpSignal();
  // 仅测试使用
  void pumpWatchdogForTest();

  // create() 返回后，客户端一定已连接，调用方可以立即使用
  Result<Unit, RichError> connect();
  // 配合connect()使用
  Result<Unit, RichError> disconnect();
  // shutdown():
  //   - 幂等：可随时调用；已关闭状态下调用返回 success。
  //   - 阻塞：本函数会等待任务计数器归零，超时上限为
  //           max(timeoutMs, 2×watchdogIntervalMs)。
  //   - 超时后强制清理，并记录 warn 日志 + 计数器。
  //   - 并发调用安全：两个线程同时调用，仅一个执行关闭，另一个返回 success。
  Result<Unit,RichError> shutdown() ;

  
  // 仅为了测试T8-C使用
  bool getRecreatingStatus();

  //  独立的外置对象，确保整个对象析构时，部分未完成线程还能调用某些对象
  struct RecreateSync {
    // pImpl（外层指针）、Impl::m_impl、Impl 内部成员（如 m_config、m_impl）
    mutable std::mutex m_lock;
    // shutdown() 与 recreate() 两个长事务之间的互斥
    mutable std::mutex m_recreateLock;
    std::atomic<bool> m_recreating{false};
    std::atomic<bool> m_terminated{false};
    std::condition_variable m_recreateCv;

    // 该对象的所有事务计数部分，重建部分需要确保该事务计数=0下才能进行
    int m_inFlight = 0; // 在 m_recreatelock 下读写
    std::condition_variable m_inFlightCv;

    // member variable(used for recreare pImpl)
    std::string m_endpointUrl;
    ClientConfig m_config;                  // 保存初始配置用于重建
    std::shared_ptr<ISdk> m_sdkForRecreate; // 保存同一对象，方便Mock测试
  };
private:

  std::string connectErrorStateToString(ConnectErrorState state);
  std::string disconnectErrorStateToString(DisconnectErrorState state);
  RichError::ErrorCode toRichErrorCode(ConnectErrorState s); 
  RichError::ErrorCode toRichErrorCode(DisconnectErrorState s);
  /*
    为了避免shutdown()后对象由空->非空,需要确保shutdown()调用时代表整个OPC_UA_Client对象的终结，建立了私有内部清理函数，表示内部清理使用
  */
  Result<Unit,RichError> shutdownInternal();

  explicit OPC_UA_Client(const std::string endpointUrl, ClientConfig config,
                         std::shared_ptr<ISdk> sdk);
  bool allowUseAPI() {
    return !m_recreateSync->m_terminated.load(std::memory_order_acquire);
  }

  // 关键：前向声明 + 不透明指针
  struct Impl;
  std::shared_ptr<Impl> pImpl;
  // Impl需要的回调上下文
  struct CallbackContext;
  // 使用 RAII 包装器
  struct UA_ClientDeleter ;
 
  std::shared_ptr<RecreateSync> m_recreateSync; // 与 RecreateGuard 共享
};