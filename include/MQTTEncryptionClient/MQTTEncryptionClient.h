#ifndef MQTTEncryptionClient_H
#define MQTTEncryptionClient_H

#include "ActionListener.h"
#include "load_config/load_config.h"
#include "sqlite3.h"

//forward declaration
struct ProtocolHeader ;

class MqttClient
{
    public:
        MqttClient(){};
        ~MqttClient();

        int createinstance();
        int connectinstance();
        int disconnectinstance();
        int subscribe();
        int publish();
        int loop();
        int config_load();
        int parse_json(std::ifstream &ifs);

        int ReadFileAndShard();
        int constructProtocolHeader();
        int EncryptSharedData();

        int SendSliceData(const std::string& payload_protocolheader_ciphertext);

        int m_port;
        int m_qos;
        bool m_retained;

        std::string m_broker;
        std::string m_client_id;
        std::string m_topicName;
        std::string m_username;
        std::string m_password;
        std::string m_payload_message;

        mqtt::async_client* m_client; 
        mqtt::connect_options m_connOpts;
        mqtt::message_ptr m_msg;
    private:
        std::mutex m_mutex;
};
#endif