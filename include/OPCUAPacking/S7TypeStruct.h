#pragma once

#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <variant>

// 通用数据值类型 - 支持所有PLC数据类型
using NormalDataType = std::variant<
    bool,         // BOOL
    int8_t,       // SINT
    uint8_t,      // BYTE
    int16_t,      // INT
    uint16_t,     // WORD
    int32_t,      // DINT
    uint32_t,     // UDINT, DWORD
    int64_t,      // LINT
    uint64_t,     // ULINT
    float,        // REAL
    double,       // LREAL
    std::string   // STRING
>;

enum class S7DataType
{
    BOOL,    // QCheckBox
    SINT,    // QSpinBox (范围: -128 ~ 127)
    BYTE,    // QSpinBox (范围: 0 ~ 255)
    INT,     // QSpinBox (范围: -32768 ~ 32767)
    WORD,    // QSpinBox (范围: 0 ~ 65535)
    DINT,    // QSpinBox (范围: -2147483648 ~ 2147483647)
    UDINT,   // QLineEdit (范围: 0 ~ 4294967295)
    DWORD,   // QLineEdit (范围: 0 ~ 4294967295)
    LINT,    // QLineEdit (范围: -2^63 ~ 2^63-1)
    ULINT,   // QLineEdit (范围: 0 ~ 2^64-1)
    REAL,    // QDoubleSpinBox (单精度浮点数)
    LREAL,   // QDoubleSpinBox (双精度浮点数)
    STRING,  // QLineEdit
    ARRAY,   // 自定义控件或QListWidget
    STRUCT,  // 自定义控件或QGroupBox
    UNKNOWN
};

// Returns the zero value for every scalar PLC type supported by
// OPC_UA_Client. ARRAY / STRUCT / UNKNOWN have no representation in
// NormalDataType and throw std::invalid_argument; callers must handle those
// before asking for a default. Defined inline so a consumer of this public
// header needs no external definition (the header is usable from an
// OPCUAPacking-only translation unit).
inline NormalDataType default_value_for(S7DataType type) {
  switch (type) {
  case S7DataType::BOOL:
    return false;
  case S7DataType::SINT:
    return int8_t{0};
  case S7DataType::BYTE:
    return uint8_t{0};
  case S7DataType::INT:
    return int16_t{0};
  case S7DataType::WORD:
    return uint16_t{0};
  case S7DataType::DINT:
    return int32_t{0};
  case S7DataType::UDINT:
  case S7DataType::DWORD:
    return uint32_t{0};
  case S7DataType::LINT:
    return int64_t{0};
  case S7DataType::ULINT:
    return uint64_t{0};
  case S7DataType::REAL:
    return 0.0f;
  case S7DataType::LREAL:
    return 0.0;
  case S7DataType::STRING:
    return std::string{};
  case S7DataType::ARRAY:
  case S7DataType::STRUCT:
  case S7DataType::UNKNOWN:
    throw std::invalid_argument(
        "default_value_for: ARRAY/STRUCT/UNKNOWN have no scalar default");
  }
  throw std::invalid_argument("default_value_for: unhandled S7DataType");
}


inline const std::map<S7DataType, std::string> S7DataTypeToString = {
    {S7DataType::BOOL,    "BOOL"},
    {S7DataType::SINT,    "SINT"},
    {S7DataType::BYTE,    "BYTE"},
    {S7DataType::INT,     "INT"},
    {S7DataType::WORD,    "WORD"},
    {S7DataType::DINT,    "DINT"},
    {S7DataType::UDINT,   "UDINT"},
    {S7DataType::DWORD,   "DWORD"},
    {S7DataType::LINT,    "LINT"},
    {S7DataType::ULINT,   "ULINT"},
    {S7DataType::REAL,    "REAL"},
    {S7DataType::LREAL,   "LREAL"},
    {S7DataType::STRING,  "STRING"},
    {S7DataType::ARRAY,   "ARRAY"},
    {S7DataType::STRUCT,  "STRUCT"},
    {S7DataType::UNKNOWN, "UNKNOWN"},
};


// 建立字符串到枚举的映射表
inline const std::unordered_map<std::string, S7DataType> typeMap = {
    {"BOOL",    S7DataType::BOOL},
    {"SINT",    S7DataType::SINT},
    {"BYTE",    S7DataType::BYTE},
    {"INT",     S7DataType::INT},
    {"WORD",    S7DataType::WORD},
    {"DINT",    S7DataType::DINT},
    {"UDINT",   S7DataType::UDINT},
    {"DWORD",   S7DataType::DWORD},
    {"LINT",    S7DataType::LINT},
    {"ULINT",   S7DataType::ULINT},
    {"REAL",    S7DataType::REAL},
    {"LREAL",   S7DataType::LREAL},
    {"STRING",  S7DataType::STRING},
    {"ARRAY",   S7DataType::ARRAY},
    {"STRUCT",  S7DataType::STRUCT},
    {"UNKNOWN", S7DataType::UNKNOWN},
};