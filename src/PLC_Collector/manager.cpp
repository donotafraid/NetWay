#include "PLC_Collector/manager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

SystemManager::SystemManager() : m_running(false), m_config() {
  // 可选：从配置文件加载
  // 1. 初始化数据库
  pool_ = std::make_shared<ConnectionPool>();

  // 2. 创建执行器
  executor_ = std::make_shared<SQLiteSliceExecutor>();

  // 3. 创建仓储
  repository_ = std::make_shared<SQLiteCacheRepository>(pool_, executor_);

  // 3. 创建网关（网络发送）
  auto push_gateway_ = std::make_shared<PushGatewayImpl>();

  // 4. 创建服务（业务逻辑）
  MessageSendService::Config service_config_;
  service_config_.batch_size = 100;
  service_config_.drain_threshold = 10;
  service_config_.max_retries = 3;
  service_config_.retry_interval_ms = 1000;
  service_config_.enable_debug_log = true;

  send_service_ = std::make_shared<MessageSendService>(
      repository_, push_gateway_, service_config_);

  // 4. 创建服务（业务逻辑）
  DataConsumer::Config dataConsumer_config_;
  service_config_.batch_size = 100;
  service_config_.drain_threshold = 10;
  dataConsumer_ = std::make_unique<DataConsumer>(m_queue, m_running,
                                                 send_service_, dataConsumer_config_);
  // 5. 启动服务
  send_service_->start();

  // 5. 设置连接状态回调（由Reporter触发）
  // 这里需要在Reporter中集成连接状态检测

  std::cout << "SystemManager initialized with database support" << std::endl;
}

SystemManager::~SystemManager() {
    if (m_running.load()) {
        stop();
    }
}

/**
 * 启动系统 - 核心协作流程
 * 
 * 分析：需要按照特定顺序启动组件
 * 1. 先设置运行标志
 * 2. 创建消费者（上报线程）
 * 3. 创建生产者（采集线程）
 * 4. 启动监控
 */
void SystemManager::start() {
    if (m_running.load()) {
        std::cout << "[Manager] System already running" << std::endl;
        return;
    }
    
    std::cout << "[Manager] Starting system..." << std::endl;
    
    // Step 1: 设置运行标志（先于所有线程）
    m_running.store(true, std::memory_order_release);

    // Step 3: 创建采集线程池（生产者）
    for (int i = 0; i < m_config.collectorThreadCount; ++i) {
        m_collectors.push_back(
            std::make_unique<CollectorThread>(
                i, m_queue, m_running, m_config
            )
        );
    }

    // Step 4: 启动监控线程（可选）
    // m_monitorThread = std::make_unique<std::thread>(
    //     &SystemManager::monitorLoop, this
    // );
    
    m_metrics.startTime = std::chrono::steady_clock::now();
    
    std::cout << "[Manager] System started with " 
             << m_collectors.size() << " collectors, 1 reporter" << std::endl;
}


/**
 * 停止系统 - 最重要的协作流程
 * 
 * 分析：必须按顺序停止，防止数据丢失
 * 1. 停止生产者（不再产生新数据）
 * 2. 清空队列（消费者处理完剩余数据）
 * 3. 停止消费者
 * 4. 汇总统计
 */
void SystemManager::stop() {
    if (!m_running.load()) {
        return;
    }
    
    std::cout << "\n[Manager] Stopping system..." << std::endl;
    
    // Step 1: 发送停止信号（所有线程会检测到）
    m_running.store(false, std::memory_order_release);
    std::cout << "[Manager] Stop signal sent" << std::endl;
    
    // Step 2: 等待采集线程退出（生产者先停）
    for (auto& collector : m_collectors) {
        collector.reset();  // 自动join
    }
    m_collectors.clear();
    std::cout << "[Manager] All collectors stopped" << std::endl;
    
    // Step 3: 上报线程会自动清空队列后退出
    // 这里需要等待上报线程处理完所有数据
    if(dataConsumer_)
    {
      dataConsumer_.reset();
      std::cout << "[Manager] dataConsumer stopped" << std::endl;
    }
    
    // Step 4: 停止监控线程
    if (m_monitorThread && m_monitorThread->joinable()) {
        m_monitorThread->join();
        m_monitorThread.reset();
    }

    //  Step 5: stop Send_Sercvice
    send_service_->stop();
    
    // Step 5: 打印统计
    printStatistics();
    
    std::cout << "[Manager] System stopped" << std::endl;
}



/**
 * 监控循环 - 健康检查
 * 
 * 分析：需要持续监控系统状态
 * 监控维度：队列深度、吞吐量、线程状态
 */
void SystemManager::monitorLoop() {
    while (m_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        printStatistics();
        
        size_t queueSize = m_queue.size_approx();
        m_metrics.maxQueueSize = std::max(m_metrics.maxQueueSize, queueSize);
        
        // 计算吞吐量
        if (dataConsumer_) {
            m_metrics.totalProcessed = dataConsumer_->getProcessedCount();
            // m_metrics.totalErrors = dataConsumer_>getErrorCount();
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_metrics.startTime
        ).count();
        
        if (elapsed > 0) {
            m_metrics.throughputPerSecond = 
                static_cast<double>(m_metrics.totalProcessed) / elapsed;
        }
        
        // 告警检查
        if (queueSize > static_cast<size_t>(m_config.maxQueueSize * 0.8)) {
            std::cerr << "[Monitor] WARNING: Queue size " << queueSize 
                     << " exceeds 80% limit!" << std::endl;
        }
        
        if (isHealthy()) {
            std::cout << "[Monitor] System healthy: queue=" << queueSize 
                     << ", throughput=" << std::fixed << std::setprecision(1)
                     << m_metrics.throughputPerSecond << " items/s" << std::endl;
        } else {
            std::cerr << "[Monitor] System unhealthy!" << std::endl;
        }
    }
}

void SystemManager::onPushGetWayConnected() {
    std::cout << "[System] PushGetWay connected" << std::endl;
}

void SystemManager::onPushGetWayDisconnected() {
    std::cout << "[System] PushGetWay disconnected" << std::endl;
}

size_t SystemManager::getBacklogCount() const {
    return true;
}

bool SystemManager::isHealthy() const {
    // 检查采集线程是否都活着
    for (const auto& collector : m_collectors) {
        if (!collector->isRunning()) {
            return false;
        }
    }
    
    // 检查队列是否严重积压
    size_t queueSize = m_queue.size_approx();
    if (queueSize > static_cast<size_t>(m_config.maxQueueSize)) {
        return false;
    }
    
    return true;
}



void SystemManager::printStatistics() const {
    std::cout << "\n=== System Statistics ===" << std::endl;
    
    // 队列统计
    size_t queueSize = m_queue.size_approx();
    std::cout << "[Queue] Size: " << queueSize << std::endl;
    std::cout << "[Queue] Max size: " << m_metrics.maxQueueSize << std::endl;
    
    // 处理统计
    if (dataConsumer_) {
        dataConsumer_->printStatistics();
    }
    
    // 采集统计
    int totalCollected = 0;
    for (const auto& collector : m_collectors) {
        totalCollected += collector->getSequenceCount();
    }
    std::cout << "[Collectors] Total collected: " << totalCollected << std::endl;
    
    // 计算总时间
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - m_metrics.startTime
    ).count();
    std::cout << "[System] Uptime: " << elapsed << "s" << std::endl;
    
    std::cout << "==========================\n" << std::endl;
}


void SystemManager::printStatus() const {
    std::cout << "\n[Status] Running: " << (m_running.load() ? "Yes" : "No")
             << ", Queue: " << m_queue.size_approx() << std::endl;
}