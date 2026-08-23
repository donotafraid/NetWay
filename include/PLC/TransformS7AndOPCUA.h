#ifndef PLC_H
#define PLC_H

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <snap7.h>
#include <dlfcn.h>

#include "MainWindows/Struct.h"
#include "PLC/Struct.h"
#include "Rust_error_deal/error_deal.h"

struct S7XMLVariableDefinition
{
    std::string variable_name = "";
    S7DataType data_type_enum = S7DataType::UNKNOWN ;
    S7DataType array_type_enum = S7DataType::UNKNOWN ;
    std::string comment;
    // in string , mean the length of string object + 2
    int s7_data_type_length = 0;
    int s7_data_array_length = 0;

    float bytes_offset = 0;
    int bit_offset = 0; //  in PLC-S7 ， bool variable saved in a bytes , SO THERE IS 8 BOOL VAIRABLE used for A BTYES VARIABLE AT MOST.  bool variable position(0~7)

    std::vector<S7XMLVariableDefinition> struct_member_vector;  //  used for Type:struct member variable 
};

struct OPCUADataBlockDefinition
{
    std::string data_block_name;
    int block_number = 0;
    bool optimized_access = false;
    int total_bytes_size = 0;
    std::vector<S7XMLVariableDefinition> variable_definitions_vector;   //  used for file member variable and struct member variable
};

class SCL_Parser
{
    public:
    explicit SCL_Parser( ) = default;
    ~SCL_Parser() = default;
    
    Result<std::string,RichError> read_file_content(const std::string& file_path);
    Result<OPCUADataBlockDefinition,RichError> parse(const std::string& file_path);
    Result<OPCUADataBlockDefinition,RichError> parse_data_block_header( std::string& file_content);
    Result<OPCUADataBlockDefinition,RichError> parse_variable( OPCUADataBlockDefinition& data_block_definition);
    Result<OPCUADataBlockDefinition,RichError> extract_variable_from_content(std::istringstream& file_content,std::string& line , OPCUADataBlockDefinition& data_block_definition );
    Result<S7XMLVariableDefinition,RichError> parse_variable_declaration( std::string& line);
    Result<bool,RichError> parse_data_type( std::string& str,S7XMLVariableDefinition& variable_definition);
    Result<OPCUADataBlockDefinition,RichError> calculate_data_block_size(OPCUADataBlockDefinition& data_block_definition);
    Result<bool,RichError> calculate_variable_offset(S7XMLVariableDefinition& var,int& last_free_byte_offset,int& current_bit_quality,float& last_var_bit_offset,S7DataType& last_data_S7_type);

    //  Remove leading and trailing whitespace characters from a string
    Result<std::string,RichError> trim(const std::string& str);
    Result<std::string,RichError>  extract_variable_name(const std::string& str);
    Result<std::string,RichError> extract_variable_comment(const std::string& str);
    Result<bool,RichError> add_array_member(S7XMLVariableDefinition& var);
    Result<S7DataType, RichError> transform_string_to_S7DataType(const std::string& str);
    Result<int,RichError> return_type_size(S7DataType type);
      
    Result<bool,RichError> is_odd_or_even();
    private:
    std::string m_file_content;
    size_t pos;
};

class DataTypeMapper {
public:
  static Result<NormalDataType, RichError>
  TransformBytesToSpecial(const S7DataType &type,
                          const std::vector<uint8_t> &bytes, int offset,
                          int string_length, int bit_offset) {
    switch (type) {
    //  the data order is little endian
    case S7DataType::BOOL: {
      auto tmp_byte = bytes[offset];
      if ((tmp_byte & (1 << bit_offset))) {
        return Result<NormalDataType, RichError>(bool{true});
      } else {
        return Result<NormalDataType, RichError>(bool{false});
      }
    }
    case S7DataType::BYTE:
      return Result<NormalDataType, RichError>(uint8_t{bytes[offset]});
    case S7DataType::INT: {
      int16_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(int16_t));
      return Result<NormalDataType, RichError>(int16_t{value});
    }
    case S7DataType::DINT: {
      int32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(int32_t));
      return Result<NormalDataType, RichError>(int32_t{value});
    }
    case S7DataType::REAL: {
      float value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(float));
      return Result<NormalDataType, RichError>(float{value});
    }
    case S7DataType::WORD: {
      uint16_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint16_t));
      return Result<NormalDataType, RichError>(uint16_t{value});
    }
    case S7DataType::UDINT: {
      uint32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
      return Result<NormalDataType, RichError>(uint32_t{value});
    }
    case S7DataType::DWORD: {
      uint32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
      return Result<NormalDataType, RichError>(uint32_t{value});
    }
    case S7DataType::STRING: {
      std::string value(reinterpret_cast<const char *>(&bytes[offset + 2]),
                        bytes[offset + 1]);
      return Result<NormalDataType, RichError>(NormalDataType{std::move(value)}
                                               // move 避免拷贝
      );
    }
    default:
      return Result<NormalDataType, RichError>(
          RichError("Unsupported data type"));
    }
  }

  static Result<bool, RichError> TransformBytesToDataType(
      const S7DataType &type, const std::vector<uint8_t> &bytes, int offset,
      int string_length, int bit_offset, ValueType&dataVar) {

    // 添加参数有效性检查
    if (bytes.empty()) {
      return Result<bool, RichError>(RichError("bytes vector is empty"));
    }

    // 检查对象是否有效
    if (type == S7DataType::UNKNOWN) {
      return Result<bool, RichError>(RichError("object type is UNKNOWN"));
    }

    // 检查偏移量是否越界
    if (offset < 0 || offset >= static_cast<int>(bytes.size())) {
      return Result<bool, RichError>(RichError("Offset out of range"));
    }

    switch (type) {
    case S7DataType::BOOL: {
      if (offset >= static_cast<int>(bytes.size())) {
        return Result<bool, RichError>(RichError("BOOL: offset out of range"));
      }
      auto tmp_byte = bytes[offset];
      bool value = (tmp_byte & (1 << bit_offset)) != 0;
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::BYTE: {
      if (offset >= static_cast<int>(bytes.size())) {
        return Result<bool, RichError>(RichError("BYTE: offset out of range"));
      }
      uint8_t value = bytes[offset];
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::INT: {
      if (offset + sizeof(int16_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("INT: insufficient data"));
      }
      int16_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(int16_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::DINT: {
      if (offset + sizeof(int32_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("DINT: insufficient data"));
      }
      int32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(int32_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::REAL: {
      if (offset + sizeof(float) > bytes.size()) {
        return Result<bool, RichError>(RichError("REAL: insufficient data"));
      }
      float value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(float));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::WORD: {
      if (offset + sizeof(uint16_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("WORD: insufficient data"));
      }
      uint16_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint16_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::UDINT: {
      if (offset + sizeof(uint32_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("UDINT: insufficient data"));
      }
      uint32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::DWORD: {
      if (offset + sizeof(uint32_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("DWORD: insufficient data"));
      }
      uint32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::STRING: {
      // 检查 STRING 格式：前两个字节是长度信息
      if (offset + 2 > static_cast<int>(bytes.size())) {
        return Result<bool, RichError>(
            RichError("STRING: insufficient data for header"));
      }

      uint8_t max_length = bytes[offset];        // 最大长度
      uint8_t actual_length = bytes[offset + 1]; // 实际长度

      // 验证长度
      if (actual_length > max_length) {
        return Result<bool, RichError>(RichError("STRING: invalid length"));
      }

      // 检查数据是否足够
      if (offset + 2 + actual_length > bytes.size()) {
        return Result<bool, RichError>(
            RichError("STRING: insufficient data for content"));
      }

      // 使用实际长度
      std::string value(
          reinterpret_cast<const char *>(bytes.data() + offset + 2),
          actual_length);
      dataVar = (value);
      break; // ✅ 添加 break
    }

    default: {
      // 未知类型
      return Result<bool, RichError>(RichError(
          "Unsupported data type: " + std::to_string(static_cast<int>(type))));
    }
    }

    // 所有分支成功执行
    return Result<bool, RichError>(true);
  }

 static Result<bool, RichError> TransformDataTypeToBytes(
      const S7DataType &type, const std::vector<uint8_t> &bytes, int offset,
      int string_length, int bit_offset, ValueType&dataVar) {

    // 添加参数有效性检查
    if (bytes.empty()) {
      return Result<bool, RichError>(RichError("bytes vector is empty"));
    }

    // 检查对象是否有效
    if (type == S7DataType::UNKNOWN) {
      return Result<bool, RichError>(RichError("object type is UNKNOWN"));
    }

    // 检查偏移量是否越界
    if (offset < 0 || offset >= static_cast<int>(bytes.size())) {
      return Result<bool, RichError>(RichError("Offset out of range"));
    }

    switch (type) {
    case S7DataType::BOOL: {
      if (offset >= static_cast<int>(bytes.size())) {
        return Result<bool, RichError>(RichError("BOOL: offset out of range"));
      }
      auto tmp_byte = bytes[offset];
      bool value = (tmp_byte & (1 << bit_offset)) != 0;
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::BYTE: {
      if (offset >= static_cast<int>(bytes.size())) {
        return Result<bool, RichError>(RichError("BYTE: offset out of range"));
      }
      uint8_t value = bytes[offset];
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::INT: {
      if (offset + sizeof(int16_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("INT: insufficient data"));
      }
      int16_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(int16_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::DINT: {
      if (offset + sizeof(int32_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("DINT: insufficient data"));
      }
      int32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(int32_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::REAL: {
      if (offset + sizeof(float) > bytes.size()) {
        return Result<bool, RichError>(RichError("REAL: insufficient data"));
      }
      float value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(float));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::WORD: {
      if (offset + sizeof(uint16_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("WORD: insufficient data"));
      }
      uint16_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint16_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::UDINT: {
      if (offset + sizeof(uint32_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("UDINT: insufficient data"));
      }
      uint32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::DWORD: {
      if (offset + sizeof(uint32_t) > bytes.size()) {
        return Result<bool, RichError>(RichError("DWORD: insufficient data"));
      }
      uint32_t value = 0;
      std::memcpy(&value, bytes.data() + offset, sizeof(uint32_t));
      
      dataVar = value;
      break; // ✅ 添加 break
    }

    case S7DataType::STRING: {
      // 检查 STRING 格式：前两个字节是长度信息
      if (offset + 2 > static_cast<int>(bytes.size())) {
        return Result<bool, RichError>(
            RichError("STRING: insufficient data for header"));
      }

      uint8_t max_length = bytes[offset];        // 最大长度
      uint8_t actual_length = bytes[offset + 1]; // 实际长度

      // 验证长度
      if (actual_length > max_length) {
        return Result<bool, RichError>(RichError("STRING: invalid length"));
      }

      // 检查数据是否足够
      if (offset + 2 + actual_length > bytes.size()) {
        return Result<bool, RichError>(
            RichError("STRING: insufficient data for content"));
      }

      // 使用实际长度
      std::string value(
          reinterpret_cast<const char *>(bytes.data() + offset + 2),
          actual_length);
      dataVar = (value);
      break; // ✅ 添加 break
    }

    default: {
      // 未知类型
      return Result<bool, RichError>(RichError(
          "Unsupported data type: " + std::to_string(static_cast<int>(type))));
    }
    }

    // 所有分支成功执行
    return Result<bool, RichError>(true);
  }


  static std::string transform_uint32_to_string(uint32_t value) {
    return std::to_string(value);
  }

  static std::string transform_uint32_to_hex_string(uint32_t value) {
    std::ostringstream oss;
    oss << "0x" << std::hex << value;
    return oss.str();
  }
};

#endif