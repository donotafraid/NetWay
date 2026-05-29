#pragma once

#include "ActionListener.h"
#include "load_config/load_config.h"
#include "sqlite3.h"
#include "Sqlite_DB/DbService.h"
#include "Rust_error_deal/error_deal.h"
#include "Thread_pool/Thread_pool_define.h"

class IConfigParser;

// 层1：请求外部层 - 定义“我需要什么配置”
struct MqttConfig {
    int m_port;
    int m_qos;
    int m_retained;
    int m_max_inflight_number;
    int keep_alive_interval ;
    bool clean_session ;
    int connection_timeout ;

    std::string m_brokerAddress;
    std::string m_client_id;
    std::string m_topicName;
    std::string m_username;
    std::string m_password;
};

// 层2：功能响应层 - 定义“如何将原始数据转换为配置”
class MqttConfigBuilder {
public:
    // 从键值对构建配置（不关心来源）
    MqttConfig buildFromKeyValue(const std::map<std::string, std::string>& kv);
    
    // 验证配置有效性（业务规则）
    bool validate(const MqttConfig& config);
    
private:
    // 配置转换规则（例如：端口默认1883，clientId自动生成等）
    std::string generateClientIdIfMissing(const std::string& input);
    std::string getValue(const std::map<std::string, std::string> &kv,
                         const std::string &key,
                         const std::string &defaultValue);
    int getIntValue(const std::map<std::string, std::string> &kv,
                    const std::string &key, int defaultValue, int minValue,
                    int maxValue);

    // 辅助函数：去除字符串首尾空白
    static std::string trim(const std::string &str);
    static bool stringToInt(const std::string& str, int& result);
};

// 层3：外部响应层 - 只负责“从某处获取原始数据”
class MqttConfigLoader {
public:
    // 依赖注入：让外部决定用什么解析器和构建器
    MqttConfigLoader(std::unique_ptr<IConfigParser> parser,
                     std::unique_ptr<MqttConfigBuilder> builder);

    bool loadFromFile(const std::string &filePath);

    MqttConfig getConfig() const;

  private:
    std::unique_ptr<IConfigParser> m_DBParser;  // 接口，而非具体类型
    std::unique_ptr<MqttConfigBuilder> m_builder;
    MqttConfig m_config;
};

// 抽象接口（解耦）
class IConfigParser {
public:
  virtual std::map<std::string, std::string>
  parseFile(const std::string &path) = 0;
  virtual std::map<std::string, std::string>
  parseString(const std::string &content) = 0;
};

class JsonParser : public IConfigParser {
public:
  std::map<std::string, std::string>
  parseFile(const std::string &path) override;

  std::map<std::string, std::string>
  parseString(const std::string &content) override;

private:
  // 核心解析逻辑：将JSON字符串转换为map<string, string>
  std::map<std::string, std::string>
  parseJsonString(const std::string &jsonStr);

  // 递归展平嵌套JSON结构
  // 例如：{"mqtt": {"broker": "localhost", "port": 1883}}
  //   -> {"mqtt.broker": "localhost", "mqtt.port": "1883"}
  void flattenJson(const std::string &prefix, const nlohmann::json &j,
                   std::map<std::string, std::string> &output);
  // 可选：支持JSON5语法（注释、尾逗号等）
  // 如果需要JSON5支持，可以改用json5库或预处理去除注释
};

enum class EncryptionMode { CBC, GCM, CTR };
enum class PaddingScheme { PKCS7, NoPadding };

// 加密配置（业务需求，不关心具体实现）
struct EncryptConfig {
    std::array<uint8_t, 32> aes_key;     // AES-256密钥
    std::array<uint8_t, 16> iv;          // 初始化向量
    uint32_t magic;                      // 魔数（用于验证）
    EncryptionMode mode;                 // CBC/GCM/CTR
    PaddingScheme padding;               // PKCS7/NoPadding
};

// 加密结果（包含元数据）
struct EncryptResult {
  std::vector<uint8_t> ciphertext;

  std::array<uint8_t, 16> iv; // 可能动态生成
  uint32_t magic;
  bool success;
  std::string errorMsg;
};

class IEncryptionStrategy {
public:
  virtual ~IEncryptionStrategy() = default;
  virtual EncryptResult encrypt(const std::vector<uint8_t> &plaintext) = 0;
  virtual std::vector<uint8_t> decrypt(const EncryptResult &cipherResult) = 0;
  virtual bool validateKey(const std::array<uint8_t, 32> &key) = 0;
};

// 3. 加密/解密模块
class DataEncryptor {
public:
    explicit DataEncryptor(const EncryptConfig& config);
    std::string encrypt(const std::string& plaintext);
    std::string decrypt(const std::string& ciphertext);
    
private:
    std::vector<uint8_t> m_key;
    EncryptConfig m_config;
};

// 具体的AES加密策略（业务逻辑）
class AesEncryptionStrategy : public IEncryptionStrategy {
public:
    explicit AesEncryptionStrategy(const EncryptConfig& config)
        : m_config(config) {
        validateKey(config.aes_key);
    }
    
    EncryptResult encrypt(const std::vector<uint8_t>& plaintext) override;
    
    std::vector<uint8_t> decrypt(const EncryptResult& cipherResult) override; 
    
    bool validateKey(const std::array<uint8_t, 32>& key) override; 
    
private:
    EncryptConfig m_config;
    std::vector<uint8_t> generateRandomIV();
    std::vector<uint8_t>
    aesDecrypt(const std::vector<uint8_t> &ciphertext,
               std::array<uint8_t, 32> aes_key, // AES-256密钥
               std::array<uint8_t, 16> iv);     // 初始化向量);
    std::vector<uint8_t>
    aesCbcEncrypt(const std::vector<uint8_t> &ciphertext,
                  std::array<uint8_t, 32> aes_key, // AES-256密钥
                  std::array<uint8_t, 16> iv);
    std::vector<uint8_t>
    aesGcmEncrypt(const std::vector<uint8_t> &ciphertext,
                  std::array<uint8_t, 32> aes_key, // AES-256密钥
                  std::array<uint8_t, 16> iv);
};

// 4. 文件分片模块
class FileSharder {
public:
    std::vector<std::string> splitFile(const std::string& filePath, size_t shardSize);
private:
    IEncryptionStrategy *m_Encrypt = nullptr;
};


// 5. 消息队列管理
class MessageQueueManager {
public:
    void pushMessage(const mqtt::const_message_ptr& msg);
    bool popMessage(mqtt::const_message_ptr& msg);
    size_t size() const;
    void clear();
    
private:
  std::queue<mqtt::const_message_ptr> m_message_queue;

  mutable std::mutex m_mutex;
};

// ==================== 1. 外部响应层 ====================
// 专门处理 MQTT 通信
class MqttTransport : public virtual mqtt::callback {
public:
  explicit MqttTransport(const MqttConfig &config);
  explicit MqttTransport();
  ~MqttTransport();

  // 连接管理
  bool connect();
  bool disconnect();
  bool isConnected() const;

  // 发布/订阅
  bool publish(const std::string &payload);
  bool subscribe(const std::string &topic = 0, int qos = 1);
  bool unsubscribe(const std::string &topic = 0);

  // 回调接口
  void message_arrived(mqtt::const_message_ptr msg) override;
  void delivery_complete(mqtt::delivery_token_ptr token) override;

  // 设置消息处理器（依赖注入）
  void setMessageHandler(std::function<void(const mqtt::const_message_ptr &)> handler);
  void processMessageQueue();
  size_t getMessageQueueSize() const;
  void clearMessageQueue();

private:
  std::unique_ptr<mqtt::async_client> m_client;
  std::queue<mqtt::const_message_ptr> m_message_queue;
  mqtt::connect_options m_connOpts;
  std::function<void(const mqtt::const_message_ptr &)> m_messageHandler;
  std::atomic<bool> m_connected{false};
  MqttConfig m_config;
};

// ==================== 2. 功能响应层 ====================
// 处理业务逻辑
class MqttBusinessHandler {
public:
  explicit MqttBusinessHandler();

  // 处理接收到的消息
  Result<bool, RichError> handleIncomingMessage(const std::string &topic,
                                                const std::string &payload ,
                                                int qos , int remained);

  // 处理发送请求
  Result<bool,RichError> handleSendRequest(const std::string &requestFilePath);

  // 处理文件分片
  std::vector<std::string> splitFileData(const std::string &filePath,
                                         size_t chunkSize);

  // 处理订阅请求
  Result<bool, RichError> handleSubscribeRequest(const std::string &topic);

  // 业务状态查询
  Result<bool,RichError> getStatus() const;

private:
  // 内部业务逻辑
  void processFileShard();
  void updateTransmitProgress(const std::string &fileId, int progress);
  bool validateMessage(const mqtt::const_message_ptr &msg);

  SqlBussinessHandler *m_dbBussinessHanlder = nullptr;
  DataEncryptor *m_encryptor = nullptr;
  MqttTransport *m_mqttSport = nullptr;
  FileSharder *m_FileSharder = nullptr;
};

// ==================== 3. 请求外部层 ====================
// 对外提供的服务接口
class MqttService {
public:
  MqttService(MqttBusinessHandler *handler);

  // 对外API
  Result<bool,RichError> sendFile(const std::string &file_path);
  Result<bool,RichError> subscribeTopic(const std::string &topic);
  Result<bool,RichError> getTransmitProgress(const std::string &fileId);

  // // 异步回调注册
  // void setProgressCallback(ProgressCallback callback);
  // void setErrorCallback(ErrorCallback callback);

private:
  MqttBusinessHandler *m_handler;
  // ProgressCallback m_progressCallback;
};

