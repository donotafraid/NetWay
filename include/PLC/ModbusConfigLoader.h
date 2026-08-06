#pragma once

#include <vector>
#include "PLC/ModbusDataNode.h"

struct ModbusDeviceConfig {
    std::string device_name;
    std::string ip_address;
    int port;
    int slave_id;
};

// loadFromJSON 返回一个包含配置和节点的复合结构
struct ModbusLoadResult {
    ModbusDeviceConfig device_config;
    std::vector<ModbusDataStruct> registers;
};


// ModbusConfigLoader.h
class ModbusConfigLoader {
public:
    // 从 JSON 文件加载，返回一组 ModbusDataNode
    Result<ModbusLoadResult, RichError> 
    loadFromJSON(const std::string& file_path);
    
    // 从 CSV 加载（方便工程师用 Excel 编辑）
    Result<ModbusLoadResult, RichError> 
    loadFromCSV(const std::string& file_path);
};