#include "Sqlite_DB/Sqlite_DB.h"

std::unique_ptr<Sqlite_DB> Sqlite_DB::db_instance = nullptr;

Sqlite_DB::~Sqlite_DB()
{
}

bool Sqlite_DB::CloseDB()
{
    return true; 
}

std::unique_ptr<Sqlite_DB> Sqlite_DB_Create::create()
{
    return std::unique_ptr<Sqlite_DB>(new Sqlite_DB());
}

void Sqlite_DB_process_transmission_and_write_to_file::ready_for_transmission_data(
    std::string& msg,
    std::vector<uint8_t> &file_data_vector, 
    std::vector<int> &slice_index_vector,
    std::condition_variable& responding_condition_var
    )
{
    request_message tmp_msg;
    tmp_msg.ParseFromString(msg);
    std::array<uint8_t,16> file_id;
    std::copy(tmp_msg.file_id().begin(),tmp_msg.file_id().end(),file_id.begin());
    for(auto slice_index = slice_index_vector.begin(); slice_index != slice_index_vector.end()&& 1; ++slice_index)
    {
        // payload_protocolheader_ciphertext
        size_t sliceStart = *slice_index *  SLICE_SIZE;
        size_t file_offset =std::min( static_cast<size_t>((*slice_index + 1) * SLICE_SIZE ) , file_data_vector.size() );
    
        std::vector<uint8_t> slice(file_data_vector.begin() + sliceStart , file_data_vector.begin() + file_offset ) ;

        auto encrypt_task = m_thread_pool_ptr->enqueue(
            [
             this,
             &responding_condition_var,
             file_id = file_id,
             slice = std::move(slice),
             slice_index = (*slice_index)]() mutable
            {
                // construct ProtocolHeader
                ProtocolHeader header;

                header.magic = (MAGIC);
                header.slice_index = (slice_index);

                std::copy(file_id.begin(),file_id.end(),header.file_id.begin());
                // Generate AES_KEY randomly
                int rc = RAND_bytes(header.AES_KEY.data(), sizeof(header.AES_KEY));
                if (rc != 1)
                {
                    std::cerr<<"Error: Failed to generate AES_KEY\n";
                    return -1;
                }
                
                // Encrypt slice by AES , need record AES , Ciphertext
                std::vector<uint8_t> CipherText;
                if (!encryptData(slice, CipherText, header.iv.data(), header))
                {
                    std::cerr << "Encrypt Failed" << std::endl;
                }

                TestMsg msg ; 
                ProtocolHeader_To_Proto(header, CipherText, msg);
                std::string msg_string;
                msg.SerializeToString(&msg_string);

                {
                    #if DEBUG_TEST == true
                        std::cout<<"[DEBUG]  async task push success! "<<std::endl;   
                    #endif
                    std::lock_guard<std::mutex> lock(m_responding_mutex);                    
                    m_responding_queue.push(msg_string);
                    responding_condition_var.notify_one();
                }
                return 0;
            });
    }
}

Sqlite_DB_process_transmission_and_write_to_file::Sqlite_DB_process_transmission_and_write_to_file
(std::shared_ptr<spdlog::logger> logger, std::queue<std::string>& m_responding_queue):
    m_thread_pool_ptr(new Thread_pool()),
    m_spdlogger(logger),
    m_responding_queue(m_responding_queue)
    {
        m_thread_pool_ptr->start();
    }

Sqlite_DB_process_transmission_and_write_to_file::~Sqlite_DB_process_transmission_and_write_to_file()
{
    delete m_thread_pool_ptr;
    std::cout<<"Sqlite_DB_process_transmission_and_write_to_file::~Sqlite_DB_process_transmission_and_write_to_file()"<<std::endl;
}

// int Sqlite_DB_process_transmission_and_write_to_file::write_message_to_lockFreeQueue(const std::string &proto_msg,std::shared_ptr<DB_Info> file_path_info)
// {
//     DB_Info_raw_ptr db_info_ptr ;
//     db_info_ptr.header_info = new ProtocolHeader ((header));
//     db_info_ptr.slice_data_info = new std::vector<uint8_t> ((plaintext_data));
//     db_info_ptr.db_file_path = new std::string (file_path_info->db_file_path);
//     db_info_ptr.input_file_path = new std::string (file_path_info->input_file_path);   
//     db_info_ptr.output_file_path = new std::string (file_path_info->output_file_name); 
// }

bool Sqlite_DB_process_transmission_and_write_to_file::is_control(uint8_t c)
{
    return c == '\r' || c == '\n';
}


void Sqlite_DB_process_transmission_and_write_to_file::delete_done_tasks_in_process_map()
{
    DB_Info_raw_ptr db_info;
    //clear lockfree_queue , m_DB_Info_vector , m_swap_DB_Info_vector
    while(m_lockfree_queue.try_dequeue(db_info))
    {
        db_info.clear();
    }

    //clear thread_pool corresponding parameters
    this->m_thread_pool_ptr->clear_scheduled_tasks();
}

int Sqlite_DB_process_transmission_and_write_to_file::DecryptSharedData(const std::vector<uint8_t> &ciphertext, std::vector<uint8_t> &plaintext, ProtocolHeader &header)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if(!ctx)
    {
        return false;
    }

    const EVP_CIPHER* cipherType = EVP_aes_256_cbc();
    if(EVP_DecryptInit_ex(ctx,cipherType,nullptr,header.AES_KEY.data(),header.iv.data()) != 1)
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

int Sqlite_DB_Manager::send_request_to_server(std::string& request)
{ 
    m_mqtt_client_ref->SendSliceData(request);
    return true;
}

Sqlite_DB_Manager::~Sqlite_DB_Manager()
{
    // delete m_store;
    delete m_transmission_info;
    delete m_thread_pool_ptr;
    delete m_connection_pool_ptr;
}

int Sqlite_DB_Manager::return_scheduled_tasks()
{
    return this->m_thread_pool_ptr->return_scheduled_tasks();
}

void Sqlite_DB_Manager::thread_pool_start()
{
    this->m_thread_pool_ptr->start();
}

void Sqlite_DB_Manager::set_scheduled_tasks_to_ThreadPOol(int num)
{
    this->m_thread_pool_ptr->set_scheduled_tasks(num);
}

bool Sqlite_DB_Manager::return_is_threadPool_active()
{
    return this->m_thread_pool_ptr->is_active();
}

void Sqlite_DB_Manager::delete_done_tasks_in_process_map()
{
    //clear cooresponding project
    this->m_transmission_info->delete_done_tasks_in_process_map();
}

void Sqlite_DB_Manager::return_vector_file_path_merge(const std::string &folder_path,std::vector<std::string>& tem_vector)
{
    if(!fs::exists(folder_path)||!fs::is_directory(folder_path))
    {
        std::cerr<<"return_vector_file_path_merge: "<<folder_path<<" does not exist\n";
        return;
    }

    std::regex pattern("tmp_\\d+");
    for(const auto& entry : fs::directory_iterator(folder_path))
    {
        if(fs::is_regular_file(entry.path()))
        {
            std::string file_name = entry.path().filename().string();
            if(std::regex_search(file_name,pattern))
            {
                tem_vector.push_back(entry.path().string());
            }
        }
    }

    std::sort(
        tem_vector.begin(),tem_vector.end(),
    []
    (
        const std::string& a,
        const std::string& b
    )
        {
            auto extraceNum = [](const std::string& str)->int
            {
                size_t pos = str.rfind("tmp_");
                if (pos == std::string::npos)
                {
                    std::cerr<<"extraceNum have pos == std::string::npos error ! "<<std::endl;
                    return -1;
                }

                size_t num_start = pos + 4;
                size_t num_end = str.find(".", num_start);
                if(num_end == std::string::npos)
                {
                    std::cerr<<"extraceNum have num_end == std::string::npos error ! "<<std::endl;
                    return -1;
                }

                std::string num_str = str.substr(num_start,num_end-num_start);
                return std::stoi(num_str);
            };

            int num_a = extraceNum(a);
            int num_b = extraceNum(b);
           
            return num_a < num_b;   
        }
    );
}

void Sqlite_DB_Manager::concatenate_file(std::vector<std::string> &file_path_vector,const MainWindows_Intermediate_Struct& file_info)    
{
    std::ofstream merge_file(file_info.output_file_path,std::ios::binary);
    int index = 0;
    for(auto& it:file_path_vector)
    {
        std::ifstream source_file(it,std::ios::binary);
        merge_file << source_file.rdbuf();
        if ( index == file_info.terminate_symbol_to_file_map.at(file_info.output_file_path).first && 
            file_info.terminate_symbol_to_file_map.at(file_info.output_file_path).second == true )
        {
            merge_file << std::endl;
        } 
        index ++;
    }
    return;
}

Sqlite_DB_function::~Sqlite_DB_function(){
    sqlite3_finalize(read_sourcefile_from_db_information_file_stmt_ptr);
    sqlite3_finalize(check_table_exist_stmt_ptr);
    sqlite3_finalize(create_new_db_information_record_stmt_ptr);
    sqlite3_finalize(create_new_subordinate_db_record_stmt_ptr);
    sqlite3_finalize(update_db_information_stmt_ptr);

    sqlite3_close(db_information_ptr);
    sqlite3_close(db_subordinate_ptr);

    delete m_connection_pool_ptr;
    std::cout<<"Sqlite_DB_function::~Sqlite_DB_function() called!\n";
}

void Sqlite_DB_function::presetting()
{
    //  open db_information_file
    if(sqlite3_open_v2(db_file_name.c_str(),&db_information_ptr,SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX,nullptr) != SQLITE_OK)
    {
        std::cerr<<"Sqlite_information::presetting: "<<db_file_name<<" open failed\n";
        return;
    }

    // if table not exist, create table in db_information_file 
    if(sqlite3_prepare_v2(db_information_ptr,check_table_exist_sql,strlen(check_table_exist_sql),&check_table_exist_stmt_ptr,nullptr) != SQLITE_OK)
    {   
        std::cerr<<"Sqlite_information::presetting: "<<db_file_name<<" prepare check_sql failed\n";
        return;
    }
    else{
        bool table_exist = (sqlite3_step(check_table_exist_stmt_ptr) == SQLITE_ROW);
        if(!table_exist)
        {
            for(auto& sql:Create_Table)
            {
                if(sqlite3_exec(db_information_ptr,sql,nullptr,nullptr,nullptr) != SQLITE_OK)
                {
                    std::cerr<<"Sqlite_information::presetting: "<<db_file_name<<" create table failed\n";
                    return;
                }
            }
        }
    }    

    //  read sourcefile information in db_information 
    if(sqlite3_prepare_v2(db_information_ptr,read_sourcefile_from_db_information_sql,strlen(read_sourcefile_from_db_information_sql),&read_sourcefile_from_db_information_file_stmt_ptr,nullptr) != SQLITE_OK)
    {
        std::cerr<<"Sqlite_information::presetting: "<<db_file_name<<" prepare read_sql failed\n";
        return;
    }

    //  create new record in db_information
    if (sqlite3_prepare_v2(db_information_ptr,create_new_db_information_record_sql,strlen(create_new_db_information_record_sql),&create_new_db_information_record_stmt_ptr,nullptr) != SQLITE_OK) {
        std::cerr<<"Sqlite_information::presetting: "<<db_file_name<<" prepare update_db_information_stmt_ptr failed\n";
        return;
    }

    //  update db_information file
    if(sqlite3_prepare_v2(db_information_ptr,update_db_information_sql,strlen(update_db_information_sql),&update_db_information_stmt_ptr,nullptr) != SQLITE_OK)
    {
        std::cerr<<"Sqlite_information::presetting: "<<db_file_name<<" prepare update_db_information_sql failed\n";
        return;
    }
}

bool Sqlite_DB_function::read_missing_slices_from_db_information_file(const std::string& file_path)
{
    // judge whether file_path is empty
    bool find_record_in_db_information_file_result = false;
    if (file_path.empty())
    {
        return find_record_in_db_information_file_result;
    }

    // bind file_path to sql,try read corresponding file information in db_information file
    int rc =sqlite3_bind_text(read_sourcefile_from_db_information_file_stmt_ptr, 1 , file_path.data(), file_path.size(),SQLITE_STATIC);
    if (rc != SQLITE_OK)
    {
        std::cerr<<"Sqlite_information::read_missing_slices_from_db_information_file: "<<" prepare read failed\n";
        return find_record_in_db_information_file_result;
    }
    // read corresponding file whether exist in db_information file
    while( (rc = sqlite3_step(read_sourcefile_from_db_information_file_stmt_ptr)) == SQLITE_ROW)
    {
        int missing_slices_index_length = sqlite3_column_bytes(read_sourcefile_from_db_information_file_stmt_ptr,3);
        const std::string& missing_slices_index_string = std::string(reinterpret_cast<const char*>(sqlite3_column_blob(read_sourcefile_from_db_information_file_stmt_ptr,3)),missing_slices_index_length);
        if(missing_slices_index_string == "")
        {
            continue;
        }

        int file_id_length = sqlite3_column_bytes(read_sourcefile_from_db_information_file_stmt_ptr,1);
        int input_file_path_length = sqlite3_column_bytes(read_sourcefile_from_db_information_file_stmt_ptr,2);
        std::string file_id  (reinterpret_cast<const char*>(sqlite3_column_blob(read_sourcefile_from_db_information_file_stmt_ptr,1)),file_id_length);
        std::string input_file_path  (reinterpret_cast<const char*>(sqlite3_column_text(read_sourcefile_from_db_information_file_stmt_ptr,2)));      

        {
            auto request_message_ptr = m_memory_pool_ref.return_ptr();
            request_message_ptr->set_is_download(false);
            request_message_ptr->set_file_id(file_id); 
            request_message_ptr->set_input_file_path(input_file_path); 
            std::string json_text (missing_slices_index_string) ;
            request_message_ptr->set_missing_slices_index_json( json_text);

            m_memory_pool_ref.push(std::move(request_message_ptr));
            find_record_in_db_information_file_result = true;
        }
    }
    sqlite3_reset(read_sourcefile_from_db_information_file_stmt_ptr);
    return find_record_in_db_information_file_result;
}

std::string Sqlite_DB_function::return_current_date_string()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d"); // 格式化为 "20240520"
    return oss.str();
}

std::vector<int> Sqlite_DB_function::return_continous_sequence(int file_size){
    std::vector<int> missing_index_vector;

    missing_index_vector.reserve(file_size );

    for(int i = 0; i < file_size; i++)
    {
        missing_index_vector.push_back(i);
    }

    return missing_index_vector;
}

int Sqlite_DB_function::create_new_file_on_db_information(const std::string& sourcefile_path)
{
    int file_size = fs::file_size(sourcefile_path);
    file_size = (file_size + SLICE_SIZE - 1)/SLICE_SIZE;
    if(file_size <= 0)
    {
        std::cerr<<"Sqlite_information::return_continous_sequence: "<<" file_size <= 0\n";
        return true;
    }

    std::array<uint8_t,16> file_id;
    {
        random_generator Generated_uuid;
        uuid uuid_ptr= Generated_uuid();
        std::copy(uuid_ptr.begin(), uuid_ptr.end(), file_id.begin());
    }
    std::vector<int> missing_index_vector = return_continous_sequence(file_size);
    std::string json_string = "";
    if(!missing_index_vector.empty())
    {
        json_string = nlohmann::json(missing_index_vector).dump();
    }

    int rc = sqlite3_bind_blob(create_new_db_information_record_stmt_ptr,1,file_id.data(),file_id.size(),SQLITE_STATIC);
    rc = sqlite3_bind_text(create_new_db_information_record_stmt_ptr,2,sourcefile_path.c_str(),sourcefile_path.size(),SQLITE_STATIC);
    rc = sqlite3_bind_text(create_new_db_information_record_stmt_ptr,3,json_string.c_str(),json_string.size(),SQLITE_STATIC);
    if(rc != SQLITE_OK)
    {
        std::cerr<<"Sqlite_information::update_new_file_on_db: "<<" prepare create_new_db_information_record_stmt_ptr failed\n";
        return -1;
    }

    rc = sqlite3_step(create_new_db_information_record_stmt_ptr);
    if(rc != SQLITE_DONE)
    {
        std::cerr<<"Sqlite_information::update_new_file_on_db: "<<" create_new_db_information_record_stmt_ptr failed\n";
        return -1;
    }
    sqlite3_reset(create_new_db_information_record_stmt_ptr);

    return true;
}


int taskExecution::send_task()
{ 
    // construct ProtocolHeader
    for(size_t i = 0; i < m_memory_pool_ref.size(); i++)
    {
        auto msg_ptr = m_memory_pool_ref.return_pre_ptr();
        if (msg_ptr->is_download())
        {
            m_memory_pool_ref.push(std::move(msg_ptr));
            continue;
        }

        msg_ptr->set_is_download(true);
        std::string msg_string;
        msg_ptr->SerializeToString(&msg_string);
        m_memory_pool_ref.push(std::move(msg_ptr));
    
        m_mqtt_client_ref.SendSliceData(msg_string);
    }
    
    #if DEBUG_TEST == true
        std::cout << "send_task is success" << std::endl;
    #endif

    return 1;
}

std::string Sqlite_DB_process_transmission_and_write_to_file::return_task_from_queue()
{
    std::string msg = m_responding_queue.front();
    m_responding_queue.pop();
    return msg;
}

int Sqlite_DB_write_file::write_message_in_db_subordinate_file_in_batch(std::queue<mqtt::const_message_ptr>& m_tmp_received_messages_queue)
{ 
    auto connection_wrapped_ptr = m_sqlite_db_function_ref.m_connection_pool_ptr->return_connectionWrapper_ptr();
    sqlite3_exec(connection_wrapped_ptr->db_ptr,"BEGIN TRANSACTION",nullptr,nullptr,nullptr);
    if (connection_wrapped_ptr == nullptr || connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT)== nullptr)
    {
        std::cerr << "get_DBfile_parameters : return_connectionWrapper_ptr is nullptr ! and connectino_wrapped_ptr.get() is "
        <<connection_wrapped_ptr.get() << std::endl;
        return false;
    }

    for (size_t i=0; i< m_tmp_received_messages_queue.size(); i++)
    {
        TestMsg data_information;
        auto msg = m_tmp_received_messages_queue.front()->to_string();
        data_information.ParseFromString(msg);
        m_tmp_received_messages_queue.pop();

        int rc = blob_parameter_insert(connection_wrapped_ptr.get(),data_information);
        if(rc != SQLITE_OK)
        {
            std::cerr<< "Insert slice_contents failed and error :"<<sqlite3_errmsg(connection_wrapped_ptr->db_ptr)<<std::endl;
            sqlite3_exec(connection_wrapped_ptr->db_ptr,"ROLLBACK",nullptr,nullptr,nullptr);
            m_sqlite_db_function_ref.m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
            return false;
        }
    
        //  insert slice_contents 
        rc = sqlite3_step(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT));
        if (rc != SQLITE_DONE || connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT) == nullptr)
        {
            std::cerr<< "Insert slice_contents failed and error :"<<sqlite3_errmsg(connection_wrapped_ptr->db_ptr)<<std::endl;
            sqlite3_exec(connection_wrapped_ptr->db_ptr,"ROLLBACK",nullptr,nullptr,nullptr);
            m_sqlite_db_function_ref.m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
            return false;
        }
        
        sqlite3_reset(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT));
    }
    sqlite3_exec(connection_wrapped_ptr->db_ptr,"COMMIT",nullptr,nullptr,nullptr);

    //  update memory pool
    m_sqlite_db_function_ref.update_memory_pool(connection_wrapped_ptr->return_db_file_path());

    //  release connection   
    m_sqlite_db_function_ref.m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));

    #if DEBUG_TEST == true
        std::cout << "write_message_in_db_subordinate_file_in_batch is success" << std::endl;
    #endif

    return SQLITE_DONE;
}

void Sqlite_DB_function::update_memory_pool(const std::string& file_path)
{
    for(size_t i = 0; i < m_memory_pool_ref.size(); i++)
    {
        auto request_message_ptr = m_memory_pool_ref.return_pre_ptr();
        std::vector<int> tmp_vector = read_missing_slices_from_db_subordinate_file(*request_message_ptr,file_path);
        if(tmp_vector.size()!=0)
        {
            request_message_ptr->set_missing_slices_index_json(nlohmann::json(tmp_vector).dump());
        }
        else
        {
            request_message_ptr->set_missing_slices_index_json("");
        }
        int rc = update_db_information(request_message_ptr->file_id(),file_path,request_message_ptr->missing_slices_index_json());
        if (rc != SQLITE_DONE)
        {
            std::cerr <<"the file: "<<request_message_ptr->input_file_path()<< " update_db_information failed and error :"<<sqlite3_errmsg(db_information_ptr)<<std::endl;
        }


        m_memory_pool_ref.push(std::move(request_message_ptr));
    }
}

int Sqlite_DB_function::update_db_information(const std::string& file_id,const std::string& file_path,const std::string& missing_slices_index_json)
{
    int rc = sqlite3_bind_blob(update_db_information_stmt_ptr,3,file_id.data(),file_id.size(),SQLITE_STATIC);
    rc = sqlite3_bind_text(update_db_information_stmt_ptr,2,file_path.c_str(),file_path.size(),SQLITE_TRANSIENT);
    rc = sqlite3_bind_text(update_db_information_stmt_ptr,1,missing_slices_index_json.data(),missing_slices_index_json.size(),SQLITE_TRANSIENT);  
    
    if(rc != SQLITE_OK)
    {
        std::cerr<< "update_db_information failed and error :"<<sqlite3_errmsg(db_information_ptr)<<std::endl;
        return -1;
    }

    rc = sqlite3_step(update_db_information_stmt_ptr);
    if(rc != SQLITE_DONE)
    {
        std::cerr<< "update_db_information failed and error :"<<sqlite3_errmsg(db_information_ptr)<<std::endl;
        return -1;
    }
    sqlite3_reset(update_db_information_stmt_ptr);
    return rc;
}

int Sqlite_DB_write_file::blob_parameter_insert(ConnectionWrapper* connection_wrapped_ptr,TestMsg& data_information)
{
    // blind parameters with slice_contents
    int rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 1,
                                data_information.file_id().data(), 
                                data_information.file_id().size(), 
                                SQLITE_STATIC);
    rc = sqlite3_bind_int(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 2,
                            data_information.slice_index());
    rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 3,
                            data_information.aes_key().data(),
                            data_information.aes_key().size(),
                            SQLITE_TRANSIENT);
    rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 4,
                            data_information.iv().data(),
                            data_information.iv().size(),
                            SQLITE_STATIC);
    rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 5,
                            data_information.ciphertext().data(),
                            data_information.ciphertext().size(),
                            SQLITE_STATIC);
    return rc;
}

Sqlite_DB_function::Sqlite_DB_function(memory_pool& memory_pool_ref)
: m_memory_pool_ref(memory_pool_ref)
,m_connection_pool_ptr(new ConnectionPool())
{

}

std::vector<int> Sqlite_DB_function::read_missing_slices_from_db_subordinate_file(request_message& request_message_ptr,const std::string& last_update_db_subordinate_file_path)
{
    std::vector<int> missing_slices_index_vector;
    auto connection_wrapped_ptr = m_connection_pool_ptr->check_use_ptr_in_connection_pool(last_update_db_subordinate_file_path);
    
    int rc =sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr
        (DB_Type::GET_FAIL_INDEX),1,request_message_ptr.missing_slices_index_json().data(),request_message_ptr.missing_slices_index_json().size(),SQLITE_STATIC);
    if(rc != SQLITE_OK)
    {
        std::cerr<<"Error binding parameter: "<<sqlite3_errmsg(db_subordinate_ptr)<<std::endl;
        return std::vector<int>{};
    }
    rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::GET_FAIL_INDEX),2,request_message_ptr.file_id().data(),request_message_ptr.file_id().size(),SQLITE_STATIC);
    if(rc != SQLITE_OK)
    {
        std::cerr<<"Error binding parameter: "<<sqlite3_errmsg(db_subordinate_ptr)<<std::endl;
        return std::vector<int>{};
    }

    while((rc = sqlite3_step(connection_wrapped_ptr->return_stmt_ptr(DB_Type::GET_FAIL_INDEX))) == SQLITE_ROW )
    {
        missing_slices_index_vector.push_back(sqlite3_column_int(connection_wrapped_ptr->return_stmt_ptr(DB_Type::GET_FAIL_INDEX),0));
    }

    m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
    return missing_slices_index_vector;
}

void Sqlite_information::presetting_db_information()
{
    m_sqlite_db_function_ref.presetting();
}


void taskExecution::write_task_to_db_suborinate_file()
{
    this->m_mqtt_client_ref.transmit_message_to_sqlite();
}

void taskExecution::delete_task_from_memory_pool(const std::string& input_file_path)
{
    for(size_t i = 0; i < m_memory_pool_ref.size(); i++)
    {
        auto tmp_object = m_memory_pool_ref.return_pre_ptr();
        if(tmp_object->input_file_path()==input_file_path)
        {
            std::cout<<"task:"<<input_file_path<<" is deleted"<<std::endl;
            return;
        }
        m_memory_pool_ref.push(std::move(tmp_object));
    }
}

void Sqlite_DB_function::merge_select_file(request_message& request_message_ref)
{ 
    int rc ;
    auto connection_wrapped_ptr = m_connection_pool_ptr->return_connectionWrapper_ptr();
    std::string file_name_string = fs::path(request_message_ref.input_file_path()).filename().string();

    // select file_id , input_file_path from record
    // in loop , should not reset the condition stmt , because the loop based on stmt to get next row 
    rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::MERGE_SELECT_FILE),1,request_message_ref.file_id().data(),request_message_ref.file_id().size(),SQLITE_STATIC);
    if (rc != SQLITE_OK)
    {
        std::cerr<<"merge_select_file:sqlite3_bind_blob error : "<<sqlite3_errmsg(db_subordinate_ptr)<<std::endl;
        return;
    }

    while ( (rc = (sqlite3_step(connection_wrapped_ptr->return_stmt_ptr(DB_Type::MERGE_SELECT_FILE)) == SQLITE_ROW)) ) {
        // select slice_index , slice_content from slice_content where file_id = ?
        //  select from 0~N , bind from 1~N
        {
            int slice_index = sqlite3_column_int(connection_wrapped_ptr->return_stmt_ptr(DB_Type::MERGE_SELECT_FILE),1);
            const char* slice_content = reinterpret_cast<const char*>(sqlite3_column_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::MERGE_SELECT_FILE),2));
            int slice_content_size = sqlite3_column_bytes(connection_wrapped_ptr->return_stmt_ptr(DB_Type::MERGE_SELECT_FILE),2);

           //write message to file 
           std::ofstream file_out("./"+file_name_string,std::ios::app | std::ios::binary);
           if(!file_out)
           {
             std::cerr<<"merge_select_file:mergerSQLData : open file error : "<<file_name_string<<std::endl;
             return;
           }
            
           file_out.write((slice_content),slice_content_size);
        }
    }
    m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
}