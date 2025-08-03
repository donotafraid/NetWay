#include "MQTTEncryptionClient/MQTTEncryptionClient.h"
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

int MqttClient::createinstance()
{
    m_client = new mqtt::async_client(m_broker, m_client_id);
    m_connOpts.set_clean_session(true);
    m_connOpts.set_keep_alive_interval(500);
    return 0;
}
int MqttClient::connectinstance()
{
    auto m_token = m_client->connect(m_connOpts);
    m_token->wait();
    if(m_token->is_complete()&& !m_token->get_return_code())
    {
        std::cout<<"connect success!"<<std::endl;
    }
    else
    {
        std::cout<<"connect failed!"<<std::endl;
    }
    return 0;
}


int MqttClient::loop()
{
    std::this_thread::sleep_for(std::chrono::seconds(5));
    return 0;
}
int MqttClient::publish()
{
    m_msg  = mqtt::make_message(m_topicName, m_payload_message);
    m_msg->set_qos(m_qos);
    auto m_token = m_client->publish(m_msg);
    m_token->wait();
    return 0;
}

int MqttClient::ReadFileAndShard()
{
    return 0;
}

int MqttClient::constructProtocolHeader()
{
    return 0;
}

int MqttClient::EncryptSharedData()
{
    return 0;
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
    m_qos = 2;
    m_retained = false;
    return 0;
}

int MqttClient::SendSliceData(const std::string& proto_msg)
{
    auto result_promise = std::make_shared<std::promise<int>>();
    auto result_future = result_promise->get_future();

    m_msg = mqtt::make_message(m_topicName, 
    proto_msg.data(), 
    proto_msg.size(),
    m_qos, 
    m_retained);

    //build monitor
    ActionListener listener(result_promise);
    {
    // std::lock_guard<std::mutex> lock(m_mutex);
    auto token = m_client->publish(m_msg);
    token->set_action_callback(listener);
    }

    if(result_future.get()==(-1))
    {
        std::cerr<<"Error : Failed to publish message in SendSliceData !\n";
    }
    return result_future.get();
}
