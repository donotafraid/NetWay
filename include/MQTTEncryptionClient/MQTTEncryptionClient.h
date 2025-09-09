#ifndef MQTTEncryptionClient_H
#define MQTTEncryptionClient_H

#include "ActionListener.h"
#include "load_config/load_config.h"
#include "sqlite3.h"

//forward declaration
struct ProtocolHeader ;
class Sqlite_DB_write_file;

class MqttClient: public virtual mqtt::callback
{
    public:
        MqttClient(){};
        ~MqttClient();

        int createinstance(Sqlite_DB_write_file* m_sqlite_DB_write_file_ptr);
        int connectinstance();
        int disconnectinstance();
        int publish();
        void subscribe_respond_topic();
        int loop();
        int config_load();
        int parse_json(std::ifstream &ifs);

        int ReadFileAndShard();
        int constructProtocolHeader();
        int EncryptSharedData();

        void SendSliceData(const std::string& payload_protocolheader_ciphertext);
        void message_arrived(mqtt::const_message_ptr mqtt_msg) override;
        void transmit_message_to_sqlite();

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

        std::queue<mqtt::const_message_ptr> m_received_messages_queue;
        std::queue<mqtt::const_message_ptr> m_tmp_received_messages_queue;
        std::mutex m_received_queue_mutex;
        Sqlite_DB_write_file* m_sqlite_DB_write_file_ptr; 
    private:
};
#endif