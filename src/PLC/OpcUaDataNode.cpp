#include "PLC/OpcUaDataNode.h"

// ==================== 构造函数 ====================
OpcUaDataNode::OpcUaDataNode(OPCUAModernDataStruct &var)
    : m_var(var),m_dirty (true) {
}

OpcUaDataNode::OpcUaDataNode(OPCUAModernDataStruct &&var)
    : m_var(std::move(var)),m_dirty (true) {
}

// ==================== 元信息接口实现 ====================
std::string OpcUaDataNode::getName() const {
    // 注意：如果 m_var 为 nullptr，这里会崩溃
    // 建议添加防御性检查
    return m_var.variable_name;
}

S7DataType OpcUaDataNode::getDataType() const {
    return m_var.data_type_enum;
}

int OpcUaDataNode::getAccessLevel() const {
    return m_var.access_level;
}

std::string OpcUaDataNode::getProtocolType() const {
  if (m_var.isOPCUAType) {
    return "OPCUA";
  } else {
    return "S7";
  }
}

std::string OpcUaDataNode::getNodeId() const {
    return m_var.variable_nodeID;
}

std::string OpcUaDataNode::getDescrition() const {
    return m_var.description;
}

std::string OpcUaDataNode::getParentPath() const {
    return m_var.parent_path;
}

std::string OpcUaDataNode::getFullPath() const {
  return m_var.variable_full_path;
}

void OpcUaDataNode::setRawValue(const ValueType &value) {
  m_var.dataValue = std::move(value);
}

bool OpcUaDataNode::isDirty() const {
    return m_dirty ;
}
void OpcUaDataNode::clearDirty() {
    m_dirty  = false;
} 

void OpcUaDataNode::setDataTypeLength(int val) {
    m_var.s7_data_type_length = val;
}

void OpcUaDataNode::setDataByte(const float &val) {
    m_var.bytes_offset = val;
}

void OpcUaDataNode::setDataBit(int val) { m_var.bit_offset = val; }

void OpcUaDataNode::setPendingStringLength(int val) {
    m_var.pendingStringLength = val;
}

int OpcUaDataNode::getDataTypeLength() {
    return m_var.s7_data_type_length;
}

float OpcUaDataNode::getDataByte() {
    return m_var.bytes_offset;
}

int OpcUaDataNode::getDataBit() {
    return m_var.bit_offset;
}

int OpcUaDataNode::getPendingStringLength() {
    return m_var.pendingStringLength.value();
}

int OpcUaDataNode::getNameSpace()
{
    return m_var.namespace_index;
}

std::string OpcUaDataNode::getFilter() {
    return m_var.filter_reason;
}

// ==================== 读取操作实现 ====================
ValueType OpcUaDataNode::readValue() const {
  return (m_var.dataValue);
}

// ==================== 写入操作实现 ====================
Result<bool, RichError> OpcUaDataNode::writeValue(const ValueType& value) {
    // 2. 类型检查和范围验证
    switch (m_var.data_type_enum) {
    case S7DataType::BOOL: {
        m_var.dataValue = value;
        m_dirty  = true;
        break;
    }

    case S7DataType::BYTE: {
      m_var.dataValue = value;
        m_dirty  = true;
        break;
    }

    case S7DataType::INT: {
  m_var.dataValue = value;
        m_dirty  = true;
        break;
    }

    case S7DataType::DINT: {
         m_var.dataValue = value;
        m_dirty  = true;
        break;
    }

    case S7DataType::WORD: {
         m_var.dataValue = value;
        m_dirty  = true;
        break;
    }

    case S7DataType::DWORD:
    case S7DataType::UDINT: {
          m_var.dataValue = value;
        m_dirty  = true;
        break;
    }

    case S7DataType::REAL: {
        m_var.dataValue = value;
        m_dirty  = true;
        break;
    }

    case S7DataType::STRING: {
        m_var.dataValue = value;
        m_dirty  = true;
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

// trim function=======================================