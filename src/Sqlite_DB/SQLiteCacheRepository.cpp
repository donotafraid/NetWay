// ============================================================
// SQLiteCacheRepository.cpp
// ============================================================

#include "Sqlite_DB/SQLiteCacheRepository.h"
#include <iostream>

SQLiteCacheRepository::SQLiteCacheRepository(
    std::shared_ptr<ConnectionPool> pool,
    std::shared_ptr<SQLiteSliceExecutor> executor)
    : pool_(pool), executor_(executor) {
    std::cout << "Enter SQLiteCacheRepository::SQLiteCacheRepository" << std::endl;
    
    if (!pool_) {
        throw std::invalid_argument("ConnectionPool cannot be null");
    }
    if (!executor_) {
        throw std::invalid_argument("Executor cannot be null");
    }
    //  initialize executor 
    auto conn_=acquireConnection();
    executor->prepareStatements(conn_.get());
    releaseConnection((conn_));
    
    std::cout << "Leave SQLiteCacheRepository::SQLiteCacheRepository" << std::endl;
}

Result<bool,RichError> SQLiteCacheRepository::insert(const SliceRecord& record) {
    std::cout << "Enter SQLiteCacheRepository::insert" << std::endl;
    
    auto conn = acquireConnection();
    if (!conn) {
      std::cout << "Leave SQLiteCacheRepository::insert (error: failed to acquire connection)" << std::endl;
      return Result<bool, RichError>(
          RichError{"insert : Failed to acquire database connection"});
    }
    
    bool success = executor_->insertSlice(conn.get(), record);
    releaseConnection(conn);
    
    if (!success) {
      std::cout << "Leave SQLiteCacheRepository::insert (error: failed to insert record)" << std::endl;
      return Result<bool, RichError>(
          RichError{"Failed to insert record:" + record.tag_name});
    }

    std::cout << "Leave SQLiteCacheRepository::insert (success)" << std::endl;
    return Result<bool, RichError>(true);
}

Result<bool,RichError> SQLiteCacheRepository::insertBatch(
    const std::vector<SliceRecord>& records) {
    
    std::cout << "Enter SQLiteCacheRepository::insertBatch (records count: " << records.size() << ")" << std::endl;
    
    if (records.empty()) {
      std::cout << "Leave SQLiteCacheRepository::insertBatch (error: empty records)" << std::endl;
      return Result<bool, RichError>(RichError{"record is empty"});
    }
    
    auto conn = acquireConnection();
    if (!conn) {
      std::cout << "Leave SQLiteCacheRepository::insertBatch (error: failed to acquire connection)" << std::endl;
      return Result<bool, RichError>(RichError{"insert batch : Failed to acquire database connection"});
    }
    
    // 开启事务
    if (!conn->begin_transaction()) {
        releaseConnection(conn);
        std::cout << "Leave SQLiteCacheRepository::insertBatch (error: failed to begin transaction)" << std::endl;
        return Result<bool, RichError>(
            RichError{"Failed to begin transaction"});
    }
    
    bool success = true;
    for (const auto& record : records) {
        if (!executor_->insertSlice(conn.get(), record)) {
            success = false;
            break;
        }
    }
    
    if (success) {
        if (!conn->commit_transaction()) {
            conn->rollback_transaction();
            releaseConnection(conn);
            std::cout << "Leave SQLiteCacheRepository::insertBatch (error: failed to commit transaction)" << std::endl;
            return Result<bool, RichError>(
                RichError{"Failed to commit transaction"});
        }
    } else {
        conn->rollback_transaction();
        releaseConnection(conn);
        std::cout << "Leave SQLiteCacheRepository::insertBatch (error: batch insert failed)" << std::endl;
        return Result<bool, RichError>(
            RichError{"Batch insert failed"});
    }
    
    releaseConnection(conn);
    std::cout << "Leave SQLiteCacheRepository::insertBatch (success)" << std::endl;
    return Result<bool, RichError>(true);
}

Result<std::vector<SliceRecord>,RichError> SQLiteCacheRepository::selectByTag(
    const std::string& tag_name) {
    
    std::cout << "Enter SQLiteCacheRepository::selectByTag (tag: " << tag_name << ")" << std::endl;
    
    auto conn = acquireConnection();
    if (!conn) {
      std::cout << "Leave SQLiteCacheRepository::selectByTag (error: failed to acquire connection)" << std::endl;
      return Result<std::vector<SliceRecord>, RichError>(
          RichError{"select by tag : Failed to acquire database connection"});
    }

    auto records = executor_->selectByTag(conn.get(), tag_name);
    releaseConnection(conn);

    std::cout << "Leave SQLiteCacheRepository::selectByTag (found " << records.size() << " records)" << std::endl;
    return Result<std::vector<SliceRecord>, RichError>(records);
}

Result<std::vector<SliceRecord>,RichError> SQLiteCacheRepository::selectByTimeRange(
    int64_t start_time, int64_t end_time) {
    
    std::cout << "Enter SQLiteCacheRepository::selectByTimeRange (start: " << start_time << ", end: " << end_time << ")" << std::endl;
    
    auto conn = acquireConnection();
    if (!conn) {
        std::cout << "Leave SQLiteCacheRepository::selectByTimeRange (error: failed to acquire connection)" << std::endl;
        return Result<std::vector<SliceRecord>,RichError>(
            RichError{"select by time range : Failed to acquire database connection"}
        );
    }
    
    auto records = executor_->selectByTimeRange(conn.get(), start_time, end_time);
    releaseConnection(conn);
    
    std::cout << "Leave SQLiteCacheRepository::selectByTimeRange (found " << records.size() << " records)" << std::endl;
    return Result<std::vector<SliceRecord>,RichError>((records));
}

Result<std::vector<SliceRecord>, RichError>
SQLiteCacheRepository::selectByTagAndTimeRange(const std::string &tag_name,
                                               int64_t start_time,
                                               int64_t end_time) {

  std::cout << "Enter SQLiteCacheRepository::selectByTagAndTimeRange (tag: " << tag_name 
            << ", start: " << start_time << ", end: " << end_time << ")" << std::endl;

  auto conn = acquireConnection();
  if (!conn) {
    std::cout << "Leave SQLiteCacheRepository::selectByTagAndTimeRange (error: failed to acquire connection)" << std::endl;
    return Result<std::vector<SliceRecord>, RichError>(
        RichError{"select by tag and time range : Failed to acquire database connection"});
  }

  auto records = executor_->selectByTagAndTimeRange(conn.get(), tag_name,
                                                    start_time, end_time);
  releaseConnection(conn);

  std::cout << "Leave SQLiteCacheRepository::selectByTagAndTimeRange (found " << records.size() << " records)" << std::endl;
  return Result<std::vector<SliceRecord>, RichError>((records));
}

Result<std::vector<SliceRecord>, RichError>
SQLiteCacheRepository::selectByNum(int special_status, int limit) {

  std::cout << "Enter SQLiteCacheRepository::selectByNum (status: " << special_status 
            << ", limit: " << limit << ")" << std::endl;

  auto conn = acquireConnection();
  if (!conn) {
    std::cout << "Leave SQLiteCacheRepository::selectByNum (error: failed to acquire connection)" << std::endl;
    return Result<std::vector<SliceRecord>, RichError>(
        RichError{"select by num : Failed to acquire database connection"});
  }

  auto records = executor_->selectByNum(conn.get(), special_status, limit);
  releaseConnection(conn);

  std::cout << "Leave SQLiteCacheRepository::selectByNum (found " << records.size() << " records)" << std::endl;
  return Result<std::vector<SliceRecord>, RichError>((records));
}

Result<size_t,RichError> SQLiteCacheRepository::countByTag(const std::string& tag_name) {
    std::cout << "Enter SQLiteCacheRepository::countByTag (tag: " << tag_name << ")" << std::endl;
    
    auto conn = acquireConnection();
    if (!conn) {
        std::cout << "Leave SQLiteCacheRepository::countByTag (error: failed to acquire connection)" << std::endl;
        return Result<size_t,RichError>(
            RichError{"count by tag : Failed to acquire database connection"}
        );
    }
    
    int count = executor_->countByTag(conn.get(), tag_name);
    releaseConnection(conn);
    
    if (count < 0) {
        std::cout << "Leave SQLiteCacheRepository::countByTag (error: count failed)" << std::endl;
        return Result<size_t,RichError>(
            RichError{"Failed to count records for tag: "}
        );
    }

    std::cout << "Leave SQLiteCacheRepository::countByTag (count: " << count << ")" << std::endl;
    return Result<size_t, RichError>(static_cast<size_t>(count));
}

Result<bool,RichError> SQLiteCacheRepository::deleteByTag(const std::string& tag_name) {
    std::cout << "Enter SQLiteCacheRepository::deleteByTag (tag: " << tag_name << ")" << std::endl;
    
    auto conn = acquireConnection();
    if (!conn) {
        std::cout << "Leave SQLiteCacheRepository::deleteByTag (error: failed to acquire connection)" << std::endl;
        return Result<bool,RichError>(
            RichError{"delete by tag : Failed to acquire database connection"}
        );
    }
    
    bool success = executor_->deleteByTag(conn.get(), tag_name);
    releaseConnection(conn);
    
    if (!success) {
        std::cout << "Leave SQLiteCacheRepository::deleteByTag (error: delete failed)" << std::endl;
        return Result<bool,RichError>(
            RichError{"Failed to delete records for tag: " + tag_name}
        );
    }
    
    std::cout << "Leave SQLiteCacheRepository::deleteByTag (success)" << std::endl;
    return Result<bool,RichError>(success);
}

Result<size_t,RichError> SQLiteCacheRepository::deleteByTime(int64_t before_time) {
    std::cout << "Enter SQLiteCacheRepository::deleteByTime (before_time: " << before_time << ")" << std::endl;
    
    auto conn = acquireConnection();
    if (!conn) {
        std::cout << "Leave SQLiteCacheRepository::deleteByTime (error: failed to acquire connection)" << std::endl;
        return Result<size_t,RichError>(
            RichError{"delete by time : Failed to acquire database connection"}
        );
    }
    
    bool success = executor_->deleteByTime(conn.get(), before_time);
    releaseConnection(conn);
    
    if (!success) {
        std::cout << "Leave SQLiteCacheRepository::deleteByTime (error: delete failed)" << std::endl;
        return Result<size_t,RichError>(
            RichError{"Failed to delete records before time:"+ std::to_string(before_time)}
        );
    }
    
    // 获取删除的行数（需要额外查询或从executor获取）
    std::cout << "Leave SQLiteCacheRepository::deleteByTime (success, deleted count: 0)" << std::endl;
    return Result<size_t,RichError>(0);  // 简化处理
}

Result<size_t, RichError>
SQLiteCacheRepository::deleteByTagAndTime(const std::string &tag_name,
                                          int64_t before_time) {
  std::cout << "Enter SQLiteCacheRepository::deleteByTagAndTime (tag: " << tag_name 
            << ", before_time: " << before_time << ")" << std::endl;
  
  auto conn = acquireConnection();
  if (!conn) {
    std::cout << "Leave SQLiteCacheRepository::deleteByTagAndTime (error: failed to acquire connection)" << std::endl;
    return Result<size_t, RichError>(
        RichError{"delete by tag and time : Failed to acquire database connection"});
  }

  bool success =
      executor_->deleteByTagAndTime(conn.get(), tag_name, before_time);
  releaseConnection(conn);

  if (!success) {
    std::cout << "Leave SQLiteCacheRepository::deleteByTagAndTime (error: delete failed)" << std::endl;
    return Result<size_t, RichError>(RichError{
        "Failed to delete records before time:" + std::to_string(before_time)});
  }

  // 获取删除的行数（需要额外查询或从executor获取）
  std::cout << "Leave SQLiteCacheRepository::deleteByTagAndTime (success, deleted count: 0)" << std::endl;
  return Result<size_t, RichError>(0); // 简化处理
}

Result<int, RichError> SQLiteCacheRepository::updateBatchStatus(int old_status, int new_status,
                                                              int limit) {
  std::cout << "Enter SQLiteCacheRepository::updateBatchStatus (old_status: " << old_status 
            << ", new_status: " << new_status << ", limit: " << limit << ")" << std::endl;
  
  auto conn = acquireConnection();
  if (!conn) {
    std::cout << "Leave SQLiteCacheRepository::updateBatchStatus (error: failed to acquire connection)" << std::endl;
    return Result<int, RichError>(
        RichError{"update batch status : Failed to acquire database connection"});
  }

  auto records = executor_->updateBatchStatus(conn.get(), new_status, old_status, limit);
  releaseConnection(conn);

  std::cout << "Leave SQLiteCacheRepository::updateBatchStatus (updated " << records << " records)" << std::endl;
  return Result<int, RichError>(records);
}

Result<int, RichError> SQLiteCacheRepository::updateBatchStatusCount(int special_status) {
  std::cout << "Enter SQLiteCacheRepository::updateBatchStatusCount (special_status: " << special_status << ")" << std::endl;
  
  auto conn = acquireConnection();
  if (!conn) {
    std::cout << "Leave SQLiteCacheRepository::updateBatchStatusCount (error: failed to acquire connection)" << std::endl;
    return Result<int, RichError>(
        RichError{"update batch status count : Failed to acquire database connection"});
  }

  auto records = executor_->updateBatchStatusCount(conn.get(), special_status);
  releaseConnection(conn);

  std::cout << "Leave SQLiteCacheRepository::updateBatchStatusCount (updated " << records << " records)" << std::endl;
  return Result<int, RichError>(records);
}

// ========== 私有方法 ==========

std::unique_ptr<ConnectionWrapper> SQLiteCacheRepository::acquireConnection() {
    std::cout << "Enter SQLiteCacheRepository::acquireConnection" << std::endl;
    
    if (!pool_) {
        std::cout << "Leave SQLiteCacheRepository::acquireConnection (nullptr - pool is null)" << std::endl;
        return nullptr;
    }
    auto ptr = pool_->get_SubConnection();
    if(ptr)
    {
        std::cout << "acquireConnection is success" << std::endl;
    }
    
    std::cout << "Leave SQLiteCacheRepository::acquireConnection" << std::endl;
    return ptr;
}

void SQLiteCacheRepository::releaseConnection(
    std::unique_ptr<ConnectionWrapper>& conn) {
    std::cout << "Enter SQLiteCacheRepository::releaseConnection" << std::endl;
    
    if (!conn) {
        std::cout << "Leave SQLiteCacheRepository::releaseConnection (conn is null)" << std::endl;
        return;
    }

    pool_->release_connectionWrapper_ptr(std::move(conn));
    
    std::cout << "Leave SQLiteCacheRepository::releaseConnection" << std::endl;
}