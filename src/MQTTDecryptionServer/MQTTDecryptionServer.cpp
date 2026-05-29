#include "MQTTDecryptionServer/MQTTDecryptionServer.h"
#include "ProtocolHeader/ProtocolHeader.h"
// #include "Sqlite_DB/Sqlite_DB.h"

    MqttServer::~MqttServer()
{
    if (m_client != nullptr && m_client->is_connected())
    {
        auto dis_token = m_client->disconnect();
        auto status =dis_token->wait_for(std::chrono::seconds(5));
        if( status != true) 
        {
            std::cerr<<"disconnect failed : "<<dis_token->get_error_message()<<std::endl;
        }
    }

    m_is_active.store(false,std::memory_order_seq_cst);
    m_sender_condition.notify_all();
    m_process_string_condition.notify_all();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    delete m_client;
    delete m_parse_info;
    std::cout<<"~MqttServer() called!"<<std::endl;
    m_client = nullptr;
    m_parse_info = nullptr;
}

int MqttServer::createinstance(std::shared_ptr<spdlog::logger> ptr)
{
   
    mqtt::create_options m_connOpts_test;
    m_connOpts_test.set_send_while_disconnected(false);
    m_connOpts_test.set_max_buffered_messages(1000);
    m_connOpts_test.set_delete_oldest_messages(true);
    m_client = new mqtt::async_client(m_broker,m_client_id,m_connOpts_test,nullptr);

    m_connOpts.set_clean_session(true);
    m_connOpts.set_keep_alive_interval(500);
    m_connOpts.set_max_inflight(m_max_inflight_number);
    // m_parse_info = new source_data_parse(ptr,m_responding_queue);
    return 0;
}

int MqttServer::connectinstance()
{
    m_token = m_client->connect(m_connOpts);
    m_token->wait();
    if(m_token->is_complete() && !m_token->get_reason_code())
    {
        std::cout<<"server connect success!"<<std::endl;
    }
    else
    {
        std::cout<<"connect failed!"<<std::endl;
    }
    m_client->set_callback(*this);
    subscribe();
    return 0;
}
int MqttServer::config_load()
{
    std::ifstream ifs("../config_folder/server_config.json");
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

    m_qos = config["qos_grade"];
    if(m_qos < 0)
    {
        std::cout<<"qos_grade is empty!"<<std::endl;
        return -1;
    }

    m_retained = config["retain_grade"];
    if(m_retained <0)
    {
        std::cout<<"retain_grade is empty!"<<std::endl;
        return -1;
    }

    m_max_inflight_number = config["max_inflight_number"];
    if(m_max_inflight_number < 0)
    {
        std::cout<<"max_inflight_number is empty!"<<std::endl;
        return -1;
    }

    m_broker= broker_address;
    m_client_id=  client_id;
    m_port= port;
    m_topicName= topicName;
    m_persistence = persistence_type;
    return 0;
}

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

void MqttServer::delivery_complete(mqtt::delivery_token_ptr mqtt_token)
{
    m_buffer_ptr->m_buffer_size.fetch_add(-1,std::memory_order_release);
    m_sender_condition.notify_one();
    std::cout<<"delivery complete , there is many inflight_message : "<<m_client->get_pending_delivery_tokens().size()<<std::endl;
}

void MqttServer::message_arrived(mqtt::const_message_ptr mqtt_msg) 
{
    if(mqtt_msg->get_topic() != "file/respond" && mqtt_msg->get_topic().find("ack")== std::string::npos)
    {
        std::lock_guard<std::mutex> lock(m_message_qeueu_mutex);
        m_message_queue.push(mqtt_msg);
    }

    // {
    //     m_process_string_condition.notify_one();
    // }
    {
        m_sender_condition.notify_one();
    }
} 

//  处理字符串任务
int MqttServer::process_string_to_task()
{
    if(m_message_queue.empty())
    {
        return 1;
    }
    std::string msg = m_message_queue.front()->to_string();
    m_message_queue.pop();

    request_message request;
    request.ParseFromString(msg);

    std::string input_file_path = request.input_file_path();
    std::string missing_index_vector = request.missing_slices_index_json();
    std::vector<int> missing_slices_index_vector;
    std::vector<uint8_t> file_data_vector;
    file_data_vector = get_file_data_vector(input_file_path);   

    nlohmann::json missing_slices_index_json = nlohmann::json::parse(missing_index_vector);
    if(missing_slices_index_json.is_array())
    {
        missing_slices_index_vector = missing_slices_index_json.get<std::vector<int>>();
    }
    else{
        std::cerr<<"missing_slices_index_json is not an array!";
        return -1;
    }

    // m_parse_info->parse_source_data_from_request(msg,file_data_vector,missing_slices_index_vector,m_sender_condition);

    #if DEBUG_TEST == true
        std::cout<<"process_string_to_task success!"<<std::endl;
    #endif

    return 1;
}

void MqttServer::start_send_reponse_to_client()
{
    std::thread([this](){
        while(true)
        {
            {
                std::unique_lock<std::mutex> lock(m_sender_mutex);
                m_sender_condition.wait(lock,[this]{
                    std::cout<<"thread ready send_reponse to client , and message_queue is empty ?  "<<m_message_queue.empty()<<" , inflight_size is exceed limitation ? "
                    <<(m_buffer_ptr->m_inflight_size.load(std::memory_order_acquire)>=m_max_inflight_number/2)<<std::endl;

                    // {
                    //     return  !m_is_active.load() || !m_responding_queue.empty()&&(m_buffer_ptr->m_inflight_size.load()<=m_max_inflight_number/2);
                    // }

                    // 这句话不放在这里，会导致线程卡死（比如放在send_reponse_to_client）
                    //  换句话说，决定线程是否继续运行，应该放在这里更新
                    m_buffer_ptr->m_inflight_size.store(m_client->get_pending_delivery_tokens().size(),std::memory_order_release);
                    {
                        return  !m_is_active.load() || !m_message_queue.empty()&&(m_buffer_ptr->m_inflight_size.load(std::memory_order_acquire)<=m_max_inflight_number/2);
                    }
                });
                if(!m_is_active.load())
                {
                    return;
                }
            }
            send_reponse_to_client();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }).detach();
}

//  loop the task queue
void MqttServer::start_process_string_to_task()
{
    std::thread([this](){
        while(true)
        {
            {
                std::unique_lock<std::mutex> lock(m_process_string_mutex);
                m_process_string_condition.wait(lock,[this]{
                    return  !m_is_active.load() || !m_message_queue.empty();
                });

                if(!m_is_active.load())
                {
                    return;
                }
            }
            process_string_to_task();
        }
    }).detach();
}

void MqttServer::send_reponse_to_client()
{
    {
        auto message_ptr = m_message_queue.front();
        m_message_queue.pop();
        
        mqtt::message_ptr r_msg = mqtt::make_message(respond_topic, 
            message_ptr.get()->get_payload().data(),
            message_ptr->get_payload().size(),
            m_qos, 
            m_retained);

        // std::string proto_msg = this->m_parse_info->return_task_from_queue();

        // mqtt::message_ptr r_msg = mqtt::make_message(respond_topic, 
        //     proto_msg.data(),
        //     proto_msg.size(),
        //     m_qos, 
        //     m_retained);
        
        {
            std::lock_guard<std::mutex> lock(m_token_mutex);
            auto token = m_client->publish(r_msg);
            std::cout<<"in waiting time, there is "<<m_client->get_pending_delivery_tokens().size()<<std::endl;
        }
        
        m_buffer_ptr->m_buffer_size.fetch_add(1,std::memory_order_release);
        std::cout<<"server publish message to client"<<std::endl;
    }
}

std::vector<uint8_t> MqttServer::get_file_data_vector(const std::string& db_file_path)
{
    //open data file
    std::ifstream file(db_file_path,std::ios::binary);
    if (!file)
    {
        std::cerr<<"get_file_data : open file failed! and the error infromation :"<<
        strerror(errno)<<std::endl;
        return {};
    }

    return{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };
    /*
    1. 完全安全 - 所有资源生命周期正确管理

    2. 更高效 - 最大化利用编译器的返回值优化

    3. 更简洁 - 减少不必要的中间变量和操作

    4. 符合现代 C++ 最佳实践 - 核心准则是"不要为编译器能自动优化的东西写额外代码"

    当返回函数内构造的对象时，优先使用直接返回临时对象的写法，让编译器选择最高效的实现方式。
    */
}

void MqttServer::subscribe()
{ 
    m_client->subscribe(m_topicName, m_qos);
}