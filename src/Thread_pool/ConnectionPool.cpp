#include "Thread_pool/ConnectionPool.h"


std::unique_ptr<ConnectionWrapper> ConnectionPool::return_connectionWrapper_ptr(std::string db_file_path)
{
    std::unique_ptr<ConnectionWrapper> ptr = nullptr;
    {
        // std::unique_lock<std::mutex> lock(m_connection_mutex);
        ptr = this->check_use_ptr_in_connection_pool(db_file_path);
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


void ConnectionPool::close_db_file_opened()
{

}

std::unique_ptr<ConnectionWrapper> ConnectionPool::check_use_ptr_in_connection_pool(std::string db_file_path)
{
    std::unique_ptr<ConnectionWrapper> ptr = nullptr;
  
    if(!m_db_file_paths_set.insert(db_file_path).second) 
    {
        //find the existed db_file_path
            for(auto it = m_connection_pool.begin(); it != m_connection_pool.end(); ++it)
            {
                //check if the connection pool has idle connection
                if((*it != nullptr)&&(*it)->return_db_name() == db_file_path)
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
        ptr = (std::make_unique<ConnectionWrapper>(db_file_path));
        //initialize the connection
        ptr->open_db_file(db_file_path);
        ptr->initialize_connection_wrapper(db_file_path);
    }
    return (ptr);
}

ConnectionPool::~ConnectionPool()
{

}

int ConnectionWrapper::prepareStatements()
{
    int rc = sqlite3_prepare_v2(db_ptr,"INSERT INTO slice_contents(file_id,slice_index,aes_key,iv,plaintext)"
    "VALUES(?,?,?,?,?)",-1,&stmt_content_ptr,nullptr);
    if(rc != SQLITE_OK || stmt_content_ptr == nullptr)
    {
        std::cerr<<"INSERT content failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;
        sqlite3_finalize(stmt_content_ptr);
        return false;
    }

    const char* SQL_InspectID = "SELECT file_id FROM slice_records WHERE file_id = ?";
    rc = sqlite3_prepare_v2(db_ptr,SQL_InspectID,-1,&stmt_InspectID_ptr,nullptr);
    if(rc != SQLITE_OK || stmt_InspectID_ptr == nullptr)
    {
        std::cerr<<"SELCET file_id failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;
        sqlite3_finalize(stmt_InspectID_ptr);
        return false;
    }

    // config data post-SQL statment : stmt 
    const char* SQL_NewRecords = "INSERT INTO slice_records (file_id,input_file_path,magic,total_slices,output_folder_path,missing_slices_json) VALUES(?,?,?,?,?,?)";
    rc = sqlite3_prepare_v2(db_ptr,SQL_NewRecords,-1,&stmt_NewRecord_ptr,nullptr);
    if(rc != SQLITE_OK || stmt_NewRecord_ptr == nullptr)
    {
        std::cerr<<"INSERT records failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;
        sqlite3_finalize(stmt_NewRecord_ptr);
        return false;
    }

    const char *SQL_Query_Statement = R"(
    WITH RECURSIVE 
        sequence(n) AS (
            SELECT 0 
            UNION ALL
            SELECT n + 1 FROM sequence WHERE n < ? 
                        )
    SELECT s.n AS missing_index
    FROM sequence s
    LEFT JOIN slice_contents sc 
        ON s.n = sc.slice_index
        AND sc.file_id  = ?
    WHERE sc.slice_index IS NULL
    ORDER BY s.n;
            )";
    rc = sqlite3_prepare_v2(db_ptr,SQL_Query_Statement,-1,&stmt_GetIndex_ptr,nullptr);
    if (rc != SQLITE_OK || stmt_GetIndex_ptr == nullptr)
    {
        std::cerr<<"prepare SQL_Query_Statement failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;
        sqlite3_finalize(stmt_GetIndex_ptr);
        return false;
    }

    const char * SQL_GetFailIndex = R"(
    UPDATE slice_records SET missing_slices_json = ? WHERE file_id = ?)";
    rc =sqlite3_prepare_v2(db_ptr, SQL_GetFailIndex,-1, &stmt_GetFailIndex_ptr, nullptr);
    if(rc != SQLITE_OK || stmt_GetFailIndex_ptr == nullptr)
    {
        std::cerr<<"prepare SQL_GetFailIndex failed and error is : "<<sqlite3_errmsg(db_ptr)<<std::endl;
        sqlite3_finalize(stmt_GetFailIndex_ptr);
        return false;
    }
    return rc ;
}

void ConnectionWrapper::reset()
{
    sqlite3_reset(stmt_content_ptr);
    sqlite3_reset(stmt_InspectID_ptr);
    sqlite3_reset( stmt_NewRecord_ptr);
    sqlite3_reset(stmt_GetIndex_ptr);
    sqlite3_reset(stmt_GetFailIndex_ptr);
}


void ConnectionWrapper::close_db_file()
{
    if(stmt_content_ptr != nullptr ||stmt_InspectID_ptr != nullptr
    || stmt_NewRecord_ptr != nullptr || stmt_GetIndex_ptr != nullptr)
    {
        sqlite3_finalize(stmt_content_ptr);
        sqlite3_finalize(stmt_InspectID_ptr);
        sqlite3_finalize(stmt_NewRecord_ptr);
        sqlite3_finalize(stmt_GetIndex_ptr);
        sqlite3_finalize(stmt_GetFailIndex_ptr);
        stmt_NewRecord_ptr = nullptr;
        stmt_InspectID_ptr = nullptr;
        stmt_content_ptr = nullptr;
        stmt_GetIndex_ptr = nullptr;
        stmt_GetFailIndex_ptr = nullptr;
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

int ConnectionWrapper::open_db_file(std::string& db_file_path)
{
    int rc =(sqlite3_open_v2(db_file_path.c_str(),&(this->db_ptr),
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX , nullptr) ) ;
    if(rc!=SQLITE_OK)
    {
        std::cerr<<"Open database failed and error is : "<<sqlite3_errmsg((this->db_ptr))<<std::endl;
        sqlite3_close(this->db_ptr);
        return false;
    }

    this->db_name = db_file_path;
    return rc;
}

void ConnectionWrapper::set_db_name(std::string db_file_name)
{
    this->db_name = db_file_name;
}

int ConnectionWrapper::initialize_connection_wrapper(std::string db_file_path)
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

std::string ConnectionWrapper::return_db_name()
{
    return this->db_name; 
}

sqlite3_stmt *ConnectionWrapper::return_stmt_ptr(DB_Type type)
{
    switch (type)
    {
    case DB_Type::CONTENT: 
        return this->stmt_content_ptr;
    case DB_Type::INSPECT_ID:
        return this->stmt_InspectID_ptr;
    case DB_Type::NEW_RECORD:
        return this->stmt_NewRecord_ptr;
    case DB_Type::GETINDEX:
        return this->stmt_GetIndex_ptr;
    case DB_Type::GETFAILINDEX:
        return this->stmt_GetFailIndex_ptr;
    default:
        std::cerr<<"return_stmt_ptr: input error type"<<std::endl;
        return nullptr;
    }
}

ConnectionWrapper::ConnectionWrapper(std::string db_file_name)
{
    this->db_name = db_file_name;
}

ConnectionWrapper::~ConnectionWrapper()
{
    if(stmt_content_ptr != nullptr)
    {
        sqlite3_finalize(stmt_content_ptr);
        stmt_content_ptr = nullptr;
    }
    if(stmt_InspectID_ptr != nullptr)
    {
        sqlite3_finalize(stmt_InspectID_ptr);
        stmt_InspectID_ptr = nullptr;
    }
    if(stmt_NewRecord_ptr != nullptr)
    {
        sqlite3_finalize(stmt_NewRecord_ptr);
        stmt_NewRecord_ptr = nullptr;
    }
    if(stmt_GetIndex_ptr != nullptr)
    {
        sqlite3_finalize(stmt_GetIndex_ptr);
        stmt_GetIndex_ptr = nullptr;
    }
    if (stmt_GetFailIndex_ptr != nullptr) {
        sqlite3_finalize(stmt_GetFailIndex_ptr);
        stmt_GetFailIndex_ptr = nullptr;
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
