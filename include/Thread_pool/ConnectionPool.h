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
    bool createTableIfNotExists() {
      // 开始事务
      begin_transaction();

      bool success = true;

      // 创建表
      const char *create_table_sql =
          "CREATE TABLE IF NOT EXISTS cache ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "timestamp INTEGER NOT NULL,"
          "tag_name TEXT NOT NULL," // 改为 TEXT
          "value REAL NOT NULL,"
          "raw_metric TEXT NOT NULL," // 改为 TEXT
          //-- 新增状态管理字段
          "status INTEGER DEFAULT 0,"  //-- 0: 待发送, 1: 发送中, 2: 已成功, 3: 永久失败
          "retry_count INTEGER DEFAULT 0,"
          "last_attempt_time INTEGER,"
          "created_at INTEGER DEFAULT (strftime('%s', 'now')),"
          "updated_at INTEGER DEFAULT (strftime('%s', 'now'))"
          ")";

      if (!execute(create_table_sql)) {
        success = false;
      }

      // 创建索引（使用 IF NOT EXISTS 避免重复）
      if (success) {
        const char *create_index1_sql =
            "CREATE INDEX IF NOT EXISTS idx_timestamp ON "
            "cache(timestamp)";
        if (!execute(create_index1_sql)) {
          success = false;
        }
      }

      if (success) {
        const char *create_index2_sql =
            "CREATE INDEX IF NOT EXISTS idx_tagname ON "
            "cache(tag_name)";
        if (!execute(create_index2_sql)) {
          success = false;
        }
      }

      if (success) {
        const char *create_index2_sql =
            "CREATE INDEX  IF NOT EXISTS idx_cache_status_created ON "
            "cache(status, created_at)";
        if (!execute(create_index2_sql)) {
          success = false;
        }
      }

      // 提交或回滚事务
      return commit_transaction();
    }

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
    
     // === 新增：获取预编译语句（由上层管理） ===
    bool prepareStatement(const std::string& sql, sqlite3_stmt** stmt) {
        return prepare(sql, stmt);
    }
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
        std::string verify_openedPath_memorySize();
        std::string return_current_date_string();
        std::string db_file_pre = "./DownloadFileManagement/";

        // trim function
        std::string generateSuffix(int counter);

      private:
        std::vector<std::string> connectFilePathVec;
        std::deque<std::unique_ptr<ConnectionWrapper>> m_connection_pool;
        std::mutex m_connection_mutex;
        std::condition_variable m_connection_cv;
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

