#include "PLC_Collector/PushGatewayImpl.h"
#include <spdlog/spdlog.h>

PushGatewayImpl::PushGatewayImpl(
    const std::string &pushgateway_url,
    const std::string &job_name ,
    const std::string &instance_name ,
    const std::string &service )
    : pushgateway_url_(pushgateway_url), job_name_(job_name),
      instance_name_(instance_name), service_(service),
      connection_state_(ConnectionState::UNKNOWN), callback_(nullptr) {
    
    spdlog::debug("Enter PushGatewayImpl::PushGatewayImpl (url: {}, job: {}, instance: {}, service: {})",
                  pushgateway_url, job_name, instance_name, service);

    // 初始化CURL全局环境（仅一次）
    static std::once_flag curl_initialized;
    std::call_once(curl_initialized, []() { 
        spdlog::debug("Initializing CURL global environment");
        curl_global_init(CURL_GLOBAL_ALL); 
    });

    auto callback = [](ConnectionState state) {
        if (state == ConnectionState::CONNECTED) {
            spdlog::info("PushGateway connected");
        } else {
            spdlog::info("PushGateway disconnected");
        }
    };
    setConnectionCallback(callback);

    // 初始连接检查
    checkConnection();
    
    spdlog::debug("Leave PushGatewayImpl::PushGatewayImpl");
}

Result<bool, RichError> PushGatewayImpl::send(const SliceRecord &slicerecord) {
    spdlog::debug("Enter PushGatewayImpl::send (tag: {}, value: {})", 
                  slicerecord.tag_name, slicerecord.value);
    
    PushGetWaySlice record = convertToPushGetWaySlice(slicerecord);
    try {
        // 构建Prometheus文本格式
        std::ostringstream body;

        // 添加HELP和TYPE
        if (!record.help_text.empty()) {
            body << "# HELP " << record.metric_name << " " << record.help_text << "\n";
        }
        body << "# TYPE " << record.metric_name << " " << record.metric_type << "\n";

        // 构建指标行（带标签）
        body << record.metric_name;

        // 添加标签
        if (!record.labels.empty()) {
            body << "{";
            bool first = true;
            for (const auto &[key, value] : record.labels) {
                if (!first) body << ",";
                body << key << "=\"" << escapeLabel(value) << "\"";
                first = false;
            }
            // 添加默认标签
            if (!first) body << ",";
            body << "job=\"" << job_name_ << "\",instance=\"" << instance_name_
                 << "\",service=\"" << service_ << "\"";
            body << "}";
        } else {
            body << "{job=\"" << job_name_ << "\",instance=\"" << instance_name_
                 << "\",service=\"" << service_ << "\"}";
        }

        body << " " << record.value << "\n";

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 发送到Pushgateway
            bool success = pushMetrics(body.str());

            if (success) {
                connection_state_ = ConnectionState::CONNECTED;
                spdlog::debug("Leave PushGatewayImpl::send (success)");
                return Result<bool, RichError>{true};
            } else {
                connection_state_ = ConnectionState::DISCONNECTED;
                spdlog::debug("Leave PushGatewayImpl::send (failed to push metrics)");
                return Result<bool, RichError>{RichError{"Failed to push metrics"}};
            }
        }

    } catch (const std::exception &e) {
        connection_state_ = ConnectionState::DISCONNECTED;
        spdlog::error("Leave PushGatewayImpl::send (exception: {})", e.what());
        return Result<bool, RichError>{RichError{e.what()}};
    }
}

Result<bool, RichError>
PushGatewayImpl::sendBatch(const std::vector<SliceRecord> &records) {
    spdlog::debug("Enter PushGatewayImpl::sendBatch (records count: {})", records.size());
    
    if (records.empty()) {
        spdlog::debug("Leave PushGatewayImpl::sendBatch (records empty)");
        return Result<bool, RichError>{RichError{"Records list is empty"}};
    }

    for (size_t idx = 0; idx < records.size(); ++idx) {
        const auto &slicerecord = records[idx];
        PushGetWaySlice record = convertToPushGetWaySlice(slicerecord);
        try {
            std::ostringstream body;

            // 批量构建所有指标
            {
                if (!record.help_text.empty()) {
                    body << "# HELP " << record.metric_name << " " << record.help_text << "\n";
                }
                body << "# TYPE " << record.metric_name << " " << record.metric_type << "\n";

                body << record.metric_name;

                if (!record.labels.empty()) {
                    body << "{";
                    bool first = true;
                    for (const auto &[key, value] : record.labels) {
                        if (!first) body << ",";
                        body << key << "=\"" << escapeLabel(value) << "\"";
                        first = false;
                    }
                    if (!first) body << ",";
                    body << "job=\"" << job_name_ << "\",instance=\"" << instance_name_ << "\"";
                    body << "}";
                } else {
                    body << "{job=\"" << job_name_ << "\",instance=\"" << instance_name_ << "\"}";
                }

                body << " " << record.value << "\n";
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                bool success = pushMetrics(body.str());

                if (success) {
                    connection_state_ = ConnectionState::CONNECTED;
                } else {
                    connection_state_ = ConnectionState::DISCONNECTED;
                    spdlog::debug("Leave PushGatewayImpl::sendBatch (failed at index {}, tag: {})", 
                                  idx, slicerecord.tag_name);
                    return Result<bool, RichError>{
                        RichError{"Failed to push batch metrics at index " + std::to_string(idx)}};
                }
            }

        } catch (const std::exception &e) {
            connection_state_ = ConnectionState::DISCONNECTED;
            spdlog::error("Leave PushGatewayImpl::sendBatch (exception at index {}: {})", 
                          idx, e.what());
            return Result<bool, RichError>{RichError{e.what()}};
        }
    }
    
    spdlog::debug("Leave PushGatewayImpl::sendBatch (success)");
    return Result<bool, RichError>{true};
}

IPushGateway::ConnectionState PushGatewayImpl::getConnectionState() const {
    spdlog::debug("Enter PushGatewayImpl::getConnectionState");
    std::lock_guard<std::mutex> lock(mutex_);
    checkConnection();
    spdlog::debug("Leave PushGatewayImpl::getConnectionState (state: {})", 
                  static_cast<int>(connection_state_));
    return connection_state_;
}

PushGetWaySlice PushGatewayImpl::convertToPushGetWaySlice(const SliceRecord &record) {
    spdlog::debug("Enter PushGatewayImpl::convertToPushGetWaySlice (tag: {})", record.tag_name);
    
    PushGetWaySlice data;
    data.metric_name = record.tag_name;
    data.metric_type = record.metric_type;
    data.help_text = record.help_text;
    data.value = record.value;
    
    spdlog::debug("Leave PushGatewayImpl::convertToPushGetWaySlice");
    return data;
}

bool PushGatewayImpl::isHealthy() const {
    spdlog::debug("Enter PushGatewayImpl::isHealthy");
    std::lock_guard<std::mutex> lock(mutex_);
    bool result = checkConnection();
    spdlog::debug("Leave PushGatewayImpl::isHealthy (result: {})", result);
    return result;
}

// 转义标签值中的特殊字符
std::string PushGatewayImpl::escapeLabel(const std::string &value) {
    spdlog::debug("Enter PushGatewayImpl::escapeLabel (value: {})", value);
    
    std::string escaped;
    for (char c : value) {
        if (c == '"') {
            escaped += "\\\"";
        } else if (c == '\\') {
            escaped += "\\\\";
        } else if (c == '\n') {
            escaped += "\\n";
        } else if (c == '\r') {
            escaped += "\\r";
        } else if (c == '\t') {
            escaped += "\\t";
        } else {
            escaped += c;
        }
    }
    
    spdlog::debug("Leave PushGatewayImpl::escapeLabel (escaped length: {})", escaped.length());
    return escaped;
}

// 实际的HTTP推送逻辑
bool PushGatewayImpl::pushMetrics(const std::string &body_content) {
    spdlog::debug("Enter PushGatewayImpl::pushMetrics (body length: {})", body_content.size());
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        spdlog::debug("Leave PushGatewayImpl::pushMetrics (failed to init CURL)");
        return false;
    }

    // 构建完整URL
    std::string url = pushgateway_url_ + "/metrics/job/" + job_name_ +
                      "/instance/" + instance_name_;
    
    spdlog::debug("pushMetrics: URL = {}", url);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_content.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body_content.size());

    // 设置超时
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    // 设置Headers
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(
        headers, "Content-Type: text/plain; version=0.0.4; charset=utf-8");
    headers = curl_slist_append(headers, "User-Agent: PushGatewayImpl/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // 执行请求
    CURLcode res = curl_easy_perform(curl);

    // 获取HTTP响应码
    long http_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    // 清理
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    // 检查结果
    bool success = (res == CURLE_OK && (http_code == 200 || http_code == 202));
    
    spdlog::debug("pushMetrics: curl result = {}, http_code = {}, success = {}", 
                  res, http_code, success);

    // 触发回调
    if (callback_ && !success) {
        spdlog::debug("pushMetrics: triggering disconnect callback");
        callback_(ConnectionState::DISCONNECTED);
    }

    spdlog::debug("Leave PushGatewayImpl::pushMetrics (success: {})", success);
    return success;
}

bool PushGatewayImpl::checkConnection() const {
    spdlog::debug("Enter PushGatewayImpl::checkConnection");
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        spdlog::debug("Leave PushGatewayImpl::checkConnection (failed to init CURL)");
        return false;
    }

    // 构建健康检查URL
    std::string url = pushgateway_url_ + "/-/healthy";
    
    spdlog::debug("checkConnection: URL = {}", url);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);

    spdlog::debug("before curl_easy_perform(curl)");
    CURLcode res = curl_easy_perform(curl);
    spdlog::debug("after curl_easy_perform(curl)");

    long http_code = 0;
    if (res == CURLE_OK) {
        // curl_easy_getinfo() 获取 HTTP 状态码
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    curl_easy_cleanup(curl);

    bool connected = (res == CURLE_OK && http_code == 200);
    
    spdlog::debug("checkConnection: curl result = {}, http_code = {}, connected = {}", 
                  res, http_code, connected);

    // 如果状态变化，触发回调
    if (callback_) {
        ConnectionState new_state = connected ? ConnectionState::CONNECTED
                                              : ConnectionState::DISCONNECTED;
        if (connection_state_ != new_state) {
            spdlog::debug("checkConnection: state changing from {} to {}", 
                          static_cast<int>(connection_state_), static_cast<int>(new_state));
            const_cast<PushGatewayImpl *>(this)->connection_state_ = new_state;
            callback_(new_state);
        }
    }

    spdlog::debug("Leave PushGatewayImpl::checkConnection (connected: {})", connected);
    return connected;
}