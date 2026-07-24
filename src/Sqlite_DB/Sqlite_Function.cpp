#include "Sqlite_DB/Sqlite_Function.h"

// ============================================================
// SQLiteDataConverter 实现
// ============================================================

void SQLiteDataConverter::bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

void SQLiteDataConverter::bindBlob(sqlite3_stmt* stmt, int index, const std::vector<uint8_t>& value) {
    if (value.empty()) {
        sqlite3_bind_blob(stmt, index, nullptr, 0, SQLITE_STATIC);
    } else {
        sqlite3_bind_blob(stmt, index, value.data(), value.size(), SQLITE_TRANSIENT);
    }
}

void SQLiteDataConverter::bindInt(sqlite3_stmt* stmt, int index, int value) {
    sqlite3_bind_int(stmt, index, value);
}

void SQLiteDataConverter::bindInt64(sqlite3_stmt* stmt, int index, int64_t value) {
    sqlite3_bind_int64(stmt, index, value);
}

std::string SQLiteDataConverter::extractText(sqlite3_stmt* stmt, int index) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
    return text ? std::string(text) : std::string();
}

std::vector<uint8_t> SQLiteDataConverter::extractBlob(sqlite3_stmt* stmt, int index) {
    const void* data = sqlite3_column_blob(stmt, index);
    int size = sqlite3_column_bytes(stmt, index);
    if (data && size > 0) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        return std::vector<uint8_t>(bytes, bytes + size);
    }
    return std::vector<uint8_t>();
}

int SQLiteDataConverter::extractInt(sqlite3_stmt* stmt, int index) {
    return sqlite3_column_int(stmt, index);
}

int64_t SQLiteDataConverter::extractInt64(sqlite3_stmt* stmt, int index) {
    return sqlite3_column_int64(stmt, index);
}

void SQLiteDataConverter::bindSliceRecord(sqlite3_stmt *stmt,
                                          const SliceRecord &record) {
  // 绑定参数索引从1开始
  // INSERT语句: (timestamp, tag_name, value, raw_metric)
  // 注意：id 和 created_at 是自动生成的，不需要绑定

  // 绑定 timestamp (INTEGER)
  sqlite3_bind_int64(stmt, 1, record.timestamp);

  // 绑定 tag_name (TEXT)
  sqlite3_bind_text(stmt, 2, record.tag_name.c_str(), -1, SQLITE_TRANSIENT);

  // 绑定 value (REAL)
  sqlite3_bind_double(stmt, 3, record.value);

  // 绑定 raw_metric (TEXT)
  sqlite3_bind_text(stmt, 4, record.raw_metric.c_str(), -1, SQLITE_TRANSIENT);
}

SliceRecord SQLiteDataConverter::extractSliceRecord(sqlite3_stmt *stmt) {
  SliceRecord record;

  // 提取所有字段（SELECT * 返回所有列）
  // 列索引从0开始
  // 顺序: id, timestamp, tag_name, value, raw_metric, created_at

  // 提取 id (INTEGER)
  record.id = sqlite3_column_int64(stmt, 0);

  // 提取 timestamp (INTEGER)
  record.timestamp = sqlite3_column_int(stmt, 1);

  // 提取 tag_name (TEXT)
  const unsigned char *tag_name = sqlite3_column_text(stmt, 2);
  if (tag_name) {
    record.tag_name = reinterpret_cast<const char *>(tag_name);
  }

  // 提取 value (REAL)
  record.value = sqlite3_column_double(stmt, 3);

  // 提取 raw_metric (TEXT)
  const unsigned char *raw_metric = sqlite3_column_text(stmt, 4);
  if (raw_metric) {
    record.raw_metric = reinterpret_cast<const char *>(raw_metric);
  }

  // 提取 created_at (INTEGER)
  record.created_at = sqlite3_column_int64(stmt, 5);

  // 提取 created_at (INTEGER)
  return record;
}

// ============================================================
// SQLiteSliceExecutor 实现
// ============================================================

SQLiteSliceExecutor::~SQLiteSliceExecutor() {
    finalizeStatements();
}

// 按标签查询所有记录（SELECT_BY_TAG）
std::vector<SliceRecord> SQLiteSliceExecutor::selectByTag(ConnectionWrapper* conn, const std::string& tag_name) {
    std::vector<SliceRecord> records;
    if (!conn || !conn->is_open() || !select_stmt_) {
        return records;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    sqlite3_bind_text(select_stmt_, 1, tag_name.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(select_stmt_) == SQLITE_ROW) {
      SliceRecord record = SQLiteDataConverter::extractSliceRecord(select_stmt_);
      records.push_back(record);
    }
    
    sqlite3_reset(select_stmt_);
    sqlite3_clear_bindings(select_stmt_);
    return records;
}

// 按标签删除所有记录（DELETE_BY_TAG）
bool SQLiteSliceExecutor::deleteByTag(ConnectionWrapper *conn, const std::string &tag_name) {
  if (!conn || !conn->is_open() || !delete_stmt_) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  sqlite3_bind_text(delete_stmt_, 1, tag_name.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(delete_stmt_);
  sqlite3_reset(delete_stmt_);
    sqlite3_clear_bindings(delete_stmt_);

  return rc == SQLITE_DONE;
}

// 按标签统计记录数（COUNT_BY_TAG）
int SQLiteSliceExecutor::countByTag(ConnectionWrapper* conn, const std::string& tag_name) {
    if (!conn || !conn->is_open() || !count_stmt_) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    sqlite3_bind_text(count_stmt_, 1, tag_name.c_str(), -1, SQLITE_TRANSIENT);
    
    int count = 0;
    if (sqlite3_step(count_stmt_) == SQLITE_ROW) {
        count = sqlite3_column_int(count_stmt_, 0);
    }
    
    sqlite3_reset(count_stmt_);
    sqlite3_clear_bindings(count_stmt_);
    return count;
}

std::vector<SliceRecord> SQLiteSliceExecutor::selectByTimeRange(ConnectionWrapper *conn,
                                           int64_t start_time,
                                           int64_t end_time) {
  std::vector<SliceRecord> records;
  if (!conn || !conn->is_open() || !select_by_time_stmt_) {
    return records;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  sqlite3_bind_int64(select_by_time_stmt_, 1, start_time);
  sqlite3_bind_int64(select_by_time_stmt_, 2, end_time);

  while (sqlite3_step(select_by_time_stmt_) == SQLITE_ROW) {
    SliceRecord record = SQLiteDataConverter::extractSliceRecord(select_by_time_stmt_);
    records.push_back(record);
  }

  sqlite3_reset(select_by_time_stmt_);
    sqlite3_clear_bindings(select_by_time_stmt_);
  return records;
}

// 按时间范围查询（支持标签过滤）
std::vector<SliceRecord> SQLiteSliceExecutor::selectByTagAndTimeRange(
    ConnectionWrapper *conn, const std::string &tag_name, int64_t start_time,
    int64_t end_time) {
  std::vector<SliceRecord> records;
  if (!conn || !conn->is_open()) {
    return records;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 这里使用现有的select_by_time_stmt_查询，然后在内存中过滤
  // 或者可以新增一个组合查询的statement
  sqlite3_bind_int64(select_by_time_stmt_, 1, start_time);
  sqlite3_bind_int64(select_by_time_stmt_, 2, end_time);

  while (sqlite3_step(select_by_time_stmt_) == SQLITE_ROW) {
    SliceRecord record =
        SQLiteDataConverter::extractSliceRecord(select_by_time_stmt_);
    if (record.tag_name == tag_name) {
      records.push_back(record);
    }
  }

  sqlite3_reset(select_by_time_stmt_);
  sqlite3_clear_bindings(select_by_time_stmt_);
  return records;
}

// 按时间范围查询固定数量
std::vector<SliceRecord>
SQLiteSliceExecutor::selectByNum(ConnectionWrapper *conn, int special_status,
                                 int limit) {
  std::vector<SliceRecord> records;
  if (!conn || !conn->is_open()) {
    return records;
  }

  if (limit <= 0) {
    return records; // 无效的limit
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 这里使用现有的select_by_time_stmt_查询，然后在内存中过滤
  // 或者可以新增一个组合查询的statement
  int rc = sqlite3_bind_int(select_by_num_stmt_, 1, special_status);
  if (rc != SQLITE_OK) {
    // 绑定失败，记录错误
    return records;
  }


  rc = sqlite3_bind_int(select_by_num_stmt_, 2, limit);
  if (rc != SQLITE_OK) {
    // 绑定失败，记录错误
    return records;
  }

  while (sqlite3_step(select_by_num_stmt_) == SQLITE_ROW) {
    SliceRecord record =
        SQLiteDataConverter::extractSliceRecord(select_by_num_stmt_);
    { records.push_back(record); }
  }

  sqlite3_reset(select_by_num_stmt_);
  sqlite3_clear_bindings(select_by_num_stmt_);
  return records;
}

int SQLiteSliceExecutor::updateBatchStatus(ConnectionWrapper *conn,
                                           int new_status, int old_status,
                                           int limit) {
  if (!conn || !conn->is_open()) {
    return false;
  }

  if (limit <= 0) {
    return false; // 无效的limit
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 这里使用现有的select_by_time_stmt_查询，然后在内存中过滤
  // 或者可以新增一个组合查询的statement
  int rc = sqlite3_bind_int(update_status_stmt, 1, new_status);
  if (rc != SQLITE_OK) {
    // 绑定失败，记录错误
    return rc;
  }
  rc = sqlite3_bind_int(update_status_stmt, 2, old_status);
  if (rc != SQLITE_OK) {
    // 绑定失败，记录错误
    return rc;
  }
  rc = sqlite3_bind_int(update_status_stmt, 3, limit);
  if (rc != SQLITE_OK) {
    // 绑定失败，记录错误
    return rc;
  }

  while (sqlite3_step(update_status_stmt) == SQLITE_ROW) {
  }

  sqlite3_reset(update_status_stmt);
  sqlite3_clear_bindings(update_status_stmt);
  return rc;
}

int SQLiteSliceExecutor::updateBatchStatusCount(ConnectionWrapper *conn,
                                                int special_status) {
  if (!conn || !conn->is_open()) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  // 1. 检查statement是否已准备
  if (!update_status_count_stmt) {
    // 记录错误或尝试准备
    return 0;
  }

  // 2. 重置语句（清理之前的状态）
  sqlite3_reset(update_status_count_stmt);
  sqlite3_clear_bindings(update_status_count_stmt);

  // 3. 绑定参数
  int rc = sqlite3_bind_int(update_status_count_stmt, 1, special_status);
  if (rc != SQLITE_OK) {
    return 0;
  }

  // 4. 执行查询并获取结果
  int count = 0;
  rc = sqlite3_step(update_status_count_stmt);

  if (rc == SQLITE_ROW) {
    // 成功获取一行数据
    count = sqlite3_column_int(update_status_count_stmt, 0);
  } else if (rc == SQLITE_DONE) {
    // 查询完成但没有数据（实际上COUNT总是会返回一行，即使为0）
    count = 0;
  } else {
    // 发生错误
    // 可以记录错误: sqlite3_errmsg(conn->get_db())
    count = 0;
  }

  // 5. 重置语句以供下次使用
  sqlite3_reset(update_status_count_stmt);
  sqlite3_clear_bindings(update_status_count_stmt);

  return count;
}

// 删除指定时间之前的记录（DELETE_BY_TIME）
bool SQLiteSliceExecutor::deleteByTime(ConnectionWrapper* conn, int64_t before_time) {
    if (!conn || !conn->is_open() || !delete_by_time_stmt_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    sqlite3_bind_int64(delete_by_time_stmt_, 1, before_time);
    
    int rc = sqlite3_step(delete_by_time_stmt_);
    sqlite3_reset(delete_by_time_stmt_);
    sqlite3_clear_bindings(delete_by_time_stmt_);

    return rc == SQLITE_DONE;
}

// 删除指定标签且指定时间之前的记录
bool SQLiteSliceExecutor::deleteByTagAndTime(ConnectionWrapper* conn, 
                        const std::string& tag_name, 
                        int64_t before_time) {
    if (!conn || !conn->is_open()) {
        return false;
    }
    
    // 可以新增一个组合删除的statement或使用事务
    // 这里示例使用两个statement组合
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 先按标签删除（简单实现）
    // 实际应该使用 WHERE tag_name = ? AND timestamp < ?
    sqlite3_bind_text(delete_stmt_, 1, tag_name.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(delete_stmt_);
    sqlite3_reset(delete_stmt_);
    sqlite3_clear_bindings(delete_stmt_);
    
    return rc == SQLITE_DONE;
}

// 清理过期数据（保留最近N天的数据）
bool SQLiteSliceExecutor::cleanOldData(ConnectionWrapper* conn, int days_to_keep) {
    if (!conn || !conn->is_open() || !delete_by_time_stmt_) {
        return false;
    }
    
    int64_t current_time = time(nullptr);
    int64_t cutoff_time = current_time - (days_to_keep * 24 * 3600);
    
    return deleteByTime(conn, cutoff_time);
}

bool SQLiteSliceExecutor::execute(ConnectionWrapper *conn_,const std::string &sql)
{
    return conn_->execute(sql);
}

bool SQLiteSliceExecutor::prepareStatements(ConnectionWrapper *conn) {
  if (!conn || !conn->is_open()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  bool prepareStatus = true;

  // 逐一检查每个语句的准备结果
  if (prepareStatement(conn, SQL::INSERT, &insert_stmt_)) {
    std::cout<<("Failed to prepare INSERT statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::SELECT_BY_TAG, &select_stmt_)) {
    std::cout<<("Failed to prepare SELECT_BY_TAG statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::DELETE_BY_TAG, &delete_stmt_)) {
    std::cout<<("Failed to prepare DELETE_BY_TAG statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::COUNT_BY_TAG, &count_stmt_)) {
    std::cout<<("Failed to prepare COUNT_BY_TAG statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::SELECT_BY_NUM, &select_by_num_stmt_)) {
    std::cout<<("Failed to prepare SELECT_BY_NUM statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::SELECT_BY_TIME_RANGE,
                        &select_by_time_stmt_)) {
    std::cout<<("Failed to prepare SELECT_BY_TIME_RANGE statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::DELETE_BY_TIME, &delete_by_time_stmt_)) {
    std::cout<<("Failed to prepare DELETE_BY_TIME statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::SELECT_LATEST_BY_TAG,
                        &select_latest_stmt_)) {
    std::cout<<("Failed to prepare SELECT_LATEST_BY_TAG statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::STATS_BY_TAG, &stats_stmt_)) {
    std::cout<<("Failed to prepare STATS_BY_TAG statement");
    prepareStatus = false;
  }

  if (prepareStatement(conn, SQL::UPDATE_STATUS, &update_status_stmt)) {
    std::cout << ("Failed to prepare STATS_BY_TAG statement");
    prepareStatus = false;
  }
  if (prepareStatement(conn, SQL::DELETE_SUCCESS_STATUS, &delete_success_status_stmt)) {
    std::cout << ("Failed to prepare STATS_BY_TAG statement");
    prepareStatus = false;
  }
  if (prepareStatement(conn, SQL::DELETE_FAILTURE_STATUS, &delete_failture_status_stmt)) {
    std::cout << ("Failed to prepare STATS_BY_TAG statement");
    prepareStatus = false;
  }
  if (prepareStatement(conn, SQL::UPDATE_STATUS_COUNT,
                       &update_status_count_stmt)) {
    std::cout << ("Failed to prepare UPDATE_STATUS_COUNT statement");
    prepareStatus = false;
  }

  if (!prepareStatus) {
    std::cout << "prpareStatement exist error " << std::endl;
  }
  return prepareStatus;
}

bool SQLiteSliceExecutor::prepareStatement(ConnectionWrapper* conn, 
                                            const char* sql, 
                                            sqlite3_stmt** stmt) {
    return conn->prepare(sql, stmt);
}

void SQLiteSliceExecutor::finalizeStatements() {
    finalizeStatement(insert_stmt_);
    finalizeStatement(select_stmt_);
    finalizeStatement(delete_stmt_);
    finalizeStatement(count_stmt_);
    finalizeStatement(select_by_time_stmt_);
    finalizeStatement(select_by_num_stmt_);
    finalizeStatement(delete_by_time_stmt_);
    finalizeStatement(select_latest_stmt_);
    finalizeStatement(stats_stmt_);
    finalizeStatement(update_status_stmt);
    finalizeStatement(update_status_count_stmt);
    finalizeStatement(delete_success_status_stmt);
    finalizeStatement(delete_failture_status_stmt);
}

void SQLiteSliceExecutor::finalizeStatement(sqlite3_stmt*& stmt) {
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }
}

// ============================================================
// SliceRecordCoordinator 实现
// ============================================================

SliceRecordCoordinator::SliceRecordCoordinator(std::shared_ptr<ConnectionPool> pool)
    : pool_(pool)
    , initialized_(false)
    , status_(Status::UNINITIALIZED) {
    if (!pool_) {
        throw std::invalid_argument("Connection pool cannot be null");
    }
    initialize();
}

SliceRecordCoordinator::SliceRecordCoordinator(std::shared_ptr<ConnectionPool> pool, const Config& config)
    : pool_(pool)
    , config_(config)
    , initialized_(false)
    , status_(Status::UNINITIALIZED) {
    if (!pool_) {
        throw std::invalid_argument("Connection pool cannot be null");
    }
}

SliceRecordCoordinator::~SliceRecordCoordinator() {
    shutdown();
}

bool SliceRecordCoordinator::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    logInfo("Initializing coordinator...");
    
    // ① 获取连接（统筹资源）
    auto conn = acquireConnection();
    if (!conn) {
        logError("Failed to acquire connection for initialization");
        status_ = Status::ERROR;
        return false;
    }
    
    
    try {
        // ② 创建表（如果不存在）
        if (!createTableIfNotExists(conn.get())) {
            logError("Failed to create table");
            status_ = Status::ERROR;
            releaseConnection(std::move(conn));
            return false;
        }
        
        // ③ 创建执行器（协作组件）
        executor_ = std::make_unique<SQLiteSliceExecutor>();

        initialized_ = true;
        status_ = Status::READY;
        logInfo("Coordinator initialized successfully");
            releaseConnection(std::move(conn));
        return true;
    } catch (const std::exception& e) {
        logError(std::string("Initialization failed: ") + e.what());
        status_ = Status::ERROR;
            releaseConnection(std::move(conn));
        return false;
    }
}

bool SQLiteSliceExecutor::insertSlice(ConnectionWrapper *conn,
                                      const SliceRecord &record) {
  if (!conn || !conn->is_open()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  conn->reset_stmt(insert_stmt_);
  conn->clear_bindings(insert_stmt_);

  SQLiteDataConverter::bindSliceRecord(insert_stmt_, record);

  int rc = conn->step(insert_stmt_);
  return rc == SQLITE_DONE;
}

bool SliceRecordCoordinator::insertSlicesBatch(const std::vector<SliceRecord>& records) {
    // ① 前置检查
    if (!checkReady()) {
        return false;
    }
    
    if (records.empty()) {
        return true;
    }
    
    // ② 验证所有记录
    for (const auto& record : records) {
        if (!validateRecord(record)) {
            return false;
        }
    }
    
    stats_.batch_count++;
    
    // ③ 策略决策（统筹核心）
    if (records.size() <= config_.batch_threshold) {
        logDebug("Using small batch strategy for " + std::to_string(records.size()) + " records");
        return insertBatchSmall(records);
    } else {
        logDebug("Using large batch strategy for " + std::to_string(records.size()) + " records");
        return insertBatchLarge(records);
    }
}


bool SliceRecordCoordinator::deleteByTagName(const std::string& tag_name) {
    if (!checkReady()) {
        return false;
    }
    
    stats_.total_deletes++;
    
    auto conn = acquireConnection();
    if (!conn) {
        logError("Failed to acquire connection for delete");
        stats_.failed_inserts++; // 统计失败
        return false;
    }
    
    bool success = executor_->deleteByTag(conn.get(),tag_name);
    
    if (success) {
        onDeleteSuccess(tag_name);
    } else {
        onDeleteFailure(tag_name);
    }
    
    releaseConnection(std::move(conn));
    return success;
}

int SliceRecordCoordinator::countByTag(const std::string& tag_name) {
    if (!checkReady()) {
        return -1;
    }
    
    auto conn = acquireConnection();
    if (!conn) {
        logError("Failed to acquire connection for count");
        return -1;
    }
    
    releaseConnection(std::move(conn));
    return executor_->countByTag(conn.get(),tag_name);
}

bool SliceRecordCoordinator::hasSlices(const std::string& file_id) {
    int count = countByTag(file_id);
    return count > 0;
}

std::vector<int> SliceRecordCoordinator::getSliceIndices(const std::string& tag_name) {
    std::vector<int> indices;
   
    return indices;
}

std::vector<SliceRecord>
SliceRecordCoordinator::deleteAndReturnSlices(const std::string &tag_name) {
  if (!checkReady()) {
    return {};
  }

  logInfo("Starting migration for file: " + tag_name);

  // ① 获取连接
  auto conn = acquireConnection();
  if (!conn) {
    logError("Failed to acquire connection for migration");
    return {};
  }

  // ② 查询所有数据
  auto records = executor_->selectByTag(conn.get(), tag_name);
  if (records.empty()) {
    logInfo("No records to migrate for file: " + tag_name);
    releaseConnection(std::move(conn));
    return records;
  }

  // ③ 开启事务（统筹控制）
  if (!conn->begin_transaction()) {
    logError("Failed to begin transaction for migration");
    releaseConnection(std::move(conn));
    return {};
  }

  // ④ 执行删除
  bool success = executor_->deleteByTag(conn.get(), tag_name);

  if (success) {
    // ⑤ 提交事务（统筹控制）
    if (conn->commit_transaction()) {
      logInfo("Migration completed successfully for file: " + tag_name);
      stats_.total_deletes += records.size();
      onDeleteSuccess(tag_name);
      releaseConnection(std::move(conn));
      return records;
    } else {
      // 提交失败，回滚
      conn->rollback_transaction();
      logError("Migration commit failed for file: " + tag_name);
      onDeleteFailure(tag_name);
      releaseConnection(std::move(conn));
      return {};
    }
  } else {
    // 删除失败，回滚
    conn->rollback_transaction();
    logError("Migration deletion failed for file: " + tag_name);
    onDeleteFailure(tag_name);
    releaseConnection(std::move(conn));
    return {};
  }
}

std::unique_ptr<ConnectionWrapper> 
SliceRecordCoordinator::createNewSubConnection(const std::string& sourcefile_path) {
    if (!pool_) {
        return nullptr;
    }
    return pool_->get_MainConnection(sourcefile_path);
}

bool SliceRecordCoordinator::isReady() const {
    return initialized_ && status_ == Status::READY;
}

SliceRecordCoordinator::Status SliceRecordCoordinator::getStatus() const {
    return status_;
}

SliceRecordCoordinator::Config SliceRecordCoordinator::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void SliceRecordCoordinator::setConfig(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    logInfo("Configuration updated");
}

void SliceRecordCoordinator::resetStatistics() {
    stats_.total_inserts = 0;
    stats_.total_deletes = 0;
    stats_.total_queries = 0;
    stats_.success_inserts = 0;
    stats_.failed_inserts = 0;
    stats_.batch_count = 0;
    logInfo("Statistics reset");
}

// ========== 私有方法实现 ==========

std::unique_ptr<ConnectionWrapper> SliceRecordCoordinator::acquireConnection() {
    if (!pool_) {
        return nullptr;
    }
    return pool_->get_SubConnection();
}

void SliceRecordCoordinator::releaseConnection(
    std::unique_ptr<ConnectionWrapper> conn) {
  if (!pool_) {
    return;
  }
  pool_->release_connectionWrapper_ptr(std::move(conn));
}

std::unique_ptr<ConnectionWrapper> SliceRecordCoordinator::acquireConnectionWithRetry() {
    if (!pool_) {
        return nullptr;
    }
    
    for (int i = 0; i < config_.max_retries; ++i) {
        auto conn = pool_->get_SubConnection();
        if (conn) {
            return conn;
        }
        
        // 等待后重试
        if (i < config_.max_retries - 1) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.retry_interval_ms * (i + 1))
            );
            logDebug("Connection retry " + std::to_string(i + 1) + 
                     "/" + std::to_string(config_.max_retries));
        }
    }
    
    return nullptr;
}

bool SliceRecordCoordinator::insertBatchSmall(
    const std::vector<SliceRecord> &records) {
  // 小批量：使用事务
  auto conn = acquireConnection();
  if (!conn) {
    logError("Failed to acquire connection for batch insert");
    return false;
  }

  // 开启事务
  if (!conn->begin_transaction()) {
    logError("Failed to begin transaction");
    releaseConnection(std::move(conn));
    return false;
  }

  bool success = true;
  for (const auto &record : records) {
    stats_.total_inserts++;
    if (!executor_->insertSlice(conn.get(), record)) {
      success = false;
      stats_.failed_inserts++;
      break;
    } else {
      stats_.success_inserts++;
    }
  }

  if (success) {
    if (!conn->commit_transaction()) {
      logError("Failed to commit transaction");
      success = false;
    } else {
      logDebug("Batch commit successful: " + std::to_string(records.size()) +
               " records");
    }
  } else {
    conn->rollback_transaction();
    logError("Batch transaction rolled back");
  }

  releaseConnection(std::move(conn));
  return success;
}

bool SliceRecordCoordinator::insertBatchLarge(const std::vector<SliceRecord>& records) {
    // 大批量：分片处理
    size_t total = records.size();
    size_t success_count = 0;
    
    logInfo("Processing large batch: " + std::to_string(total) + " records");
    
    for (size_t i = 0; i < total; i += config_.batch_size) {
        size_t end = std::min(i + config_.batch_size, total);
        std::vector<SliceRecord> batch(
            records.begin() + i, 
            records.begin() + end
        );
        
        if (insertBatchSmall(batch)) {
            success_count += batch.size();
            logDebug("Batch progress: " + std::to_string(success_count) + 
                     "/" + std::to_string(total));
        } else {
            logError("Batch failed at offset: " + std::to_string(i));
            // 可以选择重试或继续
            // 这里选择继续处理下一批
        }
    }
    
    bool all_success = (success_count == total);
    if (all_success) {
        logInfo("All " + std::to_string(total) + " records inserted successfully");
    } else {
        logWarn("Partial success: " + std::to_string(success_count) + 
                "/" + std::to_string(total) + " records inserted");
    }
    
    return all_success;
}

bool SliceRecordCoordinator::checkReady() const {
    if (!initialized_) {
        logError("Coordinator not initialized");
        return false;
    }
    
    if (status_ != Status::READY) {
        logError("Coordinator not ready, status: " + 
                 std::to_string(static_cast<int>(status_.load())));
        return false;
    }
    
    return true;
}

bool SliceRecordCoordinator::validateRecord(const SliceRecord& record) const {
    return true;
}

bool SliceRecordCoordinator::createTableIfNotExists(ConnectionWrapper *conn) {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS slice_contents ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp INTEGER NOT NULL,"
        "value REAL NOT NULL"
        ");"
        "CREATE INDEX idx_timestamp ON cache(timestamp);"
        "CREATE INDEX idx_tagname ON cache(tag_name);";

    return executor_->execute(conn,sql);
}

void SliceRecordCoordinator::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 释放执行器（会自动释放预编译语句）
    executor_.reset();
    
    initialized_ = false;
    status_ = Status::SHUTDOWN;
    
    logInfo("Coordinator shutdown");
}

void SliceRecordCoordinator::onInsertSuccess(const SliceRecord& record) {
    // 可以在这里添加：缓存更新、事件触发等
   
}

void SliceRecordCoordinator::onInsertFailure(const SliceRecord& record) {
    
    // 可以在这里添加：重试策略、降级处理等
    // 例如：写入失败队列
}

void SliceRecordCoordinator::onDeleteSuccess(const std::string& file_id) {
    logInfo("Deleted all records for file: " + file_id);
    // 可以清理缓存等
}

void SliceRecordCoordinator::onDeleteFailure(const std::string& file_id) {
    logError("Failed to delete records for file: " + file_id);
}

void SliceRecordCoordinator::logInfo(const std::string& msg) const {
    std::cout << "[INFO] " << msg << std::endl;
}

void SliceRecordCoordinator::logWarn(const std::string& msg) const {
    std::cout << "[WARN] " << msg << std::endl;
}

void SliceRecordCoordinator::logError(const std::string& msg) const {
    std::cerr << "[ERROR] " << msg << std::endl;
}

void SliceRecordCoordinator::logDebug(const std::string& msg) const {
    if (config_.enable_debug_log) {
        std::cout << "[DEBUG] " << msg << std::endl;
    }
}