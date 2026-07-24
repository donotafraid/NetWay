// ============================================================
// DataConsumer.cpp
// ============================================================

#include "PLC_Collector/DataConsumer.h"
#include <chrono>
#include <thread>
#include <iostream>

DataConsumer::DataConsumer(
    moodycamel::ConcurrentQueue<PLCData>& queue,
    std::atomic<bool>& running,
    std::shared_ptr<MessageSendService> service,
    const Config& config)
    : queue_(queue)
    , running_(running)
    , service_(service)
    , config_(config)
    , start_time_(std::chrono::steady_clock::now())
    , worker_thread_(&DataConsumer::run, this) {
    
    if (!service_) {
        throw std::invalid_argument("Service cannot be null");
    }
    
    logInfo("DataConsumer initialized");
}

DataConsumer::~DataConsumer() {
    shutdown();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    logInfo("DataConsumer destroyed");
}

void DataConsumer::run() {
    logInfo("Consumer thread started");
    
    std::vector<PLCData> batch;
    batch.reserve(config_.batch_size);
    
    while (!stop_requested_ && running_.load()) {
        PLCData data;
        
        // 尝试出队
        if (queue_.try_dequeue(data)) {
            // 1. 质量检查
            if (config_.enable_quality_check && !qualityCheck(data)) {
                dropped_count_++;
                logDebug("Dropped invalid data: quality=" + 
                         std::to_string(data.quality));
                continue;
            }
            
            // 2. 添加到批次
            batch.push_back(std::move(data));
            
            // 3. 批次满了就处理
            if (batch.size() >= config_.batch_size) {
                consumeBatch(batch);
                batch.clear();
            }
        } else {
            // 队列空，处理剩余批次
            if (!batch.empty()) {
                consumeBatch(batch);
                batch.clear();
            }
            
            // 短暂休眠
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.queue_empty_sleep_ms)
            );
        }
    }
    
    // 退出前处理剩余数据
    if (!batch.empty()) {
        consumeBatch(batch);
    }
    
    logInfo("Consumer thread stopped. Processed: " + 
            std::to_string(processed_count_.load()));
}

bool DataConsumer::consume(const PLCData& data) {
    // 委托给业务服务
    auto result = service_->handleNewData(convertToSliceRecord(data));
    
    if (result.is_success()) {
        processed_count_++;
        return true;
    } else {
        error_count_++;
        logWarn(result.unwrap_err().what());
        return false;
    }
}

bool DataConsumer::consumeBatch(const std::vector<PLCData>& batch) {
    if (batch.empty()) {
        return true;
    }
    
    // 转换为 SliceRecord
    std::vector<SliceRecord> records;
    records.reserve(batch.size());
    for (const auto& data : batch) {
        records.push_back(convertToSliceRecord(data));
    }
    
    // 委托给业务服务批量处理
    auto result = service_->handleNewDataBatch(records);
    
    if (result.is_success()) {
        processed_count_ += batch.size();
        batch_count_++;
        return true;
    } else {
        error_count_ += batch.size();
        logWarn(+ result.unwrap_err().what());
        
        // 降级：逐条重试
        size_t success_count = 0;
        for (const auto& record : records) {
            auto single_result = service_->handleNewData(record);
            if (single_result.is_success()) {
                success_count++;
            }
        }
        
        processed_count_ += success_count;
        error_count_ += (batch.size() - success_count);
        
        return success_count == batch.size();
    }
}

bool DataConsumer::qualityCheck(const PLCData& data) const {
    // 检查质量标志
    if (data.quality != 0) {
        return false;
    }
    
    // 检查值范围
    if (data.value < -10000 || data.value > 10000) {
        logDebug("Value out of range: " + std::to_string(data.value));
        return false;
    }
    
    // 检查设备ID
    if (data.deviceId < 0 || data.deviceId > 1000) {
        logDebug("Invalid device ID: " + std::to_string(data.deviceId));
        return false;
    }
    
    return true;
}

void DataConsumer::logInfo(const std::string &msg) const {
    std::cout<<msg<<std::endl;
}
void DataConsumer::logWarn(const std::string &msg) const {
    std::cout<<msg<<std::endl;
}
void DataConsumer::logError(const std::string &msg) const {
    std::cout<<msg<<std::endl;
}
void DataConsumer::logDebug(const std::string &msg) const {
    std::cout<<msg<<std::endl;
}

bool DataConsumer::isHealthy() const {
    // 检查服务是否健康
    if (!service_ || !service_->isHealthy()) {
        return false;
    }
    
    // 检查错误率是否过高
    size_t total = processed_count_.load() + error_count_.load();
    if (total > 100) {
        double error_rate = static_cast<double>(error_count_.load()) / total;
        if (error_rate > 0.5) {  // 错误率超过50%
            return false;
        }
    }
    
    return true;
}

void DataConsumer::shutdown() {
    stop_requested_ = true;
}

void DataConsumer::printStatistics() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - start_time_).count();
    
    std::cout << "\n=== DataConsumer Statistics ===" << std::endl;
    std::cout << "Processed: " << processed_count_.load() << std::endl;
    std::cout << "Errors: " << error_count_.load() << std::endl;
    std::cout << "Dropped: " << dropped_count_.load() << std::endl;
    std::cout << "Batches: " << batch_count_ << std::endl;
    std::cout << "Elapsed: " << elapsed << "s" << std::endl;
    if (elapsed > 0) {
        std::cout << "Throughput: " << (processed_count_.load() / elapsed) 
                  << " msg/s" << std::endl;
    }
    std::cout << "================================" << std::endl;
}

// ============================================================
// 辅助函数：PLCData -> SliceRecord 转换
// ============================================================
SliceRecord DataConsumer::convertToSliceRecord(const PLCData& data) {
    SliceRecord record;
    record.tag_name = "myapp_active_requests";
    record.timestamp = data.timestamp;
    record.value = data.value;
    record.help_text = "current active request";
    record.metric_type = "gauge";
    record.job_name = "my_cpp_app";
    record.instance_name = "gateway_1";
    return record;
}