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
// #include <curl/curl.h>

#define DEBUG_SLICE_CONTENT 0 
#define OUTPUT_TXT_NAME "output.txt"

//forward declaration
struct ProtocolHeader ;
class Sqlite_DB_Manager;
class source_data_parse;

#define MAX_INFLIGHT_NUMBER = 5;
class MqttServer : public virtual mqtt::callback 
{
    public:
        MqttServer(buffer_administrator* buffer_ptr):
        m_buffer_ptr(buffer_ptr) {
        };
        ~MqttServer();

        int createinstance(std::shared_ptr<spdlog::logger> ptr);
        int connectinstance();
        void subscribe();
        int disconnectinstance();
        int config_load();
        int parse_json(std::ifstream &ifs);
        int DecryptSharedData(const std::vector<uint8_t>& ciphertext , const uint8_t iv[16] , std::vector<uint8_t>& plaint, ProtocolHeader &protocolheader);

        void message_arrived(mqtt::const_message_ptr mqtt_msg) override;
        void delivery_complete(mqtt::delivery_token_ptr mqtt_token) override; 

        std::vector<uint8_t> get_file_data_vector(const std::string& db_file_path); 
        int process_string_to_task();
        void start_process_string_to_task();
        void start_send_reponse_to_client();
        void send_reponse_to_client();

        int m_port;
        int m_qos;
        int m_persistence;
        int m_retained;
        int m_max_inflight_number = 1000;
        int m_dynamic_max_inflight_number = 0;

        std::string m_broker;
        std::string m_client_id;
        std::string m_topicName;
        std::string m_username;
        std::string m_password;
        std::string respond_topic = "file/respond";

        mqtt::async_client* m_client;
        mqtt::connect_options m_connOpts;
        mqtt::token_ptr m_token;
        
        source_data_parse* m_parse_info;
        std::queue<mqtt::const_message_ptr> m_message_queue;
        std::queue<std::string> m_responding_queue;
        std::mutex m_message_qeueu_mutex;
        std::mutex m_process_string_mutex;
        std::mutex m_sender_mutex;
        std::mutex m_token_mutex;
        std::mutex m_delivery_mutex;
        std::condition_variable m_process_string_condition;
        std::condition_variable m_sender_condition;
        std::atomic<bool> m_is_active = true;
        buffer_administrator* m_buffer_ptr;

    public:
        class builder {
            public:
            buffer_administrator* buffer_ptr;
            static builder create_builder(){
                return builder();
            }

            MqttServer instance_build()
            {
                return MqttServer(buffer_ptr);
            }

            builder set_inflight_pointer(buffer_administrator* pointer)
            {
                buffer_ptr = pointer;
                return *this;
            }
        };
    private:
};


#endif