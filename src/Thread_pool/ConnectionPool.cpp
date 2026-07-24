#include "Thread_pool/ConnectionPool.h"

//static area
std::unique_ptr<memory_pool> memory_pool::instance = nullptr;
std::once_flag memory_pool::m_once_flag;
//

std::unique_ptr<ConnectionWrapper> ConnectionPool::get_SubConnection()
{
  // 验证并获取最终使用的文件路径
  std::string actual_path = verify_openedPath_memorySize();

  // 1. 尝试从池中查找匹配的连接
  for (auto it = m_connection_pool.begin(); it != m_connection_pool.end();
       ++it) {
    if (*it && (*it)->get_db_file_path() == actual_path) {
      auto ptr = std::move(*it);
      m_connection_pool.erase(it);
      return ptr;
    }
  }

  return (std::make_unique<ConnectionWrapper>(actual_path));
}

std::unique_ptr<ConnectionWrapper> ConnectionPool::get_MainConnection(const std::string &mainFilePath)
{
  // 1. 尝试从池中查找匹配的连接
  for (auto it = m_connection_pool.begin(); it != m_connection_pool.end();
       ++it) {
    if (*it && (*it)->get_db_file_path() == mainFilePath) {
      auto ptr = std::move(*it);
      m_connection_pool.erase(it);
      return ptr;
    }
  }

  return get_SubConnection();
}


void ConnectionPool::release_connectionWrapper_ptr(
    std::unique_ptr<ConnectionWrapper> sqlite3_ptr) {
  std::lock_guard<std::mutex> lock(m_connection_mutex);
  m_connection_pool.push_back(std::move(sqlite3_ptr));
  m_connection_cv.notify_one();
}

std::string ConnectionPool::return_current_date_string()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d"); // 格式化为 "20240520"
    return oss.str();
}

std::string ConnectionPool::generateSuffix(int counter)
{
  return std::string(counter, 'A'); // A, AA, AAA...
}


// 实现数据库文件的自动分片/轮转机制，当单个数据库文件超过大小限制时，自动创建新的文件。
std::string ConnectionPool::verify_openedPath_memorySize() {
  std::string base_name = db_file_pre + return_current_date_string();
  std::string extension = ".db";

  // 检查当前日期的文件，从无后缀开始
  for (int counter = 0;; counter++) {
    std::string suffix = (counter == 0) ? "" : generateSuffix(counter);
    std::string db_file_path = base_name + suffix + extension;

    if (!fs::exists(db_file_path)) {
      // 文件不存在，创建它
      m_connection_pool.push_back(
          std::make_unique<ConnectionWrapper>(db_file_path));
      connectFilePathVec.push_back(db_file_path);
      return db_file_path;
    }
    else
    {
        for(auto &item:connectFilePathVec)
        {
            if(item == db_file_path)
            {
              return db_file_path;
            }
        }
        //  the file exist but not exist in vec , so add it
        m_connection_pool.push_back(
            std::make_unique<ConnectionWrapper>(db_file_path));
        connectFilePathVec.push_back(db_file_path);
    }

    //  ensure the file exist , judge the freeSpace of file 
    if (fs::file_size(db_file_path) <= MAX_DB_FILE_LIMIT) {
      return db_file_path; // 未超限，直接使用
    }
    // 超限则继续查找下一个
  }
}

ConnectionPool::~ConnectionPool()
{
    std::cout<<"~ConnectionPool called!\n";
}

//  ConnectionWrapper-------------------------------------------
ConnectionWrapper::ConnectionWrapper(const std::string& db_file_path) {
    open(db_file_path);
}

ConnectionWrapper::ConnectionWrapper(ConnectionWrapper&& other) noexcept
    : db_ptr_(other.db_ptr_)
    , db_file_path_(std::move(other.db_file_path_))
    , in_transaction_(other.in_transaction_) {
    other.db_ptr_ = nullptr;
    other.in_transaction_ = false;
}

ConnectionWrapper& ConnectionWrapper::operator=(ConnectionWrapper&& other) noexcept {
    if (this != &other) {
        close();  // 先关闭当前连接
        
        db_ptr_ = other.db_ptr_;
        db_file_path_ = std::move(other.db_file_path_);
        in_transaction_ = other.in_transaction_;
        
        other.db_ptr_ = nullptr;
        other.in_transaction_ = false;
    }
    return *this;
}

void ConnectionWrapper::reset(sqlite3_stmt* stmt) {
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  db_file_path_.clear();
  in_transaction_ = false;
}

bool ConnectionWrapper::execute(const std::string& sql) {
    if (!db_ptr_) return false;
    
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_ptr_, sql.c_str(), nullptr, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        std::cerr << "SQL execution failed: " << sql << "\nError: " 
                  << (err_msg ? err_msg : "unknown") << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    
    return true;
}

int ConnectionWrapper::step(sqlite3_stmt* stmt) {
    if (!stmt) return SQLITE_MISUSE;
    return sqlite3_step(stmt);
}

void ConnectionWrapper::reset_stmt(sqlite3_stmt* stmt) {
    if (stmt) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
}

void ConnectionWrapper::finalize_stmt(sqlite3_stmt* stmt) {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

void ConnectionWrapper::clear_bindings(sqlite3_stmt* stmt) {
    if (stmt) {
        sqlite3_clear_bindings(stmt);
    }
}

bool ConnectionWrapper::rollback_transaction() {
    if (!in_transaction_) return false;
    if (!execute("ROLLBACK")) return false;
    in_transaction_ = false;
    return true;
}

std::string ConnectionWrapper::get_last_error() const {
    if (db_ptr_) {
        const char* err = sqlite3_errmsg(db_ptr_);
        return err ? std::string(err) : std::string();
    }
    return "Database not open";
}

int64_t ConnectionWrapper::get_last_insert_rowid() const {
    if (db_ptr_) {
        return sqlite3_last_insert_rowid(db_ptr_);
    }
    return 0;
}

int ConnectionWrapper::get_changes() const {
    if (db_ptr_) {
        return sqlite3_changes(db_ptr_);
    }
    return 0;
}

ConnectionWrapper::~ConnectionWrapper() {
    close();
}

bool ConnectionWrapper::open(const std::string &db_file_path) {
  if (db_ptr_)
    close();

  //  open db_information_file
  if (sqlite3_open_v2(db_file_path.c_str(), &db_ptr_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_NOMUTEX,
                      nullptr) != SQLITE_OK) {
    db_ptr_ = nullptr;
    std::cerr << "Sqlite_information::presetting: " << db_file_path
              << " open failed\n";
    return false;
  }

  db_file_path_ = db_file_path;

  // try create table for this opened file
  return createTableIfNotExists();
}

void ConnectionWrapper::close() {
    if (db_ptr_) {
        sqlite3_close(db_ptr_);
        db_ptr_ = nullptr;
    }
    db_file_path_.clear();
}

bool ConnectionWrapper::prepare(const std::string &sql, sqlite3_stmt **stmt) {
  int result = sqlite3_prepare_v2(db_ptr_, sql.c_str(), -1, stmt, nullptr);
  if (result != SQLITE_OK) {
    std::cout << "result code : " << result << std::endl;
  }

  return result;
}

bool ConnectionWrapper::begin_transaction() {
    if (in_transaction_) return false;
    if (!execute("BEGIN TRANSACTION")) return false;
    in_transaction_ = true;
    return true;
}

bool ConnectionWrapper::commit_transaction() {
    if (!in_transaction_) return false;
    if (!execute("COMMIT")) return false;
    in_transaction_ = false;
    return true;
}

// void ConnectionWrapper::reset()
// {
//     sqlite3_reset(stmt_newRecord_ptr);
//     sqlite3_reset(read_missing_slices_from_db_subordinate_file_stmt_ptr);
//     sqlite3_reset(delete_select_file_stmt_ptr);
//     sqlite3_reset(merge_select_db_subordinate_file_stmt_ptr);
// }

// void ConnectionWrapper::clear_bindings()
// {
//     sqlite3_reset(stmt_newRecord_ptr);
//     sqlite3_reset(read_missing_slices_from_db_subordinate_file_stmt_ptr);
//     sqlite3_reset(delete_select_file_stmt_ptr);
//     sqlite3_reset(merge_select_db_subordinate_file_stmt_ptr);
// }

// sqlite3 *ConnectionWrapper::get_db()
// {
//     return db_ptr;
// }

// int ConnectionWrapper::open_db_file(const std::string& db_file_path)
// {
//     int rc =(sqlite3_open_v2(db_file_path.c_str(),&db_ptr,
//             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX , nullptr) ) ;
//     if(rc!=SQLITE_OK)
//     {
//         std::cerr<<"Open database failed and error is : "<<sqlite3_errmsg((this->get_db()))<<std::endl;
//         sqlite3_close(this->get_db());
//         return false;
//     }

//     this->db_file_path =  std::move(db_file_path) ;
//     return rc;
// }

// int ConnectionWrapper::initialize_connection_wrapper(const std::string & db_file_path)
// {
//     //create slice_table
//     int rc = check_table_exists();
//     if (rc != true)
//     {
//         rc =sqlite3_exec(this->get_db(),"BEGIN TRANSACTION",nullptr,nullptr,nullptr);
//         if (rc != SQLITE_OK)
//         {
//             std::cerr<<"execute BEGIN TRANSACTION failed and error is : "<<sqlite3_errmsg(this->get_db())<<std::endl;
//             return false;
//         }
        
//         for(auto& sql: Create_Table)
//         {
//             char* errMsg = nullptr ;
//             rc = sqlite3_exec(this->get_db(),sql,nullptr,nullptr,&errMsg);
//             if (rc != SQLITE_OK)
//             {
//                 std::cerr<<"initialize_connection_wrapper failed : Create table failed and error is : "<<errMsg<<std::endl;
//                 sqlite3_free(errMsg);
//                 sqlite3_exec(this->get_db(),"ROLLBACK",nullptr,nullptr,nullptr);
//                 return false;
//             }
//         }
//         sqlite3_exec(this->get_db(),"COMMIT",nullptr,nullptr,nullptr);
//     }

//     // //set prepared statement
//     // if (this->prepareStatements() != SQLITE_OK)
//     // {
//     //     std::cerr << "initialize_connection_wrapper failed : Breakpoint_Resumption : ConfigDB failed!" << std::endl;
//     //     return false;
//     // }

//     // set the connection_wrapper->get_db() file to WAL mode
//     sqlite3_exec(this->get_db(), "PRAGMA journal_mode=WAL;", 0, 0, 0);
//     sqlite3_exec(this->get_db(), "PRAGMA cache_size=10000;", 0, 0, 0);
//     sqlite3_exec(this->get_db(), "PRAGMA busy_timeout=10000;", 0, 0, 0);

//     return SQLITE_OK;
// }

// bool ConnectionWrapper::check_table_exists()
// {
//     const char *check_table_sql = "SELECT name FROM sqlite_master "
//                                   "WHERE type='table'";


//     sqlite3_stmt* stmt = nullptr;
//     int table_exist_flag = false;

//     int rc = sqlite3_prepare_v2(this->get_db(),check_table_sql,-1,&stmt,nullptr);
//     if (rc != SQLITE_OK)
//     {
//         std::cerr<<"check_table_exist : prepare check_table_sql failed and error is :"<<
//         sqlite3_errmsg(this->get_db())<<std::endl;
//         return false;
//     }

//     while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
//     {
//         const char* table_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt,0));
//         if (table_name)
//         {
//             std::cout << "Table exist and this table name is :"<<table_name<<std::endl;
//             table_exist_flag = true;
//             break;
//         }
//     }

//     if(rc != SQLITE_DONE && rc != SQLITE_ROW)
//     {
//         std::cerr<<"check_table_exist : step check_table_sql failed and error is :"<<
//         sqlite3_errmsg(this->get_db())<<std::endl;
//         table_exist_flag = false;
//     }

//     sqlite3_finalize(stmt);
//     return table_exist_flag;
// }

// std::string ConnectionWrapper::return_db_file_path()
// {
//     return this->db_file_path; 
// }

// sqlite3_stmt *ConnectionWrapper::return_stmt_ptr(DB_Type type)
// {
//     switch (type)
//     {
//     case DB_Type::CONTENT: 
//         return this->stmt_newRecord_ptr;
//     case DB_Type::GET_FAIL_INDEX: 
//         return this->read_missing_slices_from_db_subordinate_file_stmt_ptr;
//     case DB_Type::MERGE_SELECT_FILE: 
//         return this->merge_select_db_subordinate_file_stmt_ptr;
//     case DB_Type::DELETE_SELECT_FILE: 
//         return this->delete_select_file_stmt_ptr;
//     default:
//         std::cerr<<"return_stmt_ptr: input error type"<<std::endl;
//         return nullptr;
//     }
// }

// ConnectionWrapper::ConnectionWrapper(const std::string& db_file_path)
// {
//     this->db_file_path = db_file_path; 
// }

// ConnectionWrapper::~ConnectionWrapper()
// {
//     if(stmt_newRecord_ptr != nullptr)
//     {
//         sqlite3_finalize(stmt_newRecord_ptr);
//         stmt_newRecord_ptr = nullptr;
//     }
//     if(read_missing_slices_from_db_subordinate_file_stmt_ptr != nullptr)
//     {
//         sqlite3_finalize(read_missing_slices_from_db_subordinate_file_stmt_ptr);
//         read_missing_slices_from_db_subordinate_file_stmt_ptr = nullptr;
//     }
//     if(merge_select_db_subordinate_file_stmt_ptr != nullptr)
//     {
//         sqlite3_finalize(merge_select_db_subordinate_file_stmt_ptr);
//         merge_select_db_subordinate_file_stmt_ptr = nullptr;
//     }
//     if(delete_select_file_stmt_ptr != nullptr)
//     {
//         sqlite3_finalize(delete_select_file_stmt_ptr);
//         delete_select_file_stmt_ptr = nullptr;
//     }

//     if (db_ptr != nullptr)
//     {
//         if(sqlite3_get_autocommit(db_ptr) == 0)
//         {
//             sqlite3_exec(db_ptr,"ROLLBACK;",0,0,0);
//         }

//         int rc = sqlite3_close(db_ptr);
//         if (rc != SQLITE_OK)
//         {
//             std::cerr<<"close db failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;    
//         }
//         db_ptr = nullptr;
//     }
//     std::cout<<"ConnectionWrapper destructor function called!"<<std::endl;
// }

std::shared_ptr<request_message> memory_pool::return_ptr(){
    return  std::make_shared<request_message>();
}

void memory_pool::push(std::shared_ptr<request_message>&& db_info){
    m_queue.push(std::move(db_info));
}

std::shared_ptr<request_message> memory_pool::return_pre_ptr(){
    std::shared_ptr<request_message> pre_ptr = m_queue.front();
    m_queue.pop();
    return pre_ptr;
}

void memory_pool::clear_memory_pool(){
    while(!m_queue.empty())
    {
        m_queue.pop();
    }
}

int memory_pool::size(){
    return m_queue.size();
}

memory_pool::~memory_pool(){
    while(!m_queue.empty())
    {
        m_queue.pop();
    }
    std::cout<<"memory_pool::~memory_pool()"<<std::endl;
}