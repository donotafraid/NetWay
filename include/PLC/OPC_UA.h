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