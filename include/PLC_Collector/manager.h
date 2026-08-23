#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include "PLC_Collector/data_types.h"
#include "PLC_Collector/collector.h"
#include "PLC_Collector/DataConsumer.h"
#include "PLC_Collector/PushGatewayImpl.h"
#include "PLC_Collector/MessageSendService.h"

#include "Sqlite_DB/SQLiteCacheRepository.h"

/**
 * 统筹协作：系统管理器（指挥者）
 * 
 * 职责矩阵：
 * - 主任务：启动和监控整个系统
 * - 副任务：线程协调、异常处理、优雅关闭
 * - 约束：不参与数据流转，只做管理
 * - 接口：对外提供启动/停止/监控API
 * 
 * 协作模式：观察者模式 + 指挥者模式
 */
class SystemManager  {
public:
  SystemManager();
  ~SystemManager();

  /**
   * 启动系统（开始协作）
   *
   * 协作流程：
   * Phase 1: 创建所有采集线程（3个）
   * Phase 2: 创建上报线程（1个）
   * Phase 3: 启动监控循环
   * Phase 4: 等待信号
   */
  void start();

  /**
   * 停止系统（结束协作） - 最重要的协作点
   *
   * 协作流程（按顺序，不可颠倒）：
   * Step 1: 发送停止信号（running = false）
   * Step 2: 等待采集线程退出（join）
   * Step 3: 上报线程处理完剩余数据
   * Step 4: 等待上报线程退出（join）
   * Step 5: 打印最终统计
   *
   * 设计原则：先停生产者，再停消费者，确保数据不丢失
   */
  void stop();

  /**
   * 系统状态查询（监控接口）
   */
  bool isRunning() const { return m_running.load(); }
  size_t getQueueSize() const { return m_queue.size_approx(); }
  int getCollectorCount() const { return m_config.collectorThreadCount; }

  /**
   * 健康检查（监控功能）
   */
  bool isHealthy() const;
  void printStatus() const;

private:
  /**
   * 监控循环（独立线程，可选）
   *
   * 监控维度：
   * 1. 队列深度（是否积压）
   * 2. 吞吐量（每秒处理数据量）
   * 3. 采集线程状态（是否卡死）
   * 4. 上报线程状态（是否正常）
   *
   * 告警策略：
   * - 队列>80%：警告
   * - 队列>100%：严重警告
   * - 吞吐量下降>50%：可能故障
   */
  void monitorLoop();

  /**
   * 打印统计信息（监控输出）
   */
  void printStatistics() const;

  /**
   * 计算性能指标
   */

  // monitor function
  void onPushGetWayConnected();
  void onPushGetWayDisconnected();
  size_t getBacklogCount() const;

  //  initialize function
  // void loadModbusDevices();

  // === 核心组件 ===
  moodycamel::ConcurrentQueue<PLCData> m_queue; // 共享队列
  std::atomic<bool> m_running;                  // 全局运行标志
  SystemConfig m_config;                        // 系统配置

  // === 工作线程 ===
  std::vector<std::unique_ptr<CollectorThread>> m_collectors; // 采集线程池
  std::unique_ptr<DataConsumer> dataConsumer_;

  // === 监控线程（可选） ===
  std::unique_ptr<std::thread> m_monitorThread;

  // === 新增：存储和发送协调 ===
  std::shared_ptr<ConnectionPool> pool_;
  std::shared_ptr<SQLiteSliceExecutor> executor_;
  std::shared_ptr<SQLiteCacheRepository> repository_;

  std::unique_ptr<PushGatewayImpl> push_gateWay_;
  std::shared_ptr<MessageSendService> send_service_;

  //  Modbus module
  // ✅ 关键：一个 IP 对应一个 Mediator
  std::unordered_map<std::string, std::shared_ptr<ModbusMediator>>
      m_modbusClients;

  // ModbusConfigLoader loader;
  // // ✅ 存储所有加载的设备节点（方便信号连接）
  // std::vector<std::shared_ptr<ModbusDataNode>> m_modbusNodes;
  // === 性能指标 ===
  struct Metrics {
    std::chrono::steady_clock::time_point startTime;
    size_t maxQueueSize = 0;
    size_t avgQueueSize = 0;
    int totalProcessed = 0;
    int totalErrors = 0;
    double throughputPerSecond = 0.0;
  } m_metrics;
};
