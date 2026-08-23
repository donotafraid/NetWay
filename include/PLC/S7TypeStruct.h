#pragma once

#include <cstdlib>
#include <cstring>
#include <map>

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