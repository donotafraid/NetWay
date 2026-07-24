// ============================================================
// MessageSendService.cpp
// ============================================================

#include "PLC_Collector/MessageSendService.h"
#include <chrono>

MessageSendService::MessageSendService(
    std::shared_ptr<ICacheRepository> repository,
    std::shared_ptr<IPushGateway> gateway,
    const Config& config)
    : repository_(repository)
    , gateway_(gateway)
    , config_(config) {
    
    std::cout << "Enter MessageSendService::MessageSendService" << std::endl;
    
    if (!repository_) {
        std::cout << "Leave MessageSendService::MessageSendService (error: repository is null)" << std::endl;
        throw std::invalid_argument("Repository cannot be null");
    }
    if (!gateway_) {
        std::cout << "Leave MessageSendService::MessageSendService (error: gateway is null)" << std::endl;
        throw std::invalid_argument("Gateway cannot be null");
    }
    
    // 注册网关连接状态回调
    gateway_->setConnectionCallback(
        [this](IPushGateway::ConnectionState state) {
            std::cout << "Enter MessageSendService::connectionCallback (state: " 
                      << static_cast<int>(state) << ")" << std::endl;
            if (state == IPushGateway::ConnectionState::CONNECTED) {
                logInfo("Gateway connected, checking backlog...");
                if (getBacklogCount()) {
                    updateState(SendState::DRAINING);
                    startBacklogProcessor();
                }
            } else {
                logWarn("Gateway disconnected");
                updateState(SendState::BACKOFF);
                stopBacklogProcessor();
            }
            std::cout << "Leave MessageSendService::connectionCallback" << std::endl;
        }
    );
    
    logInfo("MessageSendService initialized");
    
    std::cout << "Leave MessageSendService::MessageSendService" << std::endl;
}

Result<bool,RichError> MessageSendService::handleNewData(const SliceRecord& record) {
    std::cout << "Enter MessageSendService::handleNewData (tag: " << record.tag_name 
              << ", time: " << record.timestamp << ")" << std::endl;
    
    stats_.total_received++;
    
    // ---- 决策1：连接状态检查 ----
    if (gateway_->getConnectionState() != IPushGateway::ConnectionState::CONNECTED) {
        logDebug("Gateway disconnected, saving to repository");
        auto result = saveToRepository(record);
        std::cout << "Leave MessageSendService::handleNewData (gateway disconnected, saved to repo)" << std::endl;
        return result;
    }
    
    // ---- 决策2：积压检查 ----
    if (backlog_count_.load(std::memory_order_acquire)) {
        logDebug("Has backlog (" + std::to_string(backlog_count_.load(std::memory_order_acquire)) + 
                 "), saving to repository");
        auto result = saveToRepository(record);
        std::cout << "Leave MessageSendService::handleNewData (has backlog, saved to repo)" << std::endl;
        return result;
    }
    
    // ---- 决策3：直接发送 ----
    if (shouldDirectSend()) {
        auto result = sendDirectly(record);
        if (result.is_success()) {
            stats_.total_sent_direct++;
            std::cout << "Leave MessageSendService::handleNewData (direct send success)" << std::endl;
            return result;
        } else {
            // 发送失败，存入仓储
            logWarn("Direct send failed, saving to repository");
            std::cout << "failed reason: " << result.unwrap_err().what() << std::endl;
            auto save_result = saveToRepository(record);
            std::cout << "Leave MessageSendService::handleNewData (direct send failed, saved to repo)" << std::endl;
            return save_result;
        }
    }
    
    // ---- 默认：存入仓储 ----
    std::cout << "Leave MessageSendService::handleNewData (default: saved to repository)" << std::endl;
    return saveToRepository(record);
}

Result<bool,RichError> MessageSendService::handleNewDataBatch(const std::vector<SliceRecord>& records) {
    std::cout << "Enter MessageSendService::handleNewDataBatch (records count: " << records.size() << ")" << std::endl;
    
    for(auto &record: records)
    {
      auto result = this->handleNewData(record);
      if(result.is_fail())
      {
        std::cout << "Leave MessageSendService::handleNewDataBatch (error at record: " 
                  << record.tag_name << ")" << std::endl;
        return Result<bool,RichError>(result);
      }
    }
    
    std::cout << "Leave MessageSendService::handleNewDataBatch (success)" << std::endl;
    return Result<bool, RichError>(true);
}

Result<bool,RichError> MessageSendService::saveToRepository(const SliceRecord& record) {
    std::cout << "Enter MessageSendService::saveToRepository (tag: " << record.tag_name << ")" << std::endl;
    
    auto result = repository_->insert(record);
    
    if (result.is_success()) {
        onBacklogCreated();
    } 
    
    std::cout << "Leave MessageSendService::saveToRepository (success: " << result.is_success() << ")" << std::endl;
    return result;
}

Result<bool,RichError> MessageSendService::sendDirectly(const SliceRecord& record) {
    std::cout << "Enter MessageSendService::sendDirectly (tag: " << record.tag_name 
              << ", max_retries: " << config_.max_retries << ")" << std::endl;
    
    // 带重试的发送
    for (int retry = 0; retry < config_.max_retries; ++retry) {
        auto result = gateway_->send(record);
        if (result.is_success()) {
            std::cout << "Leave MessageSendService::sendDirectly (success on retry " << retry << ")" << std::endl;
            return result;
        }
        
        if (retry < config_.max_retries - 1) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.retry_interval_ms)
            );
            stats_.total_failed++;  // 重试也算失败计数
        }
    }

    std::cout << "Leave MessageSendService::sendDirectly (failed after " 
              << config_.max_retries << " retries)" << std::endl;
    return Result<bool, RichError>(
        RichError{"Failed to send after " +
                  std::to_string(config_.max_retries) + " retries"});
}

void MessageSendService::processBacklog() {
    std::cout << "Enter MessageSendService::processBacklog" << std::endl;
    
    if (!getBacklogCount()) {
        std::cout<<"call onBackLogCleared for !getBacklogCount()"<<std::endl;
        onBacklogCleared();
        std::cout << "Leave MessageSendService::processBacklog (no backlog count)" << std::endl;
        return;
    }
    
    // 1. 从仓储获取积压数据
    auto result = repository_->selectByNum(0, config_.batch_size);
    if (result.is_fail()) {
      std::cout << "Failed to fetch backlog:" << result.unwrap_err().what()<<std::endl;
      std::cout << "Leave MessageSendService::processBacklog (failed to fetch)" << std::endl;
      return;
    }
    
    auto records = result.unwrap_returnLeftValue();
    if (records.empty()) {
        std::cout<<"call onBackLogCleared for records.empty()"<<std::endl;
        onBacklogCleared();
        std::cout << "Leave MessageSendService::processBacklog (records empty)" << std::endl;
        return;
    }

    std::cout << "processBacklog: fetched " << records.size() << " records" << std::endl;

    // update status for removed data
    repository_->updateBatchStatus(0, 1, config_.batch_size);

    // 2. 发送积压数据
    bool all_sent = true;
    for (const auto& record : records) {
        auto send_result = gateway_->send(record);
        if (send_result.is_fail()) {
            all_sent = false;
            stats_.total_failed++;
            logWarn(send_result.unwrap_err().what());
            break;
        }
    }

    if (all_sent) {
      // 3. 从仓储删除已发送的数据
      auto delete_result = repository_->updateBatchStatus(1, 2, config_.batch_size);
      if (delete_result.is_success()) {
        stats_.total_sent_backlog += records.size();
        logDebug("Sent " + std::to_string(records.size()) +
                 " backlog records, remaining: " +
                 std::to_string(getBacklogCount()));
      } else {
        logError(delete_result.unwrap_err().what());
      }
    }

    // 4. 检查是否清空
    if (!getBacklogCount()) {
        std::cout<<"call onBackLogCleared for !getBacklogCount()"<<std::endl;
        onBacklogCleared();
    }
    
    std::cout << "Leave MessageSendService::processBacklog (all_sent: " << all_sent << ")" << std::endl;
}

void MessageSendService::start() {
    std::cout << "Enter MessageSendService::start" << std::endl;
    stop_processing_ = false;
    logInfo("MessageSendService started");
    std::cout << "Leave MessageSendService::start" << std::endl;
}

void MessageSendService::stop() {
    std::cout << "Enter MessageSendService::stop" << std::endl;
    stopBacklogProcessor();
    logInfo("MessageSendService stopped");
    std::cout << "Leave MessageSendService::stop" << std::endl;
}

// ========== 后台处理 ==========

void MessageSendService::startBacklogProcessor() {
    std::cout << "Enter MessageSendService::startBacklogProcessor" << std::endl;
    std::lock_guard<std::mutex> lock(processor_mutex_);
    
    if (processor_thread_ && processor_thread_->joinable()) {
        std::cout << "Leave MessageSendService::startBacklogProcessor (processor already running)" << std::endl;
        return;
    }
    
    stop_processing_ = false;
    processor_thread_ = std::make_unique<std::thread>(
        &MessageSendService::backlogProcessorLoop, this
    );
    
    std::cout << "Leave MessageSendService::startBacklogProcessor (processor started)" << std::endl;
}

void MessageSendService::stopBacklogProcessor() {
    std::cout << "Enter MessageSendService::stopBacklogProcessor" << std::endl;
    
    stop_processing_ = true;
    processor_cv_.notify_one();

    if (processor_thread_ && processor_thread_->joinable()) {
        // 检查是否当前线程就是 processor 线程
        if (std::this_thread::get_id() != processor_thread_->get_id()) {
            processor_thread_->join(); // 只有不同线程才 join
            std::cout << "stopBacklogProcessor: joined processor thread" << std::endl;
        } else {
            // 自身调用，detach 并等待退出
            processor_thread_->detach();
            std::cout << "stopBacklogProcessor: detached processor thread (self)" << std::endl;
        }
    }

    processor_thread_.reset();
    
    std::cout << "Leave MessageSendService::stopBacklogProcessor" << std::endl;
}

void MessageSendService::backlogProcessorLoop() {
    std::cout << "Enter MessageSendService::backlogProcessorLoop" << std::endl;
    logInfo("Backlog processor started");
    
    while (!stop_processing_) {
        processBacklog();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    logInfo("Backlog processor stopped");
    std::cout << "Leave MessageSendService::backlogProcessorLoop" << std::endl;
}

// ==============日志==================
void MessageSendService::logInfo(const std::string &msg) const
{
    std::cout << "[INFO] " << msg << std::endl;
}
void MessageSendService::logWarn(const std::string &msg) const
{
    std::cout << "[WARN] " << msg << std::endl;
}
void MessageSendService::logError(const std::string &msg) const
{
    std::cout << "[ERROR] " << msg << std::endl;
}
void MessageSendService::logDebug(const std::string &msg) const
{
    std::cout << "[DEBUG] " << msg << std::endl;
}

// ========== 状态管理 ==========

void MessageSendService::updateState(SendState new_state) {
    std::cout << "Enter MessageSendService::updateState (new_state: " 
              << static_cast<int>(new_state) << ")" << std::endl;
    
    SendState old = state_.exchange(new_state);
    if (old != new_state) {
        logDebug("State: " + std::to_string(static_cast<int>(old)) + 
                 " -> " + std::to_string(static_cast<int>(new_state)));
    }
    
    std::cout << "Leave MessageSendService::updateState (old: " 
              << static_cast<int>(old) << ")" << std::endl;
}

void MessageSendService::onBacklogCleared() {
    std::cout << "Enter MessageSendService::onBacklogCleared" << std::endl;
    
    backlog_count_.store(0, std::memory_order_release);
    updateState(SendState::DIRECT);
    stopBacklogProcessor();
    logInfo("Backlog cleared, switched to DIRECT mode");
    
    std::cout << "Leave MessageSendService::onBacklogCleared" << std::endl;
}

void MessageSendService::onBacklogCreated() {
    std::cout << "Enter MessageSendService::onBacklogCreated" << std::endl;
    
    if (gateway_->getConnectionState() == IPushGateway::ConnectionState::CONNECTED &&
        state_.load() == SendState::DIRECT) {
        updateState(SendState::DRAINING);
        startBacklogProcessor();
    }
    
    std::cout << "Leave MessageSendService::onBacklogCreated" << std::endl;
}

bool MessageSendService::shouldDirectSend() const {
    std::cout << "Enter MessageSendService::shouldDirectSend" << std::endl;
    
    bool result = gateway_->getConnectionState() ==
             IPushGateway::ConnectionState::CONNECTED &&
         state_.load() == SendState::DIRECT &&
         backlog_count_.load(std::memory_order_acquire) <
             config_.drain_threshold;
    
    std::cout << "Leave MessageSendService::shouldDirectSend (result: " << result << ")" << std::endl;
    return result;
}

bool MessageSendService::isHealthy() const {
    std::cout << "Enter MessageSendService::isHealthy" << std::endl;
    
    bool result = gateway_->isHealthy() &&
           state_.load() != SendState::BACKOFF;
    
    std::cout << "Leave MessageSendService::isHealthy (result: " << result << ")" << std::endl;
    return result;
}