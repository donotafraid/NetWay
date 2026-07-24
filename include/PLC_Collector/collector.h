#pragma once

#include <thread>
#include <atomic>
#include <random>
#include "PLC_Collector/concurrentqueue.h"
#include "PLC_Collector/data_types.h"
#include "PLC/Modbus.h"

/**
 * 角色分配：采集线程（生产者）
 * 
 * 职责矩阵：
 * - 主任务：循环读取PLC数据
 * - 副任务：数据格式化、添加上下文
 * - 约束：不能阻塞、不能丢失数据
 * - 接口：通过队列与上报线程交互
 */
class CollectorThread {
public:
    /**
     * 构造函数 - 初始化采集线程
     * 
     * @param threadId 线程编号（0,1,2）
     * @param queue 共享队列（与上报线程通信）
     * @param running 全局运行标志（由管理器控制）
     * @param config 系统配置
     */
    CollectorThread(int threadId,
                   moodycamel::ConcurrentQueue<PLCData>& queue,
                   std::atomic<bool>& running,
                   const SystemConfig& config);
    
    /**
     * 析构函数 - 确保线程正确退出
     * RAII原则：资源获取即初始化
     */
    ~CollectorThread();
    
    // 禁止拷贝（线程对象不可复制）
    CollectorThread(const CollectorThread&) = delete;
    CollectorThread& operator=(const CollectorThread&) = delete;
    
    /**
     * 获取线程状态（监控接口）
     */
    int getThreadId() const { return m_threadId; }
    int getSequenceCount() const { return m_sequenceCounter; }
    bool isRunning() const { return m_running.load(); }

private:
    /**
     * 采集主循环（线程入口函数）
     * 
     * 协作流程：
     * 1. 检查运行标志
     * 2. 读取PLC数据（模拟）
     * 3. 封装数据包
     * 4. 入队（非阻塞）
     * 5. 休眠（控制采集频率）
     * 6. 收到停止信号后退出
     */
    void run();
    
    /**
     * 生成模拟数据（模拟PLC读取）
     * 
     * 任务分解：
     * - 模拟设备ID（1-10随机）
     * - 模拟数值（0-100随机）
     * - 模拟异常（10%概率quality=1）
     * - 添加时间戳和序列号
     */
    PLCData generateData();
    
    /**
     * 尝试入队（容错处理）
     * 
     * 协作点：队列满时的处理策略
     * - 策略：记录日志，丢弃数据（不阻塞采集）
     * - 理由：采集不能停，宁可丢数据也不能卡死
     */
    bool tryEnqueue(const PLCData& data);

    //  ===Modbus读取对象===
    ModbusMediator mediator;

    // === 状态变量 ===
    int m_threadId;                                 // 线程编号
    moodycamel::ConcurrentQueue<PLCData>& m_queue;  // 共享队列（引用）
    std::atomic<bool>& m_running;                   // 运行标志（引用）
    SystemConfig m_config;                          // 系统配置（拷贝）
    std::thread m_thread;                           // 线程句柄
    
    // === 统计变量 ===
    int m_sequenceCounter;      // 序列号计数器（每线程独立）
    int m_successCount;         // 成功入队计数
    int m_failCount;            // 入队失败计数
};
