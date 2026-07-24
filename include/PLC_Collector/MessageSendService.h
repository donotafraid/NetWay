// ============================================================
// MessageSendService.h - 发送业务服务
// ============================================================

#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "Sqlite_DB/SQLiteCacheRepository.h"
#include "PLC_Collector/IPushGateway.h"
#include "PLC_Collector/data_types.h"

/**
 * 消息发送服务（业务层）
 * 
 * 职责：
 * - 管理发送状态机
 * - 优先级调度（积压优先）
 * - 决策：直接发送 vs 存储
 * - 依赖：ISendRepository + IPushGateway
 * 
 * 不再直接操作数据库，通过仓储接口解耦
 */
class MessageSendService {
public:
    // 状态定义
    enum class SendState {
        DIRECT,      // 直接发送
        DRAINING,    // 排空积压
        BACKOFF      // 退避
    };
    
    // 配置
    struct Config {
        size_t batch_size ;        // 积压批次大小
        size_t drain_threshold ;    // 积压阈值
        int retry_interval_ms ;    // 重试间隔
        int max_retries ;            // 最大重试次数
        bool enable_debug_log ;
    };
    
    // 构造：注入依赖
    MessageSendService(
        std::shared_ptr<ICacheRepository> repository,
        std::shared_ptr<IPushGateway> gateway,
        const Config& config = Config());
    
    ~MessageSendService() = default;
    
    // 禁止拷贝
    MessageSendService(const MessageSendService&) = delete;
    MessageSendService& operator=(const MessageSendService&) = delete;
    
    // ============================================================
    // 核心业务接口
    // ============================================================
    
    /**
     * 处理新数据（业务决策）
     * 
     * 决策逻辑：
     * 1. 连接断开 → 存储
     * 2. 有积压 → 存储
     * 3. 无积压且连接正常 → 直接发送
     */
    Result<bool,RichError> handleNewData(const SliceRecord& record);
    
    /**
     * 批量处理新数据
     */
   Result<bool,RichError> handleNewDataBatch(const std::vector<SliceRecord>& records);
    
    /**
     * 处理积压数据（定时调用）
     */
    void processBacklog();
    
    /**
     * 启动/停止
     */
    void start();
    void stop();
    
    // ============================================================
    // 状态查询
    // ============================================================
    SendState getSendState() const { return state_.load(); }
    size_t getBacklogCount()  {
      for (int i = 0; i <= 1; ++i) {
        auto count = repository_->updateBatchStatusCount(i);
        if (count.is_success()) {
          if (count.unwrap_returnLeftValue() != 0) {
            backlog_count_.store(count.unwrap_returnLeftValue(),
                                 std::memory_order_release);
            return count.unwrap_returnLeftValue();
          } else {
            continue;
          }
        }
      }

        return 0;
     }
    bool isHealthy() const;

private:
    // ============================================================
    // 状态管理
    // ============================================================
    void updateState(SendState new_state);
    void onBacklogCleared();
    void onBacklogCreated();
    bool hasBacklog()  { return getBacklogCount(); }
    bool shouldDirectSend() const;
    
    // ============================================================
    // 策略方法
    // ============================================================
    Result<bool,RichError> saveToRepository(const SliceRecord& record);
    Result<bool,RichError> sendDirectly(const SliceRecord& record);
    Result<bool,RichError> sendBacklog(const std::vector<SliceRecord>& records);
    
    // ============================================================
    // 后台处理
    // ============================================================
    void backlogProcessorLoop();
    void startBacklogProcessor();
    void stopBacklogProcessor();
    
    // ============================================================
    // 日志
    // ============================================================
    void logInfo(const std::string& msg) const;
    void logWarn(const std::string& msg) const;
    void logError(const std::string& msg) const;
    void logDebug(const std::string& msg) const;

    // ============================================================
    // 成员变量
    // ============================================================
    std::shared_ptr<ICacheRepository> repository_;  // ✅ 依赖接口
    std::shared_ptr<IPushGateway> gateway_;        // ✅ 依赖接口
    
    Config config_;
    std::atomic<SendState> state_{SendState::DIRECT};
    std::atomic<int> backlog_count_{1};
    
    // 后台线程
    std::unique_ptr<std::thread> processor_thread_;
    std::atomic<bool> stop_processing_{false};
    std::mutex processor_mutex_;
    std::condition_variable processor_cv_;
    
    // 统计
    struct Statistics {
        std::atomic<uint64_t> total_received{0};
        std::atomic<uint64_t> total_sent_direct{0};
        std::atomic<uint64_t> total_sent_backlog{0};
        std::atomic<uint64_t> total_saved{0};
        std::atomic<uint64_t> total_failed{0};
    } stats_;
    mutable std::mutex stats_mutex_;
};