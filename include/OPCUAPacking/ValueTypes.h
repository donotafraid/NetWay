#pragma  once
#include <string>
#include <variant>

#include "OPCUAPacking/S7TypeStruct.h"

using ValueType = std::variant<
    bool,        // BOOL    - UA_TYPES_BOOLEAN
    int8_t,      // SINT    - UA_TYPES_SBYTE
    uint8_t,     // BYTE    - UA_TYPES_BYTE
    int16_t,     // INT     - UA_TYPES_INT16
    uint16_t,    // WORD    - UA_TYPES_UINT16
    int32_t,     // DINT    - UA_TYPES_INT32
    uint32_t,    // UDINT   - UA_TYPES_UINT32
    int64_t,     // LINT    - UA_TYPES_INT64
    uint64_t,    // ULINT   - UA_TYPES_UINT64
    float,       // REAL    - UA_TYPES_FLOAT
    double,      // LREAL   - UA_TYPES_DOUBLE
    std::string  // STRING  - UA_TYPES_STRING
>;