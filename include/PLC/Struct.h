#pragma once

#include "load_config/Qt_library.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <typeindex> // defines std::type_index
#include "Rust_error_deal/error_deal.h"

#include <open62541/nodeids.h>
#include <open62541/client.h>
#include <open62541/config.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_config_default.h>

// 通用数据值类型 - 支持所有PLC数据类型
using NormalDataType = std::variant<
    bool,                    // BOOL
    uint8_t,                 // BYTE
    int16_t,                 // INT
    uint16_t,                // WORD  
    int32_t,                 // DINT
    uint32_t,                // UDINT,DWORD
    float,                   // REAL
    std::string              // STRING
>;

enum class S7DataType
{
    BOOL,  //QCheckBox 
    BYTE,   //QSpinBox
    INT,    //QSpinBox
    WORD,   //QSpinBox
    DINT,   //QSpinBox
    UDINT,  //QLineEdit
    DWORD,  //QLineEdit
    REAL,   //QDoubleSpinBox
    STRING, //QLineEdit
    ARRAY, 
    STRUCT,
    UNKNOWN 
};

NormalDataType default_value_for(S7DataType type); 

class Dynamic_Value{
    public:
    void *ptr = nullptr;
    std::type_index m_cpp_initalize_type = typeid(void);
    std::type_index m_cpp_introduced_type = typeid(void);
    void(*deleter)(void*) = nullptr;

    //  AVOIDING float DELETE BY SHALLOW COPY
    Dynamic_Value (const Dynamic_Value&) = delete;
    Dynamic_Value& operator= (const Dynamic_Value&) =delete; 
    
    //  RIGHT VALUE CONSTRUCT AND DELETE NULL CONSTRUCT
    template<typename T>
    Dynamic_Value (T&& val){Reset_Value(std::forward<T>(val));}
    Dynamic_Value () = default;

    bool has_value() const { return ptr != nullptr; }

    template<typename T>
    const T& get() const{
        return *(static_cast<T *>(ptr));
    }

    Dynamic_Value(Dynamic_Value&& otehr) noexcept:
    ptr(otehr.ptr),deleter(otehr.deleter)
    {
        otehr.ptr = nullptr;
        otehr.deleter = nullptr;
    }

    Dynamic_Value& operator= (Dynamic_Value &&other) noexcept{
        if(this == &other)
        {
            return *this;
        }
        if(ptr && deleter)
        {
            ptr = other.ptr;
            deleter = other.deleter;
            other.ptr = nullptr;
            other.deleter = nullptr;
        }
        return *this;
    }

    template <typename T> void Reset_Value(T &&val) {
      // 1. 获取实际要存储的类型（去除引用和cv限定符）
      using StoredType = std::decay_t<T>;

      // 2. 记录新类型的 typeid
      m_cpp_introduced_type = typeid(StoredType);

      // 3. 情况1：已有数据且类型匹配
      if (deleter && ptr && m_cpp_introduced_type == m_cpp_initalize_type) {
        // 直接赋值到现有内存
        *static_cast<StoredType *>(ptr) = std::forward<T>(val);
        return;
      }

      // 4. 情况2：首次分配
      if (deleter == nullptr && ptr == nullptr) {
        auto *new_ptr = new StoredType(std::forward<T>(val));
        if (new_ptr != nullptr) {
          ptr = new_ptr;
          m_cpp_initalize_type = typeid(StoredType);
          deleter = [](void *pointer) {
            delete static_cast<StoredType *>(pointer);
          };
        }
        return;
      }

      // 5. 情况3：类型不匹配，需要重新分配
      if (m_cpp_initalize_type != m_cpp_introduced_type) {
        // 清理旧资源
        if (deleter) {
          deleter(ptr);
        }
        ptr = nullptr;
        deleter = nullptr;

        // 分配新资源
        auto *new_ptr = new StoredType(std::forward<T>(val));
        if (new_ptr != nullptr) {
          ptr = new_ptr;
          m_cpp_initalize_type = typeid(StoredType);
          deleter = [](void *pointer) {
            delete static_cast<StoredType *>(pointer);
          };
        }
        return;
      }
    }

    Result<bool, RichError> Reset_Value_by_uint8_t(
        int data_offset, int data_length, S7DataType &data_type_enum,
        std::vector<uint8_t> &m_data_block_buffer);

    //  SHOULD DO DELETE
    ~Dynamic_Value(){
      if (deleter && ptr) {
        //  HOW TO DELETE 
        deleter(ptr);
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

static const std::map<S7DataType, std::string> S7DataTypeToString = {
    {S7DataType::BOOL, "BOOL"},     {S7DataType::BYTE, "BYTE"},
    {S7DataType::INT, "INT"},       {S7DataType::WORD, "WORD"},
    {S7DataType::DINT, "DINT"},     {S7DataType::UDINT, "UDINT"},
    {S7DataType::DWORD, "DWORD"},   {S7DataType::REAL, "REAL"},
    {S7DataType::STRING, "STRING"}, {S7DataType::ARRAY, "ARRAY"},
    {S7DataType::STRUCT, "STRUCT"}, {S7DataType::UNKNOWN, "UNKNOWN"}};

// 建立字符串到枚举的映射表
static const std::unordered_map<std::string, S7DataType> typeMap = {
    {"BOOL", S7DataType::BOOL},     {"BYTE", S7DataType::BYTE},
    {"INT", S7DataType::INT},       {"WORD", S7DataType::WORD},
    {"DINT", S7DataType::DINT},     {"UDINT", S7DataType::UDINT},
    {"DWORD", S7DataType::DWORD},   {"REAL", S7DataType::REAL},
    {"STRING", S7DataType::STRING}, {"ARRAY", S7DataType::ARRAY},
    {"STRUCT", S7DataType::STRUCT},
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
  bool is_array = false; // ← 由 arrayDimensions = "" 决定
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

  // 数据存储 - 使用 QVariant 替代 data_pointer
  QVariant dataVar;

  // 标识字段
  bool isOPCUAType = true;
  InputFormat buildType = InputFormat::BROWSER;
  bool isFromCSV = false; // ✅ 新增 - 标记是否从CSV导入

  // ==================== 构造函数 ====================

  // 从 CSV 结构体构造
  explicit OPCUAModernDataStruct(const OPCUAModernDataStructFromCSV &csvData) {
    UA_NodeId_init(&nodeID);
    fromCSVImport(csvData);
    initializeDataVar();
  }

  // 默认构造函数
  OPCUAModernDataStruct() {
    UA_NodeId_init(&nodeID);
    initializeDataVar();
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
        parentName(other.parentName),
        variable_full_path(other.variable_full_path), comment(other.comment),
        data_type_enum(other.data_type_enum),
        data_block_number(other.data_block_number),
        bytes_offset(other.bytes_offset), bit_offset(other.bit_offset),
        s7_data_type_length(other.s7_data_type_length),
        s7_data_array_length(other.s7_data_array_length),
        dataVar(other.dataVar), // 拷贝 QVariant
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
      parentName = other.parentName;
      variable_full_path = other.variable_full_path;
      comment = other.comment;
      data_type_enum = other.data_type_enum;
      data_block_number = other.data_block_number;
      bytes_offset = other.bytes_offset;
      bit_offset = other.bit_offset;
      s7_data_type_length = other.s7_data_type_length;
      s7_data_array_length = other.s7_data_array_length;
      dataVar = other.dataVar; // 拷贝 QVariant
      isOPCUAType = other.isOPCUAType;
      buildType = other.buildType;
      isFromCSV = other.isFromCSV;

      UA_NodeId_init(&nodeID);
      UA_NodeId_copy(&other.nodeID, &nodeID);
    }
    return *this;
  }

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
        arrayDimensions(std::move(other.arrayDimensions)),
        browse_name(std::move(other.browse_name)),
        filter_reason(std::move(other.filter_reason)),
        parentName(std::move(other.parentName)),
        variable_full_path(std::move(other.variable_full_path)),
        comment(std::move(other.comment)), data_type_enum(other.data_type_enum),
        data_block_number(other.data_block_number),
        bytes_offset(other.bytes_offset), bit_offset(other.bit_offset),
        s7_data_type_length(other.s7_data_type_length),
        s7_data_array_length(other.s7_data_array_length),
        dataVar(std::move(other.dataVar)), // 移动 QVariant
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
    other.data_type_enum = S7DataType::UNKNOWN;
    other.data_block_number = -1;
    other.bytes_offset = -1.0f;
    other.bit_offset = -1;
    other.s7_data_type_length = -1;
    other.s7_data_array_length = -1;
    other.isOPCUAType = true;
    other.buildType = InputFormat::BROWSER;
    other.isFromCSV = false;
    other.dataVar = QVariant(); // 重置 QVariant
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
      arrayDimensions = std::move(other.arrayDimensions);
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
      dataVar = std::move(other.dataVar); // 移动 QVariant
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
      other.data_type_enum = S7DataType::UNKNOWN;
      other.data_block_number = -1;
      other.bytes_offset = -1.0f;
      other.bit_offset = -1;
      other.s7_data_type_length = -1;
      other.s7_data_array_length = -1;
      other.isOPCUAType = true;
      other.buildType = InputFormat::BROWSER;
      other.isFromCSV = false;
      other.dataVar = QVariant(); // 重置 QVariant
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

    // QVariant
    swap(dataVar, other.dataVar);

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
    parentName.clear();
    variable_full_path.clear();
    comment.clear();
    data_type_enum = S7DataType::UNKNOWN;
    data_block_number = -1;
    bytes_offset = -1.0f;
    bit_offset = -1;
    s7_data_type_length = -1;
    s7_data_array_length = -1;

    // 重置 QVariant
    dataVar = QVariant();

    // 标识字段
    isOPCUAType = true;
    buildType = InputFormat::BROWSER;
    isFromCSV = false;

    // 重置节点ID
    UA_NodeId_clear(&nodeID);
    UA_NodeId_init(&nodeID);
  }

    // ==================== 初始化 dataVar 方法 ====================

  void initializeDataVar() {
    // 根据数据类型初始化 dataVar 为对应类型的默认值
    switch (data_type_enum) {
    case S7DataType::BOOL:
      dataVar = false;
      break;
    case S7DataType::BYTE:
    case S7DataType::WORD:
    case S7DataType::DWORD:
    case S7DataType::UDINT:
      dataVar = 0U; // 无符号整数
      break;
    case S7DataType::INT:
    case S7DataType::DINT:
      dataVar = 0; // 有符号整数
      break;
    case S7DataType::REAL:
      dataVar = 0.0f; // 浮点数
      break;
    case S7DataType::STRING:
      dataVar = QString("");
      break;
    default:
      dataVar = QVariant();
      break;
    }
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

    // 4. 初始化 dataVar
    initializeDataVar();
  }

  // 使用erase-remove惯用法（最简洁）
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



