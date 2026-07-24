// ============================================================
// IPushGateway.h - 发送网关接口
// ============================================================

#pragma once

#include <string>
#include <functional>
#include "Sqlite_DB/Sqlite_Function.h"
#include "Rust_error_deal/error_deal.h"

// 假设这些类型已定义
struct PushGetWaySlice {
    std::string metric_name;
    std::string help_text;
    std::string metric_type; // "gauge", "counter", etc.
    double value;
    std::map<std::string, std::string> labels;
};

/**
 * 推送网关接口
 * 
 * 职责：定义数据发送的契约
 */
class IPushGateway {
public:
    virtual ~IPushGateway() = default;
    
    // 发送单条数据
    virtual Result<bool,RichError> send(const  SliceRecord&  record) = 0;
    
    // 批量发送
    virtual Result<bool,RichError> sendBatch(const std::vector<SliceRecord>& records) = 0;
    
    // 连接状态
    enum class ConnectionState {
        CONNECTED,
        DISCONNECTED,
        RECONNECTING,
        UNKNOWN
    };
    
    virtual ConnectionState getConnectionState() const = 0;

    // convert SliceData To PushGetWayData
    virtual PushGetWaySlice convertToPushGetWaySlice(const SliceRecord &record) = 0;
    
    // 连接状态变化回调
    using ConnectionCallback = std::function<void(ConnectionState)>;
    virtual void setConnectionCallback(ConnectionCallback callback) = 0;
    
    // 健康检查
    virtual bool isHealthy() const = 0;
};