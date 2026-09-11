#include "PLC/OPC_UA.h"
#include <spdlog/spdlog.h>
#include <regex>
#include "PLC/ConversionDispatcher.h"


//  EndianConverter----------------------------------------------------------------------------
static Result<bool, RichError>
S7BigEndianToLittleEndian(std::vector<uint8_t> &Sourcebuffer,
                          std::vector<uint8_t> &Destbuffer,
                          const PhysicalAddress &var) {
  switch (var.dataType) {

  case S7DataType::BOOL: {
    if (Sourcebuffer[0] & (1 << var.bitOffset)) {
      Destbuffer[0] |= 1 << var.bitOffset;
    } else {
      Destbuffer[0] &= ~(1 << var.bitOffset);
    }
    break;
  }
  case S7DataType::BYTE:
    Destbuffer[0] = Sourcebuffer[0];
    break;
  case S7DataType::INT:
    Destbuffer[1] = Sourcebuffer[0];
    Destbuffer[0] = Sourcebuffer[1];
    break;
  case S7DataType::WORD:
    Destbuffer[1] = Sourcebuffer[0];
    Destbuffer[0] = Sourcebuffer[1];
    break;
  case S7DataType::DWORD:
    Destbuffer[3] = Sourcebuffer[0];
    Destbuffer[2] = Sourcebuffer[1];
    Destbuffer[1] = Sourcebuffer[2];
    Destbuffer[0] = Sourcebuffer[3];
    break;
  case S7DataType::UDINT:
    Destbuffer[3] = Sourcebuffer[0];
    Destbuffer[2] = Sourcebuffer[1];
    Destbuffer[1] = Sourcebuffer[2];
    Destbuffer[0] = Sourcebuffer[3];
    break;
  case S7DataType::DINT:
    Destbuffer[3] = Sourcebuffer[0];
    Destbuffer[2] = Sourcebuffer[1];
    Destbuffer[1] = Sourcebuffer[2];
    Destbuffer[0] = Sourcebuffer[3];
    break;
  case S7DataType::REAL:
    Destbuffer[3] = Sourcebuffer[0];
    Destbuffer[2] = Sourcebuffer[1];
    Destbuffer[1] = Sourcebuffer[2];
    Destbuffer[0] = Sourcebuffer[3];
    break;
  case S7DataType::STRING: {
    int effective_string_length = std::min(Sourcebuffer[0], Sourcebuffer[1]);
    Destbuffer[0] = Sourcebuffer[0];
    Destbuffer[1] = Sourcebuffer[1];
    if(Sourcebuffer.size()>2)
    {
      memcpy(&Destbuffer[2], &Sourcebuffer[2],
             effective_string_length);
    }
    break;
  }
  default:
    return Result<bool, RichError>::error(
        RichError("variable not found by variablePath"));
  }
  return Result<bool, RichError>::success(true);
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
    return Result<bool, RichError>::error(
        RichError("variable not found by variablePath"));
  }
  return Result<bool, RichError>::success(true);
}

//S7_Client-----------------------------------------------------------------
S7_Client::S7_Client(const std::string ip_Address, int rack, int slot,
                     const S7Object &object)
    : m_ip_Address(ip_Address), m_rack(rack), m_slot(slot),
      m_client_var(object) {}

S7_Client::~S7_Client() noexcept {
  std::lock_guard<std::recursive_mutex> lock_guard(lock);
  cleanupClient();
}

// 清理客户端资源
void S7_Client::cleanupClient() {
  if (m_client_var) {
    Cli_Destroy(&m_client_var);
  }
}

bool S7_Client::isConnectedInternal() const {
  // 1. 快速检查
  if (!m_client_var) {
    return (false);
  }

  // 2. 获取实际状态
  int cpu_state = 0;
  int result = Cli_GetPlcStatus(m_client_var, &cpu_state);

  if (result != 0) {
    // 连接断开
    spdlog::debug("PLC connection check failed: {}", result);
    return (false);
  }
  return (cpu_state == S7CpuStatusRun);
}

Result<bool, RichError> S7_Client::connect() {
  std::lock_guard<std::recursive_mutex> lock_guard(lock);

  // 1. 检查是否已连接
  if (m_client_var) {
    auto stateResult = isConnectedInternal();
    if (stateResult) {
      spdlog::info("Already connected to OPC UA server");
      return Result<bool, RichError>::success(true);
    }
  }

  // 2. 检查客户端指针
  if (!m_client_var) {
    m_client_var = Cli_Create();
    if (!m_client_var) {
      return Result<bool, RichError>::error(
          RichError("Failed to create OPC UA client"));
    }
  }

  // 4. 尝试连接
  int result =
      Cli_ConnectTo(m_client_var, m_ip_Address.c_str(), m_rack, m_slot);

  if (result != 0) {
    char error_text[256] = {0};
    Cli_ErrorText(result, error_text, sizeof(error_text));
    std::string errorMsg = "Connection failed: " + std::string(error_text) +
                           " (Error code: " + std::to_string(result) + ")";
    return Result<bool, RichError>::error(RichError(errorMsg));
  }

  // 4. 验证连接状态
  auto statusResult = isConnectedInternal();
  if (statusResult) {
    spdlog::info("Successfully connected to PLC");
    return Result<bool, RichError>::success(true);
  } else {
    // 5. 连接建立但状态异常
    Cli_Disconnect(m_client_var);
    return Result<bool, RichError>::error(
        RichError("Connection established but PLC status check failed"));
  }
}

bool S7_Client::isConnected() {
  std::lock_guard<std::recursive_mutex> lock_guard(lock);
  return isConnectedInternal();
}

S7_Client::S7_Client(S7_Client &&other) noexcept
    : m_ip_Address(other.m_ip_Address), m_rack(other.m_rack), m_slot(other.m_slot),
      m_client_var(other.m_client_var) {

  other.m_client_var = {};
  other.m_slot = 0;
  other.m_rack = 0;
  other.m_ip_Address = "";
  // 不需要操作锁，新对象的 lock 会默认初始化，源对象的 lock 保持不变
}

S7_Client &S7_Client::operator=(S7_Client &&other) noexcept {
  std::lock_guard<std::recursive_mutex> lock_guard(lock);
  if (&other == this) {
    return *this;
  } else {
    if (m_client_var) {
      cleanupClient();
    }

    m_ip_Address = (other.m_ip_Address);
    m_rack = other.m_rack;
    m_slot = other.m_slot;
    m_client_var = other.m_client_var;

    other.m_client_var = {};
    other.m_slot = 0;
    other.m_rack = 0;
    other.m_ip_Address = "";
    // 不需要操作锁，新对象的 lock 会默认初始化，源对象的 lock 保持不变
  }
  return *this;
}



Result<Unit, RichError> S7_Client::batchRead(int DB_Number, int Start_Position,
                                             int Read_Size,
                                             uint8_t *SourceData_var) const {

  std::lock_guard<std::recursive_mutex> lock_guard(lock);
  int result = Cli_DBRead(m_client_var, DB_Number, Start_Position, Read_Size,
                          SourceData_var);
  if (result != 0) {
    char error_text[256];
    Cli_ErrorText(result, error_text, sizeof(error_text));
    spdlog::error("Read failed: {} (Error code: {})", error_text, result);
    return Result<Unit, RichError>::error(RichError{error_text});
  }
  return Result<Unit, RichError>::success(Unit{});
}

Result<Unit, RichError> S7_Client::batchWrite(int DB_Number, int Start_Position,
                                              int Read_Size,
                                              uint8_t *SourceData_var) {
  std::lock_guard<std::recursive_mutex> lock_guard(lock);
  int result = Cli_DBWrite(m_client_var, DB_Number, Start_Position, Read_Size,
                           SourceData_var);
  if (result != 0) {
    char error_text[256];
    Cli_ErrorText(result, error_text, sizeof(error_text));
    spdlog::error("Write failed: {} (Error code: {})", error_text, result);
    return Result<Unit, RichError>::error(RichError{error_text});
  }
  return Result<Unit, RichError>::success(Unit{});
}

// S7_Access------------------------------------------------------------------
void S7_Access::setAddressMap(
    std::unordered_map<std::string, PhysicalAddress> &map) {
  m_addressMap = std::move(map);
}

// ============ 连接 ============
Result<bool, RichError> S7_Access::connect() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. 检查是否已连接
    if (m_isConnected && m_client_var) {
        auto statusResult = isConnectedInternal();
        if (statusResult.is_success()) {
            spdlog::info("Already connected to PLC");
            return Result<bool, RichError>::success(true);
        }
        // 状态不一致，标记为未连接
        m_isConnected = false;
    }

    // 2. 检查客户端指针
    if (!m_client_var) {
        m_client_var = Cli_Create();
        if (!m_client_var) {
            return Result<bool, RichError>::error(
                RichError("Failed to create S7 client"));
        }
    }

    // 3. 尝试连接
    spdlog::info("Connecting to PLC at {} (Rack: {}, Slot: {})", 
                 m_ip_Address, m_rack, m_slot);
    
    int result = Cli_ConnectTo(m_client_var, 
                               m_ip_Address.c_str(), 
                               m_rack, 
                               m_slot);

    if (result != 0) {
        char error_text[256] = {0};
        Cli_ErrorText(result, error_text, sizeof(error_text));
        std::string errorMsg = "Connection failed: " + std::string(error_text) +
                               " (Error code: " + std::to_string(result) + ")";
        return Result<bool, RichError>::error(RichError(errorMsg));
    }

    // 4. 验证连接状态
    auto statusResult = isConnectedInternal();
    if (statusResult.is_success() ) {
        m_isConnected = true;
        spdlog::info("Successfully connected to PLC");
        return Result<bool, RichError>::success(true);
    }

    // 5. 连接建立但状态异常
    Cli_Disconnect(m_client_var);
    return Result<bool, RichError>::error(
        RichError("Connection established but PLC status check failed"));
}

// ============ 断开连接 ============
Result<bool, RichError> S7_Access::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. 如果已经断开，直接返回
    if (!m_isConnected || !m_client_var) {
        spdlog::info("Already disconnected from PLC");
        return Result<bool, RichError>::success(true);
    }

    // 2. 断开连接
    spdlog::info("Disconnecting from PLC");
    Cli_Disconnect(m_client_var);
    
    // 3. 清理状态
    m_isConnected = false;
    
    spdlog::info("Disconnected from PLC");
    return Result<bool, RichError>::success(true);
}

// ============ 重连 ============
Result<bool, RichError> S7_Access::reconnect(int maxRetries, int retryDelayMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. 先断开当前连接
    if (m_isConnected && m_client_var) {
        Cli_Disconnect(m_client_var);
        m_isConnected = false;
    }

    // 2. 清理并重建客户端
    cleanupClient();
    m_client_var = Cli_Create();
    if (!m_client_var) {
        return Result<bool, RichError>::error(
            RichError("Failed to create S7 client for reconnection"));
    }

    // 3. 重连循环
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        spdlog::info("Reconnection attempt {}/{}", attempt, maxRetries);

        // 尝试连接
        int result = Cli_ConnectTo(m_client_var, 
                                   m_ip_Address.c_str(), 
                                   m_rack, 
                                   m_slot);

        if (result == 0) {
            // 验证连接状态
            auto statusResult = isConnectedInternal();
            if (statusResult.is_success() ) {
                m_isConnected = true;
                spdlog::info("Reconnection successful");
                return Result<bool, RichError>::success(true);
            }
            
            // 连接建立但状态异常
            spdlog::warn("Reconnection established but PLC status check failed");
            Cli_Disconnect(m_client_var);
            
            // 清理并重建客户端以备下次尝试
            cleanupClient();
            m_client_var = Cli_Create();
            if (!m_client_var) {
                return Result<bool, RichError>::error(
                    RichError("Failed to recreate S7 client"));
            }
        }

        // 连接失败
        char error_text[256] = {0};
        Cli_ErrorText(result, error_text, sizeof(error_text));
        spdlog::error("Reconnection attempt {} failed: {} (code: {})", 
                      attempt, error_text, result);

        // 如果不是最后一次尝试，等待后重试
        if (attempt < maxRetries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
            // 清理并重建客户端以备下次尝试
            cleanupClient();
            m_client_var = Cli_Create();
            if (!m_client_var) {
                return Result<bool, RichError>::error(
                    RichError("Failed to recreate S7 client"));
            }
        }
    }

    return Result<bool, RichError>::error(
        RichError("Reconnection failed after " + std::to_string(maxRetries) + 
                  " attempts"));
}

// ============ 检查连接状态（公开接口） ============
Result<bool, RichError> S7_Access::isConnected()  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return isConnectedInternal();
}

// ============ 检查连接状态（内部实现） ============
Result<bool, RichError> S7_Access::isConnectedInternal() const {
    // 1. 快速检查
    if (!m_client_var || !m_isConnected) {
        return Result<bool, RichError>::success(false);
    }

    // 2. 检查 PLC 状态（轻量级检查）
    int cpu_state = 0;
    int result = Cli_GetPlcStatus(m_client_var, &cpu_state);
    
    if (result != 0) {
        // 连接断开
        const_cast<S7_Access*>(this)->m_isConnected = false;
        spdlog::debug("PLC connection check failed: {}", result);
        return Result<bool, RichError>::success(false);
    }

    // 3. 检查 CPU 状态
    bool isRunning = (cpu_state == S7CpuStatusRun);
    spdlog::debug("PLC status: {}", cpu_state);
    
    if (!isRunning) {
        spdlog::warn("PLC is not in RUN state (state: {})", cpu_state);
    }

    return Result<bool, RichError>::success(std::move(isRunning));
}

// ============ 清理客户端 ============
void S7_Access::cleanupClient() {
    if (m_client_var) {
        Cli_Destroy(&m_client_var);
    }
    m_isConnected = false;
}

Result<std::unordered_map<std::string, ValueType>, RichError>
S7_Access::batchRead(std::vector<std::string> &requestVec) {
  bool success = true;
  Destbuffer.clear();
  Destbuffer.resize(10000);
  std::unordered_map<std::string, ValueType> resultMap;
  for (auto &requestName : requestVec) {
    auto it= m_addressMap.find(requestName);
    if (it == m_addressMap.end()) {
      return Result<std::unordered_map<std::string, ValueType>, RichError>::error(
          RichError{"request do not map"});
    }
    auto &var = *it;
    if (var.second.dataType == S7DataType::UNKNOWN) {
      continue;
    }
    Sourcebuffer.clear();
    Sourcebuffer.resize(var.second.data_type_length);
    Destbuffer.clear();
    Destbuffer.resize(var.second.data_type_length);

    {
      auto result = this->read(1, var.second.byteOffset, var.second.data_type_length,
                               Sourcebuffer.data());
      if (result.is_fail()) {
        spdlog::error("Read action is fail");
        return Result<std::unordered_map<std::string, ValueType>, RichError>(ErrorTag{},*result.get_error());
        success = false;
      } else {
        ValueType value;
        success = true;
        auto result = TransformBytesToDataType(Sourcebuffer, Destbuffer, var.second,value);
        resultMap[var.second.nodeId] = std::move(value);
        success = success && result.is_success();
      }
    }
  }
  if (success) {
    return Result<std::unordered_map<std::string, ValueType>, RichError>::success(std::move(resultMap));
  } else {
    return Result<std::unordered_map<std::string, ValueType>, RichError>::error(RichError("read variable failed"));
  }
}

Result<bool, RichError>
S7_Access::batchWrite(std::vector<WriteRequest> &requestVec) {
bool success = true;
 
  for (auto &request : requestVec) {
    auto it = m_addressMap.find(request.tagName);
    if (it == m_addressMap.end()) {
      return Result<bool, RichError>::error(RichError{"request do not map"});
    }
    auto &var = *it;
    {
      if (var.second.dataType == S7DataType::UNKNOWN) {
        continue;
      }
      tmpBuffer.clear();
      tmpBuffer.resize(var.second.data_type_length);
      TransformDataTypeToBytes(request.value, tmpBuffer, var.second);

      //  single write condition result
      auto result = this->write(1, var.second.byteOffset,
                                var.second.data_type_length, tmpBuffer.data());
      if (result.is_fail()) {
        return Result<bool, RichError>(std::move(result));
      } else {
        success = true;
        spdlog::info("Send successful");
      }
    }
  }

  //  check total write condition result
  if (success) {
    return Result<bool, RichError>::success(true);
  } else {
    return Result<bool, RichError>::error(RichError("read variable failed"));
  }
  //  reader is nullptr
  return Result<bool, RichError>::error(RichError("S7Acess is nullptr"));
}

std::optional<int>
S7_Access::probeStringLength(int byteOffset)  {
  // 只有在这里才真正调用 S7 SDK 去读长度
  auto result = meastureStringObjectLength(byteOffset).value_or(-1);
  if(result != -1)
  {
    return result;
  }
  else
  {
    return -1;
  }
}

Result<bool,RichError> S7_Access::read(int DB_Number,int Start_Position,int Read_Size,uint8_t *SourceData_var) 
{
    auto  connect_check = this->isConnected();
    if (connect_check.is_fail()) {
      if(reconnect(10, 1000).is_fail())
      {
        return Result<bool, RichError>::success(false);
      }
    }

    int result = Cli_DBRead(m_client_var,DB_Number, Start_Position, Read_Size,SourceData_var);
    if (result != 0)
    {
        char error_text[256];
        Cli_ErrorText(result, error_text, sizeof(error_text));
        spdlog::error("Read failed: {} (Error code: {})", error_text, result);
        return Result<bool, RichError>::error(RichError{error_text});
    }
    return Result<bool,RichError> ::success(true);
}

Result<int, RichError> S7_Access::meastureStringObjectLength(int startPos) {
  int stringLength = 1;
  {
    Sourcebuffer.clear();
    Sourcebuffer.resize(500);
    {
      auto result = this->read(1, startPos, stringLength, Sourcebuffer.data());
      if (result.is_fail()) {
        return Result<int, RichError>::error(std::move(*result.get_error()));
      } 
    }
  }
  int16_t value = 0;
  std::memcpy(&value, Sourcebuffer.data(),
              sizeof(int16_t));
  return Result<int, RichError>::success(value);
}

Result<bool, RichError>
S7_Access::TransformBytesToDataType(std::vector<uint8_t> &SrcBuffer,std::vector<uint8_t> &destBuffer,
                                                   PhysicalAddress &dataQuality,
                                                   ValueType &dataVar) { // 添加 dataVar 参数

  S7BigEndianToLittleEndian(SrcBuffer, destBuffer, dataQuality);

  // 添加参数有效性检查
  if (destBuffer.empty()) {
    return Result<bool, RichError>::error(RichError("bytes vector is empty"));
  }

  // 检查对象是否有效
  if (dataQuality.dataType == S7DataType::UNKNOWN) {
    return Result<bool, RichError>::error(RichError("object type is UNKNOWN"));
  }

  int bitOffset = dataQuality.bitOffset;

  switch (dataQuality.dataType) {
  case S7DataType::BOOL: {
    auto tmp_byte = destBuffer[0];
    bool value = (tmp_byte & (1 << bitOffset)) != 0;
    dataVar = value;
    break;
  }

  case S7DataType::BYTE: {
    uint8_t value = destBuffer[0];
    dataVar = value;
    break;
  }

  case S7DataType::INT: {
    int16_t value = 0;
    std::memcpy(&value, destBuffer.data() , sizeof(int16_t));
    // 从小端序转换（S7 PLC 使用大端序，但这里按小端序读取，与写入保持一致）
    // 如果需要转换字节序，在这里处理
    dataVar = value;
    break;
  }

  case S7DataType::DINT: {
    int32_t value = 0;
    std::memcpy(&value, destBuffer.data() , sizeof(int32_t));
    dataVar = value;
    break;
  }

  case S7DataType::REAL: {
    float value = 0;
    std::memcpy(&value, destBuffer.data() , sizeof(float));
    dataVar = value;
    break;
  }

  case S7DataType::WORD: {
    uint16_t value = 0;
    std::memcpy(&value, destBuffer.data() , sizeof(uint16_t));
    dataVar = value;
    break;
  }

  case S7DataType::UDINT: {
    uint32_t value = 0;
    std::memcpy(&value, destBuffer.data() , sizeof(uint32_t));
    dataVar = value;
    break;
  }

  case S7DataType::DWORD: {
    uint32_t value = 0;
    std::memcpy(&value, destBuffer.data() , sizeof(uint32_t));
    dataVar = value;
    break;
  }

  case S7DataType::STRING: {
    // 使用 PhysicalAddress 中的 data_type_length 作为最大长度
    int stringMaxLength = dataQuality.data_type_length - 2;

    // 检查 STRING 格式：前两个字节是长度信息
    uint8_t max_length = destBuffer[0];        // 最大长度
    uint8_t actual_length = destBuffer[1]; // 实际长度

    // 验证长度：实际长度不应超过最大长度
    if (actual_length > max_length) {
      return Result<bool, RichError>::error(RichError("STRING: invalid length"));
    }

    // 验证长度是否与 PhysicalAddress 中记录的一致
    if (stringMaxLength > 0 &&
        max_length > static_cast<uint8_t>(stringMaxLength)) {
      spdlog::warn(
          "STRING: max_length mismatch, PhysicalAddress: {}, actual: {}",
          stringMaxLength, max_length);
      // 不返回错误，继续处理
    }

    // 检查数据是否足够
    if (actual_length > destBuffer.size()) {
      return Result<bool, RichError>::error(
          RichError("STRING: insufficient data for content"));
    }

    // 使用实际长度
    std::string value(reinterpret_cast<const char *>(destBuffer.data()+ 2),
                      actual_length);
    dataVar = value;
    break;
  }

  default: {
    // 未知类型
    return Result<bool, RichError>::error(RichError(
        "Unsupported data type: " + std::to_string(static_cast<int>(dataQuality.dataType))));
  }
  }

  // 所有分支成功执行
  return Result<bool, RichError>::success(true);
}

void S7_Access::TransformDataTypeToBytes(
    ValueType &VariableItem, std::vector<uint8_t> &dataBuffer,
    PhysicalAddress &dataQuality) {
  switch (dataQuality.dataType) {
  case S7DataType::BOOL: {
    bool boolValue;
    if (!tryGetVariantValue(VariableItem, boolValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return;
    }
    if (boolValue) {
      dataBuffer[0] |= 1 << dataQuality.bitOffset;
    } else {
      dataBuffer[0] &= ~(1 << dataQuality.bitOffset);
    }
    break;
  }

  case S7DataType::BYTE: {
    uint8_t byteValue;
    if (!tryGetVariantValue(VariableItem, byteValue)) {
      spdlog::warn("Variant type mismatch for BYTE");
      return;
    }
    // BYTE 范围：0-255，uint8_t 天然在这个范围内
    ByteOrderConverter::toBigEndian(byteValue,
                                       &dataBuffer[0], 1);
    break;
  }

  case S7DataType::INT: {
    int16_t intValue;
    if (!tryGetVariantValue(VariableItem, intValue)) {
      spdlog::warn("Variant type mismatch for INT");
      return;
    }
    // INT 范围：-32768 到 32767，int16_t 天然在这个范围内
    ByteOrderConverter::toBigEndian(intValue,
                                       &dataBuffer[0], 2);
    break;
  }

  case S7DataType::DINT: {
    int32_t intValue;
    if (!tryGetVariantValue(VariableItem, intValue)) {
      spdlog::warn("Variant type mismatch for DINT");
      return;
    }
    // DINT 范围：-2147483648 到 2147483647，int32_t 天然在这个范围内
    ByteOrderConverter::toBigEndian(intValue,
                                       &dataBuffer[0], 4);
    break;
  }

  case S7DataType::WORD: {
    uint16_t wordValue;
    if (!tryGetVariantValue(VariableItem, wordValue)) {
      spdlog::warn("Variant type mismatch for WORD");
      return;
    }
    // WORD 范围：0-65535，uint16_t 天然在这个范围内
    ByteOrderConverter::toBigEndian(wordValue,
                                       &dataBuffer[0], 2);
    break;
  }

  case S7DataType::DWORD: {
    uint32_t dwordValue;
    if (!tryGetVariantValue(VariableItem, dwordValue)) {
      spdlog::warn("Variant type mismatch for DWORD");
      return;
    }
    // DWORD 范围：0-4294967295，uint32_t 天然在这个范围内
    ByteOrderConverter::toBigEndian(dwordValue,
                                       &dataBuffer[0], 4);
    break;
  }

  case S7DataType::UDINT: {
    uint32_t udintValue;
    if (!tryGetVariantValue(VariableItem, udintValue)) {
      spdlog::warn("Variant type mismatch for UDINT");
      return;
    }
    // UDINT 范围：0-4294967295，uint32_t 天然在这个范围内
    ByteOrderConverter::toBigEndian(udintValue,
                                       &dataBuffer[0], 4);
    break;
  }

  case S7DataType::REAL: {
    float floatValue;
    if (!tryGetVariantValue(VariableItem, floatValue)) {
      spdlog::warn("Variant type mismatch for REAL");
      return;
    }
    uint32_t tmpBuffer;
    memcpy(&tmpBuffer, &floatValue, 4);
    ByteOrderConverter::toBigEndian(tmpBuffer,
                                       &dataBuffer[0], 4);
    break;
  }

  case S7DataType::STRING: {
    std::string stringValue;
    if (!tryGetVariantValue(VariableItem, stringValue)) {
      spdlog::warn("Variant type mismatch for STRING");
      return;
    }

    // S7 STRING 格式：前两个字节分别是最大长度和实际长度
    dataBuffer[0] = dataQuality.data_type_length-2;
    dataBuffer[1] = static_cast<uint8_t>(
        std::min(stringValue.size(),
                 static_cast<size_t>(255))); // S7 STRING 最大 255 字符

    // 清空目标区域
    int stringMaxLength = dataBuffer[0];
    if (stringMaxLength > 2) {
      std::fill(dataBuffer.begin() + 2,
                dataBuffer.begin() + 2 + stringMaxLength, 0);
    }

    // 复制字符串内容（不包含终止符）
    size_t copyLength =
        std::min(stringValue.size(), static_cast<size_t>(stringMaxLength));
    if (copyLength > 0) {
      memcpy(&dataBuffer[2], stringValue.c_str(),
             copyLength);
    }
    break;
  }

  default: {
    spdlog::debug("Unknown S7 data type encountered in "
                  "TransformDataTypeToBytes: {}",
                  static_cast<int>(dataQuality.dataType));
    break;
  }
  }
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
    return Result<bool,RichError> ::success(true);
}

//  OPCUA_Access------------------------------------------------------------------
Result<bool, RichError> OPCUA_Access::connect() {
    return Result<bool, RichError>::success(true);
}

// ============ 断开连接 ============
Result<bool, RichError> OPCUA_Access::disconnect() {
    return Result<bool, RichError>::success(true);
}

// ============ 重连 ============
Result<bool, RichError> OPCUA_Access::reconnect(int maxRetries, int retryDelayMs) {
    return Result<bool, RichError>::error(
        RichError("Reconnection failed after " + std::to_string(maxRetries) + " attempts"));
}

// ============ 检查连接状态 ============
Result<bool, RichError> OPCUA_Access::isConnected() {
    return isConnectedInternal();
}

Result<bool, RichError> OPCUA_Access::isConnectedInternal() const {
    return Result<bool, RichError>::success(true);
}

void OPCUA_Access::Clear_Read_Respondse() {
  // 1. 先清理变体（它们可能依赖响应中的数据）
  for (auto &element : m_batchReadVariant) {
    UA_Variant_clear(&element);
  }
}

void OPCUA_Access::Clear_Write_Respondse() {
  // 1. 先清理变体（它们可能依赖响应中的数据）
  for (auto &element : m_batchWriteNodes) {
    // UA_Variant_clear(&element.value.value);
    UA_WriteValue_clear(
        &element); // 这会清理 nodeId, value, indexRange 等所有字段
  }
  m_batchWriteNodes.clear();
}

void OPCUA_Access::CleanupBatchNodes() {
  for (auto &node : m_batchReadNodes) {
    UA_ReadValueId_clear(&node);
  }
  m_batchReadNodes.clear();
  m_batchNodesValid = false;

  for (auto &variant : m_batchReadVariant) {
    UA_Variant_clear(&variant);
  }
  m_batchReadVariant.clear();

  for (auto &node : m_batchWriteNodes) {
    UA_WriteValue_clear(&node); // 这会清理 nodeId, value, indexRange 等所有字段
  }
  m_batchWriteNodes.clear();

  for (auto &element : m_writeValueMap) // 逻辑名 -> 物理地址)
  {
    UA_WriteValue_clear(
        &element.second); // 这会清理 nodeId, value, indexRange 等所有字段
  }
  m_writeValueMap.clear();

  m_batchWriteNodesValid = false;
}

void OPCUA_Access::Set_Read_NodeID(UA_ReadValueId &nodeid,PhysicalAddress& info)
{
  auto result{extractPureNodeIdRobust(info.fullPath)};
  nodeid.nodeId =
      UA_NODEID_STRING_ALLOC(std::stoi(result.first), result.second.data());

  nodeid.indexRange = UA_STRING_NULL;
  nodeid.attributeId = UA_ATTRIBUTEID_VALUE;
}

void OPCUA_Access::PrepareBatchRead(const std::vector<std::string> &requestVec) {
  // 复用 C++ vector
  if (m_batchNodesValid) {
    std::cout << "PrepareBatchRead skip for the batchReadNodes had initialize"
              << std::endl;
    return;
  }

  m_batchReadNodes.clear();
  m_batchReadNodes.reserve(m_addressMap.size());
  m_batchReadVariant.clear();
  m_batchReadVariant.reserve(m_addressMap.size());

  for (auto &var : requestVec) {
    auto it = m_addressMap.find(var); 
    if (it == m_addressMap.end()) {
      continue;
    }
    auto nodeInfo = it->second;
    if(nodeInfo.nameSpace != this->m_nameSpace)
    {
      continue;
    }
    UA_ReadValueId node;
    UA_Variant variant;

    UA_Variant_init(&variant);
    UA_ReadValueId_init(&node);

    Set_Read_NodeID(node, nodeInfo);
    m_batchReadNodes.push_back(std::move(node));
    m_batchReadVariant.push_back(std::move(variant));
  }

  m_batchNodesValid = true;
}

void OPCUA_Access::PrepareBatchWrite(const std::vector<WriteRequest> &requestVec) {
  // 复用 C++ vector
  if (m_batchWriteNodesValid) {
    std::cout
        << "PrepareBatchWrite skip for the PrepareBatchWrite had initialize"
        << std::endl;

    for (auto &request : requestVec) {

      auto it_writeValue = m_writeValueMap.find(request.tagName);
      if (it_writeValue != m_writeValueMap.end()) {
        auto copyWriteValue = it_writeValue->second;

        auto it_info = m_addressMap.find(request.tagName);
        if (it_info != m_addressMap.end()) {
          if (it_info->second.nameSpace != this->m_nameSpace) {
            continue;
          }

          auto result = opcua::writeConversion::dispatch(
              it_info->second.nameSpace, it_info->second.nodeId,
              it_info->second.dataType, copyWriteValue, request.value);
          if (result.is_fail()) {
            spdlog::error("transform error : {}",result.get_error()->what());
            continue;
          }
          m_batchWriteNodes.push_back(std::move(copyWriteValue));
        }

      }
    }

    return;
  }
  m_batchWriteNodes.clear();
  m_batchWriteNodes.reserve(m_addressMap.size());

  for (auto &var : m_addressMap) {
    UA_WriteValue destValue;
    UA_WriteValue_init(&destValue);

    m_writeValueMap[var.second.nodeId] = std::move(destValue);
  }

  m_batchWriteNodesValid = true;
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

Result<bool, RichError>
OPCUA_Access::batchWrite(std::vector<WriteRequest> &requestVec) {
  //  CLEAR ELEMEMT EXISTED BEFORE
  this->PrepareBatchWrite(requestVec);
  
// std::vector<UA_ReadValueId> m_batchReadNodes;

  auto result = client_pointer->batchWrite(m_batchWriteNodes);
  if(result.is_fail())
  {
    return Result<bool, RichError>::error(std::move(*result.get_error()));
  }
  else
  {
 return Result<bool, RichError>::success(true);
  }
}

Result<std::unordered_map<std::string, ValueType>, RichError>
OPCUA_Access::batchRead(std::vector<std::string> &requestVec) {
  //  CLEAR ELEMEMT EXISTED BEFORE
  this->PrepareBatchRead(requestVec);

  auto result = client_pointer->batchRead(m_batchReadNodes);
  if(result.is_fail())
  {
    return Result<std::unordered_map<std::string, ValueType>, RichError>::error(
        std::move(*result.get_error()));
  } else {
    auto readVariant = result.value_or({});
    size_t i = 0;
    std::unordered_map<std::string, ValueType> resultMap;
    for (auto &request : requestVec) {
      auto it = m_addressMap.find(request);
      if (it != m_addressMap.end()) {
        auto quality = *it;
        if (quality.second.nameSpace != this->m_nameSpace) {
          continue;
        }
        ValueType value;
        auto result = opcua::readConversion::dispatch(quality.second.dataType, i,
                                                  readVariant, value);
        if (result.is_fail()) {
        } else {
          resultMap[quality.second.nodeId] = std::move(value);
        }
      }
      ++i;
    }
    return Result<std::unordered_map<std::string, ValueType>, RichError>::success(
      std::move(resultMap));
  }
}

void OPCUA_Access::setAddressMap(
    std::unordered_map<std::string, PhysicalAddress> &map) {
  m_addressMap = map;
}

std::optional<int> OPCUA_Access::probeStringLength(int varByteOffset) {
  return std::optional<int>(0);
}

//=================OPC_UA_Client=======================
namespace { // 匿名命名空间，实现内部链接
class ReadResponseGuard {
public:
  ReadResponseGuard(UA_ReadResponse *access) : m_access(access) {}

  ~ReadResponseGuard() {
    if (m_access) {
      UA_ReadResponse_clear(m_access);
    }
  }

  // 禁止拷贝
  ReadResponseGuard(const ReadResponseGuard &) = delete;
  ReadResponseGuard &operator=(const ReadResponseGuard &) = delete;
  // 禁止移动
  ReadResponseGuard(ReadResponseGuard &&other)=delete;
  ReadResponseGuard &operator=(ReadResponseGuard &&other) = delete;

private:
  UA_ReadResponse *m_access;
};

class WriteResponseGuard {
public:
  WriteResponseGuard(UA_WriteResponse *access) : m_access(access) {}

  ~WriteResponseGuard() {
    if (m_access) {
      UA_WriteResponse_clear(m_access); // 清空旧数据
    }
  }

  // 禁止拷贝
  WriteResponseGuard(const WriteResponseGuard &) = delete;
  WriteResponseGuard &operator=(const WriteResponseGuard &) = delete;

  // 禁止移动
  WriteResponseGuard(WriteResponseGuard &&other)=delete;
  WriteResponseGuard &operator=(WriteResponseGuard &&other) = delete;

private:
  UA_WriteResponse *m_access;
};

class WriteRequestGuard {
public:
  WriteRequestGuard(UA_WriteRequest *access) : m_access(access) {}

  ~WriteRequestGuard() {
    if (m_access)
      UA_WriteRequest_clear(m_access);
  }

  // 禁止拷贝
  WriteRequestGuard(const WriteRequestGuard &) = delete;
  WriteRequestGuard &operator=(const WriteRequestGuard &) = delete;
  WriteRequestGuard &operator=(WriteRequestGuard &&) = delete;
  WriteRequestGuard(WriteRequestGuard &&other)=delete; 

private:
  UA_WriteRequest *m_access;
};

class ReadRequestGuard {
public:
  ReadRequestGuard(UA_ReadRequest *access) : m_access(access) {}

  ~ReadRequestGuard() {
    if (m_access) {
      UA_ReadRequest_clear(m_access);
    }
  }

  // 禁止拷贝
  ReadRequestGuard(const ReadRequestGuard &) = delete;
  ReadRequestGuard &operator=(const ReadRequestGuard &) = delete;
  // 禁止移动
  ReadRequestGuard(ReadRequestGuard &&other) =delete;
  ReadRequestGuard &operator=(ReadRequestGuard &&) = delete;

private:
  UA_ReadRequest *m_access;
};

class TaskGuard {
public:
  enum class Mode { Shared, Exclusive };
  TaskGuard(std::mutex &taskCvMx, std::condition_variable &taskCv,
            size_t &taskCounter)
      : m_taskCvMx(taskCvMx), m_taskCv(taskCv), m_taskCounter(taskCounter) {
    std::lock_guard<std::mutex> lock(m_taskCvMx);
    ++m_taskCounter;
  }

  ~TaskGuard() {
    bool shouldNotify = false;
    {
      std::lock_guard<std::mutex> lock(m_taskCvMx);
      if (m_taskCounter > 0) {
        --m_taskCounter;
        if (m_taskCounter == 0)
          shouldNotify = true;
      }
    }
    if (shouldNotify)
      m_taskCv.notify_all();
  }

  // 禁止拷贝
  TaskGuard(const TaskGuard &) = delete;
  TaskGuard &operator=(const TaskGuard &) = delete;

  // 禁止移动构造
  TaskGuard(TaskGuard &&other) = delete;
  // 禁止移动赋值
  TaskGuard &operator=(TaskGuard &&) = delete;

private:
  std::mutex &m_taskCvMx;
  std::condition_variable &m_taskCv;
  size_t &m_taskCounter;
};

class CASGuard {
public:
  CASGuard(std::atomic<ConnState> &connState) : m_connState(connState) {
  }

  ~CASGuard() {
    m_connState.store(ConnState::IDLE,std::memory_order_release);
  }

  // 禁止拷贝
  CASGuard(const CASGuard &) = delete;
  CASGuard &operator=(const CASGuard &) = delete;

  // 禁止移动构造
  CASGuard(CASGuard &&other) = delete;
  // 禁止移动赋值
  CASGuard &operator=(CASGuard &&) = delete;

private:
  std::atomic<ConnState> &m_connState;
};

class RecreateGuard {
public:
  RecreateGuard(std::atomic<bool> &recreating, std::mutex &recreateLock)
      : m_recreateing(recreating), m_recreateLock(recreateLock) {}

  ~RecreateGuard() {
    std::lock_guard<std::mutex> lock(m_recreateLock);
    m_recreateing.store(false, std::memory_order_release);
  }

  // 禁止拷贝
  RecreateGuard(const RecreateGuard &) = delete;
  RecreateGuard &operator=(const RecreateGuard &) = delete;

  // 禁止移动构造
  RecreateGuard(RecreateGuard &&other) = delete;
  // 禁止移动赋值
  RecreateGuard &operator=(RecreateGuard &&) = delete;

private:
  std::atomic<bool> &m_recreateing;
  std::mutex &m_recreateLock;
};

class ThreadGuard {
public:
  ThreadGuard(std::unique_ptr<std::thread> watchdogThread,
              std::condition_variable &watchdogCv)
      : m_watchdogThread(std::move(watchdogThread)), m_watchdogCv(watchdogCv) {}

  ~ThreadGuard() {
    if (m_watchdogThread && m_watchdogThread->joinable()) {
      m_watchdogCv.notify_all();
      m_watchdogThread->join();
    }
  }

  // 禁止拷贝
  ThreadGuard(const ThreadGuard &) = delete;
  ThreadGuard &operator=(const ThreadGuard &) = delete;

  // 禁止移动构造
  ThreadGuard(ThreadGuard &&other) = delete;
  // 禁止移动赋值
  ThreadGuard &operator=(ThreadGuard &&) = delete;

private:
  std::unique_ptr<std::thread> m_watchdogThread;
  std::condition_variable &m_watchdogCv;
};

}; // namespace

/**
 * @brief Impl 生命周期与回调契约
 *
 * 1. 所有 SDK 调用（如 UA_Client_Service_read/write、UA_Client_run_iterate 等）
 *    必须在持有本 Impl 的 shared_ptr 快照期间进行，以保证 Impl 存活。
 * 2. SDK 回调（stateCallback/inactivityCallback）由 SDK 内部线程触发，可能在
 *    任意时刻发生。回调只能访问原子变量或调用无锁函数（如检查标志），
 *    禁止调用任何可能阻塞或加锁的 API（包括 SDK 函数、本类的非原子方法）。
 **/
struct OPC_UA_Client::Impl:public std::enable_shared_from_this<OPC_UA_Client::Impl> {
  UA_ClientPtr m_impl;

  mutable std::mutex m_lock;
  std::string endpointUrl;

  ClientConfig m_config;
  std::atomic<ConnState> m_connState{ConnState::IDLE};// CAS状态机，描述此时是否存在线程进行连接/断连操作，connect()进行状态设置
  std::atomic<LifeState> m_lifeState{LifeState::RUNNING};// Client 存活状态机，描述是否正常存活,看门狗进行状态修改
  std::atomic<bool> m_running{true}; // 看门狗运行状态,shutdown设置，没有重置
  std::atomic<bool> m_shutdown{false}; // 析构运行状态，shutdown设置，没有重置
  std::atomic<bool> connectionLost{false}; // 断开连接标志,回调函数里设置，在看门狗里重连中重置
  std::atomic<UA_StatusCode>sdkConnectStatus{UA_STATUSCODE_GOOD};//sdk内部重连是否继续标志，回调函数里设置，看门狗中使用
  std::atomic<UA_SecureChannelState>sdkChannelState{UA_SECURECHANNELSTATE_CLOSED};//sdk内部重连是否继续标志，回调函数里设置，看门狗中使用
  std::atomic<UA_SessionState>sdkSessionState{UA_SESSIONSTATE_CLOSED};//sdk内部重连是否继续标志，回调函数里设置，看门狗中使用
  std::atomic<bool> m_stop{false};//shutdown()保护变量，避免其他因素导致shutdown()未被调用
  std::mutex m_taskCvMx;            // 专门保护任务计数和条件变量
  std::condition_variable m_taskCv;
  size_t m_taskCounter = 0; //由 m_taskCvMx 保护,当前未完成的读写任务总量，在读写任务开始和结束时修改，用于shutdown()判断是否可以析构当前对象
  std::recursive_mutex m_sdkMutex;//使得SDK 调用真正串行化，connect/disconnect 不再与读写交叠
  //使用shared_from_this() 阻止外部recreateImpl对象时，任务数!=0时Impl.reset()后可能造成:1.read/write并发访问已析构Impl对象里的内部成员
  //shutdown() 线程                           业务线程
  //A（仍在执行 __Client_Service）
  // -------------------------------------
  // wait_for 超时，m_taskCounter > 0
  // doomed->disconnect()  <----------  A 线程正持有 clientMutex 或等待响应
  //                                     （disconnect 修改状态、关闭 socket 等）
  // doomed.reset()
  //   （引用计数仍 > 0，不释放）
  //                                     A 线程返回，释放 shared_ptr 快照
  //                                     引用计数变为 0，触发 UA_Client_delete
  //                                     UA_Client_delete 再次调用 disconnect
  //                                     并最终 free(client)
  //   A 线程在 __Client_Service 中释放了 clientMutex，正在等待 clientCondition；

  // 此时 disconnect() 获取 clientMutex，设置客户端状态为 DISCONNECTED，关闭网络连接，然后可能唤醒等待的线程；

  // A 线程被唤醒后检查状态发现已断开，于是尝试访问已关闭的连接对象或安全通道内部结构，这些结构可能已被 disconnect() 破坏或释放；

  // 如果 A 线程是最后一个持有 shared_ptr 快照的线程，它返回后会触发 UA_Client_delete。但此时 UA_Client_delete 内部的 UA_Client_disconnect 会再次操作已经损坏的内部状态，可能导致双重释放或访问无效内存。

  // 看门狗辅助
  std::unique_ptr<std::thread> m_watchdogThread;
  std::condition_variable m_watchdogCv;
  mutable std::mutex m_watchDogMx;
  // 延时函数辅助
  std::mutex m_retryCvMx;
  std::condition_variable m_retryCv;
  // give-up 时间窗成员
  int64_t m_giveUpThresholdMs{60000};// 默认60秒
  std::atomic<int64_t> m_lastBadStatusMs{0}; //自首次观测到非 GOOD 起计时 60s,针对可能的业务停摆（线程卡死/暂停/忘记轮询）超过 60s，如果前状态是GOOD，会触发重连而非直接退出
  std::mutex m_BadStatusMsMx; // 专门保护m_lastBadStatusMs的赋值
  //  initialize function
  Impl(const ClientConfig &cfg, const std::string &Url,
       UA_ClientPtr client)
      : m_config(cfg), endpointUrl(std::move(Url)), m_impl(std::move(client)) {}

  // 回调内禁止同步析构客户端。回调只允许:记录状态/触发条件变量
  // "GIVEN_UP 时看门狗负责断开、退出"
  //   销毁前必须 join：任何直接或间接导致 Impl 析构的路径（如
  //   shutdown()、recreateGiveUpClient() 中的 shutdown()、OPC_UA_Client
  //   析构）都必须先调用 Impl::shutdown() 置位停止标志并 join 看门狗线程。
  // 防御性保障：~Impl() 本身会再次置位停止标志并 join，以防未来新增路径遗漏调用
  // shutdown()。
  ~Impl() {
     // 不预先置位 m_shutdown，让 shutdown() 完成断开 + 唤醒看门狗
     if (!m_stop.load(std::memory_order_acquire)) {
       shutdown();
     }
    // 防御性置位：确保看门狗线程能够退出（即使没有显式调用 shutdown()）
    m_shutdown.store(true, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    m_watchdogCv.notify_all(); // 唤醒可能在等待的看门狗
    m_retryCv.notify_all();

    if(m_watchdogThread)
      ThreadGuard threadGuard{std::move(m_watchdogThread), m_watchdogCv};
    doCleanup();
  }

  // “调用期间禁止修改/析构入参 vector”
  // 单次 SDK 调用阻塞上界 ≈ config.timeout，断线窗口叠加 connectSync
  /**
 * @brief 关于同步服务调用与看门狗重连的交互说明
 *
 * 设计意图：
 *   - 连接状态管理（重连/断连）主要由看门狗线程负责；业务读写线程在检测到连接未就绪
 *     （RECOVERING 或 GIVEN_UP）时快速失败，不主动重连，避免业务线程长时间阻塞。
 *
 * SDK 实际行为（open62541 v1.4.14）：
 *   - 任何同步服务调用（如 UA_Client_Service_read/write）在发现 SecureChannel 未打开
 *     或 Session 未激活时，会自动调用 connectSync() 尝试重新建立连接（阻塞至多
 *     config.timeout），然后才继续服务请求。
 *   - noReconnect 仅禁止通道被动关闭后的后台自动重连，不禁止服务调用前的补连；
 *     noNewSession 才会在会话丢失时直接中止连接。
 *
 * 实际交互结果：
 *   - 即使在 RECOVERING 状态下，若业务线程已通过状态检查并进入 SDK 调用，也可能触发
 *     SDK 内部的 connectSync 重连。
 *   - 该 connectSync 与看门狗的重连操作通过 SDK 内部 clientMutex 串行化，不会发生双重
 *     connect 或资源竞争，但可能导致业务线程阻塞比预期更长，且重连发起者不一定是看门狗。
 *
 * 结论：
 *   - 本封装“重连权威在 watchdog”的表述为近似成立，并非绝对。实际重连可能由读写线程
 *     触发，但最终连接状态仍由看门狗统一管理和恢复。
 *
 * 测试要求：
 *   - 需覆盖“断线瞬间有读写线程在途”的场景，验证：
 *     1) 不会出现双重 connect（无并发重复建链）；
 *     2) connectSync 与 watchdog 的 connect 调用被正确串行化；
 *     3) 最终连接恢复由 watchdog 接管（即 watchdog 的恢复逻辑能正确处理竞态）。
 */
  Result<std::vector<ReadResult>, RichError>
  batchRead(const std::vector<ReadValue> &batchReadNodes);
  // “调用期间禁止修改/析构入参 vector”
    /**
   * @brief 执行批量写入（单次尝试，无内部自动重试）
   *
   * @note 由于 OPC UA 写入操作具有非幂等性（可能导致重复执行），
   *       本接口不对网络瞬断等可恢复错误进行底层重试。
   *       若写入失败（返回 Bad），调用方应根据业务逻辑自行决定是否重试。
   *       如需自动重试，请确保写入操作的幂等性，或在上层业务循环中调用本接口。
   */
  Result<std::vector<WriteResult>, RichError>
  batchWrite(const std::vector<WriteValue> &batchWriteNodes);
  // get client status safe-thread
  Result<bool, ConnectErrorState> isConnected(UA_SecureChannelState &channelState,UA_StatusCode &statusCode,
                                      UA_SessionState &sessionState) noexcept;
  // create() 返回后，客户端一定已连接，调用方可以立即使用
  Result<Unit, ConnectErrorState> connect();
  // 等待连接就绪
  Result<Unit, DisconnectErrorState> disconnect();
  // shutdown the life of Client
  // shutdown 最坏耗时公式： ≈ join 看门狗（含其恢复循环残余)+ 在途调用剩余
  // timeoutMs。
  // 契约：
  //  - signalShutdown() 可从任意线程调用（含看门狗线程自身）。
  //  - joinWatchdog() / shutdown() 只能从非看门狗线程调用。
  //    看门狗线程若需通知退出，只能调用 signalShutdown()，
  //    随后直接返回；join 操作由外部持有者（shutdownInternal /
  //    recreateGiveUpClient / ~Impl）负责。
  void shutdown() ;
  void signalShutdown();
  void joinWatchdog();
  // clear pointer
  void doCleanup() noexcept;
  // get client status safe-thread
  std::string safeStatusCodeName(UA_StatusCode code);
  /**
   * @brief 配置客户端参数（超时、重试策略等）
  **/
  Result<Unit, RichError> configureClient(bool useDefault);
  Result<Unit, RichError> validateConfiguration(bool useDefault);
  Result<Unit, RichError> applyConfiguration(bool useDefault);
  // 启动看门狗线程
  Result<Unit, RichError> startWatchdog();
  //  延时函数
  void delayFunction(uint32_t retry);
  // 看门狗循环
  // 断开重连退避最长 retryMaxBackoffMs
  // 同一客户端允许一个常驻 iterate线程 + 多个同步服务调用线程
  void runWatchdog();

  //  辅助函数:计算退避时间
  uint32_t calculateBackoff(uint32_t retry) const;

  // 辅助函数:剩余时间是否足够触发读写功能
  bool restTimeEnoughForSDK(uint64_t &delay,uint32_t &restTime) const;

  // 辅助函数：检查是否应该触发 GIVEN_UP
  // 首次坏状态启动计时，恢复后重置
  // 确保所有恢复路径都调用了 resetGiveUpTimer()
  bool shouldTriggerGiveUp() ;
  void updateBadStatusTime();
  void resetGiveUpTimer();
  
  // 辅助函数:LifeState状态切换对应的函数
  void TurnToRunning();
  void TurnToRecovring()noexcept;
  void TurnToGiveup();
  
  //  辅助函数:返回特定有效对象
  ClientConfig getEffectiveConfig() { return m_config; }

  //  辅助函数:返回是否停止运行信号
  // true:停止运行,false:允许继续运行
  bool getEffectiveStopSignal()
  {
    return (m_shutdown.load(std::memory_order_acquire) &&
            !m_running.load(std::memory_order_acquire)) ||
           m_lifeState.load(std::memory_order_acquire) == LifeState::GIVEN_UP;
  }

  // 辅助函数:返回健康状态
  bool isHealthy() const {
    // 使用原子三元组（这些值由 stateCallback 和 run_iterate 持续更新）
    return sdkConnectStatus.load(std::memory_order_acquire) ==
               UA_STATUSCODE_GOOD &&
           sdkChannelState.load(std::memory_order_acquire) ==
               UA_SECURECHANNELSTATE_OPEN &&
           sdkSessionState.load(std::memory_order_acquire) ==
               UA_SESSIONSTATE_ACTIVATED;
  }

  //  辅助函数:返回实时时间
  using clock = std::chrono::steady_clock;
  static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clock::now().time_since_epoch())
        .count();
  }

  //  辅助函数:外部sdk锁内调用UA_Client_run_iterate
  void callRunIterate(UA_ClientPtr &client) {
    std::lock_guard<std::recursive_mutex> lock(m_sdkMutex);
    UA_Client_run_iterate(client.get(), 50);
  }
};

Result<std::vector<OPC_UA_Client::ReadResult>, RichError> OPC_UA_Client::Impl::batchRead(
    const std::vector<ReadValue> &batchReadNodes) {
  auto pImplQuote = shared_from_this();
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{"shutdown called"});
    }
  }
  TaskGuard taskGuard{pImplQuote->m_taskCvMx, pImplQuote->m_taskCv, pImplQuote->m_taskCounter};

  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) == LifeState::GIVEN_UP) {
    return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
        RichError{"client life state is give-up"});
  }
  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) == LifeState::RECOVERING) {
    return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
        RichError{"client life state is recovering"});
  }

  // 不会出现两个线程同时修改连接状态机。因此你不需要在外部用 CAS 或自定义状态标志来保证互斥。

  if(batchReadNodes.size()==0)
  {
   return Result<std::vector<ReadResult>, RichError>::error(
            RichError{"ReadNodes is empty"});
  }

  bool result = isHealthy();
  if (!result) {
    return Result<std::vector<ReadResult>, RichError>::error(
        RichError{"connect status no healthy"});
  }

  UA_ReadRequest request;
  UA_ReadRequest_init(&request);
  ReadRequestGuard readRequest{&request};

  request.nodesToRead = (UA_ReadValueId *)UA_Array_new(
      batchReadNodes.size(), &UA_TYPES[UA_TYPES_READVALUEID]);
  if (!request.nodesToRead) {
    return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
        RichError{"UA_Array_new is fail"});
  }
  request.nodesToReadSize = batchReadNodes.size();
  request.timestampsToReturn = UA_TIMESTAMPSTORETURN_NEITHER;

  size_t index = 0;
  for (auto &readNode : batchReadNodes) {
    auto & node =request.nodesToRead[index]; 
    UA_ReadValueId_init(&node);
    if (readNode.node.id.empty()) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{"node id string is empty for ns: " +
                    std::to_string(readNode.node.ns)});
    }

    node.nodeId =
        UA_NODEID_STRING_ALLOC(readNode.node.ns, readNode.node.id.data());
    if (node.nodeId.identifierType != UA_NODEIDTYPE_STRING ||
        node.nodeId.identifier.string.data == nullptr) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{"UA_NODEID_STRING_ALLOC failed for ns: " +
                    std::to_string(readNode.node.ns)});
    }
    if (node.nodeId.identifier.string.length == 0) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{"NodeId 字符串长度为 : " +
                    std::to_string(node.nodeId.identifier.string.length)});
    }
    node.indexRange = UA_STRING_NULL;
    node.attributeId = UA_ATTRIBUTEID_VALUE;
    ++index;
  }

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);// 锁内拷贝 shared_ptr 快照，确保对象生命周期
                                              // 1. 检查是否已连接
    if (!pImplQuote->m_impl) {
      return Result<std::vector<ReadResult>, RichError>::error(
          RichError{"Client is nullptr"});
    } else if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<ReadResult>, RichError>::error(
          RichError{"shutdown called or running status is false"});
    } 
    client = pImplQuote->m_impl;
  }


  uint32_t attempts = pImplQuote->m_config.maxRetries + 1;
  auto startTime = std::chrono::steady_clock::now();
  for (uint32_t retry = 0; retry < attempts; ++retry) {

    {
      std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
      if (pImplQuote->getEffectiveStopSignal()) {
        return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
            RichError{"shutdown called"});
      }
    }
 
    if (pImplQuote->m_lifeState.load(std::memory_order_acquire) == LifeState::GIVEN_UP) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{"client life state is give-up"});
    }
    if (pImplQuote->m_lifeState.load(std::memory_order_acquire) == LifeState::RECOVERING) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{"client life state is recovering"});
    }
    const auto elapsed = std::chrono::steady_clock::now() - startTime;
    //  每次单次调用最坏花费时间=servcie+connectSync≈2*timeOutMs
    if (elapsed > std::chrono::milliseconds(pImplQuote->m_config.maxTotalWaitMs)) {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          RichError{"total time budget exceeded"});
    }

    UA_ReadResponse response;
    {
      std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
      response = UA_Client_Service_read(client.get(), request);
    }
    ReadResponseGuard guard(&response);
    UA_StatusCode sr = response.responseHeader.serviceResult;
    if (sr == UA_STATUSCODE_BADCONNECTIONCLOSED ||
        sr == UA_STATUSCODE_BADCOMMUNICATIONERROR ||
        sr == UA_STATUSCODE_BADSESSIONIDINVALID ||
        sr == UA_STATUSCODE_BADSECURECHANNELIDINVALID ||
        sr == UA_STATUSCODE_BADNOTCONNECTED ||
        sr == UA_STATUSCODE_BADSECURECHANNELCLOSED ||
        sr == UA_STATUSCODE_BADSERVERNOTCONNECTED||
        sr == UA_STATUSCODE_BADTIMEOUT||
        sr == UA_STATUSCODE_BADSESSIONCLOSED 
      ) {
      if (retry != pImplQuote->m_config.maxRetries) {
        delayFunction(retry);
      } else {
        return Result<std::vector<ReadResult>, RichError>::error(
            RichError{"batchRead: max retries exhausted, last status: " +
                      std::to_string(sr)});
      }
    } else {

      if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD ||
          response.resultsSize != batchReadNodes.size() || !response.results) {
        std::stringstream ss;
        ss << "read fail :"
           << " read_response.responseHeader.serviceResult : "
           << response.responseHeader.serviceResult
           << " read_response.resultsSize : " << response.resultsSize;

        return Result<std::vector<ReadResult>, RichError>::error(
            RichError{ss.str()});
      }

      std::vector<OPC_UA_Client::ReadResult> resultVec;
      resultVec.reserve(batchReadNodes.size());
      //  RECORD RESPONSE_VALUE
      for (size_t i = 0; i < batchReadNodes.size(); ++i) {
        const auto &item = response.results[i];
        const auto &dataType = batchReadNodes[i].node.dataType;

        ReadResult var;
        var.rawStatus = item.status;
        if (item.status == UA_STATUSCODE_GOOD) {
          opcua::readConversion::dispatch(dataType, item.value, var.value);
        } else {
          var.value = std::nullopt; // 显式置空，业务层通过 has_value() 判断
        }
        resultVec.push_back(std::move(var));
      }
      return Result<std::vector<ReadResult>, RichError>::success(
          std::move(resultVec));
    }

   
  }

  return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
      RichError{"unreachable"});
}

Result<std::vector<OPC_UA_Client::WriteResult>, RichError> OPC_UA_Client::Impl::batchWrite(const std::vector<WriteValue> &batchWriteNodes) {
  auto pImplQuote = shared_from_this();
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{"shutdown called"});
    }
  }
  TaskGuard taskGuard{pImplQuote->m_taskCvMx, pImplQuote->m_taskCv, pImplQuote->m_taskCounter};

  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) == LifeState::GIVEN_UP) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{"client life state is give-up"});
  }
  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) == LifeState::RECOVERING) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{"client life state is recovering"});
  }

  if (batchWriteNodes.size() == 0) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{"writeNodes is empty"});
  }

  bool result = isHealthy();
  if(!result)
  {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{"connect status no healthy"});
  }

  UA_WriteRequest request;
  UA_WriteRequest_init(&request);
  WriteRequestGuard writeRequest{&request};

  request.nodesToWrite = (UA_WriteValue *)UA_Array_new(
      batchWriteNodes.size(), &UA_TYPES[UA_TYPES_WRITEVALUE]);
  if (!request.nodesToWrite) {
    return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
        RichError{"UA_Array_new is fail"});
  }
  request.nodesToWriteSize = batchWriteNodes.size();

  size_t index = 0;
  for (auto &writeNode : batchWriteNodes) {
    auto &node = request.nodesToWrite[index];
    UA_WriteValue_init(&node);
    if (writeNode.node.id.empty()) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{"node id string is empty for ns: " +
                    std::to_string(writeNode.node.ns)});
    }

    auto  writeResult = opcua::writeConversion::dispatch(
        writeNode.node.ns, writeNode.node.id, writeNode.node.dataType, node,
        writeNode.value);
    if (writeResult.is_fail()) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{writeResult.get_error()->what()});
    }
    if (node.nodeId.identifierType != UA_NODEIDTYPE_STRING ||
        node.nodeId.identifier.string.data == nullptr) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{"UA_NODEID_STRING_ALLOC failed for ns: " +
                    std::to_string(writeNode.node.ns)});
    }
    if (node.nodeId.identifier.string.length == 0) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{"NodeId 字符串长度为 : " +
                    std::to_string(node.nodeId.identifier.string.length)});
    }
    ++index;
  }

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(
        pImplQuote->m_lock); // 锁内拷贝 shared_ptr 快照，确保对象生命周期
    if (!pImplQuote->m_impl) {
      return Result<std::vector<WriteResult>, RichError>::error(
          RichError{"Client is nullptr"});
    } else if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<WriteResult>, RichError>::error(
          RichError{"shutdown called or running status is false"});
    }
    client = pImplQuote->m_impl;
  }

  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          RichError{"shutdown called"});
    }
  }

  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) == LifeState::GIVEN_UP) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{"client life state is give-up"});
  }
  if (pImplQuote->m_lifeState.load(std::memory_order_acquire) == LifeState::RECOVERING) {
    return Result<std::vector<WriteResult>, RichError>::error(
        RichError{"client life state is recovering"});
  }
  // 3. 执行批量写入
  UA_WriteResponse response;
  {
    std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
    response = UA_Client_Service_write(client.get(), request);
  }
  WriteResponseGuard write_response(&response);
  // 直接按值拷贝进 vector！深拷贝 4 字节整数，极速且安全！
  if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD &&
      response.resultsSize > 0 && response.results &&
      response.resultsSize == batchWriteNodes.size()) {
    std::vector<WriteResult> resultVec;
    resultVec.reserve(batchWriteNodes.size());
    for (size_t i = 0; i < response.resultsSize; ++i) {
      WriteResult var;
      if (response.results[i] == UA_STATUSCODE_GOOD) {
        var.status = OPC_UA_Client::WriteResult::Status::Good;
      } else {
        var.status = OPC_UA_Client::WriteResult::Status::Bad;
      }
      var.rawStatus = response.results[i];
      resultVec.push_back(std::move(var));
    }
    return Result<std::vector<WriteResult>, RichError>::success(
        std::move(resultVec));
  } else {
    return Result<std::vector<WriteResult>, RichError>::error(RichError{
        "write failed: " +
        safeStatusCodeName(
            response.responseHeader.serviceResult)}); // ← 明确返回失败
  }
}

Result<bool, ConnectErrorState>
OPC_UA_Client::Impl::isConnected(UA_SecureChannelState &channelState,UA_StatusCode &statusCode,
                                      UA_SessionState &sessionState) noexcept {
  auto pImplQuote = shared_from_this();
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<bool, ConnectErrorState>::error(ConnectErrorState::SHUTDOWN);
    }
  }

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(
        pImplQuote->m_lock); // 锁内拷贝 shared_ptr 快照，确保对象生命周期
    // 1. 检查是否已连接
    if (!pImplQuote->m_impl) {
      return Result<bool, ConnectErrorState>::error(ConnectErrorState::NULLPTR);
    }
    client = pImplQuote->m_impl;
  }

  // 2. 获取实际状态
  callRunIterate(client);
  UA_Client_getState(client.get(), &channelState, &sessionState, &statusCode);
  {
    sdkConnectStatus.store(statusCode, std::memory_order_release);
    sdkChannelState.store(channelState, std::memory_order_release);
    sdkSessionState.store(sessionState, std::memory_order_release);
  }

  if (statusCode != UA_STATUSCODE_GOOD) {
    pImplQuote->updateBadStatusTime();
  }
  return Result<bool, ConnectErrorState>::success(isHealthy());
}


Result<Unit, ConnectErrorState>
OPC_UA_Client::Impl::connect() {
  std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
  auto pImplQuote = shared_from_this();
  {
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::SHUTDOWN);
    }
  }
  // open62541 内部已经处理了这一点：connect 会等待正在进行的同步服务调用结束（因为它们持有同一把锁）
  // 留存其他任务时不该进行连接操作
  {
    std::lock_guard<std::mutex> lk(pImplQuote->m_taskCvMx);
    if (pImplQuote->m_taskCounter != 0) {
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::TASKWORKING);
    }
  }
  TaskGuard taskGuard{pImplQuote->m_taskCvMx, pImplQuote->m_taskCv, pImplQuote->m_taskCounter};

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(pImplQuote->m_lock);
    if (pImplQuote->getEffectiveStopSignal()) {
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::SHUTDOWN);
    }
    if (!pImplQuote->m_impl) {
      return Result<Unit, ConnectErrorState>::error(ConnectErrorState::NULLPTR);
    }
    client = pImplQuote->m_impl;
  }

  // 3. CAS 单飞 (核心)
  ConnState expected = ConnState::IDLE;
  if (!pImplQuote->m_connState.compare_exchange_strong(expected, ConnState::CONNECTING)) {
    // CAS失败，说明有其他线程正在连接
    if (expected == ConnState::CONNECTING) {
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::THREADBUSY);
    } else {
      return Result<Unit, ConnectErrorState>::error(ConnectErrorState::UNKNOWN);
    }
  }
  CASGuard casGuard{pImplQuote->m_connState};

  // 4. 尝试连接
  UA_StatusCode retval =
      UA_Client_connectAsync(client.get(), endpointUrl.c_str());
  if (retval != UA_STATUSCODE_GOOD) {
    return Result<Unit, ConnectErrorState>::error(
        ConnectErrorState::CONNECT_FAILED);
  }

  // ★ 等待连接就绪（超时上限 = timeoutMs）
  auto start = std::chrono::steady_clock::now();
  while (true) {
    UA_SecureChannelState channelState{UA_SECURECHANNELSTATE_CONNECTED};
    UA_StatusCode statusCode{UA_STATUSCODE_GOOD};
    UA_SessionState sessionState{UA_SESSIONSTATE_CLOSED};
    auto connectResult = isConnected(channelState, statusCode, sessionState);
    if (connectResult.is_fail()) {
      auto error = *connectResult.get_error();
      if (error == ConnectErrorState::SHUTDOWN) {
        return Result<Unit, ConnectErrorState>::error(
            ConnectErrorState::SHUTDOWN);
      }
      if (error == ConnectErrorState::NULLPTR) {
        return Result<Unit, ConnectErrorState>::error(
            ConnectErrorState::NULLPTR);
      }
    } else {
      auto connectResultValue = connectResult.value_or(false);
      if (connectResultValue) {
        //  健康度检测正常
        return Result<Unit, ConnectErrorState>::success(Unit{});
      }
    }

    // 检查超时
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::milliseconds(pImplQuote->m_config.timeoutMs)) {
      auto lastConnectResult = isConnected(channelState, statusCode, sessionState);
      if (lastConnectResult.is_fail()) {
        auto error = *lastConnectResult.get_error();
        if (error == ConnectErrorState::SHUTDOWN) {
          return Result<Unit, ConnectErrorState>::error(
              ConnectErrorState::SHUTDOWN);
        }
        if (error == ConnectErrorState::NULLPTR) {
          return Result<Unit, ConnectErrorState>::error(
              ConnectErrorState::NULLPTR);
        }
      } else {
        auto lastConnectResultValue = lastConnectResult.value_or(false);
        if (lastConnectResultValue) {
          return Result<Unit, ConnectErrorState>::success(Unit{});
        }
      }
      //  为后面的connect做好收尾工作
      UA_Client_disconnect(client.get());
      return Result<Unit, ConnectErrorState>::error(
          ConnectErrorState::TIMEOUT_NOTCONNECT);
    }

    {
      if (pImplQuote->getEffectiveStopSignal()) {
        return Result<Unit, ConnectErrorState>::error(
            ConnectErrorState::SHUTDOWN);
      }
    }
  }

  return Result<Unit, ConnectErrorState>::success(Unit{});
}

Result<Unit, DisconnectErrorState> OPC_UA_Client::Impl::disconnect() {
  auto pImplQuote = shared_from_this();

  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(
        pImplQuote->m_lock); // 锁内拷贝 shared_ptr 快照，确保对象生命周期
                             // 1. 检查生命周期状态 (外部输入)
    // 1. 检查是否已连接
    if (!pImplQuote->m_impl) {
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::NULLPTR);
    }
    client = pImplQuote->m_impl;
  }

  {
    std::lock_guard<std::mutex> lk(pImplQuote->m_taskCvMx);
    if (pImplQuote->m_taskCounter != 0) {
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::TASKWORKING);
    }
  }
  TaskGuard taskGuard{pImplQuote->m_taskCvMx, pImplQuote->m_taskCv,
                      pImplQuote->m_taskCounter};

  // 3. CAS 单飞 (核心)
  ConnState expected = ConnState::IDLE;
  if (!pImplQuote->m_connState.compare_exchange_strong(expected,
                                                       ConnState::CONNECTING)) {
    // CAS失败，说明有其他线程正在连接,失败的线程根据expected的状态直接返回
    if (expected == ConnState::CONNECTING) {
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::THREADBUSY);
    } else {
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::UNKNOWN);
    }
  }
  CASGuard casGuard{pImplQuote->m_connState};

  {
    std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
     UA_StatusCode retval = UA_Client_disconnectAsync(client.get());
     if (retval != UA_STATUSCODE_GOOD) {
       return Result<Unit, DisconnectErrorState>::error(
           DisconnectErrorState::DISCONNECT_FAIL);
     }
  }

  // ★ 等待CLOSED就绪（超时上限 = timeoutMs）
  auto start = std::chrono::steady_clock::now();
  bool isShutdown = false;
  while (!isShutdown) {
    UA_SecureChannelState cs;
    UA_SessionState ss;
    UA_StatusCode sc;
    auto result = pImplQuote->isConnected(cs, sc, ss);
    if (result.is_fail()) {
      // 代表shutdown called 或者 client = nullptr
      auto error = *result.get_error();
      if (error == ConnectErrorState::SHUTDOWN) {
        return Result<Unit, DisconnectErrorState>::error(
            DisconnectErrorState::SHUTDOWN);
      } else {
        return Result<Unit, DisconnectErrorState>::error(
            DisconnectErrorState::NULLPTR);
      }
    }
    if (sc != UA_STATUSCODE_GOOD) {
      pImplQuote->updateBadStatusTime();
    }
    if (cs == UA_SECURECHANNELSTATE_CLOSED)
      break;

    // 检查超时
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::milliseconds(pImplQuote->m_config.timeoutMs)) {
      //  查询
      //  UA_Client_getState（D7）据实修正返回，避免异步结果此时才更新，导致外部需要重新进入disconnect
      UA_Client_getState(client.get(), &cs, &ss, &sc);
      if (cs == UA_SECURECHANNELSTATE_CLOSED)
        return Result<Unit, DisconnectErrorState>::success(Unit{});

      UA_Client_disconnect(client.get());
      return Result<Unit, DisconnectErrorState>::error(
          DisconnectErrorState::TIMEOUT_NOTDISCONNECT);
    }

    {
      if (pImplQuote->getEffectiveStopSignal()) {
        return Result<Unit, DisconnectErrorState>::error(
            DisconnectErrorState::SHUTDOWN);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  return Result<Unit, DisconnectErrorState>::success(Unit{});
}


void OPC_UA_Client::Impl::shutdown() {
    signalShutdown();   // 置位 + notify + disconnect
    joinWatchdog();     // join 看门狗线程
    m_stop.store(true, std::memory_order_release);
}

void OPC_UA_Client::Impl::signalShutdown() {
  std::lock_guard<std::recursive_mutex> sdkLock(m_sdkMutex);
  UA_ClientPtr client;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_shutdown.load(std::memory_order_acquire)) {
      return;
    }
    m_running.store(false, std::memory_order_release);
    m_shutdown.store(true, std::memory_order_release);
    if (!m_impl) {
      return;
    }
    client = m_impl;
  }
  if (m_stop.load(std::memory_order_acquire)) {
    return;
  }

  m_watchdogCv.notify_all();
  m_retryCv.notify_all();

  // 在 m_sdkMutex 下等待所有在途 SDK 调用结束，然后执行同步断开。
  // 本函数不打断在途调用；在途调用的返回依赖其自身 timeoutMs。
  UA_Client_disconnect(client.get());
}
void OPC_UA_Client::Impl::joinWatchdog() {
  if (m_watchdogThread && m_watchdogThread->joinable() &&
      m_watchdogThread->get_id() != std::this_thread::get_id()) {
    m_watchdogThread->join();
  }
}

void OPC_UA_Client::Impl::doCleanup() noexcept {
    std::lock_guard<std::mutex> lock(m_lock);
    if (m_impl) {
      // 禁止对 SDK 配置字段的修改（因为官方禁止、且无法安全同步）
      m_impl.reset();
    }
}

std::string OPC_UA_Client::Impl::safeStatusCodeName(UA_StatusCode code)
{
  const char *name = UA_StatusCode_name(code);
    if (!name || name[0] == '\0') {
      // 备选方案1：返回格式化的十六进制数（永不失败）
      // 使用固定大小的栈缓冲区，避免动态内存分配
      char buffer[16];
      snprintf(buffer, sizeof(buffer), "0x%08X", code);
      return std::string{buffer};
    }
    else
    { 
      return std::string{name};
    }
}

// ==================== 验证部分 ====================
Result<Unit, RichError> OPC_UA_Client::Impl::validateConfiguration(bool useDefault) {
    // 1. 检查多线程支持
#if UA_MULTITHREADING < 100
#error "OPC_UA_Client requires UA_MULTITHREADING >= 100 (default) for thread safety"
#endif

    // 2. 检查 m_impl 是否已初始化
    if (!m_impl) {
        return Result<Unit, RichError>::error(RichError{"m_impl is uninitialized"});
    }

    // 3. 检查是否已经启动
    if (m_watchdogThread) {
        return Result<Unit, RichError>::error(RichError{"already started"});
    }

    // 4. 获取配置并验证
    auto config = UA_Client_getConfig(m_impl.get());
    if (!config) {
        return Result<Unit, RichError>::error(RichError{"get Config fail"});
    }

    // 5. 验证 m_config 的有效性
    auto validateResult = m_config.check();
    if (validateResult.has_value() && !useDefault) {
        // 配置无效且不允许使用默认值
        return Result<Unit, RichError>::error(RichError{validateResult.value()});
    }

    // 所有验证通过
    return Result<Unit, RichError>::success(Unit{});
}

// ==================== 赋值部分 ====================
Result<Unit, RichError>
OPC_UA_Client::Impl::applyConfiguration(bool useDefault) {
  // 1. 获取配置
  auto config = UA_Client_getConfig(m_impl.get());
  if (!config) {
    return Result<Unit, RichError>::error(RichError{"get Config fail"});
  }

  // 2. 应用默认配置（如果需要）
  if (useDefault) {
    auto result=m_config.applyDefaultIfInvalid();
    if(result.is_fail())
    {
      return Result<Unit, RichError>::error(std::move(*result.get_error()));
    }
  }

  // 3. 应用超时配置
  config->timeout = m_config.timeoutMs;
  config->requestedSessionTimeout = m_config.sessionTimeoutMs;
  config->secureChannelLifeTime = m_config.secureChannelLifeTimeMs;

  // 4. 应用重试配置
  config->noReconnect = true;//固定设为true
  config->noNewSession = true;//固定设为true
  config->connectivityCheckInterval = m_config.connectivityCheckIntervalMs;

  // 5. 设置上下文
  config->clientContext = this;

  // 6. 注册回调函数
  config->inactivityCallback = [](UA_Client *client) {
    OPC_UA_Client::Impl *self =
        static_cast<OPC_UA_Client::Impl *>(UA_Client_getContext(client));
    if(self->getEffectiveStopSignal()) 
      return;
    self->connectionLost.store(true, std::memory_order_release);
  };

  config->stateCallback = [](UA_Client *client,
                             UA_SecureChannelState channelState,
                             UA_SessionState sessionState,
                             UA_StatusCode connectStatus) {
    // 获取上下文
    OPC_UA_Client::Impl *self =
        static_cast<OPC_UA_Client::Impl *>(UA_Client_getContext(client));
    if (!self || self->getEffectiveStopSignal()) {
      return;
    }

    if (connectStatus != UA_STATUSCODE_GOOD) {
      self->updateBadStatusTime();
    }

    // 更新状态变量
    self->sdkConnectStatus.store(connectStatus, std::memory_order_release);
    self->sdkChannelState.store(channelState, std::memory_order_release);
    self->sdkSessionState.store(sessionState, std::memory_order_release);

  };

  // ==================== 7. 初始化原子状态变量 ====================
  // 注意：这些初始化应该在对象构造时已完成，但为了确保配置应用后的状态一致性，
  // 在应用配置时重新设置为初始状态

  // 连接状态机 - 设置为空闲状态
  m_connState.store(ConnState::IDLE, std::memory_order_release);

  // 生命周期状态 - 设置为运行状态（假设配置应用后客户端可用）
  m_lifeState.store(LifeState::RUNNING, std::memory_order_release);

  // 运行标志 - 设置为 true（看门狗可以运行）
  m_running.store(true, std::memory_order_release);

  // 关闭标志 - 设置为 false（未关闭）
  m_shutdown.store(false, std::memory_order_release);


  // 连接丢失标志 - 设置为 false（初始连接未丢失）
  connectionLost.store(false, std::memory_order_release);

  // SDK 连接状态 - 设置为 GOOD（初始状态良好）
  sdkConnectStatus.store(UA_STATUSCODE_GOOD, std::memory_order_release);

  // SDK 通道状态 - 设置为 OPEN（初始通道打开）
  sdkChannelState.store(UA_SECURECHANNELSTATE_CLOSED, std::memory_order_release);

  // SDK 会话状态 - 设置为 ACTIVATED（初始会话激活）
  sdkSessionState.store(UA_SESSIONSTATE_CLOSED, std::memory_order_release);

  // 更新上面设置的原子变量对象
  UA_SecureChannelState cs;
  UA_SessionState ss;
  UA_StatusCode sc;
  UA_Client_getState(m_impl.get(), &cs, &ss, &sc);
  {
    sdkConnectStatus.store(sc,std::memory_order_release);
    sdkChannelState.store(cs,std::memory_order_release);
    sdkSessionState.store(ss,std::memory_order_release);
  }
  return Result<Unit, RichError>::success(Unit{});
}

// ==================== 重构后的 configureClient ====================
Result<Unit, RichError> OPC_UA_Client::Impl::configureClient(bool useDefault) {
    // 第一步：验证
    auto validateResult = validateConfiguration(useDefault);
    if (validateResult.has_error()) {
        return validateResult;
    }

    // 第二步：应用配置
    return applyConfiguration(useDefault);
}

Result<Unit, RichError> OPC_UA_Client::Impl::startWatchdog() {
  if (m_watchdogThread && m_watchdogThread->joinable()) {
    spdlog::warn("Watchdog thread already running");
    return Result<Unit,RichError>::success(Unit{});
  }

  m_running.store(true, std::memory_order_release);
  m_watchdogThread =
      std::make_unique<std::thread>(&OPC_UA_Client::Impl::runWatchdog, this);
  if (m_watchdogThread) {
    spdlog::info("Watchdog thread started");
    return Result<Unit,RichError>::success(Unit{});
  } else {
    m_watchdogThread.reset();
    return Result<Unit,RichError>::error(RichError{"watchDog start fail"});
  }
}

void OPC_UA_Client::Impl::delayFunction(uint32_t retry) {
  // 计算退避时间
  uint32_t backoffMs = calculateBackoff(retry); // 根据重试次数计算退避时间
  if (backoffMs == 0) {
    return;
  }

  // 分段睡眠，每100ms检查一次shutdown状态
  const uint32_t segmentMs = 100; // 每段睡眠100ms
  uint32_t elapsedMs = 0;

  while (elapsedMs < backoffMs) {
    // 检查是否应该退出
    if (getEffectiveStopSignal()) {
      return;
    }

    // 计算本次睡眠时长（不超过剩余时间）
    uint32_t sleepMs = std::min(segmentMs, backoffMs - elapsedMs);

    // 使用条件变量等待，可以被shutdown唤醒
    {
      std::unique_lock<std::mutex> lock(m_retryCvMx);
      // 等待指定时间或直到被唤醒
      m_retryCv.wait_for(lock, std::chrono::milliseconds(sleepMs), [this]() {
        return getEffectiveStopSignal();
      });
    }
    elapsedMs += sleepMs;
  }
}

// 统一指数退避：delay = min(retryBackoffBaseMs * 2^retry, retryMaxBackoffMs)
uint32_t OPC_UA_Client::Impl::calculateBackoff(uint32_t retry) const {
  uint64_t base = m_config.retryBackoffBaseMs;
  uint64_t cap = m_config.retryMaxBackoffMs;

  // 防御性检查：retry 过大时直接返回上限，避免移位溢出
  if (retry >= 32) {
    return static_cast<uint32_t>(cap);
  }

  // 使用左移实现 base * 2^retry，避免浮点精度问题
  uint64_t delay = base << retry;

  // 限制在最大值内
  if (delay > cap) {
    delay = cap;
  }

  // 确保结果在 uint32_t 范围内（cap 本身就是 uint32_t，delay 被限制后自然满足）
  return static_cast<uint32_t>(delay);
}

void OPC_UA_Client::Impl::runWatchdog() {
  auto last_decision_time = std::chrono::steady_clock::now();

  // ① 修正循环条件：shutdown 或 GIVEN_UP 时退出
  while (!getEffectiveStopSignal()) {

    // ========== 高频驱动:尽力 ~150ms 周期；sync 调用在途时让位 ==========
    UA_ClientPtr client;
    {
      std::lock_guard<std::mutex> lock(m_lock);
      if (!m_impl) {
        return;
      }
      client = m_impl;
    }
    if (client) {
      callRunIterate(client);
    }

    // ========== 低频裁决（每 watchdogIntervalMs 做一次） ==========
    auto now = std::chrono::steady_clock::now();
    if (now - last_decision_time >=
        std::chrono::milliseconds(m_config.watchdogIntervalMs)) {

      // ③ 在决策块开头检查是否应该跳过恢复动作
      if (!getEffectiveStopSignal()) {
        // ---------- 以下为恢复逻辑（仅在未shutdown且未GIVEN_UP时执行）
        bool healthyResult = isHealthy();

        // ---------- 原有三个分支（connectStatus != GOOD / Channel/Session异常
        // / connectionLost） ---------- 注意：这些分支内部也可能设置
        // GIVEN_UP，但不会覆盖已有 GIVEN_UP（因为我们已经跳过整个块）
        if (!healthyResult) {
          uint32_t retryConnect = 0;
          while (!getEffectiveStopSignal()) {
            if (shouldTriggerGiveUp()) {
              TurnToGiveup();
              break;
            }
            callRunIterate(client);
            auto connectResult = connect();
            ++retryConnect;
            if (connectResult.has_value()) {
              TurnToRunning();
              break;
            } else {
              auto error = *connectResult.get_error();
              if (error == ConnectErrorState::SHUTDOWN ||
                  error == ConnectErrorState::NULLPTR) {
                TurnToGiveup();
                break;
              }
              if(error == ConnectErrorState::TASKWORKING)
              {
                break;
              }
              TurnToRecovring();
            }
            delayFunction(retryConnect);
          }
        } else if (connectionLost.load(std::memory_order_acquire)) {
          uint32_t retryConnect = 0;
          while (!getEffectiveStopSignal()) {
            if (shouldTriggerGiveUp() ) {
              TurnToGiveup();
              break;
            }
            auto disconnectResult = disconnect();
            ++retryConnect;
            if (disconnectResult.is_fail()) {
              auto errorStatus = *disconnectResult.get_error();
              if (errorStatus == DisconnectErrorState::SHUTDOWN ||
                  errorStatus == DisconnectErrorState::NULLPTR) {
                TurnToGiveup();
                break;
              }
              if (errorStatus == DisconnectErrorState::TASKWORKING) {
                break;
              }
            }
            {
              callRunIterate(client);
              auto connectResult = connect();
              if (connectResult.is_fail()) {
                auto error = *connectResult.get_error();
                if (error == ConnectErrorState::SHUTDOWN ||
                    error == ConnectErrorState::NULLPTR) {
                  TurnToGiveup();
                  break;
                }
                if (error == ConnectErrorState::TASKWORKING) {
                  break;
                }
                TurnToRecovring();
              } else {
                TurnToRunning();
                break;
              }
            }
            delayFunction(retryConnect);
          }
        }
        else
        {
          TurnToRunning();
        }
        // ---------- 恢复逻辑结束 ----------
      } // end of if (!shouldSkipRecovery)

      last_decision_time = now;
    }

    // ========== 等待下一个周期 ==========
    {
      std::unique_lock<std::mutex> lock(m_watchDogMx);
      m_watchdogCv.wait_for(lock, std::chrono::milliseconds(100),
                            [this] { return this->getEffectiveStopSignal(); });
    }

  }

}

Result<std::unique_ptr<OPC_UA_Client>, RichError>
OPC_UA_Client::create(const std::string &endpointUrl, ClientConfig config,
                      bool useDefault) {
  try {
    // new 失败会抛出 std::bad_alloc，由外层的 catch 捕获
    auto client =
        std::unique_ptr<OPC_UA_Client>(new OPC_UA_Client(endpointUrl, config));

    auto configResult = client->pImpl->configureClient(useDefault);
    if (configResult.is_fail()) {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
          RichError{configResult.get_error()->what()});
    }

    // 这是有意为之的“急切连接”策略，确保对象返回即可用。
    // 若需后台异步恢复，请上层捕获错误后自行重试。
    auto connectResult = client->connect();
    if (connectResult.is_fail()) {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
          RichError{connectResult.get_error()->what()});
    }

    // startWatchdog() 内部若 std::thread 创建失败会抛出 std::system_error
    auto wd = client->pImpl->startWatchdog();
    if (wd.is_fail())
    {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
          RichError{*wd.get_error()});
    }
    else
    {
      return Result<std::unique_ptr<OPC_UA_Client>, RichError>::success(
          std::move(client));
    }

  } catch (const std::bad_alloc &e) {
    // 处理内存分配失败
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{"Memory allocation failed: " + std::string(e.what())});
  } catch (const std::system_error &e) {
    // 处理线程创建等系统级失败
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{"System error: " + std::string(e.what())});
  } catch (const std::exception &e) {
    // 捕获其他标准异常作为兜底
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{"Unexpected error: " + std::string(e.what())});
  } catch (...) {
    // 捕获未知异常作为最终保障
    return Result<std::unique_ptr<OPC_UA_Client>, RichError>::error(
        RichError{"Unknown error"});
  }
}

OPC_UA_Client::OPC_UA_Client(const std::string endpointUrl,
                             ClientConfig config) {
  m_endpointUrl = endpointUrl;
  m_config = config;
  UA_ClientPtr raw(UA_Client_new(), UA_ClientDeleter{});
  if (raw) {
    pImpl = std::make_shared<Impl>(config, endpointUrl, std::move(raw));
  } else {
    throw std::bad_alloc(); // 内存分配失败
  }
}

OPC_UA_Client::~OPC_UA_Client() {
  {
    std::lock_guard<std::mutex> lock(m_recreateLock);
    m_terminated.store(true, std::memory_order_release);
  }
  shutdownInternal(); // 无条件清理,绕过业务早退
}

Result<Unit,RichError> OPC_UA_Client::shutdown() {
  {
    std::lock_guard<std::mutex> lock(m_recreateLock);
    if (m_recreating.load(std::memory_order_acquire)) {
        // 正在重建，拒绝关闭（或等待/记录日志）
        return Result<Unit,RichError>::error(RichError{"recreating , next time try again"});
    }
    if(m_terminated.load(std::memory_order_acquire))
    {
      return Result<Unit, RichError>::error(
          RichError{"shutdown called"});
    }
    m_terminated.store(true,std::memory_order_release);
  }

  return shutdownInternal();
}

Result<Unit, RichError> OPC_UA_Client::shutdownInternal() {
  std::shared_ptr<Impl> doomed;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (!pImpl)
      return Result<Unit, RichError>::error(RichError{"client is nullptr"});
    doomed = std::move(pImpl);
  }

  auto effectConfig = doomed->getEffectiveConfig();

  doomed->shutdown();

  // 等待任务计数器归零（条件变量驱动，微秒级响应），带超时机制
  {
    std::unique_lock<std::mutex> lk(doomed->m_taskCvMx);

    // 计算超时时间：max(timeoutMs, 2×watchdogIntervalMs)
    auto timeoutMs =
        std::max(effectConfig.timeoutMs, effectConfig.watchdogIntervalMs * 2);
    auto timeoutDuration = std::chrono::milliseconds(timeoutMs);

    // 使用 wait_for 等待，带超时
    bool completed = doomed->m_taskCv.wait_for(lk, timeoutDuration, [&]() {
      return doomed->m_taskCounter == 0; // 由锁保护，无需原子操作
    });

    if (!completed) {
      // 超时处理：任务未完成，强制清理或记录警告
      // 记录日志：任务计数器未能在超时时间内归零
      // 可以设置一个标志，表示有任务超时未完成
      // 或者直接继续执行，让资源被强制释放
    }
  }

  if (doomed)
    doomed.reset();
  return Result<Unit, RichError>::success(Unit{});
}

// 辅助函数：将 ConnectErrorState 转换为字符串描述
std::string OPC_UA_Client::connectErrorStateToString(ConnectErrorState state) {
  switch (state) {
  case ConnectErrorState::SHUTDOWN:
    return "client is shut down";
  case ConnectErrorState::NULLPTR:
    return "client implementation is null";
  case ConnectErrorState::THREADBUSY:
    return "another thread is connecting/disconnecting";
  case ConnectErrorState::UNKNOWN:
    return "unknown connection state";
  case ConnectErrorState::CONNECT_FAILED:
    return "connect failed";
  case ConnectErrorState::TIMEOUT_NOTCONNECT:
    return "connect timeout and connection not established";
  case ConnectErrorState::TASKWORKING:
    return "other read/write/disconnect/connect task is in progress";
  default:
    return "unhandled connection error";
  }
}

Result<Unit, RichError> OPC_UA_Client::connect() {
  std::shared_ptr<Impl> ImplPtr;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<Unit, RichError>::error({RichError{"client is nullptr"}});
    }
  }

  if (!allowUseAPI()) {
    return Result<Unit, RichError>::error({RichError{"RECREATING,请稍后重试"}});
  }
  auto result = ImplPtr->connect();
  if (result.has_value()) {
    return Result<Unit, RichError>::success(Unit{});
  } else {
    ConnectErrorState errorState = *result.get_error();
    return Result<Unit, RichError>::error(
        RichError{connectErrorStateToString(errorState)});
  }
}

Result<ConnectionState, RichError> OPC_UA_Client::checkConnected()  {
  std::shared_ptr<Impl> ImplPtr;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<ConnectionState, RichError>::error(
          {RichError{"client is nullptr"}});
    }
  }

  if (allowUseAPI()) {
    auto result = ImplPtr->isHealthy();
    if(result)
    {
      return Result<ConnectionState, RichError>::success(
         ConnectionState::CONNECTED);
    } else {
      return Result<ConnectionState, RichError>::success(
          ConnectionState::OBJECT_ONLY);
    }
  }
  return Result<ConnectionState, RichError>::error(
      {RichError{"RECREATING，请稍后重试"}});
}

Result<Unit, RichError> OPC_UA_Client::recreateGiveUpClient() {
  {
    std::lock_guard<std::mutex> lock(m_recreateLock);
    if(m_terminated.load(std::memory_order_acquire))
    {
      return Result<Unit, RichError>::error(
          {RichError{"shutdown call"}});
    }
    if (m_recreating.load(std::memory_order_acquire)) {
      return Result<Unit, RichError>::error(
          {RichError{"RECREATING，请稍后重试"}});
    }
    m_recreating.store(true, std::memory_order_release);
  }
  LifeState currentLifeState{LifeState::RUNNING};
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if(pImpl)
    {
      currentLifeState= pImpl->m_lifeState.load(std::memory_order_acquire);
    }
  }
  RecreateGuard recreateGuard{m_recreating,m_recreateLock};

  if (currentLifeState != LifeState::GIVEN_UP) {
    return Result<Unit, RichError>::error(
        {RichError{"当前状态不符合调用该函数"}});
  }

  // 获取配置：优先从现有 pImpl 获取有效配置，否则使用保存的初始配置
  ClientConfig effectiveConfig;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (pImpl) {
      effectiveConfig = pImpl->getEffectiveConfig();
    } else {
      effectiveConfig = m_config; // 使用初始配置
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (!pImpl) {
      return Result<Unit, RichError>::error(
          {RichError{"client = nullptr"}});
    }
  }

  try {
    UA_ClientPtr raw(UA_Client_new(), UA_ClientDeleter{});
    if (!raw) {
      std::lock_guard<std::mutex> lock(m_lock);
      return Result<Unit, RichError>::error(
          RichError{"UA_Client_new() failed"});
    }

    auto newImpl =
        std::make_shared<Impl>(effectiveConfig, m_endpointUrl, std::move(raw));

    // 4. 完整初始化新 Impl（配置 → 连接 → 启动看门狗）
    //    这些操作可能耗时，但此时不持锁，不会阻塞其他线程
    auto configResult = newImpl->configureClient(true);
    if (configResult.is_fail()) { // useDefault = true
      // 配置失败，放弃新 Impl，并将 pImpl 置空,旧 pImpl 保持不变
      std::lock_guard<std::mutex> lock(m_lock);
      return Result<Unit, RichError>::error(
          RichError{"configureClient failed"});
    }

    auto connResult = newImpl->connect();
    if (connResult.is_fail()) {
      return Result<Unit, RichError>::error(
          RichError{connectErrorStateToString(*connResult.get_error())});
    }

    // 启动看门狗
    {
      auto result = newImpl->startWatchdog();
      if (result.is_fail()) {
        std::lock_guard<std::mutex> lock(m_lock);
        return Result<Unit, RichError>::error(RichError{*result.get_error()});
      }
    }

    // 指针快照，为了避免对象在锁内析构造成可能的阻塞
    std::shared_ptr<Impl> old;
    {
      std::lock_guard<std::mutex> lock(m_lock);
      if (m_terminated.load(std::memory_order_acquire)) {
        return Result<Unit, RichError>::error(
            {RichError{"shutdown called during recreate"}});
      }
      old = std::move(pImpl);
      pImpl = newImpl;
    }

    if (old) {
      old->shutdown();
      old.reset();
    }

  } catch (const std::bad_alloc &e) {
    // 处理内存分配失败
    return Result<Unit, RichError>::error(
        RichError{"Memory allocation failed: " + std::string(e.what())});
  } catch (const std::system_error &e) {
    // 处理线程创建等系统级失败
    return Result<Unit, RichError>::error(
        RichError{"System error: " + std::string(e.what())});
  } catch (const std::exception &e) {
    // 捕获其他标准异常作为兜底
    return Result<Unit, RichError>::error(
        RichError{"Unexpected error: " + std::string(e.what())});
  } catch (...) {
    // 捕获未知异常作为最终保障
    return Result<Unit, RichError>::error(
        RichError{"Unknown error"});
  }

  return Result<Unit, RichError>::success(Unit{});
}

Result<LifeState, RichError> OPC_UA_Client::checkLifeState()  {
  std::shared_ptr<Impl> ImplPtr;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<LifeState, RichError>::error(
          {RichError{"client is nullptr"}});
    }
  }
  if (allowUseAPI())
    return Result<LifeState, RichError>::success(
        ImplPtr->m_lifeState.load(std::memory_order_acquire));
  return Result<LifeState, RichError>::error(
      {RichError{"RECREATING，请稍后重试"}});
}

// 该调用全程持有 SDK 内部互斥锁。若多线程并发，将在此处排队。
// 最坏等待时间 = 重试次数 × (同步重连 + timeoutMs)。
Result<std::vector<OPC_UA_Client::ReadResult>, RichError>
OPC_UA_Client::batchRead(const std::vector<ReadValue> &batchReadNodes) {
  std::shared_ptr<Impl> ImplPtr;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
          {RichError{"client is nullptr"}});
    }
  }
  if(allowUseAPI())
    return ImplPtr->batchRead(batchReadNodes);
  return Result<std::vector<OPC_UA_Client::ReadResult>, RichError>::error(
      {RichError{"RECREATING，请稍后重试ed"}});
}

Result<std::vector<OPC_UA_Client::WriteResult>, RichError>
OPC_UA_Client::batchWrite(const std::vector<WriteValue> &batchWriteNodes) {
  std::shared_ptr<Impl> ImplPtr;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    if (pImpl) {
      ImplPtr = pImpl;
    } else {
      return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
          {RichError{"client is nullptr"}});
    }
  }
  if(allowUseAPI())
    return ImplPtr->batchWrite(batchWriteNodes);
  return Result<std::vector<OPC_UA_Client::WriteResult>, RichError>::error(
      {RichError{"RECREATING，请稍后重试"}});
}

void OPC_UA_Client::Impl::updateBadStatusTime() {
  std::lock_guard<std::mutex> lock(m_BadStatusMsMx);
  if(m_lastBadStatusMs.load(std::memory_order_acquire)==0)
  {
    m_lastBadStatusMs.store(nowMs(), std::memory_order_release);
  }
}

void OPC_UA_Client::Impl::resetGiveUpTimer() {
  std::lock_guard<std::mutex> lock(m_BadStatusMsMx);
  if (m_lastBadStatusMs.load(std::memory_order_acquire) != 0) {
    m_lastBadStatusMs.store(0, std::memory_order_release);
  }
}

void OPC_UA_Client::Impl::TurnToRecovring() noexcept {
  // ② 状态转换时排除 GIVEN_UP（同时只在 RUNNING 时允许转 RECOVERING）
  auto currentState = m_lifeState.load(std::memory_order_acquire);
  if (currentState == LifeState::RUNNING &&
      (sdkConnectStatus.load(std::memory_order_acquire) != UA_STATUSCODE_GOOD ||
       connectionLost.load(std::memory_order_acquire) ||
       sdkSessionState.load(std::memory_order_acquire) != UA_SESSIONSTATE_ACTIVATED)) {
    m_lifeState.store(LifeState::RECOVERING, std::memory_order_release);
  }
}

void OPC_UA_Client::Impl::TurnToRunning() {
  if (m_lifeState.load(std::memory_order_acquire) == LifeState::RECOVERING ||
      m_lifeState.load(std::memory_order_acquire) == LifeState::RUNNING) {
    connectionLost.store(false, std::memory_order_release);
    m_lifeState.store(LifeState::RUNNING, std::memory_order_release);
    resetGiveUpTimer();
  }
}

void OPC_UA_Client::Impl::TurnToGiveup() {
  if(m_lifeState.load(std::memory_order_acquire)!=LifeState::GIVEN_UP)
  {
    m_lifeState.store(LifeState::GIVEN_UP, std::memory_order_release);
    updateBadStatusTime();
  }
}

bool OPC_UA_Client::Impl::shouldTriggerGiveUp() {
  int64_t ts = m_lastBadStatusMs.load(std::memory_order_acquire);
  if (ts == 0)
    return false;
  return (nowMs() - ts) >= m_giveUpThresholdMs;
}
