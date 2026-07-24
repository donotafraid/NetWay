#pragma once

#include <iostream>
#include <any>
#include <functional>
#include <thread>
#include <queue>
#include <cstdint>
#include "Thread_pool/ConnectionPool.h"
#include "load_config/load_config.h"

class SQLiteSliceExecutor;

// 数据定义
// ============================================================
struct SliceRecord {
  int64_t id;             // 自增主键
  int64_t timestamp;      // Unix时间戳
  std::string tag_name;   // 标签名
  float value;           // 数值
  std::string raw_metric; // 完整的Prometheus格式文本
  int64_t created_at;     // 创建时间（自动生成）
  std::string help_text;
  std::string metric_type; // "gauge", "counter", etc.
  std::string job_name;
  std::string instance_name;

  // 默认构造
  SliceRecord() = default;

  // 带参构造（按值传参 + move，简洁高效）
  SliceRecord(int64_t ts, std::string name, float val,std::string rawData)
      : timestamp(ts), tag_name(std::move(name)), value(val),raw_metric(std::move(rawData)) {}

  // 拷贝和移动操作（都标记 noexcept）
  SliceRecord(const SliceRecord &) = default;
  SliceRecord(SliceRecord &&) noexcept = default;
  SliceRecord &operator=(const SliceRecord &) = default;
  SliceRecord &operator=(SliceRecord &&) noexcept = default;
  ~SliceRecord() = default;
};

// ============================================================
// 角色A：统筹协调者（主类）
// ============================================================
class SliceRecordCoordinator {
public:
    // 状态枚举
    enum class Status {
        UNINITIALIZED = 0,
        READY = 1,
        ERROR = 2,
        SHUTDOWN = 3
    };

    // 配置结构
    struct Config {
        size_t batch_threshold = 100;      // 批量阈值
        size_t batch_size = 50;            // 每批大小
        int max_retries = 3;               // 最大重试次数
        int retry_interval_ms = 100;       // 重试间隔（毫秒）
        bool enable_debug_log = false;     // 是否启用调试日志
    };

    // ========== 构造与析构 ==========
    explicit SliceRecordCoordinator(std::shared_ptr<ConnectionPool> pool);
    explicit SliceRecordCoordinator(std::shared_ptr<ConnectionPool> pool, const Config& config);
    ~SliceRecordCoordinator();

    // 禁止拷贝
    SliceRecordCoordinator(const SliceRecordCoordinator&) = delete;
    SliceRecordCoordinator& operator=(const SliceRecordCoordinator&) = delete;

    // 禁止移动
    SliceRecordCoordinator(SliceRecordCoordinator&&) = delete;
    SliceRecordCoordinator& operator=(SliceRecordCoordinator&&) = delete;

    // ========== 统筹：初始化 ==========
    bool initialize();

    // ========== 统筹：业务接口 ==========
    
    // 1. 插入单条切片记录
    bool insertSlice(const SliceRecord& record);
    
    // 2. 批量插入切片记录（自动选择策略）
    bool insertSlicesBatch(const std::vector<SliceRecord>& records);
    
    // 3. 查询指定文件的所有切片
    std::vector<SliceRecord> selectByTag(ConnectionWrapper *conn,
                                         const std::string &tag_name);

    // 4. 删除指定文件的所有切片
    bool deleteByTagName(const std::string& file_id);
    
    // 5. 统计指定文件的切片数量
    int countByTag(const std::string& file_id);
    
    // 6. 检查文件是否有切片
    bool hasSlices(const std::string& file_id);
    
    // 7. 获取切片索引列表
    std::vector<int> getSliceIndices(const std::string& file_id);
    
    // 8. 删除并返回被删除的切片（用于迁移）
    std::vector<SliceRecord> deleteAndReturnSlices(const std::string& file_id);

    // ========== 连接管理（兼容旧接口） ==========
    std::unique_ptr<ConnectionWrapper> 
    createNewSubConnection(const std::string& sourcefile_path);

    // ========== 状态查询 ==========
    bool isReady() const;
    Status getStatus() const;
    Config getConfig() const;
    void setConfig(const Config& config);

    // ========== 统计信息 ==========
    struct Statistics {
        std::atomic<uint64_t> total_inserts{0};
        std::atomic<uint64_t> total_deletes{0};
        std::atomic<uint64_t> total_queries{0};
        std::atomic<uint64_t> success_inserts{0};
        std::atomic<uint64_t> failed_inserts{0};
        std::atomic<uint64_t> batch_count{0};
    };
    
    void resetStatistics();

private:
    // ========== 成员变量 ==========
    std::shared_ptr<ConnectionPool> pool_;
    std::unique_ptr<SQLiteSliceExecutor> executor_;
    Config config_;
    std::atomic<bool> initialized_;
    std::atomic<Status> status_;
    mutable std::mutex mutex_;
    Statistics stats_;

    // ========== 统筹：连接管理 ==========
    std::unique_ptr<ConnectionWrapper> acquireConnection();
    void releaseConnection( std::unique_ptr<ConnectionWrapper> conn);
    std::unique_ptr<ConnectionWrapper> acquireConnectionWithRetry();

    // ========== 统筹：策略方法 ==========
    bool insertBatchSmall(const std::vector<SliceRecord>& records);
    bool insertBatchLarge(const std::vector<SliceRecord>& records);

    // ========== 统筹：辅助方法 ==========
    bool checkReady() const;
    bool validateRecord(const SliceRecord& record) const;
    bool createTableIfNotExists(ConnectionWrapper *conn);
    void shutdown();

    // ========== 统筹：后处理钩子 ==========
    void onInsertSuccess(const SliceRecord& record);
    void onInsertFailure(const SliceRecord& record);
    void onDeleteSuccess(const std::string& file_id);
    void onDeleteFailure(const std::string& file_id);

    // ========== 统筹：日志方法 ==========
    void logInfo(const std::string& msg) const;
    void logWarn(const std::string& msg) const;
    void logError(const std::string& msg) const;
    void logDebug(const std::string& msg) const;
};

// ============================================================
// 角色D：数据转换器（协作组件）
// ============================================================
class SQLiteDataConverter {
public:
    // 绑定方法
    static void bindText(sqlite3_stmt* stmt, int index, const std::string& value);
    static void bindBlob(sqlite3_stmt* stmt, int index, const std::vector<uint8_t>& value);
    static void bindInt(sqlite3_stmt* stmt, int index, int value);
    static void bindInt64(sqlite3_stmt* stmt, int index, int64_t value);
    
    // 提取方法
    static std::string extractText(sqlite3_stmt* stmt, int index);
    static std::vector<uint8_t> extractBlob(sqlite3_stmt* stmt, int index);
    static int extractInt(sqlite3_stmt* stmt, int index);
    static int64_t extractInt64(sqlite3_stmt* stmt, int index);
    
    // 业务对象转换
    static void bindSliceRecord(sqlite3_stmt* stmt, const SliceRecord& record);
    static SliceRecord extractSliceRecord(sqlite3_stmt* stmt);
};

// ============================================================
// 角色C：SQL执行引擎（协作组件）
// ============================================================
class SQLiteSliceExecutor {
public:
  SQLiteSliceExecutor() = default; // 不再需要构造函数传入连接
  ~SQLiteSliceExecutor();

  // 禁止拷贝
  SQLiteSliceExecutor(const SQLiteSliceExecutor &) = delete;
  SQLiteSliceExecutor &operator=(const SQLiteSliceExecutor &) = delete;

  // 禁止移动
  SQLiteSliceExecutor(SQLiteSliceExecutor &&) = delete;
  SQLiteSliceExecutor &operator=(SQLiteSliceExecutor &&) = delete;

  // 所有方法都接收 ConnectionWrapper 参数
  bool insertSlice(ConnectionWrapper *conn, const SliceRecord &record);
  std::vector<SliceRecord> selectByTag(ConnectionWrapper *conn,
                                       const std::string &tag_name);
  bool deleteByTag(ConnectionWrapper *conn, const std::string &tag_name);
  int countByTag(ConnectionWrapper *conn, const std::string &tag_name);
  // 扩展查询
  std::vector<SliceRecord> selectByTimeRange(ConnectionWrapper *conn,
                                             int64_t start_time,
                                             int64_t end_time);
  std::vector<SliceRecord> selectByTagAndTimeRange(ConnectionWrapper *conn,
                                                   const std::string &tag_name,
                                                   int64_t start_time,
                                                   int64_t end_time);
  std::vector<SliceRecord> selectByNum(ConnectionWrapper *conn, int special_status,int limit);
  int updateBatchStatus(ConnectionWrapper *conn, int new_status, int old_status,
                        int limit);
  int updateBatchStatusCount(ConnectionWrapper *conn, int special_status);
  // 数据清理
  bool deleteByTime(ConnectionWrapper *conn, int64_t before_time);
  bool deleteByTagAndTime(ConnectionWrapper *conn, const std::string &tag_name,
                          int64_t before_time);
  // 清理过期数据（保留最近N天的数据）
  bool cleanOldData(ConnectionWrapper* conn, int days_to_keep);

  bool execute(ConnectionWrapper *conn, const std::string &sql);


  bool prepareStatements(ConnectionWrapper *conn);
private:
  sqlite3_stmt *insert_stmt_ = nullptr;
  sqlite3_stmt *select_stmt_ = nullptr;         // SELECT_BY_TAG
  sqlite3_stmt *delete_stmt_ = nullptr;         // DELETE_BY_TAG
  sqlite3_stmt *count_stmt_ = nullptr;          // COUNT_BY_TAG
  sqlite3_stmt *select_by_num_stmt_ = nullptr; // SELECT_BY_NUM
  sqlite3_stmt *select_by_time_stmt_ = nullptr; // SELECT_BY_TIME_RANGE
  sqlite3_stmt *delete_by_time_stmt_ = nullptr; // DELETE_BY_TIME
  sqlite3_stmt *select_latest_stmt_ = nullptr;  // SELECT_LATEST_BY_TAG
  sqlite3_stmt *stats_stmt_ = nullptr;          // STATS_BY_TAG
  sqlite3_stmt *update_status_stmt = nullptr;
  sqlite3_stmt *update_status_count_stmt = nullptr;
  sqlite3_stmt *delete_success_status_stmt = nullptr;
  sqlite3_stmt *delete_failture_status_stmt = nullptr;
  mutable std::mutex mutex_;

  bool prepareStatus = false;

  // SQL语句常量
  struct SQL {
    // 插入或替换缓存记录
    static constexpr const char *INSERT =
        "INSERT OR REPLACE INTO cache "
        "(timestamp, tag_name, value, raw_metric) "
        "VALUES (?, ?, ?, ?)";

    // 查询指定标签的所有记录（按时间戳排序）
    static constexpr const char *SELECT_BY_TAG =
        "SELECT * FROM cache WHERE tag_name = ? ORDER BY timestamp ASC";

    // 查询指定时间范围内的所有记录
    static constexpr const char *SELECT_BY_TIME_RANGE =
        "SELECT * FROM cache WHERE timestamp BETWEEN ? AND ? ORDER BY "
        "timestamp ASC";

    // 查询指定时间范围内的所有记录
    static constexpr const char *SELECT_BY_NUM =
        "SELECT * FROM cache "
        "WHERE id IN ("
        "  SELECT id FROM cache "
        "  WHERE status = ? " // 待发送状态
        "  ORDER BY timestamp ASC "
        "  LIMIT ?"
        ")"
        ;

    // 删除指定标签的所有记录
    static constexpr const char *DELETE_BY_TAG =
        "DELETE FROM cache WHERE tag_name = ?";

    // 删除指定时间范围之前的记录（用于清理旧数据）
    static constexpr const char *DELETE_BY_TIME =
        "DELETE FROM cache WHERE timestamp < ?";

    // 统计指定标签的记录数
    static constexpr const char *COUNT_BY_TAG =
        "SELECT COUNT(*) FROM cache WHERE tag_name = ?";

    // 统计指定时间范围的记录数
    static constexpr const char *COUNT_BY_TIME_RANGE =
        "SELECT COUNT(*) FROM cache WHERE timestamp BETWEEN ? AND ?";

    // 查询指定标签的最新记录（获取最新值）
    static constexpr const char *SELECT_LATEST_BY_TAG =
        "SELECT * FROM cache WHERE tag_name = ? ORDER BY timestamp DESC "
        "LIMIT 1";

    // 查询指定标签的统计信息（最大值、最小值、平均值）
    static constexpr const char *STATS_BY_TAG =
        "SELECT COUNT(*), MIN(value), MAX(value), AVG(value), SUM(value) "
        "FROM cache WHERE tag_name = ?";

        //更新数据状态
    static constexpr const char *UPDATE_STATUS =
        "UPDATE cache "
        "SET status = ?, updated_at = strftime('%s', 'now') "
        "WHERE id IN ("
        "  SELECT id FROM cache "
        "  WHERE status = ? " // 待发送状态
        "  ORDER BY timestamp ASC "
        "  LIMIT ?"
        ")";

    // 清理成功的数据（软删除后物理删除）
    static constexpr const char *DELETE_SUCCESS_STATUS =
        "DELETE FROM cache WHERE status = 2 AND updated_at < strftime('%s', "
        "'now', '-1 hour')";

    // 清理永久失败的数据
    static constexpr const char *DELETE_FAILTURE_STATUS =
        "DELETE FROM cache WHERE status = 3 AND updated_at < strftime('%s', "
        "'now', '-24 hour')";

    // 统计指定状态的记录数
    static constexpr const char *UPDATE_STATUS_COUNT =
        "SELECT COUNT(*) FROM cache WHERE status = ?";
  };

  bool prepareStatement(ConnectionWrapper *conn, const char *sql,
                        sqlite3_stmt **stmt);

  void finalizeStatements();
  void finalizeStatement(sqlite3_stmt *&stmt);
};