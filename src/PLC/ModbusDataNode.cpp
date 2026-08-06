#include "PLC/ModbusDataNode.h"

// ==================== 构造函数 ====================
ModbusDataNode::ModbusDataNode(const std::vector<ModbusDataStruct> &varVec)
    :  m_cacheValid(false)
{
  m_varVec = std::move(varVec); // 使用 move 避免拷贝
  if (!m_varVec.empty()) {
    var = &m_varVec[0]; // 指向第一个元素
  } else {
    var = nullptr;
  }
}

ModbusDataNode::ModbusDataNode(std::shared_ptr<ModbusMediator> &modbusReader,ModbusDataStruct *var)
    : m_cacheValid(false) {
  var = var;
  reader = modbusReader;
}

// ==================== 元信息接口实现 ====================
std::string ModbusDataNode::getName() const {
    return var->variable_name;
}

S7DataType ModbusDataNode::getDataType() const {
    return var->data_type_enum;
}

std::string ModbusDataNode::getProtocolType() const {
    return "MODBUS";
}

std::string ModbusDataNode::getNodeId() const {
  return var->variable_full_path;
}

int ModbusDataNode::getAccessLevel() const {
    return 1; // Modbus 默认可读写
}

std::string ModbusDataNode::getDescrition() const {
    return std::string{""};
}

// ==================== 读取操作实现 ====================
Result<QVariant, RichError> ModbusDataNode::readValue() {
    if (!m_cacheValid) {
        return Result<QVariant, RichError>(RichError{"error cache"});
    } else {
        return Result<QVariant, RichError>(QVariant(var->cached_value));
    }
}

Result<bool, RichError> ModbusDataNode::readValueFromPLC() {
    // TODO: 实现从 PLC 读取的逻辑
    auto result = reader.lock()->batchReadNode(m_varVec);
    emitBatchData();
    return Result<bool, RichError>(result);
}

// ==================== 写入操作实现 ====================
Result<bool, RichError> ModbusDataNode::writeValue(const QVariant &value) {
    switch (var->data_type_enum) {
    case S7DataType::BOOL: {
        if (!value.canConvert<bool>()) {
            m_cacheValid = false;
            return Result<bool, RichError>(RichError{"Invalid type for BOOL"});
        }
        var->cached_value = value;
        m_cacheValid = true;
        break;
    }

    case S7DataType::BYTE: {
        if (!value.canConvert<int>()) {
            m_cacheValid = false;
            return Result<bool, RichError>(RichError{"Invalid type for BYTE"});
        }
        int intValue = value.toInt();
        if (intValue < 0 || intValue > 255) {
            m_cacheValid = false;
            return Result<bool, RichError>(
                RichError{"BYTE value out of range (0-255)"});
        }
        var->cached_value = value;
        m_cacheValid = true;
        break;
    }

    case S7DataType::INT: {
        if (!value.canConvert<int>()) {
            m_cacheValid = false;
            return Result<bool, RichError>(RichError{"Invalid type for INT"});
        }
        int intValue = value.toInt();
        if (intValue < -32768 || intValue > 32767) {
            m_cacheValid = false;
            return Result<bool, RichError>(
                RichError{"INT value out of range (-32768 to 32767)"});
        }
        var->cached_value = value;
        m_cacheValid = true;
        break;
    }

    case S7DataType::WORD: {
        if (!value.canConvert<int>()) {
            m_cacheValid = false;
            return Result<bool, RichError>(RichError{"Invalid type for WORD"});
        }
        int intValue = value.toInt();
        if (intValue < 0 || intValue > 65535) {
            m_cacheValid = false;
            return Result<bool, RichError>(
                RichError{"WORD value out of range (0-65535)"});
        }
        var->cached_value = value;
        m_cacheValid = true;
        break;
    }

    case S7DataType::DWORD:
    case S7DataType::UDINT: {
        if (!value.canConvert<quint32>()) {
            m_cacheValid = false;
            return Result<bool, RichError>(
                RichError{"Invalid type for DWORD/UDINT"});
        }
        quint32 uintValue = value.toUInt();
        // DWORD 和 UDINT 都是 32 位无符号，范围 0 ~ 4294967295，无需额外校验
        var->cached_value = value;
        m_cacheValid = true;
        break;
    }

    case S7DataType::REAL: {
        if (!value.canConvert<float>()) {
            m_cacheValid = false;
            return Result<bool, RichError>(
                RichError{"Invalid type for REAL"});
        }
        float floatValue = value.toFloat();
        // REAL（32位浮点）无需范围校验，任何 float 都合法
        var->cached_value = value;
        m_cacheValid = true;
        break;
    }

    default: {
        m_cacheValid = false;
        return Result<bool, RichError>(
            RichError{"Unsupported S7 data type for writing"});
    }
    }
    
    // 这个 return 实际上不会执行到，因为 switch 中每个 case 都有 return
    return Result<bool, RichError>(RichError{"Unsupported S7 data type for writing"});
}

Result<bool, RichError> ModbusDataNode::writeValueToPLC() {
  auto result = reader.lock()->batchWriteNode(m_varVec);
  emitBatchData();
  return Result<bool, RichError>(result);
}

// trim function=======================================
void ModbusDataNode::emitBatchData() {
    std::vector<PLCData> records;
    records.reserve(m_varVec.size()); // 预分配内存
    
    for (const auto& var : m_varVec) {
        // 只收集有实际值的有效变量，忽略系统变量和数组模板
        if (var.filter_reason.empty() && var.data_type_enum != S7DataType::UNKNOWN) {
            PLCData data;
            data.protocol = "Modbus";  // 或从路径推断
            data.tag_name = var.variable_full_path; // 完整路径作为唯一标识
            data.value = var.cached_value.toFloat(); // 统一转为数值或字符串
            data.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            data.quality = 0;
            records.push_back(std::move(data));
        }
    }
    
    // ✅ 发射信号，通知 SystemManager
    emit batchDataReady(records);
}