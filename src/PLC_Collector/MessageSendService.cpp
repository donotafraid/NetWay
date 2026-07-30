// ============================================================
// MessageSendService.cpp
// ============================================================

#include "PLC_Collector/MessageSendService.h"
#include <spdlog/spdlog.h>
#include <chrono>

MessageSendService::MessageSendService(
    std::shared_ptr<ICacheRepository> repository,
    std::shared_ptr<IPushGateway> gateway,
    const Config& config)
    : repository_(repository)
    , gateway_(gateway)
    , config_(config) {
    
    spdlog::debug("Enter MessageSendService::MessageSendService");
    
    if (!repository_) {
        spdlog::debug("Leave MessageSendService::MessageSendService (error: repository is null)");
        throw std::invalid_argument("Repository cannot be null");
    }
    if (!gateway_) {
        spdlog::debug("Leave MessageSendService::MessageSendService (error: gateway is null)");
        throw std::invalid_argument("Gateway cannot be null");
    }
    
    // 注册网关连接状态回调
    gateway_->setConnectionCallback(
        [this](IPushGateway::ConnectionState state) {
            spdlog::debug("Enter MessageSendService::connectionCallback (state: {})", 
                          static_cast<int>(state));
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
            spdlog::debug("Leave MessageSendService::connectionCallback");
        }
    );
    
    logInfo("MessageSendService initialized");
    
    spdlog::debug("Leave MessageSendService::MessageSendService");
}

Result<bool,RichError> MessageSendService::handleNewData(const SliceRecord& record) {
    spdlog::debug("Enter MessageSendService::handleNewData (tag: {}, time: {})", 
                  record.tag_name, record.timestamp);
    
    stats_.total_received++;
    
    // ---- 决策1：连接状态检查 ----
    if (gateway_->getConnectionState() != IPushGateway::ConnectionState::CONNECTED) {
        logDebug("Gateway disconnected, saving to repository");
        auto result = saveToRepository(record);
        spdlog::debug("Leave MessageSendService::handleNewData (gateway disconnected, saved to repo)");
        return result;
    }
    
    // ---- 决策2：积压检查 ----
    int result = backlog_count_.load(std::memory_order_acquire);
    if (result) {
        std::string info{"Has backlog ({}), saving to repository",
                         static_cast<std::size_t>(result)};
        logDebug(info);
        auto result = saveToRepository(record);
        spdlog::debug("Leave MessageSendService::handleNewData (has backlog, saved to repo)");
        return result;
    }
    
    // ---- 决策3：直接发送 ----
    if (shouldDirectSend()) {
        auto result = sendDirectly(record);
        if (result.is_success()) {
            stats_.total_sent_direct++;
            spdlog::debug("Leave MessageSendService::handleNewData (direct send success)");
            return result;
        } else {
            // 发送失败，存入仓储
            logWarn("Direct send failed, saving to repository");
            spdlog::debug("failed reason: {}", result.unwrap_err().what());
            auto save_result = saveToRepository(record);
            spdlog::debug("Leave MessageSendService::handleNewData (direct send failed, saved to repo)");
            return save_result;
        }
    }
    
    // ---- 默认：存入仓储 ----
    spdlog::debug("Leave MessageSendService::handleNewData (default: saved to repository)");
    return saveToRepository(record);
}

Result<bool,RichError> MessageSendService::handleNewDataBatch(const std::vector<SliceRecord>& records) {
    spdlog::debug("Enter MessageSendService::handleNewDataBatch (records count: {})", records.size());
    
    for(auto &record: records)
    {
      auto result = this->handleNewData(record);
      if(result.is_fail())
      {
        spdlog::debug("Leave MessageSendService::handleNewDataBatch (error at record: {})", 
                      record.tag_name);
        return Result<bool,RichError>(result);
      }
    }
    
    spdlog::debug("Leave MessageSendService::handleNewDataBatch (success)");
    return Result<bool, RichError>(true);
}

Result<bool,RichError> MessageSendService::saveToRepository(const SliceRecord& record) {
    spdlog::debug("Enter MessageSendService::saveToRepository (tag: {})", record.tag_name);
    
    auto result = repository_->insert(record);
    
    if (result.is_success()) {
        onBacklogCreated();
    } 
    
    spdlog::debug("Leave MessageSendService::saveToRepository (success: {})", result.is_success());
    return result;
}

Result<bool,RichError> MessageSendService::sendDirectly(const SliceRecord& record) {
    spdlog::debug("Enter MessageSendService::sendDirectly (tag: {}, max_retries: {})", 
                  record.tag_name, config_.max_retries);
    
    // 带重试的发送
    for (int retry = 0; retry < config_.max_retries; ++retry) {
        auto result = gateway_->send(record);
        if (result.is_success()) {
            spdlog::debug("Leave MessageSendService::sendDirectly (success on retry {})", retry);
            return result;
        }
        
        if (retry < config_.max_retries - 1) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.retry_interval_ms)
            );
            stats_.total_failed++;  // 重试也算失败计数
        }
    }

    spdlog::debug("Leave MessageSendService::sendDirectly (failed after {} retries)", 
                  config_.max_retries);
    return Result<bool, RichError>(
        RichError{"Failed to send after " +
                  std::to_string(config_.max_retries) + " retries"});
}

void MessageSendService::processBacklog() {
    spdlog::debug("Enter MessageSendService::processBacklog");
    
    if (!getBacklogCount()) {
        spdlog::debug("call onBackLogCleared for !getBacklogCount()");
        onBacklogCleared();
        spdlog::debug("Leave MessageSendService::processBacklog (no backlog count)");
        return;
    }
    
    // 1. 从仓储获取积压数据
    auto result = repository_->selectByNum(0, config_.batch_size);
    if (result.is_fail()) {
      spdlog::error("Failed to fetch backlog: {}", result.unwrap_err().what());
      spdlog::debug("Leave MessageSendService::processBacklog (failed to fetch)");
      return;
    }
    
    auto records = result.unwrap_returnLeftValue();
    if (records.empty()) {
        spdlog::debug("call onBackLogCleared for records.empty()");
        onBacklogCleared();
        spdlog::debug("Leave MessageSendService::processBacklog (records empty)");
        return;
    }

    spdlog::debug("processBacklog: fetched {} records", records.size());

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
        std::string info{"Sent {} backlog records, remaining: {}",
                         records.size(), getBacklogCount()};
        logDebug(info);
      } else {
        logError(delete_result.unwrap_err().what());
      }
    }

    // 4. 检查是否清空
    if (!getBacklogCount()) {
        spdlog::debug("call onBackLogCleared for !getBacklogCount()");
        onBacklogCleared();
    }
    
    spdlog::debug("Leave MessageSendService::processBacklog (all_sent: {})", all_sent);
}

void MessageSendService::start() {
    spdlog::debug("Enter MessageSendService::start");
    stop_processing_ = false;
    logInfo("MessageSendService started");
    spdlog::debug("Leave MessageSendService::start");
}

void MessageSendService::stop() {
    spdlog::debug("Enter MessageSendService::stop");
    stopBacklogProcessor();
    logInfo("MessageSendService stopped");
    spdlog::debug("Leave MessageSendService::stop");
}

// ========== 后台处理 ==========

void MessageSendService::startBacklogProcessor() {
    spdlog::debug("Enter MessageSendService::startBacklogProcessor");
    std::lock_guard<std::mutex> lock(processor_mutex_);
    
    if (processor_thread_ && processor_thread_->joinable()) {
        spdlog::debug("Leave MessageSendService::startBacklogProcessor (processor already running)");
        return;
    }
    
    stop_processing_ = false;
    processor_thread_ = std::make_unique<std::thread>(
        &MessageSendService::backlogProcessorLoop, this
    );
    
    spdlog::debug("Leave MessageSendService::startBacklogProcessor (processor started)");
}

void MessageSendService::stopBacklogProcessor() {
    spdlog::debug("Enter MessageSendService::stopBacklogProcessor");
    
    stop_processing_ = true;
    processor_cv_.notify_one();

    if (processor_thread_ && processor_thread_->joinable()) {
        // 检查是否当前线程就是 processor 线程
        if (std::this_thread::get_id() != processor_thread_->get_id()) {
            processor_thread_->join(); // 只有不同线程才 join
            spdlog::debug("stopBacklogProcessor: joined processor thread");
        } else {
            // 自身调用，detach 并等待退出
            processor_thread_->detach();
            spdlog::debug("stopBacklogProcessor: detached processor thread (self)");
        }
    }

    processor_thread_.reset();
    
    spdlog::debug("Leave MessageSendService::stopBacklogProcessor");
}

void MessageSendService::backlogProcessorLoop() {
    spdlog::debug("Enter MessageSendService::backlogProcessorLoop");
    logInfo("Backlog processor started");
    
    while (!stop_processing_) {
        processBacklog();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    logInfo("Backlog processor stopped");
    spdlog::debug("Leave MessageSendService::backlogProcessorLoop");
}

// ==============日志==================
void MessageSendService::logInfo(const std::string &msg) const
{
    spdlog::info(msg);
}
void MessageSendService::logWarn(const std::string &msg) const
{
    spdlog::warn(msg);
}
void MessageSendService::logError(const std::string &msg) const
{
    spdlog::error(msg);
}
void MessageSendService::logDebug(const std::string &msg) const
{
    spdlog::debug(msg);
}

// ========== 状态管理 ==========

void MessageSendService::updateState(SendState new_state) {
    spdlog::debug("Enter MessageSendService::updateState (new_state: {})", 
                  static_cast<int>(new_state));
    
    SendState old = state_.exchange(new_state);
    if (old != new_state) {
      std::string info{"State: {} -> {}", static_cast<size_t>(old),
                       static_cast<size_t>(new_state)};
      logDebug(info);
    }
    
    spdlog::debug("Leave MessageSendService::updateState (old: {})", static_cast<int>(old));
}

void MessageSendService::onBacklogCleared() {
    spdlog::debug("Enter MessageSendService::onBacklogCleared");
    
    backlog_count_.store(0, std::memory_order_release);
    updateState(SendState::DIRECT);
    stopBacklogProcessor();
    logInfo("Backlog cleared, switched to DIRECT mode");
    
    spdlog::debug("Leave MessageSendService::onBacklogCleared");
}

void MessageSendService::onBacklogCreated() {
    spdlog::debug("Enter MessageSendService::onBacklogCreated");
    
    if (gateway_->getConnectionState() == IPushGateway::ConnectionState::CONNECTED &&
        state_.load() == SendState::DIRECT) {
        updateState(SendState::DRAINING);
        startBacklogProcessor();
    }
    
    spdlog::debug("Leave MessageSendService::onBacklogCreated");
}

bool MessageSendService::shouldDirectSend() const {
    spdlog::debug("Enter MessageSendService::shouldDirectSend");
    
    bool result = gateway_->getConnectionState() ==
             IPushGateway::ConnectionState::CONNECTED &&
         state_.load() == SendState::DIRECT &&
         backlog_count_.load(std::memory_order_acquire) <
             config_.drain_threshold;
    
    spdlog::debug("Leave MessageSendService::shouldDirectSend (result: {})", result);
    return result;
}

bool MessageSendService::isHealthy() const {
    spdlog::debug("Enter MessageSendService::isHealthy");
    
    bool result = gateway_->isHealthy() &&
           state_.load() != SendState::BACKOFF;
    
    spdlog::debug("Leave MessageSendService::isHealthy (result: {})", result);
    return result;
}