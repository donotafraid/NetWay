#pragma once

#include "load_config/load_config.h"

enum class DB_Type{
    CONTENT,
    GET_FAIL_INDEX
};

class ConnectionWrapper{
    public:
    
    ~ConnectionWrapper();
    ConnectionWrapper(std::string db_file_name)  ;
    
    int prepareStatements () ;
    void reset () ;
    void close_db_file();
    int open_db_file(std::string& db_file_name);
    
    void set_db_file_path(std::string db_file_name);
    int initialize_connection_wrapper(std::string db_file_path);
    bool check_table_exists();
    
    const char* read_missing_slices_from_db_subordinate_file_sql = R"(
        WITH expected_indices AS(
            SELECT value AS expected_index
            FROM json_each(?)
        ),
        existing_indices AS(
            SELECT slice_index
            FROM slice_contents 
            WHERE file_id = ?
        )
        SELECT e.expected_index AS missing_index
        FROM expected_indices e
        LEFT JOIN existing_indices ex ON e.expected_index = ex.slice_index
        WHERE ex.slice_index IS NULL    
        ORDER BY e.expected_index;
        )";
    sqlite3_stmt* read_missing_slices_from_db_subordinate_file_stmt_ptr = nullptr;

    bool is_done = false;
    std::string return_db_file_path();
    sqlite3_stmt* return_stmt_ptr(DB_Type type);
    sqlite3* db_ptr = nullptr;
    private:
        sqlite3_stmt* stmt_newRecord_ptr = nullptr; 
        std::string db_file_path = ""; // distiction between different connection_wrapper
    const char* Create_Table[4] = {
        R"(CREATE TABLE slice_contents (
        id INTEGER PRIMARY KEY AUTOINCREMENT, 
        file_id BLOB NOT NULL,
        slice_index INTEGER NOT NULL CHECK(slice_index >= 0), 
        aes_key BLOB NOT NULL CHECK(length(aes_key) = 32),    
        iv BLOB NOT NULL CHECK(length(iv) = 16),    
        plaintext BLOB NOT NULL,
        UNIQUE(file_id, slice_index)  
        ))"
        }; 
}; 


class ConnectionPool
{
    public:
        ConnectionPool() = default;
        ~ConnectionPool();
        std::unique_ptr<ConnectionWrapper> return_connectionWrapper_ptr();
        void release_connectionWrapper_ptr(std::unique_ptr<ConnectionWrapper> Wrapper_ptr);
        void close_db_file_opened();
        std::unique_ptr<ConnectionWrapper> check_use_ptr_in_connection_pool(std::string db_file_name);
        std::string verify_db_path_memorySize();
        std::string return_current_date_string();
        std::string db_file_pre = "./DownloadFileManagement/";
        
    private:
        std::deque<std::unique_ptr<ConnectionWrapper>> m_connection_pool;
        std::mutex m_connection_mutex;
        std::condition_variable m_connection_cv;
        std::unordered_set<std::string> m_db_file_paths_set;
};

class memory_pool
{
    public:
    static memory_pool& getInstance()
    {
        std::call_once(m_once_flag,[]()
        {
            instance.reset(new memory_pool);
        });
        return *instance;
    }

    ~memory_pool();

    void push(std::shared_ptr<request_message>&& db_info);
    std::shared_ptr<request_message> return_ptr();
    std::shared_ptr<request_message> return_pre_ptr();
    std::vector<int> check_fail_index();
    void clear_memory_pool();
    int size();
    
    private:
    //  允许创建对象
    memory_pool() = default;

    // 显式禁用拷贝和移动
    memory_pool(const memory_pool&) = delete;
    memory_pool& operator=(const memory_pool&) = delete;
    memory_pool(memory_pool&&) = delete;
    memory_pool& operator=(memory_pool&&) = delete;

    static std::unique_ptr<memory_pool> instance;
    static std::once_flag m_once_flag;
    std::queue<std::shared_ptr<request_message>> m_queue;
};

