#include "PLC/OPC_UA.h"
//  EndianConverter----------------------------------------------------------------------------
static Result<bool, RichError>
S7BigEndianToLittleEndian(std::vector<uint8_t> &Sourcebuffer,
               std::vector<uint8_t> &Destbuffer, const OPCUAModernDataStruct &var) {
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
               std::vector<uint8_t> &&Destbuffer, const OPCUAModernDataStruct &var) {
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



//S7_Access------------------------------------------------------------------
Result<bool,RichError> S7_Access::connect() 
{
    bool check_result = isConnected();
    if(check_result)
    {
      return Result<bool, RichError>(check_result);
    }

    int result =
        Cli_ConnectTo(m_client_var, m_ip_Address.data(), m_rack, m_slot);
    if (result != 0) {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        std::cout << "Connection failed: " << error_text
                << " (Error code: " << result << ")\n";
        return Result<bool, RichError>(RichError("connect fail"));
    } else {
        std::cout << "Connection successful\n";
    }
    return Result<bool, RichError>(result);
}

bool S7_Access::isConnected()
{
    int cpu_state;
    int result = Cli_GetPlcStatus(m_client_var, &cpu_state);
    if (result == 0) {
      std::cout << "CPU is in state " << cpu_state << "\n";
      if (cpu_state == S7CpuStatusRun) {
        std::cout << "CPU is running\n";
      }
    }
    // 如果连接断开，Cli_GetPlcStatus 会返回非 0 错误码
    return (result == 0);
}

void S7_Access::disconnect() 
{
    if (isConnected()) {
        Cli_Disconnect(m_client_var);
    }
    Cli_Destroy(&m_client_var);
    std::cout << "PLC_Device disconnct\n";
}

S7Object& S7_Access::getClient()
{
    return this->m_client_var;
}

Result<bool, RichError>
S7_Access::batchReadS7DataBlock_FromPLC(OPCUADataBlock *data) {
  bool success = true;
  for (auto &var : data->getVariabeDataVector()) {
    Sourcebuffer.clear();
    Sourcebuffer.resize(var.s7_data_type_length);
    {
      auto result = this->read(1, var.bytes_offset, var.s7_data_type_length,
                               Sourcebuffer.data());
      if (result.is_fail()) {
        std::cout << "Read action is fail" << std::endl;
        return Result<bool, RichError>(result);
        success = false;
      } else {
        success = true;
        auto result = S7BigEndianToLittleEndian(Sourcebuffer, Destbuffer, var);
        success = success && result.is_success();
      }
    }
  }
  if (success) {
    data->getVariableDataBuffer().clear();
    std::swap(Destbuffer, data->getVariableDataBuffer());
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError("read variable failed"));
  }
}

Result<bool, RichError>
S7_Access::batchWriteS7DataBlock_ToPLC(OPCUADataBlock *data) {
  {
     {
      bool success = true;
      for (auto &var : data->getVariabeDataVector()) {
        {
          tmpBuffer.clear();
          tmpBuffer.resize(var.s7_data_type_length);
          {
            std::move(data->getVariableDataBuffer().begin() + var.bytes_offset,
                      data->getVariableDataBuffer().begin() + var.bytes_offset +
                          var.s7_data_type_length,
                      tmpBuffer.begin());
          }

          //  single write condition result
          auto result = this->write(
              1, var.bytes_offset, var.s7_data_type_length, tmpBuffer.data());
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
}

Result<bool,RichError> S7_Access::read(int DB_Number,int Start_Position,int Read_Size,uint8_t *SourceData_var) 
{
    bool connect_check = this->isConnected();
    if (!connect_check) {
    return Result<bool, RichError>(false);
    }

    int result = Cli_DBRead(m_client_var,DB_Number, Start_Position, Read_Size,SourceData_var);
    if (result != 0)
    {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        std::cout<<"Read failed: " << error_text << " (Error code: " << result << ")\n";
        return Result<bool, RichError>(RichError{error_text});
    }
    return Result<bool,RichError> (true);
}

 Result<bool,RichError> S7_Access::write(int DB_Number,int Start_Position,int Read_Size,uint8_t *SourceData_var) 
{
    int result = Cli_DBWrite(m_client_var,DB_Number, Start_Position, Read_Size,SourceData_var);
    if (result != 0)
    {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        std::cout<<"Read failed: " << error_text << " (Error code: " << result << ")\n";
    }
    return Result<bool,RichError> (true);
}

//  OPCUA_Access------------------------------------------------------------------
Result<bool, RichError> OPCUA_Access::connect() {
  // 1. 如果已经连接，直接返回成功
  auto connectResult = isConnected();
  if (!connectResult) {
    return Result<bool, RichError>(std::move(connectResult));
  }
  configureClient(); // 配置客户端参数

  // 4. 构建连接 URL（修复临时字符串问题）
  std::string endpointUrl =
      "opc.tcp://" + m_ip_Address + ":" + std::to_string(m_port);

  // 5. 尝试连接
  UA_StatusCode result =
      UA_Client_connect(m_client_pointer, endpointUrl.c_str());

  if (result != UA_STATUSCODE_GOOD) {
    char error_text[256];
    std::string errorMsg = "Connection failed: " + std::string(error_text) +
                           " (Error code: " + std::to_string(result) + ")";
    return Result<bool, RichError>(RichError(errorMsg));
  }

  return Result<bool, RichError>(true);
}

bool OPCUA_Access::isConnected()
{
  if (!m_client_pointer) {
    return false;
  }

  UA_SecureChannelState channelState;
  UA_SessionState sessionState;
  UA_Client_getState(m_client_pointer, &channelState, &sessionState, nullptr);

  // 只有当 SecureChannel 和 Session 都处于“已建立”状态，才算真正连接成功
  return (channelState == UA_SECURECHANNELSTATE_OPEN &&
          sessionState == UA_SESSIONSTATE_ACTIVATED);
}

template<>
Result<bool,RichError> OPCUA_Access::Set_UA_Scalar_StatusCode(int nameSpace,OPCUAModernDataStruct &var,std::string &source_var)
{
    //  type->value->request->response
    // CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    UA_WriteValue_init(&m_writeValue);
    UA_NodeId_copy(&var.nodeID, &m_writeValue.nodeId);
    // m_writeValue.nodeId =
    //     UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
    m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;
    UA_String tmpBuffer = UA_STRING(const_cast<char*>(source_var.c_str()));

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setScalarCopy(&m_writeValue.value.value,&tmpBuffer,type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access :Set_UA_Scalar_StatusCode fail"));
    }
    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}

template<typename T>
Result<bool,RichError> OPCUA_Access::Set_UA_Scalar_StatusCode(int nameSpace,OPCUAModernDataStruct &var,T &source_var)
{
    //  type->value->request->response
    // CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    UA_WriteValue_init(&m_writeValue);
    UA_NodeId_copy(&var.nodeID, &m_writeValue.nodeId);
    // m_writeValue.nodeId =
    //     UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
    m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setScalarCopy(&m_writeValue.value.value,&source_var,type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access : Set_UA_Scalar_StatusCode fail"));
    }
    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}

template<typename T>
Result<bool,RichError> OPCUA_Access::Set_UA_Array_StatusCode(int nameSpace,OPCUAModernDataStruct &var,std::vector<T> &source_vector)
{
    //  type->value->request->response
    // CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    UA_WriteValue_init(&m_writeValue);
    UA_NodeId_copy(&var.nodeID, &m_writeValue.nodeId);
    // m_writeValue.nodeId =
    //     UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
    m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setArrayCopy(&m_writeValue.value.value,source_vector.data(),source_vector.size(),type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access : Set_UA_Array_StatusCode fail"));
    }
    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}

template<typename T>
Result<bool,RichError> OPCUA_Access::batchSet_UA_Scalar_StatusCode(int nameSpace,OPCUAModernDataStruct &var,T &source_var,int index)
{
    //  type->value->request->response
    // CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    auto &m_writeValue = m_batchWriteNodes[index];
    if(m_writeValue.attributeId != UA_ATTRIBUTEID_VALUE)
    {
    UA_NodeId_copy(&var.nodeID, &m_writeValue.nodeId);
      // m_writeValue.nodeId =
      //     UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
      m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;
    }

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setScalarCopy(&m_writeValue.value.value,&source_var,type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access : batchSet_UA_Scalar_StatusCode fail"));
    }
    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}

template<>
Result<bool,RichError> OPCUA_Access::batchSet_UA_Scalar_StatusCode(int nameSpace,OPCUAModernDataStruct &var,std::string &source_var,int index)
{
    //  type->value->request->response
    //  CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    auto &m_writeValue = m_batchWriteNodes[index];
    if (m_writeValue.attributeId != UA_ATTRIBUTEID_VALUE) {
    UA_NodeId_copy(&var.nodeID, &m_writeValue.nodeId);
      // m_writeValue.nodeId =
      //     UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
      m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;
    }

    // 一些 OPC UA 库提供辅助宏
    UA_String uaString = UA_STRING(const_cast<char *>(source_var.c_str()));

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setScalarCopy(&m_writeValue.value.value,&uaString,type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access : batchSet_UA_Scalar_StatusCode fail"));
    }

    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}

template<typename T>
Result<bool,RichError> OPCUA_Access::batchSet_UA_Array_StatusCode(int nameSpace,OPCUAModernDataStruct &var,std::vector<T> &source_vector,int index)
{
    //  type->value->request->response
    // CHECK TYPE MATCH
    auto it = s7_to_ua_map.find(var.data_type_enum);
    if(it == s7_to_ua_map.end())
    {
        return Result<bool,RichError> (RichError("OPCUA_Access : write type does not match T "));
    }
    const UA_DataType *type = it->second;

    //  INIT WRITE UA_VALUE
    auto &m_writeValue = m_batchWriteNodes[index];
    if (m_writeValue.attributeId != UA_ATTRIBUTEID_VALUE) {
    UA_NodeId_copy(&var.nodeID, &m_writeValue.nodeId);
      // m_writeValue.nodeId =
      //     UA_NODEID_STRING_ALLOC(nameSpace, var.variable_nodeID.data());
      m_writeValue.attributeId = UA_ATTRIBUTEID_VALUE;
    }

    //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
    UA_StatusCode status = UA_Variant_setArrayCopy(&m_writeValue.value.value,source_vector.data(),source_vector.size(),type);
    if(status != UA_STATUSCODE_GOOD)
    {
        UA_WriteValue_clear(&m_writeValue);
        return Result<bool,RichError> (RichError("OPCUA_Access : batchSet_UA_Array_StatusCode fail"));
    }
    m_writeValue.value.hasValue = true;
    return Result<bool,RichError> (true);
}


void OPCUA_Access::Clear_Read_Respondse()
{
    //  CLEAR RESOURCE AFTER SUCCESS LOOP
    UA_ReadResponse_clear(&(m_read_response));
    for (auto &element : m_batchReadVariant) {
      UA_Variant_clear(&element); // 释放第一次的堆内存
    }
}

void OPCUA_Access::Clear_Write_Respondse()
{
    //  CLEAR RESOURCE AFTER SUCCESS LOOP
    UA_WriteResponse_clear(&(m_write_response));
    for(auto & element : m_batchWriteNodes)
    {
      UA_Variant_clear(&element.value.value); // 释放第一次的堆内存
    }
}

UA_Client *OPCUA_Access::getClient()
{
    return this->m_client_pointer;
}

void OPCUA_Access::Set_Read_NodeID(UA_ReadValueId &nodeid,OPCUAModernDataStruct& data_var)
{
  if(data_var.buildType == InputFormat::BROWSER)
  {
    nodeid.nodeId = UA_NODEID_STRING_ALLOC(this->m_nameSpace,
                                           uaStringToString(data_var.nodeID.identifier.string).data());
  }
  else
  {
    nodeid.nodeId = UA_NODEID_STRING_ALLOC(this->m_nameSpace,
                                           data_var.variable_nodeID.data());
  }

  nodeid.indexRange = UA_STRING_NULL;
  nodeid.attributeId = UA_ATTRIBUTEID_VALUE;
}

Result<bool, RichError> OPCUA_Access::Read_UA_Variant_From_PLC() {
  if (!UA_Variant_isEmpty(&m_batchReadVariant[0])) {
    Clear_Read_Respondse();
  }
  Result<bool, RichError> it = this->read();
  if (it.is_success()) {
    //  RECORD RESPONSE_VALUE
    for (int i = 0; i < m_batchReadNodes.size(); ++i) {
      UA_Variant_steal(&m_read_response.results[i].value,
                       &m_batchReadVariant[i]);
    }

    return Result<bool, RichError>(true);
  } else {
    Clear_Read_Respondse();
    return Result<bool, RichError>(RichError(it.unwrap_err()));
  }
}

// T Function --------- Covert_UA_Scalar_To_Specific  
template<typename T>
void OPCUA_Access::Covert_UA_Scalar_To_Specific(T &SourceData_var,int i)
{
  // ELSE UA TPYE MATCH S7 DATA TYPE
  memcpy(&SourceData_var, ((static_cast<T *>(m_batchReadVariant[i].data))),
         sizeof(T));
}


// String Special Function --------- Covert_UA_Scalar_To_Specific  
template<>
void OPCUA_Access::Covert_UA_Scalar_To_Specific(std::string &SourceData_var,int i)
{
   const UA_String *src =
            static_cast<const UA_String *>(m_batchReadVariant[i].data) ;
        if (src && src->length>0) {
          SourceData_var.assign(reinterpret_cast<const char*>((src->data)), src->length);
        } else {
          SourceData_var = "";
    }
}       


// T Function --------- Covert_Uint8_Vector_To_Normal_Vector  
template <typename T>
void OPCUA_Access::Covert_Uint8_Vector_To_Normal_Vector(
    OPCUAModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::vector<T> &dest_vector) {
  // dest_vector.resize(var.s7_data_array_length);
  // memcpy(dest_vector.data(),
  //        reinterpret_cast<T*>((src_vector.data()) + int(var.bytes_offset)),
  //        var.s7_data_array_length*sizeof(T));
}

// char Special Function --------- Covert_Uint8_Vector_To_Normal_Vector  
template <>
void OPCUA_Access::Covert_Uint8_Vector_To_Normal_Vector(
    OPCUAModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::vector<char> &dest_vector) {
  // // ELSE UA TPYE MATCH S7 DATA TYPE
  // dest_vector.resize(var.s7_data_array_length);
  // memcpy(dest_vector.data(), (src_vector.data() + int(var.bytes_offset)),
  //        (var.s7_data_array_length + 7) / 8);
}

// string Special Function --------- Covert_Uint8_Vector_To_Normal_Vector  
template <>
void OPCUA_Access::Covert_Uint8_Vector_To_Normal_Vector(
    OPCUAModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::vector<std::string> &dest_vector) {
  // std::string result(reinterpret_cast<const char *>(src_vector.data() +
  //                                                   int(var.bytes_offset) + 2),
  //                    var.s7_data_type_length - 2);
  // dest_vector.push_back(std::move(result));
}


// T Function --------- Covert_Uint8_t_Vector_To_Normal_Scalar  
template <typename T>
void OPCUA_Access::Covert_Uint8_t_Vector_To_Normal_Scalar(
    OPCUAModernDataStruct &var, std::vector<uint8_t> &src_vector,
    T &dest_var) {
  // memcpy(&dest_var,
  //        reinterpret_cast<T *>((src_vector.data()) + int(var.bytes_offset)),
  //        sizeof(T));
}

// bool Function --------- Covert_Uint8_t_Vector_To_Normal_Scalar  
template <>
void OPCUA_Access::Covert_Uint8_t_Vector_To_Normal_Scalar(
    OPCUAModernDataStruct &var, std::vector<uint8_t> &src_vector,
    bool &dest_var) {
  // uint8_t tmp;
  // memcpy(&tmp, ((src_vector.data()) + int(var.bytes_offset)), 1);
  // dest_var = (tmp >> var.bit_offset) & 0x01;
}

// string Function --------- Covert_Uint8_t_Vector_To_Normal_Scalar  
template <>
void OPCUA_Access::Covert_Uint8_t_Vector_To_Normal_Scalar(
    OPCUAModernDataStruct &var, std::vector<uint8_t> &src_vector,
    std::string &dest_var) {
  // std::string result(reinterpret_cast<const char *>(src_vector.data() +
  //                                                   int(var.bytes_offset) + 2),
  //                    var.s7_data_type_length - 2);
  // dest_var = std::move(result);
}

// T Function --------- ByteDeserialization_memcpy  
template<typename T>
void OPCUA_Access::ByteDeserialization_memcpy(T &dest,const std::vector<uint8_t> &src,int data_offset,int data_length)
{
  // ELSE UA TPYE MATCH S7 DATA TYPE
  memcpy(&dest,src.data()+data_offset,data_length);
}

// string Function --------- ByteDeserialization_memcpy  
template<>
void OPCUA_Access::ByteDeserialization_memcpy(std::string &dest,const std::vector<uint8_t> &src,int data_offset,int data_length)
{
  // 边界检查（强烈建议加上）
  if (data_offset < 0 || data_length < 0 ||
      static_cast<size_t>(data_offset + data_length) > src.size()) {
    throw std::out_of_range("ByteDeserialization_memcpy: invalid range");
  }

  dest.assign(reinterpret_cast<const char *>(src.data() + data_offset + 2),
              data_length - 2);
}

Result<bool,RichError> OPCUA_Access::Set_UA_To_Read_Normal_Scalar(const S7DataType &s7_type,Dynamic_Value &value,int i)
{
    if( s7_type == S7DataType::BOOL)
    {
        bool tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::BYTE)
    {
        uint8_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::INT)
    {
        int16_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::WORD)
    {
        uint16_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::DWORD || s7_type == S7DataType::UDINT)
    {
        uint32_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::DINT)
    {
        int32_t tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::REAL)
    {
        float tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::STRING)
    {
        std::string tmp;
        {
            Covert_UA_Scalar_To_Specific(tmp,i);
            value.Reset_Value(std::move(tmp));
        }
    }
    else
    {
      return Result<bool, RichError>(
          RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
    }
    return Result<bool, RichError>(true);
}

Result<bool,RichError> OPCUA_Access::Set_Normal_To_Write_UA_Scalar(
      OPCUAModernDataStruct &var,
      std::vector<uint8_t> &m_data_block_buffer)
{
  if (var.data_type_enum == S7DataType::BOOL) {
    bool tmp;
    {
        Covert_Uint8_t_Vector_To_Normal_Scalar(
            var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::BYTE) {
    uint8_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::INT) {
    int16_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::WORD) {
    uint16_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::DWORD ||
             var.data_type_enum == S7DataType::UDINT) {
    uint32_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::DINT) {
    int32_t tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::REAL) {
    float tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else if (var.data_type_enum == S7DataType::STRING) {
    std::string tmp;
    {
      Covert_Uint8_t_Vector_To_Normal_Scalar(
          var, m_data_block_buffer, tmp);
      return this->write_by_scalar(var, tmp);
    }
  } else {
    return Result<bool, RichError>(
        RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
  }
}

Result<bool, RichError>
OPCUA_Access::batchSet_Normal_To_Write_UA_Scalar(OPCUAModernDataStruct &var,
                                                 int index) {
  if (var.data_type_enum == S7DataType::BOOL) {
    bool tmp{var.data_pointer->get<bool>()};
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, var, tmp, index);
  } else if (var.data_type_enum == S7DataType::BYTE) {
    uint8_t tmp{var.data_pointer->get<uint8_t>()};
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, var, tmp, index);
  } else if (var.data_type_enum == S7DataType::INT) {
    int16_t tmp{var.data_pointer->get<int16_t>()};
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, var, tmp, index);
  } else if (var.data_type_enum == S7DataType::WORD) {
    uint16_t tmp{var.data_pointer->get<uint16_t>()};
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, var, tmp, index);
  } else if (var.data_type_enum == S7DataType::DWORD ||
             var.data_type_enum == S7DataType::UDINT) {
    uint32_t tmp{var.data_pointer->get<uint32_t>()};
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, var, tmp, index);
  } else if (var.data_type_enum == S7DataType::DINT) {
    int32_t tmp{var.data_pointer->get<int32_t>()};
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, var, tmp, index);
  } else if (var.data_type_enum == S7DataType::REAL) {
    float tmp{var.data_pointer->get<float>()};
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, var, tmp, index);
  } else if (var.data_type_enum == S7DataType::STRING) {
    std::string tmp{var.data_pointer->get<std::string>()};
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, var, tmp, index);
  } else {
    return Result<bool, RichError>(
        RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
  }
}

Result<bool,RichError> OPCUA_Access::Set_Normal_To_Write_UA_Vector(
      OPCUAModernDataStruct &var,
      std::vector<uint8_t> &m_data_block_buffer)
{
    if( var.data_type_enum == S7DataType::BOOL)
    {
        std::vector<char> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                       tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::BYTE)
    {
        std::vector<uint8_t> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::INT)
    {
        std::vector<UA_Int16> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::WORD)
    {
        std::vector<UA_UInt16> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::DWORD || var.data_type_enum == S7DataType::UDINT)
    {
        std::vector<UA_UInt32> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::DINT)
    {
        std::vector<UA_Int32> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::REAL)
    {
        std::vector<UA_Float> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else if( var.data_type_enum == S7DataType::STRING)
    {
        std::vector<UA_String > tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);
          return this->write_by_vector(var, tmp);
        }
    }
    else
    {
        return Result<bool,RichError> (RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
    }
}

Result<bool,RichError> OPCUA_Access::batchSet_Normal_To_Write_UA_Vector(
      OPCUAModernDataStruct &var,
      std::vector<uint8_t> &m_data_block_buffer,int index)
{
    if( var.data_type_enum == S7DataType::BOOL)
    {
        std::vector<char> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                       tmp);
          return batchSet_UA_Array_StatusCode(m_nameSpace, var, tmp,index);
        }
    }
    else if( var.data_type_enum == S7DataType::BYTE)
    {
        std::vector<uint8_t> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,tmp);

          return batchSet_UA_Array_StatusCode(m_nameSpace, var, tmp,index);
        }
    }
    else if( var.data_type_enum == S7DataType::INT)
    {
        std::vector<UA_Int16> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);

          return batchSet_UA_Array_StatusCode(m_nameSpace, var, tmp,index);
        }
    }
    else if( var.data_type_enum == S7DataType::WORD)
    {
        std::vector<UA_UInt16> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);

          return batchSet_UA_Array_StatusCode(m_nameSpace, var, tmp,index);
        }
    }
    else if( var.data_type_enum == S7DataType::DWORD || var.data_type_enum == S7DataType::UDINT)
    {
        std::vector<UA_UInt32> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);

          return batchSet_UA_Array_StatusCode(m_nameSpace, var, tmp,index);
        }
    }
    else if( var.data_type_enum == S7DataType::DINT)
    {
        std::vector<UA_Int32> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);

          return batchSet_UA_Array_StatusCode(m_nameSpace, var, tmp,index);
        }
    }
    else if( var.data_type_enum == S7DataType::REAL)
    {
        std::vector<UA_Float> tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);

          return batchSet_UA_Array_StatusCode(m_nameSpace, var, tmp,index);
        }
    }
    else if( var.data_type_enum == S7DataType::STRING)
    {
        std::vector<UA_String > tmp;
        {
          Covert_Uint8_Vector_To_Normal_Vector(var, m_data_block_buffer,
                                        tmp);

          return batchSet_UA_Array_StatusCode(m_nameSpace, var, tmp,index);
        }
    }
    else
    {
        return Result<bool,RichError> (RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
    }
}

Result<bool, RichError> OPCUA_Access::Set_Read_UA_Array(
    std::vector<OPCUAModernDataStruct> &var_vector,
    OPCUAModernDataStruct &var,int &index)
{
    // 读取数据并存储到 data_pointer
    auto result =
        Set_UA_To_Read_Normal_Scalar(var.data_type_enum, *var.data_pointer,
                                     index); // 传递索引位置

    return Result<bool, RichError>(result);
}

Result<bool, RichError> OPCUA_Access::read() {
  auto connectResult = ensureConnection();
  if (connectResult.is_fail()) {
    return Result<bool, RichError>(std::move(connectResult));
  }

  UA_ReadRequest request;
  UA_ReadRequest_init(&request);
  request.nodesToRead = m_batchReadNodes.data();
  request.nodesToReadSize = m_batchReadNodes.size();
  request.timestampsToReturn = UA_TIMESTAMPSTORETURN_NEITHER;

  m_read_response = UA_Client_Service_read(m_client_pointer, request);

  if (m_read_response.responseHeader.serviceResult != UA_STATUSCODE_GOOD ||
      m_read_response.resultsSize == 0 ||
      !m_read_response.results[0].hasValue) {
    std::stringstream ss;
    ss << "read fail :"
       << " read_response.responseHeader.serviceResult : "
       << m_read_response.responseHeader.serviceResult
       << " read_response.resultsSize : " << m_read_response.resultsSize;

    return Result<bool, RichError>(RichError(ss.str()));
  }

  return Result<bool, RichError>(true);
}

Result<bool,RichError> OPCUA_Access::read_nameSpace()
{
    bool is_success = true;
    UA_ReadValueId rvi;
    UA_ReadValueId_init(&rvi);
    rvi.nodeId = UA_NODEID_NUMERIC(0, 2225);
    rvi.indexRange = UA_STRING_NULL;
    rvi.attributeId = UA_ATTRIBUTEID_VALUE;

    //  INIT UA_WRITE_REQUEST
    UA_ReadRequest request;
    UA_ReadRequest_init(&request);
    request.nodesToRead = &rvi;
    request.nodesToReadSize = 1;

    // UA_NodeId plcNode = UA_NODEID_STRING_ALLOC(3, const_cast<char*>("\"DB111_EdgeGatewayTest\""));
    // auto it = read_variable_from_device(plcNode,true);
    // return Result<bool,RichError> (it);

    //  TRY WRITE UA_VALUE INTO DEVICE
    UA_ReadResponse read_response =
        UA_Client_Service_read(m_client_pointer, request);
    if (read_response.responseHeader.serviceResult !=
            UA_STATUSCODE_GOOD ||
        read_response.resultsSize != 1 ||
        &read_response.results[0] != nullptr &&
        !read_response.results[0].hasValue 
    ) {
      std::stringstream ss;
      ss << "read name Space fail :"
         << " error code : "
         << read_response.responseHeader.serviceResult
         << " read_response.resultsSize : " << read_response.resultsSize;
     
      UA_ReadResponse_clear(&read_response);
      return Result<bool, RichError>(
          RichError(ss.str()));
    }
    else
    {
        UA_Variant *value = &read_response.results[0].value;
        if(!UA_Variant_hasArrayType(value, &UA_TYPES[UA_TYPES_STRING]))
        {
            return Result<bool, RichError>(
                RichError("NameSpaceArray is not string array"));
        }

        size_t nsCount = value->arrayLength;
        UA_String *nsArray = (UA_String *)value->data;
        for (size_t i = 0; i < nsCount; ++i) {
          std::string uri((char *)nsArray[i].data, nsArray[i].length);
          std::cout << "NameSpace[" << i << "] = " << uri << std::endl;
       
          auto it = read_variable_from_device(rvi.nodeId,false);
          if (it.is_fail()) {
            is_success = false;
          } else {
            break;
          }
        }
    }
    UA_ReadResponse_clear(&read_response);
    if (is_success) {
      return Result<bool, RichError>(is_success);
    } else {
      return Result<bool, RichError>(
          RichError("read_variable_from_device exist error"));
    }
}

Result<bool,RichError> OPCUA_Access::nodeIdToString(std::string &str,UA_NodeId &nodeId)
{
  UA_String output;
  UA_String_init(&output); // 初始化为空字符串

  UA_StatusCode status = UA_NodeId_print(&nodeId, &output);
  if (status != UA_STATUSCODE_GOOD) {
      return Result<bool,RichError>(RichError("invalid-nodeid"));
  }

  // 注意：output.data 不一定以 '\0' 结尾，必须用 length 构造 std::string
  str.assign(reinterpret_cast<char *>(output.data), output.length);

  // 释放 open62541 分配的内存
  UA_String_clear(
      &output); // 等价于 UA_free(output.data); output = {0, nullptr};

  return Result<bool,RichError>(true);
}

bool OPCUA_Access::isSiemensContainer(const std::string& browseName)
{
  return (browseName == "DataBlocksGlobal" ||
          browseName == "DataBlocksInstance" || browseName == "Inputs" ||
          browseName == "Outputs" || browseName == "Memory");
}

 ConnectionState OPCUA_Access::getConnectionState() const {
    if (!m_client_pointer) {
      return ConnectionState::UNINITIALIZED;
    }

    UA_SecureChannelState channelState;
    UA_SessionState sessionState;
    UA_Client_getState(m_client_pointer, &channelState, &sessionState, nullptr);

    if (channelState == UA_SECURECHANNELSTATE_OPEN &&
        sessionState == UA_SESSIONSTATE_ACTIVATED) {
      return ConnectionState::CONNECTED;
    }

    return ConnectionState::OBJECT_ONLY; // 僵尸对象
  }

  Result<bool, RichError> OPCUA_Access::ensureConnection() {
    switch (getConnectionState()) {
    case ConnectionState::CONNECTED:
      return Result<bool, RichError>(true);

    case ConnectionState::OBJECT_ONLY:
      // 僵尸对象：尝试重连而不是重新创建
      return reconnect(1, 1000);

    case ConnectionState::UNINITIALIZED:
      // 需要创建新对象
      return connect();

    default:
      return Result<bool, RichError>(RichError("Unknown state"));
    }
  }

  Result<bool, RichError> OPCUA_Access::reconnect(int maxRetries,
                                                  int retryDelayMs) {
    // 1. 如果已连接，先断开
    disconnect();

    // 2. 清理可能存在的无效客户端
    if (m_client_pointer) {
      UA_Client_delete(m_client_pointer);
      m_client_pointer = nullptr;
    }

    // 3. 创建新客户端
    m_client_pointer = UA_Client_new();
    if (!m_client_pointer) {
      UA_Client_delete(m_client_pointer);
      m_client_pointer = nullptr;
      return Result<bool, RichError>(
          RichError("Failed to create client for reconnect"));
    }
    configureClient(); // 配置客户端参数

    // 4. 重连循环
    std::string endpointUrl =
        "opc.tcp://" + m_ip_Address + ":" + std::to_string(m_port);

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
      std::cout << "Reconnection attempt " << attempt << "/" << maxRetries
                << std::endl;

      UA_StatusCode result =
          UA_Client_connect(m_client_pointer, endpointUrl.c_str());

      if (result == UA_STATUSCODE_GOOD) {
        if (waitForSessionActivation(5000).is_success()) {
          std::cout << "Reconnection successful" << std::endl;
          return Result<bool, RichError>(true);
        } else {
          std::cerr << "Session activation timeout" << std::endl;
          // 会话激活失败，继续重试
          UA_Client_disconnect(m_client_pointer);
        }
      }

      // 连接失败，记录错误
      char error_text[256];
      Cli_ErrorText(result, error_text, sizeof(error_text));
      std::cerr << "Reconnection attempt " << attempt
                << " failed: " << error_text << " (code: " << result << ")"
                << std::endl;

      // 最后一次尝试失败后不再等待
      if (attempt < maxRetries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
      }
    }

    return Result<bool, RichError>(RichError("Reconnection failed after " +
                                             std::to_string(maxRetries) +
                                             " attempts"));
  }


 std::vector<UA_Variant>& OPCUA_Access::getReadVariant() 
 {
   return m_batchReadVariant;
 }

 std::vector<UA_WriteValue>& OPCUA_Access::getWriteNodes()
 {
   return m_batchWriteNodes;
 }

  Result<bool, RichError>
  OPCUA_Access::waitForSessionActivation(int timeoutMs) {
    auto startTime = std::chrono::steady_clock::now();
    const int checkIntervalMs = 50;

    while (true) {
      UA_SecureChannelState channelState;
      UA_SessionState sessionState;
      UA_Client_getState(m_client_pointer, &channelState, &sessionState,
                         nullptr);

      // 检查是否已激活
      if (channelState == UA_SECURECHANNELSTATE_OPEN &&
          sessionState == UA_SESSIONSTATE_ACTIVATED) {
        return Result<bool, RichError>(true);
      }

      // 检查是否出错
      if (sessionState == UA_SESSIONSTATE_CLOSED ||
          channelState == UA_SECURECHANNELSTATE_CLOSED) {
        std::stringstream ss;
        ss << " sessionState :" << sessionState
           << " channelState :" << channelState ;
        return Result<bool, RichError>(RichError{std::move(ss.str())});
      }

      // 超时检查
      auto now = std::chrono::steady_clock::now();
      if (now - startTime > std::chrono::milliseconds(timeoutMs)) {
        std::stringstream ss;
        ss << "waitFor Session is over time !"; 
        return Result<bool, RichError>(RichError{std::move(ss.str())});
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
    }
  }

  void OPCUA_Access::disconnect() {
    if (!isConnected() && m_client_pointer) {
      UA_Client_disconnect(m_client_pointer);
    }
  }

  void OPCUA_Access::configureClient() {
    if (!m_client_pointer)
      return;

    UA_ClientConfig *config = UA_Client_getConfig(m_client_pointer);
    if (!config)
      return;

    // 设置默认配置
    UA_StatusCode retval = UA_ClientConfig_setDefault(config);
    if (retval != UA_STATUSCODE_GOOD) {
      std::cerr << "Failed to set default client config" << std::endl;
      return;
    }

    // 超时配置（注意单位：毫秒）
    config->timeout = 10000; // 10秒请求超时

    // 安全通道生命周期（毫秒）
    config->secureChannelLifeTime = 600000; // 10分钟

    // 会话超时（毫秒）- 关键！
    config->requestedSessionTimeout = 600000; // 向服务器请求10分钟超时

    // 连接检查间隔（毫秒）
    config->connectivityCheckInterval = 10000; // 10秒检查一次

    // 重连配置
    config->noReconnect = false;  // 允许自动重连
    config->noNewSession = false; // 允许创建新会话
    config->noSession = false;    // 需要会话

    // 其他配置
    config->outStandingPublishRequests = 1;

  }

Result<bool,RichError> OPCUA_Access::read_variable_from_device(UA_NodeId &nodeID,bool reverse_direction)
{
    std::string tmp = "";
    auto it = (nodeIdToString(tmp, nodeID));
    if(it.is_fail())
    {
        return Result<bool,RichError>(it);
    }
    else
    {
        if(m_visited_set.count(tmp))
        {
            std::cout<<"check reduplicate nodeID : "<<tmp<<std::endl;
            return Result<bool,RichError> (true);
        }
        m_visited_set.insert(tmp);
    }

    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = nodeID;
    if(reverse_direction)
    {
        // SON -> PARENT
        bd.browseDirection = UA_BROWSEDIRECTION_INVERSE; 
    }
    else
    {
        //  PARENT -> SON
        bd.browseDirection = UA_BROWSEDIRECTION_FORWARD; 
    }
    bd.includeSubtypes = true ;
    bd.resultMask = UA_BROWSERESULTMASK_ALL;  // 获取所有信息
    bd.referenceTypeId = UA_NODEID_NULL;

    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.nodesToBrowse = &bd;
    bReq.nodesToBrowseSize = 1;
    bReq.requestedMaxReferencesPerNode = 100;

    UA_BrowseResponse bResp = UA_Client_Service_browse(m_client_pointer,bReq);
    if(bResp.responseHeader.serviceResult != UA_STATUSCODE_GOOD|| 
        bResp.resultsSize != 1||
        bResp.results[0].statusCode != UA_STATUSCODE_GOOD
    )
    {
      std::cerr << "read variable from device  fail :"
                << " bResp.responseHeader.serviceResult : "
                << bResp.responseHeader.serviceResult
                << " bResp.resultsSize  : " << bResp.resultsSize << std::endl;
      UA_BrowseResponse_clear(&bResp);
      return Result<bool, RichError>(
          RichError("OPCUA_Access : read variable from device error"));
    }

    std::cout << "🔍 DEBUG: Browsing node " << tmp << " in "
              << (reverse_direction ? "INVERSE" : "FORWARD")
              << " direction, got " << bResp.results[0].referencesSize
              << " references." << std::endl;

    for(size_t i =0;i<bResp.results[0].referencesSize;++i)
    {
        // _t j=0;j<bResp.results[0].referencesSize;++j)
        {
            UA_ReferenceDescription *ref = &bResp.results[0].references[i];
            UA_NodeId fullNodeId =
                ref->nodeId.nodeId; // ns=3;s="PLC_1" 或 ns=0;i=2255
            UA_UInt16 namespaceIndex = fullNodeId.namespaceIndex; // 0, 2, 3 等
            UA_NodeIdType idType =
                fullNodeId.identifierType; // NUMERIC, STRING 等
            std::string browseName(
                (char *)ref->browseName.name.data,
                ref->browseName.name.length); // "PLC_1", "Server" 等
            UA_NodeClass NodeClass =  ref->nodeClass;
            std::string fullNodeId_str = "";
            nodeIdToString(fullNodeId_str,fullNodeId).unwrap_returnRightValue();
            std::string refTypeStr;
            nodeIdToString(refTypeStr, ref->referenceTypeId);
            std::cout << "Node: " << fullNodeId_str << " (" << browseName << ")"
                      << " namespaceIndex : " << fullNodeId.namespaceIndex
                      << " NodeClass : " << NodeClass
                      << " refTypeStr : " << refTypeStr
                      << " referenceSize : "<< bResp.results[0].referencesSize;

            // 3. 检查是否是HasTypeDefinition引用（这是类型定义，不是实际数据）
            UA_NodeId hasTypeDef =
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASTYPEDEFINITION);
            if (UA_NodeId_equal(&ref->referenceTypeId, &hasTypeDef)) {
              std::cout << std::endl;
              continue;
            }

            UA_NodeId organizes = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId hasComponent =
                UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
            UA_NodeId hasProperty = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
            if (UA_NodeId_equal(&ref->referenceTypeId, &organizes)) {
                std::cout<<" [organizes]";
            } else if (UA_NodeId_equal(&ref->referenceTypeId, &hasComponent)) {
                std::cout<<" [hasComponent]";
            } else if (UA_NodeId_equal(&ref->referenceTypeId, &hasProperty)) {
                std::cout<<" [hasProperty]";
            }

            if(!ref->isForward)
            {
                std::cout<<" isForward: false"<<std::endl;
            }
            else
            {
                std::cout<<" isForward: true"<<std::endl;
            }

            if (ref->nodeClass == UA_NODECLASS_VARIABLE) {
              std::cout << ">>> FOUND VARIABLE: " << browseName << " at "
                        << fullNodeId_str << std::endl;
            }

            {
            //   std::cout << "nodeID:" << fullNodeId_str
            //             << " checked start inverse direction : " << std::endl;
            //   (read_variable_from_device(ref->nodeId.nodeId, true));
            //   std::cout << "nodeID:" << fullNodeId_str
            //             << " checked end inverse direction : " << std::endl;

              std::cout << "nodeID:" << fullNodeId_str
                        << " checked start forward direction : " << std::endl;
              (read_variable_from_device(ref->nodeId.nodeId, false));
              std::cout << "nodeID:" << fullNodeId_str
                        << " checked end forward direction : " << std::endl;
            }
        }
    }
    return Result<bool,RichError> (true);
}

Result<bool, RichError>
OPCUA_Access::batchReadOPCUADataBlock_FromPLC(OPCUADataBlock *data) {
  bool success = true;
  //  CLEAR ELEMEMT EXISTED BEFORE
  this->PrepareBatchRead(data->getVariabeDataVector());

  //  read data from PLC
  return (this->Read_UA_Variant_From_PLC());
}

Result<bool, RichError>
OPCUA_Access::batchWriteOPCUABlock_ToPLC(OPCUADataBlock *data) {
  auto result = this->ensureConnection();
  if (result.is_fail()) {
    return Result<bool, RichError>(result);
  }

  auto writeResult = this->batchWrite();
 
  return Result<bool, RichError>(writeResult);
}

Result<bool,RichError> OPCUA_Access::expandNodeIdToString( UA_ExpandedNodeId &id)
{
    UA_String tmp;
    UA_String_init(&tmp);
    UA_StatusCode it = UA_NodeId_print(&id.nodeId, &tmp);
    if(it == UA_STATUSCODE_GOOD)
    {
        std::string nodeIdStr((char*)tmp.data,tmp.length);
        std::cout<<"NodeID: "<<nodeIdStr<<std::endl;
        UA_String_clear(&tmp);
        return Result<bool,RichError> (true);
    }
    else
    {
        return Result<bool,RichError> (RichError("NodeID get fail "));
    }
}


Result<bool, RichError> OPCUA_Access::batchWrite() {
  // 2. 准备写入请求
  UA_WriteRequest request;
  UA_WriteRequest_init(&request);
  
  // 分配写入节点数组（需要预先准备好要写入的节点和值）
  request.nodesToWrite = m_batchWriteNodes.data();  // 假设有成员变量 m_batchWriteNodes
  request.nodesToWriteSize = m_batchWriteNodes.size();
  
  // 3. 执行批量写入
  m_write_response = UA_Client_Service_write(m_client_pointer, request);
  
  // 4. 检查写入结果
  if (m_write_response.responseHeader.serviceResult != UA_STATUSCODE_GOOD ||
      m_write_response.resultsSize == 0) {
    std::stringstream ss;
    ss << "write fail :"
       << " m_write_response.responseHeader.serviceResult : "
       << m_write_response.responseHeader.serviceResult
       << " m_write_response.resultsSize : " << m_write_response.resultsSize;
    
    // 检查每个节点的写入结果
    for (size_t i = 0; i < m_write_response.resultsSize; ++i) {
      if (m_write_response.results[i] != UA_STATUSCODE_GOOD) {
        ss << " node[" << i << "] result: " << m_write_response.results[i];
      }
    }

    
    // 清理响应
    Clear_Write_Respondse();  
    return Result<bool, RichError>(RichError(ss.str()));
  }

  // 清理响应
  Clear_Write_Respondse();
  return Result<bool,RichError> (true);
}

template<typename T>
Result<bool,RichError> OPCUA_Access::write_by_vector(OPCUAModernDataStruct &SourceData_var,std::vector<T> &var)
{
    //  INITIAZLIE UA_WRTITE_VALUE
    if(this->Set_UA_Array_StatusCode(3, SourceData_var,var).is_fail())
    {
        return Result<bool,RichError> (RichError("Set UA Array StatusCode fail"));
    }
    //  INIT UA_WRITE_REQUEST
    UA_WriteRequest request;
    UA_WriteRequest_init(&request);
    request.nodesToWrite = &m_writeValue;
    request.nodesToWriteSize = 1;

    //  TRY WRITE UA_VALUE INTO DEVICE
    UA_WriteResponse write_response_status =
        UA_Client_Service_write(m_client_pointer, request);
    if (write_response_status.responseHeader.serviceResult !=
            UA_STATUSCODE_GOOD ||
        write_response_status.resultsSize != 1 ||
        &write_response_status.results[0] != nullptr &&
            write_response_status.results[0] != UA_STATUSCODE_GOOD) {
      std::stringstream ss;
      ss << "write vector value fail :"
         << " write_response_status.responseHeader.serviceResult : "
         << write_response_status.responseHeader.serviceResult
         << " read_response.resultsSize : " << write_response_status.resultsSize;
      
      UA_WriteValue_clear(&m_writeValue);
      UA_WriteResponse_clear(&write_response_status);
      return Result<bool, RichError>(RichError(ss.str()));
    }

    UA_WriteValue_clear(&m_writeValue);
    UA_WriteResponse_clear(&write_response_status);
    return Result<bool,RichError> (true);
}

template<typename T>
Result<bool,RichError> OPCUA_Access::write_by_scalar(OPCUAModernDataStruct &SourceData_var,T &var)
{
    //  INITIAZLIE UA_WRTITE_VALUE
    if (this->Set_UA_Scalar_StatusCode(3, SourceData_var, var).is_fail()) {
      return Result<bool, RichError>(RichError("Set UA Array StatusCode fail"));
    }
    //  INIT UA_WRITE_REQUEST
    UA_WriteRequest request;
    UA_WriteRequest_init(&request);
    request.nodesToWrite = &m_writeValue;
    request.nodesToWriteSize = 1;

    //  TRY WRITE UA_VALUE INTO DEVICE
    UA_WriteResponse write_response_status =
        UA_Client_Service_write(m_client_pointer, request);

    if (write_response_status.responseHeader.serviceResult !=
            UA_STATUSCODE_GOOD ||
        write_response_status.resultsSize != 1 ||
        &write_response_status.results[0] != nullptr &&
        write_response_status.results[0] != UA_STATUSCODE_GOOD
    ) {
      std::stringstream ss;
      ss << "write scalar value fail  :"
         << " write_response_status.responseHeader.serviceResult : "
         << write_response_status.responseHeader.serviceResult
         << " write_response.resultsSize : "
         << write_response_status.resultsSize;

      UA_WriteValue_clear(&m_writeValue);
      UA_WriteResponse_clear(&write_response_status);
      return Result<bool, RichError>(RichError(ss.str()));
    }

    UA_WriteValue_clear(&m_writeValue);
    UA_WriteResponse_clear(&write_response_status);
    return Result<bool,RichError> (true);
}


Result<bool,RichError> OPCUA_Access::ByteDeserialization_To_SpecialType(
         int data_offset, int data_length, S7DataType &s7_type,
         std::vector<uint8_t> &m_data_block_buffer,Dynamic_Value &value)
{
    if( s7_type == S7DataType::BOOL)
    {
        bool tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::BYTE)
    {
        uint8_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::INT)
    {
        int16_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::WORD)
    {
        uint16_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::DWORD || s7_type == S7DataType::UDINT)
    {
        uint32_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::DINT)
    {
        int32_t tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::REAL)
    {
        float tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else if( s7_type == S7DataType::STRING)
    {
        std::string tmp;
        {
            ByteDeserialization_memcpy(tmp, m_data_block_buffer,data_offset,data_length);
            value.Reset_Value(std::move(tmp));
        }
    }
    else
    {
        return Result<bool,RichError> (RichError("error in ByteDeserialization"));
    }

    return Result<bool, RichError>(true);
}