#include "PLC/SingleDataBlockRebuild.h"

//  EndianConverter----------------------------------------------------------------------------
static Result<bool, RichError>
S7BigEndianToLittleEndian(std::vector<uint8_t> &Sourcebuffer,
               std::vector<uint8_t> &Destbuffer, const S7ModernDataStruct &var) {
  switch (var.data_type_enum) {

  case S7DataType::BOOL: {
    if (Sourcebuffer[0] & (1 << var.bit_offset)) {
      Destbuffer[var.bytes_offset] |= 1 << var.bit_offset;
    } else {
      Destbuffer[var.bytes_offset] &= ~(1 << var.bit_offset);
    }
    break;
  }
  case S7DataType::BYTE:
    Destbuffer[var.bytes_offset] = Sourcebuffer[0];
    break;
  case S7DataType::INT:
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[0];
    Destbuffer[var.bytes_offset] = Sourcebuffer[1];
    break;
  case S7DataType::WORD:
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[0];
    Destbuffer[var.bytes_offset] = Sourcebuffer[1];
    break;
  case S7DataType::DWORD:
    Destbuffer[var.bytes_offset + 3] = Sourcebuffer[0];
    Destbuffer[var.bytes_offset + 2] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[2];
    Destbuffer[var.bytes_offset + 0] = Sourcebuffer[3];
    break;
  case S7DataType::UDINT:
    Destbuffer[var.bytes_offset + 3] = Sourcebuffer[0];
    Destbuffer[var.bytes_offset + 2] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[2];
    Destbuffer[var.bytes_offset + 0] = Sourcebuffer[3];
    break;
  case S7DataType::DINT:
    Destbuffer[var.bytes_offset + 3] = Sourcebuffer[0];
    Destbuffer[var.bytes_offset + 2] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[2];
    Destbuffer[var.bytes_offset + 0] = Sourcebuffer[3];
    break;
  case S7DataType::REAL:
    Destbuffer[var.bytes_offset + 3] = Sourcebuffer[0];
    Destbuffer[var.bytes_offset + 2] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[2];
    Destbuffer[var.bytes_offset + 0] = Sourcebuffer[3];
    break;
  case S7DataType::STRING: {
    int effective_string_length = std::min(Sourcebuffer[0], Sourcebuffer[1]);
    Destbuffer[var.bytes_offset] = Sourcebuffer[0];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[1];
    memcpy(&Destbuffer[var.bytes_offset + 2], &Sourcebuffer[2],
           effective_string_length);
    break;
  }
  default:
    return Result<bool, RichError>(
        RichError("variable not found by variablePath"));
  }
  return Result<bool, RichError>(true);
}

static Result<bool, RichError>
LittleEndianToS7BigEndian(std::vector<uint8_t> &Sourcebuffer,
               std::vector<uint8_t> &&Destbuffer, const S7ModernDataStruct &var) {
  switch (var.data_type_enum) {

  case S7DataType::BOOL: {
    if (Sourcebuffer[0] & (1 << var.bit_offset)) {
      Destbuffer[var.bytes_offset] |= 1 << var.bit_offset;
    } else {
      Destbuffer[var.bytes_offset] &= ~(1 << var.bit_offset);
    }
    break;
  }
  case S7DataType::BYTE:
    Destbuffer[var.bytes_offset] = Sourcebuffer[0];
    break;
  case S7DataType::INT:
    Destbuffer[var.bytes_offset] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[0];
    break;
  case S7DataType::WORD:
    Destbuffer[var.bytes_offset] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[0];
    break;
  case S7DataType::DWORD:
    Destbuffer[var.bytes_offset + 0] = Sourcebuffer[3];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[2];
    Destbuffer[var.bytes_offset + 2] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 3] = Sourcebuffer[0];
    break;
  case S7DataType::UDINT:
    Destbuffer[var.bytes_offset + 0] = Sourcebuffer[3];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[2];
    Destbuffer[var.bytes_offset + 2] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 3] = Sourcebuffer[0];
    break;
  case S7DataType::DINT:
    Destbuffer[var.bytes_offset + 0] = Sourcebuffer[3];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[2];
    Destbuffer[var.bytes_offset + 2] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 3] = Sourcebuffer[0];
    break;
  case S7DataType::REAL:
    Destbuffer[var.bytes_offset + 0] = Sourcebuffer[3];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[2];
    Destbuffer[var.bytes_offset + 2] = Sourcebuffer[1];
    Destbuffer[var.bytes_offset + 3] = Sourcebuffer[0];
    break;
  case S7DataType::STRING: {
    int effective_string_length = std::min(Sourcebuffer[0], Sourcebuffer[1]);
    Destbuffer[var.bytes_offset] = Sourcebuffer[0];
    Destbuffer[var.bytes_offset + 1] = Sourcebuffer[1];
    memcpy(&Destbuffer[var.bytes_offset + 2], &Sourcebuffer[2],
           effective_string_length);
    break;
  }
  default:
    return Result<bool, RichError>(
        RichError("variable not found by variablePath"));
  }
  return Result<bool, RichError>(true);
}

//DataBLock--------------------------------------------------------------------
Result<QVariant, RichError> DataBlock::readValue(QModelIndex index) const {
    // 1. 边界检查
    if (index.row() < 0 || index.row() >= static_cast<int>(m_variable_vector.size())) {
        return Result<QVariant, RichError>(
            RichError{QString("Index out of range: row=%1, size=%2")
                      .arg(index.row())
                      .arg(m_variable_vector.size())
                      .toStdString()}
        );
    }
    
    // 2. 获取对应行的数据（不需要遍历整个vector）
    const auto& item = m_variable_vector[index.row()];
    
    // 3. 根据列索引返回不同的数据
    switch (index.column()) {
    case 0:  // 变量名
        return Result<QVariant, RichError>(
            QVariant(QString::fromStdString(item.variable_name))
        );
        
    case 1:  // 数据类型枚举
        return Result<QVariant, RichError>(
            QVariant(static_cast<int>(item.data_type_enum))
        );
        
    case 2:  // 偏移量（字节或位）
        if (item.bytes_offset == -1) {
            return Result<QVariant, RichError>(QVariant(item.bit_offset));
        } else {
            return Result<QVariant, RichError>(QVariant(item.bytes_offset));
        }
        
    case 3: {  // 实际数据值
        // 检查 data_pointer 是否有效
        if (!item.data_pointer) {
            return Result<QVariant, RichError>(
                RichError{"data_pointer is null for variable: " + item.variable_name}
            );
        }
        
        // 根据不同类型返回对应的 QVariant
        try {
            switch (item.data_type_enum) {
            case S7DataType::BOOL:
                return Result<QVariant, RichError>(
                    QVariant(item.data_pointer->get<bool>())
                );
                
            case S7DataType::BYTE:
                return Result<QVariant, RichError>(
                    QVariant(item.data_pointer->get<uint8_t>())
                );
                
            case S7DataType::INT:
                return Result<QVariant, RichError>(
                    QVariant(item.data_pointer->get<int16_t>())
                );
                
            case S7DataType::DINT:
                return Result<QVariant, RichError>(
                    QVariant(item.data_pointer->get<int32_t>())
                );
                
            case S7DataType::REAL:
                return Result<QVariant, RichError>(
                    QVariant(item.data_pointer->get<float>())
                );
                
            case S7DataType::WORD:
                return Result<QVariant, RichError>(
                    QVariant(item.data_pointer->get<uint16_t>())
                );
                
            case S7DataType::DWORD:
                return Result<QVariant, RichError>(
                    QVariant(QString::fromStdString(
                        DataTypeMapper::transform_uint32_to_hex_string(
                            item.data_pointer->get<uint32_t>()
                        )
                    ))
                );
                
            case S7DataType::UDINT:
                return Result<QVariant, RichError>(
                    QVariant(QString::fromStdString(
                        DataTypeMapper::transform_uint32_to_string(
                            item.data_pointer->get<uint32_t>()
                        )
                    ))
                );
                
            case S7DataType::STRING:
                return Result<QVariant, RichError>(
                    QVariant(QString::fromStdString(
                        item.data_pointer->get<std::string>()
                    ))
                );
                
            default:
                return Result<QVariant, RichError>(
                    RichError{"Unsupported data type: " + 
                              std::to_string(static_cast<int>(item.data_type_enum))}
                );
            }
        } catch (const std::exception& e) {
            return Result<QVariant, RichError>(
                RichError{"Failed to read data: " + std::string(e.what())}
            );
        }
    }
    
    case 4:  // 注释
        return Result<QVariant, RichError>(
            QVariant(QString::fromStdString(item.comment))
        );
        
    default:
        return Result<QVariant, RichError>(
            RichError{QString("Column index out of range: column=%1")
                      .arg(index.column())
                      .toStdString()}
        );
    }
}

Result<bool, RichError> DataBlock::getVariableMap(
    std::vector<S7ModernDataStruct> &variable_vector) {
  if (variable_vector.size()) {
    m_variable_vector = std::move(variable_vector);
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError{"VariableMap size = 0 "});
  }
}

void DataBlock::updateS7ModernStructFromBuffer()
{
    for(auto& item: m_variable_vector)
    {
        auto result = DataTypeMapper::Data_transform_from_bytes(item.data_type_enum,m_variableDataBuffer,item.bytes_offset,item.s7_data_type_length,item.bit_offset);
        if(result.is_success())
        {
            if(item.data_type_enum == S7DataType::BOOL)
            {
              item.data_pointer->Reset_Value((std::get<bool>(result.unwrap_returnLeftValue())));
            }
            else if(item.data_type_enum == S7DataType::BYTE)
            {
                item.data_pointer->Reset_Value(std::get<uint8_t>(result.unwrap_returnLeftValue()));
            }
            else if(item.data_type_enum == S7DataType::INT)
            {
              item.data_pointer->Reset_Value(
                  std::get<int16_t>(result.unwrap_returnLeftValue()));
            }
            else if(item.data_type_enum == S7DataType::DINT)
            {
                item.data_pointer->Reset_Value(
                    std::get<int32_t>(result.unwrap_returnLeftValue()));
            }
            else if(item.data_type_enum == S7DataType::REAL)
            {
              item.data_pointer->Reset_Value(
                  std::get<float>(result.unwrap_returnLeftValue()));
            }
            else if(item.data_type_enum == S7DataType::WORD)
            {
              item.data_pointer->Reset_Value(
                  std::get<uint16_t>(result.unwrap_returnLeftValue()));
            }
            else if(item.data_type_enum == S7DataType::UDINT)
            {
              item.data_pointer->Reset_Value(QString::fromStdString(
                  DataTypeMapper::transform_uint32_to_string(
                      std::get<uint32_t>(result.unwrap_returnLeftValue()))));
            }
            else if(item.data_type_enum == S7DataType::DWORD)
            {
              item.data_pointer->Reset_Value(QString::fromStdString(
                  DataTypeMapper::transform_uint32_to_hex_string(
                      std::get<uint32_t>(result.unwrap_returnLeftValue()))));
            }
            else if(item.data_type_enum == S7DataType::STRING)
            {
              item.data_pointer->Reset_Value(
                  std::get<std::string>(result.unwrap_returnLeftValue())
                      .c_str());
            }
        }
    }
}

void DataBlock::updateBufferFromS7ModernStructByMSB(std::vector<S7ModernDataStruct> &vector)
{
   for(auto &VariableItem : vector) 
   {
     switch (VariableItem.data_type_enum) {
     case S7DataType::BOOL: {
       bool boolValue = VariableItem.data_pointer->get<bool>();
       if(boolValue)
       {
         m_variableDataBuffer[VariableItem.bytes_offset] |=
             1 << VariableItem.bit_offset;
       }
       else
       {
         m_variableDataBuffer[VariableItem.bytes_offset] &=
             ~(1 << VariableItem.bit_offset);
       }
      
       break;
     }

     case S7DataType::BYTE: {
       int intValue = VariableItem.data_pointer->get<uint8_t>();
       if (intValue >= 0 && intValue <= 255) {
         ByteOrderCoverter::to_bigEndian(
             intValue, &m_variableDataBuffer[VariableItem.bytes_offset], 1);
         break;
       } else {
            std::cout<<"Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::INT: {
       int intValue = VariableItem.data_pointer->get<int16_t>();
       if ( intValue >= -32768 && intValue <= 32767) {
         ByteOrderCoverter::to_bigEndian(
             intValue, &m_variableDataBuffer[VariableItem.bytes_offset], 2);
         break;
       } else {
            std::cout<<"Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::DINT: {
       qint64 longValue = VariableItem.data_pointer->get<int32_t>();
       if (longValue >= -2147483648LL && longValue <= 2147483647LL) {
         ByteOrderCoverter::to_bigEndian(
             longValue, &m_variableDataBuffer[VariableItem.bytes_offset], 4);
         break;
       } else {
            std::cout<<"Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::WORD: {
       int intValue = VariableItem.data_pointer->get<uint16_t>();
       if (intValue >= 0 && intValue <= 65535) {
         ByteOrderCoverter::to_bigEndian(
             intValue, &m_variableDataBuffer[VariableItem.bytes_offset], 2);
         break;
       } else {
         std::cout << "Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::DWORD:
     case S7DataType::UDINT: {
       uint32_t uintValue = VariableItem.data_pointer->get<uint32_t>();
       if (1) {
         ByteOrderCoverter::to_bigEndian(
             uintValue, &m_variableDataBuffer[VariableItem.bytes_offset], 4);
         break;
       } else {
            std::cout<<"Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::REAL: {
       float floatValue = VariableItem.data_pointer->get<float>();
       uint32_t tmp_data;
       memcpy(&tmp_data, &floatValue, 4);
       ByteOrderCoverter::to_bigEndian(
           tmp_data, &m_variableDataBuffer[VariableItem.bytes_offset], 4);
       break;
     }

     case S7DataType::STRING: {
       std::string string_value = VariableItem.data_pointer->get<std::string>();
       
       m_variableDataBuffer[VariableItem.bytes_offset] =
           VariableItem.s7_data_type_length;
       m_variableDataBuffer[VariableItem.bytes_offset + 1] =
           string_value.size();
       std::fill(m_variableDataBuffer.begin() + VariableItem.bytes_offset + 2,
                 m_variableDataBuffer.begin() + VariableItem.bytes_offset + 2
                     + VariableItem.s7_data_type_length - 2,
                 0);

       memcpy(&m_variableDataBuffer[VariableItem.bytes_offset + 2],
              string_value.c_str(), string_value.size());
       break;
     }

     default: {
       // 未知类型，直接存储
       break;
     }
     }

    
   }
}

void DataBlock::updateBufferFromS7ModernStructByLSB(std::vector<S7ModernDataStruct> &vector)
{
   for(auto &VariableItem : vector) 
   {
     switch (VariableItem.data_type_enum) {
     case S7DataType::BOOL: {
       bool boolValue = VariableItem.data_pointer->get<bool>();
       if(boolValue)
       {
         m_variableDataBuffer[VariableItem.bytes_offset] |=
             1 << VariableItem.bit_offset;
       }
       else
       {
         m_variableDataBuffer[VariableItem.bytes_offset] &=
             ~(1 << VariableItem.bit_offset);
       }
      
       break;
     }

     case S7DataType::BYTE: {
       int intValue = VariableItem.data_pointer->get<uint8_t>();
       if (intValue >= 0 && intValue <= 255) {
         ByteOrderCoverter::to_littleEndian(
             intValue, &m_variableDataBuffer[VariableItem.bytes_offset], 1);
         break;
       } else {
            std::cout<<"Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::INT: {
       int intValue = VariableItem.data_pointer->get<int16_t>();
       if ( intValue >= -32768 && intValue <= 32767) {
         ByteOrderCoverter::to_littleEndian(
             intValue, &m_variableDataBuffer[VariableItem.bytes_offset], 2);
         break;
       } else {
            std::cout<<"Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::DINT: {
       qint64 longValue = VariableItem.data_pointer->get<int32_t>();
       if (longValue >= -2147483648LL && longValue <= 2147483647LL) {
         ByteOrderCoverter::to_littleEndian(
             longValue, &m_variableDataBuffer[VariableItem.bytes_offset], 4);
         break;
       } else {
            std::cout<<"Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::WORD: {
       int intValue = VariableItem.data_pointer->get<uint16_t>();
       if (intValue >= 0 && intValue <= 65535) {
         ByteOrderCoverter::to_littleEndian(
             intValue, &m_variableDataBuffer[VariableItem.bytes_offset], 2);
         break;
       } else {
         std::cout << "Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::DWORD:
     case S7DataType::UDINT: {
       uint32_t uintValue = VariableItem.data_pointer->get<uint32_t>();
       if (1) {
         ByteOrderCoverter::to_littleEndian(
             uintValue, &m_variableDataBuffer[VariableItem.bytes_offset], 4);
         break;
       } else {
            std::cout<<"Byte value is mismatch range in model !\n ";
       }
     }

     case S7DataType::REAL: {
       float floatValue = VariableItem.data_pointer->get<float>();
       uint32_t tmp_data;
       memcpy(&tmp_data, &floatValue, 4);
       ByteOrderCoverter::to_littleEndian(
           tmp_data, &m_variableDataBuffer[VariableItem.bytes_offset], 4);
       break;
     }

     case S7DataType::STRING: {
       std::string string_value = VariableItem.data_pointer->get<std::string>();
       
       m_variableDataBuffer[VariableItem.bytes_offset] =
           VariableItem.s7_data_type_length;
       m_variableDataBuffer[VariableItem.bytes_offset + 1] =
           string_value.size();
       std::fill(m_variableDataBuffer.begin() + VariableItem.bytes_offset + 2,
                 m_variableDataBuffer.begin() + VariableItem.bytes_offset + 2
                     + VariableItem.s7_data_type_length - 2,
                 0);

       memcpy(&m_variableDataBuffer[VariableItem.bytes_offset + 2],
              string_value.c_str(), string_value.size());
       break;
     }

     default: {
       // 未知类型，直接存储
       break;
     }
     }
    
   }
}

Result<bool, RichError> DataBlock::isArray(S7ModernDataStruct &data_var) {
  size_t pos = data_var.variable_full_path.find_last_of('[');
  // IF FIND THE SYMBOL  '['
  if (pos != std::string::npos) {
    // CHECK WHETHER EXIST ']'
    if (data_var.variable_full_path.find(']', pos) != std::string::npos) {
      return Result<bool, RichError>(true);
    }
  }
  // IF DO NOT FIND THE SYMBOL '['
  return Result<bool, RichError>(RichError("the element is Scalar"));
}

Result<int, RichError>DataBlock::getSpecialStringLength(const std::string &dataName)
{
  for(auto &item : m_variable_vector) 
  {
    if(item.variable_full_path == dataName)
    {
      return Result<int,RichError> (item.s7_data_type_length);
    }
  }
  return Result<int, RichError>(RichError{"getSpecialStringLength : do not find the item in variable_vector\n"});
}

//  DeviceReader--------------------------------------------------------------------------
Result<bool,RichError> DeviceReader::ReadDataFromPLC(DataBlock *data)
{
  if(m_connectStatus.S7Switch)
  {
    return ReadS7DataBlock_FromPLC(data);
  }
  else if(m_connectStatus.OPCUASwitch)
  {
    // return ReadOPCUADataBlock_FromPLC(data);
    return batchReadOPCUADataBlock_FromPLC(data);
  }
  return Result<bool,RichError> (RichError{"reader do not have correct connect status"});
}

Result<bool, RichError>
DeviceReader::ReadS7DataBlock_FromPLC(DataBlock *data) {
  bool success = true;
  std::vector<uint8_t> &ref_buffer = data->getVariableDataBuffer();
  for (auto &var : data->getVariabeDataVector()) {
    Sourcebuffer.clear();
    Sourcebuffer.resize(var.s7_data_type_length);
    {
      auto result =
      this->m_s7Acess->read(1,  var.bytes_offset, var.s7_data_type_length, Sourcebuffer.data());
      if (result.is_fail()) {
        std::cout << "Read action is fail"<<std::endl;
        return Result<bool,RichError> (RichError{result.unwrap_err()});
        success = false;
      } else {
        success = true;
        auto result = S7BigEndianToLittleEndian(Sourcebuffer,ref_buffer,var);
        success = success && result.is_success();
      }
    }
  }
  if (success) {
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError("read variable failed"));
  }
}

Result<bool, RichError>
DeviceReader::ReadOPCUADataBlock_FromPLC(DataBlock *data) {
  bool success = true;
  return Result<bool, RichError>(true);

  // int index = 0;
  // for (auto &var : data->getVariabeDataVector()) {
  //   std::string array_member_full_path =
  //       var.variable_nodeID + "[" + std::to_string(0) + "]";
  //   if (data->isArray(var).is_success() &&
  //       var.variable_full_path != array_member_full_path) {
  //     //  meet the condition , mean the element is passed element of array , need skip it
  //     continue;
  //   }

  //   // m_opcUA->Set_Read_NodeID(var);
    
  //   //  read data from PLC
  //   auto read_result = (m_opcUA->Read_UA_Variant_From_PLC());
  //   if (read_result.is_fail()) {
  //     m_opcUA->Clear_Read_Respondse();
  //     return Result<bool, RichError>(read_result);
  //   }

  //   //  check the element if array member or normal scalar
  //   if (data->isArray(var).is_success()) {
  //     Result<bool, RichError> result = (m_opcUA->Set_Read_UA_Array(
  //         data->getVariabeDataVector(), var, index));
  //     if (result.is_fail()) {
  //       m_opcUA->Clear_Read_Respondse();
  //       return result;
  //     }

  //   } else {
  //     //  deal normal scalar condition
  //     Result<bool, RichError> result = (m_opcUA->Set_UA_To_Read_Normal_Scalar(
  //         var.data_type_enum, *var.data_pointer, 0));
  //     if (result.is_fail()) {
  //       m_opcUA->Clear_Read_Respondse();
  //       return result;
  //     }
  //   }

  //   //  clear resource
  //   ++index;
  //   m_opcUA->Clear_Read_Respondse();
  // }

  // if (success) {
  //   return Result<bool, RichError>(true);
  // } else {
  //   return Result<bool, RichError>(RichError("read variable failed"));
  // }
}

Result<bool, RichError>
DeviceReader::batchReadOPCUADataBlock_FromPLC(DataBlock *data) {
  bool success = true;
  //  CLEAR ELEMEMT EXISTED BEFORE
  // m_opcUA->PrepareBatchRead(data->getVariabeDataVector());
  return Result<bool,RichError> (true);

  // //  read data from PLC
  // auto read_result = (m_opcUA->Read_UA_Variant_From_PLC());
  // if (read_result.is_fail()) {
  //   m_opcUA->Clear_Read_Respondse();
  //   return Result<bool, RichError>(read_result);
  // }

  // //  store data into data_pointer
  // int index = 0;
  // for (auto &var : data->getVariabeDataVector()) {
  //   std::string array_member_full_path =
  //       var.variable_nodeID + "[" + std::to_string(0) + "]";
  //   if (data->isArray(var).is_success() &&
  //       var.variable_full_path != array_member_full_path) {
  //     //  meet the condition , mean the element is passed element of array , need skip it
  //     ++index;
  //     continue;
  //   }

  //   //  check the element if array member or normal scalar
  //   if (data->isArray(var).is_success()) {
  //     Result<bool, RichError> result = (m_opcUA->Set_Read_UA_Array(
  //         data->getVariabeDataVector(), var, index));
  //     if (result.is_fail()) {
  //       m_opcUA->Clear_Read_Respondse();
  //       return result;
  //     }

  //   } else {
  //     //  deal normal scalar condition
  //     Result<bool, RichError> result = (m_opcUA->Set_UA_To_Read_Normal_Scalar(
  //         var.data_type_enum, *var.data_pointer, index));
  //     if (result.is_fail()) {
  //       m_opcUA->Clear_Read_Respondse();
  //       return result;
  //     }
  //   }

  //   //  clear resource
  //   ++index;
  // }

  // //  clear batch reader variant
  // m_opcUA->Clear_Read_Respondse();

  // if (success) {
  //   return Result<bool, RichError>(true);
  // } else {
  //   return Result<bool, RichError>(RichError("read variable failed"));
  // }
}

Result<bool,RichError> DeviceReader::WriteDataToPLC(DataBlock *data)
{
  if(m_connectStatus.S7Switch)
  {
    return WriteS7DataBlock_ToPLC(data);
  }
  else if(m_connectStatus.OPCUASwitch)
  {
    return WriteSOPCUABlock_ToPLC(data);
  }
  return Result<bool,RichError> (RichError{"write do not have correct connect status"});
}

Result<bool, RichError> DeviceReader::WriteS7DataBlock_ToPLC(DataBlock *data) {
  if (m_s7Acess != nullptr) {
    bool success = true;
    std::vector<uint8_t> tmp_data;
    tmp_data.reserve(data->getDataBlockLength());
    for (auto &var : data->getVariabeDataVector()) {
      {
        tmp_data.clear();
        tmp_data.resize(var.s7_data_type_length);
        {
          std::move(data->getVariableDataBuffer().begin() + var.bytes_offset,
                    data->getVariableDataBuffer().begin() + var.bytes_offset +
                        var.s7_data_type_length,
                    tmp_data.begin());
        }

        //  single write condition result
        auto result = m_s7Acess->write(
            1, var.bytes_offset, var.s7_data_type_length, tmp_data.data());
        if (result.is_fail()) {
          return Result<bool, RichError>(result);
        } else {
          success = true;
          std::cout << "Send successful\n";
        }
      }
    }

    //  check total write condition result  
    if (success) {
      return Result<bool, RichError>(true);
    } else {
      return Result<bool, RichError>(RichError("read variable failed"));
    }
  }

  //  reader is nullptr
  return Result<bool, RichError>(RichError("S7Acess is nullptr"));
}

Result<bool, RichError> DeviceReader::WriteSOPCUABlock_ToPLC(DataBlock *data) {
  auto result = m_opcUA->ensureConnection();
  if(result.is_fail())
  {
    return Result<bool,RichError> (result);
  }
  else
  {
    return Result<bool,RichError> (true);
  }

  // for (auto &var : data->getVariabeDataVector()) {
  //   //  record the imaginary first element of array
  //   std::string array_member_full_path =
  //       var.variable_nodeID + "[" + std::to_string(0) + "]";
  //   //  check the element include [  and the element is not the first element in array
  //   if (var.variable_full_path.find('[') != std::string::npos &&
  //       var.variable_full_path != array_member_full_path) {
  //     //  meet the condition , mean the element is passed element of array
  //     continue;
  //   }

  //   //  check the element if array member or nromal scalar
  //   if (var.variable_full_path.find('[') != std::string::npos ) {
  //     //  MEAN THE ELEMENT IS ARRAY ELEMENT
  //     auto result = m_opcUA->Set_Normal_To_Write_UA_Vector(
  //         var,data->getVariableDataBuffer());
  //     if (result.is_fail()) {
  //       return Result<bool, RichError>(result);
  //     }
  //   } else {
  //     //  MEAN THE ELEMENT IS SCALAR ELEMENT
  //     auto result = m_opcUA->Set_Normal_To_Write_UA_Scalar(
  //         var,data->getVariableDataBuffer());
  //     if (result.is_fail()) {
  //       return Result<bool, RichError>(result);
  //     }
  //   }
  // }
  // return Result<bool, RichError>(true);
}

Result<bool, RichError> DeviceReader::batchWriteSOPCUABlock_ToPLC(DataBlock *data) {
  auto result = m_opcUA->ensureConnection();
  if(result.is_fail())
  {
    return Result<bool,RichError> (result);
  }
  else
  {
    return Result<bool,RichError> (true);
  }
  // m_opcUA->PrepareBatchWrite(data->getVariabeDataVector());

  // int index = 0;
  // for (auto &var : data->getVariabeDataVector()) {
  //   //  record the imaginary first element of array
  //   std::string array_member_full_path =
  //       var.variable_nodeID + "[" + std::to_string(0) + "]";
  //   //  check the element include [  and the element is not the first element in array
  //   if (var.variable_full_path.find('[') != std::string::npos &&
  //       var.variable_full_path != array_member_full_path) {
  //     //  meet the condition , mean the element is passed element of array
  //     ++index;
  //     continue;
  //   }

  //   //  check the element if array member or nromal scalar
  //   if (var.variable_full_path.find('[') != std::string::npos ) {
  //     //  MEAN THE ELEMENT IS ARRAY ELEMENT
  //     auto result = m_opcUA->batchSet_Normal_To_Write_UA_Vector(
  //         var,data->getVariableDataBuffer(),index);
  //     if (result.is_fail()) {
  //       return Result<bool, RichError>(result);
  //     }
  //   } else {
  //     //  MEAN THE ELEMENT IS SCALAR ELEMENT
  //     auto result = m_opcUA->batchSet_Normal_To_Write_UA_Scalar(
  //         var,data->getVariableDataBuffer(),index);
  //     if (result.is_fail()) {
  //       return Result<bool, RichError>(result);
  //     }
  //   }

  //   ++index;
  //   //clear write resource
  // }

  // auto writeResult = m_opcUA->batchWrite();
  // if(writeResult.is_fail())
  // {
  //   m_opcUA->Clear_Write_Respondse();
  //   return Result<bool, RichError>(writeResult);
  // }

  // m_opcUA->Clear_Write_Respondse();
  // return Result<bool, RichError>(true);
}


//  DataBlockModel-----------------------------------------------------------------------------
bool DataBlockModel::isValidIndex(const QModelIndex &index) const {
  return index.isValid() && index.row() >= 0 && index.row() < rowCount() &&
         index.column() >= 0 && index.column() < columnCount();
}

int DataBlockModel::columnCount(const QModelIndex &parent) const {
  return 5; // 名称、类型、偏移量、值,注解
}

QVariant DataBlockModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        switch (section) {
        case 0:
          return tr("VaribaleName");
        case 1:
          return tr("DataType");
        case 2:
          return tr("DataOffset");
        case 3:
          return tr("Value");
        case 4:
          return tr("Comment");
        }
    } else if (orientation == Qt::Vertical) {
        return section + 1;
    }

    return QVariant();
}

Qt::ItemFlags DataBlockModel::flags(const QModelIndex &index) const {
  if (!index.isValid())
    return Qt::NoItemFlags;

  return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
  // auto flags = QAbstractTableModel::flags(index);

  // // 打印调试信息
  // std::cout << "flags for (" << index.row() << "," << index.column()
  //           << ") = " << std::hex << flags << std::dec;
  // std::cout << " | Editable: " << ((flags & Qt::ItemIsEditable) ? "YES" : "NO")
  //           << std::endl;

  // return flags; // 必须包含 Qt::ItemIsEditable
}

QVariant DataBlockModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();

  if (index.row() >= m_dataBlock->getVariableVectorSize()) {
    return QVariant();
  }

  // 获取数据（注意：这里需要非const引用，因为可能需要在DisplayRole中读取）
  std::vector<S7ModernDataStruct> &items = m_dataBlock->getVariabeDataVector();
  S7ModernDataStruct &item = items[index.row()];

  // EditRole - 返回原始数据用于编辑
  if (role == Qt::EditRole) {
    switch (index.column()) {
    case 0: // Name 列
      return QString::fromStdString(item.variable_name);

    case 1: // Data Type 列（只读）
      return static_cast<int>(item.data_type_enum);

    case 2: // Offset 列（只读）
      return QVariant();

    case 3: // Value 列 - 根据数据类型返回原始值
      switch (item.data_type_enum) {
      case S7DataType::BOOL:
        return item.data_pointer->get<bool>();
      case S7DataType::BYTE:
        return item.data_pointer->get<uint8_t>();
      case S7DataType::INT:
        return item.data_pointer->get<int16_t>();
      case S7DataType::DINT:
        return item.data_pointer->get<int32_t>();
      case S7DataType::WORD:
        return item.data_pointer->get<uint16_t>();
      case S7DataType::DWORD:
        return item.data_pointer->get<uint32_t>();
      case S7DataType::UDINT:
        return item.data_pointer->get<uint32_t>();
      case S7DataType::REAL:
        return item.data_pointer->get<float>();
      case S7DataType::STRING:
        return QString::fromStdString(item.data_pointer->get<std::string>());
      default:
        return item.data_pointer->get<QVariant>();
      }

    case 4: // Comment 列
      return QString::fromStdString(item.comment);

    default:
      return QVariant();
    }
  }

  // DisplayRole - 返回格式化的显示数据
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case 0: // Name 列
      return QString::fromStdString(item.variable_name);

    case 1: // Data Type 列
    {
      auto it = S7DataTypeToString.find(item.data_type_enum);
      if (it != S7DataTypeToString.end()) {
        return QString::fromStdString(it->second);
      }
      return QString::fromStdString("UNKNOWN");
    }

    case 2: // Offset 列
      return item.bytes_offset;

    case 3: // Value 列 - 格式化显示
      switch (item.data_type_enum) {
      case S7DataType::BOOL:
        return item.data_pointer->get<bool>() ? "true" : "false";

      case S7DataType::BYTE:
        return QString::number(item.data_pointer->get<uint8_t>());

      case S7DataType::INT:
        return QString::number(item.data_pointer->get<int16_t>());

      case S7DataType::DINT:
        return QString::number(item.data_pointer->get<int32_t>());

      case S7DataType::WORD:
        return QString::number(item.data_pointer->get<uint16_t>());

      case S7DataType::DWORD:
        return QString("0x%1").arg(item.data_pointer->get<uint32_t>(), 8, 16,
                                   QChar('0'));

      case S7DataType::UDINT:
        return QLocale(QLocale::English)
            .toString(item.data_pointer->get<uint32_t>());
        // 结果示例： "1,234,567" 而不是 "1234567"

      case S7DataType::REAL:
        return QString::number(item.data_pointer->get<float>(), 'f', 6);

      case S7DataType::STRING:
        return QString::fromStdString(item.data_pointer->get<std::string>());

      default:
        return QString::fromStdString(item.data_pointer->get<std::string>());
      }

    case 4: // Comment 列
      return QString::fromStdString(item.comment);

    default:
      return QVariant();
    }
  }

  return QVariant();
}

bool DataBlockModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    
    if (index.row() >= m_dataBlock->getVariableVectorSize()) {
        return false;
    }
    
    std::vector<S7ModernDataStruct>& items = m_dataBlock->getVariabeDataVector(); // 注意需要非const版本
    S7ModernDataStruct& item = items[index.row()];
    
    if (index.column() == 3) { // Value 列
        // 根据数据类型进行验证和转换
        switch (item.data_type_enum) {
        case S7DataType::BOOL: {
            bool boolValue = value.toBool();
            item.data_pointer->Reset_Value(boolValue);
            break;
        }
        
        case S7DataType::BYTE: {
            bool ok;
            int intValue = value.toInt(&ok);
            if (ok && intValue >= 0 && intValue <= 255) {
                item.data_pointer->Reset_Value(static_cast<uint8_t>(intValue));
            } else {
                return false; // 数据无效
            }
            break;
        }
        
        case S7DataType::INT: {
            bool ok;
            int intValue = value.toInt(&ok);
            if (ok && intValue >= -32768 && intValue <= 32767) {
                item.data_pointer->Reset_Value(static_cast<int16_t>(intValue));
            } else {
                return false;
            }
            break;
        }
        
        case S7DataType::DINT: {
            bool ok;
            qint64 longValue = value.toLongLong(&ok);
            if (ok && longValue >= -2147483648LL && longValue <= 2147483647LL) {
                item.data_pointer->Reset_Value(static_cast<int32_t>(longValue));
            } else {
                return false;
            }
            break;
        }
        
        case S7DataType::WORD: {
            bool ok;
            int intValue = value.toInt(&ok);
            if (ok && intValue >= 0 && intValue <= 65535) {
                item.data_pointer->Reset_Value(static_cast<uint16_t>(intValue));
            } else {
                return false;
            }
            break;
        }
        
        case S7DataType::DWORD:
        case S7DataType::UDINT: {
            bool ok;
            uint32_t uintValue = value.toUInt(&ok);
            if (ok) {
                item.data_pointer->Reset_Value(uintValue);
            } else {
                return false;
            }
            break;
        }
        
        case S7DataType::REAL: {
            float floatValue = value.toFloat();
            item.data_pointer->Reset_Value(floatValue);

            float savedValue = item.data_pointer->get<float>();
            std::cout << "Saved REAL value: " << savedValue
                      << ", expected: " << floatValue << std::endl;
            break;
        }
        
        case S7DataType::STRING: {
            QString stringValue = value.toString();
            item.data_pointer->Reset_Value(stringValue.toStdString());
            break;
        }
        
        default: {
            // 未知类型，尝试存储为字符串
            item.data_pointer->Reset_Value(value.toString());
            break;
        }
        }
        
        // 数据修改成功，发射信号通知视图更新
        emit dataChanged(index, index, {Qt::DisplayRole});
        return true;
    }
    
    if (index.column() == 4) { // Comment 列
        item.comment = value.toString().toStdString();
        emit dataChanged(index, index, {Qt::DisplayRole});
        return true;
    }
    
    // Data Block Number 和 OffReset_Value 列通常只读，不允许编辑
    if (index.column() == 0 || index.column() == 1 || index.column() == 2) {
        return false; // 只读
    }
    
    return false;
}

int DataBlockModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() || !m_dataBlock
             ? 0
             : m_dataBlock->getVariableVectorSize();
}

void DataBlockModel::batchSetDataForS7() {
  if (!m_dataBlock )
    return;

  // 1. 暂时阻断信号
  bool wasBlocked = blockSignals(true);

  int rowIndex = 0;
  for (auto &item : m_dataBlock->getVariabeDataVector()) {
    Result<NormalDataType, RichError> result =
        DataTypeMapper::Data_transform_from_bytes(
            item.data_type_enum, m_dataBlock->getVariableDataBuffer(),
            item.bytes_offset, item.s7_data_type_length, item.bit_offset);
    if (result.is_success()) {
      QVariant value;
      auto &&unwrapped = result.unwrap_returnLeftValue();
      switch (item.data_type_enum) {
      case S7DataType::BOOL:
        item.data_pointer->Reset_Value(std::get<bool>(unwrapped));
        break;
      case S7DataType::BYTE:
        item.data_pointer->Reset_Value(std::get<uint8_t>(unwrapped));
        break;
      case S7DataType::INT:
        item.data_pointer->Reset_Value(std::get<int16_t>(unwrapped));
        break;
      case S7DataType::DINT:
        item.data_pointer->Reset_Value(std::get<int32_t>(unwrapped));
        break;
      case S7DataType::REAL:
        item.data_pointer->Reset_Value(std::get<float>(unwrapped));
        break;
      case S7DataType::WORD:
        item.data_pointer->Reset_Value(std::get<uint16_t>(unwrapped));
        break;
      case S7DataType::DWORD:
        item.data_pointer->Reset_Value(std::get<uint32_t>(unwrapped));
        break;
      case S7DataType::UDINT:
        item.data_pointer->Reset_Value(std::get<uint32_t>(unwrapped));
        break;
      case S7DataType::STRING:
        item.data_pointer->Reset_Value(std::get<std::string>(unwrapped));
        break;
      default:
        break;
      }

    }
    ++rowIndex;
  }

  // 2. 恢复信号并一次性通知更新
  blockSignals(wasBlocked);
  if (!wasBlocked && rowIndex > 0) {
    emit dataChanged(index(0, 3), index(rowIndex - 1, 3));
  }
}

void DataBlockModel::batchSetDataForOPCUA() {
  if (!m_dataBlock )
    return;

  // 2. 恢复信号并一次性通知更新
  {
    emit dataChanged(index(0, 3),
                     index(m_dataBlock->getVariableVectorSize() - 1, 3));
  }
}

//  S7DataDelegate-------------------------------------------------------------------
QWidget* S7DataDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
    if (index.column() == 3) { // Value 列
        // 获取第1列（数据类型列）
        QModelIndex typeIndex = index.sibling(index.row(), 1);
        // 读取数据类型
        S7DataType dataType = static_cast<S7DataType>(
            typeIndex.data(Qt::EditRole).toInt());

        switch (dataType) {
        case S7DataType::BOOL:
            return createBoolEditor(parent);
        case S7DataType::BYTE:
            return createNumberEditor(parent, S7DataType::BYTE);
        case S7DataType::INT:
            return createNumberEditor(parent, S7DataType::INT);
        case S7DataType::DINT:
            return createNumberEditor(parent, S7DataType::DINT);
        case S7DataType::WORD:
            return createNumberEditor(parent, S7DataType::WORD);
        case S7DataType::DWORD:
            return createHexEditor(parent,dataType);
        case S7DataType::UDINT:
            return createHexEditor(parent,dataType);
        case S7DataType::REAL:
            return createfloatEditor(parent);
        case S7DataType::STRING:
            return createStringEditor(parent);

        default:
            return createNumberEditor(parent, dataType);
        }
    }
    
    // 其他列使用默认编辑器
    return QStyledItemDelegate::createEditor(parent, option, index);
}

void S7DataDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QVariant value = index.data(Qt::EditRole);
    
    if (index.column() == 3) {
        QModelIndex typeIndex = index.sibling(index.row(), 1);
        S7DataType dataType = static_cast<S7DataType>(
            typeIndex.data(Qt::EditRole).toInt());

        switch (dataType) {
        case S7DataType::BOOL: {
            QCheckBox* checkBox = qobject_cast<QCheckBox*>(editor);
            if (checkBox)
                checkBox->setChecked(value.toBool());
            break;
        }
        case S7DataType::BYTE:
        case S7DataType::INT:
        case S7DataType::WORD:
        case S7DataType::DINT: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) 
                spinBox->setValue(value.toInt());
            break;
        }
        case S7DataType::DWORD:
        case S7DataType::UDINT: {
          QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
          if (lineEdit) {
            if (dataType == S7DataType::DWORD) {
              // 显示为十六进制
              lineEdit->setText(
                  QString("0x%1").arg(value.toUInt(), 8, 16, QChar('0')));
            } else {
              // 显示为十进制
              lineEdit->setText(QString::number(value.toUInt()));
            }
          }
          break;
        }
        case S7DataType::REAL: {
            QDoubleSpinBox* spinBox = qobject_cast<QDoubleSpinBox*>(editor);
            if (spinBox)
                spinBox->setValue(value.toFloat());
            break;
        }
        case S7DataType::STRING: {
            QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
            if (lineEdit)
                lineEdit->setText(value.toString());
            break;
        }
        default: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox)
                spinBox->setValue(value.toInt());
            break;
        }
        }
    } else {
        QStyledItemDelegate::setEditorData(editor, index);
    }
}

void S7DataDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                  const QModelIndex& index) const {
    if (index.column() == 3) {
        QModelIndex typeIndex = index.sibling(index.row(), 1);
        S7DataType dataType = static_cast<S7DataType>(
            typeIndex.data(Qt::EditRole).toInt());
        
        QVariant value;
        
        switch (dataType) {
        case S7DataType::BOOL: {
            QCheckBox* checkBox = qobject_cast<QCheckBox*>(editor);
            if (checkBox)
                value = checkBox->isChecked();
            break;
        }
        case S7DataType::REAL: {
            QDoubleSpinBox* spinBox = qobject_cast<QDoubleSpinBox*>(editor);
            if (spinBox)
                value = spinBox->value();
            break;
        }
        case S7DataType::STRING: {
            QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor);
            if (lineEdit)
                value = lineEdit->text();
            break;
        }
        case S7DataType::DWORD:
        case S7DataType::UDINT: {
          QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
          if (lineEdit) {
            QString text = lineEdit->text();
            bool ok;
            uint32_t result;

            if (dataType == S7DataType::DWORD && text.startsWith("0x")) {
              // 按十六进制解析
              result = text.mid(2).toUInt(&ok, 16);
            } else {
              // 按十进制解析
              result = text.toUInt(&ok, 10);
            }

            if (ok) {
              model->setData(index, result, Qt::EditRole);
            }
          }
          break;
        }
        case S7DataType::BYTE: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) {
                uint8_t byteValue = static_cast<uint8_t>(spinBox->value());
                value = QVariant::fromValue(byteValue);
            }
            break;
        }
        case S7DataType::INT: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) {
                int16_t intValue = static_cast<int16_t>(spinBox->value());
                value = QVariant::fromValue(intValue);
            }
            break;
        }
        case S7DataType::DINT: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) {
                int32_t dintValue = spinBox->value();
                value = QVariant::fromValue(dintValue);
            }
            break;
        }
        case S7DataType::WORD: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox) {
                uint16_t wordValue = static_cast<uint16_t>(spinBox->value());
                value = QVariant::fromValue(wordValue);
            }
            break;
        }
        default: {
            QSpinBox* spinBox = qobject_cast<QSpinBox*>(editor);
            if (spinBox)
                value = spinBox->value();
            break;
        }
        }
        
        if (value.isValid())
            model->setData(index, value, Qt::EditRole);
    } else {
        QStyledItemDelegate::setModelData(editor, model, index);
    }
}

QWidget* S7DataDelegate::createBoolEditor(QWidget* parent) const {
    QCheckBox* checkBox = new QCheckBox(parent);
    checkBox->setTristate(false);
    return checkBox;
}

QWidget* S7DataDelegate::createfloatEditor(QWidget* parent) const {
    QDoubleSpinBox* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(-1e10, 1e10);
    spinBox->setDecimals(3);
    return spinBox;
}

QWidget *S7DataDelegate::createHexEditor(QWidget *parent,
                                         S7DataType &dataType) const {
  QLineEdit *lineEdit = new QLineEdit(parent);

  // 根据类型设置不同的验证器
  QRegularExpressionValidator *validator = nullptr;

  switch (dataType) {
  case S7DataType::DWORD:
    // 十六进制验证：0x00000000 到 0xFFFFFFFF
    validator = new QRegularExpressionValidator(
        QRegularExpression("0x[0-9A-Fa-f]{1,8}"), lineEdit);
    lineEdit->setPlaceholderText("0x00000000");
    break;

  case S7DataType::UDINT:
    // 十进制验证：0 到 4294967295
    validator = new QRegularExpressionValidator(
        QRegularExpression("[0-9]{1,10}"), lineEdit);
    lineEdit->setPlaceholderText("0 - 4294967295");
    break;

  default:
    break;
  }

  return lineEdit;
}

QWidget* S7DataDelegate::createStringEditor(QWidget* parent) const {
    QLineEdit* lineEdit = new QLineEdit(parent);
    return lineEdit;
}

QWidget* S7DataDelegate::createNumberEditor(QWidget* parent, S7DataType dataType) const {
    QSpinBox* spinBox = new QSpinBox(parent);
    
    switch (dataType) {
    case S7DataType::BYTE:
        spinBox->setRange(0, 255);
        break;
    case S7DataType::INT:
        spinBox->setRange(-32768, 32767);
        break;
    case S7DataType::DINT:
        spinBox->setRange(-2147483648, 2147483647);
        break;
    case S7DataType::WORD:
        spinBox->setRange(0, 65535);
        break;
    case S7DataType::UDINT:
        spinBox->setRange(0, 4294967295U);
        break;
    default:
        spinBox->setRange(-999999, 999999);
    }
    
    return spinBox;
}

void S7DataDelegate::paint(QPainter *painter,
                           const QStyleOptionViewItem &option,
                           const QModelIndex &index) const {
  // 复制选项
  QStyleOptionViewItem opt = option;

  // 设置文本对齐方式为居中
  opt.displayAlignment = Qt::AlignCenter;

  // 调用基类绘制
  QStyledItemDelegate::paint(painter, opt, index);
}

//  DataBlockView---------------------------------------------------------------
void DataBlockView::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // 工具栏
    m_refreshBtn = new QPushButton("Rfresh Table PLC Data", this);
    m_writeBtn = new QPushButton("Write Data InTo PLC", this);
    m_exportBtn = new QPushButton("Expert To CSV", this);
    m_importBtn = new QPushButton("Load In CSV", this);
    
    // 搜索框
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search...");
    
    // 类型过滤器
    m_typeFilter = new QComboBox();
    m_typeFilter->addItem("All Type");
    m_typeFilter->addItem("Bool");
    m_typeFilter->addItem("Int");
    m_typeFilter->addItem("Real");
    m_typeFilter->addItem("String");
    
    // 表格视图
    m_tableView = new QTableView();
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setSortingEnabled(true);

    // 状态栏
    m_statusBar = new QStatusBar();
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    
    // 布局
    auto* topLayout = new QHBoxLayout();
    topLayout->addWidget(m_refreshBtn);
    topLayout->addWidget(m_writeBtn);
    topLayout->addWidget(m_exportBtn);
    topLayout->addWidget(m_importBtn);
    topLayout->addStretch();
    topLayout->addWidget(m_searchEdit);
    topLayout->addWidget(m_typeFilter);
    
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(m_tableView);
    mainLayout->addWidget(m_statusBar);
    mainLayout->addWidget(m_progressBar);
}

void DataBlockView::onRefreshClicked() {
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);  // 不确定进度
    updateButtonStates(true);
    
    // 发送请求给外部层
    emit requestRefresh();
}

void DataBlockView::onWriteClicked() {
    // 确认对话框
    if (QMessageBox::question(this, "确认", "确定要写入PLC吗？") == QMessageBox::Yes) {
        emit requestWrite();
    }
}

void DataBlockView::importFile() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("load file"),
        "",
        tr("support file type (*.csv *.xlsx *.json *.txt *.xml);")
    );

    if (filePath.isEmpty())
      return;
    else if (filePath.endsWith(".csv", Qt::CaseInsensitive) ||
             filePath.endsWith(".xlsx", Qt::CaseInsensitive) ||
             filePath.endsWith(".json", Qt::CaseInsensitive) ||
             filePath.endsWith(".xml", Qt::CaseInsensitive) ||
             filePath.endsWith(".txt", Qt::CaseInsensitive)) {
      emit requestFile(filePath);
      return;
    } else {
      QMessageBox::warning(this, "error",
                           " importFile : there is file type do not supported");
    }
}

void DataBlockView::initializeConnection()
{
  connect(m_refreshBtn, &QPushButton::clicked, this,
          &DataBlockView::onRefreshClicked);
  connect(m_writeBtn, &QPushButton::clicked, this,
          &DataBlockView::onWriteClicked);
  connect(m_exportBtn, &QPushButton::clicked, this,
          &DataBlockView::onExportClicked);
  connect(m_importBtn, &QPushButton::clicked, this,
          &DataBlockView::onImportClicked);

  connect(m_searchEdit, &QLineEdit::textChanged, this,
          &DataBlockView::onFilterChanged);

  connect(m_tableView, &QTableView::doubleClicked, this,
          &DataBlockView::onRowdoubleClicked);
}

// 外部响应层回调
void DataBlockView::onRefreshComplete(bool success, const QString& error) {
    m_progressBar->setVisible(false);
    updateButtonStates(false);
    
    if (success) {
        showStatusMessage("数据刷新成功", false);
    } else {
        showStatusMessage("刷新失败: " + error, true);
    }
}

void DataBlockView::onWriteComplete(bool success, const QString &error) {}
void DataBlockView::onConnectWithDataBlockView(QSplitter *splitter) {
  splitter->addWidget(this);
};
void DataBlockView::onBuildNewTableView(const std::string &ip_Address,
                                        const std::string &dataBlockName) {
  m_currentIp = ip_Address.c_str();
  m_currentDataBlock = dataBlockName.c_str();
}

//  DataBlockBuilder-----------------------------------------------------------------
Result<bool, RichError>
DataBlockBuilder::build(const OPCUADataBlockDefinition &content,const std::string &ip_Address) {
  //  build DataBlock and package it into Result
  auto result = this->add_datablock_from_OPCUADataBlockDefinition(content,ip_Address);

  if (result.is_success()) {
    emit requestSaveDataBlock(std::move(result.unwrap_returnLeftValue()));
    return Result<bool, RichError>(true);
  } else {
    std::cout << "DataBlockBuilder::build is failed." << std::endl;
    return Result<bool, RichError>(RichError{result.unwrap_err()});
  }
}

Result<std::shared_ptr<DataBlock> , RichError>
DataBlockBuilder::add_datablock_from_OPCUADataBlockDefinition(
    const OPCUADataBlockDefinition &data_block_definition,const std::string &ip_Address) {
  // Add variables of basic types through loop checking
  auto dataBlock = std::make_shared<DataBlock>();
  std::vector<S7ModernDataStruct> m_variable_vector;

  bool is_done_successfully = true;
  for (auto &var : data_block_definition.variable_definitions_vector) {
    is_done_successfully =
        add_variable_from_S7XMLVariableDefinition(
            var, data_block_definition.block_number,
            "\"" + data_block_definition.data_block_name + "\"",m_variable_vector)
            .is_success() &&
        is_done_successfully;
  }
  dataBlock->getVariableMap(m_variable_vector);
  dataBlock->calculateDataBlockLength(data_block_definition.total_bytes_size);
  dataBlock->setName(data_block_definition.data_block_name);
  dataBlock->setIpAddres(ip_Address);

  if (is_done_successfully) {
    return Result<std::shared_ptr<DataBlock> , RichError>(dataBlock);
  } else {
    return Result<std::shared_ptr<DataBlock> , RichError>(RichError("add variable failed"));
  }
}

Result<bool, RichError>
DataBlockBuilder::add_variable_from_S7XMLVariableDefinition(
    const S7XMLVariableDefinition &variable_definition, int data_block_number,
    const std::string &prefix,
    std::vector<S7ModernDataStruct> &m_variable_vector) {
  // Add variables of basic types through loop checking
  bool is_done_successfully = false;
  {
    switch (variable_definition.data_type_enum) {
    case S7DataType::ARRAY:
      for (auto &var : variable_definition.struct_member_vector)
        is_done_successfully =
            add_variable_from_S7XMLVariableDefinition(
                var, data_block_number,
                prefix + ".\"" + variable_definition.variable_name + "\"",
                m_variable_vector)
                .is_success() &&
            is_done_successfully;
      break;
    case S7DataType::STRUCT:
      for (auto &var : variable_definition.struct_member_vector)
        is_done_successfully =
            add_variable_from_S7XMLVariableDefinition(
                var, data_block_number,
                prefix + ".\"" + variable_definition.variable_name + "\"",
                m_variable_vector)
                .is_success() &&
            is_done_successfully;
      break;
    default:
      S7ModernDataStruct tmp_variable;
      if (!isNumber(variable_definition.variable_name)
               .unwrap_returnRightValue()) {
        //  NON INTEGER FOR NORMAL SUFFIX
        tmp_variable.variable_full_path =
            prefix + ".\"" + variable_definition.variable_name + "\"";
        tmp_variable.variable_nodeID = tmp_variable.variable_full_path;
      } else {
        //  INTERGER FOR SPECIAL SUFFIX
        tmp_variable.variable_full_path =
            prefix + "[" + variable_definition.variable_name + "]";
        tmp_variable.variable_nodeID = prefix;
      }
      tmp_variable.variable_name = variable_definition.variable_name;
      tmp_variable.data_type_enum = variable_definition.data_type_enum;
      tmp_variable.data_block_number = data_block_number;

      tmp_variable.bytes_offset = variable_definition.bytes_offset;
      tmp_variable.bit_offset = variable_definition.bit_offset;
      tmp_variable.s7_data_type_length =
          variable_definition.s7_data_type_length;
      tmp_variable.s7_data_array_length =
          variable_definition.s7_data_array_length;

      // initialize data_pointer
      switch (variable_definition.data_type_enum) {
      case S7DataType::BOOL:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(false);
        break;
      case S7DataType::BYTE:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(uint8_t{0});
        break;
      case S7DataType::INT:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(int{0});
        break;
      case S7DataType::DINT:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(int32_t{0});
        break;
      case S7DataType::REAL:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(float{0});
        break;
      case S7DataType::WORD:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(uint16_t{0});
        break;
      case S7DataType::DWORD:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(uint32_t{0});
        // 如需显示十六进制，在 data() 函数中转换，而不是存字符串
        break;
      case S7DataType::UDINT:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(uint32_t{0});
        break;
      case S7DataType::STRING:
        tmp_variable.data_pointer = std::make_unique<Dynamic_Value>(std::string{"Null"});
        break;
      default:
        break;
      }

      m_variable_vector.push_back(std::move(tmp_variable));
      is_done_successfully = true;
    }
  }

  return Result<bool, RichError>(is_done_successfully);
}

Result<bool, RichError> DataBlockBuilder::isNumber(const std::string &s) {
  if (s.empty())
    return false;

  char *end = nullptr;
  // 使用 strtod 而不是 atof，因为 atof 无法检测错误
  strtod(s.c_str(), &end);

  // end 指向第一个未转换的字符
  return Result<bool, RichError>(end == s.c_str() + s.length());
}

//  DataBlockController-----------------------------------------------------------
void DataBlockController::initialize(Scope *scope) {
  if (!scope) {
    return;
  } else {
    std::shared_ptr<DataBlockModel> model = scope->getShared<DataBlockModel>();
    if (!model) {
      std::cout << "DataBlockModel getSharedPtr is fail " << std::endl;
    } else {
      m_model = std::move(model);
    }

    std::shared_ptr<DeviceReader> reader = scope->getShared<DeviceReader>();
    if (!reader) {
      std::cout << "DeviceReader getSharedPtr is fail " << std::endl;
    } else {
      m_reader = std::move(reader);
    }

    std::shared_ptr<DataBlockBuilder> builder =
        scope->getShared<DataBlockBuilder>();
    if (!builder) {
      std::cout << "DataBlockBuilder getSharedPtr is fail " << std::endl;
    } else {
      m_DataBlockBuild = std::move(builder);
    }
  }

  
};

void DataBlockController::initializeView(DataBlockView *view)
{
  if(!view)
  {
    std::cout<<"initialize vie fail : view is nullptr "<<std::endl;
    return ;
  }
  else
  {
    m_view = view;
  }
}

void DataBlockController::buildConnection() {
  connect(m_DataBlockBuild.get(), &DataBlockBuilder::requestSaveDataBlock, this,
          &DataBlockController::onSaveDataBlock);
  // connect(m_view, &DataBlockView::requestFile, this,
  //         &DataBlockController::onbuildDataBlockFromDBFile);
  connect(m_view, &DataBlockView::requestRefresh, this,
          &DataBlockController::onViewReadRequested);
  connect(m_view, &DataBlockView::requestWrite, this,
          &DataBlockController::onViewWriteRequested);
}

void DataBlockController::onViewReadRequested() {
  //    notify model update
  auto result = m_reader->ReadDataFromPLC(m_DataBlock.get());
  if(result.is_fail())
  {
    std::cout<<"ViewReadRequest is fail and error : "<<result.unwrap_err().what()<<std::endl;
    return;
  }
  if(m_reader->getConnectWayForS7())
  {
    m_model->batchSetDataForS7();
  }
  else
  {
    m_model->batchSetDataForOPCUA();
  }
};

void DataBlockController::onViewWriteRequested() {
  if (m_reader->getConnectWayForS7()) {
    //  update buffer from data_pointer in BigEndian
    m_DataBlock->updateBufferFromS7ModernStructByMSB(m_model->getModelItemVecotr());
    auto result = m_reader->WriteS7DataBlock_ToPLC(m_DataBlock.get());
    if(result.is_fail())
    {
      std::cout<<"onViewWriteRequested fail : "<<result.unwrap_err().what()<<std::endl;
    }
  } else {
    //  update buffer from data_pointer in littleEndian
    m_DataBlock->updateBufferFromS7ModernStructByLSB(
        m_model->getModelItemVecotr());
    auto result = m_reader->batchWriteSOPCUABlock_ToPLC(m_DataBlock.get());
    if (result.is_fail()) {
      std::cout << "onViewWriteRequested fail : " << result.unwrap_err().what()
                << std::endl;
    }
  }
};

bool DataBlockController::onbuildDataBlockFromDBFile(const std::string &file_path,const std::string &ip_Address) {
  auto parse_result = m_DBParser.read_file_content(file_path).and_then(
      [this](std::string &fileContent) {
        return this->m_DBParser.parse(fileContent);
      });

  if (parse_result.is_success()) {
    m_DataBlockBuild->build(parse_result.unwrap_returnLeftValue(),ip_Address);
    return true;
  } else {
    std::cout << "error : SCL_Parser::Parser exist problem" << std::endl;
    return false;
  }
}


void DataBlockController::onSaveDataBlock(const std::shared_ptr<DataBlock> &dataBlock) {
  if (dataBlock) {
    //  update lastest data block
    m_DataBlock = dataBlock;
    m_model->setDataBlock(m_DataBlock);
  } else {
    std::cout << "onSaveDataBlock : dataBlock is nullptr" << std::endl;
  }
}


//  DataBlockManager-----------------------------------------------------------------
DataBlockManager::DataBlockManager(QObject* parent)
    : QObject(parent)
{
    // 注册共享服务
    m_builder = std::make_shared<DataBlockBuilder>();
}

DataBlockManager::~DataBlockManager()
{
    removeAllDataBlocks();
}

// ========== 核心接口实现 ==========
bool
DataBlockManager::createTableView(const QString &ipAddress,
                                       const QString &filePath) {
  QFileInfo info{filePath};
  DataBlockKey key(ipAddress, info.fileName());

  // 创建新的 DataBlock
  DataBlockContext *context =
      createDataBlockContext(ipAddress, info.fileName());
  if (!context) {
    emit errorOccurred(ipAddress, filePath, "Failed to create DataBlock");
    return false;
  }

  // 存储
  m_dataBlocks[key] = context;

  return true;
}

QTableView* DataBlockManager::getTableView(const QString& ipAddress, 
                                          const QString& dataBlockName)
{
    DataBlockKey key(ipAddress, dataBlockName);
    if (m_dataBlocks.contains(key)) {
        return m_dataBlocks[key]->view->getTableView();
    }
    return nullptr;
}

QWidget* DataBlockManager::getView(const QString& ipAddress, 
                                          const QString& dataBlockName)
{
    DataBlockKey key(ipAddress, dataBlockName);
    if (m_dataBlocks.contains(key)) {
        return m_dataBlocks[key]->view->getView();
    }
    return nullptr;
}


// ========== 创建 DataBlock 上下文 ==========
DataBlockContext* DataBlockManager::createDataBlockContext(const QString& ipAddress,
                                                          const QString& dataBlockName)
{
    DataBlockContext* context = new DataBlockContext();
    context->key = DataBlockKey(ipAddress, dataBlockName);
    context->createTime = QDateTime::currentDateTime();
    context->lastAccessTime = context->createTime;
    
    // 创建独立的 MVC 组件
    context->view = createView();
    context->model = createModel(ipAddress, dataBlockName);
    context->delegate = createDelegate(dataBlockName);
    context->controller = std::make_shared<DataBlockController>();
    context->view->setParent(&context->m_controllWidget);
    
    Scope tmpScope;
    tmpScope.registerService(context->model);
    tmpScope.registerService(m_builder);
    //  register special DeviceReader
    for (auto it = m_readerVector.begin(); it != m_readerVector.end();) {
      if ((*it)->getIdentifier().is_success()) {
        if ((*it)->getIdentifier().unwrap_returnRightValue() ==
            ipAddress.toStdString()) {
          tmpScope.registerService(*it);
          it = m_readerVector.erase(it); // erase 返回下一个有效迭代器
          break;
        } else {
          ++it;
        }
      } else {
        ++it;
      }
    }

    // 初始化Controller,View
    context->controller->initialize(&tmpScope);
    context->controller->initializeView(context->view);
    context->controller->buildConnection();

    if (!context->view || !context->model || !context->delegate) {
        delete context;
        return nullptr;
    }
    
    // 组装 MVC
    context->view->setModel(context->model.get());
    context->view->setDelegate(context->delegate.get());
    
    // 设置连接
    setupDataBlockConnections(context);
    
    return context;
}


DataBlockView *DataBlockManager::createView()
{
  auto view =  new DataBlockView();
  // 配置 View 属性
  return view;
}

std::shared_ptr<DataBlockModel> DataBlockManager::createModel(const QString& ipAddress,
                                                              const QString& dataBlockName)
{
    auto model = std::make_shared<DataBlockModel>();
    return model;
}

std::shared_ptr<S7DataDelegate> DataBlockManager::createDelegate(const QString& dataBlockName)
{
    auto delegate = std::make_shared<S7DataDelegate>();
    return delegate;
}

void DataBlockManager::setupDataBlockConnections(DataBlockContext* context)
{
    if (!context) return;
    
    // 连接 Model 的数据变化信号
    connect(context->model.get(), &DataBlockModel::dataChanged,
            [this, context](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                Q_UNUSED(topLeft);
                Q_UNUSED(bottomRight);
                markAsModified(context->key.ipAddress, 
                              context->key.dataBlockName, 
                              true);
            });
    
    // 可以添加其他信号连接
}

// ========== 删除接口 ==========
bool DataBlockManager::removeDataBlock(const QString& ipAddress, 
                                      const QString& dataBlockName)
{
    DataBlockKey key(ipAddress, dataBlockName);
    
    if (!m_dataBlocks.contains(key)) {
        return false;
    }
    
    // 清理资源
    DataBlockContext* context = m_dataBlocks[key];
    cleanupDataBlockContext(context);
    
    // 从映射中移除
    m_dataBlocks.remove(key);
    
    emit dataBlockRemoved(ipAddress, dataBlockName);
    
    return true;
}

void DataBlockManager::cleanupDataBlockContext(DataBlockContext* context)
{
    if (!context) return;
    
    // 断开所有信号连接
    if (context->model) {
        context->model->disconnect();
    }
    if (context->view) {
        context->view->disconnect();
    }
    
    // View、Model、Delegate 会在 shared_ptr 析构时自动清理
    // 这里只需要清空指针
    // view 对象 由 DataBlockContext 内部的widget对象管理
    context->model.reset();
    context->delegate.reset();
}

// ========== 设备连接管理 ==========
std::shared_ptr<DeviceReader> DataBlockManager::getClient(const QString& ipAddress,const std::string& connectWay)
{
    if (m_clients.contains(ipAddress)) {
        return m_clients[ipAddress];
    }
    return nullptr;
}

// ========== 业务接口实现 ==========
bool DataBlockManager::buildS7Connect(const QString &ipAddress, int rack,
                                      int slot, const std::string &connectWay) {
    auto context = getSpecialReader(ipAddress.toStdString());

    if (context) {
        // 连接已存在，可能返回 true 或重新配置
        qDebug() << "Connection already exists for IP:" << ipAddress;
        return true;  // 或者重新配置现有连接
    }
    
    // 创建新连接
    auto reader = std::make_shared<DeviceReader>();
    reader->setConnectWayStatus(connectWay);
    bool result = reader->onRequestBuildS7Object(ipAddress.toStdString(), rack, slot);
    
    if (result) {
        m_readerVector.push_back(reader);  // 不需要 move，shared_ptr 会复制
        return true;
    }
    
  return reader->onRequest7ObjectCheckConnect();
}

bool DataBlockManager::buildOPCUAConnect(const QString &ipAddress, int nameSpace,
                                      int port,const std::string &connectWay) {
  auto context = getSpecialReader(ipAddress.toStdString());

  if (!context) {
    emit errorOccurred(ipAddress, "", " not found");
    auto reader = std::make_shared<DeviceReader>();
    reader->setConnectWayStatus(connectWay);
    bool result =
        reader->onRequestBuildOPCUA(ipAddress.toStdString(), nameSpace, port);
    if (result) {
      m_readerVector.push_back(std::move(reader));
    } else {
      return false;
    }

    return true;
  }

  std::cout << ipAddress.data() << " has exist " << std::endl;
  return context->onRequestOPCUACheckConnect();
}

bool DataBlockManager::checkConnectToDevice(const QString& ipAddress,
                                      const std::string& connectWay)
{
    auto client = getClient(ipAddress,connectWay);
    if(!client)
    {
      return false;
    }
    else
    {
      if(connectWay== "S7 Offset")
      {
        return client->onRequest7ObjectCheckConnect();
      }
      else
      {
        return client->onRequestOPCUACheckConnect();
      }
    }
}

// ========== 辅助函数 ==========
DataBlockContext *
DataBlockManager::getDataBlockContext(const QString &ipAddress,
                                      const QString &filePath) {
  QFileInfo info{filePath};
  DataBlockKey key(ipAddress, info.fileName());
  if (m_dataBlocks.contains(key)) {
    return m_dataBlocks[key];
  }
  return nullptr;
}

std::shared_ptr<DeviceReader> DataBlockManager::getSpecialReader(const std::string &ip_Address) const {
 for(auto &item:m_readerVector) 
 {
  if(item->getIdentifier().is_success())
  {
    if(item->getIdentifier().unwrap_returnRightValue() == ip_Address)
    {
      return item;
    }
  }
 }
 return nullptr;
}

void DataBlockManager::markAsModified(const QString& ipAddress, 
                                     const QString& dataBlockName, 
                                     bool modified)
{
    DataBlockContext* context = getDataBlockContext(ipAddress, dataBlockName);
    if (context && context->isModified != modified) {
        context->isModified = modified;
        emit dataBlockModified(ipAddress, dataBlockName, modified);
    }
}

bool DataBlockManager::buildDataFromFile(const QString &ip_Address,const QString &filePath)
{
  auto dataPtr = getDataBlockContext(ip_Address, filePath);
  if(!dataPtr)
  {
    // create new tableView and dataBlockContext
    bool result = createTableView(ip_Address, filePath);
    if(!result)
    {
      return false;
    }
    else
    {
      DataBlockContext *context = getDataBlockContext(ip_Address, filePath);
      if(context)
      {
        //  create dataBlock for filling contextt into model
        return context->controller->onbuildDataBlockFromDBFile(
            filePath.toStdString(), ip_Address.toStdString());
      }
      else
      {
        return false;
      }
    }
  }
  {
    return true;
  }
}
