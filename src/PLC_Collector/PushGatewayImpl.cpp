#include "PLC_Collector/PushGatewayImpl.h"

PushGatewayImpl::PushGatewayImpl(
    const std::string &pushgateway_url,
    const std::string &job_name ,
    const std::string &instance_name ,
    const std::string &service )
    : pushgateway_url_(pushgateway_url), job_name_(job_name),
      instance_name_(instance_name), service_(service),
      connection_state_(ConnectionState::UNKNOWN), callback_(nullptr) {
    
    std::cout << "Enter PushGatewayImpl::PushGatewayImpl (url: " << pushgateway_url 
              << ", job: " << job_name << ", instance: " << instance_name 
              << ", service: " << service << ")" << std::endl;

    // 初始化CURL全局环境（仅一次）
    static std::once_flag curl_initialized;
    std::call_once(curl_initialized, []() { 
        std::cout << "Initializing CURL global environment" << std::endl;
        curl_global_init(CURL_GLOBAL_ALL); 
    });

    auto callback = [](ConnectionState state) {
        if (state == ConnectionState::CONNECTED) {
            std::cout << "PushGateway connected" << std::endl;
        } else {
            std::cout << "PushGateway disconnected" << std::endl;
        }
    };
    setConnectionCallback(callback);

    // 初始连接检查
    checkConnection();
    
    std::cout << "Leave PushGatewayImpl::PushGatewayImpl" << std::endl;
}

Result<bool, RichError> PushGatewayImpl::send(const SliceRecord &slicerecord) {
    std::cout << "Enter PushGatewayImpl::send (tag: " << slicerecord.tag_name 
              << ", value: " << slicerecord.value << ")" << std::endl;
    
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
                std::cout << "Leave PushGatewayImpl::send (success)" << std::endl;
                return Result<bool, RichError>{true};
            } else {
                connection_state_ = ConnectionState::DISCONNECTED;
                std::cout << "Leave PushGatewayImpl::send (failed to push metrics)" << std::endl;
                return Result<bool, RichError>{RichError{"Failed to push metrics"}};
            }
        }

    } catch (const std::exception &e) {
        connection_state_ = ConnectionState::DISCONNECTED;
        std::cout << "Leave PushGatewayImpl::send (exception: " << e.what() << ")" << std::endl;
        return Result<bool, RichError>{RichError{e.what()}};
    }
}

Result<bool, RichError>
PushGatewayImpl::sendBatch(const std::vector<SliceRecord> &records) {
    std::cout << "Enter PushGatewayImpl::sendBatch (records count: " << records.size() << ")" << std::endl;
    
    if (records.empty()) {
        std::cout << "Leave PushGatewayImpl::sendBatch (records empty)" << std::endl;
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
                    std::cout << "Leave PushGatewayImpl::sendBatch (failed at index " << idx 
                              << ", tag: " << slicerecord.tag_name << ")" << std::endl;
                    return Result<bool, RichError>{
                        RichError{"Failed to push batch metrics at index " + std::to_string(idx)}};
                }
            }

        } catch (const std::exception &e) {
            connection_state_ = ConnectionState::DISCONNECTED;
            std::cout << "Leave PushGatewayImpl::sendBatch (exception at index " << idx 
                      << ": " << e.what() << ")" << std::endl;
            return Result<bool, RichError>{RichError{e.what()}};
        }
    }
    
    std::cout << "Leave PushGatewayImpl::sendBatch (success)" << std::endl;
    return Result<bool, RichError>{true};
}

IPushGateway::ConnectionState PushGatewayImpl::getConnectionState() const {
    std::cout << "Enter PushGatewayImpl::getConnectionState" << std::endl;
    std::lock_guard<std::mutex> lock(mutex_);
    checkConnection();
    std::cout << "Leave PushGatewayImpl::getConnectionState (state: " 
              << static_cast<int>(connection_state_) << ")" << std::endl;
    return connection_state_;
}

PushGetWaySlice PushGatewayImpl::convertToPushGetWaySlice(const SliceRecord &record) {
    std::cout << "Enter PushGatewayImpl::convertToPushGetWaySlice (tag: " 
              << record.tag_name << ")" << std::endl;
    
    PushGetWaySlice data;
    data.metric_name = record.tag_name;
    data.metric_type = record.metric_type;
    data.help_text = record.help_text;
    data.value = record.value;
    
    std::cout << "Leave PushGatewayImpl::convertToPushGetWaySlice" << std::endl;
    return data;
}

bool PushGatewayImpl::isHealthy() const {
    std::cout << "Enter PushGatewayImpl::isHealthy" << std::endl;
    std::lock_guard<std::mutex> lock(mutex_);
    bool result = checkConnection();
    std::cout << "Leave PushGatewayImpl::isHealthy (result: " << result << ")" << std::endl;
    return result;
}

// 转义标签值中的特殊字符
std::string PushGatewayImpl::escapeLabel(const std::string &value) {
    std::cout << "Enter PushGatewayImpl::escapeLabel (value: " << value << ")" << std::endl;
    
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
    
    std::cout << "Leave PushGatewayImpl::escapeLabel (escaped length: " << escaped.length() << ")" << std::endl;
    return escaped;
}

// 实际的HTTP推送逻辑
bool PushGatewayImpl::pushMetrics(const std::string &body_content) {
    std::cout << "Enter PushGatewayImpl::pushMetrics (body length: " << body_content.size() << ")" << std::endl;
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        std::cout << "Leave PushGatewayImpl::pushMetrics (failed to init CURL)" << std::endl;
        return false;
    }

    // 构建完整URL
    std::string url = pushgateway_url_ + "/metrics/job/" + job_name_ +
                      "/instance/" + instance_name_;
    
    std::cout << "pushMetrics: URL = " << url << std::endl;

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
    
    std::cout << "pushMetrics: curl result = " << res << ", http_code = " << http_code 
              << ", success = " << success << std::endl;

    // 触发回调
    if (callback_ && !success) {
        std::cout << "pushMetrics: triggering disconnect callback" << std::endl;
        callback_(ConnectionState::DISCONNECTED);
    }

    std::cout << "Leave PushGatewayImpl::pushMetrics (success: " << success << ")" << std::endl;
    return success;
}

bool PushGatewayImpl::checkConnection() const {
    std::cout << "Enter PushGatewayImpl::checkConnection" << std::endl;
    
    CURL *curl = curl_easy_init();
    if (!curl) {
        std::cout << "Leave PushGatewayImpl::checkConnection (failed to init CURL)" << std::endl;
        return false;
    }

    // 构建健康检查URL
    std::string url = pushgateway_url_ + "/-/healthy";
    
    std::cout << "checkConnection: URL = " << url << std::endl;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);

    std::cout << "before curl_easy_perform(curl)" << std::endl;
    CURLcode res = curl_easy_perform(curl);
    std::cout << "after curl_easy_perform(curl)" << std::endl;

    long http_code = 0;
    if (res == CURLE_OK) {
        // curl_easy_getinfo() 获取 HTTP 状态码
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    curl_easy_cleanup(curl);

    bool connected = (res == CURLE_OK && http_code == 200);
    
    std::cout << "checkConnection: curl result = " << res << ", http_code = " << http_code 
              << ", connected = " << connected << std::endl;

    // 如果状态变化，触发回调
    if (callback_) {
        ConnectionState new_state = connected ? ConnectionState::CONNECTED
                                              : ConnectionState::DISCONNECTED;
        if (connection_state_ != new_state) {
            std::cout << "checkConnection: state changing from " 
                      << static_cast<int>(connection_state_) << " to " 
                      << static_cast<int>(new_state) << std::endl;
            const_cast<PushGatewayImpl *>(this)->connection_state_ = new_state;
            callback_(new_state);
        }
    }

    std::cout << "Leave PushGatewayImpl::checkConnection (connected: " << connected << ")" << std::endl;
    return connected;
}