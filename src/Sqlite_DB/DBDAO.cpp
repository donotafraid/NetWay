#include "Sqlite_DB/DBDAO.h"

//  ISignalHandler----------------------------------------
bool ISignalHandler::can_handle(const ExternalSignal &signal) const {
  auto types = get_supported_types();
  return std::find(types.begin(), types.end(), signal.type) != types.end();
}

// 获取处理的优先级（用于路由分发）
int ISignalHandler::get_priority() const { return 0; }

// SignalRouter-------------------------------------------- 
SignalRouter::SignalRouter() 
    : running_(true), 
      processing_thread_(&SignalRouter::process_queue, this) {
}

SignalRouter::~SignalRouter() {
    running_ = false;
    cv_.notify_one();
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
}

void SignalRouter::register_handler(SignalType type, ISignalHandler* handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handler) {
        handlers_[type] = handler;
        std::cout << "[SignalRouter] Registered handler: " 
                 << handler->get_handler_name() 
                 << " for type: " << static_cast<int>(type) << std::endl;
    }
}

void SignalRouter::unregister_handler(SignalType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
        std::cout << "[SignalRouter] Unregistered handler: " 
                 << it->second->get_handler_name() << std::endl;
        handlers_.erase(it);
    }
}

void SignalRouter::receive_signal(const ExternalSignal& signal) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    signal_queue_.push(signal);
    cv_.notify_one();
    
    std::cout << "[SignalRouter] Received signal: Type=" 
             << static_cast<int>(signal.type) 
             << ", Command=" << signal.command 
             << ", Source=" << signal.source_id << std::endl;
}

bool SignalRouter::process_signal_sync(ExternalSignal& signal) {
    return dispatch_signal(signal);
}

SignalRouter::Statistics SignalRouter::get_statistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    // 更新队列大小
    {
        std::lock_guard<std::mutex> queue_lock(queue_mutex_);
        stats_.queue_size = signal_queue_.size();
    }
    return stats_;
}

bool SignalRouter::dispatch_signal(ExternalSignal& signal) {
    ISignalHandler* handler = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(signal.type);
        if (it != handlers_.end()) {
            handler = it->second;
        }
    }
    
    if (!handler) {
        std::cerr << "[SignalRouter] No handler registered for type: " 
                 << static_cast<int>(signal.type) << std::endl;
        update_statistics(signal, false);
        return false;
    }
    
    ServiceResult<void> result = handler->handle_signal(signal);
    update_statistics(signal, result.success);

    if (!result.success) {
        std::cerr << "[SignalRouter] Handler " << handler->get_handler_name()
                  << " failed to process signal: " << signal.command
                  << " error message: " << result.error_message << std::endl;
    }

    return result.success;
}

void SignalRouter::update_statistics(const ExternalSignal& signal, bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_processed++;
    if (success) {
        stats_.total_succeeded++;
    } else {
        stats_.total_failed++;
    }
    stats_.processed_by_type[signal.type]++;
}

void SignalRouter::process_queue() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100), 
                    [this] { return !signal_queue_.empty() || !running_; });
        
        if (!running_) break;
        
        while (!signal_queue_.empty()) {
            ExternalSignal signal = signal_queue_.front();
            signal_queue_.pop();
            lock.unlock();
            
            dispatch_signal(signal);
            
            lock.lock();
        }
    }
}

//FileRecordDAO------------------------------
int FileRecordDAO::updateFileRecord(const std::string& file_id,const std::string& file_path,const std::string& missing_slices_index_json)
{
    auto conn = mainConn_pool_->get_MainConnection(file_path);
    if(!conn)
    {
      return false;
    }

    int rc = sqlite3_bind_blob(stmts_.update_stmt,3,file_id.data(),file_id.size(),SQLITE_STATIC);
    rc = sqlite3_bind_text(stmts_.update_stmt,2,file_path.c_str(),file_path.size(),SQLITE_TRANSIENT);
    rc = sqlite3_bind_text(stmts_.update_stmt,1,missing_slices_index_json.data(),missing_slices_index_json.size(),SQLITE_TRANSIENT);  
    
    if(rc != SQLITE_OK)
    {
        std::cerr<< "updateFileRecord failed and error :"<<sqlite3_errmsg(conn->raw_DBhandle())<<std::endl;
        return -1;
    }

    rc = sqlite3_step(stmts_.update_stmt);
    if(rc != SQLITE_DONE)
    {
        std::cerr<< "updateFileRecord failed and error :"<<sqlite3_errmsg(conn->raw_DBhandle())<<std::endl;
        return -1;
    }

    conn->reset(stmts_.update_stmt);
    conn->clear_bindings(stmts_.update_stmt);
    mainConn_pool_->release_connectionWrapper_ptr(std::move(conn));

    return rc;
}

int FileRecordDAO::insertFileRecord(const std::array<uint8_t, 16> &file_id,
                                    const std::string &sourcefile_path,
                                    const std::string &missing_slices_json) {
  int rc = sqlite3_bind_blob(stmts_.insert_stmt, 1, file_id.data(), file_id.size(),
                             SQLITE_STATIC);
  if (rc != SQLITE_OK)
    return -1;

  rc = sqlite3_bind_text(stmts_.insert_stmt, 2, sourcefile_path.c_str(),
                         sourcefile_path.size(), SQLITE_STATIC);
  if (rc != SQLITE_OK)
    return -1;

  rc = sqlite3_bind_text(stmts_.insert_stmt, 3, missing_slices_json.c_str(),
                         missing_slices_json.size(), SQLITE_STATIC);
  if (rc != SQLITE_OK)
    return -1;

  rc = sqlite3_step(stmts_.insert_stmt);
  if (rc != SQLITE_DONE)
    return -1;

  sqlite3_reset(stmts_.insert_stmt);
  return 0; // 成功返回0
}

std::vector<FileRecordDAO::FileRecordData>
FileRecordDAO::queryFileRecordsByPath(const std::string &file_path) {
  std::vector<FileRecordData> results;
  auto conn = mainConn_pool_->get_MainConnection(file_path);
  if(!conn || file_path.empty())
  {
    return results;
  }

  // 绑定参数
  int rc = sqlite3_bind_text(stmts_.select_path_stmt, 1, file_path.data(),
                             file_path.size(), SQLITE_STATIC);
  if (rc != SQLITE_OK) {
    std::cerr << "bind failed" << std::endl;
    return results;
  }

  // 遍历结果集
  while ((rc = sqlite3_step(stmts_.select_path_stmt)) == SQLITE_ROW) {
    FileRecordData data;

    // 提取 file_id (索引0)
    const void *file_id_blob = sqlite3_column_blob(stmts_.select_path_stmt, 0);
    int file_id_len = sqlite3_column_bytes(stmts_.select_path_stmt, 0);
    if (file_id_blob && file_id_len > 0) {
      data.file_id = std::string(reinterpret_cast<const char *>(file_id_blob),
                                 file_id_len);
    }

    // 提取 input_file_path (索引1)
    const unsigned char *path_text =
        sqlite3_column_text(stmts_.select_path_stmt, 1);
    if (path_text) {
      data.input_file_path =
          std::string(reinterpret_cast<const char *>(path_text));
    }

    // 提取 missing_slices_json (索引2)
    const void *json_blob = sqlite3_column_blob(stmts_.select_path_stmt, 2);
    int json_len = sqlite3_column_bytes(stmts_.select_path_stmt, 2);
    if (json_blob && json_len > 0) {
      data.missing_slices_json =
          std::string(reinterpret_cast<const char *>(json_blob), json_len);
    }

    data.is_valid = !data.missing_slices_json.empty();
    results.push_back(std::move(data));
  }

  // 检查循环结束原因
  if (rc != SQLITE_DONE) {
    // 处理错误
    const char *errMsg = sqlite3_errmsg(conn->raw_DBhandle());
    // 记录错误或抛出异常
  }

  conn->reset(stmts_.select_path_stmt);
  conn->clear_bindings(stmts_.select_path_stmt);
  mainConn_pool_->release_connectionWrapper_ptr(std::move(conn));
  return results;
}

std::unique_ptr<ConnectionWrapper> FileRecordDAO::createNewMainConnection(const std::string &actual_path)
{
  auto conn = mainConn_pool_->get_MainConnection(actual_path);
  if (conn)
    return conn;

  //   2. 创建新连接
  std::unique_ptr<ConnectionWrapper> ptr = std::make_unique<ConnectionWrapper>(actual_path);
  if (!ptr->open(actual_path)) {
    return nullptr;
  }

  initialize(std::move(ptr));
  return ptr;
}



//  SliceRecordDAO---------------------------
bool SliceRecordDAO::initialize() {
  auto conn_guard = SubConn_pool_->get_SubConnection();
  ConnectionWrapper *conn = conn_guard.get();
  if (!conn || !conn->is_open()) {
    return false;
  }

  bool success = true;
  success &= conn->prepare(SQL::INSERT, &stmts_.insert_stmt);
  success &= conn->prepare(SQL::SELECT_ALL_BY_FILE_ID, &stmts_.select_stmt);
  success &= conn->prepare(SQL::DELETE_BY_FILE_ID, &stmts_.delete_stmt);
  success &= conn->prepare(SQL::COUNT_BY_FILE_ID, &stmts_.count_stmt);

  return success;
}

// 1. 插入切片记录
bool SliceRecordDAO::insert_slice(const TestMsg&msg) {
  auto conn_guard = SubConn_pool_->get_SubConnection();
  ConnectionWrapper *conn = conn_guard.get();
  if (!conn || !conn->is_open()) {
    return false;
  }

  sqlite3_stmt *stmt = stmts_.insert_stmt;
  if (!stmt)
    return false;

  // 绑定参数
  size_t idx = 1;
  sqlite3_bind_blob(stmt, idx++, msg.file_id().data(), msg.file_id().size(),
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, idx++, msg.slice_index());
  sqlite3_bind_blob(stmt, idx++, msg.aes_key().data(), msg.aes_key().size(),
                    SQLITE_STATIC);
  sqlite3_bind_blob(stmt, idx++, msg.iv().data(), msg.iv().size(),
                    SQLITE_STATIC);
  sqlite3_bind_blob(stmt, idx++, msg.ciphertext().data(), msg.ciphertext().size(),
                    SQLITE_STATIC);

  // 执行
  int rc = conn->step(stmt);
  conn->reset_stmt(stmt);

  return rc == SQLITE_DONE;
}

// 2. 批量插入切片记录（事务优化）
bool SliceRecordDAO::insert_slices_batch(const std::vector<SliceRecord> &records) {
  if (records.empty())
    return true;

  auto conn_guard = SubConn_pool_->get_SubConnection();
  ConnectionWrapper *conn = conn_guard.get();
  if (!conn || !conn->is_open()) {
    return false;
  }

  // 开启事务
  if (!conn->begin_transaction()) {
    return false;
  }

  sqlite3_stmt *stmt = stmts_.insert_stmt;
  bool all_success = true;

  for (const auto &record : records) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    int idx = 1;
    sqlite3_bind_blob(stmt, idx++, record.file_id.data(), record.file_id.size(),
                      SQLITE_STATIC);
    sqlite3_bind_int(stmt, idx++, record.slice_index);
    sqlite3_bind_blob(stmt, idx++, record.aes_key.data(), record.aes_key.size(),
                      SQLITE_STATIC);
    sqlite3_bind_blob(stmt, idx++, record.iv.data(), record.iv.size(),
                      SQLITE_STATIC);
    sqlite3_bind_blob(stmt, idx++, record.plaintext.data(),
                      record.plaintext.size(), SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      all_success = false;
      break;
    }
  }

  if (all_success) {
    conn->commit_transaction();
  } else {
    conn->rollback_transaction();
  }

  conn->reset_stmt(stmt);
  return all_success;
}

// 3. 查询指定文件的所有切片
std::vector<TestMsg> SliceRecordDAO::select_all_by_file_id(const std::string &file_id) {
  std::vector<TestMsg> results;

  auto conn_guard = SubConn_pool_->get_SubConnection();
  ConnectionWrapper *conn = conn_guard.get();
  if (!conn || !conn->is_open()) {
    return results;
  }

  sqlite3_stmt *stmt = stmts_.select_stmt;
  if (!stmt)
    return results;

  // 绑定参数
  sqlite3_bind_blob(stmt, 1, file_id.data(), file_id.size(), SQLITE_STATIC);

  // 执行查询
  TestMsg msg{};
  while (conn->step(stmt) == SQLITE_ROW) {
    msg.Clear();
    msg.set_file_id(file_id.data(),file_id.size());
    msg.set_slice_index(sqlite3_column_int(stmt, 1));

    // 读取blob字段
    const void *aes_key = sqlite3_column_blob(stmt, 2);
    int aes_key_len = sqlite3_column_bytes(stmt, 2);
    msg.set_aes_key(aes_key, aes_key_len);

    const void *iv = sqlite3_column_blob(stmt, 3);
    int iv_len = sqlite3_column_bytes(stmt, 3);
    msg.set_iv(iv, iv_len);

    const void *plaintext = sqlite3_column_blob(stmt, 4);
    int plaintext_len = sqlite3_column_bytes(stmt, 4);
    msg.set_ciphertext(plaintext, plaintext_len);

    results.push_back(std::move(msg));
  }

  conn->reset_stmt(stmt);
  return results;
}

// 4. 删除指定文件的所有切片
bool SliceRecordDAO::delete_by_file_id(const std::string &file_id) {
  auto conn_guard = SubConn_pool_->get_SubConnection();
  ConnectionWrapper *conn = conn_guard.get();
  if (!conn || !conn->is_open()) {
    return false;
  }

  sqlite3_stmt *stmt = stmts_.delete_stmt;
  if (!stmt)
    return false;

  // 绑定参数
  sqlite3_bind_blob(stmt, 1, file_id.data(), file_id.size(), SQLITE_STATIC);

  // 执行删除
  int rc = conn->step(stmt);
  conn->reset_stmt(stmt);

  return rc == SQLITE_DONE;
}

// 5. 统计指定文件的切片数量
int SliceRecordDAO::count_by_file_id(const std::string &file_id) {
  auto conn_guard = SubConn_pool_->get_SubConnection();
  ConnectionWrapper *conn = conn_guard.get();
  if (!conn || !conn->is_open()) {
    return -1;
  }

  sqlite3_stmt *stmt = stmts_.count_stmt;
  if (!stmt)
    return -1;

  // 绑定参数
  sqlite3_bind_blob(stmt, 1, file_id.data(), file_id.size(), SQLITE_STATIC);

  // 执行查询
  int count = -1;
  if (conn->step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }

  conn->reset_stmt(stmt);
  return count;
}

// 6. 检查文件是否有切片
bool SliceRecordDAO::has_slices(const std::string &file_id) {
  int count = count_by_file_id(file_id);
  return count > 0;
}

// 7. 获取切片索引列表
std::vector<int> SliceRecordDAO::get_slice_indices(const std::string &file_id) {
  std::vector<int> indices;

  auto conn_guard = SubConn_pool_->get_SubConnection();
  ConnectionWrapper *conn = conn_guard.get();
  if (!conn || !conn->is_open()) {
    return indices;
  }

  // 使用SELECT_ALL查询，但只取索引
  sqlite3_stmt *stmt = stmts_.select_stmt;
  if (!stmt)
    return indices;

  sqlite3_bind_blob(stmt, 1, file_id.data(), file_id.size(), SQLITE_STATIC);

  while (conn->step(stmt) == SQLITE_ROW) {
    indices.push_back(sqlite3_column_int(stmt, 1));
  }

  conn->reset_stmt(stmt);
  return indices;
}

// 8. 删除并返回被删除的切片（用于迁移）
std::vector<TestMsg> SliceRecordDAO::delete_and_return_slices(const std::string &file_id) {
  std::vector<TestMsg> deleted = select_all_by_file_id(file_id);
  if (!deleted.empty()) {
    delete_by_file_id(file_id);
  }
  return deleted;
}