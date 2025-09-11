#include "Thread_pool/ConnectionPool.h"

//static area
std::unique_ptr<memory_pool> memory_pool::instance = nullptr;
std::once_flag memory_pool::m_once_flag;
//

std::unique_ptr<ConnectionWrapper> ConnectionPool::return_connectionWrapper_ptr()
{
    std::unique_ptr<ConnectionWrapper> ptr = nullptr;
    {
        ptr = this->check_use_ptr_in_connection_pool(verify_db_path_memorySize());
    }
    
    return (ptr);
}

void ConnectionPool::release_connectionWrapper_ptr(std::unique_ptr<ConnectionWrapper> sqlite3_ptr)
{
    sqlite3_ptr->reset();
    {
        std::lock_guard<std::mutex> lock(m_connection_mutex);
        m_connection_pool.push_back(std::move(sqlite3_ptr));
    }
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
std::string ConnectionPool::verify_db_path_memorySize()
{
    std::string db_file_path = db_file_pre + return_current_date_string() + ".db";
    if(fs::exists(db_file_path))
    {
        auto db_file_size = fs::file_size(db_file_path);
        if( db_file_size > MAX_DB_FILE_LIMIT)
        {
            std::string stem = fs::path(db_file_path).stem().string() + "A";
            std::string extention = fs::path(db_file_path).extension().string();
            std::string new_db_file_path = fs::path(db_file_path).parent_path().string() + stem + extention;

            return new_db_file_path;
        }
    }
    return db_file_path;
}

std::unique_ptr<ConnectionWrapper> ConnectionPool::check_use_ptr_in_connection_pool(std::string db_file_path)
{
    std::unique_ptr<ConnectionWrapper> ptr = nullptr;
  
    if(!m_db_file_paths_set.insert(db_file_path).second) 
    {
        //find the existed db_file_path
            for(auto it = m_connection_pool.begin(); it != m_connection_pool.end(); ++it)
            {
                //  check if the connection pool has idle connection
                //  if the db_file_path is useable , return it
                if((*it != nullptr)&& this->verify_db_path_memorySize() == db_file_path)
                {
                    ptr = std::move(*it);
                    m_connection_pool.erase(it);
                }
                break;
            }
    }

    // if do not find suitbale idle connection or db_file_path , create a new connection
    if(!ptr)
    {
        ptr = (std::make_unique<ConnectionWrapper>(this->verify_db_path_memorySize()));
        //initialize the connection
        ptr->open_db_file( this->verify_db_path_memorySize() );
        ptr->initialize_connection_wrapper(this->verify_db_path_memorySize());
    }

    if((ptr->return_stmt_ptr(DB_Type::CONTENT))&&(ptr->return_stmt_ptr(DB_Type::MERGE_SELECT_FILE))&&(ptr->return_stmt_ptr(DB_Type::GET_FAIL_INDEX)) !=nullptr)
    {
        return (ptr);
    }
    else {
        std::cout<<"ConnectionWrapper::initialize_connection_wrapper: initialize_connection_wrapper failed!\n";
        return nullptr;
    }
}

ConnectionPool::~ConnectionPool()
{
    std::cout<<"~ConnectionPool called!\n";
}

int ConnectionWrapper::prepareStatements()
{
    //  create new record in db_suborigate file
    int rc = sqlite3_prepare_v2(db_ptr,"INSERT INTO slice_contents(file_id,slice_index,aes_key,iv,plaintext)"
    "VALUES(?,?,?,?,?)",-1,&stmt_newRecord_ptr,nullptr);
    if(rc != SQLITE_OK || stmt_newRecord_ptr == nullptr)
    {
        std::cerr<<"INSERT content failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;
        sqlite3_finalize(stmt_newRecord_ptr);
        return false;
    }

    // LEFT JOIN的本质：左表所有行都会被保留，而右表（slice_contents）的行如果没有匹配，右表该项对应的所有列都会为NULL。
    // 随后，WHERE子句WHERE sc.slice_index IS NULL会过滤出这些行，即序列值n会被选中作为缺失的索引。
    // 左连接中间结果集:包含所有左表的行，以及匹配的右表的行（如果有）。如果没有匹配，中间表里的该右表项的所有列都为NULL

    // read missing_slices_from_db_subodinate_file
    rc = (sqlite3_prepare_v2(db_ptr,read_missing_slices_from_db_subordinate_file_sql,strlen(read_missing_slices_from_db_subordinate_file_sql),&read_missing_slices_from_db_subordinate_file_stmt_ptr,nullptr));
    if (rc != SQLITE_OK) {
        std::cerr<<"Sqlite_information::presetting: prepare read_missing_slices_from_db_subordinate_file_sql failed\n";
        return rc ;
    }

    //  merge select db_subordinate file
    rc = (sqlite3_prepare_v2(db_ptr,merge_select_db_subordinate_file_sql,strlen(merge_select_db_subordinate_file_sql),&merge_select_db_subordinate_file_stmt_ptr,nullptr));
    if (rc != SQLITE_OK)
    {
        std::cerr<<"Sqlite_information::presetting: prepare merge_select_db_subordinate_file_sql failed\n";
        return rc ;
    }
    return rc ;
}

void ConnectionWrapper::reset()
{
    sqlite3_reset(stmt_newRecord_ptr);
    sqlite3_reset(read_missing_slices_from_db_subordinate_file_stmt_ptr);
}


void ConnectionWrapper::close_db_file()
{
    if(stmt_newRecord_ptr != nullptr )
    {
        sqlite3_finalize(stmt_newRecord_ptr);
        stmt_newRecord_ptr = nullptr;
    }
    if(read_missing_slices_from_db_subordinate_file_stmt_ptr != nullptr)
    {
        sqlite3_finalize(read_missing_slices_from_db_subordinate_file_stmt_ptr);
        read_missing_slices_from_db_subordinate_file_stmt_ptr = nullptr;
    }

    if (db_ptr != nullptr)
    {
        if(sqlite3_get_autocommit(db_ptr) == 0)
        {
            sqlite3_exec(db_ptr,"ROLLBACK;",0,0,0);
        }

        int rc = sqlite3_close(db_ptr);
        if (rc != SQLITE_OK)
        {
            std::cerr<<"close db failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;    
        }
        db_ptr = nullptr;
    }
    std::cout<<"ConnectionWrapper stmt_ptr and db_ptr are all closed!"<<std::endl;
}

int ConnectionWrapper::open_db_file(std::string&& db_file_path)
{
    int rc =(sqlite3_open_v2(db_file_path.c_str(),&(this->db_ptr),
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX , nullptr) ) ;
    if(rc!=SQLITE_OK)
    {
        std::cerr<<"Open database failed and error is : "<<sqlite3_errmsg((this->db_ptr))<<std::endl;
        sqlite3_close(this->db_ptr);
        return false;
    }

    this->db_file_path = db_file_path;
    return rc;
}

void ConnectionWrapper::set_db_file_path(std::string&& db_file_name)
{
    this->db_file_path = db_file_name;
}

int ConnectionWrapper::initialize_connection_wrapper(std::string&& db_file_path)
{
    //create slice_table
    int rc = check_table_exists();
    if (rc != true)
    {
        rc =sqlite3_exec(this->db_ptr,"BEGIN TRANSACTION",nullptr,nullptr,nullptr);
        if (rc != SQLITE_OK)
        {
            std::cerr<<"execute BEGIN TRANSACTION failed and error is : "<<sqlite3_errmsg(this->db_ptr)<<std::endl;
            return false;
        }
        
        for(auto& sql: Create_Table)
        {
            char* errMsg = nullptr ;
            rc = sqlite3_exec(this->db_ptr,sql,nullptr,nullptr,&errMsg);
            if (rc != SQLITE_OK)
            {
                std::cerr<<"initialize_connection_wrapper failed : Create table failed and error is : "<<errMsg<<std::endl;
                sqlite3_free(errMsg);
                sqlite3_exec(this->db_ptr,"ROLLBACK",nullptr,nullptr,nullptr);
                return false;
            }
        }
        sqlite3_exec(this->db_ptr,"COMMIT",nullptr,nullptr,nullptr);
    }

    //set prepared statement
    if (this->prepareStatements() != SQLITE_OK)
    {
        std::cerr << "initialize_connection_wrapper failed : Breakpoint_Resumption : ConfigDB failed!" << std::endl;
        return false;
    }

    // set the connection_wrapper->db_ptr file to WAL mode
    sqlite3_exec(this->db_ptr, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    sqlite3_exec(this->db_ptr, "PRAGMA cache_size=10000;", 0, 0, 0);
    sqlite3_exec(this->db_ptr, "PRAGMA busy_timeout=10000;", 0, 0, 0);

    return SQLITE_OK;
}

bool ConnectionWrapper::check_table_exists()
{
    const char *check_table_sql = "SELECT name FROM sqlite_master "
                                  "WHERE type='table'";


    sqlite3_stmt* stmt = nullptr;
    int table_exist_flag = false;

    int rc = sqlite3_prepare_v2(this->db_ptr,check_table_sql,-1,&stmt,nullptr);
    if (rc != SQLITE_OK)
    {
        std::cerr<<"check_table_exist : prepare check_table_sql failed and error is :"<<
        sqlite3_errmsg(this->db_ptr)<<std::endl;
        return false;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const char* table_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt,0));
        if (table_name)
        {
            std::cout << "Table exist and this table name is :"<<table_name<<std::endl;
            table_exist_flag = true;
            break;
        }
    }

    if(rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
        std::cerr<<"check_table_exist : step check_table_sql failed and error is :"<<
        sqlite3_errmsg(this->db_ptr)<<std::endl;
        table_exist_flag = false;
    }

    sqlite3_finalize(stmt);
    return table_exist_flag;
}

std::string ConnectionWrapper::return_db_file_path()
{
    return this->db_file_path; 
}

sqlite3_stmt *ConnectionWrapper::return_stmt_ptr(DB_Type type)
{
    switch (type)
    {
    case DB_Type::CONTENT: 
        return this->stmt_newRecord_ptr;
    case DB_Type::GET_FAIL_INDEX: 
        return this->read_missing_slices_from_db_subordinate_file_stmt_ptr;
    case DB_Type::MERGE_SELECT_FILE: 
        return this->merge_select_db_subordinate_file_stmt_ptr;
    default:
        std::cerr<<"return_stmt_ptr: input error type"<<std::endl;
        return nullptr;
    }
}

ConnectionWrapper::ConnectionWrapper(std::string&& db_file_name)
{
    this->db_file_path = db_file_name;
}

ConnectionWrapper::~ConnectionWrapper()
{
    if(stmt_newRecord_ptr != nullptr)
    {
        sqlite3_finalize(stmt_newRecord_ptr);
        stmt_newRecord_ptr = nullptr;
    }
    if(read_missing_slices_from_db_subordinate_file_stmt_ptr != nullptr)
    {
        sqlite3_finalize(read_missing_slices_from_db_subordinate_file_stmt_ptr);
        read_missing_slices_from_db_subordinate_file_stmt_ptr = nullptr;
    }
    if(merge_select_db_subordinate_file_stmt_ptr != nullptr)
    {
        sqlite3_finalize(merge_select_db_subordinate_file_stmt_ptr);
        merge_select_db_subordinate_file_stmt_ptr = nullptr;
    }

    if (db_ptr != nullptr)
    {
        if(sqlite3_get_autocommit(db_ptr) == 0)
        {
            sqlite3_exec(db_ptr,"ROLLBACK;",0,0,0);
        }

        int rc = sqlite3_close(db_ptr);
        if (rc != SQLITE_OK)
        {
            std::cerr<<"close db failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;    
        }
        db_ptr = nullptr;
    }
    std::cout<<"ConnectionWrapper destructor function called!"<<std::endl;
}

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