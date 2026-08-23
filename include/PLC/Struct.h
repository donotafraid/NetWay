#pragma once

#include "load_config/Qt_library.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "PLC/S7TypeStruct.h"
#include "PLC/WriteRequestAddres.h"

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

// s7->ua type convert map
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

// UA_String 转 std::string
static std::string uaStringToString(const UA_String &str) {
    if (!str.data || str.length == 0) {
        return "";
    }
    return std::string((char*)str.data, str.length);
}

// NodeId 转字符串（用于集合的键）
static std::string nodeIdToString(const UA_NodeId *nodeId) {
    UA_String str;
    UA_String_init(&str);  // 👈 关键：初始化字符串
    UA_NodeId_print(nodeId, &str);
    std::string result((char*)str.data, str.length);
    UA_String_clear(&str);
    return result;
}


struct OPCUAModernDataStructFromCSV {
  std::string nodeId;          // 节点ID
  std::string browseName;      // 浏览名称
  std::string parentNodeId;    // 父节点ID
  std::string displayName;     // 显示名称
  std::string dataType;        // 数据类型
  std::string accessLevel;     // 访问级别（可选）
  std::string valueRank;       // 值秩（可选）
  std::string arrayDimensions; // 数组维度（可选）
  std::string description;     // 描述（可选）

  // 辅助方法：判断是否为数组
  bool isArray() const {
    return !valueRank.empty() && valueRank != "0" && valueRank != "-1";
  }

  // 获取数组维度
  std::vector<int> getDimensions() const {
    std::vector<int> dims;
    if (!arrayDimensions.empty()) {
      std::stringstream ss(arrayDimensions);
      std::string dim;
      while (std::getline(ss, dim, ',')) {
        if (!dim.empty()) {
          dims.push_back(std::stoi(dim));
        }
      }
    }
    return dims;
  }
};

enum class InputFormat{
  XML,
  BROWSER,
  CSV
};

// 变量节点信息（对应 XML 中的 UAVariable）
// 同时兼容 S7 和 OPC UA 数据结构
struct OPCUAModernDataStruct {
  // ==================== 成员变量 ====================
  // OPC UA 相关字段
  std::string variable_name;
  std::string variable_nodeID;
  UA_NodeId nodeID;
  int namespace_index = 0;
  std::string data_type;
  std::string raw_data_type;
  std::string description;
  int access_level = 0;
  std::string parent_nodeID;
  bool is_array = false;
  std::string arrayDimensions = "";
  std::string browse_name;
  std::string filter_reason;

  // S7 相关字段
  std::string parent_path;
  std::string variable_full_path;
  S7DataType data_type_enum = S7DataType::UNKNOWN;
  int data_block_number = -1;
  float bytes_offset = -1.0f;
  int bit_offset = -1;
  int s7_data_type_length = -1;
  int s7_data_array_length = -1;

  // 数据存储 - 使用 std::variant
  ValueType dataValue;

  // 标识字段
  bool isOPCUAType = true;
  InputFormat buildType = InputFormat::BROWSER;
  bool isFromCSV = false;

  std::optional<int> pendingStringLength;

  // ==================== 辅助函数 ====================

  // 获取当前存储类型的索引（用于调试）
  size_t getValueIndex() const { return dataValue.index(); }

  // 检查是否存储了特定类型
  template <typename T> bool holdsType() const {
    return std::holds_alternative<T>(dataValue);
  }

  // 安全获取值的模板方法
  template <typename T> T getValue() const {
    try {
      return std::get<T>(dataValue);
    } catch (const std::bad_variant_access &) {
      // 如果类型不匹配，返回默认值
      return T{};
    }
  }

  // 安全获取值的指针版本（不抛异常）
  template <typename T> const T *getValueIf() const {
    return std::get_if<T>(&dataValue);
  }

  // ==================== 构造函数 ====================

  // 从 CSV 结构体构造
  explicit OPCUAModernDataStruct(const OPCUAModernDataStructFromCSV &csvData) {
    UA_NodeId_init(&nodeID);
    fromCSVImport(csvData);
    initializeDataValue();
  }

  // 默认构造函数
  OPCUAModernDataStruct() : variable_full_path("") {
    UA_NodeId_init(&nodeID);
    initializeDataValue();
  }

  // ==================== 拷贝控制 ====================

  // 拷贝构造函数
  OPCUAModernDataStruct(const OPCUAModernDataStruct &other)
      : variable_name(other.variable_name),
        variable_nodeID(other.variable_nodeID),
        namespace_index(other.namespace_index), data_type(other.data_type),
        raw_data_type(other.raw_data_type), description(other.description),
        access_level(other.access_level), parent_nodeID(other.parent_nodeID),
        is_array(other.is_array), arrayDimensions(other.arrayDimensions),
        browse_name(other.browse_name), filter_reason(other.filter_reason),
        parent_path(other.parent_path),
        variable_full_path(other.variable_full_path),
        data_type_enum(other.data_type_enum),
        data_block_number(other.data_block_number),
        bytes_offset(other.bytes_offset), bit_offset(other.bit_offset),
        s7_data_type_length(other.s7_data_type_length),
        s7_data_array_length(other.s7_data_array_length),
        dataValue(other.dataValue), // std::variant 自动拷贝
        isOPCUAType(other.isOPCUAType), buildType(other.buildType),
        isFromCSV(other.isFromCSV) {
    UA_NodeId_init(&nodeID);
    UA_NodeId_copy(&other.nodeID, &nodeID);
  }

  // 拷贝赋值运算符
  OPCUAModernDataStruct &operator=(const OPCUAModernDataStruct &other) {
    if (this != &other) {
      UA_NodeId_clear(&nodeID);

      variable_name = other.variable_name;
      variable_nodeID = other.variable_nodeID;
      namespace_index = other.namespace_index;
      data_type = other.data_type;
      raw_data_type = other.raw_data_type;
      description = other.description;
      access_level = other.access_level;
      parent_nodeID = other.parent_nodeID;
      is_array = other.is_array;
      arrayDimensions = other.arrayDimensions;
      browse_name = other.browse_name;
      filter_reason = other.filter_reason;
      parent_path = other.parent_path;
      variable_full_path = other.variable_full_path;
      data_type_enum = other.data_type_enum;
      data_block_number = other.data_block_number;
      bytes_offset = other.bytes_offset;
      bit_offset = other.bit_offset;
      s7_data_type_length = other.s7_data_type_length;
      s7_data_array_length = other.s7_data_array_length;
      dataValue = other.dataValue; // std::variant 自动赋值
      isOPCUAType = other.isOPCUAType;
      buildType = other.buildType;
      isFromCSV = other.isFromCSV;

      UA_NodeId_init(&nodeID);
      UA_NodeId_copy(&other.nodeID, &nodeID);
    }
    return *this;
  }

  // ==================== 移动构造函数 ====================

  // 移动构造函数
  OPCUAModernDataStruct(OPCUAModernDataStruct &&other) noexcept
      : variable_name(std::move(other.variable_name)),
        variable_nodeID(std::move(other.variable_nodeID)),
        namespace_index(other.namespace_index),
        data_type(std::move(other.data_type)),
        raw_data_type(std::move(other.raw_data_type)),
        description(std::move(other.description)),
        access_level(other.access_level),
        parent_nodeID(std::move(other.parent_nodeID)), is_array(other.is_array),
        arrayDimensions(std::move(other.arrayDimensions)),
        browse_name(std::move(other.browse_name)),
        filter_reason(std::move(other.filter_reason)),
        parent_path(std::move(other.parent_path)),
        variable_full_path(std::move(other.variable_full_path)),
        data_type_enum(other.data_type_enum),
        data_block_number(other.data_block_number),
        bytes_offset(other.bytes_offset), bit_offset(other.bit_offset),
        s7_data_type_length(other.s7_data_type_length),
        s7_data_array_length(other.s7_data_array_length),
        dataValue(std::move(other.dataValue)), // std::variant 移动
        isOPCUAType(other.isOPCUAType), buildType(other.buildType),
        isFromCSV(other.isFromCSV) {

    // 转移 UA_NodeId 所有权
    UA_NodeId_init(&nodeID);
    UA_NodeId_copy(&other.nodeID, &nodeID);
    UA_NodeId_clear(&other.nodeID);

    // 重置源对象
    other.namespace_index = 0;
    other.access_level = 0;
    other.is_array = false;
    other.arrayDimensions = "";
    other.parent_path.clear();
    other.variable_full_path.clear();
    other.data_type_enum = S7DataType::UNKNOWN;
    other.data_block_number = -1;
    other.bytes_offset = -1.0f;
    other.bit_offset = -1;
    other.s7_data_type_length = -1;
    other.s7_data_array_length = -1;
    other.isOPCUAType = true;
    other.buildType = InputFormat::BROWSER;
    other.isFromCSV = false;
    // std::variant 被移动后变为未定义状态，重置为 bool(false)
    other.dataValue = false;
  }

  // ==================== 移动赋值运算符 ====================

  // 移动赋值运算符
  OPCUAModernDataStruct &operator=(OPCUAModernDataStruct &&other) noexcept {
    if (this != &other) {
      // 清理当前资源
      UA_NodeId_clear(&nodeID);

      // 移动基本类型
      variable_name = std::move(other.variable_name);
      variable_nodeID = std::move(other.variable_nodeID);
      namespace_index = other.namespace_index;
      data_type = std::move(other.data_type);
      raw_data_type = std::move(other.raw_data_type);
      description = std::move(other.description);
      access_level = other.access_level;
      parent_nodeID = std::move(other.parent_nodeID);
      is_array = other.is_array;
      arrayDimensions = std::move(other.arrayDimensions);
      browse_name = std::move(other.browse_name);
      filter_reason = std::move(other.filter_reason);
      parent_path = std::move(other.parent_path);
      variable_full_path = std::move(other.variable_full_path);
      data_type_enum = other.data_type_enum;
      data_block_number = other.data_block_number;
      bytes_offset = other.bytes_offset;
      bit_offset = other.bit_offset;
      s7_data_type_length = other.s7_data_type_length;
      s7_data_array_length = other.s7_data_array_length;
      dataValue = std::move(other.dataValue); // std::variant 移动赋值
      isOPCUAType = other.isOPCUAType;
      buildType = other.buildType;
      isFromCSV = other.isFromCSV;

      // 转移 UA_NodeId 所有权
      UA_NodeId_init(&nodeID);
      UA_NodeId_copy(&other.nodeID, &nodeID);
      UA_NodeId_clear(&other.nodeID);

      // 重置源对象
      other.namespace_index = 0;
      other.access_level = 0;
      other.is_array = false;
      other.arrayDimensions = "";
      other.parent_path.clear();
      other.variable_full_path.clear();
      other.data_type_enum = S7DataType::UNKNOWN;
      other.data_block_number = -1;
      other.bytes_offset = -1.0f;
      other.bit_offset = -1;
      other.s7_data_type_length = -1;
      other.s7_data_array_length = -1;
      other.isOPCUAType = true;
      other.buildType = InputFormat::BROWSER;
      other.isFromCSV = false;
      other.dataValue = false; // 重置为 bool
    }
    return *this;
  }

  // ==================== 析构函数 ====================

  ~OPCUAModernDataStruct() {
    UA_NodeId_clear(&nodeID);
    // std::variant 自动析构
  }

  // ==================== swap 方法 ====================

  void swap(OPCUAModernDataStruct &other) noexcept {
    using std::swap;

    // OPC UA 字段
    swap(variable_name, other.variable_name);
    swap(variable_nodeID, other.variable_nodeID);
    swap(namespace_index, other.namespace_index);
    swap(data_type, other.data_type);
    swap(raw_data_type, other.raw_data_type);
    swap(description, other.description);
    swap(access_level, other.access_level);
    swap(parent_nodeID, other.parent_nodeID);
    swap(is_array, other.is_array);
    swap(arrayDimensions, other.arrayDimensions);
    swap(browse_name, other.browse_name);
    swap(filter_reason, other.filter_reason);

    // S7 字段
    swap(parent_path, other.parent_path);
    swap(variable_full_path, other.variable_full_path);
    swap(data_type_enum, other.data_type_enum);
    swap(data_block_number, other.data_block_number);
    swap(bytes_offset, other.bytes_offset);
    swap(bit_offset, other.bit_offset);
    swap(s7_data_type_length, other.s7_data_type_length);
    swap(s7_data_array_length, other.s7_data_array_length);

    // std::variant
    swap(dataValue, other.dataValue);

    // 标识字段
    swap(isOPCUAType, other.isOPCUAType);
    swap(buildType, other.buildType);
    swap(isFromCSV, other.isFromCSV);

    // 交换 UA_NodeId
    UA_NodeId temp;
    UA_NodeId_init(&temp);
    UA_NodeId_copy(&nodeID, &temp);
    UA_NodeId_clear(&nodeID);
    UA_NodeId_copy(&other.nodeID, &nodeID);
    UA_NodeId_clear(&other.nodeID);
    UA_NodeId_copy(&temp, &other.nodeID);
    UA_NodeId_clear(&temp);
  }

  // ==================== reset 方法 ====================

  void reset() {
    // OPC UA 字段
    variable_name.clear();
    variable_nodeID.clear();
    namespace_index = 0;
    data_type.clear();
    raw_data_type.clear();
    description.clear();
    access_level = 0;
    parent_nodeID.clear();
    is_array = false;
    arrayDimensions = "";
    browse_name.clear();
    filter_reason.clear();

    // S7 字段
    parent_path.clear();
    variable_full_path.clear();
    data_type_enum = S7DataType::UNKNOWN;
    data_block_number = -1;
    bytes_offset = -1.0f;
    bit_offset = -1;
    s7_data_type_length = -1;
    s7_data_array_length = -1;

    // 重置 std::variant 为 bool(false)
    dataValue = false;

    // 标识字段
    isOPCUAType = true;
    buildType = InputFormat::BROWSER;
    isFromCSV = false;

    // 重置节点ID
    UA_NodeId_clear(&nodeID);
    UA_NodeId_init(&nodeID);
  }

  // ==================== 初始化 dataValue 方法 ====================

  void initializeDataValue() {
    // 根据数据类型初始化 dataValue 为对应类型的默认值
    switch (data_type_enum) {
    case S7DataType::BOOL:
      dataValue = false;
      break;
    case S7DataType::BYTE:
      dataValue = uint8_t{0};
      break;
    case S7DataType::WORD:
      dataValue = uint16_t{0};
      break;
    case S7DataType::DWORD:
      dataValue = uint32_t{0};
      break;
    case S7DataType::UDINT:
      dataValue = uint32_t{0}; // UDINT 用 uint32_t
      break;
    case S7DataType::INT:
      dataValue = int16_t{0};
      break;
    case S7DataType::DINT:
      dataValue = int32_t{0};
      break;
    case S7DataType::REAL:
      dataValue = 0.0f;
      break;
    case S7DataType::STRING:
      dataValue = std::string("");
      break;
    default:
      dataValue = bool{false}; // 默认存储 bool
      break;
    }
  }

  // ==================== 便捷取值方法 ====================

  // 获取 bool 值
  bool toBool(bool defaultValue = false) const {
    if (const auto *val = std::get_if<bool>(&dataValue)) {
      return *val;
    }
    return defaultValue;
  }

  // 获取 uint8_t 值
  uint8_t toUInt8(uint8_t defaultValue = 0) const {
    if (const auto *val = std::get_if<uint8_t>(&dataValue)) {
      return *val;
    }
    return defaultValue;
  }

  // 获取 int16_t 值
  int16_t toInt16(int16_t defaultValue = 0) const {
    if (const auto *val = std::get_if<int16_t>(&dataValue)) {
      return *val;
    }
    return defaultValue;
  }

  // 获取 uint16_t 值
  uint16_t toUInt16(uint16_t defaultValue = 0) const {
    if (const auto *val = std::get_if<uint16_t>(&dataValue)) {
      return *val;
    }
    return defaultValue;
  }

  // 获取 int32_t 值
  int32_t toInt32(int32_t defaultValue = 0) const {
    if (const auto *val = std::get_if<int32_t>(&dataValue)) {
      return *val;
    }
    return defaultValue;
  }

  // 获取 uint32_t 值
  uint32_t toUInt32(uint32_t defaultValue = 0) const {
    if (const auto *val = std::get_if<uint32_t>(&dataValue)) {
      return *val;
    }
    return defaultValue;
  }

  // 获取 float 值
  float toFloat(float defaultValue = 0.0f) const {
    if (const auto *val = std::get_if<float>(&dataValue)) {
      return *val;
    }
    return defaultValue;
  }

  // 获取 std::string 值
  std::string toString(const std::string &defaultValue = "") const {
    if (const auto *val = std::get_if<std::string>(&dataValue)) {
      return *val;
    }
    return defaultValue;
  }

  // ==================== 设置值方法 ====================

  void setBool(bool value) { dataValue = value; }
  void setUInt8(uint8_t value) { dataValue = value; }
  void setInt16(int16_t value) { dataValue = value; }
  void setUInt16(uint16_t value) { dataValue = value; }
  void setInt32(int32_t value) { dataValue = value; }
  void setUInt32(uint32_t value) { dataValue = value; }
  void setFloat(float value) { dataValue = value; }
  void setString(const std::string &value) { dataValue = value; }
  void setString(std::string &&value) { dataValue = std::move(value); }

  // ==================== 类型检查方法 ====================

  bool isBool() const { return std::holds_alternative<bool>(dataValue); }
  bool isUInt8() const { return std::holds_alternative<uint8_t>(dataValue); }
  bool isInt16() const { return std::holds_alternative<int16_t>(dataValue); }
  bool isUInt16() const { return std::holds_alternative<uint16_t>(dataValue); }
  bool isInt32() const { return std::holds_alternative<int32_t>(dataValue); }
  bool isUInt32() const { return std::holds_alternative<uint32_t>(dataValue); }
  bool isFloat() const { return std::holds_alternative<float>(dataValue); }
  bool isString() const {
    return std::holds_alternative<std::string>(dataValue);
  }

  // ==================== CSV 导入方法 ====================

  static void initializeS7Length(OPCUAModernDataStruct &item) {
    if (item.data_type_enum == S7DataType::BOOL) {
      item.s7_data_type_length = 1;
    } else if (item.data_type_enum == S7DataType::BYTE) {
      item.s7_data_type_length = 1;
    } else if (item.data_type_enum == S7DataType::INT) {
      item.s7_data_type_length = 2;
    } else if (item.data_type_enum == S7DataType::WORD) {
      item.s7_data_type_length = 2;
    } else if (item.data_type_enum == S7DataType::DINT) {
      item.s7_data_type_length = 4;
    } else if (item.data_type_enum == S7DataType::DWORD) {
      item.s7_data_type_length = 4;
    } else if (item.data_type_enum == S7DataType::UDINT) {
      item.s7_data_type_length = 4;
    } else if (item.data_type_enum == S7DataType::REAL) {
      item.s7_data_type_length = 4;
    } else if (item.data_type_enum == S7DataType::STRING) {
      item.s7_data_type_length = 0;
    } else if (item.data_type_enum == S7DataType::ARRAY) {
      item.s7_data_type_length = 0;
    } else if (item.data_type_enum == S7DataType::STRUCT) {
      item.s7_data_type_length = 0;
    } else {
      item.data_type_enum = S7DataType::UNKNOWN;
      item.s7_data_type_length = 0;
    }
  }

  // 从 CSV 结构体导入数据
  void fromCSVImport(const OPCUAModernDataStructFromCSV &csvData) {
    // 1. 直接映射字段
    variable_name = csvData.displayName;
    variable_nodeID = removeBackslashes(csvData.nodeId);
    data_type = csvData.dataType;
    raw_data_type = csvData.dataType;
    description = csvData.description;
    parent_nodeID = removeBackslashes(csvData.parentNodeId);
    arrayDimensions = csvData.arrayDimensions;
    browse_name = csvData.browseName;

    // data type enum transform
    auto it = typeMap.find(raw_data_type);
    if (it != typeMap.end()) {
      data_type_enum = it->second;
      initializeS7Length(*this);
    } else {
      data_type_enum = S7DataType::UNKNOWN;
    }

    // 2. accessLevel 转换（字符串 -> 整数）
    if (!csvData.accessLevel.empty()) {
      try {
        access_level = std::stoi(csvData.accessLevel);
      } catch (const std::exception &e) {
        access_level = 0;
      }
    }

    // 3. 其他字段保持默认值
    filter_reason = "";
    isOPCUAType = true;
    isFromCSV = true;
    buildType = InputFormat::CSV;

    // 设置 variable_full_path
    parent_path = "";
    if (!csvData.nodeId.empty()) {
      variable_full_path = removeBackslashes(csvData.nodeId);
    } else if (!csvData.displayName.empty()) {
      variable_full_path = csvData.displayName;
    } else {
      variable_full_path = "";
    }

    // 4. 初始化 dataValue
    initializeDataValue();
  }

  // 使用erase-remove惯用法
  std::string removeBackslashes(const std::string &input) {
    std::string result = input;
    result.erase(std::remove(result.begin(), result.end(), '\\'), result.end());
    return result;
  }
};

// 用于存储所有变量节点的容器
struct OPCUAParseResult {
  std::vector<OPCUAModernDataStruct> variables;
  std::map<std::string, std::string>
      type_aliases; // 别名映射：别名 -> OPC UA标准类型ID
  std::vector<std::string> namespace_uris; // 命名空间URI列表
  std::string generator_info; // 生成器信息（TIA Portal版本等）

  // 默认构造函数
  OPCUAParseResult() = default;

  // 删除拷贝构造函数和拷贝赋值
  OPCUAParseResult(const OPCUAParseResult &) = delete;
  OPCUAParseResult &operator=(const OPCUAParseResult &) = delete;

  // 移动构造函数
  OPCUAParseResult(OPCUAParseResult &&other) noexcept
      : variables(std::move(other.variables)),
        type_aliases(std::move(other.type_aliases)),
        namespace_uris(std::move(other.namespace_uris)),
        generator_info(std::move(other.generator_info)) {
    // 移动后，other处于有效但未指定的状态
  }

  // 移动赋值运算符
  OPCUAParseResult &operator=(OPCUAParseResult &&other) noexcept {
    if (this != &other) {
      variables = std::move(other.variables);
      type_aliases = std::move(other.type_aliases);
      namespace_uris = std::move(other.namespace_uris);
      generator_info = std::move(other.generator_info);
    }
    return *this;
  }

  void getOPCUAStrcutVec(std::vector<OPCUAModernDataStruct> &&vec)
  {
    for(auto &var : vec)
    {
      if (var.filter_reason != "" || !checkDotAndBackslash(uaStringToString(
                                         var.nodeID.identifier.string))) {
        continue;
      } else {
        variables.emplace_back(std::move(var));
      }
    }
  }
};



