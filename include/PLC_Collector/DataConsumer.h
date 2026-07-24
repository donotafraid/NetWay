// ============================================================
// DataConsumer.h - 数据消费者（重组后）
// ============================================================

#pragma once

#include <thread>
#include <atomic>
#include <memory>
#include <chrono>
#include "PLC_Collector/concurrentqueue.h"
#include "PLC_Collector/data_types.h"
#include "ServiceMetrics/serviceMetrics.h"
#include "PLC_Collector/IDataConsumer.h"
#include "PLC_Collector/MessageSendService.h"

/**
 * 数据消费者（重组后的 ReportThread）
 * 
 * 职责（精简后）：
 * - 从队列消费数据
 * - 数据质量检查
 * - 委托给业务服务处理
 * - 统计基础指标
 * 
 * 不再关心：
 * - 数据如何存储
 * - 数据如何发送
 * - 具体的业务逻辑
 */
class DataConsumer : public IDataConsumer {
public:
    // 配置
    struct Config {
        size_t batch_size ;           // 批量处理大小
        int queue_empty_sleep_ms ;    // 队列空时休眠时间
        bool enable_quality_check ; // 启用质量检查
        bool enable_debug_log ;    // 调试日志
    };
    
    /**
     * 构造函数 - 注入依赖
     * 
     * @param queue 共享队列
     * @param running 运行标志
     * @param service 业务服务（处理数据）
     * @param config 配置
     */
    DataConsumer(
        moodycamel::ConcurrentQueue<PLCData>& queue,
        std::atomic<bool>& running,
        std::shared_ptr<MessageSendService> service,
        const Config& config = Config());
    
    ~DataConsumer();
    
    // 禁止拷贝
    DataConsumer(const DataConsumer&) = delete;
    DataConsumer& operator=(const DataConsumer&) = delete;
    
    // IDataConsumer 接口实现
    bool consume(const PLCData& data) override;
    bool consumeBatch(const std::vector<PLCData>& batch) override;
    size_t getProcessedCount() const override { return processed_count_.load(); }
    size_t getErrorCount() const override { return error_count_.load(); }
    bool isHealthy() const override;
    void shutdown() override;
    
    // 统计信息
    void printStatistics() const;

    // trim function
    SliceRecord convertToSliceRecord(const PLCData &data);

  private:
    /**
     * 消费者主循环（线程入口）
     */
    void run();
    
    /**
     * 质量检查
     */
    bool qualityCheck(const PLCData& data) const;
    
    /**
     * 日志
     */
    void logInfo(const std::string& msg) const;
    void logWarn(const std::string& msg) const;
    void logError(const std::string& msg) const;
    void logDebug(const std::string& msg) const;
    
    // ============================================================
    // 成员变量
    // ============================================================
    
    // 外部依赖（注入）
    moodycamel::ConcurrentQueue<PLCData>& queue_;
    std::atomic<bool>& running_;
    std::shared_ptr<MessageSendService> service_;  // ✅ 依赖业务接口
    
    // 配置
    Config config_;
    
    // 线程
    std::thread worker_thread_;
    std::atomic<bool> stop_requested_{false};
    
    // 统计
    std::atomic<size_t> processed_count_{0};
    std::atomic<size_t> error_count_{0};
    std::atomic<size_t> dropped_count_{0};
    std::chrono::steady_clock::time_point start_time_;
    
    // 性能指标
    mutable std::mutex stats_mutex_;
    size_t batch_count_ = 0;
};