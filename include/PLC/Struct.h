#pragma once

#include "PLC/TransformS7AndOPCUA.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include "load_config/Qt_library.h"
#include "PLC/TransformS7AndOPCUA.h"

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

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

/**
 * 检查字符串中是否存在小数点以及小数点后面是否存在反斜杠
 * @param str 要检查的字符串
 * @param hasDot 输出：是否存在小数点
 * @param hasBackslashAfterDot 输出：小数点后面是否存在反斜杠
 * @return 返回码：0-成功，-1-参数无效
 */
static int checkDotAndBackslash(const std::string &str) {
  if (str.empty()) {
    return -1; // 空字符串
  }

  bool hasDot = false;
  bool hasBackslashAfterDot = false;

  size_t dotPos = str.find('.');
  if (dotPos != std::string::npos) {
    hasDot = true;

    // 查找小数点后面的反斜杠
    size_t backslashPos = str.find('"', dotPos + 1);
    if (backslashPos != std::string::npos) {
      hasBackslashAfterDot = true;
    }
  }

  if (hasBackslashAfterDot && hasDot) {
    //  mean the var is useful variable
    return true;
  } else {
    //  mean the var is uselessful variable
    return false;
  }
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

struct UA_Variable_Info{
    std::string nodeID;
    std::string dataType;
};

struct ModelItem {
  QString name;
  S7DataType type;
  int offset;
  QVariant value; // 存储原始类型，不转换
  QString comment;
  int DataLength = 0; // store string length
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
  std::string variable_name;   // ← 对应 displayName
  std::string variable_nodeID; // ← 对应 variable_nodeID
  UA_NodeId nodeID;
  int namespace_index = 0;   // ← 从 namespace_uris 解析得到
  std::string data_type;     // ← 对应 dataType 或 udt
  std::string raw_data_type; // ← 对应 dataType（原始类型）
  std::string description;
  int access_level = 0;
  std::string parent_nodeID;
  bool is_array = false;    // ← 由 arrayDimensions = "" 决定
  std::string arrayDimensions = "";
  std::string browse_name;
  std::string filter_reason;

  // S7 相关字段
  std::string parentName;
  std::string variable_full_path;
  std::string comment;
  S7DataType data_type_enum = S7DataType::UNKNOWN;
  int data_block_number = -1;
  float bytes_offset = -1.0f;
  int bit_offset = -1;
  int s7_data_type_length = -1;
  int s7_data_array_length = -1;

  // 数据指针
  std::unique_ptr<Dynamic_Value> data_pointer =
      std::make_unique<Dynamic_Value>();

  // 标识字段
  bool isOPCUAType = true;
  InputFormat buildType = InputFormat::BROWSER;
  bool isFromCSV = false; // ✅ 新增 - 标记是否从CSV导入

  // ==================== 构造函数 ====================

  // ==================== 从CSV数据构造 ====================

  // 从 CSV 结构体构造
  explicit OPCUAModernDataStruct(const OPCUAModernDataStructFromCSV &csvData) {
    UA_NodeId_init(&nodeID);
    fromCSVImport(csvData);
  }

  // 默认构造函数
  OPCUAModernDataStruct() { UA_NodeId_init(&nodeID); }

  // ==================== 拷贝控制 ====================

  // 拷贝构造函数 - 禁用
  OPCUAModernDataStruct(const OPCUAModernDataStruct &other) = delete;

  // 拷贝赋值运算符 - 禁用
  OPCUAModernDataStruct &operator=(const OPCUAModernDataStruct &other) = delete;

  // ==================== 移动构造函数 ====================

  OPCUAModernDataStruct(OPCUAModernDataStruct &&other) noexcept
      : variable_name(std::move(other.variable_name)),
        variable_nodeID(std::move(other.variable_nodeID)),
        namespace_index(other.namespace_index),
        data_type(std::move(other.data_type)),
        raw_data_type(std::move(other.raw_data_type)),
        description(std::move(other.description)),
        access_level(other.access_level),
        parent_nodeID(std::move(other.parent_nodeID)), is_array(other.is_array),
        arrayDimensions(other.arrayDimensions),
        browse_name(std::move(other.browse_name)),
        filter_reason(std::move(other.filter_reason)),
        parentName(std::move(other.parentName)),
        variable_full_path(std::move(other.variable_full_path)),
        comment(std::move(other.comment)), data_type_enum(other.data_type_enum),
        data_block_number(other.data_block_number),
        bytes_offset(other.bytes_offset), bit_offset(other.bit_offset),
        s7_data_type_length(other.s7_data_type_length),
        s7_data_array_length(other.s7_data_array_length),
        isOPCUAType(other.isOPCUAType), buildType(other.buildType),
        isFromCSV(other.isFromCSV) { // ✅ 新增

    // 转移 UA_NodeId 所有权
    UA_NodeId_init(&nodeID);
    UA_NodeId_copy(&other.nodeID, &nodeID);
    UA_NodeId_clear(&other.nodeID);

    // 转移数据指针
    data_pointer = std::move(other.data_pointer);

    // 重置源对象
    other.namespace_index = 0;
    other.access_level = 0;
    other.is_array = false;
    other.arrayDimensions="";
    other.data_type_enum = S7DataType::UNKNOWN;
    other.data_block_number = -1;
    other.bytes_offset = -1.0f;
    other.bit_offset = -1;
    other.s7_data_type_length = -1;
    other.s7_data_array_length = -1;
    other.isOPCUAType = true;
    other.buildType = InputFormat::BROWSER;
    other.isFromCSV = false; // ✅ 重置
  }

  // ==================== 移动赋值运算符 ====================

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
      arrayDimensions = other.arrayDimensions;
      browse_name = std::move(other.browse_name);
      filter_reason = std::move(other.filter_reason);

      parentName = std::move(other.parentName);
      variable_full_path = std::move(other.variable_full_path);
      comment = std::move(other.comment);
      data_type_enum = other.data_type_enum;
      data_block_number = other.data_block_number;
      bytes_offset = other.bytes_offset;
      bit_offset = other.bit_offset;
      s7_data_type_length = other.s7_data_type_length;
      s7_data_array_length = other.s7_data_array_length;
      isOPCUAType = other.isOPCUAType;
      buildType = other.buildType;
      isFromCSV = other.isFromCSV; // ✅ 新增

      // 转移 UA_NodeId 所有权
      UA_NodeId_init(&nodeID);
      UA_NodeId_copy(&other.nodeID, &nodeID);
      UA_NodeId_clear(&other.nodeID);

      // 转移数据指针
      data_pointer = std::move(other.data_pointer);

      // 重置源对象
      other.namespace_index = 0;
      other.access_level = 0;
      other.is_array = false;
      other.arrayDimensions = "";
      other.data_type_enum = S7DataType::UNKNOWN;
      other.data_block_number = -1;
      other.bytes_offset = -1.0f;
      other.bit_offset = -1;
      other.s7_data_type_length = -1;
      other.s7_data_array_length = -1;
      other.isOPCUAType = true;
      other.buildType = InputFormat::BROWSER;
      other.isFromCSV = false; // ✅ 重置
    }
    return *this;
  }

  // ==================== 析构函数 ====================

  ~OPCUAModernDataStruct() { UA_NodeId_clear(&nodeID); }

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
    swap(parentName, other.parentName);
    swap(variable_full_path, other.variable_full_path);
    swap(comment, other.comment);
    swap(data_type_enum, other.data_type_enum);
    swap(data_block_number, other.data_block_number);
    swap(bytes_offset, other.bytes_offset);
    swap(bit_offset, other.bit_offset);
    swap(s7_data_type_length, other.s7_data_type_length);
    swap(s7_data_array_length, other.s7_data_array_length);

    // 智能指针
    swap(data_pointer, other.data_pointer);

    // 标识字段
    swap(isOPCUAType, other.isOPCUAType);
    swap(buildType, other.buildType);
    swap(isFromCSV, other.isFromCSV); // ✅ 新增

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
    parentName.clear();
    variable_full_path.clear();
    comment.clear();
    data_type_enum = S7DataType::UNKNOWN;
    data_block_number = -1;
    bytes_offset = -1.0f;
    bit_offset = -1;
    s7_data_type_length = -1;
    s7_data_array_length = -1;

    // 智能指针
    data_pointer.reset();
    data_pointer = std::make_unique<Dynamic_Value>();

    // 标识字段
    isOPCUAType = true;
    buildType = InputFormat::BROWSER;
    isFromCSV = false; // ✅ 重置

    // 重置节点ID
    UA_NodeId_clear(&nodeID);
    UA_NodeId_init(&nodeID);
  }

  // ==================== CSV 导入方法 ====================
  
  // 从 CSV 结构体导入数据
  void fromCSVImport(const OPCUAModernDataStructFromCSV &csvData) {
      // 1. 直接映射字段
    variable_name = csvData.displayName;          // displayName -> variable_name
    variable_nodeID = csvData.nodeId;             // nodeId -> variable_nodeID
    data_type = csvData.dataType;                 // dataType -> data_type
    raw_data_type = csvData.dataType;             // dataType -> raw_data_type
    description = csvData.description;            // description -> description
    parent_nodeID = csvData.parentNodeId;         // parentNodeId -> parent_nodeID
    arrayDimensions = csvData.arrayDimensions;    // arrayDimensions -> arrayDimensions
    browse_name = csvData.browseName;             // browseName -> browse_name

    //  data type enum  transform
    auto it = typeMap.find(raw_data_type);
    if (it != typeMap.end()) {
      data_type_enum = it->second;
    } else {
      data_type_enum = S7DataType::UNKNOWN;
    }

    // 2. accessLevel 转换（字符串 -> 整数）
    if (!csvData.accessLevel.empty()) {
        try {
            access_level = std::stoi(csvData.accessLevel);
        } catch (const std::exception& e) {
            access_level = 0;
        }
    }
    
    // 6. 其他字段保持默认值
    filter_reason = "";  // 默认为空

    isOPCUAType = true;
    isFromCSV = true;
    buildType = InputFormat::CSV; // 标记为 CSV 导入
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

