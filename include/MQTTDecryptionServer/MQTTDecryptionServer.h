#ifndef MQTTDecryptionServer_H
#define MQTTDecryptionServer_H

#include <vector>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <mutex>
#include "load_config/load_config.h"

#define DEBUG_SLICE_CONTENT 0 
#define OUTPUT_TXT_NAME "output.txt"

//forward declaration
struct ProtocolHeader ;


class MqttServer : public virtual mqtt::callback 
{
    public:
        MqttServer(){};
        ~MqttServer();

        int createinstance();
        int connectinstance();
        int disconnectinstance();
        int subscribe();
        int publish();
        int loop();
        int config_load();
        int parse_json(std::ifstream &ifs);

        int DecryptSharedData(const std::vector<uint8_t>& ciphertext , const uint8_t iv[16] , std::vector<uint8_t>& plaint, ProtocolHeader &protocolheader);
        void message_arrived(mqtt::const_message_ptr mqtt_msg) override;

        int m_port;
        int m_qos;
        int m_persistence;
        bool m_retained;

        std::string m_broker;
        std::string m_client_id;
        std::string m_topicName;
        std::string m_username;
        std::string m_password;

        mqtt::async_client* m_client;
        mqtt::connect_options m_connOpts;
        mqtt::token_ptr m_token;
        
        std::mutex m_mutex;
    private:
};


#endif