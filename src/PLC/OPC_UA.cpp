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
    if(!check_result)
    {
        return Result<bool, RichError>(RichError{"connect status is error "});
    }

    int result =
        Cli_ConnectTo(m_client_var, m_ip_Address.data(), m_rack, m_slot);
    if (result != 0) {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        spdlog::error("Connection failed: {} (Error code: {})", error_text,
                      result);
        return Result<bool, RichError>(RichError("connect fail"));
    } else {
        spdlog::info("Connection successful");
    }
    return Result<bool, RichError>(result);
}

Result<bool, RichError> S7_Access::reconnect(int max_retries,
                                             int retryDelayMs) {
  disconnect();
  if (!m_client_var) {
    m_client_var = Cli_Create();
  }

  for (int attempt = 1; attempt <= max_retries; ++attempt) {
    spdlog::info("Reconnection attempt {}/{}", attempt, max_retries);

    auto result = connect();

    if (result.is_success()) {
      spdlog::info("Reconnection successful");
      return Result<bool, RichError>(true);
    } else {
      spdlog::warn("Session activation timeout");
      // 会话激活失败，继续重试
      disconnect();
      if (!m_client_var) {
        m_client_var = Cli_Create();
      }
    }

    // 最后一次尝试失败后不再等待
    if (attempt < max_retries) {
      std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
    }
  }

  return Result<bool, RichError>(RichError("Reconnection failed after " +
                                           std::to_string(max_retries) +
                                           " attempts"));
}

bool S7_Access::isConnected()
{
    int cpu_state;
    int result = Cli_GetPlcStatus(m_client_var, &cpu_state);
    if (result == 0) {
      spdlog::debug("CPU is in state {}", cpu_state);
      if (cpu_state == S7CpuStatusRun) {
        spdlog::debug("CPU is running");
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
    spdlog::info("PLC_Device disconnect");
}

S7Object& S7_Access::getClient()
{
    return this->m_client_var;
}

Result<bool, RichError>
S7_Access::batchReadS7DataBlock_FromPLC(OPCUADataBlock *data) {
  bool success = true;
  Destbuffer.clear();
  Destbuffer.resize(10000);
  for (auto &var : data->getVariabeDataVector()) {
    if (var.data_type_enum == S7DataType::UNKNOWN) {
      continue;
    }
    Sourcebuffer.clear();
    Sourcebuffer.resize(var.s7_data_type_length);

    {
      auto result = this->read(1, var.bytes_offset, var.s7_data_type_length,
                               Sourcebuffer.data());
      if (result.is_fail()) {
        spdlog::error("Read action is fail");
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
          if(var.data_type_enum == S7DataType::UNKNOWN)
          {
            continue;
          }
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
            	spdlog::info("Send successful");
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
      if(reconnect(5, 2000).is_fail())
      {
        return Result<bool, RichError>(false);
      }
    }

    int result = Cli_DBRead(m_client_var,DB_Number, Start_Position, Read_Size,SourceData_var);
    if (result != 0)
    {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        spdlog::error("Read failed: {} (Error code: {})", error_text, result);
        return Result<bool, RichError>(RichError{error_text});
    }
    return Result<bool,RichError> (true);
}

Result<int, RichError> S7_Access::meastureStringObjectLength(int startPos,OPCUAModernDataStruct &var) {
  int stringLength = 1;
  {
    Sourcebuffer.clear();
    Sourcebuffer.resize(500);
    {
      auto result = this->read(1, startPos, stringLength, Sourcebuffer.data());
      if (result.is_fail()) {
        return Result<int, RichError>(result.unwrap_err());
      } 
    }
  }
  int16_t value = 0;
  std::memcpy(&value, Sourcebuffer.data(),
              sizeof(int16_t));
  return Result<int, RichError>(value);
}

 Result<bool,RichError> S7_Access::write(int DB_Number,int Start_Position,int Read_Size,uint8_t *SourceData_var) 
{
    int result = Cli_DBWrite(m_client_var,DB_Number, Start_Position, Read_Size,SourceData_var);
    if (result != 0)
    {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        spdlog::error("Read failed: {} (Error code: {})", error_text, result);
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
    auto result{extractPureNodeIdRobust(data_var.variable_nodeID)};
    nodeid.nodeId =
        UA_NODEID_STRING_ALLOC(std::stoi(result.first), result.second.data());
  }

  nodeid.indexRange = UA_STRING_NULL;
  nodeid.attributeId = UA_ATTRIBUTEID_VALUE;
}

std::pair<std::string, std::string> OPCUA_Access::extractPureNodeIdRobust(const std::string &input) {
  {
    // 正则表达式结构：
    // 匹配部分: ^ns=    [0-9]+    ;s=
    // 读取部分:         ([0-9]+)       (.*)
    //         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    //         描述特征             描述内容
    std::regex pattern("^ns=([0-9]+);s=(.*)");
    std::smatch match;
    if (std::regex_search(input, match, pattern)) {
      // match[1] = "3"
      // match[2] = "DB111_EdgeGatewayTest" (不包含引号)
      return {match[1].str(), match[2].str()};
    }
  }
  return {"", input};
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
      spdlog::info("Reconnection attempt {}/{}", attempt, maxRetries);

      UA_StatusCode result =
          UA_Client_connect(m_client_pointer, endpointUrl.c_str());

      if (result == UA_STATUSCODE_GOOD) {
        if (waitForSessionActivation(5000).is_success()) {
          spdlog::info("Reconnection successful");
          return Result<bool, RichError>(true);
        } else {
          spdlog::warn("Session activation timeout");
          // 会话激活失败，继续重试
          UA_Client_disconnect(m_client_pointer);
        }
      }

      // 连接失败，记录错误
      char error_text[256];
      Cli_ErrorText(result, error_text, sizeof(error_text));
      spdlog::error("Reconnection attempt {} failed: {} (code: {})", attempt,
                    error_text, result);

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
      spdlog::error("Failed to set default client config");
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

