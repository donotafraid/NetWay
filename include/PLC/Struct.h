#pragma once

#include "PLC/TransformS7AndOPCUA.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include "load_config/Qt_library.h"


struct UA_Variable_Info{
    std::string nodeID;
    std::string dataType;
};

struct S7ModernDataStruct {
    std::string variable_name;
    std::string variable_full_path;
    std::string variable_nodeID;
    std::string comment;
    S7DataType data_type_enum;
    int data_block_number = -1;
    float bytes_offset = -1;
    int bit_offset = -1;
    //  s7_data_type_length mean the length of s7 , special string of s7. its length = 1 (the length of string in s7 ) + 1 (current length of string object ) + max string object context
    int s7_data_type_length = -1;
    int s7_data_array_length = -1;
    std::unique_ptr<Dynamic_Value> data_pointer = nullptr;

    // 默认构造函数
    S7ModernDataStruct() = default;
    
    // 移动构造函数
    S7ModernDataStruct(S7ModernDataStruct&& other) noexcept
        : variable_name(std::move(other.variable_name))
        , variable_full_path(std::move(other.variable_full_path))
        , variable_nodeID(std::move(other.variable_nodeID))
        , comment(std::move(other.comment))
        , data_type_enum(other.data_type_enum)
        , data_block_number(other.data_block_number)
        , bytes_offset(other.bytes_offset)
        , bit_offset(other.bit_offset)
        , s7_data_type_length(other.s7_data_type_length)
        , s7_data_array_length(other.s7_data_array_length)
        , data_pointer(std::move(other.data_pointer))
    {
        // 将源对象的状态重置为有效但未指定的状态
        other.data_block_number = -1;
        other.bytes_offset = -1;
        other.bit_offset = -1;
        other.s7_data_type_length = -1;
        other.s7_data_array_length = -1;
        // data_type_enum 可以保持原值，或者重置为默认
        // std::string 被移动后处于有效但未指定的状态，不需要额外处理
    }
    
    // 移动赋值操作符
    S7ModernDataStruct& operator=(S7ModernDataStruct&& other) noexcept {
        if (this != &other) {
            variable_name = std::move(other.variable_name);
            variable_full_path = std::move(other.variable_full_path);
            variable_nodeID = std::move(other.variable_nodeID);
            comment = std::move(other.comment);
            data_type_enum = other.data_type_enum;
            data_block_number = other.data_block_number;
            bytes_offset = other.bytes_offset;
            bit_offset = other.bit_offset;
            s7_data_type_length = other.s7_data_type_length;
            s7_data_array_length = other.s7_data_array_length;
            data_pointer = std::move(other.data_pointer);
            
            // 重置源对象
            other.data_block_number = -1;
            other.bytes_offset = -1;
            other.bit_offset = -1;
            other.s7_data_type_length = -1;
            other.s7_data_array_length = -1;
        }
        return *this;
    }
    
    // 删除拷贝构造函数和拷贝赋值
    S7ModernDataStruct(const S7ModernDataStruct&) = delete;
    S7ModernDataStruct& operator=(const S7ModernDataStruct&) = delete;
    
    // 可选：添加一个辅助函数来检查是否有效
    bool isValid() const {
        return data_block_number != -1 && 
               bytes_offset != -1 && 
               s7_data_type_length != -1;
    }
};

struct ModelItem {
  QString name;
  S7DataType type;
  int offset;
  QVariant value; // 存储原始类型，不转换
  QString comment;
  int DataLength = 0; // store string length
};

// 变量节点信息（对应 XML 中的 UAVariable）
struct OPCUAModernDataStruct {
  std::string variable_name;
  std::string variable_nodeID;
  int namespace_index;
  std::string data_type;
  std::string raw_data_type;
  std::string description;
  int access_level;
  std::string parent_nodeID;
  bool is_array;
  int array_dimension = -1;
  std::string browse_name;
  std::string filter_reason;
  std::unique_ptr<Dynamic_Value> data_pointer = std::make_unique<Dynamic_Value>();

  S7DataType data_type_enum = S7DataType::UNKNOWN;
  std::string variable_full_path;
  bool isOPCUAType = false;

  // 默认构造函数
  OPCUAModernDataStruct() = default;

  // 删除拷贝构造函数和拷贝赋值
  OPCUAModernDataStruct(const OPCUAModernDataStruct &) = delete;
  OPCUAModernDataStruct &operator=(const OPCUAModernDataStruct &) = delete;

  // 移动构造函数
  OPCUAModernDataStruct(OPCUAModernDataStruct &&other) noexcept
      : variable_name(std::move(other.variable_name)),
        variable_nodeID(std::move(other.variable_nodeID)),
        namespace_index(other.namespace_index),
        data_type(std::move(other.data_type)),
        raw_data_type(std::move(other.raw_data_type)),
        description(std::move(other.description)),
        access_level(other.access_level),
        parent_nodeID(std::move(other.parent_nodeID)),
        is_array(other.is_array), array_dimension(other.array_dimension),
        browse_name(std::move(other.browse_name)),
        filter_reason(std::move(other.filter_reason)),
        data_pointer(std::move(other.data_pointer)),
        data_type_enum(other.data_type_enum), isOPCUAType(other.isOPCUAType) {
    // 移动后，other 的数据指针已经为空
    other.namespace_index = 0;
    other.access_level = 0;
    other.is_array = false;
    other.array_dimension = 0;
    other.data_type_enum = S7DataType::UNKNOWN;
    other.isOPCUAType = false;
  }

  // 移动赋值运算符
  OPCUAModernDataStruct &operator=(OPCUAModernDataStruct &&other) noexcept {
    if (this != &other) {
      variable_name = std::move(other.variable_name);
      variable_nodeID = std::move(other.variable_nodeID);
      namespace_index = other.namespace_index;
      data_type = std::move(other.data_type);
      raw_data_type = std::move(other.raw_data_type);
      description = std::move(other.description);
      access_level = other.access_level;
      parent_nodeID = std::move(other.parent_nodeID);
      is_array = other.is_array;
      array_dimension = other.array_dimension;
      browse_name = std::move(other.browse_name);
      filter_reason = std::move(other.filter_reason);
      data_pointer = std::move(other.data_pointer);
      data_type_enum = other.data_type_enum;
      isOPCUAType = other.isOPCUAType;

      // 重置 other 的状态
      other.namespace_index = 0;
      other.access_level = 0;
      other.is_array = false;
      other.array_dimension = 0;
      other.data_type_enum = S7DataType::UNKNOWN;
      other.isOPCUAType = false;
    }
    return *this;
  }

  // 辅助方法：交换两个对象
  void swap(OPCUAModernDataStruct &other) noexcept {
    using std::swap;
    swap(variable_name, other.variable_name);
    swap(variable_nodeID, other.variable_nodeID);
    swap(namespace_index, other.namespace_index);
    swap(data_type, other.data_type);
    swap(raw_data_type, other.raw_data_type);
    swap(description, other.description);
    swap(access_level, other.access_level);
    swap(parent_nodeID, other.parent_nodeID);
    swap(is_array, other.is_array);
    swap(array_dimension, other.array_dimension);
    swap(browse_name, other.browse_name);
    swap(filter_reason, other.filter_reason);
    swap(data_pointer, other.data_pointer);
    swap(data_type_enum, other.data_type_enum);
    swap(isOPCUAType, other.isOPCUAType);
  }

  // 辅助方法：检查是否有有效数据
  bool isValid() const {
    return !variable_name.empty() && !variable_nodeID.empty();
  }

  // 辅助方法：重置对象
  void reset() {
    variable_name.clear();
    variable_nodeID.clear();
    namespace_index = 0;
    data_type.clear();
    raw_data_type.clear();
    description.clear();
    access_level = 0;
    parent_nodeID.clear();
    is_array = false;
    array_dimension = 0;
    browse_name.clear();
    filter_reason.clear();
    data_pointer.reset();
    data_type_enum = S7DataType::UNKNOWN;
    isOPCUAType = false;
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
    variables = std::move(vec); 
  }
};

