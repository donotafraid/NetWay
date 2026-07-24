
#pragma once

// ============================================================
// CacheService.h - 业务服务层
// ============================================================

#include <memory>
#include <atomic>
#include "Sqlite_DB/Interfaces.h"

/**
 * 缓存服务实现
 * 
 * 职责：
 * - 实现ICacheService接口
 * - 业务逻辑编排
 * - 数据验证
 * - 策略选择（批量阈值等）
 * - 日志记录
 */
class CacheService : public ICacheService {
public:
  // 配置
  struct Config {
    size_t batch_threshold ;  // 批量阈值
    size_t batch_size ;        // 每批大小
    int days_to_keep ;         // 保留天数
    bool enable_validation ; // 启用验证
    bool enable_debug_log ; // 启用调试日志
  };

  explicit CacheService(std::shared_ptr<ICacheRepository> repository,
                        const Config &config = Config());

  ~CacheService() = default;

  // ICacheService 接口实现
  Result<bool,RichError> saveData(const SliceRecord &record) override;
  Result<bool,RichError> saveDataBatch(const std::vector<SliceRecord> &records) override;
  Result<std::vector<SliceRecord>,RichError>
  getDataByTag(const std::string &tag_name) override;
  Result<std::vector<SliceRecord>,RichError>
  getDataByTimeRange(int64_t start_time, int64_t end_time) override;
 
  Result<size_t,RichError> cleanExpiredData(int days_to_keep) override;

  // 状态查询
  bool isHealthy() const { return healthy_.load(); }
  Config getConfig() const { return config_; }
  void setConfig(const Config &config);

private:
  // 验证
  bool validateRecord(const SliceRecord &record) const;
  bool validateBatch(const std::vector<SliceRecord> &records) const;

  // 策略选择
  Result<bool,RichError> saveBatchWithStrategy(const std::vector<SliceRecord> &records);

  // 日志
  void logInfo(const std::string &msg) const;
  void logWarn(const std::string &msg) const;
  void logError(const std::string &msg) const;
  void logDebug(const std::string &msg) const;

  std::shared_ptr<ICacheRepository> repository_;
  Config config_;
  std::atomic<bool> healthy_{true};
  mutable std::mutex mutex_;

  // 统计
  struct ServiceStats {
    std::atomic<uint64_t> total_inserts{0};
    std::atomic<uint64_t> total_queries{0};
    std::atomic<uint64_t> total_deletes{0};
    std::atomic<uint64_t> success_inserts{0};
    std::atomic<uint64_t> failed_inserts{0};
  } stats_;
};