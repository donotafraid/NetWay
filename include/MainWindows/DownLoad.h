#pragma once
#include "load_config/load_config.h"
#include "load_config/Qt_library.h"
#include "MQTTEncryptionClient/MQTTClient.h"

enum class ResponseStatus { Success, Failed, Pending };
enum class EventType { Progress, Complete, Error };

class IDataRepository;
class DownloadTaskAggregate;
class DownloadBusinessHandler;
class DownloadArchitectureCoordinator;

// 消息定义
struct DownloadRequest {
    QString requestId;
    QString fileUrl;
    QString targetFolder;
    QDateTime timestamp;
};

struct ResponseMessage {
    QString requestId;
    ResponseStatus status;  // Success, Failed, Pending
    QVariant data;
    QString errorMessage;
};

struct ExternalEvent {
    EventType type;  // Progress, Complete, Error
    QString taskId;
    QMap<QString, QVariant> data;
};

// 下载任务专用的信号类型
enum class DownloadSignalType {
    ADD_FILE,           // 添加文件
    DELETE_FILE,        // 删除文件
    PAUSE_DOWNLOAD,     // 暂停下载
    RESUME_DOWNLOAD,    // 恢复下载
    QUERY_PROGRESS,     // 查询进度
    MERGE_COMPLETE,     // 合并完成
    SLICE_DOWNLOADED    // 切片下载完成
};

// 下载任务专用信号
struct DownloadSignal {
    DownloadSignalType type;
    QString taskId;
    QVariant data;
    std::function<void(bool, const QString&)> callback;  // 异步回调
    
    DownloadSignal() = default;
    DownloadSignal(DownloadSignalType t, const QString& id, const QVariant& d = QVariant())
        : type(t), taskId(id), data(d) {}
};

class DownloadSignalRouter {
public:
    // 构造/析构函数
    explicit DownloadSignalRouter(DownloadArchitectureCoordinator* manager);
    ~DownloadSignalRouter();
    
    // 禁止拷贝
    DownloadSignalRouter(const DownloadSignalRouter&) = delete;
    DownloadSignalRouter& operator=(const DownloadSignalRouter&) = delete;
    
    // 允许移动构造
    DownloadSignalRouter(DownloadSignalRouter&& other) noexcept;
    DownloadSignalRouter& operator=(DownloadSignalRouter&& other) noexcept;
    
    // 接收信号（线程安全）
    void routeSignal(const DownloadSignal& signal);
    
    // 批量接收信号
    void routeSignals(const std::vector<DownloadSignal>& signals_vector);
    
    // 统计信息结构体
    struct Statistics {
        size_t totalProcessed = 0;
        size_t totalSucceeded = 0;
        size_t totalFailed = 0;
        size_t queueSize = 0;
        std::unordered_map<DownloadSignalType, size_t> processedByType;
    };
    
    // 获取统计信息
    Statistics getStatistics() const;
    
    // 重置统计信息
    void resetStatistics();
    
    // 清空队列
    void clearQueue();
    
    // 获取队列大小
    size_t getQueueSize() const;
    
    // 停止路由器（优雅关闭）
    void stop();
    
    // 重启路由器
    bool restart();

private:
    // 处理循环（在独立线程中运行）
    void processLoop();
    
    // 处理单个信号
    void processSignal(const DownloadSignal& signal);
    
    // 更新统计信息
    void updateStatistics(const DownloadSignal& signal, bool success);
    
    // 处理错误情况
    void handleError(const DownloadSignal& signal, const std::exception& e);
    
    // 检查是否应该停止处理
    bool shouldStop() const { return !m_running; }
    
    // 成员变量
    DownloadArchitectureCoordinator* m_manager;
    std::queue<DownloadSignal> m_queue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running;
    std::thread m_worker;
    
    // 统计信息
    struct StatisticsData {
        std::atomic<size_t> totalProcessed{0};
        std::atomic<size_t> totalSucceeded{0};
        std::atomic<size_t> totalFailed{0};
        std::unordered_map<DownloadSignalType, size_t> byType;
        mutable std::mutex statsMutex;
    };
    std::unique_ptr<StatisticsData> m_stats;
};

// 主动发起请求的客户端
class DownloadRequestClient : public QObject {
    Q_OBJECT
public:
    // 对外API：发起下载请求
    void requestDownload(const DownloadRequest& req);
    void requestPause(const QString& taskId);
    void requestResume(const QString& taskId);
    void requestProgress(const QString& taskId);
    
    // 接收功能响应层的回应
    void onResponse(const ResponseMessage& resp);
    
signals:
    // 向上层（UI/外部系统）发送请求
    void sendDownloadRequest(const DownloadRequest& req);
    void sendPauseRequest(const QString& taskId);
    
private:
    // QMap<QString, RequestCallback> m_pendingCallbacks;
    DownloadSignalRouter* m_responseRouter;
};

// 核心业务逻辑，响应请求并管理状态
class DownloadBusinessHandler : public QObject {
    Q_OBJECT
public:
    explicit DownloadBusinessHandler(IDataRepository* repo);
    
    // 响应外部请求（从请求外部层接收）
    ResponseMessage handleDownloadRequest(const DownloadRequest& req);
    ResponseMessage handlePauseRequest(const QString& taskId);
    ResponseMessage handleProgressQuery(const QString& taskId);
    
    // 响应外部系统的回调
    void onExternalResponse(const ExternalEvent& event);
    
    // 主动推送状态（向上层）
    void emitProgressUpdate(const QString& taskId, float progress);
    
signals:
    // 向下游（请求外部层）发送回应
    void responseReady(const ResponseMessage& resp);
    // 向上游（外部响应层）发送指令
    void sendToExternal(const QByteArray& data);
    
private:
    // 聚合根集合
    QMap<QString, DownloadTaskAggregate*> m_tasks;
    IDataRepository* m_repository;
    
    // 业务逻辑
    ResponseMessage validateRequest(const DownloadRequest& req);
    void updateTaskProgress(const QString& taskId);
};

// // 处理外部系统（MQTT、HTTP等）的响应
// class ExternalResponseHandler : public QObject {
//     Q_OBJECT
// public:
//     explicit ExternalResponseHandler(MqttService* mqtt);
//     // 接收外部系统消息
//     void message_ptr(const QString& topic, const QByteArray& payload);
//     // void onHttpResponse(const HttpResponse& resp);
// signals:
//     // 向上传递外部响应到功能响应层
//     void externalEventReceived(const ExternalEvent& event);
// private:
//     MqttService* m_mqttService;
//     // 将原始消息转换为领域事件
//     ExternalEvent parseToEvent(const QByteArray& raw);
// };

// 协调器（连接三层）
class DownloadArchitectureCoordinator : public QObject {
    Q_OBJECT
public:
    DownloadArchitectureCoordinator() {
        m_requestLayer = new DownloadRequestClient();
        m_businessLayer = new DownloadBusinessHandler(nullptr);
        
        // 连接信号槽
        connect(m_requestLayer, &DownloadRequestClient::sendDownloadRequest,
                m_businessLayer, &DownloadBusinessHandler::handleDownloadRequest);
                
        connect(m_businessLayer, &DownloadBusinessHandler::responseReady,
                m_requestLayer, &DownloadRequestClient::onResponse);
    }
    
private:
    DownloadRequestClient* m_requestLayer;
    DownloadBusinessHandler* m_businessLayer;
    // ExternalResponseHandler* m_externalLayer;
};