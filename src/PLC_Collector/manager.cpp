#include "PLC_Collector/manager.h"

#include <QtCore/qobjectdefs.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

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
  dataConsumer_config_.batch_size = 100;
  dataConsumer_config_.queue_empty_sleep_ms = 1000;
  dataConsumer_ = std::make_unique<DataConsumer>(m_queue, m_running,
                                                 send_service_, dataConsumer_config_);
  // 5. 启动服务
  send_service_->start();

//   loadModbusDevices();
  // 5. 设置连接状态回调（由Reporter触发）
  // 这里需要在Reporter中集成连接状态检测

  spdlog::info("SystemManager initialized with database support");
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
        spdlog::info("[Manager] System already running");
        return;
    }
    
    spdlog::info("[Manager] Starting system...");
    
    // Step 1: 设置运行标志（先于所有线程）
    m_running.store(true, std::memory_order_release);

    // // Step 3: 创建采集线程池（生产者）

    // Step 4: 启动监控线程（可选）
    // m_monitorThread = std::make_unique<std::thread>(
    //     &SystemManager::monitorLoop, this
    // );
    
    m_metrics.startTime = std::chrono::steady_clock::now();
    
    spdlog::info("[Manager] System started with {} collectors, 1 reporter", 
                 m_collectors.size());
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
    
    spdlog::info("\n[Manager] Stopping system...");
    
    // Step 1: 发送停止信号（所有线程会检测到）
    m_running.store(false, std::memory_order_release);
    spdlog::info("[Manager] Stop signal sent");
    
    // Step 2: 等待采集线程退出（生产者先停）
    for (auto& collector : m_collectors) {
        collector.reset();  // 自动join
    }
    m_collectors.clear();
    spdlog::info("[Manager] All collectors stopped");
    
    // Step 3: 上报线程会自动清空队列后退出
    // 这里需要等待上报线程处理完所有数据
    if(dataConsumer_)
    {
      dataConsumer_.reset();
      spdlog::info("[Manager] dataConsumer stopped");
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
    
    spdlog::info("[Manager] System stopped");
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
            spdlog::warn("[Monitor] WARNING: Queue size {} exceeds 80% limit!", queueSize);
        }
        
        if (isHealthy()) {
            spdlog::info("[Monitor] System healthy: queue={}, throughput={:.1f} items/s", 
                         queueSize, m_metrics.throughputPerSecond);
        } else {
            spdlog::error("[Monitor] System unhealthy!");
        }
    }
}

void SystemManager::onPushGetWayConnected() {
    spdlog::info("[System] PushGetWay connected");
}

void SystemManager::onPushGetWayDisconnected() {
    spdlog::info("[System] PushGetWay disconnected");
}

size_t SystemManager::getBacklogCount() const {
    return true;
}

// void SystemManager::loadModbusDevices() {
//   ModbusConfigLoader loader;

//   for (const auto &entry :
//        std::filesystem::directory_iterator("./config/modbus/")) {
//     if (entry.path().extension() != ".json")
//       continue;

//     // 1. 加载 JSON，得到设备配置 + 寄存器列表
//     auto loadResult = loader.loadFromJSON(entry.path().string());
//     if (loadResult.is_fail()) {
//       spdlog::error("Failed to load {}: {}", entry.path().string(),
//                     loadResult.unwrap_err().what());
//       continue;
//     }

//     auto &result = loadResult.unwrap_returnLeftValue();
//     auto &deviceConfig = result.device_config; // 设备级参数
//     auto &registers = result.registers;        // 寄存器列表

//     // 2. ✅ 创建专属 Mediator 并连接
//     auto mediator = std::make_shared<ModbusMediator>();
//     auto connectResult =
//         mediator->connect(deviceConfig.ip_address, deviceConfig.port);
//     if (connectResult.is_fail()) {
//       spdlog::error("Failed to connect to {}: {}", deviceConfig.ip_address,
//                     connectResult.unwrap_err().what());
//       continue;
//     }

//     // 3. ✅ 存入映射表（以 IP 为键）
//     m_modbusClients[deviceConfig.ip_address] = mediator;

//     // 4. ✅ 遍历寄存器，创建 ModbusDataNode
//     for (auto &reg : registers) {
//       // 填充从站 ID（从设备配置继承）
//       reg.slave_id = deviceConfig.slave_id;

//       // 创建节点，传入 Mediator 和配置
//       auto node = std::make_shared<ModbusDataNode>(mediator, &reg);

//       // 连接信号，让数据自动入队
//       connect(node.get(), &ModbusDataNode::batchDataReady, this,
//               [this](const std::vector<PLCData> &dataVec) {
//                 for (auto &element : dataVec) {
//                   this->m_queue.enqueue(std::move(element));
//                 }
//               });

//       m_modbusNodes.push_back(node);
//       spdlog::debug("Created Modbus node: {} (address: {})", reg.variable_name,
//                     reg.address);
//     }

//     spdlog::info("Loaded {} registers from {}", registers.size(),
//                  entry.path().string());
//   }
// }

bool SystemManager::isHealthy() const {
    // 检查采集线程是否都活着
    
    // 检查队列是否严重积压
    size_t queueSize = m_queue.size_approx();
    if (queueSize > static_cast<size_t>(m_config.maxQueueSize)) {
        return false;
    }
    
    return true;
}

void SystemManager::printStatistics() const {
    spdlog::info("\n=== System Statistics ===");
    
    // 队列统计
    size_t queueSize = m_queue.size_approx();
    spdlog::info("[Queue] Size: {}", queueSize);
    spdlog::info("[Queue] Max size: {}", m_metrics.maxQueueSize);
    
    // 处理统计
    if (dataConsumer_) {
        dataConsumer_->printStatistics();
    }
    
    // 采集统计
    int totalCollected = 0;
    for (const auto& collector : m_collectors) {
        totalCollected += collector->getSequenceCount();
    }
    spdlog::info("[Collectors] Total collected: {}", totalCollected);
    
    // 计算总时间
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - m_metrics.startTime
    ).count();
    spdlog::info("[System] Uptime: {}s", elapsed);
    
    spdlog::info("==========================");
}

void SystemManager::printStatus() const {
    spdlog::info("\n[Status] Running: {}, Queue: {}", 
                 (m_running.load() ? "Yes" : "No"), m_queue.size_approx());
}