#pragma  once
#include "PLC/S7TypeStruct.h"

using ValueType = std::variant<
    bool,        // BOOL    - UA_TYPES_BOOLEAN
    int8_t,      // SINT    - UA_TYPES_SBYTE    ← 新增
    uint8_t,     // BYTE    - UA_TYPES_BYTE
    int16_t,     // INT     - UA_TYPES_INT16
    uint16_t,    // WORD    - UA_TYPES_UINT16
    int32_t,     // DINT    - UA_TYPES_INT32
    uint32_t,    // UDINT   - UA_TYPES_UINT32
    int64_t,     // LINT    - UA_TYPES_INT64    ← 新增
    uint64_t,    // ULINT   - UA_TYPES_UINT64   ← 新增
    float,       // REAL    - UA_TYPES_FLOAT
    double,      // LREAL   - UA_TYPES_DOUBLE   ← 新增
    std::string  // STRING  - UA_TYPES_STRING
>;

struct WriteRequest {
  std::string tagName; // 逻辑标签名，如 "Motor.Speed" 或 "DB10.DBW20"
  ValueType value;
};

// 纯 C++ 结构体，不依赖任何协议 SDK
struct PhysicalAddress {
    // 不同协议使用不同的字段，用 std::variant 或简单结构体
    // 这里用最通用的形式，实际项目中可用 variant
    std::string protocolType; // "S7", "Modbus", "OPCUA"
    int registerAddr = 0;     // Modbus 专用
    int byteOffset = 0;       // S7/Modbus 专用
    int bitOffset = -1;       // S7 位寻址
    int data_type_length = 0; //  = maxLength + currentUseLength + content

    int nameSpace = 0;        // OPC UA 专用
    std::string nodeId;       // OPC UA 专用
    std::string fullPath;     // OPC UA 专用
    S7DataType dataType = S7DataType::UNKNOWN; // OPC UA 专用
};