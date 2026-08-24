#ifndef OPC_UA_H
#define OPC_UA_H

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

#include <string>
#include <iostream>
#include "Rust_error_deal/error_deal.h"
#include <snap7.h>
#include <spdlog/spdlog.h>
#include "PLC/Struct.h"
#include "PLC/IStringLengthProbe.h"
#include "PLC/IDeviceReader.h"
#include "PLC/ByteOrderConverter.h"

// 连接状态的枚举（比 bool 更精确）
enum class ConnectionState {
  UNINITIALIZED, // 对象未创建
  OBJECT_ONLY,   // 对象存在但未连接（僵尸）
  CONNECTING,    // 连接中
  CONNECTED,     // 已连接
  ERROR          // 错误状态
};

class ReadResponseGuard;
class OPC_UA_Client;
class UAVariantGuard;

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

  // 构建连接参数
  std::string buildConnectionInfo() const;

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

class UAVariantGuard {
public:
  UAVariantGuard(UA_Variant *var) : m_access(var) {}

  ~UAVariantGuard() {
    if (m_access) {
      UA_Variant_clear(m_access);
    }
  }

  // 禁止拷贝
  UAVariantGuard(const UAVariantGuard &) = delete;
  UAVariantGuard &operator=(const UAVariantGuard &) = delete;

  // 允许移动
  UAVariantGuard(UAVariantGuard &&other) noexcept : m_access(other.m_access) {
    other.m_access = nullptr;
  }

  UAVariantGuard &operator=(UAVariantGuard &&other) noexcept {
    if (this != &other) {
      if (m_access)
        UA_Variant_clear(m_access);
      m_access = other.m_access;
      other.m_access = nullptr;
    }
    return *this;
  }

  void release() {
    m_access = nullptr; // 不再自动清理
  }

  UA_Variant &get() const 
  {
    return *m_access;
  }

private:
  UA_Variant *m_access;
};

class OPC_UA_Client {
public:
  static Result<std::unique_ptr<OPC_UA_Client>, RichError>
  create(int port, int m_nameSpace, const std::string &endpointUrl) {

    auto member_pointer = UA_Client_new();
    auto client = std::unique_ptr<OPC_UA_Client>(
        new OPC_UA_Client(port, m_nameSpace, endpointUrl, member_pointer));
    auto connectResult = client->connect();

    if (connectResult.is_fail()) {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>(
          RichError{connectResult.unwrap_err().what()});
    } else {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>(std::move(client));
    }
  }

  OPC_UA_Client(const OPC_UA_Client &) = delete;
  OPC_UA_Client &operator=(const OPC_UA_Client &) = delete;
  OPC_UA_Client(OPC_UA_Client &&other) noexcept; // 转移指针并将other置null
  OPC_UA_Client &
  operator=(OPC_UA_Client &&other) noexcept; // 转移指针并将other置null

  ~OPC_UA_Client() noexcept;
  Result<std::vector<UAVariantGuard>, RichError>
  batchRead(const std::vector<UA_ReadValueId> &m_batchReadNodes) const;
  Result<std::vector<UA_StatusCode>, RichError>
  batchWrite(const std::vector<UA_WriteValue> &m_batchWriteNodes);

private:
  UA_Client *m_client_pointer = nullptr;

  mutable std::recursive_mutex lock;

  int m_port;
  int m_nameSpace;
  std::string m_ip_Address;
  std::string endpointUrl;

  // 清理客户端资源
  void cleanupClient();

  bool isConnectedInternal() const;

  // trait function
  Result<bool, RichError> waitForSessionActivation(int timeoutMs);

  /**
   * @brief 配置客户端参数（超时、重试策略等）
   */
  void configureClient();

  Result<bool, RichError> connect();
  bool isConnected();
  Result<bool, RichError> disconnect();

  explicit OPC_UA_Client(int port, int m_nameSpace,
                         const std::string &endpointUrl, UA_Client *client);
};

class OPCUA_Access : public IDeviceReader, public IStringLengthProbe {
public:
  OPCUA_Access(const std::string &ip_Address, int nameSpace, int port,const std::string &identifier)
      : m_ip_Address(ip_Address), m_nameSpace(nameSpace), m_port(port) {

    // 创建客户端
    auto result = OPC_UA_Client::create(m_port,m_nameSpace,identifier);
    if(result.is_success())
    {
      client_pointer = std::move(result.unwrap_returnLeftValue());
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

  /**
   * @brief 配置客户端参数（超时、重试策略等）
   */
  void configureClient();

  Result<bool, RichError> batchWrite();

  void Set_Read_NodeID(UA_ReadValueId &nodeID, PhysicalAddress &node);

  void PrepareBatchRead(const std::vector<std::string> &requestVec);
  void PrepareBatchWrite(const std::vector<WriteRequest> &requestVec);

  UA_Client *getClient();
  void Clear_Read_Respondse();
  void Clear_Write_Respondse();
  void CleanupBatchNodes();

private:
  std::unordered_map<std::string, PhysicalAddress>
      m_addressMap; // 逻辑名 -> 物理地址
  std::unordered_map<std::string, UA_WriteValue>
      m_writeValueMap; // 逻辑名 -> 物理地址

  UA_ReadResponse m_read_response;
  std::vector<UA_ReadValueId> m_batchReadNodes;
  std::vector<UA_Variant> m_batchReadVariant;

  std::vector<UA_WriteValue> m_batchWriteNodes;

  UA_Client *m_client_pointer = nullptr;
  std::unique_ptr<OPC_UA_Client> client_pointer= nullptr;

  UA_WriteResponse m_write_response;

  int m_port;
  int m_nameSpace;
  std::string m_ip_Address;
  mutable std::mutex m_mutex; // 线程安全
  bool m_isConnected = false; // 缓存连接状态
  bool m_batchNodesValid = false;
  bool m_batchWriteNodesValid = false;

  // 清理客户端资源
  void cleanupClient();

  // 构建连接 URL
  std::string buildEndpointUrl() const;

  Result<bool, RichError> isConnectedInternal() const;

  // trait function
  Result<bool, RichError> waitForSessionActivation(int timeoutMs);

  Result<bool, RichError> read();
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

  template <typename S7Type>
  struct UATypeTraits; // 特化提供 UA_DataType* 和转换函数

  template<S7DataType> struct S7TypeToUATraits; // 主模板未定义
  template <> struct S7TypeToUATraits<S7DataType::BOOL> {
    using type = UATypeTraits<bool>;
  };
  template <> struct S7TypeToUATraits<S7DataType::BYTE> {
    using type = UATypeTraits<uint8_t>;
  };
  template<S7DataType> struct S7TypeToUATraits; // 主模板未定义
  template <> struct S7TypeToUATraits<S7DataType::INT> {
    using type = UATypeTraits<int16_t>;
  };
  template <> struct S7TypeToUATraits<S7DataType::WORD> {
    using type = UATypeTraits<uint16_t>;
  };
  template<S7DataType> struct S7TypeToUATraits; // 主模板未定义
  template <> struct S7TypeToUATraits<S7DataType::DINT> {
    using type = UATypeTraits<int32_t>;
  };
  template <> struct S7TypeToUATraits<S7DataType::UDINT> {
    using type = UATypeTraits<uint32_t>;
  };
  template <> struct S7TypeToUATraits<S7DataType::DWORD> {
    using type = UATypeTraits<uint32_t>;
  };
  template <> struct S7TypeToUATraits<S7DataType::REAL> {
    using type = UATypeTraits<float>;
  };
  template <> struct S7TypeToUATraits<S7DataType::STRING> {
    using type = UATypeTraits<std::string>;
  };

  // UA->Normal
  template <S7DataType type>
  Result<bool, RichError>
  setNormalScalar(int i, const std::vector<UAVariantGuard> &batchReadVariant,
                  ValueType &dataVar) {
    using Traits = typename S7TypeToUATraits<type>::type;
    auto val = Traits::convert(batchReadVariant[i].get());
    dataVar = val; // 需要值类型与 ValueType 的赋值兼容（你可能需要进一步特化）
    return Result<bool, RichError>{true};
  }

  Result<bool, RichError>
  Set_UA_To_Read_Normal_Scalar(const S7DataType &S7_type, ValueType &dataVar,
                               int i,
                               const std::vector<UAVariantGuard> &batchReadVariant);
  template <typename T>
  void
  Covert_UA_Scalar_To_Specific(T &value, int i,
                               const std::vector<UAVariantGuard> &batchReadVariant) const ;

  // Normal->UA
  Result<bool, RichError> batchSet_Normal_To_Write_UA_Scalar(
      PhysicalAddress &var, UA_WriteValue &WriteNode, const ValueType &value);
  template <typename T>
  Result<bool, RichError>
  batchSet_UA_Scalar_StatusCode(int nameSpace, PhysicalAddress &SourceData_var,
                                T &source_var, UA_WriteValue &destValue);

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

class ReadResponseGuard {
public:
  ReadResponseGuard(OPCUA_Access *access) : m_access(access) {}

  ~ReadResponseGuard() {
    if (m_access) {
      m_access->Clear_Read_Respondse();
    }
  }

  // 禁止拷贝
  ReadResponseGuard(const ReadResponseGuard &) = delete;
  ReadResponseGuard &operator=(const ReadResponseGuard &) = delete;

  // 允许移动
  ReadResponseGuard(ReadResponseGuard &&other) noexcept
      : m_access(other.m_access) {
    other.m_access = nullptr;
  }

  void release() {
    m_access = nullptr; // 不再自动清理
  }

private:
  OPCUA_Access *m_access;
};

class WriteResponseGuard {
public:
  WriteResponseGuard(OPCUA_Access *access) : m_access(access) {}

  ~WriteResponseGuard() {
    if (m_access) {
      m_access->Clear_Write_Respondse();
    }
  }

  // 禁止拷贝
  WriteResponseGuard(const WriteResponseGuard &) = delete;
  WriteResponseGuard &operator=(const WriteResponseGuard &) = delete;

  // 允许移动
  WriteResponseGuard(WriteResponseGuard &&other) noexcept
      : m_access(other.m_access) {
    other.m_access = nullptr;
  }

  void release() {
    m_access = nullptr; // 不再自动清理
  }

private:
  OPCUA_Access *m_access;
};



#endif