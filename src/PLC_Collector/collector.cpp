#include "PLC_Collector/collector.h"
#include <iostream>

/**
 * 采集线程实现
 * 对应角色分配中的"采集者"职责
 */
CollectorThread::CollectorThread(
    int threadId,
    moodycamel::ConcurrentQueue<PLCData>& queue,
    std::atomic<bool>& running,
    const SystemConfig& config)
    : m_threadId(threadId)
    , m_queue(queue)
    , m_running(running)
    , m_config(config)
    , m_sequenceCounter(0)
    , m_successCount(0)
    , m_failCount(0) {

  {
    // 2. 建立连接
    auto connect_result = mediator.connect("172.28.80.1", 502);
    if (connect_result.is_fail()) {
      std::cerr << "Connection failed: " << connect_result.unwrap_err().what()
                << std::endl;
    }

    // 设置超时参数（所有业务模块共用）
    mediator.setResponseTimeout(1, 500); // 1.5秒

    // 3. 创建业务模块（都依赖同一个Mediator）
    TemperatureMonitor monitor(mediator);
  }
    // 启动线程（构造函数中自动启动）
    m_thread = std::thread(&CollectorThread::run, this);
    
    if (m_config.enableLogging) {
        std::cout << "[Collector " << m_threadId << "] Created" << std::endl;
    }
}


CollectorThread::~CollectorThread() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_config.enableLogging) {
        std::cout << "[Collector " << m_threadId << "] Destroyed "
                 << "(success=" << m_successCount 
                 << ", fail=" << m_failCount << ")" << std::endl;
    }
}

void CollectorThread::run() {
  if (m_config.enableLogging) {
    std::cout << "[Collector " << m_threadId << "] Started" << std::endl;
  }

  // 采集主循环
  while (m_running.load(std::memory_order_acquire)) {
    // 1. 生成数据
    PLCData data = generateData();

    // 2. 尝试入队
    bool success = tryEnqueue(data);
    if (success) {
      m_successCount++;
    } else {
      m_failCount++;
    }

    // 3. 控制采集频率
    std::this_thread::sleep_for(
        std::chrono::milliseconds(m_config.collectIntervalMs));
  }

  if (m_config.enableLogging) {
    std::cout << "[Collector " << m_threadId << "] Stopped" << std::endl;
  }
}

PLCData CollectorThread::generateData() {
    PLCData data;
    
    auto result = mediator.readHoldingRegisters(1, 0, 1);
    if (result.is_fail()) {
      data.quality = 1; 
      return data;
    }

    data.quality = 0; 
    data.value  = result.unwrap_returnLeftValue()[0] / 10.0f;

    // 模拟PLC读取
    data.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    data.sequenceNum = ++m_sequenceCounter;
    data.collectorId = m_threadId;
    
    return data;
}


bool CollectorThread::tryEnqueue(const PLCData& data) {
    // 非阻塞入队
    bool success = m_queue.enqueue(data);
    
    if (!success && m_config.enableLogging) {
        std::cerr << "[Collector " << m_threadId 
                 << "] Queue full, data lost: " << data.toString() << std::endl;
    }
    
    return success;
}