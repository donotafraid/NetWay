#include "PLC/OPCUADataBlock.h"
#include <spdlog/spdlog.h>

//OPCUADataBlock--------------------------------------------------
OPCUADataBlock::OPCUADataBlock(const std::string& name, const std::string& ip)
    : data_block_name(name)
    , ip_address(ip) {
}

// 基本数据访问实现
const std::string& OPCUADataBlock::getBlocktName() const {
    return data_block_name;
}

std::vector<OPCUAModernDataStruct>& OPCUADataBlock::getVariabeDataVector() {
    return m_variable->variables;
}

bool OPCUADataBlock::hasVariable(const std::string& path) const {
    return findVariableByPath(path) != nullptr;
}

const int OPCUADataBlock::getVariableVectorSize() const {
    return static_cast<int>(m_variable->variables.size());
}

std::vector<uint8_t> &OPCUADataBlock::getVariableDataBuffer() {
  if (dataBuffer.size() == 0) {
    dataBuffer.resize(10000);
  }
  return (dataBuffer);
}

// set part 实现
void OPCUADataBlock::setName(const std::string& name) {
    data_block_name = name;
}

void OPCUADataBlock::setIpAddres(const std::string& ip_Address) {
    ip_address = ip_Address;
}

void OPCUADataBlock::setParseResult(const std::shared_ptr<OPCUAParseResult> &parseResult)
{
  m_variable = parseResult;
}


Result<bool, RichError> OPCUADataBlock::setVariableMap(
    std::vector<OPCUAModernDataStruct> &variable_vector) {
  if (variable_vector.size()) {
    m_variable->variables = std::move(variable_vector);
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError{"VariableMap size = 0 "});
  }
}
    

// 获取标识符实现
std::string OPCUADataBlock::getIdentifier() {
    return ip_address;
}

// 批量操作实现
Result<bool, RichError> OPCUADataBlock::batchReadValues(
    const std::vector<std::string>& paths,
    std::vector<QVariant>& out_values) const {
    
    out_values.clear();
    out_values.reserve(paths.size());
    
    for (const auto& path : paths) {
        auto* var = findVariableByPath(path);
        if (!var) {
            return RichError("Variable not found: " + path);
        }
        
        if (var->data_pointer) {
            // out_values.push_back(var->data_pointer->toQVariant());
            out_values.push_back(QVariant());  // 临时返回
        } else {
            out_values.push_back(QVariant());
        }
    }
    
    return true;
}

Result<bool, RichError> OPCUADataBlock::batchWriteValues(
    const std::vector<std::string>& paths,
    const std::vector<QVariant>& values) {
    
    if (paths.size() != values.size()) {
        return RichError("Paths and values size mismatch");
    }
    
    for (size_t i = 0; i < paths.size(); ++i) {
        auto* var = findVariableByPath(paths[i]);
        if (!var) {
            return RichError("Variable not found: " + paths[i]);
        }
        
        // 写入值逻辑
        // if (var->data_pointer) {
        //     var->data_pointer->fromQVariant(values[i]);
        // }
    }
    
    return true;
}

void OPCUADataBlock::updateBufferFromS7ModernStructByMSB(std::vector<OPCUAModernDataStruct> &vector)
{
   for(auto &VariableItem : vector) 
   {
     switch (VariableItem.data_type_enum) {
     case S7DataType::BOOL: {
       bool boolValue = VariableItem.data_pointer->get<bool>();
       if(boolValue)
       {
         dataBuffer[VariableItem.bytes_offset] |=
             1 << VariableItem.bit_offset;
       }
       else
       {
         dataBuffer[VariableItem.bytes_offset] &=
             ~(1 << VariableItem.bit_offset);
       }
      
       break;
     }

     case S7DataType::BYTE: {
       int intValue = VariableItem.data_pointer->get<uint8_t>();
       if (intValue >= 0 && intValue <= 255) {
         ByteOrderCoverter::to_bigEndian(
             intValue, &dataBuffer[VariableItem.bytes_offset], 1);
         break;
       } else {
         spdlog::warn("Byte value is mismatch range in model (BYTE), value: {}", intValue);
       }
       break;
     }

     case S7DataType::INT: {
       int intValue = VariableItem.data_pointer->get<int16_t>();
       if ( intValue >= -32768 && intValue <= 32767) {
         ByteOrderCoverter::to_bigEndian(
             intValue, &dataBuffer[VariableItem.bytes_offset], 2);
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
             longValue, &dataBuffer[VariableItem.bytes_offset], 4);
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
             intValue, &dataBuffer[VariableItem.bytes_offset], 2);
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
           uintValue, &dataBuffer[VariableItem.bytes_offset], 4);
       break;
     }

     case S7DataType::REAL: {
       float floatValue = VariableItem.data_pointer->get<float>();
       uint32_t tmpBuffer;
       memcpy(&tmpBuffer, &floatValue, 4);
       ByteOrderCoverter::to_bigEndian(
           tmpBuffer, &dataBuffer[VariableItem.bytes_offset], 4);
       break;
     }

     case S7DataType::STRING: {
       std::string string_value = VariableItem.data_pointer->get<std::string>();
       
       dataBuffer[VariableItem.bytes_offset] =
           VariableItem.s7_data_type_length;
       dataBuffer[VariableItem.bytes_offset + 1] =
           string_value.size();
       std::fill(dataBuffer.begin() + VariableItem.bytes_offset + 2,
                 dataBuffer.begin() + VariableItem.bytes_offset + 2
                     + VariableItem.s7_data_type_length - 2,
                 0);

       memcpy(&dataBuffer[VariableItem.bytes_offset + 2],
              string_value.c_str(), string_value.size());
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

void OPCUADataBlock::updateBufferFromS7ModernStructByLSB(std::vector<OPCUAModernDataStruct> &vector)
{
   for(auto &VariableItem : vector) 
   {
     switch (VariableItem.data_type_enum) {
     case S7DataType::BOOL: {
       bool boolValue = VariableItem.data_pointer->get<bool>();
       if(boolValue)
       {
         dataBuffer[VariableItem.bytes_offset] |=
             1 << VariableItem.bit_offset;
       }
       else
       {
         dataBuffer[VariableItem.bytes_offset] &=
             ~(1 << VariableItem.bit_offset);
       }
      
       break;
     }

     case S7DataType::BYTE: {
       int intValue = VariableItem.data_pointer->get<uint8_t>();
       if (intValue >= 0 && intValue <= 255) {
         ByteOrderCoverter::to_littleEndian(
             intValue, &dataBuffer[VariableItem.bytes_offset], 1);
         break;
       } else {
         spdlog::warn("Byte value is mismatch range in model (BYTE), value: {}", intValue);
       }
       break;
     }

     case S7DataType::INT: {
       int intValue = VariableItem.data_pointer->get<int16_t>();
       if ( intValue >= -32768 && intValue <= 32767) {
         ByteOrderCoverter::to_littleEndian(
             intValue, &dataBuffer[VariableItem.bytes_offset], 2);
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
             longValue, &dataBuffer[VariableItem.bytes_offset], 4);
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
             intValue, &dataBuffer[VariableItem.bytes_offset], 2);
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
           uintValue, &dataBuffer[VariableItem.bytes_offset], 4);
       break;
     }

     case S7DataType::REAL: {
       float floatValue = VariableItem.data_pointer->get<float>();
       uint32_t tmpBuffer;
       memcpy(&tmpBuffer, &floatValue, 4);
       ByteOrderCoverter::to_littleEndian(
           tmpBuffer, &dataBuffer[VariableItem.bytes_offset], 4);
       break;
     }

     case S7DataType::STRING: {
       std::string string_value = VariableItem.data_pointer->get<std::string>();
       
       dataBuffer[VariableItem.bytes_offset] =
           VariableItem.s7_data_type_length;
       dataBuffer[VariableItem.bytes_offset + 1] =
           string_value.size();
       std::fill(dataBuffer.begin() + VariableItem.bytes_offset + 2,
                 dataBuffer.begin() + VariableItem.bytes_offset + 2
                     + VariableItem.s7_data_type_length - 2,
                 0);

       memcpy(&dataBuffer[VariableItem.bytes_offset + 2],
              string_value.c_str(), string_value.size());
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

// 辅助方法实现
const OPCUAModernDataStruct* OPCUADataBlock::findVariableByPath(const std::string& path) const {
    auto it = std::find_if(m_variable->variables.begin(), m_variable->variables.end(),
                          [&path](const OPCUAModernDataStruct& var) {
                              return var.variable_name == path || 
                                     var.browse_name == path ||
                                     var.variable_nodeID == path;
                          });
    
    if (it != m_variable->variables.end()) {
        return &(*it);
    }
    return nullptr;
}

bool OPCUADataBlock::validatePath(const std::string& path, std::string& error_msg) const {
    if (path.empty()) {
        error_msg = "Path is empty";
        return false;
    }
    
    // 可以添加更多的路径验证逻辑
    // 例如：检查路径格式、特殊字符等
    
    error_msg = "";
    return true;
}

Result<bool, RichError> OPCUADataBlock::isArray(OPCUAModernDataStruct &data_var) {
  if(data_var.is_array)
  {
    return Result<bool, RichError>(data_var.is_array);
  }
  else
  {
    return Result<bool, RichError>(RichError{"the var is not array element"});
  }
}