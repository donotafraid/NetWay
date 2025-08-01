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


int Sqlite_DB_store::get_DBfile_parameters(std::shared_ptr<DB_Info> db_info)
{
    const char *resumable_sql = R"(
        select 
            sr.file_id,
            sr.input_file_path,
            sr.output_folder_path,
            COALESCE(MAX(sc.slice_index),0) AS last_slice_index
        FROM slice_records sr
        LEFT JOIN slice_contents sc ON sr.file_id = sc.file_id
        GROUP BY sr.file_id,sr.input_file_path
    )";

    auto m_connection_wrapped = this->m_connection_pool_ptr->return_connectionWrapper_ptr(db_info->db_file_path);
    if(m_connection_wrapped== nullptr)
    {
        std::cerr<<"get_DBfile_parameters : return_connectionWrapper_ptr is nullptr !"<<std::endl;
        return false;
    }

    sqlite3_stmt *stmt = nullptr;

    int rc = sqlite3_prepare_v2( m_connection_wrapped->db_ptr, resumable_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr<<"get_DBfile_parameters : prepare resumable_sql failed and error is:"<<
        sqlite3_errmsg(m_connection_wrapped->db_ptr)<<std::endl;
        return rc;
    }

    while(( rc = sqlite3_step(stmt) )== SQLITE_ROW)
    {
        // get file path
        const unsigned char* path_text = sqlite3_column_text(stmt,1);
        db_info->input_file_path = std::string((char*)path_text);

        // get output file path
        const unsigned char* path_text2 = sqlite3_column_text(stmt,2);
        db_info->output_folder_path = std::string((char*)path_text2);
    }

    sqlite3_finalize(stmt);
    this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(m_connection_wrapped));

    if( rc != SQLITE_DONE)
    {
        std::cerr<<"Query execution error :"
                 <<sqlite3_errmsg(m_connection_wrapped->db_ptr)<<std::endl;
    }
    return rc;
}

Sqlite_DB_store::Sqlite_DB_store(ConnectionPool* connection_pool_ptr,
    Sqlite_DB_process_slice_info* process_slice_ptr):
    m_connection_pool_ptr(connection_pool_ptr),
    m_slice_info(process_slice_ptr)
{
    
}

int Sqlite_DB_store::interface_DB_store(std::vector<MainWindows_Intermediate_Struct>& file_info_vector)
{
        for(auto& file_info : file_info_vector)
    {
        auto db_info = std::make_shared<DB_Info>();
        db_info->input_file_path = file_info.source_file_path;
        db_info->output_folder_path = file_info.output_folder_path;
        db_info->db_file_path = file_info.db_file_path;
        db_info->header_info.total_slices = (file_info.file_size + SLICE_SIZE - 1) / SLICE_SIZE ;
        db_info->file_size = file_info.file_size;
        db_info->header_info.magic = magic;
        if(db_info->header_info.file_id == std::array<uint8_t,16>{})
        {
            random_generator Generated_uuid;
            uuid uuid_ptr= Generated_uuid();
            std::copy(uuid_ptr.begin(), uuid_ptr.end(), db_info->header_info.file_id.begin());
        }

        int rc = this->check_exist_dbFile_record(db_info);
        if(rc != SQLITE_DONE)
        {
            return rc;
        }

        auto fileData = this->get_file_data_vector(db_info);
        auto failIndex = this->get_failIndex_vector(db_info);
        rc =this->m_slice_info->interface_process_slice_info(
            db_info,
            std::move(fileData),
            std::move(failIndex),
            file_info
        ); 
    }
    return 0;
}

Sqlite_DB_process_slice_info::Sqlite_DB_process_slice_info(Thread_pool* thread_pool_ptr,
    Sqlite_DB_process_transmission_and_write_to_file* transmssion_info_ptr):
    m_thread_pool_ptr(thread_pool_ptr),
    m_transmission_ptr(transmssion_info_ptr)
    {

    }

int Sqlite_DB_process_slice_info::interface_process_slice_info(
    std::shared_ptr<DB_Info> db_info, 
    std::vector<uint8_t> &&file_data_vector,
    std::vector<int> &&fail_slice_index_vector,
    MainWindows_Intermediate_Struct& file_info )
{
    this->initialize_m_transmission_info(db_info);
    int rc = ready_for_transmission_data(db_info, std::move(file_data_vector), std::move(fail_slice_index_vector),file_info);
    return 0;
}

int Sqlite_DB_process_slice_info::ready_for_transmission_data(
    std::shared_ptr<DB_Info> db_info, 
    std::vector<uint8_t> &&file_data_vector, 
    std::vector<int> &&slice_index_vector,
    MainWindows_Intermediate_Struct& file_info)
{
    for(auto slice_index = slice_index_vector.begin(); slice_index != slice_index_vector.end(); ++slice_index)
    {
        // payload_protocolheader_ciphertext
        size_t sliceStart = *slice_index *  SLICE_SIZE;
        size_t file_offset =std::min( static_cast<size_t>((*slice_index + 1) * SLICE_SIZE ) , file_data_vector.size() );
    
        std::vector<uint8_t> slice(file_data_vector.begin() + sliceStart , file_data_vector.begin() + file_offset ) ;

        //judget if the slice is a terminate symbol
        if (is_control(slice.back()))
        {
            file_info.terminate_symbol_to_file_map.insert({file_info.output_file_path,{(*slice_index),true} });
        }
        else
        {
            file_info.terminate_symbol_to_file_map.insert({file_info.output_file_path,{(*slice_index),false} });
        }

        auto encrypt_task = m_thread_pool_ptr->enqueue(
            [m_transmission_ptr = &this->m_transmission_ptr,
             slice = std::move(slice),
             shared_db_ptr = db_info,
             slice_index = std::move(*slice_index)]() mutable
            {
                // construct ProtocolHeader
                ProtocolHeader header;

                // Generate AES_KEY randomly
                int rc = RAND_bytes(header.AES_KEY.data(), sizeof(header.AES_KEY));
                if (rc != 1)
                {
                    return -1;
                }

                header.magic = (MAGIC);
                header.slice_index = (slice_index);
                header.total_slices = (shared_db_ptr->header_info.total_slices);
                header.plaintext_size = (slice.size());
    
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
                                       // Multi-threading enabled
                (*m_transmission_ptr)->interface_process_transmission_and_write_to_file(
                    msg_string,
                    shared_db_ptr);
                return 1;
            });
    }
    return 0;
}

bool Sqlite_DB_process_slice_info::is_control(uint8_t c)
{
    return c == '\r' || c == '\n';
}

int Sqlite_DB_process_slice_info::initialize_m_transmission_info(std::shared_ptr<DB_Info> db_info)
{
    this->m_transmission_ptr->set_file_data_map(db_info->input_file_path);
    return 0;
}

std::vector<uint8_t> Sqlite_DB_store::get_file_data_vector( std::shared_ptr<DB_Info> db_info)
{
    //open data file
    std::ifstream file(db_info->input_file_path,std::ios::binary);
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

std::vector<int> Sqlite_DB_store::get_failIndex_vector(std::shared_ptr<DB_Info> db_info)
{
    auto connectionWrapped_ptr = this->m_connection_pool_ptr->return_connectionWrapper_ptr(db_info->db_file_path); 
    int rc = sqlite3_bind_int(
    connectionWrapped_ptr->return_stmt_ptr(DB_Type::GETINDEX),
    1,
    db_info->header_info.total_slices - 1);

    rc = sqlite3_bind_blob(
        connectionWrapped_ptr->return_stmt_ptr(DB_Type::GETINDEX),
        2,
        db_info->header_info.file_id.data(),
        db_info->header_info.file_id.size(),
        SQLITE_STATIC
    );

    if (rc != SQLITE_OK)
    {
        std::cerr<<"get_failIndex_vector : rc != SQLITE_OK and error infromation : "<<sqlite3_errmsg(connectionWrapped_ptr->db_ptr)<<std::endl;
        this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connectionWrapped_ptr));
        return {};
    }

    std::vector<int> missing_index_vector;
    while( (rc = sqlite3_step(connectionWrapped_ptr->return_stmt_ptr(DB_Type::GETINDEX)) ) == SQLITE_ROW)
    {
        int missing_index = sqlite3_column_int((connectionWrapped_ptr->return_stmt_ptr(DB_Type::GETINDEX)), 0);
        missing_index_vector.push_back(missing_index);
    }

    if ( rc != SQLITE_DONE)
    {
        std::cerr<<"get_failIndex_vector : rc != SQLITE_DONE and error infromation : "<<sqlite3_errmsg(connectionWrapped_ptr->db_ptr)<<std::endl;
        this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connectionWrapped_ptr));
        return {};
    }

    std::string vector_string;
    vector_string = nlohmann::json(missing_index_vector).dump();

    rc = sqlite3_bind_text(
    connectionWrapped_ptr->return_stmt_ptr(DB_Type::GETFAILINDEX),
    1,
    vector_string.c_str(),
    vector_string.size(),
    SQLITE_STATIC
    );

    rc = sqlite3_bind_blob(
        connectionWrapped_ptr->return_stmt_ptr(DB_Type::GETFAILINDEX),
        2,
        db_info->header_info.file_id.data(),
        db_info->header_info.file_id.size(),
        SQLITE_STATIC
    );

    if (rc != SQLITE_OK)
    {
        std::cerr<<"get_failIndex_vector : rc != SQLITE_OK and error infromation : "<<sqlite3_errmsg(connectionWrapped_ptr->db_ptr)<<std::endl;
        this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connectionWrapped_ptr));
        return {};
    }

    rc = sqlite3_step(connectionWrapped_ptr->return_stmt_ptr(DB_Type::GETFAILINDEX));

    if ( rc != SQLITE_DONE)
    {
        std::cerr<<"get_failIndex_vector : rc != SQLITE_DONE and error infromation : "<<sqlite3_errmsg(connectionWrapped_ptr->db_ptr)<<std::endl;
        this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connectionWrapped_ptr));
        return {};
    }

    // sqlite3_reset(connectionWrapped_ptr->return_stmt_ptr(DB_Type::GETINDEX));
    this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connectionWrapped_ptr));
    //record missing index to header_info
    db_info->header_info.total_slices = missing_index_vector.size();
    return missing_index_vector;
}

int Sqlite_DB_store::check_exist_dbFile_record(std::shared_ptr<DB_Info> db_info)
{
    auto ConnectionWrapped_ptr = this->m_connection_pool_ptr->return_connectionWrapper_ptr(db_info->db_file_path); 
    sqlite3_exec(ConnectionWrapped_ptr->db_ptr,"BEGIN TRANSACTION",nullptr,nullptr,nullptr);
    if (ConnectionWrapped_ptr == nullptr || ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::CONTENT)== nullptr)
    {
        std::cerr << "get_DBfile_parameters : return_connectionWrapper_ptr is nullptr ! and connectino_wrapped_ptr.get() is "
        <<ConnectionWrapped_ptr.get() << std::endl;
        return false;
    }

    {
        // 如果 db_info.slice_data_info.file_id  在 slice_records 中不存在， 则插入一条记录
        int rc = sqlite3_bind_blob(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::INSPECT_ID), 1,
                                db_info->header_info.file_id.data(),
                                db_info->header_info.file_id.size(),
                                SQLITE_STATIC);
        bool record_exists = ( sqlite3_step(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::INSPECT_ID)) == SQLITE_ROW ) ;
        sqlite3_reset(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::INSPECT_ID));

        if (!record_exists)
        {
            rc = sqlite3_bind_blob(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 1,
                                db_info->header_info.file_id.data(),
                                db_info->header_info.file_id.size(),
                                SQLITE_STATIC);
            rc = sqlite3_bind_int(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 4,
                                db_info->header_info.total_slices);
            rc = sqlite3_bind_int64(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 3,
                                    static_cast<int64_t>(db_info->header_info.magic));
            rc = sqlite3_bind_text(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 2,
                                db_info->input_file_path.c_str(),
                                -1,
                                SQLITE_STATIC);
            rc = sqlite3_bind_text(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 5,
                                db_info->output_folder_path.c_str(),
                                -1,
                                SQLITE_STATIC);

            rc = sqlite3_step(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD));
            if(rc != SQLITE_DONE)
            {
                std::cerr<<"NewRecorconnection_wrapper_ptr->db_ptruild failed and error is : "<<sqlite3_errmsg(ConnectionWrapped_ptr->db_ptr)<<std::endl;
                sqlite3_exec(ConnectionWrapped_ptr->db_ptr,"ROLLBACK",nullptr,nullptr,nullptr);
                this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(ConnectionWrapped_ptr));
                return false;
            }
        }
    }
    sqlite3_reset((ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD)));
    sqlite3_exec(ConnectionWrapped_ptr->db_ptr,"COMMIT",nullptr,nullptr,nullptr);
    sqlite3_reset((ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD)));
    return SQLITE_DONE;
}

Sqlite_DB_process_transmission_and_write_to_file::Sqlite_DB_process_transmission_and_write_to_file
(Thread_pool* thread_pool_ptr,MqttClient* mqtt_client_ptr,ConnectionPool* connection_pool_ptr,std::shared_ptr<spdlog::logger> logger):
    m_thread_pool_ptr(thread_pool_ptr),
    m_mqtt_client_ptr(mqtt_client_ptr),
    m_connection_pool_ptr(connection_pool_ptr),
    m_spdlogger(logger)
    {

    }

int Sqlite_DB_process_transmission_and_write_to_file::interface_process_transmission_and_write_to_file(
    const std::string &payload,
    std::shared_ptr<DB_Info> db_info)
{
        {
            if(m_mqtt_client_ptr->SendSliceData(payload)==-1)
            {
                std::cerr << "Send Slice Data Failed" << std::endl;
                return static_cast<ssize_t>(-1);
            }
            else
            {
                int rc = write_message_to_file(payload,db_info);
                if (rc != SQLITE_DONE)
                {
                    std::cerr<<"operate_raw_DBfiles : write message to file failed!"<<std::endl;
                    return static_cast<ssize_t>(-1);
                }
                return static_cast<ssize_t>(1);
            }
        };
    return 0;
}

int Sqlite_DB_process_transmission_and_write_to_file::write_message_to_file(const std::string &proto_msg,std::shared_ptr<DB_Info> file_path_info)
{
    TestMsg msg;
    ProtocolHeader header;
    msg.ParseFromString(proto_msg);

    header.magic = (msg.magic());
    header.slice_index = (msg.slice_index());
    header.total_slices = (msg.total_slices());
    header.plaintext_size = (msg.plaintext_size());    

    std::copy(msg.file_id().begin(),msg.file_id().end(),header.file_id.begin());
    std::copy(msg.iv().begin(),msg.iv().end(),header.iv.begin());
    std::copy(msg.aes_key().begin(),msg.aes_key().end(),header.AES_KEY.begin());
    std::vector<uint8_t> ciphertext_data(msg.ciphertext().begin(),msg.ciphertext().end());
    
    std::vector<uint8_t> plaintext_data;
    int rc =DecryptSharedData(ciphertext_data,plaintext_data,header);  
    if (rc != true)
    {
        std::cerr<<"write_message_to_file : Decrypt Shared Data Failed" << std::endl;
        return false;
    }
    
    DB_Info_raw_ptr db_info_ptr ;
    db_info_ptr.header_info = new ProtocolHeader ((header));
    db_info_ptr.slice_data_info = new std::vector<uint8_t> ((plaintext_data));
    db_info_ptr.db_file_path = new std::string (file_path_info->db_file_path);
    db_info_ptr.input_file_path = new std::string (file_path_info->input_file_path);   
    db_info_ptr.output_folder_path = new std::string (file_path_info->output_folder_path); 
    db_info_ptr.file_size =(file_path_info->file_size);

    m_lockfree_queue.push(std::move(db_info_ptr));

    return SQLITE_DONE;

    // {
    //     rc = this->write_dbData_to_file(db_info);
    //     if (rc != SQLITE_DONE)
    //     {
    //         std::cout<<"insert data into DB file failed !"<<std::endl;
    //     }   
    
    //     {
    //         std::string output_file_path = db_info.output_folder_path + "/tmp_"+ std::to_string(db_info.header_info.slice_index) + ".txt";
    //         std::ofstream file(output_file_path,std::ios::binary | std::ios::app);
    //         if (!file.is_open())
    //         {
    //             std::cout<<"write_message_to_file : open file failed!"<<std::endl;
    //             return -1;
    //         }
    //         else
    //         {
    //             file.write((char*)db_info.slice_data_info.data(),db_info.slice_data_info.size());
    //             file.close();
              
    //         }
    //     }
    //     return rc;
    // }
}

// int Sqlite_DB_process_transmission_and_write_to_file::write_dbData_to_file(const DB_Info &db_info)
// {
//     auto connection_wrapped_ptr = this->m_connection_pool_ptr->return_connectionWrapper_ptr(db_info.db_file_path);
//     if (connection_wrapped_ptr == nullptr || connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT)== nullptr)
//     {
//         std::cerr << "get_DBfile_parameters : return_connectionWrapper_ptr is nullptr ! and connectino_wrapped_ptr.get() is "
//         <<connection_wrapped_ptr.get() << std::endl;
//         return false;
//     }

//     sqlite3_exec(connection_wrapped_ptr->db_ptr,"BEGIN TRANSACTION",nullptr,nullptr,nullptr);

//     // blind parameters with slice_contents
//     int rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 1,
//                                db_info.header_info.file_id.data(), 
//                                db_info.header_info.file_id.size(), 
//                                SQLITE_STATIC);
//     rc = sqlite3_bind_int(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 2,
//                           db_info.header_info.slice_index);
//     rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 3,
//                            &db_info.header_info.AES_KEY,
//                            sizeof(db_info.header_info.AES_KEY),
//                            SQLITE_TRANSIENT);
//     rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 4,
//                            db_info.header_info.iv.data(),
//                            sizeof(db_info.header_info.iv),
//                            SQLITE_STATIC);

//     // update slice_records 
//     rc = sqlite3_step(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT));
//     if (rc != SQLITE_DONE || connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT) == nullptr)
//     {
//         std::cerr<< "Insert slice_contents failed and error :"<<sqlite3_errmsg(connection_wrapped_ptr->db_ptr)<<std::endl;
//         sqlite3_exec(connection_wrapped_ptr->db_ptr,"ROLLBACK",nullptr,nullptr,nullptr);
//         this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
//         return false;
//     }

//     sqlite3_reset(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT));

//     rc = UpdateReceivedSlices(db_info,connection_wrapped_ptr.get());
//     if (rc != SQLITE_DONE)
//     {
//         std::cerr<<"UpdateReceivedSlices failed and error is : "<<sqlite3_errmsg(connection_wrapped_ptr->db_ptr)<<std::endl;
//         sqlite3_exec(connection_wrapped_ptr->db_ptr,"ROLLBACK",nullptr,nullptr,nullptr);
//         this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
//         return false;
//     }

//     {
//         this->file_data_map[db_info.input_file_path]->fetch_add(db_info.header_info.plaintext_len, std::memory_order_relaxed);
//         this->m_worked_tasks.fetch_add(1, std::memory_order_relaxed);
//         // connection_wrapped_ptr->is_done = this->file_data_map[db_info.input_file_path]->load(std::memory_order_relaxed) == db_info.file_size;
//     }
//     sqlite3_exec(connection_wrapped_ptr->db_ptr,"COMMIT",nullptr,nullptr,nullptr);
//     this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
//     return SQLITE_DONE;
// }

int Sqlite_DB_process_transmission_and_write_to_file::UpdateReceivedSlices(const DB_Info &db_info, ConnectionWrapper* ConnectionWrapped_ptr)
{
    // 如果 db_info.slice_data_info.file_id  在 slice_records 中不存在， 则插入一条记录
    int rc = sqlite3_bind_blob(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::INSPECT_ID), 1,
                               db_info.header_info.file_id.data(),
                               db_info.header_info.file_id.size(),
                               SQLITE_STATIC);
    bool record_exists = ( sqlite3_step(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::INSPECT_ID)) == SQLITE_ROW ) ;
    sqlite3_reset(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::INSPECT_ID));

    if (!record_exists)
    {
        rc = sqlite3_bind_blob(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 1,
                               db_info.header_info.file_id.data(),
                               db_info.header_info.file_id.size(),
                               SQLITE_STATIC);
        rc = sqlite3_bind_int(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 4,
                              db_info.header_info.total_slices);
        rc = sqlite3_bind_int64(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 3,
                                static_cast<int64_t>(db_info.header_info.magic));
        rc = sqlite3_bind_text(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 2,
                               db_info.input_file_path.c_str(),
                               -1,
                               SQLITE_STATIC);
        rc = sqlite3_bind_text(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD), 5,
                               db_info.output_folder_path.c_str(),
                               -1,
                               SQLITE_STATIC);

        rc = sqlite3_step(ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD));
        if(rc != SQLITE_DONE)
        {
            std::cerr<<"NewRecorconnection_wrapper_ptr->db_ptruild failed and error is : "<<sqlite3_errmsg(ConnectionWrapped_ptr->db_ptr)<<std::endl;
            sqlite3_exec(ConnectionWrapped_ptr->db_ptr,"ROLLBACK",nullptr,nullptr,nullptr);
            return false;
        }
        sqlite3_reset((ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD)));
        return SQLITE_DONE; 
    }
    else
    {
        std::cout<<"slice_index : "<<db_info.header_info.slice_index<<" write in record ! "<<std::endl;
        sqlite3_reset((ConnectionWrapped_ptr->return_stmt_ptr(DB_Type::NEW_RECORD)));
        return SQLITE_DONE;
    }
    return 0;
}

const std::unordered_map<std::string,std::shared_ptr< std::atomic<int>> >& Sqlite_DB_process_transmission_and_write_to_file::return_file_data_map()
{
    return this->file_data_map;
}

void Sqlite_DB_process_transmission_and_write_to_file::set_file_data_map(const std::string &input_file_path)
{
    this->file_data_map[input_file_path] = std::make_shared<std::atomic<int>>(0);
}

void Sqlite_DB_process_transmission_and_write_to_file::delete_done_tasks_in_process_map()
{
    // do not delete DB_store , process_slice_information 
    // need to clear process_file_and_write_to_file , 

    //clear file_progress map
    this->file_data_map.clear(); 
    //reset count to doned tasks
    this->m_worked_tasks = 0;   
    DB_Info_raw_ptr db_info;
    //clear lockfree_queue , m_DB_Info_vector , m_swap_DB_Info_vector
    while(m_lockfree_queue.pop(db_info))
    {
        db_info.clear();
    }
    while(!m_DB_Info_vector.empty())
    {
        db_info = m_DB_Info_vector.back();
        db_info.clear();
    }
    while(!m_swap_DB_Info_vector.empty())
    {
        db_info = m_swap_DB_Info_vector.back();
        db_info.clear();
    }

    //clear thread_pool corresponding parameters
    this->m_thread_pool_ptr->clear_scheduled_tasks();
}

void Sqlite_DB_process_transmission_and_write_to_file::delete_done_tasks_periodicly(const std::unordered_map<std::string, bool> &file_data_map)
{
    if (!file_data_map.empty())
    {
        for (auto &it : file_data_map)
        {
            if (it.second == true)
            {
                this->file_data_map.erase(it.first);
            }
        }
    }
}

void Sqlite_DB_process_transmission_and_write_to_file::load_work_from_lockFreeQueue()
{
    DB_Info_raw_ptr db_info;
    while(m_lockfree_queue.pop(db_info) && m_DB_Info_vector.size() <1000)
    {
        // add tasks from queue to vector( empty() return true if queue is empty) )
        m_spdlogger->info("file_name : {} , slice_index : {}",
        *(db_info.input_file_path), db_info.header_info->slice_index);

        m_DB_Info_vector.push_back(db_info);
    }
    
    // call function to process the tasks in vector 
    std::swap(m_DB_Info_vector,m_swap_DB_Info_vector);
    if(m_swap_DB_Info_vector.empty())
    {
        return;
    }

    batch_deal_with_db_info(m_swap_DB_Info_vector);
    m_swap_DB_Info_vector.clear();
}

int Sqlite_DB_process_transmission_and_write_to_file::batch_deal_with_db_info(const std::vector<DB_Info_raw_ptr>& data_vector)
{
    std::string db_path(data_vector.begin()->db_file_path->data());
    auto connection_wrapped_ptr = this->m_connection_pool_ptr->return_connectionWrapper_ptr(db_path);
    sqlite3_exec(connection_wrapped_ptr->db_ptr,"BEGIN TRANSACTION",nullptr,nullptr,nullptr);
    if (connection_wrapped_ptr == nullptr || connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT)== nullptr)
    {
        std::cerr << "get_DBfile_parameters : return_connectionWrapper_ptr is nullptr ! and connectino_wrapped_ptr.get() is "
        <<connection_wrapped_ptr.get() << std::endl;
        return false;
    }

    for (auto& db_info : data_vector)
    {
        // blind parameters with slice_contents
        int rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 1,
                                   db_info.header_info->file_id.data(), 
                                   db_info.header_info->file_id.size(), 
                                   SQLITE_STATIC);
        rc = sqlite3_bind_int(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 2,
                              db_info.header_info->slice_index);
        rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 3,
                               &db_info.header_info->AES_KEY,
                               sizeof(db_info.header_info->AES_KEY),
                               SQLITE_TRANSIENT);
        rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 4,
                               db_info.header_info->iv.data(),
                               sizeof(db_info.header_info->iv),
                               SQLITE_STATIC);
        rc = sqlite3_bind_blob(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT), 5,
                               db_info.slice_data_info->data(),
                               db_info.slice_data_info->size(),
                               SQLITE_STATIC);
    
        // ready to insert slice_contents 
        rc = sqlite3_step(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT));
        if (rc != SQLITE_DONE || connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT) == nullptr)
        {
            std::cerr<< "Insert slice_contents failed and error :"<<sqlite3_errmsg(connection_wrapped_ptr->db_ptr)<<std::endl;
            sqlite3_exec(connection_wrapped_ptr->db_ptr,"ROLLBACK",nullptr,nullptr,nullptr);
            this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
            return false;
        }
    
        sqlite3_reset(connection_wrapped_ptr->return_stmt_ptr(DB_Type::CONTENT));

        {
            //update file_progress_map
            this->file_data_map[db_info.input_file_path->data()]->fetch_add(db_info.header_info->plaintext_size, std::memory_order_relaxed);
            //update tasks_worked_count  
            this->m_worked_tasks.fetch_add(1, std::memory_order_relaxed);
            // connection_wrapped_ptr->is_done = this->file_data_map[db_info.input_file_path->data()]->load(std::memory_order_relaxed) == db_info.file_size;
        }
    }
    sqlite3_exec(connection_wrapped_ptr->db_ptr,"COMMIT",nullptr,nullptr,nullptr);
    this->m_connection_pool_ptr->release_connectionWrapper_ptr(std::move(connection_wrapped_ptr));
    return SQLITE_DONE;
}

/**
 * @brief Decrypts shared data using AES-256-CBC encryption.
 *
 * @param ciphertext Input encrypted data to be decrypted
 * @param plaintext Output buffer for decrypted data (will be resized)
 * @param header Protocol header containing AES key and IV
 * @return true if decryption succeeded, false otherwise
 *
 * @note Uses OpenSSL EVP functions for decryption. The plaintext buffer
 *       will be automatically resized to fit the decrypted data.
 * @warning Proper error handling is implemented - returns false on any OpenSSL operation failure.
 */
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

const int Sqlite_DB_process_transmission_and_write_to_file::return_worked_tasks()
{
    return this->m_worked_tasks.load();
}

Sqlite_DB_Manager::Sqlite_DB_Manager(MqttClient* mqtt_client_ptr,std::shared_ptr<spdlog::logger> spdlogger):
    m_connection_pool_ptr(new ConnectionPool()),
    m_thread_pool_ptr(new Thread_pool()),
    m_mqtt_client_ptr(mqtt_client_ptr),
    m_transmission_info(new Sqlite_DB_process_transmission_and_write_to_file (m_thread_pool_ptr,m_mqtt_client_ptr,m_connection_pool_ptr,spdlogger)), 
    m_slice_info(new Sqlite_DB_process_slice_info(m_thread_pool_ptr,m_transmission_info)),
    m_store(new Sqlite_DB_store(m_connection_pool_ptr,m_slice_info)),
    m_spdlogger(spdlogger)
{
}

Sqlite_DB_Manager::~Sqlite_DB_Manager()
{
    delete m_store;
    delete m_slice_info;
    delete m_transmission_info;
    delete m_thread_pool_ptr;
    delete m_connection_pool_ptr;
}

void Sqlite_DB_Manager::Assign_tasks_to_sqlite_SB_Store(std::vector<MainWindows_Intermediate_Struct>& db_info)
{
    this->m_store->interface_DB_store(db_info);
}

const std::unordered_map<std::string, std::shared_ptr< std::atomic<int> > > &Sqlite_DB_Manager::return_file_data_map()
{
    return this->m_transmission_info->return_file_data_map();
}

int Sqlite_DB_Manager::return_worked_tasks()
{
    return this->m_transmission_info->return_worked_tasks();
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

void Sqlite_DB_Manager::merge_download_file(std::vector<MainWindows_Intermediate_Struct>& tem_vector)
{
    // std::vector<std::string> tem_file_path_vector;
    // for(auto& it: tem_vector )
    // {
    //     //collect tmp_ file to vector
    //     auto& stored_file_folder_path = it.stored_DB_folder_path;
    //     return_vector_file_path_merge(stored_file_folder_path,tem_file_path_vector);

    //     if(tem_file_path_vector.empty())
    //     {
    //         std::cerr<<"merge_download_file: "<<stored_file_folder_path<<" is empty\n";
    //         continue;
    //     }

    //     //merge tmp_ file from vector to one file
    //     concatenate_file(tem_file_path_vector, it); 
    //     tem_file_path_vector.clear();
    // }
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


int Sqlite_DB_Manager::load_work_from_lockfree_queue()
{
    m_transmission_info->load_work_from_lockFreeQueue();
    return 0;
}
