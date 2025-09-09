#include "MQTTEncryptionClient/MQTTEncryptionClient.h"
#include "Sqlite_DB/Sqlite_DB.h"
MqttClient::~MqttClient()
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
    std::cout<<"~MqttClient() called!"<<std::endl;
    m_client = nullptr;
}

int MqttClient::createinstance(Sqlite_DB_write_file* m_sqlite_DB_write_file)
{
    m_client = new mqtt::async_client(m_broker, m_client_id);
    m_connOpts.set_clean_session(true);
    m_connOpts.set_keep_alive_interval(500);
    m_sqlite_DB_write_file_ptr = m_sqlite_DB_write_file;
    return 0;
}
int MqttClient::connectinstance()
{
    auto m_token = m_client->connect(m_connOpts);
    m_token->wait();
    if(m_token->is_complete()&& !m_token->get_return_code())
    {
        std::cout<<"client connect success!"<<std::endl;
    }
    else
    {
        std::cout<<"connect failed!"<<std::endl;
    }
    m_client->set_callback(*this);
    subscribe_respond_topic();
    return 0;
}

void MqttClient::subscribe_respond_topic()
{
    auto m_token = m_client->subscribe("file/respond", m_qos);
    m_token->wait();
    if(m_token->is_complete()&& !m_token->get_return_code())
    {
        std::cout<<"client subscribe success!"<<std::endl;
    }
    else
    {
        std::cout<<"connect subscribe failed!"<<std::endl;
    }
}

void MqttClient::message_arrived(mqtt::const_message_ptr mqtt_msg)
{
    if(mqtt_msg->get_topic()!= m_topicName && mqtt_msg->get_topic().find("ack")== std::string::npos)
    {
        m_received_messages_queue.push(mqtt_msg);
    }

    #if DEBUG_TEST == true
    std::cout<<"mqttserver respond message arrived!"<<std::endl;
    #endif
}

int MqttClient::config_load()
{
    std::ifstream ifs("../config_folder/config.json");
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

int MqttClient::parse_json(std::ifstream &ifs)
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
    m_qos = 1;
    m_retained = false;
    return 0;
}

void MqttClient::SendSliceData(const std::string& proto_msg)
{
    // auto result_promise = std::make_shared<std::promise<int>>();
    // auto result_future = result_promise->get_future();

    m_msg = mqtt::make_message(m_topicName, 
    proto_msg.data(), 
    proto_msg.size(),
    m_qos, 
    m_retained);

    //build monitor
    // ActionListener listener(result_promise);
    {
    auto token = m_client->publish(m_msg);
    // token->set_action_callback(listener);
    }
}


void MqttClient::transmit_message_to_sqlite()
{
    {
        std::lock_guard<std::mutex> lock(m_received_queue_mutex);
        m_received_messages_queue.swap(m_tmp_received_messages_queue);
    }

    m_sqlite_DB_write_file_ptr->write_message_in_db_subordinate_file_in_batch(m_tmp_received_messages_queue);
}