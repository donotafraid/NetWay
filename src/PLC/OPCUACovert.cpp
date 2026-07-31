#include "PLC/OPCUACovert.h"
#include <spdlog/spdlog.h>
#include "PLC/TransformS7AndOPCUA.h"

// T Function --------- Covert_UA_Scalar_To_Specific
template <typename T>
void OPCUADataCovert::Covert_UA_Scalar_To_Specific(
    T &SourceData_var, int i, const std::vector<UA_Variant> &batchReadVariant) {
  // ELSE UA TPYE MATCH S7 DATA TYPE
  memcpy(&SourceData_var, ((static_cast<T *>(batchReadVariant[i].data))),
         sizeof(T));
}

// String Special Function --------- Covert_UA_Scalar_To_Specific
template <>
void OPCUADataCovert::Covert_UA_Scalar_To_Specific(
    std::string &SourceData_var, int i,
    const std::vector<UA_Variant> &batchReadVariant) {
  const UA_String *src =
      static_cast<const UA_String *>(batchReadVariant[i].data);
  if (src && src->length > 0) {
    SourceData_var.assign(reinterpret_cast<const char *>((src->data)),
                          src->length);
  } else {
    SourceData_var = "";
  }
}

template <typename T>
Result<bool, RichError> OPCUADataCovert::batchSet_UA_Scalar_StatusCode(
    int nameSpace, OPCUAModernDataStruct &var, T &source_var, int index,
    std::vector<UA_WriteValue> &batchWriteNodes) {
  //  type->value->request->response
  // CHECK TYPE MATCH
  auto it = s7_to_ua_map.find(var.data_type_enum);
  if (it == s7_to_ua_map.end()) {
    return Result<bool, RichError>(
        RichError("OPCUADataCovert : write type does not match T "));
  }
  const UA_DataType *type = it->second;

  //  INIT WRITE UA_VALUE
  auto &m_writeValue = batchWriteNodes[index];
  if (m_writeValue.attributeId != UA_ATTRIBUTEID_VALUE) {
    if(var.buildType==InputFormat::BROWSER)
    {
      m_writeValue.nodeId = UA_NODEID_STRING_ALLOC(
          nameSpace, uaStringToString(var.nodeID.identifier.string).data());
    }
    else
    {
      auto result{extractPureNodeIdRobust(var.variable_nodeID)};
      m_writeValue.nodeId =
          UA_NODEID_STRING_ALLOC(std::stoi(result.first), result.second.data());
    }
    m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;
  }

  //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
  UA_StatusCode status =
      UA_Variant_setScalarCopy(&m_writeValue.value.value, &source_var, type);
  if (status != UA_STATUSCODE_GOOD) {
    UA_WriteValue_clear(&m_writeValue);
    return Result<bool, RichError>(
        RichError("OPCUADataCovert : batchSet_UA_Scalar_StatusCode fail"));
  }
  m_writeValue.value.hasValue = true;
  return Result<bool, RichError>(true);
}

template <>
Result<bool, RichError> OPCUADataCovert::batchSet_UA_Scalar_StatusCode(
    int nameSpace, OPCUAModernDataStruct &var, std::string &source_var,
    int index, std::vector<UA_WriteValue> &batchWriteNodes) {
  //  type->value->request->response
  //  CHECK TYPE MATCH
  auto it = s7_to_ua_map.find(var.data_type_enum);
  if (it == s7_to_ua_map.end()) {
    return Result<bool, RichError>(
        RichError("OPCUADataCovert : write type does not match T "));
  }
  const UA_DataType *type = it->second;

  //  INIT WRITE UA_VALUE
  auto &m_writeValue = batchWriteNodes[index];
  if (m_writeValue.attributeId != UA_ATTRIBUTEID_VALUE) {
    if(var.buildType==InputFormat::BROWSER)
    {
      m_writeValue.nodeId = UA_NODEID_STRING_ALLOC(
          nameSpace, uaStringToString(var.nodeID.identifier.string).data());
    }
    else
    {
      auto result{extractPureNodeIdRobust(var.variable_nodeID)};
      m_writeValue.nodeId =
          UA_NODEID_STRING_ALLOC(std::stoi(result.first), result.second.data());
    }
    m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;
  }

  // 一些 OPC UA 库提供辅助宏
  UA_String uaString = UA_STRING(const_cast<char *>(source_var.c_str()));

  //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
  UA_StatusCode status =
      UA_Variant_setScalarCopy(&m_writeValue.value.value, &uaString, type);
  if (status != UA_STATUSCODE_GOOD) {
    UA_WriteValue_clear(&m_writeValue);
    return Result<bool, RichError>(
        RichError("OPCUADataCovert : batchSet_UA_Scalar_StatusCode fail"));
  }

  m_writeValue.value.hasValue = true;
  return Result<bool, RichError>(true);
}

Result<bool, RichError> OPCUADataCovert::Set_UA_To_Read_Normal_Scalar(
    const S7DataType &s7_type, Dynamic_Value &value, int i,
    const std::vector<UA_Variant> &batchReadVariant) {
  if (s7_type == S7DataType::BOOL) {
    bool tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      value.Reset_Value(std::move(tmp));
    }
  } else if (s7_type == S7DataType::BYTE) {
    uint8_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      value.Reset_Value(std::move(tmp));
    }
  } else if (s7_type == S7DataType::INT) {
    int16_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      value.Reset_Value(std::move(tmp));
    }
  } else if (s7_type == S7DataType::WORD) {
    uint16_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      value.Reset_Value(std::move(tmp));
    }
  } else if (s7_type == S7DataType::DWORD || s7_type == S7DataType::UDINT) {
    uint32_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      value.Reset_Value(std::move(tmp));
    }
  } else if (s7_type == S7DataType::DINT) {
    int32_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      value.Reset_Value(std::move(tmp));
    }
  } else if (s7_type == S7DataType::REAL) {
    float tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      value.Reset_Value(std::move(tmp));
    }
  } else if (s7_type == S7DataType::STRING) {
    std::string tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      value.Reset_Value(std::move(tmp));
    }
  } else {
    return Result<bool, RichError>(
        RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
  }
  return Result<bool, RichError>(true);
}

Result<bool, RichError> OPCUADataCovert::batchSet_Normal_To_Write_UA_Scalar(
    int nameSpace, OPCUAModernDataStruct &var, int index,
    std::vector<UA_WriteValue> &batchWriteNodes) {
  if (var.data_type_enum == S7DataType::BOOL) {
    bool tmp{var.data_pointer->get<bool>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  } else if (var.data_type_enum == S7DataType::BYTE) {
    uint8_t tmp{var.data_pointer->get<uint8_t>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  } else if (var.data_type_enum == S7DataType::INT) {
    int16_t tmp{var.data_pointer->get<int16_t>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  } else if (var.data_type_enum == S7DataType::WORD) {
    uint16_t tmp{var.data_pointer->get<uint16_t>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  } else if (var.data_type_enum == S7DataType::DWORD ||
             var.data_type_enum == S7DataType::UDINT) {
    uint32_t tmp{var.data_pointer->get<uint32_t>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  } else if (var.data_type_enum == S7DataType::DINT) {
    int32_t tmp{var.data_pointer->get<int32_t>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  } else if (var.data_type_enum == S7DataType::REAL) {
    float tmp{var.data_pointer->get<float>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  } else if (var.data_type_enum == S7DataType::STRING) {
    std::string tmp{var.data_pointer->get<std::string>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  } else {
    return Result<bool, RichError>(
        RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
  }
}

void OPCUADataCovert::updateBufferFromS7ModernStructByMSB(
    std::vector<OPCUAModernDataStruct> &vector,
    std::vector<uint8_t> &dataBUffer) {
  for (auto &VariableItem : vector) {
    switch (VariableItem.data_type_enum) {
    case S7DataType::BOOL: {
      bool boolValue = VariableItem.data_pointer->get<bool>();
      if (boolValue) {
        dataBUffer[VariableItem.bytes_offset] |= 1 << VariableItem.bit_offset;
      } else {
        dataBUffer[VariableItem.bytes_offset] &=
            ~(1 << VariableItem.bit_offset);
      }
      break;
    }

    case S7DataType::BYTE: {
      int intValue = VariableItem.data_pointer->get<uint8_t>();
      if (intValue >= 0 && intValue <= 255) {
        ByteOrderCoverter::to_bigEndian(
            intValue, &dataBUffer[VariableItem.bytes_offset], 1);
        break;
      } else {
        spdlog::warn("Byte value is mismatch range in model (BYTE), value: {}", intValue);
      }
      break;
    }

    case S7DataType::INT: {
      int intValue = VariableItem.data_pointer->get<int16_t>();
      if (intValue >= -32768 && intValue <= 32767) {
        ByteOrderCoverter::to_bigEndian(
            intValue, &dataBUffer[VariableItem.bytes_offset], 2);
        break;
      } else {
        spdlog::warn("Byte value is mismatch range in model (INT), value: {}", intValue);
      }
      break;
    }

    case S7DataType::DINT: {
      qint64 longValue = VariableItem.data_pointer->get<int32_t>();
      if (longValue >= -2147483648LL && longValue <= 2147483647LL) {
        ByteOrderCoverter::to_bigEndian(
            longValue, &dataBUffer[VariableItem.bytes_offset], 4);
        break;
      } else {
        spdlog::warn("Byte value is mismatch range in model (DINT), value: {}", longValue);
      }
      break;
    }

    case S7DataType::WORD: {
      int intValue = VariableItem.data_pointer->get<uint16_t>();
      if (intValue >= 0 && intValue <= 65535) {
        ByteOrderCoverter::to_bigEndian(
            intValue, &dataBUffer[VariableItem.bytes_offset], 2);
        break;
      } else {
        spdlog::warn("Byte value is mismatch range in model (WORD), value: {}", intValue);
      }
      break;
    }

    case S7DataType::DWORD:
    case S7DataType::UDINT: {
      uint32_t uintValue = VariableItem.data_pointer->get<uint32_t>();
      // 无符号类型始终在有效范围内，无需范围检查
      ByteOrderCoverter::to_bigEndian(
          uintValue, &dataBUffer[VariableItem.bytes_offset], 4);
      break;
    }

    case S7DataType::REAL: {
      float floatValue = VariableItem.data_pointer->get<float>();
      uint32_t tmp_data;
      memcpy(&tmp_data, &floatValue, 4);
      ByteOrderCoverter::to_bigEndian(
          tmp_data, &dataBUffer[VariableItem.bytes_offset], 4);
      break;
    }

    case S7DataType::STRING: {
      std::string string_value = VariableItem.data_pointer->get<std::string>();

      dataBUffer[VariableItem.bytes_offset] = VariableItem.s7_data_type_length;
      dataBUffer[VariableItem.bytes_offset + 1] = string_value.size();
      std::fill(dataBUffer.begin() + VariableItem.bytes_offset + 2,
                dataBUffer.begin() + VariableItem.bytes_offset + 2 +
                    VariableItem.s7_data_type_length - 2,
                0);

      memcpy(&dataBUffer[VariableItem.bytes_offset + 2], string_value.c_str(),
             string_value.size());
      break;
    }

    default: {
      // 未知类型，直接存储
      spdlog::debug("Unknown S7 data type encountered in updateBufferFromS7ModernStructByMSB");
      break;
    }
    }
  }
}

void OPCUADataCovert::updateBufferFromS7ModernStructByLSB(
    std::vector<OPCUAModernDataStruct> &vector,
    std::vector<uint8_t> &dataBUffer) {
  for (auto &VariableItem : vector) {
    switch (VariableItem.data_type_enum) {
    case S7DataType::BOOL: {
      bool boolValue = VariableItem.data_pointer->get<bool>();
      if (boolValue) {
        dataBUffer[VariableItem.bytes_offset] |= 1 << VariableItem.bit_offset;
      } else {
        dataBUffer[VariableItem.bytes_offset] &=
            ~(1 << VariableItem.bit_offset);
      }
      break;
    }

    case S7DataType::BYTE: {
      int intValue = VariableItem.data_pointer->get<uint8_t>();
      if (intValue >= 0 && intValue <= 255) {
        ByteOrderCoverter::to_littleEndian(
            intValue, &dataBUffer[VariableItem.bytes_offset], 1);
        break;
      } else {
        spdlog::warn("Byte value is mismatch range in model (BYTE), value: {}", intValue);
      }
      break;
    }

    case S7DataType::INT: {
      int intValue = VariableItem.data_pointer->get<int16_t>();
      if (intValue >= -32768 && intValue <= 32767) {
        ByteOrderCoverter::to_littleEndian(
            intValue, &dataBUffer[VariableItem.bytes_offset], 2);
        break;
      } else {
        spdlog::warn("Byte value is mismatch range in model (INT), value: {}", intValue);
      }
      break;
    }

    case S7DataType::DINT: {
      qint64 longValue = VariableItem.data_pointer->get<int32_t>();
      if (longValue >= -2147483648LL && longValue <= 2147483647LL) {
        ByteOrderCoverter::to_littleEndian(
            longValue, &dataBUffer[VariableItem.bytes_offset], 4);
        break;
      } else {
        spdlog::warn("Byte value is mismatch range in model (DINT), value: {}", longValue);
      }
      break;
    }

    case S7DataType::WORD: {
      int intValue = VariableItem.data_pointer->get<uint16_t>();
      if (intValue >= 0 && intValue <= 65535) {
        ByteOrderCoverter::to_littleEndian(
            intValue, &dataBUffer[VariableItem.bytes_offset], 2);
        break;
      } else {
        spdlog::warn("Byte value is mismatch range in model (WORD), value: {}", intValue);
      }
      break;
    }

    case S7DataType::DWORD:
    case S7DataType::UDINT: {
      uint32_t uintValue = VariableItem.data_pointer->get<uint32_t>();
      // 无符号类型始终在有效范围内，无需范围检查
      ByteOrderCoverter::to_littleEndian(
          uintValue, &dataBUffer[VariableItem.bytes_offset], 4);
      break;
    }

    case S7DataType::REAL: {
      float floatValue = VariableItem.data_pointer->get<float>();
      uint32_t tmp_data;
      memcpy(&tmp_data, &floatValue, 4);
      ByteOrderCoverter::to_littleEndian(
          tmp_data, &dataBUffer[VariableItem.bytes_offset], 4);
      break;
    }

    case S7DataType::STRING: {
      std::string string_value = VariableItem.data_pointer->get<std::string>();

      dataBUffer[VariableItem.bytes_offset] = VariableItem.s7_data_type_length;
      dataBUffer[VariableItem.bytes_offset + 1] = string_value.size();
      std::fill(dataBUffer.begin() + VariableItem.bytes_offset + 2,
                dataBUffer.begin() + VariableItem.bytes_offset + 2 +
                    VariableItem.s7_data_type_length - 2,
                0);

      memcpy(&dataBUffer[VariableItem.bytes_offset + 2], string_value.c_str(),
             string_value.size());
      break;
    }

    default: {
      // 未知类型，直接存储
      spdlog::debug("Unknown S7 data type encountered in updateBufferFromS7ModernStructByLSB");
      break;
    }
    }
  }
}


Result<bool, RichError> OPCUADataCovert::batchSet_Uint8_t_To_Dynamic(
   OPCUADataBlock *data) {
  auto &SourceData_var = data->getVariabeDataVector();
  auto &dataBuffer = data->getVariableDataBuffer();
  for (auto &var : SourceData_var) {
    if(var.data_type_enum== S7DataType::UNKNOWN)
    {
      continue;
    }

    auto result = DataTypeMapper::TransformBytesToDynamicValue(
        var.data_type_enum, dataBuffer, var.bytes_offset, 0, var.bit_offset,
        var.data_pointer.get());
    if (result.is_fail()) {
      return result;
    }
  }
  return Result<bool, RichError>(true);
}

Result<bool, RichError> OPCUADataCovert::batchSet_DynamicValue_To_Uint8_t(
   OPCUADataBlock *data) {
  auto &SourceData_var = data->getVariabeDataVector();
  auto &dataBuffer = data->getVariableDataBuffer();
  updateBufferFromS7ModernStructByMSB(SourceData_var, dataBuffer);
  return Result<bool, RichError>(true);
}

// T Function --------- ByteDeserialization_memcpy
template <typename T>
void OPCUADataCovert::ByteDeserialization_memcpy(
    T &dest, const std::vector<uint8_t> &src, int data_offset,
    int data_length) {
  // ELSE UA TPYE MATCH S7 DATA TYPE
  memcpy(&dest, src.data() + data_offset, data_length);
}