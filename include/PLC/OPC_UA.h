#ifndef OPC_UA_H
#define OPC_UA_H

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

#include <fstream>
#include <string>
#include <regex>
#include <iostream>
#include "Rust_error_deal/error_deal.h"
#include "PLC/Struct.h"

static const std::unordered_map<S7DataType, UA_DataType*> s7_to_ua_map = {
    {S7DataType::BOOL,   &UA_TYPES[UA_TYPES_BOOLEAN]},
    {S7DataType::BYTE,   &UA_TYPES[UA_TYPES_BYTE]},
    {S7DataType::INT,    &UA_TYPES[UA_TYPES_INT16]},
    {S7DataType::WORD,   &UA_TYPES[UA_TYPES_UINT16]},
    {S7DataType::DINT,   &UA_TYPES[UA_TYPES_INT32]},
    {S7DataType::UDINT,  &UA_TYPES[UA_TYPES_UINT32]},
    {S7DataType::DWORD,  &UA_TYPES[UA_TYPES_UINT32]},
    {S7DataType::REAL,   &UA_TYPES[UA_TYPES_FLOAT]},
    {S7DataType::STRING, &UA_TYPES[UA_TYPES_STRING]}
};

// 连接状态的枚举（比 bool 更精确）
enum class ConnectionState {
  UNINITIALIZED, // 对象未创建
  OBJECT_ONLY,   // 对象存在但未连接（僵尸）
  CONNECTING,    // 连接中
  CONNECTED,     // 已连接
  ERROR          // 错误状态
};

class S7_Access
{
    public:
      explicit S7_Access(const std::string ip_Address, int rack, int slot)
          : m_client_var(Cli_Create()), m_ip_Address(ip_Address), m_rack(rack),
            m_slot(slot) {
             
            }
            ~S7_Access() { disconnect(); };

            Result<bool, RichError> read(int DB_Number, int Start_Position,
                                         int Read_Size,
                                         uint8_t *SourceData_var);
            Result<bool, RichError> write(int DB_Number, int Start_Position,
                                          int Read_Size,
                                          uint8_t *SourceData_var);
            Result<bool, RichError> connect();
            bool isConnected();
            void disconnect();
            S7Object &getClient();

          private:
            int m_rack;
            int m_slot;
            S7Object m_client_var;
            const std::string m_ip_Address;
};

class OPCUA_Access {
public:
  OPCUA_Access(const std::string &ip_Address, int nameSpace, int port)
      : m_ip_Address(ip_Address), m_nameSpace(nameSpace), m_port(port) {

    try {
      m_readValueNodeID =
          static_cast<UA_ReadValueId *>(UA_malloc(sizeof(UA_ReadValueId)));
      if (!m_readValueNodeID)
        throw std::bad_alloc{};
      UA_ReadValueId_init(m_readValueNodeID);
      UA_Variant_init(&m_read_variant);
      UA_WriteValue_init(&m_writeValue);
    } catch (...) {
      // 清理已经分配的内存
      if (m_readValueNodeID) {
        UA_free(m_readValueNodeID);
      }
      throw; // 重新抛出异常
    }
  }

  ~OPCUA_Access() {
    std::cout << "~OPCUA call " << std::endl;
    if (m_readValueNodeID) {
      UA_free(m_readValueNodeID);
    }
    UA_Variant_clear(&m_read_variant);
    UA_WriteValue_clear(&m_writeValue);
    // Step 2: 释放客户端所有资源（内存、线程、网络句柄等）
    CleanupBatchNodes();
    UA_Client_delete(m_client_pointer);
    m_client_pointer = nullptr; // 避免悬空指针
    std::cout << "~OPCUA call end " << std::endl;
  };

  Result<bool, RichError> read();
  Result<bool, RichError> read_nameSpace();
  Result<bool, RichError> read_variable_from_device(UA_NodeId &nodeID,
                                                    bool reverse_direction);

  // trait function
  Result<bool, RichError> expandNodeIdToString(UA_ExpandedNodeId &id);
  Result<bool, RichError> nodeIdToString(std::string &str, UA_NodeId &nodeID);
  bool isSiemensContainer(const std::string &browseName);
  Result<bool, RichError> waitForSessionActivation(int timeoutMs);
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
  /**
   * @brief 优雅断开连接
   */
  void disconnect();

  /**
   * @brief 配置客户端参数（超时、重试策略等）
   */
  void configureClient();

  //  get function
  ConnectionState getConnectionState() const;
  Result<bool, RichError> ensureConnection();
  Result<bool, RichError> reconnect(int maxRetries, int retryDelayMs);

  Result<bool, RichError> batchWrite();

  template <typename T>
  Result<bool, RichError> write_by_vector(OPCUAModernDataStruct &var,
                                          std::vector<T> &src_vector);
  template <typename T>
  Result<bool, RichError> write_by_scalar(OPCUAModernDataStruct &var,
                                          T &src_vector);

  Result<bool, RichError> Read_UA_Variant_From_PLC();

  Result<bool, RichError>
  Set_UA_To_Read_Normal_Scalar(const S7DataType &S7_type, Dynamic_Value &value,
                               int i);

  Result<bool, RichError>
  Set_Read_UA_Array(std::vector<OPCUAModernDataStruct> &var_map,
                    OPCUAModernDataStruct &var, int &index);
  Result<bool, RichError>
  Set_Normal_To_Write_UA_Vector(OPCUAModernDataStruct &var,
                                std::vector<uint8_t> &m_data_block_buffer);
  Result<bool, RichError>
  batchSet_Normal_To_Write_UA_Vector(OPCUAModernDataStruct &var,
                                     std::vector<uint8_t> &m_data_block_buffer,
                                     int index);
  Result<bool, RichError>
  Set_Normal_To_Write_UA_Scalar(OPCUAModernDataStruct &var,
                                std::vector<uint8_t> &m_data_block_buffer);
  Result<bool, RichError>
  batchSet_Normal_To_Write_UA_Scalar(OPCUAModernDataStruct &var, int index);
  Result<bool, RichError> getValueFromDataPointer(OPCUAModernDataStruct &var);

  // Result<bool, RichError>
  // batchSet_Normal_To_Write_UA_Scalar(OPCUAModernDataStruct &var, int index);

  Result<bool, RichError> ByteDeserialization_To_SpecialType(
      int data_offset, int data_length, S7DataType &data_type_enum,
      std::vector<uint8_t> &m_data_block_buffer, Dynamic_Value &value);

  template <typename T> void Covert_UA_Scalar_To_Specific(T &value, int i);
  template <typename T>
  void Covert_Uint8_Vector_To_Normal_Vector(
      OPCUAModernDataStruct &var, std::vector<uint8_t> &m_data_block_buffer,
      std::vector<T> &dest_vector);
  template <typename T>
  void Covert_Uint8_t_Vector_To_Normal_Scalar(
      OPCUAModernDataStruct &var, std::vector<uint8_t> &m_data_block_buffer,
      T &dest_vector);

  template <typename T>
  void ByteDeserialization_memcpy(T &value, const std::vector<uint8_t> &src,
                                  int data_offset, int data_length);

  Result<bool, RichError> connect();
  bool isConnected();
  void Set_Read_NodeID(UA_ReadValueId &nodeID, OPCUAModernDataStruct &node);

  template <typename T>
  Result<bool, RichError>
  Set_UA_Array_StatusCode(int nameSpace, OPCUAModernDataStruct &SourceData_var,
                          std::vector<T> &source_vector);
  template <typename T>
  Result<bool, RichError>
  Set_UA_Scalar_StatusCode(int nameSpace, OPCUAModernDataStruct &SourceData_var,
                           T &source_var);

  template <typename T>
  Result<bool, RichError>
  batchSet_UA_Array_StatusCode(int nameSpace,
                               OPCUAModernDataStruct &SourceData_var,
                               std::vector<T> &source_vector, int index);
  template <typename T>
  Result<bool, RichError>
  batchSet_UA_Scalar_StatusCode(int nameSpace,
                                OPCUAModernDataStruct &SourceData_var,
                                T &source_var, int index);

  void PrepareBatchRead(std::vector<OPCUAModernDataStruct> &data_vars) {
    // 复用 C++ vector
    if (m_batchNodesValid) {
      std::cout << "PrepareBatchRead skip for the batchReadNodes had initialize"
                << std::endl;
      return;
    }
    m_batchReadNodes.clear();
    m_batchReadNodes.reserve(data_vars.size());
    m_batchReadVariant.clear();
    m_batchReadVariant.reserve(data_vars.size());

    for (auto &var : data_vars) {
      if (var.filter_reason != "" || var.is_array) {
        continue;
      }
      UA_ReadValueId node;
      UA_Variant variant;

      UA_Variant_init(&variant);
      UA_ReadValueId_init(&node);

      Set_Read_NodeID(node, var);

      m_batchReadNodes.push_back(std::move(node));
      m_batchReadVariant.push_back(std::move(variant));
    }

    m_batchNodesValid = true;
  }

  void PrepareBatchWrite(std::vector<OPCUAModernDataStruct> &data_vars) {
    // 复用 C++ vector
    if (m_batchWriteNodesValid) {
      std::cout
          << "PrepareBatchWrite skip for the PrepareBatchWrite had initialize"
          << std::endl;
      return;
    }
    m_batchWriteNodes.clear();
    m_batchWriteNodes.reserve(data_vars.size());

    for (auto &var : data_vars) {
      if (var.filter_reason != "" || var.is_array) {
        continue;
      }

      UA_WriteValue m_writeValue;
      UA_WriteValue_init(&m_writeValue);

      m_batchWriteNodes.push_back(std::move(m_writeValue));
    }

    m_batchWriteNodesValid = true;
  }

  UA_Client *getClient();
  void Clear_HasRead_var_set();
  void Clear_Read_Respondse();
  void Clear_Write_Respondse();
  void CleanupBatchNodes() {
    for (auto &node : m_batchReadNodes) {
      UA_ReadValueId_clear(&node);
    }
    m_batchReadNodes.clear();
    m_batchNodesValid = false;

    for (auto &variant : m_batchReadVariant) {
      UA_Variant_clear(&variant);
    }
    m_batchReadVariant.clear();

    for (auto &node : m_batchWriteNodes) {
      UA_WriteValue_clear(
          &node); // 这会清理 nodeId, value, indexRange 等所有字段
    }
    m_batchWriteNodes.clear();

    m_batchWriteNodesValid = false;
  }
  std::unordered_set<std::string> m_hasRead_var_set;

private:
  ConnectionState m_connectionState = ConnectionState::UNINITIALIZED;
  UA_ReadResponse m_read_response;
  std::vector<UA_ReadValueId> m_batchReadNodes;
  std::vector<UA_Variant> m_batchReadVariant;

  std::vector<UA_WriteValue> m_batchWriteNodes;

  std::unordered_set<std::string> m_visited_set;
  UA_Client *m_client_pointer = nullptr;
  UA_Variant m_read_variant;
  UA_ReadValueId *m_readValueNodeID = nullptr;

  UA_WriteResponse m_write_response;
  UA_WriteValue m_writeValue;

  int m_port;
  int m_nameSpace;
  std::string m_ip_Address;
  bool m_batchNodesValid = false;
  bool m_batchWriteNodesValid = false;
};

#endif