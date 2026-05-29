#pragma once
#include <iostream>
#include <any>
#include <functional>
#include <thread>
#include <queue>
#include "MQTTEncryptionClient/MQTTEncryptionClient.h"
#include "Thread_pool/ConnectionPool.h"
#include "load_config/load_config.h"

class ISignalHandler;

// ========== ServiceResult 完整定义 ==========
template<typename T>
struct ServiceResult {
    bool successStatus;
    T data;
    std::string error_message;
    int error_code;
    
    // 私有构造器
private:
    struct SuccessTag {};
    struct ErrorTag {};
    
    ServiceResult(SuccessTag, T&& d) 
        : successStatus(true), data(std::forward<T>(d)), error_code(0) {}
    
    ServiceResult(SuccessTag, const T& d) 
        : successStatus(true), data(d), error_code(0) {}
    
    ServiceResult(ErrorTag, const std::string& msg, int code) 
        : successStatus(false), error_message(msg), error_code(code) {}
    
public:
    // 静态工厂方法
    static ServiceResult success(T data) {
        return ServiceResult(SuccessTag{}, std::move(data));
    }
    
    static ServiceResult error(const std::string& msg, int code = -1) {
        return ServiceResult(ErrorTag{}, msg, code);
    }
    
    // 便捷方法
    bool is_success() const { return success; }
    bool is_error() const { return !success; }
    
    T& value() { 
        if (!success) throw std::runtime_error("Accessing value of error result");
        return data; 
    }
    
    const T& value() const { 
        if (!success) throw std::runtime_error("Accessing value of error result");
        return data; 
    }
    
    std::string& error() { return error_message; }
    const std::string& error() const { return error_message; }
};

// ========== void 特化 ==========
template<>
struct ServiceResult<void> {
    bool success = false;
    std::string error_message = "";
    int error_code;
    
    static ServiceResult successStatus() {
        ServiceResult result;
        result.success = true;
        result.error_code = 0;
        return result;
    }
    
    static ServiceResult error(const std::string& msg, int code = -1) {
        ServiceResult result;
        result.success = false;
        result.error_message = msg;
        result.error_code = code;
        return result;
    }
    
    bool is_success() const { return success; }
    bool is_error() const { return !success; }
    std::string& error() { return error_message; }
};

class SignalRouter {
public:
    SignalRouter();
    ~SignalRouter();
    
    // 禁止拷贝
    SignalRouter(const SignalRouter&) = delete;
    SignalRouter& operator=(const SignalRouter&) = delete;
    
    // 注册信号处理器
    void register_handler(SignalType type, ISignalHandler* handler);
    
    // 注销信号处理器
    void unregister_handler(SignalType type);
    
    // 接收外部信号（主入口）
    void receive_signal(const ExternalSignal& signal);
    
    // 同步处理信号（立即处理）
    bool process_signal_sync(ExternalSignal& signal);
    
    // 统计信息结构体
    struct Statistics {
        size_t total_processed = 0;
        size_t total_succeeded = 0;
        size_t total_failed = 0;
        size_t queue_size = 0;
        std::unordered_map<SignalType, size_t> processed_by_type;
    };
    
    // 获取统计信息
    Statistics get_statistics();
    
private:
    // 分发信号到对应的处理器
    bool dispatch_signal(ExternalSignal& signal);
    
    // 更新统计信息
    void update_statistics(const ExternalSignal& signal, bool success);
    
    // 异步处理队列中的信号
    void process_queue();
    
    std::unordered_map<SignalType, ISignalHandler*> handlers_;
    std::mutex mutex_;
    
    std::queue<ExternalSignal> signal_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_;
    std::thread processing_thread_;
    
    Statistics stats_;
    std::mutex stats_mutex_;
};

class ISignalHandler {
public:
    virtual ~ISignalHandler() = default;
    
    // 处理信号
    virtual ServiceResult<void> handle_signal(ExternalSignal& signal) = 0;
    
    // 获取处理器支持的信号类型
    virtual std::vector<SignalType> get_supported_types() const = 0;

    // 获取处理器名称（用于调试）
    virtual std::string get_handler_name() const = 0;
    
    // 检查是否可以处理该信号
    virtual bool can_handle(const ExternalSignal &signal) const;

    // 获取处理的优先级（用于路由分发）
    virtual int get_priority() const ; 
};

class FileRecordDAO {
private:
    ConnectionPool* mainConn_pool_;  // 持有连接池，不持有具体连接
    
    // SQL语句（只保留业务相关的，移除表创建）
    struct SQL {
        static constexpr const char* INSERT = 
            "INSERT INTO file_records (file_id,input_file_path,missing_slices_json,subordinate_dbfile_path,last_modified_file) VALUES(?,?,?,?,?)";
        static constexpr const char* UPDATE = 
            "UPDATE file_records SET missing_slices_json = ?, last_modified_file = ? WHERE file_id = ?";
        static constexpr const char *SELECT_BY_PATH =
            "SELECT file_id, input_file_path, missing_slices_json FROM "
            "file_records WHERE input_file_path = ?";
        static constexpr const char *DELETE =
            "DELETE FROM file_records WHERE file_id = ?";
        static constexpr const char* SELECT_BY_ID = "SELECT * FROM file_records WHERE file_id = ?";
    };
    
    // 预编译语句缓存（每个DAO独立管理）
    struct PreparedStmts {
        sqlite3_stmt* insert_stmt = nullptr;
        sqlite3_stmt* update_stmt = nullptr;
        sqlite3_stmt* delete_stmt = nullptr;
        sqlite3_stmt* select_id_stmt = nullptr;
        sqlite3_stmt* select_path_stmt = nullptr;
        
        ~PreparedStmts() {
            sqlite3_finalize(insert_stmt);
            sqlite3_finalize(update_stmt);
            sqlite3_finalize(select_id_stmt);
            sqlite3_finalize(delete_stmt);
            sqlite3_finalize(select_path_stmt);
        }
    } stmts_;
    
public:
    FileRecordDAO(ConnectionPool* pool)
        : mainConn_pool_(pool){}

    struct FileRecordData {
      std::string file_id;
      std::string input_file_path;
      std::string missing_slices_json;
      bool is_valid; // 标记是否有效数据
    };

    // DAO 只负责：把给定的数据存到数据库
    int insertFileRecord(const std::array<uint8_t, 16> &file_id,
                         const std::string &sourcefile_path,
                         const std::string &missing_slices_json);
    int updateFileRecord(const std::string &file_id,
                         const std::string &file_path,
                         const std::string &missing_slices_index_json);
    std::vector<FileRecordData>
    queryFileRecordsByPath(const std::string &file_path);

    // 初始化：预编译语句
    bool initialize(std::unique_ptr<ConnectionWrapper> conn) {
      if (!conn)
        return false;

      return conn->prepare(SQL::INSERT, &stmts_.insert_stmt) &&
             conn->prepare(SQL::UPDATE, &stmts_.update_stmt) &&
             conn->prepare(SQL::DELETE, &stmts_.delete_stmt) &&
             conn->prepare(SQL::SELECT_BY_ID, &stmts_.select_id_stmt) &&
             conn->prepare(SQL::SELECT_BY_PATH, &stmts_.select_path_stmt);
    }
    
    // 业务方法
    std::unique_ptr<ConnectionWrapper>  createNewMainConnection(const std::string &sourcefile_path);
};

class SliceRecordDAO {
public:
  ConnectionPool *SubConn_pool_; // 持有连接池，不持有具体连接

  std::unique_ptr<ConnectionWrapper>
  createNewSubConnection(const std::string &sourcefile_path);

  // 初始化：预编译所有SQL语句
  bool initialize();
  //    业务函数
  bool insert_slice(const TestMsg &msg);
  // 2. 批量插入切片记录（事务优化）
  bool insert_slices_batch(const std::vector<SliceRecord> &records);
  // 3. 查询指定文件的所有切片
  std::vector<TestMsg> select_all_by_file_id(const std::string &file_id);
  // 4. 删除指定文件的所有切片
  bool delete_by_file_id(const std::string &file_id);
  // 5. 统计指定文件的切片数量
  int count_by_file_id(const std::string &file_id);
  // 6. 检查文件是否有切片
  bool has_slices(const std::string &file_id);
  // 7. 获取切片索引列表
  std::vector<int> get_slice_indices(const std::string &file_id);
  // 8. 删除并返回被删除的切片（用于迁移）
  std::vector<TestMsg> delete_and_return_slices(const std::string &file_id);

private:
  struct SQL {
    static constexpr const char *INSERT =
        "INSERT INTO slice_contents (file_id, slice_index, aes_key, iv, "
        "plaintext) VALUES (?, ?, ?, ?, ?)";
    static constexpr const char *SELECT_ALL_BY_FILE_ID =
        "SELECT * FROM slice_contents WHERE file_id = ? ORDER BY slice_index "
        "ASC";
    static constexpr const char *DELETE_BY_FILE_ID =
        "DELETE FROM slice_contents WHERE file_id = ?";
    static constexpr const char *COUNT_BY_FILE_ID =
        "SELECT COUNT(*) FROM slice_contents WHERE file_id = ?";
  };

  struct stmtStruct{
    sqlite3_stmt *insert_stmt = nullptr;
    sqlite3_stmt *select_stmt = nullptr;
    sqlite3_stmt *delete_stmt = nullptr;
    sqlite3_stmt *count_stmt = nullptr;
  } stmts_;
};