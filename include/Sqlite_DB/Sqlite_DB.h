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

    // bool ConfigDB(ConnectionWrapper* connection_wrapper);
    // bool executeSQL(const char* sql);
    bool OpenDB(std::string db_name , sqlite3** db);
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
    ~Sqlite_DB_process_transmission_and_write_to_file() = default;
    Sqlite_DB_process_transmission_and_write_to_file(Thread_pool *thread_pool_ptr, MqttClient *mqtt_client_ptr, ConnectionPool *connection_pool_ptr,std::shared_ptr<spdlog::logger> spdlogger); 

    int interface_process_transmission_and_write_to_file(
    const std::string &msg,
    std::shared_ptr<DB_Info> db_info);

    int write_message_to_lockFreeQueue(const std::string& proto_msg,std::shared_ptr<DB_Info> db_info);

    int write_dbData_to_file(const DB_Info& payload);

    int UpdateReceivedSlices(const DB_Info& db_info,ConnectionWrapper* ConnectionWrapped_ptr);

    int DecryptSharedData(const std::vector<uint8_t> &ciphertext, std::vector<uint8_t> &plaintext, ProtocolHeader &header);
    
    const int return_worked_tasks();
    const std::unordered_map<std::string,std::shared_ptr<std::atomic<int>>>& 
    return_file_data_map();

    void set_file_data_map(const std::string& db_info);

    void delete_done_tasks_in_process_map();

    void delete_done_tasks_periodicly(const std::unordered_map<std::string,bool>& file_data_map={});

    void load_work_from_lockFreeQueue();

    int batch_deal_with_db_info(const std::vector<DB_Info_raw_ptr>& db_info_vector);

    int verify_ptr_valid(const DB_Info_raw_ptr db_info_raw_ptr);
private:
    Thread_pool* m_thread_pool_ptr ;
    MqttClient* m_mqtt_client_ptr;
    ConnectionPool* m_connection_pool_ptr;
    std::unordered_map<std::string,std::shared_ptr<std::atomic<int>>> file_data_map = {};
    std::shared_mutex m_rwMutex;
    std::atomic<int> m_worked_tasks = 0;
    moodycamel::ConcurrentQueue<DB_Info_raw_ptr> m_lockfree_queue{1000};
    std::vector<DB_Info_raw_ptr> m_DB_Info_vector;
    std::vector<DB_Info_raw_ptr> m_swap_DB_Info_vector {};
    std::shared_ptr<spdlog::logger> m_spdlogger;
};

class Sqlite_DB_process_slice_info
{
public:
    Sqlite_DB_process_slice_info(Thread_pool* thread_pool_ptr,
    Sqlite_DB_process_transmission_and_write_to_file* transmssion_info_ptr);
    ~Sqlite_DB_process_slice_info()=default;

    int interface_process_slice_info(
    std::shared_ptr<DB_Info> db_info,
    std::vector<uint8_t> &&file_data_vector,
    std::vector<int> &&slice_index_vector,
    MainWindows_Intermediate_Struct& file_info
); 

    int ready_for_transmission_data(
    std::shared_ptr<DB_Info> db_info,
    std::vector<uint8_t> &&file_data_vector,
    std::vector<int> &&slice_index_vector,
    MainWindows_Intermediate_Struct& file_info); 

    bool is_control(uint8_t c);
  
    int initialize_m_transmission_info(
        std::shared_ptr<DB_Info> db_info
    );

   
private:
    Thread_pool* m_thread_pool_ptr ;
    Sqlite_DB_process_transmission_and_write_to_file* m_transmission_ptr;
};

class Sqlite_DB_store
{
public:
    Sqlite_DB_store(ConnectionPool* connection_pool_ptr,
    Sqlite_DB_process_slice_info* process_slice_ptr);
    ~Sqlite_DB_store()=default;

    int interface_DB_store(std::vector<MainWindows_Intermediate_Struct>& file_info);

    int initialize_connection_pool_ptr(std::shared_ptr<DB_Info> db_info);

    int get_DBfile_parameters(std::shared_ptr<DB_Info> db_info);

    std::vector<uint8_t> get_file_data_vector(std::shared_ptr<DB_Info> db_info);

    std::vector<int> get_failIndex_vector( std::shared_ptr<DB_Info> db_info);   

    int check_exist_dbFile_record(std::shared_ptr<DB_Info> db_info);

private:
    ConnectionPool* m_connection_pool_ptr ;
    Sqlite_DB_process_slice_info* m_slice_info;
};

class Sqlite_DB_Manager
{
    public:
        Sqlite_DB_Manager(MqttClient* mqtt_client_ptr,std::shared_ptr<spdlog::logger> spdlogger);
        ~Sqlite_DB_Manager();
        void Assign_tasks_to_sqlite_SB_Store(std::vector<MainWindows_Intermediate_Struct>& db_info);
        const std::unordered_map< std::string,std::shared_ptr< std::atomic<int> > >&
         return_file_data_map();

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

    private:
        ConnectionPool* m_connection_pool_ptr;
        Thread_pool* m_thread_pool_ptr;
        MqttClient* m_mqtt_client_ptr;
        Sqlite_DB_process_transmission_and_write_to_file* m_transmission_info;
        Sqlite_DB_process_slice_info* m_slice_info;
        Sqlite_DB_store* m_store;  
        std::shared_ptr<spdlog::logger> m_spdlogger; 
};
