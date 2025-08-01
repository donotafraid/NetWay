#pragma once

#include "load_config/load_config.h"

enum class DB_Type{
    CONTENT,
    INSPECT_ID,
    NEW_RECORD,
    GETINDEX,
    GETFAILINDEX
};

class ConnectionWrapper{
    public:
    
    ~ConnectionWrapper();
    ConnectionWrapper(std::string db_file_name)  ;
    
    int prepareStatements () ;
    void reset () ;
    void close_db_file();
    int open_db_file(std::string& db_file_name);
    void set_db_name(std::string db_file_name);
    int initialize_connection_wrapper(std::string db_file_path);
    bool check_table_exists();

    std::string return_db_name();
    sqlite3_stmt* return_stmt_ptr(DB_Type type);
    bool is_done = false;
    sqlite3* db_ptr = nullptr;
    private:
        sqlite3_stmt* stmt_content_ptr = nullptr; 
        sqlite3_stmt* stmt_InspectID_ptr = nullptr;
        sqlite3_stmt* stmt_NewRecord_ptr = nullptr;
        sqlite3_stmt* stmt_GetIndex_ptr = nullptr;
        sqlite3_stmt* stmt_GetFailIndex_ptr = nullptr;
        std::string db_name = ""; // distiction between different connection_wrapper
    const char* Create_Table[2] = {
        R"(
        CREATE TABLE IF NOT EXISTS slice_records (
        file_id BLOB PRIMARY KEY NOT NULL,  
        input_file_path TEXT NOT NULL,
        magic INTEGER DEFAULT 0xDEADBEEF CHECK(magic = 0xDEADBEEF),  
        total_slices INTEGER NOT NULL CHECK(total_slices > 0),
        output_folder_path TEXT NOT NULL, 
        missing_slices_json, -- Store missing slices as JSON array,
        UNIQUE(file_id)
        ))",

        R"(CREATE TABLE slice_contents (
        id INTEGER PRIMARY KEY AUTOINCREMENT, 
        file_id BLOB NOT NULL,
        slice_index INTEGER NOT NULL CHECK(slice_index >= 0), 
        aes_key BLOB NOT NULL CHECK(length(aes_key) = 32),    
        iv BLOB NOT NULL CHECK(length(iv) = 16),    
        plaintext BLOB NOT NULL,
        FOREIGN KEY(file_id) REFERENCES slice_records(file_id) ON DELETE CASCADE,
        UNIQUE(file_id, slice_index)  
        ))"
        }; 
}; 


class ConnectionPool
{
    public:
        ConnectionPool() = default;
        std::unique_ptr<ConnectionWrapper> return_connectionWrapper_ptr(std::string db_file_name);
        void release_connectionWrapper_ptr(std::unique_ptr<ConnectionWrapper> Wrapper_ptr);
        void close_db_file_opened();
        std::unique_ptr<ConnectionWrapper> check_use_ptr_in_connection_pool(std::string db_file_name);
        ~ConnectionPool();
        
    private:
        std::deque<std::unique_ptr<ConnectionWrapper>> m_connection_pool;
        std::mutex m_connection_mutex;
        std::condition_variable m_connection_cv;
        std::unordered_set<std::string> m_db_file_paths_set;
};