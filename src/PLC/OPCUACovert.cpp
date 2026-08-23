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
    // if(var.buildType==InputFormat::BROWSER)
    // {
    //   m_writeValue.nodeId = UA_NODEID_STRING_ALLOC(
    //       nameSpace, uaStringToString(var.nodeID.identifier.string).data());
    // }
    // else
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
    // if(var.buildType==InputFormat::BROWSER)
    // {
    //   m_writeValue.nodeId = UA_NODEID_STRING_ALLOC(
    //       nameSpace, uaStringToString(var.nodeID.identifier.string).data());
    // }
    // else
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
    const S7DataType &s7_type, ValueType &dataVar, int i,
    const std::vector<UA_Variant> &batchReadVariant) {
  if (s7_type == S7DataType::BOOL) {
    bool tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      dataVar = tmp;
    }
  } else if (s7_type == S7DataType::BYTE) {
    uint8_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      
      dataVar = tmp;
    }
  } else if (s7_type == S7DataType::INT) {
    int16_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      
      dataVar = tmp;
    }
  } else if (s7_type == S7DataType::WORD) {
    uint16_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      
      dataVar = tmp;
    }
  } else if (s7_type == S7DataType::DWORD || s7_type == S7DataType::UDINT) {
    uint32_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      
      dataVar = tmp;
    }
  } else if (s7_type == S7DataType::DINT) {
    int32_t tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      
      dataVar = tmp;
    }
  } else if (s7_type == S7DataType::REAL) {
    float tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      
      dataVar = tmp;
    }
  } else if (s7_type == S7DataType::STRING) {
    std::string tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      dataVar = (tmp);
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
  switch (var.data_type_enum) {
  case S7DataType::BOOL: {
    bool tmp{var.getValue<bool>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  }
  case S7DataType::BYTE: {
    uint8_t tmp{static_cast<uint8_t>(var.getValue<uint16_t>())};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  }
  case S7DataType::INT: {
    int16_t tmp{static_cast<int16_t>(var.getValue<int16_t>())};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  }
  case S7DataType::WORD: {
    uint16_t tmp{static_cast<uint16_t>(var.getValue<uint16_t>())};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  }
  case S7DataType::DWORD:
  case S7DataType::UDINT: {
    uint32_t tmp{var.getValue<uint32_t>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  }
  case S7DataType::DINT: {
    int32_t tmp{var.getValue<int32_t>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  }
  case S7DataType::REAL: {
    float tmp{var.getValue<float>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  }
  case S7DataType::STRING: {
    std::string tmp{var.getValue<std::string>()};
    return batchSet_UA_Scalar_StatusCode(nameSpace, var, tmp, index,
                                         batchWriteNodes);
  }
  default: {
    return Result<bool, RichError>(
        RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
  }
  }
}

Result<bool, RichError> OPCUADataCovert::batchSet_Uint8_t_To_Dynamic(
    std::vector<std::shared_ptr<IDataNode>> &vector,
    std::vector<uint8_t> &dataBuffer) {
  for (auto &var : vector) {
    if (var->getDataType() == S7DataType::UNKNOWN) {
      continue;
    }

    auto element = dynamic_cast<INodeManager*>(var.get());
    auto value = var->readValue();

    auto result = DataTypeMapper::TransformBytesToDataType(
        element->getDataType(), dataBuffer, element->getDataByte(), 0, element->getDataBit(),
        value);
    if (result.is_fail()) {
      return result;
    }
  }
  return Result<bool, RichError>(true);
}

Result<bool, RichError> OPCUADataCovert::batchSet_Dynamic_To_Uint8_t(std::vector<IDataNode> &vector,
      std::vector<uint8_t> &dataBuffer) {
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