#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include <array>
#include <cstdint>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <google/protobuf/stubs/port.h>
#include <QtCore/qobjectdefs.h>
#include <QtCore/qobject.h>

#include "MQTTEncryptionClient/MQTTEncryptionClient.h"
#include "ProtocolHeader/ProtocolHeader.h"
#include "Thread_pool/Thread_pool_define.h"

using  namespace boost::uuids;

class Sqlite_DB;

class Sqlite_DB_Create
{
public:
    static std::unique_ptr<Sqlite_DB> create();
};

class Sqlite_DB : public QObject 
{
    Q_OBJECT
public:
    friend class Sqlite_DB_Create;
    // 获取单例对象
    static Sqlite_DB& GetInstance()
    {
        if(db_instance== nullptr)
        {
            db_instance = Sqlite_DB_Create::create();
        }
        return *db_instance;
    }

    bool OpenDB(std::string db_file_path , sqlite3** db);
    bool CreateTable(std::string table_name,sqlite3* db);
    bool check_table_exist(sqlite3* db);
    std::unordered_map<std::array<uint8_t, 16>,FileInfo,ArrayHash> DB_Info_Map;
    static bool CloseDB();
    ~Sqlite_DB();
private:
    Sqlite_DB(const Sqlite_DB& db) = delete;
    Sqlite_DB& operator=(const Sqlite_DB& db) = delete;
    Sqlite_DB() = default ;

    MqttClient* m_mqtt_client;
   
    static std::unique_ptr<Sqlite_DB> db_instance ;

};

class Sqlite_DB_process_transmission_and_write_to_file
{
public:
    ~Sqlite_DB_process_transmission_and_write_to_file() ;
    Sqlite_DB_process_transmission_and_write_to_file(std::shared_ptr<spdlog::logger> spdlogger, std::queue<std::string>& m_responding_queue); 

    int write_message_to_lockFreeQueue(const std::string& proto_msg,std::shared_ptr<DB_Info> db_info);

    int DecryptSharedData(const std::vector<uint8_t> &ciphertext, std::vector<uint8_t> &plaintext, ProtocolHeader &header);
    
    void delete_done_tasks_in_process_map();

    void load_work_from_lockFreeQueue();

    void ready_for_transmission_data(std::string& msg, std::vector<uint8_t> &file_data_vector,std::vector<int> &slice_index_vector, std::condition_variable& condition_variable );

    bool is_control(uint8_t c);

    std::string return_task_from_queue();

    private:
    Thread_pool* m_thread_pool_ptr ;
    std::shared_ptr<spdlog::logger> m_spdlogger;
    std::queue<std::string>& m_responding_queue;
    std::mutex m_responding_mutex;
    moodycamel::ConcurrentQueue<DB_Info_raw_ptr> m_lockfree_queue{1000};
};

class Sqlite_DB_Manager
{
    public:
    Sqlite_DB_Manager(MqttClient* mqtt_client_ptr,std::shared_ptr<spdlog::logger> spdlogger);
        ~Sqlite_DB_Manager();
        bool Assign_tasks_to_sqlite_SB_Store();
        const std::unordered_map< std::string,std::shared_ptr< std::atomic<int> > >&
        return_file_data_map();

        int fill_task_to_queue(std::string& msg,std::vector<int>& missing_slices_index_vector,std::vector<uint8_t>& file_data_vector);
        int return_worked_tasks();
        int return_scheduled_tasks();
        void thread_pool_start();
        bool return_is_threadPool_active(); 
        void return_vector_file_path_merge(const std::string & folder_path,std::vector<std::string>& file_path_vector); 
        void set_scheduled_tasks_to_ThreadPOol(int num);
        void delete_done_tasks_in_process_map();
        void merge_download_file(std::vector<MainWindows_Intermediate_Struct>& tem_vector);
        void concatenate_file(std::vector<std::string>& file_path_vector,const MainWindows_Intermediate_Struct& file_info );
        int load_work_from_lockfree_queue();
        std::vector<int> return_failIndex_vector(std::shared_ptr<DB_Info> db_info);
        int send_request_to_server(std::string& request);
        int presetting();
        std::shared_ptr<std::unordered_map<std::string,MainWindows_Intermediate_Struct>>  return_fileInfoMap();

        memory_pool& m_memory_pool_ref;
        private:
        ConnectionPool* m_connection_pool_ptr;
        Thread_pool* m_thread_pool_ptr;
        MqttClient* m_mqtt_client_ref;
        Sqlite_DB_process_transmission_and_write_to_file* m_transmission_info;
        // Sqlite_DB_store* m_store;  
        std::shared_ptr<spdlog::logger> m_spdlogger; 
};

class Sqlite_DB_function{
    public:
        Sqlite_DB_function(memory_pool& memory_pool_ref);
        ~Sqlite_DB_function();

        void presetting(); 
        void update_memory_pool(const std::string& file_path);
        std::string return_current_date_string();
        std::string verify_db_path_memorySize();
        int create_new_file_on_db_information(const std::string& file_path);
        int create_new_file_on_subordinate_db_record(const std::string& file_path);
        bool read_missing_slices_from_db_information_file(const std::string& file_path);
        std::vector<int> read_missing_slices_from_db_subordinate_file(request_message& request_message_ref,const std::string& last_update_db_subordinate_file_path);
        std::vector<int> return_continous_sequence(int file_size);

        // Sqlite_available_subordinate_file* m_sqlite_available_subordinate_file_ptr;
        memory_pool& m_memory_pool_ref;
        ConnectionPool* m_connection_pool_ptr;
        sqlite3* db_information_ptr = nullptr;
        sqlite3* db_subordinate_ptr = nullptr;

        std::string db_file_pre = "./DownloadFileManagement/";
        std::string db_file_name = "./DownloadFileManagement/main.db";
        const char* read_sourcefile_from_db_information_sql = "SELECT * FROM file_records WHERE input_file_path = ?";
        const char* check_table_exist_sql = "SELECT * FROM sqlite_master WHERE type='table' AND name='file_records'";
        const char* create_new_db_information_record_sql = "INSERT INTO file_records (file_id,input_file_path,missing_slices_json,subordinate_dbfile_path,last_modified_file) VALUES(?,?,?,?,?)";
        const char* create_new_subordinate_db_record_sql = "INSERT INTO slice_records (file_id,input_file_path,magic,total_slices,output_file_path,missing_slices_json) VALUES(?,?,?,?,?,?)";
        const char* update_db_information_sql = "UPDATE file_records SET missing_slices_json = ? , subordinate_dbfile_path = ? , last_modified_file = ? WHERE file_id = ?";

        sqlite3_stmt* read_sourcefile_from_db_information_file_stmt_ptr = nullptr;
        sqlite3_stmt* check_table_exist_stmt_ptr = nullptr;
        sqlite3_stmt* create_new_db_information_record_stmt_ptr = nullptr;
        sqlite3_stmt* create_new_subordinate_db_record_stmt_ptr = nullptr;
        sqlite3_stmt* update_db_information_stmt_ptr = nullptr;
    
        const char* Create_Table[4] = {
        R"(CREATE TABLE IF NOT EXISTS file_records(
        id INTEGER PRIMARY KEY AUTOINCREMENT, 
        file_id BLOB NOT NULL,
        input_file_path TEXT NOT NULL,
        missing_slices_json, -- Store missing slices as JSON array,
        subordinate_dbfile_path BLOB,
        last_modified_file BLOB,
        UNIQUE(file_id,missing_slices_json)
        ))"
        }; 
};

class Sqlite_DB_write_file{
    public:
        Sqlite_DB_write_file(Sqlite_DB_function& sqlite_db_function_ptr)
        :m_sqlite_db_function_ref(sqlite_db_function_ptr){
            std::cout<<"db_function_ref: "<<&m_sqlite_db_function_ref<<std::endl;
        };
        ~Sqlite_DB_write_file()  
        {
            std::cout << "Sqlite_DB_write_file Destructor" << std::endl;
        }


        Sqlite_DB_function& m_sqlite_db_function_ref;

        int write_message_in_db_subordinate_file_in_batch(std::queue<mqtt::const_message_ptr>& m_tmp_received_messages_queue);
        int blob_parameter_insert(ConnectionWrapper* Wrapper_ptr,TestMsg& msg);
};

class Sqlite_information{
    public:
        Sqlite_information(Sqlite_DB_function& sqlite_function_ref)
        :m_sqlite_db_function_ref(sqlite_function_ref){

        };
        ~Sqlite_information() = default;

        void presetting_db_information();
        Sqlite_DB_function& m_sqlite_db_function_ref;
};

class taskExecution
{
    public:
    taskExecution(MqttClient& mqtt_client_ptr,memory_pool& memory_pool_ref)
    :m_mqtt_client_ref(mqtt_client_ptr),m_memory_pool_ref(memory_pool_ref){}

    MqttClient& m_mqtt_client_ref;
    memory_pool& m_memory_pool_ref;

    int send_task();
    void write_task_to_db_suborinate_file();
    void delete_task_from_memory_pool(const std::string& file_path);
};