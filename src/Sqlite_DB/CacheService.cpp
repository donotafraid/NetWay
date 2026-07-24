#include "Sqlite_DB/CacheService.h"


// ============================================================
// CacheService.cpp
// ============================================================

#include <chrono>

CacheService::CacheService(
    std::shared_ptr<ICacheRepository> repository,
    const Config& config)
    : repository_(repository), config_(config) {
    
    if (!repository_) {
        throw std::invalid_argument("Repository cannot be null");
    }
    
    logInfo("CacheService initialized");
}

Result<bool,RichError> CacheService::saveData(const SliceRecord& record) {
    stats_.total_inserts++;
    
    // 1. 验证
    if (config_.enable_validation && !validateRecord(record)) {
        stats_.failed_inserts++;
        return Result<bool, RichError>(
            RichError{"Invalid record: tag_name is empty"});
    }
    
    // 2. 执行保存
    auto result = repository_->insert(record);
    
    // 3. 更新统计
    if (!result.is_fail()) {
        stats_.success_inserts++;
        logDebug("Record saved: " + record.tag_name + 
                 ", timestamp: " + std::to_string(record.timestamp));
    } else {
        stats_.failed_inserts++;
        logError("Failed to save record: " + record.tag_name + 
                 ", error: " + result.unwrap_err().what());
    }
    
    return result;
}

Result<bool,RichError> CacheService::saveDataBatch(const std::vector<SliceRecord>& records) {
    if (records.empty()) {
        return Result<bool,RichError>(RichError{"records is empty"});
    }
    
    // 1. 验证批量
    if (config_.enable_validation && !validateBatch(records)) {
        return Result<bool,RichError>(
            RichError{"Batch contains invalid records"}
        );
    }
    
    stats_.total_inserts += records.size();
    
    // 2. 策略选择
    return saveBatchWithStrategy(records);
}

Result<bool,RichError> CacheService::saveBatchWithStrategy(
    const std::vector<SliceRecord>& records) {
    
    // 策略1：小批量直接插入
    if (records.size() <= config_.batch_threshold) {
        auto result = repository_->insertBatch(records);
        if (!result.is_fail()) {
            stats_.success_inserts += records.size();
        } else {
            stats_.failed_inserts += records.size();
        }
        return result;
    }
    
    // 策略2：大批量分片处理
    size_t total = records.size();
    size_t success_count = 0;
    
    for (size_t i = 0; i < total; i += config_.batch_size) {
        size_t end = std::min(i + config_.batch_size, total);
        std::vector<SliceRecord> batch(
            records.begin() + i,
            records.begin() + end
        );
        
        auto result = repository_->insertBatch(batch);
        if (!result.is_fail()) {
            success_count += batch.size();
        } else {
            logWarn("Batch insert failed at offset: " + std::to_string(i) +
                    ", error: " + result.unwrap_err().what());
        }
    }
    
    stats_.success_inserts += success_count;
    stats_.failed_inserts += (total - success_count);
    
    if (success_count == total) {
        return Result<bool,RichError>(true);
    } else {
      return Result<bool, RichError>(
          RichError{"Partial batch insert: " + std::to_string(success_count) +
                    "/" + std::to_string(total) + " succeeded"});
    }
}

Result<std::vector<SliceRecord>,RichError> CacheService::getDataByTag(
    const std::string& tag_name) {
    
    stats_.total_queries++;
    
    if (tag_name.empty()) {
        return Result<std::vector<SliceRecord>,RichError>(
            RichError{"Tag name cannot be empty"}
        );
    }
    
    auto result = repository_->selectByTag(tag_name);
    
    if (!result.is_fail()) {
        logDebug("Query by tag: " + tag_name + 
                 ", found: " + std::to_string(result.unwrap_returnLeftValue().size()) + " records");
    }
    
    return result;
}

Result<size_t,RichError> CacheService::cleanExpiredData(int days_to_keep) {
    stats_.total_deletes++;
    
    if (days_to_keep <= 0) {
        days_to_keep = config_.days_to_keep;
    }
    
    // 计算截止时间戳
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * days_to_keep);
    auto cutoff_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        cutoff.time_since_epoch()).count();
    
    logInfo("Cleaning data before timestamp: " + std::to_string(cutoff_timestamp));
    
    auto result = repository_->deleteByTime(cutoff_timestamp);
    
    if (!result.is_fail()) {
        logInfo("Cleaned " + std::to_string(result.unwrap_returnLeftValue()) + " expired records");
    }
    
    return result;
}


// ========== 私有方法 ==========

bool CacheService::validateRecord(const SliceRecord& record) const {
    if (record.tag_name.empty()) {
        logWarn("Validation failed: empty tag_name");
        return false;
    }
    
    if (record.timestamp <= 0) {
        logWarn("Validation failed: invalid timestamp");
        return false;
    }
    
    // 检查数据大小（防止过大）
    if (record.raw_metric.size() > 1024 * 1024) {  // 1MB
        logWarn("Validation failed: raw_metric too large: " + 
                std::to_string(record.raw_metric.size()));
        return false;
    }
    
    return true;
}

bool CacheService::validateBatch(const std::vector<SliceRecord>& records) const {
    for (const auto& record : records) {
        if (!validateRecord(record)) {
            return false;
        }
    }
    return true;
}

// ========== 日志 ==========

void CacheService::logInfo(const std::string& msg) const {
    std::cout << "[INFO][CacheService] " << msg << std::endl;
}

void CacheService::logWarn(const std::string& msg) const {
    std::cout << "[WARN][CacheService] " << msg << std::endl;
}

void CacheService::logError(const std::string& msg) const {
    std::cerr << "[ERROR][CacheService] " << msg << std::endl;
}

void CacheService::logDebug(const std::string& msg) const {
    if (config_.enable_debug_log) {
        std::cout << "[DEBUG][CacheService] " << msg << std::endl;
    }
}