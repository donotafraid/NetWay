#pragma once

#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cstring>
#include <map>

// 通用数据值类型 - 支持所有PLC数据类型
using NormalDataType = std::variant<
    bool,         // BOOL
    int8_t,       // SINT      ← 新增
    uint8_t,      // BYTE
    int16_t,      // INT
    uint16_t,     // WORD
    int32_t,      // DINT
    uint32_t,     // UDINT, DWORD
    int64_t,      // LINT      ← 新增
    uint64_t,     // ULINT     ← 新增
    float,        // REAL
    double,       // LREAL     ← 新增
    std::string   // STRING
>;

enum class S7DataType
{
    BOOL,    // QCheckBox 
    SINT,    // QSpinBox (范围: -128 ~ 127)  ← 新增
    BYTE,    // QSpinBox (范围: 0 ~ 255)
    INT,     // QSpinBox (范围: -32768 ~ 32767)
    WORD,    // QSpinBox (范围: 0 ~ 65535)
    DINT,    // QSpinBox (范围: -2147483648 ~ 2147483647)
    UDINT,   // QLineEdit (范围: 0 ~ 4294967295)
    DWORD,   // QLineEdit (范围: 0 ~ 4294967295)
    LINT,    // QLineEdit (范围: -2^63 ~ 2^63-1)  ← 新增
    ULINT,   // QLineEdit (范围: 0 ~ 2^64-1)      ← 新增
    REAL,    // QDoubleSpinBox (单精度浮点数)
    LREAL,   // QDoubleSpinBox (双精度浮点数)      ← 新增
    STRING,  // QLineEdit
    ARRAY,   // 自定义控件或QListWidget
    STRUCT,  // 自定义控件或QGroupBox
    UNKNOWN 
};

NormalDataType default_value_for(S7DataType type); 



inline const std::map<S7DataType, std::string> S7DataTypeToString = {
    {S7DataType::BOOL,    "BOOL"},
    {S7DataType::SINT,    "SINT"},      // 新增
    {S7DataType::BYTE,    "BYTE"},
    {S7DataType::INT,     "INT"},
    {S7DataType::WORD,    "WORD"},
    {S7DataType::DINT,    "DINT"},
    {S7DataType::UDINT,   "UDINT"},
    {S7DataType::DWORD,   "DWORD"},
    {S7DataType::LINT,    "LINT"},      // 新增
    {S7DataType::ULINT,   "ULINT"},     // 新增
    {S7DataType::REAL,    "REAL"},
    {S7DataType::LREAL,   "LREAL"},     // 新增
    {S7DataType::STRING,  "STRING"},
    {S7DataType::ARRAY,   "ARRAY"},
    {S7DataType::STRUCT,  "STRUCT"},
    {S7DataType::UNKNOWN, "UNKNOWN"},
};


// 建立字符串到枚举的映射表
inline const std::unordered_map<std::string, S7DataType> typeMap = {
    {"BOOL",    S7DataType::BOOL},
    {"SINT",    S7DataType::SINT},      // 新增
    {"BYTE",    S7DataType::BYTE},
    {"INT",     S7DataType::INT},
    {"WORD",    S7DataType::WORD},
    {"DINT",    S7DataType::DINT},
    {"UDINT",   S7DataType::UDINT},
    {"DWORD",   S7DataType::DWORD},
    {"LINT",    S7DataType::LINT},      // 新增
    {"ULINT",   S7DataType::ULINT},     // 新增
    {"REAL",    S7DataType::REAL},
    {"LREAL",   S7DataType::LREAL},     // 新增
    {"STRING",  S7DataType::STRING},
    {"ARRAY",   S7DataType::ARRAY},
    {"STRUCT",  S7DataType::STRUCT},
    {"UNKNOWN", S7DataType::UNKNOWN},   // 新增
};

/**
 * 检查字符串中是否存在小数点以及小数点后面是否存在反斜杠
 * @param str 要检查的字符串
 * @param hasDot 输出：是否存在小数点
 * @param hasBackslashAfterDot 输出：小数点后面是否存在反斜杠
 * @return 返回码：0-成功，-1-参数无效
 */
inline int checkDotAndBackslash(const std::string &str) {
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