#include "PLC_Collector/collector.h"
#include <spdlog/spdlog.h>

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
      spdlog::error("Connection failed: {}", connect_result.unwrap_err().what());
    }

    // 设置超时参数（所有业务模块共用）
    mediator.setResponseTimeout(1, 500); // 1.5秒
  }
    // 启动线程（构造函数中自动启动）
    m_thread = std::thread(&CollectorThread::run, this);
    
    if (m_config.enableLogging) {
        spdlog::info("[Collector {}] Created", m_threadId);
    }
}


CollectorThread::~CollectorThread() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_config.enableLogging) {
        spdlog::info("[Collector {}] Destroyed (success={}, fail={})", 
                     m_threadId, m_successCount, m_failCount);
    }
}

void CollectorThread::run() {
  if (m_config.enableLogging) {
    spdlog::info("[Collector {}] Started", m_threadId);
  }

  // 采集主循环
  while (m_running.load(std::memory_order_acquire)) {
    // 3. 控制采集频率
    std::this_thread::sleep_for(
        std::chrono::milliseconds(m_config.collectIntervalMs));
  }

  if (m_config.enableLogging) {
    spdlog::info("[Collector {}] Stopped", m_threadId);
  }
}
