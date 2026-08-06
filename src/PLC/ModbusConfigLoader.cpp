// ModbusConfigLoader.cpp

#include <fstream>
#include <iostream>
#include "PLC/ModbusConfigLoader.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

Result<ModbusLoadResult, RichError>
ModbusConfigLoader::loadFromJSON(const std::string &file_path) {
  // 1. 读取文件内容
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return Result<ModbusLoadResult, RichError>(
        RichError{"Failed to open file: " + file_path});
  }

  // 2. 解析 JSON
  json root;
  try {
    file >> root;
  } catch (const json::parse_error &e) {
    return Result<ModbusLoadResult, RichError>(
        RichError{"JSON parse error: " + std::string(e.what())});
  }

  ModbusDeviceConfig device_config;
  // 3. 提取设备连接参数（必须字段）
  {
    device_config.device_name = (root.at("device_name").get<std::string>());
    device_config.ip_address = (root.at("ip_address").get<std::string>());
    device_config.port = (root.at("port").get<int>());
    device_config.slave_id = (root.at("slave_id").get<int>());
  }

  std::vector<ModbusDataStruct> dataVec;
  // 4. 提取 registers 数组（如果存在）
  if (root.contains("registers") && root["registers"].is_array()) {
    for (const auto &item : root["registers"]) {
      ModbusDataStruct reg;

      // 必需的字段（缺少则跳过或报错）
      {
        reg.variable_name = item.at("name").get<std::string>();
        reg.address = item.at("address").get<int>();
        reg.scale_factor = item.value("scale_factor", 1.0);
        reg.register_count = item.value("register_count", 1);
        reg.description = item.value("description", "");

        // node_type 字符串映射为枚举
        std::string node_type_str = item.at("node_type").get<std::string>();
        if (node_type_str == "HoldingRegister") {
          reg.node_type = ModbusNodeType::HoldingRegister;
        } else if (node_type_str == "InputRegister") {
          reg.node_type = ModbusNodeType::InputRegister;
        } else if (node_type_str == "Coil") {
          reg.node_type = ModbusNodeType::Coil;
        } else if (node_type_str == "DiscreteInput") {
          reg.node_type = ModbusNodeType::DiscreteInput;
        } else {
          return Result<ModbusLoadResult, RichError>(
              RichError{"Unknown node_type: " + node_type_str});
        }

        // data_type 映射（这里使用你已有的 S7DataType）
        std::string data_type_str = item.at("data_type").get<std::string>();
        if (data_type_str == "BOOL") {
          reg.data_type_enum = S7DataType::BOOL;
        } else if (data_type_str == "BYTE") {
          reg.data_type_enum = S7DataType::BYTE;
        } else if (data_type_str == "INT") {
          reg.data_type_enum = S7DataType::INT;
        } else if (data_type_str == "REAL") {
          reg.data_type_enum = S7DataType::REAL;
        } else if (data_type_str == "DINT") {
          reg.data_type_enum = S7DataType::DINT;
        } else if (data_type_str == "WORD") {
          reg.data_type_enum = S7DataType::WORD;
        } else if (data_type_str == "DWORD") {
          reg.data_type_enum = S7DataType::DWORD;
        } else if (data_type_str == "UDINT") {
          reg.data_type_enum = S7DataType::UDINT;
        } else {
          return Result<ModbusLoadResult, RichError>(
              RichError{"Unknown data_type: " + data_type_str});
        }

        // 生成完整路径（用于UI标识）
        reg.variable_full_path =
            device_config.device_name + "." + reg.variable_name;

        // 初始缓存值设为默认（后续读取后会刷新）
        reg.cached_value = QVariant();
        reg.filter_reason = "";

        // 填入从站 ID（从设备配置继承）
        reg.slave_id = device_config.slave_id;
      }

      dataVec.push_back(std::move(reg));
    }
  }

  return Result<ModbusLoadResult, RichError>({device_config,dataVec});
}