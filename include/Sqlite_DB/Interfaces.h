#pragma once

#include <vector>
#include <string>
#include <optional>
#include "Rust_error_deal/error_deal.h"
#include "Sqlite_DB/Sqlite_Function.h"

// ============================================================
// 仓储接口 - 定义数据访问契约
// ============================================================
class ICacheRepository {
public:
    virtual ~ICacheRepository() = default;
    
    // 插入单条记录
    virtual Result<bool,RichError> insert(const SliceRecord& record) = 0;
    
    // 批量插入
    virtual Result<bool,RichError> insertBatch(const std::vector<SliceRecord>& records) = 0;
    
    // 查询：按标签
    virtual Result<std::vector<SliceRecord>,RichError> selectByTag(const std::string& tag_name) = 0;
    
    // 查询：按时间范围
    virtual Result<std::vector<SliceRecord>,RichError> selectByTimeRange(
        int64_t start_time, int64_t end_time) = 0;
    
    // 查询：按标签+时间范围
    virtual Result<std::vector<SliceRecord>,RichError> selectByTagAndTimeRange(
        const std::string& tag_name, int64_t start_time, int64_t end_time) = 0;

    //  查询: 按时间+固定数量
    virtual Result<std::vector<SliceRecord>, RichError>
    selectByNum(int special_status, int limit) = 0;

    // 统计：按标签
    virtual Result<size_t,RichError> countByTag(const std::string& tag_name) = 0;
    
    // 删除：按标签
    virtual Result<bool,RichError> deleteByTag(const std::string& tag_name) = 0;
    
    // 删除：按时间（清理旧数据）
    virtual Result<size_t,RichError> deleteByTime(int64_t before_time) = 0;
    
    // 删除：按标签+时间
    virtual Result<size_t,RichError> deleteByTagAndTime(
        const std::string& tag_name, int64_t before_time) = 0;

    //  update status batch  
    virtual Result<int, RichError>
    updateBatchStatus(int old_status,int new_status, int limit) = 0;

    //  update status batch
    virtual Result<int, RichError>
    updateBatchStatusCount(int special_status) = 0;
};

// ============================================================
// 统计信息结构
// ============================================================
struct TagStatistics {
    size_t count = 0;
    double min_value = 0.0;
    double max_value = 0.0;
    double avg_value = 0.0;
    double sum_value = 0.0;
};


// ============================================================
// 服务接口 - 定义业务逻辑契约
// ============================================================
class ICacheService {
public:
    virtual ~ICacheService() = default;
    
    // 保存数据（含验证）
    virtual Result<bool,RichError> saveData(const SliceRecord& record) = 0;
    
    // 批量保存（含验证）
    virtual Result<bool,RichError> saveDataBatch(const std::vector<SliceRecord>& records) = 0;
    
    // 获取数据（业务逻辑：可能涉及缓存策略）
    virtual Result<std::vector<SliceRecord>,RichError> getDataByTag(
        const std::string& tag_name) = 0;
    
    // 获取指定时间范围的数据
    virtual Result<std::vector<SliceRecord>,RichError> getDataByTimeRange(
        int64_t start_time, int64_t end_time) = 0;
    
    // 清理过期数据
    virtual Result<size_t,RichError> cleanExpiredData(int days_to_keep) = 0;
    
};