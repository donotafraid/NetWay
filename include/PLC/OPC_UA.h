#ifndef OPC_UA_H
#define OPC_UA_H

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>
#include <utility>             // std::exchange

#include <string>
#include <iostream>
#include "Rust_error_deal/error_deal.h"
#include <snap7.h>
#include <spdlog/spdlog.h>
#include "PLC/Struct.h"
#include "PLC/IStringLengthProbe.h"
#include "PLC/IDeviceReader.h"
#include "PLC/ByteOrderConverter.h"
#include "PLC/UAGuard.h"

class OPC_UA_Client;

class S7_Client {
public:
  static Result<std::unique_ptr<S7_Client>, RichError>
  create(const std::string ip_Address, int rack, int slot) {

    auto member_var = Cli_Create();
    auto client = std::unique_ptr<S7_Client>(
        new S7_Client(ip_Address, rack, slot, member_var));
    auto connectResult = client->connect();

    if (connectResult.is_fail()) {
      return Result<std::unique_ptr<S7_Client>, RichError>::error(
          RichError{connectResult.get_error()->what()});
    } else {
      return Result<std::unique_ptr<S7_Client>, RichError>::success(
          std::move(client));
    }
  }

  S7_Client(const S7_Client &) = delete;
  S7_Client &operator=(const S7_Client &) = delete;
  S7_Client(S7_Client &&other) noexcept; // 转移指针并将other置null
  S7_Client &operator=(S7_Client &&other) noexcept; // 转移指针并将other置null

  ~S7_Client() noexcept;
  Result<Unit, RichError> batchRead(int DB_Number, int Start_Position,
                                    int Read_Size,
                                    uint8_t *SourceData_var) const;
  Result<Unit, RichError> batchWrite(int DB_Number, int Start_Position,
                                     int Read_Size, uint8_t *SourceData_var);

private:
  int m_rack;
  int m_slot;
  std::string m_ip_Address;
  S7Object m_client_var;

  mutable std::recursive_mutex lock;

  void cleanupClient();
  bool isConnectedInternal() const;
  Result<bool, RichError> connect();
  bool isConnected();

  explicit S7_Client(const std::string ip_Address, int rack,int slot,const S7Object &object);
};

class S7_Access : public IDeviceReader, public IStringLengthProbe {
public:
  explicit S7_Access(const std::string ip_Address, int rack, int slot)
      : m_client_var(Cli_Create()), m_ip_Address(ip_Address), m_rack(rack),
        m_slot(slot) {
    Destbuffer.reserve(10000);
    Sourcebuffer.reserve(10000);
    tmpBuffer.reserve(10000);
    Destbuffer.resize(10000);
    Sourcebuffer.resize(10000);
    tmpBuffer.resize(10000);

    m_client_var = Cli_Create();
    if (!m_client_var) {
      spdlog::error("Failed to create S7 client");
    }
  }

  ~S7_Access() {
    disconnect();
    cleanupClient();
  };

  // (IDeviceReader)
  Result<std::unordered_map<std::string, ValueType>, RichError>
  batchRead(std::vector<std::string> &requestVec) override;
  Result<bool, RichError>
  batchWrite(std::vector<WriteRequest> &requestVec) override;
  Result<bool, RichError> connect() override;
  Result<bool, RichError> reconnect(int maxRetries, int retryDelayMs) override;
  Result<bool, RichError> isConnected() override;
  Result<bool, RichError> disconnect() override;
  void
  setAddressMap(std::unordered_map<std::string, PhysicalAddress> &map) override;

  // 构建期接口 (IStringLengthProbe)
  std::optional<int> probeStringLength(int varByteOffset) override;

private:
  // 内部检查连接状态（不触发重连）
  Result<bool, RichError> isConnectedInternal() const;

  // 清理客户端资源
  void cleanupClient();

  std::unordered_map<std::string, PhysicalAddress>
      m_addressMap; // 逻辑名 -> 物理地址
  std::vector<uint8_t> Sourcebuffer;
  std::vector<uint8_t> Destbuffer;
  std::vector<uint8_t> tmpBuffer;
  int m_rack;
  int m_slot;
  S7Object m_client_var;
  const std::string m_ip_Address;
  mutable std::mutex m_mutex;
  bool m_isConnected = false; // 缓存连接状态

  Result<bool, RichError> read(int DB_Number, int Start_Position, int Read_Size,
                               uint8_t *SourceData_var);
  Result<bool, RichError> write(int DB_Number, int Start_Position,
                                int Read_Size, uint8_t *SourceData_var);

  Result<int, RichError> meastureStringObjectLength(int startPos);

  // 辅助函数模板
  template <typename VariantType, typename TargetType>
  bool tryGetVariantValue(const VariantType &variant, TargetType &outValue) {
    if (const TargetType *pValue = std::get_if<TargetType>(&variant)) {
      outValue = *pValue;
      return true;
    }
    return false;
  }

  Result<bool, RichError>
  TransformBytesToDataType(std::vector<uint8_t> &SrcBuffer,
                           std::vector<uint8_t> &destBuffer,
                           PhysicalAddress &dataQuality, ValueType &dataVar);

  void TransformDataTypeToBytes(ValueType &VariableItem,
                                std::vector<uint8_t> &dataBuffer,
                                PhysicalAddress &dataQuality);
};

class OPCUA_Access : public IDeviceReader, public IStringLengthProbe {
public:
  OPCUA_Access(const std::string &ip_Address, int nameSpace, int port,const std::string &identifier)
      : m_ip_Address(ip_Address), m_nameSpace(nameSpace), m_port(port) {

    // 创建客户端
    ClientConfig defaultConfig;
    auto result = OPC_UA_Client::create(identifier,defaultConfig).value_or(nullptr);
    if(result)
    {
      client_pointer = std::move(result);
    }
  }

  ~OPCUA_Access() noexcept{
    CleanupBatchNodes();
  };

  // (IDeviceReader)
  Result<std::unordered_map<std::string, ValueType>, RichError>
  batchRead(std::vector<std::string> &requestVec) override;
  Result<bool, RichError>
  batchWrite(std::vector<WriteRequest> &requestVec) override;
  Result<bool, RichError> connect() override;
  Result<bool, RichError> reconnect(int maxRetries, int retryDelayMs) override;
  Result<bool, RichError> isConnected() override;
  Result<bool, RichError> disconnect() override;
  void
  setAddressMap(std::unordered_map<std::string, PhysicalAddress> &map) override;

  // 构建期接口 (IStringLengthProbe)
  std::optional<int> probeStringLength(int varByteOffset) override;

  void Set_Read_NodeID(UA_ReadValueId &nodeID, PhysicalAddress &node);

  void PrepareBatchRead(const std::vector<std::string> &requestVec);
  void PrepareBatchWrite(const std::vector<WriteRequest> &requestVec);

  void Clear_Read_Respondse();
  void Clear_Write_Respondse();
  void CleanupBatchNodes();

private:
  std::unordered_map<std::string, PhysicalAddress>
      m_addressMap; // 逻辑名 -> 物理地址
  std::unordered_map<std::string, UA_WriteValue>
      m_writeValueMap; // 逻辑名 -> 物理地址

  std::vector<UA_ReadValueId> m_batchReadNodes;
  std::vector<UA_Variant> m_batchReadVariant;

  std::vector<UA_WriteValue> m_batchWriteNodes;

  UA_Client *m_client_pointer  = nullptr;
  std::unique_ptr<OPC_UA_Client> client_pointer= nullptr;

  int m_port;
  int m_nameSpace;
  std::string m_ip_Address;
  bool m_batchNodesValid = false;
  bool m_batchWriteNodesValid = false;

  Result<bool, RichError> isConnectedInternal() const;

  std::pair<std::string, std::string>
  extractPureNodeIdRobust(const std::string &input);
  void UA_Variant_steal(UA_Variant *src, UA_Variant *dst) {
    // 1. 初始化目标 Variant
    UA_Variant_init(dst);

    // 2. 手动转移所有资源
    dst->type = src->type;
    dst->data = src->data;
    dst->arrayLength = src->arrayLength;
    dst->arrayDimensions = src->arrayDimensions;
    dst->arrayDimensionsSize = src->arrayDimensionsSize;
    dst->storageType = src->storageType;

    // 3. 将源 Variant 置空
    UA_Variant_init(src); // 这个函数会将所有字段重置为 0/NULL
  }

  // 辅助函数模板
  template <typename VariantType, typename TargetType>
  bool tryGetVariantValue(const VariantType &variant, TargetType &outValue) {
    if (const TargetType *pValue = std::get_if<TargetType>(&variant)) {
      outValue = *pValue;
      return true;
    }
    return false;
  }
};


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
  RUNNING,    ///< SDK 自检全好
  RECOVERING, ///< SDK 仍在自愈（connectStatus==GOOD 但 channel/session
              ///< 未就绪）——
              //  此时只读不重连、读写快速失败
  GIVEN_UP, ///< connectStatus!=GOOD（SDK 已放弃）——
            //  才进入看门狗主动 reconnect；等待60s或经历一定次数失败后 GIVEN_UP
};

enum class DisconnectErrorState {
  SHUTDOWN,              // 检测到shutdown =true
  NULLPTR,               // 检测到client=nullptr
  THREADBUSY,            // 检测到CAS状态竞争
  TIMEOUT_NOTDISCONNECT, // TIMEOUT分为两种情况：1.发起超时：在超时分支收尾处没有检测到disconnect成功;2.在途被放弃：在超时分支收尾处检测到disconnect成功
  DISCONNECT_FAIL, // 检测到异步断连失败
  TASKWORKING,     // 检测到其他任务未完成
  UNKNOWN          // 检测到CAS状态机的未知错误
};

// 首先定义连接错误枚举
enum class ConnectErrorState {
  SHUTDOWN,
  NULLPTR,
  THREADBUSY,
  UNKNOWN,
  CONNECT_FAILED,
  TIMEOUT_NOTCONNECT,//TIMEOUT分为两种情况：1.发起超时：在超时分支收尾处没有检测到connect连接成功；2.在途被放弃：在超时分支收尾处检测到connect连接成功
  TASKWORKING//存在其他read/write/disconnect/connect正在运行
};

struct ClientConfig {
  // ==================== 超时配置 ====================
  uint32_t timeoutMs =
      10000; // 单次读写请求超时（毫秒），本地网络 5000，远程/4G 15000-30000
  uint32_t sessionTimeoutMs =
      600000; // 会话超时（毫秒），频繁操作 600000，长时间空闲 3600000
  uint32_t secureChannelLifeTimeMs =
      600000; // 安全通道生命周期（毫秒），应 ≥ sessionTimeoutMs
  uint32_t maxTotalWaitMs = 0;
       //单次读写请求超时所允许的最大等待时间，断线窗口内单次调用可能阻塞 2×timeoutMs,设置大小起码是偶数*timeoutMs

  // ==================== 重试配置 ====================
  uint32_t maxRetries = 3; // 额外重试次数（0表示不重试，但至少执行一次请求）
  uint32_t retryBackoffBaseMs =
      1000; // 重试退避基数（毫秒），实际延迟 = base * (2^n)
  uint32_t retryMaxBackoffMs =
      8000; // 重试最大退避时间（毫秒），防止无限增长

  // noReconnect =
  // false->启用自动重连，true=禁用;默认禁用;看门狗重连与 noReconnect
  // 无关、noReconnect 只管 SDK
  // 内部"是设计意图;看门狗和同步调用函数来进行可能的重连
  // noNewSession=true -> Session 失效后立即 give-up，禁止自动重建——“不恢复模式”
  // 看门狗重连与 noNewSession
  // noNewSession 只管 SDK
  // 内部"是设计意图;看门狗进行可能的重连
  // noRecoonect , noNewSession 默认设为true，外部不该修改

  // ==================== 心跳 & 保活配置 ====================
  uint32_t connectivityCheckIntervalMs =
      10000; // 连接检查间隔（毫秒），稳定网络 30000-60000，不稳定 5000-10000
  uint32_t watchdogIntervalMs =
      5000; // WatchDog 检查间隔（毫秒），建议 5000-30000

  // ==================== 网络环境预设 ====================
  enum NetworkProfile {
    PROFILE_LOCAL,   // 本地网络：低延迟、高稳定
    PROFILE_REMOTE,  // 远程网络：中延迟、中稳定
    PROFILE_MOBILE,  // 4G/5G 网络：高延迟、低稳定
    PROFILE_UNSTABLE // 不稳定网络（工业现场）
  };

  // 应用预设网络配置
  void applyProfile(NetworkProfile profile) {
    switch (profile) {
    case PROFILE_LOCAL:
      timeoutMs = 5000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 2;
      retryBackoffBaseMs = 500;
      retryMaxBackoffMs = 4000;

      // maxTotalWaitMs 约束：
      // 1. > timeoutMs (5000)
      // 2. >= timeoutMs + retryMaxBackoffMs (5000 + 4000 = 9000)
      // 3. >= 2 * timeoutMs * (maxRetries+1) (2 * 5000 * 3 = 30000)
      // 4. < sessionTimeoutMs (600000)
      // 取最大值：max(5001, 9000, 30000) = 30000
      maxTotalWaitMs = 30000; // 30秒

      connectivityCheckIntervalMs = 30000;
      watchdogIntervalMs = 10000;
      break;

    case PROFILE_REMOTE:
      timeoutMs = 15000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 3;
      retryBackoffBaseMs = 1000;
      retryMaxBackoffMs = 10000;

      // maxTotalWaitMs 约束：
      // 1. > timeoutMs (15000)
      // 2. >= timeoutMs + retryMaxBackoffMs (15000 + 10000 = 25000)
      // 3. >= 2 * timeoutMs * (maxRetries+1) (2 * 15000 * 4 = 120000)
      // 4. < sessionTimeoutMs (600000)
      // 取最大值：max(15001, 25000, 120000) = 120000
      maxTotalWaitMs = 120000; // 120秒（2分钟）

      connectivityCheckIntervalMs = 15000;
      watchdogIntervalMs = 8000;
      break;

    case PROFILE_MOBILE:
      timeoutMs = 30000;
      sessionTimeoutMs = 600000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 4;
      retryBackoffBaseMs = 2000;
      retryMaxBackoffMs = 20000;

      // maxTotalWaitMs 约束：
      // 1. > timeoutMs (30000)
      // 2. >= timeoutMs + retryMaxBackoffMs (30000 + 20000 = 50000)
      // 3. >= 2 * timeoutMs * (maxRetries+1) (2 * 30000 * 5 = 300000)
      // 4. < sessionTimeoutMs (600000)
      // 取最大值：max(30001, 50000, 300000) = 300000
      maxTotalWaitMs = 300000; // 300秒（5分钟）

      connectivityCheckIntervalMs = 10000;
      watchdogIntervalMs = 5000;
      break;

    case PROFILE_UNSTABLE:
      timeoutMs = 20000;
      sessionTimeoutMs = 300000;
      secureChannelLifeTimeMs = sessionTimeoutMs;

      maxRetries = 5;
      retryBackoffBaseMs = 1000;
      retryMaxBackoffMs = 15000;

      // maxTotalWaitMs 约束：
      // 1. > timeoutMs (20000)
      // 2. >= timeoutMs + retryMaxBackoffMs (20000 + 15000 = 35000)
      // 3. >= 2 * timeoutMs * (maxRetries+1) (2 * 20000 * 6 = 240000)
      // 4. < sessionTimeoutMs (300000)
      // 取最大值：max(20001, 35000, 240000) = 240000
      maxTotalWaitMs = 240000; // 240秒（4分钟）

      connectivityCheckIntervalMs = 5000;
      watchdogIntervalMs = 3000;
      break;
    }
  }

   // ==================== 只读校验（不修改任何成员） ====================
  std::optional<std::string> check() const {
    // 收集所有错误信息
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

      // 使用 uint64_t 安全计算预算下界：2 * timeoutMs * (maxRetries + 1)
      uint64_t requiredBudget =
          2ULL * timeoutMs * (static_cast<uint64_t>(maxRetries) + 1);
      if (requiredBudget > std::numeric_limits<uint32_t>::max()) {
        // 如果预算下界超过 uint32_t 范围，则任何有效的 maxTotalWaitMs
        // 都不可能满足
        add_error("maxTotalWaitMs cannot satisfy required budget (overflow)");
      } else if (maxTotalWaitMs < static_cast<uint32_t>(requiredBudget)) {
        add_error("maxTotalWaitMs (" + std::to_string(maxTotalWaitMs) +
                  ") must be >= 2*timeoutMs*(maxRetries+1) = " +
                  std::to_string(requiredBudget) +
                  " (timeoutMs=" + std::to_string(timeoutMs) +
                  ", maxRetries=" + std::to_string(maxRetries) + ")");
      }
    } else {
      add_error("请调用 applyProfile() 或 applyDefaultIfInvalid()");
    }

    if (errors.empty()) {
      return std::nullopt; // 校验通过
    }

    // 拼接所有错误信息
    std::string combined;
    for (size_t i = 0; i < errors.size(); ++i) {
      if (i > 0)
        combined += "; ";
      combined += errors[i];
    }
    return combined;
  }

  // ==================== 显式修正（调用方主动调用） ====================
  // 返回值：true 表示配置被修正为默认值，false 表示配置原本就合法
  Result<bool,RichError> applyDefaultIfInvalid() {
    auto error = check();
    if (!error.has_value()) {
      return Result<bool,RichError>::success(false); // 配置合法，无需修正
    }

    // 应用默认 LOCAL 配置
    applyProfile(PROFILE_LOCAL);

    // 二次校验：确保修正后的配置完全合法
    auto errorAfterFix = check();
    if (errorAfterFix.has_value()) {
      // 理论上不应发生，若发生说明预设本身有 bug，必须暴露
    return Result<bool, RichError>::error(RichError{"default config exist problem"}); 
    }

    return Result<bool, RichError>::success(true); // 配置已修正为默认 LOCAL 
  }
};

class OPC_UA_Client {
public:
  // 使用现代 C++ 领域类型，彻底取代 UA_* 类型 (R-11 核心要求)
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
  // GIVEN_UP 计时依赖业务 ~1s 轮询
  Result<ConnectionState, RichError> checkConnected() ;
  // 重建失败时旧 pImpl 保持不变，上层可再次调用重试
  Result<Unit, RichError> recreateGiveUpClient() ;
  // 业务可以通过这里获取客户端的存活状态
  Result<LifeState, RichError> checkLifeState() ;

  // create() 返回后，客户端一定已连接，调用方可以立即使用
  Result<Unit, RichError> connect();
  // 幂等、可随时调用,需要等待pImpl对象内部任务全部完成,以及清理pImpl对象,存在阻塞
  // 超时后记录告警并强制 proceed（在途任务持有的 shared_ptr 快照会保证
  //      UA_Client 生命周期安全，可安全放弃等待
  /**
   * @brief 关闭客户端，释放所有资源
   *
   * 最坏阻塞上界：
   *   ≈ max(timeoutMs, 2×watchdogIntervalMs)   // 等待在途任务自觉退出
   *   + timeoutMs                                // disconnect()
   * 排队阻塞（如有）
   *   + 看门狗残余循环                            // 至多 watchdogIntervalMs
   *   ≈ 3×timeoutMs + 3×watchdogIntervalMs
   *
   * 使用 MOBILE 档（timeoutMs=30000, watchdogIntervalMs=5000）时，
   * 最坏 ≈ 3×30000 + 3×5000 = 105000ms ≈ 105秒
   */
  Result<Unit,RichError> shutdown() ;

  /*
    为了避免shutdown()后对象由空->非空,需要确保shutdown()调用时代表整个OPC_UA_Client对象的终结，建立了私有内部清理函数，表示内部清理使用
  */
  Result<Unit,RichError> shutdownInternal();

private:
  // 使用 RAII 包装器
  struct UA_ClientDeleter {
    void operator()(UA_Client *c) const noexcept {
      if (c)
        UA_Client_delete(c);
    }
  };
  using UA_ClientPtr = std::shared_ptr<UA_Client>;

  std::string connectErrorStateToString(ConnectErrorState state);
  explicit OPC_UA_Client(const std::string endpointUrl, ClientConfig config);
  bool allowUseAPI(){
    return m_recreating.load(std::memory_order_acquire) == false;
  }

  // 关键：前向声明 + 不透明指针
  struct Impl;
  std::shared_ptr<Impl> pImpl;
  // pImpl（外层指针）、Impl::m_impl、Impl 内部成员（如 m_config、m_impl）
  mutable std::mutex m_lock;
  // shutdown() 与 recreate() 两个长事务之间的互斥
  mutable std::mutex m_recreateLock;
  std::atomic<bool>m_recreating{false};
  std::atomic<bool>m_terminated {false};

  // member variable(used for recreare pImpl)
  std::string m_endpointUrl;
  ClientConfig m_config; // 保存初始配置用于重建
};

class OwnedVariant {
  public:
  // ============ 默认构造函数 ============
  OwnedVariant() noexcept
      : m_variant() // 零初始化，相当于 UA_Variant_init
  {
  }

  // ============ 从原生 UA_Variant 接管所有权 ============
  explicit OwnedVariant(UA_Variant &&variant) noexcept : m_variant(variant) {
    // 关键：将源 Variant 置空，防止其析构时释放资源
    UA_Variant_init(
        &variant); // 或者手动置零：variant.data = nullptr; variant.type =
                   // nullptr; variant.arrayLength = 0;
  }

  // ============ 析构函数：释放资源 ============
  ~OwnedVariant() noexcept { UA_Variant_clear(&m_variant); }

  // ============ 拷贝构造 ============
  // 删除：禁止拷贝，防止意外的深拷贝开销
  OwnedVariant(const OwnedVariant &) = delete;

  // ============ 拷贝赋值 ============
  OwnedVariant &operator=(const OwnedVariant &) = delete;

  // ============ 移动构造 ============
  OwnedVariant(OwnedVariant &&other) noexcept
      : m_variant() // 先初始化一个空 Variant
  {
    // 1. 将对方的资源 "偷" 过来
    m_variant = std::exchange(other.m_variant, UA_Variant());
    // 注意：std::exchange 会读取 other.m_variant 的值，然后用 UA_Variant()
    // 覆盖它 UA_Variant() 是一个值初始化，相当于 UA_Variant_init，所有字段为
    // 0/nullptr

    // 或者更显式的写法（不依赖 std::exchange）：
    // m_variant = other.m_variant;           // 浅拷贝指针
    // UA_Variant_init(&other.m_variant);     // 置空源对象
  }

  // ============ 移动赋值 ============
  OwnedVariant &operator=(OwnedVariant &&other) noexcept {
    if (this != &other) { // 防止自我移动
      // 1. 释放当前持有的资源
      UA_Variant_clear(&m_variant);

      // 2. 偷取对方的资源
      m_variant = std::exchange(other.m_variant, UA_Variant());
    }
    return *this;
  }

  // ============ 访问器 ============
  UA_Variant *get() noexcept { return &m_variant; }
  const UA_Variant *get() const noexcept { return &m_variant; }

  UA_Variant *operator->() noexcept { return &m_variant; }
  const UA_Variant *operator->() const noexcept { return &m_variant; }

  UA_Variant &operator*() noexcept { return m_variant; }
  const UA_Variant &operator*() const noexcept { return m_variant; }

private:
  UA_Variant m_variant;
};

#endif