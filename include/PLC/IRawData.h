#pragma once
#include "PLC/Struct.h"
#include <iostream>
#include <string>
#include <fstream>
#include <regex>
#include <set>

#include "PLC/ModbusDataStruct.h"

// 放在业务逻辑层，永远不需要为 Modbus 修改
struct RawDataField {
    std::string name;          // 变量名（通用）
    std::string data_type;     // "BOOL", "INT", "REAL"（通用）
    std::string value;         // 默认值（通用）
    
    // 关键：容纳所有“未知的、特定于协议的”键值对
    std::unordered_map<std::string, std::string> metadata; 
};

using RawDataTable = std::vector<RawDataField>;

// Modbus 专属映射器
class ModbusMapper {
public:
    std::vector<ModbusDataStruct> map(const RawDataTable& raw) {
        // 遍历 raw，构建 Modbus 结构体（完全独立的逻辑）
        return std::vector<ModbusDataStruct>{};
    }
};