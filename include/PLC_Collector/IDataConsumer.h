// ============================================================
// IDataConsumer.h - 数据消费者接口
// ============================================================

#pragma once

#include "PLC_Collector/data_types.h"

/**
 * 数据消费者接口
 * 
 * 职责：定义消费数据的契约
 */
class IDataConsumer {
public:
    virtual ~IDataConsumer() = default;
    
    // 处理单条数据
    virtual bool consume(const PLCData& data) = 0;
    
    // 批量处理
    virtual bool consumeBatch(const std::vector<PLCData>& batch) = 0;
    
    // 状态查询
    virtual size_t getProcessedCount() const = 0;
    virtual size_t getErrorCount() const = 0;
    virtual bool isHealthy() const = 0;
    
    // 停止信号
    virtual void shutdown() = 0;
};