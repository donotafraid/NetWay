#include "PLC/StructuredDataNode.h"

// ==================== 构造函数 ====================
StructuredDataNode::StructuredDataNode(const std::vector<OPCUAModernDataStruct> &varVec)
    :  m_cacheValid(false)
{
  m_varVec = std::move(varVec); // 使用 move 避免拷贝
  if (!m_varVec.empty()) {
    m_var = &m_varVec[0]; // 指向第一个元素
  } else {
    m_var = nullptr;
  }
}

StructuredDataNode::StructuredDataNode(OPCUAModernDataStruct *var)
    : m_cacheValid(false) {
  m_var = var;
}

// ==================== 元信息接口实现 ====================
std::string StructuredDataNode::getName() const {
    // 注意：如果 m_var 为 nullptr，这里会崩溃
    // 建议添加防御性检查
    if (!m_var) {
        return "";  // 或抛出异常
    }
    return m_var->variable_name;
}

S7DataType StructuredDataNode::getDataType() const {
    if (!m_var) {
        return S7DataType::UNKNOWN;  // 假设有 UNKNOWN 枚举值
    }
    return m_var->data_type_enum;
}

int StructuredDataNode::getAccessLevel() const {
    if (!m_var) {
        return 0;
    }
    return m_var->access_level;
}

std::string StructuredDataNode::getProtocolType() const {
    return "S7/OPCUA";
}

std::string StructuredDataNode::getNodeId() const {
    if (!m_var) {
        return "";
    }
    return m_var->variable_nodeID;
}

std::string StructuredDataNode::getDescrition() const {
    if (!m_var) {
        return "";
    }
    return m_var->description;
}

// ==================== 读取操作实现 ====================
Result<QVariant, RichError> StructuredDataNode::readValue() {
    if (!m_cacheValid) {
        return Result<QVariant, RichError>(RichError{"error cache"});
    }
    
    if (!m_var) {
        return Result<QVariant, RichError>(RichError{"m_var is nullptr"});
    }
    
    return Result<QVariant, RichError>(m_var->dataVar);
}

Result<bool, RichError> StructuredDataNode::readValueFromPLC() {
    if (!m_reader) {
        return Result<bool, RichError>(RichError{"m_reader is nullptr"});
    }

    auto result = m_reader->batchReadOPCUADataBlock_FromPLC(m_varVec);
    if(result.is_success())
    {
        emitBatchData();
    }
    return result;
}

// ==================== 写入操作实现 ====================
Result<bool, RichError> StructuredDataNode::writeValue(const QVariant& value) {
    // 1. 先检查 m_var 是否有效
    if (!m_var) {
        return Result<bool, RichError>(RichError{"m_var is nullptr"});
    }

    // 2. 类型检查和范围验证
    switch (m_var->data_type_enum) {
    case S7DataType::BOOL: {
        if (!value.canConvert<bool>()) {
            return Result<bool, RichError>(RichError{"Invalid type for BOOL"});
        }
        bool boolValue = value.toBool();
        m_var->dataVar = boolValue;
        m_cacheValid = true;
        break;
    }

    case S7DataType::BYTE: {
        if (!value.canConvert<int>()) {
            return Result<bool, RichError>(RichError{"Invalid type for BYTE"});
        }
        int intValue = value.toInt();
        if (intValue < 0 || intValue > 255) {
            return Result<bool, RichError>(
                RichError{"BYTE value out of range (0-255)"});
        }
        m_var->dataVar = static_cast<uint8_t>(intValue);
        m_cacheValid = true;
        break;
    }

    case S7DataType::INT: {
        if (!value.canConvert<int>()) {
            return Result<bool, RichError>(RichError{"Invalid type for INT"});
        }
        int intValue = value.toInt();
        if (intValue < -32768 || intValue > 32767) {
            return Result<bool, RichError>(
                RichError{"INT value out of range (-32768 to 32767)"});
        }
        m_var->dataVar = static_cast<int16_t>(intValue);
        m_cacheValid = true;
        break;
    }

    case S7DataType::DINT: {
        if (!value.canConvert<qlonglong>()) {
            return Result<bool, RichError>(RichError{"Invalid type for DINT"});
        }
        qlonglong longValue = value.toLongLong();
        if (longValue < -2147483648LL || longValue > 2147483647LL) {
            return Result<bool, RichError>(
                RichError{"DINT value out of range (-2147483648 to 2147483647)"});
        }
        m_var->dataVar = static_cast<int32_t>(longValue);
        m_cacheValid = true;
        break;
    }

    case S7DataType::WORD: {
        if (!value.canConvert<int>()) {
            return Result<bool, RichError>(RichError{"Invalid type for WORD"});
        }
        int intValue = value.toInt();
        if (intValue < 0 || intValue > 65535) {
            return Result<bool, RichError>(
                RichError{"WORD value out of range (0-65535)"});
        }
        m_var->dataVar = static_cast<uint16_t>(intValue);
        m_cacheValid = true;
        break;
    }

    case S7DataType::DWORD:
    case S7DataType::UDINT: {
        if (!value.canConvert<quint32>()) {
            return Result<bool, RichError>(
                RichError{"Invalid type for DWORD/UDINT"});
        }
        quint32 uintValue = value.toUInt();
        // DWORD 和 UDINT 都是 32 位无符号，范围 0 ~ 4294967295，无需额外校验
        m_var->dataVar = static_cast<uint32_t>(uintValue);
        m_cacheValid = true;
        break;
    }

    case S7DataType::REAL: {
        if (!value.canConvert<float>()) {
            return Result<bool, RichError>(RichError{"Invalid type for REAL"});
        }
        float floatValue = value.toFloat();
        // REAL（32位浮点）无需范围校验，任何 float 都合法
        m_var->dataVar = floatValue;
        m_cacheValid = true;
        break;
    }

    case S7DataType::STRING: {
        if (!value.canConvert<QString>()) {
            return Result<bool, RichError>(RichError{"Invalid type for STRING"});
        }
        QString strValue = value.toString();
        // 可选：增加字符串长度校验（根据 S7 定义的最大长度）
        if (strValue.length() > 254) { // 假设最大 254 字符
            return Result<bool, RichError>(
                RichError{"STRING value exceeds maximum length"});
        }
        m_var->dataVar = strValue;
        m_cacheValid = true;
        break;
    }

    default: {
        return Result<bool, RichError>(
            RichError{"Unsupported S7 data type for writing"});
    }
    }

    // ✅ 写入成功，返回 true
    return Result<bool, RichError>(true);
}

Result<bool, RichError> StructuredDataNode::writeValueToPLC() {
  if (!m_reader) {
    return Result<bool, RichError>(RichError{"m_reader is nullptr"});
  }
  auto result = m_reader->batchWriteOPCUABlock_ToPLC(m_varVec);
  if (result.is_success()) {
    emitBatchData();
  }
  return result;
}


// trim function=======================================
void StructuredDataNode::emitBatchData() {
    std::vector<PLCData> records;
    records.reserve(m_varVec.size()); // 预分配内存
    
    for (const auto& var : m_varVec) {
        // 只收集有实际值的有效变量，忽略系统变量和数组模板
        if (var.filter_reason.empty() && var.data_type_enum != S7DataType::UNKNOWN) {
            PLCData data;
            data.protocol = "opcua";  // 或从路径推断
            data.tag_name = var.variable_nodeID; // 完整路径作为唯一标识
            data.value = var.dataVar.toFloat(); // 统一转为数值或字符串
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