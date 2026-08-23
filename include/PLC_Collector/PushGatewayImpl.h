// ============================================================
// PushGatewayImpl.h - 推送网关实现
// ============================================================
#pragma once

#include "PLC_Collector/IPushGateway.h"
#include <curl/curl.h>

using ConnectionCallback = std::function<void(IPushGateway::ConnectionState)>;

// ============ PushGatewayImpl 实现 ============

class PushGatewayImpl : public IPushGateway {
public:
  explicit PushGatewayImpl(
      const std::string &pushgateway_url = "http://localhost:9092",
      const std::string &job_name = "my_cpp_app",
      const std::string &instance_name = "gateway_1",
      const std::string &service = "myapp");  

  ~PushGatewayImpl() {
    // 不需要每次都清理全局CURL，在程序退出时清理
    // 但可以清理实例特定的资源
  }

  Result<bool, RichError> send(const SliceRecord &record) override; 

  Result<bool, RichError>
  sendBatch(const std::vector<SliceRecord> &records) override; 

  ConnectionState getConnectionState() const override; 

  void setConnectionCallback(ConnectionCallback callback) override {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = callback;
  }

  bool isHealthy() const override ;

  PushGetWaySlice convertToPushGetWaySlice(const SliceRecord &record) override;
private:
  std::string pushgateway_url_;
  std::string job_name_;
  std::string instance_name_;
  std::string service_;
  ConnectionState connection_state_;
  ConnectionCallback callback_;
  mutable std::mutex mutex_;

  // 转义标签值中的特殊字符
  std::string escapeLabel(const std::string &value); 

  // 实际的HTTP推送逻辑
  bool pushMetrics(const std::string &body_content);

  // ============================================================
  //  业务转换
  // ============================================================
 
  PushGetWaySlice toPushGetWayMessage(const SliceRecord &record) {
    PushGetWaySlice msg;
    msg.metric_name = record.tag_name;
    msg.help_text = record.help_text;
    msg.metric_type = record.metric_type;
    msg.value = record.value;
    return msg;
  }

  /**
   * 批量转换
   */
  std::vector<PushGetWaySlice>
  toPushGetWayMessages(const std::vector<SliceRecord> &records) {
    std::vector<PushGetWaySlice> messages;
    messages.reserve(records.size());
    for (const auto &record : records) {
      messages.push_back(toPushGetWayMessage(record));
    }
    return messages;
  }

  bool checkConnection() const; 
};