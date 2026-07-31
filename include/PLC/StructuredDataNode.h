#pragma once
#include "PLC/Struct.h"
#include "PLC_Collector/IDataNode.h"
#include "spdlog/spdlog.h"

class StructuredDataNode : public IDataNode {
private:
  OPCUAModernDataStruct *m_var; // 指向真实数据（非拥有）
  QVariant m_cachedValue; // ✅ 新增：本地缓存，用于 UI 快速刷新
  bool m_cacheValid = false;

public:
  StructuredDataNode(OPCUAModernDataStruct *var) : m_var(var) {}

  std::string getName() const override { return m_var->variable_name; }
  std::string getPath() const override { return m_var->variable_full_path; }
  S7DataType getDataType() const override { return m_var->data_type_enum; }

  Result<QVariant, RichError> readValue() override {
    if (!m_cacheValid) {
      return Result<QVariant, RichError>(RichError{"error cache"});
    } else {
      return Result<QVariant, RichError>(m_cachedValue);
    }
  }

  Result<bool, RichError> writeValue(const QVariant &value) override {
    // 1. 先进行类型检查和范围验证
    switch (m_var->data_type_enum) {
    case S7DataType::BOOL: {
      if (!value.canConvert<bool>()) {
        return Result<bool, RichError>(RichError("Invalid type for BOOL"));
      }
      bool boolValue = value.toBool();
      m_cachedValue = boolValue;
      m_cacheValid = true;
      break;
    }

    case S7DataType::BYTE: {
      if (!value.canConvert<int>()) {
        return Result<bool, RichError>(RichError("Invalid type for BYTE"));
      }
      int intValue = value.toInt();
      if (intValue < 0 || intValue > 255) {
        return Result<bool, RichError>(
            RichError("BYTE value out of range (0-255)"));
      }
      m_cachedValue = static_cast<uint8_t>(intValue);
      m_cacheValid = true;
      break;
    }

    case S7DataType::INT: {
      if (!value.canConvert<int>()) {
        return Result<bool, RichError>(RichError("Invalid type for INT"));
      }
      int intValue = value.toInt();
      if (intValue < -32768 || intValue > 32767) {
        return Result<bool, RichError>(
            RichError("INT value out of range (-32768 to 32767)"));
      }
      m_cachedValue = static_cast<int16_t>(intValue);
      m_cacheValid = true;
      break;
    }

    case S7DataType::DINT: {
      if (!value.canConvert<qlonglong>()) {
        return Result<bool, RichError>(RichError("Invalid type for DINT"));
      }
      qlonglong longValue = value.toLongLong();
      if (longValue < -2147483648LL || longValue > 2147483647LL) {
        return Result<bool, RichError>(
            RichError("DINT value out of range (-2147483648 to 2147483647)"));
      }
      m_cachedValue = static_cast<int32_t>(longValue);
      m_cacheValid = true;
      break;
    }

    case S7DataType::WORD: {
      if (!value.canConvert<int>()) {
        return Result<bool, RichError>(RichError("Invalid type for WORD"));
      }
      int intValue = value.toInt();
      if (intValue < 0 || intValue > 65535) {
        return Result<bool, RichError>(
            RichError("WORD value out of range (0-65535)"));
      }
      m_cachedValue = static_cast<uint16_t>(intValue);
      m_cacheValid = true;
      break;
    }

    case S7DataType::DWORD:
    case S7DataType::UDINT: {
      if (!value.canConvert<quint32>()) {
        return Result<bool, RichError>(
            RichError("Invalid type for DWORD/UDINT"));
      }
      quint32 uintValue = value.toUInt();
      // DWORD 和 UDINT 都是 32 位无符号，范围 0 ~ 4294967295，无需额外校验
      m_cachedValue = static_cast<uint32_t>(uintValue);
      m_cacheValid = true;
      break;
    }

    case S7DataType::REAL: {
      if (!value.canConvert<float>()) {
        return Result<bool, RichError>(RichError("Invalid type for REAL"));
      }
      float floatValue = value.toFloat();
      // REAL（32位浮点）无需范围校验，任何 float 都合法
      m_cachedValue = floatValue;
      m_cacheValid = true;
      break;
    }

    case S7DataType::STRING: {
      if (!value.canConvert<QString>()) {
        return Result<bool, RichError>(RichError("Invalid type for STRING"));
      }
      QString strValue = value.toString();
      // 可选：增加字符串长度校验（根据 S7 定义的最大长度）
      if (strValue.length() > 254) { // 假设最大 254 字符
        return Result<bool, RichError>(
            RichError("STRING value exceeds maximum length"));
      }
      m_cachedValue = strValue;
      m_cacheValid = true;
      break;
    }

    default: {
      return Result<bool, RichError>(
          RichError("Unsupported S7 data type for writing"));
    }
    }

    // ✅ 写入成功，返回 true
    return Result<bool, RichError>(true);
  }

  std::string getProtocolType() const override { return "S7/OPCUA"; }
  std::string getNodeId() const override { return m_var->variable_nodeID; }
  int getAccessLevel() const override { return m_var->access_level; }
};