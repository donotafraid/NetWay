#include "PLC/OPC_UA.h"
#include <spdlog/spdlog.h>
#include <regex>

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
            return Result<bool, RichError>(true);
        }
        // 状态不一致，标记为未连接
        m_isConnected = false;
    }

    // 2. 检查客户端指针
    if (!m_client_var) {
        m_client_var = Cli_Create();
        if (!m_client_var) {
            return Result<bool, RichError>(
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
        spdlog::error(errorMsg);
        return Result<bool, RichError>(RichError(errorMsg));
    }

    // 4. 验证连接状态
    auto statusResult = isConnectedInternal();
    if (statusResult.is_success() ) {
        m_isConnected = true;
        spdlog::info("Successfully connected to PLC");
        return Result<bool, RichError>(true);
    }

    // 5. 连接建立但状态异常
    Cli_Disconnect(m_client_var);
    return Result<bool, RichError>(
        RichError("Connection established but PLC status check failed"));
}

// ============ 断开连接 ============
Result<bool, RichError> S7_Access::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. 如果已经断开，直接返回
    if (!m_isConnected || !m_client_var) {
        spdlog::info("Already disconnected from PLC");
        return Result<bool, RichError>(true);
    }

    // 2. 断开连接
    spdlog::info("Disconnecting from PLC");
    Cli_Disconnect(m_client_var);
    
    // 3. 清理状态
    m_isConnected = false;
    
    spdlog::info("Disconnected from PLC");
    return Result<bool, RichError>(true);
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
        return Result<bool, RichError>(
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
                return Result<bool, RichError>(true);
            }
            
            // 连接建立但状态异常
            spdlog::warn("Reconnection established but PLC status check failed");
            Cli_Disconnect(m_client_var);
            
            // 清理并重建客户端以备下次尝试
            cleanupClient();
            m_client_var = Cli_Create();
            if (!m_client_var) {
                return Result<bool, RichError>(
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
                return Result<bool, RichError>(
                    RichError("Failed to recreate S7 client"));
            }
        }
    }

    return Result<bool, RichError>(
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
        return Result<bool, RichError>(false);
    }

    // 2. 检查 PLC 状态（轻量级检查）
    int cpu_state = 0;
    int result = Cli_GetPlcStatus(m_client_var, &cpu_state);
    
    if (result != 0) {
        // 连接断开
        const_cast<S7_Access*>(this)->m_isConnected = false;
        spdlog::debug("PLC connection check failed: {}", result);
        return Result<bool, RichError>(false);
    }

    // 3. 检查 CPU 状态
    bool isRunning = (cpu_state == S7CpuStatusRun);
    spdlog::debug("PLC status: {}", cpu_state);
    
    if (!isRunning) {
        spdlog::warn("PLC is not in RUN state (state: {})", cpu_state);
    }

    return Result<bool, RichError>(isRunning);
}

// ============ 清理客户端 ============
void S7_Access::cleanupClient() {
    if (m_client_var) {
        Cli_Destroy(&m_client_var);
    }
    m_isConnected = false;
}

// ============ 构建连接信息 ============
std::string S7_Access::buildConnectionInfo() const {
    return m_ip_Address + " (Rack: " + std::to_string(m_rack) + 
           ", Slot: " + std::to_string(m_slot) + ")";
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
      return Result<std::unordered_map<std::string, ValueType>, RichError>(
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
        return Result<std::unordered_map<std::string, ValueType>, RichError>(RichError{result.unwrap_err()});
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
    return Result<std::unordered_map<std::string, ValueType>, RichError>(std::move(resultMap));
  } else {
    return Result<std::unordered_map<std::string, ValueType>, RichError>(RichError("read variable failed"));
  }
}

Result<bool, RichError>
S7_Access::batchWrite(std::vector<WriteRequest> &requestVec) {
bool success = true;
 
  for (auto &request : requestVec) {
    auto it = m_addressMap.find(request.tagName);
    if (it == m_addressMap.end()) {
      return Result<bool, RichError>(RichError{"request do not map"});
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
    return Result<bool, RichError>(true);
  } else {
    return Result<bool, RichError>(RichError("read variable failed"));
  }
  //  reader is nullptr
  return Result<bool, RichError>(RichError("S7Acess is nullptr"));
}

std::optional<int>
S7_Access::probeStringLength(int byteOffset)  {
  // 只有在这里才真正调用 S7 SDK 去读长度
  auto result = meastureStringObjectLength(byteOffset);
  if(result.is_success())
  {
    return result.unwrap_returnLeftValue();
  }
  else
  {
    spdlog::error("probeStringLength: {}",result.unwrap_err().what());
    return -1;
  }
}

Result<bool,RichError> S7_Access::read(int DB_Number,int Start_Position,int Read_Size,uint8_t *SourceData_var) 
{
    auto  connect_check = this->isConnected();
    if (connect_check.is_fail()) {
      if(reconnect(10, 1000).is_fail())
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

Result<int, RichError> S7_Access::meastureStringObjectLength(int startPos) {
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

Result<bool, RichError>
S7_Access::TransformBytesToDataType(std::vector<uint8_t> &SrcBuffer,std::vector<uint8_t> &destBuffer,
                                                   PhysicalAddress &dataQuality,
                                                   ValueType &dataVar) { // 添加 dataVar 参数

  S7BigEndianToLittleEndian(SrcBuffer, destBuffer, dataQuality);

  // 添加参数有效性检查
  if (destBuffer.empty()) {
    return Result<bool, RichError>(RichError("bytes vector is empty"));
  }

  // 检查对象是否有效
  if (dataQuality.dataType == S7DataType::UNKNOWN) {
    return Result<bool, RichError>(RichError("object type is UNKNOWN"));
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
      return Result<bool, RichError>(RichError("STRING: invalid length"));
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
      return Result<bool, RichError>(
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
    return Result<bool, RichError>(RichError(
        "Unsupported data type: " + std::to_string(static_cast<int>(dataQuality.dataType))));
  }
  }

  // 所有分支成功执行
  return Result<bool, RichError>(true);
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
    return Result<bool,RichError> (true);
}

//  OPCUA_Access------------------------------------------------------------------
Result<bool, RichError> OPCUA_Access::connect() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. 检查是否已连接
    if (m_isConnected && m_client_pointer) {
        auto stateResult = isConnectedInternal();
        if (stateResult.is_success()) {
            spdlog::info("Already connected to OPC UA server");
            return Result<bool, RichError>(true);
        }
        // 状态不一致，标记为未连接
        m_isConnected = false;
    }

    // 2. 检查客户端指针
    if (!m_client_pointer) {
        m_client_pointer = UA_Client_new();
        if (!m_client_pointer) {
            return Result<bool, RichError>(RichError("Failed to create OPC UA client"));
        }
        configureClient();
    }

    // 3. 构建连接 URL
    std::string endpointUrl = buildEndpointUrl();
    spdlog::info("Connecting to OPC UA server: {}", endpointUrl);

    // 4. 尝试连接
    UA_StatusCode result = UA_Client_connect(m_client_pointer, endpointUrl.c_str());

    if (result != UA_STATUSCODE_GOOD) {
        std::string errorMsg = "Connection failed: " + 
                               std::string(UA_StatusCode_name(result)) +
                               " (Error code: " + std::to_string(result) + ")";
        spdlog::error(errorMsg);
        return Result<bool, RichError>(RichError(errorMsg));
    }

    // 5. 等待会话激活
    auto activationResult = waitForSessionActivation(5000);
    if (!activationResult.is_success() ) {
        UA_Client_disconnect(m_client_pointer);
        return Result<bool, RichError>(RichError("Session activation timeout"));
    }

    // 6. 标记为已连接
    m_isConnected = true;
    spdlog::info("Successfully connected to OPC UA server");
    return Result<bool, RichError>(true);
}

// ============ 断开连接 ============
Result<bool, RichError> OPCUA_Access::disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. 如果已经断开，直接返回
    if (!m_isConnected || !m_client_pointer) {
        return Result<bool, RichError>(true);
    }

    // 2. 断开连接
    spdlog::info("Disconnecting from OPC UA server");
    UA_Client_disconnect(m_client_pointer);
    
    // 3. 清理状态
    m_isConnected = false;
    
    spdlog::info("Disconnected from OPC UA server");
    return Result<bool, RichError>(true);
}

// ============ 重连 ============
Result<bool, RichError> OPCUA_Access::reconnect(int maxRetries, int retryDelayMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. 先断开当前连接
    if (m_isConnected && m_client_pointer) {
        UA_Client_disconnect(m_client_pointer);
        m_isConnected = false;
    }

    // 2. 清理并重建客户端
    cleanupClient();
    m_client_pointer = UA_Client_new();
    if (!m_client_pointer) {
        return Result<bool, RichError>(
            RichError("Failed to create new client for reconnection"));
    }
    configureClient();

    // 3. 重连循环
    std::string endpointUrl = buildEndpointUrl();

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        spdlog::info("Reconnection attempt {}/{}", attempt, maxRetries);

        // 尝试连接
        UA_StatusCode result = UA_Client_connect(m_client_pointer, endpointUrl.c_str());

        if (result == UA_STATUSCODE_GOOD) {
            // 等待会话激活
            auto activationResult = waitForSessionActivation(5000);
            if (activationResult.is_success()) {
                m_isConnected = true;
                spdlog::info("Reconnection successful");
                return Result<bool, RichError>(true);
            }
            
            // 会话激活失败，继续重试
            spdlog::warn("Session activation timeout, retrying...");
            UA_Client_disconnect(m_client_pointer);
            // 注意：断开后需要清理并重建客户端
            cleanupClient();
            m_client_pointer = UA_Client_new();
            if (m_client_pointer) {
                configureClient();
            }
        }

        // 连接失败
        spdlog::error("Reconnection attempt {} failed: {} (code: {})", 
                      attempt,
                      UA_StatusCode_name(result),
                      result);

        // 如果不是最后一次尝试，等待后重试
        if (attempt < maxRetries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
            // 清理并重建客户端以备下次尝试
            cleanupClient();
            m_client_pointer = UA_Client_new();
            if (m_client_pointer) {
                configureClient();
            }
        }
    }

    return Result<bool, RichError>(
        RichError("Reconnection failed after " + std::to_string(maxRetries) + " attempts"));
}

// ============ 检查连接状态 ============
Result<bool, RichError> OPCUA_Access::isConnected() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return isConnectedInternal();
}

Result<bool, RichError> OPCUA_Access::isConnectedInternal() const {
    // 1. 快速检查
    if (!m_client_pointer || !m_isConnected) {
        return Result<bool, RichError>(false);
    }

    // 2. 获取实际状态
    UA_SecureChannelState channelState;
    UA_SessionState sessionState;
    UA_Client_getState(m_client_pointer, &channelState, &sessionState, nullptr);

    bool connected = (channelState == UA_SECURECHANNELSTATE_OPEN &&
                      sessionState == UA_SESSIONSTATE_ACTIVATED);

    // 3. 如果状态不一致，更新缓存
    if (!connected && m_isConnected) {
        // 缓存状态与实际状态不符，但因为是 const 方法，不能修改
        // 可以在非 const 方法中修复
        const_cast<OPCUA_Access*>(this)->m_isConnected = false;
    }

    return Result<bool, RichError>(connected);
}

void OPCUA_Access::cleanupClient() {
  if (m_client_pointer) {
    UA_Client_delete(m_client_pointer);
  }
}

std::string OPCUA_Access::buildEndpointUrl() const {
  return "opc.tcp://" + m_ip_Address + ":" + std::to_string(m_port);
}

void OPCUA_Access::Clear_Read_Respondse() {
  // 1. 先清理变体（它们可能依赖响应中的数据）
  for (auto &element : m_batchReadVariant) {
    UA_Variant_clear(&element);
  }

  // 2. 再清理响应
  UA_ReadResponse_clear(&m_read_response);
}

void OPCUA_Access::Clear_Write_Respondse() {
  // 1. 先清理变体（它们可能依赖响应中的数据）
  for (auto &element : m_batchWriteNodes) {
    // UA_Variant_clear(&element.value.value);
    UA_WriteValue_clear(
        &element); // 这会清理 nodeId, value, indexRange 等所有字段
  }
  m_batchWriteNodes.clear();

  //  CLEAR RESOURCE AFTER SUCCESS LOOP
  UA_WriteResponse_clear(&(m_write_response));
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

UA_Client *OPCUA_Access::getClient()
{
    return this->m_client_pointer;
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

          auto result = batchSet_Normal_To_Write_UA_Scalar(
              it_info->second, copyWriteValue, request.value);
          if (result.is_fail()) {
            spdlog::error("transform error : {}",result.unwrap_err().what());
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


Result<bool, RichError> OPCUA_Access::read() {
  auto connectResult = isConnected();
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

Result<bool, RichError>
OPCUA_Access::batchWrite(std::vector<WriteRequest> &requestVec) {
  //  CLEAR ELEMEMT EXISTED BEFORE
  this->PrepareBatchWrite(requestVec);

  auto result = this->batchWrite();

  //  check total write condition result
  return Result<bool,RichError> (std::move(result)); 
}

Result<std::unordered_map<std::string, ValueType>, RichError>
OPCUA_Access::batchRead(std::vector<std::string> &requestVec) {
  //  CLEAR ELEMEMT EXISTED BEFORE
  this->PrepareBatchRead(requestVec);

  // RAII 守卫：确保在任何退出路径下都会清理
  ReadResponseGuard guard(this);

  auto it = this->read();
  if (it.is_success()) {
    //  RECORD RESPONSE_VALUE
    for (int i = 0; i < m_batchReadNodes.size(); ++i) {
      UA_Variant_copy(&m_read_response.results[i].value,
                      &m_batchReadVariant[i]);
    }

    int i = 0;
    std::unordered_map<std::string, ValueType> resultMap;
    for (auto &request : requestVec) {
      auto it = m_addressMap.find(request);
      if (it != m_addressMap.end()) {
        auto quality = *it;
        if (quality.second.nameSpace != this->m_nameSpace) {
          continue;
        }
        ValueType value;
        auto result = Set_UA_To_Read_Normal_Scalar(
            quality.second.dataType, value, i, m_batchReadVariant);
        if (result.is_fail()) {
        } else {
          resultMap[quality.second.nodeId] = std::move(value);
        }
      }
      ++i;
    }

    return Result<std::unordered_map<std::string, ValueType>, RichError>(
        std::move(resultMap));
  } else {
    return Result<std::unordered_map<std::string, ValueType>, RichError>(RichError(it.unwrap_err()));
  }
}

// String Special Function --------- Covert_UA_Scalar_To_Specific
template <>
void OPCUA_Access::Covert_UA_Scalar_To_Specific(
    std::string &SourceData_var, int i,
    const std::vector<UA_Variant> &batchReadVariant) {
  const UA_String *src =
      static_cast<const UA_String *>(batchReadVariant[i].data);
  if (src && src->data && src->length > 0) {
    SourceData_var.assign(reinterpret_cast<const char *>((src->data)),
                          src->length);
  } else {
    SourceData_var = "";
  }
}

template <typename T>
void OPCUA_Access::Covert_UA_Scalar_To_Specific(
    T &SourceData_var, int i, const std::vector<UA_Variant> &batchReadVariant) {
  // ELSE UA TPYE MATCH S7 DATA TYPE
  memcpy(&SourceData_var, ((static_cast<T *>(batchReadVariant[i].data))),
         sizeof(T));
}


Result<bool, RichError> OPCUA_Access::Set_UA_To_Read_Normal_Scalar(
    const S7DataType &s7_type, ValueType &dataVar, int i,
    const std::vector<UA_Variant> &batchReadVariant) {
  if (s7_type == S7DataType::BOOL) {
    bool tmp;
    {
      Covert_UA_Scalar_To_Specific(tmp, i, batchReadVariant);
      dataVar = (tmp != 0); // 或者 (tmp ? true : false)
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

  void OPCUA_Access::setAddressMap(
      std::unordered_map<std::string, PhysicalAddress> &map) {
    m_addressMap = map;
  }

  std::optional<int> OPCUA_Access::probeStringLength(int varByteOffset) {
    return std::optional<int>(0);
  }

  void OPCUA_Access::configureClient() {
    if (!m_client_pointer)
      return;

    UA_ClientConfig *config = UA_Client_getConfig(m_client_pointer);
    if (!config)
      return;

    UA_ClientConfig_setDefault(config);

    // 1. 根据现场网络状况调整超时
    // 如果是本地网络（<10ms 延迟）：5 秒足够
    // 如果是远程/4G 网络（>100ms 延迟）：15-30 秒
    config->timeout = 10000; // 10 秒（适中）

    // 2. 会话超时：根据业务空闲时间调整
    // 如果频繁操作：600000 (10分钟)
    // 如果长时间空闲：3600000 (1小时)
    config->requestedSessionTimeout = 600000; // 10分钟

    // 3. 安全通道：稍长于会话超时
    config->secureChannelLifeTime = 600000; // 10分钟

    // 4. 连接检查：根据网络稳定性调整
    // 稳定网络：30-60 秒
    // 不稳定网络：5-10 秒
    config->connectivityCheckInterval = 10000; // 10秒

    // 5. 启用自动重连（工业场景推荐）
    config->noReconnect = false;
    config->noNewSession = false;

    // 6. 添加自定义重连回调
    config->stateCallback =
        [](UA_Client *client, UA_SecureChannelState channelState,
           UA_SessionState sessionState, UA_StatusCode status) {
          // 处理状态变化
          switch (channelState) {
          case UA_SECURECHANNELSTATE_OPEN:
            spdlog::info("Secure channel is open");
            break;
          case UA_SECURECHANNELSTATE_CLOSED:
            spdlog::warn("Secure channel is closed");
            break;
          case UA_SECURECHANNELSTATE_CONNECTING:
            spdlog::debug("Secure channel is connecting");
            break;
          default:
            break;
          }

          switch (sessionState) {
          case UA_SESSIONSTATE_ACTIVATED:
            spdlog::info("Session is activated");
            break;
          case UA_SESSIONSTATE_CLOSED:
            spdlog::warn("Session is closed");
            break;
          case UA_SESSIONSTATE_CREATED:
            spdlog::debug("Session is created");
            break;
          default:
            break;
          }

          if (status != UA_STATUSCODE_GOOD) {
            spdlog::error("Connection status: {}", UA_StatusCode_name(status));
          }
        };
  }

Result<bool, RichError> OPCUA_Access::batchWrite() {
  // 2. 准备写入请求
  UA_WriteRequest request;
  UA_WriteRequest_init(&request);

  WriteResponseGuard guard(this);
  
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
    
    return Result<bool, RichError>(RichError(ss.str()));
  }

  return Result<bool,RichError> (true);
}

template <typename T>
Result<bool, RichError> OPCUA_Access::batchSet_UA_Scalar_StatusCode(
    int nameSpace, PhysicalAddress &var, T &source_var,UA_WriteValue &destValue) {
  //  type->value->request->response
  // CHECK TYPE MATCH
  auto it = s7_to_ua_map.find(var.dataType);
  if (it == s7_to_ua_map.end()) {
    return Result<bool, RichError>(
        RichError("OPCUADataCovert : write type does not match T "));
  }
  const UA_DataType *type = it->second;

  //  INIT WRITE UA_VALUE
  if (destValue.attributeId != UA_ATTRIBUTEID_VALUE) {
    {
      auto result{extractPureNodeIdRobust(var.fullPath)};
      destValue.nodeId =
          UA_NODEID_STRING_ALLOC(std::stoi(result.first), result.second.data());
    }
    destValue.attributeId = UA_ATTRIBUTEID_VALUE;
  }

  //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
  UA_StatusCode status =
      UA_Variant_setScalarCopy(&destValue.value.value, &source_var, type);
  if (status != UA_STATUSCODE_GOOD) {
    UA_WriteValue_clear(&destValue);
    return Result<bool, RichError>(
        RichError("OPCUADataCovert : batchSet_UA_Scalar_StatusCode fail"));
  }
  destValue.value.hasValue = true;
  return Result<bool, RichError>(true);
}

template <>
Result<bool, RichError>
OPCUA_Access::batchSet_UA_Scalar_StatusCode(
    int nameSpace, PhysicalAddress &var, std::string &source_var,UA_WriteValue &destValue) {
  //  type->value->request->response
  //  CHECK TYPE MATCH
  auto it = s7_to_ua_map.find(var.dataType);
  if (it == s7_to_ua_map.end()) {
    return Result<bool, RichError>(
        RichError("OPCUADataCovert : write type does not match T "));
  }
  const UA_DataType *type = it->second;

  //  INIT WRITE UA_VALUE
  if (destValue.attributeId != UA_ATTRIBUTEID_VALUE) {
    {
      auto result{extractPureNodeIdRobust(var.fullPath)};
      destValue.nodeId =
          UA_NODEID_STRING_ALLOC(std::stoi(result.first), result.second.data());
    }
    destValue.attributeId = UA_ATTRIBUTEID_VALUE;
  }

  // 一些 OPC UA 库提供辅助宏
  UA_String uaString = UA_STRING(const_cast<char *>(source_var.c_str()));

  //  COPY RESOURCE TO UA_VARIANT OF UA_WriteValue
  UA_StatusCode status =
      UA_Variant_setScalarCopy(&destValue.value.value, &uaString, type);
  if (status != UA_STATUSCODE_GOOD) {
    UA_WriteValue_clear(&destValue);
    return Result<bool, RichError>(
        RichError("OPCUADataCovert : batchSet_UA_Scalar_StatusCode fail"));
  }

  destValue.value.hasValue = true;
  return Result<bool, RichError>(true);
}

Result<bool, RichError> OPCUA_Access::batchSet_Normal_To_Write_UA_Scalar(
    PhysicalAddress &dataQuality, UA_WriteValue &WriteNode, const ValueType &VariableItem) {

  switch (dataQuality.dataType) {
  case S7DataType::BOOL: {
    bool tmpValue;
    if (!tryGetVariantValue(VariableItem, tmpValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return Result<bool, RichError>(
          RichError{"batchSet_Normal_To_Write_UA_Scalar : type not match"});
    }
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, dataQuality, tmpValue,
                                         WriteNode);
  }
  case S7DataType::BYTE: {
    uint8_t tmpValue;
    if (!tryGetVariantValue(VariableItem, tmpValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return Result<bool, RichError>(
          RichError{"batchSet_Normal_To_Write_UA_Scalar : type not match"});
    }
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, dataQuality, tmpValue,
                                         WriteNode);
  }
  case S7DataType::INT: {
    int16_t tmpValue;
    if (!tryGetVariantValue(VariableItem, tmpValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return Result<bool, RichError>(
          RichError{"batchSet_Normal_To_Write_UA_Scalar : type not match"});
    }
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, dataQuality, tmpValue,
                                         WriteNode);
  }
  case S7DataType::WORD: {
    uint16_t tmpValue;
    if (!tryGetVariantValue(VariableItem, tmpValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return Result<bool, RichError>(
          RichError{"batchSet_Normal_To_Write_UA_Scalar : type not match"});
    }
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, dataQuality, tmpValue,
                                         WriteNode);
  }
  case S7DataType::DWORD:
  case S7DataType::UDINT: {
    uint32_t tmpValue;
   if (!tryGetVariantValue(VariableItem, tmpValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return Result<bool, RichError>(
          RichError{"batchSet_Normal_To_Write_UA_Scalar : type not match"});
    }
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, dataQuality, tmpValue,
                                         WriteNode);
  }
  case S7DataType::DINT: {
    int32_t tmpValue;
    if (!tryGetVariantValue(VariableItem, tmpValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return Result<bool, RichError>(
          RichError{"batchSet_Normal_To_Write_UA_Scalar : type not match"});
    }
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, dataQuality, tmpValue,
                                         WriteNode);
  }
  case S7DataType::REAL: {
    float tmpValue;
     if (!tryGetVariantValue(VariableItem, tmpValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return Result<bool, RichError>(
          RichError{"batchSet_Normal_To_Write_UA_Scalar : type not match"});
    }
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, dataQuality, tmpValue,
                                         WriteNode);
  }
  case S7DataType::STRING: {
    std::string tmpValue;
   if (!tryGetVariantValue(VariableItem, tmpValue)) {
      spdlog::warn("Variant type mismatch for BOOL");
      return Result<bool, RichError>(
          RichError{"batchSet_Normal_To_Write_UA_Scalar : type not match"});
    }
    return batchSet_UA_Scalar_StatusCode(m_nameSpace, dataQuality, tmpValue,
                                         WriteNode);
  }
  default: {
    return Result<bool, RichError>(
        RichError("Read_UA_Variant_From_PLC : s7_type is unknown"));
  }
  }
}
