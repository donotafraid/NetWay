#pragma once

#include "PLC/IRawData.h"
#include "PLC_Collector/IDataNode.h"
#include "PLC/Struct.h"
#include "PLC/IStringLengthProbe.h"
#include "PLC/WriteRequestAddres.h"

#include <vector>
#include <string>
#include <set>

// 1. 定义解析策略接口（抽象基类）
class MapperpStrategy {
public:
  virtual ~MapperpStrategy() = default;
  virtual std::vector<std::shared_ptr<IDataNode>>
  map(const RawDataTable &rawTable) const = 0;
  virtual void setProbe(std::shared_ptr<IStringLengthProbe> probe) = 0;
  // ⭐ 新增方法：返回物理地址映射表（只给 Reader 用）
  virtual std::unordered_map<std::string, PhysicalAddress>
  buildAddressMap(std::vector<std::shared_ptr<IDataNode>> &dataVec) = 0;
};

class OpcUaMapper :public MapperpStrategy{
public:
  // 核心映射接口：RawDataTable -> OPCUA 结构体列表
  std::vector<std::shared_ptr<IDataNode>>
  map(const RawDataTable &rawTable) const override;

  void setProbe(std::shared_ptr<IStringLengthProbe> probe) override;

  // ⭐ 新增方法：返回物理地址映射表（只给 Reader 用）
  std::unordered_map<std::string, PhysicalAddress>
  buildAddressMap(std::vector<std::shared_ptr<IDataNode>> &dataVec) override; 

private:
  struct calculateBlock {
    int last_free_byte_offset = 0;
    int current_bit_quality = 0;
    float last_used_var_byte_offset = 0;
    S7DataType last_data_S7_type = S7DataType::UNKNOWN;
  };

  std::shared_ptr<IStringLengthProbe> m_probe; // 依赖抽象
  // ==================== 辅助工具函数 ====================

  // 从 metadata 中安全获取值（忽略大小写）
  std::string getMeta(const RawDataField &field, const std::string &key) const;

  // 解析 OPC UA NodeId 字符串（如 "ns=2;s=Speed"）
  void parseNodeId(const std::string &fullNodeId, int &outNamespace,
                   std::string &outIdentifier) const;

  // 提取父节点名称（用于数组索引拼接）
  std::string extractLastPartWithoutIndex(const std::string &input) const;

  // 判断是否保留变量
  bool shouldKeepVariable(const std::string &var_name,
                          const std::string &node_id,
                          const std::string &type_id,
                          const std::string &browse_name,
                          std::string &filter_reason, bool isArray,
                          std::string &valueRankStr) const;

  // 计算变量偏移量
  Result<bool, RichError>
  calculate_variable_offset(OPCUAModernDataStruct &var,
                            calculateBlock &calStruct) const;

  // ==================== 常量定义 ====================
  const std::set<std::string> SYSTEM_VARIABLES_BLACKLIST = {
      // 设备信息类
      "DeviceRevision", "HardwareRevision", "SoftwareRevision", "Manufacturer",
      "Model", "SerialNumber", "OrderNumber", "RevisionCounter",
      "EngineeringRevision", "DeviceManual", "OperatingMode",

      // OPC UA 模型类
      "SimaticStructuresType", "SimaticOperatingState",

      // 容器节点类
      "DataBlocksGlobal", "DataBlocksInstance", "Counters", "Timers", "Inputs",
      "Outputs", "Memory", "DataBlocksGlobal",

      // 枚举定义类
      "EnumValues"};

  const std::set<std::string> TYPE_NODE_PREFIXES = {
      "VT_", // VariableType
      "V_",  // VariableType instance
      "DT_", // DataType
      "TE_"  // TypeEncoding
  };
};

class S7Mapper : public MapperpStrategy {
public:
  // 核心映射接口：RawDataTable -> OPCUA 结构体列表
  std::vector<std::shared_ptr<IDataNode>>
  map(const RawDataTable &rawTable) const override;

  void setProbe(std::shared_ptr<IStringLengthProbe> probe) override;

  // ⭐ 新增方法：返回物理地址映射表（只给 Reader 用）
  std::unordered_map<std::string, PhysicalAddress>
  buildAddressMap(std::vector<std::shared_ptr<IDataNode>> &dataVec) override; 

private:
  struct calculateBlock {
    int last_free_byte_offset = 0;
    int current_bit_quality = 0;
    float last_used_var_byte_offset = 0;
    S7DataType last_data_S7_type = S7DataType::UNKNOWN;
  };

  std::shared_ptr<IStringLengthProbe> m_probe; // 依赖抽象
  // ==================== 辅助工具函数 ====================

  // 从 metadata 中安全获取值（忽略大小写）
  std::string getMeta(const RawDataField &field, const std::string &key) const;

  // 解析 OPC UA NodeId 字符串（如 "ns=2;s=Speed"）
  void parseNodeId(const std::string &fullNodeId, int &outNamespace,
                   std::string &outIdentifier) const;

  // 提取父节点名称（用于数组索引拼接）
  std::string extractLastPartWithoutIndex(const std::string &input) const;

  // 判断是否保留变量
  bool shouldKeepVariable(const std::string &var_name,
                          const std::string &node_id,
                          const std::string &type_id,
                          const std::string &browse_name,
                          std::string &filter_reason, bool isArray,
                          std::string &valueRankStr) const;

  // 计算变量偏移量
  Result<bool, RichError>
  calculate_variable_offset(OPCUAModernDataStruct &var,
                            calculateBlock &calStruct) const;

  // ==================== 常量定义 ====================
  const std::set<std::string> SYSTEM_VARIABLES_BLACKLIST = {
      // 设备信息类
      "DeviceRevision", "HardwareRevision", "SoftwareRevision", "Manufacturer",
      "Model", "SerialNumber", "OrderNumber", "RevisionCounter",
      "EngineeringRevision", "DeviceManual", "OperatingMode",

      // OPC UA 模型类
      "SimaticStructuresType", "SimaticOperatingState",

      // 容器节点类
      "DataBlocksGlobal", "DataBlocksInstance", "Counters", "Timers", "Inputs",
      "Outputs", "Memory", "DataBlocksGlobal",

      // 枚举定义类
      "EnumValues"};

  const std::set<std::string> TYPE_NODE_PREFIXES = {
      "VT_", // VariableType
      "V_",  // VariableType instance
      "DT_", // DataType
      "TE_"  // TypeEncoding
  };
};