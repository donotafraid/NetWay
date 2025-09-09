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
class Sqlite_DB_Manager;
class Sqlite_DB_process_transmission_and_write_to_file;

class MqttServer : public virtual mqtt::callback 
{
    public:
        MqttServer(){};
        ~MqttServer();

        int createinstance(std::shared_ptr<spdlog::logger> ptr);
        int connectinstance();
        void subscribe();
        int disconnectinstance();
        int config_load();
        int parse_json(std::ifstream &ifs);
        int DecryptSharedData(const std::vector<uint8_t>& ciphertext , const uint8_t iv[16] , std::vector<uint8_t>& plaint, ProtocolHeader &protocolheader);

        void message_arrived(mqtt::const_message_ptr mqtt_msg) override;
        std::vector<uint8_t> get_file_data_vector(const std::string& db_file_path); 
        int process_string_to_task();
        void start_process_string_to_task();
        void start_send_reponse_to_client();
        void send_reponse_to_client();

        int m_port;
        int m_qos;
        int m_persistence;
        bool m_retained;

        std::string m_broker;
        std::string m_client_id;
        std::string m_topicName;
        std::string m_username;
        std::string m_password;
        std::string respond_topic = "file/respond";

        mqtt::async_client* m_client;
        mqtt::connect_options m_connOpts;
        mqtt::token_ptr m_token;
        
        Sqlite_DB_process_transmission_and_write_to_file* m_transmission_info;
        std::queue<mqtt::const_message_ptr> m_message_queue;
        std::queue<std::string> m_responding_queue;
        std::mutex m_message_qeueu_mutex;
        std::mutex m_process_string_mutex;
        std::mutex m_sender_mutex;
        std::condition_variable m_process_string_condition;
        std::condition_variable m_sender_condition;
        std::atomic<bool> m_is_active = true;
        
    private:
};


#endif