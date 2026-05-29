#include "MQTTEncryptionClient/MQTTClient.h"

//  MqttConfigBuilder----------------------------------------------
MqttConfig MqttConfigBuilder::buildFromKeyValue(
    const std::map<std::string, std::string> &kv) {
  MqttConfig config;

  // 1. 字符串类型配置项（直接映射）
  config.m_brokerAddress = getValue(kv, "broker_address", "localhost");
  config.m_client_id = getValue(kv, "client_id", generateClientIdIfMissing(""));
  config.m_topicName = getValue(kv, "topic_name", "test/topic");
  config.m_username = getValue(kv, "username", "");
  config.m_password = getValue(kv, "password", "");

  // 2. 整数类型配置项（带转换和范围检查）
  config.m_port = getIntValue(kv, "port", 1883, 1, 65535);
  config.m_qos = getIntValue(kv, "qos", 1, 0, 2);           // QoS只能是0,1,2
  config.m_retained = getIntValue(kv, "retained", 0, 0, 1); // 布尔值用0/1表示
  config.m_max_inflight_number = getIntValue(kv, "max_inflight", 10, 1, 65535);

  return config;
}

std::string
MqttConfigBuilder::getValue(const std::map<std::string, std::string> &kv,
                            const std::string &key,
                            const std::string &defaultValue) {
  auto it = kv.find(key);
  return (it != kv.end() && !it->second.empty()) ? it->second : defaultValue;
}

// 生成默认clientId（如果用户未提供）
std::string
MqttConfigBuilder::generateClientIdIfMissing(const std::string &input) {
  if (!input.empty()) {
    return input;
  }
  // 生成规则：前缀 + 时间戳 + 随机数
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();
  int randomNum = rand() % 10000;
  return "mqtt_client_" + std::to_string(timestamp) + "_" +
         std::to_string(randomNum);
}

// 辅助函数：去除字符串首尾空白
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, last - first + 1);
}

// 辅助函数：字符串转整数（带错误检查）
static bool stringToInt(const std::string& str, int& result) {
    if (str.empty()) {
        return false;
    }
    
    try {
        size_t pos;
        result = std::stoi(str, &pos);
        // 确保整个字符串都被解析（没有多余字符）
        return pos == str.length();
    } catch (const std::invalid_argument&) {
        return false;
    } catch (const std::out_of_range&) {
        return false;
    }
}

int getIntValue(const std::map<std::string, std::string> &kv,
                const std::string &key, int defaultValue, int minValue,
                int maxValue) {
  // 1. 查找键是否存在
  auto it = kv.find(key);
  if (it == kv.end()) {
    return defaultValue;
  }

  // 2. 获取值并去除空白字符
  std::string valueStr = trim(it->second);

  // 3. 空字符串处理
  if (valueStr.empty()) {
    return defaultValue;
  }

  // 4. 字符串转整数
  int intValue;
  if (!stringToInt(valueStr, intValue)) {
    // 转换失败，可以根据需要抛出异常或返回默认值
    // 这里选择抛出异常以便上层捕获处理
    std::ostringstream oss;
    oss << "Invalid integer value for key '" << key << "': '" << it->second
        << "'";
    throw std::invalid_argument(oss.str());
  }

  // 5. 范围检查
  if (intValue < minValue || intValue > maxValue) {
    std::ostringstream oss;
    oss << "Value " << intValue << " for key '" << key << "' is out of range ["
        << minValue << ", " << maxValue << "]";
    throw std::out_of_range(oss.str());
  }

  return intValue;
}

// JsonParser-------------------------------------------------------
std::map<std::string, std::string>
JsonParser::parseFile(const std::string &path) {
  std::map<std::string, std::string> result;

  // 1. 检查文件是否存在并打开
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("Cannot open JSON file: " + path);
  }

  // 2. 读取文件内容
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();
  file.close();

  if (content.empty()) {
    throw std::runtime_error("JSON file is empty: " + path);
  }

  // 3. 解析JSON内容
  return parseJsonString(content);
}

std::map<std::string, std::string>
JsonParser::parseString(const std::string &content) {
  if (content.empty()) {
    throw std::runtime_error("JSON string is empty");
  }

  return parseJsonString(content);
}

std::map<std::string, std::string>
JsonParser::parseJsonString(const std::string &jsonStr) {
  std::map<std::string, std::string> result;

  try {
    // 1. 解析JSON
    nlohmann::json j = nlohmann::json::parse(jsonStr);

    // 2. 展平JSON对象（支持嵌套）
    flattenJson("", j, result);

  } catch (const nlohmann::json::parse_error &e) {
    throw std::runtime_error(std::string("JSON parse error: ") + e.what());
  } catch (const std::exception &e) {
    throw std::runtime_error(std::string("Unexpected error: ") + e.what());
  }

  return result;
}

// 递归展平嵌套JSON结构
// 例如：{"mqtt": {"broker": "localhost", "port": 1883}}
//   -> {"mqtt.broker": "localhost", "mqtt.port": "1883"}
void JsonParser::flattenJson(const std::string &prefix, const nlohmann::json &j,
                             std::map<std::string, std::string> &output) {

  if (j.is_object()) {
    // 处理JSON对象
    for (auto &[key, value] : j.items()) {
      std::string newKey = prefix.empty() ? key : prefix + "." + key;
      flattenJson(newKey, value, output);
    }
  } else if (j.is_array()) {
    // 处理JSON数组
    for (size_t i = 0; i < j.size(); ++i) {
      std::string arrayKey = prefix + "[" + std::to_string(i) + "]";
      flattenJson(arrayKey, j[i], output);
    }
  } else {
    // 处理基本类型（string, number, boolean, null）
    std::string value;
    if (j.is_string()) {
      value = j.get<std::string>();
    } else if (j.is_number_integer()) {
      value = std::to_string(j.get<int>());
    } else if (j.is_number_float()) {
      value = std::to_string(j.get<float>());
    } else if (j.is_boolean()) {
      value = j.get<bool>() ? "true" : "false";
    } else if (j.is_null()) {
      value = ""; // null值转为空字符串
    } else {
      // 其他类型转为字符串
      value = j.dump();
    }

    output[prefix] = value;
  }
}

// MqttConfigLoader-----------------------------------------------
bool MqttConfigLoader::loadFromFile(const std::string &filePath) {
  auto rawData = m_DBParser->parseFile(filePath);     // 层3：获取原始数据
  m_config = m_builder->buildFromKeyValue(rawData); // 层2：转换
  return m_builder->validate(m_config);             // 层2：验证
}

MqttConfig MqttConfigLoader::getConfig() const { return m_config; }

// DataEncryptor--------------------------------------------------


// AesEncryptionStrategy-----------------------------------------------------------
EncryptResult
AesEncryptionStrategy::encrypt(const std::vector<uint8_t> &plaintext)  {
  EncryptResult result{{}, {},  0, false, ""};
  // int rc = RAND_bytes(result.AesKey.data(), sizeof(result.AesKey));
  // if(!rc)
  // {
  //   return result;
  // }

  result.magic = m_config.magic;
  result.iv = m_config.iv;

  try {
    // 业务规则1：如果IV未指定，动态生成
    if(m_config.iv.empty())
    {
        return EncryptResult{{},{},0,false,"iv is empty"};
    }

    // 业务规则2：根据模式选择具体算法
    switch (m_config.mode) {
    case EncryptionMode::CBC:
      result.ciphertext = aesCbcEncrypt(plaintext, m_config.aes_key, m_config.iv);
      break;
    case EncryptionMode::GCM:
      result.ciphertext = aesGcmEncrypt(plaintext, m_config.aes_key, m_config.iv);
      break;
    default:
      throw std::runtime_error("Unsupported mode");
    }

    result.success = true;
  } catch (const std::exception &e) {
    result.success = false;
    result.errorMsg = e.what();
  }

  return result;
}

std::vector<uint8_t>
AesEncryptionStrategy::decrypt(const EncryptResult &cipherResult)  {
  // 业务规则3：验证魔数
  if (cipherResult.magic != m_config.magic) {
    throw std::runtime_error("Magic number mismatch");
  }

  std::array<uint8_t, 16> iv; // 初始化向量
  // use diffent decrypt code in different condition 
  if(m_config.iv.empty())
  {
    return aesDecrypt(cipherResult.ciphertext, m_config.aes_key, m_config.iv);
  }
  else
  {
    size_t copySize = std::min(cipherResult.ciphertext.size(), iv.size());
    std::copy_n(cipherResult.ciphertext.begin(), copySize, iv.begin());
    return aesDecrypt(cipherResult.ciphertext, m_config.aes_key, iv);
  }

}

bool AesEncryptionStrategy::validateKey(
    const std::array<uint8_t, 32> &key) {
  // 业务规则5：密钥强度检查
  int zeroCount = std::count(key.begin(), key.end(), 0);
  if (zeroCount > 28) { // 太弱
    throw std::runtime_error("Weak encryption key");
  }
  return true;
}

std::vector<uint8_t> AesEncryptionStrategy::aesDecrypt(const std::vector<uint8_t> &ciphertext,
               std::array<uint8_t, 32> aes_key, // AES-256密钥
               std::array<uint8_t, 16> iv)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if(!ctx)
    {
        return std::vector<uint8_t>{};
    }

    const EVP_CIPHER* cipherType = EVP_aes_256_cbc();
    if(EVP_DecryptInit_ex(ctx,cipherType,nullptr,aes_key.data(),iv.data()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return std::vector<uint8_t>{};
    }

    std::vector<uint8_t> plaintext{};
    plaintext.resize(ciphertext.size() + EVP_CIPHER_CTX_block_size(ctx));
    int len , plainLen = 0 ; 
    if ( EVP_DecryptUpdate(ctx , plaintext.data() , &len , ciphertext.data() , ciphertext.size()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return std::vector<uint8_t>{};
    }
    plainLen = len ;

    if( EVP_DecryptFinal_ex(ctx , plaintext.data() + len , &len) != 1)
    {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        std::cerr<<"EVP_EncryptInit_ex failed and error is : "<<err_buf<<std::endl;
        EVP_CIPHER_CTX_free(ctx);
        return std::vector<uint8_t>{};
    }
    plainLen += len ;
    plaintext.resize(plainLen);

    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

//  FileSharder-----------------------------------------
std::vector<std::string>
FileSharder::splitFile(const std::string &sourcefile_path, size_t shardSize) {
  request_message tmp_msg;
  std::vector<std::string> fileSliceVector{};

  // 0. 打开并读取文件
  std::ifstream file(sourcefile_path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Error: Cannot open file " << sourcefile_path << std::endl;
    return {};
  }

  // 1.获取文件大小
  file.seekg(0, std::ios::end);
  size_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  // 2. 创建 std::vector<uint8_t> 对象并读取整个文件内容
  std::vector<uint8_t> file_data(file_size);
  file.read(reinterpret_cast<char *>(file_data.data()), file_size);
  file.close();

  // 3. 计算总切片数
  size_t total_slices = (file_size + shardSize - 1) / shardSize;
  std::cout << "Total slices: " << total_slices << std::endl;

  // 4. 分配file_id
  std::array<uint8_t, 16> file_id;
  {
    boost::uuids::random_generator Generated_uuid;
    boost::uuids::uuid uuid_ptr = Generated_uuid();
    std::copy(uuid_ptr.begin(), uuid_ptr.end(), file_id.begin());
  }

  // 5. 为每个切片创建加密任务
  for (size_t slice_index = 0; slice_index < total_slices; ++slice_index) {
    size_t slice_start = slice_index * shardSize;
    size_t slice_end = std::min(slice_start + shardSize, file_size);

    // 提取切片数据
    std::vector<uint8_t> slice(file_data.begin() + slice_start,
                               file_data.begin() + slice_end);

    {
      EncryptResult result = m_Encrypt->encrypt(slice);
      // RAND_bytes(header.AES_KEY.data(), sizeof(header.AES_KEY));
      if (!result.success) {
        return {};
      }

      TestMsg msg;
      {
        msg.set_file_id(file_id.data(), file_id.size());
        msg.set_slice_index(slice_index);
        msg.set_ciphertext(result.ciphertext.data(), result.ciphertext.size());
      }
      std::string msg_string;
      msg.SerializeToString(&msg_string);

      fileSliceVector.push_back(msg_string);
    }
  }
  return fileSliceVector;
}

// MqttService------------------------------------------------------
Result<bool, RichError> MqttService::sendFile(const std::string &file_path) {
  if(file_path.empty())
  {
    return Result<bool, RichError>{RichError{"file_path is empty"}};
  }
  return m_handler->handleSendRequest(file_path);
}

Result<bool, RichError> MqttService::subscribeTopic(const std::string &topic) {
  if (topic.empty()) {
    return Result<bool, RichError>{RichError{"topic is empty"}};
  }
  return m_handler->handleSubscribeRequest(topic);
};

Result<bool, RichError>
MqttService::getTransmitProgress(const std::string &fileId) {
  if (fileId.empty()) {
    return Result<bool, RichError>{RichError{"file_id is empty"}};
  }
  return m_handler->getStatus();
};

// // void MqttClient::SendSliceData(const std::string& proto_msg)
// // {
// //     // auto result_promise = std::make_shared<std::promise<int>>();
// //     // auto result_future = result_promise->get_future();
// //     m_msg = mqtt::make_message(m_topicName, 
// //     proto_msg.data(), 
// //     proto_msg.size(),
// //     m_qos, 
// //     m_retained);
// //     //build monitor
// //     // ActionListener listener(result_promise);
// //     {
// //     auto token = m_client->publish(m_msg);
// //     token->wait();
// //     if(token->is_complete())
// //     {
// //         std::cout<<"send message success!"<<std::endl;
// //     }
// //     else {
// //         std::cout<<"send message failed!"<<std::endl;
// //     }
// //     // token->set_action_callback(listener);
// //     }
// // }
// //   }

// MqttBusinessHandler--------------------------------------------- 

 // 处理接收到的消息
Result<bool, RichError>
MqttBusinessHandler::handleIncomingMessage(const std::string &topic,
                                           const std::string &payload, int qos,
                                           int remained) {
  return Result<bool, RichError>(m_mqttSport->publish(payload));
}

// 处理发送请求
Result<bool, RichError> MqttBusinessHandler::handleSendRequest(const std::string &requestFilePath) {
  std::vector<std::string> data_Vector{
      this->splitFileData(requestFilePath, SLICE_SIZE)};
      if(data_Vector.empty())
      {
        return Result<bool, RichError>(RichError{"slice data Vector is empty"});
      }
      for(auto &it : data_Vector)
      {
        this->handleIncomingMessage("", "", 0, 0);
      }
      return Result<bool, RichError>(true);
}

// 处理文件分片
std::vector<std::string>
MqttBusinessHandler::splitFileData(const std::string &filePath,
                                   size_t chunkSize) {
  return m_FileSharder->splitFile(filePath, chunkSize);
}

// 处理订阅请求
Result<bool, RichError> MqttBusinessHandler::handleSubscribeRequest(const std::string &topic) {
  return m_mqttSport->subscribe(topic);
}

// 业务状态查询
Result<bool, RichError> MqttBusinessHandler::getStatus() const {
  return Result<bool, RichError>(false);
}

// MqttTransport------------------------------------------
MqttTransport::MqttTransport() : m_client(nullptr), m_connected(false) {
  // 默认配置
  m_config.m_brokerAddress = "tcp://localhost:1883";
  m_config.m_client_id = "mqtt_client_" + std::to_string(time(nullptr));
  m_config.m_topicName = "";
  m_config.keep_alive_interval = 20;
  m_config.clean_session = true;
  m_config.connection_timeout = 30;
}

MqttTransport::MqttTransport(const MqttConfig &config)
    : m_config(config), m_connected(false) {}

MqttTransport::~MqttTransport() { disconnect(); }

// ==================== 连接管理 ====================

bool MqttTransport::connect() {
  try {
    if (m_connected) {
      std::cout << "Already connected" << std::endl;
      return true;
    }

    // 创建客户端
    m_client = std::make_unique<mqtt::async_client>(m_config.m_brokerAddress,
                                                    m_config.m_client_id);

    // 设置回调
    m_client->set_callback(*this);

    // 配置连接选项
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(m_config.keep_alive_interval);
    connOpts.set_clean_session(m_config.clean_session);
    connOpts.set_automatic_reconnect(true);

    if (!m_config.m_username.empty()) {
      connOpts.set_user_name(m_config.m_username);
      connOpts.set_password(m_config.m_password);
    }

    std::cout << "Connecting to MQTT broker: " << m_config.m_brokerAddress
              << std::endl;

    // 同步连接
    auto token = m_client->connect(connOpts);
    token->wait();

    m_connected = token->is_complete();
    if (m_connected) {
      std::cout << "Connected successfully, Client ID: " << m_config.m_client_id
                << std::endl;
    }

    return m_connected;
  } catch (const mqtt::exception &e) {
    std::cerr << "MQTT connection error: " << e.what() << std::endl;
    return false;
  }
}

bool MqttTransport::disconnect() {
  try {
    if (m_client && m_connected) {
      std::cout << "Disconnecting from MQTT broker..." << std::endl;
      auto token = m_client->disconnect();
      token->wait();
      m_connected = false;
      std::cout << "Disconnected successfully" << std::endl;
    }
    return true;
  } catch (const mqtt::exception &e) {
    std::cerr << "MQTT disconnect error: " << e.what() << std::endl;
    return false;
  }
}

bool MqttTransport::isConnected() const {
  return m_connected && m_client && m_client->is_connected();
}

// ==================== 发布/订阅 ====================

bool MqttTransport::publish(const std::string &payload) {
  if (!isConnected()) {
    std::cerr << "Not connected, cannot publish" << std::endl;
    return false;
  }

  try {
    auto message =
        std::make_shared<mqtt::message>(m_config.m_topicName, payload, m_config.m_qos, m_config.m_retained);
    auto token = m_client->publish(message);
    token->wait_for(std::chrono::seconds(5));

    if (token->is_complete()) {
      std::cout << "Published message to topic: " <<m_config.m_topicName << ", QOS: " << m_config.m_qos
                << ", Retained: " << m_config.m_retained << std::endl;
      return true;
    }
    return false;
  } catch (const mqtt::exception &e) {
    std::cerr << "Publish error: " << e.what() << std::endl;
    return false;
  }
}

bool MqttTransport::subscribe(const std::string &topic, int qos) {
  if (!isConnected()) {
    std::cerr << "Not connected, cannot subscribe" << std::endl;
    return false;
  }

  if (topic.empty()) {
    std::cerr << "Topic cannot be empty" << std::endl;
    return false;
  }

  try {
    std::cout << "Subscribing to topic: " << topic << " with QOS: " << qos
              << std::endl;
    auto token = m_client->subscribe(topic, qos);
    token->wait();

    bool success = token->is_complete();
    if (success) {
      std::cout << "Subscribed successfully to: " << topic << std::endl;
    }
    return success;
  } catch (const mqtt::exception &e) {
    std::cerr << "Subscribe error: " << e.what() << std::endl;
    return false;
  }
}

bool MqttTransport::unsubscribe(const std::string &topic) {
  if (!isConnected()) {
    std::cerr << "Not connected, cannot unsubscribe" << std::endl;
    return false;
  }

  if (topic.empty()) {
    std::cerr << "Topic cannot be empty" << std::endl;
    return false;
  }

  try {
    std::cout << "Unsubscribing from topic: " << topic << std::endl;
    auto token = m_client->unsubscribe(topic);
    token->wait();

    bool success = token->is_complete();
    if (success) {
      std::cout << "Unsubscribed successfully from: " << topic << std::endl;
    }
    return success;
  } catch (const mqtt::exception &e) {
    std::cerr << "Unsubscribe error: " << e.what() << std::endl;
    return false;
  }
}

// ==================== 回调接口 ====================

void MqttTransport::message_arrived(mqtt::const_message_ptr msg) {
  if (!msg)
    return;

  std::cout << "Message arrived - Topic: " << msg->get_topic()
            << ", Payload: " << msg->to_string() << ", QOS: " << msg->get_qos()
            << std::endl;

  // 存入队列
  m_message_queue.push(msg);

  // 如果设置了自定义处理器，直接调用
  if (m_messageHandler) {
    m_messageHandler(msg);
  }
}

void MqttTransport::delivery_complete(mqtt::delivery_token_ptr token) {
  if (token) {
    std::cout << "Delivery complete for message: " << token->get_message_id()
              << std::endl;
  }
}

// ==================== 消息处理 ====================

void MqttTransport::setMessageHandler(
    std::function<void(const mqtt::const_message_ptr &)> handler) {
  m_messageHandler = std::move(handler);
}

void MqttTransport::processMessageQueue() {
  while (!m_message_queue.empty()) {
    auto msg = m_message_queue.front();
    m_message_queue.pop();

    if (m_messageHandler) {
      m_messageHandler(msg);
    }
  }
}

size_t MqttTransport::getMessageQueueSize() const {
  return m_message_queue.size();
}

void MqttTransport::clearMessageQueue() {
  while (!m_message_queue.empty()) {
    m_message_queue.pop();
  }
}