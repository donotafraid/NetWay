#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>

// 协议类型字段（opcua/modbus/s7）
enum DataType{
    opcua,
    modbus,
    s7
};

/**
 * 任务分析产出：定义数据结构
 * 
 * 问题：PLC数据需要标准化传输
 * 方案：统一数据格式，包含完整上下文信息
 * 特性：支持序列化、可追溯、易扩展
 */
struct PLCData {
    // 核心数据（从PLC读取的原始信息）
    int64_t timestamp;      // 采集时间戳（毫秒）
    int32_t deviceId;       // PLC设备ID（1-10）
    float value;            // 模拟量数值（0-100）
    uint8_t quality;        // 数据质量（0:良好, 1:异常）
    std::string protocol;
    std::string tag_name;
    
    // 元数据（用于追踪和调试）
    int32_t sequenceNum;    // 全局序列号（从1开始递增）
    uint32_t collectorId;   // 采集线程ID（0-2）
    
    // 构造函数
    PLCData() 
        : timestamp(0)
        , deviceId(0)
        , value(0.0f)
        , quality(0)
        , sequenceNum(0)
        , collectorId(0),protocol("Null"),tag_name("Null"){}
    
    // 格式化输出（用于日志和调试）
    std::string toString() const {
        std::stringstream ss;
        ss << "[dev=" << deviceId 
           << ", val=" << std::fixed << std::setprecision(2) << value
           << ", qual=" << static_cast<int>(quality)
           << ", seq=" << sequenceNum 
           << ", col=" << collectorId
           << ", protocol=" << protocol 
           << ", tag=" << tag_name 
           << ", ts=" << timestamp << "]";
        return ss.str();
    }
    
    // 数据有效性检查
    bool isValid() const {
        return quality == 0 && value >= 0.0f && value <= 100.0f;
    }
};

/**
 * 任务分析产出：系统配置
 * 
 * 问题：系统参数需要灵活调整
 * 方案：集中管理配置项，支持运行时修改
 */
struct SystemConfig {
    // 线程配置
    int collectorThreadCount = 3;    // 采集线程数
    int reporterThreadCount = 1;      // 上报线程数（固定为1）
    
    // 性能参数
    int collectIntervalMs = 4000;      // 采集间隔（毫秒）
    int reportBatchSize = 10;         // 批量上报大小
    int maxQueueSize = 10000;         // 队列告警阈值
    
    // 运行控制
    bool running = true;              // 全局运行标志
    bool enableLogging = true;        // 日志开关
    
    // 协议字段
    DataType datatype = DataType::modbus; 
};
