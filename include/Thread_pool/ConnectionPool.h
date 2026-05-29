#pragma once

#include "load_config/load_config.h"

enum class DB_Type{
    CONTENT,
    GET_FAIL_INDEX,
    MERGE_SELECT_FILE,
    DELETE_SELECT_FILE
};

class ConnectionWrapper {
private:
    sqlite3* db_ptr_ = nullptr;
    std::string db_file_path_ = "";
    bool in_transaction_ = false;
    
public:
    // === 构造/析构 ===
    ConnectionWrapper() = default;
    explicit ConnectionWrapper(const std::string& db_file_path);
    ~ConnectionWrapper();
    
    // 禁止拷贝，允许移动
    ConnectionWrapper(const ConnectionWrapper&) = delete;
    ConnectionWrapper& operator=(const ConnectionWrapper&) = delete;
    ConnectionWrapper(ConnectionWrapper&& other) noexcept;
    ConnectionWrapper& operator=(ConnectionWrapper&& other) noexcept;
    
    // === 连接管理 ===
    bool open(const std::string& db_file_path);
    void close();
    bool is_open() const { return db_ptr_ != nullptr; }
    void reset(sqlite3_stmt *stmt); // 重置所有状态

    // === 底层执行能力 ===
    bool execute(const std::string& sql);
    bool prepare(const std::string& sql, sqlite3_stmt** stmt);
    int step(sqlite3_stmt* stmt);
    void reset_stmt(sqlite3_stmt* stmt);
    void finalize_stmt(sqlite3_stmt* stmt);
    void clear_bindings(sqlite3_stmt* stmt);
    
    // === 事务管理 ===
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();
    
    // === 辅助方法 ===
    std::string get_last_error() const;
    int64_t get_last_insert_rowid() const;
    int get_changes() const;
    std::string get_db_file_path() const { return db_file_path_; }
    
    // 谨慎暴露：仅供需要直接sqlite3操作的场景
    sqlite3* raw_DBhandle() { return db_ptr_; }
};

class ConnectionPool
{
    public:
        ConnectionPool() = default;
        ~ConnectionPool();
        std::unique_ptr<ConnectionWrapper> get_SubConnection();
        std::unique_ptr<ConnectionWrapper> get_MainConnection(const std::string &mainFilePath);
        void release_connectionWrapper_ptr(std::unique_ptr<ConnectionWrapper> Wrapper_ptr);
        void close_db_file_opened();
        // 实现数据库连接的池化管理
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

