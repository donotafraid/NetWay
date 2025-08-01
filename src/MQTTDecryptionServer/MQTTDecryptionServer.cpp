#include "MQTTDecryptionServer/MQTTDecryptionServer.h"
#include "ProtocolHeader/ProtocolHeader.h"

    MqttServer::~MqttServer()
{
    if (m_client->is_connected())
    {
        auto dis_token = m_client->disconnect();
        auto status =dis_token->wait_for(std::chrono::seconds(5));
        if( status != true) 
        {
            std::cerr<<"disconnect failed : "<<dis_token->get_error_message()<<std::endl;
        }
    }
    delete m_client;
    std::cout<<"~MqttServer() called!"<<std::endl;
    m_client = nullptr;
}

int MqttServer::createinstance()
{
    m_client = new mqtt::async_client(m_broker, m_client_id, m_persistence, nullptr);
    m_connOpts.set_clean_session(true);
    m_connOpts.set_keep_alive_interval(500);
    return 0;
}
int MqttServer::connectinstance()
{
    m_token = m_client->connect(m_connOpts);
    m_token->wait();
    if(m_token->is_complete() && !m_token->get_reason_code())
    {
        std::cout<<"connect success!"<<std::endl;
    }
    else
    {
        std::cout<<"connect failed!"<<std::endl;
    }
    return 0;
}
int MqttServer::config_load()
{
    std::ifstream ifs("../config_folder/config_server.json");
    if (!ifs.is_open())
    {
        std::cout<<"open config.json failed!"<<std::endl;
        return -1;
    }

    int result = parse_json(ifs);
    if (result == -1)
    {
        std::cout<<"parse_json failed!"<<std::endl;
        return -1;
    }
    return 0;
}

int MqttServer::parse_json(std::ifstream &ifs)
{
    nlohmann::json config;
    ifs>>config;
    
    std::string broker_address = config["broker_address"];
    if (broker_address == "")
    {
        std::cout<<"broker_address is empty!"<<std::endl;
        return -1;
    }

    std::string client_id = config["client_id"];
    if (client_id == "")
    {
        std::cout<<"client_id is empty!"<<std::endl;
        return -1;
    }

    int persistence_type = config["persistence_type"];
    if (persistence_type < 0)
    {
        std::cout<<"persistence_type is empty!"<<std::endl;
        return -1;
    }

    std::string persistence_context = config["persistence_context"];
    if (persistence_context == ""){
        std::cout<<"persistence_context is empty!"<<std::endl;
        return -1;
    }

    std::string topicName = config["topicName"];
    if (topicName == ""){
        std::cout<<"topicName is empty!"<<std::endl;
        return -1;
    }

    int port = config["port"];
    if (port < 0)
    {
        std::cout<<"port is empty!"<<std::endl;
        return -1;
    }

    m_broker= broker_address;
    m_client_id=  client_id;
    m_port= port;
    m_topicName= topicName;
    m_persistence = persistence_type;
    m_qos = 2;
    return 0;
}

/**
 * @brief Decrypts shared data using AES-256-CBC encryption.
 *
 * @param ciphertext The encrypted data to be decrypted.
 * @param iv Initialization vector for the decryption.
 * @param plaintext Output parameter for the decrypted data.
 * @param header Protocol header containing the AES key.
 * @return true if decryption succeeded, false otherwise.
 */
int MqttServer::DecryptSharedData(const std::vector<uint8_t> &ciphertext, const uint8_t iv[16], std::vector<uint8_t> &plaintext, ProtocolHeader &header)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if(!ctx)
    {
        return false;
    }

    const EVP_CIPHER* cipherType = EVP_aes_256_cbc();
    if(EVP_DecryptInit_ex(ctx,cipherType,nullptr,header.AES_KEY.data(),iv) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    plaintext.resize(ciphertext.size() + EVP_CIPHER_CTX_block_size(ctx));
    int len , plainLen = 0 ; 
    if ( EVP_DecryptUpdate(ctx , plaintext.data() , &len , ciphertext.data() , ciphertext.size()) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    plainLen = len ;

    if( EVP_DecryptFinal_ex(ctx , plaintext.data() + len , &len) != 1)
    {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        std::cerr<<"EVP_EncryptInit_ex failed and error is : "<<err_buf<<std::endl;
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
    plainLen += len ;
    plaintext.resize(plainLen);

    EVP_CIPHER_CTX_free(ctx);
    return true;
}

void MqttServer::message_arrived(mqtt::const_message_ptr mqtt_msg) 
{
    // judge if the message include ProtocolHeader
    if(mqtt_msg->get_payload().size() < sizeof(ProtocolHeader) && mqtt_msg->get_payload().size() != 0)
    {
        std::cout<<"message_arrived: payload size is too small!"<<std::endl;
        return;
    }
}
