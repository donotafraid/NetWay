#pragma once

#include <memory>
#include "Sqlite_DB/Interfaces.h"
#include "Thread_pool/ConnectionPool.h"
#include "Sqlite_Function.h"

/**
 * SQLite仓储实现
 * 
 * 职责：
 * - 实现ICacheRepository接口
 * - 管理连接生命周期
 * - 使用SQLiteCacheExecutor执行操作
 */
class SQLiteCacheRepository : public ICacheRepository {
public:
    SQLiteCacheRepository(
        std::shared_ptr<ConnectionPool> pool,
        std::shared_ptr<SQLiteSliceExecutor> executor);
    
    ~SQLiteCacheRepository() = default;
    
    // ICacheRepository 接口实现
    Result<bool,RichError> insert(const SliceRecord& record) override;
    Result<bool,RichError> insertBatch(const std::vector<SliceRecord>& records) override;
    Result<std::vector<SliceRecord>,RichError> selectByTag(const std::string& tag_name) override;
    Result<std::vector<SliceRecord>,RichError> selectByTimeRange(
        int64_t start_time, int64_t end_time) override;
    Result<std::vector<SliceRecord>,RichError> selectByTagAndTimeRange(
        const std::string& tag_name, int64_t start_time, int64_t end_time) override;
    Result<std::vector<SliceRecord>, RichError> selectByNum(int special_status,
                                                            int limit) override;

    Result<size_t,RichError> countByTag(const std::string& tag_name) override;
    Result<bool,RichError> deleteByTag(const std::string& tag_name) override;
    Result<size_t,RichError> deleteByTime(int64_t before_time) override;
    Result<size_t,RichError> deleteByTagAndTime(
        const std::string& tag_name, int64_t before_time) override;
    Result<int, RichError>
    updateBatchStatus(int old_status,int new_status, int limit) override;
    Result<int, RichError> updateBatchStatusCount(int special_status) override;

  private:
    // 连接管理
    std::unique_ptr<ConnectionWrapper> acquireConnection();
    void releaseConnection(std::unique_ptr<ConnectionWrapper> &ptr);
    
    std::shared_ptr<ConnectionPool> pool_;
    std::shared_ptr<SQLiteSliceExecutor> executor_;
};
